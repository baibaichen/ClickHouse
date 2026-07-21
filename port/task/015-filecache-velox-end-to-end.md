# Task 015: `FileCache` Velox End-to-End Validation and Basic Benchmark

> **MVP task.**
>
> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> creates test and benchmark files in the Velox checkout. It writes one result
> file under this ClickHouse checkout. Do not
> modify ClickHouse source files. Do not commit or stage either repository.
>
> Production changes are permitted **only** when a concrete integration defect
> is found during testing. Each such defect must be described in the result
> file before the fix is applied.

## Status and authority

```text
task_015_allowed: true
environment_profile: root-oss
authority: port/task/fullreview/root-oss/2/003-014-review-decisions.md
authority: port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md
closure: port/task/fullreview/root-oss/2/003-014-targeted-b2-b5-closure.md
```

Task 015 is prohibited until:

```text
1. Task 011's B2/B3 corrective scope is accepted.
2. Task 012's B4/B5 corrective scope is accepted.
3. Task 014's B4 (Task-014 half) corrective confirmation is accepted.
4. A targeted re-review of the Tasks 003-014 rows affected by B2-B5 records
   zero unresolved findings.
5. The user explicitly approves starting Task 015.
```

Recording a fresh decision document for that targeted re-review is good
practice but is **not** itself a blocking gate: once the targeted review
records zero unresolved findings and the user approves, Task 015 may start
without waiting for a separately-authored decision-doc artifact.

All five gates are satisfied. The targeted review closed B2-B5 with zero
unresolved findings, and the user's instruction "好，继续；直到做完 task 015"
supplies the explicit approval.

This task also carries the corrective fix and end-to-end evidence for **B1 —
direct-IO source with background download**, deferred from the Review-2
audit of Tasks 003-014 (see `### B1 corrective contract` below). B1 does not
block starting Task 015; it is part of this task's own deliverable.

## Goal

Validate the complete `FileCacheBufferedInput` → `FileCacheInputStream` →
`FileCache` integration end-to-end. Cover all required read scenarios:

```text
miss → fill → hit
bypass / cache-only
BackUp within output buffer
SkipInt64 across segment boundary
seekToPosition region-relative coordinates
non-zero region.offset absolute coordinate mapping
discarded enqueue before load (no use-after-free)
load is a no-op planning barrier (does not dereference stream)
DWRF stripe-metadata path (shouldPrefetchStripes = false)
path-only key when etag is empty
different non-empty etags → different keys
manager shutdown while stream is alive but not actively reading
direct-IO source + background download completes successfully (B1)
direct-IO source with an unaligned final segment/tail explicitly skips
  background download instead of silently falling back to buffered I/O (B1)
```

### ClickHouse integration-test migration ownership

Audit the applicable scenarios in:

```text
tests/integration/test_filesystem_cache/test.py
tests/integration/test_cache_bypass_on_disk_failure/test.py
tests/queries/0_stateless/*filesystem_cache*
```

Port selected end-to-end behavior, not ClickHouse server/configuration plumbing.
The Velox E2E fixture must cover at least:

```text
cold miss -> cache fill -> later hit
partial segment continuation across readers
cache write failure -> configured bypass or propagated failure
truncated/invalid cached data -> source recovery without stale reader state
reserve-ahead/downloaded-size accounting at the public FileCache boundary
random seeks across hit/miss/bypass paths
direct-IO source + background download (B1)
```

Every one of these seven rows requires a concrete `TEST_F` in
`FileCacheE2ETest.cpp` with a real fixture, deterministic offsets, and asserted
output bytes/counters — see `### Complete E2E scenario matrix (binding)`
below, which supersedes the comment-only placeholders that used to appear
later in this file. Record a migration matrix in the Task-015 receipt:
original CH test/scenario, Velox test name, and any explicit exclusion. Task
012 remains the owner of focused `FileSegment` resume/reconciliation UTs, and
Task 014 remains the owner of focused reader/handoff tests. Task 015 must
exercise those behaviors through the assembled public path without
duplicating their internal test logic.

Deliverables:
- Velox focused test binary `velox_ch_filecache_e2e_test`.
- Velox benchmark binary `velox_ch_filecache_seek_benchmark`.
- All test scenarios pass; benchmark builds and runs without crash, and its
  warmup/smoke pass asserts correct output bytes before any timing loop (see
  `### Benchmark: binding requirements, not a skeleton` below).

## Starting point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected HEAD:    descendant of the task-014 result commit
```

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/3-consumers/03-filecache-buffered-input-design.md
<clickhouse_repo>/port/3-consumers/01-filecache-read-context-design.md
<clickhouse_repo>/port/task/result/014-filecache-buffered-input-result.md
<clickhouse_repo>/port/task/fullreview/root-oss/2/003-014-review-decisions.md
```

## B1 corrective contract: direct-IO source with background download

### Dependency pre-check

Before editing, confirm every row is true; stop as `blocked` and report the gap
instead of guessing:

| External name | Approved mapping | Source of approval |
|---|---|---|
| Task 007 `ReadBufferFromVeloxReadFile`/`ReadBufferFromFileBase` | accepted, `directIoAlignment_` member exists but is `protected` with no public accessor | `port/task/result/007-filecache-io-adapters-result.md`; `ReadBufferFromVeloxReadFile.h:275` |
| Task 012 `CacheMetadata::downloadImpl` | accepted; allocates its background buffer via `velox::AlignedBuffer::allocate<char>(..., memory_pool)` (fixed 64-byte alignment, no runtime alignment parameter) | `port/task/result/012-filecache-core-scc-result.md`; `Metadata.cpp:975-1052` |
| Task 014 `FileCacheInputStream::allocateOutputBufferIfNeeded` | accepted; already over-allocates and rounds the address up to `directIoAlignment_` for the foreground path | `port/task/result/014-filecache-buffered-input-result.md`; `FileCacheInputStream.cpp:95-125` |
| `velox::AlignedBuffer::kAlignment` | `64` (fixed, compile-time); insufficient for a direct-IO alignment requirement larger than 64 (e.g. 512/4096-byte sectors) | `velox/buffer/Buffer.h:352` |

### Root cause (verified)

`CacheMetadata::downloadImpl` (`Metadata.cpp:975-1052`) allocates its
background-download scratch buffer with `AlignedBuffer::allocate<char>` at
`Metadata.cpp:999` (fixed 64-byte alignment only) and then calls
`buf->set(memory->asMutable<char>(), std::min(size_to_download, memory->size()))`
at `Metadata.cpp:1000`. When the handed-off reader requires direct I/O with an
alignment greater than 64 (queried via `ReadFile::directIo` and stored in
`directIoAlignment_`, `ReadBufferFromVeloxReadFile.cpp:390-396`), `set()`'s
alignment check (`ReadBufferFromVeloxReadFile.cpp:266-282`,
`checkDirectIoRead`, `ReadBufferFromVeloxReadFile.cpp:225-247`) throws because
the pointer address (and potentially the requested length) violates
`directIoAlignment_`. The exception propagates to
`CacheMetadata::processDownloadQueue`'s `catch (...)`
(`Metadata.cpp:943-960`), which calls `file_segment.setDownloadFailed()`: the
failure is fail-closed (no corruption, no aborted release build) but
background completion never succeeds for a direct-IO source. Task 014's
foreground path (`FileCacheInputStream::allocateOutputBufferIfNeeded`,
`FileCacheInputStream.cpp:95-125`) already solves the identical problem by
over-allocating `rounded + directIoAlignment_` bytes and rounding the raw
address up to the required alignment; this is the pattern to mirror.

### Required fix (production changes justified by this defect)

1. **Expose the reader's required alignment.** Add a public accessor on
   `ReadBufferFromFileBase` (`velox/ch/IO/ReadBufferFromVeloxReadFile.h`,
   Task-007-owned but modifiable here because this is the concrete defect
   this task exists to fix): `size_t directIoAlignment() const { return
   directIoAlignment_; }`. Do not change `directIoAlignment_`'s type, default,
   or any existing call site's behavior.
2. **Align the background buffer.** In `CacheMetadata::downloadImpl`
   (`Metadata.cpp:975-1052`), before calling `buf->set(...)`, query
   `buf->directIoAlignment()`. If it is `<= 1`, keep the existing
   `AlignedBuffer::allocate<char>(std::min(DBMS_DEFAULT_BUFFER_SIZE,
   size_to_download), memory_pool)` path unchanged. If it is `> 1`, mirror
   `FileCacheInputStream.cpp:106-125`: over-allocate enough bytes to carve out
   an alignment-multiple usable region (`rounded + alignment` bytes,
   `rounded` an alignment multiple `<= size_to_download`) and round the raw
   pool address up to the alignment, then pass the aligned pointer and
   `rounded` length to `buf->set(...)`. `memory` is a scratch buffer reused
   across `processDownloadQueue`'s loop across possibly many segments with
   different alignment requirements; do not reuse a previously-allocated
   `memory` buffer for a segment whose required alignment it does not already
   satisfy — (re)allocate when the stored alignment changes. Preserve the
   exact file offset and read length contract already enforced by the
   surrounding loop (`Metadata.cpp:1006-1045`); do not change the seek/offset
   math, only the buffer's address and usable length.
3. **Explicit unaligned-tail skip.** `buf->set(ptr, size)` also requires
   `size % directIoAlignment_ == 0`
   (`ReadBufferFromVeloxReadFile.cpp:272-275`). If the aligned-down `rounded`
   size computed in step 2 is `0` (the remaining `size_to_download` for a
   final segment/tail is smaller than one full alignment unit), do not call
   `buf->set(...)` at all and do not fall back to a buffered/unaligned read.
   Instead, log at `LOG_TEST` level and `return` from `downloadImpl` without
   downloading that segment in the background (the segment stays
   `PARTIALLY_DOWNLOADED`; a later foreground read still completes it through
   the already-aligned `FileCacheInputStream` path). This is a fail-closed
   skip, not a silent correctness gap: no unaligned direct-IO read is ever
   attempted, and no buffered fallback is introduced.
4. Do not modify `ReadBufferFromVeloxReadFile.cpp`'s `checkDirectIoRead`,
   `set`, or `seek` alignment enforcement; the fix lives entirely in the
   caller (`Metadata.cpp`), which must satisfy the adapter's existing
   contract, not relax it.

### Execution-discovered foreground tail defect

The B1 tail E2E exposed a second concrete integration defect: the foreground
reader's destination buffer was aligned, but `ReadBufferFromFileBase::nextImpl`
passed the unaligned logical tail length directly to the direct-IO `pread`
check. The accepted fix in `ReadBufferFromVeloxReadFile.cpp` keeps
`checkDirectIoRead`, `set`, and `seek` strict: it rounds the physical request
length up to the direct-IO alignment, validates that aligned request, and
publishes only the logical bytes up to `readUntil_`. This is not a buffered
fallback and never exposes the physical over-read.

### Post-acceptance direct-IO test-plane correction

The original `IoAdaptersTest` cases still asserted the pre-Task-015 contract:
an unaligned logical EOF tail/right bound must be rejected before `pread`.
Task 015 intentionally changed that behavior to an aligned physical request
with logical clamping. Running CTest without rebuilding every registered target
used the stale `velox_ch_io_test` executable and hid this contradiction.

The binding adapter tests are:

```text
ReaderDirectIoUnalignedTailUsesAlignedPhysicalRead
ReaderDirectIoUnalignedRightBoundClampsPhysicalRead
```

They must prove the physical offset/address/length are aligned, only logical
bytes are exposed, and reaching the logical right bound issues no second read.
A mutation that passes `logicalToRead` directly as the physical length must make
both tests fail on direct-IO length validation.

The accumulated gate must first build every registered `velox_ch_*` target and
only then run `ctest -R '^velox_ch_'`; running CTest alone is not accepted
evidence after a shared-library/source change.

### B1 end-to-end test (binding)

Add `TEST_F(FileCacheE2ETest, DirectIoSourceBackgroundDownloadCompletes)`:

1. construct a `ReadFile`/adapter combination whose `directIo(alignment)`
   reports a real alignment `> 64` (e.g. 512), matching how
   `FileCacheInputStream`'s constructor queries it
   (`FileCacheInputStream.cpp:62-66`);
2. configure the cache with `backgroundDownloadThreads > 0`;
3. hand off a partially-downloaded segment to the background-download queue
   (the same production path `CacheMetadata::downloadImpl` serves) so the
   background worker must call `buf->set(...)` on the direct-IO reader;
4. assert the background download completes successfully (the segment
   reaches `DOWNLOADED`, not `PARTIALLY_DOWNLOADED_NO_CONTINUATION` via
   `setDownloadFailed()`), and a subsequent read is a cache hit with no
   further remote I/O.

Add `TEST_F(FileCacheE2ETest, DirectIoUnalignedTailSkipsBackgroundDownload)`:

1. same direct-IO alignment setup, but size the remaining
   `size_to_download` for the final segment so it is smaller than one
   alignment unit and not itself alignment-sized;
2. assert `downloadImpl` returns without calling `buf->set(...)` for that
   tail (no thrown alignment exception is caught as a failure; the segment
   is simply left for foreground completion), and that a subsequent
   foreground read still completes and returns correct bytes through
   `FileCacheInputStream`'s already-aligned path.

RED: before the fix, `DirectIoSourceBackgroundDownloadCompletes` fails because
the background worker's `catch (...)` marks the segment
`setDownloadFailed()` after the alignment exception from `buf->set(...)`;
capture this failing run (against the pre-fix `Metadata.cpp`) as the RED log.

False-green mutation: after the fix, revert step 2's aligned-allocation branch
(force the `directIoAlignment() <= 1` path unconditionally), rebuild, and
confirm `DirectIoSourceBackgroundDownloadCompletes` fails again for the exact
pre-fix reason. Restore the fix and re-confirm green.

## File scope

Create in the Velox checkout:

```text
<velox_repo>/velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
<velox_repo>/velox/ch/Disks/IO/tests/FileCacheTestHelpers.h
<velox_repo>/velox/ch/benchmarks/CMakeLists.txt
<velox_repo>/velox/ch/benchmarks/FileCacheSeekBenchmark.cpp
```

Modify in the Velox checkout:

```text
<velox_repo>/velox/ch/CMakeLists.txt
<velox_repo>/velox/ch/Disks/IO/CMakeLists.txt
<velox_repo>/velox/ch/Disks/IO/tests/CMakeLists.txt
<velox_repo>/velox/ch/IO/ReadBufferFromVeloxReadFile.h  (B1: add the
  directIoAlignment() public accessor only; no behavior change to any
  existing call site)
<velox_repo>/velox/ch/IO/ReadBufferFromVeloxReadFile.cpp  (execution-discovered
  B1 foreground-tail defect: aligned physical read with logical-byte clamp;
  no relaxation of checkDirectIoRead/set/seek)
<velox_repo>/velox/ch/Interpreters/FileCache/Metadata.cpp  (B1: align
  CacheMetadata::downloadImpl's background buffer and add the explicit
  unaligned-tail skip; this is the concrete integration defect this task's
  charter permits fixing)
```

These two production files are modified **only** as the direct, documented fix
for the B1 defect described above. Do not touch any other line in either file.
Every other production behavior in this task remains test-only; see the
"Production changes are permitted only when a concrete integration defect is
found" rule at the top of this file.

Create in the ClickHouse checkout:

```text
<clickhouse_repo>/port/task/result/015-filecache-velox-e2e-result.md
```

Every new Velox C++ file must begin with the Apache 2.0 Facebook license
header from `port/task/003-filecache-basic-common-shims.md`.

`FileCacheTestHelpers.h` is the shared, header-only fixture surface used by
both the E2E binary and benchmark. It owns deterministic source data,
counting/direct-IO `ReadFile` helpers, manager/cache/input construction, and
stream draining so the benchmark does not reimplement a divergent fixture.

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the baselines**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected: Velox on `filecache` after Task 014.
Record all pre-existing dirty files in the result file.

- [ ] **Step 2: Create the `velox/ch/Disks/IO/tests` directory and skeleton CMakeLists.txt**

If `velox/ch/Disks/IO/CMakeLists.txt` does not already include a
`tests` subdirectory, append to it:

```cmake
if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Append the E2E target to the existing
`velox/ch/Disks/IO/tests/CMakeLists.txt` created by Task 014:

```cmake
add_executable(velox_ch_filecache_e2e_test FileCacheE2ETest.cpp)
add_test(velox_ch_filecache_e2e_test velox_ch_filecache_e2e_test)

target_link_libraries(
  velox_ch_filecache_e2e_test
  PRIVATE
    velox_ch_filecache_dwio
    velox_ch_filecache_manager
    velox_ch_filecache_core
    velox_ch_filecache
    velox_dwio_common
    velox_file
    velox_test_util
    velox_exception
    velox_memory
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

If `velox/ch/CMakeLists.txt` does not yet include the `Disks` subdirectory,
append:

```cmake
add_subdirectory(Disks)
```

and ensure `velox/ch/Disks/CMakeLists.txt` contains:

```cmake
add_subdirectory(IO)
```

- [ ] **Step 3: Create the complete Velox E2E test file**

Create `velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp` with the repository
license, the production headers, the fixture from Step 4, and every test listed
in that step. Do not add `GTEST_SKIP`, empty bodies, fake cache classes, or
compile-only assertions. The first build is expected to expose real
API/integration errors.

- [ ] **Step 4: Complete the Velox E2E fixture and test cases**

Use the following fixture to set up a per-test `FileCacheManager` and an in-memory
`ReadFile` backed by a fixed byte pattern:

```cpp
class FileCacheE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        filesystems::registerLocalFileSystem();
        tempDir_ = common::testutil::TempDirectoryPath::create();
        pool_ = memory::memoryManager()->addLeafPool("e2e");
        auto opts = makeOptions(tempDir_->getPath(), pool_.get());
        manager_ = FileCacheManager::create(std::move(opts));
        FileCacheManager::setInstance(manager_.get());
        cache_ = manager_->getDefault();
        ASSERT_NE(cache_, nullptr);
    }

    void TearDown() override {
        if (FileCacheManager::getInstance()) {
            FileCacheManager::getInstance()->shutdown();
            FileCacheManager::setInstance(nullptr);
        }
        manager_.reset();
    }

    // Create a ReadFile backed by `data` (copied, not referenced).
    std::shared_ptr<ReadFile> makeMemoryReadFile(std::vector<char> data);

    // Create a FileCacheBufferedInput for the given ReadFile.
    std::unique_ptr<FileCacheBufferedInput> makeInput(
        std::shared_ptr<ReadFile> file,
        FileCacheKey key);

    // Read all bytes from a FileCacheInputStream via Next().
    std::vector<char> readAll(SeekableInputStream& stream);

    std::shared_ptr<common::testutil::TempDirectoryPath> tempDir_;
    std::shared_ptr<memory::MemoryPool> pool_;
    std::shared_ptr<FileCacheManager> manager_;
    FileCachePtr cache_;
};
```

Implement each test case with concrete assertions. The key behavioral
contracts to verify per test:

**`MissFillHit`:**
1. Write a 256 KiB byte pattern to a `MemoryReadFile`.
2. `enqueue` a region covering 0..128 KiB.
3. Call `Next()` to exhaustion; assert bytes match the pattern.
4. `isBuffered(0, 128*1024)` on a freshly constructed `FileCacheBufferedInput`
   for the same key returns `true`.
5. `Next()` on the second stream reads the same bytes without remote I/O
   (assert `MemoryReadFile::preadv` call count does not increase).

**`CacheOnlyMissFails`:**
1. Set `FileCacheReadOptions::tempCacheOnly = true`.
2. Attempt to read a key that has never been cached.
3. Assert `Next()` throws `VeloxRuntimeError`.

**`ReadIfExistsBypassMode`:**
1. Set `readIfExistsOtherwiseBypass = true`.
2. Read an uncached key; assert bytes equal the source data (bypass path).
3. No new `FileSegment` must remain in the cache after the read completes.

**`BackUpWithinOutputBuffer`:**
1. Read 64 KiB via `Next()`.
2. Call `BackUp(1024)`.
3. `ByteCount()` must equal `64 * 1024 - 1024`.
4. Re-reading those 1024 bytes via `Next()` must return the same bytes.

**`SkipAcrossSegmentBoundary`:**
1. Enqueue a 512 KiB region; `Next()` to consume the first segment.
2. `SkipInt64` to skip past the first segment boundary.
3. `Next()` must return data starting from the correct absolute offset.

**`SeekToPositionRegionRelative`:**
1. Enqueue a region starting at absolute offset 4096.
2. Seek to region-relative position 256.
3. `Next()` must return data starting at absolute file offset 4096 + 256.
4. `ByteCount()` must equal 256 after seek before any further `Next()`.

**`NonzeroRegionOffsetAbsoluteCoordinates`:**
1. Enqueue region `{offset=65536, length=32768}`.
2. Read all bytes via `Next()`.
3. Assert that file bytes [65536, 98303] are returned, not [0, 32767].

**`DiscardedEnqueueNoUseAfterFree`:**
1. `enqueue` a region; immediately discard the returned
   `unique_ptr<SeekableInputStream>` without calling `Next()`.
2. Call `load()`.
3. Verify no crash and no sanitizer error.

**`LoadIsNopPlanningBarrier`:**
1. `enqueue` three regions.
2. Discard all three returned streams.
3. Call `load()`.
4. Verify no crash (load must not dereference any stream pointer).

**`DWRFShouldPrefetchStripesIsFalse`:**
1. Construct a `FileCacheBufferedInput`.
2. Assert `shouldPrefetchStripes()` returns `false`.
3. Assert `preloaded()` returns `false`.

**`PathOnlyKeyWhenEtagEmpty`:**
1. Derive a key via `FileCacheKey::fromPath(path)`.
2. Read through `FileCacheBufferedInput` using that key.
3. Re-derive the key with the same path; assert cache hit.

**`DifferentEtagsDifferentKeys`:**
1. Create two `FileCacheBufferedInput` instances for the same path but etags
   `"v1"` and `"v2"`, using `FileCacheKey::fromKey(sipHash128(path+etag))`.
2. Fill both caches.
3. Assert the two keys compare not-equal.
4. Assert each stream hits its own segment, not the other's.

**`ShutdownWhileStreamAliveNotReading`:**
1. `enqueue` a region; hold the stream alive.
2. Call `FileCacheManager::shutdown()` without the stream calling `Next()`.
3. Assert shutdown completes without deadlock or crash.
4. The held stream must be allowed to destruct without further reads.

### Complete E2E scenario matrix (binding)

The scenarios above cover the `## Goal` read-mechanics list. The following
additional cases are required to close the CH migration-matrix rows from
`### ClickHouse integration-test migration ownership` above that are **not**
already covered by a case above. Every case below must construct the same real
fixture (`FileCacheManager`, `FileCache`, real `ReadFile`, real
`FileCacheBufferedInput`) and assert concrete output bytes/counters; no
comment-only body is acceptable.

**`PartialSegmentContinuationAcrossReaders`** (migration row: "partial segment
continuation across readers"):
1. Enqueue a region; call `Next()` only partway (stop before the stream is
   exhausted), then destroy the stream and the owning
   `FileCacheBufferedInput` (simulating one query ending mid-read).
2. Construct a second, independent `FileCacheBufferedInput` for the same key
   and region.
3. `Next()` to exhaustion on the second stream; assert the full byte range
   matches the source pattern.
4. Assert `MemoryReadFile::preadv` call count on the second pass is smaller
   than a fresh cold read would require (the first reader's partial download
   was continued, not restarted from offset 0).

**`CacheWriteFailureConfiguredBypassOrPropagate`** (migration row: "cache
write failure -> configured bypass or propagated failure"; production hook:
`FileCacheInputStream::writeCache`, `FileCacheInputStream.cpp:542-565`, and
`FileCache::skipCacheOnDiskFailure()`):
1. Configure the cache directory so a cache-file write fails (e.g. an
   unwritable per-key subdirectory), with `skip_cache_on_disk_failure` unset
   (default `false`).
2. Read via `Next()`; assert the write failure **propagates** (the read
   throws), since bypass is not configured.
3. Reconfigure with `skip_cache_on_disk_failure = true` and repeat with a
   fresh key; assert the read **succeeds** with correct bytes (bypassing the
   cache write), and that no partial/corrupt cache file is left for that key.

**`TruncatedOrInvalidCachedDataSourceRecovery`** (migration row:
"truncated/invalid cached data -> source recovery without stale reader
state"):
1. Fill the cache for a key via one stream (miss → fill → hit).
2. Externally invalidate that cached segment (remove or truncate its on-disk
   cache file and/or invalidate the `FileCache` entry through a supported
   production API — do not fabricate an unsupported CH-only recovery path;
   `port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md`
   §2 ("Task 014 — Buffered input reader" verdict, "Minor holes vs CH reader
   robustness" bullet) and §6.1 ("014: direct-IO + background download (no
   test); `getCacheReadBuffer` rename-race / truncated / empty-file") already
   document CH's rename-race lock+retry
   (`src/Disks/IO/CachedOnDiskReadBufferFromFile.cpp:366-394`) and the
   externally-truncated-segment bypass
   (`src/Disks/IO/CachedOnDiskReadBufferFromFile.cpp:448-477`) as **not**
   ported).
3. Construct a fresh `FileCacheBufferedInput`/stream for the same key/region
   and read via `Next()`.
4. Assert the fresh stream detects the miss (does not return stale/corrupt
   bytes from the invalidated file) and correctly refills from source; assert
   final bytes match the source pattern exactly.
5. If this scenario surfaces a genuine defect (stale bytes returned, a crash,
   or a hang) rather than a documented, already-accepted gap, treat it as a
   concrete integration defect per this task's charter: describe it and fix
   the minimal production defect.

**`ReserveAheadDownloadedSizeAccounting`** (migration row: "reserve-ahead/
downloaded-size accounting at the public FileCache boundary"):
1. Read a known byte range through `FileCacheBufferedInput`/`Next()`.
2. Assert `cache_->getUsedCacheSize()` equals the expected downloaded byte
   count and `cache_->getFileSegmentsNum()` equals the expected segment count,
   using only the public `FileCache` boundary (no internal `Metadata`/
   `FileSegment` reach-through).

**`RandomSeeksAcrossHitMissBypassPaths`** (migration row: "random seeks across
hit/miss/bypass paths"; this case also serves as the benchmark's smoke
correctness proof, see `### Benchmark: binding requirements, not a skeleton`
below):
1. Using a fixed seed (e.g. `std::mt19937_64` seeded with a literal constant,
   not a time-based seed, so the sequence is fully deterministic), generate a
   sequence of at least 50 `(offset, length)` pairs spanning the test file.
2. For each pair, alternate across the three modes: normal cache-eligible
   read (may hit or miss depending on prior coverage), `tempCacheOnly` on an
   uncached range (expect throw, matching `CacheOnlyMissFails`), and
   `readIfExistsOtherwiseBypass` on an uncached range (expect bypass-path
   correct bytes, matching `ReadIfExistsBypassMode`).
3. For every cache-eligible read, assert the returned bytes exactly equal the
   corresponding slice of the known source pattern.
4. Track and assert that at least one iteration was a hit (no `preadv` call)
   and at least one was a miss (a `preadv` call occurred), so the sequence
   actually exercises all three paths rather than only one by chance of the
   fixed seed.

**`DirectIoSourceBackgroundDownloadCompletes`** and
**`DirectIoUnalignedTailSkipsBackgroundDownload`** (migration row: "direct-IO
source + background download"): specified in full under
`## B1 corrective contract` above; do not duplicate their bodies here.

- [ ] **Step 5: Build and run the Velox E2E tests, with RED/false-green, mono/non-mono, and accumulated-regression evidence**

Build and run in the existing mono configuration:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_e2e_test \
  > <velox_build_dir>/build_015_e2e.log 2>&1
echo "exit: $?"

ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_filecache_e2e_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_015_e2e.log 2>&1
echo "exit: $?"
```

Expected:

```text
Build exit code: 0.
100% tests passed, 0 tests failed.
```

Then, for every scenario in `## Goal`, `### Complete E2E scenario matrix
(binding)`, and `### B1 corrective contract`, capture both pieces of evidence
in the receipt (a missing-header compile failure alone is not sufficient RED
evidence for either):

```text
behavioral RED: run the test against the pre-implementation state, or with the
  specific production branch it claims to cover replaced by an
  `if (false)`/reverted body, and confirm it fails for the declared behavioral
  reason (e.g. MissFillHit's second-stream isBuffered/preadv-count assertion
  fails if isBuffered's no-create probe is bypassed).
false-green probe: after the test passes, temporarily remove or invert the
  one implementation branch it claims to prove, rebuild, confirm the test now
  fails, then restore the branch and re-confirm green.
```

Then re-run the same focused test in the non-mono configuration
(`VELOX_MONO_LIBRARY=OFF`, the same configuration Tasks 012/014 already
established under `<velox_build_dir>-task012-nonmono` or an equivalent fresh
non-mono build directory):

```bash
ctest \
  --test-dir <velox_build_dir_nonmono> \
  -R '^velox_ch_filecache_e2e_test$' \
  --output-on-failure \
  > <velox_build_dir_nonmono>/test_015_e2e_nonmono.log 2>&1
echo "exit: $?"
```

Finally, re-run the accumulated `velox_ch_` regression CTest set already
established by Tasks 012/013/014 (mono configuration) to confirm zero
regressions elsewhere:

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_' \
  --output-on-failure \
  > <velox_build_dir>/test_015_accumulated.log 2>&1
echo "exit: $?"
```

Expected: the E2E binary's internal GTest count increases by exactly the number
of cases added in this task. The accumulated CTest count increases by exactly
one because `add_test` registers the whole
`velox_ch_filecache_e2e_test` executable as one CTest target, with zero
regressions in any previously-accepted `velox_ch_*` binary.

- [ ] **Step 6: Add the Velox random-seek benchmark**

Create the benchmark directory and CMakeLists (this scaffolding is structural,
not test logic, and may be created as shown):

```cmake
# velox/ch/benchmarks/CMakeLists.txt
# Copyright (c) Facebook, Inc. and its affiliates.
# Licensed under the Apache License, Version 2.0 ...

add_executable(velox_ch_filecache_seek_benchmark FileCacheSeekBenchmark.cpp)

target_link_libraries(
  velox_ch_filecache_seek_benchmark
  PRIVATE
    velox_ch_filecache_dwio
    velox_ch_filecache_manager
    velox_ch_filecache_core
    velox_ch_filecache
    velox_file
    velox_exception
    velox_memory
    Folly::folly
    Folly::follybenchmark
    gflags::gflags
)
```

Append to `velox/ch/CMakeLists.txt` (inside an `if(VELOX_ENABLE_BENCHMARKS)` guard):

```cmake
if(${VELOX_ENABLE_BENCHMARKS})
  add_subdirectory(benchmarks)
endif()
```

### Benchmark: binding requirements, not a skeleton

The `BENCHMARK(...)` bodies below **must** be real, working code, not the
comment-only placeholders ("Exact implementation fills...", "Then benchmark
seeks...") that used to appear in this step. Every requirement is binding:

1. `setupFixture()` must construct a real `FileCacheManager`, `FileCache`,
   an in-memory or temp-file-backed synthetic source of `FLAGS_file_size_mb`
   bytes with a known, checkable byte pattern (e.g. `byte[i] = i % 251`), and
   a real `FileCacheBufferedInput` factory, exactly as
   `FileCacheE2ETest`'s fixture does (reuse the same construction helpers by
   linking `velox_ch_filecache_dwio`/`velox_ch_filecache_manager`; do not
   reimplement a second, divergent fixture).
2. `seekOffsets` must be generated with a fixed-seed deterministic PRNG (the
   same `std::mt19937_64` fixed-seed approach as
   `RandomSeeksAcrossHitMissBypassPaths` in Step 4), not `std::random_device`
   or a time-based seed, so every benchmark run exercises the identical
   offset sequence.
3. `FileCacheSeekCacheHit`: before entering the timed loop, warm every
   segment referenced by `seekOffsets` (read each once so it reaches
   `DOWNLOADED`), then, for each benchmark iteration, perform a real
   `seekToPosition` + one real `Next()` per pre-generated offset and assert
   (via `VELOX_CHECK`/`chassert`, not a silently-ignored return value) that
   the returned bytes match the known pattern at that offset. A benchmark
   iteration that skips the real seek+`Next()` call or the byte check is not
   an acceptable implementation of this row.
4. `FileCacheSeekCacheMiss`: before each timed iteration (or each outer
   repetition), clear/reset the cache directory so every seek is a genuine
   cold miss, then perform the real seek + `Next()` + byte-pattern assertion
   exactly as in the hit case.
5. `FileCacheSeekBypass`: construct the input with
   `readIfExistsOtherwiseBypass = true` against an always-uncached key, then
   perform the real seek + `Next()` + byte-pattern assertion.
6. Before `folly::runBenchmarks()` executes any timed iteration, `main` must
   run one **smoke pass** through all three modes (hit/miss/bypass) with
   `--bm_min_iters` effectively `1`, asserting correct bytes for at least one
   offset per mode, and abort (non-zero exit) if any assertion fails. This is
   the "benchmark-smoke" gate: a benchmark that only measures wall-clock time
   without ever validating output correctness is not acceptable evidence that
   the benchmark exercises the real hit/miss/bypass paths it claims to.
7. `folly::doNotOptimizeAway` may still be used on the final byte buffer or
   checksum to prevent dead-code elimination of the *timed* portion, but must
   not be used as a substitute for the correctness assertions above.

Create `velox/ch/benchmarks/FileCacheSeekBenchmark.cpp`, implementing exactly
the requirements above (the file/flag/include layout below is structural
scaffolding; the `BENCHMARK` bodies and `setupFixture()` must contain the real
logic from points 1-7, not comments describing it):

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

// Benchmark: random seekToPosition access patterns via FileCacheInputStream.
//
// Usage:
//   velox_ch_filecache_seek_benchmark \
//     --bm_min_iters=10 \
//     --file_size_mb=256 \
//     --cache_dir=/tmp/fc_bench \
//     --cache_size_mb=512
//
// Metrics emitted (per iteration):
//   seek_cache_hit_ns   — seek + one Next() when segment is already DOWNLOADED
//   seek_cache_miss_ns  — seek + one Next() on first access (miss → fill)
//   seek_bypass_ns      — seek + one Next() with readIfExistsOtherwiseBypass
//
// Every mode below performs a real seek + Next() and checks the returned
// bytes against the known synthetic pattern before the timed portion
// completes; see Task 015's "Benchmark: binding requirements" section.

#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/time/Timer.h"
#include "velox/dwio/common/ReaderOptions.h"

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>

#include <random>

DEFINE_int32(file_size_mb, 64, "Synthetic file size in MiB");
DEFINE_int32(cache_size_mb, 128, "FileCache max size in MiB");
DEFINE_string(cache_dir, "tmp/fc_seek_bench", "FileCache directory path");
DEFINE_int32(seek_count, 1000, "Number of random seeks per benchmark run");

namespace facebook::velox::ch
{
namespace
{

struct BenchmarkFixture;

static std::unique_ptr<BenchmarkFixture> gFixture;

struct BenchmarkFixture
{
    std::shared_ptr<FileCacheManager> manager;
    FileCachePtr cache;
    std::shared_ptr<ReadFile> sourceFile;
    FileCacheKey cacheKey;
    std::vector<uint64_t> seekOffsets; // fixed-seed deterministic offsets
    std::vector<char> expectedPattern; // known byte pattern for correctness checks
    memory::MemoryPool* pool;
};

// Constructs manager/cache/source/offsets per the binding requirements above.
// Deterministic: uses a fixed-seed std::mt19937_64, never std::random_device.
void setupFixture();

// One real seek + Next() at `offset`, checking the returned bytes against
// gFixture->expectedPattern. Returns false (and must abort the smoke pass)
// on any mismatch.
bool seekAndVerify(uint64_t offset, bool bypass);

} // namespace
} // namespace facebook::velox::ch

BENCHMARK(FileCacheSeekCacheHit)
{
    // Warm every referenced segment once (outside the timed portion is not
    // possible with folly::Benchmark's single-callback model, so warming
    // happens once via a static-initialized flag inside the implementation;
    // only the steady-state hit path is timed).
    for (auto offset : facebook::velox::ch::gFixture->seekOffsets)
    {
        const bool ok = facebook::velox::ch::seekAndVerify(offset, /*bypass*/ false);
        folly::doNotOptimizeAway(ok);
    }
}

BENCHMARK(FileCacheSeekCacheMiss)
{
    // Each iteration targets a key that has not been read before, forcing a
    // genuine cold miss-fill cycle.
    for (auto offset : facebook::velox::ch::gFixture->seekOffsets)
    {
        const bool ok = facebook::velox::ch::seekAndVerify(offset, /*bypass*/ false);
        folly::doNotOptimizeAway(ok);
    }
}

BENCHMARK(FileCacheSeekBypass)
{
    // readIfExistsOtherwiseBypass = true; no FileSegment is ever created.
    for (auto offset : facebook::velox::ch::gFixture->seekOffsets)
    {
        const bool ok = facebook::velox::ch::seekAndVerify(offset, /*bypass*/ true);
        folly::doNotOptimizeAway(ok);
    }
}

int main(int argc, char** argv) {
    folly::init(&argc, &argv);
    facebook::velox::ch::setupFixture();

    // Smoke pass: validate at least one real seek+Next() per mode (miss, hit,
    // bypass) before any timed benchmark iteration runs. Abort with a
    // non-zero exit if any assertion fails — a benchmark that never checks
    // output correctness is not acceptable evidence for this task.
    const auto smokeOffset = facebook::velox::ch::gFixture->seekOffsets.front();
    // First touch of smokeOffset: cold miss (nothing warmed it yet).
    VELOX_CHECK(facebook::velox::ch::seekAndVerify(smokeOffset, /*bypass*/ false));
    // Second touch of the same offset: now a cache hit (miss above filled it).
    VELOX_CHECK(facebook::velox::ch::seekAndVerify(smokeOffset, /*bypass*/ false));
    // A different, never-touched offset in bypass mode: no segment is created.
    VELOX_CHECK(facebook::velox::ch::seekAndVerify(
        facebook::velox::ch::gFixture->seekOffsets.back(), /*bypass*/ true));

    folly::runBenchmarks();
    if (facebook::velox::ch::gFixture &&
        facebook::velox::ch::gFixture->manager) {
        facebook::velox::ch::gFixture->manager->shutdown();
        facebook::velox::ch::FileCacheManager::setInstance(nullptr);
    }
    return 0;
}
```

Implement `setupFixture()` and `seekAndVerify()` per the binding requirements
above (no comment-only body is acceptable for either), then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_seek_benchmark \
  > <velox_build_dir>/build_015_benchmark.log 2>&1
echo "exit: $?"
```

Run the benchmark (short warmup to confirm both it does not crash and the
smoke-pass correctness assertions pass):

```bash
<velox_build_dir>/velox/ch/benchmarks/velox_ch_filecache_seek_benchmark \
  --bm_min_iters=3 \
  --file_size_mb=8 \
  --cache_size_mb=32 \
  --cache_dir=<velox_build_dir>/fc_bench \
  > <velox_build_dir>/bench_015_seek.log 2>&1
echo "exit: $?"
```

Expected: exit code 0; the smoke-pass `VELOX_CHECK`s in `main` did not abort;
the log contains timing rows for the three benchmark variants. A non-zero
exit or a missing timing row for any of the three variants is a failure of
this step, not evidence to weaken or remove.

- [ ] **Step 7: Verify that no *unexpected* production defects required changes**

`velox/ch/IO/ReadBufferFromVeloxReadFile.h`,
`velox/ch/IO/ReadBufferFromVeloxReadFile.cpp`, and
`velox/ch/Interpreters/FileCache/Metadata.cpp` are **expected** to change,
exactly as documented in `## B1 corrective contract` above; that diff is not
a violation of this step. Inspect the diff for every other production file to
confirm nothing else changed:

```bash
cd <velox_repo>
git --no-pager diff -- \
  velox/ch/Disks/IO/FileCacheBufferedInput.cpp \
  velox/ch/Disks/IO/FileCacheInputStream.cpp \
  velox/ch/Interpreters/FileCache/FileCache.cpp \
  velox/ch/Interpreters/FileCache/FileCacheManager.cpp
```

If any of these four files changed, describe the defect and fix in the result
file before stating `status: success`. Separately, inspect the B1 diff itself
and confirm it touches only the documented accessor addition
(`ReadBufferFromVeloxReadFile.h`), aligned physical/logical-tail handling
(`ReadBufferFromVeloxReadFile.cpp`), and alignment/skip logic
(`Metadata.cpp`'s `downloadImpl`), with no unrelated change in those files:

```bash
git --no-pager diff -- \
  velox/ch/IO/ReadBufferFromVeloxReadFile.h \
  velox/ch/IO/ReadBufferFromVeloxReadFile.cpp \
  velox/ch/Interpreters/FileCache/Metadata.cpp
```

- [ ] **Step 8: Inspect all task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short

```

Expected:

```text
No whitespace errors in Velox.
Only task-owned files appear in the diff.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 9: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/015-filecache-velox-e2e-result.md
```

Use exactly this structure:

````markdown
# Task 015 Result: `FileCache` Velox E2E Validation and Basic Benchmark

## Status

status: success

## Repository state

```text
Velox branch, HEAD, git status --short
```

## Files changed

```text
<list only task-owned Velox files>
```

## Commands run

```text
<configure, build, test, benchmark commands>
```

## Generated logs

```text
<velox_build_dir>/build_015_e2e.log
<velox_build_dir>/test_015_e2e.log
<velox_build_dir_nonmono>/test_015_e2e_nonmono.log
<velox_build_dir>/test_015_accumulated.log
<velox_build_dir>/build_015_benchmark.log
<velox_build_dir>/bench_015_seek.log
<one RED log and one false-green mutation log per material test, including
  the two B1 tests>
```

## Test results

```text
velox_ch_filecache_e2e_test (mono):     <N> tests, 0 failed
velox_ch_filecache_e2e_test (non-mono): <N> tests, 0 failed
accumulated velox_ch_* regression:      <N> tests, 0 failed
  (delta from the last accepted count: +<N> for this task's new cases)
velox_ch_filecache_seek_benchmark:      smoke-pass assertions passed;
                                        3 iterations, no crash,
                                        timing rows for cache-hit / miss / bypass
```

## Migration matrix

```text
<one row per CH scenario audited under "ClickHouse integration-test migration
  ownership": original CH test/scenario, Velox test name, explicit exclusion
  if not migrated>
```

## RED and false-green evidence

```text
<one row per material test: test name, RED log path + failing reason,
  false-green mutation log path + the exact branch/line reverted, restored
  green re-run log path>
```

## Production defects found

```text
None
(or describe each defect and the fix applied)
```

## Verification

```text
No E2E source contains GTEST_SKIP or DISABLED_ tests.
Final Velox E2E build exit code: 0 (mono and non-mono)
Accumulated regression: 0 unexpected failures
Benchmark build exit code: 0
Benchmark smoke-pass assertions: passed (no VELOX_CHECK abort)
git diff --check: no whitespace errors in Velox
  All three B1 production files (ReadBufferFromVeloxReadFile.h,
  ReadBufferFromVeloxReadFile.cpp, Metadata.cpp) changed only as documented
  in "## B1 corrective contract" and its execution-discovered defect
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
````

## Explicit exclusions

Do not implement in this task:

```text
Parquet / ORC / DWRF format reader integration tests — those require
  Velox format-reader fixtures and fall under Task 015 follow-up work once
  the core read-state-machine tests here pass cleanly.

Background prefetch via executor — load() is a no-op planning barrier;
  prefetch wiring is a post-MVP follow-up.

Per-query cache limit tests — those depend on QueryLimit (task 011) and the
  enableFilesystemQueryCacheLimit setting being wired into the Gluten
  request context, which is deferred.

SsdCache / checkpoint tests — the E2E suite uses a memory-backed cache only;
  SsdCache durability tests belong to a dedicated cache-persistence task.

Gluten builder and lifecycle integration — deferred to Tasks 018-019.

CH's rename-race lock+retry (`CachedOnDiskReadBufferFromFile.cpp:366-394`) and
  the externally-truncated-segment bypass (`CachedOnDiskReadBufferFromFile.cpp:448-477`)
  remain unported per
  `port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md`
  §2 ("Task 014 — Buffered input reader" verdict, "Minor holes vs CH reader
  robustness" bullet) and §6.1 ("014: direct-IO + background download (no
  test); `getCacheReadBuffer` rename-race / truncated / empty-file");
  `TruncatedOrInvalidCachedDataSourceRecovery` tests only the miss/refill
  behavior Velox already implements and does not require implementing CH's
  specific rename-race retry.

format-cpp-code.sh global run — do not run the formatter globally; apply
  clang-format-15 only to the new files created by this task if needed.
```
