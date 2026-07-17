# Task 004: Add `StatusFile` and `Guards.h`

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `/home/chang/OpenSource/velox` and writes one
> result file under this ClickHouse checkout. Do not modify any ClickHouse source
> files outside `port/task/result/`. Do not commit or stage either repository.

## Goal

Implement `StatusFile.h/.cpp` — the per-cache-directory exclusive-ownership
guard — and `Guards.h` — the full five-guard lock-order contract for
`FileCache`. Both depend only on Task 003 shims plus `folly::File`.

Deliverables:
- `velox/ch/Common/StatusFile.h` / `StatusFile.cpp`
- `velox/ch/Interpreters/FileCache/Guards.h`
- An expanded `velox_ch_filecache` CMake target that compiles `StatusFile.cpp`
- A focused test executable `velox_ch_guards_test`

## Starting point

```text
Velox repository: /home/chang/OpenSource/velox
Required branch:  filecache
Expected HEAD:    Task 003 completed (bf379041f or a direct descendant)
```

Do not require a clean worktree but do not overwrite unrelated changes. Stop
if the branch is not `filecache`.

## Design references

Read before editing:

```text
/home/chang/SourceCode/ClickHouse/port/task/ENVIRONMENT.md
/home/chang/SourceCode/ClickHouse/port/1-dependencies/01-filecache-infra-mapping.md
/home/chang/SourceCode/ClickHouse/port/1-dependencies/02-filecache-basic-shims-design.md
/home/chang/SourceCode/ClickHouse/port/task/result/003-filecache-basic-common-shims-result.md
```

Use the ClickHouse implementations only as behavioral references:

```text
/home/chang/SourceCode/ClickHouse/src/Common/StatusFile.h
/home/chang/SourceCode/ClickHouse/src/Common/StatusFile.cpp
/home/chang/SourceCode/ClickHouse/src/Interpreters/FileCache/Guards.h
```

## File scope

Modify:

```text
/home/chang/OpenSource/velox/velox/ch/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Common/ProfileEvents.h
```

Create:

```text
/home/chang/OpenSource/velox/velox/ch/Common/StatusFile.h
/home/chang/OpenSource/velox/velox/ch/Common/StatusFile.cpp
/home/chang/OpenSource/velox/velox/ch/Interpreters/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/Guards.h
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/StatusFileAndGuardsTest.cpp
/home/chang/SourceCode/ClickHouse/port/task/result/004-filecache-status-and-guards-result.md
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
HEAD is the Task 003 commit or a direct descendant.
Record all pre-existing dirty files in the result file.
```

- [ ] **Step 2: Add a failing focused test**

Create `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt`:

```cmake
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_executable(velox_ch_guards_test StatusFileAndGuardsTest.cpp)
add_test(velox_ch_guards_test velox_ch_guards_test)

target_link_libraries(
  velox_ch_guards_test
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

Create `velox/ch/Interpreters/FileCache/tests/StatusFileAndGuardsTest.cpp`:

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

#include "velox/ch/Common/StatusFile.h"
#include "velox/ch/Interpreters/FileCache/Guards.h"
#include "velox/common/base/Exceptions.h"
#include "velox/common/testutil/TempDirectoryPath.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

namespace facebook::velox::ch
{
namespace
{

using common::testutil::TempDirectoryPath;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// StatusFile tests
// ---------------------------------------------------------------------------

TEST(StatusFileTest, WritePidFillFunctionDoesNotThrow)
{
    auto directory = TempDirectoryPath::create();
    const std::string path = directory->getPath() + "/status";
    EXPECT_NO_THROW(StatusFile file(path, StatusFile::writePid()));
    // After destruction the path is removed.
    EXPECT_FALSE(fs::exists(path));
}

TEST(StatusFileTest, EmptyFillFunctionDoesNotThrow)
{
    auto directory = TempDirectoryPath::create();
    const std::string path = directory->getPath() + "/status";
    EXPECT_NO_THROW(StatusFile file(path, nullptr));
    EXPECT_FALSE(fs::exists(path));
}

TEST(StatusFileTest, SecondInstanceOnSamePathThrows)
{
    auto directory = TempDirectoryPath::create();
    const std::string path = directory->getPath() + "/status";

    StatusFile first(path, StatusFile::writePid());

    // A second StatusFile on the same path must fail because the first
    // holds the exclusive flock.
    EXPECT_THROW(StatusFile second(path, StatusFile::writePid()), VeloxRuntimeError);
}

TEST(StatusFileTest, DestructorUnlinksPath)
{
    auto directory = TempDirectoryPath::create();
    const std::string path = directory->getPath() + "/status";
    {
        StatusFile file(path, StatusFile::writePid());
        EXPECT_TRUE(fs::exists(path));
    }
    EXPECT_FALSE(fs::exists(path));
}

TEST(StatusFileTest, AfterDestructionNewInstanceSucceeds)
{
    auto directory = TempDirectoryPath::create();
    const std::string path = directory->getPath() + "/status";
    {
        StatusFile first(path, StatusFile::writePid());
    }
    // Previous destructor closed and unlinked; a fresh instance must succeed.
    EXPECT_NO_THROW(StatusFile second(path, StatusFile::writePid()));
}

// ---------------------------------------------------------------------------
// Guards tests
// ---------------------------------------------------------------------------

// CachePriorityGuard locks compile and are the correct lock types.
TEST(GuardsTest, CachePriorityGuardLockTypes)
{
    CachePriorityGuard guard;

    CachePriorityGuard::ReadLock readLock = guard.readLock();
    static_assert(
        std::is_same_v<
            CachePriorityGuard::ReadLock,
            std::shared_lock<SharedMutex>>);

    readLock.unlock();

    CachePriorityGuard::WriteLock writeLock = guard.writeLock();
    static_assert(
        std::is_same_v<
            CachePriorityGuard::WriteLock,
            std::unique_lock<SharedMutex>>);
}

// tryReadLock / tryWriteLock return an unowned lock when they fail.
TEST(GuardsTest, CachePriorityGuardTryLockMayFail)
{
    CachePriorityGuard guard;

    // Hold write lock, then try to take another read lock (should fail).
    auto writeLock = guard.writeLock();

    auto tryRead = guard.tryReadLock();
    EXPECT_FALSE(tryRead.owns_lock());

    auto tryWrite = guard.tryWriteLock();
    EXPECT_FALSE(tryWrite.owns_lock());
}

// CacheStateGuard::tryLockFor compiles and returns within the timeout.
TEST(GuardsTest, CacheStateGuardTryLockFor)
{
    CacheStateGuard guard;
    CacheStateGuard::Lock lock = guard.tryLockFor(1ms);
    EXPECT_TRUE(lock.owns_lock());
}

// CacheStateGuard::tryLockFor with zero timeout on a held lock returns unowned.
TEST(GuardsTest, CacheStateGuardTryLockForFailsWhenHeld)
{
    CacheStateGuard guard;
    auto held = guard.lock();
    auto result = std::async(std::launch::async, [&guard]
    {
        return guard.tryLockFor(0ms).owns_lock();
    });
    EXPECT_FALSE(result.get());
}

// KeyGuard and FileSegmentGuard lock types are not interchangeable with
// CachePriorityGuard lock types — they are distinct struct::Lock types.
TEST(GuardsTest, LockTypesAreNotInterchangeable)
{
    static_assert(
        !std::is_same_v<KeyGuard::Lock, CachePriorityGuard::WriteLock>,
        "KeyGuard::Lock must differ from CachePriorityGuard::WriteLock");
    static_assert(
        !std::is_same_v<FileSegmentGuard::Lock, KeyGuard::Lock>,
        "FileSegmentGuard::Lock must differ from KeyGuard::Lock");
    static_assert(
        !std::is_same_v<CacheMetadataGuard::Lock, KeyGuard::Lock>,
        "CacheMetadataGuard::Lock must differ from KeyGuard::Lock");
}

// Each guard's lock() acquires and owns.
TEST(GuardsTest, AllGuardsLockSuccessfully)
{
    {
        CacheMetadataGuard g;
        auto lock = g.lock();
        EXPECT_TRUE(lock.owns_lock());
    }
    {
        KeyGuard g;
        auto lock = g.lock();
        EXPECT_TRUE(lock.owns_lock());
    }
    {
        FileSegmentGuard g;
        auto lock = g.lock();
        EXPECT_TRUE(lock.owns_lock());
    }
}

} // namespace
} // namespace facebook::velox::ch
```

Create `velox/ch/Interpreters/FileCache/CMakeLists.txt`:

```cmake
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Guards.h is a header-only addition to velox_ch_filecache.
# With VELOX_MONO_LIBRARY=ON the global include_directories(.) from the root
# CMakeLists already makes it findable; we register the FILE_SET only in
# non-mono builds so IDE navigation and install work there too.
if(NOT VELOX_MONO_LIBRARY)
  target_sources(
    velox_ch_filecache
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/Guards.h
  )
endif()

if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Create `velox/ch/Interpreters/CMakeLists.txt`:

```cmake
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_subdirectory(FileCache)
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
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_004_guards.log 2>&1
```

Then try to build:

```bash
if /home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_guards_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected:

```text
Configure succeeds.
Build fails because StatusFile.h, StatusFile.cpp, and Guards.h do not yet exist.
```

- [ ] **Step 4: Extend `ProfileEvents.h` with guard-related events**

Add the three missing events to the `ProfileEvents::Event` enum inside
`velox/ch/Common/ProfileEvents.h`:

```cpp
FilesystemCacheStateLockMicroseconds,
FilesystemCachePriorityWriteLockMicroseconds,
FilesystemCachePriorityReadLockMicroseconds,
```

These complete the no-op `ProfileEventTimeIncrement` calls already used by the
guard implementations. Do not alter any other enumerator.

- [ ] **Step 5: Implement `StatusFile.h`**

Create `velox/ch/Common/StatusFile.h`:

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

#include "velox/ch/Common/FileCacheFilesystem.h"

#include <folly/File.h>

#include <functional>
#include <string>

namespace facebook::velox::ch
{

/// Provides that no more than one cache instance uses the same cache directory.
///
/// On construction it opens or creates the status file at `path`, acquires an
/// exclusive inter-process `flock` (non-blocking), truncates the file, and
/// writes diagnostic information via `fill`.  The lock is held until the
/// destructor runs.  The destructor explicitly closes the `folly::File` before
/// calling `unlink`, preserving the invariant that the lock is released before
/// the path disappears from the filesystem.
///
/// Constructor throws `VeloxRuntimeError` if the lock cannot be acquired
/// (another instance is running) or if any syscall fails.
/// Destructor ignores close and unlink errors (cannot throw).
class StatusFile
{
public:
    /// Callback writing diagnostic content directly to the open file descriptor.
    /// May be nullptr, in which case the file is created but left empty.
    using FillFunction = std::function<void(int fd)>;

    StatusFile(std::string path, FillFunction fill);
    ~StatusFile();

    StatusFile(const StatusFile &) = delete;
    StatusFile & operator=(const StatusFile &) = delete;

    /// Returns a FillFunction that writes the current PID as a decimal string.
    static FillFunction writePid();

private:
    const std::string path_;
    folly::File file_;
};

} // namespace facebook::velox::ch
```

- [ ] **Step 6: Implement `StatusFile.cpp`**

Create `velox/ch/Common/StatusFile.cpp`:

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

#include "velox/ch/Common/StatusFile.h"
#include "velox/ch/Common/FileCacheException.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace facebook::velox::ch
{

StatusFile::FillFunction StatusFile::writePid()
{
    return [](int fd)
    {
        const std::string pid = std::to_string(static_cast<long>(::getpid()));
        (void)::write(fd, pid.data(), pid.size());
    };
}

StatusFile::StatusFile(std::string path, FillFunction fill)
    : path_(std::move(path))
{
    // Open or create the status file.
    const int rawFd = ::open(
        path_.c_str(),
        O_WRONLY | O_CREAT | O_CLOEXEC,
        0666);

    if (rawFd == -1)
        throwFileCacheException(
            "StatusFile: cannot open '{}': {}",
            path_,
            std::strerror(errno));

    // Transfer ownership to folly::File so the fd is closed on any exception.
    file_ = folly::File(rawFd, /*ownsFd=*/true);

    // Acquire exclusive flock (non-blocking).  Two separate open() calls in the
    // same or different processes each get a distinct open-file-description; the
    // kernel will deny the second try_lock.
    if (!file_.try_lock())
        throwFileCacheException(
            "StatusFile: cannot lock '{}'. "
            "Another cache instance using the same path is already running.",
            path_);

    // Truncate and seek to the beginning before writing content.
    if (::ftruncate(file_.fd(), 0) != 0)
        throwFileCacheException(
            "StatusFile: cannot truncate '{}': {}", path_, std::strerror(errno));

    if (::lseek(file_.fd(), 0, SEEK_SET) == static_cast<off_t>(-1))
        throwFileCacheException(
            "StatusFile: cannot seek '{}': {}", path_, std::strerror(errno));

    if (fill)
        fill(file_.fd());
}

StatusFile::~StatusFile()
{
    // Explicitly close before unlink so the flock is released before the path
    // disappears.  folly::File::close sets the fd to -1, preventing the member
    // destructor from double-closing.
    file_.close();

    // Best-effort removal; ignore errors (destructor must not throw).
    (void)::unlink(path_.c_str());
}

} // namespace facebook::velox::ch
```

- [ ] **Step 7: Implement `Guards.h`**

Create `velox/ch/Interpreters/FileCache/Guards.h`:

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

#include "velox/ch/Common/ProfileEvents.h"
#include "velox/ch/Common/SharedMutex.h"

#include <chrono>
#include <mutex>
#include <shared_mutex>

namespace facebook::velox::ch
{

/**
 * Lock ordering (outermost to innermost):
 *
 *   CachePriorityGuard
 *     > CacheMetadataGuard
 *       > KeyGuard
 *         > FileSegmentGuard
 *
 * CacheStateGuard (total size / element counters) is independent of the
 * priority chain; it is taken after CachePriorityGuard when both are needed.
 *
 * Nested Lock types are intentionally distinct structs so that a
 * KeyGuard::Lock cannot be passed where a FileSegmentGuard::Lock is expected,
 * enforcing the ordering contract at compile time.
 */

/// Priority queue guard.
/// WriteLock for structural mutations; ReadLock for read-only iteration.
struct CachePriorityGuard
{
    using WriteLock = std::unique_lock<SharedMutex>;
    using ReadLock = std::shared_lock<SharedMutex>;

    ReadLock tryReadLock()
    {
        return ReadLock(mutex, std::try_to_lock);
    }

    WriteLock tryWriteLock()
    {
        return WriteLock(mutex, std::try_to_lock);
    }

    ReadLock readLock()
    {
        ProfileEventTimeIncrement<Microseconds> watch(
            ProfileEvents::FilesystemCachePriorityReadLockMicroseconds);
        return ReadLock(mutex);
    }

    WriteLock writeLock()
    {
        ProfileEventTimeIncrement<Microseconds> watch(
            ProfileEvents::FilesystemCachePriorityWriteLockMicroseconds);
        return WriteLock(mutex);
    }

    CachePriorityGuard() = default;
    CachePriorityGuard(const CachePriorityGuard &) = delete;
    CachePriorityGuard & operator=(const CachePriorityGuard &) = delete;

private:
    SharedMutex mutex;
};

/// State guard protecting total-size / element counters.
struct CacheStateGuard
{
    using Mutex = std::timed_mutex;

    struct Lock : public std::unique_lock<Mutex>
    {
        using Base = std::unique_lock<Mutex>;
        using Base::Base;
    };

    Lock tryLock()
    {
        return Lock(mutex, std::try_to_lock);
    }

    Lock lock()
    {
        ProfileEventTimeIncrement<Microseconds> watch(
            ProfileEvents::FilesystemCacheStateLockMicroseconds);
        return Lock(mutex);
    }

    Lock tryLockFor(const std::chrono::milliseconds & acquireTimeout)
    {
        ProfileEventTimeIncrement<Microseconds> watch(
            ProfileEvents::FilesystemCacheStateLockMicroseconds);
        return Lock(mutex, acquireTimeout);
    }

    CacheStateGuard() = default;
    CacheStateGuard(const CacheStateGuard &) = delete;
    CacheStateGuard & operator=(const CacheStateGuard &) = delete;

private:
    Mutex mutex;
};

/// Metadata guard.  One instance per CacheMetadata object.
struct CacheMetadataGuard
{
    struct Lock : public std::unique_lock<std::mutex>
    {
        explicit Lock(std::mutex & m) : std::unique_lock<std::mutex>(m) {}
    };

    Lock lock()
    {
        return Lock(mutex);
    }

    CacheMetadataGuard() = default;
    CacheMetadataGuard(const CacheMetadataGuard &) = delete;
    CacheMetadataGuard & operator=(const CacheMetadataGuard &) = delete;

    std::mutex mutex;
};

/// Key guard.  One instance per cache key entry.
struct KeyGuard
{
    struct Lock : public std::unique_lock<std::mutex>
    {
        explicit Lock(std::mutex & m) : std::unique_lock<std::mutex>(m) {}
    };

    Lock lock()
    {
        return Lock(mutex);
    }

    KeyGuard() = default;
    KeyGuard(const KeyGuard &) = delete;
    KeyGuard & operator=(const KeyGuard &) = delete;

    std::mutex mutex;
};

/// File-segment guard.  One instance per FileSegment.
struct FileSegmentGuard
{
    struct Lock : public std::unique_lock<std::mutex>
    {
        explicit Lock(std::mutex & m) : std::unique_lock<std::mutex>(m) {}
    };

    Lock lock()
    {
        return Lock(mutex);
    }

    FileSegmentGuard() = default;
    FileSegmentGuard(const FileSegmentGuard &) = delete;
    FileSegmentGuard & operator=(const FileSegmentGuard &) = delete;

    std::mutex mutex;
};

} // namespace facebook::velox::ch
```

- [ ] **Step 8: Update `CMakeLists.txt` files**

Replace `velox/ch/Common/CMakeLists.txt` with:

```cmake
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

velox_add_library(
  velox_ch_filecache
  StatusFile.cpp
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
)

# In non-mono builds velox_ch_filecache is a real library; link its direct deps.
if(NOT VELOX_MONO_LIBRARY)
  target_link_libraries(velox_ch_filecache PRIVATE Folly::folly)
endif()

if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Append to `velox/ch/CMakeLists.txt` (add after the existing `add_subdirectory(Common)` line):

```cmake
add_subdirectory(Interpreters)
```

- [ ] **Step 9: Build the focused test**

Reconfigure using the same command as Step 3, then build:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_guards_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_guards.log 2>&1
```

Expected:

```text
Exit code 0.
```

Do not add `-j`.

- [ ] **Step 10: Run the focused test**

```bash
ctest \
  --test-dir /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -R '^velox_ch_guards_test$' \
  --output-on-failure \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_004_guards.log 2>&1
```

Expected:

```text
100% tests passed, 0 tests failed.
```

- [ ] **Step 11: Inspect only task-owned changes**

```bash
cd /home/chang/OpenSource/velox
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/CMakeLists.txt \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Common/ProfileEvents.h \
  velox/ch/Common/StatusFile.h \
  velox/ch/Common/StatusFile.cpp \
  velox/ch/Interpreters/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/Guards.h \
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/tests/StatusFileAndGuardsTest.cpp
```

Expected:

```text
No whitespace errors.
No files outside the declared scope were changed by this task.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 12: Write the result handoff**

Create:

```text
/home/chang/SourceCode/ClickHouse/port/task/result/004-filecache-status-and-guards-result.md
```

Use exactly this structure:

````markdown
# Task 004 Result: Add `StatusFile` and `Guards.h`

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
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_004_guards.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_red.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_guards.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_004_guards.log
```

## Verification

```text
Red build failed because StatusFile.h/Guards.h were absent.
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
Task 005: implement FileCacheWorkerPool, FileCacheWorker, FileCacheThreadPool.
```
````

If blocked or failed, set the status accordingly, include the first actionable
error and log path, and do not claim success.

## Explicit exclusions

Do not implement in this task:

```text
FileCacheScheduler / BackgroundSchedulePool
FileCacheWorkerPool / FileCacheThreadPool / FileCacheWorker
FileCacheQueryIdScope
ThreadPool.h / ThreadPool.cpp
ReadBufferFromVeloxReadFile / WriteBufferFromVeloxWriteFile
FileCache leaf types or algorithms (FileSegment, Metadata, etc.)
SipHash128
Gluten integration
```

These belong to later tasks.
