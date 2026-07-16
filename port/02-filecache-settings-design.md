# `FileCacheSettings` 配置设计

这个文档定义 ClickHouse `FileCacheSettings` 迁移到 Velox 后的配置分层。核心原则：

- `FileCache` 对象的算法配置直接参考 ClickHouse。
- Velox 侧只设计配置来源、分类和传递方式。
- 当前阶段不支持 `cache_on_write_operations` / write-through cache。

## 配置分层

ClickHouse 把很多设置都放在 `FileCacheSettings` / `FilesystemCacheSettings` /
`ReadSettings` 里。迁移到 Velox 后应该分三层：

```text
FileCacheConfig
  cache 实例级配置，决定 FileCache 生命周期、容量、后台线程、驱逐策略。

FileCacheReadOptions
  单次读取配置，进入 FileCacheInputStream::ReadInfo。

FileCacheRequestContext
  单次请求上下文，表达 query/user/origin/cacheable/segment type。
```

## `FileCacheConfig`

`FileCacheConfig` 对应 ClickHouse `FileCacheSettings` 中 cache 实例级配置。
这些配置由 `FileCacheManager` / `FileCacheFactory` 创建 `FileCache` 时读取。

```cpp
struct FileCacheConfig
{
    // Cache 实例名称，用于区分多个 cache 配置。
    std::string name;
    // 本地 cache 根目录。
    std::string path;

    // Cache 最大字节数；0 表示不按大小限制。
    uint64_t maxSize = 0;
    // Cache 最大元素数，即 file segment 数；0 表示不按元素数限制。
    uint64_t maxElements = 0;
    // 单个 file segment 的最大大小。
    uint64_t maxFileSegmentSize = 0;
    // File segment 边界对齐粒度。
    uint64_t boundaryAlignment = 0;
    // 下载 reserve 时至少向前预留的粒度；0 表示按实际请求精确预留。
    uint64_t reserveGranularity = 0;

    // 驱逐策略，例如 LRU / SLRU / overcommit 变体。
    FileCachePolicy cachePolicy = FileCachePolicy::SLRU;
    // SLRU 中 protected/probationary 队列的大小比例。
    double slruSizeRatio = 0.0;

    // 后台下载线程数；0 表示禁用后台下载。
    uint64_t backgroundDownloadThreads = 0;
    // 后台下载队列长度限制；0 表示不限制或禁用，按 CH 语义保持。
    uint64_t backgroundDownloadQueueSizeLimit = 0;
    // 后台下载单个 file segment 的最大下载大小。
    uint64_t backgroundDownloadMaxFileSegmentSize = 0;

    // 启动时加载 cache metadata 的最大线程数。
    uint64_t loadMetadataThreads = 1;
    // 是否异步加载 cache metadata。
    bool loadMetadataAsynchronously = false;

    // 后台维持空闲 size 的目标比例。
    double keepFreeSpaceSizeRatio = 0.0;
    // 后台维持空闲 elements 的目标比例。
    double keepFreeSpaceElementsRatio = 0.0;
    // 后台 free-space 任务单批移除的 segment 数。
    uint64_t keepFreeSpaceRemoveBatch = 0;
    // 后台 free-space 任务中执行实际文件删除的线程数。
    uint64_t keepFreeSpaceEvictionThreads = 1;

    // 后台清理 invalidated priority entries 的周期，单位毫秒。
    uint64_t invalidatedEntriesCleanupIntervalMs = 10000;
    // invalidated entries 累积到该阈值后触发后台清理。
    uint64_t invalidatedEntriesCleanupThreshold = 1000;
    // 单次后台清理最多移除的 invalidated entries 数。
    uint64_t invalidatedEntriesCleanupRemoveBatch = 0;

    // 是否启用单 query cache 写入限额。
    bool enableFilesystemQueryCacheLimit = false;
    // Deprecated；仅为 CH 配置兼容保留。
    uint64_t cacheHitsThreshold = 0;
    // Undocumented；仅为 CH 配置兼容保留。
    bool enableBypassCacheWithThreshold = false;
    // bypass cache 阈值；只在 enableBypassCacheWithThreshold 启用时有意义。
    uint64_t bypassCacheThreshold = 0;
    // 是否按 user_id 写入不同 cache 目录。
    bool writeCachePerUserIdDirectory = false;
    // 是否允许运行时动态调整 cache 大小/元素限制。
    bool allowDynamicCacheResize = false;
    // 动态 resize 获取排他锁的等待超时，单位毫秒。
    uint64_t dynamicResizeLockWaitMs = 1000;

    // maxSize 相对磁盘总空间的比例；0 表示不按磁盘空间比例推导。
    double maxSizeRatioToTotalSpace = 0.0;
    // Cache 磁盘 IO 失败时是否跳过 cache 操作而不是传播错误。
    bool skipCacheOnDiskFailure = false;

    // 是否按 system/data segment type 拆分 cache 空间。
    bool useSplitCache = false;
    // split cache 中 system segment 占总 cache 的比例。
    double splitCacheRatio = 0.1;
    // overcommit 驱逐策略中每次驱逐步长。
    uint64_t overcommitEvictionEvictStep = 10 * 1024 * 1024;

    // Debug/sanitizer 构建下随机检查 cache 正确性的概率。
    double checkCacheProbability = 0.0;

    // idle client TTL，超过该时长未访问的 client cache 可被清理。
    uint64_t idleClientTtlSec = 7 * 24 * 60 * 60;
    // idle client 检查周期；0 表示按 TTL 自动推导。
    uint64_t idleClientCheckIntervalSec = 0;
    // 清理 idle client cache 使用的最大线程数。
    uint64_t idleClientEvictionThreads = 4;

    // 是否导出 filesystem cache eviction Prometheus 指标。
    bool exposePrometheusEvictionMetrics = false;
    // 是否导出按 user 维度的 eviction 指标；可能带来较高基数。
    bool exposePrometheusEvictionMetricsPerUser = false;

    // 为 CH 配置兼容解析；当前阶段不支持 write-through cache。
    bool cacheOnWriteOperations = false;
};
```

### 当前不支持的实例级配置

| ClickHouse setting | 当前策略 |
|---|---|
| `cache_on_write_operations` | 当前阶段不支持；配置存在也应忽略或显式拒绝，避免用户误以为 write-through 生效 |

### 保留但不建议使用的实例级配置

这些配置在 ClickHouse 中存在，但不应该成为新设计的主路径。为了和 CH 配置一一对应，
Velox 侧仍保留字段和解析。

| ClickHouse setting | 当前策略 |
|---|---|
| `cache_hits_threshold` | Deprecated；保留字段，默认不使用 |
| `enable_bypass_cache_with_threshold` | Undocumented；保留字段，默认不使用 |
| `bypass_cache_threshold` | Undocumented；仅在上一项启用时有意义，默认不使用 |

## `FileCacheReadOptions`

`FileCacheReadOptions` 对应单次读取行为，放进 `FileCacheInputStream::ReadInfo`。

```cpp
struct FileCacheReadOptions
{
    // Cache-only 读取：miss 是错误，不允许远端 bypass。
    bool tempCacheOnly = false;
    // 只在 cache 已存在时读取；miss 时绕过 cache 读远端，不创建 segment。
    bool readIfExistsOtherwiseBypass = false;
    // holder 完成时是否允许后台继续下载未完成 segment。
    bool allowBackgroundDownload = true;
    // packed storage metadata 文件是否允许后台下载。
    bool allowBackgroundDownloadForMetadataFilesInPackedStorage = true;
    // fetch 场景是否允许后台下载。
    bool allowBackgroundDownloadDuringFetch = true;
    // 启用 filesystem cache 时是否倾向使用更大的远端读 buffer，减少 cache 碎片。
    bool preferBiggerBufferSize = true;

    // 一次读取最多持有的 file segment 数；0 表示不限制。
    uint64_t segmentsBatchSize = 0;
    // 单次读取覆盖的 boundary alignment override。
    std::optional<uint64_t> boundaryAlignment;

    // 远端源文件读取 buffer 大小。
    uint64_t remoteFsBufferSize = 0;
    // 本地 cache segment 读取 buffer 大小。
    uint64_t localFsBufferSize = 0;
    // reserve cache 空间时等待锁的超时，单位毫秒。
    uint64_t reserveSpaceWaitLockTimeoutMs = 0;
    // 单 query 最多允许下载到 cache 的大小。
    uint64_t maxDownloadSizePerQuery = 0;
    // 超过单 query cache 写入限额时是否跳过下载。
    bool skipDownloadIfExceedsPerQueryCacheWriteLimit = true;

    // 是否记录 filesystem cache log。
    bool enableFilesystemCacheLog = false;
};
```

这些配置不应该影响 `FileCache` 实例结构，只影响一次读取：

- `tempCacheOnly` 决定 miss 时抛“临时数据不在 cache”。
- `readIfExistsOtherwiseBypass` 决定只查 cache，不创建新 segment。
- `segmentsBatchSize` 控制一次 holder 持有多少 segment。
- `allowBackgroundDownload` 控制 holder 完成时是否允许后台继续下载。
- `allowBackgroundDownloadForMetadataFilesInPackedStorage` 和
  `allowBackgroundDownloadDuringFetch` 保留 CH 的细分后台下载开关；是否在 Velox
  当前读路径实际使用，需要在接入具体 reader 时再确认。
- `preferBiggerBufferSize` 对应 CH 中 filesystem cache 激活时建议远端读使用更大
  buffer、减少 cache fragmentation 的 hint。
- `localFsBufferSize` 控制读本地 cache segment 的 buffer。
- `remoteFsBufferSize` 控制远端 `ReadBufferFromVeloxReadFile` 的 buffer。
- `maxDownloadSizePerQuery` 和 `skipDownloadIfExceedsPerQueryCacheWriteLimit`
  配合 `enableFilesystemQueryCacheLimit` 使用。

## `FileCacheRequestContext`

`FileCacheRequestContext` 表达单次请求的身份和分类信息。

```cpp
struct FileCacheRequestContext
{
    // 当前 query id，用于 query limit 和日志。
    std::string queryId;
    // 当前用户/client id，用于 origin、权限、per-user 目录和 idle client eviction。
    std::string userId;
    // 当前用户权重，用于 overcommit/per-user cache usage。
    uint64_t userWeight = 0;
    // 单次读取是否值得缓存；映射方式仍需单独 review。
    bool cacheable = true;
    // 当前读取对应的 segment 类型，例如 Data/System/General。
    FileSegmentKeyType segmentType = FileSegmentKeyType::Data;
};
```

它用于构造 ClickHouse 里的 `FileCacheOriginInfo` 等价对象：

```text
FileCacheOriginInfo {
  user_id
  weight
  segment_type
}
```

## 配置来源

### `ConfigBase`

`FileCacheConfig` 从 `velox::config::ConfigBase` 读取。建议配置 key 使用统一前缀：

```text
file-cache.<cache-name>.path
file-cache.<cache-name>.max-size
file-cache.<cache-name>.max-elements
file-cache.<cache-name>.max-file-segment-size
file-cache.<cache-name>.cache-policy
file-cache.<cache-name>.background-download-threads
```

如果只有一个默认 cache，也可以支持简写：

```text
file-cache.path
file-cache.max-size
```

### `ReaderOptions`

`ReaderOptions` 提供 Velox scan reader 的内存和读参数：

- `memoryPool`：用于 `BufferPtr` / `AlignedBuffer` 分配。
- `loadQuantum`：可以作为 `remoteFsBufferSize` 的默认值。
- `cacheable`：可映射到 `FileCacheRequestContext::cacheable`，但这个映射仍需单独审查。

### `FileOptions` / `FileIoContext`

`FileOptions` 和 `FileIoContext` 用于传递单文件/单次读上下文：

- `fileSize`
- `readRangeHint`
- `tokenProvider`
- `fileReadOps`
- `cacheable`

这些字段可以作为 `FileCacheRequestContext` 和 `FileCacheReadOptions` 的补充来源，
但不能替代 cache 实例配置。

## 配置加载接口

建议接口：

```cpp
class FileCacheSettingsLoader
{
public:
    static FileCacheConfig loadCacheConfig(
        const config::ConfigBase & config,
        std::string_view cacheName);

    static FileCacheReadOptions makeReadOptions(
        const io::ReaderOptions & readerOptions,
        const FileOptions & fileOptions,
        const config::ConfigBase * sessionConfig);

    static FileCacheRequestContext makeRequestContext(
        const FileIoContext & fileIoContext,
        const io::ReaderOptions & readerOptions,
        const config::ConfigBase * sessionConfig);
};
```

`FileCacheBufferedInput` 构造时应接收已经解析好的 `FileCacheReadOptions` 和
`FileCacheRequestContext`，不要在 `Next` 热路径里反复解析配置。

## 配置校验

保留 ClickHouse `FileCacheSettings::validate` 的核心约束：

- `path` 必须非空。
- `maxSize` / `maxElements` 至少有一个限制。
- `maxFileSegmentSize` 必须大于 0。
- `boundaryAlignment` 和 `reserveGranularity` 要与 segment split/reserve 逻辑一致。
- `slruSizeRatio` / `splitCacheRatio` 必须在有效范围内。
- `keepFreeSpace*Ratio` 必须在有效范围内。
- 线程数和 batch size 不能为 0 的项保留 non-zero 约束。
- 当前阶段如果设置 `cache_on_write_operations=true`，应明确报不支持或记录 warning 并禁用。

## 与当前设计的连接

```text
FileCacheManager
  -> load FileCacheConfig
  -> create FileCache

FileCacheBufferedInput
  -> receives FileCacheReadOptions
  -> receives FileCacheRequestContext
  -> creates FileCacheInputStream

FileCacheInputStream::initializeIfNeeded
  -> uses FileCacheReadOptions
  -> calls FileCache::get / getOrSet / getDownloadedContiguousOrEmpty

FileCacheInputStream::createReadFromFileSegmentState
  -> uses FileCacheRequestContext origin information
```
