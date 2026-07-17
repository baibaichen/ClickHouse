# 01. `FileCache` 迁移落地顺序设计

这个文档定义从 ClickHouse `src/Interpreters/FileCache` 迁移到 Velox
`velox/ch/...` 的落地顺序。目标是先按文件 DAG 降低编译风险，到中心 SCC 后按功能
闭环成组落地，避免为了临时编译制造假接口。

## 总原则

- 目录按 ClickHouse `src` 下路径对齐到 `velox/ch/...`。
- 先迁移无状态/低依赖文件，再迁移 priority，再迁移中心 SCC。
- 中心 SCC 不按单文件硬拆，按功能闭环迁移。
- `FileCache` 算法不重写，只替换基础设施。
- 当前阶段不支持 `cache_on_write_operations` / write-through cache。
- 每一阶段都要有可编译目标。

## Review 分层

设计按依赖方向分为三个目录：

```text
3-consumers -> 2-file-cache -> 1-dependencies
```

- [`1-dependencies/`](1-dependencies/)：scheduler、thread pool、caller identity、
  metrics、locks、logging、filesystem和 debug/cancellation hooks。
- [`2-file-cache/`](2-file-cache/)：`FileCache` / `FileSegment` / `Metadata` /
  priority / query limit / factory等 module文件。
- [`3-consumers/`](3-consumers/)：read options/request context、`FileCacheManager`、
  `FileCacheBufferedInput`和 `FileCacheInputStream`。

## CMake target

建议先建一个主 target：

```text
velox_ch_filecache
```

包含：

```text
velox/ch/Interpreters/FileCache/*
velox/ch/Disks/IO/FileCacheBufferedInput*
velox/ch/Disks/IO/FileCacheInputStream*
velox/ch/IO/ReadBufferFromVeloxReadFile*
velox/ch/IO/WriteBufferFromVeloxWriteFile*
velox/ch/Common/SipHash128*
velox/ch/Common/FileCacheScheduler*
velox/ch/Common/ThreadPool*
velox/ch/Common/FileCacheQueryIdScope*
velox/ch/Common/StatusFile*
```

第一版不拆多个 target，避免 target 间互相依赖过早复杂化。等编译稳定后，如果需要，
再拆：

```text
velox_ch_filecache_core
velox_ch_filecache_io
velox_ch_filecache_dwio
```

## 阶段 0：目录和基础 shim

### 新增目录

```text
velox/ch/Common/
velox/ch/IO/
velox/ch/Disks/IO/
velox/ch/Interpreters/FileCache/
```

### 先落地的基础 shim

```text
velox/ch/Common/ClickHouseAliases.h
velox/ch/Common/FileCacheException.h
velox/ch/Common/FileCacheMetrics.h
velox/ch/Common/FileCacheBoundedQueue.h
velox/ch/Common/SipHash128.h / .cpp
velox/ch/Common/FileCacheScheduler.h / .cpp
velox/ch/Common/ThreadPool.h / .cpp
velox/ch/Common/FileCacheQueryIdScope.h / .cpp
velox/ch/IO/ReadBufferFromVeloxReadFile.h / .cpp
velox/ch/IO/WriteBufferFromVeloxWriteFile.h / .cpp
```

### 目的

- 把 ClickHouse 常用基础设施替换集中到少量文件。
- 后续迁移算法文件时尽量少改业务逻辑。

### 可以 stub 的内容

- metrics 可以先是 no-op / atomic counter。
- scheduler 可以先用简单 executor + timer wrapper。
- `ReadBufferFromVeloxReadFile::getRemoteFileMetadata` 可先返回 `nullopt`，但要保留接口。

### 不能 stub 的内容

- `ReadBufferFromVeloxReadFile::seek` / `setReadUntilPosition` / `next` /
  `getFileOffsetOfBufferEnd`，这些直接影响读状态机。
- `WriteBufferFromVeloxWriteFile::next`，读 miss 写 cache 依赖它。

### 验证

```text
velox_ch_filecache target 能空编译
```

## 阶段 1：叶子类型和配置

### 文件

```text
velox/ch/Interpreters/FileCache/FileSegmentKeyType.h / .cpp
velox/ch/Interpreters/FileCache/FileCacheKey.h / .cpp
velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h
velox/ch/Interpreters/FileCache/FileCache_fwd.h
velox/ch/Interpreters/FileCache/FileCache_fwd_internal.h
velox/ch/Interpreters/FileCache/FileCacheUtils.h
velox/ch/Interpreters/FileCache/Guards.h
velox/ch/Interpreters/FileCache/ShardedMap.h
velox/ch/Interpreters/FileCache/FileCacheSettings.h / .cpp
```

`FileCache_fwd.h` / `FileCache_fwd_internal.h` 逐文件设计详见
[`FileCache` forward文件设计](2-file-cache/01-filecache-fwd-files-design.md)。

`FileCacheUtils.h` 逐文件设计详见
[`FileCacheUtils.h` 设计](2-file-cache/04-filecache-utils-design.md)。

`ShardedMap.h` 逐文件设计详见
[`ShardedMap.h` 设计](2-file-cache/05-filecache-sharded-map-design.md)。

`FileCacheSettings.h` / `FileCacheSettings.cpp` 逐文件设计详见
[`FileCacheSettings` 设计](2-file-cache/06-filecache-settings-files-design.md)。

### 依赖替换

| CH | Velox |
|---|---|
| `String` | `std::string` |
| `UInt64`, `UInt128` | `uint64_t` / 自定义 128-bit helper |
| `Exception` | `VELOX_FAIL` / `VELOX_CHECK` / wrapper |
| `Poco::Util::AbstractConfiguration` | `ConfigBase` |
| `Settings` machinery | `FileCacheConfig` |

### 可以 stub 的内容

- `FileCacheSettings::dumpToSystemSettingsColumns` 不迁移。
- structured system table 输出不迁移。

### 不能 stub 的内容

- key hash / path string / origin identity。
- settings validate。
- `FileSegmentKeyType::General` prefix must stay empty.
- `FileCacheOriginInfo::operator==` remains user-id-only, while `OriginPoolKey`
  compares user id, weight, and segment type.

### 验证

```text
基础类型单测：key parse/hash/toString、origin hash、settings parse/validate
```

`FileSegmentKeyType` / `FileCacheOriginInfo` 语义详见
[`FileSegmentKeyType` / `FileCacheOriginInfo` 设计](2-file-cache/02-filecache-origin-segment-type-design.md)。

## 阶段 2：priority 和 eviction 类型

### 文件

```text
velox/ch/Interpreters/FileCache/CacheUsage.h
velox/ch/Interpreters/FileCache/EvictionCandidates.h / .cpp
velox/ch/Interpreters/FileCache/IFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
```

### 迁移策略

- 保留 ClickHouse 算法。
- 替换锁、日志、异常、metrics。
- 不引入 Velox `AsyncDataCache` 的 eviction 语义。

### 可以 stub 的内容

- debug dump / metrics 可先最小实现。

### 不能 stub 的内容

- `add` / `remove` / `tryIncreasePriority`
- `collectEvictionInfo`
- `collectCandidatesForEviction`
- `HoldSpace`
- `InvalidatedEntryInfo`

### 验证

```text
迁移 CH priority 单测或新增等价 gtest：
LRU add/remove/evict
SLRU promote/downgrade
Split routes by FileSegmentKeyType
```

Priority / eviction 语义详见
[Priority / eviction设计](2-file-cache/07-filecache-priority-eviction-design.md)。
`OvercommitFileCachePriority` 属于 Cloud / distributed-cache 条件路径，第一阶段后置。

## 阶段 3：中心 SCC 文件 review：metadata

### 文件

```text
velox/ch/Interpreters/FileCache/FileSegmentInfo.h
velox/ch/Interpreters/FileCache/Metadata.h / .cpp
```

### 文件依赖

逐文件 review，但不能把 `Metadata.cpp` 当成能在 `FileSegment.cpp` 之前独立完成的模块：

```text
FileSegmentInfo.h -> priority/key/origin leaf types
Metadata.h       -> FileSegment.h + priority + guards + sharded map
Metadata.cpp     -> FileSegment.cpp API + FileCache common/internal origins
```

实际落地时，`Metadata.cpp`、`FileSegment.cpp` 和必要的 `FileCache` static origin 接口必须在
同一实现批次内形成可链接闭环。

### 不能 stub 的内容

- `getKeyMetadata`
- `lockKeyMetadata`
- `LockedKey::removeFileSegment`
- `getFileSegmentPath`
- key state transition
- cleanup queue / cleanup worker
- download queue / background download worker
- shutdown / download-worker resize

### 验证

```text
metadata get/create/remove key
path layout
LockedKey prevents mutation while held
remove empty key
background queue cancel and resize
```

逐文件设计详见
[Metadata文件设计](2-file-cache/08-filecache-metadata-files-design.md)。

## 阶段 4：中心 SCC 第二组：`FileSegment`

### 文件

```text
velox/ch/Interpreters/FileCache/FileSegment.h / .cpp
```

### 功能闭环

一起落：

```text
FileSegment state machine
FileSegmentsHolder
DownloadState
reserve/write/complete
ReadBufferFromVeloxReadFile
WriteBufferFromVeloxWriteFile
```

### 可以 stub 的内容

- debug per-segment logger。
- read-back helper 可后置。
- `WriteBufferToFileSegment` 后置。它当前服务 `TemporaryDataOnDisk` 写
  `FileSegmentKind::Ephemeral` segment，不是读 miss 填 cache 的主路径，也不承担
  `cache_on_write_operations`。

### 不能 stub 的内容

- downloader ownership
- `reserve`
- `write`
- `completePartAndResetDownloader`
- `complete`
- `detach`
- `getCurrentWriteOffset`

### 验证

```text
EMPTY -> DOWNLOADING -> DOWNLOADED
PARTIALLY_DOWNLOADED
PARTIALLY_DOWNLOADED_NO_CONTINUATION
reserve before write
wrong offset write fails
holder destructor completes/removes
```

逐文件设计详见
[`FileSegment` 文件设计](2-file-cache/09-filecache-file-segment-design.md)。

## 阶段 5：中心 SCC 第三组：`FileCache`

### 文件

```text
velox/ch/Interpreters/FileCache/FileCache.h / .cpp
```

### 功能闭环

一起落：

```text
get
getOrSet
set
tryReserve
doEviction
metadata load
background cleanup / free-space keeping
dynamic resize
shutdown
```

### 可以 stub 的内容

- Prometheus metrics 可后置。

### 不能 stub 的内容

- `getOrSet`
- `get`
- `set`
- `tryReserve`
- eviction candidate collection
- metadata load
- background free-space keeper
- dynamic resize
- shutdown/deactivate background operations

### 验证

```text
getOrSet creates holes
get does not create cache segments
getDownloadedContiguousOrEmpty cache-only behavior
tryReserve evicts releasable segments
removeKey/removeFileSegment
```

逐文件设计详见
[`FileCache` 核心文件设计](2-file-cache/10-filecache-core-files-design.md)。

## 阶段 5.1：`QueryLimit`

严格按文件 review：

```text
velox/ch/Interpreters/FileCache/QueryLimit.h
velox/ch/Interpreters/FileCache/QueryLimit.cpp
```

setting 启用时不能 stub query context holder 或 query priority。

逐文件设计详见
[`QueryLimit` 文件设计](2-file-cache/11-filecache-query-limit-design.md)。

## 阶段 5.2：factory semantics / manager implementation

严格按文件 review CH factory；Velox保留真实 Factory，并由 Manager持有：

```text
src/Interpreters/FileCache/FileCacheFactory.h / .cpp
velox/ch/Interpreters/FileCache/FileCacheFactory.h / .cpp
velox/ch/Interpreters/FileCache/FileCacheManager.h / .cpp
```

manager 是 runtime owner并包含一个真实 Factory。只有 Factory持有多个 named/path
`FileCache` registry；Manager不实现第二套 registry。

逐文件设计详见
[`FileCacheFactory` 文件设计](2-file-cache/12-filecache-factory-files-design.md)。

Manager目标文件设计详见
[`FileCacheManager` 文件设计](3-consumers/02-filecache-manager-design.md)。

## 阶段 6：Velox scan 接入

### 文件

```text
velox/ch/Disks/IO/FileCacheBufferedInput.h / .cpp
velox/ch/Disks/IO/FileCacheInputStream.h / .cpp
```

### 功能闭环

一起落：

```text
FileCacheBufferedInput::enqueue
FileCacheBufferedInput::load
FileCacheBufferedInput::read
FileCacheInputStream::Next
FileCacheInputStream::BackUp / SkipInt64 / seekToPosition
```

### 确定的第一版行为

- `load` 是 no-op planning barrier；不能解引用 `enqueue` 返回的 stream。
- `preload` 是 no-op，`preloaded` / `shouldPreload` 返回 false。
- `shouldPrefetchStripes` 返回 false，避免 DWRF要求 `CacheInputStream`。
- `executor` 返回 constructor-injected executor。
- `hasCache` 返回 false，因为 `FileSegment` 不满足 `CachePin`-based
  `cacheRegion` / `findCachedRegion` API。

### 不能 stub 的内容

- `Next` 的三态 read path。
- `BackUp` 当前 output buffer 内回退。
- `seekToPosition` region-relative坐标和 current-buffer fast path。
- FileCache lookup使用 `region.offset + position` absolute坐标。
- 不得走 `AsyncDataCache` raw bytes retention。

### 验证

```text
cache miss -> remote read -> cache write -> later hit
cache hit -> local cache read
bypass mode
temp cache-only miss
BackUp / Skip / seek
enqueue result destroyed before load
DWRF stripe metadata cache uses non-CacheInputStream path
random row-group seek correctness/performance
```

## 阶段 7：DWIO/scan builder 接入

### 目标

扩展 Gluten已经通过 `hive::BufferedInputBuilder::registerBuilder` 注册的
`GlutenBufferedInputBuilder`。它覆盖 `FileSplitReader`、`FileIndexReader`以及
Iceberg positional/equality delete readers，并按配置选择：

```text
if FileCache enabled:
    FileCacheBufferedInput
else if AsyncDataCache enabled:
    CachedBufferedInput
else:
    BufferedInput / DirectBufferedInput
```

### 原则

- `FileCacheBufferedInput` 和 `CachedBufferedInput` 不在同一路径双重缓存 raw bytes。
- Builder从 `FileCacheManager::instance`取得 `FileCachePtr`。
- Builder需要 `FileCacheReadOptions`、`FileCacheRequestContext`、`FileCacheFileIdentity`。
- identity resolver当前返回 path + empty etag，key使用 `fromPath`；未来 etag非空时使用
  `SipHash(path + etag)`。
- FileCache未启用时保持现有 `CachedBufferedInput` / `GlutenDirectBufferedInput`
  fallback。
- Gluten `VeloxBackend` 持有 process-global `shared_ptr<FileCacheManager>`；per-runtime
  connectors不拥有 Manager。
- `NativeBackendInitializer` shutdown hook在 Spark Context shutdown之后进入
  `VeloxBackend::tearDown`，先 shutdown Manager，再销毁 executor和 memory manager。

### 验证

```text
ORC/DWRF/Parquet smoke scan
cache enabled/disabled path selection
AsyncDataCache disabled on FileCache path
path-only key while etag is empty
different non-empty etags produce different keys
FileSplitReader / FileIndexReader / Iceberg delete-reader path selection
VeloxRuntime unregister connector does not stop process FileCacheManager
VeloxBackend tearDown after task barrier shuts Manager before runtime resources
```

## 阶段 8：补充能力

### 后置项目

```text
background download
free-space keeper
invalidated entries cleanup
idle client eviction
dynamic resize reload
stats / metrics / debug APIs
CacheFileSystem / CachedReadFile fallback
```

这些能力可以按需求逐步打开，但接口应该在前面阶段预留。

## 当前不做

```text
cache_on_write_operations
CachedOnDiskWriteBufferFromFile
CachedWriteFile
write-through cache
```

读路径 miss 后写本地 cache segment 仍然必须支持；这是 `FileSegment::write` 的职责，
不是 `cache_on_write_operations`。

## 最小第一批 PR

建议第一批只做：

```text
目录 + CMake target
基础 shim
FileCacheKey / FileSegmentKeyType / OriginInfo
SipHash128 helper
FileCacheScheduler
FileCacheQueryIdScope
FileCacheThreadPool / FileCacheWorker + CH-compatible using aliases
ProfileEvents / CurrentMetrics / OpenTelemetry / FailPoint no-op shims
Guards / logger / fs compatibility shims
FileCacheSettings parse/validate skeleton
ReadBufferFromVeloxReadFile
WriteBufferFromVeloxWriteFile
```

目标是让 `velox_ch_filecache` 能编译，并为后续算法文件提供稳定依赖。
