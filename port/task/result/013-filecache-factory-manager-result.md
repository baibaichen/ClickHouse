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
