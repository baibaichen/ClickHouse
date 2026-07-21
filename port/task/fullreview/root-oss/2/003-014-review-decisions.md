# Tasks 003-014 Full-Review Decisions — `root-oss` Review 2

## Status

```text
environment_profile: root-oss
review_round: 2
decision_status: approved
task_015_allowed: true
```

Evidence:

```text
port/task/fullreview/root-oss/2/evidence/003-014-consumer-contract-ledger.md
port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md
port/task/fullreview/root-oss/2/evidence/014-senior-review-corrective-result.md
port/task/fullreview/root-oss/2/003-014-targeted-b2-b5-closure.md
```

This file is authoritative over the evidence report's proposed disposition.

## B1 — direct-IO source with background download

### Decision

Defer the implementation and E2E evidence to Task 015.

Rationale:

- direct I/O is not the CH or Velox default;
- CH `ReadSettings.local_fs_settings.direct_io_threshold` defaults to zero;
- CH local cache-file reads explicitly use fresh default settings plus ordinary
  `pread`, without inheriting `direct_io_threshold`;
- Velox `ReadFile::directIo` defaults to false with alignment 1;
- the current port fails closed for the explicit direct-IO source combination:
  the background worker catches the alignment exception and marks the segment
  download failed without corrupting cached or returned data.

### Task-015 implementation recommendation

Task 015 owns the complete fix and E2E:

1. expose or reuse the handed-off reader's required direct-I/O alignment;
2. in Task-012 `CacheMetadata::downloadImpl`, allocate the background buffer
   from the Manager-owned MemoryPool with address and usable length aligned to
   that requirement;
3. preserve aligned file offset and read length;
4. if a final segment/tail cannot satisfy the direct-I/O length contract,
   explicitly skip background download for that segment instead of silently
   falling back to buffered I/O;
5. add a direct-IO source plus `backgroundDownloadThreads > 0` E2E that hands
   off a partially downloaded segment and proves successful background
   completion, or the explicit unaligned-tail skip;
6. add a false-green mutation that removes the alignment handling and makes the
   E2E fail for the expected alignment error.

B1 does not block starting Task 015 once B2-B5 are closed.

## B2 — `MoveEvictionPos` evidence

Owner: Task 011.

Decision: implement now.

Add a focused priority test equivalent to CH `MoveEvictionPos`:

- place the reserve/background cursor on a middle LRU entry;
- move/splice that entry;
- prove each cursor advances to the correct next entry;
- prove no candidate is skipped or revisited;
- false-green: disable `moveEvictionPosIfEqual` and prove the test fails.

No production change is expected unless the new test finds a defect.

## B3 — SLRU rollback and dynamic-resize evidence

Owner: Task 011.

Decision: implement now.

Add:

1. a test that arms
   `file_cache_slru_downgrade_fail_before_finalize`, forces a downgrade failure,
   and proves probationary/protected sizes, entry state, and iterator identity
   roll back;
2. a Task-011 equivalent of CH `SLRUDynamicResizeCorrectEviction`, with entries
   in both sub-queues and assertions that the resized priority satisfies its new
   limits;
3. false-green mutations that remove the rollback guard and one resize branch,
   with each test failing for the declared reason.

No production change is expected unless the tests find a defect.

## B4 — concurrent reader reset-before-complete evidence

Owner: Task 012/014.

Decision: implement now.

Add a deterministic two-thread test with barriers:

1. thread A owns and uses the remote reader;
2. thread B attempts extraction at the state-publication boundary;
3. thread A must call `resetRemoteFileReader` before
   `completePartAndResetDownloader` publishes the no-continuation state;
4. prove no thread can extract a reader still borrowed by another thread;
5. prove downloader state and reader ownership are coherent after completion;
6. false-green: reverse/remove the reset-before-complete ordering and prove the
   race test fails.

No sleeps or timing-only assertions are allowed.

## B5 — queue pipeline ownership

Owner: Task 012.

Decision: implement now.

The timed `tryPush(batch, 10)` and non-blocking `tryPop(batch)` behavior already
has Task-003 coverage. Add an SCC-owned queue-pipeline case to
`velox_ch_filecache_core_scc_test` so the Task-012 target independently proves
the exact call shapes used by the FileCache collector/loader path.

The test must:

- execute timed `tryPush`;
- execute non-blocking `tryPop`;
- prove FIFO value transfer;
- prove `finish` wakes/drains the pipeline;
- include a false-green mutation of one call shape.

No duplicate queue implementation is permitted.

## Accepted forward obligations

- The current `THROW`/`THROW_LOGICAL` collapse remains accepted. Reintroduce a
  typed user-vs-logical distinction only when a system-table or observability
  caller whose behavior depends on that distinction is ported.
- The real structured-errno producer remains a pre-release gate.
- `StatusFile` unclean-restart diagnostics remain a pre-release gate.
- F-CALLERID, scheduler recursive-mutex resolution, and real observability remain
  Task 017.
- SipHash mutation and malformed-character differential fuzz remain
  post-Task-019 evidence.

## Execution gate

Before Task 015:

1. amend Tasks 011 and 012/014 with B2-B5;
2. append `reopened_by_contract_audit` to affected receipts;
3. implement B2-B5 tests and any defects they expose;
4. run focused mono/non-mono and accumulated tests;
5. rerun the targeted Tasks 003-014 review rows;
6. record zero unresolved findings.

The targeted review completed with B2-B5 closed and zero unresolved findings.
The user explicitly approved continuous execution through Task 015.

```text
task_015_allowed: true
```
