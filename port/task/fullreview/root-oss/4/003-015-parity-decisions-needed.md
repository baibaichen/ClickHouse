# Tasks 003-015 Parity — Decisions Needed

Only items that require a user choice. Each row-level fact is already backed by
`evidence/003-015-velox-parity-matrix.md`. Nothing here is a correctness defect
in the accepted implementation; these are ratification / forward-gate choices.

---

## PD-1 — Real kernel `O_DIRECT` for the cache read path (Important)

```text
decision_id: PD-1
source rows:  R-014-17 (D3)
```

**Plain-language question:** Cache reads currently go through the OS page cache
via local-FS `pread`; they never use real kernel `O_DIRECT`. CH's real caller can
run with direct I/O. Is page-cache-only serving an *accepted permanent* behavior
for this port, or must real-kernel `O_DIRECT` be proven before production?

- **Option A (recommended):** Keep page-cache-only for the MVP; make real
  `O_DIRECT` a **mandatory pre-release gate** (prove read/write under real
  `O_DIRECT`, not strict-mock coverage). — *Matches the existing D3 disposition.*
- **Option B:** Require real `O_DIRECT` integration + proof now, before declaring
  003-015 parity closed.

**Review recommendation:** A. The deviation is already registered; strict-mock
tests give logic coverage; blocking MVP parity on kernel-level DIO is
disproportionate when it is cleanly deferrable.

**Risk of A:** a real-DIO-only defect (alignment, short pread under O_DIRECT)
stays undiscovered until the pre-release gate runs.
**Risk of B:** delays MVP sign-off for a gate that is well-understood and
independently schedulable.

---

## PD-2 — Task-015 acceptance provenance (Important)

```text
decision_id: PD-2
source rows:  R-015-1, R-015-2
```

**Plain-language question:** There is **no independent Task-015 decision file** on
this machine. R-015-1/2 are graded `MATCH` because they are E2E validation of
already-approved 003-014 behavior (empty production diff, no new
guarantee-changing code), with provenance drawn only from the Task-015
contract + receipt. Is that sufficient to ratify the MVP acceptance gate, or do
you want a standalone Task-015 decision record?

- **Option A (recommended):** Accept the Task-015 contract + receipt as sufficient
  provenance; record this audit as the ratification.
- **Option B:** Author a standalone `015-review-decisions.md` to formally close the
  MVP gate the way rounds 1 and 2 were closed.

**Review recommendation:** A. Nothing in 015 introduces new behavior to approve;
it only exercises signed 003-014 contracts. A dedicated record is bookkeeping, not
new risk assessment.

**Risk of A:** the 015 gate lacks a decision file symmetric with rounds 1/2 —
future auditors must infer approval from this audit.
**Risk of B:** minor effort for a record that restates existing sign-offs.

---

## PD-3 — `wait()` query cancellation deferral (confirm)

```text
decision_id: PD-3
source rows:  R-012-8
```

**Plain-language question:** `FileSegment::wait` has a 60s bounded deadline but
does not consult `throwIfKilled`, so an in-`wait` query cancel is not honored
until the deadline. Round-1 §6 accepted deferring this to Task 017. Confirm this
still holds for MVP?

- **Option A (recommended):** Confirm deferral to Task 017; the 60s deadline
  prevents indefinite hang.
- **Option B:** Require in-`wait` `throwIfKilled` now.

**Review recommendation:** A — already the round-1 disposition; unchanged.
**Risk of A:** worst-case 60s cancel latency on a downloading segment.
**Risk of B:** pulls Task-017 cancellation work forward with no MVP driver.

---

## PD-4 — Write-through parity is Task-016 scope (confirm)

```text
decision_id: PD-4
source rows:  R-010-3, R-XC-6
```

**Plain-language question:** Write-path routing fields and the `WRITE_THROUGH`
cache-log element have no MVP read consumer. Confirm write-through parity is
entirely Task-016 scope and not required for 003-015 MVP sign-off?

- **Option A (recommended):** Confirm Task-016 scope; excluded from the 003-015
  denominator.
- **Option B:** Pull write-through into the current parity requirement.

**Review recommendation:** A — Task 016 is the accepted owner; no MVP read path
exercises write-through.
**Risk of A:** none for the read MVP.
**Risk of B:** expands scope into deferred write-consumer work.

---

## PD-5 — `readBigAt` / CACHED read-until deviations final (confirm)

```text
decision_id: PD-5
source rows:  R-014-12, R-014-14 (F-014-2)
```

**Plain-language question:** Both are registered structural deviations
(`readBigAt` positioned read; CACHED `setReadUntilPosition(getDownloadedSize())`
segment-relative vs CH absolute, F-014-2 accept-with-registration). Confirm the
ledger entries are final and no further proof is owed?

- **Option A (recommended):** Confirm final; both signed accept-with-registration.
- **Option B:** Request additional proof for either.

**Review recommendation:** A — both carry existing sign-off; behavior verified via
the read-path tests (including the post-fix refill/skip families).
**Risk of A:** none identified beyond the registered deviation.
**Risk of B:** re-opens signed decisions without new evidence of a problem.

---

## Non-decision reminders (no user choice needed, listed for completeness)

- Structured errno **producer** remains a pre-release gate (round-1 §3): the
  concrete writer must emit real POSIX errno; current `LocalWriteFile` converts to
  text only. The errno *consumer* path is already proven.
- Task-017 real counters (B1/B2 surface present, real `ProfileEvents`/
  `CurrentMetrics` deferred), F-CALLERID diagnostic, SD8 recursive-mutex: forward
  work, already tracked.
- Gluten (Tasks 018-019): out of scope.
