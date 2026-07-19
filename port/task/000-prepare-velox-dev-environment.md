# Task 000: Prepare Velox Development Environment

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task can run
> in parallel with ClickHouse `FileCache` design review because it only touches the
> Velox checkout/build directory under `<velox_repo>`.

## Goal

Configure and verify the existing Velox Debug build directory that later
`FileCache` port tasks will use.

## Files and outputs

Create or update only these generated artifacts:

```text
<velox_build_dir>/configure_filecache_env.log
<velox_build_dir>/cache_filecache_env.txt
<velox_build_dir>/targets_filecache_env.txt
<clickhouse_repo>/port/task/result/000-prepare-velox-dev-environment-result.md
```

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm Velox checkout and tools**

Run:

```bash
cd <velox_repo>
git --no-pager status --short --branch
<cmake> --version
<ninja> --version
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
mkdir -p <velox_build_dir>
```

Expected:

```text
The command exits with code 0.
```

- [ ] **Step 3: Configure CMake**

Run:

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`, redirecting output to
`<velox_build_dir>/configure_filecache_env.log`.

Expected:

```text
CMake generation completes successfully.
<velox_build_dir>/build.ninja exists.
```

- [ ] **Step 4: Capture the generated build configuration**

Run:

```bash
<cmake> -LAH -N <velox_build_dir> \
  > <velox_build_dir>/cache_filecache_env.txt

grep -E '^(CMAKE_BUILD_TYPE|CMAKE_MAKE_PROGRAM|VELOX_ENABLE_BENCHMARKS|CMAKE_C_COMPILER|CMAKE_CXX_COMPILER):' \
  <velox_build_dir>/cache_filecache_env.txt
```

Expected:

```text
CMAKE_BUILD_TYPE:STRING=Debug
CMAKE_MAKE_PROGRAM:FILEPATH=<ninja>
VELOX_ENABLE_BENCHMARKS:BOOL=ON
CMAKE_C_COMPILER and CMAKE_CXX_COMPILER are visible for later debugging.
```

> **Note for `root-oss`:** CMake may auto-discover the Ninja executable and
> populate `CMAKE_MAKE_PROGRAM` without an explicit `-DCMAKE_MAKE_PROGRAM`
> flag on the configure command line. The cached resolved path must still equal
> the selected profile's `<ninja>` value. If the two differ, reconfigure with
> `-DCMAKE_MAKE_PROGRAM=<ninja>` appended to the profile recipe.

- [ ] **Step 5: Capture available Ninja targets**

Run:

```bash
<ninja> \
  -C <velox_build_dir> \
  -t targets \
  > <velox_build_dir>/targets_filecache_env.txt

wc -l <velox_build_dir>/targets_filecache_env.txt
```

Expected:

```text
targets_filecache_env.txt is non-empty.
```

- [ ] **Step 6: Write result file**

Create the result directory:

```bash
mkdir -p <clickhouse_repo>/port/task/result
```

Write `<clickhouse_repo>/port/task/result/000-prepare-velox-dev-environment-result.md`
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
<velox_build_dir>/configure_filecache_env.log
<velox_build_dir>/cache_filecache_env.txt
<velox_build_dir>/targets_filecache_env.txt
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
Path to <clickhouse_repo>/port/task/result/000-prepare-velox-dev-environment-result.md
One-line status
```

Do not start a full build in this task unless explicitly asked. The next handoff
task should choose a specific target after reading `targets_filecache_env.txt`.
