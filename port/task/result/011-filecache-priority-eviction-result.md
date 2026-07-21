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

## Post-acceptance contract audit 1 (Review-2 B2/B3)

```text
controller_status: reopened_by_contract_audit
environment_profile: root-oss
task: 011
reopened_by: port/task/fullreview/root-oss/2/003-014-review-decisions.md (B2, B3)
reopened_by: port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md
  §2 "Task 011 — Priority / Eviction — REOPEN (test-plane)", §7.2 item 2
```

The Tasks 003-014 full review (Review 2) found the accepted migration above
structurally faithful and identified no implementation defect, but reopened
Task 011 on the test-plane only:

```text
HOLE: no MoveEvictionPos-equivalent cursor test exists
  (LRUFileCachePriority.h:41,201 declares an unused
  friend class ::FileCacheTest_MoveEvictionPos_Test).
UNPROVEN: file_cache_slru_downgrade_fail_before_finalize
  (SLRUFileCachePriority.cpp:584) is armed in production but no test arms it.
HOLE: no SLRUDynamicResizeCorrectEviction-equivalent test exists
  (SLRUFileCachePriority.cpp:852-874 collectEvictionInfoForResize).
```

This receipt is reopened per the state machine in `EXECUTION_PROTOCOL.md`
(`accepted -> reopened_by_contract_audit -> worker_running`). The original
acceptance above is unchanged and immutable; this section is additive. The
binding corrective contract is recorded in
`port/task/011-filecache-priority-eviction.md`, section
`## Review-2 corrective scope: B2/B3 test evidence`. A fresh Worker must
execute that scope, append a `## Worker attempt 2` (or `## Corrective wave 1`)
section below with the required RED/false-green/mono/non-mono/accumulated
evidence, before Task 015 may start.

No production change is authorized by this audit alone; the corrective task
may change production code only if its own new tests expose a real defect.

## Worker attempt 2 (Review-2 B2/B3 corrective wave)

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 011 (Review-2 B2/B3 test-plane corrective scope)
```

Executes exactly `port/task/011-filecache-priority-eviction.md` §`## Review-2
corrective scope: B2/B3 test evidence`. Test-only: no production `.cpp`/`.h`
was changed — none of the three new tests exposed a defect in
`moveEvictionPosIfEqual`, the `DowngradedEntriesInfos` rollback, or
`collectEvictionInfoForResize`, so per the Production-change rule no production
edit was made.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `b92a0ae3a96493aa63df44bc38514c68003db28e` | clean (no FileCache source dirty) |
| `/root/oss/clickhouse` | `ch-filecache` | `f0679980a2e135dd87e1193a5b901feb899bb328` | only `tmp/` untracked |

Baseline gate confirmed green before starting (Task 012's own gate, both
configs): `velox_ch_filecache_core_scc_test` 101/101 mono
(`_build/debug`) and 101/101 non-mono (`_build/debug-task012-nonmono`, rebuilt
from current source); accumulated mono CTest 13/13.

## Files changed (test-plane only; exactly the corrective file scope)

```text
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/MoveEvictionPosTest.cpp   (NEW, B2)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp  (MODIFIED, B3a + B3b + addDownloadedSegment fixture helper)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt            (MODIFIED, new velox_ch_filecache_priority_cursor_test target only)
```

No production source, no ClickHouse source. `git status --short` in Velox shows
exactly these three paths (two `M`, one `??`). Nothing staged/committed/pushed.

## Tests added

```text
B2  velox_ch_filecache_priority_cursor_test :: TEST(FileCacheTest, MoveEvictionPos)
      Global-scope, own standalone executable (matches friend
      ::FileCacheTest_MoveEvictionPos_Test; avoids the "FileCacheTest" gtest
      suite-name collision with FileCacheTest.cpp's TEST_F cases).
B3a velox_ch_filecache_core_scc_test :: PriorityEvictionTest.SLRUDowngradeFailpointRollsBackBeforeFinalize
      Arms file_cache_slru_downgrade_fail_before_finalize via SCOPED_TESTVALUE_SET
      and drives the downgrade through tryIncreasePriority.
B3b velox_ch_filecache_core_scc_test :: PriorityEvictionTest.SLRUDynamicResizeEvictsFromBothSubQueues
      Proves collectEvictionInfoForResize requires eviction from BOTH sub-queues
      and modifySizeLimits then applies the new 8/6 limits without LOGICAL_ERROR.
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure mono (new CMake target) | 0 | `/root/oss/velox/_build/debug/task011corr/configure_b2.log` |
| build B2 cursor test (mono) | 0 | `/root/oss/velox/_build/debug/task011corr/build_b2_mono.log` |
| build scc test w/ B3a+B3b (mono) | 0 | `/root/oss/velox/_build/debug/task011corr/build_scc_mono.log` |
| run B2 cursor test (mono) GREEN 1/1 | 0 | `/root/oss/velox/_build/debug/task011corr/green_b2_mono.log` |
| run scc test (mono) GREEN 103/103 | 0 | `/root/oss/velox/_build/debug/task011corr/green_scc_mono.log` |
| accumulated CTest `^velox_ch_` (mono) 14/14 | 0 | `/root/oss/velox/_build/debug/task011corr/accumulated_ctest_mono.log` |
| configure non-mono (new CMake target) | 0 | `/root/oss/velox/_build/debug-task012-nonmono/configure_task011corr.log` |
| build both targets (non-mono) | 0 | `/root/oss/velox/_build/debug-task012-nonmono/build_task011corr.log` |
| run scc test (non-mono) GREEN 103/103 | 0 | `/root/oss/velox/_build/debug-task012-nonmono/test_scc_task011corr.log` |
| run B2 cursor test (non-mono) GREEN 1/1 | 0 | `/root/oss/velox/_build/debug-task012-nonmono/test_cursor_task011corr.log` |
| B2 RED (mutate moveEvictionPosIfEqual) | 1 | `/root/oss/velox/_build/debug/task011corr/red_b2_MUT.log` |
| B3a RED (mutate ~DowngradedEntriesInfos rollback) | 1 | `/root/oss/velox/_build/debug/task011corr/red_b3a_MUT.log` |
| B3b RED (mutate collectEvictionInfoForResize) | 1 | `/root/oss/velox/_build/debug/task011corr/red_b3b_MUT.log` |

The "eight logs" required by the task: mono/non-mono focused for both
executables (green_scc_mono, test_scc_task011corr, green_b2_mono,
test_cursor_task011corr), mono accumulated (accumulated_ctest_mono), and the
RED/false-green mutation log for each of the three tests (red_b2_MUT,
red_b3a_MUT, red_b3b_MUT). B3a's own mono green run is in green_b3a_mono.log;
the full-suite mono green (including B3b) is green_scc_mono.log.

## Acceptance evidence

```text
test count:
  velox_ch_filecache_core_scc_test:      103 (mono) / 103 (non-mono)  [was 101; +2 = B3a, B3b]
  velox_ch_filecache_priority_cursor_test: 1 (mono) /   1 (non-mono)  [new, B2]
  accumulated mono CTest (^velox_ch_):    14/14                       [was 13; +1 new target]
failed tests: 0
skipped/disabled tests: 0 (no *_DISABLED_*, no GTEST_SKIP, no comment-only bodies)
benchmark result: n/a (not required by B2/B3)
git diff --check: no whitespace errors

RED / false-green mutation (each: mutate production line -> test FAILS for the
declared reason [not a compile error, build exit 0] -> restore via
`git checkout --` -> test GREEN again):
  B2  LRUFileCachePriority.cpp moveEvictionPosIfEqual `*pos = std::next(it);`
        -> both cursors stay at offset 10 (moved-away node) instead of 20;
           ASSERT_EQ fails. Restored -> 1/1 green.
  B3a SLRUFileCachePriority.cpp ~DowngradedEntriesInfos rollback loop
        `for (auto & entry : *this) entry.rollbackState();`
        -> a protected entry is left stuck in Evicting (state 2) not Active (0);
           EXPECT_EQ fails ("left stuck ... after a skipped downgrade
           finalization"). Restored -> green.
  B3b SLRUFileCachePriority.cpp collectEvictionInfoForResize probationary
        delegation `info->add(probationary_queue.collectEvictionInfoForResize(...))`
        -> queues_requiring_eviction == 1 (protected only) not 2; ASSERT_EQ
           fails, and modifySizeLimits would then throw LOGICAL_ERROR
           (probationary 10 > new 4-byte limit). Restored -> green.

post-restore: LRUFileCachePriority.cpp and SLRUFileCachePriority.cpp are clean
(tracked, unmodified); only the three test-plane files remain changed.
```

## Worker review

```text
review subagent: one read-only code-review agent over the complete B2/B3
  task-owned diff (MoveEvictionPosTest.cpp, PriorityEvictionTest.cpp,
  CMakeLists.txt) plus the production surfaces they exercise.
findings: no significant issues. Confirmed all three tests are non-vacuous
  (each catches its targeted regression and fails cleanly under the RED
  mutation); addDownloadedSegment path/releasable()/lifetime correct; B3a
  objects valid across the throw (RAII rollback runs during unwinding before
  the assertions read state); B2 at global scope in its own executable (friend
  match, no suite collision); new CMake block mirrors the scc link set and
  leaves the scc block untouched; no regression to the 6 pre-existing cases
  (103/103 full binary).
resolutions: none required (no actionable finding).
unresolved findings: none.
```

The reviewer noted it could not itself run the mutations (read-only) nor the
non-mono config; the Worker performed both directly (RED confirmed for all
three; non-mono 103/103 + 1/1 green — logs above), closing those two
limitations with observed evidence.

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 011 (Review-2 B2/B3 corrective scope) was attempted.
No production source was changed (no defect exposed); test-plane only.
Changes are unstaged and uncommitted in both repositories.
The worker stopped after writing this receipt.
```

## Controller review 2 — Review-2 B2/B3

```text
controller_status: accepted
environment_profile: root-oss
scope: test/evidence only
```

The corrective wave added:

- a dedicated global-friend `MoveEvictionPos` executable proving both LRU
  cursors advance independently;
- an armed SLRU downgrade failpoint test proving RAII rollback; and
- an SLRU dynamic-resize test requiring eviction from both sub-queues.

Independent review:

```text
spec compliance: approved
technical quality: approved
Blocker/Major findings: 0
```

Controller evidence:

```text
mono:
  cursor 1/1
  B3 focused 2/2
  accumulated CTest 14/14

non-mono:
  VELOX_MONO_LIBRARY=OFF
  cursor/core focused CTest 2/2

failed/skipped/disabled:
  0/0/0

mutations:
  cursor update removed -> B2 fails
  downgrade rollback removed -> B3a fails
  probationary resize branch removed -> B3b fails

git diff --check:
  clean
```

No production defect was exposed and no production source changed.

Accepted Velox commit:

```text
b18a8d039904a0421011f6d5a47bcefa1669185b
Task 011: Complete priority eviction evidence
```

B2 and B3 are closed.
