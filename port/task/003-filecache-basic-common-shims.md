# Task 003: Correct `FileCache` Basic Common Shims

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and appends
> corrective evidence to one result file under this ClickHouse checkout. Do not
> modify ClickHouse source files. Do not commit or stage either repository.

## Status and authority

The original Task 003 implementation was accepted, then reopened by the
post-acceptance source-contract audit. This document is the only executable
Task 003 plan. The original implementation instructions are intentionally not
retained here; the existing receipt preserves their result.

Task 012 must not start until this corrective task is implemented, reviewed, and
accepted.

### Second reopen (B1/B2 — 2026-07-20, home-chang)

The mandatory Tasks 003-010 whole-port review (artifacts under
`port/task/fullreview/root-oss/1/`, carried forward on `home-chang`) reopened
Task 003 a second time for the **`ProfileEvents` / `CurrentMetrics` enumerator
name surface only** (findings B1/B2). This is the sole 003-010 finding that
requires corrective Velox implementation before Tasks 011/012; every other
finding is an accepted deviation, a recorded sign-off, or a deferred task. The
exact B1/B2 contract is in `## Approved dependency decisions` →
`### ProfileEvents / CurrentMetrics enumerator surface (B1/B2)`. The receipt
carries a matching `## Corrective source-contract audit 3 (B1/B2 enum surface)`
section. Real event/metric counters remain Task 017; this reopen adds only the
enumerator **names** plus compile-coverage and false-green evidence.

## Approved dependency decisions

The corrective worker must apply these decisions exactly. Discovering another
unreviewed dependency or changing one of these decisions triggers the
unreviewed-dependency gate in `EXECUTION_PROTOCOL.md`: stop the whole pipeline,
record the dependency, and wait for user review.

### Finishable bounded queue

`FileCacheBoundedQueue<T>` must preserve the CH
`ConcurrentBoundedQueue<T>` contract used by `FileCache.cpp`:

```cpp
bool push(T value);
bool tryPush(const T & value, uint64_t timeoutMilliseconds = 0);
bool tryPush(T && value, uint64_t timeoutMilliseconds = 0);
bool pop(T & value);
bool tryPop(T & value);
bool tryPop(T & value, uint64_t timeoutMilliseconds);
void finish();
```

Semantics:

```text
push:
  wait for capacity or finish
  return false after finish

tryPush(value, 0):
  never wait
  return false when full or finished

tryPush(value, timeout > 0):
  wait up to timeout for capacity
  wake on pop or finish
  return false on timeout or finish

pop:
  wait for data or finish
  after finish, drain queued values before returning false

tryPop(value):
  never wait
  return false when empty

tryPop(value, timeout):
  wait up to timeout for data
  wake on push or finish
  after finish, drain queued values before returning false

finish:
  be idempotent
  reject new pushes
  wake every blocked producer and consumer
```

Capacity zero is valid. Non-blocking pushes always fail; blocking/timed
producers wait only until timeout or `finish`.

When assigning the front element to the caller:

```text
if T has noexcept move-assignment:
  move
otherwise:
  copy
```

Do not retain the current fallback that performs a potentially-throwing move
when `T` is not copy-assignable. CH deliberately rejects that case to keep a
failed assignment from damaging the element that remains in the queue.

### `chassert`

Add the independent public header:

```text
velox/ch/Common/ClickHouseAssert.h
```

Do not place the macro in `ClickHouseAliases.h`, and do not map it to
`VELOX_DCHECK`, `VELOX_CHECK`, or standard `assert`.

Exact contract:

```text
!defined(NDEBUG) || defined(FOLLY_SANITIZE):
  evaluate the expression exactly once
  on failure log the expression text or explicit diagnostic message
  abort; do not throw

ordinary Release:
  evaluate neither the expression nor the diagnostic message
  retain compile-time expression checking through sizeof
```

Include `folly/CPortability.h` and use Folly's normalized `FOLLY_SANITIZE`
macro. Preserve both call shapes:

```cpp
chassert(expression);
chassert(expression, "diagnostic message");
```

### Exception categories

Do not introduce a `FileCacheErrorKind` or a universal CH exception adapter.
Map each migrated CH throw site according to its category:

```text
ErrorCodes::BAD_ARGUMENTS  -> VELOX_USER_FAIL
ErrorCodes::LOGICAL_ERROR -> VELOX_FAIL
```

`throwFileCacheException` remains a runtime/logical-error convenience helper.
It must not be used for `BAD_ARGUMENTS`.

### Filesystem exceptions

Follow Velox local-filesystem conventions:

```text
throw Velox exceptions
include operation, path, numeric error, and message in diagnostic text
do not add a structured errno exception
do not parse exception text to drive control flow
```

The approved Task 007 writer contract handles a `WriteFile::append` exception by
reconciling the physical file size against:

```text
downloadedSize <= physicalSize <= reservedSize
```

and then propagating the original exception. If a future caller needs to branch
on errno outside that reviewed path, stop at the dependency gate.

### Logger placeholder

First-phase logging remains no-op, but `getLogger` must return a non-null,
name-only object because `FileCache.cpp` calls `log->name()` outside logging
macros.

Required public shape:

```cpp
class FileCacheLogger
{
public:
    explicit FileCacheLogger(std::string name);
    const std::string & name() const;

private:
    std::string name_;
};

using LoggerPtr = std::shared_ptr<FileCacheLogger>;

LoggerPtr getLogger(std::string_view name);
std::string getCurrentExceptionMessage(bool withStackTrace = false);
void tryLogCurrentException(...) noexcept;
```

`LOG_TEST`, `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, and
`LOG_ERROR` must not evaluate any argument in this phase.

`getCurrentExceptionMessage` deliberately continues to return an empty string,
and `tryLogCurrentException` remains no-op. Real exception formatting is an
explicit deferred deliverable in Task 017.

### `ProfileEvents` / `CurrentMetrics` enumerator surface (B1/B2)

Tasks 011/012 reference `ProfileEvents::Event` and `CurrentMetrics::Metric`
enumerators that the current shims do not define, so the center-SCC will not
compile. Task 011 cannot edit these headers (out of its file scope) and must not
create fake enumerators. This reopen adds the missing enumerator **names** as
no-op shim surface. `increment`, `add`, `sub`, and the RAII helpers stay no-op;
real counters remain Task 017.

**B1 — add these 31 `ProfileEvents::Event` enumerators** to
`velox/ch/Common/ProfileEvents.h` (referenced by `src/Interpreters/FileCache/`,
absent from the current 16-name enum). Keep the existing enumerators; do not
remove the three `CachedReadBuffer*` names (they belong to Task 014, harmless):

```text
FileSegmentFailToIncreasePriority
FileSegmentHolderCompleteMicroseconds
FileSegmentIncreasePriorityMicroseconds
FileSegmentLockMicroseconds
FilesystemCacheBackgroundDownloadQueuePush
FilesystemCacheBackgroundEvictedBytes
FilesystemCacheBackgroundEvictedFileSegments
FilesystemCacheBackgroundRemovedInvalidatedEntries
FilesystemCacheCreatedKeyDirectories
FilesystemCacheDowngradedFileSegments
FilesystemCacheEvictedBytes
FilesystemCacheEvictedFileSegments
FilesystemCacheEvictionReusedIterator
FilesystemCacheEvictionSkippedEvictingFileSegments
FilesystemCacheEvictionSkippedFileSegments
FilesystemCacheEvictionSkippedMovingFileSegments
FilesystemCacheEvictionTries
FilesystemCacheEvictMicroseconds
FilesystemCacheFailedEvictionCandidates
FilesystemCacheFailToReserveSpaceBecauseOfCacheResize
FilesystemCacheFreeSpaceKeepingThreadErrors
FilesystemCacheFreeSpaceKeepingThreadRun
FilesystemCacheFreeSpaceKeepingThreadWorkMilliseconds
FilesystemCacheHoldFileSegments
FilesystemCacheIdleClientEvictions
FilesystemCacheInvalidatedEntriesCleanupThreadWorkMilliseconds
FilesystemCacheLoadMetadataMicroseconds
FilesystemCacheLockKeyMicroseconds
FilesystemCacheLockMetadataMicroseconds
FilesystemCacheLockOriginPoolMicroseconds
FilesystemCacheUnusedHoldFileSegments
```

Note: `FilesystemCacheHoldFileSegments` is both an `Event` (here) and a
`Metric` (already defined in `CurrentMetrics.h`) in CH — both are required.

**B2 — add these 5 `CurrentMetrics::Metric` enumerators** to
`velox/ch/Common/CurrentMetrics.h` (the hard compile-blockers referenced via
`add`/`sub` on the in-scope path):

```text
FilesystemCacheElements
FilesystemCacheInvalidatedElements
FilesystemCachePriorityQueueElements
FilesystemCacheSize
FilesystemCacheKeys
```

**Do NOT add** (out of scope; adding them is over-port):

```text
FilesystemCacheEvictionThreads          # only ThreadPool ctor args at
FilesystemCacheEvictionThreadsActive    #   FileCache.cpp:583-586; the accepted
FilesystemCacheEvictionThreadsScheduled #   FileCacheThreadPool ctor takes no
                                        #   metric params, so Task 012 drops
                                        #   these args — never referenced.
FilesystemCacheOvercommitUsers          # excluded overcommit surface (O2).
```

**RED coverage (mandatory):** add one compile-coverage test (translation unit or
`static_assert`) that references every center-SCC-required event and metric name.
It must fail to compile against the pre-change 16-event / 6-metric enums.
Concrete RED seeds: `ProfileEvents::increment(ProfileEvents::FilesystemCacheEvictedFileSegments)`
and `CurrentMetrics::add(CurrentMetrics::FilesystemCacheSize, 1)`.

**False-green probe (mandatory):** delete one already-present name (e.g. event
`FileSegmentWriteMicroseconds` or metric `CacheFileSegments`) and prove the
coverage test goes RED — showing the test checks the enum surface rather than
trivially passing.

## Goal

Correct the already-present Task 003 shims so later `FileCache` tasks receive:

```text
the complete CH queue API and state-machine behavior
the exact CH chassert build-mode behavior
correct Velox user/runtime exception classification
Velox-style filesystem failure handling
a non-null name-only logger without eager formatting
```

The deliverables are:

```text
velox_ch_common_test
velox_ch_chassert_release_probe
velox_ch_chassert_sanitizer_gate_test
```

## Starting point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected HEAD:    4b14de7f1 or a direct descendant created by the user
```

Do not require a clean worktree and do not overwrite unrelated changes. Stop if
the branch is not `filecache`. Record all pre-existing dirty files before
editing.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/task/EXECUTION_PROTOCOL.md
<clickhouse_repo>/port/1-dependencies/01-filecache-infra-mapping.md
<clickhouse_repo>/port/1-dependencies/02-filecache-basic-shims-design.md
<clickhouse_repo>/port/task/result/003-filecache-basic-common-shims-result.md
```

Use these CH files as behavioral sources of truth:

```text
<clickhouse_repo>/src/Common/ConcurrentBoundedQueue.h
<clickhouse_repo>/base/base/MoveOrCopyIfThrow.h
<clickhouse_repo>/base/base/defines.h
<clickhouse_repo>/src/Interpreters/FileCache/FileCache.cpp
```

## File scope

Modify in the Velox checkout:

```text
<velox_repo>/velox/ch/Common/CMakeLists.txt
<velox_repo>/velox/ch/Common/FileCacheBoundedQueue.h
<velox_repo>/velox/ch/Common/FileCacheException.h
<velox_repo>/velox/ch/Common/FileCacheFilesystem.h
<velox_repo>/velox/ch/Common/logger_useful.h
<velox_repo>/velox/ch/Common/ProfileEvents.h
<velox_repo>/velox/ch/Common/CurrentMetrics.h
<velox_repo>/velox/ch/Common/tests/CMakeLists.txt
<velox_repo>/velox/ch/Common/tests/BasicShimsTest.cpp
```

Create in the Velox checkout:

```text
<velox_repo>/velox/ch/Common/ClickHouseAssert.h
<velox_repo>/velox/ch/Common/tests/ChassertReleaseProbe.cpp
<velox_repo>/velox/ch/Common/tests/ChassertSanitizerGateTest.cpp
```

Append a corrective section in the ClickHouse checkout:

```text
<clickhouse_repo>/port/task/result/003-filecache-basic-common-shims-result.md
```

Every new Velox C++ file must use the repository's Apache 2.0 license header and
Allman-style braces.

## Corrective steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

Run:

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Confirm the branch and expected ancestry. Record the exact HEAD and every
pre-existing dirty file in the corrective receipt before editing.

- [ ] **Step 2: Add focused RED coverage**

Extend `BasicShimsTest.cpp` with tests for every row below:

| Area | Required proof |
|---|---|
| Queue API | `tryPush(batch, 10)` and `tryPop(batch)` compile with the exact Task 012 call shapes |
| Timed push wakeup | a timed producer remains pending while full and succeeds after `pop` frees capacity |
| Timed push timeout | a full queue returns false only after the requested timeout |
| Non-blocking pop | `tryPop(value)` returns false immediately on an empty queue |
| Timed pop push wakeup | a timed consumer succeeds after `push` |
| Timed pop finish wakeup | a timed consumer returns false after `finish` |
| Capacity zero | non-blocking push fails; blocked/timed producers are released by `finish` |
| Drain | after `finish`, queued values are returned in FIFO order before false |
| Rejected push | every push form returns false after `finish` |
| Move assignment | a type with noexcept move-assignment is moved from the queue |
| Copy assignment | a copyable type with potentially-throwing move-assignment is copied |
| Copy failure | a throwing copy leaves the source element queued and recoverable |
| Logger identity | `getLogger("filecache.test")` is non-null and `name()` returns the same text |
| Logger no-op | all `LOG_*` macros leave side-effect counters unchanged |
| Exception helper | `throwFileCacheException` throws `VeloxRuntimeError`, not `VeloxUserError` |
| Filesystem helper | context, path, numeric error, and error message are present in the runtime exception |

Use promises/futures or condition variables to coordinate threads. Do not use
`sleep`. A short `future::wait_for` may prove that a worker is still pending,
and a longer bounded `wait_for` must prevent a failed test from hanging.

Add Debug `chassert` coverage to `BasicShimsTest.cpp`:

```text
false expression dies and reports the expression text
false expression with a custom diagnostic dies and reports that diagnostic
true expression is evaluated exactly once
```

Create `ChassertReleaseProbe.cpp`, compiled with `NDEBUG`, as a normal executable
whose exit status proves both side-effect counters remain zero:

```text
expression side effect is not evaluated
custom diagnostic construction side effect is not evaluated
```

Create `ChassertSanitizerGateTest.cpp`, compiled with
`NDEBUG;FOLLY_SANITIZE=1`, as a death-test executable proving `chassert(false)`
still aborts.

- [ ] **Step 3: Prove the current implementation is RED**

Configure the existing Debug build:

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`. For `home-chang`, also add
`-DVELOX_BUILD_TESTING=ON` (already present in the `root-oss` effective
configuration). Redirect output to `<velox_build_dir>/configure_task_003_corrective.log`.

Build all three targets and require a failure caused by the missing corrective
API or behavior:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_common_test \
  velox_ch_chassert_release_probe \
  velox_ch_chassert_sanitizer_gate_test \
  > <velox_build_dir>/build_task_003_corrective_red.log 2>&1
```

If the build unexpectedly succeeds, run the three CTest entries and require at
least one contract test to fail. If all pass before implementation, stop and
explain which current code already satisfies the reopened contract.

- [ ] **Step 4: Implement the approved contracts**

Implement only the contracts in this task:

1. Add the timed/non-blocking queue overloads and shared wait predicates.
2. Notify one consumer only after a successful push; notify one producer only
   after a successful pop; notify all waiters from `finish`.
3. Assign the front element by noexcept move or copy exactly as CH does. Pop it
   only after assignment succeeds.
4. Add `ClickHouseAssert.h` with the exact Debug/sanitizer and ordinary-Release
   preprocessor branches.
5. Make `getLogger` return the non-null name-only placeholder.
6. Keep every logging macro, `tryLogCurrentException`, and
   `getCurrentExceptionMessage` no-op without argument evaluation.
7. Keep `throwFileCacheException` runtime-only. Do not add an error-kind enum or
   a `BAD_ARGUMENTS` mode.
8. Keep filesystem failures as `VeloxRuntimeError` diagnostics without
   structured errno or fallback control flow.

Do not change callers in later task scopes during this corrective task.

- [ ] **Step 5: Register all public headers and test targets**

Ensure `velox/ch/Common/CMakeLists.txt` lists `ClickHouseAssert.h` in
`velox_ch_filecache`.

Ensure `velox/ch/Common/tests/CMakeLists.txt` defines and registers:

```text
velox_ch_common_test
velox_ch_chassert_release_probe
velox_ch_chassert_sanitizer_gate_test
```

Target-specific compile definitions:

```text
velox_ch_chassert_release_probe:
  NDEBUG

velox_ch_chassert_sanitizer_gate_test:
  NDEBUG
  FOLLY_SANITIZE=1
```

The sanitizer-gate target verifies branch selection only; real sanitizer CI
provides the normalized Folly macro from its configured toolchain.

- [ ] **Step 6: Build the corrective targets**

Reconfigure with the Step 3 command, then run:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_common_test \
  velox_ch_chassert_release_probe \
  velox_ch_chassert_sanitizer_gate_test \
  > <velox_build_dir>/build_task_003_corrective.log 2>&1
```

Expected exit code: 0. Do not pass `-j`.

- [ ] **Step 7: Run the complete Task 003 gate**

Run:

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_(common_test|chassert_release_probe|chassert_sanitizer_gate_test)$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_003_corrective.log 2>&1
```

Expected:

```text
100% tests passed, 0 tests failed.
```

- [ ] **Step 8: Inspect only task-owned changes**

Run:

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Common/ClickHouseAssert.h \
  velox/ch/Common/FileCacheBoundedQueue.h \
  velox/ch/Common/FileCacheException.h \
  velox/ch/Common/FileCacheFilesystem.h \
  velox/ch/Common/logger_useful.h \
  velox/ch/Common/tests/CMakeLists.txt \
  velox/ch/Common/tests/BasicShimsTest.cpp \
  velox/ch/Common/tests/ChassertReleaseProbe.cpp \
  velox/ch/Common/tests/ChassertSanitizerGateTest.cpp
```

Require:

```text
no whitespace errors
no changes outside the declared scope
all changes unstaged and uncommitted
```

- [ ] **Step 9: Append the corrective receipt**

Append these sections to the existing Task 003 result file rather than
overwriting the original acceptance history:

```markdown
## Corrective source-contract audit

status: success

## Corrective Velox baseline

<branch, starting HEAD, and pre-existing dirty files>

## Corrective files changed

<task-owned files only>

## Corrective RED evidence

<failing target/test, first relevant diagnostic, and log path>

## Corrective commands and logs

<configure, build, test, diff-check commands and log paths>

## Corrective contract verification

<one result for every Step 2 matrix row>

## Deferred work

getCurrentExceptionMessage remains empty; real formatting is owned by Task 017.

## Blocking errors

None
```

If blocked or failed, set that status, record the first actionable error and
log path, and stop without claiming success.

## Acceptance gate

Task 003 is accepted only when:

```text
all queue API and state-machine rows pass
move-or-copy tests prove exact CH assignment selection and exception safety
Debug, ordinary Release, and sanitizer-gate chassert tests pass
ordinary Release proves expression and diagnostic message non-evaluation
BAD_ARGUMENTS/LOGICAL_ERROR mapping is documented without a new error enum
filesystem failures remain Velox-style and do not expose structured errno
getLogger is non-null and name-only
logging and current-exception formatting remain no-op
all three CTest targets pass
the corrective receipt contains RED and final verification evidence
```

## Explicit exclusions

Do not implement in this task:

```text
real logging
non-empty getCurrentExceptionMessage
structured errno exceptions
Guards.h
StatusFile.h / StatusFile.cpp
FileCacheScheduler
FileCacheWorkerPool / FileCacheThreadPool
FileCacheQueryIdScope
SipHash128
ReadBufferFromVeloxReadFile / WriteBufferFromVeloxWriteFile
FileCache leaf types or algorithms
Gluten integration
```

These belong to later tasks. Real logging and exception formatting are
explicitly owned by Task 017.
