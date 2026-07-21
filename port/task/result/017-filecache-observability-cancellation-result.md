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

---

## Worker attempt (Post-acceptance amendment 1 — 填命中三件套 + 记算子级 IoStatistics)

```text
worker_status: blocked
environment_profile: home-chang
task: 017 (post-acceptance amendment 1)
```

### Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `18a81d77d` (`Task 018a: Validate install with hasDefault, not throwing getDefault`) | clean |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | clean (this receipt only) |

Branch is `filecache2` (ahead 15 of `baibaichen/filecache`), matching the dispatch note
(HEAD contains 018a + `hasDefault`). Those committed files were left untouched.

### Files changed

```text
None. No Velox source was modified. The amendment is blocked before any code change (see Blockers).
Only this receipt section was appended in the ClickHouse checkout.
```

### Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| `git status --short --branch` / `git log -1` (velox baseline) | 0 | (inline, no build attempted) |
| `git diff --check` (velox, post-analysis) | 0 | (clean — no edits made) |

No configure/build/test was run: the blocker is a design/API-contract gap that must be
resolved by the user before any read-path edit can be written. No `task017_hitmetrics_*.log`
was produced because no build/test command was issued.

### Contract analysis (why blocked)

The amendment requires, at each of the three read-path semantic points, recording the same N
bytes in **two** places: (1) global `ProfileEvents::increment(...)` and (2) an operator-level
`io::IoStatistics` using a **distinctly-named "专属字段"** (design §3: do NOT reuse native
`ramHit`/`ramReadBytes`; do NOT invent or misuse a native field). The amendment itself directs:
"若 `IoStatistics` 无合适的自定义计数入口，停为 blocked 记录，不臆造。"

Findings from the authoritative sources:

1. Global side is reachable in scope. `velox/ch/Common/ProfileEvents.h` already defines the
   three enums `CachedReadBufferReadFromCacheBytes` / `ReadFromSourceBytes` / `CacheWriteBytes`
   with real atomic storage and an `increment` entry point (Task 017 attempt 1). Full-repo
   search confirms **0** increment call sites today. The `ReadType` decision in
   `FileCacheInputStream.cpp` already distinguishes reading a cache segment (`ReadType::CACHED`)
   vs reading the source (`REMOTE_FS_READ_*`), and `writeCache`/`readFromCurrentSegment` are the
   natural cache-write points — so the global three-tuple could be filled without new branch
   logic. CH authority口径 confirmed: `src/Common/ProfileEvents.cpp:850` (ReadFromSourceBytes),
   `:852` (ReadFromCacheBytes), `:854` (CacheWriteBytes).

2. Operator-level side has **no in-scope entry point**. `velox/common/io/IoStatistics.h`
   (velox trunk) exposes only:
   - fixed native byte/latency counters — `rawBytesRead_`, `rawBytesWritten_`, `ramHit_`,
     `read_`, `ssdRead_`, `prefetch_`, `*LatencyUs_`, `readGap_` — every one carrying a native
     semantic the design §3 decision explicitly forbids repurposing (borrowing `ramReadBytes`
     would mislabel our disk-cache hit as "ram read bytes" in the Spark UI);
   - `incOperationCounters(operation, ...)` + `operationStats_` (a `map<string,
     OperationCounters>` keyed by a name string) — but `OperationCounters` has **no bytes
     field** (only `resourceThrottleCount`/`localThrottleCount`/…/`retryCount`/`latencyInMs`/
     `requestCount`/`delayInjectedInSecs`). It cannot record hit/source/write **bytes**.

   There is therefore **no generic named-byte custom-counter entry point** in `io::IoStatistics`
   for a distinctly-named `fileCacheReadFromCacheBytes` / `…SourceBytes` / `…WriteBytes`. The
   only way to add one is to edit `velox/common/io/IoStatistics.{h,cpp}` (add a dedicated field
   or a named byte map) — which is **velox trunk**, and the amendment mandates "velox 主干零改动
   (git diff 仅在 velox/ch/)" and "不碰 … velox 主干".

3. Corroboration from the benchmark: `velox/ch/benchmarks/CacheReadHarness.cpp:586`
   `FcbiHarness::fillTiers` sets `r.tiers.ramBytes = 0` and reads only native
   `ioStats.ssdRead().sum()` / `ioStats.read().sum()` — i.e. there is currently **no** filecache
   hit-byte attribution field to read back, and `FileCacheWrapperBenchmark.cpp:251` explicitly
   comments that "the IoStatistics load path did not attribute" those bytes. `FileCacheInputStream`
   today holds no `IoStatistics` member at all (verified — the amendment's premise is correct).

This is the exact `blocked` condition the amendment pre-authorized: a required operator-level
custom counter has no reviewed API to land in, and the two escape hatches (reuse a native field
/ edit velox trunk) are both explicitly forbidden.

### Decision needed from the user

Pick the operator-level "专属字段" landing before this amendment can proceed (global
ProfileEvents side is ready to fill in scope regardless):

- **Option A — extend `io::IoStatistics` in velox trunk** with dedicated named byte counters
  (e.g. three new `IoCounter`/`atomic_uint64_t` fields `fileCacheReadFromCacheBytes_` /
  `…SourceBytes_` / `…WriteBytes_` with `inc*`/getter accessors, or a generic named-byte map).
  This is the design §3 "专属字段" intent but requires a velox-trunk diff (widens scope to
  `velox/common/io/IoStatistics.{h,cpp}`), contradicting "velox 主干零改动".
- **Option B — introduce a filecache-owned stats sink under `velox/ch/`** (a small
  `FileCacheIoStats` type, threaded through `FileCacheBufferedInput` → `FileCacheInputStream`
  alongside/instead of `io::IoStatistics`) that carries the three dedicated byte counters.
  Stays inside `velox/ch/`, but is NOT the `io::IoStatistics` the amendment names, and would
  need a separate Gluten `customStats` bridge later (§3 five-layer) — i.e. it changes the
  "thread the existing `ioStatistics` into `FileCacheInputStream`" contract.
- **Option C — split the amendment**: fill only the global ProfileEvents three-tuple now (fully
  in scope, no trunk edit) and defer the operator-level `IoStatistics` field to a task that is
  allowed to touch `velox/common/io/IoStatistics.{h,cpp}` (or that defines the `velox/ch/` sink).

Each option changes either the file scope or the named target of the amendment, so it needs a
user decision recorded in the design/amendment before a worker can implement.

### Acceptance evidence

```text
test count: N/A (blocked before build)
failed tests: N/A
skipped/disabled tests: N/A
benchmark result: N/A
git diff --check: clean (no edits); git status: clean except this receipt
```

### Worker review

```text
review subagent: not launched — no diff to review (blocked before any code change).
findings: N/A
resolutions: N/A
unresolved findings: the operator-level custom-counter API gap above (external decision).
```

### Blockers

```text
`io::IoStatistics` (velox trunk, velox/common/io/IoStatistics.h) has no suitable custom
named-byte-counter entry point for a distinctly-named filecache hit/source/write-bytes metric:
- all fixed byte counters (ramHit_, read_, ssdRead_, rawBytesRead_, ...) carry native semantics
  that design §3 forbids repurposing;
- the only extensible slot, operationStats_ via incOperationCounters(), stores OperationCounters
  which has NO bytes field.
Adding a dedicated counter requires editing velox/common/io/IoStatistics.{h,cpp} (velox trunk),
which the amendment forbids ("velox 主干零改动"; "不碰 velox 主干"). Per the amendment's own
instruction ("若 IoStatistics 无合适的自定义计数入口，停为 blocked 记录，不臆造") and the
EXECUTION_PROTOCOL unreviewed-dependency gate, work is stopped. User must choose Option A / B / C
(or another approach) and record it in the design + amendment before redispatch.
```

### Worker declaration

```text
Only Task 017 post-acceptance amendment 1 was attempted.
No Velox source was modified; the Velox worktree is clean (git diff --check clean).
The prior accepted Task 017 receipt content above was not erased or altered.
The worker stopped after writing this receipt.
```

---

## Worker attempt 2 (Post-acceptance amendment 1 — 填命中三件套，仅全局 ProfileEvents)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 017 (post-acceptance amendment 1, narrowed to global ProfileEvents only — user decision C)
```

### Scope note

Attempt 1 was correctly blocked; the Controller narrowed scope via user decision C. This
attempt implements ONLY the three global `ProfileEvents` byte increments in the read path. The
operator-level `io::IoStatistics` half was SPLIT OUT (deferred to a Gluten-phase backlog task per
the amendment "拆分说明"). No `IoStatistics`, `FileCacheBufferedInput`, `ProfileEvents.h`, Gluten,
or velox-trunk file was touched.

### Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` (ahead 15 of `baibaichen/filecache`) | `18a81d77d` (`Task 018a: Validate install with hasDefault, not throwing getDefault`) | clean |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | clean (this receipt only) |

The committed 018a / `hasDefault` files were left untouched.

### Files changed

```text
Modified (Velox, all under velox/ch/Disks/IO/):
  velox/ch/Disks/IO/FileCacheInputStream.cpp      (three ProfileEvents increments in the read path)
  velox/ch/Disks/IO/tests/CMakeLists.txt          (register velox_ch_filecache_hit_metrics_test)

Created (Velox):
  velox/ch/Disks/IO/tests/FileCacheHitMetricsTest.cpp  (RED + acceptance tests, 5 cases)

Result receipt (ClickHouse checkout): this section only.
```

### Implementation (contract fill)

Three currently-empty (0 increment sites) global `ProfileEvents` filled in the read path, using
the existing `ReadType` decision — no new branch logic:

| 语义点 | 记录点 | 全局 ProfileEvents |
|---|---|---|
| 从缓存段读到 N 字节（命中） | `readFromCurrentSegment`, after the read, `servedFromCache = (readType==CACHED)`, over trimmed `size` | `increment(CachedReadBufferReadFromCacheBytes, N)` |
| 从源读到 N 字节（回源/未命中） | same point, `!servedFromCache` (REMOTE_FS_READ_*), over trimmed `size` | `increment(CachedReadBufferReadFromSourceBytes, N)` |
| 下载写入缓存 N 字节 | inside `writeCache` (covers BOTH the main-read download AND predownload callers) | `increment(CachedReadBufferCacheWriteBytes, N)` |
| 预下载从源读到 N 字节 | `predownloadForCurrentSegment`, per read chunk `got` | `increment(CachedReadBufferReadFromSourceBytes, N)` |

Design fidelity to CH authority (`src/Disks/IO/CachedOnDiskReadBufferFromFile.cpp`):
- hit/source split mirrors `nextImplStep` (`:1592` ReadFromCacheBytes, `:1598` ReadFromSourceBytes);
- `CacheWriteBytes` lives INSIDE `writeCache` (CH `:1298`) so it fires for both the main download
  and the predownload write;
- predownload source bytes mirror CH `:1108` (`ReadFromSourceBytes`); CH's predownload-specific
  counters (`CachedReadBufferPredownloadedFromSourceBytes` / `PredownloadedBytes`) have no port
  enum, so only the source-bytes total is recorded.
- The hit/source classification (`servedFromCache`) is captured BEFORE `state.readType` can be
  reassigned to `REMOTE_FS_READ_BYPASS_CACHE` on a cache-write failure, so the attribution reflects
  where the bytes were actually served from. Write increment uses the untrimmed download chunk
  (bytes written to the segment); hit/source increment uses the trimmed `size` served to the caller.
- Enum names only added as increment call sites; `ProfileEvents.h` NOT edited.

### Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| cmake configure | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task017_hitmetrics_v2_configure.log` |
| build hit_metrics test (initial) | 0 | `.../task017_hitmetrics_v2_build.log` |
| ctest hit_metrics (initial, 4 pass) | 0 | `.../task017_hitmetrics_v2_test.log` |
| RED probe build (hit increment neutralized) | 0 | `.../task017_hitmetrics_v2_redbuild.log` |
| RED probe ctest (expect FAIL) | 8 | `.../task017_hitmetrics_v2_redtest.log` |
| build all gates (pre review-fix) | 0 | `.../task017_hitmetrics_v2_regbuild.log` |
| build both benchmarks | 0 | `.../task017_hitmetrics_v2_benchbuild.log` |
| ctest all gates (pre review-fix, 8/8) | 0 | `.../task017_hitmetrics_v2_regtest.log` |
| build all gates + benchmarks (after predownload fix) | 0 | `.../task017_hitmetrics_v2_regbuild2.log` |
| ctest all gates (after fix, 8/8) | 0 | `.../task017_hitmetrics_v2_regtest2.log` |
| RED probe v2 build (current tree) | 0 | `.../task017_hitmetrics_v2_redbuild2.log` |
| RED probe v2 ctest (expect FAIL) | 8 | `.../task017_hitmetrics_v2_redtest2.log` |
| final restore build | 0 | `.../task017_hitmetrics_v2_finalbuild.log` |
| final restore ctest (green) | 0 | `.../task017_hitmetrics_v2_finaltest.log` |

### Test results

```text
New gate:
  velox_ch_filecache_hit_metrics_test: 5 tests, 0 failed, 0 skipped
    ColdMissCountsSourceAndWriteBytes  — cold miss: source+=N, write+=N, cache+=0
    HitCountsCacheBytes                — warm hit: cache+=N, source+=0, write+=0
    BypassCountsSourceOnly             — bypass: source+=N, write+=0, cache+=0
    HitRatioComputableFromCounters     — hit/(hit+source)==1.0 for a full re-read
    PredownloadCountsSourceAndWriteBytes — predownload prefix counted in source AND write

Regression gates (all 0 failed / 0 skipped):
  velox_ch_observability_test:            14
  velox_ch_filecache_e2e_test:            17
  velox_ch_filecache_buffered_input_test: 19
  velox_ch_filecache_manager_test:        20
  velox_ch_filecache_core_scc_test:       47
  velox_ch_cancellation_test:              5
  velox_ch_filecache_connector_test:       4

Benchmarks: velox_ch_filecache_seek_benchmark + velox_ch_filecache_wrapper_benchmark both build.
```

### Acceptance evidence / RED proof

```text
test count (new): hit_metrics 5
failed tests: 0
skipped/disabled tests: 0 (rg GTEST_SKIP|DISABLED_ on FileCacheHitMetricsTest.cpp: none)

RED (hit increment neutralized, current tree):
  Replaced `increment(CachedReadBufferReadFromCacheBytes, size)` with a no-op `(void)size`,
  rebuilt, reran -> HitCountsCacheBytes (EXPECT_EQ cacheBytes delta == n) and
  HitRatioComputableFromCounters (ratio==1.0) both FAILED; 0% passed (task017_hitmetrics_v2_redtest2.log).
  Restored the real increment -> 5/5 green (task017_hitmetrics_v2_finaltest.log).
  Proves the tests actually drive the record point (non-empty assertions).

Assertions use ProfileEvents::get() before/after DIFF (cumulative process-wide counters).

git diff --check: no whitespace errors.
git status --short: only 3 files, all under velox/ch/Disks/IO/. ZERO velox-trunk diff.
```

### Worker review

```text
review subagent: general-purpose read-only reviewer over the diff (FileCacheInputStream.cpp +
  CMakeLists.txt + FileCacheHitMetricsTest.cpp).
findings:
  Main-read attribution correct: servedFromCache captured before the readType reassignment;
    hit/source over trimmed size; write over untrimmed download size; relaxed-atomic increments
    have no concurrency/lifetime issue. PASS.
  SHOULD-FIX (fidelity): predownloadForCurrentSegment reads from source and writes cache in its
    own loop but originally attributed NEITHER counter, diverging from CH which increments
    CacheWriteBytes inside writeCache (:1298, covers both callers) and ReadFromSourceBytes for
    predownloaded chunks (:1108). Under-counts source+write on a non-sequential first read.
  FALSE-GREEN gap: all initial tests read {0,n} sequentially, never exercising predownload, so
    the suite would stay green with predownload attribution missing.
resolutions:
  Verified the finding against CH authority (grep of CachedOnDiskReadBufferFromFile.cpp confirmed
  :1108 ReadFromSourceBytes for predownload, :1298 CacheWriteBytes inside writeCache, :1592/:1598
  main-read split). Applied CH-faithful fix:
    - moved the CacheWriteBytes increment INTO writeCache (fires for both main-read and predownload);
    - added ReadFromSourceBytes for each predownloaded `got` chunk in predownloadForCurrentSegment.
  Added PredownloadCountsSourceAndWriteBytes test (cold read starting 16 KiB into a 64 KiB segment
  forces predownload of [0,16KiB)); asserts source and write deltas exceed the `len` bytes served,
  closing the false-green gap. Rebuilt all gates + benchmarks -> 8/8 gates green, both benchmarks
  build. Re-ran the RED proof on the fixed tree -> still load-bearing.
unresolved findings: none.
```

### Blockers

```text
None.
```

### Worker declaration

```text
Only Task 017 post-acceptance amendment 1 (narrowed to the global ProfileEvents three-tuple) was
attempted. Changes are confined to velox/ch/Disks/IO/ (FileCacheInputStream.cpp, tests/CMakeLists.txt,
new tests/FileCacheHitMetricsTest.cpp); ZERO velox-trunk diff. Changes are unstaged and uncommitted.
The prior accepted Task 017 receipt and the attempt-1 (blocked) section above were not erased or altered.
The worker stopped after writing this receipt.
```

## Controller review — amendment 1 attempt 2（填命中三件套，仅全局 ProfileEvents）

```text
controller_status: accepted
environment_profile: home-chang
task: 017 (amendment 1, attempt 2)
```

## Review evidence

```text
scope review: git diff 仅 velox/ch/ —— FileCacheInputStream.cpp、tests/CMakeLists.txt、
  新增 tests/FileCacheHitMetricsTest.cpp。trunk 零改动（git diff --name-only 无 velox/ch/
  以外文件）。IoStatistics/FileCacheBufferedInput 未碰（算子级那半已拆到 backlog #29）。
implementation review: 三个 increment 归属正确、对齐 CH：
  - 主读路径 servedFromCache = (state.readType == ReadType::CACHED)（:732，直接读既有
    ReadType，无新造判断）→ hit 记 ReadFromCacheBytes(:778)、source 记 ReadFromSourceBytes
    (:781)，记的是 trim 后真正给 caller 的 size。
  - predownload source 字节 ReadFromSourceBytes(:588)。
  - writeCache 内 CacheWriteBytes(:533)。
  - 只记原始累计量，不报率（率由消费端相除）。CH 权威口径 ProfileEvents.cpp:850/852/854。
  worker 自审发现并修了一个 predownload 字节未归属的保真缺口（对齐 CH
  CachedOnDiskReadBufferFromFile.cpp:1108/1298/1592/1598），补了 predownload 测试。
log and test review: Controller 独立复现 RED —— 把 hit increment 的 size 中和为 0，重建
  （exit 0），跑 hit_metrics：HitCountsCacheBytes(:239) 与 HitRatioComputableFromCounters
  (:298) FAILED（"hit+source > 0" 得 0）。还原；grep 确认无残留 probe；重建；hit_metrics
  5/5 绿。回归（Controller 亲跑）：observability 14 / e2e 17 / buffered_input 19 /
  core_scc 47 全绿（0 failed / 0 skipped）；两 benchmark 构建通过。
unresolved findings: none.
```

## Required changes

```text
None.
```

## Commits (amendment 1)

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `f76379397` |
| `/home/chang/SourceCode/ClickHouse` | (this amendment's ClickHouse commit — 含 port/design 观测设计文档) |
