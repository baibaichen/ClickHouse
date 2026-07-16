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
| `BackgroundSchedulePool`（定时 + 提前触发任务） | `folly::FunctionScheduler`，通过 `addFunction` 定时，通过 `resetFunctionTimer` 提前触发 | wrapper：封成 `FileCacheScheduler` | 需要 review |
| `ThreadFromGlobalPool` / 线程池 | `folly::Executor` / `CPUThreadPoolExecutor` / `IOThreadPoolExecutor` | 直接替换，构造时注入 executor | 需要 review |
| `WriteBufferFromFile` / `ReadBufferFromFileBase` | `WriteFile` / `ReadFile` | wrapper：`WriteBufferFromVeloxWriteFile` / `ReadBufferFromVeloxReadFile` | 已 review |
| `fs::` 文件系统操作 | `std::filesystem` 处理目录和 exists/remove；本地 IO 通过 `LocalReadFile` / `LocalWriteFile` | 直接替换 + wrapper | 需要 review |
| `sipHash128` | 不直接换成 `SpookyHashV2`；需要保留 CH cache key hash 语义 | 保留小 helper | 需要 review |
| `std::shared_mutex` / CH 锁 | `folly::SharedMutex` 用于读写锁；`std::mutex` 用于普通状态锁 | 直接替换或薄 typedef | 需要 review |
| `LOG_*` / `logger_useful` | `LOG` / `VLOG` / `FB_LOG_EVERY_MS` | 直接替换 | 需要 review |
| `getThreadId` / `getCallerId` | `FileCacheRequestContext` + 必要时 `thread_local` caller token | wrapper：不要依赖 Velox 全局线程状态 | 需要 review |
| `ProfileEvents` / `CurrentMetrics` | `FileCacheMetrics` 本地 counters；后续接 `RuntimeMetric` / `IoStats` / `StatsReporter` | wrapper；第一版可 no-op/atomic | 需要 review |
| `OpenTelemetry` | 暂不接；后续如需要再接 Velox tracing/`TraceContext` | 剥离 / 后置 | 需要 review |
| `QueryStatus::throwIfKilled` | 暂不接；如需要取消语义，从 Velox query/task context 显式传入 cancellation hook | 剥离 / 后置 | 需要 review |
| `FailPoint` | 暂不接；测试阶段用 Velox test hook 或注入式 failure callback | 剥离 / 后置 | 需要 review |
| `assertCacheCorrectness*` | 保留接口和 debug/sanitizer 开关；第一版可 no-op，后续迁移完整检查 | 剥离 / 后置 | 需要 review |

## 重点说明

### `sipHash128`

`FileCacheKey::fromPath` 会影响 cache 路径和 metadata 兼容性。不能因为 Velox 有
`folly::hash::SpookyHashV2` 就直接替换 hash 算法。若目标是兼容 ClickHouse cache
layout，应迁移 SipHash128 小 helper；如果明确接受 cache key 不兼容，才可以换 hash。

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

这样后续如果 Velox 侧调度设施变化，不影响 `FileCache` 算法代码。

### `getCallerId`

ClickHouse 用 caller id 判定 downloader 所有权。Velox 里不要依赖不明确的全局线程
状态。建议：

```text
FileCacheRequestContext.queryId
thread_local monotonically increasing token
```

组合成 caller token，用于 `FileSegment::getOrSetDownloader` / `isDownloader`。

### A 类剥离项

下面这些当前不进入主迁移路径：

```text
OpenTelemetry
QueryStatus::throwIfKilled
FailPoint
assertCacheCorrectness*
```

它们不是没价值，而是会扩大第一阶段迁移面。保留接口位置，后续按测试/观测需求补。
