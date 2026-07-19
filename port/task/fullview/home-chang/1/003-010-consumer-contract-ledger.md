# Consumer 合同台账 (Consumer Contract Ledger) — Tasks 003–010 Dependency Surface

> A-section artifact per `/home/chang/SourceCode/.ai/share_data/local-cache/filecache-port-authoring-guide.md`.
> Read-only trace. Every contract row traces to a REAL CH consumer call site (file:line),
> framed from how **Task 011 (priority/eviction)** and **Task 012 (FileCache/FileSegment/Metadata/QueryLimit/Factory core)**
> actually consume the dependency. This is the §A authoritative artifact; every downstream
> Task 011/012 contract row must trace back to a row here.
>
> CH root: `/home/chang/SourceCode/ClickHouse/src/`
> Velox root: `/home/chang/OpenSource/velox/velox/ch/` (branch `filecache`, HEAD 89039901a)

---

## PART 1 — Per-Dependency Behavior Contract Tables

### Task 003 — Basic common shims (`ConcurrentBoundedQueue`/`FileCacheBoundedQueue`, logging, exceptions)

Consumed overload set: `push(T&&)`, `pop(T&)`, `tryPop(T&)` (1-arg non-blocking), `tryPush(T&&, UInt64=0)`, `finish()`, `ctor(capacity)`.

| 行为 Behavior | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 | 所有权/生命周期 | 并发要求 | 持久化 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|---|---|---|
| construct(capacity) | `FileCache.cpp:1704,1705,2260` | `ConcurrentBoundedQueue<T>(size_t)` | idle→ready | — | value-owned local | MPMC | — | `FileCacheBoundedQueue` | 012 |
| push (blocking) | `FileCache.cpp:1774` | `[[nodiscard]] bool push(T&&)` | block until slot/finished | returns false if finished, never throws | moves in | MPMC | — | `push(T)` ✓ | 012 |
| pop (blocking, drain-after-finish) | `FileCache.cpp:1721,1763,2282` | `[[nodiscard]] bool pop(T&)` | block until item OR finished+empty | false when finished+drained | moves out | MPMC | — | `pop(T&)` ✓ | 012 |
| tryPop (non-blocking) | `FileCache.cpp:1721` | `[[nodiscard]] bool tryPop(T&)` | immediate | false if empty | moves out | MPMC | — | `tryPop(T&)` ✓ | 012 |
| tryPush (timed) | `FileCache.cpp:1863` | `[[nodiscard]] bool tryPush(T&&, UInt64 ms=0)` | wait ≤ ms | false on timeout/finished/full | moves in | MPMC | — | `tryPush(T&&,uint64_t=0)` ✓ | 012 |
| finish (one-way, idempotent) | `FileCache.cpp:1792,1895,1897,2273,2360` | `void finish()` | latch; wakes all blocked | idempotent | — | wakes all | — | `finish()` ✓ | 012 |
| LOG_{TEST,TRACE,DEBUG,INFO,WARNING,ERROR} | `FileCache/*.cpp` (LOG_TEST×42, INFO×18, WARNING×17, DEBUG×10, ERROR×6, TRACE×5) | macros | — | — | — | — | — | `ch/Common/logger_useful.h` maps all 6 ✓ | 011/012 |
| throw Exception + ErrorCodes | `FileCache/*.cpp` (`throw Exception`×114; LOGICAL_ERROR×76, BAD_ARGUMENTS×31, NOT_ENOUGH_SPACE, FILECACHE_ACCESS_DENIED, NOT_IMPLEMENTED…) | `throw Exception(code,fmt,…)` | — | noreturn | — | — | — | `throwFileCacheException→VELOX_FAIL` (lossy: all codes collapse) ⚠ | 011/012 |

**Leaf-invisible:** `[[nodiscard]]` on every bool return (checked at `1774`/`2345`, ignored elsewhere); `finish()` one-way + idempotent; **drain-after-finish** (`pop` returns queued items post-`finish` — relied on at `1721`,`2282`).

### Task 004 — `StatusFile` and `Guards`

Sole `StatusFile` consumer: `FileCache.cpp:517` `status_file = make_unique<StatusFile>(fs::path(getBasePath())/"status", StatusFile::write_full_info)` (decl `FileCache.h:343`).

| 行为 Behavior | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 | 所有权/生命周期 | 并发要求 | 持久化 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|---|---|---|
| create cache-dir lock file | `FileCache.cpp:517` | `StatusFile(std::string, FillFunction)` | open→flock→write pid/info | throws if locked by another instance | `unique_ptr<StatusFile>` member | one per cache base dir | writes pid+revision to disk | `ch/Common/StatusFile` | 004→012 |
| dtor: close-before-unlink | `Common/StatusFile.cpp:109-116` | `~StatusFile()` | flock release → path removed | never throws (logs on error) | — | — | removes file | `closeNoThrow()` then `::unlink` (Velox `StatusFile.cpp:141,144`) — order preserved ✓ | 004 |
| `lockCache()` → priority write lock | `FileCache.cpp:600` | `CachePriorityGuard::WriteLock lockCache() const` | acquire top-level lock | blocks | held per critical section | top of lock order | — | `SharedMutex` write lock | 011/012 |
| CacheState timed lock (reserve) | `QueryLimit.cpp:22`, `Guards.h:124` | `CacheStateGuard::tryLockFor(timeout)` | timed acquire | false on timeout | — | independent of priority guard | — | must expose timed lock (**verify present**) | 011/012 |

**CH lock ordering (`Guards.h:53`):** `CachePriorityGuard::Lock > CacheMetadataGuard::Lock > KeyGuard::Lock > FileSegmentGuard::Lock`; `CacheStateGuard` independent. Distinct nested `Lock` structs are intentionally non-interchangeable (`Guards.h:78-81`).

**Verbatim StatusFile diagnostics (CH `Common/StatusFile.cpp`):**
- L60 `Status file {} already exists - unclean restart. Contents:\n{}`
- L62 `Status file {} already exists and is empty - probably unclean hardware restart.`
- L76 `Cannot lock file {}. Another server instance in same directory is already running.`
- L90 `Writing pid {} to {}`
- L112 `Cannot close file {}, {}` · L115 `Cannot unlink file {}, {}`

### Task 005 — Thread pools

| 行为 Behavior | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 | 所有权/生命周期 | 并发要求 | 持久化 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|---|---|---|
| ThreadPool ctor (eviction) | `FileCache.cpp:583-589` | `(metrics×3, max_threads, max_free=0, queue_size)` | idle→ready | throws if start fails | `unique_ptr`-owned | self-gated to max_threads | — | `FileCacheThreadPool` ✓ | 012 |
| scheduleOrThrowOnError | `FileCache.cpp:1758` | `void scheduleOrThrowOnError(Job)` | enqueue | **throws on enqueue failure (no silent drop)** | job by value | MT-safe | — | ✓ | 012 |
| wait (drain + rethrow) | `FileCache.cpp:2682` | `void wait()` | drain running+queued | rethrows first job exception | — | blocks | — | ✓ (rethrows) | 012 |
| ThreadFromGlobalPool ctor | `Metadata.cpp:1024,1027,1064`; `FileCache.cpp:534` | `ThreadFromGlobalPool(Callable)` | detached→running | — | move-only handle | — | — | `FileCacheWorker` ✓ | 011/012 |
| join | `FileCache.cpp:2036,2390`; `Metadata.cpp:1038-1041` | `void join()` | running→joined | rethrows callable exception | — | blocks | — | ✓ (join once) | 012 |
| destroy-while-joinable | (all handles, implicit) | dtor | — | CH std::terminate; **Velox `VELOX_CHECK`** (`ThreadPool.cpp:74`) — stricter | must join first | — | — | ✓ stricter | 012 |
| dynamic add w/ exception-safe registration | `Metadata.cpp:1064-1069` | `try{…}catch{pop_back;throw;}` | register→(fail)→rollback | rolls back on ctor throw | — | — | — | preserve rollback | 011/012 |

**Three-phase shutdown ordering (leaf-invisible — the reason 005 stayed stable):**
- `FileCache::deactivateBackgroundOperations` (`FileCache.cpp:2666-2684`): (1) `shutdown.store(true)` + join `load_metadata_main_thread`; (2) `keep_up_free_space_ratio_task->deactivate()` + `background_cleanup_task->deactivate()` (stop schedulers); (3) `eviction_pool->wait()` then `metadata.shutdown()`.
- `CacheMetadata::shutdown` (`Metadata.cpp:1030-1042`): **cancel-before-join** — `download_queue->cancel()`+`cleanup_queue->cancel()` FIRST (wake threads), then join download threads, then cleanup thread.

### Task 006 — `FileCacheScheduler` + caller/query-id scope

CH has no `FileCacheScheduler` class; it shims `BackgroundSchedulePool` task registration for `background_cleanup_task` and `keep_up_free_space_ratio_task`.

| 行为 Behavior | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 | 所有权/生命周期 | 并发要求 | 持久化 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|---|---|---|
| createTask | `FileCache.cpp:559,591` | `createTask(name, cb)` → holder | none→Idle | empty holder after shutdown | holder is FileCache member; dtor stops | pool-global | — | folly Timekeeper + worker pool | 006→012 |
| schedule (immediate) | `FileCache.cpp:562,592` | `bool schedule()` | Idle/Delayed→Queued; Queued coalesced; Running→pendingImmediate | false after shutdown | — | multiple schedules coalesce (idempotent) | — | preserve coalesce | 006 |
| scheduleAfter (delayed) | `FileCache.cpp:563,1641` | `bool scheduleAfter(ms)` | Idle/Delayed→Delayed; **Running+pendingImmediate → returns false** | false after shutdown | — | **immediate must beat delayed, never downgrade** | — | Velox `FileCacheScheduler.cpp:152-153` honors ✓ | 006 |
| deactivate (wait-for-running) | `FileCache.cpp:575,2674-2678` | `void deactivate()` | Any→Deactivated | — | blocks until running cb returns; forbids future runs | waits only if Running | — | preserve wait semantics | 006/012 |
| callback never throws out | `FileCache.cpp:1627-1636` | try/catch-all→log | — | catch all → `tryLogCurrentException`; else task dies | — | — | — | preserve catch | 006 |
| query-id init guard | `QueryLimit.cpp:15-19` | `CurrentThread::isInitialized() && tryGetQueryContext() && !getQueryId().empty()` | — | empty query-id → treated absent | thread-local | not across OS-thread boundary | — | `thread_local` + `FileCacheQueryIdScope` RAII | 006→012 |
| caller-id composition (empty guard) | `FileSegment.cpp:256-259` | `getQueryId()+":"+toString(getThreadId())`; empty→`"None:…"` | — | empty→`None` prefix | thread-local | — | — | Velox `FileCacheQueryIdScope.cpp:49-56` reproduces both ✓ | 006 |
| query context for KILL status | `FileSegment.cpp:581` | `tryGetQueryContext()->getProcessListElementSafe()` | — | nullptr if no context | — | — | — | context accessor | 012 |
| lock-timeout setting from context | `WriteBufferToFileSegment.cpp:34` | `tryGetQueryContext()->getSettingsRef()[…]` | — | nullptr → default timeout | — | — | — | context accessor | 012 |

### Task 007 — IO adapters (`ReadBufferFromVeloxReadFile` / `WriteBufferFromVeloxWriteFile`)

| 行为 Behavior | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 | 所有权/生命周期 | 并发要求 | 持久化 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|---|---|---|
| setReadUntilPosition | `Disks/IO/CachedOnDiskReadBufferFromFile.cpp:796` | `void setReadUntilPosition(size_t)` | narrow read window; eof at boundary | — | reader owns window | downloader single-thread | — | Velox `Read.cpp:297` not stubbed ✓ | 007 |
| seek | `CachedOnDiskReadBufferFromFile.cpp:829,834,844,848` | `off_t seek(off_t, int whence)` | reset position | throw on neg offset/bad whence | — | seek-once (S3) managed by CH | — | Velox `Read.cpp:255` not stubbed ✓ | 007 |
| getFileOffsetOfBufferEnd | `CachedOnDiskReadBufferFromFile.cpp:525,532,538,850,854` | `size_t getFileOffsetOfBufferEnd() const` | read absolute buf-end offset | — | — | — | — | Velox `Read.h:29` ✓ | 007 |
| next / nextImpl | `CachedOnDiskReadBufferFromFile.cpp:1307,1316` | `bool next()`/`bool nextImpl()` | settle→load→republish; empty=eof | nextImpl exception → cancel + rethrow | working view 1 nextImpl | per-nextImpl downloader window | — | Velox `Read.cpp:131,170` ✓ | 007 |
| getRemoteFileMetadata | `CachedOnDiskReadBufferFromFile.cpp:1032,1711` | `std::optional<RemoteFileMetadata> getRemoteFileMetadata()` | truncation detection | — | — | one request | — | **allowed nullopt** (base default `ReadBufferFromFileBase.h:79`) | 007→012/014 |
| write into cache (borrow+flush) | `Interpreters/FileCache/FileSegment.cpp:492-496` | `set(from,size,size); SCOPE_EXIT(set(nullptr,0)); next()` | append pending→reset cursor | nextImpl exception → cancel + rethrow | borrows caller buffer; SCOPE_EXIT returns it (zero-copy) | downloader exclusive | append to disk | Velox `WriteBuffer…:68` | 007 |
| lazy open (append if resumed) | `FileSegment.cpp:478-483` | `O_WRONLY\|O_APPEND\|O_CLOEXEC if downloaded_size>0` | closed→open(append) | throws on open fail | `unique_ptr<WriteBufferFromFile>` | — | append | already-open append only | 007 |
| sync / finalize / cancel | writer teardown paths | `sync()` append→flush no-close; `finalize()` append→close idempotent, no implicit fsync; `cancel() noexcept` idempotent, no append | — | canceled refuses further ops | — | — | flush/close | Velox `Write.cpp:94/102/122` ✓ | 007 |
| partial-write physical-size reconcile | `FileSegment.cpp:520-527` | `file_size=fs::file_size(); if(downloaded_size!=file_size) downloaded_size=file_size;` | exception → recalibrate downloaded | never mark reserved-but-unwritten as downloaded | local cache writer only | — | physical file tail authoritative | Velox writer only supplies settle+rethrow primitive; reconcile itself → **012** | **012** (007 only propagates) |

**§E primitive semantics:** Velox `WriteFile` does not expose errno; append is all-or-throw but local disk may physically partial-write → reconcile by `fs::file_size` (design §E). Velox writer `next()` catch performs `settle` so `count()/getPosition()` reflect flushed offset for caller reconcile.

### Task 008 — Leaf types (`FileCacheKey` / `FileCacheUtils` / key parser / checked arithmetic)

| 行为 Behavior | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 | 所有权/生命周期 | 并发要求 | 持久化 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|---|---|---|
| parse dir name → key (metadata load) | `FileCache.cpp:2416` | `static FileCacheKey fromKeyString(const std::string&)` | value ctor from UInt128 | throws `BAD_ARGUMENTS` if `size()!=32`; **non-hex silently accepted** via `unhexUInt` | by value | pure | reads persisted 32-hex dir name | value port; **must reproduce silent non-hex acceptance** | 008→012 |
| path → key | `FileCache.cpp:2082` | `static FileCacheKey fromPath(const std::string&)` | `sipHash128(path)` | — | by value | pure | — | must be bit-identical sipHash128 | 008→012 |
| roundDownToMultiple | `FileCache.cpp:972` | `size_t(size_t,size_t)` | `multiple==0→num`; else floor | no throw | value | pure | — | value port | 008→011/012 |
| roundUpToMultiple | `FileCache.cpp:974-975`; `FileSegment.cpp:895,962,964` | `size_t(size_t,size_t)` | `multiple==0→num`; rem==0→num; else `num+(multiple-rem)` | **throws `std::overflow_error`** on genuine overflow | value | pure | — | value port | 008→011/012 |
| checked add | `FileCacheUtils.h:32` | `common::addOverflow(T,T,T&)→bool` (`base/arithmeticOverflow.h`) | true on overflow → drives throw | — | n/a | pure | — | `__builtin_add_overflow` | 008 reopen → reused 013/014 |

**Leaf-invisible:** parser is **length-strict, hex-lax** — `if (key_str.size()!=32) throw BAD_ARGUMENTS "Invalid cache key hex: {}"` (`FileCacheKey.cpp:44`); non-hex chars map through the hex table (invalid→0) with no rejection. `roundUp` deliberately avoids `num+multiple-1` to prevent false overflow (`FileCacheUtils.h:23-26`). `toKeyString`/`Key::random`/`fromKey` have **no in-tree consumer**.

### Task 009 — `ShardedMap` (consumer = metadata `origins`)

**Critical:** `ShardedMap` is used ONLY for `origins` dedup (`Metadata.h:290` `ShardedMap<OriginPoolKey, OriginInfoPtr>`, value = `shared_ptr<const OriginInfo>`). The primary key→`KeyMetadata` index is **NOT** a `ShardedMap` — it is `MetadataBucket` (`std::vector<MetadataBucket>`, `Metadata.h:276`). Node-stability concerns apply to `MetadataBucket`, outside 009's scope.

| 行为 Behavior | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 | 所有权/生命周期 | 并发要求 | 持久化 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|---|---|---|
| withShard (get-or-create origin) | `Metadata.cpp:108` | `auto withShard(const Key&, F&&) const` → f's result | find→(maybe)emplace→**return copied shared_ptr** | if emplace/make_shared throws, propagates; SCOPE_EXIT recomputes size from actual `map.size()` → no phantom increment | returns copied shared_ptr; map keeps own ref; **no ref escapes callback** | per-shard `std::mutex`; callback runs under lock; **no lock re-entry** | — | F14FastMap OK **iff value stays shared_ptr & no ref escapes** | 009→012 |
| forEachShard (purge by user) | `Metadata.cpp:121` | `void forEachShard(F&&) const` | per-shard sequential lock; erase-while-iterate | erase no-throw; SCOPE_EXIT accounts delta | in-shard only | each shard locked in turn — NOT a global snapshot | — | F14 safe (single active iterator) | 009→012 |
| size | `ShardedMap.h:55` | `size_t size() const` | `total_count.load(relaxed)` | — | n/a | relaxed atomic; transient under concurrency | — | atomic port | 009→011/012 |

**Size-under-exception contract:** `size_before = map.size(); SCOPE_EXIT(accountSizeDelta(size_before, map.size()))` — delta computed from actual before/after size, so a throwing `emplace` leaves `total_count` unchanged. **No node-address stability reliance** in any current consumer.

### Task 010 — `FileCacheSettings` (parse/validate) — consumer = FileCache/Factory field reads

38 fields in `LIST_OF_FILE_CACHE_SETTINGS`; contract row = the CONSUMER that reads the field (not the parser). Representative rows:

| 行为 Behavior | 触发调用点 file:line (consumer) | 状态转移 | 错误行为 | 并发 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|
| `path` → cache dir | `FileCache.cpp:305`; `FileCacheFactory.cpp:83,129,194` | ctor→CacheMetadata | throws if unset/relative | factory mutex on dedup | `cfg.path` + allowed-root check | 010/011 |
| `max_size` | `FileCache.cpp:385,396,424`; resize `2864,2884` | drives priority cap | must ≠0 or ratio-derived | resize lock | `cfg.maxSize` | 010/011 |
| `max_file_segment_size` | `FileCache.cpp:278,741`; resize `2905,2907` | segment split (atomic load) | must ≥ boundary_alignment | atomic | `cfg.maxFileSegmentSize` | 011 |
| `boundary_alignment` | `FileCache.cpp:280,971` → `roundDown/UpToMultiple @972,974,975`; `FileSegment.cpp:895,962,964` | alignment rounding | must ≤ max_file_segment_size | const after ctor | `cfg.boundaryAlignment` | 011 |
| `cache_policy` | `FileCache.cpp:312,377,404,413` | selects LRU/SLRU/Split/overcommit | one-hot enum | ctor | `cfg.cachePolicy` + one-hot `parsePolicy` | 011 |
| `slru_size_ratio` / `split_cache_ratio` | `FileCache.cpp:387,398,388`; `SplitFileCachePriority.cpp:29-37`; resize `2866,2871` | SLRU/split fan-out | finite check | resize | `cfg.slruSizeRatio`/`cfg.splitCacheRatio` | 011 |
| `background_download_threads` / `…_queue_size_limit` / `…_max_file_segment_size` | `FileCache.cpp:306,307,282`; `FileSegment.cpp:964`; resize `2807-2848` | metadata download pool/queue | 0 disables | runtime settable | `cfg.background*` | 011 |
| 6× `NonZeroUInt64` (`load_metadata_threads`, `keep_free_space_eviction_threads`, `invalidated_entries_cleanup_{threshold,interval_ms,remove_batch}`, `idle_client_eviction_threads`) | `FileCache.cpp:283,291,292,293,294,297` | pool/trigger sizing | **type-enforced ≠0** (Velox re-checks post-hoc `:439,442,445,449,453,457`) | mixed | `cfg.*` + explicit `==0` checks | 011 |
| `enable_filesystem_query_cache_limit` | `FileCache.cpp:421` | enables `FileCacheQueryLimit` | — | ctor | `cfg.enableFilesystemQueryCacheLimit` | 012 (QueryLimit) |
| `max_size_ratio_to_total_space` | validate-only `FileCacheSettings.cpp:266-283` (statvfs→max_size) | ratio→max_size | ∈(0,1] | validate-time | `cfg.maxSizeRatioToTotalSpace` + `isfinite` | 010 |
| `cache_hits_threshold` | declared `FileCache.cpp:121`, **no read** (deprecated) | — | — | — | carried, unused | 010 parse-only |

**Verbatim CH validation errors** (`FileCacheSettings.cpp`): L232 `` `path` is required parameter of cache configuration ``; L238 `` Either `max_size` or `max_size_ratio_to_total_space` must be defined… ``; L241 `` `max_size` and `max_size_ratio_to_total_space` cannot be specified at the same time ``; L244 `` `max_size` cannot be 0 ``; L247 `` `overcommit_eviction_evict_step` cannot be zero ``; L258-264 boundary_alignment ≤ max_file_segment_size; L269 `` `max_size_ratio_to_total_space` must be in range (0, 1] ``. **Velox strings do NOT match verbatim** (kebab-case keys `max-size`, reworded) — see Gate H1.

---

## PART 2 — Coverage Matrix (call site → required behavior → owning task)

| Dep (task) | CH consumer call sites (anchors) | Required behavior | Velox impl? | Over-port? | Owning consumer task |
|---|---|---|---|---|---|
| 003 queue | `FileCache.cpp:1704,1705,1721,1763,1774,1792,1863,1895,2260,2273,2282,2345,2360` | push/pop/tryPop/tryPush(timed)/finish/drain-after-finish | ✓ | O1: timed `tryPop(T&,ms)`, O2: const-ref `tryPush` (no call site) | 012 |
| 003 logging | `FileCache/*.cpp` (98 LOG sites) | 6 macros | ✓ all mapped | — | 011/012 |
| 003 exceptions | `FileCache/*.cpp` (114 throws) | throw+codes | ✓ (lossy) | — | 011/012 |
| 004 StatusFile | `FileCache.cpp:517` | create/lock/close-before-unlink/verbatim diag | partial | — | 012 |
| 004 Guards | `FileCache.cpp:600`; `QueryLimit.cpp:22`; `Guards.h:53` ordering | lock ordering; timed CacheState lock | ✓ (verify tryLockFor) | — | 011/012 |
| 005 threadpool | `FileCache.cpp:534,583,1758,2036,2390,2682`; `Metadata.cpp:1024,1027,1064,1030-1042` | ctor/schedule/wait/join/3-phase shutdown/cancel-before-join | ✓ | — | 012 |
| 006 scheduler | `FileCache.cpp:559,562,563,591,592,1641,2674-2678` | createTask/schedule/scheduleAfter/deactivate; **immediate>delayed** | ✓ (`FileCacheScheduler.cpp:152-153`) | `setCallback` test-only | 006/012 |
| 006 queryid | `QueryLimit.cpp:15-27,76`; `FileSegment.cpp:256-259,581`; `WriteBufferToFileSegment.cpp:34` | empty-guard + `qid:tid` composition + context accessors | ✓ | — | 012 |
| 007 read adapter | `CachedOnDiskReadBufferFromFile.cpp:525,532,538,796,829-854,1032,1307,1316,1711` | seek/setReadUntilPosition/next/getFileOffsetOfBufferEnd (not stubbed); getRemoteFileMetadata (nullopt allowed) | ✓ (metadata=nullopt) | — | 007/012 |
| 007 write adapter | `FileSegment.cpp:478-496,520-527,549` | borrow+flush/append/sync/finalize/cancel; partial-write reconcile→012 | ✓ (reconcile in 012) | — | 007/012 |
| 008 key/utils | `FileCache.cpp:972,974,975,2082,2416`; `FileSegment.cpp:895,962,964`; `FileCacheUtils.h:32` | fromKeyString(len-strict,hex-lax)/fromPath/round{Down,Up}/addOverflow | ✓ | `checkedAdd`/VELOX_FAIL (no CH consumer yet) | 008/012 |
| 009 shardedmap | `Metadata.cpp:108,121`; `Metadata.h:290` | withShard/forEachShard/size; size-under-exception; no ref escape | ✓ | Hash template param, unused `lock_wait_event_` | 012 |
| 010 settings | `FileCache.cpp:278-424, 2807-2947`; `FileCacheFactory.cpp:83,129,194`; `SplitFileCachePriority.cpp:29-37` | 38-field parse+validate; consumers read each | ✓ | allowed-root check (additive); `dumpToSystemSettingsColumns` intentionally not ported | 010/011 |

---

## PART 3 — 结构偏离台账 (Structural Deviation Ledger)

| # | CH 结构 | Velox 替代 | 保证差异 | 硬约束出处 | E 探针证据 | 人工签字 |
|---|---|---|---|---|---|---|
| **009-1** | `std::unordered_map<Key,Value>` (node-based; element/reference addresses **stable** across insert/erase of other elements) | `folly::F14FastMap<Key,Value,Hash>` (`ShardedMap.h:20,60` — confirmed) | F14 rehash **relocates values** → element addresses/references invalidated on insert; unordered_map never relocates | **NO hard constraint** — `std::unordered_map` compiles in Velox; guide §3 flags as needing sign-off | **NEUTRALIZED for current consumer**: value is `shared_ptr` (F14 moves the 8-byte ptr, pointee stable); sole consumer `Metadata.cpp:105-130` copies out shared_ptr, no ref escapes | ✅ **R1 SIGNED (user 2026-07-19): KEEP F14.** CH consumer proven not to need node stability (`Metadata.cpp:108` copies out `shared_ptr`, `:121-130` erase-while-iterate within single callback). **Invariant locked:** no `Value&`/iterator may escape `withShard`/`forEachShard` across a mutation — **011/012 review MUST enforce this.** |
| **009-2** | per-shard `DB::ProfiledMutexLock(mutex, lock_wait_event)` | per-shard plain `std::unique_lock<std::mutex>` (`ShardedMap.h:75`); `lock_wait_event_` stored but **never read** | drops profiled lock-wait telemetry | none | grep: event unused in Velox | ✅ **R9 SIGNED (user 2026-07-19):** acceptable — 011 needs no lock-wait telemetry. Dead `lock_wait_event_` member → trim under R11/O4. |
| **009-3** | fixed `std::hash<Key>` | added `Hash` template param (`ShardedMap.h:53`) | over-generalization; harmless | none | — | flagged R11/O4 (over-port, harmless) |
| **003-1** | typed `ErrorCodes` (LOGICAL_ERROR/BAD_ARGUMENTS/NOT_ENOUGH_SPACE/…) | single `VELOX_FAIL` via `throwFileCacheException` | error-code identity lost; downstream can't distinguish `NOT_ENOUGH_SPACE` from `LOGICAL_ERROR` | none | 114 throws / 7 codes collapse | ✅ **R7 SIGNED (user 2026-07-19):** accept — deliberate, 114 sites. **Constraint:** any 011/012 path that must distinguish `NOT_ENOUGH_SPACE` vs `LOGICAL_ERROR` MUST reintroduce a typed subtype at that call site. |
| **004-1** | verbatim StatusFile diagnostics (`already exists - unclean restart`, empty-file variant, `Writing pid {}`, `Another server instance … already running`) + **read-old-contents-before-truncate** unclean-restart detection branch (CH `StatusFile.cpp:50-63`) | **ENTIRE unclean-restart branch NOT ported** — Velox ctor (`StatusFile.cpp:93-131`) has no read-old-contents step at all; only reworded lock-contention text | crash-recovery diagnostics lost | none | grep: CH `:50-63` branch absent in Velox | ⚠️ **R3 REOPEN — DEFERRED to pre-release (user 2026-07-19, release blocker).** flock core (double-instance guard) IS aligned; only crash-recovery *diagnostics* missing. Does NOT touch SCC → does not block 011/012. Impl-only fix (see review §R3), interface unchanged. |
| **004-2** | raw `int fd` + manual `close`/`unlink`, close-before-unlink | `folly::File` + `closeNoThrow()` then `::unlink` (`StatusFile.cpp:141,144` — confirmed) | order preserved ✓ | folly::File infra | dtor order match | equivalent (accept) |
| **004-3** | CH custom `SharedMutex` for `CachePriorityGuard` | `SharedMutex` (Velox `Guards.h:49`) | RW semantics match; impl differs | infra matrix equivalence | Read/Write overloads match | equivalent (accept) |
| **004-4 (FillFunction)** | `FillFunction = std::function<void(WriteBuffer&)>` | `std::function<void(int fd)>` (Velox `StatusFile.h:46`) | callback IO abstraction changed | infra (CH `WriteBuffer` not ported) | **NOT a deviation** — grep-confirmed: no CH consumer writes a custom FillFunction; only built-in `write_full_info` used by file cache (`FileCache.cpp:517`). `WriteBuffer&`→`int fd` is consumer-invisible internal substitution | ✅ accept (equivalent infra substitution) |
| **005-1** | `ThreadFromGlobalPool` (shared global pool) | per-pool `FileCacheWorker` + local `FileCacheWorkerPool` self-gated to maxThreads | no global pool; local budget | none (design choice) | ThreadPool.h design | accept (design) |
| **005-2** | destroy-joinable → `std::terminate` | `VELOX_CHECK` (`ThreadPool.cpp:74`) | Velox stricter/explicit | none | — | accept (equivalent-stricter) |
| **006-1** | `CurrentThread::getQueryId()` (TLS via ThreadStatus) | `thread_local std::string` set by `FileCacheQueryIdScope` RAII | requires explicit scope; not across OS-thread boundary | infra | `SchedulerAndScopeTest.cpp` | accept (infra) |
| **006-2** | centralized `delayed_thread` + multimap timer; non-recursive mutex + `activate` revive | per-task `folly::Future timerFuture_`; `std::recursive_mutex`; **no activate** | timer source differs (allowed); `activate` revive semantics dropped | none | tests | ✅ **R8 SIGNED (user 2026-07-19):** accept — no CH file-cache consumer relies on `activate`-revive of a deactivated task (CH only `schedule`/`scheduleAfter`/`deactivate`). |
| **007-1** | `working_buffer/internal_buffer/pos` (BufferBase); `std::vector<char>` | `FileCacheBufferState` + `BufferPtr/AlignedBuffer`; raw ptr = non-owning view only | zero-copy borrow semantics preserved; vector forbidden | design §7.1-7.2 (soft) | `IoAdaptersTest.cpp` | accept (design) |
| **007-2** | typed errno (ENOSPC/EDQUOT) on write | **no errno** → reconcile by `fs::file_size` | typed classification lost; physical-size authoritative | **HARD** — Velox `WriteFile` doesn't expose errno (§E, E-probe) | §E empirical probe | ✅ **R6 SIGNED (user 2026-07-19):** accept — the one hard-constraint-backed deviation. Reconcile-by-physical-size is the only available primitive. |
| **007-3** | `getRemoteFileMetadata` real (S3 listing) | not implemented → base-default `nullopt` | truncation detection inert until Velox metadata source | design §E allows nullopt (soft) | `ReadBufferFromFileBase.h:79` | accept (deferred 012/014 by design) |
| **008-1** | `fromKeyString` error `"Invalid cache key hex: {}"` | `"Invalid cache key hex string: expected 32 characters, got {}"` (`FileCacheKey.cpp:54` — confirmed) | text deviation; both length-strict | none | both have `!=32` branch | accept (low; text only) |
| **008-2** | `unhexUInt<UInt128>` (non-hex → hex-table value, silent) | explicit nibble lambda, non-hex→`0xFF`, additive to reproduce overflow | intended parity, unverified by CH-differential | none | commented claim only | ⏸️ **R5 DEFERRED to post-019 (user 2026-07-19):** local impact, CH-parity is diagnostics convenience only. Needs differential fuzz vs CH `unhexUInt` (`'g'`, mixed case) — post-019. |
| **008-3** | `common::addOverflow` (shared helper) | `__builtin_add_overflow` inline + **new** `checkedAdd→VELOX_FAIL` (`FileCacheUtils.h:58`) | roundUp throws in both; `checkedAdd` is speculative for 013/014 | none | both throw `std::overflow_error` | flagged R11/O3 (over-port until 013/014 consumer shown) |
| **008-4** | CH `SipHash.h` `sipHash128` | `ch/Common/SipHash128.h` (faithful: seeds + CH `v2^=0xff` at `:118`) | **must be bit-identical** for on-disk key compat | HARD (persistence) | impl faithful; golden vectors NOT proven CH-derived | ⏸️ **R4 DEFERRED to post-019 (user 2026-07-19):** local impact (worst case = cache all-miss/re-download, not a correctness fault). Impl looks correct; CH-parity is diagnostics convenience. Needs CH-computed golden-vector test + `0xff→0xee` probe — post-019. |
| **010-1** | `BaseSettings<Traits>` PImpl with per-field `.value`+`.changed` | `FileCacheConfig` plain aggregate; presence tracked via 3 local bools | loses per-field `.changed`; only path/max_size/ratio track presence | none | grep: consumers read `.changed` only on those 3 | ✅ **R2 SIGNED (user 2026-07-19): accept, no reopen.** CH dynamic resize (`FileCache.cpp:2800 applySettingsIfPossible`) uses **per-field value comparison `new!=actual`**, NOT `.changed`. `.changed` is read ONLY in validate, ONLY for path/max_size/ratio (already covered). **Constraint:** 012 `applySettingsIfPossible` MUST use per-field value comparison; MUST NOT depend on field presence. Restoring per-field presence would be over-port. |
| **010-2** | (no CH counterpart) | Velox **adds** allowed-root path authorization (`FileCacheSettings.cpp:211-263`) | security additive, not a CH port | none | — | ✅ **R10 SIGNED (user 2026-07-19):** accept — intended additive security policy, not scope creep. |

---

## PART 4 — GATES

**Holes (CH call site → weak/missing Velox mapping):**
- **H1 (008):** `fromKeyString` error text differs from CH verbatim (`FileCacheKey.cpp:54` vs CH `:44`). Hole if any oracle asserts CH message. Also 010 verbatim validation strings diverge (kebab-case).
- **H2 (004):** StatusFile verbatim diagnostics (`unclean restart`, `Writing pid {}`, `Another server instance … already running`) **not reproduced** — unclean-restart operational diagnostics regression.
- **H3 (010, low):** `.changed` semantics gap — Velox tracks presence only for path/max_size/ratio; safe for current consumers (grep-verified) but implicit, breaks if a future consumer reads `.changed` elsewhere.
- **G-004-verify: RESOLVED** — Velox exposes `CacheStateGuard::tryLockFor(const std::chrono::milliseconds&)` at `Guards.h:107` (also `tryLock()` at `:95`). Timed-reserve path is NOT a hole.
- **007 deferred (not a 007 hole):** `getRemoteFileMetadata` (nullopt allowed) truncation branch inert, and partial-write `fs::file_size` reconcile (`FileSegment.cpp:520-527`) — both correctly owned by **012/014**, not 007.

**Over-ports (Velox behavior → no CH call site):**
- **O1 (003):** `FileCacheBoundedQueue::tryPop(T&, ms)` timed overload — no CH call site (only 0-arg used).
- **O2 (003):** const-ref `tryPush` overload — CH only calls rvalue form.
- **O3 (008):** `checkedAdd`/`VELOX_FAIL` in `FileCacheUtils.h:58` — not in CH; speculative until a 013/014 consumer is shown.
- **O4 (009):** `Hash` template param + unused `lock_wait_event_` (dead profiling surface).
- **O5 (006):** `FileCacheScheduler::setCallback` — test-only, no production consumer.
- **Intentional non-ports (NOT over-ports):** `dumpToSystemSettingsColumns` / structured system-table output (010, per design); `LOG_FATAL` (003, no CH FileCache site).

**Structural deviations requiring human sign-off (§3, not auto-approvable):** 009-1 (F14FastMap — headline; neutralized-but-consumer-dependent), 003-1 (error-code collapse), 004-1 (diagnostic text), 008-4 (sipHash128 bit-identity — persistence hard constraint), 010-2 (added allowed-root check). 007-2 (no-errno → physical-size reconcile) is the one deviation backed by a **hard** Velox-primitive constraint (§E).
