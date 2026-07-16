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
| `BackgroundSchedulePool`（定时 + 提前触发任务） | `folly::FunctionScheduler`，通过 one-shot `addFunctionOnce` + `cancelFunctionAndWait` 封装 `schedule` / `scheduleAfter` / `deactivate`，详见 `07-filecache-scheduler-design.md` | wrapper：封成 `FileCacheScheduler` | 已 review |
| `ThreadFromGlobalPool` / 线程池 | `folly::Executor` / `CPUThreadPoolExecutor` / `IOThreadPoolExecutor` | 直接替换，构造时注入 executor | 需要 review |
| `WriteBufferFromFile` / `ReadBufferFromFileBase` | `WriteFile` / `ReadFile` | wrapper：`WriteBufferFromVeloxWriteFile` / `ReadBufferFromVeloxReadFile` | 已 review |
| `fs::` 文件系统操作 | `std::filesystem` 处理目录和 exists/remove；本地 IO 通过 `LocalReadFile` / `LocalWriteFile` | 直接替换 + wrapper | 需要 review |
| `sipHash128` | 不直接换成 `SpookyHashV2`；需要保留 CH cache key hash 语义，详见 `06-filecache-key-hash-design.md` | 保留小 helper | 已 review |
| `std::shared_mutex` / CH 锁 | `folly::SharedMutex` 用于读写锁；`std::mutex` 用于普通状态锁 | 直接替换或薄 typedef | 需要 review |
| `LOG_*` / `logger_useful` | `LOG` / `VLOG` / `FB_LOG_EVERY_MS` | 直接替换 | 需要 review |
| `getThreadId` / `getCallerId` | `FileCacheCallerToken`，由 `ConnectorQueryCtx::queryId`、`scanId` / `driverId` 和 `FileCacheInputStream` 本地 token 组成，详见 `08-filecache-caller-token-design.md` | wrapper：显式传递 downloader ownership | 已 review |
| `ProfileEvents` / `CurrentMetrics` | `FileCacheMetrics` 本地 counters；后续接 `RuntimeMetric` / `IoStats` / `StatsReporter` | wrapper；第一版可 no-op/atomic | 需要 review |
| `OpenTelemetry` | 暂不接；后续如需要再接 Velox tracing/`TraceContext` | 剥离 / 后置 | 需要 review |
| `QueryStatus::throwIfKilled` | 暂不接；如需要取消语义，从 Velox query/task context 显式传入 cancellation hook | 剥离 / 后置 | 需要 review |
| `FailPoint` | 暂不接；测试阶段用 Velox test hook 或注入式 failure callback | 剥离 / 后置 | 需要 review |
| `assertCacheCorrectness*` | 保留接口和 debug/sanitizer 开关；第一版可 no-op，后续迁移完整检查 | 剥离 / 后置 | 需要 review |

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
    std::unique_ptr<WriteBufferFromVeloxWriteFile> cacheWriter;
};
```

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

这个类只覆盖读路径 miss 后填充本地 cache segment，以及临时数据写入
`WriteBufferToFileSegment` 所需能力；不实现 `cache_on_write_operations`。

### `sipHash128`

`FileCacheKey::fromPath` 会影响 cache 路径和 metadata 兼容性。不能因为 Velox 有
`folly::hash::SpookyHashV2` 就直接替换 hash 算法。当前决策是迁移 ClickHouse
SipHash128 小 helper，保持 cache key/path 兼容。

详细设计见 [`06-filecache-key-hash-design.md`](06-filecache-key-hash-design.md)。

### `ProfileEvents` / `CurrentMetrics`

不要在算法代码里散落 Velox 指标 API。应集中到：

```text
FileCacheMetrics
```

第一版可以是：

```text
atomic counters / no-op timers
```

后续再映射到：

```text
RuntimeMetric
IoStats
StatsReporter
```

### `BackgroundSchedulePool`

`FileCache` 不直接依赖 `folly::FunctionScheduler`。通过 `FileCacheScheduler`
保留 CH 需要的语义：

```text
scheduleAfter(delay)
triggerNow()
cancel()
shutdown()
```

已确认 `folly::FunctionScheduler` 提供 `addFunction`、`addFunctionOnce`、
`cancelFunction`、`cancelFunctionAndWait`、`resetFunctionTimer`。当前设计选择 one-shot
模式：由 `FileCache` task 函数自己决定下一次 `schedule` / `scheduleAfter`，更贴近
ClickHouse 当前逻辑。

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

Velox 可以取得 `ConnectorQueryCtx::queryId`，也可以在调用点读取当前 OS thread id。
但是 Velox `driverId` 与 OS `threadId` 不是等价物：同一 `driverId` 同一时刻只会
on-thread 在一个线程上执行，但下一次 resume 可能换到另一个 executor thread。

因此迁移设计不声称 `driverId == threadId`。建议引入显式的 `FileCacheCallerToken`：

```text
queryId
scanId / driverId
file path + split range
FileCacheInputStream-local sequence
```

它表达的是“当前读流 / 下载 continuation 拥有这个 segment 的填充权”，用于
`FileSegment::getOrSetDownloader` / `isDownloader` / `write` / `complete`。详细设计见
[`08-filecache-caller-token-design.md`](08-filecache-caller-token-design.md)。

### A 类剥离项

下面这些当前不进入主迁移路径：

```text
OpenTelemetry
QueryStatus::throwIfKilled
FailPoint
assertCacheCorrectness*
```

它们不是没价值，而是会扩大第一阶段迁移面。保留接口位置，后续按测试/观测需求补。
