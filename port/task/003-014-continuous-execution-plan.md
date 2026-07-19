# `FileCache` Tasks 003-014 Continuous Execution Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the Task-003 enum surfaces, execute the FileCache center SCC
and its Factory/Manager and read consumers through Task 014, then stop at the
mandatory Tasks 003-014 full review.

**Architecture:** One task-authoring wave first makes the numbered contracts
match the cross-profile decisions. Task 011 migrates the priority/eviction half
of the center SCC without compiling it; Task 012 immediately adds the mutually
dependent core and provides the single green SCC closure. Tasks 013 and 014 add
ownership and reader integration on top of that accepted core.

**Tech Stack:** C++20, Velox, Folly, CMake, Ninja, GTest, CTest, Git.

## Global Constraints

- Environment profile is `root-oss`.
- ClickHouse repository is `/root/oss/clickhouse` on `ch-filecache`.
- Velox repository is `/root/oss/velox` on `filecache`.
- Velox build directory is `/root/oss/velox/_build/debug`.
- Source `/root/oss/velox-helper/env.sh` before every configure, build, or test.
- Never pass `-j` to Ninja and never use `nproc`.
- Redirect every build and test to a unique log under the Velox build directory.
- Use CH source and real callers as behavior truth.
- Use
  `port/task/fullreview/cross-profile/1/003-010-review-decisions.md` as the
  authoritative reviewed mapping layer.
- Do not infer an unreviewed dependency mapping; stop with
  `waiting_for_user`.
- Preserve exact CH internal structure unless a registered platform deviation
  permits the replacement.
- Every material test requires behavioral RED and false-green evidence.
- Disabled, skipped, unregistered, comment-only, and no-assert tests do not
  count.
- Do not modify Gluten in Tasks 003-014.
- Do not rebase or amend; every accepted task gets a new commit.
- Task 011 and Task 012 are one atomic implementation stage with separate
  Workers and receipts.
- Do not start Task 015. Stop after the mandatory Tasks 003-014 full review.

---

### Task 1: Amend the Numbered Contracts

**Files:**
- Modify: `port/task/003-filecache-basic-common-shims.md`
- Modify: `port/task/011-filecache-priority-eviction.md`
- Modify: `port/task/012-filecache-core-scc.md`
- Modify: `port/task/013-filecache-factory-manager.md`
- Modify: `port/task/014-filecache-buffered-input.md`
- Modify: `port/task/result/003-filecache-basic-common-shims-result.md`
- Modify: `port/task/EXECUTION_PROTOCOL.md`
- Modify: `port/task/CONTROLLER_HANDOFF.md`

**Interfaces:**
- Consumes:
  `port/task/fullreview/cross-profile/1/003-010-review-decisions.md`.
- Produces: executable task contracts whose Workers need no profile-specific
  interpretation.

- [ ] **Step 1: Amend Task 003**

Add:

```text
controller_status: reopened_by_contract_audit
B1: exact 34-name ProfileEvents list from cross-profile decisions
B2: exact five-name CurrentMetrics list from cross-profile decisions
ProfileEvents and CurrentMetrics remain no-op
compile-coverage test references every required name
false-green mutation deletes one required name and must fail compilation
```

Extend the Task-003 file scope to:

```text
/root/oss/velox/velox/ch/Common/ProfileEvents.h
/root/oss/velox/velox/ch/Common/CurrentMetrics.h
/root/oss/velox/velox/ch/Common/tests/BasicShimsTest.cpp
```

- [ ] **Step 2: Amend Task 011**

Add:

```text
minimal non-overcommit CacheUsage surface only
SD2 F14FastMap/F14FastSet mapping
original_queue_types remains std::unordered_map
SD5 std::list preservation
exact Task-011 ProfileEvents and CurrentMetrics name lists
typed subtype at behaviorally error-code-sensitive call sites
Task-003 B1/B2 acceptance pre-check
tests and compile closure belong to Task 012
```

Remove or supersede instructions that require full
`CacheUsagePerUser`/overcommit bodies.

- [ ] **Step 3: Amend Task 012**

Add:

```text
Memory<> -> MemoryPool BufferPtr
SCOPE_EXIT -> Folly scope guard
Stopwatch -> call-site-limited DeltaCpuWallTimeStopWatch alias
callOnce/OnceFlag -> std::call_once/std::once_flag
typed FileCacheErrnoException consumer
ENOSPC/EDQUOT-only reconciliation
generic exceptions do not reconcile
per-field value comparison in applySettingsIfPossible
three-phase FileCache shutdown
queue-cancel-before-worker-join in CacheMetadata shutdown
SD1 no-reference-escape
SD3 std::map, SD4 F14 bucket gate, SD5 std::list
F-CALLERID and SD8 deferred to Task 017
StatusFile restart diagnostics deferred pre-release
false-green evidence for every material test
```

Remove or supersede:

```text
reconcile-every-exception pseudo-code
offsetof on LockedKey
test-side reconciliation
private copies of approved helpers
```

- [ ] **Step 4: Amend Task 013**

Delete the private `checkedAdd` instruction and require:

```cpp
FileCacheUtils::checkedAdd(lhs, rhs, operation)
```

Add dependency pre-check rows for `FileCacheWorkerPool`,
`FileCacheScheduler`, `OpenedFileCache`, `folly::Timekeeper`, and F14
shared-pointer registries.

- [ ] **Step 5: Amend Task 014**

Add:

```text
canceled reader is never returned to FileSegment
getRemoteFileMetadata == nullopt does not provide truncation metadata
checkedAdd is the shared Task-008 helper
false-green probe for every material handoff/seek/failure test
no Gluten modifications
```

Remove stale warnings about nonexistent null-returning fixtures or comment-only
test bodies.

- [ ] **Step 6: Update workflow state**

Set:

```text
Current state: Task 003 reopened by cross-profile review
Then dispatch: Task 003 corrective Worker only
Task 011 allowed only after Task 003 acceptance and explicit user approval
```

- [ ] **Step 7: Validate and commit the authoring wave**

Run:

```bash
cd /root/oss/clickhouse
git diff --check
grep -nE 'reopened_by_contract_audit|FileCacheErrnoException|SD2|false-green' \
  port/task/003-filecache-basic-common-shims.md \
  port/task/011-filecache-priority-eviction.md \
  port/task/012-filecache-core-scc.md \
  port/task/013-filecache-factory-manager.md \
  port/task/014-filecache-buffered-input.md
```

Expected: every amended contract contains its required decisions; no whitespace
errors.

Commit:

```bash
git add \
  port/task/003-filecache-basic-common-shims.md \
  port/task/011-filecache-priority-eviction.md \
  port/task/012-filecache-core-scc.md \
  port/task/013-filecache-factory-manager.md \
  port/task/014-filecache-buffered-input.md \
  port/task/result/003-filecache-basic-common-shims-result.md \
  port/task/EXECUTION_PROTOCOL.md \
  port/task/CONTROLLER_HANDOFF.md
git commit \
  -m 'Prepare `FileCache` Tasks 003-014 for continuous execution' \
  -m 'Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
```

### Task 2: Correct Task 003 Enum Surfaces

**Files:**
- Modify: `/root/oss/velox/velox/ch/Common/ProfileEvents.h`
- Modify: `/root/oss/velox/velox/ch/Common/CurrentMetrics.h`
- Modify: `/root/oss/velox/velox/ch/Common/tests/BasicShimsTest.cpp`
- Modify:
  `/root/oss/clickhouse/port/task/result/003-filecache-basic-common-shims-result.md`

**Interfaces:**
- Consumes: exact B1/B2 name lists from the amended Task 003.
- Produces: complete no-op enum name surfaces required by Tasks 011/012.

- [ ] **Step 1: Add the compile-coverage RED**

Add one test that references every existing and newly required
`ProfileEvents::Event` and `CurrentMetrics::Metric` name. The pre-change source
must fail compilation on the first missing name.

- [ ] **Step 2: Record RED evidence**

Build:

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug \
  velox_ch_common_test \
  > /root/oss/velox/_build/debug/build_task_003_enum_red.log 2>&1
```

Expected: non-zero exit caused by a missing required event/metric enumerator.

- [ ] **Step 3: Add no-op names**

Add exactly the 34 approved event names and five approved metric names. Do not
change `increment`, `add`, `sub`, timer, or `Increment` behavior.

- [ ] **Step 4: Run the focused green test**

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug \
  velox_ch_common_test \
  velox_ch_chassert_release_probe \
  velox_ch_chassert_sanitizer_gate_test \
  > /root/oss/velox/_build/debug/build_task_003_enum_green.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug \
  -R '^(velox_ch_common_test|velox_ch_chassert_release_probe|velox_ch_chassert_sanitizer_gate_test)$' \
  --output-on-failure \
  > /root/oss/velox/_build/debug/test_task_003_enum_green.log 2>&1
```

Expected: all selected tests pass with zero skipped/disabled tests.

- [ ] **Step 5: Run the false-green mutation**

Temporarily remove one required existing event name from `ProfileEvents.h`.
Rebuild `velox_ch_common_test`.

Expected: compile failure in the coverage test. Restore the name and rerun the
green build.

- [ ] **Step 6: Review and accept Task 003**

The Controller verifies:

- exact name sets;
- no real metric/event implementation;
- no forbidden metrics;
- genuine RED and false-green logs;
- clean mono and non-mono registration.

Commit Velox:

```bash
git -C /root/oss/velox add \
  velox/ch/Common/ProfileEvents.h \
  velox/ch/Common/CurrentMetrics.h \
  velox/ch/Common/tests/BasicShimsTest.cpp
git -C /root/oss/velox commit \
  -m 'Task 003: Complete `FileCache` metric names' \
  -m 'Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
```

Commit the accepted receipt and workflow state separately in ClickHouse.

### Task 3: Migrate Task 011 Priority and Eviction Sources

**Files:**
- Create/modify the exact Task-011 files listed in
  `port/task/011-filecache-priority-eviction.md`.
- Modify:
  `port/task/result/011-filecache-priority-eviction-result.md`.

**Interfaces:**
- Consumes: accepted Tasks 003-010 and registered SD2/SD5 mappings.
- Produces: unregistered priority/eviction source that Task 012 compiles against
  real core types.

- [ ] **Step 1: Confirm atomic-stage prerequisites**

Verify:

```text
Velox branch is filecache
Task 003 corrective commit is present
Task 012 contract is ready to run immediately afterward
```

- [ ] **Step 2: Migrate the Task-011 source**

Follow the amended task exactly. Do not add fake `FileCache`, `KeyMetadata`,
`FileSegmentInfo`, or `FileCacheReserveStat` definitions.

- [ ] **Step 3: Run structural checks**

Run the exact symbol and fake-definition checks from Task 011 and:

```bash
git -C /root/oss/velox diff --check
```

Expected: all required symbols found, no fake core definitions, no whitespace
errors.

- [ ] **Step 4: Review and commit Task 011**

The Controller verifies source/structure parity and the migration-only boundary.
No build success is claimed.

Commit:

```bash
git -C /root/oss/velox add -- \
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
git -C /root/oss/velox commit \
  -m 'Task 011: Add `FileCache` priority eviction' \
  -m 'Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
```

Immediately dispatch Task 012 after the receipt commit.

### Task 4: Close the Task 012 Center SCC

**Files:**
- Create/modify the exact Task-012 files listed in
  `port/task/012-filecache-core-scc.md`.
- Modify: `port/task/result/012-filecache-core-scc-result.md`.

**Interfaces:**
- Consumes: Task-011 priority/eviction source and accepted Tasks 003-010.
- Produces: compiled `velox_ch_filecache_core`, complete FileCache API, and
  focused center-SCC tests.

- [ ] **Step 1: Write the RED tests**

Implement every amended Task-012 mandatory test using production classes and
real temporary files. Include:

```text
typed ENOSPC and EDQUOT
generic-exception non-reconciliation
partial-file resume
strict-prefix physical write
query-limit holder lifetime
queue pipeline
shutdown/deactivate/cancel-before-join
SD4 no-reference-across-mutation
```

- [ ] **Step 2: Capture behavioral RED**

Build before adding the core implementation:

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug \
  velox_ch_filecache_core_scc_test \
  > /root/oss/velox/_build/debug/build_task_012_red.log 2>&1
```

Expected: failure for missing Task-012 production types or behavior, not an
environment/configuration error.

- [ ] **Step 3: Implement the complete SCC**

Follow the amended Task 012. Keep typed errno handling, shutdown ordering,
container structure, settings comparison, and registered mappings exact.

- [ ] **Step 4: Build the SCC**

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug \
  velox_ch_filecache_core_scc_test \
  > /root/oss/velox/_build/debug/build_task_012_green.log 2>&1
```

Expected: successful compilation and link of Task-011 and Task-012 sources.

- [ ] **Step 5: Run focused and accumulated tests**

```bash
ctest --test-dir /root/oss/velox/_build/debug \
  -R '^velox_ch_filecache_core_scc_test$' \
  --output-on-failure \
  > /root/oss/velox/_build/debug/test_task_012_core.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug \
  -R '^velox_ch_' \
  --output-on-failure \
  > /root/oss/velox/_build/debug/test_task_012_accumulated.log 2>&1
```

Expected: focused and accumulated FileCache tests pass with zero
skipped/disabled tests.

- [ ] **Step 6: Verify non-mono**

Configure:

```bash
source /root/oss/velox-helper/env.sh
mkdir -p /root/oss/velox/_build/debug-task012-nonmono
/usr/bin/cmake \
  -S /root/oss/velox \
  -B /root/oss/velox/_build/debug-task012-nonmono \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/usr/local/bin/ninja \
  -DCMAKE_TOOLCHAIN_FILE=/root/oss/gluten/dev/vcpkg/toolchain.cmake \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DVELOX_GFLAGS_TYPE=static \
  -DVELOX_BUILD_TESTING=ON \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_ENABLE_EXEC=ON \
  -DVELOX_ENABLE_PARQUET=OFF \
  -DVELOX_ENABLE_REMOTE_FUNCTIONS=ON \
  -DVELOX_ENABLE_GROUPED_TESTS=OFF \
  -DVELOX_MONO_LIBRARY=OFF \
  -DVELOX_BUILD_RUNNER=OFF \
  -DVELOX_ENABLE_GEO=OFF \
  -DVELOX_BUILD_MINIMAL=OFF \
  -DVELOX_SIMDJSON_SKIPUTF8VALIDATION=ON \
  -DMAX_HIGH_MEM_JOBS=16 \
  -DMAX_LINK_JOBS=16 \
  -DVELOX_FORCE_COLORED_OUTPUT=ON \
  > /root/oss/velox/_build/debug-task012-nonmono/configure_task_012_nonmono.log 2>&1
/usr/local/bin/ninja -C /root/oss/velox/_build/debug-task012-nonmono \
  velox_ch_filecache_core_scc_test \
  > /root/oss/velox/_build/debug-task012-nonmono/build_task_012_nonmono.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug-task012-nonmono \
  -R '^velox_ch_filecache_core_scc_test$' \
  --output-on-failure \
  > /root/oss/velox/_build/debug-task012-nonmono/test_task_012_nonmono.log 2>&1
```

Expected: configure reports `VELOX_MONO_LIBRARY=OFF`; focused build and test
pass.

- [ ] **Step 7: Run false-green mutations**

For each material contract, remove or bypass the production behavior named by
the amended task. Every corresponding test must fail for the declared reason.
Restore all mutations and rerun the focused suite.

- [ ] **Step 8: Review and commit Task 012**

Controller verifies the full SCC, tests, physical files, shutdown behavior,
container guarantees, and no test-side reconciliation.

Commit:

```bash
git -C /root/oss/velox add -- \
  velox/ch/Interpreters/FileCache/CMakeLists.txt \
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
  velox/ch/Interpreters/FileCache/EvictionCandidates.cpp \
  velox/ch/Interpreters/FileCache/FileSegmentInfo.h \
  velox/ch/Interpreters/FileCache/FileSegment.h \
  velox/ch/Interpreters/FileCache/FileSegment.cpp \
  velox/ch/Interpreters/FileCache/Metadata.h \
  velox/ch/Interpreters/FileCache/Metadata.cpp \
  velox/ch/Interpreters/FileCache/FileCache.h \
  velox/ch/Interpreters/FileCache/FileCache.cpp \
  velox/ch/Interpreters/FileCache/QueryLimit.h \
  velox/ch/Interpreters/FileCache/QueryLimit.cpp \
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/tests/PriorityEvictionTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileSegmentInfoTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp \
  velox/ch/Interpreters/FileCache/tests/MetadataTest.cpp \
  velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp \
  velox/ch/Interpreters/FileCache/tests/QueryLimitTest.cpp
git -C /root/oss/velox commit \
  -m 'Task 012: Add `FileCache` center SCC' \
  -m 'Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
```

Commit the accepted receipt separately in ClickHouse.

### Task 5: Add Task 013 Factory and Manager

**Files:**
- Create/modify the exact Task-013 files listed in
  `port/task/013-filecache-factory-manager.md`.
- Modify: `port/task/result/013-filecache-factory-manager-result.md`.

**Interfaces:**
- Consumes: accepted Task-012 FileCache API and Manager-owned runtime services.
- Produces: Factory/Manager lifecycle, registry, settings updates, and global
  instance access.

- [ ] **Step 1: Write RED tests**

Cover:

```text
runtime-service lifetime
factory owns no services
registry/settings lock order
deactivate outside registry lock
global instance publication/release
deduplication by shared_ptr control block
overflow through FileCacheUtils::checkedAdd
```

- [ ] **Step 2: Capture RED**

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug \
  velox_ch_filecache_manager_test \
  > /root/oss/velox/_build/debug/build_task_013_red.log 2>&1
```

Expected: failure because the Factory/Manager production implementation is
absent.

- [ ] **Step 3: Implement Task 013**

Follow the amended contract. Do not add a private checked-add helper.

- [ ] **Step 4: Build and test**

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug \
  velox_ch_filecache_manager_test \
  > /root/oss/velox/_build/debug/build_task_013_green.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug \
  -R '^velox_ch_filecache_manager_test$' \
  --output-on-failure \
  > /root/oss/velox/_build/debug/test_task_013_manager.log 2>&1
```

Expected: all focused tests pass.

- [ ] **Step 5: Verify non-mono**

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug-task012-nonmono \
  velox_ch_filecache_manager_test \
  > /root/oss/velox/_build/debug-task012-nonmono/build_task_013_nonmono.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug-task012-nonmono \
  -R '^velox_ch_filecache_manager_test$' \
  --output-on-failure \
  > /root/oss/velox/_build/debug-task012-nonmono/test_task_013_nonmono.log 2>&1
```

Expected: focused non-mono test passes.

- [ ] **Step 6: Review, mutate, and commit**

Run the amended false-green probes, restore, rerun green, then obtain Controller
acceptance.

Commit:

```bash
git -C /root/oss/velox add -- \
  velox/ch/Interpreters/FileCache/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/FileCacheFactory.h \
  velox/ch/Interpreters/FileCache/FileCacheFactory.cpp \
  velox/ch/Interpreters/FileCache/FileCacheManager.h \
  velox/ch/Interpreters/FileCache/FileCacheManager.cpp \
  velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp
git -C /root/oss/velox commit \
  -m 'Task 013: Add `FileCache` manager' \
  -m 'Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
```

### Task 6: Add Task 014 Buffered Input

**Files:**
- Create/modify the exact Task-014 files listed in
  `port/task/014-filecache-buffered-input.md`.
- Modify: `port/task/result/014-filecache-buffered-input-result.md`.

**Interfaces:**
- Consumes: Factory/Manager FileCache API, Task-007 adapters, Task-012
  FileSegment state, and Task-008 checked arithmetic.
- Produces: buffered input and stream consumers for hit/miss, handoff, seek, and
  failure behavior.

- [ ] **Step 1: Write RED tests and migrate CH cases**

Cover:

```text
cache hit and miss
bypass threshold
reader attach/detach/handoff
canceled reader not returned to FileSegment
in-buffer and out-of-buffer seek
source failure
disk failure with skip_cache_on_disk_failure
query-context lifetime
downloader cleanup
getRemoteFileMetadata == nullopt boundary
```

List every assigned CH gtest and its Velox destination or exact exclusion.

- [ ] **Step 2: Capture RED**

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug \
  velox_ch_filecache_buffered_input_test \
  > /root/oss/velox/_build/debug/build_task_014_red.log 2>&1
```

Expected: failure because the buffered-input production implementation is
absent.

- [ ] **Step 3: Implement Task 014**

Follow the amended contract. Change no Gluten file.

- [ ] **Step 4: Build and test**

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug \
  velox_ch_filecache_buffered_input_test \
  > /root/oss/velox/_build/debug/build_task_014_green.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug \
  -R '^velox_ch_filecache_buffered_input_test$' \
  --output-on-failure \
  > /root/oss/velox/_build/debug/test_task_014_buffered_input.log 2>&1
```

Expected: all focused tests pass with no disabled/skipped cases.

- [ ] **Step 5: Verify accumulated behavior**

Run all registered `velox_ch_` tests and the Task-014 mono/non-mono gates with
unique logs.

- [ ] **Step 6: Verify non-mono**

```bash
source /root/oss/velox-helper/env.sh
/usr/local/bin/ninja -C /root/oss/velox/_build/debug-task012-nonmono \
  velox_ch_filecache_buffered_input_test \
  > /root/oss/velox/_build/debug-task012-nonmono/build_task_014_nonmono.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug-task012-nonmono \
  -R '^velox_ch_filecache_buffered_input_test$' \
  --output-on-failure \
  > /root/oss/velox/_build/debug-task012-nonmono/test_task_014_nonmono.log 2>&1
```

Expected: focused non-mono test passes.

- [ ] **Step 7: Review, mutate, and commit**

Run the amended handoff/seek/failure false-green probes, restore, rerun green,
and obtain Controller acceptance.

Commit:

```bash
git -C /root/oss/velox add -- \
  velox/ch/CMakeLists.txt \
  velox/ch/Disks/CMakeLists.txt \
  velox/ch/Disks/IO/CMakeLists.txt \
  velox/ch/Disks/IO/FileCacheRequestContext.h \
  velox/ch/Disks/IO/FileCacheFileIdentity.h \
  velox/ch/Disks/IO/FileCacheBufferedInput.h \
  velox/ch/Disks/IO/FileCacheBufferedInput.cpp \
  velox/ch/Disks/IO/FileCacheInputStream.h \
  velox/ch/Disks/IO/FileCacheInputStream.cpp \
  velox/ch/Disks/IO/tests/CMakeLists.txt \
  velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp
git -C /root/oss/velox commit \
  -m 'Task 014: Add `FileCache` buffered input' \
  -m 'Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
```

### Task 7: Run the Mandatory Tasks 003-014 Full Review

**Files:**
- Create:
  `port/task/fullreview/cross-profile/2/003-014-consumer-contract-ledger.md`
- Create:
  `port/task/fullreview/cross-profile/2/003-014-full-review-result.md`
- Modify: `port/task/CONTROLLER_HANDOFF.md`
- Modify: `port/task/EXECUTION_PROTOCOL.md`

**Interfaces:**
- Consumes: accepted Tasks 003-014, CH source/real callers, registered
  deviations, tests, and receipts.
- Produces: the mandatory zero-unresolved gate before Task 015.

- [ ] **Step 1: Dispatch the A ledger agent**

Use section A of the authoring guide over Tasks 003-014, including center SCC,
Factory/Manager, and reader consumers.

- [ ] **Step 2: Apply the Controller A gate**

Verify every call site, contract row, structure row, task owner, and E-probe
candidate.

- [ ] **Step 3: Dispatch the D review agent**

Use section D of the guide. Require behavior and internal structure review over:

```text
priority and eviction
FileSegment state/accounting
Metadata locks/lifetimes
FileCache settings/shutdown
QueryLimit holder destruction
Factory/Manager ownership
reader handoff
cache hit/miss and seek
source/disk failure
exception cleanup
```

- [ ] **Step 4: Apply the Controller verdict gate**

Independently verify all non-matches, structural deviations, and high-risk
matches. Record `reopen proposed` on any finding.

- [ ] **Step 5: Stop**

Set:

```text
task_015_allowed: false
```

Do not dispatch Task 015. Present the full-review result to the user and wait
for explicit approval.
