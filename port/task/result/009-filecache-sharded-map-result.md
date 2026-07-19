# Task 009 Result: `FileCache` `ShardedMap`

## Worker attempt 1

```text
worker_status: blocked
environment_profile: root-oss
task: 009
```

Blocked during the mandatory pre-edit dependency/contract preflight, before any
Velox source or CMake file was modified. The blocker is a file-scope conflict:
`ShardedMap.h` must be registered in the existing non-mono `PUBLIC HEADERS`
`FILE_SET`, but that registration requires editing
`velox/ch/Interpreters/FileCache/CMakeLists.txt`, which the declared Task-009
file scope does not permit. Per the controller's preflight instruction and
`EXECUTION_PROTOCOL.md` worker rule 4, the correct action is to block rather than
silently omit the registration or expand scope.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/clickhouse` | `ch-filecache` | `4a168b41ae497efdb9dad3ddd81d87f99b14510f` | clean (no tracked/untracked changes) |
| `/root/oss/velox` | `filecache` | `24686d2c68831566439911eec8a69287e6fa39e3` | clean (no tracked/untracked changes) |

`git log -1 --oneline`:
- ClickHouse: `4a168b41ae4 Task 008: Accept corrected leaf-type contracts`
- Velox: `24686d2c6 Task 008: Restore key parser compatibility`

The Task-009 receipt did not previously exist. Task 008 is the accepted
prerequisite (leaf types, `FileCacheOriginInfo.h`, `OriginPoolKeyHash`,
tests/CMake infrastructure).

## Files changed

```text
/root/oss/clickhouse/port/task/result/009-filecache-sharded-map-result.md   (this receipt only)
```

No Velox files were created or modified. `ShardedMap.h`, `ShardedMapTest.cpp`,
and the `tests/CMakeLists.txt` edit were intentionally NOT written, because the
task cannot be completed correctly and completely within its declared file scope
(see Blockers). Both implementation repositories remain clean.

## Preflight: derived behavioral contract (CH source + real callers)

Source of truth: `src/Interpreters/FileCache/ShardedMap.h` and its two real
callers `CacheMetadata::origins` (`src/Interpreters/FileCache/Metadata.{h,cpp}`)
and `CacheUsagePerUser::clients_map` (`src/Interpreters/FileCache/CacheUsage.h`).

| Aspect | CH behavior (authoritative) | Approved Velox port target |
|---|---|---|
| Template | `template<typename Key, typename Value, size_t num_shards=32>` | add `typename Hash=std::hash<Key>` as 4th param (design 05) |
| Guard | `static_assert(num_shards>0)` (design 05) | same, before members |
| Map | `std::unordered_map<Key,Value>` | `folly::F14FastMap<Key,Value,Hash>` (design 05) |
| Shard selection | `std::hash<Key>{}(key) % num_shards` | `Hash{}(key) % num_shards`, same `Hash` used for F14 internal hashing |
| Shard struct | `struct Shard { mutable std::mutex mutex; Map map; }` | identical |
| Members | `const ProfileEvents::Event lock_wait_event; mutable std::array<Shard,num_shards> shards; mutable std::atomic<size_t> total_count{0}` | same, Velox `_`-suffixed names |
| Constructor | `explicit ShardedMap(ProfileEvents::Event lock_wait_event_)` | identical signature; store value; no-op first phase (design 05) |
| Lock | `DB::ProfiledMutexLock(shard.mutex, lock_wait_event)` | `std::unique_lock<std::mutex>` + no-op profile shim (design 05); one mutex per shard; lock covers callback only, never spans shards |
| `withShard` | `const`; lock owning shard; `size_before=map.size()`; `SCOPE_EXIT(accountSizeDelta(...))`; `return f(shard.map)` | `const`; generic `F` supporting void AND value return by value; callback runs under lock; exception-safe size guard fires on throw |
| `forEachShard` | `const`; iterate `shards` in array order; lock ONE shard at a time; same accounting; `f(shard.map)`; void | identical; sequential, NOT simultaneous; no global snapshot; must not lock all shards at once |
| `size()` | `total_count.load(relaxed)` | same; `noexcept`; relaxed atomic snapshot, not a correctness value |
| `accountSizeDelta` | `after>before`→`fetch_add(after-before,relaxed)`; `after<before`→`fetch_sub(before-after,relaxed)` | identical; `noexcept`; guard itself must not throw |
| Copyability | `private boost::noncopyable` | explicitly deleted copy ctor + copy assignment |
| Callback contract | mutate its shard map; return value BY VALUE; no escaping iterator/reference/pointer; no same-map recursion (same-shard reentry deadlocks) | document all three restrictions in header |

Caller-observed contract:
- `getOrCreateSharedOrigin`: `origins.withShard(pool_key, [&](auto & map) ->
  OriginInfoPtr { find/emplace; return it->second; })` — returns a **copied**
  `shared_ptr<const OriginInfo>` by value; nothing escapes the lock.
- `removeSharedOrigins`: `origins.forEachShard([&](auto & map){ erase entries
  whose it->first.user_id == user_id; })` — per-shard best-effort cleanup.
- `origins` is constructed with
  `ProfileEvents::FilesystemCacheLockOriginPoolMicroseconds` (Metadata.cpp:226).
- `CacheUsagePerUser::size()` returns `clients_map.size()`. The
  `withShard`/`forEachShard` bodies of `CacheUsagePerUser` (overcommit path) are
  not present on this branch; design 05 defers overcommit and confirms the origin
  pool is the metadata main path. These belong to Tasks 011/012.

## Preflight: external dependency review (all individually reviewed/available)

```text
folly::F14FastMap (folly/container/F14Map.h)   reviewed design 05; already used by accepted FileCacheOriginInfo.h (Task 008) — available
ProfileEvents::Event + no-op increment          reviewed design 05; exists in velox/ch/Common/ProfileEvents.h
OriginPoolKey / OriginPoolKeyHash               reviewed design 02; exists in velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h (user_id-only hash, full equality)
boost::noncopyable -> deleted copy ops          reviewed design 05
DB::ProfiledMutexLock -> std::unique_lock+shim   reviewed design 05
SCOPE_EXIT/base/scope_guard.h -> local RAII/Folly scope guard, same exception behavior   reviewed design 05
test link targets velox_ch_filecache, velox_exception, Folly::folly, fmt::fmt, GTest::gtest[/main]   exist (used by velox_ch_guards_test)
```

No unreviewed behavioral dependency was found. The only blocking issue is the
CMake public-header registration scope conflict below.

Non-blocking observation for a later task (NOT a Task-009 issue): the production
event `ProfileEvents::FilesystemCacheLockOriginPoolMicroseconds` used by
`Metadata.cpp` when constructing `origins` is not yet in the Velox
`ProfileEvents` enum. Task 009 does not port `Metadata.cpp`; its test uses the
already-present `FilesystemCacheGetOrSetMicroseconds`, and the header only stores
the passed `Event`. Adding that enum value belongs to the task that ports
`Metadata` (Task 012), not here.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| Baseline/status (`git status`, `git log`) both repos | 0 | (inline above; read-only, no persistent log) |
| Convention audit: directory headers vs FILE_SET | 0 | (inline below; read-only) |
| Task 008 precedent: `git show 4b14de7f1 -- .../CMakeLists.txt` | 0 | (inline below; read-only) |

No configure/build/test/mutation commands were run: the task was blocked during
the pre-edit preflight, so `configure_task_009_sharded_map.log`,
`build_task_009_red.log`, `build_task_009_sharded_map.log`, and
`test_task_009_sharded_map.log` were intentionally not produced.

## Acceptance evidence

```text
test count: not reached (blocked before edits)
failed tests: not reached
skipped/disabled tests: not reached
RED evidence: not reached (would require creating ShardedMapTest.cpp)
behavioral mutation proofs: not reached (post-implementation step)
benchmark result: n/a
git diff --check: n/a (no edits made; both repos clean)
```

## Worker review

```text
review subagent: not launched — the read-only diff review is a post-implementation
  step; there is no task-owned diff to review because the task blocked during
  pre-edit preflight.
findings: n/a
resolutions: n/a
unresolved findings: the scope/registration conflict recorded under Blockers,
  which requires a controller/user decision (task-scope amendment) and cannot be
  resolved by the worker within the declared scope.
```

## Blockers

```text
BLOCKER: Task-009 file scope forbids the CMake public-header registration that the
repository's universal convention requires for the new public header ShardedMap.h.

Evidence:
1. ShardedMap.h is a public FileCache API header. In CH it is included by the
   public headers Metadata.h and CacheUsage.h; it is the same category as
   FileCacheOriginInfo.h. It is header-only (a class template, no .cpp), exactly
   like the already-registered FileCacheUtils.h and FileCacheOriginInfo.h.

2. Universal convention (zero exceptions). Every public header in
   velox/ch/Interpreters/FileCache/ is registered in the non-mono
   "PUBLIC HEADERS" FILE_SET of velox/ch/Interpreters/FileCache/CMakeLists.txt:
     directory headers : FileCache_fwd.h, FileCache_fwd_internal.h, FileCacheKey.h,
                          FileCacheOriginInfo.h, FileCacheUtils.h,
                          FileSegmentKeyType.h, Guards.h   (7 headers)
     FILE_SET headers   : the same 7 headers
   ShardedMap.h would be the ONLY public header in that directory not registered.

3. Task 008 precedent (register-on-creation, even without a production consumer).
   Commit 4b14de7f1 ("Task 008: Add FileCache leaf types") added
   FileSegmentKeyType.h, FileCacheOriginInfo.h, FileCache_fwd.h,
   FileCache_fwd_internal.h, FileCacheKey.h, and FileCacheUtils.h to that FILE_SET.
   At Task 008 those headers had NO production consumer — their only consumer in
   the Velox tree is tests/LeafTypesTest.cpp — yet Task 008 registered them
   immediately, and its declared file scope explicitly included
   velox/ch/Interpreters/FileCache/CMakeLists.txt for exactly this purpose.
   ShardedMap.h is directly analogous: a public header whose only Task-009
   consumer is a test (ShardedMapTest.cpp).

4. Task-009 declared file scope (009-filecache-sharded-map.md, "File scope"):
     Create: ShardedMap.h, tests/ShardedMapTest.cpp, this result file
     Modify: velox/ch/Interpreters/FileCache/tests/CMakeLists.txt   (tests only)
   The parent velox/ch/Interpreters/FileCache/CMakeLists.txt is NOT in scope, and
   Task-009 Step 4 provides no step to register ShardedMap.h in the FILE_SET.

5. No downstream task registers it either. No occurrence of "ShardedMap.h" exists
   in tasks 010-013. Task 012 modifies the parent CMakeLists.txt but its Step 15
   only adds compiled core sources (velox_add_library velox_ch_filecache_core ...)
   and preserves existing entries; it does not add ShardedMap.h to the FILE_SET.
   So omitting it in Task 009 leaves it permanently unregistered.

Why this must block (not proceed, not expand scope):
- EXECUTION_PROTOCOL.md worker rule 4: modify only the declared file scope; if
  another file is required, stop as blocked instead of silently expanding scope.
- The controller's explicit preflight instruction: if ShardedMap.h must be
  registered in the non-mono PUBLIC HEADERS FILE_SET but the declared Task-009
  file scope does not permit the required CMake file, write a blocked receipt and
  stop, rather than silently omit registration or expand scope.
- Proceeding under the root-oss mono build (VELOX_MONO_LIBRARY=ON, where the
  FILE_SET block is skipped) would make the acceptance gate pass while leaving a
  real non-mono install/IDE-completeness defect and the sole convention exception.

Exact decision needed from the controller/user (one of):
(a) PREFERRED — Amend Task 009 file scope to add
    velox/ch/Interpreters/FileCache/CMakeLists.txt to "Modify", and add a step to
    append ${CMAKE_CURRENT_SOURCE_DIR}/ShardedMap.h to the existing non-mono
    "PUBLIC HEADERS" FILE_SET (mirroring the Task 008 registration). Then
    redispatch Task 009.
(b) Explicitly waive FILE_SET registration for ShardedMap.h (e.g. a documented
    rule that header-only, test-only-consumed headers defer registration to the
    task that introduces their production consumer), record that decision in the
    canonical design (05-filecache-sharded-map-design.md) and a Task-009
    amendment, and redispatch.
(c) Assign the registration to Task 012 with an explicit written FILE_SET step,
    record it in the Task 012 contract, and redispatch Task 009 unchanged.

Any of (a)/(b)/(c) must be written into the canonical design and/or the numbered
task before redispatch, per the unreviewed-dependency / scope gate.
```

## Worker declaration

```text
Only Task 009 was attempted.
No Velox files were created, modified, staged, or committed; both implementation
repositories remain clean at their baseline HEADs.
This receipt is unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Controller unblock response 1

```text
controller_status: blocker_resolved
environment_profile: root-oss
task: 009
```

## Resolution

```text
root cause:
  Task 009 creates a public header but omitted the parent FileCache CMakeLists
  from its file scope. The universal directory convention and Task-008 precedent
  require immediate non-mono PUBLIC HEADERS registration.

decision:
  Add velox/ch/Interpreters/FileCache/CMakeLists.txt to Task-009 scope, append
  ShardedMap.h to the existing non-mono file set, reduce the focused test's
  direct links to velox_ch_filecache plus GTest, and require a separate non-mono
  consumer build/test.

task or environment update:
  Updated the canonical ShardedMap design and Task 009 with the registration,
  consumer-interface, and non-mono proof contract.

evidence:
  Every existing public FileCache header is registered; ShardedMap.h would
  otherwise be the sole exception. The existing velox_ch_filecache PUBLIC
  interface already carries Folly/fmt/velox_exception requirements.

redispatch same task: yes
```

## Worker attempt 2

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 009
```

Redispatched after `Controller unblock response 1` (`blocker_resolved`,
`redispatch same task: yes`). The attempt-1 blocker — the parent
`FileCache/CMakeLists.txt` was outside the declared file scope needed to register
the new public header — is resolved by the Controller amendment now present in
`port/task/009-filecache-sharded-map.md` ("Controller amendment after Worker
attempt 1"), which adds that file to `Modify`, requires appending
`${CMAKE_CURRENT_SOURCE_DIR}/ShardedMap.h` to the existing non-mono `PUBLIC
HEADERS` file set, reduces the focused test's direct links to `velox_ch_filecache`
+ GTest, and mandates a separate non-mono consumer build/test.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/clickhouse` | `ch-filecache` | `83b5521a2de` | clean |
| `/root/oss/velox` | `filecache` | `24686d2c68831566439911eec8a69287e6fa39e3` | clean |

`git log -1 --oneline`:
- ClickHouse: `83b5521a2de Task 009: Resolve public header registration scope`
- Velox: `24686d2c6 Task 008: Restore key parser compatibility`

HEADs are unchanged at completion (no commit/stage performed).

## Preflight: contract re-derivation and blocker resolution

Re-derived the `ShardedMap` contract independently from the CH source
(`src/Interpreters/FileCache/ShardedMap.h`) and its two real callers
(`CacheMetadata::origins` in `Metadata.cpp` `getOrCreateSharedOrigin` /
`removeSharedOrigins`, and `CacheUsagePerUser::clients_map` in `CacheUsage.h`),
matching design `05-filecache-sharded-map-design.md`:

- `template<typename Key, typename Value, size_t num_shards=32, typename
  Hash=std::hash<Key>>`; `static_assert(num_shards>0)`.
- `Map = folly::F14FastMap<Key,Value,Hash>`; same `Hash` for shard selection and
  F14 internal hashing; `shards_[Hash{}(key) % num_shards]`.
- one `std::mutex` per shard; lock covers the callback only, never spans shards.
- `withShard` const: lock owning shard, record `size_before`, install
  exception-safe size guard (fires even if the callback throws), invoke `f(map)`,
  return the callback result **by value**.
- `forEachShard` const: iterate shards in array order, lock ONE shard at a time
  (sequential, not simultaneous), same accounting, void.
- `size()` const noexcept: `total_count_.load(relaxed)`.
- `accountSizeDelta`: `after>before`→`fetch_add`; `after<before`→`fetch_sub`
  (relaxed, noexcept).
- copy ctor + copy assignment deleted; `ProfileEvents::Event lock_wait_event`
  stored (no-op first phase; not removed).
- callback contract documented: mutate-only-under-lock, no same-map recursion, no
  escaping iterator/reference/pointer, results returned by value.

Real-caller confirmation: `getOrCreateSharedOrigin` returns a **copied**
`OriginInfoPtr` by value (nothing escapes the lock); `removeSharedOrigins` is a
per-shard best-effort erase in `forEachShard`. No same-map recursion, no escaping
handles.

Dependency review — every external dependency has an explicit reviewed mapping;
no new unreviewed dependency or scope issue exists:

```text
folly::F14FastMap                 design 05; already used by FileCacheOriginInfo.h (Task 008)
ProfileEvents::Event + no-op       design 05; exists in velox/ch/Common/ProfileEvents.h
OriginPoolKey/OriginPoolKeyHash    design 02; exists in FileCacheOriginInfo.h (user_id-only hash)
boost::noncopyable -> deleted ops  design 05
ProfiledMutexLock -> std::unique_lock + no-op shim   design 05
SCOPE_EXIT -> local RAII guard, same exception behavior   design 05
parent CMakeLists non-mono FILE_SET registration     Controller amendment (scope resolved)
reduced test link (velox_ch_filecache + GTest)       Controller amendment; matches velox_ch_leaf_types_test
```

Proceeded (no re-block): the only attempt-1 blocker is resolved by the amendment.

## Files changed

```text
/root/oss/velox/velox/ch/Interpreters/FileCache/ShardedMap.h                  (new)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/ShardedMapTest.cpp      (new)
/root/oss/velox/velox/ch/Interpreters/FileCache/CMakeLists.txt                (modified: non-mono FILE_SET += ShardedMap.h)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt          (modified: velox_ch_sharded_map_test target)
/root/oss/clickhouse/port/task/result/009-filecache-sharded-map-result.md     (this receipt append)
```

Exactly the five declared artifacts. `git status --short` (velox) shows only the
two `M` CMake files and two `??` new files; ClickHouse shows only this receipt.

## Commands and outcomes

Environment: `source /root/oss/velox-helper/env.sh`, then the full selected-profile
CMake configuration directly (no `build.sh`), Ninja without `-j`.

| Command purpose | Exit | Log |
|---|---:|---|
| mono configure (full root config) | 0 | `/root/oss/velox/_build/debug/configure_task_009_attempt2.log` |
| RED build (missing header) | 1 (expected) | `/root/oss/velox/_build/debug/build_task_009_attempt2_red.log` |
| GREEN build focused test | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt2_sharded_map.log` |
| focused `ctest -R ^velox_ch_sharded_map_test$` | 0 | `/root/oss/velox/_build/debug/test_task_009_attempt2_sharded_map.log` |
| discovery (`--gtest_list_tests`, count) | 0 | `/root/oss/velox/_build/debug/list_task_009_attempt2_sharded_map.log` |
| direct gtest run | 0 | `/root/oss/velox/_build/debug/run_task_009_attempt2_sharded_map.log` |
| MUTATION1 build+run (exception-safety) | 0 / 1 | `/root/oss/velox/_build/debug/build_task_009_attempt2_mut1_exc.log`, `test_task_009_attempt2_mut1_exc.log` |
| MUTATION2 build+run (hash routing) | 0 / 1 | `/root/oss/velox/_build/debug/build_task_009_attempt2_mut2_hash.log`, `test_task_009_attempt2_mut2_hash.log` |
| MUTATION3 build+run (concurrency lock) | 0 / 124 | `/root/oss/velox/_build/debug/build_task_009_attempt2_mut3_lock.log`, `test_task_009_attempt2_mut3_lock.log` |
| SameKey non-vacuous probe (post-review) | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt2_samekey_probe.log` |
| regression build Tasks 003-009 (7 targets) | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt2_regression.log` |
| regression `ctest` Tasks 003-009 (7 entries) | 0 | `/root/oss/velox/_build/debug/test_task_009_attempt2_regression.log` |
| non-mono configure (`VELOX_MONO_LIBRARY=OFF`) | 0 | `/root/oss/velox/_build/debug-task009-nonmono/configure_task_009_attempt2_nonmono.log` |
| non-mono build (real `libvelox_ch_filecache.a`) | 0 | `/root/oss/velox/_build/debug-task009-nonmono/build_task_009_attempt2_nonmono_sharded_map.log` |
| non-mono focused `ctest` + discovery | 0 | `/root/oss/velox/_build/debug-task009-nonmono/test_task_009_attempt2_nonmono_sharded_map.log`, `list_task_009_attempt2_nonmono.log`, `run_task_009_attempt2_nonmono.log` |

## RED evidence (pre-implementation)

Added the test target + `ShardedMapTest.cpp` + the non-mono FILE_SET
registration FIRST, then built `velox_ch_sharded_map_test` before creating
`ShardedMap.h`:

```text
ShardedMapTest.cpp:17:10: fatal error:
  velox/ch/Interpreters/FileCache/ShardedMap.h: No such file or directory
```

Per protocol, the missing-header compile RED is necessary but NOT sufficient; the
behavioral mutation proofs below establish runtime semantics.

## Behavioral mutation proofs (post-implementation; all reverted, no markers)

Pristine `ShardedMap.h` SHA1 `f01a1d5f5504a8896e1b7b8e7326c50a982a56b2`; header
restored to that exact SHA after every mutation; final `grep` for `MUTATION`/`TMP`
markers is empty.

1. Exception-safe size accounting — changed the `withShard` `SizeGuard` to skip
   accounting during stack unwinding (`if (std::uncaught_exceptions()==0)`), i.e.
   account only on normal return. Result: the two normal size tests still PASS
   (`InsertUpdatesSize`, `EraseUpdatesSize`) but both throw-after-mutation tests
   FAIL (`ExceptionAfterInsertUpdatesSize`, `ExceptionAfterEraseUpdatesSize`).
   Proves those tests specifically validate that the guard fires on `throw`.

2. Shard/hash routing and same-hash use — changed shard selection from a pure
   function of `Hash{}(key)` to `(Hash{}(key)+total_count_) % num_shards` so
   same-hash keys diverge. Result: `OriginPoolKeyHashSameUserSameShard` FAILS
   (`found` = 0; k1 not visible from k2's callback), while the direct
   `hasher(k1)%32 == hasher(k2)%32` check still holds. Proves the test validates
   that same-hash keys co-locate via the modulo/hash routing.

3. Sequential lock traversal / concurrency — pre-implementation runtime RED is
   impossible (header absent → compile-only). Removing the shard lock did NOT
   reliably fail (200 short-lived threads across 32 shards rarely overlap on one
   shard: 0/8). A deterministic proof was used instead: replacing the per-shard
   mutex with a single global lock (`shards_[0].mutex`) serializes all shards, so
   `DifferentShardsConcurrent` DEADLOCKS (3/3, `timeout` exit 124) while the other
   11 tests still PASS. Proves cross-shard concurrency depends on independent
   per-shard mutexes, and that `DifferentShardsConcurrent` genuinely distinguishes
   a per-shard-lock implementation from a serializing one.

## Acceptance evidence

```text
mono focused test count: 12 (0 disabled, 0 skipped); 100% passed, determinism 30/30
non-mono focused test count: 12 (0 disabled, 0 skipped); 100% passed, determinism 15/15
Tasks 003-009 regression: exactly 7 ctest entries, 100% passed, 0 failed
  (velox_ch_common_test, velox_ch_guards_test, velox_ch_threadpool_test,
   velox_ch_scheduler_test, velox_ch_io_test, velox_ch_leaf_types_test,
   velox_ch_sharded_map_test)
git diff --check: no whitespace errors
scope: only the 5 declared artifacts changed
```

Fresh builds:
- mono `/root/oss/velox/_build/debug`: header-only `ShardedMap.h` + `ShardedMapTest.cpp`
  compiled fresh and `velox_ch_sharded_map_test` relinked; `libvelox.a` relinked.
- non-mono `/root/oss/velox/_build/debug-task009-nonmono` (`VELOX_MONO_LIBRARY=OFF`):
  real `libvelox_ch_filecache.a` built; the reduced consumer (links only
  `velox_ch_filecache` + GTest) linked and ran, proving the `velox_ch_filecache`
  PUBLIC interface propagates Folly and the other public-header dependencies.
  CMake file-API codemodel confirms `ShardedMap.h` is a member of the
  `velox_ch_filecache` `HEADERS` file set (visibility PUBLIC) with the same
  backtrace/source-group as the sibling public headers
  (`FileCacheOriginInfo.h`, `FileCacheUtils.h`, `Guards.h`).

## Worker review

```text
review subagent: one read-only code-review subagent over the complete task-owned
  diff (four Velox files) against CH source, real callers, design 05, and both
  build modes.
findings:
  - No blocker or major issues. Port judged a faithful, correct translation of CH
    ShardedMap.h; delivered header identical to the task-approved reference plus
    the parent-requested return-by-value documentation paragraph. Correctness,
    per-shard concurrency, exception-safety (guard destroyed before lock while
    holding it), reference/iterator lifetime (auto return decays under the lock),
    and CMake interface all PASS. The DifferentShardsConcurrent fix judged a
    legitimate, non-weakening, deterministic strengthening.
  - Minor 1: SameKeyAlwaysSameShard was effectively assertion-free (only proved
    no-deadlock).
  - Minor 2: dead local std::atomic<bool> overlap_detected in that test.
resolutions:
  - Minor 1+2 resolved together using the reviewer's own suggested approach
    ("assert a monotonic enter/exit counter never exceeds 1"): rewrote
    SameKeyAlwaysSameShard to track peak concurrent occupancy and
    EXPECT_EQ(max_active, 1), and removed the dead variable. This is deterministic
    on the correct per-shard-lock implementation (occupancy can never exceed 1).
    A probe confirmed same-shard overlap cannot be *forced* without deadlocking a
    correct implementation (0/40), which is inherent; the deterministic FAILURE
    proof for the locking contract therefore remains mutation 3 (single global
    lock -> DifferentShardsConcurrent deadlock). Reran mono focused (30/30),
    discovery (12, 0 disabled), Tasks 003-009 regression (7/7), and the non-mono
    build/test (15/15) after the change.
  - The DifferentShardsConcurrent fix (deviation from the task's literal test
    body) was forced by correctness: the declared version failed deterministically
    (15/15) on the correct implementation because std::latch::arrive_and_wait
    releases the two threads asymmetrically (the last arriver proceeds while the
    earlier arriver wakes from a futex), so the post-latch fetch_add/load==2 window
    never overlapped. The fix asserts a shared both_inside flag set only after both
    callbacks reach a latch while holding their (different) shard locks; it passes
    deterministically on correct code and deadlocks on a serializing implementation.
unresolved findings: none
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 009 was attempted.
Changes are unstaged and uncommitted; both repository HEADs are unchanged.
No scratch files remain (the temporary pristine-header copy under a tmp/ scratch
directory was removed).
The worker stopped after writing this receipt.
```

## Controller review 1

```text
controller_status: changes_requested
environment_profile: root-oss
task: 009
worker_attempt_reviewed: 2
```

## Review evidence

```text
scope and CMake review:
  Attempt 2 changed exactly the two declared CMake files, created ShardedMap.h
  and ShardedMapTest.cpp, and appended this receipt. The public header is in the
  non-mono PUBLIC HEADERS file set; the reduced focused consumer links only
  velox_ch_filecache plus GTest and passes in both modes.

implementation review:
  Lock order, per-shard concurrency, exception-safe size accounting, relaxed
  snapshot size, Hash-based routing, F14 lifetime restrictions, and OriginPool
  hashing match CH and the design.

  One generic API divergence remains. CH's forwarding-reference parameter is a
  named lvalue at the call site (`f(shard.map)`). The port invokes
  `std::forward<F>(f)(shard.map)` in withShard and on every forEachShard
  iteration. A ref-qualified callable therefore selects operator() && instead
  of CH's operator() &, and forEachShard repeatedly invokes the rvalue-qualified
  path. Current lambda callers hide this divergence.

test/evidence review:
  No test calls withShard for its return value, so the explicit return-by-value
  contract is unproven. No test uses a ref-qualified functor. The existing
  exception/hash/global-lock mutations are valid but do not cover these APIs.
  Same-key serialization and different-shard concurrency are otherwise
  sufficiently exercised.

log review:
  Worker logs prove 12/12 focused tests in mono/non-mono, zero disabled/skipped,
  7/7 accumulated regression, exception/hash/locking mutation failures, and the
  public header interface. They contain no callback invocation/return proof.

independent review:
  A fresh read-only review judged current callers safe but confirmed the generic
  return behavior is untested. The Controller applies the stricter exact-port
  contract: current caller limitations do not authorize changing public generic
  API semantics.

unresolved findings:
  1. Named callback invocation differs from CH for ref-qualified functors.
  2. Return-by-value/reference decay is not covered by a focused test.
```

## Required changes

```text
1. Invoke the named callback as `f(shard.map)` in withShard and forEachShard.
2. Add genuine RED tests with distinct lvalue/rvalue-qualified overloads for
   both methods; the current forwarding implementation must select the wrong
   overload and fail.
3. Add a callback returning Value& and prove the ShardedMap return is a detached
   copy. Capture a mutation proof with reference-preserving return semantics.
4. Rerun mono/non-mono focused gates and Task 003-009 regression; launch one
   fresh read-only review and append Worker attempt 3.
```

## Commits

No implementation or acceptance commit was created.

## Worker attempt 3

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 009
```

Redispatched after `Controller review 1` (`controller_status: changes_requested`).
The two required changes were:
1. Invoke the named callback as `f(shard.map)` in both `withShard` and `forEachShard`
   (fix the `std::forward<F>(f)(shard.map)` divergence from CH).
2. Add RED tests for ref-qualified functor invocation and return-by-value semantics.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/clickhouse` | `ch-filecache` | `4500c197112` | clean |
| `/root/oss/velox` | `filecache` | `24686d2c68831566439911eec8a69287e6fa39e3` | four unstaged Task-009 artifacts from attempt 2 only (ShardedMap.h, ShardedMapTest.cpp, two CMake files) |

`git log -1 --oneline`:
- ClickHouse: `4500c197112 Task 009: Require exact callback semantics`
- Velox: `24686d2c6 Task 008: Restore key parser compatibility`

## Preflight: contract re-derivation (unchanged from attempt 2)

CH source confirms `return f(shard.map)` in `withShard` and `f(shard.map)` in
`forEachShard` — named lvalue invocations. No new unreviewed dependency or scope
issue. The attempt-1 blocker (CMake FILE_SET registration) and the Controller
review-1 required changes are both resolved within the declared file scope.

## Files changed

```text
/root/oss/velox/velox/ch/Interpreters/FileCache/ShardedMap.h                  (modified: std::forward<F>(f)(shard.map) → f(shard.map) in withShard and forEachShard)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/ShardedMapTest.cpp      (modified: +3 new tests: WithShardRefQualifiedUsesLvalueOverload, ForEachShardRefQualifiedUsesLvalueOverload, WithShardReturnCopiesValue)
/root/oss/velox/velox/ch/Interpreters/FileCache/CMakeLists.txt                (unchanged from attempt 2: non-mono FILE_SET += ShardedMap.h)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt          (unchanged from attempt 2: velox_ch_sharded_map_test target)
/root/oss/clickhouse/port/task/result/009-filecache-sharded-map-result.md     (this receipt append)
```

Exactly the five declared artifacts. No other files changed.

## TDD sequence

### Step 1: Add RED tests (before production edit)

Added three new tests to `ShardedMapTest.cpp`:
1. `WithShardRefQualifiedUsesLvalueOverload` — functor with distinct `operator()(Map&) &`
   and `operator()(Map&) &&`; passed as temporary (prvalue); expects lvalue overload.
2. `ForEachShardRefQualifiedUsesLvalueOverload` — same functor for `forEachShard`;
   expects lvalue overload on all 32 shard iterations.
3. `WithShardReturnCopiesValue` — callback returns `int&`; `decltype(auto)` at call site;
   mutating copy must not affect map element.

### Step 2: RED build + run

With `std::forward<F>(f)(shard.map)` still in place:

```text
WithShardRefQualifiedUsesLvalueOverload: lv=0 (expected 1) rv=1 (expected 0)  FAILED
ForEachShardRefQualifiedUsesLvalueOverload: lv=0 (expected 32) rv=32 (expected 0)  FAILED
WithShardReturnCopiesValue: stored=99 == 99  PASSED (return contract already correct)
```

### Step 3: Production fix

Changed both invocation sites in `ShardedMap.h`:
```cpp
// withShard
return f(shard.map);    // was: return std::forward<F>(f)(shard.map);
// forEachShard
f(shard.map);           // was: std::forward<F>(f)(shard.map);
```

### Step 4: Mutation proof for return semantics

Temporarily changed `auto withShard(...)` to `decltype(auto) withShard(...)`.
Result: `WithShardReturnCopiesValue` FAILED (`stored=0`, expected 99) —
with `decltype(auto)` return the caller gets `int&` (live reference to map element),
`copy = 0` mutates the stored value, proving the `auto` return's copy-decaying semantics.
Then restored `auto`.

## Commands and outcomes

| Command purpose | Exit | Log |
|---|---:|---|
| RED build (std::forward still in place) | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt3_red.log` |
| RED test run (`*RefQualified*:*ReturnCopies*`) | 0 (2 fail expected) | `/root/oss/velox/_build/debug/test_task_009_attempt3_red.log` |
| GREEN build after fix | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt3_sharded_map.log` |
| focused `ctest -R ^velox_ch_sharded_map_test$` | 0 | `/root/oss/velox/_build/debug/test_task_009_attempt3_sharded_map.log` |
| discovery (`--gtest_list_tests`, 15 tests) | 0 | `/root/oss/velox/_build/debug/list_task_009_attempt3_sharded_map.log` |
| direct gtest run (15 tests) | 0 | `/root/oss/velox/_build/debug/run_task_009_attempt3_sharded_map.log` |
| MUTATION-declauto build | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt3_mut_declauto.log` |
| MUTATION-declauto run (`*ReturnCopies*`, FAIL) | 0 | `/root/oss/velox/_build/debug/test_task_009_attempt3_mut_declauto.log` |
| MUTATION1 build+run (exception-safe, 2 FAIL) | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt3_mut1_exc.log`, `test_task_009_attempt3_mut1_exc.log` |
| MUTATION2 build+run (hash routing, 1 FAIL) | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt3_mut2_hash.log`, `test_task_009_attempt3_mut2_hash.log` |
| MUTATION3 build (global lock, deadlock on DifferentShards) | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt3_mut3_lock.log`, `test_task_009_attempt3_mut3_lock.log` |
| determinism mono 30/30 | 0 | (inline; green in all 30) |
| regression build Tasks 003-009 (7 targets) | 0 | `/root/oss/velox/_build/debug/build_task_009_attempt3_regression.log` |
| regression `ctest` Tasks 003-009 (7/7) | 0 | `/root/oss/velox/_build/debug/test_task_009_attempt3_regression.log` |
| non-mono build | 0 | `/root/oss/velox/_build/debug-task009-nonmono/build_task_009_attempt3_nonmono_sharded_map.log` |
| non-mono discovery (15 tests) | 0 | `/root/oss/velox/_build/debug-task009-nonmono/list_task_009_attempt3_nonmono.log` |
| non-mono direct run (15/15) | 0 | `/root/oss/velox/_build/debug-task009-nonmono/run_task_009_attempt3_nonmono.log` |
| non-mono ctest | 0 | `/root/oss/velox/_build/debug-task009-nonmono/test_task_009_attempt3_nonmono_sharded_map.log` |
| non-mono determinism 15/15 | 0 | (inline; green in all 15) |

## RED evidence (pre-fix)

```text
WithShardRefQualifiedUsesLvalueOverload:
  lv=0 (expected 1) → FAIL: std::forward<F>(f) casts to rvalue, invokes operator()&&
  rv=1 (expected 0) → FAIL
ForEachShardRefQualifiedUsesLvalueOverload:
  lv=0 (expected 32) → FAIL: each loop iteration casts to rvalue, invokes operator()&&
  rv=32 (expected 0) → FAIL
WithShardReturnCopiesValue: PASS (return type orthogonal to forwarding fix)
```

## Mutation proofs (post-fix; all reverted)

1. Exception-safe accounting (skip during unwinding) → `ExceptionAfterInsertUpdatesSize` and
   `ExceptionAfterEraseUpdatesSize` FAIL (`size=0 expected 1`, `size=1 expected 0`).
2. Hash routing shift (`Hash{}(key)+total_count_`) → `OriginPoolKeyHashSameUserSameShard` FAILS
   (`found=0`, expected 1).
3. Global lock (all shards share `shards_[0].mutex`) → `DifferentShardsConcurrent` deadlocks
   (process hung on `barrier.arrive_and_wait()` with `timeout 15`, never printed `[OK]`/`[FAILED]`).
4. `decltype(auto)` return in `withShard` → `WithShardReturnCopiesValue` FAILS (`stored=0`, expected 99):
   caller receives `int&` to map element; `copy = 0` mutates it under the released lock.

All mutations reverted. No MUTATION/TMP markers remain. `git diff --check`: exit 0.

## Acceptance evidence

```text
mono focused test count: 15 (0 disabled, 0 skipped); 100% passed, determinism 30/30
non-mono focused test count: 15 (0 disabled, 0 skipped); 100% passed, determinism 15/15
Tasks 003-009 regression: exactly 7 ctest entries, 100% passed, 0 failed
  (velox_ch_common_test, velox_ch_guards_test, velox_ch_threadpool_test,
   velox_ch_scheduler_test, velox_ch_io_test, velox_ch_leaf_types_test,
   velox_ch_sharded_map_test)
git diff --check: exit 0 (no whitespace errors)
scope: only the 5 declared artifacts changed; both repos at original HEADs
```

## Worker review

```text
review subagent: one fresh read-only code-review subagent over the complete
  task-owned diff (ShardedMap.h + ShardedMapTest.cpp + two CMake files),
  with CH reference, design 05, RED/GREEN/mutation logs, and both build modes.
findings:
  None. All nine focus areas (lvalue semantics, return lifetime, SizeGuard
  ordering, generic API, RED evidence, test correctness, existing-test
  preservation, CMake scope, concurrency) reviewed and confirmed correct.
  The review explicitly confirmed: f(shard.map) matches CH exactly; auto return
  copies before lock release; SizeGuard fires before lock (LIFO); ref-qualified
  functor tests are genuine RED/GREEN; non-mono link-only-filecache+GTest passes.
resolutions: n/a (no findings to resolve)
unresolved findings: none
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 009 was attempted.
Changes are unstaged and uncommitted; both repository HEADs are unchanged.
No scratch files remain (tmp/ShardedMap.h.pristine_attempt3 removed after mutations).
The worker stopped after writing this receipt.
```

## Controller review 2

```text
controller_status: accepted
environment_profile: root-oss
task: 009
worker_attempt_reviewed: 3
```

## Review evidence

```text
scope and CMake review:
  The final Velox diff contains exactly ShardedMap.h, ShardedMapTest.cpp, and
  the two declared CMake files. ShardedMap.h is in the non-mono PUBLIC HEADERS
  file set; the reduced focused consumer declares only velox_ch_filecache plus
  GTest and passes in both modes.

implementation review:
  Default 32-way hash modulo sharding, one mutex per shard, callback-under-lock,
  sequential forEachShard, exception-safe size accounting, relaxed total-size
  snapshot, F14 map/hash consistency, OriginPoolKeyHash behavior, deleted copy
  operations, and preserved no-op lock event match CH and the approved design.

  Both methods invoke the named callback as an lvalue exactly like CH.
  withShard's auto return copies a callback reference result while the lock is
  held, so no map reference escapes. SizeGuard is destroyed before the lock and
  accounts insert/erase deltas during normal return and exception unwinding.

test and false-green review:
  Ref-qualified functors captured genuine RED against std::forward invocation;
  the reference-preserving return mutation makes the detached-copy test fail.
  Existing exception/hash/global-lock mutations prove size accounting, shard
  routing, and independent shard locks. Same-key callbacks remain serialized and
  different-shard callbacks overlap deterministically.

log and Controller gate review:
  Worker evidence passes 15/15 in mono/non-mono, zero disabled/skipped, repeated
  determinism, and Task 003-009 CTest 7/7.

  Controller logs:
    /root/oss/velox/_build/debug/configure_task_009_controller.log
    /root/oss/velox/_build/debug/build_task_009_controller.log
    /root/oss/velox/_build/debug/test_task_009_controller.log
    /root/oss/velox/_build/debug/discovery_task_009_controller.log
    /root/oss/velox/_build/debug/test_task_009_precommit_mono_retry.log
    /root/oss/velox/_build/debug-task009-nonmono/configure_task_009_controller.log
    /root/oss/velox/_build/debug-task009-nonmono/build_task_009_controller.log
    /root/oss/velox/_build/debug-task009-nonmono/test_task_009_controller.log
    /root/oss/velox/_build/debug-task009-nonmono/discovery_task_009_controller.log
    /root/oss/velox/_build/debug-task009-nonmono/test_task_009_precommit_nonmono_retry.log

  Controller mono CTest passed 7/7 and directly listed/ran 15/15 tests. Non-mono
  CTest passed 1/1 and directly listed/ran 15/15 tests with
  VELOX_MONO_LIBRARY=OFF. The first precommit helpers resolved relative build
  paths against the wrong checkout and ran no tests; the persisted absolute-path
  retries close that harness-only gap.

independent review:
  A fresh read-only Controller review checked the generic API, return lifetime,
  lock/guard ordering, exceptions, concurrency, Hash/F14 behavior, CMake
  interface, and all mutation evidence and reported no Blocker or Major finding.

unresolved findings:
  None.
```

## Required changes

```text
None.
```

## Accepted implementation commit

| Repository | Commit |
|---|---|
| `/root/oss/velox` | `096ba0c9ef8d68ca91ca62a7b15cf6a74bbc058a` |

## Whole-port review sign-off — SD1 F14 deviation (2026-07-20)

```text
task: 009
status: accepted (deviation signed off; no implementation change)
environment_profile: home-chang
reviewed: 2026-07-20
```

### Decision

`ShardedMap` keeps `folly::F14FastMap` (`ShardedMap.h:60`) instead of CH's
`std::unordered_map` (`ShardedMap.h:24`). This is a §3 guarantee-changing
deviation (F14 relocates mapped values on rehash; `std::unordered_map` does not)
with **no hard platform constraint** — it therefore requires an explicit human
sign-off, which is recorded here (user, 2026-07-20).

### Why it is safe for the current consumer

The sole in-scope consumer, `CacheMetadata::getOrCreateSharedOrigin` /
`removeSharedOrigins` (`Metadata.cpp:105-130`), copies an `OriginInfoPtr` out of
the locked callback and never retains a map iterator or mapped-value reference
across a mutation. `ShardedMap::withShard`'s `auto` return decays the callback's
reference to a value copy before the lock is released.

### Locked invariant (enforced by Tasks 011/012 review, not by a runtime test)

```text
No mapped-value reference, iterator, or mapped-value address may survive a
mutation (insert/erase/rehash) of the same F14 shard map. Values must be copied
out (or held via the stored shared_ptr/unique_ptr) before the locked callback
returns.
```

Registered in the Task 011 (`## Structural deviations — review-enforced`) and
Task 012 (`### Approved deviations and native mappings`) contracts.

### Test coverage

Existing `ShardedMapTest.WithShardReturnCopiesValue` proves the API layer does
not leak a live reference to a caller (the `auto` return is a value copy). Per
the user decision (2026-07-20), this API-layer guard plus the review-enforced
invariant is sufficient; a demonstrative "reference-survives-rehash" test is not
added, consistent with treating the invariant as a review-enforced contract
rather than a runtime-provable property.

### Note

The accepted implementation commit above (`096ba0c9…`) is on the `root-oss`
machine's `filecache` branch; the identical baseline is present on this
`home-chang` checkout's `filecache2` branch at HEAD's ancestor. No code changed
for this sign-off.
