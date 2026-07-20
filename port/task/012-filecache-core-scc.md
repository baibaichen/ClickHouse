# Task 012: `FileCache` Center SCC — Mandatory Compile/Link Closure

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes one
> result file under this ClickHouse checkout. Do not modify ClickHouse source
> files. Do not commit or stage either repository.

## Pre-execution source-contract amendment

Task 012 must not start until corrective Tasks 003, 004, 006, 007, and 008 are
accepted and the post-Task-010 whole-port review has zero unresolved findings.

All comment-only test bodies and fixture placeholders later in this file are
non-authoritative and must not be copied. A test name or a green empty body is not
evidence.

### Approved deviations and native mappings (whole-port review, 2026-07-20)

The post-Task-010 whole-port review approved these. The Worker must apply them
exactly; discovering another unreviewed dependency or changing one of these
triggers the `EXECUTION_PROTOCOL.md` unreviewed-dependency gate. Authoritative
decision record: `port/task/fullreview/root-oss/1/003-010-review-decisions.md`
(approved) and `port/task/fullview/home-chang/1/003-010-review-decisions.md`.

**Native mappings the Worker must use (recording them here forbids "pick the
closest Velox API"):**

| CH dependency | Approved Velox mapping | Limits |
|---|---|---|
| `Memory<>` + `DBMS_DEFAULT_BUFFER_SIZE` (D3) | MemoryPool-charged `BufferPtr` (reuse `FileCacheBufferState`/SD9) | preserve buffer size, reuse, lifetime, memory accounting; pin the `DBMS_DEFAULT_BUFFER_SIZE` value |
| `SCOPE_EXIT` (D9) | Folly scope guard | must run on normal return and exception unwind |
| `Stopwatch` (D11) | `using Stopwatch = facebook::velox::DeltaCpuWallTimeStopWatch` | call-site-limited: construct + read one wall-time snapshot; convert `elapsed().wallNanos / 1'000'000`; NOT a general `stop`/`reset`/`restart` replacement |
| `callOnce`/`OnceFlag` (D8) | `std::call_once`/`std::once_flag` | exact mapping; already recorded elsewhere in this file |

**Structured errno contract (R6/007-2 + E1).** Task 012 must implement the
CH-shaped error path against a stable, FileCache-owned typed exception:

```text
FileCacheErrnoException
  getErrno() -> int   // numeric POSIX errno; callers must NOT parse exception text
```

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

Do NOT add a temporary "reconcile every exception" fallback for the current
`LocalWriteFile`. The absence of an errno producer is a separate pre-release
gap and must not change the final `FileSegment` state machine.

**Settings apply (R2/010-1).** `applySettingsIfPossible` MUST decide per field by
value comparison `new_settings[X] != actual_settings[X]` (matching CH
`FileCache.cpp:2800`). It MUST NOT depend on per-field `.changed`/presence
tracking; restoring per-field presence would be over-port.

**Error-code identity (R7/003-1).** The pervasive `throw Exception(ErrorCodes::…)`
→ single `VELOX_FAIL` collapse is accepted. But any 012 path that must
distinguish `NOT_ENOUGH_SPACE` from `LOGICAL_ERROR` (e.g. reserve failure vs a
logic fault) MUST reintroduce a typed distinction at that call site rather than
relying on the collapsed category.

**Container structures the Worker must NOT change (§3):**
- SD3 — `KeyMetadata` stays an ordered `std::map` (range/`lower_bound`/node
  stability are load-bearing).
- SD5 — LRU queues and the `FileSegments` container stay `std::list` (iterator
  stability + `splice`).
- SD4 — an F14 metadata bucket is allowed ONLY if review proves no iterator,
  mapped-value reference, or mapped-value address survives a bucket mutation
  (the stored `shared_ptr<KeyMetadata>` keeps the pointee stable; the slot is not).
- SD1 — `ShardedMap` keeps `folly::F14FastMap` under the locked invariant: no
  `Value&`/iterator escapes `withShard`/`forEachShard` across a mutation
  (Task 011/012 review MUST enforce this).

**Forced platform remaps already registered (no implementation change):** SD6
(Task 005 thread pool → folly executor + backlog + futures), SD7 (Task 006
scheduler → `folly::Timekeeper` + per-task `Future`), SD9 (Task 007 owned buffer
→ MemoryPool-charged `BufferPtr`). SD8 (scheduler `recursive_mutex`) is an
accepted-interface / deferred-implementation item; see the deferred-work note in
Task 006 / Task 017.

### Task 012 execution — CH-source authority + sub-attempt plan (2026-07-20)

The first Task-012 Worker cleared both pre-implementation gates (no unreviewed
dependency; all mappings confirmed) but correctly returned `blocked` on **scope**:
the SCC is ~7.9k lines of CH center source + ~2.9k lines of the Task-011 `.cpp`
+ 6 behavioral-RED tests, with no intermediate link step. A single monolithic
pass cannot reach the required fidelity without risking false-green. Two Controller
decisions (user-approved 2026-07-20):

**CH source is authoritative over this task's illustrative signatures.** Steps
9-13 show illustrative shapes; where they diverge from CH, port toward CH:
- `FileCache::getOrSet` / `get` / `getDownloadedContiguousOrEmpty` / `set` /
  `trySet` / `tryReserve` / `getQueryContextHolder` use the CH `FileCache.h`
  signatures (e.g. `getOrSet(key, offset, size, file_size,
  CreateFileSegmentSettings, file_segments_limit, origin, boundary_alignment)`),
  NOT the `(key, Range, origin, options)` form.
- `FileSegmentInfo::download_finished_time` is **`time_t`** (CH
  `FileSegmentInfo.h:74`); all callers in Metadata/FileSegment stay consistent
  with `time_t`.
- `LockedKey::removeFileSegment` keeps CH's multiple arities
  (`Metadata.h:391,397`): the `(offset, lock)` form and the
  `(offset, lock, can_be_broken, invalidate_queue_entry)` form must both compile.
- General rule: any other Step 9-13 shape that disagrees with CH
  `FileSegment.h`/`Metadata.h`/`FileCache.h`/`QueryLimit.h` is reconciled toward
  the CH header. New-file specs (e.g. FileSegmentInfo.h layout) win only where CH
  has no counterpart.

**Sub-attempt plan (multiple bounded Workers, ONE final green gate).** The stage
is accepted only when the whole SCC compiles+links and every test passes
(0 failed / 0 skipped) — that final gate does not change. To make delivery
truthful, the Controller dispatches bounded sub-attempts against the SAME gate.
Intermediate sub-attempts MUST NOT claim a link/test green (the SCC has no
intermediate link step); they deliver source + a `configure`/compile-only sanity
check and hand off. Suggested cut (Controller may adjust by dependency need):

```text
S1  FileSegmentInfo.h + FileSegment.h + Metadata.h + FileCache.h + QueryLimit.h
    (headers only) — establishes the real types the Task-011 .cpp were written
    against; configure succeeds; no link expected.
S2  FileSegment.cpp + Metadata.cpp (+ finish the Task-011 .cpp against the real
    headers) — compile-only per TU; no link expected.
S3  FileCache.cpp + QueryLimit.cpp — compile-only per TU.
S4  CMake registration + the 6 behavioral-RED tests + the FINAL green
    compile+link+`velox_ch_filecache_core_scc_test` run (0 failed / 0 skipped).
    This is the only sub-attempt that asserts the stage gate.
```

Each sub-attempt is a fresh Worker with a bounded file list, records its own
receipt section, and is reviewed by the Controller before the next. A sub-attempt
that cannot meet even its bounded compile-only checkpoint returns `blocked`. The
`## Mandatory executable tests` and all amendments above remain binding on S4.

**Partial-physical-append-failure test (confirmed approach).** Drive the real
production `FileSegment::write` path over an injected production `velox::WriteFile`
that physically commits a strict prefix and then throws `FileCacheErrnoException`
(ENOSPC/EDQUOT). Reconciliation must happen inside production `FileSegment`, never
inside the test double — the double only injects the fault.

### S2 unblock — three structural resolutions (2026-07-20)

The S2 Worker correctly blocked on three real structural gaps that require
editing files outside the raw S2 `.cpp` list. All three are now resolved; S2's
scope is expanded exactly as follows (nothing more):

- **B1 — `EvictionCandidates.h` portability fix (authorized).** The header was
  copied verbatim from CH and uses C++23 "deducing this"
  (`auto begin(this auto&& self)`) and `folly::F14FastSet::merge`, neither of
  which compiles under the home-chang g++ 13.3 / gnu++20 toolchain. Task 011 was
  "structural check only" so this was never compiled. S2 is authorized to edit
  `EvictionCandidates.h`: replace deducing-this with explicit const / non-const
  `begin`/`end` overloads, and replace `kept_alive_cache_usage.merge(other...)`
  with an insert-range loop. Behavior is unchanged; this is a pure portability
  fix, CH semantics preserved.

- **B2a — inject the reserve timeout (authorized; executes approved design).**
  CH `Metadata.cpp:966` reads
  `reserve_space_wait_lock_timeout_milliseconds` from the global `Context`. The
  approved design (`08-filecache-metadata-files-design.md:471-473`) already
  mandates injecting it from `FileCacheConfig` into `CacheMetadata` instead. S1
  simply omitted the plumbing. S2/S3 are authorized to add:
  a `reserve_space_wait_lock_timeout_milliseconds` field to `FileCacheConfig`
  (`FileCacheSettings.h`, name e.g. `reserveSpaceWaitLockTimeoutMilliseconds`,
  default matching CH), a matching `CacheMetadata` constructor parameter + member
  (`Metadata.h`), and the `FileCache` pass-through at construction
  (`FileCache.h`/`.cpp`, wired in S3). `downloadImpl` reads the injected member,
  not any global Context.

- **B2b — opened-file-handle invalidation: throw not-supported + TODO (user
  decision 2026-07-20).** CH calls `OpenedFileCache::instance().remove(path,
  flags)` (idempotent) after a rename (`FileSegment.cpp:800-802`) and after a
  removal (`Metadata.cpp:1267-1268`). `OpenedFileCache` is a process singleton
  that belongs to the Task-013 Manager and does not exist in the SCC phase. Per
  the user decision, **do NOT** inject a no-op (a silent fallback). Instead, at
  each of the two call sites, add a `TODO(Task 013)` and `throw` a
  not-supported/not-implemented exception (`VELOX_NYI` or a FileCache
  not-implemented exception) in place of the `OpenedFileCache::remove` calls, so
  any code path that actually reaches opened-handle invalidation fails loudly
  rather than silently skipping a correctness-relevant step. Task 013 replaces
  the throw with the real Manager-backed invalidation. This relaxes the design
  `08:571` "no-stub" rule for this one item, recorded here as the amendment:
  the SCC phase intentionally throws-not-implemented here, and the mandatory S4
  tests do NOT exercise the rename/remove opened-handle-invalidation paths.

### S2 unblock 2 — B3 CacheMetadata worker pool injection (2026-07-20, authorized)

The S2 redispatch completed 6 of 7 TUs; `Metadata.cpp` blocked on B3: the Velox
`FileCacheWorker` (Task 005) ctor is `FileCacheWorker(FileCacheWorkerPool&,
Function)` — unlike CH's `ThreadFromGlobalPool`, which draws from an implicit
global pool — so `CacheMetadata` cannot start its download/cleanup threads
without an explicit `FileCacheWorkerPool&`. This is **not** a new architecture
decision: the approved thread-pool design already fixes it.

- Design `1-dependencies/04-filecache-thread-pool-design.md:11,45-48`:
  `GlobalThreadPool` → **manager-owned `FileCacheWorkerPool`**; the
  `FileCacheManager` holds the single shared worker pool and stops it at
  shutdown; `04:142` keeps `CacheMetadata::download_threads` on
  `ThreadFromGlobalPool` (= Velox `FileCacheWorker`).
- Since the Manager is Task 013, the SCC phase injects the pool by reference:
  `CacheMetadata`'s constructor takes a `FileCacheWorkerPool&` parameter + stores
  a reference member; `CacheMetadata::startup` creates its download/cleanup
  `FileCacheWorker`s bound to that pool (mirroring CH `Metadata.cpp:1024,1027`).
  In the SCC phase the pool is owned by `FileCache` and passed to `CacheMetadata`
  at construction; Task 013 later moves ownership to the Manager and injects it
  the same way (no CacheMetadata signature change at 013).

Authorized (executes approved design 04): S2/S3 add the `FileCacheWorkerPool&`
ctor parameter + reference member to `CacheMetadata` (`Metadata.h`), reach it
from `startup` (`Metadata.cpp`), and have `FileCache` own the pool and pass it in
(`FileCache.h`/`.cpp`, wired in S3). Do NOT make `CacheMetadata` own the pool
(design says manager/FileCache owns the single shared pool).

### S3 unblock — B5/B6 FileCache host-injected members (2026-07-20, authorized)

S3 completed `QueryLimit.cpp` but blocked on two `FileCache.cpp` gaps, both the
same "CH reads a global Context, Velox injects from the host/Manager" pattern as
B2a/B3 — and both already fixed by the approved design. Controller-authorized
(executes approved design; not a new decision):

- **B5 — `commonUserId` injection.** CH `getCommonUserID` (`FileCache.cpp:138-141`)
  reads `Context::getGlobalContextInstance()->getFilesystemCacheUser()`, falling
  back to `ServerUUID::get()`. Design `10-filecache-core-files-design.md:43,138,
  161,164,193` mandates: `ServerUUID/common cache user -> manager-injected stable
  commonUserId`; "Velox 没有 CH `ServerUUID`；`FileCacheManager::Options.commonUserId`
  必须由宿主显式提供", injected into `CacheMetadata`. SCC phase: `FileCache`
  receives/holds a stable `commonUserId` (std::string) and uses it in
  `getCommonOrigin`/`getCommonUserID`; no global Context, no ServerUUID. Task 013
  supplies it from the Manager.
- **B6 — `FileCacheScheduler` + `folly::Timekeeper` ownership.** CH creates its
  `background_cleanup_task` / `keep_up_free_space_ratio_task` via
  `Context::getGlobalContextInstance()->getSchedulePool().createTask(...)`
  (`FileCache.cpp:559,591`). Design `05-filecache-scheduler-design.md:21,162-173`
  replaces `BackgroundSchedulePool` with `FileCacheScheduler(shared_ptr<folly::
  Timekeeper>, ...)`. SCC phase: `FileCache` owns a `folly::Timekeeper` + a
  `FileCacheScheduler` and calls `createTask` on it (SD6/SD7 already registered).

**FileCache construction shape (SCC phase).** CH `FileCache(cache_name,
settings)` is 2-arg. To carry the manager-injected dependencies without a global
Context, the SCC-phase `FileCache` constructor is extended to receive/establish:
the `commonUserId` (B5), and it owns the `FileCacheWorkerPool` (B3) + the
`folly::Timekeeper`/`FileCacheScheduler` (B6), passing the pool + reserve timeout
(B2a) into `CacheMetadata`. Ownership stays FileCache/Manager (design 04/10);
Task 013 moves these to the Manager and injects them the same way (no CacheMetadata
signature change). Record any FileCache-ctor argument additions as the approved
injection shape; do NOT read a global Context and do NOT invent a ServerUUID.

### Mandatory executable tests

Each case below must contain real setup, execute the production path, and assert the
observable postcondition:

| Contract | Required assertion |
|---|---|
| missing key + `KeyNotFoundPolicy::THROW` | production call throws the expected Velox exception |
| releasable reserve eviction | reserve succeeds only after the real candidate is removed; cache size and segment state agree |
| empty query id | `getQueryContextHolder` returns null and creates no map entry |
| same query id | two holders share one context and one map entry |
| last holder release | destroying the final holder removes the map entry |
| doomed context destruction | final context destructor runs after the cache write lock is released; test callback can reacquire the lock |
| max download size | query LRU limit equals the configured maximum and rejects excess reservation |
| queue pipeline | real timed `tryPush(batch, 10)` and non-blocking `tryPop(batch)` paths execute |
| remote reader handoff | reader detach, buffer-end offset, available bytes, and downloader release satisfy Task 007 |
| partial-file resume | production `FileSegment` writes a first prefix, releases/recreates its writer through the real continuation path, verifies the existing physical size, appends without truncating the prefix, and keeps downloaded/physical size consistent |
| partial physical append failure | the production `FileSegment` path observes an append that physically commits a strict prefix and then throws, reads `filesystem::file_size`, enforces `downloadedSize <= physicalSize <= reservedSize`, updates downloaded size to physical size, marks the download failed, preserves the original exception, and never counts reserved-but-unwritten bytes |

For each material test, capture a behavioral RED against the pre-implementation or
intentionally broken path. Missing-header compile failure alone is insufficient.

The two `FileSegment` cases above are the integration half of the Task-007
adapter contract. Task 007 proves only the already-open `WriteFile` behavior
(append preserves existing bytes, and a partial-writing `WriteFile` exception is
propagated once while the adapter becomes canceled). Task 012 must execute the
real file-opening, downloaded/reserved accounting, filesystem-size
reconciliation, and failure-publication path. A test that performs reconciliation
inside test code or a mock instead of production `FileSegment` is false-green.

### ClickHouse gtest migration ownership

Audit `src/Interpreters/tests/gtest_filecache.cpp` before writing the Task-012
tests. Port every case whose production owner is in the center SCC, adapting the
existing fixture and temporary-directory patterns rather than copying
ClickHouse-only infrastructure:

```text
FileSegment reserve/write/complete/partial-state cases -> FileSegmentTest.cpp
metadata restore/path/cleanup cases                    -> MetadataTest.cpp
FileCache get/getOrSet/remove/eviction cases           -> FileCacheTest.cpp
query-limit holder/accounting cases                    -> QueryLimitTest.cpp
```

The audit must list every relevant CH test and its Velox destination. If a case
is not migrated, record the exact reason (unsupported excluded feature,
superseded by a stronger production-path test, or assigned to Task 014/015).
Do not carry over sleeps, comment-only bodies, or assertions that exercise only
test doubles. The resume and partial-physical-write cases are new Task-012 tests:
CH has useful FileCache fixtures but no existing test that proves both exact
contracts.

### CMake registration

Inspect the existing `velox/ch/Interpreters/FileCache/CMakeLists.txt` before editing.
Preserve an existing `add_subdirectory(tests)` block; do not add the same source and
binary directory twice. The literal duplicate block later in this file is
superseded.

## Goal

Produce a single compilable and linkable batch that closes the center strongly
connected component (SCC) of the `FileCache` implementation:

```text
IFileCachePriority/LRU/SLRU/Split/EvictionCandidates from Task 011
FileSegmentInfo.h
FileSegment.h / FileSegment.cpp
Metadata.h / Metadata.cpp       (owns CleanupQueue and DownloadQueue)
FileCache.h / FileCache.cpp
QueryLimit.h / QueryLimit.cpp
```

The deliverable is a compiled and tested `velox_ch_filecache_core` library and
a `velox_ch_filecache_core_scc_test` executable.

## Why the Center SCC Cannot Be Split

The priority and center implementation files form a genuine strongly connected component and
cannot be split into independently linkable units:

```text
FileSegment.cpp
  -> FileCache::tryReserve / tryIncreasePriority / config getters
  -> LockedKey::removeFileSegment / removeAllFileSegments
  -> KeyMetadata path / origin APIs

Metadata.cpp
  -> FileSegment state / range / write / reserve / detach / getInfo
  -> FileCache::getInternalOrigin (static internal origin factory)

FileCache.cpp
  -> CacheMetadata (owns download/cleanup workers)
  -> FileSegment (creates and owns through metadata)
  -> FileCacheQueryLimit (optional construction)

QueryLimit.cpp
  -> FileCache::lockCache (CachePriorityGuard::WriteLock)
  -> KeyMetadata (for path/access in add record)

priority/eviction .cpp files
  -> FileCache definitions
  -> Metadata/KeyMetadata/FileSegment
```

Creating a fake `FileCache` stub with only the symbols `FileSegment.cpp` needs
would require re-implementing the same eviction/reserve algorithm that
`FileCache.cpp` provides. The result would be two conflicting implementations
linked into the same test binary. The only sound option is to compile all `.cpp`
files together and link once.

Headers (`FileSegment.h`, `Metadata.h`, `FileCache.h`, `QueryLimit.h`) can be
reviewed and added one at a time in earlier substeps because they carry no
link-time symbols. The single compile/link closure is required only for the
`.cpp` files.

## Starting Point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected predecessors:
  Task 003: basic common shims (ClickHouseAliases, FileCacheBoundedQueue, etc.)
  Task 004: StatusFile, Guards.h
  Task 005: FileCacheWorkerPool / FileCacheThreadPool
  Task 006: FileCacheScheduler, FileCacheQueryIdScope
  Task 007: ReadBufferFromVeloxReadFile, WriteBufferFromVeloxWriteFile
  Task 008: SipHash128, key/origin/segment types, forward files, utils
  Task 009: ShardedMap
  Task 010: FileCacheSettings/FileCacheConfig
  Task 011: priority/eviction source migration, atomic Part A (not compiled)
```

Do not require a clean worktree. Stop if the branch is not `filecache`.

## Design References

Read before editing:

```text
port/task/ENVIRONMENT.md
port/01-filecache-port-order-design.md
port/2-file-cache/07-filecache-priority-eviction-design.md
port/2-file-cache/08-filecache-metadata-files-design.md
port/2-file-cache/09-filecache-file-segment-design.md
port/2-file-cache/10-filecache-core-files-design.md
port/2-file-cache/11-filecache-query-limit-design.md
port/1-dependencies/01-filecache-infra-mapping.md
port/1-dependencies/02-filecache-basic-shims-design.md
port/1-dependencies/04-filecache-thread-pool-design.md
port/1-dependencies/06-filecache-caller-token-design.md
```

Use ClickHouse source only as behavioral reference:

```text
src/Interpreters/FileCache/FileSegmentInfo.h
src/Interpreters/FileCache/CacheUsage.h
src/Interpreters/FileCache/IFileCachePriority.h / .cpp
src/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
src/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
src/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
src/Interpreters/FileCache/EvictionCandidates.h / .cpp
src/Interpreters/FileCache/FileSegment.h / .cpp
src/Interpreters/FileCache/Metadata.h / .cpp
src/Interpreters/FileCache/FileCache.h / .cpp
src/Interpreters/FileCache/QueryLimit.h / .cpp
```

## File Scope

Modify:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/CMakeLists.txt
<velox_repo>/velox/ch/Interpreters/FileCache/CacheUsage.h
<velox_repo>/velox/ch/Interpreters/FileCache/IFileCachePriority.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/EvictionCandidates.h / .cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/FileSegmentInfo.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileSegment.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileSegment.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/Metadata.h
<velox_repo>/velox/ch/Interpreters/FileCache/Metadata.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/FileCache.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCache.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/QueryLimit.h
<velox_repo>/velox/ch/Interpreters/FileCache/QueryLimit.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp
<clickhouse_repo>/port/task/result/012-filecache-core-scc-result.md
```

Every new Velox C++ and CMake file must begin with the Apache 2.0 license
header. Use `/* ... */` for C++ and `#` for CMake, matching the repository style.

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected: branch `filecache`, HEAD is a descendant of `bf379041f`.
Record pre-existing dirty files in the result file. Stop if the branch differs.

- [ ] **Step 2: Create the test CMakeLists**

Append the following target to the existing
`velox/ch/Interpreters/FileCache/tests/CMakeLists.txt`; preserve every test
target added by Tasks 008-010:

```cmake
add_executable(
  velox_ch_filecache_core_scc_test
  PriorityEvictionTest.cpp
  FileSegmentInfoTest.cpp
  FileSegmentTest.cpp
  MetadataTest.cpp
  FileCacheTest.cpp
  QueryLimitTest.cpp
)
add_test(velox_ch_filecache_core_scc_test velox_ch_filecache_core_scc_test)

target_link_libraries(
  velox_ch_filecache_core_scc_test
  PRIVATE
    velox_ch_filecache_core
    velox_ch_filecache
    velox_test_util
    velox_exception
    velox_file
    velox_memory
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

- [ ] **Step 3: Write priority/eviction and `FileSegmentInfo` tests (red)**

Create `PriorityEvictionTest.cpp` against the real Task-011 headers and the
real core types that this task will add. Do not declare local
`KeyMetadata`/`FileCache` substitutes. Cover:

```text
LRU add/remove/evict and stable iterator
zero-size entry counts neither bytes nor elements
reserve/background cursors advance independently
total-space cleanup uses min(requested,current)
SLRU second access promotes probationary to protected
addForRestore restores original queue
Split routes General/Data to Data and System to System
Split partitions both bytes and elements
failed second resize rolls first resize back
EvictionInfo keeps separate QueueID entries and usage pins
EvictionCandidates removeQueueEntries/original queue restore/failure accounting
```

Use a real temporary `FileCache` fixture once the SCC implementation exists;
the file is expected to fail at the red-build step before those definitions are
added.

Create `velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp`:

```cpp
#include "velox/ch/Interpreters/FileCache/FileSegmentInfo.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <type_traits>

namespace facebook::velox::ch
{
namespace
{

TEST(FileSegmentInfoTest, StateEnumLayout)
{
    // Order and underlying values preserved from ClickHouse.
    static_assert(static_cast<uint8_t>(FileSegmentState::DOWNLOADED) == 0);
    static_assert(static_cast<uint8_t>(FileSegmentState::EMPTY) == 1);
    static_assert(static_cast<uint8_t>(FileSegmentState::DOWNLOADING) == 2);
    static_assert(
        static_cast<uint8_t>(
            FileSegmentState::PARTIALLY_DOWNLOADED_NO_CONTINUATION)
        == 3);
    static_assert(
        static_cast<uint8_t>(FileSegmentState::PARTIALLY_DOWNLOADED) == 4);
    static_assert(static_cast<uint8_t>(FileSegmentState::DETACHED) == 5);
}

TEST(FileSegmentInfoTest, KindEnumLayout)
{
    static_assert(static_cast<uint8_t>(FileSegmentKind::Regular) == 0);
    static_assert(static_cast<uint8_t>(FileSegmentKind::Ephemeral) == 1);
}

TEST(FileSegmentInfoTest, InfoSnapshotCompiles)
{
    FileSegmentInfo info;
    (void)info.key;
    (void)info.offset;
    (void)info.path;
    (void)info.range_left;
    (void)info.range_right;
    (void)info.kind;
    (void)info.state;
    (void)info.size;
    (void)info.downloaded_size;
    (void)info.download_finished_time;
    (void)info.cache_hits;
    (void)info.references;
    (void)info.is_unbound;
    (void)info.queue_entry_type;
    (void)info.origin;
}

TEST(FileSegmentInfoTest, KindToString)
{
    EXPECT_EQ(toString(FileSegmentKind::Regular), "Regular");
    EXPECT_EQ(toString(FileSegmentKind::Ephemeral), "Ephemeral");
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 4: Write `FileSegmentTest.cpp` (red)**

Create `velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp`:

```cpp
#include "velox/ch/Interpreters/FileCache/FileSegment.h"
#include "velox/ch/Interpreters/FileCache/FileCache.h"
#include "velox/ch/Common/FileCacheQueryIdScope.h"
#include <folly/system/ThreadId.h>
#include <gtest/gtest.h>

namespace facebook::velox::ch
{
namespace
{

// Range is inclusive [left, right].
TEST(RangeTest, SizeIsRightMinusLeftPlusOne)
{
    FileSegment::Range r{10, 19};
    EXPECT_EQ(r.size(), 10ULL);
}

TEST(RangeTest, ContainsPoint)
{
    FileSegment::Range r{5, 10};
    EXPECT_TRUE(r.contains(5));
    EXPECT_TRUE(r.contains(10));
    EXPECT_FALSE(r.contains(4));
    EXPECT_FALSE(r.contains(11));
}

TEST(RangeTest, ContainsRange)
{
    FileSegment::Range outer{0, 100};
    FileSegment::Range inner{10, 50};
    FileSegment::Range overlap{80, 110};
    EXPECT_TRUE(outer.contains(inner));
    EXPECT_FALSE(outer.contains(overlap));
}

TEST(RangeTest, StrictOrderingNonOverlapping)
{
    FileSegment::Range a{0, 9};
    FileSegment::Range b{10, 19};
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(CreateFileSegmentSettingsTest, RegularIsBounded)
{
    CreateFileSegmentSettings s;
    EXPECT_EQ(s.kind, FileSegmentKind::Regular);
    EXPECT_FALSE(s.unbounded);
}

TEST(CreateFileSegmentSettingsTest, EphemeralIsUnbounded)
{
    CreateFileSegmentSettings s{FileSegmentKind::Ephemeral};
    EXPECT_EQ(s.kind, FileSegmentKind::Ephemeral);
    EXPECT_TRUE(s.unbounded);
}

TEST(CallerIdTest, SameScopeStableId)
{
    FileCacheQueryIdScope scope("q1");
    auto id1 = FileSegment::getCallerId();
    auto id2 = FileSegment::getCallerId();
    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, "None:" + std::to_string(folly::getOSThreadID()));
}

TEST(CallerIdTest, NoScopeBackgroundId)
{
    // Without a query scope, caller is "None:<tid>".
    auto id = FileSegment::getCallerId();
    EXPECT_TRUE(id.rfind("None:", 0) == 0);
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 5: Write `MetadataTest.cpp` (red)**

Create `velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp`:

```cpp
#include "velox/ch/Interpreters/FileCache/Metadata.h"
#include "velox/ch/Interpreters/FileCache/FileCache.h"
#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/common/testutil/TempDirectoryPath.h"
#include <gtest/gtest.h>
#include <filesystem>

namespace facebook::velox::ch
{
namespace
{

using common::testutil::TempDirectoryPath;
namespace fs = std::filesystem;

TEST(PathLayoutTest, RegularDownloadingFilename)
{
    // Downloading segment: filename is just the offset decimal string.
    EXPECT_EQ(
        CacheMetadata::getFileNameForFileSegment(
            100, FileSegmentKind::Regular, std::nullopt),
        "100");
}

TEST(PathLayoutTest, RegularDownloadedFilename)
{
    // Downloaded segment: "<offset>_<size>".
    EXPECT_EQ(
        CacheMetadata::getFileNameForFileSegment(
            100, FileSegmentKind::Regular, 512),
        "100_512");
}

TEST(PathLayoutTest, EphemeralFilename)
{
    // Ephemeral segment: "<offset>_temporary".
    EXPECT_EQ(
        CacheMetadata::getFileNameForFileSegment(
            0, FileSegmentKind::Ephemeral, std::nullopt),
        "0_temporary");
}

TEST(LockedKeyTest, MemberOrderDestructionSafety)
{
    // lock member must be declared after key_metadata in LockedKey to ensure
    // the lock releases before the metadata shared_ptr drops.
    // Verify by inspecting static member offsets:
    static_assert(
        offsetof(LockedKey, key_metadata) < offsetof(LockedKey, lock),
        "LockedKey::lock must be declared after key_metadata");
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 6: Write `FileCacheTest.cpp` (red)**

Create `velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp`:

Build one real fixture from the final Task 012 production constructors and injected
runtime services. Do not write an abbreviated constructor or null/fake dependency.
Implement executable tests for:

```text
InitializeOnce
GetDoesNotCreateMetadata
GetOrSetCreatesHoles
TryReserveEvictsReleasable
ShutdownJoinsWorkers
SecondProcessStatusLockFails
InternalOriginAccessAllKeys
CommonOriginIsInjectedUserId
```

Each test must assert the production state after the call. The shutdown case must
use worker/timer probes proving completion rather than relying only on absence of a
hang.

- [ ] **Step 7: Write `QueryLimitTest.cpp` (red)**

Create `velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp`:

Use the real `FileCache` lock and query-limit APIs. Implement every query-limit case
listed in the mandatory executable-test table at the top of this task. The doomed
context test must use a destructor callback that reacquires the cache write lock,
which proves destruction occurred after lock release without using sleep.

- [ ] **Step 8: Verify the red build**

Configure:

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`. For `home-chang`, also add
`-DVELOX_BUILD_TESTING=ON` (already present in the `root-oss` effective
configuration). Redirect output to `<velox_build_dir>/configure_task_012_scc.log`.

Then attempt the build, expecting failure:

```bash
if <ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_core_scc_test \
  > <velox_build_dir>/build_task_012_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected: configure succeeds, build fails because SCC headers and `.cpp` files
do not exist. If configure fails for another reason, stop and report instead of
continuing.

- [ ] **Step 9: Implement `FileSegmentInfo.h`**

`FileSegmentInfo.h` is a pure leaf with no mutual dependencies. Implement the
exact enum layouts verified by the tests:

```cpp
#pragma once

#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h"
#include "velox/ch/Interpreters/FileCache/IFileCachePriority.h"

#include <cstdint>
#include <chrono>
#include <string>

namespace facebook::velox::ch
{

enum class FileSegmentState : uint8_t
{
    DOWNLOADED = 0,
    EMPTY = 1,
    DOWNLOADING = 2,
    PARTIALLY_DOWNLOADED_NO_CONTINUATION = 3,
    PARTIALLY_DOWNLOADED = 4,
    DETACHED = 5,
};

enum class FileSegmentKind : uint8_t
{
    Regular = 0,
    Ephemeral = 1,
};

// Defined in FileSegment.cpp (no separate FileSegmentInfo.cpp created).
std::string toString(FileSegmentKind kind);

struct FileSegmentInfo
{
    FileCacheKey key;
    uint64_t offset = 0;
    std::string path;
    uint64_t range_left = 0;
    uint64_t range_right = 0;
    FileSegmentKind kind = FileSegmentKind::Regular;
    FileSegmentState state = FileSegmentState::EMPTY;
    uint64_t size = 0;
    uint64_t downloaded_size = 0;
    std::chrono::time_point<std::chrono::steady_clock> download_finished_time{};
    uint64_t cache_hits = 0;
    uint32_t references = 0;
    bool is_unbound = false;
    IFileCachePriority::QueueEntryType queue_entry_type
        = IFileCachePriority::QueueEntryType::None;
    FileCacheOriginInfo origin;
};

} // namespace facebook::velox::ch
```

- [ ] **Step 10: Implement `FileSegment.h`**

The full `FileSegment.h` declares:

- `CreateFileSegmentSettings` (kind, unbounded)
- `Range` struct: inclusive `[left, right]`, `size`, `contains`, `operator<`
- `FileSegment` class with all API groups:
  - Constant state: `range`, `key`, `offset`, `kind`, `path`
  - Any-holder: `getOrSetDownloader`, `isDownloader`, `wait`,
    `getDownloadedSize`, `getReservedSize`, `getCurrentWriteOffset`,
    `detach`, `complete`, `increasePriority`
  - Cache-internal: `segmentLock`, `priorityIterator`, `keyMetadata`
  - Downloader-only: `reserve`, `write`, `getRemoteFileReader`,
    `setRemoteFileReader`, `resetRemoteFileReader`,
    `extractRemoteFileReader`, `getLocalCacheWriter`,
    `completePartAndResetDownloader`, `resetDownloader`
  - Static: `getCallerId`
- `FileSegmentsHolder` RAII type
- Required type aliases:
  - `RemoteFileReaderPtr = std::shared_ptr<ReadBufferFromVeloxReadFile>`
  - `LocalCacheWriterPtr = std::shared_ptr<WriteBufferFromVeloxWriteFile>`
  - `FileSegmentsHolderPtr = std::shared_ptr<FileSegmentsHolder>`

Key invariants to encode in the header:

```text
Range::size() == right - left + 1
FileSegment is non-copyable and non-movable
FileSegmentsHolder is move-only
is_unbound and background_download_enabled are immutable after construction
size_in_filename transitions false -> true only (atomic)
terminal states DOWNLOADED and DETACHED are published last after all fields final
```

`wait` signature injects a cancellation token:

```cpp
FileSegmentState wait(
    size_t offset,
    const folly::CancellationToken & cancellation_token);
```

- [ ] **Step 11: Implement `Metadata.h`**

`Metadata.h` declares the following types. Do not split into separate files.

**`FileSegmentMetadata`**

```cpp
struct FileSegmentMetadata
{
    explicit FileSegmentMetadata(std::shared_ptr<FileSegment> file_segment_);

    bool releasable() const
    {
        return file_segment.use_count() == 1;
    }

    size_t size() const;

    const std::shared_ptr<FileSegment> file_segment;
    bool removed = false;
};
using FileSegmentMetadataPtr = std::shared_ptr<FileSegmentMetadata>;
```

**`KeyMetadata`** inherits `std::map<size_t, FileSegmentMetadataPtr>` (ordered,
not F14; lower_bound and adjacency queries depend on ordering).

Members:

```text
const FileCacheKey key
const std::shared_ptr<FileCacheOriginInfo> origin  (shared / deduped)
KeyState: ACTIVE / REMOVING / REMOVED
KeyGuard for external callers
std::atomic<bool> created_base_directory
```

Methods: `lock`, `tryLock`, `lockNoStateCheck`, `createBaseDirectory`,
`getPath`, `getFileSegmentPath` overloads, `checkAccess`, `assertAccess`,
download/cleanup queue submission.

**`CacheMetadata`**

Must declare:
- 1024-bucket shard array, each bucket is
  `folly::F14FastMap<FileCacheKey, KeyMetadataPtr, FileCacheKeyHash>` wrapped
  with a per-bucket `CacheMetadataGuard`
- origin dedup pool (ShardedMap of `FileCacheOriginInfo`)
- `CleanupQueue` (handwritten internal type, NOT `FileCacheBoundedQueue`):
  uses `folly::F14FastSet` for deduplication, mutex+cv for blocking pop,
  `cancel` flag and `notify_all` on cancel
- `DownloadQueue` (handwritten internal type, NOT `FileCacheBoundedQueue`):
  uses `std::queue` of `DownloadInfo`, bounded capacity, mutex+cv, cancel flag
- download worker vector: `std::vector<std::shared_ptr<DownloadThread>>`
- cleanup worker thread
- client-access callback

**`DownloadInfo`** struct:

```cpp
struct DownloadInfo
{
    FileCacheKey key;
    uint64_t offset = 0;
    std::weak_ptr<FileSegment> segment; // must not be removed; see design
};
```

**`LockedKey`**

Member order (must be preserved for correct destruction sequence):

```cpp
class LockedKey
{
public:
    // ...
private:
    // Declaration order determines destruction order.
    // lock must be destroyed BEFORE key_metadata drops its shared reference.
    const std::shared_ptr<KeyMetadata> key_metadata;
    KeyGuard::Lock lock;
};
```

`LockedKey` provides: map iteration/lower_bound, `get`/`tryGet` by offset,
`removeFileSegment` variants, `removeAllReleasableSegments`,
`submitToDownloadQueue`, range intersection, empty-key delayed cleanup,
metadata/file sync.

**`CacheMetadata::Iterator` and `BatchedIterator`**

Declare both as nested classes with distinct locking contracts:

```text
Iterator:      one segment per next; not thread-safe
BatchedIterator: one non-empty bucket batch per nextBatch;
                sequential calls may run on different threads (no concurrent calls)
```

- [ ] **Step 12: Implement `QueryLimit.h`**

The `QueryLimit.h` header declares:

**`FileCacheQueryLimit`**

```cpp
class FileCacheQueryLimit
{
public:
    struct QueryContext;
    using QueryContextPtr = std::shared_ptr<QueryContext>;

    struct QueryContextHolder
    {
        QueryContextHolder() = default;
        QueryContextHolder(
            std::string query_id_,
            FileCache * cache_,
            FileCacheQueryLimit * limit_,
            QueryContextPtr ctx_);

        QueryContextHolder(const QueryContextHolder &) = delete;
        QueryContextHolder & operator=(const QueryContextHolder &) = delete;
        QueryContextHolder(QueryContextHolder &&) = default;
        QueryContextHolder & operator=(QueryContextHolder &&) = default;

        ~QueryContextHolder();

        QueryContextPtr context;

    private:
        std::string query_id;
        FileCache * cache = nullptr;
        FileCacheQueryLimit * limit = nullptr;
    };

    QueryContextPtr tryGetQueryContext(
        const CacheStateGuard::Lock & state_lock);

    QueryContextPtr getOrSetQueryContext(
        const std::string & query_id,
        const FileCacheReadOptions & options,
        const CachePriorityGuard::WriteLock & write_lock);

    void removeQueryContext(
        const std::string & query_id,
        QueryContextPtr & doomed,
        const CachePriorityGuard::WriteLock & write_lock);

private:
    folly::F14FastMap<std::string, QueryContextPtr> query_map;
    std::mutex query_map_mutex;
};

using FileCacheQueryLimitPtr = std::unique_ptr<FileCacheQueryLimit>;
```

**`QueryContext`**

```cpp
struct FileCacheQueryLimit::QueryContext
{
    explicit QueryContext(
        size_t query_cache_size,
        bool recache_on_limit_exceeded_);

    QueryContext(const QueryContext &) = delete;
    QueryContext & operator=(const QueryContext &) = delete;

    IFileCachePriority::IteratorPtr tryGet(
        const FileCacheKey & key,
        size_t offset,
        const CachePriorityGuard::WriteLock &);

    void add(
        KeyMetadata & key_metadata,
        size_t offset,
        size_t size,
        const CachePriorityGuard::WriteLock &);

    void remove(
        const FileCacheKey & key,
        size_t offset,
        const CachePriorityGuard::WriteLock &);

    bool recache_on_limit_exceeded;
    LRUFileCachePriority priority;

private:
    folly::F14FastMap<
        FileCacheKeyAndOffset,
        IFileCachePriority::IteratorPtr,
        FileCacheKeyAndOffsetHash>
        records;
};
```

- [ ] **Step 13: Implement `FileCache.h`**

`FileCache.h` is the public API apex of the SCC. Key declarations:

**`FileCacheReserveStat`** — exact field list:

```cpp
struct FileCacheReserveStat
{
    struct Stat
    {
        size_t releasable_size = 0;
        size_t releasable_count = 0;
        size_t non_releasable_size = 0;
        size_t non_releasable_count = 0;
    };

    // indexed by static_cast<uint8_t>(FileSegmentKind)
    std::array<Stat, 2> stat_by_kind{};
    Stat total;
    size_t evicting = 0;
    size_t moving = 0;
    size_t invalidated = 0;
    size_t candidates_iterated = 0;
    size_t clients_iterated = 0;
};
```

**`FileCache` public API groups** (all must be declared; no TBD):

```text
lifecycle:
  void initialize()
  bool isInitialized() const
  void deactivateBackgroundOperations()

origin/path:
  FileCacheOriginInfo getCommonOrigin() const
  static FileCacheOriginInfo getInternalOrigin()
  FileCacheOriginInfo getCommonOriginWithSegmentKeyType(FileSegmentKeyType) const
  std::string getFileSegmentPath(const FileCacheKey &, uint64_t offset,
      FileSegmentKind, const KeyMetadata &) const
  std::string getKeyPath(const FileCacheKey &, const KeyMetadata &) const

lookup/create:
  FileSegmentsHolder getOrSet(const FileCacheKey &, FileSegment::Range,
      const FileCacheOriginInfo &, const FileCacheReadOptions &)
  FileSegmentsHolder get(const FileCacheKey &, FileSegment::Range,
      const FileCacheOriginInfo &)
  FileSegmentsHolder getDownloadedContiguousOrEmpty(
      const FileCacheKey &, FileSegment::Range,
      const FileCacheOriginInfo &)
  FileSegmentsHolder set(const FileCacheKey &, uint64_t offset, uint64_t size,
      const FileCacheOriginInfo &, const CreateFileSegmentSettings &)
  FileSegmentsHolder trySet(const FileCacheKey &, uint64_t offset, uint64_t size,
      const FileCacheOriginInfo &, const CreateFileSegmentSettings &)

reservation/priority:
  bool tryReserve(FileSegment &, size_t size, const FileCacheReadOptions &)
  void tryIncreasePriority(FileSegment &)
  CachePriorityGuard::WriteLock lockCache()

remove/admin:
  void removeFileSegment(const FileCacheKey &, uint64_t offset,
      const FileCacheOriginInfo &)
  void removeKey(const FileCacheKey &, const FileCacheOriginInfo &)
  void removePathIfExists(const std::string & path, const FileCacheOriginInfo &)
  void removeAllReleasable(const FileCacheOriginInfo &)
  void sync()
  CacheMetadata::Iterator getCacheIterator()
  std::vector<FileSegmentInfo> getFileSegmentInfos(
      const FileCacheKey &, const FileCacheOriginInfo &)
  std::string dumpQueue() const
  FileCacheUsage getUsage() const

settings/stats:
  void applySettingsIfPossible(const FileCacheConfig & new_config,
      FileCacheConfig & current_config)
  size_t capacity() const
  size_t getUsedCacheSize() const
  size_t getFileSegmentsNum() const

query limit:
  FileCache::QueryContextHolderPtr getQueryContextHolder(
      const std::string & query_id, const FileCacheReadOptions &)
```

`getQueryContextHolder` returns `std::unique_ptr<FileCacheQueryLimit::QueryContextHolder>`.

**Member order** for correct destruction sequence:

```text
main_priority   declared before metadata
  -> metadata destroyed first, priority iterators remain valid
StatusFile      held for full FileCache lifetime
metadata        CacheMetadata
query_limit     optional FileCacheQueryLimitPtr
```

- [ ] **Step 14: Implement the `.cpp` files**

First finish every Task-011 priority/eviction `.cpp` against the real
`FileCache`/`Metadata`/`FileSegment` types. Remove no public method and add no
compatibility stub. Then implement the center-SCC `.cpp` files below.

All four `.cpp` files must be added before attempting the final build. There is
intentionally no intermediate link step; partial `.cpp` presence is not
expected to link.

### `FileSegment.cpp`

Implement the exact state machine and invariants from
`port/2-file-cache/09-filecache-file-segment-design.md`. Key points:

- `toString(FileSegmentKind)` is defined here (not in a separate file).
- `getCallerId` queries `FileCacheQueryIdScope::currentQueryId()` and
  `folly::getOSThreadID()`:
  - non-empty query id → `"<query-id>:<tid>"`
  - empty query id → `"None:<tid>"`
- `getOrSetDownloader` election and state transition happen under `segment_guard`.
- `wait` slices in one-second increments, checks the cancellation token each
  slice, and returns after 60 seconds without blocking indefinitely.
- `reserve`: calls `cache_->tryReserve(*this, size, options)`.
- `write` short-write path: reconciles `downloaded_size` with actual on-disk
  file size before propagating the write exception; never leaves
  `downloaded_size > actual on-disk size`.
- Final rename `<offset>` → `<offset>_<size>` precedes publishing `DOWNLOADED`.
  Rename failure keeps legacy `<offset>` path; `size_in_filename` stays false.
- `FileSegmentsHolder::reset` catches and logs (no-op shim) per-segment
  completion exceptions; continues cleaning remaining segments.
- `detach` sequence: clear downloader → publish `DETACHED` → reset
  `key_metadata` weak_ptr → reset priority iterator → cancel writer →
  release `DownloadState`.

### `Metadata.cpp`

Implement all eight sections from
`port/2-file-cache/08-filecache-metadata-files-design.md`:

1. Metadata wrappers and origin pool
2. Key locking and path layout
3. Bucket lookup and key state recovery (four `KeyNotFoundPolicy` behaviors)
4. `IteratorImpl` / `BatchedIteratorImpl`
5. Key removal and directory cleanup
6. `CleanupQueue` with F14FastSet deduplication, cancel, notify_all
7. `DownloadQueue` and workers: bounded std::queue, weak_ptr identity check,
   per-worker `stopFlag` under queue mutex, resize join sequence
8. Worker shutdown and resize ordering

Path layout invariant:

```text
Regular downloading:  <offset>
Regular downloaded:   <offset>_<size>
Ephemeral:           <offset>_temporary
```

Key path invariant:

```text
without per-user:  <base>/<segment-prefix>/<first-3-key-chars>/<full-key>
with per-user:     <base>/<segment-prefix>/<user-id>.<weight>/<first-3-key-chars>/<full-key>
```

`REMOVING` key reactivation by `CREATE_EMPTY`: cancel delayed removal, restore
`ACTIVE`, return same locked key.

`DownloadInfo` must carry `weak_ptr<FileSegment>` in addition to key+offset.
Using key+offset alone for identity would accept a new segment created at the
same offset after the original was deleted.

Replace `OpenedFileCache::instance().remove(...)` with the manager-owned
opened-file cache invalidation reference/callback injected at construction.

### `QueryLimit.cpp`

Implement `port/2-file-cache/11-filecache-query-limit-design.md`:

- `tryGetQueryContext`: acquires `query_map_mutex`, looks up current query id
  from `FileCacheQueryIdScope::currentQueryId()`, returns shared pointer.
- `getOrSetQueryContext`: creates or reuses `QueryContext` under
  `query_map_mutex`.
- `removeQueryContext`: moves context out of map under mutex; caller destroys
  the doomed context **after** releasing the `CachePriorityGuard::WriteLock`.
- `QueryContextHolder::~QueryContextHolder`: acquires cache write lock, calls
  `removeQueryContext`, releases lock, then lets doomed context go out of scope.
  Must not throw.

### `FileCache.cpp`

Implement `port/2-file-cache/10-filecache-core-files-design.md`:

- `initialize`: uses `std::call_once` (retry-on-exception semantics, matching
  CH `callOnce`); acquires `StatusFile` process lock; dispatches sync or async
  metadata initialization.
- Scheduler task names include the cache name:
  `"FileCache:<name>:background-cleanup"` and `"FileCache:<name>:free-space"`.
- `getImpl`: `lower_bound(range.left)`, includes previous segment when it
  overlaps, respects `fileSegmentsLimit`, bypass-threshold shortcut for large
  ranges returns one synthetic `DETACHED` segment.
- `fillHolesWithEmptyFileSegments`: `getOrSet` path creates metadata-owned
  `EMPTY` segments; `get` path creates synthetic `DETACHED` placeholders.
- `doTryReserve` fast path requires: main iterator exists AND no main eviction
  needed AND `query_context == nullptr`.
- Background free-space keeper: collector/remover/finalizer pipeline using two
  `FileCacheBoundedQueue<EvictionBatchPtr>` instances. `running_removers` must
  be incremented **before** submitting each remover task to the worker pool;
  roll back on submission failure.
- Metadata load: parallel listing/loading with
  `FileCacheBoundedQueue<KeyDirectoryWork>` capacity 1000 when workers > 0;
  listing producers use `tryPush`, falling back to direct load when the queue
  is full or capacity is 0; last listing worker calls `finish`.
- Overcommit policy (`LRU_OVERCOMMIT` / `SLRU_OVERCOMMIT`): explicitly reject
  with `VELOX_FAIL`; do not stub.

- [ ] **Step 15: Update `CMakeLists.txt`**

Append the compiled core library to
`velox/ch/Interpreters/FileCache/CMakeLists.txt`. Preserve the existing
`target_sources(velox_ch_filecache ...)` entries from Tasks 008 and 010 and the
existing `add_subdirectory(tests)` block:

```cmake
velox_add_library(
  velox_ch_filecache_core
  IFileCachePriority.cpp
  LRUFileCachePriority.cpp
  SLRUFileCachePriority.cpp
  SplitFileCachePriority.cpp
  EvictionCandidates.cpp
  FileSegment.cpp
  Metadata.cpp
  FileCache.cpp
  QueryLimit.cpp
)

target_link_libraries(
  velox_ch_filecache_core
  PUBLIC
    velox_ch_filecache
    velox_file
    velox_memory
    Folly::folly
    fmt::fmt
)

if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Do not replace or remove leaf, ShardedMap, or settings test targets from the
shared tests CMake file.

- [ ] **Step 16: One final build**

Reconfigure using the same command as Step 8, then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_core_scc_test \
  > <velox_build_dir>/build_task_012_scc.log 2>&1
```

Expected: exit code 0. This is the single compile/link closure that proves the
SCC is complete.

- [ ] **Step 17: Run the focused tests**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_filecache_core_scc_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_012_scc.log 2>&1
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 18: Inspect task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Interpreters/FileCache/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/CacheUsage.h \
  velox/ch/Interpreters/FileCache/IFileCachePriority.h \
  velox/ch/Interpreters/FileCache/IFileCachePriority.cpp \
  velox/ch/Interpreters/FileCache/LRUFileCachePriority.h \
  velox/ch/Interpreters/FileCache/LRUFileCachePriority.cpp \
  velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h \
  velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp \
  velox/ch/Interpreters/FileCache/SplitFileCachePriority.h \
  velox/ch/Interpreters/FileCache/SplitFileCachePriority.cpp \
  velox/ch/Interpreters/FileCache/EvictionCandidates.h \
  velox/ch/Interpreters/FileCache/EvictionCandidates.cpp \
  velox/ch/Interpreters/FileCache/FileSegmentInfo.h \
  velox/ch/Interpreters/FileCache/FileSegment.h \
  velox/ch/Interpreters/FileCache/FileSegment.cpp \
  velox/ch/Interpreters/FileCache/Metadata.h \
  velox/ch/Interpreters/FileCache/Metadata.cpp \
  velox/ch/Interpreters/FileCache/FileCache.h \
  velox/ch/Interpreters/FileCache/FileCache.cpp \
  velox/ch/Interpreters/FileCache/QueryLimit.h \
  velox/ch/Interpreters/FileCache/QueryLimit.cpp \
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp \
  velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp \
  velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp
```

Expected: no whitespace errors, no files outside the declared scope changed by
this task, changes remain unstaged and uncommitted.

- [ ] **Step 19: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/012-filecache-core-scc-result.md
```

Use exactly this structure:

````markdown
# Task 012 Result: `FileCache` Center SCC

## Status

status: success

## Velox status

```text
<paste branch, HEAD, and final `git status --short`>
```

## Files changed

```text
<list only task-owned files>
```

## Commands run

```text
<paste configure, build, test, and verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_task_012_scc.log
<velox_build_dir>/build_task_012_red.log
<velox_build_dir>/build_task_012_scc.log
<velox_build_dir>/test_task_012_scc.log
```

## Verification

```text
Red build failed because SCC headers and .cpp files were absent.
Final build exit code:
Focused test result:
git diff --check result:
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 013: FileCacheFactory and FileCacheManager.
```
````

If blocked or failed, set the status accordingly, include the first actionable
error and log path, and do not claim success.

## Explicit Exclusions

Do not implement in this task:

```text
FileCacheFactory / FileCacheManager
FileCacheBufferedInput / FileCacheInputStream
FileCacheRequestContext / FileCacheFileIdentity
WriteBufferToFileSegment / TemporaryDataOnDisk
CacheFileSystem / CachedReadFile
cache_on_write_operations
LRU_OVERCOMMIT / SLRU_OVERCOMMIT implementations
Prometheus/custom metrics (keep no-op shims)
Gluten integration
```

These belong to Tasks 013 and 014.
