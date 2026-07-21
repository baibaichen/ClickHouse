# Tasks 003–015 Parity Audit — Decisions Needed (Review round 4)

This file contains **only** items that require an explicit user choice. It
does not repeat narrative summary, completed/approved decisions, or findings
that have only one mandatory follow-up action. See
[`003-015-ch-parity-audit.md`](003-015-ch-parity-audit.md) for the full
synthesis, counts, and verdict.

Excluded from this file (already resolved or not genuine user choices):
Task-015 `D1`/`D2`/`D6` (already approved), Task-015 `D3`/`D4` real-`O_DIRECT`
gate (already conditional with one mandatory evidence path — real kernel
`O_DIRECT`), `F-CALLERID`/scheduler-recursive-mutex (already deferred to
Task 017), Tasks 016–019 scope (already decided), and automatic engineering
follow-ups (structured errno producer, `StatusFile` unclean-restart
diagnostics — these have exactly one required action, not a user choice
between options).

---

## R2-D1 — Task-009 `ShardedMap.h` public-header registration

```text
decision_id:      R2-D1
source rows:      CC-D1-SHARDEDMAP-HDR (matrix §5B); R-WITHSHARD-01, R-BUCKETMAP-01 (matrix §5, context only — unaffected either way)
```

**Plain-language question:** Should `ShardedMap.h` be registered as a
public `FILE_SET` header in the non-mono `velox_ch_filecache` build (a
build-packaging change with no runtime-behavior effect), so that non-mono
consumers can use the standalone library without reaching into internal
source paths?

**Option A:** Approve — keep `ShardedMap.h` registered as a public
non-mono header (current implementation).

**Option B:** Reject/revert — leave `ShardedMap.h` unregistered.

**Review recommendation:** Approve. This is packaging-only, has no runtime
effect, and matches the pattern of every other FileCache public header.

**Risk of each option:**
- Option A: none identified — build-only change.
- Option B: non-mono consumers cannot use the standalone `ShardedMap`
  surface without reaching into internal source paths, inconsistent with
  every other public FileCache header.

---

## R2-D2 — Task-012 `call_once` → mutex+flag replacement

```text
decision_id:      R2-D2
source rows:      L-CALLONCE-01 (UNPROVEN), D-INIT-01 (UNPROVEN, partial)
```

**Plain-language question:** Should `FileCache::initialize()`'s one-time
guard remain a hand-rolled `std::mutex` + completed-flag (current Velox
code) instead of the CH-mapped `std::call_once`, given that in the current
statically linked toolchain an exception unwinding through the
`pthread_once` path aborted the process instead of leaving the once-flag
retryable?

**Option A:** Approve the mutex+flag replacement as documented — it
preserves the CH-required retry-on-throw guarantee even though it deviates
from the literal `std::call_once` primitive mapping.

**Option B:** Reject — require investigating and fixing the underlying
static-link/unwind-through-`pthread_once` toolchain issue, and restore
`std::call_once`.

**Review recommendation:** Approve. The replacement preserves the
consumer-visible retry-on-throw guarantee; the deviation is an internal
primitive choice, not a consumer-visible behavior change, and the
toolchain investigation in Option B has open-ended cost with no
guarantee-visible benefit.

**Risk of each option:**
- Option A: a second one-time-init primitive now exists alongside
  remaining `std::call_once` usages elsewhere in the port — a small
  consistency/maintenance cost.
- Option B: an open-ended toolchain investigation may block Task-012
  acceptance indefinitely over a purely internal primitive choice.

---

## R2-D3 — Task-012 failpoint seams via `TestValue`, production `velox_test_util` link

```text
decision_id:      R2-D3
source rows:      L-FP-EVICTSEG-01, L-FP-EVICTPUSH-01 (both INTENTIONAL_DEVIATION, D10-approved-absent); CC-TESTUTIL-LINK (matrix §5B)
```

**Plain-language question:** Should the **production**
`velox_ch_filecache_core` library keep a permanent `PUBLIC` link dependency
on `velox_test_util` (to host release-inert `TestValue::adjust` failpoint
seams used for resize/eviction/shutdown mutation testing), given that
`TestValue::adjust` is compiled out under `NDEBUG` but the link dependency
itself remains in the production library?

**Option A:** Approve — keep the current release-inert `TestValue::adjust`
seams and the `velox_test_util` production link; behavior is a no-op
outside tests.

**Option B:** Reject — remove the production-library dependency on
`velox_test_util` now, and implement any needed failpoint surface entirely
within Task 017 without touching the production link graph.

**Review recommendation:** Approve, but require the production→test-util
link dependency to be **explicitly acknowledged and accepted** rather than
silently carried forward — it is an unusual coupling of test
infrastructure into a production library, even though it is behavior-inert
in release builds.

**Risk of each option:**
- Option A: a production library retains a build-time dependency on
  test-support code; if this pattern grows it complicates release
  packaging or risks accidental symbol reliance.
- Option B: removes valuable mutation/rollback test coverage for
  eviction/resize/shutdown failure paths until Task 017 lands, and
  requires reworking already-passing tests.

---

## R2-D4 — Task-013 Manager mutation serialization and transactional reload

```text
decision_id:      R2-D4
source rows:      D-INIT-01, E-GETORCREATE-01, E-CREATE-01, E-UPDCFG-01 (all UNPROVEN)
```

**Plain-language question:** Should `FileCacheManager` lifecycle mutations
(create/reload/remove/clear/shutdown across multiple cache instances) be
serialized under one Manager-owned mutex, with new-cache
registration/pool-growth/initialization made a fail-close transaction that
rolls back only bindings introduced by a failed reload?

**Option A:** Approve — serialize mutations and make registration/pool
growth/init a fail-close transaction (current implementation).

**Option B:** Reject — require a more granular concurrent/versioned
registry design instead of full serialization.

**Review recommendation:** Approve. This is a safety-preserving fix for a
real cross-cache-deadlock and uninitialized-cache-after-partial-failure
risk, with no consumer-visible change to correct-usage call patterns. Four
denominator rows remain `UNPROVEN` pending this approval.

**Risk of each option:**
- Option A: reload/create/remove operations across different caches are
  serialized rather than concurrent — a potential (currently unmeasured)
  throughput cost under many simultaneous cache reconfigurations.
- Option B: leaves a known deadlock/uninitialized-cache defect unresolved
  while a more complex registry is designed, and keeps 4 rows `UNPROVEN`
  indefinitely.

---

## R2-D5 — Task-013 `OpenedFileCache` weak-bucket-state deleter

```text
decision_id:      R2-D5
source rows:      OpenedFileCache VELOX_EXTENSION inventory item (matrix §8); backs Manager-owned bounded lifetime (D-DEACT-01, E-CLEAR-01 context)
```

**Plain-language question:** Should the bounded-lifetime
`OpenedFileCache`'s file-handle deleter capture a **weak** reference to its
bucket's map/mutex state (closing the file without touching destroyed
cache state if the bucket is already gone), replacing the original
CH-style deleter that captured a **raw** bucket pointer and reproduced a
heap use-after-free (confirmed under ASan) when a handle outlived the
Manager?

**Option A:** Approve — keep the weak-bucket-state deleter (current
implementation); proven safe by an ASan mutation, preserves normal
weak-entry cleanup.

**Option B:** Reject — require every opened-file handle to co-own the
entire Manager/cache instead (stronger lifetime coupling).

**Review recommendation:** Approve. This is a proven memory-safety fix — a
real ASan-reproduced use-after-free is eliminated — with no behavior loss
in the common case; it should not remain unapproved given it protects
against a real defect.

**Risk of each option:**
- Option A: a late-closing handle silently cannot erase an
  already-destroyed bucket entry — harmless, a no-op on an already-freed
  structure.
- Option B: forces every opened-file handle to keep the entire
  Manager/cache alive, reintroducing the unbounded-lifetime problem this
  change was meant to fix.

---

## R2-D6 — Tasks 007/014 reader detach and query-pool lifetime

```text
decision_id:      R2-D6
source rows:      P-RB-SETDETACH-01, G-NEXTIMPL-01 (both UNPROVEN)
```

**Plain-language question:** Should the read-buffer's detach-on-`set(nullptr,
0)` fully clear all internal/working views — lazily restoring owned
storage only on a later foreground read, and releasing the
query-pool-owned buffer before background handoff — replacing the original
implementation that restored its own internal buffer and retained a
query-pool-charged `BufferPtr` that a background worker could touch after
the query pool was destroyed (a reproduced use-after-free)?

**Option A:** Approve — adopt the corrected detach/handoff/lifetime model
(current implementation); matches CH detach semantics and removes the
cross-thread pool-lifetime hazard.

**Option B:** Reject — require every handed-off reader to keep its
original query-pool-owned buffer alive across background handoff.

**Review recommendation:** Approve. This closes a reproduced (not
hypothetical) use-after-free and aligns with CH's detach contract. Both
`P-RB-SETDETACH-01` and `G-NEXTIMPL-01` remain `UNPROVEN` until this is
approved.

**Risk of each option:**
- Option A: a background-handed-off reader cannot reuse its original owned
  buffer (must use a Manager-pool-backed external buffer instead) —
  acceptable, since background reads already always use an external
  buffer.
- Option B: retains a proven use-after-free hazard in production.

---

## R2-D7 — Task-014 direct-I/O predownload and failure hardening

```text
decision_id:      R2-D7
source rows:      G-PREDL-01 (MATCH), G-ISCACHED-01 (EQUIVALENT, checkedAdd hardening), G-WRITECACHE-01 (MATCH, bypass-continue)
```

**Plain-language question:** Should the accepted Task-014 hardening be
kept — skip only the optional predownload optimization when its gap
cannot satisfy direct-I/O alignment (never silently falling back to
buffered I/O), use a shared overflow-checked `checkedAdd` for range
probes, and continue reading/caching later segments after a configured
disk-failure bypass on one segment?

**Option A:** Approve — keep all three fixes as documented (current
implementation; already reflected as `MATCH`/`EQUIVALENT` in the parity
matrix).

**Option B:** Reject — require an alternate design (e.g., always attempt
predownload with a buffered-I/O fallback, or unchecked-arithmetic range
probes).

**Review recommendation:** Approve. All three are fail-close/source-
correctness fixes with existing production-path test/mutation coverage;
they already back `MATCH`/`EQUIVALENT` rows, so approval mainly
regularizes bookkeeping rather than changing behavior.

**Risk of each option:**
- Option A: skipping predownload under misalignment slightly reduces an
  optimization's coverage for explicit direct-I/O sources (does not
  change returned bytes).
- Option B: reintroduces the original unaligned direct-I/O seek/read
  defect, or the unchecked-overflow range-probe defect, that this
  decision fixed.

---

## R2-D8 — Task-012 B4 mutation corrected to implemented ordering

```text
decision_id:      R2-D8
source rows:      B-COMPPART-01, B-EXTRDR-01, B-RESETRDR-01 (Section B ownership rows exercised by the corrected mutation; no production-code row changes)
```

**Plain-language question:** Should the B4 mutation test be corrected to
match the actually-implemented ownership-publish ordering
(`setDownloadedUnlocked` destroys `download_data` before publishing
`DOWNLOADED`; the partial-completion path retains the reader but stays
state-gated from extraction) instead of the originally-authored — and
unreachable — reversed-order expectation?

**Option A:** Approve — use the corrected partial-download mutation
(proving `reader_a.use_count() == 2`) that tests real ownership-sharing
without weakening any state gate.

**Option B:** Reject — keep pursuing the original mutation's premise and
treat the state machine itself as needing a behavior change to make the
original mutation reachable.

**Review recommendation:** Approve. The original mutation described a
state transition that cannot occur in the accepted implementation; the
corrected mutation tests the actual ownership-sharing invariant with no
production-code change.

**Risk of each option:**
- Option A: none identified — this is a test-correctness fix, not a
  behavior change.
- Option B: would require introducing a new, unneeded, unrequested
  production state-machine path solely to satisfy an incorrect test
  premise, adding needless complexity/risk.

---

## T015-D5 — Task-015 benchmark non-destructive fresh keys

```text
decision_id:      T015-D5
source rows:      Benchmark fresh-random-key freshness (OVER_PORT/extension, matrix §8; benchmarks/FileCacheSeekBenchmark.cpp)
```

**Plain-language question:** Should the Task-015 seek benchmark use
freshly generated random 128-bit cache keys for hit-setup/miss/bypass
reads (avoiding key collisions from persisted metadata across process
runs) instead of destructively deleting the user-supplied cache directory
before each run?

**Option A:** Approve — keep fresh-random-key generation (current
implementation); non-destructive to an arbitrary user-supplied path, and
preserves cold-miss semantics.

**Option B:** Reject/modify — require the benchmark to explicitly clear
the cache directory (or mandate a benchmark-dedicated directory) instead
of relying on key freshness to avoid collisions.

**Review recommendation:** Approve. The current approach avoids an unsafe
destructive operation on a flag-supplied path while still producing valid
cold-miss/hit measurements — the safer default for a benchmark tool.

**Risk of each option:**
- Option A: repeated benchmark runs accumulate unused cache entries in the
  target directory over time (disk growth), since old keys are never
  cleaned up.
- Option B: a destructive clear on a user-supplied path risks deleting
  unrelated data if the flag is misconfigured, and is not reversible.

---

## G-CACHEBUF-01 — Restore CH external-truncation self-heal, or approve the gap

```text
decision_id:      G-CACHEBUF-01
source rows:      G-CACHEBUF-01 (UNPROVEN/Important, matrix §6); real-O_DIRECT-adjacent forward-gate list (matrix §10)
```

**Plain-language question:** CH's read path (`getCacheReadBuffer`) detects
when an on-disk cache file has been externally truncated below its
recorded downloaded size, then warns, bypasses to the remote source, and
re-fetches. Velox's `createCacheReadBuffer` (`FileCache.cpp:395`) has **no
size-mismatch check at all** — a `FileSegment.cpp:1235-1246` comment
claims this behavior exists, but the code does not implement it. Should
this CH self-heal behavior be restored in Velox, or should the gap be
explicitly accepted as a permanent, approved intentional deviation?

**Option A:** Restore the CH self-heal — detect on-disk size < recorded
downloaded size at cache-file open, warn, bypass to the remote source, and
re-fetch (matches CH exactly; converts this row from `UNPROVEN` to
`MATCH`).

**Option B:** Explicitly approve the gap as an intentional deviation —
record the rationale for treating external truncation of cache files as
out-of-scope/acceptable risk in this deployment, and correct the stale
`FileSegment.cpp` comment that currently claims unimplemented behavior.

**Review recommendation:** Require restoring the CH self-heal (Option A).
External truncation of cache files (e.g., a concurrent process, a
disk-repair tool, or manual intervention) is a plausible real-world
failure mode that CH explicitly guards against; silently serving
truncated/incorrect data — or failing where CH would gracefully
bypass-and-refetch — is a correctness regression, not a cosmetic
difference.

**Risk of each option:**
- Option A: requires new code plus a focused test proving the
  warn/bypass/re-fetch path — a small, well-scoped implementation cost.
- Option B: leaves a real, currently-silent correctness gap in production
  (readers may get incorrect/truncated data, or an unhandled failure,
  from an externally truncated cache file where CH would recover), and
  leaves a misleading comment in the codebase asserting behavior that
  does not exist.

---

## SD4-EVIDENCE — F14 metadata-bucket no-reference-across-mutation proof

```text
decision_id:      SD4-EVIDENCE
source rows:      R-BUCKETMAP-01 (EQUIVALENT with evidence debt); root-oss/1 SD4
```

**Plain-language question:** `SD4` conditionally approved the Task-012 F14
metadata-bucket replacement **only if** no iterator, mapped-value
reference, or mapped-value address survives a bucket mutation. The port
currently relies on the by-construction argument that `shared_ptr<KeyMetadata>`
keeps the pointee stable even though the map slot itself is not stable,
with no dedicated test proving no reference/iterator/address survives a
mutation. Should this source-level stability argument be accepted as
sufficient proof of `SD4`, or should a dedicated focused test/mutation be
required first?

**Option A:** Accept the current source-level proof (the
`shared_ptr<KeyMetadata>` indirection is a structural guarantee
independent of any test) as sufficient to close `SD4`.

**Option B:** Require a dedicated focused test (e.g., capture a raw
iterator/reference/address before a forced bucket mutation/rehash and
assert it no longer aliases live state, or an equivalent ASan/mutation
probe) before treating `SD4` as closed.

**Review recommendation:** Require the focused evidence (Option B) unless
the user explicitly accepts the source-argument as sufficient. `SD4` was
approved *with* a proof condition, not unconditionally; that condition has
not yet been independently discharged by a test, and a container-primitive
swap is exactly the kind of guarantee-changing replacement the audit rules
treat as needing explicit proof rather than inference.

**Risk of each option:**
- Option A: `SD4`'s proof condition remains formally undischarged; a
  future refactor that invalidates the shared_ptr-indirection assumption
  would have no test to catch a real reference/iterator-survival bug.
- Option B: requires authoring and maintaining an additional targeted test
  for an internal container-implementation detail with no direct
  consumer-visible symptom today.

---

*11 decisions total: `R2-D1`–`R2-D8` (8), `T015-D5` (1), `G-CACHEBUF-01`
(1), `SD4-EVIDENCE` (1). Artifact:
`port/task/fullreview/root-oss/4/003-015-parity-decisions-needed.md`.
Read-only synthesis; no production source, CMake, tests, receipts, Git
state, Velox, or Gluten were modified.*
