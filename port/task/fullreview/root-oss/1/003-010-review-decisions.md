# Tasks 003-010 Full-Review Decisions — `root-oss` Review 1

## Status

```text
environment_profile: root-oss
review_round: 1
decision_status: approved
task_011_allowed: false
```

Frozen implementation baselines:

```text
ClickHouse: da28e83e8b3cb69090624b0a0b1f13cd78c13279
Velox:      89039901aa4287ce811a3b1628867b0796c76678
```

This file records the user decisions made after reviewing:

```text
port/task/fullreview/root-oss/1/evidence/011-012-consumer-contract-ledger.md
port/task/fullreview/root-oss/1/evidence/003-010-full-review-result.md
```

It supersedes the report's proposed reopen list where a finding was explicitly
accepted as a deliberate deviation or deferred to a later task.

## 1. Required before Tasks 011/012

### B1 — `ProfileEvents` enumerator surface

Owner: Task 003 corrective work.

Decision:

- Add the 31 `ProfileEvents` names referenced by the current CH `FileCache`
  source but absent from the Velox shim.
- Keep `ProfileEvents::increment` and timer behavior no-op in this phase.
- Add one compile-coverage test that references the complete required name set.
- Add a false-green mutation that deletes one already-present required name and
  proves the coverage test fails to compile.
- Real event counters remain Task 017.

### B2 — `CurrentMetrics` enumerator surface

Owner: Task 003 corrective work.

Decision:

- Add only the five missing names still referenced by the in-scope Task 011/012
  implementation:

```text
FilesystemCacheElements
FilesystemCacheInvalidatedElements
FilesystemCachePriorityQueueElements
FilesystemCacheSize
FilesystemCacheKeys
```

- Keep `CurrentMetrics::add`, `sub`, and `Increment` no-op in this phase.
- Do not add the three CH eviction-thread metrics because the accepted
  `FileCacheThreadPool` constructor deliberately has no metrics parameters.
- Do not add `FilesystemCacheOvercommitUsers`; overcommit is excluded.
- Add complete compile-coverage and false-green mutation evidence.
- Real metric counters remain Task 017.

### Immediate reopen list

```text
Task 003 only
```

Task 006 and Task 009 are not reopened by this review.

## 2. Task 012 infrastructure mappings

Task 012 must record these mappings before its Worker starts. The Worker must
not choose a different primitive without triggering the dependency gate.

| CH dependency | Approved Velox mapping | Limits |
|---|---|---|
| `Memory<>` | MemoryPool-charged `BufferPtr` | Preserve buffer size, reuse, lifetime, and memory accounting. |
| `SCOPE_EXIT` | Folly scope guard | Must run on normal return and exception unwind. |
| `Stopwatch` | `using Stopwatch = facebook::velox::DeltaCpuWallTimeStopWatch` | This alias is call-site-limited: construct and read one wall-time snapshot. It does not provide CH `stop`, `reset`, or `restart`. Convert with `elapsed().wallNanos / 1'000'000`. |
| `callOnce` / `OnceFlag` | `std::call_once` / `std::once_flag` | Exact CH mapping; CH `callOnce` is a wrapper over these standard primitives. |

The `Stopwatch` alias is not a general replacement for every CH `Stopwatch`
consumer. It is approved only for the two Task-012 `FileCache` call sites that
construct a watch and immediately consume elapsed wall milliseconds.

## 3. Structured errno contract

### Task 012 consumes errno

Task 012 must implement the CH-shaped error path against a stable,
FileCache-owned typed exception contract:

```text
FileCacheErrnoException
  getErrno() -> int
```

The exact class may wrap `std::error_code`/`std::system_error`, but callers must
receive a numeric POSIX errno without parsing exception text.

`FileSegment::write` behavior:

```text
catch FileCacheErrnoException:
  mark download failed
  if errno is ENOSPC or EDQUOT:
    read physical file size
    require downloaded_size <= physical_size <= reserved_size
    update downloaded_size to physical_size
  rethrow the original exception

catch any other exception:
  mark download failed
  rethrow the original exception
```

Task 012 must not add a temporary "reconcile every exception" fallback for the
current `LocalWriteFile`. The absence of an errno producer is a separate
pre-release gap and must not change the final `FileSegment` state machine.

Task-012 tests must use a real-file-backed `WriteFile` double that:

1. physically commits a strict prefix;
2. throws `FileCacheErrnoException` with `ENOSPC` or `EDQUOT`;
3. executes the production `FileSegment::write` path; and
4. proves physical/downloaded/reserved accounting, failed state, and original
   exception propagation.

The test must not perform reconciliation in the test double.

### Pre-release errno producer gate

The current `LocalWriteFile` does not provide reliable structured errno:

- a positive short `write` does not have a defined errno;
- `LocalWriteFile` converts the current errno to text only; and
- its `VeloxRuntimeError` carries generic `INVALID_STATE`, not POSIX errno.

This does not block Tasks 011/012 development, but it blocks production release.
A separate pre-release task, whose number will be assigned when scheduled, must
change the existing FileCache concrete writer internally:

- keep `WriteBufferFromFileBase` and the existing concrete writer public API;
- do not require a new implementation of the generic `velox::WriteFile`
  interface;
- let the FileCache writer own an fd through `folly::File` or an equivalent
  FileCache-local RAII handle;
- call `::write` directly;
- retry `EINTR`;
- continue after a positive short write;
- capture errno immediately when `write` returns `-1`;
- treat `write == 0` as a short-write failure without inventing an errno;
- throw the Task-012 `FileCacheErrnoException`;
- preserve zero-copy, resume, partial-write, finalize, cancel, detach, and
  exception-identity behavior.

The release gate requires real `ENOSPC` and `EDQUOT` evidence plus the existing
Task-007 and Task-012 regression suites.

## 4. Approved structure deviations

### SD1 / M1 — Task 009 `ShardedMap`

Decision: keep `folly::F14FastMap`.

The current FileCache consumers do not require mapped-value address stability:

- `withShard` returns values by copy;
- the origin consumer copies out `shared_ptr`;
- no current consumer retains a map iterator or mapped-value reference across a
  callback or rehash.

Contract:

```text
Callbacks must not retain a map iterator, mapped-value reference, or mapped-value
address after the locked callback returns.
```

This is an explicitly approved structural deviation. Task 009 is not reopened.

### SD2 — Task 011 flat containers

Confirmed mapping:

```text
absl::flat_hash_map -> folly::F14FastMap
absl::flat_hash_set -> folly::F14FastSet
```

Both families are flat/non-node containers and do not guarantee mapped-value,
reference, or iterator stability across rehash.

The current values remain safe:

- `QueueID -> unique_ptr<QueueEvictionInfo>` keeps the pointee stable;
- `set<shared_ptr<CacheUsage>>` keeps the pointee stable;
- `FileCacheKey -> KeyCandidates` does not retain a reference while inserting
  another key or rehashing;
- `original_queue_types` remains `std::unordered_map`.

`F14NodeMap`/`F14NodeSet` are not required.

### SD6 — Task 005 thread-pool remap

Decision: no implementation change.

Register the forced platform mapping from the CH-owned worker/job/CV structure
to the accepted Folly executor, backlog, futures, and injected worker-pool
structure. Current consumer guarantees are preserved.

### SD7 — Task 006 scheduler remap

Decision: no implementation change.

Register the forced platform mapping from the CH delay thread and
`std::multimap` state to `folly::Timekeeper`, Future continuations, and the
accepted explicit task state machine. External scheduling semantics are
preserved.

### SD9 — Task 007 memory mapping

Decision: no implementation change.

Register the forced platform mapping from CH-owned `Memory<>` buffers to
MemoryPool-charged `BufferPtr` ownership. The buffer state machine is preserved;
the deliberate difference is Velox memory accounting.

## 5. Structures that must remain unchanged

### SD3

`KeyMetadata` must remain an ordered `std::map`. Range lookup, adjacency, and
node stability are load-bearing.

### SD5

LRU queues and the `FileSegments` container must remain `std::list`. Iterator
stability and splice behavior are load-bearing.

### SD4

Task 012 may use an F14 metadata bucket only if review proves that no iterator,
mapped-value reference, or mapped-value address survives a bucket mutation.
The stored `shared_ptr<KeyMetadata>` keeps the pointee stable, but the map slot
itself is not stable.

## 6. Deferred to Task 017

### F-CALLERID

The current `None:<tid>` value is sufficient for downloader identity because the
OS thread id remains unique for the active thread and both sides use the same
helper.

Task 017 must:

- add `FileCacheQueryIdScope` source/header and tests to its file scope;
- restore the CH diagnostic format `None:<threadname>:<tid>`;
- use an approved Velox/Folly thread-name primitive; and
- replace the prefix-only test with an exact-format RED and false-green probe.

F-CALLERID does not block Tasks 011/012.

### SD8

The accepted `std::recursive_mutex` is internal to the background scheduler and
does not change the observed `schedule`, `scheduleAfter`, `deactivate`, or
lifetime semantics.

Task 017 must add `FileCacheScheduler` source/header and tests to its scope and
choose one final resolution:

1. retain and explicitly register the recursive mutex with an E probe proving
   the inline Future-continuation re-entry; or
2. attach/dispatch the continuation outside the lock and restore a
   non-recursive mutex.

SD8 does not block Tasks 011/012.

### Real observability

Task 003 adds only no-op event/metric names. Task 017 remains responsible for:

- real `ProfileEvents` counters and timers;
- real `CurrentMetrics` gauges;
- real logging; and
- current-exception formatting.

## 7. Other accepted decisions

These are closed review checks, not unresolved implementation findings.

| ID | Plain-language meaning | Decision |
|---|---|---|
| D10 | CH can deliberately trigger selected disk failures through failpoints. The current Velox failpoint shim does nothing, so those exact injected paths cannot yet be triggered. | Accept for the MVP; it does not change ordinary runtime behavior and does not block the core migration. |
| E1 | A write may physically commit only part of a requested buffer and then report an error. The real file size must show exactly the committed prefix. | Closed by a real-file probe. Task 012 still tests the production `FileSegment` errno-specific reconciliation path. |
| E2 | Reopening a partially downloaded file must preserve its existing prefix and continue writing at the physical end instead of truncating it. | Closed by a real-file probe. Task 012 must use the verified non-exclusive, non-truncating resume mode. |
| E3 | When a remote reader is handed to `FileSegment`, it must have no unread attached bytes, its buffer-end offset must equal the current write offset, and no pointer to caller memory may remain. | Closed by the accepted Task-007 reader-handoff test. |
| E4 | A timed cache-state lock must actually time out and return an unheld lock rather than waiting forever. | Closed because `CacheStateGuard` uses a real `std::timed_mutex`; the mapping is accepted. |
| E5 | The bounded queue must support immediate failure, bounded waits, wake-up on `finish`, and draining queued values before returning false. | Closed by the accepted queue implementation and focused tests. |

## 8. Execution gates

### Before Task 011

Required:

1. complete and accept Task 003 B1/B2 corrective work;
2. record SD1, SD2, SD6, SD7, and SD9 in the structure-deviation ledger; and
3. amend the Task-012 contract with the approved M2 and errno-consumer rules.

### Before Task 012 acceptance

Required:

1. production `FileSegment` tests for typed `ENOSPC` and `EDQUOT`;
2. production partial-file resume and strict-prefix reconciliation evidence;
3. SD4 confirmation; and
4. no test-side implementation of reconciliation.

### Before production release

Required:

1. implement the structured errno producer in the FileCache concrete writer;
2. run real `ENOSPC` and `EDQUOT` tests; and
3. prove the writer's public protocol remains compatible with Tasks 007/012/016.

Until B1/B2 are corrected and accepted:

```text
task_011_allowed: false
```
