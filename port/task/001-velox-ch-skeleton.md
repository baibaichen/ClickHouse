# Task 001: Add `velox/ch` Skeleton Target

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task only
> touches the Velox checkout under `<velox_repo>`. Do not modify
> ClickHouse source files for this task.

## Goal

Create the initial `velox/ch` directory tree and a minimal `velox_ch_filecache`
CMake target that builds successfully. This unblocks later tasks that add
`FileCache` shims under `velox/ch/Common`, `velox/ch/IO`, and
`velox/ch/Interpreters/FileCache`.

## Design references

Read these before editing Velox:

```text
<clickhouse_repo>/port/00-filecache-velox-migration.md
<clickhouse_repo>/port/01-filecache-port-order-design.md
<clickhouse_repo>/port/task/ENVIRONMENT.md
```

## Files

Modify:

```text
<velox_repo>/velox/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/CMakeLists.txt
<velox_repo>/velox/ch/Common/CMakeLists.txt
<velox_repo>/velox/ch/Common/FileCacheSkeleton.cpp
<velox_repo>/velox/ch/Common/FileCacheSkeleton.h
<velox_repo>/velox/ch/IO/.gitkeep
<velox_repo>/velox/ch/Disks/IO/.gitkeep
<velox_repo>/velox/ch/Interpreters/FileCache/.gitkeep
<clickhouse_repo>/port/task/result/001-velox-ch-skeleton-result.md
```

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm clean Velox starting point**

Run:

```bash
cd <velox_repo>
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
  <velox_repo>/velox/ch/Common \
  <velox_repo>/velox/ch/IO \
  <velox_repo>/velox/ch/Disks/IO \
  <velox_repo>/velox/ch/Interpreters/FileCache

touch \
  <velox_repo>/velox/ch/IO/.gitkeep \
  <velox_repo>/velox/ch/Disks/IO/.gitkeep \
  <velox_repo>/velox/ch/Interpreters/FileCache/.gitkeep
```

Expected:

```text
Directories exist.
```

- [ ] **Step 3: Add minimal source/header**

Create `<velox_repo>/velox/ch/Common/FileCacheSkeleton.h`:

```cpp
#pragma once

namespace facebook::velox::ch {

bool fileCacheSkeletonLinked();

} // namespace facebook::velox::ch
```

Create `<velox_repo>/velox/ch/Common/FileCacheSkeleton.cpp`:

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

Create `<velox_repo>/velox/ch/CMakeLists.txt`:

```cmake
add_subdirectory(Common)
```

Create `<velox_repo>/velox/ch/Common/CMakeLists.txt`:

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

Modify `<velox_repo>/velox/CMakeLists.txt`.

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

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`, redirecting output to
`<velox_build_dir>/configure_task_001_velox_ch_skeleton.log`.

Expected:

```text
Exit code 0.
The log ends with "Build files have been written to".
```

- [ ] **Step 7: Build the new target**

Run:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache \
  > <velox_build_dir>/build_task_001_velox_ch_skeleton.log 2>&1
```

Expected:

```text
Exit code 0.
The build log shows `velox_ch_filecache` was built or was already up to date.
```

- [ ] **Step 8: Confirm target appears in Ninja target list**

Run:

```bash
<ninja> \
  -C <velox_build_dir> \
  -t targets \
  | grep '^velox_ch_filecache:' \
  > <velox_build_dir>/target_task_001_velox_ch_filecache.txt
```

Expected:

```text
<velox_build_dir>/target_task_001_velox_ch_filecache.txt
contains a `velox_ch_filecache:` target line.
```

- [ ] **Step 9: Write result file**

Create the result directory:

```bash
mkdir -p <clickhouse_repo>/port/task/result
```

Write `<clickhouse_repo>/port/task/result/001-velox-ch-skeleton-result.md`
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
<velox_build_dir>/configure_task_001_velox_ch_skeleton.log
<velox_build_dir>/build_task_001_velox_ch_skeleton.log
<velox_build_dir>/target_task_001_velox_ch_filecache.txt
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
Path to <clickhouse_repo>/port/task/result/001-velox-ch-skeleton-result.md
One-line status
```

Do not commit the Velox changes unless explicitly asked.
