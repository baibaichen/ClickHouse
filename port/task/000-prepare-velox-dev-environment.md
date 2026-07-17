# Task 000: Prepare Velox Development Environment

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task can run
> in parallel with ClickHouse `FileCache` design review because it only touches the
> Velox checkout/build directory under `/home/chang/OpenSource/velox`.

## Goal

Configure and verify the existing Velox Debug build directory that later
`FileCache` port tasks will use.

## Files and outputs

Create or update only these generated artifacts:

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_filecache_env.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/cache_filecache_env.txt
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/targets_filecache_env.txt
/home/chang/SourceCode/ClickHouse/port/task/result/000-prepare-velox-dev-environment-result.md
```

## Steps

- [ ] **Step 1: Confirm Velox checkout and tools**

Run:

```bash
cd /home/chang/OpenSource/velox
git --no-pager status --short --branch
/usr/bin/cmake --version
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja --version
```

Expected:

```text
The Velox checkout exists.
CMake prints a version.
Ninja prints a version.
```

- [ ] **Step 2: Create the build directory**

Run:

```bash
mkdir -p /home/chang/OpenSource/velox/cmake-build-debug-gcc13
```

Expected:

```text
The command exits with code 0.
```

- [ ] **Step 3: Configure CMake**

Run:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  2>&1 | tee /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_filecache_env.log
```

Expected:

```text
CMake generation completes successfully.
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build.ninja exists.
```

- [ ] **Step 4: Capture the generated build configuration**

Run:

```bash
/usr/bin/cmake -LAH -N /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/cache_filecache_env.txt

grep -E '^(CMAKE_BUILD_TYPE|CMAKE_MAKE_PROGRAM|VELOX_ENABLE_BENCHMARKS|CMAKE_C_COMPILER|CMAKE_CXX_COMPILER):' \
  /home/chang/OpenSource/velox/cmake-build-debug-gcc13/cache_filecache_env.txt
```

Expected:

```text
CMAKE_BUILD_TYPE:STRING=Debug
CMAKE_MAKE_PROGRAM:FILEPATH=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja
VELOX_ENABLE_BENCHMARKS:BOOL=ON
CMAKE_C_COMPILER and CMAKE_CXX_COMPILER are visible for later debugging.
```

- [ ] **Step 5: Capture available Ninja targets**

Run:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -t targets \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/targets_filecache_env.txt

wc -l /home/chang/OpenSource/velox/cmake-build-debug-gcc13/targets_filecache_env.txt
```

Expected:

```text
targets_filecache_env.txt is non-empty.
```

- [ ] **Step 6: Write result file**

Create the result directory:

```bash
mkdir -p /home/chang/SourceCode/ClickHouse/port/task/result
```

Write `/home/chang/SourceCode/ClickHouse/port/task/result/000-prepare-velox-dev-environment-result.md`
with this structure:

````markdown
# Task 000 Result: Prepare Velox Development Environment

## Status

status: success

## Velox status

```text
<paste `git --no-pager status --short --branch` output here>
```

## Commands run

```text
<paste the commands that were run>
```

## Generated files

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_filecache_env.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/cache_filecache_env.txt
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/targets_filecache_env.txt
```

## CMake cache summary

```text
<paste the grep output from Step 4>
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Read targets_filecache_env.txt and choose the smallest target that can compile a new velox/ch/Common shim.
```
````

If the task is blocked or failed, set `status: blocked` or `status: failed`, replace `None`
under `Blocking errors` with the first actionable error, and still write the result file.

- [ ] **Step 7: Return result path**

Return a short message with:

```text
Path to /home/chang/SourceCode/ClickHouse/port/task/result/000-prepare-velox-dev-environment-result.md
One-line status
```

Do not start a full build in this task unless explicitly asked. The next handoff
task should choose a specific target after reading `targets_filecache_env.txt`.
