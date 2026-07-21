# ClickHouse → Velox FileCache Parity Audit — Tasks 003–015 (Review round 4)

## 0. Status

```text
environment_profile:     root-oss
audit_round:             4
phase_a_status:          complete, validated (215 denominator rows, 0 BLOCKED)
phase_b_status:          complete, validated
phase_c_status:          complete — independent reviewer returned
                         READY_FOR_SYNTHESIS, 0 Critical, 0 Important
                         audit-document corrections; Phase-B counts/
                         classifications confirmed unchanged
verdict:                 PARITY_BLOCKED
```

This document is the durable synthesis of the accepted Tasks 003–015
ClickHouse (CH) → Velox `FileCache` port parity audit. It is derived from,
and must be read together with, the two Phase-A/Phase-B evidence artifacts:

- CH consumer contract ledger (Phase A, behavior oracle = CH `src/`):
  [`evidence/003-015-ch-consumer-contract-ledger.md`](evidence/003-015-ch-consumer-contract-ledger.md)
- Velox parity matrix (Phase B, classification of every ledger row):
  [`evidence/003-015-velox-parity-matrix.md`](evidence/003-015-velox-parity-matrix.md)

No production code, CMake, tests, receipts, prior reviews, Git state, Velox,
or Gluten were modified to produce this synthesis. Only these two files plus
this file and the companion decisions file were written.

---

## 1. Exact baselines and accepted scope

### 1.1 Repositories audited

| repo | branch | HEAD audited by Phase A/B/C | current actual HEAD at synthesis time |
|---|---|---|---|
| ClickHouse (`/root/oss/clickhouse`) | `ch-filecache` | `92707bea40423b8ddf53102e8f9eba8a2fa27658` | `a6155b4617fd226d37ef32504c50583822b096fb` |
| Velox (`/root/oss/velox`) | `filecache` | `43a9e6f75ffb94be38836b45fd476325665f50be` | `43a9e6f75ffb94be38836b45fd476325665f50be` (unchanged) |

**CH HEAD freeze note.** The Phase-A/B/C-audited CH HEAD is
`92707bea40423b8ddf53102e8f9eba8a2fa27658`. Production `src/` at that HEAD is
byte-identical to the expected source base
`a8f99d0d4f5ad3804e6a1693b5d8374865d0133c`
(`git diff --stat a8f99d0d4f…HEAD -- src/` is empty); the only delta at that
HEAD versus the expected base is the Review-4 agent-prompt file. Since that
audited HEAD, further **concurrent forward-planning edits** by another
process in this shared environment have appeared outside the Review-4 scope
— collectively touching
`port/design/filecache-task-017-018-joint-design.md`,
`port/task/017-filecache-observability-cancellation.md`,
`port/task/017a-filecache-statistics-cancellation.md`,
`port/task/017b-filecache-logging-exception-stack.md`,
`port/task/018-filecache-gluten-integration.md`,
`port/task/CONTROLLER_HANDOFF.md`, `port/task/EXECUTION_PROTOCOL.md`, and
`port/task/fullreview/root-oss/3/016-019-task-review.md`. These files were
absent or clean at audit startup and are **not** part of the accepted HEAD
`92707bea404`. This synthesis does **not** edit, revert, or use their
working-tree content, and does not enumerate their new substantive claims:
they are mentioned here only collectively, as excluded and untouched.

**Frozen-baseline reading rule.** Every pre-existing input this synthesis
relies on (`CONTROLLER_HANDOFF.md`, `EXECUTION_PROTOCOL.md`,
`017-filecache-observability-cancellation.md`,
`018-filecache-gluten-integration.md`,
`root-oss/3/016-019-task-review.md`, and every other design/task/review
doc) is read **pinned to the accepted SHA**
`git show 92707bea40423b8ddf53102e8f9eba8a2fa27658:<path>` — never the
live working tree and never a bare `HEAD` reference, since the real
repository `HEAD` in this shared environment has since moved beyond the
audited commit. `port/task/017a-filecache-statistics-cancellation.md`,
`port/task/017b-filecache-logging-exception-stack.md`, and
`port/design/filecache-task-017-018-joint-design.md` do not exist at the
pinned SHA (`git show <pinned-SHA>:<path>` fails for all three) — they
have **no accepted baseline whatsoever** and are ignored entirely by this
synthesis, not merely deprioritized.

Re-verified at synthesis time: `git diff --stat a8f99d0d4f…HEAD -- src/`
is still **empty** against the current live tree, and none of the
concurrent forward-planning edits above touch `velox/ch/`, any CMake
file, any test, any receipt, or `port/task/fullreview/root-oss/4/`. They
remain outside the accepted Tasks 003–015 implementation scope and do not
change any Phase-A/B classification, count, or percentage.

Velox HEAD is unchanged (`43a9e6f75f…`, worktree clean) between Phase B and
synthesis.

### 1.2 Accepted Velox commit range

`4bea8d15e^..43a9e6f75` (equivalently `bf379041f..43a9e6f75`), **23 commits**,
inclusive of the first Task-003 commit `4bea8d15e`. Full commit-to-task table:
[`evidence/003-015-velox-parity-matrix.md` §1](evidence/003-015-velox-parity-matrix.md#1-baselines-and-accepted-velox-commit-range).

### 1.3 Scope

Audited: the accepted implementation owned by Tasks 003–015 —
`velox/ch/Common/`, `velox/ch/IO/`, `velox/ch/Interpreters/FileCache/`,
`velox/ch/Disks/IO/`, task-owned tests/CMake, and the Task-015 E2E/seek
benchmark — against CH `src/Interpreters/FileCache/`,
`src/Disks/IO/` FileCache consumers, and every other real CH caller reachable
from those surfaces.

**Excluded from the parity denominator** (not defects of Tasks 003–015):

- **Task 016** (`TemporaryDataOnDisk`): deferred by the user; no Velox
  temporary-data consumer exists. 8 `X-TMP-*` rows recorded for reachability only.
- **Task 017 / 018 / 019** (per frozen accepted-HEAD content; see §1.1,
  §10): planned, `task_017_allowed: false` / `task_018_allowed: false`,
  reviewed contract pending redesign. Design inputs are inventoried in §10.
- High-level cached write-through consumer (`cache_on_write_operations`,
  `CachedOnDiskWriteBufferFromFile`) — explicitly rejected/deferred; 14
  Forward-Appendix-H rows.
- SQL system-table / `SYSTEM DROP·SYNC` / `DESCRIBE FILESYSTEM CACHE` /
  `ServerAsynchronousMetrics` observability — 15 Forward-Appendix-F rows.

---

## 2. Methodology and denominator definition

Full method: [ledger §Method](evidence/003-015-ch-consumer-contract-ledger.md#method)
and [matrix §2](evidence/003-015-velox-parity-matrix.md#2-method-and-authority).

- **Behavior oracle**: current CH `src/` production source and its real
  callers — never Velox tests, receipts, task docs, or prior reviews.
- **Denominator**: **215 atomic in-scope CH consumer contract rows**
  recovered in Phase A from real production callers, one row per
  independently-classifiable behavior (state transition, ownership,
  concurrency/lock, error/errno behavior, persistence/path, or setting).
  Composite behaviors were split (Revision 4) so no row hides an independent
  sub-behavior that could carry a different Velox status.
- **Not counted in the denominator**: `VELOX_EXTENSION`, Tasks 016–019
  forward consumers (29 Forward-Appendix rows + 8 excluded `X-TMP-*` rows),
  duplicate rows, and compile-only names with no real CH caller.
- **Classification vocabulary** (Phase B, per row): `MATCH`, `EQUIVALENT`,
  `INTENTIONAL_DEVIATION` (only if explicitly approved/conditionally
  accepted), `MISSING`, `UNPROVEN` (present but the guarantee is unproven —
  including any row governed by an unapproved, guarantee-changing
  root-oss/2 `D1`–`D8` replacement), `VELOX_EXTENSION`, `OVER_PORT`.
- **Formulas**:

```text
semantic parity   = (MATCH + EQUIVALENT) / 215
accepted coverage = (MATCH + EQUIVALENT + approved/conditional INTENTIONAL_DEVIATION) / 215
```

- Total documented contracts across all categories: **252** = 215
  denominator + 29 forward (Appendices F + H) + 8 excluded (`X-TMP-*`).
- **Phase-C independent verification** (Controller review, this round):
  checked every `MATCH`/`EQUIVALENT` claim against source, searched for
  missing callers/sibling paths, checked state/failure/lifecycle
  divergence, checked structural-deviation approvals, checked that test
  evidence was freshly built (not a stale executable), and checked the
  quantitative denominator/counts. Result: **zero Critical, zero Important**
  corrections to either evidence document; Phase-B counts and
  classifications **confirmed unchanged**. Verdict: **READY_FOR_SYNTHESIS**.

---

## 3. Status counts and percentages

Raw counts (215 denominator rows; source:
[matrix §4](evidence/003-015-velox-parity-matrix.md#4-quantification), independently
re-verified by an automated parser at
[matrix §12](evidence/003-015-velox-parity-matrix.md#12-automated-validation-and-self-review)):

| status | count |
|---|---|
| MATCH | 172 |
| EQUIVALENT | 30 |
| INTENTIONAL_DEVIATION | 5 |
| UNPROVEN | 8 |
| MISSING | 0 |
| **total** | **215** |

```text
semantic parity   = (172 + 30) / 215       = 202 / 215 = 94.0%
accepted coverage = (172 + 30 + 5) / 215   = 207 / 215 = 96.3%
```

Forward rows (29) and Task-016-excluded rows (8) are **not** part of either
percentage; `VELOX_EXTENSION`/`OVER_PORT` items (§8) are outside the CH
denominator entirely.

---

## 4. Per-bucket summary

Copied exactly from the validated
[matrix §4 "Per-bucket breakdown"](evidence/003-015-velox-parity-matrix.md#per-bucket-breakdown)
(do not recompute — this is the authoritative table):

| bucket | MATCH | EQUIV | INT_DEV | UNPROVEN | MISSING | total | sem.parity | accepted |
|---|---|---|---|---|---|---|---|---|
| Tasks 003–010 (combined) | 99 | 20 | 4 | 2 | 0 | 125 | 95.2% | 98.4% |
| Task 011 | 25 | 0 | 0 | 0 | 0 | 25 | 100.0% | 100.0% |
| Task 012 | 30 | 1 | 1 | 0 | 0 | 32 | 96.9% | 100.0% |
| Task 013 | 8 | 2 | 0 | 4 | 0 | 14 | 71.4% | 71.4% |
| Task 014 | 10 | 2 | 0 | 2 | 0 | 14 | 85.7% | 85.7% |
| Task 015 | 0 | 5 | 0 | 0 | 0 | 5 | 100.0% | 100.0% |
| cross-cutting | 0 | 0 | 0 | 0 | 0 | 0 | — | — |
| **total** | **172** | **30** | **5** | **8** | **0** | **215** | **94.0%** | **96.3%** |

`cross-cutting = 0`: every denominator row has a truthful single
`task_owner` recovered in Phase A; no row qualifies as an ownerless
≥3-task shared-infrastructure row (per the audit's own definition).

**Reading this table**: Task 013 (`FileCacheFactory`/manager lifecycle) has
the weakest current coverage (71.4%/71.4%) because all 4 of its UNPROVEN
rows are governed by the pending `D4` transactional-reload decision. Tasks
011 and 015 are fully closed (100%/100%). Tasks 003–010 combined carry 2 of
the 8 UNPROVEN rows (the `D2` `call_once` replacement) plus 4 approved-MVP
intentional deviations (`D10` failpoints + `B-CALLERID-01`).

---

## 5. Critical / Important findings, grouped by root cause

**Critical findings: 0** (confirmed independently by Phase C).

**Important findings: 10 in-denominator rows** (8 `UNPROVEN` + 2 `Important`
intentional deviations), grouped by root cause:

### 5.1 Root cause: pending root-oss/2 `D2`/`D4`/`D6` guarantee-changing replacements (7 UNPROVEN rows)

All seven rows below have a present, tested Velox implementation whose
*guarantee* depends on an unapproved (`user_post_review`, no user decision
recorded) structural replacement. Per audit rule, an unapproved
guarantee-changing replacement is `UNPROVEN` even though current output
tests pass.

| row_id | task | governing pending decision |
|---|---|---|
| `L-CALLONCE-01` | 003 (primitive) / 012 (usage) | `D2` — `std::call_once` replaced by mutex+flag |
| `D-INIT-01` | 013 | `D2` (once-guard) **and** `D4` (Manager init transaction) |
| `E-GETORCREATE-01` | 013 | `D4` — Manager mutation serialization/transactional reload |
| `E-CREATE-01` | 013 | `D4` |
| `E-UPDCFG-01` | 013 | `D4` |
| `P-RB-SETDETACH-01` | 007/014 | `D6` — reader detach/query-pool-lifetime UAF fix |
| `G-NEXTIMPL-01` | 014 | `D6` |

Full row-level evidence:
[matrix §6](evidence/003-015-velox-parity-matrix.md#6-missing--unproven--unapproved-differences-severity--reopen-owner).
Decision detail: [decisions-needed file](003-015-parity-decisions-needed.md),
items `R2-D2`, `R2-D4`, `R2-D6`.

### 5.2 Root cause: source-confirmed absent CH self-heal behavior (1 UNPROVEN row)

| row_id | task | finding |
|---|---|---|
| `G-CACHEBUF-01` | 014 | CH's `getCacheReadBuffer` detects on-disk size < recorded downloaded size (external truncation) and warns + bypasses to the remote source + re-fetches (`FileSegment.cpp:1235-1246` even documents this in a comment). Velox's `createCacheReadBuffer` (`FileCache.cpp:395`) has **no size-mismatch check at all** — the comment describes behavior that does not exist in code. This is independent of `D1`–`D8`. |

Everything else about `G-CACHEBUF-01` (cache-file open mode = `pread`, no
`O_DIRECT`; the CH rename-race handling) is present and tested
(`CachedReaderSeesGrownSegment`, mutation M9); only the external-truncation
self-heal sub-behavior is absent. Decision:
[decisions-needed file](003-015-parity-decisions-needed.md), item `G-CACHEBUF-01`.

### 5.3 Root cause: intentionally deviating behavior rated Important (2 rows)

| row_id | why Important | disposition |
|---|---|---|
| `B-WAIT-01` | Read-path cancellation structurally present but the caller passes a default never-cancel token; real `KILL`/timeout interruption is not wired. | **Deferred to Task 017** — accepted current no-op-shim contract, not an auto-approved permanent deviation. |
| `P-META-REMOTEMETA-01` | `getRemoteFileMetadata` unconditionally returns `nullopt`, so truncation-vs-logic-error discrimination is impaired (both fail-closed identically). | **Accepted** per cross-profile/1 + Task-014 contract; real provider is Task 017/018 forward work. |

Both are already fully classified/approved-or-deferred (§6), not open user
decisions — see §9 for their reopen ownership.

---

## 6. Intentional deviations (5) and exact approval state

Copied and cross-checked against
[matrix §7](evidence/003-015-velox-parity-matrix.md#7-intentional-deviations-5--exact-approval-state):

| row_id | deviation | exact approval state |
|---|---|---|
| `B-WAIT-01` | Structural cancellation-token signature present; caller passes a default never-cancel token; only the 60 s hard deadline bounds a stuck wait. | **Deferred to Task 017** ("Task 017 owns real read-path cancellation"; contract "must be redesigned"). Current no-op-shim is the accepted contract for Tasks 003–015 only — **not** a final approved permanent deviation. |
| `B-CALLERID-01` | `getCallerId()` returns `None:<tid>`, not CH's verbatim `None:<threadname>:<tid>`. | Downloader-identity uniqueness **explicitly accepted as sufficient** (root-oss/1 §6 F-CALLERID, cross-profile/1 "Caller identity"). Exact diagnostic-format restoration is a **deferred Task-017** obligation, not pending user approval. |
| `L-FP-EVICTSEG-01` | `file_cache_simulate_evicting_segment` failpoint seam absent; the production `evicting→DETACHED` logic it would force is present and correct. | **`D10` approved** (root-oss/1 §7): no-op/absent failpoint shims accepted for the MVP; real failpoint surface is Task 017. |
| `L-FP-EVICTPUSH-01` | `file_cache_background_eviction_push_fail` seam absent; production free-space reschedule-on-failure logic is present and correct. | **`D10` approved** (same as above). |
| `P-META-REMOTEMETA-01` | `getRemoteFileMetadata` returns `nullopt` unconditionally; both truncation and logic errors fail-close identically. | **Accepted** (cross-profile/1: "`getRemoteFileMetadata == nullopt` means truncation metadata is unavailable; tests must not assume a real metadata source"). Real provider is Task 017/018 forward object-storage plumbing. |

**Do not confuse with approved deviations**: root-oss/2 `D1`–`D8` are **all**
recorded as `user_post_review` with no appended user decision — per audit
rule these are **not** auto-approved deviations, and the 7 rows they govern
are classified `UNPROVEN` (§5.1), not `INTENTIONAL_DEVIATION`.

Task-015 direct-I/O `D3`/`D4` are **conditional** intentional deviations that
are explicitly **outside the 215-row denominator** (CH cache-file reads are
never `O_DIRECT`, so there is no CH denominator behavior to deviate from);
see §11.

---

## 7. All 8 UNPROVEN rows

Full detail:
[matrix §6](evidence/003-015-velox-parity-matrix.md#6-missing--unproven--unapproved-differences-severity--reopen-owner).

| row_id | severity | task | why UNPROVEN | reopen owner |
|---|---|---|---|---|
| `L-CALLONCE-01` | Important | 003/012 | `FileCache.cpp:429` uses `std::mutex`+`bool` flag, not `std::call_once`; root-oss/1 approved only the `std::call_once` mapping. | user post-review `D2` |
| `D-INIT-01` | Important | 013 | `initialize()` body matches CH, but its once-guard is `D2` and it is wrapped in the `D4` Manager init transaction — both pending. | user post-review `D2` + `D4` |
| `E-GETORCREATE-01` | Important | 013 | CH dedup-by-path/alias/conflict-throw semantics match, but registration is wrapped in `D4`'s `mutationMutex` + transactional grow/rollback (pending). | user post-review `D4` |
| `E-CREATE-01` | Important | 013 | Same as above via `createLocked`; `D4` governs the transaction. | user post-review `D4` |
| `E-UPDCFG-01` | Important | 013 | CH `updateSettingsFromConfig` maps to `FileCacheManager::applyConfigs`, a `D4` serialized fail-close transactional reload (pending). CH restore-on-failure is preserved/hardened. | user post-review `D4` |
| `P-RB-SETDETACH-01` | Important | 007/014 | `set(nullptr,0)` empty-detach + `releaseOwnedBuffer` before background handoff is the `D6` use-after-free fix — a pending guarantee-changing ownership/lifetime model vs CH "restore owned window". | user post-review `D6` |
| `G-NEXTIMPL-01` | Important | 014 | `readNextChunk` orchestration matches CH, but reader-handoff safety (release/drop-on-throw) is the `D6` UAF fix (pending). | user post-review `D6` |
| `G-CACHEBUF-01` | Important | 014 | Core cache-file open (pread, no O_DIRECT) matches, and the CH rename-race is handled equivalently — but CH's external-truncation self-heal (warn + bypass + re-fetch) is **absent**; a stale code comment claims it exists. | hardening / pre-release gate (independent of `D1`–`D8`) |

If the user approves `D2`/`D4`/`D6` as documented, seven of these eight become
eligible for their row-specific `MATCH`/`EQUIVALENT`/`INTENTIONAL_DEVIATION`
classification.
`G-CACHEBUF-01` needs an independent real fix or an explicit deviation
decision — it is **not** resolved by any `D1`–`D8` approval.

---

## 8. Velox extensions and over-port inventory

Copied and cross-checked against
[matrix §8](evidence/003-015-velox-parity-matrix.md#8-velox-only-inventory--velox_extension--over_port-outside-the-ch-denominator)
and [matrix §5B](evidence/003-015-velox-parity-matrix.md#5b-cmake--object-library--odr-build-closure-mandatory):

| item | class | note |
|---|---|---|
| `FileCacheManager` (composes Factory + `FileCacheWorkerPool` + `FileCacheScheduler` + `OpenedFileCache` + `MemoryPool`) | `VELOX_EXTENSION` | No CH analogue; its constituent CH contracts (Factory §E, global ownership/shutdown) are separately scored in the denominator. Carries `D4` serialization + shutdown ordering. |
| `OpenedFileCache` (1024-bucket weak-`OpenedFile` map) | `VELOX_EXTENSION` | Ports CH `src/IO/OpenedFileCache.h` as a Task-013 dependency (user-approved mapping). `D5` weak-bucket deleter (pending) makes late-handle destruction safe. |
| `FileCacheQueryIdScope` | `VELOX_EXTENSION` | CH has no dedicated scope class; backs `O-QUERYSCOPE-01`/`B-CALLERID-01`. Exact-format restoration is Task-017 `F-CALLERID`. |
| Direct-I/O foreground aligned physical round-up/logical clamp | `VELOX_EXTENSION` / conditional `INT_DEV` | Task015 `D3` conditional; logic coverage only (strict mocks); real kernel `O_DIRECT` mandatory (§11). |
| Direct-I/O background aligned-body/pure-tail skip (+ `TestValue` notification) | `VELOX_EXTENSION` / conditional `INT_DEV` | Task015 `D4`: non-DIO accepted as CH-aligned; DIO policy pending real `O_DIRECT`. |
| Benchmark fresh-random-key freshness | `OVER_PORT` | Task015 `D5` **UNRESOLVED** (approve/modify/reject not signed). Avoids destructively clearing a flag-supplied path. |
| `TestValue::adjust`/`FAIL_POINT_TRIGGER` release-inert seams | `VELOX_EXTENSION` (test surface) | `D3` (pending) maps selected CH `fiu` failpoints to Velox `TestValue`; release-elided but the **production** `velox_ch_filecache_core` library PUBLIC-links `velox_test_util` (`Interpreters/FileCache/CMakeLists.txt:99`). Extra seams beyond the CH denominator (SLRU-downgrade, dynamic-resize, modify-size-limits, manager shutdown/apply). |
| Forward-F low-level surfaces present without an in-scope consumer (`removeAllReleasable`, `removeKeyIfExists`, `removePathIfExists`, `sync`, `getFileSegmentInfos`, `getCacheIterator`, `tryGetCachePaths`, `getUsedCacheSize`, `dumpQueue`, `getEvictionPolicyType`) | `OVER_PORT` | Implemented although SQL system-table/`SYSTEM DROP·SYNC`/`DESCRIBE` consumers are out of Tasks 003–015 scope. |
| Low-level write-through segment-creation surfaces (`set`, `trySet`, `getOrSet(write,limit=1,file_size=0)`) | `OVER_PORT` | Present (also reachable from read-fill); the high-level write-through consumer that would drive them (`CachedOnDiskWriteBufferFromFile`) is absent (Forward Appendix H). |
| CMake mono/non-mono object-library + `FILE_SET` header surfaces (4 libraries: `velox_ch_filecache`, `_core`, `_manager`, `_dwio`) | `VELOX_EXTENSION` (build) | No CH analogue (CH is a monolithic `dbms`). `D1` (pending) registers `ShardedMap.h` as a public non-mono header — build-packaging only. Verified ODR-clean: 23 sources + 48 headers each registered exactly once. |

---

## 9. Required task reopen list

### 9.1 Unconditional (must be reopened regardless of any pending `D1`–`D8` decision)

| task | reason |
|---|---|
| **Task 014** (`FileSegment`/`FileCache` read path) | `G-CACHEBUF-01` external-truncation self-heal is source-confirmed absent, independent of any `D1`–`D8` decision. Must either implement the CH-matching fix, or the user must explicitly record an approved intentional deviation and the stale `FileSegment.cpp:1235-1246` comment must be corrected. |

### 9.2 Decision-conditional (reopen scope and whether reopening is even needed depends on the user's `D1`–`D8`/Task-015-`D5` decision — see companion decisions file)

| task | governs | decision |
|---|---|---|
| Task 009 (`ShardedMap`) | build packaging only | `R2-D1` |
| Task 012 (`FileCache`/`FileSegment` core SCC) | `L-CALLONCE-01` primitive swap; production `velox_test_util` link | `R2-D2`, `R2-D3` |
| Task 013 (`FileCacheFactory`/Manager) | `D-INIT-01`, `E-GETORCREATE-01`, `E-CREATE-01`, `E-UPDCFG-01`; `OpenedFileCache` deleter | `R2-D4`, `R2-D5` |
| Task 007/014 (reader detach, read path) | `P-RB-SETDETACH-01`, `G-NEXTIMPL-01` | `R2-D6` |
| Task 014 (predownload/failure hardening) | already reflected as `MATCH`/`EQUIVALENT`; approval regularizes bookkeeping | `R2-D7` |
| Task 012 (mutation-test correction) | test-authoring correction only, no production effect | `R2-D8` |
| Task 015 (benchmark) | non-destructive fresh-key benchmark behavior | `T015-D5` |
| Task 012 (`Metadata`/`ShardedMap`) | SD4 no-reference-across-mutation proof | `SD4-EVIDENCE` |

### 9.3 Forward planned owners (not reopened now — future task scope, already decided)

| owner | items |
|---|---|
| **Task 017** | `B-WAIT-01` real cancellation wiring; `B-CALLERID-01` exact `None:<threadname>:<tid>` format; real `CurrentMetrics`/`ProfileEvents` (`F-USED-01`, `F-METRIC-01`); `SD8` scheduler recursive-mutex resolution; `L-FP-EVICTSEG-01`/`L-FP-EVICTPUSH-01` real failpoint surface. Per frozen-baseline content (§1.1), Task 017 remains one planned task pending redesign (`task_017_allowed: false`); redesign scope beyond that frozen content is not part of this audit's accepted baseline. |
| **Task 018 / 019** | `P-META-REMOTEMETA-01` real remote-metadata provider; Gluten builder-selection and real host-path E2E; complete benchmark suite (per frozen `018-filecache-gluten-integration.md`: full Velox benchmark suite, not only TPCH). Forbidden to inspect/modify in this audit. |
| **Real kernel `O_DIRECT`** (`FWD-ODIRECT-01`) | Mandatory before performance claims or Gluten rollout; closes the Task-015 `D3`/`D4` conditional gate. Owner: Task 017/018 design wave. |
| **Pre-release gates** | Structured errno **producer** in the concrete cache writer (consumer side already done); `StatusFile` unclean-restart read-before-truncate diagnostics; real `ENOSPC`/`EDQUOT` evidence. These are automatic engineering follow-ups already directed by cross-profile/1, not open user decisions. |
| **Post-Task-019 evidence** | `sipHash128` golden vectors + mutation (`Q-KEYHASH-01`); malformed-character differential fuzz vs `unhexUInt` (`Q-KEYVALID-01`). |

---

## 10. Task-017/018 design inputs

Task 017/018 are **planned, not accepted implementation**, and are
correctly excluded from the 215-row denominator. Per the frozen-baseline
reading rule (§1.1), the design-input record below reflects only the
accepted-HEAD (`92707bea404`) content of the pre-existing task documents:

- **`port/task/017-filecache-observability-cancellation.md`**:
  `disposition: planned`, `task_017_allowed: false` — "user wants this
  capability, but the reviewed contract must be redesigned first";
  co-designed with Task 018 at the statistics boundary (neither task's
  metric storage or benchmark output columns may be finalized
  independently).
- **`port/task/018-filecache-gluten-integration.md`**: `disposition:
  planned`, `task_018_allowed: false` — "user wants Gluten integration and
  the complete Velox benchmark suite, but the reviewed contract must be
  redesigned first"; co-designed with Task 017; must adapt the complete
  Velox benchmark suite (not only TPCH) from the `baibaichen/ch-filecache`
  reference, matching behavior/harness structure to the current
  `velox/ch` `FileCacheManager`/`FileCacheBufferedInput` APIs, not copying
  files verbatim.

Both remain priority-only selections by the user — neither approves the
previously reviewed contract nor authorizes implementation.

**Concurrent forward-planning edits excluded.** As noted in §1.1, further
edits to these and related forward-planning files (including a new
joint-design document and a split of Task 017 into two sub-task documents)
have appeared in this shared environment outside the Review-4 scope, were
absent or clean at audit startup, and are not part of the accepted HEAD.
This synthesis does not use their working-tree content, does not edit or
revert them, and does not enumerate their substantive claims — they are
recorded here only as excluded and untouched. None of this material
changes any Tasks 003–015 classification, count, or percentage in this
audit.

---

## 11. Forward gates

These are pre-release/forward gates, **outside** the 215-row denominator,
that must close before further rollout claims:

| gate | status | note |
|---|---|---|
| **Real kernel `O_DIRECT`** (`FWD-ODIRECT-01`) | `UNPROVEN`, mandatory | Strict direct-I/O adapter mocks (`ReadBufferFromVeloxReadFile` alignment checks, `io_test ReaderDirectIo*`) are logic coverage only. Root-oss/3: "place a real kernel `O_DIRECT` integration test before performance claims or Gluten rollout." Governs the Task015 `D3`/`D4` conditional acceptance. Owner: Task 017/018 design wave. |
| **Real errno producer** | forward, pre-release | The typed-errno **consumer** contract (`B-WRITE-01`/`L-ERRNOEXC-01`) is done and tested; the concrete cache-file writer that reliably **produces** structured `ENOSPC`/`EDQUOT` in production is a separate, not-yet-closed pre-release gate (cross-profile/1). |
| **`StatusFile` unclean-restart diagnostics** | forward, pre-release | Basic ctor/flock/ftruncate/write/dtor/unlink lifecycle is in the denominator (Section M) and matches; read-before-truncate diagnostics for an unclean-restart scenario is a separate, deliberately-not-modeled pre-release gate. |
| **`G-CACHEBUF-01` external-truncation self-heal** | source-confirmed **absent**, in-denominator `UNPROVEN` finding | See §5.2, §7. Requires a real fix or an explicit approved-deviation decision (`decisions-needed` file). |
| **SD4 no-reference-across-mutation proof** | evidence debt | `R-BUCKETMAP-01` relies on the by-construction `shared_ptr<KeyMetadata>` stability argument; no dedicated test proves no iterator/reference/address survives an F14 `MetadataBucket` mutation. See `decisions-needed` item `SD4-EVIDENCE`. |

These forward gates are listed separately from the denominator
finding-count in §3/§5 — they do not inflate the 8-UNPROVEN /
0-Critical / 2-Important-deviation counts, except for `G-CACHEBUF-01`,
which **is** one of the 8 in-denominator UNPROVEN rows and is listed in
both places for completeness.

---

## 12. Final verdict

```text
PARITY_BLOCKED
```

**Rationale.** Semantic parity (94.0%) and accepted coverage (96.3%) are
high, zero rows are `MISSING`, and zero findings are rated Critical.
However:

1. **Eight in-denominator rows remain `UNPROVEN`** (§7) — seven because they
   depend on root-oss/2 `D2`/`D4`/`D6` guarantee-changing replacements that
   are still `user_post_review` with no appended user decision, and one
   (`G-CACHEBUF-01`) because a CH behavior (external-truncation self-heal) is
   source-confirmed **absent** from the Velox implementation despite a code
   comment asserting it exists.
2. Per audit rule, an unapproved guarantee-changing replacement cannot be
   scored `MATCH`/`INTENTIONAL_DEVIATION` regardless of how well current
   output tests pass, and a source-confirmed absent behavior cannot be
   scored anything but `UNPROVEN`/`MISSING`.

A verdict of `PARITY_ACCEPTED_WITH_DECISIONS` is not appropriate while a
real (not merely pending-approval) behavioral gap — `G-CACHEBUF-01` — sits
unresolved in the denominator with no decision framing offered to close it
other than "fix it or explicitly accept the gap."

**Minimum path to unblock** (in order):

1. **Decide `D2`/`D4`/`D6`** (root-oss/2, all currently `user_post_review`):
   approve, modify, or reject each. Approving as documented makes seven of
   the eight UNPROVEN rows to `MATCH`/`INTENTIONAL_DEVIATION`.
2. **Fix or explicitly approve a deviation for `G-CACHEBUF-01`**: either
   implement the CH-matching external-truncation warn/bypass/re-fetch
   behavior, or have the user explicitly record acceptance of the gap as a
   permanent intentional deviation (and correct the misleading source
   comment either way).
3. **Close the remaining required evidence gates** named in §11 as
   practical: the SD4 no-reference-across-mutation proof (or an explicit
   user acceptance of the current source-level argument), the structured
   errno producer, and `StatusFile` unclean-restart diagnostics.
4. **Real kernel `O_DIRECT` remains mandatory** before any performance claim
   or Gluten rollout — this gates Task-015 `D3`/`D4` regardless of the
   `D2`/`D4`/`D6`/`G-CACHEBUF-01` resolution above, and is tracked
   separately as a Task-017/018 forward-design item (§10), not as part of
   the Tasks 003–015 acceptance decision itself.

Once steps 1–2 are resolved, re-run Phase B's classification on the
affected rows only (no re-audit of the full 215 rows is required) and this
verdict should be re-evaluated; it is expected to become
`PARITY_ACCEPTED_WITH_DECISIONS` or `PARITY_ACCEPTED` at that point,
independent of the still-open forward `O_DIRECT`/Task-017/018 gates, which
are correctly out of the Tasks 003–015 denominator by design.

---

## 13. Decisions requiring user choice

See the companion file:
[`003-015-parity-decisions-needed.md`](003-015-parity-decisions-needed.md)
for the 11 items requiring an explicit user decision
(`R2-D1`–`R2-D8`, `T015-D5`, `G-CACHEBUF-01`, `SD4-EVIDENCE`).

---

*Artifacts: `port/task/fullreview/root-oss/4/003-015-ch-parity-audit.md`
(this file) and
`port/task/fullreview/root-oss/4/003-015-parity-decisions-needed.md`.
Read-only synthesis; no production source, CMake, tests, receipts, Git
state, Velox, or Gluten were modified.*
