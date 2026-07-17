# 22. `FileCacheFactory` 文件迁移设计

## 结论

本批次严格按两个 CH 文件 review：

```text
src/Interpreters/FileCache/FileCacheFactory.h
src/Interpreters/FileCache/FileCacheFactory.cpp
```

Velox 不实现第二套 registry。CH factory行为并入：

```text
velox/ch/Interpreters/FileCache/FileCacheManager.h
velox/ch/Interpreters/FileCache/FileCacheManager.cpp
```

并提供 compatibility header：

```text
velox/ch/Interpreters/FileCache/FileCacheFactory.h
```

内容只包含：

```cpp
#include <velox/ch/Interpreters/FileCache/FileCacheManager.h>

namespace facebook::velox::ch
{
using FileCacheFactory = FileCacheManager;
}
```

不创建 target `FileCacheFactory.cpp`。

This batch is an exact registry, name/path deduplication, settings-snapshot, and reload-semantics port, extended with the runtime ownership already assigned to FileCacheManager.

## 为什么不是两个类

CH `FileCacheFactory` 已负责：

```text
singleton registry
multiple named caches
same-path deduplication
settings snapshots
config reload
create/get/remove/clear
```

Velox `FileCacheManager` 额外持有：

```text
FileCacheScheduler
FileCacheWorkerPool
OpenedFileCache
local filesystem / memory pool
stable commonUserId
explicit shutdown
```

这些 runtime resources 在 CH 来自 global `Context` / global pool / singleton。

所以目标结构是：

```text
FileCacheManager
  = CH FileCacheFactory registry semantics
  + Velox runtime-resource ownership

FileCacheFactory
  = compatibility alias only
```

不能让 Factory 和 Manager各自持有 mutex/map，否则 name/path identity、settings reload和
shutdown会分叉。

## `FileCacheFactory.h`

### `FileCacheData`

把 CH nested data结构保留在 `FileCacheManager`：

```cpp
class FileCacheManager final
{
public:
    class FileCacheData
    {
    public:
        FileCacheData(
            FileCachePtr cache,
            const FileCacheConfig & config,
            std::string config_path);

        FileCacheConfig getSettings() const;
        void setSettings(FileCacheConfig config);

        const FileCachePtr cache;
        const std::string config_path;

    private:
        mutable std::mutex settings_mutex_;
        FileCacheConfig settings_;
    };
};
```

保留 `getSettings` / `setSettings` 名字，减少 admin/reload调用差异；真实类型是
`FileCacheConfig`。

`cache` 和 canonical `config_path` immutable。settings snapshot单独加锁，使 reload不需要
持有 registry mutex。

### config name分离

CH API把 name和 settings分开：

```cpp
getOrCreate(cache_name, settings, config_path);
```

因此 `FileCacheConfig` 不包含 `name`。manager配置使用：

```cpp
struct NamedFileCacheConfig
{
    std::string name;
    FileCacheConfig config;
    std::string configPath;
};
```

这保留：

```text
different names + same path + same settings
  -> alias one FileCacheData/FileCache
```

如果 `name` 参与 settings equality，该合法 alias会被误判为配置冲突。

### containers

CH：

```cpp
using CacheByName =
    std::unordered_map<std::string, FileCacheDataPtr>;
using Caches =
    std::unordered_set<FileCacheDataPtr>;
```

Velox：

```cpp
using CacheByName =
    folly::F14FastMap<std::string, FileCacheDataPtr>;
using Caches =
    folly::F14FastSet<FileCacheDataPtr>;
```

安全依据：

```text
no iterator/reference escapes registry mutex
getAll returns a copied map
getUniqueInstances returns copied shared_ptr set
FileCacheData address is stable through shared_ptr
```

### singleton API

真实 owner由外部创建：

```cpp
static FileCacheManager * getInstance();
static void setInstance(FileCacheManager * manager);
```

为 alias兼容增加：

```cpp
static FileCacheManager & instance();
```

`instance` 对 null global pointer明确失败；不能 lazy-create无 runtime dependencies 的默认
manager。

owner sequence：

```text
manager = FileCacheManager::create(options)
FileCacheManager::setInstance(manager.get())
...
manager->shutdown()
FileCacheManager::setInstance(nullptr)
```

global pointer不拥有 manager。

### API

保留 registry API：

```text
getOrCreate
create
get
getAll
getUniqueInstances
getByName
remove
clear
```

增加 manager runtime API：

```text
getDefault
initialize
shutdown
refreshStats
```

## `FileCacheFactory.cpp`

### registry lock

一个 mutex保护：

```text
name -> FileCacheDataPtr map
default cache name
unique-cache worker budgets
shutdown state
```

settings mutex只保护 `FileCacheData::settings_`。

lock order：

```text
manager registry mutex
  -> FileCacheData settings mutex
```

reload在 registry snapshot后释放 manager mutex，再读取/写入 settings；不能反向获取。

### `get`

直接迁移：

```text
lock registry
find name
missing -> explicit error
return copied FileCachePtr
```

### `getOrCreate`

目标算法按以下顺序实现：

```text
lock registry

if name already exists:
  if existing effective settings == requested settings:
    return existing cache
  otherwise:
    reject name rebind

find existing unique cache by normalized path

if path exists:
  settings must equal
  add new name alias to same FileCacheData
  return shared cache

otherwise:
  reserve/grow worker-pool budget
  construct FileCacheData/FileCache
  insert name
  return new cache
```

### CH name-rebind edge bug

CH 当前在这个无效调用序列下结果不一致：

```text
A -> /old
B -> /new
getOrCreate("A", settings(/new))
```

它按 `/new` 找到 B，尝试 `emplace("A", B)`；插入失败结果未检查，但函数返回 B：

```text
this call returns B
later get("A") still returns old A
```

target明确拒绝同名不同 path/settings。该 fail-fast只影响冲突 registry调用，不改变合法
配置行为。

### `create`

保留与 `getOrCreate` 的差异：

```text
name already exists -> always error
same path + equal settings under a new name -> create alias
same path + different settings -> error
new path -> create unique cache
```

### path identity

比较 `FileCacheConfig.path` 的已授权、absolute、normalized值。manager不重新解析 raw path。

CH/target都按 path string dedup；`StatusFile` process lock仍是遗漏 alias/symlink的最终保护。

### settings equality

settings equality不包含：

```text
cache name
config source path
```

它比较所有 effective `FileCacheConfig` fields，包括 normalized path和 ratio-derived
effective maxSize。

同 path但任一 effective field不同必须拒绝，不能让两个算法实例共享同一 on-disk
directory。

### `getAll` / `getUniqueInstances`

`getAll`：

```text
return name -> data snapshot
aliases appear separately
```

`getUniqueInstances`：

```text
deduplicate FileCacheDataPtr
one entry per actual FileCache
```

shutdown、stats、worker-budget和 system-wide admin操作必须遍历 unique instances，不能按
name重复执行。

### default cache

`getDefault` 等价于：

```text
get(defaultCacheName)
```

default name必须在 initialization结束时存在。

### default config load

CH `loadDefaultCaches(Poco config, Context)` 替换为 manager initialization：

```text
FileCacheSettingsLoader parses NamedFileCacheConfig list
getOrCreate each entry
initialize each unique cache exactly once
```

name aliases不能重复 initialize同一个 cache。

### reload

不迁移 Poco config polling。manager接收新的：

```cpp
std::vector<NamedFileCacheConfig>
```

对每个 unique cache：

```text
load current actual settings snapshot
match requested canonical config source/name
keep path immutable
FileCache::applySettingsIfPossible(new, actual)
store returned/mutated actual snapshot even after partial apply exception
```

不能在 registry mutex下调用 `applySettingsIfPossible`，因为它可能 resize、spawn/join workers
或等待 filesystem eviction。

alias config sources：

```text
all aliases for one FileCache must resolve to equal requested settings
one canonical configPath is stored in FileCacheData
```

target可以在 reload时校验所有 aliases，避免 CH 只跟踪首个 config path导致的 silent
divergence；这是 conflict detection，不改变合法 alias行为。

### worker-pool budget

只有创建新的 unique `FileCache` 才增加 budget；新增 name alias不增加。

流程：

```text
compute old/new manager workerPoolMax
grow FileCacheWorkerPool first
construct/register cache
on construction/insertion failure:
  roll back budget/max
```

cache initialization可以在 registry lock外执行，但同一个 cache只能被一个 initialization
owner触发；`FileCache::initialize` 的 `once_flag` 仍是最终保护。

### remove

`remove(cache)` 删除所有指向该 cache的 aliases。

target不能在 registry mutex下执行 cache shutdown/destruction。流程：

```text
under lock:
  erase aliases
  retain FileCacheDataPtr snapshot

outside lock:
  deactivate/shutdown cache if removal means lifecycle end
  release snapshot
  then lower worker-pool max
```

如果外部仍持有 `FileCachePtr`，registry removal和runtime shutdown必须由 caller lifecycle
明确区分；不能缩减仍运行 cache的 worker budget。

第一阶段 manager-owned cache removal只允许在显式 shutdown/deactivate后进行。

### `clear` / shutdown

CH `clear` 只清 registry；Velox manager还拥有 shared runtime resources。

manager shutdown：

```text
under registry lock:
  set shutdown
  snapshot unique FileCacheData
  clear name map

outside lock:
  deactivate every unique FileCache
  scheduler shutdown
  worker pool shutdown
  opened-file cache clear
```

不能在 registry mutex下 join workers或销毁 caches。

测试用 `clear` 可以复用同一流程，但 manager如需继续使用，必须重新建立 runtime resources；
第一阶段建议 `clear` 仅用于 terminal shutdown/test teardown。

### path prefix helper

CH：

```cpp
getPathPrefixForRelativeCachePath(ContextPtr)
```

不迁移。Velox `FileCacheManager::Options` 已显式提供：

```text
cachePathPrefix
allowedCacheRoot
```

path resolution/authorization属于 `21` 的 settings loader。

## tests

### registry

```text
create/get one cache
missing get fails
same name + equal settings getOrCreate returns same cache
same name + different path/settings rejected
different names + same path/settings alias same FileCacheData
same path + different settings rejected
create rejects existing name
getAll includes aliases
getUniqueInstances deduplicates aliases
remove erases all aliases
```

### conflict regression

```text
A -> /old
B -> /new
getOrCreate("A", /new settings) fails
get("A") remains /old
```

### lifecycle/resources

```text
new unique cache grows worker max once
name alias does not grow worker max
initialization runs once per unique cache
shutdown processes each unique cache once
no cache destruction/join under registry mutex
global instance requires explicit manager installation
```

### reload

```text
equal config no-op
reload only unique caches
alias requested configs must remain equal
path change rejected
partial apply stores actual snapshot
reload does not hold registry mutex across apply
```

## Review 状态

`FileCacheFactory.h` 和 `FileCacheFactory.cpp` 已按文件 review。registry、name/path
dedup、settings snapshot和 reload semantics并入真实 `FileCacheManager`；通过
`using FileCacheFactory = FileCacheManager` 保留 CH 名称，不实现第二套 registry。
