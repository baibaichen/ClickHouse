# Task 014 Result: `FileCacheBufferedInput` and `FileCacheInputStream`

## Status

status: success (self-review fix wave applied; see "Self-review" section)

## Velox status

```text
branch filecache, HEAD bbda44d2531af0235851bc069fd2d583762d8d96 (unchanged; nothing committed)

git status --short:
 M velox/ch/CMakeLists.txt
?? velox/ch/Disks/
```

## Gluten status

```text
git -C /root/oss/gluten status --short: 20 pre-existing dirty files, all dated
2026-07-14 (six days before this task's session). Task 014 created ZERO Gluten
changes (all edits were under /root/oss/velox/velox/ch/Disks/).
```

## Files changed

```text
Modified:
  velox/ch/CMakeLists.txt                                  (add_subdirectory(Disks))
Created:
  velox/ch/Disks/CMakeLists.txt
  velox/ch/Disks/IO/CMakeLists.txt                         (velox_ch_filecache_dwio)
  velox/ch/Disks/IO/FileCacheRequestContext.h
  velox/ch/Disks/IO/FileCacheFileIdentity.h
  velox/ch/Disks/IO/FileCacheBufferedInput.h
  velox/ch/Disks/IO/FileCacheBufferedInput.cpp
  velox/ch/Disks/IO/FileCacheInputStream.h
  velox/ch/Disks/IO/FileCacheInputStream.cpp
  velox/ch/Disks/IO/tests/CMakeLists.txt
  velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp
No Gluten files. No ClickHouse source files.
```

## Commands run

```text
# Environment
source /root/oss/velox-helper/env.sh

# Configure (mono, _build/debug)
/usr/bin/cmake -S /root/oss/velox -B /root/oss/velox/_build/debug
  > _build/debug/configure_task_014_buffered_input.log 2>&1

# RED build (test + CMake present; production implementations absent -> link failure)
/usr/local/bin/ninja -C _build/debug velox_ch_filecache_buffered_input_test
  > _build/debug/build_task_014_red.log 2>&1   # failed (undefined references)

# GREEN build + focused test (mono)
/usr/local/bin/ninja -C _build/debug velox_ch_filecache_buffered_input_test
  > _build/debug/build_task_014_buffered_input.log 2>&1
ctest --test-dir _build/debug -R '^velox_ch_filecache_buffered_input_test$' --output-on-failure
  > _build/debug/test_task_014_buffered_input.log 2>&1

# Accumulated velox_ch_ regression (mono): 13 registered velox_ch_ tests
ctest --test-dir _build/debug -R '^velox_ch_' --output-on-failure
  > _build/debug/test_task_014_accumulated.log 2>&1

# Non-mono build + focused test (_build/debug-task012-nonmono, VELOX_MONO_LIBRARY=OFF)
/usr/bin/cmake -S /root/oss/velox -B _build/debug-task012-nonmono
  > _build/debug-task012-nonmono/configure_task_014_nonmono.log 2>&1
/usr/local/bin/ninja -C _build/debug-task012-nonmono velox_ch_filecache_buffered_input_test
  > _build/debug-task012-nonmono/build_task_014_nonmono.log 2>&1
ctest --test-dir _build/debug-task012-nonmono -R '^velox_ch_filecache_buffered_input_test$' --output-on-failure
  > _build/debug-task012-nonmono/test_task_014_nonmono.log 2>&1

# False-green mutation suite (9 material mutations)
bash run_all_mutations.sh   # log: _build/debug/mutation_task_014.log

# Verification
git -C /root/oss/velox diff --check
git -C /root/oss/velox status --short
git -C /root/oss/gluten status --short
```

## Generated logs

```text
/root/oss/velox/_build/debug/configure_task_014_buffered_input.log
/root/oss/velox/_build/debug/build_task_014_red.log
/root/oss/velox/_build/debug/build_task_014_buffered_input.log
/root/oss/velox/_build/debug/test_task_014_buffered_input.log
/root/oss/velox/_build/debug/test_task_014_accumulated.log
/root/oss/velox/_build/debug/mutation_task_014.log
/root/oss/velox/_build/debug-task012-nonmono/configure_task_014_nonmono.log
/root/oss/velox/_build/debug-task012-nonmono/build_task_014_nonmono.log
/root/oss/velox/_build/debug-task012-nonmono/test_task_014_nonmono.log
```

## Verification

```text
Red build failed because the production implementations were absent
  (build_task_014_red.log: undefined references to
   FileCacheBufferedInput::FileCacheBufferedInput(...) and
   FileCacheInputStream::isRemoteTruncationConfirmed(...); the test compiled).
Final build exit code: 0 (mono and non-mono).
Focused test result: 100% tests passed, 19 tests, 0 failed, 0 disabled, 0 skipped
  (mono and non-mono).
Accumulated velox_ch_ mono regression: 100% tests passed, 13/13.
False-green mutations: 9 material mutations, 9/9 caught, restored, final green.
git diff --check result: clean (exit 0), no whitespace errors; only the 11
  task-owned files changed.
Gluten repository status: 20 pre-existing dirty files, none touched by Task 014.
```

## ClickHouse reader-test migration matrix

```text
CH gtest_filecache.cpp case                       -> Velox destination
--------------------------------------------------------------------------------
CachedReadBuffer (basic read + LRU queue order)   -> MissThenHit (miss fills cache;
                                                     second stream reads cache, no
                                                     source read)
CachedReadBufferTruncatedObjectPredownload        -> TruncatedObjectPredownloadMetadataAbsent
  (predownload gap, EOF, release+withdraw)           (predownload EOF -> throw; segment
                                                      PARTIALLY_DOWNLOADED_NO_CONTINUATION;
                                                      reader withdrawn) + the
                                                      isRemoteTruncationConfirmed helper
                                                      asserted for BOTH metadata-present
                                                      (size==offset -> true) and absent
                                                      (nullopt -> false) branches
CachedReadBufferSourceFailure (nextImpl path)     -> SourceFailureReleasesDownloader
  (source fails; downloader released; reader          (Next throws; getRemoteFileReader
   withdrawn; healthy reader recovers)                empty after; healthy reader reads
                                                      end to end)
CachedReadBufferReadBigAtSourceFailure            -> EXCLUDED (readBigAt is not ported;
                                                      per design 03 line 588 it is "not
                                                      directly migrated"). Its observable
                                                      (source failure releases downloader +
                                                      withdraws reader, segment stays usable)
                                                      is covered by SourceFailureReleasesDownloader
                                                      through the Next path.
CachedReadBufferTruncatedObject (nextImpl EOF)    -> covered by the zero-byte-before-region-end
                                                      path in readFromCurrentSegment (metadata
                                                      absent -> clean throw); the predownload
                                                      variant is the migrated e2e case above.
CachedReadBufferTruncatedObjectReadBigAt          -> EXCLUDED (readBigAt not ported; Task 015
                                                      owns whole-system scenarios).
CachedReadBufferReadDuringExceptionUnwinding      -> EXCLUDED. Relies on
                                                      std::uncaught_exceptions() accounting for a
                                                      cached read issued from a destructor during
                                                      an unrelated unwind; not part of the MVP
                                                      reader state machine. Task 015 owns
                                                      whole-system unwinding scenarios.
reader seek/handoff/concurrent-reader behavior    -> SeekWithinBufferIsCheap, SeekOutOfBufferRebuilds
                                                      (region-relative seek), ReaderHandoffQ1Q2
                                                      (Q1 partial download -> detach -> Q2 reuse
                                                      the segment's remote reader from
                                                      currentWriteOffset), CachedReaderSeesGrownSegment
                                                      (CACHED reader on a prefix must see a
                                                      concurrently-completed segment).
```

## Behavioral invariants verified

```text
[x] lazy Next: load never dereferences stream (EnqueueResultDiscardedBeforeLoad)
[x] region-relative ByteCount / seek (RegionRelativeCoordinates: ByteCount==20 for a
    [40,60) region; segment created at absolute offset 40)
[x] absolute FileCache / ReadFile offsets (RegionRelativeCoordinates)
[x] checkedAdd overflow for region.offset + length (RegionOverflowRejected)
[x] shouldPrefetchStripes = false, preloaded = false, shouldPreload = false,
    hasCache = false, executor returns injected executor (DwioContractValues)
[x] isBuffered uses no-create get (IsBufferedNoCreateProbe: false + no metadata +
    no source read)
[x] cache miss fills cache; cache hit reads cache with no source read (MissThenHit)
[x] bypass threshold: large read reads source, creates no metadata (BypassThreshold)
[x] reader attach/detach/handoff: Q1 writes one chunk, detaches reader, Q2 reuses it
    from currentWriteOffset (ReaderHandoffQ1Q2; proven by Q2's own source never read)
[x] canceled/failed reader never returned to FileSegment (SourceFailureReleasesDownloader;
    getRemoteFileReader empty after; false-green M2)
[x] in-buffer seek is O(1), keeps holder/downloader (SeekWithinBufferIsCheap; bypass so a
    slow-path rebuild would re-read the source; false-green M3)
[x] out-of-buffer seek releases state, keeps queryContextHolder (SeekOutOfBufferRebuilds,
    QueryContextLifetime; false-green M5)
[x] queryContextHolder acquired in ctor, held to dtor, never reset by seek
    (QueryContextLifetime: weak_ptr not expired across an out-of-buffer seek, expired only
    on destruction)
[x] source failure releases downloader + withdraws reader; healthy reader recovers
    (SourceFailureReleasesDownloader)
[x] skip_cache_on_disk_failure bypasses cache-write failure but preserves the source read;
    otherwise the failure propagates and the downloader is released
    (DiskFailureSkipBypasses, DiskFailurePropagatesWithoutSkip; false-green M4)
[x] getRemoteFileMetadata()==nullopt boundary: no fabricated size; both metadata-present and
    absent decision branches covered (TruncatedObjectPredownloadMetadataAbsent; false-green M6/M7)
[x] direct IO: aligned buffers work; misaligned external buffer rejected (DirectIoAlignment)
[x] empty etag -> fromPath key; non-empty etags -> SipHash key; different etags produce
    different keys and different cache entries (PathAndEtagKeyDerivation)
[x] downloader cleanup on destruction (DownloaderCleanupOnDestruction)
[x] CACHED reader tracks a concurrently-grown/completed segment (CachedReaderSeesGrownSegment;
    self-review Issue 1 fix; false-green M9)
```

## Self-review

```text
One read-only code-review agent audited the full diff against the CH source of truth and the
12 handoff/coordinate/lifetime invariants. Two genuine findings, both resolved:

HIGH  - Stale CACHED reader read bound: a CACHED reader opened on a partial prefix kept the
        cache-file size frozen at open time (ReadBufferFromVeloxReadFile freezes readUntil_),
        so a segment grown/completed by a concurrent downloader produced a spurious
        "cannot read all data" at the stale boundary. Fixed: CACHED reads are bounded by the
        live FileSegment::getDownloadedSize() (local coordinates), and updateReadStateIfNeeded
        re-prepares the CACHED reader on every chunk (refreshing the bound and re-opening a
        renamed completed segment). Added regression test CachedReaderSeesGrownSegment and
        false-green mutation M9 (reverting the fix reproduces the reviewer's exact error).

MEDIUM- Direct-IO predownload scratch buffer was allocated with only 64-byte AlignedBuffer
        alignment, violating a larger direct-IO alignment on set(). Fixed: predownload now
        reuses the already-aligned output buffer as scratch (CH shares its internal buffer for
        predownload when large enough); the unused ReadFromFileSegmentState::predownloadBuffer
        member was removed.

All gates rerun green after the fixes: mono focused 19/19, non-mono 1/1, accumulated 13/13,
9/9 material false-green mutations caught.
```

## Cross-task note flagged for the mandatory Tasks 003-014 review

```text
Background download of a partially-downloaded segment via a handed-off reader is disabled in
the MVP read path (test config backgroundDownloadThreads=0). The handoff-retained reader carries
a Velox-owned internal buffer (Task 007 ReadBufferFromVeloxReadFile::set(nullptr, 0) restores the
owned window), which is incompatible with the Task 012 background-download debug assertion
CacheMetadata::downloadImpl `buf->internalBuffer().empty()` (that reflects CH's set(nullptr, 0) ->
empty-internal-buffer semantics). This is a Task 007/012 vs Task 014 interaction first exercised
here; productizing background download with the Velox reader is deferred and flagged for the review.
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Mandatory whole-port source-contract review of Tasks 003-014 (per the amended Task 014
review checkpoint). Task 015 (Velox-only FileCache E2E + random-seek benchmark) may start
only with zero unresolved findings and explicit user approval.
```

---

## Corrective wave (senior-review findings)

```text
state: corrective changes applied after the Task-014 senior review
changes are unstaged/uncommitted
build env: source /root/oss/velox-helper/env.sh (no -j / no nproc)
mono:     /root/oss/velox/_build/debug
non-mono: /root/oss/velox/_build/debug-task012-nonmono (VELOX_MONO_LIBRARY=OFF)
```

### Findings resolved

```text
Fix 1 (background handoff blocker) — Task 007 reopened. set(nullptr,0) now
  detaches to an empty internal buffer (was restoreOwnedWindow); the owned window
  is restored lazily on a later normal read. The Task 012 worker precondition
  chassert(buf->internalBuffer().empty()) now holds. See the Task-007 receipt
  corrective audit. Background download with the Velox reader is now productized;
  makeConfig defaults backgroundDownloadThreads to 0 only for tests that do not
  need it.

Fix 2 (background integration) — new production-path tests with
  backgroundDownloadThreads > 0. Also surfaced and fixed a latent use-after-free:
  a handed-off remote reader retained an owned buffer charged to the query-scoped
  MemoryPool; a background worker that outlives the query could free it against a
  dead pool (velox Buffer holds a raw MemoryPool* and frees in its destructor).
  Fix: FileCacheInputStream hands off the reader with
  ReadBufferFromVeloxReadFile::releaseOwnedBuffer() (owned buffer is never used
  for I/O by a handed-off reader — reads always target an external buffer).

Fix 3 (direct-IO predownload) — createReadFromFileSegmentState skips the optional
  predownload optimization when a direct-IO source cannot satisfy the alignment of
  the gap [currentWriteOffset, offset) (currentWriteOffset or bytesToPredownload
  not an alignment-multiple). It releases the elected downloader and reads the
  segment via the normal aligned bypass path at `offset`. No buffered-IO fallback,
  no fabricated size, no unaligned direct-IO seek/read.

Fix 4a — FileCacheBufferedInput::isBuffered computes the range end with
  FileCacheUtils::checkedAdd(offset, length, ...) - 1 up front, rejecting an
  overflowing offset+length before any wrap-prone arithmetic.

Fix 4b — new multi-segment disk-failure test: a mid-region cache-write failure is
  skipped and the read continues, caching later segments.

Fix 4c — the FileCacheInputStream/owner (FileCacheBufferedInput) lifetime
  requirement is documented in the public headers, plus the handed-off-reader pool
  contract. No ownership redesign beyond the code-safety releaseOwnedBuffer.
```

### Tests added (velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp)

```text
BackgroundDownloadCompletesHandedOffSegment  (fix 2; backgroundDownloadThreads=2;
  spinUntil, no sleeps; asserts DOWNLOADED, source1 preadBytes==40 no duplicate,
  fresh query reads all-from-cache => no leak/stale pointer)
BackgroundHandoffReleasesQueryPoolMemory     (fix 2 pool safety; distinct query
  pool; queryPool.usedBytes()==0 after handoff; query pool destroyed before the
  background download completes)
DirectIoPredownloadSkipsWhenUnaligned        (fix 3)
IsBufferedRejectsOverflow                    (fix 4a; VELOX_ASSERT_THROW message match)
DiskFailureSkipContinuesAcrossSegments       (fix 4b)
(makeInput gained an optional readerPool param; a spinUntil bounded-wait helper
 was added.)
```

### Acceptance evidence

```text
velox_ch_filecache_buffered_input_test: 24/24 passed, 0 failed, 0 disabled,
  0 skipped (mono AND non-mono). (19 before this wave; +5.)
velox_ch_io_test (Task 007):            33/33 (mono AND non-mono).
velox_ch_filecache_core_scc_test (012): 101/101 (mono).
velox_ch_filecache_manager_test (013):  42/42 (mono).
accumulated velox_ch_ mono (ctest -R ^velox_ch_): 13/13.
Background tests re-run 12-15x each: no flakiness.

false-green mutations (build + run + restore byte-for-byte; all CAUGHT):
  M2 always-withdraw-reader        => BackgroundDownloadCompletesHandedOffSegment FAILED
  M3 remove direct-IO skip         => DirectIoPredownloadSkipsWhenUnaligned FAILED
  M4 checkedAdd -> raw offset+len  => IsBufferedRejectsOverflow FAILED
  M5 remove skip_cache_on_disk...  => DiskFailureSkipContinuesAcrossSegments FAILED
  M6 remove releaseOwnedBuffer     => BackgroundHandoffReleasesQueryPoolMemory SIGSEGV (real UAF)
  (M1 restoreOwnedWindow => Task-007 ReaderSetNullDetaches FAILED)

git diff --check: clean (tracked + untracked Disks scan). Gluten: untouched
  (20 pre-existing dirty files).

logs (build dirs):
  _build/debug/build_sr_all_bufinput.log, build_sr_pool_fix.log
  _build/debug/test_sr_final2_mono.log, test_sr_accumulated_mono.log
  _build/debug/mut2_M[2-6]_*_{build,test}.log
  _build/debug-task012-nonmono/build_sr_nonmono2.log, test_sr_nonmono.log
```

### Files changed (Task-014-owned, unstaged)

```text
velox/ch/Disks/IO/FileCacheInputStream.cpp   (fix 3 skip guard; fix 2 releaseOwnedBuffer handoff)
velox/ch/Disks/IO/FileCacheInputStream.h     (fix 4c owner-lifetime docs)
velox/ch/Disks/IO/FileCacheBufferedInput.cpp (fix 4a checkedAdd + FileCacheUtils include)
velox/ch/Disks/IO/FileCacheBufferedInput.h   (fix 4c lifetime + handed-off-reader pool docs)
velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp (fix 2/3/4a/4b tests; makeInput pool param)
Task-007-owned (see 007 receipt): ReadBufferFromVeloxReadFile.{h,cpp}, IoAdaptersTest.cpp
```

## Controller review 1

```text
controller_status: accepted
environment_profile: root-oss
```

Scope:

- Inspected all Task-014 production/test files and CMake wiring.
- Traced hit/miss, handoff, downloader lifetime, seek, source/disk failure,
  checked arithmetic, query context, Manager services, and background
  continuation.
- Confirmed no Task 015 or Gluten change.

Review iterations:

```text
worker self-review:
  stale cached-read bound fixed
  direct-IO scratch alignment fixed

senior review:
  background handoff abort found across Tasks 007/012/014
  direct-IO predownload and overflow evidence gaps found

corrective review:
  Task-007 detach semantics restored
  query-pool buffer UAF found and fixed
  background continuation, direct-IO skip, overflow, and multi-segment bypass tested
  Blocker/Major findings: 0
```

Controller final evidence:

```text
mono:
  velox_ch_io_test: 33/33
  velox_ch_filecache_core_scc_test: 101/101
  velox_ch_filecache_manager_test: 42/42
  velox_ch_filecache_buffered_input_test: 24/24
  accumulated CTest: 13/13

non-mono:
  VELOX_MONO_LIBRARY=OFF
  velox_ch_io_test: 33/33
  velox_ch_filecache_buffered_input_test: 24/24
  focused CTest: 2/2

failed/skipped/disabled:
  0/0/0

git diff --check:
  clean
```

Accepted behavior:

- successful handoff detaches caller views and releases query-pool-owned memory;
- background download continues with its own external MemoryPool buffer;
- canceled/failed readers are never returned;
- direct-IO predownload skips only an unaligned optional optimization and never
  falls back to buffered I/O;
- checked region arithmetic rejects overflow;
- multi-segment disk-failure bypass continues source reads and later caching;
- query context survives seek and releases after stream state.

The direct-IO-source plus background-download combination remains a low-risk
follow-up test for Task 015; remote production sources report alignment 1.

Independent final review:

```text
spec compliance: approved
technical quality: approved
Blocker/Major findings: 0
```

Accepted Velox commit:

```text
b92a0ae3a Task 014: Add `FileCache` buffered input
```

Task 014 is accepted. Task 015 remains prohibited until the mandatory
Tasks 003-014 full review reaches zero unresolved findings and the user
explicitly approves.
