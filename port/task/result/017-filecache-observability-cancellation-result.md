# Task 017 Result: Observability and Cancellation Hardening

## Worker attempt 1

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 017
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `8958b20426a3d864494dbdd2479873d3a32b7807` | clean |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | clean |

Note: the Velox branch is `filecache2` and ClickHouse is `ch-filecache`, matching the
dispatch note (baseline HEAD `8958b2042`, all prior commits accepted). Task 016 was
DEFERRED (no Velox consumer) and is not a real dependency of 017; not blocked on it.

## Status

status: success

## Velox status

```text
branch: filecache2 (ahead 11 of baibaichen/filecache)
HEAD:   8958b20426a3d864494dbdd2479873d3a32b7807
git status --short: only task-owned files modified/added (see Files changed);
                    git diff --check reports no whitespace errors.
```

## Initial shim audit (pre-change, `git show HEAD:...`)

```text
grep -c "noreturn\|inline.*{}" on committed HEAD 8958b2042:
  QueryStatus.h:      0   (stub throwIfKilled() const {} spans one line but not matched by regex; was a no-op)
  CurrentMetrics.h:   2   (inline add/sub no-op bodies)
  ProfileEvents.h:    1   (inline increment no-op body)
  logger_useful.h:    0   (no-op bodies span multiple lines; getCurrentExceptionMessage returned {})
```

All four shims were confirmed no-op stubs before the change (verified by reading each file).

## Files changed

```text
Modified (declared scope + F-CALLERID/SD8 + outdated first-phase test assertions):
  velox/ch/Common/logger_useful.h
  velox/ch/Common/CurrentMetrics.h
  velox/ch/Common/ProfileEvents.h
  velox/ch/Common/QueryStatus.h
  velox/ch/Common/CMakeLists.txt
  velox/ch/Common/tests/CMakeLists.txt
  velox/ch/Common/tests/BasicShimsTest.cpp          (updated superseded first-phase no-op assertions)
  velox/ch/Common/tests/SchedulerAndScopeTest.cpp   (F-CALLERID exact-format + SD8 E-probe)
  velox/ch/Common/FileCacheQueryIdScope.cpp         (F-CALLERID: None:<threadname>:<tid>)
  velox/ch/Common/FileCacheQueryIdScope.h           (doc update)
  velox/ch/Disks/IO/FileCacheInputStream.cpp        (cancellation safe points)
  velox/ch/Disks/IO/FileCacheInputStream.h          (QueryStatus ctor param + member)
  velox/ch/Disks/IO/tests/CMakeLists.txt
  velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp  (NoScopeBackgroundId -> exact format)

Created:
  velox/ch/Common/CurrentMetrics.cpp                (atomic gauge storage)
  velox/ch/Common/ProfileEvents.cpp                 (atomic event storage)
  velox/ch/Common/tests/ObservabilityTest.cpp
  velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp

Result receipt (ClickHouse checkout):
  port/task/result/017-filecache-observability-cancellation-result.md
```

## Scope note: cancellation wiring vs. the task's ConnectorQueryCtx path

The task Step 7 prescribes forwarding `ConnectorQueryCtx::cancellationToken()` through
`FileCacheBufferedInput` (adding a `connectorQueryCtx_` member). The actual accepted
implementation has NO `ConnectorQueryCtx` anywhere in `velox/ch/`: `FileCacheBufferedInput`
receives only a `FileCacheRequestContext` (plain `queryId`/`userId` strings), never a
`ConnectorQueryCtx*` or a cancellation token. Threading a `ConnectorQueryCtx*` would change
the `FileCacheBufferedInput` constructor signature and every caller (E2E/manager/builder),
which is OUTSIDE the declared file scope and would trigger the unreviewed-dependency gate.

The MVP-faithful resolution kept within scope: `FileCacheInputStream` gains the optional
`QueryStatus queryStatus = {}` constructor parameter (exactly as the task's `.h` snippet
shows, "defaulting to no-op"), stores it, and calls `throwIfKilled()` at the 4 safe points.
`FileCacheBufferedInput::enqueue`/`read` construct the stream with the default (never-cancel)
`QueryStatus{}` because no token source exists. Tests construct `FileCacheInputStream`
directly with a `folly::CancellationSource` token to prove the throw points and the
downloader-lease-not-held invariant. Connecting a real token source is a follow-up once a
`ConnectorQueryCtx` is threaded into the builder.

## SD8 resolution chosen + justification

Resolution **option 1: retain `std::recursive_mutex` with an E-probe** proving the inline
Future-continuation re-entry.

Justification: `FileCacheScheduledTask::armTimerLocked` runs under `mutex_` and attaches a
continuation via `timekeeper_->after(delayMs).toUnsafeFuture().thenValue(...)`. When the
antecedent promise is already fulfilled at attach time, folly runs the continuation INLINE on
the attaching thread — which already holds `mutex_`. The continuation re-locks `mutex_` (to
call `queueImmediateLocked`), so a non-recursive mutex would self-deadlock. `ManualTimekeeper::
after(0)` fulfils its promise immediately (verified in the folly source: `if (dur.count()==0)
promise.setValue(...)`), giving a deterministic trigger. The E-probe
`FileCacheSchedulerTest.ZeroDelayInlineContinuationDoesNotSelfDeadlock` calls
`scheduleAfter(0)` and asserts it returns and the callback runs within 5s; with a non-recursive
mutex this call would deadlock and the test would hang (RED for option 2). Retention is the
lower-risk choice: moving the continuation off-lock (option 2) would require restructuring the
timer-arm/cancel/generation logic that Tasks 006/011-015 already depend on.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| cmake configure | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_017.log` |
| build new + common/scheduler tests | 0 | `.../build_017_observability.log` |
| ctest new + common/scheduler | 0 | `.../test_017_observability.log` |
| RED-probe build (QueryStatus neutered) | 0 | `.../build_017_redprobe.log` |
| RED-probe ctest (expect FAIL) | 8 | `.../test_017_redprobe.log` |
| build regression targets | 0 | `.../build_017_regression.log` |
| rebuild core_scc after F-CALLERID test fix | 0 | `.../build_017_scc_rebuild.log` |
| ctest regression (e2e/buffered/manager/scc) | 0 | `.../test_017_regression.log` |

## Generated logs

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_017.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_017_observability.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_017_observability.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_017_redprobe.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_017_redprobe.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_017_regression.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_017_scc_rebuild.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_017_regression.log
```

## Test results

```text
New gates:
  velox_ch_observability_test: 14 tests, 0 failed, 0 skipped
  velox_ch_cancellation_test:   5 tests, 0 failed, 0 skipped

Regression gates:
  velox_ch_common_test:                33 tests, 0 failed, 0 skipped
  velox_ch_scheduler_test:             21 tests, 0 failed, 0 skipped (SD8 E-probe here)
  velox_ch_filecache_core_scc_test:    47 tests, 0 failed, 0 skipped (F-CALLERID test here)
  velox_ch_filecache_manager_test:     19 tests, 0 failed, 0 skipped
  velox_ch_filecache_buffered_input_test: 19 tests, 0 failed, 0 skipped
  velox_ch_filecache_e2e_test:         17 tests, 0 failed, 0 skipped

getCurrentExceptionMessage: std ("std sentinel") and Velox ("velox sentinel") text preserved,
                            empty outside a catch.
```

## Acceptance evidence / RED proofs

```text
test count (new): observability 14, cancellation 5
failed tests: 0
skipped/disabled tests: 0  (rg GTEST_SKIP|DISABLED_ on both new test files: none)

RED — metrics/cancellation (whole-behavior probe):
  Neutered QueryStatus::throwIfKilled() and isCancelled() to no-op/false, rebuilt, reran:
    velox_ch_observability_test + velox_ch_cancellation_test -> 0% passed, 2/2 FAILED
    (test_017_redprobe.log). Restored real QueryStatus.h -> both 100% pass again.
  This proves the cancellation is actually checked at the throw points and the tests are
  not false-green. The cancellation tests additionally assert, via a no-create get() snapshot
  (expectNoHeldDownloader), that no segment is left DOWNLOADING and isDownloader()==false after
  the exception — i.e. the downloader lease is not held across the throwIfKilled() check.

RED — metrics counters:
  ObservabilityTest.CurrentMetricsTest.AddIsNotNoOp asserts ASSERT_NE(before, after) and an
  exact +7 delta; a no-op add() (pre-change body) leaves get() unchanged -> fails.
  TimeIncrementRecordsElapsed asserts delta >= 4000us after a 5ms sleep; a no-op timer -> 0.

RED — F-CALLERID exact format:
  SchedulerAndScopeTest.FileCacheQueryIdScopeTest.NoScopeProducesExactNoneFormat and
  FileSegmentTest.CallerIdTest.NoScopeBackgroundId both require three colon-separated fields
  (None:<threadname>:<tid>) and include a false-green guard EXPECT_NE(id, "None:"+tid) that
  fails for the OLD two-field None:<tid> form. Confirmed by the fact that before updating the
  implementation these tests would assert the old two-field format; the pre-change
  NoScopeBackgroundId asserting "None:"+tid failed against the new three-field output during
  the first regression run (test_017_regression.log first pass) and passes after the fix.

RED — SD8 scheduler re-entry:
  FileCacheSchedulerTest.ZeroDelayInlineContinuationDoesNotSelfDeadlock exercises the inline
  Future-continuation re-entry (scheduleAfter(0) -> ManualTimekeeper::after(0) immediately
  ready -> .thenValue runs inline under mutex_ -> continuation re-locks mutex_). With a
  non-recursive mutex this deadlocks and the test hangs; it passes with the retained
  std::recursive_mutex.

git diff --check: no whitespace errors.
git status: only task-owned files (the 4 Common shims + their .cpp/CMake/tests,
  FileCacheInputStream.{cpp,h} + its test, FileCacheQueryIdScope.{cpp,h} + SchedulerAndScope
  test for F-CALLERID/SD8, and the two outdated first-phase test files BasicShimsTest.cpp /
  FileSegmentTest.cpp). No production code outside scope changed.
```

## Worker review

```text
review subagent: general-purpose read-only reviewer over the full task-owned diff.
findings:
  Verdict CLEAN — no CRITICAL or SHOULD-FIX findings.
  #1 metrics/events: real atomic storage, read-back via get(), real elapsed us; all enum
     names preserved (compile-guard test names every one). PASS.
  #2 cancellation: exactly the 4 safe points, none between getOrSetDownloader/
     completePartAndResetDownloader, reserve/write, inside writeCache, or predownload after
     reserve. PASS.
  #3 QueryStatus: default never cancels; cancelled token throws VeloxRuntimeError. PASS.
  #4 logger: non-empty exceptionStr for std+Velox, empty outside catch; tryLogCurrentException
     noexcept and swallows logging failure; LOG_TRACE/DEBUG/INFO lazy; LOG_TEST fully no-op;
     LOG_WARNING/ERROR compile-safe. PASS.
  #5 F-CALLERID: exact None:<threadname>:<tid>, qid path unchanged, false-green guard present.
     PASS.
  #6 SD8: recursive_mutex retention justified; E-probe genuinely proves inline re-entry. PASS.
  #7 public API: only the defaulted QueryStatus ctor param added; no other signature change.
     PASS.
  #8 false-green: none; counter/cancel/format tests all assert real deltas/throws/format.
  NITs (non-actionable): ProfileEventTimeIncrement<Microseconds> specialization is byte-
     identical to the primary template (kept deliberately for CH call-shape symmetry, per the
     task snippet); tryLogCurrentException outside a catch would log an empty tail (only
     reachable by contract violation).
resolutions:
  No actionable findings. NITs left as-is: the <Microseconds> specialization is retained on
  purpose (task's ProfileEvents.h snippet shows both the primary template and the
  specialization); the empty-tail path is unreachable in correct usage.
unresolved findings: none.
```

## Blockers

```text
None.
```

## Recommended next task

```text
No further post-MVP tasks are queued. Follow-ups intentionally NOT done here (documented as
task exclusions or scope gaps):
  - Threading a real ConnectorQueryCtx / cancellation-token source into FileCacheBufferedInput
    so enqueue()/read() forward a live token (requires a builder/constructor signature change
    outside this task's file scope; the QueryStatus plumbing and safe check points are ready).
  - Structured-log output / FilesystemCacheLog entries; Prometheus metrics bridge; per-query
    ProfileEvents aggregation; cancellation inside the reserve() wait loop — all deferred per
    the task's "Explicit exclusions".
Background download, free-space keeper, dynamic resize reload, and Prometheus metrics belong
to "阶段 8：补充能力" in port/01-filecache-port-order-design.md and are scheduled by product
priority.
```

## Worker declaration

```text
Only Task 017 was attempted.
Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Controller review 1 (accepted)

```text
controller_status: accepted
environment_profile: home-chang
task: 017
```

## Review evidence

```text
scope: PASS. Changes confined to the 4 Common shims (+ new CurrentMetrics.cpp/
  ProfileEvents.cpp) + their CMake/tests, QueryStatus.h, logger_useful.h,
  FileCacheQueryIdScope.{h,cpp} (F-CALLERID), FileCacheInputStream.{cpp,h} (cancel
  points) + new FileCacheCancellationTest, SchedulerAndScopeTest (SD8 E-probe),
  and FileSegmentTest.cpp's CallerIdTest.NoScopeBackgroundId. The FileSegmentTest
  edit is a REQUIRED connected change: F-CALLERID changed the caller-id format
  None:<tid> -> None:<threadname>:<tid>, so the old EXPECT_EQ(id,"None:"+tid)
  necessarily fails; updated to exact-3-field + old-format regression guard.
  Legitimate, not scope creep. git diff --check clean.
implementation: shims made real behind the SAME public APIs (atomic metric/event
  counters readable back; ProfileEventTimeIncrement records elapsed us; QueryStatus
  wraps folly::CancellationToken; logger real VLOG + current-exception format).
  Cancellation checked at 4 safe points in FileCacheInputStream with the downloader
  lease released before throwIfKilled().
cancellation RED — INDEPENDENTLY VERIFIED: I neutered QueryStatus::throwIfKilled()
  to a no-op, rebuilt, and velox_ch_cancellation_test went RED (3 FAILED incl.
  NextThrowsWhenCancelledAfterFirstSegment and CancellationDoesNotLeakDownloaderLease);
  restored byte-identical (no probe remnant) -> 5/5 green. Load-bearing, not false-green.
SD8: resolved by RETAINING std::recursive_mutex + an E-probe (scheduler_test) proving
  the inline Future-continuation re-entry (scheduleAfter(0) -> queueImmediateLocked);
  a non-recursive mutex self-deadlocks (RED for option 2). Lower-risk; accepted.
F-CALLERID: exact None:<threadname>:<tid> format with EXPECT_NE false-green guard.
regression — re-ran directly: observability 14 / cancellation 5 / scheduler 21 /
  core_scc 47, all 0 failed/0 skipped; receipt also shows common 33 / manager 19 /
  buffered_input 19 / e2e 17 green. Making the shims real broke no accepted test.
cancellation-vs-ConnectorQueryCtx note (receipt): 017 injects QueryStatus directly
  rather than threading ConnectorQueryCtx (which is the same connector-integration
  seam that parked TPCH). Accepted: the cancellation contract (throw at safe points,
  no lease held) is fully met via QueryStatus; wiring the real ConnectorQueryCtx
  source belongs to the connector-integration task (018 / a dedicated task), same as
  the TPCH filecache engine. Not a gap in 017's deliverable.
unresolved findings: none.
```

## Required changes

```text
None. Task 017 accepted.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | (this acceptance — see Velox `Task 017:` commit) |
| `/home/chang/SourceCode/ClickHouse` | receipt+handoff = this commit |
