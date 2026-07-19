# Task 011: Port Priority/Eviction Sources (Center-SCC Part A)

> **Atomic batch rule:** Task 011 and Task 012 are one implementation stage.
> Task 011 writes the exact priority/eviction sources but does not register or
> compile them. Run Task 012 immediately afterward in the same Velox worktree;
> Task 012 adds the mutually dependent core files, tests, CMake registration,
> and the only green build for this stage.

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies only `<velox_repo>` plus its result handoff. Do not
> modify ClickHouse source. Do not stage or commit either repository.

## Goal

Port the real ClickHouse priority and eviction files without fake
`FileCache`, `KeyMetadata`, `FileSegmentInfo`, or `FileCacheReserveStat`
definitions:

```text
CacheUsage.h
IFileCachePriority.h / .cpp
LRUFileCachePriority.h / .cpp
SLRUFileCachePriority.h / .cpp
SplitFileCachePriority.h / .cpp
EvictionCandidates.h / .cpp
```

These `.cpp` files include or call center-SCC types, so compiling them before
Task 012 would require false stubs. Task 011 intentionally stops after exact
source migration and structural verification. Task 011 has no test target and
no compile closure of its own; both belong to Task 012, which links these
sources into `velox_ch_filecache_core` and exercises them through
`velox_ch_filecache_core_scc_test`.

## Dependency pre-check (stop at the gate if any row is unmet)

Before editing, confirm every external name this task reaches already has an
explicit reviewed mapping. If any row below is not yet true in the Velox
worktree, stop as `blocked` and report it; do not guess a mapping.

| External name | Approved mapping | Source of approval |
|---|---|---|
| Task-003 B1 `ProfileEvents` name surface | `velox/ch/Common/ProfileEvents.h` contains all 31 required names, no-op | Task 003 corrective acceptance (must be `accepted`, not merely `reopened_by_contract_audit`) before this task starts |
| Task-003 B2 `CurrentMetrics` name surface | `velox/ch/Common/CurrentMetrics.h` contains the 5 required names, no-op | Task 003 corrective acceptance |
| `DB` namespace | `facebook::velox::ch` | `port/1-dependencies/01-filecache-infra-mapping.md` |
| `String`/`UInt*` aliases | Task-003 `ClickHouseAliases.h` | Task 003 result |
| `Exception`/`ErrorCodes`/`chassert` | `VELOX_FAIL`/`VELOX_CHECK`/`velox/ch/Common/ClickHouseAssert.h` | Task 003 corrective acceptance |
| `absl::flat_hash_map`/`flat_hash_set` | `folly::F14FastMap`/`F14FastSet` with explicit hash (SD2 below) | `port/task/fullreview/cross-profile/1/003-010-review-decisions.md` Task-011 contract decisions |
| `boost::noncopyable` | deleted copy constructor/assignment | `port/1-dependencies/01-filecache-infra-mapping.md` |
| `magic_enum` | explicit exhaustive `switch` | `port/1-dependencies/01-filecache-infra-mapping.md` |
| Task-009 `ShardedMap`/`F14FastMap` no-reference-escape contract (SD1) | approved, unrelated to this task's own containers but binding on any reused shard type | cross-profile decisions, "Profile reconciliation" table |

Do not start Task 011 while Task 003's B1/B2 corrective work is only
`reopened_by_contract_audit`; that status means the required no-op name
surfaces are not yet present.

## Consumer-contract excerpts (CH source, real callers, file:line)

Task 011 is a source migration, but every port decision below is anchored to
real CH definitions and their real callers, not to a leaf-header guess.

### `CacheUsage` — non-overcommit subset only

```text
src/Interpreters/FileCache/CacheUsage.h:32-37   CacheUsageStatGuard (mutex + Lock)
src/Interpreters/FileCache/CacheUsage.h:41-75   CacheUsage: origin_info, priority
                                                 (non-owning), guard, update,
                                                 total_size/total_elements,
                                                 touch/idleFor, comparisons,
                                                 lessWithAssumption
src/Interpreters/FileCache/CacheUsage.h:79-122  CacheUsagePerUser: snapshot,
                                                 tryGet, getOrSet, touchClient,
                                                 collectIdleClients,
                                                 CacheUserData with
                                                 CurrentMetrics::Increment on
                                                 FilesystemCacheOvercommitUsers
```

Real consumers of the base `CacheUsage` type reachable from the center SCC:

```text
src/Interpreters/FileCache/EvictionCandidates.h:92   addCacheUsage(CacheUsagePtr)
src/Interpreters/FileCache/EvictionCandidates.h:97   takeKeptAliveCacheUsage(EvictionInfo &)
src/Interpreters/FileCache/EvictionCandidates.h:111  kept_alive_cache_usage (flat_hash_set<CacheUsagePtr>)
src/Interpreters/FileCache/IFileCachePriority.h:456  virtual setCacheUsageStatGuard(...) {} (no-op base hook)
src/Interpreters/FileCache/IFileCachePriority.h:460  virtual touchClientAccess(const UserID &) {} (no-op base hook)
src/Interpreters/FileCache/IFileCachePriority.h:463  virtual collectIdleClients(...) const { return {}; } (no-op base hook)
```

`CacheUsagePerUser` (`CacheUsage.h:79-122`) has no reachable caller in the
Task-011/012 center SCC: nothing in `IFileCachePriority`, `LRUFileCachePriority`,
`SLRUFileCachePriority`, `SplitFileCachePriority`, or `EvictionCandidates`
constructs, stores, or calls it. Its only consumer is
`OvercommitFileCachePriority`, which is explicitly excluded from this port.
Porting `CacheUsagePerUser` bodies here would be over-port: a behavior with no
in-scope caller.

**Mandatory scope line:** port only the `CacheUsage` struct, `CacheUsagePtr`,
and `CacheUsageStatGuard` from `CacheUsage.h`. Do not port `CacheUsagePerUser`,
its `CacheUserData`, or `CurrentMetrics::FilesystemCacheOvercommitUsers`. Base
`IFileCachePriority` no-op/throw hooks (`setCacheUsageStatGuard`,
`collectIdleClients`) are ported as no-ops exactly as CH declares them; a
priority subclass overriding them with real per-user logic is
`OvercommitFileCachePriority` and stays out of scope.

### Container structure-deviation registrations (binding on this task)

| CH structure | CH file:line | Velox replacement | Guarantee difference | Hard constraint | Approval |
|---|---|---|---|---|---|
| SD2: `EvictionInfo : absl::flat_hash_map<QueueID, QueueEvictionInfoPtr>` | `EvictionCandidates.h:56` | `folly::F14FastMap<QueueID, QueueEvictionInfoPtr>` member (not inherited) | F14 rehash may move values; no reference/iterator may survive a mutating call | none of `EvictionInfo`'s own call sites retain a reference across mutation (verified structurally in Step 7) | cross-profile decisions, Task-011 contract |
| SD2: `kept_alive_cache_usage: absl::flat_hash_set<CacheUsagePtr>` | `EvictionCandidates.h:111` | `folly::F14FastSet<CacheUsagePtr>` | none observed (`shared_ptr` values, no address escape) | n/a | cross-profile decisions |
| SD2: `candidates: absl::flat_hash_map<FileCacheKey, KeyCandidates, ...>` | `EvictionCandidates.h:187` | `folly::F14FastMap<FileCacheKey, KeyCandidates, FileCacheKeyHash>` | same as above | n/a | cross-profile decisions |
| not a deviation: `original_queue_types: std::unordered_map<const FileSegmentMetadata *, IFileCachePriority::QueueEntryType>` | `EvictionCandidates.h:193` | remains `std::unordered_map` | none; explicitly excluded from the F14 mapping | n/a | cross-profile decisions: "Map only CH `absl::flat_hash_map/set` containers to F14. Keep `original_queue_types` as `std::unordered_map`." |
| SD5: `LRUQueue = std::list<EntryPtr>` | `LRUFileCachePriority.h:169` | remains `std::list` | n/a (no deviation) | n/a | cross-profile decisions |
| SD5: `LRUQueue = std::list<Entry>` | `SLRUFileCachePriority.h:154` | remains `std::list` | n/a (no deviation) | n/a | cross-profile decisions |

Any container substitution not in this table (including a node-based F14
variant, or any container swap in `SplitFileCachePriority` or the priority
base class) is out of scope for this task; stop at the dependency gate instead
of improvising one.

### Typed-subtype guidance for reserve/eviction failures

The priority/eviction sources migrated here throw only `ErrorCodes::LOGICAL_ERROR`
invariant violations (e.g. `LRUFileCachePriority.cpp:138,150,160,638,648,683,804,846,892,934,947`;
`SLRUFileCachePriority.cpp:152,162,797,806,812,902`; `IFileCachePriority.cpp:56,64,69`;
`EvictionCandidates.cpp:131,153,296`). Map every one of these to `VELOX_FAIL`
exactly as Task 003 requires; do not introduce a space-related subtype for
them.

The real call site that distinguishes an out-of-space condition from a logical
error is downstream, in `src/Interpreters/FileCache/WriteBufferToFileSegment.cpp:97`
(`ErrorCodes::NOT_ENOUGH_SPACE` when `tryReserve` fails). That call site is out
of this task's file scope (Task 014 territory), but if any Task-011-owned
function itself needs to report a distinguishable reserve/eviction failure
(bool return is insufficient, e.g. a caller must tell "reservation impossible"
from "internal invariant broken"), use a typed subtype rather than collapsing
both into one opaque `VELOX_FAIL`. Record any such call site explicitly in the
result receipt; do not add one silently.

## RED and false-green probe requirements (migration-only task)

Task 011 has no test binary, so its RED/false-green evidence is structural,
using the Step 7 symbol/grep checks:

```text
RED: before any file under velox/ch/Interpreters/FileCache/{CacheUsage.h,
  IFileCachePriority.*, LRUFileCachePriority.*, SLRUFileCachePriority.*,
  SplitFileCachePriority.*, EvictionCandidates.*} is created, every `rg`
  lookup in Step 7 finds zero matches. Record this pre-implementation grep
  output as the RED evidence.

false-green probe: after implementation, pick one required symbol from the
  Step 7 list (for example `requiresAfterEvictWrite`), delete just that
  declaration from the migrated source, rerun the same `rg` lookup, and
  confirm it now finds zero matches (the check fails as designed). Restore
  the declaration and rerun Step 7 to confirm it passes again. Record both
  grep outputs (missing and restored) in the result receipt. A Step-7 check
  that would still "pass" with a required symbol deleted is false-green and
  must be fixed before this task is accepted.
```

## Prerequisites

```text
Tasks 003-010 completed in the same Velox worktree.
Task 003's B1/B2 corrective work (velox/ch/Common/ProfileEvents.h and
  CurrentMetrics.h name surfaces) is accepted, not merely
  reopened_by_contract_audit. Verify by reading the accepted Controller
  review in port/task/result/003-filecache-basic-common-shims-result.md
  before starting.
Velox branch: filecache
Task 012 is ready to run immediately after this task.
```

Read:

```text
port/task/ENVIRONMENT.md
port/task/fullreview/cross-profile/1/003-010-review-decisions.md
port/2-file-cache/07-filecache-priority-eviction-design.md
port/2-file-cache/10-filecache-core-files-design.md
port/task/result/003-filecache-basic-common-shims-result.md
port/task/result/010-filecache-settings-result.md
```

Behavioral source of truth:

```text
src/Interpreters/FileCache/CacheUsage.h
src/Interpreters/FileCache/IFileCachePriority.h / .cpp
src/Interpreters/FileCache/LRUFileCachePriority.h / .cpp
src/Interpreters/FileCache/SLRUFileCachePriority.h / .cpp
src/Interpreters/FileCache/SplitFileCachePriority.h / .cpp
src/Interpreters/FileCache/EvictionCandidates.h / .cpp
```

## File scope

Create:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/CacheUsage.h
<velox_repo>/velox/ch/Interpreters/FileCache/IFileCachePriority.h
<velox_repo>/velox/ch/Interpreters/FileCache/IFileCachePriority.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/LRUFileCachePriority.h
<velox_repo>/velox/ch/Interpreters/FileCache/LRUFileCachePriority.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h
<velox_repo>/velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/SplitFileCachePriority.h
<velox_repo>/velox/ch/Interpreters/FileCache/SplitFileCachePriority.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/EvictionCandidates.h
<velox_repo>/velox/ch/Interpreters/FileCache/EvictionCandidates.cpp
<clickhouse_repo>/port/task/result/011-filecache-priority-eviction-result.md
```

Do not create or modify CMake/test files in this task. Task 012 owns them.

## Required replacements

Apply only reviewed infrastructure substitutions:

```text
DB namespace                         -> facebook::velox::ch
String/UInt*                         -> Task 003 aliases
Exception/ErrorCodes/chassert        -> VELOX_FAIL/VELOX_CHECK
absl flat maps/sets                  -> folly F14 with explicit hash
CurrentMetrics/ProfileEvents/logging -> existing compatible shims
boost::noncopyable                   -> deleted copy operations
magic_enum                           -> explicit exhaustive switch
```

The `CurrentMetrics`/`ProfileEvents` shims already exist and stay no-op (Task
003). The priority/eviction sources reference exactly this subset of names;
every one must already resolve from the Task-003 B1/B2 name surfaces:

```text
Task-011 ProfileEvents names:
  FilesystemCacheBackgroundRemovedInvalidatedEntries
  FilesystemCacheDowngradedFileSegments
  FilesystemCacheEvictionReusedIterator
  FilesystemCacheEvictionSkippedEvictingFileSegments
  FilesystemCacheEvictionSkippedFileSegments
  FilesystemCacheEvictionSkippedMovingFileSegments
  FilesystemCacheEvictionTries
  FilesystemCacheEvictMicroseconds
  FilesystemCacheFailedEvictionCandidates

Task-011 CurrentMetrics names:
  FilesystemCacheElements
  FilesystemCacheInvalidatedElements
  FilesystemCachePriorityQueueElements
  FilesystemCacheSize
```

If any of these names is missing from `velox/ch/Common/ProfileEvents.h` or
`velox/ch/Common/CurrentMetrics.h`, stop at the dependency gate: Task 003's
B1/B2 corrective work is not actually complete, regardless of its receipt
status.

Do not copy:

```text
OvercommitFileCachePriority
CacheUsagePerUser (snapshot/getOrSet/touchClient/collectIdleClients bodies)
CurrentMetrics::FilesystemCacheOvercommitUsers
Cloud-only distributed-cache branches
SQL/system-table presentation
```

## Exact invariants

The port is incomplete unless all of these are visible in the migrated source:

```text
zero-size queue entry:
  counts neither bytes nor elements until first positive increment

LRU:
  std::list entry storage
  reserve and background eviction cursors are independent
  invalidation is lazy and iterator-safe

SLRU:
  probationary + protected queues
  PreActive transition
  external SLRUIterator updated when an entry moves queues
  addForRestore restores original queue type

Split:
  General and Data route to Data
  System routes to System
  size and element limits both split by ratio
  failed System resize rolls Data limits back

EvictionInfo:
  map keyed by QueueID
  aggregate bytes/elements/holds
  keptAliveCacheUsage transferred on merge
  total-space cleanup uses min(requested,current)

EvictionCandidates:
  evict without priority/state locks
  afterEvictWrite before afterEvictState
  removeQueueEntries for resize
  requiresAfterEvictWrite/requiresAfterEvictState
  original queue type capture and lookup
  failed candidate accounting
```

## Steps

- [ ] **Step 1: Confirm the atomic-batch baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected:

```text
Branch is filecache.
Tasks 003-010 files are present.
Record pre-existing dirty files; do not overwrite them.
```

- [ ] **Step 2: Port `CacheUsage` and `IFileCachePriority`**

Migrate `CacheUsage.h` and `IFileCachePriority.h/.cpp` from the ClickHouse
sources line by line, applying only the replacements above.

From `CacheUsage.h`, port only the non-overcommit subset registered above:
`CacheUsageStatGuard`, `CacheUsage` (constructor, `update`, `total_size`,
`total_elements`, `touch`, `idleFor`, `operator<`, `operator==`,
`lessWithAssumption`), and `CacheUsagePtr`. **Do not** port `CacheUsagePerUser`,
its nested `CacheUserData`, or any `CurrentMetrics::FilesystemCacheOvercommitUsers`
reference; they have no reachable caller in this stage (see the consumer-contract
excerpts above) and belong to the excluded `OvercommitFileCachePriority`.

From `IFileCachePriority.h/.cpp`, keep the full public interface, including:

```text
Entry/Iterator/HoldSpace
QueueType/QueueEntryType
EvictionCursor/CollectStatus
collectEvictionInfo
collectCandidatesForEviction
add/addForRestore/tryIncreasePriority
removeInvalidatedEntries
modifySizeLimits
getSize/getElementsCount/getStateInfoForLog
setCacheUsageStatGuard/touchClientAccess/collectIdleClients (base no-op hooks only)
```

Use forward declarations for `FileSegment`, `FileSegmentInfo`,
`FileCacheReserveStat`, `KeyMetadata`, and `EvictionCandidates`, exactly as the
CH headers do. Do not define them here.

- [ ] **Step 3: Port `LRUFileCachePriority`**

Migrate `LRUFileCachePriority.h/.cpp` with the exact list/state/cursor
algorithms. Preserve both `collectEvictionInfo` branches:

```text
is_total_space_cleanup == true:
  target = min(requested,current)

otherwise:
  target = shortage relative to configured limits and held space
```

Do not replace `std::list` or either eviction cursor.

- [ ] **Step 4: Port `SLRUFileCachePriority`**

Migrate `SLRUFileCachePriority.h/.cpp`. Preserve `SLRUIterator` identity,
`entry_mutex`, `is_protected`, `PreActive`, promotion/downgrade and
`addForRestore`. Do not flatten the two queues.

- [ ] **Step 5: Port `SplitFileCachePriority`**

Migrate `SplitFileCachePriority.h/.cpp`. Compute all four limits:

```text
data bytes
data elements
system bytes
system elements
```

Preserve rollback if the second sub-priority resize throws.

- [ ] **Step 6: Port `EvictionCandidates`**

Migrate `EvictionCandidates.h/.cpp`. Keep its real dependencies on
`Metadata`/`FileSegment`; do not replace them with test doubles or local
structs. It is expected to remain unregistered until Task 012.

- [ ] **Step 7: Run structural parity checks**

First capture the RED baseline before any of these files exist (or, if
re-running after a partial attempt, on a clean checkout at the Step-1 HEAD):

```bash
cd <velox_repo>
for symbol in \
  'enum class EvictionCursor' \
  'class HoldSpace' \
  'class EvictionInfo' \
  'class EvictionCandidates' \
  'removeQueueEntries' \
  'requiresAfterEvictWrite' \
  'requiresAfterEvictState' \
  'addForRestore' \
  'is_total_space_cleanup'
do
  rg -n "$symbol" velox/ch/Interpreters/FileCache \
    >> <velox_build_dir>/check_task_011_priority_symbols_red.log
done
```

Expected: `check_task_011_priority_symbols_red.log` is empty (zero matches);
none of these symbols exist yet. This is the RED evidence.

Then, after implementing Steps 2-6, run the same checks for the final record:

```bash
cd <velox_repo>

for symbol in \
  'enum class EvictionCursor' \
  'class HoldSpace' \
  'class EvictionInfo' \
  'class EvictionCandidates' \
  'removeQueueEntries' \
  'requiresAfterEvictWrite' \
  'requiresAfterEvictState' \
  'addForRestore' \
  'is_total_space_cleanup'
do
  rg -n "$symbol" velox/ch/Interpreters/FileCache \
    >> <velox_build_dir>/check_task_011_priority_symbols.log
done

if rg -n 'FileCacheReserveStat.*stub|struct KeyMetadata.*stub|class FileCache.*stub' \
  velox/ch/Interpreters/FileCache
then
  echo "ERROR: fake SCC definitions found"
  exit 1
fi

if rg -n 'CacheUsagePerUser|FilesystemCacheOvercommitUsers' \
  velox/ch/Interpreters/FileCache
then
  echo "ERROR: excluded overcommit surface was ported"
  exit 1
fi

git --no-pager diff --check
```

Expected:

```text
Every required symbol is found.
No fake SCC definition is found.
No CacheUsagePerUser / FilesystemCacheOvercommitUsers reference is found.
No whitespace error is reported.
```

Finally, run the false-green mutation probe described above: delete the
`requiresAfterEvictWrite` declaration from the migrated `EvictionCandidates.h`,
rerun the symbol loop, confirm it no longer finds a match for that symbol,
save that output as `check_task_011_priority_symbols_mutated.log`, then
restore the declaration and rerun the final Step-7 command to confirm a clean
pass again.

Do not run or claim a priority build in Task 011. The green build belongs to
Task 012.

- [ ] **Step 8: Inspect task-owned files**

```bash
cd <velox_repo>
git --no-pager status --short
git --no-pager diff -- \
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
  velox/ch/Interpreters/FileCache/EvictionCandidates.cpp
```

Expected:

```text
Only declared files were changed by this task.
All changes remain unstaged and uncommitted.
```

- [ ] **Step 9: Write the atomic Part-A result**

Create:

```text
<clickhouse_repo>/port/task/result/011-filecache-priority-eviction-result.md
```

Include:

```text
status: success
Velox branch/HEAD/dirty status
files created
RED structural-check log path (pre-implementation, zero matches)
final structural-check log path (post-implementation, all matches found)
false-green mutation log path (one symbol deleted, check fails) and the
  restored/passing rerun
explicit statement of the CacheUsage scope actually ported (base CacheUsage +
  CacheUsageStatGuard only; CacheUsagePerUser and
  FilesystemCacheOvercommitUsers not ported)
explicit statement: "No build claimed; Task 012 closes the atomic SCC batch"
first actionable error if any
recommended next task: Task 012 immediately
```

If any priority source needs a fake core definition to proceed, set status to
`blocked`, report the dependency, and stop. Do not invent the definition.

## Explicit exclusions

```text
CMake registration and priority tests (Task 012)
FileSegmentInfo/FileSegment/Metadata/FileCache/QueryLimit (Task 012)
Factory/Manager (Task 013)
OvercommitFileCachePriority
```
