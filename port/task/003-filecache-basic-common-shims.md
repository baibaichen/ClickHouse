# Task 003: Correct `FileCache` Basic Common Shims

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and appends
> corrective evidence to one result file under this ClickHouse checkout. Do not
> modify ClickHouse source files. Do not commit or stage either repository.

## Status and authority

```text
controller_status: reopened_by_contract_audit
```

The original Task 003 implementation was accepted, then reopened first by the
post-acceptance source-contract audit and again by the Tasks 003-010
cross-profile review recorded in
`port/task/fullreview/cross-profile/1/003-010-review-decisions.md`. This
document is the only executable Task 003 plan. The original implementation
instructions are intentionally not retained here; the existing receipt
preserves their result.

Task 011 and Task 012 must not start until this corrective task, including its
`ProfileEvents`/`CurrentMetrics` name-surface scope below, is implemented,
reviewed, and accepted.

## Corrective scope B1/B2: no-op `ProfileEvents`/`CurrentMetrics` name surfaces

This is the exact, non-negotiable scope reopened by the cross-profile review.
Apply it in addition to the "Approved dependency decisions" below. Do not
reinterpret or shrink either name list.

### B1 — 31 `ProfileEvents` names

Add these 31 names, referenced by CH `src/Interpreters/FileCache/*` and
currently absent from `velox/ch/Common/ProfileEvents.h`:

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
FilesystemCacheEvictMicroseconds
FilesystemCacheEvictedBytes
FilesystemCacheEvictedFileSegments
FilesystemCacheEvictionReusedIterator
FilesystemCacheEvictionSkippedEvictingFileSegments
FilesystemCacheEvictionSkippedFileSegments
FilesystemCacheEvictionSkippedMovingFileSegments
FilesystemCacheEvictionTries
FilesystemCacheFailToReserveSpaceBecauseOfCacheResize
FilesystemCacheFailedEvictionCandidates
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

### B2 — five `CurrentMetrics` names

Add only these five names to `velox/ch/Common/CurrentMetrics.h`:

```text
FilesystemCacheElements
FilesystemCacheInvalidatedElements
FilesystemCachePriorityQueueElements
FilesystemCacheSize
FilesystemCacheKeys
```

Do not add `FilesystemCacheEvictionThreads`,
`FilesystemCacheEvictionThreadsActive`,
`FilesystemCacheEvictionThreadsScheduled`, or
`FilesystemCacheOvercommitUsers`. The accepted thread-pool mapping drops the
three constructor-only metrics, and overcommit remains excluded.

### B1/B2 rules

```text
ProfileEvents and CurrentMetrics remain no-op: increment/add/sub/Increment
  keep their current no-op bodies; do not implement real counters.
Add one compile-coverage test (in BasicShimsTest.cpp) that references every
  required B1 name and every required B2 name so a missing name fails
  compilation, not just a missing feature.
False-green mutation: delete exactly one required name from ProfileEvents.h
  or CurrentMetrics.h and prove the compile-coverage test target fails to
  build. Restore the name afterward and re-prove the build succeeds.
Real event counters and real metrics remain Task 017.
```

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
| B1 `ProfileEvents` coverage | a compile-coverage test references every one of the 31 required names; deleting one name from `ProfileEvents.h` fails compilation |
| B2 `CurrentMetrics` coverage | a compile-coverage test references every one of the 5 required names; deleting one name from `CurrentMetrics.h` fails compilation |

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

Add the B1/B2 compile-coverage test to `BasicShimsTest.cpp`:

```text
one function/test body that references (odr-uses) every one of the 31 B1
  ProfileEvents::Event names and every one of the 5 B2 CurrentMetrics::Metric
  names, e.g. by passing each enumerator through ProfileEvents::increment or
  CurrentMetrics::add.
```

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
9. Add every B1 name to `ProfileEvents::Event` and every B2 name to
   `CurrentMetrics::Metric` in `velox/ch/Common/ProfileEvents.h` and
   `velox/ch/Common/CurrentMetrics.h`. Keep `increment`/`add`/`sub`/`Increment`
   no-op. Do not add `FilesystemCacheEvictionThreads*` or
   `FilesystemCacheOvercommitUsers`.

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

Then perform the B1/B2 false-green mutation probe: temporarily delete exactly
one required name from `ProfileEvents.h` (e.g. the last B1 name) and confirm
`velox_ch_common_test` fails to build because the compile-coverage test no
longer resolves that name. Restore the name, repeat once for one `CurrentMetrics`
name, restore it, and re-run Step 6/Step 7 to confirm the final build and test
run are green again. Record both mutation logs in the corrective receipt.

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
  velox/ch/Common/ProfileEvents.h \
  velox/ch/Common/CurrentMetrics.h \
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

## B1/B2 false-green mutation evidence

<mutation log path and outcome for the deleted ProfileEvents name;
mutation log path and outcome for the deleted CurrentMetrics name;
confirmation that both were restored and the final build/test are green>

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
every B1 ProfileEvents name and every B2 CurrentMetrics name compiles through
  the coverage test, with ProfileEvents/CurrentMetrics remaining no-op
the delete-one-name false-green mutation fails compilation for one B1 name and
  one B2 name, and the build/test are green again after restoring both names
the corrective receipt contains RED, mutation, and final verification evidence
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
