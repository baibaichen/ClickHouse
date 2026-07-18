# Task 004: Correct `StatusFile` Diagnostics

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `/home/chang/OpenSource/velox` and appends
> corrective evidence to one result file under this ClickHouse checkout. Do not
> modify ClickHouse source files. Do not commit or stage either repository.

## Status and authority

The original Task 004 implementation was accepted, then reopened by the
post-acceptance source-contract audit. This document is the only executable
Task 004 plan. The original receipt preserves the implementation and review
history.

The existing `Guards.h`, lock lifecycle, mono/non-mono header registration,
non-throwing destructor, and close-before-unlink behavior remain accepted.
Corrective work is limited to the `StatusFile` diagnostic content and the
build-time revision dependency it requires.

If implementation exposes another dependency or mapping not explicitly covered
below, stop the entire pipeline under `EXECUTION_PROTOCOL.md` and wait for user
review.

## Approved corrective contract

### Full diagnostic fill

Add:

```cpp
static FillFunction writeFullInfo();
```

`FileCache` will use this reusable fill function; callers must not format these
fields themselves. When the fill runs during `StatusFile` construction, it
writes exactly:

```text
PID: <decimal process id>\n
Started at: <local YYYY-MM-DD HH:MM:SS>\n
Revision: <full Velox source revision>\n
```

There are exactly three lines and three trailing newline characters, with no
extra prefix, suffix, or blank line.

### Timestamp semantics

Follow CH source, not the old task wording. CH uses
`LocalDateTime(time(nullptr))`, so `Started at` means local wall-clock time when
the `StatusFile` fill executes during construction. It is not a separately
tracked process-start timestamp.

Use the existing Velox/POSIX pattern:

```text
std::chrono::system_clock::now
std::chrono::system_clock::to_time_t
localtime_r
strftime("%Y-%m-%d %H:%M:%S")
```

If `localtime_r` returns null or `strftime` returns zero, throw
`VeloxRuntimeError` through `VELOX_FAIL`. Do not return a default, UTC value, or
partially formatted timestamp.

### Build revision

Velox has no existing build-revision API. Add a generated private header:

```text
template:
  velox/ch/Common/VeloxBuildRevision.h.in

generated:
  <build>/velox/ch/Common/VeloxBuildRevision.h
```

The template defines:

```cpp
namespace facebook::velox::ch::detail
{
inline constexpr std::string_view kVeloxBuildRevision =
    "@VELOX_BUILD_REVISION_VALUE@";
}
```

`velox/ch/Common/CMakeLists.txt` selects the value:

```text
1. Read the CMake cache string VELOX_BUILD_REVISION.
2. Strip surrounding whitespace.
3. If non-empty, use it.
4. Otherwise find Git and run `git rev-parse HEAD` at configure time with
   PROJECT_SOURCE_DIR as the working directory.
5. Require a non-empty hexadecimal result whose length is 40 (SHA-1) or 64
   (SHA-256); this rejects shortened or descriptive values.
6. If no explicit value and Git cannot provide one, fail configure and explain
   `-DVELOX_BUILD_REVISION=<full-source-revision>`.
```

Do not cache the Git fallback as `VELOX_BUILD_REVISION`; otherwise a later
checkout can silently retain a stale SHA in the build directory. Store the
selected value in the local configure variable
`VELOX_BUILD_REVISION_VALUE`.

The header records HEAD at the last CMake configure. A checkout change by
itself is not a CMake input, so rerun configure before building after HEAD
changes.

Do not hardcode a revision, use `unknown`, use `PYVELOX_VERSION`, omit the
field, call Git at runtime, or expose the generated header as an installed
public API.

After `configure_file`, use:

```cmake
velox_include_directories(
  velox_ch_filecache
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
```

This existing helper selects the real underlying target in both mono and
non-mono builds instead of applying target properties to an alias.

### Complete writes and errors

The current `writePid` ignores the result of raw `write`, which is weaker than
CH's finalized `WriteBuffer` contract. Add one private helper used by both
`writePid` and `writeFullInfo`:

```cpp
void writeAll(int fd, std::string_view contents)
{
    if (folly::writeFull(fd, contents.data(), contents.size()) == -1)
    {
        const int error = errno;
        VELOX_FAIL(
            "Cannot write StatusFile contents: error code {} ({})",
            error,
            std::strerror(error));
    }
}
```

`folly::writeFull` owns EINTR and partial-write handling. Do not add another
retry loop, ignore failure, parse an exception message, or add structured errno
control flow.

An exception from a fill propagates from the constructor. Existing
`folly::File` ownership closes the fd during unwinding.

### Exclusion evidence

Retain the existing same-process double-open test and add a real cross-process
test:

```text
parent constructs StatusFile and holds the lock
parent calls fork
child independently constructs StatusFile on the same path
child exits 0 only when construction throws VeloxRuntimeError
child exits nonzero for unexpected success or exception
parent calls waitpid and requires normal exit status 0
```

The child must call `_exit`, so it does not run destructors for the parent's
inherited objects. Do not describe the same-process test as cross-process
coverage.

## Goal

Correct `StatusFile` so its ownership guard retains the accepted lifecycle and
its content matches the CH `FileCache` diagnostic contract:

```text
real full source revision
construction-time local timestamp
complete write/error propagation
same-process and cross-process exclusion evidence
```

The existing `velox_ch_guards_test` remains the focused test executable.

## Starting point

```text
Velox repository: /home/chang/OpenSource/velox
Required branch:  filecache
Expected HEAD:    c755512a8 Task 003: Correct FileCache common shims
```

Do not require a clean worktree, but record and preserve every pre-existing
dirty file. Stop if the branch is not `filecache`.

## Sources of truth

Read before editing:

```text
/home/chang/SourceCode/ClickHouse/port/task/ENVIRONMENT.md
/home/chang/SourceCode/ClickHouse/port/task/EXECUTION_PROTOCOL.md
/home/chang/SourceCode/ClickHouse/port/1-dependencies/02-filecache-basic-shims-design.md
/home/chang/SourceCode/ClickHouse/port/task/result/003-filecache-basic-common-shims-result.md
/home/chang/SourceCode/ClickHouse/port/task/result/004-filecache-status-and-guards-result.md
/home/chang/SourceCode/ClickHouse/src/Common/StatusFile.h
/home/chang/SourceCode/ClickHouse/src/Common/StatusFile.cpp
/home/chang/SourceCode/ClickHouse/src/Interpreters/FileCache/FileCache.cpp
```

Verified Velox infrastructure:

```text
/usr/local/include/folly/FileUtil.h
  folly::writeFull retries EINTR/partial writes and returns -1 on error.

/home/chang/OpenSource/velox/velox/common/process/Profiler.cpp
  existing localtime_r + strftime local-time pattern.

/home/chang/OpenSource/velox/CMake/VeloxUtils.cmake
  velox_include_directories handles mono and non-mono targets.
```

## File scope

Modify in the Velox checkout:

```text
/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Common/StatusFile.h
/home/chang/OpenSource/velox/velox/ch/Common/StatusFile.cpp
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/StatusFileAndGuardsTest.cpp
```

Create in the Velox checkout:

```text
/home/chang/OpenSource/velox/velox/ch/Common/VeloxBuildRevision.h.in
```

The generated header under the build directory is an artifact, not a tracked
source file.

Append corrective evidence in the ClickHouse checkout:

```text
/home/chang/SourceCode/ClickHouse/port/task/result/004-filecache-status-and-guards-result.md
```

Do not modify `Guards.h`, `ProfileEvents.h`, `velox/ch/CMakeLists.txt`, or
`velox/ch/Interpreters/FileCache/CMakeLists.txt`; their accepted contracts are
unchanged.

## Corrective steps

- [ ] **Step 1: Confirm the baseline**

Run:

```bash
cd /home/chang/OpenSource/velox
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Record the exact HEAD and pre-existing dirty files in the receipt.

- [ ] **Step 2: Add focused RED tests**

Extend `StatusFileAndGuardsTest.cpp`:

| Test | Required proof |
|---|---|
| `WriteFullInfoHasExactThreeLineContract` | while `StatusFile` is alive, read the file; require exactly three newline-terminated lines, exact labels/order, decimal current PID, timestamp regex `[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}`, and revision equal to `detail::kVeloxBuildRevision` |
| `WritePidPropagatesWriteFailure` | fill closes its fd, then invokes `StatusFile::writePid`; construction throws `VeloxRuntimeError` rather than succeeding |
| `WriteFullInfoPropagatesWriteFailure` | same closed-fd setup with `writeFullInfo`; construction throws `VeloxRuntimeError` |
| `SecondProcessOnSamePathThrows` | parent holds the file; forked child attempts a second open; child reports `VeloxRuntimeError` through exit status; parent verifies with `waitpid` |

Retain and run every existing `StatusFileTest` and `GuardsTest`, including
same-process exclusion, close-failure, unlink, all guard lock types, timed lock,
and non-interchangeability.

The test may include the generated private header. Add the Common build
directory to `velox_ch_guards_test` with:

```cmake
target_include_directories(
  velox_ch_guards_test
  PRIVATE
    ${PROJECT_BINARY_DIR}/velox/ch/Common
)
```

Do not make the generated header public.

No test may use `sleep`, `GTEST_SKIP`, or `DISABLED_`.

- [ ] **Step 3: Prove RED**

Reconfigure the default build and build `velox_ch_guards_test`:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_BUILD_TESTING=ON \
  -UVELOX_BUILD_REVISION \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_004_corrective.log 2>&1

/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_guards_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_corrective_red.log 2>&1
```

Require failure caused by the missing `writeFullInfo` or generated revision
header. If the build succeeds, run only the four new tests and require at least
one contract failure. Record the first relevant diagnostic and log.

- [ ] **Step 4: Generate the build revision**

In `velox/ch/Common/CMakeLists.txt`:

1. Define `VELOX_BUILD_REVISION` as a cache string without forcing a value.
2. Copy and strip it into local `VELOX_BUILD_REVISION_VALUE`.
3. When empty, `find_package(Git QUIET)` and run `${GIT_EXECUTABLE} rev-parse
   HEAD` in `${PROJECT_SOURCE_DIR}`.
4. Fail configure if Git is unavailable, the command fails, or output is empty.
5. Fail configure unless the selected value is 40 or 64 hexadecimal
   characters.
6. Run `configure_file` with `@ONLY`.
7. Add `${CMAKE_CURRENT_BINARY_DIR}` through
   `velox_include_directories`.

Create `VeloxBuildRevision.h.in` with the namespace/value shown in the approved
contract. Do not add the template or generated header to the public `HEADERS`
file set.

- [ ] **Step 5: Implement exact fills**

In `StatusFile.h`, add only `writeFullInfo`.

In `StatusFile.cpp`:

1. Include the generated header, `folly/FileUtil.h`, and required standard
   headers.
2. Add private `writeAll` exactly once and use it from both fills.
3. Add private local-time formatter using the approved clock and format.
4. Keep `writePid` output byte-compatible with CH: decimal PID without newline.
5. Implement `writeFullInfo` with exact labels/order/newlines and
   `detail::kVeloxBuildRevision`.
6. Leave constructor lock/truncate/seek/fill ordering and destructor unchanged.

- [ ] **Step 6: Verify revision selection paths**

First reconfigure the default build after adding the CMake/header
implementation:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_BUILD_TESTING=ON \
  -UVELOX_BUILD_REVISION \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_004_corrective_green.log 2>&1
```

Then verify the default checkout fallback:

```bash
git -C /home/chang/OpenSource/velox rev-parse HEAD
grep -A1 'kVeloxBuildRevision' \
  /home/chang/OpenSource/velox/cmake-build-debug-gcc13/velox/ch/Common/VeloxBuildRevision.h
```

Require the generated value to equal the full Git HEAD.

Explicit override: configure a non-mono build with a known 40-character
hexadecimal revision:

```bash
mkdir -p /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-nonmono

/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_BUILD_TESTING=ON \
  -DVELOX_MONO_LIBRARY=OFF \
  -DVELOX_BUILD_REVISION=0123456789abcdef0123456789abcdef01234567 \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-nonmono \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-nonmono/configure_task_004_corrective.log 2>&1
```

Require the generated header to contain exactly the explicit value.

Invalid explicit value: configure a separate probe build with
`-DVELOX_BUILD_REVISION=not-a-revision` and require configure to fail:

```bash
mkdir -p /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-invalid-revision

if /usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_BUILD_TESTING=ON \
  -DVELOX_BUILD_REVISION=not-a-revision \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-invalid-revision \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-invalid-revision/configure_task_004_invalid_revision.log 2>&1
then
  echo "ERROR: invalid revision unexpectedly configured"
  exit 1
fi
```

Require the log to contain the revision validation diagnostic.

- [ ] **Step 7: Build and run both gates**

Default mono build:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_guards_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_corrective.log 2>&1

ctest \
  --test-dir /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -R '^velox_ch_guards_test$' \
  --output-on-failure \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_004_corrective.log 2>&1
```

Non-mono explicit-revision build:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-nonmono \
  velox_ch_guards_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-nonmono/build_task_004_corrective.log 2>&1

ctest \
  --test-dir /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-nonmono \
  -R '^velox_ch_guards_test$' \
  --output-on-failure \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task004-nonmono/test_task_004_corrective.log 2>&1
```

Do not pass `-j`. Every command must exit 0 and every test must pass with no
skips. Use a log-analysis subagent for every build/test log.

- [ ] **Step 8: Inspect scope**

Run `git diff --check`, `git status --short`, and inspect only the six declared
source files. Require no unrelated, staged, or committed changes.

- [ ] **Step 9: Append the corrective receipt**

Append, do not overwrite, these sections:

```markdown
## Corrective diagnostic contract

status: success

## Corrective baseline and scope

<branch, starting HEAD, pre-existing dirty files, task-owned files>

## Corrective RED evidence

<first expected failure and log>

## Revision evidence

<Git fallback value, explicit override value, generated-header checks>

## Corrective test results

<one result for every new and retained test; mono and non-mono totals>

## Corrective review

<independent self-review findings and resolutions>

## Blocking errors

None
```

If blocked or failed, record that status, first actionable error, and log path,
then stop without claiming success.

## Acceptance gate

Task 004 is corrected only when:

```text
writeFullInfo emits exactly the CH three-line format
Started at is construction-time local YYYY-MM-DD HH:MM:SS
Revision is explicit or full configure-time Git HEAD
missing/invalid revision never degrades to a fallback value
writePid and writeFullInfo use complete writes and propagate failure
same-process and forked cross-process exclusion tests pass
all accepted lock/destructor/unlink/guard tests still pass
default mono and explicit-revision non-mono gates both pass
no Guards.h or unrelated code changed
the receipt contains RED, revision, test, and review evidence
```

## Explicit exclusions

Do not implement:

```text
changes to Guards.h or lock ordering
real logging
structured errno exceptions
process-start timestamp tracking
runtime Git invocation
public Velox version API
FileCache production caller wiring
FileCache algorithms
Gluten integration
```

Production caller wiring remains part of the later `FileCache` migration.
