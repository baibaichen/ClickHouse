# 00. ClickHouse `FileCache` 到 Velox 的迁移总览

## 目标

把 ClickHouse `FileCache` 的核心算法和语义迁移到 `/home/chang/OpenSource/velox`，
但不使用 Velox `AsyncDataCache` / `SsdCache` 重新实现这些算法。

迁移后需要保留：

- file segment 生命周期和部分下载状态机
- metadata 加载与恢复
- 容量预留、驱逐、priority 策略
- 单 query cache 写入限额
- user/origin 隔离
- 后台下载、后台清理、动态 resize 等接口

当前阶段明确不支持：

```text
cache_on_write_operations
CachedOnDiskWriteBufferFromFile
CachedWriteFile
write-through cache
```

但读路径 miss 后填充本地 cache segment 仍然必须支持，这是 `FileSegment::write`
和 `WriteBufferFromVeloxWriteFile` 的职责。

## 文档目录

根目录保留跨分类文档：

| 序号 | 文档 | 内容 |
|---|---|---|
| 00 | `00-filecache-velox-migration.md` | 总览、范围和核心决策 |
| 01 | [`01-filecache-port-order-design.md`](01-filecache-port-order-design.md) | 跨分类实现顺序、SCC 切片和阶段计划 |

### 1. 依赖

| 序号 | 文档 | 内容 |
|---|---|---|
| 01 | [`FileCache` 底层设施替换矩阵](1-dependencies/01-filecache-infra-mapping.md) | CH 基础设施到 Velox 的总映射 |
| 02 | [基础 shims](1-dependencies/02-filecache-basic-shims-design.md) | locks、logging和 filesystem |
| 03 | [Metrics/debug shims](1-dependencies/03-filecache-metrics-debug-design.md) | metrics、tracing、cancellation和 failpoints |
| 04 | [线程池](1-dependencies/04-filecache-thread-pool-design.md) | global physical pool和 per-cache logical pool |
| 05 | [`FileCacheScheduler`](1-dependencies/05-filecache-scheduler-design.md) | timer与 callback execution |
| 06 | [Caller identity](1-dependencies/06-filecache-caller-token-design.md) | query id、physical TID和 downloader ownership |

### 2. `FileCache`

| 序号 | 文档 | 内容 |
|---|---|---|
| 01 | [Forward files](2-file-cache/01-filecache-fwd-files-design.md) | `FileCache_fwd.h` / `FileCache_fwd_internal.h` |
| 02 | [Origin和 segment type](2-file-cache/02-filecache-origin-segment-type-design.md) | `FileSegmentKeyType` / `FileCacheOriginInfo` |
| 03 | [Key/hash](2-file-cache/03-filecache-key-hash-design.md) | `FileCacheKey` / `sipHash128` |
| 04 | [Utils](2-file-cache/04-filecache-utils-design.md) | `FileCacheUtils.h` |
| 05 | [Sharded map](2-file-cache/05-filecache-sharded-map-design.md) | `ShardedMap.h` |
| 06 | [Settings](2-file-cache/06-filecache-settings-files-design.md) | `FileCacheSettings.h` / `FileCacheSettings.cpp` |
| 07 | [Priority/eviction](2-file-cache/07-filecache-priority-eviction-design.md) | priority和 eviction文件 |
| 08 | [Metadata](2-file-cache/08-filecache-metadata-files-design.md) | `FileSegmentInfo.h` / `Metadata.h` / `Metadata.cpp` |
| 09 | [`FileSegment`](2-file-cache/09-filecache-file-segment-design.md) | `FileSegment.h` / `FileSegment.cpp` |
| 10 | [`FileCache`](2-file-cache/10-filecache-core-files-design.md) | `FileCache.h` / `FileCache.cpp` |
| 11 | [`QueryLimit`](2-file-cache/11-filecache-query-limit-design.md) | `QueryLimit.h` / `QueryLimit.cpp` |
| 12 | [`FileCacheFactory`](2-file-cache/12-filecache-factory-files-design.md) | registry和 name/path dedup |

### 3. 使用方

| 序号 | 文档 | 内容 |
|---|---|---|
| 01 | [Read context](3-consumers/01-filecache-read-context-design.md) | `FileCacheReadOptions` / `FileCacheRequestContext` |
| 02 | [`FileCacheManager`](3-consumers/02-filecache-manager-design.md) | runtime ownership和 lifecycle |
| 03 | [Buffered input](3-consumers/03-filecache-buffered-input-design.md) | `FileCacheBufferedInput` / `FileCacheInputStream` |

## 核心决策

### 1. 目录布局

Velox 侧路径使用：

```text
velox/ch/
```

并按 ClickHouse `src` 下路径对齐，例如：

```text
src/Interpreters/FileCache/FileCache.h
  -> velox/ch/Interpreters/FileCache/FileCache.h

src/Disks/IO/CachedOnDiskReadBufferFromFile.cpp
  -> velox/ch/Disks/IO/FileCacheBufferedInput.cpp

src/IO/ReadBufferFromFileBase.h
  -> velox/ch/IO/ReadBufferFromVeloxReadFile.h
```

### 2. 主读路径

主读路径选择 `BufferedInput` 层：

```text
FileCacheBufferedInput : BufferedInput
FileCacheInputStream   : SeekableInputStream
```

`FileCacheInputStream::Next` 对应 CH `CachedOnDiskReadBufferFromFile::nextImplStep`。
这比把逻辑塞进 `ReadFile::pread` 更接近 CH 的 streaming reader 状态机。

### 3. Buffer adapter

保留两个最小 CH-style adapter：

```text
ReadBufferFromVeloxReadFile
WriteBufferFromVeloxWriteFile
```

它们分别接受 Velox `ReadFile` / `WriteFile`，内部用 `BufferPtr` /
`AlignedBuffer` 持有固定 IO buffer，并提供 CH reader/writer 路径需要的最小接口。

### 4. `FileCache` 算法

这些类作为算法类迁移，不用 Velox cache 类替代：

```text
FileCache
FileSegment
FileSegmentsHolder
CacheMetadata
KeyMetadata
FileSegmentMetadata
LockedKey
IFileCachePriority
LRUFileCachePriority
SLRUFileCachePriority
SplitFileCachePriority
OvercommitFileCachePriority
EvictionInfo
EvictionCandidates
CacheUsage
CacheUsagePerUser
FileCacheQueryLimit
FileCacheSettings
```

### 5. 生命周期

生命周期参考 Velox `AsyncDataCache` 的模式：

```text
FileCacheManager::create
FileCacheManager::getInstance / setInstance
FileCacheManager::shutdown
FileCacheManager::refreshStats
FileCacheManager::toString
```

但 `FileCacheManager` 不继承 `memory::Cache`，因为 `FileCache` 是磁盘 file segment
cache，不是内存页 cache。

Gluten集成中，process-global owner是 `gluten::VeloxBackend`，与其持有
`AsyncDataCache` 的模式一致。`VeloxRuntime` / Hive connector只使用 Manager，不负责
shutdown；`NativeBackendInitializer` 在 Spark Context停止后调用
`VeloxBackend::tearDown`关闭 Manager。

## Review 状态

当前范围内的依赖、`FileCache` 文件和使用方均已完成设计 review。每类文档按实现顺序放在
对应目录；跨分类的实际落地顺序统一由
[`01-filecache-port-order-design.md`](01-filecache-port-order-design.md)维护。

已确认后置或排除：

```text
WriteBufferToFileSegment        -> 后置；不属于读 miss填 cache主路径
CachedOnDiskWriteBufferFromFile -> 不迁移
cache_on_write_operations       -> 不支持
write-through cache             -> 不支持
```

## 当前落地策略

先按文件 DAG 落低依赖文件；到中心 SCC 后按功能闭环成组迁移。

第一批最小 PR：

```text
目录 + CMake target
基础 shim
FileCacheKey / FileSegmentKeyType / OriginInfo
FileCacheSettings parse/validate skeleton
ReadBufferFromVeloxReadFile
WriteBufferFromVeloxWriteFile
```

详见 [`01-filecache-port-order-design.md`](01-filecache-port-order-design.md)。

## 未决问题

1. `userId`、`weight` 如何从 Prestissimo/connector context 流入
   `FileCacheRequestContext`。
2. `FileIoContext::cacheable` 是否要映射到 `FileCacheRequestContext::cacheable`。
3. 第一阶段是否只接 DWIO/scan 的 `FileCacheBufferedInput`，还是同时添加
   `CacheFileSystem` / `CachedReadFile` 兜底。
4. ClickHouse system-table 输出中哪些应变成 Velox runtime stats，哪些应变成 debug
   API。
