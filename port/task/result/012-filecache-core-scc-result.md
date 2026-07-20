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
