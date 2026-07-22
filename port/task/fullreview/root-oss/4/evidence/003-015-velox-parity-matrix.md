# Velox parity matrix for accepted Tasks 003-015 (Phase B)

READ-ONLY audit. Oracle = CH `src/` (Phase-A ledger `003-015-ch-consumer-contract-ledger.md`,
64 rows). Velox citations are current `file:line` at branch `filecache2`, HEAD `398426810`
under `/home/chang/OpenSource/velox/velox/ch/`. Approval provenance cited from
`root-oss/1/003-010-review-decisions.md` (B1/B2, SD1-SD9, D10, E1-E5, F-CALLERID, SD8) and
`home-chang/2/011-014-review-decisions.md` (014 F-014-1 fixed `e142429ef`; 011/012/013 pass;
F-014-2 registered; SD4 proven satisfied). NO independent Task-015 decision file exists on this
machine — 015 rows carry provenance "no independent 015 decision file" and are graded honestly.

Guarantee rule applied: a different container/lock/state representation is EQUIVALENT/MATCH only
if the substitution is in the signed deviation ledger (SDx / Bx / Dx); otherwise UNPROVEN.

**Evidence-timeliness note (Phase C, `root-oss/4`):** rows R-014-8, R-014-13, and R-015-1 had
their evidence columns refreshed after Phase C found the original green basis rested on a
pre-fix snapshot. Two Task-014 read-path defects were fixed after the round-2 acceptance
baseline and are present at the declared HEAD `398426810`: `01c007abe` (`SkipInt64` cross-segment
desync; old `SkipAcrossSegmentBoundary` was false-green, 3 real cross-segment skip tests added)
and `006a15996` (CACHED reader freeze on a still-downloading segment; crashed 6/22 TPC-H queries;
`CachedReaderRefillsWhenDownloadingSegmentGrows` added). The MATCH grades hold on the post-fix
HEAD; only the cited evidence (including the E2E test count, corrected 17->21) was updated. Status
values and the 64/57/7 counts are unchanged.

## Main parity table

| row_id | task | surface | CH contract summary + citation | Velox implementation + citation | focused test evidence | mutation/RED evidence | status | guarantee difference | severity | action/reopen owner |
|---|---|---|---|---|---|---|---|---|---|---|
| R-006-1 | 006 | caller/TID scope | stable per-thread `getCallerId` for downloader election; `FileSegment.cpp:254` | `FileSegment::getCallerId`->`FileCacheQueryIdScope::getCallerId` `FileSegment.cpp:225-227`, `FileCacheQueryIdScope.cpp:50-66` returns `"<qid>:<tid>"` / `None:<name>:<tid>` | `SchedulerAndScopeTest.cpp`, election used in `FileSegmentTest`/`FileCacheE2ETest` | — | MATCH (identity correctness) | Identity carried by OS tid (unique) — equivalent. Diagnostic format `None:<threadname>:<tid>` intentionally not fully restored | Minor | none (F-CALLERID diag = Task 017 forward, per round-1 §6) |
| R-008-1 | 008 | leaf enum FileSegmentState | 6-state enum, ordering is wire/log contract; `FileSegmentInfo.h:10-42` | `FileSegmentInfo.h:29-61` — identical enumerators+order, `uint8_t` | `FileSegmentInfoTest.StateEnumLayout` | layout test asserts ordinals | MATCH | none | — | — |
| R-008-2 | 008 | leaf enum FileSegmentKind | `{Regular,Ephemeral}`, Ephemeral=>unbound; `FileSegmentInfo.h:44-59` | `FileSegmentInfo.h:63-77` identical | `FileSegmentInfoTest.KindEnumLayout/KindToString` | — | MATCH | Ephemeral removal path is Task-016 write consumer (excluded); enum surface matches | — | — |
| R-008-3 | 008 | leaf struct FileSegmentInfo | 15-field introspection snapshot; `FileSegmentInfo.h:63-80` | `FileSegmentInfo.h:83-101` — same field set+types | `FileSegmentInfoTest.InfoSnapshotFieldsPresent` | — | MATCH | none | — | — |
| R-008-4 | 008 | Range type | inclusive interval, `size/contains/operator</==`; `FileSegment.h:74-92` | `FileSegment.h:97-112` byte-identical semantics | `LeafTypesTest` | — | MATCH | none | — | — |
| R-010-1 | 010 | instance settings defaults | LIST_OF_FILE_CACHE_SETTINGS names+defaults; `FileCacheSettings.cpp:36-74` | `FileCacheSettings.h:37-92` defaults match CH exactly (`maxSize=0`, `cacheOnWriteOperations=false`, etc.) | `FileCacheSettingsTest` | — | MATCH | `cache_on_write_operations`/overcommit policies fail-fast if set (Task-016 write & B2 overcommit exclusions); default values identical | — | none (write-through=016, overcommit excluded by B2) |
| R-010-2 | 010 | read options fields | `temp_cache_only`,`read_if_exists...`,`segments_batch_size`,`boundary_alignment`,`allow_background_download`,`reserve...timeout`; `CachedOnDiskReadBufferFromFile.cpp:231…` | `FileCacheReadOptions.h`; consumed in `FileCacheInputStream.cpp:282-340` (batch routing), `:493-519` | `FileCacheBufferedInputTest`, `FileCacheE2ETest.CacheOnlyMissFails/ReadIfExistsBypassMode` | E2E bypass/temp-only discriminating asserts | MATCH | none | — | — |
| R-010-3 | 010 | write options fields | `throw_on_error_from_cache`,`enable_filesystem_cache_log`,reserve timeout; `CachedOnDiskWriteBufferFromFile.cpp:441-445` | write-through path (`WriteBufferToFileSegment`) is Task-016; read-path uses `skipCacheOnDiskFailure` `FileCacheSettings.h:79` | — | — | INTENTIONAL_DEVIATION | write-path consumer deferred to Task 016 (ledger fwd-obligation); read-path fields present | Minor | Task 016 |
| R-011-1 | 011 | increasePriority | best-effort LRU/SLRU bump, `increasing_priority` atomic dedup, hits++; `FileSegment.cpp:1420-1448` | `FileSegment.cpp` increasePriority + `tryIncreasePriority` under priority lock; `LRU/SLRU/SplitFileCachePriority.cpp` | `PriorityEvictionTest` | priority/eviction white-box downgraded to non-blocking backlog (round-2 §4) | MATCH | queue move under CachePriorityGuard, container `std::list` (SD5) | — | none |
| R-011-2 | 011 | reserve-ahead | `reserve(size,timeout,reason,stat,hint)`; extend unbound; PDNC on fail; `FileSegment.cpp:630-717` | `FileSegment.cpp:630-717` region (reserve delegates to `cache->tryReserve`) | `FileSegmentTest`, `PriorityEvictionTest` | — | MATCH | invariant downloaded<=reserved under segment_guard | — | none |
| R-011-3 | 011 | shrink/reconcile | `shrinkFileSegmentToDownloadedSize`, force_shrink, rename; last-holder only; `FileSegment.cpp:873-939` | `FileSegment.cpp` shrink path; frees surplus via queue decrementSize | `FileSegmentTest` | — | MATCH | key+segment lock; std::list splice preserved | — | none |
| R-012-1 | 012 | getOrSetDownloader | EMPTY/PD->DOWNLOADING, returns id/`notAllowed:<state>`; `FileSegment.cpp:284-305` | `FileSegment.cpp:284-305` (`getOrSetDownloader`, caller_id at :291) | `FileSegmentTest` election tests | — | MATCH | under segment_guard | — | none |
| R-012-2 | 012 | completePartAndResetDownloader | DOWNLOADING->terminal, clears downloader, cv.notify; `FileSegment.cpp:853-871` | `FileSegment.cpp` completePartAndResetDownloader | `FileSegmentTest`, `FileCacheBufferedInputTest` | — | MATCH | segment_guard + cv.notify_all | — | none |
| R-012-3 | 012 | resetDownloader | reset DOWNLOADING then clear; `FileSegment.cpp:322-333` | `FileSegment.cpp:322-333` region | `FileSegmentTest` | — | MATCH | none | — | none |
| R-012-4 | 012 | write into reserved | offset/reserved checks; ENOSPC/EDQUOT(28/122) reconcile downloaded<=file<=reserved; `FileSegment.cpp:415-550` | `FileSegment.cpp:486-519` catches `FileCacheErrnoException`, `getErrno`, 28/122 branch, `chassert(downloaded<=physical<=reserved)` | `FileSegmentTest` real-file ENOSPC/EDQUOT double | round-1 §3 errno contract E1 real-file probe | MATCH | structured errno per signed §3 contract; `FileCacheErrnoException` producer for real O_DIRECT is pre-release gate | Minor | none (errno producer = pre-release gate, round-1 §3) |
| R-012-5 | 012 | setDownloadFinishedWithoutContinuation | ->PDNC, reader withdrawn first; `FileSegment.cpp:818-831` | `FileSegment.cpp` setDownloadFinishedWithoutContinuation | `FileSegmentTest`, reader `Q1Q2Handoff` | H-014a withdraw-before-publish probe (014 review) | MATCH | segment_guard | — | none |
| R-012-6 | 012 | setDownloadFailed(Unlocked) | ->PDNC, keeps downloader, cancels writer; `FileSegment.cpp:812-851` | `FileSegment.cpp:749-764` region | `FileSegmentTest` | — | MATCH | none | — | none |
| R-012-7 | 012 | setDownloadedUnlocked | ->DOWNLOADED, rename `<offset>`->`<offset>_<size>`; `FileSegment.cpp:719-754` | `FileSegment.cpp:719-754` region (rename via openedFileCache seam) | `FileSegmentTest`, `MetadataTest` | — | MATCH | none | — | none |
| R-012-8 | 012 | wait(offset) | blocks while DOWNLOADING; 60s deadline, 1s poll, `throwIfKilled`; `FileSegment.cpp:552-601` | `FileSegment.cpp:526-565` — 60s deadline + 1s cv.wait_for poll; `throwIfKilled` NOT wired into wait loop (`QueryStatus` no-op default) | `FileSegmentTest` wait tests | — | INTENTIONAL_DEVIATION | Bounded 60s deadline present (no indefinite hang); query-cancellation via `throwIfKilled` in wait is Task-017 scope | Minor | Task 017 (round-1 §6: wait throwIfKilled = 017) |
| R-012-9 | 012 | remote reader handoff (download) | get/set/resetRemoteFileReader, invariant end==currentWriteOffset; `FileSegment.cpp:370-413` | `FileSegment.cpp:370-413` region; reused in `FileCacheInputStream.cpp:213-280` | reader `Q1Q2HandoffReusesReaderFromWriteOffset` | E3 reader-handoff test (Task 007) | MATCH | segment_guard; offset==bufferEnd chassert | — | none |
| R-012-10 | 012 | extractRemoteFileReader (bypass) | state-gated move-out (DOWNLOADED/PDNC), not downloader-gated; `FileSegment.cpp:391-401` | `FileSegment.cpp:391-401` region | reader tests | — | MATCH | documented race window preserved | — | none |
| R-012-11 | 012 | complete + holder completeAndPopFront | terminal-state routing, bg-download/shrink/remove; `FileSegment.cpp:972-1143` | `FileSegment.cpp:972-1143` region; holder path `FileSegment.h:379-411` | `FileSegmentTest`, `FileCacheE2ETest` dtor-order | — | MATCH | key lock + segment_guard | — | none |
| R-012-12 | 012 | detach | ->DETACHED, resets metadata/iterator/writer; `FileSegment.cpp:1400-1418` | `FileSegment.cpp:1400-1418` region | `FileSegmentTest` | — | MATCH | requires segment_guard held | — | none |
| R-012-13 | 012 | ~FileSegment | finalize leftover writer, metric decrement, swallow; `FileSegment.cpp:1450-1465` | `FileSegment.cpp:1450-1465` region | `FileSegmentTest` | — | MATCH | none | — | none |
| R-012-14 | 012 | holder ~/reset | FIFO complete, bg-download=true, std::list; `FileSegment.cpp:1486-1515` | `FileSegment.h:379+`, `FileSegment.cpp:1467-1529` region; `FileSegments=std::list` `FileCache_fwd_internal.h:34` | `FileSegmentTest`, E2E | — | MATCH | std::list iterator stability (SD5/R-XC-2) | — | none |
| R-012-15 | 012 | getOrSet (miss/fill) | contiguous no-hole holder; `FileCache.cpp:949-1116` | `FileCache.cpp:949-1116` region (getOrSet), reached via `FileCacheInputStream.cpp:282-340` | `FileCacheTest`, `FileCacheE2ETest.MissFillHit` | E2E hit-phase `preadCount()==0` proves cache serve | MATCH | key metadata lock; SD3 std::map | — | none |
| R-012-16 | 012 | get (exists-or-detached) | EMPTY returned as DETACHED; `FileCache.cpp:1118` | `FileCache.cpp:919-953` — `fill_with_detached=true`, synth DETACHED FileSegment | `FileCacheTest`, `FileCacheE2ETest.ReadIfExistsBypassMode` | — | MATCH | key metadata lock; no fill | — | none |
| R-012-17 | 012 | getDownloadedContiguousOrEmpty | never fills/synth; empty holder on hole; `FileCache.cpp:1161-1202` | `FileCache.cpp` getDownloadedContiguousOrEmpty region | `FileCacheTest`, `FileCacheE2ETest.CacheOnlyMissFails` | E2E temp-only throw | MATCH | RETURN_NULL key lock | — | none |
| R-012-18 | 012 | set/trySet | single-segment holder, LOGICAL_ERROR on intersection; `FileCache.cpp:936-947` | `FileCache.cpp:936-947` region | `FileCacheTest` | — | MATCH | key metadata lock | — | none |
| R-012-19 | 012 | tryReserve | grant/deny no-throw; lock order Priority>Key>State; `FileCache.cpp:1262` | `FileCache.cpp` tryReserve; lock types `Guards.h:47-81` | `FileCacheTest`, `PriorityEvictionTest` | eviction white-box = non-blocking backlog | MATCH | CachePriorityGuard(SharedMutex)+KeyGuard+CacheStateGuard(timed_mutex) per Guards.h | — | none |
| R-012-20 | 012 | removeKeyIfExists | idempotent key removal; `FileCache.cpp:2059` | `FileCache.cpp` removeKeyIfExists region | `FileCacheTest`, `FileCacheFactoryManagerTest` | — | MATCH | Priority+Key+FileSegment guards | — | none |
| R-012-21 | 012 | query context holder | per-query write-limit scope; `FileCache.cpp:3226` | `QueryLimit.cpp`/`QueryLimit.h` getQueryContextHolder | `QueryLimitTest` | round-2 QueryLimit F14 newly signed | MATCH | F14 tables signed (round-2 §5, same-class as SD1) | — | none |
| R-012-22 | 012 | skipCacheOnDiskFailure | governs disk-error swallow; `CachedOnDiskReadBufferFromFile.cpp:133` | `FileCacheSettings.h:79`; consumed `FileCacheInputStream.cpp:549-573` writeCache | `FileCacheHitMetricsTest`/reader tests | — | MATCH | default false | — | none |
| R-012-23 | 012 | lock ordering invariant | Priority>Metadata>Key>FileSegment; `Guards.h:18-66` | `Guards.h:32-37` (hierarchy doc), `:47-177` distinct Lock types | `StatusFileAndGuardsTest` | compile-time type separation (wrong lock = compile error) | MATCH | identical hierarchy + primitive types | — | none |
| R-013-1 | 013 | factory create/get | getOrCreate/get/create/getByName; `FileCacheFactory.h:42-59` | `FileCacheFactory.cpp:126,174,215,239` + `FileCacheManager.cpp:90,143` | `FileCacheFactoryManagerTest` | — | MATCH | `std::mutex` registry | — | none |
| R-013-2 | 013 | factory settings mutation | getSettings/setSettings thread-safe; `FileCacheFactory.h:26-35` | `FileCacheFactory.cpp:35,41` (per-FileCacheData settings) | `FileCacheFactoryManagerTest` | — | MATCH | settings_mutex snapshot/replace | — | none |
| R-013-3 | 013 | factory bulk ops | getAll/getUniqueInstances/loadDefault/updateSettings/remove/clear; `FileCacheFactory.h:56-67` | `FileCacheFactory.cpp:224,230,248,277`; `FileCacheManager.cpp:170,225` | `FileCacheFactoryManagerTest` | — | MATCH | registry mutex | — | none |
| R-013-4 | 013 | cache lifecycle | ctor/dtor/initialize/isInitialized/throwInit/deactivate/sync; OnceFlag, StatusFile; `FileCache.h:111-124,251-255` | `FileCache.cpp:173,321-332,377-396` (`std::call_once`, StatusFile at :396), `:1918,1939` | `FileCacheFactoryManagerTest`, `StatusFileAndGuardsTest` | — | MATCH | call_once (retry-on-exc), StatusFile base-dir guard; main_priority-before-metadata destroy order preserved | — | none (StatusFile crash-diagnostic = pre-release backlog) |
| R-014-1 | 014 | batch acquisition | getDownloadedContiguousOrEmpty/get/getOrSet by settings; temp throw; `ReadBuf cpp:224-270` | `FileCacheInputStream.cpp:282-340` nextFileSegmentsBatch | `FileCacheBufferedInputTest`, `FileCacheE2ETest` | — | MATCH | segments_batch_size bound | — | none |
| R-014-2 | 014 | initialize (first read) | first batch build, LOGICAL_ERROR on re-init/empty; `ReadBuf cpp:272-294` | `FileCacheInputStream.cpp:342-360` initializeIfNeeded | `FileCacheBufferedInputTest` | — | MATCH | none | — | none |
| R-014-3 | 014 | read-type routing | CACHED/PUT_IN_CACHE/BYPASS decision + downloader election; `ReadBuf cpp:577-745` | `FileCacheInputStream.cpp:365-466` createReadFromFileSegmentState + `:468-547` prepare | `FileCacheBufferedInputTest` Miss/Hit/Bypass/Predownload | reserve-fail RED (014 review) | MATCH | segment lock via wait/getOrSetDownloader | — | none |
| R-014-4 | 014 | cache buffer open + rename retry | pread no O_DIRECT; FILE_DOESNT_EXIST reopen once; `ReadBuf cpp:315-478` | `FileCacheInputStream.cpp:126-211` getCacheReadBuffer | `FileCacheBufferedInputTest` | — | MATCH | local FS openFileForRead; path recompute seam | — | none |
| R-014-5 | 014 | truncation self-heal (load-bearing) | short cache file->nullptr->bypass, LOG_WARNING, never LOGICAL_ERROR falsely; `ReadBuf cpp:448-477` | `FileCacheInputStream.cpp:174-204` — trustSizeFromFilename gate, `cacheFileSize<getDownloadedSize()`->nullptr bypass; empty-file guard | `FileCacheBufferedInputTest.ExternalTruncationSelfHealsFromSource`+`EmptyCacheFileSelfHealsFromSource` | F-014-1 RED verified: remove branch->2 tests RED (Velox `e142429ef`, round-2 §1) | MATCH | state-before-size ordering + bypass preserved; was the one real 014 bug, now fixed+RED-proven | — | none (closed) |
| R-014-6 | 014 | remote buffer election/reuse | reuse shared segment reader / bypass non-shared; BoundedReadBuffer; `ReadBuf cpp:480-555` | `FileCacheInputStream.cpp:213-280` getRemoteReadBuffer | `FileCacheBufferedInputTest.Q1Q2Handoff...` | — | MATCH | offset==currentWriteOffset chassert | — | none |
| R-014-7 | 014 | predownload continuation | gap download, PDNC bypass on fail, CANNOT_READ_ALL_DATA on trunc; `ReadBuf cpp:934-1193` | `FileCacheInputStream.cpp:575-669` predownloadForCurrentSegment | `FileCacheBufferedInputTest.PredownloadFromMidSegment` | bytes=12 probe (014 review) | MATCH | withdraw-before-release; remote-object-truncation CANNOT_READ_ALL_DATA sub-path excluded (no Velox remote-metadata source — legit, deferred) | — | none |
| R-014-8 | 014 | segment read + reconcile | reserve->writeCache; bypass+PDNC on fail; last-seg resize; `ReadBuf cpp:1498-1803` | `FileCacheInputStream.cpp:743-849` readFromCurrentSegment; read-while-downloading continuation via `cachedPrefixEndAbsolute`/`updateReadStateIfNeeded` (`:499-505,706-726`), fixed in commit `006a15996` | `FileCacheBufferedInputTest`; E2E `CachedReaderRefillsWhenDownloadingSegmentGrows` (added `006a15996`) | reserve-fail RED; `006a15996` refill RED (neutralizing the continuation -> "Reading past end" abort, falsifiable) | MATCH | physical reconcile invariant R-XC-3. **Evidence-timeliness:** green basis updated from the pre-fix snapshot to fix commit `006a15996` — pre-fix the CACHED branch froze `readUntil_` at the first flushed chunk and crashed 6/22 TPC-H queries; MATCH holds on the post-fix HEAD `398426810` | — | none |
| R-014-9 | 014 | cache write failure recovery | ENOSPC/EDQUOT LOG_INFO+false; else CACHE_CANNOT_WRITE unless skip; `ReadBuf cpp:1260-1301` | `FileCacheInputStream.cpp:549-573` writeCache | `FileCacheBufferedInputTest` | `MidDownloadCacheWriteFailureReleasesDownloaderNoLeak` | MATCH | skip_cache_on_disk_failure default false | — | none |
| R-014-10 | 014 | completion + advance | completeAndPopFront + increasePriority next; `ReadBuf cpp:875-915` | `FileCacheInputStream.cpp:671-700` completeCurrentSegmentAndAdvance | `FileCacheBufferedInputTest` | — | MATCH | drops state before throw | — | none |
| R-014-11 | 014 | nextImpl + downloader cleanup | per-step election + SCOPE_EXIT reset; `ReadBuf cpp:1303-1496` | `FileCacheInputStream.cpp:851-981` Next (catch + post-advance guard + dtor `:89-106`) | `FileCacheBufferedInputTest.MidDownload...NoLeak` | catch-block isolated-RED = P-014-a Task-015 deferral (dtor second net) | MATCH | downloader owned 1 term; defense-in-depth | — | none |
| R-014-12 | 014 | readBigAt (positioned) | stays downloader whole call; `ReadBuf cpp:1805-1998` | NOT ported (design-03 exclusion; `supportsReadAt`/`readBigAt` absent) | — | verified absent (014 review §3) | INTENTIONAL_DEVIATION | readBigAt deliberately not ported; downloader-release covered by Next catch+dtor | Minor | none (legit exclusion, round-2/014 §3) |
| R-014-13 | 014 | seek semantics | reset on real move; CANNOT_SEEK_THROUGH_FILE; `ReadBuf cpp:2000-2052` | `FileCacheInputStream.cpp:1023+` seekToPosition (fast in-buffer + slow reset via shared `invalidateAndReposition`, added in commit `01c007abe`) | `FileCacheBufferedInputTest.SeekWithin/Outside`, `FileCacheE2ETest.SeekToPosition...`; cross-segment skip family (3 real tests added `01c007abe`; old `SkipAcrossSegmentBoundary` false-green deprecated) | `01c007abe` cross-segment skip RED (mid-segment skip across 1/2 boundaries + consecutive skips, assert actual bytes) | MATCH | allow_seeks_after_first_read gating. **Evidence-timeliness:** green basis for the seek/skip coordinate path updated from the pre-fix snapshot to fix commit `01c007abe` — `SkipInt64` previously did Next()-then-rollback and desynced `position_` across a segment boundary; MATCH holds on post-fix HEAD `398426810` | — | none |
| R-014-14 | 014 | read-until / bounds | reset batch on change, LOGICAL_ERROR guards; `ReadBuf cpp:2054-2091` | `FileCacheInputStream.cpp` setReadUntilPosition path (`:493,512,519`) | `FileCacheBufferedInputTest` | — | INTENTIONAL_DEVIATION | CACHED setReadUntilPosition uses `getDownloadedSize()` segment-relative (`:493`) vs CH absolute — F-014-2 registered deviation | Minor | none (F-014-2 accept-with-registration, round-2/014 §7) |
| R-014-15 | 014 | content-cached predicate | true iff range downloaded/contiguous; `ReadBuf cpp:2138-2181` | `FileCacheBufferedInput.cpp:100-119` isBuffered / isContentCached | `FileCacheBufferedInputTest`, `FileCacheE2ETest.DownloadedSizeAccounting...` | — | MATCH | none | — | none |
| R-014-16 | 014 | seek-cheap / position | position accounting, cheap if uninit/CACHED; `ReadBuf cpp:2093-2136` | `FileCacheInputStream.cpp` getPosition/isSeekCheap/getInfoForLog | `FileCacheBufferedInputTest` | — | MATCH | none | — | none |
| R-014-17 | 014 | O_DIRECT read baseline | cache reads never O_DIRECT/mmap, always pread; `ReadBuf cpp:343-357` | cache reader opened via local FS `openFileForRead` `FileCacheInputStream.cpp:155`; no O_DIRECT propagation | `FileCacheBufferedInputTest` | — | INTENTIONAL_DEVIATION (conditional, D3) | Strict-mock/local-file reads only; real kernel O_DIRECT integration UNPROVEN — mandatory forward gate | Important | Task 017+/pre-release (D3 real O_DIRECT gate) |
| R-014-18 | 014 | file size discovery | UNKNOWN_FILE_SIZE if none; `ReadBuf cpp:152-167` | `FileCacheBufferedInput`/`FileCacheInputStream` tryGetFileSize (`:193`), getFileSize | `FileCacheBufferedInputTest` | — | MATCH | none | — | none |
| R-015-1 | 015 | end-to-end read contract | full ctor+read loop miss->fill->hit; `ReadBuf cpp:99-150,1303-1496` | assembled path `FileCacheBufferedInput`->`FileCacheInputStream`->`FileCache` via real `FileCacheManager` | `FileCacheE2ETest` (21 `TEST_F` at HEAD `398426810`, `velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp`: MissFillHit, ColdMissFillThenHit, cross-segment skip family, `CachedReaderRefillsWhenDownloadingSegmentGrows`, etc.) | E2E `preadCount()==0` hit proof; seek benchmark hit "no source read" RED (exit 134) | MATCH | inherits all R-014 guarantees; E2E validates already-approved 003-014 behavior. **Evidence-timeliness:** count corrected from stale "17 tests" (pre-fix snapshot) to the actual 21 at HEAD `398426810`; the extra tests were added by fix commits `01c007abe` and `006a15996` | Minor | none — no independent 015 decision file (provenance: 015 contract/receipt only); NOT a new guarantee-changing deviation |
| R-015-2 | 015 | random-seek repositioning | cheap in-buffer seek + full reset; `ReadBuf cpp:2000-2052` | `FileCacheInputStream.cpp:1023+` seekToPosition; benchmark `FileCacheSeekBenchmark.cpp` | `FileCacheE2ETest.RandomSeeksAcrossHitMissBypass` | seek benchmark green (hit/miss/bypass rows) | MATCH | as R-014-13 | Minor | none — no independent 015 decision file (provenance: 015 contract/receipt only) |
| R-XC-1 | cross-cutting | lock type map | 5 distinct non-interchangeable Lock types; `Guards.h:76-174` | `Guards.h:47-177` — SharedMutex/timed_mutex/std::mutex, compile-time separation (`:41`) | `StatusFileAndGuardsTest` | wrong-lock = compile error | MATCH | identical primitives + type separation | — | none |
| R-XC-2 | cross-cutting | FileSegmentsHolder container | `std::list<FileSegmentPtr>`, iterator stability; `FileSegment.h:344-383` | `FileCache_fwd_internal.h:34` `FileSegments=std::list<FileSegmentPtr>`; holder `FileSegment.h:379-422` | `FileSegmentTest`, E2E | — | MATCH | std::list preserved (SD5, signed §5) | — | none |
| R-XC-3 | cross-cutting | reserve/download/physical invariant | downloaded<=file<=reserved across all mutations; ENOSPC reconcile; `FileSegment.cpp:447…752` | `FileSegment.cpp:508` `chassert(downloaded<=physical<=reserved)`; atomics under segment_guard | `FileSegmentTest` real-file ENOSPC | E1 real-file probe (round-1 §3) | MATCH | invariant enforced; atomic sizes | — | none |
| R-XC-4 | cross-cutting | typed errno->exception | errno 28/122 special-cased everywhere; no fallback hiding non-space; `ReadBuf cpp:1277…` | `FileSegment.cpp:486-519` (`FileCacheErrnoException::getErrno`); `FileCacheInputStream.cpp:549-573` writeCache | `FileSegmentTest`, `FileCacheBufferedInputTest` | E1 real-file probe | MATCH | signed §3 structured-errno contract; producer for real O_DIRECT = pre-release gate | Minor | none (errno producer pre-release, round-1 §3) |
| R-XC-5 | cross-cutting | failpoints / TestValue | write_through_cache_fail, EIO, NETWORK_ERROR injections; `WriteBuf cpp:33-38…` | `FailPoint.h` shim; write-through failpoints (`write_through_cache_fail`, distributed) are Task-016/017 write-path | reader `MidDownload...` fault-injecting file double | — | INTENTIONAL_DEVIATION | D10 accepted: failpoint shim is no-op for MVP; injected disk-failure paths not triggerable; does not change runtime behavior | Minor | none (D10 accepted, round-1 §7) |
| R-XC-6 | cross-cutting | filesystem cache log | READ_FROM_CACHE/BYPASS/DOWNLOADED/WRITE_THROUGH element w/ exact CacheType; `ReadBuf cpp:184-222` | `FilesystemCacheLog.h`; hit/read logging `FileCacheHitMetricsTest`; real log emission = Task 017 | `FileCacheHitMetricsTest` | — | INTENTIONAL_DEVIATION | Read-path CacheType classification present; WRITE_THROUGH element = Task-016 write consumer; real logging counters = Task 017 (round-1 §6) | Minor | Task 016/017 |

## Velox-only inventory (NOT in CH denominator)

| id | surface | Velox location | classification | note |
|---|---|---|---|---|
| V-1 | `FileCacheManager` (Options/create/setInstance/getInstance/getDefault/shutdown) | `FileCacheManager.{h,cpp}` | VELOX_EXTENSION | Manager-owned ownership layer wrapping `FileCacheFactory`; the accepted Velox design substitute for CH's global/context-owned FileCache. Injects workerPool/scheduler/openedFileCache/localFileSystem/commonUserId. Not a CH consumer contract; it is the approved platform seam (013). |
| V-2 | `QueryStatus`/`throwIfKilled` no-op shim | `QueryStatus.h:34-55` | VELOX_EXTENSION | Task-017 forward cancellation scaffold; default token never cancelled (no-op). Present but inert for MVP. |
| V-3 | `FileCacheSeekBenchmark` + `FileCacheWrapperBenchmark` + `CacheReadHarness` (fcbi/cbi/dbi) | `velox/ch/benchmarks/` | VELOX_EXTENSION | Task-015 benchmark drivers; not a CH behavior contract. |
| V-4 | fail-fast rejection of `cache_on_write_operations` / overcommit policies | `FileCacheSettings.cpp:406-416` | VELOX_EXTENSION | Explicit "unsupported in phase-1" guards (B2 overcommit exclusion + Task-016 write-through). Fail-close, not silent. |
| V-5 | `TpchAbBenchmark`/`AbBenchmark*` skeleton (filecache engine PARKED) | `velox/ch/benchmarks/` | OVER_PORT (benign) | filecache engine cannot route reads without production `HiveConnectorUtil` change (Task 018); cbi/direct skeleton ported but the fcbi path has no real in-tree consumer yet. Parked, not shipped as a working three-engine gate. Not a CH consumer; no correctness risk (benchmark-only). |

## Status counts by group

| group | MATCH | EQUIVALENT | INTENTIONAL_DEVIATION | MISSING | UNPROVEN | rows |
|---|---|---|---|---|---|---|
| Tasks 003-010 (R-006,008,010) | 7 | 0 | 1 (R-010-3) | 0 | 0 | 8 |
| Task 011 | 3 | 0 | 0 | 0 | 0 | 3 |
| Task 012 | 22 | 0 | 1 (R-012-8) | 0 | 0 | 23 |
| Task 013 | 4 | 0 | 0 | 0 | 0 | 4 |
| Task 014 | 15 | 0 | 3 (R-014-12, R-014-14, R-014-17) | 0 | 0 | 18 |
| Task 015 | 2 | 0 | 0 | 0 | 0 | 2 |
| cross-cutting | 4 | 0 | 2 (R-XC-5, R-XC-6) | 0 | 0 | 6 |
| **total** | **57** | **0** | **7** | **0** | **0** | **64** |

Velox-only (excluded from denominator): 5 rows (V-1..V-4 VELOX_EXTENSION, V-5 OVER_PORT-benign).

## Parity percentages (integer numerator/denominator)

In-scope CH denominator = all 64 recovered rows (0 MISSING, 0 duplicate, all have a real CH caller).

- **Semantic parity** = (MATCH + EQUIVALENT) / all-in-scope = (57 + 0) / 64 = **57/64 = 89.1%**
- **Accepted coverage** = (MATCH + EQUIVALENT + approved INTENTIONAL_DEVIATION) / all-in-scope
  = (57 + 0 + 7) / 64 = **64/64 = 100%**

All 7 INTENTIONAL_DEVIATION rows are backed by a signed decision (R-010-3 Task-016 fwd; R-012-8
round-1 §6; R-014-12 & R-014-14 round-2/014 §3/§7 F-014-2; R-014-17 D3 conditional; R-XC-5 D10;
R-XC-6 round-1 §6). Zero UNPROVEN, zero MISSING.

## Decisions-needed seed list

No row is silently UNPROVEN. The items below are the open forward gates surfaced by the deviation
rows (each already registered, but each has an outstanding real-world proof obligation), plus the
015-provenance flags. Plain-language question per item:

1. **R-014-17 (D3 real O_DIRECT — Important):** cache reads currently use local-FS pread with no
   kernel `O_DIRECT`. Question: *is serving all cache reads through the page cache (never O_DIRECT)
   an accepted permanent behavior, or must real-kernel O_DIRECT integration be proven before
   production?* D3 marks it a mandatory forward gate — strict-mock/local-file tests are logic
   coverage only, not real-kernel proof. Owner: Task 017+/pre-release.
2. **R-012-8 (wait cancellation):** `wait()` has the 60s bounded deadline but does not consult
   `throwIfKilled`. Question: *is deferring in-`wait` query cancellation to Task 017 acceptable for
   MVP given the 60s deadline prevents indefinite hang?* (round-1 §6 says yes; confirm still holds.)
3. **R-010-3 / R-XC-6 (write-through consumer):** write-path routing fields and the WRITE_THROUGH
   cache-log element have no MVP read consumer. Question: *confirm write-through parity is entirely
   Task-016 scope and not required for the 003-015 MVP sign-off.*
4. **R-014-12 / R-014-14 (readBigAt & CACHED read-until F-014-2):** both are registered structural
   deviations. Question: *F-014-2 (CACHED `setReadUntilPosition(getDownloadedSize())` segment-relative
   vs CH absolute) is signed accept-with-registration — confirm the ledger entry is final and no
   further proof is owed.*
5. **R-015-1 / R-015-2 (015 provenance):** *no independent Task-015 decision file exists on this
   machine.* Both rows are graded MATCH because they are E2E validation of already-approved 003-014
   behavior (production diff empty, no new guarantee-changing code). Question: *does the reviewer
   accept the Task-015 contract+receipt as sufficient provenance, or is a standalone 015 decision
   record required to ratify the MVP acceptance gate?* This is the only provenance gap; it is NOT a
   guarantee-changing deviation, so it is not UNPROVEN — but it is flagged per instruction.

## Notes on excluded / forward items (not counted)

- Task-016 `WriteBufferToFileSegment`, `TemporaryDataOnDisk*`, distributed `FileSegmentRangeWriter`
  branches: forward obligations, excluded from denominator (ledger fwd-obligations).
- Task-017 observability/cancellation (real ProfileEvents/CurrentMetrics counters, `wait`
  throwIfKilled, F-CALLERID `None:<threadname>:<tid>` diagnostic, SD8 recursive-mutex): forward
  work; the enumerator *surface* (B1/B2) is present and in-scope, the real *counters* are 017.
- Gluten (Tasks 018-019): out of scope, never touched.
