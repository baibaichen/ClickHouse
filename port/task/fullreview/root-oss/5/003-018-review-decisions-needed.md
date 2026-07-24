# Tasks 003–018 Review 5 — Decisions Needed

```text
review_status:      accepted
review_scope:       Tasks 003-018
verdict_file:       port/task/fullreview/root-oss/5/003-018-whole-port-review.md
```

## Summary

**No decision blocks Review 5.** All items below are non-blocking forward debt
or informational records. The user does not need to decide any of them before
accepting Review 5 or before Task 017B written-spec review begins.

---

## D4 / D6 — Pending Non-blocking Forward Debt

```text
r2_d4_status:   pending — not yet decided
r2_d6_status:   pending — not yet decided
parity_rows:    D-INIT-01, E-GETORCREATE-01, E-CREATE-01, E-UPDCFG-01 (D4)
                P-RB-SETDETACH-01, G-NEXTIMPL-01 (D6)
denominator:    215 (six rows remain UNPROVEN in denominator — do not remove)
blocks_r5:      no (user decision 2026-07-24)
```

Per the binding user decision of 2026-07-24, `R2-D4` (Manager mutation
serialization / transactional reload) and `R2-D6` (reader detach / query-pool
lifetime) remain `pending`. Their six governed parity rows remain `UNPROVEN` in
the 215-row denominator. They are not reclassified, approved, rejected, or
removed by this review.

These rows represent real forward debt: the implementations are present and
correct-looking at source, but no focused serialization or lifetime tests cover
them because the governing user decisions are still outstanding. When the user is
ready to address them, each decision requires:

- R2-D4: a user approve/reject/modify on Manager mutation serialization
  semantics, followed by focused `mutation_mutex_`-range tests in
  `FileCacheManager.cpp:296-...,454-483`.
- R2-D6: a user approve/reject/modify on reader-detach / query-pool-lifetime
  semantics, followed by focused lifetime tests for
  `FileCacheInputStream.cpp:1029-1063`.

**No action required from the user before Task 017B written-spec review begins.**

---

## OpenedFileCache Wiring / Documentation Choice — Future Task 013, Non-blocking

```text
owner:          Task 013
blocks_r5:      no
blocks_017b:    no
```

`OpenedFileCache::get()` is never called on the production read path (Low-1 in
the verdict). Cache-file reads open a fresh `velox::ReadFile` via
`FileCache::createCacheReadBuffer` → `fs->openFileForRead`, while only the
invalidation callback `opened->remove(path)` is wired. The invalidation therefore
operates against an always-empty cache (dead in practice).

This is within the documented design latitude
(`port/2-file-cache/09-filecache-file-segment-design.md:254-256`) and no
accepted parity row claims read-path wiring. When Task 013 is next revisited, the
Controller should choose one of:

- Wire `OpenedFileCache::get()` into `createCacheReadBuffer` to deliver the CH
  cross-reader FD-sharing optimization (parity gain).
- Document the component and invalidation path as intentionally read-unwired, so
  the dead complexity is clearly distinguished from active behavior.

**Neither choice is required before Review 5 acceptance or Task 017B. This is a
future Task 013 follow-up.**

---

## SD4 Focused Evidence — Non-blocking Follow-up

```text
row:            R-BUCKETMAP-01
current_status: EQUIVALENT (resting on by-construction shared_ptr stability)
blocks_r5:      no
```

No focused test proves that no iterator / mapped-value reference / address
survives an F14 `MetadataBucket` mutation (`MetadataTest.cpp` has no SD4 probe).
`R-BUCKETMAP-01` stays `EQUIVALENT` on the by-construction
`shared_ptr<KeyMetadata>` stability argument.

When capacity permits, a no-reference-across-mutation probe in `MetadataTest.cpp`
would close this evidence debt. **Not a required action before any near-term task.**

---

## `bytes_read` Leaf vs Stream Naming — Documented Intentional, Non-blocking

```text
row:            none (Review-5 Low-3 observation)
blocks_r5:      no
```

`AbBenchmarkBase.cpp:345-354` fills `bytes_read` from `TaskStats` leaf-scan
`rawInputBytes` (physical scan I/O), not from the stream-level `rawBytesRead`.
This is intentional (scan-level bytes reflect true storage I/O) and is recorded
in the integrated contract ledger's CSV field-source trace.

The naming is slightly misleading in isolation but is accurate to its semantics.
No action required. Documented here for completeness in case a future reviewer
questions the metric source.
