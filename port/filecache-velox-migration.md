# ClickHouse `FileCache` 到 Velox 的迁移设计

## 目标

把完整的 ClickHouse `FileCache` 功能和算法迁移到
`/home/chang/OpenSource/velox`，同时把 ClickHouse 的基础设施替换成 Velox
基础设施。

迁移后需要保留 ClickHouse 的缓存语义：

- 文件段生命周期和部分下载状态机
- 从本地缓存目录加载和恢复元数据
- 容量预留和驱逐
- `LRU`、`SLRU`、split cache、overcommit 驱逐策略
- 单 query 缓存写入限额
- 按 user/origin 统计，以及 idle client 驱逐
- 后台下载、后台维持空闲空间、invalidated entry 清理
- 动态 resize 和可 reload 的设置
- 指标和缓存状态检查

当前阶段明确不支持 `cache_on_write_operations` / write-through cache。读路径 miss
填充 `FileSegment` 所需的本地 cache 写入仍然需要迁移，但对象写入同时填充 cache 的
外层 write-through 逻辑不在当前范围。

迁移时不应该用 Velox `AsyncDataCache` 或 `SsdCache` 重新实现这些算法。
这两个 Velox 类的语义不同：它们是以内存页缓存和可选 SSD 缓存为核心，
围绕 `CachePin`、exclusive/shared entry、score-based eviction 构建。
它们不应该替代 ClickHouse `FileCache` 的算法类。

## 推荐接入方式

使用 `CacheFileSystem` 作为 Velox 的主接入口。

`CacheFileSystem` 应该是已经解析出的真实 `FileSystem` 外面的一层
decorator，而不是一个普通的 scheme matcher。它不应该和 `s3`、`hdfs`、
`gcs`、`file` 这些已有 scheme 抢匹配。

推荐查找流程：

```text
filesystems::getFileSystem(path, config)
  -> 通过现有 scheme registry 解析真实文件系统
     例如 S3FileSystem / HdfsFileSystem / GcsFileSystem / LocalFileSystem
  -> 如果启用了 filesystem cache:
         return CacheFileSystem(real_fs, file_cache_manager, config)
     否则:
         return real_fs
```

这样可以避免：

- 抢占已有 scheme
- 通过 `getFileSystem` 递归解析 `CacheFileSystem`
- 要求调用方把路径改写成 `cache://...`

`BufferedInput` 接入应该作为 Velox scan / DWIO 读路径的主方案，因为它比
`ReadFile` 更接近 ClickHouse `CachedOnDiskReadBufferFromFile` 的 streaming
reader 语义。

## `FileCacheBufferedInput`

详细的 `FileCacheBufferedInput` 集成设计已经拆到
[`filecache-buffered-input-design.md`](filecache-buffered-input-design.md)。

## 与 Velox cache 共存

Velox 已经有 `AsyncDataCache`、`SsdCache`、`CachedBufferedInput` 和
`CacheInputStream`。

新的 `FileCache` 应该按下面方式共存：

```text
FileCacheBufferedInput / FileCacheInputStream
  承载 ClickHouse FileCache 读路径语义

CachedBufferedInput / CacheInputStream
  继续承载 Velox AsyncDataCache 语义
  不应该和 FileCacheBufferedInput 同时缓存同一份 raw bytes
```

当某条 scan 路径选择 `FileCacheBufferedInput` 时，`CachedBufferedInput` 应该
二选一：

- 不参与这条路径；
- 或者通过 `cacheable=false` / marker 跳过 `AsyncDataCache` retention。

这样可以避免：

```text
remote bytes -> FileCache local segment -> AsyncDataCache memory entry
```

变成意外的双重缓存。

## 新增 Velox 侧模块

建议位置：

```text
velox/ch/
```

按 ClickHouse `src` 下路径对齐的主要文件：

```text
velox/ch/Interpreters/FileCache/FileCache.h / .cpp
velox/ch/Interpreters/FileCache/FileSegment.h / .cpp
velox/ch/Interpreters/FileCache/Metadata.h / .cpp
velox/ch/Interpreters/FileCache/IFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
velox/ch/Interpreters/FileCache/OvercommitFileCachePriority.h
velox/ch/Interpreters/FileCache/EvictionCandidates.h / .cpp
velox/ch/Interpreters/FileCache/CacheUsage.h
velox/ch/Interpreters/FileCache/FileCacheSettings.h / .cpp
velox/ch/Interpreters/FileCache/FileCacheFactory.h / .cpp
velox/ch/Disks/IO/FileCacheBufferedInput.h / .cpp
velox/ch/Disks/IO/FileCacheInputStream.h / .cpp
velox/ch/IO/ReadBufferFromVeloxReadFile.h / .cpp
velox/ch/Interpreters/FileCache/WriteBufferToFileSegment.h / .cpp
velox/ch/Common/FileCacheScheduler.h / .cpp
```

## 替换映射

### IO

| ClickHouse 依赖 | Velox 替换项 | 是否人工审查过 |
|---|---|---|
| `ReadBufferFromFileBase` | 一个接受 Velox `ReadFile` 的 `ReadBufferFromVeloxReadFile`，内部自带缓冲区 | 是 |
| `WriteBufferFromFile` | 一个接受 Velox `WriteFile` 的 `WriteBufferFromVeloxWriteFile`，内部自带 `BufferPtr` 缓冲区 | 是 |
| `WriteBufferToFileSegment` | 保留为临时数据 / cache segment 写入 adapter；不承载 `cache_on_write_operations` | 否 |
| `CachedOnDiskWriteBufferFromFile` | 当前不支持；`cache_on_write_operations` / write-through cache 暂不迁移 | 否 |
| `OpenedFileCache` | 用 `using OpenedFileCache = FileHandleFactory` 一类 alias 保留 CH 名字；为 `FileCache` 本地 segment 单独持有实例，删除 segment 时 invalidate 对应 handle | 是 |

#### `OpenedFileCache` 对应关系

ClickHouse `OpenedFileCache` 的作用是缓存本地只读文件的打开句柄，避免读本地 cache
segment 时反复 open/close。Velox 已有 `FileHandleFactory` / `FileHandleCache`
机制，可以复用这个机制，但保留 ClickHouse 名字，方便迁移代码对齐。

建议在 `velox/ch/Interpreters/FileCache` 内部做 alias：

```cpp
using OpenedFile = FileHandle;
using OpenedFileCache = FileHandleFactory;
using OpenedFileCachePtr = std::shared_ptr<OpenedFileCache>;
using OpenedFilePtr = FileHandleCachedPtr;
```

`FileCache` 或 `FileCacheManager` 单独持有一个 `OpenedFileCache` 实例：

```cpp
class FileCacheManager
{
private:
    OpenedFileCache openedFileCache_;
};
```

不要复用 Hive connector 里的 `fileHandleFactory_` 实例。Hive connector 的
`FileHandleCache` 是 connector 级共享，用于 source 文件；`FileCache` 本地 segment
有自己的生命周期，删除 segment 时必须 invalidate 对应本地 handle。

迁移 CH 逻辑时可以保持类似调用形态：

```text
openedFileCache.get(path)
openedFileCache.remove(path)
```

底层实现可以委托 `FileHandleFactory::generate` 和 `FileHandleFactory::clearCache`。
如果需要按单个 path 删除，而现有 `CachedFactory` 没有单 key remove，就在
`OpenedFileCache` 外面包一个很薄的 wrapper，仍然把核心类型 alias 到
`FileHandleFactory`。

读路径：

```text
FileCacheInputStream::Next
  -> FileCache::getOrSet
  -> 缓存命中：读取本地 cache segment
  -> 缓存未命中：读取 inner ReadFile，写入 FileSegment，并返回字节
```

当前范围没有对象写入的 write-through 路径；只迁移读 miss 后填充本地 cache
segment 所需的 `FileSegment::write` 底层能力。

### 配置

| ClickHouse 依赖 | Velox 替换项 | 是否人工审查过 |
|---|---|---|
| `Poco::Util::AbstractConfiguration` | `velox::config::ConfigBase` | 是 |
| `NamedCollection` | connector properties / `ConfigBase` prefix | 是 |
| `Settings`, `ReadSettings`, `FilesystemCacheSettings` | `FileCacheConfig` / `FileCacheReadOptions` / `FileCacheRequestContext` | 是 |

ClickHouse 设置名应该尽量保留，并翻译成带前缀的 Velox config key，例如：

```text
file-cache.path
file-cache.max-size
file-cache.max-elements
file-cache.cache-policy
file-cache.background-download-threads
```

详细配置分层设计见 [`filecache-settings-design.md`](filecache-settings-design.md)。

### 生命周期管理

`FileCache` 实例创建、全局入口、shutdown、stats 和 `OpenedFileCache` 持有方式见
[`filecache-manager-lifecycle-design.md`](filecache-manager-lifecycle-design.md)。

### 底层设施替换

ClickHouse `FileCache` 依赖的底层设施到 Velox 的替换矩阵见
[`filecache-infra-mapping.md`](filecache-infra-mapping.md)。

### 落地顺序

按文件 DAG 和中心 SCC 功能切片的具体迁移顺序见
[`filecache-port-order-design.md`](filecache-port-order-design.md)。

### 线程和调度

| ClickHouse 依赖 | Velox 替换项 | 是否人工审查过 |
|---|---|---|
| `ThreadPool` | `folly::CPUThreadPoolExecutor` 或 `folly::IOThreadPoolExecutor` | 否 |
| `ThreadFromGlobalPool` | 提交到自有 executor 的 task | 否 |
| `BackgroundSchedulePoolTaskHolder` | 新的 `FileCacheScheduler` wrapper | 否 |
| `callOnce`, `OnceFlag` | `folly::once_flag` / `folly::call_once` 或 `std::once_flag` | 否 |

`FileCacheScheduler` 只需要暴露 `FileCache` 需要的操作：

```text
scheduleAfter(delay)
cancel()
shutdown()
```

这样可以把 ClickHouse 的调度假设隔离在 Velox 适配层里。

### Query 和 user 上下文

| ClickHouse 依赖 | Velox 替换项 | 是否人工审查过 |
|---|---|---|
| `CurrentThread::getQueryId` | 从 Velox connector/query context 显式传入 query id | 否 |
| `ThreadStatus` | 不做直接依赖；显式传入需要的字段 | 否 |
| `FileCacheOriginInfo::user_id` | 从 connector/session context 获取 user/client id | 否 |
| `FileIoContext::cacheable` | 单次读取的 cacheable hint | 否 |

不要依赖全局线程状态。Velox 集成层应该显式传递一个紧凑的 cache context：

```text
struct FileCacheRequestContext {
  std::string queryId;
  std::string userId;
  uint64_t userWeight;
  bool cacheable;
  FileSegmentKeyType segmentType;
};
```

`FileCacheBufferedInput` 可以从 `FileIoContext`、`FileOptions`、`ReaderOptions` 或
connector-specific properties 推导这个 context。

### 指标和可观测性

| ClickHouse 依赖 | Velox 替换项 | 是否人工审查过 |
|---|---|---|
| `CurrentMetrics` | 内部 atomics + stats snapshot，之后可接 `StatsReporter` | 否 |
| `ProfileEvents` | `IoStatistics`, `IoStats`, `RuntimeMetric`, `RuntimeCounter` | 否 |
| `DimensionalMetrics`, `HistogramMetrics` | Velox stats/reporting wrapper | 否 |
| 通过 `ColumnsDescription` 输出 system table | Velox stats/config dump API | 否 |

第一版迁移应该保留一个本地 `FileCacheStats` 结构。之后再接入 Velox runtime
stats 和导出指标。

### 异常和日志

| ClickHouse 依赖 | Velox 替换项 | 是否人工审查过 |
|---|---|---|
| `Exception`, `ErrorCodes::LOGICAL_ERROR` | `VELOX_FAIL` / `VELOX_CHECK` / `VeloxRuntimeError` | 否 |
| 用户输入错误 | `VELOX_USER_FAIL` / `VeloxUserError` | 否 |
| `logger_useful` | `LOG`, `VLOG`, `FB_LOG_EVERY_MS` | 否 |

边界上使用 Velox 异常；但当错误消息描述 cache invariant 时，尽量保留接近
ClickHouse 的表达。

### 基础类型和 helper

| ClickHouse 依赖 | Velox 替换项 | 是否人工审查过 |
|---|---|---|
| `String` | `std::string` | 否 |
| `UInt64`, `UInt128` | `uint64_t`，自定义 `FileCacheKeyHash` / 128-bit helper | 否 |
| `UUID`, `ServerUUID` | 仅在仍需要时用显式配置或本地 UUID helper | 否 |
| `SipHash`, `hex`, `randomSeed` | Velox/folly helper 或迁移小型 helper | 否 |
| `base/unit.h` | Velox 风格常量 | 否 |
| `SharedMutex`, `SharedLockGuard` | `folly::SharedMutex`、`std::shared_mutex`，或薄封装 | 否 |
| `scope_guard` | `folly::ScopeGuard` | 否 |

不要把 ClickHouse `FileCacheKey` 映射成 Velox `cache::FileCacheKey`。
Velox `cache::FileCacheKey` 是 `fileNum + offset`，而 ClickHouse
`FileCacheKey` 标识 origin object，并用于 metadata path。

## 需要保留的类

下面这些类应该作为算法类迁移，而不是被 Velox cache 类替换：

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

Velox 基础设施应该围绕这些类出现，而不是进入它们的算法内部；只有 IO、
调度、日志、指标、异常和上下文传递需要替换。

## 落地策略

在到达中心 SCC 之前，按文件级 DAG 顺序迁移。到达中心 SCC 后，按功能 SCC
切片落地，而不是强行逐文件拆。

### 文件级分层

第 0 层：

```text
FileCacheKey
FileCacheUtils
FileCache_fwd
FileCache_fwd_internal
FileSegmentKeyType
Guards
ShardedMap
```

第 1 层：

```text
FileCacheOriginInfo
FileCacheSettings
```

第 2 层：

```text
IFileCachePriority
```

第 3 层：

```text
CacheUsage
FileSegmentInfo
SplitFileCachePriority
```

第 4 层：

```text
EvictionCandidates
FileSegment
LRUFileCachePriority
```

第 5 层：

```text
Metadata
QueryLimit
SLRUFileCachePriority
WriteBufferToFileSegment replacement
```

第 6 层：

```text
FileCache
```

第 7 层：

```text
FileCacheFactory
CacheFileSystem
FileCacheBufferedInput
FileCacheInputStream
ReadBufferFromVeloxReadFile
```

### 中心 SCC 功能切片

到达中心 SCC 时，按下面这些完整单元落地：

1. `Metadata`, `KeyMetadata`, `LockedKey`, `FileSegmentMetadata`
2. `FileSegment` 状态机和 `FileSegmentsHolder`
3. `FileCache::get`, `FileCache::getOrSet`, `FileCache::set`，以及 segment 创建
4. reserve 和 eviction：`tryReserve`, `doTryReserve`, `doEviction`, `EvictionCandidates`
5. completion 和 background download
6. query limit、dynamic resize、cleanup、idle client eviction

这样可以避免为了临时编译而创造假接口或半成品实现。

## 实施阶段

### 阶段 1：核心编译骨架

- 创建 `velox/ch/...`，目录结构按 ClickHouse `src` 下路径对齐
- 迁移基础类型、guard、settings、key/origin 类型
- 用 Velox/基础 C++ 类型替换 ClickHouse 类型
- 添加 CMake target 和最小测试

### 阶段 2：Priority 算法

- 迁移 `IFileCachePriority`
- 迁移 `LRU`、`SLRU`、split、overcommit policy
- 迁移 `EvictionInfo`、`EvictionCandidates`、`CacheUsage`
- 只替换 metrics/logging/errors 基础设施

### 阶段 3：Metadata 和 segment 生命周期

- 迁移 `Metadata`
- 迁移 `FileSegment`
- 用 Velox `ReadFile` / `WriteFile` adapter 替换 ClickHouse buffer
- 保持 segment 状态机语义不变

### 阶段 4：`FileCache` 编排

- 迁移 `FileCache`
- 接通 reserve、eviction、completion、metadata load、background task
- 实现 Velox scheduler 和 settings reload bridge

### 阶段 5：Velox scan 读路径接入

- 添加 `FileCacheBufferedInput`
- 添加 `FileCacheInputStream`
- 接入 DWIO/scan 创建 `BufferedInput` 的位置
- 确保不和 `CachedBufferedInput` / `AsyncDataCache` 双重缓存

### 阶段 6：通用 `FileSystem` 兜底

- 如确实需要覆盖非 scan 的 `ReadFile` 调用，再添加 `CacheFileSystem` /
  `CachedReadFile` 兜底
- 兜底路径不能替代 `FileCacheBufferedInput` 的主读路径状态机
- 确保不和 S3/HDFS/GCS/local scheme 冲突

### 阶段 7：测试和验证

- 尽量迁移 ClickHouse `FileCache` 单元测试
- 添加 Velox `ReadFile` / `FileSystem` wrapper 测试
- 添加 S3/HDFS/local fake filesystem 测试
- 添加 downloader、reserve、eviction、cleanup 并发测试
- 添加 restart/metadata-load 测试
- 添加 dynamic resize 和 idle-client eviction 测试

## 未决设计点

1. 确切 config prefix，以及 settings 是全局还是每个 filesystem/cache instance
   一份。
2. `queryId`、`userId`、`weight` 如何从 Prestissimo/connector context 流入
   `FileIoContext`。
3. 第一阶段是否只接 DWIO/scan 的 `FileCacheBufferedInput`，还是同时添加
   `CacheFileSystem` / `CachedReadFile` 兜底。
4. ClickHouse system-table 输出里有多少应该变成 Velox runtime stats，有多少
   应该变成 debug API。

## 建议

以 `FileCacheBufferedInput` 作为 scan 读路径主接入口，并保持 ClickHouse
`FileCache` 算法完整。只替换基础设施：配置、IO handle、调度、指标、日志、
异常和上下文传递。

迁移落地时，在中心 SCC 之前按文件 DAG 推进；到中心 SCC 后按功能切片推进。
这样既能让早期 patch 易于 review，又能保留 `FileCache` 正确性依赖的语义环。
