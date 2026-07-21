# Task 012: `FileCache` Center SCC — Mandatory Compile/Link Closure

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes one
> result file under this ClickHouse checkout. Do not modify ClickHouse source
> files. Do not commit or stage either repository.

## Status and authority

```text
controller_status: reopened_by_contract_audit
environment_profile: root-oss
reopened_by: port/task/fullreview/root-oss/2/003-014-review-decisions.md (B4, B5)
reopened_by: port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md
  §2 "Task 012 — Center SCC — REOPEN", §7.2 items 3-4
```

The center SCC below (the full `## Goal` through the false-green evidence
requirement, and the original Steps) is accepted: `velox_ch_filecache_core` and
`velox_ch_filecache_core_scc_test` build green in mono and non-mono. The
Review-2 audit reopened Task 012 on two coverage/evidence gaps only: `### B4 —
concurrent reset-before-complete race evidence` and `### B5 — SCC-owned
queue-pipeline evidence` below are the binding, additive corrective scope a
fresh Worker must execute before Task 015 may start. B1 (direct-IO background
buffer alignment) is explicitly deferred to Task 015 and is not part of this
task's corrective scope. Neither B4 nor B5 reopens, weakens, or contradicts any
already-accepted decision elsewhere in this file.

## Pre-execution source-contract amendment

Task 012 must not start until corrective Tasks 003, 004, 006, 007, and 008 are
accepted and the post-Task-010 whole-port review has zero unresolved findings.

All comment-only test bodies and fixture placeholders later in this file are
non-authoritative and must not be copied. A test name or a green empty body is not
evidence.

### Mandatory executable tests

Each case below must contain real setup, execute the production path, and assert the
observable postcondition:

| Contract | Required assertion |
|---|---|
| missing key + `KeyNotFoundPolicy::THROW` | production call throws the expected Velox exception |
| releasable reserve eviction | reserve succeeds only after the real candidate is removed; cache size and segment state agree |
| empty query id | `getQueryContextHolder` returns null and creates no map entry |
| same query id | two holders share one context and one map entry |
| last holder release | destroying the final holder removes the map entry |
| doomed context destruction | final context destructor runs after the cache write lock is released; test callback can reacquire the lock |
| max download size | query LRU limit equals the configured maximum and rejects excess reservation |
| queue pipeline | real timed `tryPush(batch, 10)` and non-blocking `tryPop(batch)` paths execute |
| remote reader handoff | reader detach, buffer-end offset, available bytes, and downloader release satisfy Task 007 |
| partial-file resume | production `FileSegment` writes a first prefix, releases/recreates its writer through the real continuation path, verifies the existing physical size, appends without truncating the prefix, and keeps downloaded/physical size consistent |
| partial physical append failure | the production `FileSegment` path observes an append that physically commits a strict prefix and then throws, reads `filesystem::file_size`, enforces `downloadedSize <= physicalSize <= reservedSize`, updates downloaded size to physical size, marks the download failed, preserves the original exception, and never counts reserved-but-unwritten bytes |
| concurrent reset-before-complete race (B4) | a deterministic two-thread test proves `resetRemoteFileReader` before `completePartAndResetDownloader`/`setDownloadFinishedWithoutContinuation` is the only ordering under which no thread ever extracts a reader another thread still holds |
| SCC-owned queue pipeline (B5) | `velox_ch_filecache_core_scc_test` itself (not `velox_ch_common_test`) proves timed `tryPush(batch, 10)`, non-blocking `tryPop`, FIFO order, and `finish()` wake/drain against the real `FileCacheBoundedQueue` call shapes used by `FileCache.cpp`'s collector/loader pipelines |

For each material test, capture a behavioral RED against the pre-implementation or
intentionally broken path. Missing-header compile failure alone is insufficient.

The two `FileSegment` cases above are the integration half of the Task-007
adapter contract. Task 007 proves only the already-open `WriteFile` behavior
(append preserves existing bytes, and a partial-writing `WriteFile` exception is
propagated once while the adapter becomes canceled). Task 012 must execute the
real file-opening, downloaded/reserved accounting, filesystem-size
reconciliation, and failure-publication path. A test that performs reconciliation
inside test code or a mock instead of production `FileSegment` is false-green.

### ClickHouse gtest migration ownership

Audit `src/Interpreters/tests/gtest_filecache.cpp` before writing the Task-012
tests. Port every case whose production owner is in the center SCC, adapting the
existing fixture and temporary-directory patterns rather than copying
ClickHouse-only infrastructure:

```text
FileSegment reserve/write/complete/partial-state cases -> FileSegmentTest.cpp
metadata restore/path/cleanup cases                    -> MetadataTest.cpp
FileCache get/getOrSet/remove/eviction cases           -> FileCacheTest.cpp
query-limit holder/accounting cases                    -> QueryLimitTest.cpp
```

The audit must list every relevant CH test and its Velox destination. If a case
is not migrated, record the exact reason (unsupported excluded feature,
superseded by a stronger production-path test, or assigned to Task 014/015).
Do not carry over sleeps, comment-only bodies, or assertions that exercise only
test doubles. The resume and partial-physical-write cases are new Task-012 tests:
CH has useful FileCache fixtures but no existing test that proves both exact
contracts.

### CMake registration

Inspect the existing `velox/ch/Interpreters/FileCache/CMakeLists.txt` before editing.
Preserve an existing `add_subdirectory(tests)` block; do not add the same source and
binary directory twice. The literal duplicate block later in this file is
superseded.

### Approved infrastructure mappings (dependency pre-check)

Every external name below must resolve to exactly this mapping before
implementing the corresponding `.cpp` file. If a required Task-003/009/011
name surface (B1/B2 `ProfileEvents`/`CurrentMetrics`, `ClickHouseAssert.h`,
F14 shim headers) is missing, stop at the dependency gate instead of
substituting a guess.

| CH dependency | Approved Velox mapping | Limits / source |
|---|---|---|
| `Memory<>` (CH `src/Common/Memory.h`, used by `FileSegment`'s local write buffer) | MemoryPool-charged `BufferPtr` | Preserve size, reuse, and lifetime accounting; cross-profile decisions, Task-012 contract |
| `SCOPE_EXIT` (`base/base/defines.h`) | Folly scope guard (`folly::makeGuard` / `SCOPE_EXIT` equivalent) | Must run on both normal and exceptional exit | cross-profile decisions |
| `Stopwatch` (`src/Common/Stopwatch.h`, used by `FileCache.cpp`'s two wall-clock measurement sites) | `using Stopwatch = facebook::velox::DeltaCpuWallTimeStopWatch;` | Only the two call sites that construct and read one wall snapshot; use `elapsed().wallNanos / 1'000'000`; no `stop`/`reset`/`restart` contract is ported | cross-profile decisions |
| `callOnce`/`OnceFlag` (`src/Common/callOnce.h`, used by `FileCache::initialize`) | `std::call_once` / `std::once_flag` | Preserve CH's retry-on-exception semantics: an exception during initialization does not mark `once_flag` used up, so a subsequent call retries; preserve `init_exception` publication and rethrow | cross-profile decisions |

Reuse these mappings directly at their call sites. Do not add a private
FileCache-local reimplementation of any of them (e.g. a local wall-clock
timer duplicating `Stopwatch`, a hand-rolled once-flag instead of
`std::call_once`, or a local checked-arithmetic helper instead of the shared
`FileCacheUtils::checkedAdd` from Task 008). A private copy that
diverges even slightly from the approved mapping is an unreviewed dependency;
stop at the gate instead of introducing one.

SD1 (no-reference-escape), SD2 (`absl` flat containers -> F14, registered in
Task 011), SD3 (`KeyMetadata` ordered `std::map`), SD5 (`std::list` LRU/SLRU
queues) are approved structure deviations/preservations inherited from Tasks
009 and 011; Task 012 must not silently re-decide them. SD4 is registered
below for the `CacheMetadata` bucket array this task adds.

### Structure-deviation registrations (this task)

| CH structure | Velox replacement | Guarantee difference | Hard constraint / gate | Approval |
|---|---|---|---|---|
| SD1: `ShardedMap` callback (Task 009), reused by `CacheMetadata`'s origin dedup pool | `folly::F14FastMap`-backed shard, no map-slot reference/address/iterator may escape a callback | F14 rehash may move values | proof: every `ShardedMap`/bucket callback in `Metadata.cpp` copies out values before returning; no reference, pointer, or iterator into a shard survives past the callback | cross-profile decisions: "Keep `F14FastMap`; the user approved the no-reference-escape contract" |
| SD3: `KeyMetadata` (`Metadata.h`) | remains ordered `std::map<size_t, FileSegmentMetadataPtr>`, not F14 | n/a (no deviation; CH structure preserved) | `lower_bound` and adjacency queries in `Metadata.cpp` depend on ordering | cross-profile decisions |
| SD4: `CacheMetadata`'s 1024-bucket shard array, each bucket `folly::F14FastMap<FileCacheKey, KeyMetadataPtr, FileCacheKeyHash>` | F14 accepted only under a no-reference-across-mutation proof | F14 rehash may move values | every `CacheMetadata` bucket accessor (`Metadata.cpp`) must copy the `KeyMetadataPtr` (a `shared_ptr`, stable across rehash) out of the bucket before releasing the per-bucket guard; no raw reference/iterator into a bucket may cross a mutating call | cross-profile decisions |
| SD5: `FileSegments`/LRU/SLRU queues and cursors | remain `std::list` | n/a (no deviation; inherited from Task 011) | n/a | cross-profile decisions |

### `FileCacheErrnoException` consumer contract (typed errno, no reconcile-every-exception fallback)

Task 012 defines and consumes a typed errno exception:

```cpp
class FileCacheErrnoException : public velox::VeloxRuntimeError
{
public:
    int getErrno() const;
};
```

`FileSegment::write`'s short-write reconciliation applies **only** to this
typed exception, and **only** for `ENOSPC`/`EDQUOT`:

```text
catch FileCacheErrnoException & e:
  mark download failed
  if e.getErrno() is ENOSPC or EDQUOT:
    if downloaded_size is zero:
      remove the failed new file
    otherwise:
      read physical file size via filesystem::file_size
      require downloadedSize <= physicalSize <= reservedSize
      set downloaded_size to physical_size
  rethrow the original exception

catch any other exception (including FileCacheErrnoException with a
different errno, and any non-errno exception):
  mark download failed
  rethrow the original exception
  do NOT reconcile downloaded_size against physical size
```

This supersedes any instruction elsewhere in this task that reconciles
`downloaded_size` against on-disk size for every write exception. A single
catch-all reconciliation is a forbidden fallback: it silently masks
non-space failures (corruption, permission errors, disk-hardware errors)
behind a resize that looks like a successful partial write.

Forbidden, per the cross-profile decision:

```text
reconciling every append exception regardless of errno;
parsing errno from exception text;
implementing reconciliation inside a test double or mock WriteFile.
```

Required tests (both are material contracts from the mandatory-tests table
above):

```text
positive: a real-file-backed WriteFile double commits a strict prefix and
  throws a typed ENOSPC (or EDQUOT) FileCacheErrnoException; the production
  FileSegment path reconciles downloaded_size to the physical size and
  rethrows.
negative: the same double throws a generic (non-errno, or errno-mapped to
  a different value) exception; the production FileSegment path does NOT
  reconcile downloaded_size and simply rethrows. Assert downloaded_size is
  unchanged from its pre-exception value.
```

Producing a real, structured-errno-raising `WriteFile` in the concrete
FileCache writer remains a separate pre-release gate (not this task); Task
012's consumer logic and tests do not depend on that producer existing yet.

### Settings reload: per-field comparison, not field presence

`applySettingsIfPossible(new_config, current_config)` must compare each field
of `new_config` against the corresponding field of `current_config` by value,
and apply/reject changes per field based on that comparison. It must **not**
use whether a field is present/set in the incoming configuration payload as
the reload condition; `FileCacheConfig` fields are always structurally
present (Task 010), so a presence check would always evaluate true and
silently reapply every field on every call, including unrelated no-op
settings pushes.

### Shutdown ordering (mandatory, three phases at `FileCache` level plus queue-cancel-before-join at `CacheMetadata` level)

`FileCache::deactivateBackgroundOperations` must execute in exactly this order:

```text
1. set shutdown flag
2. join the metadata-load thread
3. deactivate both scheduler tasks (background-cleanup, free-space)
4. wait for the eviction/free-space worker pool to drain
5. shutdown metadata (CacheMetadata::shutdown)
```

`CacheMetadata::shutdown` itself must cancel its queues (`CleanupQueue`,
`DownloadQueue`) **before** joining the workers that pop from them: setting
the cancel flag and calling `notify_all` first unblocks any worker parked in
a blocking pop, so the subsequent join cannot hang. Joining first and
cancelling second is a deadlock risk and is rejected.

The `ShutdownJoinsWorkers` test in the mandatory-tests table must observe
this exact ordering (e.g. via barriers/probes proving each phase completed
before the next begins), not merely the absence of a hang.

### Deferred to Task 017 / pre-release (do not implement here)

```text
F-CALLERID: restoring "None:<threadname>:<tid>" caller identity is Task 017.
  This task's getCallerId keeps the current "None:<tid>" / "<query-id>:<tid>"
  shape from Task 006; do not add thread-name formatting here.
SD8: the scheduler recursive-mutex resolution/registration is Task 017.
  This task must not change FileCacheScheduler's locking discipline.
StatusFile unclean-restart diagnostics (three-line PID/Started/Revision text
  and closeNoThrow-before-unlink ordering) remain a pre-release gate; this
  task's StatusFile usage (acquiring the process lock during initialize) is
  unaffected and unchanged.
Real ProfileEvents/CurrentMetrics counters, real logging, and real exception
  text formatting remain Task 017. Keep every name-surface shim no-op.
```

### False-green evidence requirement

Every material test row in the mandatory-tests table above requires both a
behavioral RED (fails against the pre-implementation or an intentionally
reverted path, for the declared behavioral reason) and a false-green probe
(temporarily remove or `if (false)`-guard the specific implementation branch
the test claims to cover, and confirm the test now fails). Record both pieces
of evidence per test in the result receipt; a test with only a compile-time
RED, or with no false-green probe, is not accepted evidence for that row.

### B4 — concurrent `resetRemoteFileReader`-before-complete race evidence (owner: Task 012/014)

CH/Velox source of truth for the invariant:
`FileSegment.cpp:788-801` (`setDownloadFinishedWithoutContinuation`'s comment
and `chassert(!download_data || !download_data->remote_file_reader)`
precondition), `FileSegment.cpp:823-841`
(`completePartAndResetDownloader`, which reaches the same "reader up for
grabs" publication via `resetDownloadingStateUnlocked` → `setDownloadedUnlocked`
when a segment finishes fully), and `FileSegment.h:210,231,233`
(`completePartAndResetDownloader`, `extractRemoteFileReader`,
`resetRemoteFileReader`). Every real production caller already follows the
required order — `resetRemoteFileReader()` immediately followed by the
state-publishing call (`completePartAndResetDownloader()` or
`setDownloadFinishedWithoutContinuation()`) — at exactly two call sites in
this task's scope, `Metadata.cpp:1049-1050`
(`CacheMetadata::downloadImpl`), and six call sites in Task 014's scope,
`FileCacheInputStream.cpp:603-604,631-632,710-711,754-755,836-837,849,857`
(see Task 014's `### B4 (Task-014 half)` section for the exact pairing of
each site).
The existing single-threaded regression,
`FileSegmentTest.cpp:568-612`
(`ExtractRemoteFileReaderOnlyInTerminalOrPartialNoContinuationState`), proves
the sequential ordering; it does not prove the ordering holds under real
concurrent scheduling. This is the gap B4 closes.

Add `TEST_F(FileSegmentTest, ConcurrentExtractRacesResetBeforeComplete)` (this
file is already registered in `velox_ch_filecache_core_scc_test`; no CMake
change is needed) using **three** `std::barrier`s, following the existing
`std::barrier` pattern already used for deterministic concurrent scheduling in
`FileCacheTest.cpp:744` (`std::barrier sync(2); ... sync.arrive_and_wait();`).
The middle barrier is required to make the two production calls
(`resetRemoteFileReader()` and `completePartAndResetDownloader()`) individually
observable by thread B; without it, a single-thread reordering of the two
calls is invisible to a two-barrier test (thread B only ever observes the
state after *both* calls have already run, in whichever order):

1. thread A (the downloader) writes data, then calls `getRemoteFileReader()`
   to keep a local copy of the reader it is about to hand off (`reader_a`),
   then `arrive_and_wait()`s on barrier 1;
2. thread B `arrive_and_wait()`s on barrier 1 too, then immediately calls
   `extractRemoteFileReader()` and asserts it returns `nullptr` (the state is
   still `DOWNLOADING`, so extraction must be gated by state regardless of
   scheduling) — this proves point 4 of the Review-2 decision ("no thread can
   extract a reader still borrowed by another thread") for the pre-publication
   window;
3. thread A calls `resetRemoteFileReader()` only (the first half of the
   correct production sequence — the reader is withdrawn from `download_data`
   while thread A is still the exclusive downloader), then
   `arrive_and_wait()`s on barrier 2;
4. thread B `arrive_and_wait()`s on barrier 2, then calls
   `extractRemoteFileReader()` and asserts it returns `nullptr`: the reader
   was already withdrawn by `resetRemoteFileReader()` in step 3, so there is
   nothing left to extract, regardless of what download state gets published
   next — this is the invariant the task states: **after thread A calls
   `resetRemoteFileReader()` then `completePartAndResetDownloader()`, thread
   B's `extractRemoteFileReader()` must return `nullptr`**;
5. thread A calls `completePartAndResetDownloader()` (the second half of the
   correct sequence, publishing the terminal download state), then
   `arrive_and_wait()`s on barrier 3;
6. thread B `arrive_and_wait()`s on barrier 3, then calls
   `extractRemoteFileReader()` again and asserts it still returns `nullptr`,
   and that thread A's locally-held `reader_a` (from step 1) remains the only
   live reference to the reader (e.g. via its use count, or by asserting a
   subsequent `getRemoteFileReader()` on thread A's side is not reachable
   post-`completePartAndResetDownloader()`) — this proves point 5 of the
   Review-2 decision ("downloader state and reader ownership are coherent
   after completion": exactly one handoff (to `reader_a`, already completed
   before the race window even opened), no duplicate live reference, no
   thread left believing it still owns the reader). Do **not** assert that
   thread B can extract the same reader `reader_a` refers to: nothing
   re-stashes the reader into `download_data` after step 3's withdrawal, so it
   is never extractable again absent a separate, explicit re-stash (e.g. a
   fresh `setRemoteFileReader()` call), which this sequence does not perform.

RED: before this test exists, a regression that publishes the terminal state
before withdrawing the reader (i.e. calls `completePartAndResetDownloader()`
before `resetRemoteFileReader()`) is not proven false by any existing test.
Capture a run of this test against a temporarily reordered call sequence (see
the false-green mutation below) as the RED evidence, since the current
production code already honors the correct order.

False-green mutation: in a scratch copy of the test only (not production),
swap steps 3 and 5 so thread A calls `completePartAndResetDownloader()` first
(before barrier 2) and `resetRemoteFileReader()` second (before barrier 3),
matching what a regression at either real call site would do, rebuild, and
confirm the test now fails deterministically: after barrier 2, the download
state is already terminal (`DOWNLOADED`) but `download_data->remote_file_reader`
has not yet been reset, so thread B's step-4 `extractRemoteFileReader()` now
returns a **non-null** reader — the exact same object as thread A's locally
held `reader_a` — while thread A still believes it exclusively owns that
reader and is about to call `resetRemoteFileReader()` on it (which becomes a
silent no-op against an already-emptied field, since thread B already moved
the reader out). This is the double-ownership the reversed order exposes:
thread A's `reader_a` and thread B's newly-extracted reader alias the same
underlying object simultaneously. Confirm the test fails at the step-4
assertion (now observing non-null instead of the expected `nullptr`), not at
a compile error. Restore the correct call order and re-confirm green. Do not
mutate production code for this probe; the point is to prove the *test* can
fail when the *documented* ordering contract is violated by a caller.

No production change is expected. If the test exposes an actual defect in
`FileSegment.cpp`'s state-publication gating, fix the minimal defect and
document it in the receipt; do not add a sleep, a timing-only assertion, or a
retry loop to paper over a real race.

### B5 — SCC-owned queue-pipeline evidence (owner: Task 012)

The timed `tryPush(batch, 10)` / non-blocking `tryPop(batch)` call shapes
already have Task-003 compile-coverage in `velox_ch_common_test`
(`BasicShimsTest.cpp:265-274`, `Task012CallShapesCompile`), but that binary
does not link `velox_ch_filecache_core`, so it cannot catch a regression in how
the FileCache collector/loader pipelines actually call
`FileCacheBoundedQueue<T>`. The real call shapes to reproduce are the ones
`FileCache.cpp`'s free-space-keeping eviction pipeline and metadata-load
listing pipeline already use in production:

```text
FileCache.cpp:1626-1627  FileCacheBoundedQueue<EvictionBatchPtr>
                         pending_eviction_queue(queue_capacity),
                         pending_finalization_queue(queue_capacity)
FileCache.cpp:1691       pending_eviction_queue.pop(batch)              (blocking pop)
FileCache.cpp:1702       pending_finalization_queue.push(std::move(batch))
FileCache.cpp:1643       pending_finalization_queue.tryPop(batch)       (non-blocking, "finalize_removed(false)")
FileCache.cpp:1643       pending_finalization_queue.pop(batch)          (blocking, "finalize_removed(true)")
FileCache.cpp:1799       pending_eviction_queue.tryPush(batch, push_timeout_ms)  (timed)
FileCache.cpp:1720,1831,1833  .finish()
FileCache.cpp:2228       FileCacheBoundedQueue<std::pair<fs::path, OriginInfo>>
                         key_dirs_queue(...)
FileCache.cpp:2313       key_dirs_queue.tryPush({key_dir_path, origin})
FileCache.cpp:2250       key_dirs_queue.pop(item)
FileCache.cpp:2241,2328  key_dirs_queue.finish()
```

Add a queue-pipeline case to `FileCacheTest.cpp` (already registered in
`velox_ch_filecache_core_scc_test`; no CMake change needed) using
`facebook::velox::ch::FileCacheBoundedQueue` (the real production type
included transitively via `FileCache.cpp`'s own header, not a private
redeclaration) with the exact call shapes above:

1. construct a `FileCacheBoundedQueue<int>` (or another small value type) with
   a small bounded capacity;
2. execute a timed `tryPush(value, 10)` call matching the `tryPush(batch,
   push_timeout_ms)` shape at `FileCache.cpp:1799`, and assert it succeeds;
3. execute a non-blocking `tryPop(value)` call matching the
   `finalize_removed(false)` shape at `FileCache.cpp:1643`, and assert the
   popped value equals what was pushed (FIFO: push two values, pop twice,
   assert order);
4. from a second thread, block on a blocking `pop(value)` (matching
   `FileCache.cpp:1691`/`FileCache.cpp:1643`'s blocking branch) on an empty
   queue, then call `finish()` from the first thread, and assert the blocked
   `pop` wakes and returns `false` (drained-and-finished), proving `finish()`
   wakes/drains the pipeline exactly as the collector/remover shutdown path
   requires.

RED: before this test exists, `velox_ch_filecache_core_scc_test` has zero
coverage of `FileCacheBoundedQueue`; a regression in the timed-`tryPush`,
non-blocking-`tryPop`, FIFO, or `finish()`-wake behavior reachable only from
this binary would pass CI. Capture the pre-implementation state (grep
confirming zero `FileCacheBoundedQueue` references in
`velox/ch/Interpreters/FileCache/tests/`) as the RED evidence.

False-green mutation: temporarily swap the non-blocking `tryPop` call in step 3
for a blocking `pop` (a call-shape mutation, matching the Review-2 requirement
"a false-green mutation of one call shape"), rebuild, and confirm the test now
hangs or fails (a blocking `pop` on a queue that may still be empty at that
point in the test does not return the same way `tryPop` does) — demonstrating
the test actually depends on the specific call shape it claims to prove.
Restore the non-blocking call and re-confirm green.

No duplicate queue implementation is permitted: this test must include
`velox/ch/Common/FileCacheBoundedQueue.h` and use the same
`facebook::velox::ch::FileCacheBoundedQueue` template Task 003 defined and
`FileCache.cpp` already uses; it must not declare a local queue type or copy
the queue's internals into the test file.

### B4/B5 mono/non-mono and accumulated gates

Both B4 and B5 land inside files `velox_ch_filecache_core_scc_test` already
compiles. Closing this corrective work requires re-proving the same build
closure Task 012 already established (`port/task/result/012-filecache-core-scc-result.md`,
101/101 focused in both mono and non-mono, 11/11 accumulated mono CTests):

```text
1. Build and run velox_ch_filecache_core_scc_test in the existing mono
   configuration (<velox_build_dir>), full suite green.
2. Build and run velox_ch_filecache_core_scc_test in the existing non-mono
   configuration (VELOX_MONO_LIBRARY=OFF), full suite green.
3. Re-run the accumulated mono CTest set and confirm the focused and
   accumulated counts each increase by exactly the number of new tests added
   here (one for B4, one for B5), with zero regressions elsewhere.
```

Record all required logs (mono focused, mono accumulated, non-mono focused,
plus the RED and false-green mutation logs for both B4 and B5) in the
corrective receipt appended to
`port/task/result/012-filecache-core-scc-result.md`.

### B4/B5 corrective stop conditions

Stop as `blocked` and escalate instead of improvising if:

```text
velox_ch_filecache_core_scc_test does not build green in mono/non-mono before
  this corrective work starts (that would be an unrelated regression, not a
  B4/B5 defect; do not fix it silently under this task's scope).
the two-thread B4 test cannot be made deterministic with std::barrier alone
  (e.g. it requires a third barrier, a different Velox scheduling primitive,
  or exposes a genuine lock-ordering hazard in FileSegment.cpp) — stop and
  report the exact hazard instead of adding a sleep or a retry loop.
a required assertion can only pass by weakening it, adding a sleep, or
  skipping/disabling the test.
```

## Goal

Produce a single compilable and linkable batch that closes the center strongly
connected component (SCC) of the `FileCache` implementation:

```text
IFileCachePriority/LRU/SLRU/Split/EvictionCandidates from Task 011
FileSegmentInfo.h
FileSegment.h / FileSegment.cpp
Metadata.h / Metadata.cpp       (owns CleanupQueue and DownloadQueue)
FileCache.h / FileCache.cpp
QueryLimit.h / QueryLimit.cpp
```

The deliverable is a compiled and tested `velox_ch_filecache_core` library and
a `velox_ch_filecache_core_scc_test` executable.

## Why the Center SCC Cannot Be Split

The priority and center implementation files form a genuine strongly connected component and
cannot be split into independently linkable units:

```text
FileSegment.cpp
  -> FileCache::tryReserve / tryIncreasePriority / config getters
  -> LockedKey::removeFileSegment / removeAllFileSegments
  -> KeyMetadata path / origin APIs

Metadata.cpp
  -> FileSegment state / range / write / reserve / detach / getInfo
  -> FileCache::getInternalOrigin (static internal origin factory)

FileCache.cpp
  -> CacheMetadata (owns download/cleanup workers)
  -> FileSegment (creates and owns through metadata)
  -> FileCacheQueryLimit (optional construction)

QueryLimit.cpp
  -> FileCache::lockCache (CachePriorityGuard::WriteLock)
  -> KeyMetadata (for path/access in add record)

priority/eviction .cpp files
  -> FileCache definitions
  -> Metadata/KeyMetadata/FileSegment
```

Creating a fake `FileCache` stub with only the symbols `FileSegment.cpp` needs
would require re-implementing the same eviction/reserve algorithm that
`FileCache.cpp` provides. The result would be two conflicting implementations
linked into the same test binary. The only sound option is to compile all `.cpp`
files together and link once.

Headers (`FileSegment.h`, `Metadata.h`, `FileCache.h`, `QueryLimit.h`) can be
reviewed and added one at a time in earlier substeps because they carry no
link-time symbols. The single compile/link closure is required only for the
`.cpp` files.

## Starting Point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected predecessors:
  Task 003: basic common shims (ClickHouseAliases, FileCacheBoundedQueue, etc.)
  Task 004: StatusFile, Guards.h
  Task 005: FileCacheWorkerPool / FileCacheThreadPool
  Task 006: FileCacheScheduler, FileCacheQueryIdScope
  Task 007: ReadBufferFromVeloxReadFile, WriteBufferFromVeloxWriteFile
  Task 008: SipHash128, key/origin/segment types, forward files, utils
  Task 009: ShardedMap
  Task 010: FileCacheSettings/FileCacheConfig
  Task 011: priority/eviction source migration, atomic Part A (not compiled)
```

Do not require a clean worktree. Stop if the branch is not `filecache`.

## Design References

Read before editing:

```text
port/task/ENVIRONMENT.md
port/01-filecache-port-order-design.md
port/2-file-cache/07-filecache-priority-eviction-design.md
port/2-file-cache/08-filecache-metadata-files-design.md
port/2-file-cache/09-filecache-file-segment-design.md
port/2-file-cache/10-filecache-core-files-design.md
port/2-file-cache/11-filecache-query-limit-design.md
port/1-dependencies/01-filecache-infra-mapping.md
port/1-dependencies/02-filecache-basic-shims-design.md
port/1-dependencies/04-filecache-thread-pool-design.md
port/1-dependencies/06-filecache-caller-token-design.md
```

Use ClickHouse source only as behavioral reference:

```text
src/Interpreters/FileCache/FileSegmentInfo.h
src/Interpreters/FileCache/CacheUsage.h
src/Interpreters/FileCache/IFileCachePriority.h / .cpp
src/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
src/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
src/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
src/Interpreters/FileCache/EvictionCandidates.h / .cpp
src/Interpreters/FileCache/FileSegment.h / .cpp
src/Interpreters/FileCache/Metadata.h / .cpp
src/Interpreters/FileCache/FileCache.h / .cpp
src/Interpreters/FileCache/QueryLimit.h / .cpp
```

## File Scope

Modify:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/CMakeLists.txt
<velox_repo>/velox/ch/Interpreters/FileCache/CacheUsage.h
<velox_repo>/velox/ch/Interpreters/FileCache/IFileCachePriority.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/EvictionCandidates.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/FileSegmentInfo.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileSegment.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileSegment.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/Metadata.h
<velox_repo>/velox/ch/Interpreters/FileCache/Metadata.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/FileCache.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCache.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/QueryLimit.h
<velox_repo>/velox/ch/Interpreters/FileCache/QueryLimit.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp
<clickhouse_repo>/port/task/result/012-filecache-core-scc-result.md
```

Every new Velox C++ and CMake file must begin with the Apache 2.0 license
header. Use `/* ... */` for C++ and `#` for CMake, matching the repository style.

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected: branch `filecache`, HEAD is a descendant of `bf379041f`.
Record pre-existing dirty files in the result file. Stop if the branch differs.

- [ ] **Step 2: Create the test CMakeLists**

Append the following target to the existing
`velox/ch/Interpreters/FileCache/tests/CMakeLists.txt`; preserve every test
target added by Tasks 008-010:

```cmake
add_executable(
  velox_ch_filecache_core_scc_test
  PriorityEvictionTest.cpp
  FileSegmentInfoTest.cpp
  FileSegmentTest.cpp
  MetadataTest.cpp
  FileCacheTest.cpp
  QueryLimitTest.cpp
)
add_test(velox_ch_filecache_core_scc_test velox_ch_filecache_core_scc_test)

target_link_libraries(
  velox_ch_filecache_core_scc_test
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

- [ ] **Step 3: Write priority/eviction and `FileSegmentInfo` tests (red)**

Create `PriorityEvictionTest.cpp` against the real Task-011 headers and the
real core types that this task will add. Do not declare local
`KeyMetadata`/`FileCache` substitutes. Cover:

```text
LRU add/remove/evict and stable iterator
zero-size entry counts neither bytes nor elements
reserve/background cursors advance independently
total-space cleanup uses min(requested,current)
SLRU second access promotes probationary to protected
addForRestore restores original queue
Split routes General/Data to Data and System to System
Split partitions both bytes and elements
failed second resize rolls first resize back
EvictionInfo keeps separate QueueID entries and usage pins
EvictionCandidates removeQueueEntries/original queue restore/failure accounting
```

Use a real temporary `FileCache` fixture once the SCC implementation exists;
the file is expected to fail at the red-build step before those definitions are
added.

Create `velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp`:

```cpp
#include "velox/ch/Interpreters/FileCache/FileSegmentInfo.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <type_traits>

namespace facebook::velox::ch
{
namespace
{

TEST(FileSegmentInfoTest, StateEnumLayout)
{
    // Order and underlying values preserved from ClickHouse.
    static_assert(static_cast<uint8_t>(FileSegmentState::DOWNLOADED) == 0);
    static_assert(static_cast<uint8_t>(FileSegmentState::EMPTY) == 1);
    static_assert(static_cast<uint8_t>(FileSegmentState::DOWNLOADING) == 2);
    static_assert(
        static_cast<uint8_t>(
            FileSegmentState::PARTIALLY_DOWNLOADED_NO_CONTINUATION)
        == 3);
    static_assert(
        static_cast<uint8_t>(FileSegmentState::PARTIALLY_DOWNLOADED) == 4);
    static_assert(static_cast<uint8_t>(FileSegmentState::DETACHED) == 5);
}

TEST(FileSegmentInfoTest, KindEnumLayout)
{
    static_assert(static_cast<uint8_t>(FileSegmentKind::Regular) == 0);
    static_assert(static_cast<uint8_t>(FileSegmentKind::Ephemeral) == 1);
}

TEST(FileSegmentInfoTest, InfoSnapshotCompiles)
{
    FileSegmentInfo info;
    (void)info.key;
    (void)info.offset;
    (void)info.path;
    (void)info.range_left;
    (void)info.range_right;
    (void)info.kind;
    (void)info.state;
    (void)info.size;
    (void)info.downloaded_size;
    (void)info.download_finished_time;
    (void)info.cache_hits;
    (void)info.references;
    (void)info.is_unbound;
    (void)info.queue_entry_type;
    (void)info.origin;
}

TEST(FileSegmentInfoTest, KindToString)
{
    EXPECT_EQ(toString(FileSegmentKind::Regular), "Regular");
    EXPECT_EQ(toString(FileSegmentKind::Ephemeral), "Ephemeral");
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 4: Write `FileSegmentTest.cpp` (red)**

Create `velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp`:

```cpp
#include "velox/ch/Interpreters/FileCache/FileSegment.h"
#include "velox/ch/Interpreters/FileCache/FileCache.h"
#include "velox/ch/Common/FileCacheQueryIdScope.h"
#include <folly/system/ThreadId.h>
#include <gtest/gtest.h>

namespace facebook::velox::ch
{
namespace
{

// Range is inclusive [left, right].
TEST(RangeTest, SizeIsRightMinusLeftPlusOne)
{
    FileSegment::Range r{10, 19};
    EXPECT_EQ(r.size(), 10ULL);
}

TEST(RangeTest, ContainsPoint)
{
    FileSegment::Range r{5, 10};
    EXPECT_TRUE(r.contains(5));
    EXPECT_TRUE(r.contains(10));
    EXPECT_FALSE(r.contains(4));
    EXPECT_FALSE(r.contains(11));
}

TEST(RangeTest, ContainsRange)
{
    FileSegment::Range outer{0, 100};
    FileSegment::Range inner{10, 50};
    FileSegment::Range overlap{80, 110};
    EXPECT_TRUE(outer.contains(inner));
    EXPECT_FALSE(outer.contains(overlap));
}

TEST(RangeTest, StrictOrderingNonOverlapping)
{
    FileSegment::Range a{0, 9};
    FileSegment::Range b{10, 19};
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(CreateFileSegmentSettingsTest, RegularIsBounded)
{
    CreateFileSegmentSettings s;
    EXPECT_EQ(s.kind, FileSegmentKind::Regular);
    EXPECT_FALSE(s.unbounded);
}

TEST(CreateFileSegmentSettingsTest, EphemeralIsUnbounded)
{
    CreateFileSegmentSettings s{FileSegmentKind::Ephemeral};
    EXPECT_EQ(s.kind, FileSegmentKind::Ephemeral);
    EXPECT_TRUE(s.unbounded);
}

TEST(CallerIdTest, SameScopeStableId)
{
    FileCacheQueryIdScope scope("q1");
    auto id1 = FileSegment::getCallerId();
    auto id2 = FileSegment::getCallerId();
    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, "None:" + std::to_string(folly::getOSThreadID()));
}

TEST(CallerIdTest, NoScopeBackgroundId)
{
    // Without a query scope, caller is "None:<tid>".
    auto id = FileSegment::getCallerId();
    EXPECT_TRUE(id.rfind("None:", 0) == 0);
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 5: Write `MetadataTest.cpp` (red)**

Create `velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp`:

```cpp
#include "velox/ch/Interpreters/FileCache/Metadata.h"
#include "velox/ch/Interpreters/FileCache/FileCache.h"
#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/common/testutil/TempDirectoryPath.h"
#include <gtest/gtest.h>
#include <filesystem>

namespace facebook::velox::ch
{
namespace
{

using common::testutil::TempDirectoryPath;
namespace fs = std::filesystem;

TEST(PathLayoutTest, RegularDownloadingFilename)
{
    // Downloading segment: filename is just the offset decimal string.
    EXPECT_EQ(
        CacheMetadata::getFileNameForFileSegment(
            100, FileSegmentKind::Regular, std::nullopt),
        "100");
}

TEST(PathLayoutTest, RegularDownloadedFilename)
{
    // Downloaded segment: "<offset>_<size>".
    EXPECT_EQ(
        CacheMetadata::getFileNameForFileSegment(
            100, FileSegmentKind::Regular, 512),
        "100_512");
}

TEST(PathLayoutTest, EphemeralFilename)
{
    // Ephemeral segment: "<offset>_temporary".
    EXPECT_EQ(
        CacheMetadata::getFileNameForFileSegment(
            0, FileSegmentKind::Ephemeral, std::nullopt),
        "0_temporary");
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 6: Write `FileCacheTest.cpp` (red)**

Create `velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp`:

Build one real fixture from the final Task 012 production constructors and injected
runtime services. Do not write an abbreviated constructor or null/fake dependency.
Implement executable tests for:

```text
InitializeOnce
GetDoesNotCreateMetadata
GetOrSetCreatesHoles
TryReserveEvictsReleasable
ShutdownJoinsWorkers
SecondProcessStatusLockFails
InternalOriginAccessAllKeys
CommonOriginIsInjectedUserId
```

Each test must assert the production state after the call. The shutdown case must
use worker/timer probes proving completion rather than relying only on absence of a
hang.

- [ ] **Step 7: Write `QueryLimitTest.cpp` (red)**

Create `velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp`:

Use the real `FileCache` lock and query-limit APIs. Implement every query-limit case
listed in the mandatory executable-test table at the top of this task. The doomed
context test must use a destructor callback that reacquires the cache write lock,
which proves destruction occurred after lock release without using sleep.

- [ ] **Step 8: Verify the red build**

Configure:

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`. For `home-chang`, also add
`-DVELOX_BUILD_TESTING=ON` (already present in the `root-oss` effective
configuration). Redirect output to `<velox_build_dir>/configure_task_012_scc.log`.

Then attempt the build, expecting failure:

```bash
if <ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_core_scc_test \
  > <velox_build_dir>/build_task_012_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected: configure succeeds, build fails because SCC headers and `.cpp` files
do not exist. If configure fails for another reason, stop and report instead of
continuing.

- [ ] **Step 9: Implement `FileSegmentInfo.h`**

`FileSegmentInfo.h` is a pure leaf with no mutual dependencies. Implement the
exact enum layouts verified by the tests:

```cpp
#pragma once

#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h"
#include "velox/ch/Interpreters/FileCache/IFileCachePriority.h"

#include <cstdint>
#include <chrono>
#include <string>

namespace facebook::velox::ch
{

enum class FileSegmentState : uint8_t
{
    DOWNLOADED = 0,
    EMPTY = 1,
    DOWNLOADING = 2,
    PARTIALLY_DOWNLOADED_NO_CONTINUATION = 3,
    PARTIALLY_DOWNLOADED = 4,
    DETACHED = 5,
};

enum class FileSegmentKind : uint8_t
{
    Regular = 0,
    Ephemeral = 1,
};

// Defined in FileSegment.cpp (no separate FileSegmentInfo.cpp created).
std::string toString(FileSegmentKind kind);

struct FileSegmentInfo
{
    FileCacheKey key;
    uint64_t offset = 0;
    std::string path;
    uint64_t range_left = 0;
    uint64_t range_right = 0;
    FileSegmentKind kind = FileSegmentKind::Regular;
    FileSegmentState state = FileSegmentState::EMPTY;
    uint64_t size = 0;
    uint64_t downloaded_size = 0;
    std::chrono::time_point<std::chrono::steady_clock> download_finished_time{};
    uint64_t cache_hits = 0;
    uint32_t references = 0;
    bool is_unbound = false;
    IFileCachePriority::QueueEntryType queue_entry_type
        = IFileCachePriority::QueueEntryType::None;
    FileCacheOriginInfo origin;
};

} // namespace facebook::velox::ch
```

- [ ] **Step 10: Implement `FileSegment.h`**

The full `FileSegment.h` declares:

- `CreateFileSegmentSettings` (kind, unbounded)
- `Range` struct: inclusive `[left, right]`, `size`, `contains`, `operator<`
- `FileSegment` class with all API groups:
  - Constant state: `range`, `key`, `offset`, `kind`, `path`
  - Any-holder: `getOrSetDownloader`, `isDownloader`, `wait`,
    `getDownloadedSize`, `getReservedSize`, `getCurrentWriteOffset`,
    `detach`, `complete`, `increasePriority`
  - Cache-internal: `segmentLock`, `priorityIterator`, `keyMetadata`
  - Downloader-only: `reserve`, `write`, `getRemoteFileReader`,
    `setRemoteFileReader`, `resetRemoteFileReader`,
    `extractRemoteFileReader`, `getLocalCacheWriter`,
    `completePartAndResetDownloader`, `resetDownloader`
  - Static: `getCallerId`
- `FileSegmentsHolder` RAII type
- Required type aliases:
  - `RemoteFileReaderPtr = std::shared_ptr<ReadBufferFromVeloxReadFile>`
  - `LocalCacheWriterPtr = std::shared_ptr<WriteBufferFromVeloxWriteFile>`
  - `FileSegmentsHolderPtr = std::shared_ptr<FileSegmentsHolder>`

Key invariants to encode in the header:

```text
Range::size() == right - left + 1
FileSegment is non-copyable and non-movable
FileSegmentsHolder is move-only
is_unbound and background_download_enabled are immutable after construction
size_in_filename transitions false -> true only (atomic)
terminal states DOWNLOADED and DETACHED are published last after all fields final
```

`wait` signature injects a cancellation token:

```cpp
FileSegmentState wait(
    size_t offset,
    const folly::CancellationToken & cancellation_token);
```

- [ ] **Step 11: Implement `Metadata.h`**

`Metadata.h` declares the following types. Do not split into separate files.

**`FileSegmentMetadata`**

```cpp
struct FileSegmentMetadata
{
    explicit FileSegmentMetadata(std::shared_ptr<FileSegment> file_segment_);

    bool releasable() const
    {
        return file_segment.use_count() == 1;
    }

    size_t size() const;

    const std::shared_ptr<FileSegment> file_segment;
    bool removed = false;
};
using FileSegmentMetadataPtr = std::shared_ptr<FileSegmentMetadata>;
```

**`KeyMetadata`** inherits `std::map<size_t, FileSegmentMetadataPtr>` (ordered,
not F14; lower_bound and adjacency queries depend on ordering).

Members:

```text
const FileCacheKey key
const std::shared_ptr<FileCacheOriginInfo> origin  (shared / deduped)
KeyState: ACTIVE / REMOVING / REMOVED
KeyGuard for external callers
std::atomic<bool> created_base_directory
```

Methods: `lock`, `tryLock`, `lockNoStateCheck`, `createBaseDirectory`,
`getPath`, `getFileSegmentPath` overloads, `checkAccess`, `assertAccess`,
download/cleanup queue submission.

**`CacheMetadata`**

Must declare:
- 1024-bucket shard array, each bucket is
  `folly::F14FastMap<FileCacheKey, KeyMetadataPtr, FileCacheKeyHash>` wrapped
  with a per-bucket `CacheMetadataGuard`
- origin dedup pool (ShardedMap of `FileCacheOriginInfo`)
- `CleanupQueue` (handwritten internal type, NOT `FileCacheBoundedQueue`):
  uses `folly::F14FastSet` for deduplication, mutex+cv for blocking pop,
  `cancel` flag and `notify_all` on cancel
- `DownloadQueue` (handwritten internal type, NOT `FileCacheBoundedQueue`):
  uses `std::queue` of `DownloadInfo`, bounded capacity, mutex+cv, cancel flag
- download worker vector: `std::vector<std::shared_ptr<DownloadThread>>`
- cleanup worker thread
- client-access callback

**`DownloadInfo`** struct:

```cpp
struct DownloadInfo
{
    FileCacheKey key;
    uint64_t offset = 0;
    std::weak_ptr<FileSegment> segment; // must not be removed; see design
};
```

**`LockedKey`**

Member order (must be preserved for correct destruction sequence):

```cpp
class LockedKey
{
public:
    // ...
private:
    // Declaration order determines destruction order.
    // lock must be destroyed BEFORE key_metadata drops its shared reference.
    const std::shared_ptr<KeyMetadata> key_metadata;
    KeyGuard::Lock lock;
};
```

Do not verify this with `offsetof(LockedKey, ...)`: `LockedKey` is not a
standard-layout type (it holds a `shared_ptr` member and a non-trivial lock
member), so `offsetof` on it is only conditionally supported and typically
raises `-Winvalid-offsetof` under a `-Werror` build. Verify it instead with
the structural check (Step 18) confirming `key_metadata` is declared
textually before `lock` in the class body of `Metadata.h`, including its
false-green mutation probe (temporarily swap the two declaration lines,
confirm the check fails, restore, confirm it passes again). A comment-only
gtest sketch is not evidence and must not be added in its place.

`LockedKey` provides: map iteration/lower_bound, `get`/`tryGet` by offset,
`removeFileSegment` variants, `removeAllReleasableSegments`,
`submitToDownloadQueue`, range intersection, empty-key delayed cleanup,
metadata/file sync.

**`CacheMetadata::Iterator` and `BatchedIterator`**

Declare both as nested classes with distinct locking contracts:

```text
Iterator:      one segment per next; not thread-safe
BatchedIterator: one non-empty bucket batch per nextBatch;
                sequential calls may run on different threads (no concurrent calls)
```

- [ ] **Step 12: Implement `QueryLimit.h`**

The `QueryLimit.h` header declares:

**`FileCacheQueryLimit`**

```cpp
class FileCacheQueryLimit
{
public:
    struct QueryContext;
    using QueryContextPtr = std::shared_ptr<QueryContext>;

    struct QueryContextHolder
    {
        QueryContextHolder() = default;
        QueryContextHolder(
            std::string query_id_,
            FileCache * cache_,
            FileCacheQueryLimit * limit_,
            QueryContextPtr ctx_);

        QueryContextHolder(const QueryContextHolder &) = delete;
        QueryContextHolder & operator=(const QueryContextHolder &) = delete;
        QueryContextHolder(QueryContextHolder &&) = default;
        QueryContextHolder & operator=(QueryContextHolder &&) = default;

        ~QueryContextHolder();

        QueryContextPtr context;

    private:
        std::string query_id;
        FileCache * cache = nullptr;
        FileCacheQueryLimit * limit = nullptr;
    };

    QueryContextPtr tryGetQueryContext(
        const CacheStateGuard::Lock & state_lock);

    QueryContextPtr getOrSetQueryContext(
        const std::string & query_id,
        const FileCacheReadOptions & options,
        const CachePriorityGuard::WriteLock & write_lock);

    void removeQueryContext(
        const std::string & query_id,
        QueryContextPtr & doomed,
        const CachePriorityGuard::WriteLock & write_lock);

private:
    folly::F14FastMap<std::string, QueryContextPtr> query_map;
    std::mutex query_map_mutex;
};

using FileCacheQueryLimitPtr = std::unique_ptr<FileCacheQueryLimit>;
```

**`QueryContext`**

```cpp
struct FileCacheQueryLimit::QueryContext
{
    explicit QueryContext(
        size_t query_cache_size,
        bool recache_on_limit_exceeded_);

    QueryContext(const QueryContext &) = delete;
    QueryContext & operator=(const QueryContext &) = delete;

    IFileCachePriority::IteratorPtr tryGet(
        const FileCacheKey & key,
        size_t offset,
        const CachePriorityGuard::WriteLock &);

    void add(
        KeyMetadata & key_metadata,
        size_t offset,
        size_t size,
        const CachePriorityGuard::WriteLock &);

    void remove(
        const FileCacheKey & key,
        size_t offset,
        const CachePriorityGuard::WriteLock &);

    bool recache_on_limit_exceeded;
    LRUFileCachePriority priority;

private:
    folly::F14FastMap<
        FileCacheKeyAndOffset,
        IFileCachePriority::IteratorPtr,
        FileCacheKeyAndOffsetHash>
        records;
};
```

- [ ] **Step 13: Implement `FileCache.h`**

`FileCache.h` is the public API apex of the SCC. Key declarations:

**`FileCacheReserveStat`** — exact field list:

```cpp
struct FileCacheReserveStat
{
    struct Stat
    {
        size_t releasable_size = 0;
        size_t releasable_count = 0;
        size_t non_releasable_size = 0;
        size_t non_releasable_count = 0;
    };

    // indexed by static_cast<uint8_t>(FileSegmentKind)
    std::array<Stat, 2> stat_by_kind{};
    Stat total;
    size_t evicting = 0;
    size_t moving = 0;
    size_t invalidated = 0;
    size_t candidates_iterated = 0;
    size_t clients_iterated = 0;
};
```

**`FileCache` public API groups** (all must be declared; no TBD):

```text
lifecycle:
  void initialize()
  bool isInitialized() const
  void deactivateBackgroundOperations()

origin/path:
  FileCacheOriginInfo getCommonOrigin() const
  static FileCacheOriginInfo getInternalOrigin()
  FileCacheOriginInfo getCommonOriginWithSegmentKeyType(FileSegmentKeyType) const
  std::string getFileSegmentPath(const FileCacheKey &, uint64_t offset,
      FileSegmentKind, const KeyMetadata &) const
  std::string getKeyPath(const FileCacheKey &, const KeyMetadata &) const

lookup/create:
  FileSegmentsHolder getOrSet(const FileCacheKey &, FileSegment::Range,
      const FileCacheOriginInfo &, const FileCacheReadOptions &)
  FileSegmentsHolder get(const FileCacheKey &, FileSegment::Range,
      const FileCacheOriginInfo &)
  FileSegmentsHolder getDownloadedContiguousOrEmpty(
      const FileCacheKey &, FileSegment::Range,
      const FileCacheOriginInfo &)
  FileSegmentsHolder set(const FileCacheKey &, uint64_t offset, uint64_t size,
      const FileCacheOriginInfo &, const CreateFileSegmentSettings &)
  FileSegmentsHolder trySet(const FileCacheKey &, uint64_t offset, uint64_t size,
      const FileCacheOriginInfo &, const CreateFileSegmentSettings &)

reservation/priority:
  bool tryReserve(FileSegment &, size_t size, const FileCacheReadOptions &)
  void tryIncreasePriority(FileSegment &)
  CachePriorityGuard::WriteLock lockCache()

remove/admin:
  void removeFileSegment(const FileCacheKey &, uint64_t offset,
      const FileCacheOriginInfo &)
  void removeKey(const FileCacheKey &, const FileCacheOriginInfo &)
  void removePathIfExists(const std::string & path, const FileCacheOriginInfo &)
  void removeAllReleasable(const FileCacheOriginInfo &)
  void sync()
  CacheMetadata::Iterator getCacheIterator()
  std::vector<FileSegmentInfo> getFileSegmentInfos(
      const FileCacheKey &, const FileCacheOriginInfo &)
  std::string dumpQueue() const
  FileCacheUsage getUsage() const

settings/stats:
  void applySettingsIfPossible(const FileCacheConfig & new_config,
      FileCacheConfig & current_config)
  size_t capacity() const
  size_t getUsedCacheSize() const
  size_t getFileSegmentsNum() const

query limit:
  FileCache::QueryContextHolderPtr getQueryContextHolder(
      const std::string & query_id, const FileCacheReadOptions &)
```

`getQueryContextHolder` returns `std::unique_ptr<FileCacheQueryLimit::QueryContextHolder>`.

**Member order** for correct destruction sequence:

```text
main_priority   declared before metadata
  -> metadata destroyed first, priority iterators remain valid
StatusFile      held for full FileCache lifetime
metadata        CacheMetadata
query_limit     optional FileCacheQueryLimitPtr
```

- [ ] **Step 14: Implement the `.cpp` files**

First finish every Task-011 priority/eviction `.cpp` against the real
`FileCache`/`Metadata`/`FileSegment` types. Remove no public method and add no
compatibility stub. Then implement the center-SCC `.cpp` files below.

All four `.cpp` files must be added before attempting the final build. There is
intentionally no intermediate link step; partial `.cpp` presence is not
expected to link.

### `FileSegment.cpp`

Implement the exact state machine and invariants from
`port/2-file-cache/09-filecache-file-segment-design.md`. Key points:

- `toString(FileSegmentKind)` is defined here (not in a separate file).
- `getCallerId` queries `FileCacheQueryIdScope::currentQueryId()` and
  `folly::getOSThreadID()`:
  - non-empty query id → `"<query-id>:<tid>"`
  - empty query id → `"None:<tid>"`
- `getOrSetDownloader` election and state transition happen under `segment_guard`.
- `wait` slices in one-second increments, checks the cancellation token each
  slice, and returns after 60 seconds without blocking indefinitely.
- `reserve`: calls `cache_->tryReserve(*this, size, options)`.
- `write` short-write path: catch `FileCacheErrnoException`; reconcile
  `downloaded_size` against the actual on-disk file size (via
  `filesystem::file_size`) **only when** `getErrno()` is `ENOSPC` or
  `EDQUOT`, per the "`FileCacheErrnoException` consumer contract" above;
  never leaves `downloaded_size > actual on-disk size` on that path. Any
  other exception (a different errno, or a non-`FileCacheErrnoException`)
  marks the download failed and rethrows unchanged, without touching
  `downloaded_size`. Do not reconcile on every write exception.
- Final rename `<offset>` → `<offset>_<size>` precedes publishing `DOWNLOADED`.
  Rename failure keeps legacy `<offset>` path; `size_in_filename` stays false.
- `FileSegmentsHolder::reset` catches and logs (no-op shim) per-segment
  completion exceptions; continues cleaning remaining segments.
- `detach` sequence: clear downloader → publish `DETACHED` → reset
  `key_metadata` weak_ptr → reset priority iterator → cancel writer →
  release `DownloadState`.

### `Metadata.cpp`

Implement all eight sections from
`port/2-file-cache/08-filecache-metadata-files-design.md`:

1. Metadata wrappers and origin pool
2. Key locking and path layout
3. Bucket lookup and key state recovery (four `KeyNotFoundPolicy` behaviors)
4. `IteratorImpl` / `BatchedIteratorImpl`
5. Key removal and directory cleanup
6. `CleanupQueue` with F14FastSet deduplication, cancel, notify_all
7. `DownloadQueue` and workers: bounded std::queue, weak_ptr identity check,
   per-worker `stopFlag` under queue mutex, resize join sequence
8. Worker shutdown and resize ordering

Path layout invariant:

```text
Regular downloading:  <offset>
Regular downloaded:   <offset>_<size>
Ephemeral:           <offset>_temporary
```

Key path invariant:

```text
without per-user:  <base>/<segment-prefix>/<first-3-key-chars>/<full-key>
with per-user:     <base>/<segment-prefix>/<user-id>.<weight>/<first-3-key-chars>/<full-key>
```

`REMOVING` key reactivation by `CREATE_EMPTY`: cancel delayed removal, restore
`ACTIVE`, return same locked key.

`DownloadInfo` must carry `weak_ptr<FileSegment>` in addition to key+offset.
Using key+offset alone for identity would accept a new segment created at the
same offset after the original was deleted.

Replace `OpenedFileCache::instance().remove(...)` with the manager-owned
opened-file cache invalidation reference/callback injected at construction.

### `QueryLimit.cpp`

Implement `port/2-file-cache/11-filecache-query-limit-design.md`:

- `tryGetQueryContext`: acquires `query_map_mutex`, looks up current query id
  from `FileCacheQueryIdScope::currentQueryId()`, returns shared pointer.
- `getOrSetQueryContext`: creates or reuses `QueryContext` under
  `query_map_mutex`.
- `removeQueryContext`: moves context out of map under mutex; caller destroys
  the doomed context **after** releasing the `CachePriorityGuard::WriteLock`.
- `QueryContextHolder::~QueryContextHolder`: acquires cache write lock, calls
  `removeQueryContext`, releases lock, then lets doomed context go out of scope.
  Must not throw.

### `FileCache.cpp`

Implement `port/2-file-cache/10-filecache-core-files-design.md`:

- `initialize`: uses `std::call_once` (retry-on-exception semantics, matching
  CH `callOnce`); acquires `StatusFile` process lock; dispatches sync or async
  metadata initialization.
- Scheduler task names include the cache name:
  `"FileCache:<name>:background-cleanup"` and `"FileCache:<name>:free-space"`.
- `getImpl`: `lower_bound(range.left)`, includes previous segment when it
  overlaps, respects `fileSegmentsLimit`, bypass-threshold shortcut for large
  ranges returns one synthetic `DETACHED` segment.
- `fillHolesWithEmptyFileSegments`: `getOrSet` path creates metadata-owned
  `EMPTY` segments; `get` path creates synthetic `DETACHED` placeholders.
- `doTryReserve` fast path requires: main iterator exists AND no main eviction
  needed AND `query_context == nullptr`.
- Background free-space keeper: collector/remover/finalizer pipeline using two
  `FileCacheBoundedQueue<EvictionBatchPtr>` instances. `running_removers` must
  be incremented **before** submitting each remover task to the worker pool;
  roll back on submission failure.
- Metadata load: parallel listing/loading with
  `FileCacheBoundedQueue<KeyDirectoryWork>` capacity 1000 when workers > 0;
  listing producers use `tryPush`, falling back to direct load when the queue
  is full or capacity is 0; last listing worker calls `finish`.
- Overcommit policy (`LRU_OVERCOMMIT` / `SLRU_OVERCOMMIT`): explicitly reject
  with `VELOX_FAIL`; do not stub.

- [ ] **Step 15: Update `CMakeLists.txt`**

Append the compiled core library to
`velox/ch/Interpreters/FileCache/CMakeLists.txt`. Preserve the existing
`target_sources(velox_ch_filecache ...)` entries from Tasks 008 and 010 and the
existing `add_subdirectory(tests)` block:

```cmake
velox_add_library(
  velox_ch_filecache_core
  IFileCachePriority.cpp
  LRUFileCachePriority.cpp
  SLRUFileCachePriority.cpp
  SplitFileCachePriority.cpp
  EvictionCandidates.cpp
  FileSegment.cpp
  Metadata.cpp
  FileCache.cpp
  QueryLimit.cpp
)

target_link_libraries(
  velox_ch_filecache_core
  PUBLIC
    velox_ch_filecache
    velox_file
    velox_memory
    Folly::folly
    fmt::fmt
)

if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Do not replace or remove leaf, ShardedMap, or settings test targets from the
shared tests CMake file.

- [ ] **Step 16: One final build**

Reconfigure using the same command as Step 8, then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_core_scc_test \
  > <velox_build_dir>/build_task_012_scc.log 2>&1
```

Expected: exit code 0. This is the single compile/link closure that proves the
SCC is complete.

- [ ] **Step 17: Run the focused tests**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_filecache_core_scc_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_012_scc.log 2>&1
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 18: Inspect task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Interpreters/FileCache/CMakeLists.txt \
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
  velox/ch/Interpreters/FileCache/EvictionCandidates.cpp \
  velox/ch/Interpreters/FileCache/FileSegmentInfo.h \
  velox/ch/Interpreters/FileCache/FileSegment.h \
  velox/ch/Interpreters/FileCache/FileSegment.cpp \
  velox/ch/Interpreters/FileCache/Metadata.h \
  velox/ch/Interpreters/FileCache/Metadata.cpp \
  velox/ch/Interpreters/FileCache/FileCache.h \
  velox/ch/Interpreters/FileCache/FileCache.cpp \
  velox/ch/Interpreters/FileCache/QueryLimit.h \
  velox/ch/Interpreters/FileCache/QueryLimit.cpp \
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp \
  velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp \
  velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp
```

Expected: no whitespace errors, no files outside the declared scope changed by
this task, changes remain unstaged and uncommitted.

Also run the `LockedKey` member-order structural check (replacing the
forbidden `offsetof` compile-time assertion):

```bash
awk '/class LockedKey/,/^};/' velox/ch/Interpreters/FileCache/Metadata.h \
  | grep -n 'key_metadata\|KeyGuard::Lock lock' \
  > <velox_build_dir>/check_task_012_lockedkey_order.log
```

Expected: `key_metadata` appears on an earlier line than `lock` in the
captured output. False-green probe: temporarily swap the two declaration
lines, rerun the check, confirm it now reports `lock` before `key_metadata`
(i.e., the check would catch the regression), then restore the original
order and rerun to confirm a clean pass. Record both outputs in the receipt.

- [ ] **Step 19: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/012-filecache-core-scc-result.md
```

Use exactly this structure:

````markdown
# Task 012 Result: `FileCache` Center SCC

## Status

status: success

## Velox status

```text
<paste branch, HEAD, and final `git status --short`>
```

## Files changed

```text
<list only task-owned files>
```

## Commands run

```text
<paste configure, build, test, and verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_task_012_scc.log
<velox_build_dir>/build_task_012_red.log
<velox_build_dir>/build_task_012_scc.log
<velox_build_dir>/test_task_012_scc.log
```

## Verification

```text
Red build failed because SCC headers and .cpp files were absent.
Final build exit code:
Focused test result:
git diff --check result:
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 013: FileCacheFactory and FileCacheManager.
```
````

If blocked or failed, set the status accordingly, include the first actionable
error and log path, and do not claim success.

## Explicit Exclusions

Do not implement in this task:

```text
FileCacheFactory / FileCacheManager
FileCacheBufferedInput / FileCacheInputStream
FileCacheRequestContext / FileCacheFileIdentity
WriteBufferToFileSegment / TemporaryDataOnDisk
CacheFileSystem / CachedReadFile
cache_on_write_operations
LRU_OVERCOMMIT / SLRU_OVERCOMMIT implementations
Prometheus/custom metrics (keep no-op shims)
Gluten integration
```

These belong to Tasks 013 and 014.
