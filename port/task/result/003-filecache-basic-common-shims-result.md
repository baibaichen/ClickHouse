# Task 003 Result: Add `FileCache` Basic Common Shims

## Worker attempt 1

```text
worker_status: ready_for_controller
task: 003
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache` | `bf379041f` | clean (`git status --short` empty) |

## Files changed

```text
/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt (modified)
/home/chang/OpenSource/velox/velox/ch/Common/ClickHouseAliases.h (new)
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheException.h (new)
/home/chang/OpenSource/velox/velox/ch/Common/SharedMutex.h (new)
/home/chang/OpenSource/velox/velox/ch/Common/logger_useful.h (new)
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheFilesystem.h (new)
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheBoundedQueue.h (new)
/home/chang/OpenSource/velox/velox/ch/Common/tests/CMakeLists.txt (new)
/home/chang/OpenSource/velox/velox/ch/Common/tests/BasicShimsTest.cpp (new)
/home/chang/SourceCode/ClickHouse/port/task/result/003-filecache-basic-common-shims-result.md (this file)
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| Configure (baseline, pre-shims) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_003_basic_common.log` (first run) |
| Red build (`velox_ch_common_test` before shim headers exist) | 1 (expected failure) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_red.log` |
| Reconfigure (after adding shim headers) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_003_basic_common.log` (final run) |
| Build `velox_ch_common_test` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_basic_common.log` |
| `ctest -R '^velox_ch_common_test$'` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_basic_common.log` |
| `git --no-pager diff --check` | 0 | (inline, see below) |

## Acceptance evidence

```text
test count: 1 ctest entry (velox_ch_common_test), containing 10 gtest cases
  (ClickHouseAliasesTest.PrimitiveTypes, LoggerUsefulTest.ArgumentsAreNotEvaluated,
  FileCacheExceptionTest.ThrowsVeloxRuntimeError, SharedMutexTest.SupportsExclusiveAndSharedLocks,
  FileCacheFilesystemTest.LocalFilesystemAlias, FileCacheFilesystemTest.FilesystemErrorKeepsContext,
  FileCacheBoundedQueueTest.CapacityZeroTryPushFails, FileCacheBoundedQueueTest.FinishDrainsQueuedValues,
  FileCacheBoundedQueueTest.FinishReleasesBlockedConsumer, FileCacheBoundedQueueTest.FinishReleasesBlockedProducer)
failed tests: 0
skipped/disabled tests: 0
benchmark result: not required for this task
git diff --check: clean, no whitespace errors reported
```

Red-build confirmation (before shims existed): build failed with
`velox/ch/Common/tests/BasicShimsTest.cpp:16:10: fatal error:
velox/ch/Common/ClickHouseAliases.h: No such file or directory`, confirming
the test was genuinely failing prior to implementation.

Final `ctest` output: `100% tests passed, 0 tests failed out of 1`.

`git --no-pager status --short` after implementation (Velox repo):
```text
 M velox/ch/Common/CMakeLists.txt
?? velox/ch/Common/ClickHouseAliases.h
?? velox/ch/Common/FileCacheBoundedQueue.h
?? velox/ch/Common/FileCacheException.h
?? velox/ch/Common/FileCacheFilesystem.h
?? velox/ch/Common/SharedMutex.h
?? velox/ch/Common/logger_useful.h
?? velox/ch/Common/tests/
```
Only files inside the declared file scope were touched; nothing else in the
Velox worktree is dirty. Changes remain unstaged and uncommitted.

## Worker review

```text
review subagent: code-review (read-only, one invocation) over the complete
  task-owned diff (CMakeLists.txt diff + full contents of all 8 new files),
  given the Task 003 spec, referenced design docs, and the behavioral
  reference ConcurrentBoundedQueue.h, plus build/test outcomes.

findings:
  1. (raised before this receipt existed) Missing result receipt file at
     port/task/result/003-filecache-basic-common-shims-result.md.
     This is expected sequencing per EXECUTION_PROTOCOL.md (the receipt is
     the final artifact, written only after review); not a code defect.
  2. (informational, non-blocking) FinishReleasesBlockedConsumer /
     FinishReleasesBlockedProducer tests have an inherent scheduling race:
     finish() could run before the background thread reaches its blocking
     wait(), in which case the test still passes without exercising the
     notify/wakeup path. This code is verbatim from the task file's
     prescribed literal test content, not a worker deviation, so no
     in-scope fix applies without deviating from the authoritative task
     spec.

  All other aspects (FileCacheBoundedQueue push/pop/tryPush/finish
  correctness and concurrency, VELOX_FAIL/VeloxRuntimeError integration,
  folly::SharedMutex usage, CMakeLists.txt header/test registration,
  fidelity to the task's literal code blocks, and file scope) were verified
  clean with no actionable findings. Overall subagent verdict after
  accounting for the expected receipt-timing note: no in-scope code changes
  required.

resolutions: None required; findings were either expected process sequencing
  or informational notes about spec-prescribed code, not implementation
  defects. This receipt is now written to close finding 1.

unresolved findings: None.
```

## Blockers

```text
None
```

## Worker declaration

```text
Only Task 003 was attempted.
Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Controller review 1

```text
controller_status: accepted
task: 003
```

## Review evidence

```text
scope review:
  Velox changed only the nine files declared by Task 003. The ClickHouse
  checkout changed only this receipt.

implementation review:
  Primitive aliases, Velox exception adaptation, folly::SharedMutex alias,
  no-op logging macros, std::filesystem adaptation, and the finishable bounded
  queue match the Task 003 contract. The queue rejects pushes after finish,
  drains queued values, preserves capacity-zero nonblocking behavior, and
  notifies both producer and consumer condition variables.

cross-task architecture review:
  The new headers remain in the header-only velox_ch_filecache interface
  target and introduce no Task 004+ implementation. Names and signatures match
  the accepted dependency designs and later task call sites.

log and test review:
  The red build failed on the intentionally absent ClickHouseAliases.h.
  The final worker build linked velox_ch_common_test. Worker and Controller
  ctest runs each discovered and passed exactly one ctest entry with zero
  failed or skipped tests; the source contains ten focused gtest cases.
  Controller logs:
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_controller_final.log
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_controller_final.log

  The worker correctly noted that the two blocked-wakeup tests have a
  scheduling window in which finish can win before the async operation waits.
  This is not an unresolved implementation finding: the tested public outcome
  remains correct, and direct inspection confirms finish updates finished_
  under the mutex and calls notify_all on both condition variables.

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
| `/home/chang/OpenSource/velox` | `4bea8d15e` |

## Corrective source-contract audit

```text
status: success
```

### Branch / HEAD / dirty status

| Repository | Branch | Starting HEAD | Pre-existing dirty files |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache` | `4b14de7f1146dbf303acd55ee76296fdd87e87c1` | clean (`git status --short` empty) |

### Task-owned files changed

```text
/home/chang/OpenSource/velox/velox/ch/Common/CMakeLists.txt (modified: added ClickHouseAssert.h to HEADERS)
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheBoundedQueue.h (modified: rewritten to the approved queue contract)
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheException.h (inspected only, no change needed — already throws VeloxRuntimeError only)
/home/chang/OpenSource/velox/velox/ch/Common/FileCacheFilesystem.h (modified: diagnostic now includes context, path, numeric error value, and message text explicitly)
/home/chang/OpenSource/velox/velox/ch/Common/logger_useful.h (modified: FileCacheLogger became a real name-only class instead of an empty struct; getLogger now returns a non-null LoggerPtr)
/home/chang/OpenSource/velox/velox/ch/Common/ClickHouseAssert.h (new: chassert(expr[, message]) built on folly/CPortability.h's FOLLY_SANITIZE)
/home/chang/OpenSource/velox/velox/ch/Common/tests/CMakeLists.txt (modified: added velox_ch_chassert_release_probe and velox_ch_chassert_sanitizer_gate_test targets)
/home/chang/OpenSource/velox/velox/ch/Common/tests/BasicShimsTest.cpp (modified: added/extended RED-then-GREEN coverage for all corrective contracts)
/home/chang/OpenSource/velox/velox/ch/Common/tests/ChassertReleaseProbe.cpp (new: NDEBUG-only plain executable proving zero evaluation)
/home/chang/OpenSource/velox/velox/ch/Common/tests/ChassertSanitizerGateTest.cpp (new: NDEBUG+FOLLY_SANITIZE=1 death test)
/home/chang/SourceCode/ClickHouse/port/task/result/003-filecache-basic-common-shims-result.md (this corrective section)
```

No file outside this declared scope was touched (`git status --short` in the Velox repo lists exactly these nine Velox paths, matching the corrective task's file scope).

### RED failure evidence

Before `ClickHouseAssert.h` existed (and before the queue/filesystem/logger
corrections were made), building the three corrective targets failed as
expected:

```text
velox/ch/Common/tests/BasicShimsTest.cpp:17:10: fatal error:
  velox/ch/Common/ClickHouseAssert.h: No such file or directory
velox/ch/Common/tests/ChassertReleaseProbe.cpp:16:10: fatal error:
  velox/ch/Common/ClickHouseAssert.h: No such file or directory
velox/ch/Common/tests/ChassertSanitizerGateTest.cpp:16:10: fatal error:
  velox/ch/Common/ClickHouseAssert.h: No such file or directory
ninja: build stopped: subcommand failed. (3 FAILED targets out of 7)
```

Log: `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_corrective_red.log`
(exit code 1, confirmed by a task subagent log analysis).

### Commands and log paths

| Command purpose | Exit code | Log |
|---|---:|---|
| Reconfigure (RED phase, tests added) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_003_corrective.log` |
| RED build of the three corrective targets | 1 (expected) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_corrective_red.log` |
| Reconfigure (after implementation) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_003_corrective.log` (appended) |
| Build all three corrective targets | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_corrective.log` |
| `ctest -R '^velox_ch_(common_test\|chassert_release_probe\|chassert_sanitizer_gate_test)$'` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_corrective.log` |
| `git --no-pager diff --check` | 0 | (inline, no whitespace errors) |
| Stability check: `velox_ch_common_test --gtest_filter="FileCacheBoundedQueueTest.*" --gtest_repeat=20 --gtest_shuffle` | 0 | (ad hoc, scratch log removed after inspection; all 16 queue tests passed on every one of the 20 shuffled repeats) |

### Verification per contract row

Queue:
- Task 012 call shapes `tryPush(batch, 10)` / `tryPop(poppedBatch)` compile and pass: `FileCacheBoundedQueueTest.Task012CallShapesCompile` — PASS.
- Timed producer pending while full, succeeds after pop: `TimedTryPushRemainsPendingWhileFullAndSucceedsAfterPop` — PASS.
- Full queue timed push times out rather than returning immediately: `TimedTryPushOnFullQueueTimesOutRatherThanReturningImmediately` (asserts pending at 20ms, ready by 1s) — PASS.
- Non-blocking empty tryPop returns immediately: `NonBlockingEmptyTryPopReturnsImmediately` — PASS.
- Timed tryPop wakes on push: `TimedTryPopWakesOnPush` — PASS.
- Timed tryPop wakes on finish: `TimedTryPopWakesOnFinish` — PASS.
- Capacity-zero nonblocking push fails, blocked producer released by finish: `CapacityZeroTryPushFails`, `CapacityZeroBlockedProducerReleasedByFinish` — PASS.
- Capacity-zero timed producer released by finish: `CapacityZeroTimedProducerReleasedByFinish` — PASS.
- Finish drains FIFO then false: `FinishDrainsQueuedValues` — PASS.
- Every push form rejects after finish: `AllPushFormsRejectAfterFinish` — PASS.
- Noexcept move type uses move assignment: `NoexceptMoveAssignableTypeUsesMove` — PASS.
- Copyable type with throwing move assignment uses copy: `ThrowingMoveAssignableTypeUsesCopy` — PASS.
- Throwing copy leaves source queued and recoverable: `ThrowingCopyLeavesElementQueuedAndRecoverable` — PASS.
- Finish releases blocked consumer/producer (pre-existing, unchanged): `FinishReleasesBlockedConsumer`, `FinishReleasesBlockedProducer` — PASS.

Chassert:
- Debug default diagnostic includes expression text: `ClickHouseAssertTest.DebugDefaultDiagnosticIncludesExpressionText` (death test matching `"1 == 2"`) — PASS.
- Debug custom diagnostic included: `DebugCustomDiagnosticIncluded` (death test matching `"custom diagnostic message"`) — PASS.
- Debug true expression evaluated exactly once: `DebugTrueExpressionEvaluatedExactlyOnce` — PASS.
- Ordinary Release evaluates neither expression nor message: `velox_ch_chassert_release_probe` (NDEBUG-only target) exits 0, proving the counter incremented by both the asserted expression and the diagnostic-message builder stayed at zero — PASS.
- Sanitizer build still aborts under NDEBUG: `velox_ch_chassert_sanitizer_gate_test` (NDEBUG + `FOLLY_SANITIZE=1`) death test `chassert(false)` — PASS.

Logger:
- `getLogger` returns non-null with correct name identity: `GetLoggerReturnsNonNullWithNameIdentity` — PASS.
- All LOG_TEST/TRACE/DEBUG/INFO/WARNING/ERROR macros evaluate no arguments: `AllLogMacrosDoNotEvaluateArguments` (plus the original `ArgumentsAreNotEvaluated`) — PASS.
- `getCurrentExceptionMessage` stays empty (both overload forms): `CurrentExceptionMessageRemainsEmptyFirstPhase` — PASS.
- `tryLogCurrentException` remains a no-op: `TryLogCurrentExceptionIsNoOpFirstPhase` — PASS.

Exception:
- `throwFileCacheException` throws `VeloxRuntimeError`, never `VeloxUserError`, with `errorCode() == "INVALID_STATE"`: `FileCacheExceptionTest.ThrowsVeloxRuntimeError`, `NeverThrowsVeloxUserError` — PASS. (File itself required no code change; it already used `VELOX_FAIL`.)

Filesystem:
- `throwFileCacheExceptionFromFilesystemError` throws `VeloxRuntimeError` (never `VeloxUserError`) and the message contains the operation context, the failing path, the numeric error code value, and the error message text: `FileCacheFilesystemTest.FilesystemErrorKeepsContext` — PASS.

### Deferred work statement

`getCurrentExceptionMessage` deliberately continues to return an empty
string and `tryLogCurrentException` remains a no-op in this corrective
pass. Real exception-message formatting and real logging output are
explicitly out of scope here and belong to Task 017, per the approved
logger contract.

### Self-review

```text
review subagent: code-review (read-only, one invocation), given full
  contents of every task-owned file (current on-disk state), the diff
  against the prior committed state, the ClickHouse behavioral sources
  (ConcurrentBoundedQueue.h, MoveOrCopyIfThrow.h, defines.h,
  sanitizer_defs.h), and the approved corrective contract summarized above.

findings: none. The subagent independently re-ran the full test suite
  (all 30 gtest cases plus the two standalone probes) and additionally
  repeated the concurrency-sensitive queue tests 20x under
  --gtest_shuffle, confirming stability. It confirmed: the queue's exact
  method surface and notify placement, the move-iff-noexcept /
  copy-otherwise assignment with pop-after-assignment-succeeds ordering,
  the chassert() NDEBUG/FOLLY_SANITIZE gate (via macro expansion and all
  three build variants), FileCacheException.h's unmodified
  VeloxRuntimeError-only behavior, the filesystem diagnostic's four
  required components, the logger's non-null name-only shape and
  zero-argument-evaluation macros, and that only the nine declared files
  were touched.

resolutions: none required (no findings raised).
unresolved findings: none.
```

### Blocking errors

```text
None.
```

## Corrective source-contract audit 2 (`emplaceImpl` forwarding)

```text
status: success
```

### Review finding being fixed

```text
code-quality reviewer finding: `velox/ch/Common/FileCacheBoundedQueue.h`'s
  private `emplaceImpl(std::optional<uint64_t>, T value)` took its argument
  by value, so the move/copy into the helper's parameter happened at the
  call boundary — before the wait/`is_finished` checks ran. A failed/full/
  finished `tryPush(T &&)` therefore silently consumed (moved-from) the
  caller's rvalue, and a failed/full/finished `tryPush(const T &)` performed
  an unconditional copy, even though the push never actually enqueued
  anything. This diverges from CH's
  `src/Common/ConcurrentBoundedQueue.h::emplaceImpl<bool back, typename... Args>`
  (line 31), which is `Args &&... args` and only does
  `queue_.emplace_back(std::forward<Args>(args)...)` (or `emplace_front`)
  after the wait and `is_finished` checks succeed, so a failed push never
  touches the caller's argument.

scope of fix: private helper signature and body only
  (`emplaceImpl` in `FileCacheBoundedQueue.h`). No public API, call site, or
  other Task 003 file changed. Per the corrective brief,
  `tryLogCurrentException(...)` in `logger_useful.h` is explicitly untouched
  (accepted consequence of the already-approved compatibility signature).
```

### RED failure evidence

```text
Two new deterministic gtest cases were added to
  `velox/ch/Common/tests/BasicShimsTest.cpp` (FileCacheBoundedQueueTest
  suite), using a new local `MoveTrackingProbe` type that records whether
  it was moved-from and a static copy counter:

  - FailedFullTryPushMoveDoesNotConsumeCallerValue: fills a capacity-1
    queue, then calls `queue.tryPush(std::move(value), 0)` on the full
    queue (non-blocking, must fail) and asserts `value.movedFrom == false`
    and `value.value == 42` (untouched).
  - FailedFullTryPushConstRefDoesNotCopyCallerValue: same full-queue setup,
    calls `queue.tryPush(value, 0)` (const-ref overload) and asserts
    `MoveTrackingProbe::copyCount == 0`.

Run against the pre-fix (by-value `emplaceImpl`) code, both failed exactly
  as predicted, with no rebuild needed (the buggy signature already
  compiled against these call sites):

    [ RUN      ] FileCacheBoundedQueueTest.FailedFullTryPushMoveDoesNotConsumeCallerValue
    .../BasicShimsTest.cpp:543: Failure
    Value of: value.movedFrom
      Actual: true
    Expected: false
    [  FAILED  ] FileCacheBoundedQueueTest.FailedFullTryPushMoveDoesNotConsumeCallerValue (0 ms)
    [ RUN      ] FileCacheBoundedQueueTest.FailedFullTryPushConstRefDoesNotCopyCallerValue
    .../BasicShimsTest.cpp:555: Failure
    Expected equality of these values:
      MoveTrackingProbe::copyCount
        Which is: 1
      0
    [  FAILED  ] FileCacheBoundedQueueTest.FailedFullTryPushConstRefDoesNotCopyCallerValue (0 ms)
    2 FAILED TESTS

  This confirms the reviewer's finding is real and reproducible: the
  caller's rvalue was moved-from, and the caller's value was copied, even
  though the push failed (queue stayed full the whole time).

Logs:
  Build (compiled cleanly, no source change needed yet):
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_forwarding_red.log
  RED test run (exit code 1, 2 failed tests, summarized above):
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_forwarding_red.log
```

### Fix applied

```text
`emplaceImpl` changed from:
    bool emplaceImpl(std::optional<uint64_t> timeoutMilliseconds, T value)
    { ... queue_.emplace_back(std::move(value)); ... }

to a perfectly-forwarding member template:
    template <typename U>
    bool emplaceImpl(std::optional<uint64_t> timeoutMilliseconds, U && value)
    { ... queue_.emplace_back(std::forward<U>(value)); ... }

`push(T value)`, `tryPush(const T &, uint64_t = 0)`, `tryPush(T &&, uint64_t = 0)`,
  both `tryPop` overloads, `pop`, `finish`, `assignFront`, all wait/predicate/
  notify logic, and every other Task 003 public API and queue-state semantic
  are byte-for-byte unchanged. `push` still binds `U` to `T` via
  `std::move(value)`; `tryPush(const T &, ...)` now binds `U` to `const T &`
  (forwarding-reference collapse), so `std::forward<U>(value)` only copies
  once the wait/`is_finished` checks pass and the element is actually
  enqueued; `tryPush(T &&, ...)` binds `U` to `T`, so the move only happens
  after those same checks pass.
```

### Commands and log paths

| Command purpose | Exit code | Log |
|---|---:|---|
| RED build of `velox_ch_common_test` (pre-fix, tests added) | 0 (compiles; new tests only assert on runtime behavior) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_forwarding_red.log` |
| RED test run: `--gtest_filter="FileCacheBoundedQueueTest.FailedFullTryPush*"` | 1 (2 tests failed, expected) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_forwarding_red.log` |
| Rebuild all three Task 003 targets (`velox_ch_common_test`, `velox_ch_chassert_release_probe`, `velox_ch_chassert_sanitizer_gate_test`) after the fix | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_003_forwarding_fix.log` |
| `ctest -R '^velox_ch_(common_test\|chassert_release_probe\|chassert_sanitizer_gate_test)$'` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_forwarding_fix.log` |
| Verbose gtest re-run of the full `FileCacheBoundedQueueTest` suite (18 cases) to confirm the two new tests pass individually | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_forwarding_fix_gtest_verbose.log` |
| Full `velox_ch_common_test` gtest run (all 32 cases, regression check) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_003_forwarding_fix_full_gtest.log` |
| `git --no-pager diff --check` | 0 | (inline, no whitespace errors) |

### Final results

```text
build: all three targets linked with 0 errors and 0 warnings mentioning
  FileCacheBoundedQueue.h or BasicShimsTest.cpp (confirmed by a task
  subagent log analysis of build_task_003_forwarding_fix.log).

ctest: `100% tests passed, 0 tests failed out of 3`
  (velox_ch_common_test, velox_ch_chassert_release_probe,
  velox_ch_chassert_sanitizer_gate_test all PASS).

gtest (full velox_ch_common_test binary): `[==========] 32 tests from 7
  test suites ran. [  PASSED  ] 32 tests.` — includes both new GREEN tests:
  FileCacheBoundedQueueTest.FailedFullTryPushMoveDoesNotConsumeCallerValue
    — OK
  FileCacheBoundedQueueTest.FailedFullTryPushConstRefDoesNotCopyCallerValue
    — OK
  and all 16 previously-existing FileCacheBoundedQueueTest cases plus every
  other suite (ClickHouseAliasesTest, LoggerUsefulTest, FileCacheExceptionTest,
  SharedMutexTest, FileCacheFilesystemTest, ClickHouseAssertTest) still pass,
  confirming no regression from the signature change.

git --no-pager diff --check: clean, no whitespace errors.

scope: `git status --short` in the Velox repo still lists exactly the same
  nine paths as before this corrective pass (6 modified + 3 new, all inside
  velox/ch/Common/); nothing outside that scope was touched.
```

### Self-review 2

```text
review subagent: task (log-analysis only, used to summarize
  build_task_003_forwarding_fix.log). Direct inspection (not delegated) of
  the final `emplaceImpl` template confirmed: (a) the predicate/wait_for/
  wait/`finished_` check ordering is byte-for-byte identical to before the
  change — only the parameter type and the final `emplace_back` argument
  changed; (b) `push`, both `tryPush` overloads, both `tryPop` overloads,
  `pop`, `finish`, and `assignFront` are textually unchanged from the prior
  accepted corrective pass; (c) `tryLogCurrentException` in
  `logger_useful.h` was not touched, per the explicit instruction that the
  reviewer's second Minor note is an accepted consequence of the
  already-approved compatibility signature; (d) `git status --short` and
  `git diff --stat` against the `4b14de7f1` base show only the same nine
  Velox paths as the prior corrective pass, with no new files and no
  changes outside `velox/ch/Common/`.

findings: none. The fix is a minimal, surgical change to one private helper
  template, restoring exact CH-parity with
  `ConcurrentBoundedQueue::emplaceImpl(Args &&...)`'s forward-after-checks
  ordering, verified RED-then-GREEN with two new deterministic tests plus a
  full-suite regression pass (32/32 gtest cases, 3/3 ctest entries).

resolutions: the code-quality reviewer's finding is now fixed;
  `queue_.emplace_back` in `emplaceImpl` is only reached, and only forwards
  (never eagerly moves/copies), after the wait and `finished_` checks
  succeed.

unresolved findings: none.
```

### Blocking errors 2

```text
None.
```

## Controller corrective review

```text
status: accepted
implementation commit:
  c755512a8f38974aa8f2ab45dd5909b308bf07b1
```

### Contract review

```text
review 1:
  independent specification review compared every task-owned file, CMake target,
  test, receipt section, and log against the corrected Task 003 contract and the
  CH ConcurrentBoundedQueue, MoveOrCopyIfThrow, and chassert sources.
  result: spec compliant.

review 2:
  the first independent code-quality review found one CH-parity gap:
  emplaceImpl accepted T by value, so a failed tryPush could move/copy its
  caller's argument before the queue accepted it.
  result: changes requested despite the review classifying the impact as minor,
  because the approved contract requires strict CH behavior.

fix:
  emplaceImpl now perfect-forwards only after wait and finished checks.
  Two deterministic tests proved RED on the old behavior and GREEN after the
  fix for both rvalue consumption and const-lvalue copying.

re-review:
  fresh specification and code-quality reviews independently inspected the
  complete post-fix diff and evidence.
  result: both approved; no unresolved findings.
```

### Controller evidence

```text
Controller directly inspected all six modified and three new Velox files.
Controller directly inspected both RED logs, the final build log, the final
CTest log, the full gtest log, and both corrective receipt sections.

forwarding RED:
  2 expected failures:
    FailedFullTryPushMoveDoesNotConsumeCallerValue
    FailedFullTryPushConstRefDoesNotCopyCallerValue

final build:
  all three Task 003 targets linked successfully.

final CTest:
  3/3 passed, 0 failed.

full gtest:
  32/32 passed.

scope:
  exactly the nine declared Velox files changed.
  git diff --check passed in both repositories.
  no unrelated changes were present.
```

### Unresolved findings

```text
None.
```

### Recommended next task

```text
Run the dependency preflight for reopened Task 004 before corrective coding.
Stop if it exposes any mapping not already reviewed in the canonical design.
```

## Post-acceptance cross-profile contract audit

```text
controller_status: reopened_by_contract_audit
audit source: port/task/fullreview/cross-profile/1/003-010-review-decisions.md
task: 003
```

### Finding

```text
The Tasks 003-010 cross-profile review (round 1) reopened Task 003 a second
time. It found the accepted implementation still lacks the required no-op
`ProfileEvents`/`CurrentMetrics` name surfaces used by real CH `FileCache`
and `OpenedFileCache` source:

B1: 34 ProfileEvents::Event names absent from velox/ch/Common/ProfileEvents.h:
    31 referenced by CH FileCache (src/Interpreters/FileCache/*), plus 3
    (OpenedFileCacheHits, OpenedFileCacheMisses, OpenedFileCacheMicroseconds)
    referenced by CH src/IO/OpenedFileCache.h and required by the
    user-approved Task-013 OpenedFileCache dependency mapping recorded in
    port/task/fullreview/cross-profile/1/003-010-review-decisions.md.
B2: 5 CurrentMetrics::Metric names referenced by CH FileCache but absent from
    velox/ch/Common/CurrentMetrics.h (excluding the three constructor-only
    eviction-thread metrics and FilesystemCacheOvercommitUsers, which remain
    out of scope).

Absent these names, Task 011 (priority/eviction), Task 012 (center SCC), and
Task 013 (OpenedFileCache) cannot reference the full no-op event/metric
surface their migrated CH source expects, which would force an unreviewed
workaround at implementation time.
```

### Corrective contract

```text
The exact corrective contract, including the full B1/B2 name lists, the
no-op requirement, the compile-coverage test requirement, and the
delete-one-name false-green mutation requirement, is recorded in
port/task/003-filecache-basic-common-shims.md under
"Corrective scope B1/B2: no-op ProfileEvents/CurrentMetrics name surfaces".
This receipt file remains the append-only evidence target for the corrective
Worker attempt that implements that scope.
```

### Status

```text
status: reopened, not yet corrected
Task 011 and Task 012 must not start until a corrective Worker attempt below
records B1/B2 RED, mutation, and final-green evidence, and a Controller
review accepts it.
```
