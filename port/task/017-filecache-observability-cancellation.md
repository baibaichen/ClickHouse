# Task 017: `FileCache` Observability and Cancellation Hardening

> **Planned mainline hardening task; design revision pending.**
>
> **Prerequisite:** Tasks 003–015 must be complete. Task 016 is deferred and is
> not a code prerequisite. The shims in
> `velox/ch/Common/` (logger_useful.h, CurrentMetrics.h, ProfileEvents.h,
> QueryStatus.h) must be present as no-op stubs from the earlier port tasks.
> `FileCacheInputStream` must be implemented from task 014.
>
> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes
> one result file under this ClickHouse checkout. Do not modify ClickHouse
> source files. Do not commit or stage either repository.

## Status and user disposition

```text
environment_profile: root-oss
disposition: planned
task_017_allowed: false
reason: user wants this capability, but the reviewed contract must be redesigned first
```

The user explicitly selected observability and cancellation as work that should
be completed. This records priority only; it does not approve the current
contract or authorize implementation. Redesign is a separate step.

## Goal

Replace the selected no-op shims in `velox/ch/Common/` with lightweight
real implementations behind the **same public APIs**, and connect
`ConnectorQueryCtx::cancellationToken()` to `QueryStatus::throwIfKilled()`
in `FileCacheInputStream::Next` without allowing the downloader lease to be
held across a cancellation check point.

Specific deliverables:

1. **`logger_useful.h`** — preserve the non-null name-only logger API from
   corrected Task 003; replace no-op macros with lazy VLOG wrappers; implement
   non-empty current-exception formatting; and make
   `tryLogCurrentException` log that text via `LOG(WARNING)`.

2. **`CurrentMetrics.h`** — replace `add/sub/Increment` no-ops with
   `std::atomic<int64_t>` per metric; reset on process exit is acceptable.

3. **`ProfileEvents.h`** — replace `increment` no-ops with
   `std::atomic<uint64_t>` per event; `ProfileEventTimeIncrement` records
   elapsed microseconds on destruction; reset on process exit is acceptable.

4. **`QueryStatus.h`** — replace the stub with a `folly::CancellationToken`
   wrapper; `throwIfKilled()` calls `VELOX_FAIL` when
   `token_.isCancellationRequested()`.

5. **`FileCacheInputStream`** — connect `QueryStatus` at safe cancellation
   points (see Step 7 for exact rules); the downloader lease must not be
   held when `throwIfKilled()` is called.

Deliverable: `velox_ch_observability_test` and
`velox_ch_cancellation_test` pass all scenarios.

## Starting point

```text
Velox repository:    <velox_repo>
Required branch:     filecache
Expected HEAD:       descendant of the task-016 result commit
```

Stop if the branch is not `filecache`.

Verify the four shims exist and are no-ops:

```bash
grep -c "noreturn\|inline.*{}" \
  <velox_repo>/velox/ch/Common/QueryStatus.h \
  <velox_repo>/velox/ch/Common/CurrentMetrics.h \
  <velox_repo>/velox/ch/Common/ProfileEvents.h \
  <velox_repo>/velox/ch/Common/logger_useful.h
```

Record the output in the result file. Each file must show at least one
inline empty body `{}` indicating the current no-op stubs.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/1-dependencies/02-filecache-basic-shims-design.md
<clickhouse_repo>/port/1-dependencies/03-filecache-metrics-debug-design.md
<clickhouse_repo>/port/1-dependencies/06-filecache-caller-token-design.md
<clickhouse_repo>/port/3-consumers/03-filecache-buffered-input-design.md
  (section "FileCacheInputStream::Next 的主调用关系")
<clickhouse_repo>/port/task/result/014-filecache-buffered-input-result.md
<clickhouse_repo>/port/task/result/015-filecache-velox-e2e-result.md
```

Reference ClickHouse implementations for behavioral semantics only — do not
copy CH-specific infrastructure:

```text
<clickhouse_repo>/src/Common/logger_useful.h
<clickhouse_repo>/src/Common/CurrentMetrics.h
<clickhouse_repo>/src/Common/ProfileEvents.h
<clickhouse_repo>/src/Interpreters/FileCache/EvictionCandidates.cpp
<clickhouse_repo>/src/Interpreters/FileCache/SLRUFileCachePriority.cpp
```

## File scope

Modify in the Velox checkout:

```text
<velox_repo>/velox/ch/Common/logger_useful.h
<velox_repo>/velox/ch/Common/CurrentMetrics.h
<velox_repo>/velox/ch/Common/ProfileEvents.h
<velox_repo>/velox/ch/Common/QueryStatus.h
<velox_repo>/velox/ch/Disks/IO/FileCacheInputStream.cpp
```

Modify in the Velox checkout (add tests subdirectory if absent):

```text
<velox_repo>/velox/ch/Common/CMakeLists.txt
<velox_repo>/velox/ch/Common/tests/CMakeLists.txt
<velox_repo>/velox/ch/Disks/IO/tests/CMakeLists.txt
```

Create in the Velox checkout:

```text
<velox_repo>/velox/ch/Common/tests/ObservabilityTest.cpp
<velox_repo>/velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp
```

Create in the ClickHouse checkout:

```text
<clickhouse_repo>/port/task/result/017-filecache-observability-cancellation-result.md
```

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Record the HEAD and any pre-existing dirty files in the result file.

- [ ] **Step 2: Add the observability tests as `GTEST_SKIP()` stubs**

Add to `velox/ch/Common/tests/CMakeLists.txt` (create if absent; the file
already exists from task 003):

```cmake
add_executable(velox_ch_observability_test ObservabilityTest.cpp)
add_test(velox_ch_observability_test velox_ch_observability_test)

target_link_libraries(
  velox_ch_observability_test
  PRIVATE
    velox_ch_filecache
    velox_test_util
    velox_exception
    Folly::folly
    GTest::gtest
    GTest::gtest_main
)
```

Create `velox/ch/Common/tests/ObservabilityTest.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

#include "velox/ch/Common/CurrentMetrics.h"
#include "velox/ch/Common/ProfileEvents.h"
#include "velox/ch/Common/QueryStatus.h"
#include "velox/ch/Common/logger_useful.h"
#include "velox/common/base/Exceptions.h"

#include <gtest/gtest.h>
#include <folly/CancellationToken.h>

namespace facebook::velox::ch
{
namespace
{

TEST(CurrentMetricsTest, AddAndReadBack) {
    GTEST_SKIP() << "implement";
}

TEST(CurrentMetricsTest, IncrementScopedDecrementsOnDestruct) {
    GTEST_SKIP() << "implement";
}

TEST(CurrentMetricsTest, SubDecrements) {
    GTEST_SKIP() << "implement";
}

TEST(ProfileEventsTest, IncrementAndReadBack) {
    GTEST_SKIP() << "implement";
}

TEST(ProfileEventsTest, TimeIncrementRecordsElapsed) {
    GTEST_SKIP() << "implement";
}

TEST(QueryStatusTest, NoopWhenNotCancelled) {
    GTEST_SKIP() << "implement";
}

TEST(QueryStatusTest, ThrowsWhenTokenCancelled) {
    GTEST_SKIP() << "implement";
}

TEST(QueryStatusTest, DefaultConstructedIsNeverCancelled) {
    GTEST_SKIP() << "implement";
}

TEST(LoggerUsefulTest, GetLoggerReturnsNonNullPtr) {
    GTEST_SKIP() << "implement";
}

TEST(LoggerUsefulTest, CurrentExceptionMessageIsEmptyOutsideCatch) {
    GTEST_SKIP() << "implement";
}

TEST(LoggerUsefulTest, CurrentExceptionMessageFormatsStdException) {
    GTEST_SKIP() << "implement";
}

TEST(LoggerUsefulTest, CurrentExceptionMessageFormatsVeloxException) {
    GTEST_SKIP() << "implement";
}

TEST(LoggerUsefulTest, TryLogCurrentExceptionNoThrow) {
    GTEST_SKIP() << "implement";
}

}
}
```

Add to `velox/ch/Disks/IO/tests/CMakeLists.txt`:

```cmake
add_executable(velox_ch_cancellation_test FileCacheCancellationTest.cpp)
add_test(velox_ch_cancellation_test velox_ch_cancellation_test)

target_link_libraries(
  velox_ch_cancellation_test
  PRIVATE
    velox_ch_filecache
    velox_test_util
    velox_exception
    Folly::folly
    GTest::gtest
    GTest::gtest_main
)
```

Create `velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

#include "velox/ch/Disks/IO/FileCacheInputStream.h"
#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Common/QueryStatus.h"
#include "velox/common/testutil/TempDirectoryPath.h"

#include <gtest/gtest.h>
#include <folly/CancellationToken.h>

namespace facebook::velox::ch
{
namespace
{

TEST(FileCacheCancellationTest, NextThrowsWhenCancelledBeforeRead) {
    GTEST_SKIP() << "implement";
}

TEST(FileCacheCancellationTest, NextThrowsWhenCancelledAfterFirstSegment) {
    GTEST_SKIP() << "implement";
}

TEST(FileCacheCancellationTest, DownloaderNotHeldWhenCancelledDuringWait) {
    GTEST_SKIP() << "implement";
}

TEST(FileCacheCancellationTest, NoCancellationTokenNeverCancels) {
    GTEST_SKIP() << "implement";
}

TEST(FileCacheCancellationTest, CancellationDoesNotLeakDownloaderLease) {
    GTEST_SKIP() << "implement";
}

}
}
```

Build the skeleton (must compile):

```bash
# Follow the selected profile's configure recipe from ENVIRONMENT.md.
# For root-oss: source <velox_env> first. For home-chang: add -DVELOX_BUILD_TESTING=ON
# (already included in the root-oss effective configuration).
# Redirect to <velox_build_dir>/configure_017.log.

<ninja> \
  -C <velox_build_dir> \
  velox_ch_observability_test \
  velox_ch_cancellation_test \
  > <velox_build_dir>/build_017_skeleton.log 2>&1
echo "exit: $?"
```

Expected: build succeeds (skeleton only, all cases skip).

- [ ] **Step 3: Replace `CurrentMetrics.h` with atomic counters**

Replace the body of `velox/ch/Common/CurrentMetrics.h` with:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace facebook::velox::ch
{

namespace CurrentMetrics
{

enum Metric : int
{
    CacheFileSegments,
    FilesystemCacheHoldFileSegments,
    FilesystemCacheDownloadQueueElements,
    FilesystemCacheDelayedCleanupElements,
    FilesystemCacheReserveThreads,
    FilesystemCacheSizeLimit,
    kMetricCount
};

namespace detail
{
extern std::atomic<int64_t> values[static_cast<int>(kMetricCount)];
} // namespace detail

inline int64_t get(Metric m)
{
    return detail::values[static_cast<int>(m)].load(std::memory_order_relaxed);
}

inline void add(Metric m, int64_t delta = 1)
{
    detail::values[static_cast<int>(m)].fetch_add(
        delta, std::memory_order_relaxed);
}

inline void sub(Metric m, int64_t delta = 1)
{
    detail::values[static_cast<int>(m)].fetch_sub(
        delta, std::memory_order_relaxed);
}

class Increment
{
public:
    explicit Increment(Metric m, int64_t delta = 1)
        : metric_(m), delta_(delta)
    {
        add(metric_, delta_);
    }

    ~Increment()
    {
        sub(metric_, delta_);
    }

    Increment(const Increment &) = delete;
    Increment & operator=(const Increment &) = delete;

private:
    Metric metric_;
    int64_t delta_;
};

} // namespace CurrentMetrics

} // namespace facebook::velox::ch
```

Add a `CurrentMetrics.cpp` to provide the storage array:

```cpp
// velox/ch/Common/CurrentMetrics.cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * Licensed under the Apache License, Version 2.0 ...
 */

#include "velox/ch/Common/CurrentMetrics.h"

namespace facebook::velox::ch::CurrentMetrics::detail
{
std::atomic<int64_t> values[static_cast<int>(kMetricCount)]{};
} // namespace
```

Task 004 converted `velox_ch_filecache` to a compiled library. Add both metric
storage sources to that existing target:

```cmake
target_sources(
  velox_ch_filecache
  PRIVATE
    CurrentMetrics.cpp
    ProfileEvents.cpp
)
```

- [ ] **Step 4: Replace `ProfileEvents.h` with atomic event counters**

Replace the body of `velox/ch/Common/ProfileEvents.h` with:

```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace facebook::velox::ch
{

namespace ProfileEvents
{

enum Event : int
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
    kEventCount
};

namespace detail
{
extern std::atomic<uint64_t> values[static_cast<int>(kEventCount)];
} // namespace detail

inline uint64_t get(Event e)
{
    return detail::values[static_cast<int>(e)].load(std::memory_order_relaxed);
}

inline void increment(Event e, uint64_t delta = 1)
{
    detail::values[static_cast<int>(e)].fetch_add(
        delta, std::memory_order_relaxed);
}

} // namespace ProfileEvents

template <typename Unit>
class ProfileEventTimeIncrement
{
public:
    explicit ProfileEventTimeIncrement(ProfileEvents::Event event)
        : event_(event)
        , start_(std::chrono::steady_clock::now())
    {
    }

    ~ProfileEventTimeIncrement()
    {
        using namespace std::chrono;
        const auto elapsed = duration_cast<Unit>(
            steady_clock::now() - start_);
        ProfileEvents::increment(event_, elapsed.count());
    }

    ProfileEventTimeIncrement(const ProfileEventTimeIncrement &) = delete;
    ProfileEventTimeIncrement & operator=(const ProfileEventTimeIncrement &) = delete;

private:
    ProfileEvents::Event event_;
    std::chrono::steady_clock::time_point start_;
};

struct Microseconds
{
};

template <>
class ProfileEventTimeIncrement<Microseconds>
{
public:
    explicit ProfileEventTimeIncrement(ProfileEvents::Event event)
        : event_(event)
        , start_(std::chrono::steady_clock::now())
    {
    }

    ~ProfileEventTimeIncrement()
    {
        using namespace std::chrono;
        const auto us = duration_cast<microseconds>(
            steady_clock::now() - start_);
        ProfileEvents::increment(event_, us.count());
    }

    ProfileEventTimeIncrement(const ProfileEventTimeIncrement &) = delete;
    ProfileEventTimeIncrement & operator=(const ProfileEventTimeIncrement &) = delete;

private:
    ProfileEvents::Event event_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace facebook::velox::ch
```

Add `ProfileEvents.cpp` with the array storage (analogous to `CurrentMetrics.cpp`).

- [ ] **Step 5: Add real logging and current-exception formatting**

Preserve the corrected Task 003 `FileCacheLogger`, `LoggerPtr`, `getLogger`, and
`name()` public shapes. Do not change the logger back to a public `name` field,
and never return null.

Add Folly's verified exception-string API:

```cpp
#include <folly/ExceptionString.h>
#include <fmt/format.h>
#include <glog/logging.h>

inline std::string getCurrentExceptionMessage(bool = false)
{
    const auto exception = std::current_exception();
    if (!exception)
        return {};

    const auto message = folly::exceptionStr(exception);
    return std::string(message.data(), message.size());
}
```

`folly::exceptionStr(std::exception_ptr)` handles standard, Velox, nested, and
non-standard exceptions without a rethrow/catch ladder. The
`withStackTrace` parameter remains for CH call-shape compatibility. Do not
synthesize a second stack trace: a `VeloxException` string already includes its
Velox-managed stack-trace state when enabled.

Implement `tryLogCurrentException` without allowing a logging failure to replace
the active exception:

```cpp
inline void tryLogCurrentException(const LoggerPtr & logger, ...) noexcept
{
    try
    {
        const auto msg = getCurrentExceptionMessage();
        if (logger)
            LOG(WARNING) << "[" << logger->name() << "] exception: " << msg;
        else
            LOG(WARNING) << "exception: " << msg;
    }
    catch (...)
    {
        // This diagnostic helper must not replace the exception being handled.
    }
}
```

Replace the no-op production macros with lazy formatting. Evaluate the logger
expression once per enabled log call and use `name()` for the tag:

```cpp
#define FILECACHE_LOG_IMPL(level, logger_ptr, ...)            \
    do                                                         \
    {                                                          \
        if (VLOG_IS_ON(level))                                 \
        {                                                      \
            const auto & _fc_logger = (logger_ptr);            \
            const std::string _fc_msg =                        \
                fmt::format(__VA_ARGS__);                      \
            if (_fc_logger != nullptr)                         \
                VLOG(level) << "[" << _fc_logger->name()       \
                            << "] " << _fc_msg;                \
            else                                               \
                VLOG(level) << _fc_msg;                        \
        }                                                      \
    } while (false)

#define LOG_TRACE(logger_ptr, ...) FILECACHE_LOG_IMPL(3, logger_ptr, __VA_ARGS__)
#define LOG_DEBUG(logger_ptr, ...) FILECACHE_LOG_IMPL(2, logger_ptr, __VA_ARGS__)
#define LOG_INFO(logger_ptr, ...)  FILECACHE_LOG_IMPL(1, logger_ptr, __VA_ARGS__)
#define LOG_WARNING(logger_ptr, ...) \
    do { LOG(WARNING) << fmt::format(__VA_ARGS__); } while (false)
#define LOG_ERROR(logger_ptr, ...) \
    do { LOG(ERROR) << fmt::format(__VA_ARGS__); } while (false)
#define LOG_TEST(...) do {} while (false)
```

`LOG_TEST` remains no-op and must not evaluate its arguments. Verify that its
existing Task 003 non-evaluation test still passes.

- [ ] **Step 6: Replace `QueryStatus.h` with a `folly::CancellationToken` wrapper**

Replace the body of `velox/ch/Common/QueryStatus.h` with:

```cpp
#pragma once

#include <folly/CancellationToken.h>
#include "velox/common/base/Exceptions.h"

#include <memory>

namespace facebook::velox::ch
{

class QueryStatus
{
public:
    QueryStatus() = default;

    explicit QueryStatus(folly::CancellationToken token)
        : token_(std::move(token))
    {
    }

    void throwIfKilled() const
    {
        if (token_.isCancellationRequested())
            VELOX_FAIL("FileCache query cancelled");
    }

    bool isCancelled() const
    {
        return token_.isCancellationRequested();
    }

private:
    folly::CancellationToken token_;
};

using QueryStatusPtr = std::shared_ptr<QueryStatus>;

} // namespace facebook::velox::ch
```

- [ ] **Step 7: Connect `QueryStatus` in `FileCacheInputStream`**

Edit `velox/ch/Disks/IO/FileCacheInputStream.cpp` to accept and store a
`QueryStatus` at construction time, and call `throwIfKilled()` at exactly
the following safe points:

**Cancellation check points (safe = downloader lease NOT held):**

```text
1. Start of initializeIfNeeded(), before FileCache::getOrSet / get.
2. Start of nextFileSegmentsBatch(), before the cache lookup.
3. In the outer loop of Next(), after completing/advancing a segment and
   before starting the next one.
4. In prepareReadFromFileSegmentState(), after FileSegment::wait()
   returns (the wait may block; the downloader lease is not yet held at
   that point).
```

**Points where throwIfKilled() must NOT be called (downloader held):**

```text
- Between getOrSetDownloader() and completePartAndResetDownloader().
- Between reserve() and write().
- Inside writeCache().
- Inside predownloadForCurrentSegment() after reserve() is called.
```

The `FileCacheInputStream` constructor must accept an optional
`QueryStatus queryStatus = {}` parameter (defaulting to no-op). The
`FileCacheBufferedInput::enqueue` and `FileCacheBufferedInput::read` must
forward the `ConnectorQueryCtx::cancellationToken()` wrapped in a
`QueryStatus` when constructing `FileCacheInputStream`.

Add to `FileCacheInputStream.h`:

```cpp
// In the constructor declaration:
FileCacheInputStream(
    FileCacheBufferedInput * owner,
    velox::common::Region region,
    FileCacheRequestContext cacheContext,
    LogType logType,
    QueryStatus queryStatus = {});   // ← new parameter
```

Add to `FileCacheInputStream`'s private section:

```cpp
QueryStatus queryStatus_;
```

Modify `FileCacheBufferedInput::enqueue`:

```cpp
// Before creating the FileCacheInputStream, extract the cancellation token:
QueryStatus status{
    connectorQueryCtx_ != nullptr
        ? connectorQueryCtx_->cancellationToken()
        : folly::CancellationToken{}};

return std::make_unique<FileCacheInputStream>(
    this,
    region,
    requestContext_,
    logType,
    std::move(status));
```

`FileCacheBufferedInput` must store the `ConnectorQueryCtx*` reference if
it does not already do so. Add a member:

```cpp
const ConnectorQueryCtx * connectorQueryCtx_{nullptr};
```

and pass it from the constructor.

- [ ] **Step 8: Implement the observability test cases**

Replace each `GTEST_SKIP()` in
`velox/ch/Common/tests/ObservabilityTest.cpp` with real assertions:

**`CurrentMetricsTest.AddAndReadBack`:**
1. Read the initial value of `CacheFileSegments`.
2. Call `add(CacheFileSegments, 5)`.
3. Assert `get(CacheFileSegments)` equals `initial + 5`.
4. Restore: call `sub(CacheFileSegments, 5)`.

**`CurrentMetricsTest.IncrementScopedDecrementsOnDestruct`:**
1. Record initial value.
2. Open a scope: `{ Increment inc(CacheFileSegments, 3); }`.
3. After the scope, assert value is back to initial.

**`CurrentMetricsTest.SubDecrements`:**
1. `add(FilesystemCacheSizeLimit, 10)`.
2. `sub(FilesystemCacheSizeLimit, 4)`.
3. Assert `get == initial + 6`.
4. Restore.

**`ProfileEventsTest.IncrementAndReadBack`:**
1. Read initial value of `FilesystemCacheReserveAttempts`.
2. `increment(FilesystemCacheReserveAttempts, 2)`.
3. Assert value increased by 2.

**`ProfileEventsTest.TimeIncrementRecordsElapsed`:**
1. Record initial value of `FilesystemCacheReserveMicroseconds`.
2. Introduce a `ProfileEventTimeIncrement<Microseconds>` scope containing a
   `std::this_thread::sleep_for(std::chrono::milliseconds(5))`.
3. After the scope, assert the delta is ≥ 4000 (4 ms in microseconds) to
   allow for scheduler jitter.

**`QueryStatusTest.NoopWhenNotCancelled`:**
1. `QueryStatus s`.
2. Assert `s.throwIfKilled()` does not throw.
3. Assert `s.isCancelled()` is false.

**`QueryStatusTest.ThrowsWhenTokenCancelled`:**
1. `folly::CancellationSource src`.
2. `QueryStatus s{src.getToken()}`.
3. `src.requestCancellation()`.
4. Assert `s.throwIfKilled()` throws `VeloxRuntimeError`.

**`QueryStatusTest.DefaultConstructedIsNeverCancelled`:**
1. `QueryStatus s{}`.
2. Assert `s.isCancelled()` is false after any number of checks.

**`LoggerUsefulTest.GetLoggerReturnsNonNullPtr`:**
1. `auto log = getLogger("filecache.test")`.
2. Assert `log != nullptr`.
3. Assert `log->name() == "filecache.test"`.

**`LoggerUsefulTest.CurrentExceptionMessageIsEmptyOutsideCatch`:**
1. Call `getCurrentExceptionMessage(true)` with no active exception.
2. Assert the result is empty.

**`LoggerUsefulTest.CurrentExceptionMessageFormatsStdException`:**
1. Throw `std::runtime_error("std sentinel")`.
2. In the catch block, call `getCurrentExceptionMessage(true)`.
3. Assert the result is non-empty and contains `std sentinel`.

**`LoggerUsefulTest.CurrentExceptionMessageFormatsVeloxException`:**
1. Throw `VELOX_FAIL("velox sentinel")`.
2. In the catch block, call `getCurrentExceptionMessage(true)`.
3. Assert the result is non-empty and contains `velox sentinel`.
4. Do not assert a fixed stack-trace string; Velox stack capture is
   configuration-dependent.

**`LoggerUsefulTest.TryLogCurrentExceptionNoThrow`:**
1. Wrap in try/catch: throw a `std::runtime_error("test error")`.
2. In the catch block, call `tryLogCurrentException(log)`.
3. Assert no secondary exception escapes.

- [ ] **Step 9: Implement the cancellation test cases**

Replace each `GTEST_SKIP()` in
`velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp` with real
assertions. Use the same per-test fixture as `FileCacheE2ETest` (from
Task 015).

**`NextThrowsWhenCancelledBeforeRead`:**
1. Create a `folly::CancellationSource`; cancel it immediately.
2. Construct a `ConnectorQueryCtx` whose `cancellationToken()` is the
   cancelled token.
3. `enqueue` a region; call `Next()`.
4. Assert `VeloxRuntimeError` is thrown.
5. Assert the segment is not left in a DOWNLOADING state.

**`NextThrowsWhenCancelledAfterFirstSegment`:**
1. Create a 4-segment read; read the first segment successfully.
2. Cancel the token.
3. Call `Next()` again; assert `VeloxRuntimeError`.
4. Assert no downloader lease remains on any segment.

**`DownloaderNotHeldWhenCancelledDuringWait`:**
1. Create a stream where a second segment is in DOWNLOADING state held by
   another thread.
2. Cancel the token while the stream is waiting.
3. After the exception, verify `fileSegment.isDownloader()` is false for
   all segments visible to this stream.

**`NoCancellationTokenNeverCancels`:**
1. Construct `FileCacheInputStream` with default `QueryStatus{}`.
2. Read the full region; no exception must occur.

**`CancellationDoesNotLeakDownloaderLease`:**
1. Begin a write/download cycle; cancel the token at the point between
   `wait()` returning and `getOrSetDownloader()` being called.
2. The exception must propagate; verify via `fileSegment.getDownloader()`
   that no caller ID is registered on the segment after the exception.

- [ ] **Step 10: Build and run all tests**

Reject any skipped/disabled case before the final build:

```bash
if rg -n 'GTEST_SKIP|DISABLED_' \
  <velox_repo>/velox/ch/Common/tests/ObservabilityTest.cpp \
  <velox_repo>/velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp
then
  echo "ERROR: skipped observability/cancellation test remains"
  exit 1
fi
```

Then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_observability_test \
  velox_ch_cancellation_test \
  velox_ch_common_test \
  > <velox_build_dir>/build_017_observability.log 2>&1
echo "exit: $?"

ctest \
  --test-dir <velox_build_dir> \
  -R '^(velox_ch_observability_test|velox_ch_cancellation_test|velox_ch_common_test)$' \
  --output-on-failure \
  > <velox_build_dir>/test_017_observability.log 2>&1
echo "exit: $?"
```

Expected:

```text
Build exit code: 0.
100% tests passed, 0 tests failed.
```

Also build the E2E test from Task 015 to confirm the shim changes do not
break existing coverage:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_e2e_test \
  > <velox_build_dir>/build_017_regression.log 2>&1
echo "exit: $?"

ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_filecache_e2e_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_017_regression.log 2>&1
echo "exit: $?"
```

Expected:

```text
Build exit code: 0.
100% tests passed, 0 tests failed.
```

- [ ] **Step 11: Inspect task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Common/logger_useful.h \
  velox/ch/Common/CurrentMetrics.h \
  velox/ch/Common/CurrentMetrics.cpp \
  velox/ch/Common/ProfileEvents.h \
  velox/ch/Common/ProfileEvents.cpp \
  velox/ch/Common/QueryStatus.h \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Common/tests/CMakeLists.txt \
  velox/ch/Common/tests/ObservabilityTest.cpp \
  velox/ch/Disks/IO/FileCacheInputStream.cpp \
  velox/ch/Disks/IO/FileCacheInputStream.h \
  velox/ch/Disks/IO/FileCacheBufferedInput.cpp \
  velox/ch/Disks/IO/FileCacheBufferedInput.h \
  velox/ch/Disks/IO/tests/CMakeLists.txt \
  velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp
```

Expected:

```text
No whitespace errors.
Only the task-owned files appear in the diff.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 12: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/017-filecache-observability-cancellation-result.md
```

Use exactly this structure:

````markdown
# Task 017 Result: Observability and Cancellation Hardening

## Status

status: success

## Velox status

```text
<branch, HEAD, git status --short>
```

## Files changed

```text
<list only task-owned files>
```

## Commands run

```text
<configure, build, test, verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_017.log
<velox_build_dir>/build_017_skeleton.log
<velox_build_dir>/build_017_observability.log
<velox_build_dir>/test_017_observability.log
<velox_build_dir>/build_017_regression.log
<velox_build_dir>/test_017_regression.log
```

## Test results

```text
velox_ch_observability_test: <N> tests, 0 failed
velox_ch_cancellation_test:  <N> tests, 0 failed
velox_ch_common_test:        <N> tests, 0 failed (regression)
velox_ch_filecache_e2e_test: <N> tests, 0 failed (regression)
getCurrentExceptionMessage:  std and Velox exception text preserved
```

## Verification

```text
Initial shim audit output: <grep -c result>
Final build exit code: 0
git diff --check: no whitespace errors
```

## Blocking errors

```text
None
```

## Recommended next task

```text
No further post-MVP tasks are queued. Background download, free-space keeper,
dynamic resize reload, and Prometheus metrics belong to the "阶段 8：补充能力"
phase defined in port/01-filecache-port-order-design.md and should be
scheduled based on product priority.
```
````

## Explicit exclusions

Do not implement in this task:

```text
Overcommit / OvercommitFileCachePriority — explicitly deferred from the
  port (port/01-filecache-port-order-design.md section "阶段 2").

Write-through cache / cache_on_write_operations — explicitly deferred.

Full Prometheus metrics bridge — exposePrometheusEvictionMetrics and
  related settings require a Velox-side Prometheus sink that does not
  exist yet; atomic counters are the MVP implementation.

Structured log output / FilesystemCacheLog entries — the
  FilesystemCacheLog.h shim is present but remains a no-op in this task;
  structured log wiring is a follow-up.

Cancellation inside reserve() wait loop — the reserve() implementation
  may itself block; injecting cancellation inside the blocking wait
  requires passing a CancellationToken all the way into FileSegment::reserve
  which is an intrusive change deferred to a separate task.

Per-query statistics aggregation — the ProfileEvents atomic counters are
  process-wide; per-query aggregation requires a query-context stats map
  that is deferred.

glog initialization — this task assumes the Velox binary initializes glog
  (via folly::init or LOG_INIT); no new init call is added here.
```
