# Tasks 003-015 ClickHouse-to-Velox Parity Audit

Read-only source-contract parity audit of the accepted ClickHouse `FileCache`
port through Task 015. This is the durable final report; the two evidence files
under `evidence/` are the row-level backing.

## 1. Baselines and scope

```text
environment_profile: home-chang
ClickHouse repo:  /home/chang/SourceCode/ClickHouse   branch ch-filecache2   HEAD db3456deba7
Velox repo:       /home/chang/OpenSource/velox        branch filecache2      HEAD 398426810
Gluten:           out of scope, never touched (Tasks 018-019)
```

Behavior oracle: ClickHouse `src/Interpreters/FileCache/` and the real
`FileCache` read/write consumers `src/Disks/IO/CachedOnDiskReadBufferFromFile.*`
and `CachedOnDiskWriteBufferFromFile.*`. Task receipts, prior reviews, and tests
were treated as declared intent and evidence, never as the behavior oracle.

Audited implementation (owned by accepted Tasks 003-015):

```text
velox/ch/Common/  velox/ch/IO/  velox/ch/Interpreters/FileCache/  velox/ch/Disks/IO/
task-owned tests + CMake, Task-015 E2E and seek benchmark
```

Excluded from the parity denominator (forward work, not accepted implementation):
Task 016 (temporary-data / write-through consumer, deferred), Task 017
(observability + cancellation, planned — only the no-op enumerator surface B1/B2
is in scope now), Tasks 018-019 (Gluten integration).

### Environment-note (prompt vs machine layout)

The audit prompt was authored for the `root-oss` layout. On `home-chang` the
approval/decision artifacts live at different paths, and there is **no
independent Task-015 decision file**; the prompt's `D1-D6` IDs do not exist here.
Real decision IDs on this machine: `B1/B2`, `SD1-SD9`, `D10`, `E1-E5`, `R2-R10`,
plus the round-2 findings. Authoritative approval sources used:

```text
Round-1 (003-010): port/task/fullreview/root-oss/1/003-010-review-decisions.md
Round-2 (011-014): port/task/fullview/home-chang/2/011-014-review-decisions.md (+ .../2/evidence/)
```

## 2. Methodology and denominator

Three read-only phases, each a fresh isolated agent:

- **Phase A** — CH-only consumer-contract recovery (read only ClickHouse source
  and history; no Velox). Output: `evidence/003-015-ch-consumer-contract-ledger.md`,
  64 atomic rows, 0 BLOCKED.
- **Phase B** — Velox implementation comparison; each CH row classified with the
  vocabulary below, guarantees (not just outputs) checked, every structural
  replacement cross-checked against a signed deviation. Output:
  `evidence/003-015-velox-parity-matrix.md`.
- **Phase C** — independent high-rigor reviewer that tried to break A/B: spot-read
  both CH and Velox citations on the highest-risk rows, hunted for skipped
  callers, checked deviation sign-off, checked test freshness, re-counted the
  denominator. Output: `003-015-parity-phase-c-review.md`.

**Denominator** = the 64 atomic in-scope CH consumer-contract rows from Phase A.
`VELOX_EXTENSION`, Tasks 016-019 forward consumers, duplicates, and compile-only
names with no real CH caller are excluded.

Classification vocabulary: `MATCH`, `EQUIVALENT`, `INTENTIONAL_DEVIATION`,
`MISSING`, `UNPROVEN`, `VELOX_EXTENSION`, `OVER_PORT` (defined in the prompt; an
unregistered guarantee-changing replacement is `UNPROVEN` even if output tests
pass).

## 3. Status counts and percentages

| group | MATCH | EQUIVALENT | INTENTIONAL_DEVIATION | MISSING | UNPROVEN | rows |
|---|---|---|---|---|---|---|
| Tasks 003-010 | 7 | 0 | 1 | 0 | 0 | 8 |
| Task 011 | 3 | 0 | 0 | 0 | 0 | 3 |
| Task 012 | 22 | 0 | 1 | 0 | 0 | 23 |
| Task 013 | 4 | 0 | 0 | 0 | 0 | 4 |
| Task 014 | 15 | 0 | 3 | 0 | 0 | 18 |
| Task 015 | 2 | 0 | 0 | 0 | 0 | 2 |
| cross-cutting | 4 | 0 | 2 | 0 | 0 | 6 |
| **Total** | **57** | **0** | **7** | **0** | **0** | **64** |

Plus 5 Velox-only rows (excluded from the denominator): 4 `VELOX_EXTENSION`,
1 benign `OVER_PORT`.

```text
Semantic parity  = (MATCH + EQUIVALENT) / in-scope        = (57 + 0) / 64 = 57/64 = 89.1%
Accepted coverage = (MATCH + EQUIVALENT + approved DEV) / in-scope = (57 + 0 + 7) / 64 = 64/64 = 100%
```

All 7 `INTENTIONAL_DEVIATION` rows cite a real signed decision. Zero `MISSING`,
zero `UNPROVEN`, zero unapproved deviations.

## 4. Per-task summary

- **003-010** (8): leaf types, settings, shims, sharded map, errno contract — all
  `MATCH` except R-010-3 (write-through options fields have no MVP read consumer;
  registered Task-016 deviation).
- **011** (3): priority/eviction — faithful direct translation; algorithm, locks,
  containers align with CH (round-2 pass, internal structure signed).
- **012** (23): center SCC (segment/metadata/cache/query-limit) — 22 `MATCH`,
  1 registered deviation (R-012-8 `wait` defers `throwIfKilled` to Task 017 under
  a 60s bounded deadline). F14 metadata + QueryLimit tables signed (SD4 condition
  proven, round-2 §5).
- **013** (4): factory/manager/opened-file-handle — all `MATCH`; the
  `OpenedFileCache` weak-ref self-clean with no eviction cap was confirmed present
  (no spurious LRU cap added).
- **014** (18): reader/inputstream — 15 `MATCH`, 3 registered deviations
  (R-014-12 `readBigAt`, R-014-14 CACHED read-until F-014-2, R-014-17 O_DIRECT
  baseline). The truncation self-heal (F-014-1, CH `CachedOnDiskReadBufferFromFile.cpp:448-477`)
  is present and RED-proven.
- **015** (2): Velox-only E2E + seek benchmark — `MATCH`; validates
  already-approved 003-014 behavior; production diff empty.
- **cross-cutting** (6): lock hierarchy, `std::list` iterator stability,
  physical/downloaded/reserved reconciliation — 4 `MATCH`, 2 registered
  deviations (R-XC-5 failpoint/TestValue no-op shim, R-XC-6 write-through cache
  log element).

## 5. Critical / Important findings

- **Critical: 0.**
- **Important: 1 — R-014-17 (D3 real kernel O_DIRECT).** Cache reads currently use
  local-FS `pread` through the page cache; real-kernel `O_DIRECT` integration is
  unproven. D3 is a *conditional* intentional deviation and a **mandatory forward
  gate**: strict-mock / local-file tests are logic coverage only, not real-kernel
  proof. Owner: Task 017+/pre-release. This does not lower accepted coverage (the
  deviation is registered) but it is the one outstanding real-world proof
  obligation for the read path.

Phase C additionally caught (and resolved, evidence-only) that the matrix had
been graded against a pre-fix snapshot for two read-path rows; see §7.

## 6. Intentional deviations and approval state

| row_id | surface | deviation | approval |
|---|---|---|---|
| R-010-3 | write options fields | write-through routing fields, no MVP read consumer | Task-016 scope (registered) |
| R-012-8 | `wait(offset)` | 60s bounded deadline, does not consult `throwIfKilled` | round-1 §6, deferred to Task 017 |
| R-014-12 | `readBigAt` (positioned) | registered structural deviation | Task-014 receipt exemption (signed) |
| R-014-14 | CACHED read-until (F-014-2) | segment-relative `setReadUntilPosition(getDownloadedSize())` vs CH absolute | accept-with-registration (round-2) |
| R-014-17 | O_DIRECT baseline | cache reads never inherit O_DIRECT/mmap, always `pread` | **D3 conditional — real O_DIRECT is a forward gate (Important)** |
| R-XC-5 | failpoints / TestValue | no-op shim; injected disk-failure paths not triggerable | D10 accepted for MVP |
| R-XC-6 | filesystem cache log | WRITE_THROUGH element has no MVP read consumer | Task-016 scope (registered) |

## 7. UNPROVEN rows

**None.** No row is silently `UNPROVEN`. The only real-world proof obligation is
R-014-17 (real O_DIRECT), which is a *registered conditional deviation*, not an
unproven contract.

Evidence-timeliness note (Phase C resolution): the matrix was authored against a
pre-fix snapshot and initially graded R-014-8 / R-014-13 / R-015-1 with stale
evidence, while two read-path defects were open. Both defects were fixed and
independently verified earlier this cycle and are present at HEAD `398426810`:

```text
01c007abe  SkipInt64 cross-segment desync — old SkipAcrossSegmentBoundary was false-green;
           replaced by 3 real cross-segment skip tests. (R-014-13)
006a15996  CACHED reader freeze on a still-downloading segment — crashed 6/22 TPC-H queries;
           fixed via cachedPrefixEndAbsolute / updateReadStateIfNeeded;
           CachedReaderRefillsWhenDownloadingSegmentGrows added. (R-014-8)
```

The MATCH grades hold on the post-fix HEAD; the matrix evidence references were
updated to the fix commits and the E2E test count corrected (17 → 21, verified
`grep -c TEST_F(FileCacheE2ETest` = 21). Counts and percentages are unchanged.

## 8. Velox extensions / over-port

| id | surface | classification |
|---|---|---|
| V-1 | `FileCacheManager` ownership layer wrapping `FileCacheFactory` | `VELOX_EXTENSION` (approved 013 platform seam) |
| V-2 | `QueryStatus`/`throwIfKilled` no-op shim | `VELOX_EXTENSION` (Task-017 forward scaffold, inert) |
| V-3 | seek/wrapper benchmarks + `CacheReadHarness` | `VELOX_EXTENSION` (Task-015 drivers) |
| V-4 | fail-fast rejection of `cache_on_write_operations` / overcommit | `VELOX_EXTENSION` (explicit unsupported-in-phase-1, fail-close) |
| V-5 | `TpchAbBenchmark`/`AbBenchmark*` skeleton | `OVER_PORT` (benign; benchmark-only, no correctness risk) |

None counted in the CH denominator. V-4 is fail-close (rejects unsupported
settings, does not silently no-op). V-5 is benign benchmark scaffolding.

## 9. Required task reopen list

**None for correctness.** Zero `MISSING`, zero `UNPROVEN`, zero unapproved
deviations at HEAD `398426810`. The two read-path defects that would have
required a reopen (SkipInt64, CACHED refill) were already fixed and verified this
cycle. Outstanding obligations are forward gates, not reopens (see §10-11).

## 10. Task-017 / 018 design inputs

- Real `ProfileEvents`/`CurrentMetrics` counters and timers (B1/B2 surface is
  present and in-scope; the real counters are Task 017).
- `wait()` in-progress query cancellation via `throwIfKilled` (R-012-8).
- F-CALLERID diagnostic `None:<threadname>:<tid>` and SD8 recursive-mutex final
  resolution (Task 017).
- Gluten `VeloxBackend` ownership + Builder integration reusing the Task-018a
  `registerFileCacheBufferedInputBuilder` seam (Tasks 018-019).

## 11. Forward gates

- **Real kernel `O_DIRECT` (R-014-17 / D3) — mandatory pre-release gate.** Prove
  cache read/write under real `O_DIRECT`, not strict-mock/local-file logic
  coverage.
- Structured errno **producer** (round-1 §3) — the FileCache concrete writer must
  produce real POSIX errno; current `LocalWriteFile` only converts to text. Real
  `ENOSPC`/`EDQUOT` evidence required pre-release. (The errno *consumer* path in
  `FileSegment::write` is already proven.)
- Write-through consumer (Task 016) for R-010-3 / R-XC-6.

## 12. Final verdict

```text
PARITY_ACCEPTED_WITH_DECISIONS
```

Rationale: semantic parity 57/64 (89.1%) and accepted coverage 64/64 (100%) with
zero `MISSING`, zero `UNPROVEN`, zero unapproved deviations, zero Critical
findings, and all 7 deviations signed. The verdict is `WITH_DECISIONS` rather
than plain `ACCEPTED` solely because of the outstanding user-facing ratification
items in `003-015-parity-decisions-needed.md` — chiefly the real-O_DIRECT forward
gate (Important) and the absence of an independent Task-015 decision record. None
of these is a correctness defect in the accepted 003-015 implementation.

## Artifacts

```text
port/task/fullreview/root-oss/4/003-015-ch-parity-audit.md            (this file)
port/task/fullreview/root-oss/4/003-015-parity-decisions-needed.md    (user-decision items)
port/task/fullreview/root-oss/4/003-015-parity-phase-c-review.md      (independent review + resolution)
port/task/fullreview/root-oss/4/evidence/003-015-ch-consumer-contract-ledger.md   (Phase A, 64 rows)
port/task/fullreview/root-oss/4/evidence/003-015-velox-parity-matrix.md           (Phase B, 64 rows)
```
