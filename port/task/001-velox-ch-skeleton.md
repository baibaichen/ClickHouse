# Task 001: Add `velox/ch` Skeleton Target

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task only
> touches the Velox checkout under `/home/chang/OpenSource/velox`. Do not modify
> ClickHouse source files for this task.

## Goal

Create the initial `velox/ch` directory tree and a minimal `velox_ch_filecache`
CMake target that builds successfully. This unblocks later tasks that add
`FileCache` shims under `velox/ch/Common`, `velox/ch/IO`, and
`velox/ch/Interpreters/FileCache`.

## Design references

Read these before editing Velox:

```text
/home/chang/SourceCode/ClickHouse/port/00-filecache-velox-migration.md
/home/chang/SourceCode/ClickHouse/port/01-filecache-port-order-design.md
/home/chang/SourceCode/ClickHouse/port/task/ENVIRONMENT.md
```

## Files

Modify:

```text
/home/chang/OpenSource/velox/velox/CMakeLists.txt
```

Create:

```text
/home/chang/OpenSource/velox/velox/ch/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheSkeleton.cpp
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheSkeleton.h
/home/chang/OpenSource/velox/velox/ch/IO/.gitkeep
/home/chang/OpenSource/velox/velox/ch/Disks/IO/.gitkeep
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/.gitkeep
/home/chang/SourceCode/ClickHouse/port/task/result/001-velox-ch-skeleton-result.md
```

## Steps

- [ ] **Step 1: Confirm clean Velox starting point**

Run:

```bash
cd /home/chang/OpenSource/velox
git --no-pager status --short --branch
```

Expected:

```text
Record current branch and dirty status. Do not require a clean tree, but do not
overwrite unrelated changes.
```

- [ ] **Step 2: Create directory skeleton**

Run:

```bash
mkdir -p \
  /home/chang/OpenSource/velox/velox/ch/Common \
  /home/chang/OpenSource/velox/velox/ch/IO \
  /home/chang/OpenSource/velox/velox/ch/Disks/IO \
  /home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache

touch \
  /home/chang/OpenSource/velox/velox/ch/IO/.gitkeep \
  /home/chang/OpenSource/velox/velox/ch/Disks/IO/.gitkeep \
  /home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/.gitkeep
```

Expected:

```text
Directories exist.
```

- [ ] **Step 3: Add minimal source/header**

Create `/home/chang/OpenSource/velox/velox/ch/Common/FileCacheSkeleton.h`:

```cpp
#pragma once

namespace facebook::velox::ch {

bool fileCacheSkeletonLinked();

} // namespace facebook::velox::ch
```

Create `/home/chang/OpenSource/velox/velox/ch/Common/FileCacheSkeleton.cpp`:

```cpp
#include "velox/ch/Common/FileCacheSkeleton.h"

namespace facebook::velox::ch {

bool fileCacheSkeletonLinked() {
  return true;
}

} // namespace facebook::velox::ch
```

Expected:

```text
Files compile without depending on future FileCache code.
```

- [ ] **Step 4: Add `velox/ch` CMake files**

Create `/home/chang/OpenSource/velox/velox/ch/CMakeLists.txt`:

```cmake
add_subdirectory(Common)
```

Create `/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt`:

```cmake
velox_add_library(
  velox_ch_filecache
  FileCacheSkeleton.cpp
)

velox_link_libraries(
  velox_ch_filecache
  velox_exception
)
```

Expected:

```text
`velox_ch_filecache` is a real build target with one source file.
```

- [ ] **Step 5: Register `velox/ch` in top-level Velox CMake**

Modify `/home/chang/OpenSource/velox/velox/CMakeLists.txt`.

Add this line after `add_subdirectory(common)`:

```cmake
add_subdirectory(ch)
```

Expected nearby context:

```cmake
include_directories(external/xxhash)
add_subdirectory(buffer)
add_subdirectory(common)
add_subdirectory(ch)
add_subdirectory(core)
```

- [ ] **Step 6: Reconfigure CMake**

Run:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_001_velox_ch_skeleton.log 2>&1
```

Expected:

```text
Exit code 0.
The log ends with "Build files have been written to".
```

- [ ] **Step 7: Build the new target**

Run:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_filecache \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_001_velox_ch_skeleton.log 2>&1
```

Expected:

```text
Exit code 0.
The build log shows `velox_ch_filecache` was built or was already up to date.
```

- [ ] **Step 8: Confirm target appears in Ninja target list**

Run:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -t targets \
  | grep '^velox_ch_filecache:' \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/target_task_001_velox_ch_filecache.txt
```

Expected:

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/target_task_001_velox_ch_filecache.txt
contains a `velox_ch_filecache:` target line.
```

- [ ] **Step 9: Write result file**

Create the result directory:

```bash
mkdir -p /home/chang/SourceCode/ClickHouse/port/task/result
```

Write `/home/chang/SourceCode/ClickHouse/port/task/result/001-velox-ch-skeleton-result.md`
with this structure:

````markdown
# Task 001 Result: Add `velox/ch` Skeleton Target

## Status

status: success

## Velox status

```text
<paste `git --no-pager status --short --branch` output here>
```

## Files changed

```text
velox/CMakeLists.txt
velox/ch/CMakeLists.txt
velox/ch/Common/CMakeLists.txt
velox/ch/Common/FileCacheSkeleton.cpp
velox/ch/Common/FileCacheSkeleton.h
velox/ch/IO/.gitkeep
velox/ch/Disks/IO/.gitkeep
velox/ch/Interpreters/FileCache/.gitkeep
```

## Commands run

```text
<paste configure/build/target-list commands>
```

## Generated logs

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_001_velox_ch_skeleton.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_001_velox_ch_skeleton.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/target_task_001_velox_ch_filecache.txt
```

## Build result

```text
<paste one-line build result and target grep output>
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Add the first Common shim under velox/ch/Common, starting with no-op metrics/debug shims or basic aliases.
```
````

If the task is blocked or failed, set `status: blocked` or `status: failed`, replace `None`
under `Blocking errors` with the first actionable error, and still write the result file.

- [ ] **Step 10: Return result path**

Return a short message with:

```text
Path to /home/chang/SourceCode/ClickHouse/port/task/result/001-velox-ch-skeleton-result.md
One-line status
```

Do not commit the Velox changes unless explicitly asked.
