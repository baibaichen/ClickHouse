# Task 018a Result: Pure-Velox Connector Integration — Route Hive Reads Through `FileCache`

## Worker attempt 1

```text
worker_status: blocked
environment_profile: home-chang
task: 018a
```

Task 018a asks a new `FileCacheBufferedInputBuilder::create(...)` to make a
per-call routing decision between two branches:

- **Select FileCache** "when the Manager has an installed default cache";
- **Fallback** to the free `createBufferedInput(...)` "when no FileCache is
  installed (**Manager has no default cache**)".

Before writing any implementation the worker enumerated every dependency the
task reaches (worker rules 5-6) and derived the contract from the authoritative
sources (CH callers via the design docs + the accepted 013/014 receipts + the
Velox trunk extension point). One decision is genuinely unreviewed and cannot be
implemented without either (a) a generic exception-swallowing fallback the
project rules forbid, or (b) expanding scope into `FileCacheManager.h` which the
task forbids. Per the EXECUTION_PROTOCOL unreviewed-dependency gate the worker
did NOT guess a "closest" API, add a shim, or add a fallback; no source or CMake
file was created or modified.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `cb67d8afb09211adeddcf669318f52aa07a43fca` | clean (0 files) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `7c5a5291d1668e9a6774374857308c269dc653dc` | only untracked `port/task/018a-filecache-connector-integration.md` (the task file itself) |

`HEAD cb67d8afb` (velox) = "Task 017: FileCache observability and cancellation
hardening". Branch is `filecache2` / `ch-filecache2` (task text says
`filecache`); recorded as observed per the standing dispatch note — did not stop
over that mismatch. No pre-existing dirty files in velox. No
staging/commit/amend/rebase/push. No build-directory logs were written (the
worker stopped before configure/build; no false-green log manufactured).

## Files changed

```text
None. The worker stopped at the unreviewed-dependency gate before creating the
FileCacheBufferedInputBuilder .h/.cpp/test or editing either CMakeLists.txt.
Only this result receipt is written under port/task/result/.
```

## Pre-implementation gate work completed before stopping

```text
Extension point re-confirmed from source (not guessed):
  - BufferedInputBuilder is the 7-arg virtual create() override at
    velox/connectors/hive/BufferedInputBuilder.h:43-50; getInstance() is never
    null (a DefaultBufferInputBuilder is statically registered at
    BufferedInputBuilder.cpp:44-45); registerBuilder(...) overwrites builder_.
  - Fallback target createBufferedInput(...) is public, declared at
    velox/connectors/hive/HiveConnectorUtil.h:142 (namespace
    facebook::velox::connector::hive), and DefaultBufferInputBuilder forwards to
    it verbatim (BufferedInputBuilder.cpp:22-41).
  - Native cache mutual-exclusion keys off connectorQueryCtx->cache()
    (HiveConnectorUtil.cpp:661); ConnectorQueryCtx::cache() and ::queryId() exist
    (Connector.h:515,527).
  - FileHandle::file is std::shared_ptr<ReadFile>; path = file->getName()
    (velox/common/caching/FileHandle.h).
  - Our FileCacheBufferedInput 12-arg ctor confirmed at
    velox/ch/Disks/IO/FileCacheBufferedInput.h:46-58 (arg order matches the
    task's sourcing table).
  - FileCacheRequestContext real fields confirmed
    (FileCacheRequestContext.h:35-42): queryId, userId, userWeight, cacheable,
    segmentType — matches the benchmark usage (queryId + userId set).
  - FileCacheManager exposes getDefault() (FileCacheManager.h:94) and
    commonUserId() (FileCacheManager.h:95); FileCache::getCommonOrigin() is an
    instance method (FileCache.h:174); FileCacheKey::fromPath exists
    (FileCacheKey.h:45). All present — no name moved.
```

## Blockers

```text
BLOCKER B1 — no non-throwing "has an installed default cache" predicate exists on
FileCacheManager, so the task's required per-call routing between the FileCache
branch and the fallback branch cannot be implemented without violating a
higher-authority contract or the declared file scope.

  The task's create() must choose:
    - FileCache branch      when "the Manager has an installed default cache";
    - createBufferedInput   when "Manager has no default cache".
  This presupposes a queryable, non-throwing predicate for "does this Manager
  have an installed default cache?". None exists:
    - FileCacheManager's ONLY default accessor is getDefault() const
      (FileCacheManager.h:94; impl FileCacheManager.cpp:148-153). It THROWS
      (throwFileCacheException -> VELOX_FAIL -> VeloxRuntimeError) when
      defaultCacheName_ is empty ("no default cache configured"), and its
      delegate FileCacheFactory::get THROWS ("There is no cache by name")
      otherwise. It never returns null.
    - A Manager built with an EMPTY defaultCacheName is explicitly allowed
      (FileCacheManager.cpp:50 only validates when the name is non-empty), so the
      "Manager has no default cache" deployment the task's fallback targets is a
      REAL, constructible state that makes getDefault() throw.
    - defaultCacheName_ is private with NO accessor; the only public non-throwing
      registry view is factory().getAll() (name->data map), which cannot answer
      "is the DEFAULT installed?" without the private default name.

  The two ways to satisfy the task branch are both disallowed:
    (a) try { cache = manager_.getDefault(); } catch (...) { fallback; } — this
        is exactly the silent, generic-catch fallback the project rules forbid
        ("Avoid fallback paths"; catching VeloxRuntimeError would also swallow a
        genuine bug such as a default name registered but its cache missing,
        turning a real error into a wrong-path native read).
    (b) Add a non-throwing predicate (e.g. bool hasDefault() const /
        FileCachePtr tryGetDefault() const) to FileCacheManager.h — OUT of the
        declared file scope (Task 018a may modify only new/changed C++ under
        velox/ch/Disks/IO/ and the two velox/ch/Disks/IO CMakeLists.txt; the
        result receipt). Editing FileCacheManager.h is a scope expansion the task
        forbids.

  This also collides with a HIGHER-AUTHORITY design contract: design
  port/3-consumers/02-filecache-manager-design.md:668-685 lists "missing default
  cache" as an ERROR the Manager must raise and states the Manager must NOT "fall
  back to another cache". So making the Manager's default lookup non-throwing, or
  making the builder silently swallow the Manager's error, contradicts the
  accepted Manager contract that Task 013 implemented and the controller
  accepted. The numbered task cannot silently weaken that contract
  (EXECUTION_PROTOCOL "Authority order for behavior").

  CH defining source / real callers:
    - Selection predicate is a NEW port-side concept (no direct CH caller); the
      closest CH intent is "if a FileCache is installed for this read, route
      through it, else read the source directly", realized in CH at the
      IDisk/settings layer, not via a throwing getDefault.
    - Velox side: FileCacheManager.h:94 (getDefault), FileCacheManager.cpp:148-153
      (throws on empty default), FileCacheFactory.cpp:215-222 (get throws),
      FileCacheManager.cpp:50-58 (empty default name is allowed at create),
      design 02:227 (getDefault is the sole default API), design 02:668-685
      (missing default = error, no fall back to another cache).

  Required API/state/ownership/concurrency behavior needed:
    - A NON-THROWING way for the builder to decide "FileCache installed vs not"
      for this Manager, evaluated per create() call, thread-safe against
      concurrent scans, and holding a reference (Manager must outlive builder).

  Candidate resolutions (need a user/controller decision; do not pick one here):
    1. Add FileCacheManager::hasDefault() const (or tryGetDefault() returning a
       null FileCachePtr when unset) and AUTHORIZE Task 018a to modify
       FileCacheManager.h/.cpp for exactly that predicate. Cleanest; keeps the
       Manager's "missing default = error" contract intact for getDefault() while
       giving the builder a non-throwing selector. Requires widening 018a's file
       scope by two files.
    2. Redefine the builder so it is ONLY registered when a FileCache IS
       installed: registerFileCacheBufferedInputBuilder captures
       manager_.getDefault() (a valid FileCachePtr) once at registration; the
       "no FileCache installed" deployment simply never registers our builder and
       keeps DefaultBufferInputBuilder. Then create() has no per-call
       no-default branch at all — matching the task's own case-2 parenthetical
       "(or the default DefaultBufferInputBuilder restored)". This removes the
       fallback branch the task text also asks for in step 3, so it needs an
       explicit decision that the builder is FileCache-only and the fallback is
       "don't register us", not "internal branch".
    3. Explicitly authorize the generic-catch fallback inside create() despite
       the "avoid fallback paths" rule (least preferred; hides the
       default-registered-but-missing bug class).

  Exact decision needed from the user/controller:
    Choose how create() distinguishes "FileCache installed" from "not installed"
    without a throwing call and without violating design 02's "missing default =
    error / no fall back" contract — option 1 (add + authorize a non-throwing
    Manager predicate, widening scope), option 2 (FileCache-only builder; fallback
    = don't register), or option 3 (authorize the generic-catch fallback). Record
    the decision in the Task 018a amendment and the Manager design if option 1.
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| `git status/log` velox + ClickHouse baselines | 0 | (captured in this receipt) |
| Read + contract-derivation of BufferedInputBuilder.{h,cpp}, HiveConnectorUtil.{h,cpp} guard, FileCacheBufferedInput.h ctor, FileCacheManager.{h,cpp}, FileCacheFactory.{cpp,h}, FileCacheRequestContext.h, FileHandle.h, Connector.h, Options.h, the seek benchmark, designs 02/03, receipts 013/014 | 0 | (research only; no build attempted — no false-green log manufactured) |

No configure/build/test was run: the worker stopped at the unreviewed-dependency
gate before any source could be written, so there is nothing to compile.
Manufacturing a build log without an implementation would not be truthful
acceptance evidence.

## Acceptance evidence

```text
test count: 0 (no test target built)
failed tests: n/a
skipped/disabled tests: n/a
git diff --check: clean (no changes; velox tree at baseline cb67d8afb)
```

## Worker review

```text
review subagent: not launched. Protocol rule 10 launches the read-only reviewer
  over a COMPLETED task-owned diff after local validation. This attempt produced
  no diff (blocked before any in-scope file could be written), so there is
  nothing to review; this mirrors how the Task-013 attempt-1 blocked receipt
  handled the same situation.
findings: N/A
resolutions: N/A
unresolved findings: the structural decision B1 above (needs a controller/user
  decision, not a code fix).
```

## Blockers (summary)

```text
B1 — the builder's required "FileCache installed vs not" routing has no
non-throwing predicate on FileCacheManager; the only two ways to add one are a
forbidden generic-catch fallback or an out-of-scope edit to FileCacheManager.h,
and either collides with design 02's "missing default = error / no fall back"
contract. Needs an explicit user/controller decision (options 1/2/3 above).
```

## Worker declaration

```text
Only Task 018a was attempted.
No source or CMake file was created or modified; both worktrees are at their
baselines (velox cb67d8afb, clean; ClickHouse ch-filecache2 7c5a5291d with only
the untracked task file). No staging/commit/amend/rebase/push. Only this result
receipt was written.
The worker stopped after writing this receipt.
```

## Worker attempt 2

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 018a
```

Attempt 1 stopped at the unreviewed-dependency gate (blocker B1). The Controller
resolved B1 by amending the task with the "Routing model — validate at install,
take at `create`" decision (2026-07-21): the builder has NO per-call
"is FileCache installed?" branch. Install-time validation calls
`manager.getDefault()` once (throw propagates, fail-fast, not caught); the
builder holds `FileCacheManager&` and resolves `getDefault()` per call in
`create()`; the "not installed" fallback lives at the registration boundary
(never register our builder). Attempt 2 implements exactly this model.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/home/chang/OpenSource/velox` | `filecache2` | `cb67d8afb09211adeddcf669318f52aa07a43fca` | clean (0 tracked/untracked) |
| `/home/chang/SourceCode/ClickHouse` | `ch-filecache2` | `7c5a5291d1668e9a6774374857308c269dc653dc` | only untracked `port/task/018a-*.md` (task file) + this receipt |

No staging/commit/amend/rebase/push. All velox changes confined to `velox/ch/`.

## Files changed

```text
NEW  /home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheBufferedInputBuilder.h
NEW  /home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheBufferedInputBuilder.cpp
NEW  /home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp
MOD  /home/chang/OpenSource/velox/velox/ch/Disks/IO/CMakeLists.txt         (add builder .cpp; link velox_hive_connector)
MOD  /home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/CMakeLists.txt   (register velox_ch_filecache_connector_test)
MOD  /home/chang/SourceCode/ClickHouse/port/task/result/018a-filecache-connector-integration-result.md (this receipt)
```

## Implementation summary

```text
FileCacheBufferedInputBuilder (final : connector::hive::BufferedInputBuilder):
  - holds FileCacheManager& manager_ (captured at construction; Manager must
    outlive builder — documented in header).
  - create() overrides the 7-arg base signature (FileHandle resolves to
    velox::FileHandle per enclosing-namespace lookup, as in the base):
      1. VELOX_CHECK_NULL(connectorQueryCtx->cache(),
           "FileCache and AsyncDataCache cannot both be installed")  (per-call guard)
      2. FileCachePtr cache = manager_.getDefault()  (per-call; cannot throw for
           "no default" after install-time validation; no bare getInstance)
      3. construct FileCacheBufferedInput via the 12-arg ctor:
           readFile   = fileHandle.file
           cache      = manager_.getDefault()
           cacheKey   = FileCacheKey::fromPath(fileHandle.file->getName())
           origin     = cache->getCommonOrigin()  (instance method)
           cacheOpts  = FileCacheReadOptions{}
           reqCtx     = {queryId=connectorQueryCtx->queryId(), userId=manager_.commonUserId()}
           metricsLog = dwio::common::MetricsLog::voidLog()
           ioStatistics/ioStats/executor/readerOpts/fileReadOps forwarded from create() args
      NO internal fallback branch.
registerFileCacheBufferedInputBuilder(FileCacheManager& manager):
      (void)manager.getDefault();  // fail-fast install-time validation; throw propagates, NOT caught
      BufferedInputBuilder::registerBuilder(make_shared<FileCacheBufferedInputBuilder>(manager));
Trunk (velox/connectors/hive/*, anything outside velox/ch/): ZERO diff.
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| CMake configure (regenerate) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task018a_v2_configure.log` |
| Build velox_ch_filecache_connector_test (initial, revealed 2 signature fixes) | 1→fixed | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task018a_v2_build.log` |
| Build velox_ch_filecache_connector_test (after FileHandle/SsdCache fixes) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task018a_v2_build2.log` |
| Run connector test (GREEN, 4/4) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task018a_v2_connector_test.log` |
| Case-5 RED build (FileCache construction neutralized in create) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task018a_v2_red_build.log` |
| Case-5 RED run (case 1 FAILS as required) | 1 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task018a_v2_red_test.log` |
| Build all 6 gates + connector + 2 benchmarks (restored) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task018a_v2_regression_build.log` |
| Run connector (final, after review comment) | 0 | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13/task018a_v2_final_connector_test.log` |
| Run e2e / buffered_input / manager / core_scc / observability / cancellation | 0 | `task018a_v2_{e2e,buffered_input,manager,core_scc,observability,cancellation}.log` under `.../cmake-build-debug-gcc13/` |

## Acceptance evidence

```text
New test binary: velox_ch_filecache_connector_test (FileCacheBufferedInputBuilderTest)
  test count: 4, failed: 0, skipped/disabled: 0
    1 FileCacheSelectedAndCacheHit       — dynamic_cast<FileCacheBufferedInput*> non-null;
                                            real enqueue+drain populates cache (getFileSegmentsNum>0,
                                            cold preadCount>0); 2nd input isBuffered + preadCount==0 (hit)
    2 NotInstalledKeepsNative            — no register: dynamic_cast==null; native read() returns content
    3 InstallTimeFailFastOnEmptyDefault  — register throws VeloxRuntimeError ("no default cache configured");
                                            registered builder pointer unchanged (no side-effect registration)
    4 MutualExclusionGuardThrows         — real AsyncDataCache in ctx: create throws "cannot both be installed"

Case-5 RED requirement (falsifiability):
  Neutralized create() to return native createBufferedInput instead of FileCacheBufferedInput,
  rebuilt, ran case 1 -> FAILED at FileCacheBufferedInputBuilderTest.cpp:321
    "Expected: (fcInput) != (nullptr), actual: NULL vs (nullptr)"  (exit 1)
  Then restored the FileCache construction, rebuilt, reran -> 4/4 PASSED (exit 0).
  Proves the test truly exercises the FileCache path (no false-green).

Regression gates (all green, 0 failed / 0 skipped), counts match task exactly:
  velox_ch_filecache_e2e_test              17
  velox_ch_filecache_buffered_input_test   19
  velox_ch_filecache_manager_test          19
  velox_ch_filecache_core_scc_test         47
  velox_ch_observability_test              14   (task label "observability_test")
  velox_ch_cancellation_test                5   (task label "cancellation_test")

Benchmarks still build (no run required):
  velox_ch_filecache_seek_benchmark        linked OK
  velox_ch_filecache_wrapper_benchmark     linked OK

Trunk-diff proof (git diff --stat + untracked):
  velox/ch/Disks/IO/CMakeLists.txt         (+6)
  velox/ch/Disks/IO/tests/CMakeLists.txt   (+25)
  new: velox/ch/Disks/IO/FileCacheBufferedInputBuilder.{h,cpp}
  new: velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp
  Changes outside velox/ch/: NONE
git diff --check: clean
Nothing staged/committed. No -j used.
```

## Worker review

```text
review subagent: pr-review-toolkit:code-reviewer (read-only), single launch over the
  complete Task 018a diff (5 files) + context (BufferedInputBuilder.{h,cpp},
  FileCacheBufferedInput.h, FileCacheManager.h, HiveConnectorUtil.cpp guard).
findings:
  - No critical/actionable defects. All 6 focus areas verified correct:
    (1) 12-arg ctor sourcing correct; (2) Manager& + per-call getDefault safe,
    FileCachePtr keeps the cache alive for the input's life; (3) install-time
    validation fails fast, throw not swallowed, no registration on failure;
    (4) mutual-exclusion guard mirrors native selection; (5) tests are
    falsifiable, no false-green, TearDown restores the static default builder;
    (6) CMake mono/non-mono linking correct.
  - One INFORMATIONAL fragility note: TearDown must re-register the native
    default builder BEFORE resetting manager_ (the FileCache builder aliases
    manager_ by reference). Current order is already correct — flagged only for
    an explicit comment.
resolutions:
  - Added a clarifying comment in TearDown documenting the required order
    (comment-only test change). Rebuilt + reran connector test: 4/4 PASSED.
unresolved findings: none.
```

## Blockers

```text
None. B1 was resolved by the Controller's routing-model amendment; attempt 2
implemented it fully and all gates are green.
```

## Worker declaration

```text
Only Task 018a was attempted (attempt 2).
Changes are unstaged and uncommitted. velox at cb67d8afb (branch filecache2);
all diffs confined to velox/ch/ (zero trunk diff). ClickHouse at 7c5a5291d
(branch ch-filecache2) with only the untracked task file and this receipt.
No staging/commit/amend/rebase/push.
The worker stopped after writing this receipt.
```

## Controller review 1

```text
controller_status: accepted
environment_profile: home-chang
task: 018a
```

## Review evidence

```text
scope review: All changes confined to velox/ch/ — new
  FileCacheBufferedInputBuilder.{h,cpp} + tests/FileCacheBufferedInputBuilderTest.cpp,
  and +6/+25 lines in the two velox/ch/Disks/IO CMakeLists.txt. Trunk-diff proof
  INDEPENDENTLY confirmed by Controller: `git diff --stat` shows only the two
  velox/ch CMakeLists; untracked list shows only the three new velox/ch files.
  Zero diff outside velox/ch/ (velox/connectors/hive/* untouched). ClickHouse
  side: only the task file + this receipt (+ handoff), no source touched.

implementation review: Independently read all three new files against the
  amended routing model and the 12-arg FileCacheBufferedInput ctor.
  - registerFileCacheBufferedInputBuilder: `(void)manager.getDefault();` fail-fast
    install-time validation (throw NOT caught) then registerBuilder — matches the
    decision exactly. No bare singleton; builder holds FileCacheManager&.
  - create(): (1) VELOX_CHECK_NULL(connectorQueryCtx->cache(), "...cannot both be
    installed") per-call guard; (2) cache = manager_.getDefault() per-call (safe
    post-validation); (3) 12-arg ctor sourcing verified correct — readFile=
    fileHandle.file, cacheKey=FileCacheKey::fromPath(file->getName()), origin=
    cache->getCommonOrigin() (instance), reqCtx={queryId=ctx->queryId(),
    userId=manager_.commonUserId()}, metricsLog=voidLog(), io/executor/readerOpts/
    fileReadOps forwarded. NO internal createBufferedInput fallback branch.
    readerOpts passed to a value member (ReaderOptions readerOptions_) so no
    dangling-ref risk. Lifetime: builder aliases Manager by ref; the emitted
    FileCacheBufferedInput holds a FileCachePtr (shared) keeping the cache alive
    for the input's lifetime independent of the builder.

cross-task architecture review: Consistent with Task 013 (Manager-owned default,
  getDefault throws on missing — design 02 "missing default = error / no fall
  back" NOT weakened; the builder never makes the Manager's lookup non-throwing
  and never swallows its error) and Task 014 (FileCacheBufferedInput ctor + read
  path reused unchanged). The install-boundary fallback preserves the native
  DefaultBufferInputBuilder for non-FileCache deployments.

log and test review: Controller INDEPENDENTLY reproduced the RED and reran all
  gates (did not trust worker logs):
  - RED: neutralized create() to return native createBufferedInput; rebuilt
    (exit 0); case-1 FAILED at FileCacheBufferedInputBuilderTest.cpp:323
    (dynamic_cast<FileCacheBufferedInput*> == NULL). Restored; grep confirms no
    residual probe/HiveConnectorUtil include; rebuilt; connector test 4/4 PASSED.
  - Regression (Controller-run counts): e2e 17, buffered_input 19, manager 19,
    core_scc 47, observability 14, cancellation 5 — all 0 failed / 0 skipped,
    counts match the task exactly. Both benchmarks (seek, wrapper) link OK.
  - Test quality: 4 cases all drive the real BufferedInputBuilder::create
    extension point with a real FileHandle/ConnectorQueryCtx/read. Case 1 proves
    miss->fill (getFileSegmentsNum>0, cold preadCount>0) then hit (fresh input,
    preadCount==0). Case 2 dynamic_cast==null + native read succeeds. Case 3
    empty-default install throws VeloxRuntimeError with no side-effect
    registration. Case 4 real AsyncDataCache triggers the mutual-exclusion guard.
    TearDown re-registers the native default BEFORE resetting the Manager
    (correct order; no dangling reference).

unresolved findings: none.
```

## Required changes

```text
None.
```

## Commits

| Repository | Commit |
|---|---|
| `/home/chang/OpenSource/velox` | `dac50ae27` |
| `/home/chang/SourceCode/ClickHouse` | this acceptance commit (task file + receipt + handoff) |
