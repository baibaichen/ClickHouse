# Task 012 Result: `FileCache` Center SCC

## Worker attempt 1

```text
worker_status: blocked
environment_profile: home-chang
task: 012
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `e5b2af1a9` | clean (0 files); `filecache2...baibaichen/filecache [ahead 2]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | clean |

`HEAD` `e5b2af1a9` = "Task 011: Port priority/eviction sources (center-SCC Part A)".
The prompt's environment profile (`home-chang`, branch `filecache2`, HEAD `e5b2af1a9`)
overrides the generic branch name `filecache` written in `012-*.md`; the HEAD matches
the Task-011 committed sources this task builds on.

## Files changed

```text
None. The worktree was returned to the clean baseline commit e5b2af1a9.
Two files were created during investigation (FileSegmentInfo.h, tests/FileSegmentInfoTest.cpp)
and then removed, because a leaf-only delivery does not restore the atomic green build this
task requires and would leave the tree in a half-scaffolded, non-building state.
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| `git status --short --branch` / `git log -1` (velox baseline) | 0 | (stdout captured in receipt) |
| Read + API extraction of all Task-011 headers and Task 003-010 shims | 0 | (research only; no build attempted for a false-green) |

No configure/build/test log was produced. The single mandatory gate for this task is a
green `velox_ch_filecache_core_scc_test` (0 failed / 0 skipped). Because that gate cannot be
truthfully closed in this pass (see Blockers), running a configure/red-build only to
manufacture a partial log would not constitute the required acceptance evidence and is
therefore not claimed.

## Acceptance evidence

```text
test count: 0 (no test target built)
failed tests: n/a
skipped/disabled tests: n/a
benchmark result, when required: n/a
git diff --check: clean (no changes; tree at baseline e5b2af1a9)
```

## Worker review

```text
review subagent: not launched
findings: N/A — the read-only review step (protocol rule 10) is reached only after a local
  green build/test exists. There is no completed diff to review; launching a reviewer over an
  empty/baseline tree would produce no actionable findings.
resolutions: N/A
unresolved findings: the scope blocker below
```

## Blockers

```text
Blocker: the assigned single-pass acceptance gate (one green compile+link+test closure of the
entire FileCache center SCC) cannot be met truthfully in one worker attempt at the required
fidelity. This is a scope/size blocker, NOT an unreviewed-dependency blocker.

Pre-implementation gate results (protocol worker rules 5 and 6) — both PASS:
  * Unreviewed-dependency gate: PASS. Every CH dependency reached by this task has an explicit
    approved Velox mapping. Verified concrete APIs against the real headers:
      - Memory<> + DBMS_DEFAULT_BUFFER_SIZE (D3)  -> FileCacheBufferState / CacheBuffer
        (allocateOwned(pool,size,alignment)) in
        velox/ch/IO/ReadBufferFromVeloxReadFile.h.
      - SCOPE_EXIT (D9)  -> folly SCOPE_EXIT({...}) from <folly/ScopeGuard.h>
        (already used in SLRUFileCachePriority.cpp:690).
      - Stopwatch (D11)  -> facebook::velox::DeltaCpuWallTimeStopWatch
        (velox/common/time/CpuWallTimer.h); elapsed().wallNanos / 1'000'000.
      - callOnce/OnceFlag (D8)  -> std::call_once / std::once_flag.
      - FileCacheBoundedQueue: velox/ch/Common/FileCacheBoundedQueue.h
        (push/tryPush(timeoutMs)/pop/tryPop/finish; NO size()/cancel()).
      - Thread pools (Task 005), Scheduler + FileCacheQueryIdScope (Task 006), IO adapters
        (Task 007), leaf types (Task 008), ShardedMap withShard/forEachShard (Task 009),
        FileCacheConfig/FileCacheReadOptions (Task 010) — all located and signature-verified.
      - FileCacheErrnoException (getErrno()->int): does NOT yet exist anywhere in velox/ch/;
        the 012 amendment authorizes Task 012 to IMPLEMENT it as a FileCache-owned typed
        exception. Confirmed there is currently no structured-errno producer in the write path
        (grep for errno/ENOSPC/EDQUOT in velox/ch/ hits only StatusFile.cpp). The amendment
        itself calls this a "separate pre-release gap" and forbids a reconcile-every-exception
        fallback; the partial-physical-append-failure test must instead drive a real fault via
        an injected production WriteFile that commits a strict prefix then throws
        FileCacheErrnoException — feasible, but only inside the full FileSegment::write path.
  * Contract-derivation gate: PASS with one recorded divergence to reconcile toward CH source.
    The illustrative signatures in 012-*.md Steps 9-13 diverge from the authoritative CH
    headers and must be reconciled toward CH (CH source is higher authority than the task per
    EXECUTION_PROTOCOL "Authority order"). Concrete examples:
      - FileCache::getOrSet in CH takes (key, offset, size, file_size, CreateFileSegmentSettings,
        file_segments_limit, origin, boundary_alignment) — NOT the (key, Range, origin, options)
        form shown in Step 13.
      - FileCache::get / getDownloadedContiguousOrEmpty / set / trySet / tryReserve /
        getQueryContextHolder signatures in CH (FileCache.h:147-258) differ from Step 13.
      - FileSegmentInfo::download_finished_time is time_t in CH but the Step-9 spec (and its
        FileSegmentInfoTest) use std::chrono::time_point<steady_clock>; the new-file spec wins
        for FileSegmentInfo.h but callers in Metadata/FileSegment must be adapted consistently.
      - LockedKey::removeFileSegment has two arities that must both compile against one
        declaration (SLRUFileCachePriority.cpp:812 uses (offset, lock); EvictionCandidates.cpp:
        318-319 uses (offset, lock, can_be_broken, invalidate_queue_entry)).

Why the gate cannot be closed in one pass (scope):
  * CH center-SCC source to port faithfully:
      FileSegment.cpp 1554, FileSegment.h 388
      Metadata.cpp 1425, Metadata.h 432
      FileCache.cpp 3248, FileCache.h 476
      QueryLimit.cpp 170, QueryLimit.h 95
      FileSegmentInfo.h 81
    plus FINISHING the already-committed Task-011 .cpp files against the real types they were
    intentionally written without (IFileCachePriority/LRU/SLRU/Split/EvictionCandidates .cpp,
    ~2900 lines), plus SIX behavioral-RED test files (FileSegment/Metadata/FileCache/QueryLimit/
    PriorityEviction/FileSegmentInfo).
  * The SCC is, by the task's own analysis, a genuine strongly connected component: there is
    NO intermediate link step. Partial .cpp presence is explicitly not expected to link. The
    stage is green only when ALL of FileSegment/Metadata/FileCache/QueryLimit + all Task-011
    .cpp compile and link together and every test passes with 0 failed / 0 skipped.
  * Therefore a leaf-only or header-only partial delivery does NOT restore the build and is not
    an acceptable intermediate; it would leave the tree non-building. The honest outcome for a
    single pass that cannot faithfully port ~9000 lines AND iterate them to a real clean
    compile+link+test is `blocked`, per protocol rules 8 (no false-green), 9 (real behavioral
    RED required per contract), and 12 (blocked when unresolved). Emitting thousands of lines of
    unverified adapted C++ and asserting a passing velox_ch_filecache_core_scc_test would be
    exactly the false-green the protocol forbids.

Exact decision needed from the controller/user:
  1. Confirm the reconciliation direction for the Step 9-13 signature divergences (port toward
     the authoritative CH FileCache.h/Metadata.h/QueryLimit.h signatures, treating 012-*.md
     Steps 9-13 as illustrative), and confirm the FileSegmentInfo::download_finished_time type
     (chrono::time_point per Step 9 spec vs time_t per CH) so downstream callers are consistent.
  2. Decide how to make this atomic SCC stage tractable for a compile/link-verified delivery,
     e.g. authorize an incremental header-first-then-single-cpp-closure worker sub-plan with
     interim configure-only checkpoints, or split the port across multiple bounded worker
     attempts against the SAME green gate, rather than a single monolithic pass.
  3. Confirm the injected-production-WriteFile approach for the partial-physical-append-failure
     test (a real production FileSegment::write path over a fault-injecting velox::WriteFile that
     commits a strict prefix then throws FileCacheErrnoException) satisfies "execute the
     production path, not reconciliation inside a mock".

First actionable evidence: the five CH center-SCC files total 7869 lines
(wc -l FileSegment.{h,cpp} Metadata.{h,cpp} FileCache.{h,cpp} QueryLimit.{h,cpp}
 FileSegmentInfo.h -> 1554+388+1425+432+3248+476+170+95+81), which must all be ported and
linked together with the ~2900 lines of Task-011 .cpp (currently non-compiling by design) into
one green closure. No structured-errno producer exists in velox/ch/ today
(grep errno velox/ch/ -> only Common/StatusFile.cpp).
```

## Worker declaration

```text
Only Task 012 was attempted.
No implementation changes were left on disk; the Velox worktree is at the clean baseline
commit e5b2af1a9. Changes are unstaged and uncommitted (there are none).
The worker stopped after writing this receipt.
```

## Controller unblock response 1 (scope: sub-attempt plan)

```text
controller_status: blocker_resolved
task: 012
resolved: 2026-07-20
```

## Resolution

```text
root cause:
  Not a dependency blocker (that gate PASSed). A real scope blocker: the atomic
  SCC is ~7868 lines of CH center source + ~2900 lines of Task-011 .cpp + 6
  behavioral-RED tests, with no intermediate link step. A single monolithic
  worker pass cannot reach the required fidelity without risking false-green.
  The worker correctly returned blocked rather than fabricating a green.
  Controller independently verified the reported signature divergences against
  CH source (getOrSet multi-arg @ FileCache.h:147; download_finished_time = time_t
  @ FileSegmentInfo.h:74; removeFileSegment multiple arities @ Metadata.h:391,397)
  and the 7868-line total.

decision (user-approved 2026-07-20):
  1. CH source is authoritative over the task's illustrative Step 9-13 signatures.
     Port toward CH: getOrSet/get/set/tryReserve/getQueryContextHolder use CH
     FileCache.h signatures; download_finished_time is time_t; removeFileSegment
     keeps CH's multiple arities. Recorded in the Task-012 amendment
     "### Task 012 execution — CH-source authority + sub-attempt plan".
  2. Split Task 012 into bounded sub-attempts against the SAME final green gate
     (velox_ch_filecache_core_scc_test, 0 failed / 0 skipped). Intermediate
     sub-attempts deliver source + compile-only sanity and MUST NOT claim a
     link/test green. Cut: S1 headers; S2 FileSegment.cpp+Metadata.cpp (+finish
     Task-011 .cpp); S3 FileCache.cpp+QueryLimit.cpp; S4 CMake + 6 tests + final
     green. Each is a fresh Worker, reviewed before the next.
  3. Partial-physical-append-failure test: real production FileSegment::write over
     an injected production WriteFile that commits a strict prefix then throws
     FileCacheErrnoException; reconciliation in production, not the test double.

task update:
  Task 012 amendment records CH-source authority, the 4-sub-attempt plan, and the
  test approach.

evidence:
  CH signatures FileCache.h:147,177; FileSegmentInfo.h:74; Metadata.h:391,397.
  Center-SCC total 7868 lines (wc -l). No structured-errno producer in velox/ch
  today (grep errno velox/ch -> only StatusFile.cpp) — FileCacheErrnoException is
  implemented in S2/S3 per the amendment; the errno PRODUCER remains a pre-release
  gap and must not become a reconcile-every-exception fallback.

redispatch: yes — as bounded sub-attempts S1..S4 (Controller dispatches S1 first).
```

## Worker attempt (S1 headers)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 012 (sub-attempt S1 — center-SCC HEADERS ONLY)
```

S1 delivers only the five center-SCC headers plus a configure + compile-only
(header-parse) sanity check. Per the sub-attempt plan, S1 does NOT link the
library and does NOT build or run any test target; that is S4. It is expected
and correct that the full `velox_ch_filecache_core` library / SCC test do not
link yet, because the `.cpp` files are S2-S4.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `e5b2af1a9` | clean (0 files); `filecache2...baibaichen/filecache [ahead 2]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | clean |

`HEAD e5b2af1a9` = "Task 011: Port priority/eviction sources (center-SCC Part A)".
No pre-existing dirty files in either repository. Only the five new headers were
created; no other file was modified. No staging/commit/amend/rebase/push.

## Files changed

```text
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/FileSegmentInfo.h   (new, 102 lines)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/FileSegment.h       (new, 413 lines)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/Metadata.h          (new, 475 lines)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/QueryLimit.h        (new, 126 lines)
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/FileCache.h         (new, 507 lines)
```

CMakeLists.txt was intentionally NOT edited: the home-chang build is
`VELOX_MONO_LIBRARY=ON`, so headers are already discoverable via the root
`include_directories(.)`; the non-mono `FILE_SET HEADERS` block is skipped in
this build, and configure + header-parse both succeed without touching it. No
`.cpp` source and no test target were registered (that is S4).

## CH-source-authority reconciliations applied (headers)

```text
FileSegmentInfo::download_finished_time         -> time_t (CH FileSegmentInfo.h:74), NOT chrono.
FileCache::getOrSet/get/getDownloadedContiguousOrEmpty/set/trySet/tryReserve/getQueryContextHolder
                                                -> CH FileCache.h signatures (multi-arg getOrSet, etc.).
LockedKey::removeFileSegment                    -> both CH arities declared (Metadata.h:391,397):
                                                   (offset, lock, can_be_broken, invalidate_queue_entry)
                                                   and (offset, can_be_broken, invalidate_queue_entry).
FileCacheReserveStat                            -> CH shape (total_stat, stat_by_kind, getStatByKind,
                                                   Stat{releasable/non_releasable size+count, evicting_count,
                                                   moving_count, invalidated_count, candidates_iteration_steps,
                                                   clients_iterated}, State{Releasable,NonReleasable,Evicting,
                                                   Moving,Invalidated}, update, operator+=). This matches the
                                                   Task-011 .cpp call sites (stat.total_stat, stat.update,
                                                   FileCacheReserveStat::State::*), NOT the task's illustrative
                                                   total/evicting/moving shape.
getCommonOrigin                                 -> instance method `const OriginInfo & getCommonOrigin() const`
                                                   (reviewed core design change; host-injected commonUserId);
                                                   getInternalOrigin stays static (Task-011 EvictionCandidates.cpp
                                                   calls FileCache::getInternalOrigin()).
```

## Recorded mappings applied (headers reach these)

```text
D3  Memory<> + DBMS_DEFAULT_BUFFER_SIZE  -> CacheMetadata::downloadImpl(FileSegment&, std::optional<CacheBuffer>&).
D8  callOnce/OnceFlag                    -> std::once_flag initialize_called.
D-011-2 pcg64_fast/randomSeed RNG        -> folly::Random; CheckCacheProbability stores only the
                                            std::bernoulli_distribution + seed (generator drawn at the
                                            call site in FileCache.cpp, S3).
magic_enum::enum_count<FileSegmentKind>() -> literal 2 (std::array<Stat,2> stat_by_kind).
CH ReadBufferFromFileBase/WriteBufferFromFile -> velox ReadBufferFromFileBase/WriteBufferFromFileBase (IO/*.h).
throw Exception(ErrorCodes::LOGICAL_ERROR,...) -> throwFileCacheException(...) (FileSegmentMetadata inline methods).
FilesystemCacheSettings (read-time per-query settings) -> FileCacheReadOptions (getQueryContextHolder,
                                            FileCacheQueryLimit::getOrSetQueryContext).
TSA macros                               -> via velox/ch/Common/ClickHouseTSA.h (transitively through
                                            LRU/SLRU/Split headers).
F14 (absl-style)                         -> MetadataBucket = folly::F14FastMap<FileCacheKey, KeyMetadataPtr,
                                            FileCacheKeyHash>; ShardedMap origins F14 inner map.
FileCacheErrnoException                  -> NOT referenced by any header type; no forward declaration was
                                            needed in S1. Its full definition remains S2/S3.
```

## §3 structural invariants exposed by the headers

```text
SD3 KeyMetadata      -> privately inherits ordered std::map<size_t, FileSegmentMetadataPtr> (NOT F14);
                        lower_bound / adjacency / iterator-stability preserved.
SD5 FileSegments/LRU -> FileSegments stays std::list (via FileCache_fwd_internal.h);
                        LRUQueue std::list preserved in the Task-011 headers.
SD4 MetadataBucket   -> folly::F14FastMap wrapped by per-bucket CacheMetadataGuard; the stored
                        shared_ptr<KeyMetadata> keeps the pointee stable across bucket mutation.
LockedKey            -> member order key_metadata (shared_ptr) BEFORE lock (KeyGuard::Lock), so the
                        lock destructs before the metadata reference drops (three-phase / cancel-before-join
                        structure the .cpp will honor).
FileCache            -> main_priority declared before metadata (metadata destroyed first, priority
                        iterators stay valid); StatusFile held for the full lifetime.
noncopyable          -> deleted copy/move where CH used boost::noncopyable (FileSegment, FileSegmentsHolder,
                        FileSegmentMetadata, KeyMetadata, CacheMetadata, LockedKey, FileCache, QueryContext,
                        QueryContextHolder).
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure (home-chang recipe + `-DVELOX_BUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s1_configure.log` |
| header-parse (`/usr/bin/c++` g++ 13.3, real project flags `-std=gnu++20` from `compile_commands.json`, `-fsyntax-only -x c++`) for all 5 headers | 0 (all 5) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s1_headercheck.log` |

The header-parse check reuses the exact include/define flags the project uses to
compile an existing FileCache TU (`FileCacheSettings.cpp`), so it exercises the
real compiler + include graph. No `-j` was passed. Each header was parsed
standalone (`#include`-only via `-x c++`).

## Acceptance evidence

```text
configure: exit 0 (task012_s1_configure.log).
header parse (5/5): exit 0 each — FileSegmentInfo.h, FileSegment.h, Metadata.h,
  QueryLimit.h, FileCache.h all parse under the project's real g++ 13.3 / gnu++20
  flags (task012_s1_headercheck.log).
download_finished_time == time_t: confirmed (FileSegmentInfo.h and FileSegment.h).
link/test: NOT ATTEMPTED IN S1 BY DESIGN. The center SCC has no intermediate link
  step; the .cpp are S2-S4. No green link or test is claimed.
git diff --check: n/a (all files new/untracked); `git status --short` shows exactly
  the 5 new headers, nothing else. Baseline commit e5b2af1a9 preserved.
```

## Worker review

```text
review subagent: one read-only general-purpose reviewer over the S1 header diff,
  given the CH headers, the CH-source-authority rules, and the approved mappings.
findings:
  1. FileSegmentMetadata::releasable uses `file_segment.use_count() == 1` vs CH
     `isSharedPtrUnique(file_segment)` — flagged as a possibly-unreviewed semantic
     substitution.
  2. QueryLimit.h uses folly::F14FastMap for `Records` and `query_map` vs CH
     std::unordered_map — flagged as a possibly-undocumented deviation.
  Reviewer confirmed: download_finished_time == time_t; FileCacheReserveStat CH
  shape; §3 invariants (ordered std::map KeyMetadata, F14 MetadataBucket, LockedKey
  member order, main_priority-before-metadata) all correct; no missing methods, no
  wrong types, no spurious friends/members, no over-port.
resolutions:
  1. NOT actionable — already a reviewed mapping. Metadata design doc
     `port/2-file-cache/08-filecache-metadata-files-design.md` (lines 171-180)
     explicitly authorizes the port to write `return file_segment.use_count() == 1;`
     ("Velox port 可以直接写 ..."), with the caveat to keep the same reference-count
     expectation and not degrade it to a state-only check — which this port honors.
     Kept as written.
  2. NOT actionable — reviewed. Task-012 Step 12 illustrative shape specifies
     `folly::F14FastMap` for both `records` and `query_map`, consistent with the
     port-wide `std::unordered_map -> folly::F14FastMap` container mapping in the
     infra matrix (`port/1-dependencies/01-filecache-infra-mapping.md`). Both maps
     are internal, non-order-dependent lookup maps (no iterator/reference escapes),
     so the SD1/SD4 no-escaping-reference invariant is satisfied. Kept as written.
unresolved findings: none.
```

## Blockers

```text
None.

Note on an out-of-scope pre-existing defect discovered while header-parsing:
`velox/ch/Interpreters/FileCache/EvictionCandidates.h` (a Task-011 header, NOT in
S1 scope) does not parse under the current g++ 13.3 / gnu++20 configuration:
  - it uses C++23 "deducing this" (`auto begin(this auto && self)`), unsupported by
    g++ 13.3 even at -std=gnu++23; and
  - it calls `folly::F14FastSet::merge`, which this folly version does not provide.
It has never actually been compiled (Task 011 was "structural check only" and its
`.cpp` are not yet registered — `EvictionCandidates.cpp` is absent from
`compile_commands.json`). FileCache.h therefore does NOT include EvictionCandidates.h;
it forward-declares `EvictionInfo`/`EvictionCandidates` (they are only referenced by
reference/pointer in private FileCache method signatures, matching CH, which also does
not transitively pull the full definition into its apex). This keeps all five S1
headers parseable now. The EvictionCandidates.h defects are flagged here for S2/S4 to
resolve when the priority/eviction `.cpp` are finished and the SCC is compiled+linked;
they are not an S1 blocker and are outside the S1 file scope.
```

## Worker declaration

```text
Only Task 012 sub-attempt S1 (center-SCC headers only) was attempted.
Exactly five new header files were created; no other file was modified; CMakeLists
was not touched. No .cpp, no tests, no test/CMake registration were added.
Link and test were intentionally NOT attempted in S1 (no false green claimed).
Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Controller review (S1 headers, accepted)

```text
controller_status: accepted (sub-attempt S1; stage gate remains S4)
environment_profile: home-chang
task: 012 S1
reviewed: 2026-07-20
```

## Review evidence (S1)

```text
scope: exactly 5 new headers (FileSegmentInfo/FileSegment/Metadata/FileCache/
  QueryLimit .h); no .cpp, no test, no CMake change; git diff --check clean.
CH-source authority verified: FileCache::getOrSet byte-identical to CH
  FileCache.h:147 (key,offset,size,file_size,CreateFileSegmentSettings,
  file_segments_limit,origin,boundary_alignment_); FileSegmentInfo::
  download_finished_time = time_t; LockedKey::removeFileSegment keeps both CH
  arities (Metadata.h:432,438).
logs (independently read): task012_s1_configure.log -> "Configuring done /
  Generating done / Build files written" (exit 0). task012_s1_headercheck.log ->
  all 5 headers -fsyntax-only exit 0, 0 errors. Only benign warnings: "#pragma
  once in main file" (expected for standalone header syntax-check) and
  "'guarded_by' attribute directive ignored [-Wattributes]" — the expected GCC
  behavior for the D-011-7 TSA macros (GCC ignores, does not error; Clang honors).
S1 correctly did NOT claim a link or test green (SCC has no intermediate link
  step; .cpp are S2-S4).
unresolved findings: None for S1.
```

## Commits (S1)

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `853840ae5 Task 012: Port center-SCC headers (sub-attempt S1)` |

## Worker attempt (S2 FileSegment/Metadata + finish Task-011 cpp)

```text
worker_status: blocked
environment_profile: home-chang
task: 012 (sub-attempt S2 — FileSegment.cpp + Metadata.cpp + finish Task-011 .cpp; compile-only)
```

S2 was to CREATE `FileSegment.cpp` and `Metadata.cpp` and MODIFY the five committed
Task-011 `.cpp` (`IFileCachePriority/LRU/SLRU/Split/EvictionCandidates.cpp`) so they
compile per-TU against the real S1 headers, with a per-TU COMPILE-ONLY check (no link,
no test — that is S4). The declared S2 file scope is exactly those two new `.cpp`, the
five existing `.cpp`, optionally `tests/`-less `CMakeLists.txt`, and optionally a small
FileCache-owned header for `FileCacheErrnoException`. It does NOT include editing the
accepted S1 headers (`FileSegment.h`, `Metadata.h`, `FileCache.h`, `FileCacheSettings.h`)
or the Task-011 header `EvictionCandidates.h`.

Both pre-implementation gates were worked, then S2 stopped `blocked` on **two concrete
structural conflicts that can only be resolved by editing headers outside the S2 file
scope**. Neither is a "pick the closest API" guess; each is a reviewed mapping whose
required injection point was never built into the S1/Task-011 headers. Per protocol
worker rule 4 (stop, do not silently expand scope) and the fallback/unreviewed-dependency
rules, S2 does not guess an API, add a no-op/stub, or edit accepted headers.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `853840ae5` | clean (0 files); `filecache2...baibaichen/filecache [ahead 3]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | clean |

`HEAD 853840ae5` = "Task 012: Port center-SCC headers (sub-attempt S1)". No pre-existing
dirty files in either repository. No source file was created or modified in this attempt;
the Velox worktree remains exactly at the clean S1 baseline (`git status --porcelain`
empty). No staging/commit/amend/rebase/push. Only build-directory probe logs were written
(under `<velox_build_dir>`, outside the worktree).

## Files changed

```text
None. No .cpp was written and no CMake/header edited: S2 stopped blocked before any
in-scope file could be made to compile, because compilation is impossible without header
edits that lie outside the S2 file scope (see Blockers).
```

## Contract-derivation done before stopping

```text
Full CH source read and contract-derived: src/Interpreters/FileCache/FileSegment.cpp
(1554 lines) and Metadata.cpp (1425 lines), plus their design docs
(09-filecache-file-segment-design.md, 08-filecache-metadata-files-design.md) and the
committed S1 headers. A complete CH-dependency -> velox-shim map was built (recon over
velox/ch): throwFileCacheException, logger_useful no-op macros, ProfileEvents/
CurrentMetrics no-op shims, ProfileEventTimeIncrement<Microseconds>, WriteBufferFrom-
VeloxWriteFile/ReadBufferFromVeloxReadFile + CacheBuffer, FileCacheQueryIdScope::
currentQueryId/getCallerId + folly::getOSThreadID, folly SCOPE_EXIT (no SCOPE_EXIT_SAFE),
chassert (no UNUSED macro -> use (void)/[[maybe_unused]]), Guards.h lock types,
FileCacheUtils::roundUpToMultiple, ShardedMap withShard/forEachShard, ThreadFromGlobalPool
= FileCacheWorker (make_unique<ThreadFromGlobalPool>(workerPool, callable) + join()),
DeltaCpuWallTimeStopWatch (velox/common/time/CpuWallTimer.h, confirmed present).

CH-source-authority reconciliations noted for the .cpp (would have been applied):
  - FileSegment::wait: committed S1 header is `State wait(size_t offset)` WITHOUT the
    folly::CancellationToken the task Step 10 / 09-design illustrate. The accepted S1
    header wins (CH-source-authority + accepted-receipt): port `wait` with the 1s-slice /
    60s-deadline cv loop but no injected token / QueryStatus (background never calls
    wait; query cancellation is simply not wired in this MVP). This is NOT a blocker.
  - magic_enum::enum_name(KeyState/State) in Metadata.cpp -> explicit switch/name helper.
  - toString(FileSegmentKind) defined in FileSegment.cpp via the switch helper (no
    magic_enum), matching the S1 declaration in FileSegmentInfo.h.
  - WriteBufferFromOwnString (getInfoForLogUnlocked) -> fmt/std::string assembly.
  - timeInSeconds(system_clock::now()) -> duration_cast<seconds>(now().time_since_epoch()).
  - Memory<> owned buffer (downloadImpl) -> std::optional<CacheBuffer> per S1 Metadata.h
    downloadImpl signature and D3.
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| Extract real project compile flags from `compile_commands.json` (FileCacheSettings.cpp TU) | 0 | `/tmp/ch_flags.txt` (flags string; `-std=gnu++20`, real includes/defines) |
| Per-TU compile probe `IFileCachePriority.cpp` (`/usr/bin/c++` g++ 13.3, real flags, `-fsyntax-only`) | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2_IFileCachePriority_probe.log` |
| Per-TU compile probe `LRUFileCachePriority.cpp` (same flags, `-fsyntax-only`) | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2_LRUFileCachePriority_probe.log` |
| Copy of first-blocker evidence (EvictionCandidates.h header defect) | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2_evictioncandidates_header_defect.log` |

No `-j` was passed. The probes reuse the exact include/define/std flags the project uses
to compile an existing FileCache TU. No configure was re-run (S1's configure is still
valid; the blocker is source/header-structural, not a configure failure). No link and no
test were attempted (correct for S2 by design).

## Acceptance evidence

```text
per-TU object compiles: 0 of the targeted TUs compile. Root cause is NOT the (unwritten)
  FileSegment.cpp/Metadata.cpp — it is that the five Task-011 .cpp I must finish cannot
  compile because a Task-011 HEADER outside the S2 modify-scope does not parse under the
  project's real g++ 13.3 / gnu++20 toolchain, AND the .cpp cannot be written faithfully
  because the accepted S1 headers lack two reviewed injection points.
link/test: NOT attempted in S2 by design (the center SCC has no intermediate link step;
  FileCache.cpp/QueryLimit.cpp are S3, tests+final green are S4).
git diff --check: n/a (no changes). git status --porcelain empty; baseline 853840ae5
  preserved.
```

## Worker review

```text
review subagent: not launched.
findings: N/A — protocol rule 10 launches the read-only reviewer over a COMPLETED task-
  owned diff after local validation. S2 produced no diff (blocked before any in-scope file
  could compile); reviewing an empty/baseline tree yields no actionable finding. This
  mirrors how the S1-precursor blocked attempt handled the same situation.
resolutions: N/A
unresolved findings: the two structural blockers below.
```

## Blockers

```text
S2 is blocked on structural gaps that can only be closed by editing files OUTSIDE the
declared S2 file scope. Both are reviewed mappings; neither is an unreviewed-dependency
"closest API" choice, and neither may be stubbed/no-op'd (forbidden fallback) or worked
around by silently expanding scope (worker rule 4).

BLOCKER B1 — Task-011 header `EvictionCandidates.h` does not compile under the project
toolchain, and it is NOT in the S2 modify-scope.
  First actionable error (task012_s2_IFileCachePriority_probe.log / _header_defect.log):
    EvictionCandidates.h:125:32: error: 'class folly::F14FastSet<std::shared_ptr<
      facebook::velox::ch::CacheUsage>>' has no member named 'merge'
        (EvictionInfo::takeKeptAliveCacheUsage calls kept_alive_cache_usage.merge(...))
    EvictionCandidates.h:157:16: error: expected identifier before 'this'
        auto begin(this auto && self) { return self.candidates.begin(); }   // C++23
    EvictionCandidates.h:158:14: error: expected identifier before 'this'
        auto end(this auto&& self) { return self.candidates.end(); }        // C++23
  Cause: the header uses C++23 "deducing this" (unsupported by g++ 13.3 even at
  -std=gnu++23) and `folly::F14FastSet::merge` (absent in this folly). It has never been
  compiled (Task 011 was "structural check only"; EvictionCandidates.cpp is absent from
  compile_commands.json). Every Task-011 .cpp I must "finish" includes this header
  transitively (IFileCachePriority.cpp:17 -> EvictionCandidates.h directly; LRU/SLRU/Split
  reach it via FileCache.h -> ... ), so ALL five fail at the SAME first errors before any
  of my SCC-type wiring is even exercised.
  S1's own receipt flagged exactly these defects "for S2/S4 to resolve" — but S2's declared
  file scope lists only the two new .cpp and the five existing .cpp, NOT EvictionCandidates.h
  (a header). Fixing it (replace `this auto&&` deducing-this with const/non-const overloads;
  replace F14FastSet::merge with an insert-range loop) is a small, safe header edit, but it
  is a scope expansion the Controller must authorize.

BLOCKER B2 — the accepted S1 headers lack two reviewed injection points that Metadata.cpp
and FileSegment.cpp REQUIRE per the design's explicitly non-stubbable list; adding them
means editing accepted S1 headers, also outside S2 scope.
  (a) Background-download reserve timeout. CH Metadata.cpp:966 reads
      Context::getGlobalContextInstance()->getReadSettings()
        .filesystem_cache_settings.reserve_space_wait_lock_timeout_milliseconds
      and passes it to FileSegment::reserve. The reviewed design
      (08-filecache-metadata-files-design.md:471-473) mandates: "背景下载的 reserve timeout
      不再从 global Context 取。把 reserve_space_wait_lock_timeout_milliseconds 从
      FileCacheConfig 注入 CacheMetadata." BUT: FileCacheConfig (FileCacheSettings.h) has NO
      reserve_space_wait_lock_timeout_milliseconds field, and CacheMetadata (Metadata.h) has
      NO constructor parameter/member for it. There is no velox Context. So downloadImpl
      cannot obtain the timeout without either (i) adding the config field + CacheMetadata
      member (edits FileCacheSettings.h + Metadata.h, out of scope), or (ii) a hard-coded
      literal / no-op (forbidden fallback + contradicts the reviewed injection contract).
  (b) Opened-file-handle invalidation. CH FileSegment.cpp:801-802
      (renameToIncludeSizeInNameUnlocked) and Metadata.cpp:1267-1268 (removeFileSegmentImpl)
      call OpenedFileCache::instance().remove(path, flags [| O_DIRECT]). The reviewed design
      (08:511-517 and 09-filecache-file-segment-design.md:646-653) mandates replacing the
      singleton with "an injected opened-file cache invalidation callback/reference, 不创建
      新的 singleton", and 08:571 lists "file removal + opened handle invalidation" among the
      "第一阶段不允许 stub 的内容" (must NOT be stubbed in phase 1). BUT: OpenedFileCache is a
      Task-013 Manager concept (3-consumers/02-filecache-manager-design.md:35,89,244 —
      manager-owned), it does not exist in velox/ch yet, and neither FileSegment nor
      CacheMetadata has any injected callback/reference member in the committed S1 headers.
      So the invalidation cannot be expressed without (i) adding an injected
      std::function/reference member to CacheMetadata (and reaching it from FileSegment)
      — edits Metadata.h/FileSegment.h, out of scope — or (ii) dropping/omitting the call
      (a stub explicitly forbidden by 08:571).

Exact decision needed from the Controller (any one path unblocks a redispatch):
  1. B1: authorize S2 to also edit the Task-011 header EvictionCandidates.h with the two
     minimal portability fixes (deducing-this -> const/non-const overloads; F14FastSet::
     merge -> insert-range), since the S1 receipt already scoped these to "S2/S4"; or move
     them into a dedicated fix.
  2. B2(a): authorize adding `reserve_space_wait_lock_timeout_milliseconds` (name TBD, e.g.
     `reserveSpaceWaitLockTimeoutMs`) to FileCacheConfig AND a matching CacheMetadata
     constructor parameter/member, and confirm FileCache passes it through at construction
     — i.e. amend FileCacheSettings.h + Metadata.h (+ FileCache.h/FileCache.cpp wiring in
     S3) within the S2/S3 scope.
  3. B2(b): confirm the concrete opened-file invalidation injection shape for the SCC —
     e.g. a `std::function<void(const std::string& path)>` (or an
     `OpenedFileCache*`/reference) member on CacheMetadata, injected at construction and
     reached by FileSegment through its cache/key-metadata — and authorize adding that
     member to Metadata.h (and the reach path in FileSegment.h) in S2, OR explicitly rule
     that opened-handle invalidation is deferred to Task 013 and that omitting the call in
     the SCC is acceptable for now (which would relax 08:571's non-stub rule and must be
     recorded as an amendment, since silently omitting it here is otherwise a forbidden
     stub).

First actionable evidence: task012_s2_IFileCachePriority_probe.log shows the B1 first
errors verbatim. CH line anchors for B2: Metadata.cpp:966 (Context reserve timeout);
FileSegment.cpp:801-802 and Metadata.cpp:1267-1268 (OpenedFileCache::instance().remove).
Design anchors: 08:471-473, 08:511-517, 08:571; 09:646-653; 02-manager:35,89,244.
```

## Worker declaration

```text
Only Task 012 sub-attempt S2 was attempted.
No source or CMake file was created or modified; the Velox worktree is at the clean S1
baseline commit 853840ae5 (git status --porcelain empty). Only build-directory probe logs
were written. Changes are unstaged and uncommitted (there are none).
The worker stopped after writing this receipt.
```

## Controller unblock response 2 (S2 structural gaps)

```text
controller_status: blocker_resolved
task: 012 S2
resolved: 2026-07-20
```

## Resolution

```text
root cause:
  Three structural gaps that need edits outside the raw S2 .cpp list. Controller
  independently verified all three:
  B1  EvictionCandidates.h (verbatim from CH) uses C++23 deducing-this
      (this auto&&) + folly::F14FastSet::merge; g++13.3/gnu++20 rejects both.
      Never compiled (Task 011 was structural-only). All five Task-011 .cpp
      include it transitively and die at the same first errors.
      Confirmed: EvictionCandidates.h:35,36 deducing-this; :3 (line 99 in CH)
      .merge. Matches CH verbatim.
  B2a Metadata.cpp:966 reads reserve_space_wait_lock_timeout_milliseconds from
      global Context; design 08:471-473 mandates injecting it from FileCacheConfig
      into CacheMetadata. S1 omitted the field/member. Confirmed CH:964-967 +
      design 08:471-473.
  B2b FileSegment.cpp:800-802 + Metadata.cpp:1263-1268 call
      OpenedFileCache::instance().remove(path,flags) (idempotent) to invalidate
      cached open handles; OpenedFileCache is a Task-013 Manager singleton absent
      in the SCC phase. Confirmed CH call sites + OpenedFileCache.h:110 instance().

decision:
  B1 (Controller-authorized, executes a portability fix, CH semantics unchanged):
     S2 may edit EvictionCandidates.h — deducing-this -> explicit const/non-const
     begin/end overloads; F14FastSet::merge -> insert-range loop.
  B2a (Controller-authorized, executes approved design 08:471-473):
     add reserve_space_wait_lock_timeout_milliseconds to FileCacheConfig
     (FileCacheSettings.h), a CacheMetadata ctor param+member (Metadata.h), and
     the FileCache pass-through (FileCache.h/.cpp wired in S3). downloadImpl reads
     the injected member, not a global Context.
  B2b (USER decision 2026-07-20): do NOT inject a no-op (silent fallback).
     At both call sites add TODO(Task 013) and THROW a not-supported/not-
     implemented exception (VELOX_NYI or a FileCache not-implemented exception)
     in place of OpenedFileCache::remove, so any path reaching opened-handle
     invalidation fails loudly. Task 013 replaces the throw with the real
     Manager-backed invalidation. Relaxes design 08:571 no-stub for THIS item
     only; S4 tests do NOT exercise the rename/remove opened-handle paths.

task update:
  Recorded in the Task-012 amendment "### S2 unblock — three structural
  resolutions (2026-07-20)". S2 file scope expands to include EvictionCandidates.h,
  FileCacheSettings.h, and the Metadata.h/FileSegment.h injection points, plus
  the two throw-not-implemented call sites.

redispatch: yes — S2 with the expanded, enumerated scope.
```

## Worker attempt (S2 redispatch — FileSegment/Metadata + finish Task-011 cpp)

```text
worker_status: blocked
environment_profile: home-chang
task: 012 (sub-attempt S2 redispatch — FileSegment.cpp + Metadata.cpp + finish Task-011 .cpp; per-TU compile-only)
```

S2 (redispatched after the Controller resolved B1/B2a/B2b) delivers `.cpp` source with a per-TU
COMPILE-ONLY check (no link, no test — that is S4). The three structural resolutions were applied
exactly. All authorized deliverables are on disk and compile per-TU EXCEPT `Metadata.cpp`, which is
blocked by ONE remaining structural gap (B3) outside the B1/B2a/B2b authorization: `CacheMetadata`
cannot construct its download/cleanup `ThreadFromGlobalPool` workers without an injected
`FileCacheWorkerPool &`, which the accepted S1 header does not carry and which does not exist in the
SCC phase (it is manager-owned, Task 013). Per protocol worker rule 4 and the unreviewed-scope gate,
S2 does not fabricate a global pool, add an unauthorized member, or force S3 `FileCache.cpp` wiring.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `853840ae5` | clean (0 files); `filecache2...baibaichen/filecache [ahead 3]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | clean |

`HEAD 853840ae5` = "Task 012: Port center-SCC headers (sub-attempt S1)". No pre-existing dirty
files. No staging/commit/amend/rebase/push. Only build-directory probe logs were written under
`<velox_build_dir>` (outside the worktree).

## Files changed

```text
NEW:
  velox/ch/Interpreters/FileCache/FileSegment.cpp            (ported from CH FileSegment.cpp, 1554 lines)
  velox/ch/Interpreters/FileCache/Metadata.cpp              (ported from CH Metadata.cpp, 1425 lines)
  velox/ch/Interpreters/FileCache/FileCacheErrnoException.h (FileCache-owned typed errno exception; getErrno()->int)
MODIFIED (authorized structural fixes):
  velox/ch/Interpreters/FileCache/EvictionCandidates.h      (B1: deducing-this -> const/non-const begin/end; F14FastSet::merge -> insert-range loop)
  velox/ch/Interpreters/FileCache/FileCacheSettings.h       (B2a: reserveSpaceWaitLockTimeoutMilliseconds = 1000; operator== stays = default)
  velox/ch/Interpreters/FileCache/Metadata.h                (B2a: CacheMetadata ctor param + const member reserve_space_wait_lock_timeout_milliseconds)
MODIFIED (finish Task-011 .cpp against real headers):
  velox/ch/Interpreters/FileCache/EvictionCandidates.cpp    (fmt: KeyState enum -> static_cast<int>)
  velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp (D9: CH-form SCOPE_EXIT({...}) -> folly SCOPE_EXIT { ... };)
  velox/ch/Interpreters/FileCache/SplitFileCachePriority.cpp(C++23 std::to_underlying -> static_cast<underlying_type_t>)
NOT MODIFIED:
  CMakeLists.txt  — deliberately left unmodified. Adding Metadata.cpp to the existing
    velox_ch_filecache source list would break that library's build on B3; the Step-15
    velox_ch_filecache_core target also needs FileCache.cpp/QueryLimit.cpp (S3). Registration is
    deferred to S3/S4 once B3 is resolved and the S3 .cpp exist. The S2 checkpoint is per-TU
    compile-only (via the real project flags), not a library/link.
```

## Three structural resolutions applied (B1/B2a/B2b)

```text
B1  EvictionCandidates.h: `auto begin(this auto&& self)` / `auto end(this auto&&)` replaced by
    explicit const + non-const begin/end overloads; `kept_alive_cache_usage.merge(other...)`
    replaced by an insert-range loop that also clears the source (dedup by shared_ptr value; CH
    semantics preserved). This alone made IFileCachePriority.cpp and LRUFileCachePriority.cpp
    compile immediately (they die transitively on this header before B1).
B2a FileCacheConfig gained `reserveSpaceWaitLockTimeoutMilliseconds` (default 1000, matching CH
    `filesystem_cache_reserve_space_wait_lock_timeout_milliseconds`). CacheMetadata gained a ctor
    parameter (default 1000) + a `const size_t reserve_space_wait_lock_timeout_milliseconds` member.
    `downloadImpl` reads the injected MEMBER, never a global Context (there is no velox Context).
B2b At BOTH opened-file-handle invalidation sites the OpenedFileCache::instance().remove calls are
    NOT ported and NOT replaced by a no-op. Each site adds `// TODO(Task 013)` and THROWs `VELOX_NYI`.
    IMPORTANT (from the review): both throws are placed OUTSIDE the surrounding best-effort/fs
    try/catch(...) blocks so the not-implemented error propagates loudly instead of being swallowed:
      - rename site (FileSegment.cpp renameToIncludeSizeInNameUnlocked): the fs::rename stays inside
        the best-effort try; on success a `renamed` flag is set and the VELOX_NYI is raised after the
        try. The mandatory S4 tests do NOT exercise the rename/remove opened-handle paths.
      - removal site (Metadata.cpp removeFileSegmentImpl): the fs::remove stays inside the fs try;
        a flag records that invalidation is required, and the VELOX_NYI is raised after the try
        (before/around the erase).
```

## Other CH-source-authority reconciliations applied

```text
- FileCacheErrnoException (NEW header): FileCache-owned typed exception, `getErrno()->int`. Replaces
  CH `ErrnoException` on the write path. FileSegment::write reconcile: catch FileCacheErrnoException
  -> setDownloadFailed; if file exists: downloaded_size==0 -> fs::remove (CH cleanup branch restored
  per review finding #1), else ENOSPC(28)/EDQUOT(122) -> read physical size, enforce
  downloaded<=physical<=reserved, set downloaded=physical; rethrow ORIGINAL. catch(...) -> mark
  failed + rethrow. NO reconcile-every-exception fallback. The production LocalWriteFile::append
  reports short writes via VELOX_CHECK, not this typed exception, so no production PRODUCER exists yet
  (documented pre-release gap; S4 injects the fault via a production WriteFile that commits a strict
  prefix then throws FileCacheErrnoException).
- Writer: CH `make_unique<WriteBufferFromFile>(path,0,flags)` -> make_shared<WriteBufferFromVeloxWriteFile>
  over velox::LocalWriteFile(path, shouldCreateParentDirectories=false, shouldThrowOnFileAlreadyExists=false,
  bufferIo=true), which seeks to end (append) and creates-if-absent; external-only writer (buffer size 0).
- FileSegment::wait: accepted S1 header takes NO cancellation token, so the 1s-slice/60s-deadline cv
  loop is ported without QueryStatus/CurrentThread (query cancellation is not wired in this MVP).
- getCallerId -> FileCacheQueryIdScope::getCallerId(); toString(FileSegmentKind)/stateToString/keyStateName
  -> explicit switch helpers (no magic_enum); timeInSeconds -> duration_cast<seconds>; OpenTelemetry
  SpanHolder / ProfileEvents / CurrentMetrics / logging -> existing no-op shims.
- removeFileSegmentImpl accesses `file_segment->queue_iterator` DIRECTLY (LockedKey is a friend of
  FileSegment) instead of the public getQueueIterator(); getQueueIterator() re-locks the
  non-recursive FileSegmentGuard we already hold and would self-deadlock (review finding #9 fixed).
- downloadImpl: CH reused an owned Memory<>(DBMS_DEFAULT_BUFFER_SIZE) as the reader's external target;
  the S1 signature is std::optional<CacheBuffer>& and the reader owns a MemoryPool-charged buffer
  (SD9), so the port reads through the reader's OWN buffer (buf->set(nullptr,0)) and leaves `memory`
  unused. Reviewer assessed this as a faithful, behavior-preserving reconciliation.
- getCommonOrigin: CH getKeyMetadata skipped client-access tracking for the common-origin user via a
  STATIC FileCache::getCommonOrigin(); the accepted S1 header makes it an INSTANCE method and
  CacheMetadata has no FileCache back-reference, so the port skips only empty + internal ids and
  documents that common-id filtering moves into the injected on_client_access callback (S3).
- cancel-before-join preserved in CacheMetadata::shutdown (download_queue->cancel() +
  cleanup_queue->cancel() precede all joins).
- §3: KeyMetadata stays ordered std::map; MetadataBucket F14 with no escaping iterator/ref;
  ShardedMap callbacks return by value (getOrCreateSharedOrigin copies the shared_ptr;
  removeSharedOrigins mutates in place); LockedKey member order (key_metadata before lock) preserved.
```

## Commands and outcomes

Per-TU COMPILE-ONLY probes with the project's real flags (extracted from `compile_commands.json`
for the existing `FileCacheSettings.cpp` TU: `/usr/bin/c++` g++ 13.3, `-std=gnu++20`, real
includes/defines), `-fsyntax-only`. No `-j`. No link, no test (correct for S2 by design). No
configure re-run (S1's configure is still valid; the blocker is source/header-structural).

| Command purpose | Exit code | Log |
|---|---:|---|
| compile-only `IFileCachePriority.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2b_IFileCachePriority.log` |
| compile-only `LRUFileCachePriority.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2b_LRUFileCachePriority.log` |
| compile-only `SLRUFileCachePriority.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2b_SLRUFileCachePriority.log` |
| compile-only `SplitFileCachePriority.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2b_SplitFileCachePriority.log` |
| compile-only `EvictionCandidates.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2b_EvictionCandidates.log` |
| compile-only `FileSegment.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2b_FileSegment.log` |
| compile-only `Metadata.cpp` | 1 (B3 only) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2b_Metadata.log` |

## Acceptance evidence

```text
per-TU object compiles: 6 of 7 targeted TUs pass -fsyntax-only under the project's real g++ 13.3 /
  gnu++20 flags (all 5 Task-011 .cpp + FileSegment.cpp). Metadata.cpp fails on EXACTLY ONE
  structural gap (B3), 3 identical errors, all `'worker_pool' was not declared in this scope`
  (Metadata.cpp:989, 991, 1031 in CacheMetadata::startup / setBackgroundDownloadThreads). Nothing
  else in Metadata.cpp errors.
link/test: NOT attempted in S2 by design (SCC has no intermediate link step; FileCache.cpp/
  QueryLimit.cpp are S3; tests + final green are S4). No green link or test is claimed.
git diff --check: n/a for new files; the modified files carry no whitespace errors.
git status --short: exactly the 6 modified + 3 new FileCache files listed above; baseline 853840ae5
  preserved; unstaged/uncommitted.
```

## Worker review

```text
review subagent: one read-only general-purpose reviewer over the S2 diff, given the CH source
  (FileSegment.cpp, Metadata.cpp), the amendment + three resolutions, and the shim/header surface.
  Asked for source-contract fidelity, errno/reconcile, cancel-before-join, B2b throw placement, B2a
  injection, B1 correctness, §3, F14 invariant, over-port, and confirmation that B3 is the sole
  compile blocker.
findings (actionable, all resolved):
  1. [high] B2b VELOX_NYI at BOTH sites was thrown INSIDE a swallowing try/catch(...), so in release
     builds the not-implemented error was logged and suppressed (defeats "fail loudly").
     RESOLVED: both throws moved OUTSIDE the try/catch — rename site guards on a `renamed` flag and
     throws after the best-effort try; removal site records an `opened_handle_invalidation_required`
     flag and throws after the fs try (around the erase). Re-probed: FileSegment.cpp still exit 0;
     Metadata.cpp still only B3.
  2. [med] FileSegment::write dropped CH's `downloaded_size==0 -> fs::remove` cleanup branch on the
     errno path. RESOLVED: restored the full CH three-way structure (exists -> {==0 remove; else
     ENOSPC/EDQUOT reconcile}).
  3. [med] removeFileSegmentImpl called getQueueIterator() which re-locks the non-recursive
     FileSegmentGuard already held -> self-deadlock. RESOLVED: access the friend member
     file_segment->queue_iterator directly (as CH does), no relock.
findings assessed as acceptable / not defects:
  - downloadImpl reader-owned-buffer reconciliation (faithful; SD9 pool-charged buffer); dropping
    CH's internalBuffer().empty() assert (acceptable since no external buffer is installed).
  - getCommonOrigin instance-vs-static reconciliation: acceptable, with common-id filtering deferred
    to the S3-injected callback (flagged for S3 verification).
  - errno catch(Exception&)/catch(fs::filesystem_error&) collapse into catch(...): faithful
    (mark-failed + rethrow original; no blanket reconcile).
  - §3 / F14 no-escaping-reference invariant, state-machine transition order, cancel-before-join: OK.
verdict: safety-critical items #1 (errno reconcile) and #5 (cancel-before-join) satisfied; #2 (B2b
  throw placement) NOW satisfied after moving the throws outside the swallowing try/catch.
unresolved findings: none of the review findings remain unresolved. The ONLY unresolved item is the
  external structural blocker B3 below (needs a Controller/user decision, not a code fix).
```

## Blockers

```text
BLOCKER B3 — CacheMetadata cannot construct its background workers without an injected
FileCacheWorkerPool, which is manager-owned (Task 013) and absent from the accepted S1 headers.

First actionable error (task012_s2b_Metadata.log), Metadata.cpp:989, 991, 1031:
    error: 'worker_pool' was not declared in this scope
in CacheMetadata::startup and CacheMetadata::setBackgroundDownloadThreads, where CH does
    download_threads.back()->thread = std::make_unique<ThreadFromGlobalPool>([this, thread]{ ... });
    cleanup_thread = std::make_unique<ThreadFromGlobalPool>([this]{ cleanupThreadFunc(); });

Root cause (structural, reviewed dependency, missing injection point — parallel to B2a/B2b):
  - `ThreadFromGlobalPool` is aliased to `FileCacheWorker` (velox/ch/Common/ThreadPool.h:96), whose
    ONLY non-default constructor is `FileCacheWorker(FileCacheWorkerPool & pool, Function function)`
    (ThreadPool.h:61). There is no bare-callable constructor.
  - The thread-pool design MANDATES injecting the shared pool at every ThreadFromGlobalPool site:
    `port/1-dependencies/04-filecache-thread-pool-design.md:148-154` —
    "所有原 ThreadFromGlobalPool(lambda) call sites 显式注入 shared pool:
     std::make_unique<ThreadFromGlobalPool>(worker_pool, load_function);"
    and 04:11 "GlobalThreadPool -> manager-owned FileCacheWorkerPool".
  - The accepted S1 `Metadata.h` `CacheMetadata` has NO `FileCacheWorkerPool &` (or `*`) member and
    no ctor parameter for one (grep of Metadata.h shows only download_threads / cleanup_thread /
    download_threads_num). The accepted S1 `FileCache.h` also owns NO `FileCacheWorkerPool` — only a
    `std::unique_ptr<ThreadPool> eviction_pool` (= FileCacheThreadPool) and BackgroundSchedulePool
    task holders, both of which are ALSO injected a FileCacheWorkerPool by the manager. So there is
    no worker pool anywhere in the SCC-phase headers to pass down to CacheMetadata.
  - This is NOT one of the three resolved gaps (B1/B2a/B2b): B2a authorized only the reserve-timeout
    ctor param+member on CacheMetadata; it did not authorize a worker-pool member. Adding one forces
    FileCache (S3) to own/construct a FileCacheWorkerPool and pass it through — i.e. it forces
    FileCache.cpp changes, which the S2 prompt explicitly says to leave as an S3 TODO ("Do NOT edit
    FileCache.h beyond what B2a pass-through needs ... leave the FileCache-side wiring TODO for S3").

Why not resolved in the .cpp: `startup`/`setBackgroundDownloadThreads`/`shutdown` bodies are the only
Metadata.cpp code that needs the pool, and they cannot compile without naming a real
`FileCacheWorkerPool &`. Fabricating a process-global pool singleton, hard-coding a pool, or marking
the workers no-op are all forbidden (unreviewed-dependency / no-fallback / no-silent-stub rules). The
manager-owned pool genuinely does not exist in the SCC phase — this is directly analogous to B2b's
manager-owned OpenedFileCache, which the user resolved by decision.

Exact decision needed from the Controller/user (any one path unblocks a redispatch of S2's Metadata.cpp):
  (a) authorize adding a `FileCacheWorkerPool &` (or `FileCacheWorkerPool *`) member + ctor parameter
      to `CacheMetadata` (Metadata.h), with FileCache passing it through at construction wired in S3
      (FileCache.h/.cpp) — i.e. the exact B2a-style injection, extended to the worker pool; OR
  (b) decide, per a B2b-style relaxation, that in the SCC phase CacheMetadata's background download +
      cleanup workers are deferred to Task 013: e.g. `startup`/`setBackgroundDownloadThreads` throw
      not-implemented (VELOX_NYI) in place of constructing threads, with the S4 tests not exercising
      the background-download / delayed-cleanup worker paths (the "ShutdownJoinsWorkers" and
      "queue pipeline" S4 cases would then need a decision on whether they run against real workers,
      which requires (a)); OR
  (c) provide/confirm an SCC-phase source for the FileCacheWorkerPool (e.g. a fixture-owned pool the
      test injects) and the exact member/parameter shape to add.

Reference anchors: velox/ch/Common/ThreadPool.h:61,96; Metadata.h CacheMetadata (no pool member);
FileCache.h:334 (eviction_pool only); design 04:11,148-154; CH Metadata.cpp:1020-1028
(startup construction), 1049-1096 (setBackgroundDownloadThreads).
```

## Worker declaration

```text
Only Task 012 sub-attempt S2 (redispatch) was attempted.
Two new .cpp (FileSegment.cpp, Metadata.cpp) + one new header (FileCacheErrnoException.h) were
created; three headers (EvictionCandidates.h, FileCacheSettings.h, Metadata.h) and three Task-011
.cpp (EvictionCandidates.cpp, SLRUFileCachePriority.cpp, SplitFileCachePriority.cpp) were modified.
The three structural resolutions (B1/B2a/B2b) were applied exactly; the read-only review's three
actionable findings were all resolved and re-probed. 6 of 7 targeted TUs compile per-TU; Metadata.cpp
is blocked solely by B3 (worker-pool injection, outside the B1/B2a/B2b authorization). CMakeLists was
intentionally not modified (registration deferred to S3/S4 with B3 + the S3 .cpp). Link and test were
not attempted (S4). Changes are unstaged and uncommitted. The worker stopped after writing this
receipt.
```

## Controller unblock response 3 (B3 CacheMetadata worker pool)

```text
controller_status: blocker_resolved
task: 012 S2
resolved: 2026-07-20
```

## Resolution

```text
root cause:
  Velox FileCacheWorker (Task 005) ctor requires FileCacheWorkerPool& (no implicit
  global pool like CH ThreadFromGlobalPool), so CacheMetadata cannot start its
  download/cleanup threads without an injected FileCacheWorkerPool&. S1 headers
  omitted that injection point. Controller verified: CH CacheMetadata ctor
  (Metadata.h:189) takes only background_download_threads_ (count), starts threads
  via make_unique<ThreadFromGlobalPool>(...) at Metadata.cpp:1024,1027 — implicit
  global pool. Velox FileCacheWorker ctor = (FileCacheWorkerPool&, Function)
  (ThreadPool.h:61).

decision (Controller-authorized; executes approved design
  1-dependencies/04-filecache-thread-pool-design.md:11,45-48,142):
  The single shared FileCacheWorkerPool is manager/FileCache-owned (NOT
  CacheMetadata-owned). SCC phase: add a FileCacheWorkerPool& ctor parameter +
  reference member to CacheMetadata (Metadata.h); CacheMetadata::startup binds its
  download/cleanup FileCacheWorkers to it; FileCache owns the pool and passes it in
  (FileCache.h/.cpp, wired in S3). Task 013 later moves pool ownership to the
  Manager and injects it the same way (no CacheMetadata signature change at 013).

work preservation:
  The S2-redispatch working tree is PRESERVED (not reverted): 6 modified + 3 new
  files already implement B1/B2a/B2b, FileSegment.cpp, FileCacheErrnoException.h,
  and all five finished Task-011 .cpp (6 of 7 TUs compile-only clean). The next
  worker continues on this working tree and only needs to add the B3 injection so
  Metadata.cpp compiles.

redispatch: yes — S2 finish, B3 injection only, on the existing working tree.
```

## Worker attempt (S2 finish — B3 worker pool injection)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 012
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `853840ae52ca70f17eb9669f237b8fe5eed3d017` | dirty (prior S2 work preserved) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `eb500d41e2a2a0e99736d51bc7c9ae5597a3a1b3` | receipt append only |

Preserved pre-existing working-tree changes (not reverted): EvictionCandidates.{h,cpp},
FileCacheSettings.h, Metadata.h, SLRUFileCachePriority.cpp, SplitFileCachePriority.cpp,
FileCacheErrnoException.h, FileSegment.cpp, Metadata.cpp.

## Files changed (this attempt — B3 only)

```text
velox/ch/Interpreters/FileCache/Metadata.h
velox/ch/Interpreters/FileCache/Metadata.cpp
```

## B3 edit

CH `CacheMetadata` starts its download/cleanup threads with `ThreadFromGlobalPool`
(implicit global pool). In Velox `ThreadFromGlobalPool` = `FileCacheWorker`, whose
ctor is `FileCacheWorker(FileCacheWorkerPool &, Function)` (Task 005,
`velox/ch/Common/ThreadPool.h`), so `CacheMetadata` needs an explicit
`FileCacheWorkerPool &`. Authorized by Task 012 amendment "S2 unblock 2 — B3" and
approved design `1-dependencies/04-filecache-thread-pool-design.md:11,45-48,142`.

- `Metadata.h`: added a `FileCacheWorkerPool & worker_pool_` ctor parameter to
  `CacheMetadata` (inserted before the trailing defaulted
  `reserve_space_wait_lock_timeout_milliseconds_`), and a
  `FileCacheWorkerPool & worker_pool;` reference member placed right after
  `write_cache_per_user_directory` (matching member-init order, no `-Wreorder`).
- `Metadata.cpp`: added the matching ctor parameter and the
  `, worker_pool(worker_pool_)` member initializer.
- `startup` and `setBackgroundDownloadThreads` already bound their
  `FileCacheWorker`s to `worker_pool` (mirroring CH `Metadata.cpp:1024,1027`) and
  now compile against the new member. `shutdown` cancel-before-join
  (`Metadata.cpp:998-1007`) is unchanged.
- `CacheMetadata` does NOT own the pool (reference member, not value/unique_ptr).
  The `FileCache`-side ownership + pass-through at construction is S3; no
  `FileCache.cpp` was written. Metadata.h already includes `ThreadPool.h`, which
  forward-declares `FileCacheWorkerPool`, so no new forward decl was needed.

## Commands and outcomes (per-TU COMPILE-ONLY, `-fsyntax-only`, no link, no `-j`)

Flags derived from `compile_commands.json` (`FileCacheKey.cpp` entry), `-o`/`-c`
stripped, `-fsyntax-only -c <src>` appended. Run from
`/home/chang/OpenSource/velox/cmake-build-debug-gcc13`.

| TU | Exit code | Log |
|---|---:|---|
| `EvictionCandidates.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2c_EvictionCandidates.log` |
| `FileSegment.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2c_FileSegment.log` |
| `Metadata.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2c_Metadata.log` |
| `IFileCachePriority.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2c_IFileCachePriority.log` |
| `LRUFileCachePriority.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2c_LRUFileCachePriority.log` |
| `SLRUFileCachePriority.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2c_SLRUFileCachePriority.log` |
| `SplitFileCachePriority.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s2c_SplitFileCachePriority.log` |

All 7 target TUs compile-only clean (exit 0). `Metadata.cpp` (the B3 blocker) now
passes: `grep -c "error:" task012_s2c_Metadata.log` = 0, no `-Wreorder`/"will be
initialized after" warning. Link/test NOT attempted by design — the full
library/test link is S4, and `FileCache.cpp`/`QueryLimit.cpp` are S3.

## Worker review

```text
review subagent: general-purpose, read-only, B3 delta (Metadata.h/.cpp) + design anchors
findings: none — injection correct; member-init order matches declaration order (no -Wreorder);
          worker_pool is a reference member (CacheMetadata does not own the pool);
          shutdown cancel-before-join intact; no over-port; only authorized dependency FileCacheWorkerPool.
resolutions: none required
unresolved findings: none
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 012 (S2 finish — B3) was attempted.
Changes are unstaged and uncommitted; all prior working-tree changes preserved.
The worker stopped after writing this receipt.
```

## Controller review (S2 finish, accepted)

```text
controller_status: accepted (sub-attempt S2; stage gate remains S4)
environment_profile: home-chang
task: 012 S2
reviewed: 2026-07-20
```

## Review evidence (S2)

```text
scope: 6 modified + 3 new files (EvictionCandidates.{h,cpp}, FileCacheSettings.h,
  Metadata.h, SLRU/Split .cpp; new FileSegment.cpp, Metadata.cpp,
  FileCacheErrnoException.h). git diff --check clean.

compile-only (independently verified logs): all 7 target TUs -fsyntax-only exit 0
  (task012_s2c_*.log), incl. Metadata.cpp 0 errors. No link/test claimed (SCC has
  no intermediate link step; green gate is S4). FileCache.cpp/QueryLimit.cpp are S3.

source-contract fidelity (independent Controller review subagent) — CLEAN, 8/8:
  1. errno reconcile: FileSegment::write catches FileCacheErrnoException first,
     reconciles physical<=... only on ENOSPC/EDQUOT, `throw;` rethrows the ORIGINAL
     (not wrapped), generic catch(...) marks failed + rethrows, no reconcile-every-
     exception fallback; getErrno()->int noexcept. Faithful to CH:501-539.
  2. B2b throw (CRITICAL, prior-round swallow bug fixed): rename site
     (FileSegment.cpp:723-727) and removal site (Metadata.cpp:1250-1253) raise
     VELOX_NYI OUTSIDE the best-effort try/catch — not swallowed; OpenedFileCache
     not ported; no no-op.
  3. B2a: downloadImpl reads injected reserve_space_wait_lock_timeout_milliseconds
     member (Metadata.cpp:935 / Metadata.h:304); no residual global Context.
  4. B3: CacheMetadata ctor takes FileCacheWorkerPool& (Metadata.h:229), reference
     member (301, not owned); startup binds workers to it.
  5. cancel-before-join: shutdown cancels queues (Metadata.cpp:1000-1001) then
     joins (1003-1009). Matches CH:1032-1042.
  6. §3: KeyMetadata private std::map; MetadataBucket F14 no-escape; LockedKey
     member order key_metadata-before-lock; ShardedMap withShard copy-out.
  7. B1: deducing-this -> const/non-const begin/end; F14FastSet::merge -> insert-
     range loop preserving shared_ptr dedup.
  8. over/under-port: no OpenedFileCache singleton; no overcommit/cloud/system-table.

advisory (not a defect, forwarded to Task 013): at the removal site the port
  erases (Metadata.cpp:1249-1250) before the VELOX_NYI, whereas CH erases at the
  final return. Harmless in the SCC phase (the throw aborts anyway); Task 013 must
  keep erase + opened-handle invalidation coupled when it replaces the throw with
  real Manager-backed invalidation.

unresolved findings: None for S2.
```

## Commits (S2)

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `dd7eaf43f Task 012: Port FileSegment/Metadata + finish priority .cpp (sub-attempt S2)` |

## Worker attempt (S3 FileCache/QueryLimit)

```text
worker_status: blocked
environment_profile: home-chang
task: 012 (sub-attempt S3 — FileCache.cpp + QueryLimit.cpp; per-TU COMPILE-ONLY, no link/test by design)
```

S3 was to CREATE `FileCache.cpp` and `QueryLimit.cpp` with a per-TU COMPILE-ONLY check
(no link, no test — that is S4), MAY minimally MODIFY `FileCache.h`/`QueryLimit.h`/
`CMakeLists.txt` to complete the S2-deferred FileCache-side wiring (own the
`FileCacheWorkerPool`, pass it + the reserve timeout into `CacheMetadata`, resolve
S2 `TODO(S3)` markers). `QueryLimit.cpp` is delivered and compiles per-TU clean.
`FileCache.cpp` is BLOCKED on structural gaps that require edits OUTSIDE the S3 file
scope and/or a construction-time dependency-injection decision (commonUserId,
scheduler/timekeeper ownership) — parallel to the S2 B2a/B3 injection-point gaps.
Per protocol worker rule 4 and the unreviewed-dependency / no-fallback rules, S3 does
not guess a mapping, add a no-op/stub shim for an unreviewed dependency, invent a
commonUserId source, or silently change the public `FileCache(cache_name, settings)`
constructor.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `dd7eaf43f` | clean (0 files); `filecache2...baibaichen/filecache [ahead 4]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | clean (receipt append only) |

`HEAD dd7eaf43f` = "Task 012: Port FileSegment/Metadata + finish priority .cpp
(sub-attempt S2)". No pre-existing dirty files. No staging/commit/amend/rebase/push.
Only build-directory probe logs were written under `<velox_build_dir>` (outside the
worktree). The single new source `QueryLimit.cpp` is untracked; nothing else changed.

## Files changed

```text
NEW (delivered, compiles per-TU clean):
  velox/ch/Interpreters/FileCache/QueryLimit.cpp   (ported from CH QueryLimit.cpp, ~185 lines)
NOT written (blocked):
  velox/ch/Interpreters/FileCache/FileCache.cpp    (CH FileCache.cpp = 3248 lines; see Blockers)
  FileCache.h wiring (own pool/scheduler/commonUserId) — depends on the B5 decision
  CMakeLists.txt registration — deferred to S4 with FileCache.cpp present
```

## QueryLimit.cpp — contract fidelity + compile evidence

```text
CH QueryLimit.cpp (170 lines) ported faithfully. Reviewed mappings applied exactly:
  - CurrentThread query-init  -> FileCacheQueryIdScope::currentQueryId() (non-empty == initialized).
  - FilesystemCacheSettings   -> FileCacheReadOptions (maxDownloadSizePerQuery,
                                 !skipDownloadIfExceedsPerQueryCacheWriteLimit — one-hot negation preserved).
  - throw Exception(LOGICAL_ERROR, fmt, ...) -> throwFileCacheException(fmt, ..., key.toString()).
  - std::unordered_map -> folly::F14FastMap (in the accepted S1 header).
TOCTOU discipline preserved byte-for-byte: removeQueryContext resets this holder's
  reference UNDER query_map_mutex before the use_count()==1 decision; doomed context is
  moved out and destroyed by ~QueryContextHolder AFTER the cache write lock scope ends.
Per-TU compile: exit 0, 0 errors (only the expected TSA -Wattributes / -Wchanges-meaning
  warnings shared by every FileCache TU under g++ 13.3 / gnu++20).
```

## Commands and outcomes (per-TU COMPILE-ONLY, `-fsyntax-only`, no link, no `-j`)

Flags extracted from `compile_commands.json` (the existing `FileCacheKey.cpp` TU:
`/usr/bin/c++` g++ 13.3, `-std=gnu++20`, real project includes/defines), with
`-c`/`-o` stripped and `-fsyntax-only -c <src>` appended. Saved to
`<velox_build_dir>/task012_flags.txt`. No configure re-run (S2's configure is still
valid; the blocker is source/header-structural, not a configure failure).

| Command purpose | Exit code | Log |
|---|---:|---|
| compile-only `QueryLimit.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s3_QueryLimit.log` |
| gap probe: DimensionalMetrics/HistogramMetrics/CurrentMetrics::get/FailPointInjection absence | 1 (expected) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s3_gap_probe.log` |

## Acceptance evidence

```text
per-TU object compiles: QueryLimit.cpp passes -fsyntax-only (exit 0, 0 errors).
  FileCache.cpp NOT written -> blocked before compile (structural gaps below).
link/test: NOT attempted in S3 by design (SCC has no intermediate link step; the green
  compile+link+test gate is S4). No green link or test is claimed.
git diff --check: n/a (QueryLimit.cpp is new/untracked). git status --short shows exactly
  one new file (QueryLimit.cpp); baseline dd7eaf43f preserved; unstaged/uncommitted.
```

## Contract-derivation done before stopping (FileCache.cpp)

```text
Full CH FileCache.cpp (3248 lines) + FileCache.h read and contract-derived against the
accepted S1/S2 velox headers. A complete CH-dependency -> velox map was built. Of the
FileCache.cpp dependency surface, these are RESOLVABLE in the S3 scope with already-
reviewed mappings (would be applied in the redispatch, NOT blockers):
  - ConcurrentBoundedQueue<T> -> FileCacheBoundedQueue<T> (infra matrix MPMC-queue mapping;
    push/tryPush(timeoutMs)/pop/tryPop/finish all present and API-compatible).
  - randomSeed()/pcg64_fast -> folly::Random (D-011-2, as in FileCacheKey.cpp/LRU .cpp).
  - WriteBufferFromOwnString (Stat::toString, on_cannot_evict message) -> fmt assembly
    (D-011-4, as in EvictionCandidates.cpp).
  - parse<UInt64>/tryParse<UInt64> (loadMetadata name parsing) -> std::from_chars / folly::tryTo.
  - getKeyTypePrefix / OriginInfo / FileSegmentKeyType -> existing velox leaf types.
  - Stopwatch (D11) at freeSpaceRatioKeepingThreadFunc/backgroundCleanupTaskFunc: single
    end-of-scope wall snapshot via DeltaCpuWallTimeStopWatch (elapsed().wallNanos/1e6);
    drop CH `.stop()` (only feeds a no-op ProfileEvents::increment) — within D11's
    call-site-limited surface.
  - callOnce/OnceFlag -> std::call_once/std::once_flag (D8; header already declares it).
  - ProfileEvents::increment / CurrentMetrics::Increment/add/sub -> existing no-op shims
    (all enumerators FileCache.cpp needs on the non-metric path are present).
  - eviction_pool: CH ctor takes CurrentMetrics thread enumerators; velox `ThreadPool`
    = FileCacheThreadPool ctor is (workerPool, maxThreads, queueSize) — MUST use the velox
    ctor anyway, which ELIMINATES the CurrentMetrics::FilesystemCacheEvictionThreads/
    Active/Scheduled enumerators and CurrentMetrics::get from the port entirely.
  - Prometheus DimensionalMetrics/HistogramMetrics registration block + the per-user
    metric block in onSegmentEvicted are in the task's Explicit Exclusions
    ("Prometheus/custom metrics (keep no-op shims)"); dropping them removes the only
    references to those (nonexistent) shims. onSegmentEvicted keeps the two no-op
    ProfileEvents::increment calls.
  - fiu_fail / fiu_do_on / FailPointInjection::pauseFailPoint (debug-only test hooks):
    map to no-op at the call site (FailPoint.h intent; the mandatory S4 tests do not
    exercise these injection points), consistent with "metrics/debug no-op shims".
```

## Blockers

```text
S3 is blocked on FileCache.cpp by TWO structural gaps that need edits/decisions beyond the
explicit S3 authorization (which named only: own the FileCacheWorkerPool, pass pool ref +
reserve timeout into CacheMetadata, resolve S2 TODO(S3) markers). Both are reviewed
DEPENDENCIES whose SCC-phase INJECTION POINT is missing — directly parallel to the S2
B2a (reserve timeout) and B3 (worker pool) gaps the controller already resolved. Neither is
an unreviewed "closest API" guess, and neither may be stubbed/no-op'd or worked around by
silently changing the public FileCache constructor.

BLOCKER B5 — commonUserId injection for FileCache::getCommonOrigin (no SCC-phase source).
  CH getCommonOrigin() is a STATIC method whose user id comes from
  Context::getGlobalContextInstance()->getFilesystemCacheUser() or ServerUUID
  (FileCache.cpp:138-143, 427-431). Velox has no Context and no ServerUUID. The accepted S1
  header already changed getCommonOrigin() to an INSTANCE method returning
  `const FileCacheOriginInfo &` with a documented "host-injected stable commonUserId"
  (FileCache.h:151-153), and the reviewed core design mandates host/manager injection
  (10-filecache-core-files-design.md "manager-injected runtime dependencies" +
  "common/internal origin": commonUserId must be non-empty, stable across restarts,
  != "internal", shared by all caches, provided by FileCacheManager::Options). BUT:
    - FileCache has NO commonUserId member and the constructor is the CH-fixed public apex
      `FileCache(const std::string & cache_name, const FileCacheSettings & settings)`
      (FileCache.h:136) with no injection point.
    - There is no manager (Task 013) in the SCC phase and no velox Context/ServerUUID.
  So getCommonOrigin() cannot be DEFINED in FileCache.cpp without either (i) adding a
  commonUserId ctor parameter + member to FileCache (changes the public constructor
  signature — an architecture decision), or (ii) hard-coding a literal user id
  (forbidden fallback, and it would violate the design's stability/host-provided contract).
  getInternalOrigin() (static, returns OriginInfo("internal")) is trivial and NOT blocked.

BLOCKER B6 — background scheduler + timekeeper + worker-pool OWNERSHIP wiring in FileCache.
  FileCache.cpp creates its two scheduled tasks via
  Context::getGlobalContextInstance()->getSchedulePool().createTask(...) (FileCache.cpp:559,
  591) and starts load_metadata_main_thread / idle-eviction / eviction_pool workers on the
  global pool. In velox these map to (reviewed): BackgroundSchedulePool -> FileCacheScheduler
  (createTask(name, callback)); ThreadFromGlobalPool -> FileCacheWorker(FileCacheWorkerPool&,
  Function); ThreadPool -> FileCacheThreadPool(FileCacheWorkerPool&, maxThreads, queueSize)
  (design 10 + 04 + 05). The S2/B3 resolution already authorized FileCache to OWN the single
  FileCacheWorkerPool and pass it by reference into CacheMetadata. To also create scheduled
  tasks and free-space/idle-eviction/load-metadata workers, FileCache must, in the SCC phase,
  additionally OWN a FileCacheScheduler and its std::shared_ptr<folly::Timekeeper> (the
  scheduler ctor is FileCacheScheduler(shared_ptr<Timekeeper>, FileCacheWorkerPool&)), and
  construct load_metadata_main_thread / idle-eviction threads / eviction_pool bound to the
  owned worker pool. The accepted S1 FileCache.h declares the holders
  (BackgroundSchedulePoolTaskHolder keep_up_free_space_ratio_task / background_cleanup_task,
  std::unique_ptr<ThreadPool> eviction_pool) but owns NO FileCacheWorkerPool, NO
  FileCacheScheduler, and NO Timekeeper. Adding these members + wiring is exactly the B3-style
  "manager-owned resource, owned by FileCache in the SCC phase" extension, but it was NOT
  explicitly enumerated in the S3 prompt (which named only the worker pool + CacheMetadata
  pass-through), and it changes how the metadata ctor and eviction_pool are constructed. This
  needs the same controller authorization the pool got (B3), extended to the scheduler +
  timekeeper, plus confirmation that FileCache owns them in the SCC phase (Task 013 moves
  ownership to the Manager, same injection shape).

Exact decision needed from the Controller/user (both unblock a FileCache.cpp redispatch):
  1. B5: authorize adding a `commonUserId` (stable, non-empty, != "internal") injection to
     FileCache for the SCC phase — either (a) a new ctor parameter + `const String
     common_user_id` member on FileCache (getCommonOrigin() derives its OriginInfo from it;
     Task 013 later passes FileCacheManager::Options.commonUserId the same way), OR
     (b) confirm an SCC-phase source/shape the S4 test fixture provides. Do NOT accept a
     hard-coded literal (violates the design's host-provided/stability contract).
  2. B6: authorize FileCache to OWN, in the SCC phase, the FileCacheWorkerPool (already B3),
     a FileCacheScheduler, and a std::shared_ptr<folly::Timekeeper>, and to construct its
     scheduled tasks via FileCacheScheduler::createTask and its background workers
     (load_metadata_main_thread, idle-client eviction threads, eviction_pool =
     FileCacheThreadPool) bound to the owned pool — the B3 pattern extended to the scheduler
     + timekeeper. Confirm the metadata ctor is constructed with the owned pool ref + the
     FileCacheConfig reserveSpaceWaitLockTimeoutMilliseconds.
  Once B5/B6 are authorized, the remaining FileCache.cpp port is mechanical: apply the
  reviewed mappings listed under "Contract-derivation done before stopping" (queue,
  randomSeed, fmt, parse, Stopwatch call-site, callOnce, no-op metrics/failpoints, velox
  eviction_pool ctor), and register FileCache.cpp/QueryLimit.cpp/the S2 .cpp in
  CMakeLists.txt for S4.

First actionable evidence:
  - QueryLimit.cpp compiles: task012_s3_QueryLimit.log (exit 0, 0 errors).
  - Missing shims/injection points: task012_s3_gap_probe.log shows
    DimensionalMetrics/HistogramMetrics/CurrentMetrics::get/FailPointInjection "has not
    been declared" (the metric/failpoint ones are resolved by dropping the excluded
    Prometheus block + using the velox eviction_pool ctor; they are recorded here only to
    document that the shim edits are NOT needed once the excluded metrics are dropped).
  - B5 anchors: CH FileCache.cpp:138-143,427-431; velox FileCache.h:151-153,136; design
    10 "manager-injected runtime dependencies" / "common/internal origin".
  - B6 anchors: CH FileCache.cpp:559,591 (createTask), 534 (load_metadata_main_thread),
    583-593 (eviction_pool), 2666-2685 (shutdown ordering); velox FileCacheScheduler.h
    ctor + createTask; ThreadPool.h FileCacheThreadPool/FileCacheWorkerPool ctors;
    velox FileCache.h:326-338 (holders present, pool/scheduler/timekeeper absent);
    design 04:11,45-48,142 + 10 "shared worker pool sizing"/"manager-injected".
```

## Worker review

```text
review subagent: one read-only general-purpose reviewer over the ONLY completed in-scope
  diff (QueryLimit.cpp), given the CH QueryLimit.cpp, the accepted S1 QueryLimit.h, and the
  reviewed mappings (FileCacheQueryIdScope, FileCacheReadOptions field names, F14 maps,
  throwFileCacheException). FileCache.cpp produced no diff (blocked before it could be
  written faithfully), so there was nothing else in-scope to review.
findings: none. Reviewer confirmed all 5 critical correctness points (removeQueryContext
  TOCTOU ordering, ~QueryContextHolder destroy-after-lock, getOrSetQueryContext one-hot
  negation, QueryContext::add duplicate handling, tryGetQueryContext under mutex) and that
  every symbol resolves (throwFileCacheException fmt-style, FileCacheKey::toString,
  LRUFileCachePriority(QueueType,max,elems) ctor, IFileCachePriority::add 5-arg overload).
resolutions: none required.
unresolved findings: none from the review. The only unresolved items are the external
  structural blockers B5/B6 (need a Controller/user decision, not a code fix).
```

## Worker declaration

```text
Only Task 012 sub-attempt S3 was attempted.
Exactly one new file (QueryLimit.cpp) was created and compiles per-TU clean; no other file
was created or modified; CMakeLists was not touched (S4). FileCache.cpp was not written:
S3 stopped blocked on B5 (commonUserId injection) and B6 (scheduler/timekeeper/worker-pool
ownership wiring), both manager-injected dependencies whose SCC-phase injection point is
missing from the accepted headers and whose resolution changes the public FileCache
constructor / ownership — beyond the explicit S3 authorization, parallel to S2 B2a/B3.
Link and test were not attempted (S4). Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```

## Controller unblock response 4 (S3 B5/B6 FileCache injection)

```text
controller_status: blocker_resolved
task: 012 S3
resolved: 2026-07-20
```

## Resolution

```text
root cause:
  QueryLimit.cpp done (compile-only clean, review 0 findings). FileCache.cpp
  blocked on two "CH reads global Context, Velox injects from host/Manager" gaps
  (same pattern as accepted B2a/B3), both already fixed by approved design:
  B5 commonUserId: CH getCommonUserID reads Context->getFilesystemCacheUser() /
     ServerUUID (FileCache.cpp:138-141). Design 10:43,138,164,193 -> manager-
     injected stable commonUserId; no ServerUUID in Velox.
  B6 scheduler: CH creates scheduled tasks via Context->getSchedulePool()
     .createTask (FileCache.cpp:559,591). Design 05:21,162-173 -> FileCacheScheduler
     (shared_ptr<folly::Timekeeper>, ...) replaces BackgroundSchedulePool.

decision (Controller-authorized; executes approved design 10 + 05):
  SCC-phase FileCache is extended to receive/establish the manager-injected deps
  without a global Context: hold a stable commonUserId (B5, std::string) used by
  getCommonOrigin/getCommonUserID; own a folly::Timekeeper + FileCacheScheduler
  (B6) for createTask; own the FileCacheWorkerPool (B3) and pass it + the reserve
  timeout (B2a) into CacheMetadata. Ownership stays FileCache/Manager (design 04/10);
  Task 013 moves these to the Manager, injecting the same way (no CacheMetadata
  signature change). Do NOT read a global Context; do NOT invent a ServerUUID.

work preservation:
  Working tree PRESERVED: QueryLimit.cpp (new, compile-only clean) stays. The next
  worker continues on this working tree and adds FileCache.cpp + the FileCache.h
  injection wiring.

redispatch: yes — S3 finish (FileCache.cpp + FileCache.h wiring), B5/B6 authorized.
```

## Worker attempt (S3 finish 3 — FileCache.cpp incremental)

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 012 (sub-attempt S3 finish — FileCache.cpp + QueryLimit.cpp; per-TU COMPILE-ONLY, no link/test by design)
```

S3 finish delivers `FileCache.cpp` (new, ported from CH `FileCache.cpp`, 3248 lines)
on the preserved S3 working tree (`QueryLimit.cpp` already present and compile-only
clean from the prior S3 attempt). The file was authored INCREMENTALLY in small
batches to avoid the resource limit that stopped the two prior S3-finish workers.
Both center-SCC TUs owned by S3 (`FileCache.cpp`, `QueryLimit.cpp`) now compile
per-TU (`-fsyntax-only`, real project flags, no `-j`). No link and no test were
attempted (the SCC has no intermediate link step; the green compile+link+test gate
is S4). No FileCache.h edit was needed — the accepted B5/B6/B3 wiring
(`common_user_id`, owned `worker_pool`/`timekeeper`/`scheduler`, extended ctor) was
already present, and `FileCache.cpp` matches it exactly.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `dd7eaf43f` | dirty (S3 work preserved): ` M FileCache.h`, `?? QueryLimit.cpp` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` | receipt append only |

`HEAD dd7eaf43f` = "Task 012: Port FileSegment/Metadata + finish priority .cpp
(sub-attempt S2)". Preserved pre-existing working-tree changes (NOT reverted):
` M FileCache.h` (B5/B6/B3 injection wiring), `?? QueryLimit.cpp` (compile-only clean).
No staging/commit/amend/rebase/push.

## Files changed (this attempt)

```text
NEW (delivered, compiles per-TU clean):
  velox/ch/Interpreters/FileCache/FileCache.cpp    (ported from CH FileCache.cpp, 3248 lines)
MODIFIED (source registration for compilation; no link claimed):
  velox/ch/Interpreters/FileCache/CMakeLists.txt   (added the 9 center-SCC .cpp to the
                                                    existing velox_ch_filecache velox_sources
                                                    list: IFileCachePriority/LRU/SLRU/Split/
                                                    EvictionCandidates/FileSegment/Metadata/
                                                    FileCache/QueryLimit .cpp. The dedicated
                                                    velox_ch_filecache_core library + SCC test
                                                    target remain S4.)
PRESERVED (not modified this attempt):
  velox/ch/Interpreters/FileCache/FileCache.h      ( M, prior B5/B6/B3 wiring — no edit needed)
  velox/ch/Interpreters/FileCache/QueryLimit.cpp   (?? prior S3, compile-only clean)
```

## Applied deviations (all pre-authorized; recorded)

```text
B5  getCommonOrigin() returns the injected `common_origin` member (built from `common_user_id`
    in the ctor). No global Context, no ServerUUID, no getCommonUserID(). getInternalOrigin()
    stays static "internal".
B6  background_cleanup_task / keep_up_free_space_ratio_task created via the owned
    `scheduler.createTask(name, cb)` (FileCacheScheduler over an owned folly::ThreadWheelTimekeeper),
    not Context::getGlobalContextInstance()->getSchedulePool(). Task names include the cache name
    ("FileCache:<name>:background-cleanup", "FileCache:<name>:free-space").
B3/B2a  `metadata` constructed in the ctor init-list with the owned `worker_pool` (by ref) +
    `settings.reserveSpaceWaitLockTimeoutMilliseconds`. worker_pool/timekeeper/scheduler are
    FileCache-owned and declared before `metadata` (no -Wreorder).
R2  applySettingsIfPossible decides each field by value comparison `new != actual`
    (matching CH FileCache.cpp:2800), not `.changed` tracking.
R7  Overcommit policies (LRU_OVERCOMMIT/SLRU_OVERCOMMIT) rejected explicitly (throwFileCacheException);
    reserve "cannot evict enough space" returns false, only evict()/candidate-evict failure throws.
Three-phase shutdown  deactivateBackgroundOperations order matches CH:2666-2685
    (shutdown.store -> stop_loading_metadata + join load_metadata_main_thread -> deactivate the two
    scheduled tasks -> eviction_pool->wait() -> metadata.shutdown()).
Mechanical mappings  ConcurrentBoundedQueue -> FileCacheBoundedQueue (load-metadata + free-space
    pipelines); randomSeed()/pcg64_fast -> folly::Random (CheckCacheProbability::doCheck);
    WriteBufferFromOwnString -> fmt (Stat::toString, on_cannot_evict message);
    parse<UInt64>/tryParse<UInt64> -> std::from_chars (tryParseUInt64/parseUInt64, full-string match);
    Stopwatch (D11) -> DeltaCpuWallTimeStopWatch, single elapsed().wallNanos/1'000'000 snapshot,
    no .stop()/.reset()/.restart() (freeSpaceRatioKeepingThreadFunc, backgroundCleanupTaskFunc);
    callOnce/OnceFlag (D8) -> std::call_once/std::once_flag (initialize);
    ThreadFromGlobalPool(lambda) -> FileCacheWorker(worker_pool, lambda) (load_metadata_main_thread,
    listing/loading threads, idle-client eviction threads); ThreadPool eviction_pool ->
    FileCacheThreadPool(worker_pool, max_threads, queue_size) (no CurrentMetrics enumerators);
    StatusFile(path, StatusFile::writeFullInfo()); FailPointInjection / fiu_* -> dropped (no-op);
    ProfileEvents / CurrentMetrics / logging -> existing no-op shims;
    DROPPED (Explicit Exclusions): Prometheus DimensionalMetrics/HistogramMetrics registration +
    per-user eviction metric blocks; onSegmentEvicted is a no-op; overcommit priority impls.
```

## Commands and outcomes (per-TU COMPILE-ONLY, `-fsyntax-only`, no link, no `-j`)

Flags extracted from `compile_commands.json` (the existing `FileCacheKey.cpp` TU:
`/usr/bin/c++` g++ 13.3, `-std=gnu++20`, real project includes/defines), with `-c`/`-o`
stripped and `-fsyntax-only -c <src>` appended. Run from `<velox_build_dir>`.

| TU | Exit code | Log |
|---|---:|---|
| `FileCache.cpp`  | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s3d_FileCache.log` |
| `QueryLimit.cpp` | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s3d_QueryLimit.log` |

Both TUs pass `-fsyntax-only` (exit 0, `grep -c error:` = 0). The only warnings are the
expected benign TSA `-Wattributes` ("guarded_by"/"requires_capability" attribute ignored
by GCC) and the pre-existing `-Wchanges-meaning` from `SplitFileCachePriority.h` — the same
warnings every FileCache TU emits under g++ 13.3 / gnu++20 (documented in S1/S2). No
`-Wreorder` / "will be initialized after". Link/test NOT attempted by design (S4).

## Acceptance evidence

```text
per-TU object compiles: 2 of 2 S3 TUs pass -fsyntax-only under the project's real g++ 13.3 /
  gnu++20 flags (FileCache.cpp, QueryLimit.cpp). 0 errors each.
link/test: NOT attempted in S3 by design. No green link or test is claimed.
git diff --check: clean (no whitespace errors).
git status --short: ` M CMakeLists.txt`, ` M FileCache.h` (preserved), `?? FileCache.cpp`,
  `?? QueryLimit.cpp` (preserved). Baseline dd7eaf43f preserved; unstaged/uncommitted.
```

## Worker review

```text
review subagent: one read-only general-purpose reviewer over FileCache.cpp (+ FileCache.h for
  member/ctor names), given CH FileCache.cpp and the amendment (approved deviations, S2/S3 unblocks,
  Explicit Exclusions). Asked for R2 value-comparison, R7, three-phase shutdown, B5/B6/B3 wiring,
  no global Context/ServerUUID, dropped Prometheus/overcommit, core-algorithm fidelity (getImpl/
  getOrSet/get/getDownloadedContiguousOrEmpty/doTryReserve/doEviction/loadMetadata*/freeSpaceRatioImpl/
  doDynamicResizeImpl), D11 Stopwatch, from_chars parsing, and over/under-port.
findings: 12/12 PASS. No actionable defects in FileCache.cpp/FileCache.h; the port is a faithful
  behavioral reproduction of CH within the approved deviation set.
  Non-blocking observations (not defects): (1) `running_removers` is incremented AFTER
  scheduleOrThrowOnError, matching CH:1794 (the Step-14 "increment before submit" text is
  illustrative; CH source is authoritative, so the port follows CH). (2) FileSegmentInfo.h
  download_finished_time type is a different-file item (S1-owned), outside S3 scope.
resolutions: none required (no actionable findings).
unresolved findings: none.
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 012 sub-attempt S3 (finish 3 — FileCache.cpp incremental) was attempted.
One new file (FileCache.cpp) was created and compiles per-TU clean; CMakeLists.txt was modified
to register the 9 center-SCC .cpp for compilation (no link claimed; the dedicated
velox_ch_filecache_core library + SCC test target remain S4). FileCache.h was NOT edited (the
prior B5/B6/B3 wiring already matched); QueryLimit.cpp was preserved. Link and test were not
attempted (S4). Changes are unstaged and uncommitted; all prior working-tree changes preserved.
The worker stopped after writing this receipt.
```

## Controller review (S3 FileCache/QueryLimit, accepted)

```text
controller_status: accepted (sub-attempt S3; stage gate remains S4)
environment_profile: home-chang
task: 012 S3
reviewed: 2026-07-20
```

## Review evidence (S3)

```text
note: FileCache.cpp was authored incrementally (two prior workers hit an output/
  resource limit writing ~3248 lines in one shot; a third wrote it in batches).
  This is a mechanical authoring strategy only; the content is a faithful port.

scope: new FileCache.cpp (2643 lines, complete), new QueryLimit.cpp; modified
  FileCache.h (B5/B6/B3 injection wiring), CMakeLists.txt (source list). diff
  --check clean.

compile-only (independently verified): FileCache.cpp + QueryLimit.cpp -fsyntax-only
  exit 0, 0 errors (task012_s3d_*.log). No link/test claimed (S4 gate).

source-contract fidelity (independent Controller review subagent) — CLEAN, 9/9:
  1. getOrSet 8-arg matches CH FileCache.h; body line-for-line (holes/splitRange/
     EMPTY); get uses fill_with_detached + DETACHED fallback, never creates cache
     segments; getDownloadedContiguousOrEmpty contiguity+write-offset checks.
  2. tryReserve/doEviction match CH; lock ordering cache_guard->key->cache_state_guard.
  3. R2 applySettingsIfPossible value comparison new!=actual (Velox 2343-2456 vs
     CH 2800+), early-out new==actual (CH:2802), NO .changed reliance.
  4. three-phase shutdown byte-identical CH:2666-2684 (Velox 1912-1931).
  5. B5 getCommonUserID/getCommonOrigin use injected common_user_id; no Context,
     no ServerUUID (comment only).
  6. B6 both tasks via owned scheduler.createTask (Velox 435,466) not getSchedulePool.
  7. B3/B2a CacheMetadata ctor with worker_pool ref + reserveSpaceWaitLockTimeoutMilliseconds.
  8. overcommit policies throw; Prometheus/metrics/failpoint dropped to no-op; no
     OpenedFileCache singleton; FileCacheBoundedQueue/folly::Random/
     DeltaCpuWallTimeStopWatch/std::call_once applied.
  9. QueryLimit.cpp TOCTOU discipline, destroy-doomed-after-lock, max-download-size,
     FileCacheKeyAndOffset map faithful; CurrentThread -> FileCacheQueryIdScope.
  Minor benign deltas (magic_enum->literal 2; LOGICAL_ERROR->throwFileCacheException;
  trimmed log tails) do not affect behavior.

unresolved findings: None for S3.
```

## Commits (S3)

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `16b4fc155 Task 012: Port FileCache.cpp + QueryLimit.cpp (sub-attempt S3)` |

## Worker attempt (S4 CMake + tests + green build)

```text
worker_status: blocked
environment_profile: home-chang
task: 012 (sub-attempt S4 — CMake registration + 6 behavioral tests + final compile/link/test gate)
```

S4 registered the center-SCC test target, authored all six behavioral test files, and drove the
whole SCC through a real compile + LINK + test run. The SCC compiles and links cleanly, and
**46 of 47 tests pass** (all mandatory contracts EXCEPT releasable reserve eviction). The stage
gate is not green because of ONE structural blocker: the mandatory "releasable reserve eviction"
contract cannot be satisfied while the approved **B2b** decision makes SCC-phase file removal
throw `VELOX_NYI`. This is the same class of blocker as B2a/B2b/B3/B5/B6 (a manager-owned concept
absent in the SCC phase) and needs a Controller/user decision, not a code fix I am authorized to make.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `16b4fc155` | clean (0 files); `filecache2...baibaichen/filecache [ahead 5]` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `7bbb2350a69` | receipt append only |

`HEAD 16b4fc155` = "Task 012: Port FileCache.cpp + QueryLimit.cpp (sub-attempt S3)". No
pre-existing dirty files. No staging/commit/amend/rebase/push.

## Files changed (this attempt)

```text
MODIFIED (minimal production seam required by the mandatory partial-physical-append-failure test):
  velox/ch/Interpreters/FileCache/FileSegment.h    (+13: static WriteFileFactory injection seam +
                                                    setWriteFileFactoryForTesting/createWriteFile;
                                                    <functional> include)
  velox/ch/Interpreters/FileCache/FileSegment.cpp  (write() now creates the local cache writer's
                                                    velox::WriteFile via createWriteFile(); the
                                                    DEFAULT factory reproduces the exact original
                                                    LocalWriteFile(path,false,false,true) — production
                                                    behavior is byte-identical unless a test installs
                                                    a fault-injecting file)
MODIFIED (test registration):
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt (adds velox_ch_filecache_core_scc_test +
                                                    add_test; links velox_ch_filecache — which
                                                    already contains the 9 SCC .cpp from S2/S3 — so
                                                    the whole SCC links once. A separate
                                                    velox_ch_filecache_core library is NOT created:
                                                    that would double-define every SCC symbol (ODR).)
NEW (6 behavioral tests, 1369 lines):
  velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp   (88)
  velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp       (447)
  velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp          (140)
  velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp         (305)
  velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp        (209)
  velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp  (180)
```

## Minimal production seam (recorded)

```text
The mandatory partial-physical-append-failure test (task table + S4 confirmed approach) requires
driving the PRODUCTION FileSegment::write reconcile over "an injected production velox::WriteFile
that physically commits a strict prefix then throws FileCacheErrnoException", with reconciliation
inside production FileSegment, never the test double. The committed S2 FileSegment::write
constructed its own velox::LocalWriteFile inline (FileSegment.cpp) with NO injection seam, so the
authorized test approach was not expressible. S4 added the smallest possible seam that keeps CH
semantics identical:
  - FileSegment::WriteFileFactory = std::function<unique_ptr<velox::WriteFile>(const std::string&)>.
  - FileSegment::createWriteFile(path) calls a file-static factory whose DEFAULT reproduces the
    exact prior construction: LocalWriteFile(path, /*createParentDirs*/false,
    /*throwOnExists*/false, /*bufferIo*/true). Production behavior is unchanged when no override
    is installed.
  - FileSegment::setWriteFileFactoryForTesting(factory) installs an override; the test uses a RAII
    ScopedWriteFileFactory that restores the default on scope exit.
Reconciliation stays entirely in production FileSegment::write (reads fs::file_size, enforces
downloaded<=physical<=reserved, sets downloaded=physical, marks failed, rethrows original). The
injected PartialCommitThenThrowWriteFile ONLY commits a strict prefix then throws
FileCacheErrnoException(ENOSPC) — it performs no reconciliation. This is recorded as a
testability seam (not a link-time defect); flagged for Controller review since it edits accepted
S2 production code, though it changes no runtime behavior on the default path.
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure (home-chang recipe + `-DVELOX_BUILD_TESTING=ON`) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s4_configure.log` |
| build `velox_ch_filecache_core_scc_test` (whole SCC compile+LINK) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s4_build.log` |
| run suite excluding the blocked eviction test (`--gtest_filter=-FileCacheTest.TryReserveEvictsReleasable`) | 0 (46/46 pass) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s4_test_nonevict.log` |
| full gate `ctest -R ^velox_ch_filecache_core_scc_test$` | 8 (1 test RED: eviction) | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s4_test.log` |
| isolate eviction RED | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task012_s4_eviction_red.log` |

No `-j` was passed. The SCC compiled and LINKED cleanly (exit 0) — the center strongly-connected
component (priority/eviction + FileSegment/Metadata/FileCache/QueryLimit) is a complete
compile/link closure.

## Acceptance evidence

```text
build/link: exit 0. The whole center SCC compiles and LINKS into velox_ch_filecache and the
  velox_ch_filecache_core_scc_test executable. Two real LINK-surfaced items were handled:
    - initial PriorityEvictionTest used protected getHoldSize/getHoldElements -> rewrote to assert
      held space via the public canFit (compile fix, not a production change).
    - initial PriorityEvictionTest constructed CacheUsage (ctor undefined in the SCC library, it
      belongs to the excluded overcommit path) -> replaced with an empty-source
      takeKeptAliveCacheUsage exercise (still runs the B1 insert-range+clear path). No production
      symbol was missing for the SCC itself; the SCC links.

test count: 47 registered; 46 PASS, 1 FAIL (0 skipped/disabled).
failed tests: FileCacheTest.TryReserveEvictsReleasable — genuine behavioral RED against the
  approved B2b VELOX_NYI (see Blockers). It throws (catchable), does not abort the binary.
skipped/disabled tests: 0.
git diff --check: clean (no whitespace errors).

Behavioral RED / production-path evidence per material contract:
  missing key + THROW            -> FileCacheTest.MissingKeyThrows: cache.removeFileSegment on a
                                    random key throws (lockKeyMetadata THROW); removeFileSegmentIfExists
                                    does not. PASS.
  releasable reserve eviction    -> FileCacheTest.TryReserveEvictsReleasable: RED — reserving on a
                                    2nd segment evicts the releasable downloaded candidate, whose
                                    file removal hits the B2b VELOX_NYI (Metadata.cpp:1250). BLOCKED.
  empty query id                 -> QueryLimitTest.EmptyQueryIdCreatesNoContext: holder present but
                                    context null, no map entry. PASS.
  same query id                  -> QueryLimitTest.SameQueryIdSharesContext: both holders share one
                                    QueryContext pointer, use_count==3 (map+2 holders). PASS.
  last holder release            -> QueryLimitTest.LastHolderReleaseRemovesEntry: weak_ptr expires
                                    only after the last holder dies; a fresh holder gets a new context.
                                    PASS.
  doomed context destruction     -> QueryLimitTest.DoomedContextDestroyedAfterLockRelease: after the
                                    last holder dtor (removeQueryContext under lockCache, doomed
                                    destroyed after the lock scope), the cache write lock is
                                    re-acquirable and the context weak_ptr is expired. PASS.
  max download size              -> QueryLimitTest.MaxDownloadSizeRejectsExcessReservation: per-query
                                    LRU getSizeLimitApprox()==maxDownloadSizePerQuery; a reserve of
                                    seg>max under an active FileCacheQueryIdScope("q1") is rejected
                                    through the real reserve->doTryReserve->query_priority->canFit
                                    path. PASS.
  queue pipeline                 -> FileCacheTest.BoundedQueuePipeline: real timed tryPush(v,10)
                                    fills to capacity, a full-queue timed tryPush returns false,
                                    non-blocking tryPop drains FIFO, finish() then rejects push and
                                    drains. PASS.
  remote reader handoff          -> FileSegmentDownloadTest.RemoteReaderHandoffDetachesAndPreservesOffsets:
                                    real ReadBufferFromVeloxReadFile over a LocalReadFile, read a
                                    prefix (getFileOffsetOfBufferEnd==prefix, available==prefix,
                                    getPosition==0), downloader setRemoteFileReader/getRemoteFileReader,
                                    second install rejected, resetRemoteFileReader detaches (segment
                                    reader null) while the externally held reader keeps its offsets.
                                    PASS.
  partial-file resume            -> FileSegmentDownloadTest.PartialFileResumeAppendsWithoutTruncation:
                                    production FileSegment writes a 3000-byte prefix, releases via the
                                    real completePartAndResetDownloader (state PARTIALLY_DOWNLOADED),
                                    re-acquires the downloader, verifies physical size == downloaded,
                                    appends a 2000-byte suffix, and a byte-for-byte fread proves the
                                    prefix was NOT truncated. PASS.
  partial physical append failure-> FileSegmentDownloadTest.PartialPhysicalAppendFailureReconciles-
                                    DownloadedToPhysical: an injected production velox::WriteFile
                                    (PartialCommitThenThrowWriteFile) commits a strict 1000-byte
                                    prefix of the failing chunk then throws FileCacheErrnoException
                                    (ENOSPC). PRODUCTION FileSegment::write reads fs::file_size,
                                    enforces downloaded<=physical<=reserved, sets downloaded=physical
                                    (4000), marks failed, and rethrows the ORIGINAL exception
                                    (getErrno()==28). Reconciliation is entirely in production; the
                                    double only injects the fault. PASS.
  priority/eviction bookkeeping  -> PriorityEvictionTest: LRU limits/canFit/HoldSpace accounting,
                                    SLRU protected/probationary split ratio (0.6 -> 600/400), Split
                                    Data/System partitioning + aggregate limit, EvictionCandidates
                                    empty bookkeeping + getOriginalQueueType None, EvictionInfo
                                    per-queue aggregation + takeKeptAliveCacheUsage (B1 path). PASS.

To reach the S4 mandatory contracts without tripping the B2b rename/remove throw for the OTHER
tests (which the S4 prompt explicitly forbids exercising), the populate/resume tests set
boundaryAlignment == segment size and download strict prefixes so a completed segment shrinks to a
target that rounds up to the full range and stays PARTIALLY_DOWNLOADED (no <offset>-><offset>_<size>
rename). This is faithful (real production complete/shrink path) and keeps those tests off the B2b
throw. The eviction contract has no such workaround (see Blockers).
```

## Worker review

```text
review subagent: one read-only general-purpose reviewer over the complete S4 diff (FileSegment
  seam + CMake + 6 tests), given the mandatory-tests table, the diff, and the test outcomes. Asked
  for false-green tests, whether the two FileSegment tests drive production reconcile (not mock),
  test-oracle correctness vs CH, flake/abort risk, seam fidelity, and eviction-blocker accuracy.
findings:
  1. No false-green tests: all 6 files have real assertions on the production path; no
     disabled/assertion-free bodies.
  2. partial-physical-append-failure DOES drive production FileSegment::write reconcile
     (FileSegment.cpp catch block reads fs::file_size, sets downloaded=physical, rethrows original);
     the injected WriteFile only commits a prefix + throws (no reconciliation). Confirmed.
  3. The production seam's DEFAULT factory is byte-identical to the original LocalWriteFile
     construction; ScopedWriteFileFactory is RAII and restores an equivalent default.
  4. Oracles correct (path filenames, caller-id, enum values, Range size, use_count, reserve
     rejection).
  5. Eviction blocker confirmed UNAVOIDABLE: removeFileSegmentImpl sets
     opened_handle_invalidation_required for any candidate with downloaded>0 && file exists, then
     unconditionally VELOX_NYI; evicting a releasable DOWNLOADED candidate always hits it.
  Non-blocking nit: ScopedWriteFileFactory dtor comment referenced a nonexistent nullptr sentinel.
resolutions:
  - Fixed the ScopedWriteFileFactory dtor comment (cosmetic; no behavior change).
  - All other findings were confirmations (no action needed).
unresolved findings: none from the review. The only unresolved item is the external B2b-vs-eviction
  blocker below (needs a Controller/user decision).
```

## Blockers

```text
BLOCKER B7 — the mandatory "releasable reserve eviction" contract conflicts with the approved B2b
throw. Evicting a releasable DOWNLOADED candidate physically removes its file, which the SCC-phase
B2b decision makes throw VELOX_NYI, so the mandatory eviction test cannot pass.

First actionable error (task012_s4_eviction_red.log / task012_s4_test.log):
  Metadata.cpp:1250, removeFileSegmentImpl: "Opened-file-handle invalidation on file removal is not
  implemented in the SCC phase (Task 013 Manager); path: <cache>/<key3>/<key>/0" (ErrorCode
  NOT_IMPLEMENTED), then FileCache "Failed to evict 1 file segments".

Root cause (structural, an approved-decision conflict — NOT a test-authoring mistake and NOT a
missing dependency I may map):
  - The mandatory-tests table requires "releasable reserve eviction: reserve succeeds only after
    the real candidate is removed; cache size and segment state agree." A cache-resident, evictable,
    releasable candidate must have been completed with downloaded>0 (it occupies cache space and has
    a file on disk).
  - Production eviction runs EvictionCandidates::evict() -> LockedKey::removeFileSegment(...) ->
    LockedKey::removeFileSegmentImpl (Metadata.cpp). For any candidate with getDownloadedSize()>0 &&
    fs::exists(path), removeFileSegmentImpl does fs::remove(path) and sets
    opened_handle_invalidation_required = true, then UNCONDITIONALLY throws VELOX_NYI
    (Metadata.cpp:1250) — the approved B2b decision (2026-07-20): replace OpenedFileCache::remove
    with a loud not-implemented throw so any path reaching opened-handle invalidation fails loudly.
  - Therefore evicting ANY downloaded releasable candidate hits the B2b throw. There is NO faithful
    production path that evicts a downloaded segment without reaching removeFileSegmentImpl (a
    downloaded==0 segment occupies no space and is removed as EMPTY on complete, so it can never be
    the space-forcing eviction candidate). The reviewer independently confirmed this is unavoidable.
  - The S4 prompt itself says "Do NOT exercise the opened-file-handle rename/remove invalidation
    paths (B2b decision: those throw VELOX_NYI)". That instruction and the mandatory eviction
    contract are in direct conflict: the B2b decision (made 2026-07-20) assumed the S4 tests would
    not reach removal, but eviction is removal.

I worked the rename half of this tension inside my scope (populate/resume tests set
boundaryAlignment == segment size and download strict prefixes so completion stays
PARTIALLY_DOWNLOADED and never renames <offset> -> <offset>_<size>), which kept 46/47 tests off the
B2b throw. The eviction half has no such workaround: eviction deletes the candidate's file.

Per EXECUTION_PROTOCOL worker rules 4 and 8 and the no-weaken-semantics rule, I did NOT:
  - remove/relax the B2b throw (an approved user decision; changing it would weaken semantics and
    override a decision outside my authority);
  - rewrite the eviction test to assert a non-eviction path or the current stub (that would be the
    false-green the protocol forbids).
So the eviction test stays as a genuine behavioral RED and the stage gate remains red.

Exact decision needed from the Controller/user (either unblocks a redispatch of the eviction test):
  1. Refine B2b so the SCC-phase file-removal throw fires only when an opened handle actually
     exists to invalidate (e.g. gate on an injected opened-file-cache reference that is empty in the
     SCC phase), so normal eviction/removal proceeds and only a real stale-handle case throws; OR
  2. Explicitly exempt SCC-phase eviction file-removal from the B2b throw (record as an amendment,
     analogous to the B2b relaxation itself), with Task 013 restoring real Manager-backed
     invalidation. In both cases the mandatory eviction test then evicts a releasable downloaded
     candidate and asserts cache size/segment state as required.

Reference anchors: Metadata.cpp:1215-1257 (removeFileSegmentImpl; opened_handle_invalidation_required
+ VELOX_NYI @1250); EvictionCandidates.cpp EvictionCandidates::evict -> removeFileSegment;
FileCache.cpp doTryReserve/doEviction; Task-012 amendment "S2 unblock ... B2b" (2026-07-20);
FileSegment.cpp setDownloadedUnlocked -> renameToIncludeSizeInNameUnlocked (:696) VELOX_NYI @ the
rename site (worked around by the partial-download alignment trick for the non-eviction tests).
```

## Worker declaration

```text
Only Task 012 sub-attempt S4 was attempted.
The center SCC compiles and LINKS cleanly; velox_ch_filecache_core_scc_test builds (exit 0). Six
new behavioral test files were created and one minimal production seam (FileSegment WriteFileFactory,
default byte-identical to prior behavior) was added to make the authorized partial-physical-append-
failure test expressible; the tests CMake registers the SCC test target. 46 of 47 tests pass with
0 skipped; the sole failure is a genuine behavioral RED for the "releasable reserve eviction"
mandatory contract, blocked by the approved B2b VELOX_NYI on SCC-phase file removal (BLOCKER B7,
needs a Controller/user decision). Changes are unstaged and uncommitted; all prior working-tree
changes preserved. The worker stopped after writing this receipt.
```

## Controller unblock response 5 (B7 — B2b correction)

```text
controller_status: blocker_resolved
task: 012 S4
resolved: 2026-07-20
```

## Resolution

```text
root cause (Controller's own prior mis-call, now corrected):
  The S2 B2b decision made SCC-phase file removal THROW VELOX_NYI. But in CH the
  removal site does two adjacent INDEPENDENT things (Metadata.cpp:1261,1267):
  (1) fs::remove(path) — core of eviction, must run; (2)
  OpenedFileCache::instance().remove(...) — invalidate cached open handles, a
  Task-013 Manager concept. The worker faithfully placed the throw so that ANY
  eviction of a downloaded segment hits it -> the mandatory "releasable reserve
  eviction" test (FileCacheTest.TryReserveEvictsReleasable) is a genuine RED.
  Confirmed: Velox Metadata.cpp:1247-1254 throws unconditionally when
  opened_handle_invalidation_required; CH:1261 fs::remove then :1267 OpenedFileCache.

decision (user-approved 2026-07-20 — corrects B2b):
  Perform fs::remove normally (eviction works). Replace ONLY the opened-handle
  invalidation with a no-op + TODO(Task 013), NOT a throw, at BOTH sites (removal
  in Metadata.cpp; rename in FileSegment.cpp). Semantically safe: no
  OpenedFileCache exists in the SCC phase (grep velox/ch -> none), so no cached
  handles can go stale; invalidation has nothing to do until Task 013 introduces
  the handle cache and wires the real invalidation into the same seam. Task 013
  (which owns OpenedFileCache per design 02:35,89,244 and 013:117,249,426,453)
  replaces the two no-ops with the real Manager-backed invalidation.

task update:
  Recorded in the amendment "### S2 unblock — three structural resolutions" B2b
  block as "B2b CORRECTION (B7, 2026-07-20)".

evidence:
  46/47 tests already pass; only TryReserveEvictsReleasable RED, caused solely by
  the B2b throw. No other blocker. SCC compiles AND LINKS (S4 build exit 0).

redispatch: yes — on the existing S4 working tree: change the two throws to
  no-op + TODO(013), rerun the full green gate (velox_ch_filecache_core_scc_test,
  0 failed / 0 skipped).
```

## Worker attempt (S4 finish — B7 fix + green gate)

```text
worker_status: ready_for_controller
sub_attempt: S4 finish (B7 fix)
environment: home-chang
date: 2026-07-20
```

### B7 delta (opened-file-handle invalidation: throw -> no-op + TODO(Task 013))

```text
Corrects the earlier B2b over-broad throw. In CH the removal/rename sites do two
adjacent but INDEPENDENT things: (1) the actual filesystem op (`fs::remove` /
`fs::rename`) — core of eviction/rename, MUST run; (2) opened-file-handle
invalidation (`OpenedFileCache::instance().remove(...)`) — a Task-013 Manager
concept absent in the SCC phase. Fix: keep (1); replace ONLY (2) with a no-op +
`TODO(Task 013)`, NOT a throw. Semantically safe: no OpenedFileCache exists yet,
so no cached handles can go stale.

Metadata.cpp removeFileSegmentImpl (~L1207-1256):
  - `fs::remove(path)` still runs (L1225).
  - Removed the unconditional `VELOX_NYI` that fired whenever
    `opened_handle_invalidation_required`; the branch now does the CH-faithful
    erase-and-return-next-iterator only, with a `TODO(Task 013)` no-op
    (`(void)removed_path`). Both branches `return key_metadata->erase(it)`,
    matching CH's terminal erase.

FileSegment.cpp renameToIncludeSizeInNameUnlocked (~L720-750):
  - `fs::rename(old_path, new_path)` still runs (L729); `size_in_filename`
    published on success.
  - Removed the `if (renamed) VELOX_NYI(...)`; the opened-handle drop is now a
    `TODO(Task 013)` no-op (`(void)renamed`), no throw.

Eviction test (FileCacheTest.TryReserveEvictsReleasable):
  Already exercised the REAL production reserve+evict+`fs::remove` path and
  asserted the real eviction OUTCOME (cache size stays at capacity, count stays
  1, key2 present), never a throw. One assertion was corrected: after eviction
  key1's metadata is fully removed, so the per-key `getFileSegmentInfos(key1,..)`
  throws THROW_LOGICAL (CH-faithful: no such key) instead of returning empty. The
  "key1 gone" check now uses the non-throwing whole-cache enumeration
  `getFileSegmentInfos(user_id)`: exactly one segment survives and it belongs to
  key2 — proving the releasable key1 segment was evicted via the real production
  path. Oracle = CH behavior. No test weakened; no skip/disable.
```

### Eviction behavioral RED evidence

```text
Before the B7 fix (VELOX_NYI still in place), the build linked (exit 0) and
46/47 tests passed; FileCacheTest.TryReserveEvictsReleasable was RED because the
second populateSegment's reserve triggered real eviction of the releasable key1
segment, which reached removeFileSegmentImpl and threw VELOX_NYI
("Opened-file-handle invalidation on file removal is not implemented ...").
This is a genuine RED against the intentionally-broken (throw-on-eviction) path.
After the fix, eviction completes: fs::remove deletes key1's file, key1 metadata
is erased, cache size stays at capacity (4096), segment count stays 1, and the
sole surviving segment is key2. Test GREEN.
```

### Commands + exit codes

```text
configure: /usr/bin/cmake -DCMAKE_BUILD_TYPE=Debug -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_BUILD_TESTING=ON -G Ninja -S <velox_repo> -B <velox_build_dir>
  exit 0    log: <velox_build_dir>/task012_s4b_configure.log
build:  ninja -C <velox_build_dir> velox_ch_filecache_core_scc_test
  exit 0 (compiles AND LINKS)    log: <velox_build_dir>/task012_s4b_build.log
test:   ctest --test-dir <velox_build_dir> -R '^velox_ch_filecache_core_scc_test$' --output-on-failure
  exit 0    log: <velox_build_dir>/task012_s4b_test.log

<velox_repo>       = /home/chang/OpenSource/velox
<velox_build_dir>  = /home/chang/OpenSource/velox/cmake-build-debug-gcc13
```

### Final test count

```text
[==========] 47 tests from 10 test suites ran.
[  PASSED  ] 47 tests.
0 failed / 0 skipped / 0 disabled.
ctest exit code: 0
```

### Velox working-tree state (unstaged, uncommitted — preserved)

```text
branch: filecache2   HEAD: 16b4fc155
 M velox/ch/Interpreters/FileCache/FileSegment.cpp
 M velox/ch/Interpreters/FileCache/FileSegment.h
 M velox/ch/Interpreters/FileCache/Metadata.cpp
 M velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
?? velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp
?? velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp
?? velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp
?? velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp
?? velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp
?? velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp
git diff --check: clean (exit 0)
```

### Read-only review (B7 delta + eviction test)

```text
Independent read-only reviewer: no must-fix findings. Confirmed fs::remove and
fs::rename still run; opened-handle invalidation is now a no-op + TODO(Task 013),
not a throw; erase-then-return-next-iterator matches CH; TryReserveEvictsReleasable
asserts the real eviction outcome via non-throwing whole-cache enumeration, no
false-green; the other 46 tests are genuine (real setup, real production-path
calls, real asserts); no DISABLED_/GTEST_SKIP. Two cosmetic nits only (an intended
documented Task-013 seam branch; a redundant EXPECT_NE) — left as-is.
```

## Controller review (S4 green gate, accepted) — TASK 012 COMPLETE

```text
controller_status: accepted (S4 stage gate GREEN; Task 012 fully accepted)
environment_profile: home-chang
task: 012 S4 / whole Task 012
reviewed: 2026-07-20
```

## Review evidence (S4 — final stage gate)

```text
scope: 6 new behavioral test files + modified tests/CMakeLists.txt (registers
  velox_ch_filecache_core_scc_test) + B7 fix in FileSegment.cpp/Metadata.cpp.
  diff --check clean.

build/link/test (independently verified, not from receipt):
  configure exit 0; build LINKS velox_ch_filecache_core_scc_test (build log:
  "Linking CXX executable ..."), exit 0 — the whole SCC (priority/eviction +
  FileSegment/Metadata/FileCache/QueryLimit) is a complete compile/link closure.
  Ran the 73MB binary directly: "[==========] 47 tests from 10 test suites ran.
  [ PASSED ] 47 tests." 0 failed, 0 skipped. --gtest_list_tests = 47.

B7 correction verified: Metadata.cpp:1245-1252 opened-handle block is
  no-op + TODO(Task 013) with fs::remove still running at :1225; FileSegment.cpp
  :740-749 handle block is no-op + TODO with fs::rename still at :729. No
  VELOX_NYI at either site. Eviction (fs::remove) works.

false-green audit (independent Controller review subagent) — SOUND, 0 findings:
  - Partial-file resume (FileSegmentTest.cpp:164): real production path
    (getOrSetDownloader/reserve/write prefix -> completePartAndResetDownloader ->
    re-acquire -> append suffix), reads back on-disk bytes, prefix not truncated.
  - Partial physical append failure (FileSegmentTest.cpp:304): injected WriteFile
    ONLY commits prefix + throws FileCacheErrnoException(ENOSPC); reconciliation is
    in PRODUCTION FileSegment.cpp:486-514 (downloaded<=physical<=reserved, mark
    failed, rethrow original) — NOT in the test double.
  - B7 eviction (FileCacheTest.cpp:179): asserts real eviction outcome (capacity
    held, 1 survivor = key2), not a throw.
  - missing-key THROW, query-id holders + doomed-context-after-lock, max download
    size, queue pipeline, remote reader handoff: all real assertions.
  - Oracles derive from CH behavior. No false-green / weak-oracle / mock-reconcile.

cross-task architecture review:
  Atomic 011+012 stage restored to a full green compile/link/test closure. The
  four sub-attempts (S1 headers, S2 FileSegment/Metadata + finish 011 cpp, S3
  FileCache/QueryLimit, S4 CMake+tests) delivered the SCC without false-green;
  every structural gap (B1/B2a/B2b+B7/B3/B5/B6) was resolved by approved design or
  explicit user decision. Opened-file-handle invalidation is deferred to Task 013
  (which owns OpenedFileCache) with the seam in place (no-op+TODO).

unresolved findings: None. TASK 012 ACCEPTED.
```

## Commits (S4)

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `13b2dc63d Task 012: Center-SCC green build + behavioral tests (sub-attempt S4)` |
