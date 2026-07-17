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

| 分类 | CH | Velox / 迁移设计 | 状态 |
|---|---|---|---|
| IO | `ReadBufferFromFileBase` | `ReadBufferFromVeloxReadFile`，接受 Velox `ReadFile`，内部自带 `BufferPtr` | 已 review |
| IO | `WriteBufferFromFile` | `WriteBufferFromVeloxWriteFile`，接受 Velox `WriteFile`，内部自带 `BufferPtr` | 已 review |
| IO | `WriteBufferToFileSegment` | 保留为临时数据 / cache segment 写入 adapter，不承载 `cache_on_write_operations` | 待 review |
| IO | `CachedOnDiskWriteBufferFromFile` | 当前不支持，write-through cache 暂不迁移 | 已确认范围 |
| IO | `OpenedFileCache` | `OpenedFileCache` alias/wrapper 到独立 `FileHandleFactory` / `FileHandleCache` 实例 | 已 review |
| 配置 | `Poco::Util::AbstractConfiguration` | `ConfigBase` | 已 review |
| 配置 | `NamedCollection` | connector properties / `ConfigBase` prefix | 已 review |
| 配置 | `Settings` / `ReadSettings` / `FilesystemCacheSettings` | `FileCacheConfig` / `FileCacheReadOptions` / `FileCacheRequestContext` | 已 review |
| 调度 | `BackgroundSchedulePool` | `FileCacheScheduler` wrapper over `folly::FunctionScheduler`，详见 `07` | 已 review |
| 线程 | `ThreadFromGlobalPool` / `ThreadPool` | `FileCacheWorker` / `FileCacheThreadPool`，用 `using` 保留 CH 名字；详见 `09` | 待 review |
| Hash | `sipHash128` | 保留小 helper，不直接换 `SpookyHashV2`；详见 `06` | 已 review |
| 身份 | `getThreadId` / `getCallerId` | `FileCacheCallerToken`，显式表达 downloader ownership；详见 `08` | 已 review |
| 锁 | CH locks / `std::shared_mutex` | `folly::SharedMutex` / `std::mutex` / thin typedef | 待 review |
| 日志 | `LOG_*` / `logger_useful` | `LOG` / `VLOG` / `FB_LOG_EVERY_MS` | 待 review |
| 指标 | `ProfileEvents` / `CurrentMetrics` | `FileCacheMetrics`，后续接 `RuntimeMetric` / `IoStats` / `StatsReporter` | 待 review |
| 调试 | `OpenTelemetry` / `FailPoint` / `assertCacheCorrectness*` | 当前剥离/后置，保留接口位置 | 待 review |

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
