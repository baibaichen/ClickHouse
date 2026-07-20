# Task 014 Result: `FileCacheBufferedInput` and `FileCacheInputStream`

## Worker attempt 1

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 014
```

Implemented the Velox scan/DWIO read path for the ClickHouse `FileCache`:
`FileCacheRequestContext`, `FileCacheFileIdentity`, `FileCacheBufferedInput`
(a `dwio::common::BufferedInput` subclass), and `FileCacheInputStream` (a
`dwio::common::SeekableInputStream` streaming state machine ported from CH
`CachedOnDiskReadBufferFromFile`). The focused gate
`velox_ch_filecache_buffered_input_test` builds (exit 0) and passes 16/16
(0 failed / 0 skipped), executing the mandatory behavioral matrix plus migrated
CH reader cases. The two shared-file-touching gates
(`velox_ch_filecache_manager_test`, `velox_ch_filecache_core_scc_test`) still
build and pass after the CMake edit.

## Status

status: success

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `5e3ee1ac9` | clean (0 files); `filecache2...baibaichen/filecache [ahead 7]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | receipt append only |
| `/home/chang/SourceCode/gluten1` | (unchanged) | (unchanged) | pre-existing untracked `tools/gluten-it/spark-home/` only |

Branch is `filecache2` / `ch-filecache` (task text says `filecache`); recorded
as observed per the dispatch note — did not stop over the mismatch. `HEAD
5e3ee1ac9` (velox) = "Task 013: FileCacheFactory + FileCacheManager +
OpenedFileCache" (accepted). No pre-existing dirty files in velox. No
staging/commit/amend/rebase/push.

## Velox status

```text
Branch: filecache2  HEAD: 5e3ee1ac9
git status --short:
 M velox/ch/CMakeLists.txt
?? velox/ch/Disks/
git diff --check: clean (exit 0)
```

## Gluten status

```text
?? tools/gluten-it/spark-home/     (pre-existing untracked; NOT touched by this task)
```

No Gluten file was created or modified.

## Files changed

```text
MODIFIED:
  velox/ch/CMakeLists.txt                                  (+ add_subdirectory(Disks))
NEW:
  velox/ch/Disks/CMakeLists.txt
  velox/ch/Disks/IO/CMakeLists.txt
  velox/ch/Disks/IO/FileCacheRequestContext.h
  velox/ch/Disks/IO/FileCacheFileIdentity.h               (deriveKey: path-only vs SipHash128(path+etag))
  velox/ch/Disks/IO/FileCacheBufferedInput.h
  velox/ch/Disks/IO/FileCacheBufferedInput.cpp
  velox/ch/Disks/IO/FileCacheInputStream.h
  velox/ch/Disks/IO/FileCacheInputStream.cpp              (ported CH CachedOnDiskReadBufferFromFile state machine)
  velox/ch/Disks/IO/tests/CMakeLists.txt
  velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp  (16 executable tests)
  (this receipt)
```

## Library-name adaptation (mono build)

The task's illustrative CMake links a `velox_ch_filecache_dwio` /
`velox_ch_filecache_manager` / `velox_ch_filecache_core` library set. Under the
mono build (`VELOX_MONO_LIBRARY=ON`) those are NOT separate libraries: a second
`velox_add_library` would double-define the center-SCC / Factory / Manager
symbols and break the link (the same ODR reason Tasks 012/013 compiled their
sources INTO `velox_ch_filecache`). Following the established pattern
(`Interpreters/FileCache/CMakeLists.txt`, `IO/CMakeLists.txt`), the two Disks/IO
`.cpp` sources are registered into `velox_ch_filecache` via `velox_sources()`,
and the test links `velox_ch_filecache` + `velox_dwio_common` once. This is the
reviewed mono-build adaptation from the dispatch note, not an unreviewed change.

## Design decisions / substitutions used (all reviewed primitives)

```text
- Local cache-segment file open in getCacheReadBuffer: opened via
  filesystems::getFileSystem(path)->openFileForRead(path) — the SAME primitive
  the D1 OpenedFileCache uses (FileCache exposes no local-fs/opened-file-cache
  accessor). Local scheme is registered by the Manager/tests.
- ReadInfo readers typed as FileSegment::RemoteFileReaderPtr
  (== shared_ptr<ReadBufferFromFileBase>) so they interoperate with
  getRemoteFileReader / setRemoteFileReader for the Q1/Q2 handoff (the illustrative
  header named the concrete ReadBufferFromVeloxReadFile; the base is required for
  the handoff contract and is a faithful mapping, not a weakening).
- Region-relative CACHED reader coordinates: the local cache file holds the
  segment's bytes in a 0-based space, so CACHED uses
  setReadUntilPosition(getDownloadedSize()) + seek(offset - range.left); remote
  readers use ABSOLUTE source offsets (min(range.right+1, fileSize)).
- Output buffer size honors cacheOptions().remoteFsBufferSize when set, else 1 MiB.
```

## Commands run

```text
configure (home-chang recipe + -DVELOX_BUILD_TESTING=ON)
ninja -C <build> velox_ch_filecache_buffered_input_test           (no -j)
ninja -C <build> velox_ch_filecache_manager_test velox_ch_filecache_core_scc_test
ctest --test-dir <build> -R '^velox_ch_filecache_buffered_input_test$' --output-on-failure
ctest --test-dir <build> -R '^(velox_ch_filecache_buffered_input_test|velox_ch_filecache_manager_test|velox_ch_filecache_core_scc_test)$'
git diff --check ; git status --short (velox + gluten)
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_014_buffered_input.log` |
| build buffered_input test (post-review-fix, final) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_full.log` |
| build all 3 gates (final) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_final.log` |
| ctest buffered_input | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_buffered_input.log` |
| ctest all 3 gates (final) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_final.log` |
| pre-existing gates build/test | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_pregates.log`, `.../test_task_014_pregates.log` |

Note: there is no separate RED-build log because the implementation was authored
before the first build; RED evidence was captured behaviorally instead (below).

## Generated logs

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_014_buffered_input.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_buffered_input.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_full.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_final.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_buffered_input.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_final.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_pregates.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_pregates.log
```

## Acceptance evidence

```text
velox_ch_filecache_buffered_input_test:
  test count: 16
  failed tests: 0
  skipped/disabled tests: 0
velox_ch_filecache_manager_test (shared-file regression): 19/19, 0 failed / 0 skipped
velox_ch_filecache_core_scc_test (shared-file regression): 47/47, 0 failed / 0 skipped
git diff --check: clean (exit 0)
git status: only velox/ch/CMakeLists.txt (M) + velox/ch/Disks/ (untracked); unstaged/uncommitted
Gluten: only pre-existing untracked tools/gluten-it/spark-home/; no task change
```

## RED / behavioral evidence

```text
Genuine RED caught real bugs BEFORE they were fixed: the first full run of the
16-case suite had 4 FAILED tests, each pointing at a real defect (not a missing
header):
  - MissThenHitFillsCache: cache-hit over-read (CACHED reader used ABSOLUTE
    readUntil against the segment-relative local cache file) -> fixed by
    setReadUntilPosition(getDownloadedSize()).
  - NonZeroRegion / SeekOutsideBuffer: !isDownloader() assertion in prepare -
    the post-advance front segment was left holding a downloader across Next
    calls -> fixed by the CH-SCOPE_EXIT-equivalent release of the post-advance
    front downloader.

Predownload path RED-verification (proves the H1 review fix is load-bearing and
not false-green):
  - A probe VELOX_FAIL at predownloadForCurrentSegment entry FIRES with
    `bytes=12` under PredownloadFromMidSegment -> the predownload branch is
    genuinely reached.
  - Removing the H1 fix (`state.reader->set(outputBuffer, outputCapacity)` after
    the predownload block) turns PredownloadFromMidSegment RED (the real read
    lands in the predownload scratch, not the output buffer). Restoring it -> GREEN.
```

## Behavioral invariants verified

```text
[x] lazy Next: load never dereferences stream (EnqueueResultDiscardedBeforeLoadNoCreate)
[x] region-relative ByteCount / seek (NonZeroRegionRelativeCoordinatesAbsoluteData, SeekWithinBufferFastPath)
[x] absolute FileCache / ReadFile offsets (NonZeroRegionRelativeCoordinatesAbsoluteData)
[x] shouldPrefetchStripes = false (DwioContractFlags)
[x] preloaded = false; preload no-op (DwioContractFlags)
[x] hasCache = false (DwioContractFlags)
[x] executor returns injected executor (DwioContractFlags)
[x] isBuffered uses no-create get (IsBufferedColdMissNoCreate)
[x] seekToPosition buffer fast path (SeekWithinBufferFastPath)
[x] seekToPosition out-of-buffer rebuild keeps query holder (SeekOutsideBufferRebuildsButKeepsData)
[x] queryContextHolder never reset by seek (SeekOutsideBufferRebuildsButKeepsData; holder held in ctor)
[x] empty etag -> fromPath key (EmptyEtagUsesPathKey)
[x] non-empty etag -> SipHash key; different etags produce different keys (DifferentEtagsDifferentKeys)
[x] enqueue result discarded before load -> no crash / use-after-free (EnqueueResultDiscardedBeforeLoadNoCreate)
[x] miss then hit fills cache (MissThenHitFillsCache)
[x] bypass reads remote without creating metadata (BypassDoesNotCreateMetadata)
[x] Q1/Q2 handoff reuses reader from write offset (Q1Q2HandoffReusesReaderFromWriteOffset)
[x] predownload gap [writeOffset, offset) (PredownloadFromMidSegment) — RED-verified reached (bytes=12)
[x] reserve failure -> bypass, data still correct (ReserveFailureBypassesCacheButReturnsData)
[x] region.offset + region.length overflow throws (RegionOverflowThrows) via checkedAdd
[x] BackUp within last output buffer (BackUpWithinBufferPreservesData)
[x] exception on cold cache-only read leaves no leaked downloader (TempCacheOnlyMissThrowsAndLeavesNoLeak)
[ ] direct-IO alignment: NOT re-tested here; the underlying
    ReadBufferFromVeloxReadFile alignment reject/accept contract is owned and
    tested by Task 007 (velox_ch_io_test). FileCacheInputStream relies on that
    reader unchanged.
[~] random row-group seek correctness: covered functionally by
    SeekOutsideBufferRebuildsButKeepsData (seek far -> holder rebuild -> correct
    bytes). The random-seek BENCHMARK is Task 015's deliverable, not this task.
```

## CH reader-test migration table

| CH case (`src/Interpreters/tests/gtest_filecache.cpp`) | Destination in `FileCacheBufferedInputTest.cpp` / exclusion |
|---|---|
| `CachedReadBuffer` (miss fills queue; second reader hits cache) | `MissThenHitFillsCache` (miss fills cache, second stream is fully buffered + reads cache) |
| reader seek / handoff / concurrent-reader | `Q1Q2HandoffReusesReaderFromWriteOffset`, `SeekWithinBufferFastPath`, `SeekOutsideBufferRebuildsButKeepsData` |
| `CachedReadBufferTruncatedObjectPredownload` (predownload boundary + handoff) | predownload state machine covered by `PredownloadFromMidSegment` (RED-verified the predownload branch is reached and the H1 buffer re-borrow is load-bearing). The *remote-object-truncated-mid-predownload* CANNOT_READ_ALL_DATA sub-path is NOT ported: `ReadBufferFromVeloxReadFile` exposes no `getRemoteFileMetadata`, so the "object shrank between listing and reading" diagnostic has no Velox source; excluded as out of the MVP reader contract (whole-system truncation scenarios belong to Task 015). |
| `CachedReadBufferReadBigAtSourceFailure` | Excluded: `readBigAt` is explicitly NOT migrated (design 03: `readBigAt` "不直接迁移"). Downloader/reader-release-on-failure is instead covered by `TempCacheOnlyMissThrowsAndLeavesNoLeak` (exception on the read path leaves no leaked downloader; a subsequent normal read succeeds). |
| `CachedReadBufferSourceFailure` (next-path source failure + healthy-reader recovery) | Recovery-after-failure covered by `TempCacheOnlyMissThrowsAndLeavesNoLeak` (throw on read path, then a healthy stream reads end to end). A source-`next`-throws injection was attempted and dropped: `LocalReadFile` caches its fd/size at open, so deleting/truncating the source does not fault an already-open handle — no clean Velox fault-injection point exists without a new mock reader (out of the declared file scope). Recorded as an explicit exclusion. |

## Worker review

```text
review subagent: one read-only general-purpose reviewer over the complete
  Task-014 diff (assembled at /tmp/t14_diff.txt) with the task file, the CH
  source, the handoff design HTML, the interface headers, and the 013 receipt.
  Asked only for correctness / concurrency / lifetime / region-coordinate /
  handoff / false-green findings; it did not edit files.
findings:
  H1 [high] Predownload did not restore the caller output buffer before the
     real read: after the predownload loop the reader's working buffer still
     pointed at the predownload scratch (CH restores it via SCOPE_EXIT). The
     post-predownload read would land in scratch, not the query output buffer.
  H2 [high] Bypass-after-failed-predownload: the fresh bypass reader never had
     the query output buffer installed (CH swaps the borrowed buffer in).
  M1 [med]  Possible double completePartAndResetDownloader with a conflicting
     reuse flag on the same segment when no advance occurs.
  L1 [low/false-green] The predownload / reservation-failure paths were not
     exercised by any test (all H1/H2/skip-on-disk-failure branches were dead),
     so neutralizing them kept the suite green. The migration table's
     predownload/source-failure cases were not covered.
  Reviewer confirmed CORRECT (no change): region-coordinate split (CACHED
     relative vs remote absolute), isBuffered no-create get + prefix check,
     checkedAdd usage everywhere, BackUp bound, SkipInt64 remainder logic,
     the Next exception path (no canceled reader returned to FileSegment),
     destructor order (queryContextHolder_ outlives readInfo_.reset()), and the
     seekToPosition fast/slow paths (queryContextHolder_ never reset).
resolutions:
  H1 RESOLVED: readFromCurrentSegment now re-installs the caller output buffer
     (`state.reader->set(outputBuffer, outputCapacity)`) after the predownload
     block for BOTH the predownload-completed and bypass sub-paths. Threaded the
     output buffer + capacity into readFromCurrentSegment.
  H2 RESOLVED: same re-install covers the freshly-built bypass reader.
  M1 RESOLVED: the post-advance front-downloader release now fires ONLY when the
     front segment actually changed (`&front != beforeAdvance`), so the just-read
     segment (already released with the correct reuse flag) is never touched
     twice with a conflicting flag.
  L1 RESOLVED: added PredownloadFromMidSegment (pre-populates a partial segment
     via the direct FileCache API, then a stream reads across the gap — probe
     confirms bytesToPredownload=12 is reached; removing the H1 fix turns it RED)
     and ReserveFailureBypassesCacheButReturnsData (tiny cache -> reserve fails
     -> bypass -> data still correct). Also honored cacheOptions().remoteFsBufferSize
     so a small buffer can bound one downloader term.
  All fixes rebuilt clean; all 16 buffered-input tests pass; both shared-file
  gates re-run green (19/19, 47/47).
unresolved findings: none.
```

## Verification

```text
Red build failed for the right reasons: the first suite run had 4 real
  behavioral failures (over-read on cache hit, downloader-left-held assertion),
  each fixed by a targeted change; the predownload H1 fix is RED-verified.
Final build exit code: 0
Focused test result: 16/16 passed, 0 failed, 0 skipped
git diff --check result: clean (exit 0)
Gluten repository status: unchanged (only pre-existing untracked spark-home/)
```

## Blocking errors

```text
None
```

## Blockers

```text
None. All three gates are green.
```

## Recommended next task

```text
Task 014 checkpoint: run the whole-port source-contract review of Tasks 003-014.
Only with zero unresolved findings and explicit user approval, proceed to
Task 015 (Velox-only FileCache E2E + random-seek benchmark). The remote-object
truncation and readBigAt-style source-failure scenarios excluded above (no Velox
getRemoteFileMetadata / no clean LocalReadFile fault-injection point) belong to
Task 015's whole-system coverage.
```

## Worker declaration

```text
Only Task 014 was attempted.
Changes are unstaged and uncommitted; velox remains at baseline 5e3ee1ac9.
File scope is exactly: velox/ch/CMakeLists.txt (add_subdirectory) + the new
velox/ch/Disks/** tree (2 CMake dirs, 4 headers, 2 .cpp, 1 test CMake, 1 test)
+ this receipt. No ClickHouse source, no Gluten file, no file outside that set
was changed.
The worker stopped after writing this receipt.
```

## Controller review 1

```text
controller_status: changes_requested
environment_profile: home-chang
task: 014
```

## Review evidence

```text
scope review: PASS. Only velox/ch/CMakeLists.txt (+add_subdirectory(Disks)) and the
  new velox/ch/Disks/** tree changed. Gluten clean (pre-existing untracked
  spark-home/ only). ClickHouse: receipt only. git diff --check clean.
gate verification (Controller-run directly): velox_ch_filecache_buffered_input_test
  16/16, velox_ch_filecache_manager_test 19/19, velox_ch_filecache_core_scc_test
  47/47 — all 0 failed / 0 skipped.
implementation review (independent read-only reviewer over the full diff + the CH
  reader + FileSegment/FileCache API): NO real correctness bug. Confirmed correct:
    - lazy load (requests_ holds copied Region values; load dereferences nothing);
    - queryContextHolder_ declared before readInfo_ (destructs last) and alive during
      the destructor's explicit readInfo_.reset(); never reset by seekToPosition;
    - downloader release ordering incl. the M1 post-advance guard (&front !=
      beforeAdvance) — no double-release, canceled reader not returned to FileSegment;
    - region-relative stream coords vs absolute FileCache/ReadFile offsets; CACHED
      segment-relative (setReadUntilPosition(getDownloadedSize()) + seek(off-range.left)),
      remote absolute clamped to fileSize; checkedAdd everywhere (RED-verified: the
      cache-hit over-read was a genuine mixed-coordinate bug, really fixed);
    - H1 predownload fix present + load-bearing (reader->set(outputBuffer,...) after
      predownload for both completed and bypass sub-paths; PredownloadFromMidSegment
      reaches bytesToPredownload=12, RED on fix removal);
    - DWIO flags all false + asserted; isBuffered uses no-create get (not getOrSet);
      key derivation empty->fromPath / non-empty->SipHash128, different etags differ.
  Two excluded CH migration cases verified LEGITIMATE: reader exposes no
    getRemoteFileMetadata and no readBigAt (grep-confirmed); LocalReadFile caches
    fd/size so no clean in-scope fault-injection point. Deferral to Task 015 sound.
unresolved findings (all weak-assertion / coverage-gap, non-blocking correctness but
  gate-relevant false-green risk on named paths):
  F1 (medium) ReserveFailureBypassesCacheButReturnsData asserts ONLY data
    correctness — does not prove reserve failed or the bypass path was taken. A
    pure cache-success path would also pass; the test cannot fail if bypass were
    broken but the tiny cache happened to admit the segment. This is the weakest
    link and the test is named for the bypass path.
  F3 (low) the in-`try` mid-read exception-cleanup path (Next catch block,
    FileCacheInputStream.cpp ~763-773: releaseDownloaderIfNeeded(false)+state_.reset()
    then rethrow) has NO direct test. TempCacheOnlyMissThrowsAndLeavesNoLeak throws in
    nextFileSegmentsBatch BEFORE any downloader/state_ is acquired, so it never
    reaches that catch block. That catch block is the one that guarantees a
    mid-download exception releases the downloader without returning the canceled
    reader — exactly the leak the design forbids.
  F2/F4 (low, ACCEPTED as-is, no change required): MissThenHitFillsCache proves the
    hit via isBuffered==true rather than a remote-read==0 counter; Q1Q2 handoff proven
    by correct continuation rather than reader-object identity. Both assert the
    observable contract; acceptable.
```

## Required changes

```text
Strengthen the two false-green gaps on the paths the tests are named for. TESTS
ONLY — do NOT change the (accepted-correct) production implementation:

1. F1 — ReserveFailureBypassesCacheButReturnsData: after the read, add an assertion
   that the bypass path was actually taken, e.g. EXPECT_EQ(cache->getFileSegmentsNum(),
   0u) (no cache metadata persisted for the segment) OR a state/counter probe proving
   reserve failed and REMOTE_FS_READ_BYPASS_CACHE was used. The assertion MUST fail if
   the reserve-failure->bypass branch were broken while the cache happened to succeed.
   Verify it goes RED if you neutralize the bypass switch (then restore).

2. F3 — add a test that reaches the in-`try` mid-read exception-cleanup path (the
   Next catch block at ~763-773) with a downloader actually held: drive a real
   REMOTE_FS_READ_AND_PUT_IN_CACHE read that acquires the downloader, then fail the
   read/reserve/write MID-stream (a mock/failing source ReadFile or a reserve failure
   after the downloader is held), and assert NO downloader/holder leaked (e.g. a
   subsequent independent acquisition on the same segment/key succeeds and does not
   deadlock or hit a still-held-downloader assertion). If a mid-read fault genuinely
   cannot be injected within the declared file scope (test file + existing helpers),
   record precisely why in the receipt with the exact API limitation, and I will
   accept F3 as a documented Task-015 deferral. Do NOT silently drop it.

Re-run all three gates green (buffered_input + manager + scc, 0 failed / 0 skipped).
Do NOT stage/commit. Append "## Worker attempt 2" (preserve attempt 1 + this review).
```

## Worker attempt 2

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 014
```

Addresses Controller review 1 findings F1 (medium) and F3 (low). Scope of this
attempt = TESTS ONLY. The accepted production implementation
(`FileCacheBufferedInput.{h,cpp}`, `FileCacheInputStream.{h,cpp}`, the helper
headers, and the CMake) was NOT changed — the only edited file is
`velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp`. All three gates are
green (buffered_input 17/17, manager 19/19, scc 47/47; 0 failed / 0 skipped).

## Repository baselines (attempt 2)

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `5e3ee1ac9` | attempt-1 Task-014 changes preserved (unstaged) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | receipt append only |
| `/home/chang/SourceCode/gluten1` | (unchanged) | (unchanged) | pre-existing untracked `tools/gluten-it/spark-home/` only |

velox remains at baseline `5e3ee1ac9`; no staging/commit/amend/rebase/push.

## Files changed (attempt 2)

```text
velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp   (TESTS ONLY)
  + include FileCacheErrnoException.h
  + ThrowOnAppendWriteFile (throws FileCacheErrnoException on first append)
  + ScopedWriteFileFactory (RAII installer for the FileSegment write-file seam,
    mirrors the pattern in FileSegmentTest.cpp; restores the production default)
  ~ ReserveFailureBypassesCacheButReturnsData: added the F1 bypass-proof assertion
  + MidDownloadCacheWriteFailureReleasesDownloaderNoLeak (F3)
```

The attempt-1 production files are byte-identical to Controller-review-1's
accepted diff (verified: no probe/RED remnants — `grep -c "F1 RED\|F3 RED\|F3
PROBE"` over both production files and the test = 0).

## F1 resolution — bypass path now proven, RED-verified

```text
ReserveFailureBypassesCacheButReturnsData previously asserted ONLY
readAll == content (a cache-SUCCESS path would pass identically). It now also
proves the BYPASS path was taken: after the read (tiny cache, maxSize=8 ->
reserve fails), a tempCacheOnly read of the SAME range must throw because
NOTHING was written to the cache on the bypass path:
    FileCacheReadOptions opts; opts.tempCacheOnly = true;
    ... EXPECT_ANY_THROW(stream->Next(&data, &size));

RED verification (test genuinely distinguishes bypass from cache-success):
  Temporarily raising the cache to maxSize=16MiB / maxElements=100 makes the
  reserve SUCCEED, so the segment IS written to cache and the tempCacheOnly read
  then SUCCEEDS -> EXPECT_ANY_THROW fails:
    [  FAILED  ] FileCacheBufferedInputTest.ReserveFailureBypassesCacheButReturnsData
  Restored the tiny-cache settings -> GREEN. This proves the assertion cannot
  pass on a cache-success path; it fails if reserve-failure were to cache the
  segment instead of bypassing.
  (Note: neutralizing only the `state.readType = REMOTE_FS_READ_BYPASS_CACHE`
  line does NOT flip the test, because a failed reserve never calls writeCache
  regardless — so nothing is cached either way; the meaningful discriminator is
  reserve success vs failure, which the large-cache RED exercises directly.)
```

## F3 resolution — mid-download catch block reached with a downloader held

```text
MidDownloadCacheWriteFailureReleasesDownloaderNoLeak drives a real
REMOTE_FS_READ_AND_PUT_IN_CACHE read that acquires the segment downloader in
prepareReadFromFileSegmentState, then faults the cache write MID-download by
installing a ThrowOnAppendWriteFile via the process-wide FileSegment write-file
factory seam (setWriteFileFactoryForTesting, the same seam FileSegmentTest.cpp
uses). With skipCacheOnDiskFailure=false (the FileCache default),
FileCacheInputStream::writeCache rethrows, so the exception propagates out of
readFromCurrentSegment INTO FileCacheInputStream::Next's in-`try` catch block
(the releaseDownloaderIfNeeded(false)+state_.reset()+rethrow block).

REACHABILITY PROVEN (not asserted on faith): a temporary probe
`VELOX_FAIL("[F3 PROBE] catch reached; isDownloader={}", fileSegment.isDownloader())`
placed at the top of that catch block FIRES during the test with
`isDownloader=true`:
    Function:Next, Expression: [F3 PROBE] catch reached; isDownloader=true
So the exact branch the Controller named is exercised WITH a real downloader
held. Probe removed after verification (grep count 0).

The test asserts no leak: after the faulting stream is destroyed and the
write-file factory restored, an INDEPENDENT stream on the same key/segment
becomes the downloader and reads the segment end to end (no deadlock / no
still-held-downloader assertion): EXPECT_EQ(readAll(*stream), content).

Honest scope note on the single-neutralize RED for F3: removing ONLY the
catch-block `releaseDownloaderIfNeeded` does NOT flip this test RED, because the
FileCacheInputStream DESTRUCTOR is a second, independent safety net — it calls
`releaseDownloaderIfNeeded(front, false)` then `readInfo_.reset()` when the
stream is destroyed, so the cross-stream "no leak" observable is guarded by BOTH
the catch block and the destructor (defense-in-depth, matching CH's
nextImplStep SCOPE_EXIT + destructor). A test that isolates the catch block from
the destructor safety net would require either (a) probing internal downloader
state on the SAME live stream immediately after the throw (no such public probe
exists on FileCacheInputStream — it exposes no getter for state_ / the held
downloader), or (b) leaking a still-borrowed reader observably, which also
requires reading FileSegment-internal reader identity not exposed publicly.
Neither is available within the declared file scope (the test file + existing
helpers; no new production accessor may be added in this tests-only attempt).
What IS proven in-scope and recorded above: the catch block is genuinely reached
with a downloader held (probe), the mid-download fault does not deadlock, and
the segment stays reusable by an independent downloader. The finer-grained
"catch-block-only, isolated from the destructor" assertion is recorded as a
Task-015 whole-system concern (it needs a downloader/reader state probe on the
live stream that this MVP reader deliberately does not expose).
```

## Commands and outcomes (attempt 2)

| Command purpose | Exit code | Log |
|---|---:|---|
| build buffered_input test | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_attempt2.log` |
| build all 3 gates (final) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_attempt2_final.log` |
| ctest all 3 gates (final) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_attempt2_final.log` |
| F1 RED-verify (large-cache neutralize) | (RED as expected) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/../tmp f1red2 (transient; not persisted)` |
| F3 catch-reachability probe | (probe fired) | (transient; probe removed) |

No `-j` was passed to ninja.

## Acceptance evidence (attempt 2)

```text
velox_ch_filecache_buffered_input_test: 17 tests, 0 failed, 0 skipped
velox_ch_filecache_manager_test:        19 tests, 0 failed, 0 skipped
velox_ch_filecache_core_scc_test:       47 tests, 0 failed, 0 skipped
git diff --check: clean (exit 0)
git status: velox/ch/CMakeLists.txt (M) + velox/ch/Disks/ (untracked); the only
  attempt-2 change vs attempt 1 is the one test file; production files unchanged.
Gluten: only pre-existing untracked tools/gluten-it/spark-home/; no task change.
probe/RED remnants: none (grep -c over production + test = 0).
```

## Worker review (attempt 2)

```text
review subagent: not launched for attempt 2 — this attempt only strengthens two
  tests (no production change), directly satisfying the Controller's F1 and F3.
  The full read-only review of the implementation was done in attempt 1 and its
  findings (H1/H2/M1/L1) were resolved there; the Controller independently
  re-reviewed and accepted the implementation. Re-reviewing unchanged production
  code would yield nothing new; the two test changes were instead verified by
  the F1 RED-on-large-cache check and the F3 catch-reachability probe above.
findings: none new.
resolutions: F1 resolved (bypass-proof assertion, RED-verified); F3 resolved to
  the extent possible in-scope (catch block proven reached with a downloader
  held; no deadlock; segment reusable) with the precise in-scope-impossibility
  of the isolated catch-only RED documented for Task 015.
unresolved findings: none.
```

## Blockers (attempt 2)

```text
None. All three gates are green.
```

## Worker declaration (attempt 2)

```text
Only Task 014 was attempted (attempt 2: the F1/F3 tests-only fix).
Changes are unstaged and uncommitted; all attempt-1 production files are
preserved byte-identical. The only attempt-2 edit is
velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp. velox remains at
baseline 5e3ee1ac9. No ClickHouse source, no Gluten file changed.
The worker stopped after writing this receipt.
```

## Controller review 2

```text
controller_status: accepted
environment_profile: home-chang
task: 014
```

## Review evidence

```text
scope review: PASS. Attempt-2 change is test-only (FileCacheBufferedInputTest.cpp);
  production files byte-identical to the Controller-review-1-accepted diff. Full
  task scope = velox/ch/CMakeLists.txt (+add_subdirectory(Disks)) + the new
  velox/ch/Disks/** tree. Gluten clean (pre-existing untracked spark-home/ only).
implementation review: accepted in Controller review 1 (independent reviewer, no
  correctness bug on state machine / lifetime / coordinates / downloader release;
  both CH exclusions verified legitimate).
F1 resolution — INDEPENDENTLY VERIFIED by the Controller: I reproduced the RED —
  raised the test cache to maxSize=16MiB/maxElements=100 so reserve SUCCEEDS, the
  segment is cached, and the bypass-proof assertion (tempCacheOnly re-read must
  throw) then FAILED:
    [ FAILED ] FileCacheBufferedInputTest.ReserveFailureBypassesCacheButReturnsData
  Restored the tiny-cache settings -> GREEN. The assertion genuinely distinguishes
  the reserve-failure->bypass path from a cache-success path.
F3 resolution — ACCEPTED as a documented in-scope limitation + Task-015 deferral:
  the worker proved (probe) the in-`try` Next catch block IS reached with a
  downloader held (isDownloader=true) via a real REMOTE_FS_READ_AND_PUT_IN_CACHE
  read faulted mid-download by an injected ThrowOnAppendWriteFile (the
  setWriteFileFactoryForTesting seam). The test asserts no leak (an independent
  stream on the same key becomes downloader and reads end-to-end). The isolated
  catch-block-only RED does not flip because the FileCacheInputStream destructor
  is a second safety net (defense-in-depth matching CH's SCOPE_EXIT + destructor);
  isolating it would need a live-stream downloader-state probe the MVP reader
  deliberately does not expose. That finer assertion is correctly recorded as a
  Task-015 whole-system concern — I accept this per my review-1 offer.
log and test review: Controller re-ran all three binaries directly after the
  restore-rebuild — velox_ch_filecache_buffered_input_test 17/17,
  velox_ch_filecache_manager_test 19/19, velox_ch_filecache_core_scc_test 47/47,
  all 0 failed / 0 skipped. No probe/RED remnants (grep 0).
unresolved findings: none. (F2/F4 accepted as observable-contract assertions in
  review 1; F3 finer assertion deferred to Task 015 with recorded justification.)
```

## Required changes

```text
None. Task 014 accepted.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `bc78ef541` |
| `/home/chang/SourceCode/ClickHouse` | receipt+handoff = this commit |
