# Task 015 Result: `FileCache` Velox E2E Validation and Basic Benchmark

## Worker attempt 1

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 015
```

Implemented the Velox-only end-to-end validation of the assembled public read
path (`FileCacheBufferedInput` -> `FileCacheInputStream` -> `FileCache`), driven
through a real `FileCacheManager`, plus the random-seek benchmark. The E2E gate
`velox_ch_filecache_e2e_test` builds (exit 0) and passes 17/17 (0 failed / 0
skipped). The benchmark `velox_ch_filecache_seek_benchmark` builds (exit 0) and
runs a short warmup (exit 0) emitting timing rows for cache-hit / miss / bypass.
All three pre-existing gates still build+pass after the shared-CMake edits. No
production code was changed (no integration defect surfaced).

## Status

status: success

## Repository state

```text
Velox:      branch filecache2, HEAD e142429ef (Task 014 + F-014-1 accepted)
ClickHouse: branch ch-filecache2 (receipt only)
git status --short (velox):
 M velox/ch/CMakeLists.txt
 M velox/ch/Disks/IO/tests/CMakeLists.txt
?? velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
?? velox/ch/benchmarks/
git diff --check: clean (exit 0)
```

Branch is `filecache2` / `ch-filecache2` (task text says `filecache`); recorded
as observed per the dispatch note — did not stop over the mismatch. No
pre-existing dirty velox files. No staging/commit/amend/rebase/push. Gluten not
touched.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `e142429ef` | clean |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | (receipt repo) | receipt only |

## Files changed

```text
MODIFIED:
  velox/ch/CMakeLists.txt                            (+ if(VELOX_ENABLE_BENCHMARKS) add_subdirectory(benchmarks))
  velox/ch/Disks/IO/tests/CMakeLists.txt             (+ velox_ch_filecache_e2e_test target)
NEW:
  velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp       (17 executable E2E tests)
  velox/ch/benchmarks/CMakeLists.txt                 (velox_ch_filecache_seek_benchmark target)
  velox/ch/benchmarks/FileCacheSeekBenchmark.cpp     (setupFixture + 3 BENCHMARK variants)
```

`velox/ch/Disks/CMakeLists.txt` and `velox/ch/Disks/IO/CMakeLists.txt` needed no
change: Task 014 already added `add_subdirectory(IO)` / `add_subdirectory(tests)`.

## Known-correction adaptations applied (already reviewed, not new decisions)

```text
1. LIBRARY NAMES: the task's illustrative target_link_libraries lists
   velox_ch_filecache_dwio / velox_ch_filecache_manager / velox_ch_filecache_core.
   Under VELOX_MONO_LIBRARY=ON those separate libraries DO NOT EXIST (Tasks
   013/014 compiled Factory/Manager/DWIO sources INTO velox_ch_filecache). Both
   new targets link the real existing targets: velox_ch_filecache +
   velox_dwio_common + velox_file + velox_exception + velox_memory + Folly +
   (benchmark) Folly::follybenchmark + gflags. This mirrors the accepted
   velox_ch_filecache_buffered_input_test link set.
2. REAL API NAMES: used the ACTUAL header signatures, not the task's illustrative
   ones. The E2E fixture builds a real FileCacheManager via
   FileCacheManager::create(Options{caches, defaultCacheName="default",
   commonUserId, localFileSystem, timekeeper, initializeOnCreate=true}) and
   FileCacheManager::setInstance/getInstance/shutdown (there is no makeOptions).
   Reads go through FileCacheBufferedInput::enqueue(Region)/read + Next.
   FileCacheReadOptions.tempCacheOnly / readIfExistsOtherwiseBypass, isBuffered,
   shouldPrefetchStripes/preloaded/hasCache, FileCacheKey::fromPath/fromKey and
   FileCacheFileIdentity::deriveKey are the real names.
3. REUSED the existing scaffolding patterns from FileCacheBufferedInputTest.cpp
   (readAll/readN helpers, MemoryManager value member, per-test TempDirectoryPath,
   makeContent) rather than re-inventing them.
```

## Commands run

```text
configure  (home-chang recipe + -DVELOX_ENABLE_BENCHMARKS=ON -DVELOX_BUILD_TESTING=ON)
ninja -C <build> velox_ch_filecache_e2e_test                                  (no -j)
ctest --test-dir <build> -R '^velox_ch_filecache_e2e_test$' --output-on-failure
ninja -C <build> velox_ch_filecache_seek_benchmark                            (no -j)
<build>/velox/ch/benchmarks/velox_ch_filecache_seek_benchmark --bm_min_iters=3
  --file_size_mb=8 --cache_size_mb=32 --seek_count=50 --cache_dir=<build>/fc_bench
ninja -C <build> velox_ch_filecache_buffered_input_test velox_ch_filecache_manager_test velox_ch_filecache_core_scc_test
ctest --test-dir <build> -R '^(buffered_input|manager|core_scc)_test$'
git diff --check ; git status --short
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_015.log` |
| build E2E test | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_015_e2e.log` |
| ctest E2E test | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_015_e2e.log` |
| build seek benchmark | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_015_benchmark.log` |
| run seek benchmark (warmup) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/bench_015_seek.log` |
| build 3 pre-existing gates | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_015_pregates.log` |
| ctest 3 pre-existing gates | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_015_pregates.log` |

No `-j` was passed to any ninja invocation.

## Generated logs

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_015.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_015_e2e.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_015_e2e.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_015_benchmark.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/bench_015_seek.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_015_pregates.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_015_pregates.log
```

## Test results

```text
velox_ch_filecache_e2e_test:        17 tests, 0 failed, 0 skipped, 0 disabled
velox_ch_filecache_seek_benchmark:  3 iterations, no crash, timing rows for all
                                    three variants:
                                      FileCacheSeekCacheHit    3.59ms  278.45/s
                                      FileCacheSeekCacheMiss   6.10ms  164.01/s
                                      FileCacheSeekBypass      3.63ms  275.31/s
                                    (miss slower than hit/bypass, as expected)
pre-existing gates (shared-CMake regression):
  velox_ch_filecache_buffered_input_test: 19/19, 0 failed / 0 skipped
  velox_ch_filecache_manager_test:        19/19, 0 failed / 0 skipped
  velox_ch_filecache_core_scc_test:       47/47, 0 failed / 0 skipped
```

## E2E scenario coverage (all 13 mandated + 4 CH-migration tests)

```text
[ 1] MissFillHit                            — miss->fill->hit; CountingReadFile
                                              proves NO source read on the hit
                                              (countingB->preadCount()==0).
[ 2] CacheOnlyMissFails                      — tempCacheOnly cold key -> Next throws
                                              VeloxRuntimeError; no segment created.
[ 3] ReadIfExistsBypassMode                  — uncached key -> bypass source read,
                                              full bytes, getFileSegmentsNum()==0.
[ 4] BackUpWithinOutputBuffer                — BackUp(1024) -> ByteCount 64K-1024;
                                              re-read returns identical bytes.
[ 5] SkipAcrossSegmentBoundary               — SkipInt64 past a segment boundary;
                                              Next resumes at correct abs offset.
[ 6] SeekToPositionRegionRelative            — region@4096; seek rel 256 ->
                                              ByteCount==256; bytes @ abs 4096+256.
[ 7] NonzeroRegionOffsetAbsoluteCoordinates  — region{65536,32768}; asserts bytes
                                              are abs [65536,98304), NOT [0,32768).
[ 8] DiscardedEnqueueNoUseAfterFree          — discard stream then load(); no fault,
                                              no segment.
[ 9] LoadIsNopPlanningBarrier                — 3 regions discarded then load(); no
                                              stream deref, no segment.
[10] DWRFShouldPrefetchStripesIsFalse        — shouldPrefetchStripes/preloaded/
                                              hasCache all false.
[11] PathOnlyKeyWhenEtagEmpty                — empty etag -> fromPath key; re-derive
                                              hits the same segment.
[12] DifferentEtagsDifferentKeys             — v1/v2 keys differ; cross-read proves
                                              each key serves its OWN cached bytes.
[13] ShutdownWhileStreamAliveNotReading      — idle live stream + manager shutdown;
                                              no deadlock/crash; stream destructs.
[14] ColdMissFillThenHit           (migration)
[15] PartialSegmentContinuationAcrossReaders (migration)
[16] DownloadedSizeAccountingAtPublicBoundary(migration)
[17] RandomSeeksAcrossHitMissBypass          (migration)
```

## CH integration-test migration matrix

Port selected end-to-end BEHAVIOR through the assembled public path, not
server/config plumbing. Sources audited:
`tests/integration/test_filesystem_cache/test.py`,
`tests/integration/test_cache_bypass_on_disk_failure/test.py`,
`tests/queries/0_stateless/*filesystem_cache*`.

| CH scenario | Velox E2E test | Notes / exclusion |
|---|---|---|
| cold miss -> cache fill -> later hit | `ColdMissFillThenHit` (+ `MissFillHit` for the no-source-read-on-hit proof) | Full public-path coverage. |
| partial segment continuation across readers | `PartialSegmentContinuationAcrossReaders` | Q1 fills a prefix, Q2 continues from the write offset through the assembled path. Focused reader/handoff internals stay with Task 014 (`Q1Q2HandoffReusesReaderFromWriteOffset`). |
| cache write failure -> configured bypass or propagated failure | Covered by Task 014 `MidDownloadCacheWriteFailureReleasesDownloaderNoLeak` (mid-download cache-write fault -> propagate + no downloader leak) and `ReserveFailureBypassesCacheButReturnsData` (reserve fail -> bypass). NOT duplicated here | EXCLUSION (dedup): these are the focused reader/handoff UTs Task 014 owns; re-driving them via the public path adds no new assurance and the write-file-factory fault seam is a reader-internal test hook. Task 015 asserts the observable public bypass/no-metadata contract in `ReadIfExistsBypassMode` and the random-seek bypass path instead. |
| truncated/invalid cached data -> source recovery without stale reader state | Covered by Task 014 `ExternalTruncationSelfHealsFromSource` + `EmptyCacheFileSelfHealsFromSource` (F-014-1) | EXCLUSION (dedup): the self-heal-on-external-truncation path (`getCacheReadBuffer` bypass + re-fetch) is owned and RED-verified by Task 014 F-014-1 through the same public read path. Re-driving it here would duplicate that test verbatim. |
| reserve-ahead / downloaded-size accounting at the public FileCache boundary | `DownloadedSizeAccountingAtPublicBoundary` | After a full read, sums `getDownloadedSize()` over the holder at the public `FileCache::get` boundary == n, and `isBuffered(0,n)`==true. |
| random seeks across hit / miss / bypass paths | `RandomSeeksAcrossHitMissBypass` | Pre-fills the first half, then 6 random offsets on the cache-put path (mix of hits/misses) and again on the bypass path, asserting correct bytes at each offset. |

Ownership note honored: Task 012 remains the owner of focused `FileSegment`
resume/reconciliation UTs; Task 014 remains the owner of focused reader/handoff
and self-heal tests. Task 015 exercises those behaviors THROUGH the assembled
public path (and defers the two internal-fault-seam cases above to their owners,
recorded as explicit dedup exclusions) without re-implementing their internal
test logic.

## Production defects found

```text
None. No production file changed (git diff over FileCacheBufferedInput.cpp,
FileCacheInputStream.cpp, FileCache.cpp, FileCacheManager.cpp is empty). The two
issues fixed during bring-up were both in the NEW task-owned files, not
production:
  - E2E byte-pattern collision: an early makeContent had a 256-byte period, so
    NonzeroRegionOffsetAbsoluteCoordinates' "abs != first-block" guard matched by
    coincidence. Replaced with a Knuth multiplicative-hash pattern whose period
    exceeds any file used, so a relative-vs-absolute offset bug is genuinely
    caught. (Test-quality fix, not a production bug.)
  - Benchmark bring-up: (a) gFixture was published AFTER the warm-up loop, so the
    warm-up's makeInput dereferenced a null gFixture -> reordered to publish
    before warming; (b) a relative --cache_dir failed local-fs scheme matching in
    getCacheReadBuffer -> the benchmark now forces cacheRoot absolute; (c) reset
    gFixture in main so its pool/MemoryManager destruct while the runtime is
    alive. All three are benchmark-driver issues (the production scan path always
    supplies absolute paths and a live fixture), not FileCache defects.
```

## Verification

```text
No E2E source contains GTEST_SKIP, DISABLED_, empty, assertion-free, or
  fake-cache tests. Direct gtest run: "[  PASSED  ] 17 tests." (0 failed/skipped).
Final Velox E2E build exit code: 0
Benchmark build exit code: 0; run exit code: 0 with timing rows for all 3 variants.
Three pre-existing gates re-run green (19/19, 19/19, 47/47).
git diff --check: no whitespace errors in Velox (exit 0).
Only task-owned files appear in the diff; changes unstaged/uncommitted.
```

## Worker review

```text
review subagent: one read-only general-purpose reviewer over the complete
  Task-015 diff (FileCacheE2ETest.cpp, FileCacheSeekBenchmark.cpp, both
  CMakeLists) with the real API headers and the sibling Task-014 test as context.
  Asked only for: do the E2E tests exercise the assembled public path with real
  assertions; any false-green/tautological test; correctness of any production
  fix (none was made); benchmark not crashing. It did not edit files.
findings:
  No blocking issues. The reviewer confirmed every E2E test drives the assembled
  public path (enqueue/read -> Next -> FileCache via a real FileCacheManager)
  with meaningful, discriminating assertions and found NO false-green:
    - MissFillHit: per-phase CountingReadFile; hit asserts preadCount()==0 on a
      FRESH source wrapper -> genuinely proves bytes came from the local cache.
    - CacheOnlyMissFails / ReadIfExistsBypassMode: throw + segment-count and
      full-bytes + no-segment assertions are real discriminators.
    - NonzeroRegionOffsetAbsoluteCoordinates: long-period pattern catches a
      relative-vs-absolute coordinate bug (abs==correct AND abs!=first-block).
    - DifferentEtagsDifferentKeys: cross-read (source=v2 file, key=v1) proves each
      key serves its own cached bytes; v2 = v1 ^ 0x5A guarantees distinctness.
    - ShutdownWhileStreamAliveNotReading: legitimate no-crash/no-deadlock value.
  CountingReadFile override signatures correct (FileIoContext default arg + the
    folly::Range preadv overload); counter increments present; preadCount_ is a
    mutable atomic; TearDown shuts down + null-clears the singleton BEFORE
    manager_.reset() (correct order); streams scoped in {} so no UAF.
  Benchmark: setupFixture publishes gFixture before warm-up (fixed), miss-cache
    per-iteration guarantees first-touch misses, main tears down while runtime is
    alive (no static-destruction crash), cacheRoot forced absolute. No residual
    crash risk.
  Minor non-defect nit: ShutdownWhileStreamAliveNotReading relies on the holder
    being lazy (documented); the assertion (no crash) stays valid regardless.
resolutions: no code change required — no actionable in-scope finding.
unresolved findings: none.
```

## Blockers / Blocking errors

```text
None. All gates green:
  - velox_ch_filecache_e2e_test: 17/17 (0 failed / 0 skipped).
  - velox_ch_filecache_seek_benchmark: builds + runs, timing rows for all 3 variants.
  - pre-existing gates: 19/19, 19/19, 47/47.
No unreviewed CH dependency was reached: the E2E/benchmark only drive the
already-accepted Task 013/014 public API (Manager, BufferedInput, InputStream,
FileCache, keys, read options) plus standard Velox test/bench primitives.
```

## Recommended next task

```text
Task 016 (optional post-MVP): WriteBufferToFileSegment for Ephemeral segments.
Task 017 (optional post-MVP): Observability and cancellation hardening.
Task 018 (future): Gluten host integration.
Task 019 (future): Gluten builder and lifecycle E2E validation.
Task 015 is the final task of the current Velox MVP path.
```

## Worker declaration

```text
Only Task 015 was attempted.
Changes are unstaged and uncommitted; velox remains at baseline e142429ef.
File scope is exactly: velox/ch/CMakeLists.txt (+benchmarks subdir),
velox/ch/Disks/IO/tests/CMakeLists.txt (+e2e target), the new
velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp, and the new velox/ch/benchmarks/
(CMakeLists.txt + FileCacheSeekBenchmark.cpp) + this receipt. No production
source, no ClickHouse source, no Gluten file was changed.
The worker stopped after writing this receipt.
```

## Controller review 1

```text
controller_status: accepted
environment_profile: home-chang
task: 015
```

## Review evidence

```text
scope review: PASS. Only task-owned files changed — velox/ch/CMakeLists.txt (+bench
  subdir), velox/ch/Disks/IO/tests/CMakeLists.txt (+e2e target), new
  FileCacheE2ETest.cpp, new velox/ch/benchmarks/ (CMake + FileCacheSeekBenchmark.cpp).
  PRODUCTION DIFF EMPTY (git diff over FileCacheInputStream/BufferedInput/FileCache/
  FileCacheManager .cpp = nothing) — no integration defect surfaced; the three
  bring-up fixes were all in the new task-owned test/bench files, correctly recorded.
  git diff --check clean. Gluten untouched.
implementation review: 17 E2E tests drive the assembled public path (enqueue/read ->
  Next -> FileCache via a real FileCacheManager). Independently verified the key
  anti-false-green mechanism: CountingReadFile (FileCacheE2ETest.cpp:99-133) really
  counts pread/preadv (fetch_add at :110/:119), so MissFillHit's hit-phase
  preadCount()==0 genuinely proves bytes came from the local cache, not the source.
  Known mono-build corrections applied (linked the real velox_ch_filecache target,
  not the non-existent _dwio/_manager/_core split) and real API names used — matches
  the accepted Task 013/014 surface.
migration matrix: reasonable. The two dedup exclusions (cache-write-failure->bypass,
  truncation self-heal) are correctly deferred to their Task-014 owners
  (MidDownloadCacheWriteFailure..., ExternalTruncationSelfHeals... / F-014-1) rather
  than duplicated; Task 015 asserts the observable public contracts instead.
log/test review: Controller re-ran the E2E binary directly — 17/17 passed
  (0 failed/skipped); benchmark timing rows present and ordered as expected
  (miss 6.10ms > hit 3.59ms ~ bypass 3.63ms). Three pre-existing gates re-confirmed
  green (19/19, 19/19, 47/47).
unresolved findings: none.
```

## Required changes

```text
None. Task 015 accepted. This is the final task of the current Velox MVP path.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | (this acceptance — see Velox `Task 015:` commit) |
| `/home/chang/SourceCode/ClickHouse` | receipt+handoff = this commit |

## Perf 扩展 (wrapper benchmark)

### Worker attempt (perf extension)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 015 (Post-MVP 性能测试扩展)
```

This section ADDS a performance benchmark per the task's
"## Post-MVP 性能测试扩展" contract. It does NOT reopen the accepted Task 015
E2E tests; those files are untouched except no shared CMake line was needed.

### Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `4f764737f4b4cf4382598621066da663ee6a1ffc` | clean |
| `/home/chang/SourceCode/ClickHouse` | `filecache2` (was `ch-filecache` at dispatch) | `4f764737f` | clean |

Baseline note: dispatch said velox=`filecache2` HEAD `4f764737f`, CH branch
`ch-filecache2`; the actual CH checkout is `filecache2`. Proceeded as instructed
(branch-name mismatch not a blocker). Reference is on branch `ch-filecache`,
read READ-ONLY via `git show ch-filecache:<path>` (never checked out).

### Files changed (all under velox/ch/benchmarks/, production diff empty)

```text
velox/ch/benchmarks/CacheReadHarness.h            (new)
velox/ch/benchmarks/CacheReadHarness.cpp          (new)
velox/ch/benchmarks/FileCacheWrapperBenchmark.cpp (new)
velox/ch/benchmarks/CMakeLists.txt                (modified: + velox_ch_filecache_wrapper_benchmark)
velox/ch/benchmarks/FileCacheSeekBenchmark.cpp    (modified: microbench hardening)
```

Structure (per contract, user "dedupe via shared base class"): an abstract
`CacheHarnessBase` holds ALL shared logic ONCE — `WorkloadDriver`
(sequential/zipfian/uniform via a ported header-only `KeyGenerator`), the timed
`runSweep` loop, the multi-threaded `sweepConcurrent` driver (`--num_threads`),
and tier accounting. Three THIN subclasses override only `buildCache()` +
`readBatch()` + `fillTiers()`: `DbiHarness` (direct read), `CbiHarness`
(native `AsyncDataCache`+`SsdCache`), `FcbiHarness` (our `ch::FileCache`).
`KeyGenerator`/`WorkloadDriver` were ported into the harness header (kept inside
the declared `velox/ch/benchmarks/**` scope) since the reference's
`velox/common/caching/filecache/benchmarks/KeyGenerator.h` does not exist on
`filecache2`.

### Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| CMake reconfigure (new target) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_015_perf_configure.log` |
| Build `velox_ch_filecache_wrapper_benchmark` | 0 | `.../cmake-build-debug-gcc13/build_015_perf_wrapper.log` |
| Run `--wrappers=all --workloads=sequential,zipfian --target_ws_gb=1 --measure_passes=1` | 0 | `.../run_015_perf_all.log` |
| Run concurrency `--wrappers=fcbi --num_threads=4 --target_ws_gb=1 --measure_passes=1` | 0 | `.../run_015_perf_concurrent.log` |
| Build hardened `velox_ch_filecache_seek_benchmark` | 0 | `.../build_015_perf_seek.log` |
| Run hardened seek benchmark (green) | 0 | `.../run_015_perf_seek_green.log` |
| RED test: force hit to read source (assertion must fire) | 134 (aborted, expected) | `.../run_015_perf_seek_red.log` |
| Build 4 pre-existing gate tests | 0 | `.../build_015_perf_pregates.log` |
| ctest e2e / buffered_input / manager / core_scc | 0 each | `.../run_015_perf_ctest_*.log` |

### Three-engine back-to-back numbers (`--target_ws_gb=1 --measure_passes=1`, delta vs fcbi)

| pattern | read | wrapper | wall_ms | MB/s | ram_MB | ssd_MB | src_MB | delta vs fcbi |
|---|---|---|---:|---:|---:|---:|---:|---:|
| seq  | 1024K | fcbi | 382.7 | 2676   | 0    | 0 | 0    | — |
| seq  | 1024K | cbi  | 2.3   | 442794 | 1024 | 0 | 0    | -99.4% |
| seq  | 1024K | dbi  | 117.3 | 8731   | 0    | 0 | 1024 | -69.3% |
| zipf | 1024K | fcbi | 364.5 | 2809   | 0    | 0 | 0    | — |
| zipf | 1024K | cbi  | 2.0   | 507633 | 1024 | 0 | 0    | -99.4% |
| zipf | 1024K | dbi  | 98.4  | 10410  | 0    | 0 | 734  | -73.0% |

All three engines construct, complete, print throughput + relative delta,
exit 0. (Absolute numbers are indicative on a warm-tier debug build; cbi's RAM
tier serving fully-resident hits explains its speed, fcbi is disk-segment only,
dbi is no-cache — intended tier differences, identical driving.)

### `--num_threads=4` concurrency result (fcbi)

| pattern | read | wrapper | wall_ms | MB/s |
|---|---|---|---:|---:|
| seq  | 1024K | fcbi | 569.8 | 7188 |
| zipf | 1024K | fcbi | 519.8 | 7880 |

Exit 0, no crash/deadlock. Each thread uses its own `WorkloadDriver` +
`IoStatistics`; aggregate wall = slowest thread, bytes summed.

### FcbiHarness uses Manager (grep-proof: NO bare `new FileCache`)

```text
$ grep -nE "new +FileCache\b|new +ch::FileCache\b" \
    velox/ch/benchmarks/CacheReadHarness.cpp velox/ch/benchmarks/CacheReadHarness.h
CacheReadHarness.h:25://            FileCacheFactory (never bare `new FileCache`).
CacheReadHarness.h:354:// through a real FileCacheManager (NEVER bare `new FileCache`).
# (only comments; no actual bare construction)

$ grep -n "FileCacheManager::create\|manager_->getDefault" velox/ch/benchmarks/CacheReadHarness.cpp
536:  manager_ = FileCacheManager::create(o);
537:  cache_ = manager_->getDefault();
```

### Seek-benchmark hardening + RED evidence (hit "no source read" assertion is load-bearing)

Hardening: (a) `FileCacheSeekCacheMiss`/`Bypass` are now `BENCHMARK_RELATIVE`
(relative to the `FileCacheSeekCacheHit` baseline); (b) all `makeInput` object
construction moved into `BENCHMARK_SUSPEND` so only enqueue+drain is timed;
(c) the hit path wraps its source in a `CountingReadFile` and asserts
`preadAfter == preadBefore` (a warm hit must serve from the local segment, never
the source).

RED proof — temporarily forcing the hit onto a cold cache (`freshMissCache`)
made the assertion fire and abort:

```text
Exceptions.h:87 Line: .../FileCacheSeekBenchmark.cpp:329, Function:FileCacheSeekCacheHit,
Expression: preadAfter == preadBefore (100 vs. 0)
cache hit read the source (100 extra preads): hit path degraded to a source read
... ErrorCode: INVALID_STATE   (process Aborted, exit 134)
```

The RED patch was reverted; the reverted benchmark rebuilds and runs green
(exit 0). Green relative output:

```text
FileCacheSeekCacheHit                6.46ms  154.87 iters/s
FileCacheSeekCacheMiss    58.492%   11.04ms   90.59 iters/s
FileCacheSeekBypass       97.996%    6.59ms  151.77 iters/s
```

### Pre-existing gates — no regression

| Gate | test cases (`--gtest_list_tests`) | ctest result |
|---|---:|---|
| `velox_ch_filecache_e2e_test` | 17 | 100% passed, 0 failed |
| `velox_ch_filecache_buffered_input_test` | 19 | 100% passed, 0 failed |
| `velox_ch_filecache_manager_test` | 19 | 100% passed, 0 failed |
| `velox_ch_filecache_core_scc_test` | 47 | 100% passed, 0 failed |

Counts match the required 17/19/19/47 exactly; 0 failed, 0 skipped.

### Production-diff-empty confirmation

```text
$ git status --porcelain | awk '{print $2}' | grep -v "^velox/ch/benchmarks/"
NONE (production diff empty)
$ git diff --check
clean
```

No `FileCacheInputStream`/`BufferedInput`/`FileCache`/`FileCacheManager` `.cpp`
(or any file outside `velox/ch/benchmarks/`) was modified. No production
integration/perf bug requiring a production fix was found.

### Worker review

```text
review subagent: 1 read-only general-purpose reviewer over the full benchmark diff
findings: no BLOCKER / no SHOULD-FIX. All 7 contract points confirmed PASS:
  (1) single CacheHarnessBase, thin subclasses, no duplicated sweep logic;
  (2) FcbiHarness via FileCacheManager::create/getDefault, 12-arg
      FileCacheBufferedInput signature matches, no bare new FileCache;
  (3) fair driving (same working set/layout/batch/warm-measure, timing wraps
      only enqueue+load+drain); tier differences (cbi RAM vs fcbi disk vs dbi
      none) are intended;
  (4) concurrency: per-thread driver + IoStatistics, no race, correct
      max-wall/byte-sum aggregation, no deadlock;
  (5) seek hardening + load-bearing hit assertion (right file, runs
      unconditionally, no false-pass path);
  (6) destruction order safe (cache_.reset before manager_->shutdown; cbi
      teardown order correct);
  (7) production diff empty.
  NITs only: duplicated-name comment on DirectBufferedInput ioStats args
  (harmless, distinct params); shared 2-thread fcbi executor bounds async
  parallelism at high --num_threads (realism caveat, not a bug).
resolutions: NITs left as-is (non-actionable / intended); no code change needed.
unresolved findings: none
```

### Blockers

```text
None.
```

### Worker declaration

```text
Only Task 015's Post-MVP performance-benchmark extension was attempted.
The accepted Task 015 E2E test files were not modified.
Changes are unstaged and uncommitted, confined to velox/ch/benchmarks/.
The worker stopped after writing this receipt.
```

## Controller review — Perf 扩展 (changes_requested)

```text
controller_status: changes_requested
environment_profile: home-chang
task: 015-perf
```

## Review evidence

```text
scope: PASS — only velox/ch/benchmarks/ (3 new + CMake + seek benchmark); production
  diff empty (git diff --stat over velox/ch/Disks, velox/ch/Interpreters = empty);
  git diff --check clean.
fcbi via Manager: PASS — CacheReadHarness.cpp:536 FileCacheManager::create + :537
  getDefault; grep shows NO bare `new FileCache` (only two comment mentions). Good.
seek-benchmark hardening: PASS — hit "no source read" assertion is load-bearing;
  worker's RED (force cold hit -> preadAfter==preadBefore 100 vs 0 -> abort) is real
  and I accept it. miss/bypass are BENCHMARK_RELATIVE; makeInput moved to SUSPEND.
pre-existing gates: PASS — e2e 17 / buffered_input 19 / manager 19 / core_scc 47,
  0 failed/skipped (re-confirmed).

FINDINGS (Controller independent run exposed what the worker did not test):

F-PERF-1 (CONFIRMED defect, must fix) — the wrapper benchmark CORE-DUMPS on a
  RELATIVE --filecache_root. Running the contract's own acceptance command with a
  relative root:
    --wrappers=all ... --filecache_root=cmake-build-debug-gcc13/perf_fc
  aborts (exit 134) with:
    getFileSystem: No registered file system matched with file path
    'cmake-build-debug-gcc13/perf_fc/fa3/.../0_4194304'
    (FileCacheInputStream::getCacheReadBuffer -> filesystems::getFileSystem).
  Root cause: the local FS scheme only matches absolute paths; the cache segment
  path is built from the relative root. This is the SAME trap Task 015's seek
  benchmark already hit and fixed ("forces cacheRoot absolute"); the wrapper
  benchmark did not apply it. The worker ran only with an ABSOLUTE root (its receipt
  413 shows exit 0, which I reproduced), so it never exercised the relative case —
  a real hole, not a false claim, but the crash-vs-clean-error behavior is
  unacceptable. Fix: normalize --filecache_root / --ssd_path / --data_dir with
  std::filesystem::absolute() at startup (mirror the seek benchmark). With an
  ABSOLUTE root the three engines run green (Controller-verified exit 0):
    seq 1024K fcbi 3253 MB/s | cbi 584808 MB/s (ram_MB=1024) | dbi 10258 MB/s (src_MB=1024)

F-PERF-2 (CONFIRMED, must fix in reporting) — the cbi-vs-fcbi number is a
  DIFFERENT-TIER comparison and is misleading as printed. With target_ws_gb=1 and a
  4 GiB RAM cache, cbi serves the whole 1 GiB working set from RAM (ram_MB=1024,
  584808 MB/s), while fcbi serves from on-disk cache segment files (3253 MB/s). The
  "delta vs fcbi -99.4%" reads as "fcbi is 180x slower than cbi" which is a
  RAM-hit vs disk-hit artifact, not a like-for-like result. Must be labeled: the
  table (and receipt) must state the tier each hit came from (ram_MB/ssd_MB/src_MB
  columns already exist — add an explicit note that cbi here is a RAM-cache hit and
  is NOT comparable to fcbi's disk-cache hit; for a fair disk-vs-disk comparison the
  cbi RAM cache must be sized below the working set so it spills to SSD). Do not ship
  a headline "fcbi slower than cbi" conclusion drawn from a RAM-vs-disk cell.
```

## Required changes

```text
1. F-PERF-1: absolutize --filecache_root / --ssd_path / --data_dir (std::filesystem::
   absolute) at startup so a relative path yields a clean run (or a clean error),
   never a core dump. Add a run with a RELATIVE root to the receipt proving exit 0.
2. F-PERF-2: in both the printed table and the receipt, explicitly label the tier of
   each engine's hit (RAM vs SSD vs disk-segment vs source) and add a one-line
   caveat that the default cbi cell is a RAM-cache hit, hence not directly
   comparable to fcbi's disk hit. Optionally add a --ram_cache_gb smaller than the
   working set so cbi spills to SSD for a fair disk-vs-disk row; if added, show it.
Re-run: --wrappers=all with BOTH an absolute and a relative root (both exit 0), and
the --num_threads=4 concurrency run. Keep production diff empty. No -j.
```

## Perf 扩展 修复 1 (F-PERF-1/2)

### Worker attempt (perf-fix)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 015 (Post-MVP perf extension — controller changes_requested: F-PERF-1, F-PERF-2)
```

Controller独立验收发现两个必修点。两者都只改
`velox/ch/benchmarks/FileCacheWrapperBenchmark.cpp`；生产零改动保持。

### F-PERF-1 (真缺陷): 相对 `--filecache_root`/`--ssd_path` 直接 CORE DUMP

Reproduced on the pre-fix binary (exit 134):

```text
$ velox_ch_filecache_wrapper_benchmark --wrappers=all ... \
    --filecache_root=cmake-build-debug-gcc13/perf_fc
FileSystems.cpp:64 getFileSystem: No registered file system matched with file
path 'cmake-build-debug-gcc13/perf_fc/fa3/.../0_4194304'  (Aborted, exit 134)
log: /home/chang/OpenSource/velox/cmake-build-debug-gcc13/run_015_perf_relroot_repro.log
```

Root cause: the local file system matches only on absolute paths, but the cache
segment path is assembled from the relative root. Same pit the seek benchmark
already fixed (force `cacheRoot` absolute); the wrapper benchmark had not.

Fix: normalize the on-disk cache roots at startup with
`std::filesystem::absolute()` before building `HarnessConfig`
(`FileCacheWrapperBenchmark.cpp`, the `config.ssdPath` / `config.filecacheRoot`
assignments), mirroring `FileCacheSeekBenchmark`'s `setupFixture`. (The
synthetic source blob is already an absolute `/tmp` path; there is no
`--data_dir` flag in this benchmark.)

Post-fix evidence — relative root now exits 0:

```text
$ velox_ch_filecache_wrapper_benchmark --wrappers=all \
    --workloads=sequential,zipfian --target_ws_gb=1 --measure_passes=1 \
    --filecache_root=cmake-build-debug-gcc13/perf_fc \
    --ssd_path=cmake-build-debug-gcc13/perf_ssd
relative-root exit: 0
log: .../cmake-build-debug-gcc13/run_015_perffix_relroot.log
```

### F-PERF-2 (报告误导): cbi(RAM 命中) vs fcbi(磁盘命中) 非同层, 不能直接比

Fix: added a `hit_layer` column (derived from the per-tier byte counters:
`RAM` / `SSD` / `disk` / `page-cache` / `source(!)`) and an explicit footnote
after every table warning that a cbi `RAM` row and an fcbi `disk` row are NOT a
like-for-like comparison, so the `delta vs fcbi` between them is a RAM-vs-disk
artifact — it does not mean fcbi is slower same-for-same. The footnote also
tells the reader how to get a fair disk-vs-disk row (`--ram_cache_gb` below
`--target_ws_gb` so cbi's hit_layer becomes `SSD`).

Layer-annotated three-engine table (absolute root, `--target_ws_gb=1
--measure_passes=1`; log `.../run_015_perffix_absroot.log`):

| pattern | read | wrapper | hit_layer | wall_ms | MB/s | ram_MB | ssd_MB | src_MB | delta vs fcbi |
|---|---|---|---|---:|---:|---:|---:|---:|---:|
| seq  | 1024K | fcbi | disk       | 372.5 | 2749   | 0    | 0 | 0    | — |
| seq  | 1024K | cbi  | RAM        | 1.7   | 595257 | 1024 | 0 | 0    | -99.5% |
| seq  | 1024K | dbi  | page-cache | 96.3  | 10629  | 0    | 0 | 1024 | -74.1% |
| zipf | 1024K | fcbi | disk       | 293.4 | 3490   | 0    | 0 | 0    | — |
| zipf | 1024K | cbi  | RAM        | 1.9   | 544923 | 1024 | 0 | 0    | -99.4% |
| zipf | 1024K | dbi  | page-cache | 87.4  | 11716  | 0    | 0 | 734  | -70.2% |

The cbi `RAM` hit_layer makes the -99% delta explicit as a RAM-vs-disk artifact,
NOT a "fcbi is 180x slower" conclusion.

Fair local-cache comparison row (cbi RAM tier forced below the working set with
`--wrappers=both --ram_cache_gb=0.125 --ssd_cache_gb=4`; log
`.../run_015_perffix_fair.log`, exit 0):

| pattern | read | wrapper | hit_layer | wall_ms | MB/s | ram_MB | ssd_MB | src_MB | delta vs fcbi |
|---|---|---|---|---:|---:|---:|---:|---:|---:|
| seq | 1024K | fcbi | disk      | 301.5 | 3396 | 0 | 0   | 0   | — |
| seq | 1024K | cbi  | source(!) | 6695.2 | 153 | 0 | 590 | 546 | +2120.6% |

Honest read: with a 128 MiB RAM tier and single-shot warm, cbi cannot hold the
1 GiB target SSD-resident, so it re-downloads (`src_MB=546`) — the `source(!)`
hit_layer flags exactly that the number is NOT a clean SSD-hit and must not be
read as a same-layer result. (A clean disk-vs-disk cbi row needs a chunked-warm
RAM->SSD flush protocol, which this MVP microbench does not implement; the
annotation prevents the misread rather than fabricating a fair number.)

### Re-run gates (both roots + concurrency)

| Command | Exit code | Log |
|---|---:|---|
| Rebuild `velox_ch_filecache_wrapper_benchmark` | 0 | `.../build_015_perffix_wrapper.log` |
| `--wrappers=all` RELATIVE root | 0 | `.../run_015_perffix_relroot.log` |
| `--wrappers=all` ABSOLUTE root | 0 | `.../run_015_perffix_absroot.log` |
| `--wrappers=both` fair disk-vs-disk row | 0 | `.../run_015_perffix_fair.log` |
| `--wrappers=fcbi --num_threads=4` concurrency | 0 | `.../run_015_perffix_concurrent.log` |

### Production-diff-empty confirmation (unchanged)

```text
$ git status --porcelain | awk '{print $2}' | grep -v "^velox/ch/benchmarks/"
NONE (production diff empty)
$ git diff --check
clean
```

Only `velox/ch/benchmarks/FileCacheWrapperBenchmark.cpp` changed in this fix
round; the accepted E2E tests, the seek benchmark, the harness, and all
production sources are untouched. The four pre-existing gates (e2e 17 /
buffered_input 19 / manager 19 / core_scc 47) are unaffected by this change
(no shared file touched) and remain green from the prior attempt.

### Worker declaration (perf-fix)

```text
F-PERF-1 and F-PERF-2 were addressed within velox/ch/benchmarks/ only.
Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Controller review 2 — Perf 扩展 (accepted)

```text
controller_status: accepted
environment_profile: home-chang
task: 015-perf
```

## Review evidence

```text
F-PERF-1 fix — INDEPENDENTLY VERIFIED: with a RELATIVE --filecache_root (which
  core-dumped before, exit 134) the wrapper benchmark now runs exit 0. Fix is
  std::filesystem::absolute() normalization of --filecache_root/--ssd_path at
  startup, mirroring the seek benchmark.
F-PERF-2 fix — VERIFIED: output now has a `hit_layer` column (fcbi=disk / cbi=RAM /
  dbi=page-cache) and a footnote stating the engines serve from different tiers, that
  a cbi-RAM vs fcbi-disk `delta` is a RAM-vs-disk artifact (not a same-tier slowdown),
  and how to force a fair disk-vs-disk row (--ram_cache_gb below the working set).
  Honest labeling, not a fabricated fair number.
Controller-run numbers (relative root, exit 0):
  seq 1024K fcbi=disk 3403 MB/s | cbi=RAM 577167 MB/s (ram_MB=1024) | dbi=page-cache 10302 MB/s
Concurrency: --wrappers=fcbi --num_threads=4 exit 0 (no crash/deadlock).
scope: only velox/ch/benchmarks/ (this round only FileCacheWrapperBenchmark.cpp);
  production diff empty; git diff --check clean.
carried-forward PASS (review 1): fcbi via FileCacheManager (no bare new FileCache);
  seek-benchmark hit "no source read" assertion load-bearing (worker RED real);
  pre-existing gates e2e 17 / buffered_input 19 / manager 19 / core_scc 47 no regression.
unresolved findings: none.
```

## Required changes

```text
None. Task 015 perf extension accepted.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | (this acceptance — see Velox `Task 015:` perf commit) |
| `/home/chang/SourceCode/ClickHouse` | receipt = this commit |

## Perf 扩展 2 (TPCH AbBenchmark)

### Worker attempt 1

```text
worker_status: blocked
environment_profile: home-chang
task: 015 (TPCH AbBenchmark extension)
```

### Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `8958b2042` | clean (no changes) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `4d63850e0e0` | clean before receipt append |

Reference read read-only via `git show ch-filecache:<path>` (branch not checked out).

### Blocker: `--input_source=filecache` requires an unreviewed production wiring change

The A/B benchmark itself ports cleanly, but the **`filecache` engine cannot route
any read** in our `filecache2` tree without modifying a production file, and the
required modification is an unreviewed design decision. Per EXECUTION_PROTOCOL
"Unreviewed dependency gate" and this task's hard rule ("Modify ONLY
`velox/ch/benchmarks/**`; do NOT touch production code"), I stopped instead of
inventing a mapping or expanding scope.

Root cause — how the reference makes `--input_source=filecache` work:

1. `AbBenchmarkMain.cpp` (reference) only **installs a process-wide FileCache
   singleton** (`ch::FileCache::setInstance`) and sets `--cache_gb=0`. It does
   **not** itself construct any `FileCacheBufferedInput`.
2. The actual read-path routing lives in a **production file** on `ch-filecache`:
   `velox/connectors/hive/HiveConnectorUtil.cpp` `createBufferedInput` was
   modified to add a first branch:
   ```cpp
   if (auto* fileCache = ch::FileCache::getInstance()) {
     VELOX_CHECK_NULL(connectorQueryCtx->cache(), ...);
     return std::make_unique<ch::FileCacheBufferedInput>(
         fileHandle.file, readerOpts.memoryPool(), fileCache,
         ch::FileCacheKey::fromPath(fileHandle.file->getName()),
         ch::FileCache::getCommonOrigin(),
         ch::CreateFileSegmentSettings{}, std::move(ioStatistics));
   }
   ```
3. Our `filecache2` `velox/connectors/hive/HiveConnectorUtil.cpp`
   `createBufferedInput` has **zero** FileCache references
   (`grep -c FileCache … = 0`). It knows only `CachedBufferedInput` (cbi),
   plain `BufferedInput`, and `DirectBufferedInput`. TPCH TableScan reads
   therefore never reach our `velox/ch/Disks/IO/FileCacheBufferedInput`.

So `--input_source=filecache` in a ported benchmark would install a FileCache
that **nothing ever reads through** — the benchmark would silently run direct
reads and mislabel them "filecache" (a false-green result the protocol forbids).
Making it real requires editing production `HiveConnectorUtil.cpp`, which is out
of the declared `velox/ch/benchmarks/**` scope.

### Why this is an unreviewed-dependency STOP, not a "minimal integration fix"

The reference wiring assumes a **bare global singleton** FileCache
(`ch::FileCache::getInstance()` + `ch::FileCache::getCommonOrigin()`). Our
FileCache is **Manager-owned** and our contract (adaptation point b) explicitly
**forbids bare `ch::FileCache::getInstance`**. Our `FileCacheManager` exposes
`getInstance()/instance()/getDefault()` (returns `FileCachePtr`), and our
`FileCacheBufferedInput` ctor signature differs (it takes
`FileCacheOriginInfo origin`, `FileCacheReadOptions`, `FileCacheRequestContext`,
and a second `velox::IoStats`, not the reference's positional
`getCommonOrigin()`/`CreateFileSegmentSettings{}` form).

Porting the connector branch therefore requires deciding, with no approved
mapping:

- How a Manager-owned FileCache is exposed to `HiveConnectorUtil` at read time
  (via `FileCacheManager::getInstance()->getDefault()`? a new accessor?), since
  the connector must not construct or bare-`getInstance` a `FileCache`.
- What `FileCacheOriginInfo origin` the connector passes (the reference's
  `ch::FileCache::getCommonOrigin()` is a bare static that our Manager-owned
  design may not expose identically).
- How to populate our ctor's `FileCacheReadOptions` / `FileCacheRequestContext`
  / second `IoStats` from the connector's `readerOpts`/`ioStats`.
- The mutual-exclusion invariant with `connectorQueryCtx->cache()`
  (AsyncDataCache) in our tree.

These are production-integration design decisions (a Task-016/017-class read-path
seam), not a localized benchmark bug. The protocol requires stopping and asking
for user review rather than choosing a "closest" API or adding the branch.

### What CAN be ported cleanly today (not done, pending the decision)

- `direct` engine: needs no FileCache; ports near-verbatim.
- `cbi` engine: native `AsyncDataCache` via `--cache_gb>0`; ports near-verbatim.
- The `AbBenchmarkBase` sweep/CSV harness and a TPCH `numQueries`/`buildPlan`
  subclass (via `TpchQueryBuilder`): port cleanly.
- Only the `filecache` third engine is blocked, and it is the entire point of
  the extension, so a partial two-engine deliverable would not satisfy the
  three-engine acceptance gate.

### Decision needed from the user

Approve (and record in the design + Task-015 amendment) the production wiring for
routing Hive-connector reads to our Manager-owned `FileCacheBufferedInput`:
which accessor exposes the Manager's `FileCachePtr` to `createBufferedInput`,
what `origin` / `FileCacheReadOptions` / `FileCacheRequestContext` / `IoStats`
the connector supplies, and the AsyncDataCache mutual-exclusion rule. Once
approved, redispatch this extension; the benchmark port itself is
straightforward on top of that seam.

### Acceptance evidence (as far as reached)

```text
production diff: EMPTY (git status --short clean; no files modified)
HiveConnectorUtil.cpp FileCache refs (our tree): 0
build/smoke gates: not run — blocked before any file was created
split=1 requirement: understood (--num_splits_per_file gflag, QueryBenchmarkBase.cpp:58 default 10);
  would be passed on every TPCH command once unblocked
```

### Blockers

```text
Routing `--input_source=filecache` reads to our FileCacheBufferedInput requires
modifying production velox/connectors/hive/HiveConnectorUtil.cpp
(createBufferedInput), which is (1) outside the declared velox/ch/benchmarks/**
scope and (2) an unreviewed design decision (Manager-owned FileCache exposure +
origin/options/context mapping + AsyncDataCache mutual exclusion). Stopped per
the Unreviewed-dependency gate. No production or benchmark file was changed.
```

### Worker declaration

```text
Only the Task 015 TPCH-extension was attempted.
No files were changed; the tree is clean.
The worker stopped after writing this receipt.
```

## Controller unblock response — Perf 扩展 2 (TPCH) : waiting_for_user / parked

```text
controller_status: waiting_for_user
task: 015-tpch
```

## Resolution

```text
root cause (worker 正确 blocked, Controller 核实属实):
  要让 TPCH 查询真的走我们的 FileCache, 必须让 Velox Hive 连接器的
  createBufferedInput (velox/connectors/hive/HiveConnectorUtil.cpp, 所有 TPCH 读的
  必经入口) 选择 FileCacheBufferedInput。我们 filecache2 的这个生产函数里 FileCache
  字样为 0 (grep 确认) —— TPCH 读到不了我们的 FileCache。
  ch-filecache 那条线是靠直改此生产函数 + 塞裸单例 `if(ch::FileCache::getInstance())`
  接的 (HiveConnectorUtil.cpp:666)；那正是我们 Manager-owned 设计明确禁止的裸单例,
  且我们的 FileCacheBufferedInput ctor 签名不同。硬接需要一整套未 review 的读路径
  集成决策 (Manager 如何把 FileCachePtr 暴露给连接器、origin/options/context/IoStats、
  与 AsyncDataCache 的互斥)。不接就编译 benchmark 会静默跑成 direct 读却标称 filecache
  (假绿)。故 worker 未硬来、未造裸单例、未假绿, 正确 blocked。生产零改动, 树干净。

task 归属核查 (Controller):
  连接器集成在规划里属 Task 018 (Gluten Host Integration): 经
  GlutenBufferedInputBuilder::create 选择 FileCache, 且经 FileCacheManager —— 不像
  ch-filecache 那样直改 Velox 主干连接器塞裸单例。这更对齐"不污染 velox 主干 + 经
  Manager"的原则。也就是说: 纯 Velox (无 Gluten) 环境下让 TPCH 读走 fcbi, 当前架构
  没有入口, 入口设计在 Task 018 的 Gluten Builder 里。

decision (user 待定):
  TPCH 三引擎端到端对比依赖连接器集成 (Task 018 / 或新立纯-Velox 集成 task)。用户
  当前优先做 Task 017 (可观测性: 点亮 ProfileEvents/CurrentMetrics 以便性能测试能收集
  真实缓存指标 + 真日志 + 查询取消 + F-CALLERID + SD8)。TPCH 扩展 PARKED, 待连接器
  集成路径确定 (018 或独立 task) 后再解冻。已移植的 AbBenchmark cbi/direct 两引擎骨架
  可留待那时接上 filecache 引擎。

redispatch same task: no (parked; 转 Task 017)
```
