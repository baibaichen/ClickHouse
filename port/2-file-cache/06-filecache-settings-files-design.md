# 06. `FileCacheSettings` 文件迁移设计

## 结论

本批次严格按两个文件 review：

```text
src/Interpreters/FileCache/FileCacheSettings.h
src/Interpreters/FileCache/FileCacheSettings.cpp
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/FileCacheSettings.h
velox/ch/Interpreters/FileCache/FileCacheSettings.cpp
```

真实 Velox 类型：

```cpp
struct FileCacheConfig;
using FileCacheSettings = FileCacheConfig;
```

This batch is an exact effective-configuration and validation-semantics port for valid configurations, not a port of ClickHouse settings machinery.

有效配置的字段、默认值、解析结果和 runtime reload行为保持一致。不迁移：

```text
BaseSettings macros / pimpl
Poco AbstractConfiguration
NamedCollection
ColumnsDescription
system.filesystem_cache_settings column dumping
CH disk-layer non-cache keys
```

这些替换为 Velox `ConfigBase` 和 manager stats/config API。

无效配置有两个显式 fail-fast增强：

```text
maxFileSegmentSize == 0 -> reject
ratio-derived effective maxSize == 0 -> reject
```

第一项避免 `FileCache::splitRange` 无进度；第二项保持 explicit `maxSize=0` 已有拒绝意图。
这两项不改变任何有效配置的算法行为。

## 功能分层

两个 CH 文件混合了四类职责：

```text
1. cache-instance fields and defaults
2. config source parsing
3. semantic validation / effective max-size derivation
4. ClickHouse system-table exposure
```

Velox 分为：

```text
FileCacheConfig:
  effective cache-instance values

FileCacheSettingsLoader:
  ConfigBase parsing, presence tracking, path resolution, validation

FileCacheManager:
  multiple cache discovery, same-path dedup, reload orchestration, stats
```

## build registration

`FileCacheSettings.cpp` 通过 `velox_sources(velox_ch_filecache PRIVATE ...)`
注册，兼容 mono alias 和 non-mono real library。

`FileCacheSettings.h` / `FileCacheReadOptions.h` 是 public headers；创建时加入
现有 non-mono `PUBLIC HEADERS` file set。由于 settings header 公开 include
`velox/common/config/Config.h`，`velox_common_config` 必须是 non-mono
`velox_ch_filecache` 的 `PUBLIC` link dependency。

focused settings test 只直接 link `velox_ch_filecache` + GTest，并在 mono /
non-mono 两个 build 中运行，防止直接 link `velox_common_config` / Folly / fmt /
exception 掩盖 public interface 缺失。

## `FileCacheSettings.h`

### 不迁移 CH settings macros

删除：

```text
FILE_CACHE_SETTINGS_SUPPORTED_TYPES
DECLARE_SETTING_TRAIT
DECLARE_SETTING_SUBSCRIPT_OPERATOR
FileCacheSettingsImpl pimpl
```

原因不是裁剪算法，而是 Velox 已有 plain config模式。目标算法文件继续通过
`FileCacheSettings` alias访问字段。

### `FileCacheConfig`

字段及默认值：

```cpp
struct FileCacheConfig
{
    std::string path;

    uint64_t maxSize = 0;
    uint64_t maxElements = FILECACHE_DEFAULT_MAX_ELEMENTS;
    uint64_t maxFileSegmentSize =
        FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE;
    uint64_t boundaryAlignment =
        FILECACHE_DEFAULT_FILE_SEGMENT_ALIGNMENT;
    uint64_t reserveGranularity =
        FILECACHE_DEFAULT_RESERVE_GRANULARITY;

    bool cacheOnWriteOperations = false;
    FileCachePolicy cachePolicy =
        FILECACHE_DEFAULT_CACHE_POLICY;
    double slruSizeRatio = FILECACHE_DEFAULT_SLRU_RATIO;

    uint64_t backgroundDownloadThreads =
        FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_THREADS;
    uint64_t backgroundDownloadQueueSizeLimit =
        FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_QUEUE_SIZE_LIMIT;
    uint64_t backgroundDownloadMaxFileSegmentSize =
        FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE_WITH_BACKGROUND_DOWLOAD;

    uint64_t loadMetadataThreads =
        FILECACHE_DEFAULT_LOAD_METADATA_THREADS;
    bool loadMetadataAsynchronously = false;

    double keepFreeSpaceSizeRatio =
        FILECACHE_DEFAULT_FREE_SPACE_SIZE_RATIO;
    double keepFreeSpaceElementsRatio =
        FILECACHE_DEFAULT_FREE_SPACE_ELEMENTS_RATIO;
    uint64_t keepFreeSpaceRemoveBatch =
        FILECACHE_DEFAULT_FREE_SPACE_REMOVE_BATCH;
    uint64_t keepFreeSpaceEvictionThreads =
        FILECACHE_DEFAULT_FREE_SPACE_EVICTION_THREADS;

    uint64_t invalidatedEntriesCleanupIntervalMs = 10'000;
    uint64_t invalidatedEntriesCleanupThreshold = 1'000;
    uint64_t invalidatedEntriesCleanupRemoveBatch =
        FILECACHE_DEFAULT_FREE_SPACE_REMOVE_BATCH;

    bool enableFilesystemQueryCacheLimit = false;
    uint64_t cacheHitsThreshold = 0;
    bool enableBypassCacheWithThreshold = false;
    uint64_t bypassCacheThreshold = FILECACHE_BYPASS_THRESHOLD;

    bool writeCachePerUserIdDirectory = false;
    bool allowDynamicCacheResize = false;
    uint64_t dynamicResizeLockWaitMs = 1'000;

    double maxSizeRatioToTotalSpace = 0;
    bool skipCacheOnDiskFailure = false;

    bool useSplitCache = false;
    double splitCacheRatio = 0.1;
    uint64_t overcommitEvictionEvictStep = 10ULL * 1024 * 1024;

    double checkCacheProbability = 0.001;

    uint64_t idleClientTtlSec = 7 * 24 * 60 * 60;
    uint64_t idleClientCheckIntervalSec = 0;
    uint64_t idleClientEvictionThreads = 4;

    bool exposePrometheusEvictionMetrics = false;
    bool exposePrometheusEvictionMetricsPerUser = false;

    bool operator==(const FileCacheConfig &) const = default;
};
```

默认值引用
[`FileCache` forward文件](01-filecache-fwd-files-design.md)的 constants；不在此文件重复
numeric literals，除 CH 未抽取 constant 的
设置。

### 字段分类

实际算法字段：

```text
path / maxSize / maxElements
segment size/alignment/reserve granularity
LRU/SLRU/Split policy
metadata/background workers and queues
free-space and invalidated-entry maintenance
query limit
bypass threshold
per-user directory layout
dynamic resize
disk failure behavior
```

compat/后置字段：

```text
cacheHitsThreshold:
  deprecated, parsed/stored, no algorithm consumer

cacheOnWriteOperations:
  write-through caller后置; true is rejected in first phase

overcommitEvictionEvictStep:
  parsed/validated, but overcommit policy is unsupported

idleClient*:
  parsed/stored; without overcommit client tracking remains disabled

exposePrometheusEvictionMetrics*:
  parsed/stored; first-phase metrics shim may be no-op

checkCacheProbability:
  only debug/sanitizer correctness path
```

### copy/move/equality

plain value type直接使用 compiler-generated copy/move和 default equality。行为对应 CH
pimpl 的 deep copy和 value equality。

### 删除的 header API

不迁移：

```text
getColumnsDescription
dumpToSystemSettingsColumns
loadFromCollection
isPathRelativeInConfig public getter
```

system-table exposure由 `FileCacheManagerStats` 负责；NamedCollection/disk registration是
CH integration，不是 Velox FileCache algorithm。

## `FileCacheSettings.cpp`

### config key layout

canonical keys：

```text
file-cache.<name>.path
file-cache.<name>.max-size
file-cache.<name>.max-elements
file-cache.<name>.max-file-segment-size
...
```

默认 cache 可选简写：

```text
file-cache.path
file-cache.max-size
```

parser使用：

```text
ConfigBase::rawConfigsWithPrefix
ConfigBase::valueExists
ConfigBase::get<T>
```

unknown keys under a cache prefix必须报错。CH `non_cache_keys` 不迁移，因为 Velox cache
namespace不混入 disk-layer keys。

### presence tracking

以下约束依赖“是否显式配置”，不能只看解析后的 `0`：

```text
max-size
max-size-ratio-to-total-space
path
```

loader在局部保存：

```cpp
struct Presence
{
    bool path;
    bool maxSize;
    bool maxSizeRatioToTotalSpace;
};
```

不要把 presence bits泄漏进算法 `FileCacheConfig`。

### policy parsing

接受 CH spellings：

```text
lru / LRU
slru / SLRU
lru_overcommit / LRU_OVERCOMMIT
slru_overcommit / SLRU_OVERCOMMIT
```

overcommit strings可以成功 parse，但 validation随后返回明确 unsupported。不能静默降级到
普通 LRU/SLRU。

### path resolution

loader接收 manager-provided：

```text
cachePathPrefix
allowedCacheRoot
```

流程：

```text
path missing -> error
relative path -> cachePathPrefix / path
lexically normalize
require absolute
verify final path lies under allowedCacheRoot
```

必须在 create-directories / filesystem-space 查询之前验证 allowed root，避免无效配置在
任意路径产生副作用。

不保留 `isPathRelativeInConfig` member；该信息只在 loader局部用于诊断。

### max size source

必须恰好配置一个：

```text
max-size
max-size-ratio-to-total-space
```

explicit `max-size`：

```text
must be > 0
effective maxSize = configured value
```

ratio：

```text
must be in (0, 1]
create cache directory after path authorization
totalSpace = std::filesystem::space(path).capacity
effective maxSize = floor(ratio * totalSpace)
effective maxSize must be > 0
```

`maxElements` 不是 `maxSize` 的替代来源；此前草案中的“maxSize/maxElements至少一个”修正为
CH 的真实规则。

### validation

保留 CH validation：

```text
path explicitly configured/resolved and absolute
exactly one max-size source
explicit/effective maxSize > 0
overcommitEvictionEvictStep > 0
useSplitCache cannot combine with overcommit
boundaryAlignment <= maxFileSegmentSize
maxSizeRatioToTotalSpace in (0, 1]
```

设置类型的 non-zero语义：

```text
loadMetadataThreads > 0
keepFreeSpaceEvictionThreads > 0
invalidatedEntriesCleanupIntervalMs > 0
invalidatedEntriesCleanupThreshold > 0
invalidatedEntriesCleanupRemoveBatch > 0
idleClientEvictionThreads > 0
```

第一阶段额外 unsupported checks：

```text
cacheOnWriteOperations == true -> unsupported
LRU_OVERCOMMIT / SLRU_OVERCOMMIT -> unsupported
```

fail-fast safety checks：

```text
maxFileSegmentSize > 0
ratio-derived effective maxSize > 0
```

`maxFileSegmentSize == 0` 会让 `FileCache::splitRange` 无进度，不能复制 CH 当前校验缺口。

### ratio行为

不要在 settings loader新增 CH 没有的 normalization：

```text
slruSizeRatio
splitCacheRatio
keepFreeSpaceSizeRatio
keepFreeSpaceElementsRatio
```

priority代码当前对 SLRU/Split ratio使用 `std::clamp`，并在子队列 size/elements变成 0 时
抛错。free-space ratio由 `FileCache` 原算法消费。

如果未来要收紧这些无效输入，先在 CH 和 Velox共同定义 behavior；本次不改变。

### `maxSizeRatioToTotalSpace`

保留 derivation时机：

```text
config load/reload computes effective maxSize
FileCache receives concrete maxSize
```

`FileCache` hot path不重复查询 disk capacity。

### runtime reload

`FileCacheConfig` 是 requested/effective settings snapshot。manager保存每个 unique cache的
actual config。

`FileCache::applySettingsIfPossible` 按
[`FileCache` 核心文件设计](10-filecache-core-files-design.md)只应用：

```text
backgroundDownloadQueueSizeLimit
backgroundDownloadThreads
backgroundDownloadMaxFileSegmentSize
reserveGranularity
maxSize / maxElements when dynamic resize is allowed
maxFileSegmentSize
idleClientTtlSec
idleClientCheckIntervalSec
exposePrometheusEvictionMetrics*
```

其他字段保持旧 actual value直到 restart。发生部分 apply后异常时，manager必须保存
`FileCache` 回报的 actual snapshot，不能把完整 new config标记为已生效。

dynamic background worker increase：

```text
recompute manager shared workerPoolMax
grow worker pool
start new download workers
publish actual backgroundDownloadThreads
```

decrease在 workers停止/join后再降低 shared max。

早期 Manager草案中“config reload第一阶段只留接口”的结论修正为：core `FileCache`
需要的上述 reload
行为必须迁移；watcher/config-source刷新机制可以由宿主决定。

### system table / stats

不迁移：

```text
ColumnsDescription construction
MutableColumnsAndConstraints insertion
system.filesystem_cache_settings schema
```

manager stats/config snapshot应提供等价可观察值，但不复制 ClickHouse SQL system table。

## 与 reader config 分层

这些不属于 `FileCacheConfig`：

```text
maxDownloadSizePerQuery
skipDownloadIfExceedsPerQueryCacheWriteLimit
reserveSpaceWaitLockTimeoutMs
allowBackgroundDownload
tempCacheOnly
readIfExistsOtherwiseBypass
remote/local buffer sizes
```

它们继续位于 `FileCacheReadOptions`。不要把 instance config和 request config重新合并。
使用方结构和来源见
[`FileCache` read options和 request context设计](../3-consumers/01-filecache-read-context-design.md)。

## 测试要求

### defaults

```text
every FileCacheConfig default matches FileCacheSettings.cpp / 18 constants
default policy SLRU
default SLRU ratio 0.6
default metadata threads 16
default background threads 5
default free-space disabled
default check probability 0.001
```

### parsing

```text
named and default cache prefixes
all supported scalar types
case-insensitive policy spellings
unknown key rejected
relative path resolved under cachePathPrefix
absolute/relative path outside allowed root rejected before side effects
```

### validation

```text
missing path
relative path left unresolved
neither/both max-size sources
explicit maxSize zero
ratio outside (0, 1]
ratio-derived maxSize zero
maxFileSegmentSize zero
boundaryAlignment greater than maxFileSegmentSize
all non-zero fields
overcommit step zero
split + overcommit
overcommit unsupported
cacheOnWriteOperations unsupported
```

### reload

```text
no-op equal config
each reloadable field
constructor-only field stays in actual config
partial apply failure preserves actual snapshot
worker-pool growth happens before background worker growth
same-path aliases share one actual config
```

## Review 状态

`FileCacheSettings.h` 和 `FileCacheSettings.cpp` 已按文件 review。有效配置的 fields、
defaults、effective max-size derivation和 reload semantics迁移；CH BaseSettings/Poco/
NamedCollection/system-table infrastructure不迁移。第一阶段明确拒绝 write-through和
overcommit，并对 `maxFileSegmentSize=0` / derived `maxSize=0` fail fast。
