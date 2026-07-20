# Task 011 Result: Port Priority/Eviction Sources (Center-SCC Part A)

## Worker attempt 1

```text
worker_status: blocked
environment_profile: home-chang
task: 011
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `d93fa99e5` (`Task 003: Add ProfileEvents/CurrentMetrics enumerator surface (B1/B2)`) | clean working tree; branch is ahead 1 of `baibaichen/filecache` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `4dea29b6c1e` (`Task 010 review: Record approved deviations and mappings into task documents`) | clean working tree; branch is ahead 2 of `baibaichen/ch-filecache` |

Note: the dispatch prompt named the Velox branch `filecache2` / HEAD `d93fa99e5`,
which matches the working tree. The ENVIRONMENT/task text calls the branch
`filecache`; treated the prompt as authoritative for this environment.

No files were created or modified in either repository. No sources were written
because the unreviewed-dependency gate fired before implementation (worker rules
5 and 6, EXECUTION_PROTOCOL.md "Unreviewed dependency gate").

## Files changed

```text
None (blocked before writing any of the 11 declared Velox sources).
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| `git status/log` baseline in both repos (read-only) | 0 | n/a (captured in Repository baselines) |

No configure/build/test commands were run. Task 011 has no build gate; the gate
that fired is the source-contract dependency gate, not a compile failure.

## Acceptance evidence

```text
test count: n/a (no tests in this task; blocked before source migration)
failed tests: n/a
skipped/disabled tests: n/a
benchmark result: n/a
git diff --check: clean (no changes made)
```

Structural/source-contract self-check could not be reached: the port cannot be
written without either an approved Velox mapping for the dependencies listed
below or explicit approval to substitute them, and the gate forbids guessing a
"closest" API or fabricating a shim.

## Worker review

```text
review subagent: not launched (no diff exists to review; blocked before writing sources)
findings: n/a
resolutions: n/a
unresolved findings: n/a
```

## Blockers

The Task-011 exact-port sources reach several CH dependencies that have **no
explicit reviewed Velox mapping** in the approved design, the Task-011
"Required replacements" list, or the accepted 003-010 receipts. Per
EXECUTION_PROTOCOL.md ("During migration, 'dependency' includes every CH
external class, macro, type alias, helper, API, lifecycle primitive...") and
worker rules 4-6, each must be reviewed before it can be used or substituted.
The Task-011 "Required replacements" list covers only: `DB` namespace,
`String`/`UInt*`, `Exception`/`ErrorCodes`/`chassert`, `absl`->`folly F14`,
`CurrentMetrics`/`ProfileEvents`/logging shims, `boost::noncopyable`, and
`magic_enum`. The dependencies below are outside that set and outside the
`port/1-dependencies/01-filecache-infra-mapping.md` matrix (verified: no rows
for any of them).

### D-011-1 — Per-thread ProfileEvents timer (already recorded as ownership hole H2)

```text
CH defining source + caller:
  EvictionCandidates.cpp:267
    auto timer = DB::CurrentThread::getProfileEvents()
                   .timer(ProfileEvents::FilesystemCacheEvictMicroseconds);
required behavior:
  RAII scope-bound timer; starts on construct, records elapsed microseconds
  into the per-thread ProfileEvents counter on destruct.
candidate Velox primitives:
  none confirmed. The Task-003 ProfileEvents shim exposes only
  ProfileEvents::increment (no-op) and ProfileEventTimeIncrement<Unit>
  (name-only). There is no per-thread getProfileEvents()/.timer() surface.
known semantic difference:
  no per-thread counter accumulation exists in the shim.
decision needed:
  This is exactly ownership hole H2 in
  port/task/fullreview/root-oss/1/evidence/011-012-consumer-contract-ledger.md
  ("ambiguous 003 vs 006 ... needs Controller confirmation, do not guess").
  Assign an owner and an approved mapping (e.g. drop the timer as a no-op like
  the other ProfileEvents surface, or add a scope-bound no-op timer shim), or
  confirm it is deferred to Task 017 like the rest of real observability
  (003-010-review-decisions.md section 6 "Real observability").
```

### D-011-2 — `randomSeed()` + `pcg64` RNG

```text
CH defining source + callers:
  LRUFileCachePriority.cpp:86   queue_id(randomSeed())
  LRUFileCachePriority.cpp:862  pcg64 generator(randomSeed()); std::shuffle(...)
    (include: pcg-random/pcg_random.hpp, Common/randomSeed.h)
required behavior:
  queue_id: a process-unique-enough size_t identifier per LRU queue, used to
    key EvictionInfo (QueueID) so a priority finds its own per-queue target.
  shuffle: seed a PRNG to randomly permute the queue in shuffle().
candidate Velox primitives:
  folly::Random / std::random_device / std::mt19937_64 exist in Velox, but no
  reviewed mapping selects one, and queue_id uniqueness vs. collision behavior
  is a correctness property (EvictionInfo map keying) that must be decided, not
  guessed.
decision needed:
  Approve a specific Velox RNG/seed mapping for (a) queue_id generation and
  (b) shuffle, with the uniqueness guarantee for queue_id stated.
```

### D-011-3 — `LockMemoryExceptionInThread` + `VariableContext::Global`

```text
CH defining source + callers:
  LRUFileCachePriority.cpp:208  LockMemoryExceptionInThread lock_memory_tracker(VariableContext::Global);
  LRUFileCachePriority.cpp:310  (same, in removeInvalidatedEntries catch path)
    (include: Common/LockMemoryExceptionInThread.h)
required behavior:
  RAII guard that suppresses memory-tracker exceptions (e.g. MEMORY_LIMIT_EXCEEDED)
  for the scope, so bookkeeping allocations in the invalidated-refs deque (and
  the catch-path push_front that must not lose a ref) cannot throw.
candidate Velox primitives:
  none. Velox has no CH-style thread memory tracker; this may legitimately be a
  no-op shim, but that is a behavior decision (it changes when/whether these
  allocations can throw) and is not in any approved mapping.
decision needed:
  Approve a mapping (most likely a no-op RAII shim, given Velox has no CH memory
  tracker) and record it, OR assign it to an owning task's shim scope.
```

### D-011-4 — `WriteBufferFromOwnString` + `operator<<` string building

```text
CH defining source + callers:
  EvictionCandidates.cpp:39-49   QueueEvictionInfo::toString()  (WriteBufferFromOwnString wb; wb << ...; wb.str())
  EvictionCandidates.cpp:74-82   EvictionInfo::toString()       (same)
required behavior:
  in-memory string builder with streaming operator<< for size_t/std::string,
  returning the accumulated std::string via .str().
candidate Velox primitives:
  fmt / std::ostringstream / std::string concatenation are all available, but
  no reviewed mapping selects the replacement. Distinct from the already-ported
  WriteBufferFromVeloxWriteFile (a file sink, not a string builder).
decision needed:
  Approve a specific string-builder substitution (e.g. rewrite these two
  toString bodies with fmt::format) as a reviewed transliteration.
```

### D-011-5 — `thread_local_rng` + `std::bernoulli_distribution` (failpoint path)

```text
CH defining source + caller:
  SLRUFileCachePriority.cpp:584-590  fiu_do_on(file_cache_slru_downgrade_fail_before_finalize, {
    if (std::bernoulli_distribution(0.001)(thread_local_rng)) throw ...; });
    (include: Common/thread_local_rng.h)
required behavior:
  thread-local PRNG used only inside a failpoint body.
candidate Velox primitives:
  The Task-003 FailPoint shim (Common/FailPoint.h) makes FAIL_POINT_TRIGGER a
  no-op do{}while(false), so the fiu_do_on body never executes. If the failpoint
  is mapped to that no-op, thread_local_rng is never reached. But the migration
  must decide how fiu_do_on / fiu_init map to the shim, which is not spelled out
  for these specific call sites.
decision needed:
  Confirm the failpoint mapping (fiu_do_on -> no-op via the FailPoint shim) so
  the thread_local_rng/bernoulli body is a dead no-op branch, OR provide a
  reviewed RNG mapping if the body must remain live.
```

### D-011-6 — `assert_cast<T*>`

```text
CH defining source + callers (representative):
  SLRUFileCachePriority.cpp:131,267,369,557,603 ; SplitFileCachePriority.cpp uses
  it indirectly via getNestedOrThis; include Common/assert_cast.h
required behavior:
  debug-checked static downcast (chassert(dynamic_cast) in debug, static_cast in
  release) used to obtain SLRUIterator*/SplitIterator* from an Iterator*.
candidate Velox primitives:
  a static_cast or a small assert_cast shim; both plausible, but the choice is a
  reviewed-transliteration decision not present in the Required-replacements list.
decision needed:
  Approve a specific mapping (add an assert_cast shim, or use static_cast with a
  chassert(dynamic_cast) guard) and record it.
```

### Also flagged (resolvable, but recorded so the Controller can confirm intent)

```text
- FilesystemCacheOvercommitUsers: CH CacheUsage.h CacheUserData holds
  CurrentMetrics::Increment metric_increment{CurrentMetrics::FilesystemCacheOvercommitUsers}.
  003-010-review-decisions.md B2 explicitly forbids adding this metric, and the
  ledger marks the whole CacheUsagePerUser machinery as excluded-overcommit (O2).
  The faithful resolution is to port CacheUsage.h while dropping the banned
  metric member (excluded overcommit observability), per design doc 07
  ("keep types, no-op/minimal implementation") + ledger residual concern #4
  ("minimal reachable subset ... rather than porting the whole file"). This is
  resolvable in-scope once the above gates are cleared, but is recorded because
  it is a deliberate deviation from a line-by-line copy of CacheUsage.h.
- SCOPE_EXIT (SLRUFileCachePriority.cpp:678) has an approved Task-012 mapping
  (Folly scope guard, 003-010-review-decisions.md section 2) but that row is
  scoped to Task 012, not Task 011; confirm it may be used here too.
- FileCache::getInternalOrigin() (SLRUFileCachePriority.cpp:695,721) is a
  Task-012-owned static factory (hole H3). This is an allowed forward reference
  to a Task-012 type (Task 011 does not compile), but recorded for completeness.
```

First actionable error (single, exact): the first exact-port source to write is
`IFileCachePriority`/`LRUFileCachePriority`, and `LRUFileCachePriority.cpp:86`
requires `randomSeed()` (D-011-2) with no approved Velox mapping; writing it
would require guessing an RNG/seed primitive, which the unreviewed-dependency
gate forbids.

## Worker declaration

```text
Only Task 011 was attempted.
No changes were made; nothing is staged or committed in either repository.
The worker stopped after writing this receipt.
Recommended next step per protocol: the Controller escalates the unreviewed
dependencies D-011-1..D-011-6 (and confirms the three flagged items) for user
review, records the approved mappings into the canonical design and the Task-011
amendment, then redispatches Task 011 from the source-contract check.
```

## Controller unblock response 1

```text
controller_status: blocker_resolved
task: 011
resolved: 2026-07-20
```

## Resolution

```text
root cause:
  The Task-011 exact-port sources reach seven CH dependencies with no approved
  Velox mapping (not in Required-replacements, the infra-mapping matrix, or the
  accepted 003-010 receipts). The Worker correctly stopped at the
  unreviewed-dependency gate instead of guessing. Controller independently
  confirmed all seven: CH source uses them, and none is mapped anywhere.

decision (user-reviewed 2026-07-20):
  D-011-1 ProfileEvents timer  -> Task-003 ProfileEventTimeIncrement no-op RAII
                                  (ledger hole H2 resolved to Task 003).
  D-011-2 randomSeed()/pcg64   -> folly::Random (queue_id = rand64(); shuffle via
                                  folly::Random); faithful to CH's random source.
  D-011-3 LockMemoryExceptionInThread(Global) -> no-op/omit (Velox has no CH
                                  memory-tracker fault injection).
  D-011-4 WriteBufferFromOwnString -> std::ostringstream / fmt (debug strings).
  D-011-5 thread_local_rng + bernoulli in failpoint -> no-op with the Task-003
                                  no-op failpoint.
  D-011-6 assert_cast          -> VELOX_CHECK + dynamic_cast (debug) / static_cast
                                  (release), matching CH assert_cast semantics.
  Conflict CacheUsage.h OvercommitUsers -> port CacheUsage.h but DROP the
                                  FilesystemCacheOvercommitUsers member (:16, :116);
                                  overcommit excluded per B2 / ledger O2.

task update:
  Recorded all seven mappings in port/task/011-filecache-priority-eviction.md
  under "## Approved dependency mappings (unblock 2026-07-20, user-reviewed)".

evidence:
  CH usage confirmed at EvictionCandidates.cpp:267; LRUFileCachePriority.cpp:86,
  208,310,862; EvictionCandidates.cpp:39,75; SLRUFileCachePriority.cpp:587,
  131,267,369,557,603; CacheUsage.h:16,116. None present in
  port/1-dependencies/01-filecache-infra-mapping.md, 003 receipt, or velox/ch.

redispatch same task: yes
```

## Worker attempt 2

```text
worker_status: blocked
environment_profile: home-chang
task: 011
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `d93fa99e5` (`Task 003: Add ProfileEvents/CurrentMetrics enumerator surface (B1/B2)`) | clean working tree; branch ahead 1 of `baibaichen/filecache` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `4dea29b6c1e` (`Task 010 review: Record approved deviations and mappings into task documents`) | ` M port/task/011-filecache-priority-eviction.md` (Controller amendment), `?? port/task/result/011-filecache-priority-eviction-result.md` (this receipt) |

Prompt named the Velox branch `filecache2` / HEAD `d93fa99e5`, matching the working
tree; treated the prompt as authoritative for this environment. No Velox files were
created or modified: the unreviewed-dependency gate fired again on a DIFFERENT,
still-unmapped dependency before any of the 11 declared sources could be written.

## Files changed

```text
None (blocked before writing any of the 11 declared Velox sources).
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| `git status/log` baselines in both repos (read-only) | 0 | n/a (captured above) |
| `rg`/`grep` for `TSA_*` / `SCOPE_EXIT` / thread-annotation mappings across `port/` and `velox/ch/` and folly (read-only) | 0 | n/a (evidence below) |

No configure/build/test commands were run. Task 011 has no build gate; the gate
that fired is the source-contract dependency gate, not a compile failure.

## Acceptance evidence

```text
test count: n/a (no tests in this task; blocked before source migration)
failed tests: n/a
skipped/disabled tests: n/a
benchmark result: n/a
git diff --check: clean in velox (no changes made)
```

The structural/source-contract self-check could not be reached: writing the two
priority headers requires emitting CH thread-safety annotation macros that have no
reviewed Velox mapping, and the gate forbids inventing a spelling, a no-op shim, or
silently dropping them.

## Progress since attempt 1 (the seven approved mappings were all resolved)

All seven previously-blocking dependencies now have explicit, applicable mappings
that this worker verified against the port before stopping:

```text
D-011-1 ProfileEvents timer  -> ProfileEventTimeIncrement<Microseconds> is present in
        velox/ch/Common/ProfileEvents.h (no-op RAII). Applicable.
D-011-2 randomSeed()/pcg64   -> folly/Random.h present at deps-download/folly/folly/Random.h.
        Applicable (queue_id = folly::Random::rand64(); shuffle via folly::Random).
D-011-3 LockMemoryExceptionInThread(Global) -> omit; the two noexcept paths
        (addInvalidatedRef, removeInvalidatedEntries catch) stand unchanged. Applicable.
D-011-4 WriteBufferFromOwnString -> std::ostringstream for the two toString bodies. Applicable.
D-011-5 thread_local_rng + bernoulli -> dead under the FAIL_POINT_TRIGGER no-op
        (velox/ch/Common/FailPoint.h). Applicable.
D-011-6 assert_cast          -> VELOX_CHECK + dynamic_cast (debug) / static_cast (release);
        VELOX_CHECK/VELOX_FAIL available via velox/common/base/Exceptions.h. Applicable.
CacheUsage.h OvercommitUsers  -> drop the CurrentMetrics::Increment member (:16 extern, :116);
        CurrentMetrics::Increment shim exists but the member is excluded per B2/O2. Applicable.
```

Also confirmed resolvable within the approved rule set (NOT new blockers):

```text
- SCOPE_EXIT (SLRUFileCachePriority.cpp:678) HAS a reviewed mapping after all:
  port/task/fullreview/root-oss/1/003-010-review-decisions.md:85 maps
  `SCOPE_EXIT -> Folly scope guard`, and the 011-012 ledger (D9, line 76) assigns
  ownership to Task 003. folly/ScopeGuard.h (deps-download/folly/folly/ScopeGuard.h)
  provides the `SCOPE_EXIT { ... };` macro. Attempt 1's flag on this item is retracted.
- FileCache::getInternalOrigin (SLRUFileCachePriority.cpp:695,721) is an allowed
  forward reference to a Task-012-owned type; Task 011 does not compile, so a
  header-visible use is fine and is NOT a blocker.
- magic_enum -> explicit exhaustive switch, boost::noncopyable -> deleted copy ops,
  absl flat map/set -> folly F14: all covered by "## Required replacements". Applicable.
```

## Blockers

A single genuinely-new unmapped dependency remains. It is NOT one of the seven the
Controller approved, NOT in "## Required replacements", NOT in
`port/1-dependencies/01-filecache-infra-mapping.md`, and NOT in any accepted 003-010
receipt. Per EXECUTION_PROTOCOL.md "Unreviewed dependency gate" and worker rules 4-6,
and per the redispatch instruction ("if you reach a new unmapped CH dependency/macro/
type/API, STOP, record it, set worker_status: blocked. Do NOT extend the approved list
yourself or guess"), the worker stops without writing source.

### D-011-7 — Clang thread-safety annotation macros (`TSA_*`)

```text
CH defining source:
  base/base/defines.h:58  #define TSA_GUARDED_BY(...) __attribute__((guarded_by(__VA_ARGS__)))
  base/base/defines.h:60  #define TSA_REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
  base/base/defines.h:78  #define TSA_SUPPRESS_WARNING_FOR_READ(x)
                            ([&]() TSA_NO_THREAD_SAFETY_ANALYSIS -> const auto & { return (x); }())

CH call sites that MUST be emitted into Task-011 source (exact-port):
  LRUFileCachePriority.h:181  LRUQueue::iterator reserve_eviction_pos TSA_GUARDED_BY(eviction_pos_mutex);
  LRUFileCachePriority.h:182  LRUQueue::iterator background_eviction_pos TSA_GUARDED_BY(eviction_pos_mutex);
  LRUFileCachePriority.h:186  LRUQueue::iterator & evictionPos(EvictionCursor) TSA_REQUIRES(eviction_pos_mutex);
  LRUFileCachePriority.h:187  const LRUQueue::iterator & evictionPos(EvictionCursor) const TSA_REQUIRES(eviction_pos_mutex);
  LRUFileCachePriority.h:193  std::deque<InvalidatedRef> invalidated_refs TSA_GUARDED_BY(invalidated_mutex);
  LRUFileCachePriority.cpp:856 chassert(TSA_SUPPRESS_WARNING_FOR_READ(reserve_eviction_pos) == queue.end());
  LRUFileCachePriority.cpp:857 chassert(TSA_SUPPRESS_WARNING_FOR_READ(background_eviction_pos) == queue.end());
  SLRUFileCachePriority.h:219  std::weak_ptr<Entry> entry TSA_GUARDED_BY(entry_mutex);

required behavior:
  Compile-time-only Clang -Wthread-safety annotations (Clang capability model).
  No runtime effect. TSA_SUPPRESS_WARNING_FOR_READ additionally forms a
  no-analysis lambda that returns a reference to the guarded member for a read
  that intentionally happens without the lock.

why this is a real, reviewed-transliteration decision (not cosmetic drop):
  - The port has NO thread-safety-annotation macros: `grep -rn TSA_/GUARDED_BY velox/ch`
    returns nothing; Guards.h and ShardedMap.h carry guarded members with the
    annotations already removed, but that removal lives inside their own accepted
    diffs, not in any citable mapping row.
  - folly ships no drop-in GUARDED_BY/REQUIRES macro
    (grep of deps-download/folly for `define .*GUARDED_BY` is empty); velox/common/base
    exposes no such macro either. So there is no existing shim to reuse.
  - The annotation's PRESENCE is part of a reviewed contract:
    `port/task/fullreview/root-oss/1/evidence/011-012-consumer-contract-ledger.md:301`
    (ST7) cites `weak_ptr<Entry> entry (TSA_GUARDED_BY entry_mutex)` as an approved
    SLRU-iterator identity invariant. Whether Task 011 preserves the annotation
    (mapped to Clang/folly capability attributes) or drops it is therefore a
    contract-relevant transliteration choice, which the gate reserves for user review.

candidate Velox mappings (NOT chosen — for the user to pick):
  (a) Drop all TSA_* annotations (matching the de-facto Guards.h/ShardedMap.h choice);
      TSA_SUPPRESS_WARNING_FOR_READ(x) becomes plain `(x)`.
  (b) Add a port-wide no-op shim header (e.g. Common/ThreadSafetyAnnotations.h) that
      defines TSA_GUARDED_BY/TSA_REQUIRES/TSA_SUPPRESS_WARNING_FOR_READ as empty /
      identity, preserving the CH text verbatim.
  (c) Map to real Clang capability attributes so -Wthread-safety stays active in Velox.
  Note: options (b) and (c) would add a file OUTSIDE Task 011's declared 11-file scope
  (a new Common shim header), which is itself a scope decision only the Controller/user
  can authorize.

decision needed:
  Approve one TSA_* mapping (drop / no-op shim / real Clang attributes) AND, if a shim
  header is chosen, authorize the out-of-scope file it requires. Record it in the
  canonical design and the Task-011 amendment, then redispatch Task 011 from the
  source-contract check.

first actionable error (single, exact):
  The first source to write is IFileCachePriority.h (clean under the approved rules),
  but the next required headers LRUFileCachePriority.h:181 and SLRUFileCachePriority.h:219
  cannot be transliterated without a reviewed decision for TSA_GUARDED_BY /
  TSA_REQUIRES / TSA_SUPPRESS_WARNING_FOR_READ. Writing them would require choosing a
  spelling or a shim, which the unreviewed-dependency gate forbids.
```

## Worker review

```text
review subagent: not launched (no diff exists to review; blocked before writing sources)
findings: n/a
resolutions: n/a
unresolved findings: n/a
```

## Worker declaration

```text
Only Task 011 was attempted.
No Velox changes were made; nothing is staged or committed in either repository.
The only ClickHouse-repo change is this appended receipt section.
The worker stopped after writing this receipt.
Recommended next step per protocol: the Controller escalates the single new
unreviewed dependency D-011-7 (TSA_* thread-safety annotation macros) for user
review, records the approved mapping (and any out-of-scope shim authorization) into
the canonical design and the Task-011 amendment, then redispatches Task 011 from the
source-contract check. The seven previously-approved mappings need no further review.
```

## Controller unblock response 2 (D-011-7 TSA macros)

```text
controller_status: blocker_resolved
task: 011
resolved: 2026-07-20
```

## Resolution

```text
root cause:
  Transliterating the LRU/SLRU priority headers requires CH's Clang
  thread-safety annotation macros TSA_GUARDED_BY / TSA_REQUIRES /
  TSA_SUPPRESS_WARNING_FOR_READ (base/defines.h:58-67), used at 8 sites. No
  reviewed Velox mapping existed; folly has no drop-in. The macro presence is
  contract-relevant (ledger ST7: lock-to-data relationship). Worker correctly
  stopped at the gate. Controller confirmed: CH defines them as standard Clang
  __attribute__((guarded_by(...))) etc.; absent from velox/ch and folly.

decision (user-reviewed 2026-07-20):
  CREATE velox/ch/Common/ClickHouseTSA.h with the CH TSA_* macros copied
  verbatim (standard Clang attributes; compile-time only, zero runtime effect).
  - Clang honors them -> the CH lock-to-data static thread-safety check (ST7)
    is preserved 1:1.
  - GCC (the home-chang cmake-build-debug-gcc13 compiler) ignores these
    attributes without error, so the build passes on the current toolchain.
  Do NOT define them empty (that would drop the Clang check). Owner: Task 011
  (added to its file scope).

task update:
  Task 011 file scope now creates velox/ch/Common/ClickHouseTSA.h; the approved
  mappings table has a new D-011-7 row.

evidence:
  CH macros base/defines.h:58-67; uses LRUFileCachePriority.h:181,182,186,187,
  193 + .cpp:856,857 + SLRUFileCachePriority.h:219. Not in velox/ch or folly.

redispatch same task: yes
```

## Worker attempt 3

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 011
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `d93fa99e5` (`Task 003: Add ProfileEvents/CurrentMetrics enumerator surface (B1/B2)`) | clean working tree; branch ahead 1 of `baibaichen/filecache` |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache` | `da28e83e8b3` (`Task 010: Accept FileCache settings migration`) | clean (only this appended receipt) |

All eight approved mappings (D-011-1..D-011-7 plus the CacheUsage.h OvercommitUsers
conflict resolution) are now in the task file. No new unmapped CH dependency was
reached; the unreviewed-dependency gate did not fire. Sources were written.

## Files changed

```text
velox/ch/Common/ClickHouseTSA.h
velox/ch/Interpreters/FileCache/CacheUsage.h
velox/ch/Interpreters/FileCache/IFileCachePriority.h
velox/ch/Interpreters/FileCache/IFileCachePriority.cpp
velox/ch/Interpreters/FileCache/LRUFileCachePriority.h
velox/ch/Interpreters/FileCache/LRUFileCachePriority.cpp
velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h
velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp
velox/ch/Interpreters/FileCache/SplitFileCachePriority.h
velox/ch/Interpreters/FileCache/SplitFileCachePriority.cpp
velox/ch/Interpreters/FileCache/EvictionCandidates.h
velox/ch/Interpreters/FileCache/EvictionCandidates.cpp
```

Exactly the 12 declared files, all new/untracked. No CMake/test files touched. No
ClickHouse source touched.

## Applied dependency mappings

```text
D-011-1 ProfileEvents timer -> ProfileEventTimeIncrement<Microseconds> no-op RAII
        (EvictionCandidates.cpp evict()), documented in-code.
D-011-2 randomSeed()/pcg64   -> queue_id = folly::Random::rand64();
        shuffle via folly::ThreadLocalPRNG + std::shuffle (LRUFileCachePriority.cpp).
D-011-3 LockMemoryExceptionInThread(Global) -> omitted (addInvalidatedRef and the
        removeInvalidatedEntries catch path); surrounding logic unchanged, noted in-code.
D-011-4 WriteBufferFromOwnString -> std::ostringstream (both toString bodies).
D-011-5 thread_local_rng + bernoulli in failpoint -> dead under FAIL_POINT_TRIGGER
        no-op; no live RNG emitted (SLRUFileCachePriority.cpp).
D-011-6 assert_cast -> file-local dynamic_cast+VELOX_CHECK (debug/sanitizer) /
        static_cast (release) helper in SLRUFileCachePriority.cpp.
D-011-7 TSA_* macros -> velox/ch/Common/ClickHouseTSA.h copied verbatim from
        base/base/defines.h:56-85 (non-empty; Clang honors, GCC ignores).
        Included by LRU/SLRU headers where TSA_* are used.
CacheUsage.h OvercommitUsers -> dropped (:16 extern + :116 Increment member).
```

Structural transliteration notes: DB -> facebook::velox::ch; String/UInt* -> Task-003
aliases; Exception/ErrorCodes -> throwFileCacheException; chassert -> ClickHouseAssert
shim; absl flat map/set -> folly::F14FastMap/F14FastSet with explicit hash
(FileCacheKeyHash for the candidates map); CurrentMetrics/ProfileEvents/logging ->
no-op shims; boost::noncopyable -> deleted copy ops; magic_enum -> explicit exhaustive
switch (Entry::stateName, IFileCachePriority::typeName). SD5 preserved
(LRUQueue = std::list<EntryPtr>, splice/iterator stability). SD2 F14 invariant honored:
EvictionInfo value is unique_ptr (stable pointee); no mapped-value ref/iterator/address
retained across a same-map mutation. Overcommit/cloud/system-table code not ported; no
fake center-SCC definitions.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| `git status/log` baselines (read-only) | 0 | n/a (captured above) |
| Structural parity checks (Step 7: symbol grep + fake-SCC grep + `git diff --check`) | 0 | `cmake-build-debug-gcc13/check_task_011_priority_symbols.log` (regenerated inline) |
| `git status --short` scope check (Step 8) | 0 | n/a (12 files, all untracked) |

No configure/build/test commands were run. Task 011 has no build gate by design; the
green compile/link belongs to Task 012 (atomic SCC batch).

## Acceptance evidence

```text
test count: n/a (no tests in this task; Task 012 owns tests + CMake)
failed tests: n/a
skipped/disabled tests: n/a
benchmark result: n/a
structural/source-contract self-check:
  - required symbols all found (enum class EvictionCursor, class HoldSpace,
    class EvictionInfo, class EvictionCandidates, removeQueueEntries,
    requiresAfterEvictWrite, requiresAfterEvictState, addForRestore,
    is_total_space_cleanup) — nonzero match counts for each.
  - no fake SCC definition found (grep for *stub patterns empty).
  - git diff --check: clean (no whitespace errors).
  - scope: exactly the 12 declared files changed, nothing else.
No build claimed; Task 012 closes the atomic SCC batch.
```

## Worker review

```text
review subagent: pr-review-toolkit:code-reviewer (read-only), one launch over the
  complete Task-011 diff, given Required-replacements, all eight approved mappings,
  the structural deviations (SD2/SD5), the exact invariants, CH source paths, and the
  TSA macro source.
findings: none actionable. Verdict: faithful transliteration. Verified TSA byte-fidelity
  and header includes; SD5 std::list preservation; F14 SD2 invariant (no surviving
  mapped-value ref/iterator across same-map mutation; unique_ptr-stable EvictionInfo
  values; addImpl copies scalars before try_emplace); all eight mappings applied;
  algorithm/branch/lock/ordering fidelity (afterEvictWrite before afterEvictState;
  LRU total-space-cleanup min(requested,current); SLRU PreActive + addForRestore
  routing; Split four-limit + rollback on failed System resize; EvictionInfo aggregate
  + takeKeptAliveCacheUsage on merge); CacheUsage.h OvercommitUsers drop with nothing
  else material dropped. No over-port / scope creep.
  Informational non-issues (no action): dropped OvercommitFileCachePriority friend
  template and FileCacheTest_MoveEvictionPos_Test friend (out-of-scope cloud/test hooks;
  OVERCOMMIT enum surface retained); trailing `return false;` after the exhaustive
  switch in LRU iterateImpl's is_evictable_state lambda (avoids -Wreturn-type; behavior
  identical); candidates map uses FileCacheKeyHash where CH used std::hash<FileCacheKey>
  (explicit hash per the approved F14 rule).
resolutions: no actionable in-scope findings required changes.
unresolved findings: none.
```

## Blockers

```text
None. All eight dependency mappings were applied; no new unmapped CH dependency was
reached. Structural/source-contract self-check passed.
```

## Worker declaration

```text
Only Task 011 was attempted.
Changes are unstaged and uncommitted in both repositories.
The 12 declared Velox sources are new/untracked; the only ClickHouse-repo change is
this appended receipt section.
No build was claimed; Task 012 closes the atomic SCC batch.
The worker stopped after writing this receipt.
Recommended next task: Task 012 immediately (registers all priority/core sources in
CMake, adds the real center-SCC types, and runs the green build gate
velox_ch_filecache_core_scc_test).
```

## Controller review (attempt 3, accepted)

```text
controller_status: accepted
environment_profile: home-chang
task: 011
reviewed: 2026-07-20
```

## Review evidence

```text
scope review:
  git status = exactly 12 new untracked files under velox/ch/ (11 priority/
  eviction sources + Common/ClickHouseTSA.h); no tracked file modified; no CMake
  or test file touched (Task 012 owns them); git diff --check clean.

mapping fidelity (independently verified from source):
  D-011-1 ProfileEventTimeIncrement<Microseconds> no-op RAII @ EvictionCandidates.cpp:273.
  D-011-2 folly::Random::rand64() queue_id @ LRU.cpp:82; folly PRNG + std::shuffle
          body matches CH pcg64/randomSeed shuffle; no CH randomSeed/pcg64 residue.
  D-011-3 LockMemoryExceptionInThread omitted (documented no-op @ LRU.cpp:202,305).
  D-011-4 std::ostringstream @ EvictionCandidates.cpp:43,80.
  D-011-5 failpoint bernoulli reduced to no-op @ SLRU.cpp:598.
  D-011-6 local assert_cast = dynamic_cast + VELOX_CHECK_NOT_NULL; call-site split
          (assert_cast vs plain dynamic_cast) matches CH exactly.
  D-011-7 velox/ch/Common/ClickHouseTSA.h copied VERBATIM from CH base/defines.h
          (guarded_by / requires_capability / SUPPRESS_WARNING_FOR_READ lambda form).
  CacheUsage.h FilesystemCacheOvercommitUsers member DROPPED (CH:16,116 gone).

structural transliteration (§3) — independent Controller review subagent:
  CT1/SD5 LRUQueue stays std::list; ST7 SLRUIterator external-iterator +
  entry_mutex + weak_ptr<Entry> TSA_GUARDED_BY + atomic is_protected intact;
  ST1 atomic Entry::State machine + lock-token'd setters byte-identical;
  ZI1/ZI2 zero-size accounting throw-conditions match; ZI4 EvictionCandidates::
  evict takes no lock params (runs lock-free); ZI5 Split rollback cannot-throw
  structure matches. F14 (SD1/SD2): absl->folly F14/F14Set, original_queue_types
  correctly kept std::unordered_map; no mapped-value ref/iterator/address escapes.

over-port review:
  No OvercommitFileCachePriority, cloud/distributed-cache branch, or SQL/
  system-table code anywhere. Overcommit friend decl dropped; the
  "0, // Overcommit available only for CH Cloud" literal arg preserved (no branch).

cross-task architecture review:
  Atomic-stage contract honored: Task 011 wrote sources only, no CMake/test/build.
  Task-012-owned missing center-SCC types are expected and not defects. Only
  benign addition = unreachable dead-code guards after exhaustive enum switches
  (replacing CH -Werror exhaustiveness reliance).

log and test review:
  N/A — Task 011 has no build gate by design (structural/source-contract check).
  The green build is Task 012's gate.

unresolved findings:
  None.
```

## Required changes

```text
None. Accepted.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `e5b2af1a9 Task 011: Port priority/eviction sources (center-SCC Part A)` |
| `/home/chang/SourceCode/ClickHouse` | (this receipt + handoff commit) |
