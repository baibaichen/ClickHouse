# Task-014 Senior Review — Corrective Result — `root-oss` Review 2 Evidence

Read-only senior review of the accepted-but-uncommitted Task-014
(`FileCacheBufferedInput` / `FileCacheInputStream`) surfaced a Task-007/012 vs
Task-014 interaction blocker plus four integration/hardening gaps. This report
records the findings and the surgical corrective changes that resolve them. All
changes are **unstaged and uncommitted**; Task 015 / Gluten were not started.

- ClickHouse source of truth: `/root/oss/clickhouse` (branch `ch-filecache`),
  `src/Interpreters/Cache/` — `Metadata.cpp` (`downloadImpl`), `FileSegment.cpp`
  (`complete` / `getSizeForBackgroundDownload`), `src/IO/BufferBase.h`
  (`set(nullptr, 0)`), `src/IO/CachedOnDiskReadBufferFromFile.cpp` (predownload).
- Velox implementation: `/root/oss/velox` (branch `filecache`), port under
  `velox/ch/`. Tasks 003-013 committed; Task 014 (`velox/ch/Disks/`) untracked.
- Build env: `source /root/oss/velox-helper/env.sh` (no `-j`, no `nproc`).
  Mono `_build/debug`; non-mono `_build/debug-task012-nonmono`
  (`VELOX_MONO_LIBRARY=OFF`).

---

## Findings and resolutions

### F1 — Background handoff blocker (reopens Task 007 `set(nullptr, 0)`)

`ReadBufferFromFileBase::set(nullptr, 0)` called `restoreOwnedWindow()`, so after
a handoff detach `internalBuffer().empty()` was **false**. This diverges from CH
`BufferBase::set(nullptr, 0)` (empty internal buffer) and violates the Task-012
worker precondition `chassert(buf->internalBuffer().empty())` in
`CacheMetadata::downloadImpl`, forcing Task 014 to disable background download.

Resolution (Task 007): `set(nullptr, 0)` now `detach()`s (empty internal buffer,
`available()==0`, owned `BufferPtr` retained, file offset preserved). The owned
read window is restored **lazily** in `nextImpl` on a later normal read when an
owned buffer exists and no external buffer is attached. Direct-IO alignment,
right bound, terminal/cancel behavior, and the E3 handoff/buffer-end invariants
are preserved. Tests: `ReaderSetNullDetaches` /
`ReaderHandoffSatisfiesFileSegmentInvariants` now assert
`internalBuffer().empty()`; `ReaderLazilyReusesOwnedWindowAfterDetach` proves the
lazy owned-storage reuse.

### F2 — Background integration + latent use-after-free

Fix 2 required a production-path test with `backgroundDownloadThreads > 0`
proving the worker completes a handed-off partial segment without assertion,
stale pointer, duplicate source read, or downloader leak. Building it exposed a
**high-severity latent use-after-free**: the handed-off remote reader kept an
owned buffer charged to the query-scoped `MemoryPool`
(`owner_->memoryPool() == readerOptions.memoryPool()`), yet the reader lives in
the long-lived `FileSegment` and can be destroyed later on a background worker
thread. `velox::Buffer` holds a **raw** `MemoryPool*` and frees against it in its
destructor, so if the query pool is torn down before the async download drops the
reader, the release is a UAF. The owned buffer is pure liability here: a
handed-off reader always reads into an external buffer (the query output buffer,
or the worker's `memory`), so its owned buffer is never used for I/O.

Resolution (Task 007 + 014): new `ReadBufferFromVeloxReadFile::releaseOwnedBuffer`
frees the owned allocation and detaches every view on the calling thread (pool
still alive); `FileCacheInputStream::readNextChunk` calls it at the successful
handoff (`readerCanBeReused`). Tests:
`BackgroundDownloadCompletesHandedOffSegment` (functional completion; `spinUntil`
bounded wait, no sleeps; `source1->preadBytes()==40` proves no duplicate read; a
fresh cache-only read proves no leak/stale pointer) and
`BackgroundHandoffReleasesQueryPoolMemory` (distinct query pool; asserts
`queryPool->usedBytes()==0` after handoff and destroys the query pool **before**
the background download completes).

### F3 — Direct-IO predownload safety

Under direct IO, `prepareReadFromFileSegmentState` seeks to the (possibly
unaligned) `currentWriteOffset` before predownload and issues gap-sized reads —
either throwing on the unaligned direct-IO seek/read or (if naively "made to
work") silently degrading to buffered IO.

Resolution: `createReadFromFileSegmentState` skips the optional predownload when
`directIoAlignment_ > 1` and `currentWriteOffset % align != 0 ||
bytesToPredownload % align != 0`, releasing the elected downloader and reading the
segment through the normal aligned bypass path at `offset` (still
alignment-validated by the reader). No buffered-IO fallback, no fabricated size.
Test: `DirectIoPredownloadSkipsWhenUnaligned`.

### F4 — Remaining evidence

- **F4a** `FileCacheBufferedInput::isBuffered` computes the range end with
  `FileCacheUtils::checkedAdd(offset, length, ...) - 1` up front, rejecting an
  overflowing `offset + length` before any wrap-prone arithmetic. Test:
  `IsBufferedRejectsOverflow` (uses `VELOX_ASSERT_THROW` with a message match so
  the rejection is proven to originate from `isBuffered`'s own guard rather than
  incidentally from `FileCache::get`'s `Range`).
- **F4b** `DiskFailureSkipContinuesAcrossSegments`: a mid-region cache-write
  failure across a multi-segment region is skipped (`skip_cache_on_disk_failure`)
  and the read continues, with a later segment still caching.
- **F4c** The `FileCacheInputStream`/owner (`FileCacheBufferedInput`) lifetime
  requirement and the handed-off-reader pool contract are documented in the
  public headers. No ownership redesign beyond the code-safety `releaseOwnedBuffer`
  (which the review found necessary).

---

## Verification

```text
velox_ch_io_test (Task 007):                     33/33  (mono + non-mono)
velox_ch_filecache_buffered_input_test (014):    24/24  (mono + non-mono)
velox_ch_filecache_core_scc_test (012):          101/101 (mono)
velox_ch_filecache_manager_test (013):           42/42  (mono)
accumulated ctest -R ^velox_ch_ (mono):          13/13
0 disabled, 0 skipped anywhere. Background tests re-run 12-15x: no flakiness.
git diff --check: clean (tracked + untracked). Gluten: untouched.
```

### False-green mutations (build + run + restore byte-for-byte; all CAUGHT)

```text
M1  set(nullptr,0): detach -> restoreOwnedWindow  => ReaderSetNullDetaches FAILED
M2  handoff: always withdraw reader               => BackgroundDownloadCompletesHandedOffSegment FAILED
M3  remove direct-IO predownload skip             => DirectIoPredownloadSkipsWhenUnaligned FAILED
M4  isBuffered checkedAdd -> raw offset+length     => IsBufferedRejectsOverflow FAILED
M5  remove skip_cache_on_disk_failure              => DiskFailureSkipContinuesAcrossSegments FAILED
M6  remove releaseOwnedBuffer at handoff           => BackgroundHandoffReleasesQueryPoolMemory SIGSEGV (real UAF)
```

### Files changed (all unstaged)

```text
Task-007 (tracked):
  velox/ch/IO/ReadBufferFromVeloxReadFile.h
  velox/ch/IO/ReadBufferFromVeloxReadFile.cpp
  velox/ch/IO/tests/IoAdaptersTest.cpp
Task-014 (untracked, under velox/ch/Disks/IO/):
  FileCacheInputStream.cpp, FileCacheInputStream.h
  FileCacheBufferedInput.cpp, FileCacheBufferedInput.h
  tests/FileCacheBufferedInputTest.cpp
```

## Status

```text
All senior-review findings resolved. Changes unstaged/uncommitted.
Task 015 / Gluten NOT started. The mandatory Tasks 003-014 full-review A-ledger
and D-review deliverables in this directory remain to be produced separately.
```

## Controller disposition

```text
controller_status: accepted
Task-007 corrective commit: 1e3cc3209
Task-014 implementation commit: b92a0ae3a
Task-015 allowed: false
```

The corrective integration is accepted with zero unresolved Blocker/Major
findings. The next action is the mandatory Tasks 003-014 full review.
