# Task 006: Add `FileCacheScheduler` and `FileCacheQueryIdScope`

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes one
> result file under this ClickHouse checkout. Do not modify any ClickHouse source
> files outside `port/task/result/`. Do not commit or stage either repository.

## Whole-port review registration (2026-07-20)

The post-Task-010 whole-port review recorded three items on this task. Task 006
**remains accepted**; none blocks Tasks 011/012.

- **SD7 (forced remap, no change).** The scheduler replaces CH's 4-boolean state +
  `std::multimap` delayed queue + delay-thread with `enum class State` +
  `folly::Timekeeper` + per-task `Future`. Forced (no CH `BackgroundSchedulePool`
  / Poco `NotificationQueue` in Velox); transitions verified equivalent.
- **SD8 (deferred implementation).** The scheduler uses one `std::recursive_mutex`
  where CH uses two `std::mutex` (`exec_mutex` + `schedule_mutex`), permitting
  re-entrant locking. The interface semantics are correct and tested; this is an
  implementation deviation whose justification is only a code comment
  (`FileCacheScheduler.h` inline-future-under-lock). Deferred: a later task should
  attach the continuation off-lock (`.via(workerPool)` or after releasing the
  lock) and revert to `std::mutex`, matching CH 1:1 — OR register the hard
  constraint with a reentrancy RED + false-green probe and human sign-off.
- **F-CALLERID (deferred, diagnostic only).** `getCallerId` produces
  `None:<tid>` where CH produces `None:<threadname>:<tid>`
  (`FileSegment.cpp:254-259`). Functionally correct — downloader ownership uses
  `caller_id == downloader_id` equality and `tid` is per-thread-unique — so the
  only loss is a thread-name field in diagnostic logs. Deferred with SD8 /
  pre-release: native-map `getThreadName` → `folly::getCurrentThreadName`, restore
  the three-field format, and replace the prefix-only test
  (`SchedulerAndScopeTest.cpp` `NoScopeProducesNonePrefix`, which passes for both
  formats) with a full-structure RED + false-green probe.

Authoritative record: `port/task/fullreview/root-oss/1/003-010-review-decisions.md`
and `port/task/fullview/home-chang/1/003-010-review-decisions.md`.

## Post-acceptance source-contract audit — task reopened

The original Task 006 lifetime fixes remain required. This amendment corrects one
additional scheduling-priority divergence.

CH `BackgroundSchedulePoolTaskInfo::scheduleAfter` never replaces an already
scheduled immediate run with a delayed run. Immediate work has priority:

```text
state == Running and pendingImmediate == true:
  scheduleAfter(delay) returns false
  pendingImmediate remains true
  no delayed request replaces it

state == Running and pendingImmediate == false:
  scheduleAfter(delay) records or overwrites one delayed next run

schedule while a delayed next run is pending:
  cancel the delayed request
  record one immediate next run
```

The corrective Worker must add a deterministic RED test:

1. enter the callback and keep it running with a barrier;
2. call `schedule` from another thread;
3. allow the callback to call `scheduleAfter`;
4. release the callback;
5. prove the next callback runs immediately without advancing the manual clock;
6. prove `scheduleAfter` reported that it did not replace the immediate request.

Retain all attempt-2 weak-ownership, generation, holder-destruction, and
`deactivate` guarantees.

## Goal

Implement two independent `FileCache` infrastructure components:

**`FileCacheScheduler`** — replaces `BackgroundSchedulePool` /
`BackgroundSchedulePoolTaskHolder`. Provides a task state machine
(Idle → Delayed → Queued → Running → Deactivated) backed by a
`folly::Timekeeper` for timer scheduling and the Task 005
`FileCacheWorkerPool` for callback execution.  CH-compatible aliases:

```cpp
using BackgroundSchedulePool = FileCacheScheduler;
using BackgroundSchedulePoolTaskHolder = FileCacheScheduledTaskHolder;
```

**`FileCacheQueryIdScope`** — replaces `CurrentThread` query-id access. Sets a
thread-local query id for the duration of a synchronous operation so that the
argument-free `FileSegment::getCallerId` can compose `"<query-id>:<os-tid>"`.

Deliverables: `FileCacheScheduler.h/.cpp`, `FileCacheQueryIdScope.h/.cpp`, and
a focused test executable `velox_ch_scheduler_test`.

## Controller amendment after Worker attempt 1

This amendment overrides every raw `this` capture and conflicting lifetime
assumption in the literal scheduler algorithms below:

```text
Destroying or move-assigning a FileCacheScheduledTaskHolder must be safe while
its task is Delayed or Queued. It must not require shutting down or draining the
process-wide FileCacheWorkerPool first.

Every worker-pool closure and timer continuation must use lifetime-safe task
ownership, such as a weak_ptr obtained through enable_shared_from_this and
locked before any member access. No asynchronous closure may dereference a
destroyed FileCacheScheduledTask.

deactivate() must still prevent future callbacks and wait for a callback that is
already running. Queued and delayed closures may remain in their executors, but
must become safe no-ops after holder destruction.
```

Add focused tests that:

```text
1. Fill the worker pool, queue a scheduled task, destroy its holder before
   releasing the workers, then drain the pool and prove the callback did not run.
2. Schedule a delayed task, destroy its holder before advancing the
   ManualTimekeeper, advance past the deadline, drain the pool, and prove the
   callback did not run.
```

The final build log must show fresh compilation of both Task 006 implementation
sources and `SchedulerAndScopeTest.cpp`, followed by relinking of the library and
test executable. Worker attempt 1's green-build log compiled only
`FileCacheScheduler.cpp`, so it did not support the receipt's stronger
full-recompile claim.

## Starting point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected HEAD:    Task 005 completed (ThreadPool added)
```

Do not require a clean worktree but do not overwrite unrelated changes. Stop
if the branch is not `filecache`.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/1-dependencies/05-filecache-scheduler-design.md
<clickhouse_repo>/port/1-dependencies/06-filecache-caller-token-design.md
<clickhouse_repo>/port/task/result/005-filecache-thread-pools-result.md
```

## File scope

Modify:

```text
<velox_repo>/velox/ch/Common/CMakeLists.txt
<velox_repo>/velox/ch/Common/tests/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/Common/FileCacheScheduler.h
<velox_repo>/velox/ch/Common/FileCacheScheduler.cpp
<velox_repo>/velox/ch/Common/FileCacheQueryIdScope.h
<velox_repo>/velox/ch/Common/FileCacheQueryIdScope.cpp
<velox_repo>/velox/ch/Common/tests/SchedulerAndScopeTest.cpp
<clickhouse_repo>/port/task/result/006-filecache-scheduler-and-caller-scope-result.md
```

Every new Velox C++ and CMake file must begin with the Apache 2.0 license
header in the repository's comment form.

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

Expected: branch is `filecache`, HEAD is the Task 005 commit or a descendant.
Record all pre-existing dirty files in the result file.

- [ ] **Step 2: Add a failing focused test**

Append to `velox/ch/Common/tests/CMakeLists.txt`:

```cmake
add_executable(velox_ch_scheduler_test SchedulerAndScopeTest.cpp)
add_test(velox_ch_scheduler_test velox_ch_scheduler_test)

target_link_libraries(
  velox_ch_scheduler_test
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

Create `velox/ch/Common/tests/SchedulerAndScopeTest.cpp`:

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

#include "velox/ch/Common/FileCacheQueryIdScope.h"
#include "velox/ch/Common/FileCacheScheduler.h"
#include "velox/ch/Common/ThreadPool.h"
#include "velox/common/base/Exceptions.h"

#include <folly/futures/ManualTimekeeper.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>

namespace facebook::velox::ch
{
namespace
{

using namespace std::chrono_literals;

// Construct a scheduler backed by a ManualTimekeeper and a small worker pool.
struct TestScheduler
{
    std::shared_ptr<folly::ManualTimekeeper> tk =
        std::make_shared<folly::ManualTimekeeper>();
    FileCacheWorkerPool pool{4, 1, "sched-test"};
    FileCacheScheduler scheduler{tk, pool};
};

// ---------------------------------------------------------------------------
// Alias tests
// ---------------------------------------------------------------------------

TEST(FileCacheSchedulerTest, AliasesAreCorrect)
{
    static_assert(std::is_same_v<BackgroundSchedulePool, FileCacheScheduler>);
    static_assert(
        std::is_same_v<
            BackgroundSchedulePoolTaskHolder,
            FileCacheScheduledTaskHolder>);
}

// ---------------------------------------------------------------------------
// schedule / scheduleAfter tests
// ---------------------------------------------------------------------------

TEST(FileCacheSchedulerTest, ScheduleDispatchesExactlyOneCallback)
{
    TestScheduler ts;
    auto holder = ts.scheduler.createTask("test-task", [] {});
    ASSERT_TRUE(static_cast<bool>(holder));

    std::promise<void> ran;
    auto ranFuture = ran.get_future();

    holder->setCallback([&ran]
    {
        ran.set_value();
    });

    holder->schedule();

    ASSERT_EQ(ranFuture.wait_for(5s), std::future_status::ready);
}

TEST(FileCacheSchedulerTest, ScheduleAfterRunsAfterTimekeeperAdvance)
{
    TestScheduler ts;

    std::promise<void> ran;
    auto ranFuture = ran.get_future();

    auto holder = ts.scheduler.createTask("delayed-task", [&ran]
    {
        ran.set_value();
    });

    holder->scheduleAfter(100);  // 100 ms

    // Not yet fired.
    EXPECT_EQ(ranFuture.wait_for(0ms), std::future_status::timeout);

    ts.tk->advance(200ms);

    ASSERT_EQ(ranFuture.wait_for(5s), std::future_status::ready);
}

TEST(FileCacheSchedulerTest, ScheduleAdvancesDelayedTask)
{
    TestScheduler ts;

    std::atomic<int> count{0};
    std::promise<void> firstRan;
    auto firstFuture = firstRan.get_future();

    auto holder = ts.scheduler.createTask("advance-task", [&count, &firstRan]
    {
        if (count.fetch_add(1) == 0)
            firstRan.set_value();
    });

    holder->scheduleAfter(10000); // far future
    // Calling schedule() cancels the pending timer and queues immediately.
    holder->schedule();

    ASSERT_EQ(firstFuture.wait_for(5s), std::future_status::ready);
    EXPECT_GE(count.load(), 1);
}

TEST(FileCacheSchedulerTest, MultipleScheduleCallsCoalesceWhileQueued)
{
    TestScheduler ts;

    // Use a gate to hold the first callback while we fire extra schedule() calls.
    // Track the coalesced second run via a separate promise so we can wait on it.
    std::promise<void> gate;
    auto gateFuture = gate.get_future().share();
    std::atomic<int> count{0};
    std::promise<void> firstRan;
    auto firstFuture = firstRan.get_future();
    std::promise<void> coalescedRan;
    auto coalescedFuture = coalescedRan.get_future();

    auto holder = ts.scheduler.createTask(
        "coalesce-task",
        [&count, &gateFuture, &firstRan, &coalescedRan]() mutable
        {
            const int n = count.fetch_add(1);
            if (n == 0)
            {
                firstRan.set_value();
                gateFuture.get(); // block first run
            }
            else if (n == 1)
            {
                coalescedRan.set_value(); // coalesced second run
            }
        });

    holder->schedule(); // → Queued → Running (first)
    ASSERT_EQ(firstFuture.wait_for(5s), std::future_status::ready);

    // First execution is blocked.  Queue two more schedule() calls; they must
    // coalesce into exactly one pending next-run.
    holder->schedule();
    holder->schedule();

    gate.set_value(); // release first run

    // The coalesced run must fire exactly once.
    ASSERT_EQ(coalescedFuture.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(count.load(), 2);
}

TEST(FileCacheSchedulerTest, SameTaskNeverRunsConcurrently)
{
    TestScheduler ts;

    // Gate the first execution to keep it alive while we fire more schedule()
    // calls; then release it and verify the second run starts only after.
    std::promise<void> firstRunning;
    auto firstRunningFuture = firstRunning.get_future();
    std::promise<void> releaseFirst;
    auto releaseFirstFuture = releaseFirst.get_future().share();
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};
    std::atomic<int> count{0};
    std::promise<void> secondDone;
    auto secondFuture = secondDone.get_future();

    auto holder = ts.scheduler.createTask("no-concurrent", [&]
    {
        const int cur = concurrent.fetch_add(1, std::memory_order_acq_rel) + 1;
        int expected = maxConcurrent.load();
        while (cur > expected && !maxConcurrent.compare_exchange_weak(expected, cur))
        {
        }
        const int run = count.fetch_add(1);
        if (run == 0)
        {
            firstRunning.set_value();
            releaseFirstFuture.get(); // hold until released
        }
        else if (run == 1)
        {
            secondDone.set_value();
        }
        concurrent.fetch_sub(1, std::memory_order_acq_rel);
    });

    holder->schedule();
    ASSERT_EQ(firstRunningFuture.wait_for(5s), std::future_status::ready);

    // First run is alive.  A concurrent execution is impossible by contract;
    // schedule() now queues a pending next-run.
    holder->schedule();

    // Release the first run.
    releaseFirst.set_value();

    // Wait for the second (coalesced) run to complete.
    ASSERT_EQ(secondFuture.wait_for(5s), std::future_status::ready);

    // The two runs never overlapped.
    EXPECT_EQ(maxConcurrent.load(), 1);
}

TEST(FileCacheSchedulerTest, CallbackSelfReschedulesAfter)
{
    TestScheduler ts;
    std::atomic<int> count{0};
    std::promise<void> reachedTwo;
    auto twoFuture = reachedTwo.get_future();

    FileCacheScheduledTask * rawTask = nullptr;
    auto holder = ts.scheduler.createTask("self-reschedule", [&]
    {
        if (count.fetch_add(1) + 1 < 2)
            rawTask->scheduleAfter(10);
        else
            reachedTwo.set_value();
    });
    rawTask = holder.get();

    holder->schedule();
    ts.tk->advance(200ms);

    ASSERT_EQ(twoFuture.wait_for(5s), std::future_status::ready);
    EXPECT_GE(count.load(), 2);
}

TEST(FileCacheSchedulerTest, CallbackExceptionDoesNotLeaveTaskRunning)
{
    TestScheduler ts;

    std::promise<void> afterThrow;
    auto afterFuture = afterThrow.get_future();
    std::atomic<int> count{0};
    std::promise<void> firstStarted;
    auto firstStartedFuture = firstStarted.get_future();

    auto holder = ts.scheduler.createTask("exc-task", [&]
    {
        const int n = count.fetch_add(1);
        if (n == 0)
        {
            firstStarted.set_value(); // signal before throw
            throw std::runtime_error("test exception");
        }
        afterThrow.set_value();
    });

    holder->schedule();

    // Wait until the first invocation has at least started, then schedule again.
    // The task may be in Running (exception not yet caught) or back in Idle;
    // either way a second execution must eventually happen.
    ASSERT_EQ(firstStartedFuture.wait_for(5s), std::future_status::ready);
    holder->schedule();

    ASSERT_EQ(afterFuture.wait_for(5s), std::future_status::ready);
    EXPECT_GE(count.load(), 2);
}

TEST(FileCacheSchedulerTest, DeactivatePreventsQueuedCallbackFromRunning)
{
    TestScheduler ts;

    std::atomic<bool> ran{false};
    auto holder = ts.scheduler.createTask("deactivate-queued", [&ran]
    {
        ran.store(true);
    });

    // Block the pool so the task sits in Queued state.
    std::promise<void> unblock;
    auto unblockFuture = unblock.get_future().share();
    std::vector<FileCacheWorker> blockers;
    for (int i = 0; i < 4; ++i)
        blockers.emplace_back(ts.pool, [unblockFuture]() mutable { unblockFuture.get(); });

    holder->schedule(); // sits in queue since all workers are busy

    holder->deactivate(); // cancel before execution

    unblock.set_value();
    for (auto & w : blockers)
        w.join();

    // After all blocking workers have joined, the stale closure (if it was
    // dequeued) has completed as a no-op, because deactivate() incremented
    // the generation before releasing the workers.
    EXPECT_FALSE(ran.load());
}

TEST(FileCacheSchedulerTest, DeactivateWaitsForRunningCallback)
{
    TestScheduler ts;

    std::promise<void> running;
    auto runningFuture = running.get_future();
    std::promise<void> unblock;
    auto unblockFuture = unblock.get_future();
    std::atomic<bool> completed{false};

    auto holder = ts.scheduler.createTask("deactivate-running", [&]
    {
        running.set_value();
        unblockFuture.wait();
        completed.store(true);
    });

    holder->schedule();
    runningFuture.get(); // callback is now executing

    // deactivate blocks until the running callback returns.
    std::thread deactivator([&holder]
    {
        holder->deactivate();
    });

    unblock.set_value();
    deactivator.join();

    // After deactivate() returns the callback must have completed — deactivate
    // guarantees it waited for the running execution to finish.
    EXPECT_TRUE(completed.load());
}

TEST(FileCacheSchedulerTest, HolderDestructorDeactivatesTask)
{
    TestScheduler ts;

    std::atomic<int> count{0};
    std::promise<void> running;
    auto runningFuture = running.get_future();
    std::promise<void> gate;
    auto gateFuture = gate.get_future();

    auto holderPtr = std::make_unique<FileCacheScheduledTaskHolder>(
        ts.scheduler.createTask("dtor-deactivate", [&]
        {
            running.set_value();
            gateFuture.wait();
            count.fetch_add(1);
        }));

    (*holderPtr)->schedule();
    ASSERT_EQ(runningFuture.wait_for(5s), std::future_status::ready);

    // Destroy the holder from a background thread.  The destructor calls
    // deactivate() which must block until the running callback returns.
    std::thread destroyer([h = std::move(holderPtr)] {});

    gate.set_value(); // release callback
    destroyer.join();

    // Callback ran exactly once; subsequent schedules are impossible.
    EXPECT_EQ(count.load(), 1);
}

TEST(FileCacheSchedulerTest, ShutdownCancelsAllTimersAndWaitsCallbacks)
{
    TestScheduler ts;
    std::atomic<int> count{0};
    std::vector<FileCacheScheduledTaskHolder> holders;

    for (int i = 0; i < 3; ++i)
    {
        holders.push_back(ts.scheduler.createTask(
            "shutdown-task-" + std::to_string(i),
            [&count] { count.fetch_add(1); }));
        holders.back()->scheduleAfter(10000); // far-future timers
    }

    // shutdown must cancel pending timers and return promptly.
    ts.scheduler.shutdown();

    // No callback should have fired (timers were cancelled before advancing tk).
    EXPECT_EQ(count.load(), 0);
}

TEST(FileCacheSchedulerTest, TriggerNowViaScheduleOnDelayedTask)
{
    TestScheduler ts;

    std::promise<void> ran;
    auto ranFuture = ran.get_future();

    auto holder = ts.scheduler.createTask("trigger-now", [&ran]
    {
        ran.set_value();
    });

    holder->scheduleAfter(9999999); // far future
    holder->schedule();             // triggerNow: cancel timer, run immediately

    ASSERT_EQ(ranFuture.wait_for(5s), std::future_status::ready);
}

// ---------------------------------------------------------------------------
// FileCacheQueryIdScope tests
// ---------------------------------------------------------------------------

TEST(FileCacheQueryIdScopeTest, ScopeSetAndRestoresQueryId)
{
    EXPECT_TRUE(FileCacheQueryIdScope::currentQueryId().empty());

    {
        FileCacheQueryIdScope scope("query-abc");
        EXPECT_EQ(FileCacheQueryIdScope::currentQueryId(), "query-abc");

        {
            FileCacheQueryIdScope inner("query-xyz");
            EXPECT_EQ(FileCacheQueryIdScope::currentQueryId(), "query-xyz");
        }

        // inner scope exited; outer restored.
        EXPECT_EQ(FileCacheQueryIdScope::currentQueryId(), "query-abc");
    }

    EXPECT_TRUE(FileCacheQueryIdScope::currentQueryId().empty());
}

TEST(FileCacheQueryIdScopeTest, PhysicalTidChangeMakesCallerIdDiffer)
{
    // Two threads with the same query id must produce different caller ids.
    const std::string queryId = "shared-query";

    std::string tid1, tid2;

    std::thread t1([&]
    {
        FileCacheQueryIdScope scope(queryId);
        tid1 = FileCacheQueryIdScope::getCallerId();
    });

    std::thread t2([&]
    {
        FileCacheQueryIdScope scope(queryId);
        tid2 = FileCacheQueryIdScope::getCallerId();
    });

    t1.join();
    t2.join();

    EXPECT_NE(tid1, tid2);
    // Both must start with the shared query id.
    EXPECT_EQ(tid1.substr(0, queryId.size()), queryId);
    EXPECT_EQ(tid2.substr(0, queryId.size()), queryId);
}

TEST(FileCacheQueryIdScopeTest, NoScopeProducesNonePrefix)
{
    // Without a scope the caller id uses the "None" prefix.
    const std::string callerId = FileCacheQueryIdScope::getCallerId();
    EXPECT_EQ(callerId.substr(0, 5), "None:");
}

TEST(FileCacheQueryIdScopeTest, SameQueryDifferentResumeProducesDifferentCallerId)
{
    // Simulates a Velox driver resuming on a new OS thread after the previous
    // execution released the downloader lease.  The new thread must get a
    // different caller id even though the query id is the same.
    const std::string queryId = "resume-query";

    std::string before, after;

    std::thread t1([&]
    {
        FileCacheQueryIdScope scope(queryId);
        before = FileCacheQueryIdScope::getCallerId();
    });
    t1.join();

    std::thread t2([&]
    {
        FileCacheQueryIdScope scope(queryId);
        after = FileCacheQueryIdScope::getCallerId();
    });
    t2.join();

    // Different physical threads → different os-tid component.
    EXPECT_NE(before, after);
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
configuration). Redirect output to `<velox_build_dir>/configure_task_006_scheduler.log`.

Try to build:

```bash
if <ninja> \
  -C <velox_build_dir> \
  velox_ch_scheduler_test \
  > <velox_build_dir>/build_task_006_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected: configure succeeds; build fails because `FileCacheScheduler.h` and
`FileCacheQueryIdScope.h` do not exist.

- [ ] **Step 4: Implement `FileCacheQueryIdScope.h`**

Create `velox/ch/Common/FileCacheQueryIdScope.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * (license header)
 */

#pragma once

#include <string>
#include <string_view>

namespace facebook::velox::ch
{

/// RAII scope that sets a thread-local query id for the duration of a
/// synchronous operation.
///
/// `FileSegment::getCallerId` composes `"<query-id>:<os-tid>"` by reading
/// `currentQueryId()`.  This mirrors ClickHouse's `CurrentThread` query-id
/// mechanism without requiring explicit parameter threading through all
/// FileSegment APIs.
///
/// Nesting is supported: the destructor restores the previous query id.
/// The scope must not cross an OS-thread scheduling boundary; it only
/// stabilises the identity for one synchronous execution window.
class FileCacheQueryIdScope
{
public:
    explicit FileCacheQueryIdScope(std::string_view queryId);
    ~FileCacheQueryIdScope();

    FileCacheQueryIdScope(const FileCacheQueryIdScope &) = delete;
    FileCacheQueryIdScope & operator=(const FileCacheQueryIdScope &) = delete;

    /// Returns the current thread-local query id, or an empty string_view if
    /// no scope is active.
    static std::string_view currentQueryId();

    /// Returns the caller identity string used by `FileSegment::getCallerId`:
    ///   "<query-id>:<os-tid>"  when a scope is active
    ///   "None:<os-tid>"        otherwise
    static std::string getCallerId();

private:
    std::string previousQueryId_;
};

} // namespace facebook::velox::ch
```

- [ ] **Step 5: Implement `FileCacheQueryIdScope.cpp`**

Create `velox/ch/Common/FileCacheQueryIdScope.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * (license header)
 */

#include "velox/ch/Common/FileCacheQueryIdScope.h"

#include <folly/system/ThreadId.h>

#include <string>
#include <string_view>

namespace facebook::velox::ch
{

namespace
{
// Thread-local storage for the current query id.
thread_local std::string tCurrentQueryId;
} // namespace

FileCacheQueryIdScope::FileCacheQueryIdScope(std::string_view queryId)
    : previousQueryId_(tCurrentQueryId)
{
    tCurrentQueryId.assign(queryId);
}

FileCacheQueryIdScope::~FileCacheQueryIdScope()
{
    tCurrentQueryId = std::move(previousQueryId_);
}

std::string_view FileCacheQueryIdScope::currentQueryId()
{
    return tCurrentQueryId;
}

std::string FileCacheQueryIdScope::getCallerId()
{
    const auto tid = std::to_string(folly::getOSThreadID());
    const auto & qid = tCurrentQueryId;
    if (qid.empty())
        return "None:" + tid;
    return qid + ":" + tid;
}

} // namespace facebook::velox::ch
```

- [ ] **Step 6: Implement `FileCacheScheduler.h`**

Create `velox/ch/Common/FileCacheScheduler.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * (license header)
 */

#pragma once

#include "velox/ch/Common/ThreadPool.h"

#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace folly
{
class Timekeeper;
} // namespace folly

namespace facebook::velox::ch
{

class FileCacheScheduledTask;
class FileCacheScheduler;

/// RAII holder for a `FileCacheScheduledTask`.
///
/// Default-constructible; move-only.  Destructor calls `deactivate()` if the
/// task is still active.  Mirrors `BackgroundSchedulePoolTaskHolder`.
class FileCacheScheduledTaskHolder
{
public:
    FileCacheScheduledTaskHolder() = default;
    explicit FileCacheScheduledTaskHolder(
        std::shared_ptr<FileCacheScheduledTask> task);

    FileCacheScheduledTaskHolder(const FileCacheScheduledTaskHolder &) = delete;
    FileCacheScheduledTaskHolder &
    operator=(const FileCacheScheduledTaskHolder &) = delete;

    FileCacheScheduledTaskHolder(FileCacheScheduledTaskHolder &&) noexcept;
    FileCacheScheduledTaskHolder &
    operator=(FileCacheScheduledTaskHolder &&) noexcept;

    ~FileCacheScheduledTaskHolder();

    explicit operator bool() const;
    FileCacheScheduledTask * operator->();
    const FileCacheScheduledTask * operator->() const;

    FileCacheScheduledTask * get();

private:
    std::shared_ptr<FileCacheScheduledTask> task_;
};

/// A single named task managed by `FileCacheScheduler`.
///
/// State machine:
///   Idle → schedule() → Queued → [running] → Running → [done] → Idle
///   Any → scheduleAfter(ms) → Delayed → [timer fires] → Queued → Running → Idle
///   Any → deactivate() → Deactivated (waits for running callback to return)
///
/// Invariants:
/// - The same task never executes concurrently with itself.
/// - Multiple `schedule()` calls while Queued or Running coalesce into one
///   next-run request.
/// - `deactivate()` blocks until any in-flight callback returns, then prevents
///   all future executions.
class FileCacheScheduledTask
{
public:
    /// Replace the callback (used in tests that need to set it after createTask).
    void setCallback(std::function<void()> callback);

    /// Dispatch for immediate execution.
    /// Returns false if already Deactivated or if the scheduler is shut down.
    bool schedule();

    /// Dispatch for execution after `delayMs` milliseconds.
    /// Returns false if already Deactivated or shut down.
    bool scheduleAfter(uint64_t delayMs);

    /// Prevent all future executions; block until any running callback returns.
    void deactivate();

    const std::string & name() const;

private:
    friend class FileCacheScheduler;

    enum class State : uint8_t
    {
        Idle,
        Delayed,
        Queued,
        Running,
        Deactivated,
    };

    FileCacheScheduledTask(
        std::string name,
        std::function<void()> callback,
        FileCacheScheduler & scheduler);

    // Called by the worker closure when it actually starts executing.
    void runCallback();

    // Cancel the current timer future (if any).  Must be called under mutex_.
    void cancelTimerLocked();

    // Queue one immediate execution on the worker pool.
    // Must be called under mutex_.  Transitions state to Queued.
    void queueImmediateLocked();

    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::string name_;
    std::function<void()> callback_;
    FileCacheScheduler & scheduler_;

    State state_{State::Idle};
    uint64_t generation_{0}; // incremented on cancel/deactivate

    // Pending next-run request accumulated while Running.
    bool pendingImmediate_{false};
    bool pendingDelayed_{false};
    uint64_t pendingDelayMs_{0};

    // Handle for the outstanding delayed future (may be cancelled).
    folly::SemiFuture<folly::Unit> timerFuture_{
        folly::SemiFuture<folly::Unit>::makeEmpty()};
};

/// Scheduler that maps `BackgroundSchedulePool` semantics onto
/// `folly::Timekeeper` (timer) + `FileCacheWorkerPool` (execution).
///
/// All scheduled callbacks execute on the shared worker pool, so long-running
/// callbacks do not block timer delivery.
///
/// Shutdown procedure:
///   1. `scheduler.shutdown()` — deactivates all live tasks and waits.
///   2. `worker_pool.shutdown()` — stops the executor.
class FileCacheScheduler
{
public:
    FileCacheScheduler(
        std::shared_ptr<folly::Timekeeper> timekeeper,
        FileCacheWorkerPool & workerPool);

    ~FileCacheScheduler();

    /// Create a named task.  Returns an empty holder if already shut down.
    FileCacheScheduledTaskHolder createTask(
        std::string name,
        std::function<void()> callback);

    /// Deactivate all live tasks and wait for running callbacks to return.
    /// Subsequent `createTask` calls return empty holders.
    void shutdown();

private:
    friend class FileCacheScheduledTask;

    std::shared_ptr<folly::Timekeeper> timekeeper_;
    FileCacheWorkerPool & workerPool_;

    mutable std::mutex mutex_;
    bool shutdown_{false};
    std::vector<std::weak_ptr<FileCacheScheduledTask>> tasks_;
};

/// CH alias: `using BackgroundSchedulePool = FileCacheScheduler`
using BackgroundSchedulePool = FileCacheScheduler;

/// CH alias: `using BackgroundSchedulePoolTaskHolder = FileCacheScheduledTaskHolder`
using BackgroundSchedulePoolTaskHolder = FileCacheScheduledTaskHolder;

} // namespace facebook::velox::ch
```

- [ ] **Step 7: Implement `FileCacheScheduler.cpp`**

Create `velox/ch/Common/FileCacheScheduler.cpp`.  Implement each method
according to the state machine described in the header and in the design doc
`05-filecache-scheduler-design.md`. Key algorithms:

**`FileCacheScheduledTask::schedule()`**

```text
lock mutex_
if Deactivated: return false
if Idle or Delayed:
    cancelTimerLocked()
    queueImmediateLocked()       // state = Queued
    return true
if Queued:
    return false                 // already queued, coalesce
if Running:
    pendingImmediate_ = true
    pendingDelayed_ = false
    return true
```

**`FileCacheScheduledTask::scheduleAfter(delayMs)`**

```text
lock mutex_
if Deactivated: return false
if Idle or Delayed:
    cancelTimerLocked()
    state = Delayed
    generation_ snapshot gen
    weak task snapshot = weak_from_this()
    timerFuture_ = timekeeper.after(delayMs)
        .deferValue([weak task, gen](folly::Unit) {
            shared task = weak task.lock()
            if no shared task: return
            lock task mutex_
            if task generation_ != gen or task state != Delayed: return
            task queueImmediateLocked()
        })
    return true
if Queued:
    return false  // already running imminently; keep queued
if Running:
    pendingDelayed_ = true (overwrite)
    pendingImmediate_ = false (delayed is lower priority than immediate)
    pendingDelayMs_ = delayMs
    return true
```

**`FileCacheScheduledTask::runCallback()`**

```text
lock mutex_
generation_ check (bail if stale)
state = Running
pendingImmediate_ = false
pendingDelayed_ = false
unlock

try callback_() catch all → store/log; do not rethrow

lock mutex_
if Deactivated:
    cv_.notify_all()
    return
if pendingImmediate_:
    pendingImmediate_ = false
    queueImmediateLocked()
elif pendingDelayed_:
    pendingDelayed_ = false
    // re-arm timer (outside lock)
else:
    state = Idle
```

**`FileCacheScheduledTask::deactivate()`**

```text
lock mutex_
if already Deactivated: return
generation_++
cancelTimerLocked()
bool wasRunning = (state == Running)
state = Deactivated
pendingImmediate_ = false
pendingDelayed_ = false
unlock

if wasRunning:
    wait cv_ until state != Running   // i.e. until runCallback sets Deactivated and notifies
```

**`FileCacheScheduledTask::cancelTimerLocked()`**

```text
if timerFuture_.valid():
    timerFuture_.cancel()  // folly SemiFuture cancel
    timerFuture_ = SemiFuture::makeEmpty()
generation_++
```

**`FileCacheScheduledTask::queueImmediateLocked()`**

```text
state = Queued
gen snapshot = generation_
weak task snapshot = weak_from_this()
workerPool_.schedule([weak task, gen] {
    shared task = weak task.lock()
    if no shared task: return
    lock task mutex_
    if gen != task generation_ or task state != Queued: return  // stale
    unlock
    task runCallback()
})
```

**`FileCacheScheduler::createTask`**

```text
lock mutex_
if shutdown_: return empty holder
task = make_shared<FileCacheScheduledTask>(name, callback, *this)
tasks_.push_back(weak_ptr(task))
return FileCacheScheduledTaskHolder(task)
```

**`FileCacheScheduler::shutdown`**

```text
lock mutex_
shutdown_ = true
snapshot = tasks_ (lock weak_ptr to shared_ptr)
unlock

for each live task in snapshot:
    task->deactivate()
```

Exceptions from callbacks must not escape `runCallback`; catch all, log via
`LOG_ERROR` (no-op shim), and continue.

- [ ] **Step 8: Register the new sources in `CMakeLists.txt`**

Add `FileCacheScheduler.cpp` and `FileCacheQueryIdScope.cpp` to the
`velox_add_library` source list in `velox/ch/Common/CMakeLists.txt`, and add
`FileCacheScheduler.h` and `FileCacheQueryIdScope.h` to the HEADERS list.
The resulting list of sources in that call will be:

```cmake
StatusFile.cpp
ThreadPool.cpp
FileCacheQueryIdScope.cpp
FileCacheScheduler.cpp
```

- [ ] **Step 9: Build the focused test**

Reconfigure using the same command as Step 3, then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_scheduler_test \
  > <velox_build_dir>/build_task_006_scheduler.log 2>&1
```

Expected: exit code 0.  Do not add `-j`.

- [ ] **Step 10: Run the focused test**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_scheduler_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_006_scheduler.log 2>&1
```

Expected: `100% tests passed, 0 tests failed.`

- [ ] **Step 11: Regression check**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^(velox_ch_common_test|velox_ch_guards_test|velox_ch_threadpool_test)$' \
  --output-on-failure \
  >> <velox_build_dir>/test_task_006_scheduler.log 2>&1
```

Expected: all three tests pass.

- [ ] **Step 12: Inspect only task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Common/FileCacheScheduler.h \
  velox/ch/Common/FileCacheScheduler.cpp \
  velox/ch/Common/FileCacheQueryIdScope.h \
  velox/ch/Common/FileCacheQueryIdScope.cpp \
  velox/ch/Common/tests/CMakeLists.txt \
  velox/ch/Common/tests/SchedulerAndScopeTest.cpp
```

Expected: no whitespace errors; no files outside the declared scope changed;
changes remain unstaged.

- [ ] **Step 13: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/006-filecache-scheduler-and-caller-scope-result.md
```

Use exactly this structure:

````markdown
# Task 006 Result: Add `FileCacheScheduler` and `FileCacheQueryIdScope`

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
<velox_build_dir>/configure_task_006_scheduler.log
<velox_build_dir>/build_task_006_red.log
<velox_build_dir>/build_task_006_scheduler.log
<velox_build_dir>/test_task_006_scheduler.log
```

## Verification

```text
Red build failed because FileCacheScheduler.h was absent.
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
Task 007: implement ReadBufferFromVeloxReadFile and WriteBufferFromVeloxWriteFile.
```
````

## Explicit exclusions

Do not implement in this task:

```text
ReadBufferFromVeloxReadFile / WriteBufferFromVeloxWriteFile
FileCache, FileSegment, Metadata, CacheMetadata — the actual FileCache algorithms
FileCacheBufferedInput / FileCacheInputStream
SipHash128
FileCacheKey / FileSegmentKeyType / FileCacheSettings
Gluten integration
BackgroundSchedulePool async-task grouping or system-table introspection
```
