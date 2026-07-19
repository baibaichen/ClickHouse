# Task 007 Result: Add `ReadBufferFromVeloxReadFile` and `WriteBufferFromVeloxWriteFile`

## Worker attempt 1

```text
worker_status: ready_for_controller
task: 007
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/SourceCode/ClickHouse` (protocol/receipts) | `ch-filecache` | `5af6ab908fed76e079c7ed9bc96f45924d7ebe14` | `?? tmp/` (Controller scratch — not touched) |
| `/home/chang/OpenSource/velox` (implementation) | `filecache` | `d9f4517c504b93dfc29e61bdfebd0f6cd3bf5a2e` | clean |

Both baselines matched the Task 007 / ENVIRONMENT expectations. The ClickHouse
checkout was never modified outside `port/task/result/` (only this receipt is
added); `tmp/` was left untouched.

## Files changed

```text
# Velox (/home/chang/OpenSource/velox) — modify
velox/ch/CMakeLists.txt                         (+1 line: add_subdirectory(IO))

# Velox — create
velox/ch/IO/CMakeLists.txt
velox/ch/IO/ReadBufferFromVeloxReadFile.h
velox/ch/IO/ReadBufferFromVeloxReadFile.cpp
velox/ch/IO/WriteBufferFromVeloxWriteFile.h
velox/ch/IO/WriteBufferFromVeloxWriteFile.cpp
velox/ch/IO/tests/CMakeLists.txt
velox/ch/IO/tests/IoAdaptersTest.cpp

# ClickHouse (/home/chang/SourceCode/ClickHouse) — create
port/task/result/007-filecache-io-adapters-result.md   (this receipt)
```

Final `git status --short`:

```text
# /home/chang/OpenSource/velox
 M velox/ch/CMakeLists.txt
?? velox/ch/IO/

# /home/chang/SourceCode/ClickHouse
## ch-filecache...origin/master [ahead 50]
?? tmp/
?? port/task/result/007-filecache-io-adapters-result.md   (this receipt, once written)
```

All changed paths are within the Task 007 declared scope (File scope section,
task lines 68-87). No file outside the declared scope was modified. Changes are
unstaged and uncommitted.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| Baseline: `git status --short --branch` + `git log -1` in both repos | 0 | (inline, see baselines) |
| Configure (RED, tests subdir only, sources not yet registered) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_007_io.log` |
| Build RED `ninja velox_ch_io_test` (expected fail: header absent) | 1 (expected) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_red.log` |
| Configure (GREEN, full IO sources + headers registered) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_007_io.log` |
| Build GREEN `ninja velox_ch_io_test` (initial adapters) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_io.log` |
| TDD RED (new test `ExternalBufferIsSingleUseThenRevertsToInternal`): build ok, test FAILS | build 0 / test 1 (expected) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_red.log` |
| Build GREEN `ninja velox_ch_io_test` (after external-buffer revert fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_io.log` |
| Build regression targets (common/guards/threadpool/scheduler) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_io.log` |
| Focused `ctest -R '^velox_ch_io_test$'` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_007_io.log` |
| gtest discovery `--gtest_list_tests` + direct run (0 disabled/skipped proof) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_007_io.log` |
| Regression `ctest -R '^(velox_ch_common_test|velox_ch_guards_test|velox_ch_threadpool_test|velox_ch_scheduler_test)$'` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_007_io.log` |
| Scope: `git diff --check` (Velox) | 0 (clean) | (inline) |

Configure command (per `ENVIRONMENT.md`, re-run in the existing build dir to
pick up the new `CMakeLists.txt` files; Ninja parallelism auto, no `-j`):

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13
```

Build command form:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 velox_ch_io_test
```

## Acceptance evidence

```text
Red build (header absent): FAILED as required — "fatal error:
  velox/ch/IO/ReadBufferFromVeloxReadFile.h: No such file or directory"
  (configure succeeded; build failed). Log: build_task_007_red.log.

TDD red (external-buffer single-use test): FAILED as required before the fix —
  IoAdaptersTest.cpp:226 "second next() must not write into the consumed
  external buffer": Actual "EFGH", Expected "ABCD". Log: build_task_007_red.log.

Final build exit code: 0 (clean; no warnings or errors).

test count: 16 tests in velox_ch_io_test (2 suites:
  ReadBufferFromVeloxReadFileTest = 9, WriteBufferFromVeloxWriteFileTest = 7).
failed tests: 0  ( [  PASSED  ] 16 tests. )
skipped/disabled tests: 0  (--gtest_list_tests lists 16; run reports 16 run,
  0 DISABLED, 0 SKIPPED). Test is registered with ctest (test #410) and runs
  under ctest (100% passed, 0 failed out of 1).
regression: velox_ch_common_test, velox_ch_threadpool_test,
  velox_ch_scheduler_test, velox_ch_guards_test — 4/4 passed (100%).
benchmark result, when required: N/A (Task 007 requires no benchmark).
git diff --check: clean (no whitespace/conflict errors).
```

## Worker review

```text
review subagent: exactly one read-only code-review subagent was launched over
  the complete Task-007-owned diff (tracked velox/ch/CMakeLists.txt change plus
  the untracked velox/ch/IO/ tree read directly by path). It was given the task
  spec, the design/behavioral references, the Task 006 dependency receipt, the
  build/test logs, and the two deliberate deviations to scrutinize. The agent
  made no edits.

findings:
  1) [Minor / latent lifetime] ReadBufferFromVeloxReadFile: the externalBuffer_
     flag was written but never read; after one set()+next() cycle bufData_
     stayed pointing at the caller's external buffer and the internal buffer was
     never restored, so a subsequent next() (or seek()+next()) would read into
     caller memory the caller is entitled to release after the first next().
     No out-of-bounds write (toRead is bounded by bufCapacity_), but a
     use-after-free footgun for the future FileCacheInputStream consumer, and a
     doc/impl mismatch versus the stated "single read cycle" contract.
  The reviewer confirmed everything else clean: next() destination sizing is
  bounds-safe (and safer than the task's literal pseudo-code); offset math
  across next/advance/seek/set is consistent; advance() check forms are correct;
  write-side totalWritten_ accounting and flush/finalize/cancel guards are
  correct; the CMake mono-safe deviation is correct for both build modes; and
  the OwnershipTransferSharedPtr test fix is valid and strengthens the test.

resolutions:
  1) Implemented the stated single-use external-buffer contract via TDD. Added a
     failing test (ExternalBufferIsSingleUseThenRevertsToInternal) that proves a
     second next() must not clobber the consumed external buffer (RED: external
     buffer showed "EFGH"). Fixed next() to restore the internal buffer at the
     top of every non-armed read cycle (externalBuffer_ now drives behavior),
     making the external buffer genuinely single-use and reverting to internal
     afterward. Re-ran the affected gate: velox_ch_io_test 16/16 GREEN and the
     4 regression suites GREEN. (Per the protocol flowchart FINDINGS -> FIX ->
     VALIDATE -> READY; the single review subagent budget is preserved — no
     second review was launched.)

unresolved findings: none.
```

## Blockers

```text
None. All acceptance gates are green; the one review finding was actionable,
in-scope, and resolved with a TDD regression test.
```

## Deviations from the task's literal snippets (source-of-truth escalation)

The adapters' **business contracts were not changed**. The following literal
snippets in the task file were obviously defective; per the worker guardrail
"do not copy an obviously-flawed literal snippet / do not weaken assertions",
they were corrected to faithfully realize the task's own *stated* contracts, and
each correction is documented here so the Controller can fix the source-of-truth
task file (`port/task/007-filecache-io-adapters.md`).

1. **`IO/CMakeLists.txt` (Step 3) — mono-library incompatibility.**
   The literal snippet calls `target_sources(velox_ch_filecache PRIVATE ... PUBLIC
   FILE_SET HEADERS ...)` unconditionally. With `VELOX_MONO_LIBRARY=ON` (the
   environment default), `velox_ch_filecache` is an ALIAS to `velox`, and CMake
   errors: `target_sources can not be used on an ALIAS target` (empirically
   reproduced with CMake 3.28.3). Adaptation: register `.cpp` via the repo's
   mono-safe `velox_sources(velox_ch_filecache PRIVATE ...)` helper (routes to
   `velox` in mono, to `velox_ch_filecache` otherwise) and register the public
   header `FILE_SET` only inside `if(NOT VELOX_MONO_LIBRARY)`, mirroring the
   existing `velox/ch/Interpreters/FileCache/CMakeLists.txt` `Guards.h` pattern.
   Verified: GREEN configure + build compiled both `.cpp` into `velox`.
   Recommended task-file fix: replace the Step 3 snippet with the mono-safe form.

2. **`ReadBufferFromVeloxReadFile::next()` (Step 6 pseudo-code) — real
   lifetime/overflow defect.** The literal pseudo-code computes
   `toRead = min(externalBuffer_ ? bufCapacity_ : internalBuffer_.size(), ...)`,
   resets `externalBuffer_ = false` after one read, but never restores `bufData_`
   to the internal buffer. This does not implement the spec's own stated contract
   (task lines 568-570, 628-630, 714: "for the next read cycle", "reset external
   flag after one use", "valid until the next next()/seek()/set()"). Two concrete
   defects: (a) a second `next()` after `set()+next()` takes the
   `internalBuffer_.size()` branch while `bufData_` still points at the smaller
   external buffer → `pread` up to `internalBuffer_.size()` bytes into a smaller
   buffer → heap overflow; (b) even bounding by `bufCapacity_`, `bufData_` keeps
   pointing at caller memory that the caller may already have released → UAF.
   Fix (implemented, TDD-covered): at the top of `next()`, when not armed
   (`externalBuffer_ == false`), restore `bufData_ = internalBuffer_.data();
   bufCapacity_ = internalBuffer_.size();`. Also keep `toRead` bounded by
   `bufCapacity_` in all states (never `internalBuffer_.size()` while pointing at
   an external buffer). New test: `ExternalBufferIsSingleUseThenRevertsToInternal`.
   Recommended task-file fix: bound `toRead` by `bufCapacity_` and restore the
   internal buffer after each single external read cycle (or drop `externalBuffer_`
   and require `set()` before every external read).

3. **`OwnershipTransferSharedPtr` test (Step 2) — logically unsatisfiable.**
   The literal test declares the owning `std::shared_ptr<MockWriteFile> wf`
   *inside* the inner block, so `wf` and `writer` are both destroyed at the
   block's close; the post-block `EXPECT_FALSE(weak.expired())` is then
   impossible (object destroyed → `weak.expired()` is `true`). The snippet's own
   comment ("the original wf is still alive here because we hold wf on the stack")
   describes the intended behavior but contradicts the code (`wf` is not on the
   outer stack, and it is copied, not moved). Fix: hoist `wf`/`weak` to the outer
   scope so the writer shares ownership via a copied `shared_ptr`; the object
   correctly outlives the writer while the caller holds `wf`
   (`EXPECT_FALSE(weak.expired())`), and an added `wf.reset();
   EXPECT_TRUE(weak.expired());` proves the writer released its reference (no
   leak). This does not weaken the assertion or change the adapter contract; it
   makes the test verify the intended shared-ownership lifetime.
   Recommended task-file fix: declare `wf` in the outer scope and add the release
   assertion.

Additional faithful (non-contract) implementation choices, for completeness:
- `advance()` (read side) uses the single-expression `VELOX_CHECK(pos_ + n <=
  bufEnd_, "advance past buffer end")` rather than `VELOX_CHECK_LE(pos_, bufEnd_)`;
  the two-arg macro would pass `char*` operands to fmt and format them as
  C-strings. The write side's `VELOX_CHECK_LE(writePos_ + n, buffer_.size(), ...)`
  is on `size_t` and is fine.
- Both constructors are `explicit` and validate inputs (`VELOX_CHECK_NOT_NULL`
  on the file, `VELOX_CHECK_GT(bufferSize, 0u, ...)`).
- Added the `bufStartOffset_` field per the Step 6 note; kept it in sync in
  `next()`/`seek()`.

## Worker declaration

```text
Only Task 007 was attempted.
Changes are unstaged and uncommitted in both repositories.
No staging, commit, amend, rebase, push, PR, or worktree was performed.
CONTROLLER_HANDOFF.md was not modified.
Task 008 was not started.
The worker stopped after writing this receipt.
```

## Recommended next task

```text
Task 008: port SipHash128, FileCacheKey, FileSegmentKeyType,
FileCacheOriginInfo, forward files, and FileCacheUtils leaf types, and
FileCacheSettings (port-order stage 1). (Controller to dispatch after accepting
Task 007 and, ideally, correcting the three literal-snippet defects noted above
in the source-of-truth task file.)
```

## Controller review 1

```text
controller_status: changes_requested
task: 007
```

## Review evidence

```text
scope review:
  The Velox checkout contains only the eight declared Task 007 paths. The
  ClickHouse checkout adds only this receipt; tmp/ remains unrelated
  Controller scratch.

implementation review:
  The mono-safe CMake registration, single-use external-buffer restoration, and
  corrected shared_ptr ownership test faithfully implement the stated task
  contracts. The task specification has been amended to make those corrections
  authoritative.

  ReadBufferFromVeloxReadFile::advance computes pos_ + n before validating n.
  A negative n can move before the allocation, and an oversized positive n can
  form an out-of-range pointer; either pointer expression is undefined behavior
  before VELOX_CHECK evaluates. The method must validate the signed count
  against bufEnd_ - pos_ before pointer arithmetic.

  seek casts negative SEEK_SET results and SEEK_CUR results before zero to
  size_t, turning them into huge offsets. It also performs signed addition
  without an overflow guard. setReadUntilPosition only sets atEof_ to true, so
  extending a prior boundary cannot resume reads despite the public contract.
  set accepts null/zero external buffers, and write-side advance validates
  writePos_ + n after that unsigned addition may already have wrapped.

cross-task architecture review:
  Task 014 consumes these adapters as the remote/cache read boundary. Invalid
  cursor arithmetic or a readUntil state that cannot resume would corrupt its
  absolute-position state machine. These checks belong in the Task 007 adapter,
  not in every future consumer.

log and test review:
  The header-red and external-buffer behavioral-red are genuine. The final
  focused suite passed 16/16 tests with zero disabled/skipped cases and all four
  Task 003-006 regression ctest entries passed. Existing tests do not cover the
  invalid cursor and boundary-transition paths above.

unresolved findings:
  Undefined pointer arithmetic in read advance.
  Negative/overflowing seek conversion.
  readUntil extension cannot clear boundary EOF.
  Invalid external-buffer and write-advance overflow inputs are unchecked.
```

## Required changes

```text
1. Implement every Offset and boundary safety rule in the Controller amendment.
2. Add the focused tests listed by the amendment, including a valid backward
   SEEK_CUR case and rejection paths.
3. Preserve the accepted mono-safe CMake, single-use external-buffer, and
   shared-ownership corrections.
4. Produce fresh final build, 003-006 regression, discovery, and no-skip logs.
5. Launch exactly one fresh read-only review subagent for attempt 2, resolve all
   actionable in-scope findings, append Worker attempt 2, and stop.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `711a848501d54dffaf5afc53278a97bb10825aa3` |

## Worker attempt 2

```text
worker_status: ready_for_controller
task: 007
```

Rework of Worker attempt 1 per Controller review 1 (`controller_status:
changes_requested`) and the Task 007 "Controller amendment after Worker
attempt 1". Worker attempt 1, Controller review 1, Required changes, and Commits
above are unchanged.

## Repository baselines (attempt 2)

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/SourceCode/ClickHouse` (protocol/receipts) | `ch-filecache` | `5af6ab908fed76e079c7ed9bc96f45924d7ebe14` | `M port/task/007-filecache-io-adapters.md`, `M port/task/CONTROLLER_HANDOFF.md`, `?? port/task/result/007-filecache-io-adapters-result.md`, `?? tmp/` — all Controller-owned/scratch, not touched |
| `/home/chang/OpenSource/velox` (implementation) | `filecache` | `d9f4517c504b93dfc29e61bdfebd0f6cd3bf5a2e` | `M velox/ch/CMakeLists.txt`, `?? velox/ch/IO/` — attempt-1 work |

Both baselines matched the attempt-2 dispatch expectations. The Task 007 file and
`CONTROLLER_HANDOFF.md` were never modified.

## Files changed (attempt 2)

Velox repo (`/home/chang/OpenSource/velox`) — same eight declared Task 007 paths
as attempt 1; attempt 2 modified three of them:

```text
velox/ch/IO/ReadBufferFromVeloxReadFile.cpp   (advance, seek, setReadUntilPosition, set; +#include <limits>)
velox/ch/IO/WriteBufferFromVeloxWriteFile.cpp (advance: overflow-safe validation)
velox/ch/IO/tests/IoAdaptersTest.cpp          (+11 focused safety tests; removed unused MockReadFile::callCount)
```

Preserved unchanged from attempt 1 (accepted): `velox/ch/CMakeLists.txt`,
`velox/ch/IO/CMakeLists.txt`, `velox/ch/IO/ReadBufferFromVeloxReadFile.h`,
`velox/ch/IO/WriteBufferFromVeloxWriteFile.h`, `velox/ch/IO/tests/CMakeLists.txt`.

ClickHouse repo: only this receipt (`port/task/result/007-filecache-io-adapters-result.md`),
appended below the Controller sections.

## TDD RED evidence (attempt 2)

Behavioral RED captured against the UNCHANGED attempt-1 production implementation
(tests added first, production untouched):

```text
Suite RED — /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_007_attempt2_red.log
  26 tests: 18 passed, 8 FAILED. Failures are genuine behavioral failures
  ("throws nothing" where rejection is required; EOF latch stuck; position
  corrupted by unsigned wrap) — not build/test errors:
    ReadBufferFromVeloxReadFileTest.AdvanceRejectsNegativeCount       (advance(-1) accepted; cursor moved)
    ReadBufferFromVeloxReadFileTest.SeekSetRejectsNegativeOffset      (seek(-1,SEEK_SET) accepted)
    ReadBufferFromVeloxReadFileTest.SeekCurRejectsPositionBeforeStart (seek(-10,SEEK_CUR) accepted)
    ReadBufferFromVeloxReadFileTest.SeekCurRejectsPositiveOverflow    (seek(100,SEEK_CUR) at off_t max wrapped)
    ReadBufferFromVeloxReadFileTest.SetReadUntilPositionResumesAfterExtension (EOF latch never cleared)
    ReadBufferFromVeloxReadFileTest.SetRejectsNullExternalBuffer      (set(nullptr,16) accepted)
    ReadBufferFromVeloxReadFileTest.SetRejectsZeroCapacityExternalBuffer (set(buf,0) accepted)
    WriteBufferFromVeloxWriteFileTest.AdvanceRejectsOverflowingCount  (advance(SIZE_MAX-4) wrapped to 3, accepted; position corrupted)
  Two guard tests passed against attempt-1 as expected and are preserved:
    AdvanceRejectsOversizedCount        (attempt-1 rejects oversized positive advances only via out-of-range
                                         pointer arithmetic that this fix removes; the extreme PTRDIFF_MAX
                                         value is included per the amendment)
    SeekCurSupportsValidNegativeOffset  (valid backward SEEK_CUR already worked; guards preservation)

Reviewer Finding 1 RED — /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_007_attempt2_finding1_red.log
  1 test, 0 passed, 1 FAILED: SeekCancelsArmedExternalBuffer — next() after
  set()+seek() wrote "EFGH" into the external buffer the seek should have
  released. Fixed, then GREEN.
```

## Production changes (attempt 2)

Implements every "Offset and boundary safety" rule; the validation itself performs
no invalid pointer arithmetic and no overflowing signed/unsigned addition:

```text
advance(ptrdiff_t n): VELOX_CHECK_GE(n,0) first, then compare n against
  available = bufEnd_ - pos_ (in-bounds pointer subtraction) before forming
  pos_ + n. Rejects negative and oversized/extreme (PTRDIFF_MAX) counts without
  building an out-of-range pointer.
seek(SEEK_SET): reject negative offsets.
seek(SEEK_CUR): guard positive overflow before the add (offset <= off_t_max -
  current); the negative path computes current + offset (never overflows for a
  non-negative current) then rejects < 0. Safe for off_t min/max.
seek(...): additionally cancels any armed external buffer (reverts bufData_/
  bufCapacity_ to the internal buffer and clears externalBuffer_) — fix for
  reviewer Finding 1, consistent with set()'s documented single-cycle validity.
setReadUntilPosition(filePos): atEof_ = currentOffset_ >= readUntil_ (both
  directions); extending the boundary clears EOF so next() resumes. Real EOF is
  still re-derived by next()'s empty-pread check.
set(data,size): VELOX_CHECK_NOT_NULL(data) + VELOX_CHECK_GT(size,0u).
WriteBufferFromVeloxWriteFile::advance(size_t n): VELOX_CHECK_LE(n,
  buffer_.size() - writePos_) — compares against remaining capacity so the
  validation cannot overflow (writePos_ <= buffer_.size() invariant).
```

Preserved accepted attempt-1 corrections: mono-safe CMake registration
(`velox_sources` + header FILE_SET only when `NOT VELOX_MONO_LIBRARY`), one-cycle
external-buffer restoration in `next`, and the corrected shared_ptr
shared-ownership write test.

## Commands and outcomes (attempt 2)

| Command purpose | Exit code | Log |
|---|---:|---|
| RED build — io test vs unchanged production | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_attempt2_red.log` |
| RED run — suite (8 genuine failures) | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_007_attempt2_red.log` |
| Finding-1 RED build | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_attempt2_finding1_red.log` |
| Finding-1 RED run — seek/external | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_007_attempt2_finding1_red.log` |
| Fresh final rebuild — recompiles all 3 sources | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_attempt2_final.log` |
| Regression targets build (003-006) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_007_attempt2_regression.log` |
| Full ctest — io + 003-006 (5 suites) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_007_attempt2_all_gates.log` |
| ctest discovery (-N) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/discovery_task_007_attempt2.log` |
| No-disabled/no-skipped + gtest_list_tests | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/noskip_task_007_attempt2.log` |
| git diff --check — both repos | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/gitcheck_task_007_attempt2.log` |

Build tool: JetBrains Ninja, no `-j`/`nproc`. The fresh rebuild log shows 3
recompiled Task 007 sources (`ReadBufferFromVeloxReadFile.cpp`,
`WriteBufferFromVeloxWriteFile.cpp`, `IoAdaptersTest.cpp`) plus relink of
`libvelox.a` and `velox_ch_io_test`. Full current Velox diff artifact:
`/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task_007_attempt2_full_velox.diff`.

## Acceptance evidence (attempt 2)

```text
test count: velox_ch_io_test = 27 tests, 27 passed. Full ctest gate = 5/5 suites, 100% passed:
  #406 velox_ch_common_test, #407 velox_ch_threadpool_test, #408 velox_ch_scheduler_test,
  #409 velox_ch_guards_test, #410 velox_ch_io_test.
failed tests: none (final)
skipped/disabled tests: none — --gtest_list_tests enumerates all 27 io cases;
  grep for DISABLED_/GTEST_SKIP/GTEST_FILTER in IoAdaptersTest.cpp = 0 matches;
  the run reports 0 SKIPPED.
benchmark result: not required for Task 007.
git diff --check: clean in both repos (Velox tracked exit 0, 0 untracked-file
  whitespace errors; ClickHouse exit 0).
```

## Worker review (attempt 2)

```text
review subagent: exactly one fresh read-only code-review subagent
  ("task007-attempt2-review"), given the full Task 007 contract, the Controller
  amendment and Controller review 1, the complete current Velox diff
  (task_007_attempt2_full_velox.diff plus file paths), the design/behavioral
  references, and all attempt-2 logs. Instructed not to edit.
verdict: changes_requested (1 major + 1 minor).
findings:
  F1 (major): seek() did not clear armed external-buffer state. Per set()'s
    contract a seek ends the external buffer's validity, so set()+seek()+next()
    would pread into caller-owned storage the caller was allowed to release
    (use-after-free hazard reachable by the Task 014 consumer). In scope of the
    amendment's "never retain or write through stale caller-owned storage".
  F2 (minor, test-only): MockReadFile::callCount_ was never incremented (pread
    updates the base ReadFile::bytesRead_), so callCount() always returned 0 —
    a latent misleading test-double accessor.
resolutions:
  F1: added RED test SeekCancelsArmedExternalBuffer (genuine RED captured), then
    made seek() revert to the internal buffer and clear externalBuffer_. GREEN;
    reran fresh rebuild + all gates.
  F2: removed the unused, misleading callCount()/callCount_ (nothing references
    them; base ReadFile::bytesRead() already exists). No production behavior change.
  Production code changed after the review, so per protocol NO second reviewer
  was launched; the fixes and full gate reruns are documented here.
unresolved findings: none
false-green check: RED logs prove each new assertion fails against the unfixed
  code for the correct behavioral reason before it passes; no assertion was weakened
  and no test-only hook was added to production.
```

## Source-of-truth notes (attempt 2)

```text
- Followed the authoritative "Controller amendment after Worker attempt 1", which
  overrides the original task pseudo-code snippets.
- Behavior matches the ClickHouse behavioral references
  (src/IO/ReadBufferFromFileBase.h, src/IO/WriteBufferFromFile.h) at the semantic
  level: signed advance/seek arithmetic is bounds- and overflow-checked before the
  cursor mutates, and rejected operations leave state unchanged.
- Errors propagate via VELOX_CHECK_*/throwFileCacheException (VeloxException);
  no silent fallback masks a failure.
- No business contract was weakened; the seek()-clears-external-buffer fix follows
  set()'s documented single-cycle validity window (data valid only "until the next
  next, seek, or set").
- currentOffset_ invariant 0 <= currentOffset_ <= off_t max is maintained, so the
  off_t/size_t casts in seek()/getPosition() are safe.
```

## Repository statuses (final, attempt 2)

```text
Velox (/home/chang/OpenSource/velox): branch filecache, HEAD d9f4517c5.
  Dirty = " M velox/ch/CMakeLists.txt", "?? velox/ch/IO/" — exactly the eight
  declared Task 007 paths. Unstaged, uncommitted.
ClickHouse (/home/chang/SourceCode/ClickHouse): branch ch-filecache, HEAD
  5af6ab908fe. Dirty = " M port/task/007-filecache-io-adapters.md",
  " M port/task/CONTROLLER_HANDOFF.md", "?? port/task/result/007-filecache-io-adapters-result.md".
  The baseline "?? tmp/" (Controller scratch) is no longer present at final; it
  was removed externally. This Worker never created, modified, or deleted tmp/
  (all scratch and logs live under the Velox build dir). The Task 007 file and
  CONTROLLER_HANDOFF.md were not modified by this Worker.
```

## Blockers (attempt 2)

```text
None.
```

## Worker declaration (attempt 2)

```text
Only Task 007 was attempted; Task 008 was not started.
Changes are unstaged and uncommitted in both repositories.
No stage, commit, amend, rebase, push, PR, worktree, or branch change was performed.
The Task 007 file and CONTROLLER_HANDOFF.md were not modified.
The worker stopped after writing this receipt.
```

## Controller review 2

```text
controller_status: changes_requested
task: 007
worker_attempt_reviewed: 2
```

## Review evidence

```text
accepted attempt-2 corrections:
  Signed read advance, seek overflow/bounds, bidirectional EOF updates, invalid
  external-buffer inputs, write-advance overflow, and set()+seek() external
  buffer cancellation are implemented and covered by genuine RED evidence.
  The final fresh build recompiled all three Task 007 sources; 27/27 focused
  tests and all five Task 003-007 ctest suites passed with no disabled/skipped
  tests.

unresolved finding 1 (blocker, external-buffer lifetime):
  set() promises caller-owned memory is valid only until the next next(), seek(),
  or set() call. next() clears externalBuffer_ only after a non-empty pread.
  Early atEof/readUntil returns, an empty pread, and a pread exception all leave
  the external buffer armed. A caller may release that memory after next()
  returns false or throws; extending readUntil or retrying can then write into
  the expired pointer. Even after a successful external read, a following empty
  internal read changes bufData_ without resetting pos_/bufEnd_, leaving an
  incoherent window whose pointer subtraction is undefined.

unresolved finding 2 (blocker, read boundary):
  setReadUntilPosition updates only readUntil_ and atEof_. If next() has already
  loaded a window beyond a newly smaller boundary, bufferEnd() still exposes the
  full old window and advance() accepts consumption past the promised exclusive
  limit.

unresolved finding 3 (major, terminal writer state):
  write() and next() reject finalized/cancelled writers, but advance() does not.
  advance() after either terminal transition silently stages bytes and increments
  totalWritten_ even though no later flush can commit them.

cross-task impact:
  Task 014 stores and reuses these readers across region/segment boundaries.
  Stale caller memory and a buffer window wider than the active region can cause
  memory corruption or return bytes outside the requested region. The adapter
  must enforce these invariants centrally.
```

## Required changes

```text
1. Implement every rule in "Controller amendment after Worker attempt 2".
2. Add all five focused tests listed there before changing production code and
   capture a genuine attempt-3 behavioral RED.
3. Preserve every accepted attempt-1 and attempt-2 behavior and test.
4. Produce fresh final build, Task 003-007 regression, discovery, no-skip, and
   diff-check logs.
5. Launch exactly one fresh read-only review subagent for attempt 3, resolve all
   actionable in-scope findings, append Worker attempt 3, and stop.
```

## Worker attempt 3

```text
worker_status: ready_for_controller
task: 007
```

Rework of Worker attempts 1-2 per Controller review 2 (`controller_status:
changes_requested`). This section implements the three unresolved findings and
the "Controller amendment after Worker attempt 2". Worker attempt 1, Controller
review 1, Worker attempt 2, Controller review 2, its Required changes, and the
Commits section above are unchanged.

## Repository baselines (attempt 3)

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache` | `d9f4517c504b93dfc29e61bdfebd0f6cd3bf5a2e` | `M velox/ch/CMakeLists.txt`, `?? velox/ch/IO/` (attempt 1-2 work) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `5af6ab908fed76e079c7ed9bc96f45924d7ebe14` | `M port/task/007-...md`, `M port/task/CONTROLLER_HANDOFF.md`, `?? port/task/result/007-...-result.md` (all Controller-owned) |

## Files changed (attempt 3)

```text
velox/ch/IO/ReadBufferFromVeloxReadFile.cpp   (next() all-exits reset + try/catch rethrow; new resetToInternalWindow(); setReadUntilPosition() window constraint)
velox/ch/IO/ReadBufferFromVeloxReadFile.h     (declare private resetToInternalWindow())
velox/ch/IO/WriteBufferFromVeloxWriteFile.cpp (advance() rejects finalized_/cancelled_ before mutation)
velox/ch/IO/tests/IoAdaptersTest.cpp          (+ ThrowingReadFile mock; + 6 focused attempt-3 tests; 27 preserved -> 33 total)
```

The other four declared Task 007 paths are unchanged from attempt 2:
`velox/ch/CMakeLists.txt`, `velox/ch/IO/CMakeLists.txt`,
`velox/ch/IO/tests/CMakeLists.txt`, `velox/ch/IO/WriteBufferFromVeloxWriteFile.h`.
No file outside the eight declared paths was touched. The only ClickHouse write
is this appended section.

## TDD RED evidence (attempt 3)

Six focused tests were added first and run against the **unchanged attempt-2
production** binary. Genuine behavioral RED: 33 tests, 27 passed, exactly the 6
new tests FAILED, each for the expected defect (not a build/test mistake).

```text
build (test compiles vs attempt-2 production): cmake-build-debug-gcc13/build_task_007_attempt3_red.log  (exit 0)
run (RED):                                     cmake-build-debug-gcc13/test_task_007_attempt3_red.log   (exit 1)
result: [==========] 33 ran; [ PASSED ] 27; [ FAILED ] 6

F1 external-buffer lifetime / pointer coherence:
  ExternalBufferReleasedAtBoundaryThenNextUsesInternal  -> IoAdaptersTest.cpp:463 external buffer written "EFGH" on the resumed read (armed buffer survived a boundary-false next())
  ExternalBufferPreadThrowsThenRetryUsesInternal        -> IoAdaptersTest.cpp:503 external buffer written "ABCD" on retry (armed buffer survived a pread exception); original "simulated pread failure" propagated
  EmptyPreadAfterExternalReadKeepsCoherentInternalWindow-> IoAdaptersTest.cpp:531 getFileOffsetOfBufferEnd() != getPosition() (stale external pos_/bufEnd_ vs internal bufData_ = cross-allocation subtraction)
F2 read boundary constraint:
  ShrinkReadUntilConstrainsLoadedWindow                 -> IoAdaptersTest.cpp:553/555 bufferEnd still 8 (expected 4); :558 advance(5) accepted past the shrunk boundary
F3 terminal writer state:
  AdvanceAfterFinalizeThrows                            -> IoAdaptersTest.cpp:700/701 advance(1) accepted after finalize and moved position
  AdvanceAfterCancelThrows                              -> IoAdaptersTest.cpp:715/716 advance(1) accepted after cancel and moved position
```

## Production changes (attempt 3)

```text
ReadBufferFromVeloxReadFile::next()  (ReadBufferFromVeloxReadFile.cpp:60-116)
  - Every non-success exit (entry atEof_; currentOffset_ >= readUntil_; empty pread;
    pread exception) now calls resetToInternalWindow() and, for the exception path,
    catches, resets, then `throw;` rethrows the ORIGINAL ReadFile exception (no
    silent fallback).
  - The read destination is chosen locally (dest/destCapacity) and committed to
    bufData_/pos_/bufEnd_ only on a successful non-empty pread, so no non-success
    path ever leaves stale external pointers or forms cross-allocation pointers.
ReadBufferFromVeloxReadFile::resetToInternalWindow()  (ReadBufferFromVeloxReadFile.cpp:118-129, .h private)
  - Restores bufData_/bufCapacity_ to internalBuffer_, sets pos_==bufEnd_==bufData_
    (empty), bufStartOffset_=currentOffset_, externalBuffer_=false; preserves
    currentOffset_. Reads internalBuffer_.data()/size() directly (never the stale
    bufData_/bufCapacity_).
ReadBufferFromVeloxReadFile::setReadUntilPosition()  (ReadBufferFromVeloxReadFile.cpp:194-214)
  - After updating readUntil_/atEof_, constrains an already-loaded window in
    file-offset space: boundary at/behind current position -> empty window at pos_
    + EOF; boundary inside the loaded window -> clamp bufEnd_ to readUntil_;
    extending -> unchanged window, EOF cleared. Unsigned math cannot underflow
    (clamp branch guarantees currentOffset_ < readUntil_ and bufStartOffset_ <=
    currentOffset_) and cannot expose external memory.
WriteBufferFromVeloxWriteFile::advance()  (WriteBufferFromVeloxWriteFile.cpp:70-84)
  - Rejects finalized_/cancelled_ with VELOX_CHECK before the capacity subtraction
    and before mutating writePos_/totalWritten_, matching write()/next().
```

All accepted attempt-1/attempt-2 behavior is preserved: signed `advance` bounds,
`seek` `SEEK_SET`/`SEEK_CUR` overflow and before-start checks, `seek` disarming an
armed external buffer, bidirectional EOF in `setReadUntilPosition`, `set` null/zero
rejection, write-advance overflow-safe check, mono-safe CMake registration, and
the shared-ownership test.

## Commands and outcomes (attempt 3)

| Command purpose | Exit code | Log |
|---|---:|---|
| Build io test vs unchanged production (RED) | 0 | `cmake-build-debug-gcc13/build_task_007_attempt3_red.log` |
| Run io test (RED: 27 pass / 6 fail) | 1 | `cmake-build-debug-gcc13/test_task_007_attempt3_red.log` |
| Build io test with fixes (GREEN) | 0 | `cmake-build-debug-gcc13/build_task_007_attempt3_green.log` |
| Run io test (GREEN: 33/33) | 0 | `cmake-build-debug-gcc13/test_task_007_attempt3_green.log` |
| Fresh final rebuild (3 sources recompiled + relink) | 0 | `cmake-build-debug-gcc13/build_task_007_attempt3_final.log` |
| Build Task 003-007 regression targets | 0 | `cmake-build-debug-gcc13/build_task_007_attempt3_regression.log` |
| Task 003-007 combined ctest gate (5/5) | 0 | `cmake-build-debug-gcc13/test_task_007_attempt3_all_gates.log` |
| Final io run (authoritative, 33/33) | 0 | `cmake-build-debug-gcc13/test_task_007_attempt3_io_final.log` |
| ctest -N + gtest_list_tests discovery | 0 | `cmake-build-debug-gcc13/discovery_task_007_attempt3.log` |
| No-disabled/no-skipped verification | 0 | `cmake-build-debug-gcc13/noskip_task_007_attempt3.log` |
| git diff --check both repos + untracked scan | 0 | `cmake-build-debug-gcc13/gitcheck_task_007_attempt3.log` |
| Consolidated Velox diff for reviewer | - | `cmake-build-debug-gcc13/task_007_attempt3_full_velox.diff` |

## Acceptance evidence (attempt 3)

```text
test count:
  velox_ch_io_test: 33 tests (23 ReadBufferFromVeloxReadFileTest + 10 WriteBufferFromVeloxWriteFileTest), all pass.
  Fresh final rebuild recompiled ReadBufferFromVeloxReadFile.cpp, WriteBufferFromVeloxWriteFile.cpp,
  IoAdaptersTest.cpp and relinked libvelox.a + velox_ch_io_test (build_task_007_attempt3_final.log).
  Task 003-007 ctest gate: 5/5 suites passed (#406 velox_ch_common_test, #407 velox_ch_threadpool_test,
  #408 velox_ch_scheduler_test, #409 velox_ch_guards_test, #410 velox_ch_io_test), 100%, 0 failed.
failed tests: none.
skipped/disabled tests: none. No DISABLED_/GTEST_SKIP in source; 0 disabled in gtest listing;
  final run shows "[ PASSED ] 33 tests." with no SKIPPED/DISABLED lines (noskip_task_007_attempt3.log).
discovery: ctest -N lists the 5 Task 003-007 suites; gtest_list_tests lists 33 io cases (discovery_task_007_attempt3.log).
git diff --check: clean in both repositories; all 7 untracked IO files pass an explicit
  git diff --check --no-index whitespace scan (gitcheck_task_007_attempt3.log).
```

## Worker review (attempt 3)

```text
review subagent: one fresh read-only code-review subagent "task007-attempt3-review"
  (distinct from the attempt-1 and attempt-2 reviewers); made no edits.
inputs provided: full Task 007 contract + both Controller amendments + Controller reviews 1 and 2,
  the consolidated Velox diff (task_007_attempt3_full_velox.diff), all attempt-3 build/test/discovery/
  no-skip/git-check logs, and the current production/test/CMake files.
verdict: approved.
findings: none (no blocker/major/minor).
  Verified: all 5 next() exits reset correctly and the catch rethrows the original exception with a
  non-throwing reset; bufData_/pos_/bufEnd_ always share one allocation (no cross-allocation UB);
  setReadUntilPosition unsigned math cannot underflow and never exposes external memory; writer advance
  rejects terminal states before mutation; every accepted attempt-1/2 fix preserved; the 6 RED tests are
  genuine (not weak assertions) and ThrowingReadFile proves the original exception propagates; scope is
  the 8 declared paths only; CMake registration is the accepted mono-safe form; Task 014 reuse invariants
  (no stale caller memory, no out-of-region bytes) hold.
resolutions: no actionable finding required a code change; therefore no second reviewer was launched
  (production code was not modified after the review).
unresolved findings: none.
```

## Source-of-truth notes (attempt 3)

```text
- The fix follows "Controller amendment after Worker attempt 2" literally: every attempted next()
  ends the external-buffer lifetime; every non-success path restores a coherent empty internal window,
  preserves currentOffset_, and rethrows ReadFile exceptions unchanged; no silent fallback (bare throw;).
- Validation itself avoids UB/overflow: setReadUntilPosition works in file-offset space and only clamps
  when currentOffset_ < readUntil_ < windowEndOffset (no unsigned underflow); the read destination is
  committed only after a successful pread, so no non-success path forms an out-of-range or cross-allocation
  pointer.
- The empty-pread coherence test asserts the CORRECT invariant (getFileOffsetOfBufferEnd() == getPosition());
  its RED against attempt-2 arises from the exact cross-allocation pointer subtraction the amendment targets,
  and its GREEN value is deterministic. No assertion was weakened and no buggy literal snippet was copied.
```

## Repository statuses (final, attempt 3)

```text
Velox      /home/chang/OpenSource/velox      branch filecache      HEAD d9f4517c504b93dfc29e61bdfebd0f6cd3bf5a2e
  dirty: M velox/ch/CMakeLists.txt ; ?? velox/ch/IO/ (7 files) -- exactly the 8 declared Task 007 paths, unstaged/uncommitted.
ClickHouse /home/chang/SourceCode/ClickHouse branch ch-filecache  HEAD 5af6ab908fed76e079c7ed9bc96f45924d7ebe14
  dirty: M port/task/007-filecache-io-adapters.md (Controller) ; M port/task/CONTROLLER_HANDOFF.md (Controller) ;
         ?? port/task/result/007-filecache-io-adapters-result.md (this receipt, appended only).
```

## Blockers (attempt 3)

```text
None.
```

## Worker declaration (attempt 3)

```text
Only Task 007 was attempted; Task 008 was not started.
Changes are unstaged and uncommitted; no stage/commit/amend/rebase/push/PR/worktree/branch change was performed.
The task file and CONTROLLER_HANDOFF.md were not modified; no earlier receipt section was altered.
The worker stopped after writing this receipt.
```

## Controller review 3

```text
controller_status: accepted
task: 007
worker_attempt_reviewed: 3
```

## Review evidence

```text
scope review:
  Velox contained exactly the eight declared Task 007 paths and no staged or
  unrelated changes. ClickHouse contained only the Controller-owned task
  amendments, this receipt, and the handoff update.

implementation review:
  Every next() outcome now ends an armed external-buffer cycle correctly.
  False and exceptional outcomes restore bufData_, bufCapacity_, pos_, and
  bufEnd_ to one coherent empty internal window while preserving currentOffset_;
  ReadFile exceptions are rethrown unchanged. A successful read publishes only
  the selected destination and disarms it for the following cycle.

  setReadUntilPosition constrains an already-loaded window in offset space. Its
  clamp subtraction is guarded by bufStartOffset_ <= currentOffset_ <
  readUntil_, and the loaded-window end cannot exceed the prior read boundary.
  advance validates signed counts before pointer arithmetic; seek validates
  negative results and positive overflow before addition. Writer advance now
  rejects finalized/cancelled state before any mutation and validates capacity
  without unsigned wrap.

cross-task architecture review:
  The adapters remain leaf dependencies on Task 003 shims and Velox file
  interfaces. They introduce no dependency on FileCache/FileSegment or
  scheduling. The state invariants required by Task 014 reader reuse hold:
  caller-owned memory is never reused after its promised lifetime, and a
  narrowed region boundary cannot expose out-of-region bytes.

log and test review:
  Attempt 3 contains genuine behavioral RED: all six new tests failed against
  attempt-2 production for the exact lifetime, pointer-coherence, boundary, and
  terminal-writer defects. The fresh final build recompiled both production
  sources and IoAdaptersTest.cpp. Worker final evidence passed 33/33 focused
  cases and all five Task 003-007 suites with no disabled/skipped tests.
  Controller independently reran the five suites in
  cmake-build-debug-gcc13/test_task_007_controller_final.log: 5/5 passed,
  0 failed, in 2.43 seconds.

unresolved findings:
  None.
```

## Required changes

```text
None.
```

## Accepted implementation commit

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `711a848501d54dffaf5afc53278a97bb10825aa3` |

## Worker attempt 4

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 007
```

Corrective rework implementing the **post-acceptance replacement contract**
(task lines 8-213, which supersede every later reader/writer API, pseudo-code,
lifecycle, and test instruction), per the canonical design
`port/design/filecache-reader-handoff-and-contract-recovery.html`. Worker
attempts 1-3, Controller reviews 1-3, and the accepted attempt-3 implementation
commit above are unchanged; this section is appended only. This attempt runs on
the `root-oss` profile (attempts 1-3 ran on `home-chang`).

## Repository baselines (attempt 4)

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/clickhouse` (protocol/receipts) | `ch-filecache` | `b9cf0dbd348` | clean |
| `/root/oss/velox` (implementation) | `filecache` | `b3c2832e18f76b574faf74e2d6ba05c2da741efd` | clean |

Both repositories were clean at dispatch and matched the stated baselines. The
accepted attempt-3 IO adapters are committed in Velox at
`711a848501d5...` (Task 007 commit), so this attempt's edits appear as
modifications to five already-tracked files.

## Dependency / source-contract table (derived from CH source, design, and real callers)

| External CH class/API/lifecycle | Reviewed mapping (design §) | Velox replacement | Notes |
|---|---|---|---|
| `BufferBase` state (internal/working buffer, pos, offset, available, count, hasPendingData) | §5, §7.1 | `FileCacheBufferState` + `CacheBuffer` | pool-charged `BufferPtr`, non-owning working view, settled `bytes` |
| `ReadBuffer::next` (settle `bytes+=offset`; nextImpl; exception cancel+rethrow; false→empty; true→publish+reset pos) | §3 (ReadBuffer.cpp:103-143), task 62-93 | `ReadBufferFromFileBase::next` | exception state **terminal**; second `next` rejected (`VELOX_CHECK(!canceled_)`) |
| `ReadBuffer::eof` = `!hasPendingData() && !next()` | §4, task 73-75 | base `eof()` | fills the buffer |
| `ReadBuffer::set(ptr,size)` persist until explicit set; `set(nullptr,0)` detach | §2, §4, task 76-82 | base `set()` + `restoreOwnedWindow` | detach restores owned window, preserves file offset, drops caller ref |
| `SeekableReadBuffer` seek/getPosition/getFileOffsetOfBufferEnd/supportsRightBoundedReads | §5, task 56-60 | base impl over `fileOffsetOfBufferEnd_`/`readUntil_` | getPosition = bufEnd − available |
| `ReadBuffer::setReadUntilPosition/End` (shrink clamps loaded window, extend resumes) | task 92, controller-amend-2 | base `setReadUntilPosition` | offset-space clamp, no unsigned underflow |
| `ReadBufferFromFileBase` getFileName/tryGetFileSize | task 60 | virtual/`fileSize_` | |
| `FileSegment::RemoteFileReaderPtr = shared_ptr<ReadBufferFromFileBase>` | §5, task 42 | `ch::ReadBufferFromFileBase` polymorphic base | virtual dtor + concrete surface over one `readInto` virtual |
| Owned memory = pool `BufferPtr`/`AlignedBuffer` (never `std::vector<char>`) | §6, §7.1 | `AlignedBuffer::allocate<char>` charged to injected `MemoryPool` | |
| `ReadFile::directIo(alignment)` at construction; owned+external aligned or reject | §6, task 96-101 | `readInto`/`set` alignment enforcement | power-of-two owned alloc; misaligned external rejected, no fallback |
| `ReadFile::pread` | §5, §7.1 | `readInto` → `pread(off,len,dest)` | returns bytes read |
| `WriteBuffer::next` (no-op if canceled/offset 0; append; settle; reset; exception settle+cancel+rethrow) | §7.4 (WriteBuffer.cpp) | `WriteBufferFromFileBase::next` | |
| `WriteBuffer::finalize/cancel/sync` (finalize idempotent+throw-if-canceled; cancel noexcept idempotent no-op-after-finalize) | §7.4 | base finalize/cancel/sync | |
| `FileSegment::write` shape: `set(from,size,offset=size); SCOPE_EXIT set(nullptr,0); next()`; buffer size 0 | §7.3, FileSegment.cpp | writer external zero-copy path | `set` never touches the file → safe after cancel |
| Owned writer buffer size 0 = external-only; >0 = pool `BufferPtr` | §7.1, task 127-131 | ctor branch | |
| `unique_ptr<WriteFile>` single ownership; wrapper is `shared_ptr` | §7.3, task 168-171 | `WriteBufferFromVeloxWriteFile` owns one `unique_ptr` | |
| `WriteFile::append/flush/close/getName` | §7.4 map | nextImpl/syncImpl/finalizeImpl/getFileName | getFileName cached to survive cancel release |
| Application zero-copy (no reader→writer staging memcpy) | §7.2 | `append(string_view(workingBegin, offset))` | |
| Resume (append-mode open) + partial-write `fs::file_size` reconciliation | §7.3, §7.5 | **FileSegment (Task 013/014)**, NOT the adapter | see scope decision below |

Every mapping above is explicitly reviewed in the canonical design or the
replacement task section; no name-only mapping, old shim, old receipt, or
conflicting later snippet was relied upon. Velox APIs verified against current
source (`velox/common/file/File.h`: `ReadFile::directIo`, `pread`;
`WriteFile::append/flush/close/getName`; `velox/buffer/Buffer.h`:
`AlignedBuffer::allocate`, `BufferPtr`).

## Scope decision: resume / partial-write reconciliation (task lines 168-180)

Per the binding rule ("binding only where Task 007's declared adapter scope
implements them"), resume and partial-write reconciliation are **FileSegment**
responsibilities and are out of the adapter's declared file scope, confirmed
directly from CH source `FileSegment::write` (`src/Interpreters/FileCache/FileSegment.cpp`):
the append-mode open (`O_WRONLY | O_APPEND`) and the `fs::file_size` /
`downloaded_size` reconciliation live in `FileSegment::write`, not in the
`WriteBufferFromFile` adapter. The adapter's in-scope obligations are
(a) append without truncating, and (b) propagate the append exception unchanged
and transition to canceled so FileSegment can reconcile. Both are implemented
and tested (writer tests 10, 11, 8). This is explicit attribution per the design
(§7.3/§7.5), **not** a silently deferred adapter contract, and **no**
filesystem-opening/`fs::file_size` helper was invented inside the adapter. No
block was warranted: no new file outside scope is required, and every mapping is
reviewed.

## Concrete divergences of the accepted attempt-3 implementation vs. the replacement contract (root cause)

```text
1. eof() only returned a latched atEof_ flag; contract requires !hasPendingData() && !next().
2. A pread exception reset to an internal window and allowed retry; contract requires a terminal,
   non-retryable canceled state.
3. set(nullptr,0) was rejected (VELOX_CHECK_NOT_NULL); contract requires it to detach.
4. An external buffer auto-reverted to internal after one next(); contract requires it to persist
   until an explicit set().
5. Only read-only position()/bufferEnd() were exposed; contract requires the full BufferBase surface
   (internalBuffer/buffer/position/offset/available/hasPendingData/count) plus cancel/isCanceled/
   setReadUntilEnd/supportsExternalBufferMode/supportsRightBoundedReads/tryGetFileSize.
6. Owned memory was std::vector<char>, unaccounted and outside MemoryPool; no direct-IO alignment.
7. Writer took shared_ptr<WriteFile>, rejected buffer size 0, and had no external set(from,size,offset)
   zero-copy path; contract requires a single unique_ptr<WriteFile>, buffer size 0 external-only, and
   a no-staging-copy append.
8. Writer finalize threw on repeat and cancel threw after finalize; contract requires idempotent
   finalize and a noexcept/idempotent cancel that is a no-op after finalize.
9. No ch::ReadBufferFromFileBase / ch::WriteBufferFromFileBase compatibility base for
   FileSegment::RemoteFileReaderPtr / the writer wrapper to store.
```

## Files changed (attempt 4)

```text
# /root/oss/velox  (modified — the 5 adapter/test files needing the replacement contract)
velox/ch/IO/ReadBufferFromVeloxReadFile.h     (FileCacheBufferState, CacheBuffer, ch::ReadBufferFromFileBase, ReadBufferFromVeloxReadFile)
velox/ch/IO/ReadBufferFromVeloxReadFile.cpp
velox/ch/IO/WriteBufferFromVeloxWriteFile.h   (ch::WriteBufferFromFileBase, WriteBufferFromVeloxWriteFile)
velox/ch/IO/WriteBufferFromVeloxWriteFile.cpp
velox/ch/IO/tests/IoAdaptersTest.cpp          (full new-contract suite: 24 tests)

# /root/oss/clickhouse
port/task/result/007-filecache-io-adapters-result.md   (this appended section only)
```

The three declared CMake files (`velox/ch/CMakeLists.txt`,
`velox/ch/IO/CMakeLists.txt`, `velox/ch/IO/tests/CMakeLists.txt`) needed **no**
change: no files were added or removed, so the accepted mono-safe registration
still applies. No file outside the declared Task 007 scope was touched.

## TDD RED evidence (attempt 4)

Genuine behavioral RED captured against the **unchanged accepted attempt-3
production** using only the pre-correction public API surface, so each failure is
a real behavioral/state/ownership difference (not an API-shape compile error).
Build `task007_attempt4_red_build.log` (exit 0), run
`task007_attempt4_red_run.log` (exit 1): **7 tests, 0 passed, 7 FAILED**, each
for the exact superseded-contract reversal:

```text
Attempt4RedTest.SetNullDetachesInsteadOfRejecting  -> old threw "External buffer must not be null" (contract: detach)
Attempt4RedTest.ExternalBufferPersistsAcrossReads  -> ext still "AAAA" after 2nd read (old auto-reverted to internal)
Attempt4RedTest.PreadExceptionIsTerminalNoRetry    -> preadCalls()==2 (old retried; contract: terminal, ==1)
Attempt4RedTest.EofFillsBuffer                     -> position()==bufferEnd() (old eof() did not fill)
Attempt4RedTest.WriterBufferSizeZeroAllowed        -> old threw "WriteBuffer size must be > 0"
Attempt4RedTest.FinalizeIsIdempotent               -> old threw on repeat finalize
Attempt4RedTest.CancelAfterFinalizeIsNoop          -> old threw on cancel-after-finalize
```

These cover the reopening reasons in design §4 (eof triggers read; exception
terminal; detach; external persist; writer buffer-size-0 attach; writer teardown
idempotency).

New-API-only replacement behaviors (buffer-state surface, pool accounting,
direct-IO alignment, unique_ptr ownership, external zero-copy) cannot be executed
by the old API. Their genuineness is proven by two **mutation proofs** against the
new production (`task007_attempt4_mutation_build.log`,
`task007_attempt4_mutation_run.log`, exit 1):

```text
IoAdaptersTest.WriterExternalZeroCopyAppend  FAILS when nextImpl appends a staging std::string copy
IoAdaptersTest.ReaderDirectIoAlignment       FAILS when the set() alignment checks are disabled
```

Both were then reverted; `grep MUTATION PROOF velox/ch/IO/` = none.

## Commands and outcomes (attempt 4)

| Command purpose | Exit | Log |
|---|---:|---|
| Baseline build/run of accepted attempt-3 test (33/33) | 0 | `_build/debug/task007_attempt4_baseline_build.log` |
| RED build (7 old-API tests vs unchanged production) | 0 | `_build/debug/task007_attempt4_red_build.log` |
| RED run (7/7 FAIL, genuine behavioral) | 1 | `_build/debug/task007_attempt4_red_run.log` |
| Green build (new production + suite) | 0 | `_build/debug/task007_attempt4_green_build.log` |
| Green run (22/24; 2 pool tests needed local MemoryManager) | 1 | `_build/debug/task007_attempt4_green_run.log` |
| Rebuild after pool-fixture fix | 0 | `_build/debug/task007_attempt4_green_build2.log` |
| Green run (24/24) | 0 | `_build/debug/task007_attempt4_green_run2.log` |
| Mutation-proof build | 0 | `_build/debug/task007_attempt4_mutation_build.log` |
| Mutation-proof run (2/2 FAIL as expected) | 1 | `_build/debug/task007_attempt4_mutation_run.log` |
| Fresh final build (touch all 3 sources; recompile both .cpp + test, relink libvelox.a + velox_ch_io_test) | 0 | `_build/debug/task007_attempt4_final_build.log` |
| Final io run (24/24) | 0 | `_build/debug/task007_attempt4_io_final.log` |
| gtest discovery (--gtest_list_tests) = 24 | 0 | `_build/debug/task007_attempt4_discovery.log` |
| Task 003-007 combined ctest gate (5/5) | 0 | `_build/debug/task007_attempt4_all_gates.log` |
| Regression targets build (003-006 + 007) | 0 | `_build/debug/task007_attempt4_regression_build.log` |
| Post-review hardening rebuild | 0 | `_build/debug/task007_attempt4_postreview_build.log` |
| Post-review gate rerun (5/5, io 24/24) | 0 | `_build/debug/task007_attempt4_postreview_gates.log` |
| Consolidated Velox diff for reviewer | - | `_build/debug/task007_attempt4_full_velox.diff` |

Configure was already current in `/root/oss/velox/_build/debug` (root-oss:
`VELOX_MONO_LIBRARY=ON`, `VELOX_BUILD_TESTING=ON`, vcpkg toolchain). Every build
sourced `/root/oss/velox-helper/env.sh` and used `/usr/local/bin/ninja` with no
`-j`. `build.sh` was not used as evidence.

## Fresh-build proof

`task007_attempt4_final_build.log` shows, after touching all three sources:
`Building CXX .../ch/IO/WriteBufferFromVeloxWriteFile.cpp.o`,
`Building CXX .../ch/IO/ReadBufferFromVeloxReadFile.cpp.o`,
`Building CXX .../IoAdaptersTest.cpp.o`, then
`Linking CXX static library lib/libvelox.a` and
`Linking CXX executable velox/ch/IO/tests/velox_ch_io_test` — i.e. fresh
compilation of both production `.cpp` files and `IoAdaptersTest.cpp` plus a relink
of `libvelox.a` and `velox_ch_io_test`.

## Acceptance evidence (attempt 4)

```text
test count: velox_ch_io_test = 24 tests (13 Reader..., 11 Writer...), all pass.
  Registered with ctest as #434 and run under ctest (Passed).
failed tests: none (final).
skipped/disabled tests: none. grep DISABLED_/GTEST_SKIP/GTEST_FILTER in IoAdaptersTest.cpp = 0;
  --gtest_list_tests enumerates all 24; the run reports no SKIPPED/DISABLED lines.
discovery: task007_attempt4_discovery.log lists 24 cases.
Task 003-007 ctest gate: 5/5 suites passed (#427 common, #428 threadpool, #429 scheduler,
  #432 guards, #434 io), 100%, 0 failed (task007_attempt4_postreview_gates.log).
git diff --check (Velox): clean (exit 0); no untracked stray files under velox/ch/IO/.
```

## Worker review (attempt 4)

```text
review subagent: exactly one fresh read-only code-review subagent
  ("task007-attempt4-review"), given the replacement contract (task lines 8-213),
  the canonical HTML design, the CH source/real-caller references (BufferBase,
  ReadBuffer(.cpp), SeekableReadBuffer, WriteBuffer(.cpp), FileSegment::write,
  CachedOnDiskReadBufferFromFile), the Velox primitives, the full 5-file diff, and
  the test/mutation/RED evidence. Instructed not to edit; it made no edits.
verdict: no blocker/major/minor defects; "the implementation faithfully realizes
  the replacement contract." It independently confirmed the reader next() state
  machine, set/detach persistence + handoff invariants, setReadUntilPosition
  clamp (no unsigned underflow, never exposes bytes >= boundary), seek overflow
  guards, direct-IO owned allocation staying in-bounds and pool accounting, the
  full writer lifecycle (append-exactly-offset, finalize=next+close no extra
  flush, cancel noexcept via unique_ptr reset matching LocalWriteFile's
  non-throwing dtor), zero-copy address identity, no smuggled resume/fs::file_size
  logic, and that FileSegment::RemoteFileReaderPtr can store the base. It judged
  all 24 tests genuine (not false-green).
findings (both explicitly below the reporting bar / informational, not defects):
  I1. set() direct-IO offset check validated getPosition() rather than the actual
      next-read offset (equal only when available()==0, which the flow guarantees).
  I2. direct-IO alignment was assumed power-of-two but not asserted.
resolutions:
  I1: changed set() to validate fileOffsetOfBufferEnd_ % alignment (the actual
      next-read offset) — strictly more correct.
  I2: added VELOX_CHECK((alignment & (alignment-1)) == 0) at construction.
  Rebuilt and reran all gates GREEN (24/24 io, 5/5 ctest;
  task007_attempt4_postreview_*.log). Production changed after the review, so per
  protocol NO second reviewer was launched; the fixes are narrow hardening on the
  direct-IO path (no contracted-path behavior change) and the affected gates were
  rerun.
unresolved findings: none.
false-green check: 7 old-API behavioral RED tests failed against the prior
  production for the correct reasons; 2 mutation proofs show the new-API zero-copy
  and direct-IO tests fail when the behavior is broken; no assertion was weakened
  and no test-only hook was added to production.
```

## Blockers (attempt 4)

```text
None. No unreviewed dependency, no required file outside the declared scope, and
no unresolved source/design/task disagreement. Resume/partial-write filesystem
reconciliation is correctly attributed to FileSegment (Task 013/014) per the
canonical design and CH source, not silently deferred.
```

## Repository statuses (final, attempt 4)

```text
Velox      /root/oss/velox      branch filecache      HEAD b3c2832e18f76b574faf74e2d6ba05c2da741efd
  dirty: M velox/ch/IO/ReadBufferFromVeloxReadFile.{h,cpp}, M velox/ch/IO/WriteBufferFromVeloxWriteFile.{h,cpp},
         M velox/ch/IO/tests/IoAdaptersTest.cpp  (exactly the 5 needed declared files; unstaged/uncommitted).
ClickHouse /root/oss/clickhouse branch ch-filecache HEAD b9cf0dbd348
  dirty: ?? port/task/result/007-filecache-io-adapters-result.md (this appended receipt only).
```

## Worker declaration (attempt 4)

```text
Only Task 007 was attempted; Task 008 was not started.
Changes are unstaged and uncommitted; no stage/commit/amend/rebase/push/PR/worktree/branch change was performed.
No ClickHouse task/design/protocol/environment/handoff file was modified; no earlier receipt section was altered.
The worker stopped after writing this receipt.
```

## Controller review 4

```text
controller_status: changes_requested
environment_profile: root-oss
task: 007
worker_attempt_reviewed: 4
```

## Review evidence

```text
scope review:
  Worker attempt 4 changed exactly the five existing Task-007 Velox adapter/test
  files and appended this receipt. No CMake, later-task, design, protocol, or
  handoff file was changed by the Worker. Both repositories were unstaged and
  uncommitted at review time.

source-contract and architecture review:
  The compatibility bases, MemoryPool-backed owned buffers, persistent external
  reader target, terminal reader exception state, unique WriteFile ownership,
  and zero-copy writer attach follow the replacement contract. CH source and
  canonical design place append-mode opening and physical-size reconciliation in
  FileSegment, which Task 012 ports, not in the already-open WriteFile adapter.

implementation review:
  The flattened buffer state has undefined behavior on the mandatory
  set(nullptr, 0) path. FileCacheBufferState::set computes ptr + size and
  ptr + offset even when ptr is null, while CacheBuffer::size and the shared
  offset/available accessors subtract null pointers. The writer test itself calls
  offset after detach, so the green run executes this undefined behavior with
  sanitizers disabled. set also does not reject offset greater than size, which
  can create an out-of-range cursor and unsigned underflow.

  WriteBufferFromFileBase::cancel releases WriteFile and marks canceled but does
  not discard the pending working state or detach caller memory. A canceled
  external writer therefore retains the caller pointer and nonzero offset,
  contrary to the exact cancel contract.

  Direct-IO validation covers external set address/size/current offset only.
  seek can install an unaligned next-read offset, and nextImpl can pass an
  unaligned final/right-bound length to ReadFile::pread. Real LocalReadFile
  requires full aligned O_DIRECT reads, but MockReadFile accepts every
  offset/length and the direct-IO test never performs a read, so this path is
  currently false-green.

test and mandatory-evidence review:
  Mandatory writer test 10 does not open a file in append/no-truncate mode; it
  preloads an in-memory observer after the WriteFile has already been created.
  Mandatory writer test 11 configures its mock to throw before appending any
  byte, so it does not exercise a partial physical write or reconcile
  filesystem size against downloaded/reserved sizes. The adapter cannot prove
  either behavior because it has no path, downloaded size, reserved size, or
  file-opening responsibility.

  Worker RED logs correctly show seven superseded behaviors failing. Final logs
  show 24/24 focused and 5/5 combined success, but no log proves real partial
  physical-write reconciliation or a real direct-IO read at unaligned
  EOF/right-bound/seek. The initial green_run2 receipt claim also disagrees with
  its log (the two pool-accounting tests still failed there); later final and
  post-review gates are green, so this is a receipt-accuracy issue rather than
  the primary blocker.

independent review:
  A fresh read-only Controller review independently confirmed the mandatory
  writer-test scope conflict and the untested direct-IO read failure. Local
  Controller tracing additionally found the null-pointer arithmetic and
  cancel-state defects above.

unresolved findings:
  1. Null-detach and invalid-offset buffer-state operations are not memory-safe.
  2. cancel does not discard/detach pending working state.
  3. Direct-IO seek/read-length alignment is neither enforced nor tested.
  4. Mandatory writer tests 10-11 require FileSegment behavior outside Task 007
     and conflict with the canonical design/task sequence.
```

## Required changes

```text
1. Make null/zero an explicit coherent empty state without null pointer
   arithmetic; make size/offset/available null-safe; reject offset > size.
   Add focused tests for detach accessors/finalize and invalid offsets.
2. Make cancel discard the pending cursor and detach external memory while
   remaining noexcept/idempotent. Prove offset is zero and no caller pointer is
   retained after cancel and append failure.
3. Resolve the direct-IO contract. At minimum, fail closed before issuing an
   unaligned offset/length and add a mock that enforces alignment during actual
   reads. If non-block-sized file tails/right bounds must work, approve and
   specify the required underlying ReadFile cooperation instead of adding a
   buffered fallback.
4. User decision required for mandatory tests 10-11:
   recommended: keep Task 007 adapter-only, replace these two tests with genuine
   adapter obligations, and amend Task 012 to prove append-mode resume and
   physical-size/downloaded-size reconciliation in FileSegment;
   alternatives: delay Task 007 acceptance until Task 012, or expand Task 007
   into the center SCC (not recommended).
5. Correct the attempt-4 status/log statements, append a new Worker attempt after
   the approved task/design amendment, capture genuine RED for every in-scope
   fix, rerun all gates, and launch one fresh read-only review.
```

## Commits

No implementation or acceptance commit was created.

## Controller unblock response 1

```text
controller_status: waiting_for_user
environment_profile: root-oss
task: 007
```

## Resolution

```text
root cause:
  The replacement task lists resume/no-truncate and partial physical-write
  reconciliation as mandatory Task-007 writer tests, while CH source and the
  canonical design assign file opening, physical file size, downloaded size,
  reserved size, and failure publication to FileSegment. FileSegment is not in
  Task 007 scope and is ported in Task 012.

decision:
  Waiting for the user to choose the task boundary. The Controller recommends
  preserving the adapter-only boundary and moving the two integration
  obligations into an explicit Task-012 amendment.

task or environment update:
  None until the user approves the boundary. Do not edit the canonical design or
  numbered tasks and do not redispatch Task 007 yet.

evidence:
  CH FileSegment.cpp owns append-mode opening and filesystem-size
  reconciliation; Task 012 owns FileSegment.h/.cpp. The current mock throws
  before any partial write and cannot observe downloaded/reserved sizes.

redispatch same task: no (waiting for user)
```

## Controller unblock response 2

```text
controller_status: blocker_resolved
environment_profile: root-oss
task: 007
```

## Resolution

```text
root cause:
  Task 007 can prove only behavior observable from an already-open WriteFile.
  Append-mode opening, physical path, downloaded/reserved sizes, and
  filesystem-size reconciliation belong to production FileSegment in Task 012.
  Existing CH tests cover ordinary FileSegment writes, source failures, partial
  states, cache-disk failure before write, and truncated cache recovery, but no
  existing test precisely covers append-mode continuation plus an append that
  physically writes a strict prefix before throwing and then reconciles size.

decision:
  User approved the Controller recommendation:
    Task 007 keeps the adapter-only boundary and tests its observable half.
    Task 012 owns mandatory production FileSegment UTs for resume/no-truncate
    and partial physical-write reconciliation.

task or environment update:
  Updated the Task-007 replacement contract with adapter-only tests 10-11 and
  the attempt-4 safety/direct-IO findings.
  Updated Task 012 with two mandatory production FileSegment tests and explicit
  false-green rejection.
  Updated the canonical reader/writer handoff design with Task-012 ownership.

evidence:
  CH FileSegment.cpp owns O_APPEND opening and filesystem-size reconciliation;
  Task 012 owns FileSegment.h/.cpp. The current adapter owns only an already-open
  WriteFile and cannot observe downloaded/reserved state.

redispatch same task: yes
```
