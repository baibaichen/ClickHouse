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
