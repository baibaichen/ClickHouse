# Full Review: Tasks 003-010 — `root-oss` Review 1 Evidence (corrected — structural + behavioral)

Phase-D single-agent, read-only, cross-repository **source-contract and
leaf-implementation-structure** review of the accepted ClickHouse → Velox
`FileCache` port (Tasks 003-010), performed through the Task-011/012
center-SCC consumer lens **and** the guide §3 structural direct-translation
lens.

This revision corrects the prior report, which the Controller rejected as
incomplete: it reviewed external/consumer behavior but did **not** audit the
internal implementation structure (state representation, mutex/CV/lock order,
containers and stability, ownership/member/destruction order, async lifetime)
that guide §3 requires. This revision adds a complete per-task structural
parity audit, corrects the enum counts and ownership, reclassifies M2, revisits
`getCallerId`, and registers every unregistered structural deviation found.

- ClickHouse source truth: `/root/oss/clickhouse` @ `da28e83e8b3cb69090624b0a0b1f13cd78c13279`
  (branch `ch-filecache`; `FileCache` relocated to `src/Interpreters/FileCache/`).
  Working-tree HEAD `d29cdd2ca71` is a docs-only descendant
  (`git diff da28e83e HEAD -- src/Interpreters/FileCache/` is empty), so `src/`
  is byte-for-byte the frozen baseline.
- Accepted Velox implementation: `/root/oss/velox` @ `89039901aa4287ce811a3b1628867b0796c76678`
  (exact accepted commit; port under `velox/ch/`). Both baselines confirmed.
- Method: I reconstructed each CH consumer contract **and read the CH leaf
  internals** (`ConcurrentBoundedQueue.h`, `ThreadPool.h`, `BackgroundSchedulePool.h`,
  `Guards.h`, `ShardedMap.h`, buffer bases, `callOnce.h`, `getCallerId`),
  then read the complete accepted Velox implementation source (not receipts,
  not diffs) and its focused tests, then reconciled structure-to-structure.

---

## 0. Scope, baselines, blind spots

**Verdict subjects:** accepted Tasks 003-010 only. Tasks 011/012 are the
consumer + internal-structure lens; they receive **no** verdict.
`task_011_allowed` is adjudicated in §8.

**What is newly in scope in this revision (the Controller's correction):** the
guide §3 rule — *consumer-invisible internal structure (state representation,
lock structure & order, containers & their stability guarantees) must be
`exactly` direct-translated from CH unless a hard Velox-primitive constraint
forbids it, and every non-1:1 translation must be registered in the
structure-deviation ledger with CH→Velox→hard-constraint→evidence, with
human sign-off required for any deviation that is **not** hard-constraint
forced.* §3 is explicitly "有牙" (has teeth): a guarantee-changing swap that
Velox does not force is unresolved until a human signs off.

**Enum-scan method (reproducible):** `grep -roP 'extern const Event \K\w+'`
and `'extern const Metric \K\w+'` over `src/Interpreters/FileCache/`, deduped,
diffed against the enumerators defined in `velox/ch/Common/ProfileEvents.h`
and `CurrentMetrics.h`. Numbers in §1/§6 are from that scan.

**Blind spots / declared non-coverage:**
- Tasks 011/012 are not implemented; all `ST*/CT*/MO*/ZI*` structural rows that
  live inside not-yet-ported files (`IFileCachePriority`, `LRU/SLRU/Split`,
  `EvictionCandidates`, `FileSegment`, `Metadata`, `FileCache`, `QueryLimit`)
  are **future obligations**. This review judges structure only where a 003-010
  deliverable already fixes it (`Guards.h`, `FileCache_fwd_internal.h`,
  `ShardedMap.h`, the buffer bases, the thread pool, the scheduler).
- I did not build either tree; behavioral claims rely on reading test bodies +
  the Controller-rerun E-probe artifact `files/task-e-writer-probe.md`.
- Fault injection (`FAIL_POINT_TRIGGER` no-op) is unobservable (F-D10).

---

## 1. Independent CH contract reconstruction and A/D reconciliation

My reconstruction agrees with the A ledger's dependency set and owner map on
the substantive contracts. Differences are cases where A's inline text is
imprecise, or where A (by design never inspecting Velox) could not see the
implementation is incomplete or structurally divergent — the latter is exactly
what Phase D adds.

| # | A ledger statement | Independent CH source finding | Reconciliation |
|---|---|---|---|
| R1 | D5 "ProfileEvents (42 distinct)", owner 003 | **44** distinct `extern const Event` across `src/Interpreters/FileCache/` | Prior D report said "41"; correct is **44**. `velox/ch/Common/ProfileEvents.h` defines **16**; **13** referenced are present, **31** referenced are missing, **3** defined are outside the source set (`CachedReadBuffer*` trio → Task 014). → Blocker **B1** (counts corrected in §6). |
| R2 | D6 "CurrentMetrics (18 distinct)" | **15** distinct `extern const Metric` in `src/Interpreters/FileCache/` | `velox/ch/Common/CurrentMetrics.h` defines **6**; **6** present, **9** missing, **0** extra. Prior D report said "15 in-scope / 6 / 9 missing" — the 15/6/9 is right; the *which-are-blockers* text was wrong (fixed in §6 B2). |
| R3 | D30 policy enum order | Real CH `Core/SettingsEnums.h:462` = `{LRU,SLRU,SLRU_OVERCOMMIT,LRU_OVERCOMMIT}` | A text loose; Velox `FileCache_fwd.h:24` matches real CH. No drift. |
| R4 | D28 key type set | Real CH `FileSegmentKeyType.h:8-12` = `{General=0,System,Data}` | Velox `FileSegmentKeyType.h:24-28` matches values exactly. No drift. |
| R5 | D1 `void finish()` | CH `ConcurrentBoundedQueue::finish()` returns `bool` | Return discarded at all 5 center-SCC sites; `void` faithful. No drift. |
| R6 | SD1 "human sign-off REQUIRED — not on file" | `ShardedMap.h:60` inner map is `folly::F14FastMap`; no artifact | Confirmed; **Major M1** unresolved. |
| R7 | Enum owner = Task 003 | Headers **created by Task 002** (`002-common-noop-shims.md:56-157`), extended by Task 004 (`f948fb6a4`), real impl owned by future Task 017 (`017-...:30-33,443`) | **Ownership correction:** the ledger owner-map assigns the *compatibility surface* to 003, but the files are Task-002 no-op shims; the enumerator-name surface must be completed as a shim before 011/012 (see §6 B1/B2). |

A ownership decisions H1-H3 (ledger §9) and over-ports O1-O6 were adjudicated
against the implementation; results folded into §2/§3/§6.

---

## 2. Per-task verdicts

Status legend: **matches** / **drift** / **hole** / **over-port** /
**structural-remap** (non-1:1 internal translation) / **unproven**.

### Task 003 — basic common shims — verdict: **REOPEN (enum surface only)**

| Contract / structure row | CH ref | Velox ref | Status |
|---|---|---|---|
| D1 bounded queue overloads (push/tryPush×2/pop/tryPop×2/finish; cap 0 valid; move-or-copy-if-throw) | `ConcurrentBoundedQueue.h` | `FileCacheBoundedQueue.h:47-174` | matches (structural parity §3.1) |
| D2 exception surface (throw + `getErrno`/`addMessage`, code distinction) | `FileSegment.cpp:501-539` | `FileCacheException.h:27-36` | matches (approved substitution; errno loss covered by E1) |
| `chassert` debug/sanitizer abort, release no-eval | `base/defines.h` | `ClickHouseAssert.h:48-73` | matches |
| **D5 ProfileEvents surface** | **44** `extern const Event` | `ProfileEvents.h:11-26` defines **16** (13 in-set) | **hole — Blocker B1** |
| **D6 CurrentMetrics surface** | **15** `extern const Metric` | `CurrentMetrics.h:11-18` defines **6** | **hole — Blocker B2** |
| D4 logger name-only / non-throwing | `FileCache.cpp:304` | `logger_useful.h:29-71` | matches |
| D7 String/UInt aliases | pervasive | `ClickHouseAliases.h:24,29,30` | matches |
| D10 fail-points | `FileSegment.cpp:486` | `FailPoint.h` no-op | drift (fault injection disabled) |
| D8 `callOnce`/`OnceFlag` | `Common/callOnce.h` (`= std::call_once`/`std::once_flag`) | native-mapped by 012 (`012-...:1060`) | **matches (exact native mapping)** — reclassified out of M2 |
| D3/D9/D11/D12 `Memory<>`, `SCOPE_EXIT`, `Stopwatch`, `getThreadName` | `Metadata.cpp:939`; `FileSegment.cpp:494`; `FileCache.cpp:1923`; `FileSegment.cpp:257` | absent as CH shims | **Task-012 amendments / Task-006 driver** — reclassified out of 003 (§6 M2) |
| D13 `isSharedPtrUnique` | `Metadata.h:42` | 012 maps `use_count()==1` | matches |

**Reopen scope (003):** complete **only** the `ProfileEvents`/`CurrentMetrics`
enumerator-NAME surface on the shared shim headers (B1/B2) + add an
enum-coverage RED test with false-green probe. The M2 leaf helpers are **not** a
003 reopen (see §6 M2). The 003 REOPEN is now strictly the enum surface.

### Task 004 — StatusFile + Guards — verdict: **ACCEPT**

| Contract / structure row | CH ref | Velox ref | Status |
|---|---|---|---|
| StatusFile 3-line `write_full_info`; ctor flock(LOCK_EX,NB); dtor `closeNoThrow`+`unlink` non-throwing | `StatusFile.cpp:38-116` | `StatusFile.cpp:66-130`, dtor `141-144` | matches (single-owner RAII; no container/concurrency state — §3.2) |
| D15-19 lock order + distinct nested `Lock` types + `CacheStateGuard` `std::timed_mutex` | `Guards.h:52-53,104` | `Guards.h:44-176` | **matches (structural parity §3.2)** |
| `SharedMutex` | `Common/SharedMutex.h` | `SharedMutex.h`→`folly::SharedMutex` | matches (approved 1:1 infra swap) |

### Task 005 — thread pools — verdict: **ACCEPT (structural-remap SD6 must be registered)**

| Contract / structure row | CH ref | Velox ref | Status |
|---|---|---|---|
| D20 `ThreadFromGlobalPool` immediate start, join-rethrows, joinable, move-only, destroy-while-joinable aborts | `FileCache.cpp:534`; `Metadata.cpp:1024` | `ThreadPool.{h,cpp}` `FileCacheWorker` | matches behavior; **structural-remap SD6** |
| D21 `scheduleOrThrowOnError` (throw on full), `wait` (barrier, rethrow first) | `FileCache.cpp:583-589,1899` | `ThreadPool.cpp:161-261` | matches behavior; ctor drops 3 `CurrentMetrics` (ties B2); **structural-remap SD6** |
| Internal: job priority queue + worker list + 2 CVs | `Common/ThreadPool.h:224,193-194` | folly executor + `MeteredExecutor` + `backlog_`/`pending_`/`inFlight_` (`ThreadPool.h:132,204-212`) | **structural-remap — hard-constraint forced, UNREGISTERED (SD6)** |

Behavioral guarantees (throw-on-full, `wait` blocks until every `t()` runs incl.
backlog, join rethrows, destroy-joinable aborts, member-destruction order drains
completion lambdas) verified in source; the remap is forced (no CH
`GlobalThreadPool`/`ThreadPoolImpl` in Velox) but must be registered per §3.

### Task 006 — scheduler + caller scope — verdict: **REOPEN (`getCallerId` format) + register SD7/SD8**

| Contract / structure row | CH ref | Velox ref | Status |
|---|---|---|---|
| D22 immediate `schedule` preempts pending delayed | `BackgroundSchedulePool.cpp:47-55` | `FileCacheScheduler.cpp:104-126` | matches |
| scheduleAfter never downgrades pending immediate; Delayed re-arms (default `overwrite=true`) | `BackgroundSchedulePool.cpp:57-74` | `FileCacheScheduler.cpp:128-160` | matches |
| `deactivate` waits for in-flight callback; idempotent; safe after partial init | `BackgroundSchedulePool.cpp:76-` | `FileCacheScheduler.cpp:162-188` | matches (`cv_.wait` on `callbackInFlight_`) |
| Internal state machine: 4 booleans (`deactivated/scheduled/delayed/executing`) + 2 mutexes (`exec_mutex`,`schedule_mutex`) + `std::multimap` delayed queue + dedicated delay-thread | `BackgroundSchedulePool.h:204-213,92,149` | `enum class State` + `std::recursive_mutex` + folly `Timekeeper`/`Future` (`FileCacheScheduler.h:120,172,197,231`) | **structural-remap — SD7 (forced) + SD8 (recursive_mutex, guarantee-changing)** |
| **D23 `getCallerId` no-scope format** | `FileSegment.cpp:254-259` = `None:{getThreadName()}:{tid}` | `FileCacheQueryIdScope.cpp:49-56` = `None:{tid}` | **hole/drift — REOPEN F-CALLERID** (threadname component dropped; §6) |

### Task 007 — IO adapters — verdict: **ACCEPT (structural-mapping SD9 must be registered)**

| Contract / structure row | CH ref | Velox ref | Status |
|---|---|---|---|
| D24 writer `set/next/finalize/cancel`, `buf_size=0` zero-copy | `FileSegment.cpp:483-496` | `WriteBufferFromVeloxWriteFile.h:61-113` | matches |
| D25 reader relay `available/getFileOffsetOfBufferEnd/getPosition/setReadUntilPosition` | `FileSegment.cpp:370-412` | `ReadBufferFromVeloxReadFile.h:182-238` | matches (E3) |
| E1 partial-write physical-prefix reconcile | `FileSegment.cpp:498-531` | `IoAdaptersTest.cpp:779,852` + probe | matches (real `LocalWriteFile`) |
| Internal buffer state (`internal_`/`working_`/`position_`/`bytes_`) | CH `BufferBase` | `FileCacheBufferState` (`ReadBufferFromVeloxReadFile.h`) | **matches (faithful state repr)** |
| Owned buffer: CH `Memory<>`/`std::vector<char>` | `Metadata.cpp:939` | pool-charged `BufferPtr` | **structural-mapping SD9 (forced by velox MemoryPool; accounting delta)** |

### Task 008 — leaf types — verdict: **ACCEPT**

| Contract / structure row | CH ref | Velox ref | Status |
|---|---|---|---|
| D26 `fromKeyString` validates `size()!=32` then `unhexUInt<UInt128>` | `FileCacheKey.cpp:41-46` | `FileCacheKey.cpp:51-77` | matches |
| D27 `sipHash128` CH variant | `SipHash.h:78-90` | `SipHash128.cpp:50-124` | matches (pure fn, no state) |
| D28 `FileSegmentKeyType{General=0,System,Data}` | `FileSegmentKeyType.h:8-12` | `FileSegmentKeyType.h:24-28` | matches |
| D29 origin info + `OriginPoolKey`/`Hash` | `FileCacheOriginInfo.h` | `FileCacheOriginInfo.h:30-83` | matches (value types; **dead `F14Map.h` include :20 — no container declared**, harmless) |
| D31 `FileSegments=std::list` + fwd ptrs | `FileCache_fwd_internal.h` | `FileCache_fwd_internal.h:24-46` | matches (SD5 honored) |
| D32 `roundDown/UpToMultiple` overflow-checked | `FileCacheUtils.h` | `FileCacheUtils.h:28-53` | matches |
| D33 `FileCacheKeyAndOffset` | `QueryLimit.h:62` (named struct) | `FileCacheKey.h:56` (`std::pair`) | matches (trivial; **dead `F14Map.h` include :20**, harmless) |

### Task 009 — ShardedMap — verdict: **REOPEN (structural — M1/SD1)**

| Contract / structure row | CH ref | Velox ref | Status |
|---|---|---|---|
| D34 `withShard`/`forEachShard`/`size`; 32 shards; exception-safe size accounting; copy-out | `ShardedMap.h`; `Metadata.cpp:108-135` | `ShardedMap.h:55-134` | matches (API + copy-out + `std::array<Shard>` + atomic count faithful) |
| **SD1 container** `std::unordered_map`→`folly::F14FastMap` | `ShardedMap.h:24` | `ShardedMap.h:60` | **matches-but-unresolved — Major M1** (address-stability change; not forced; no sign-off) |
| `lock_wait_event` profiling | `ShardedMap.h` (`ProfiledMutexLock`) | stored, unused | drift (diagnostic no-op) |

### Task 010 — settings — verdict: **ACCEPT**

| Contract / structure row | CH ref | Velox ref | Status |
|---|---|---|---|
| D35 full ctor field surface (~35 fields) + `FileCacheSettings` alias | `FileCache.cpp:277-401` | `FileCacheSettings.h:33-100` | matches |
| Internal: CH PIMPL `BaseSettings` → plain value-type struct | `FileCacheSettings.h` (PIMPL `impl`) | `FileCacheConfig` (plain aggregate) | matches — **no concurrency/container/ownership state; benign under §3** |
| D30 policy enum + `FILECACHE_DEFAULT_*` | `Core/SettingsEnums.h:462` | `FileCache_fwd.h:24-46` | matches (order matches real CH; R3) |
| D37/O5 `FileCacheReadOptions` field surface | fwd-decl in center-SCC | fully defined | over-port (anticipatory Task 014; harmless) |

---

## 3. Leaf-implementation structural parity (guide §3)

Per-task audit of the five §3 dimensions — **state representation**,
**mutex/CV/lock order**, **containers & stability**, **ownership/member/
destruction order**, **async lifetime** — comparing CH internals to the accepted
Velox source. Rows marked **REMAP** are non-1:1 structural translations and are
registered in §4.

### 3.1 Task 003 — bounded queue (`FileCacheBoundedQueue`)

| Dimension | CH (`ConcurrentBoundedQueue.h`) | Velox (`FileCacheBoundedQueue.h`) | Verdict |
|---|---|---|---|
| State repr | `bool is_finished`, `size_t max_fill` | `bool finished_`, `size_t capacity_` | 1:1 |
| Mutex/CV | 1 `std::mutex` + 2 CVs (`push_condition`,`pop_condition`) | 1 `std::mutex` + 2 CVs (`producerCv_`,`consumerCv_`) | 1:1 |
| Containers & stability | `std::deque<T> queue` | `std::deque<T> queue_` (`:192`) | 1:1 (same container, same FIFO/back-front) |
| Ownership/member/destruction | value elements; nothrow-move-else-copy on pop (`detail::moveOrCopyIfThrow`) | `assignFront` = nothrow-move-else-copy (`:176-183`) | 1:1 |
| Async lifetime | synchronous MPMC | synchronous MPMC | 1:1 |

**Result:** faithful direct translation. No deviation. (Port omits the
front-push/back-pop overloads = O4, correctly unported.)

### 3.2 Task 004 — Guards + StatusFile

| Dimension | CH (`Guards.h`) | Velox (`Guards.h`) | Verdict |
|---|---|---|---|
| State repr | 5 guard structs; each nested `Lock` a distinct struct (compile-time non-interchangeable) | identical 5 structs, distinct `Lock` types (`:44-176`) | 1:1 |
| Mutex types | `CachePriorityGuard`=`SharedMutex`; `CacheStateGuard`=`std::timed_mutex`; others `std::mutex` | identical (`SharedMutex`→`folly::SharedMutex`, approved) | 1:1 |
| Lock order | `CachePriorityGuard > CacheMetadataGuard > KeyGuard > FileSegmentGuard`; `CacheStateGuard` after reservation (`Guards.h:52-53`) | same order documented + enforced by distinct `Lock` types (`:29-41`) | 1:1 |
| Ownership/member/destruction | RAII `unique_lock` returned by value | identical | 1:1 |
| Async lifetime | n/a (leaf mutexes) | n/a | 1:1 |

`StatusFile`: single-owner RAII (`const path_`, `folly::File`, `FillFunction`);
dtor `closeNoThrow`→`unlink` non-throwing (`StatusFile.cpp:141-144`). No
container/concurrency state. **Result:** faithful. No deviation.

### 3.3 Task 005 — thread pool (`FileCacheWorker`/`FileCacheThreadPool`) — **REMAP (SD6)**

| Dimension | CH (`Common/ThreadPool.h`, `ThreadFromGlobalPool`) | Velox (`ThreadPool.{h,cpp}`) | Verdict |
|---|---|---|---|
| State repr | `ThreadPoolImpl`: `boost::heap::priority_queue<JobWithPriority> jobs` (`:224`), `scheduled_jobs`, `first_exception` | `FileCacheThreadPool`: `inFlight_`, `std::deque<PendingTask> backlog_` (`:204`), `std::vector<SemiFuture> pending_` (`:205`) | **REMAP** — job-priority-queue → backlog-deque + futures |
| Mutex/CV | 1 `std::mutex` (`:193`) + 2 CVs (`job_finished` `:194`, `new_job_or_shutdown`) | 1 `std::mutex mutex_` + folly futures (no CVs) | **REMAP** |
| Containers & stability | worker threads managed internally; priority-ordered jobs | `folly::CPUThreadPoolExecutor executor_` (`:132`) + `folly::MeteredExecutor` (`:212`) | **REMAP** — FIFO vs priority (no priorities used by center-SCC → latent) |
| Ownership/member/destruction | `~ThreadPool` joins workers | documented member order: `meteredExecutor_` declared last → destroyed first → blocks until completion lambdas drain (`:193-212`) | **REMAP** (correct) |
| Async lifetime | `ThreadFromGlobalPool` join rethrows; destroy-joinable = terminate | `FileCacheWorker` `shared_ptr<State>` captured by executor lambda; join rethrows; destroy/move-assign-joinable = `VELOX_CHECK` abort (`ThreadPool.cpp:54-91`) | matches (guarantee preserved) |

**Result:** structural remap; **hard-constraint forced** (no CH
`GlobalThreadPool`/`ThreadPoolImpl` in Velox — the port maps onto the shared
`folly::CPUThreadPoolExecutor`). Consumer-relied guarantees verified preserved.
**Currently UNREGISTERED** → register as **SD6** (§4). Non-blocking; the only
latent guarantee-delta (job priority ordering) is unused by the center-SCC.

### 3.4 Task 006 — scheduler (`FileCacheScheduledTask`/`Scheduler`) — **REMAP (SD7 + SD8)**

| Dimension | CH (`BackgroundSchedulePool.h`) | Velox (`FileCacheScheduler.{h,cpp}`) | Verdict |
|---|---|---|---|
| State repr | 4 booleans `deactivated/scheduled/delayed/executing` (`:210-213`) with documented invariants | single `enum class State{Idle,Delayed,Queued,Running,Deactivated}` (`:120`) + `callbackInFlight_` (`:187`) + `pendingImmediate_/pendingDelayed_` | **REMAP (SD7)** — re-encoded state machine |
| Mutex/CV | **two** `std::mutex`: `exec_mutex` (`:204`, serializes execution) + `schedule_mutex` (`:205`, guards booleans) | **one** `std::recursive_mutex mutex_` (`:172`) + `condition_variable_any cv_` (`:173`) | **REMAP (SD8)** — 2 plain mutexes → 1 recursive mutex; **guarantee-changing (re-entrancy allowed)** |
| Containers & stability | `DelayedTasks = std::multimap<Poco::Timestamp,TaskInfoPtr>` (`:92`) + dedicated `delayed_thread` (`:149`) | `folly::Timekeeper` + per-task `folly::Future timerFuture_` (`:197`) | **REMAP (SD7)** |
| Ownership/member/destruction | `TaskHolder` dtor deactivates | `Holder` dtor/move-assign deactivates (`FileCacheScheduler.cpp:45-55`); tasks held as `weak_ptr` in scheduler (`:233`) | matches |
| Async lifetime | `execute` holds `exec_mutex` for whole callback → no concurrent self-run | worker closure + timer continuation capture `weak_ptr` (never raw `this`) + `generation_` guard; `deactivate` `cv_.wait` on `callbackInFlight_`; no-concurrent-self via Queued-coalescing (`FileCacheScheduler.cpp:263-296`) | matches (guarantee preserved) |

**Result:** the state-machine + delayed-queue remap (**SD7**) is hard-constraint
forced (no CH `BackgroundSchedulePool`/Poco `NotificationQueue` in Velox) and
its transitions are verified equivalent. The **`std::recursive_mutex` (SD8)** is
a **guarantee-changing** substitution justified only by a code comment
(`FileCacheScheduler.h:157-167`: folly `Future::thenValue` may run the
continuation inline on the lock-holding thread); it is **not** independently
proven to be forced — attaching the continuation outside the lock or via
`.via(executor)` would avoid recursion — and it is **UNREGISTERED**. Register
both; SD8 additionally needs the hard-constraint claim verified or a redesign
(§4, §6).

### 3.5 Task 007 — IO adapters (`FileCacheBufferState` + read/write bases) — **mapping SD9**

| Dimension | CH (`BufferBase`/`ReadBuffer`/`WriteBuffer`/`*FromFileBase`) | Velox (`ReadBufferFromVeloxReadFile.h`, `WriteBufferFromVeloxWriteFile.h`) | Verdict |
|---|---|---|---|
| State repr | `internal_buffer`, `working_buffer`, `pos`, `bytes` (settled count) | `FileCacheBufferState`: `internal_`, `working_`, `position_`, `bytes_` | **1:1 state repr** |
| Position bookkeeping | `file_offset_of_buffer_end`, `read_until_position`, `getPosition = end-available` | `fileOffsetOfBufferEnd_`, `readUntil_`, `getPosition()` (`ReadBufferFromVeloxReadFile.h:229`) | 1:1 |
| Containers & stability | owned bytes via `Memory<>` (aligned raw alloc) | pool-charged `BufferPtr ownedBuffer_` | **mapping SD9** — memory now MemoryPool-charged (accounting delta) |
| Ownership/member/destruction | writer owns fd; dtor must not finalize | writer owns `unique_ptr<WriteFile>`; reader owns `shared_ptr<ReadFile>` or non-owning `ReadFile*`; writer dtor never finalizes (`WriteBufferFromVeloxWriteFile.h:42-44`) | matches |
| Async lifetime | reader extractable/relayed (Task 012) | reader relay in FileSegment (012); adapter self-contained | matches |
| Class shape | deep hierarchy (`BufferBase`→…→`*FromFileBase`) | **flattened** to 2 bases + shared `FileCacheBufferState` | design choice; guarantees preserved (note, not a §3 guarantee-change) |

**Result:** buffer-state representation faithfully reproduced. Owned-memory
mapping `Memory<>`→`BufferPtr` is forced by the velox MemoryPool model
(**SD9**, register; note the accounting-semantics delta). Class-hierarchy
flattening preserves guarantees → benign.

### 3.6 Task 008/010 — leaf value types & config

Stateless leaves (no lock/container/lifetime state): `FileCacheKey` (value +
hash), `SipHash128` (pure), `FileSegmentKeyType` (enum), `FileCacheOriginInfo`/
`OriginPoolKey` (value + user_id-only hash), `FileCacheUtils` (pure checked
arithmetic), `FileCacheConfig` (plain aggregate). No state representation, no
mutex, no container-stability, no async lifetime. **Result:** faithful; the
only structural notes are the two **dead `folly/container/F14Map.h` includes**
in `FileCacheKey.h:20` and `FileCacheOriginInfo.h:20` (no F14 container is
declared — value types + hash functors only), which are harmless. `FileSegments
= std::list` (`FileCache_fwd_internal.h:34`) preserved (SD5).

---

## 4. Structure-deviation ledger (guide §3 — corrected & completed)

Full 6-column format. **Hard-constraint forced** deviations need registration +
evidence; **not-forced** deviations additionally need human sign-off ("有牙").

| ID | CH structure | Velox replacement | Guarantee difference | Hard platform constraint | E / design evidence | Human sign-off | Phase-D status |
|---|---|---|---|---|---|---|---|
| SD1 | `ShardedMap` inner `std::unordered_map` (`ShardedMap.h:24`) | `folly::F14FastMap` (`ShardedMap.h:60`) | value/address relocation on rehash | **none** — guide states `std::unordered_map` compiles in Velox | n/a | **absent (REQUIRED)** | **UNRESOLVED (M1)** — functionally safe for `origins` copy-out, but sign-off mandatory |
| SD6 | `ThreadPoolImpl` job-priority-queue + worker list + 2 CVs (`Common/ThreadPool.h:224,193-194`); `ThreadFromGlobalPool` | `folly::CPUThreadPoolExecutor`+`MeteredExecutor`+`backlog_`/`pending_`/`inFlight_` (`ThreadPool.h:132,204-212`) | FIFO vs priority ordering (latent — center-SCC uses one priority); enqueue-not-block on pool-exhaustion | **real** — no CH `GlobalThreadPool`/`ThreadPoolImpl` primitive in Velox | Task 005 design + `ThreadPoolTest`; guarantees verified in source | infra-matrix class (forced) | **UNREGISTERED** → register (Minor). Non-blocking |
| SD7 | `BackgroundSchedulePoolTaskInfo` 4-boolean state + `std::multimap<Poco::Timestamp>` delayed queue + delay-thread (`BackgroundSchedulePool.h:210-213,92,149`) | `enum class State` + `folly::Timekeeper` + per-task `Future` (`FileCacheScheduler.h:120,197,231`) | none observed — transitions verified equivalent | **real** — no CH `BackgroundSchedulePool`/Poco `NotificationQueue` in Velox | Task 006 design + `SchedulerAndScopeTest`; source-verified | infra-matrix class (forced) | **UNREGISTERED** → register (Minor). Non-blocking |
| SD8 | scheduler `exec_mutex` + `schedule_mutex` (two `std::mutex`, `BackgroundSchedulePool.h:204-205`) | one `std::recursive_mutex` (`FileCacheScheduler.h:172`) | **re-entrant locking now permitted** (std::mutex forbids it) | **claimed only** — folly inline-continuation-under-lock (code comment `:157-167`); an executor-driven / lock-released continuation avoids it | design comment only; behavior tested | **absent** | **UNRESOLVED (Major)** → register + verify hard-constraint or redesign |
| SD9 | IO owned buffer `Memory<>`/`std::vector<char>` (`Metadata.cpp:939`) | pool-charged `BufferPtr` (`ReadBufferFromVeloxReadFile.h`) | memory now MemoryPool-charged (accounting/lifetime delta); buffer-state layout preserved | **real** — velox MemoryPool is the platform allocation model | Task 007 design + `IoAdaptersTest`; E1 probe | infra-matrix class (forced) | **UNREGISTERED** → register (Minor). Non-blocking |
| — Guards | 4-level order + distinct `Lock` types + `timed_mutex` | faithful | none | — | E4 (citation) | approved 1:1 | resolved (matches) |
| — SharedMutex | CH custom | `folly::SharedMutex` | none | — | infra matrix | approved | resolved |
| — Queue | `std::deque`+2CV+finished | faithful | none | — | E5 | approved | resolved (matches) |
| — callOnce | `std::once_flag`+`std::call_once` (`Common/callOnce.h`) | `std::call_once` (012) | none (CH `callOnce` *is* `std::call_once`) | — | `012-...:1060` | approved 1:1 | resolved (matches) |
| SD2 | `EvictionInfo/EvictionCandidates` `absl::flat_hash_*` (CT7/CT8) | folly F14 (011 "Required replacements") | absl & F14 both relocate; indirection (`unique_ptr`/`shared_ptr`) survives — confirm `original_queue_types` raw-ptr keys | Task 011 directs `absl→folly F14` | n/a (011 not implemented) | confirm-only | **future (011)** |
| SD3 | `KeyMetadata : std::map<size_t,…>` (CT3) | 012 keeps `std::map` | — | — | — | keep CH | future (on-track) |
| SD4 | `MetadataBucket : std::unordered_map` (CT4) | 012 plans F14 (values are `shared_ptr`) | pointee stable; confirm no cached raw ptr/iterator across mutation | — | — | confirm before 012 accept | future |
| SD5 | `LRUQueue = std::list<EntryPtr>` (CT1) | kept `std::list` (`FileCache_fwd_internal.h:34`) | none | — | — | — | resolved (matches) |

**Realized, unresolved deviations in accepted 003-010 code:** **SD1 (M1)** and
**SD8 (recursive_mutex)**. **SD6/SD7/SD9** are forced but **unregistered** (the
prior review missed them by auditing only behavior); registering them is a
condition of a clean zero-unresolved gate.

---

## 5. Coverage matrix (call site → behavior → implementation / test / probe / structure)

| Center-SCC consumer path (CH file:line) | Dep(s) | Implemented? | Structure (§3) | Test / probe |
|---|---|---|---|---|
| Background eviction `FileCache.cpp:1690-1905` (queue + pool) | D1,D21,**D5**,D11 | queue+pool yes; **D5 events NO** | queue 1:1; pool **SD6** | `BasicShimsTest`,`ThreadPoolTest`; **B1 gap** |
| `initialize` `FileCache.cpp:508-598` (OnceFlag, StatusFile, threads, scheduler; eviction-pool ctor w/ 3 metrics) | D8,D14,D20,D22,**D6** | StatusFile+scheduler yes; D8 native; **eviction metrics args dropped** | StatusFile 1:1; scheduler **SD7/SD8**; pool **SD6** | `StatusFileAndGuardsTest`,`SchedulerAndScopeTest`; **B2 gap (LRU/Metadata metrics)** |
| `backgroundCleanupTaskFunc` `FileCache.cpp:1907-1957` | D22,D11 | scheduler yes | **SD7/SD8** | `SchedulerAndScopeTest` immediate-preempts-delayed |
| `loadMetadata*` `FileCache.cpp:2196-2440` | D1,D20,D26,D28 | yes | queue 1:1; pool **SD6** | `LeafTypesTest`,`BasicShimsTest` |
| `FileSegment::write` `FileSegment.cpp:430-540` | D24,**D9**,D2 | adapter yes; SCOPE_EXIT=012 | buffer-state 1:1; **SD9** | `IoAdaptersTest` Writer* + E1 |
| reader relay `FileSegment.cpp:370-412` | D25 | adapter yes | buffer-state 1:1 | `IoAdaptersTest.ReaderHandoff…` (E3) |
| **`getCallerId` `FileSegment.cpp:254-259`** | D23,D12 | **threadname dropped** | — | `SchedulerAndScopeTest` **prefix-only (false-green)** → **F-CALLERID** |
| `getOrCreateSharedOrigin`/`removeSharedOrigins` `Metadata.cpp:108-135` | D34,D29 | yes (F14) | **SD1** | `ShardedMapTest` (copy-out; no rehash-hazard) |
| `FileCacheQueryLimit::*` `QueryLimit.cpp:15-168` | D23,D33,**D5/D6** | leaf yes; metrics NO | — | B1/B2 gap |
| eviction candidates timer `EvictionCandidates.cpp:267` | H2 (`FilesystemCacheEvictMicroseconds`) | timer yes; **event NO** | — | B1 gap |

Build wiring: `velox/CMakeLists.txt:19 add_subdirectory(ch)`; every test a real
`add_executable`+`add_test`. Not orphaned.

---

## 6. Findings (Blocker / Major / Minor)

### B1 — Task-003 `ProfileEvents` enumerator surface incomplete (Blocker)
- **Invariant:** every `ProfileEvents::Event` referenced by the center-SCC must
  be a defined enumerator so 011/012 compile.
- **Corrected evidence:** `src/Interpreters/FileCache/` references **44**
  distinct `extern const Event`; `velox/ch/Common/ProfileEvents.h:11-26` defines
  **16**. **13** referenced names are present, **31** are **missing**, and **3**
  defined names are outside the source set (`CachedReadBufferReadFromCacheBytes`,
  `…ReadFromSourceBytes`, `…CacheWriteBytes` — they belong to Task 014's
  `CachedOnDiskReadBufferFromFile`, harmless here). Missing includes the eviction
  family (`FilesystemCacheEvictedBytes/EvictedFileSegments/EvictionTries/
  EvictMicroseconds`, `EvictionCandidates.cpp`, `LRUFileCachePriority.cpp`),
  `FilesystemCacheLoadMetadataMicroseconds`, `FilesystemCacheLockKey/Metadata/
  OriginPoolMicroseconds`, the `FilesystemCacheBackground*` family, and
  `FilesystemCacheHoldFileSegments` (which is **both** an `Event` and a `Metric`
  in `FileSegment.cpp:34,41` — the port defines only the metric).
- **Ownership (corrected):** the header was **created by Task 002**
  (`002-common-noop-shims.md:56,108` — a no-op shim) and extended once by Task
  004 (`f948fb6a4`, the 3 lock events). The A ledger assigns the *compatibility
  surface* to Task 003. The *real* counter implementation is the future Task
  017's job (`017-...:443-445`), but the **enumerator NAMES** must exist before
  011/012 compile. This reopen completes only the **name surface** on the shared
  shim header, not the counters.
- **Concrete 011/012 impact:** Task 011 declares `CurrentMetrics/ProfileEvents/
  logging -> existing compatible shims` (`011-...:90`) and **cannot** create or
  modify `ProfileEvents.h` (not in its file scope: `011-...:62-77`); Task 012
  owns CMake/tests but also does not list the enum header for edit. Porting
  `EvictionCandidates.cpp`/`LRUFileCachePriority.cpp`/`Metadata.cpp` then fails
  to compile (missing enumerators) and trips the unreviewed-dependency gate.
- **CH ref:** `extern const Event` across `src/Interpreters/FileCache/*.cpp`;
  timer `EvictionCandidates.cpp:267`. **Velox ref:** `ProfileEvents.h:11-26`.
- **Smallest RED:** a TU that references every center-SCC event (or a
  `static_assert` on a magic-count) — RED = does-not-compile against the current
  16-enumerator enum. Concrete seed: increment `FilesystemCacheEvictedFileSegments`.
- **False-green probe:** delete one already-defined event (e.g.
  `FileSegmentWriteMicroseconds`) and confirm the coverage TU goes RED — proving
  it checks the enum rather than trivially passing.
- **Blocks 011/012:** yes. **Severity: Blocker.**

### B2 — Task-003 `CurrentMetrics` enumerator surface incomplete (Blocker)
- **Corrected evidence:** `src/Interpreters/FileCache/` references **15**
  `extern const Metric`; `velox/ch/Common/CurrentMetrics.h:11-18` defines **6**
  (**0** extra). **9** are missing.
- **Corrected scope (which of the 9 actually block compile):**
  - **5 hard blockers (kept `add/sub` call sites):** `FilesystemCacheElements`,
    `FilesystemCacheInvalidatedElements`, `FilesystemCachePriorityQueueElements`,
    `FilesystemCacheSize` (`LRUFileCachePriority.cpp:46,52,64,71,112-113,167,
    199-200,784`, Task 011) and `FilesystemCacheKeys` (`Metadata.cpp:348,647`,
    Task 012). Task 011 keeps these as "existing compatible shims" so they are
    referenced and must be defined.
  - **3 NOT blockers (dropped with the ctor):** the eviction trio
    `FilesystemCacheEvictionThreads/…Active/…Scheduled` is referenced **only** as
    `ThreadPool` ctor arguments at `FileCache.cpp:583-586`. The accepted port
    `FileCacheThreadPool` ctor takes **no** metric parameters
    (`ThreadPool.h:158-161`, D21), so Task 012 must **drop** these three args when
    translating — they disappear from ported code and are therefore not compile
    blockers. *(This corrects the prior report, which named the eviction trio as
    the thing that "cannot compile the eviction-pool construction.")*
  - **1 excluded:** `FilesystemCacheOvercommitUsers` (`CacheUsage.h:116`) is on
    the excluded overcommit surface (O2) — not on the in-scope compile path.
- **CH ref:** `extern const Metric` blocks; hard-blocker sites above.
  **Velox ref:** `CurrentMetrics.h:11-18`.
- **Smallest RED:** a TU that does `CurrentMetrics::add(CurrentMetrics::
  FilesystemCacheSize, …)` — RED = does-not-compile. **False-green:** remove a
  present metric (e.g. `CacheFileSegments`) to confirm the check bites.
- **Note:** fixing B2 does **not** require reopening Task 005 (its ctor omitting
  the metrics is deliberate and coherent).
- **Blocks 011/012:** yes (via the 5 kept metrics). **Severity: Blocker.**

### F-CALLERID — Task-006 `getCallerId` no-scope format drift (REOPEN, Major)
- **Invariant (guide's exact rule):** the two-branch caller-id format is a
  **verbatim diagnostic/identity contract** — the guide's §A generation
  instruction explicitly lists "诊断/日志文本是否逐字要求" (diagnostic/log text
  required verbatim, the same class as the StatusFile 3-line reopen), and the A
  ledger calls the `None:<threadname>:<tid>` vs `<query_id>:<tid>` format a
  "verbatim diagnostic/identity contract" (`ledger §Notes(006)`).
- **Evidence:** CH `FileSegment.cpp:254-259` produces
  `fmt::format("None:{}:{}", getThreadName(), toString(getThreadId()))` =
  **`None:<threadname>:<tid>`** (three colon-separated fields). The port
  `FileCacheQueryIdScope.cpp:49-56` produces **`"None:" + tid`** =
  **`None:<tid>`** — the `<threadname>` middle component is entirely dropped
  (root cause: the missing `getThreadName` shim, D12).
- **Why the accepted test did not catch it (false-green):**
  `SchedulerAndScopeTest.cpp:649-653` (`NoScopeProducesNonePrefix`) asserts only
  `callerId.substr(0,5) == "None:"` — the prefix — which passes for **both**
  `None:<threadname>:<tid>` and `None:<tid>`. This is exactly the self-proving
  false-green the guide (§C) targets.
- **Decision — Task 006 must REOPEN.** The prior report classified this "ACCEPT
  (minor diagnostic drift)" on the reasoning that `tid` alone keeps the
  equality-based downloader hand-off correct (`getCallerId() ==
  downloader_id` at `FileSegment.cpp:365-367`, `Metadata.cpp:898`). That
  equality *does* still hold (both sides use the same helper; `tid` is
  per-thread-unique). **But the guide's rule is that verbatim diagnostic/identity
  text is a contract regardless of whether an "it happens to still work"
  argument exists** — that reasoning is precisely what caused the historical
  reopen cascade (StatusFile 3-line text, 006 immediate-vs-delayed). Under the
  exact rule, dropping `<threadname>` is a contract violation → **REOPEN**.
- **Concrete 011/012 impact:** the caller-id string is the downloader identity
  written to logs and stored in `download_data->downloader_id`; Task 012 ports
  the `getOrSetDownloader`/`isDownloaderUnlocked` equality and the log lines. A
  format the review has *blessed as wrong* becomes a frozen contract other tasks
  build on; any future consumer that logs, parses, or asserts the caller-id sees
  the degraded 2-field form.
- **Smallest RED:** assert the full no-scope structure — set the OS thread name
  to a known value (e.g. `TestDl`) and assert `getCallerId()` equals
  `"None:TestDl:" + <tid>` (three fields; middle == the set thread name).
  Against the current port (`None:<tid>`) this is RED (two fields; threadname
  absent).
- **False-green probe:** with the RED test in place, blank the threadname in the
  fixed implementation (force `getThreadName()`→`""`, or wrap the threadname
  insertion in `if(false)`) and confirm the test still fails — proving it
  inspects the threadname component, not merely "there are 3 fields."
- **Resolution:** native-map `getThreadName` (D12) → `folly::getCurrentThreadName()`
  (`folly/system/ThreadName.h`) and restore `None:<threadname>:<tid>`; replace
  the prefix-only test. **Severity: Major (REOPEN).**

### M1 — SD1 `ShardedMap` F14 deviation unresolved (Major)
- **Invariant:** guide §3 — a guarantee-changing container swap **not** forced by
  a hard Velox constraint requires a registered deviation + human sign-off.
- **Evidence:** `ShardedMap.h:60` uses `folly::F14FastMap` where CH uses
  `std::unordered_map` (`ShardedMap.h:24`). F14 relocates values on rehash;
  `std::unordered_map` does not. No hard-constraint justification (guide:
  `std::unordered_map` compiles in Velox); no sign-off on file.
- **Impact on 011/012:** no functional break today — the only in-scope consumer
  `getOrCreateSharedOrigin`/`removeSharedOrigins` (`Metadata.cpp:108-135`) copies
  out an `OriginInfoPtr` and never caches an address/iterator/reference across a
  rehash (header documents copy-out, `ShardedMap.h:34-42`). Latent hazard: a
  future consumer holding a reference into the shard map would be silently broken
  where CH is safe.
- **CH ref:** `ShardedMap.h:24`. **Velox ref:** `ShardedMap.h:60`.
- **Smallest RED:** a consumer that stashes a reference into the shard map,
  forces a rehash, then dereferences — RED against F14, green against
  `std::unordered_map`. **False-green probe:** extend `ShardedMapTest`'s copy-out
  test with an `if(false)` removal of the copy to prove it exercises the
  relocation path.
- **Resolution:** register SD1 with hard-constraint evidence + human sign-off, or
  revert to `std::unordered_map`. **Severity: Major.** Blocks the clean gate.

### M2 — leaf-shim surface: reclassified (was "Task-003 reopen")
The prior report bundled `Memory<>`/`callOnce`/`SCOPE_EXIT`/`Stopwatch`/
`getThreadName` into a Task-003 reopen "because A assigned them to 003." The
Controller's rule — *do not expand Task 003 merely because A assigned an owner* —
requires reclassifying each by **where it is actually consumed** and **whether a
recorded mapping exists**. None of these lives in a shared header that 011/012
include-but-cannot-extend (unlike B1/B2), so none is a Task-003 reopen:

| Leaf | CH ref | Consumed in | Correct classification |
|---|---|---|---|
| D8 `callOnce`/`OnceFlag` | `Common/callOnce.h` | `FileCache.cpp:503` (012-created) | **Exact approved native mapping.** CH `callOnce` *is* `std::call_once`/`std::once_flag`; Task 012 explicitly selects `std::call_once` (`012-...:1060`). No reopen, no amendment. *(The cached-`init_exception`/rethrow behavior is **not** in this leaf — it is `FileCache::initialize`'s own logic, `FileCache.cpp:461-462,521,527` + `throwInitExceptionIfNeeded` — a Task-012 body obligation verified when 012 is reviewed, not a leaf contract. This corrects the prior report, which mis-attributed exception-caching to `callOnce`.)* |
| D9 `SCOPE_EXIT` | `FileSegment.cpp:494`; `Metadata.cpp:1054` | 012-created files | **Task-012 contract amendment** — record the approved native mapping (`folly` scope-guard) before the 012 worker starts. Low risk. |
| D11 `Stopwatch` | `FileCache.cpp:1923` | 012-created file | **Task-012 contract amendment** — record mapping (`folly::stop_watch`/`std::chrono::steady_clock`). Low risk. |
| D3 `Memory<>`+`DBMS_DEFAULT_BUFFER_SIZE` | `Metadata.cpp:939,963` | 012-created file | **Task-012 contract amendment** — record mapping to the pool-charged buffer (reuse `FileCacheBufferState`/`BufferPtr`, SD9); pin `DBMS_DEFAULT_BUFFER_SIZE` value. Moderate risk (memory accounting). |
| D12 `getThreadName` | `FileSegment.cpp:257` | Task-006 `getCallerId` (accepted) | **Drives Task-006 REOPEN (F-CALLERID)** — native-map `folly::getCurrentThreadName()` and restore the caller-id format. Not a 003 reopen. |

**Net:** Task 003 reopens **only** for B1/B2. The M2 helpers are one exact native
mapping (D8), three Task-012 amendments to record before 012 starts (D3/D9/D11),
and one Task-006 reopen driver (D12). **Severity: Major (process — the D3/D9/D11
mappings must be recorded before the 012 worker starts, per the guide §1
anti-pattern of letting a worker "pick the closest Velox API").**

### SD6 — Task-005 thread-pool folly remap unregistered (Minor)
- **Evidence:** CH `ThreadPoolImpl` (job-priority-queue `Common/ThreadPool.h:224`
  + 2 CVs `:193-194`) → `folly::CPUThreadPoolExecutor`+`MeteredExecutor`+backlog
  deque + futures (`ThreadPool.h:132,204-212`). Forced (no CH `GlobalThreadPool`
  in Velox); guarantees (throw-on-full, `wait` barrier over in-flight+backlog,
  join-rethrow, destroy-joinable-abort, completion-lambda-draining destruction
  order) verified in `ThreadPool.cpp`.
- **011/012 impact:** none functional; latent job-priority-ordering delta is
  unused by the center-SCC (all eviction removers same priority).
- **Smallest RED / false-green:** a `wait()` test that schedules `maxThreads+1`
  tasks and asserts all bodies ran before `wait` returns (RED if a backlogged
  task's future is not tracked); false-green = drop the `pending_.push_back` of a
  backlogged future and confirm the test fails.
- **Resolution:** register as SD6 (§4) with the hard-constraint citation.
  **Severity: Minor (registration gap).**

### SD7 — Task-006 scheduler state-machine remap unregistered (Minor)
- **Evidence:** CH 4-boolean state + `std::multimap` delayed queue + delay-thread
  (`BackgroundSchedulePool.h:210-213,92,149`) → `enum class State` + folly
  `Timekeeper` + per-task `Future` (`FileCacheScheduler.h:120,197`). Forced (no CH
  `BackgroundSchedulePool`/Poco `NotificationQueue` in Velox); transitions
  verified equivalent (§3.4).
- **Resolution:** register as SD7. **Severity: Minor (registration gap).**

### SD8 — Task-006 scheduler `std::recursive_mutex` substitution (Major)
- **Invariant:** §3 — a guarantee-changing mutex substitution not forced by a
  platform primitive needs registration **and** human sign-off.
- **Evidence:** CH uses two `std::mutex` (`exec_mutex`+`schedule_mutex`,
  `BackgroundSchedulePool.h:204-205`); the port uses one `std::recursive_mutex`
  (`FileCacheScheduler.h:172`), which **permits re-entrant locking** that
  `std::mutex` forbids. The justification is a **code comment**
  (`FileCacheScheduler.h:157-167`: folly `Future::thenValue` may run the
  continuation inline on the lock-holding thread), **not** an independent
  hard-constraint proof — attaching the continuation outside `mutex_` or via
  `.via(workerPool)` would avoid recursion. Unregistered; no sign-off.
- **011/012 impact:** none functional (the mutex is internal to the scheduler
  leaf; no consumer locks it; behavior is correct and tested). It blocks the
  clean zero-unresolved gate.
- **Smallest RED / false-green:** a test that fulfils the timer promise on the
  same thread that holds `mutex_` while attaching the continuation (manual
  `Timekeeper` advanced inline) and asserts no self-deadlock — RED against a
  plain `std::mutex`; false-green = swap `recursive_mutex`→`std::mutex` and
  confirm the deadlock/abort surfaces (proving the test exercises the reentrant
  path).
- **Resolution:** either register SD8 with a verified hard-constraint (prove the
  inline-under-lock continuation is unavoidable) + human sign-off, or redesign to
  attach the continuation off-lock and revert to `std::mutex`. **Severity: Major.**

### SD9 — Task-007 owned-buffer `Memory<>`→`BufferPtr` mapping unregistered (Minor)
- **Evidence:** CH `Memory<>`/`std::vector<char>` owned buffer (`Metadata.cpp:939`)
  → pool-charged `BufferPtr` (`ReadBufferFromVeloxReadFile.h`, `FileCacheBufferState`).
  Buffer-state layout (`internal_`/`working_`/`position_`/`bytes_`) is 1:1; the
  delta is that owned memory is now velox-MemoryPool-charged (accounting +
  lifetime). Forced (velox MemoryPool is the platform model).
- **Resolution:** register as SD9; note the accounting delta so Task 012's D3
  download-scratch mapping stays consistent. **Severity: Minor (registration gap).**

---

## 7. Missing evidence and E-probe status

| Item | State |
|---|---|
| E1 (partial-write physical-prefix truth) | Closed by real-`LocalWriteFile` probe (`files/task-e-writer-probe.md`) + `IoAdaptersTest.cpp:779,852`. Production `FileSegment` reconcile + lost errno∈{28,122} remain a **mandatory Task-012** RED. |
| E2 (append-without-truncate on resume) | Closed by probe (open-without-truncate + `lseek(SEEK_END)`, single-writer assumption). File-open mode is a Task-012 obligation. |
| E3 (reader-handoff invariants) | Closed by `IoAdaptersTest.cpp:360-379` (strong oracle). |
| E4 (`CacheStateGuard` timed acquire) | Closed by citation — real `std::timed_mutex` (`Guards.h:87`); negative-timeout coverage thin but acceptable. |
| E5 (queue timed/non-blocking/finish) | Closed by `BasicShimsTest` timed/non-blocking/finish tests. |
| **B1/B2 enum-coverage tests** | **Missing** — no test asserts the center-SCC event/metric surface is complete (why the holes survived to Phase D). |
| **SD1 relocation-hazard test** | **Missing** — `ShardedMapTest` proves copy-out, not reference-across-rehash. |
| **SD8 reentrancy test** | **Missing/implicit** — reentrant path exercised only incidentally by the manual-timekeeper tests; no explicit oracle + false-green. |
| **F-CALLERID structure test** | **False-green** — `NoScopeProducesNonePrefix` checks only the `None:` prefix (`SchedulerAndScopeTest.cpp:649-653`). |
| Fault injection (D10) | **Unobservable** (`FAIL_POINT_TRIGGER` no-op). |

---

## 8. Conclusion

**Reopen list (revised):**
1. **Task 003** — complete **only** the `ProfileEvents` (B1: 44 referenced / 16
   defined / 31 missing) and `CurrentMetrics` (B2: 15 / 6 / 9 missing, 5 hard
   compile-blockers) **enumerator-name** surface on the shared shim headers
   (`velox/ch/Common/ProfileEvents.h`, `CurrentMetrics.h`; created by Task 002,
   extended by Task 004). Add an enum-coverage RED test + false-green probe. The
   real counter implementation stays Task 017's. **M2 leaf helpers are NOT part
   of this reopen.**
2. **Task 006** — **REOPEN** for `getCallerId` (F-CALLERID): restore the exact
   `None:<threadname>:<tid>` format via a native-mapped `getThreadName`
   (`folly::getCurrentThreadName`), and replace the prefix-only test with a
   full-structure RED + false-green. Also register SD7 and resolve SD8
   (recursive_mutex: verify the hard-constraint or redesign off-lock).
3. **Task 009** — resolve SD1/M1: register the `folly::F14FastMap` deviation with
   hard-constraint evidence + human sign-off, or revert to `std::unordered_map`;
   add the reference-across-rehash oracle.

**Structural-registration obligations (condition of a clean gate, not full
reopens):** SD6 (Task 005 thread-pool folly remap), SD7 (Task 006 scheduler
remap), SD9 (Task 007 owned-buffer mapping) — forced, guarantees verified, but
**unregistered**; add them to the structure-deviation ledger. SD8 (Task 006
recursive_mutex) is **unresolved** (Major) until registered-with-proof or
redesigned.

**Task-012 pre-start contract amendments (M2 reclassification):** record the
approved native mappings for D3 (`Memory<>`→pool `BufferPtr`), D9
(`SCOPE_EXIT`→folly guard), D11 (`Stopwatch`→folly `stop_watch`) **before** the
012 worker starts. D8 (`callOnce`→`std::call_once`) is already exact and
recorded (`012-...:1060`).

**Accept (with recorded notes):** Tasks 004, 005 (SD6), 007 (SD9), 008, 010.
Carry-forward non-blocking drifts: D20/D21 ctor signatures (pool ref; metrics
dropped — subsumed by B2); D10 fault injection disabled; D2 `ErrnoException`
`getErrno`/`addMessage` lost (deferred to Task 012 per E1). SD2/SD4 confirm-only
when Task 011/012 are reviewed.

**Zero-unresolved gate state: NOT achieved.** Open: **B1, B2** (Blockers);
**F-CALLERID** (Major, Task-006 reopen); **M1/SD1** and **SD8** (Major,
guarantee-changing, no sign-off); **SD6/SD7/SD9** (Minor, unregistered forced
remaps); **M2** Task-012 amendments (D3/D9/D11) to record; missing
enum-coverage, SD1-hazard, SD8-reentrancy evidence, and the false-green
caller-id test.

**`task_011_allowed = false`** — 011/012 cannot compile against the current
`ProfileEvents`/`CurrentMetrics` enumerator surface (B1, B2) and cannot extend
those headers within their own file scope; Task 006 must reopen for the
`getCallerId` verbatim-contract violation (F-CALLERID); and SD1 (M1) plus SD8
remain unresolved guarantee-changing structural deviations. The report ends
**`reopen proposed`**; no implementation was changed and Task 011 must not start.

**Self-check:**
- Every Task 003-010 has a verdict (§2) **and** a five-dimension structural
  parity row (§3.1-3.6): 003 queue (§3.1), 004 Guards+StatusFile (§3.2), 005
  thread pool (§3.3), 006 scheduler (§3.4), 007 IO (§3.5), 008/010 leaves (§3.6),
  009 ShardedMap (§2 + SD1). Concurrency leaves get full 5-row tables; stateless
  value leaves are explicitly declared state/lock/container/lifetime-free.
- Every non-1:1 internal translation is classified in §4: **SD1** (not forced →
  sign-off, M1), **SD6/SD7/SD9** (forced → register), **SD8** (guarantee-changing,
  weakly-forced → register+prove or redesign), plus the approved 1:1 rows
  (Guards/SharedMutex/queue/callOnce) and the future SD2/SD3/SD4/SD5.
- Every finding carries a CH `file:line`, a Velox `file:line`, concrete 011/012
  impact, a smallest RED, a false-green probe, and a severity.
- Enum counts independently rescanned (44/16/13/31/3; 15/6/6/9/0) and ownership
  corrected (headers = Task 002 shim, extended by Task 004, real impl Task 017).
- No deviation was assumed approved by an earlier design: SD6/SD7/SD9 cite the
  hard platform constraint; SD8's "forced" claim is explicitly downgraded to a
  code-comment (not an approval artifact); SD1 has no sign-off; the callOnce and
  ThreadPool acceptances cite the exact task/design lines.
- `zero-unresolved` and `task_011_allowed=false` are stated explicitly.

## 9. Controller validation

```text
controller_status: reopen_proposed
environment_profile: root-oss
task_011_allowed: false
```

The Controller independently validated the A and D artifacts rather than
accepting the delegated verdicts:

- the frozen CH source remains unchanged from
  `da28e83e8b3cb69090624b0a0b1f13cd78c13279`;
- the Velox implementation remains exactly
  `89039901aa4287ce811a3b1628867b0796c76678`;
- all 270 parsed A-ledger CH `file:line` references resolve to existing files and
  in-range lines;
- an independent source scan found 44 referenced `ProfileEvents` names, 16
  defined names, and 31 referenced names missing; it found 15 referenced
  `CurrentMetrics` names, 6 defined names, and 9 missing;
- a control TU using existing enum names compiled, while focused RED TUs failed
  for the intended absent names:
  `FilesystemCacheEvictMicroseconds` and
  `FilesystemCacheEvictionThreads`;
- CH `FileSegment::getCallerId` includes the thread name in its no-query branch,
  while the accepted Task-006 helper omits it and the current test checks only
  the `None:` prefix;
- CH `ShardedMap` uses `std::unordered_map`; accepted Task 009 uses
  `folly::F14FastMap`, and no hard constraint or human sign-off justifies the
  changed address-stability guarantee;
- the scheduler replaces two plain mutexes with one
  `std::recursive_mutex`; the implementation comment explains the inline-future
  path, but no evidence proves that an off-lock continuation cannot preserve the
  CH non-recursive structure.

Fresh Controller evidence:

```text
/root/oss/velox/_build/debug/full_review_enum_surface_control.log
  compile exit: 0

/root/oss/velox/_build/debug/full_review_profile_events_red.log
  expected compile failure:
  FilesystemCacheEvictMicroseconds is not a member of ProfileEvents

/root/oss/velox/_build/debug/full_review_current_metrics_red.log
  expected compile failure:
  FilesystemCacheEvictionThreads is not a member of CurrentMetrics

/root/oss/velox/_build/debug/full_review_e_probe_run_controller.log
  13/13 checks passed

/root/oss/velox/_build/debug/test_full_review_e_io_controller.log
  3/3 tests passed
  0 failed
  0 skipped/disabled
```

### Authoritative verdict

| Task | Verdict | Required next action |
|---|---|---|
| 003 | `reopen` | Complete the `ProfileEvents` and `CurrentMetrics` enumerator-name surfaces and add complete compile-coverage RED plus false-green evidence. |
| 004 | `accept` | None from this review. |
| 005 | `accept` | SD6 is now recorded as a forced platform remap; no implementation change required by this review. |
| 006 | `reopen` | Restore exact `None:<threadname>:<tid>` caller identity and replace the prefix-only test; resolve SD8 by proving/signing the recursive-mutex deviation or redesigning the continuation off-lock. SD7 is recorded as the approved scheduler-platform remap. |
| 007 | `accept` | SD9 is now recorded as the required MemoryPool mapping; production `FileSegment` reconciliation remains Task 012. |
| 008 | `accept` | None from this review. |
| 009 | `reopen` | Revert to `std::unordered_map`, or provide a hard constraint and explicit human sign-off for F14; add the reference-across-rehash oracle. |
| 010 | `accept` | None from this review. |

SD6, SD7, and SD9 are registered by the structure-deviation ledger in this
report as forced platform mappings with preserved consumer guarantees. They are
not independent reopen reasons. SD1 and SD8 remain unresolved.

Before a Task-012 worker starts, its contract must also record the approved
native mappings for `Memory<>`, `SCOPE_EXIT`, and `Stopwatch`; this is a
task-authoring amendment, not a Task-003 implementation change.

No implementation was modified. The zero-unresolved gate is not achieved, so
Task 011 and Task 012 remain prohibited until the reopened work is corrected,
reviewed, and accepted.
