# 02. `FileCacheManager` 文件设计

## 结论

本批次严格按两个目标文件 review：

```text
velox/ch/Interpreters/FileCache/FileCacheManager.h
velox/ch/Interpreters/FileCache/FileCacheManager.cpp
```

`FileCacheManager` 没有单个 CH 同名源文件。它组合：

```text
CH FileCacheFactory registry semantics       -> ../2-file-cache/12
CH global Context/runtime dependencies       -> explicit ownership
FileCache scheduler                          -> ../1-dependencies/05
global/local worker-pool mapping             -> ../1-dependencies/04
opened local file handles                    -> BufferedInput / Metadata / FileSegment
settings loader and runtime apply            -> ../2-file-cache/06
```

Manager包含一个真实 `FileCacheFactory`。只有 Factory持有 registry，只有 Manager持有
runtime resources。

## 文件功能

Manager职责：

```text
process-level runtime owner
default cache lookup
shared dynamic physical worker pool
shared scheduled-task timer/runtime
manager-owned opened-file cache
stable common user identity
cache initialize/clear/shutdown
settings reload orchestration and worker-budget updates
stats/config snapshots
```

registry/name/path dedup由 contained `FileCacheFactory` 实现。Manager不实现
`FileCache` lookup/reservation/eviction算法。

dependency direction：

```text
FileCacheManager
  -> owns runtime resources
  -> owns FileCacheFactory

FileCacheFactory
  -> owns registry
  -> constructs FileCache with Manager-owned explicit dependencies

FileCache
  -> never calls or stores FileCacheManager/FileCacheFactory
```

这样 `FileCache` 算法可独立测试，也避免 manager/cache ownership cycle。

### 与 `AsyncDataCache` 的边界

Manager只参考 Velox `AsyncDataCache` 的 process-level lifecycle模式：

| `AsyncDataCache` | `FileCacheManager` |
|---|---|
| `create` | `create` |
| `getInstance` / `setInstance` | `getInstance` / `setInstance` |
| `shutdown` | `shutdown` |
| `refreshStats` / `toString` | `refreshStats` / `toString` |

它不继承 `memory::Cache`，不复用 `AsyncDataCache` shards，也不改变 ClickHouse
`FileCache` 的 metadata、priority或 eviction算法。

### 与 scan读路径的边界

`FileCacheBufferedInput` 不创建 cache，也不在读热路径解析实例配置：

```text
scan setup / BufferedInput builder
  -> manager.getDefault() or manager.get(cacheName)
  -> FileCacheBufferedInput(
         FileCachePtr,
         FileCacheReadOptions,
         FileCacheRequestContext)
```

Manager持有的 `OpenedFileCache` 只服务本地 cache segment，不复用 Hive source-file
handle cache。`FileSegment` / `Metadata` 通过注入的 reference或 callback使已删除路径的
handle失效。

## `FileCacheManager.h`

### supporting types

```cpp
struct NamedFileCacheConfig
{
    std::string name;
    FileCacheConfig config;
    std::string configPath;
};
```

name/configPath不参与 `FileCacheConfig` equality，详见
[`FileCacheSettings`](../2-file-cache/06-filecache-settings-files-design.md)和
[`FileCacheFactory`](../2-file-cache/12-filecache-factory-files-design.md)。

```cpp
struct FileCacheManagerStats
{
    folly::F14FastMap<std::string, FileCacheStats> cachesByName;
    FileHandleCacheStats openedFileCache;
    size_t uniqueCaches = 0;
    size_t workerPoolMax = 0;
    size_t workerPoolActive = 0;
};
```

alias names可分别出现在 `cachesByName`；`uniqueCaches` 去重实际 instances。

### `Options`

```cpp
struct Options
{
    std::vector<NamedFileCacheConfig> caches;
    std::string defaultCacheName;

    std::string commonUserId;
    std::string cachePathPrefix;
    std::string allowedCacheRoot;

    std::shared_ptr<filesystems::FileSystem> localFileSystem;
    memory::MemoryPool * memoryPool = nullptr;
    std::shared_ptr<folly::Timekeeper> timekeeper;

    bool initializeOnCreate = true;
};
```

required validation：

```text
commonUserId non-empty and != "internal"
cachePathPrefix / allowedCacheRoot absolute and normalized
localFileSystem non-null and local
memoryPool non-null and outlives manager
timekeeper non-null
defaultCacheName resolves after registry construction
all NamedFileCacheConfig names non-empty
```

settings loader在进入 manager前完成 path authorization和 effective config derivation。

### creation

```cpp
static std::shared_ptr<FileCacheManager> create(Options options);
```

constructor只构造成员，不启动 callback/worker。`create` 执行 two-phase initialization：

```text
validate options
deduplicate named configs by normalized path/settings
compute workerPoolMax
construct manager/resources
register all caches
optionally initialize unique caches
return shared manager
```

不能在 C++ constructor中启动捕获 `this` 的 background work；后续步骤抛异常时对象
destructor未必能看到完整构造状态。

### singleton entry

```cpp
static FileCacheManager * getInstance();
static FileCacheManager & instance();
static void setInstance(FileCacheManager * manager);
```

实现使用：

```cpp
static std::atomic<FileCacheManager *> global_instance;
```

contract：

```text
set null -> manager exactly once during startup
set same manager -> allowed/no-op
replace live different manager -> error
set manager -> null only after shutdown
instance when null -> explicit error
```

acquire/release ordering保证已构造 manager在 publish后可见。raw pointer不拥有生命周期。

`setInstance` 同步管理两个 global entries：

```text
install:
  Manager pointer
  Manager-owned Factory pointer

uninstall:
  Factory pointer
  Manager pointer
```

不允许只安装其中一个。Factory pointer先清除，阻止新的 registry users进入正在 shutdown的
manager。

### registry API

Manager exposes：

```cpp
FileCacheFactory & factory();
const FileCacheFactory & factory() const;
FileCachePtr get(const std::string & name) const;
FileCachePtr getDefault() const;
```

registry API本体位于
[`FileCacheFactory`](../2-file-cache/12-filecache-factory-files-design.md)。

### runtime API

```cpp
void initialize();
void applyConfigs(
    const std::vector<NamedFileCacheConfig> & configs);
void shutdown();

FileCacheManagerStats refreshStats() const;
std::string toString(bool details = true) const;

OpenedFileCache & openedFileCache();
FileCacheWorkerPool & workerPool();
FileCacheScheduler & scheduler();
FileCacheFactory & factory();
```

不要向 query/reader暴露 raw executor；它们只接收具体 `FileCachePtr` 和 opened-file service。

### lifecycle state

```cpp
enum class State
{
    Created,
    Initialized,
    ShuttingDown,
    Shutdown,
};
```

state在 Manager lifecycle mutex下访问。

允许：

```text
Created -> Initialized
Created -> ShuttingDown -> Shutdown
Initialized -> ShuttingDown -> Shutdown
```

`clear` 不改变 manager state；它只移除/deactivate caches。

shutdown idempotent。进入 `ShuttingDown` 后拒绝 create/getOrCreate/applyConfigs。

### member order

声明顺序：

```cpp
Options/static identity values
shared_ptr localFileSystem_
memory::MemoryPool * memoryPool_
shared_ptr<folly::Timekeeper> timekeeper_

FileCacheWorkerPool workerPool_
FileCacheScheduler scheduler_
OpenedFileCache openedFileCache_
FileCacheFactory factory_

lifecycle mutex/state
```

更精确的 destruction requirement：

```text
factory/registry references destroyed before opened-file cache
opened-file cache destroyed before scheduler/worker pool
scheduler destroyed before worker pool
```

因为 C++ 按声明逆序析构，`factory_` 必须声明在 resources之后。

`shutdown` 仍必须显式执行；成员顺序只是异常/析构兜底。

## `FileCacheManager.cpp`

### option/config validation

Manager不重复解析 scalar settings。Factory按
[`FileCacheFactory` 设计](../2-file-cache/12-filecache-factory-files-design.md)验证 name/path
registry约束；
Manager验证 runtime/options/default cache：

```text
default name exists
```

Named configs按 input order处理；同一路径第一个 name成为 actual `FileCache::name`，后续是
aliases。该顺序应 deterministic。

### worker max计算

通过 Factory validation按 normalized path/settings dedup unique configs。

每个 unique cache：

```text
cacheWorkerMax =
  loadMetadataThreads
  + (loadMetadataAsynchronously ? 1 : 0)
  + backgroundDownloadThreads
  + 1 metadata cleanup worker
  + 1 scheduled background-cleanup callback
  + (freeSpaceKeepingEnabled
      ? 1 scheduled collector + keepFreeSpaceEvictionThreads
      : 0)
```

manager：

```text
workerPoolMax = max(1, checkedSum(cacheWorkerMax))
workerPoolMin = 1
```

使用 checked addition；overflow是配置错误。

构造：

```cpp
folly::CPUThreadPoolExecutor(
    {workerPoolMax, workerPoolMin},
    NamedThreadFactory("FileCache"));
```

Folly dynamic mode按需创建 workers，idle timeout后回落。

### resource construction

顺序：

```text
workerPool
scheduler(timekeeper, workerPool)
openedFileCache(localFileSystem, memoryPool)
factory(runtime references)
factory registry caches
```

每个 `FileCache` 构造时显式注入：

```text
workerPool
scheduler
openedFileCache
localFileSystem
memoryPool
commonUserId
```

不注入 `FileCacheManager*`。

### create/getOrCreate

调用 `factory_.getOrCreate`；registry语义按
[`FileCacheFactory` 设计](../2-file-cache/12-filecache-factory-files-design.md)。

新 unique cache：

```text
compute new manager max with checked addition
workerPool.setNumThreads(newMax)
construct FileCache/FileCacheData
Factory inserts registry
publish new budget
```

失败回滚：

```text
Factory erases partial registry entries
destroy partial cache outside registry lock
workerPool.setNumThreads(oldMax)
rethrow
```

cache尚未 initialize时没有 long-running worker，回滚缩容不会等待。

alias insertion不修改 worker max。

### initialization

```text
under lifecycle lock:
  reject shutdown

Factory snapshot:
  getUniqueInstances

outside lock:
  call cache.initialize once per unique cache

under lock:
  state = Initialized
```

如果同步 initialize失败：

```text
deactivate already initialized caches outside lock
keep manager in Created or transition to shutdown according to create path
propagate
```

`create(initializeOnCreate=true)` 失败时 manager不 publish。

异步 metadata initialization按 `FileCache` 自身 contract返回；后续
`throwInitExceptionIfNeeded` 提供错误。

### `get` and snapshots

所有 registry lookup由 Factory短暂持锁并返回 copied shared pointers/maps。

不在 registry lock下：

```text
initialize
apply settings
deactivate/shutdown
refresh expensive cache details
join workers
filesystem operations
```

### `applyConfigs`

流程：

```text
parse/validate configs before call

under lifecycle lock:
  reject shutdown

Factory:
  validate names/path aliases
  snapshot affected unique FileCacheData

Manager:
  compute desired worker max

for each unique cache outside lock:
  if background threads grow:
    grow shared worker max first
  call FileCache::applySettingsIfPossible(new, actual)
  persist actual snapshot even on exception

after decreases/workers joined:
  lower shared worker max to actual aggregate budget
```

新增 unique cache复用 getOrCreate流程。

path改变不支持。

如果任一 apply失败：

```text
already applied fields remain
all FileCacheData store truthful actual snapshots
worker max recomputed from actual snapshots
propagate first exception
```

不伪装成 transaction rollback；CH apply本身允许 partial apply。

### `remove`

第一阶段 remove语义：

```text
Factory under registry lock:
  find unique FileCacheData
  erase all aliases
  retain data snapshot

outside lock:
  cache.deactivateBackgroundOperations
  release opened-file handles for its path
  destroy manager references

recompute/lower worker max after all cache workers exit
```

任何外部 `FileCachePtr` 此后只能持有 inactive cache，不能重新启动/使用。

这比 CH registry-only remove更强，是 Manager runtime ownership的必要 lifecycle contract。

### `clear`

用户确认的行为：

```text
deactivate every unique cache
clear all aliases/registry
clear opened handles belonging to removed caches
shrink dynamic worker max to 1
keep scheduler/worker pool alive
keep manager state Created/Initialized
allow later cache creation
```

实现：

```text
Factory snapshots and erases under registry lock
deactivate/destroy outside lock
shrink after workers have exited
```

clear不调用 scheduler global shutdown；每个 cache task holder deactivate自己的 timers。

### `shutdown`

```text
under lifecycle lock:
  if already Shutdown/ShuttingDown -> wait/return
  state = ShuttingDown

outside lock:
  Factory snapshots and clears registry
  deactivate all unique caches
  scheduler.shutdown
  workerPool.shutdown
  openedFileCache.clear

under lock:
  state = Shutdown
  notify waiters
```

concurrent shutdown callers通过 condition variable等待最终 state。

顺序不能改变：

```text
cache callbacks/workers stop
timer chains cancel
physical pool stops
opened handles clear
```

### opened-file cache

Manager持有独立 cache-segment handle cache，不复用 Hive source-file handles。

提供：

```text
get(path/open options)
remove(path)
clear
stats
```

FileSegment rename和 Metadata removal通过注入 reference/callback直接 invalidate。

### stats/debug

`refreshStats`：

```text
Factory snapshots name map
collect cache approx size/elements/limits outside lock
collect worker pool stats
collect opened handle stats
```

alias names共享同一个 underlying stats object，但输出可按 name展示。

`toString` 不应持 registry lock调用 `FileCache::toString`/iterate。

### global pointer lifetime

Gluten中的 process owner确定为：

```text
gluten::VeloxBackend
  -> shared_ptr<FileCacheManager> fileCacheManager_
```

不是 `HiveConnector` / `VeloxRuntime`：

```text
VeloxBackend:
  one process-global instance per executor JVM

VeloxRuntime:
  multiple instances
  owns scoped Hive/Iceberg/Delta connectors and per-runtime executors

HiveConnector:
  registry object
  unregister does not wait for active tasks
```

因此 connector和 Builder只使用 `FileCacheManager::getInstance`返回的 non-owning pointer。
不引入 `ReadLease`；生命周期 barrier沿用 Gluten现有 `AsyncDataCache`模式。

初始化：

```text
VeloxBackend::init:
  initialize Velox memory manager
  register local/remote filesystems
  create FileCacheManager from backend config
  FileCacheManager::setInstance(fileCacheManager_.get())
  existing GlutenBufferedInputBuilder resolves Manager at create time
```

shutdown：

```text
Spark Context shutdown stops/cancels tasks
NativeBackendInitializer lib-unloading hook
  -> VeloxBackend::tearDown
       -> fileCacheManager_->shutdown()
       -> FileCacheManager::setInstance(nullptr)
       -> fileCacheManager_.reset()
       -> then reset executors / memory manager / filesystem clients
```

`NativeBackendInitializer` hook使用
`SPARK_CONTEXT_SHUTDOWN_PRIORITY - 1`，因此发生在 Spark Context shutdown之后。Manager
shutdown必须放在 `VeloxBackend::tearDown`资源销毁开头，不能放在 `HiveConnector`
destructor，也不能等 executor / memory pool销毁后才执行。

`FileCacheBufferedInput` 持有 `FileCachePtr`，但 Manager-owned runtime resources仍依赖上述
process shutdown barrier。若宿主绕过该 barrier并在 active Velox tasks期间直接调用
`tearDown`，属于 host lifecycle violation。

## error handling

明确失败：

```text
invalid/null runtime dependency
invalid commonUserId
duplicate/conflicting names
same path/different effective settings
missing default cache
operation after ShuttingDown
worker budget overflow
worker pool growth failure
cache initialization failure
settings apply failure
```

不能：

```text
fall back to another cache
silently reuse same name with new path
silently lower worker budget below active workers
continue initialization with only a subset of requested caches
```

## tests

### options/resources

```text
required dependency validation
commonUserId validation
worker max formula across unique caches
aliases counted once
dynamic pool min=1 and max calculation
```

### registry

全部 [`FileCacheFactory` tests](../2-file-cache/12-filecache-factory-files-design.md#tests)。

### initialization

```text
two-phase create does not publish partial manager
initialize each unique cache once
sync failure cleans initialized caches
async initialization lifecycle
```

### apply

```text
growth before new workers
shrink after workers stop
partial apply keeps truthful actual snapshots
path change rejected
new cache/alias through apply
```

### clear/remove

```text
deactivate outside registry lock
clear unique caches once
clear keeps manager reusable
worker max returns to 1
external pointer sees inactive cache
```

### shutdown/singleton

```text
instance before install fails
install/get/uninstall
replacement of live instance fails
shutdown idempotent/concurrent
Gluten VeloxBackend owns Manager for process lifetime
VeloxRuntime/connector destruction does not shutdown Manager
NativeBackendInitializer hook shuts down Manager before executors/memory manager
no callbacks after shutdown
resource shutdown order
operation after shutdown fails
```

## Review 状态

`FileCacheManager.h` 和 `FileCacheManager.cpp` 已按目标文件 review。Manager是唯一 runtime
owner并包含唯一 `FileCacheFactory` registry。资源显式注入、dynamic worker budget、
settings apply、clear和 shutdown都有明确生命周期，不依赖 CH global `Context`。
