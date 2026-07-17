# Task 004 Result: Add `StatusFile` and `Guards.h`

## Worker attempt 1

```text
worker_status: ready_for_controller
task: 004
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache` | `4bea8d15e` (Task 003: Add `FileCache` common shims) | clean (`git status --short` empty) |

## Files changed

```text
/home/chang/OpenSource/velox/velox/ch/CMakeLists.txt (modified)
/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt (modified)
/home/chang/OpenSource/velox/velox/ch/Common/ProfileEvents.h (modified)
/home/chang/OpenSource/velox/velox/ch/Common/StatusFile.h (new)
/home/chang/OpenSource/velox/velox/ch/Common/StatusFile.cpp (new)
/home/chang/OpenSource/velox/velox/ch/Interpreters/CMakeLists.txt (new)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/CMakeLists.txt (new)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/Guards.h (new)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt (new)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/StatusFileAndGuardsTest.cpp (new)
/home/chang/SourceCode/ClickHouse/port/task/result/004-filecache-status-and-guards-result.md (this file)
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| Reconfigure (baseline, pre red-build test) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_004_guards.log` (first run) |
| Red build (`velox_ch_guards_test` before StatusFile.h/.cpp and Guards.h exist) | 1 (expected failure) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_red.log` |
| Reconfigure (after adding StatusFile/Guards implementation) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_004_guards.log` (second run) |
| Build `velox_ch_guards_test` (first pass, before review fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_guards.log` (overwritten by final rerun below) |
| `ctest -R '^velox_ch_guards_test$'` (first pass, before review fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_004_guards.log` (overwritten by final rerun below) |
| Reproduce non-mono configure defect (`-DVELOX_MONO_LIBRARY=OFF`, scratch build dir, ad hoc verification only, not part of the declared gate) | 1 (confirmed defect) | ad hoc scratch log, deleted after use; not a declared task log |
| Reconfigure non-mono after review fix (ad hoc verification only) | 0 | ad hoc scratch log, deleted after use; not a declared task log |
| Reconfigure (final rerun, mono mode, after review fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_004_guards.log` (third run, appended) |
| Build `velox_ch_guards_test` (final rerun, after review fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_guards.log` |
| `ctest -R '^velox_ch_guards_test$'` (final rerun, after review fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_004_guards.log` |
| `git --no-pager diff --check` (final) | 0 | (inline, see below) |

## Acceptance evidence

```text
test count: 1 ctest entry (velox_ch_guards_test), containing 11 gtest cases
  (StatusFileTest.WritePidFillFunctionDoesNotThrow,
  StatusFileTest.EmptyFillFunctionDoesNotThrow,
  StatusFileTest.SecondInstanceOnSamePathThrows,
  StatusFileTest.DestructorUnlinksPath,
  StatusFileTest.AfterDestructionNewInstanceSucceeds,
  GuardsTest.CachePriorityGuardLockTypes,
  GuardsTest.CachePriorityGuardTryLockMayFail,
  GuardsTest.CacheStateGuardTryLockFor,
  GuardsTest.CacheStateGuardTryLockForFailsWhenHeld,
  GuardsTest.LockTypesAreNotInterchangeable,
  GuardsTest.AllGuardsLockSuccessfully)
failed tests: 0
skipped/disabled tests: 0
benchmark result: not required for this task
git diff --check: clean, no whitespace errors reported
```

Red-build confirmation (before StatusFile/Guards existed): build failed with
`velox/ch/Interpreters/FileCache/tests/StatusFileAndGuardsTest.cpp:17:10:
fatal error: velox/ch/Common/StatusFile.h: No such file or directory`,
confirming the test was genuinely failing prior to implementation.

Final `ctest` output (mono-mode gate, after implementation and the review
fix below): `100% tests passed, 0 tests failed out of 1`. Running the test
binary directly confirmed all 11 named gtest cases execute real assertions
(including an actual `VeloxRuntimeError` thrown and logged for the
second-instance double-lock case, and a real async-timeout failure path for
`CacheStateGuardTryLockForFailsWhenHeld`).

`git --no-pager status --short` after implementation (Velox repo):
```text
 M velox/ch/CMakeLists.txt
 M velox/ch/Common/CMakeLists.txt
 M velox/ch/Common/ProfileEvents.h
?? velox/ch/Common/StatusFile.cpp
?? velox/ch/Common/StatusFile.h
?? velox/ch/Interpreters/
```
Only files inside the declared file scope were touched; nothing else in the
Velox worktree is dirty. Changes remain unstaged and uncommitted.

## Worker review

```text
review subagent: code-review (read-only, one invocation) over the complete
  task-owned diff (full diffs of the three modified tracked files plus full
  contents of all seven new files), given the Task 004 spec, referenced
  design docs, the prior accepted Task 003 receipt, the ClickHouse
  behavioral references (src/Common/StatusFile.h/.cpp,
  src/Interpreters/FileCache/Guards.h), and build/test outcomes.

findings:
  1. (blocking) velox/ch/Interpreters/FileCache/CMakeLists.txt registered
     Guards.h into the "HEADERS" FILE_SET with INTERFACE scope, while
     Common/CMakeLists.txt's velox_add_library() call (now that
     StatusFile.cpp makes velox_ch_filecache a real, non-INTERFACE library)
     registers the same "HEADERS" FILE_SET name on the same target with
     PUBLIC scope in non-mono builds. CMake requires one FILE_SET name to
     keep a single consistent scope across all target_sources() calls on a
     target, so `-DVELOX_MONO_LIBRARY=OFF` configure fails with:
       "target_sources Scope INTERFACE for file set "HEADERS" does not
        match original scope PUBLIC"
     This defect was present verbatim in the task spec's own literal CMake
     code blocks and was never exercised by the task's own build/test gate
     because VELOX_MONO_LIBRARY defaults to ON and the task's prescribed
     commands never pass -DVELOX_MONO_LIBRARY=OFF. Reproduced independently
     against this exact worktree with a scratch, non-declared build
     directory before and after the fix.
  2. (informational, non-blocking) All literal-code deliverables (StatusFile.h,
     StatusFile.cpp, Guards.h, the test file, and the rest of the CMake
     tree) were otherwise verified byte-for-byte identical to the task
     spec's mandated code blocks, with correct license headers in every new
     file. folly::File move-assignment safety
     (`file_ = folly::File(rawFd, true)`), destructor close-before-unlink
     ordering, throwFileCacheException -> VeloxRuntimeError propagation, and
     CacheStateGuard::tryLockFor's timed_mutex timeout semantics were all
     independently confirmed correct.

resolutions:
  1. Changed the INTERFACE keyword to PUBLIC in the target_sources() call in
     velox/ch/Interpreters/FileCache/CMakeLists.txt (the only change needed;
     this file is inside the task's declared file scope) and updated the
     adjacent comment to explain why PUBLIC is required once
     velox_ch_filecache carries a compiled source. Reconfigured with
     `-DVELOX_MONO_LIBRARY=OFF` in a scratch, non-declared build directory
     and confirmed configure now succeeds (exit 0, no CMake FILE_SET
     error); the scratch directory was deleted afterward since it is not a
     declared task artifact. Reran the full mono-mode acceptance gate
     (reconfigure, build velox_ch_guards_test, ctest) from a clean state
     after the fix; all three steps still pass with the same 11/11 test
     outcome as before the fix, confirming no regression.
  2. No action required; informational only.

unresolved findings: None.
```

## Blockers

```text
None
```

## Worker declaration

```text
Only Task 004 was attempted.
Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Controller review 1

```text
controller_status: changes_requested
task: 004
```

## Review evidence

```text
scope review:
  Task-owned changes are confined to the declared Velox files and this
  receipt. No unrelated dirty files were found.

implementation review:
  StatusFile and guard behavior otherwise matches Task 004, including
  lock-before-truncate, close-before-unlink ordering, distinct guard lock
  types, and the corrected PUBLIC CMake FILE_SET scope.

cross-task architecture review:
  The new StatusFile and Guards interfaces match the accepted dependency
  design and later task call sites.

log and test review:
  The red build failed for the expected missing StatusFile header. The final
  target builds and all 11 focused gtests run through the registered ctest
  entry with no failures or skips.

unresolved findings:
  StatusFile::~StatusFile calls folly::File::close, which can throw on a
  close(2) failure. Because the destructor is implicitly noexcept, this can
  terminate the process and violates StatusFile.h's explicit non-throwing
  destructor contract. Folly provides closeNoThrow for this path.
```

## Required changes

```text
1. Use file_.closeNoThrow() before unlink in StatusFile::~StatusFile.
2. Add focused evidence that StatusFile destruction remains non-throwing.
3. Rerun the affected build and test gates.

The authoritative Task 004 literal implementation has been corrected from
file_.close() to file_.closeNoThrow() before redispatch.
```

## Commits

| Repository | Commit |
|---|---|

## Worker attempt 2

```text
worker_status: ready_for_controller
task: 004
```

## Repository baselines (attempt 2)

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache` | `4bea8d15e` (Task 003: Add `FileCache` common shims) | dirty, matching attempt 1's ending state exactly: `M velox/ch/CMakeLists.txt`, `M velox/ch/Common/CMakeLists.txt`, `M velox/ch/Common/ProfileEvents.h`, `?? velox/ch/Common/StatusFile.cpp`, `?? velox/ch/Common/StatusFile.h`, `?? velox/ch/Interpreters/` (Guards.h, both CMake files, and the test tree, including the PUBLIC FILE_SET fix from attempt 1). No files outside Task 004's declared scope were dirty. |

## Files changed (attempt 2)

Only two files were modified from attempt 1's state, both already inside the
declared file scope:

```text
/home/chang/OpenSource/velox/velox/ch/Common/StatusFile.cpp (modified: closeNoThrow fix)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/StatusFileAndGuardsTest.cpp (modified: new regression test)
/home/chang/SourceCode/ClickHouse/port/task/result/004-filecache-status-and-guards-result.md (this attempt-2 section appended)
```

All other attempt-1 files (both CMakeLists.txt modifications, ProfileEvents.h,
StatusFile.h, Guards.h, and the tests/CMakeLists.txt, including the
already-correct `PUBLIC` FILE_SET scope fix) are untouched in this attempt.

### Change 1: `StatusFile::~StatusFile` non-throwing close

In `velox/ch/Common/StatusFile.cpp`, changed:

```cpp
file_.close();
```

to:

```cpp
file_.closeNoThrow();
```

with an updated comment explaining that `close()` throws `std::system_error`
on a `close(2)` failure, which would escape this implicitly `noexcept`
destructor and call `std::terminate()`, violating `StatusFile.h`'s documented
"destructor ignores close and unlink errors (cannot throw)" contract.
`closeNoThrow()` swallows the error, returns `bool`, and still transfers fd
ownership away from `file_` so the member's own destructor does not
double-close.

### Change 2: focused non-throwing destruction evidence

Added `StatusFileTest.DestructorDoesNotThrowWhenCloseFails` to
`StatusFileAndGuardsTest.cpp` (plus `#include <unistd.h>` for `::close`).
`StatusFile`'s `FillFunction` (`std::function<void(int fd)>`) is invoked by
the constructor as `fill(file_.fd())` with the real underlying fd, after
`open`, `try_lock`, `ftruncate`, and `lseek` have all already succeeded. The
new test supplies a `FillFunction` that calls `::close(fd)` itself during
construction, so the constructor completes normally, but the destructor's
later `file_.closeNoThrow()` call then hits `close(2)` on an already-closed fd
(`EBADF`). The test wraps construction/destruction in `EXPECT_NO_THROW` and
confirms the status file path is still unlinked afterward, giving direct
behavioral evidence that destruction survives a failing `close(2)` rather than
only asserting the destructor's static `noexcept` signature.

## Commands and outcomes (attempt 2)

| Command purpose | Exit code | Log |
|---|---:|---|
| Build `velox_ch_guards_test` (first pass, before regression-proof detour) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_guards_attempt2.log` |
| `ctest -R '^velox_ch_guards_test$'` (first pass) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_004_guards_attempt2.log` |
| `--gtest_list_tests` (enumerate all 12 gtest cases) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/gtest_list_task_004_attempt2.log` |
| Direct full gtest run (first pass, 12/12 passed) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/gtest_run_task_004_attempt2.log` |
| Regression-proof: temporarily reverted `closeNoThrow()` back to `close()`, rebuilt (ad hoc, same declared build dir, output captured for evidence) | 0 (build) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_regression_check.log` |
| Regression-proof: ran only `StatusFileTest.DestructorDoesNotThrowWhenCloseFails` against the reverted (buggy) `close()` | 134 (`SIGABRT`, expected — proves the test catches the controller-flagged defect) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/gtest_regression_proof.log` |
| Restored the `closeNoThrow()` fix; rebuilt `velox_ch_guards_test` (final) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_guards_attempt2_final.log` |
| `ctest -R '^velox_ch_guards_test$'` (final, fix restored) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_004_guards_attempt2_final.log` |
| Direct full gtest run (final, fix restored, 12/12 passed) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/gtest_run_task_004_attempt2_final.log` |
| `git --no-pager diff --check` (final) | 0 | (inline, see below) |

Note on the regression-proof detour: after restoring the fix by `mv`-ing back
a pre-edit backup copy of `StatusFile.cpp`, the restored file's mtime was
older than the already-compiled buggy object file, so the first rebuild
silently skipped recompilation (ninja saw no newer input). This was caught
before finalizing by comparing the rebuild log (no compile line for
`StatusFile.cpp.o`) against the still-failing regression test, `touch`-ing the
restored file to force ninja to see it as changed, and rebuilding again; the
final build log above shows `StatusFile.cpp.o` actually recompiling and
relinking, and the final test run confirms the fix is in effect.

## Acceptance evidence (attempt 2)

```text
test count: 1 ctest entry (velox_ch_guards_test), containing 12 gtest cases
  (the 11 from attempt 1 plus the new
  StatusFileTest.DestructorDoesNotThrowWhenCloseFails)
failed tests: 0 (final state, fix in place)
skipped/disabled tests: 0
benchmark result: not required for this task
git diff --check: clean, no whitespace errors reported
```

Final direct gtest run (fix in place): `[==========] 12 tests from 2 test
suites ran. (2 ms total)` / `[  PASSED  ] 12 tests.`, with
`StatusFileTest.DestructorDoesNotThrowWhenCloseFails` explicitly listed and
passing among them.

Regression proof (fix temporarily reverted to `close()`, evidence only, not
part of the final accepted state): running the new test alone produced
`terminate called after throwing an instance of 'std::system_error'` /
`what():  close() failed: Bad file descriptor` and the process aborted
(`SIGABRT`, ctest reported "Subprocess aborted***Exception"). This confirms
the new test reproduces exactly the failure mode the controller flagged in
attempt 1, and that `closeNoThrow()` is what prevents it.

`git --no-pager status --short` after attempt 2 (Velox repo), unchanged from
attempt 1's ending state:
```text
 M velox/ch/CMakeLists.txt
 M velox/ch/Common/CMakeLists.txt
 M velox/ch/Common/ProfileEvents.h
?? velox/ch/Common/StatusFile.cpp
?? velox/ch/Common/StatusFile.h
?? velox/ch/Interpreters/
```
Only files inside the declared file scope are dirty; nothing else in the
Velox worktree changed. Changes remain unstaged and uncommitted.

## Worker review (attempt 2)

```text
review subagent: code-review (read-only, one invocation) over the complete
  Task 004 attempt-2 diff: the closeNoThrow() fix in StatusFile.cpp, the new
  StatusFileTest.DestructorDoesNotThrowWhenCloseFails test, full current
  contents of StatusFile.h/.cpp, Guards.h, and both FileCache CMakeLists.txt
  files, plus git status/diff --stat confirmation of scope, and all
  build/ctest/gtest/regression-proof evidence above. Given the Task 004 spec,
  the prior attempt-1 receipt including the controller's required-changes
  list, and the ClickHouse behavioral reference.

findings:
  1. (informational, non-blocking) The new test's technique of closing the
     real fd early via the FillFunction callback and relying on the fd number
     not being reassigned before the destructor's closeNoThrow() call is
     race-free only because the single-threaded gtest binary opens no other
     files concurrently at that instant; this is standard, acceptable
     fd-failure-injection practice for a single-threaded unit test and not a
     defect to change.

resolutions:
  1. No action required; informational only, explicitly confirmed by the
     regression-proof log to reliably reproduce the intended failure mode.

unresolved findings: None. Reviewer's overall verdict: "This attempt's
  changes are correct and sufficient. The controller's required fix is
  implemented properly, backed by real evidence (build, ctest, full gtest
  run, and a genuine before/after regression proof), and no unresolved
  concerns remain."
```

## Blockers (attempt 2)

```text
None
```

## Worker declaration (attempt 2)

```text
Only Task 004 was attempted (rework of the controller's required changes).
Changes are unstaged and uncommitted.
The worker stopped after appending this attempt-2 section to the receipt.
```

## Controller review 2

```text
controller_status: accepted
task: 004
```

## Review evidence

```text
scope review:
  Both attempts remained inside the declared Task 004 scope. The ClickHouse
  changes are limited to the corrected Task 004 specification and this receipt.

implementation review:
  StatusFile uses lock-before-truncate and non-throwing close-before-unlink.
  The typed guards preserve lock separation and the intended lock hierarchy.
  CMake uses the required PUBLIC HEADERS file-set scope for non-mono builds.

cross-task architecture review:
  StatusFile, Guards, and ProfileEvents contracts match the accepted dependency
  design and the interfaces consumed by Tasks 005-014.

log and test review:
  Attempt 2 added a close-failure regression test that aborts with the old
  throwing close and passes with closeNoThrow. The final Controller build and
  ctest run passed the registered velox_ch_guards_test with no failures or
  skips:
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_004_controller_final.log
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_004_controller_final.log

unresolved findings:
  None.
```

## Required changes

```text
None.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `f948fb6a4` |
