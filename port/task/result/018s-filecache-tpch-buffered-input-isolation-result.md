worker_status: blocked
environment_profile: root-oss
task: 018S
phase: step0_baseline
one_driver_validity: not_reached
four_driver_executed: false
root_cause_claimed: false
blocked_at: STEP 0 (baseline verification, before any changes)
blocker_class: pre_existing_red_baseline (build-configuration, Task-018S-unrelated)

# Task 018S — FileCache TPCH BufferedInput Isolation — BLOCKED at STEP 0

## Summary

STEP 0 of the task requires running four baseline test binaries *before any
changes* and stopping if **any** baseline is RED. Three of the four baselines
are GREEN, but `velox_ch_filecache_buffered_input_test` is **RED** on the
unmodified tree: **34 passed, 4 failed, and 1 test hangs indefinitely**.

The RED baseline is **not** caused by Task-018S (no source was modified; the
working tree is clean at the documented HEAD). It is a pre-existing,
build-configuration reality: the specified build directory
`relwithdebinfo-vcpkg-arrow` compiles with `-DNDEBUG`, which turns the
`TestValue` fault-injection hooks into no-ops. All 5 failing/hanging `ch` tests
depend on those injection hooks, so they cannot pass (or terminate) in this
build.

Per the STEP 0 stop condition and the task's fail-close protocol, I stopped
immediately, preserved all raw logs, made **no** source/build/git changes, and
did **not** claim any performance root cause.

## Environment / identity (no changes made)

- Profile: `root-oss`
- Velox repo: `/root/oss/velox`, branch `filecache`, HEAD `0c5b5918eb8374f39b09248be091da94bf4d72f0`
- Working tree: **clean** (`git status --short` empty) — no files modified, staged, committed, or pushed.
- Build dir: `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow`
- Build type: `RelWithDebInfo`; `CMAKE_CXX_FLAGS_RELWITHDEBINFO = -O2 -g -DNDEBUG`

## Baseline build

Built the four STEP 0 test targets (unmodified tree) with the task's build
command pattern:

- Targets: `velox_ab_benchmark_schema_test velox_dwio_common_test velox_hive_filecache_buffered_input_test velox_ch_filecache_buffered_input_test`
- Build log: `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_baseline.log`
- Result: all four targets built and linked successfully (`[1033/1033]`).
- Binary mtime `2026-07-25 17:58` is newer than all `ch` sources (`2026-07-24`), confirming the binary reflects current source.

## Baseline results

| # | Binary | Result | Log |
|---|--------|--------|-----|
| 1 | `velox_ab_benchmark_schema_test` | **GREEN** 26/26 | `baseline_schema_test.log` |
| 2 | `velox_dwio_common_test --gtest_filter='DirectBufferedInputTest.*'` | **GREEN** 13/13 | `baseline_direct_test.log` |
| 3 | `velox_hive_filecache_buffered_input_test` | **GREEN** 6/6 | `baseline_hive_fcbi_test.log` |
| 4 | `velox_ch_filecache_buffered_input_test` | **RED** 34 pass / 4 fail / 1 hang | `baseline_ch_fcbi_test.log` (hang, killed at 180s → exit 124), `baseline_ch_excl.log` (4 fail), `baseline_ch_single.log` (hang, exit 124) |

All logs are under `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/`.

### `ch` baseline failures (deterministic, reproduced)

Run excluding only the hanging test
(`--gtest_filter='-FileCacheBufferedInputTest.CancellationDuringSegmentWaitThrows'`)
→ 34 passed, 4 failed:

1. `FileCacheBufferedInputTest.DiskFailurePropagatesWithoutSkip`
   - `FileCacheBufferedInputTest.cpp:924`: expected `stream->Next(...)` to throw; **it threw nothing**.
   - `:931`: segment state `DOWNLOADED(0)` vs expected `PARTIALLY_DOWNLOADED_NO_CONTINUATION(3)`.
2. `FileCacheBufferedInputTest.DiskFailureSkipContinuesAcrossSegments`
   - `:970`: `writeCount >= 3` actual `0 vs 3` (injected failing write never attempted).
   - `:986`: `totalCached < data.size()` actual `40 vs 40`.
3. `FileCacheBufferedInputTest.CancellationDeferredUntilAfterSegmentWriteCompletes`
   - cancellation hook never fires; assertions on deferred-cancellation state fail.
4. `FileCacheBufferedInputTest.CacheRenameOpenRaceRetries` — **20004 ms** (hit its own 20s internal timeout because the injected race never happens).

### `ch` baseline hang (indefinite, reproduced twice)

`FileCacheBufferedInputTest.CancellationDuringSegmentWaitThrows` **never
terminates** (killed by `timeout 60`/`timeout 180` → exit `124`, twice). It
parks on an **un-timed** `waiterAtWait.wait()`
(`FileCacheBufferedInputTest.cpp:1735`) whose baton is only posted from the
`beforeSegmentWait` `ScopedTestValue` hook — a no-op in this build, so the wait
never returns.

## Diagnosis of the blocker (build-configuration fact, not a study root cause)

- `velox/common/testutil/TestValue.h:86-105`: `TestValue::set(...)` and
  `TestValue::adjust(...)` are compiled to **empty no-ops when `NDEBUG` is
  defined**.
- This `RelWithDebInfo` build defines `-DNDEBUG` (see CMakeCache above), so every
  `SCOPED_TESTVALUE_SET` / `ScopedTestValue` injection point is inert.
- The 5 RED tests each depend on such injection: write-failure injection
  (`DiskFailure*`), the `beforeSegmentWait` hook (`CancellationDuringSegmentWaitThrows`),
  the `afterDownloaderElected` hook (`CancellationDeferredUntilAfterSegmentWriteCompletes`),
  and a forced cache-rename race (`CacheRenameOpenRaceRetries`).
- Task 014's result doc (`port/task/result/014-filecache-buffered-input-result.md`)
  validated these tests as green in a **`_build/debug`** build (`24/24 passed`),
  i.e. without `-DNDEBUG`. They were never green in a `RelWithDebInfo` build.

This is a technical description of *why the STEP 0 baseline gate is RED*. It is
**not** a claim about the FileCache/BufferedInput performance decomposition,
which remains unmeasured and unclaimed.

## Why this blocks the whole task (not just STEP 0)

- STEP 0 explicitly: "If any baseline is RED, stop." → triggered.
- Task-018S Step 6.2 (brief `task-018s-6-brief.md:53-54`) requires "all
  registered tests in each binary pass; no skip/disabled result satisfies the
  gate," for `velox_ch_filecache_buffered_input_test`. In this `-DNDEBUG` build
  those 5 injection-dependent tests can never pass, so the task's own completion
  gate is unsatisfiable in the specified build directory.
- Changing the build type to a non-`NDEBUG` (Debug/testing) configuration is out
  of scope: the study (Tasks 5–7) explicitly requires the `RelWithDebInfo`/Release
  `velox_tpch_benchmark`, and the task forbids reconfiguring the build.

## What was NOT done (fail-close)

- No source, build, or git changes of any kind (tree clean).
- No Task 1–7 implementation started (STEP 0 gate blocked entry).
- No performance matrix run; no root cause claimed.

## Options to unblock (need user decision)

1. Provide/point to a non-`NDEBUG` (Debug or `-UNDEBUG` / testing-enabled) build
   directory for the `velox_ch_filecache_buffered_input_test` gate, keeping the
   `RelWithDebInfo`/Release `velox_tpch_benchmark` for the perf matrix; or
2. Approve treating the 5 `TestValue`-injection tests
   (`DiskFailurePropagatesWithoutSkip`, `DiskFailureSkipContinuesAcrossSegments`,
   `CancellationDeferredUntilAfterSegmentWriteCompletes`, `CacheRenameOpenRaceRetries`,
   `CancellationDuringSegmentWaitThrows`) as a **known pre-existing RED baseline**
   in `RelWithDebInfo`, and adjust the STEP 0 / Step 6.2 gate to
   "no *new* failures vs. this documented baseline" so implementation can proceed.

## Raw artifact paths (all preserved)

- `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_baseline.log`
- `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/baseline_schema_test.log`
- `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/baseline_direct_test.log`
- `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/baseline_hive_fcbi_test.log`
- `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/baseline_ch_fcbi_test.log`
- `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/baseline_ch_excl.log`
- `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/baseline_ch_single.log`

## Test counts

- GREEN baselines: 26 + 13 + 6 = 45 tests passed.
- RED baseline: 34 passed, 4 failed, 1 hung (of 39 registered `ch` tests).

## Controller unblock response 1

```text
controller_status: blocker_resolved
environment_profile: root-oss
task: 018S
redispatch_same_task: yes
```

## Resolution

```text
root cause:
  RelWithDebInfo defines NDEBUG, which compiles TestValue::set/adjust to no-ops.
  The five RED/hanging tests require those injection hooks.

verified working pattern:
  The same clean Velox HEAD builds and passes the complete
  velox_ch_filecache_buffered_input_test in /root/oss/velox/_build/debug.

evidence:
  /root/oss/velox/_build/debug/build_018s_debug_gate_probe.log
  /root/oss/velox/_build/debug/test_018s_debug_gate_probe.log
  build: exit 0
  tests: 39/39 passed, including all five injection-dependent cases

decision:
  Use Debug for the full CH FCBI behavior gate.
  Use RelWithDebInfo for all performance binaries/runs and the 34 release-safe
  CH FCBI cases.

task update:
  Global constraints and Task 6 now define the dual-build gate explicitly.

four_driver_authorized:
  false
```
