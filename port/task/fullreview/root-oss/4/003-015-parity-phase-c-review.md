# Phase C — Independent verification of the 003-015 CH→Velox FileCache parity audit

READ-ONLY. I attempted to BREAK the Phase-A ledger and Phase-B matrix, not ratify them.
Oracle = ClickHouse `ch-filecache2`. Port = Velox `filecache2`, HEAD `398426810`.

## (a) Verdict summary

The matrix's *classification methodology* (denominator, deviation provenance, guarantee rule)
is sound and the arithmetic is correct. However, the matrix is **stale relative to its own
declared HEAD** and this staleness produced **false-green MATCH grades on two high-risk
read-path rows**. The matrix header stamps HEAD `398426810`, but at that HEAD the Velox tree
contains two Task-014 read-path *bug fixes* committed AFTER the matrix/round-2 acceptance
(`01c007abe`, `006a15996`). The matrix graded the affected rows MATCH/green while the code was
actually broken (one crashed 6/22 TPC-H queries; one had a false-green test). The rows are
now correct at HEAD, but they were graded MATCH on unverified/wrong evidence — a process defect
that inflated confidence, even though the end-state at HEAD happens to be MATCH.

- **Critical findings: 1** (matrix evidence provably predates HEAD; two read-path bugs graded green).
- **Important findings: 2** (R-015-1 test-count claim contradicts HEAD; R-014-8 continuation guarantee was absent when graded MATCH).

Net effect on the *final* denominator: the 64/57/7 counts remain **numerically correct for
HEAD `398426810`** because the fixes restore MATCH. The problem is evidentiary integrity, not
the end tally.

## (b) Critical findings

### C1 — Matrix evidence predates its declared HEAD; two read-path defects graded MATCH/green
- **Rows:** R-014-8, R-014-13, R-015-1, R-015-2 (read-path state machine + seek/skip coordinates — the exact high-risk set Phase C is told to attack).
- **What's wrong:** The matrix header claims Velox HEAD `398426810` and grades these rows MATCH with "E2E green (17 tests)" / "seek benchmark green". But `git log` shows two defect fixes committed AFTER the round-2 acceptance baseline (`bc78ef541`) and after the matrix was written:
  - `01c007abe` "Fix `FileCacheInputStream::SkipInt64` cross-segment desync" — `SkipInt64` called `Next()` then rolled `position_` back; across a segment boundary the irreversible `completeCurrentSegmentAndAdvance()` had already run, desyncing `position_` from the held segment. Commit message states the pre-existing `SkipAcrossSegmentBoundary` test **was false-green** (it read the whole first segment before skipping, so the skip started AT the boundary). This is R-015-2 / R-014-13 (cross-segment seek/skip coordinates).
  - `006a15996` "Fix CACHED reader freeze on a still-downloading segment" — the CACHED branch bounded the reader to a transient `getDownloadedSize()` (e.g. first 1 MiB flush), froze `readUntil_`, and aborted the DWIO reader with "Reading past end", **crashing 6 of 22 TPC-H queries (q2/11/15/17/20/21) on the filecache engine**. This is R-014-8 (segment read + reconcile) and the read-while-downloading continuation the row claims MATCH for.
- **Proof it changes classification:** At HEAD the E2E suite is **21 tests** (`grep -c "^TEST_F(FileCacheE2ETest"` = 21), but the matrix R-015-1 cites "**17 tests**" and R-015-2 cites the benchmark green. 17 ≠ 21; the SkipInt64 fix added the corrected `SkipAcrossSegmentBoundary`-family tests and the freeze fix added `CachedReaderRefillsWhenDownloadingSegmentGrows`. The matrix was therefore authored against a snapshot before these fixes, i.e. it certified R-014-8/R-015-1/R-015-2 as MATCH/green while the read-while-downloading continuation was broken and the skip test was false-green. A MATCH grade backed by a false-green test and a since-crashing path is not a verified MATCH.
- **Correct disposition:** rows are MATCH *at HEAD* (fixes verified present: `updateReadStateIfNeeded`/`cachedPrefixEndAbsolute` at `FileCacheInputStream.cpp:702,725`, `invalidateAndReposition` reused by `SkipInt64` and `seekToPosition`). But the matrix must be re-stamped to the true HEAD it was evaluated at, or re-evaluated at `398426810` with the 21-test suite, and the R-015-1 "17 tests" evidence corrected. Grading MATCH on stale/false-green evidence is the finding.

## (c) Important findings

### I1 — R-015-1 test-evidence figure is factually wrong at declared HEAD
- **Row:** R-015-1. Matrix cites `FileCacheE2ETest (17 tests: MissFillHit, ColdMissFillThenHit, ...)`. At HEAD `398426810` there are 21 `TEST_F(FileCacheE2ETest ...)`. The cited evidence count does not match the tree the matrix claims to describe. Either the HEAD stamp or the test list is wrong; both cannot be true. Because R-015-1 is the MVP acceptance-gate row, its evidence must be exact.

### I2 — R-014-8 "read-while-downloading continuation" guarantee was not actually present when graded MATCH
- **Row:** R-014-8. The CH contract (ledger R-014-8, and the continuation semantics shared with R-012-8 `wait`) requires that a reader consuming a still-`DOWNLOADING` segment continues past the first flushed chunk. `006a15996`'s message states the pre-fix Velox froze at the first 1 MiB chunk and aborted — i.e. the continuation guarantee the row grades MATCH was absent. Now satisfied via `cachedPrefixEndAbsolute`/`updateReadStateIfNeeded` (`FileCacheInputStream.cpp:499-505,706-726`), explicitly "mirroring CH nextImplStep". Row is MATCH at HEAD but was mis-graded when written.

## (d) Rows I independently CONFIRMED (spot-checked both CH and Velox citations)

- **R-014-5** (truncation self-heal, load-bearing): faithful port. Velox `FileCacheInputStream.cpp:174-205` mirrors CH `CachedOnDiskReadBufferFromFile.cpp:448-477` exactly — state-observed-BEFORE-size ordering (`downloadState` at :187 then `cacheFileSize` at :192), `trustSizeFromFilename` gate = `hasSizeInFileName && state∈{DOWNLOADED,DETACHED}`, `nullptr` bypass (NOT LOGICAL_ERROR/detach), empty-file guard. CH comment's priority-lock rationale for not-removing is preserved in intent. MATCH confirmed.
- **R-012-4 / R-XC-3** (write-into-reserved + ENOSPC/EDQUOT reconcile): Velox `FileSegment.cpp:486-514` catches `FileCacheErrnoException`, reads `getErrno()`, branches on 28/122, and asserts `chassert(downloaded_size <= physical_size && physical_size <= reserved_size)` at :508 with `downloaded_size = physical_size` reconcile. Byte-faithful to round-1 §3 signed errno contract and CH `FileSegment.cpp:525`. MATCH confirmed.
- **R-XC-3** invariant `downloaded<=physical<=reserved` — confirmed enforced at the mutation site.

## (e) Denominator / count recheck

- Rows: 8+3+23+4+18+2+6 = **64** ✓ (matches ledger §Counts and matrix total).
- MATCH: 7+3+22+4+15+2+4 = **57** ✓.
- INTENTIONAL_DEVIATION: R-010-3, R-012-8, R-014-12, R-014-14, R-014-17, R-XC-5, R-XC-6 = **7** ✓. 57+7 = 64, 0 MISSING, 0 UNPROVEN, 0 EQUIVALENT.
- No VELOX_EXTENSION (V-1..V-5) double-counted; they are excluded (5 rows). No duplicate row_id.
- Semantic parity = 57/64 = 89.06% → **89.1%** ✓ (correctly rounded).
- Accepted coverage = 64/64 = **100%** ✓.
- **All counts and both percentages are arithmetically correct as written.** The defect (C1/I1/I2)
  is evidentiary staleness, not a miscount: the fixes restore the same MATCH grades, so the tally is
  unchanged. But the MATCH grades for R-014-8/R-014-13/R-015-1/R-015-2 rest on evidence that did not
  hold at the moment they were assigned.

## (f) Weak approval provenance

- **R-015-1, R-015-2 (015 rows):** matrix itself flags "no independent Task-015 decision file exists".
  I confirm: `root-oss/1/003-010-review-decisions.md` and `fullview/home-chang/2/011-014-review-decisions.md`
  contain no 015 sign-off; round-2 §6 explicitly says Task 015 "需要用户明确批准才能开始". Provenance is
  "015 contract/receipt only" as the matrix states — acceptable as non-guarantee-changing, but the
  test-count error (I1) means the 015 acceptance evidence is not clean and should be re-run at HEAD.
- All 7 INTENTIONAL_DEVIATION rows DO cite a real signed decision (verified: R-010-3 ledger fwd-obligation
  = Task 016; R-012-8 round-1 §6 wait/throwIfKilled=017; R-014-12 & R-014-14 = round-2 §3 readBigAt
  exclusion + §7/F-014-2 registration [round-2 body confirms F-014-2 as the *un*-revoked one; the
  segment-relative-vs-absolute item §3 was the REVOKED misjudgment — the matrix correctly cites the
  registered F-014-2, not the revoked one]; R-014-17 D3 conditional O_DIRECT gate; R-XC-5 D10 round-1 §7;
  R-XC-6 round-1 §6). No unregistered guarantee-changing replacement was found graded MATCH.

## Confirmation

Read-only. No production, Gluten, or configuration file was modified. The only file written is this
review under `root-oss/4/`.

## Resolution (post-coordinator, evidence-only, no production change)

C1/I1/I2 were all **audit-document evidence lag**, not production defects. The two underlying bugs
were already fixed and verified in this round, and are present at the declared HEAD `398426810`:
`01c007abe` (`SkipInt64` cross-segment desync; old `SkipAcrossSegmentBoundary` false-green retired,
3 real cross-segment skip tests added) and `006a15996` (CACHED reader freeze on a still-downloading
segment; had crashed 6/22 TPC-H queries q2/11/15/17/20/21; `CachedReaderRefillsWhenDownloadingSegmentGrows`
added). Per audit protocol I refreshed the matrix evidence columns only — no ledger contract row and
no production code was touched.

Matrix (`evidence/003-015-velox-parity-matrix.md`) rows updated:
- **R-014-8** — added `006a15996` continuation cite (`cachedPrefixEndAbsolute`/`updateReadStateIfNeeded`,
  `FileCacheInputStream.cpp:499-505,706-726`), the refill RED test, and an evidence-timeliness note that
  the green basis moved from the pre-fix snapshot to fix commit `006a15996`. Status stays MATCH.
- **R-014-13** — added `01c007abe` cite (shared `invalidateAndReposition` slow-path), the 3 real
  cross-segment skip tests (old false-green deprecated), the skip RED, and an evidence-timeliness note.
  Status stays MATCH (classification unchanged; F-014-2 remains on R-014-14).
- **R-015-1** — corrected "17 tests" to the actual **21** `TEST_F(FileCacheE2ETest ...)` at HEAD
  (verified via `grep -c` on `velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp`), cited the test file, and
  added an evidence-timeliness note.
- Added a top-of-file evidence-timeliness note summarizing the refresh and affirming status values and
  the 64/57/7 counts are unchanged.

Re-review after the refresh:
- **C1** — resolved: the two rows now cite the fix commits and their falsifiable RED tests instead of a
  stale/false-green snapshot; the green basis is explicitly dated to the post-fix HEAD. No longer Critical.
- **I1** — resolved: R-015-1 evidence now states 21 tests matching HEAD `398426810`; the contradiction is gone.
- **I2** — resolved: R-014-8 now cites the read-while-downloading continuation implementation and its RED,
  so the previously-absent guarantee is documented as present at HEAD.

Counts re-verified unchanged: 64 rows, 57 MATCH, 7 INTENTIONAL_DEVIATION; semantic 57/64 = 89.1%;
accepted coverage 64/64 = 100%. All fixes restore the same MATCH grades, so the tally is untouched.

**Residual after resolution: zero Critical, zero Important.**

Read-only re-confirmed: no production or Gluten file touched; only files under
`/home/chang/SourceCode/ClickHouse/port/task/fullreview/root-oss/4/` were written (this review and the
matrix evidence file).
