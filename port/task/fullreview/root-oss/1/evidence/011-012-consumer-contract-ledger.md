# Consumer Contract Ledger — Tasks 003-010 Dependencies Reachable from the FileCache Center-SCC (Tasks 011/012) — `root-oss` Review 1 Evidence

## Provenance

- Agent: Phase-A contract-recovery agent (single agent, fresh context).
- ClickHouse frozen baseline commit: `da28e83e8b3cb69090624b0a0b1f13cd78c13279` (branch `ch-filecache`).
  - Verified: current HEAD `d29cdd2ca71cad7fcef34d773dc83f7fca166fd7` is a documentation-only
    descendant; `git diff da28e83e HEAD -- src/` is empty (0 lines), and the frozen commit is an
    ancestor of HEAD. `src/` is byte-for-byte the frozen baseline.
- Execution timestamp (UTC): 2026-07-19T12:31:50Z.
- Method: top-down. Contracts are derived only from CH source and its real callers inside the
  center-SCC (Task 011 priority/eviction + Task 012 core files). No leaf-header-only inference.
- Independence constraints honored:
  - Velox repository (`/root/oss/velox`) was **not** inspected.
  - Accepted port receipts and Velox tests were **not** inspected.
  - Tasks 011/012 text was used only to bound the source set (which files 011/012 port) and to
    read the intended priority/eviction + center-SCC scope, never as contract authority.
  - No Git repository was modified, staged, committed, or pushed. Read-only.

## 0. Scope and the "center-SCC" consumer set

The center-SCC is the mutually dependent compile/link closure that Tasks 011 and 012 port. These
are the **consumers** whose real call sites define the contracts below.

Task 011 (priority/eviction) source files:
- `CacheUsage.h`
- `IFileCachePriority.h` / `.cpp`
- `LRUFileCachePriority.h` / `.cpp`
- `SLRUFileCachePriority.h` / `.cpp`
- `SplitFileCachePriority.h` / `.cpp`
- `EvictionCandidates.h` / `.cpp`

Task 012 (center SCC) source files:
- `FileSegmentInfo.h`
- `FileSegment.h` / `.cpp`
- `Metadata.h` / `.cpp` (owns `CleanupQueue`, `DownloadQueue`)
- `FileCache.h` / `.cpp`
- `QueryLimit.h` / `.cpp`

Explicit consumer-side exclusions (per Task 011 text, used only to bound source set):
`OvercommitFileCachePriority` and cloud-only distributed-cache branches are **not** ported. Their
absence changes the reachable surface of `CacheUsage` / `QueryLimit` idle-client machinery — see the
over-port notes in Section 6.

## Owner-task map (Tasks 003-010)

Derived from the `## File scope` / `Create:` declarations of Tasks 003-010 and the predecessor list
in Task 012 (`012-filecache-core-scc.md:139-147`). Ownership is a scope fact, not a contract fact.

| Owner task | Owns (CH names as they appear to the center-SCC) |
|---|---|
| 003 | `String`/`UInt*` aliases, `Exception`/`ErrorCodes`/`chassert`, `ConcurrentBoundedQueue` (`FileCacheBoundedQueue`), `CurrentMetrics`, `ProfileEvents`, `LoggerPtr`/`LOG_*`, `callOnce`/`OnceFlag`, `Memory<>`, `getThreadId`/`getThreadName`, `isSharedPtrUnique`, `SharedMutex`, `Stopwatch`, `SCOPE_EXIT`, `fmt`, `fiu`/fail-points, `Context::getGlobalContextInstance` shim, `DBMS_DEFAULT_BUFFER_SIZE` |
| 004 | `StatusFile`, `Guards.h` (`CachePriorityGuard`, `CacheStateGuard`, `CacheMetadataGuard`, `KeyGuard`, `FileSegmentGuard`) |
| 005 | `ThreadFromGlobalPool`, `ThreadPool` (`FileCacheWorkerPool`/`FileCacheThreadPool`) |
| 006 | `BackgroundSchedulePool`/`BackgroundSchedulePoolTaskHolder` (`FileCacheScheduler`), `FileCacheQueryIdScope`/`CurrentThread` query-id access |
| 007 | `ReadBufferFromFileBase` (`ReadBufferFromVeloxReadFile`), `WriteBufferFromFile` (`WriteBufferFromVeloxWriteFile`) |
| 008 | `FileCacheKey`, `SipHash128`, `FileSegmentKeyType`, `FileCacheOriginInfo`/`OriginPoolKey`/`OriginPoolKeyHash`, `FileCache_fwd.h` (policy enum + default constants + ownership aliases), `FileCache_fwd_internal.h`, `FileCacheUtils.h`, `FileCacheKeyAndOffset`/`FileCacheKeyAndOffsetHash` |
| 009 | `ShardedMap` (`FileCacheUtils::ShardedMap`) |
| 010 | `FileCacheSettings`/`FileCacheConfig`, `FileCacheReadOptions`/`ReadSettings`, `FilesystemCacheSettings` |

## 1. Reachable dependency inventory

"Reachable" = at least one real call site in the center-SCC consumer set. Each row is expanded into
a contract table in Section 2 and mapped in the Section 4 matrix.

| # | Dependency | Owner | First / representative entry call site (file:line) | Reachable? |
|---|---|---|---|---|
| D1 | `ConcurrentBoundedQueue<T>` overload set | 003 | `FileCache.cpp:1704`, `1721`, `1763`, `1774`, `1863`, `2260`, `2282`, `2345` | yes |
| D2 | `Exception` + `ErrorCodes::*` + `chassert`/`chassert(msg)` | 003 | `EvictionCandidates.cpp:296`; `Metadata.cpp:71`; `FileSegment.cpp:441` | yes |
| D3 | `Memory<>` + `DBMS_DEFAULT_BUFFER_SIZE` | 003 | `Metadata.cpp:939`, `963` | yes |
| D4 | `LoggerPtr`/`getLogger`/`LOG_*` (TRACE/DEBUG/INFO/WARNING/TEST) | 003 | `FileCache.cpp:304`, `597`, `1821`; `FileSegment.cpp:568` | yes |
| D5 | `ProfileEvents` (42 distinct) + `CurrentThread::getProfileEvents().timer` | 003 | `EvictionCandidates.cpp:267`; `FileCache.cpp:1747` | yes |
| D6 | `CurrentMetrics` (18 distinct) + `CurrentMetrics::Increment`/`add`/`sub` | 003 | `Metadata.cpp:715`, `759`, `799`; `CacheUsage.h:116` | yes |
| D7 | `String`/`UInt*`/`UInt128` aliases | 003 | pervasive; `FileCache.h:101`, `FileCacheKey.h` | yes |
| D8 | `callOnce`/`OnceFlag` | 003 | `FileCache.h:341` (`initialize_called`) | yes |
| D9 | `SCOPE_EXIT` | 003 | `FileSegment.cpp:494`; `Metadata.cpp:1054` | yes |
| D10 | fail-point macros (`fiu_do_on`/`FailPoints`) | 003 | `FileSegment.cpp:486`; `FileCache.cpp:1860`; `EvictionCandidates.cpp:310` | yes |
| D11 | `Stopwatch` | 003 | `FileCache.cpp:1923` | yes |
| D12 | `getThreadId`/`getThreadName` | 003/006 | `FileSegment.cpp:257`, `259` | yes |
| D13 | `isSharedPtrUnique` | 003 | `Metadata.h:42` (`FileSegmentMetadata::releasable`) | yes |
| D14 | `StatusFile` (+ `write_full_info`) | 004 | `FileCache.cpp:517` | yes |
| D15 | `CachePriorityGuard` (Read/Write shared lock) | 004 | `FileCache.cpp:600`, `1733`, `1843`; `Guards.h:76` | yes |
| D16 | `CacheStateGuard` (`std::timed_mutex`, `tryLockFor`) | 004 | `FileCache.cpp:1737`, `1809`; `Guards.h:104` | yes |
| D17 | `CacheMetadataGuard` (`std::mutex`) | 004 | `Metadata.cpp:765`; `Metadata.h:271` | yes |
| D18 | `KeyGuard` (`std::mutex`) | 004 | `Metadata.h:162`, `429` | yes |
| D19 | `FileSegmentGuard` (`std::mutex`) | 004 | `FileSegment.cpp:506`; `FileSegment.h:323` | yes |
| D20 | `ThreadFromGlobalPool` (ctor-from-callable, `join`, `joinable`) | 005 | `FileCache.cpp:534`, `2302`, `2374`, `2390`; `Metadata.cpp:1024`, `1027` | yes |
| D21 | `ThreadPool` (`scheduleOrThrowOnError`, `wait`, ctor w/ metrics+limits) | 005 | `FileCache.cpp:583`, `1758`, `1899` | yes |
| D22 | `BackgroundSchedulePool`/`TaskHolder` (`createTask`/`schedule`/`scheduleAfter`/`deactivate`) | 006 | `FileCache.cpp:559`, `562`, `563`, `575`, `591`, `592`, `1641` | yes |
| D23 | `CurrentThread` query-id (`isInitialized`/`getQueryId`/`tryGetQueryContext`) + `getCallerId` format | 006 | `FileSegment.cpp:254-259`, `581`; `QueryLimit.cpp:17-19`, `28` | yes |
| D24 | `WriteBufferFromFile` (`set`/`next`/`finalize`/`cancel`, append flags) | 007 | `FileSegment.cpp:483`, `492`, `494`, `496`, `728`, `846`, `1080` | yes |
| D25 | `ReadBufferFromFileBase` remote reader (get/set/extract/reset) | 007 | `FileSegment.cpp:370-412`; `Metadata.cpp:963-988` | yes |
| D26 | `FileCacheKey` (`fromKeyString` parser, `toString`, `fromPath`, `fromKey`) | 008 | `FileCache.cpp:2416`; `FileCacheKey.cpp:41`, `21`, `33` | yes |
| D27 | `SipHash128` (`sipHash128`) | 008 | `FileCacheKey.cpp:33` | yes |
| D28 | `FileSegmentKeyType` (`Data`/`System`/`General`, prefix/toString) | 008 | `FileCache.cpp:429`, `440`, `2196`, `2232` | yes |
| D29 | `FileCacheOriginInfo`/`OriginPoolKey`/`OriginPoolKeyHash`/`UserID` | 008 | `Metadata.cpp:108`; `FileCache.h:106`; `Metadata.h:290` | yes |
| D30 | `FileCache_fwd.h` (policy enum + `FILECACHE_DEFAULT_*` + ownership aliases) | 008 | `FileCacheSettings.cpp:38-47`; `FileCache.cpp:312` | yes |
| D31 | `FileCache_fwd_internal.h` (`KeyMetadataPtr`, `FileSegmentPtr`, `FileSegments`, `LockedKeyPtr`, `FileCachePriorityPtr`, `FileSegmentMetadataPtr`) | 008 | `IFileCachePriority.h:7`; `FileSegment.h:15`; `Metadata.h:10` | yes |
| D32 | `FileCacheUtils::roundUpToMultiple`/`roundDownToMultiple` | 008 | `FileCache.cpp:972`, `974`, `975` | yes |
| D33 | `FileCacheKeyAndOffset`/`FileCacheKeyAndOffsetHash` | 008 | `QueryLimit.cpp:104`, `120`; `QueryLimit.h:62` | yes |
| D34 | `FileCacheUtils::ShardedMap<K,V,Hash>` (`withShard`, `forEachShard`, ctor w/ lock-wait event) | 009 | `Metadata.cpp:108`, `121`; `Metadata.h:290`; `CacheUsage.h:121` | yes |
| D35 | `FileCacheSettings`/`FileCacheConfig` field access (`settings[FileCacheSetting::x]`) | 010 | `FileCache.cpp:277-309`, `312`, `385-388` | yes |
| D36 | `FilesystemCacheSettings` query-limit fields (`max_download_size_per_query`, `skip_download_if_exceeds_per_query_cache_write_limit`) | 010 | `QueryLimit.cpp:82-83` | yes |
| D37 | `ReadSettings`/`FileCacheReadOptions` | 010 | `QueryLimit.h:9`; `FileCache.h:32` (fwd; see over-port note O5) | partial |

## 2. Per-call-site contract tables

Columns: **Behavior** | **Call site (file:line)** | **Signature / overload** | **State transition** |
**Error behavior** | **Ownership / lifetime** | **Concurrency** | **Persistence** | **Owner**.
"Allowed Velox substitution" is folded into the notes under each table (it is a mapping hint, not a
contract fact). Every row is anchored to a real consumer call site; behaviors with no call site are
NOT listed here — they appear in Section 6 (over-port candidates).

### Task 003 — basic common shims

The historically-reopened Task 003 gap was the **queue overload set** exposed by the real
`FileCache` callers (timed `tryPush`, non-blocking `tryPop`). At the frozen baseline the only
`ConcurrentBoundedQueue` consumers in the center-SCC are in `FileCache.cpp` (background eviction
pipeline + metadata loading); `Metadata.cpp`'s `CleanupQueue`/`DownloadQueue` are hand-rolled
`std::unordered_set`/`std::queue` + `condition_variable` and do **not** use the bounded queue
(`Metadata.cpp:729-734`, `806-834`).

| Behavior | Call site | Signature / overload | State transition | Error behavior | Ownership / lifetime | Concurrency | Persistence | Owner |
|---|---|---|---|---|---|---|---|---|
| Construct bounded queue with fixed capacity; capacity 0 is valid (every push fails immediately) | `FileCache.cpp:1704-1705`, `2260` | `ConcurrentBoundedQueue<T>(size_t capacity)` | empty→open | none (ctor) | stack-local, joined before scope exit | MPMC | in-memory | 003 |
| Blocking push | `FileCache.cpp:1774` | `bool push(T&&)` — blocks until room or finished; returns false iff finished | open→(item enqueued) | returns false on finished; never throws | value moved in | wakes one popper | in-memory | 003 |
| Non-blocking push (no timeout) | `FileCache.cpp:2345` | `bool tryPush(T)` — returns false immediately if full **or** finished | open unchanged if full | false if full/finished; caller then loads inline | copy/move in | — | in-memory | 003 |
| **Timed** push | `FileCache.cpp:1863` | `bool tryPush(T, size_t milliseconds)` — waits up to N ms for room | open→enqueued or timeout | false on timeout/finished; caller drains + retries (`max_push_attempts`) | move in | blocks bounded time | in-memory | 003 |
| Blocking pop | `FileCache.cpp:1763`, `2282` | `bool pop(T&)` — blocks until item or (finished **and** empty), then false | dequeue or drained→false | false when finished+empty; never throws | value moved out | wakes one pusher | in-memory | 003 |
| **Non-blocking** pop | `FileCache.cpp:1721` | `bool tryPop(T&)` — returns false immediately if empty | dequeue or unchanged | false if empty | value moved out | — | in-memory | 003 |
| Close queue (idempotent-ish; last-writer semantics) | `FileCache.cpp:1792`, `1895`, `1897`, `2273`, `2360` | `void finish()` — subsequent `push` fail, in-flight `pop` drain then return false | open→finished | none | unblocks all waiters | queue drained by consumers | in-memory | 003 |
| Exception→`VELOX_FAIL`-style throw with `ErrorCodes::*` + format args | `EvictionCandidates.cpp:296-298`; `Metadata.cpp:71`, `79`; `FileSegment.cpp:441-443`, `451-453`, `458`, `462-465`; `QueryLimit.cpp:108-111`, `122` | `throw Exception(ErrorCodes::CODE, fmt, args...)` | n/a | propagates; codes used: `LOGICAL_ERROR` (76×), `BAD_ARGUMENTS` (31×), `FAULT_INJECTED` (3×), `NOT_IMPLEMENTED`, `NOT_ENOUGH_SPACE`, `NO_ELEMENTS_IN_CONFIG`, `FILECACHE_ACCESS_DENIED` | thrown by value, caught by ref | callers catch `...`, `ErrnoException&`, `Exception&` distinctly (`FileSegment.cpp:501`, `533`) | — | 003 |
| Debug/sanitizer assertion with lazy message | `Metadata.h:64`, `82`; `EvictionCandidates.cpp:279-282`; `FileSegment.cpp:447` | `chassert(cond)` / `chassert(cond, msg)` | n/a | abort in debug/sanitizer; **no-op in release** (call it "exception", not "crash", in prose) | — | some asserts read atomics without locks | — | 003 |
| Reusable download scratch buffer sized to buffer default | `Metadata.cpp:939`, `963` | `std::optional<Memory<>>`; `memory.emplace(std::min(DBMS_DEFAULT_BUFFER_SIZE, size_to_download))` | lazily allocated, reused across segments on one thread | allocation may throw → caught by download loop | owned by download thread frame | thread-local scratch | in-memory | 003 |
| Named logger per cache; leveled logging | `FileCache.cpp:304`; `FileSegment.cpp:257` (`getLog()` shared in release, per-segment in debug — `FileSegment.h:331-335`) | `getLogger(name)`→`LoggerPtr`; `LOG_TRACE/DEBUG/INFO/WARNING/TEST(log, fmt, ...)` | n/a | logging must not throw | logger lifetime ≥ owner | thread-safe sink | diagnostic text only | 003 |
| Per-thread profile-event timer (RAII) | `EvictionCandidates.cpp:267` | `auto timer = CurrentThread::getProfileEvents().timer(ProfileEvents::FilesystemCacheEvictMicroseconds)` | starts on construct, records on destruct | none | RAII, scope-bound | per-thread counters | counters only | 003/006 |
| Profile-event increment | `FileCache.cpp:1747`, `1771`, `1885`, `1932` | `ProfileEvents::increment(event[, amount])` | n/a | none | atomic counters | global counters | counters only | 003 |
| Current-metric gauge inc/dec + RAII `Increment` | `Metadata.cpp:715`, `759`, `799`, `816`; `CacheUsage.h:116` | `CurrentMetrics::add/sub(metric[, n])`; `CurrentMetrics::Increment` | n/a | none | atomic gauges | global gauges | gauges only | 003 |
| One-shot init guard | `FileCache.h:341`; used around `initialize()` | `OnceFlag initialize_called;` + `callOnce` | uninit→init once | init exception cached (`init_exception`) and rethrown (`throwInitExceptionIfNeeded`) | member of `FileCache` | thread-safe once | in-memory | 003 |
| Scope-exit cleanup | `FileSegment.cpp:494`; `Metadata.cpp:1054` | `SCOPE_EXIT({ ... })` | runs on scope unwind incl. exceptions | reverses partial state (`cache_writer->set(nullptr,0)`) | RAII | — | — | 003 |
| Fail-point injection (test hooks) | `FileSegment.cpp:486`; `FileCache.cpp:1860`; `EvictionCandidates.cpp:310` | `fiu_do_on(FailPoints::name, { ... })` | n/a | injects `ErrnoException`/`Exception` on the real path | — | — | test-only | 003 |
| Elapsed-time stopwatch | `FileCache.cpp:1923` | `Stopwatch watch; watch.elapsedMilliseconds()` | n/a | none | thread-local | — | timing only | 003 |
| Releasability by unique ownership | `Metadata.h:42` | `bool isSharedPtrUnique(file_segment)` | n/a | none | reads shared_ptr use_count semantics | must be checked under key lock | — | 003 |

Notes (003): `Context::getGlobalContextInstance()->getSchedulePool()` (`FileCache.cpp:559`, `591`)
and `StorageID::createEmpty()` (`FileCache.cpp:560`, `591`) are infra shims on the scheduler access
path; the scheduler itself is Task 006 (see D22). `StorageID` degenerates to a task label. Ownership
of the `Context` global-instance shim is ambiguous between 003 and 006 — flagged as hole H1 (Sec 6).

### Task 004 — StatusFile and Guards

| Behavior | Call site | Signature / overload | State transition | Error behavior | Ownership / lifetime | Concurrency | Persistence | Owner |
|---|---|---|---|---|---|---|---|---|
| Exclusive on-disk status/lock file with full diagnostic body | `FileCache.cpp:517` | `make_unique<StatusFile>(path, StatusFile::write_full_info)` | absent→held | throws if another live holder owns the file (same/cross process) | owned by `FileCache::status_file` (`FileCache.h:343`); dtor `closeNoThrow` then unlink | one holder per file | on-disk file created; removed on dtor | 004 |
| Priority queue guard — write lock (structure mutation) | `FileCache.cpp:600` (`lockCache`), `1733`, `1843`; `Guards.h:82`, `93-97` | `CachePriorityGuard::WriteLock = std::unique_lock<SharedMutex>`; `writeLock()`, `tryWriteLock()` | unlocked→exclusive | none (blocking) / false-lock on try | RAII lock object passed by const-ref to guarded APIs | exclusive; profiled | — | 004 |
| Priority queue guard — read lock (read-only iteration / candidate collection) | `Guards.h:83`, `88-92`; consumed via `iterate(..., ReadLock)` `IFileCachePriority.h:261-264` | `ReadLock = std::shared_lock<SharedMutex>`; `readLock()`, `tryReadLock()` | unlocked→shared | none | RAII | shared readers | — | 004 |
| Cache state guard — protects total size/elements counters; **timed** acquire | `FileCache.cpp:1737`, `1809` (`tryLockFor`); `Guards.h:104-132` | `CacheStateGuard::Lock : unique_lock<std::timed_mutex>`; `lock()`, `tryLock()`, `tryLockFor(ms)` | unlocked→held or timeout | `tryLockFor` returns unheld lock on timeout (checked `if (!lock)`) | RAII | exclusive on counters; timed | — | 004 |
| Metadata bucket guard | `Metadata.cpp:765`; `Metadata.h:270-275` | `CacheMetadataGuard::Lock : unique_lock<std::mutex>`; `MetadataBucket::lock()` | unlocked→held | none | one guard per bucket (1024 buckets) | shards contention | — | 004 |
| Per-key guard | `Metadata.h:162`, `429`; taken by `LockedKey` ctor | `KeyGuard::Lock : unique_lock<std::mutex>`; `KeyMetadata::guard.lock()` | unlocked→held | none | one guard per key; held by `LockedKey` for its lifetime | one holder per key | — | 004 |
| Per-file-segment guard | `FileSegment.cpp:506` (`lock()`), pervasive; `FileSegment.h:323` | `FileSegmentGuard::Lock : unique_lock<std::mutex>`; `FileSegment::lock()` | unlocked→held | none | `mutable` member of `FileSegment` | one holder per segment | — | 004 |

Notes (004): the **lock ordering** is a hard structural invariant (see Section 3, LK rows): documented
at `Guards.h:52-53` as `CachePriorityGuard::Lock > CacheMetadataGuard::Lock > KeyGuard::Lock >
FileSegmentGuard::Lock`, with `CacheStateGuard` acquired **after** successful space reservation
(`Guards.h:23-27`). The struct-vs-`using` design (`Guards.h:78-81`) makes each guard's `Lock` a
distinct non-interchangeable type — a compile-time contract the port must preserve.

### Task 005 — thread pools

| Behavior | Call site | Signature / overload | State transition | Error behavior | Ownership / lifetime | Concurrency | Persistence | Owner |
|---|---|---|---|---|---|---|---|---|
| Long-running background thread from callable (async metadata load) | `FileCache.cpp:534` | `make_unique<ThreadFromGlobalPool>(callable)` — starts immediately | idle→running | ctor may throw (pool exhausted) → caught by `initialize` | owned by `load_metadata_main_thread`; joined in dtor (`FileCache.cpp:2671`) | one thread | — | 005 |
| Vector of ad-hoc worker threads (listing/loading/parallel remove) | `FileCache.cpp:2023`, `2302`, `2374`; `Metadata.cpp:1024`, `1027`, `1064` | `std::vector<ThreadFromGlobalPool>`; `emplace_back(callable)` | construct→run | `emplace_back` may throw → `handle_exception()` breaks | joined explicitly (`FileCache.cpp:2036`, `2388-2393`; `Metadata.cpp:1037-1091`) | N threads; producer/consumer via D1 queue | — | 005 |
| Thread join / joinable check | `FileCache.cpp:2036`, `2389-2393`, `2671`; `Metadata.cpp:1037`, `1040`, `1091` | `thread.join()`, `thread.joinable()` | running→joined | join propagates nothing; exceptions captured via `first_exception`/rethrow (`FileCache.cpp:2395-2396`) | must join before destroying owner | — | — | 005 |
| Bounded worker pool for lock-free eviction removers | `FileCache.cpp:583-589` | `make_unique<ThreadPool>(metric, metric_active, metric_scheduled, max_threads, max_free_threads=0, queue_size)` | constructed | ctor may throw | owned by `eviction_pool`; `wait()` in dtor (`FileCache.cpp:2681-2682`) | N removers | — | 005 |
| Schedule a task or throw on error (never silently drop) | `FileCache.cpp:1758` | `eviction_pool->scheduleOrThrowOnError(callable)` | queued | throws if scheduling fails (bounded queue full/shutdown) | task closure captures `[&]` of `freeSpaceRatioImpl` frame — must outlive via `wait()` | removers run concurrently with collector | — | 005 |
| Drain and wait for all pool tasks | `FileCache.cpp:1899`, `2682` | `eviction_pool->wait()` | running→drained | rethrows first task exception (per CH `ThreadPool::wait`) | barrier before frame teardown | joins all | — | 005 |

Notes (005): `ThreadFromGlobalPool` is used both as an owning `unique_ptr` handle and inside
`std::vector`, so the ported handle must be default-insertable, movable, `join`/`joinable`, and start
on construction from a callable. The **exception-propagation** contract (worker exceptions surfaced
via an explicit `first_exception` + `std::rethrow_exception`, `FileCache.cpp:2262-2271`, `2395`) is a
consumer-visible behavior, not an internal detail.

### Task 006 — scheduler and caller-token scope

The historically-reopened Task 006 gap was **immediate must preempt delayed** on a single task
holder. Both maintenance tasks below use `schedule()` (immediate) and `scheduleAfter(ms)` (delayed)
on the same holder, so the immediate-over-delayed priority is a live contract here.

| Behavior | Call site | Signature / overload | State transition | Error behavior | Ownership / lifetime | Concurrency | Persistence | Owner |
|---|---|---|---|---|---|---|---|---|
| Create a reschedulable background task | `FileCache.cpp:559-560`, `591` | `getSchedulePool().createTask(StorageID, name, callable)` → `TaskHolder` | Idle | — | holder owned by `FileCache` (`keep_up_free_space_ratio_task`, `background_cleanup_task`); dtor deactivates | task runs on pool thread | — | 006 |
| Immediate schedule (wake now) | `FileCache.cpp:562`, `592`, `1955`, `2925` | `holder->schedule()` | Idle/Delayed→Queued **now**; must override a pending delayed run | — | callback may run on another thread immediately | must preempt a pending `scheduleAfter` | — | 006 |
| Delayed schedule (after ms) | `FileCache.cpp:563`, `1641`, `1957` | `holder->scheduleAfter(size_t ms)` → bool | Idle→Delayed(ms) | returns bool "scheduled"; a pending immediate wins | reschedule interval from live settings (`backgroundCleanupIntervalMs`) | timer-backed | — | 006 |
| Deactivate (cancel; wait for in-flight to finish) | `FileCache.cpp:575`, `2675`, `2678` | `holder->deactivate()` | any→Deactivated | must be idempotent + safe after partial init | prevents orphan reschedule on `this` | joins in-flight callback | — | 006 |
| Invalidate-notifier wake hook (priority → cleanup task) | `FileCache.cpp:561-562` | `main_priority->setInvalidateNotifier(threshold, [this]{ background_cleanup_task->schedule(); })` | fires when invalidated-entry count reaches threshold | callback must be exception-safe | notifier stored in priority (`IFileCachePriority.h:380-384`, `490-491`) | fired under priority write lock; wakes task | — | 006 (hook) / 011 (storage) |
| Caller-id token for downloader identity + logs — **exact format** | `FileSegment.cpp:254-259` | `static String getCallerId()`; if `!CurrentThread::isInitialized() \|\| getQueryId().empty()` → `"None:{threadname}:{tid}"` else `"{query_id}:{tid}"` | n/a | none | value string | reads thread-local query id | identity string used to gate downloader | 006 |
| Query-context presence probe | `QueryLimit.cpp:15-20` | `CurrentThread::isInitialized() && CurrentThread::get().tryGetQueryContext() && !getQueryId().empty()` | n/a | none | thread-local | — | — | 006 |
| Query id lookup for per-query limit | `QueryLimit.cpp:28`; `FileSegment.cpp:581` | `CurrentThread::getQueryId()`, `CurrentThread::tryGetQueryContext()` | n/a | returns empty/nullptr when no query | thread-local | read under `query_map_mutex` | — | 006 |

Notes (006): the ported `FileCacheQueryIdScope` must make `getQueryId()` empty (→ `"None:..."`
branch) when no scope is active. The `"None:<threadname>:<tid>"` vs `"<query_id>:<tid>"` two-branch
format is a verbatim diagnostic/identity contract (downloader identity is compared for equality at
`FileSegment.cpp:367` and `Metadata.cpp:898`), so drift silently breaks downloader hand-off.

### Task 007 — IO adapters (reader/writer hand-off)

The historically-reopened Task 007 gap was the **full reader/writer relay + partial-write
reconcile**. The center-SCC exercises the writer buffer protocol in `FileSegment::write` and the
completion/reset paths, and the reader ownership relay in the getter/extract/reset methods.

| Behavior | Call site | Signature / overload | State transition | Error behavior | Ownership / lifetime | Concurrency | Persistence | Owner |
|---|---|---|---|---|---|---|---|---|
| Create cache writer; append when resuming a partially-downloaded segment | `FileSegment.cpp:478-484` | `make_unique<WriteBufferFromFile>(getPath(), buf_size=0, flags)`; `flags = O_WRONLY\|O_APPEND\|O_CLOEXEC` iff `downloaded_size>0` else `-1` (create/truncate) | none→open writer | ctor may throw (disk) → caught, `setDownloadFailed` | `download->cache_writer` (`LocalCacheWriterPtr = shared_ptr<WriteBufferFromFile>`), reset on terminal state | one downloader | opens on-disk cache file | 007 |
| Zero-copy working-buffer set + flush (write reserved bytes) | `FileSegment.cpp:492`, `494`, `496` | `writer->set(from, size, offset=size)`; `SCOPE_EXIT(writer->set(nullptr,0))`; `writer->next()` | buffer bound→flushed→detached | `next()` throws on IO error; reconciled below | caller-owned `from` buffer; writer must not retain after `set(nullptr,0)` | serialized by downloader role | bytes hit disk on `next()` | 007 |
| Physical-size invariant after a successful write | `FileSegment.cpp:498-499` | `downloaded_size += size; chassert(filesystem::file_size(path) == downloaded_size)` | accounting advanced | debug assert only | — | reads real file size | on-disk size must equal accounting | 007/012 |
| Partial-write reconcile after a failed append (no-space) | `FileSegment.cpp:517-527` | on `ErrnoException` with errno ∈ {28 ENOSPC, 122 EDQUOT}: `file_size=fs::file_size(path)`; `chassert(downloaded_size<=file_size<=reserved_size)`; `if (downloaded_size!=file_size) downloaded_size=file_size` | accounting reconciled to physical prefix | original exception rethrown (`FileSegment.cpp:531`) | — | single downloader under segment lock (`lk`) | trusts physical file size as truth | 007/012 (see E1) |
| Finalize writer on completion | `FileSegment.cpp:728`, `1080`, `1119` | `writer->finalize()` | open→flushed/closed | finalize may throw → handled per path | writer then `reset()` | downloader | flushes remaining | 007 |
| Cancel writer on failure/detach | `FileSegment.cpp:846`, `1406` | `writer->cancel()` then `writer.reset()` | open→cancelled | must not throw on cancel path | — | — | leaves partial file for reconcile | 007 |
| Remote reader ownership relay (get / extract / reset / set) | `FileSegment.cpp:370-412` | `getRemoteFileReader()`, `extractRemoteFileReader()` (move-out, gated on `available()==0` & offset match, `FileSegment.cpp:394-398`), `resetRemoteFileReader()`, `setRemoteFileReader(ptr)` (throws if already set at wrong offset) | reader attached↔detached | `set` throws `LOGICAL_ERROR` if replacing a live reader | `RemoteFileReaderPtr = shared_ptr<ReadBufferFromFileBase>` in `download_data` | only downloader mutates | reader position must equal write offset (invariant `FileSegment.h:226-228`) | 007 |
| Background-download read loop uses reader buffer | `Metadata.cpp:963-988` | `memory.emplace(...)`; reader `next()`/`read` into `memory`, then `file_segment.write(...)` | streams remote→cache | exceptions abort the segment download | reader owned by segment | one download thread | writes cache file | 007 |

Notes (007): `WriteBufferFromFile` is consumed through the CH streaming-buffer surface
`set(ptr,size,offset)`/`next()`/`finalize()`/`cancel()` and the `buf_size=0` + explicit POSIX
`flags` construction — the append-vs-truncate decision is driven by `downloaded_size>0`. The reader
relay's `available()==0 && getFileOffsetOfBufferEnd()==getCurrentWriteOffset()` precondition
(`FileSegment.cpp:394-397`, `FileSegment.h:226-228`) is the zero-copy hand-off contract.

### Task 008 — leaf types

The historically-reopened Task 008 gap was the **key-parser validation** and the shared
checked-arithmetic helper. Both are reachable here.

| Behavior | Call site | Signature / overload | State transition | Error behavior | Ownership / lifetime | Concurrency | Persistence | Owner |
|---|---|---|---|---|---|---|---|---|
| Parse a 32-char hex directory name into a key — **with validation** | `FileCache.cpp:2416`; `FileCacheKey.cpp:41-46` | `static FileCacheKey fromKeyString(const std::string&)`; throws `BAD_ARGUMENTS` iff `size()!=32`, then `unhexUInt<UInt128>` | n/a | `BAD_ARGUMENTS "Invalid cache key hex: {}"` | value | — | key ↔ directory name round-trip | 008 |
| Render key as 32-char lowercase hex (directory name) | `FileCacheKey.cpp:21-24`; used in path building `Metadata.cpp` | `std::string toString() const` → `getHexUIntLowercase(key)` | n/a | none | value | — | persisted as dir name | 008 |
| Derive key from a path via CH SipHash-2-4 128-bit | `FileCacheKey.cpp:31-34` | `static FileCacheKey fromPath(path)` → `FileCacheKey(sipHash128(path.data(), path.size()))` | n/a | none | value | — | stable hash across restarts | 008 |
| Wrap a raw 128-bit key | `FileCacheKey.cpp:36-39`; `Metadata.cpp` load path | `static FileCacheKey fromKey(const UInt128&)` | n/a | none | value | — | — | 008 |
| SipHash128 primitive (CH variant key0=key1=0, v2^=0xff) | `FileCacheKey.cpp:33` | `sipHash128(const char*, size_t)` → `UInt128` | n/a | none | pure | — | must match on-disk keys | 008 |
| Segment key-type routing enum + prefixes | `FileCache.cpp:429`, `440`, `2196-2198`, `2232-2233` | `FileSegmentKeyType{General,Data,System}`; `getKeyTypePrefix(type)`; extension→type classification | n/a | none | value enum | — | prefixes are on-disk directory names | 008 |
| Origin info + dedup pool key/hash | `FileCache.cpp:429`, `106-107`; `Metadata.cpp:108`; `Metadata.h:290` | `FileCacheOriginInfo{UserID user_id, weight, FileSegmentKeyType}`; `OriginPoolKey`, `OriginPoolKeyHash`; `getCommonUserID()`, `getInternalOrigin()` | n/a | none | immutable, shared via `OriginInfoPtr` | shared across keys w/ same origin | encodes user dir when `write_cache_per_user_directory` | 008 |
| Ownership aliases + `FileSegments` container + fwd ptrs | `IFileCachePriority.h:7`; `FileSegment.h:15`; `Metadata.h:10`, `28` | `KeyMetadataPtr`, `KeyMetadataWeakPtr`, `FileSegmentPtr`, `FileSegments`, `LockedKeyPtr`, `FileCachePriorityPtr`, `FileSegmentMetadataPtr` | n/a | none | shared/weak/unique per alias | — | in-memory | 008 |
| Policy enum + default constants | `FileCacheSettings.cpp:38-47`; `FileCache.cpp:312-314` | `FileCachePolicy{LRU,SLRU,LRU_OVERCOMMIT,SLRU_OVERCOMMIT}`; `FILECACHE_DEFAULT_*` | n/a | none | compile-time | — | config defaults | 008 |
| Checked-arithmetic alignment helpers | `FileCache.cpp:972`, `974-975` | `FileCacheUtils::roundDownToMultiple(x, m)`, `roundUpToMultiple(x, m)` | n/a | none (overflow-safe helper) | pure | — | boundary alignment of segment ranges | 008 |
| Query-limit map key + hash | `QueryLimit.cpp:104`, `120`, `133`; `QueryLimit.h:62` | `FileCacheKeyAndOffset{key, offset}`, `FileCacheKeyAndOffsetHash` | n/a | none | value | keyed in `unordered_map` | — | 008 |

Notes (008): `FileCacheKey::random()` (`FileCacheKey.cpp:26-29`, uses `UUIDHelpers::generateV4`) has
**no** center-SCC call site → over-port candidate O1 (Sec 6). The parser's `size()!=32` +
`unhexUInt` is the exact validation contract; anything looser silently mis-loads on-disk keys.

### Task 009 — ShardedMap

| Behavior | Call site | Signature / overload | State transition | Error behavior | Ownership / lifetime | Concurrency | Persistence | Owner |
|---|---|---|---|---|---|---|---|---|
| Sharded map declaration (origin dedup pool) | `Metadata.h:290` | `mutable FileCacheUtils::ShardedMap<OriginPoolKey, OriginInfoPtr> origins;` (default 32 shards, explicit `OriginPoolKeyHash`) | — | — | member of `CacheMetadata` | 32 shards, one `std::mutex` each | in-memory | 009 |
| Locked single-shard read-modify-write with callback | `Metadata.cpp:108-119` (`getOrCreateSharedOrigin`) | `origins.withShard(pool_key, [&](auto & map) -> OriginInfoPtr { ... })` | insert-or-find under shard lock | callback exception must keep size accounting consistent (see Sec 3 SM row) | callback runs holding the shard lock — must not re-enter the same shard | shard mutex held for callback | — | 009 |
| Sequential per-shard iteration (dedup-pool GC on client removal) | `Metadata.cpp:121-...` (`removeSharedOrigins`) | `origins.forEachShard([&](auto & map){ ... })` — locks each shard in turn, not all at once | scans/erases per shard | exception-safe size accounting | callback per shard under that shard's lock | one shard locked at a time | — | 009 |
| Per-user cache-usage map (idle-client tracking) | `CacheUsage.h:121` | `FileCacheUtils::ShardedMap<UserID, CacheUserData> clients_map;` | see O2 | — | member of `CacheUsagePerUser` | sharded | in-memory | 009 |

Notes (009): `clients_map` (`CacheUsage.h:121`) is only mutated by `CacheUsagePerUser`
methods, which are consumed by the **excluded** `OvercommitFileCachePriority` — from the in-scope
center-SCC it is not exercised (over-port candidate O2). `ShardedMap`'s constructor takes a
`lock_wait_event` parameter (Task 009 scope) that is a no-op shim in phase 1. **Structure deviation
SD1** (Sec 7): Task 009 replaces `std::unordered_map` with `F14FastMap` — a guarantee-changing
substitution the guide (§3) flags as requiring human sign-off, because origins/clients are reached
by `OriginInfoPtr`/value and F14 relocates values on rehash.

### Task 010 — settings

| Behavior | Call site | Signature / overload | State transition | Error behavior | Ownership / lifetime | Concurrency | Persistence | Owner |
|---|---|---|---|---|---|---|---|---|
| Effective config field access at construction | `FileCache.cpp:277-309` | `settings[FileCacheSetting::field]` (BaseSettings accessor); ported to `config.field` | copies into `FileCache` members | none (values validated upstream in `FileCacheSettings::validate`) | values copied into `FileCache` const/atomic members | read once at ctor | config→runtime | 010 |
| Fields consumed (exhaustive at ctor) | `FileCache.cpp:278-309` | `max_file_segment_size`, `enable_bypass_cache_with_threshold`, `bypass_cache_threshold`, `boundary_alignment`, `reserve_granularity`, `background_download_max_file_segment_size`, `load_metadata_threads`, `load_metadata_asynchronously`, `write_cache_per_user_id_directory`, `allow_dynamic_cache_resize`, `dynamic_resize_lock_wait_ms`, `keep_free_space_size_ratio`, `keep_free_space_elements_ratio`, `keep_free_space_remove_batch`, `keep_free_space_eviction_threads`, `invalidated_entries_cleanup_threshold`/`_interval_ms`/`_remove_batch`, `idle_client_ttl_sec`/`_check_interval_sec`/`_eviction_threads`, `use_split_cache`, `split_cache_ratio`, `skip_cache_on_disk_failure`, `expose_prometheus_eviction_metrics`(`_per_user`), `path`, `background_download_queue_size_limit`, `background_download_threads`, `check_cache_probability` | n/a | — | — | — | — | 010 |
| Eviction-policy field with `.value` + creator dispatch | `FileCache.cpp:312`, `377`, `385-388`, `396-399` | `settings[FileCacheSetting::cache_policy].value` → `FileCachePolicy`; `max_size`, `max_elements`, `slru_size_ratio`, `overcommit_eviction_evict_step` | selects `LRU`/`SLRU` creator; `*_OVERCOMMIT` throws `BAD_ARGUMENTS` unless distributed-cache build (`FileCache.cpp:368-371`) | throws `BAD_ARGUMENTS` on unsupported policy / split+overcommit combo (`FileCache.cpp:379`) | — | — | — | 010 |
| Live (reloadable) settings apply | `FileCache.h:266` decl; `FileCache.cpp:2800` def | `applySettingsIfPossible(new, actual)` reads the same field set under `apply_settings_mutex` | mutates atomics (`max_file_segment_size`, `reserve_granularity`, TTLs, ...) | rejects impossible changes | `apply_settings_mutex` (`FileCache.h:349`) | serialized | reload | 010 |
| Per-query download limit fields | `QueryLimit.cpp:82-83` | `FilesystemCacheSettings.max_download_size_per_query`, `.skip_download_if_exceeds_per_query_cache_write_limit` | sets query-context caps | none | copied into `QueryContext` | — | — | 010 |

Notes (010): the accessor form `settings[FileCacheSetting::x]` (and `.value` for the enum field) is
the BaseSettings surface; Task 010 replaces it with a plain `FileCacheConfig` struct + explicit
rejection of `cacheOnWriteOperations=true` and overcommit policies. `ReadSettings`/
`FileCacheReadOptions` are forward-declared on center-SCC APIs (`QueryLimit.h:9`, `FileCache.h:32`)
but no in-scope call site dereferences their fields → partial reachability (over-port candidate O5).

## 3. CH structure baseline (§3 direct-translation baseline)

Consumer-invisible internal structure that the port must translate **exactly** unless a hard Velox
constraint (proved by a section-E probe) forbids it. Each row cites the CH declaration.

### 3.1 State representation

| ID | Structure | CH reference | Guarantee the center-SCC relies on |
|---|---|---|---|
| ST1 | Priority `Entry::State` = `std::atomic<State>` over `{Active, PreActive, Evicting, Moving, Invalidated, Removed}`; state transitions guarded by specific locks encoded in the setter signatures | `IFileCachePriority.h:74-96`, `101-166` | State read lock-free (`getState()`); each transition asserts its predecessor and takes a specific guard token (`setActiveFlag(CacheStateGuard::Lock&)`, `setEvictingFlag(LockedKey&)`, `setRemoved(CachePriorityGuard::WriteLock&)`) |
| ST2 | `Entry` fields: `const key`, `const offset`, `const KeyMetadataWeakPtr key_metadata` (weak so invalidated entries don't pin metadata), `std::atomic<size_t> size` | `IFileCachePriority.h:59-67` | zero-size entries count nothing until first positive increment; weak metadata ref for lazy removal |
| ST3 | `FileSegment` state: `atomic<State> download_state`, `atomic<size_t> downloaded_size`/`reserved_size` with invariant `downloaded_size <= reserved_size`, `atomic<bool> size_in_filename` (false→true only), lazy `unique_ptr<DownloadState>` | `FileSegment.h:300-321` | downloaded/reserved read lock-free; `DownloadState` (downloader id, reader, writer, write_mutex) created only while downloading |
| ST4 | `KeyMetadata::KeyState` = `{ACTIVE, REMOVING, REMOVED}` (plain member under key guard) | `Metadata.h:117-122`, `161` | `tryLock` fails unless ACTIVE; cleanup only removes REMOVING keys |
| ST5 | `FileSegmentMetadata.removed` (bool under `LockedKey`) + evicting-state read through the queue iterator's entry | `Metadata.h:46-93` | `isEvictingOrRemoved` combines local bool with entry atomic state |
| ST6 | `FileCacheReserveStat` = `total_stat` + `std::array<Stat, enum_count<FileSegmentKind>()>` per-kind | `FileCache.h:71-72` | per-kind accounting; `magic_enum::enum_count` replaced by explicit count |
| ST7 | SLRU external iterator identity: `SLRUIterator` holds `LRUIterator lru_iterator` + separate `weak_ptr<Entry> entry` (TSA_GUARDED_BY `entry_mutex`) + `atomic<bool> is_protected`; `PreActive→Active` flip done atomically with the inner-pointer update in `setIterator` under `entry_mutex` | `SLRUFileCachePriority.h:209-225`, `212` | iteration never sees an entry evictable before the iterator points at it |

### 3.2 Locks and lock order

| ID | Lock | CH reference | Order / rule |
|---|---|---|---|
| LK1 | Global lock order | `Guards.h:52-53` | `CachePriorityGuard::Lock > CacheMetadataGuard::Lock > KeyGuard::Lock > FileSegmentGuard::Lock` |
| LK2 | `CacheStateGuard` acquisition point | `Guards.h:23-27`, `56` | taken **after** successful space reservation to update total size/elements; separate `std::timed_mutex` |
| LK3 | Per-method lock sequences (getOrSet/get/set; tryReserve; removeIfExists; removeAllReleasable; getSnapshot; FileSegment::complete) | `Guards.h:18-66` | documented sequences must be preserved verbatim (e.g. `FileCache::tryReserve` = PriorityWrite/Read → KeyGuard → per-evicted KeyGuards → CacheStateGuard) |
| LK4 | `LRUFileCachePriority` internal aux locks | `LRUFileCachePriority.h:183`, `194` | `eviction_pos_mutex` guards the two eviction cursors; `invalidated_mutex` guards `invalidated_refs`; both are leaf mutexes independent of the priority write lock |
| LK5 | `SLRUIterator::entry_mutex` | `SLRUFileCachePriority.h:220` | per-iterator leaf mutex enabling lock-free `getEntry`/state reads during queue moves |
| LK6 | `FileCacheQueryLimit::query_map_mutex` | `QueryLimit.h:86-90` | the map is reached under two different cache locks (State for reads, PriorityWrite for writes), so this dedicated leaf mutex is the only real serializer of `query_map` |
| LK7 | `CacheUsageStatGuard` (`std::mutex`) | `CacheUsage.h:32-37` | makes per-user counters update atomically with main state counters (needed only for overcommit `check`) |
| LK8 | `CacheMetadata::key_prefix_directory_mutex` (`SharedMutex`) | `Metadata.h:265` | guards key-prefix directory creation |
| LK9 | `FileCache` top-level mutexes | `FileCache.h:342`, `345`, `349`, `356-358` | `init_mutex`, `dynamic_resize_lock` (`shared_timed_mutex`), `apply_settings_mutex`, and the three cache guards `cache_guard`/`queue_guard`/`cache_state_guard` |

### 3.3 Containers and stability guarantees

| ID | Container | CH reference | Stability guarantee relied on |
|---|---|---|---|
| CT1 | `LRUQueue = std::list<EntryPtr>` | `LRUFileCachePriority.h:169` | iterator/reference stability across `splice` and across other insert/erase; a non-zero-size entry stays at the same iterator until Invalidated+removed (`LRUFileCachePriority.h:294-299`); cursors and `LRUIterator` cache raw list iterators |
| CT2 | `invalidated_refs = std::deque<InvalidatedRef>` + `atomic<size_t> invalidated_count` | `LRUFileCachePriority.h:188-197` | background cleanup can skip via the atomic without locking; deque append/pop-front stability |
| CT3 | `KeyMetadata : std::map<size_t, FileSegmentMetadataPtr>` (private inheritance) | `Metadata.h:98` | ordered-by-offset; node stability; `lower_bound`, ordered iteration, and `emplace`/`emplaceUnlocked` used by callers (`LockedKey.emplace`, `Metadata.h:152-154`, `375-376`) |
| CT4 | `MetadataBucket : std::unordered_map<FileCacheKey, KeyMetadataPtr>` with its own `CacheMetadataGuard` | `Metadata.h:269-275` | node/pointer stability — `KeyMetadata` reached by pointer while other buckets mutate |
| CT5 | `MetadataBuckets = std::vector<MetadataBucket>` sized `buckets_num=1024` (fixed at construction) | `Metadata.h:256`, `276-277` | fixed-size sharding; bucket chosen by key hash (`getMetadataBucket`) |
| CT6 | `origins`/`clients_map` = `ShardedMap<...>` | `Metadata.h:290`; `CacheUsage.h:121` | 32-shard partitioning; see SD1/SM row |
| CT7 | `EvictionInfo : absl::flat_hash_map<QueueID, QueueEvictionInfoPtr>` + `absl::flat_hash_set<CacheUsagePtr>` | `EvictionCandidates.h:56`, `111` | values are `unique_ptr`/`shared_ptr` (indirection survives rehash); order not relied on |
| CT8 | `EvictionCandidates::candidates : absl::flat_hash_map<FileCacheKey, KeyCandidates>` + `original_queue_types : std::unordered_map<const FileSegmentMetadata*, QueueEntryType>` | `EvictionCandidates.h:187`, `193` | keyed by raw `FileSegmentMetadata*` (stable — held via shared_ptr in candidate vectors) |
| CT9 | `QueryContext::Records = std::unordered_map<FileCacheKeyAndOffset, IteratorPtr, Hash>`; `QueryContextMap = std::unordered_map<String, QueryContextPtr>` | `QueryLimit.h:62-63`, `84-85` | value-ptr indirection; entry extracted (not erased-in-place) to defer destruction outside cache lock (`QueryLimit.cpp:58-59`) |
| CT10 | `SplitFileCachePriority::PriorityPerType = std::array<IFileCachePriorityPtr, 3>` | `SplitFileCachePriority.h:26`, `154` | fixed 3-slot array indexed by `FileSegmentKeyType`; General routes to Data (`getPriorityType`) |

### 3.4 Member order and destruction order

| ID | Aggregate | CH reference | Ordering contract |
|---|---|---|---|
| MO1 | `FileCache`: `main_priority` declared **before** `metadata` | `FileCache.h:351-355` | metadata holds iterators referencing priority internal state ⇒ metadata destroyed first (before priority) |
| MO2 | `LockedKey`: `key_metadata` (shared_ptr) then `KeyGuard::Lock lock` | `Metadata.h:428-429` | `lock` must be destructed before `key_metadata` (comment) — releases key mutex before dropping the metadata ref |
| MO3 | `CacheUsagePerUser::CacheUserData`: `priority` then `usage` | `CacheUsage.h:108-116` | `usage` destroyed first; its final refcount decrement orders `priority` destruction after concurrent `snapshot` copies release |
| MO4 | `EvictionInfo::~EvictionInfo() { clear(); }` | `EvictionCandidates.h:64-67`, `111` | base map (hold spaces) cleared before `kept_alive_cache_usage` pins drop, so `~HoldSpace` never releases into a per-user priority a concurrent `snapshot` already erased |
| MO5 | `FileSegment` members: `download_data` (unique_ptr) after the atomics, `queue_iterator` mutable, `cv`, `increasing_priority` atomic_flag | `FileSegment.h:288-340` | `DownloadState` freed at terminal state; `cv` outlives waiters |
| MO6 | `CacheMetadata` shutdown/destruction: `download_threads`/`cleanup_thread` joined in `shutdown`, queues cancelled first | `Metadata.cpp:1030-1042` (cancel `1032-1033`, join `1038`,`1041`); dynamic re-thread join `1091` | threads stopped (queue `cancel()` → notify → join) before maps torn down |
| MO7 | Kept-alive-usage merge on `EvictionInfo::add`/`addOrUpdate` | `EvictionCandidates.cpp:97`, `104`; `EvictionCandidates.h:94-100` | `takeKeptAliveCacheUsage(other)` must run when merging so merged raw pointers stay pinned |

### 3.5 Zero-size / accounting invariants (structural, consumer-relied)

| ID | Invariant | CH reference |
|---|---|---|
| ZI1 | A queue entry added with size 0 counts neither bytes nor elements until the first positive `incrementSize` | `IFileCachePriority.h:67`; `LRUFileCachePriority.h:80-92` (add doc), state `add` split into zero-size-under-write-lock then size-under-state-lock |
| ZI2 | `incrementSize` requires the `CacheStateGuard::Lock` (stronger consistency for eviction); `decrementSize` requires no lock | `IFileCachePriority.h:177-182` |
| ZI3 | Reserve vs. background eviction cursors are independent and lazily invalidated / iterator-safe | `LRUFileCachePriority.h:179-187`, `250-253` |
| ZI4 | `EvictionCandidates::evict()` runs **without** cache priority/state locks (only per-key `tryLock` + segment lock); queue-entry invalidation deferred to `afterEvictWrite` (write lock) which must run before `afterEvictState` (state lock) | `EvictionCandidates.cpp:262-330`; `EvictionCandidates.h:140-152` |
| ZI5 | Split cache: four limits (`max_data_segment_size/elements`, `max_system_segment_size/elements`) split by ratio; failed System resize rolls Data limits back (rollback cannot throw) | `SplitFileCachePriority.h:155-159`; `SplitFileCachePriority.cpp:118`, `132-149` (`modifySizeLimits` try/catch rollback) |

## 4. Coverage matrix (call site → behavior → owner task)

Forward direction: each major center-SCC consumer path → the Tasks 003-010 dependencies (`D#` from
Section 1) it exercises. Reverse direction (every `D#` → its call site) is the Section 1 inventory;
`D1`-`D37` all have ≥1 call site, and every Section-2 row is anchored to a call site, so both
directions close (see Section 8 self-check).

| Consumer path (file:line) | Dependencies exercised | Owner tasks |
|---|---|---|
| `FileCache::FileCache` ctor `FileCache.cpp:277-401` | D4, D7, D30, D35, D36 (via metadata ctor), D17, D29 | 003,004,008,010 |
| `FileCache::initialize`/`initializeImpl` `FileCache.cpp:508-598` | D8, D14, D20, D21, D22 (create+scheduleAfter+deactivate), D4 | 003,004,005,006 |
| `FileCache::freeSpaceRatioImpl` (background eviction) `FileCache.cpp:1690-1905` | D1 (push/tryPush-timed/pop/tryPop/finish), D5, D10, D21 (scheduleOrThrowOnError/wait), D4; consumes 011 `collectCandidatesForEviction`/`EvictionCandidates`/`removeEntries` | 003,005; +011 |
| `FileCache::backgroundCleanupTaskFunc` + `backgroundCleanupIntervalMs` `FileCache.cpp:1907-1957` | D11, D22 (schedule/scheduleAfter), D5; consumes 011 `removeInvalidatedEntries` | 003,006; +011 |
| `FileCache::evictIdleClients` `FileCache.cpp:1983` | D22, D6; consumes 011 `collectIdleClients` (base no-op) | 006; +011 (O2/O3) |
| `FileCache::loadMetadata`/`loadMetadataImpl`/`loadMetadataForKey` `FileCache.cpp:2196-2440` | D1 (tryPush non-blocking/pop/finish), D20 (listing/loading threads, join), D26 (`fromKeyString`), D28, D29, D4 | 003,005,008 |
| `FileCache::tryReserve`/`doTryReserve`/`doEviction` `FileCache.cpp` (reserve path) | D15, D16, D2; consumes 011 priority `collectEvictionInfo`/`canFit`/`add`, `EvictionCandidates` | 003,004; +011 |
| `FileCache::getOrSet`/`splitRange`/`createFileSegmentsFromRanges` `FileCache.cpp:960-1230` | D32 (`round*ToMultiple`), D2, D4; consumes 012 `FileSegment`/`LockedKey` | 003,008 |
| `FileSegment::write` `FileSegment.cpp:430-540` | D24 (set/next), D9 (SCOPE_EXIT), D10, D2, D19; **partial-write reconcile** (E1) | 003,004,007 |
| `FileSegment::complete`/`shrink`/`setDownloadFailed` `FileSegment.cpp:700-1130` | D24 (finalize/cancel), D25 (reader reset), D19, D2, D4 | 003,004,007 |
| `FileSegment::getCallerId`/`getOrSetDownloader`/`wait` `FileSegment.cpp:254-600` | D23 (exact caller-id format), D12, D4 | 003,006 |
| `CacheMetadata::downloadThreadFunc`/`downloadImpl` `Metadata.cpp:841-1000` | D3 (Memory), D25, D6, D20; consumes 012 `FileSegment` | 003,005,007 |
| `CacheMetadata::getOrCreateSharedOrigin`/`removeSharedOrigins` `Metadata.cpp:108-135` | D34 (withShard/forEachShard), D29 | 008,009 |
| `CacheMetadata::startup`/`shutdown`/`setBackgroundDownloadThreads` `Metadata.cpp:1018-1095` | D20 (ThreadFromGlobalPool ctor/join), D6, D9 | 003,005 |
| `LockedKey` ctor/dtor + `removeFileSegment*` `Metadata.h:357-430`; `Metadata.cpp` | D18 (KeyGuard held for lifetime), D13, D2 | 003,004 |
| `FileCacheQueryLimit::*` `QueryLimit.cpp:15-168` | D23 (CurrentThread), D33, D36, D15, D16, D2; consumes 011 `LRUFileCachePriority`/`add`/`remove` | 003,004,006,008,010; +011 |
| `EvictionCandidates::evict`/`afterEvict*`/`removeQueueEntries` `EvictionCandidates.cpp:130-330` | D5 (per-thread timer), D2, D10, D19; consumes 012 `LockedKey`/`FileSegment` | 003,004 |

## 5. Section-E candidates (undocumented Velox primitive semantics)

Behavior-affecting assumptions the CH source relies on but that cannot be **proved** from CH source
alone because they depend on a Velox platform primitive's failure/boundary semantics. Each needs a
focused E probe before any consuming row can be marked `matches` downstream.

| ID | Ledger row(s) | Assumed primitive semantic | Why source can't prove it | Probe question |
|---|---|---|---|---|
| E1 | D24, ST3, `FileSegment.cpp:517-527`, `499` | After a **partial** write that physically commits a strict prefix and then fails (ENOSPC/EDQUOT), the on-disk file size equals exactly the bytes durably written, so `fs::file_size(path)` is a safe truth to reconcile `downloaded_size` to; and after a **successful** `next()` the file size equals `downloaded_size`. | CH relies on `WriteBufferFromFile`/POSIX flushing bytes to the file before signaling error and never buffering past the reported size. The Velox `WriteFile` adapter (`WriteBufferFromVeloxWriteFile`, Task 007) may buffer/short-write differently and does not expose errno. This is exactly the CH-noted "reconcile by physical file size" case. | On the Velox write adapter, after a partial append that throws, does `fs::file_size` equal the bytes actually committed (≤ requested), with no trailing/short buffered bytes? |
| E2 | D24, `FileSegment.cpp:480-483` | Opening the writer with `O_WRONLY\|O_APPEND\|O_CLOEXEC` when `downloaded_size>0` appends at the existing physical end without truncating the prefix; `buf_size=0` means the buffer is fully caller-driven via `set(...)`. | The append/truncate semantics and the "zero internal buffer, caller supplies working buffer" contract are properties of the Velox write adapter, not provable from CH source. | Does the Velox write adapter honor append-without-truncate for a resumed segment and a zero internal buffer with `set(ptr,size,offset)` + `next()`? |
| E3 | D25, `FileSegment.cpp:394-398`; `FileSegment.h:226-228` | The remote reader's `available()==0` and `getFileOffsetOfBufferEnd()==getCurrentWriteOffset()` after a handoff mean the extracted reader is positioned exactly at the write offset (zero-copy continuation). | The buffer/position bookkeeping of `ReadBufferFromVeloxReadFile` (Task 007) determines whether these invariants hold; not derivable from CH source. | Does the Velox read adapter expose `available`/`getFileOffsetOfBufferEnd`/`setReadUntilPosition` with CH-equivalent semantics so the handoff invariant holds? |
| E4 | D16, LK2, `FileCache.cpp:1809`; `Guards.h:124-128` | `CacheStateGuard::tryLockFor(ms)` gives a genuine timed acquire (returns an unheld lock on timeout) usable to bail the background eviction pass. | Depends on the ported timed-mutex primitive faithfully implementing `std::timed_mutex::try_lock_for`. Likely an already-approved infra mapping, but the timeout semantics affect the background-eviction bail path. | Does the ported `CacheStateGuard` timed lock return "not held" on timeout rather than blocking? (Confirm; else demote to infra mapping.) |
| E5 | D1, `FileCache.cpp:1863`, `2345` | `ConcurrentBoundedQueue` timed `tryPush(x, ms)` and non-blocking `tryPush(x)`/`tryPop(x)` have the exact "false on full/finished/timeout" semantics the collector relies on to fall back to inline loading and to bound the push-retry loop. | The ported `FileCacheBoundedQueue` (Task 003) must replicate these blocking/timeout/finish semantics; source shows the required behavior but not that the port provides it. | Do the ported queue overloads return false (not block/throw) on full-with-timeout, on finished, and on empty `tryPop`, and does `finish()` drain `pop` to false? |

Note: E4 and E5 name **ported infrastructure** (Task 004/003) rather than a native Velox primitive;
they are listed because the behavior is a hard dependency of a center-SCC path. If the port's infra
matrix already guarantees `std::timed_mutex`/`ConcurrentBoundedQueue`-equivalent semantics, the
Controller may close them by citation instead of a probe. E1-E3 are genuine native-primitive probes.

## 6. Over-port candidates and ownership holes

Per the section-A hard gate: a behavior with **no** center-SCC call site is an over-port (must be
deleted or justified), and a call site with **no** owner is a hole (must stop and be filled). These
are reported, not silently resolved.

### 6.1 Over-port candidates (behavior with no in-scope call site)

| ID | Behavior | CH reference | Why flagged |
|---|---|---|---|
| O1 | `FileCacheKey::random()` | `FileCacheKey.cpp:26-29` | no center-SCC caller; only used by tests/introspection outside 011/012 scope |
| O2 | `CacheUsagePerUser` full machinery (`getOrSet`, `snapshot`, `tryGet`, `touchClient`, `collectIdleClients`, `canRemoveUser`) and `CacheUsage::update`/`operator<`/`lessWithAssumption` | `CacheUsage.h:41-122` | exercised only by the **excluded** `OvercommitFileCachePriority`; from in-scope code only `CacheUsagePtr` appears as an opaque pin in `EvictionInfo` (MO4/MO7) |
| O3 | `IFileCachePriority::collectIdleClients`/`touchClientAccess`/`getUsageStatPerClient` real (non-base) implementations | `IFileCachePriority.h:456-463`; base at `IFileCachePriority.cpp:72-77` | in-scope callers hit only the **base** no-op/throw versions (`FileCache.cpp:555`, `1983`, `2725`); real bodies live in overcommit (excluded) |
| O4 | `ConcurrentBoundedQueue::emplace`/`emplaceImpl` perfect-forwarding | (CH queue header) | **no** center-SCC call site uses `emplace` on the bounded queue (only push/tryPush/pop/tryPop/finish). The historical Task-003 `emplaceImpl` reopen is **not** reachable from 011/012 at this baseline — port only if another task needs it |
| O5 | `ReadSettings`/`FileCacheReadOptions` field dereference | `FileCache.h:32`; `QueryLimit.h:9` | only forward-declared on center-SCC signatures; no in-scope call site reads their fields (the fields feed higher-level readers in Tasks 014/016) |
| O6 | `OvercommitFileCachePriority`, `LRU_OVERCOMMIT`/`SLRU_OVERCOMMIT` policies, distributed-cache branches | `FileCache.cpp:339-366` (behind `ENABLE_DISTRIBUTED_CACHE`), `368-371`, `377-380` | explicitly excluded by Task 011; in-scope path throws `BAD_ARGUMENTS`. Confirms exclusion is a real code fork, not a silent drop |

### 6.2 Ownership holes (call site whose owner is ambiguous / unassigned)

| ID | Call site | Missing owner | Note |
|---|---|---|---|
| H1 | `Context::getGlobalContextInstance()->getSchedulePool()` `FileCache.cpp:559`, `591`; `StorageID::createEmpty()` `560`, `591` | ambiguous 003 vs 006 | The scheduler is Task 006 (`FileCacheScheduler`), but the `Context` global-instance accessor and `StorageID` label shim are not named in any 003-010 scope. Task 006 supplies the scheduler singleton access, so it likely owns this, but the assignment is not explicit — **needs Controller confirmation**, do not guess. |
| H2 | `CurrentThread::getProfileEvents().timer(...)` `EvictionCandidates.cpp:267` | ambiguous 003 vs 006 | Per-thread ProfileEvents access blends the ProfileEvents shim (003) with thread-context (006). Which task owns the per-thread profile timer is not explicit in 003-010 scopes. |
| H3 | `getInternalOrigin()`/`getCommonOrigin()`/`getCommonUserID()` static origin factories `FileCache.cpp:429`, `FileCache.h:126-129` | 008 vs 012 | These statics live on `FileCache` (Task 012) but produce Task-008 `FileCacheOriginInfo` values consumed by the priority layer; the origin **type** is 008, the **factory** is 012. Recorded here so no task silently absorbs the other's surface. |

## 7. Structure-deviation ledger (guide §3)

Only guarantee-**changing** replacements are deviations. Semantic-1:1 infra swaps (`SharedMutex`→
`folly::SharedMutex`, `std::mutex`→a lock with identical semantics) are approved infra mappings and
are **not** listed. This ledger records deviations that need a hard-constraint proof + human sign-off,
per the guide's "有牙" rule. Empty of *approved* deviations by design — these are **candidates** the
D review/Controller must resolve.

| CH structure | Proposed Velox replacement | Guarantee difference | Hard-constraint source | E-probe evidence | Human sign-off |
|---|---|---|---|---|---|
| SD1: `ShardedMap` inner `std::unordered_map` (CT6, `ShardedMap.h`) | `folly::F14FastMap` (Task 009 scope) | node/value address stability lost on rehash: `std::unordered_map` keeps values put; F14 relocates value bytes | none identified (guide explicitly says `std::unordered_map` compiles in Velox) | n/a | **REQUIRED — not on file** |
| SD2: `EvictionInfo`/`EvictionCandidates` `absl::flat_hash_map`/`flat_hash_set` (CT7, CT8) | `folly::F14` (Task 011 required-replacements list) | absl and F14 both relocate on rehash; center-SCC stores `unique_ptr`/`shared_ptr`/vectors so indirection survives, **but** `original_queue_types` keys on raw `FileSegmentMetadata*` (stable via shared_ptr) — verify equivalence | Task 011 "Required replacements" lists `absl → folly F14` as reviewed | n/a — confirm indirection makes it a no-op deviation | confirm-only |
| SD3: `KeyMetadata : std::map<size_t, FileSegmentMetadataPtr>` (CT3) | must stay `std::map` (ordered, `lower_bound`, node stability) | any hash-map swap changes ordering + `lower_bound` semantics | `Metadata.h:98`, `374` (`lower_bound`) | n/a | keep CH structure (no deviation permitted) |
| SD4: `MetadataBucket : std::unordered_map<FileCacheKey, KeyMetadataPtr>` (CT4) | must preserve node/pointer stability (KeyMetadata reached by pointer across bucket mutation) | F14 would relocate `KeyMetadataPtr` values — pointers stay valid (they are `shared_ptr`), but if a raw `KeyMetadata*`/iterator is cached across mutation it breaks | `Metadata.h:269-275` | n/a | confirm before any F14 swap |
| SD5: `LRUQueue = std::list<EntryPtr>` (CT1) | must stay `std::list` | splice + iterator stability is load-bearing (cursors + `LRUIterator` cache list iterators) | `LRUFileCachePriority.h:169`, `294-299` | n/a | keep CH structure (no deviation permitted) |

## 8. Self-check (completeness gate, performed by the A agent)

- **Every call site maps to a row:** each consumer path in Section 4 lists the `D#` rows it
  exercises; each `D#` has a Section-2 contract table row (or is explicitly demoted to Section 6).
  No center-SCC dependency call site was found without a corresponding row.
- **Every row maps to a call site:** every Section-2 row cites a real center-SCC call site
  (file:line); behaviors with no in-scope caller were moved to Section 6 over-port candidates
  (O1-O6), not left as unsupported contract rows.
- **Every structure guarantee has a source citation:** all Section 3 rows (ST/LK/CT/MO/ZI) cite an
  exact CH `file:line`.
- **One owner per dependency row:** Section 1 assigns exactly one Tasks-003-010 owner per `D#`;
  genuinely ambiguous owners are raised as holes H1-H3 (not silently assigned) and cross-task
  factories as H3.
- **No leaf-header-only inference:** every contract was read from a real consumer path
  (`FileCache.cpp`/`FileSegment.cpp`/`Metadata.cpp`/`QueryLimit.cpp`/`EvictionCandidates.cpp`), not
  from the dependency's own header alone.
- **No "generally useful" additions:** queue `emplace` (O4), `FileCacheKey::random` (O1), overcommit
  machinery (O2/O3/O6), and `ReadSettings` fields (O5) were **excluded** from contract rows because
  they have no center-SCC call site.

### Residual concerns for the Controller / D phase

1. Ownership holes H1-H3 need an explicit owner decision (do not guess).
2. Structure deviation SD1 (`ShardedMap` → F14) has no hard-constraint justification on file and
   per the guide requires human sign-off; SD2/SD4 need a confirm-only check that pointer indirection
   makes the F14 swap a non-deviation.
3. Section-E probes E1-E3 (native Velox write/read adapter failure/boundary semantics) must close
   before any consuming row is marked `matches`; E4-E5 may be closed by the infra matrix if it
   already guarantees `std::timed_mutex`/`ConcurrentBoundedQueue`-equivalent semantics.
4. `CacheUsage.h` is in Task 011's file scope but is overwhelmingly an overcommit (excluded) surface
   (O2/O3); the D phase should confirm the minimal reachable subset (EvictionInfo pin +
   `CacheUsageStatGuard` + base no-op hooks) rather than porting the whole file as "required".

## 9. Controller A-gate resolution

```text
controller_status: accepted_for_D
```

The Controller validated all 270 parsed CH `file:line` references: every cited
file exists and every line/range is within the frozen source. High-risk queue,
scheduler, IO-adapter, key-parser, settings, lock-order, container-stability,
and destruction-order rows were opened against the cited CH source. Phase D
must still reconstruct the contract independently and validate the meaning of
each citation.

### Ownership decisions

| Hole | Resolution |
|---|---|
| H1 | Task 006 owns the scheduler adaptation surface. CH `Context` lookup and `StorageID` are not ported as Task-003 shims; the Task-012 caller must receive/use `FileCacheScheduler` and preserve the task-name contract. |
| H2 | Task 003 owns the `ProfileEvents` timer/increment compatibility surface. Task 006 owns only query-id thread-local state; the per-thread eviction timer does not depend on that state. |
| H3 | This is an intentional cross-task boundary, not a Tasks-003-010 ownership hole: Task 008 owns `FileCacheOriginInfo`; Task 012 owns the `FileCache` factory methods that construct it. |

### Section-E disposition before D

| Candidate | Disposition |
|---|---|
| E1 | The adapter obligation already accepted in Task 007 is to propagate a partial-append exception, settle attempted bytes, cancel, detach caller memory, and preserve the physically committed prefix. Production filesystem-size reconciliation belongs to Task 012 and remains a mandatory Task-012 production-path test; it is not evidence against the accepted Task-007 adapter by itself. |
| E2 | Opening/reopening the physical cache file in append-without-truncate mode belongs to Task 012. Task 007 proves behavior for an already-open `WriteFile`; Task 012 must empirically prove the selected Velox filesystem open mode before its implementation can be accepted. |
| E3 | Closed for the current adapter by `IoAdaptersTest.ReaderHandoffSatisfiesFileSegmentInvariants` (`velox/ch/IO/tests/IoAdaptersTest.cpp:357-379`) and the implementation in `ReadBufferFromVeloxReadFile.cpp:131-193,220-253,297-321`. D must verify the test oracle and false-green evidence. |
| E4 | Closed by direct use of `std::timed_mutex` and timed `std::unique_lock` construction in `velox/ch/Interpreters/FileCache/Guards.h:85-112`. D must verify focused coverage or mark missing evidence. |
| E5 | Closed by `FileCacheBoundedQueue.h:47-174` plus focused timed/non-blocking/finish tests in `velox/ch/Common/tests/BasicShimsTest.cpp:265-390`. D must verify the tests and mutation evidence. |

E1 and E2 remain pre-Task-012 primitive questions. They do not prevent D from
reviewing the already-accepted Tasks 003-010, but Task 012 must not begin
implementation until a focused E probe or an equivalent production-path RED
test fixes the exact Velox file-opening and physical-prefix oracle.

## 10. Section-E probe evidence

```text
probe_status: complete
velox_commit: 89039901aa4287ce811a3b1628867b0796c76678
```

The focused E probe used the real `LocalWriteFile`, the real
`WriteBufferFromVeloxWriteFile`, and real temporary files. Its complete report is
stored in the session artifact `files/task-e-writer-probe.md`. Temporary probe
source and data were removed after the review decisions were recorded.

| Candidate | Empirical conclusion |
|---|---|
| E1 | With a real kernel short write induced by `RLIMIT_FSIZE`, `LocalWriteFile` committed exactly 6 of 16 requested bytes and threw its original exception. The adapter settled the full attempted 16 bytes once, canceled, detached caller memory, did not retry, and left `filesystem::file_size` equal to the 9-byte prior prefix plus the 6 committed bytes. |
| E2 | Reopening an existing file with `shouldThrowOnFileAlreadyExists=false` preserves the prefix and positions `LocalWriteFile` at its end; the adapter then appends without truncation. The default exclusive-create mode rejects the existing file and must not be used for resume. |
| E3 | The existing production-adapter handoff test directly proves `available() == 0`, the buffer-end offset equals the current write offset, and caller memory is detached. |
| E4 | The implementation uses `std::timed_mutex` with timed `std::unique_lock`; D still owns the evidence-strength verdict. |
| E5 | The queue implementation and focused tests exercise timed/non-blocking/finish behavior; D still owns the evidence-strength verdict. |

Controller reran the probe and focused adapter evidence:

```text
/root/oss/velox/_build/debug/full_review_e_probe_run_controller.log
  13/13 checks passed
  0 failures

/root/oss/velox/_build/debug/test_full_review_e_io_controller.log
  3/3 tests passed
  0 failed
  0 skipped/disabled
```

This closes the primitive questions only. Task 012 still owns the production
`FileSegment` resume and downloaded/physical/reserved-size reconciliation test.
