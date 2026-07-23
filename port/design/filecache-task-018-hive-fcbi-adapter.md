# Task 018 Velox Hive FCBI Adapter Design

## Status

```text
decision_date: 2026-07-23
decision: approved
owner: Task 018 Velox-only TPCH integration
task_019_ownership: unchanged
```

## Problem

Task 018 installs a process-wide `FileCacheManager` for
`input_source=filecache`, but the current Velox
`HiveConnectorUtil::createBufferedInput` only selects CBI or direct buffered
input. The installed manager has no consumer, so TPCH "filecache" runs use the
direct path.

The false-green signature is definitive: all 66 FileCache performance rows
have zero cache lookups, cache reads, predownloads, and evictions. The reported
performance result is rejected.

## Selection contract

`HiveConnectorUtil::createBufferedInput` uses this order:

```text
FileCacheManager installed -> FileCacheBufferedInput
otherwise ConnectorQueryCtx cache present -> CachedBufferedInput
otherwise existing Nimble/direct selection
```

FileCache and CBI are mutually exclusive. If both are present, fail before
constructing either input.

When no `FileCacheManager` is installed, behavior is byte-for-byte unchanged.

## FileCache mapping

The adapter consumes the accepted current API rather than the old
`baibaichen/ch-filecache` constructor:

```text
manager:
  FileCacheManager::getInstance()

cache:
  manager->getDefault()
  missing default cache is an error

file identity:
  path = fileHandle.file->getName()
  etag = empty until a real object-version provider exists
  key = FileCacheFileIdentity::deriveKey(identity)

request context:
  queryId = connectorQueryCtx->queryId()
  userId = manager->commonUserId()
  userWeight = 0
  cacheable = readerOpts.cacheable()
  segmentType = FileSegmentKeyType::Data

origin:
  FileCacheOriginInfo(userId, userWeight, segmentType)

read behavior:
  FileCacheReadOptions{}

statistics and execution:
  MetricsLog::voidLog()
  existing IoStatistics shared_ptr
  existing IoStats shared_ptr
  existing executor
  existing ReaderOptions
  copied fileReadOps
  copied connectorQueryCtx->cancellationToken()
```

`FileCacheBufferedInput` owns copied value state and shared statistic objects;
streams retain its existing borrowed-owner lifetime contract.

## Files and linkage

```text
velox/connectors/hive/HiveConnectorUtil.cpp
  Add the FileCache selection branch and current API mapping.

velox/connectors/hive/CMakeLists.txt
  Link velox_hive_connector to:
    velox_ch_filecache_dwio
    velox_ch_filecache_manager
    velox_ch_filecache_core
    velox_ch_filecache

velox/connectors/hive/tests/HiveFileCacheBufferedInputTest.cpp
velox/connectors/hive/tests/CMakeLists.txt
  Add focused selection, mutual exclusion, identity/context/token, and real
  miss-fill-hit tests.
```

Mono and non-mono builds must both link. Do not duplicate FileCache sources in
the Hive target.

## Tests

The focused test must prove:

1. no manager + no CBI returns the existing direct input;
2. no manager + CBI returns `CachedBufferedInput`;
3. installed manager + no CBI returns `FileCacheBufferedInput`;
4. manager + CBI fails closed;
5. the returned FCBI carries canonical path identity, manager user identity,
   query ID, cacheable flag, Data segment type, and copied cancellation token;
6. a real first read records a FileCache miss/write and a second read records a
   hit/cache-read with byte-exact output;
7. manager shutdown and singleton withdrawal happen before pool/timekeeper
   destruction.

A mutation that removes the FileCache branch must fail the type and
miss-fill-hit tests.

## TPCH gates

After focused tests pass:

1. rerun all 22×3 correctness;
2. every query must still have identical rows/hash and empty errors;
3. FileCache mode must report nonzero lookups and source/write activity;
4. with `cold_each_round=false`, later rounds must report FileCache hits and
   nonzero `cache_read_mib`;
5. only then rerun H2 performance.

Any all-zero FileCache metric set is a hard false-green failure regardless of
process exit or timing.

## Task 019 boundary

Task 019 remains the owner of Gluten configuration, lifecycle, Builder
selection, JNI/Java/Scala metrics, native Gluten E2E, and Spark E2E. This design
adds only a Velox Hive adapter needed to make Task-018 native TPCH use the
already accepted FCBI implementation.

## Non-goals

```text
object-storage etag retrieval
new user-identity resolution
Gluten Builder changes
Spark integration
new FileCache policy or read semantics
fallback from FileCache failure to CBI/direct
```
