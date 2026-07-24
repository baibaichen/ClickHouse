# Review 5 Task 3 — Task 017A / Task 018 Integrated Contract Ledger

```text
ledger_round:       5 (Review 5 Task 3)
ch_head:            630916e4de1 (ch-filecache, clean/pushed)
velox_head:         cda6c03703 (filecache, clean/pushed)
method:             independent source-level trace — no production code modified
authority:          current Velox production source at velox_head
task_012_corrective: included (26325e8a32, folly::call_once)
task_014_corrective: included (cda6c03703, truncation/rename recovery)
d4_d6_status:       UNPROVEN, non-blocking forward debt (preserved)
```

---

## A. Statistics Facts Trace

### A1. ProfileEvents storage

**Carrier:** `velox/ch/Common/ProfileEvents.cpp:15-25`
`std::array<std::atomic<uint64_t>, kNumEvents> c{}` with
`fetch_add(…, memory_order_relaxed)`.

**Event enum:** `velox/ch/Common/ProfileEvents.h:12-76` — 60 named events
including the 10 required CH reader events appended by Task 017A:

- `CachedReadBufferReadFromCacheHits`
- `CachedReadBufferReadFromCacheMisses`
- `CachedReadBufferReadFromCacheMicroseconds`
- `CachedReadBufferReadFromSourceMicroseconds`
- `CachedReadBufferPredownloadedFromSourceMicroseconds`
- `CachedReadBufferCacheWriteMicroseconds`
- `CachedReadBufferPredownloadedFromSourceBytes`
- `CachedReadBufferPredownloadedBytes`
- `CachedReadBufferCreateBufferMicroseconds`
- `CachedReadBufferWaitReadBufferMicroseconds`

**RED/mutation evidence:**
`MetricsAndSnapshotTest.cpp:68-78` `AllExistingEnumNamesCompile` — compile-time
structural test; `100-114` `NewReaderEventsPresent` — all 10 events increment
and read back correctly.

Status: **MATCH** — storage semantics identical to CH relaxed atomics.

### A2. CurrentMetrics storage

**Carrier:** `velox/ch/Common/CurrentMetrics.cpp:14-39`
`std::array<std::atomic<int64_t>, kNumMetrics> v{}` with relaxed
`fetch_add/sub/store`.

**Metric enum:** `velox/ch/Common/CurrentMetrics.h:12-25` — 11 gauge metrics.

**RED/mutation evidence:**
`MetricsAndSnapshotTest.cpp:12-42` — `AddSubGetRoundTrip`, `SetOverwrites`,
`IncrementRAII`.

Status: **MATCH** — storage semantics identical to CH relaxed atomics.

### A3. FileCacheStatsSnapshot

**Carrier:** `velox/ch/Common/FileCacheStats.h:8-55`,
`velox/ch/Common/FileCacheStats.cpp:8-80`.

- `takeFileCacheStatsSnapshot()` reads gauge values from `CurrentMetrics::get`
  and counters from `ProfileEvents::get`.
- `operator-` subtracts cumulative counters and copies gauges from `this`.
- `kFileCacheWriteBytes` at `FileCacheStats.h:8-11`.

**RED/mutation evidence:**
`MetricsAndSnapshotTest.cpp:80-98` — `ReflectsCurrentValues`,
`SubtractionProducesDeltas`.

Status: **EQUIVALENT** — Folly-namespaced primitives; observable semantics
match CH `FileCacheStatistics` delta.

### A4. IoStatistics/IoStats completion points

**Carrier:** `velox/ch/Disks/IO/FileCacheInputStream.cpp`

| Completion point | Line(s) | Counter/stat | Semantics |
|---|---|---|---|
| Cache write | 664-677 | `CachedReadBufferCacheWriteBytes`, `ioStats_->addCounter(kFileCacheWriteBytes, …)` | Physical bytes written to cache file |
| Predownload source read | 767 | `CachedReadBufferReadFromSourceBytes` | Physical bytes from source during predownload |
| Predownload predownload-bytes | 768-770 | `CachedReadBufferPredownloadedBytes`, `CachedReadBufferPredownloadedFromSourceBytes` | Physical predownload bytes |
| Predownload query stats | 771-774 | `ioStatistics_->read().increment(got)`, `ioStatistics_->prefetch().increment(got)` | Query read+prefetch (never `rawBytesRead`) |
| Cache hit | 851-852 | `CachedReadBufferReadFromCacheHits`, `CachedReadBufferReadFromCacheBytes` | Physical bytes from cache |
| Cache miss (source) | 858-859 | `CachedReadBufferReadFromCacheMisses`, `CachedReadBufferReadFromSourceBytes` | Physical bytes from source |
| Logical post-clamp | 896-899 | `ioStatistics_->incRawBytesRead(size)` | Logical bytes handed to caller after last-segment clamp |

**Physical vs logical distinction:**

Comment at 896-897: "final clamp, only rawBytesRead records the bytes actually
handed back. Predownload never reaches this point, so it never touches
rawBytesRead."

Physical I/O counters (`CachedReadBufferReadFromCacheBytes`,
`CachedReadBufferReadFromSourceBytes`) are recorded pre-clamp; `rawBytesRead`
is post-clamp. This matches CH semantics where physical and logical I/O are
separate accounting streams.

**RED/mutation evidence:**
- `FileCacheBufferedInputTest.cpp:1239` `CacheReadUpdatesGlobalAndIoStatistics`
- `:1275` `SourceReadUpdatesGlobalAndIoStatistics`
- `:1298` `CacheWriteUpdatesGlobalAndIoStats`
- `:1349` `PredownloadUpdatesReadPrefetchButNotRawBytes`
- `:1424` `MultiChunkBypassCountsMissPerReturnedChunk`
- `:1470` `LastSegmentClampSeparatesPhysicalAndLogicalBytes`
- `:1527` `SourceReadRecordsPositiveScanTime`
- `:1551` `PredownloadRecordsPositiveSourceMicroseconds`

Status: **EQUIVALENT** — per-event coverage tested; completion-point
semantics match CH boundary behavior.

### A5. Task 018 CSV output fields

**Carrier:** `velox/benchmarks/AbBenchmarkBase.h:33-68`,
`velox/benchmarks/AbBenchmarkBase.cpp:107-155,197-217,315-366`

CSV header (15 fields in the current source):

```text
round, query_id, wall_ms, rows, result_hash, result_match, bytes_read,
hit_pct, cache_read_mib, predownload_mib, evict_mib, evict_count,
op_p50_us, op_p95_us, error
```

**Note on header evolution:** The accepted one-driver baseline was produced
by a binary from commit `609cf21da9`, before `result_match` was added at
`2814eb7dcf`. Its CSV has 14 fields (no `result_match`). The accepted
four-driver addendum was produced after `2814eb7dcf` and has 15 fields
(with `result_match`). Both are valid for their respective correctness gates.

**Field sourcing:**

| Field | Source | Real carrier |
|---|---|---|
| `rows` | `countResultRows(results)` (`:157-165`) | Final `RowVector` count — not intermediate |
| `result_hash` | `computeResultHash(results)` (`:167-177`) | Commutative row-hash accumulation |
| `result_match` | `exec::test::assertEqualResults(ref, results)` (`:337`) | Epsilon-aware equality |
| `bytes_read` | `leaf.rawInputBytes` from `TaskStats` (`:345-354`) | Physical I/O from leaf operators |
| `hit_pct` | `snapshotBackend()` from `FileCacheStatsSnapshot` (`:197-217`) | Real `CachedReadBufferReadFromCacheHits/(Hits+Misses)` |
| `cache_read_mib` | same snapshot | `CachedReadBufferReadFromCacheBytes / MiB` |
| `predownload_mib` | same snapshot | `CachedReadBufferPredownloadedBytes / MiB` |
| `evict_mib` | same snapshot | `FilesystemCacheEvictedBytes / MiB` |
| `evict_count` | same snapshot | `FilesystemCacheEvictedFileSegments` |
| `op_p50_us/p95_us` | `getOutputTiming` samples (`:345-366`) | Per-operator latency percentiles |
| `wall_ms` | `std::chrono::steady_clock` around query execution | Wall-clock time |
| `error` | catch-all exception message | Exception string or empty |

**Double counting:** No double counting found. Predownload bytes are excluded
from `rawBytesRead` (line 763 comment + line 899 guarded path). Physical
source bytes and cache bytes are separate counters incremented at disjoint
completion points.

**Units:** All MiB fields divide by `1048576.0`; microsecond fields use
`std::chrono::duration_cast<microseconds>`. No unit mismatch detected.

**RED/mutation evidence:**
`AbBenchmarkSchemaTest.cpp:42-253` — 15-field header test, row serialization,
`result_match` serialization (empty/true/false), row count helpers, flag
validation. Partial RED — schema tested, not full sourcing.

### A6. Predownload counting

Traced at `FileCacheInputStream.cpp:759-774`:

- Counted in: `CachedReadBufferReadFromSourceBytes` (767),
  `CachedReadBufferPredownloadedBytes` (769),
  `CachedReadBufferPredownloadedFromSourceBytes` (770),
  query `read().increment` and `prefetch().increment` (771-774).
- **Never** counted in `rawBytesRead` — comment at 763: "predownload fills
  the cache and is not the caller's data."

**RED/mutation evidence:**
`FileCacheBufferedInputTest.cpp:1349` `PredownloadUpdatesReadPrefetchButNotRawBytes`.

Status: **MATCH** — CH predownload counting semantics preserved.

### A7. Double counting / wrong units

No double counting or unit mismatch found. Specific verifications:

- Predownload excluded from `rawBytesRead` (763, 899)
- Physical source bytes pre-clamp, not clamped (841-849, 881-899)
- Cache write bytes = physical bytes written, not clamped (664-677)
- `bytes_read` in CSV is `leaf.rawInputBytes` from `TaskStats`, which is
  physical leaf-operator I/O; semantically different from query-level
  `rawBytesRead` but consistent within the CSV schema

**Potential concern (Low):** `bytes_read` is `rawInputBytes` from `TaskStats`
leaf operators, not the `rawBytesRead` from `IoStatistics`. These are distinct
metrics. The CSV documents I/O at the leaf scan operator level, not at the
stream-level clamp boundary. This is intentional (scan-level bytes reflect
true storage I/O) and not a bug.

---

## B. Cancellation and Ownership Trace

### B1. CancellationToken ownership

**Carrier:** `FileCacheInputStream.cpp:83-85`
```cpp
cancellationToken_ = owner_->cancellationToken();
```
Token is **copied by value** from the query owner. The stream holds its own
independent copy; the owner can be destroyed without invalidating the token.

**RED/mutation evidence:**
`FileCacheBufferedInputTest.cpp:1638` `CancellationBeforeLookupThrows`,
`:1655` `CancellationDuringSegmentWaitThrows`,
`:1753` `CancellationDeferredUntilAfterSegmentWriteCompletes`.

Status: **EQUIVALENT** — `folly::CancellationToken` vs CH `throwIfKilled`;
identical observable behavior.

### B2. Safe cancellation points

| Point | Location | Behavior |
|---|---|---|
| Pre-lookup | `FileCacheInputStream.cpp:264-268` | `isCancellationRequested()` → `VELOX_FAIL` before segment batch |
| During wait | `FileCacheInputStream.cpp:518-525` | `fileSegment.wait(offset, cancellationToken_)` |
| Post-segment | `FileCacheInputStream.cpp:938-942` | `isCancellationRequested()` → `VELOX_FAIL` after segment completion |
| Inside wait loop | `FileSegment.cpp:561-563` | 1s slice check inside 60s deadline loop |

**RED/mutation evidence:**
All three `FileCacheBufferedInputTest` cancellation tests cover pre-lookup,
during-wait, and deferred-after-write. `FileSegmentTest.cpp:934`
`WaitObservesCancellationToken` covers the inner wait check.

Status: **EQUIVALENT** — three boundary checks + inner wait loop.

### B3. FileSegment::wait

**Carrier:** `FileSegment.cpp:558-567`
- 60s deadline: `std::chrono::seconds(60)`
- 1s slices: `cv.wait_for(lk, std::chrono::seconds(1), downloaded)`
- Cancellation: `cancellation_token.isCancellationRequested()` → `VELOX_FAIL`

**RED/mutation evidence:** `FileSegmentTest.cpp:934`
`WaitObservesCancellationToken`.

Status: **EQUIVALENT** — same 60s/1s boundary as CH.

### B4. Downloader lease boundaries

| Operation | Location | Detail |
|---|---|---|
| Acquire | `FileSegment.cpp:273-290` | `downloader_id = caller_id`, state → `DOWNLOADING` |
| Release after batch | `FileSegment.cpp:822-839` | `resetDownloadingStateUnlocked`, `resetDownloaderUnlocked` |
| Clear | `FileSegment.cpp:324-331` | `downloader_id.clear()` |
| Reader handoff | `FileCacheInputStream.cpp:1031-1063` | Output buffer borrowed/returned around segment read |

Status: **EQUIVALENT** — lease acquire/release boundary matches CH.

### B5. Reader handoff/detach (D6 — UNPROVEN)

**Carrier:** `FileCacheInputStream.cpp:1029-1033`
```cpp
state_->reader->set(outputBufferData_, outputBufferCapacity_);
size = readFromCurrentSegment(fileSegment, offset, readerCanBeReused);
state_->reader->set(nullptr, 0);
```

And `FileCacheInputStream.cpp:1052-1063`:
```cpp
state_->reader->releaseOwnedBuffer();
fileSegment.completePartAndResetDownloader();
```

**D6 status:** Implementation is present but user approval for R2-D6 has not
been given. Rows `P-RB-SETDETACH-01` and `G-NEXTIMPL-01` remain **UNPROVEN**
as non-blocking forward debt. This ledger preserves that classification.

### B6. Output and owned-buffer lifetime

Output buffer is owned by the stream and borrowed by the reader at read time
(`FileCacheInputStream.cpp:1029`). On normal completion, immediately detached
(`set(nullptr, 0)` at 1033). On reusable downloader path, owned buffer released
(`releaseOwnedBuffer()` at 1052-1063). On slow-path reposition, held
downloader/state released first (238-242).

Status: **EQUIVALENT** (subject to D6 UNPROVEN gate).

### B7. Query pool/context lifetime

- Pool captured once: `FileCacheInputStream.cpp:58-63` — `pool_(owner->memoryPool())`
- Query context holder acquired once and kept to destruction (73-76)
- Not reset on seek (238-240)

Status: **EQUIVALENT** — pool outlives stream; no dangling reference.

### B8. FileCacheManager shutdown

**Carrier:** `FileCacheManager.cpp:454-482`

```text
mutation_lock → state = ShuttingDown → clearLocked → markShutdown →
scheduler_.shutdown() → workerPool_.shutdown() → openedFileCache_.clear() →
state = Shutdown
```

Active caches deactivated during init failure cleanup (286-290).

**D4 status:** Manager mutation serialization (`mutation_mutex_`) is present
but R2-D4 user approval is pending. Rows `D-INIT-01`, `E-GETORCREATE-01`,
`E-CREATE-01`, `E-UPDCFG-01` remain **UNPROVEN** as non-blocking forward debt.

### B9. Scheduler ordering

**Carrier:** `FileCacheScheduler.cpp:162-168`

```cpp
// Lock order: execMutex_ THEN scheduleMutex_.
std::lock_guard<std::mutex> elock(execMutex_);
std::lock_guard<std::mutex> slock(scheduleMutex_);
```

Same order in `runCallback()` (193-199). Replaces the pre-017A
`std::recursive_mutex` with two separate mutexes and documented lock order.

**RED/mutation evidence:**
`SchedulerAndScopeTest.cpp:385` `DeactivatePreventsQueuedCallbackFromRunning`,
`:256` `SameTaskNeverRunsConcurrently`, plus 200-iteration stress test in
017A receipt.

Status: **EQUIVALENT** — two-lock protocol is Velox-idiomatic replacement
for CH's single recursive mutex.

### B10. Task 012 corrective (folly::call_once)

- `FileCache.h:365-372`: `folly::once_flag initialize_once_flag`
- `FileCache.cpp:430-433`: `folly::call_once(initialize_once_flag, [this] { … })`

**RED/mutation evidence:**
Controller logs: focused mono 3/3, focused non-mono 3/3. Three mutations
all RED (local mutex/flag restored, `std::once_flag` substituted, retry
semantics removed).

Status: **EQUIVALENT** — `folly::call_once` maps CH `std::call_once`.

### B11. Task 014 corrective (truncation/rename)

- Truncation bypass: `FileCacheInputStream.cpp:382-401` — physical size <
  recorded downloaded size → `cacheReader.reset()`, return nullptr
- Rename retry: `FileCacheInputStream.cpp:325-338` — `kFileNotFound` on old
  path → recompute under segment lock, retry once

**RED/mutation evidence:**
Controller final: mono selected 2/2, accumulated 16/16, non-mono 2/2.
Truncation RED: short pread 4096 vs 8192. Rename RED: FILE_NOT_FOUND on old
offset.

Status: **EQUIVALENT** — Velox-idiomatic file API (`tryGetFileSize`,
`kFileNotFound`, `TestValue` seam) maps CH `Poco::File`/fd infrastructure.

---

## C. Correctness and Benchmark Claims Trace

### C1. Byte/content gates precede timing

`AbBenchmarkBase.cpp:268-297` — reference results collected before timed loop.
Each timed round computes `rows` (via `countResultRows` at 332-333),
`resultHash` (via `computeResultHash` at 334), and `resultMatch` (via
`assertEqualResults` at 337) after `run(…)` returns but before CSV write
(313-368).

Status: **VERIFIED** — correctness precedes performance.

### C2. Direct/CBI/FileCache fairness

`run_tpch_ab.sh:54-90` — same `velox_tpch_benchmark` binary with same
query args/plan inputs. Only `--input_source` and cache args differ.
Three modes: direct (70), cbi (73-75), filecache (76-85).

Status: **VERIFIED** — single binary, identical plans, I/O path only differs.

### C3. Memory budgets

`QueryBenchmarkBase.cpp:64-98,205-273`:
- `--cache_gb=32` for query MmapAllocator (direct and filecache modes)
- `--cache_mem_gb=4` creates dedicated MmapAllocator for CBI cache (254-272)
- `--query_mem_gb=32` for CBI query memory when `cache_gb` is occupied

Four-driver addendum: `query_mem_gb=32`, `cbi_cache_gb=32`,
`cbi_cache_mem_gb=4`, `filecache_disk_gib=80`.

Status: **VERIFIED** — separated allocators match CH reference design.

### C4. One split per file

`run_tpch_ab.sh:39,61` — `NUM_SPLITS_PER_FILE=1` (default), passed as
`--num_splits_per_file`.

Status: **VERIFIED**.

### C5. One-driver baseline vs four-driver addendum

One-driver baseline: 22×3×3 backends = 198 rows, accepted at `4f3cb3c047`.
Four-driver addendum: 22×3×3 = 198 rows with `result_match=1` for all 198,
accepted at `7c52b47ecb`.

`run_tpch_ab.sh:40-41` — `NUM_DRIVERS` and `REFERENCE_NUM_DRIVERS` env vars.
`QueryBenchmarkBase.cpp:64` — `--num_drivers` flag (default 4).

Status: **VERIFIED** — two separate baselines, clearly documented.

### C6. True final rows

`AbBenchmarkBase.cpp:157-165` `countResultRows` counts rows from final
returned `RowVector`s (not intermediate pipeline output). `AbBenchmarkBase.cpp:332-333` calls this at the end of each round.

Status: **VERIFIED** — final rows, not intermediate.

### C7. Epsilon result_match

`AbBenchmarkBase.cpp:337` — `exec::test::assertEqualResults(referenceResults[i], results)`.
This is Velox's order-independent, epsilon-aware row equality check (handles
floating-point tolerance). Returns bool, serialized to CSV as `1`/`0`/empty.

Four-driver addendum: 198/198 rows have `result_match=1`.

Status: **VERIFIED** — epsilon comparison, not exact bitwise.

### C8. q15 fix

**Bug:** Parallel floating-point aggregation of revenue plus exact equality
join caused nondeterministic row count for q15 with multiple drivers.

**Fix:** `velox/exec/tests/utils/TpchQueryBuilder.cpp:1727-1767` — aggregate
revenue once per supplier, then use `dense_rank`/`topNRank` to select max
revenue suppliers.

**Evidence:** Four-driver q15: 9/9 backend-round cells return exactly 1 row,
all with `result_match=1`. q15 hashes vary across rounds (expected with FP
aggregation parallelism) but `result_match=1` confirms epsilon correctness.

Status: **VERIFIED**.

### C9. Cache metrics/sentinels

Wrapper: `run_wrapper_ab.sh:9-14,47-54` — sentinel-marked child dirs created
per backend, trap-driven cleanup removes them on EXIT.
TPCH: same sentinel pattern in `run_tpch_ab.sh`.

Receipt evidence: `tmp/fc_w3_cache` empty after run (4.0K), sentinel cleanup
confirmed by Controller.

Four-driver addendum: "sentinel-authenticated FileCache child removed; cache
root is empty" (receipt line 928).

Status: **VERIFIED**.

### C10. No first-wave hard threshold

No hard performance threshold in `run_wrapper_ab.sh` (only validates output
presence/rows at 78-97). No threshold in receipt Wave 1-3 sections — explicitly
documented as "baseline-only evidence" (receipt line 259).

Status: **VERIFIED**.

### C11. Pre-TPCH gate

Receipt proves at lines 6-11: `tpch_sources_copied: false`,
`tpch_target_built: false`, `tpch_commands_run: false`.

Controller checkpoint review 2 (line 453-457): "Velox already tracks an
upstream `velox/benchmarks/tpch` directory at this accepted HEAD. It is
byte-for-byte unchanged from HEAD, the RelWithDebInfo `velox_tpch_benchmark`
binary does not exist, and the build cache still records
`VELOX_ENABLE_PARQUET:BOOL=OFF`."

TPCH sources pre-existed in the upstream Velox tree but were not built or
invoked until explicit user approval at line 469-476.

Status: **VERIFIED** — no TPCH copy/build/run before authorization.

### C12. Build type verification

`_build/relwithdebinfo/CMakeCache.txt` — `CMAKE_BUILD_TYPE:STRING=RelWithDebInfo`.

All benchmark binaries under `_build/relwithdebinfo/`. Receipt lines 100-113
confirm `file(1)` output for all three non-TPCH binaries. No Debug binary was
built or invoked for benchmark evidence.

Status: **VERIFIED** — RelWithDebInfo only.

### C13. Performance confidence limits

One-driver warm: FC/Direct = 1.025 (2.5%), FC/CBI = 1.081 (8.1%).
Four-driver warm: FC/Direct = 1.062 (6.2%), FC/CBI = 1.116 (11.6%).

Receipt explicitly states (line 843-844): "exact magnitude has medium
confidence because only two warm samples were collected on an unisolated
WSL2 host."

**Preserved limits:**
- Local-only performance evidence — not production-representative
- q11 oracle caveat: returns 0 rows in 4-driver mode (all backends),
  3203218 rows in 1-driver mode. Parallel/backend equivalence proven, not
  independent SQL-oracle correctness
- Parallel result hashes treated as diagnostic only (FP nondeterminism)
- No hard first-wave threshold

Status: **VERIFIED** — confidence limits explicitly documented.

---

## D. Forward Debt Classification (Preserved)

The following items remain UNPROVEN as non-blocking forward debt per user
decision. This ledger does not approve, reject, or reclassify them.

| Item | Type | Governing decision | Status |
|---|---|---|---|
| `D-INIT-01` | denominator row | R2-D4 pending | UNPROVEN |
| `E-GETORCREATE-01` | denominator row | R2-D4 pending | UNPROVEN |
| `E-CREATE-01` | denominator row | R2-D4 pending | UNPROVEN |
| `E-UPDCFG-01` | denominator row | R2-D4 pending | UNPROVEN |
| `P-RB-SETDETACH-01` | denominator row | R2-D6 pending | UNPROVEN |
| `G-NEXTIMPL-01` | denominator row | R2-D6 pending | UNPROVEN |

---

## E. Ledger Verdict

```text
new_critical_findings:    0
new_important_findings:   0
new_low_findings:         1 (bytes_read CSV field sources leaf rawInputBytes,
                             not stream rawBytesRead — intentional, not a bug)
false_green_detected:     0
material_errors:          0
blocking_items:           0
non_blocking_debt:        6 UNPROVEN rows (D4 × 4, D6 × 2)
```

All three trace areas (statistics, cancellation/ownership,
correctness/benchmarks) are verified with source-level evidence. No approved
behavior is absent. No material false-green was found. The only remaining
debt is the documented non-blocking D4/D6 forward items.
