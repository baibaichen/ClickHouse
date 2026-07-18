# Task 003: Add `FileCache` Basic Common Shims

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `/home/chang/OpenSource/velox` and writes one
> result file under this ClickHouse checkout. Do not modify ClickHouse source
> files. Do not commit or stage either repository.

## Post-acceptance source-contract audit — task reopened

The original Task 003 acceptance remains in the receipt, but this task is reopened.
The queue contract below supersedes every narrower `FileCacheBoundedQueue` API or
test later in this file.

Task 012 ports `FileCache::freeSpaceRatioImpl`, whose normal background-eviction
path requires all of these CH queue operations:

```cpp
bool tryPush(const T & value, uint64_t timeoutMilliseconds = 0);
bool tryPush(T && value, uint64_t timeoutMilliseconds = 0);
bool tryPop(T & value);
bool tryPop(T & value, uint64_t timeoutMilliseconds);
```

Required semantics:

```text
tryPush(value, 0):
  return immediately

tryPush(value, timeout > 0):
  wait up to timeout for capacity
  wake on pop or finish
  return false on timeout or finish

tryPop(value):
  never wait
  return false when empty

tryPop(value, timeout):
  wait up to timeout for data
  wake on push or finish
```

The corrective Worker must first add focused RED tests proving:

1. timed `tryPush` blocks while full and succeeds when a consumer frees capacity;
2. timed `tryPush` returns false after the requested timeout;
3. non-blocking `tryPop` returns immediately on an empty queue;
4. timed `tryPop` wakes on push and on `finish`;
5. the exact Task 012 call shapes `tryPush(batch, 10)` and `tryPop(batch)` compile.

### `chassert` compatibility shim

Task 012 ports many non-heavy CH invariants expressed as `chassert`. Add:

```text
velox/ch/Common/ClickHouseAssert.h
```

Do not map `chassert` to `VELOX_DCHECK`, `VELOX_CHECK`, or standard `assert`.
The required behavior is:

```text
!defined(NDEBUG) || defined(FOLLY_SANITIZE):
  evaluate the expression exactly once
  log the expression text or explicit message
  abort; do not throw

ordinary Release:
  do not evaluate the expression
  preserve compile-time expression checking with sizeof
```

Include `folly/CPortability.h` and use its normalized `FOLLY_SANITIZE` macro.
Keep the one-argument and two-argument call forms:

```cpp
chassert(expression);
chassert(expression, "diagnostic message");
```

Use a dedicated header instead of `ClickHouseAliases.h`, so aliases do not acquire
glog and portability dependencies. Register `ClickHouseAssert.h` in the Task 003
public header file set; this overrides any narrower CMake header list later in the
task.

The corrective Worker must add:

1. death test for the default expression diagnostic;
2. death test for a custom message;
3. test proving a true expression is evaluated exactly once;
4. sanitizer-mode compile/death coverage with `NDEBUG`;
5. a separate ordinary-Release probe proving an expression with a side effect is
   not evaluated.

The existing Debug configure remains the only CMake configure. Exercise the other
preprocessor branches with two dedicated targets in the same build:

```text
velox_ch_chassert_release_probe
  source: ChassertReleaseProbe.cpp
  compile definitions: NDEBUG
  no FOLLY_SANITIZE definition
  assertion: chassert(++counter == 1) leaves counter == 0

velox_ch_chassert_sanitizer_gate_test
  source: ChassertSanitizerGateTest.cpp
  compile definitions: NDEBUG;FOLLY_SANITIZE=1
  assertion: chassert(false) still causes process death
```

The sanitizer-gate target proves branch selection without requiring a second
sanitizer toolchain configure; real sanitizer CI supplies the same normalized
`FOLLY_SANITIZE` macro through `folly/CPortability.h`.

Register both executables with CTest. Add `ClickHouseAssert.h`,
`ChassertReleaseProbe.cpp`, and `ChassertSanitizerGateTest.cpp` to the task-owned
CMake/file-review lists. All three assertion targets must run in the Task 003
acceptance gate.

Task 012 must not start until this corrective task is accepted.

## Goal

Replace the remaining foundational ClickHouse dependencies needed by later
`FileCache` algorithm tasks:

```text
primitive aliases
CH-compatible assertion shim
Velox exception adapter
shared mutex alias
no-op CH-style logging
std::filesystem adapter
finishable bounded queue
```

The deliverable is an expanded header-only `velox_ch_filecache` interface
library plus a focused `velox_ch_common_test` test executable.

## Starting point

```text
Velox repository: /home/chang/OpenSource/velox
Required branch:  filecache
Expected HEAD:    bf379041f Add initial `velox/ch` FileCache skeleton
```

Do not require a clean worktree, but do not overwrite unrelated changes. Stop
if the branch is not `filecache`.

## Design references

Read before editing:

```text
/home/chang/SourceCode/ClickHouse/port/task/ENVIRONMENT.md
/home/chang/SourceCode/ClickHouse/port/1-dependencies/01-filecache-infra-mapping.md
/home/chang/SourceCode/ClickHouse/port/1-dependencies/02-filecache-basic-shims-design.md
/home/chang/SourceCode/ClickHouse/port/2-file-cache/10-filecache-core-files-design.md
/home/chang/SourceCode/ClickHouse/port/task/result/002-common-noop-shims-result.md
```

Use the ClickHouse implementations only as behavioral references:

```text
/home/chang/SourceCode/ClickHouse/src/Common/ConcurrentBoundedQueue.h
/home/chang/SourceCode/ClickHouse/base/base/defines.h
```

## File scope

Modify:

```text
/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt
```

Create:

```text
/home/chang/OpenSource/velox/velox/ch/Common/ClickHouseAliases.h
/home/chang/OpenSource/velox/velox/ch/Common/ClickHouseAssert.h
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheException.h
/home/chang/OpenSource/velox/velox/ch/Common/SharedMutex.h
/home/chang/OpenSource/velox/velox/ch/Common/logger_useful.h
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheFilesystem.h
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheBoundedQueue.h
/home/chang/OpenSource/velox/velox/ch/Common/tests/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Common/tests/BasicShimsTest.cpp
/home/chang/OpenSource/velox/velox/ch/Common/tests/ChassertReleaseProbe.cpp
/home/chang/OpenSource/velox/velox/ch/Common/tests/ChassertSanitizerGateTest.cpp
/home/chang/SourceCode/ClickHouse/port/task/result/003-filecache-basic-common-shims-result.md
```

Every new Velox C++ and CMake file must begin with this license header:

```text
Copyright (c) Facebook, Inc. and its affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

Use the repository's existing comment form around that text (`/* ... */` for
C++, `#` for CMake).

## Steps

- [ ] **Step 1: Confirm the Velox baseline**

Run:

```bash
cd /home/chang/OpenSource/velox
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected:

```text
Branch is filecache.
HEAD is bf379041f or a direct descendant created by the user.
Record all pre-existing dirty files in the result file.
```

- [ ] **Step 2: Add a failing focused test**

Create `velox/ch/Common/tests/CMakeLists.txt`:

```cmake
add_executable(velox_ch_common_test BasicShimsTest.cpp)
add_test(velox_ch_common_test velox_ch_common_test)

target_link_libraries(
  velox_ch_common_test
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

Create `velox/ch/Common/tests/BasicShimsTest.cpp`:

```cpp
#include "velox/ch/Common/ClickHouseAliases.h"
#include "velox/ch/Common/FileCacheBoundedQueue.h"
#include "velox/ch/Common/FileCacheException.h"
#include "velox/ch/Common/FileCacheFilesystem.h"
#include "velox/ch/Common/SharedMutex.h"
#include "velox/ch/Common/logger_useful.h"
#include "velox/common/base/Exceptions.h"
#include "velox/common/testutil/TempDirectoryPath.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <type_traits>

namespace facebook::velox::ch
{
namespace
{

using common::testutil::TempDirectoryPath;
using namespace std::chrono_literals;

TEST(ClickHouseAliasesTest, PrimitiveTypes)
{
    static_assert(std::is_same_v<String, std::string>);
    static_assert(std::is_same_v<UInt8, uint8_t>);
    static_assert(std::is_same_v<UInt64, uint64_t>);
    static_assert(std::is_same_v<Int64, int64_t>);
}

TEST(LoggerUsefulTest, ArgumentsAreNotEvaluated)
{
    int evaluated = 0;
    LOG_TEST(getLogger("test"), "value {}", ++evaluated);
    EXPECT_EQ(evaluated, 0);
}

TEST(FileCacheExceptionTest, ThrowsVeloxRuntimeError)
{
    EXPECT_THROW(
        throwFileCacheException("invalid value {}", 42),
        VeloxRuntimeError);
}

TEST(SharedMutexTest, SupportsExclusiveAndSharedLocks)
{
    SharedMutex mutex;
    {
        std::unique_lock lock(mutex);
    }
    {
        std::shared_lock lock(mutex);
    }
}

TEST(FileCacheFilesystemTest, LocalFilesystemAlias)
{
    auto directory = TempDirectoryPath::create();
    const auto nested = directory->getPath() + "/a/b";
    EXPECT_TRUE(fs::create_directories(nested));
    EXPECT_TRUE(fs::exists(nested));
    EXPECT_GT(fs::remove_all(directory->getPath() + "/a"), 0);
    EXPECT_FALSE(fs::exists(nested));
}

TEST(FileCacheFilesystemTest, FilesystemErrorKeepsContext)
{
    const fs::filesystem_error error(
        "read failed",
        fs::path("/cache/file"),
        std::make_error_code(std::errc::io_error));

    try
    {
        throwFileCacheExceptionFromFilesystemError(error, "loading cache");
        FAIL() << "Expected VeloxRuntimeError";
    }
    catch (const VeloxRuntimeError & exception)
    {
        EXPECT_NE(
            std::string(exception.what()).find("loading cache"),
            std::string::npos);
    }
}

TEST(FileCacheBoundedQueueTest, CapacityZeroTryPushFails)
{
    FileCacheBoundedQueue<int> queue(0);
    EXPECT_FALSE(queue.tryPush(1));
}

TEST(FileCacheBoundedQueueTest, FinishDrainsQueuedValues)
{
    FileCacheBoundedQueue<int> queue(2);
    ASSERT_TRUE(queue.push(1));
    ASSERT_TRUE(queue.push(2));
    queue.finish();

    int value = 0;
    ASSERT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 1);
    ASSERT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 2);
    EXPECT_FALSE(queue.pop(value));
    EXPECT_FALSE(queue.push(3));
}

TEST(FileCacheBoundedQueueTest, FinishReleasesBlockedConsumer)
{
    FileCacheBoundedQueue<int> queue(1);
    std::promise<void> started;
    auto startedFuture = started.get_future();
    auto result = std::async(std::launch::async, [&]
    {
        started.set_value();
        int value = 0;
        return queue.pop(value);
    });

    startedFuture.get();
    queue.finish();
    ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
    EXPECT_FALSE(result.get());
}

TEST(FileCacheBoundedQueueTest, FinishReleasesBlockedProducer)
{
    FileCacheBoundedQueue<int> queue(1);
    ASSERT_TRUE(queue.push(1));

    std::promise<void> started;
    auto startedFuture = started.get_future();
    auto result = std::async(std::launch::async, [&]
    {
        started.set_value();
        return queue.push(2);
    });

    startedFuture.get();
    queue.finish();
    ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
    EXPECT_FALSE(result.get());
}

}
}
```

Append this to the current `velox/ch/Common/CMakeLists.txt` without changing the
existing interface target yet:

```cmake
if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

- [ ] **Step 3: Verify the test fails before implementation**

Reconfigure:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_BUILD_TESTING=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_003_basic_common.log 2>&1
```

Then run:

```bash
if /home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_common_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected:

```text
Configure succeeds.
Build fails because the new shim headers do not exist yet.
```

If configure fails for another reason, stop and report that error instead of
continuing.

- [ ] **Step 4: Add primitive aliases and exception adapter**

Create `velox/ch/Common/ClickHouseAliases.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace facebook::velox::ch
{

using String = std::string;

using UInt8 = uint8_t;
using UInt16 = uint16_t;
using UInt32 = uint32_t;
using UInt64 = uint64_t;
using UInt128 = unsigned __int128;

using Int8 = int8_t;
using Int16 = int16_t;
using Int32 = int32_t;
using Int64 = int64_t;

}
```

Create `velox/ch/Common/FileCacheException.h`:

```cpp
#pragma once

#include "velox/common/base/Exceptions.h"

#include <fmt/format.h>

#include <utility>

namespace facebook::velox::ch
{

template <typename... Args>
[[noreturn]] void throwFileCacheException(
    fmt::format_string<Args...> format,
    Args &&... args)
{
    VELOX_FAIL(
        "{}",
        fmt::format(format, std::forward<Args>(args)...));
}

}
```

- [ ] **Step 5: Add mutex, logging, and filesystem adapters**

Create `velox/ch/Common/SharedMutex.h`:

```cpp
#pragma once

#include <folly/SharedMutex.h>

namespace facebook::velox::ch
{

using SharedMutex = folly::SharedMutex;

}
```

Create `velox/ch/Common/logger_useful.h`:

```cpp
#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace facebook::velox::ch
{

struct FileCacheLogger
{
};

using LoggerPtr = std::shared_ptr<FileCacheLogger>;

inline LoggerPtr getLogger(std::string_view)
{
    return {};
}

inline std::string getCurrentExceptionMessage(bool = false)
{
    return {};
}

inline void tryLogCurrentException(...) noexcept
{
}

}

#define LOG_TEST(...) \
    do                \
    {                 \
    } while (false)
#define LOG_TRACE(...) LOG_TEST(__VA_ARGS__)
#define LOG_DEBUG(...) LOG_TEST(__VA_ARGS__)
#define LOG_INFO(...) LOG_TEST(__VA_ARGS__)
#define LOG_WARNING(...) LOG_TEST(__VA_ARGS__)
#define LOG_ERROR(...) LOG_TEST(__VA_ARGS__)
```

Create `velox/ch/Common/FileCacheFilesystem.h`:

```cpp
#pragma once

#include "velox/common/base/Exceptions.h"

#include <filesystem>
#include <string_view>

namespace facebook::velox::ch
{

namespace fs = std::filesystem;

[[noreturn]] inline void throwFileCacheExceptionFromFilesystemError(
    const fs::filesystem_error & error,
    std::string_view context)
{
    VELOX_FAIL("{}: {}", context, error.what());
}

}
```

- [ ] **Step 6: Add the finishable bounded queue**

Create `velox/ch/Common/FileCacheBoundedQueue.h`:

```cpp
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <type_traits>
#include <utility>

namespace facebook::velox::ch
{

template <typename T>
class FileCacheBoundedQueue
{
public:
    explicit FileCacheBoundedQueue(size_t capacity)
        : capacity_(capacity)
    {
    }

    FileCacheBoundedQueue(const FileCacheBoundedQueue &) = delete;
    FileCacheBoundedQueue & operator=(const FileCacheBoundedQueue &) = delete;

    bool push(T value)
    {
        {
            std::unique_lock lock(mutex_);
            producerCv_.wait(lock, [&]
            {
                return finished_ || queue_.size() < capacity_;
            });

            if (finished_)
                return false;

            queue_.emplace_back(std::move(value));
        }

        consumerCv_.notify_one();
        return true;
    }

    bool tryPush(T value)
    {
        {
            std::lock_guard lock(mutex_);
            if (finished_ || queue_.size() >= capacity_)
                return false;
            queue_.emplace_back(std::move(value));
        }

        consumerCv_.notify_one();
        return true;
    }

    bool pop(T & value)
    {
        {
            std::unique_lock lock(mutex_);
            consumerCv_.wait(lock, [&]
            {
                return finished_ || !queue_.empty();
            });

            if (finished_ && queue_.empty())
                return false;

            if constexpr (
                std::is_nothrow_move_assignable_v<T>
                || !std::is_copy_assignable_v<T>)
                value = std::move(queue_.front());
            else
                value = queue_.front();

            queue_.pop_front();
        }

        producerCv_.notify_one();
        return true;
    }

    void finish()
    {
        {
            std::lock_guard lock(mutex_);
            finished_ = true;
        }

        producerCv_.notify_all();
        consumerCv_.notify_all();
    }

    bool isFinished() const
    {
        std::lock_guard lock(mutex_);
        return finished_;
    }

    size_t size() const
    {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable producerCv_;
    std::condition_variable consumerCv_;
    std::deque<T> queue_;
    size_t capacity_;
    bool finished_ = false;
};

}
```

Do not replace Metadata's handwritten `DownloadQueue` or `CleanupQueue` with
this class in later tasks. This class replaces only ClickHouse
`ConcurrentBoundedQueue` call sites.

- [ ] **Step 7: Register the new headers**

Replace `velox/ch/Common/CMakeLists.txt` with:

```cmake
velox_add_library(
  velox_ch_filecache
  INTERFACE
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
)

if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

- [ ] **Step 8: Build the focused test**

Reconfigure using the same command as Step 3, then build:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_common_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_basic_common.log 2>&1
```

Expected:

```text
Exit code 0.
```

Do not add `-j`.

- [ ] **Step 9: Run the focused test**

Run:

```bash
ctest \
  --test-dir /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -R '^velox_ch_common_test$' \
  --output-on-failure \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_basic_common.log 2>&1
```

Expected:

```text
100% tests passed, 0 tests failed.
```

- [ ] **Step 10: Inspect only task-owned changes**

Run:

```bash
cd /home/chang/OpenSource/velox
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Common/ClickHouseAliases.h \
  velox/ch/Common/FileCacheException.h \
  velox/ch/Common/SharedMutex.h \
  velox/ch/Common/logger_useful.h \
  velox/ch/Common/FileCacheFilesystem.h \
  velox/ch/Common/FileCacheBoundedQueue.h \
  velox/ch/Common/tests/CMakeLists.txt \
  velox/ch/Common/tests/BasicShimsTest.cpp
```

Expected:

```text
No whitespace errors.
No files outside the declared scope were changed by this task.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 11: Write the result handoff**

Create:

```text
/home/chang/SourceCode/ClickHouse/port/task/result/003-filecache-basic-common-shims-result.md
```

Use exactly this structure:

````markdown
# Task 003 Result: Add `FileCache` Basic Common Shims

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
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_003_basic_common.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_red.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_basic_common.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_basic_common.log
```

## Verification

```text
Red build failed because shim headers were absent.
Final build exit code:
Focused test result:
git diff --check result:
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 004: add StatusFile and migrate Guards.h.
```
````

If blocked or failed, set the status accordingly, include the first actionable
error and log path, and do not claim success.

## Explicit exclusions

Do not implement in this task:

```text
Guards.h
StatusFile.h / StatusFile.cpp
FileCacheScheduler
FileCacheWorkerPool / FileCacheThreadPool
FileCacheQueryIdScope
SipHash128
ReadBufferFromVeloxReadFile / WriteBufferFromVeloxWriteFile
FileCache leaf types or algorithms
Gluten integration
```

These belong to later tasks.
