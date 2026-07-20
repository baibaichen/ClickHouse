# Tasks 003-014 Full-Review Decisions — `home-chang` Review 2

## Status

Round 2 of the whole-port source-contract review, run at the mandatory
post-Task-014 checkpoint. Scope: Tasks 003-014, with emphasis on the code that did
not exist in round 1 (the center SCC 011/012, Factory/Manager/OpenedFileCache 013,
the DWIO reader/handoff 014). Per the user's directive this round reviews BOTH
consumer-observable semantics AND internal implementation structure (guide §3).

Method: guide A (consumer contract ledger) then guide D (four parallel read-only
subsystem sweeps), each re-deriving contracts from real CH callers (file:line) and
diffing the actual Velox implementation. Controller then independently verified the
load-bearing findings and deduplicated against round 1.

Artifacts:
- Ledger: `port/task/fullview/home-chang/2/011-014-consumer-contract-ledger.md`
- Sweeps: `port/task/fullview/home-chang/2/evidence/{011-priority-eviction,012-center-scc,013-factory-manager,014-reader-handoff}-review.md`
- Round-1 baseline (authoritative, carried forward): `port/task/fullreview/root-oss/1/003-010-review-decisions.md`

## 0. Verdict summary

| Shard | Verdict | Confirmed | Plausible |
|---|---|---|---|
| Task 011 priority/eviction | ACCEPT | 1 test-coverage hole (F-011-T) | 1 (F-011-O reachability — resolved accept) |
| Task 012 center SCC | ACCEPT (zero unresolved) | 0 | 4 advisory (benign/already-signed) |
| Task 013 Factory/Manager/OpenedFileCache | ACCEPT | 0 reopen | 2 minor (benign) |
| Task 014 reader/handoff | **REOPEN** | F-014-1 (real hole) [F-014-2 WITHDRAWN — misdiagnosis, not a deviation] | 2 (non-blocking) |

Net: **one task reopens (014)** for one genuine behavior hole (F-014-1). F-014-2 was
WITHDRAWN after code verification (reading the local cache segment is segment-relative
in BOTH CH and Velox — a match, not a deviation). Everything else accepts. §3 structural fidelity of the
center SCC is confirmed in code, not merely asserted.

## 1. Dedup against round 1 (overlapping items — NOT re-litigated)

These round-2 structural findings map to already-signed round-1 decisions; recorded
as **already-signed, still holds** (or condition now discharged), not as new work:

| Round-2 label | Round-1 decision | Status |
|---|---|---|
| SD-012-1 `MetadataBucket`→`F14FastMap` ("SD4") | Round-1 **SD4** (decisions §5) authorized an F14 metadata bucket *only if review proves no iterator/ref/address survives a mutation* | **Condition discharged.** Shard 012 read every bucket consumer in `Metadata.cpp` and confirmed `getKeyMetadata` copies `it->second` into a `KeyMetadataPtr` under the bucket lock before any rehash-inducing insert; remove/iterate hold the lock across the batch and mutate only via terminal `erase(it)->next`. No SD1-class escape. **Accept, no new sign-off needed.** |
| SD-011-2 absl→F14 in eviction containers | Round-1 **SD2** (decisions §4) | Already-signed. `original_queue_types` correctly stays `std::unordered_map` (SD-011-1). |
| SD1 no-escape invariant (009 `ShardedMap`) | Round-1 **SD1/M1** (decisions §4) | Already-signed; round-2 re-verified the invariant holds in the 012 code paths that use the sharded metadata. |
| SD3 `KeyMetadata`=std::map, SD5 std::list queues | Round-1 **§5 "must remain unchanged"** | Confirmed preserved in code (node-stability load-bearing; not swapped). |
| errno reconcile (physical<=downloaded<=reserved) | Round-1 **§3 errno contract** (012 consumes; producer is a pre-release gate) | Already-signed; 012 matches. Pre-release producer gate still open (unchanged). |
| D-011-1..7 infra mappings, B2 overcommit-metric drop | Task-011 "Approved dependency mappings" + round-1 **B2/O2** | Applied exactly as signed. |
| SD6/SD7/SD9, D3/D9/D11, R2/R7 | Round-1 §4 + Task-012 amendments | Already-signed; no code divergence found. |

## 2. New decisions required (this round)

### F-014-1 — REOPEN Task 014: self-heal-on-external-truncation is dropped (CONFIRMED)

**Controller-verified against source.** CH `getCacheReadBuffer`
(`src/Disks/IO/CachedOnDiskReadBufferFromFile.cpp:448-477`): for a size-in-filename
segment in `DOWNLOADED`/`DETACHED` state, it reads the actual on-disk cache-file
size and, if `cache_file_size < file_segment.getDownloadedSize()`, resets the reader
and returns `nullptr` — telling the caller to bypass the cache and re-fetch from the
source (self-heal), plus an empty-file `LOGICAL_ERROR` guard at `:474`. CH's own
comment (`:465-469`) states throwing `CANNOT_READ_ALL_DATA` here would be
misinterpreted as a broken `MergeTree` part and wrongly detach it.

Velox `getCacheReadBuffer` (`FileCacheInputStream.cpp:123-146`) opens the local
cache file and returns it **unconditionally** — no size check, no empty-file guard.

Why this is a hole and NOT covered by the two accepted exclusions: the accepted
exclusions were *remote-object* truncation (`CANNOT_READ_ALL_DATA`, needs
`getRemoteFileMetadata`, genuinely absent in Velox) and `readBigAt` (deliberately
not ported). F-014-1 is a *different mechanism* — the **local cache-file** size —
and every input CH uses is present in Velox: `FileSegment::hasSizeInFileName`,
`getDownloadedSize`, `state`, and `ReadBufferFromVeloxReadFile::size()/tryGetFileSize()`.
So it is an omitted behavior, not a platform limitation.

Failure scenario: a cache-segment file truncated outside ClickHouse (or a
zero-length cache file) yields a silent short read instead of a transparent
re-fetch; when that file backs a MergeTree mark/metadata file the read is corrupt.

Fix (Task 014 reopen, implementation + test):
- In `getCacheReadBuffer`, after opening the local file, when
  `hasSizeInFileName() && state ∈ {DOWNLOADED, DETACHED}` and the opened file's size
  `< getDownloadedSize()`: reset the cache reader and signal CACHED->BYPASS (return a
  null/empty sentinel that the caller routes to `REMOTE_FS_READ_BYPASS_CACHE`),
  mirroring CH `:456-471`. Add the empty-file guard (`cache_file_size == 0`).
- RED test + false-green probe: pre-populate a DOWNLOADED size-in-filename segment,
  externally truncate its cache file, then read — assert the stream returns the
  correct full bytes (re-fetched from source) and did NOT short-read/throw. The test
  must go RED against the current unconditional-open code.

### F-014-2 — WITHDRAWN (misdiagnosis; NOT a deviation, no sign-off needed)

**This finding is retracted after code-level verification (user-driven, 2026-07-20).**
The original claim — "CH sets CACHED read-until in absolute coords, Velox uses
segment-relative, therefore a §3 deviation" — was a **misidentification of the CH
line**. The cited `CachedOnDiskReadBufferFromFile.cpp:796`
`setReadUntilPosition(min(range.right+1, file_size))` is the read-until on
`state->buf`, which for the CACHED read type IS the cache-file reader working in the
segment file's own space — not evidence of an absolute cache-file coordinate.

The load-bearing comparison is how each side SEEKS the local cache-file reader:
- CH CACHED (`CachedOnDiskReadBufferFromFile.cpp:820-829`):
  `seek_offset = offset - range.left; state->buf->seek(seek_offset, SEEK_SET);`
- Velox CACHED (`FileCacheInputStream.cpp:451-452`):
  `seekOffset = offset - range.left; state->reader->seek(seekOffset, SEEK_SET);`

**Byte-for-byte identical: both use segment-relative `offset - range.left`.** The
real cache-reading class on our side is `ReadBufferFromVeloxReadFile` wrapping the
per-segment 0-based file; CH's is the `pread` `ReadBufferFromFile` over the same
per-segment file. Reading the local cache segment is segment-relative in BOTH — this
is a MATCH, not a deviation. No structural deviation, no §3 sign-off, no code change.
The user was correct that this layer is (and should be) relative and that CH is the
same.

### F-011-T — test-coverage note, DOWNGRADED to non-blocking backlog (not a reopen)

`PriorityEvictionTest.cpp` white-box-includes the internal priority headers and
directly constructs `LRUFileCachePriority`/`SLRUFileCachePriority` (so the internals
ARE unit-drivable — it is not black-box-limited), but its 7 existing cases only
exercise post-`add` empty-state getters and hold accounting; none fills a queue,
triggers eviction, drives an SLRU promote/downgrade, or the hold-space split.

**Decision (user-corrected 2026-07-20): DOWNGRADE to non-blocking backlog. Do NOT
reopen 011, do NOT force into Task 015 as a gate.** Rationale:
1. The eviction IMPLEMENTATION is already §3-verified as a faithful literal
   translation of CH (shard 011 = ACCEPT); the code is correct, this is only
   regression-net thinness.
2. Eviction ORDER / SLRU transitions are `FileCache`-internal and NOT
   consumer-observable through the public get/getOrSet/tryReserve API, so they fail
   the A-ledger "consumer-observable behavior" bar. A white-box assertion on internal
   eviction order would weld the test to the current implementation structure — a
   brittle test that mis-fires on legitimate refactors, poor risk/reward.
3. Therefore it is NOT a correctness gate for Task 015.
Parked as an optional standalone hardening test task; not part of the zero-unresolved
gate.

## 3. Resolved-on-verification (no action)

- **F-011-O / O-011** (CacheUsage `CacheUsagePerUser` over-port): **NOT over-port.**
  `collectIdleClients` is reached on a real path (`FileCache.cpp:1983`), not only via
  the excluded overcommit policy. Keep as-is. Accept.
- **SD-012-3** (QueryLimit `query_map`/`records` = F14): benign, same class as the
  signed SD1 (shared_ptr values, dedicated `query_map_mutex`, no ref/iterator escape;
  `removeQueryContext` moves+erases under the mutex, returns by value). Accept; add a
  bookkeeping ledger row (N1). Not a reopen.
- **013 minor PLAUSIBLE**: error-text punctuation (no CH-text oracle → benign);
  single-key `remove(path,flags)` unused by seams (legitimate CH-mirror API, not
  over-port). No action.
- **013 O-013b LRU-trap**: **CLEARED** — `OpenedFileCache.h` has zero
  LRU/capacity/eviction (grep-confirmed); pure weak_ptr self-clearing 1024-shard
  table mirroring CH `src/IO/OpenedFileCache.h`.

## 4. Zero-unresolved gate

**NOT met — one blocker.** Task 014 reopens for **F-014-1 only** (fix + RED test).
F-014-2 is signed-accepted (no code change; ledger row §5). F-011-T is downgraded to
non-blocking backlog (not a gate). Task 015 must not start until F-014-1 is
fixed+tested and the user explicitly approves.

Carried-forward pre-release items (unchanged, not part of this gate): Task 004
StatusFile R3; Task 006 F-CALLERID; Task 008 sipHash R4/R5; SD8 recursive_mutex;
the errno-producer pre-release gate.

## 5. Structure-deviation ledger — new signed rows (this round)

| CH 结构 | Velox 替代 | 保证差异 | 硬约束出处 | E 探针证据 | 人工签字 |
|---|---|---|---|---|---|
| `FileCacheQueryLimit` `query_map`/`records` = node-based | `folly::F14FastMap` (值为 shared_ptr) | flat rehash 搬移 value（同 SD1 类） | 与已签字 SD1 同类；`query_map_mutex` 保护，`removeQueryContext` move+erase 后按值返回，无 ref/iterator 逃逸 | 代码核验无逃逸（shard 012） | **SIGNED** (SD-012-3/N1, review 2) |

`MetadataBucket`→`F14FastMap`（SD-012-1）不新开行：它是 round-1 **SD4** 的条件授权，
本轮已在代码中证明 no-escape 不变量成立，条件解除，沿用 SD4 签字。

## 6. F-014-1 root cause (user asked: 不是要求 exactly 移植吗？)

§3 铁律确实要求 exactly 移植内部结构；但 §3 只盯**内部结构**（锁/容器/状态机形状），
**行为漏一条它不覆盖**——那是 A 台账 + D 清扫的职责。F-014-1 正是行为漏移植：

- Task 014 从 CH `CachedOnDiskReadBufferFromFile` 移植时走「函数→函数映射表」
  （task 文件的 mapping table），映射表只说 `getCacheReadBuffer` 负责「打开本地缓存
  文件」，没把 CH 函数体内那段**截断自愈分支**（`cpp:448-477`）列成独立合同行。
- worker 实现了主干（打开并返回本地文件），主干对；防御性分支不在映射表里就漏了。
- 逐 task 的 controller review 和 worker 共享同一张映射表，盲区相同（guide 第 1 节
  根因 3：逐 task review 共享盲区）。只有本轮拿 CH 源码逐行 diff 的整体清扫 diff 出来。

教训：移植粒度是「函数」而非「函数体每条分支」时，防御性/边界分支最易漏。
这正是 checkpoint 整体清扫存在的意义——它按预期抓到了一条 per-task review 抓不到的洞。
