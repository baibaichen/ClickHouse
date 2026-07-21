# Task 015 Result: `FileCache` Velox E2E Validation and Basic Benchmark

## Status

status: success

## Repository state

```text
Velox branch: filecache
HEAD: ad1a13c37e87cecda464ac8dfcc9fee57c093eb6
Environment profile: root-oss
git status --short:
M velox/ch/CMakeLists.txt
 M velox/ch/Disks/IO/tests/CMakeLists.txt
 M velox/ch/IO/ReadBufferFromVeloxReadFile.cpp
 M velox/ch/IO/ReadBufferFromVeloxReadFile.h
 M velox/ch/Interpreters/FileCache/Metadata.cpp
?? velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
?? velox/ch/Disks/IO/tests/FileCacheTestHelpers.h
?? velox/ch/benchmarks/
```

## Files changed

```text
velox/ch/CMakeLists.txt                              (modified: added benchmarks subdirectory)
velox/ch/Disks/IO/tests/CMakeLists.txt               (modified: added E2E test target)
velox/ch/IO/ReadBufferFromVeloxReadFile.cpp           (modified: B1 foreground direct-IO tail round-up + overflow guard)
velox/ch/IO/ReadBufferFromVeloxReadFile.h             (modified: B1 directIoAlignment() accessor)
velox/ch/Interpreters/FileCache/Metadata.cpp          (modified: B1 aligned background buffer + tail skip + overflow guards)
velox/ch/Disks/IO/tests/FileCacheTestHelpers.h        (new: shared test helper header)
velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp          (new: E2E test, 20 cases)
velox/ch/benchmarks/CMakeLists.txt                    (new: benchmark CMake)
velox/ch/benchmarks/FileCacheSeekBenchmark.cpp        (new: random-seek benchmark)
```

## Commands run

```text
source /root/oss/velox-helper/env.sh
ninja -C _build/debug velox_ch_filecache_e2e_test velox_ch_filecache_seek_benchmark
ninja -C _build/debug-task012-nonmono velox_ch_filecache_e2e_test
ctest --test-dir _build/debug -R '^velox_ch_' --output-on-failure
_build/debug/velox/ch/Disks/IO/tests/velox_ch_filecache_e2e_test
_build/debug-task012-nonmono/velox/ch/Disks/IO/tests/velox_ch_filecache_e2e_test
_build/debug/velox/ch/benchmarks/velox_ch_filecache_seek_benchmark --bm_min_iters=3 --file_size_mb=8 --cache_size_mb=32 --cache_dir=_build/debug/task015/tmp/fc_bench_v2
```

## Generated logs

```text
_build/debug/task015/build_015_final_review.log              (mono build — green, original)
_build/debug/task015/build_015_identity_fix.log              (mono build after identity-test fix — green)
_build/debug/task015/test_015_final_all_green.log            (20 E2E tests — green, original)
_build/debug/task015/test_015_final_all_green_v2.log         (20 E2E tests after identity-test fix — green)
_build/debug/task015/test_015_identity_fix_green.log         (PathOnlyKeyWhenEtagEmpty + DifferentEtagsDifferentKeys focused green)
_build/debug/task015/test_015_accumulated_v2.log             (15 CTest targets — green)
_build/debug-task012-nonmono/task015/build_015_final_v2.log  (non-mono build — green)
_build/debug-task012-nonmono/task015/test_015_final_v2.log   (non-mono 20 E2E — green)
_build/debug/task015/bench_015_final_v2.log                  (benchmark smoke+timing)
_build/debug/task015/mutations/                              (mutation RED/green logs)
```

## Test results

```text
velox_ch_filecache_e2e_test (mono):     20 tests, 0 failed
velox_ch_filecache_e2e_test (non-mono): 20 tests, 0 failed
accumulated velox_ch_* regression:      15 CTest targets, 0 failed
velox_ch_filecache_seek_benchmark:      smoke-pass assertions passed;
                                        3 iterations, no crash,
                                        timing rows for cache-hit / miss / bypass
```

## Benchmark output

```text
============================================================================
FileCacheSeekBenchmark.cpp               relative  time/iter   iters/s
============================================================================
FileCacheSeekCacheHit                                 361.49ms      2.77
FileCacheSeekCacheMiss                                   1.02s   981.72m
FileCacheSeekBypass                                    32.37ms     30.90
```

### Benchmark cross-run key safety (review item A fix)

The benchmark now uses `FileCacheKey::random()` for every key that must be fresh
(the normal hit-path key, every miss-path key, and every bypass-path key).
This guarantees keys are collision-free across process runs even when the cache
directory retains persisted metadata from a prior run — the 128-bit random space
makes accidental collision astronomically unlikely without destructively clearing
the user-supplied `--cache_dir`.  Fixed-seed seek offsets are unchanged (the PRNG
seed 12345 generates deterministic positions for repeatable timing).

## Migration matrix

| CH test/scenario | Velox test name | Exclusion |
|---|---|---|
| cold miss -> cache fill -> later hit | MissFillHit | — |
| partial segment continuation across readers | PartialSegmentContinuationAcrossReaders | — |
| cache write failure -> configured bypass or propagated failure | CacheWriteFailureConfiguredBypassOrPropagate | — |
| truncated/invalid cached data -> source recovery | TruncatedOrInvalidCachedDataSourceRecovery | CH rename-race retry not ported (documented gap) |
| reserve-ahead/downloaded-size accounting | ReserveAheadDownloadedSizeAccounting | — |
| random seeks across hit/miss/bypass paths | RandomSeeksAcrossHitMissBypassPaths | — |
| direct-IO source + background download | DirectIoSourceBackgroundDownloadCompletes | — |
| direct-IO unaligned tail skip | DirectIoUnalignedTailSkipsBackgroundDownload | — |
| cache-only miss fails | CacheOnlyMissFails | — |
| bypass mode (readIfExistsOtherwiseBypass) | ReadIfExistsBypassMode | — |
| BackUp within output buffer | BackUpWithinOutputBuffer | — |
| Skip across segment boundary | SkipAcrossSegmentBoundary | — |
| seekToPosition region-relative | SeekToPositionRegionRelative | — |
| non-zero region.offset absolute coords | NonzeroRegionOffsetAbsoluteCoordinates | — |
| discarded enqueue no use-after-free | DiscardedEnqueueNoUseAfterFree | — |
| load is no-op planning barrier | LoadIsNopPlanningBarrier | — |
| DWRF shouldPrefetchStripes=false | DWRFShouldPrefetchStripesIsFalse | — |
| path-only key when etag empty | PathOnlyKeyWhenEtagEmpty | — |
| different etags -> different keys | DifferentEtagsDifferentKeys | — |
| shutdown while stream alive not reading | ShutdownWhileStreamAliveNotReading | — |

## RED and false-green evidence (mutation matrix)

| # | Test | Mutation | Log path | Failing assertion/reason | Restored green |
|---|---|---|---|---|---|
| 1 | DirectIoSourceBackgroundDownloadCompletes | `if (false && alignment > 1)` in Metadata.cpp — disable aligned buffer path | mutations/mut1_b1_bg_download.log | "External buffer address violates the direct-IO alignment" | mutations/test_final_green.log |
| 2 | DirectIoUnalignedTailSkipsBackgroundDownload | Comment out TestValue::adjust in Metadata.cpp tail-skip | mutations/mut2c_no_notify.log | spinUntil times out — "background worker must invoke the unalignedTailSkip notification" | mutations/test_final_green.log |
| 3 | DirectIoUnalignedTailSkipsBackgroundDownload | Revert foreground round-up in ReadBufferFromVeloxReadFile.cpp | mutations/mut3b_check_noup.log | "Direct-IO read length violates the required alignment" | mutations/test_final_green.log |
| 4 | MissFillHit | `DOWNLOADED → REMOTE_FS_READ_BYPASS_CACHE` in chooseReadInfo normal path | mutations/red_mut_hitbypass.log | `source2->preadCalls()` Expected: 0, Actual: >0 (line 147) | test_015_final_all_green.log |
| 5 | PathOnlyKeyWhenEtagEmpty | Empty-etag branch in `FileCacheFileIdentity::deriveKey` returns `fromPath(path + "_wrong")` | mutations/red_mut_identityA.log | `EXPECT_EQ(key, FileCacheKey::fromPath(path))` — 16-byte key mismatch (line 339); cache miss on re-derive (source2 pread > 0) | test_015_identity_fix_green.log |
| 6 | DifferentEtagsDifferentKeys | Non-empty-etag branch ignores etag — returns `fromPath(path)` for both versions | mutations/red_mut_identityB.log | `EXPECT_NE(key1, key2)` fails — both keys equal (line 370); stale key1 content served to key2 consumer (line 380, 390) | test_015_identity_fix_green.log |
| 7 | PartialSegmentContinuationAcrossReaders | Same as #4 | mutations/red_mut_hitbypass.log | `source2->preadBytes() < data.size()` Expected: true, Actual: 32768 vs 32768 (line 447) | test_015_final_all_green.log |
| 8 | RandomSeeksAcrossHitMissBypassPaths | Same as #4 | mutations/red_mut_hitbypass.log | `sawHit` Expected: true (line 673) — no cache hits observed | test_015_final_all_green.log |
| 9 | CacheOnlyMissFails | Disable both `opts.tempCacheOnly` branches in `FileCacheInputStream.cpp` (`if (false && ...)`) — uncached request follows normal source-read path | mutations/red_mut_cacheonly_clean.log | `EXPECT_THROW(stream->Next(&buf, &size), VeloxException)` — `Actual: it throws nothing` (line 164); clean behavioral failure, no SIGSEGV | mutations/green_cacheonly_clean.log |
| 10 | ReadIfExistsBypassMode | Disable `readIfExistsOtherwiseBypass` decision (`if (false && ...)` in chooseReadInfo) + bypass getOrSet in nextFileSegmentsBatch | mutations/red_mut_bypass_full.log | `cache_->getFileSegmentInfos(commonUser()).empty()` Expected: true (line 183) — segments persist | test_015_final_all_green.log |
| 11 | BackUpWithinOutputBuffer | Don't decrement `position_` in `BackUp` | mutations/red_mut_backup.log | `stream->ByteCount()` Expected: 64*1024-1024, Actual: 65536 (line 206) | test_015_final_all_green.log |
| 12 | SkipAcrossSegmentBoundary | Don't increment `position_` in `SkipInt64` | mutations/red_mut_skip.log | Exception/timeout — readNextChunk reads at wrong absolute offset | test_015_final_all_green.log |
| 13 | SeekToPositionRegionRelative | Don't assign `position_ = newPosition` in seekToPosition slow path | mutations/red_mut_seek.log | `stream->ByteCount()` Expected: 256, Actual: 0; data mismatch (lines 259, 264) | test_015_final_all_green.log |
| 14 | NonzeroRegionOffsetAbsoluteCoordinates | `absolutePosition()` returns `position_` without `region_.offset` | mutations/red_mut_region.log | Data mismatch — reads from offset 0 instead of 65536 (line 280) | test_015_final_all_green.log |
| 15 | DiscardedEnqueueNoUseAfterFree | `load()` throws unconditionally instead of being a no-op | mutations/red_mut_load.log | EXPECT_NO_THROW fails — "load() must not be called…" thrown (line 296) | test_015_final_all_green.log |
| 16 | LoadIsNopPlanningBarrier | Same as #15 | mutations/red_mut_load.log | EXPECT_NO_THROW fails (line 312) | test_015_final_all_green.log |
| 17 | DWRFShouldPrefetchStripesIsFalse | `shouldPrefetchStripes()` returns `true` | mutations/red_mut_prefetch.log | EXPECT_FALSE fails (line 324) | test_015_final_all_green.log |
| 18 | ShutdownWhileStreamAliveNotReading | `shutdown()` VELOX_FAILs before teardown | mutations/red_mut_shutdown.log | EXPECT_NO_THROW fails — "shutdown rejected: active caches still exist" (line 406) | test_015_final_all_green.log |
| 19 | CacheWriteFailureConfiguredBypassOrPropagate | Remove `skipCacheOnDiskFailure()` bypass — always rethrow disk errors | mutations/red_mut_diskfail.log | Case 2: unexpected exception thrown — bypass path disabled, error propagates (unhandled exception) | test_015_final_all_green.log |
| 20 | TruncatedOrInvalidCachedDataSourceRecovery | `removeKeyIfExists` made no-op (stale data remains) | mutations/red_mut_invalidate.log | `source3->preadCalls() > 0` Expected: true, Actual: 0 vs 0 (line 544) — reads stale cache | test_015_final_all_green.log |
| 21 | ReserveAheadDownloadedSizeAccounting | `getUsedCacheSize()` always returns 0 | mutations/red_mut_accounting.log | `cache_->getUsedCacheSize()` Expected: 8192, Actual: 0 (line 561) | test_015_final_all_green.log |

## Code hardenings applied (review gate items)

```text
A. Benchmark cross-run key collision fix:
   - All cache keys (hit, miss, bypass) now use FileCacheKey::random() instead of
     deterministic fromPath() strings with a counter. This guarantees freshness
     even when the cache directory retains metadata from prior runs, without
     destructively clearing the user-supplied cache_dir via remove_all.
   - Fixed-seed seek offsets (PRNG seed 12345) remain unchanged for repeatability.

B. CacheWriteFailureConfiguredBypassOrPropagate:
   - Added EXPECT_THROW(cache_->getFileSegmentInfos(key, commonUser()), VeloxException)
     which proves no metadata residue for the bypass key (the key doesn't exist at
     all in cache metadata after bypass, hence getFileSegmentInfos throws).

C. DirectIoUnalignedTailSkipsBackgroundDownload:
   - Removed unused `FileSegment * skippedSegment` raw pointer from the ScopedTestValue
     callback closure (was captured but never read).
   - Added EXPECT_GT(source2->preadCalls(), 0u) assertion proving the fresh
     direct-IO source actually performed remote I/O for the skipped tail,
     preventing a full-cache-hit false green.

D. Overflow guards:
   - ReadBufferFromVeloxReadFile.cpp: VELOX_CHECK_LE before (logicalToRead + alignment - 1)
     ensures no size_t wrap.
   - Metadata.cpp: VELOX_CHECK_LE before (scratch_capacity + alignment) ensures no
     size_t wrap; VELOX_CHECK_LE before pointer-alignment round-up ensures no
     uintptr_t wrap.
   - All guards use VELOX_CHECK with no fallback (fail-close).

E. Identity-test production source of truth (Task 015 clean-up gate):
   - FileCacheE2ETest.cpp now includes FileCacheFileIdentity.h and uses
     FileCacheFileIdentity::deriveKey as the single key-derivation entry point.
   - PathOnlyKeyWhenEtagEmpty: derives key via deriveKey({path, ""}), asserts
     equality with FileCacheKey::fromPath(path), fills, re-derives, and confirms
     preadCalls == 0. Mutation A (wrong path suffix) produces a clean FAILED at
     line 339 (mutations/red_mut_identityA.log).
   - DifferentEtagsDifferentKeys: derives both keys via deriveKey({path, "v1"})
     and deriveKey({path, "v2"}), preserving all isolated content/hit assertions.
     Mutation B (etag ignored, path-only for both) collapses key1 == key2 and
     yields a clean FAILED at line 370 + content mismatch at lines 380/390
     (mutations/red_mut_identityB.log).
   - The direct SipHash128 include and manual sipHash128() call were removed from
     the test; FileCacheFileIdentity.h remains unchanged after mutations.

F. CacheOnlyMissFails clean mutation (replaces SIGSEGV evidence):
   - Disabling both opts.tempCacheOnly branches (nextFileSegmentsBatch line 256
     and chooseReadInfo line 387) with `if (false && ...)` causes an uncached
     cache-only request to fall through to the normal source-read path.
   - EXPECT_THROW observes no exception and reports: "Actual: it throws nothing"
     (line 164) — clean behavioral failure, no SIGSEGV/abort.
   - Log: mutations/red_mut_cacheonly_clean.log. Restore log:
     mutations/green_cacheonly_clean.log.
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 016 (optional post-MVP): WriteBufferToFileSegment for Ephemeral segments.
Task 017 (optional post-MVP): Observability and cancellation hardening.
Task 018 (future): Gluten host integration.
Task 019 (future): Gluten builder and lifecycle E2E validation.
```

## Controller acceptance

```text
controller_status: accepted
environment_profile: root-oss
```

The first independent review found one Critical production gap and six
Important test/benchmark gaps. The fix waves corrected the background loop's
unaligned-tail read, preserved bounded scratch allocation, added foreground
aligned physical reads with logical clamping, strengthened the affected E2E
cases, and made the benchmark measure real checked seek paths. A second review
identified the cross-run miss-key collision and incomplete mutation evidence;
both were corrected. The final strict review reported:

```text
Critical findings: 0
Important findings: 0
spec compliance: approved
technical quality: approved
final Task 015 verdict: approved
```

Controller evidence:

```text
E2E mono:                 20/20
E2E non-mono:             20/20
accumulated mono CTest:   15/15
mutation RED rows:        21, covering 20/20 tests
benchmark smoke:          passed
benchmark timing rows:    hit / miss / bypass present
git diff --check:         clean
skipped/disabled tests:   0/0
```

Accepted Velox commit:

```text
aadc10db7bffbbc49ee9d7dcee1e01e78bbadfff
Task 015: Complete FileCache E2E validation
```

Task 015 is accepted. Tasks 016-019 remain unimplemented pending the requested
post-Task-015 task-contract review.

## Post-acceptance correction — direct-IO adapter tests

The accepted production behavior rounds an unaligned logical tail/right bound
up to an aligned physical direct-IO request and exposes only logical bytes.
Two pre-existing adapter tests still expected the old fail-before-`pread`
behavior. The original accumulated CTest run did not rebuild
`velox_ch_io_test`, so its stale executable produced a false-green gate.

Fresh rebuild before correction:

```text
IoAdaptersTest.ReaderDirectIoUnalignedTailRejectedBeforePread: failed
IoAdaptersTest.ReaderDirectIoUnalignedRightBoundRejectedBeforePread: failed
reason: both observed the new successful aligned physical read
```

Corrected tests:

```text
ReaderDirectIoUnalignedTailUsesAlignedPhysicalRead
  logical tail: 100 bytes
  physical pread: 512 bytes
  exposed bytes: 100

ReaderDirectIoUnalignedRightBoundClampsPhysicalRead
  logical bound: 600 bytes
  physical pread: 1024 bytes
  exposed bytes: 600
  reads after logical bound: 0
```

RED mutation:

```text
mutation: physicalToRead = logicalToRead
build: succeeded
tail test: failed on 100 % 512 direct-IO length validation
right-bound test: failed on 600 % 512 direct-IO length validation
logs:
  _build/debug/task015_followup/build_io_red.log
  _build/debug/task015_followup/test_io_red.log
```

Restored final gate:

```text
velox_ch_io_test mono:      33/33
velox_ch_io_test non-mono:  33/33
all 15 registered velox_ch_* targets built before CTest
accumulated mono CTest:     15/15
accumulated non-mono CTest: 15/15
git diff --check:           clean
review:                     approved, no Critical/Important findings
```

Accepted Velox follow-up commit:

```text
43a9e6f75ffb94be38836b45fd476325665f50be
Task 015: Sync direct-IO adapter tests
```
