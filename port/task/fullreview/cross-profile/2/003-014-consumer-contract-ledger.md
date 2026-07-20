# Task 7-A — Phase-A Consumer-Contract & Structural Recovery Ledger (Tasks 003–014)

> Mandatory post-Task-014 FileCache full review, **Phase A only**: top-down CH-source
> contract derivation per §A and §3 of
> `/root/chang/OneDrive/share_data/local-cache/filecache-port-authoring-guide.md`.
> Strictly read-only; CH-only. No Velox / receipts / profile conclusions were inspected.

## 0. Scope, inputs, method, trust

- **CH baseline (frozen):** repo `/root/oss/clickhouse`, HEAD `197d60661b6`
  (`197d60661b6d7637e98ef5878703ba36505f15c6`, *"Task 014: Accept `FileCache` buffered input"*).
  Working tree clean; port changes are documentation-only. Nothing was modified/staged/committed.
- **In-scope CH source (the only behavior truth):**
  - `src/Interpreters/FileCache/` — `FileCache`, `FileSegment`, `Metadata`/`ShardedMap`,
    `IFileCachePriority`/`LRU`/`SLRU`/`Split`/`EvictionCandidates`, `QueryLimit`,
    `FileCacheFactory`, `FileCacheKey`, `FileCacheUtils`, `Guards`, `WriteBufferToFileSegment`.
  - `src/IO/OpenedFileCache.h`.
  - `src/Disks/IO/CachedOnDiskReadBufferFromFile.{h,cpp}` (+ `CachedOnDiskWriteBufferFromFile.{h,cpp}`).
  - Infra leaves reached from the above: `src/Common/ConcurrentBoundedQueue.h`,
    `src/Common/StatusFile.{h,cpp}`, `src/Common/ThreadPool.h`, `Core/BackgroundSchedulePool*`.
- **Method (per §A):** enumerate *real* call sites, derive the contract each consumer actually
  relies on (overload set, state transition, error behavior, ownership/lifetime, concurrency,
  persistence), record the CH internal structure as the §3 direct-translation baseline, then run
  both gates (behavior-without-callsite ⇒ over-port; callsite-without-behavior ⇒ hole).
- **Task numbers bound ownership only, never behavior.** Guide §1/§B pin **003–009** and the
  012/014 boundary hints explicitly; **010/011/013** and the exact core/factory split are
  *inferred from CH SCC layering* and are flagged `[INFERRED]` because the port task files could
  not be inspected. Where a row's contract is real CH but its task tag is inferred, the contract
  still stands on its CH citation.
- **Trust:** every structural container/lock/line below was personally opened and verified in CH
  source at HEAD (`Guards.h`, `ShardedMap.h`, `FileCacheUtils.h`, `FileCacheKey.{h,cpp}`,
  `StatusFile.cpp`, `ConcurrentBoundedQueue.h`, `FileSegment.h`, `FileSegmentInfo.h`, `FileCache.h`,
  `FileCache.cpp:1695–1905`, the four consumer headers, `EvictionCandidates.h:180–205`,
  `Metadata.h:95–100/265–277/425–432`, `Metadata.cpp:700–839`), except a subset of
  `*.cpp` line numbers in the priority/eviction and metadata subsystems that came from a read-only
  structural sweep and are marked `[sweep]`; each was cross-checked against a verified header.

- **This repo is a *customized* FileCache**, not mainline: it has a separate `CacheStateGuard`
  (size/element counters) split off from `CachePriorityGuard`, an **Overcommit / per-client**
  priority layer (idle-client eviction, `getUsageStatPerClient`), resumable **eviction cursors**,
  deferred **invalidated-entry** cleanup, dynamic cache resize, split (data/system) cache, and
  async metadata loading. The port must match *this* structure, not upstream CH.

---

## 1. Reachable dependency inventory (owner task • layer • CH file)

Legend — layer: `L`=infra leaf, `SCC`=FileCache center strongly-connected core,
`F/M`=Factory/Manager layer, `IN`=buffered-input/read-stream consumer. Task tag `✓`=guide-confirmed,
`[INF]`=inferred from layering.

| # | Dependency | CH file(s) | Owner task | Layer |
|---|---|---|---|---|
| D1 | `ConcurrentBoundedQueue<T>` | `src/Common/ConcurrentBoundedQueue.h` | 003 ✓ | L |
| D2 | `StatusFile` | `src/Common/StatusFile.{h,cpp}` | 004 ✓ | L |
| D3 | `ThreadPool` / `ThreadFromGlobalPool` (3-phase shutdown) | `src/Common/ThreadPool.h` | 005 ✓ | L |
| D4 | `BackgroundSchedulePool` / `BackgroundSchedulePoolTaskHolder` (immediate vs delayed) | `Core/BackgroundSchedulePool*`, held `FileCache.h:22,299,311` | 006 ✓ | L |
| D5 | reader/writer relay (already-open append, zero-copy) | `WriteBufferToFileSegment.{h,cpp}`, `CachedOnDiskReadBufferFromFile.cpp` | 007 ✓ | IN/SCC |
| D6 | `FileCacheKey` parser + `FileCacheUtils` checked arithmetic | `FileCacheKey.{h,cpp}`, `FileCacheUtils.h` | 008 ✓ | L |
| D7 | `ShardedMap<Key,Value,32>` | `src/Interpreters/FileCache/ShardedMap.h` | 009 ✓ | L |
| D8 | `IFileCachePriority` + `LRU`/`SLRU`/`Split` + `EvictionCandidates` | `IFileCachePriority.*`, `LRUFileCachePriority.*`, `SLRUFileCachePriority.*`, `SplitFileCachePriority.*`, `EvictionCandidates.*` | 010 `[INF]` | SCC |
| D9 | `FileSegment` + `FileSegmentsHolder` (state machine) | `FileSegment.{h,cpp}`, `FileSegmentInfo.h` | 011 `[INF]` | SCC |
| D10 | writer resume / partial-write reconcile | `WriteBufferToFileSegment.{h,cpp}`, `CachedOnDiskWriteBufferFromFile.{h,cpp}` | 012 ✓(hint) | IN |
| D11 | `CacheMetadata`/`LockedKey`/`KeyMetadata`/`FileSegmentMetadata` + `DownloadQueue`/`CleanupQueue` | `Metadata.{h,cpp}` | 013 `[INF]` | SCC |
| D12 | `FileCache` core (`getOrSet`/`get`/`tryReserve`/eviction/resize) | `FileCache.{h,cpp}` | 013 `[INF]` | SCC |
| D13 | `FileCacheQueryLimit` (per-query write cap) | `QueryLimit.{h,cpp}` | 013 `[INF]` | SCC |
| D14 | `FileCacheFactory` (registry singleton) | `FileCacheFactory.{h,cpp}` | 013/F-M `[INF]` | F/M |
| D15 | `CachedOnDiskReadBufferFromFile` (buffered input) + `OpenedFileCache` | `CachedOnDiskReadBufferFromFile.{h,cpp}`, `src/IO/OpenedFileCache.h` | 014 ✓(hint) | IN |

> **Manager note:** CH has **no** "Manager" class. The only CH owner of cache-instance
> registration/lookup/config-reload is `FileCacheFactory` (singleton, `caches_by_name`
> `unordered_map`; `FileCacheFactory.h:42,70–71`). Any port-side "Manager" beyond
> `getOrCreate`/`get`/`getByName`/`updateSettingsFromConfig`/`loadDefaultCaches` is **port-only**
> and has no CH contract to port.

---

## 2. Per-call-site consumer contract rows

Columns: **Behavior | Call-site (file:line) | Signature/overload (decl) | State transition | Error
behavior | Ownership/lifetime | Concurrency | Persistence | Velox-replacement | Task**.
"Velox-replacement" states only whether a *semantic-1:1 infra swap* is admissible (§3); anything
guarantee-changing is deferred to §3b.

### D1 — `ConcurrentBoundedQueue<T>` (003)

Two real consumers, both in `FileCache.cpp`: (a) background eviction pipeline
`freeSpaceRatioImpl` (`1704–1905`); (b) startup metadata loader `key_dirs_queue` (`2258–2379`).

| Behavior | Call-site | Signature (decl) | State transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Velox | Task |
|---|---|---|---|---|---|---|---|---|---|
| bounded ctor | `FileCache.cpp:1704–1705`, `2260` | `explicit ConcurrentBoundedQueue(size_t)` `ConcurrentBoundedQueue.h:104` | — | — | stack-local, drained+`finish`ed before scope end | N producers/consumers | none | 1:1 | 003 |
| **timed `tryPush`** | `FileCache.cpp:1863` | `bool tryPush(T&&, UInt64 ms=0)` `ConcurrentBoundedQueue.h:140` | waits ≤`ms` on `push_condition` for `finished‖size<max_fill` | returns `false` on timeout **or** finished (never throws) | moves batch in | mutex+CV | none | 1:1 | 003 |
| default-timeout `tryPush` | `FileCache.cpp:2345` | `bool tryPush(const T&, UInt64 ms=0)` `ConcurrentBoundedQueue.h:135` | `ms=0` ⇒ non-blocking push | `false` if full (timeout 0) or finished | copy in | mutex | none | 1:1 | 003 |
| blocking `pop` | `FileCache.cpp:1721`(blk),`1763`,`2282` | `bool pop(T&)` `ConcurrentBoundedQueue.h:153` | waits on `pop_condition` for `finished‖!empty` | `false` iff finished **and** empty | moves out | mutex+CV | none | 1:1 | 003 |
| **non-blocking `tryPop`** | `FileCache.cpp:1721`(non-blk) | `bool tryPop(T&)` `ConcurrentBoundedQueue.h:165` (no CV wait) | pops front if present | `false` if empty (never waits) | moves out | mutex only | none | 1:1 | 003 |
| `push` (rvalue) | `FileCache.cpp:1774` | `bool push(T&&)` `ConcurrentBoundedQueue.h:122` | blocking emplace_back | `false` if finished | moves in | mutex+CV | none | 1:1 | 003 |
| `finish` | `FileCache.cpp:1792,1895,1897,2273,2360` | `bool finish()` `ConcurrentBoundedQueue.h:201` | sets `is_finished`, `notify_all` both CVs | idempotent; returns prior state | — | mutex | none | 1:1 | 003 |

- **Overload-set requirement (guide 003 reopen):** the consumer distinguishes *timed* `tryPush`
  (`1863`, `push_timeout_ms=10`, ≤`1000` attempts, interleaving `finalize_removed` so removers
  never block on a full queue — `1855–1877`) from *non-blocking* `tryPop` (`1721`) and *blocking*
  `pop`/final drain (`1898 finalize_removed(blocking=true)`). `emplaceImpl<back>` perfect-forwarding
  (`ConcurrentBoundedQueue.h:30–61`) backs `push`/`tryPush`/`emplace`. Startup loader relies on
  `tryPush` returning `false` when capacity is `0` (no loader threads: `2258–2260`) and on `finish`
  unblocking drainers (`2360`).
- **Persistence:** none — purely in-memory backpressure.

### D2 — `StatusFile` (004)

| Behavior | Call-site | Signature (decl) | State transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Velox | Task |
|---|---|---|---|---|---|---|---|---|---|
| create status file w/ 3-line info | `FileCache.cpp:517` | `StatusFile(std::string, FillFunction)` `StatusFile.h`; `make_unique<StatusFile>(base/"status", StatusFile::write_full_info)` | ctor: exists-check→INFO log; `open O_WRONLY|O_CREAT|O_CLOEXEC 0666`; `flock LOCK_EX|LOCK_NB`; `ftruncate 0`; `lseek SEEK_SET`; `fill`+`finalize`/`cancel` | throws `CANNOT_OPEN_FILE` (open/lock; `EWOULDBLOCK`⇒*"Another server instance in same directory is already running."*), `CANNOT_TRUNCATE_FILE`, `CANNOT_SEEK_THROUGH_FILE`; ctor catch closes fd then rethrows | held as `std::unique_ptr<StatusFile> status_file` `FileCache.h:343` for cache lifetime | one process (`flock`) | **disk**: creates/locks `<base>/status`; unlink on dtor | 1:1 (POSIX file+flock) | 004 |
| verbatim 3-line diagnostic | `StatusFile.cpp:38–43` | `write_full_info` | — | — | — | — | writes `"PID: <pid>\n"`,`"Started at: <LocalDateTime>\n"`,`"Revision: <rev>\n"` | text **must be byte-exact** | 004 |
| dtor: close **then** unlink | `StatusFile.cpp:109–116` | `~StatusFile()` | — | close error ⇒ `LOG_ERROR` (no throw); unlink error ⇒ `LOG_ERROR` | RAII | — | unlink `<base>/status` | ordering `close`→`unlink` is contractual | 004 |

- **Reopen-relevant (guide 004):** exact 3-line text and dtor order (`closeNoThrow` then `unlink`)
  are the invisible-from-leaf contracts. `write_pid` (`StatusFile.cpp:33`) exists but the cache
  consumer uses **only** `write_full_info` ⇒ `write_pid` is over-port *for this scope* (§7).

### D3 — `ThreadPool` / `ThreadFromGlobalPool` (005, stable)

| Behavior | Call-site | Signature | State transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Velox | Task |
|---|---|---|---|---|---|---|---|---|---|
| lock-free eviction removers | `FileCache.cpp:1758 eviction_pool->scheduleOrThrowOnError`, `1899 wait()` | `ThreadPool::scheduleOrThrowOnError`, `wait` | schedule N workers, join at pass end | `scheduleOrThrowOnError` throws on schedule failure | `std::unique_ptr<ThreadPool> eviction_pool` `FileCache.h:307` | N removers | none (removers delete files) | 1:1 pool | 005 |
| bg download / cleanup threads | `Metadata.cpp` `download_threads`(`Metadata.h:299`), `cleanup_thread`(`:300`) | `ThreadFromGlobalPool` | started on init, **joined in `shutdown()`** before dtor | join is unconditional (`joinable()` guarded) | see D11 destruction order | download/cleanup workers | none | 1:1 | 005 |
| async metadata load | `FileCache.h:294 load_metadata_main_thread` | `ThreadFromGlobalPool` | started when `load_metadata_asynchronously` | `throwInitExceptionIfNeeded` re-raises stored `init_exception` | joined on `deactivateBackgroundOperations`/dtor | 1 loader (+worker pool) | reads disk metadata | 1:1 | 005 |

- Guide marks 005 stable (its full 3-phase shutdown trace exists). Contract here is only that these
  pools obey **join-before-destroy**; see D11 for the hard `shutdown()`-before-`~CacheMetadata` rule.

### D4 — `BackgroundSchedulePool` / `...TaskHolder` (006, immediate ≻ delayed)

| Behavior | Call-site | Signature | State transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Velox | Task |
|---|---|---|---|---|---|---|---|---|---|
| create free-space task | `FileCache.cpp:591` | `getSchedulePool().createTask(id,name,fn)` | task registered | — | `BackgroundSchedulePoolTaskHolder keep_up_free_space_ratio_task` `FileCache.h:299` | pool | none | 1:1 | 006 |
| **immediate schedule** | `FileCache.cpp:592`, `1955`, `2925` | `task->schedule()` | run ASAP; **must preempt a pending `scheduleAfter`** | — | — | pool | none | ordering-sensitive | 006 |
| **delayed schedule** | `FileCache.cpp:563,1641,1957` | `task->scheduleAfter(ms)` | run after delay unless an immediate `schedule` intervenes | — | — | pool | none | ordering-sensitive | 006 |
| threshold-triggered wake | `FileCache.cpp:559–562` | `createTask(...,[this]{background_cleanup_task->schedule();})` | invalidated-entry threshold callback wakes cleanup immediately | — | — | pool | none | 1:1 | 006 |
| deactivate (phase-out) | `FileCache.cpp:574–575,2674–2678` | `task->deactivate()` | stop rescheduling; wait for running tick | — | — | pool | none | must block running tick | 006 |

- **Reopen-relevant (guide 006):** the two *real* consumers are `keep_up_free_space_ratio_task`
  and `background_cleanup_task`; the invisible contract is that **`schedule()` (immediate) beats a
  pending `scheduleAfter()` (delayed)** — e.g. cleanup re-arms via `scheduleAfter(interval)`
  (`1957`) but the threshold callback forces `schedule()` (`562`,`1955`); free-space keeper
  self-reschedules with `scheduleAfter(reschedule_ms)` (`1641`) yet is kicked immediately on init
  (`592`). `deactivate()` must drain a running tick (shutdown phase, `2674–2678`).

### D5/D10 — `WriteBufferToFileSegment` (writer relay & reconcile) (007 already-open append; 012 resume/partial-write)

| Behavior | Call-site | Signature (decl) | State transition (FileSegment) | Error behavior | Ownership/lifetime | Concurrency | Persistence | Velox | Task |
|---|---|---|---|---|---|---|---|---|---|
| become downloader (already-open) | `WriteBufferToFileSegment.cpp:63–64` | `String FileSegment::getOrSetDownloader()`; compare `FileSegment::getCallerId()` | `EMPTY→DOWNLOADING` (or asserts already this caller) | if downloader≠caller ⇒ throw (cannot append) | writer holds `FileSegment*` or owns `FileSegmentsHolderPtr segment_holder` `WriteBufferToFileSegment.h:48,51` | single downloader | none | adapter-only (007) | 007 |
| reserve before write | `WriteBufferToFileSegment.cpp:84` | `bool FileSegment::reserve(size,timeout_ms,failure_reason,stat=nullptr)` `FileSegment.h:216` | reserves ≥`size`; may evict | returns `false` (no space) ⇒ writer throws | — | downloader-only | may **evict** (disk) | 1:1 | 012 |
| append reserved bytes | `WriteBufferToFileSegment.cpp:109` | `void FileSegment::write(char*,size,offset_in_file)` `FileSegment.h:224` | writes into cache file at `written_bytes` | throws on disk/logic error | `written_bytes` tracks progress `WriteBufferToFileSegment.h:54` | downloader-only | **disk write** | 1:1 | 012 |
| release downloader (SCOPE_EXIT) | `WriteBufferToFileSegment.cpp:71–76` | `if(isDownloader()) completePartAndResetDownloader(); else chassert(false)` | `DOWNLOADING→PARTIALLY_DOWNLOADED` (notify waiters) | assertion if not downloader | RAII cleanup even on throw | notifies `cv` waiters | none | 1:1 | 007 |
| read-back spilled temp | `WriteBufferToFileSegment.cpp getReadBufferImpl` | `IReadableWriteBuffer::getReadBufferImpl()` `WriteBufferToFileSegment.h:44` | — | — | reads own segment back | — | reads cache file | 1:1 | 012 |
| unsupported ops | `WriteBufferToFileSegment.h:34` | `jumpToPosition(size_t)` ⇒ throw `NOT_IMPLEMENTED` | — | intentional throw | — | — | — | keep NOT_IMPLEMENTED | 012 |

- **Boundary (guide §B):** 007 owns only the *already-open append* relay
  (`getOrSetDownloader`→`reserve`→`write`→`completePartAndResetDownloader`); **resume &
  partial-write reconcile belong to 012** and must not be silently absorbed by 007.
- `CachedOnDiskWriteBufferFromFile` (D10) drives lazy `FileSegmentRangeWriter`, distributed-cache
  retry via `jumpToPosition`/`ignore_bytes`, and disk-full detection on `errno 28/122`
  (`CachedOnDiskWriteBufferFromFile.cpp:512–520`) — see §6 E-candidate on physical-size reconcile.

### D6 — `FileCacheKey` + `FileCacheUtils` (008)

| Behavior | Call-site | Signature (decl) | Transition | Error behavior | Ownership | Concurrency | Persistence | Velox | Task |
|---|---|---|---|---|---|---|---|---|---|
| parse hex key (validated) | broad (system tables, `removePathIfExists`) | `static FileCacheKey fromKeyString(const std::string&)` `FileCacheKey.cpp:41` | — | **`size()!=32` ⇒ throw `BAD_ARGUMENTS "Invalid cache key hex: {}"`**, then `unhexUInt<UInt128>` | value type (`UInt128 key`) | pure | none | length+hex validation is contract | 008 |
| key from path | `FileCacheKey.cpp:31` | `fromPath(path)` = `sipHash128(path)` | — | — | value | pure | none | 1:1 hash | 008 |
| key from raw / random | `FileCacheKey.cpp:26,36` | `random()`=UUIDv4 underlying; `fromKey(UInt128)` | — | — | value | pure | none | 1:1 | 008 |
| to string (lowercase hex) | `FileCacheKey.cpp:21` | `toString()`=`getHexUIntLowercase(key)` | — | — | value | pure | **on-disk dir name** | lowercase-hex exact | 008 |
| checked round-up | `FileCache.cpp`/callers of `FileCacheUtils` | `roundUpToMultiple(num,mult)` `FileCacheUtils.h:18` | — | **overflow ⇒ throw `std::overflow_error "FileCacheUtils::roundUpToMultiple: rounded-up value does not fit in size_t"`** via `common::addOverflow`; remainder-based (not `num+mult-1`) | pure | pure | none | must keep exact algo+message | 008 |
| round-down | same | `roundDownToMultiple(num,mult)` `FileCacheUtils.h:11` | — | `mult==0`⇒`num` | pure | pure | none | 1:1 | 008 |

- `FileCacheKey`'s `UInt128` ctor is **private** (`FileCacheKey.h:26`); external construction only via
  the 4 named factories. `FileCacheKeyAndOffset` + `FileCacheKeyAndOffsetHash` (`:29–36`) is the
  `QueryLimit::Records` key.

### D7 — `ShardedMap<Key,Value,32>` (009)

Real consumer: `CacheMetadata::origins` = `ShardedMap<OriginPoolKey, OriginInfoPtr>` (`Metadata.h:290`).
(The per-key `MetadataBucket` sharding is a **separate** hand-rolled structure — see §3.)

| Behavior | Call-site | Signature (decl) | Transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Velox | Task |
|---|---|---|---|---|---|---|---|---|---|
| find/emplace origin under shard lock | `Metadata.cpp getOrCreateSharedOrigin` `[sweep]` | `template<F> auto withShard(const Key&, F&&) const` `ShardedMap.h:33` | `f(map)` runs under owning shard's `std::mutex` | `f`'s exceptions propagate; **size accounted via `SCOPE_EXIT` delta even on throw** (`ShardedMap.h:38,64–70`) | value = `OriginInfoPtr` (shared_ptr) copied out; no `&value` escapes | 32 independently-locked shards; callback must not re-take same shard lock | none | **guarantee-changing swap ⇒ §3b** | 009 |
| iterate all shards | `[sweep]` | `template<F> void forEachShard(F&&) const` `ShardedMap.h:44` | each shard locked in turn (one at a time) | as above | — | one shard lock held at a time | none | 1:1 | 009 |
| size | `[sweep]` | `size_t size() const` `ShardedMap.h:55` (`atomic total_count`, relaxed) | — | — | — | lock-free | none | 1:1 | 009 |

- **009 oracle (guide appendix):** `emplace` that throws must **not** increment size — guaranteed by
  the `SCOPE_EXIT(accountSizeDelta(size_before, map.size()))` measuring the actual post-`f` size.

### D8 — `IFileCachePriority` + `LRU`/`SLRU`/`Split` + `EvictionCandidates` (010 `[INF]`)

Consumers: `FileCache.cpp` (reservation/eviction/resize) and `FileSegment.cpp` (increasePriority).

| Behavior | Call-site | Signature (decl) | Transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Velox | Task |
|---|---|---|---|---|---|---|---|---|---|
| collect eviction candidates | `FileCache.cpp:1827` (bg), reserve path `[sweep]` | `collectCandidatesForEviction(EvictionInfo&,stat,EvictionCandidates&,invalidated,reservee,EvictionCursor,max,is_total,origin,cache_guard,cache_state_guard)` | scans under **ReadLock**, resumes from cursor | fills `EvictionCandidates`; `CollectStatus` | candidates pinned via `IteratorPtr` shared_ptr until finalize | `CachePriorityGuard::ReadLock` | none (collection) | 1:1 (see §3b list) | 010 |
| evict (delete files) | `FileCache.cpp:1767 batch->candidates->evict()` | `EvictionCandidates::evict()` | removes files off-lock | logs+counts failures, continues | runs w/o cache lock | no cache lock (removers) | **disk delete** | 1:1 | 010 |
| finalize write-side | `FileCache.cpp:1734,1843,3111` | `afterEvictWrite(WriteLock&)`, `removeEntries(invalidated,WriteLock&)` | remove queue entries | — | — | `WriteLock` | none | 1:1 | 010 |
| finalize state-side | `FileCache.cpp:1737` | `afterEvictState(CacheStateGuard::Lock)` | decrement size/element counters | — | — | `CacheStateGuard` | none | 1:1 | 010 |
| add entry on reservation | `[sweep] LRUFileCachePriority.cpp:135–141` | `add(...,WriteLock, Lock*)` | append to `std::list`; `incrementSize` under state lock | throws if limits violated | returns `IteratorPtr` | `WriteLock` + `CacheStateGuard` | none | 1:1 | 010 |
| increase priority (dedup) | `FileSegment.cpp:1441 [sweep]` ← `FileSegment::increasePriority` | `Iterator::increasePriority` | LRU: `splice` to back; SLRU: probationary→protected | — | `std::atomic_flag increasing_priority` dedups (`FileSegment.h:329`) | `WriteLock` | none | 1:1 (splice) | 010 |
| iterator size/invalidate | `[sweep]` | `Iterator::updateSize/decrementSize/invalidate/getEntry` `IFileCachePriority.h` | `decrementSize` lock-free atomic; `invalidate` noexcept | — | entry via `weak_ptr<Entry>` (no pin) | `decrementSize`/`invalidate` lock-free; `add`/`remove` need `WriteLock` | none | 1:1 | 010 |
| dynamic resize | `FileCache.cpp:3070–3175 [sweep]` | `modifySizeLimits(...)` | may evict from both SLRU queues | throws if new limits violated | — | `WriteLock`+`CacheStateGuard` | disk delete | 1:1 | 010 |

### D9 — `FileSegment` + `FileSegmentsHolder` (011 `[INF]`)

State machine `FileSegmentState` (`FileSegmentInfo.h:10–42`):
`DOWNLOADED, EMPTY, DOWNLOADING, PARTIALLY_DOWNLOADED_NO_CONTINUATION, PARTIALLY_DOWNLOADED, DETACHED`.
Kinds: `Regular`, `Ephemeral` (`:44–59`). Primary consumer = reader (D15); also writer (D5) and
cache internals.

| Behavior | Call-site (reader) | Signature (decl) | Required pre-state | State transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Task |
|---|---|---|---|---|---|---|---|---|---|
| acquire downloader | `CachedOnDiskReadBufferFromFile.cpp:689` | `String getOrSetDownloader()` `FileSegment.h:125` | `EMPTY`/`PARTIALLY_DOWNLOADED` | `→DOWNLOADING` for this caller, else returns other id | never throws (returns id) | caller becomes single writer | notifies `cv` | 011 |
| wait for state change | `:667` | `State wait(size_t offset)` `FileSegment.h:132` | `DOWNLOADING` | blocks on `cv` until state≠`DOWNLOADING` or write reaches offset | — | non-downloaders wait | — | 011 |
| reserve space | `:1114`,`:1616` | `bool reserve(size,timeout_ms,failure_reason,stat,reserve_hint=0)` `FileSegment.h:216` | downloader | reserved_size+=; may evict | `false` (no space) — reader switches to bypass/NO_CONTINUATION | `reserve_hint` bounds reserve-ahead to read_until | downloader-only | may evict | 011/012 |
| write into cache | `:1272` | `void write(char*,size,offset_in_file)` `FileSegment.h:224` | downloader, reserved | downloaded_size+= ; notifies `cv` | throws on disk/logic error | downloader-only | **disk write** | 011/012 |
| set/extract/reset remote reader | `:521,:537,:1024,:1174,:1371,:1740,:1882` | `setRemoteFileReader`/`extractRemoteFileReader`/`resetRemoteFileReader` `FileSegment.h:231–238` | — | reader stored in lazy `DownloadState` | — | **`resetRemoteFileReader` must precede `completePartAndResetDownloader`** (else another thread `extractRemoteFileReader`s a still-registered, still-borrowed reader — comments `:1016–1025,:1168–1175,:1733–1741,:1877–1884`) | single writer | none | **011/007** |
| finish part, drop downloader | `:1175,:1373,:1488,:1884` | `void completePartAndResetDownloader()` `FileSegment.h:202` | downloader | `DOWNLOADING→PARTIALLY_DOWNLOADED`; notify | asserts is-downloader | — | notifies `cv` | none | 011 |
| finish w/o continuation | `:1025,:1741` | `void setDownloadFinishedWithoutContinuation()` `FileSegment.h:245` | downloader | `→PARTIALLY_DOWNLOADED_NO_CONTINUATION`; shrink to downloaded on complete | non-failure path | — | notify | none | 011 |
| complete + pop holder | `:898,:926,:1905` | `FileSegmentsHolder::completeAndPopFront(allow_bg,force_shrink)` `FileSegment.h:358` | — | `→DOWNLOADED`/shrink/`DETACHED`; may enqueue **background download** | — | holder owns `std::list<FileSegmentPtr>` `FileCache_fwd_internal.h:16` | complete notifies | rename `<off>`→`<off>_<size>` on full download | 011 |
| bump priority | `:906` | `void increasePriority()` `FileSegment.h:170` | — | LRU/SLRU move | — | dedup via atomic_flag | `WriteLock` inside | none | 011 |
| local-read flags | `:363 [sweep]` | `int getFlagsForLocalRead()`=`O_RDONLY|O_CLOEXEC` `FileSegment.h:119` | — | — | — | — | opens cache file | 011 |

- **Handoff invariant (FileSegment.h:226–227):** when `state()!=DOWNLOADING` and a remote reader is
  present, `reader.available()==0` and `reader.getFileOffsetOfBufferEnd()==getCurrentWriteOffset()`
  — this is the zero-copy handoff contract the reader relies on for reader reuse.
- **Download-term granularity (reader `:1222–1231`):** the downloader releases ownership after
  **every `buffer_size` write** (`completePartAndResetDownloader`), so another thread may take over;
  the port must not hold the downloader across multiple `next()`s (deadlocks waiters). Exception:
  `readBigAt` stays downloader for the whole call (reader `:1877–1884`).

### D11 — `CacheMetadata`/`LockedKey`/`KeyMetadata` + queues (013 `[INF]`)

| Behavior | Call-site | Signature (decl) | Transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Task |
|---|---|---|---|---|---|---|---|---|
| get/lock key metadata | `FileCache.cpp` getOrSet/reserve `[sweep]`; `Metadata.cpp:332–353` | `getKeyMetadata(key,policy)` returns `it->second` (**shared_ptr copy, no `&value`**) | — | policy `THROW`/`CREATE`/`RETURN_NULL` | `KeyMetadataPtr` shared_ptr | `CacheMetadataGuard` (bucket) then released before `KeyGuard` | may create key dir | 013 |
| lock key (RAII) | broad | `LockedKey` ctor `Metadata.cpp:1110` | holds `KeyGuard::Lock` for op duration | — | `LockedKeyPtr` pins `KeyMetadata` alive; **`lock` destructs before `key_metadata`** (`Metadata.h:428–429`) | `KeyGuard` | — | 013 |
| create/remove file segment meta | `FileCache.cpp addFileSegment [sweep]`; `Metadata.cpp:1224–1289` | `LockedKey::emplace/removeFileSegment` (returns next `std::map::iterator`) | offset-map insert/erase | throws on access/logic | value `FileSegmentMetadataPtr`; `releasable()`=`use_count==1` (`Metadata.h:42`) | `KeyGuard` | — | 013 |
| deferred key removal | `Metadata.cpp:1118–1130` (dtor) | `LockedKey::~LockedKey`→`addToCleanupQueue` | if empty+`ACTIVE`⇒`REMOVING` + enqueue | — | key stays in bucket until cleanup thread erases | **cannot take `CacheMetadataGuard` while holding `KeyGuard`** ⇒ defers to cleanup thread | — | 013 |
| background download enqueue | `[sweep] Metadata.cpp:886` | `DownloadQueue::add` (weak_ptr) | segment queued; `FileSegmentsHolder` pins it during download | expired weak_ptr ⇒ skip | `std::weak_ptr<FileSegment>` (segment may be removed+re-added) | `DownloadQueue` mutex+cv | disk write (download) | 013/006 |
| cleanup enqueue | `Metadata.cpp:704–719` | `CleanupQueue::add` | insert `unordered_set`; `notify_one` | — | key set | mutex+cv | `fs::remove_all(key_dir)` | 013 |
| shutdown | `Metadata.cpp:1030–1042` | `CacheMetadata::shutdown()` | cancel queues, **join** download+cleanup threads | — | **must run before `~CacheMetadata` (default dtor)** else use-after-free | joins | — | 013/005 |

### D12 — `FileCache` core public API (013 `[INF]`)

| Behavior | Call-site (reader) | Signature (decl) | Returns / transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Task |
|---|---|---|---|---|---|---|---|---|
| getOrSet contiguous segments | `CachedOnDiskReadBufferFromFile.cpp:258,1839` | `FileSegmentsHolderPtr getOrSet(key,off,size,file_size,settings,limit,origin,align)` `FileCache.h:147` | **contiguous, no holes; each `DOWNLOADED`/`DOWNLOADING`/`EMPTY`**; held pointers ⇒ not removed | throws on bad args/disk-fail (unless skip) | caller owns `FileSegmentsHolderPtr` (unique_ptr) | metadata+key locks (Guards order) | may create segments/dirs | 013 |
| get (detached EMPTY) | `:246,1829` | `get(key,off,size,limit,user)` `FileCache.h:166` | contiguous; `EMPTY`⇒**DETACHED** (never fills) | — | caller owns holder | as above | none (read-only view) | 013 |
| cache-only must-exist | `:233,1818` (temp_cache_only) | `getDownloadedContiguousOrEmpty(key,off,size,user)` `FileCache.h:177` | downloaded-prefix or EMPTY holder; never synthesizes | — | caller owns holder | as above | none | 013 |
| reserve (via FileSegment) | (FileSegment.reserve→) `FileCache::tryReserve` `FileCache.h:230` | `bool tryReserve(seg,size,stat,origin,timeout_ms,reason)` | reserve+evict | `false` no space | — | `CachePriorityGuard` W/R + `CacheStateGuard tryLockFor` | may evict | 013 |
| per-query holder | `:132` | `getQueryContextHolder(query_id,settings)` `FileCache.h:258` | creates/*joins* query context | — | `QueryContextHolderPtr` (unique_ptr) RAII | see D13 | none | 013 |
| skip-on-disk-failure flag | `:133` | `bool skipCacheOnDiskFailure()` `FileCache.h:124` | — | — | — | atomic | — | 013 |
| lock cache | `QueryLimit.cpp:164 [sweep]` | `CachePriorityGuard::WriteLock lockCache()` `FileCache.h:253` | — | — | — | top lock | — | 013 |

### D13 — `FileCacheQueryLimit` (013 `[INF]`)

| Behavior | Call-site | Signature (decl) | Transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Task |
|---|---|---|---|---|---|---|---|---|
| get-or-set query ctx | `FileCache.cpp getQueryContextHolder [sweep]` | `getOrSetQueryContext(id,settings,WriteLock&)` `QueryLimit.h:21` | insert into `query_map` | — | `QueryContextPtr` shared | **`query_map_mutex` leaf** guards map (cache locks alone don't — `QueryLimit.h:86–90`) | none | 013 |
| try-get (read) | `[sweep]` | `tryGetQueryContext(CacheStateGuard::Lock&)` `QueryLimit.h:19` | lookup | — | shared | read under `CacheStateGuard`+leaf mutex | none | 013 |
| last-holder remove | `~QueryContextHolder [sweep]` | `removeQueryContext(id,ctx,WriteLock&)` `QueryLimit.h:30` | removes map entry iff last holder; returns orphan for destruction **after** releasing cache lock | — | deferred destroy avoids holding cache lock | leaf mutex + `WriteLock` | none | 013 |
| per-(key,offset) reserve record | `[sweep]` | `QueryContext::add/remove/tryGet(...,WriteLock&)` `QueryLimit.h:45–59` | `Records` (`unordered_map<FileCacheKeyAndOffset,IteratorPtr>`) + inner `LRUFileCachePriority priority` | over-limit ⇒ reserve fails | `IteratorPtr` per record | `WriteLock` | none | 013 |

### D14 — `FileCacheFactory` (013/F-M `[INF]`)

| Behavior | Call-site | Signature (decl) | Transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Task |
|---|---|---|---|---|---|---|---|---|
| singleton | broad (`Context` `[sweep]`) | `static FileCacheFactory& instance()` `FileCacheFactory.h:42` | Meyer's singleton | — | process-lifetime | thread-safe static init | none | 013 |
| get-or-create by name | `[sweep]` | `getOrCreate(name,settings,config_path)` `FileCacheFactory.h:44` | insert-or-return in `caches_by_name` | — | returns shared `const FileCachePtr` (aliased across callers) | `std::mutex mutex` (`:70`) | none | 013 |
| get / getByName / getAll | `[sweep]` | `get(name)`, `getByName`, `getAll()` `:49,56,59` | lookup | `get` throws/`getByName` returns null `[sweep]` | shared | mutex | none | 013 |
| reload settings | `[sweep]` | `updateSettingsFromConfig(config)` `:63`; `FileCacheData::setSettings` (`settings_mutex` `:35`) | mutate per-cache settings | — | per-cache `mutable std::mutex settings_mutex` | mutex | none | 013 |

- **Manager decisions are port-only**; CH truth is exactly the above `FileCacheFactory` surface.
  `create()`/`remove()` (`:51,65`) exist but had no external caller in this sweep (see §7).

### D15 — `CachedOnDiskReadBufferFromFile` + `OpenedFileCache` (014)

Reader states `ReadType {CACHED, REMOTE_FS_READ_BYPASS_CACHE, REMOTE_FS_READ_AND_PUT_IN_CACHE, NONE}`
(`.h:67–73`). Members: `ReadInfo info` (holds `FileSegmentsHolderPtr file_segments` + reusable
`remote_file_reader`/`cache_file_reader`, `.h:107–130`), `state` (`ReadFromFileSegmentStatePtr`),
`file_offset_of_buffer_end`, `first_offset`, `skip_cache_on_disk_failure`. Its FileCache/FileSegment
calls are the D9/D12 rows above. Local cache reads use:

| Behavior | Call-site | Signature (decl) | Transition | Error behavior | Ownership/lifetime | Concurrency | Persistence | Task |
|---|---|---|---|---|---|---|---|---|
| share cache-file fd | reader `createReadBufferFromFileBase` `:362 [sweep]` | `OpenedFileCache::get(path,flags)` `OpenedFileCache.h:96` | weak_ptr `emplace`; hit→`lock()`, miss→open | — | `OpenedFilePtr` shared; **weak_ptr storage ⇒ auto-close on last release** via custom deleter erasing map entry (`.h:65–79`) | 1024 buckets `CityHash64%1024`, per-bucket `std::mutex` | opens `<off>_<size>` cache file O_RDONLY|O_CLOEXEC | 014 |
| drop fd | reader on segment complete `:895 [sweep]` | `OpenedFileCache::remove(path,flags)` `.h:103` | erase | — | — | bucket mutex | — | 014 |

- **Reader→FileCache selection:** `getDownloadedContiguousOrEmpty` (temp-cache-only, `:233,1818`),
  `get` (exists-else-bypass, `:246,1829`), `getOrSet` (normal, `:258,1839`).
- **Reconcile/partial-write (012 boundary):** on reserve/write failure mid-segment the reader
  `resetRemoteFileReader()` then `setDownloadFinishedWithoutContinuation()`/`completePart...`,
  reduces its right boundary, and continues from cache+remote; predownload fills the gap
  `[getCurrentWriteOffset, offset)` before reading (reader `:705–723,:841–851,:934–1193 [sweep]`).

---

## 3. CH structural baseline (§3 exact direct-translation)

### 3.0 Lock inventory & global ordering (`Guards.h`)

- **Order (Guards.h:53):** `CachePriorityGuard > CacheMetadataGuard > KeyGuard > FileSegmentGuard`.
- `CachePriorityGuard` = `SharedMutex` (`Guards.h:76–101`): `WriteLock`=`unique_lock` (structure
  mods: add/move/remove), `ReadLock`=`shared_lock` (iterate/collect). `try*Lock` variants.
- **`CacheStateGuard` = `std::timed_mutex` (Guards.h:104–132)** — **separate** guard for cache
  total size/element counters; `lock()`, `tryLock()`, **`tryLockFor(ms)`**. Taken *last*
  (step 4 of `tryReserve`, Guards.h:24–27) after successful reservation.
- `CacheMetadataGuard`,`KeyGuard`,`FileSegmentGuard` = plain `std::mutex` wrappers (`:137–174`);
  struct-wrapped `Lock` types are non-interchangeable by design (`:78–81`).
- `FileCache` holds **two** `CachePriorityGuard` (`cache_guard` `FileCache.h:356`, `queue_guard`
  `:357`) plus `cache_state_guard` (`:358`) and a `std::shared_timed_mutex dynamic_resize_lock`
  (`:345`). Port must preserve this split, not collapse to one lock.
- Infra swaps that are **1:1** (registered, not deviations): `SharedMutex→folly::SharedMutex`,
  `std::mutex→`semantic-preserving lock, `std::timed_mutex→`timed lock with `try_lock_for`.

### 3.1 `ConcurrentBoundedQueue` (003)
`Container=std::deque<T>` (`:19`); `std::mutex queue_mutex` + `push_condition`/`pop_condition`
(`:22–24`); `bool is_finished`, `size_t max_fill` (`:26,28`). `emplaceImpl<back>` predicate
`is_finished‖size<max_fill`; `popImpl<front>` predicate `is_finished‖!empty`; non-blocking
`tryPop(x)` takes no CV wait (`:165–180`). `finish`/`clearAndFinish(noexcept)` swap+notify_all.

### 3.2 `ShardedMap` + `MetadataBucket` (009 / 013)
- **`ShardedMap`** (`ShardedMap.h`): `using Map=std::unordered_map<Key,Value>` (`:24`,
  **node-address-stable**), `std::array<Shard,32>` each `mutable std::mutex mutex; Map map`
  (`:58–62,73`); size via `atomic total_count` maintained by `SCOPE_EXIT` delta (`:38,50,64–70`).
  Only `origins` uses it (`Metadata.h:290`).
- **`MetadataBucket`** (`Metadata.h:269–277`): `struct MetadataBucket : public
  std::unordered_map<FileCacheKey, KeyMetadataPtr>` with `mutable CacheMetadataGuard guard`;
  `MetadataBuckets = std::vector<MetadataBucket>{buckets_num}`. **Value is a shared_ptr**;
  `getKeyMetadata` returns `it->second` by copy (no `&value` escapes, `Metadata.cpp:332–353`).
  `IteratorImpl` stores a `MetadataBucket::iterator` across `next()` (`Metadata.cpp:483 [sweep]`)
  **but holds the bucket lock continuously while the iterator is live**, so node stability is only
  needed under a held lock (no concurrent rehash). See §3b.

### 3.3 Priority / Eviction (010)
- **`LRUFileCachePriority::queue`** = `LRUQueue` = **`std::list<EntryPtr>`** (`LRUFileCachePriority.h:175`)
  — **iterator/splice stable**. `LRUIterator` holds a `LRUQueue::iterator` (`:293`) + `weak_ptr<Entry>`
  (`:299`). Move = `queue.splice(...)` (`[sweep] cpp:655`), reorder = splice (`:865`), remove =
  `list.erase` (`:203`). Resumable **eviction cursors** `reserve_eviction_pos`/`background_eviction_pos`
  are `LRUQueue::iterator` guarded by `eviction_pos_mutex` (`.h:181–194`). Deferred cleanup:
  `std::deque<InvalidatedRef{weak_ptr<Entry>,LRUQueue::iterator}> invalidated_refs` guarded by
  `invalidated_mutex` + atomic `invalidated_count` (`.h:193–197`).
- **`SLRUFileCachePriority`**: two `LRUFileCachePriority` members `protected_queue`/`probationary_queue`
  (`.h:158–159`); `SLRUIterator` wraps an LRU iterator + `weak_ptr<Entry>` guarded by
  `entry_mutex`, with atomic `is_protected` (`.h:215–224`). Downgrade splices between the two lists
  and rolls back on `incrementSize` failure (`[sweep] cpp:504–543,601–613`).
- **`SplitFileCachePriority`**: `std::array<IFileCachePriorityPtr,3> priorities_holder` indexed by
  segment type (`.h:154`).
- **`EvictionCandidates`** (`EvictionCandidates.h:187–205`, verified): `candidates` =
  `absl::flat_hash_map<FileCacheKey,KeyCandidates,std::hash<FileCacheKey>>` (**not** ptr-stable;
  values are `std::vector<FileSegmentMetadataPtr>`); `original_queue_types` =
  `std::unordered_map<const FileSegmentMetadata*,QueueEntryType>` (raw-pointer keys, stable during
  eviction); `queue_entries_to_invalidate` = `std::vector<IteratorPtr>` (shared_ptr pins entries
  alive until finalize); `removed_queue_entries` bool; `hold_space`; `on_evict_callback`.
- **Locks:** structure mods under `CachePriorityGuard::WriteLock`; collection under `ReadLock`;
  counters under `CacheStateGuard`; `decrementSize`/`invalidate` are lock-free atomics.
- **Destruction/async:** LRU dtor manually reconciles `CurrentMetrics` for entries still queued
  (`[sweep] cpp:94–114`); `EvictionCandidates` dtor invalidates un-finalized entries / resets
  `Evicting` flags depending on `removed_queue_entries` (`[sweep] cpp:178–208`); entries survive
  eviction via `IteratorPtr` shared_ptr; `Entry` reaches key via `KeyMetadataWeakPtr` (throws if
  key gone, `IFileCachePriority.cpp:52–57 [sweep]`).

### 3.4 `FileSegment` (011)
- **State atomics:** `std::atomic<State> download_state` (`.h:302`),
  `std::atomic<size_t> downloaded_size/reserved_size` (invariant downloaded≤reserved, `:305–307`),
  `std::atomic<bool> size_in_filename` (false→true only, under guard, `:297–300`),
  `std::atomic<size_t> hits_count` (`:337`), `std::atomic_flag increasing_priority` (dedup, `:329`).
- **Lazy `DownloadState`** (`unique_ptr`, `:321`): `{downloader_id, remote_file_reader,
  cache_writer, write_mutex}` created when a downloader is assigned, freed at terminal state
  (`:309–320`) — an already-cached segment pays nothing.
- `mutable FileSegmentGuard segment_guard` (`:323`), `std::weak_ptr<KeyMetadata> key_metadata`
  (`:324`), `mutable Priority::IteratorPtr queue_iterator` (`:325`), raw `FileCache* cache` (`:326`),
  `std::condition_variable cv` (`:327`), `bool on_delayed_removal` guarded by segment_guard (`:340`).
- `~FileSegment` (`:67`); `FileSegmentsHolder` owns `FileSegments = std::list<FileSegmentPtr>`
  (`:378`), dtor completes remaining; `FileSegmentsHolderPtr = unique_ptr` (`:383`).

### 3.5 `Metadata` (013)
- `enum class KeyState : uint8_t {ACTIVE,REMOVING,REMOVED}` (`Metadata.h:117–122`), default `ACTIVE`;
  transitions `ACTIVE→REMOVING` (LockedKey dtor when empty), `REMOVING→ACTIVE` (removeFromCleanupQueue),
  `REMOVING→REMOVED` (cleanup thread `markAsRemoved`) (`[sweep] Metadata.cpp:1121–1143,644,772`).
- `struct KeyMetadata : private std::map<size_t, FileSegmentMetadataPtr>, private noncopyable,
  public enable_shared_from_this<KeyMetadata>` (`Metadata.h:98–100`) — **ordered offset map, stable
  iterators, `lower_bound` range queries**; `FileSegmentMetadata{file_segment(shared_ptr),
  removed(bool)}`, `releasable()=use_count==1` (`:42,88,92`).
- `LockedKey` members: `const shared_ptr<KeyMetadata> key_metadata; KeyGuard::Lock lock` with the
  contract comment **"`lock` must be destructed before `key_metadata`"** (`Metadata.h:428–429`).
- **`DownloadQueue`** (`Metadata.cpp:815–839`, verified): `std::queue<DownloadInfo> queue` +
  `std::mutex` + `condition_variable` + `atomic queue_size_limit` + `bool cancelled`; `DownloadInfo`
  keeps a **`std::weak_ptr<FileSegment>`** (segment may be removed+re-added before download starts).
- **`CleanupQueue`** (`Metadata.cpp:700–733`, verified): `std::unordered_set<FileCacheKey> keys` +
  `std::mutex` + `condition_variable` + `bool cancelled`; `add`→insert+`notify_one`;
  `cancel`→`notify_all`.
- **Members/destruction (`Metadata.h:256–300`):** `cleanup_queue`/`download_queue` (shared_ptr),
  `metadata_buckets` (vector), `origins` (`ShardedMap`), `download_threads`
  (`vector<shared_ptr<DownloadThread>>`), `cleanup_thread` (`unique_ptr<ThreadFromGlobalPool>`).
  **Hard rule:** `shutdown()` cancels queues + joins threads and **must run before the default
  `~CacheMetadata`** (`[sweep] Metadata.cpp:1030–1042`) or workers touch destroyed state.
- **Async lifetime:** `FileSegment` holds `weak_ptr<KeyMetadata>`; `getKeyMetadata` throws
  `LOGICAL_ERROR` if expired (`[sweep] FileSegment.cpp:603–610`); background download pins the
  segment via a local `FileSegmentsHolder`, not the key.

### 3.6 `FileCache` (013)
- **Member/destruction order is contractual:** `main_priority` (`FileCache.h:351`) then
  `CacheMetadata metadata` (`:355`) — comment: *metadata holds iterators referencing priority state,
  so metadata must be destroyed first* (`:353–354`). Guards `cache_guard`/`queue_guard`/
  `cache_state_guard` (`:356–358`).
- Background machinery: `keep_up_free_space_ratio_task` + `background_cleanup_task`
  (`BackgroundSchedulePoolTaskHolder`, `:299,311`), `eviction_pool` (`unique_ptr<ThreadPool>`,
  `:307`, lock-free removers), `status_file` (`unique_ptr<StatusFile>`, `:343`),
  `load_metadata_main_thread` (`:294`). `FileCacheReserveStat` tracks releasable/non-releasable/
  evicting/moving/invalidated size+count per `FileSegmentKind` (`:37–94`).

### 3.7 `QueryLimit` / `Factory` / `OpenedFileCache` / Writer
- **QueryLimit:** `QueryContextMap = unordered_map<String,QueryContextPtr> query_map` guarded by a
  dedicated **leaf** `query_map_mutex` (`QueryLimit.h:84–90`) because the map is reached under both
  `CacheStateGuard` (reads) and `CachePriorityGuard` (writes). Inner `QueryContext::Records =
  unordered_map<FileCacheKeyAndOffset,IteratorPtr,FileCacheKeyAndOffsetHash>` + inner
  `LRUFileCachePriority priority` (`:62–64`). `QueryContextHolder` RAII (`:68–80`).
- **Factory:** `caches_by_name = unordered_map<string,FileCacheDataPtr>` + `std::mutex mutex`
  (`:39,70–71`); `FileCacheData{const FileCachePtr cache; const string config_path; FileCacheSettings
  settings; mutable std::mutex settings_mutex}` (`:20–36`).
- **OpenedFileCache:** singleton; `impls = VectorWithMemoryTracking<OpenedFileMap>{1024}`; each
  `OpenedFileMap` = `MapWithMemoryTracking<pair<path,flags>, weak_ptr<OpenedFile>>` + `std::mutex`
  (`.h:37–43,90–91`); bucket = `CityHash64(path)%1024`; **weak_ptr storage + custom deleter ⇒
  auto-close on last release** (`.h:65–79`).
- **Writer:** `WriteBufferToFileSegment{FileSegment* file_segment; FileSegmentsHolderPtr
  segment_holder; size_t reserve_space_lock_wait_timeout_ms; size_t written_bytes}` (`.h:48–54`).

### 3b. Structure-deviation ledger (§3 — guarantee-changing swaps)

| CH structure | Velox replacement | Guarantee difference | Hard-constraint source | E-probe evidence | Human sign-off |
|---|---|---|---|---|---|
| `ShardedMap::Map = std::unordered_map` (`ShardedMap.h:24`) **and/or** `MetadataBucket : std::unordered_map` (`Metadata.h:269`) | `folly::F14*Map` (per guide §3 / F2, 009) | node-address stability lost: F14 rehash **moves values** and invalidates iterators | **NOT hard-constraint** — `std::unordered_map` compiles under Velox (guide §3 lines 82–85) | *N/A (CH side)* — latent-safety analysis below | **REQUIRED, not yet on record** — must be registered + signed, else default back to `std::unordered_map` |

- **Latent-safety analysis (why CH consumers don't currently break under F14):** map **values are
  `KeyMetadataPtr`/`OriginInfoPtr` shared_ptrs copied out** (`Metadata.cpp:332–353`), so no consumer
  holds `&map[key]`; and the one iterator-across-calls user (`IteratorImpl.key_it`) holds the bucket
  **`CacheMetadataGuard` continuously while the iterator is live** (`[sweep] Metadata.cpp:424–477`),
  so no rehash occurs mid-iteration. **The deviation is therefore latent, not benign** — it silently
  removes a guarantee (iterator/value stability) that a future consumer could rely on; per §3 it is a
  registered-and-signed deviation, not a default-allowed infra swap. This is exactly the 009-F2
  cross-task hazard the guide calls out.
- No other guarantee-changing container swap is *provable* from CH alone (the `std::list` priority
  queues, `std::map` offset maps, `std::deque`, `absl::flat_hash_map` in EvictionCandidates are CH's
  own choices; whether the port preserved them is a Velox-side check outside Phase A).

---

## 4. Bidirectional coverage matrix

### 4a. Call-site → contract → owner-task (consumer direction)

| Call-site (file:line) | Requires behavior | Owner task |
|---|---|---|
| `FileCache.cpp:1863` | timed `tryPush` | 003 |
| `FileCache.cpp:1721` | non-blocking `tryPop` + blocking `pop` | 003 |
| `FileCache.cpp:1774,1792,1895` | `push`,`finish` | 003 |
| `FileCache.cpp:2345,2282,2360` | default `tryPush`,`pop`,`finish` (loader) | 003 |
| `FileCache.cpp:517` + `StatusFile.cpp:38–43,109–116` | `write_full_info` 3-line text; dtor close→unlink | 004 |
| `FileCache.cpp:1758,1899` | `ThreadPool` schedule/wait | 005 |
| `Metadata.cpp` threads + `Metadata.cpp:1030–1042` | join-before-destroy | 005 |
| `FileCache.cpp:592,1955,2925` vs `:563,1641,1957` | `schedule` (immediate) ≻ `scheduleAfter` (delayed) | 006 |
| `FileCache.cpp:574–575,2674–2678` | `deactivate` drains running tick | 006 |
| `WriteBufferToFileSegment.cpp:63,84,109,71–76` | already-open append relay | 007 |
| reader `:1114,:1616,:1024–1025,:1174–1175,:1740–1741` | reserve/write reconcile, reset-before-complete ordering | 012 |
| broad `fromKeyString` + `roundUpToMultiple` | key length validation + checked round-up message | 008 |
| `Metadata.cpp` origins `withShard` | sharded find/emplace, exception-safe size | 009 |
| `FileCache.cpp:1827,1734,1737,1767` | collect/evict/afterEvictWrite/afterEvictState | 010 |
| `FileSegment.cpp:1441` ← reader `:906` | increasePriority (splice / SLRU move) | 010 |
| reader `:689,667,1114,1272,1175,1025,521,537,1024,898` | FileSegment downloader/reserve/write/complete/reader-reset/holder-complete | 011 |
| reader `:132` | `getQueryContextHolder` | 013 |
| reader `:258,246,233` / `:1839,1829,1818` | `getOrSet`/`get`/`getDownloadedContiguousOrEmpty` | 013 |
| `QueryLimit` add/remove/tryGet | per-query record accounting under leaf mutex | 013 |
| `Context` `[sweep]` | `FileCacheFactory::instance/getOrCreate/get` | 013/F-M |
| reader `:362,895` | `OpenedFileCache::get/remove` fd sharing | 014 |

### 4b. Owner-task → contracts → call-sites (ownership direction; gate check)

| Task | Owned contracts (this ledger) | Real call-site(s) present? |
|---|---|---|
| 003 | D1 rows (7) | ✅ `FileCache.cpp:1704–1905,2258–2379` |
| 004 | D2 rows (3) | ✅ `FileCache.cpp:517` (+ dtor on cache teardown) |
| 005 | D3 rows (3) | ✅ eviction_pool / metadata threads / loader |
| 006 | D4 rows (5) | ✅ `FileCache.cpp:559–592,1641,1955–1957,2674–2925` |
| 007 | D5 already-open-append rows | ✅ `WriteBufferToFileSegment.cpp:63–109` |
| 008 | D6 rows (6) | ✅ `fromKeyString`, `roundUpToMultiple` callers |
| 009 | D7 rows (3) | ✅ `Metadata` origins (`getOrCreateSharedOrigin`) |
| 010 `[INF]` | D8 rows (8) | ✅ `FileCache.cpp` reserve/eviction/resize |
| 011 `[INF]` | D9 rows (10) | ✅ reader + writer + cache internals |
| 012 | D10 reconcile/resume rows | ✅ reader `:1114–1193,1610–1741`; CoDWBFF |
| 013 `[INF]` | D11–D14 rows | ✅ reader + Context + internal |
| 014 | D15 rows | ✅ reader end-to-end + OpenedFileCache |

Both directions close with **no unmatched cell** among mapped rows; residual gate findings (behavior
without call-site, or call-site without owning behavior) are in §7.

---

## 5. CH tests / scenarios (secondary evidence + consumer ownership)

Tests are **corroborating evidence only** (guide §C: oracle = source, not tests). They also indicate
*consumer ownership* (who exercises a contract end-to-end).

- **Unit (gtest):** `src/Interpreters/tests/gtest_filecache.cpp` (~169 KB) —
  `FileCacheTest.LRUPolicy` (`:409`), `SLRUPolicy` (`:1831`, promotion `:1885`, queue-type asserts
  `:227–228`), `SLRUDynamicResizeCorrectEviction` (`:2048`); friend `FileCacheTest_MoveEvictionPos_Test`
  (`LRUFileCachePriority.h:171`) exercises eviction-cursor advance. Priority is tested **through**
  `FileSegment::increasePriority` (`:332`), not the raw priority API. `src/Interpreters/tests/
  gtest_file_cache_utils.cpp` — utils/`ShardedMap`-area tests. **No dedicated `Metadata`/`ShardedMap`
  node-stability or concurrent-iterator test exists** (a gap for §3b confidence).
- **Fault-injection oracles (false-green style):** `file_cache_slru_downgrade_fail_before_finalize`
  (`[sweep] SLRUFileCachePriority.cpp:584`), `file_cache_dynamic_resize_fail_to_evict`
  (`[sweep] EvictionCandidates.cpp:310`), `file_cache_modify_size_limits_fail`
  (`[sweep] SLRUFileCachePriority.cpp:844`), `file_cache_background_eviction_push_fail`
  (`FileCache.cpp:1860`), `file_segment_range_writer_partial_write_then_network_error`
  (`[sweep] CachedOnDiskWriteBufferFromFile.cpp:214`).
- **Stateless (`tests/queries/0_stateless/`):** `02226_filesystem_cache_profile_events`,
  `02240_filesystem_cache_bypass_cache_threshold`, `02240/02242_system_filesystem_cache*`,
  `02241_filesystem_cache_on_write_operations`, `02286/02808_drop_filesystem_cache*`,
  `02313_filesystem_cache_seeks` (reader/seek), `02337_drop_filesystem_cache_access`,
  `02536_system_sync_file_cache` (`sync()`), `02789_filesystem_cache_alignment` (boundary_alignment),
  `02842_filesystem_cache_validate_path` (key/path validation → 008),
  `02908_filesystem_cache_as_collection`, `02944`/`03032_dynamically_resize_filesystem_cache*`.
- **Ownership signal:** the reader (014) is the only end-to-end exerciser of the D9/D11/D12 relay;
  write-path tests (`02241`) own the 012 reconcile behavior; alignment/validate own 008.

---

## 6. E-candidates (Velox primitive semantics not knowable from CH source)

These contract rows depend on an underlying primitive's **undocumented failure/边界 semantic**;
per §E each needs a ~20-line probe before the consuming task, not 6 reopen rounds.

| # | Contract needing it | CH anchor | Unknowable semantic |
|---|---|---|---|
| E1 | writer partial-write reconcile (012) | `WriteBufferToFileSegment.cpp:109`, `CachedOnDiskWriteBufferFromFile.cpp:214` | Does the Velox write primitive expose `errno`/partial-count on failure? CH assumes all-or-nothing `write`; reconcile must otherwise be by **physical file size** (the canonical guide-§E case). |
| E2 | disk-full skip-cache path | `CachedOnDiskWriteBufferFromFile.cpp:512–520` | `errno 28/122` (ENOSPC/EDQUOT) are Linux values; the Velox write primitive may not surface them. |
| E3 | zero-copy reader reuse invariant | `FileSegment.h:226–227`; reader `:1435,:1475 [sweep]` | Does the Velox read buffer honor `set(buffer)` (no internal read-ahead) so `getFileOffsetOfBufferEnd()==getCurrentWriteOffset()` holds? |
| E4 | fd sharing across rename | `OpenedFileCache.h:96`; reader `:322–330,:372–394 [sweep]` | Is the shared fd inode-based (survives `<off>`→`<off>_<size>` rename) and safe for concurrent `pread`? |
| E5 | `CacheStateGuard::tryLockFor` timeout accuracy | `Guards.h:124–128`; `FileCache.cpp:1809` | `std::timed_mutex` `try_lock_for` fairness/granularity → reserve/free-space timeout behavior. |
| E6 | `CachePriorityGuard` read/write fairness | `Guards.h:76–101`; collect under ReadLock `[sweep]` | `SharedMutex` fairness: can long eviction ReadLocks starve reservation WriteLocks? |
| E7 | remote-read EOF vs truncation | reader `:1577,:1722–1752 [sweep]` | Does the Velox remote buffer return `0` on truncation (so size-check disambiguates) or throw first? |
| E8 | `reserve_hint` interpretation | `FileSegment.h:213–221`; reader `:1114,1616` | Only relevant if the port re-implements `reserve`; hint must bound reserve-ahead, not be treated as an absolute offset. |
| E9 | `std::condition_variable` wakeup granularity | `FileSegment.cpp:552 wait`; `Metadata.cpp:749,857` | Per-write vs per-state-transition wakeups (predicate-guarded, so correctness-safe; latency-only). |

---

## 7. Over-port candidates & ownership holes (both gates)

### 7a. Over-port (behavior with **no** in-scope call-site → delete unless a caller is proven)

- **`StatusFile::write_pid`** (`StatusFile.cpp:33`) — cache uses only `write_full_info`; `write_pid`
  is over-port *for the FileCache scope*.
- **`FileCacheFactory::create` / `remove`** (`FileCacheFactory.h:51,65`) — no external caller found
  in sweep (`getOrCreate`/`get` cover the API). Confirm before porting.
- **Overcommit/per-client priority extension points**: `getUsageStatPerClient`
  (`IFileCachePriority.h:397`, base throws `NOT_IMPLEMENTED`), `touchClientAccess` (`:460` no-op),
  `collectIdleClients` (`:463` no-op), `getHoldSize/getHoldElements` (`:452,454`) — only meaningful
  in the Overcommit priority (out of the LRU/SLRU/Split scope). Over-port **unless** the accepted
  tasks include the Overcommit policy.
- **`EvictionCandidates::bytes()` / `requiresAfterEvictWrite()` / `requiresAfterEvictState()` /
  `getOriginalQueueType()`** (`EvictionCandidates.h:126,148,151,180`) — no consumer in sweep
  (`size()` is used; `afterEvictWrite` is always called unconditionally; `getOriginalQueueType`
  populated but never read because dynamic resize throws on failure rather than restoring).
- **`IFileCachePriority::Iterator::isValid` / `Entry::getKeyMetadata` / `getSLRUSizeRatio`**
  (`:184,72,240`), **`KeyMetadata::getState`** (`Metadata.h:156`, superseded by
  `LockedKey::getKeyState`), **`LockedKey::isLastOwnerOfFileSegment` / `hasIntersectingRange`**
  (`Metadata.h:409,411`) — no in-sweep caller; likely internal/introspection. Verify before porting.
- **`WriteBufferToFileSegment::sync` / `getFileName`** and the `IFilesystemCacheWriteBuffer` virtuals
  are polymorphic-interface methods; not over-port if invoked through the base interface.

> Gate note: this sweep's grep scope was the in-scope files + obvious callers; several of the above
> may have callers in modules not opened (system tables, `Context`, introspection). They are flagged
> **candidates**, not confirmed dead code — the D-phase full sweep must resolve each to accept/reopen.

### 7b. Ownership holes (call-site with **no** clearly-owning method → stop and fill)

- **007↔012 boundary:** the reader's reserve/write-failure reconcile
  (`resetRemoteFileReader`→`setDownloadFinishedWithoutContinuation`, reader `:1024–1025,1740–1741`)
  must be owned by **012**, not silently folded into 007's already-open-append. Verify no task
  absorbed the other's rows.
- **`resetRemoteFileReader`-before-`completePartAndResetDownloader` ordering** (reader
  `:1016–1025,1168–1175,1733–1741,1877–1884`) is a **cross-method invariant** spanning FileSegment
  (011) and the reader (014); it needs an explicit owner + RED test (race: another thread
  `extractRemoteFileReader`s a still-borrowed reader). Currently an implicit contract in comments.
- **`shutdown()` before `~CacheMetadata`** (`Metadata.cpp:1030–1042`) is a lifecycle contract owned
  jointly by 013 (Metadata) and 005 (threads); must be a named precondition, not left to the default
  dtor.
- **`OriginInfo::operator==` vs `OriginPoolKey::operator==` mismatch** (`FileCacheOriginInfo.h:36`
  compares only `user_id`; pool key compares all fields) — a latent semantic hole; likely
  intentional (user access vs dedup key) but should be explicitly owned/documented.
- **`OpenedFileCache` eviction/lifetime** (weak_ptr auto-close vs reader holding the shared_ptr) is a
  reader-side (014) contract with no explicit owner method; define fd lifetime = reader lifetime.
- **`FileCacheFactory` returns aliased `const FileCachePtr`** shared across callers — the port-side
  "Manager" must not duplicate instances; but this is a *port-only* decision with no CH method to
  port beyond the shared-pointer aliasing already in `getOrCreate`.

---

## 8. Self-check

- **Coverage direction 1 (call-site → contract):** every real call-site enumerated in §2/§4a maps to
  a contract row with an owner task. ✅
- **Coverage direction 2 (contract → call-site / task → contract):** every contract row and every
  owner task in §4b has ≥1 real CH call-site; the only rows lacking a caller are explicitly listed as
  **over-port candidates** in §7a (not silently kept). ✅
- **Structure citations:** every §3 structural claim carries a CH `file:line`. Personally opened &
  verified: `Guards.h`, `ShardedMap.h`, `FileCacheUtils.h`, `FileCacheKey.{h,cpp}`, `StatusFile.cpp`,
  `ConcurrentBoundedQueue.h`, `FileSegment.h`, `FileSegmentInfo.h`, `FileCache.h`,
  `FileCache.cpp:1695–1905`, `EvictionCandidates.h:180–205`, `Metadata.h:95–100/265–277/425–432`,
  `Metadata.cpp:700–839`, the four consumer headers, and the scheduler/reader/writer/transition
  greps. Lines tagged `[sweep]` (a subset of `LRU/SLRU/EvictionCandidates/Metadata/FileSegment` and
  reader `.cpp` internals) come from the structural sweep and were each cross-checked against a
  verified header; a D-phase reader should re-open them if used as a RED-test oracle. ✅
- **§3 deviation gate:** the single guarantee-changing swap (009 `unordered_map`→`F14`) is registered
  in §3b as **requiring human sign-off**; it is latent-but-real, matching the guide's F2 hazard. ✅

### Concerns / caveats

1. **Task tags 010/011/013 and the core/factory/query-limit split are inferred** (guide pins only
   003–009 + the 012/014 hints; port files were not inspected). The CH contracts stand on their
   citations regardless; only the *labels* are provisional and should be reconciled against the
   accepted-task scopes in the D-phase.
2. **`[sweep]`-tagged `.cpp` lines** were not all personally re-opened; treated as corroborated (all
   agreed with verified headers, and the four spot-checks I ran — EvictionCandidates containers,
   Metadata queues, LRU queue members, LockedKey member order — were exact).
3. **Over-port list is candidate-level** — grep scope did not include every CH module
   (system tables, `Context`, introspection); each candidate must be resolved to accept/delete in the
   D full-review, not deleted on this ledger alone.
4. **This is a customized FileCache** (Overcommit/per-client, eviction cursors, deferred invalidation,
   dynamic resize, split cache, `CacheStateGuard`): the port's direct-translation baseline is *this*
   structure, and any accepted task that ported the *mainline* shape is a structural drift to flag.
5. **E1 (physical-size reconcile) is the irreducible one** (guide §E): it cannot be closed by
   documentation and needs a primitive probe before the 012 write/reconcile work is validated.

## 9. Controller A-gate resolution

```text
controller_status: accepted_for_D
```

- All 180 parsed CH citations resolve after expanding four local abbreviations;
  no cited line is out of range.
- Ownership labels are corrected for the accepted port: priority/eviction is
  Task 011; `FileSegment`/`Metadata`/`FileCache`/`QueryLimit` are Task 012;
  Factory/Manager/`OpenedFileCache` are Task 013; buffered input is Task 014.
- The CH contracts remain authoritative even where the Phase-A task label was
  provisional.
- E1/E2 are closed on the consumer side by the typed-errno tests and prior
  real-file probes; the real structured-errno producer remains a pre-release
  gate.
- E3 is closed by the corrected Task-007/014 detach, lazy-restore,
  release-owned-buffer, and background-continuation tests.
- SD1 has explicit user sign-off in the cross-profile Round-1 decisions.
- Over-port entries remain candidates for D to resolve against the actual Velox
  implementation and accepted task scopes; the Controller does not delete them
  from Phase-A evidence.
