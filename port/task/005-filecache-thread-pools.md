# Task 005: Add `FileCacheWorkerPool`, `FileCacheWorker`, and `FileCacheThreadPool`

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes one
> result file under this ClickHouse checkout. Do not modify any ClickHouse source
> files outside `port/task/result/`. Do not commit or stage either repository.

## Whole-port review registration (2026-07-20)

SD6 — this task's thread pool replaces CH `ThreadPoolImpl` (job-priority-queue +
worker list + 2 CVs) and `ThreadFromGlobalPool` with a `folly` executor +
`MeteredExecutor` + backlog deque + futures. The post-Task-010 whole-port review
classified this as a **forced platform remap** (no CH `GlobalThreadPool` in
Velox); consumer guarantees (throw-on-full, `wait` barrier over in-flight +
backlog, join-rethrow, destroy-joinable abort, completion-lambda draining) were
verified preserved. **No implementation change required.** Authoritative record:
`port/task/fullreview/root-oss/1/003-010-review-decisions.md` (§4 SD6, approved).

## Goal

Implement the two-layer thread-pool infrastructure needed by every background
`FileCache` operation:

```text
FileCacheWorkerPool   — process-level shared dynamic executor (folly::CPUThreadPoolExecutor)
FileCacheWorker       — long-running task handle with join / exception propagation
FileCacheThreadPool   — per-cache logical pool; submits short tasks, collects exceptions, wait
```

Provide CH-compatible aliases:

```cpp
using ThreadFromGlobalPool = FileCacheWorker;
using ThreadPool = FileCacheThreadPool;
```

Deliverable: `velox/ch/Common/ThreadPool.h/.cpp` and a focused test executable
`velox_ch_threadpool_test`.

## Controller amendment after Worker attempt 1

This amendment overrides the conflicting literal class, implementation, and
test blocks below:

```text
FileCacheThreadPool must enforce its maxThreads argument as a per-cache
concurrency limit. Reuse folly::MeteredExecutor as the dispatch conduit over
FileCacheWorkerPool's shared executor, but do not treat maxInQueue as a running
task limit: Folly admits its next queue item before the current task body
finishes. Keep excess local tasks in a logical admission backlog until a
running task completes so they never block physical worker threads. queueSize
continues to bound all pending tasks owned by the logical pool.

Add a focused test with a shared physical pool larger than the logical pool.
Hold the first maxThreads tasks at a gate and prove an additional task cannot
start before the gate is released, then prove all tasks complete.

FileCacheWorker move-assignment must reject overwriting a joinable target,
preserving the same no-silent-background-task-leak invariant as its destructor.
Add focused success and death-path tests.
```

Do not defer these invariants to `FileCacheManager`; later capacity budgeting
depends on each logical pool enforcing its own `maxThreads`.

## Starting point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected HEAD:    Task 004 completed (StatusFile + Guards added)
```

Do not require a clean worktree but do not overwrite unrelated changes. Stop
if the branch is not `filecache`.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/1-dependencies/04-filecache-thread-pool-design.md
<clickhouse_repo>/port/task/result/004-filecache-status-and-guards-result.md
```

## File scope

Modify:

```text
<velox_repo>/velox/ch/Common/CMakeLists.txt
<velox_repo>/velox/ch/Common/tests/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/Common/ThreadPool.h
<velox_repo>/velox/ch/Common/ThreadPool.cpp
<velox_repo>/velox/ch/Common/tests/ThreadPoolTest.cpp
<clickhouse_repo>/port/task/result/005-filecache-thread-pools-result.md
```

Every new Velox C++ and CMake file must begin with the Apache 2.0 license
header in the form used by the repository (`/* ... */` for C++, `#` for CMake).

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

Expected: branch is `filecache`, HEAD is the Task 004 commit or a descendant.
Record all pre-existing dirty files in the result file.

- [ ] **Step 2: Add a failing focused test**

Add `ThreadPoolTest.cpp` to the existing test directory for `velox_ch_common_test`.

Append to `velox/ch/Common/tests/CMakeLists.txt` — do not replace the existing
content, just add a second executable:

```cmake
add_executable(velox_ch_threadpool_test ThreadPoolTest.cpp)
add_test(velox_ch_threadpool_test velox_ch_threadpool_test)

target_link_libraries(
  velox_ch_threadpool_test
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

Create `velox/ch/Common/tests/ThreadPoolTest.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/ch/Common/ThreadPool.h"
#include "velox/common/base/Exceptions.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace facebook::velox::ch
{
namespace
{

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// FileCacheWorkerPool helpers
// ---------------------------------------------------------------------------

static FileCacheWorkerPool makePool(
    size_t maxThreads = 4,
    size_t minThreads = 1,
    const std::string & prefix = "test-pool")
{
    return FileCacheWorkerPool(maxThreads, minThreads, prefix);
}

// ---------------------------------------------------------------------------
// FileCacheWorker tests
// ---------------------------------------------------------------------------

TEST(FileCacheWorkerTest, AliasIsFileCacheWorker)
{
    static_assert(std::is_same_v<ThreadFromGlobalPool, FileCacheWorker>);
}

TEST(FileCacheWorkerTest, WorkerRunsFunctionAndIsJoinable)
{
    auto pool = makePool();
    std::promise<void> done;
    auto doneFuture = done.get_future();

    FileCacheWorker worker(pool, [&done]
    {
        done.set_value();
    });

    EXPECT_TRUE(worker.joinable());
    ASSERT_EQ(doneFuture.wait_for(5s), std::future_status::ready);
    worker.join();
    EXPECT_FALSE(worker.joinable());
}

TEST(FileCacheWorkerTest, JoinPropagatesException)
{
    auto pool = makePool();
    std::promise<void> started;
    auto startedFuture = started.get_future();

    FileCacheWorker worker(pool, [&started]
    {
        started.set_value();
        throw std::runtime_error("worker error");
    });

    startedFuture.get();
    EXPECT_THROW(worker.join(), std::runtime_error);
}

TEST(FileCacheWorkerTest, MoveTransfersOwnership)
{
    auto pool = makePool();
    std::promise<void> done;
    auto doneFuture = done.get_future();

    FileCacheWorker w1(pool, [&done] { done.set_value(); });
    FileCacheWorker w2 = std::move(w1);

    EXPECT_FALSE(w1.joinable()); // NOLINT: intentional move-from use
    EXPECT_TRUE(w2.joinable());

    doneFuture.get();
    w2.join();
}

TEST(FileCacheWorkerTest, DestructorOnUnjoinedWorkerChecks)
{
    auto pool = makePool();
    std::promise<void> done;
    auto doneFuture = done.get_future();

    auto * worker = new FileCacheWorker(pool, [&done] { done.set_value(); });
    doneFuture.get(); // let function finish so the issue is only about join

    // The destructor is noexcept like std::thread; an unjoined worker is fatal.
    EXPECT_DEATH({ delete worker; }, "");
}

// ---------------------------------------------------------------------------
// FileCacheWorkerPool tests
// ---------------------------------------------------------------------------

TEST(FileCacheWorkerPoolTest, DynamicPoolStartsWorkersOnDemand)
{
    FileCacheWorkerPool pool(4, 1, "dynamic-pool");
    std::atomic<int> counter{0};
    std::vector<FileCacheWorker> workers;

    for (int i = 0; i < 4; ++i)
    {
        workers.emplace_back(pool, [&counter]
        {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    for (auto & w : workers)
        w.join();

    EXPECT_EQ(counter.load(), 4);
}

TEST(FileCacheWorkerPoolTest, SetNumThreadsGrows)
{
    FileCacheWorkerPool pool(2, 1, "resize-pool");
    pool.setNumThreads(8);

    std::atomic<int> counter{0};
    std::vector<FileCacheWorker> workers;
    for (int i = 0; i < 8; ++i)
        workers.emplace_back(pool, [&counter] { counter.fetch_add(1); });
    for (auto & w : workers)
        w.join();
    EXPECT_EQ(counter.load(), 8);
}

// ---------------------------------------------------------------------------
// FileCacheThreadPool tests
// ---------------------------------------------------------------------------

TEST(FileCacheThreadPoolTest, AliasIsFileCacheThreadPool)
{
    static_assert(std::is_same_v<ThreadPool, FileCacheThreadPool>);
}

TEST(FileCacheThreadPoolTest, ScheduleAndWaitCompletesAllTasks)
{
    FileCacheWorkerPool workerPool(4, 1, "logical-pool");
    FileCacheThreadPool pool(workerPool, /*maxThreads=*/4, /*queueSize=*/16);

    std::atomic<int> counter{0};
    for (int i = 0; i < 8; ++i)
    {
        pool.scheduleOrThrowOnError([&counter]
        {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.wait();
    EXPECT_EQ(counter.load(), 8);
}

TEST(FileCacheThreadPoolTest, WaitRethrowsFirstException)
{
    FileCacheWorkerPool workerPool(4, 1, "exc-pool");
    FileCacheThreadPool pool(workerPool, 4, 16);

    pool.scheduleOrThrowOnError([]
    {
        throw std::runtime_error("pool task error");
    });

    EXPECT_THROW(pool.wait(), std::runtime_error);
}

TEST(FileCacheThreadPoolTest, LocalConcurrencyRespectedByMetadataWorkers)
{
    // Simulate metadata listing: up to maxThreads workers blocked on a barrier.
    // All must run concurrently (no starvation) when pool is large enough.
    constexpr int kMetadataThreads = 4;
    FileCacheWorkerPool workerPool(kMetadataThreads + 2, 1, "meta-pool");
    FileCacheThreadPool pool(workerPool, kMetadataThreads, kMetadataThreads * 2);

    std::promise<void> barrier;
    auto barrierFuture = barrier.get_future().share();
    std::atomic<int> atBarrier{0};
    std::promise<void> allAtBarrier;
    auto allAtBarrierFuture = allAtBarrier.get_future();

    for (int i = 0; i < kMetadataThreads; ++i)
    {
        pool.scheduleOrThrowOnError([&atBarrier, &allAtBarrier, barrierFuture, kMetadataThreads]() mutable
        {
            if (atBarrier.fetch_add(1) + 1 == kMetadataThreads)
                allAtBarrier.set_value();
            barrierFuture.wait();
        });
    }

    ASSERT_EQ(allAtBarrierFuture.wait_for(5s), std::future_status::ready);
    barrier.set_value();
    pool.wait();
    EXPECT_EQ(atBarrier.load(), kMetadataThreads);
}

TEST(FileCacheThreadPoolTest, SafeShrinkPreconditionDocumented)
{
    // shrink (setNumThreads on worker pool) must only happen after all tasks
    // using the old capacity have been joined / wait()ed.  This test verifies
    // that setNumThreads on the backing FileCacheWorkerPool is safe to call
    // after wait().
    FileCacheWorkerPool workerPool(8, 1, "shrink-pool");
    FileCacheThreadPool pool(workerPool, 8, 32);

    std::atomic<int> counter{0};
    for (int i = 0; i < 8; ++i)
        pool.scheduleOrThrowOnError([&counter] { counter.fetch_add(1); });
    pool.wait();

    // Safe to shrink the backing executor now that all tasks are done.
    workerPool.setNumThreads(2);
    EXPECT_EQ(counter.load(), 8);
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 3: Verify the test fails before implementation**

Reconfigure:

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`. For `home-chang`, also add
`-DVELOX_BUILD_TESTING=ON` (already present in the `root-oss` effective
configuration). Redirect output to `<velox_build_dir>/configure_task_005_threadpool.log`.

Try to build:

```bash
if <ninja> \
  -C <velox_build_dir> \
  velox_ch_threadpool_test \
  > <velox_build_dir>/build_task_005_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected: configure succeeds; build fails because `ThreadPool.h` does not exist.

- [ ] **Step 4: Implement `ThreadPool.h`**

Create `velox/ch/Common/ThreadPool.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "velox/common/base/Exceptions.h"

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace facebook::velox::ch
{

class FileCacheWorkerPool;

/// Handle for a long-running task submitted to a `FileCacheWorkerPool`.
///
/// Semantics mirror `ThreadFromGlobalPool` in ClickHouse:
/// - Constructed with a pool reference and a callable; the callable is
///   dispatched immediately to the pool's executor.
/// - `join` blocks until the callable returns and rethrows any exception.
/// - Destroying a joinable worker triggers `VELOX_CHECK` to prevent silent
///   background-task leaks.
/// - Move-only; the moved-from object reports `joinable() == false`.
class FileCacheWorker
{
public:
    using Function = std::function<void()>;

    FileCacheWorker() = default;

    FileCacheWorker(FileCacheWorkerPool & pool, Function function);

    FileCacheWorker(FileCacheWorker && other) noexcept;
    FileCacheWorker & operator=(FileCacheWorker && other) noexcept;

    FileCacheWorker(const FileCacheWorker &) = delete;
    FileCacheWorker & operator=(const FileCacheWorker &) = delete;

    /// Destroys the handle. Triggers VELOX_CHECK if still joinable.
    ~FileCacheWorker();

    /// Blocks until the function returns; rethrows any exception.
    /// Calling join on an unjoinable worker is a no-op.
    void join();

    /// True until join() has been called (or the worker was move-constructed).
    bool joinable() const;

private:
    struct State
    {
        folly::Promise<folly::Unit> finished;
        folly::SemiFuture<folly::Unit> future = finished.getSemiFuture();
        std::exception_ptr exception;
        std::atomic_bool joined{false};
    };

    std::shared_ptr<State> state_;
};

/// CH alias: `using ThreadFromGlobalPool = FileCacheWorker`
using ThreadFromGlobalPool = FileCacheWorker;

/// Process-level shared dynamic executor (analogous to `GlobalThreadPool` in
/// ClickHouse).  Owned by `FileCacheManager`; shared across all `FileCache`
/// instances and their logical `FileCacheThreadPool`s.
///
/// Capacity contract:
/// - `maxThreads` encodes the conservative budget computed by
///   `FileCacheManager` from all registered caches.
/// - Idle workers retire after the Folly default timeout (≈ 60 s).
/// - `setNumThreads` grows or shrinks; must only shrink after all tasks
///   needing the current capacity have joined or wait()ed.
class FileCacheWorkerPool
{
public:
    FileCacheWorkerPool(
        size_t maxThreads,
        size_t minThreads,
        std::string threadNamePrefix);

    FileCacheWorkerPool(const FileCacheWorkerPool &) = delete;
    FileCacheWorkerPool & operator=(const FileCacheWorkerPool &) = delete;

    /// Dispatch a long-running callable and return its handle.
    FileCacheWorker startThread(FileCacheWorker::Function function);

    /// Dispatch a short task; returns a SemiFuture that resolves on completion.
    folly::SemiFuture<folly::Unit> schedule(std::function<void()> task);

    /// Stop the underlying executor (waits for running tasks to finish).
    void shutdown();

    /// Adjust the maximum number of threads (thread-safe per Folly contract).
    void setNumThreads(size_t threads);

private:
    folly::CPUThreadPoolExecutor executor_;

    friend class FileCacheWorker;
};

/// Per-cache logical pool (analogous to `ThreadPoolImpl<ThreadFromGlobalPool>`
/// in ClickHouse).  Submits bounded short tasks to the shared
/// `FileCacheWorkerPool`, tracks pending futures, and rethrows the first
/// exception on `wait`.
///
/// No OS threads are owned; all physical execution is via the shared pool.
class FileCacheThreadPool
{
public:
    FileCacheThreadPool(
        FileCacheWorkerPool & workerPool,
        size_t maxThreads,
        size_t queueSize);

    FileCacheThreadPool(const FileCacheThreadPool &) = delete;
    FileCacheThreadPool & operator=(const FileCacheThreadPool &) = delete;

    /// Dispatch a task. Throws `VeloxRuntimeError` immediately if the queue is
    /// at `queueSize` capacity.
    void scheduleOrThrowOnError(std::function<void()> task);

    /// Block until all previously submitted tasks complete.
    /// Rethrows the first collected exception (if any).
    void wait();

private:
    FileCacheWorkerPool & workerPool_;
    const size_t maxThreads_;
    const size_t queueSize_;

    std::mutex mutex_;
    std::vector<folly::SemiFuture<folly::Unit>> pending_;
};

/// CH alias: `using ThreadPool = FileCacheThreadPool`
using ThreadPool = FileCacheThreadPool;

} // namespace facebook::velox::ch
```

- [ ] **Step 5: Implement `ThreadPool.cpp`**

Create `velox/ch/Common/ThreadPool.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/ch/Common/ThreadPool.h"
#include "velox/ch/Common/FileCacheException.h"

#include <folly/futures/Future.h>

#include <exception>
#include <utility>

namespace facebook::velox::ch
{

// ---------------------------------------------------------------------------
// FileCacheWorker
// ---------------------------------------------------------------------------

FileCacheWorker::FileCacheWorker(FileCacheWorkerPool & pool, Function function)
    : state_(std::make_shared<State>())
{
    auto state = state_;
    pool.executor_.add(
        [state, func = std::move(function)]() mutable
        {
            try
            {
                func();
                state->finished.setValue(folly::Unit{});
            }
            catch (...)
            {
                state->exception = std::current_exception();
                state->finished.setValue(folly::Unit{});
            }
        });
}

FileCacheWorker::FileCacheWorker(FileCacheWorker && other) noexcept
    : state_(std::move(other.state_))
{
}

FileCacheWorker & FileCacheWorker::operator=(FileCacheWorker && other) noexcept
{
    if (this != &other)
        state_ = std::move(other.state_);
    return *this;
}

FileCacheWorker::~FileCacheWorker()
{
    VELOX_CHECK(
        !joinable(),
        "FileCacheWorker destroyed without join(); "
        "this leaks a background task. "
        "Call join() before destroying the worker.");
}

void FileCacheWorker::join()
{
    if (!state_ || state_->joined.exchange(true, std::memory_order_acq_rel))
        return;

    std::move(state_->future).get();

    if (state_->exception)
        std::rethrow_exception(state_->exception);

    state_.reset();
}

bool FileCacheWorker::joinable() const
{
    return state_ && !state_->joined.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// FileCacheWorkerPool
// ---------------------------------------------------------------------------

FileCacheWorkerPool::FileCacheWorkerPool(
    size_t maxThreads,
    size_t minThreads,
    std::string threadNamePrefix)
    : executor_(
          std::make_pair(maxThreads, minThreads),
          std::make_shared<folly::NamedThreadFactory>(std::move(threadNamePrefix)))
{
}

FileCacheWorker FileCacheWorkerPool::startThread(FileCacheWorker::Function function)
{
    return FileCacheWorker(*this, std::move(function));
}

folly::SemiFuture<folly::Unit> FileCacheWorkerPool::schedule(
    std::function<void()> task)
{
    auto [promise, future] = folly::makePromiseContract<folly::Unit>();
    auto sharedPromise = std::make_shared<folly::Promise<folly::Unit>>(
        std::move(promise));

    executor_.add(
        [t = std::move(task), p = std::move(sharedPromise)]() mutable
        {
            try
            {
                t();
                p->setValue(folly::Unit{});
            }
            catch (...)
            {
                p->setException(std::current_exception());
            }
        });

    return std::move(future);
}

void FileCacheWorkerPool::shutdown()
{
    executor_.stop();
    executor_.join();
}

void FileCacheWorkerPool::setNumThreads(size_t threads)
{
    executor_.setNumThreads(threads);
}

// ---------------------------------------------------------------------------
// FileCacheThreadPool
// ---------------------------------------------------------------------------

FileCacheThreadPool::FileCacheThreadPool(
    FileCacheWorkerPool & workerPool,
    size_t maxThreads,
    size_t queueSize)
    : workerPool_(workerPool)
    , maxThreads_(maxThreads)
    , queueSize_(queueSize)
{
}

void FileCacheThreadPool::scheduleOrThrowOnError(std::function<void()> task)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.size() >= queueSize_)
        throwFileCacheException(
            "FileCacheThreadPool queue full ({} / {})", pending_.size(), queueSize_);

    pending_.push_back(workerPool_.schedule(std::move(task)));
}

void FileCacheThreadPool::wait()
{
    std::vector<folly::SemiFuture<folly::Unit>> toWait;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        toWait.swap(pending_);
    }

    std::exception_ptr firstException;
    for (auto & future : toWait)
    {
        try
        {
            std::move(future).get();
        }
        catch (...)
        {
            if (!firstException)
                firstException = std::current_exception();
        }
    }

    if (firstException)
        std::rethrow_exception(firstException);
}

} // namespace facebook::velox::ch
```

- [ ] **Step 6: Register the new sources in `CMakeLists.txt`**

Replace `velox/ch/Common/CMakeLists.txt` with the current content of that
file, adding `ThreadPool.h` to HEADERS and `ThreadPool.cpp` to sources.  The
file will look like:

```cmake
# (license header)

velox_add_library(
  velox_ch_filecache
  StatusFile.cpp
  ThreadPool.cpp
  HEADERS
    ClickHouseAliases.h
    CurrentMetrics.h
    FailPoint.h
    FileCacheBoundedQueue.h
    FileCacheException.h
    FileCacheFilesystem.h
    FilesystemCacheLog.h
    logger_useful.h
    OpenTelemetryTraceContext.h
    ProfileEvents.h
    QueryStatus.h
    SharedMutex.h
    StatusFile.h
    ThreadPool.h
)

if(NOT VELOX_MONO_LIBRARY)
  target_link_libraries(velox_ch_filecache PRIVATE Folly::folly)
endif()

if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

- [ ] **Step 7: Build the focused test**

Reconfigure using the same command as Step 3, then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_threadpool_test \
  > <velox_build_dir>/build_task_005_threadpool.log 2>&1
```

Expected: exit code 0. Do not add `-j`.

- [ ] **Step 8: Run the focused test**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_threadpool_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_005_threadpool.log 2>&1
```

Expected: `100% tests passed, 0 tests failed.`

- [ ] **Step 9: Regression check — previous test still passes**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^(velox_ch_common_test|velox_ch_guards_test)$' \
  --output-on-failure \
  >> <velox_build_dir>/test_task_005_threadpool.log 2>&1
```

Expected: both tests pass.

- [ ] **Step 10: Inspect only task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Common/ThreadPool.h \
  velox/ch/Common/ThreadPool.cpp \
  velox/ch/Common/tests/CMakeLists.txt \
  velox/ch/Common/tests/ThreadPoolTest.cpp
```

Expected: no whitespace errors; no files outside the declared scope changed;
changes remain unstaged.

- [ ] **Step 11: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/005-filecache-thread-pools-result.md
```

Use exactly this structure:

````markdown
# Task 005 Result: Add `FileCacheWorkerPool`, `FileCacheWorker`, `FileCacheThreadPool`

## Status

status: success

## Velox status

```text
<paste branch, HEAD, and final `git status --short`>
```

## Files changed

```text
<list only task-owned files>
```

## Commands run

```text
<paste configure, build, test, and verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_task_005_threadpool.log
<velox_build_dir>/build_task_005_red.log
<velox_build_dir>/build_task_005_threadpool.log
<velox_build_dir>/test_task_005_threadpool.log
```

## Verification

```text
Red build failed because ThreadPool.h was absent.
Final build exit code:
Focused test result:
Regression test result:
git diff --check result:
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 006: implement FileCacheScheduler and FileCacheQueryIdScope.
```
````

## Explicit exclusions

Do not implement in this task:

```text
FileCacheScheduler / BackgroundSchedulePool / BackgroundSchedulePoolTaskHolder
FileCacheQueryIdScope
Business stop flags (stopFlag, download_queue->cancel) — these belong to Metadata
ReadBufferFromVeloxReadFile / WriteBufferFromVeloxWriteFile
FileCache leaf types or algorithms
SipHash128
Gluten integration
```

The `stopFlag` inside `DownloadThread` and the `download_queue`/`cleanup_queue`
cancel logic are part of `Metadata` business protocol and will be implemented
when `Metadata.h/.cpp` is ported in a later task.
