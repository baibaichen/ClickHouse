# Task 011: Port Priority/Eviction Sources (Center-SCC Part A)

> **Atomic batch rule:** Task 011 and Task 012 are one implementation stage.
> Task 011 writes the exact priority/eviction sources but does not register or
> compile them. Run Task 012 immediately afterward in the same Velox worktree;
> Task 012 adds the mutually dependent core files, tests, CMake registration,
> and the only green build for this stage.

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies only `/home/chang/OpenSource/velox` plus its result handoff. Do not
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
source migration and structural verification.

## Prerequisites

```text
Tasks 003-010 completed in the same Velox worktree.
Velox branch: filecache
Task 012 is ready to run immediately after this task.
```

Read:

```text
port/task/ENVIRONMENT.md
port/2-file-cache/07-filecache-priority-eviction-design.md
port/2-file-cache/10-filecache-core-files-design.md
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
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/CacheUsage.h
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/IFileCachePriority.h
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/IFileCachePriority.cpp
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/LRUFileCachePriority.h
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/LRUFileCachePriority.cpp
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/SLRUFileCachePriority.h
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/SLRUFileCachePriority.cpp
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/SplitFileCachePriority.h
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/SplitFileCachePriority.cpp
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/EvictionCandidates.h
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/EvictionCandidates.cpp
/home/chang/SourceCode/ClickHouse/port/task/result/011-filecache-priority-eviction-result.md
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

Do not copy:

```text
OvercommitFileCachePriority
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
cd /home/chang/OpenSource/velox
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

Keep the full public interface, including:

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
CacheUsage snapshot/getOrSet/touch/collectIdleClients
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

```bash
cd /home/chang/OpenSource/velox

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
    >> cmake-build-debug-gcc13/check_task_011_priority_symbols.log
done

if rg -n 'FileCacheReserveStat.*stub|struct KeyMetadata.*stub|class FileCache.*stub' \
  velox/ch/Interpreters/FileCache
then
  echo "ERROR: fake SCC definitions found"
  exit 1
fi

git --no-pager diff --check
```

Expected:

```text
Every required symbol is found.
No fake SCC definition is found.
No whitespace error is reported.
```

Do not run or claim a priority build in Task 011. The green build belongs to
Task 012.

- [ ] **Step 8: Inspect task-owned files**

```bash
cd /home/chang/OpenSource/velox
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
/home/chang/SourceCode/ClickHouse/port/task/result/011-filecache-priority-eviction-result.md
```

Include:

```text
status: success
Velox branch/HEAD/dirty status
files created
structural check command and log
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
