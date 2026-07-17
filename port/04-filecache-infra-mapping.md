# `FileCache` 底层设施替换矩阵

这个文档记录 ClickHouse `FileCache` 依赖的底层设施在 Velox 中的对应关系。
它用于指导迁移时的 shim / wrapper 设计，不改变 `FileCache` 自身算法。

## 处理方式定义

| 处理方式 | 含义 |
|---|---|
| 直接替换 | Velox 侧已有语义足够接近，可以直接使用 |
| wrapper | Velox 侧有底层能力，但需要一个 CH-style wrapper 隔离语义差异 |
| 保留小 helper | 为兼容 CH 行为，应迁移或保留小型 helper，而不是换算法 |
| 剥离 / 后置 | 当前阶段不实现；保留接口或后续再接 |

## 替换矩阵

| CH | Velox | 处理方式 | review 状态 |
|---|---|---|---|
| `String` / `UInt64` / `Int64` 等 CH 基础别名 | `velox/ch/Common/ClickHouseAliases.h` 中提供 `using String = std::string` 等；`UInt8` 用 `uint8_t`，不照搬 CH 的 `char8_t` | compat alias | 已 review |
| `BackgroundSchedulePool`（定时 + 并行执行 + 提前触发） | cancelable `folly::Timekeeper` future + shared `FileCacheWorkerPool`；详见 `07-filecache-scheduler-design.md` | wrapper + CH aliases | 已 review |
| `ThreadFromGlobalPool` / 线程池 | `FileCacheWorker` / `FileCacheThreadPool`，通过 `using ThreadFromGlobalPool = FileCacheWorker` 和 `using ThreadPool = FileCacheThreadPool` 保留 CH 名字，详见 `09-filecache-thread-pool-design.md` | wrapper：保留 CH-style join/resize/shutdown 语义 | 需要 review |
| `WriteBufferFromFile` / `ReadBufferFromFileBase` | `WriteFile` / `ReadFile` | wrapper：`WriteBufferFromVeloxWriteFile` / `ReadBufferFromVeloxReadFile` | 已 review |
| `ConcurrentBoundedQueue` | `folly::MPMCQueue<std::optional<T>>` + sentinel termination | 直接替换；保留 bounded/blocking/timed/drain 语义 | 已 review |
| `fs::` 文件系统操作 | `std::filesystem` + 必要时 Velox `FileSystem` local API；详见 `11-filecache-basic-shims-design.md` | compat shim | 需要 review |
| `sipHash128` | 不直接换成 `SpookyHashV2`；需要保留 CH cache key hash 语义，详见 `06-filecache-key-hash-design.md` | 保留小 helper | 已 review |
| `absl::flat_hash_map` / `absl::flat_hash_set` | 默认用 `folly::F14FastMap` / `folly::F14FastSet`；需要 value 地址稳定时再用 `F14NodeMap` / `F14NodeSet` | 直接替换，详见 `06` 和 `13` | 已 review |
| `std::shared_mutex` / CH 锁 | CH-compatible guard classes，内部用 `folly::SharedMutex` / `std::mutex`；详见 `11-filecache-basic-shims-design.md` | compat shim | 需要 review |
| `LOG_*` / `logger_useful` | CH-compatible logging macros，内部用 `LOG` / `VLOG`；详见 `11-filecache-basic-shims-design.md` | compat shim | 需要 review |
| `getThreadId` / `getCallerId` | `FileCacheQueryIdScope` 提供 query id，`folly::getOSThreadID` 提供当前物理线程 identity；详见 `08-filecache-caller-token-design.md` | compat scope：保留 CH execution ownership | 已 review |
| `ProfileEvents` / `CurrentMetrics` | no-op shim，保留 CH 调用点；后续再接 `RuntimeMetric` / `IoStats` / `StatsReporter`，详见 `10-filecache-metrics-debug-design.md` | using/compat shim | 需要 review |
| `OpenTelemetry` | no-op `OpenTelemetry::SpanHolder` shim，详见 `10-filecache-metrics-debug-design.md` | using/compat shim | 需要 review |
| `QueryStatus::throwIfKilled` | no-op `QueryStatus` shim；后续接 `ConnectorQueryCtx::cancellationToken`，详见 `10-filecache-metrics-debug-design.md` | compat shim，后续补语义 | 需要 review |
| `FailPoint` | no-op failpoint shim，详见 `10-filecache-metrics-debug-design.md` | using/compat shim | 需要 review |
| `assertCacheCorrectness*` | no-op correctness shim，详见 `10-filecache-metrics-debug-design.md` | using/compat shim | 需要 review |

## 重点说明

### `ReadBufferFromVeloxReadFile`

不要用 Velox `ReadFileInputStream` / `SeekableFileInputStream` 去模拟
ClickHouse `ReadBufferFromFileBase`，也不要引入新的 reader 层级。这里只需要一个
接受 Velox `ReadFile` 的 CH-style reader：**`ReadBufferFromVeloxReadFile`**。

它等价于迁移版 `ReadBufferFromFileBase`：自己持有缓冲区，底层通过
`ReadFile::pread` 做 positioned read。

```cpp
class ReadBufferFromVeloxReadFile /* : public ReadBufferFromFileBase */
{
public:
    ReadBufferFromVeloxReadFile(
        std::shared_ptr<ReadFile> file,
        FileIoContext context,
        size_t bufferSize);

    void seek(uint64_t offset);
    void setReadUntilPosition(uint64_t end);
    void setReadUntilEnd();

    bool next();
    bool eof();

    void set(char * data, size_t size); // 可选：复用外部 buffer
    char * position();
    void advance(size_t size);
    size_t available() const;

    uint64_t getPosition() const;
    uint64_t getFileOffsetOfBufferEnd() const;

private:
    std::shared_ptr<ReadFile> file_;
    FileIoContext context_;
    BufferPtr buffer_;
    char * externalBuffer_ = nullptr;
    size_t externalBufferSize_ = 0;
    size_t available_ = 0;
    size_t positionInBuffer_ = 0;
    uint64_t currentOffset_ = 0;
    uint64_t readUntil_ = 0;
};
```

固定 IO buffer 按 Velox 习惯使用 `BufferPtr` / `AlignedBuffer`：

```cpp
buffer_ = AlignedBuffer::allocate<char>(bufferSize, pool);
```

`next` 的核心逻辑：

```text
buffer = externalBuffer ? externalBuffer : buffer_->asMutable<char>()
size = min(buffer.size, readUntil - currentOffset)
file->pread(currentOffset, size, buffer)
available = size
positionInBuffer = 0
currentOffset += size
```

同一个类可以包装两种 Velox `ReadFile`：

- 远端/真实源文件的 `ReadFile`；
- 本地 cache segment 文件的 `ReadFile`。

本地 cache segment 文件必须直接用 local filesystem 打开，不能再走
`FileCacheBufferedInput`，否则会递归进入 cache。

### `WriteBufferFromVeloxWriteFile`

`WriteBufferFromVeloxWriteFile` 是 `ReadBufferFromVeloxReadFile` 的对称类：接受
Velox `WriteFile`，内部自带 Velox `BufferPtr` 缓冲区，对 `FileSegment::write`
提供 ClickHouse 风格的 write buffer 接口。

```cpp
class WriteBufferFromVeloxWriteFile
{
public:
    WriteBufferFromVeloxWriteFile(
        std::unique_ptr<WriteFile> file,
        memory::MemoryPool & pool,
        size_t bufferSize);

    void set(char * data, size_t size, size_t offset);
    void next();

    void finalize();
    void sync();
    void cancel();

    uint64_t writtenBytes() const { return writtenBytes_; }

private:
    std::unique_ptr<WriteFile> file_;
    BufferPtr buffer_;

    char * externalBuffer_ = nullptr;
    size_t externalBufferSize_ = 0;
    size_t externalOffset_ = 0;

    uint64_t writtenBytes_ = 0;
    bool finalized_ = false;
    bool canceled_ = false;
};
```

固定写 buffer 同样按 Velox 习惯用 `BufferPtr` / `AlignedBuffer`：

```cpp
buffer_ = AlignedBuffer::allocate<char>(bufferSize, &pool);
```

`set` 的语义对应 ClickHouse `WriteBuffer::set`：调用方可以把一段外部 memory
临时挂给 writer，`next` 负责把它写入 `WriteFile`。

```text
set(data, size, offset)
  -> externalBuffer = data
  -> externalBufferSize = size
  -> externalOffset = offset

next()
  -> buffer = externalBuffer ? externalBuffer : buffer_->asMutable<char>()
  -> file_->append({buffer, externalBufferSize})
  -> writtenBytes += externalBufferSize
```

在 `FileSegment::DownloadState` 中：

```cpp
struct DownloadState
{
    std::shared_ptr<ReadBufferFromVeloxReadFile> remoteReader;
    std::shared_ptr<WriteBufferFromVeloxWriteFile> cacheWriter;
};
```

这里的 `shared_ptr` 管理 writer wrapper，保持 ClickHouse `LocalCacheWriterPtr` 的
ownership 和 `getLocalCacheWriter` 接口。`WriteBufferFromVeloxWriteFile` 内部的
`std::unique_ptr<WriteFile> file_` 不变，底层 file handle 仍然只有一个 owner。

`FileSegment::write` 中的调用保持和 CH 现有逻辑一致：

```text
if (!download.cacheWriter)
    download.cacheWriter = open local cache segment WriteFile

download.cacheWriter->set(from, size, size)
download.cacheWriter->next()
downloadedSize += size
```

`finalize` 调 `WriteFile::flush` / `close`。`sync` 至少应 flush；如果 Velox
`WriteFile` 实现没有显式 fsync 语义，第一版只映射到 `flush`。`cancel` 用于写失败：
标记 canceled，释放 writer，后续由 `FileSegment::setDownloadFailed` 处理 segment
状态和残留文件。

#### short write

CH `WriteBufferFromFileDescriptor::nextImpl` 会循环处理 short write。Velox
`LocalWriteFile::append` 当前只调用一次 `::write`，返回长度小于请求长度时直接抛异常，
且内部 size 不记录已经写入的部分。

例如：

```text
requested: 1 MiB
written:   256 KiB
then:      ENOSPC / EDQUOT / I/O failure
```

此时文件已经增长 256 KiB，但 `FileSegment::downloaded_size` 尚未更新。wrapper 不能直接
采用一次 `append` 的失败语义，否则 metadata 与实际文件大小会分叉。

`WriteBufferFromVeloxWriteFile` 使用自己的 in-memory `writtenBytes` 记录正常进度。正常
路径只调用 `WriteFile::append` 并更新计数，不增加 `stat` / `file_size` syscall。

只有 `append` 抛异常时：

```text
read actual on-disk file size
if the write made progress:
  continue with the unwritten suffix
if no further progress is possible:
  expose the actual size to FileSegment
  mark download failed
  propagate the exception
```

禁止解析 Velox exception message 中的 errno，也不能把失败伪装成成功。详细的 segment
bookkeeping 见 [`15-filecache-file-segment-design.md`](15-filecache-file-segment-design.md)。

### `ConcurrentBoundedQueue`

`FileCache.cpp` 使用 bounded queue 的位置：

```text
parallel metadata load
background free-space remover pipeline
```

Folly `MPMCQueue` 覆盖：

```text
fixed capacity       -> constructor(capacity)
blocking push/pop    -> blockingWrite / blockingRead
nonblocking push/pop -> write / read
timed push/pop       -> tryWriteUntil / tryReadUntil
```

Folly 没有 native `finish`。按 `MPMCQueue` 官方建议使用 sentinel。为避免 sentinel 与真实
值冲突：

```cpp
folly::MPMCQueue<std::optional<T>>
```

协议：

```text
producer writes std::nullopt after all normal work
consumer reads all earlier FIFO items
consumer that reads nullopt:
  re-enqueues nullopt
  exits
```

一个 sentinel 因此可以依次终止未知数量的 consumers，同时保留 drain-before-exit。
异常路径使用 `std::once_flag` 保证只注入一次。

`MPMCQueue` 不接受 capacity 0。metadata load 没有 loading workers 时不构造 queue，
listing worker 直接 load。

work item 必须满足 Folly 的 nothrow move/destruct requirement。已经验证：

```text
pair<filesystem::path, FileCacheOriginInfo>
shared_ptr<EvictionBatch>
optional wrappers of both
```

都满足。

不使用 `boost::sync_bounded_queue`：它会给生产 target 新增 `Boost::thread` 链接依赖，
而 Velox core 当前只普遍使用 `Boost::headers`；同时它没有本路径需要的 timed push。

这个类覆盖读路径 miss 后填充本地 cache segment 所需能力；不实现
`cache_on_write_operations`。

`WriteBufferToFileSegment` 不是读 miss 主路径，也不是“先写 cache 再写远端”的
write-through 组件。当前实际调用点是 `TemporaryDataOnDisk`：它创建
`FileSegmentKind::Ephemeral` segment，把临时数据写到本地 cache segment，后续再从该
segment path 读回。Velox 第一阶段不迁移 `TemporaryDataOnDisk` 写入 `FileCache`
的能力，因此 `WriteBufferToFileSegment` 后置。

### `sipHash128`

`FileCacheKey::fromPath` 会影响 cache 路径和 metadata 兼容性。不能因为 Velox 有
`folly::hash::SpookyHashV2` 就直接替换 hash 算法。当前决策是迁移 ClickHouse
SipHash128 小 helper，保持 cache key/path 兼容。

详细设计见 [`06-filecache-key-hash-design.md`](06-filecache-key-hash-design.md)。

### Metrics / debug / cancellation

这些依赖不是当前主线。第一阶段尽量 no-op，只保留 ClickHouse 调用点。统一放到：

```text
ProfileEvents shim
CurrentMetrics shim
QueryStatus shim
OpenTelemetry shim
FailPoint shim
assertCacheCorrectness shim
```

详细设计见 [`10-filecache-metrics-debug-design.md`](10-filecache-metrics-debug-design.md)。

### `BackgroundSchedulePool`

`FileCache` 不直接依赖 Folly timer/future API。通过 `FileCacheScheduler`
保留 CH 需要的语义：

```text
scheduleAfter(delay)
triggerNow()
cancel()
shutdown()
```

底层使用 `folly::Timekeeper::after` 的 cancelable future 负责 delay，并通过 shared
dynamic `FileCacheWorkerPool` 并行执行 callback。当前设计使用 one-shot 模式：由
`FileCache` task 函数自己决定下一次 `schedule` / `scheduleAfter`。

详细设计见 [`07-filecache-scheduler-design.md`](07-filecache-scheduler-design.md)。

这样后续如果 Velox 侧调度设施变化，不影响 `FileCache` 算法代码。

### `getCallerId`

ClickHouse 用 caller id 判定 downloader ownership。它不是用户身份，也不是永久绑定
某个 file segment 的线程，而是当前下载 lease 的 owner：

```text
getOrSetDownloader
  -> 如果 segment 没有 downloader，把当前 caller id 写入 downloader_id

reserve / write / completePartAndResetDownloader
  -> 只有当前 caller id == downloader_id 才能执行
```

ClickHouse 当前实现把 caller id 写成 `queryId:threadId`。这说明同一个 query
内的并发读线程需要区分，但不代表 segment 永久只能由某个线程处理。downloader
reset/complete 后，另一个线程或 background downloader 可以重新抢到 lease。

Velox 可以取得 `ConnectorQueryCtx::queryId`，并通过 `folly::getOSThreadID` 取得当前
OS thread id。`driverId` 与 `threadId` 不等价：driver 下一次 resume 可以换 executor
thread，而 CH downloader lease 必须在当前同步调用结束前释放。

迁移使用 `FileCacheQueryIdScope` 在同步 `FileCacheInputStream` 入口设置 query id，
`FileSegment::getCallerId` 保持静态无参 API并组合 query id 与当前 TID。background
download 使用自己的 worker TID。这样保持 `FileSegmentsHolder` 的异常清理和 CH
physical-thread execution ownership，不把 lease 扩大到 driver/stream 生命周期。详细设计见
[`08-filecache-caller-token-design.md`](08-filecache-caller-token-design.md)。

### A 类后置项

`ProfileEvents` / `CurrentMetrics` / `OpenTelemetry` / `FailPoint` /
`assertCacheCorrectness*` 不直接依赖 CH 实现。第一版通过 no-op shim 保留接口位置。
