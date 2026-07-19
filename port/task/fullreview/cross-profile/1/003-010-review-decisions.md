# Tasks 003-010 Cross-Profile Review Decisions — Round 1

## Status and authority

```text
decision_status: approved
profiles: root-oss, home-chang
task_011_allowed: false
```

Frozen implementation baselines:

```text
ClickHouse source: da28e83e8b3cb69090624b0a0b1f13cd78c13279
Velox:            89039901aa4287ce811a3b1628867b0796c76678
```

Authority order:

```text
CH source and real callers
  -> this cross-profile decisions file
  -> numbered task contract
  -> profile-specific review evidence
```

Profile artifacts remain evidence:

```text
port/task/fullreview/root-oss/1/003-010-review-decisions.md
port/task/fullreview/root-oss/1/evidence/011-012-consumer-contract-ledger.md
port/task/fullreview/root-oss/1/evidence/003-010-full-review-result.md

port/task/fullview/home-chang/1/003-010-review-decisions.md
port/task/fullview/home-chang/1/003-010-consumer-contract-ledger.md
port/task/fullview/home-chang/1/003-010-full-review.md
```

When profile artifacts disagree, CH source and real consumers decide. A profile
does not win because it is newer or because it found more issues.

## Profile reconciliation

| Topic | Authoritative decision |
|---|---|
| `ProfileEvents` / `CurrentMetrics` | The root-oss B1/B2 finding governs. Complete the no-op name surface before Task 011. |
| Task-009 `ShardedMap` | Keep `F14FastMap`; the user approved the no-reference-escape contract. |
| Settings `.changed` | Accept the Task-010 representation, but Task 012 must compare each new value with the current value and must not use field presence as the reload condition. |
| `StatusFile` unclean-restart diagnostics | Home-chang R3 is valid. Restore it in a separate pre-release gate; it does not block Tasks 011-014. |
| `sipHash128` golden proof | Home-chang R4 is valid evidence debt. Defer a CH-derived golden-vector test and mutation probe until after Task 019. |
| Malformed-character parser proof | Home-chang R5 is valid evidence debt. Defer differential fuzz against CH `unhexUInt` until after Task 019. |
| Structured errno | The root-oss decision governs. Task 012 consumes a typed errno contract; the real errno producer is a pre-release gate. No reconcile-every-exception fallback is allowed. |
| Error-code collapse | Accept the Velox exception baseline. Reintroduce a typed subtype at any call site whose behavior distinguishes `NOT_ENOUGH_SPACE` from `LOGICAL_ERROR`. |
| Caller identity | The current `None:<tid>` remains sufficient for core downloader identity. Restore `None:<threadname>:<tid>` in Task 017. |
| Scheduler recursive mutex | It does not change current scheduler behavior. Resolve or explicitly register it in Task 017. |
| Allowed-root authorization | Accept the additive path-containment security boundary from Task 010. |

## Required before Task 011

### Task 003 B1 — no-op `ProfileEvents` names

- Add these 31 names referenced by the current CH FileCache source and absent
  from the Velox shim:

```text
FileSegmentFailToIncreasePriority
FileSegmentHolderCompleteMicroseconds
FileSegmentIncreasePriorityMicroseconds
FileSegmentLockMicroseconds
FilesystemCacheBackgroundDownloadQueuePush
FilesystemCacheBackgroundEvictedBytes
FilesystemCacheBackgroundEvictedFileSegments
FilesystemCacheBackgroundRemovedInvalidatedEntries
FilesystemCacheCreatedKeyDirectories
FilesystemCacheDowngradedFileSegments
FilesystemCacheEvictMicroseconds
FilesystemCacheEvictedBytes
FilesystemCacheEvictedFileSegments
FilesystemCacheEvictionReusedIterator
FilesystemCacheEvictionSkippedEvictingFileSegments
FilesystemCacheEvictionSkippedFileSegments
FilesystemCacheEvictionSkippedMovingFileSegments
FilesystemCacheEvictionTries
FilesystemCacheFailToReserveSpaceBecauseOfCacheResize
FilesystemCacheFailedEvictionCandidates
FilesystemCacheFreeSpaceKeepingThreadErrors
FilesystemCacheFreeSpaceKeepingThreadRun
FilesystemCacheFreeSpaceKeepingThreadWorkMilliseconds
FilesystemCacheHoldFileSegments
FilesystemCacheIdleClientEvictions
FilesystemCacheInvalidatedEntriesCleanupThreadWorkMilliseconds
FilesystemCacheLoadMetadataMicroseconds
FilesystemCacheLockKeyMicroseconds
FilesystemCacheLockMetadataMicroseconds
FilesystemCacheLockOriginPoolMicroseconds
FilesystemCacheUnusedHoldFileSegments
```

- Keep event increments and timers no-op in this phase.
- Add one compile-coverage test for the complete required name set.
- Delete one required name as a false-green mutation and prove the coverage
  test fails.
- Real event counters remain Task 017.

### Task 003 B2 — no-op `CurrentMetrics` names

Add only:

```text
FilesystemCacheElements
FilesystemCacheInvalidatedElements
FilesystemCachePriorityQueueElements
FilesystemCacheSize
FilesystemCacheKeys
```

Do not add:

```text
FilesystemCacheEvictionThreads
FilesystemCacheEvictionThreadsActive
FilesystemCacheEvictionThreadsScheduled
FilesystemCacheOvercommitUsers
```

The accepted thread-pool mapping drops the three constructor-only metrics, and
overcommit remains excluded. Keep all metric operations no-op until Task 017.
Require complete compile coverage and a delete-one-name false-green mutation.

### Structure registrations

Record before a Task-011 Worker starts:

- SD1: Task-009 `F14FastMap`, with the approved no-reference-escape contract.
- SD2: `absl::flat_hash_map/set` to `F14FastMap/F14FastSet`.
- SD3: `KeyMetadata` remains `std::map`.
- SD4: an F14 metadata bucket is allowed only when no map-slot reference,
  iterator, or address survives mutation.
- SD5: LRU queues and `FileSegments` remain `std::list`.
- SD6: CH thread pool to the injected Folly executor/backlog/future structure.
- SD7: CH delay thread and multimap to `Timekeeper` and Future continuations.
- SD9: CH-owned `Memory<>` to MemoryPool-charged `BufferPtr`.

## Task-011 contract decisions

Task 011 is migration-only and intentionally has no green build. Task 012 closes
the compile/link SCC immediately afterward.

Mandatory contract:

- Port only the non-overcommit `CacheUsage` subset reachable from the center
  SCC: the `EvictionInfo` lifetime pin, `CacheUsageStatGuard`, and base
  no-op/throw hooks.
- Do not port `CacheUsagePerUser` bodies or add
  `FilesystemCacheOvercommitUsers`.
- Map only CH `absl::flat_hash_map/set` containers to F14. Keep
  `original_queue_types` as `std::unordered_map`.
- Use `F14FastMap`/`F14FastSet`; node variants are unnecessary.
- Preserve `std::list` for LRU/SLRU entries and cursors.
- Preserve state transitions, zero-size accounting, independent eviction
  cursors, SLRU iterator identity, split-cache rollback, and
  `afterEvictWrite`-before-`afterEvictState`.
- If a reserve/eviction caller distinguishes `NOT_ENOUGH_SPACE` from
  `LOGICAL_ERROR`, use a typed subtype rather than one opaque failure.
- Stop at the dependency gate if any required event/metric name is absent.

Task-011 event names:

```text
FilesystemCacheBackgroundRemovedInvalidatedEntries
FilesystemCacheDowngradedFileSegments
FilesystemCacheEvictionReusedIterator
FilesystemCacheEvictionSkippedEvictingFileSegments
FilesystemCacheEvictionSkippedFileSegments
FilesystemCacheEvictionSkippedMovingFileSegments
FilesystemCacheEvictionTries
FilesystemCacheEvictMicroseconds
FilesystemCacheFailedEvictionCandidates
```

Task-011 metric names:

```text
FilesystemCacheElements
FilesystemCacheInvalidatedElements
FilesystemCachePriorityQueueElements
FilesystemCacheSize
```

## Task-012 contract decisions

### Infrastructure mappings

| CH dependency | Approved Velox mapping | Limits |
|---|---|---|
| `Memory<>` | MemoryPool `BufferPtr` | Preserve size, reuse, lifetime, and accounting. |
| `SCOPE_EXIT` | Folly scope guard | Run on normal and exceptional exit. |
| `Stopwatch` | `using Stopwatch = facebook::velox::DeltaCpuWallTimeStopWatch` | Only the two FileCache call sites that construct and read one wall snapshot. Use `elapsed().wallNanos / 1'000'000`; no `stop/reset/restart` contract. |
| `callOnce` / `OnceFlag` | `std::call_once` / `std::once_flag` | Exact CH mapping. Preserve FileCache-level `init_exception` publication and rethrow. |

### Typed errno consumer

Task 012 defines and consumes:

```text
FileCacheErrnoException
  getErrno() -> int
```

`FileSegment::write`:

```text
catch FileCacheErrnoException:
  mark download failed
  if errno is ENOSPC or EDQUOT:
    if downloaded_size is zero:
      remove the failed new file
    otherwise:
      read physical file size
      require downloaded_size <= physical_size <= reserved_size
      set downloaded_size to physical_size
  rethrow the original exception

catch any other exception:
  mark download failed
  rethrow the original exception
```

Forbidden:

- reconciling every append exception;
- parsing errno from exception text;
- implementing reconciliation inside a test double.

Task-012 tests use a real-file-backed `WriteFile` double that commits a strict
prefix and throws typed `ENOSPC` or `EDQUOT`, then execute the production
`FileSegment` path. A separate negative test proves a generic exception does not
run the errno-specific reconciliation branch.

The concrete FileCache writer that reliably produces structured errno is a
separate pre-release gate. Its absence does not change Task-012 consumer logic.

### Settings reload

`applySettingsIfPossible` must compare each new field value with the current
field value. It must not rely on whether a field was present in the latest
configuration payload.

### Structure and lifecycle

- `ShardedMap` callbacks must not leak map-slot references, addresses, or
  iterators.
- `KeyMetadata` remains ordered `std::map`.
- `FileSegments` and LRU/SLRU queues remain `std::list`.
- An F14 metadata bucket is accepted only with a no-reference-across-mutation
  proof.
- `deactivateBackgroundOperations` order:

```text
set shutdown
join metadata-load thread
deactivate both scheduler tasks
wait for eviction pool
shutdown metadata
```

- `CacheMetadata::shutdown` cancels queues before joining their workers.
- Tests use barriers/futures and observable state, never sleeps.
- F-CALLERID and scheduler recursive-mutex resolution remain Task 017.
- `StatusFile` unclean-restart diagnostics remain a pre-release gate.

## Task-013 contract decisions

- Reuse `FileCacheUtils::checkedAdd`; do not add a private Manager helper.
- Factory owns no runtime service. Manager-owned scheduler, worker pool,
  filesystem callback, and opened-file cache outlive every cache.
- Preserve registry/settings lock order.
- Deactivate caches outside the registry lock.
- Preserve reverse member-destruction order and global-instance publication.
- F14 registry/set values are `shared_ptr`; pointees remain stable across
  rehash.

## Task-014 contract decisions

- Preserve the remote-reader handoff sequence and Task-007 buffer invariants.
- On a reader exception, a canceled reader must not be stored back in
  `FileSegment`.
- In-buffer seek is O(1) and keeps holder/downloader state.
- Out-of-buffer seek releases downloader state and rebuilds read state.
- `queryContextHolder` lives from stream construction to destruction and is not
  reset by seek.
- `skip_cache_on_disk_failure` may bypass cache writes while preserving source
  reads.
- `getRemoteFileMetadata == nullopt` means truncation metadata is unavailable;
  tests must not assume a real metadata source.
- Port every assigned CH reader/handoff test or record an exact ownership or
  supersession reason.
- Do not modify Gluten in Task 014.

## Deferred and release gates

### Task 017

- restore `None:<threadname>:<tid>`;
- resolve/register the scheduler recursive mutex;
- implement real `ProfileEvents`, `CurrentMetrics`, logging, and exception text.

### Post-Task-019 evidence

- CH-computed `sipHash128` golden vectors and a `0xff -> 0xee` mutation;
- malformed-character differential fuzz against CH `unhexUInt`.

### Pre-release

- structured errno producer in the FileCache concrete writer;
- `StatusFile` read-before-truncate unclean-restart diagnostics;
- real `ENOSPC`/`EDQUOT` evidence.

## Execution gate

Task 011 remains prohibited until:

1. Task 003 B1/B2 corrective work is accepted;
2. numbered Tasks 011-014 contain these decisions, dependency pre-checks, RED
   matrices, and false-green probes; and
3. the Controller records zero unresolved findings.
