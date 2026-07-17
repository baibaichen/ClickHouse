# `FileCache` 迁移落地顺序设计

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

设计按三类对象推进：

```text
使用方 -> FileCache -> 依赖方
```

- **使用方**：`FileCacheBufferedInput` / `FileCacheInputStream`、settings/read options、
  `FileCacheManager`、caller token 等。当前基本结束。
- **依赖方**：`FileCache` 算法依赖的底层设施，例如 hash、scheduler、thread pool、
  metrics、locks、logging、fs、debug/cancellation hooks。当前正在 review。
- **`FileCache` 本体**：`FileCache` / `FileSegment` / `CacheMetadata` / priority / query
  limit 等中心 SCC。当前只定原则，尚未进入逐功能闭环 review。

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
velox/ch/Common/FileCacheScheduler*
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
velox/ch/Common/FileCacheAliases.h
velox/ch/Common/FileCacheException.h
velox/ch/Common/FileCacheMetrics.h
velox/ch/Common/SipHash128.h / .cpp
velox/ch/Common/FileCacheScheduler.h / .cpp
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
[`12-filecache-origin-segment-type-design.md`](12-filecache-origin-segment-type-design.md)。

## 阶段 2：priority 和 eviction 类型

### 文件

```text
velox/ch/Interpreters/FileCache/CacheUsage.h
velox/ch/Interpreters/FileCache/EvictionCandidates.h / .cpp
velox/ch/Interpreters/FileCache/IFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/OvercommitFileCachePriority.h
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

## 阶段 3：中心 SCC 第一组：metadata

### 文件

```text
velox/ch/Interpreters/FileCache/FileSegmentInfo.h
velox/ch/Interpreters/FileCache/Metadata.h / .cpp
```

### 功能闭环

一起落：

```text
FileSegmentMetadata
KeyMetadata
LockedKey
CacheMetadata
CleanupQueue
DownloadQueue skeleton
```

### 可以 stub 的内容

- background download 线程可以先建接口，不真正启动。
- cleanup queue 可以先同步执行或 no-op，但删除路径必须保留。

### 不能 stub 的内容

- `getKeyMetadata`
- `lockKeyMetadata`
- `LockedKey::removeFileSegment`
- `getFileSegmentPath`
- key state transition

### 验证

```text
metadata get/create/remove key
path layout
LockedKey prevents mutation while held
remove empty key
```

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

## 阶段 5：中心 SCC 第三组：`FileCache`

### 文件

```text
velox/ch/Interpreters/FileCache/FileCache.h / .cpp
velox/ch/Interpreters/FileCache/QueryLimit.h / .cpp
velox/ch/Interpreters/FileCache/FileCacheFactory.h / .cpp
velox/ch/Interpreters/FileCache/FileCacheManager.h / .cpp
```

### 功能闭环

一起落：

```text
get
getOrSet
set
tryReserve
doEviction
query limit
metadata load
background cleanup hooks
dynamic resize hooks
FileCacheManager lifecycle
```

### 可以 stub 的内容

- config reload 可先保留接口。
- Prometheus metrics 可后置。
- background free-space keeper 可先 disabled，但接口保留。

### 不能 stub 的内容

- `getOrSet`
- `get`
- `set`
- `tryReserve`
- eviction candidate collection
- query limit holder if setting enabled
- shutdown/deactivate background operations

### 验证

```text
getOrSet creates holes
get does not create cache segments
getDownloadedContiguousOrEmpty cache-only behavior
tryReserve evicts releasable segments
removeKey/removeFileSegment
```

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

### 可以 stub 的内容

- `load` 第一版只初始化，不主动下载。
- `shouldPreload` 可先 false。

### 不能 stub 的内容

- `Next` 的三态 read path。
- `BackUp` 当前 output buffer 内回退。
- `seekToPosition` reset state。
- 不得走 `AsyncDataCache` raw bytes retention。

### 验证

```text
cache miss -> remote read -> cache write -> later hit
cache hit -> local cache read
bypass mode
temp cache-only miss
BackUp / Skip / seek
```

## 阶段 7：DWIO/scan builder 接入

### 目标

找到 Velox 创建 `BufferedInput` 的位置，按配置选择：

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
- `FileCacheBufferedInput` 需要拿到 `FileCachePtr`、`FileCacheReadOptions`、
  `FileCacheRequestContext`。

### 验证

```text
ORC/DWRF/Parquet smoke scan
cache enabled/disabled path selection
AsyncDataCache disabled on FileCache path
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
FileCacheCallerToken
FileCacheThreadPool / FileCacheWorker + CH-compatible using aliases
ProfileEvents / CurrentMetrics / OpenTelemetry / FailPoint no-op shims
Guards / logger / fs compatibility shims
FileCacheSettings parse/validate skeleton
ReadBufferFromVeloxReadFile
WriteBufferFromVeloxWriteFile
```

目标是让 `velox_ch_filecache` 能编译，并为后续算法文件提供稳定依赖。
