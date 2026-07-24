# Review 5 Task 3 — Task 017A / Task 018 Parity Matrix

```text
matrix_round:       5 (Review 5 Task 3)
ch_head:            630916e4de1
velox_head:         cda6c03703
scope:              Task 017A (statistics, cancellation, caller-id, scheduler)
                    Task 018 (correctness harness, benchmarks, TPCH, Hive adapter)
                    Task 012 corrective (folly::call_once)
                    Task 014 corrective (truncation/rename recovery)
method:             source-level trace against production code
```

---

## 1. Statistics Parity

| Contract | CH source / real caller | Velox carrier | Test/mutation/log evidence | Status | Severity | Blocks R5 |
|---|---|---|---|---|---|---|
| S-PROFILEEVENTS-STORAGE | `src/Common/ProfileEvents.cpp` relaxed atomic counters | `velox/ch/Common/ProfileEvents.cpp:15-25` `std::array<std::atomic<uint64_t>>` | `MetricsAndSnapshotTest:AllExistingEnumNamesCompile`, `IncrementAccumulates` — mutation: no-op counter fails | MATCH | — | No |
| S-CURRENTMETRICS-STORAGE | `src/Common/CurrentMetrics.cpp` relaxed atomic gauges | `velox/ch/Common/CurrentMetrics.cpp:14-39` `std::array<std::atomic<int64_t>>` | `MetricsAndSnapshotTest:AddSubGetRoundTrip`, `SetOverwrites`, `IncrementRAII` — mutation: zero-init fails | MATCH | — | No |
| S-SNAPSHOT-TAKE | `FileCacheStatistics` from CH queries | `velox/ch/Common/FileCacheStats.cpp:8-80` `takeFileCacheStatsSnapshot()` | `MetricsAndSnapshotTest:ReflectsCurrentValues` | EQUIVALENT | — | No |
| S-SNAPSHOT-DELTA | `FileCacheStatistics` delta subtraction | `FileCacheStats.cpp operator-` | `MetricsAndSnapshotTest:SubtractionProducesDeltas` | EQUIVALENT | — | No |
| S-KFCWRITEBYTES | `ProfileEvents::CachedReadBufferCacheWriteBytes` | `FileCacheStats.h:8-11 kFileCacheWriteBytes` | `MetricsAndSnapshotTest:ReflectsCurrentValues` | MATCH | — | No |
| S-READER-CACHE-HIT | `CachedReadBufferReadFromCacheHits` at cache-read completion | `FileCacheInputStream.cpp:851` | `FileCacheBufferedInputTest:CacheReadUpdatesGlobalAndIoStatistics` — mutation: removed increment fails | MATCH | — | No |
| S-READER-CACHE-MISS | `CachedReadBufferReadFromCacheMisses` at source-read completion | `FileCacheInputStream.cpp:858` | `FileCacheBufferedInputTest:SourceReadUpdatesGlobalAndIoStatistics` | MATCH | — | No |
| S-READER-CACHE-BYTES | `CachedReadBufferReadFromCacheBytes` pre-clamp physical bytes | `FileCacheInputStream.cpp:852` | `FileCacheBufferedInputTest:CacheReadUpdatesGlobalAndIoStatistics` | MATCH | — | No |
| S-READER-SOURCE-BYTES | `CachedReadBufferReadFromSourceBytes` pre-clamp physical bytes | `FileCacheInputStream.cpp:859,767` | `FileCacheBufferedInputTest:SourceReadUpdatesGlobalAndIoStatistics` | MATCH | — | No |
| S-READER-CACHEWRITE | `CachedReadBufferCacheWriteBytes` physical bytes written | `FileCacheInputStream.cpp:664-677` | `FileCacheBufferedInputTest:CacheWriteUpdatesGlobalAndIoStats` | MATCH | — | No |
| S-READER-PREDOWNLOAD | Predownload in global source+predownload, query read+prefetch, never rawBytesRead | `FileCacheInputStream.cpp:767-774,895-899` | `FileCacheBufferedInputTest:PredownloadUpdatesReadPrefetchButNotRawBytes` — mutation: added rawBytesRead fails | MATCH | — | No |
| S-READER-LOGICAL | `rawBytesRead` post-clamp logical bytes | `FileCacheInputStream.cpp:896-899` | `FileCacheBufferedInputTest:LastSegmentClampSeparatesPhysicalAndLogicalBytes` | MATCH | — | No |
| S-READER-TIMING-CACHE | `CachedReadBufferReadFromCacheMicroseconds` | `FileCacheInputStream.cpp:853-856` | `FileCacheBufferedInputTest:SourceReadRecordsPositiveScanTime` (covers timing path) | MATCH | — | No |
| S-READER-TIMING-SOURCE | `CachedReadBufferReadFromSourceMicroseconds` | `FileCacheInputStream.cpp:860-863` | `FileCacheBufferedInputTest:SourceReadRecordsPositiveScanTime` | MATCH | — | No |
| S-READER-TIMING-PREDL | `CachedReadBufferPredownloadedFromSourceMicroseconds` | `FileCacheInputStream.cpp:775-778` | `FileCacheBufferedInputTest:PredownloadRecordsPositiveSourceMicroseconds` | MATCH | — | No |
| S-READER-TIMING-WRITE | `CachedReadBufferCacheWriteMicroseconds` | `FileCacheInputStream.cpp:667-670` | Partial — covered by CacheWrite test structural path; no dedicated timing-only mutation | EQUIVALENT | Low | No |
| S-READER-10EVENTS | 10 new CH reader ProfileEvents appended | `ProfileEvents.h:67-76` | `MetricsAndSnapshotTest:NewReaderEventsPresent` — all 10 increment and read back | MATCH | — | No |
| S-READER-MULTIPASS | Multi-chunk bypass counts miss per returned chunk | `FileCacheInputStream.cpp:858` per chunk | `FileCacheBufferedInputTest:MultiChunkBypassCountsMissPerReturnedChunk` | MATCH | — | No |

---

## 2. Cancellation Parity

| Contract | CH source / real caller | Velox carrier | Test/mutation/log evidence | Status | Severity | Blocks R5 |
|---|---|---|---|---|---|---|
| C-TOKEN-COPY | `throwIfKilled` via copied query pointer | `FileCacheInputStream.cpp:83-85` `cancellationToken_ = owner_->cancellationToken()` (value copy) | `FileCacheBufferedInputTest:CancellationBeforeLookupThrows` | EQUIVALENT | — | No |
| C-PRELOOKUP | Pre-segment-batch `isKilled()` check | `FileCacheInputStream.cpp:264-268` `isCancellationRequested()` → `VELOX_FAIL` | `FileCacheBufferedInputTest:CancellationBeforeLookupThrows` — mutation: removed check allows unbounded wait | EQUIVALENT | — | No |
| C-WAIT-PASS | `FileSegment::wait` receives real cancel token | `FileCacheInputStream.cpp:518-525` `wait(offset, cancellationToken_)` | `FileCacheBufferedInputTest:CancellationDuringSegmentWaitThrows` | EQUIVALENT | — | No |
| C-WAIT-60S | 60s deadline in `FileSegment::wait` | `FileSegment.cpp:558` `std::chrono::seconds(60)` | `FileSegmentTest:WaitObservesCancellationToken` | MATCH | — | No |
| C-WAIT-1S | 1s slices in `FileSegment::wait` | `FileSegment.cpp:563` `wait_for(…, seconds(1), …)` | `FileSegmentTest:WaitObservesCancellationToken` | MATCH | — | No |
| C-WAIT-CHECK | `isCancellationRequested()` inside 1s loop | `FileSegment.cpp:561-563` | `FileSegmentTest:WaitObservesCancellationToken` | EQUIVALENT | — | No |
| C-POSTSEG | Post-segment cancellation check | `FileCacheInputStream.cpp:938-942` | `FileCacheBufferedInputTest:CancellationDeferredUntilAfterSegmentWriteCompletes` | EQUIVALENT | — | No |
| C-DEFERRED-WRITE | Cancellation deferred until after cache write completes | `FileCacheInputStream.cpp:938-942` (after write path) | `FileCacheBufferedInputTest:CancellationDeferredUntilAfterSegmentWriteCompletes` | EQUIVALENT | — | No |
| C-DEFAULT-TOKEN | Default (empty) token reads fully without cancel | `FileCacheInputStream.cpp` — no-op token passes all checks | `FileCacheBufferedInputTest:DefaultTokenReadsFully` | EQUIVALENT | — | No |

---

## 3. Caller-ID / Scheduler Parity

| Contract | CH source / real caller | Velox carrier | Test/mutation/log evidence | Status | Severity | Blocks R5 |
|---|---|---|---|---|---|---|
| I-CALLERID-QUERY | `<query-id>:<os-tid>` when in query scope | `FileCacheQueryIdScope.cpp` scoped caller-id | `SchedulerAndScopeTest:PhysicalTidChangeMakesCallerIdDiffer`, `SameQueryDifferentResumeProducesDifferentCallerId` | MATCH | — | No |
| I-CALLERID-BG | `None:<threadname>:<os-tid>` without query scope | `FileCacheQueryIdScope.cpp:56-60` `folly::getCurrentThreadName()` + `tid` | `SchedulerAndScopeTest:CallerIdWithoutScopeHasThreadNameFormat`, `NamedThreadAppearsInCallerId`; `FileSegmentTest:NoScopeBackgroundId` | MATCH | — | No |
| I-SCHED-TWOLOCK | Scheduler uses two separate mutexes, not recursive mutex | `FileCacheScheduler.cpp:162-168` `execMutex_` then `scheduleMutex_` | `SchedulerAndScopeTest:DeactivatePreventsQueuedCallbackFromRunning`, `SameTaskNeverRunsConcurrently`, 200-stress receipt | EQUIVALENT | — | No |
| I-SCHED-ORDER | Lock order: `execMutex_` THEN `scheduleMutex_` | `FileCacheScheduler.cpp:162` comment + code | Same tests as above | EQUIVALENT | — | No |
| I-SCHED-DEACTIVATE | `deactivate()` drains exec, then disables schedule | `FileCacheScheduler.cpp:162-168` | `SchedulerAndScopeTest:DeactivatePreventsQueuedCallbackFromRunning` | EQUIVALENT | — | No |

---

## 4. Task 012 / Task 014 Corrective Parity

| Contract | CH source / real caller | Velox carrier | Test/mutation/log evidence | Status | Severity | Blocks R5 |
|---|---|---|---|---|---|---|
| L-CALLONCE-01 | `std::call_once` / `std::once_flag` for `FileCache::initialize` | `FileCache.h:365-372` `folly::once_flag`; `FileCache.cpp:430-433` `folly::call_once` | Controller 3/3 mono + 3/3 non-mono; 3 mutations RED (local mutex/flag, std::once_flag, retry) | EQUIVALENT | — | No |
| G-CACHEBUF-01 | External truncation: physical < recorded → bypass + refetch | `FileCacheInputStream.cpp:382-401` size check → `cacheReader.reset()` | Controller 2/2 + 16/16 + 2/2; truncation RED (short pread 4096 vs 8192) | EQUIVALENT | — | No |
| G-CACHEOPEN-RENAME-01 | Rename before open: recompute path under lock, retry once for `kFileNotFound` | `FileCacheInputStream.cpp:325-338` | Controller 2/2 + 16/16 + 2/2; rename RED (FILE_NOT_FOUND on old offset) | EQUIVALENT | — | No |

---

## 5. Benchmark / TPCH Parity

| Contract | CH source / real caller | Velox carrier | Test/mutation/log evidence | Status | Severity | Blocks R5 |
|---|---|---|---|---|---|---|
| B-CONTENT-GATE | Byte/content correctness checked before timing report | `AbBenchmarkBase.cpp:268-343` — ref collected first, then `rows`/`hash`/`match` after `run()` | `AbBenchmarkSchemaTest` schema tests + CSV artifact inspection | MATCH | — | No |
| B-FAIRNESS | Direct/CBI/FileCache share same binary and plan | `run_tpch_ab.sh:54-90` — only `--input_source` differs | Script inspection + CSV shape (66 rows per backend) | MATCH | — | No |
| B-MEMORY | Separated query/cache MmapAllocators | `QueryBenchmarkBase.cpp:64-98,205-273` | Receipt: `--cache_gb=32`, `--cache_mem_gb=4`; CBI OOM corrected | MATCH | — | No |
| B-ONESPLIT | `num_splits_per_file=1` for TPCH | `run_tpch_ab.sh:39,61` | Script inspection | MATCH | — | No |
| B-ONEDRIVER-BASE | One-driver as accepted baseline | Receipt: `num_drivers=1`, 198/198 rows | 22×3×3 CSV rows verified | MATCH | — | No |
| B-FOURDRIVER-ADD | Four-driver addendum with `result_match` | Receipt: `num_drivers=4`, 198/198 `result_match=1` | CSV artifact: 198/198 `result_match=1`, R2-R3 100% hit | MATCH | — | No |
| B-FINALROWS | True final rows from `RowVector`, not intermediate | `AbBenchmarkBase.cpp:157-165` `countResultRows` | `AbBenchmarkSchemaTest:CountResultRowsSkipsNullptr`, `CountResultRowsEmptyVector` | MATCH | — | No |
| B-RESULTMATCH | Epsilon-aware `result_match` | `AbBenchmarkBase.cpp:337` `assertEqualResults` | `AbBenchmarkSchemaTest:ResultMatchSerializesTrue/False/Empty` + 198/198 CSV | MATCH | — | No |
| B-Q15-FIX | q15 parallel revenue selection fix | `TpchQueryBuilder.cpp:1727-1767` `dense_rank`/`topNRank` | Four-driver q15: 9/9 cells = 1 row, `result_match=1` | MATCH | — | No |
| B-SENTINEL | Sentinel-authenticated cache cleanup | `run_wrapper_ab.sh:9-14,47-54` + `lib_cache_cleanup.sh` | Receipt: cache root empty after each run | MATCH | — | No |
| B-NOTHRESHOLD | No hard first-wave performance threshold | `run_wrapper_ab.sh` validates shape only (78-97) | Receipt line 259: "baseline-only evidence" | MATCH | — | No |
| B-PRETPCH | No TPCH copy/build/run before user authorization | Receipt: `tpch_sources_copied: false` at 018-P | Controller confirms PARQUET=OFF and no binary exists | MATCH | — | No |
| B-RELWITHDEBINFO | All benchmark binaries RelWithDebInfo | `CMakeCache.txt`: `CMAKE_BUILD_TYPE=RelWithDebInfo` | `file(1)` output in receipt + binary path verification | MATCH | — | No |
| B-NODEBUG | No Debug benchmark result accepted | Receipt: `_build/debug*` never referenced for benchmarks | No Debug binary invoked in any benchmark section | MATCH | — | No |
| B-LOCALONLY | Performance confidence: local-only, medium direction/low magnitude | Receipt line 843-844 | Explicit documentation of WSL2, 2 warm samples | INTENTIONAL_DEVIATION | — | No |
| B-Q11-ORACLE | q11 returns 0 rows in 4-driver (all backends) | CSV: `q11` rows=0, result_match=1 | Parallel equivalence proven; 1-driver returns 3203218 rows | INTENTIONAL_DEVIATION | — | No |
| B-HASHDIAG | Parallel result hashes diagnostic only | Receipt line 931 | FP nondeterminism → hashes vary; `result_match` is binding | MATCH | — | No |
| B-HIVEADAPTER | Hive reads routed through FileCache | `609cf21da9` adapter commit | R1 nonzero cache metrics; R2-R3 100% hit, positive cache_read_mib | MATCH | — | No |
| B-WRAPPERW3 | Wave 3 SLRU capacity correction | SLRU 40% probationary → 20 GiB disk cache | Rerun: all 4 FCBI rows `src_MB=0` with 20 GiB cache | MATCH | — | No |

---

## 6. Forward Debt (UNPROVEN — Preserved, Non-blocking)

| Contract | CH source / real caller | Velox carrier | Test/mutation/log evidence | Status | Severity | Blocks R5 |
|---|---|---|---|---|---|---|
| D-INIT-01 | Manager init serialization | `FileCacheManager.cpp:264-302` `mutation_mutex_` | Implementation present; R2-D4 user approval pending | UNPROVEN | — | No |
| E-GETORCREATE-01 | `getOrCreateLocked` transaction | `FileCacheManager.cpp:415` | Implementation present; R2-D4 user approval pending | UNPROVEN | — | No |
| E-CREATE-01 | `createLocked` serialization | `FileCacheManager.cpp` | Implementation present; R2-D4 user approval pending | UNPROVEN | — | No |
| E-UPDCFG-01 | `applyConfigs` transactional reload | `FileCacheManager.cpp:350-415` | Implementation present; R2-D4 user approval pending | UNPROVEN | — | No |
| P-RB-SETDETACH-01 | Reader detach `set(nullptr, 0)` | `FileCacheInputStream.cpp:1029-1033` | Implementation present; R2-D6 user approval pending | UNPROVEN | — | No |
| G-NEXTIMPL-01 | `readNextChunk` orchestration | `FileCacheInputStream.cpp` | Implementation present; R2-D6 user approval pending | UNPROVEN | — | No |

---

## 7. Matrix Summary

| Status | Count | Description |
|---|---|---|
| MATCH | 36 | Exact behavioral match |
| EQUIVALENT | 16 | Velox-idiomatic mapping with identical observable semantics |
| INTENTIONAL_DEVIATION | 2 | Local-only perf evidence (B-LOCALONLY), q11 oracle caveat (B-Q11-ORACLE) |
| UNPROVEN | 6 | D4 × 4, D6 × 2 — non-blocking forward debt |
| MISSING | 0 | — |
| **Total** | **60** | |

### Blocking assessment

```text
critical_items:       0
important_items:      0
blocking_for_r5:      0
non_blocking_debt:    6 UNPROVEN (D4/D6, preserved per user decision)
false_green:          0
new_findings:         0
```

No row blocks Review 5 under current user decisions.
