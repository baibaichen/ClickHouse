# Full Review (Round 2) — Task 011: Priority & Eviction

Verdict: **ACCEPT** (implementation is a faithful §3 literal translation) with **two open findings**:
one CONFIRMED test-coverage hole (F-011-T) and one PLAUSIBLE over-port to adjudicate (F-011-O,
= ledger O-011). No behavioral drift, no structural deviation, no unregistered container swap found.

## 0. Scope & Inputs

- CH baseline: `/home/chang/SourceCode/ClickHouse/src/Interpreters/FileCache/` —
  `IFileCachePriority.{h,cpp}`, `LRUFileCachePriority.{h,cpp}`, `SLRUFileCachePriority.{h,cpp}`,
  `SplitFileCachePriority.{h,cpp}`, `EvictionCandidates.{h,cpp}`, `CacheUsage.h`.
- Velox port: `/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/` (same base names)
  + `tests/PriorityEvictionTest.cpp`.
- Read in full: all 12 source files above (both sides), the test, the round-2 ledger
  (`.../2/011-014-consumer-contract-ledger.md`), the round-1 decisions
  (`.../fullreview/root-oss/1/003-010-review-decisions.md`), and `port/task/011-...md`.
- Method: every contract re-derived from CH source line:line; receipts and tests NOT trusted as
  behavioral truth. SLRU (37k each) diffed by a dedicated subagent against all 10 load-bearing
  invariants; all other files diffed directly here.
- Not covered: `FileCache.cpp` reserve driver (Task 012), `Metadata`/`FileSegment` (012). This
  shard judges only the six Task-011 files + how they are exercised.

## 1. Per-file fidelity verdicts

| File | Verdict | Notes |
|---|---|---|
| `IFileCachePriority.h/.cpp` | matches | Line-for-line. 7-state machine, all flag transitions + lock-gating, `Entry`, `Iterator`, `HoldSpace`, `removeEntries`, `EvictionCursor`, `InvalidatedEntryInfo` all present. `magic_enum::enum_name`→explicit `stateName`/`typeName` switch (approved). `friend OvercommitFileCachePriority` correctly dropped (overcommit excluded). |
| `LRUFileCachePriority.h/.cpp` | matches | `LRUQueue = std::list<EntryPtr>` preserved; `splice` (`:651,727,862`), both eviction cursors (`reserve_eviction_pos`/`background_eviction_pos`) under a **separate** `eviction_pos_mutex`, `moveEvictionPosIfEqual`, `invalidated_refs = std::deque` + atomic count, lazy iterator-safe `removeInvalidatedEntries`, zero-size not counted (`add`/`iterateImpl Active: size>0`), PreActive non-evictable — all faithful. RNG `pcg64/randomSeed`→`folly::Random`/`ThreadLocalPRNG` (D-011-2). `LockMemoryExceptionInThread` omitted (D-011-3). TSA macros preserved (D-011-7). |
| `SLRUFileCachePriority.h/.cpp` | matches | Subagent-confirmed on all 10 invariants: `SLRUIterator{entry_mutex, is_protected atomic, weak_ptr<Entry>}`; `setIterator` updates `entry` under `entry_mutex` then `setActiveFlag` (PreActive→Active atomic with pointer update, CH `:906-927` ≡ V `:913-934`); two queues not flattened; `addForRestore` routes `SLRU_Protected`→protected; promotion gated on `is_space_reservation_complete`; downgrade splice; two-queue eviction ordering; lock order intact. `assert_cast`→`dynamic_cast`+`VELOX_CHECK` (D-011-6); bernoulli-in-failpoint no-op (D-011-5). |
| `SplitFileCachePriority.h/.cpp` | matches | Four limits (data/system × size/elements), System-resize-throw rollback of Data limits (`:155-165`), Data-first total-cleanup drain with `min(requested,current)` (`:256-271`), `SplitIterator::getType`→`SplitCache_Data/System`, `getNestedOrThis()` unwrap to inner SLRU type. Only cosmetic: `std::to_underlying`→`static_cast<std::underlying_type_t>` (C++20 portability). |
| `EvictionCandidates.h/.cpp` | matches | Hold-space split faithful: `evict()` runs with **no cache lock**, `removeFileSegment(..., invalidate_queue_entry=false)`, deferred `queue_entries_to_invalidate`, `afterEvictState` invalidates under state lock (H-011-b **closed**). `afterEvictWrite` before `afterEvictState`. `removeQueueEntries` captures `original_queue_types[candidate.get()]` via `getNestedOrThis()`. dtor re-invalidate / `resetEvictingFlag` gated on `removed_queue_entries`. `FailPoint`/`ProfileEvent`/callback-swallow all present. |
| `CacheUsage.h` | matches (minus overcommit metric) | `OvercommitUsers` metric member dropped per B2. `CacheUsageStatGuard` (`std::mutex`), base no-op hooks, `usage`-after-`priority` destruction order all preserved. See F-011-O. |

## 2. Mandatory checklist results

- **Overload completeness**: complete. `add`/`addForRestore`, both `canFit` overloads,
  `collectEvictionInfo`/`collectEvictionInfoForResize`/`collectCandidatesForEviction`,
  `tryIncreasePriority`, `iterate`, `removeEntries` (static), full `Iterator` surface
  (`getEntry`/`incrementSize`/`decrementSize`/`isValid`/`remove`/`invalidate`/
  `invalidateBeforeRemove`/`getType`/`getNestedOrThis`/`check`). None dropped.
- **Ordering/priority**: zero-size entry counts neither bytes nor elements until first positive
  `incrementSize` (`LRU add :162`, `iterateImpl Active :347`); reserve vs background cursors
  independent (`evictionPos` switch, separate mutex); SLRU probationary/protected + PreActive +
  `addForRestore`; Split four-limit + rollback. All present.
- **Lazy iterator-safe invalidation**: `invalidate()` sets atomic + leaves entry in queue;
  `removeInvalidatedEntries` acquires write lock lazily, skips stale/expired refs without touching
  their iterators. Faithful.
- **Error-propagation + cleanup**: `evict()` per-candidate try/catch → `failed_candidates` +
  `FilesystemCacheFailedEvictionCandidates` + `LOG_ERROR`; on-evict callback exception swallowed;
  dtor consistency restore. Faithful. `throwFileCacheException` replaces `Exception(ErrorCodes)`
  (approved) — message text preserved verbatim.
- **§3 structure**: `std::list` splice/iterator stability preserved (SD-011-3, SD5). No F14 swap on
  any load-bearing container. F14 appears only where CH itself used `absl::flat_hash_*`
  (`EvictionInfo : F14FastMap<QueueID, unique_ptr>`, `candidates : F14FastMap<FileCacheKey,
  KeyCandidates>`, `kept_alive_cache_usage : F14FastSet<shared_ptr>`) — this is the **approved**
  absl→folly substitution (SD2), not a new deviation. `original_queue_types` correctly stays
  `std::unordered_map` (SD-011-1). SD1 invariant ("no mapped-value reference survives a mutation")
  **holds**: `EvictionInfo::get` returns `const&` into a `unique_ptr` value (pointee stable across
  rehash); `candidates` values are copied/moved out, never aliased across an insert; `takeKeptAlive`
  copies `shared_ptr`s then clears source. Verified by reading every mutation site.

## 3. Findings

### F-011-T — Test-coverage hole (CONFIRMED, severity: medium)
`tests/PriorityEvictionTest.cpp` exercises **only empty-state getters and RAII hold accounting**:
`getType`/`getSizeLimit`/`canFit` on empty queues, `HoldSpace` reserve/release, `SLRU` ratio
limits, `Split` partition limits, and `EvictionCandidates` empty bookkeeping. It never populates a
queue, never evicts, never moves an entry between SLRU queues, never drives the hold-space split,
and never re-fetches an entry after an SLRU downgrade. Per guide §C the contract rows demand
behavioral RED tests with a false-green probe; those are absent for Task 011's load-bearing
contracts. The implementation is correct on read, but there is **no oracle-backed test that would
turn red** if a future edit broke: (a) getEntry double-fetch (H-011-a), (b) hold-space
double-reserve (H-011-b), (c) PreActive-visibility skip (H-011-c), (d) zero-size accounting, (e)
reserve/background cursor independence, (f) Split System-resize rollback.

Needed RED + false-green probes (oracle = CH source lines cited):
- **getEntry double-fetch** (oracle `SLRU:906-933`): SLRU downgrade an entry, then read
  `slru_iterator->getEntry()->size`; must observe the *new* Active entry, not size 0.
  False-green: replace `getEntry` body with a snapshot of the pre-move `entry` ⇒ test reads 0.
- **hold-space split** (oracle `EvictionCandidates.cpp:314-343,392-402`): a reserver evicts under
  partial availability; a second thread taking `CacheStateGuard::Lock` between `evict()` and
  `afterEvictState()` must NOT see the freed space as fittable. False-green: move the
  `queue_entries_to_invalidate` invalidation from `afterEvictState` into `evict()` ⇒ double-reserve.
- **cursor independence** (oracle `LRU:925-970`): drive a `Reserve` pass and a `Background` pass;
  advancing one cursor must not move the other; `getEvictionPosCount(Reserve)` ≠
  `getEvictionPosCount(Background)`. False-green: collapse both to one member ⇒ counts equal.
- **zero-size accounting** (oracle `LRU add :162`, `incrementSize :796`): `add(size=0)` then assert
  `getSize==0 && getElementsCount==0`; first `incrementSize` flips elements 0→1. False-green:
  count the element at `add` time ⇒ elements==1 too early.
- **Split rollback** (oracle `Split:155-165`): make System `modifySizeLimits` throw; assert Data
  limits are restored to `prev_data_*`. False-green: drop the `catch(...)` rollback ⇒ Data stays
  resized.

### F-011-O — `CacheUsagePerUser` over-port (PLAUSIBLE, = ledger O-011; D adjudicates)
`CacheUsage.h` ports the **full** `CacheUsagePerUser` (`snapshot`/`getOrSet`/`touchClient`/
`collectIdleClients`/`ShardedMap<UserID,CacheUserData>`), whose only real driver is the overcommit
policy — which is explicitly **excluded** (task "Do not copy `OvercommitFileCachePriority`"; base
virtuals `touchClientAccess`/`collectIdleClients` are no-ops). The task authorizes only "the minimal
reachable subset (EvictionInfo pin, `CacheUsageStatGuard`, base no-op hooks)". The full
`CacheUsagePerUser` class exceeds that subset and has no non-overcommit caller in Task 011.
Recommendation: D confirm whether Task 012/013 reaches `CacheUsagePerUser` (via
`FileCache::getUsageStatPerClient` at `FileCache.cpp:2725`); if not, it is an over-port to trim.
Not a correctness risk (the code is faithful and unreferenced), so left as PLAUSIBLE.

## 4. Already-signed items — still hold (do NOT re-litigate)

- **SD2** (absl→folly F14 in `EvictionInfo`/`candidates`/`kept_alive_cache_usage`): faithful; SD1
  no-escape invariant verified in code. Still holds.
- **SD-011-1** (`original_queue_types` stays `std::unordered_map`): preserved.
- **SD-011-3 / SD5** (`std::list` + splice + cursor stability): preserved, not swapped.
- **SD-011-4** (SLRU PreActive/Moving 2-phase not simplified): mirrored faithfully.
- **D-011-1..7** infra mappings (ProfileEvent timer shim, `folly::Random`, omitted
  `LockMemoryExceptionInThread`, `ostringstream`, no-op bernoulli failpoint, `dynamic_cast`+check,
  `ClickHouseTSA.h`): all applied exactly as approved.
- **B2** overcommit metric drop in `CacheUsage.h`: applied.

## 5. Conclusion

- Task 011 reopen list: **none for correctness.** Two follow-ups: **F-011-T** (add the behavioral
  RED + false-green tests above — Task 012 owns test registration, so route there) and **F-011-O**
  (D adjudicate the `CacheUsagePerUser` over-port).
- Confirmed findings: **1** (F-011-T). Plausible findings: **1** (F-011-O).
- zero-unresolved gate: the source port passes; the gap is test oracles, not behavior.
- The implementation is safe to keep; it is a genuine §3 literal translation, not an equivalent
  reinvention.
