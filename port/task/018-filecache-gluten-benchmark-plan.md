# Task 018: FileCache Velox Benchmark Suite — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a Velox-native FileCache benchmark suite — byte-exact correctness gate, dedicated `FileCacheBufferedInput` micro, wrapper A/B, and TPCH baseline — consuming accepted Task 017A APIs.

**Architecture:** Velox benchmark binaries reuse the `baibaichen/ch-filecache` commit `45387d564` harness with a thin adapter to the current `FileCacheManager` API, covering byte-exact correctness verification, a dedicated `FileCacheBufferedInput` micro, the wrapper A/B across `direct`/`cbi`/`filecache`, and the TPCH baseline. No code is committed by the worker.

**Tech Stack:** C++20 (Velox — `CMAKE_CXX_STANDARD 20` in `CMakeLists.txt:16`), CMake/Ninja, vcpkg, GTest, folly, gflags.

## Global Constraints

```text
Task 018 is Velox-only: no Gluten or Spark integration is in scope.
Task 017A: accepted; Task 018 consumes its APIs only.
Task 017B: independent; executes after Task 018 and accepted Review 5 (Review 5
reviews Tasks 003-018 as a Velox-only FileCache system).
Task 019: excluded from this plan entirely.
No hard performance regression threshold (baseline only).
No commit by worker; controller commits accepted subtasks.
RelWithDebInfo benchmark build uses vcpkg toolchain.
Every benchmark binary is freshly built and run from a RelWithDebInfo or
Release build directory; Debug benchmark binaries are forbidden.
External dataset: ${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}
is required only after 018-P approval; it is not a pre-checkpoint blocker.
All temp/cache directories use tmp/ relative to CWD or build dirs; never /tmp.
All build/test output redirected to log files in the build directory.
Build/test logs are analyzed by task subagents per protocol.
No -j or nproc arguments to ninja.
Stop after all non-TPCH work. Do not copy, build, or run TPCH sources/targets
until the user approves the pre-TPCH checkpoint.
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
/// OperatorStats -> TaskStats.
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

### `FileCacheBufferedInput` constructor (accepted Task 017A)

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
    folly::F14FastMap<std::string, std::string> fileReadOps = {},
    folly::CancellationToken cancellationToken = {});
```

The **complete** Task-017A constructor order is:
`readFile, cache, cacheKey, origin, cacheOptions, requestContext, metricsLog, ioStatistics, ioStats, executor, readerOptions, fileReadOps, cancellationToken`

The appended `cancellationToken` has a default (`= {}`), so the benchmark's
`FileCacheBufferedInput` construction (via the test helper `makeInput`) needs no
change; it constructs with the default token.

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
| 018-B | Dedicated FCBI micro target + wrapper A/B smoke | Velox repo | FCBI micro exits 0; three wrapper Markdown tables produced with cbi/fcbi/dbi rows | 018-A not green |
| 018-D | Orchestration scripts | Velox repo | `bash -n` clean + sentinel tests pass | 018-A not green |
| 018-H1 | Non-TPCH performance waves 1–3 | Velox repo | RelWithDebInfo/Release outputs collected for core, FCBI, and wrapper modes | 018-B or 018-D not green |
| 018-P | Mandatory pre-TPCH checkpoint | Controller/user | non-TPCH receipt reviewed and user explicitly approves TPCH | 018-A/B/D/H1 incomplete |
| 018-C | TPCH benchmark correctness (post-checkpoint) | Velox repo | `rows`, `result_hash`, empty `error`, and zero exit status agree across all three modes for all 22 queries; q01 has rows > 0 | pre-TPCH approval absent or `${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}` unset |
| 018-H2 | TPCH performance wave 4 (post-checkpoint) | Velox repo | RelWithDebInfo/Release TPCH CSVs collected | 018-P approval absent or 018-C not green |

Execution order:

```text
018-A -> 018-B -> 018-D -> 018-H1
  -> STOP at 018-P for user approval
  -> 018-C -> 018-H2
```

Task 017B is independent and scheduled after Task 018 plus accepted Review 5
(not a Task-018 dependency).

---

## Environment Setup

### Velox non-TPCH benchmark build (RelWithDebInfo)

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
  -DVELOX_ENABLE_PARQUET=OFF \
  -DVELOX_BUILD_TESTING=ON \
  -DVELOX_MONO_LIBRARY=ON \
  -DVELOX_GFLAGS_TYPE=static \
  > /root/oss/velox/_build/relwithdebinfo/configure_018.log 2>&1
echo "exit: $?"
```

This RelWithDebInfo tree starts with `-DVELOX_ENABLE_PARQUET=OFF` and is used for
Tasks 018-A/B/H1. Only after 018-P approval, Task 018-C reconfigures this same
tree with `-DVELOX_ENABLE_PARQUET=ON` for 018-C/H2.

---

## Task 018-A: Benchmark Adapter + Correctness Verify

**Status: COMPLETE** — Velox commit `9850a70fa` ("Task 018: Add `FileCache`
correctness harness"), branch `filecache`, on top of accepted Task 017A
(`a856d836c`). The step checkboxes below are retained as the reproducible
procedure; they are not re-executed unless the harness is rebuilt.

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

- [ ] **Step 9: The FileCache statistics carrier is Task 017A-owned; Task 018 does not mutate it**

The only statistics-carrier line inside `velox_cache_verify`'s reach is
`FileCacheInputStream.cpp`'s `ioStats_->addCounter(kFileCacheWriteBytes, ...)`,
which is **Task 017A-owned source** — Task 018 owns no line there, so this plan
must not mutate it (see Global Constraints: Task 018 consumes Task 017A APIs
only). The content-corruption mutation in Step 8 above (over the 018-owned
`CacheReadHarness.cpp`) is the correctness gate for this subtask. The downstream
metric-bridge consumer of `kFileCacheWriteBytes` is out of scope for Task 018
(see *Explicit Exclusions*).

**Gate:** `velox_cache_verify` PASS all modes; Step 8 content-corruption
mutation confirmed RED then restored.

---

## Task 018-B: Dedicated FCBI Micro + Wrapper A/B Smoke

**Status: COMPLETE** — Velox commit `df9091e78` ("Task 018: Add
`FileCacheBufferedInput` microbenchmark"), branch `filecache`. The step
checkboxes below are retained as the reproducible procedure.

**Files:**
- Already created in 018-A: `velox/dwio/common/benchmarks/BufferedInputWrapperBenchmark.cpp`
- Target: `velox_bufferedinput_wrapper_benchmark` (registered in CMakeLists from 018-A)
- Existing FCBI seek micro: `velox/ch/benchmarks/FileCacheSeekBenchmark.cpp` (already in repo, target `velox_ch_filecache_seek_benchmark` in `velox/ch/benchmarks/CMakeLists.txt`)
- Create dedicated FCBI micro target: `velox/ch/benchmarks/FileCacheBufferedInputBenchmark.cpp`
- Modify: `velox/ch/benchmarks/CMakeLists.txt` — add `velox_ch_fcbi_benchmark`

**Interfaces:**
- Consumes: `FileCacheManager` API, `FileCacheBufferedInput`, test helpers from `velox/ch/Disks/IO/tests/FileCacheTestHelpers.h` (`makeManager`, `makeInput`, `makeDeterministicData`, `readAll`, `CountingReadFile`, `FileCacheTestOptions`), `velox_bufferedinput_wrapper_benchmark` binary (from 018-A)
- Produces: `velox_ch_fcbi_benchmark` binary (folly benchmark table on stdout), and wrapper smoke Markdown tables at `tmp/wrapper_direct.md`, `tmp/wrapper_cbi.md`, and `tmp/wrapper_fcbi.md`

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
  --wrappers=dbi \
  --target_ws_gb=0.25 \
  --remote_gb=0.5 \
  --read_sizes_kib=1024 \
  --workloads=sequential \
  --measure_passes=1 \
  --report_dir= \
  --out=tmp/wrapper_direct.md \
  > /root/oss/velox/_build/relwithdebinfo/test_018b_direct.log 2>&1
echo "exit: $?"
```

- [ ] **Step 5: Smoke run — cbi mode**

```bash
/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
  --wrappers=cbi \
  --ram_cache_gb=0.125 \
  --ssd_cache_gb=1 \
  --ssd_path=tmp/cbi_018b \
  --target_ws_gb=0.25 \
  --remote_gb=0.5 \
  --read_sizes_kib=1024 \
  --workloads=sequential \
  --measure_passes=1 \
  --report_dir= \
  --out=tmp/wrapper_cbi.md \
  > /root/oss/velox/_build/relwithdebinfo/test_018b_cbi.log 2>&1
echo "exit: $?"
```

- [ ] **Step 6: Smoke run — filecache mode**

```bash
/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
  --wrappers=fcbi \
  --filecache_root=tmp/fc_018b \
  --filecache_disk_gb=1 \
  --target_ws_gb=0.25 \
  --remote_gb=0.5 \
  --read_sizes_kib=1024 \
  --workloads=sequential \
  --measure_passes=1 \
  --report_dir= \
  --out=tmp/wrapper_fcbi.md \
  > /root/oss/velox/_build/relwithdebinfo/test_018b_fc.log 2>&1
echo "exit: $?"
```

- [ ] **Step 7: Validate Markdown schema and content**

```bash
for f in tmp/wrapper_direct.md tmp/wrapper_cbi.md tmp/wrapper_fcbi.md; do
  test -s "$f"
  grep -Fq '| pattern | read | wrapper | wall_ms | MB/s | ram_MB | ssd_MB | src_MB | Δ vs cbi |' "$f"
done
grep -Fq '| seq | 1024K | dbi |' tmp/wrapper_direct.md
grep -Fq '| seq | 1024K | cbi |' tmp/wrapper_cbi.md
grep -Fq '| seq | 1024K | fcbi |' tmp/wrapper_fcbi.md
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

**Gate:** All three wrapper Markdown outputs contain the common table header and
the expected wrapper row. Dedicated FCBI micro and seek micro exit 0. Every
binary path is under `_build/relwithdebinfo`.

---

## Task 018-D: Safe Orchestration Scripts

**Status: COMPLETE** — Velox commit `5ae39651b`, branch `filecache`.
All three scripts created and `chmod +x`; `bash -n` clean; 34 assertions pass.
Report: `/root/oss/clickhouse/.superpowers/sdd/018-D-report.md`.

**Files (new, untracked, mode 0755 / executable):**
- `velox/benchmarks/scripts/lib_cache_cleanup.sh` (sourced safety library)
- `velox/benchmarks/scripts/run_wrapper_ab.sh` (wrapper A/B orchestrator)
- `velox/benchmarks/scripts/run_tpch_ab.sh` (TPCH A/B orchestrator, 018-P gated)

**Interfaces:**
- Consumes: `velox_tpch_benchmark`, `velox_bufferedinput_wrapper_benchmark` binaries (RelWithDebInfo/Release only; Debug rejected)
- Produces: Shell scripts that handle cache lifecycle safely

---

### Step 1: `lib_cache_cleanup.sh` (exact accepted content)

```bash
#!/usr/bin/env bash
# Sourced by the Task 018-D benchmark orchestration scripts. Provides
# sentinel-based, fail-close cache cleanup so a mis-set path can never wipe an
# arbitrary directory.
#
# Safety model (mirrors the C++ helper dwio::common::bench::clearBenchmarkCacheRoot
# in velox/dwio/common/benchmarks/CacheReadHarness.cpp):
#   * Every destructive call resolves DIR and ROOT with `realpath -m` and refuses
#     to act unless DIR is a *strict component child* of ROOT (a proper
#     descendant, decided component-by-component so a sibling like `<root>2`
#     that merely shares a string prefix is rejected).
#   * A directory is only wiped when it carries an authentic sentinel: a regular,
#     non-symlink file named exactly `.velox_benchmark_cache_sentinel`. The
#     benchmark binaries preserve this file when they reset their cache roots, so
#     the EXIT trap here can still authenticate and remove the run directory.
#   * The filesystem root, the current working directory and DIR==ROOT are always
#     refused.
set -euo pipefail

# Exact sentinel name shared with the C++ contract (kCacheSentinelName in
# velox/dwio/common/benchmarks/CacheReadHarness.h). Do not rename without
# updating that header and Task 018-D.
SENTINEL_NAME=".velox_benchmark_cache_sentinel"

# Registry of (dir, root) pairs to clean on exit; appended by setup_trap_cleanup.
# Declared before any function so the trap handlers never need a top-level
# `local` (which is a syntax error outside a function body).
_CLEANUP_DIRS=()
_CLEANUP_ROOTS=()
_CLEANUP_TRAP_ARMED=0

# True when `child` is a strict (proper) component-wise descendant of `base`.
# Both arguments must already be absolute, lexically-normal paths (as produced by
# `realpath -m`). Equal paths return false. The check appends a trailing slash to
# `base` and strips it as a *literal* prefix, so the match can only succeed on a
# component boundary -- `/a/tmp2/x` is NOT considered a child of `/a/tmp`.
_is_strict_component_child() {
  local child="$1"
  local base="$2"
  local base_slash rest
  [[ "$child" != "$base" ]] || return 1
  if [[ "$base" == "/" ]]; then
    base_slash="/"
  else
    base_slash="$base/"
  fi
  rest="${child#"$base_slash"}"
  # Prefix absent -> not nested; prefix present but nothing after -> equal.
  [[ "$rest" != "$child" ]] || return 1
  [[ -n "$rest" ]] || return 1
  return 0
}

# Validates that DIR may be safely operated on relative to ROOT. Rejects empty
# inputs, non-absolute resolutions, the filesystem root, the current working
# directory, DIR==ROOT, and any DIR that is not a strict component child of ROOT.
validate_cache_dir() {
  local dir="${1-}"
  local root="${2-}"
  if [[ -z "$dir" || -z "$root" ]]; then
    echo "ERROR: cache dir and root must be non-empty (dir='$dir' root='$root')" >&2
    return 1
  fi
  local real_dir real_root cwd
  real_dir="$(realpath -m -- "$dir")"
  real_root="$(realpath -m -- "$root")"
  if [[ "$real_dir" != /* || "$real_root" != /* ]]; then
    echo "ERROR: resolved paths must be absolute (dir='$real_dir' root='$real_root')" >&2
    return 1
  fi
  if [[ "$real_dir" == "/" || "$real_root" == "/" ]]; then
    echo "ERROR: refusing to operate on the filesystem root (dir='$real_dir' root='$real_root')" >&2
    return 1
  fi
  cwd="$(realpath -m -- "$PWD")"
  if [[ "$real_dir" == "$cwd" ]]; then
    echo "ERROR: refusing to operate on the current working directory: $real_dir" >&2
    return 1
  fi
  if [[ "$real_dir" == "$real_root" ]]; then
    echo "ERROR: cache dir must not equal its root: $real_dir" >&2
    return 1
  fi
  if ! _is_strict_component_child "$real_dir" "$real_root"; then
    echo "ERROR: cache dir '$real_dir' is not a strict child of root '$real_root'" >&2
    return 1
  fi
  return 0
}

# create_sentinel DIR ROOT
# Validates (DIR strict child of ROOT), then creates DIR and writes a fresh
# sentinel as a mode-0600 regular file. Refuses to reuse any pre-existing marker
# -- regular, non-regular, or symlink (including a dangling one) -- and creates
# the file without following a symlink via `noclobber` (set -C) plus a 0177 umask.
create_sentinel() {
  local dir="${1-}"
  local root="${2-}"
  validate_cache_dir "$dir" "$root" || return 1
  local real_dir marker payload
  real_dir="$(realpath -m -- "$dir")"
  marker="$real_dir/$SENTINEL_NAME"
  # -e is false for a dangling symlink, so test -L explicitly too: any existing
  # marker of any type is refused rather than silently reused/overwritten.
  if [[ -e "$marker" || -L "$marker" ]]; then
    echo "ERROR: refusing to reuse a pre-existing sentinel marker: $marker" >&2
    return 1
  fi
  mkdir -p -- "$real_dir"
  payload="velox-benchmark-$$-$(date +%s)"
  # Subshell isolates `set -C` and the umask. noclobber makes `>` fail if the
  # target exists (so we never write through a symlink or clobber a file);
  # umask 0177 yields a 0600 regular file. $$ is the parent shell PID even here.
  if ! (
    set -C
    umask 0177
    printf '%s\n' "$payload" > "$marker"
  ); then
    echo "ERROR: failed to create sentinel (does it already exist?): $marker" >&2
    return 1
  fi
  if [[ -L "$marker" || ! -f "$marker" ]]; then
    echo "ERROR: sentinel is not a regular file after creation: $marker" >&2
    return 1
  fi
  return 0
}

# safe_wipe_cache_dir DIR ROOT
# Validates, requires an authentic (regular, non-symlink) sentinel inside DIR,
# then removes only the resolved DIR and verifies it is gone. No unresolved
# variables and no globbing: the target is the quoted, realpath-resolved child.
safe_wipe_cache_dir() {
  local dir="${1-}"
  local root="${2-}"
  validate_cache_dir "$dir" "$root" || return 1
  local real_dir marker
  real_dir="$(realpath -m -- "$dir")"
  marker="$real_dir/$SENTINEL_NAME"
  # -f follows symlinks (true only for a regular file); ! -L rejects a symlink
  # named like the sentinel. Together: a genuine regular file is required.
  if [[ -L "$marker" || ! -f "$marker" ]]; then
    echo "ERROR: refusing to wipe '$real_dir': missing authentic sentinel '$SENTINEL_NAME'" >&2
    return 1
  fi
  rm -rf -- "$real_dir"
  if [[ -e "$real_dir" || -L "$real_dir" ]]; then
    echo "ERROR: failed to remove cache dir: $real_dir" >&2
    return 1
  fi
  return 0
}

# Wipes every registered (dir, root) pair through safe_wipe_cache_dir. Runs from
# the EXIT/signal traps, so all `local`s stay inside this function body and the
# trap strings themselves contain no `local`. Returns nonzero if any wipe failed.
_run_cleanup() {
  local rc=0
  local n="${#_CLEANUP_DIRS[@]}"
  local i=0
  while [ "$i" -lt "$n" ]; do
    if ! safe_wipe_cache_dir "${_CLEANUP_DIRS[$i]}" "${_CLEANUP_ROOTS[$i]}"; then
      rc=1
    fi
    i=$((i + 1))
  done
  return "$rc"
}

# EXIT trap body. Preserves an original nonzero exit status; if the script
# succeeded (status 0) but cleanup failed, exits nonzero instead. `_exit_status`
# is captured first, as a plain (non-local) assignment, so `$?` is not disturbed.
_on_exit() {
  _exit_status=$?
  _cleanup_status=0
  _run_cleanup || _cleanup_status=$?
  if [ "$_exit_status" -ne 0 ]; then
    exit "$_exit_status"
  fi
  exit "$_cleanup_status"
}

# INT/TERM trap body. Detaches the EXIT trap (so cleanup is not run twice), wipes
# the registered dirs, and exits 130 (128 + signal) regardless of cleanup result.
_on_signal() {
  trap - EXIT
  _run_cleanup || true
  exit 130
}

# setup_trap_cleanup DIR ROOT
# Registers a (dir, root) pair for trap-driven cleanup and arms the EXIT/INT/TERM
# traps exactly once. Duplicate (dir, root) registrations are ignored so a pair
# is never wiped (or reported) twice.
setup_trap_cleanup() {
  local dir="${1-}"
  local root="${2-}"
  local n="${#_CLEANUP_DIRS[@]}"
  local i=0
  while [ "$i" -lt "$n" ]; do
    if [[ "${_CLEANUP_DIRS[$i]}" == "$dir" && "${_CLEANUP_ROOTS[$i]}" == "$root" ]]; then
      return 0
    fi
    i=$((i + 1))
  done
  _CLEANUP_DIRS+=("$dir")
  _CLEANUP_ROOTS+=("$root")
  if [ "$_CLEANUP_TRAP_ARMED" -eq 0 ]; then
    trap '_on_exit' EXIT
    trap '_on_signal' INT TERM
    _CLEANUP_TRAP_ARMED=1
  fi
  return 0
}

# validate_benchmark_binary BIN
# Requires BIN to be executable and to resolve (symlinks followed) to a path that
# names a RelWithDebInfo or Release build directory. Debug build binaries are
# refused, per the Task 018 global constraint forbidding Debug benchmarks.
validate_benchmark_binary() {
  local bin="${1-}"
  if [[ -z "$bin" ]]; then
    echo "ERROR: validate_benchmark_binary: empty binary path" >&2
    return 1
  fi
  if [[ ! -x "$bin" ]]; then
    echo "ERROR: benchmark binary is not executable: $bin" >&2
    return 1
  fi
  local real lower probe
  real="$(realpath -- "$bin")"
  lower="${real,,}"
  # Strip the allowed 'relwithdebinfo' token before the Debug scan so it is not a
  # false positive (it does not contain the substring "debug", but stripping is
  # explicit and future-proof), then reject any genuine Debug build path.
  probe="${lower//relwithdebinfo/}"
  case "$probe" in
    *debug*)
      echo "ERROR: refusing Debug benchmark binary (RelWithDebInfo/Release required): $real" >&2
      return 1
      ;;
  esac
  case "$lower" in
    *relwithdebinfo* | *release*)
      return 0
      ;;
    *)
      echo "ERROR: benchmark binary is not from a RelWithDebInfo/Release build dir: $real" >&2
      return 1
      ;;
  esac
}
```

---

### Step 2: `run_wrapper_ab.sh` (exact accepted content)

```bash
#!/usr/bin/env bash
# Task 018-D wrapper A/B orchestrator.
#
# Runs velox_bufferedinput_wrapper_benchmark ONCE with --wrappers=all so the
# Markdown table carries real CBI-relative deltas for all three read paths:
# cbi (AsyncDataCache RAM+SSD), fcbi (ch::FileCache) and dbi (DirectBufferedInput).
# cbi is the delta baseline; fcbi/dbi rows report `Δ vs cbi`.
#
# The cbi SSD tier and the fcbi disk tier each get their own sentinel-marked
# child directory under an absolute CACHE_ROOT. Both are registered for
# trap-driven cleanup: the benchmark preserves the sentinel when it resets those
# roots, and the EXIT trap authenticates and removes them afterwards. Nothing is
# deleted manually here -- the trap does it so the sentinel + strict-child checks
# always gate the removal.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib_cache_cleanup.sh
source "$SCRIPT_DIR/lib_cache_cleanup.sh"

: "${BIN:?set BIN to the velox_bufferedinput_wrapper_benchmark path (RelWithDebInfo/Release)}"
: "${CACHE_ROOT:=$(pwd)/tmp/velox_wrapper_ab_cache}"
: "${OUT:=$(pwd)/tmp/wrapper_ab_results/wrapper_all.md}"

# A/B knobs (env-overridable). Defaults mirror the benchmark's own gflags.
: "${RAM_CACHE_GB:=4}"
: "${SSD_CACHE_GB:=80}"
: "${FILECACHE_DISK_GB:=80}"
: "${TARGET_WS_GB:=32}"
: "${REMOTE_GB:=0}"
: "${READ_SIZES_KIB:=1024,8192}"
: "${WORKLOADS:=sequential,zipfian}"
: "${MEASURE_PASSES:=3}"
# COLD_EACH_PASS is optional; set it to 1/true to wipe the local tier before
# every measure pass (cold cache-populate path). Left unset means warm reuse.

validate_benchmark_binary "$BIN"

CACHE_ROOT="$(realpath -m -- "$CACHE_ROOT")"
if [[ "$CACHE_ROOT" != /* ]]; then
  echo "ERROR: CACHE_ROOT must resolve to an absolute path: $CACHE_ROOT" >&2
  exit 1
fi
mkdir -p -- "$CACHE_ROOT"
mkdir -p -- "$(dirname -- "$OUT")"

# Separate sentineled child dirs: cbi owns the SSD tier, fcbi owns the disk tier.
CBI_SSD_DIR="$CACHE_ROOT/cbi_ssd"
FCBI_DIR="$CACHE_ROOT/fcbi_cache"

create_sentinel "$CBI_SSD_DIR" "$CACHE_ROOT"
setup_trap_cleanup "$CBI_SSD_DIR" "$CACHE_ROOT"
create_sentinel "$FCBI_DIR" "$CACHE_ROOT"
setup_trap_cleanup "$FCBI_DIR" "$CACHE_ROOT"

args=(
  --wrappers=all
  --ram_cache_gb="$RAM_CACHE_GB"
  --ssd_cache_gb="$SSD_CACHE_GB"
  --filecache_disk_gb="$FILECACHE_DISK_GB"
  --target_ws_gb="$TARGET_WS_GB"
  --remote_gb="$REMOTE_GB"
  --read_sizes_kib="$READ_SIZES_KIB"
  --workloads="$WORKLOADS"
  --measure_passes="$MEASURE_PASSES"
  --ssd_path="$CBI_SSD_DIR"
  --filecache_root="$FCBI_DIR"
  --report_dir=
  --out="$OUT"
)
if [[ "${COLD_EACH_PASS:-}" == "1" || "${COLD_EACH_PASS:-}" == "true" ]]; then
  args+=(--cold_each_pass)
fi

echo "Running wrapper A/B (all wrappers) -> $OUT" >&2
"$BIN" "${args[@]}"

# Validate the Markdown output: non-empty, carries the common header, and has a
# row for each of the three wrappers (labels cbi/fcbi/dbi; workload labels are
# seq/zipf/uni, not the long 'sequential').
if [[ ! -s "$OUT" ]]; then
  echo "ERROR: wrapper output is empty: $OUT" >&2
  exit 1
fi
HEADER='| pattern | read | wrapper | wall_ms | MB/s | ram_MB | ssd_MB | src_MB | Δ vs cbi |'
if ! grep -qF -- "$HEADER" "$OUT"; then
  echo "ERROR: wrapper output missing the common header: $OUT" >&2
  exit 1
fi
for w in cbi fcbi dbi; do
  if ! grep -qF -- "| $w |" "$OUT"; then
    echo "ERROR: wrapper output missing the '$w' row: $OUT" >&2
    exit 1
  fi
done

echo "Wrapper A/B complete; validated $OUT (cbi/fcbi/dbi rows present)." >&2
# No manual wipe: the EXIT trap authenticates each sentinel and removes the two
# cache child dirs.
```

---

### Step 3: `run_tpch_ab.sh` (exact accepted content)

> **Global hard rule — 018-P gate:** No real TPCH run may happen before the
> pre-TPCH checkpoint (Task 018-P) is explicitly approved. This script enforces
> this by checking `TPCH_APPROVED=1` *before* it references `BIN` or `TPCH_DATA`.
> The creation and syntax-testing of this script is part of Task 018-D; the first
> real TPCH run is Task 018-H2, after 018-P approval.

```bash
#!/usr/bin/env bash
# Task 018-D TPCH A/B orchestrator.
#
# 018-P GATE: TPCH is forbidden until the user approves the pre-TPCH checkpoint.
# This script therefore refuses to touch anything TPCH-related -- it does NOT
# even look at BIN or TPCH_DATA -- unless TPCH_APPROVED=1 is set in the
# environment. Creation/syntax of this script is part of Task 018-D; actually
# running it against real TPCH data happens only in Task 018-H2, after approval.
#
# When approved, it sweeps velox_tpch_benchmark across the three A/B backends:
#   direct    -- pure Velox DirectBufferedInput reads (no application cache)
#   cbi       -- in-process AsyncDataCache sized by --cache_gb
#   filecache -- ch::FileCache on a sentinel-marked, trap-cleaned disk root
# Only the filecache backend owns a task-managed disk directory; direct and cbi
# need no task-owned cache root.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib_cache_cleanup.sh
source "$SCRIPT_DIR/lib_cache_cleanup.sh"

# --- 018-P approval gate: checked before BIN / TPCH_DATA are referenced. ---
: "${TPCH_APPROVED:?refusing to run TPCH: set TPCH_APPROVED=1 only after the 018-P pre-TPCH checkpoint is approved}"
if [[ "$TPCH_APPROVED" != "1" ]]; then
  echo "ERROR: TPCH_APPROVED must be exactly 1 (got '$TPCH_APPROVED'); 018-P approval required" >&2
  exit 1
fi

# Only AFTER approval do we require the data directory and the binary.
: "${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}"
: "${BIN:?set BIN to the velox_tpch_benchmark path (RelWithDebInfo/Release)}"
validate_benchmark_binary "$BIN"

: "${CACHE_ROOT:=$(pwd)/tmp/velox_tpch_ab_cache}"
: "${OUT_DIR:=$(pwd)/tmp/tpch_ab_results}"
: "${DATA_FORMAT:=parquet}"
: "${QUERY_ID:=0}"
: "${ROUNDS:=3}"
: "${NUM_SPLITS_PER_FILE:=1}"
: "${NUM_DRIVERS:=4}"
: "${FILECACHE_DISK_GIB:=58}"
: "${CACHE_GB:=4}"

CACHE_ROOT="$(realpath -m -- "$CACHE_ROOT")"
if [[ "$CACHE_ROOT" != /* ]]; then
  echo "ERROR: CACHE_ROOT must resolve to an absolute path: $CACHE_ROOT" >&2
  exit 1
fi
mkdir -p -- "$OUT_DIR" "$CACHE_ROOT"

for MODE in direct cbi filecache; do
  args=(
    --input_source="$MODE"
    --data_path="$TPCH_DATA"
    --data_format="$DATA_FORMAT"
    --query_id="$QUERY_ID"
    --rounds="$ROUNDS"
    --num_splits_per_file="$NUM_SPLITS_PER_FILE"
    --num_drivers="$NUM_DRIVERS"
    --out="$OUT_DIR/tpch_${MODE}.csv"
  )
  case "$MODE" in
    direct)
      # No application cache and no task-owned disk root.
      :
      ;;
    cbi)
      # In-process AsyncDataCache; the binary requires --cache_gb > 0 for cbi.
      args+=(--cache_gb="$CACHE_GB")
      ;;
    filecache)
      # Only this backend owns a disk cache root; sentinel it and arm cleanup.
      FC_DIR="$CACHE_ROOT/filecache_run"
      create_sentinel "$FC_DIR" "$CACHE_ROOT"
      setup_trap_cleanup "$FC_DIR" "$CACHE_ROOT"
      args+=(--filecache_root="$FC_DIR" --filecache_disk_gib="$FILECACHE_DISK_GIB")
      ;;
  esac
  echo "Running TPCH A/B backend '$MODE' -> $OUT_DIR/tpch_${MODE}.csv" >&2
  "$BIN" "${args[@]}"
done

echo "TPCH A/B complete; CSVs under $OUT_DIR" >&2
# No manual wipe: the EXIT trap authenticates the filecache sentinel and removes
# the disk cache child directory.
```

---

### Step 4: File mode

```bash
chmod +x velox/benchmarks/scripts/lib_cache_cleanup.sh \
         velox/benchmarks/scripts/run_wrapper_ab.sh \
         velox/benchmarks/scripts/run_tpch_ab.sh
```

---

### Step 5: Syntax check

```bash
for f in velox/benchmarks/scripts/*.sh; do
  bash -n "$f" && echo "OK: $f" || echo "FAIL: $f"
done
```

Expected output:
```
OK: velox/benchmarks/scripts/lib_cache_cleanup.sh
OK: velox/benchmarks/scripts/run_tpch_ab.sh
OK: velox/benchmarks/scripts/run_wrapper_ab.sh
```

---

### Step 6: Sentinel safety cases

Test the key safety invariants of `lib_cache_cleanup.sh` in a sandbox under
`tmp/` (no real TPCH or build required):

```bash
# Source the library (no-op side-effects; arrays and flag are initialised)
(
  cd /root/oss/velox
  source velox/benchmarks/scripts/lib_cache_cleanup.sh

  ROOT="$(pwd)/tmp/test_sentinel_root"
  DIR="$ROOT/run1"
  mkdir -p "$ROOT"

  # 1a. create_sentinel: creates a 0600 regular non-symlink file.
  create_sentinel "$DIR" "$ROOT"
  [[ -f "$DIR/$SENTINEL_NAME" && ! -L "$DIR/$SENTINEL_NAME" ]] && echo "1a SENTINEL_REGULAR_OK"

  # 1b. safe_wipe_cache_dir: removes the sentinel-marked child dir.
  safe_wipe_cache_dir "$DIR" "$ROOT" && [[ ! -d "$DIR" ]] && echo "1b WIPE_OK"

  # 2. No-sentinel dir is refused; payload survives.
  mkdir -p "$ROOT/nosent"
  touch "$ROOT/nosent/payload"
  safe_wipe_cache_dir "$ROOT/nosent" "$ROOT" 2>&1 | grep -q "ERROR" && \
    [[ -f "$ROOT/nosent/payload" ]] && echo "2 NO_SENTINEL_REFUSED_OK"
  rm -rf "$ROOT/nosent"

  # 3. Sibling-prefix escape: /a/tmp2/x must NOT be treated as child of /a/tmp.
  SIBLING="$(dirname "$ROOT")2/run"
  validate_cache_dir "$SIBLING" "$ROOT" 2>&1 | grep -q "ERROR" && echo "3 SIBLING_ESCAPE_REFUSED_OK"

  # 4a. Symlink sentinel is refused on create (pre-existing symlink marker).
  mkdir -p "$ROOT/symsent"
  ln -s /dev/null "$ROOT/symsent/$SENTINEL_NAME"
  create_sentinel "$ROOT/symsent" "$ROOT" 2>&1 | grep -q "ERROR" && echo "4a CREATE_REFUSES_SYMLINK_OK"
  # 4b. Symlink sentinel is refused on wipe.
  safe_wipe_cache_dir "$ROOT/symsent" "$ROOT" 2>&1 | grep -q "ERROR" && \
    [[ -d "$ROOT/symsent" ]] && echo "4b WIPE_REFUSES_SYMLINK_OK"
  rm -rf "$ROOT/symsent"

  # 5. Filesystem root, cwd, and DIR==ROOT are refused.
  validate_cache_dir "/" "$ROOT" 2>&1 | grep -q "ERROR" && echo "5a FS_ROOT_REFUSED_OK"
  validate_cache_dir "$(pwd)" "$ROOT" 2>&1 | grep -q "ERROR" && echo "5b CWD_REFUSED_OK"
  validate_cache_dir "$ROOT" "$ROOT" 2>&1 | grep -q "ERROR" && echo "5c DIR_EQ_ROOT_REFUSED_OK"

  rm -rf "$ROOT"
  echo "All sentinel safety cases passed."
)
```

---

### Step 7: Fake RelWithDebInfo wrapper e2e

Verify `run_wrapper_ab.sh` end-to-end without a real binary by providing a fake
binary whose path contains `relwithdebinfo` and that emits the exact live Markdown
table format:

```bash
(
  cd /root/oss/velox
  FDIR="$(pwd)/tmp/018d_e2e_fake"
  FBIN="$FDIR/relwithdebinfo/velox_bufferedinput_wrapper_benchmark"
  CR="$FDIR/cache_root"
  OUT="$FDIR/out/wrapper_all.md"
  mkdir -p "$(dirname "$FBIN")" "$CR" "$(dirname "$OUT")"

  # Fake binary: write the exact Markdown table and drop a file into each cache
  # dir to simulate cache use (sentinel already created by the script).
  cat > "$FBIN" <<'EOF'
#!/usr/bin/env bash
# Extract --ssd_path and --filecache_root from args; touch a payload in each.
for arg in "$@"; do
  case "$arg" in
    --ssd_path=*)      touch "${arg#--ssd_path=}/payload_cbi" ;;
    --filecache_root=*) touch "${arg#--filecache_root=}/payload_fcbi" ;;
    --out=*)           OUT="${arg#--out=}" ;;
  esac
done
mkdir -p "$(dirname "$OUT")"
cat > "$OUT" <<'MD'
| pattern | read | wrapper | wall_ms | MB/s | ram_MB | ssd_MB | src_MB | Δ vs cbi |
|---------|------|---------|---------|------|--------|--------|--------|----------|
| seq | 1024K | cbi | 1200 | 820.0 | 4096 | 80000 | 0 | — |
| seq | 1024K | fcbi | 2250 | 440.0 | 0 | 0 | 80000 | +87.5% |
| seq | 1024K | dbi | 1174 | 837.4 | 0 | 0 | 80000 | -2.1% |
MD
EOF
  chmod +x "$FBIN"

  # e2e run: exits 0, output validated, both cache child dirs trap-cleaned.
  BIN="$FBIN" CACHE_ROOT="$CR" OUT="$OUT" \
    bash velox/benchmarks/scripts/run_wrapper_ab.sh && echo "E2E_EXIT0_OK"
  [[ ! -d "$CR/cbi_ssd" && ! -d "$CR/fcbi_cache" ]] && echo "E2E_TRAP_CLEANED_OK"
  [[ -d "$CR" ]] && echo "E2E_CACHE_ROOT_INTACT_OK"
  grep -qF '| cbi |'  "$OUT" && echo "E2E_CBI_ROW_OK"
  grep -qF '| fcbi |' "$OUT" && echo "E2E_FCBI_ROW_OK"
  grep -qF '| dbi |'  "$OUT" && echo "E2E_DBI_ROW_OK"

  # Debug binary rejection: path contains 'debug', must be rejected before any run.
  DBIN="$FDIR/debug/velox_bufferedinput_wrapper_benchmark"
  mkdir -p "$(dirname "$DBIN")" && cp "$FBIN" "$DBIN" && chmod +x "$DBIN"
  BIN="$DBIN" CACHE_ROOT="$CR" OUT="$OUT" \
    bash velox/benchmarks/scripts/run_wrapper_ab.sh 2>&1 | grep -qi "debug" && \
    echo "E2E_DEBUG_REJECTED_OK"

  rm -rf "$FDIR"
)
```

---

### Step 8: TPCH `TPCH_APPROVED` pre-gate (no TPCH run)

Confirm that `run_tpch_ab.sh` refuses to proceed — and never references `BIN` or
`TPCH_DATA` — unless `TPCH_APPROVED=1`. No TPCH binary or data is required.

```bash
(
  cd /root/oss/velox

  # Gate fires before BIN/TPCH_DATA: all three unset → TPCH_APPROVED message.
  out=$(bash velox/benchmarks/scripts/run_tpch_ab.sh 2>&1 || true)
  echo "$out" | grep -q "TPCH_APPROVED" && \
  ! echo "$out" | grep -qE "\bBIN\b|\bTPCH_DATA\b" && \
  echo "GATE_UNSET_OK"

  # TPCH_APPROVED=0 → "must be exactly 1" error, still no BIN/TPCH_DATA demand.
  out=$(TPCH_APPROVED=0 bash velox/benchmarks/scripts/run_tpch_ab.sh 2>&1 || true)
  echo "$out" | grep -q "must be exactly 1" && echo "GATE_ZERO_OK"
  echo "$out" | grep -q "018-P" && echo "GATE_018P_MENTIONED_OK"
)
```

---

### Step 9: Sandbox mutations (safety invariant regression probes)

Prove the two critical safety properties hold, and that targeted mutations break
each one:

```bash
(
  cd /root/oss/velox
  SBX="$(pwd)/tmp/018d_mutation_sandbox"
  mkdir -p "$SBX"
  cp velox/benchmarks/scripts/lib_cache_cleanup.sh "$SBX/lib_orig.sh"

  # Baseline: sibling-escape and no-sentinel probes both HELD.
  ROOT="$SBX/root" && mkdir -p "$ROOT"
  SIBLING="$(dirname "$ROOT")2/run"
  (source "$SBX/lib_orig.sh"; validate_cache_dir "$SIBLING" "$ROOT" 2>&1 | grep -q "ERROR") && \
    echo "BASELINE_SIBLING_HELD"
  DIR="$ROOT/nosentdir" && mkdir -p "$DIR"
  (source "$SBX/lib_orig.sh"; safe_wipe_cache_dir "$DIR" "$ROOT" 2>&1 | grep -q "ERROR") && \
    [[ -d "$DIR" ]] && echo "BASELINE_NOSENTINEL_HELD"

  # Mutation A: force _is_strict_component_child to always return 0.
  awk '/^_is_strict_component_child\(\)/{found=1} found && /return 0/{sub(/return 0/,"return 0 # mutant"); found=0} 1' \
    "$SBX/lib_orig.sh" > "$SBX/lib_mutA.sh"
  # Replace body so it always succeeds:
  awk 'NR==FNR{next} /^_is_strict_component_child\(\)/,/^}/' \
    "$SBX/lib_orig.sh" "$SBX/lib_orig.sh" > /dev/null  # no-op pre-check
  sed 's/^\( *\)\["\$rest" != "\$child"\] || return 1/\1true/' \
    "$SBX/lib_orig.sh" | \
    sed 's/^\( *\)\["\$rest" != "\$child"\]/\1true/' > "$SBX/lib_mutA.sh"
  # Quick structural check: if sibling escape is now allowed, VIOLATED.
  result=$(source "$SBX/lib_mutA.sh" 2>/dev/null; validate_cache_dir "$SIBLING" "$ROOT" 2>&1 || true)
  [[ -z "$result" ]] && echo "MUTATION_A_SIBLING_VIOLATED" || echo "MUTATION_A_sibling_held_unexpectedly"

  # Mutation B: disable the sentinel guard in safe_wipe_cache_dir.
  sed 's/if \[\[ -L "\$marker".*\]\]; then/if false; then/' \
    "$SBX/lib_orig.sh" > "$SBX/lib_mutB.sh"
  result=$(source "$SBX/lib_mutB.sh"; safe_wipe_cache_dir "$DIR" "$ROOT" 2>&1 || true)
  [[ -z "$result" ]] && echo "MUTATION_B_NOSENTINEL_VIOLATED" || echo "MUTATION_B_nosentinel_held_unexpectedly"

  # Restore: confirm no tracked file was modified.
  rm -rf "$SBX"
  git -C /root/oss/velox diff --quiet velox/benchmarks/scripts/lib_cache_cleanup.sh && \
    echo "TRACKED_LIB_UNCHANGED_OK"
)
```

---

**Gate (018-D):** All of the following must be GREEN before proceeding to Task 018-H1:

- `bash -n` exits 0 for all three scripts (Step 5).
- Sentinel safety cases all print `OK` (Step 6): `SENTINEL_REGULAR_OK`,
  `WIPE_OK`, `NO_SENTINEL_REFUSED_OK`, `SIBLING_ESCAPE_REFUSED_OK`,
  `CREATE_REFUSES_SYMLINK_OK`, `WIPE_REFUSES_SYMLINK_OK`, `FS_ROOT_REFUSED_OK`,
  `CWD_REFUSED_OK`, `DIR_EQ_ROOT_REFUSED_OK`.
- Fake wrapper e2e (Step 7): `E2E_EXIT0_OK`, `E2E_TRAP_CLEANED_OK`,
  `E2E_CACHE_ROOT_INTACT_OK`, `E2E_CBI_ROW_OK`, `E2E_FCBI_ROW_OK`,
  `E2E_DBI_ROW_OK`, `E2E_DEBUG_REJECTED_OK`.
- TPCH gate (Step 8): `GATE_UNSET_OK`, `GATE_ZERO_OK`, `GATE_018P_MENTIONED_OK`.
- Mutation probes (Step 9): `BASELINE_SIBLING_HELD`, `BASELINE_NOSENTINEL_HELD`,
  `MUTATION_A_SIBLING_VIOLATED`, `MUTATION_B_NOSENTINEL_VIOLATED`,
  `TRACKED_LIB_UNCHANGED_OK`.
- No real TPCH run executed; no stage/commit/amend/rebase/push.

---

## Task 018-H1: Non-TPCH Performance Waves (Baseline, No Threshold)

**Depends on:** 018-B and 018-D only (not 018-C).

**Files:**
- No new source files. Uses the non-TPCH binaries from 018-A/018-B and the
  accepted orchestration script from 018-D.
- Output: Markdown/timing artifacts in `tmp/` and logs in the build directory.

**Interfaces:**
- Consumes: `velox_ch_filecache_seek_benchmark`, `velox_ch_fcbi_benchmark`,
  `velox_bufferedinput_wrapper_benchmark`, and the accepted
  `velox/benchmarks/scripts/run_wrapper_ab.sh`.
- Produces: baseline folly benchmark tables and the wrapper A/B Markdown table
  (all read paths). No performance threshold.

**Scheduling:** Waves 1–3 run after 018-B and 018-D are green. The Worker then
stops at 018-P; no TPCH source is copied, built, or run here.

Every binary path is under a RelWithDebInfo or Release build directory; a Debug
binary invalidates the result.

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

### Wave 2: Dedicated FCBI micro (`velox_ch_fcbi_benchmark`)

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

### Wave 3: Wrapper A/B — all read paths (dbi/cbi/fcbi) via the accepted script

Use the accepted 018-D orchestrator `run_wrapper_ab.sh`. It runs
`velox_bufferedinput_wrapper_benchmark` once with `--wrappers=all` and writes a
single Markdown table (`cbi` is the delta baseline; `fcbi`/`dbi` rows carry
`Δ vs cbi`), sentinel-cleaning the cbi SSD tier and the fcbi disk tier on exit.
Drive it only with the environment variables the accepted script reads (`BIN`,
`CACHE_ROOT`, `OUT`, and the A/B knobs); it emits Markdown via `--out`, never a
CSV file and never an `--input_source` flag:

```bash
mkdir -p tmp/fc_w3
BIN=/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
CACHE_ROOT="$(pwd)/tmp/fc_w3_cache" \
OUT="$(pwd)/tmp/fc_w3/wrapper_all.md" \
RAM_CACHE_GB=4 SSD_CACHE_GB=10 FILECACHE_DISK_GB=20 \
TARGET_WS_GB=8 REMOTE_GB=9 \
READ_SIZES_KIB=1024,8192 WORKLOADS=sequential,zipfian \
MEASURE_PASSES=3 \
  bash velox/benchmarks/scripts/run_wrapper_ab.sh \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w3.log 2>&1
echo "Wave 3 exit: $?"
```

The asymmetric raw disk sizes are intentional. CBI can use its full 10 GiB SSD
tier after the chunked RAM-to-SSD warm. The production-default FileCache policy
is `SLRU` with `slruSizeRatio=0.6`; first-touch warm entries occupy only the 40%
probationary queue. A 20 GiB FileCache therefore provides 8 GiB of effective
first-touch capacity for the 8 GiB target. The Controller must reject the run if
the measured FCBI rows still report non-zero source bytes.

Output: `tmp/fc_w3/wrapper_all.md` — one Markdown table with `cbi`, `fcbi`, and
`dbi` rows (the accepted script validates the common header and all three rows).

### Steps

- [ ] **Step 1: Build non-TPCH benchmark targets in RelWithDebInfo**

```bash
source /root/oss/velox-helper/env.sh
ninja -C /root/oss/velox/_build/relwithdebinfo \
  velox_ch_filecache_seek_benchmark \
  velox_ch_fcbi_benchmark \
  velox_bufferedinput_wrapper_benchmark \
  > /root/oss/velox/_build/relwithdebinfo/build_018h1.log 2>&1
echo "exit: $?"
```

- [ ] **Step 2: Run Wave 1 (core seek micro)**
- [ ] **Step 3: Run Wave 2 (dedicated FCBI micro)**
- [ ] **Step 4: Run Wave 3 (wrapper A/B, all read paths)**
- [ ] **Step 5: Collect the non-TPCH artifacts and hand off to 018-P**

The Worker records the build type, exact binary paths, and the Wave 1–3
artifacts (`test_018h_w1.log`, `test_018h_w2.log`, `tmp/fc_w3/wrapper_all.md`)
into the Task-018 receipt, then stops for the mandatory pre-TPCH checkpoint.

**Gate (018-H1):** Waves 1–3 exit 0; every benchmark binary path is under a
RelWithDebInfo or Release build directory (any Debug binary invalidates the
result); the wrapper Markdown carries the common header and the `cbi`/`fcbi`/`dbi`
rows. No hard regression threshold is applied. No TPCH source is copied, built,
or run.

---

## Task 018-P: Mandatory Pre-TPCH Checkpoint

**This is a hard STOP.** Waves 1–3 (018-H1) are complete; no TPCH work may begin
until the user explicitly approves.

The Worker writes the non-TPCH results to the Task-018 receipt — build type,
exact binary paths, correctness results (018-A), the 018-D safety evidence, and
the Wave 1–3 artifacts — then sets:

```text
worker_status: waiting_for_pre_tpch_approval
tpch_sources_copied: false
tpch_target_built: false
tpch_commands_run: false
```

The Controller verifies the receipt and asks for explicit user approval. No
Worker continues into 018-C or 018-H2 under the original dispatch. Only after
approval does the Controller dispatch a fresh Task-018 Worker to perform 018-C
(the first authorized TPCH source copy, target registration, fresh
RelWithDebInfo build, and correctness run) and then 018-H2.

**Gate (018-P):** the non-TPCH receipt is reviewed and the user explicitly
approves TPCH. BLOCKED while any of 018-A/B/D/H1 is incomplete.

---

## Task 018-C: TPCH Benchmark Correctness

**Carry-forward reconciliation (Velox-only).** Before the first TPCH run,
confirm the two properties 018-C and 018-H2 inherit from the earlier Velox
stages:

- [ ] **Step 0a: Reconfigure Parquet ON after 018-P approval.** The pre-checkpoint build is
   intentionally Parquet OFF. Reconfigure the same RelWithDebInfo build with
   `-DVELOX_ENABLE_PARQUET=ON` and verify Arrow/Parquet dependencies before
   copying or building the TPCH target:

   ```bash
   source /root/oss/velox-helper/env.sh
   cmake -S /root/oss/velox -B /root/oss/velox/_build/relwithdebinfo -G Ninja \
     -DCMAKE_BUILD_TYPE=RelWithDebInfo \
     -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
     -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
     -DVELOX_ENABLE_BENCHMARKS=ON \
     -DVELOX_ENABLE_PARQUET=ON \
     -DVELOX_ENABLE_ARROW_TESTING=OFF \
     -DVELOX_BUILD_TESTING=ON \
     -DVELOX_MONO_LIBRARY=ON \
     -DVELOX_GFLAGS_TYPE=static \
     > /root/oss/velox/_build/relwithdebinfo/configure_018c_parquet.log 2>&1
   ```

   `ARROW_CMAKE_ARGS` in
   `CMake/resolve_dependency_modules/arrow/CMakeLists.txt` now hard-codes
   `-DARROW_ZSTD_USE_SHARED=OFF` next to `-DARROW_WITH_ZSTD=ON`, so no
   `-D...=...` flag for it needs to be passed on this command line. This
   environment's vcpkg `zstd` triplet exports only the
   `zstd::libzstd_static`/`zstd::libzstd` CMake targets (no
   `zstd::libzstd_shared`), while bundled Arrow is built `ARROW_BUILD_STATIC=ON`;
   without the flag Arrow's `ThirdpartyToolchain.cmake` zstd resolution fails
   with `Zstandard target doesn't exist: zstd::libzstd_shared`. Tracking the
   flag in the CMakeLists.txt (instead of only in a generated
   `arrow_ep-build/CMakeCache.txt` edit) makes a from-scratch recreation of
   the `arrow_ep` prefix reproducible without a manual `cmake -D... .`
   invocation inside the generated `arrow_ep-build` directory.

- [ ] **Step 0b: Split backend-specific statistics before runtime use.** The current
   `bytes_dl_mib` and `evict_mib` columns are not comparable across CBI and
   FileCache. Before building TPCH, change `AbBenchmarkBase.cpp` to emit:

   ```text
   cache_read_mib    CBI hitBytes / FileCache cacheReadBytes
   predownload_mib   FileCache predownloadedFromSourceBytes; zero for CBI/direct
   evict_mib         FileCache evictedBytes; zero for CBI/direct
   evict_count       FileCache evictedSegments / CBI numEvict; zero for direct
   ```

   Replace `BackendSnapshot::downloadBytes`, `evictUnits`, and `evictInBytes`
   with `cacheReadBytes`, `predownloadBytes`, `evictedBytes`, and
   `evictionCount`. Add matching fields to `AbCsvRow`, update
   `writeCsvHeader`/`writeCsvRow`, and compute monotonic deltas independently.
   Add a focused unit-testable helper or test target that proves both FileCache
   and CBI mappings; a mutation that swaps bytes/count units must fail.

Only `wall_ms`, `rows`, `result_hash`, `bytes_read`, `hit_pct`, and
`cache_read_mib` are cross-backend comparisons. Backend-specific columns are
explicitly zero when unavailable; no count is labeled as MiB.

- [ ] **Step 0c: Lock the post-reconciliation CSV schema.**

The exact header after Step 0b is:

```text
round,query_id,wall_ms,rows,result_hash,bytes_read,hit_pct,cache_read_mib,predownload_mib,evict_mib,evict_count,op_p50_us,op_p95_us,error
```

The focused schema test must require exactly these 14 fields in this order.
Before interpreting any correctness row, the shell gate below compares the
header byte-for-byte and requires each success row to have exactly 14 fields.

- [ ] **Step 0d: Restore the reference query/cache allocator separation.**

The `baibaichen/ch-filecache` benchmark keeps query memory and CBI cache memory
independent. Port its `QueryBenchmarkBase.cpp` flags and initialization:

```text
cache_gb=32      CBI query allocator capacity; also enables CBI
cache_mem_gb=4   dedicated CBI MmapAllocator capacity
query_mem_gb=32  direct/FileCache query allocator capacity
```

`cache_mem_gb > 0` must create a dedicated `MmapAllocator` for
`AsyncDataCache`; it must not consume the query allocator selected by
`cache_gb`. When `cache_gb == 0`, `query_mem_gb` selects the same 32 GiB mmap
query allocator for direct and FileCache without constructing CBI. This is the
reviewed reference behavior, not a new benchmark policy.

Behavioral RED is the SF100 CBI evidence: q09/q21/q22 exhaust the legacy shared
4 GiB allocator; q21 still exhausts 8 and 16 GiB. The reference-separated
configuration must pass q09/q21/q22 with a 32 GiB query allocator and a
dedicated 4 GiB cache allocator. Record allocator capacities from the logs and
add a focused flag/configuration test if the mapping cannot otherwise be
asserted deterministically.

This TPCH A/B is Velox-native; it does not build, require, or run any Spark or
Gluten component.

**Authorization gate:** This entire section is post-checkpoint. Before the user
approves 018-P, do not copy the TPCH reference sources, add/build the TPCH
target, inspect `${TPCH_DATA}`, or run any TPCH command.

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
  MEMORY_ARGS=(--query_mem_gb=32)
  if [[ "$MODE" == "cbi" ]]; then
    MEMORY_ARGS+=(--cache_gb=32 --cache_mem_gb=4)
  else
    MEMORY_ARGS+=(--cache_gb=0)
  fi
  if ! /root/oss/velox/_build/relwithdebinfo/velox/benchmarks/tpch/velox_tpch_benchmark \
    --input_source=$MODE \
    --data_path="${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}" \
    --data_format=parquet \
    --query_id=1 \
    --rounds=1 \
    --num_splits_per_file=1 \
    --num_drivers=1 \
    "${MEMORY_ARGS[@]}" \
    --filecache_root=tmp/fc_tpch_$MODE \
    --filecache_disk_gib=10 \
    --out=tmp/tpch_q01_$MODE.csv \
    > /root/oss/velox/_build/relwithdebinfo/test_018c_$MODE.log 2>&1; then
    echo "FAIL: q01 $MODE exited nonzero"
    exit 1
  fi
done

EXPECTED_HEADER='round,query_id,wall_ms,rows,result_hash,bytes_read,hit_pct,cache_read_mib,predownload_mib,evict_mib,evict_count,op_p50_us,op_p95_us,error'
for CSV in tmp/tpch_q01_direct.csv tmp/tpch_q01_cbi.csv tmp/tpch_q01_filecache.csv; do
  test "$(head -n 1 "$CSV")" = "$EXPECTED_HEADER" || {
    echo "FAIL: unexpected CSV header in $CSV"
    exit 1
  }
  awk -F, 'NR==2 && NF!=14 { exit 1 }' "$CSV" || {
    echo "FAIL: expected exactly 14 fields in the success row of $CSV"
    exit 1
  }
done
```

- [ ] **Step 4: Verify row-count AND result-checksum correctness**

Row count alone is insufficient (two different result sets can share a
cardinality). Compare BOTH `rows` (CSV field 4) and `result_hash` (CSV field 5,
the commutative `hashValueAt` checksum added to the harness in 018-A):

```bash
check_row() { awk -F, -v r=2 'NR==r{print $4}' "$1"; }
check_hash() { awk -F, -v r=2 'NR==r{print $5}' "$1"; }
check_error() { awk -F, -v r=2 'NR==r{print $NF}' "$1"; }
ROWS_DIRECT=$(check_row tmp/tpch_q01_direct.csv); HASH_DIRECT=$(check_hash tmp/tpch_q01_direct.csv)
ROWS_CBI=$(check_row tmp/tpch_q01_cbi.csv);       HASH_CBI=$(check_hash tmp/tpch_q01_cbi.csv)
ROWS_FC=$(check_row tmp/tpch_q01_filecache.csv);  HASH_FC=$(check_hash tmp/tpch_q01_filecache.csv)
ERR_DIRECT=$(check_error tmp/tpch_q01_direct.csv)
ERR_CBI=$(check_error tmp/tpch_q01_cbi.csv)
ERR_FC=$(check_error tmp/tpch_q01_filecache.csv)
echo "rows: direct=$ROWS_DIRECT cbi=$ROWS_CBI fc=$ROWS_FC"
echo "hash: direct=$HASH_DIRECT cbi=$HASH_CBI fc=$HASH_FC"
if [ "$ROWS_DIRECT" = "$ROWS_CBI" ] && [ "$ROWS_DIRECT" = "$ROWS_FC" ] \
   && [ "$HASH_DIRECT" = "$HASH_CBI" ] && [ "$HASH_DIRECT" = "$HASH_FC" ] \
   && [ -n "$HASH_DIRECT" ] \
   && [ "$ROWS_DIRECT" -gt 0 ] \
   && [ -z "$ERR_DIRECT" ] && [ -z "$ERR_CBI" ] && [ -z "$ERR_FC" ]; then
  echo "PASS"
else
  echo "FAIL"
  exit 1
fi
```

Expected: PASS — identical row count AND identical result checksum across all
three modes for q01.

- [ ] **Step 5: Run all 22 queries correctness (rows + checksum)**

```bash
EXPECTED_HEADER='round,query_id,wall_ms,rows,result_hash,bytes_read,hit_pct,cache_read_mib,predownload_mib,evict_mib,evict_count,op_p50_us,op_p95_us,error'
for Q in $(seq 1 22); do
  for MODE in direct cbi filecache; do
    MEMORY_ARGS=(--query_mem_gb=32)
    if [[ "$MODE" == "cbi" ]]; then
      MEMORY_ARGS+=(--cache_gb=32 --cache_mem_gb=4)
    else
      MEMORY_ARGS+=(--cache_gb=0)
    fi
    if ! /root/oss/velox/_build/relwithdebinfo/velox/benchmarks/tpch/velox_tpch_benchmark \
      --input_source=$MODE \
      --data_path="${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}" \
      --data_format=parquet \
      --query_id=$Q \
      --rounds=1 \
      --num_splits_per_file=1 \
      --num_drivers=1 \
      "${MEMORY_ARGS[@]}" \
      --filecache_root=tmp/fc_tpch_correctness_$MODE \
      --filecache_disk_gib=10 \
      --out=tmp/tpch_correctness_q${Q}_$MODE.csv \
      > /root/oss/velox/_build/relwithdebinfo/test_018c_all_q${Q}_$MODE.log 2>&1; then
      echo "FAIL q$Q $MODE: nonzero exit"
      exit 1
    fi
  done
  for MODE in direct cbi filecache; do
    CSV=tmp/tpch_correctness_q${Q}_$MODE.csv
    test "$(head -n 1 "$CSV")" = "$EXPECTED_HEADER" || {
      echo "FAIL q$Q $MODE: unexpected CSV header"
      exit 1
    }
    awk -F, 'NR==2 && NF!=14 { exit 1 }' "$CSV" || {
      echo "FAIL q$Q $MODE: expected exactly 14 fields in the success row"
      exit 1
    }
  done
  ROWS_D=$(awk -F, 'NR==2{print $4}' tmp/tpch_correctness_q${Q}_direct.csv)
  ROWS_C=$(awk -F, 'NR==2{print $4}' tmp/tpch_correctness_q${Q}_cbi.csv)
  ROWS_F=$(awk -F, 'NR==2{print $4}' tmp/tpch_correctness_q${Q}_filecache.csv)
  HASH_D=$(awk -F, 'NR==2{print $5}' tmp/tpch_correctness_q${Q}_direct.csv)
  HASH_C=$(awk -F, 'NR==2{print $5}' tmp/tpch_correctness_q${Q}_cbi.csv)
  HASH_F=$(awk -F, 'NR==2{print $5}' tmp/tpch_correctness_q${Q}_filecache.csv)
  ERR_D=$(awk -F, 'NR==2{print $NF}' tmp/tpch_correctness_q${Q}_direct.csv)
  ERR_C=$(awk -F, 'NR==2{print $NF}' tmp/tpch_correctness_q${Q}_cbi.csv)
  ERR_F=$(awk -F, 'NR==2{print $NF}' tmp/tpch_correctness_q${Q}_filecache.csv)
  if [ "$ROWS_D" != "$ROWS_C" ] || [ "$ROWS_D" != "$ROWS_F" ] \
     || [ "$HASH_D" != "$HASH_C" ] || [ "$HASH_D" != "$HASH_F" ] \
     || [ -z "$HASH_D" ] \
     || [ -n "$ERR_D" ] || [ -n "$ERR_C" ] || [ -n "$ERR_F" ]; then
    echo "FAIL q$Q: rows direct=$ROWS_D cbi=$ROWS_C fc=$ROWS_F | hash direct=$HASH_D cbi=$HASH_C fc=$HASH_F | errors direct='$ERR_D' cbi='$ERR_C' fc='$ERR_F'"
    exit 1
  fi
  echo "q$Q PASS ($ROWS_D rows, hash=$HASH_D)"
done
```

**Gate:** every process exits zero, every `error` field is empty, and `rows` and
`result_hash` are identical across direct, cbi, and filecache for all 22 queries
(checksum comparison, not row count alone); q01 reports more than zero rows.

---

## Task 018-H2: TPCH Performance Wave (Baseline, No Threshold)

**Depends on:** 018-P approval and green 018-C.

**Files:**
- No new source files. Uses `velox_tpch_benchmark` from 018-C.
- Output: TPCH timing CSVs in `tmp/` and logs in the build directory.

**Interfaces:**
- Consumes: `velox_tpch_benchmark` (RelWithDebInfo/Release) and the accepted
  `velox/benchmarks/scripts/run_tpch_ab.sh` (018-P gated).
- Produces: baseline TPCH CSVs. No performance threshold.

**Authorization:** Forbidden before the user approves 018-P, and then only after
018-C is green. Every benchmark binary path is under a RelWithDebInfo or Release
build directory; a Debug binary invalidates the result.

### Wave 4: TPCH performance (smoke q01/q09/q21, then full 22)

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

For the full three-backend A/B sweep, use the accepted 018-D orchestrator; its
`TPCH_APPROVED=1` gate must already be satisfied by the 018-P approval, and it
sentinel-cleans the filecache disk root on exit:

```bash
TPCH_APPROVED=1 \
BIN=/root/oss/velox/_build/relwithdebinfo/velox/benchmarks/tpch/velox_tpch_benchmark \
TPCH_DATA="${TPCH_DATA:?set TPCH_DATA to the TPCH Parquet directory}" \
OUT_DIR="$(pwd)/tmp/tpch_ab_results" \
CACHE_ROOT="$(pwd)/tmp/velox_tpch_ab_cache" \
QUERY_ID=0 ROUNDS=3 NUM_SPLITS_PER_FILE=1 NUM_DRIVERS=4 \
FILECACHE_DISK_GIB=80 CACHE_GB=4 \
  bash velox/benchmarks/scripts/run_tpch_ab.sh \
  > /root/oss/velox/_build/relwithdebinfo/test_018h2_ab.log 2>&1
echo "TPCH A/B exit: $?"
```

### Steps

- [ ] **Step 1: Confirm 018-P approval and green 018-C**
- [ ] **Step 2: Build `velox_tpch_benchmark` in RelWithDebInfo (if not already built by 018-C)**
- [ ] **Step 3: Run Wave 4 smoke (q01, q09, q21)**
- [ ] **Step 4: Run Wave 4 full (all 22) if smoke passes**
- [ ] **Step 5: Collect CSVs, report median wall_ms per (mode, query, round)**

**Gate (018-H2):** 018-P approval present and 018-C green; every benchmark
binary path is under a RelWithDebInfo or Release build directory (any Debug
binary invalidates the result); all authorized TPCH waves exit 0, CSVs are
collected, and no exception or timeout occurs. No hard regression threshold is
applied; the noise band comes from within-run variance.

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
# FileCacheManager::Options must expose the three validated resource fields (018-A).
grep -q "localFileSystem" velox/ch/Interpreters/FileCache/FileCacheManager.h || { echo "BLOCKED: Options.localFileSystem missing"; exit 1; }
grep -q "timekeeper" velox/ch/Interpreters/FileCache/FileCacheManager.h || { echo "BLOCKED: Options.timekeeper missing"; exit 1; }
grep -q "commonUserId() const" velox/ch/Interpreters/FileCache/FileCacheManager.h || { echo "BLOCKED: FileCacheManager::commonUserId() missing"; exit 1; }

# 3. vcpkg toolchain
test -f "$CMAKE_TOOLCHAIN_FILE" || { echo "BLOCKED: CMAKE_TOOLCHAIN_FILE not set/found"; exit 1; }

# 4. TPCH data is checked only after 018-P approval.
# Do not inspect or require TPCH_DATA during the pre-checkpoint phase.
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

1. Full CTest of all `velox_ch_*` and benchmark harness test targets
   (`velox_cache_read_harness_test`, `velox_cache_verify_test`), freshly rebuilt.
2. Re-run the non-TPCH benchmark smokes (correctness verify, dedicated FCBI
   micro, wrapper A/B) from a RelWithDebInfo/Release build; confirm no exception
   and no Debug binary is used.
3. Read-only code-review subagent on the complete Velox diff.
4. Worker writes the result receipt. Worker does NOT commit.

---

## Explicit Exclusions

**Moved to Task 019 (Gluten integration + Spark E2E) — excluded entirely from
Task 018.** Task 019 owns the former 018-E/F/G work: the Gluten configuration
and `VeloxBackend`/`FileCacheManager` lifecycle (former 018-E), the
`GlutenBufferedInputBuilder` selection/identity/cancellation-token adapter
(former 018-F), and the `fileCacheWriteBytes` native -> JNI -> Java -> Scala
`SQLMetric` bridge with its Spark end-to-end verification (former 018-G). None
of that is built, tested, or depended on by Task 018.

```text
Task 017B (logging) — independent, executes after Task 018 and accepted Review 5, not a dependency
Review 5 — reviews Tasks 003-018 as a Velox-only FileCache system after Task 018 is accepted; does not review Task 019 integration
pageLoadTimeNs key mismatch — existing bug, out of scope
Hard performance regression thresholds — baseline only
Multi-cache configuration — single "default" only
Prometheus / HTTP metrics server — deferred
Kernel O_DIRECT integration — deferred
AsyncDataCache + FileCache co-existence — rejected by mutual exclusion
TestValue seam — not used unless already approved in Task 017A
```

---

## Task-Owned Files Summary

All files created or modified by this plan live in `/root/oss/velox/` (Velox
repo, branch `filecache`):

- `velox/benchmarks/AbBenchmark*.{h,cpp}` and `velox/benchmarks/CMakeLists.txt` (018-A adapter)
- `velox/benchmarks/QueryBenchmarkBase.cpp` (018-C reference query/cache allocator separation)
- `velox/dwio/common/benchmarks/**` correctness harness, wrapper benchmark, and tests (018-A)
- `velox/ch/benchmarks/FileCacheBufferedInputBenchmark.cpp` + `velox/ch/benchmarks/CMakeLists.txt` (018-B)
- `velox/benchmarks/scripts/{lib_cache_cleanup,run_wrapper_ab,run_tpch_ab}.sh` (018-D)
- `velox/benchmarks/tpch/**` (018-C, post-checkpoint)

No repository or worktree outside `/root/oss/velox` is touched.

---

## Result Receipt Location

```text
/root/oss/clickhouse/port/task/result/018-filecache-velox-benchmark-result.md
```

Worker never stages or commits. Controller commits accepted subtasks.
