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

## Controller interruption response 2

```text
controller_status: worker_replaced
environment_profile: root-oss
task: 018S
four_driver_authorized: false
```

## Interruption state

```text
reason:
  The post-crash Worker repeatedly violated the user-mandated serialized-target
  build rule and then stopped responding to handoff/stop instructions.

active_processes:
  none

last_file_activity:
  analyzer: 2026-07-26 03:12:21 UTC
  runner:   2026-07-26 03:08:35 UTC

implementation_state:
  Velox HEAD remains 0c5b5918eb with an unstaged Task-018S diff.
  Tasks 1-4 implementation is present.
  Task-5 analyzer and runner plus tests are present.

accepted local evidence:
  schema/runtime stats: 35/35
  Hive passthrough: 10/10
  RelWithDebInfo CH FCBI release-safe: 34/34
  Debug CH FCBI full behavior: 39/39
  analyzer unit tests before smoke fixes: 24/24
  runner tests: 10/10

smoke:
  q04 A/B/C commands completed with result_match=1.
  C warm row has hit_pct=100 and no predownload/eviction.
  Analyzer rejected the result because operation counts were zero and smoke
  sample path lookup was wrong.
  Fixes exist in the current dirty diff but have not been rebuilt/retested.

next_worker:
  Inspect the complete dirty diff.
  Verify analyzer smoke/path and operation-count fixes with tests.
  Build exactly one Velox target per invocation with one unique log.
  Rerun q04 smoke, then probe validation, review, and one-driver matrix.
  Stop at waiting_for_four_driver_approval.

root_cause_claimed:
  false
```

## Controller interruption response 3

```text
controller_status: execution_paused
environment_profile: root-oss
task: 018S
four_driver_authorized: false
```

## Concurrent-run contamination

```text
event:
  The superseded post-crash Worker resumed an old q04 smoke command while the
  replacement Worker was running the one-driver probe-validation matrix.

overlap:
  q04 passthrough smoke overlapped q17 A/off and q17 C/off probe samples.

controller_action:
  Terminated both exact process trees.
  Marked the current probe-validation run invalid.
  Instructed both Workers to stop.

preserved_uncontaminated_evidence:
  q09 probe samples 8/8 completed before the overlap.
  q20 probe samples 8/8 completed before the overlap.

invalid_evidence:
  q17 and all later samples in
  /root/oss/velox/tmp/tpch_buffered_input_probe_1driver_worker2
  cannot satisfy the no-concurrent-cells gate.

resume_condition:
  Do not launch another Worker or benchmark until both background Workers are
  idle/completed and no TPCH/build/test process remains.

root_cause_claimed:
  false
```

## Worker attempt 2

```text
worker_status: blocked
environment_profile: root-oss
task: 018S
phase: one_driver_probe_validation
one_driver_validity: not_reached
four_driver_executed: false
root_cause_claimed: false
blocked_at: Task 6 Step 4 / one-driver probe validation
blocker_class: concurrent_run_contamination
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `0c5b5918eb8374f39b09248be091da94bf4d72f0` | dirty Task-018S diff across 22 modified C++ files plus untracked benchmark runner/analyzer/tests |
| `/root/oss/gluten` | `fix/vcpkg-arrow-squashed` | `c44409a7c3d8fab17ac5369b7cad8b3c80f5a437` | clean |
| `/root/oss/clickhouse` | `ch-filecache` | `dedc90e3ee1bc34cdb619252e4937240704ffaee` | canonical receipt already dirty from Controller edits only |

## Files changed

```text
velox/benchmarks/AbBenchmarkBase.cpp
velox/benchmarks/AbBenchmarkBase.h
velox/benchmarks/AbBenchmarkMain.cpp
velox/benchmarks/AbBenchmarkMain.h
velox/benchmarks/QueryBenchmarkBase.cpp
velox/benchmarks/QueryBenchmarkBase.h
velox/benchmarks/scripts/analyze_tpch_buffered_input_matrix.py
velox/benchmarks/scripts/run_tpch_buffered_input_matrix.sh
velox/benchmarks/scripts/tests/test_analyze_tpch_buffered_input_matrix.py
velox/benchmarks/scripts/tests/test_tpch_buffered_input_matrix.sh
velox/benchmarks/tests/AbBenchmarkSchemaTest.cpp
velox/ch/Common/FileCacheStats.h
velox/ch/Disks/IO/FileCacheBufferedInput.cpp
velox/ch/Disks/IO/FileCacheBufferedInput.h
velox/ch/Disks/IO/FileCacheInputStream.cpp
velox/ch/Disks/IO/FileCacheInputStream.h
velox/common/io/IoStatistics.cpp
velox/common/io/IoStatistics.h
velox/connectors/hive/FileDataSource.cpp
velox/connectors/hive/FileDataSource.h
velox/connectors/hive/HiveConnectorUtil.cpp
velox/connectors/hive/HiveConnectorUtil.h
velox/connectors/hive/tests/HiveFileCacheBufferedInputTest.cpp
velox/dwio/common/DirectBufferedInput.cpp
velox/dwio/common/DirectInputStream.cpp
velox/dwio/common/tests/DirectBufferedInputTest.cpp
/root/oss/clickhouse/.superpowers/sdd/task-018s-one-driver-report.md
/root/oss/clickhouse/port/task/result/018s-filecache-tpch-buffered-input-isolation-result.md
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| analyzer RED after new smoke regression test | 1 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_analyzer_red_20260726T0400.log` |
| analyzer GREEN | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_analyzer_green_20260726T0401.log` |
| preserved smoke re-analysis | 1 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_smoke_reanalyze_20260726T0402.log` |
| runner syntax | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_runner_syntax_20260726T0402.log` |
| runner GREEN | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_runner_green_20260726T0402.log` |
| build `velox_ab_benchmark_schema_test` | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_schema_20260726T0405.log` |
| build `velox_dwio_common_test` | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_direct_20260726T0408.log` |
| build `velox_hive_filecache_buffered_input_test` | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_hive_fcbi_20260726T0411.log` |
| build RelWithDebInfo `velox_ch_filecache_buffered_input_test` | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_ch_fcbi_rel_20260726T0411.log` |
| build Debug `velox_ch_filecache_buffered_input_test` | 0 | `/root/oss/velox/_build/debug/build_018s_ch_fcbi_debug_20260726T0414.log` |
| build `velox_tpch_benchmark` | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_tpch_20260726T0417.log` |
| schema/runtime stats test | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_schema_20260726T0420.log` |
| direct buffered-input focused tests | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_direct_20260726T0420.log` |
| Hive passthrough tests | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_hive_fcbi_20260726T0420.log` |
| RelWithDebInfo CH FCBI release-safe tests | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_ch_fcbi_rel_20260726T0422.log` |
| Debug CH FCBI full tests | 0 | `/root/oss/velox/_build/debug/test_018s_ch_fcbi_debug_20260726T0422.log` |
| q04 smoke | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_q04_smoke_worker2_20260726T0424.log` |
| one-driver probe validation | 1 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_probe_1driver_worker2_20260726T0430.log` |
| terminated q17/C sample | 1 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_probe_1driver_worker2/drivers_1/q17/C/forward/sample_1.log` |

## Acceptance evidence

```text
test count:
  analyzer: expected RED 1/25, then GREEN 25/25
  runner shell tests: 10/10
  schema/runtime stats: 35/35
  direct buffered-input focused tests: 15 passed; 2 disabled pre-existing tests reported by gtest
  Hive passthrough: 10/10
  RelWithDebInfo CH FCBI release-safe: 34/34
  Debug CH FCBI full behavior: 39/39
  q04 smoke: valid analyzer result

failed tests:
  none before probe validation

skipped/disabled tests:
  `velox_dwio_common_test --gtest_filter='DirectBufferedInputTest.*'` reports 2 disabled pre-existing tests; no newly added Task-018S test was skipped

benchmark result, when required:
  q04 smoke artifacts:
    /root/oss/velox/tmp/tpch_buffered_input_smoke_worker2/
  smoke deltas (round 2):
    A wall_ms=67418.268 storage_read_ops=962 storage_read_bytes=5625167056 enqueue_count=720 next_count=7200 returned_bytes=5625132188 max_chunk_bytes=1048576 passthrough_read_bytes=0
    B wall_ms=66915.675 storage_read_ops=6000 storage_read_bytes=5625132188 enqueue_count=720 next_count=6000 returned_bytes=5625132188 max_chunk_bytes=1048576 passthrough_read_bytes=5625132188
    C wall_ms=66751.545 hit_pct=100.0000 predownload_mib=0.0000 evict_mib=0.0000 local_read_ops=6000 local_read_bytes=6042932077 enqueue_count=720 next_count=6000 returned_bytes=5625132188 max_chunk_bytes=1048576
  one-driver probe validation uncontaminated partial evidence preserved for q09 and q20 under:
    /root/oss/velox/tmp/tpch_buffered_input_probe_1driver_worker2/
  invalid probe evidence:
    per Controller interruption response 3, a superseded Worker resumed an old q04 smoke run that overlapped q17 probe samples
    the Controller terminated both exact process trees
    q17/C/forward/sample_1 then stopped on SIGTERM before `result.csv` or `meta.json` was written

git diff --check:
  clean
```

## Worker review

```text
review subagent:
  not run
findings:
  none
resolutions:
  n/a
unresolved findings:
  Task 018S blocked before the required read-only diff review because probe validation produced an invalid sample set
```

## Blockers

```text
First actionable error:
  The one-driver probe-validation run became invalid because a superseded Worker
  concurrently resumed an old q04 smoke command while this Worker was running
  q17 probe samples. The Controller terminated both exact process trees, so the
  q17/C/forward/sample_1 benchmark stopped on SIGTERM before producing sample
  artifacts and the analyzer never ran.

Evidence:
  controller record:
    this receipt, Controller interruption response 3
  top-level log:
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_probe_1driver_worker2_20260726T0430.log
  terminated sample log:
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_probe_1driver_worker2/drivers_1/q17/C/forward/sample_1.log
  partial artifact root:
    /root/oss/velox/tmp/tpch_buffered_input_probe_1driver_worker2/

Observed in the sample log after the Controller kill:
  signal: SIGTERM
  receiver pid: 112459
  sender reported by glog: pid 113528 uid 0
```

## Worker declaration

```text
Only Task 018S was attempted.
Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Blocked handoff update

```text
worker_status: blocked
reason:
  Controller found a second legacy Worker concurrently running q04 smoke
  during this Worker's q17 probe-validation run.
  Treat the entire current probe-validation run as INVALID due to
  host/process contamination. Do not analyze it and do not continue it.

preserved_uncontaminated_evidence:
  q09 probe samples: 8/8 completed before contamination
  q20 probe samples: 8/8 completed before contamination

invalid_evidence:
  q17 A/C probe samples are contaminated by the overlapping q04 smoke run
  all later samples in
    /root/oss/velox/tmp/tpch_buffered_input_probe_1driver_worker2
  are invalid for the no-concurrent-cells gate

four_driver_executed:
  false
```

## Controller trace/replay functional checkpoint

```text
controller_status: q04_trace_replay_functional_accepted
environment_profile: root-oss
task: 018S
worker_status: waiting_for_trace_replay_timing_approval
q04_capture_validity: passed
q04_replay_validity: passed
trace_replay_timing_authorized: false
five_query_trace_replay_executed: false
one_driver_matrix_executed: false
four_driver_executed: false
root_cause_claimed: false
task_017b_status: paused
```

### Checkpoint evidence

```text
capture:
  watchdog state: completed
  child exit: 0
  elapsed: 137.892 seconds
  result_match: 1
  error: empty
  files: 240
  events: 10320
  capture tids/thread indices: 11/11

functional replay:
  watchdog state: completed
  child exit: 0
  elapsed: 28.275 seconds
  logical bytes A/B/C: 5625132188
  B passthrough bytes: 5625132188
  C warm hits/misses: 6000/0
  C warm cache/source bytes: 6042932077/0
  C warm hit percent: 100
  predownload/eviction gates: passed
  byte oracle: passed
  cache cleanup: passed

review:
  reviewer: Claude Opus 4.8
  verdict: APPROVE
  Critical: 0
  Important: 0
  unresolved: 0

git diff --check:
  clean
```

This checkpoint proves only that q04 logical `BufferedInput` capture and
single-thread A/B/C replay work with correct bytes and path identity. It does not
provide a performance result or authorize the remaining focused queries,
timing protocol, one-driver matrix, four-driver matrix, mutation, or Task 017B.

## Controller q04 single-thread timing checkpoint

```text
controller_status: q04_single_thread_timing_accepted
environment_profile: root-oss
task: 018S
worker_status: waiting_for_single_thread_timing_review
q04_single_thread_timing_authorized: true
q04_single_thread_timing_validity: passed
five_query_timing_authorized: false
five_query_timing_executed: false
concurrent_replay_executed: false
one_driver_matrix_executed: false
four_driver_executed: false
performance_root_cause_claimed: false
task_017b_status: paused
```

### Timing evidence

```text
mode:
  single-thread deterministic replay
samples:
  forward A,B,C x3
  reverse C,B,A x2
watchdog:
  completed, child exit 0, 54.549 seconds, no signals

pooled medians:
  A Direct:             2112.091 ms
  B FCBI passthrough:    890.226 ms
  C warm FileCache:      849.991 ms

decomposition:
  B-A: -1221.865 ms, (B-A)/A=-0.578509
  C-B:   -40.236 ms, (C-B)/B=-0.045197
  C-A: -1262.100 ms, (C-A)/A=-0.597560

validity:
  15/15 samples present
  logical bytes identical
  A/B/C path gates passed
  C warm hits/misses=6000/0
  byte verification passed before timing
  timed oracle disabled
  forward/reverse B-A sign agreed
  forward/reverse C-B sign agreed
  cache cleanup passed
  no residual process

review:
  timing implementation APPROVE
  Critical=0
  Important=0
```

This checkpoint is not a query-runtime result. It measures only q04's logical
access pattern replayed sequentially on one thread. It cannot attribute
multi-thread contention or establish a performance root cause. The user must
review this checkpoint before any other query is captured or timed.

## Controller cold FileCache timing decision

```text
controller_status: q04_passthrough_timing_superseded
environment_profile: root-oss
task: 018S
previous_passthrough_timing:
  functional validity: passed
  performance interpretation: superseded_by_user_cold_filecache_decision
  use in cold study: prohibited
cold_filecache_timing_review:
  reviewer: Claude Opus 4.8
  verdict: APPROVE FOR WATCHDOG PILOT
  Critical: 0
  Important: 0
five_query_timing_executed: false
concurrent_replay_executed: false
four_driver_executed: false
performance_root_cause_claimed: false
task_017b_status: paused
```

The accepted passthrough artifact remains only a replay-harness diagnostic.
The replacement q04 study uses A Direct / B fresh cold FileCache / C persistent
warm FileCache, with source Parquet files intentionally warm in the OS page
cache. No result from the passthrough pilot may be incorporated into that cold
FileCache decomposition.

## Controller q04 cold FileCache timing attempt 1

```text
controller_status: blocked_waiting_for_cold_gate_revision_approval
environment_profile: root-oss
task: 018S
q04_cold_filecache_timing_validity: failed_before_timed_samples
watchdog:
  state: fatal_log
  watchdog exit: 86
  child exit: -6
  elapsed: 33.349 seconds
failure:
  phase: untimed verified cold B
  predownloaded_from_source_bytes: 1449630844
  invalid gate: cold B predownload counters must equal zero
result_json_written: false
automatic_retry_executed: false
cache_cleanup: passed
residual_processes: none
five_query_timing_executed: false
concurrent_replay_executed: false
four_driver_executed: false
performance_root_cause_claimed: false
task_017b_status: paused
```

`FileCacheInputStream::predownloadForCurrentSegment` synchronously fills cache
gaps when the cold q04 access trace resumes beyond a partially populated
segment's current write offset. That foreground work is part of the requested
cold FileCache population cost; it is not a background download
(`backgroundDownloadThreads=0`). The harness incorrectly rejected the normal
cold path before any timed sample. Warm C must still have zero predownload
counters.

Evidence:

```text
report:
  /root/oss/clickhouse/.superpowers/sdd/task-018s-q04-cold-filecache-timing-report.md
log:
  /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_trace_q04_cold_timing_1/timing_q04_cold.log
watchdog status:
  /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_trace_q04_cold_timing_1/timing_q04_cold.watchdog.json
```

No retry is authorized by this checkpoint.

## Controller q04 cold FileCache timing final checkpoint

```text
controller_status: q04_cold_filecache_execution_complete_performance_inconclusive
environment_profile: root-oss
task: 018S
worker_status: waiting_for_cold_filecache_protocol_review
q04_cold_filecache_execution_validity: passed
q04_cold_filecache_performance_validity: inconclusive_nonstationary
evidence_status: passed_with_documented_derivation
attempts:
  _1: failed before timed samples on superseded zero-predownload gate
  _2: completed, child exit 0, 15/15 samples
third_attempt_executed: false
five_query_execution: false
concurrent_replay_executed: false
four_driver_executed: false
performance_root_cause_claimed: false
task_017b_status: paused
```

### Attempt 2 evidence

```text
watchdog:
  state: completed
  child exit: 0
  elapsed: 197.557 seconds
  signals: none
result:
  SHA-256: 865c99171e222e978ef05bb856a3a919428c31e24eb17307346ecac27622d0b9
  exact order: forward A/B/C x3, reverse C/B/A x2
  samples per cell: 5
  logical bytes per sample: 5625132188
  distinct B roots: 5
  B predownload counters: 1449630844/1449630844
  B eviction bytes/segments: 0/0
  C hits/misses: 6000/0
  C source/predownload/eviction: 0
cache cleanup: passed
residual processes: none
```

Pooled medians:

```text
A Direct:            2207.225 ms
B cold FileCache:   29093.116 ms
C warm FileCache:     959.456 ms

B-A: +26885.891 ms, (B-A)/A=+12.180857
C-B: -28133.661 ms, (C-B)/B=-0.967021
C-A:  -1247.769 ms, (C-A)/A=-0.565311
```

Forward/reverse B-A signs are both positive. Forward/reverse C-B signs are
both negative. This is a one-thread logical replay result with source Parquet
files warm in the OS page cache; it does not reproduce query concurrency or
establish a performance root cause.

The cold-B samples are nonstationary: `11303.844`, `29093.116`, `26923.497`,
`35250.999`, and `29369.541` milliseconds. Every B sample created about
6.2 GiB of new cache data, but the run did not record host dirty/writeback
state. The large first-to-later drift is consistent with writeback throttling
or cache-file metadata accumulation. The pooled 29.093-second value is retained
only as a raw observation and is not accepted as a stable performance result.

### Evidence schema caveat

The result rows omit `cache_write_bytes` and `oracle_read_bytes`. The Controller
did not claim these were directly recomputed from absent fields:

- B cache writes are proven positive by within-pass cache reads on five distinct
  initially-empty roots plus positive foreground predownload.
- C cache writes are zero because source reads, misses, and predownload are all
  zero.
- timed oracle bytes are zero by the hash-pinned replay code path:
  `verifyBytes=false` for both timed option sets, and oracle accumulation is
  conditional on `verifyBytes`.

The replay's internal gates checked these fields before output. This closes the
current result with a documented derivation, but the omission is an Important
follow-up: future artifacts must serialize both fields before being described
as artifact-self-contained.

Full report:

```text
/root/oss/clickhouse/.superpowers/sdd/task-018s-q04-cold-filecache-timing-report.md
```

No `_3`, remaining-query timing, concurrent replay, four-driver execution,
performance root-cause claim, or Task 017B work followed.

## Controller commit checkpoint

```text
Velox commit:
  9c4b995747
  Add `BufferedInput` trace replay tooling

committed trace metadata:
  velox/dwio/common/benchmarks/traces/task_018s_q04/manifest.json
  velox/dwio/common/benchmarks/traces/task_018s_q04/SHA256SUMS
  velox/dwio/common/benchmarks/traces/task_018s_q04/TRACE_PROVENANCE.md

full events artifact:
  tracked by git: false
  reason: 2041347 bytes exceeds the repository 1 MiB artifact limit
  external path:
    /root/oss/velox/tmp/tpch_buffered_input_trace_q04_watchdog_1/capture_trace/events.jsonl
  SHA-256:
    5616daf42854ff05f12bf3bcdbd880a3fbc03a0408e8fbf91b68ccb912cee65a

push executed: false
pull request created: false
```
