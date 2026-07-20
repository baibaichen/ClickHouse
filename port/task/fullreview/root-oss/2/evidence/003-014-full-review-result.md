# Full Review: Tasks 003–014 — `root-oss` Review 2 Evidence

> Single Phase-D agent, strict read-only cross-repository source-contract + structural review.
> Nothing was modified/staged/committed/pushed in either repository. Verdicts are re-derived
> from CH source and real callers; receipts, task files, and existing tests are treated as
> locators/corroboration, never as behavior truth. Per guide §D and §3 of
> `/root/chang/OneDrive/share_data/local-cache/filecache-port-authoring-guide.md`.

---

## 0. Scope, baselines, blind spots

### 0.1 Frozen baselines (verified, unchanged by documentation acceptance)
- **CH source truth:** `/root/oss/clickhouse` @ `197d60661b6d7637e98ef5878703ba36505f15c6`
  ("Task 014: Accept `FileCache` buffered input"). Working tree clean. CH FileCache lives in
  `src/Interpreters/FileCache/` (not `src/Interpreters/Cache/`; the senior-review note referencing
  `Cache/` was a different branch). Consumers read: `src/Disks/IO/CachedOnDiskReadBufferFromFile.{h,cpp}`,
  `CachedOnDiskWriteBufferFromFile.{h,cpp}`, `src/Common/ConcurrentBoundedQueue.h`,
  `src/Common/StatusFile.{h,cpp}`, `src/Common/ThreadPool.h`, `src/IO/OpenedFileCache.h`.
- **Accepted Velox implementation:** `/root/oss/velox` @ `b92a0ae3a96493aa63df44bc38514c68003db28e`
  ("Task 014: Add `FileCache` buffered input"). Port lives under `velox/ch/`.

### 0.2 Accepted-commit → task map (from `git log`, the ground truth for ownership)
| Task | Commit(s) | Production files (new/major) |
|---|---|---|
| 003 | `4bea8d15e`,`c755512a8`,`1b41f7338` | `FileCacheBoundedQueue.h`, shims, ProfileEvents/CurrentMetrics names |
| 004 | `f948fb6a4`,`5ed26f941` | `StatusFile.{h,cpp}`, `Guards.h` |
| 005 | `b21177a51` | `ThreadPool.{h,cpp}` |
| 006 | `d9f4517c5`,`b3c2832e1` | `FileCacheScheduler.{h,cpp}`, `FileCacheQueryIdScope.{h,cpp}` |
| 007 | `711a84850`,`7e7f157fc`,`1e3cc3209` | `ReadBufferFromVeloxReadFile.{h,cpp}`, `WriteBufferFromVeloxWriteFile.{h,cpp}` |
| 008 | `4b14de7f1`,`24686d2c6` | `FileCacheKey.{h,cpp}`, `FileCacheUtils.h`, `SipHash128.{h,cpp}` |
| 009 | `096ba0c9e` | `ShardedMap.h` |
| 010 | `89039901a` | `FileCacheSettings.{h,cpp}`, `FileCacheReadOptions.h` |
| 011 | `72b77cc2f` | `LRU/SLRU/Split/EvictionCandidates/IFileCachePriority`, `CacheUsage.h` |
| 012 | `a46ff4716` | `FileSegment.{h,cpp}`, `Metadata.{h,cpp}`, `FileCache.{h,cpp}`, `QueryLimit.{h,cpp}` |
| 013 | `bbda44d25` | `FileCacheFactory.{h,cpp}`, `FileCacheManager.{h,cpp}`, `OpenedFileCache.h` |
| 014 | `b92a0ae3a` | `FileCacheInputStream.{h,cpp}`, `FileCacheBufferedInput.{h,cpp}` |

### 0.3 What was reviewed
Every accepted 003–014 production file in full (both headers and `.cpp`), the reachable CH callers
that bind each contract, plus the focused test files and their `CMakeLists.txt` registration
(mono and non-mono). Method: five independent reconstruction+diff passes over disjoint subsystems,
followed by personal verification of every claimed Blocker and every cross-cutting adjudication
(direct-IO/background download, `THROW`/`THROW_LOGICAL`, Task-011 test-plane holes, `downloadImpl`
buffer alignment, ownership relabeling).

### 0.4 Blind spots and not-verdict subjects (explicit)
- **Tasks 016+** and **pre-release / post-019** items are *not* verdict subjects here; they are only
  checked for "is the deferral explicit and does it hide a *current* contract failure?".
- **E-plane primitive semantics** (Velox `WriteFile`/`ReadFile` undocumented failure modes) remain
  the irreducible residue per guide §E; consumer-side behavior is verdictable, the real producer is not.
- **`[sweep]`-tagged CH `.cpp` line numbers** inherited from the Phase-A ledger were re-anchored
  against verified headers wherever used as a verdict oracle; a handful of deep eviction-loop internals
  in `FileCache::doTryReserve` were read through their entry paths, not exhaustively line-diffed
  (flagged in §6).

---

## 1. Independent CH reconstruction + A/D reconciliation

### 1.1 Phase-A provisional ownership labels vs actual task scopes (ADJUDICATED)
The Phase-A ledger §1 tagged D8/D9/D11/D12/D13/D14 as `[INFERRED]` because the port task files were not
open at that time. The Phase-A **§9 controller resolution already corrected** these, and the corrected
labels are confirmed by the accepted commits (§0.2) and the task-file scopes:

| A-ledger dep | Phase-A §1 `[INF]` label | **Actual owner (commit-verified)** | Reconciliation |
|---|---|---|---|
| D8 priority/eviction | 010 `[INF]` | **011** (`72b77cc2f`) | relabel confirmed; 010 is *settings* |
| D9 `FileSegment` | 011 `[INF]` | **012** (`a46ff4716`) | relabel confirmed |
| D10 writer resume/reconcile | 012 | **012** | matches |
| D11 `Metadata`/queues | 013 `[INF]` | **012** (`a46ff4716`) | relabel confirmed |
| D12 `FileCache` core | 013 `[INF]` | **012** (`a46ff4716`) | relabel confirmed |
| D13 `QueryLimit` | 013 `[INF]` | **012** (`a46ff4716`) | relabel confirmed |
| D14 `FileCacheFactory` | 013/F-M `[INF]` | **013** (`bbda44d25`) | matches |
| D15 reader + `OpenedFileCache` | 014 | reader→**014**; `OpenedFileCache`→**013** | `OpenedFileCache` landed with the Manager (013), consumed by 014 |

**Adjudication:** the Phase-A §9 relabeling is **correct and complete**; no CH contract row is orphaned
or double-owned. The `[INF]` tags are superseded. The only nuance: `OpenedFileCache.h` is *owned* by
Task 013 (it is a Manager dependency per the Round-1 user-approved mapping) but *consumed* by the
Task-014 reader — this split is intentional and matches the accepted code.

### 1.2 Dependency-level reconciliation (independent reconstruction vs A rows)
Every A-ledger contract row was re-derived from the real callers and reconciled. The independent pass
**agreed with the A ledger on all external contracts**; the material *additions* the D-pass found are
structural/test-plane, not contract-surface (see §2, §4, §5). Directional coverage (call-site→behavior
and task→behavior) closes with no unmatched mapped cell; residual items are the over-port/hole
adjudications in §2.6 and the reopen findings in §5.

---

## 2. Per-task verdicts 003–014

Legend: **matches / drift / hole / over-port / unproven** per guide §D.

### Task 003 — `ConcurrentBoundedQueue` → `FileCacheBoundedQueue.h` — **ACCEPT**
Full overload set present and behavior-faithful: timed `tryPush(T&&,ms=0)` (`FileCacheBoundedQueue.h:61-64`),
default `tryPush(const T&,ms=0)` (`:56-59`), blocking `pop` (`:69-72`), **non-blocking `tryPop`** (no CV wait,
`:75-88`), `push(T&&)` (`:49-52`), `emplaceImpl<U>` perfect-forward (`:112-142`); `std::deque`+`std::mutex`+two
CVs (`:189-194`); predicates `finished||size<max_fill` / `finished||!empty`. This is exactly the overload set
the CH consumers bind at `FileCache.cpp:1863` (timed), `:1721` (non-blocking `tryPop`), loader `:2345,2282,2360`.
- **drift (minor):** `finish()` returns `void` vs CH `bool` (`ConcurrentBoundedQueue.h:201`). No caller captures
  the return value (`FileCache.cpp:1792,1895,1897,2273,2360`). Harmless.
- **over-port:** none — scope-limited omissions (`pushFront`, `emplace`, `clearAndFinish`, `isFinished`, `clear`)
  are correctly absent (no CH FileCache caller). Correct gate outcome.
- **unproven:** no explicit `finish()` double-call idempotency test (trivially correct).
- Tests focused + false-green-probed (timed-vs-immediate `:303`, no-CV-wait `:276`, move-not-consumed `:540`).
  Registered in `velox_ch_common_test`.

### Task 004 — `StatusFile` + `Guards.h` — **ACCEPT**
- **matches:** byte-exact 3-line `writeFullInfo` (`StatusFile.cpp:81-91` = CH `:38-43`); dtor order
  `closeNoThrow`→`unlink`, both non-throwing (`:141-144`); `open O_WRONLY|O_CREAT|O_CLOEXEC 0666`,
  `flock LOCK_EX|LOCK_NB` via `folly::File::try_lock`, `ftruncate 0`, `lseek SEEK_SET`. `Guards.h` lock
  inventory + order `CachePriorityGuard > CacheMetadataGuard > KeyGuard > FileSegmentGuard`, **`CacheStateGuard`
  = separate `std::timed_mutex` with `tryLockFor`** (`Guards.h:85-120`), non-interchangeable struct-wrapped
  `Lock` types (compile-time `static_assert` test).
- **drift (minor):** `flock` collision text "…same path…" vs CH "…same directory…"; CH error codes
  (`CANNOT_OPEN_FILE`/`CANNOT_TRUNCATE_FILE`/`CANNOT_SEEK_THROUGH_FILE`) collapse to `VeloxRuntimeError`.
- **over-port (minor, benign):** `writePid()` (`StatusFile.h:55`) — cache uses only `writeFullInfo`; ported +
  tested. Keep with a scope note.
- **explicit deferral:** the CH unclean-restart read-before-truncate diagnostic (`StatusFile.cpp:52-63`) is
  **absent** — this is the Round-1 pre-release gate ("does not block Tasks 011-014"). Does not hide a current
  failure (the primary `flock` exclusion contract is present and tested, `StatusFileAndGuardsTest.cpp:162-192`).
  *Documentation debt:* no in-code annotation records the deferral (Minor, §5).

### Task 005 — `ThreadPool` → `ThreadPool.{h,cpp}` — **ACCEPT**
- **matches:** `scheduleOrThrowOnError`/`wait` (rethrows first exception), **pool is injected by reference**
  (`FileCacheWorkerPool &`, `ThreadPool.h:164`) and never owned/grown/resized by `FileCache`; join-before-destroy
  enforced by fatal `VELOX_CHECK(!joinable())` in the worker dtor (`ThreadPool.cpp` worker); local concurrency
  cap via `inFlight_`/`backlog_`. Round-1 B2 drop confirmed: `FilesystemCacheEvictionThreads{,Active,Scheduled}`
  absent. Fail-closed capacity guard `numThreads()` present.
- Tests: `EXPECT_DEATH` false-green probes for the un-joined-worker and move-over-joinable paths; barrier-based
  concurrency-cap test (no sleeps). Clean.

### Task 006 — `FileCacheScheduler` + `FileCacheQueryIdScope` — **ACCEPT**
- **matches (the historically-reopened invisible contract):** `schedule()` (immediate) **preempts a pending
  `scheduleAfter()` (delayed)** — `cancelTimerLocked()` + `queueImmediateLocked()` + generation bump
  (`FileCacheScheduler.cpp:104-125,152-153`), and `scheduleAfter` returns `false` while an immediate is pending.
  `deactivate()` drains a running tick (`cv_.wait` on `callbackInFlight_`, `:162-187`) and is idempotent; tasks
  hold `weak_ptr` (never raw `this`). Directly false-green-probed
  (`ScheduleAfterWhileRunningDoesNotReplacePendingImmediate`, `ScheduleAdvancesDelayedTask` with a
  `ManualTimekeeper` that is never advanced).
- **approved drift / deferral:** caller identity `None:<tid>` (`FileCacheQueryIdScope.cpp:53`) is the Round-1
  interim; `None:<threadname>:<tid>` restore is **F-CALLERID → Task 017**. Scheduler `recursive_mutex`
  resolution is **Task 017**. Both work correctly now and do not hide a current failure. *Documentation debt:*
  neither deferral is annotated in code (Minor, §5).

### Task 007 — IO adapters (detach/handoff corrective) — **ACCEPT**
The corrective wave (`1e3cc3209`) is faithful and test-proven (cross-checked against the senior-review M1–M6):
- **F1/M1** `ReadBufferFromFileBase::set(nullptr,0)` now `detach()`s → empty internal buffer, `available()==0`,
  owned `BufferPtr` retained, offset preserved (`ReadBufferFromVeloxReadFile.cpp:249-263`); lazy owned-window
  restore in `nextImpl` (`:191-205`). `ReaderSetNullDetaches` asserts `internalBuffer().empty()`; M1 mutation caught.
- **F2/M2+M6** `releaseOwnedBuffer()` (`:121-146`) frees the owned pool allocation + detaches views on the calling
  thread and is invoked at the successful handoff (`FileCacheInputStream.cpp:856`); this closes the **latent
  query-pool UAF** (a handed-off reader lives in the long-lived `FileSegment` and could otherwise be destroyed on
  a background worker after the query pool is torn down; `velox::Buffer` holds a raw `MemoryPool*`).
  `BackgroundHandoffReleasesQueryPoolMemory` asserts `usedBytes()==0` and tears the query pool down before the
  background download completes; M6 removal reproduces the SIGSEGV.
- **007↔012 boundary is clean:** `WriteBufferFromVeloxWriteFile` is a pure append adapter with no `FileSegment`
  knowledge; all reconcile logic (`resetRemoteFileReader`/`completePartAndResetDownloader`/
  `setDownloadFinishedWithoutContinuation`) lives in the reader/SCC, not folded into the 007 adapter.
- `WriteBufferFromVeloxWriteFile::jumpToPosition` correctly absent (different interface; `FileSegment::write`
  never calls it) — not an over-port, not a hole.

### Task 008 — `FileCacheKey` + `FileCacheUtils` + `SipHash128` — **ACCEPT**
- **matches:** `fromKeyString` validates `size()==32` then reproduces CH `unhexUInt<UInt128>` accumulation exactly,
  including the **non-hex→0xFF addition (not OR) carry** (`FileCacheKey.cpp:64-76`, distinguished by
  `MalformedCarryHighWord/LowWord` at `LeafTypesTest.cpp:141-163`); `toString`=lowercase hex; `fromPath`=`sipHash128`;
  private `UInt128` ctor with exactly four named factories; `roundUpToMultiple` remainder-based with the **exact**
  overflow message (`FileCacheUtils.h:49-51`); `roundDownToMultiple(_,0)=num`. `SipHash128` uses the CH `v2^=0xff`
  finalization (`SipHash128.cpp:118`).
- **exceeds deferral (good):** CH-derived **golden vectors are present** (`LeafTypesTest.cpp:53-79`) — the Round-1
  post-019 deferral was for golden vectors *and* the mutation probe; the port already added the golden vectors. The
  `0xff→0xee` mutation and the malformed-char *differential fuzz* remain the post-019 residue and do not hide a
  current failure (correctness is already probed).
- **drift (minor):** length-error message wording differs from CH.

### Task 009 — `ShardedMap.h` — **ACCEPT**
- **SD1 (registered + user-signed):** `Map = folly::F14FastMap` replaces CH `std::unordered_map`. This is the
  guide-§3 guarantee-changing swap, and it **is** the Round-1 SD1 deviation with the user-approved
  no-reference-escape contract (Round-1 lines 46-47, 138). It is therefore **not** unregistered drift. The
  contract is enforced structurally (callbacks return by value; `WithShardReturnCopiesValue` proves `auto` decay)
  and the 32-shard/`withShard`/`forEachShard`/relaxed-atomic-`size` structure matches CH.
- **matches:** exception-safe size accounting via a `SizeGuard` measuring the actual pre/post map-size delta
  (fires on throw), matching CH's `SCOPE_EXIT`.
- **unproven:** the 009 oracle *"emplace that throws must not increment size"* is only exercised as
  throw-*after*-insert / throw-*after*-erase; the throw-*during*-emplace (allocation-abort, no insert) path is not
  directly probed. Implementation is provably correct (delta is measured, not predicted). Minor (§6).

### Task 010 — `FileCacheSettings` — **ACCEPT**
- **matches:** the Round-1 reload rule is honored — `applyConfigs` compares **values** via
  `settingsEqual(actual, target.config)` (`FileCacheManager.cpp:426-428`), *not* config-payload presence; equal
  reload is a no-op (`EqualReloadIsNoOp`), changed value reaches apply (`ApplySettingsRunsOutsideRegistryLock`).
  Allowed-root path-containment authorization present (`FileCacheSettings.cpp:253-262`, 8 containment tests).
- **unproven (minor):** no seam-level false-green proving `applySettingsIfPossible` is *not entered* for an
  unchanged value (end-state asserted, not the bypass); allowed-root tests pass `"/"` so they do not exercise a
  restrictive root. Neither blocks 015.

### Task 011 — Priority / Eviction — **REOPEN (test-plane)**
Structural translation is **fully faithful** (all rows `matches`): LRU=`std::list` splice/`erase` (SD5),
SLRU two-queue with rollback, `SplitFileCachePriority` `std::array<…,3>`, resumable cursors under
`eviction_pos_mutex`, `invalidated_refs` `std::deque`, atomics/lock-free `decrementSize`/`invalidate`,
`afterEvictWrite`-before-`afterEvictState` ordering (`FileCache.cpp:1336+`), `EvictionCandidates::evict()`
off-lock with failure-continue. **SD2** (`absl::flat_hash_map/set`→`F14`) applied and registered; **`original_queue_types`
correctly kept `std::unordered_map`** (`EvictionCandidates.h:226`). **`CacheUsage.h` correctly stripped** of
`CacheUsagePerUser` / `FilesystemCacheOvercommitUsers`.

Reopen reasons (all coverage/evidence, **none an implementation blocker**):
- **HOLE — `MoveEvictionPos`:** `friend class ::FileCacheTest_MoveEvictionPos_Test` is declared
  (`LRUFileCachePriority.h:41,201`) but **no such test exists** (`PriorityEvictionTest.cpp` has 6 tests, none
  named MoveEvictionPos). The CH gtest `gtest_filecache.cpp:2421` cursor-advance regression is unmigrated. The
  cursor `move`/`moveEvictionPosIfEqual` splice path is thus untested.
- **UNPROVEN — SLRU downgrade failpoint:** `file_cache_slru_downgrade_fail_before_finalize` is armed
  (`SLRUFileCachePriority.cpp:584`) but **no test arms it** (`PriorityEvictionTest` arms only
  `file_cache_modify_size_limits_fail` in `SLRUModifySizeLimitsRollbackOnThrow`). The downgrade rollback scope-guard
  is unexercised.
- **HOLE — SLRU dynamic-resize eviction:** CH `SLRUDynamicResizeCorrectEviction` (`gtest_filecache.cpp:2048`,
  evict from *both* sub-queues) has no Velox equivalent (only LRU failure-rollback is covered).
- Minor: `takeKeptAliveCacheUsage` uses range-`insert` instead of node-`merge` (F14 has no `merge`; benign, callers
  pass rvalues); `getSLRUSizeRatio`, `requiresAfterEvictState()`, `getHoldSize/getHoldElements` over-port (benign,
  no CH caller). `shuffle` RNG source differs (semantics identical).

### Task 012 — Center SCC — **REOPEN**
**012·FileSegment + FileCache core — accept (faithful).** Near-verbatim translation. Verified adjudications:
- **`call_once` replacement (approved):** `FileCache::initialize()` uses `std::lock_guard<std::mutex>` +
  `initialize_completed` flag (`FileCache.cpp:425-476`) — one-at-a-time, publication-after-success, retry-after-throw;
  the static-linked-`pthread_once`-abort rationale is accurate. `matches`.
- **Typed errno consumer (approved):** `FileSegment::write` catches `FileCacheErrnoException`, gates the reconcile on
  `ENOSPC||EDQUOT`, `downloaded==0`→remove-file else `chassert(downloaded ≤ physical ≤ reserved)`+`downloaded=physical`,
  rethrows; catch-all marks-failed+rethrows with **no** reconcile. The reconcile runs in **production**, not a test
  double; negative tests (`GenericExceptionDoesNotReconcile`, `DifferentErrnoDoesNotReconcile`) prove a generic/EIO
  exception does not take the branch. `matches`.
- **`reset`-before-`completePartAndResetDownloader` ordering:** enforced by `chassert(!download_data||!remote_file_reader)`
  before publishing NO_CONTINUATION (`FileSegment.cpp:788-801`) + single-threaded ordering test. The **concurrent
  two-thread race** RED (a thread `extractRemoteFileReader`s a still-borrowed reader) is deferred to the reader
  (014) and is currently **unproven** (§6) — invariant is enforced, so not a functional hole.
- **Error-code collapse:** no over-collapse — CH itself expresses no-space as `bool`+`failure_reason` and reserves
  `LOGICAL_ERROR` for logic violations; the port preserves this split (`VELOX_FAIL` only for logic). `matches`.
- **Member/destruction order contractual:** `main_priority` declared before `CacheMetadata metadata` (metadata
  destroyed first), two `CachePriorityGuard` (`cache_guard`,`queue_guard`) + `cache_state_guard` +
  `shared_timed_mutex dynamic_resize_lock` all present. `matches`.
- Minor: handoff-invariant comment (CH `FileSegment.h:226-227`) not carried into the port header (doc);
  `getFlagsForLocalRead()` replaced by path-keyed `OpenedFileCache` invalidation (registered over-port; reader uses
  a Velox file reader, not fd flags).

**012·Metadata + QueryLimit — reopen.** Structure matches: **SD3** (`KeyMetadata = std::map`) preserved, **SD4**
(`MetadataBucket = F14FastMap`) carries the no-reference-escape comment and is structurally safe (copy-before-release
at `Metadata.cpp:390`; `IteratorImpl` holds the bucket lock continuously while its iterator is live); `shutdown()`
cancels queues **before** joining workers and is a **named** precondition invoked from
`deactivateBackgroundOperations` in the Round-1 order (set shutdown → join load thread → deactivate scheduler tasks →
wait eviction pool → `metadata.shutdown()`); `DownloadQueue` weak_ptr, `CleanupQueue`, background download pins via a
local `FileSegmentsHolder`. Reopen reasons:
- **DRIFT (minor, under accepted baseline) — `THROW` vs `THROW_LOGICAL` collapse:** CH throws `BAD_ARGUMENTS` vs
  `LOGICAL_ERROR` (`Metadata.cpp:300,302`); the port emits `VELOX_FAIL`→`VeloxRuntimeError` for both
  (`Metadata.cpp:338-339`). The two real CH callers are `FileCache::removeFileSegment` (user DDL → `THROW`/BAD_ARGUMENTS,
  `FileCache.cpp:2068`) and `FileCache::getFileSegmentInfos` (internal introspection → `THROW_LOGICAL`/LOGICAL_ERROR,
  `:2704`) — **both are system-table/observability paths not wired in the 003–014 reader scope**. This falls inside the
  Round-1 accepted "Velox exception baseline" collapse; the tests (`KeyNotFoundPolicyThrow`/`…ThrowLogical` asserting
  only `VeloxRuntimeError`) faithfully reflect the collapse but cannot *distinguish* the policies — a false-green for
  the *distinction* contract. **Forward obligation (Round-1 error-code rule):** when Task 015+/017 wires a caller whose
  behavior distinguishes user-error from internal-error, a typed subtype must be reintroduced.
- **HOLE (coverage) — mandatory "queue pipeline" row absent from the center-SCC binary:** the timed-`tryPush`/
  non-blocking-`tryPop` proof lives in `velox_ch_common_test` (`BasicShimsTest.cpp:269-280`), **not** in
  `velox_ch_filecache_core_scc_test` (`tests/CMakeLists.txt:82-106`). The Task-012 acceptance row is satisfied in a
  *different* binary; a CI gate running only the SCC binary would miss a `FileCacheBoundedQueue` regression.
- **MAJOR (cross-cutting) — direct-IO source + background download (`downloadImpl`) alignment gap:** see §5-B1;
  owned jointly with Task 014.
- Minor over-ports: `DownloadInfo` and `getFileNameForFileSegment` made public for tests; `QueryContextHolder` made
  movable; `releasable()` uses `use_count()==1` without the named `isSharedPtrUnique` acquire helper; `CleanupQueue`
  uses `F14FastSet` (consistent with SD2 policy but not registered as its own SD entry).

### Task 013 — Factory / Manager / `OpenedFileCache` — **ACCEPT**
- **matches:** `getOrCreate` returns the shared aliased `FileCachePtr` for same name/path (tests assert identical raw
  pointer); `get/getByName/getAll/getUniqueInstances`; F14 registry with `shared_ptr` values stable across rehash;
  Manager is port-only and owns scheduler/workerPool/`OpenedFileCache`/Factory with **reverse member-destruction
  order** (`factory_` last-declared→destroyed-first; `mutation_mutex_` outlives all); deactivation **outside** the
  registry lock (seam-tested); global-instance publish/uninstall order; `applyConfigs` fail-close transaction with
  `rollbackNewBindingsLocked`; **pool fail-closed** — `growWorkerBudget` lives only in the Factory (Manager-owned),
  `FileCache` has no pool pointer; `checkedAdd` reused (no private Manager helper). The `instance()` mechanism is an
  atomic pointer (per the task spec, since CH has no Manager) — behaviorally identical to CH's Meyer singleton from a
  caller's view.
- **OpenedFileCache mapping (Round-1 user-approved, binding) — compliant:** fixed **1024** buckets
  (`kBuckets`), per-bucket `std::map<(path,flags),weak_ptr<OpenedFile>>` + co-located `std::mutex`, values
  `weak_ptr`, custom deleter auto-closes on last release; **both** the bucket vector and per-bucket maps use
  `velox::memory::StlAllocator` charged to the injected `MemoryPool` (no untracked container); bucket hash
  substituted `CityHash64`→`std::hash<std::string>` **for internal bucket-index only**; **`ProfileEvents.h` not
  edited by Task 013** (git log: last touched by Task 003). Port-only enhancement: the deleter captures a
  `weak_ptr<BucketState>` so a handle can outlive the cache (ASan-proven).
- **over-port — `FileCacheFactory::create`/`remove`:** the Phase-A sweep found no CH caller, but the Task-013 spec
  explicitly required them and they are tested → **justified over-port**, not dead code.
- **explicit deferral:** `StatusFile` unclean-restart diagnostic is a pre-release gate; does not affect any
  Factory/Manager contract (no code reads the status file). Undocumented deferral (Minor).
- Minor: `normalizePath`/`settingsEqual` are byte-duplicated in the Factory and Manager anonymous namespaces (drift
  risk if one is fixed and not the other); `MemoryPool` charge for `OpenedFileCache` allocations is functionally
  wired but **not asserted** by any `usedBytes()` test (unproven).

### Task 014 — Buffered input reader — **ACCEPT with one Major cross-cutting reopen item**
- **matches:** `ReadType` enum, `ReadInfo` reuse, the three FileCache selectors
  (`getDownloadedContiguousOrEmpty`/`get`/`getOrSet`), `DOWNLOADING`→`wait`→re-check, downloader election +
  predownload, `PARTIALLY_DOWNLOADED_NO_CONTINUATION` handling, cache/bypass/put-in-cache seek math, per-chunk
  cache write with `readerCanBeReused`, EOF-before-region reconcile, **exception path never returns a canceled
  reader** to the segment, `queryContextHolder` acquired once and never reset by seek, in-buffer seek O(1) vs
  out-of-buffer seek releases downloader + rebuilds, `skip_cache_on_disk_failure` propagation,
  `getRemoteFileMetadata==nullopt` handled gracefully (truncation metadata unavailable; tests do not assume a real
  metadata source).
- **F3/M3 direct-IO predownload skip** implemented + tested (`DirectIoPredownloadSkipsWhenUnaligned`); **F4a/M4**
  `isBuffered` `checkedAdd(...)-1` overflow guard; **F4b/M5** cross-segment disk-failure skip. `readBigAt` correctly
  absent (different `SeekableInputStream` interface — not a deferral). `E3` zero-copy reuse invariant holds and is
  tested (`ReaderHandoffSatisfiesFileSegmentInvariants`, `ReaderHandoffQ1Q2`). Test counts verified: `velox_ch_io_test`
  33, `velox_ch_filecache_buffered_input_test` 24; no DISABLED/skipped/sleep-based tests (`spinUntil` is a bounded
  yield-loop).
- **MAJOR reopen (see §5-B1):** direct-IO source + background download alignment gap.
- Minor holes vs CH reader robustness (all non-blocking): `getCacheReadBuffer` lacks the CH rename-race lock+retry
  (`CachedOnDiskReadBufferFromFile.cpp:366-394`), the externally-truncated-segment bypass (`:448-477`), and the
  zero-byte cache-file guard (`:474-475`); `completeCurrentSegmentAndAdvance` drops CH's
  `assert(offset > completed_range.right)`; CACHED `updateReadStateIfNeeded` re-prepares every chunk
  (over-conservative, perf-only, documented).

### 2.6 Over-port / hole gate outcomes (consolidated)
- **Over-port candidates → resolved:** `StatusFile::write_pid` (kept, tested, benign); `FileCacheFactory::create/remove`
  (justified by task spec); Overcommit hooks `getUsageStatPerClient`/`touchClientAccess`/`collectIdleClients`
  (**have real callers** in the ported `FileCache.cpp` — base no-op/throw correctly ported → `matches`);
  `EvictionCandidates::bytes()`/`requiresAfterEvictWrite()`/`getOriginalQueueType()` (**have callers** → `matches`);
  benign-dead: `getSLRUSizeRatio`, `requiresAfterEvictState()`, `getHoldSize/getHoldElements`. No over-port must be deleted.
- **Holes → resolved:** 007↔012 boundary clean; `shutdown()`-before-`~CacheMetadata` is a named precondition;
  `OpenedFileCache` weak_ptr lifetime = reader lifetime (Manager-owned, ASan-proven); `OriginInfo::operator==`
  (user_id only) vs `OriginPoolKey` (all fields) is the intentional access-vs-dedup split (documented via
  `OriginPoolKeyHash`). Remaining genuine gaps are the Task-011 test holes, the SCC-binary queue-pipeline coverage,
  the concurrent reset-before-complete race, and the direct-IO/background gap.

---

## 3. End-to-end coverage matrix (call-site → behavior → implemented? tested? probed?)

| Reachable call-site (CH) | Required behavior | Owner | Impl? | Tested? | False-green probe? |
|---|---|---|---|---|---|
| `FileCache.cpp:1863` / `:1721` | timed `tryPush` / non-blocking `tryPop` | 003 | ✅ | ✅ (common bin) | ✅ |
| `FileCache.cpp:517` + `StatusFile.cpp:38-43,109-116` | 3-line diag; dtor close→unlink | 004 | ✅ | ✅ | ✅ (closeNoThrow) |
| `FileCache.cpp:1758,1899` | pool schedule/wait, join-before-destroy | 005 | ✅ | ✅ | ✅ (EXPECT_DEATH) |
| `FileCache.cpp:592/1955` vs `:563/1641/1957` | immediate ≻ delayed; deactivate drains | 006 | ✅ | ✅ | ✅ (ManualTimekeeper) |
| `WriteBufferToFileSegment.cpp:63-109` | already-open append relay | 007 | ✅ | ✅ | ✅ (M1/M2/M6) |
| `fromKeyString` / `roundUpToMultiple` | length+hex; checked round-up msg | 008 | ✅ | ✅ | ✅ (carry, overflow) |
| `Metadata` origins `withShard` | sharded find/emplace, exception-safe size | 009 | ✅ | ✅ | ✅ (`auto`-decay) |
| `FileCache.cpp:1827/1734/1737/1767` | collect/evict/afterEvictWrite/afterEvictState | 011 | ✅ | ✅ | ✅ (resize-fail) |
| `FileSegment.cpp:1441` ← reader `:906` | increasePriority splice/SLRU move + dedup | 011 | ✅ | ⚠️ partial | ❌ (atomic_flag dedup) |
| priority `move`/cursor advance | `moveEvictionPosIfEqual` | 011 | ✅ | ❌ **hole** | ❌ (MoveEvictionPos) |
| SLRU downgrade rollback | `…downgrade_fail_before_finalize` | 011 | ✅ | ❌ **unproven** | ❌ (failpoint unarmed) |
| reader `:689/667/1114/1272/1175/1025` | FileSegment downloader/reserve/write/complete/reader-reset | 012 | ✅ | ✅ | ✅ (errno ±) |
| reader reset-before-complete (concurrent) | cross-method race | 012/014 | ✅ | ⚠️ single-thread only | ❌ **unproven race** |
| `FileSegment::write` errno reconcile | ENOSPC/EDQUOT physical-size reconcile | 012 | ✅ | ✅ | ✅ (generic/EIO neg) |
| `lockKeyMetadata` THROW vs THROW_LOGICAL | BAD_ARGUMENTS vs LOGICAL_ERROR | 012 | ⚠️ collapsed | ⚠️ non-distinguishing | ❌ (false-green) |
| SCC binary queue-pipeline row | timed push/pop in SCC test | 012 | ✅ | ⚠️ wrong binary | — |
| reader `:132` | `getQueryContextHolder` lifetime | 012/014 | ✅ | ✅ | ✅ (seam) |
| reader `:258/246/233` | `getOrSet`/`get`/`getDownloadedContiguousOrEmpty` | 012 | ✅ | ✅ | ✅ |
| `Context` `[sweep]` | Factory `instance/getOrCreate/get` | 013 | ✅ | ✅ | ✅ (mutation) |
| reader `:362/895` | `OpenedFileCache::get/remove` fd sharing | 013/014 | ✅ | ✅ (behavior) | ⚠️ pool-charge unproven |
| handed-off reader → background download | `downloadImpl` completes segment | 012/014 | ⚠️ **direct-IO gap** | ❌ (no DIO+bg test) | — |
| reader detach/handoff/query-pool | `set(nullptr,0)`/`releaseOwnedBuffer` | 007/014 | ✅ | ✅ | ✅ (M1/M6) |

---

## 4. Structural-deviation ledger

| SD | CH structure | Port | Status |
|---|---|---|---|
| SD1 | `ShardedMap::Map = std::unordered_map` | `folly::F14FastMap` (no-ref-escape) | **registered + user-signed** (Round-1); enforced + probed. OK. |
| SD2 | `absl::flat_hash_map/set` (EvictionCandidates/EvictionInfo) | `F14FastMap/F14FastSet` | registered; applied. `original_queue_types` correctly kept `std::unordered_map`. OK. |
| SD3 | `KeyMetadata = std::map` | `std::map` | preserved. OK. |
| SD4 | `MetadataBucket = std::unordered_map` | `F14FastMap` | registered; structurally safe (copy-before-release; lock-held iteration). **No test-level no-ref-across-mutation proof** (unproven, §6). |
| SD5 | LRU/SLRU queues + `FileSegments` = `std::list` | `std::list` | preserved. OK. |
| SD6 | CH thread pool | injected Folly executor/backlog/future | preserved; fail-closed. OK. |
| SD7 | CH delay thread + multimap | `Timekeeper` + Future continuations | preserved (scheduler immediate≻delayed proven). OK. |
| SD9 | CH-owned `Memory<>` | MemoryPool-charged `BufferPtr` | preserved. OK. |
| — | `CleanupQueue` `std::unordered_set` | `F14FastSet` | **unregistered swap** — consistent with SD2 policy and safe (no ref held across mutation), but not its own registered SD entry (Minor, §5). |
| — | `EvictionCandidates::merge` (F14 has no `merge`) | range-`insert` | benign behavioral drift (callers pass rvalues). OK. |

No guarantee-changing swap is unregistered-and-unsafe. The only two book-keeping gaps are the missing SD4
test-level proof and the unregistered `CleanupQueue` `F14FastSet` — both structurally safe today.

---

## 5. Findings — Blocker / Major (Minor summarized)

### Blocker
**None.** The one item escalated to "Blocker" by the reader pass (direct-IO + background download) is
**fail-closed** on personal verification (see B1) and is therefore a **Major**, not a hard Blocker.

### Major

**B1 — Direct-IO source + background download buffer-alignment gap (owner: Task 012 `downloadImpl` + Task 014 reader).**
- **CH:** `Metadata.cpp:939-964` — CH's remote reader is never `O_DIRECT` (direct IO applies only to *local cache-file*
  reads, `Metadata.cpp:1266-1268`), so its background buffer needs no alignment.
- **Velox:** the port makes the **source** file potentially direct-IO — `FileCacheInputStream.cpp:65`
  (`sourceReadFile()->directIo(alignment)`), and the stashed remote reader inherits that alignment
  (`ReadBufferFromVeloxReadFile.cpp:389-396`, 3-arg ctor). The **foreground** path over-aligns its output buffer
  (`FileCacheInputStream.cpp:106-124`), but the **background** path allocates a plain, pool-aligned buffer
  (`Metadata.cpp:999`, `AlignedBuffer::allocate` without the over-align step) and calls `buf->set(...)`, whose
  direct-IO branch requires `ptr % directIoAlignment_ == 0` (`ReadBufferFromVeloxReadFile.cpp:266-280`). For a
  direct-IO source this **throws** inside `downloadImpl`.
- **Impact:** fail-closed, not a crash/corruption — the worker catches it (`Metadata.cpp:943-960`:
  `setDownloadFailed()` + log + continue). Net effect: **background download never succeeds for direct-IO sources**;
  such segments are repeatedly marked download-failed. Untested and **undocumented** (no deferral note; the
  senior-review F3 handled only the foreground predownload).
- **Smallest RED:** a `DirectIoReadFile` source with `alignment=512` + `backgroundDownloadThreads=1`; read one
  aligned chunk to force a handoff; `spinUntil(segmentDownloaded)` — currently times out / segment ends
  `DOWNLOAD_FAILED`.
- **False-green probe:** delete the `directIoAlignment_>1` check in `ReadBufferFromFileBase::set`
  (`ReadBufferFromVeloxReadFile.cpp:266`) — a test that doesn't combine direct-IO + background download stays green.
- **Blocks Task 015:** **Yes, conditionally** — resolve before 015 relies on background download of direct-IO
  sources (over-align the `downloadImpl` buffer to `buf->directIoAlignment_`, mirroring the foreground path) **or**
  add an explicit fail-closed guard/deferral documenting direct-IO sources as unsupported for background download.

### Major (test-plane / coverage — block the zero-unresolved gate, not the runtime)

**B2 — Task 011 `MoveEvictionPos` cursor-advance hole (owner 011).** Friend declared, no test
(`LRUFileCachePriority.h:201`; CH `gtest_filecache.cpp:2421`). RED: move a middle entry that a cursor points at,
assert both `reserve_eviction_pos`/`background_eviction_pos` advance. False-green: drop `moveEvictionPosIfEqual`.
Blocks-015: no (impl correct; coverage only).

**B3 — Task 011 SLRU downgrade-failpoint unproven + SLRU dynamic-resize hole (owner 011).**
`file_cache_slru_downgrade_fail_before_finalize` armed but never triggered; `SLRUDynamicResizeCorrectEviction`
unmigrated. RED: arm the failpoint mid-downgrade and assert both sub-queue sizes/states roll back;
resize an SLRU with entries in both sub-queues and assert usage ≤ new limit. Blocks-015: no.

**B4 — Task 012 concurrent reset-before-complete race unproven (owner 012/014).** Invariant enforced by
`chassert(!download_data||!remote_file_reader)` (`FileSegment.cpp:798`) + a single-thread ordering test, but the
two-thread race (a thread `extractRemoteFileReader`s a still-borrowed reader) has no RED. Blocks-015: no
(invariant enforced), but is the exact class of concurrency gap this review exists to catch.

**B5 — Task 012 queue-pipeline coverage in the wrong binary (owner 012).** The mandatory timed-`tryPush`/
non-blocking-`tryPop` proof is only in `velox_ch_common_test`, not `velox_ch_filecache_core_scc_test`
(`tests/CMakeLists.txt:82-106`). Add the queue-pipeline case (or the common test) to the SCC binary. Blocks-015: no.

### Minor (summarized; full rows in the per-task sections)
`finish()` void-vs-bool (003); `flock` message + generic error codes + `writePid` over-port + missing unclean-restart
deferral annotation (004); deferral annotations for F-CALLERID and scheduler `recursive_mutex` (006); key length-error
wording + post-019 deferral annotations (008); 009-oracle throw-during-emplace unproven; settings no-op bypass +
allowed-root restrictive-root coverage (010); `takeKeptAliveCacheUsage`/`getSLRUSizeRatio`/`requiresAfterEvictState`
benign (011); `THROW`/`THROW_LOGICAL` collapse false-green + `DownloadInfo`/`getFileNameForFileSegment` public +
`QueryContextHolder` movable + `releasable` use_count + `CleanupQueue` unregistered `F14FastSet` (012);
`normalizePath`/`settingsEqual` duplication + `OpenedFileCache` pool-charge unproven + StatusFile deferral annotation
(013); `getCacheReadBuffer` rename-race/truncated/empty-file robustness + missing advance assertion + over-conservative
re-prepare (014).

---

## 6. Missing evidence / E-plane status

### 6.1 Unproven rows (focused assertion or recorded deferral missing)
- **011:** eviction-cursor resume path; `CollectStatus` SUCCESS/CANNOT_EVICT distinction; `increasing_priority`
  atomic-flag dedup; `EvictionCandidates::evict()` continue-after-per-segment-failure; `invalidateBeforeRemove` skip;
  SLRU downgrade failpoint; `MoveEvictionPos`; SLRU dynamic-resize.
- **012:** concurrent reset-before-complete race (B4); SD4 no-reference-across-mutation test-level proof;
  `releasable()` unit-level; `THROW`/`THROW_LOGICAL` distinction unprovable-by-construction (accepted collapse).
- **009:** throw-*during*-emplace no-size-increment oracle.
- **013:** `OpenedFileCache` `MemoryPool` charge (`usedBytes()`) unasserted; settings equal-reload *bypass* seam.
- **014:** direct-IO + background download (no test); `getCacheReadBuffer` rename-race / truncated / empty-file.
- **Deep internals not exhaustively line-diffed:** `FileCache::doTryReserve` eviction candidate loop and
  `FileSegmentsHolder::completeAndPopFrontImpl` body were read through their entry paths (verbatim `complete()` gives
  high confidence) but not line-by-line; re-open if used as a future RED oracle.

### 6.2 E-plane (guide §E — Velox primitive semantics)
- **E1/E2 (structured errno):** consumer side is **closed** (typed `FileCacheErrnoException` + real-file-backed
  double + negative tests, `FileSegment::write`); the **real structured-errno producer** in the concrete writer
  remains an explicit **pre-release gate** (Round-1). Not hiding a current failure; no reconcile-every-exception
  fallback exists.
- **E3 (zero-copy reuse):** **closed** by the corrected 007/014 detach + lazy owned-window restore +
  `releaseOwnedBuffer` + background-continuation tests.
- **E4 (fd sharing across rename):** `OpenedFileCache` behavior tested; CH rename-race lock+retry not ported (Minor
  §5, 014).
- **E5–E9** (timed-mutex granularity, SharedMutex fairness, EOF-vs-truncation, `reserve_hint`, cv wakeup): latency/
  fairness-only or predicate-guarded — correctness-safe; no current failure.

### 6.3 Deferral verification (explicit? hides a current failure?)
| Deferral | Target | Explicit? | Hides current failure? |
|---|---|---|---|
| `StatusFile` unclean-restart diagnostic | pre-release | ✅ decision; ❌ no code annotation | No |
| Caller id `None:<threadname>:<tid>` (F-CALLERID) | Task 017 | ✅ decision; ❌ no code annotation | No (`None:<tid>` works) |
| Scheduler `recursive_mutex` resolution | Task 017 | ✅ decision; ❌ no code annotation | No |
| Real structured-errno producer | pre-release | ✅ | No (consumer closed) |
| SipHash `0xff→0xee` mutation + malformed-char differential fuzz | post-019 | ✅ (golden vectors + carry probes already present) | No |
| `readBigAt` / exception-unwind | n/a (different interface / equivalent) | ✅ | No |
| `getRemoteFileMetadata==nullopt` truncation source | Task 015+ | ✅ (graceful early-out; tests don't assume a source) | No |

Note on the task-prompt's "SD8": the Round-1 registrations are SD1–SD7 and SD9; **there is no SD8**. The
caller-identity item is tracked as **F-CALLERID → Task 017**, not an SD.

---

## 7. Conclusion

### 7.1 Per-task verdicts
| Task | Verdict |
|---|---|
| 003, 004, 005, 006, 007, 008, 009, 010, 013 | **accept** |
| 011 | **reopen** (test-plane: MoveEvictionPos hole, SLRU downgrade failpoint unproven, SLRU dynamic-resize hole) |
| 012 | **reopen** (Major direct-IO/background `downloadImpl` alignment; queue-pipeline coverage in wrong binary; `THROW`/`THROW_LOGICAL` collapse false-green + forward-obligation; concurrent reset-before-complete race unproven) |
| 014 | **accept with one Major cross-cutting reopen item** (direct-IO + background download — shared with 012) |

FileSegment + FileCache-core (the 012 sub-scope) is itself faithful and would be accept; Task 012 is marked reopen
because the Metadata/`downloadImpl` direct-IO gap and the SCC-binary coverage row live in its scope.

### 7.2 Reopen list (ordered)
1. **Task 012 / Task 014 — B1 (Major):** over-align the `downloadImpl` background buffer to
   `buf->directIoAlignment_` (mirror the foreground path) **or** add an explicit fail-closed guard + deferral note;
   add a direct-IO + `backgroundDownloadThreads>0` RED test.
2. **Task 011 — B2/B3 (Major, coverage):** migrate `MoveEvictionPos`; arm
   `file_cache_slru_downgrade_fail_before_finalize`; migrate `SLRUDynamicResizeCorrectEviction`.
3. **Task 012 — B4 (concurrency evidence):** add the two-thread reset-before-complete race RED (owner 012/014).
4. **Task 012 — B5 (coverage):** include the queue-pipeline case in `velox_ch_filecache_core_scc_test`.
5. **Task 012 — `THROW`/`THROW_LOGICAL`:** record the accepted collapse + the Round-1 forward obligation to
   reintroduce a typed subtype when a distinguishing (system-table/observability) caller is ported.
6. **Documentation debt (Minor, batchable):** in-code deferral annotations for StatusFile unclean-restart,
   F-CALLERID, scheduler `recursive_mutex`, post-019 SipHash/parser; register `CleanupQueue` `F14FastSet` as an SD;
   de-duplicate `normalizePath`/`settingsEqual`; add SD4 no-ref-across-mutation and `OpenedFileCache` pool-charge tests.

### 7.3 Zero-unresolved gate
**NOT met.** One Major runtime inconsistency (B1, fail-closed but real, untested, undocumented) plus four
Major test-plane/coverage gaps (B2–B5) and a set of Minor documentation/evidence items remain unresolved. No
unregistered-and-unsafe structural deviation was found; no over-port requires deletion; all listed deferrals are
explicit and none hides a current contract failure.

### 7.4 Gate

```text
task_015_allowed = false
```

Task 015 must not start until B1 is resolved-or-explicitly-deferred (with the buffer-alignment fix or a fail-closed
guard) and the Task-011 test-plane holes (B2/B3), the concurrent reset-before-complete RED (B4), and the SCC-binary
queue-pipeline coverage (B5) are closed, with zero unresolved findings recorded.

## 8. Controller validation

```text
controller_status: reopen_proposed
task_015_allowed: false
```

The Controller independently validated the A ledger citations and all five D
findings:

- 180 parsed CH citations resolve to existing files and in-range lines.
- B1 is real: Task-014 can hand off a direct-IO source reader, while Task-012
  `downloadImpl` allocates a normally aligned background buffer and then calls
  the adapter's stricter direct-IO `set`. The exception is caught and marks the
  segment failed, so the issue is fail-closed rather than corrupting or aborting
  release builds, but background completion cannot succeed for that source.
- B2/B3 are real evidence holes: no `MoveEvictionPos`,
  `SLRUDynamicResizeCorrectEviction`, or armed
  `file_cache_slru_downgrade_fail_before_finalize` test exists.
- B4 is real: FileSegment handoff tests cover sequential state gates but not the
  two-thread extract/reset-before-complete race.
- B5 is real: Task-012 call-shape coverage exists only in
  `velox_ch_common_test`; the center-SCC binary does not own the mandatory
  queue-pipeline case.

Authoritative proposed reopen scope:

| Owner | Required correction |
|---|---|
| Task 011 | Add cursor-move, SLRU downgrade rollback/failpoint, and SLRU dynamic-resize evidence. |
| Task 012 | Align the background buffer for direct-IO source readers; add the concurrency race RED and SCC-owned queue-pipeline test. |
| Task 014 | Add the direct-IO + background-download integration test that proves the Task-012 alignment fix. |

The accepted `THROW`/`THROW_LOGICAL` collapse remains a forward obligation:
introduce a typed distinction only when a system-table/observability caller that
branches on it is ported.

No implementation was modified during this review. Tasks 003-010 and Task 013
remain accepted; Task 014 remains accepted except for the cross-cutting B1
integration evidence. Tasks 011 and 012 require corrective review before the
zero-unresolved gate can pass.
