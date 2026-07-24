# Review 4 Closure Ledger — Tasks 003–015

```text
ledger_round:           5 (Review 5 Task 2)
frozen_ch_head:         03f88d4e3e672eea95f90e348c1c533958d63e0d
frozen_velox_head:      7c52b47ecbe6799df37966bd07b03899e0612d66
review_4_velox_head:    43a9e6f75ffb94be38836b45fd476325665f50be
review_4_ch_head:       92707bea40423b8ddf53102e8f9eba8a2fa27658
method:                 independent source/test/log inspection — no production code modified
authority:              current Velox production source at frozen head (behavior oracle)
```

This file is the row-by-row Review-4 debt closure required by Review 5 Task 2.
Every unresolved Review-4 item is enumerated: all 8 UNPROVEN denominator rows,
all 11 user decisions, all Critical/Important findings, all corrective task
obligations, and all evidence-debt/forward-gate items. Each receives exactly
one disposition.

Source documents consumed:

- `port/task/fullreview/root-oss/4/003-015-ch-parity-audit.md`
- `port/task/fullreview/root-oss/4/003-015-parity-decisions-needed.md`
- `port/task/fullreview/root-oss/4/003-015-parity-user-decisions.md`
- `port/task/fullreview/root-oss/4/evidence/003-015-velox-parity-matrix.md`
- `port/task/result/017a-filecache-statistics-cancellation-result.md`
- `port/task/fullreview/root-oss/5/evidence/003-018-baselines.md`

---

## 1. Reference baseline delta

Velox commits between Review-4 audit head and Review-5 frozen head:

```text
43a9e6f75  Task 015: Sync direct-IO adapter tests (Review-4 head)
e68690f    Task 017A: Implement FileCache metrics storage
74165cd    Task 017A: Wire FileCache reader statistics
72d485c    Task 017A: Propagate FileCache cancellation
4a2b0f8    Task 017A: Align FileCache scheduler locking
4bfe970    Task 017A: Update caller-id acceptance coverage
a856d83    Task 017A: Document caller-id format
[9 Task 018 commits]  benchmarks and TPCH (no FileCache contract changes)
7c52b47ecb  Fix TPCH q15 parallel revenue selection (frozen head)
```

Files changed in `velox/ch/` between the two heads (from `git diff --stat`):
- `Common/`: CurrentMetrics.cpp, ProfileEvents.cpp, FileCacheStats.cpp,
  FileCacheScheduler.cpp/h, FileCacheQueryIdScope.cpp/h (017A only)
- `Disks/IO/`: FileCacheInputStream.cpp/h, FileCacheBufferedInput.cpp/h,
  tests/FileCacheBufferedInputTest.cpp (017A + 018 benchmarks)
- `Interpreters/FileCache/FileSegment.cpp`, `tests/FileSegmentTest.cpp` (017A)
- `benchmarks/` — new FileCacheBufferedInputBenchmark.cpp, updated CMakeLists (018)

No changes to `FileCache.cpp` `initialize()`, `createCacheReadBuffer()`,
`FileCacheManager.cpp` transaction semantics, `OpenedFileCache.h`, or
`FileCacheSettings.*` between the two heads.

---

## 2. Review-4 UNPROVEN row closure (8 rows)

All 8 rows remain in the denominator.  Two are `reopen_task` (approved fix
absent); six are `waiting_for_user` (governing decision still pending).

### 2.1 `L-CALLONCE-01` — UNPROVEN → **EQUIVALENT** (closed, Task 012 corrective)

```text
task_owner:    012 / 003 (primitive registration)
old_status:    UNPROVEN (at frozen head 7c52b47ecb)
new_status:    EQUIVALENT
disposition:   closed
closed_at:     26325e8a32 (Task 012 corrective, Review 5)
blocking_for_parity: resolved
```

**User decision (Review-4):** `R2-D2 = modify` — replace the hand-rolled
mutex+flag with `folly::call_once` / `folly::once_flag`.

**Implementation at `26325e8a32`:**

`FileCache.h`: `std::mutex initialize_mutex` and `bool initialize_completed`
removed; `folly::once_flag initialize_once_flag` added (includes
`folly/synchronization/CallOnce.h`). `FileCache.cpp:initialize()` body wrapped
in `folly::call_once(initialize_once_flag, [this]{ … })`.

**Rationale for EQUIVALENT (not MATCH):** `folly::once_flag` /
`folly::call_once` is the Velox-idiomatic mapping of CH's `std::once_flag` /
`std::call_once` with identical observable semantics: body executes exactly
once; concurrent callers block until done; a throwing body leaves the flag
incomplete and a later call retries; successful completion is
sequentially-consistently published. The primitive is Folly-namespaced rather
than std-namespaced, consistent with the R2-D2 mapping approved in
`003-015-parity-user-decisions.md`.

**Controller evidence at `26325e8a32`:**

```text
focused mono   3/3 (InitializeOnce, SecondProcessStatusLockFails,
                    ConcurrentInitializersSucceedOnce)
focused non-mono 3/3

mutations (all RED):
  local mutex/flag restored — structural RED (structural pin: once_flag field absent)
  std::once_flag substituted — structural RED (folly::basic_once_flag type mismatch)
  retry semantics removed    — runtime RED (retry assertions fail)
```

Log paths under `/root/oss/velox/`:
`_build/debug/{build,test}_review5_task012_controller_final.log`
`_build/debug-task017a-nonmono/{build,test}_review5_task012_controller_final.log`

---

### 2.2 `D-INIT-01` — UNPROVEN → **waiting_for_user** (D4 pending)

```text
task_owner:    013
old_status:    UNPROVEN
new_status:    UNPROVEN (unchanged)
disposition:   waiting_for_user
governing_decisions: R2-D4 (pending)
```

`R2-D2` (once-guard) was resolved at `26325e8a32` (Task 012 corrective) and
is no longer a blocking factor. `D-INIT-01` now remains UNPROVEN solely because
`R2-D4` (Manager mutation serialization / transactional reload) has no user
decision.

**Primary block:** R2-D4 still `pending` — user has not approved, rejected, or
modified the Manager mutation-serialization/transactional-reload design.

---

### 2.3 `E-GETORCREATE-01` — UNPROVEN → **waiting_for_user** (D4 pending)

```text
task_owner:    013
old_status:    UNPROVEN
new_status:    UNPROVEN (unchanged)
disposition:   waiting_for_user
governing_decision: R2-D4 (pending)
```

`FileCacheManager.cpp:264-302` has the `mutation_mutex_` serialization and
`getOrCreateLocked` with `rollbackNewBindingsLocked` at line 415. The
implementation is present, but R2-D4 user approval has not been given.

---

### 2.4 `E-CREATE-01` — UNPROVEN → **waiting_for_user** (D4 pending)

```text
task_owner:    013
old_status:    UNPROVEN
new_status:    UNPROVEN (unchanged)
disposition:   waiting_for_user
governing_decision: R2-D4 (pending)
```

Same as 2.3: `createLocked` wrapped in `mutation_mutex_` at frozen head; user
approval for D4 not given.

---

### 2.5 `E-UPDCFG-01` — UNPROVEN → **waiting_for_user** (D4 pending)

```text
task_owner:    013
old_status:    UNPROVEN
new_status:    UNPROVEN (unchanged)
disposition:   waiting_for_user
governing_decision: R2-D4 (pending)
```

`FileCacheManager.cpp:350-415` transactional `applyConfigs` implementation is
present; user approval for D4 not given.

---

### 2.6 `P-RB-SETDETACH-01` — UNPROVEN → **waiting_for_user** (D6 pending)

```text
task_owner:    007/014
old_status:    UNPROVEN
new_status:    UNPROVEN (unchanged)
disposition:   waiting_for_user
governing_decision: R2-D6 (pending)
```

`velox/ch/Disks/IO/FileCacheInputStream.cpp:944` has `state_->reader->set(nullptr, 0)`
and line 974 has `state_->reader->releaseOwnedBuffer()`. The D6 detach
implementation is present but user approval has not been given.

---

### 2.7 `G-NEXTIMPL-01` — UNPROVEN → **waiting_for_user** (D6 pending)

```text
task_owner:    014
old_status:    UNPROVEN
new_status:    UNPROVEN (unchanged)
disposition:   waiting_for_user
governing_decision: R2-D6 (pending)
```

`readNextChunk` orchestration and reader-handoff safety at frozen head is the
D6 implementation; user approval not given.

---

### 2.8 `G-CACHEBUF-01` — UNPROVEN → **EQUIVALENT**

```text
task_owner:    014
old_status:    UNPROVEN
new_status:    EQUIVALENT
disposition:   closed
closed_at:     cda6c03703 (Task 014 corrective, Review 5)
blocking_for_parity: resolved
```

**User decision (Review-4):** `G-CACHEBUF-01 = approve_fix` — restore CH
external-truncation self-heal (physical file size < recorded downloaded size →
warn + bypass + re-fetch).

**Gap verification at frozen head `7c52b47ecb`:**

`velox/ch/Interpreters/FileCache/FileCache.cpp:395-400`:
```cpp
FileSegment::RemoteFileReaderPtr FileCache::createCacheReadBuffer(const std::string & path) const
{
    auto read_file = open_read_file(path);
    return std::make_shared<ReadBufferFromVeloxReadFile>(std::move(read_file), memory_pool);
}
```

No size-mismatch check exists. The stale comment at
`velox/ch/Interpreters/FileCache/FileSegment.cpp:1237-1238` still claims
"`getCacheReadBuffer` compares the on-disk size against the recorded one and
discards the broken entry (re-fetching from the source)" — but the code at
`FileCache.cpp:395-400` performs no such comparison.

The gap was source-confirmed absent at the frozen head.

**Accepted corrective at `cda6c03703`:**

```text
FileCacheInputStream:
  size-suffixed DOWNLOADED/DETACHED files compare physical and recorded sizes;
  a shorter file returns no cache reader and switches to source bypass;
  cache metadata/priority state is preserved;
  a concurrent <offset> -> <offset>_<size> rename retries the changed path once,
  only for kFileNotFound and after recomputing under the segment lock.

Evidence:
  physical truncation RED and mutation: short pread 4096 vs 8192;
  rename/open RED and mutation: FILE_NOT_FOUND on old <offset>;
  Controller final mono selected 2/2, accumulated 16/16, non-mono 2/2;
  independent final review: APPROVE, no Blocker/Major findings.
```

This is EQUIVALENT rather than MATCH because the Velox port uses
`ReadBufferFromVeloxReadFile::tryGetFileSize`, `kFileNotFound`, and a
`TestValue` seam instead of ClickHouse's file-descriptor/Poco infrastructure.

---

## 3. User decision closure (11 decisions)

### 3.1 `R2-D1` — ShardedMap.h public-header registration

```text
decision_id:   R2-D1
user_decision: none recorded
disposition:   waiting_for_user
affects_unproven_rows: no
```

`velox/ch/Interpreters/FileCache/CMakeLists.txt:51` registers `ShardedMap.h`
in the public `FILE_SET HEADERS` — Option A is already implemented.  No user
decision (approve/reject) was given in `003-015-parity-user-decisions.md`. This
is a build-packaging-only decision with no effect on any denominator UNPROVEN
row; it does not block parity closure.

---

### 3.2 `R2-D2` — call_once → folly::call_once replacement

```text
decision_id:   R2-D2
user_decision: modify (use folly::call_once / folly::once_flag)
implementation_at_26325e8a32: PRESENT (Task 012 corrective)
disposition:   closed
```

Decision was given in `003-015-parity-user-decisions.md`. Implementation
delivered at `26325e8a32`. See row 2.1 above for full evidence.

---

### 3.3 `R2-D3` — production velox_test_util link

```text
decision_id:   R2-D3
user_decision: none recorded
disposition:   waiting_for_user
affects_unproven_rows: no
```

`velox/ch/Interpreters/FileCache/CMakeLists.txt:99` keeps `velox_test_util`
as a `PUBLIC` link dependency on the production `velox_ch_filecache_core`
library (Option A). `L-FP-EVICTSEG-01` and `L-FP-EVICTPUSH-01` are already
INTENTIONAL_DEVIATION (D10 approved). No UNPROVEN denominator row depends on
this decision.

---

### 3.4 `R2-D4` — Manager mutation serialization and transactional reload

```text
decision_id:   R2-D4
user_decision: pending (explicitly deferred in decisions file)
disposition:   waiting_for_user
affects_unproven_rows: D-INIT-01, E-GETORCREATE-01, E-CREATE-01, E-UPDCFG-01
blocks_review_5: no (user decision 2026-07-24)
```

`003-015-parity-user-decisions.md` records: `| R2-D4 | pending | Do not
approve, reject, modify, or reclassify…`. Implementation is present
(`mutation_mutex_` in `FileCacheManager.h:181`, `getOrCreateLocked`,
`rollbackNewBindingsLocked` in `FileCacheManager.cpp`). Approval is absent.
Four denominator rows (2.2–2.5) remain UNPROVEN.

**User reaffirmation (Review 5, 2026-07-24):** R2-D4 remains `pending`; its
four governed rows remain UNPROVEN as non-blocking forward debt.

---

### 3.5 `R2-D5` — OpenedFileCache weak-bucket-state deleter

```text
decision_id:   R2-D5
user_decision: none recorded
disposition:   waiting_for_user
affects_unproven_rows: no
```

`velox/ch/Interpreters/FileCache/OpenedFileCache.h:86-89` documents and
implements the `weak_ptr<BucketState>` deleter (Option A). The ASan-reproduced
UAF fix is present. No UNPROVEN denominator row depends on this decision.

---

### 3.6 `R2-D6` — reader detach and query-pool lifetime

```text
decision_id:   R2-D6
user_decision: pending (explicitly deferred in decisions file)
disposition:   waiting_for_user
affects_unproven_rows: P-RB-SETDETACH-01, G-NEXTIMPL-01
blocks_review_5: no (user decision 2026-07-24)
```

`003-015-parity-user-decisions.md` records: `| R2-D6 | pending | Do not
approve, reject, modify, or reclassify…`. Implementation is present
(`FileCacheInputStream.cpp:944,974`). Approval is absent. Two denominator rows
(2.6–2.7) remain UNPROVEN.

**User reaffirmation (Review 5, 2026-07-24):** R2-D6 remains `pending`; its two
governed rows remain UNPROVEN as non-blocking forward debt.

---

### 3.7 `R2-D7` — direct-I/O predownload and failure hardening

```text
decision_id:   R2-D7
user_decision: none recorded
disposition:   waiting_for_user
affects_unproven_rows: no (G-PREDL-01 MATCH, G-ISCACHED-01 EQUIVALENT, G-WRITECACHE-01 MATCH)
```

All three governed rows are already MATCH or EQUIVALENT at the Review-4
baseline and remain so at the frozen head. Decision is required only for
bookkeeping regularization. No UNPROVEN rows depend on this decision.

---

### 3.8 `R2-D8` — B4 mutation corrected to implemented ordering

```text
decision_id:   R2-D8
user_decision: none recorded
disposition:   waiting_for_user
affects_unproven_rows: no (B-COMPPART-01, B-EXTRDR-01, B-RESETRDR-01 all MATCH)
```

`velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp:1052`
`ConcurrentExtractRacesResetBeforeComplete` exists at both the Review-4 head
and the frozen head (same test). All governed rows are MATCH. Decision affects
test authoring only; no denominator row depends on it.

---

### 3.9 `T015-D5` — benchmark non-destructive fresh keys

```text
decision_id:   T015-D5
user_decision: none recorded
disposition:   waiting_for_user
affects_unproven_rows: no (benchmark is OVER_PORT, outside 215 denominator)
```

`velox/ch/benchmarks/FileCacheSeekBenchmark.cpp:74,108,142,176` uses
`FileCacheKey::random()` for fresh keys (Option A implemented). No user
decision recorded. No denominator row depends on this.

---

### 3.10 `G-CACHEBUF-01` — restore CH external-truncation self-heal

```text
decision_id:   G-CACHEBUF-01
user_decision: approve_fix
implementation_at_frozen_head: ABSENT
implementation_at_cda6c03703: PRESENT
disposition:   closed
```

See row 2.8 above for full evidence. The approved fix is implemented and the
row/decision are closed as one obligation.

---

### 3.11 `SD4-EVIDENCE` — no-reference-across-mutation proof

```text
decision_id:   SD4-EVIDENCE
user_decision: none recorded
disposition:   waiting_for_user
affects_rows: R-BUCKETMAP-01 (EQUIVALENT, evidence debt)
```

No focused test proving that no iterator, mapped-value reference, or
mapped-value address survives an F14 `MetadataBucket` mutation exists at the
frozen head. `velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp` was
searched — no SD4 probe found. `R-BUCKETMAP-01` remains EQUIVALENT with open
evidence debt. The by-construction `shared_ptr<KeyMetadata>` stability argument
is still the only proof.

---

## 4. Important and Low findings closure

Review-4 §5.3 listed two findings that were INTENTIONAL_DEVIATION with
forward-deferred actions to Task 017: B-WAIT-01 (Important) and B-CALLERID-01
(Low). Task 017A is accepted. Both rows are reclassified at the frozen head.

### 4.1 `B-WAIT-01` — cancellation token wiring

```text
row_id:        B-WAIT-01
task_owner:    012
old_status:    INTENTIONAL_DEVIATION (Important) — no-op token wired
new_status:    EQUIVALENT
disposition:   closed
```

**Review-4 finding:** "structural cancel signature present but read caller wires
no-op token (`folly::CancellationToken{}` at `FileCacheInputStream.cpp:410`);
real KILL wiring Task 017."

**Verification at frozen head `7c52b47ecb`:**

- `velox/ch/Disks/IO/FileCacheInputStream.h:189`: `folly::CancellationToken cancellationToken_`
- `FileCacheInputStream.cpp:83-85`: `cancellationToken_ = owner_->cancellationToken()` (real token copied from owner)
- `FileCacheInputStream.cpp:267`: pre-lookup cancellation check (safe point)
- `FileCacheInputStream.cpp:436`: `downloadState = fileSegment.wait(offset, cancellationToken_)` — real token passed
- `FileCacheInputStream.cpp:850`: post-segment cancellation check
- `FileSegment.cpp:558-566`: 60s deadline, 1s slices, `cancellation_token.isCancellationRequested()` with `VELOX_FAIL("Query was cancelled...")`

Tests:
- `FileCacheBufferedInputTest.cpp:1638` `CancellationBeforeLookupThrows` — token cancelled pre-read throws
- `FileCacheBufferedInputTest.cpp:1655` `CancellationDuringSegmentWaitThrows` — token cancelled inside `FileSegment::wait` throws within 1s slice
- `FileSegmentTest.cpp:934` `WaitObservesCancellationToken` — covers pre-cancelled token check

**Rationale for EQUIVALENT (not MATCH):** Mechanism is `folly::CancellationToken`
(not CH's `throwIfKilled`/query-status check), but observable behavior is
identical: blocks on DOWNLOADING, reraises cancellation within 1s slice, 60s
deadline, non-no-op token required from caller. The deviation is in the
underlying cancellation primitive, not in the consumer-visible contract.

---

### 4.2 `B-CALLERID-01` — caller-id format

```text
row_id:        B-CALLERID-01
task_owner:    006
old_status:    INTENTIONAL_DEVIATION (Low) — None:<tid> vs CH None:<threadname>:<tid>
new_status:    MATCH
disposition:   closed
```

**Review-4 finding:** "`getCallerId()` returns `None:<tid>`, not CH's verbatim
`None:<threadname>:<tid>`; F-CALLERID deferred."

**Verification at frozen head `7c52b47ecb`:**

`velox/ch/Common/FileCacheQueryIdScope.cpp:56-60`:
```cpp
std::string name = folly::getCurrentThreadName().value_or("unknown");
return "None:" + name + ":" + tid;
```

CH format: `None:<threadname>:<tid>` using `getThreadName()` (Linux `prctl(PR_GET_NAME)`).
Velox format: `None:<name>:<tid>` using `folly::getCurrentThreadName()` (also
uses `prctl(PR_GET_NAME)` on Linux). The format is structurally and
semantically identical.

Tests:
- `SchedulerAndScopeTest.cpp:780-787` `CallerIdWithoutScopeHasThreadNameFormat`
  — asserts three-field format `None:<name>:<tid>` and non-empty middle field
- `SchedulerAndScopeTest.cpp:793-808` `NamedThreadAppearsInCallerId`
  — asserts named thread's name (`FcTestWorker`) appears in caller id
- `FileSegmentTest.cpp:155` `NoScopeBackgroundId`
  — asserts `None:<thread-name>:<os-tid>` three-field format with correct TID
  suffix and non-empty thread-name middle field

**Rationale for MATCH:** The returned string now matches the CH format verbatim
(`None:<threadname>:<tid>`) using the same OS primitive.

---

## 5. Forward-planned Task 017 obligations (§9.3) at frozen head

These items were assigned to "Task 017" as forward-deferred in Review-4 §9.3
and are now verifiable against accepted Task 017A.

### 5.1 SD8 — scheduler recursive-mutex resolution

```text
item:          SD8 — FileCacheScheduledTask recursive-mutex
was_deferred_to: Task 017
disposition:   closed (017A implemented)
row_effect:    O-QUERYSCOPE-01 remains EQUIVALENT (internal change, no observable delta)
```

`velox/ch/Common/FileCacheScheduler.cpp` diff `43a9e6f75..7c52b47ecb`:
- `std::recursive_mutex` replaced by `std::mutex scheduleMutex_` + `std::mutex execMutex_`
- `deactivate()` now acquires `execMutex_` first (drain), then `scheduleMutex_`
- Lock-order comment added: "execMutex_ THEN scheduleMutex_"

O-QUERYSCOPE-01 was already EQUIVALENT with note "recursive_mutex SD8 deferred
Task 017 (no observable change)". The SD8 fix does not change the observable
contract; the row stays EQUIVALENT.

---

### 5.2 F-USED-01 / F-METRIC-01 — CurrentMetrics / ProfileEvents storage

```text
items:         F-USED-01, F-METRIC-01 (Forward-Appendix-F, outside 215 denominator)
was_deferred_to: Task 017
disposition:   closed (017A implemented)
```

Task 017A added:
- `velox/ch/Common/CurrentMetrics.cpp` — process-wide out-of-line relaxed atomic gauges
- `velox/ch/Common/ProfileEvents.cpp` — out-of-line relaxed atomic counters (10 new CH reader events appended)
- `velox/ch/Common/FileCacheStats.cpp` — `takeFileCacheStatsSnapshot`, `kFileCacheWriteBytes`

These were Forward-Appendix-F rows, outside the 215-row denominator. They do
not change any denominator classification.

---

### 5.3 `L-FP-EVICTSEG-01` / `L-FP-EVICTPUSH-01` — real failpoint surface

```text
items:         L-FP-EVICTSEG-01, L-FP-EVICTPUSH-01 (INTENTIONAL_DEVIATION, D10)
was_deferred_to: Task 017 (real fiu-based failpoint surface)
disposition:   forward_deferred (still D10-approved INTENTIONAL_DEVIATION; no new seams in 017A)
```

`velox/ch/Common/FailPoint.h` exists at frozen head with the `FAIL_POINT_TRIGGER`
macro. However, the specific named seams `file_cache_simulate_evicting_segment`
and `file_cache_background_eviction_push_fail` are **not present** in any
`velox/ch/` source at the frozen head (confirmed by `git grep`). These rows
remain INTENTIONAL_DEVIATION under D10 approval. Full failpoint surface remains
a future task obligation.

---

## 6. Evidence debt (§11 forward gates)

### 6.1 FWD-ODIRECT-01 — real kernel O_DIRECT

```text
gate:          FWD-ODIRECT-01
disposition:   forward_deferred (Task 017/018 design wave)
status:        unchanged — still UNPROVEN, mandatory pre-rollout gate
```

Strict mock-based DIO tests exist (Task 015). No real kernel `O_DIRECT`
integration test exists at the frozen head. This is outside the 215-row
denominator; the Task-015 D3/D4 conditional remains ungated.

---

### 6.2 Real errno producer

```text
gate:          Real errno producer (ENOSPC/EDQUOT from concrete cache writer)
disposition:   forward_deferred (pre-release gate)
status:        unchanged — B-WRITE-01 consumer side MATCH; producer side UNPROVEN
```

`B-WRITE-01` (consumer: typed `ErrnoException` reconciliation) is MATCH at
frozen head. The concrete cache-file writer that reliably produces structured
`ENOSPC`/`EDQUOT` in production is not yet implemented.

---

### 6.3 StatusFile unclean-restart diagnostics

```text
gate:          StatusFile unclean-restart read-before-truncate diagnostics
disposition:   forward_deferred (pre-release gate)
status:        unchanged — basic StatusFile lifecycle MATCH; unclean-restart path not proven
```

Section M denominator rows (StatusFile ctor/flock/ftruncate/write/dtor/unlink)
are all MATCH. The unclean-restart read-before-truncate diagnostic path is a
separately-scoped pre-release gate not yet implemented.

---

### 6.4 `P-META-REMOTEMETA-01` — remote metadata provider

```text
row_id:        P-META-REMOTEMETA-01
task_owner:    007 / 014
status:        INTENTIONAL_DEVIATION (already counted in denominator)
disposition:   forward_deferred (future object-storage plumbing task)
```

`FileCacheInputStream.cpp:760` returns `std::nullopt` unconditionally from
`getRemoteFileMetadata()`, so truncation-vs-logic-error discrimination is
impaired: both paths fail-closed via `VELOX_FAIL`. This was accepted in
Review-4 under cross-profile/1 and the Task-014 contract ("nullopt = unavailable;
tests must not assume a real metadata source"). The real remote-metadata provider
was forward-deferred to Task 017/018 object-storage plumbing.

Task 017A is accepted; no remote-metadata provider was introduced in 017A.
P-META-REMOTEMETA-01 remains INTENTIONAL_DEVIATION and is now explicitly tracked
as `forward_deferred` in this ledger. This item was raised as a minor finding
during the Task 012 corrective independent review and was not previously listed
in the closure ledger's disposition table.

---

## 7. Parity count update

Only rows that changed from Review-4 are listed. All other rows are unchanged.

| row_id | old_status | new_status | reason |
|---|---|---|---|
| `B-WAIT-01` | INTENTIONAL_DEVIATION | EQUIVALENT | Task 017A wired real `folly::CancellationToken` with tested cancellation |
| `B-CALLERID-01` | INTENTIONAL_DEVIATION | MATCH | Task 017A restored `None:<threadname>:<tid>` format via `folly::getCurrentThreadName()` |
| `L-CALLONCE-01` | UNPROVEN | EQUIVALENT | Task 012 corrective at `26325e8a32`: `folly::once_flag` / `folly::call_once` implemented; 3/3 focused tests in mono and non-mono |
| `G-CACHEBUF-01` | UNPROVEN | EQUIVALENT | Task 014 corrective at `cda6c03703`: physical truncation bypass/refetch plus rename/open retry; Controller 2/2, 16/16, 2/2 |

Updated aggregate counts (denominator = 215):

| status | Review-4 | Review-5 frozen | Task 012 corrective | Task 014 corrective |
|---|---|---|---|---|
| MATCH | 172 | **173** | **173** | **173** |
| EQUIVALENT | 30 | **31** | **32** | **33** |
| INTENTIONAL_DEVIATION | 5 | **3** | **3** | **3** |
| UNPROVEN | 8 | **8** | **7** | **6** |
| MISSING | 0 | 0 | 0 | 0 |
| **total** | **215** | **215** | **215** | **215** |

```text
Review-5 frozen:
  semantic parity   = (173 + 31) / 215 = 204/215 = 94.9%
  accepted coverage = (173 + 31 +  3) / 215 = 207/215 = 96.3%

Review-5 Task 012 corrective:
  semantic parity   = (173 + 32) / 215 = 205/215 = 95.3%
  accepted coverage = (173 + 32 +  3) / 215 = 208/215 = 96.7%

Review-5 Task 014 corrective:
  semantic parity   = (173 + 33) / 215 = 206/215 = 95.8%
  accepted coverage = (173 + 33 +  3) / 215 = 209/215 = 97.2%
```

6 UNPROVEN rows persist. The overall verdict remains **PARITY_BLOCKED** solely
because the governing decisions remain pending (D4 × 4, D6 × 2). No approved
corrective implementation is absent.

---

## 8. Disposition summary

| item | type | disposition | task_owner |
|---|---|---|---|
| `L-CALLONCE-01` | UNPROVEN row | `closed` | Task 012 corrective at `26325e8a32` |
| `D-INIT-01` | UNPROVEN row | `waiting_for_user` | R2-D4 pending |
| `E-GETORCREATE-01` | UNPROVEN row | `waiting_for_user` | R2-D4 pending |
| `E-CREATE-01` | UNPROVEN row | `waiting_for_user` | R2-D4 pending |
| `E-UPDCFG-01` | UNPROVEN row | `waiting_for_user` | R2-D4 pending |
| `P-RB-SETDETACH-01` | UNPROVEN row | `waiting_for_user` | R2-D6 pending |
| `G-NEXTIMPL-01` | UNPROVEN row | `waiting_for_user` | R2-D6 pending |
| `G-CACHEBUF-01` | UNPROVEN row + corrective task | `closed` | Task 014 corrective at `cda6c03703` |
| `B-WAIT-01` | Important INTENTIONAL_DEVIATION | `closed` | 017A resolved |
| `B-CALLERID-01` | Low INTENTIONAL_DEVIATION | `closed` | 017A resolved |
| `R2-D1` | user decision | `waiting_for_user` | — |
| `R2-D2` | user decision | `closed` | Task 012 corrective at `26325e8a32` |
| `R2-D3` | user decision | `waiting_for_user` | — |
| `R2-D4` | user decision | `waiting_for_user` | — |
| `R2-D5` | user decision | `waiting_for_user` | — |
| `R2-D6` | user decision | `waiting_for_user` | — |
| `R2-D7` | user decision | `waiting_for_user` | — |
| `R2-D8` | user decision | `waiting_for_user` | — |
| `T015-D5` | user decision | `waiting_for_user` | — |
| `G-CACHEBUF-01` | user decision | `closed` | Task 014 corrective at `cda6c03703` (same obligation as row) |
| `SD4-EVIDENCE` | user decision + evidence debt | `waiting_for_user` | — |
| SD8 scheduler recursive-mutex | §9.3 forward obligation | `closed` | 017A resolved |
| F-USED-01/F-METRIC-01 | §9.3 forward obligation | `closed` | 017A resolved |
| L-FP-EVICTSEG-01 / L-FP-EVICTPUSH-01 | §9.3 forward obligation | `forward_deferred` | future task |
| `P-META-REMOTEMETA-01` | INTENTIONAL_DEVIATION (§6.4) | `forward_deferred` | future object-storage task |
| FWD-ODIRECT-01 | §11 evidence gate | `forward_deferred` | Task 017/018 wave |
| Real errno producer | §11 evidence gate | `forward_deferred` | pre-release |
| StatusFile unclean-restart | §11 evidence gate | `forward_deferred` | pre-release |

### Counts by disposition

| disposition | count |
|---|---|
| `closed` | 7 (B-WAIT-01, B-CALLERID-01, SD8, F-USED-01/F-METRIC-01, L-CALLONCE-01, R2-D2, G-CACHEBUF-01 row+decision) |
| `accepted_deviation` | 0 |
| `forward_deferred` | 6 (L-FP-EVICTSEG-01, L-FP-EVICTPUSH-01, P-META-REMOTEMETA-01, FWD-ODIRECT-01, errno producer, StatusFile) |
| `reopen_task` | 0 |
| `waiting_for_user` | 15 (D-INIT-01, E-GETORCREATE-01, E-CREATE-01, E-UPDCFG-01, P-RB-SETDETACH-01, G-NEXTIMPL-01, R2-D1, R2-D3, R2-D4, R2-D5, R2-D6, R2-D7, R2-D8, T015-D5, SD4-EVIDENCE) |

### Unique task reopens

None.

---

## 9. Verdict

```text
review_5_task_2_verdict:      accepted_with_non_blocking_forward_debt
reason:                       D4/D6 remain pending by user decision but do not block Review 5
reopen_task_012:              closed at 26325e8a32 (Task 012 corrective; L-CALLONCE-01 EQUIVALENT; R2-D2 closed)
reopen_task_014:              closed at cda6c03703 (G-CACHEBUF-01 EQUIVALENT; G-CACHEOPEN-RENAME-01 closed)
waiting_for_user_count:       15 items (all non-blocking for Review 5)
parity_verdict:               PARITY_INCOMPLETE_NON_BLOCKING
unproven_rows:                6 (L-CALLONCE-01 and G-CACHEBUF-01 moved to EQUIVALENT)
blocking_items:               0
```

Task 012 and Task 014 corrective implementations are closed. Review 5 remains
required to report D4/D6 and the six remaining UNPROVEN rows, but they do not
block integrated tracing, final acceptance, or Task-017B authorization.
