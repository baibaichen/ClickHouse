# Task 018: FileCache Gluten Integration + Velox Benchmark Suite — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate FileCache into Gluten (lifecycle, Builder, metrics bridge) and deliver Velox-side benchmark suite (correctness gate, micro, wrapper A/B, TPCH) consuming accepted Task 017A APIs.

**Architecture:** Velox benchmark binaries reuse the `baibaichen/ch-filecache` commit `45387d564` harness with a thin adapter to the current `FileCacheManager` API. Gluten integration lives in an isolated worktree (`/root/oss/gluten-018`) and wires config → lifecycle → Builder → full metric propagation through C++ → JNI → Java → Scala. No code committed by the worker.

**Tech Stack:** C++20 (Velox — `CMAKE_CXX_STANDARD 20` in `CMakeLists.txt:16`), CMake/Ninja, vcpkg, GTest, folly, Java 8/11 (Gluten JNI), Scala 2.12 (Gluten Spark), gflags.

## Global Constraints

```text
Task 017A: accepted; Task 018 consumes its APIs only.
Task 017B: independent; executes after Task 018.
Task 019: excluded from this plan entirely.
No hard performance regression threshold (baseline only).
No commit by worker; controller commits accepted subtasks.
RelWithDebInfo benchmark build uses vcpkg toolchain.
External dataset: ${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory} (BLOCKED if unset).
All temp/cache directories use tmp/ relative to CWD or build dirs; never /tmp.
Gluten dirty worktree (/root/oss/gluten branch main) is never modified.
All build/test output redirected to log files in the build directory.
Build/test logs are analyzed by task subagents per protocol.
No -j or nproc arguments to ninja.
```

---

## Accepted Task 017A API (consumed, not implemented here)

### File: `velox/ch/Common/FileCacheStats.h`

```cpp
#pragma once

#include <cstdint>

namespace facebook::velox::ch
{

/// RuntimeMetric key for bytes written to the FileCache. Used in IoStats
/// free-form counters; flows through FileDataSource -> RuntimeMetric ->
/// OperatorStats -> TaskStats -> Gluten JNI -> Spark SQLMetric.
inline constexpr const char * kFileCacheWriteBytes = "fileCacheWriteBytes";

/// Point-in-time snapshot of FileCache gauges + cumulative counters.
struct FileCacheStatsSnapshot
{
    // Gauges (from CurrentMetrics)
    int64_t cacheSize = 0;
    int64_t cacheSizeLimit = 0;
    int64_t cacheKeys = 0;
    int64_t cacheElements = 0;
    int64_t cacheFileSegments = 0;
    int64_t holdFileSegments = 0;
    int64_t invalidatedElements = 0;
    int64_t priorityQueueElements = 0;
    int64_t downloadQueueElements = 0;
    int64_t delayedCleanupElements = 0;
    int64_t reserveThreads = 0;

    // Cumulative counters (from ProfileEvents)
    uint64_t cacheReadBytes = 0;
    uint64_t sourceReadBytes = 0;
    uint64_t cacheWriteBytes = 0;
    uint64_t cacheHitCount = 0;
    uint64_t cacheMissCount = 0;
    uint64_t predownloadedFromSourceBytes = 0;
    uint64_t predownloadedBytes = 0;
    uint64_t reserveAttempts = 0;
    uint64_t reserveFailures = 0;
    uint64_t evictedBytes = 0;
    uint64_t evictedSegments = 0;
    uint64_t evictionTries = 0;
    uint64_t waitReadBufferMicroseconds = 0;
    uint64_t readFromSourceMicroseconds = 0;
    uint64_t predownloadedFromSourceMicroseconds = 0;
    uint64_t readFromCacheMicroseconds = 0;
    uint64_t cacheWriteMicroseconds = 0;
    uint64_t createBufferMicroseconds = 0;

    /// Subtract a previous snapshot to get deltas for cumulative counters.
    /// Gauge fields are taken from `*this` (the newer snapshot).
    FileCacheStatsSnapshot operator-(const FileCacheStatsSnapshot & prev) const;
};

/// Loads a point-in-time snapshot from the global metrics storage.
FileCacheStatsSnapshot takeFileCacheStatsSnapshot();

} // namespace facebook::velox::ch
```

Type: `inline constexpr const char *` in `velox/ch/Common/FileCacheStats.h`.
Header: `velox/ch/Common/FileCacheStats.h`.

Usage in `FileCacheInputStream` (Task 017A code):
```cpp
ioStats_->addCounter(std::string(kFileCacheWriteBytes),
    RuntimeCounter(bytesWritten, RuntimeCounter::Unit::kBytes));
```

`IoStats::addCounter` signature (confirmed at `velox/common/file/File.h:57`):
```cpp
void addCounter(const std::string& name, RuntimeCounter counter);
```

### `FileCacheBufferedInput` constructor (current live, no token yet)

```cpp
// velox/ch/Disks/IO/FileCacheBufferedInput.h:65-80
FileCacheBufferedInput(
    std::shared_ptr<ReadFile> readFile,
    FileCachePtr cache,
    FileCacheKey cacheKey,
    FileCacheOriginInfo origin,
    FileCacheReadOptions cacheOptions,
    FileCacheRequestContext requestContext,
    const dwio::common::MetricsLogPtr & metricsLog,
    std::shared_ptr<io::IoStatistics> ioStatistics,
    std::shared_ptr<velox::IoStats> ioStats,
    folly::Executor * executor,
    const dwio::common::ReaderOptions & readerOptions,
    folly::F14FastMap<std::string, std::string> fileReadOps = {});
```

Task 017A appends one parameter at the end:
```cpp
    folly::CancellationToken cancellationToken = {});
```

The **complete** Task-017A constructor order is:
`readFile, cache, cacheKey, origin, cacheOptions, requestContext, metricsLog, ioStatistics, ioStats, executor, readerOptions, fileReadOps, cancellationToken`

Task 017A also produces the public accessor (consumed by the 018-F builder test):
```cpp
const folly::CancellationToken & cancellationToken() const; // returns the stored token
```

### `ConnectorQueryCtx::cancellationToken` (live at `velox/connectors/Connector.h:558`)

```cpp
const folly::CancellationToken& cancellationToken() const {
    return cancellationToken_;
}
```

---

## Reference Benchmark Commit

All benchmark harness adaptation derives from:

```text
Remote: baibaichen/ch-filecache
Commit: 45387d56452c86557f3ed9c39c20ae68b76bddea
        "Mark F17/F18 status from the streaming rewrite in the port audit"
```

Exact source files on that commit (via `git ls-tree --name-only -r baibaichen/ch-filecache`):

```text
velox/benchmarks/AbBenchmarkBase.h              (310 lines)
velox/benchmarks/AbBenchmarkBase.cpp            (310 lines)
velox/benchmarks/AbBenchmarkMain.h              (36 lines)
velox/benchmarks/AbBenchmarkMain.cpp            (146 lines)
velox/benchmarks/CMakeLists.txt                 (adds velox_benchmark_ab library)
velox/benchmarks/tpch/TpchBenchmark.h           (inherits AbBenchmarkBase)
velox/benchmarks/tpch/TpchBenchmark.cpp         (uses dispatchAbMain)
velox/benchmarks/tpch/TpchBenchmarkMain.cpp     (calls dispatchAbMain)
velox/dwio/common/benchmarks/CacheReadHarness.h (40+ lines header)
velox/dwio/common/benchmarks/CacheReadHarness.cpp (593 lines)
velox/dwio/common/benchmarks/CacheVerify.h      (30 lines)
velox/dwio/common/benchmarks/CacheVerify.cpp    (173 lines approx)
velox/dwio/common/benchmarks/CacheVerifyMain.cpp
velox/dwio/common/benchmarks/WorkloadDriver.h   (offset generators)
velox/dwio/common/benchmarks/BufferedInputWrapperBenchmark.cpp (811 lines)
velox/dwio/common/benchmarks/CMakeLists.txt
velox/dwio/common/benchmarks/tests/CacheReadHarnessTest.cpp (108 lines)
velox/dwio/common/benchmarks/tests/CacheVerifyTest.cpp      (173 lines)
velox/dwio/common/benchmarks/tests/CMakeLists.txt
```

The reference uses a bare `ch::FileCache` singleton (`ch::FileCache::setInstance`).
The **current API** uses `FileCacheManager`. The adapter layer (Task 018-A) replaces
only the `installFileCache()` function in `AbBenchmarkMain.cpp`; all other harness
code is near-copy from the reference commit.

---

## Subtask Decomposition

| ID | Title | Scope | Hard Gate | BLOCKED if |
|---|---|---|---|---|
| 018-A | Benchmark adapter + correctness verify | Velox repo | `velox_cache_verify` PASS all modes | Task 017A APIs absent |
| 018-B | Dedicated FCBI micro target + wrapper A/B smoke | Velox repo | FCBI micro exits 0, 3 CSVs produced with no error rows | 018-A not green |
| 018-C | TPCH benchmark correctness | Velox repo | `rows` AND `result_hash` identical across modes for q01 | `${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}` unset |
| 018-D | Orchestration scripts | Velox repo | `bash -n` clean + sentinel tests pass | 018-A not green |
| 018-E | Gluten lifecycle + config | Gluten worktree | Lifecycle GTest green | Worktree creation fails |
| 018-F | Gluten Builder adapter | Gluten worktree | `dynamic_cast<FileCacheBufferedInput*>` succeeds | 018-E not green |
| 018-G | Complete Gluten metric bridge | Gluten worktree | native `sumRuntimeMetric` + Java carrier + Scala updater tests green, JNI/Java/Scala compile | 018-F not green |
| 018-H | Performance waves (baseline) | Velox repo | CSVs collected, no crash | 018-C green, 018-B green |

Execution order: 018-A → 018-B → 018-C (parallel with 018-D) → 018-E → 018-F → 018-G → 018-H

Task 017B is independent and scheduled after Task 018 (not a dependency).

---

## Environment Setup

### Velox benchmark build (RelWithDebInfo)

```bash
source /root/oss/velox-helper/env.sh

# Prechecks (BLOCKED if any fail)
command -v cmake ninja gcc g++ || { echo "BLOCKED: missing build tools"; exit 1; }
test -f "${CMAKE_TOOLCHAIN_FILE:?set CMAKE_TOOLCHAIN_FILE}" || { echo "BLOCKED: vcpkg toolchain missing"; exit 1; }

cmake -S /root/oss/velox -B /root/oss/velox/_build/relwithdebinfo -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_ENABLE_PARQUET=ON \
  -DVELOX_BUILD_TESTING=ON \
  -DVELOX_MONO_LIBRARY=ON \
  -DVELOX_GFLAGS_TYPE=static \
  > /root/oss/velox/_build/relwithdebinfo/configure_018.log 2>&1
echo "exit: $?"
```

### Gluten isolated worktree

```bash
cd /root/oss/gluten
# Create worktree from current HEAD of main on an explicit new branch
git worktree add -b task-018-filecache /root/oss/gluten-018 HEAD
cd /root/oss/gluten-018

source /root/oss/velox-helper/env.sh
cmake -S cpp -B cpp/build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
  -DVELOX_HOME=/root/oss/velox \
  -DVELOX_BUILD_PATH=/root/oss/velox/_build/debug \
  > cpp/build/configure_018.log 2>&1
echo "exit: $?"
```

This preserves the dirty `/root/oss/gluten` (branch `main`) untouched. The worktree
has its own branch `task-018-filecache` from the same HEAD commit.

---

## Task 018-A: Benchmark Adapter + Correctness Verify

**Files:**
- Near-copy from `baibaichen/ch-filecache` (commit `45387d564`):
  - `velox/benchmarks/AbBenchmarkBase.h`
  - `velox/benchmarks/AbBenchmarkBase.cpp`
  - `velox/benchmarks/AbBenchmarkMain.h`
  - `velox/benchmarks/AbBenchmarkMain.cpp`
  - `velox/dwio/common/benchmarks/CacheReadHarness.h`
  - `velox/dwio/common/benchmarks/CacheReadHarness.cpp`
  - `velox/dwio/common/benchmarks/CacheVerify.h`
  - `velox/dwio/common/benchmarks/CacheVerify.cpp`
  - `velox/dwio/common/benchmarks/CacheVerifyMain.cpp`
  - `velox/dwio/common/benchmarks/WorkloadDriver.h`
  - `velox/dwio/common/benchmarks/BufferedInputWrapperBenchmark.cpp`
  - `velox/dwio/common/benchmarks/CMakeLists.txt`
  - `velox/dwio/common/benchmarks/tests/CacheReadHarnessTest.cpp`
  - `velox/dwio/common/benchmarks/tests/CacheVerifyTest.cpp`
  - `velox/dwio/common/benchmarks/tests/CMakeLists.txt`
- Adapter (current API differs from reference):
  - Modify: `velox/benchmarks/AbBenchmarkMain.cpp` — replace `installFileCache()` body
  - Modify: `velox/benchmarks/CMakeLists.txt` — add `velox_benchmark_ab` library

**Interfaces:**
- Consumes: `FileCacheManager::create` (`:108`), `FileCacheManager::setInstance` (`:122`), `FileCacheManager::getInstance` (`:110`), `FileCachePtr getDefault()` (`:127`), `shutdown()` (`:132`), `takeFileCacheStatsSnapshot` (from `velox/ch/Common/FileCacheStats.h`), `kFileCacheWriteBytes`
- Produces: `velox_benchmark_ab` library, `velox_cache_read_harness` library, `velox_cache_verify` binary, `velox_bufferedinput_wrapper_benchmark` binary

**Adapter: `installFileCache()` replacement in `AbBenchmarkMain.cpp`**

The reference uses:
```cpp
auto cache = std::make_unique<ch::FileCache>("ab_benchmark", settings);
cache->initialize();
ch::FileCache::setInstance(cache.get());
```

`FileCacheManager::Options` requires three additional non-null fields that the
reference `ch::FileCache` did not: `localFileSystem`, `memoryPool`, and
`timekeeper` (enforced by `FileCacheManager::validateOptions` at
`velox/ch/Interpreters/FileCache/FileCacheManager.cpp:104-133`). `installFileCache`
runs after `QueryBenchmarkBase::initialize()`, so `memory::memoryManager()` is
live. Own the pool/filesystem/timekeeper in process-lifetime statics that
outlive the manager, and drop them only after `manager->shutdown()`.

Replace with (all `Options` fields verified live at
`velox/ch/Interpreters/FileCache/FileCacheManager.h:95-106`):
```cpp
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/ch/Common/FileCacheStats.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/memory/Memory.h"

#include <folly/futures/ThreadWheelTimekeeper.h>

// Process-lifetime owners. Declaration order matters for destruction: the
// manager (declared last) is destroyed first, then the timekeeper/fs/pool it
// referenced. teardownFileCache() resets them explicitly in the safe order.
static std::shared_ptr<facebook::velox::memory::MemoryPool> g_benchPool;
static std::shared_ptr<facebook::velox::filesystems::FileSystem> g_benchFs;
static std::shared_ptr<folly::ThreadWheelTimekeeper> g_benchTimekeeper;
static std::shared_ptr<ch::FileCacheManager> g_benchManager;

void installFileCache()
{
    std::error_code ec;
    std::filesystem::remove_all(FLAGS_filecache_root, ec);
    ec.clear();
    std::filesystem::create_directories(FLAGS_filecache_root, ec);
    VELOX_USER_CHECK(!ec, "Failed to create --filecache_root: {} ({})",
        FLAGS_filecache_root, ec.message());

    // FileCacheManager cache paths must be absolute (validateOptions).
    const std::string root =
        std::filesystem::absolute(FLAGS_filecache_root).string();

    facebook::velox::filesystems::registerLocalFileSystem();
    g_benchPool = facebook::velox::memory::memoryManager()->addLeafPool(
        "filecache_bench");
    g_benchFs = facebook::velox::filesystems::getFileSystem(root, {});
    g_benchTimekeeper = std::make_shared<folly::ThreadWheelTimekeeper>();

    ch::FileCacheConfig cfg;
    cfg.path = root;
    cfg.maxSize = static_cast<uint64_t>(FLAGS_filecache_disk_gib) << 30;

    ch::FileCacheManager::Options opts;
    opts.caches = {{.name = "default", .config = cfg, .configPath = root}};
    opts.defaultCacheName = "default";
    opts.commonUserId = "benchmark";
    opts.cachePathPrefix = root;
    opts.allowedCacheRoot = root;
    opts.localFileSystem = g_benchFs;
    opts.memoryPool = g_benchPool.get();
    opts.timekeeper = g_benchTimekeeper;
    opts.initializeOnCreate = true;

    g_benchManager = ch::FileCacheManager::create(std::move(opts));
    ch::FileCacheManager::setInstance(g_benchManager.get());
}

void teardownFileCache()
{
    if (g_benchManager)
    {
        auto snapshot = ch::takeFileCacheStatsSnapshot();
        LOG(INFO) << "FileCache teardown: cacheSize=" << snapshot.cacheSize
                  << " cacheReadBytes=" << snapshot.cacheReadBytes
                  << " sourceReadBytes=" << snapshot.sourceReadBytes
                  << " cacheWriteBytes=" << snapshot.cacheWriteBytes
                  << " hitCount=" << snapshot.cacheHitCount
                  << " missCount=" << snapshot.cacheMissCount
                  << " evictedBytes=" << snapshot.evictedBytes;
        // Documented strict order: shutdown -> setInstance(nullptr) -> drop the
        // owning shared_ptr, THEN release the timekeeper/fs/pool the manager
        // referenced (manager destruction already released its references).
        g_benchManager->shutdown();
        ch::FileCacheManager::setInstance(nullptr);
        g_benchManager.reset();
        g_benchTimekeeper.reset();
        g_benchFs.reset();
        g_benchPool.reset();
    }
}
```

The `CacheReadHarness` adapter replaces `ch::FileCache::getInstance()` with:
```cpp
auto* mgr = ch::FileCacheManager::getInstance();
VELOX_CHECK_NOT_NULL(mgr);
auto defaultCache = mgr->getDefault();
```

The reference `AbBenchmarkBase.cpp` `runAb()` (verified via
`git show baibaichen/ch-filecache:velox/benchmarks/AbBenchmarkBase.cpp`) builds a
`Row row;` per (round, query), fills `row.rows` from `pipeline.operatorStats.back().outputPositions`,
snapshots the backend via a `snapshotBackend()` hook, and writes one CSV line via
`writeCsvRow`. The reference `Row` struct and CSV header (`AbBenchmarkBase.cpp:85,100`)
are `round,query_id,wall_ms,rows,bytes_read,hit_pct,bytes_dl_mib,evict_mib,op_p50_us,op_p95_us,error`.

Two adapter changes to `AbBenchmarkBase.cpp`:

(1) Point `snapshotBackend()` at the new stats API. The reference computes
`hit_pct`/`bytes_dl_mib`/`evict_mib` from a backend snapshot; replace the
snapshot type with `FileCacheStatsSnapshot`:
```cpp
#include "velox/ch/Common/FileCacheStats.h"

// snapshotBackend() body:
const auto s = ch::takeFileCacheStatsSnapshot();
// populateBackendDelta(row, before, after) already subtracts via operator-:
//   auto delta = after - before;  // FileCacheStatsSnapshot::operator-
//   row.bytesDlMib = delta.predownloadedFromSourceBytes / (1<<20); etc.
```

(2) Add a content checksum so correctness compares actual result bytes, not
just row counts. `run()` returns `std::pair<unique_ptr<TaskCursor>, std::vector<RowVectorPtr>> {cursor, results}`
(reference `AbBenchmarkBase.h:59`). Add a `uint64_t resultHash{};` field to `Row`,
an order-independent (commutative) accumulation over every result row, and emit
it as a new CSV column after `rows`:
```cpp
// In the success branch of runAb(), after:
// auto [cursor, results] = run(plans[i], queryConfigs_);
uint64_t resultHash = 0;
for (const auto& rv : results) {
    if (rv == nullptr) {
        continue;
    }
    // BaseVector::hashValueAt returns uint64_t (velox/vector/BaseVector.h:377).
    // Summation is commutative, so multi-driver row reordering does not change
    // the checksum: identical result content -> identical hash across modes.
    for (velox::vector_size_t r = 0; r < rv->size(); ++r) {
        resultHash += rv->hashValueAt(r);
    }
}
row.resultHash = resultHash;
```

Update `writeCsvHeader`/`writeCsvRow` to insert `result_hash` immediately after
`rows` (so the header becomes
`round,query_id,wall_ms,rows,result_hash,bytes_read,hit_pct,bytes_dl_mib,evict_mib,op_p50_us,op_p95_us,error`):
```cpp
// writeCsvHeader:
out << "round,query_id,wall_ms,rows,result_hash,bytes_read,hit_pct,"
    << "bytes_dl_mib,evict_mib,op_p50_us,op_p95_us,error\n";
// writeCsvRow (after `<< row.rows << ","`):
out << row.resultHash << ",";
```

With this column `rows` is CSV field 4 and `result_hash` is CSV field 5.

- [ ] **Step 1: Copy benchmark sources from reference**

```bash
cd /root/oss/velox
git show baibaichen/ch-filecache:velox/benchmarks/AbBenchmarkBase.h > velox/benchmarks/AbBenchmarkBase.h
git show baibaichen/ch-filecache:velox/benchmarks/AbBenchmarkBase.cpp > velox/benchmarks/AbBenchmarkBase.cpp
git show baibaichen/ch-filecache:velox/benchmarks/AbBenchmarkMain.h > velox/benchmarks/AbBenchmarkMain.h
git show baibaichen/ch-filecache:velox/benchmarks/AbBenchmarkMain.cpp > velox/benchmarks/AbBenchmarkMain.cpp
mkdir -p velox/dwio/common/benchmarks/tests
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/CacheReadHarness.h > velox/dwio/common/benchmarks/CacheReadHarness.h
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/CacheReadHarness.cpp > velox/dwio/common/benchmarks/CacheReadHarness.cpp
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/CacheVerify.h > velox/dwio/common/benchmarks/CacheVerify.h
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/CacheVerify.cpp > velox/dwio/common/benchmarks/CacheVerify.cpp
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/CacheVerifyMain.cpp > velox/dwio/common/benchmarks/CacheVerifyMain.cpp
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/WorkloadDriver.h > velox/dwio/common/benchmarks/WorkloadDriver.h
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/BufferedInputWrapperBenchmark.cpp > velox/dwio/common/benchmarks/BufferedInputWrapperBenchmark.cpp
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/CMakeLists.txt > velox/dwio/common/benchmarks/CMakeLists.txt
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/tests/CacheReadHarnessTest.cpp > velox/dwio/common/benchmarks/tests/CacheReadHarnessTest.cpp
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/tests/CacheVerifyTest.cpp > velox/dwio/common/benchmarks/tests/CacheVerifyTest.cpp
git show baibaichen/ch-filecache:velox/dwio/common/benchmarks/tests/CMakeLists.txt > velox/dwio/common/benchmarks/tests/CMakeLists.txt
git show baibaichen/ch-filecache:velox/benchmarks/CMakeLists.txt > velox/benchmarks/CMakeLists.txt
```

- [ ] **Step 2: Apply adapter to `AbBenchmarkMain.cpp`**

- Replace the reference `installFileCache()`/`teardownFileCache()` with the
  `FileCacheManager`-based versions shown above (statics for pool/fs/timekeeper).
- The reference returns `std::unique_ptr<ch::FileCache>` held in `ownedFileCache`
  with a `folly::makeGuard` that calls `ch::FileCache::setInstance(nullptr)`, and a
  `setColdResetFn` lambda that re-installs the cache each cold round. Rewrite all
  three to the Manager API: drop `ownedFileCache`; call `installFileCache()` on
  the filecache path; replace the guard body with `teardownFileCache()`; and make
  the cold-reset lambda `teardownFileCache(); installFileCache();`.
- Update includes to `velox/ch/Interpreters/FileCache/FileCacheManager.h`,
  `velox/ch/Interpreters/FileCache/FileCacheSettings.h`,
  `velox/ch/Common/FileCacheStats.h`, `velox/common/file/FileSystems.h`,
  `velox/common/memory/Memory.h`, and `<folly/futures/ThreadWheelTimekeeper.h>`.

- [ ] **Step 3: Apply adapter to `CacheReadHarness.cpp`**

Replace every `ch::FileCache::getInstance()` call with:
```cpp
auto* mgr = ch::FileCacheManager::getInstance();
VELOX_CHECK_NOT_NULL(mgr);
auto cache = mgr->getDefault();
```

Replace `ch::FileCache::setInstance(nullptr)` teardown with:
```cpp
ch::FileCacheManager::setInstance(nullptr);
```

- [ ] **Step 4: Apply adapter to `AbBenchmarkBase.cpp`**

Point `snapshotBackend()` at `ch::takeFileCacheStatsSnapshot()` and compute
deltas via `FileCacheStatsSnapshot::operator-` (add `#include "velox/ch/Common/FileCacheStats.h"`).
Add the `uint64_t resultHash{};` field to `Row`, the commutative
`hashValueAt` accumulation over `results`, and the `result_hash` CSV column
after `rows`, exactly as shown in the "AbBenchmarkBase.cpp" adapter block above.

- [ ] **Step 5: Build `velox_cache_verify` target**

```bash
source /root/oss/velox-helper/env.sh
ninja -C /root/oss/velox/_build/relwithdebinfo velox_cache_verify \
  > /root/oss/velox/_build/relwithdebinfo/build_018a.log 2>&1
echo "exit: $?"
```

Expected: exit 0.

- [ ] **Step 6: Run correctness gate**

```bash
mkdir -p tmp/fc_verify_018
/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_cache_verify \
  --target_ws_mb=64 \
  --read_size_kib=1024 \
  --filecache_root=tmp/fc_verify_018 \
  --filecache_disk_gib=1 \
  > /root/oss/velox/_build/relwithdebinfo/test_018a_verify.log 2>&1
echo "exit: $?"
grep -c "PASS" /root/oss/velox/_build/relwithdebinfo/test_018a_verify.log
```

Expected: exit 0, all modes PASS byte-exact verification.

- [ ] **Step 7: Build and run harness unit tests**

```bash
ninja -C /root/oss/velox/_build/relwithdebinfo velox_cache_read_harness_test velox_cache_verify_test \
  > /root/oss/velox/_build/relwithdebinfo/build_018a_tests.log 2>&1
ctest --test-dir /root/oss/velox/_build/relwithdebinfo \
  -R '^velox_cache_(read_harness|verify)_test$' --output-on-failure \
  > /root/oss/velox/_build/relwithdebinfo/test_018a_unit.log 2>&1
echo "exit: $?"
```

Expected: exit 0.

- [ ] **Step 8: Mutation — corrupt returned bytes (content-corruption correctness)**

**File:** `velox/dwio/common/benchmarks/CacheReadHarness.cpp`
**Function:** `CacheReadHarness::readAndVerifyBlock` (the function that reads from FCBI and compares against the expected buffer)

Inject at the return point of the cached-read path, immediately after `readFile->pread(offset, length, buffer.data())` succeeds:
```cpp
// MUTATION: corrupt first byte of returned buffer to prove verify catches it
if (length > 0)
{
    buffer.data()[0] ^= 0xFF;
}
```

Re-run:
```bash
/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_cache_verify \
  --target_ws_mb=64 --read_size_kib=1024 \
  --filecache_root=tmp/fc_verify_018_mut \
  --filecache_disk_gib=1 \
  > /root/oss/velox/_build/relwithdebinfo/test_018a_mut_byte.log 2>&1
```

**Expected failed assertion:** `VELOX_CHECK_EQ(actual[0], expected[0])` in `readAndVerifyBlock` reports byte mismatch. Process exits non-zero.

Remove mutation after confirming RED.

- [ ] **Step 9: Statistics mutation is owned by Task 018-G, not here**

The only statistics carrier line inside `velox_cache_verify`'s reach is
`FileCacheInputStream.cpp`'s `ioStats_->addCounter(kFileCacheWriteBytes, ...)`,
which is **Task 017A-owned source** — Task 018 owns no line there, so this plan
must not mutate it (see Global Constraints: Task 018 consumes Task 017A APIs
only). The 018-owned statistics carrier and its dedicated RED mutation live in
Task 018-G (the Gluten `Metrics.getOperatorMetrics` `fileCacheWriteBytes` pass,
proven by `MetricsCarrierTest`). The content-corruption mutation in Step 8
above (over the 018-owned `CacheReadHarness.cpp`) is the correctness gate for
this subtask.

**Gate:** `velox_cache_verify` PASS all modes; Step 8 content-corruption
mutation confirmed RED then restored.

---

## Task 018-B: Dedicated FCBI Micro + Wrapper A/B Smoke

**Files:**
- Already created in 018-A: `velox/dwio/common/benchmarks/BufferedInputWrapperBenchmark.cpp`
- Target: `velox_bufferedinput_wrapper_benchmark` (registered in CMakeLists from 018-A)
- Existing FCBI seek micro: `velox/ch/benchmarks/FileCacheSeekBenchmark.cpp` (already in repo, target `velox_ch_filecache_seek_benchmark` in `velox/ch/benchmarks/CMakeLists.txt`)
- Create dedicated FCBI micro target: `velox/ch/benchmarks/FileCacheBufferedInputBenchmark.cpp`
- Modify: `velox/ch/benchmarks/CMakeLists.txt` — add `velox_ch_fcbi_benchmark`

**Interfaces:**
- Consumes: `FileCacheManager` API, `FileCacheBufferedInput`, test helpers from `velox/ch/Disks/IO/tests/FileCacheTestHelpers.h` (`makeManager`, `makeInput`, `makeDeterministicData`, `readAll`, `CountingReadFile`, `FileCacheTestOptions`), `velox_bufferedinput_wrapper_benchmark` binary (from 018-A)
- Produces: `velox_ch_fcbi_benchmark` binary (folly benchmark table on stdout), and wrapper A/B CSV files at `tmp/wrapper_direct.csv`, `tmp/wrapper_cbi.csv`, `tmp/wrapper_fc.csv` (from `velox_bufferedinput_wrapper_benchmark`)

**FCBI micro benchmark (`FileCacheBufferedInputBenchmark.cpp`):**

This is a dedicated micro target that exercises `FileCacheBufferedInput` through the full `read`/`Next` path (unlike the existing seek benchmark that tests only `seekToPosition`). It reuses `FileCacheTestHelpers.h` and mirrors the proven fixture setup of `velox/ch/benchmarks/FileCacheSeekBenchmark.cpp`. The exact live helper signatures are:

```cpp
// velox/ch/Disks/IO/tests/FileCacheTestHelpers.h
std::vector<char> makeDeterministicData(size_t n);
std::shared_ptr<FileCacheManager> makeManager(
    const std::string& cacheDir, memory::MemoryPool* pool,
    const std::shared_ptr<filesystems::FileSystem>& fs,
    const std::shared_ptr<folly::Timekeeper>& timekeeper,
    const FileCacheTestOptions& testOpts = {});
std::unique_ptr<FileCacheBufferedInput> makeInput(
    FileCacheManager& manager, FileCachePtr cache, std::shared_ptr<ReadFile> source,
    FileCacheKey key, memory::MemoryPool* pool, folly::Executor* executor,
    FileCacheReadOptions opts = {}, const std::string& queryId = "q");
std::vector<char> readAll(dwio::common::SeekableInputStream& stream);
```

Complete file `velox/ch/benchmarks/FileCacheBufferedInputBenchmark.cpp`:

```cpp
#include "velox/ch/Disks/IO/tests/FileCacheTestHelpers.h"
#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/memory/Memory.h"
#include "velox/dwio/common/Options.h"

#include <folly/Benchmark.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/futures/ManualTimekeeper.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>

#include <filesystem>
#include <random>

DEFINE_int32(file_size_mb, 128, "Synthetic file size in MiB");
DEFINE_int32(cache_size_mb, 256, "FileCache max size in MiB");
DEFINE_string(cache_dir, "tmp/fc_fcbi_bench", "FileCache directory path");
DEFINE_int32(region_size_kib, 1024, "Region size in KiB per read");
DEFINE_int32(regions_per_iter, 16, "Regions read per benchmark iteration");

namespace facebook::velox::ch
{
namespace
{
using test::CountingReadFile;
using test::FileCacheTestOptions;
using test::makeDeterministicData;
using test::makeInput;
using test::makeManager;
using test::readAll;

struct FcbiFixture
{
    std::shared_ptr<memory::MemoryPool> pool;
    std::shared_ptr<filesystems::FileSystem> fs;
    std::shared_ptr<folly::Timekeeper> timekeeper;
    std::shared_ptr<folly::CPUThreadPoolExecutor> executor;
    std::shared_ptr<FileCacheManager> manager;
    FileCachePtr cache;
    std::shared_ptr<CountingReadFile> sourceFile;
    std::vector<char> data;
    FileCacheKey hotKey; // pre-warmed in setup for the hot benchmarks
};

static std::unique_ptr<FcbiFixture> gFixture;

uint64_t regionBytes()
{
    return static_cast<uint64_t>(FLAGS_region_size_kib) * 1024;
}

// Reads `regions` regions of `regionBytes()` starting at `startOffset` through a
// fresh FCBI over `key`; returns total bytes returned. A fresh (random) key
// yields a cold fill; the warmed hotKey yields cache hits.
size_t readRegions(const FileCacheKey & key, uint64_t startOffset, int regions)
{
    auto & f = *gFixture;
    FileCacheReadOptions opts;
    opts.remoteFsBufferSize = regionBytes();
    opts.localFsBufferSize = regionBytes();
    auto inp = makeInput(
        *f.manager, f.cache, f.sourceFile, key, f.pool.get(), f.executor.get(), opts);
    size_t total = 0;
    uint64_t offset = startOffset;
    for (int r = 0; r < regions && offset + regionBytes() <= f.data.size(); ++r)
    {
        auto stream = inp->read(offset, regionBytes(), dwio::common::LogType::STREAM);
        total += readAll(*stream).size();
        offset += regionBytes();
    }
    return total;
}

void warmHotKey()
{
    auto & f = *gFixture;
    const uint64_t totalRegions = f.data.size() / regionBytes();
    for (uint64_t start = 0; start < totalRegions; start += FLAGS_regions_per_iter)
        readRegions(f.hotKey, start * regionBytes(), FLAGS_regions_per_iter);
}

void setupFixture()
{
    filesystems::registerLocalFileSystem();
    gFixture = std::make_unique<FcbiFixture>();
    gFixture->pool = memory::deprecatedAddDefaultLeafMemoryPool("fcbi_bench");

    std::string cacheDir = FLAGS_cache_dir;
    if (!cacheDir.empty() && cacheDir[0] != '/')
        cacheDir = std::filesystem::absolute(cacheDir).string();
    std::filesystem::create_directories(cacheDir);

    gFixture->fs = filesystems::getFileSystem(cacheDir, {});
    gFixture->timekeeper = std::make_shared<folly::ManualTimekeeper>();
    gFixture->executor = std::make_shared<folly::CPUThreadPoolExecutor>(2);

    const size_t fileSize = static_cast<size_t>(FLAGS_file_size_mb) * 1024 * 1024;
    gFixture->data = makeDeterministicData(fileSize);
    gFixture->sourceFile = std::make_shared<CountingReadFile>(gFixture->data);

    FileCacheTestOptions testOpts;
    testOpts.maxSize = static_cast<uint64_t>(FLAGS_cache_size_mb) * 1024 * 1024;
    testOpts.maxFileSegmentSize = regionBytes();
    gFixture->manager = makeManager(
        cacheDir, gFixture->pool.get(), gFixture->fs, gFixture->timekeeper, testOpts);
    FileCacheManager::setInstance(gFixture->manager.get());
    gFixture->cache = gFixture->manager->getDefault();

    gFixture->hotKey = FileCacheKey::random();
    warmHotKey(); // read the whole file once so hot benchmarks hit the cache
}

// Sequential read of regions_per_iter regions over the warmed key (cache hits).
BENCHMARK(FCBI_SequentialHot, n)
{
    auto & f = *gFixture;
    const uint64_t span = FLAGS_regions_per_iter * regionBytes();
    const uint64_t maxStart = f.data.size() > span ? f.data.size() - span : 0;
    for (unsigned i = 0; i < n; ++i)
    {
        uint64_t start = maxStart == 0 ? 0 : (static_cast<uint64_t>(i) * span) % maxStart;
        start -= start % regionBytes();
        folly::doNotOptimizeAway(readRegions(f.hotKey, start, FLAGS_regions_per_iter));
    }
}

// Random-offset reads over the warmed key (cache hits).
BENCHMARK(FCBI_RandomHot, n)
{
    auto & f = *gFixture;
    std::mt19937_64 gen(42);
    const uint64_t maxStart =
        f.data.size() > regionBytes() ? f.data.size() - regionBytes() : 0;
    std::uniform_int_distribution<uint64_t> dist(0, maxStart);
    for (unsigned i = 0; i < n; ++i)
    {
        for (int r = 0; r < FLAGS_regions_per_iter; ++r)
        {
            uint64_t offset = dist(gen);
            offset -= offset % regionBytes();
            folly::doNotOptimizeAway(readRegions(f.hotKey, offset, 1));
        }
    }
}

// Cold fill: a fresh random key each iteration forces a source read + cache
// write (never previously cached, even across process restarts).
BENCHMARK(FCBI_SequentialCold, n)
{
    for (unsigned i = 0; i < n; ++i)
    {
        FileCacheKey coldKey = FileCacheKey::random();
        folly::doNotOptimizeAway(readRegions(coldKey, 0, FLAGS_regions_per_iter));
    }
}

} // namespace
} // namespace facebook::velox::ch

int main(int argc, char ** argv)
{
    folly::init(&argc, &argv);
    facebook::velox::ch::setupFixture();

    // Smoke: one hot read must return data before timing.
    VELOX_CHECK_GT(
        facebook::velox::ch::readRegions(
            facebook::velox::ch::gFixture->hotKey, 0,
            FLAGS_regions_per_iter),
        0,
        "FCBI smoke read returned no data");

    folly::runBenchmarks();

    if (facebook::velox::ch::gFixture && facebook::velox::ch::gFixture->manager)
    {
        facebook::velox::ch::gFixture->manager->shutdown();
        facebook::velox::ch::FileCacheManager::setInstance(nullptr);
    }
    // Destroy the fixture explicitly: manager before pool.
    facebook::velox::ch::gFixture.reset();
    return 0;
}
```

CMakeLists addition in `velox/ch/benchmarks/CMakeLists.txt`:
```cmake
add_executable(velox_ch_fcbi_benchmark FileCacheBufferedInputBenchmark.cpp)

target_link_libraries(
  velox_ch_fcbi_benchmark
  PRIVATE
    velox_ch_filecache_dwio
    velox_ch_filecache_manager
    velox_ch_filecache_core
    velox_ch_filecache
    velox_file
    velox_exception
    velox_memory
    Folly::folly
    Folly::follybenchmark
    gflags::gflags)
```

- [ ] **Step 1: Create `FileCacheBufferedInputBenchmark.cpp` (dedicated FCBI micro)**

Create the file with the complete contents shown in the "FCBI micro benchmark"
block above (fixture + `readRegions`/`warmHotKey`/`setupFixture` helpers +
`FCBI_SequentialHot`, `FCBI_RandomHot`, `FCBI_SequentialCold` benchmarks +
`main`).

- [ ] **Step 2: Register `velox_ch_fcbi_benchmark` in `velox/ch/benchmarks/CMakeLists.txt`**

- [ ] **Step 3: Build wrapper benchmark and both FCBI micros**

```bash
source /root/oss/velox-helper/env.sh
ninja -C /root/oss/velox/_build/relwithdebinfo \
  velox_bufferedinput_wrapper_benchmark \
  velox_ch_filecache_seek_benchmark \
  velox_ch_fcbi_benchmark \
  > /root/oss/velox/_build/relwithdebinfo/build_018b.log 2>&1
echo "exit: $?"
```

- [ ] **Step 4: Smoke run — direct mode**

```bash
mkdir -p tmp
/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
  --input_source=direct \
  --target_ws_gb=1 \
  --read_sizes_kib=1024 \
  --workloads=sequential \
  --measure_passes=1 \
  --rounds=1 \
  --out=tmp/wrapper_direct.csv \
  > /root/oss/velox/_build/relwithdebinfo/test_018b_direct.log 2>&1
echo "exit: $?"
```

- [ ] **Step 5: Smoke run — cbi mode**

```bash
/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
  --input_source=cbi \
  --cache_gb=2 \
  --target_ws_gb=1 \
  --read_sizes_kib=1024 \
  --workloads=sequential \
  --measure_passes=1 \
  --rounds=1 \
  --out=tmp/wrapper_cbi.csv \
  > /root/oss/velox/_build/relwithdebinfo/test_018b_cbi.log 2>&1
echo "exit: $?"
```

- [ ] **Step 6: Smoke run — filecache mode**

```bash
/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
  --input_source=filecache \
  --filecache_root=tmp/fc_018b \
  --filecache_disk_gib=4 \
  --target_ws_gb=1 \
  --read_sizes_kib=1024 \
  --workloads=sequential \
  --measure_passes=1 \
  --rounds=1 \
  --out=tmp/wrapper_fc.csv \
  > /root/oss/velox/_build/relwithdebinfo/test_018b_fc.log 2>&1
echo "exit: $?"
```

- [ ] **Step 7: Validate CSV schema and content**

```bash
head -1 tmp/wrapper_direct.csv tmp/wrapper_cbi.csv tmp/wrapper_fc.csv
# Expect identical header
wc -l tmp/wrapper_direct.csv tmp/wrapper_cbi.csv tmp/wrapper_fc.csv
# Each must have >=2 lines (header + 1+ data rows)
grep -c "error" tmp/wrapper_fc.csv
# Error column must be empty for all data rows
```

- [ ] **Step 8: Run dedicated FCBI micro baseline**

```bash
mkdir -p tmp/fc_fcbi_018b
/root/oss/velox/_build/relwithdebinfo/velox/ch/benchmarks/velox_ch_fcbi_benchmark \
  --bm_min_iters=5 \
  --file_size_mb=128 \
  --cache_dir=tmp/fc_fcbi_018b \
  --cache_size_mb=256 \
  > /root/oss/velox/_build/relwithdebinfo/test_018b_fcbi.log 2>&1
echo "exit: $?"
```

**Gate:** All three wrapper CSVs have >=1 data row with empty error field. Both FCBI micro and seek micro exit 0.

---

## Task 018-C: TPCH Benchmark Correctness

**Files:**
- Near-copy from reference: `velox/benchmarks/tpch/TpchBenchmark.h`, `velox/benchmarks/tpch/TpchBenchmark.cpp`, `velox/benchmarks/tpch/TpchBenchmarkMain.cpp`
- Modify: `velox/benchmarks/tpch/CMakeLists.txt` (link `velox_benchmark_ab`)

**Interfaces:**
- Consumes: `AbBenchmarkBase` (from 018-A), `dispatchAbMain`, `QueryBenchmarkBase`
- Produces: `velox_tpch_benchmark` binary with `--input_source={direct,cbi,filecache}`, `--num_splits_per_file=1`, `--include_results`

**BLOCKED semantics:** If `${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}` is unset, this task cannot run. The variable must point to a directory containing TPC-H SF1+ Parquet files.

- [ ] **Step 1: Copy TPCH benchmark from reference**

```bash
cd /root/oss/velox
mkdir -p velox/benchmarks/tpch
git show baibaichen/ch-filecache:velox/benchmarks/tpch/TpchBenchmark.h > velox/benchmarks/tpch/TpchBenchmark.h
git show baibaichen/ch-filecache:velox/benchmarks/tpch/TpchBenchmark.cpp > velox/benchmarks/tpch/TpchBenchmark.cpp
git show baibaichen/ch-filecache:velox/benchmarks/tpch/TpchBenchmarkMain.cpp > velox/benchmarks/tpch/TpchBenchmarkMain.cpp
git show baibaichen/ch-filecache:velox/benchmarks/tpch/CMakeLists.txt > velox/benchmarks/tpch/CMakeLists.txt
```

- [ ] **Step 2: Build TPCH benchmark**

```bash
source /root/oss/velox-helper/env.sh
ninja -C /root/oss/velox/_build/relwithdebinfo velox_tpch_benchmark \
  > /root/oss/velox/_build/relwithdebinfo/build_018c.log 2>&1
echo "exit: $?"
```

- [ ] **Step 3: Run q01 in all three modes with `num_splits_per_file=1`**

```bash
mkdir -p tmp
for MODE in direct cbi filecache; do
  /root/oss/velox/_build/relwithdebinfo/velox/benchmarks/tpch/velox_tpch_benchmark \
    --input_source=$MODE \
    --data_path="${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}" \
    --data_format=parquet \
    --query_id=1 \
    --rounds=1 \
    --num_splits_per_file=1 \
    --num_drivers=4 \
    --filecache_root=tmp/fc_tpch_$MODE \
    --filecache_disk_gib=10 \
    --cache_gb=4 \
    --out=tmp/tpch_q01_$MODE.csv \
    > /root/oss/velox/_build/relwithdebinfo/test_018c_$MODE.log 2>&1
  echo "$MODE exit: $?"
done
```

- [ ] **Step 4: Verify row-count AND result-checksum correctness**

Row count alone is insufficient (two different result sets can share a
cardinality). Compare BOTH `rows` (CSV field 4) and `result_hash` (CSV field 5,
the commutative `hashValueAt` checksum added to the harness in 018-A):

```bash
check_row() { awk -F, -v r=2 'NR==r{print $4}' "$1"; }
check_hash() { awk -F, -v r=2 'NR==r{print $5}' "$1"; }
ROWS_DIRECT=$(check_row tmp/tpch_q01_direct.csv); HASH_DIRECT=$(check_hash tmp/tpch_q01_direct.csv)
ROWS_CBI=$(check_row tmp/tpch_q01_cbi.csv);       HASH_CBI=$(check_hash tmp/tpch_q01_cbi.csv)
ROWS_FC=$(check_row tmp/tpch_q01_filecache.csv);  HASH_FC=$(check_hash tmp/tpch_q01_filecache.csv)
echo "rows: direct=$ROWS_DIRECT cbi=$ROWS_CBI fc=$ROWS_FC"
echo "hash: direct=$HASH_DIRECT cbi=$HASH_CBI fc=$HASH_FC"
if [ "$ROWS_DIRECT" = "$ROWS_CBI" ] && [ "$ROWS_DIRECT" = "$ROWS_FC" ] \
   && [ "$HASH_DIRECT" = "$HASH_CBI" ] && [ "$HASH_DIRECT" = "$HASH_FC" ] \
   && [ -n "$HASH_DIRECT" ]; then echo "PASS"; else echo "FAIL"; fi
```

Expected: PASS — identical row count AND identical result checksum across all
three modes for q01.

- [ ] **Step 5: Run all 22 queries correctness (rows + checksum)**

```bash
for Q in $(seq 1 22); do
  for MODE in direct filecache; do
    /root/oss/velox/_build/relwithdebinfo/velox/benchmarks/tpch/velox_tpch_benchmark \
      --input_source=$MODE \
      --data_path="${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}" \
      --data_format=parquet \
      --query_id=$Q \
      --rounds=1 \
      --num_splits_per_file=1 \
      --num_drivers=4 \
      --filecache_root=tmp/fc_tpch_correctness_$MODE \
      --filecache_disk_gib=10 \
      --cache_gb=4 \
      --out=tmp/tpch_correctness_q${Q}_$MODE.csv \
      > /root/oss/velox/_build/relwithdebinfo/test_018c_all_q${Q}_$MODE.log 2>&1
  done
  ROWS_D=$(awk -F, 'NR==2{print $4}' tmp/tpch_correctness_q${Q}_direct.csv)
  ROWS_F=$(awk -F, 'NR==2{print $4}' tmp/tpch_correctness_q${Q}_filecache.csv)
  HASH_D=$(awk -F, 'NR==2{print $5}' tmp/tpch_correctness_q${Q}_direct.csv)
  HASH_F=$(awk -F, 'NR==2{print $5}' tmp/tpch_correctness_q${Q}_filecache.csv)
  if [ "$ROWS_D" != "$ROWS_F" ] || [ "$HASH_D" != "$HASH_F" ] || [ -z "$HASH_D" ]; then
    echo "FAIL q$Q: rows direct=$ROWS_D fc=$ROWS_F | hash direct=$HASH_D fc=$HASH_F"
    exit 1
  fi
  echo "q$Q PASS ($ROWS_D rows, hash=$HASH_D)"
done
```

**Gate:** `rows` AND `result_hash` identical across direct and filecache for all 22 queries (checksum comparison, not row count alone).

---

## Task 018-D: Safe Orchestration Scripts

**Files:**
- Create: `velox/benchmarks/scripts/lib_cache_cleanup.sh`
- Create: `velox/benchmarks/scripts/run_tpch_ab.sh`
- Create: `velox/benchmarks/scripts/run_wrapper_ab.sh`

**Interfaces:**
- Consumes: `velox_tpch_benchmark`, `velox_bufferedinput_wrapper_benchmark` binaries
- Produces: Shell scripts that handle cache lifecycle safely

- [ ] **Step 1: Create `lib_cache_cleanup.sh`**

```bash
#!/usr/bin/env bash
# Sourced by benchmark scripts. Provides sentinel-based safe cache cleanup.
set -euo pipefail

SENTINEL_NAME=".velox_benchmark_cache_sentinel"

# Registry of (dir, root) pairs to clean on exit; appended by setup_trap_cleanup.
# Declared before any function so the trap bodies never need `local`.
_CLEANUP_DIRS=()
_CLEANUP_ROOTS=()

validate_cache_dir() {
  local dir="$1"
  local root="$2"
  [[ "$dir" = /* ]] || { echo "ERROR: cache dir must be absolute: $dir" >&2; return 1; }
  local real_dir real_root
  real_dir="$(realpath -m "$dir")"
  real_root="$(realpath -m "$root")"
  [[ "$real_dir" == "$real_root"/* ]] || { echo "ERROR: $dir not child of $root" >&2; return 1; }
}

create_sentinel() {
  local dir="$1"
  mkdir -p "$dir"
  echo "velox-benchmark-$$-$(date +%s)" > "$dir/$SENTINEL_NAME"
}

safe_wipe_cache_dir() {
  local dir="$1"
  local root="$2"
  validate_cache_dir "$dir" "$root" || return 1
  [[ -f "$dir/$SENTINEL_NAME" ]] || { echo "ERROR: no sentinel in $dir" >&2; return 1; }
  rm -rf "$dir"
}

# Wipes every registered cache dir via safe_wipe_cache_dir. Runs in trap context
# (top level), so all `local`s live inside this function body, never in the trap.
_run_cleanup() {
  local i=0
  while [ "$i" -lt "${#_CLEANUP_DIRS[@]}" ]; do
    safe_wipe_cache_dir "${_CLEANUP_DIRS[$i]}" "${_CLEANUP_ROOTS[$i]}" 2>/dev/null || true
    i=$((i + 1))
  done
}

setup_trap_cleanup() {
  local dir="$1"
  local root="$2"
  # Register this (dir, root) pair. The EXIT/INT/TERM traps clean every
  # registered pair through safe_wipe_cache_dir (sentinel + child-of-root
  # checks), so nothing outside a sentinel-marked child of root is ever removed.
  _CLEANUP_DIRS+=("$dir")
  _CLEANUP_ROOTS+=("$root")
  # EXIT runs at top level, where `local` is a syntax error: capture the code
  # into a plain variable and re-exit with it to preserve the original status.
  trap '_rc=$?; _run_cleanup; exit "$_rc"' EXIT
  trap '_run_cleanup; exit 130' INT TERM
}
```

`_run_cleanup` is defined earlier in the same file (before `setup_trap_cleanup`)
so the trap bodies contain no `local` and stay valid bash.

- [ ] **Step 2: Create `run_tpch_ab.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_cache_cleanup.sh"

: "${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}"
: "${BIN:?set BIN to velox_tpch_benchmark path}"
: "${OUT_DIR:=tmp/tpch_ab_results}"
: "${FC_ROOT:=$(pwd)/tmp/fc_tpch}"
: "${FC_SIZE_GIB:=10}"
: "${CACHE_GB:=4}"
: "${ROUNDS:=3}"
: "${SPLITS:=1}"
: "${DRIVERS:=4}"

mkdir -p "$OUT_DIR"

for MODE in direct cbi filecache; do
  CACHE_DIR="$FC_ROOT/${MODE}_run"
  create_sentinel "$CACHE_DIR"
  setup_trap_cleanup "$CACHE_DIR" "$FC_ROOT"

  "$BIN" \
    --input_source="$MODE" \
    --data_path="$TPCH_DATA" \
    --data_format=parquet \
    --query_id=0 \
    --rounds="$ROUNDS" \
    --num_splits_per_file="$SPLITS" \
    --num_drivers="$DRIVERS" \
    --filecache_root="$CACHE_DIR" \
    --filecache_disk_gib="$FC_SIZE_GIB" \
    --cache_gb="$CACHE_GB" \
    --out="$OUT_DIR/tpch_${MODE}.csv"

  safe_wipe_cache_dir "$CACHE_DIR" "$FC_ROOT"
done
```

- [ ] **Step 3: Create `run_wrapper_ab.sh`**

Complete script (wrapper A/B matrix over dbi/cbi/fcbi, i.e. `direct`, `cbi`,
`filecache`). Only `filecache` owns a run-specific cache dir, so the sentinel
cleanup is armed only for that mode; `direct` needs no cache, `cbi` uses an
in-process AsyncDataCache sized by `--cache_gb`:

```bash
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_cache_cleanup.sh"

: "${BIN:?set BIN to velox_bufferedinput_wrapper_benchmark path}"
: "${OUT_DIR:=tmp/wrapper_ab_results}"
: "${FC_ROOT:=$(pwd)/tmp/fc_wrapper}"
: "${FC_SIZE_GIB:=10}"
: "${CACHE_GB:=8}"
: "${TARGET_WS_GB:=4}"
: "${READ_SIZES_KIB:=1024,8192}"
: "${WORKLOADS:=sequential,zipfian}"
: "${MEASURE_PASSES:=3}"
: "${ROUNDS:=3}"

mkdir -p "$OUT_DIR"

# Wrapper matrix: direct (dbi), cbi, filecache (fcbi).
for MODE in direct cbi filecache; do
  args=(
    --input_source="$MODE"
    --target_ws_gb="$TARGET_WS_GB"
    --read_sizes_kib="$READ_SIZES_KIB"
    --workloads="$WORKLOADS"
    --measure_passes="$MEASURE_PASSES"
    --rounds="$ROUNDS"
    --cold_each_round
    --out="$OUT_DIR/wrapper_${MODE}.csv"
  )
  case "$MODE" in
    cbi)
      args+=(--cache_gb="$CACHE_GB")
      ;;
    filecache)
      CACHE_DIR="$FC_ROOT/${MODE}_run"
      create_sentinel "$CACHE_DIR"
      setup_trap_cleanup "$CACHE_DIR" "$FC_ROOT"
      args+=(--filecache_root="$CACHE_DIR" --filecache_disk_gib="$FC_SIZE_GIB")
      ;;
  esac
  "$BIN" "${args[@]}"
  if [ "$MODE" = "filecache" ]; then
    safe_wipe_cache_dir "$FC_ROOT/${MODE}_run" "$FC_ROOT"
  fi
done
```

- [ ] **Step 4: Syntax-check scripts**

```bash
for f in velox/benchmarks/scripts/*.sh; do
  bash -n "$f" && echo "OK: $f" || echo "FAIL: $f"
done
```

Expected: no syntax errors.

- [ ] **Step 5: Functional test of sentinel logic**

```bash
source velox/benchmarks/scripts/lib_cache_cleanup.sh
TEST_ROOT="$(pwd)/tmp/test_cache_root"
TEST_DIR="$TEST_ROOT/run1"
mkdir -p "$TEST_ROOT"
create_sentinel "$TEST_DIR"
test -f "$TEST_DIR/$SENTINEL_NAME" && echo "SENTINEL_OK"
safe_wipe_cache_dir "$TEST_DIR" "$TEST_ROOT" && echo "WIPE_OK"
mkdir -p "$TEST_DIR"
safe_wipe_cache_dir "$TEST_DIR" "$TEST_ROOT" 2>&1 | grep -q "ERROR" && echo "SAFETY_OK"
rm -rf "$TEST_ROOT"
```

**Gate:** `bash -n` clean; SENTINEL_OK, WIPE_OK, SAFETY_OK all printed.

---

## Task 018-E: Gluten Lifecycle + Config (Fail-Close)

**Files (in `/root/oss/gluten-018`):**
- Modify: `cpp/velox/config/VeloxConfig.h` — add the five FileCache config keys
- Create: `cpp/velox/compute/FileCacheSupport.h` — testable `validateFileCacheConfig` + `buildFileCacheManager`
- Create: `cpp/velox/compute/FileCacheSupport.cc`
- Modify: `cpp/velox/CMakeLists.txt` — add `compute/FileCacheSupport.cc` to `VELOX_SRCS`
- Modify: `cpp/velox/compute/VeloxBackend.h` — add `fileCacheManager_` + `fileCacheTimekeeper_` fields and `initFileCache()` declaration
- Modify: `cpp/velox/compute/VeloxBackend.cc` — implement `initFileCache()`, add mutual-exclusion dispatch in `init()`, extend `tearDown()`
- Create: `cpp/velox/tests/FileCacheSupportTest.cc` — helper unit tests (no `VeloxBackend`, no global re-init)
- Create: `cpp/velox/tests/FileCacheGlutenLifecycleTest.cc` — one end-to-end lifecycle test through `VeloxBackend`
- Modify: `cpp/velox/tests/CMakeLists.txt` — register both test targets

**Interfaces:**
- Consumes: `FileCacheManager::create` (`:108`), `setInstance` (`:122`), `getInstance` (`:110`), `getDefault` (`:127`), `shutdown` (`:132`), `refreshStats` (`:134`); `FileCacheManager::Options` fields `localFileSystem`/`memoryPool`/`timekeeper` (`:102-104`); `VeloxMemoryManager::getAggregateMemoryPool` (`cpp/velox/memory/VeloxMemoryManager.h:70`); `AllocationListener::noop` (`cpp/core/memory/AllocationListener.h:28`); `filesystems::getFileSystem`; `folly::ThreadWheelTimekeeper`
- Produces: `gluten::validateFileCacheConfig`, `gluten::buildFileCacheManager`, `VeloxBackend::initFileCache`, the five config keys

**All five approved configuration keys (exact, `namespace gluten`, after `kVeloxSsdCheckSumReadVerificationEnabled` at `VeloxConfig.h:148`):**

```cpp
const std::string kVeloxFileCacheEnabled =
    "spark.gluten.sql.columnar.backend.velox.fileCacheEnabled";
const bool kVeloxFileCacheEnabledDefault = false;

const std::string kVeloxFileCachePath =
    "spark.gluten.sql.columnar.backend.velox.fileCachePath";

const std::string kVeloxFileCacheSize =
    "spark.gluten.sql.columnar.backend.velox.fileCacheSize";
const uint64_t kVeloxFileCacheSizeDefault = 10ULL << 30;

const std::string kVeloxFileCacheMaxSegmentSize =
    "spark.gluten.sql.columnar.backend.velox.fileCacheMaxSegmentSize";
const uint64_t kVeloxFileCacheMaxSegmentSizeDefault = 8ULL << 20;

const std::string kVeloxFileCacheBackgroundDownloadThreads =
    "spark.gluten.sql.columnar.backend.velox.fileCacheBackgroundDownloadThreads";
const uint64_t kVeloxFileCacheBackgroundDownloadThreadsDefault = 4;
```

**Testable helper (`cpp/velox/compute/FileCacheSupport.h`):**

Factored out of `VeloxBackend` so the config→manager path is unit-testable
without constructing a `VeloxBackend` (which would re-init glog + the global
Velox memory manager on every test — invalid global state). `VeloxBackend`
becomes a thin caller that supplies production values.

```cpp
#pragma once

#include <folly/futures/Timekeeper.h>

#include <memory>

namespace facebook::velox {
namespace config { class ConfigBase; }
namespace memory { class MemoryPool; }
namespace filesystems { class FileSystem; }
namespace ch { class FileCacheManager; }
} // namespace facebook::velox

namespace gluten {

/// Throws (`VELOX_USER_FAIL`) if the FileCache config is invalid: both
/// AsyncDataCache and FileCache enabled, or FileCache enabled with empty path.
/// No-op when FileCache is disabled. Called before either cache is allocated.
void validateFileCacheConfig(const facebook::velox::config::ConfigBase& conf);

/// Builds and initializes a `FileCacheManager` from `conf`, mapping all five
/// keys into `FileCacheConfig`/`Options`. Returns nullptr if FileCache is
/// disabled. Does NOT install the process singleton. The caller keeps
/// `memoryPool`/`localFileSystem`/`timekeeper` alive for the manager's lifetime.
std::shared_ptr<facebook::velox::ch::FileCacheManager> buildFileCacheManager(
    const facebook::velox::config::ConfigBase& conf,
    facebook::velox::memory::MemoryPool* memoryPool,
    std::shared_ptr<facebook::velox::filesystems::FileSystem> localFileSystem,
    std::shared_ptr<folly::Timekeeper> timekeeper);

} // namespace gluten
```

**`cpp/velox/compute/FileCacheSupport.cc`:**

```cpp
#include "compute/FileCacheSupport.h"

#include "config/VeloxConfig.h"

#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/common/config/Config.h"
#include "velox/common/memory/MemoryPool.h"

#include <filesystem>

namespace gluten {

void validateFileCacheConfig(const facebook::velox::config::ConfigBase& conf) {
  if (!conf.get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    return;
  }
  // Mutual exclusion: reject both caches enabled BEFORE either is allocated.
  VELOX_USER_CHECK(
      !conf.get<bool>(kVeloxCacheEnabled, false),
      "Cannot enable both AsyncDataCache ({}) and FileCache ({}) simultaneously",
      kVeloxCacheEnabled,
      kVeloxFileCacheEnabled);
  const auto path = conf.get<std::string>(kVeloxFileCachePath, "");
  VELOX_USER_CHECK(
      !path.empty(), "{} must be set when FileCache is enabled", kVeloxFileCachePath);
}

std::shared_ptr<facebook::velox::ch::FileCacheManager> buildFileCacheManager(
    const facebook::velox::config::ConfigBase& conf,
    facebook::velox::memory::MemoryPool* memoryPool,
    std::shared_ptr<facebook::velox::filesystems::FileSystem> localFileSystem,
    std::shared_ptr<folly::Timekeeper> timekeeper) {
  validateFileCacheConfig(conf);
  if (!conf.get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    return nullptr;
  }
  VELOX_CHECK_NOT_NULL(memoryPool);
  VELOX_CHECK(
      memoryPool->kind() == facebook::velox::memory::MemoryPool::Kind::kLeaf,
      "FileCache requires a leaf memory pool for direct buffer allocations");

  // Stable, dedicated, absolute directory (no random cache.<uuid> suffix): the
  // FileCache reloads metadata across restarts.
  const auto cachePath =
      std::filesystem::absolute(conf.get<std::string>(kVeloxFileCachePath, "")).string();
  const auto cacheSize = conf.get<uint64_t>(kVeloxFileCacheSize, kVeloxFileCacheSizeDefault);
  const auto maxSegmentSize =
      conf.get<uint64_t>(kVeloxFileCacheMaxSegmentSize, kVeloxFileCacheMaxSegmentSizeDefault);
  const auto bgThreads = conf.get<uint64_t>(
      kVeloxFileCacheBackgroundDownloadThreads, kVeloxFileCacheBackgroundDownloadThreadsDefault);

  facebook::velox::ch::FileCacheConfig cfg;
  cfg.path = cachePath;
  cfg.maxSize = cacheSize; // logical upper bound; no all-free-space precheck.
  cfg.maxFileSegmentSize = maxSegmentSize;
  cfg.backgroundDownloadThreads = bgThreads; // maps the 5th key into FileCacheConfig.

  facebook::velox::ch::FileCacheManager::Options opts;
  opts.caches = {{.name = "default", .config = cfg, .configPath = cachePath}};
  opts.defaultCacheName = "default";
  opts.commonUserId = "gluten";
  opts.cachePathPrefix = cachePath;
  opts.allowedCacheRoot = cachePath;
  opts.localFileSystem = std::move(localFileSystem); // validateOptions: non-null.
  opts.memoryPool = memoryPool;                      // validateOptions: non-null.
  opts.timekeeper = std::move(timekeeper);           // validateOptions: non-null.
  opts.initializeOnCreate = true;
  return facebook::velox::ch::FileCacheManager::create(std::move(opts));
}

} // namespace gluten
```

**`VeloxBackend.h` additions:**

Forward-declare the manager and memory pool, and include the timekeeper header
(the fields are `shared_ptr`, so forward declarations suffice even with the
inline destructor).
Near the top:
```cpp
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace facebook::velox::ch {
class FileCacheManager;
}

namespace facebook::velox::memory {
class MemoryPool;
}
```
In the private data section, after
`std::shared_ptr<facebook::velox::config::ConfigBase> backendConf_;`:
```cpp
  std::shared_ptr<facebook::velox::ch::FileCacheManager> fileCacheManager_;
  // Dedicated leaf pool: FileCache performs direct allocations and Velox
  // rejects allocations on aggregate pools.
  std::shared_ptr<facebook::velox::memory::MemoryPool> fileCacheMemoryPool_;
  // Owned production timekeeper (real HHWheelTimer thread) the manager co-owns.
  std::shared_ptr<folly::ThreadWheelTimekeeper> fileCacheTimekeeper_;
```
In the private methods, after `void initCache();`:
```cpp
  void initFileCache();
```

**`initFileCache()` (`VeloxBackend.cc`), supplying exact production values:**

```cpp
#include "compute/FileCacheSupport.h"

#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Common/FileCacheStats.h"
#include "velox/common/file/FileSystems.h"

#include <folly/futures/ThreadWheelTimekeeper.h>

void VeloxBackend::initFileCache() {
  if (!backendConf_->get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    return;
  }
  const auto cachePath = std::filesystem::absolute(
      backendConf_->get<std::string>(kVeloxFileCachePath, "")).string();

  // Registered local filesystem for the cache path (registerLocalFileSystem()
  // already ran earlier in init()).
  auto localFs = facebook::velox::filesystems::getFileSystem(cachePath, nullptr);
  // Owned production timekeeper.
  fileCacheTimekeeper_ = std::make_shared<folly::ThreadWheelTimekeeper>();
  fileCacheMemoryPool_ =
      globalMemoryManager_->getAggregateMemoryPool()->addLeafChild("filecache");

  fileCacheManager_ = buildFileCacheManager(
      *backendConf_,
      fileCacheMemoryPool_.get(),
      localFs,
      fileCacheTimekeeper_);
  facebook::velox::ch::FileCacheManager::setInstance(fileCacheManager_.get());
  LOG(INFO) << "FileCache is ready at " << cachePath;
}
```

**Mutual-exclusion dispatch in `init()` (item: fail BEFORE `initCache()`):**

Replace the single `initCache();` call at `VeloxBackend.cc:295` with a validated
one-path dispatch. Validation runs before either cache is allocated, so a
misconfiguration fails fast with no partial startup state, and exactly one cache
path is initialized:

```cpp
  // Validate FileCache/AsyncDataCache mutual exclusion BEFORE allocating either
  // cache. On conflict this throws and no AsyncDataCache is ever constructed.
  validateFileCacheConfig(*backendConf_);
  if (backendConf_->get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    initFileCache(); // exactly the FileCache path
  } else {
    initCache();     // existing AsyncDataCache path (no-op when disabled)
  }
```

**Teardown (`VeloxBackend::tearDown`), strict order, before `globalMemoryManager_.reset()`:**

Insert at the very start of `tearDown()` (before `executor_.reset()` and the
existing `globalMemoryManager_.reset()` at `VeloxBackend.cc:450`):

```cpp
  // FileCache shutdown BEFORE executors, AsyncDataCache, and the global memory
  // manager: the manager holds a raw pointer to fileCacheMemoryPool_ and co-owns
  // fileCacheTimekeeper_. Documented strict order is
  //   shutdown -> setInstance(nullptr) -> drop the owning shared_ptr.
  if (fileCacheManager_) {
    const auto snapshot = facebook::velox::ch::takeFileCacheStatsSnapshot();
    LOG(INFO) << "FileCache teardown: cacheSize=" << snapshot.cacheSize
              << " cacheReadBytes=" << snapshot.cacheReadBytes
              << " sourceReadBytes=" << snapshot.sourceReadBytes
              << " cacheWriteBytes=" << snapshot.cacheWriteBytes
              << " hitCount=" << snapshot.cacheHitCount
              << " missCount=" << snapshot.cacheMissCount;
    fileCacheManager_->shutdown();
    facebook::velox::ch::FileCacheManager::setInstance(nullptr);
    fileCacheManager_.reset();
    // The manager's raw pool pointer is gone, so the dedicated leaf can now be
    // released before its aggregate parent/global memory manager.
    fileCacheMemoryPool_.reset();
    fileCacheTimekeeper_.reset();
  }
```

The existing teardown body (executors, AsyncDataCache dump, `globalMemoryManager_.reset()`) is unchanged and still runs after this block.

- [ ] **Step 1: Add the five config keys to `cpp/velox/config/VeloxConfig.h`**

Insert the exact block above after `kVeloxSsdCheckSumReadVerificationEnabled` (line 148), inside `namespace gluten`.

- [ ] **Step 2: Create `cpp/velox/compute/FileCacheSupport.h` and `.cc`**

Create both files with the exact contents shown above, then add
`compute/FileCacheSupport.cc` to `VELOX_SRCS` in `cpp/velox/CMakeLists.txt`
(after `compute/VeloxBackend.cc`, line 159).

- [ ] **Step 3: Add fields + `initFileCache()` declaration + includes to `cpp/velox/compute/VeloxBackend.h`**

- [ ] **Step 4: Implement `initFileCache()` in `cpp/velox/compute/VeloxBackend.cc`** (exact code above)

- [ ] **Step 5: Replace the `initCache();` call at `VeloxBackend.cc:295` with the validated one-path dispatch** (exact code above)

- [ ] **Step 6: Extend `VeloxBackend::tearDown()` with the FileCache shutdown block** (exact code above, at the start of `tearDown`)

- [ ] **Step 7: Create `cpp/velox/tests/FileCacheSupportTest.cc` (helper unit tests, no `VeloxBackend`)**

These tests exercise the factored helpers directly with a test-constructed
pool/filesystem/timekeeper, so they never re-init glog or the global memory
manager. `deprecatedAddDefaultLeafMemoryPool` lazily initializes the default
memory manager (same approach as `FileCacheSeekBenchmark.cpp`).

```cpp
#include <gtest/gtest.h>

#include <filesystem>

#include "compute/FileCacheSupport.h"
#include "config/VeloxConfig.h"

#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/common/config/Config.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/memory/Memory.h"

#include <folly/futures/ThreadWheelTimekeeper.h>

namespace fs = std::filesystem;
using facebook::velox::config::ConfigBase;

class FileCacheSupportTest : public ::testing::Test {
 protected:
  std::string testDir_;
  std::shared_ptr<facebook::velox::memory::MemoryPool> pool_;
  std::shared_ptr<facebook::velox::filesystems::FileSystem> fs_;
  std::shared_ptr<folly::ThreadWheelTimekeeper> tk_;

  void SetUp() override {
    facebook::velox::filesystems::registerLocalFileSystem();
    testDir_ = fs::absolute("tmp/fc_support_test").string();
    fs::create_directories(testDir_);
    pool_ = facebook::velox::memory::deprecatedAddDefaultLeafMemoryPool("fc_support_test");
    fs_ = facebook::velox::filesystems::getFileSystem(testDir_, nullptr);
    tk_ = std::make_shared<folly::ThreadWheelTimekeeper>();
  }

  void TearDown() override {
    facebook::velox::ch::FileCacheManager::setInstance(nullptr);
    fs::remove_all(testDir_);
  }

  std::shared_ptr<ConfigBase> conf(
      bool fc, bool adc, const std::string& path, uint64_t bgThreads = 4) {
    return std::make_shared<ConfigBase>(std::unordered_map<std::string, std::string>{
        {gluten::kVeloxFileCacheEnabled, fc ? "true" : "false"},
        {gluten::kVeloxCacheEnabled, adc ? "true" : "false"},
        {gluten::kVeloxFileCachePath, path},
        {gluten::kVeloxFileCacheSize, std::to_string(64ULL << 20)},
        {gluten::kVeloxFileCacheBackgroundDownloadThreads, std::to_string(bgThreads)}});
  }
};

TEST_F(FileCacheSupportTest, DisabledReturnsNull) {
  auto c = conf(false, false, testDir_);
  EXPECT_EQ(gluten::buildFileCacheManager(*c, pool_.get(), fs_, tk_), nullptr);
}

TEST_F(FileCacheSupportTest, MissingPathThrows) {
  auto c = conf(true, false, "");
  EXPECT_ANY_THROW(gluten::validateFileCacheConfig(*c));
  EXPECT_ANY_THROW(gluten::buildFileCacheManager(*c, pool_.get(), fs_, tk_));
}

TEST_F(FileCacheSupportTest, BothCachesEnabledThrows) {
  auto c = conf(true, true, testDir_);
  EXPECT_ANY_THROW(gluten::validateFileCacheConfig(*c));
}

TEST_F(FileCacheSupportTest, ValidConfigBuildsManager) {
  auto c = conf(true, false, testDir_ + "/valid");
  ASSERT_EQ(
      pool_->kind(),
      facebook::velox::memory::MemoryPool::Kind::kLeaf);
  auto mgr = gluten::buildFileCacheManager(*c, pool_.get(), fs_, tk_);
  ASSERT_NE(mgr, nullptr);
  ASSERT_NE(mgr->getDefault(), nullptr);
  mgr->shutdown();
}

TEST_F(FileCacheSupportTest, AggregatePoolIsRejectedBeforeManagerCreation) {
  auto c = conf(true, false, testDir_ + "/aggregate");
  ASSERT_NE(pool_->parent(), nullptr);
  ASSERT_NE(
      pool_->parent()->kind(),
      facebook::velox::memory::MemoryPool::Kind::kLeaf);
  EXPECT_ANY_THROW(
      gluten::buildFileCacheManager(*c, pool_->parent(), fs_, tk_));
}

TEST_F(FileCacheSupportTest, BackgroundThreadsMappedToConfig) {
  // backgroundDownloadThreads feeds FileCacheFactory::computeCacheWorkerMax
  // (velox/ch/Interpreters/FileCache/FileCacheFactory.cpp:411), so it shows up
  // in refreshStats().workerPoolMax. Two builds differing only in the thread
  // count must differ in workerPoolMax by exactly that delta.
  auto c2 = conf(true, false, testDir_ + "/two", /*bgThreads=*/2);
  auto c8 = conf(true, false, testDir_ + "/eight", /*bgThreads=*/8);
  auto m2 = gluten::buildFileCacheManager(*c2, pool_.get(), fs_, tk_);
  auto m8 = gluten::buildFileCacheManager(*c8, pool_.get(), fs_, tk_);
  ASSERT_NE(m2, nullptr);
  ASSERT_NE(m8, nullptr);
  EXPECT_EQ(m8->refreshStats().workerPoolMax - m2->refreshStats().workerPoolMax, 6u);
  m8->shutdown();
  m2->shutdown();
}
```

- [ ] **Step 8: Create `cpp/velox/tests/FileCacheGlutenLifecycleTest.cc` (one end-to-end lifecycle test)**

Exactly one `VeloxBackend::create` call (in the single test), so glog and the
global memory manager are initialized once. Uses `AllocationListener::noop()`,
following the live pattern at `cpp/velox/tests/BufferOutputStreamTest.cc:32` and
`cpp/velox/tests/MemoryManagerTest.cc:53`.

```cpp
#include <gtest/gtest.h>

#include <filesystem>

#include "compute/VeloxBackend.h"
#include "config/VeloxConfig.h"

#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"

namespace fs = std::filesystem;

// Single test: create the backend once with FileCache enabled, assert the
// manager is installed with a default cache, tear down, assert it is withdrawn.
TEST(FileCacheGlutenLifecycleTest, InstallAndTeardownThroughBackend) {
  const std::string dir = fs::absolute("tmp/fc_lifecycle_test").string();
  fs::create_directories(dir);

  std::unordered_map<std::string, std::string> conf{
      {gluten::kVeloxFileCacheEnabled, "true"},
      {gluten::kVeloxFileCachePath, dir},
      {gluten::kVeloxFileCacheSize, std::to_string(64ULL << 20)}};

  gluten::VeloxBackend::create(gluten::AllocationListener::noop(), conf);
  EXPECT_NE(facebook::velox::ch::FileCacheManager::getInstance(), nullptr);
  EXPECT_NE(facebook::velox::ch::FileCacheManager::getInstance()->getDefault(), nullptr);

  gluten::VeloxBackend::get()->tearDown();
  EXPECT_EQ(facebook::velox::ch::FileCacheManager::getInstance(), nullptr);

  fs::remove_all(dir);
}
```

- [ ] **Step 9: Register both tests in `cpp/velox/tests/CMakeLists.txt`**

```cmake
add_velox_test(velox_file_cache_support_test SOURCES FileCacheSupportTest.cc)
add_velox_test(velox_file_cache_gluten_lifecycle_test SOURCES FileCacheGlutenLifecycleTest.cc)
```

- [ ] **Step 10: Build and run both test targets**

```bash
cd /root/oss/gluten-018
ninja -C cpp/build velox_file_cache_support_test velox_file_cache_gluten_lifecycle_test \
  > cpp/build/build_018e.log 2>&1
ctest --test-dir cpp/build \
  -R '^velox_file_cache_(support|gluten_lifecycle)_test$' --output-on-failure \
  > cpp/build/test_018e.log 2>&1
echo "exit: $?"
```

A subagent analyzes `build_018e.log`/`test_018e.log` and returns a concise summary.

- [ ] **Step 11: Mutation — remove the mutual-exclusion check**

**File:** `cpp/velox/compute/FileCacheSupport.cc`
**Function:** `gluten::validateFileCacheConfig`

Comment out the `VELOX_USER_CHECK(!conf.get<bool>(kVeloxCacheEnabled, false), ...)`
mutual-exclusion check (2 lines).

Re-run: `ctest --test-dir cpp/build -R '^velox_file_cache_support_test$' --output-on-failure`

**Expected failed assertion:** `FileCacheSupportTest.BothCachesEnabledThrows`'s
`EXPECT_ANY_THROW(gluten::validateFileCacheConfig(*c))` sees no throw — test
reports FAILED. Restore after confirming RED.

- [ ] **Step 12: Mutation — drop the background-threads mapping**

**File:** `cpp/velox/compute/FileCacheSupport.cc`
**Function:** `gluten::buildFileCacheManager`

Comment out `cfg.backgroundDownloadThreads = bgThreads;`.

Re-run: `ctest --test-dir cpp/build -R '^velox_file_cache_support_test$' --output-on-failure`

**Expected failed assertion:** `FileCacheSupportTest.BackgroundThreadsMappedToConfig`'s
`EXPECT_EQ(delta, 6u)` fails (both builds now report the default thread count).
Restore after confirming RED.

**Gate:** Both test targets pass; the two mutations prove mutual-exclusion and
the 5th-key mapping are covered. The `MissingPathThrows` and
`ValidConfigBuildsManager` cases cover path validation and successful
construction; the lifecycle test covers end-to-end install/teardown ordering.

---

## Task 018-F: Gluten Builder Adapter

**Files (in `/root/oss/gluten-018`):**
- Modify: `cpp/velox/memory/GlutenBufferedInputBuilder.h` — add FileCache selection logic
- Create: `cpp/velox/tests/FileCacheGlutenBuilderTest.cc`
- Modify: `cpp/velox/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FileCacheManager::getInstance` (`:110`), `FileCacheManager::getDefault` (`:127`), `FileCacheManager::commonUserId` (`:128`), `FileCacheFileIdentity::deriveKey` (`FileCacheFileIdentity.h:44`), `ReaderOptions::cacheable` (`Options.h:857`), `FileCacheRequestContext::segmentType`/`FileCacheOriginInfo::segment_type`, `FileCacheBufferedInput` constructor + `cancellationToken()` accessor (Task 017A version with token), `ConnectorQueryCtx::cancellationToken` (`:558`)
- Produces: Builder that returns `FileCacheBufferedInput` when the manager is installed, preserving the CBI and direct paths unchanged

**Builder implementation (exact, based on live `GlutenBufferedInputBuilder.h:27-62`):**

The `create` method signature is:
```cpp
std::unique_ptr<facebook::velox::dwio::common::BufferedInput> create(
    const facebook::velox::FileHandle& fileHandle,
    const facebook::velox::dwio::common::ReaderOptions& readerOpts,
    const facebook::velox::connector::ConnectorQueryCtx* connectorQueryCtx,
    std::shared_ptr<facebook::velox::io::IoStatistics> ioStatistics,
    std::shared_ptr<facebook::velox::IoStats> ioStats,
    folly::Executor* executor,
    const folly::F14FastMap<std::string, std::string>& fileReadOps = {}) override
```

Add FileCache branch BEFORE the existing CBI check:

```cpp
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Disks/IO/FileCacheFileIdentity.h"
#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Disks/IO/FileCacheRequestContext.h"
#include "velox/ch/Interpreters/FileCache/FileCacheReadOptions.h"
#include "velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h"

// In GlutenBufferedInputBuilder::create(), as first branch:
auto* fcManager = facebook::velox::ch::FileCacheManager::getInstance();
if (fcManager != nullptr)
{
    auto defaultCache = fcManager->getDefault();
    VELOX_CHECK_NOT_NULL(defaultCache.get(), "FileCacheManager has no default cache");

    const std::string path = fileHandle.file->getName();
    auto cacheKey = facebook::velox::ch::FileCacheFileIdentity::deriveKey({path, ""});

    facebook::velox::ch::FileCacheRequestContext reqCtx;
    reqCtx.queryId = connectorQueryCtx->queryId();
    reqCtx.userId = fcManager->commonUserId();
    reqCtx.cacheable = readerOpts.cacheable(); // ReaderOptions::cacheable (Options.h:857)
    // reqCtx.segmentType stays FileSegmentKeyType::Data (default).

    facebook::velox::ch::FileCacheReadOptions cacheOpts;
    cacheOpts.remoteFsBufferSize = readerOpts.loadQuantum();
    cacheOpts.localFsBufferSize = readerOpts.loadQuantum();

    // Origin: weight left as std::nullopt; segment type taken from the request.
    // (The 3-arg FileCacheOriginInfo ctor would force a concrete weight, so set
    // the field directly to keep weight == std::nullopt.)
    facebook::velox::ch::FileCacheOriginInfo origin(reqCtx.userId);
    origin.segment_type = reqCtx.segmentType;

    folly::CancellationToken token = connectorQueryCtx->cancellationToken();

    // Constructor order (Task 017A): readFile, cache, key, origin, cacheOptions,
    // requestContext, metricsLog, ioStatistics, ioStats, executor,
    // readerOptions, fileReadOps, cancellationToken
    return std::make_unique<facebook::velox::ch::FileCacheBufferedInput>(
        fileHandle.file,
        std::move(defaultCache),
        std::move(cacheKey),
        std::move(origin),
        std::move(cacheOpts),
        std::move(reqCtx),
        facebook::velox::dwio::common::MetricsLog::voidLog(),
        std::move(ioStatistics),
        std::move(ioStats),
        executor,
        readerOpts,
        fileReadOps,
        std::move(token));
}
// --- Existing CBI and direct paths below are the current live create() body,
// --- copied verbatim so the non-FileCache behavior is preserved exactly. ---
if (connectorQueryCtx->cache())
{
    return std::make_unique<facebook::velox::dwio::common::CachedBufferedInput>(
        fileHandle.file,
        dwio::common::MetricsLog::voidLog(),
        fileHandle.uuid,
        connectorQueryCtx->cache(),
        facebook::velox::connector::Connector::getTracker(connectorQueryCtx->scanId(), readerOpts.loadQuantum()),
        fileHandle.groupId,
        std::move(ioStatistics),
        std::move(ioStats),
        executor,
        readerOpts,
        fileReadOps);
}
return std::make_unique<GlutenDirectBufferedInput>(
    fileHandle.file,
    dwio::common::MetricsLog::voidLog(),
    fileHandle.uuid,
    facebook::velox::connector::Connector::getTracker(connectorQueryCtx->scanId(), readerOpts.loadQuantum()),
    fileHandle.groupId,
    std::move(ioStatistics),
    std::move(ioStats),
    executor,
    readerOpts,
    fileReadOps);
```

`FileCacheManager::commonUserId()` is a public accessor
(`velox/ch/Interpreters/FileCache/FileCacheManager.h:128`), returning the
`commonUserId` supplied to `Options`.

- [ ] **Step 1: Add FileCache selection logic to `cpp/velox/memory/GlutenBufferedInputBuilder.h`**
- [ ] **Step 2: Create `cpp/velox/tests/FileCacheGlutenBuilderTest.cc`**

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "memory/GlutenBufferedInputBuilder.h"

#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/common/caching/FileHandle.h"
#include "velox/common/caching/FileIds.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/file/LocalFile.h"
#include "velox/common/io/IoStatistics.h"
#include "velox/common/memory/Memory.h"
#include "velox/connectors/Connector.h"

#include <folly/CancellationToken.h>
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace fs = std::filesystem;
using namespace facebook::velox;

class FileCacheGlutenBuilderTest : public ::testing::Test {
 protected:
  std::string testDir_;
  std::string testFile_;
  std::string cacheDir_;
  std::shared_ptr<memory::MemoryPool> pool_;
  std::shared_ptr<filesystems::FileSystem> fs_;
  std::shared_ptr<folly::ThreadWheelTimekeeper> tk_;
  std::shared_ptr<config::ConfigBase> sessionProps_;
  std::shared_ptr<ch::FileCacheManager> fcManager_;

  void SetUp() override {
    filesystems::registerLocalFileSystem();
    testDir_ = fs::absolute("tmp/fc_builder_test").string();
    cacheDir_ = testDir_ + "/cache";
    testFile_ = testDir_ + "/data.bin";
    fs::create_directories(testDir_);
    {
      std::ofstream ofs(testFile_, std::ios::binary);
      const std::string data(4096, 'X');
      ofs.write(data.data(), data.size());
    }
    pool_ = memory::deprecatedAddDefaultLeafMemoryPool("fc_builder_test");
    fs_ = filesystems::getFileSystem(cacheDir_, nullptr);
    tk_ = std::make_shared<folly::ThreadWheelTimekeeper>();
    // Non-null session properties: ConnectorQueryCtx VELOX_CHECK_NOT_NULLs it.
    sessionProps_ =
        std::make_shared<config::ConfigBase>(std::unordered_map<std::string, std::string>{});
  }

  void TearDown() override {
    ch::FileCacheManager::setInstance(nullptr);
    fcManager_.reset();
    fs::remove_all(testDir_);
  }

  void installFileCache() {
    ch::FileCacheConfig cfg;
    cfg.path = cacheDir_;
    cfg.maxSize = 64ULL << 20;
    ch::FileCacheManager::Options opts;
    opts.caches = {{.name = "default", .config = cfg, .configPath = cacheDir_}};
    opts.defaultCacheName = "default";
    opts.commonUserId = "test";
    opts.cachePathPrefix = cacheDir_;
    opts.allowedCacheRoot = cacheDir_;
    opts.localFileSystem = fs_;
    opts.memoryPool = pool_.get();
    opts.timekeeper = tk_;
    opts.initializeOnCreate = true;
    fcManager_ = ch::FileCacheManager::create(std::move(opts));
    ch::FileCacheManager::setInstance(fcManager_.get());
  }

  // Single shared helper: builds a real FileHandle + ConnectorQueryCtx (real
  // constructor order; cancellationToken is the 14th ctor arg) and runs the
  // builder. The returned input copies what it needs, so the local connCtx may
  // be destroyed on return.
  std::unique_ptr<dwio::common::BufferedInput> runBuilder(
      folly::CancellationToken token = {}) {
    gluten::GlutenBufferedInputBuilder builder;
    auto readFile = std::make_shared<facebook::velox::LocalReadFile>(testFile_);
    FileHandle fileHandle;
    fileHandle.file = readFile;
    fileHandle.uuid = StringIdLease(fileIds(), testFile_);
    fileHandle.groupId = StringIdLease(fileIds(), testFile_);
    dwio::common::ReaderOptions readerOpts(pool_.get());
    auto ioStats = std::make_shared<io::IoStatistics>();
    auto ioStatsObj = std::make_shared<velox::IoStats>();
    connector::ConnectorQueryCtx connCtx(
        pool_.get(),                // operatorPool
        pool_.get(),                // connectorPool
        sessionProps_.get(),        // sessionProperties (non-null)
        nullptr,                    // spillConfig
        common::PrefixSortConfig{}, // prefixSortConfig
        nullptr,                    // expressionEvaluator
        nullptr,                    // cache (no AsyncDataCache)
        "test-query",               // queryId
        "task-0",                   // taskId
        "plan-0",                   // planNodeId
        0,                          // driverId
        "UTC",                      // sessionTimezone
        /*adjustTimestampToTimezone=*/false,
        token);                     // cancellationToken (14th param)
    return builder.create(fileHandle, readerOpts, &connCtx, ioStats, ioStatsObj, nullptr);
  }
};

TEST_F(FileCacheGlutenBuilderTest, ReturnsFileCacheBufferedInputWhenManagerInstalled) {
  installFileCache();
  auto result = runBuilder();
  auto* fcbi = dynamic_cast<ch::FileCacheBufferedInput*>(result.get());
  ASSERT_NE(fcbi, nullptr);
}

TEST_F(FileCacheGlutenBuilderTest, ReturnsDirectWhenNoCache) {
  // No FileCacheManager installed and no AsyncDataCache: expect the direct path.
  auto result = runBuilder();
  ASSERT_NE(result.get(), nullptr);
  EXPECT_EQ(dynamic_cast<ch::FileCacheBufferedInput*>(result.get()), nullptr);
}

TEST_F(FileCacheGlutenBuilderTest, CopiedCancellationTokenReachesInput) {
  installFileCache();
  folly::CancellationSource src;
  auto result = runBuilder(src.getToken());
  auto* fcbi = dynamic_cast<ch::FileCacheBufferedInput*>(result.get());
  ASSERT_NE(fcbi, nullptr);
  // The token is stored by value; cancelling the source is observed through the
  // FCBI accessor (FileCacheBufferedInput::cancellationToken(), Task 017A).
  EXPECT_FALSE(fcbi->cancellationToken().isCancellationRequested());
  src.requestCancellation();
  EXPECT_TRUE(fcbi->cancellationToken().isCancellationRequested());
}
```

- [ ] **Step 3: Register test target in CMakeLists.txt**

```cmake
add_velox_test(velox_file_cache_gluten_builder_test SOURCES FileCacheGlutenBuilderTest.cc)
```

- [ ] **Step 4: Build and run**

```bash
cd /root/oss/gluten-018
ninja -C cpp/build velox_file_cache_gluten_builder_test \
  > cpp/build/build_018f.log 2>&1
ctest --test-dir cpp/build -R '^velox_file_cache_gluten_builder_test$' \
  --output-on-failure > cpp/build/test_018f.log 2>&1
echo "exit: $?"
```

- [ ] **Step 5: Mutation — remove FileCache branch**

**File:** `cpp/velox/memory/GlutenBufferedInputBuilder.h`
**Function:** `GlutenBufferedInputBuilder::create`

Comment out the entire `if (fcManager != nullptr)` block.

Re-run: `ctest --test-dir cpp/build -R '^velox_file_cache_gluten_builder_test$' --output-on-failure`

**Expected failed assertions:** `ReturnsFileCacheBufferedInputWhenManagerInstalled` and `CopiedCancellationTokenReachesInput` both `dynamic_cast` to `ch::FileCacheBufferedInput*` and get `nullptr` — their `ASSERT_NE(fcbi, nullptr)` report FAILED.

Restore after confirming RED.

**Gate:** All three builder tests pass (FCBI selection, direct fallback, and
copied-cancellation-token observed via `fcbi->cancellationToken()`); the
mutation proves the FileCache branch is covered.

---

## Task 018-G: Complete Gluten Metric Bridge

**Goal:** Wire `fileCacheWriteBytes` end-to-end from C++ `IoStats` through JNI to Spark `SQLMetric`.

**Metric propagation path (traced from live code):**

```text
FileCacheInputStream::read
  -> ioStats_->addCounter("fileCacheWriteBytes", RuntimeCounter(bytesWritten, RuntimeCounter::Unit::kBytes))
     [velox/common/file/File.h:57, Task 017A code]

FileDataSource collects RuntimeMetrics from IoStats into operator customStats
  -> OperatorStats.customStats["fileCacheWriteBytes"]

exec::toPlanStats aggregates per-node
  -> PlanNodeStats.operatorStats[i].customStats["fileCacheWriteBytes"]

WholeStageResultIterator::collectMetrics  [cpp/velox/compute/WholeStageResultIterator.cc, collectMetrics()]
  -> metrics_->get(Metrics::kFileCacheWriteBytes)[metricIndex] =
       gluten::sumRuntimeMetric(second->customStats, kFileCacheWriteBytes);
     [extracted testable helper, cpp/velox/compute/RuntimeMetricUtil.h]

JniWrapper.cc NewObject call  [cpp/core/jni/JniWrapper.cc:637-685]
  -> longArray[Metrics::kFileCacheWriteBytes],
  (after kLoadLazyVectorTime, before the taskStats string)

Java Metrics.java constructor  [backends-velox/src/main/java/org/apache/gluten/metrics/Metrics.java:120]
  -> receives long[] fileCacheWriteBytes parameter (after loadLazyVectorTime, before taskStats)
  -> stores in public long[] fileCacheWriteBytes field

Java Metrics.getOperatorMetrics  [backends-velox/src/main/java/org/apache/gluten/metrics/Metrics.java:219]
  -> passes fileCacheWriteBytes[index] to OperatorMetrics constructor (after loadLazyVectorTime[index])

Java OperatorMetrics.java  [backends-velox/src/main/java/org/apache/gluten/metrics/OperatorMetrics.java:112]
  -> constructor receives long fileCacheWriteBytes (after long loadLazyVectorTime)
  -> stores in public long fileCacheWriteBytes field

Scala MetricsUtil.mergeMetrics  [backends-velox/src/main/scala/org/apache/gluten/metrics/MetricsUtil.scala:185]
  -> a SECOND `new OperatorMetrics(...)` call site: accumulate fileCacheWriteBytes
     across the merged suites and pass it as the last constructor argument
     (mandatory — otherwise backends-velox fails to compile once OperatorMetrics
     gains the parameter)

Scala FileSourceScanMetricsUpdater.updateNativeMetrics  [backends-velox/src/main/scala/org/apache/gluten/metrics/FileSourceScanMetricsUpdater.scala:92]
  -> ScanMetricsUtil.inc(fileCacheWriteBytes, operatorMetrics.fileCacheWriteBytes)

Scala VeloxMetricsApi.genFileSourceScanTransformerMetricsFull  [backends-velox/src/main/scala/org/apache/gluten/backendsapi/velox/VeloxMetricsApi.scala:266]
  -> "fileCacheWriteBytes" -> SQLMetrics.createSizeMetric(sparkContext, "file cache write bytes")
```

**Files (in `/root/oss/gluten-018`):**

| Layer | File | Change |
|---|---|---|
| C++ enum | `cpp/core/utils/Metrics.h` | Add `kFileCacheWriteBytes` before `kEnd` (after `kLoadLazyVectorTime`) |
| C++ helper | `cpp/velox/compute/RuntimeMetricUtil.h` / `.cc` (NEW) | Testable `gluten::sumRuntimeMetric(customStats, key)` used by `collectMetrics` |
| C++ constant | `cpp/velox/compute/WholeStageResultIterator.cc` | Add `const std::string kFileCacheWriteBytes = "fileCacheWriteBytes";` at file scope |
| C++ collect | `cpp/velox/compute/WholeStageResultIterator.cc` | Add propagation line in `collectMetrics` using `gluten::sumRuntimeMetric` |
| C++ build | `cpp/velox/CMakeLists.txt` | Add `compute/RuntimeMetricUtil.cc` to `VELOX_SRCS` |
| JNI call | `cpp/core/jni/JniWrapper.cc` | Add `longArray[Metrics::kFileCacheWriteBytes],` after `kLoadLazyVectorTime` line |
| JNI signature | `cpp/core/jni/JniWrapper.cc` | Update `"<init>"` signature (one more `[J` before `Ljava/lang/String;`) |
| Java Metrics | `backends-velox/src/main/java/org/apache/gluten/metrics/Metrics.java` | Add `public long[] fileCacheWriteBytes;` field, constructor param, assignment, getOperatorMetrics pass |
| Java OperatorMetrics | `backends-velox/src/main/java/org/apache/gluten/metrics/OperatorMetrics.java` | Add `public long fileCacheWriteBytes;` field, constructor param, assignment |
| Scala MetricsUtil | `backends-velox/src/main/scala/org/apache/gluten/metrics/MetricsUtil.scala` | `mergeMetrics`: accumulate `fileCacheWriteBytes`, pass to the 2nd `new OperatorMetrics` |
| Scala VeloxMetricsApi | `backends-velox/src/main/scala/org/apache/gluten/backendsapi/velox/VeloxMetricsApi.scala` | Add entry in `genFileSourceScanTransformerMetricsFull` Map |
| Scala FileSourceScanMetricsUpdater | `backends-velox/src/main/scala/org/apache/gluten/metrics/FileSourceScanMetricsUpdater.scala` | Add field + `ScanMetricsUtil.inc` call |
| C++ test | `cpp/velox/tests/FileCacheGlutenMetricsTest.cc` (NEW) | Native carrier gate: `sumRuntimeMetric` + enum order |
| Java test | `backends-velox/src/test/java/org/apache/gluten/metrics/MetricsCarrierTest.java` (NEW) | Java carrier gate: `getOperatorMetrics` carries the exact value |
| Scala test | `backends-velox/src/test/scala/org/apache/gluten/metrics/FileSourceScanMetricsUpdaterSuite.scala` (NEW) | Scala gate: updater increments the `SQLMetric` |

Every `new OperatorMetrics` / `new Metrics` call site in `backends-velox` was
searched; the two `new OperatorMetrics` sites are `Metrics.java:175`
(`getOperatorMetrics`) and `MetricsUtil.scala:185` (`mergeMetrics`) — both are
updated. (`TestSparkDataFile.java:286`'s `new Metrics(...)` is Iceberg's own
`org.apache.iceberg.Metrics`, unrelated.)

**Detailed changes:**

### 1. C++ `Metrics.h` — add enum entry (after `kLoadLazyVectorTime`, before `kEnd`)

```cpp
    kLoadLazyVectorTime,

    // FileCache metrics.
    kFileCacheWriteBytes,

    // The end of enum items.
    kEnd,
```

### 2. C++ `RuntimeMetricUtil` helper + `WholeStageResultIterator.cc` constant and propagation

`WholeStageResultIterator::collectMetrics` and `runtimeMetric` are private, so
the FileCache carrier is exercised through a small extracted free helper that
`collectMetrics` also uses. Create `cpp/velox/compute/RuntimeMetricUtil.h`:
```cpp
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace facebook::velox {
struct RuntimeMetric;
}

namespace gluten {

/// Sum aggregation of a named Velox runtime metric from operator customStats,
/// returning 0 when the key is absent. Extracted from
/// WholeStageResultIterator::collectMetrics so the FileCache byte-counter
/// propagation is unit-testable without constructing a Task/plan.
int64_t sumRuntimeMetric(
    const std::unordered_map<std::string, facebook::velox::RuntimeMetric>& customStats,
    const std::string& key);

} // namespace gluten
```
and `cpp/velox/compute/RuntimeMetricUtil.cc`:
```cpp
#include "compute/RuntimeMetricUtil.h"

#include "velox/common/base/RuntimeMetrics.h"

namespace gluten {

int64_t sumRuntimeMetric(
    const std::unordered_map<std::string, facebook::velox::RuntimeMetric>& customStats,
    const std::string& key) {
  const auto it = customStats.find(key);
  return it == customStats.end() ? 0 : it->second.sum;
}

} // namespace gluten
```
Add `compute/RuntimeMetricUtil.cc` to `VELOX_SRCS` in `cpp/velox/CMakeLists.txt`
(after `compute/WholeStageResultIterator.cc`, line 162).

In `WholeStageResultIterator.cc`, at file scope (alongside `kLocalReadBytes` at
line 67):
```cpp
const std::string kFileCacheWriteBytes = "fileCacheWriteBytes";
```
Add `#include "compute/RuntimeMetricUtil.h"` to the includes. In `collectMetrics`,
after the `kWriteIOTime` line (~line 592):
```cpp
      metrics_->get(Metrics::kFileCacheWriteBytes)[metricIndex] =
          gluten::sumRuntimeMetric(second->customStats, kFileCacheWriteBytes);
```

### 3. JNI `JniWrapper.cc` — add to NewObject call and update signature

After `longArray[Metrics::kLoadLazyVectorTime],` (line 684), add:
```cpp
      longArray[Metrics::kFileCacheWriteBytes],
```

Update the `"<init>"` signature string (line 316) from:
```
"([J[J[J[J[J[J[J[J[J[JJ[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[JLjava/lang/String;)V"
```
to (one additional `[J` inserted before `Ljava/lang/String;`, taking the array
count from 44 to 45; the single scalar `J` for `veloxToArrow` is unchanged):
```
"([J[J[J[J[J[J[J[J[J[JJ[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[JLjava/lang/String;)V"
```

Verify the new signature has 45 array parameters (must equal the number of
`long[]` args passed to `NewObject`):
`printf '%s' '<new signature>' | grep -o '\[J' | wc -l` → 45.

### 4. Java `Metrics.java` — field, constructor param, assignment, getOperatorMetrics

Field declaration (after `public long[] loadLazyVectorTime;`, before `public SingleMetric singleMetric`):
```java
  public long[] fileCacheWriteBytes;
```

Constructor parameter (after `long[] loadLazyVectorTime`, before `String taskStats`):
```java
      long[] loadLazyVectorTime,
      long[] fileCacheWriteBytes,
      String taskStats) {
```

Assignment in constructor body (after `this.loadLazyVectorTime = loadLazyVectorTime;`):
```java
    this.fileCacheWriteBytes = fileCacheWriteBytes;
```

In `getOperatorMetrics` method (after `loadLazyVectorTime[index]`):
```java
        loadLazyVectorTime[index],
        fileCacheWriteBytes[index]);
```

### 5. Java `OperatorMetrics.java` — field and constructor param

Field (after `public long loadLazyVectorTime;`):
```java
  public long fileCacheWriteBytes;
```

Constructor parameter (after `long loadLazyVectorTime`):
```java
      long loadLazyVectorTime,
      long fileCacheWriteBytes) {
```

Assignment (after `this.loadLazyVectorTime = loadLazyVectorTime;`):
```java
    this.fileCacheWriteBytes = fileCacheWriteBytes;
```

### 6. Scala `MetricsUtil.scala` — `mergeMetrics` aggregation (2nd `new OperatorMetrics`)

`mergeMetrics` (`MetricsUtil.scala:91-231`) is the SECOND `new OperatorMetrics`
call site. It MUST also pass `fileCacheWriteBytes` or `backends-velox` fails to
compile once `OperatorMetrics` gains the parameter.

Accumulator (after `var loadLazyVectorTime: Long = 0` at line 144):
```scala
    var fileCacheWriteBytes: Long = 0
```

Inside the `while (metricsIterator.hasNext)` loop (after
`loadLazyVectorTime += metrics.loadLazyVectorTime` at line 182):
```scala
      fileCacheWriteBytes += metrics.fileCacheWriteBytes
```

In the `new OperatorMetrics(...)` argument list (after `loadLazyVectorTime` at
line 229 — the last argument):
```scala
      loadLazyVectorTime,
      fileCacheWriteBytes
    )
```

### 7. Scala `VeloxMetricsApi.scala` — register SQLMetric

In `genFileSourceScanTransformerMetricsFull` Map (after the `"loadLazyVectorTime"` entry at approx line 265):
```scala
      "fileCacheWriteBytes" -> SQLMetrics.createSizeMetric(sparkContext, "file cache write bytes")
```

### 8. Scala `FileSourceScanMetricsUpdater.scala` — field + update

Field (after `private val loadLazyVectorTime`):
```scala
  private val fileCacheWriteBytes: Option[SQLMetric] = metric("fileCacheWriteBytes")
```

In `updateNativeMetrics`, after existing `ScanMetricsUtil.inc(loadLazyVectorTime, operatorMetrics.loadLazyVectorTime)`:
```scala
      ScanMetricsUtil.inc(fileCacheWriteBytes, operatorMetrics.fileCacheWriteBytes)
```

### Steps

- [ ] **Step 1: Add `kFileCacheWriteBytes` to C++ `Metrics.h` enum**
- [ ] **Step 2: Create `RuntimeMetricUtil.h`/`.cc`, register in `VELOX_SRCS`, add the constant + `collectMetrics` propagation in `WholeStageResultIterator.cc`**
- [ ] **Step 3: Update JNI NewObject call and signature in `JniWrapper.cc`**
- [ ] **Step 4: Add field/param to Java `Metrics.java` (field, constructor, assignment, getOperatorMetrics)**
- [ ] **Step 5: Add field/param to Java `OperatorMetrics.java`**
- [ ] **Step 6: Aggregate `fileCacheWriteBytes` in `MetricsUtil.scala` `mergeMetrics` (2nd `new OperatorMetrics`)**
- [ ] **Step 7: Add SQLMetric in `VeloxMetricsApi.scala`**
- [ ] **Step 8: Add field + update in `FileSourceScanMetricsUpdater.scala`**
- [ ] **Step 9: Create the native carrier test `cpp/velox/tests/FileCacheGlutenMetricsTest.cc`**

Exercises the extracted production helper `gluten::sumRuntimeMetric` (the exact
aggregation `collectMetrics` uses to move `fileCacheWriteBytes` out of
`customStats`) and the enum order — NOT merely `IoStats`:

```cpp
#include <gtest/gtest.h>

#include <unordered_map>

#include "compute/RuntimeMetricUtil.h"
#include "core/utils/Metrics.h"

#include "velox/common/base/RuntimeMetrics.h"

using namespace facebook::velox;

TEST(FileCacheGlutenMetricsTest, SumRuntimeMetricReadsFileCacheWriteBytes) {
  // customStats holds an aggregated (sum=12288, count=2) fileCacheWriteBytes.
  std::unordered_map<std::string, RuntimeMetric> customStats;
  customStats.emplace(
      "fileCacheWriteBytes",
      RuntimeMetric(
          /*sum=*/12288, /*count=*/2, /*min=*/4096, /*max=*/8192,
          RuntimeCounter::Unit::kBytes));
  EXPECT_EQ(gluten::sumRuntimeMetric(customStats, "fileCacheWriteBytes"), 12288);
  EXPECT_EQ(gluten::sumRuntimeMetric(customStats, "absent"), 0);
}

TEST(FileCacheGlutenMetricsTest, MetricsEnumOrderCorrect) {
  EXPECT_EQ(
      static_cast<int>(gluten::Metrics::kFileCacheWriteBytes),
      static_cast<int>(gluten::Metrics::kLoadLazyVectorTime) + 1);
  EXPECT_EQ(
      static_cast<int>(gluten::Metrics::kEnd),
      static_cast<int>(gluten::Metrics::kFileCacheWriteBytes) + 1);
}
```

Register and build/run:
```cmake
add_velox_test(velox_file_cache_gluten_metrics_test SOURCES FileCacheGlutenMetricsTest.cc)
```
```bash
cd /root/oss/gluten-018
ninja -C cpp/build velox_file_cache_gluten_metrics_test > cpp/build/build_018g.log 2>&1
ctest --test-dir cpp/build -R '^velox_file_cache_gluten_metrics_test$' \
  --output-on-failure > cpp/build/test_018g.log 2>&1
echo "exit: $?"
```

- [ ] **Step 10: Create the Java carrier test `backends-velox/src/test/java/org/apache/gluten/metrics/MetricsCarrierTest.java`**

Constructs `Metrics` arrays and proves `getOperatorMetrics` carries the exact
`fileCacheWriteBytes` value (this is also the target of the statistics mutation
in Step 13). Uses length-1 arrays; only `fileCacheWriteBytes` is non-zero:

```java
package org.apache.gluten.metrics;

import org.junit.Assert;
import org.junit.Test;

public class MetricsCarrierTest {
  private static long[] a(long v) {
    return new long[] {v};
  }

  @Test
  public void getOperatorMetricsCarriesFileCacheWriteBytes() {
    final long expected = 987654L;
    // Constructor order matches Metrics.java: 10 arrays, veloxToArrow (scalar),
    // then 34 arrays, then fileCacheWriteBytes, then taskStats.
    Metrics metrics =
        new Metrics(
            a(1), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0),
            0L,
            a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0),
            a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0),
            a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0),
            a(0), a(0), a(0),
            a(0),
            a(expected),
            "");
    OperatorMetrics op = metrics.getOperatorMetrics(0);
    Assert.assertEquals(expected, op.fileCacheWriteBytes);
  }
}
```

Run (surefire selects the JUnit test; scalatest is disabled with an empty
`wildcardSuites`; profiles follow `docs/developers/HowTo.md`):
```bash
cd /root/oss/gluten-018
mvn test -Pspark-3.5 -Pbackends-velox -pl backends-velox \
  -Dtest=MetricsCarrierTest -DwildcardSuites= -DfailIfNoTests=false -q \
  > cpp/build/test_018g_java.log 2>&1
echo "Java carrier test exit: $?"
```

- [ ] **Step 11: Create the Scala updater test `backends-velox/src/test/scala/org/apache/gluten/metrics/FileSourceScanMetricsUpdaterSuite.scala`**

Constructs an `OperatorMetrics`, invokes `FileSourceScanMetricsUpdater`, and
proves the `SQLMetric` value increments:

```scala
package org.apache.gluten.metrics

import org.apache.spark.sql.execution.metric.SQLMetric

import org.scalatest.funsuite.AnyFunSuite

class FileSourceScanMetricsUpdaterSuite extends AnyFunSuite {

  test("updateNativeMetrics increments the fileCacheWriteBytes SQLMetric") {
    val sqlMetric = new SQLMetric("size", 0L)
    val updater =
      new FileSourceScanMetricsUpdater(Map("fileCacheWriteBytes" -> sqlMetric))

    // OperatorMetrics: 44 zero longs then fileCacheWriteBytes = 12345.
    val op = new OperatorMetrics(
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0,
      12345L)

    updater.updateNativeMetrics(op)
    assert(sqlMetric.value == 12345L)
  }
}
```

Run (scalatest-maven-plugin selects the suite via `wildcardSuites`; surefire is
disabled with `-Dtest=none`):
```bash
cd /root/oss/gluten-018
mvn test -Pspark-3.5 -Pbackends-velox -pl backends-velox \
  -DwildcardSuites=org.apache.gluten.metrics.FileSourceScanMetricsUpdaterSuite \
  -Dtest=none -DfailIfNoTests=false -q \
  > cpp/build/test_018g_scala.log 2>&1
echo "Scala updater test exit: $?"
```

- [ ] **Step 12: JNI signature / full-bridge compile gate**

The native library must compile with the new JNI signature and the Java/Scala
bridge must compile with the new constructor arity:
```bash
cd /root/oss/gluten-018
ninja -C cpp/build gluten > cpp/build/build_018g_native.log 2>&1
echo "native build exit: $?"
mvn compile -pl backends-velox -am -DskipTests -q > cpp/build/build_018g_javac.log 2>&1
echo "Java/Scala compile exit: $?"
```
A subagent analyzes each log and returns a concise summary.

- [ ] **Step 13: Statistics mutation — drop the 018-owned Java carrier pass**

**File:** `backends-velox/src/main/java/org/apache/gluten/metrics/Metrics.java`
**Function:** `getOperatorMetrics`

This is the 018-owned statistics carrier. Change the `fileCacheWriteBytes[index]`
argument passed to `new OperatorMetrics(...)` to a wrong constant, e.g.:
```java
        loadLazyVectorTime[index],
        0L); // MUTATION: was fileCacheWriteBytes[index]
```

Re-run the Java carrier test:
```bash
mvn test -Pspark-3.5 -Pbackends-velox -pl backends-velox \
  -Dtest=MetricsCarrierTest -DwildcardSuites= -DfailIfNoTests=false -q
```

**Expected failed assertion:** `MetricsCarrierTest.getOperatorMetricsCarriesFileCacheWriteBytes`'s
`assertEquals(expected, op.fileCacheWriteBytes)` fails (`0 != 987654`). Restore
after confirming RED.

This mutation stays within Task-018-owned source. The Task-017A
`FileCacheInputStream.cpp` `ioStats_->addCounter` line is NOT mutated here (Task
018 owns no line there); the byte-level correctness mutation lives in 018-A
Step 8 (`CacheReadHarness.cpp`).

**Gate (all three carriers + compile gate required):**
- Native: `velox_file_cache_gluten_metrics_test` proves `sumRuntimeMetric` reads
  the correct key/sum and the enum order.
- Java: `MetricsCarrierTest` proves `getOperatorMetrics` carries the value; the
  Step 13 mutation confirms coverage.
- Scala: `FileSourceScanMetricsUpdaterSuite` proves the `SQLMetric` increments.
- Compile gate: native `gluten` + `mvn compile` succeed with the new JNI
  signature and constructor arity.

---

## Task 018-H: Performance Waves (Baseline, No Threshold)

**Files:**
- No new source files. Uses binaries from 018-A/B/C.
- Output: CSVs in `tmp/` directory and logs in build directory.

**Interfaces:**
- Consumes: `velox_ch_filecache_seek_benchmark`, `velox_ch_fcbi_benchmark`, `velox_bufferedinput_wrapper_benchmark`, `velox_tpch_benchmark`
- Produces: Baseline timing CSVs and folly benchmark tables. No performance threshold.

**Scheduling:** 018-H runs after 018-B and 018-C are green. Task 019 is excluded. Task 017B is not a dependency.

### Wave 1: Core seek microbenchmark (existing target, baseline)

```bash
mkdir -p tmp/fc_w1
/root/oss/velox/_build/relwithdebinfo/velox/ch/benchmarks/velox_ch_filecache_seek_benchmark \
  --bm_min_iters=5 \
  --file_size_mb=64 \
  --cache_dir=tmp/fc_w1 \
  --cache_size_mb=128 \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w1.log 2>&1
echo "Wave 1 exit: $?"
```

Output: `test_018h_w1.log` with folly benchmark table.

### Wave 2: Dedicated FCBI micro (NEW target, `velox_ch_fcbi_benchmark`)

```bash
mkdir -p tmp/fc_w2
/root/oss/velox/_build/relwithdebinfo/velox/ch/benchmarks/velox_ch_fcbi_benchmark \
  --bm_min_iters=10 \
  --file_size_mb=128 \
  --cache_dir=tmp/fc_w2 \
  --cache_size_mb=256 \
  --region_size_kib=1024 \
  --regions_per_iter=16 \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w2.log 2>&1
echo "Wave 2 exit: $?"
```

Output: `test_018h_w2.log` with folly benchmark table for FCBI enqueue+load+read.

### Wave 3: Wrapper A/B — full dbi/cbi/fcbi matrix (cold+hot, sequential+zipfian, 1M+8M)

Use the safe orchestration script from 018-D, which runs the full `direct`
(dbi) / `cbi` / `filecache` (fcbi) matrix and handles per-mode flags and
sentinel cache cleanup:

```bash
mkdir -p tmp/fc_w3
BIN=/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
OUT_DIR=tmp/fc_w3 \
FC_ROOT="$(pwd)/tmp/fc_w3_cache" \
FC_SIZE_GIB=10 CACHE_GB=8 TARGET_WS_GB=4 \
READ_SIZES_KIB=1024,8192 WORKLOADS=sequential,zipfian \
MEASURE_PASSES=3 ROUNDS=3 \
  bash velox/benchmarks/scripts/run_wrapper_ab.sh \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w3.log 2>&1
echo "Wave 3 exit: $?"
```

Output: `tmp/fc_w3/wrapper_direct.csv`, `tmp/fc_w3/wrapper_cbi.csv`,
`tmp/fc_w3/wrapper_filecache.csv` (all three wrapper modes).

### Wave 4: TPCH all 22 queries (correctness before perf)

Correctness already verified in 018-C. Now run perf baseline:

```bash
for Q in 1 9 21; do
  /root/oss/velox/_build/relwithdebinfo/velox/benchmarks/tpch/velox_tpch_benchmark \
    --input_source=filecache \
    --data_path="${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}" \
    --data_format=parquet \
    --query_id=$Q \
    --rounds=3 \
    --num_splits_per_file=1 \
    --num_drivers=4 \
    --filecache_root=tmp/fc_tpch_w4 \
    --filecache_disk_gib=80 \
    --out=tmp/tpch_w4_fc_q${Q}.csv \
    > /root/oss/velox/_build/relwithdebinfo/test_018h_w4_q${Q}.log 2>&1
  echo "Wave 4 q$Q exit: $?"
done

# Full 22 only after smoke passes
/root/oss/velox/_build/relwithdebinfo/velox/benchmarks/tpch/velox_tpch_benchmark \
  --input_source=filecache \
  --data_path="${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}" \
  --data_format=parquet \
  --query_id=0 \
  --rounds=3 \
  --num_splits_per_file=1 \
  --num_drivers=4 \
  --filecache_root=tmp/fc_tpch_w4_full \
  --filecache_disk_gib=80 \
  --out=tmp/tpch_w4_fc_full.csv \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w4_full.log 2>&1
echo "Wave 4 full exit: $?"
```

### Steps

- [ ] **Step 1: Build all benchmark targets**

```bash
source /root/oss/velox-helper/env.sh
ninja -C /root/oss/velox/_build/relwithdebinfo \
  velox_ch_filecache_seek_benchmark \
  velox_ch_fcbi_benchmark \
  velox_bufferedinput_wrapper_benchmark \
  velox_tpch_benchmark \
  > /root/oss/velox/_build/relwithdebinfo/build_018h.log 2>&1
echo "exit: $?"
```

- [ ] **Step 2: Run Wave 1 (core seek micro)**
- [ ] **Step 3: Run Wave 2 (dedicated FCBI micro)**
- [ ] **Step 4: Run Wave 3 (wrapper A/B)**
- [ ] **Step 5: Run Wave 4 smoke (q01, q09, q21)**
- [ ] **Step 6: Run Wave 4 full (all 22) if smoke passes**
- [ ] **Step 7: Collect CSVs, report median wall_ms per (mode, query, round)**

**Gate:** All waves exit 0. CSVs collected. No crash or timeout. No hard regression threshold (baseline establishment only). Noise band established from within-run variance.

---

## Dependency Prechecks (BLOCKED semantics)

Run before any implementation step. If any check fails, the plan is BLOCKED.

```bash
cd /root/oss/velox
source /root/oss/velox-helper/env.sh

# 1. Branch
git --no-pager branch --show-current  # Must be "filecache"

# 2. Task 017A APIs exist (BLOCKED if absent)
test -f velox/ch/Common/FileCacheStats.h || { echo "BLOCKED: FileCacheStats.h not found"; exit 1; }
grep -q "takeFileCacheStatsSnapshot" velox/ch/Common/FileCacheStats.h || { echo "BLOCKED: takeFileCacheStatsSnapshot missing"; exit 1; }
grep -q "kFileCacheWriteBytes" velox/ch/Common/FileCacheStats.h || { echo "BLOCKED: kFileCacheWriteBytes missing"; exit 1; }
# FCBI must carry the cancellation token AND expose the accessor (018-F builder test).
grep -q "CancellationToken cancellationToken" velox/ch/Disks/IO/FileCacheBufferedInput.h || { echo "BLOCKED: FCBI ctor lacks cancellationToken param"; exit 1; }
grep -q "cancellationToken() const" velox/ch/Disks/IO/FileCacheBufferedInput.h || { echo "BLOCKED: FCBI lacks cancellationToken() accessor"; exit 1; }
# FileCacheManager::Options must expose the three validated resource fields (018-A/E).
grep -q "localFileSystem" velox/ch/Interpreters/FileCache/FileCacheManager.h || { echo "BLOCKED: Options.localFileSystem missing"; exit 1; }
grep -q "timekeeper" velox/ch/Interpreters/FileCache/FileCacheManager.h || { echo "BLOCKED: Options.timekeeper missing"; exit 1; }
grep -q "commonUserId() const" velox/ch/Interpreters/FileCache/FileCacheManager.h || { echo "BLOCKED: FileCacheManager::commonUserId() missing"; exit 1; }
# FileCacheConfig must expose backgroundDownloadThreads (018-E 5th key mapping).
grep -q "backgroundDownloadThreads" velox/ch/Interpreters/FileCache/FileCacheSettings.h || { echo "BLOCKED: FileCacheConfig.backgroundDownloadThreads missing"; exit 1; }

# 3. vcpkg toolchain
test -f "$CMAKE_TOOLCHAIN_FILE" || { echo "BLOCKED: CMAKE_TOOLCHAIN_FILE not set/found"; exit 1; }

# 4. Gluten worktree can be created
cd /root/oss/gluten && git worktree list | grep -v "018" | head -5

# 5. TPCH data (BLOCKED for 018-C and Wave 4)
test -d "${TPCH_DATA:-}" || echo "BLOCKED: TPCH_DATA unset or directory missing (018-C, 018-H Wave 4 cannot run)"
```

---

## Review Gates

### After each subtask

1. Build modified targets with `-Werror`, redirecting output to the build-dir log.
2. A subagent analyzes each build/test log and returns only a concise summary
   (build/test output is never dumped into the main context).
3. Run focused test(s), redirecting to a uniquely-named log in the build dir.
4. Verify RED (mutation proves coverage), then restore the mutation.
5. Verify GREEN (implementation satisfies test).
6. No new compiler warnings.
7. Worker does NOT commit.

### After all subtasks

1. Full CTest of all `velox_ch_*` targets in the debug build (freshly rebuilt).
2. Full CTest of all new Gluten test targets (`velox_file_cache_support_test`,
   `velox_file_cache_gluten_lifecycle_test`, `velox_file_cache_gluten_builder_test`,
   `velox_file_cache_gluten_metrics_test`) plus the Java `MetricsCarrierTest` and
   Scala `FileSourceScanMetricsUpdaterSuite`.
3. Read-only code-review subagent on the complete diff (Velox + Gluten).
4. Worker writes the result receipt. Worker does NOT commit.

---

## Explicit Exclusions

```text
Task 019 (Spark E2E) — excluded entirely
Task 017B (logging) — independent, executes after Task 018, not a dependency
pageLoadTimeNs key mismatch — existing bug, out of scope
Hard performance regression thresholds — baseline only
Multi-cache configuration — single "default" only
Prometheus / HTTP metrics server — deferred
Kernel O_DIRECT integration — deferred
AsyncDataCache + FileCache co-existence — rejected by mutual exclusion
TestValue seam — not used unless already approved in Task 017A
Spark SQLMetric end-to-end verification — full Spark-to-SQLMetric E2E deferred to Task 019; this plan proves the Scala updater increments the SQLMetric in isolation (FileSourceScanMetricsUpdaterSuite) and that the whole bridge compiles
```

---

## Task-Owned Files Summary

All files created or modified by this plan are either in:
1. `/root/oss/velox/` (Velox repo, branch `filecache`)
2. `/root/oss/gluten-018/` (isolated worktree)

No file in the dirty `/root/oss/gluten` (branch `main`) is touched. The worktree
shares no modified paths with the dirty main working tree.

---

## Result Receipt Location

```text
/root/oss/clickhouse/port/task/result/018-filecache-gluten-integration-result.md
```

Worker never stages or commits. Controller commits accepted subtasks.
