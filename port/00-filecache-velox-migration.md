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

| 序号 | 文档 | 内容 |
|---|---|---|
| 00 | `00-filecache-velox-migration.md` | 总览、范围、核心决策、review 状态 |
| 01 | [`01-filecache-buffered-input-design.md`](01-filecache-buffered-input-design.md) | `FileCacheBufferedInput` / `FileCacheInputStream` 读路径设计 |
| 02 | [`02-filecache-settings-design.md`](02-filecache-settings-design.md) | 配置分层：`FileCacheConfig` / `FileCacheReadOptions` / `FileCacheRequestContext` |
| 03 | [`03-filecache-manager-lifecycle-design.md`](03-filecache-manager-lifecycle-design.md) | `FileCacheManager` 生命周期、全局入口、shutdown、stats |
| 04 | [`04-filecache-infra-mapping.md`](04-filecache-infra-mapping.md) | CH 底层设施到 Velox 的替换矩阵 |
| 05 | [`05-filecache-port-order-design.md`](05-filecache-port-order-design.md) | 文件落地顺序、SCC 切片、阶段计划 |
| 06 | [`06-filecache-key-hash-design.md`](06-filecache-key-hash-design.md) | `FileCacheKey` 和 `sipHash128` 兼容设计 |
| 07 | [`07-filecache-scheduler-design.md`](07-filecache-scheduler-design.md) | `FileCacheScheduler` 调度封装设计 |
| 08 | [`08-filecache-caller-token-design.md`](08-filecache-caller-token-design.md) | `getCallerId` / downloader ownership token 设计 |
| 09 | [`09-filecache-thread-pool-design.md`](09-filecache-thread-pool-design.md) | `ThreadFromGlobalPool` / `ThreadPool` 迁移设计 |
| 10 | [`10-filecache-metrics-debug-design.md`](10-filecache-metrics-debug-design.md) | metrics / tracing / debug no-op shim 设计 |
| 11 | [`11-filecache-basic-shims-design.md`](11-filecache-basic-shims-design.md) | locks / logging / filesystem basic shims 设计 |
| 12 | [`12-filecache-origin-segment-type-design.md`](12-filecache-origin-segment-type-design.md) | `FileSegmentKeyType` / `FileCacheOriginInfo` 设计 |

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

## Review 状态总表

本设计按三类对象推进：

```text
使用方 -> FileCache -> 依赖方
```

- **使用方**：Velox/DWIO 如何调用 cache。`FileCacheBufferedInput` /
  `FileCacheInputStream`、settings/read options/request context、manager 生命周期、
  caller token 都属于这一类。当前使用方设计基本结束，只剩具体接入时的字段来源细节。
- **`FileCache` 本体**：`FileCache`、`FileSegment`、`CacheMetadata`、priority、query
  limit 等算法类。当前只定了原则：算法迁移，不用 Velox cache 替代；中心 SCC 按功能闭环迁移。
- **依赖方**：`FileCache` 算法依赖的底层设施，例如 hash、scheduler、thread pool、
  metrics、locks、logging、fs、debug/cancellation hooks。当前正在 review 这一层。

| 分类 | CH | Velox / 迁移设计 | 状态 |
|---|---|---|---|
| 使用方 | `CachedOnDiskReadBufferFromFile` | `FileCacheBufferedInput` / `FileCacheInputStream`，详见 `01` | 已 review |
| 使用方 | `Settings` / `ReadSettings` / `FilesystemCacheSettings` | `FileCacheConfig` / `FileCacheReadOptions` / `FileCacheRequestContext`，详见 `02` | 已 review |
| 使用方 | cache 获取和生命周期 | `FileCacheManager`，详见 `03` | 已 review |
| 使用方 | `getThreadId` / `getCallerId` | `FileCacheCallerToken`，显式表达 downloader ownership；详见 `08` | 已 review |
| 依赖方 | `ReadBufferFromFileBase` | `ReadBufferFromVeloxReadFile`，接受 Velox `ReadFile`，内部自带 `BufferPtr` | 已 review |
| 依赖方 | `WriteBufferFromFile` | `WriteBufferFromVeloxWriteFile`，接受 Velox `WriteFile`，内部自带 `BufferPtr` | 已 review |
| 依赖方 | `WriteBufferToFileSegment` | 只服务 `TemporaryDataOnDisk` 写 `Ephemeral` segment；第一阶段不迁移 | 已确认后置 |
| 依赖方 | `CachedOnDiskWriteBufferFromFile` | 当前不支持，write-through cache 暂不迁移 | 已确认范围 |
| 依赖方 | `OpenedFileCache` | `OpenedFileCache` alias/wrapper 到独立 `FileHandleFactory` / `FileHandleCache` 实例 | 已 review |
| 依赖方 | `Poco::Util::AbstractConfiguration` | `ConfigBase` | 已 review |
| 依赖方 | `NamedCollection` | connector properties / `ConfigBase` prefix | 已 review |
| 依赖方 | `BackgroundSchedulePool` | `FileCacheScheduler` wrapper over `folly::FunctionScheduler`，详见 `07` | 已 review |
| 依赖方 | `ThreadFromGlobalPool` / `ThreadPool` | `FileCacheWorker` / `FileCacheThreadPool`，用 `using` 保留 CH 名字；详见 `09` | 待 review |
| 依赖方 | `sipHash128` | 保留小 helper，不直接换 `SpookyHashV2`；详见 `06` | 已 review |
| `FileCache` 本体 | `FileSegmentKeyType` / `FileCacheOriginInfo` | 直接迁移 CH 语义；详见 `12` | 已 review |
| 依赖方 | CH locks / `std::shared_mutex` | CH-compatible guard classes + `folly::SharedMutex` / `std::mutex`；详见 `11` | 待 review |
| 依赖方 | `LOG_*` / `logger_useful` / `fs::` | logging and filesystem compat shims；详见 `11` | 待 review |
| 依赖方 | `ProfileEvents` / `CurrentMetrics` | no-op shim，保留 CH 调用点；详见 `10` | 待 review |
| 依赖方 | `QueryStatus::throwIfKilled` / `OpenTelemetry` / `FailPoint` / `assertCacheCorrectness*` | no-op shim，cancellation 后续接 `ConnectorQueryCtx::cancellationToken`；详见 `10` | 待 review |
| `FileCache` 本体 | `FileCache` / `FileSegment` / `CacheMetadata` / priorities | 算法迁移；中心 SCC 按功能闭环 review | 原则已定，未 review |

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

详见 [`05-filecache-port-order-design.md`](05-filecache-port-order-design.md)。

## 未决问题

1. `userId`、`weight` 如何从 Prestissimo/connector context 流入
   `FileCacheRequestContext`。
2. `FileIoContext::cacheable` 是否要映射到 `FileCacheRequestContext::cacheable`。
3. 第一阶段是否只接 DWIO/scan 的 `FileCacheBufferedInput`，还是同时添加
   `CacheFileSystem` / `CachedReadFile` 兜底。
4. ClickHouse system-table 输出中哪些应变成 Velox runtime stats，哪些应变成 debug
   API。
