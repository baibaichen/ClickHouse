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
