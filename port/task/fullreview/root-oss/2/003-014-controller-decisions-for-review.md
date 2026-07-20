# Tasks 003-014 Controller Decisions for User Post-Review

## Status

```text
environment_profile: root-oss
review_round: 2
review_status: user_post_review
blocks_current_execution: false
```

This file records material decisions that remain in the accepted implementation
but did not receive a separate, explicit user approval before they were made.
They were accepted by Worker/Controller review because they fixed compilation,
source-parity, concurrency, lifetime, or memory-safety defects.

The user may approve, modify, or reject them after the fact. That review does
not block B2-B5 corrections or Task 015.

## D1 — Task-009 public-header registration

**Background:** `ShardedMap.h` is consumed as a public FileCache header in
non-mono builds.

**Problem:** The original task registered implementation sources but omitted
`ShardedMap.h` from the non-mono public `FILE_SET`. Downstream consumers could
compile in mono mode and fail to consume the standalone library.

**Controller decision:** Register `ShardedMap.h` as a public non-mono header.

**Why:** This changes build packaging only, not runtime behavior.

**Risk/alternative:** The alternative is keeping the header unregistered and
requiring consumers to reach into the source tree, which is inconsistent with
the other FileCache public headers.

**Current implementation:** Task 009 accepted implementation/receipt,
`096ba0c9e`.

**User post-review:** pending.

## D2 — Task-012 `call_once` replacement

**Background:** CH `callOnce` is a thin wrapper around `std::call_once` and
expects retry after a throwing initialization.

**Problem:** In the current statically linked toolchain, an exception unwinding
through the `pthread_once` path aborted instead of returning to the caller and
leaving the once flag retryable.

**Controller decision:** Use one mutex plus a completed flag around
`FileCache::initialize`.

**Why:** It serializes initialization, publishes completion only after success,
and leaves the flag unset on exception so a later call retries.

**Risk/alternative:** It is a local reimplementation of once semantics. The
alternative is fixing the static unwind/toolchain path and returning to
`std::call_once`.

**Current implementation:** Task 012, Velox `a46ff4716`.

**User post-review:** pending.

## D3 — Task-012 release-inert failpoint seams

**Background:** CH tests use failpoints to force resize, eviction, and shutdown
failure paths.

**Problem:** The initial Velox failpoint shim was a no-op, so important rollback
paths could not be proven.

**Controller decision:** Map the selected FileCache failpoints to Velox
`TestValue::adjust` seams and link the existing `velox_test_util` pattern.
Release behavior remains inert unless a test explicitly enables and registers
the point.

**Why:** This enables production-path mutation/rollback tests without adding
test-only branches to the algorithms.

**Risk/alternative:** It adds a production-library dependency on the existing
Velox test-value support. The alternative is a separate FileCache failpoint
implementation in Task 017.

**Current implementation:** Task 012, Velox `a46ff4716`.

**User post-review:** pending.

## D4 — Task-013 mutation serialization and transactional reload

**Background:** One Manager owns a shared worker pool and may create, reload,
remove, clear, or shut down multiple cache instances.

**Problem:** Unsynchronized lifecycle operations could overwrite a newer worker
budget, resize a stopped pool, or leave a newly registered but uninitialized
cache after a later configuration error.

**Controller decision:** Serialize Manager mutations with one Manager-owned
mutex and make new-cache registration, pool growth, and initialization a
fail-close transaction. On failure, remove only bindings introduced by that
reload, restore the truthful surviving budget/pool size, and rethrow the
original exception.

**Why:** It prevents cross-cache deadlocks and retrievable uninitialized caches.

**Risk/alternative:** Reload operations are serialized rather than concurrent.
The alternative is a more complex versioned/transactional registry.

**Current implementation:** Task 013, Velox `bbda44d25`.

**User post-review:** pending.

## D5 — Task-013 safe `OpenedFileCache` late-handle destruction

**Background:** CH uses an immortal `OpenedFileCache` singleton. The port makes
it a bounded-lifetime Manager member.

**Problem:** The CH-style custom deleter captured a raw bucket pointer. If a
file handle outlived the Manager, its last release touched a destroyed map and
mutex. An ASan mutation reproduced the heap use-after-free.

**Controller decision:** Store each bucket's map/mutex in shared state and make
the handle deleter capture a weak reference. If the bucket is gone, the deleter
closes the file without touching destroyed cache state.

**Why:** It preserves normal weak-entry cleanup while making bounded Manager
lifetime safe.

**Risk/alternative:** A late handle cannot erase an already-destroyed bucket,
which is harmless. The alternative is forcing every handle to co-own the whole
Manager/cache.

**Current implementation:** Task 013, Velox `bbda44d25`.

**User post-review:** pending.

## D6 — Tasks 007/014 reader detach and query-pool lifetime

**Background:** A query reader may be handed to a long-lived `FileSegment` and
later consumed/destroyed by a background worker.

**Problem:** The first adapter implementation restored its owned internal
buffer on `set(nullptr, 0)`. This differed from CH detach semantics, violated
Task-012's empty-internal-buffer precondition, and retained a `BufferPtr`
charged to a query-scoped `MemoryPool`. A background worker could destroy it
after the query pool died; a mutation reproduced a real use-after-free.

**Controller decision:**

- detach every internal/working view on `set(nullptr, 0)`;
- lazily restore owned storage on a later normal foreground read;
- release the query-pool-owned `BufferPtr` before successful background
  handoff;
- let the background worker supply its own Manager-pool-backed external buffer.

**Why:** It matches CH handoff state and removes the cross-thread pool lifetime
hazard.

**Risk/alternative:** A handed-off reader cannot reuse its original owned
buffer, but that buffer is unnecessary because background reads always use an
external worker buffer.

**Current implementation:** Task-007 corrective `1e3cc3209` plus Task 014
`b92a0ae3a`.

**User post-review:** pending.

## D7 — Task-014 direct-I/O predownload and failure hardening

**Background:** Task 014 introduced optional predownload, checked range probes,
and multi-segment cache writes.

**Problems found during review:**

- optional predownload could issue an unaligned direct-I/O seek/read;
- `isBuffered` used unchecked `offset + length`;
- a cache-write failure in one segment needed to bypass that write and continue
  reading/caching later segments.

**Controller decision:**

- skip only the optional predownload optimization when its gap cannot satisfy
  direct-I/O alignment; preserve normal direct-I/O validation and never fall
  back to buffered I/O;
- use shared `FileCacheUtils::checkedAdd`;
- continue the source read after configured disk-failure bypass and allow later
  segments to cache.

**Why:** All three changes are fail-close or source-correctness fixes and are
covered by production-path tests/mutations.

**Risk/alternative:** Skipping predownload can reduce optimization coverage for
an explicit direct-I/O source, but does not alter returned bytes.

**Current implementation:** Task 014, Velox `b92a0ae3a`.

**User post-review:** pending.

## Historical autonomous decisions later replaced

These are retained for audit but are not current approval requests:

| Historical decision | Replacement |
|---|---|
| Detach restored the owned internal window | Empty detach + lazy restore + release before handoff |
| `FileCache::loadMetadataImpl` temporarily grew/restored the shared pool | Manager owns aggregate sizing; FileCache fails closed on insufficient capacity |
| FileCache failpoints were all no-op | Selected release-inert `TestValue` seams |
| `EvictionInfo` changed public inheritance to composition | CH public inheritance preserved; only base container swapped |
| `OpenedFileCache` deleter captured raw bucket state | Weak bucket-state deleter |

## User review record

The user may append one decision per item:

```text
D1: approve / modify / reject
D2: approve / modify / reject
D3: approve / modify / reject
D4: approve / modify / reject
D5: approve / modify / reject
D6: approve / modify / reject
D7: approve / modify / reject
```

Until the user appends that record, this document remains
`review_status: user_post_review` and does not block the current Task-015 plan.
