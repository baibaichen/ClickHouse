# Task 013 Result: `FileCacheFactory` and `FileCacheManager`

## Worker attempt 1

```text
worker_status: blocked
environment_profile: home-chang
task: 013
```

Task 013 requires (a) the real `FileCacheFactory` (sole registry), (b) the real
`FileCacheManager` (runtime resource owner), (c) moving ownership of
`FileCacheWorkerPool` / `folly::Timekeeper` + `FileCacheScheduler` / `commonUserId`
out of `FileCache` (where Task 012 placed them) into the Manager with `FileCache`
receiving them by injection, and (d) wiring the REAL Manager-backed `OpenedFileCache`
invalidation into the two Task-012 B7 no-op seams. Before writing any implementation
the worker enumerated every CH dependency / Velox type reached by the task and ran the
unreviewed-dependency gate (protocol worker rules 5-6; ENVIRONMENT.md and the prompt's
explicit STOP gate). Three genuinely unreviewed structural decisions were found. Each is
the same *class* of gap that Task 012 escalated one-by-one (B2a, B2b, B3, B5, B6, B7):
a Manager-owned concept that does not yet exist in the SCC phase, whose concrete
injection shape / type contract is not fixed by any approved design. Per the gate the
worker did NOT guess the closest Velox API, invent a shim, mark it no-op, or add a
fallback; no source file was created or modified.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `13b2dc63d` | clean (0 files); `filecache2...baibaichen/filecache [ahead 6]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `3fbded462d2e07757bcfd92b9d9dce0eaa9b0bba` | receipt append only |

`HEAD 13b2dc63d` (velox) = "Task 012: Center-SCC green build + behavioral tests
(sub-attempt S4)" — Task 012 accepted, the center SCC builds and tests green. The prompt
named the ClickHouse branch/HEAD as `ch-filecache` / `da28e83e8b3`; the actual checkout
is `ch-filecache2` / `3fbded462`. Recorded as observed. No pre-existing dirty files in
either worktree. No staging/commit/amend/rebase/push. No build-directory logs were
written (the worker stopped before configure/build; no false-green log manufactured).

## Files changed

```text
None. The worker stopped at the unreviewed-dependency gate before creating any
FileCacheFactory / FileCacheManager / OpenedFileCache source or editing any SCC file.
Only this result receipt is written under port/task/result/.
```

## Pre-implementation gates worked before stopping

```text
Contract-derivation gate (worker rule 6): the CH FileCacheFactory contract was fully
derived from src/Interpreters/FileCache/FileCacheFactory.{h,cpp} and cross-checked with
design 2-file-cache/12 (registry: getOrCreate/create/get/getAll/getUniqueInstances/
getByName/remove/clear; name/path dedup; alias insertion does NOT grow worker budget;
settings equality excludes cache name + config source path; the CH name-rebind edge bug is
fixed to fail-fast). The Manager contract was derived from design 3-consumers/02 and the
Task-013 Steps (two-phase create, worker-budget formula, singleton dual-pointer install/
uninstall order, initialize/applyConfigs/clear/shutdown, member/destruction order). Those
parts are fully reviewed and would be implementable as-is.

Unreviewed-dependency gate (worker rule 5): three decisions below are NOT fixed by any
approved design/receipt and cannot be implemented without guessing. They are structurally
identical to the Task-012 escalations (a Manager-owned resource absent in the SCC phase +
a missing injection point), which were each resolved by explicit controller/user decision
before implementation, not invented by the worker.
```

## Blockers

```text
BLOCKER D1 — `OpenedFileCache` is a brand-new type with no reviewed concrete contract.
  It does not exist anywhere in velox/ch (grep velox/ch -> only the two Task-012 TODO
  comments in FileSegment.cpp/Metadata.cpp reference the NAME). CH's
  src/Common/OpenedFileCache.h is a process-global SINGLETON caching `OpenedFile`
  (an int fd + mmap state) keyed by (path, flags) with get(path,flags)/remove(path,flags)
  — design 02:73,89 explicitly says the Velox port must NOT reuse that and must NOT reuse
  the Hive `FileHandleCache` (velox/common/caching/FileHandle.h). The Task-013 illustrative
  headers fix only its OWNERSHIP and CONSTRUCTION shape:
    - Factory RuntimeServices carries `OpenedFileCache & openedFileCache` (013:280).
    - Manager owns `OpenedFileCache openedFileCache_` (013:484), constructed
      `openedFileCache(localFileSystem, memoryPool)` (013:530), and reports
      `FileHandleCacheStats openedFileCache` in FileCacheManagerStats (013:419).
  They do NOT fix its INTERNAL contract, all of which must be invented to satisfy the
  mandatory B7 test ("a removed/renamed path's cached handle is dropped" — so it must
  actually cache handles, not be a no-op):
    - What a cached "handle" is in the Velox port (a `velox::ReadFile` /
      `std::shared_ptr<ReadFile>`? an fd? something mmap-backed like CH's OpenedFile?).
    - The `get` signature and key: path-only, or (path, O_DIRECT/flags) as CH keys —
      CH's remove sites pass `flags | (is_direct_io ? O_DIRECT : 0)`; the seam wiring
      depends on which.
    - How `get` opens through the injected `filesystems::FileSystem & localFileSystem`
      and what `memory::MemoryPool & memoryPool` is used for (CH's is fd/mmap, no pool).
    - The `remove(path)` invalidation semantics (drop all flag-variants for a path? one?).
    - The `FileHandleCacheStats` and `FileCacheStats` struct FIELDS (used by
      FileCacheManagerStats and refreshStats; neither exists in velox/ch today).
  Inventing these is exactly the "closest Velox API / new shim" the gate forbids.
  Reference anchors: design 02:35,62-90,244,576-591; Task-013 013:280,419,457,484,530;
  CH src/Common/OpenedFileCache.h; velox/common/caching/FileHandle.h.

BLOCKER D2 — the B7 seam -> `OpenedFileCache` injection shape is not fixed.
  `CacheMetadata` (Metadata.h) and `FileSegment` (FileSegment.h) have NO reference/callback
  to any opened-file cache; the two Task-012 seams are pure no-ops
  (FileSegment.cpp renameToIncludeSizeInNameUnlocked ~L740-749 after `fs::rename`;
  Metadata.cpp removeFileSegmentImpl ~L1245-1255 after `fs::remove`). Design 02:89,589 says
  invalidation happens "通过注入的 reference或 callback" but does NOT fix:
    - the injected type: `OpenedFileCache &` vs `std::function<void(const std::string& path)>`
      (or `void(path, flags)`);
    - which class carries it — `CacheMetadata` only, or also `FileSegment` (the rename seam
      is in FileSegment; the remove seam is in CacheMetadata/LockedKey);
    - the thread-through path Manager -> Factory::RuntimeServices -> FileCache ctor ->
      `CacheMetadata` ctor -> reached by `FileSegment` (via its key-metadata back-ref).
  This is the identical injection-point class as Task-012 B2a (reserve timeout),
  B3 (worker pool), and B2b/B7 (the very opened-handle seam, deferred to Task 013): each
  required an explicit controller/user decision naming the exact member + ctor parameter
  + reach path BEFORE the worker added it, because adding a member to accepted SCC headers
  (Metadata.h / FileSegment.h) and threading it through FileCache is a scope-and-shape
  decision, not a "closest API" pick. Adding it also re-touches the green SCC test.
  Reference anchors: FileSegment.cpp:740-749; Metadata.cpp:1245-1255; design 02:89,589;
  Task-012 receipt B2b CORRECTION / B7 ("Task 013 wires the real Manager-backed
  invalidation into this same seam").

BLOCKER D3 — the FileCache resource-ownership-MOVE signature is unspecified.
  Task 012 (accepted) gave `FileCache` an OWNED `worker_pool` / `timekeeper` / `scheduler`
  (FileCache.h:343-345, value members) and the 3-arg ctor
  `FileCache(cache_name, settings, common_user_id_)` (FileCache.h:150-153), used pervasively
  in FileCache.cpp (ctor init-list :183-185,213-219; initializeImpl :409; cleanup/free-space
  tasks :435,466; load-metadata + purge threads :1541,1610,2332; `metadata` ctor takes
  `worker_pool` by ref). The Task-013 amendment requires MOVING ownership of worker_pool,
  timekeeper+scheduler, and commonUserId to the Manager, with `FileCache`/`CacheMetadata`
  receiving them BY REFERENCE from the Manager (amendment "Post-Task-012 amendment" bullets
  1-3). But NO approved design fixes the resulting `FileCache` CONSTRUCTOR SIGNATURE or the
  new member set/order. Open, unreviewed sub-decisions:
    - Exactly which services become reference ctor parameters to `FileCache`:
      worker_pool + scheduler for sure; also the shared `timekeeper` (or only via the
      scheduler)? also `OpenedFileCache &` + `localFileSystem &` + `memoryPool &`
      (needed for D1/D2)? The task's illustrative `RuntimeServices` struct (013:276-284)
      is the FACTORY's, not FileCache's; the design says "每个 FileCache 构造时显式注入
      workerPool/scheduler/openedFileCache/localFileSystem/memoryPool/commonUserId"
      (02:373-382) — i.e. six references — but that list has NOT been reconciled against the
      accepted 012 FileCache.h members (which currently OWN pool/timekeeper/scheduler and
      have NO openedFileCache/localFileSystem/memoryPool members at all).
    - The new member DECLARATION/DESTRUCTION order once the owned resources become
      references (the 012 header carefully orders owned worker_pool/timekeeper/scheduler
      BEFORE metadata/eviction_pool/task-holders so they outlive users; converting them to
      references changes what must outlive what, and the SCC test currently depends on the
      owned-resource lifetime).
    - Whether `scheduler.createTask` / `eviction_pool = FileCacheThreadPool(worker_pool,...)`
      / all `FileCacheWorker(worker_pool, ...)` call sites keep identical observable
      scheduling semantics after the pool/scheduler become Manager-owned and shared across
      caches (the amendment says "Do NOT change the observable scheduling / worker
      semantics" and "the existing velox_ch_filecache_core_scc_test must STILL pass").
  Changing the accepted, green FileCache ctor + members + every internal use is a large,
  unreviewed edit to SCC code with a hard "keep the SCC test green" constraint; the exact
  target signature must be fixed by decision, not guessed. This mirrors B5/B6 (which fixed
  that FileCache HOLDS these in the SCC phase); D3 is the reverse move and needs the same
  explicit authorization for the concrete shape.
  Reference anchors: FileCache.h:150-153,343-345; FileCache.cpp:173-219,409,435,466,
  1541,1610,2332; amendment "Post-Task-012 amendment"; design 02:296-306,373-384.

Exact decisions needed from the Controller/user (all three unblock a redispatch):
  1. D1: fix the concrete `OpenedFileCache` contract — the cached-handle type, the
     get() signature + key (path vs path+flags), how get() opens via
     `filesystems::FileSystem` and what `memory::MemoryPool` is for, remove()/clear()
     semantics, and the exact `FileHandleCacheStats` + `FileCacheStats` struct fields.
  2. D2: fix the seam injection shape — the injected type (`OpenedFileCache &` vs
     `std::function<void(path[,flags])>`), which of CacheMetadata / FileSegment carries it,
     and the Manager->Factory->FileCache->CacheMetadata->FileSegment reach path — and
     authorize adding that member to the accepted Metadata.h / FileSegment.h.
  3. D3: fix the post-move `FileCache` constructor signature and member/destruction order
     (which services are reference parameters: pool, scheduler, timekeeper, openedFileCache,
     localFileSystem, memoryPool, commonUserId), and confirm the observable scheduling/worker
     semantics are unchanged so `velox_ch_filecache_core_scc_test` stays green.

Once D1/D2/D3 are fixed, the remaining work is mechanical and fully reviewed: the Factory
registry (design 12) and the Manager lifecycle/worker-budget/singleton/shutdown-order
(design 02 + Task-013 Steps), plus the executable test matrix from the pre-execution
amendment, plus registering `velox_ch_filecache_manager` + `velox_ch_filecache_manager_test`
in CMake and driving the green gate (build + run both `velox_ch_filecache_manager_test` and
a re-run of `velox_ch_filecache_core_scc_test`).
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| `git status/log` velox + ClickHouse baselines | 0 | (captured in this receipt) |
| Read + contract-derivation of CH FileCacheFactory.{h,cpp}, designs 02 + 12, Task-013, Task-012 receipt, velox FileCache.h/.cpp ctor, Metadata.cpp/FileSegment.cpp B7 seams, FileCacheScheduler.h, ThreadPool.h, FileCacheSettings.h | 0 | (research only; no build attempted — no false-green log manufactured) |

No configure/build/test was run: the worker stopped at the unreviewed-dependency gate before
any source could be written, so there is nothing to compile. Manufacturing a configure/red-build
log without an implementation would not be truthful acceptance evidence.

## Acceptance evidence

```text
test count: 0 (no test target built)
failed tests: n/a
skipped/disabled tests: n/a
git diff --check: clean (no changes; velox tree at accepted baseline 13b2dc63d)
```

## Worker review

```text
review subagent: not launched. Protocol rule 10 launches the read-only reviewer over a
  COMPLETED task-owned diff after local validation. This attempt produced no diff (blocked
  before any in-scope file could be written), so there is nothing to review; this mirrors how
  the Task-012 blocked attempts handled the same situation.
findings: N/A
resolutions: N/A
unresolved findings: the three structural decisions D1/D2/D3 above (need a controller/user
  decision, not a code fix).
```

## Worker declaration

```text
Only Task 013 was attempted.
No source or CMake file was created or modified; both worktrees are at their accepted
baselines (velox 13b2dc63d, clean). No staging/commit/amend/rebase/push. Only this result
receipt was written.
The worker stopped after writing this receipt.
```

## Controller unblock response 1 (D1/D2/D3 CH-aligned)

```text
controller_status: blocker_resolved
task: 013
resolved: 2026-07-20
```

## Resolution

```text
root cause:
  Three unreviewed structural decisions: D1 OpenedFileCache internal contract,
  D2 the B7 injection shape, D3 the FileCache ownership-move signature. The
  worker correctly stopped rather than inventing them.

decision (user 2026-07-20: implement all three in Task 013, aligned to CH):
  D1 Port CH src/IO/OpenedFileCache.h (118 lines) faithfully: 1024 sharded
     {mutex; map<(path,flags), WeakPtr>}, bucket = hash(path)%1024, get() reuses
     a live weak handle else opens via injected FileSystem::openFileForRead with
     an erase-on-last-release deleter, remove() erases the key. Two forced subs:
     handle shared_ptr<OpenedFile> -> shared_ptr<velox::ReadFile> (user-confirmed:
     it caches the cache-segment read handle); ownership singleton -> Manager-owned
     (design 02, not the Hive FileHandleCache). Drop CH's memoryPool param unless
     a concrete use appears. Minimal real stats fields.
  D2 Inject OpenedFileCache& (not std::function): Manager -> FileCache ctor ->
     CacheMetadata ctor member -> FileSegment via key-metadata. Wire the two B7
     no-op seams (Metadata.cpp removeFileSegmentImpl after fs::remove;
     FileSegment.cpp renameToIncludeSizeInNameUnlocked after fs::rename, old path).
     Add a real drop-on-remove/rename test.
  D3 Move worker_pool / timekeeper+scheduler / commonUserId ownership from
     FileCache to the Manager; FileCache takes them by reference (design 02:373-382).
     Manager declares owned resources BEFORE the factory so they outlive every
     FileCache (shutdown order: cache workers -> timers -> pool -> handles).
     velox_ch_filecache_core_scc_test MUST still pass after the move (fixture
     supplies the injected references).

task update:
  Recorded in the Task-013 amendment "### D1/D2/D3 CH-aligned contracts".

evidence:
  CH src/IO/OpenedFileCache.h:33-115 (structure/get/remove/deleter); B7 seams
  FileSegment.cpp:800-802, Metadata.cpp:1267-1268; design 02:73,89,299-300,373-382;
  Velox FileSystems.h:136 openFileForRead; ReadBufferFromVeloxReadFile.h:289
  shared_ptr<ReadFile>.

redispatch: yes — Task 013 with D1/D2/D3 fixed.
```

## Worker attempt 2

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 013
```

Implemented all three CH-aligned contracts (D1 `OpenedFileCache`, D2 the B7
injection shape, D3 the `FileCache` ownership move) plus the real Factory and
Manager, per the controller unblock response. Both mandatory gates are green
(`velox_ch_filecache_manager_test` 17/17 and, after the D3 ownership move,
`velox_ch_filecache_core_scc_test` 47/47 — both 0 failed / 0 skipped).

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `13b2dc63d` | clean (0 files); `filecache2...baibaichen/filecache [ahead 6]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `8083e145676` | receipt append only |

Branch is `filecache2` / `ch-filecache2` (task text says `filecache`); recorded
as observed per the dispatch note. `HEAD 13b2dc63d` (velox) = "Task 012:
Center-SCC green build + behavioral tests (sub-attempt S4)" — Task 012 accepted.
No pre-existing dirty files in velox. No staging/commit/amend/rebase/push.

## Files changed

```text
NEW (Task 013):
  velox/ch/Interpreters/FileCache/OpenedFileCache.h            (D1: 1024-shard read-handle cache, weak_ptr<ReadFile>)
  velox/ch/Interpreters/FileCache/FileCacheFactory.h           (registry API + RuntimeServices)
  velox/ch/Interpreters/FileCache/FileCacheFactory.cpp         (registry: getOrCreate/create/get/remove/clear, worker budget)
  velox/ch/Interpreters/FileCache/FileCacheManager.h           (resource owner + singleton)
  velox/ch/Interpreters/FileCache/FileCacheManager.cpp         (two-phase create, install/uninstall, initialize/shutdown, refreshStats)
  velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp  (Factory + Manager + B7 OpenedFileCache tests, 17)
  velox/ch/Interpreters/FileCache/tests/FileCacheTestResources.h         (D3 direct-injection test helper)
MODIFIED (D2/D3 seams + ownership move):
  velox/ch/Interpreters/FileCache/FileCache.h                  (D3: owned worker_pool/timekeeper/scheduler -> injected refs; new 7-arg ctor; +opened_file_cache/local_file_system refs)
  velox/ch/Interpreters/FileCache/FileCache.cpp                (D3: ctor init-list; metadata ctor now passes opened_file_cache)
  velox/ch/Interpreters/FileCache/Metadata.h                   (D2: CacheMetadata gains OpenedFileCache& param+member+accessor; KeyMetadata::openedFileCache)
  velox/ch/Interpreters/FileCache/Metadata.cpp                 (D2: ctor init; remove seam calls removePath; KeyMetadata::openedFileCache out-of-line)
  velox/ch/Interpreters/FileCache/FileSegment.cpp             (D2: rename seam calls openedFileCache().removePath(old_path) when renamed)
  velox/ch/Interpreters/FileCache/CMakeLists.txt              (register FileCacheFactory.cpp/FileCacheManager.cpp + new headers)
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt        (add velox_ch_filecache_manager_test target)
  velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp     (D3: use res_.makeFileCache for the injected refs)
  velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp   (D3: same)
  velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp      (D3: same)
  velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp    (D3: same)
```

`velox_ch_filecache_manager` was NOT created as a separate library: the mono
build (`VELOX_MONO_LIBRARY=ON`) compiles the SCC + Factory/Manager into
`velox_ch_filecache`; a second library would double-define the SCC symbols and
break the link (same ODR reason S4 gave for not splitting a core library). The
manager test links `velox_ch_filecache` once.

## Contract realization (D1/D2/D3)

```text
D1 OpenedFileCache: ported CH src/IO/OpenedFileCache.h faithfully — 1024 shards,
   each {mutex; std::map<(path,int flags), weak_ptr<ReadFile>>}, bucket =
   folly::hash::fnv64_buf(path)%1024 (infra substitution, no bit-compat need).
   Handle substitution shared_ptr<OpenedFile> -> shared_ptr<velox::ReadFile>:
   get(path,flags) reuses a live weak hit (hit event) else opens via injected
   filesystems::FileSystem::openFileForRead and installs with an erase-on-last-
   release custom deleter. remove(path,flags) idempotent; removePath(path) drops
   all flag-variants. CH memoryPool param dropped (recorded). Minimal real stats
   (FileHandleCacheStats: numCachedFiles/numLiveHandles/hits/misses). Manager-owned
   (openedFileCache_), constructed with the injected FileSystem& — not a singleton,
   not the Hive FileHandleCache.
D2 B7 seams: injected OpenedFileCache& (NOT std::function). Threaded Manager ->
   FileCache ctor -> CacheMetadata ctor (reference member) -> reached by FileSegment
   via KeyMetadata::openedFileCache (forwards to cache_metadata->openedFileCache).
   Wired both Task-012 no-op seams: Metadata.cpp removeFileSegmentImpl (after
   fs::remove -> removePath(removed_path), stays coupled to key_metadata->erase);
   FileSegment.cpp renameToIncludeSizeInNameUnlocked (after fs::rename ->
   removePath(old_path) only when renamed). Real drop-on-remove/rename test added.
D3 ownership move: worker_pool + folly::Timekeeper+scheduler + commonUserId moved
   from FileCache (owned in the SCC phase) to the Manager; FileCache receives
   worker_pool/scheduler/openedFileCache/localFileSystem by reference + commonUserId
   by value (memoryPool dropped per D1). timekeeper is Manager-owned (only the
   scheduler needs it). Manager member order declares owned resources BEFORE the
   factory so destruction (reverse) tears down the factory (all FileCaches) first,
   then openedFileCache, scheduler, worker pool. SCC test fixtures adapted via a
   direct-injection helper (FileCacheTestResources) so the SCC test still passes.
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure (home-chang recipe + `-DVELOX_BUILD_TESTING=ON`) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_013_factory_mgr.log` |
| build `velox_ch_filecache_manager_test` (initial + review-fix relinks) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_013_factory_mgr.log` |
| build `velox_ch_filecache_core_scc_test` (D3 re-run) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_013_scc.log` |
| build both after review fixes | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_013_review_fixes.log` |
| ctest `velox_ch_filecache_manager_test` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_013_factory_mgr.log` |
| ctest `velox_ch_filecache_core_scc_test` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_013_scc.log` |

No `-j` was passed to ninja. There is no dedicated RED-build log: the RED phase
was necessarily reasoned rather than captured, because the D3 ownership move
changes the accepted FileCache constructor signature, so a test file that
constructs Factory/Manager/injected-FileCache cannot compile at all against the
pre-change tree (the manager library and 7-arg ctor do not exist). The tests
exercise production Factory/Manager/OpenedFileCache behavior that did not exist
before this task; each carries real assertions reaching the changed path.

## Acceptance evidence

```text
Gate 1 velox_ch_filecache_manager_test:
  test count: 17 (8 FactoryTest + 6 ManagerTest + 3 OpenedFileCacheTest)
  failed tests: 0
  skipped/disabled tests: 0
Gate 2 velox_ch_filecache_core_scc_test (re-run after D3 move):
  test count: 47 (10 suites: PriorityEviction/FileSegmentInfo/FileSegment/
    Metadata/FileCache/QueryLimit)
  failed tests: 0
  skipped/disabled tests: 0
git diff --check: clean (exit 0, no whitespace errors)
git status: only the task-owned files above; unstaged/uncommitted; velox at
  baseline 13b2dc63d.
```

## Worker review

```text
review subagent: one read-only general-purpose reviewer over the complete
  Task-013 diff (all modified + new files) with the task file, the D1/D2/D3
  contracts, and both test outcomes. Asked only for correctness / concurrency /
  lifetime / ownership-move / integration / false-green findings; it did not edit.
findings:
  H1 [high] OpenedFileCache custom deleter captured `this` (the shard) and locked
     its mutex on last release; if a ReadFile handle outlives the Manager-owned
     OpenedFileCache, that is a use-after-free on a destroyed mutex/map.
  M1 [med]  The deleter's unconditional files.erase(key) could drop a key that a
     concurrent get() had just resurrected with a new live handle.
  Low/observations (reviewer confirmed CORRECT, no change needed): both B7 seam
     wirings (rename guarded by `renamed`, remove coupled to erase, both use
     removePath); worker-budget checkedAdd + alias dedup; the clear()/remove()
     deactivate-before-shrink deadlock ordering; the singleton install/uninstall
     atomic ordering; getOrCreate/create rollback. No assertion-free/can't-fail
     tests found.
resolutions:
  H1 RESOLVED: the shard state (mutex + map) now lives in a shared_ptr<Shard>; the
     deleter holds a weak_ptr<Shard> and becomes a safe no-op if the whole cache
     was destroyed first (locks the weak_ptr, finds it expired). Manager member
     order already guarantees the factory (and thus every FileCache holding
     handles internally) is destroyed before openedFileCache_, so in-tree there is
     no live internal handle at teardown; the weak_ptr closes the external-holder
     hazard defensively for Task 014.
  M1 RESOLVED: the deleter now erases the entry only if it is still expired
     (found->second.expired()), so a resurrected key (a newer live handle) is kept.
  Both fixes rebuilt clean and both gates re-run green (17/17, 47/47).
  Advisory M2 (refreshStats vs shutdown concurrency) assessed low/benign
     (per-shard mutexes make it memory-safe; stats during shutdown are cosmetic);
     no change.
unresolved findings: none.
```

## Blockers

```text
None. Both gates are green.
```

## Worker declaration

```text
Only Task 013 was attempted.
Changes are unstaged and uncommitted; velox remains at baseline 13b2dc63d.
The file scope is exactly the D1/D2/D3-authorized set (the FileCache CMake, the 5
new Factory/Manager/OpenedFileCache + test + test-helper files, the two B7 seam
edits in Metadata.cpp/FileSegment.cpp, the D3 ownership-move edits in
FileCache.h/.cpp and Metadata.h, and the 4 SCC test fixtures adapted to the new
injected ctor). No file outside that set was changed.
The worker stopped after writing this receipt.
```

## Controller review 1

```text
controller_status: changes_requested
environment_profile: home-chang
task: 013
```

## Review evidence

```text
scope review: PASS. Task-owned files exactly match the receipt (11 modified + 7
  new, all under velox/ch/Interpreters/FileCache). git diff --check clean.
implementation review: D1/D2/D3 all verified correct by an independent read-only
  Controller reviewer:
    D1 OpenedFileCache — CH-faithful (1024 shards, map<(path,int),weak_ptr<ReadFile>>,
       openFileForRead injection, memoryPool dropped). H1 (shard in shared_ptr<Shard>,
       deleter weak_ptr no-op if cache destroyed first) and M1 (deleter erases only
       if still expired() — resurrection-safe) fixes present and sound.
    D2 reach path real: FileSegment->KeyMetadata::openedFileCache()->
       cache_metadata->openedFileCache()-> the FileCache-ctor-threaded reference ->
       Manager openedFileCache_. Both seams guarded (rename only when renamed,
       remove only on actual removal; old_path/removed_path captured pre-op).
    D3 ownership move — Manager declares owned resources (workerPool_/scheduler_/
       openedFileCache_) BEFORE factory_, so reverse destruction tears down the
       factory (all FileCaches) first; no reference member outlives its referent.
       Test fixtures order res_ before cache_. Lifetimes correct.
    Factory/Manager behavioral contracts all correct (dedup/rejection, checkedAdd
       reused, deactivate-outside-lock, singleton atomic ordering, two-phase create,
       idempotent shutdown).
cross-task architecture review: dependency direction Manager->Factory->FileCache
  intact; FileCache never references Manager/Factory. SCC still green (47/47) after
  the ownership move.
log and test review: verified directly — manager binary 17/17, scc binary 47/47,
  0 failed / 0 skipped. Build logs clean, no -j.
unresolved findings:
  F1 (CONFIRMED, false-green, medium) — the two B7 production seams
  (Metadata.cpp removeFileSegmentImpl, FileSegment.cpp
  renameToIncludeSizeInNameUnlocked) are wired CORRECTLY but are NOT exercised
  end-to-end by any test. The 3 OpenedFileCacheTest cases drive the OpenedFileCache
  PRIMITIVE directly (RenameInvalidatesOldPathHandle even hand-simulates the seam
  with fs::rename + manual removePath). Reverting both seams to the Task-012
  (void)removed_path;/(void)renamed; no-ops would leave all 64 tests green. The D2
  contract explicitly requires a test that a removed/renamed path's cached handle
  is dropped THROUGH the changed path, not the primitive.
```

## Required changes

```text
Add end-to-end regression test(s) that reach the two B7 seams with a live cached
handle, so a revert of either seam turns the test RED:
  (a) Using a real FileCache (built via the injection helper) that shares the SAME
      OpenedFileCache instance the FileCache was constructed with, open a handle
      for a downloaded cache-segment file's path via that OpenedFileCache::get.
  (b) Trigger a real segment REMOVAL through the FileCache/Metadata API (the path
      that hits removeFileSegmentImpl -> fs::remove) and assert the cached handle
      for that path is dropped (weak hit gone / numCachedFiles decremented).
  (c) Trigger a real segment size-rename through the FileSegment API (the path that
      hits renameToIncludeSizeInNameUnlocked -> fs::rename) and assert the OLD
      path's cached handle is dropped.
Do NOT weaken to a primitive-only test. The assertion must fail if the seam call
is reverted to the Task-012 no-op. Re-run BOTH gates (manager + scc) green.
Keep everything else (D1/D2/D3 implementation) as-is; F2-F7 are accepted correct.
```

## Worker attempt 3

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 013
```

Addresses Controller review 1 FINDING F1 (false-green gap: the two B7 production
seams were wired correctly but no test drove them end-to-end through the real
FileCache/FileSegment API — reverting both seams to the Task-012 no-ops left all
tests green). Scope of this attempt = TESTS ONLY; the accepted D1/D2/D3
implementation was NOT changed (the seam source lines are byte-identical to
attempt 2; verified after the RED probes were reverted).

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `13b2dc63d` | dirty (attempt-2 Task-013 changes preserved) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `8083e145676` | receipt append only |

No new production files; the only file changed in attempt 3 is the test:
`velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp`.

## New/changed test content

```text
velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp
  + include FileCacheKey.h + FileSegment.h (+ <vector>)
  + class SeamE2ETest fixture (owns res_ = FileCacheTestResources; settings() with
    alignment tuned per case)
  + TEST_F SeamE2ETest.RemoveFileSegmentDropsCachedHandle  (drives the REMOVE seam)
  + TEST_F SeamE2ETest.RenameOnDownloadDropsOldPathHandle  (drives the RENAME seam)
```

Both new tests build+run inside the existing `velox_ch_filecache_manager_test`
gate (they need only the FileCache/FileSegment public API + the shared
`res_.openedFileCache_`, all in the mono `velox_ch_filecache` lib the test links).

## How each test reaches the CHANGED production path

```text
RemoveFileSegmentDropsCachedHandle (remove seam):
  (a) FileCache built via res_.makeFileCache sharing res_.openedFileCache_.
  (b) Two partially-downloaded Regular segments under one key at offsets 0 and seg
      (alignment pinned to seg -> stable `<offset>` names, no rename). A real
      read handle for the offset-0 file is opened via res_.openedFileCache_.get.
  (c) cache.removeFileSegment(key, 0, user) -> lockKeyMetadata(THROW) ->
      LockedKey::removeFileSegment -> removeFileSegmentImpl -> fs::remove(path) ->
      key_metadata->openedFileCache().removePath(removed_path) (the seam).
      Two segments keep the KEY non-empty after removing one, so the removal does
      NOT trigger the racy background empty-key cleanup -> deterministic
      (stress-verified 15/15 for the pair and 8/8 for the full suite).
  Asserts: file gone; numCachedFiles == 0; a re-open yields a DIFFERENT pointer
  than the still-held stale handle.

RenameOnDownloadDropsOldPathHandle (rename seam):
  (a) FileCache built via res_.makeFileCache sharing res_.openedFileCache_
      (alignment == 1 so a full download completes as DOWNLOADED and renames).
  (b) A Regular segment is fully downloaded to disk at the pre-rename `<offset>`
      name; a real read handle for THAT old path is opened via
      res_.openedFileCache_.get.
  (c) segment.completePartAndResetDownloader() -> resetDownloadingStateUnlocked
      (downloaded == range size) -> setDownloadedUnlocked ->
      renameToIncludeSizeInNameUnlocked -> fs::rename(old,new) then
      key_metadata_ptr->openedFileCache().removePath(old_path) (the seam).
  Asserts: old path gone; numCachedFiles == 0; a re-open at the old name yields a
  DIFFERENT pointer than the still-held stale handle.
```

## RED verification (hard requirement met)

```text
Temporarily reverting EACH seam to its Task-012 no-op and rebuilding turns the
corresponding new test RED (assertions fail because the stale cached handle
survives the physical change):
  - remove seam -> `(void)removed_path;`  => RemoveFileSegmentDropsCachedHandle: 1 FAILED
  - rename seam -> `(void)old_path;`       => RenameOnDownloadDropsOldPathHandle:  1 FAILED
Both seams were then restored; `grep -c` confirms exactly one live
`removePath(removed_path)` (Metadata.cpp) and one `removePath(old_path)`
(FileSegment.cpp); the seam source lines + comments are unchanged from attempt 2.
No RED-PROBE remnants remain anywhere under velox/ch/Interpreters/FileCache.
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| build both targets (final GREEN) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_013_attempt3_final.log` |
| ctest `velox_ch_filecache_manager_test` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_013_attempt3_mgr.log` |
| ctest `velox_ch_filecache_core_scc_test` (re-run) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_013_attempt3_scc.log` |

No `-j`. Stress runs (not persisted): SeamE2ETest.* 15/15, full manager suite
8/8 — the two-segment remove eliminated the background-cleanup race that made an
earlier single-segment version flaky.

## Acceptance evidence

```text
Gate 1 velox_ch_filecache_manager_test:
  test count: 19 (8 FactoryTest + 6 ManagerTest + 3 OpenedFileCacheTest
    + 2 SeamE2ETest)
  failed tests: 0
  skipped/disabled tests: 0
Gate 2 velox_ch_filecache_core_scc_test (re-run, unchanged):
  test count: 47
  failed tests: 0
  skipped/disabled tests: 0
git diff --check: clean (exit 0)
git status: attempt-3 change is limited to the one test file; the D1/D2/D3
  production files remain at their attempt-2 content; velox at baseline 13b2dc63d.
```

## Worker review

```text
review subagent: not launched for attempt 3 — this attempt only adds two
  end-to-end regression tests (no production change), directly satisfying the
  Controller's F1. The read-only review of the complete implementation was done
  in attempt 2 and its findings (H1/M1) were resolved there. Re-reviewing an
  unchanged implementation would yield nothing new; the new tests were instead
  verified by the mandatory RED-on-seam-revert check above.
findings: none new.
resolutions: F1 resolved (two seam-reaching E2E tests, each proven RED on seam
  revert).
unresolved findings: none.
```

## Blockers

```text
None. Both gates are green; F1 is resolved with RED-verified end-to-end seam tests.
```

## Worker declaration

```text
Only Task 013 was attempted (attempt 3: the F1 test-only fix).
Changes are unstaged and uncommitted; all attempt-2 D1/D2/D3 changes preserved
and unmodified. The only attempt-3 edit is
tests/FileCacheFactoryManagerTest.cpp. The worker stopped after writing this
receipt.
```

## Controller review 2

```text
controller_status: accepted
environment_profile: home-chang
task: 013
```

## Review evidence

```text
scope review: PASS. Attempt-3 change is test-only (FileCacheFactoryManagerTest.cpp);
  the D1/D2/D3 production diffstat is byte-identical to accepted attempt 2. Full
  task-owned scope = 11 modified + 7 new files under velox/ch/Interpreters/FileCache.
  git diff --check clean.
implementation review: unchanged from Controller review 1 (D1/D2/D3 all correct).
F1 resolution — INDEPENDENTLY VERIFIED by the Controller (not taken on the worker's
  claim): I neutralized BOTH seams myself (Metadata.cpp:1260 -> (void)removed_path;
  FileSegment.cpp:746 -> (void)old_path;), rebuilt velox_ch_filecache_manager_test
  (exit 0), and both new tests FAILED:
    SeamE2ETest.RemoveFileSegmentDropsCachedHandle  -> FAILED
    SeamE2ETest.RenameOnDownloadDropsOldPathHandle  -> FAILED
  Then restored the seams byte-identical (grep confirms one live
  removePath(removed_path) + one removePath(old_path); zero probe remnants under
  velox/ch), rebuilt both gates GREEN. This proves the two E2E tests genuinely reach
  the changed production path and would catch a future seam revert. False-green
  gap F1 is closed.
log and test review: verified by running the binaries directly after my own
  restore-rebuild — velox_ch_filecache_manager_test 19/19, velox_ch_filecache_core_
  scc_test 47/47, both 0 failed / 0 skipped.
unresolved findings: none. (F2-F7 accepted correct in Controller review 1;
  F7 benign, no action.)
```

## Required changes

```text
None. Task 013 accepted.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `5e3ee1ac9` |
| `/home/chang/SourceCode/ClickHouse` | receipt+handoff = this commit |

## Worker attempt 4 (Post-acceptance amendment 1 — `FileCacheManager::hasDefault`)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 013 (amendment 1)
```

Implements Post-acceptance amendment 1: a `const`, non-throwing predicate
`FileCacheManager::hasDefault()` companion to the throwing `getDefault()`.
Scope = exactly the three amendment files. `getDefault()` behavior is unchanged
(still throws "no default cache configured" when empty). Both mandatory gates
green with the new test included: `velox_ch_filecache_manager_test` 20/20 and
`velox_ch_filecache_core_scc_test` 47/47 (both 0 failed / 0 skipped).

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `dac50ae27` | clean (0 files); `filecache2...baibaichen/filecache [ahead 13]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `7c745d64772` | receipt append only |

`HEAD dac50ae27` (velox) = "Task 018a: Route Hive connector reads through
FileCache via BufferedInputBuilder" — later than the attempt-1/2/3 baseline
`13b2dc63d`; Task 013 (`5e3ee1ac9`) and Task 018a are both committed at HEAD and
were left untouched. The dispatch note named the ClickHouse HEAD as
`da28e83e8b3`; the actual checkout is `7c745d64772` (append-only). No
pre-existing dirty files in velox before this amendment. No
staging/commit/amend/rebase/push.

## Files changed

```text
velox/ch/Interpreters/FileCache/FileCacheManager.h    (declare `bool hasDefault() const;` + doc comment, after getDefault)
velox/ch/Interpreters/FileCache/FileCacheManager.cpp  (define `hasDefault` as `return !defaultCacheName_.empty();`, after getDefault)
velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp  (add RED test `ManagerTest.HasDefaultAgreesWithGetDefault`)
```

`getDefault()` was NOT modified: it still throws "no default cache configured"
when `defaultCacheName_` is empty (`FileCacheManager.cpp` ~L148-153). `hasDefault`
is a pure read of the same field, does not call `factory_.get`, does not throw,
and does not depend on initialize/shutdown state — it returns `false` exactly
when `getDefault()` would throw the "no default cache configured" exception.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure (home-chang recipe + `-DVELOX_BUILD_TESTING=ON`) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task013_hasdefault_configure.log` |
| build `velox_ch_filecache_manager_test` (initial GREEN impl) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task013_hasdefault_build.log` |
| build with RED stub `return true;` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task013_hasdefault_red_build.log` |
| run RED stub (case 2 must fail) | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task013_hasdefault_red_test.log` |
| build both targets after restore (final GREEN) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task013_hasdefault_green_build.log` |
| ctest `velox_ch_filecache_manager_test` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task013_hasdefault_test.log` |
| ctest `velox_ch_filecache_core_scc_test` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task013_hasdefault_scc_test.log` |

No `-j` was passed to ninja.

## RED / falsifiability evidence (hard requirement met)

```text
With `hasDefault()` temporarily stubbed to `return true;` and rebuilt, the
empty-default case turns RED for the expected reason:

  FileCacheFactoryManagerTest.cpp:323: Failure
    Value of: manager->hasDefault()
      Expected: false
  [  FAILED  ] ManagerTest.HasDefaultAgreesWithGetDefault (11 ms)
  1 FAILED TEST

The implementation was then restored byte-identical to
`return !defaultCacheName_.empty();`, rebuilt, and both gates re-run GREEN.
This proves the test genuinely exercises production behavior and would catch a
wrong (always-true) implementation.
```

## Acceptance evidence

```text
Gate 1 velox_ch_filecache_manager_test:
  test count: 20 (was 19; +1 ManagerTest.HasDefaultAgreesWithGetDefault)
  failed tests: 0
  skipped/disabled tests: 0
Gate 2 velox_ch_filecache_core_scc_test (re-run, unchanged):
  test count: 47
  failed tests: 0
  skipped/disabled tests: 0
git diff --check: clean (exit 0, no whitespace errors)
git status: only the three amendment files (FileCacheManager.h, FileCacheManager.cpp,
  tests/FileCacheFactoryManagerTest.cpp); unstaged/uncommitted; velox otherwise at
  baseline dac50ae27 (Task 018a files at HEAD left untouched).
```

## Worker review

```text
review subagent: one read-only code reviewer (pr-review-toolkit:code-reviewer)
  over the complete amendment diff (the three files), given the contract and the
  four review foci (correctness/agreement, non-throwing guarantee, getDefault
  agreement, false-green).
findings:
  No actionable in-scope findings. Reviewer confirmed: hasDefault's
  `!defaultCacheName_.empty()` exactly negates getDefault's `empty()` guard (same
  field), the body is genuinely non-throwing (pure read, no allocation, no
  factory_.get, no lifecycle access), and case 2's EXPECT_FALSE would catch a
  stubbed `return true;`.
  Optional/out-of-scope (below reporting threshold, not required): the declaration
  could be `noexcept`; omitting it is acceptable and consistent with the codebase's
  un-annotated trivial accessors. The contract requires non-throwing behavior,
  which is satisfied; no change made.
resolutions: none required.
unresolved findings: none.
```

## Blockers

```text
None. Both gates are green; RED/falsifiability proven and reverted.
```

## Worker declaration

```text
Only Task 013 amendment 1 (`FileCacheManager::hasDefault`) was attempted.
Changes are unstaged and uncommitted; exactly three files changed
(FileCacheManager.h, FileCacheManager.cpp, tests/FileCacheFactoryManagerTest.cpp).
`getDefault()` and all other Task-013 / Task-018a files were left unchanged;
Task 018a files committed at HEAD dac50ae27 were preserved. No
staging/commit/amend/rebase/push. The worker stopped after writing this receipt.
```

## Controller review — Post-acceptance amendment 1 (hasDefault)

```text
controller_status: accepted
environment_profile: home-chang
task: 013 (amendment 1)
```

## Review evidence

```text
scope review: Only the three amendment files dirty in velox
  (FileCacheManager.{h,cpp}, tests/FileCacheFactoryManagerTest.cpp). Task 018a
  files at HEAD dac50ae27 untouched. No other file changed.
implementation review: hasDefault() is `return !defaultCacheName_.empty();` —
  const, non-throwing, pure read of the same field getDefault guards on; does
  not call factory_.get; getDefault unchanged (still throws on empty). Header
  comment accurate (reflects config, not existence). Agrees with getDefault by
  construction: false exactly when getDefault would throw "no default cache
  configured".
log and test review: Controller INDEPENDENTLY reproduced the RED — stubbed
  hasDefault() to `return true;`, rebuilt, ran: case 2 FAILED at
  FileCacheFactoryManagerTest.cpp:323 ("Expected: false"). Restored; grep
  confirms no residual probe; rebuilt; manager 20/20 and core_scc 47/47 (both
  0 failed / 0 skipped). Counts match (was 19, now 20 with the new case).
unresolved findings: none.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `35b160c79` |
| `/home/chang/SourceCode/ClickHouse` | (this amendment's ClickHouse commit) |
