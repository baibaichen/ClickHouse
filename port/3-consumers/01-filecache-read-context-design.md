# 01. `FileCache` read options和 request context设计

本文只定义 `FileCache` 使用方的一次读取参数和请求身份。Cache实例级
`FileCacheConfig`、defaults、解析和 validation统一由
[`FileCacheSettings` 文件设计](../2-file-cache/06-filecache-settings-files-design.md)
定义，不在这里重复。

配置边界：

```text
FileCacheConfig
  cache instance lifetime and algorithm settings
  -> owned by FileCacheFactory/FileCacheManager

FileCacheReadOptions
  one read operation
  -> consumed by FileCacheInputStream

FileCacheRequestContext
  query/user/origin/cacheability identity
  -> converted to FileCacheOriginInfo and query-limit context
```

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

## 配置校验边界

`FileCacheSettingsLoader::loadCacheConfig` 和所有实例级 validation由
[`FileCacheSettings` 文件设计](../2-file-cache/06-filecache-settings-files-design.md)
定义。本层只组合已验证的 `FileCacheConfig` 与单次读取参数；例如
`boundaryAlignment` override必须用目标 cache的 `maxFileSegmentSize` 校验。

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
