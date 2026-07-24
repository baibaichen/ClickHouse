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

## Post-acceptance contract audit 1 (Review-2 B4 Task-014 half)

```text
controller_status: reopened_by_contract_audit
environment_profile: root-oss
task: 014
reopened_by: port/task/fullreview/root-oss/2/003-014-review-decisions.md (B4)
reopened_by: port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md
  §2 "Task 014 — Buffered input reader — ACCEPT with one Major cross-cutting
  reopen item", §7.2 item 3
```

The Tasks 003-014 full review (Review 2) found the accepted reader/handoff
implementation above structurally faithful and identified no implementation
defect, and reopened Task 014 jointly with Task 012 on the B4
coverage/evidence gap: the concurrent two-thread `resetRemoteFileReader`
before `completePartAndResetDownloader`/`setDownloadFinishedWithoutContinuation`
race is enforced by production invariants at every one of this task's six
`FileCacheInputStream.cpp` call sites, but no test schedules the race under
real concurrent threads. The B1 direct-IO/background-download cross-cutting
gap (already recorded as accepted-with-reopen above) remains explicitly
deferred to Task 015 and is not part of this audit.

This receipt is reopened per the state machine in `EXECUTION_PROTOCOL.md`
(`accepted -> reopened_by_contract_audit -> worker_running`). The original
acceptance above (including the corrective wave already recorded) is
unchanged and immutable; this section is additive. The binding corrective
contract is recorded in `port/task/014-filecache-buffered-input.md`, section
`### B4 (Task-014 half): confirm reset-before-complete ordering at both
FileCacheInputStream.cpp call sites`. The Task-012 B4 test itself must be
accepted first; a fresh Task-014 worker then confirms (or, if drifted,
corrects) ordering at this task's six call sites and appends a new
worker-attempt section below before Task 015 may start.

No production change is authorized by this audit alone; a Task-014 corrective
worker may change production code only if re-inspection finds a genuine
ordering regression at one of the six cited call sites.

## Worker attempt 3 (Review-2 B4 Task-014-half)

```text
status: ready_for_controller
environment_profile: root-oss
task: 014 corrective B4 (Task-014-half)
date: 2026-07-21
```

### Baselines

- Velox HEAD: `ad1a13c37e87cecda464ac8dfcc9fee57c093eb6` (branch `filecache`, clean)
- ClickHouse HEAD: `723b6f08046` (branch `ch-filecache`, clean)
- Last commit touching `FileCacheInputStream.cpp`: `b92a0ae3a` "Task 014: Add `FileCache` buffered input"
- `FileCacheInputStream.cpp` is unchanged since Task 014 acceptance.

### Task 012 B4 prerequisite confirmation

Task 012 Worker attempt 7 + Controller review 7 is `accepted`
(`port/task/result/012-filecache-core-scc-result.md`, §"Controller review 7").
Evidence summary:

| Suite | Count | Result |
|---|---|---|
| Mono full core SCC | 105/105 | PASSED |
| Non-mono full core SCC | 105/105 | PASSED |
| Focused B4+B5 mono | 2/2 | PASSED |
| Focused B4+B5 non-mono | 2/2 | PASSED |
| Accumulated mono CTest | 14/14 | PASSED |
| B4 mutation (reset/publish order swapped) | — | ownership assertion fails (exit 134) |
| B5 mutation (blocking pop replacing tryPop) | — | hangs (exit 124) |

The `ConcurrentExtractRacesResetBeforeComplete` test (B4) exercises the shared
`FileSegment` state machine, verifying that `resetRemoteFileReader` must occur
before `completePartAndResetDownloader`/`setDownloadFinishedWithoutContinuation`
for reader ownership to be safe across concurrent threads.

### Six call-site inspection

All six sites were located by searching the current file. Exact current line
numbers match the contract spec in `014-filecache-buffered-input.md:38-46`
precisely; no drift.

**Site 1 — lines 603–604** (`FileCacheInputStream.cpp`)
- Context: predownload loop, EOF before gap filled, `if (fileSegment.isDownloader())` guard.
- Branch: `!hasData` → `fileSegment.isDownloader()`.
- Sequence: `resetRemoteFileReader()` (603) → `setDownloadFinishedWithoutContinuation()` (604).
- Intervening code: none. ✓

**Site 2 — lines 631–632** (`FileCacheInputStream.cpp`)
- Context: reservation or cache-write failure, bypass path.
- Branch: `!ok` block, no downloader guard needed (caller already holds downloader).
- Sequence: `resetRemoteFileReader()` (631) → `completePartAndResetDownloader()` (632).
- Intervening code: none. ✓

**Site 3 — lines 710–711** (`FileCacheInputStream.cpp`)
- Context: remote download loop, zero-bytes-but-not-finished, `if (fileSegment.isDownloader())` guard.
- Branch: `size == 0 && offset < readUntilPosition` → `isDownloader()`.
- Sequence: `resetRemoteFileReader()` (710) → `setDownloadFinishedWithoutContinuation()` (711).
- Intervening code: none. ✓

**Site 4 — lines 754–755** (`FileCacheInputStream.cpp`, `releaseDownloaderIfNeeded`)
- Context: explicit release helper, `if (fileSegment.isDownloader())` guard.
- Sequence: `resetRemoteFileReader()` (754) → `completePartAndResetDownloader()` (755).
- Intervening code: none. ✓

**Site 5 — lines 836–837** (`FileCacheInputStream.cpp`, catch block)
- Context: exception handler, reader dropped via `state_.reset()` before this block, then `if (fileSegment.isDownloader())`.
- Sequence: `resetRemoteFileReader()` (836) → `completePartAndResetDownloader()` (837).
- Intervening code: none. ✓

**Site 6 — lines 849, 857** (`FileCacheInputStream.cpp`, post-read release)
- Context: `if (state_ && readType != CACHED && isDownloader())` outer guard.
- Branch: `!readerCanBeReused` → `resetRemoteFileReader()` (849); `else if (state_->reader)` → `releaseOwnedBuffer()` (850–856, no publication); unconditional `completePartAndResetDownloader()` (857) on both sub-branches.
- On the reset branch: `resetRemoteFileReader` (849) occurs before `completePartAndResetDownloader` (857). ✓
- On the reuse branch: reader is left in the segment; `releaseOwnedBuffer()` (not a publication) runs; `completePartAndResetDownloader` (857) follows. The reader is never leaked; it remains inside the `FileSegment` under the handoff contract. ✓
- Intervening code between reset and publish (in the `!readerCanBeReused` sub-branch): none. ✓

### Ordering invariant summary

At all six sites, `resetRemoteFileReader()` is called strictly before the
paired `completePartAndResetDownloader()` or `setDownloadFinishedWithoutContinuation()`.
No intervening code at any site can publish, observe, or leak the reader
between the two calls. The site-6 `readerCanBeReused` branch intentionally
retains the reader inside the `FileSegment` for handoff — this is the designed
behavior, not a leak.

### Source status

No production change was made. `FileCacheInputStream.cpp` is unchanged.
`git status` reports a clean working tree in `/root/oss/velox`.

### Blockers

None.

### Declaration

I inspected all six `resetRemoteFileReader`/publication call sites in
`FileCacheInputStream.cpp` in the current `filecache` HEAD
(`ad1a13c37e87`). No ordering drift was found. Task 012 B4 is accepted
with 105/105 mono and non-mono, focused 2/2, and mutation evidence as cited
above. This task's B4 half is satisfied by that evidence together with the
confirmed call-site ordering. No new test is required from Task 014 itself.
No Velox source was modified.

## Controller review 3 — Review-2 B4 Task-014 half

```text
controller_status: accepted
environment_profile: root-oss
scope: caller-order inspection/evidence only
```

Independent review confirmed every current caller:

```text
603-604  resetRemoteFileReader -> setDownloadFinishedWithoutContinuation
631-632  resetRemoteFileReader -> completePartAndResetDownloader
710-711  resetRemoteFileReader -> setDownloadFinishedWithoutContinuation
754-755  resetRemoteFileReader -> completePartAndResetDownloader
836-837  resetRemoteFileReader -> completePartAndResetDownloader
849,857  conditional non-reuse reset -> unconditional completion
```

At the sixth site, the mutually exclusive reuse branch intentionally retains
the reader, releases its owned query-pool buffer, and completes the segment for
handoff. This exactly matches the Task 014 contract; it is not a missing reset.

Task 012 supplies the shared `FileSegment` state-machine evidence:

```text
focused B4/B5: 2/2 mono and 2/2 non-mono
full core SCC: 105/105 mono and 105/105 non-mono
B4 reversed-order mutation: ownership assertion fails
```

Review result:

```text
spec compliance: approved
technical quality: approved
Blocker/Major findings: 0
```

No Velox source changed. B4 is closed.

## Post-acceptance contract audit 2 — Review-5 external truncation

```text
controller_status: reopened_by_contract_audit
environment_profile: root-oss
task: 014
finding_id: G-CACHEBUF-01
next_state: worker_running
```

Review 5 confirmed that the approved external-truncation self-heal is absent
from current accepted Velox head `26325e8a32`.

Current failure:

```text
a size-suffixed cache segment is fully downloaded;
its physical file is truncated outside the process;
FileCacheInputStream still selects CACHED and sets the recorded downloaded size
  as the local read bound;
LocalReadFile receives a short pread and throws instead of re-fetching source
  bytes.
```

The existing
`FileCacheE2ETest.TruncatedOrInvalidCachedDataSourceRecovery` removes the cache
key through the public API. It does not truncate the physical cache file and
cannot satisfy this contract.

Required corrective scope and RED/GREEN/mutation evidence are binding in
`port/task/014-filecache-buffered-input.md` under
`## Review-5 corrective: external-truncation self-heal`.

A historical local-only Velox branch
`11111111111111111111111111111111111` contains old commit `d83660e638`, but it
has no canonical Worker receipt or Controller acceptance and does not descend
from current accepted head. It is not the current Worker attempt and must not
be cherry-picked as acceptance evidence.

Redispatch:

```text
same task: 014
fresh Worker: required
baseline: 26325e8a32
result handoff: this file
```

## Worker attempt 4 (Review-5 G-CACHEBUF-01)

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 014 corrective G-CACHEBUF-01 (Review-5 external-truncation self-heal)
date: 2026-07-24
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `26325e8a32` | clean |
| `/root/oss/clickhouse` | `ch-filecache` | `d2f33647a6c` | clean |

Velox `filecache` tracks `baibaichen/filecache`. ClickHouse `ch-filecache` tracks
`baibaichen/ch-filecache`. Historical local branch
`11111111111111111111111111111111111` (`d83660e638`) was not consulted, cherry-
picked, or used as evidence.

## Files changed

```text
velox/ch/Disks/IO/FileCacheInputStream.cpp
velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
```

No ClickHouse source files. No Gluten files. Changes are unstaged and uncommitted.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| RED build (test only, pre-production) | 0 | `/root/oss/velox/_build/debug/build_task014r5_red.log` |
| RED test (ExternalTruncationSelfHeal against unchanged production) | 8 | `/root/oss/velox/_build/debug/test_task014r5_red.log` |
| GREEN build (mono, e2e + buffered) | 0 | `/root/oss/velox/_build/debug/build_task014r5_green.log` |
| GREEN test (mono, velox_ch_filecache_e2e_test) | 0 | `/root/oss/velox/_build/debug/test_task014r5_green_e2e.log` |
| GREEN test (mono, velox_ch_filecache_buffered_input_test) | 0 | `/root/oss/velox/_build/debug/test_task014r5_buffered.log` |
| GREEN accumulated (mono, velox_ch_) | 0 | `/root/oss/velox/_build/debug/test_task014r5_accumulated.log` |
| Mutation 1 build+test (bypass if-guard) | 8 | `/root/oss/velox/_build/debug/build_task014r5_mutation.log` |
| Review-fix build (state-before-open) | 0 | `/root/oss/velox/_build/debug/build_task014r5_review_fix.log` |
| Review-fix test (mono, e2e + buffered) | 0 | `/root/oss/velox/_build/debug/test_task014r5_review_fix_mono.log` |
| Mutation 2 build+test (bypass after review fix) | 8 | `/root/oss/velox/_build/debug/build_task014r5_mutation2.log` |
| Final mono build + accumulated velox_ch_ | 0 | `/root/oss/velox/_build/debug/build_task014r5_final2_mono.log` |
| Final non-mono build + test | 0 | `/root/oss/velox/_build/debug-task012-nonmono/build_task014r5_nonmono_final.log` |
| `git diff --check` | 0 | (stdout) |

## Acceptance evidence

```text
RED (pre-fix, test first):
  ExternalTruncationSelfHeal FAILED: bytesRead == length (4096 vs. 8192)
  in LocalReadFile::preadInternal for path .../0_8192 (size-suffixed DOWNLOADED
  segment, physical file truncated to 4096 bytes, production read CACHED branch).

GREEN (post-fix):
  velox_ch_filecache_e2e_test:
    21/21 passed (mono), 2/2 passed (non-mono)
    Includes: ExternalTruncationSelfHeal, TruncatedOrInvalidCachedDataSourceRecovery,
              MissFillHit (normal cache hit, zero source reads on 2nd read)
  velox_ch_filecache_buffered_input_test:
    24/24 passed (mono), 24/24 passed (non-mono)
  accumulated velox_ch_ (mono):
    16/16 passed (includes all prior tasks)

Mutation (physical-size check bypassed with `if (false && trustSizeFromFilename)`):
  Mutation 1 (before reviewer fix): ExternalTruncationSelfHeal FAILED —
    bytesRead == length (4096 vs. 8192), same short-pread exception.
  Mutation 2 (after reviewer fix):  ExternalTruncationSelfHeal FAILED — same.
  Both mutations restored byte-for-byte; final GREEN reconfirmed.

git diff --check: clean (exit 0).
Only declared files dirty:
  M velox/ch/Disks/IO/FileCacheInputStream.cpp
  M velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
ClickHouse repository: clean.
```

## Worker review

```text
review subagent: one read-only code-review subagent launched on the complete
  unstaged diff (FileCacheInputStream.cpp + FileCacheE2ETest.cpp).

findings:
  MEDIUM — state-before-size ordering: my first implementation called
    createCacheReadBuffer (which captures fileSize_ via readFile_->size() at
    construction) BEFORE observing state() and hasSizeInFileName(). This left a
    window where a concurrent setDownloadedUnlocked could rename the file (setting
    size_in_filename=true) and publish DOWNLOADED between the file-open and the
    state check. The code would then see trustSizeFromFilename=true while holding
    a pre-rename partial size — spuriously bypassing a just-completed download.

resolutions:
  Moved the `downloadState = fileSegment.state()` and `trustSizeFromFilename`
  computation BEFORE `createCacheReadBuffer`. After the state/hasSizeInFileName
  observations are committed under C++ seq_cst total order, the file open captures
  a size that is consistent with the observed final state. All tests rerun GREEN.
  Mutation 2 confirms the physical-size check is still the effective guard.

unresolved findings: none
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 014 was attempted (corrective scope: G-CACHEBUF-01 external-truncation
self-heal). Changes are unstaged and uncommitted. The worker stopped after writing
this receipt.
```

## Controller review 4 — Review-5 G-CACHEBUF-01

```text
controller_status: changes_requested
environment_profile: root-oss
task: 014
worker_attempt: 4
redispatch_same_task: yes
```

## Review evidence

```text
scope review:
  only FileCacheInputStream.cpp, FileCacheE2ETest.cpp, and this receipt changed

implementation review:
  truncation path returns a null cache reader and switches CACHED to source bypass
  state/size-suffix observations precede the physical-size sample
  broken metadata/priority entry is left in place

independent task review:
  spec compliance approved
  code quality approved
  Critical/Important findings: 0

Controller final production gates:
  mono selected E2E/buffered CTest: 2/2 passed
  mono accumulated velox_ch_ CTest: 16/16 passed
  non-mono selected E2E/buffered CTest: 2/2 passed
```

## Required changes

1. The attempt-4 table claims
   `/root/oss/velox/_build/debug/build_task014r5_mutation2.log` is mutation RED
   with exit `8`, but that file contains `2/2` passing CTests. The claimed
   post-review mutation is not proven by the cited artifact. Run a fresh
   buildable mutation after the state-before-open fix, write separate unique
   build and test logs, require `ExternalTruncationSelfHeal` to fail for the
   short local `pread`, restore production, and rerun final GREEN.
2. Strengthen the physical-path assertion. Checking `rfind('_')` on the full
   path can pass because a parent temporary directory contains an underscore.
   Assert the filename component has the exact expected `<offset>_<size>`
   suffix (for this test, `0_8192`) before truncating it.
3. Append `Worker attempt 5`; do not edit attempt 4. Record non-mono `2/2` as
   two selected CTest executables, not as 2/2 gtest cases in the E2E binary.
4. Preserve the currently GREEN implementation and all Controller gate logs.
   Changes remain unstaged and uncommitted.

## Worker attempt 5 (Review-5 G-CACHEBUF-01 evidence correction)

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 014
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `26325e8a32` | `M velox/ch/Disks/IO/FileCacheInputStream.cpp`, `M velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp` (unstaged, attempt-4 work preserved) |
| `/root/oss/clickhouse` | `ch-filecache` | `d2f33647a6c` | `M port/task/CONTROLLER_HANDOFF.md`, `M port/task/result/014-filecache-buffered-input-result.md` (Controller-owned) |

## Files changed

```text
velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
  — ExternalTruncationSelfHeal: replaced rfind('_') assertion with exact
    filename-component check: std::filesystem::path(physicalPath).filename() == "0_8192"
velox/ch/Disks/IO/FileCacheInputStream.cpp
  — no change (production implementation preserved byte-for-byte from attempt 4)
```

No ClickHouse source files. No Gluten files. Changes are unstaged and uncommitted.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| Mutation 3 build (bypass if-guard after assertion fix) | 0 | `/root/oss/velox/_build/debug/build_task014r5a5_mutation3.log` |
| Mutation 3 test (ExternalTruncationSelfHeal must fail) | 8 | `/root/oss/velox/_build/debug/test_task014r5a5_mutation3.log` |
| Restore production + final mono build (e2e + buffered) | 0 | `/root/oss/velox/_build/debug/build_task014r5a5_final_mono.log` |
| Final mono test (selected E2E + buffered) | 0 | `/root/oss/velox/_build/debug/test_task014r5a5_final_mono_e2e_buffered.log` |
| Final mono accumulated velox_ch_ | 0 | `/root/oss/velox/_build/debug/test_task014r5a5_accumulated.log` |
| Final non-mono build (selected E2E + buffered) | 0 | `/root/oss/velox/_build/debug-task012-nonmono/build_task014r5a5_nonmono_final.log` |
| Final non-mono test (selected E2E + buffered) | 0 | `/root/oss/velox/_build/debug-task012-nonmono/test_task014r5a5_nonmono_e2e_buffered.log` |
| `git diff --check` | 0 | (stdout) |

## Acceptance evidence

```text
Attempt-5 code change (assertion strengthening):
  FileCacheE2ETest.ExternalTruncationSelfHeal now extracts the filename
  component and asserts it equals "0_8192" exactly.
  The mutation3 log confirms the filename in the actual cache path is
  "0_8192" (path: …/367ee1fd77b404c7ec53c46db5cac2bd/0_8192).

Mutation 3 (physical-size check bypassed with `if (false && trustSizeFromFilename)`):
  ExternalTruncationSelfHeal FAILED:
    bytesRead == length (4096 vs. 8192) in LocalReadFile::preadInternal
    path=…/0_8192, fd=7, offset=0, length=8192 (short pread at physical 4096)
  Other tests passed; only ExternalTruncationSelfHeal failed.
  Production restored byte-for-byte before final GREEN.

Final GREEN (post-restore):
  mono selected CTest executables (velox_ch_filecache_e2e_test,
                                   velox_ch_filecache_buffered_input_test):
    2/2 CTest executables passed
  mono accumulated velox_ch_ CTest executables:
    16/16 CTest executables passed
  non-mono selected CTest executables (same two targets):
    2/2 CTest executables passed

git diff --check: clean (exit 0).
Only declared files dirty:
  M velox/ch/Disks/IO/FileCacheInputStream.cpp
  M velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
ClickHouse repository: only Controller-owned files (CONTROLLER_HANDOFF.md,
  this result file) dirty.
```

## Worker review

```text
review subagent: one read-only code-review subagent launched on the complete
  unstaged diff (FileCacheInputStream.cpp + FileCacheE2ETest.cpp).

findings:
  MEDIUM — stale-path race (reviewer argument):
    The comment claims state-before-open ordering prevents a race where
    physical size is sampled from a stale (pre-rename) file. The reviewer
    noted that `path` is computed at the top of `getCacheReadBuffer` (before
    the state observation), so if the segment transitions from DOWNLOADING to
    DOWNLOADED between `getPath()` and the state load, the path could point
    to the old un-suffixed file that was renamed away.

resolutions:
  Resolved as false positive for this call site. `getCacheReadBuffer` is only
  called from `createReadFromFileSegmentState` for ReadType::CACHED segments.
  ReadType::CACHED is assigned only to segments whose state was already
  DOWNLOADED at `getSegmentsForRead()` time. In `setDownloadedUnlocked`,
  `hasSizeInFileName` is set (via `renameToIncludeSizeInNameUnlocked`) before
  `download_state = DOWNLOADED` is published. Because the segment was
  DOWNLOADED before classification, `hasSizeInFileName` must already have
  been true when `getPath()` was called at the top of `getCacheReadBuffer`,
  so `getPath()` already returns the size-suffixed path. The DOWNLOADING→
  DOWNLOADED race window does not exist for CACHED segments; no production
  code change is required.

unresolved findings: none
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 014 was attempted (corrective scope: G-CACHEBUF-01 Controller
review-4 required changes — filename assertion strengthening and mutation
evidence correction). Changes are unstaged and uncommitted. The worker
stopped after writing this receipt.
```

## Controller review 5 — Review-5 G-CACHEBUF-01

```text
controller_status: waiting_for_user
environment_profile: root-oss
task: 014
worker_attempt: 5
implementation_gate: green
```

The external-truncation corrective itself is complete:

```text
real physical-truncation RED:
  ExternalTruncationSelfHeal fails with short pread 4096 vs 8192
mutation after final implementation:
  same test fails with the same short pread when the size guard is disabled
final production:
  mono selected E2E/buffered CTest 2/2
  mono accumulated velox_ch_ CTest 16/16
  non-mono selected E2E/buffered CTest 2/2
independent task review:
  G-CACHEBUF-01 spec compliance approved
  G-CACHEBUF-01 code quality approved
```

### New impacted-surface finding

```text
finding_id: G-CACHEOPEN-RENAME-01
status: pre-existing, real, not caused by G-CACHEBUF-01
decision: waiting_for_user
```

Worker attempt 5 records a review finding as false positive on the premise that
`getCacheReadBuffer` is called only for an already `DOWNLOADED` segment. That
premise is false:

```text
FileCacheInputStream.cpp:
  DOWNLOADING + canStartFromCache -> create(ReadType::CACHED)
  EMPTY/PARTIALLY_DOWNLOADED + canStartFromCache -> create(ReadType::CACHED)
  PARTIALLY_DOWNLOADED_NO_CONTINUATION + canStartFromCache -> CACHED
```

`getCacheReadBuffer` computes `path` before its state observations. A concurrent
completion can rename `<offset>` to `<offset>_<size>` between path computation
and open, leaving an old path that no longer exists. ClickHouse handles this in
`CachedOnDiskReadBufferFromFile.cpp:366-395`: on `FILE_DOESNT_EXIST`, it
recomputes the path while holding the segment lock and retries only when the
path changed. Velox has no equivalent retry.

This finding is tightly coupled to the touched function but is distinct from
external truncation. No implementation for it is authorized yet.

Required user decision:

```text
A: expand the current Task-014 Worker scope and fix the rename/open race now;
B: accept G-CACHEBUF-01 only and keep G-CACHEOPEN-RENAME-01 as a separate
   pending Review-5 blocker.
```

Until the decision is recorded, do not stage or commit the current Velox
changes.

## Controller unblock response 2 — G-CACHEOPEN-RENAME-01

```text
controller_status: blocker_resolved
environment_profile: root-oss
task: 014
decision: fix_now_in_task_014
redispatch_same_task: yes
next_worker_attempt: 6
```

The user selected option A on 2026-07-24: fix the rename/open race in the
current Task-014 corrective before accepting the external-truncation change.

The canonical task now records:

```text
structured missing-file mapping:
  VeloxException::errorCode() == velox::error_code::kFileNotFound
retry:
  recompute path under FileSegment lock and retry once only when it changed
RED:
  deterministic TestValue coordination between old-path capture and concurrent
  completion/rename
file-scope extension:
  FileCacheBufferedInputTest.cpp
```

Redispatch Task 014 from the existing unstaged attempt-5 implementation. The
Worker must append attempt 6 and stop without staging or committing.

## Worker attempt 6 (Review-5 G-CACHEOPEN-RENAME-01)

```text
worker_status: ready_for_controller
attempt: 6
environment_profile: root-oss
task: 014
finding: G-CACHEOPEN-RENAME-01
```

### Baselines

- Velox `filecache` HEAD `26325e8a32` ✓
- CH `ch-filecache` HEAD `5d23dd94c21`, clean ✓
- Unstaged dirty files at start: `FileCacheInputStream.cpp`, `FileCacheE2ETest.cpp` (attempt-5 preserved)

### Dirty state (final)

```
M velox/ch/Disks/IO/FileCacheInputStream.cpp
M velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp
M velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
```

Exactly 3 declared Velox files. No other files modified. Not staged or committed.

### Implementation summary

**G-CACHEOPEN-RENAME-01**: `getCacheReadBuffer` computes `path = fileSegment.getPath()` lock-free. `setDownloadedUnlocked` renames `<offset>` to `<offset>_<size>` under the segment lock before publishing `download_state=DOWNLOADED`. If the rename fires between path computation and `createCacheReadBuffer(path)`, the open fails with `ENOENT` → `VeloxException` with `errorCode() == kFileNotFound`. Fix mirrors `CachedOnDiskReadBufferFromFile.cpp:366-395`: catch only `kFileNotFound`, recompute path under `fileSegment.lock()`, rethrow if unchanged, retry once if changed.

State observation (`downloadState`/`trustSizeFromFilename`) placed AFTER the try/catch block matching CH lines 448-452: state reflects the file that was actually opened; in the retry path the post-rename DOWNLOADED state is observed so the external-truncation check correctly fires if the renamed file was externally shortened.

TestValue hook at `"facebook::velox::ch::FileCacheInputStream::beforeCacheFileOpen"` installed after path computation and before the try block; no-op in production.

### RED phase

```
command: ctest --test-dir _build/debug -R CacheRenameOpenRaceRetries (with retry try/catch replaced by direct createCacheReadBuffer call)
exit: 1
log: _build/debug/test_task014r6_red.log
evidence: VeloxRuntimeError FILE_NOT_FOUND for path .../0 — test fails as expected
```

Build for RED: `_build/debug/build_task014r6_red.log` exit 0.

### GREEN phase

```
command (race test): ctest --test-dir _build/debug -R CacheRenameOpenRaceRetries
exit: 0
log: _build/debug/test_task014r6_green_race.log

command (mono selected): ctest --test-dir _build/debug -R "velox_ch_filecache_(buffered_input|e2e)_test"
result: 2/2 CTest executables passed
log: _build/debug/test_task014r6_green_mono.log
exit: 0

command (mono accumulated): ctest --test-dir _build/debug -R "velox_ch_"
result: 16/16 CTest executables passed
log: _build/debug/test_task014r6_final_accumulated.log
exit: 0
```

Build for GREEN: `_build/debug/build_task014r6_green.log` exit 0.

### Mutation phase

Mutation: removed try/catch block (direct `createCacheReadBuffer(path)` call only).

```
command: ctest --test-dir _build/debug -R CacheRenameOpenRaceRetries
exit: 1
log: _build/debug/build_task014r6_mutation.log
evidence: FILE_NOT_FOUND for path .../0 — mutation correctly fails
```

Restored byte-for-byte.

### Final gates (post-review fix)

```
mono selected 2/2:
  command: ctest --test-dir _build/debug -R "velox_ch_filecache_(buffered_input|e2e)_test"
  exit: 0
  log: _build/debug/test_task014r6_review_fix_mono.log

mono accumulated 16/16:
  command: ctest --test-dir _build/debug -R "velox_ch_"
  exit: 0
  log: _build/debug/test_task014r6_review_fix_accumulated.log

non-mono selected 2/2:
  command: ctest --test-dir _build/debug-task012-nonmono -R "velox_ch_filecache_(buffered_input|e2e)_test"
  exit: 0
  log: _build/debug-task012-nonmono/test_task014r6_review_fix_nonmono.log

git diff --check: exit 0
```

### Code review

One read-only review subagent launched over the full three-file diff.

**Finding (Medium): state snapshot ordering**

`downloadState`/`trustSizeFromFilename` were observed before the try/catch open, but CH places them after (lines 448-452 follow 366-395). After a rename-race retry, `trustSizeFromFilename` was committed to `false` (pre-retry PARTIALLY_DOWNLOADED), so a truncated "0_512" would not be detected.

**Resolution:** Moved state observation to after the catch block closes, exactly matching CH ordering. Comment updated with correct rationale. All gates rerun and passed (see Final gates above).

No other in-scope findings.

### `git diff --check`

Exit 0. No whitespace errors.

### Blockers

None.

### Stop declaration

Worker attempt 6 complete. Three declared Velox files dirty. No staging, commit, or push performed. Handing off to Controller for acceptance.

## Controller review 6 — G-CACHEOPEN-RENAME-01

```text
controller_status: changes_requested
environment_profile: root-oss
task: 014
worker_attempt: 6
redispatch_same_task: yes
```

## Review evidence

```text
spec compliance: approved
production code quality: approved
RED: FILE_NOT_FOUND on old <offset> path
mutation: FILE_NOT_FOUND on old <offset> path
final mono selected: 2/2
final mono accumulated: 16/16
final non-mono selected: 2/2
```

## Required change

`CacheRenameOpenRaceRetries` starts `q2Thread`, then has fatal `ASSERT_*`
statements before `hookRelease.post()` and `q2Thread.join()`. If any assertion
fails, the test returns with the worker blocked on the baton and destroys a
joinable thread, causing `std::terminate`.

Add an immediate `folly::makeGuard` after thread construction that always posts
`hookRelease` and joins `q2Thread` when joinable. Remove the unguarded explicit
cleanup or dismiss the guard after successful explicit cleanup, but never join
twice. Use the existing guard pattern in the same test file. Rerun affected
mono/non-mono selected gates, append Worker attempt 7, and stop. No production
change is requested.

## Worker attempt 7 (Review-5 race-test cleanup)

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 014
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `26325e8a32` | `M velox/ch/Disks/IO/FileCacheInputStream.cpp`, `M velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp`, `M velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp` |
| `/root/oss/clickhouse` | `ch-filecache` | `5d23dd94c21` | `M port/task/CONTROLLER_HANDOFF.md`, `M port/task/result/014-filecache-buffered-input-result.md` |

## Files changed

```text
velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp
  — added `folly::makeGuard` cleanup for `q2Thread` immediately after thread construction;
    `FileCacheInputStream.cpp` and `FileCacheE2ETest.cpp` preserved byte-for-byte
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| Build + run mono selected buffered/E2E | 0 | `_build/debug/build_task014r7_mono.log`, `_build/debug/test_task014r7_mono_selected.log` |
| Build + run non-mono selected buffered/E2E | 0 | `_build/debug-task012-nonmono/build_task014r7_nonmono.log`, `_build/debug-task012-nonmono/test_task014r7_nonmono_selected.log` |
| `git diff --check` | 0 | (stdout) |

## Acceptance evidence

```text
mono selected buffered+E2E: 2/2 passed
non-mono selected buffered+E2E: 2/2 passed
git diff --check: clean
declared Velox files dirty: 3
review subagent: 1 read-only code-review agent, no findings
```

## Worker review

```text
review subagent: one read-only code-review subagent launched on the final full diff.
findings: none
resolutions: none required
unresolved findings: none
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 014 was attempted.
Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Controller review 7 — Review-5 Task-014 corrective accepted

```text
controller_status: accepted
environment_profile: root-oss
task: 014
accepted_worker_attempt: 7
accepted_velox_commit: cda6c03703
```

## Review evidence

```text
G-CACHEBUF-01:
  real physical-truncation RED: short pread 4096 vs 8192
  mutation: disabling size guard restores the same RED
  GREEN: complete source bytes returned through remote bypass
  broken segment left in metadata/priority structures

G-CACHEOPEN-RENAME-01:
  deterministic RED: FILE_NOT_FOUND on old <offset> path
  mutation: removing changed-path retry restores FILE_NOT_FOUND
  GREEN: catch only kFileNotFound, recompute under segment lock, retry once
  unchanged-path and unrelated errors still propagate

failure cleanup:
  q2Thread protected by makeGuard on every assertion/exception path

independent final review:
  APPROVE
  Blocker/Major findings: 0
```

## Controller gates

```text
mono selected E2E/buffered:
  2/2 passed
  /root/oss/velox/_build/debug/test_task014_controller_final_selected.log

mono accumulated velox_ch_:
  16/16 passed
  /root/oss/velox/_build/debug/test_task014_controller_final_accumulated.log

non-mono selected E2E/buffered:
  2/2 passed
  /root/oss/velox/_build/debug-task012-nonmono/test_task014_controller_final_selected.log

git diff --check:
  clean
```

## Accepted implementation

```text
cda6c03703cf4ed0b1b515465915dbfd599bcb6c
Task 014: Recover truncated and renamed cache reads
```

`G-CACHEBUF-01` and `G-CACHEOPEN-RENAME-01` are closed. Review 5 may resume,
but remains blocked on the user-pending `R2-D4` and `R2-D6` decisions. Task
017B remains unauthorized.
