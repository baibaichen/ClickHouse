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

## Worker attempt 4 (F-014-1 reopen fix)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 014 (reopen — F-014-1 only)
```

### Reopen contract

`reopened_by_contract_audit` for exactly one behavior hole, F-014-1: port CH's
self-heal-on-external-truncation in `getCacheReadBuffer`
(`src/Disks/IO/CachedOnDiskReadBufferFromFile.cpp:448-477`). No other change; the
segment-relative coordinate system (F-014-2, signed-accepted) is untouched.

### Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `bc78ef541` | clean (Task 014 accepted) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | (receipt repo) | receipt-only |

Branch is `filecache2` (per dispatch note, proceed; the `filecache`-vs-name
mismatch is expected and not a blocker).

### Files changed

```text
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheInputStream.cpp
/home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp
```

Only the two allowed files. No header signature change was needed
(`getCacheReadBuffer` already returns `ReaderPtr`; a null return signals bypass).
No CMake, no Gluten, no other scope.

### Implementation summary

In `getCacheReadBuffer` (`FileCacheInputStream.cpp`), AFTER opening the local
cache file and BEFORE returning it:
- Observe the terminal state first (`fileSegment.state()`), then read the actual
  on-disk size via `readInfo_.cacheReader->tryGetFileSize()` (set from
  `readFile_->size()` at reader construction). Ordering mirrors CH's concurrency
  reasoning.
- `trustSizeFromFilename = hasSizeInFileName() && state ∈ {DOWNLOADED, DETACHED}`.
- If `trustSizeFromFilename && cacheFileSize < getDownloadedSize()`: reset the
  cache reader and `return nullptr` (do NOT throw) — the caller bypasses the
  cache and re-fetches from source. Covers the empty-file case for the gated
  segment type too.
- Else if `cacheFileSize == 0`: `VELOX_FAIL("Attempt to read from an empty cache
  file: {}", path)` — LOGICAL_ERROR-class, mirroring CH `:474-475`.
- Non-truncated case unchanged.

Routing: the `create` lambda in `createReadFromFileSegmentState` now treats a
null CACHED buffer as a switch to `REMOTE_FS_READ_BYPASS_CACHE` (flips `type`,
rebuilds the reader via `getRemoteReadBuffer`, and sets `s->readType = type` so
`prepareReadFromFileSegmentState` uses the absolute-offset bypass branch). This
mirrors CH's "null cache reader => bypass". The rename-reopen branch and the
per-segment 0-based CACHED coordinate system are unchanged.

### Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| build (GREEN, initial) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_reopen_1.log` |
| test buffered_input (GREEN, initial) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_reopen_1.log` |
| build (RED, self-heal neutralized to `if (false)`) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_reopen_red.log` |
| test new tests (RED, neutralized) | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_reopen_red.log` |
| build all 3 gates (fix restored) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_reopen_2.log` |
| ctest all 3 gates | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_reopen_all.log` |

No `-j` was used on any ninja invocation.

### Acceptance evidence

```text
Gate test counts (all 0 failed / 0 skipped):
  velox_ch_filecache_buffered_input_test : 19 tests (17 original + 2 new), 19 passed, 0 failed, 0 skipped
  velox_ch_filecache_manager_test        : 19 tests, passed, 0 failed, 0 skipped
  velox_ch_filecache_core_scc_test       : 47 tests, passed, 0 failed, 0 skipped
  ctest aggregate: 100% tests passed, 0 tests failed out of 3

New tests:
  FileCacheBufferedInputTest.ExternalTruncationSelfHealsFromSource  OK
  FileCacheBufferedInputTest.EmptyCacheFileSelfHealsFromSource      OK

RED-on-neutralize evidence (self-heal branch replaced with `if (false)`):
  Both new tests FAIL (TEST_EXIT=1, 2 FAILED TESTS).
  - ExternalTruncationSelfHealsFromSource: the CACHED reader is bounded to the
    segment's downloadedSize (64) but the on-disk file is 32 bytes, so
    LocalReadFile::PReadInternal throws "bytesRead == length (32 vs. 64)" — a hard
    read failure with no self-heal (the exact behavioral hole).
  - EmptyCacheFileSelfHealsFromSource: with the bypass branch disabled, the
    `cacheFileSize == 0` guard fires VELOX_FAIL "Attempt to read from an empty
    cache file", so the read fails instead of self-healing.
  With the fix restored, both re-fetch the full 64 original bytes and pass.

git diff --check: clean (no whitespace errors)
git diff --stat: 2 files changed, 155 insertions(+)
```

### Behavioral invariants (F-014-1)

```text
[x] size-in-filename DOWNLOADED/DETACHED + actual_size < downloadedSize -> bypass + re-fetch, no throw
[x] actual_size == 0 (non-gated CACHED) -> LOGICAL_ERROR-class throw (VELOX_FAIL "Attempt to read from an empty cache file")
[x] non-truncated case unchanged
[x] state observed before size (CH concurrency ordering preserved)
[x] rename-reopen branch unchanged
[x] segment-relative CACHED coordinate system unchanged (F-014-2 out of scope)
[x] RED verified by neutralizing the self-heal branch
```

### Worker review

```text
review subagent: one read-only general-purpose reviewer over the full git diff.
findings:
  - No actionable issues. Routing correct: null CACHED buffer flips to
    REMOTE_FS_READ_BYPASS_CACHE, prepareReadFromFileSegmentState uses absolute
    offsets; segment-relative CACHED math never reached on the heal path.
  - state-before-size ordering preserved.
  - Empty-file VELOX_FAIL guard is correct; for the gated size-in-filename case
    the bypass branch pre-empts it, so the throw only fires for a genuinely
    0-byte non-size-in-filename CACHED segment (mirrors CH).
  - Both new tests genuinely exercise the changed path and were confirmed RED
    when the branch is neutralized; no trivial-pass path (64 distinct bytes,
    full-buffer EXPECT_EQ).
non-blocking observation:
  - EmptyCacheFileSelfHealsFromSource covers the bypass branch (its documented
    intent), not the VELOX_FAIL throw path; directly testing the throw would need
    a non-size-in-filename empty segment, judged out of scope for this reopen.
resolutions: no code change required; gates already green.
unresolved findings: none.
```

### Blockers

```text
None.
```

### Worker declaration

```text
Only Task 014 F-014-1 (reopen) was attempted.
Changes are unstaged and uncommitted (velox only; two files).
The worker stopped after writing this receipt.
```

## Controller review 3 (F-014-1 reopen)

```text
controller_status: accepted
environment_profile: home-chang
task: 014 (reopen: F-014-1)
```

## Review evidence

```text
scope review: PASS. Only FileCacheInputStream.cpp (+45, the self-heal branch) and
  tests/FileCacheBufferedInputTest.cpp (+110, two RED tests) changed vs accepted
  HEAD bc78ef541. No other file touched; git diff --check clean.
implementation review: the self-heal branch (FileCacheInputStream.cpp:146-182)
  mirrors CH getCacheReadBuffer (CachedOnDiskReadBufferFromFile.cpp:448-477):
  observes terminal state first, reads actual on-disk size via tryGetFileSize;
  when hasSizeInFileName() && state ∈ {DOWNLOADED,DETACHED} && cacheFileSize <
  getDownloadedSize() -> reset reader + return nullptr (caller routes CACHED ->
  REMOTE_FS_READ_BYPASS_CACHE, re-fetch, no throw, MergeTree part not wrongly
  detached); empty-file (cacheFileSize==0) -> VELOX_FAIL, matching CH :474-475.
  Segment-relative CACHED seek (offset-range.left) unchanged (F-014-2 confirmed a
  match, withdrawn).
F-014-1 RED — INDEPENDENTLY VERIFIED by Controller: neutralized BOTH new branches
  (if(false) on the truncation check and the empty-file guard), rebuilt, and the two
  new tests FAILED:
    ExternalTruncationSelfHealsFromSource -> FAILED
    EmptyCacheFileSelfHealsFromSource     -> FAILED
  Restored byte-identical (grep 0 probe remnants), rebuilt, all three gates GREEN.
log/test review: Controller re-ran the three binaries directly after restore —
  velox_ch_filecache_buffered_input_test 19/19, velox_ch_filecache_manager_test
  19/19, velox_ch_filecache_core_scc_test 47/47; 0 failed / 0 skipped.
unresolved findings: none. F-014-2 WITHDRAWN (misdiagnosis; reading the local cache
  segment is segment-relative in BOTH CH and Velox — byte-identical seek(offset-
  range.left); no deviation, no code change). F-011-T downgraded to non-blocking
  backlog.
```

## Required changes

```text
None. Task 014 F-014-1 reopen accepted; whole-port review 2 zero-unresolved gate met.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | (this acceptance — see Velox `Task 014:` reopen commit) |
| `/home/chang/SourceCode/ClickHouse` | receipt+review-docs+handoff = this commit |

## Worker attempt (post-acceptance amendment 1 — SkipInt64 cross-segment)

```text
worker_status: blocked
environment_profile: home-chang
task: 014 (post-acceptance amendment 1)
```

### Summary

The amendment's declared fix (rewrite `SkipInt64` to align with CH
`CachedOnDiskReadBufferFromFile::seek`) was implemented and is correct in
isolation (review clean, all local gates green), BUT it does **not** resolve the
real TPCH crash. Direct instrumentation proves the 6 crashing queries never call
`SkipInt64` or `seekToPosition` on the failing path. The crash is a **different,
out-of-scope bug**: `FileCacheInputStream::Next` returns `got == 0` at exactly
`position_ == 1048576` (1 MiB = `kDefaultOutputBufferSize`) while ~5 MiB of the
region remain, so DWIO `StreamUtil.h:67 readBytes` aborts "Reading past end".
Fixing this is outside the amendment's declared file/behavior scope
(`SkipInt64` rewrite), so per EXECUTION_PROTOCOL (record the conflict, do not
expand scope, do not fabricate green) this attempt is `blocked`.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `1a90c042afcd45d41b0de8c37edb0621c9ad310f` | `velox/exec/tests/utils/TpchQueryBuilder.cpp` dirty (pre-existing 018b, preserved) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `49961f961452760c188666775a16710cacad9cc0` | `port/task/014-*.md`, `port/task/018b-*.md` dirty (pre-existing) |

## Files changed (this attempt, all under velox/ch/)

```text
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheInputStream.cpp
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheInputStream.h
/home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
```

`git diff --stat velox/ch/`: 3 files, +160 / -25. `git diff --check` clean.
Pre-existing `velox/exec/tests/utils/TpchQueryBuilder.cpp` (018b) untouched.

## Implementation

- Rewrote `SkipInt64` (`FileCacheInputStream.cpp:931-963`): fast path advances
  `offsetInOutputBuffer_`/`position_` when the target stays inside the published
  output buffer; slow path computes `target = position_ + toSkip`,
  `VELOX_CHECK_LE(target, region_.length)`, and calls the new helper
  `invalidateAndReposition(target)` — no `Next()` call, no rollback side effect.
- Factored `invalidateAndReposition(uint64_t)` (`FileCacheInputStream.cpp:990`),
  reused by both `SkipInt64` slow path and `seekToPosition` slow path. Body is
  byte-for-byte identical to the original inlined `seekToPosition` slow path.
- Header: added the private declaration.
- Added 3 tests (`FileCacheE2ETest.cpp`): `SkipFromMidSegmentAcrossBoundary`,
  `SkipMidSegmentAcrossTwoSegments`, `ConsecutiveSkipsAcrossBoundaries` — each
  pre-warms the cache (hit path), reads part of segment 0, skips across one/two
  boundaries, and asserts the actual bytes at the correct absolute offset.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| debug build e2e (pre-fix, RED build) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/build_red_e2e2.log` |
| brute-force pre-fix skip search (all pass — bug not reproducible at unit level) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/run_brute_prefix.log` |
| debug build e2e (with fix, GREEN) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/build_final_e2e.log` |
| e2e run (20 tests, 17+3) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/reg_filecache_e2e_final.log` |
| buffered_input 19 | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/reg_filecache_buffered_input.log` |
| manager 20 | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/reg_filecache_manager.log` |
| core_scc 47 | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/reg_filecache_core_scc.log` |
| observability 14 | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/reg_observability.log` |
| cancellation 5 | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/reg_cancellation.log` |
| connector 4 | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/reg_filecache_connector.log` |
| hit_metrics 5 | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_logs/reg_filecache_hit_metrics.log` |
| release build benchmark (pre-fix binary) | 0 | (stale binary predating changes; used for RED) |
| RELEASE TPCH RED q2/11/15/17/20/21 (pre-fix binary) | 0 | `/home/chang/OpenSource/velox/cmake-build-release-gcc13/task014_logs/red_q*.log` |
| release build benchmark (with fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-release-gcc13/task014_logs/build_release_final.log` |
| RELEASE TPCH GREEN attempt q2/11/15/17/20/21 (with fix) — STILL FAIL | 0 | `/home/chang/OpenSource/velox/cmake-build-release-gcc13/task014_logs/green_q*.log` |
| instrumented run q17 (SkipInt64/seek/Next debug prints) | 0 | `/home/chang/OpenSource/velox/cmake-build-release-gcc13/task014_logs/dbg3_q17.log` |
| direct q2 baseline (works, 12.3M rows) | 0 | `/home/chang/OpenSource/velox/cmake-build-release-gcc13/task014_logs/direct_q2.log` |

## Acceptance evidence

```text
test count (debug regression, all pass, 0 failed / 0 skipped):
  e2e 20 (17 original + 3 new), buffered_input 19, manager 20, core_scc 47,
  observability 14, cancellation 5, connector 4, hit_metrics 5.
existing SkipAcrossSegmentBoundary: still green.

RED unit test requirement: NOT satisfiable in-harness. A brute-force search over
  thousands of (remoteFsBufferSize x partial-read x skip-distance) combinations
  on the cache-HIT path (run_brute_prefix.log) shows the PRE-FIX SkipInt64 returns
  byte-correct data in every case: it reads-and-discards every skipped byte
  sequentially, which keeps the (non-re-seekable) CACHED reader aligned. The
  desync the amendment describes does not reproduce with LocalReadFile-backed
  segments; it needs the production compressed-page DWIO stack.

E2E RED (authoritative): PRE-FIX release binary, all 6 queries fail:
  q2/11/15/17/20/21 -> 0 rows, error="task failed (see ERROR log)",
  ERROR log = "StreamUtil.h:67 readBytes ... Reading past end" (INVALID_STATE).
  Stack: PageReader::skip -> seekToPage -> prepareDataPageV1 -> readBytes.

E2E GREEN (attempted): WITH-FIX release binary, all 6 queries STILL fail with the
  IDENTICAL "Reading past end". Fix is compiled in (binary newer than source,
  3x invalidateAndReposition present, 0x "position_ -= produced").

ROOT CAUSE of the real crash (definitive, via instrumentation dbg3_q17.log):
  Next() returns via the `got == 0` branch at position_=1048576 (exactly 1 MiB =
  kDefaultOutputBufferSize) with region length 6318358. Zero SkipInt64 calls,
  zero seekToPosition calls, zero Next-returns-at-region-end on the crash path.
  => The crash is a sequential-read refill bug at the 1 MiB output-buffer
  boundary, NOT a SkipInt64 cross-segment rollback bug. The amendment's premise
  is incorrect for this crash; the declared scope (rewrite SkipInt64) cannot fix
  it. direct q2 succeeds (12,381,970 rows, empty error), so data/query are fine;
  only the filecache read path is broken.

benchmark result: 6/6 queries still crash with the fix -> E2E acceptance NOT met.
git diff --check: clean (velox/ch/ only; TpchQueryBuilder.cpp 018b preserved).
```

## Worker review

```text
review subagent: pr-review-toolkit:code-reviewer (read-only, over the velox/ch/ diff)
findings: No correctness / lifetime / concurrency / false-green issues.
  - target = position_ + toSkip correct given the DWIO contract.
  - invalidateAndReposition confirmed byte-for-byte behavior-preserving vs the
    original inlined seekToPosition slow path.
  - No residual advance-via-Next-then-rollback.
  - VELOX_CHECK_LE(target, region_.length) is the right bound (matches seek).
  - The 3 new tests genuinely hit the slow path and assert real bytes at the
    correct absolute offset; low false-green risk.
  - Minor non-blocking: a stray brace tidy in completeCurrentSegmentAndAdvance.
resolutions: Reverted the stray brace so completeCurrentSegmentAndAdvance is
  untouched; rebuilt+reran e2e (20 pass) and the clean release binary.
unresolved findings: None from the review of the SkipInt64 diff itself.
```

## Blockers

```text
BLOCKER: The amendment's fix does not resolve the real TPCH crash. Instrumented
evidence (cmake-build-release-gcc13/task014_logs/dbg3_q17.log) proves the crash
path never calls SkipInt64/seekToPosition; Next() returns got==0 at position_
== 1 MiB (kDefaultOutputBufferSize) with ~5 MiB of the region unread. This is a
sequential-read / output-buffer-refill defect, distinct from the SkipInt64
cross-segment rollback the amendment targets. Resolving it requires a scope
change / new contract review (likely in Next / readFromCurrentSegment /
updateCurrentReaderIfNeeded around the 1 MiB buffer boundary), which is outside
this amendment's declared file+behavior scope.

Decision needed from the controller/user: re-scope the amendment (or open a new
task) to fix the Next() got==0-at-1MiB refill bug. The SkipInt64 rewrite in this
attempt is correct and review-clean and can be kept as a faithful CH-alignment
change, but it must not be presented as fixing the 6 TPCH queries — it does not.
```

## Worker declaration

```text
Only Task 014 post-acceptance amendment 1 was attempted.
Changes are unstaged and uncommitted (velox/ch/ only; 018b TpchQueryBuilder.cpp
preserved and untouched).
The worker stopped after writing this receipt.
```

## Controller review — amendment 1（SkipInt64 跨段修复；refill bug 拆出）

```text
controller_status: accepted (SkipInt64 部分)
environment_profile: home-chang
task: 014 (amendment 1)
note: worker 诚实证伪了原根因假设——SkipInt64 修复正确但非本次 TPCH 崩溃解药；
      真凶是 Next() 在 1 MiB output-buffer 边界的 refill bug，已拆为独立 task。
```

## Review evidence

```text
scope review: SkipInt64 改动限于 FileCacheInputStream.{h,cpp} + tests/FileCacheE2ETest.cpp
  (3 文件)。TpchQueryBuilder.cpp 的 018b 两处补丁是既有未提交改动，本次不提交（单独走 018b）。
implementation review: SkipInt64 重写对齐 CH seek/原生：快路径目标在当前 buffer 内→只挪
  offsetInOutputBuffer_/position_（保 BackUp 语义）；慢路径 target=position_+toSkip →
  invalidateAndReposition（无 Next()、无回拨），下次 Next 从 position_ 重找段。抽出共享
  helper invalidateAndReposition(:988)，seekToPosition 亦复用。消除了原"Next()+position_-=produced"
  不可逆副作用。CH 权威 CachedOnDiskReadBufferFromFile::seek(:2000)。
log and test review: Controller 独立复现 RED——慢路径去掉 invalidate（改 position_=target 不重找段），
  重建，3 个新测试 FAILED（SkipFromMidSegmentAcrossBoundary:419 / SkipMidSegmentAcrossTwoSegments:454
  / ConsecutiveSkipsAcrossBoundaries:488），而旧 SkipAcrossSegmentBoundary 仍绿——坐实旧测试是
  假绿（先读完整段再 skip，测不到）。还原；grep 无残留 probe；e2e 20/20、buffered_input 19、
  core_scc 47 全绿。3 新测试均验真实读出字节（content.substr(target,4096)），非仅 ByteCount。
unresolved: TPCH 端到端未通过——但因 refill bug（Next got==0 @1MiB），非 SkipInt64；
  拆为 Task #30 后续（reopen 014 amendment 2）。
```

## Commits (amendment 1 — SkipInt64)

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `01c007abe` |
| `/home/chang/SourceCode/ClickHouse` | (this acceptance commit) |

## Worker attempt (post-acceptance amendment 2 — CACHED reader readUntil freeze / output-buffer refill bug)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 014 (post-acceptance amendment 2)
```

### Summary

Fixed the real cause of the TPCH filecache crash (amendment 1's `SkipInt64`
rewrite did NOT resolve it). The crash is a read-while-downloading refill freeze
on the CACHED path: a CACHED reader over a still-DOWNLOADING segment was bounded
to the segment's downloaded prefix at prepare time, and `updateReadStateIfNeeded`
only re-prepared when `offset >= getCurrentWriteOffset()`. When a concurrent
driver's downloader advanced the write offset past the cursor, that condition
stayed false, so the stale reader froze at the first flushed chunk (1 MiB) and
`Next` reported a premature end of region -> DWIO `StreamUtil.h:67` aborts
"Reading past end". Fixing the refill freeze then surfaced a SECOND, distinct
DOWNLOADING rename TOCTOU race in `getCacheReadBuffer` (intermittent q20
FILE_NOT_FOUND); both are fixed. All 6 previously-crashing queries now pass
deterministically.

### Root-cause verification vs CH (per the amendment's "务必核实")

The amendment suggested a one-line change of `:465` from
`setReadUntilPosition(getDownloadedSize())` to `setReadUntilPosition(segmentSize)`
and required verifying whether that over-reads. It DOES over-read and is
insufficient, for two confirmed reasons:

1. Our cache reader `ReadBufferFromVeloxReadFile::nextImpl`
   (`velox/ch/IO/ReadBufferFromVeloxReadFile.cpp:183`) computes
   `toRead = min(destCapacity, readUntil_ - startOffset)` — it does NOT clamp to
   the reader's own `fileSize_`. And `LocalReadFile::preadInternal`
   (`velox/common/file/LocalFile.cpp:204-222`) THROWS on any short read
   (`VELOX_CHECK_EQ(bytesRead, length)`) — there is NO implicit EOF clamp. So
   bounding a DOWNLOADING segment's reader to `segmentSize` while the on-disk
   cache file holds only `downloadedSize` bytes would `pread` past EOF and throw.
   CH avoids this because its generic local-file reader naturally short-reads at
   the cache file's real EOF (a size cap our port lacks).
2. CH `CachedOnDiskReadBufferFromFile.cpp:796` bounds to
   `min(range.right + 1, file_size_)` where `file_size_` is the SOURCE object size;
   the CACHED local reader still short-reads at the cache-file EOF, and CH keeps
   reading a growing DOWNLOADING cache file because it reuses the SAME fd (pread
   sees appended bytes). Our reader caches its size at open and cannot see growth,
   so the faithful equivalent is to RE-PREPARE (reopen / re-read size) when the
   reader's downloaded prefix is exhausted.

Therefore the minimal faithful fix keeps `:465` bounding to the downloaded prefix
(the on-disk size; `FileSegment.cpp:484` chassert guarantees
`file_size(path) == downloaded_size`) and instead fixes the FREEZE in the
re-prepare trigger. This is the "扩到完整对齐" path the amendment authorizes, with
reasoning recorded here.

### Scope (this amendment)

```text
velox/ch/Disks/IO/FileCacheInputStream.cpp   — refill re-prepare + rename-retry
velox/ch/Disks/IO/FileCacheInputStream.h     — ReadFromFileSegmentState field
velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp — RED refill test
```

No other velox/ch file, no velox trunk, no Gluten. The pre-existing 018b
`velox/exec/tests/utils/TpchQueryBuilder.cpp` patch is preserved untouched.
`Next` / `readFromCurrentSegment` / `SkipInt64` (amendment 1) are unchanged.

### Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `01c007abe4198be1120d0f447048e819704c26fa` | `velox/exec/tests/utils/TpchQueryBuilder.cpp` dirty (pre-existing 018b, preserved) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `750effdc613197bc94bcf4819cd2e8c85c554f58` | receipt append only |

### Implementation

Fix A — refill freeze (BUG 1):
- `FileCacheInputStream.h`: added `uint64_t cachedPrefixEndAbsolute = 0` to
  `ReadFromFileSegmentState` (absolute offset where a CACHED reader's downloaded
  prefix ends; 0 when the segment was fully DOWNLOADED at prepare).
- `prepareReadFromFileSegmentState` CACHED branch: after bounding to
  `getDownloadedSize()` and seeking, record `cachedPrefixEndAbsolute =
  range.left + downloadedSize` ONLY when `state() != DOWNLOADED`.
- `updateReadStateIfNeeded`: for a CACHED read, re-prepare when
  `prefixExhausted` (`cachedPrefixEndAbsolute != 0 && offset >=
  cachedPrefixEndAbsolute`) OR the original `caughtUpToWrite`
  (`state() != DOWNLOADED && offset >= getCurrentWriteOffset()`). Re-preparing
  opens a fresh reader over the grown cache file (or the DOWNLOADING branch waits
  for more / elects a downloader). Fully DOWNLOADED segments keep
  `cachedPrefixEndAbsolute == 0` and never re-prepare on this axis (no regression
  for the 16 already-working queries).

Fix B — DOWNLOADING rename TOCTOU (BUG 2, surfaced by the E2E once BUG 1 fixed):
- `getCacheReadBuffer`: `getPath()` is sampled lock-free; a concurrent
  downloader can rename `<offset>` -> `<offset>_<size>` between the sample and
  `openFileForRead`, making the sampled path vanish (FILE_NOT_FOUND). CH does not
  hit this because it opens the fd once and the descriptor survives the rename.
  On any open exception, re-sample `getPath()` (now the renamed name) and retry
  once; if the name is unchanged, rethrow (a genuine open failure). The rename
  happens exactly once per segment lifetime, so one retry closes the window.

### RED evidence (BUG 1)

New test `FileCacheE2ETest.CachedReaderRefillsWhenDownloadingSegmentGrows`
deterministically reproduces the freeze with two interleaved streams over one
6 MiB segment (no compressed-page stack, no threads/sleeps):
1. downloader stream D downloads the first 1 MiB (writeOffset = 1 MiB);
2. reader stream R reads its first 1 MiB CACHED (prepare bound = 1 MiB);
3. D downloads 3 more MiB (writeOffset = 4 MiB) — the cache file grows;
4. R must read the rest of the region.

RED verification (fix neutralized: `prefixExhausted = false && ...`, rebuilt):
```text
FileCacheE2ETest.cpp:888: Failure
Expected equality of these values:
  rest.size()   Which is: 0
  n - chunk     Which is: 5242880
CACHED reader froze at its initial 1 MiB prefix (refill bug)
[  FAILED  ] FileCacheE2ETest.CachedReaderRefillsWhenDownloadingSegmentGrows
```
Fix restored (grep "RED-NEUTRALIZE" = 0) -> GREEN. This is a distinct bug from
`SkipInt64` (the crash path calls no skip/seek; the frozen prefix is the cause).

RED evidence (BUG 2): before Fix B, release TPCH q20 failed intermittently
(1 of 3 runs) with `FILE_NOT_FOUND: .../<key>/0` in `getCacheReadBuffer` ->
`LocalReadFile` open (stack through `loadFileMetaData`). After Fix B, q20 passed
5/5.

### Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| debug build e2e (with fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_refill_logs/build_e2e_fix.log` |
| ctest e2e (with fix) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_refill_logs/test_e2e_fix.log` |
| debug build e2e (fix neutralized, RED) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_refill_logs/build_e2e_red.log` |
| ctest refill test (neutralized) -> FAILED | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_refill_logs/test_e2e_red.log` |
| debug build all 8 gates (both fixes) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_refill_logs/build_all_gates2.log` |
| ctest all 8 gates | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task014_refill_logs/test_all_gates2.log` |
| release build tpch_ab benchmark (final) | 0 | `/home/chang/OpenSource/velox/cmake-build-release-gcc13/task014_refill_logs/build_release_benchmark2.log` |
| release TPCH final q2/11/15/17/20/21 | 0 | `/home/chang/OpenSource/velox/cmake-build-release-gcc13/task014_refill_logs/final_run_q*.log`, `final_q*.csv` |
| release q20 x5 (determinism, all pass) | 0 | `/home/chang/OpenSource/velox/cmake-build-release-gcc13/task014_refill_logs/run_q20b*.log` |

No `-j` on any ninja invocation.

### Acceptance evidence

```text
Debug regression (all 0 failed / 0 skipped):
  velox_ch_filecache_e2e_test          : 21 tests (20 prior + 1 new refill test)
  velox_ch_filecache_buffered_input_test : 19
  velox_ch_filecache_manager_test      : 20
  velox_ch_filecache_core_scc_test     : 47
  velox_ch_observability_test          : 14
  velox_ch_cancellation_test           : 5
  velox_ch_filecache_connector_test    : 4
  velox_ch_filecache_hit_metrics_test  : 5
  ctest aggregate: 100% tests passed, 0 failed out of 8
Amendment-1 skip tests + old SkipAcrossSegmentBoundary: still green (within e2e).

END-TO-END (decisive) — release binary, filecache path, num_splits_per_file=1,
num_drivers=4, dataset /home/chang/test/tpch-double/tpch-generated-100.0-parquet-decimal_as_double:
  q02  exit 0  rows 12382493  error=<empty>
  q11  exit 0  rows 3180133   error=<empty>
  q15  exit 0  rows 45334136  error=<empty>
  q17  exit 0  rows 600036861 error=<empty>
  q20  exit 0  rows 967853    error=<empty>   (5/5 deterministic after Fix B)
  q21  exit 0  rows 39950     error=<empty>
All 6 previously-crashing queries exit 0 with a result row and EMPTY error.

git diff --check: clean
git diff --stat velox/ch/: 3 files, +139 / -10
Pre-existing velox/exec/tests/utils/TpchQueryBuilder.cpp (018b): untouched.
Gluten: not touched.
```

### Worker review

```text
review subagent: one read-only general-purpose reviewer over the velox/ch/ diff
  (correctness of the bound/re-prepare, DOWNLOADING over-read safety, rename-retry
  safety, and false-green in the new test).
findings: No actionable / blocking issues.
  - BUG 1 coordinate space correct (absolute vs absolute); no spin (DOWNLOADING
    branch waits, not busy-loops); no double-read (fresh reader seeks to cursor);
    fully-DOWNLOADED segments never re-prepare on the prefix axis (no regression).
  - BUG 2 single retry sufficient (rename is once-per-lifetime) and fail-closed
    (rethrows when the name is unchanged, so a genuine FILE_NOT_FOUND still
    propagates).
  - New test is a deterministic, thread-free RED guard for BUG 1 (verified RED on
    neutralize); additionally exercises a downloader handoff for the tail.
non-blocking observations (no change required):
  - the `!= DOWNLOADED` guard on the `caughtUpToWrite` clause is redundant with
    the prefix axis (harmless).
  - `catch (const std::exception &)` is broad but safe here only because of the
    name-equality rethrow.
  - BUG 2 has no dedicated unit test (inherently threaded); it is covered by the
    release TPCH E2E (q20 5/5).
resolutions: no code change required; all gates green.
unresolved findings: none.
```

### Blockers

```text
None. All 8 debug gates green; all 6 release TPCH queries exit 0 with empty error.
```

### Worker declaration

```text
Only Task 014 post-acceptance amendment 2 was attempted.
Changes are unstaged and uncommitted (velox/ch/ only: 3 files). velox trunk and
the pre-existing 018b TpchQueryBuilder.cpp are untouched; no ClickHouse source,
no Gluten file changed.
The worker stopped after writing this receipt.
```

## Controller review — amendment 2（CACHED readUntil refill bug）

```text
controller_status: accepted
environment_profile: home-chang
task: 014 (amendment 2)
```

## Review evidence

```text
scope review: 改动限于 FileCacheInputStream.{h,cpp} + tests/FileCacheE2ETest.cpp（velox/ch/）。
  TpchQueryBuilder.cpp 018b 未提交补丁保留未动。velox 主干零改动（本 amendment）。
implementation review: worker 采用比契约"改一行 bound"更正确的修法（Controller 复核认可）：
  - 保留 setReadUntilPosition(downloadedSize)（不过读磁盘实际字节——注释点明 wrapped ReadFile
    在 short pread 上抛、无隐式 EOF clamp，故仅 bound 到段长会读过文件尾，验证了契约的过读警告）；
  - 新增 cachedPrefixEndAbsolute = range.left + downloadedSize（DOWNLOADING 段，:505），
    updateReadStateIfNeeded 在 offset 到达该 prefix 末尾时 re-prepare（:725-731），开覆盖已增长
    缓存文件的新 reader，避免冻结在首 chunk 报 premature end。等价对齐 CH nextImplStep 的
    边下边读续读（CH 靠 min(range.right+1,file_size_)+DOWNLOADING 续读）。保留 CH 原
    offset>=getCurrentWriteOffset 触发。DOWNLOADED 段 cachedPrefixEndAbsolute==0 → 无需 re-prepare。
log and test review: Controller 独立复现 RED——中和续读（prefixExhausted 恒 false），新测试
  CachedReaderRefillsWhenDownloadingSegmentGrows FAILED @:888（冻结重现）；还原→e2e 21/21、
  buffered_input 19、core_scc 47 全绿；grep 无残留 probe。新测试精确造根因场景（downloader 只下
  1 MiB、reader 读到 1 MiB 再续读）。
END-TO-END（决定性，Controller 亲跑 release 二进制，非转述）：重编 release
  velox_ch_filecache_tpch_ab_benchmark，重跑 6 个曾崩 query，全 exit 0、error 列空、行数正确：
  q2=12382459, q11=3180087, q15=45334136, q17=600036861, q20=967965, q21=39950。
  => filecache 引擎全 22 通过（refill bug 已解，两 bug 修毕）。
unresolved findings: none.
```

## Commits (amendment 2 — refill)

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `006a15996` |
| `/home/chang/SourceCode/ClickHouse` | (this acceptance commit) |
