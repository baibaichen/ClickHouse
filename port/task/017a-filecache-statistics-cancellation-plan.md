# Task 017A — FileCache Statistics, Cancellation, and Scheduler Parity — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace no-op metrics with real relaxed-atomic storage, wire double-accounting in the reader, propagate cancellation tokens, and harden the scheduler with two plain locks matching the CH pattern.

**Architecture:** Metrics storage lives in dedicated `.cpp` TUs (not header-local statics) to guarantee a single process-wide instance across shared-library boundaries. The reader instruments both a global `ProfileEvents` counter and a query-scoped `IoStatistics`/`IoStats` ledger for every completed I/O fact. The scheduler replaces `std::recursive_mutex` with the CH two-plain-lock pattern (`execMutex_` → `scheduleMutex_`) so Folly inline-completion cannot self-deadlock.

**Tech Stack:** C++20, Folly futures/CancellationToken, GTest, CMake/Ninja, velox `IoStatistics`/`IoStats`.

## Global Constraints

- Velox source: `/root/oss/velox`
- Mono build dir: `/root/oss/velox/_build/debug` (`VELOX_MONO_LIBRARY=ON`)
- Non-mono build dir: `/root/oss/velox/_build/debug-task017a-nonmono` (`VELOX_MONO_LIBRARY=OFF`)
- Environment: `source /root/oss/velox-helper/env.sh` before cmake/ninja
- Toolchain: `-DCMAKE_TOOLCHAIN_FILE=/root/oss/gluten/dev/vcpkg/toolchain.cmake`
- No C++ sleeps in tests or production code
- Worker never commits; Controller commits after review per EXECUTION_PROTOCOL
- All build output redirected to log files in the build directory
- Do not build or run benchmark targets from Debug builds. Task 018 owns every
  benchmark build and uses RelWithDebInfo or Release.

---

### Task 1: Metrics Storage and Snapshot

**Files:**
- Modify: `velox/ch/Common/CurrentMetrics.h` (enum + declarations only; storage removed to .cpp)
- Create: `velox/ch/Common/CurrentMetrics.cpp` (array storage + function bodies)
- Modify: `velox/ch/Common/ProfileEvents.h` (enum + declarations only; storage removed to .cpp)
- Create: `velox/ch/Common/ProfileEvents.cpp` (array storage + function bodies + RAII timer)
- Create: `velox/ch/Common/FileCacheStats.h` (snapshot struct + factory + subtraction + `kFileCacheWriteBytes`)
- Create: `velox/ch/Common/FileCacheStats.cpp` (implementation)
- Modify: `velox/ch/Common/CMakeLists.txt` (register 3 new sources + 1 new header)
- Create: `velox/ch/Common/tests/MetricsAndSnapshotTest.cpp`
- Modify: `velox/ch/Common/tests/CMakeLists.txt` (add test target)

**Interfaces:**
- Produces: `CurrentMetrics::add`, `CurrentMetrics::sub`, `CurrentMetrics::get`, `CurrentMetrics::set`, `CurrentMetrics::Increment` (RAII)
- Produces: `ProfileEvents::increment`, `ProfileEvents::get`
- Produces: `ProfileEventTimeIncrement<Microseconds>` (RAII timer)
- Produces: `struct FileCacheStatsSnapshot`, `takeFileCacheStatsSnapshot()`, `FileCacheStatsSnapshot::operator-`
- Produces: `kFileCacheWriteBytes` constant (inline constexpr `const char*`)

- [ ] **Step 1: Write `CurrentMetrics.h` — enum + thin declarations forwarding to .cpp**

Replace the entire body of `velox/ch/Common/CurrentMetrics.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace facebook::velox::ch
{

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
    FilesystemCacheElements,
    FilesystemCacheInvalidatedElements,
    FilesystemCachePriorityQueueElements,
    FilesystemCacheSize,
    FilesystemCacheKeys,
    END
};

inline constexpr size_t kNumMetrics = static_cast<size_t>(END);

void add(Metric m, int64_t delta = 1);
void sub(Metric m, int64_t delta = 1);
int64_t get(Metric m);
void set(Metric m, int64_t v);

class Increment
{
public:
    explicit Increment(Metric m, int64_t delta = 1);
    ~Increment();
    Increment(const Increment &) = delete;
    Increment & operator=(const Increment &) = delete;

private:
    Metric metric_;
    int64_t delta_;
};

} // namespace CurrentMetrics

} // namespace facebook::velox::ch
```

- [ ] **Step 2: Create `CurrentMetrics.cpp` — storage array + function bodies**

Create `velox/ch/Common/CurrentMetrics.cpp`:

```cpp
#include "velox/ch/Common/CurrentMetrics.h"

#include <array>
#include <atomic>

namespace facebook::velox::ch
{

namespace CurrentMetrics
{

namespace
{
std::array<std::atomic<int64_t>, kNumMetrics> & storage()
{
    static std::array<std::atomic<int64_t>, kNumMetrics> v{};
    return v;
}
} // namespace

void add(Metric m, int64_t delta)
{
    storage()[static_cast<size_t>(m)].fetch_add(delta, std::memory_order_relaxed);
}

void sub(Metric m, int64_t delta)
{
    storage()[static_cast<size_t>(m)].fetch_sub(delta, std::memory_order_relaxed);
}

int64_t get(Metric m)
{
    return storage()[static_cast<size_t>(m)].load(std::memory_order_relaxed);
}

void set(Metric m, int64_t v)
{
    storage()[static_cast<size_t>(m)].store(v, std::memory_order_relaxed);
}

Increment::Increment(Metric m, int64_t delta) : metric_(m), delta_(delta)
{
    add(metric_, delta_);
}

Increment::~Increment()
{
    sub(metric_, delta_);
}

} // namespace CurrentMetrics

} // namespace facebook::velox::ch
```

- [ ] **Step 3: Write `ProfileEvents.h` — enum (all 50 existing + 10 new) + declarations**

Replace the entire body of `velox/ch/Common/ProfileEvents.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace facebook::velox::ch
{

namespace ProfileEvents
{

enum Event
{
    // 50 existing events (unchanged order)
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
    FilesystemCacheStateLockMicroseconds,
    FilesystemCachePriorityWriteLockMicroseconds,
    FilesystemCachePriorityReadLockMicroseconds,
    FileSegmentFailToIncreasePriority,
    FileSegmentHolderCompleteMicroseconds,
    FileSegmentIncreasePriorityMicroseconds,
    FileSegmentLockMicroseconds,
    FilesystemCacheBackgroundDownloadQueuePush,
    FilesystemCacheBackgroundEvictedBytes,
    FilesystemCacheBackgroundEvictedFileSegments,
    FilesystemCacheBackgroundRemovedInvalidatedEntries,
    FilesystemCacheCreatedKeyDirectories,
    FilesystemCacheDowngradedFileSegments,
    FilesystemCacheEvictMicroseconds,
    FilesystemCacheEvictedBytes,
    FilesystemCacheEvictedFileSegments,
    FilesystemCacheEvictionReusedIterator,
    FilesystemCacheEvictionSkippedEvictingFileSegments,
    FilesystemCacheEvictionSkippedFileSegments,
    FilesystemCacheEvictionSkippedMovingFileSegments,
    FilesystemCacheEvictionTries,
    FilesystemCacheFailToReserveSpaceBecauseOfCacheResize,
    FilesystemCacheFailedEvictionCandidates,
    FilesystemCacheFreeSpaceKeepingThreadErrors,
    FilesystemCacheFreeSpaceKeepingThreadRun,
    FilesystemCacheFreeSpaceKeepingThreadWorkMilliseconds,
    FilesystemCacheHoldFileSegments,
    FilesystemCacheIdleClientEvictions,
    FilesystemCacheInvalidatedEntriesCleanupThreadWorkMilliseconds,
    FilesystemCacheLoadMetadataMicroseconds,
    FilesystemCacheLockKeyMicroseconds,
    FilesystemCacheLockMetadataMicroseconds,
    FilesystemCacheLockOriginPoolMicroseconds,
    FilesystemCacheUnusedHoldFileSegments,
    OpenedFileCacheHits,
    OpenedFileCacheMisses,
    OpenedFileCacheMicroseconds,
    // 10 new CH CachedOnDiskReadBufferFromFile reader events
    CachedReadBufferWaitReadBufferMicroseconds,
    CachedReadBufferReadFromSourceMicroseconds,
    CachedReadBufferPredownloadedFromSourceMicroseconds,
    CachedReadBufferReadFromCacheMicroseconds,
    CachedReadBufferCacheWriteMicroseconds,
    CachedReadBufferPredownloadedFromSourceBytes,
    CachedReadBufferPredownloadedBytes,
    CachedReadBufferCreateBufferMicroseconds,
    CachedReadBufferReadFromCacheHits,
    CachedReadBufferReadFromCacheMisses,
    END
};

inline constexpr size_t kNumEvents = static_cast<size_t>(END);

void increment(Event e, uint64_t delta = 1);
uint64_t get(Event e);

} // namespace ProfileEvents

struct Microseconds {};

template <typename Unit>
class ProfileEventTimeIncrement
{
public:
    explicit ProfileEventTimeIncrement(ProfileEvents::Event e);
    ~ProfileEventTimeIncrement();
    ProfileEventTimeIncrement(const ProfileEventTimeIncrement &) = delete;
    ProfileEventTimeIncrement & operator=(const ProfileEventTimeIncrement &) = delete;

    uint64_t elapsed() const;

private:
    ProfileEvents::Event event_;
    uint64_t startNs_;
};

} // namespace facebook::velox::ch
```

- [ ] **Step 4: Create `ProfileEvents.cpp` — storage array + RAII timer impl**

Create `velox/ch/Common/ProfileEvents.cpp`:

```cpp
#include "velox/ch/Common/ProfileEvents.h"

#include <array>
#include <atomic>
#include <chrono>

namespace facebook::velox::ch
{

namespace ProfileEvents
{

namespace
{
std::array<std::atomic<uint64_t>, kNumEvents> & storage()
{
    static std::array<std::atomic<uint64_t>, kNumEvents> c{};
    return c;
}
} // namespace

void increment(Event e, uint64_t delta)
{
    storage()[static_cast<size_t>(e)].fetch_add(delta, std::memory_order_relaxed);
}

uint64_t get(Event e)
{
    return storage()[static_cast<size_t>(e)].load(std::memory_order_relaxed);
}

} // namespace ProfileEvents

namespace
{
uint64_t nowNs()
{
    // duration_cast guarantees nanoseconds regardless of steady_clock::period.
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
} // namespace

template <typename Unit>
ProfileEventTimeIncrement<Unit>::ProfileEventTimeIncrement(ProfileEvents::Event e)
    : event_(e), startNs_(nowNs())
{
}

template <typename Unit>
ProfileEventTimeIncrement<Unit>::~ProfileEventTimeIncrement()
{
    const uint64_t elapsedNs = nowNs() - startNs_;
    // Convert nanoseconds to microseconds (the only Unit instantiated).
    const uint64_t us = elapsedNs / 1000;
    ProfileEvents::increment(event_, us);
}

template <typename Unit>
uint64_t ProfileEventTimeIncrement<Unit>::elapsed() const
{
    return (nowNs() - startNs_) / 1000;
}

template class ProfileEventTimeIncrement<Microseconds>;

} // namespace facebook::velox::ch
```

- [ ] **Step 5: Create `FileCacheStats.h` — snapshot struct + `kFileCacheWriteBytes`**

Create `velox/ch/Common/FileCacheStats.h`:

```cpp
#pragma once

#include <cstdint>

namespace facebook::velox::ch
{

/// RuntimeMetric key for bytes written to the FileCache. Used in IoStats
/// free-form counters; flows through FileDataSource -> RuntimeMetric ->
/// OperatorStats -> TaskStats -> Gluten JNI -> Spark SQLMetric.
inline constexpr const char * kFileCacheWriteBytes = "fileCacheWriteBytes";

/// Point-in-time snapshot of FileCache gauges + cumulative counters.
struct FileCacheStatsSnapshot
{
    // Gauges (from CurrentMetrics)
    int64_t cacheSize = 0;
    int64_t cacheSizeLimit = 0;
    int64_t cacheKeys = 0;
    int64_t cacheElements = 0;
    int64_t cacheFileSegments = 0;
    int64_t holdFileSegments = 0;
    int64_t invalidatedElements = 0;
    int64_t priorityQueueElements = 0;
    int64_t downloadQueueElements = 0;
    int64_t delayedCleanupElements = 0;
    int64_t reserveThreads = 0;

    // Cumulative counters (from ProfileEvents)
    uint64_t cacheReadBytes = 0;
    uint64_t sourceReadBytes = 0;
    uint64_t cacheWriteBytes = 0;
    uint64_t cacheHitCount = 0;
    uint64_t cacheMissCount = 0;
    uint64_t predownloadedFromSourceBytes = 0;
    uint64_t predownloadedBytes = 0;
    uint64_t reserveAttempts = 0;
    uint64_t reserveFailures = 0;
    uint64_t evictedBytes = 0;
    uint64_t evictedSegments = 0;
    uint64_t evictionTries = 0;
    uint64_t waitReadBufferMicroseconds = 0;
    uint64_t readFromSourceMicroseconds = 0;
    uint64_t predownloadedFromSourceMicroseconds = 0;
    uint64_t readFromCacheMicroseconds = 0;
    uint64_t cacheWriteMicroseconds = 0;
    uint64_t createBufferMicroseconds = 0;

    /// Subtract a previous snapshot to get deltas for cumulative counters.
    /// Gauge fields are taken from `*this` (the newer snapshot).
    FileCacheStatsSnapshot operator-(const FileCacheStatsSnapshot & prev) const;
};

/// Loads a point-in-time snapshot from the global metrics storage.
FileCacheStatsSnapshot takeFileCacheStatsSnapshot();

} // namespace facebook::velox::ch
```

- [ ] **Step 6: Create `FileCacheStats.cpp` — implementation**

Create `velox/ch/Common/FileCacheStats.cpp`:

```cpp
#include "velox/ch/Common/FileCacheStats.h"
#include "velox/ch/Common/CurrentMetrics.h"
#include "velox/ch/Common/ProfileEvents.h"

namespace facebook::velox::ch
{

FileCacheStatsSnapshot FileCacheStatsSnapshot::operator-(
    const FileCacheStatsSnapshot & prev) const
{
    FileCacheStatsSnapshot delta;
    // Gauges: take current (this) values
    delta.cacheSize = cacheSize;
    delta.cacheSizeLimit = cacheSizeLimit;
    delta.cacheKeys = cacheKeys;
    delta.cacheElements = cacheElements;
    delta.cacheFileSegments = cacheFileSegments;
    delta.holdFileSegments = holdFileSegments;
    delta.invalidatedElements = invalidatedElements;
    delta.priorityQueueElements = priorityQueueElements;
    delta.downloadQueueElements = downloadQueueElements;
    delta.delayedCleanupElements = delayedCleanupElements;
    delta.reserveThreads = reserveThreads;
    // Cumulative: subtract
    delta.cacheReadBytes = cacheReadBytes - prev.cacheReadBytes;
    delta.sourceReadBytes = sourceReadBytes - prev.sourceReadBytes;
    delta.cacheWriteBytes = cacheWriteBytes - prev.cacheWriteBytes;
    delta.cacheHitCount = cacheHitCount - prev.cacheHitCount;
    delta.cacheMissCount = cacheMissCount - prev.cacheMissCount;
    delta.predownloadedFromSourceBytes = predownloadedFromSourceBytes - prev.predownloadedFromSourceBytes;
    delta.predownloadedBytes = predownloadedBytes - prev.predownloadedBytes;
    delta.reserveAttempts = reserveAttempts - prev.reserveAttempts;
    delta.reserveFailures = reserveFailures - prev.reserveFailures;
    delta.evictedBytes = evictedBytes - prev.evictedBytes;
    delta.evictedSegments = evictedSegments - prev.evictedSegments;
    delta.evictionTries = evictionTries - prev.evictionTries;
    delta.waitReadBufferMicroseconds = waitReadBufferMicroseconds - prev.waitReadBufferMicroseconds;
    delta.readFromSourceMicroseconds = readFromSourceMicroseconds - prev.readFromSourceMicroseconds;
    delta.predownloadedFromSourceMicroseconds = predownloadedFromSourceMicroseconds - prev.predownloadedFromSourceMicroseconds;
    delta.readFromCacheMicroseconds = readFromCacheMicroseconds - prev.readFromCacheMicroseconds;
    delta.cacheWriteMicroseconds = cacheWriteMicroseconds - prev.cacheWriteMicroseconds;
    delta.createBufferMicroseconds = createBufferMicroseconds - prev.createBufferMicroseconds;
    return delta;
}

FileCacheStatsSnapshot takeFileCacheStatsSnapshot()
{
    FileCacheStatsSnapshot s;
    // Gauges
    s.cacheSize = CurrentMetrics::get(CurrentMetrics::FilesystemCacheSize);
    s.cacheSizeLimit = CurrentMetrics::get(CurrentMetrics::FilesystemCacheSizeLimit);
    s.cacheKeys = CurrentMetrics::get(CurrentMetrics::FilesystemCacheKeys);
    s.cacheElements = CurrentMetrics::get(CurrentMetrics::FilesystemCacheElements);
    s.cacheFileSegments = CurrentMetrics::get(CurrentMetrics::CacheFileSegments);
    s.holdFileSegments = CurrentMetrics::get(CurrentMetrics::FilesystemCacheHoldFileSegments);
    s.invalidatedElements = CurrentMetrics::get(CurrentMetrics::FilesystemCacheInvalidatedElements);
    s.priorityQueueElements = CurrentMetrics::get(CurrentMetrics::FilesystemCachePriorityQueueElements);
    s.downloadQueueElements = CurrentMetrics::get(CurrentMetrics::FilesystemCacheDownloadQueueElements);
    s.delayedCleanupElements = CurrentMetrics::get(CurrentMetrics::FilesystemCacheDelayedCleanupElements);
    s.reserveThreads = CurrentMetrics::get(CurrentMetrics::FilesystemCacheReserveThreads);
    // Cumulative
    s.cacheReadBytes = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromCacheBytes);
    s.sourceReadBytes = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromSourceBytes);
    s.cacheWriteBytes = ProfileEvents::get(ProfileEvents::CachedReadBufferCacheWriteBytes);
    s.cacheHitCount = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromCacheHits);
    s.cacheMissCount = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromCacheMisses);
    s.predownloadedFromSourceBytes = ProfileEvents::get(ProfileEvents::CachedReadBufferPredownloadedFromSourceBytes);
    s.predownloadedBytes = ProfileEvents::get(ProfileEvents::CachedReadBufferPredownloadedBytes);
    s.reserveAttempts = ProfileEvents::get(ProfileEvents::FilesystemCacheReserveAttempts);
    s.reserveFailures = ProfileEvents::get(ProfileEvents::FilesystemCacheFailedReserveAttempts);
    s.evictedBytes = ProfileEvents::get(ProfileEvents::FilesystemCacheEvictedBytes);
    s.evictedSegments = ProfileEvents::get(ProfileEvents::FilesystemCacheEvictedFileSegments);
    s.evictionTries = ProfileEvents::get(ProfileEvents::FilesystemCacheEvictionTries);
    s.waitReadBufferMicroseconds = ProfileEvents::get(ProfileEvents::CachedReadBufferWaitReadBufferMicroseconds);
    s.readFromSourceMicroseconds = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromSourceMicroseconds);
    s.predownloadedFromSourceMicroseconds = ProfileEvents::get(ProfileEvents::CachedReadBufferPredownloadedFromSourceMicroseconds);
    s.readFromCacheMicroseconds = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromCacheMicroseconds);
    s.cacheWriteMicroseconds = ProfileEvents::get(ProfileEvents::CachedReadBufferCacheWriteMicroseconds);
    s.createBufferMicroseconds = ProfileEvents::get(ProfileEvents::CachedReadBufferCreateBufferMicroseconds);
    return s;
}

} // namespace facebook::velox::ch
```

- [ ] **Step 7: Update `velox/ch/Common/CMakeLists.txt` — register new sources and header**

In the `velox_add_library(velox_ch_filecache ...)` call, add three sources and one header:

```cmake
velox_add_library(
  velox_ch_filecache
  CurrentMetrics.cpp
  ProfileEvents.cpp
  FileCacheStats.cpp
  StatusFile.cpp
  ThreadPool.cpp
  FileCacheQueryIdScope.cpp
  FileCacheScheduler.cpp
  SipHash128.cpp
  HEADERS
    ClickHouseAliases.h
    ClickHouseAssert.h
    CurrentMetrics.h
    FailPoint.h
    FileCacheBoundedQueue.h
    FileCacheException.h
    FileCacheFilesystem.h
    FileCacheQueryIdScope.h
    FileCacheScheduler.h
    FileCacheStats.h
    FilesystemCacheLog.h
    logger_useful.h
    OpenTelemetryTraceContext.h
    ProfileEvents.h
    QueryStatus.h
    SharedMutex.h
    SipHash128.h
    StatusFile.h
    ThreadPool.h
)
```

- [ ] **Step 8: Write `MetricsAndSnapshotTest.cpp`**

Create `velox/ch/Common/tests/MetricsAndSnapshotTest.cpp`:

```cpp
#include "velox/ch/Common/CurrentMetrics.h"
#include "velox/ch/Common/FileCacheStats.h"
#include "velox/ch/Common/ProfileEvents.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace facebook::velox::ch;

TEST(CurrentMetricsTest, AddSubGetRoundTrip)
{
    auto m = CurrentMetrics::CacheFileSegments;
    auto before = CurrentMetrics::get(m);
    CurrentMetrics::add(m, 5);
    EXPECT_EQ(CurrentMetrics::get(m), before + 5);
    CurrentMetrics::sub(m, 3);
    EXPECT_EQ(CurrentMetrics::get(m), before + 2);
    CurrentMetrics::sub(m, 2);
    EXPECT_EQ(CurrentMetrics::get(m), before);
}

TEST(CurrentMetricsTest, SetOverwrites)
{
    auto m = CurrentMetrics::FilesystemCacheSize;
    CurrentMetrics::set(m, 42);
    EXPECT_EQ(CurrentMetrics::get(m), 42);
    CurrentMetrics::set(m, 0);
    EXPECT_EQ(CurrentMetrics::get(m), 0);
}

TEST(CurrentMetricsTest, IncrementRAII)
{
    auto m = CurrentMetrics::FilesystemCacheElements;
    auto before = CurrentMetrics::get(m);
    {
        CurrentMetrics::Increment inc(m, 7);
        EXPECT_EQ(CurrentMetrics::get(m), before + 7);
    }
    EXPECT_EQ(CurrentMetrics::get(m), before);
}

TEST(ProfileEventsTest, IncrementAccumulates)
{
    auto e = ProfileEvents::FilesystemCacheReserveAttempts;
    auto before = ProfileEvents::get(e);
    ProfileEvents::increment(e, 10);
    ProfileEvents::increment(e, 3);
    EXPECT_EQ(ProfileEvents::get(e), before + 13);
}

TEST(ProfileEventsTest, TimeIncrementRecordsNonzero)
{
    auto e = ProfileEvents::FilesystemCacheGetMicroseconds;
    auto before = ProfileEvents::get(e);
    {
        ProfileEventTimeIncrement<Microseconds> timer(e);
        // Busy spin for at least 1 microsecond to guarantee nonzero elapsed
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::microseconds(50))
        {
        }
    }
    EXPECT_GT(ProfileEvents::get(e), before);
}

TEST(ProfileEventsTest, AllExistingEnumNamesCompile)
{
    // Verify index correctness: last existing event is at index 49
    EXPECT_EQ(
        static_cast<size_t>(ProfileEvents::OpenedFileCacheMicroseconds), 49u);
    // First new event is at index 50
    EXPECT_EQ(
        static_cast<size_t>(ProfileEvents::CachedReadBufferWaitReadBufferMicroseconds), 50u);
    // Total count: 50 existing + 10 new = 60
    EXPECT_EQ(ProfileEvents::kNumEvents, 60u);
}

TEST(FileCacheStatsSnapshotTest, ReflectsCurrentValues)
{
    CurrentMetrics::set(CurrentMetrics::FilesystemCacheSize, 1024);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromCacheBytes, 100);
    auto snap = takeFileCacheStatsSnapshot();
    EXPECT_EQ(snap.cacheSize, 1024);
    EXPECT_GE(snap.cacheReadBytes, 100u);
}

TEST(FileCacheStatsSnapshotTest, SubtractionProducesDeltas)
{
    auto before = takeFileCacheStatsSnapshot();
    ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromSourceBytes, 500);
    CurrentMetrics::set(CurrentMetrics::FilesystemCacheKeys, 7);
    auto after = takeFileCacheStatsSnapshot();
    auto delta = after - before;
    EXPECT_EQ(delta.sourceReadBytes, 500u);
    EXPECT_EQ(delta.cacheKeys, 7); // gauge: from `after`
}

TEST(ProfileEventsTest, NewReaderEventsPresent)
{
    ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromCacheHits);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromCacheMisses);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromCacheMicroseconds, 10);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromSourceMicroseconds, 20);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferPredownloadedFromSourceMicroseconds, 30);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferCacheWriteMicroseconds, 40);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferPredownloadedFromSourceBytes, 50);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferPredownloadedBytes, 60);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferCreateBufferMicroseconds, 70);
    ProfileEvents::increment(ProfileEvents::CachedReadBufferWaitReadBufferMicroseconds, 80);
    EXPECT_GE(ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromCacheHits), 1u);
    EXPECT_GE(ProfileEvents::get(ProfileEvents::CachedReadBufferWaitReadBufferMicroseconds), 80u);
}
```

- [ ] **Step 9: Add test target in `velox/ch/Common/tests/CMakeLists.txt`**

Append:

```cmake
add_executable(velox_ch_metrics_snapshot_test MetricsAndSnapshotTest.cpp)
add_test(velox_ch_metrics_snapshot_test velox_ch_metrics_snapshot_test)

target_link_libraries(
  velox_ch_metrics_snapshot_test
  PRIVATE
    velox_ch_filecache
    velox_test_util
    velox_exception
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

- [ ] **Step 10: Build**

```bash
source /root/oss/velox-helper/env.sh && cd /root/oss/velox/_build/debug && ninja velox_ch_metrics_snapshot_test > build_pt1.log 2>&1
```

Expected: exit 0. Then dispatch a `task` subagent to analyze `build_pt1.log` and return only a
concise pass/fail summary plus any compiler errors (repository build-log protocol — never paste
the raw log into the main context).

- [ ] **Step 11: Run tests**

```bash
cd /root/oss/velox/_build/debug && ./velox/ch/Common/tests/velox_ch_metrics_snapshot_test > test_pt1.log 2>&1
```

Expected: 9 passing tests. Then dispatch a `task` subagent to analyze `test_pt1.log` and return a
concise pass/fail summary.

- [ ] **Step 12: Mutation RED**

Temporarily replace `fetch_add` in `CurrentMetrics.cpp` `add` with a no-op (`(void)m; (void)delta;`). Rebuild and retest.

Expected: `AddSubGetRoundTrip` fails. Revert.

---

### Task 2: Reader Double-Accounting (Global + Query Ledger)

**Files:**
- Modify: `velox/ch/Disks/IO/FileCacheBufferedInput.h` (add `ioStatistics()`/`ioStats()` public accessors)
- Modify: `velox/ch/Disks/IO/FileCacheInputStream.h` (add `ioStatistics_`, `ioStats_` private members)
- Modify: `velox/ch/Disks/IO/FileCacheInputStream.cpp` (ctor capture + accounting calls at read/write/predownload sites)
- Modify: `velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp` (statistics tests)

**Interfaces:**
- Consumes: `ProfileEvents::increment` (from Task 1)
- Consumes: `kFileCacheWriteBytes` (from Task 1, `velox/ch/Common/FileCacheStats.h`)
- Consumes: `io::IoStatistics::ssdRead()`, `io::IoStatistics::read()`, `io::IoStatistics::prefetch()`, `io::IoStatistics::incRawBytesRead`, `io::IoStatistics::incTotalScanTimeNs`
- Consumes: `velox::IoStats::addCounter(const std::string& name, RuntimeCounter counter)`
- Produces: double-accounting wiring (each I/O fact updates both global ProfileEvents AND query IoStatistics/IoStats). Mapping (design §3.4): physical cache read before clamp → global cache bytes + `ssdRead`; physical source read before clamp → global source bytes + `read`; post-clamp logical return → `incRawBytesRead`; predownload → global source/predownload bytes + `read` + `prefetch` (NOT `incRawBytesRead`); cache write → `IoStats["fileCacheWriteBytes"]`.

- [ ] **Step 1: Add public accessors to `FileCacheBufferedInput.h`**

After the existing `memoryPool()` accessor (around line 126), add:

```cpp
io::IoStatistics * ioStatistics() const { return ioStatistics_.get(); }
velox::IoStats * ioStats() const { return ioStats_.get(); }
```

- [ ] **Step 2: Add private members to `FileCacheInputStream.h`**

In the private section, add:

```cpp
io::IoStatistics * ioStatistics_ = nullptr;
velox::IoStats * ioStats_ = nullptr;
```

Add include at top of `FileCacheInputStream.h`:

```cpp
#include "velox/common/io/IoStatistics.h"
#include "velox/common/file/File.h"
```

- [ ] **Step 3: Wire `FileCacheInputStream.cpp` ctor to capture from owner**

After the existing `queryContextHolder_` assignment (line ~73), add:

```cpp
ioStatistics_ = owner_->ioStatistics();
ioStats_ = owner_->ioStats();
```

Add includes at top of `FileCacheInputStream.cpp`:

```cpp
#include "velox/ch/Common/FileCacheStats.h"
#include "velox/ch/Common/ProfileEvents.h"
#include "velox/common/base/RuntimeMetrics.h"
```

- [ ] **Step 4: Add accounting in read-from-cache path**

Site: `FileCacheInputStream::readFromCurrentSegment`, immediately after
`state_->reader->next()` determines `size`, before cache write and before the
final requested-range clamp, when `state_->readType == ReadType::CACHED`.
These are physical bytes read from the local cache, matching CH:

```cpp
ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromCacheHits);
ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromCacheBytes, size);
if (ioStatistics_)
    ioStatistics_->ssdRead().increment(size);
```

After cache write and the final range clamp, increment
`incRawBytesRead(static_cast<int64_t>(size))` once with the logical bytes
actually returned.

- [ ] **Step 5: Add accounting in read-from-source path**

Site: the same post-`next`, pre-write, pre-clamp point when `state_->readType`
is a remote read (`REMOTE_FS_READ_AND_PUT_IN_CACHE` or
`REMOTE_FS_READ_BYPASS_CACHE`). These are physical source bytes, matching CH:

```cpp
ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromCacheMisses);
ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromSourceBytes, size);
if (ioStatistics_)
    ioStatistics_->read().increment(size);
```

After cache write and the final range clamp, increment `incRawBytesRead` once
with only the logical bytes actually returned.

- [ ] **Step 6: Add accounting in predownload path**

Site: `FileCacheInputStream::predownloadForCurrentSegment`, after each source read of `got`
bytes succeeds (immediately before/after the `reserve`+`writeCache` for that gap chunk, once
per loop iteration).

Predownloaded bytes are gap bytes fetched from source to fill the cache; they are **NOT**
logical bytes returned to the caller. Per design §3.4 ("predownload source bytes → `read` and
`prefetch`") they update **both** `read` and `prefetch`, and per design §3.4 line "logical
bytes returned → `incRawBytesRead`" they must **NOT** touch `incRawBytesRead` — the logical
returned bytes are counted exactly once on the cache/source return in Step 4/5:

```cpp
ProfileEvents::increment(ProfileEvents::CachedReadBufferPredownloadedBytes, got);
ProfileEvents::increment(ProfileEvents::CachedReadBufferPredownloadedFromSourceBytes, got);
ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromSourceBytes, got);
if (ioStatistics_)
{
    ioStatistics_->read().increment(got);
    ioStatistics_->prefetch().increment(got);
    // Deliberately NO incRawBytesRead here: predownload is not returned to the
    // caller. Adding it would double-count the gap against raw input bytes.
}
```

- [ ] **Step 7: Add accounting in cache-write path**

After successful `fileSegment.write(data, size, offset)`:

```cpp
ProfileEvents::increment(ProfileEvents::CachedReadBufferCacheWriteBytes, size);
if (ioStats_)
    ioStats_->addCounter(kFileCacheWriteBytes, RuntimeCounter(static_cast<int64_t>(size), RuntimeCounter::Unit::kBytes));
```

- [ ] **Step 8: Add hit/miss accounting**

Count hit/miss at the same post-`next`, pre-clamp physical-read point as Steps
4/5, even when `size == 0`, matching CH. Do not count at reader construction:
a reused multi-chunk bypass reader must record one miss per physical read.

```cpp
ProfileEvents::increment(
    isCacheRead ? ProfileEvents::CachedReadBufferReadFromCacheHits
                : ProfileEvents::CachedReadBufferReadFromCacheMisses);
```

- [ ] **Step 9: Add latency accounting**

At entry of source-read scope:

```cpp
ProfileEventTimeIncrement<Microseconds> sourceTimer(ProfileEvents::CachedReadBufferReadFromSourceMicroseconds);
```

At entry of cache-read scope:

```cpp
ProfileEventTimeIncrement<Microseconds> cacheTimer(ProfileEvents::CachedReadBufferReadFromCacheMicroseconds);
```

At entry of cache-write scope:

```cpp
ProfileEventTimeIncrement<Microseconds> writeTimer(ProfileEvents::CachedReadBufferCacheWriteMicroseconds);
```

Map to `IoStatistics` scan time:

```cpp
if (ioStatistics_)
    ioStatistics_->incTotalScanTimeNs(static_cast<int64_t>(sourceTimer.elapsed()) * 1000);
```

- [ ] **Step 10: Route all statistics tests through an extended `makeInput`, then add deterministic ledger tests**

First extend the existing `makeInput` fixture helper. Do **not** hand-roll the raw
`FileCacheBufferedInput` constructor inside individual tests — that is fragile and drifts from
the real type signatures (`FileCacheRequestContext` and `FileCacheOriginInfo` are structs the
fixture already builds correctly). Add two optional parameters after `readerPool`. Complete
replacement of the helper body:

```cpp
    std::unique_ptr<FileCacheBufferedInput> makeInput(
        FileCacheManager & manager,
        FileCachePtr cache,
        std::shared_ptr<ReadFile> source,
        FileCacheKey key,
        FileCacheReadOptions opts = {},
        const std::string & queryId = "q",
        velox::memory::MemoryPool * readerPool = nullptr,
        std::shared_ptr<io::IoStatistics> ioStatistics = nullptr,
        std::shared_ptr<velox::IoStats> ioStats = nullptr)
    {
        dwio::common::ReaderOptions readerOptions(readerPool ? readerPool : pool_.get());
        FileCacheRequestContext context;
        context.queryId = queryId;
        context.userId = manager.commonUserId();
        FileCacheOriginInfo origin(manager.commonUserId(), context.userWeight);
        return std::make_unique<FileCacheBufferedInput>(
            std::move(source),
            std::move(cache),
            std::move(key),
            origin,
            std::move(opts),
            context,
            dwio::common::MetricsLog::voidLog(),
            std::move(ioStatistics),
            std::move(ioStats),
            executor_.get(),
            readerOptions);
    }
```

All existing callers pass at most `readerPool`, so they keep compiling unchanged. Task 3
extends this same helper once more (adds the cancellation token argument) — no test ever calls
the raw constructor.

Then append these tests inside the anonymous namespace. Every assertion is an **exact** delta so
that dropping either the global `ProfileEvents` update or the query `IoStatistics`/`IoStats`
update fails the test. `IoCounter::increment(n)` bumps `count()` by 1 and `sum()` by `n` (verified
in `velox/common/base/IoCounter.h`), so byte totals are read from `sum()`. Global counters are
process-wide, but a GTest binary runs its tests serially and the config sets
`backgroundDownloadThreads = 0`, so before/after deltas are deterministic:

```cpp
TEST_F(FileCacheBufferedInputTest, CacheReadUpdatesGlobalAndIoStatistics)
{
    auto manager = makeManager();
    auto cache = makeCache(*manager);
    const auto data = makeData(4096);
    auto key = FileCacheKey::fromPath("stats-cache-read");
    auto ioStatistics = std::make_shared<io::IoStatistics>();
    auto ioStats = std::make_shared<velox::IoStats>();

    // Warm the cache: the first read fully downloads [0, 4096) into one segment.
    {
        auto warmSource = std::make_shared<CountingReadFile>(data);
        auto warm = makeInput(*manager, cache, warmSource, key, {}, "q", nullptr, ioStatistics, ioStats);
        readAll(*warm->read(0, 4096, dwio::common::LogType::STREAM));
    }

    // Second read of the same key is a pure cache hit.
    const uint64_t globalBefore = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromCacheBytes);
    const uint64_t ssdSumBefore = ioStatistics->ssdRead().sum();
    const uint64_t ssdCountBefore = ioStatistics->ssdRead().count();
    const uint64_t rawBefore = ioStatistics->rawBytesRead();

    auto source = std::make_shared<CountingReadFile>(data);
    auto input = makeInput(*manager, cache, source, key, {}, "q", nullptr, ioStatistics, ioStats);
    readAll(*input->read(0, 4096, dwio::common::LogType::STREAM));

    // Cache read: global cache-read bytes and query ssdRead each advance by 4096,
    // the hit is counted as logical returned bytes (rawBytesRead), and no source
    // byte is touched.
    EXPECT_EQ(ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromCacheBytes) - globalBefore, 4096u);
    EXPECT_EQ(ioStatistics->ssdRead().sum() - ssdSumBefore, 4096u);
    EXPECT_GT(ioStatistics->ssdRead().count(), ssdCountBefore);
    EXPECT_EQ(ioStatistics->rawBytesRead() - rawBefore, 4096u);
    EXPECT_EQ(source->preadBytes(), 0u);
}

TEST_F(FileCacheBufferedInputTest, SourceReadUpdatesGlobalAndIoStatistics)
{
    auto manager = makeManager();
    auto cache = makeCache(*manager);
    const auto data = makeData(4096);
    auto source = std::make_shared<CountingReadFile>(data);
    auto key = FileCacheKey::fromPath("stats-source-read");
    auto ioStatistics = std::make_shared<io::IoStatistics>();
    auto input = makeInput(*manager, cache, source, key, {}, "q", nullptr, ioStatistics);

    const uint64_t globalBefore = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromSourceBytes);
    const uint64_t rawBefore = ioStatistics->rawBytesRead();
    const uint64_t readSumBefore = ioStatistics->read().sum();

    readAll(*input->read(0, 4096, dwio::common::LogType::STREAM));

    // Cold read: 4096 source bytes returned -> global source bytes, query read
    // sum, and raw input bytes each advance by exactly 4096.
    EXPECT_EQ(ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromSourceBytes) - globalBefore, 4096u);
    EXPECT_EQ(ioStatistics->read().sum() - readSumBefore, 4096u);
    EXPECT_EQ(ioStatistics->rawBytesRead() - rawBefore, 4096u);
}

TEST_F(FileCacheBufferedInputTest, CacheWriteUpdatesGlobalAndIoStats)
{
    auto manager = makeManager();
    auto cache = makeCache(*manager);
    const auto data = makeData(4096);
    auto source = std::make_shared<CountingReadFile>(data);
    auto key = FileCacheKey::fromPath("stats-cache-write");
    auto ioStats = std::make_shared<velox::IoStats>();
    auto input = makeInput(*manager, cache, source, key, {}, "q", nullptr, nullptr, ioStats);

    const uint64_t globalBefore = ProfileEvents::get(ProfileEvents::CachedReadBufferCacheWriteBytes);

    readAll(*input->read(0, 4096, dwio::common::LogType::STREAM));

    // The cold read wrote the whole 4096-byte segment to cache exactly once.
    EXPECT_EQ(ProfileEvents::get(ProfileEvents::CachedReadBufferCacheWriteBytes) - globalBefore, 4096u);
    auto stats = ioStats->stats();
    auto it = stats.find(kFileCacheWriteBytes);
    ASSERT_NE(it, stats.end());
    EXPECT_EQ(it->second.sum, 4096);
}

TEST_F(FileCacheBufferedInputTest, SameFactUpdatesBothLedgers)
{
    // A single cold read updates BOTH the global ProfileEvents ledger AND the
    // query IoStatistics/IoStats ledger independently -- neither is derived from
    // the other.
    auto manager = makeManager();
    auto cache = makeCache(*manager);
    const auto data = makeData(4096);
    auto source = std::make_shared<CountingReadFile>(data);
    auto key = FileCacheKey::fromPath("stats-dual-ledger");
    auto ioStatistics = std::make_shared<io::IoStatistics>();
    auto ioStats = std::make_shared<velox::IoStats>();
    auto input = makeInput(*manager, cache, source, key, {}, "q", nullptr, ioStatistics, ioStats);

    const uint64_t gSrcBefore = ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromSourceBytes);
    const uint64_t gWrBefore = ProfileEvents::get(ProfileEvents::CachedReadBufferCacheWriteBytes);
    const uint64_t rawBefore = ioStatistics->rawBytesRead();

    readAll(*input->read(0, 4096, dwio::common::LogType::STREAM));

    EXPECT_EQ(ProfileEvents::get(ProfileEvents::CachedReadBufferReadFromSourceBytes) - gSrcBefore, 4096u);
    EXPECT_EQ(ProfileEvents::get(ProfileEvents::CachedReadBufferCacheWriteBytes) - gWrBefore, 4096u);
    EXPECT_EQ(ioStatistics->rawBytesRead() - rawBefore, 4096u);
    auto stats = ioStats->stats();
    auto it = stats.find(kFileCacheWriteBytes);
    ASSERT_NE(it, stats.end());
    EXPECT_EQ(it->second.sum, 4096);
}

TEST_F(FileCacheBufferedInputTest, PredownloadUpdatesReadPrefetchButNotRawBytes)
{
    // Deterministic predownload built on the accepted
    // TruncatedObjectPredownloadMetadataAbsent scenario, but with the full object
    // present so the predownload SUCCEEDS and every byte count is exact. A first
    // reader partially fills a segment; a second reader seeks past the written
    // prefix, becomes the downloader, and predownloads the exact gap before its
    // own read.
    auto manager = makeManager();
    auto cache = makeCache(*manager, [](FileCacheConfig & c) { c.maxFileSegmentSize = 10; });
    const auto data = makeData(10);
    const auto key = FileCacheKey::fromPath("predownload-stats");
    const FileCacheOriginInfo origin(manager->commonUserId(), 0);

    // Pin the single segment [0, 10) so its state is observable across readers.
    auto probe = cache->getOrSet(key, 0, data.size(), data.size(), CreateFileSegmentSettings{}, 0, origin);
    ASSERT_EQ(probe->size(), 1u);

    // Q1 downloads [0, 2) and stops, leaving the segment PARTIALLY_DOWNLOADED.
    auto source1 = std::make_shared<CountingReadFile>(data);
    FileCacheReadOptions q1;
    q1.remoteFsBufferSize = 2;
    auto input1 = makeInput(*manager, cache, source1, key, q1, "q1");
    auto stream1 = input1->read(0, data.size(), dwio::common::LogType::STREAM);
    const void * chunk = nullptr;
    int size = 0;
    ASSERT_TRUE(stream1->Next(&chunk, &size));
    ASSERT_EQ(probe->front().getCurrentWriteOffset(), 2u);

    // Q2 seeks to offset 5 (> currentWriteOffset 2), becomes the downloader, and
    // predownloads the exact gap [2, 5) = 3 bytes from source, then reads [5, 10).
    auto ioStatistics = std::make_shared<io::IoStatistics>();
    auto source2 = std::make_shared<CountingReadFile>(data);
    FileCacheReadOptions q2;
    q2.remoteFsBufferSize = 8;
    auto input2 = makeInput(*manager, cache, source2, key, q2, "q2", nullptr, ioStatistics);

    const uint64_t gPredownBefore = ProfileEvents::get(ProfileEvents::CachedReadBufferPredownloadedBytes);
    const uint64_t gPredownSrcBefore = ProfileEvents::get(ProfileEvents::CachedReadBufferPredownloadedFromSourceBytes);
    const uint64_t readSumBefore = ioStatistics->read().sum();
    const uint64_t prefetchSumBefore = ioStatistics->prefetch().sum();
    const uint64_t rawBefore = ioStatistics->rawBytesRead();

    auto stream2 = input2->read(0, data.size(), dwio::common::LogType::STREAM);
    std::vector<uint64_t> seekPositions{5};
    dwio::common::PositionProvider provider(seekPositions);
    stream2->seekToPosition(provider);
    const void * chunk2 = nullptr;
    int size2 = 0;
    ASSERT_TRUE(stream2->Next(&chunk2, &size2));
    const auto returned = static_cast<uint64_t>(size2);
    ASSERT_GT(returned, 0u);

    // Predownload of exactly 3 gap bytes: both global predownload counters += 3.
    EXPECT_EQ(ProfileEvents::get(ProfileEvents::CachedReadBufferPredownloadedBytes) - gPredownBefore, 3u);
    EXPECT_EQ(ProfileEvents::get(ProfileEvents::CachedReadBufferPredownloadedFromSourceBytes) - gPredownSrcBefore, 3u);
    // Query ledger: predownload maps to BOTH read and prefetch (design §3.4).
    EXPECT_EQ(ioStatistics->prefetch().sum() - prefetchSumBefore, 3u);
    // read() gets the 3 predownload bytes plus the `returned` bytes read at offset 5.
    EXPECT_EQ(ioStatistics->read().sum() - readSumBefore, 3u + returned);
    // KEY invariant (design §3.4): predownload is NOT logical returned bytes, so
    // rawBytesRead advances only by the bytes returned to the caller -- never the
    // 3-byte gap. This fails if the predownload path wrongly calls incRawBytesRead.
    EXPECT_EQ(ioStatistics->rawBytesRead() - rawBefore, returned);
}
```

Add these includes at the top of `FileCacheBufferedInputTest.cpp`:

```cpp
#include "velox/ch/Common/FileCacheStats.h"
#include "velox/ch/Common/ProfileEvents.h"
#include "velox/common/base/RuntimeMetrics.h"
```

- [ ] **Step 11: Build**

```bash
cd /root/oss/velox/_build/debug && ninja velox_ch_filecache_buffered_input_test > build_pt2.log 2>&1
```

Expected: exit 0. Then dispatch a `task` subagent to read `build_pt2.log` and return only a
concise pass/fail summary plus any compiler errors (per the repository build-log protocol — do
not paste the raw log into the main context).

- [ ] **Step 12: Run tests**

```bash
cd /root/oss/velox/_build/debug && ./velox/ch/Disks/IO/tests/velox_ch_filecache_buffered_input_test --gtest_filter="*Updates*" > test_pt2.log 2>&1
```

Expected: all 5 new tests pass (`CacheReadUpdatesGlobalAndIoStatistics`,
`SourceReadUpdatesGlobalAndIoStatistics`, `CacheWriteUpdatesGlobalAndIoStats`,
`SameFactUpdatesBothLedgers`, `PredownloadUpdatesReadPrefetchButNotRawBytes`; the `*Updates*`
filter matches exactly these five). Then dispatch a `task` subagent to analyze `test_pt2.log`
and return a concise pass/fail summary.

- [ ] **Step 13: Mutation RED (two independent mutations)**

1. Comment out `ProfileEvents::increment(ProfileEvents::CachedReadBufferReadFromCacheBytes, size)`
   in the read-from-cache path. Rebuild + retest: `CacheReadUpdatesGlobalAndIoStatistics` fails
   on its exact global-delta assertion. Revert.
2. In the predownload path, add the forbidden `ioStatistics_->incRawBytesRead(got)` back.
   Rebuild + retest: `PredownloadUpdatesReadPrefetchButNotRawBytes` fails because `rawBytesRead`
   advances by `returned + 3` instead of `returned`. Revert.

This proves both the global-ledger update AND the correct (non-raw) predownload mapping are
load-bearing.

---

### Task 3: Cancellation Token Propagation

**Files:**
- Modify: `velox/ch/Disks/IO/FileCacheBufferedInput.h` (add ctor param + member + accessor)
- Modify: `velox/ch/Disks/IO/FileCacheBufferedInput.cpp` (store token in ctor + pass in clone)
- Modify: `velox/ch/Disks/IO/FileCacheInputStream.h` (add `<folly/CancellationToken.h>` include + `cancellationToken_` member)
- Modify: `velox/ch/Disks/IO/FileCacheInputStream.cpp` (ctor capture; real token to `FileSegment::wait`; two `TestValue::adjust` hooks — `beforeSegmentWait`, `afterDownloaderElected`; cancellation checks in `nextFileSegmentsBatch` and `completeCurrentSegmentAndAdvance`; `#include "velox/common/testutil/TestValue.h"`)
- Modify: `velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp` (extend `makeInput`; add `StallingReadFile`; cancellation tests)

**Interfaces:**
- Consumes: `folly::CancellationToken` (from folly)
- Produces: `FileCacheBufferedInput::cancellationToken()` accessor
- Produces for Task 018: ctor parameter `folly::CancellationToken cancellationToken = {}` appended after `fileReadOps`
- Produces (test-only): `TestValue` injection points `FileCacheInputStream::beforeSegmentWait` and `FileCacheInputStream::afterDownloaderElected` (compiled out in release)
- Safe cancellation points (design §4.2): top of `nextFileSegmentsBatch` (before first lookup / between batches); inside `FileSegment::wait`; after `completeAndPopFront` in `completeCurrentSegmentAndAdvance` (after completing a segment). No check between downloader election and release, or between reserve and write.

- [ ] **Step 1: Add ctor parameter to `FileCacheBufferedInput.h`**

Change constructor signature — append `cancellationToken` after `fileReadOps`:

```cpp
FileCacheBufferedInput(
    std::shared_ptr<ReadFile> readFile,
    FileCachePtr cache,
    FileCacheKey cacheKey,
    FileCacheOriginInfo origin,
    FileCacheReadOptions cacheOptions,
    FileCacheRequestContext requestContext,
    const dwio::common::MetricsLogPtr & metricsLog,
    std::shared_ptr<io::IoStatistics> ioStatistics,
    std::shared_ptr<velox::IoStats> ioStats,
    folly::Executor * executor,
    const dwio::common::ReaderOptions & readerOptions,
    folly::F14FastMap<std::string, std::string> fileReadOps = {},
    folly::CancellationToken cancellationToken = {});
```

Add `#include <folly/CancellationToken.h>` to the header.

Add private member (after `cancellationToken_` after the fileReadOps-related members are not present, so place after `fileSize_`):

```cpp
folly::CancellationToken cancellationToken_;
```

Add public accessor (after the `ioStats()` accessor):

```cpp
const folly::CancellationToken & cancellationToken() const
{
    return cancellationToken_;
}
```

- [ ] **Step 2: Update `FileCacheBufferedInput.cpp` ctor**

Add parameter to the definition:

```cpp
FileCacheBufferedInput::FileCacheBufferedInput(
    std::shared_ptr<ReadFile> readFile,
    FileCachePtr cache,
    FileCacheKey cacheKey,
    FileCacheOriginInfo origin,
    FileCacheReadOptions cacheOptions,
    FileCacheRequestContext requestContext,
    const dwio::common::MetricsLogPtr & metricsLog,
    std::shared_ptr<io::IoStatistics> ioStatistics,
    std::shared_ptr<velox::IoStats> ioStats,
    folly::Executor * executor,
    const dwio::common::ReaderOptions & readerOptions,
    folly::F14FastMap<std::string, std::string> fileReadOps,
    folly::CancellationToken cancellationToken)
```

In the member initializer list, add after `fileSize_(...)`:

```cpp
      cancellationToken_(std::move(cancellationToken))
```

In the `clone()` method (line ~145), pass `cancellationToken_` as the last argument to the new `FileCacheBufferedInput`.

- [ ] **Step 3: Add `cancellationToken_` to `FileCacheInputStream.h`**

`FileCacheInputStream.h` does **not** currently include `<folly/CancellationToken.h>` (verified
against the live header — the earlier draft's "already present at line 23" claim was wrong). Add
the include near the other includes, then add the private member:

```cpp
#include <folly/CancellationToken.h>
```

```cpp
folly::CancellationToken cancellationToken_;
```

- [ ] **Step 4: Capture token in `FileCacheInputStream.cpp` ctor**

After the `queryContextHolder_` assignment (the last statement of the constructor body, ~line 72):

```cpp
cancellationToken_ = owner_->cancellationToken();
```

- [ ] **Step 5: Use the stored token in `FileSegment::wait`, guarded by a test hook**

In `createReadFromFileSegmentState`, the `State::DOWNLOADING` branch (live line ~410). Insert a
`TestValue` hook immediately before the wait — this is a safe check point (the caller is a pure
waiter holding no downloader lease) — then pass the real token instead of a fresh empty one:

```cpp
// BEFORE:
downloadState = fileSegment.wait(offset, folly::CancellationToken{});
// AFTER:
common::testutil::TestValue::adjust(
    "facebook::velox::ch::FileCacheInputStream::beforeSegmentWait", this);
downloadState = fileSegment.wait(offset, cancellationToken_);
```

Add `#include "velox/common/testutil/TestValue.h"` to `FileCacheInputStream.cpp`. (`TestValue::adjust`
compiles to a no-op in `NDEBUG`/release builds, so this adds no production overhead.)

- [ ] **Step 6: Add a test hook the instant this stream wins the downloader lease**

Still in `createReadFromFileSegmentState`, as the FIRST statement inside
`if (downloaderId == FileSegment::getCallerId())` (the `EMPTY`/`PARTIALLY_DOWNLOADED` branch,
live line ~421) — after the lease is held, before any predownload/reserve/write. **No**
cancellation is checked here (design §4.2 forbids a check between downloader election and
release); the hook only lets a test request cancellation in the middle of a live download
transaction to prove it is deferred:

```cpp
if (downloaderId == FileSegment::getCallerId())
{
    common::testutil::TestValue::adjust(
        "facebook::velox::ch::FileCacheInputStream::afterDownloaderElected", this);
}
```

Insert only the `TestValue::adjust` call as the first statement of the existing
branch; retain the branch's following predownload/read-type statements verbatim.

- [ ] **Step 7: Add the "before first lookup / between batches" cancellation check**

At the very top of `nextFileSegmentsBatch`, before any of the three cache lookups. This single
site covers both design §4.2 safe points "before the first FileCache lookup" and "between
completed segment batches": it runs on the first read and whenever the batch is exhausted, and
never while a downloader lease or reserve/write is held:

Insert this check as the first statement of the existing function body:

```cpp
if (cancellationToken_.isCancellationRequested())
    VELOX_FAIL("FileCache read cancelled before segment batch lookup");
```

Retain all three existing cache-lookup branches after this inserted check.

- [ ] **Step 8: Add the "after completing/advancing a segment" cancellation check**

In `completeCurrentSegmentAndAdvance`, AFTER `completeAndPopFront` finalizes and releases the
just-read segment, BEFORE fetching the next batch. This is design §4.2's "after
completing/advancing the current segment" safe point: the downloader has already been released
in `readNextChunk` (lines ~846-858) and the segment is completed, so no lease or in-flight
reserve/write is held:

```cpp
    readInfo_.fileSegments->completeAndPopFront(
        owner_->cacheOptions().allowBackgroundDownload, /*force_shrink_to_downloaded_size=*/false);

    if (cancellationToken_.isCancellationRequested())
        VELOX_FAIL("FileCache read cancelled after completing a segment");

    if (readInfo_.fileSegments->empty() && !nextFileSegmentsBatch(nextOffset))
        return false;
```

- [ ] **Step 9: Extend `makeInput` with the token, add the `StallingReadFile` helper**

Extend the same `makeInput` helper once more — append a trailing `cancellationToken` parameter
(after the `ioStats` parameter added in Task 2) and forward it as the last constructor argument.
No test calls the raw constructor:

```cpp
    std::unique_ptr<FileCacheBufferedInput> makeInput(
        FileCacheManager & manager,
        FileCachePtr cache,
        std::shared_ptr<ReadFile> source,
        FileCacheKey key,
        FileCacheReadOptions opts = {},
        const std::string & queryId = "q",
        velox::memory::MemoryPool * readerPool = nullptr,
        std::shared_ptr<io::IoStatistics> ioStatistics = nullptr,
        std::shared_ptr<velox::IoStats> ioStats = nullptr,
        folly::CancellationToken cancellationToken = {})
    {
        dwio::common::ReaderOptions readerOptions(readerPool ? readerPool : pool_.get());
        FileCacheRequestContext context;
        context.queryId = queryId;
        context.userId = manager.commonUserId();
        FileCacheOriginInfo origin(manager.commonUserId(), context.userWeight);
        return std::make_unique<FileCacheBufferedInput>(
            std::move(source),
            std::move(cache),
            std::move(key),
            origin,
            std::move(opts),
            context,
            dwio::common::MetricsLog::voidLog(),
            std::move(ioStatistics),
            std::move(ioStats),
            executor_.get(),
            readerOptions,
            /*fileReadOps*/ {},
            std::move(cancellationToken));
    }
```

Add a `StallingReadFile` next to `CountingReadFile` in the anonymous namespace. Its FIRST
`pread` parks the downloader (segment stays `DOWNLOADING`) until a baton is posted; later
`pread`s serve data normally. A `std::atomic<bool>` (not a second baton post, which would be UB)
signals "downloader is parked" so the flag can be observed with the existing bounded `spinUntil`:

```cpp
/// Source whose FIRST pread blocks until `release` is posted, parking a downloader
/// inside a DOWNLOADING segment so another reader is forced onto FileSegment::wait.
/// Subsequent preads serve data normally.
class StallingReadFile : public ReadFile
{
public:
    StallingReadFile(std::string data, std::atomic<bool> & entered, folly::Baton<> & release)
        : data_(std::move(data)), entered_(entered), release_(release)
    {
    }

    std::string_view pread(uint64_t offset, uint64_t length, void * buf, const FileIoContext & = {}) const override
    {
        if (!stalled_.exchange(true))
        {
            entered_.store(true);
            release_.wait();
        }
        if (offset >= data_.size())
            return {};
        const uint64_t n = std::min<uint64_t>(length, data_.size() - offset);
        std::memcpy(buf, data_.data() + offset, n);
        return std::string_view(static_cast<const char *>(buf), n);
    }

    uint64_t size() const override { return data_.size(); }
    uint64_t memoryUsage() const override { return data_.size(); }
    bool shouldCoalesce() const override { return false; }
    std::string getName() const override { return "<StallingReadFile>"; }
    uint64_t getNaturalReadSize() const override { return 1024; }

private:
    std::string data_;
    std::atomic<bool> & entered_;
    folly::Baton<> & release_;
    mutable std::atomic<bool> stalled_{false};
};
```

- [ ] **Step 10: Write the cancellation tests**

Add these includes at the top of `FileCacheBufferedInputTest.cpp` (alongside the existing folly
includes):

```cpp
#include <folly/CancellationToken.h>
#include <folly/ScopeGuard.h>
#include <folly/synchronization/Baton.h>

#include <exception>
#include <mutex>
```

Append to `FileCacheBufferedInputTest.cpp`:

```cpp
TEST_F(FileCacheBufferedInputTest, DefaultTokenReadsFully)
{
    // Default (empty) token: nothing is ever cancelled, the read completes.
    auto manager = makeManager();
    auto cache = makeCache(*manager);
    const auto data = makeData(4096);
    auto source = std::make_shared<CountingReadFile>(data);
    auto key = FileCacheKey::fromPath("cancel-default");
    auto input = makeInput(*manager, cache, source, key);
    EXPECT_EQ(readAll(*input->read(0, 4096, dwio::common::LogType::STREAM)).size(), 4096u);
}
```

```cpp
TEST_F(FileCacheBufferedInputTest, CopiedTokenReachesStream)
{
    auto manager = makeManager();
    auto cache = makeCache(*manager);
    const auto data = makeData(4096);
    auto source = std::make_shared<CountingReadFile>(data);
    auto key = FileCacheKey::fromPath("cancel-token-copy");

    folly::CancellationSource src;
    auto input = makeInput(*manager, cache, source, key, {}, "q", nullptr, nullptr, nullptr, src.getToken());

    EXPECT_FALSE(input->cancellationToken().isCancellationRequested());
    src.requestCancellation();
    EXPECT_TRUE(input->cancellationToken().isCancellationRequested());
}

TEST_F(FileCacheBufferedInputTest, CancellationBeforeLookupThrows)
{
    auto manager = makeManager();
    auto cache = makeCache(*manager);
    const auto data = makeData(4096);
    auto source = std::make_shared<CountingReadFile>(data);
    auto key = FileCacheKey::fromPath("cancel-before-lookup");

    folly::CancellationSource src;
    src.requestCancellation(); // cancelled before any I/O
    auto input = makeInput(*manager, cache, source, key, {}, "q", nullptr, nullptr, nullptr, src.getToken());

    // The first nextFileSegmentsBatch check throws before any source read happens.
    VELOX_ASSERT_THROW(readAll(*input->read(0, 4096, dwio::common::LogType::STREAM)), "cancelled");
    EXPECT_EQ(source->preadBytes(), 0u);
}

TEST_F(FileCacheBufferedInputTest, CancellationDuringSegmentWaitThrows)
{
    // A downloader parks the segment in DOWNLOADING; a second reader is forced
    // onto FileSegment::wait with an *uncancelled* token, reaches the
    // beforeSegmentWait hook, and is cancelled only once it is actually there.
    // This exercises the cancellation check *inside* FileSegment::wait -- not the
    // pre-lookup check (the token is still uncancelled when the batch is looked
    // up), which is the defect the earlier draft had by pre-cancelling.
    auto manager = makeManager();
    auto cache = makeCache(*manager);
    const auto data = makeData(4096);
    auto key = FileCacheKey::fromPath("cancel-during-wait");

    std::atomic<bool> downloaderParked{false};
    folly::Baton<> releaseDownloader;
    std::once_flag releaseOnce;
    auto releaseDownloaderFn = [&] { std::call_once(releaseOnce, [&] { releaseDownloader.post(); }); };
    auto stalling = std::make_shared<StallingReadFile>(data, downloaderParked, releaseDownloader);

    // The beforeSegmentWait hook fires once per wait() call; guard the post so a
    // (theoretical) second wait iteration cannot double-post the baton (UB).
    folly::Baton<> waiterAtWait;
    std::once_flag atWaitOnce;
    ScopedTestValue beforeWait(
        "facebook::velox::ch::FileCacheInputStream::beforeSegmentWait",
        std::function<void(void *)>(
            [&](void *) { std::call_once(atWaitOnce, [&] { waiterAtWait.post(); }); }));

    folly::CancellationSource cancelSrc;

    // Downloader: elects itself and parks in pread, holding the segment DOWNLOADING.
    std::exception_ptr downloaderError;
    std::thread downloader([&]
    {
        try
        {
            auto in = makeInput(*manager, cache, stalling, key, {}, "downloader");
            readAll(*in->read(0, 4096, dwio::common::LogType::STREAM));
        }
        catch (...)
        {
            downloaderError = std::current_exception();
        }
    });
    auto downloaderGuard = folly::makeGuard([&]
    {
        releaseDownloaderFn();
        if (downloader.joinable())
            downloader.join();
    });

    ASSERT_TRUE(spinUntil([&] { return downloaderParked.load(); }, std::chrono::seconds(20)))
        << "downloader never parked in pread (segment not DOWNLOADING)";

    // Waiter: same key, uncancelled token. It must reach FileSegment::wait.
    std::exception_ptr waiterError;
    std::atomic<bool> waiterDone{false};
    std::thread waiter([&]
    {
        try
        {
            auto in = makeInput(*manager, cache, stalling, key, {}, "waiter",
                                nullptr, nullptr, nullptr, cancelSrc.getToken());
            readAll(*in->read(0, 4096, dwio::common::LogType::STREAM));
        }
        catch (...)
        {
            waiterError = std::current_exception();
        }
        waiterDone.store(true);
    });
    auto waiterGuard = folly::makeGuard([&]
    {
        releaseDownloaderFn(); // let the waiter's wait() end even under a mutation
        if (waiter.joinable())
            waiter.join();
    });

    // The waiter is parked immediately before FileSegment::wait: cancel it there.
    // The wait loop observes the cancellation within one 1s slice and throws.
    waiterAtWait.wait();
    cancelSrc.requestCancellation();

    ASSERT_TRUE(spinUntil([&] { return waiterDone.load(); }, std::chrono::seconds(30)))
        << "waiter never observed cancellation inside FileSegment::wait";
    waiter.join();
    waiterGuard.dismiss();
    ASSERT_TRUE(waiterError != nullptr) << "waiter returned without throwing";
    VELOX_ASSERT_THROW(std::rethrow_exception(waiterError), "cancelled");

    // Release + join the downloader; its own read is uncancelled and must succeed.
    releaseDownloaderFn();
    downloader.join();
    downloaderGuard.dismiss();
    if (downloaderError)
        std::rethrow_exception(downloaderError);
}

TEST_F(FileCacheBufferedInputTest, CancellationDeferredUntilAfterSegmentWriteCompletes)
{
    // Request cancellation the instant this reader owns the downloader lease for
    // the first segment (mid-transaction). Cancellation must NOT interrupt the
    // reserve+write; the exception is deferred to the next safe boundary, by
    // which point the first segment is fully written. The earlier draft cancelled
    // *before* the transaction began, proving nothing.
    auto manager = makeManager();
    auto cache = makeCache(*manager, [](FileCacheConfig & c) { c.maxFileSegmentSize = 8; });
    const auto data = makeData(16);
    auto source = std::make_shared<CountingReadFile>(data);
    auto key = FileCacheKey::fromPath("cancel-after-downloader-elected");

    folly::CancellationSource cancelSrc;
    std::atomic<bool> cancelledOnce{false};
    ScopedTestValue afterElected(
        "facebook::velox::ch::FileCacheInputStream::afterDownloaderElected",
        std::function<void(void *)>([&](void *)
        {
            if (!cancelledOnce.exchange(true))
                cancelSrc.requestCancellation();
        }));

    auto input = makeInput(*manager, cache, source, key, {}, "q",
                           nullptr, nullptr, nullptr, cancelSrc.getToken());

    // The read throws only at the safe boundary AFTER the first segment's
    // reserve+write completes -- never mid-transaction.
    VELOX_ASSERT_THROW(
        readAll(*input->read(0, 16, dwio::common::LogType::STREAM)), "cancelled");

    // Proof the write/complete happened before the exception: segment [0, 8) is
    // fully DOWNLOADED (8 bytes) and no segment is left DOWNLOADING.
    const auto infos = cache->getFileSegmentInfos(manager->commonUserId());
    bool firstComplete = false;
    for (const auto & info : infos)
    {
        EXPECT_NE(info.state, FileSegment::State::DOWNLOADING)
            << "segment at " << info.range_left << " left DOWNLOADING after cancellation";
        if (info.range_left == 0)
            firstComplete = info.state == FileSegment::State::DOWNLOADED && info.downloaded_size == 8;
    }
    EXPECT_TRUE(firstComplete)
        << "first segment [0, 8) was not fully written before the cancellation exception";
}
```

- [ ] **Step 11: Build**

```bash
cd /root/oss/velox/_build/debug && ninja velox_ch_filecache_buffered_input_test > build_pt3.log 2>&1
```

Expected: exit 0. Then dispatch a `task` subagent to analyze `build_pt3.log` and return a concise
pass/fail summary with any compiler errors.

- [ ] **Step 12: Run tests**

```bash
cd /root/oss/velox/_build/debug && ./velox/ch/Disks/IO/tests/velox_ch_filecache_buffered_input_test --gtest_filter="*Token*:*Cancellation*" > test_pt3.log 2>&1
```

Expected: all 5 pass (`DefaultTokenReadsFully`, `CopiedTokenReachesStream`,
`CancellationBeforeLookupThrows`, `CancellationDuringSegmentWaitThrows`,
`CancellationDeferredUntilAfterSegmentWriteCompletes`). Then dispatch a `task` subagent to
analyze `test_pt3.log` and return a concise pass/fail summary.

- [ ] **Step 13: Mutation RED (two independent mutations)**

1. Revert the wait call to `fileSegment.wait(offset, folly::CancellationToken{})` (empty token).
   Rebuild + retest: `CancellationDuringSegmentWaitThrows` fails — the waiter never observes
   cancellation, so `waiterDone` stays false and the 30s `spinUntil` assertion fires (the
   `waiterGuard`/`downloaderGuard` scope guards still release the downloader and join both
   threads, so the run tears down cleanly instead of hanging). Revert.
2. Delete the `nextFileSegmentsBatch` pre-lookup check. Rebuild + retest:
   `CancellationBeforeLookupThrows` fails — the read no longer throws before touching the source
   (`source->preadBytes()` becomes non-zero). Revert.

---

### Task 4: Caller Identity + Scheduler Two-Lock Parity

**Files:**
- Modify: `velox/ch/Common/FileCacheQueryIdScope.h` (update the public caller-id format contract)
- Modify: `velox/ch/Common/FileCacheQueryIdScope.cpp` (`getCallerId()` — add thread name)
- Modify: `velox/ch/Common/FileCacheScheduler.h` (replace `recursive_mutex` + `condition_variable_any` with exactly two plain `std::mutex` — `execMutex_` and `scheduleMutex_`; remove `cv_`, `callbackInFlight_`, and `#include <condition_variable>`)
- Modify: `velox/ch/Common/FileCacheScheduler.cpp` (rewrite locking per two-lock protocol)
- Modify: `velox/ch/Common/tests/SchedulerAndScopeTest.cpp` (add 5 new tests via the existing `TestScheduler`/`FileCacheQueryIdScopeTest`; no new fixture)
- Modify: `velox/ch/Interpreters/FileCache/FileSegment.cpp` (remove the obsolete comment that says the restored background format is deferred)
- Modify: `velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp` (update the pre-existing no-scope caller-id acceptance test to the restored three-field format)

**Interfaces:**
- Produces: `getCallerId()` returns `"<query-id>:<os-tid>"` or `"None:<thread-name>:<os-tid>"`
- Produces: `execMutex_` (serializes callback execution + deactivate drain)
- Produces: `scheduleMutex_` (protects state/pending/generation/timer)
- Lock order when both needed: `execMutex_` first, then `scheduleMutex_`

- [ ] **Step 1: Update `getCallerId()` in `FileCacheQueryIdScope.cpp`**

Add `#include <folly/system/ThreadName.h>` at the top. Replace the function body:

```cpp
std::string FileCacheQueryIdScope::getCallerId()
{
    const auto tid = std::to_string(folly::getOSThreadID());
    const auto & qid = tCurrentQueryId;
    if (qid.empty())
    {
        std::string name = folly::getCurrentThreadName().value_or("unknown");
        return "None:" + name + ":" + tid;
    }
    return qid + ":" + tid;
}
```

- [ ] **Step 2: Replace the mutex declarations in `FileCacheScheduler.h` with exactly two plain mutexes**

The header must end up with **only** `execMutex_` and `scheduleMutex_` — no condition variable
and no `callbackInFlight_` (the drain is done by acquiring `execMutex_`, which cannot be held
while a callback runs, so no separate flag or CV is needed).

Replace the member block:

```cpp
    mutable std::recursive_mutex mutex_;
    std::condition_variable_any cv_;
```

with:

```cpp
    // Two plain locks (CH BackgroundSchedulePool structure):
    //   execMutex_     - serializes callback execution; deactivate() acquires it
    //                    to drain (block until) any running callback.
    //   scheduleMutex_ - protects state_/pending*/generation_/timerFuture_.
    // Lock order when both are needed: execMutex_ THEN scheduleMutex_.
    mutable std::mutex execMutex_;
    mutable std::mutex scheduleMutex_;
```

Then delete the now-unused `callbackInFlight_` member and its comment block entirely, and remove
`#include <condition_variable>` from the header includes. Also update the long `armTimerLocked`
doc comment: it currently justifies a `std::recursive_mutex` by describing inline-continuation
self-deadlock; replace that paragraph with the two-lock rule — the continuation is attached
**after** `scheduleMutex_` is released (see Step 9), so an inline run re-locks a *free*
`scheduleMutex_` and cannot self-deadlock; `weak_ptr + generation` still guard lifetime and
staleness.

- [ ] **Step 3: Change `armTimerLocked` signature in `FileCacheScheduler.h`**

Replace (line 170):

```cpp
    void armTimerLocked(uint64_t delayMs);
```

With:

```cpp
    void armTimerLocked(std::unique_lock<std::mutex> & lock, uint64_t delayMs);
```

- [ ] **Step 4: Rewrite `setCallback()` in `FileCacheScheduler.cpp`**

```cpp
void FileCacheScheduledTask::setCallback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(scheduleMutex_);
    callback_ = std::move(callback);
}
```

- [ ] **Step 5: Rewrite `schedule()` in `FileCacheScheduler.cpp`**

```cpp
bool FileCacheScheduledTask::schedule()
{
    std::lock_guard<std::mutex> lock(scheduleMutex_);

    if (state_ == State::Deactivated)
        return false;

    if (state_ == State::Idle || state_ == State::Delayed)
    {
        cancelTimerLocked();
        queueImmediateLocked();
        return true;
    }

    if (state_ == State::Queued)
        return false;

    // Running: coalesce into one immediate next-run request.
    pendingImmediate_ = true;
    pendingDelayed_ = false;
    return true;
}
```

- [ ] **Step 6: Rewrite `scheduleAfter()` in `FileCacheScheduler.cpp`**

```cpp
bool FileCacheScheduledTask::scheduleAfter(uint64_t delayMs)
{
    std::unique_lock<std::mutex> lock(scheduleMutex_);

    if (state_ == State::Deactivated)
        return false;

    if (state_ == State::Idle || state_ == State::Delayed)
    {
        cancelTimerLocked();
        armTimerLocked(lock, delayMs);
        return true;
    }

    if (state_ == State::Queued)
        return false;

    // Running
    if (pendingImmediate_)
        return false;

    pendingDelayed_ = true;
    pendingDelayMs_ = delayMs;
    return true;
}
```

- [ ] **Step 7: Rewrite `deactivate()` — execMutex_ then scheduleMutex_**

Lock order: `execMutex_` first (blocks until any running callback finishes), then `scheduleMutex_`.

```cpp
void FileCacheScheduledTask::deactivate()
{
    // Acquire execMutex_ first: if a callback is running this blocks until it
    // returns (the drain). execMutex_ is never held while a callback runs, so no
    // CV or in-flight flag is needed.
    std::lock_guard<std::mutex> elock(execMutex_);
    std::lock_guard<std::mutex> slock(scheduleMutex_);

    if (state_ == State::Deactivated)
        return;

    cancelTimerLocked();
    state_ = State::Deactivated;
    pendingImmediate_ = false;
    pendingDelayed_ = false;
}
```

- [ ] **Step 8: Rewrite `runCallback()` — execMutex_ serializes, scheduleMutex_ for state**

```cpp
void FileCacheScheduledTask::runCallback()
{
    // Acquire execMutex_ to serialize execution. deactivate() also acquires it,
    // so it drains any running callback automatically.
    std::lock_guard<std::mutex> elock(execMutex_);

    {
        std::lock_guard<std::mutex> slock(scheduleMutex_);
        if (state_ == State::Deactivated)
            return;
        state_ = State::Running;
        pendingImmediate_ = false;
        pendingDelayed_ = false;
    }

    try
    {
        callback_();
    }
    catch (...)
    {
        LOG_ERROR(
            getLogger("FileCacheScheduler"),
            "Task '{}' callback threw: {}",
            name_,
            getCurrentExceptionMessage(true));
    }

    std::unique_lock<std::mutex> slock(scheduleMutex_);

    if (state_ == State::Deactivated)
        return;

    if (pendingImmediate_)
    {
        pendingImmediate_ = false;
        pendingDelayed_ = false;
        queueImmediateLocked();
    }
    else if (pendingDelayed_)
    {
        pendingDelayed_ = false;
        const uint64_t delayMs = pendingDelayMs_;
        armTimerLocked(slock, delayMs);
    }
    else
    {
        state_ = State::Idle;
    }
}
```

- [ ] **Step 9: Rewrite `armTimerLocked()` — attach outside the lock, publish only if still current**

The key algorithm to survive inline completion **and** concurrent supersession:

```cpp
void FileCacheScheduledTask::armTimerLocked(
    std::unique_lock<std::mutex> & lock, uint64_t delayMs)
{
    // Phase 1: publish Delayed state and snapshot the generation under
    // scheduleMutex_ (held on entry).
    state_ = State::Delayed;
    const uint64_t gen = generation_;
    std::weak_ptr<FileCacheScheduledTask> weakSelf = weak_from_this();

    // Arm the Timekeeper timer while still holding scheduleMutex_. This only
    // starts the timer; no continuation runs yet.
    auto sf = scheduler_.timekeeper_->after(std::chrono::milliseconds(delayMs));

    // Phase 2: release scheduleMutex_ BEFORE attaching .thenValue(). If the
    // promise is already fulfilled (delayMs == 0, or a concurrent advance()),
    // folly runs the continuation INLINE on this thread; with the lock released
    // it re-locks a free scheduleMutex_ instead of self-deadlocking.
    lock.unlock();

    auto future = std::move(sf)
        .toUnsafeFuture()
        .thenValue(
            [weakSelf, gen](folly::Unit)
            {
                auto self = weakSelf.lock();
                if (!self)
                    return;
                std::lock_guard<std::mutex> slock(self->scheduleMutex_);
                if (gen != self->generation_ || self->state_ != State::Delayed)
                    return; // superseded by schedule()/scheduleAfter()/deactivate()
                self->queueImmediateLocked();
            });

    // Phase 3: reacquire scheduleMutex_ and publish the handle ONLY if this timer
    // is still the current one. If the generation moved while we were unlocked (a
    // concurrent schedule()/scheduleAfter()/deactivate(), or an inline run that
    // already advanced us to Queued), do NOT overwrite timerFuture_: that would
    // clobber a newer live timer handle with this now-stale one, so a later
    // cancelTimerLocked() would cancel the wrong (already-completed) future and
    // leak the real timer. The dropped `future` is harmless -- its continuation
    // no-ops on the generation check. armTimerLocked always returns with `lock`
    // HELD, so scheduleAfter()/runCallback() resume with a valid lock.
    lock.lock();
    if (gen == generation_ && state_ == State::Delayed)
        timerFuture_ = std::move(future);
}
```

- [ ] **Step 10: Rewrite `queueImmediateLocked()` — update lock type in worker closure**

```cpp
void FileCacheScheduledTask::queueImmediateLocked()
{
    state_ = State::Queued;
    const uint64_t gen = generation_;
    std::weak_ptr<FileCacheScheduledTask> weakSelf = weak_from_this();
    scheduler_.workerPool_.schedule(
        [weakSelf, gen]
        {
            auto self = weakSelf.lock();
            if (!self)
                return;
            {
                std::lock_guard<std::mutex> lock(self->scheduleMutex_);
                if (gen != self->generation_ || self->state_ != State::Queued)
                    return;
            }
            self->runCallback();
        });
}
```

- [ ] **Step 11: Rewrite `cancelTimerLocked()`**

No change to logic, just ensure it's called under `scheduleMutex_` (already the case):

```cpp
void FileCacheScheduledTask::cancelTimerLocked()
{
    if (timerFuture_.valid())
    {
        timerFuture_.cancel();
        timerFuture_ = folly::Future<folly::Unit>::makeEmpty();
    }
    ++generation_;
}
```

- [ ] **Step 12: Delete the old CV / `callbackInFlight_` machinery**

Remove every use of the members deleted in Step 2: the `cv_.wait(lock, ...)` drain in the old
`deactivate()`, the `cv_.notify_all()` in the old `runCallback()`, and both `callbackInFlight_`
reads/writes. The two-lock design drains via `execMutex_`, so none of these remain. After this
step a grep for `cv_`, `callbackInFlight_`, `recursive_mutex`,
`condition_variable_any`, or `condition_variable` in
`FileCacheScheduler.{h,cpp}` must return nothing. Do not grep for the generic
`mutex_` identifier: the separate `FileCacheScheduler` owner class legitimately
retains its own plain mutex for `createTask`/`shutdown`.

- [ ] **Step 13: Add scheduler + caller-id tests in `SchedulerAndScopeTest.cpp`**

Reuse the existing `TestScheduler` RAII struct (do NOT add a parallel gtest fixture — every test
in this file already uses `TestScheduler`), and add the caller-id tests to the existing
`FileCacheQueryIdScopeTest` suite. The existing `DeactivateWaitsForRunningCallback`,
`ScopeSetAndRestoresQueryId`, `NoScopeProducesNonePrefix`, and
`PhysicalTidChangeMakesCallerIdDiffer` tests already cover the running-callback drain and the
`<query-id>:<tid>` / `None:` formats; under the two-lock design they must keep passing, so they
are not duplicated here.

Add one include at the top:

```cpp
#include <folly/system/ThreadName.h>
```

Update the pre-existing `CallerIdTest.NoScopeBackgroundId` in
`velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp`; it must no longer
pin the obsolete `None:<tid>` format:

```cpp
TEST(CallerIdTest, NoScopeBackgroundId)
{
    const auto id = FileSegment::getCallerId();
    ASSERT_EQ(id.rfind("None:", 0), 0u);
    const auto firstColon = id.find(':');
    const auto lastColon = id.rfind(':');
    ASSERT_NE(firstColon, lastColon) << "expected None:<name>:<tid>, got " << id;
    EXPECT_EQ(id.substr(lastColon + 1), std::to_string(folly::getOSThreadID()));
}
```

Append these tests:

```cpp
TEST(FileCacheQueryIdScopeTest, CallerIdWithoutScopeHasThreadNameFormat)
{
    // New coverage for Step 1: without a query scope the id is None:<name>:<tid>
    // (three colon-separated fields). Read-only -- it never mutates this thread's
    // name, so it cannot pollute other tests.
    const std::string id = FileCacheQueryIdScope::getCallerId();
    ASSERT_EQ(id.substr(0, 5), "None:");
    const auto firstColon = id.find(':');
    const auto lastColon = id.rfind(':');
    EXPECT_NE(firstColon, lastColon) << "expected None:<name>:<tid>, got " << id;
}

TEST(FileCacheQueryIdScopeTest, NamedThreadAppearsInCallerId)
{
    // Folly has no clean way to restore "no name" once a name is set, so run the
    // whole scenario in a fresh child thread. Its name dies with the thread and
    // the test-runner thread is never mutated (avoids polluting other tests).
    std::string callerId;
    bool nameSet = false;
    std::thread worker([&]
    {
        nameSet = folly::setThreadName("FcTestWorker");
        callerId = FileCacheQueryIdScope::getCallerId();
    });
    worker.join();
    ASSERT_TRUE(nameSet);
    EXPECT_EQ(callerId.substr(0, 5), "None:");
    EXPECT_NE(callerId.find("FcTestWorker"), std::string::npos)
        << "thread name missing from caller id: " << callerId;
}

TEST(FileCacheSchedulerTest, InlineTimerCompletionDoesNotDeadlock)
{
    // ManualTimekeeper::advance() fulfils the timer promise inline on this thread.
    // Under the two plain locks with unlock-before-attach, the inline continuation
    // re-locks a FREE scheduleMutex_ (no self-deadlock). A promise confirms the
    // callback actually executed through the worker queue.
    TestScheduler ts;
    std::promise<void> ran;
    auto ranFuture = ran.get_future();
    std::atomic<int> runs{0};
    auto holder = ts.scheduler.createTask("inline-timer", [&]
    {
        runs.fetch_add(1);
        ran.set_value();
    });

    holder->scheduleAfter(100);
    ts.tk->advance(100ms); // fulfils the timer promise inline on THIS thread
    ASSERT_EQ(ranFuture.wait_for(5s), std::future_status::ready);
    holder->deactivate();
    EXPECT_EQ(runs.load(), 1);
}

TEST(FileCacheSchedulerTest, ConcurrentScheduleAndDeactivateReachDeactivated)
{
    // Stress concurrent schedule() against deactivate(): the two-lock design must
    // neither deadlock nor crash, and deactivate() is terminal -- afterwards both
    // schedule() and scheduleAfter() refuse.
    TestScheduler ts;
    std::atomic<int> runs{0};
    auto holder = ts.scheduler.createTask("concurrent", [&] { runs.fetch_add(1); });

    std::vector<std::thread> threads;
    threads.reserve(10);
    for (int i = 0; i < 10; ++i)
        threads.emplace_back([&] { holder->schedule(); });
    holder->deactivate();
    for (auto & t : threads)
        t.join();

    // Reaching here proves no deadlock. Terminal-state invariant (not a tautology):
    EXPECT_FALSE(holder->schedule());
    EXPECT_FALSE(holder->scheduleAfter(5));
}

TEST(FileCacheSchedulerTest, StaleTimerAfterScheduleIsNoOp)
{
    // scheduleAfter arms a timer; schedule() then supersedes it (generation bump
    // + immediate run). When the superseded timer later fires, its generation
    // check must make it a no-op -- it must not queue a second run. Guards the
    // Step 9 generation check on both the continuation and the reinstall.
    TestScheduler ts;
    std::promise<void> ran;
    auto ranFuture = ran.get_future();
    std::atomic<int> runs{0};
    auto holder = ts.scheduler.createTask("stale-timer", [&]
    {
        if (runs.fetch_add(1) == 0)
            ran.set_value();
    });

    holder->scheduleAfter(50);
    // Established idiom in this file: wait until the timer is registered before
    // advancing (advance() only fires entries already present in the schedule).
    while (ts.tk->numScheduled() == 0)
        std::this_thread::yield();
    holder->schedule(); // supersede: cancel timer (gen bump) + queue immediate
    ASSERT_EQ(ranFuture.wait_for(5s), std::future_status::ready);

    // Fire the now-stale timer deadline: the continuation must no-op on generation.
    ts.tk->advance(50ms);
    holder->deactivate();
    EXPECT_EQ(runs.load(), 1);
}
```

- [ ] **Step 14: Build**

```bash
cd /root/oss/velox/_build/debug && ninja velox_ch_scheduler_test > build_pt4.log 2>&1
```

Expected: exit 0. Then dispatch a `task` subagent to analyze `build_pt4.log` and return a concise
pass/fail summary with any compiler errors.

- [ ] **Step 15: Run tests**

```bash
cd /root/oss/velox/_build/debug && ./velox/ch/Common/tests/velox_ch_scheduler_test > test_pt4.log 2>&1
```

Expected: all existing tests plus the 5 new ones pass (`CallerIdWithoutScopeHasThreadNameFormat`,
`NamedThreadAppearsInCallerId`, `InlineTimerCompletionDoesNotDeadlock`,
`ConcurrentScheduleAndDeactivateReachDeactivated`, `StaleTimerAfterScheduleIsNoOp`). Then dispatch
a `task` subagent to analyze `test_pt4.log` and return a concise pass/fail summary.

- [ ] **Step 16: Mutation RED**

Revert `getCallerId()` to omit `getCurrentThreadName()` (return `"None:" + tid` only). Rebuild,
retest.

Expected: `FileCacheQueryIdScopeTest.NamedThreadAppearsInCallerId` fails. Revert.

- [ ] **Step 17: Stress test (no sleeps, deterministic)**

```bash
cd /root/oss/velox/_build/debug && ./velox/ch/Common/tests/velox_ch_scheduler_test --gtest_repeat=100 > stress_pt4.log 2>&1
```

Expected: exit 0, no deadlock/timeout. Then dispatch a `task` subagent to analyze `stress_pt4.log`
and confirm zero failures across all repeats.

---

### Task 5: Accumulated Build Gate and Receipt

**Files:**
- None created/modified (verification-only task)

**Interfaces:**
- Consumes: all targets from Tasks 1–4

- [ ] **Step 1: Discover all registered velox_ch targets (mono)**

```bash
cd /root/oss/velox/_build/debug && ctest -N -R "velox_ch_" 2>&1 | grep "Test #" | sed 's/.*: //' | sort > velox_ch_targets_mono.txt && cat velox_ch_targets_mono.txt
```

Expected: lists all `velox_ch_*` test targets (at least 16 after Task 1 adds `velox_ch_metrics_snapshot_test`). Current 15 pre-existing targets:
- `velox_ch_chassert_release_probe`
- `velox_ch_chassert_sanitizer_gate_test`
- `velox_ch_common_test`
- `velox_ch_filecache_buffered_input_test`
- `velox_ch_filecache_core_scc_test`
- `velox_ch_filecache_e2e_test`
- `velox_ch_filecache_manager_test`
- `velox_ch_filecache_priority_cursor_test`
- `velox_ch_guards_test`
- `velox_ch_io_test`
- `velox_ch_leaf_types_test`
- `velox_ch_scheduler_test`
- `velox_ch_settings_test`
- `velox_ch_sharded_map_test`
- `velox_ch_threadpool_test`

Plus 1 new: `velox_ch_metrics_snapshot_test`.

- [ ] **Step 2: Build ALL discovered test targets (mono)**

```bash
cd /root/oss/velox/_build/debug && ninja $(cat velox_ch_targets_mono.txt | tr '\n' ' ') > build_all_mono_pt5.log 2>&1
```

Expected: exit 0. Then dispatch a `task` subagent to analyze `build_all_mono_pt5.log` and return a
concise pass/fail summary with any errors.

- [ ] **Step 3: Run CTest (mono)**

```bash
cd /root/oss/velox/_build/debug && ctest -R "velox_ch_" --output-on-failure > ctest_mono_pt5.log 2>&1
```

Expected: all tests pass, zero failures. Then dispatch a `task` subagent to analyze
`ctest_mono_pt5.log` and return a concise summary (counts + any failing tests).

- [ ] **Step 4: Configure non-mono build**

```bash
mkdir -p /root/oss/velox/_build/debug-task017a-nonmono
source /root/oss/velox-helper/env.sh && cmake \
  -DCMAKE_TOOLCHAIN_FILE=/root/oss/gluten/dev/vcpkg/toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DVELOX_GFLAGS_TYPE=static \
  -DVELOX_BUILD_TESTING=ON \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_ENABLE_EXEC=ON \
  -DVELOX_ENABLE_PARQUET=OFF \
  -DVELOX_ENABLE_REMOTE_FUNCTIONS=ON \
  -DVELOX_ENABLE_GROUPED_TESTS=OFF \
  -DVELOX_MONO_LIBRARY=OFF \
  -DVELOX_BUILD_RUNNER=OFF \
  -DVELOX_ENABLE_GEO=OFF \
  -DVELOX_BUILD_MINIMAL=OFF \
  -DVELOX_SIMDJSON_SKIPUTF8VALIDATION=ON \
  -DMAX_HIGH_MEM_JOBS=16 \
  -DMAX_LINK_JOBS=16 \
  -DVELOX_FORCE_COLORED_OUTPUT=ON \
  -G Ninja \
  -S /root/oss/velox \
  -B /root/oss/velox/_build/debug-task017a-nonmono \
  > /root/oss/velox/_build/debug-task017a-nonmono/configure_nonmono_pt5.log 2>&1
```

Expected: exit 0.

- [ ] **Step 5: Discover all registered velox_ch targets (non-mono)**

```bash
cd /root/oss/velox/_build/debug-task017a-nonmono && ctest -N -R "velox_ch_" 2>&1 | grep "Test #" | sed 's/.*: //' | sort > velox_ch_targets_nonmono.txt && cat velox_ch_targets_nonmono.txt
```

Expected: same target set as mono.

- [ ] **Step 6: Build ALL discovered test targets (non-mono)**

```bash
cd /root/oss/velox/_build/debug-task017a-nonmono && ninja $(cat velox_ch_targets_nonmono.txt | tr '\n' ' ') > build_all_nonmono_pt5.log 2>&1
```

Expected: exit 0. Then dispatch a `task` subagent to analyze `build_all_nonmono_pt5.log` and
return a concise pass/fail summary with any errors.

- [ ] **Step 7: Run CTest (non-mono)**

```bash
cd /root/oss/velox/_build/debug-task017a-nonmono && ctest -R "velox_ch_" --output-on-failure > ctest_nonmono_pt5.log 2>&1
```

Expected: all tests pass, zero failures. Then dispatch a `task` subagent to analyze
`ctest_nonmono_pt5.log` and return a concise summary (counts + any failing tests).

- [ ] **Step 8: Verify no disabled/skipped tests**

```bash
grep -ci "DISABLED\|NotRun\|SKIP" /root/oss/velox/_build/debug/ctest_mono_pt5.log /root/oss/velox/_build/debug-task017a-nonmono/ctest_nonmono_pt5.log
```

Expected: 0 for both.

- [ ] **Step 9: Whitespace check**

```bash
cd /root/oss/velox && git diff --check
```

Expected: no trailing whitespace in modified files.

- [ ] **Step 10: Strict placeholder scan**

Scan every file created or modified by Tasks 1–4 for placeholder/stub markers
that must never reach a review. The Controller commits each reviewed subtask, so
scan the full Task-017A commit range rather than the clean working-tree diff:

```bash
cd /root/oss/velox && git diff --name-only 43a9e6f75ffb94be38836b45fd476325665f50be..HEAD -- 'velox/ch/*' | \
  xargs grep -nE 'TODO|FIXME|XXX|HACK|placeholder|PLACEHOLDER|stub|STUB|NotImplemented|not implemented|\bWIP\b|\.\.\.' 2>/dev/null
```

Expected: no matches. Any hit must be resolved (or, if a literal `...` is legitimately part of a
log/string, confirmed by inspection) before the task is considered done. Report the scan result
(clean, or the offending `path:line`) in the completion summary.

- [ ] **Step 11: Worker completion (no commit)**

The worker does **not** stage, commit, or push anything (per EXECUTION_PROTOCOL — the Controller
commits after review). Finish by returning a concise summary: per-task build/test pass/fail (from
the subagent log analyses), the placeholder-scan result, and the two build modes' CTest totals.

---

## Excluded from this plan (017B scope)

Per design §5 and task exclusions:
- Logger implementation (`logger_useful.h` real impl)
- Exception formatting (`getCurrentExceptionMessage`, `tryLogCurrentException`)
- Exception stack output
- Both logger and function-name exception logging overloads
- `LOG_TEST` non-evaluation

These are independently owned by Task 017B.

---

## Cross-References

| Obligation | Owner | Gate |
|------------|-------|------|
| `kFileCacheWriteBytes` propagation to Spark | Task 018 | Gluten metric bridge |
| Real `ConnectorQueryCtx::cancellationToken` supply | Task 018 | Builder `cancellationToken` param after `fileReadOps` |
| Logger/exception real impl | Task 017B | Independent |
| Prometheus reporter | Deferred | Not Task 017A/018 |

---

## Mutation Evidence Matrix

| Task | Behavior | Mutation | Expected RED |
|------|----------|----------|--------------|
| 1 | `CurrentMetrics::add` increments | Replace `fetch_add` with no-op in `CurrentMetrics.cpp` | `AddSubGetRoundTrip` fails |
| 1 | RAII timer records time | Return 0 from timer dtor in `ProfileEvents.cpp` | `TimeIncrementRecordsNonzero` fails |
| 2 | Cache read bytes → global | Comment out `ProfileEvents::increment(CachedReadBufferReadFromCacheBytes, size)` | `CacheReadUpdatesGlobalAndIoStatistics` fails |
| 2 | Cache write bytes → IoStats | Comment out `ioStats_->addCounter(kFileCacheWriteBytes)` | `CacheWriteUpdatesGlobalAndIoStats` fails |
| 2 | Predownload maps to `read`+`prefetch`, NOT `incRawBytesRead` | Add `ioStatistics_->incRawBytesRead(got)` in predownload path | `PredownloadUpdatesReadPrefetchButNotRawBytes` fails (rawBytesRead delta wrong) |
| 2 | Physical-vs-logical clamp | Use post-clamp size for global/query cache/source I/O, or pre-clamp size for `rawBytesRead` | `LastSegmentClampSeparatesPhysicalAndLogicalBytes` fails |
| 2 | Predownload contributes to global source total | Remove `CachedReadBufferReadFromSourceBytes` from predownload | `PredownloadUpdatesReadPrefetchButNotRawBytes` fails |
| 3 | Real token passed to wait | Revert to `folly::CancellationToken{}` | `CancellationDuringSegmentWaitThrows` fails (waiter never throws) |
| 3 | Check before lookup | Remove `nextFileSegmentsBatch` `isCancellationRequested` check | `CancellationBeforeLookupThrows` fails |
| 4 | Thread name in caller ID | Omit `getCurrentThreadName()` | `FileCacheQueryIdScopeTest.NamedThreadAppearsInCallerId` fails |
| 4 | `armTimerLocked` publishes only if current | Race-only supersede-during-unlocked-window coverage | 200× scheduler stress; deterministic mutation deferred until a production test seam is approved |
| 4 | Inline completion under two plain locks | Revert to a single non-recursive `std::mutex` held across `.thenValue()` | `InlineTimerCompletionDoesNotDeadlock` deadlocks/times out |

---

## Acceptance Criteria

1. Every existing `CurrentMetrics` and `ProfileEvents` enum name preserved and compiles.
2. 10 new CH reader events added (exact names: `CachedReadBufferWaitReadBufferMicroseconds`, `CachedReadBufferReadFromSourceMicroseconds`, `CachedReadBufferPredownloadedFromSourceMicroseconds`, `CachedReadBufferReadFromCacheMicroseconds`, `CachedReadBufferCacheWriteMicroseconds`, `CachedReadBufferPredownloadedFromSourceBytes`, `CachedReadBufferPredownloadedBytes`, `CachedReadBufferCreateBufferMicroseconds`, `CachedReadBufferReadFromCacheHits`, `CachedReadBufferReadFromCacheMisses`).
3. Storage arrays in `CurrentMetrics.cpp` and `ProfileEvents.cpp` (not header-local statics).
4. `FileCacheStatsSnapshot` public type in `velox/ch/Common/FileCacheStats.h`: `takeFileCacheStatsSnapshot()` + `operator-` + `kFileCacheWriteBytes`.
5. Same read/write fact updates both global ProfileEvents and query `IoStatistics`/`IoStats`. Physical cache/source bytes before clamp update the matching global byte event and `ssdRead`/`read`; post-clamp logical returned bytes update `incRawBytesRead` exactly once. Predownload source bytes update global source/predownload bytes plus `read` and `prefetch`, but never `incRawBytesRead`; scan time flows via `incTotalScanTimeNs`.
6. `FileCacheBufferedInput` ctor: `folly::CancellationToken cancellationToken = {}` appended after `fileReadOps` to preserve all existing positional calls.
7. `FileSegment::wait` receives the stored token (not empty).
8. Cancellation only at safe checkpoints (no check while downloader/reserve-write active).
9. No raw `ConnectorQueryCtx*` in any FileCache code.
10. Caller ID: `<query-id>:<os-tid>` with query scope, `None:<thread-name>:<os-tid>` without.
11. The `FileCacheScheduledTask` class declares exactly two plain `std::mutex` (`execMutex_` + `scheduleMutex_`) and no `recursive_mutex`, `condition_variable`/`condition_variable_any`, or `callbackInFlight_`. The separate `FileCacheScheduler` owner class retains its existing plain mutex. The Folly timer continuation is attached outside `scheduleMutex_` and the timer handle is reinstalled only when the generation is still current. Lock order: `execMutex_` → `scheduleMutex_`.
12. `weak_ptr + generation` lifetime pattern retained.
13. Both mono and non-mono builds: all `velox_ch_*` targets dynamically discovered via `ctest -N -R`, freshly built, and pass CTest.
14. No disabled/skipped/comment-only tests. No C++ sleeps.
15. Every deterministic material behavior has a buildable mutation RED. The
    supersede-during-unlocked-window generation guard is race-only and has 200×
    stress evidence; do not add a production `TestValue` seam before Review 5
    disposes the pending production-test-util decision.
16. Worker does not stage/commit; Controller commits after task review per EXECUTION_PROTOCOL.
