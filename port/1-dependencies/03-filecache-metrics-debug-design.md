# 03. Metrics / tracing / debug no-op shim设计

## 结论

metrics、tracing、failpoint、correctness checker、event log 都不是当前迁移主线。
第一阶段尽量全部 no-op，只保留 ClickHouse 调用点和名字，避免迁移 `FileCache` 算法时
被观测系统阻塞。

做法：

```text
真实实现用 Velox / FileCache 风格的小类
ClickHouse 名字通过 using 或兼容函数保留
默认行为全部 no-op
后续需要观测能力时再接 RuntimeMetric / IoStats / StatsReporter / TestValue / trace
```

唯一例外：`QueryStatus::throwIfKilled` 属于等待路径的取消语义。第一阶段如果能从
`ConnectorQueryCtx::cancellationToken` 传入，就实现；如果接入点还没准备好，也先用
no-op shim 保留调用点，但文档中明确这是后续必须补的行为，不把它当作已完成能力。

## 映射

| ClickHouse | Velox port first phase |
|---|---|
| `ProfileEvents` | no-op `ProfileEvents` shim |
| `ProfileEventTimeIncrement` | no-op RAII timer shim |
| `CurrentMetrics` | no-op `CurrentMetrics` shim |
| `FilesystemCacheLog` | no-op `FilesystemCacheLog` shim |
| `OpenTelemetry::SpanHolder` | no-op `SpanHolder` shim |
| `FailPoint` | no-op `FailPoint` shim |
| `assertCacheCorrectness*` | no-op correctness checker shim |
| `QueryStatus::throwIfKilled` | no-op first; later maps to Velox cancellation token |

## 文件位置

建议集中到：

```text
velox/ch/Common/ProfileEvents.h
velox/ch/Common/CurrentMetrics.h
velox/ch/Common/OpenTelemetryTraceContext.h
velox/ch/Common/FailPoint.h
velox/ch/Common/FilesystemCacheLog.h
velox/ch/Common/QueryStatus.h
```

算法迁移阶段优先让 CH 风格调用点少改。后续如果需要接 Velox-native 观测实现，
也应藏在这些 CH-compatible shim 背后。

## no-op shim 接口

### `ProfileEvents`

保留 CH 调用方式：

```cpp
namespace ProfileEvents
{
    enum Event
    {
        FilesystemCacheGetOrSetMicroseconds,
        FilesystemCacheGetMicroseconds,
        FilesystemCacheReserveAttempts,
        FilesystemCacheFailedReserveAttempts,
        FilesystemCacheReserveMicroseconds,
        CachedReadBufferReadFromCacheBytes,
        CachedReadBufferReadFromSourceBytes,
        CachedReadBufferCacheWriteBytes,
        FileSegmentWaitMicroseconds,
        FileSegmentWriteMicroseconds,
        FileSegmentCompleteMicroseconds,
        FilesystemCacheCheckCorrectness,
        FilesystemCacheCheckCorrectnessMicroseconds,
    };

    inline void increment(Event, uint64_t = 1) {}
}

enum Time
{
    Nanoseconds,
    Microseconds,
    Milliseconds,
    Seconds,
};

template <Time unit>
class ProfileEventTimeIncrement
{
public:
    explicit ProfileEventTimeIncrement(ProfileEvents::Event) {}
};
```

第一版只声明已迁移代码实际用到的 enum。`Time` 和 non-type template parameter必须保留，
因为 `Guards.h` 使用 `ProfileEventTimeIncrement<Microseconds>`；写成
`template <typename Unit>` 不能编译。迁移过程中遇到新 event，再补 enum，不做全量搬运。

### `CurrentMetrics`

保留 CH 调用方式：

```cpp
namespace CurrentMetrics
{
    enum Metric
    {
        CacheFileSegments,
        FilesystemCacheHoldFileSegments,
        FilesystemCacheDownloadQueueElements,
        FilesystemCacheDelayedCleanupElements,
        FilesystemCacheReserveThreads,
        FilesystemCacheSizeLimit,
    };

    inline void add(Metric, int64_t = 1) {}
    inline void sub(Metric, int64_t = 1) {}

    class Increment
    {
    public:
        explicit Increment(Metric, int64_t = 1) {}
    };
}
```

`Increment` 是 no-op RAII guard，保留异常安全结构但不记录指标。

### `OpenTelemetry::SpanHolder`

保留 CH 调用方式：

```cpp
namespace OpenTelemetry
{
    class SpanHolder
    {
    public:
        explicit SpanHolder(std::string_view) {}

        template <typename T>
        void addAttribute(std::string_view, const T &) {}
    };
}
```

### `FailPoint`

第一版 no-op。只保留必要宏/函数入口：

```cpp
#define FAIL_POINT_TRIGGER(...) do {} while (false)
```

如果迁移代码实际使用其他 failpoint API，再按调用点补最小 no-op。

### `FilesystemCacheLog`

第一版 no-op：

```cpp
class FilesystemCacheLog
{
public:
    template <typename T>
    void add(T &&) {}
};
```

如果 reader 需要构造 log element，则保留最小 `FilesystemCacheLogElement`：

```cpp
struct FilesystemCacheLogElement
{
    enum class CacheType
    {
        READ_FROM_CACHE,
        READ_FROM_FS_BYPASSING_CACHE,
        READ_FROM_FS_AND_DOWNLOADED_TO_CACHE,
    };

    CacheType cache_type;
};
```

不实现 user-visible filesystem cache log table。

### `QueryStatus::throwIfKilled`

第一版提供 no-op `QueryStatus` shim：

```cpp
class QueryStatus
{
public:
    void throwIfKilled() const {}
};

using QueryStatusPtr = std::shared_ptr<QueryStatus>;
```

`FileSegment::wait` 中保留调用点：

```cpp
if (query_status)
    query_status->throwIfKilled();
```

后续接入使用方时再把它接到：

```text
ConnectorQueryCtx::cancellationToken()
```

并实现：

```text
if token.isCancellationRequested():
    throw cancellation exception
```

这个是后续必须补的语义，不应该长期保持 no-op。

### `assertCacheCorrectness*`

第一版 no-op：

```cpp
inline void assertCacheCorrectness() {}
```

不要在第一阶段迁移完整检查逻辑。完整检查需要理解 `FileCache` 中心 SCC 后再做。

## 为什么第一阶段 no-op

这些依赖不是主线：

```text
metrics: 观测，不影响算法正确性
tracing: 观测，不影响算法正确性
failpoint: 测试注入，不影响生产路径
filesystem cache log: 诊断，不影响读写状态机
assertCacheCorrectness: debug check，不替代正常状态转换测试
```

如果第一阶段试图全部接到 Velox runtime stats / tracing / test hooks，会扩大迁移面，
并且容易让 `FileCache` 算法迁移卡在非核心设施上。

## 后续接入顺序

后续按需要逐步替换 no-op：

```text
1. QueryStatus shim -> ConnectorQueryCtx::cancellationToken
2. ProfileEvents / CurrentMetrics shim -> atomic counters
3. metrics shim -> IoStats / RuntimeMetric
4. event log -> runtime stats 或 debug endpoint
5. FailPoint -> Velox TestValue
6. OpenTelemetry -> Velox trace context
7. assertCacheCorrectness -> debug/full checker
```

其中 cancellation 优先级最高，因为它影响等待路径可取消性。

## 测试要求

第一阶段只测 shim 行为：

```text
ProfileEvents::increment compiles and is no-op
ProfileEventTimeIncrement can be constructed/destructed
CurrentMetrics::add/sub compile and are no-op
CurrentMetrics::Increment can be constructed/destructed
OpenTelemetry::SpanHolder can be constructed and addAttribute compiles
FilesystemCacheLog::add accepts log element and is no-op
QueryStatus::throwIfKilled is no-op
assertCacheCorrectness shim compiles and is no-op
```

后续接入 metrics/cancellation 时再补行为测试。

## Review 状态

本文档已完成 review。关键决策：

```text
metrics/debug/tracing/event-log/correctness 第一阶段全部 no-op
类名/调用点尽量用 using 或兼容 shim 对齐 ClickHouse
cancellation 先保留 no-op 调用点，后续接 ConnectorQueryCtx::cancellationToken
```
