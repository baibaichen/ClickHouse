# Consumer Contract Ledger — Tasks 011–014 (Center SCC + Consumers)

> §A artifact per `/home/chang/SourceCode/.ai/share_data/local-cache/filecache-port-authoring-guide.md`
> (sections **A** = top-down consumer ledger; **§3** = structural literal-translation baseline).
> Round **2** of the port review. The Velox implementation now **EXISTS**; this ledger is the §3
> structural baseline that the follow-up full-review **(D)** will diff the Velox port against.
> READ-ONLY. Every contract row traces to a **real CH caller (file:line)** — no contract is derived
> bottom-up from a leaf header.
>
> New axis for R2 (user): cover BOTH **(a) consumer-observable SEMANTICS** and **(b) INTERNAL
> IMPLEMENTATION STRUCTURE** per §3 — state representation, lock structure + lock ORDER, container
> types + their stability/relocation guarantees, ownership/lifetime, async/scheduling shape.
>
> CH root: `/home/chang/SourceCode/ClickHouse/src/`
> Velox root: `/home/chang/OpenSource/velox/velox/ch/`
> Abbreviations: `ReadBuffer` = `src/Disks/IO/CachedOnDiskReadBufferFromFile.cpp`;
> `WriteBuffer` = `src/Disks/IO/CachedOnDiskWriteBufferFromFile.cpp`;
> `WBToSeg` = `src/Interpreters/FileCache/WriteBufferToFileSegment.cpp`.
> Files without a `src/` prefix are under `src/Interpreters/FileCache/`.
>
> **Global lock order (`Guards.h:20-53`)** — every row's 并发 column is relative to this:
> `CachePriorityGuard` (`SharedMutex`; Write⊃Read) **>** `CacheMetadataGuard` **>** `KeyGuard` **>** `FileSegmentGuard`.
> `CacheStateGuard` (plain `std::mutex`, counters) is **independent**, taken *after* the priority
> write lock is released to mutate size/element counters and offers a **timed** `tryLockFor`.
>
> Candidate structural deviations against the existing Velox port are surfaced (§3) for **D to
> adjudicate** — this ledger does NOT judge accept/reopen. 003–010 rows are settled in round-1
> (`port/task/fullview/home-chang/1/003-010-consumer-contract-ledger.md`); 011–014 is new ground.

---

## PART 1 — Per-Dependency Behavior Contract Tables

### Task 011 — Priority / Eviction

Consumed types: `IFileCachePriority` (+ `Iterator`/`IteratorPtr`/`Entry`), `LRUFileCachePriority`,
`SLRUFileCachePriority`, `SplitFileCachePriority`, `EvictionCandidates`, `CacheUsage`.
The four eviction entry paths are **query-limit** (`FileCache.cpp:1524`), **main reserve**
(`:1545`), **background keep-free-space** (`:1827`), **dynamic resize** (`:3088`).

**Method-name reconciliation (source truth, task brief was stale):** the real API is
`tryIncreasePriority` (not `increasePriority`), `incrementSize`/`decrementSize` (not `updateSize`),
`collectCandidatesForEviction`, `collectEvictionInfo`, `canFit`, `add`/`addForRestore`, `iterate`,
`removeEntries` (static). `EvictionCandidates` uses `add`/`evict`/`afterEvictWrite`/`afterEvictState`/
`removeQueueEntries` + a `hold_space` member — **there is no `setSpaceHolder`/`finalize` method**
("finalize" semantics = `afterEvictState` + dtor).

| 行为 Behavior | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 (逐字) | 所有权/生命周期 | 并发要求 (锁) | 持久化 | 允许的 Velox 替换 | 归属 task |
|---|---|---|---|---|---|---|---|---|---|
| `collectCandidatesForEviction` (all 4 paths) | `FileCache.cpp:1524,1545,1827,3088`; recursion `SLRU:312,332,355,396,431,474`, `Split:285,292,303` | `bool collectCandidatesForEviction(EvictionInfo&, FileCacheReserveStat&, EvictionCandidates&, InvalidatedEntriesInfos&, IteratorPtr reservee, EvictionCursor, size_t max_candidates_size, bool is_total_space_cleanup, const OriginInfo&, CachePriorityGuard&, CacheStateGuard&)` | each candidate → `Entry::Evicting` via `res.add`→`setEvictingFlag`; invalidated entries funnel to out-param | false ⇒ caller composes `"cannot evict enough space (stat: ...)"` `FileCache.cpp:1504`; internal `LOG_TEST "Failed to collect eviction candidates..."` `LRU:620`. Does not throw | `res`/`invalidated_entries` caller-owned; candidate ptrs alias `FileSegmentMetadata` living in KeyMetadata bucket | takes ReadLock internally (`LRU:561`); receives *guards* and up-locks as needed; `reservee` routes SLRU protected/probationary | — | interface port | 011 |
| `canFit` (probe, no state change) | `FileCache.cpp:1346,2543`; `Split:222`, `SLRU:127,133,134,136,157`, `LRU:802` | `bool canFit(size, elements, const CacheStateGuard::Lock&, IteratorPtr reservee=nullptr, const OriginInfo&={}, bool is_initial_load=false) const` | none | pure predicate, no throw | — | **`CacheStateGuard::Lock` MUST be held** | — | port | 011 |
| `add` (new zero/nonzero-size entry) | `FileCache.cpp:1417,2551`; `SLRU:171,172,192,193,568,748`, `Split:196` | `IteratorPtr add(KeyMetadataPtr, offset, size, const WriteLock&, const CacheStateGuard::Lock*, bool is_initial_load=false)` | new entry → `Active` (or `PreActive` for SLRU up/downgrade) | `LOGICAL_ERROR "Adding non-zero size entry without state lock..."` `LRU:137`; DEBUG `"Attempt to add duplicate queue entry to queue: {}"` `LRU:149`; `"Not enough space to add a new entry {}. Current state: {}"` `LRU:159` | returns `IteratorPtr` (shared); entry held by `std::list` node | **WriteLock**; nonzero size also needs `CacheStateGuard::Lock` | — | port | 011 |
| `addForRestore` (resize rollback only) | `FileCache.cpp:3210`; `Split:209`, `SLRU:176` | `IteratorPtr addForRestore(KeyMetadataPtr, offset, size, QueueEntryType original_queue_type, const WriteLock&, const CacheStateGuard::Lock*)` | SLRU routes `SLRU_Protected`→protected `SLRU:186`; base delegates to `add` | as `add` | as `add` | WriteLock + StateLock | — | port | 011 |
| `collectEvictionInfo` (+ build HoldSpace) | `FileCache.cpp:1357,1368`; `SLRU:690` | `EvictionInfoPtr collectEvictionInfo(size, elements, Iterator* reservee, bool is_total_space_cleanup, const OriginInfo&, const CacheStateGuard::Lock&)` | partial free space now available ⇒ builds `HoldSpace` placeholder | missing queue ⇒ `LOGICAL_ERROR "Eviction info for queue with id {} does not exist ({})"` `EvictionCandidates.cpp:152` | `EvictionInfoPtr` (unique) caller-owned; contains `HoldSpacePtr`, dtor releases | **`CacheStateGuard::Lock` MUST be held** | — | port | 011 |
| `tryIncreasePriority` (hot access) | `FileCache.cpp:1258` ← `FileSegment.cpp:1441` | `bool tryIncreasePriority(Iterator&, bool is_space_reservation_complete, CachePriorityGuard&, CacheStateGuard&)` | LRU: `splice` to tail. SLRU: probationary→protected only if `is_space_reservation_complete`; sets `Entry::Moving` | LRU non-blocking `tryWriteLock` fail ⇒ returns false `LRU:722`; SLRU fail ⇒ in-place fallback | Iterator borrowed | LRU `tryWriteLock` (may fail); SLRU phased write+state | — | port | 011 |
| `iterate` (dump / query limit / system) | `FileCache.cpp:2790`; `SLRU:202,203`, `Split:114,115` | `void iterate(IterateFunc, FileCacheReserveStat&, const CachePriorityGuard::ReadLock&)` | read-only; invalidated entries counted not removed | func throw ⇒ propagates | func borrows `LockedKey&` + `FileSegmentMetadataPtr` | **ReadLock** | — | port | 011 |
| `removeEntries` (static; cleanup + eviction) | `FileCache.cpp:1412,1435,1572,1735,1843,3136`; impl `IFileCachePriority.cpp:80` | `static void removeEntries(const vector<InvalidatedEntryInfo>&, const WriteLock&)` | each `Invalidated`→`remove`→`Removed`; `Removed` skipped | `chassert` state∈{Invalidated,Removed} else `"Unexpected state: {}"` `IFileCachePriority.cpp:95` | `entry` shared_ptr validates possibly-stale iterator | **WriteLock** | — | port | 011 |
| `Iterator::getEntry` (double-fetch race) | `FileSegment.cpp:1207,1215`; `FileCache.cpp:1384,1466`; `EvictionCandidates.cpp:558` | `EntryPtr getEntry() const` | none | LRU `assertValid` ⇒ `"Attempt to use invalid iterator (entry: {}, iterator: {})"` `LRU:845`; SLRU ⇒ `"Entry pointer expired"` `SLRU:902` | locks weak `entry`; throw if expired | LRU lock-free (weak+atomic); SLRU `entry_mutex` | — | port; **must re-fetch, not snapshot** (see §hole) | 011 |
| `Iterator::incrementSize` (reserve commit) | `FileCache.cpp:1381,1446,1449`; `SLRU:605,759` | `void incrementSize(size, const CacheStateGuard::Lock&)` | size 0→nonzero increments elements | `!canFit` ⇒ `LOGICAL_ERROR "Cannot increment size by {} for entry {}. Current state: {}"` `LRU:804` | — | **`CacheStateGuard::Lock` MUST be held** | — | port | 011 |
| `Iterator::decrementSize` | download-fail / truncate paths | `void decrementSize(size)` | empty ⇒ elements−1 | `chassert(size>=)` | — | **no cache lock needed** (`IFileCachePriority.h:177`) | — | port | 011 |
| `Iterator::invalidate` (soft delete) | `FileCache.cpp:1456`; `Metadata.cpp:1239`; `EvictionCandidates.cpp:190,395` | `void invalidate() noexcept` | →`Invalidated`, size→0, state->sub; Main queue registers `invalidated_refs`, may fire notifier | noexcept | entry stays in queue for background remove | lock-free (atomic exchange + `invalidated_mutex` on ref list) | — | port | 011 |
| `Iterator::invalidateBeforeRemove` | `EvictionCandidates.cpp:242` | `void invalidateBeforeRemove(const WriteLock&) noexcept` | →`Invalidated`, no `invalidated_refs` registration | noexcept | caller removes under same WriteLock | **WriteLock** | — | port | 011 |
| `Iterator::remove` (hard delete) | `EvictionCandidates.cpp:256`; `LRU:749` | `void remove(const WriteLock&)` | →`Removed`, `queue.erase` | — | node destroyed | **WriteLock** | — | port | 011 |
| `Iterator::getType`/`getNestedOrThis` | `EvictionCandidates.cpp:240,557`; `SLRU:131,267,369` | — | none | none | — | lock-free (atomic `is_protected`) | — | port; **unwrap Split→SLRU is load-bearing** | 011 |
| `EvictionCandidates::add` | `LRU:590` | `void add(const FileSegmentMetadataPtr&, LockedKey&)` | candidate → `Entry::Evicting` | — | stores `FileSegmentMetadataPtr` (shared); accrues bytes | holds `LockedKey` (KeyGuard) | — | port | 011 |
| `EvictionCandidates::evict` (**no cache lock** — deletes files) | `FileCache.cpp:1578,3133`; `SLRU:732` | `void evict()` | per-segment `removeFileSegment(..., invalidate_queue_entry=false)`; successes → `queue_entries_to_invalidate` (deferred invalidate under state lock) | not releasable ⇒ `LOGICAL_ERROR "Eviction candidate is not releasable: {} (evicting or removed flag: {})"` `:296`; fault `file_cache_dynamic_resize_fail_to_evict`⇒`FAULT_INJECTED "Failed to evict file segment"`; failures → `failed_candidates`, `LOG_ERROR "Failed to evict file segment ({}): {}"`, ProfileEvent `FilesystemCacheFailedEvictionCandidates`; on-evict callback exception swallowed `"On-evict callback failed; ignored"` | `key_metadata->tryLock()` per key | **no cache lock** (deliberate; file deletion is slow) | deletes cache files | port; **preserve hold-space split** (§hole) | 011 |
| `EvictionCandidates::afterEvictWrite` (mutate queue structure) | `FileCache.cpp:1411,1571`; `SLRU:736` | `void afterEvictWrite(const WriteLock&)` | runs write callbacks (SLRU downgrade splice) then clears | callback throw propagates | — | **WriteLock** | — | port | 011 |
| `EvictionCandidates::afterEvictState` (mutate counters + invalidate queue entries) | `FileCache.cpp:1445,1574`; `SLRU:758` | `void afterEvictState(const CacheStateGuard::Lock&)` | invalidates each `queue_entries_to_invalidate` | — | — | **`CacheStateGuard::Lock`** (freed space atomically substituted by reserver) | — | port | 011 |
| `EvictionCandidates::removeQueueEntries` (resize-only) | `FileCache.cpp:3111` | `void removeQueueEntries(const WriteLock&)` | stores `original_queue_types` (via `getNestedOrThis`); `invalidateBeforeRemove`→`markDelayedRemovalAndResetQueueIterator`→`setRemovedFlag`→`queue_iterator->remove`; sets `removed_queue_entries=true` | `chassert(candidate->releasable())` | — | **WriteLock** | — | port | 011 |
| `EvictionCandidates::getFailedCandidates`/`getOriginalQueueType`/dtor | `FileCache.cpp:1586,3138,3208` | `FailedCandidates getFailedCandidates() const`; `QueueEntryType getOriginalQueueType(const FileSegmentMetadata*)`; `~EvictionCandidates()` | dtor re-invalidates pending or `resetEvictingFlag` (unless `removed_queue_entries`) | `getFirstErrorMessage` empty ⇒ `chassert(false)` | dtor noexcept | — | — | port | 011 |
| `CacheUsage` (overcommit-only; base = no-op) | `FileCache.cpp:555` (touchClientAccess), `:1983` (collectIdleClients), `:2725` (getUsageStatPerClient) | base virtuals return `{}`/empty | — | — | `CacheUsagePerUser` = `ShardedMap<UserID, CacheUserData>`; `usage` declared after `priority` so it destructs first (safe shutdown, `CacheUsage.h:110-116`) | `CacheUsageStatGuard` = `std::mutex` (overcommit `check` atomicity only) | — | port only if overcommit ported (else **over-port**) | 011 |

**§3 CH internal structure (011 baseline):**
- **`IFileCachePriority::Entry`** (`IFileCachePriority.h:59-167`): `{const key, const offset, KeyMetadataWeakPtr, atomic<size_t> size, atomic<State> state}`; 7-state machine `Active/PreActive/Evicting/Moving/Invalidated/Removed` (+downgrade transients), each transition gated by *which lock* is held. Base holds `atomic max_size/max_elements`, `on_evict_callback`, `invalidate_notifier`.
- **`LRUFileCachePriority`**: `LRUQueue = std::list<EntryPtr>` (`LRU.h:169`). **Relies on `std::list` iterator stability + `splice`** (`LRU:655,731,865`) for all up/downgrade + SLRU inter-queue moves — splice never invalidates iterators. `State{atomic size, atomic elements_num}` shared as `StatePtr`. Eviction cursors `reserve_eviction_pos`/`background_eviction_pos` = two `list::iterator`s guarded by a **separate** `eviction_pos_mutex` (not the cache lock); `moveEvictionPosIfEqual` advances a cursor before remove/splice to prevent dangling. `invalidated_refs = std::deque<{weak_ptr<Entry>, list::iterator}>` + atomic count, `invalidated_mutex`.
- **`SLRUFileCachePriority`**: two `LRUFileCachePriority` value members (protected/probationary). `SLRUIterator` = `LRUIterator + weak_ptr<Entry>` (`entry_mutex`) + `atomic<bool> is_protected`. `setIterator` makes new-entry `setActiveFlag` + pointer update atomic w.r.t. `iterateImpl`.
- **`SplitFileCachePriority`**: routing layer over Data/System inner priorities; `is_total_space_cleanup` drains Data first then System.
- **`EvictionCandidates`**: `candidates = absl::flat_hash_map<FileCacheKey, KeyCandidates>` (`EvictionCandidates.h:187`); `original_queue_types = unordered_map<const FileSegmentMetadata*, QueueEntryType>` (**bare-pointer key**, relies on metadata address stability during candidacy). **Hold-space semantics**: `evict()` deletes files with `invalidate_queue_entry=false` so freed space is visible only to the current reserver until `afterEvictState` invalidates the queue entries under the state lock.

---

### Task 012 — Center SCC (FileCache / FileSegment / Metadata / QueryLimit)

Heaviest external consumers: `ReadBuffer`, `WriteBuffer`, `WBToSeg`, plus
`CachedObjectStorage.cpp`, `InterpreterSystemQuery.cpp`, `ServerAsynchronousMetrics.cpp`.

#### 012-A · `FileCache` public API

| 行为 | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 (逐字) | 所有权/生命周期 | 并发 (锁) | 归属 |
|---|---|---|---|---|---|---|---|
| `getOrSet` (read+create EMPTY) | `ReadBuffer:258,1839`; `WriteBuffer:276` | `FileSegmentsHolderPtr getOrSet(key, offset, size, file_size, settings, file_segments_limit, origin, boundary_alignment?)` `FileCache.h:147` | may create EMPTY segments that transition post-return | throws on init failure | returns `FileSegmentsHolderPtr` (unique) owning a list of `FileSegmentPtr` | `CacheMetadataGuard`→`KeyGuard` | 012 |
| `get` (read-only, no create) | `ReadBuffer:246,1829` | `FileSegmentsHolderPtr get(key, offset, size, file_segments_limit, settings, origin)` `FileCache.h:166` | no EMPTY→other guarantee (`.h:163`) | — | holder owns segments | same | 012 |
| `set` (explicit write) | `WriteBuffer:333` | `set(key, offset, size, settings, origin)` `FileCache.h:183` | creates EMPTY→DOWNLOADING on write | — | holder | metadata→key | 012 |
| `removeKeyIfExists` | `CachedObjectStorage.cpp:157`; `WriteBuffer:473` | `void removeKeyIfExists(key, user_id)` `.h:207` | segments→DETACHED | silent if absent | — | PriorityWrite→Key→FileSegment | 012 |
| `removeKey` | `InterpreterSystemQuery.cpp:626` | `void removeKey(key, user_id)` `.h:204` | →DETACHED | throws if absent | — | same | 012 |
| `removeAllReleasable` | `InterpreterSystemQuery.cpp:607,618` | `void removeAllReleasable(user_id)` `.h:213` | releasable→DETACHED | — | — | PriorityWrite, one KeyGuard at a time | 012 |
| `tryReserve` | `FileSegment.cpp:710` (sole caller) | `bool tryReserve(fs, size, stat, prio_settings, lock_wait_timeout_ms, ...)` `.h:230` | reserves via priority/eviction | false on no-space | — | Priority(Write/Read)→Key→(evict Keys)→CacheState | 012 |
| `initialize`/`isInitialized` | `CachedObjectStorage.cpp:36,102,132,156`; `FileCacheFactory.cpp:264`; `InterpreterSystemQuery.cpp:604,614`; `TemporaryDataOnDisk.cpp:338`; `FileCacheSettings.cpp:161` | `void initialize()` `.h:115`; `bool isInitialized() const` `.h:117` | — | `throwInitExceptionIfNeeded` `.h:120` | — | — | 012 |
| `applySettingsIfPossible` | `FileCacheFactory.cpp:211` | live settings migration | per-field value diff | — | — | resize lock | 012 |
| `getUsedCacheSize`/`getMaxFileSegmentSize` | `ServerAsynchronousMetrics.cpp:211`; `FileCacheSettings.cpp:162`; `WriteBuffer:333` | `size_t getUsedCacheSize() const` `.h:217`; inline `.h:222` | — | — | — | CacheStateGuard / lock-free | 012 |
| `getFileSegmentInfos`/`dumpQueue` | introspection (`.h:240,244`) | — | — | — | — | — | 012 |

**Over-port candidates (declared, no production caller found this sweep):** `getDownloadedContiguousOrEmpty` (`.h:177`), `trySet` (`.h:190`), `removeFileSegment`/`removeFileSegmentIfExists` (`.h:198,201`), `removePathIfExists` (`.h:210`), `tryGetCachePaths` (`.h:215`) — **D must confirm before accepting a Velox port of these**.

#### 012-B · `FileSegment` (downloader/reader handoff state machine)

State enum `FileSegmentState` = {EMPTY, DOWNLOADING, PARTIALLY_DOWNLOADED, PARTIALLY_DOWNLOADED_NO_CONTINUATION, DOWNLOADED, DETACHED}.

| 行为 | 触发调用点 file:line | 签名 | 状态转移 | 错误行为 (逐字) | 所有权/并发 | 归属 |
|---|---|---|---|---|---|---|
| `getOrSetDownloader` | `ReadBuffer:689,1222,1231`; `WriteBuffer:173`; `WBToSeg:63` | `String getOrSetDownloader()` `.h:125` | EMPTY/PARTIAL→DOWNLOADING (winner) | — | FileSegmentGuard; atomic election | 012 |
| `isDownloader` | `ReadBuffer:758,783,839,1013,1251,1368,1487,1542,1730`; `WriteBuffer:155,171` | `bool isDownloader() const` | — | — | read under lock | 012 |
| `resetDownloader` | `ReadBuffer:702` | `void resetDownloader()` `.h:204` | DOWNLOADING→PARTIALLY_DOWNLOADED / EMPTY (`:317,319`) | — | releases downloader | 012 |
| `completePartAndResetDownloader` | `ReadBuffer:1175,1373,1488`; `WriteBuffer:156,210` | `void completePartAndResetDownloader()` `.h:202` | commits written part, publishes state, wakes waiters `:853`; asserts `download_state==DOWNLOADING\|...` `:862` | — | releases downloader after `resetRemoteFileReader` if reader not reusable | 012 |
| `complete` | `FileSegmentsHolder`→`:1520`; def `:972,999` | `complete(FileSegmentPtr&&, allow_bg_download, force_shrink)` | →DOWNLOADED/PARTIALLY_DOWNLOADED (`:930,933`); no-op if detached | `"Cannot complete file segment: {}"` `:989`; `"Unexpected state while completing file segment"` `:1136` | KeyGuard→FileSegmentGuard | 012 |
| `write` | `ReadBuffer:1272`; `WriteBuffer:209`; `WBToSeg:109` | `void write(char* from, size_t size, size_t offset_in_file)` `:415` | requires DOWNLOADING | `"Writing zero size is not allowed"` `:422`; `"Expected DOWNLOADING state, got {}"` `:433`; `"Attempt to write {} bytes to offset: {}, but current write offset is {} ({})"` `:441`; `"Not enough space is reserved. Available: {}, expected: {}"` `:452`; `"File segment is already fully downloaded"` `:458`; `"Cannot download beyond file segment boundaries..."` `:462` | downloader-only | 012 |
| `reserve` | `ReadBuffer:1114,1616`; `WriteBuffer:197`; `WBToSeg:84` | `bool reserve(size, lock_wait_timeout_ms, failure_reason&, stat?, reserve_hint=0)` `.h:216` | delegates `cache->tryReserve` `:710` | `"Zero space reservation is not allowed"` `:638` | — | 012 |
| `wait` | `ReadBuffer:667` | `State wait(offset)` `:552` | blocks on DOWNLOADING | `"Cannot wait on a file segment with empty state"` `:564` | cancellable short-slice loop (**no fixed sleep**) | 012 |
| `getRemoteFileReader`/`setRemoteFileReader`/`extractRemoteFileReader`/`resetRemoteFileReader` | `ReadBuffer:516,521,537,1024,1174,1371,1740` | `.h:231,234,236,238` | reader attach/extract/withdraw; state-gated (extract), downloader-gated (reset) | `setRemoteFileReader` twice ⇒ `"Remote file reader already exists"` `:410` | shared via segment; invariant `reader.getFileOffsetOfBufferEnd()==getCurrentWriteOffset()` `ReadBuffer:525` | 012 |
| `setDownloadFinishedWithoutContinuation` | `ReadBuffer:1025,1741` | `.h:245` | →PARTIALLY_DOWNLOADED_NO_CONTINUATION `:829` | — | asserts `!download_data->remote_file_reader` `:828`; **reader withdrawn first** | 012 |
| `setDownloadFailed` | def `:812` | — | →failed/detached | — | — | 012 |
| `getCurrentWriteOffset`/`getDownloadedSize`/`range` | `ReadBuffer:568,706,843,...`; `WriteBuffer:109,116,161,184,228,313` | atomics; `const Range&` `.h:102` | — | ctor `"Attempt to create incorrect range: [{}, {}]"` `:137` | atomic (valid after detach) | 012 |
| `isDetached`/`isCompleted`/`assertNotDetached` | `ReadBuffer:924`; `WriteBuffer:287,385` | — | terminal | `"Cache file segment is in detached state, operation not allowed. It can happen when cache was concurrently dropped with SYSTEM DROP FILESYSTEM CACHE FORCE. Please, retry. File segment info: {}"` `:1478` | — | 012 |
| internal state-machine guards | via `setDownloadState`/`assertIsDownloaderUnlocked` | `:179,351` | terminal-state guard | `"Updating state to {} of file segment is not allowed, because it is already completed ({})"` `:184`; `"Queue iterator cannot be set twice"` `:208`; `"Operation \`{}\` can be done only by downloader. (CallerId: {}, downloader id: {})"` `:351` | FileSegmentGuard::Lock | 012 |

#### 012-C · `Metadata` (CacheMetadata / KeyMetadata / LockedKey)

| 行为 | 触发调用点 | 签名 | 所有权/并发 | 归属 |
|---|---|---|---|---|
| `lockKeyMetadata`/`getKeyMetadata` | intra FileCache.cpp (get/getOrSet/set) | `LockedKeyPtr lockKeyMetadata(...)` `.h:232`; `KeyMetadataPtr getKeyMetadata(...)` `.h:226` | takes KeyGuard via metadata lock | 012 |
| `removeKey` | FileCache::removeKey* | `void removeKey(key, if_exists, user_id)` `.h:238` | PriorityWriteLock | 012 |
| `iterate` | introspection/factory | `void iterate(IterateFunc&&, user_id)` `.h:212` | one KeyGuard at a time | 012 |
| `LockedKey::removeFileSegment` | FileSegment complete (`isLastOwnerOfFileSegment` `:884`) | `:391,397` | holds KeyGuard; on rename/remove seam also calls `OpenedFileCache::remove` (see 013-D2) | 012 |
| `removeEmptyKey`/`removeFileSegmentImpl` (reconciliation) | internal `:306,422` | — | KeyGuard | 012 — **confirm ported (hole-risk)** |

#### 012-D · `FileCacheQueryLimit`

| 行为 | 触发调用点 | 签名 | 并发 | 归属 |
|---|---|---|---|---|
| gate honored | `FileCache.cpp:421` (`enable_filesystem_query_cache_limit` ⇒ construct) | — | ctor | 012 |
| `tryGetQueryContext` | `FileCache.cpp:1342` (in tryReserve) | `QueryContextPtr tryGetQueryContext(CacheStateGuard::Lock&)` `:19` | read under CacheStateGuard | 012 |
| `getOrSetQueryContext`/`removeQueryContext` | QueryContextHolder ctor/dtor | `:21,30` (def `:33,69`) | write under CachePriorityGuard::WriteLock; doomed ptr destroyed after lock release | 012 |
| priority accounting / recache | `FileCache.cpp:1345,1347` | `query_context->getPriority()` | — | 012 |
| recache-disabled error | `FileCache.cpp:1351` | — | `"recache_on_query_limit_exceeded is disabled (while reserving for {}:{} with size {}): {}"` | 012 |

**§3 CH internal structure (012 baseline):**
- **`FileSegment`** atomics `download_state, downloaded_size, reserved_size, size_in_filename, hits_count, increasing_priority` (`FileSegment.h:300-337`). Lazy `struct DownloadState` (member `:313`) allocated on first download, freed at terminal state; holds `remote_file_reader` + `cache_writer`. `std::weak_ptr<KeyMetadata> key_metadata` (`:324`); `lock()` on expired ⇒ `"Cannot lock key, key metadata is not set ({})"` `:608`.
- **`KeyMetadata : private std::map<size_t, FileSegmentMetadataPtr>`** (`Metadata.h:98/124`) — **ordered, node-stable map** (offset→segment); iterator/reference stability across insert/erase relied on.
- **`CacheMetadata` index** = `MetadataBuckets = std::vector<MetadataBucket>` (`Metadata.h:276`), `MetadataBucket : std::unordered_map<FileCacheKey, KeyMetadataPtr>` (`:269`) — fixed `buckets_num` vector (no vector rehash); per-bucket map may rehash but values are `shared_ptr` (pointees stable).
- **Ownership**: KeyMetadata owns FileSegmentMetadata (owns FileSegment); FileSegment holds `weak_ptr` back. `FileSegmentsHolderPtr = unique_ptr<FileSegmentsHolder>` (list of `FileSegmentPtr`).
- **`FileCacheQueryLimit`**: `QueryContextMap = unordered_map<String, QueryContextPtr>` `query_map` read under CacheStateGuard, written under CachePriorityGuard (documented dual-lock `:86-88`).

---

### Task 013 — Factory / Manager + OpenedFileCache

#### 013-D0 · "FileCacheManager" concept (no CH class)

**CH has no `FileCacheManager` class.** The runtime-owner role is split between the global
`Context` and the `FileCacheFactory` singleton. The Velox port **adds** a `FileCacheManager` class
(`velox/ch/.../FileCacheManager.h`) as the runtime resource owner (worker pool + timekeeper +
scheduler + OpenedFileCache + the sole Factory). This is a **port design addition**, not a CH port —
D must adjudicate it as such (mirrors round-1 O-style "concept has no CH counterpart" ruling).

| CH mechanism | 触发调用点 file:line | Behavior |
|---|---|---|
| singleton ownership | `FileCacheFactory.cpp:43-47` (Meyers) | `caches_by_name` holds the only long-lived owning refs |
| bootstrap | `programs/server/Server.cpp:2408`; `programs/local/LocalServer.cpp:1617` (`loadDefaultCaches`); `RegisterDiskCache.cpp:109` (`getOrCreate`) | server startup + disk creation |
| shutdown order | `Context.cpp:1011-1020` | `deactivateBackgroundOperations()` on every unique instance **then** `FileCacheFactory::clear()`; ordering load-bearing (clear-before-catalog-shutdown would deadlock GlobalThreadPool) |

#### 013-D1 · `FileCacheFactory`

| 行为 | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 (逐字) | 所有权/生命周期 | 并发 (锁) | 归属 |
|---|---|---|---|---|---|---|---|
| `getOrCreate` (**dedup by path**) | `RegisterDiskCache.cpp:109`; `FileCacheFactory.cpp:263` | `getOrCreate(cache_name, settings, config_path) -> FileCachePtr` | linear path lookup; miss⇒construct+insert; hit different name⇒add name alias to same `FileCacheData` | `"Found more than one cache configuration with the same path, but with different cache settings ({} and {})"` / `"Cache with name {} exists, but it has a different path"` (both `BAD_ARGUMENTS`) | factory holds owning copy until clear/remove | `std::mutex mutex` held throughout | 013 |
| `get` | `StorageObjectStorageSource.cpp:1419` | `get(cache_name) -> FileCachePtr` | map lookup→`->cache` | `` "There is no cache by name `{}`" `` `BAD_ARGUMENTS` `:70` (**backtick punctuation**) | shared, not transferred | `mutex` | 013 |
| `getByName` | `CachedObjectStorage.cpp:126`; `Context.cpp:1942`; `InterpreterSystemQuery.cpp:612,670`; `InterpreterDescribeCacheQuery.cpp:40`; `StorageSystemDisks.cpp:102`; `StorageSystemRemoteDataPaths.cpp:382`; `checkDataPart.cpp:488`; `Server.cpp:3125` | `getByName(cache_name) -> FileCacheDataPtr` | returns whole `FileCacheData` | `"There is no cache by name: {}"` `BAD_ARGUMENTS` `:160` (**colon punctuation — differs from `get`**) | `shared_ptr<FileCacheData>` exposes cache/config_path/settings | `mutex` | 013 |
| `getAll` | `StorageSystemFilesystemCache.cpp:47`; `StorageSystemFilesystemCacheSettings.cpp:34`; `InterpreterShowTablesQuery.cpp:223` | `getAll() -> CacheByName` | returns map **copy** (incl. alias duplicates) | — | snapshot copy | `mutex` | 013 |
| `getUniqueInstances` | `Context.cpp:1011`; `InterpreterSystemQuery.cpp:602,662`; `ServerAsynchronousMetrics.cpp:209` | `getUniqueInstances() -> Caches` | dedups aliases | — | snapshot | `mutex` | 013 |
| `updateSettingsFromConfig` | **only** `Server.cpp:2843` | `updateSettingsFromConfig(config)` | per-config_path reload; force `path`=old; `applySettingsIfPossible(new,old)` if changed | apply throw ⇒ rollback `setSettings(old)` + `tryLogCurrentException` + **rethrow** (`:213-220`) | mutates `FileCacheData::settings` only | copy under `mutex`; apply under per-data `settings_mutex` (not factory mutex) | 013 |
| `loadDefaultCaches` | `Server.cpp:2408`; `LocalServer.cpp:1617` | `loadDefaultCaches(config, context)` | iterate `filesystem_caches.*`→getOrCreate→initialize | propagates | initial owning entries | via getOrCreate | 013 |
| `clear` | `Context.cpp:1020` | `clear()` | `caches_by_name.clear()` | — | releases all owning `FileCacheData` | `mutex` | 013 |
| `create` / `remove(FileCachePtr)` | **no production caller** (`.h:51,65`) | — | — | `create`: `"Cache with name {} already exists"` `BAD_ARGUMENTS` `:125` | — | `mutex` | 013 — **over-port candidate** |

#### 013-D2 · `OpenedFileCache` (Task 013 D1 port source)

| 行为 | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 | 所有权/生命周期 | 并发 (锁) | 归属 |
|---|---|---|---|---|---|---|---|
| `get` (open-or-share FD) | `ReadBufferFromFile.h:100`; `AsynchronousReadBufferFromFile.h:35` | `get(path, flags) -> OpenedFilePtr` | `CityHash64(path)%1024` bucket→`emplace(key, weak{})`→hit if `weak.lock()`; else `new OpenedFile` with deleter, backfill weak | `OpenedFile` ctor may throw on open | returns `shared_ptr<OpenedFile>`; map stores **weak_ptr**; last shared_ptr dtor deleter `erase(key)` then `delete` | per-bucket `std::mutex`, held throughout; deleter re-takes same lock | 013 |
| `remove` (invalidate stale FD; paired flags) | `Metadata.cpp:1267-1268`; `FileSegment.cpp:801-802` (both call `flags` **and** `flags\|O_DIRECT`) | `remove(path, flags)` | bucket→`files.erase(key)` | — | only removes weak entry; existing shared holders keep their FD (inode unchanged) | per-bucket `mutex` | 013 |
| `instance` | all callers | `static OpenedFileCache& instance()` | Meyers singleton | — | static lifetime, never destroyed | thread-safe static-local | 013 |

**§3 CH internal structure (013 baseline):**
- **`FileCacheFactory`**: `std::unordered_map<std::string, shared_ptr<FileCacheData>> caches_by_name` + `std::mutex`. **Dedup key = path, not name** (aliases share one `shared_ptr`). `FileCacheData` has its own `settings_mutex` (`cache`/`config_path` are `const`). Node-stable via `shared_ptr` indirection.
- **`OpenedFileCache`**: `VectorWithMemoryTracking<OpenedFileMap> impls{1024}`; each `OpenedFileMap` = `MapWithMemoryTracking<Key, weak_ptr<OpenedFile>>` + `std::mutex`. `Key = std::pair<std::string path, int flags>`; bucket hashed by **`CityHash64(path)` only** (flags not in hash). **It is NOT a size-bounded LRU** — no capacity, no eviction order; entries self-clear via weak_ptr + deleter, plus explicit `remove`. Invalidation protocol: (1) refcount→0 deleter, (2) explicit `remove` (paired with/without `O_DIRECT`).

---

### Task 014 — DWIO reader / downloader handoff (`CachedOnDiskReadBufferFromFile`)

CH baseline only — the Velox reader port does **not exist yet** (this is the handoff task; deviation
column is empty by construction). `enum class ReadType {CACHED, REMOTE_FS_READ_BYPASS_CACHE, REMOTE_FS_READ_AND_PUT_IN_CACHE, NONE}` (`ReadBuffer.h:67-73`).

| 行为 | 触发调用点 file:line | 签名/重载 | 状态转移 | 错误行为 (逐字) | 所有权/生命周期 | 并发 | 归属 |
|---|---|---|---|---|---|---|---|
| decide read type per segment | `ReadBuffer:577,617` | `createReadFromFileSegmentState(FileSegment&, offset, ReadInfo&, log)` | DETACHED→BYPASS `:647`; DOWNLOADING→CACHED-if-`canStartFromCache` else `wait(offset)` loop `:653-669`; DOWNLOADED→CACHED `:670`; EMPTY/PARTIAL→CACHED or (win downloader) REMOTE_AND_PUT+predownload `:674-729`; NO_CONTINUATION→CACHED-or-BYPASS `:731-742` | temp-cache-only unreadable ⇒ `TEMPORARY_DATA_NOT_IN_CACHE "Temporary data is no longer present in the cache (cache-only read of [{}, {}) for key {}): the data was removed (eviction, expiry, writer failure or server restart). The query has to be retried"` `:59-68` | returns `ReadFromFileSegmentStatePtr` (unique) holding shared `buf` | `getOrSetDownloader`/`wait` block; single-downloader election `:689` | 014 |
| `canStartFromCache` | `ReadBuffer:559-570` | `static bool canStartFromCache(offset, const FileSegment&)` | `current_write_offset > current_offset` | — | read-only | lock-free | 014 |
| win/give-back downloader | `ReadBuffer:689,702,1373,1488` | `getOrSetDownloader`/`resetDownloader`/`completePartAndResetDownloader` | EMPTY/PARTIAL→DOWNLOADING; commit+publish+wake | — | exclusive writer for the term | atomic election; term = 1 `nextImpl` step (`:1218-1231`) except `readBigAt` (whole call, `:1917-1931`) | 014 |
| remote reader reuse/extract | `ReadBuffer:516,521,532-547` | `getRemoteFileReader`/`setRemoteFileReader`/`extractRemoteFileReader` | reuse iff `offset==reader.getFileOffsetOfBufferEnd()` else fresh | — | extracted reader moved into buffer-local `info.remote_file_reader` (non-shareable) | "cannot directly check info.remote_file_reader because of possible race with background downloader" `:536` | 014 |
| **withdraw-before-publish** ordering | `ReadBuffer:1024,1174,1371,1740` | `resetRemoteFileReader` **before** `setDownloadFinishedWithoutContinuation`/`completePartAndResetDownloader` | detach reader before state publish | rationale verbatim `:1015-1023` | keeps reader exclusively ours until dropped | not type-enforced — hand-maintained across 4 sites (**hole**) | 014 |
| `predownload` loop | `ReadBuffer:934,988-1190` | `predownloadForFileSegment(...)` | read source→reserve→writeCache; `bytes_to_predownload-=size` | reserve fail ⇒ bypass, `LOG_DEBUG "Predownload failed because of space limit ({}). Will read from remote filesystem starting from offset: {}"` `:1182`; truncation ⇒ `CANNOT_READ_ALL_DATA "Remote object was truncated between listing and reading: actual object size {}, expected object size {}, ..."` `:1051`; generic ⇒ `LOGICAL_ERROR "Failed to predownload remaining {} bytes. ..."` `:1063`; offset mismatch ⇒ `LOGICAL_ERROR "Buffer's offsets mismatch after predownloading; ..."` `:1088` | — | — | 014 |
| writeCache disk failure | `ReadBuffer:1260-1301,1272` | — | asserts state==NO_CONTINUATION `:1280,1283` | ENOSPC(28)/EDQUOT(122) ⇒ `LOG_INFO "Insert into cache is skipped due to insufficient disk space. ({})"` return false `:1277`; skip-on-disk-failure ⇒ `LOG_ERROR "Insert into cache is skipped due to disk IO error. ({})"` `:1286`; else `CACHE_CANNOT_WRITE_TO_CACHE_DISK "Filesystem cache disk IO error (errno {}): {}. Consider setting skip_cache_on_disk_failure=true in cache config."` `:1289` | — | downloader-only | 014 |
| nextImpl error context + cleanup | `ReadBuffer:1303-1385,1443-1463` | — | SCOPE_EXIT keyed on `uncaught_exceptions()>on_entry` `:1354`; if downloader & reader not reusable ⇒ `resetRemoteFileReader` then `completePartAndResetDownloader` `:1368`; final `chassert(!isDownloader())` `:1379` | `e.addMessage("Cache info: {}", ...)` `:1311`; zero-read-unfinished ⇒ `CANNOT_READ_ALL_DATA` (truncation) `:1743` or `LOGICAL_ERROR "Having zero bytes, but range is not finished: ..."` `:1755` | inner SCOPE_EXIT drops reader ref on throw `:1459` | 014 |
| self-heal on external truncation | `ReadBuffer:456-472,600-601` | `getCacheReadBuffer` returns `nullptr` (bypass) | CACHED→BYPASS silently | `LOG_WARNING "Cache file {} is shorter than its recorded size ({} < {}); it was likely truncated outside ClickHouse. Bypassing the cache; the data will be re-fetched from the source"` `:459`; empty file ⇒ `LOGICAL_ERROR "Attempt to read from an empty cache file: {}"` `:474` | — | — | 014 — **hole-risk if dropped** |
| `seek` | `ReadBuffer:2000-2052` | `off_t seek(off_t, int whence)` | in-buffer if within `working_buffer` else full reset (`info.reset(); state.reset(); initialized=false`) | not-first & `!allow_seeks_after_first_read` ⇒ `CANNOT_SEEK_THROUGH_FILE "Seek is allowed only before first read attempt from the buffer. Current offset {}, seek offset: {}"` `:2007`; bad whence ⇒ `ARGUMENT_OUT_OF_BOUND "Expected SEEK_SET or SEEK_CUR as whence"` `:2019` / `"Only SEEK_SET allowed"` `:2040` | — | — | 014 |
| `setReadUntilPosition` | `ReadBuffer:796,2070-2086` | `void setReadUntilPosition(size_t)` | resets + `initialized=false` | initialized & no-seek ⇒ ``LOGICAL_ERROR "Method `setReadUntilPosition()` not allowed"`` `:2073` | — | — | 014 |
| `getFileOffsetOfBufferEnd`/`getPosition` | `ReadBuffer.h:57`; `:2093` | inline / `file_offset_of_buffer_end - available()` | — | — | — | — | 014 |
| segment advance + dtor | `ReadBuffer:875-932,1492` | `completeFileSegmentAndGetNext` / dtor | `file_offset>range.right` ⇒ `completeAndPopFront(allow_background_download, false)` `:898`, refill batch; dtor completes front if not done `:924` | — | idempotent via `!front().isCompleted()` guard `:924` | 014 |

**§3 CH internal structure (014 baseline):**
- **Buffer state**: `read_until_position`, `file_offset_of_buffer_end`, `implementation_buffer`, `read_type`, current segment iterator into `FileSegmentsHolder`, `predownload_memory` (only when `internalBuffer().size()<DBMS_DEFAULT_BUFFER_SIZE`). `ReadInfo` holds reusable shared `remote_file_reader` + `cache_file_reader`.
- **Ownership**: `FileSegmentsHolderPtr` (unique) owns segments; remote reader shared via segment, or extracted to buffer-local non-shareable for bypass.
- **Concurrency/scheduling**: single-downloader per term; background-download enqueue **delegated** to `FileSegmentsHolder`/`allow_background_download` (`:898`) — the reader does not itself schedule. Boundary alignment via `cache_settings.boundary_alignment` passed to `getOrSet` (`:266`); last-segment working-buffer resize `:1664-1677`.

---

## PART 2 — Coverage Matrix (call site → required behavior → owning task)

| Dep (task) | CH consumer call sites (anchors) | Required behavior | Velox impl exists? | Hole? | Over-port? | Owning task |
|---|---|---|---|---|---|---|
| 011 IFileCachePriority | `FileCache.cpp:1258,1346,1357,1381,1417,1524,1545,1827,3088,3210`; `FileSegment.cpp:1207,1215,1441` | collect/canFit/add/collectEvictionInfo/tryIncreasePriority/iterate/removeEntries/Iterator ops | ✓ | getEntry double-fetch; PreActive visibility | — | 011 |
| 011 LRU/SLRU/Split | `SLRU:*`, `Split:*`, `LRU:*` (intra) | queue movement/splice, protected/probationary, routing | ✓ | — | SLRU PreActive/Moving 2-phase may be simplifiable under single-lock | 011 |
| 011 EvictionCandidates | `FileCache.cpp:1411,1445,1571,1574,1578,3111,3133,3138,3208`; `LRU:590`; `SLRU:732,736,758` | add/evict(no-lock)/afterEvictWrite/afterEvictState/removeQueueEntries/hold-space | ✓ | hold-space split; bare-ptr `original_queue_types` | — | 011 |
| 011 CacheUsage | `FileCache.cpp:555,1983,2725` | overcommit client tracking (base no-op) | ✓ | — | **whole chain if no overcommit** | 011 |
| 012 FileCache API | `ReadBuffer:246,258,1829,1839`; `WriteBuffer:276,333,473`; `CachedObjectStorage.cpp:36,157`; `InterpreterSystemQuery.cpp:604,607,614,618,626`; `FileSegment.cpp:710` | getOrSet/get/set/remove*/tryReserve/init | ✓ | — | getDownloadedContiguousOrEmpty, trySet, removeFileSegment*, tryGetCachePaths, removePathIfExists | 012 |
| 012 FileSegment | `ReadBuffer:516-1741`; `WriteBuffer:109-385`; `WBToSeg:63,84,109` | downloader/reader handoff, write, reserve, wait, complete, state machine | ✓ | getSizeForBackgroundDownload public wrapper | — | 012 |
| 012 Metadata | intra FileCache.cpp; `FileSegment.cpp:884`; `Metadata.cpp:1239,1267` | lockKeyMetadata/removeKey/iterate/removeFileSegment/reconciliation | ✓ | removeEmptyKey/removeFileSegmentImpl | — | 012 |
| 012 QueryLimit | `FileCache.cpp:421,1342,1345,1347,1351` | gate/getOrSet-context/accounting/recache-error | ✓ | — | — | 012 |
| 013 Factory | `RegisterDiskCache.cpp:109`; `FileCacheFactory.cpp:211,263`; `Context.cpp:1011,1020,1942`; `Server.cpp:2408,2843,3125`; `InterpreterSystemQuery.cpp:602,612,662,670`; system storages | getOrCreate(path-dedup)/get/getByName/getAll/getUniqueInstances/updateSettingsFromConfig/clear | ✓ | punctuation-verbatim errors; path-vs-name dedup | `create`, `remove(FileCachePtr)` (no caller) | 013 |
| 013 Manager concept | (no CH class; `Context`+`Factory`) | runtime ownership + shutdown order | ✓ (port-added class) | — | port-design addition (not a CH port) — D adjudicates | 013 |
| 013 OpenedFileCache | `ReadBufferFromFile.h:100`; `AsynchronousReadBufferFromFile.h:35`; `Metadata.cpp:1267`; `FileSegment.cpp:801` | get(path,flags)/remove(paired O_DIRECT)/weak-ptr self-clear | ✓ | paired-O_DIRECT remove; rename seam | "LRU with eviction" over-port (it is NOT a bounded LRU) | 013 |
| 014 reader handoff | `ReadBuffer:246-2093` (see 014 table) | read-type SM, downloader handoff, withdraw-before-publish, predownload, reconcile, seek, self-heal | ✗ (not ported yet) | withdraw-before-publish; readBigAt whole-call downloader; self-heal-on-truncation | massive diag strings, predownload_memory, S3 plumbing | 014 |

---

## PART 3 — 结构偏离台账 (Candidate Structural Deviation Ledger — surfaced for D)

READ-ONLY surfacing. "Velox 替代" is grep-observed from the existing port; "判定" is left to **D**.
Node-stability rule of thumb (§3): `std::map`/`std::list`/`std::unordered_map` are node-stable
(pointees/iterators survive rehash/insert of others); `folly::F14FastMap` **relocates values** on
rehash. Deviation = *guarantee* change, not type-name change.

| # | CH 结构 | Velox 替代 (grep-observed) | 保证差异 | 硬约束出处? | E 探针证据 | 判定 (D 裁决) |
|---|---|---|---|---|---|---|
| **SD-011-1** | `EvictionCandidates::original_queue_types = std::unordered_map<const FileSegmentMetadata*, QueueEntryType>` (bare-ptr key) | `std::unordered_map<...>` **preserved** (`EvictionCandidates.h:230`) | none (kept) | n/a | matches | likely accept — verify ptr-key lifetime still bounded by candidacy |
| **SD-011-2** | `EvictionInfo` / `candidates` map keyed by QueueID / FileCacheKey (CH `absl::flat_hash_map` for candidates) | `EvictionInfo : folly::F14FastMap<QueueID, QueueEvictionInfoPtr>` (`:79`); `candidates : folly::F14FastMap<FileCacheKey, KeyCandidates>` (`:224`); `kept_alive_cache_usage : folly::F14FastSet` (`:141`) | F14 relocates values on rehash; but values are `Ptr`/by-value structs not aliased across mutation | **no hard constraint** (guide §3 → needs sign-off, cf. round-1 009-1) | port comment `:125` notes F14 lacks `merge` (insert-range workaround) | **candidate deviation** — D: confirm no `KeyCandidates&`/iterator escapes across insert into `candidates` |
| **SD-011-3** | `LRUQueue = std::list<EntryPtr>` + `splice` + iterator-stable cursors | `LRUQueue = std::list<EntryPtr>` **preserved** (`LRU.h:187`); cursors `std::deque invalidated_refs` preserved | none (kept) — the load-bearing splice/iterator-stability is respected | n/a | matches | likely accept (correct §3 literal translation) |
| **SD-011-4** | SLRU `PreActive`/`Moving` + 2-phase write/state callbacks (born from CH dual-lock priority/state separation) | port mirrors CH 2-phase (not simplified) | none if faithfully mirrored | n/a | (needs D read) | accept-if-faithful; note simplification was *possible* but not taken — no deviation |
| **SD-012-1** | `MetadataBucket : std::unordered_map<FileCacheKey, KeyMetadataPtr>` (node-stable) | `MetadataBucket : folly::F14FastMap<FileCacheKey, KeyMetadataPtr, FileCacheKeyHash>` (`Metadata.h:331`, **self-labelled "SD4"**) | F14 rehash relocates the `KeyMetadataPtr` value; pointee `KeyMetadata` is `shared_ptr` (stable), but any escaped `KeyMetadataPtr&`/iterator held across a bucket mutation dangles | **no hard constraint** (`std::unordered_map` compiles in Velox) — same class as round-1 009-1 | port comment `:328-331`: "iterators/mapped-value references never escape while holding a [lock]" — an *asserted* invariant, not proven | **candidate deviation (headline)** — D must enforce the "no ref/iterator escapes across mutation" invariant on every `MetadataBucket` consumer (mirrors 009-1 sign-off discipline) |
| **SD-012-2** | `KeyMetadata : private std::map<size_t, FileSegmentMetadataPtr>` (ordered, node-stable) | `KeyMetadata : private std::map<...>` **preserved** (`Metadata.h:124`) | none (kept) | n/a | matches | accept (correct §3 literal translation) |
| **SD-012-3** | `FileCacheQueryLimit::query_map = std::unordered_map<String, QueryContextPtr>` | (verify — likely F14 in port) | if F14: relocates value; value is `Ptr` | none | **NOT YET READ** — D verify QueryLimit.h container | flag for D to check |
| **SD-013-1** | `FileCacheFactory::caches_by_name = std::unordered_map<std::string, FileCacheDataPtr>` + `std::mutex` | `CacheByName = folly::F14FastMap<std::string, FileCacheDataPtr>`; `Caches = folly::F14FastSet<FileCacheDataPtr>` (`FileCacheFactory.h:85-86`) | F14 relocates values; values are `shared_ptr` (pointee stable); `getAll` returns a copy so no ref escapes | **no hard constraint** | matches round-1 009-1 class | **candidate deviation** — likely benign (shared_ptr + copy-out), D confirm no `FileCacheDataPtr&` escapes |
| **SD-013-2** | `OpenedFileCache` bucket hash = `CityHash64(path)` | port uses `folly::hash::fnv64_buf(path)` (`OpenedFileCache.h:191`, **self-noted `:61`**) | different bucket distribution only; **not a persistence/bit-compat surface** (in-memory FD table) | none (self-noted "no bit-compat requirement") | port comment `:54-61` | accept (in-memory only; hash choice irrelevant to correctness) |
| **SD-013-3** | `OpenedFileCache` = weak_ptr self-clearing table (no capacity, no LRU eviction order) | port mirrors weak_ptr + deleter (`OpenedFileCache.h:73-118`) | none if mirrored | n/a | port comment confirms deleter-holds-weak_ptr-to-shard | accept — **but D must confirm the port did NOT invent a bounded-LRU** (the name "OpenedFileCache"/"D1 LRU" invites over-port; the CH source has no eviction policy) |
| **SD-013-4** | (no CH class) `FileCacheManager` runtime-owner | port **adds** `FileCacheManager` (`FileCacheManager.h`) owning worker pool + scheduler + timekeeper + OpenedFileCache + Factory | additive concept; CH splits this across `Context`+`Factory` | n/a (design addition) | port header docstring | **D adjudicates as port-design addition** (not a CH-port deviation; check shutdown order mirrors `Context.cpp:1011-1020`) |
| **014** | (reader not ported yet) | — | — | — | — | deferred — deviation table populated when Velox reader lands |

---

## PART 4 — GATES

**Gate rule (§A hard):** 有行为但无任何调用点 → over-port candidate (delete); 有调用点但无映射行为 → hole (stop, fill).

### Candidate HOLES (CH call site exists → weak/missing/subtle Velox mapping) — for D
- **H-011-a `getEntry` double-fetch race** (`FileSegment.cpp:1207-1217`): SLRU downgrade invalidates the old entry immediately after `setIterator`, so consumers **must re-fetch** the entry; a single-snapshot port reads `size=0` false-mismatch.
- **H-011-b hold-space split** (`EvictionCandidates.cpp:318-336`): `evict()` deletes files with `invalidate_queue_entry=false`; freed space stays invisible to other threads until `afterEvictState`. Collapsing this into one step re-introduces the double-reserve bug.
- **H-011-c `PreActive` visibility**: `iterateImpl` must treat `PreActive` as non-evictable across an unlocked atomic read (`LRU:352-360`).
- **H-012-a `getSizeForBackgroundDownload`**: only the `...Unlocked` variant grep-confirmed; verify the public wrapper is ported (`FileSegment.h:194`).
- **H-012-b Metadata reconciliation**: `removeEmptyKey`/`removeFileSegmentImpl` (`Metadata.h:306,422`) internal cleanup — confirm ported.
- **H-013-a paired-`O_DIRECT` remove**: `OpenedFileCache::remove` must be called for **both** `flags` and `flags|O_DIRECT` (`Metadata.cpp:1267-1268`, `FileSegment.cpp:801-802`); dropping the O_DIRECT variant lets a stale FD serve old bytes.
- **H-013-b rename seam**: `FileSegment.cpp:788-802` relies on `remove(old_path)` after rename; `removeFileSegmentImpl` only clears `new_path`. Hidden cross-Segment/Factory invalidation protocol.
- **H-013-c error-text punctuation**: `get` uses `` `{}` `` (backticks), `getByName` uses `: {}` (colon) — verbatim-divergent; any oracle asserting CH text must match the exact method.
- **H-014-a withdraw-before-publish** (`ReadBuffer:1015-1023`, 4 sites): not type-enforced; a port must replicate exactly or race on the shared remote reader.
- **H-014-b `readBigAt` whole-call downloader** (`ReadBuffer:1917-1931`): holds downloader for the entire call, unlike the per-step release in `nextImplStep`; mirroring per-step release logic-errors.
- **H-014-c self-heal on external truncation** (`ReadBuffer:456-472,600-601`): silently converts CACHED→BYPASS (returns `nullptr`, not throw); dropping it turns cache corruption into read failures / wrongly-detached MergeTree parts.

### Candidate OVER-PORTS (Velox behavior → no CH call site) — for D
- **O-012**: `FileCache::getDownloadedContiguousOrEmpty`, `trySet`, `removeFileSegment`/`removeFileSegmentIfExists`, `removePathIfExists`, `tryGetCachePaths` — no production caller found this sweep.
- **O-013-a**: `FileCacheFactory::create`, `remove(FileCachePtr)` — declared, zero production/test callers.
- **O-013-b (headline)**: any bounded-LRU / capacity / eviction-order logic in the ported `OpenedFileCache` — the CH source has **none** (weak_ptr self-clear only). The "D1 LRU" naming invites this over-port.
- **O-011**: entire `CacheUsage`/`CacheUsagePerUser`/`CacheUsageStatGuard`/`EvictionInfo::kept_alive_cache_usage` chain if the Velox deployment does not use the overcommit policy (base virtuals are all no-op).
- **O-014**: byte-identical replication of the giant `LOGICAL_ERROR` diagnostic field-lists (`ReadBuffer:1755-1795`), `predownload_memory` small-buffer optimization, S3-specific `getReadUntilPosition`/`getStopReason` — port the *classification* (truncation⇒retryable `CANNOT_READ_ALL_DATA` vs bug⇒`LOGICAL_ERROR`), not the plumbing.

### Candidate structural deviations needing human sign-off (§3) — for D
**SD-012-1** (`MetadataBucket` → F14FastMap, headline — same class as round-1 009-1), **SD-011-2** /
**SD-013-1** (F14 in EvictionInfo/candidates/Factory registry — benign iff shared_ptr/by-value + no
ref escape, D confirm), **SD-013-4** (`FileCacheManager` port-added concept), **SD-013-3** (confirm
OpenedFileCache did not grow a bounded-LRU). **SD-012-3** (QueryLimit container) is **unverified** —
D must read `QueryLimit.h` to classify.

---

*End of ledger. Round-2 §A/§3 baseline for Tasks 011–014. Accept/reopen adjudication is D's job.*
