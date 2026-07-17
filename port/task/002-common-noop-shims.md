# Task 002: Replace Skeleton with Common No-op Shims

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task only
> touches the Velox checkout under `/home/chang/OpenSource/velox`.

## Goal

Replace the temporary `FileCacheSkeleton` source with the first real
header-only `velox/ch/Common` shims from the FileCache port design. These shims
are all first-phase no-ops and exist only to keep ClickHouse `FileCache` call
sites compilable during the algorithm port.

## Branch and commit policy

Work in Velox:

```text
Repository: /home/chang/OpenSource/velox
Branch:     filecache
Base commit to amend: 9f7652f64 Add initial `velox/ch` FileCache skeleton
```

For this task only, amend the existing Velox skeleton commit instead of adding a
new Velox commit. Do not rewrite any ClickHouse repository commits.

Do not add `.gitkeep` files.

## Design references

Read these before editing Velox:

```text
/home/chang/SourceCode/ClickHouse/port/1-dependencies/03-filecache-metrics-debug-design.md
/home/chang/SourceCode/ClickHouse/port/1-dependencies/02-filecache-basic-shims-design.md
/home/chang/SourceCode/ClickHouse/port/task/result/001-velox-ch-skeleton-result.md
```

## Files

Modify:

```text
/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt
```

Delete:

```text
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheSkeleton.cpp
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheSkeleton.h
```

Create:

```text
/home/chang/OpenSource/velox/velox/ch/Common/ProfileEvents.h
/home/chang/OpenSource/velox/velox/ch/Common/CurrentMetrics.h
/home/chang/OpenSource/velox/velox/ch/Common/OpenTelemetryTraceContext.h
/home/chang/OpenSource/velox/velox/ch/Common/FailPoint.h
/home/chang/OpenSource/velox/velox/ch/Common/FilesystemCacheLog.h
/home/chang/OpenSource/velox/velox/ch/Common/QueryStatus.h
/home/chang/SourceCode/ClickHouse/port/task/result/002-common-noop-shims-result.md
```

## Steps

- [ ] **Step 1: Confirm Velox branch**

Run:

```bash
cd /home/chang/OpenSource/velox
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected:

```text
Branch is filecache.
HEAD is the skeleton commit, or a direct amended descendant of it.
```

If not on `filecache`, stop and report the current branch.

- [ ] **Step 2: Remove skeleton files**

Run:

```bash
rm -f \
  /home/chang/OpenSource/velox/velox/ch/Common/FileCacheSkeleton.cpp \
  /home/chang/OpenSource/velox/velox/ch/Common/FileCacheSkeleton.h
```

Expected:

```text
The skeleton files are removed from the working tree.
```

- [ ] **Step 3: Add `ProfileEvents` shim**

Create `/home/chang/OpenSource/velox/velox/ch/Common/ProfileEvents.h`:

```cpp
#pragma once

#include <cstdint>

namespace facebook::velox::ch {

namespace ProfileEvents {

enum Event {
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

} // namespace ProfileEvents

template <typename Unit>
class ProfileEventTimeIncrement {
 public:
  explicit ProfileEventTimeIncrement(ProfileEvents::Event) {}
};

struct Microseconds {};

} // namespace facebook::velox::ch
```

- [ ] **Step 4: Add `CurrentMetrics` shim**

Create `/home/chang/OpenSource/velox/velox/ch/Common/CurrentMetrics.h`:

```cpp
#pragma once

#include <cstdint>

namespace facebook::velox::ch {

namespace CurrentMetrics {

enum Metric {
  CacheFileSegments,
  FilesystemCacheHoldFileSegments,
  FilesystemCacheDownloadQueueElements,
  FilesystemCacheDelayedCleanupElements,
  FilesystemCacheReserveThreads,
  FilesystemCacheSizeLimit,
};

inline void add(Metric, int64_t = 1) {}

inline void sub(Metric, int64_t = 1) {}

class Increment {
 public:
  explicit Increment(Metric, int64_t = 1) {}
};

} // namespace CurrentMetrics

} // namespace facebook::velox::ch
```

- [ ] **Step 5: Add `OpenTelemetry` shim**

Create `/home/chang/OpenSource/velox/velox/ch/Common/OpenTelemetryTraceContext.h`:

```cpp
#pragma once

#include <string_view>

namespace facebook::velox::ch {

namespace OpenTelemetry {

class SpanHolder {
 public:
  explicit SpanHolder(std::string_view) {}

  template <typename T>
  void addAttribute(std::string_view, const T&) {}
};

} // namespace OpenTelemetry

} // namespace facebook::velox::ch
```

- [ ] **Step 6: Add `FailPoint` shim**

Create `/home/chang/OpenSource/velox/velox/ch/Common/FailPoint.h`:

```cpp
#pragma once

#define FAIL_POINT_TRIGGER(...) \
  do {                          \
  } while (false)
```

- [ ] **Step 7: Add `FilesystemCacheLog` shim**

Create `/home/chang/OpenSource/velox/velox/ch/Common/FilesystemCacheLog.h`:

```cpp
#pragma once

namespace facebook::velox::ch {

struct FilesystemCacheLogElement {
  enum class CacheType {
    READ_FROM_CACHE,
    READ_FROM_FS_BYPASSING_CACHE,
    READ_FROM_FS_AND_DOWNLOADED_TO_CACHE,
  };

  CacheType cache_type{CacheType::READ_FROM_FS_BYPASSING_CACHE};
};

class FilesystemCacheLog {
 public:
  template <typename T>
  void add(T&&) {}
};

} // namespace facebook::velox::ch
```

- [ ] **Step 8: Add `QueryStatus` shim**

Create `/home/chang/OpenSource/velox/velox/ch/Common/QueryStatus.h`:

```cpp
#pragma once

#include <memory>

namespace facebook::velox::ch {

class QueryStatus {
 public:
  void throwIfKilled() const {}
};

using QueryStatusPtr = std::shared_ptr<QueryStatus>;

} // namespace facebook::velox::ch
```

- [ ] **Step 9: Update CMake target**

Modify `/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt` to:

```cmake
velox_add_library(
  velox_ch_filecache
  INTERFACE
  HEADERS
    CurrentMetrics.h
    FailPoint.h
    FilesystemCacheLog.h
    OpenTelemetryTraceContext.h
    ProfileEvents.h
    QueryStatus.h
)
```

- [ ] **Step 10: Reconfigure CMake**

Run:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_002_common_noop_shims.log 2>&1
```

Expected:

```text
Exit code 0.
The log ends with "Build files have been written to".
```

- [ ] **Step 11: Verify target/header registration**

Because these shims are header-only and `VELOX_MONO_LIBRARY=ON`, there is no source
object to compile in this task. Verify CMake registered the target/header set:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -t targets \
  | grep 'velox_ch_filecache' \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/target_task_002_velox_ch_filecache.txt || true

grep -R "ProfileEvents.h" /home/chang/OpenSource/velox/cmake-build-debug-gcc13/CMakeFiles \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/headers_task_002_common_noop_shims.txt || true
```

Expected:

```text
Configure succeeded.
No FileCacheNoopShims.cpp object is needed.
target_task_002_velox_ch_filecache.txt may be empty under VELOX_MONO_LIBRARY=ON;
headers_task_002_common_noop_shims.txt should show ProfileEvents.h registered in generated CMake metadata.
```

- [ ] **Step 12: Confirm skeleton and replacement source are absent**

Run:

```bash
cd /home/chang/OpenSource/velox
test ! -e velox/ch/Common/FileCacheSkeleton.cpp
test ! -e velox/ch/Common/FileCacheSkeleton.h
test ! -e velox/ch/Common/FileCacheNoopShims.cpp
git --no-pager status --short --branch
```

Expected:

```text
No FileCacheSkeleton files remain.
No FileCacheNoopShims.cpp file is added.
No .gitkeep files are added.
```

- [ ] **Step 13: Amend Velox skeleton commit**

Run:

```bash
cd /home/chang/OpenSource/velox
git add \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Common/ProfileEvents.h \
  velox/ch/Common/CurrentMetrics.h \
  velox/ch/Common/OpenTelemetryTraceContext.h \
  velox/ch/Common/FailPoint.h \
  velox/ch/Common/FilesystemCacheLog.h \
  velox/ch/Common/QueryStatus.h

git add -u velox/ch/Common/FileCacheSkeleton.cpp velox/ch/Common/FileCacheSkeleton.h

git commit --amend --no-edit
```

Expected:

```text
The previous Velox skeleton commit is amended.
The branch remains filecache.
```

- [ ] **Step 14: Write result file**

Create the result directory:

```bash
mkdir -p /home/chang/SourceCode/ClickHouse/port/task/result
```

Write `/home/chang/SourceCode/ClickHouse/port/task/result/002-common-noop-shims-result.md`
with this structure:

````markdown
# Task 002 Result: Replace Skeleton with Common No-op Shims

## Status

status: success

## Velox status

```text
<paste `git --no-pager status --short --branch` and `git --no-pager log -1 --oneline` output here>
```

## Files changed

```text
velox/ch/Common/CMakeLists.txt
velox/ch/Common/ProfileEvents.h
velox/ch/Common/CurrentMetrics.h
velox/ch/Common/OpenTelemetryTraceContext.h
velox/ch/Common/FailPoint.h
velox/ch/Common/FilesystemCacheLog.h
velox/ch/Common/QueryStatus.h
deleted: velox/ch/Common/FileCacheSkeleton.cpp
deleted: velox/ch/Common/FileCacheSkeleton.h
```

## Commands run

```text
<paste configure/build/amend commands>
```

## Generated logs

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_002_common_noop_shims.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/target_task_002_velox_ch_filecache.txt
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/headers_task_002_common_noop_shims.txt
```

## Build result

```text
<paste configure result and header registration result>
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Add basic Common shims for SharedMutex/logger/filesystem, or start FileCache leaf types once Common no-op shims are accepted.
```
````

If the task is blocked or failed, set `status: blocked` or `status: failed`, replace `None`
under `Blocking errors` with the first actionable error, and still write the result file.

- [ ] **Step 15: Return result path**

Return a short message with:

```text
Path to /home/chang/SourceCode/ClickHouse/port/task/result/002-common-noop-shims-result.md
One-line status
```
