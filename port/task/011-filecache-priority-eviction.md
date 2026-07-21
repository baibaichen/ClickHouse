# Task 011: Port Priority/Eviction Sources (Center-SCC Part A)

> **Atomic batch rule:** Task 011 and Task 012 are one implementation stage.
> Task 011 writes the exact priority/eviction sources but does not register or
> compile them. Run Task 012 immediately afterward in the same Velox worktree;
> Task 012 adds the mutually dependent core files, tests, CMake registration,
> and the only green build for this stage.

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies only `<velox_repo>` plus its result handoff. Do not
> modify ClickHouse source. Do not stage or commit either repository.

## Status and authority

```text
controller_status: reopened_by_contract_audit
environment_profile: root-oss
reopened_by: port/task/fullreview/root-oss/2/003-014-review-decisions.md (B2, B3)
reopened_by: port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md
  §2 "Task 011 — Priority / Eviction — REOPEN (test-plane)", §7.2 item 2
```

The original priority/eviction source migration below (Steps 1-9) is accepted
and unchanged: Task 012 already compiles and links these sources and the
whole-port Tasks-003-010 review found no defect in them. The Review-2 audit of
Tasks 003-014 found the migration structurally faithful but reopened Task 011
on the **test-plane** only: two real CH regression tests were never migrated,
and one production failpoint was never armed. `## Review-2 corrective scope:
B2/B3 test evidence` below is the binding, additive amendment a fresh Worker
must execute before Task 015 may start. It does not reopen, weaken, or
contradict any accepted decision in the sections that follow.

## Goal

Port the real ClickHouse priority and eviction files without fake
`FileCache`, `KeyMetadata`, `FileSegmentInfo`, or `FileCacheReserveStat`
definitions:

```text
CacheUsage.h
IFileCachePriority.h / .cpp
LRUFileCachePriority.h / .cpp
SLRUFileCachePriority.h / .cpp
SplitFileCachePriority.h / .cpp
EvictionCandidates.h / .cpp
```

These `.cpp` files include or call center-SCC types, so compiling them before
Task 012 would require false stubs. Task 011 intentionally stops after exact
source migration and structural verification. Task 011 has no test target and
no compile closure of its own; both belong to Task 012, which links these
sources into `velox_ch_filecache_core` and exercises them through
`velox_ch_filecache_core_scc_test`.

## Dependency pre-check (stop at the gate if any row is unmet)

Before editing, confirm every external name this task reaches already has an
explicit reviewed mapping. If any row below is not yet true in the Velox
worktree, stop as `blocked` and report it; do not guess a mapping.

| External name | Approved mapping | Source of approval |
|---|---|---|
| Task-003 B1 `ProfileEvents` name surface | `velox/ch/Common/ProfileEvents.h` contains all 34 required names, no-op | Task 003 corrective acceptance (must be `accepted`, not merely `reopened_by_contract_audit`) before this task starts |
| Task-003 B2 `CurrentMetrics` name surface | `velox/ch/Common/CurrentMetrics.h` contains the 5 required names, no-op | Task 003 corrective acceptance |
| `DB` namespace | `facebook::velox::ch` | `port/1-dependencies/01-filecache-infra-mapping.md` |
| `String`/`UInt*` aliases | Task-003 `ClickHouseAliases.h` | Task 003 result |
| `Exception`/`ErrorCodes`/`chassert` | `VELOX_FAIL`/`VELOX_CHECK`/`velox/ch/Common/ClickHouseAssert.h` | Task 003 corrective acceptance |
| `absl::flat_hash_map`/`flat_hash_set` | `folly::F14FastMap`/`F14FastSet` with explicit hash (SD2 below) | `port/task/fullreview/cross-profile/1/003-010-review-decisions.md` Task-011 contract decisions |
| `boost::noncopyable` | deleted copy constructor/assignment | `port/1-dependencies/01-filecache-infra-mapping.md` |
| `magic_enum` | explicit exhaustive `switch` | `port/1-dependencies/01-filecache-infra-mapping.md` |
| Task-009 `ShardedMap`/`F14FastMap` no-reference-escape contract (SD1) | approved, unrelated to this task's own containers but binding on any reused shard type | cross-profile decisions, "Profile reconciliation" table |

Do not start Task 011 while Task 003's B1/B2 corrective work is only
`reopened_by_contract_audit`; that status means the required no-op name
surfaces are not yet present.

## Consumer-contract excerpts (CH source, real callers, file:line)

Task 011 is a source migration, but every port decision below is anchored to
real CH definitions and their real callers, not to a leaf-header guess.

### `CacheUsage` — non-overcommit subset only

```text
src/Interpreters/FileCache/CacheUsage.h:32-37   CacheUsageStatGuard (mutex + Lock)
src/Interpreters/FileCache/CacheUsage.h:41-75   CacheUsage: origin_info, priority
                                                 (non-owning), guard, update,
                                                 total_size/total_elements,
                                                 touch/idleFor, comparisons,
                                                 lessWithAssumption
src/Interpreters/FileCache/CacheUsage.h:79-122  CacheUsagePerUser: snapshot,
                                                 tryGet, getOrSet, touchClient,
                                                 collectIdleClients,
                                                 CacheUserData with
                                                 CurrentMetrics::Increment on
                                                 FilesystemCacheOvercommitUsers
```

Real consumers of the base `CacheUsage` type reachable from the center SCC:

```text
src/Interpreters/FileCache/EvictionCandidates.h:92   addCacheUsage(CacheUsagePtr)
src/Interpreters/FileCache/EvictionCandidates.h:97   takeKeptAliveCacheUsage(EvictionInfo &)
src/Interpreters/FileCache/EvictionCandidates.h:111  kept_alive_cache_usage (flat_hash_set<CacheUsagePtr>)
src/Interpreters/FileCache/IFileCachePriority.h:456  virtual setCacheUsageStatGuard(...) {} (no-op base hook)
src/Interpreters/FileCache/IFileCachePriority.h:460  virtual touchClientAccess(const UserID &) {} (no-op base hook)
src/Interpreters/FileCache/IFileCachePriority.h:463  virtual collectIdleClients(...) const { return {}; } (no-op base hook)
```

`CacheUsagePerUser` (`CacheUsage.h:79-122`) has no reachable caller in the
Task-011/012 center SCC: nothing in `IFileCachePriority`, `LRUFileCachePriority`,
`SLRUFileCachePriority`, `SplitFileCachePriority`, or `EvictionCandidates`
constructs, stores, or calls it. Its only consumer is
`OvercommitFileCachePriority`, which is explicitly excluded from this port.
Porting `CacheUsagePerUser` bodies here would be over-port: a behavior with no
in-scope caller.

**Mandatory scope line:** port only the `CacheUsage` struct, `CacheUsagePtr`,
and `CacheUsageStatGuard` from `CacheUsage.h`. Do not port `CacheUsagePerUser`,
its `CacheUserData`, or `CurrentMetrics::FilesystemCacheOvercommitUsers`. Base
`IFileCachePriority` no-op/throw hooks (`setCacheUsageStatGuard`,
`collectIdleClients`) are ported as no-ops exactly as CH declares them; a
priority subclass overriding them with real per-user logic is
`OvercommitFileCachePriority` and stays out of scope.

### Container structure-deviation registrations (binding on this task)

| CH structure | CH file:line | Velox replacement | Guarantee difference | Hard constraint | Approval |
|---|---|---|---|---|---|
| SD2: `EvictionInfo : absl::flat_hash_map<QueueID, QueueEvictionInfoPtr>` | `EvictionCandidates.h:56` | `EvictionInfo : folly::F14FastMap<QueueID, QueueEvictionInfoPtr>` — keep CH's public inheritance from the map type; swap only the base container | F14 rehash may move values; no reference/iterator may survive a mutating call | none of `EvictionInfo`'s own call sites retain a reference across mutation (verified structurally in Step 7) | cross-profile decisions, Task-011 contract; user-approved: base container swap only, composition is not approved |
| SD2: `kept_alive_cache_usage: absl::flat_hash_set<CacheUsagePtr>` | `EvictionCandidates.h:111` | `folly::F14FastSet<CacheUsagePtr>` | none observed (`shared_ptr` values, no address escape) | n/a | cross-profile decisions |
| SD2: `candidates: absl::flat_hash_map<FileCacheKey, KeyCandidates, ...>` | `EvictionCandidates.h:187` | `folly::F14FastMap<FileCacheKey, KeyCandidates, FileCacheKeyHash>` | same as above | n/a | cross-profile decisions |
| not a deviation: `original_queue_types: std::unordered_map<const FileSegmentMetadata *, IFileCachePriority::QueueEntryType>` | `EvictionCandidates.h:193` | remains `std::unordered_map` | none; explicitly excluded from the F14 mapping | n/a | cross-profile decisions: "Map only CH `absl::flat_hash_map/set` containers to F14. Keep `original_queue_types` as `std::unordered_map`." |
| SD5: `LRUQueue = std::list<EntryPtr>` | `LRUFileCachePriority.h:169` | remains `std::list` | n/a (no deviation) | n/a | cross-profile decisions |
| SD5: `LRUQueue = std::list<Entry>` | `SLRUFileCachePriority.h:154` | remains `std::list` | n/a (no deviation) | n/a | cross-profile decisions |

Any container substitution not in this table (including a node-based F14
variant, or any container swap in `SplitFileCachePriority` or the priority
base class) is out of scope for this task; stop at the dependency gate instead
of improvising one.

### Typed-subtype guidance for reserve/eviction failures

The priority/eviction sources migrated here throw only `ErrorCodes::LOGICAL_ERROR`
invariant violations (e.g. `LRUFileCachePriority.cpp:138,150,160,638,648,683,804,846,892,934,947`;
`SLRUFileCachePriority.cpp:152,162,797,806,812,902`; `IFileCachePriority.cpp:56,64,69`;
`EvictionCandidates.cpp:131,153,296`). Map every one of these to `VELOX_FAIL`
exactly as Task 003 requires; do not introduce a space-related subtype for
them.

The real call site that distinguishes an out-of-space condition from a logical
error is downstream, in `src/Interpreters/FileCache/WriteBufferToFileSegment.cpp:97`
(`ErrorCodes::NOT_ENOUGH_SPACE` when `tryReserve` fails). That call site is out
of this task's file scope (Task 014 territory), but if any Task-011-owned
function itself needs to report a distinguishable reserve/eviction failure
(bool return is insufficient, e.g. a caller must tell "reservation impossible"
from "internal invariant broken"), use a typed subtype rather than collapsing
both into one opaque `VELOX_FAIL`. Record any such call site explicitly in the
result receipt; do not add one silently.

## RED and false-green probe requirements (migration-only task)

Task 011 has no test binary, so its RED/false-green evidence is structural,
using the Step 7 symbol/grep checks:

```text
RED: before any file under velox/ch/Interpreters/FileCache/{CacheUsage.h,
  IFileCachePriority.*, LRUFileCachePriority.*, SLRUFileCachePriority.*,
  SplitFileCachePriority.*, EvictionCandidates.*} is created, every `rg`
  lookup in Step 7 finds zero matches. Record this pre-implementation grep
  output as the RED evidence.

false-green probe: after implementation, pick one required symbol from the
  Step 7 list (for example `requiresAfterEvictWrite`), delete just that
  declaration from the migrated source, rerun the same `rg` lookup, and
  confirm it now finds zero matches (the check fails as designed). Restore
  the declaration and rerun Step 7 to confirm it passes again. Record both
  grep outputs (missing and restored) in the result receipt. A Step-7 check
  that would still "pass" with a required symbol deleted is false-green and
  must be fixed before this task is accepted.
```

## Review-2 corrective scope: B2/B3 test evidence

This corrective scope is the only remaining work item for Task 011. B3a/B3b go
into the already-existing, already-registered
`velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp` (as before;
no CMake change for these two). B2 requires a **new, dedicated standalone
test file and executable** — `velox/ch/Interpreters/FileCache/tests/
MoveEvictionPosTest.cpp`, registered by a **new** `add_executable`/`add_test`
pair in `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` — instead of
being added to any existing test file (see the B2 section below for why: a
global-scope `TEST(FileCacheTest, MoveEvictionPos)` sharing a gtest test-suite
name with the existing `TEST_F(FileCacheTest, ...)` fixture cases, in the same
binary, is a gtest suite collision, not merely a namespace or friend-scope
problem). This corrective scope therefore modifies exactly one existing test
file (`PriorityEvictionTest.cpp`, B3a/B3b, no CMake change) and adds exactly
two new files (`MoveEvictionPosTest.cpp`, B2; the `CMakeLists.txt` edit that
registers it as its own executable, `velox_ch_filecache_priority_cursor_test`).

### Corrective dependency pre-check

Before editing, confirm every row is true; stop as `blocked` and report the gap
instead of guessing:

| External name | Approved mapping | Source of approval |
|---|---|---|
| Task 012 acceptance | `velox_ch_filecache_core_scc_test` builds green in both mono and non-mono configurations | `port/task/result/012-filecache-core-scc-result.md` Controller review 1 |
| `facebook::velox::common::testutil::TestValue` / `SCOPED_TESTVALUE_SET` | already used by the accepted `SLRUModifySizeLimitsRollbackOnThrow` test in this same file | `PriorityEvictionTest.cpp:273-282` |
| `FAIL_POINT_TRIGGER` macro | Velox TestValue-backed injection point, release-inert unless armed | `velox/ch/Common/FailPoint.h:8-19` |
| `CacheMetadata`/`KeyMetadata` construction pattern (used by `PriorityEvictionTest`'s fixture for B3a/B3b, and inlined directly in B2's new, standalone `MoveEvictionPosTest.cpp` since a plain global-scope `TEST` has no fixture) | `PriorityEvictionTest::SetUp`/`makeKeyMetadata` | `PriorityEvictionTest.cpp:47-86` |
| `velox_ch_filecache_core_scc_test`'s link set for the production types B2 needs (`velox_ch_filecache_core`, `velox_ch_filecache`, `velox_test_util`, `velox_exception`, `velox_file`, `velox_memory`, `Folly::folly`, `fmt::fmt`, `GTest::gtest`, `GTest::gtest_main`) | the exact library set the new `velox_ch_filecache_priority_cursor_test` executable must also link, since it exercises the same `LRUFileCachePriority`/`CacheMetadata`/`KeyMetadata` production surface in a fresh binary | `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` (`velox_ch_filecache_core_scc_test` target) |

### B2 — `MoveEvictionPos` cursor test (owner: Task 011)

CH source of truth: `src/Interpreters/tests/gtest_filecache.cpp:2421-2487`
(`TEST_F(FileCacheTest, MoveEvictionPos)`). Velox production surface under
test: `LRUFileCachePriority.h:41,201` (the currently-unused
`friend class ::FileCacheTest_MoveEvictionPos_Test` declaration),
`LRUFileCachePriority.cpp:625-656` (`LRUFileCachePriority::move`),
`LRUFileCachePriority.cpp:919-967` (`evictionPos`, `getEvictionPos`,
`setEvictionPos`, `moveEvictionPosIfEqual`, `resetEvictionPos`,
`getEvictionPosCount`).

**Target file: a new, standalone `velox/ch/Interpreters/FileCache/tests/
MoveEvictionPosTest.cpp`, built as its own new executable — not
`PriorityEvictionTest.cpp`, and not `FileCacheTest.cpp` either.** The friend
grant at `LRUFileCachePriority.h:201`, `friend class
::FileCacheTest_MoveEvictionPos_Test`, is a **global-namespace** class name.
gtest's `TEST(FileCacheTest, MoveEvictionPos)` macro generates a class named
exactly `FileCacheTest_MoveEvictionPos_Test` in whatever namespace the macro
invocation is lexically written in, so the test's C++ source location must be
at global scope (not inside `namespace facebook::velox::ch { namespace {
... } }`, which is where `PriorityEvictionTest.cpp` wraps everything — a
`TEST_F(PriorityEvictionTest, ...)` or any other `TEST`/`TEST_F` written
there would generate `facebook::velox::ch::(anonymous)::FileCacheTest_..._Test`,
which does not match the global friend). Do not add a public cursor accessor
or a new friend declaration to work around this: the existing friend is
already sufficient once the test is compiled at the right lexical scope.

Global lexical scope is necessary but **not sufficient**: `FileCacheTest.cpp`
also has a lexically-global-reachable spot (after its closing namespace
braces) where a `TEST(FileCacheTest, MoveEvictionPos)` would generate the
right C++ class — but `FileCacheTest.cpp` already defines
`facebook::velox::ch::(anonymous)::FileCacheTest`, a `TEST_F` fixture class,
and populates it with many `TEST_F(FileCacheTest, ...)` cases
(`FileCacheTest.cpp:93-175` the fixture, `:179` onward the cases). gtest
identifies a test suite by its **string name alone** ("FileCacheTest"), not by
the C++ namespace or class the macro invocation resolves to; registering a
plain `TEST(FileCacheTest, MoveEvictionPos)` in the *same test binary* as
those `TEST_F(FileCacheTest, ...)` cases is a **test-suite collision** — two
irreconcilable fixture identities sharing one suite name in one process — and
gtest fails fatally at test registration/`RUN_ALL_TESTS` time, not at compile
time. This is true regardless of which `.cpp` file the `TEST(...)` line
physically lives in, as long as it ends up linked into the same executable as
`FileCacheTest.cpp`'s `TEST_F(FileCacheTest, ...)` cases — so it is **not**
fixed by moving the line to the end of `FileCacheTest.cpp` itself, nor by
adding it to any other `.cpp` file that Task 012 still links into
`velox_ch_filecache_core_scc_test`. The only correct fix is a **separate
executable**: put `TEST(FileCacheTest, MoveEvictionPos)` in a new file that is
never linked into the same binary as `FileCacheTest.cpp`'s `TEST_F` cases.

Create `velox/ch/Interpreters/FileCache/tests/MoveEvictionPosTest.cpp`, a
small standalone test file (no fixture, no shared `SetUp`) containing, at
**global scope** (no enclosing namespace at all), exactly this one test. Since
this is a new file, `LRUFileCachePriority.h` is not yet included anywhere in
it; add `#include "velox/ch/Interpreters/FileCache/LRUFileCachePriority.h"` to
its include block, plus whatever other production headers
(`CacheMetadata`/`KeyMetadata`, `CachePriorityGuard`, `CacheStateGuard`) the
test body in the steps below needs — this is a test-file include, not a
production change. Register the new file as its own executable,
`velox_ch_filecache_priority_cursor_test`, in
`velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` (see "CMake
registration" below); do not add it to the `velox_ch_filecache_core_scc_test`
`add_executable(...)` source list, and do not add it as a new `TEST_F` case
inside the existing `FileCacheTest.cpp` — either would recreate the exact
suite collision this section exists to avoid.

#### CMake registration (new, required for B2 only)

Add, after the existing `velox_ch_filecache_core_scc_test` block in
`velox/ch/Interpreters/FileCache/tests/CMakeLists.txt`, a new, separate
`add_executable`/`add_test`/`target_link_libraries` block:

```cmake
# Task 011 corrective (B2): MoveEvictionPos cursor test. Deliberately a
# separate executable from velox_ch_filecache_core_scc_test: a global-scope
# TEST(FileCacheTest, MoveEvictionPos) (required so gtest generates the
# friend class ::FileCacheTest_MoveEvictionPos_Test that
# LRUFileCachePriority.h grants) would collide, by gtest test-suite name, with
# FileCacheTest.cpp's own TEST_F(FileCacheTest, ...) fixture cases if linked
# into the same binary.
add_executable(
  velox_ch_filecache_priority_cursor_test
  MoveEvictionPosTest.cpp
)
add_test(velox_ch_filecache_priority_cursor_test velox_ch_filecache_priority_cursor_test)

target_link_libraries(
  velox_ch_filecache_priority_cursor_test
  PRIVATE
    velox_ch_filecache_core
    velox_ch_filecache
    velox_test_util
    velox_exception
    velox_file
    velox_memory
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

This mirrors `velox_ch_filecache_core_scc_test`'s link set exactly (same
production library, `velox_ch_filecache_core`, and the same test/exception/
file/memory/Folly/fmt/GTest dependencies), because `MoveEvictionPosTest.cpp`
exercises the identical `LRUFileCachePriority`/`CacheMetadata`/`KeyMetadata`
production surface, just in its own process. This is the only CMake change
authorized by this corrective scope; it does not touch, reorder, or duplicate
any line inside the existing `velox_ch_filecache_core_scc_test` block.

**Why the friend is required at all:** the CH test calls
`src.add(std::make_shared<Entry>(...), write_lock, &state_lock)` — the
*private* `LRUIterator add(EntryPtr, const CachePriorityGuard::WriteLock &,
const CacheStateGuard::Lock *)` overload (`LRUFileCachePriority.h:265-268`),
not the public `IteratorPtr add(KeyMetadataPtr, ...)` overload
(`LRUFileCachePriority.h:116-122`) — plus `dst.move(...)` (private,
`LRUFileCachePriority.h:272-276`) and `src.setEvictionPos`/`getEvictionPos`
(private, `LRUFileCachePriority.h:280-281`). Only `resetEvictionPos`
(`:164-168`) and `getEvictionPosCount` (`:171-177`) are already public. The
corrective test may use these production priority methods and private cursor
state through the existing friend exactly as the CH test does; it must not
motivate widening the class's public interface. Do not authorize any
`LRUFileCachePriority.h`/`.cpp` change under this corrective scope unless the
test, once correctly placed and passing, exposes an actual defect in
`moveEvictionPosIfEqual` itself (see the "Production-change rule" below) —
never to work around scope/namespace/friend mismatches.

Add, at global scope in the new `MoveEvictionPosTest.cpp`,
`TEST(FileCacheTest, MoveEvictionPos)` that:

1. constructs two independent `facebook::velox::ch::LRUFileCachePriority`
   instances (`src`, `dst`) directly via the public constructor
   (`LRUFileCachePriority.h:73-78`), modelling the two SLRU sub-queues the way
   the CH test does;
2. mints a `KeyMetadataPtr` using the same construction pattern as
   `PriorityEvictionTest::SetUp`/`makeKeyMetadata`
   (`PriorityEvictionTest.cpp:47-86`: a standalone `CacheMetadata` plus
   `getKeyMetadata(..., CacheMetadata::KeyNotFoundPolicy::CREATE_EMPTY, ...)`),
   inlined directly in this test's body — a global-scope `TEST` has no
   fixture `SetUp` to share it with other cases;
3. adds three entries to `src` at offsets 0, 10, 20 via the private
   `add(EntryPtr, ...)` overload, using this test's own
   `CachePriorityGuard`/`CacheStateGuard` instances;
4. points **both** `EvictionCursor::Reserve` and `EvictionCursor::Background` at
   the middle entry (offset 10) via `setEvictionPos`;
5. calls `dst.move(it_middle, src, write_lock, state_lock)` to splice the
   middle entry out of `src`;
6. asserts **both** cursors in `src` now point at the surviving offset-20 entry
   (not the moved node, not `src.queue.end()`, not left dangling on the entry
   that now lives in `dst`);
7. separately, sets `Reserve` and `Background` to two different surviving
   positions, calls `resetEvictionPos(Reserve)`, and asserts `Background` is
   untouched (`getEvictionPosCount` for `Background` stays 1, `Reserve` becomes
   0) — this is the CH test's independence check at
   `gtest_filecache.cpp:2477-2487`.

RED: before this test exists, or with `moveEvictionPosIfEqual`'s body replaced
by a no-op, `src`'s cursors keep pointing at the spliced-out node (owned by
`dst` after the move) instead of advancing to `std::next(it)`; the assertion at
step 6 fails. Capture this failing run as the RED log.

False-green mutation: after the test passes, comment out the `*pos = std::next(it);`
line inside `moveEvictionPosIfEqual` (`LRUFileCachePriority.cpp:964-965`),
rebuild, and confirm the test fails for the same reason as the RED capture (a
dangling cross-queue cursor), not a compile error. Restore the line and
re-confirm green.

### B3a — SLRU downgrade failpoint rollback (owner: Task 011)

CH source of truth: the failpoint itself,
`src/Common/FailPoint.cpp:107` (`REGULAR(file_cache_slru_downgrade_fail_before_finalize)`)
and `src/Interpreters/FileCache/SLRUFileCachePriority.cpp:584`
(`fiu_do_on(FailPoints::file_cache_slru_downgrade_fail_before_finalize, ...)`).
No CH test arms this failpoint directly; the closest CH analog,
`TEST_F(FileCacheTest, SLRUDowngradeRollbackResetsEvictingOnSkippedFinalization)`
(`gtest_filecache.cpp:3381-3466`), proves the same rollback postcondition by
manually running only the write phase and dropping the candidates — use it as
the assertion model, but drive the failure through the real failpoint instead
of a hand-simulated skip. Velox production surface:
`SLRUFileCachePriority.cpp:414-623` (`collectCandidatesForEvictionInProtected`
and its `DowngradedEntriesInfos` RAII rollback), the trigger at
`SLRUFileCachePriority.cpp:584`, and the promotion call site
`SLRUFileCachePriority.cpp:625-768` (`tryIncreasePriority`) that reaches it
unconditionally once a probationary entry is promoted and the protected
sub-queue lacks room.

Add `TEST_F(PriorityEvictionTest, SLRUDowngradeFailpointRollsBackBeforeFinalize)`
that:

1. constructs an `SLRUFileCachePriority` with a small `max_size`/`max_elements`
   and `size_ratio` so the protected sub-queue has an exact, small capacity
   (mirror `SLRUModifySizeLimitsRollbackOnThrow`'s 30/6/0.5 shape or the CH
   rollback test's 15-byte/3-element protected limit);
2. fills the protected sub-queue to its limit via `addForRestore(...,
   QueueEntryType::SLRU_Protected, ...)`, exactly as
   `SLRUModifySizeLimitsRollbackOnThrow` already does for its own setup;
3. adds one probationary entry and calls `tryIncreasePriority(*it,
   /*is_space_reservation_complete*/true, queue_guard_, state_guard_)` so
   promotion must downgrade an existing protected entry to make room, reaching
   `collectCandidatesForEvictionInProtected`;
4. arms `file_cache_slru_downgrade_fail_before_finalize` with
   `SCOPED_TESTVALUE_SET` (same pattern as
   `SLRUModifySizeLimitsRollbackOnThrow`, `PriorityEvictionTest.cpp:273-282`)
   to throw inside the `addAfterEvictStateCallback`, i.e. after
   `afterEvictWrite` has already spliced the empty `PreActive` probationary
   entry but before any downgraded entry's size/iterator is finalized;
5. asserts, after the throw propagates out of `tryIncreasePriority`:
   - every protected entry that was queued for downgrade is back in
     `Entry::State::Active`, not left in `Evicting` or `Moving`
     (`DowngradedEntryInfo::rollbackState`, `SLRUFileCachePriority.cpp:503-514`);
   - `getProtectedSize`/`getProbationarySize` equal their pre-promotion values
     (no byte was left double-counted or dropped);
   - the original protected entry's queue iterator identity is unchanged (the
     spliced-in empty `PreActive` probationary entry was invalidated by
     rollback, not left live and orphaned).

RED: before this test exists, or with the `DowngradedEntriesInfos` destructor's
rollback loop (`SLRUFileCachePriority.cpp:535-541`) replaced by a no-op body,
the failpoint throw leaks a protected entry stuck in `Evicting`/`Moving` and/or
a live orphaned `PreActive` probationary entry; the size/state assertions in
step 5 fail. Capture this failing run as the RED log.

False-green mutation: after the test passes, remove the
`for (auto & entry : *this) entry.rollbackState();` loop from
`~DowngradedEntriesInfos` (or replace the body with a no-op), rebuild, and
confirm the test fails for the declared reason (stuck `Evicting` state or
size mismatch), not a compile error. Restore the loop and re-confirm green.

### B3b — SLRU dynamic-resize eviction from both sub-queues (owner: Task 011)

CH source of truth: `src/Interpreters/tests/gtest_filecache.cpp:2048-2131`
(`TEST_F(FileCacheTest, SLRUDynamicResizeCorrectEviction)`) and the fix it
guards, `src/Interpreters/FileCache/SLRUFileCachePriority.cpp:862-884`
(`SLRUFileCachePriority::collectEvictionInfoForResize` delegating separately to
`protected_queue`/`probationary_queue` with per-sub-queue desired limits, so
neither sub-queue's shrink silently short-circuits). Velox production surface:
`SLRUFileCachePriority.cpp:852-874` (`collectEvictionInfoForResize`),
`SLRUFileCachePriority.cpp:823-851` (`modifySizeLimits`). The CH test drives
this through the full `FileCache`/`applySettingsIfPossible` stack; Task 011 has
no `FileCache` in scope, so this corrective test exercises
`SLRUFileCachePriority` directly at the priority level, matching this file's
existing direct-construction style.

Add `TEST_F(PriorityEvictionTest, SLRUDynamicResizeEvictsFromBothSubQueues)`
that:

1. constructs an `SLRUFileCachePriority` with the CH test's exact shape
   (`max_size=30, max_elements=6, size_ratio=0.5` → protected 15/3,
   probationary 15/3);
2. populates both sub-queues to their limit (3 protected entries via
   `addForRestore(..., SLRU_Protected, ...)`, 2-3 probationary entries via
   `addForRestore(..., SLRU_Probationary, ...)`, mirroring the CH test's
   15-bytes-protected / 10-bytes-probationary starting state);
3. calls `collectEvictionInfoForResize(/*desired_max_size*/8,
   /*desired_max_elements*/6, origin, state_lock)` — a shrink so aggressive
   that **both** sub-queues (protected limit 4, probationary limit 4) must
   contribute eviction candidates, matching the CH regression's exact
   8-byte/6-element target;
4. asserts the returned `EvictionInfo` requires eviction from both the
   protected and probationary portions (not just one), then drives
   `collectCandidatesForEviction`/`evict()`/`afterEvictWrite`/`afterEvictState`
   to completion and calls `modifySizeLimits(8, 6, 0.5, state_lock)`;
5. asserts the final `getProtectedSize`/`getProbationarySize`/
   `getElementsCount` all satisfy the new 8-byte/6-element limits, and that
   `modifySizeLimits` does not throw `LOGICAL_ERROR` (the CH regression's core
   assertion, `gtest_filecache.cpp:2124` `ASSERT_NO_THROW`).

RED: before this test exists, or with
`SLRUFileCachePriority::collectEvictionInfoForResize`'s probationary-delegation
line (`SLRUFileCachePriority.cpp:868-870`, `info->add(probationary_queue...)`)
removed so only the protected sub-queue is asked to shrink, the probationary
sub-queue stays over its new 4-byte limit and either the final size assertion
in step 5 fails or `modifySizeLimits` throws `LOGICAL_ERROR` because
`state->getSize(lock) > max_size_` for the probationary sub-queue. Capture this
failing run as the RED log.

False-green mutation: after the test passes, delete the
`info->add(probationary_queue.collectEvictionInfoForResize(...))` call
(`SLRUFileCachePriority.cpp:868-870`), rebuild, and confirm the test fails for
the declared reason. Restore the call and re-confirm green.

### Production-change rule (B2/B3, all three tests)

No production change is expected from B2/B3. If, and only if, one of the three
tests above exposes a real defect in `moveEvictionPosIfEqual`, the
`DowngradedEntriesInfos` rollback, or `collectEvictionInfoForResize`, fix the
minimal defect, document it in the corrective receipt (root cause, exact
lines changed, before/after test outcome), and keep the fix inside this task's
already-declared file scope. Do not add a fallback, a silent catch, or a new
typed exception category to route around a failing assertion. In particular,
`LRUFileCachePriority.h`'s existing `friend class
::FileCacheTest_MoveEvictionPos_Test` (`:201`) is sufficient for B2 once the
test is placed at global scope in the new, standalone
`MoveEvictionPosTest.cpp` (see the B2 section above); do not modify
`LRUFileCachePriority.h`/`.cpp` to add a public cursor accessor or an
additional friend to route around a scope/namespace/suite-collision mismatch —
that is not a production defect.

### Mono/non-mono and accumulated gates

This corrective work touches two separate build artifacts: `PriorityEvictionTest.cpp`
(B3a/B3b), which Task 012 already compiles into `velox_ch_filecache_core_scc_test`
(no CMake change), and the new `MoveEvictionPosTest.cpp` (B2), compiled into
its own new `velox_ch_filecache_priority_cursor_test` executable (CMake change
required — see "CMake registration" above). Closing this corrective task
requires the **same build closure Task 012 already proved for the existing
target, plus a first-time green build of the new one**:

```text
1. Build and run velox_ch_filecache_core_scc_test in the existing mono
   configuration (<velox_build_dir>), full suite green (B3a/B3b added).
2. Build and run velox_ch_filecache_core_scc_test in the existing non-mono
   configuration (VELOX_MONO_LIBRARY=OFF), full suite green (B3a/B3b added).
3. Build and run the new velox_ch_filecache_priority_cursor_test in the same
   mono configuration, green (1/1, B2).
4. Build and run the new velox_ch_filecache_priority_cursor_test in the same
   non-mono configuration, green (1/1, B2).
5. Re-run the accumulated mono CTest set already established by Task 012
   (`port/task/result/012-filecache-core-scc-result.md` records
   101/101 focused + 11/11 accumulated) and confirm it now also includes
   velox_ch_filecache_priority_cursor_test, with the focused
   velox_ch_filecache_core_scc_test count increasing by exactly the number of
   B3a/B3b tests added, the accumulated CTest count increasing by exactly one
   (the new executable's CTest entry), and zero regressions elsewhere.
```

Record all eight logs (mono/non-mono focused for both executables, mono
accumulated, plus the RED and false-green mutation logs for each of the three
tests) in the corrective receipt.

### Corrective file scope

Modify (already exists, already registered — no CMake change):

```text
<velox_repo>/velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp
  (B3a, B3b)
```

Create (new file) and register (new CMake target — the only CMake change
authorized by this corrective scope):

```text
<velox_repo>/velox/ch/Interpreters/FileCache/tests/MoveEvictionPosTest.cpp
  (B2: a global-scope TEST(FileCacheTest, MoveEvictionPos) in a standalone
  file, deliberately not added to FileCacheTest.cpp or
  PriorityEvictionTest.cpp, to avoid a gtest test-suite collision with
  FileCacheTest.cpp's own TEST_F(FileCacheTest, ...) fixture cases — see the
  B2 section above)
<velox_repo>/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
  (add the new velox_ch_filecache_priority_cursor_test executable/add_test
  block; do not modify the existing velox_ch_filecache_core_scc_test block)
```

Do not add `MoveEvictionPosTest.cpp`'s sources to the existing
`velox_ch_filecache_core_scc_test` `add_executable(...)` list, and do not put
B2's test case inside `FileCacheTest.cpp` or `PriorityEvictionTest.cpp`: both
would recreate the suite collision (or the friend/namespace mismatch) this
corrective scope exists to avoid.

Do not modify `LRUFileCachePriority.h`/`.cpp` (or any other production header)
as part of this corrective scope unless one of the B2/B3 tests, once correctly
implemented and placed, exposes an actual production defect per the
"Production-change rule" above.

Append to (append-only, per `EXECUTION_PROTOCOL.md`'s state machine):

```text
<clickhouse_repo>/port/task/result/011-filecache-priority-eviction-result.md
```

### Corrective stop conditions

Stop as `blocked` and escalate instead of improvising if:

```text
velox_ch_filecache_core_scc_test does not build green in mono/non-mono before
  this corrective work starts (Task 012's own gate regressed; that is a
  Task-012 defect, not a Task-011 one).
tryIncreasePriority's promotion path does not reach
  collectCandidatesForEvictionInProtected for the constructed scenario (the
  test setup does not exercise the failpoint; do not arm the failpoint via a
  different, unreviewed code path).
a required assertion can only pass by weakening it, adding a sleep, or
  skipping/disabling the test.
the global-scope TEST(FileCacheTest, MoveEvictionPos) in the new,
  standalone MoveEvictionPosTest.cpp still fails to access
  LRUFileCachePriority's private members after being placed exactly as
  specified (a friend/scope mismatch): stop as blocked and report the exact
  compiler diagnostic; do not add a public accessor or a new friend
  declaration to work around it.
gtest reports a test-suite registration/fatal error naming "FileCacheTest" at
  RUN_ALL_TESTS time for either velox_ch_filecache_core_scc_test or the new
  velox_ch_filecache_priority_cursor_test (the suite collision this section
  exists to avoid): stop as blocked and report which two binaries or
  translation units the colliding TEST/TEST_F registrations came from; do not
  rename the test suite away from "FileCacheTest" to work around it (that
  would break the friend-class name match) and do not merge the two
  executables back together.
```


## Prerequisites

```text
Tasks 003-010 completed in the same Velox worktree.
Task 003's B1/B2 corrective work (velox/ch/Common/ProfileEvents.h and
  CurrentMetrics.h name surfaces) is accepted, not merely
  reopened_by_contract_audit. Verify by reading the accepted Controller
  review in port/task/result/003-filecache-basic-common-shims-result.md
  before starting.
Velox branch: filecache
Task 012 is ready to run immediately after this task.
```

Read:

```text
port/task/ENVIRONMENT.md
port/task/fullreview/cross-profile/1/003-010-review-decisions.md
port/2-file-cache/07-filecache-priority-eviction-design.md
port/2-file-cache/10-filecache-core-files-design.md
port/task/result/003-filecache-basic-common-shims-result.md
port/task/result/010-filecache-settings-result.md
```

Behavioral source of truth:

```text
src/Interpreters/FileCache/CacheUsage.h
src/Interpreters/FileCache/IFileCachePriority.h / .cpp
src/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
src/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
src/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
src/Interpreters/FileCache/EvictionCandidates.h / .cpp
```

## File scope

Create:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/CacheUsage.h
<velox_repo>/velox/ch/Interpreters/FileCache/IFileCachePriority.h
<velox_repo>/velox/ch/Interpreters/FileCache/IFileCachePriority.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/LRUFileCachePriority.h
<velox_repo>/velox/ch/Interpreters/FileCache/LRUFileCachePriority.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h
<velox_repo>/velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/SplitFileCachePriority.h
<velox_repo>/velox/ch/Interpreters/FileCache/SplitFileCachePriority.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/EvictionCandidates.h
<velox_repo>/velox/ch/Interpreters/FileCache/EvictionCandidates.cpp
<clickhouse_repo>/port/task/result/011-filecache-priority-eviction-result.md
```

Do not create or modify CMake files in this task's original Steps 1-9. Task 012
owns CMake registration for those. The Review-2 corrective scope above is the
exception, in two different ways: it modifies the already-registered
`<velox_repo>/velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp`
by adding `TEST_F` cases to it (that file's CMake registration was already
performed by Task 012 and must not be touched again here), **and** it creates
a genuinely new file, `<velox_repo>/velox/ch/Interpreters/FileCache/tests/
MoveEvictionPosTest.cpp`, together with a matching new CMake block in
`<velox_repo>/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt` (see "CMake
registration" and "Corrective file scope" above) — this is the one CMake edit
this corrective scope both requires and authorizes.

## Required replacements

Apply only reviewed infrastructure substitutions:

```text
DB namespace                         -> facebook::velox::ch
String/UInt*                         -> Task 003 aliases
Exception/ErrorCodes/chassert        -> VELOX_FAIL/VELOX_CHECK
absl flat maps/sets                  -> folly F14 with explicit hash
CurrentMetrics/ProfileEvents/logging -> existing compatible shims
boost::noncopyable                   -> deleted copy operations
magic_enum                           -> explicit exhaustive switch
```

The `CurrentMetrics`/`ProfileEvents` shims already exist and stay no-op (Task
003). The priority/eviction sources reference exactly this subset of names;
every one must already resolve from the Task-003 B1/B2 name surfaces:

```text
Task-011 ProfileEvents names:
  FilesystemCacheBackgroundRemovedInvalidatedEntries
  FilesystemCacheDowngradedFileSegments
  FilesystemCacheEvictionReusedIterator
  FilesystemCacheEvictionSkippedEvictingFileSegments
  FilesystemCacheEvictionSkippedFileSegments
  FilesystemCacheEvictionSkippedMovingFileSegments
  FilesystemCacheEvictionTries
  FilesystemCacheEvictMicroseconds
  FilesystemCacheFailedEvictionCandidates

Task-011 CurrentMetrics names:
  FilesystemCacheElements
  FilesystemCacheInvalidatedElements
  FilesystemCachePriorityQueueElements
  FilesystemCacheSize
```

If any of these names is missing from `velox/ch/Common/ProfileEvents.h` or
`velox/ch/Common/CurrentMetrics.h`, stop at the dependency gate: Task 003's
B1/B2 corrective work is not actually complete, regardless of its receipt
status.

Do not copy:

```text
OvercommitFileCachePriority
CacheUsagePerUser (snapshot/getOrSet/touchClient/collectIdleClients bodies)
CurrentMetrics::FilesystemCacheOvercommitUsers
Cloud-only distributed-cache branches
SQL/system-table presentation
```

## Exact invariants

The port is incomplete unless all of these are visible in the migrated source:

```text
zero-size queue entry:
  counts neither bytes nor elements until first positive increment

LRU:
  std::list entry storage
  reserve and background eviction cursors are independent
  invalidation is lazy and iterator-safe

SLRU:
  probationary + protected queues
  PreActive transition
  external SLRUIterator updated when an entry moves queues
  addForRestore restores original queue type

Split:
  General and Data route to Data
  System routes to System
  size and element limits both split by ratio
  failed System resize rolls Data limits back

EvictionInfo:
  map keyed by QueueID
  aggregate bytes/elements/holds
  keptAliveCacheUsage transferred on merge
  total-space cleanup uses min(requested,current)

EvictionCandidates:
  evict without priority/state locks
  afterEvictWrite before afterEvictState
  removeQueueEntries for resize
  requiresAfterEvictWrite/requiresAfterEvictState
  original queue type capture and lookup
  failed candidate accounting
```

## Steps

- [ ] **Step 1: Confirm the atomic-batch baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected:

```text
Branch is filecache.
Tasks 003-010 files are present.
Record pre-existing dirty files; do not overwrite them.
```

- [ ] **Step 2: Port `CacheUsage` and `IFileCachePriority`**

Migrate `CacheUsage.h` and `IFileCachePriority.h/.cpp` from the ClickHouse
sources line by line, applying only the replacements above.

From `CacheUsage.h`, port only the non-overcommit subset registered above:
`CacheUsageStatGuard`, `CacheUsage` (constructor, `update`, `total_size`,
`total_elements`, `touch`, `idleFor`, `operator<`, `operator==`,
`lessWithAssumption`), and `CacheUsagePtr`. **Do not** port `CacheUsagePerUser`,
its nested `CacheUserData`, or any `CurrentMetrics::FilesystemCacheOvercommitUsers`
reference; they have no reachable caller in this stage (see the consumer-contract
excerpts above) and belong to the excluded `OvercommitFileCachePriority`.

From `IFileCachePriority.h/.cpp`, keep the full public interface, including:

```text
Entry/Iterator/HoldSpace
QueueType/QueueEntryType
EvictionCursor/CollectStatus
collectEvictionInfo
collectCandidatesForEviction
add/addForRestore/tryIncreasePriority
removeInvalidatedEntries
modifySizeLimits
getSize/getElementsCount/getStateInfoForLog
setCacheUsageStatGuard/touchClientAccess/collectIdleClients (base no-op hooks only)
```

Use forward declarations for `FileSegment`, `FileSegmentInfo`,
`FileCacheReserveStat`, `KeyMetadata`, and `EvictionCandidates`, exactly as the
CH headers do. Do not define them here.

- [ ] **Step 3: Port `LRUFileCachePriority`**

Migrate `LRUFileCachePriority.h/.cpp` with the exact list/state/cursor
algorithms. Preserve both `collectEvictionInfo` branches:

```text
is_total_space_cleanup == true:
  target = min(requested,current)

otherwise:
  target = shortage relative to configured limits and held space
```

Do not replace `std::list` or either eviction cursor.

- [ ] **Step 4: Port `SLRUFileCachePriority`**

Migrate `SLRUFileCachePriority.h/.cpp`. Preserve `SLRUIterator` identity,
`entry_mutex`, `is_protected`, `PreActive`, promotion/downgrade and
`addForRestore`. Do not flatten the two queues.

- [ ] **Step 5: Port `SplitFileCachePriority`**

Migrate `SplitFileCachePriority.h/.cpp`. Compute all four limits:

```text
data bytes
data elements
system bytes
system elements
```

Preserve rollback if the second sub-priority resize throws.

- [ ] **Step 6: Port `EvictionCandidates`**

Migrate `EvictionCandidates.h/.cpp`. Keep its real dependencies on
`Metadata`/`FileSegment`; do not replace them with test doubles or local
structs. It is expected to remain unregistered until Task 012.

- [ ] **Step 7: Run structural parity checks**

First capture the RED baseline before any of these files exist (or, if
re-running after a partial attempt, on a clean checkout at the Step-1 HEAD):

```bash
cd <velox_repo>
for symbol in \
  'enum class EvictionCursor' \
  'class HoldSpace' \
  'class EvictionInfo' \
  'class EvictionCandidates' \
  'removeQueueEntries' \
  'requiresAfterEvictWrite' \
  'requiresAfterEvictState' \
  'addForRestore' \
  'is_total_space_cleanup'
do
  rg -n "$symbol" velox/ch/Interpreters/FileCache \
    >> <velox_build_dir>/check_task_011_priority_symbols_red.log
done
```

Expected: `check_task_011_priority_symbols_red.log` is empty (zero matches);
none of these symbols exist yet. This is the RED evidence.

Then, after implementing Steps 2-6, run the same checks for the final record:

```bash
cd <velox_repo>

for symbol in \
  'enum class EvictionCursor' \
  'class HoldSpace' \
  'class EvictionInfo' \
  'class EvictionCandidates' \
  'removeQueueEntries' \
  'requiresAfterEvictWrite' \
  'requiresAfterEvictState' \
  'addForRestore' \
  'is_total_space_cleanup'
do
  rg -n "$symbol" velox/ch/Interpreters/FileCache \
    >> <velox_build_dir>/check_task_011_priority_symbols.log
done

if rg -n 'FileCacheReserveStat.*stub|struct KeyMetadata.*stub|class FileCache.*stub' \
  velox/ch/Interpreters/FileCache
then
  echo "ERROR: fake SCC definitions found"
  exit 1
fi

if rg -n 'CacheUsagePerUser|FilesystemCacheOvercommitUsers' \
  velox/ch/Interpreters/FileCache
then
  echo "ERROR: excluded overcommit surface was ported"
  exit 1
fi

git --no-pager diff --check
```

Expected:

```text
Every required symbol is found.
No fake SCC definition is found.
No CacheUsagePerUser / FilesystemCacheOvercommitUsers reference is found.
No whitespace error is reported.
```

Finally, run the false-green mutation probe described above: delete the
`requiresAfterEvictWrite` declaration from the migrated `EvictionCandidates.h`,
rerun the symbol loop, confirm it no longer finds a match for that symbol,
save that output as `check_task_011_priority_symbols_mutated.log`, then
restore the declaration and rerun the final Step-7 command to confirm a clean
pass again.

Do not run or claim a priority build in Task 011. The green build belongs to
Task 012.

- [ ] **Step 8: Inspect task-owned files**

```bash
cd <velox_repo>
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Interpreters/FileCache/CacheUsage.h \
  velox/ch/Interpreters/FileCache/IFileCachePriority.h \
  velox/ch/Interpreters/FileCache/IFileCachePriority.cpp \
  velox/ch/Interpreters/FileCache/LRUFileCachePriority.h \
  velox/ch/Interpreters/FileCache/LRUFileCachePriority.cpp \
  velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h \
  velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp \
  velox/ch/Interpreters/FileCache/SplitFileCachePriority.h \
  velox/ch/Interpreters/FileCache/SplitFileCachePriority.cpp \
  velox/ch/Interpreters/FileCache/EvictionCandidates.h \
  velox/ch/Interpreters/FileCache/EvictionCandidates.cpp
```

Expected:

```text
Only declared files were changed by this task.
All changes remain unstaged and uncommitted.
```

- [ ] **Step 9: Write the atomic Part-A result**

Create:

```text
<clickhouse_repo>/port/task/result/011-filecache-priority-eviction-result.md
```

Include:

```text
status: success
Velox branch/HEAD/dirty status
files created
RED structural-check log path (pre-implementation, zero matches)
final structural-check log path (post-implementation, all matches found)
false-green mutation log path (one symbol deleted, check fails) and the
  restored/passing rerun
explicit statement of the CacheUsage scope actually ported (base CacheUsage +
  CacheUsageStatGuard only; CacheUsagePerUser and
  FilesystemCacheOvercommitUsers not ported)
explicit statement: "No build claimed; Task 012 closes the atomic SCC batch"
first actionable error if any
recommended next task: Task 012 immediately
```

If any priority source needs a fake core definition to proceed, set status to
`blocked`, report the dependency, and stop. Do not invent the definition.

## Explicit exclusions

```text
CMake registration and priority tests (Task 012)
FileSegmentInfo/FileSegment/Metadata/FileCache/QueryLimit (Task 012)
Factory/Manager (Task 013)
OvercommitFileCachePriority
```
