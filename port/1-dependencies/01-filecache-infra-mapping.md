# 01. `FileCache` 底层设施替换矩阵

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

| CH | Velox | 处理方式 |
|---|---|---|
| `String` / `UInt64` / `Int64` 等 CH 基础别名 | `velox/ch/Common/ClickHouseAliases.h` 中提供 `using String = std::string` 等；`UInt8` 用 `uint8_t`，不照搬 CH 的 `char8_t` | compat alias |
| `chassert` | `velox/ch/Common/ClickHouseAssert.h` | compat macro：Debug/sanitizer 时记录并 abort；普通 Release 时 expression 和 diagnostic message 都不求值 |
| `DB::Exception(ErrorCodes::BAD_ARGUMENTS, ...)` | `VELOX_USER_FAIL` | 直接映射为 `VeloxUserError` / `kInvalidArgument`，不经过统一 exception helper |
| `DB::Exception(ErrorCodes::LOGICAL_ERROR, ...)` | `VELOX_FAIL` | 直接映射为 `VeloxRuntimeError` / `kInvalidState`；`throwFileCacheException` 只允许作为 runtime convenience helper |
| `ErrnoException` / `fs::filesystem_error` | Velox exception + errno/path/message text | 遵循 Velox local filesystem 风格，不增加结构化 errno exception；`WriteFile` partial-write 处理见 Task 007 |
| `BackgroundSchedulePool` | cancelable `folly::Timekeeper` future + shared `FileCacheWorkerPool`；详见 [`FileCacheScheduler`](05-filecache-scheduler-design.md) | wrapper + CH aliases |
| `ThreadFromGlobalPool` / `ThreadPool` | `FileCacheWorker` / `FileCacheThreadPool`；详见[线程池设计](04-filecache-thread-pool-design.md) | wrapper，保留 CH-style join/resize/shutdown |
| `WriteBufferFromFile` / `ReadBufferFromFileBase` | `WriteFile` / `ReadFile` | `WriteBufferFromVeloxWriteFile` / `ReadBufferFromVeloxReadFile` |
| `ConcurrentBoundedQueue` | CH-compatible `FileCacheBoundedQueue<T>` | mutex + condition variables；保留 blocking `push`/`pop`、timed/non-blocking `tryPush`/`tryPop`、capacity 0、`finish` wakeup/drain 和 CH move-or-copy exception safety |
| `fs::` 文件系统操作 | `std::filesystem` + 必要的 Velox local `FileSystem` API；详见[基础 shims](02-filecache-basic-shims-design.md) | compat shim |
| `sipHash128` | 保留 CH cache key hash语义；详见[key/hash设计](../2-file-cache/03-filecache-key-hash-design.md) | 保留小 helper |
| `absl::flat_hash_map` / `absl::flat_hash_set` | 默认用 `folly::F14FastMap` / `folly::F14FastSet`；需要 value地址稳定时使用 node variant | 按具体 ownership选择 |
| `std::shared_mutex` / CH 锁 | CH-compatible guards；内部使用 `folly::SharedMutex` / `std::mutex`；详见[基础 shims](02-filecache-basic-shims-design.md) | compat shim |
| `LOG_*` / `logger_useful` | 第一阶段 no-op macros + non-null name-only `LoggerPtr`；真实 logging 和 current-exception formatting 后置到 Task 017；详见[基础 shims](02-filecache-basic-shims-design.md) | compat shim |
| `getThreadId` / `getCallerId` | `FileCacheQueryIdScope` + `folly::getOSThreadID`；详见[caller identity](06-filecache-caller-token-design.md) | 保留 CH execution ownership |
| `ProfileEvents` / `CurrentMetrics` | no-op shim；后续可接 Velox stats；详见[metrics/debug设计](03-filecache-metrics-debug-design.md) | using/compat shim |
| `OpenTelemetry` | no-op `OpenTelemetry::SpanHolder`；详见[metrics/debug设计](03-filecache-metrics-debug-design.md) | compat shim |
| `QueryStatus::throwIfKilled` | no-op shim；后续接 `ConnectorQueryCtx::cancellationToken`；详见[metrics/debug设计](03-filecache-metrics-debug-design.md) | compat shim |
| `FailPoint` / `assertCacheCorrectness*` | no-op shims；详见[metrics/debug设计](03-filecache-metrics-debug-design.md) | using/compat shim |

## IO compatibility contract

完整场景、ownership 图和 CH 源码映射见
[reader handoff 与 IO compatibility 设计](../design/filecache-reader-handoff-and-contract-recovery.html)。

这里的 “wrapper” 只允许替换内部实现，不允许重新定义 reader/writer 行为：

```text
CH FileCache state machine
  -> ch::ReadBufferFromFileBase compatibility contract
       -> ReadBufferFromVeloxReadFile
            -> shared_ptr<ReadFile> + MemoryPool

FileSegment::write
  -> WriteBufferFromVeloxWriteFile
       -> WriteFile
```

`ReadBufferFromFileBase` compatibility contract 必须保留：

```text
internal buffer / working buffer / mutable position
offset / available / hasPendingData / count
set(ptr, size) / set(nullptr, 0)
next / eof / cancel
seek / getPosition / getFileOffsetOfBufferEnd
setReadUntilPosition / setReadUntilEnd
supportsExternalBufferMode / supportsRightBoundedReads
getFileName / tryGetFileSize
```

`next`、`eof` 和异常状态转换以 CH `ReadBuffer` 为准：

```text
next:
  require no pending data
  settle bytes += offset
  call nextImpl
  on exception: cancel, then rethrow
  on false: publish an empty working buffer
  on true: reset position to working-buffer begin

eof:
  return !hasPendingData && !next
```

External buffer 在显式 `set` 或 null/zero detach 前保持有效；成功读取不能自动把下一次
read target 切回 owned buffer。handoff 前必须满足：

```text
reader.available() == 0
reader.getFileOffsetOfBufferEnd() == FileSegment::getCurrentWriteOffset()
reader no longer references the caller-owned output buffer
```

Owned IO memory 必须由 Velox `MemoryPool` 分配。构造 reader 时调用
`ReadFile::directIo` 获取 alignment；direct IO 模式下 owned buffer 必须满足 alignment，
caller-provided external buffer 不满足时必须显式拒绝，不允许静默 fallback。

`WriteBufferFromVeloxWriteFile` 必须支持 CH `FileSegment::write` 使用的零复制 attach/flush：

```text
construct with buffer size 0
set(from, size, offset=size)
next                         # append offset bytes
set(nullptr, 0)              # detach
finalize                     # idempotent
cancel                       # noexcept and idempotent
```

Writer 同时提供 `sync` 和 `getFileName`。不得用强制 memcpy 到 writer-owned buffer 的
实现替代这条 hot path，除非后续设计明确批准该行为和性能变化。

Reader/writer 可以 composition 复用一个内部 `FileCacheBufferState`：

```text
optional BufferPtr ownedBuffer
working begin/end
mutable position
settled bytes
set / offset / available / count
swapWorkingState
```

Owned memory 必须是 `BufferPtr` / `AlignedBuffer` 并由 `MemoryPool` 计费；raw pointer
只表示 caller-owned memory 的同步 non-owning view。

Writer 的 Velox mapping 不得混淆普通 write 与 durability：

```text
nextImpl  -> WriteFile::append
sync      -> next + WriteFile::flush
finalize  -> next + WriteFile::close
cancel    -> no append/flush; discard pending state and release the owned WriteFile
```

`FileSegment::DownloadState` 持有 shared writer wrapper；wrapper 独占一个
`unique_ptr<WriteFile>`。当前 remote-miss 路径和后置 `WriteBufferToFileSegment`
都借用同一块 caller-owned `BufferPtr` 完成 append，不经过 writer staging copy。
这里的 zero-copy 只承诺没有用户态中间 memcpy，不承诺 kernel zero-copy。
