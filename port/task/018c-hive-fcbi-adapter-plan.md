# Task 018-C Velox Hive FCBI Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Velox-native Hive/Parquet scans select the accepted `FileCacheBufferedInput` whenever Task 018 installs a `FileCacheManager`.

**Architecture:** Add a fail-close FileCache branch before the existing CBI/direct branches in `HiveConnectorUtil::createBufferedInput`. Map current manager, file identity, request context, statistics, executor, reader options, file-read options, and cancellation token into the accepted 13-parameter FCBI constructor.

**Tech Stack:** C++20, Velox Hive connector, FileCacheManager, GoogleTest, CMake/Ninja, RelWithDebInfo.

## Global Constraints

```text
Binding design: port/design/filecache-task-018-hive-fcbi-adapter.md
Task 018 remains Velox-only; no Gluten/Spark file is touched.
Task 019 ownership is unchanged.
Selection order: FileCache -> CBI -> existing Nimble/direct.
FileCache and CBI are mutually exclusive and fail closed.
No fallback from FileCache construction/read failure.
Use FileCacheFileIdentity::deriveKey; never hand-roll key hashing.
Copy ConnectorQueryCtx::cancellationToken by value.
Worker does not stage or commit.
No Ninja -j or nproc; all build/test/query output uses unique build-dir logs.
```

---

### Task 1: Add focused Hive FCBI selection and miss-hit tests

**Files:**
- Create: `/root/oss/velox/velox/connectors/hive/tests/HiveFileCacheBufferedInputTest.cpp`
- Modify: `/root/oss/velox/velox/connectors/hive/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `hive::createBufferedInput`, `FileCacheManager`, `ConnectorQueryCtx`
- Proves: direct/CBI/FCBI selection, mapping, mutual exclusion, real miss-fill-hit

- [ ] **Step 1: Add the test source to `velox_hive_connector_test`**

Add:

```cmake
HiveFileCacheBufferedInputTest.cpp
```

to the existing executable source list.

Add these direct test dependencies:

```cmake
velox_ch_filecache_dwio
velox_ch_filecache_manager
velox_ch_filecache_core
velox_ch_filecache
```

- [ ] **Step 2: Build a real fixture**

Reuse the established `HiveConnectorTestBase`/`ConnectorQueryCtx` setup and
FileCache test helpers. The fixture owns, in destruction order:

```text
FileCacheManager shared_ptr
FileCache default-cache shared_ptr
ThreadWheelTimekeeper shared_ptr
memory pool shared_ptr
temporary source/cache directories
```

Teardown order is:

```text
manager->shutdown()
FileCacheManager::setInstance(nullptr)
drop manager/cache
drop timekeeper/pool
remove temporary directories
```

- [ ] **Step 3: Write RED selection tests**

Add tests:

```text
NoManagerNoCbiSelectsDirect
NoManagerWithCbiSelectsCachedBufferedInput
ManagerSelectsFileCacheBufferedInput
ManagerAndCbiFailClosed
```

Before production changes, the manager-selection test must return
`DirectBufferedInput`, and the mutual-exclusion test must not throw. Record this
behavioral RED.

- [ ] **Step 4: Write RED mapping test**

Create a `ConnectorQueryCtx` with:

```text
queryId = query.HiveFileCacheBufferedInputTest
cancellation token = a real CancellationSource token
readerOpts.cacheable = true
file path = fixture source path
```

After dynamic-casting to FCBI, assert:

```cpp
EXPECT_EQ(fcbi->requestContext().queryId, queryId);
EXPECT_EQ(fcbi->requestContext().userId, manager->commonUserId());
EXPECT_EQ(fcbi->requestContext().userWeight, 0);
EXPECT_TRUE(fcbi->requestContext().cacheable);
EXPECT_EQ(
    fcbi->requestContext().segmentType,
    ch::FileSegmentKeyType::Data);
EXPECT_EQ(fcbi->origin().user_id, manager->commonUserId());
EXPECT_EQ(fcbi->origin().weight, std::optional<uint64_t>(0));
EXPECT_EQ(fcbi->origin().segment_type, ch::FileSegmentKeyType::Data);
EXPECT_EQ(
    fcbi->cacheKey(),
    ch::FileCacheFileIdentity::deriveKey({sourcePath, ""}));
EXPECT_TRUE(fcbi->cancellationToken().canBeCancelled());
cancellationSource.requestCancellation();
EXPECT_TRUE(fcbi->cancellationToken().isCancellationRequested());
```

- [ ] **Step 5: Write RED real miss-fill-hit test**

Use deterministic source bytes. Read the full file through the selected FCBI
twice. Assert byte equality and snapshot deltas:

```text
first read:
  cacheMissCount > 0
  sourceReadBytes > 0
  cacheWriteBytes > 0

second read:
  cacheHitCount > 0
  cacheReadBytes > 0
```

The test must fail before the production adapter because the selected input is
direct and FileCache counters stay zero.

- [ ] **Step 6: Run focused RED**

Build and run `velox_hive_connector_test` with a GTest filter covering only the
new suite. Redirect to:

```text
_build/relwithdebinfo/build_018c_hive_fcbi_red.log
_build/relwithdebinfo/test_018c_hive_fcbi_red.log
```

Expected: selection/mapping/miss-hit RED for the stated reasons.

---

### Task 2: Implement the current FileCache adapter

**Files:**
- Modify: `/root/oss/velox/velox/connectors/hive/HiveConnectorUtil.cpp`
- Modify: `/root/oss/velox/velox/connectors/hive/CMakeLists.txt`

**Interfaces:**
- Produces: FCBI selection through the existing `createBufferedInput` signature
- Preserves: existing CBI and direct paths when no manager is installed

- [ ] **Step 1: Add current FileCache includes**

```cpp
#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Disks/IO/FileCacheFileIdentity.h"
#include "velox/ch/Disks/IO/FileCacheRequestContext.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
```

- [ ] **Step 2: Add the branch before CBI**

Implement:

```cpp
if (auto* manager = ch::FileCacheManager::getInstance()) {
  VELOX_USER_CHECK_NULL(
      connectorQueryCtx->cache(),
      "FileCache and AsyncDataCache cannot both be installed");

  auto cache = manager->getDefault();
  VELOX_USER_CHECK_NOT_NULL(cache, "FileCacheManager has no default cache");

  ch::FileCacheRequestContext requestContext;
  requestContext.queryId = connectorQueryCtx->queryId();
  requestContext.userId = manager->commonUserId();
  requestContext.userWeight = 0;
  requestContext.cacheable = readerOpts.cacheable();
  requestContext.segmentType = ch::FileSegmentKeyType::Data;

  ch::FileCacheOriginInfo origin(
      requestContext.userId,
      requestContext.userWeight,
      requestContext.segmentType);

  const ch::FileCacheFileIdentity identity{
      .path = fileHandle.file->getName(),
      .etag = ""};

  return std::make_unique<ch::FileCacheBufferedInput>(
      fileHandle.file,
      std::move(cache),
      ch::FileCacheFileIdentity::deriveKey(identity),
      std::move(origin),
      ch::FileCacheReadOptions{},
      std::move(requestContext),
      dwio::common::MetricsLog::voidLog(),
      std::move(ioStatistics),
      std::move(ioStats),
      executor,
      readerOpts,
      fileReadOps,
      connectorQueryCtx->cancellationToken());
}
```

Do not change the existing CBI/Nimble/direct branches.

- [ ] **Step 3: Link FileCache libraries**

Append these entries to the existing
`velox_link_libraries(velox_hive_connector PRIVATE ...)` block:

```cmake
velox_ch_filecache_dwio
velox_ch_filecache_manager
velox_ch_filecache_core
velox_ch_filecache
```

- [ ] **Step 4: Run GREEN**

Build and run the filtered new suite:

```text
_build/relwithdebinfo/build_018c_hive_fcbi_green.log
_build/relwithdebinfo/test_018c_hive_fcbi_green.log
```

Expected: all new tests pass, no warnings.

- [ ] **Step 5: Mutation RED**

Temporarily disable the manager branch while preserving compilation. The
manager-selection, mapping, and miss-hit tests must fail. Restore the branch and
rerun GREEN.

- [ ] **Step 6: Mono/non-mono linkage gate**

Build the Hive connector test and TPCH benchmark in the accepted mono
RelWithDebInfo tree and the existing non-mono test tree. Every output goes to a
unique build-dir log. No duplicate FileCache source registration is allowed.

- [ ] **Step 7: Review and commit**

Run `git diff --check`, inspect all tracked/untracked task files, and dispatch
an independent task review. Worker does not stage or commit.

Controller commits only the adapter/CMake/test files in one standalone Velox
commit after approval.

---

### Task 3: Re-run TPCH correctness and performance

**Files:**
- Append: `/root/oss/clickhouse/port/task/result/018-filecache-velox-benchmark-result.md`
- Artifacts: `/root/oss/velox/tmp/` and RelWithDebInfo build logs

**Interfaces:**
- Consumes: accepted Hive FCBI adapter and Task-018 TPCH binary
- Produces: non-false-green 22×3 correctness and H2 performance evidence

- [ ] **Step 1: Focused TPCH FCBI proof**

Run q01 in FileCache mode for three rounds with `cold_each_round=false`.
Require:

```text
round 1: FileCache lookup/source/write activity > 0
round 2/3: cache hits > 0 and cache_read_mib > 0
all rounds: rows/hash correct and errors empty
```

- [ ] **Step 2: Re-run all 22×3 correctness**

Use the accepted 32 GiB query / 4 GiB CBI cache configuration. Require all
previous correctness gates plus nonzero FileCache metrics.

- [ ] **Step 3: Re-run H2**

Run smoke, full FileCache, and full three-backend A/B. Reject any all-zero
FileCache metric set before interpreting timing.

- [ ] **Step 4: Report and review**

Report medians and variation using only the corrected adapter run. Mark the
previous 3.5%/2.1% result as superseded invalid evidence. Worker stops at
`ready_for_controller`; Controller independently parses every CSV before Task
018 acceptance.
