# Task 020 Result: `FileCache` Core Baseline and Velox IO Uplift Review

## Review status

```text
task: 020
review_status: approved
reviewed_repository: https://github.com/baibaichen/velox.git
reviewed_branch: filecache2
reviewed_range: 5785a43a..fc37a7eb
implementation_head: fc37a7eb
pre_squash_final_head: c27364763
pre_squash_production_remediation_head: 228d9661c
pre_squash_review_evidence_head: 093d3ddaf
pre_squash_initial_review_result_commit: ddf00a44c29
```

The initial review found one business-read correctness blocker, two production
contract gaps, one core-test pollution bug and missing evidence for material
failure paths. The remediation and two follow-up test-only commits close all
findings and evidence gaps. Task 020 is approved.

## Reviewed implementation

```text
c609572d9  R1: relocate IO integration tests
60a0c55dc  R2-1: FileCacheCoalescedLoad type skeleton
c4dbd10cf  R2-2: dual-role FileCacheInputStream + takeLastOutputBuffer
09d7bd0d5  R2-3: internal-stream loadData + shared lookup + duplicate handling
53278625c  R2-4: stream/load bindings + prefetch/demand triggering
8fad26c09  R2-5: remove warmSourceGroup + cancel planned loads
d9c51f62b  R2-6: QueryLimit + prefetch statistics + warm-test rewrites
eb2fb9996  R3: remove wait-test dependency on the FileSegment hook
8fde74b1a  R4: restore FileCache core baseline
2f13b4b27  R5 C3/C4: source latency + offset assertions
366651dec  R5 C6: fold preload into FileCacheInputStream
51e3c14ff  R5 C7: fix DOWNLOADING geometric coverage
```

Authoritative design:

```text
port/design/filecache-buffered-input-review-remediation.md
  9cfdb9e4de2  final FileCacheCoalescedLoad architecture
  4087348a07f  Direct-aligned business/internal stream ownership
  7567b7c2371  intentional batch getData(requestIndices) divergence
```

## Verified gates

### Core production boundary

The following files have zero diff relative to `5785a43a`:

```text
velox/ch/Interpreters/FileCache/FileCache.h
velox/ch/Interpreters/FileCache/FileCache.cpp
velox/ch/Interpreters/FileCache/FileSegment.h
velox/ch/Interpreters/FileCache/FileCacheErrnoException.h
```

`FileSegment.cpp` has only the two approved changes:

```text
include velox/ch/IO/FileCacheLocalWriteFile.h
default writer factory: LocalWriteFile -> FileCacheLocalWriteFile
```

The removed core warm APIs/state and wait hook are absent:

```text
submitWarm
inflightWarmForTest
warm_mutex / warm_cv / warm_cancel_source
DownloaderLease
FileSegment::wait::beforeWait
```

### Architectural direction

The production implementation follows the approved high-level split:

```text
FileCacheBufferedInput:
  planning, grouping, stream/load bindings, prefetch/demand trigger

FileCacheCoalescedLoad:
  group lifecycle, request RAM ownership, all-or-nothing getData

internal FileCacheInputStream:
  segment lookup, downloader election/wait, source/local read, reserve/write,
  errno handling

FileCache core:
  unchanged state machine
```

The following design decisions are present:

- business streams retain the Direct-aligned `bufferedInput_` back-pointer;
- internal streams use shared `FileCacheReadContext` and may outlive the input;
- both prefetch and demand groups create `FileCacheCoalescedLoad`;
- request mapping uses stable `requestIndex` plus exact `memberChunks`;
- `getData(requestIndices)` performs an all-or-nothing `BufferPtr` ownership move;
- duplicate regions materialize once and copy only for the duplicate request;
- special cache modes share the same lookup policy as `FileCacheInputStream`;
- the group holder is released on success and exception;
- `FileCacheQueryIdScope` restores per-query download-limit enforcement;
- whole-file preload still returns RAM slices rather than reading `FileSegment`.

## Findings

### B1 — Coalesced RAM delivery does not establish a published window

**Severity:** blocker
**Files:** `velox/ch/Disks/IO/FileCacheInputStream.cpp:1099-1121`,
`1172-1184`, `1313-1321`

`serveCoalescedWindow` returns a pointer from `coalescedWindows_` and advances
`position_`, but it does not set:

```text
outputBufferStart_
outputBufferSize_
offsetInOutputBuffer_
currentWindowBase
```

`BackUp` operates exclusively on those published-window members.

Concrete first-window trace:

```text
business Next
  -> serveCoalescedWindow returns N bytes
  -> offsetInOutputBuffer_ is still 0

decoder consumes N - 10 bytes
  -> BackUp(10)
  -> VELOX_CHECK_LE(10, 0)
  -> exception
```

If an ordinary buffer was published before the coalesced window, the stale
ordinary-window metadata can instead make `BackUp` succeed and the next `Next`
return bytes from the old buffer. That is a data-correctness failure.

Retained RED evidence:

```text
test:
  FileCacheFormatE2ETest.UncompressedDwrfReadFullyBacksUpCoalescedWindow

business path:
  dynamically generated > 1 MiB uncompressed DWRF
  -> DwrfRowReader
  -> StringDirectColumnReader::next
  -> SeekableInputStream::readFully
  -> FileCacheInputStream::BackUp

observed failure:
  FileCacheInputStream.cpp:1318
  (5021 vs. 0) BackUp beyond output buffer
```

The file is generated by the test and is intentionally larger than the default
1 MiB footer speculative read. This prevents reader construction from warming
the entire file before column-stream planning and forces the business read
through a coalesced RAM window.

### M1 — Preloaded `enqueue` bypasses `ScanTracker` reference/read accounting

**Severity:** major
**Files:** `velox/ch/Disks/IO/FileCacheBufferedInput.cpp:163-172`,
`1000-1021`

When `preloaded()` is true, `enqueue` returns `makePreloadedStream` before it:

```text
copies StreamIdentifier to TrackingId
calls ScanTracker::recordReference
passes the TrackingId to FileCacheInputStream
```

`makePreloadedStream` constructs the stream with the default empty tracking id,
so RAM delivery also does not call `recordRead` for the requested stream.

This violates the approved section 11.6 contract and prevents later access
history from reflecting actual preload consumption.

Retained Direct-versus-FileCache RED evidence:

```text
test:
  FileCacheBufferedInputTest.PreloadedStreamTracksReferenceAndReadLikeDirect

same input:
  64-byte file, whole-file preload, same region, same StreamIdentifier,
  full stream consumption

DirectBufferedInput:
  payload correct
  referencedBytes = 64
  readBytes = 64

FileCacheBufferedInput:
  payload correct
  referencedBytes = 0
  readBytes = 0
```

### M2 — Delivered-byte accounting is not unified at `Next`

**Severity:** major
**Files:** `velox/ch/Disks/IO/FileCacheInputStream.cpp:996-1008`,
`1117-1121`, `1161-1167`, `1172-1184`

The implementation records business delivery separately in:

```text
readFromCurrentSegment
serveCoalescedWindow
servePreloadWindow
```

The pending-window fast path does not record delivery. After `BackUp` or a
buffer-local seek, bytes returned again by `Next` are therefore omitted from
`ScanTracker`. This is the exact drift section 11.6 required the unified stream
to remove.

Retained Direct-versus-FileCache RED evidence:

```text
test:
  FileCacheBufferedInputTest.BackedUpBytesAreRecordedAgainLikeDirect

same input:
  64-byte stream
  -> Next returns 64
  -> BackUp(16)
  -> Next returns the same 16-byte tail again

DirectBufferedInput:
  replay payload correct
  referencedBytes = 64
  readBytes = 80

FileCacheBufferedInput:
  replay payload correct
  referencedBytes = 64
  readBytes = 64
```

### M3 — Core test factory guard restores the wrong production default

**Severity:** major
**File:** `velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp:295-313`

`ScopedWriteFileFactory` restores:

```cpp
velox::LocalWriteFile(path, false, false, true)
```

The production default is now:

```cpp
FileCacheLocalWriteFile(path)
```

The two writers intentionally differ in disk-error exception typing. After this
guard exits, every later test in the process runs with a non-production factory,
so disk-failure behavior can be silently mis-tested.

## Missing or insufficient test evidence

### T1 — Prefetch RAM handoff can falsely pass through local disk

`ColdPrefetchExecutesImmediately`
(`FileCacheBufferedInputBuilderTest.cpp:2806`) proves that the business read does
not re-read the source, but does not prove it consumes the prepared RAM buffer.
A wrong implementation that reads the newly written local `FileSegment` also
passes.

### T2 — Prefetch failure to demand fallback is untested

No focused test proves:

```text
prefetch internal source failure
-> load becomes kCancelled
-> no partial request payload is published
-> business Next retries through the ordinary demand path
-> data is correct and no downloader/waiter leaks
```

This is a material `CoalescedLoad` failure-path contract, not covered by the
existing single-stream source-failure test.

### T3 — Once-only `getData` consumption is untested

No test verifies a second `getData` call for an already consumed request returns
`std::nullopt`. The `consumed` invariant is central to preventing repeat
delivery.

### T4 — Null-executor prefetch lazy trigger is only half-tested

`NullExecutorSkipsWarm` verifies that `load` performs no IO, but never calls the
stream's first `Next`. It does not prove the planned prefetch load is then
triggered lazily.

### T5 — C7 partial-state branches lack focused regression coverage

The `DOWNLOADING` branch has focused coverage, but the new
`PARTIALLY_DOWNLOADED` and
`PARTIALLY_DOWNLOADED_NO_CONTINUATION` classification branches do not.

## Task 020 required remediation work

### R1 — Make the three retained RED tests pass without changing them

The following tests are frozen in Velox commit
`093d3ddaf7b709fad303fbcdb04c142585c477f5`:

```text
FileCacheFormatE2ETest.UncompressedDwrfReadFullyBacksUpCoalescedWindow
FileCacheBufferedInputTest.PreloadedStreamTracksReferenceAndReadLikeDirect
FileCacheBufferedInputTest.BackedUpBytesAreRecordedAgainLikeDirect
```

Do not change their test bodies, fixtures, inputs, control implementations or
assertions to obtain green results. Production changes must satisfy these exact
observations:

```text
B1:
  the uncompressed DWRF scan returns all expected rows
  no BackUp exception
  BackUp and the following Next remain within the same coalesced RAM window

M1:
  FileCache preload returns the same payload as Direct
  referencedBytes = 64
  readBytes = 64

M2:
  FileCache returns the same 16-byte replay tail as Direct
  referencedBytes = 64
  readBytes = 80
```

The implementation must preserve the approved FileCache core production
boundary: no new production changes to `FileCache.h`, `FileCache.cpp`,
`FileSegment.h` or `FileCacheErrnoException.h`; `FileSegment.cpp` remains limited
to the approved typed-writer integration.

### R2 — Restore the real production writer after the fault-injection test

`ScopedWriteFileFactory` in `FileSegmentTest.cpp` must restore
`FileCacheLocalWriteFile`, not `velox::LocalWriteFile`. The restored writer must
therefore preserve production's typed `FileCacheErrnoException` behavior for
later tests in the same process. The test seam must not leak a different writer
based on test order.

### R3 — Add T1 prefetch RAM-handoff evidence

Add a focused business-stream test with these assertions:

```text
cold prefetch completes before business Next
source read count does not increase during business Next
IoStatistics::ssdRead does not increase during business Next
returned payload is correct
```

Checking only the source read count is insufficient because a fallback read from
the newly written local `FileSegment` also avoids the source. The zero
`ssdRead` delta is required to distinguish RAM handoff from a local disk hit.

### R4 — Add T2 prefetch-failure-to-demand-fallback evidence

Use a deterministic failing `ReadFile` or executor-controlled source; do not add
a production test hook or use sleeps. Prove:

```text
prefetch source read fails
the coalesced load publishes no partial request payload
the business stream retries through the ordinary demand path
the demand source read succeeds and returns the exact payload
no downloader or waiter remains after completion
```

The test must fail if the background exception escapes into the business read,
if partial RAM data is published, or if the failed prefetch leaves the segment
stuck.

### R5 — Add T3 once-only `getData` evidence

After a successful load:

```text
first getData({requestIndex}) returns the exact payload
second getData({requestIndex}) returns std::nullopt
the second call performs no source or local read
```

This must exercise the real `FileCacheCoalescedLoad` request and its `consumed`
state, not a copied test implementation.

### R6 — Add T4 null-executor lazy-trigger evidence

Retain the stream returned by `enqueue`, call `load` with a null executor, then
prove:

```text
before first Next:
  a prefetch group is planned
  no source/local IO occurred

on first Next:
  the planned load executes synchronously
  the exact payload is returned
  source IO occurs exactly through this trigger
```

`NullExecutorSkipsWarm` alone remains insufficient because it stops before the
business stream consumes anything.

### R7 — Add T5 partial-state classifier evidence

Construct the states through the real `FileSegment` API, without a production
hook, and assert the state before classification:

```text
PARTIALLY_DOWNLOADED with an insufficient downloaded prefix
  -> ChunkCacheState::kMiss

PARTIALLY_DOWNLOADED_NO_CONTINUATION with an insufficient downloaded prefix
  -> ChunkCacheState::kDownloading
```

Each case must classify a chunk that extends beyond the downloaded prefix. A
test whose requested sub-range is already fully covered by the prefix does not
exercise the missing branch.

### R8 — Re-run acceptance and update this review

Run the targeted regressions and all Task 020 gates. Keep separate logs under
the build directory. Re-check the core production boundary against `5785a43a`.
Update this result with the fixing commit range, exact test counts and the final
verdict. Do not push as part of Task 020 remediation.

## Reported test results

The implementation author reported these gates green:

```text
velox_ch_filecache_connector_test:       51
velox_ch_filecache_buffered_input_test:  36
velox_ch_filecache_e2e_test:             21
velox_ch_cancellation_test:               9
velox_ch_filecache_hit_metrics_test:      7
velox_ch_filecache_core_scc_test:        49
velox_ch_filecache_manager_test:         20
velox_ch_filecache_format_e2e_test:       2
velox_ch_io_test:                        33
```

This review verified code/diff structure and the core diff gate. It did not
rerun all full binaries. It built the two affected test targets and independently
reproduced the three expected failures introduced by `093d3ddaf`:

```text
tracking regressions: 2 tests, 2 expected failures
format regression:    1 test, 1 expected failure
```

The logs are:

```text
cmake-build-debug-gcc13/build_task020_retained_red_tests.log
cmake-build-debug-gcc13/test_task020_tracking_reds.log
cmake-build-debug-gcc13/test_task020_backup_red.log
```

M3 and T1-T5 remain unclosed.

## Remediation result (implementation author)

All eight remediation items (R1-R8) were addressed. The following are the
pre-squash audit commits, preserved by backup branch
`backup/filecache2-pre-squash-fc37a7eb`; the reviewed final implementation is
the single Velox commit `fc37a7eb`:

```text
228d9661c  Rem-R1: ScanTracker + coalesced-window BackUp parity with Direct
10c74efa8  Rem-R2: restore FileCacheLocalWriteFile in the test writer seam
45ae3f691  Rem-R3..R7: coalesced-load evidence tests T1-T5
```

Item-by-item:

```text
R1  M1/M2/B1: the three frozen RED tests pass without any change to their bodies.
    M1 PreloadedStreamTracksReferenceAndReadLikeDirect  -> referencedBytes 64, readBytes 64
    M2 BackedUpBytesAreRecordedAgainLikeDirect          -> referencedBytes 64, readBytes 80
    B1 UncompressedDwrfReadFullyBacksUpCoalescedWindow  -> no BackUp exception, all rows
    Root causes fixed in Disks/IO only: enqueue records the ScanTracker reference
    unconditionally before the preload fast-return and threads the tracking id into
    the preload stream; the Next pending-window fast path re-records replayed bytes
    after BackUp; serveCoalescedWindow publishes the non-owning window metadata so a
    BackUp inside a coalesced RAM window no longer throws. Initial delivery records a
    window once; only replayed bytes are re-counted (no double count).
R2  ScopedWriteFileFactory now reinstalls FileCacheLocalWriteFile, not
    velox::LocalWriteFile, so no writer leaks across tests by order.
R3  T1 PrefetchServesBusinessFromRamNotLocalDisk: source read AND ssdRead deltas 0.
R4  T2 PrefetchFailureFallsBackToDemandPath: no partial payload, demand re-reads,
    no downloader/waiter left (test-local GatedFailReadFile, no production hook).
R5  T3 GetDataIsConsumedOnce: second getData returns nullopt with no extra IO.
R6  T4 NullExecutorPlannedLoadTriggersOnFirstNext: planned load runs on first Next.
R7  T5 partial-state classifier: PARTIALLY_DOWNLOADED short prefix -> kMiss;
    PARTIALLY_DOWNLOADED_NO_CONTINUATION short prefix -> kDownloading, via the real
    FileSegment API (setDownloadFinishedWithoutContinuation), state asserted first.
```

Independent RED reproduction (neutralize production, observe red, revert): the
three frozen tests, T1 (ssdRead is the sole RAM-vs-disk discriminator: 131072 vs 0
when serveCoalescedWindow is disabled), and T5-a were each confirmed load-bearing.

Core production boundary re-checked against `5785a43a`: `FileCache.{h,cpp}`,
`FileSegment.h`, `FileCacheErrnoException.h` diff empty; `FileSegment.cpp` limited
to the two approved typed-writer lines. Remediation touched no core production file
(R1 is Disks/IO only; R2 and R3-R7 are test-only).

All gates re-run green after remediation:

```text
velox_ch_filecache_connector_test:       57   (was 51; +6 evidence tests)
velox_ch_filecache_buffered_input_test:  38   (+2 M1/M2)
velox_ch_filecache_e2e_test:             21
velox_ch_cancellation_test:               9
velox_ch_filecache_hit_metrics_test:      7
velox_ch_filecache_core_scc_test:        49
velox_ch_filecache_manager_test:         20
velox_ch_filecache_format_e2e_test:       3   (+1 B1)
velox_ch_io_test:                        33
```

At that point the commits were not pushed and awaited reviewer re-check.

## Reviewer re-check

Reviewed Velox range:

```text
093d3ddaf..45ae3f691
```

The production remediation is correct:

- B1, M1 and M2 are resolved without changing the three frozen RED tests;
- the coalesced RAM slice now publishes the window metadata used by `BackUp`;
- preload records the reference before its fast return and carries the tracking id
  into the business stream;
- replayed bytes are recorded only when `BackUp` or a buffer-local seek reopens a
  published window, with no initial-delivery double count;
- M3 is resolved by restoring `FileCacheLocalWriteFile`;
- T1, T3 and both T5 classifier cases provide the requested evidence.

The reviewer independently reran all nine acceptance binaries:

```text
velox_ch_filecache_connector_test:       57
velox_ch_filecache_buffered_input_test:  38
velox_ch_filecache_e2e_test:             21
velox_ch_cancellation_test:               9
velox_ch_filecache_hit_metrics_test:      7
velox_ch_filecache_core_scc_test:        49
velox_ch_filecache_manager_test:         20
velox_ch_filecache_format_e2e_test:       3
velox_ch_io_test:                        33
total:                                  237
```

All passed with zero failures and zero skipped tests. The reviewer also rechecked
the core production boundary against `5785a43a`; it remains unchanged.

Two evidence items remained open after the first reviewer re-check. Both were
closed by test-only follow-up commits.

### RR1 — T4 originally passed when the planned-load trigger was removed

**Severity:** major test-evidence gap

**File:** `velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp:3488-3531`

`NullExecutorPlannedLoadTriggersOnFirstNext` proves that `load` performs no eager
IO and that the later business read returns correct data, but it does not prove
that the first `Next` executes the planned `FileCacheCoalescedLoad`.

The reviewer neutralized
`FileCacheInputStream::triggerCoalescedLoadIfNeeded` under batch gdb by forcing
the function to return immediately on every call. The breakpoint was hit twice,
yet the test still passed:

```text
[  PASSED  ] 1 test.
trigger_breakpoint_hits=2
```

With the trigger absent, `serveCoalescedWindow` finds no RAM window and the stream
falls through to the ordinary cold demand path. That path also reads the source,
returns the exact payload and makes `preadCount > 0`, satisfying every current
post-`Next` assertion. Therefore R6/T4 was not closed by the first remediation.

Evidence log:

```text
cmake-build-debug-gcc13/reviewer_task020_t4_neutralized.log
```

**Closed by (pre-squash audit commit):**
`3b4296cb6b39edf0470c949d20d699cc501903eb`

The strengthened test calls `Next` once and asserts that the complete 128 KiB
planned coalesced load has already downloaded, rather than allowing a one-segment
64 KiB demand read to satisfy the case. It is load-bearing:

```text
normal implementation:
  57 connector tests pass

triggerCoalescedLoadIfNeeded neutralized:
  EXPECT_FALSE(downloadedAfterFirstNext < n)
  Actual: true
  Expected: false
  downloaded 65536 of 131072
```

Evidence logs:

```text
cmake-build-debug-gcc13/test_t4_expect_false_normal.log
cmake-build-debug-gcc13/test_t4_expect_false_neutralized.log
cmake-build-debug-gcc13/test_t4_expect_false_connector_full.log
```

### RR2 — T2 originally did not exercise failure after partial materialization

**Severity:** major test-evidence gap

**File:** `velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp:3347-3411`

`PrefetchFailureFallsBackToDemandPath` proves basic first-read failure recovery:
the background exception is contained, the load is cancelled, and a later demand
read succeeds. It does not prove the required all-or-nothing publication
contract.

Concrete trace:

```text
file size:             128 KiB
default loadQuantum:   8 MiB
load requests:         1
injected failure:      first source read
materialized buffers:  0
```

The first internal `Next` throws before any request or buffer has completed, so
the test cannot detect an implementation that publishes each successfully
materialized request incrementally and then fails on a later request. Its
`downloadedBytes == 0` assertion observes disk residency, not RAM publication,
and is necessarily true when the first read fails. Therefore the demand-fallback
part of R4/T2 was covered, but its no-partial-publication part was unproven at
that re-check.

**Closed by (pre-squash audit commit):**
`b8fc70d475ec2cba9921dbb190282e312ec289f4`

The strengthened test creates three adjacent 64 KiB requests in one prefetch
load. Source read 1 materializes and persists request A; source read 2 fails
while materializing request B. It then proves:

```text
load state = kCancelled
getData({A}) = std::nullopt
A persisted bytes = 64 KiB
B persisted bytes = 0
A business read uses 64 KiB local ssdRead, not partially published RAM
B business read retries the source and returns the exact payload
```

The complete connector binary passes all 57 tests after this test-only change.

Evidence logs:

```text
cmake-build-debug-gcc13/test_t2_atomic_direct.log
cmake-build-debug-gcc13/test_t2_atomic_connector_full.log
cmake-build-debug-gcc13/reviewer_task020_connector_final.log
```

Reviewer gate logs:

```text
cmake-build-debug-gcc13/reviewer_task020_connector.log
cmake-build-debug-gcc13/reviewer_task020_buffered_input.log
cmake-build-debug-gcc13/reviewer_task020_e2e.log
cmake-build-debug-gcc13/reviewer_task020_cancellation.log
cmake-build-debug-gcc13/reviewer_task020_hit_metrics.log
cmake-build-debug-gcc13/reviewer_task020_core_scc.log
cmake-build-debug-gcc13/reviewer_task020_manager.log
cmake-build-debug-gcc13/reviewer_task020_format_e2e.log
cmake-build-debug-gcc13/reviewer_task020_io.log
```

## Final verdict

```text
status: APPROVE
note: B1/M1/M2/M3 and T1-T5 are closed; 237 acceptance tests pass; core production boundary remains unchanged
```
