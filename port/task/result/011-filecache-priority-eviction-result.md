# Task 011 — Priority/Eviction Source Migration (Center-SCC Part A) — Result

```text
status: success
result: migration-only source written; structurally validated; NO build claimed
```

## Baselines (exact)

```text
ClickHouse: /root/oss/clickhouse   branch ch-filecache   HEAD 47e8555d8bacf09972ac4c8bd723a5e0daf7f4e0   (clean, source unmodified)
Velox:      /root/oss/velox        branch filecache       HEAD 1b41f73382668ffdc8d902e6dc5268e2e22832e2
Velox dirty status: 11 NEW untracked files (below); nothing staged, committed, amended, rebased, or pushed.
Profile: root-oss (ENVIRONMENT.md).
```

## Files created (exactly the Task-011 scope — no CMake, no tests)

```text
velox/ch/Interpreters/FileCache/CacheUsage.h
velox/ch/Interpreters/FileCache/IFileCachePriority.h
velox/ch/Interpreters/FileCache/IFileCachePriority.cpp
velox/ch/Interpreters/FileCache/LRUFileCachePriority.h
velox/ch/Interpreters/FileCache/LRUFileCachePriority.cpp
velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h
velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp
velox/ch/Interpreters/FileCache/SplitFileCachePriority.h
velox/ch/Interpreters/FileCache/SplitFileCachePriority.cpp
velox/ch/Interpreters/FileCache/EvictionCandidates.h
velox/ch/Interpreters/FileCache/EvictionCandidates.cpp
```

`git status --short` shows only these 11 files (all `??`). No `CMakeLists.txt` or test file
touched. No ClickHouse source modified.

## CacheUsage scope actually ported (mandatory statement)

Ported the base subset only: `CacheUsageStatGuard`, the `CacheUsage` struct (constructor, `update`,
`total_size`, `total_elements`, `touch`, `idleFor`, `operator<`, `operator==`, `lessWithAssumption`
— declarations), and `CacheUsagePtr`.

NOT ported: `CacheUsagePerUser`, its nested `CacheUserData`, and
`CurrentMetrics::FilesystemCacheOvercommitUsers`. These belong to the excluded
`OvercommitFileCachePriority` and have no reachable caller in the Task-011/012 center SCC.
(The out-of-line definitions of `CacheUsage`'s own methods live only in ClickHouse cloud-only code;
they are absent from CH's public tree too, so their absence here is faithful, not a stub gap.)

## Structural RED / final / mutation evidence (logs under /root/oss/velox/_build/debug/)

```text
RED (pre-implementation, zero matches):
  /root/oss/velox/_build/debug/check_task_011_priority_symbols_red.log   -> 0 lines (empty). Captured before any file existed.

FINAL (post-implementation, every required symbol found):
  /root/oss/velox/_build/debug/check_task_011_priority_symbols.log       -> 65 lines; all 9 symbols present:
    enum class EvictionCursor, class HoldSpace, class EvictionInfo, class EvictionCandidates,
    removeQueueEntries, requiresAfterEvictWrite, requiresAfterEvictState, addForRestore, is_total_space_cleanup.

FALSE-GREEN MUTATION probe (delete-one-symbol):
  /root/oss/velox/_build/debug/check_task_011_priority_symbols_mutated.log
    Deleted the `requiresAfterEvictWrite()` declaration from EvictionCandidates.h, reran the loop:
    `requiresAfterEvictWrite` matches dropped 1 -> 0 (the check fails as designed).
  Restored the declaration and reran the FINAL loop: `requiresAfterEvictWrite` matches 0 -> 1 (clean pass).

Over-port negative check (grep CacheUsagePerUser|FilesystemCacheOvercommitUsers): 0 matches.
Fake-SCC-definition check (grep '...stub'): 0 matches.
git diff --check: no whitespace errors.
```

Note: `rg` is unavailable in this environment; `grep -rn` (identical `path:line:match` output) was used
as the equivalent for every Step-7 lookup.

## Commands run (structural validation only — no build)

```text
grep -rn "<symbol>" velox/ch/Interpreters/FileCache        # RED / FINAL / mutated loops -> logs above
grep -rn 'CacheUsagePerUser|FilesystemCacheOvercommitUsers' # over-port -> none
grep -rn '...stub'                                          # fake SCC defs -> none
git --no-pager diff --check                                 # -> no whitespace errors
git --no-pager status --short                               # -> only the 11 files
```

## Source-parity notes / infra mappings applied

Approved mappings (registered in the cross-profile decisions / consumer-contract ledger / design 07):
`namespace DB`->`facebook::velox::ch`; include remap; `Exception(LOGICAL_ERROR,...)`->`VELOX_FAIL`,
`Exception(BAD_ARGUMENTS,...)`->`VELOX_USER_FAIL`; `absl::flat_hash_map/set`->`folly::F14FastMap/F14FastSet`
(SD2); `EvictionInfo` keeps CH public inheritance, base map swapped to `F14FastMap`; `original_queue_types`
stays `std::unordered_map`; `std::list` LRU/SLRU queues preserved; `boost::noncopyable`->deleted copy ops;
`magic_enum::enum_name`->explicit exhaustive `switch`; `ProfileEvents`/`CurrentMetrics`/`LOG_*`/`FailPoint`
no-op shims; `fiu_do_on(FailPoints::X,{...})`->`FAIL_POINT_TRIGGER(X)` (D10); per-thread profile timer
`CurrentThread::getProfileEvents().timer(EV)`->`ProfileEventTimeIncrement<Microseconds>(EV)` (D5);
`SCOPE_EXIT`->folly `<folly/ScopeGuard.h>` (Task-012 infra table, consumed here).

Mappings NOT individually itemized in the ledger/design but applied as trivial, behavior-preserving
leaf-infra under design-07's "replace reviewed infrastructure, preserve algorithm" rule — FLAGGED for
Controller confirmation:
- `randomSeed()` -> `folly::Random::rand64()`  (queue_id seed + shuffle seed; folly is a core dep).
- `pcg64 gen(seed)` -> `std::mt19937_64 gen(seed)`  (pcg not present in the Velox vcpkg tree; PRNG only
  feeds the test-only `shuffle()`).
- `LockMemoryExceptionInThread(VariableContext::Global)` -> removed (no CH per-thread MemoryTracker in
  Velox; the guard is a pure no-op; a comment records the drop). 2 sites in LRUFileCachePriority.cpp.
- `assert_cast<T*>(x)` -> `static_cast<T*>(x)`  (release-mode `assert_cast` behavior; the downcast is an
  SLRU algorithm invariant). 5 sites in SLRUFileCachePriority.cpp.
- `WriteBufferFromOwnString`+`operator<<` -> `fmt::format` in the two `toString()` methods
  (EvictionCandidates.cpp); produced strings are byte-identical to the CH `<<` chains (verified).
- `NOT_IMPLEMENTED` (only `IFileCachePriority::getUsageStatPerClient`) -> `VELOX_FAIL` (generic runtime;
  the infra table itemizes only `LOGICAL_ERROR`->`VELOX_FAIL` and `BAD_ARGUMENTS`->`VELOX_USER_FAIL`).
- C++23 -> C++20 rewrites (Velox is `CMAKE_CXX_STANDARD 20`): deducing-this
  `begin/end(this auto&& self)` -> const + non-const overloads (EvictionCandidates.h);
  `std::to_underlying(e)` -> `static_cast<uint8_t>(e)` (SplitFileCachePriority.cpp).
- CH `base/defines.h` thread-safety annotations `TSA_GUARDED_BY`/`TSA_REQUIRES`/
  `TSA_SUPPRESS_WARNING_FOR_READ` preserved verbatim with `#ifndef`-guarded no-op macro definitions in
  LRUFileCachePriority.h (no shared `defines.h` shim exists and file scope forbids adding one; the
  annotations have zero runtime effect and match CH's non-Clang path).
- Two compile-safety additions (unreachable, warning-silencing, behavior-preserving): a trailing
  `return false;` after the exhaustive `is_evictable_state` switch in `LRUFileCachePriority::iterateImpl`,
  and a trailing `VELOX_FAIL(...)` after each exhaustive `evictionPos` switch (both reference-returning /
  bool-returning functions whose only non-returning case throws).

Preserved invariants (spot-verified against CH): zero-size accounting; independent reserve/background
LRU cursors; lazy iterator-safe invalidation; SLRU probationary/protected split, `PreActive` transition,
`SLRUIterator` identity via `setIterator`, `addForRestore` original-queue routing, downgrade rollback;
Split General+Data->Data / System->System routing and dual size+element ratio split with system-resize
rollback; `EvictionInfo` per-`QueueID` targets with `min(requested,current)` total-space cleanup and
`kept_alive_cache_usage` transfer on merge; `EvictionCandidates` lock-free `evict()` with
`afterEvictWrite` strictly before `afterEvictState`, `removeQueueEntries`, and original-queue-type capture.

`friend class OvercommitFileCachePriority;` (a never-defined forward friend template in
IFileCachePriority.h) is retained verbatim from CH — it is a declaration, not a ported/faked class.

## Self-review

One read-only self-review subagent compared all 11 ported files against their CH originals across every
transformation category (namespace, includes, error macros, enum-name switches, F14 mapping + EvictionInfo
base swap, deleted copy ops, SCOPE_EXIT, fail-points, profile timer, assert_cast, PRNG, memory-tracker
drop, C++23->C++20 rewrites, toString byte-parity, over-port exclusion, guaranteed-copy-elision of the
SLRU sub-queue members). Result: no high-confidence faithfulness or correctness bugs; all findings clean.
No findings required a fix.

## Blockers

None. No priority source required a fake `FileCache`/`KeyMetadata`/`FileSegment`/`FileSegmentInfo`/
`FileCacheReserveStat` definition. All 9 Task-011 `ProfileEvents` names and 4 `CurrentMetrics` names were
present in the accepted Task-003 shims (`velox/ch/Common/ProfileEvents.h`, `CurrentMetrics.h`).

## Declaration

No build is claimed. Task 011 is migration-only; the priority/eviction `.cpp` files reference center-SCC
types (`FileSegment`, `FileSegmentInfo`, `FileSegmentMetadata`, `KeyMetadata`, `LockedKey`, `FileCache`,
`FileCacheReserveStat`, `FileSegmentKind`) via includes that Task 012 provides. Task 012 closes the atomic
SCC batch: it adds those types, registers all priority/core sources into `velox_ch_filecache_core`, adds
CMake + `velox_ch_filecache_core_scc_test`, and runs the only green build for this stage.

## Recommended next task

Task 012 — immediately, in the same Velox `filecache` worktree.

## Post-review fix (Critical finding)

An independent Task-011 review reported one Critical finding: `EvictionInfo::takeKeptAliveCacheUsage`
in `velox/ch/Interpreters/FileCache/EvictionCandidates.h` called `kept_alive_cache_usage.merge(...)`,
but real `folly::F14FastSet` (`folly/container/F14Set.h`) has no `merge` method — only `std`'s
`unordered_set`/`unordered_map` provide `merge`. This would have failed to compile at Task 012.

Fix applied (surgical, algorithm-preserving): replaced the `merge` call with the iterator-range
`insert` overload that `F14FastSet` does provide:

```cpp
void takeKeptAliveCacheUsage(EvictionInfo & other)
{
    kept_alive_cache_usage.insert(
        other.kept_alive_cache_usage.begin(),
        other.kept_alive_cache_usage.end());
}
```

This preserves the original ordering (pins copied into `this` before the merged entries move) and does
not clear `other.kept_alive_cache_usage` explicitly — its owning `EvictionInfo` is discarded by the
caller (`EvictionCandidates::add`/`addOrUpdate`), matching the prior `merge`-based behavior (which also
never guaranteed clearing the source beyond what the caller's discard already does).

Validation after the fix (re-run of the exact Task-011 structural checks, no build claimed):
```text
grep -rn -E 'CacheUsagePerUser|FilesystemCacheOvercommitUsers' velox/ch/Interpreters/FileCache   -> 0 matches (over-port check clean)
grep -rn '...stub' velox/ch/Interpreters/FileCache                                               -> 0 matches (fake-SCC-definition check clean)
grep -rn '.merge(' velox/ch/Interpreters/FileCache                                                -> 0 matches (no remaining non-existent-API calls)
git --no-pager diff --check                                                                       -> no whitespace errors
git --no-pager status --short                                                                      -> still exactly the same 11 `??` files; nothing staged/committed
```

Confirmed against the real `folly/container/F14Set.h` (vendored at
`/root/gluten/dev/vcpkg/vcpkg_installed/x64-linux-avx/include/folly/container/F14Set.h`) that
`F14FastSet` supports the iterator-range constructor, `void insert(InputIt first, InputIt last)`
(line 341), so the replacement is a real, existing API — not another fake/over-port.

Remaining concerns: none identified for this finding. As before, no build of the priority/eviction
sources has been attempted (Task 012 provides the center-SCC types needed to compile); this fix is
verified by header inspection and structural grep only, consistent with the migration-only scope of
Task 011.

## Controller review 1

```text
controller_status: accepted
environment_profile: root-oss
build_claim: none (migration-only by contract)
```

Scope and structure:

- Inspected all 11 Task-011 files against the CH originals.
- Confirmed no fake center-SCC definition, CMake file, or test file.
- Confirmed the non-overcommit `CacheUsage` subset only.
- Confirmed `EvictionInfo` preserves public inheritance while swapping only the
  base flat-map type.
- Confirmed F14 map/set mappings, `original_queue_types` as
  `std::unordered_map`, `std::list` queue/cursor structure, state transitions,
  zero-size accounting, SLRU identity, split rollback, and eviction finalization
  order.

Review finding and resolution:

```text
finding:
  F14FastSet does not provide absl flat_hash_set::merge

fix:
  iterator-range insert copies every CacheUsagePtr pin before entries move

re-review:
  spec compliance approved
  technical quality approved
  Critical/Important findings: 0
```

Controller structural gate:

```text
required symbols: all present
CacheUsagePerUser / FilesystemCacheOvercommitUsers: absent
fake SCC definitions: absent
residual .merge call: absent
git diff --check: clean
```

The reviewed leaf adaptations (PRNG used only for shuffle, absent MemoryTracker
guard, release-equivalent downcasts, byte-equivalent formatting, C++20 syntax,
no-op TSA annotations, and exhaustive-switch warning guards) do not change the
priority/eviction consumer contract. Task 012 remains responsible for the first
compile/link validation.

Accepted Velox commit:

```text
72b77cc2f995c9a6e1d3bb82cd28bfc0beade9a4
Task 011: Add `FileCache` priority eviction
```

Task 011 is accepted. Task 012 must run immediately to close the atomic center
SCC.
