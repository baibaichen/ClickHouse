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
| `BackgroundSchedulePool` | cancelable `folly::Timekeeper` future + shared `FileCacheWorkerPool`；详见 [`FileCacheScheduler`](05-filecache-scheduler-design.md) | wrapper + CH aliases |
| `ThreadFromGlobalPool` / `ThreadPool` | `FileCacheWorker` / `FileCacheThreadPool`；详见[线程池设计](04-filecache-thread-pool-design.md) | wrapper，保留 CH-style join/resize/shutdown |
| `WriteBufferFromFile` / `ReadBufferFromFileBase` | `WriteFile` / `ReadFile` | `WriteBufferFromVeloxWriteFile` / `ReadBufferFromVeloxReadFile` |
| `ConcurrentBoundedQueue` | `folly::MPMCQueue<std::optional<T>>` + sentinel termination | 保留 bounded/blocking/timed/drain语义 |
| `fs::` 文件系统操作 | `std::filesystem` + 必要的 Velox local `FileSystem` API；详见[基础 shims](02-filecache-basic-shims-design.md) | compat shim |
| `sipHash128` | 保留 CH cache key hash语义；详见[key/hash设计](../2-file-cache/03-filecache-key-hash-design.md) | 保留小 helper |
| `absl::flat_hash_map` / `absl::flat_hash_set` | 默认用 `folly::F14FastMap` / `folly::F14FastSet`；需要 value地址稳定时使用 node variant | 按具体 ownership选择 |
| `std::shared_mutex` / CH 锁 | CH-compatible guards；内部使用 `folly::SharedMutex` / `std::mutex`；详见[基础 shims](02-filecache-basic-shims-design.md) | compat shim |
| `LOG_*` / `logger_useful` | CH-compatible macros；内部使用 `LOG` / `VLOG`；详见[基础 shims](02-filecache-basic-shims-design.md) | compat shim |
| `getThreadId` / `getCallerId` | `FileCacheQueryIdScope` + `folly::getOSThreadID`；详见[caller identity](06-filecache-caller-token-design.md) | 保留 CH execution ownership |
| `ProfileEvents` / `CurrentMetrics` | no-op shim；后续可接 Velox stats；详见[metrics/debug设计](03-filecache-metrics-debug-design.md) | using/compat shim |
| `OpenTelemetry` | no-op `OpenTelemetry::SpanHolder`；详见[metrics/debug设计](03-filecache-metrics-debug-design.md) | compat shim |
| `QueryStatus::throwIfKilled` | no-op shim；后续接 `ConnectorQueryCtx::cancellationToken`；详见[metrics/debug设计](03-filecache-metrics-debug-design.md) | compat shim |
| `FailPoint` / `assertCacheCorrectness*` | no-op shims；详见[metrics/debug设计](03-filecache-metrics-debug-design.md) | using/compat shim |
