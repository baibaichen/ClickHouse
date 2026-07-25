# Task 018S: FileCache TPCH `BufferedInput` Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.
>
> - **Disposition:** reviewed_executable — implementation_authorized
> - **Task ID:** 018S
> - **Binding design:**
>   `port/design/filecache-tpch-buffered-input-performance-investigation.md`
> - **Prerequisite:** Task 018R accepted
> - **Successor:** a result-specific one-variable mutation/corrective plan;
>   Task 017B remains paused
> - **Environment:** `root-oss`
> - **Velox baseline:** `filecache` at
>   `0c5b5918eb8374f39b09248be091da94bf4d72f0`
> - **Gluten dependency baseline:** local `fix/vcpkg-arrow-squashed` at
>   `c44409a7c3d8fab17ac5369b7cad8b3c80f5a437`; do not push
> - **Result:**
>   `port/task/result/018s-filecache-tpch-buffered-input-isolation-result.md`
> - **Plan review:**
>   `port/task/fullreview/root-oss/5/018s-implementation-plan-review.md`
> - **Implementation status:** AUTHORIZED by user on 2026-07-25 through the
>   mandatory one-driver checkpoint. Four-driver execution remains unauthorized
>   until the checkpoint is accepted and the user explicitly approves it.

**Goal:** Build and execute a reproducible one-driver-then-four-driver A/B/C
matrix that separates the cost of replacing `DirectBufferedInput` from the
incremental cost of the FileCache state/local-file path, without claiming a
root cause before a later one-variable mutation.

**Architecture:** Add a process-scoped C++ RAII benchmark/test override for
`filecache_passthrough` that keeps
`FileCacheBufferedInput`/`FileCacheInputStream` streaming behavior but reads the
source `ReadFile` without touching FileCache. The connector session property
controls metrics only and cannot select passthrough.
Collect low-perturbation process and `IoStatistics` query deltas in the existing
A/B CSV. Run focused TPCH queries through Direct, passthrough, and warm
FileCache at one driver, stop for a Controller/user checkpoint, then repeat at
four drivers and report the decomposition for user selection of the next
mutation.

**Tech Stack:** C++20, Velox `BufferedInput`, connector session properties,
`IoStatistics`, `getrusage`, GoogleTest, CMake/Ninja, Bash, Python 3 standard
library, TPCH SF100 Parquet, RelWithDebInfo.

## Global Constraints

```text
Binding design:
  port/design/filecache-tpch-buffered-input-performance-investigation.md

Primary comparison:
  FileCache versus Direct. CBI is out of scope for Task 018S.

Matrix:
  driver settings: 1, then 4
  A: DirectBufferedInput + source ReadFile
  B: FileCacheBufferedInput passthrough + source ReadFile
  C: FileCacheBufferedInput + warm FileCache

Focused queries:
  q09, q20, q17, q21, q04

Fixed data/settings:
  dataset=/root/oss/test-data/tpch-sf100-parquet-double
  num_splits_per_file=1
  reference_num_drivers=1
  query_mem_gb=32
  filecache_disk_gib=80
  build_type=RelWithDebInfo

Samples:
  five warm samples per driver/query/cell
  block 1 order A,B,C with three samples
  block 2 order C,B,A with two samples
  one process per sample; round 1 discarded, round 2 retained

Task 017B:
  authorized but paused until this investigation and the selected corrective
  are accepted.

No perf dependency.
No CBI run.
No full 22-query run.
No Task 017B implementation.
No Gluten source change, build, commit, or push.
No production fallback path.
No per-chunk logging.
No fine-grained lock timers in this task.
No root-cause claim from A/B/C timing alone.
No Ninja -j or nproc.
All C++ changes use Allman-style braces.
Worker never stages, commits, amends, rebases, pushes, or creates a PR.
Performance configure/build/test/query commands write unique logs under:
  <velox_perf_build_dir>
  (/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow)

TestValue-dependent behavioral build/test commands write unique logs under:
  <velox_build_dir>
  (/root/oss/velox/_build/debug)
```

`TestValue::set` and `TestValue::adjust` are compiled out under `NDEBUG`.
Therefore the five injection-dependent
`velox_ch_filecache_buffered_input_test` cases are Debug-only behavioral gates:

```text
DiskFailurePropagatesWithoutSkip
DiskFailureSkipContinuesAcrossSegments
CancellationDeferredUntilAfterSegmentWriteCompletes
CacheRenameOpenRaceRetries
CancellationDuringSegmentWaitThrows
```

The full binary must pass in Debug. RelWithDebInfo must build the binary and run
the other 34 release-safe cases with these five excluded. This is a
build-configuration split, not a skipped-test success claim: Debug supplies the
binding behavior evidence for all 39 cases.

The one-driver matrix is a mandatory checkpoint. The first Worker writes
`worker_status: waiting_for_four_driver_approval` and stops. Four-driver
execution requires a fresh Worker after Controller review and explicit user
approval.

---

## File Structure

### Velox files modified

| File | Responsibility |
|---|---|
| `velox/benchmarks/QueryBenchmarkBase.h` | Store connector session overrides used by every query task |
| `velox/benchmarks/QueryBenchmarkBase.cpp` | Apply connector session overrides in `CursorParameters::beforeTaskStart` |
| `velox/benchmarks/AbBenchmarkMain.h` | Declare input-source parsing and matrix cell enum |
| `velox/benchmarks/AbBenchmarkMain.cpp` | Select Direct/passthrough/FileCache and enable probe stats |
| `velox/benchmarks/AbBenchmarkBase.h` | Define process/runtime-stat snapshots and expanded CSV row |
| `velox/benchmarks/AbBenchmarkBase.cpp` | Collect `getrusage`, scan runtime stats, and serialize CSV |
| `velox/benchmarks/tests/AbBenchmarkSchemaTest.cpp` | TDD for source parsing, process deltas, runtime-stat aggregation, and CSV |
| `velox/connectors/hive/HiveConnectorUtil.h` | Declare the metrics session key and process-scoped passthrough RAII override |
| `velox/connectors/hive/HiveConnectorUtil.cpp` | Select passthrough only while the benchmark/test RAII override is live |
| `velox/connectors/hive/FileDataSource.h` | Declare runtime-stat keys exported from `IoStatistics` |
| `velox/connectors/hive/FileDataSource.cpp` | Export probe stats to `OperatorStats::runtimeStats` |
| `velox/common/io/IoStatistics.h` | Hold default-off relaxed probe counters |
| `velox/common/io/IoStatistics.cpp` | Implement probe enable/record/snapshot methods |
| `velox/dwio/common/DirectBufferedInput.cpp` | Record Direct enqueue facts when probe is enabled |
| `velox/dwio/common/DirectInputStream.cpp` | Record Direct `Next` and seek facts when probe is enabled |
| `velox/dwio/common/tests/DirectBufferedInputTest.cpp` | Prove planned and unplanned Direct streams export probe facts |
| `velox/ch/Common/FileCacheStats.h` | Name the passthrough source-byte runtime counter |
| `velox/ch/Disks/IO/FileCacheBufferedInput.h` | Add cache/passthrough mode and mode-preserving clone/accessors |
| `velox/ch/Disks/IO/FileCacheBufferedInput.cpp` | Permit null cache only in passthrough and record enqueue facts |
| `velox/ch/Disks/IO/FileCacheInputStream.h` | Declare passthrough chunk reader |
| `velox/ch/Disks/IO/FileCacheInputStream.cpp` | Read source directly in passthrough; record `Next`/seek/source facts |
| `velox/connectors/hive/tests/HiveFileCacheBufferedInputTest.cpp` | Prove selection, no FileCache touch, 1 MiB chunks, seek, and bytes |

### Velox files created

| File | Responsibility |
|---|---|
| `velox/benchmarks/scripts/run_tpch_buffered_input_matrix.sh` | Fail-close sequential 3+2 A/B/C runner |
| `velox/benchmarks/scripts/analyze_tpch_buffered_input_matrix.py` | Validate samples and produce block/pooled decomposition |
| `velox/benchmarks/scripts/tests/test_tpch_buffered_input_matrix.sh` | Shell dry-run, fake-binary, ordering, and cleanup tests |
| `velox/benchmarks/scripts/tests/test_analyze_tpch_buffered_input_matrix.py` | Python analyzer validity/summary tests |

### ClickHouse handoff file

| File | Responsibility |
|---|---|
| `port/task/result/018s-filecache-tpch-buffered-input-isolation-result.md` | Immutable Worker attempts, checkpoint, and Controller reviews |

No CMake source list changes are needed: all modified C++ files already belong
to existing targets, and scripts are invoked directly.

---

## Task 1: Add benchmark-only connector session propagation

**Files:**
- Modify: `/root/oss/velox/velox/benchmarks/QueryBenchmarkBase.h`
- Modify: `/root/oss/velox/velox/benchmarks/QueryBenchmarkBase.cpp`
- Modify: `/root/oss/velox/velox/benchmarks/AbBenchmarkMain.h`
- Modify: `/root/oss/velox/velox/benchmarks/AbBenchmarkMain.cpp`
- Modify: `/root/oss/velox/velox/benchmarks/tests/AbBenchmarkSchemaTest.cpp`
- Modify: `/root/oss/velox/velox/connectors/hive/HiveConnectorUtil.h`
- Modify: `/root/oss/velox/velox/connectors/hive/HiveConnectorUtil.cpp`

**Interfaces:**
- Produces:
  `QueryBenchmarkBase::setConnectorSessionProperty(connectorId, key, value)`
- Produces: `AbInputSource::{kDirect,kFileCachePassthrough,kFileCache,kCbi}`
- Produces: `parseAbInputSource(std::string_view) -> AbInputSource`
- Produces: process-scoped `ScopedFileCachePassthroughForBenchmark`
- Consumed by: Task 2 probe enablement, Task 3 passthrough selection, and every
  reference/timed query

- [ ] **Step 1: Add RED input-source parser tests**

In `AbBenchmarkSchemaTest.cpp`, add:

```cpp
TEST(AbBenchmarkMainTest, ParsesInputSources)
{
  EXPECT_EQ(parseAbInputSource("direct"), AbInputSource::kDirect);
  EXPECT_EQ(
      parseAbInputSource("filecache_passthrough"),
      AbInputSource::kFileCachePassthrough);
  EXPECT_EQ(parseAbInputSource("filecache"), AbInputSource::kFileCache);
  EXPECT_EQ(parseAbInputSource("cbi"), AbInputSource::kCbi);
  EXPECT_THROW(parseAbInputSource("other"), VeloxUserError);
}

TEST(AbBenchmarkMainTest, PassthroughOverrideIsScopedAndExclusive)
{
  EXPECT_FALSE(fileCachePassthroughForBenchmarkEnabled());
  {
    ScopedFileCachePassthroughForBenchmark guard;
    EXPECT_TRUE(fileCachePassthroughForBenchmarkEnabled());
    EXPECT_THROW(
        { ScopedFileCachePassthroughForBenchmark secondGuard; },
        VeloxException);
  }
  EXPECT_FALSE(fileCachePassthroughForBenchmarkEnabled());
}
```

Expected RED: compile failure because `AbInputSource` and
`parseAbInputSource` do not exist.

- [ ] **Step 2: Add RED connector-session propagation helper test**

Declare a pure helper:

```cpp
using ConnectorSessionProperties =
    std::unordered_map<
        std::string,
        std::unordered_map<std::string, std::string>>;

void applyConnectorSessionProperties(
    core::QueryCtx& queryCtx,
    const ConnectorSessionProperties& properties);
```

Add a test that creates:

```cpp
auto executor = std::make_shared<folly::CPUThreadPoolExecutor>(1);
memory::deprecatedDefaultMemoryManager();
auto queryCtx =
    core::QueryCtx::Builder()
        .executor(executor.get())
        .pool(memory::memoryManager()->addRootPool("018s-session-test"))
        .queryId("018s-session-test")
        .build();
```

Apply:

```cpp
ConnectorSessionProperties properties{
    {exec::test::kHiveConnectorId,
     {{"buffered_input_perf_probe", "true"}}}};
```

and assert:

```cpp
applyConnectorSessionProperties(*queryCtx, properties);
const auto* session =
    queryCtx->connectorSessionProperties(exec::test::kHiveConnectorId);
ASSERT_NE(session, nullptr);
EXPECT_TRUE(session->get<bool>("buffered_input_perf_probe", false));
```

- [ ] **Step 3: Run Task-1 RED**

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  export VELOX_DEPENDENCY_SOURCE=SYSTEM
  export Arrow_SOURCE=SYSTEM
  export simdjson_SOURCE=SYSTEM
  export GLUTEN_VCPKG_PREFER_CONFIG=OFF
  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    --target velox_ab_benchmark_schema_test
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_task1_red.log 2>&1
```

Expected: nonzero compile result naming the missing enum/helper.

- [ ] **Step 4: Implement the parser**

In `AbBenchmarkMain.h`:

```cpp
enum class AbInputSource : uint8_t
{
  kDirect,
  kFileCachePassthrough,
  kFileCache,
  kCbi,
};

AbInputSource parseAbInputSource(std::string_view inputSource);
```

In `AbBenchmarkMain.cpp`:

```cpp
AbInputSource parseAbInputSource(std::string_view inputSource)
{
  if (inputSource == "direct")
  {
    return AbInputSource::kDirect;
  }
  if (inputSource == "filecache_passthrough")
  {
    return AbInputSource::kFileCachePassthrough;
  }
  if (inputSource == "filecache")
  {
    return AbInputSource::kFileCache;
  }
  if (inputSource == "cbi")
  {
    return AbInputSource::kCbi;
  }
  VELOX_USER_FAIL(
      "Unknown --input_source: {} "
      "(expected direct, filecache_passthrough, filecache, or cbi)",
      inputSource);
}
```

- [ ] **Step 5: Implement connector session storage and application**

In `QueryBenchmarkBase.h`, add the alias, setter, and member:

```cpp
using ConnectorSessionProperties =
    std::unordered_map<
        std::string,
        std::unordered_map<std::string, std::string>>;

void setConnectorSessionProperty(
    std::string connectorId,
    std::string key,
    std::string value);

static void applyConnectorSessionProperties(
    core::QueryCtx& queryCtx,
    const ConnectorSessionProperties& properties);

protected:
  ConnectorSessionProperties connectorSessionProperties_;
```

In `QueryBenchmarkBase.cpp`:

```cpp
void QueryBenchmarkBase::setConnectorSessionProperty(
    std::string connectorId,
    std::string key,
    std::string value)
{
  connectorSessionProperties_[std::move(connectorId)][std::move(key)] =
      std::move(value);
}

void QueryBenchmarkBase::applyConnectorSessionProperties(
    core::QueryCtx& queryCtx,
    const ConnectorSessionProperties& properties)
{
  for (const auto& [connectorId, values] : properties)
  {
    auto valuesCopy = values;
    queryCtx.setConnectorSessionOverridesUnsafe(
        connectorId, std::move(valuesCopy));
  }
}
```

Before `readCursor(params, addSplits)`:

```cpp
if (!connectorSessionProperties_.empty())
{
  const auto properties = connectorSessionProperties_;
  params.beforeTaskStart = [properties](exec::Task& task)
  {
    QueryBenchmarkBase::applyConnectorSessionProperties(
        *task.queryCtx(), properties);
  };
}
```

This callback runs after the `Task` and `QueryCtx` exist and before drivers
start. It is copied for each reference and timed query; no mutable process
global is used.

- [ ] **Step 6: Wire input-source properties**

In `dispatchAbMain`, parse once:

```cpp
const auto inputSource = parseAbInputSource(FLAGS_input_source);
```

Declare:

```cpp
DEFINE_bool(
    buffered_input_perf_probe,
    false,
    "Collect Task-018S BufferedInput probe counters.");
```

When the flag is true, enable the probe session property:

```cpp
if (FLAGS_buffered_input_perf_probe)
{
  ab.setConnectorSessionProperty(
      exec::test::kHiveConnectorId,
      std::string(connector::hive::kBufferedInputPerfProbeSession),
      "true");
}
```

For passthrough add:

```cpp
if (inputSource == AbInputSource::kFileCachePassthrough)
{
  FLAGS_cache_gb = 0;
  passthroughOverride.emplace();
}
```

Declare before the mode branch:

```cpp
std::optional<
    connector::hive::ScopedFileCachePassthroughForBenchmark>
    passthroughOverride;
```

In `HiveConnectorUtil.h`, declare:

```cpp
class ScopedFileCachePassthroughForBenchmark
{
public:
  ScopedFileCachePassthroughForBenchmark();
  ~ScopedFileCachePassthroughForBenchmark();
  ScopedFileCachePassthroughForBenchmark(
      const ScopedFileCachePassthroughForBenchmark&) = delete;
  ScopedFileCachePassthroughForBenchmark& operator=(
      const ScopedFileCachePassthroughForBenchmark&) = delete;
};

bool fileCachePassthroughForBenchmarkEnabled();
```

In `HiveConnectorUtil.cpp`, back it with one `std::atomic_bool`. The constructor
changes false to true with compare-exchange/release and fails if another
override is active. The destructor stores false with release.
`fileCachePassthroughForBenchmarkEnabled` loads with acquire ordering.

The override object remains alive through `ab.initialize`, all reference/timed
queries, and `ab.shutdown`; it is destroyed on every normal/exceptional return
from `dispatchAbMain`.

Install and reset a real FileCache only for `kFileCache`. The passthrough mode
must follow the Direct cold-reset branch and must never call
`installFileCache`.

- [ ] **Step 7: Run Task-1 GREEN**

Build and run:

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    --target velox_ab_benchmark_schema_test
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_task1_green.log 2>&1

/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tests/velox_ab_benchmark_schema_test \
  --gtest_filter='AbBenchmarkMainTest.*:QueryBenchmarkBaseTest.*' \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_task1_green.log 2>&1
```

Expected: all selected tests pass.

---

## Task 2: Add default-off comparable `BufferedInput` probe statistics

**Files:**
- Modify: `/root/oss/velox/velox/common/io/IoStatistics.h`
- Modify: `/root/oss/velox/velox/common/io/IoStatistics.cpp`
- Modify: `/root/oss/velox/velox/connectors/hive/FileDataSource.h`
- Modify: `/root/oss/velox/velox/connectors/hive/FileDataSource.cpp`
- Modify: `/root/oss/velox/velox/connectors/hive/HiveConnectorUtil.h`
- Modify: `/root/oss/velox/velox/connectors/hive/HiveConnectorUtil.cpp`
- Modify: `/root/oss/velox/velox/dwio/common/DirectBufferedInput.cpp`
- Modify: `/root/oss/velox/velox/dwio/common/DirectInputStream.cpp`
- Modify: `/root/oss/velox/velox/dwio/common/tests/DirectBufferedInputTest.cpp`
- Modify: `/root/oss/velox/velox/benchmarks/tests/AbBenchmarkSchemaTest.cpp`

**Interfaces:**
- Produces: `io::BufferedInputProbeSnapshot`
- Produces:
  `IoStatistics::enableBufferedInputProbe`, `recordBufferedInputEnqueue`,
  `recordBufferedInputNext`, `recordBufferedInputSeek`,
  `bufferedInputProbeSnapshot`
- Produces connector session key `buffered_input_perf_probe`
- Produces TableScan runtime metrics consumed by Task 4

- [ ] **Step 1: Add RED snapshot tests**

In `AbBenchmarkSchemaTest.cpp`:

```cpp
TEST(BufferedInputProbeStatsTest, DisabledIsZeroCostAndEmpty)
{
  io::IoStatistics stats;
  stats.recordBufferedInputEnqueue(100);
  stats.recordBufferedInputNext(40);
  stats.recordBufferedInputSeek();
  EXPECT_EQ(stats.bufferedInputProbeSnapshot(), io::BufferedInputProbeSnapshot{});
}

TEST(BufferedInputProbeStatsTest, RecordsCountsBytesAndMaximum)
{
  io::IoStatistics stats;
  stats.enableBufferedInputProbe();
  stats.recordBufferedInputEnqueue(100);
  stats.recordBufferedInputEnqueue(60);
  stats.recordBufferedInputNext(32);
  stats.recordBufferedInputNext(80);
  stats.recordBufferedInputSeek();
  const auto snapshot = stats.bufferedInputProbeSnapshot();
  EXPECT_EQ(snapshot.enqueueCount, 2);
  EXPECT_EQ(snapshot.enqueueBytes, 160);
  EXPECT_EQ(snapshot.nextCount, 2);
  EXPECT_EQ(snapshot.returnedBytes, 112);
  EXPECT_EQ(snapshot.seekCount, 1);
  EXPECT_EQ(snapshot.maxChunkBytes, 80);
}

TEST(BufferedInputProbeStatsTest, MergePreservesProbeCounters)
{
  io::IoStatistics left;
  io::IoStatistics right;
  left.enableBufferedInputProbe();
  right.enableBufferedInputProbe();
  left.recordBufferedInputEnqueue(10);
  left.recordBufferedInputNext(16);
  right.recordBufferedInputEnqueue(64);
  right.recordBufferedInputNext(32);
  right.recordBufferedInputNext(80);
  right.recordBufferedInputSeek();
  left.merge(right);
  const auto snapshot = left.bufferedInputProbeSnapshot();
  EXPECT_EQ(snapshot.enqueueCount, 2);
  EXPECT_EQ(snapshot.enqueueBytes, 74);
  EXPECT_EQ(snapshot.nextCount, 3);
  EXPECT_EQ(snapshot.returnedBytes, 128);
  EXPECT_EQ(snapshot.seekCount, 1);
  EXPECT_EQ(snapshot.maxChunkBytes, 80);
}

TEST(BufferedInputProbeStatsTest, MergeEnablesDisabledDestination)
{
  io::IoStatistics left;
  io::IoStatistics right;
  right.enableBufferedInputProbe();
  right.recordBufferedInputEnqueue(64);
  right.recordBufferedInputNext(80);
  left.merge(right);
  const auto snapshot = left.bufferedInputProbeSnapshot();
  EXPECT_EQ(snapshot.enqueueCount, 1);
  EXPECT_EQ(snapshot.enqueueBytes, 64);
  EXPECT_EQ(snapshot.nextCount, 1);
  EXPECT_EQ(snapshot.returnedBytes, 80);
  EXPECT_EQ(snapshot.maxChunkBytes, 80);
}
```

Expected RED: missing type and methods.

- [ ] **Step 2: Implement the default-off probe**

In `IoStatistics.h`:

```cpp
struct BufferedInputProbeSnapshot
{
  uint64_t enqueueCount{0};
  uint64_t enqueueBytes{0};
  uint64_t nextCount{0};
  uint64_t returnedBytes{0};
  uint64_t seekCount{0};
  uint64_t maxChunkBytes{0};

  bool operator==(const BufferedInputProbeSnapshot&) const = default;
};
```

Add public methods:

```cpp
void enableBufferedInputProbe();
void recordBufferedInputEnqueue(uint64_t bytes);
void recordBufferedInputNext(uint64_t bytes);
void recordBufferedInputSeek();
BufferedInputProbeSnapshot bufferedInputProbeSnapshot() const;
```

Add private fields:

```cpp
std::atomic_bool bufferedInputProbeEnabled_{false};
std::atomic_uint64_t bufferedInputEnqueueCount_{0};
std::atomic_uint64_t bufferedInputEnqueueBytes_{0};
std::atomic_uint64_t bufferedInputNextCount_{0};
std::atomic_uint64_t bufferedInputReturnedBytes_{0};
std::atomic_uint64_t bufferedInputSeekCount_{0};
std::atomic_uint64_t bufferedInputMaxChunkBytes_{0};
```

`enableBufferedInputProbe` stores true with release ordering. Record methods
load the enabled flag with acquire ordering, then use relaxed counter atomics.
`recordBufferedInputNext` updates max with a relaxed CAS loop.

The disabled branch is the production default. It performs one predictable
atomic-boolean load and must not allocate, lock, or update a counter.

Extend `IoStatistics::merge`: when the source probe is enabled, enable the
destination, add every count/byte field, and retain the larger maximum chunk.
This is required because `FileDataSource::setFromDataSource` merges per-split
I/O ledgers. The merge test above is the binding proof.

- [ ] **Step 3: Export the probe through TableScan runtime stats**

In `FileDataSource.h`, add:

```cpp
static constexpr std::string_view kBufferedInputEnqueueCount{
    "bufferedInputEnqueueCount"};
static constexpr std::string_view kBufferedInputEnqueueBytes{
    "bufferedInputEnqueueBytes"};
static constexpr std::string_view kBufferedInputNextCount{
    "bufferedInputNextCount"};
static constexpr std::string_view kBufferedInputReturnedBytes{
    "bufferedInputReturnedBytes"};
static constexpr std::string_view kBufferedInputSeekCount{
    "bufferedInputSeekCount"};
static constexpr std::string_view kBufferedInputMaxChunkBytes{
    "bufferedInputMaxChunkBytes"};
```

At the end of `FileDataSource::getRuntimeStats`, read one snapshot and insert
nonzero `RuntimeMetric`s with `kNone` or `kBytes` units. Do not emit absent
metrics when the probe is disabled.

- [ ] **Step 4: Enable only through a connector session property**

In `HiveConnectorUtil.h`:

```cpp
inline constexpr std::string_view kBufferedInputPerfProbeSession =
    "buffered_input_perf_probe";
```

At the start of `createBufferedInput`:

```cpp
const auto* session = connectorQueryCtx->sessionProperties();
if (session->get<bool>(
        std::string(kBufferedInputPerfProbeSession), false))
{
  ioStatistics->enableBufferedInputProbe();
}
```

The session property is set by Task 1 for the benchmark only.

- [ ] **Step 5: Record Direct facts**

In `DirectBufferedInput::enqueue`, after validating the region:

```cpp
if (ioStatistics_)
{
ioStatistics_->recordBufferedInputEnqueue(region.length);
}
```

In `DirectInputStream::Next`, record only successful returned chunks:

```cpp
if (ioStats_)
{
  ioStats_->recordBufferedInputNext(static_cast<uint64_t>(*size));
}
```

In `DirectInputStream::seekToPosition`:

```cpp
if (ioStats_)
{
  ioStats_->recordBufferedInputSeek();
}
```

Use the existing `IoStatistics* ioStats_` member; do not introduce another
owner or global registry.

In `DirectBufferedInputTest.cpp`, enable the probe on the existing
`IoStatistics` fixture and cover both:

```text
planned:
  enqueue -> load -> Next -> seek
unplanned:
  DirectBufferedInput::read -> Next -> seek
```

Assert both paths increase `nextCount`, `returnedBytes`, `seekCount`, and
`maxChunkBytes`; only the planned path must increase `enqueueCount`. This
proves the actual Direct cell exports nonzero probe facts.

- [ ] **Step 6: Run Task-2 GREEN and disabled-path mutation**

Build and run the snapshot tests. Then mutate
`enableBufferedInputProbe` to leave `bufferedInputProbeEnabled_` false: the
enabled test must fail. Restore and rerun.

Exact GREEN commands:

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    --target velox_ab_benchmark_schema_test velox_dwio_common_test
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_task2_green.log 2>&1

/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tests/velox_ab_benchmark_schema_test \
  --gtest_filter='BufferedInputProbeStatsTest.*' \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_task2_green.log 2>&1

/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/dwio/common/tests/velox_dwio_common_test \
  --gtest_filter='DirectBufferedInputTest.*Probe*' \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_task2_direct_green.log 2>&1
```

For mutation and restore, rebuild both targets and rerun the same filters.
Final logs:

```text
build_018s_task2_green.log
test_018s_task2_green.log
test_018s_task2_direct_green.log
test_018s_task2_mutation.log
test_018s_task2_restore.log
```

All live under
`/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/`.

---

## Task 3: Implement `FileCacheBufferedInput` passthrough

**Files:**
- Modify: `/root/oss/velox/velox/ch/Common/FileCacheStats.h`
- Modify: `/root/oss/velox/velox/ch/Disks/IO/FileCacheBufferedInput.h`
- Modify: `/root/oss/velox/velox/ch/Disks/IO/FileCacheBufferedInput.cpp`
- Modify: `/root/oss/velox/velox/ch/Disks/IO/FileCacheInputStream.h`
- Modify: `/root/oss/velox/velox/ch/Disks/IO/FileCacheInputStream.cpp`
- Modify: `/root/oss/velox/velox/connectors/hive/HiveConnectorUtil.cpp`
- Modify: `/root/oss/velox/velox/connectors/hive/tests/HiveFileCacheBufferedInputTest.cpp`

**Interfaces:**
- Produces: `FileCacheBufferedInput::ReadMode::{kCache,kPassthrough}`
- Produces: actual B-cell source reads with no FileCache state/stat changes
- Produces runtime metric `fileCachePassthroughReadBytes`

- [ ] **Step 1: Add RED selection and fail-close tests**

Change the fixture member to a mutable config:

```cpp
config::ConfigBase sessionProperties_{
    std::unordered_map<std::string, std::string>{}, true};
```

Reset it in fixture `SetUp` with:

```cpp
sessionProperties_.reset();
```

Add:

```cpp
TEST_F(
    HiveFileCacheBufferedInputTest,
    PassthroughSelectsFileCacheBufferedInputWithoutManager)
{
  sessionProperties_
      .set(std::string(kBufferedInputPerfProbeSession), "true");
  ScopedFileCachePassthroughForBenchmark passthroughOverride;
  auto source = std::make_shared<InMemoryReadFile>(
      std::string(64, 'x'), "/source/passthrough.orc");
  auto handle = makeFileHandle(source);
  dwio::common::ReaderOptions readerOpts(pool_.get());
  auto queryCtx = makeQueryCtx();
  auto ioStats = std::make_shared<io::IoStatistics>();
  auto ioS = std::make_shared<velox::IoStats>();
  auto input = createBufferedInput(
      handle, readerOpts, queryCtx.get(), ioStats, ioS, executor_.get(), {});
  auto* fcbi = dynamic_cast<FileCacheBufferedInput*>(input.get());
  ASSERT_NE(fcbi, nullptr);
  EXPECT_EQ(fcbi->readMode(), FileCacheBufferedInput::ReadMode::kPassthrough);
}

TEST_F(
    HiveFileCacheBufferedInputTest,
    ProbePropertyAloneDoesNotSelectPassthrough)
{
  sessionProperties_.set(
      std::string(kBufferedInputPerfProbeSession), "true");
  auto source = std::make_shared<InMemoryReadFile>(
      std::string(64, 'x'), "/source/probe-only.orc");
  auto handle = makeFileHandle(source);
  dwio::common::ReaderOptions readerOpts(pool_.get());
  auto queryCtx = makeQueryCtx();
  auto ioStats = std::make_shared<io::IoStatistics>();
  auto ioS = std::make_shared<velox::IoStats>();
  auto input = createBufferedInput(
      handle, readerOpts, queryCtx.get(), ioStats, ioS, executor_.get(), {});
  EXPECT_NE(
      dynamic_cast<dwio::common::DirectBufferedInput*>(input.get()), nullptr);
  EXPECT_EQ(dynamic_cast<FileCacheBufferedInput*>(input.get()), nullptr);
}

TEST_F(
    HiveFileCacheBufferedInputTest,
    PassthroughRejectsInstalledFileCacheOrCbi)
{
  ScopedFileCachePassthroughForBenchmark passthroughOverride;
  auto source = std::make_shared<InMemoryReadFile>(
      std::string(64, 'x'), "/source/passthrough-invalid.orc");
  auto handle = makeFileHandle(source);
  dwio::common::ReaderOptions readerOpts(pool_.get());
  auto ioStats = std::make_shared<io::IoStatistics>();
  auto ioS = std::make_shared<velox::IoStats>();

  installManager();
  auto noCbiCtx = makeQueryCtx();
  EXPECT_THROW(
      createBufferedInput(
          handle,
          readerOpts,
          noCbiCtx.get(),
          ioStats,
          ioS,
          executor_.get(),
          {}),
      VeloxUserError);

  manager_->shutdown();
  FileCacheManager::setInstance(nullptr);
  manager_.reset();
  auto cbiCtx = makeQueryCtx(sharedAsyncCache_.get());
  EXPECT_THROW(
      createBufferedInput(
          handle,
          readerOpts,
          cbiCtx.get(),
          ioStats,
          ioS,
          executor_.get(),
          {}),
      VeloxUserError);
}
```

The second test must explicitly execute both invalid combinations and match
diagnostics containing `filecache_passthrough requires no FileCacheManager`
and `filecache_passthrough requires no AsyncDataCache`.

- [ ] **Step 2: Add RED streaming tests**

Use source data of `(2 << 20) + 17` bytes and assert:

```cpp
auto stream = input->read(
    0, sourceData.size(), dwio::common::LogType::STREAM);
ASSERT_TRUE(stream->Next(&data, &size));
EXPECT_EQ(size, 1 << 20);
ASSERT_TRUE(stream->Next(&data, &size));
EXPECT_EQ(size, 1 << 20);
ASSERT_TRUE(stream->Next(&data, &size));
EXPECT_EQ(size, 17);
EXPECT_FALSE(stream->Next(&data, &size));
EXPECT_EQ(readResult, sourceData);
```

Snapshot `takeFileCacheStatsSnapshot` before/after and assert every FileCache
counter delta is zero. Assert:

```cpp
EXPECT_EQ(source->preadBytes(), sourceData.size());
EXPECT_EQ(
    ioS->stats().at(ch::kFileCachePassthroughReadBytes).sum,
    sourceData.size());
const auto probe = ioStats->bufferedInputProbeSnapshot();
EXPECT_EQ(probe.nextCount, 3);
EXPECT_EQ(probe.returnedBytes, sourceData.size());
EXPECT_EQ(probe.maxChunkBytes, 1 << 20);
```

Add a seek test: read once, seek outside the current chunk to position
`(1 << 20) + 7`, then assert the remaining bytes equal
`sourceData.substr((1 << 20) + 7)` and `probe.seekCount == 1`.

Add a `BackUp` fast-return test:

```cpp
ASSERT_TRUE(stream->Next(&data, &size));
EXPECT_EQ(size, 1 << 20);
stream->BackUp(17);
ASSERT_TRUE(stream->Next(&data, &size));
EXPECT_EQ(size, 17);
const auto probe = ioStats->bufferedInputProbeSnapshot();
EXPECT_EQ(probe.nextCount, 2);
EXPECT_EQ(probe.returnedBytes, (1 << 20) + 17);
```

This proves both `FileCacheInputStream::Next` branches record successful
returns: remaining bytes after `BackUp`, and a newly-read chunk.

Expected RED: passthrough currently falls through to Direct and no passthrough
mode/metrics exist.

- [ ] **Step 3: Run Task-3 RED**

Build `velox_hive_filecache_buffered_input_test`; run only the new tests.
Expected: compile/test failure for missing mode/session behavior.

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    --target velox_hive_filecache_buffered_input_test
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_task3_red.log 2>&1
```

- [ ] **Step 4: Add the mode and constructor invariant**

In `FileCacheBufferedInput.h`:

```cpp
enum class ReadMode : uint8_t
{
  kCache,
  kPassthrough,
};
```

Append `ReadMode readMode = ReadMode::kCache` to the constructor. Add:

```cpp
ReadMode readMode() const
{
  return readMode_;
}

bool isPassthrough() const
{
  return readMode_ == ReadMode::kPassthrough;
}
```

Store `readMode_`. In the constructor:

```cpp
VELOX_CHECK_NOT_NULL(
    sourceReadFile_, "FileCacheBufferedInput requires a source ReadFile");
if (readMode_ == ReadMode::kCache)
{
  VELOX_CHECK_NOT_NULL(cache_, "cache mode requires a FileCache");
}
else
{
  VELOX_CHECK_NULL(cache_, "passthrough mode must not own a FileCache");
}
```

`clone` preserves `readMode_`. `isBuffered` returns false immediately in
passthrough. `fileCache` checks `kCache` before dereferencing.

- [ ] **Step 5: Select passthrough in Hive**

Before the FileCacheManager branch:

```cpp
if (fileCachePassthroughForBenchmarkEnabled())
{
  VELOX_USER_CHECK_NULL(
      ch::FileCacheManager::getInstance(),
      "filecache_passthrough requires no FileCacheManager");
  VELOX_USER_CHECK_NULL(
      connectorQueryCtx->cache(),
      "filecache_passthrough requires no AsyncDataCache");

  ch::FileCacheRequestContext requestContext
  {
      .queryId = connectorQueryCtx->queryId(),
      .userId = "benchmark-passthrough",
      .userWeight = 0,
      .cacheable = readerOpts.cacheable(),
      .segmentType = ch::FileSegmentKeyType::Data};

  return std::make_unique<ch::FileCacheBufferedInput>(
      fileHandle.file,
      nullptr,
      ch::FileCacheKey{},
      ch::FileCacheOriginInfo{},
      ch::FileCacheReadOptions{},
      std::move(requestContext),
      dwio::common::MetricsLog::voidLog(),
      std::move(ioStatistics),
      std::move(ioStats),
      executor,
      readerOpts,
      fileReadOps,
      connectorQueryCtx->cancellationToken(),
      ch::FileCacheBufferedInput::ReadMode::kPassthrough);
}
```

`FileCacheKey` and `FileCacheOriginInfo` are default-constructible at the
binding baseline. They remain unused in passthrough mode.

- [ ] **Step 6: Add the passthrough chunk path**

In `FileCacheInputStream` construction, acquire `queryContextHolder_` only for
cache mode.

Declare:

```cpp
size_t readNextPassthroughChunk();
```

At the top of `readNextChunk` after region-end validation:

```cpp
if (owner_->isPassthrough())
{
  return readNextPassthroughChunk();
}
```

`readNextPassthroughChunk` must:

1. allocate the same output buffer as cache mode;
2. create/reuse `ReadBufferFromVeloxReadFile` over `sourceReadFile`;
3. bound it to `region.offset + region.length`;
4. seek only when the reader is new or the requested absolute offset changed;
5. read at most `outputBufferCapacity_` (default 1 MiB);
6. clamp to the region end;
7. update `IoStatistics::read`, `incRawBytesRead`, and scan time;
8. add `kFileCachePassthroughReadBytes` to `IoStats`;
9. return the size to the common `Next` path for probe accounting;
10. publish the same region-relative output-buffer window used by cache mode.

It must not invoke any `ProfileEvents::CachedReadBuffer*`, FileCache snapshot,
FileSegment, priority, eviction, downloader, cache-reader, or cache-write path.

`seekToPosition` calls `recordBufferedInputSeek`; its existing fast/slow buffer
logic remains. On a slow passthrough seek, resetting `readInfo_.remoteReader`
forces the next chunk to seek the source reader to the new absolute position.

In `FileCacheBufferedInput::enqueue` and the common
`FileCacheInputStream::Next` path, call `recordBufferedInputEnqueue` for every
region. In `Next`, call `recordBufferedInputNext(avail)` before the early return
that serves bytes remaining after `BackUp`, and call
`recordBufferedInputNext(got)` after a successful new chunk. These calls apply
to both cache and passthrough modes, making B/C comparable.

Every call is null-safe:

```cpp
if (ioStatistics_)
{
  ioStatistics_->recordBufferedInputNext(got);
}
```

Likewise guard `recordBufferedInputEnqueue`, `recordBufferedInputSeek`, and the
passthrough `IoStats::addCounter` call. Existing unit fixtures are allowed to
construct streams with null statistics.

- [ ] **Step 7: Run Task-3 GREEN and mutation**

Run all `HiveFileCacheBufferedInputTest.*`. Then mutate the passthrough branch
to call `owner_->fileCache().getOrSet`; the no-FileCache-touch test must fail or
throw. Restore and rerun.

Final logs:

```text
build_018s_task3_green.log
test_018s_task3_green.log
test_018s_task3_mutation.log
test_018s_task3_restore.log
```

Exact GREEN command:

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    --target velox_hive_filecache_buffered_input_test
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_task3_green.log 2>&1

/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/connectors/hive/tests/velox_hive_filecache_buffered_input_test \
  --gtest_filter='HiveFileCacheBufferedInputTest.*' \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_task3_green.log 2>&1
```

For mutation and restore, rebuild the same target and rerun the same filter.

---

## Task 4: Expand the A/B CSV with process and scan I/O deltas

**Files:**
- Modify: `/root/oss/velox/velox/benchmarks/AbBenchmarkBase.h`
- Modify: `/root/oss/velox/velox/benchmarks/AbBenchmarkBase.cpp`
- Modify: `/root/oss/velox/velox/benchmarks/tests/AbBenchmarkSchemaTest.cpp`

**Interfaces:**
- Produces: `RusageDelta computeRusageDelta(const rusage&, const rusage&)`
- Produces: `ScanIoSnapshot collectScanIoStats(const exec::TaskStats&)`
- Produces exact 32-field investigation CSV
- Consumed by: Task 5 analyzer

- [ ] **Step 1: Add RED `getrusage` delta test**

```cpp
TEST(AbBenchmarkStatsTest, ComputesRusageDelta)
{
  rusage before{};
  before.ru_utime = {.tv_sec = 3, .tv_usec = 900000};
  before.ru_stime = {.tv_sec = 1, .tv_usec = 100000};
  before.ru_nvcsw = 10;
  before.ru_nivcsw = 20;

  rusage after{};
  after.ru_utime = {.tv_sec = 5, .tv_usec = 100000};
  after.ru_stime = {.tv_sec = 1, .tv_usec = 600000};
  after.ru_nvcsw = 14;
  after.ru_nivcsw = 27;

  const auto delta = computeRusageDelta(before, after);
  EXPECT_EQ(delta.userNanos, 1'200'000'000);
  EXPECT_EQ(delta.systemNanos, 500'000'000);
  EXPECT_EQ(delta.voluntaryCsw, 4);
  EXPECT_EQ(delta.involuntaryCsw, 7);
}
```

- [ ] **Step 2: Add RED scan-stat aggregation test**

Create `TaskStats` with two `TableScan` operators and one non-scan operator.
Populate `runtimeStats` with:

```cpp
storageReadBytes = RuntimeMetric(300, 3, 50, 150, kBytes)
localReadBytes = RuntimeMetric(500, 4, 80, 200, kBytes)
prefetchBytes = RuntimeMetric(100, 2, 40, 60, kBytes)
bufferedInputEnqueueCount = RuntimeMetric(5)
bufferedInputEnqueueBytes = RuntimeMetric(700, kBytes)
bufferedInputNextCount = RuntimeMetric(9)
bufferedInputReturnedBytes = RuntimeMetric(800, kBytes)
bufferedInputSeekCount = RuntimeMetric(2)
bufferedInputMaxChunkBytes = RuntimeMetric(128, kBytes)
fileCachePassthroughReadBytes = RuntimeMetric(600, kBytes)
```

Distribute values across both TableScan operators. Assert the helper merges
sums/counts and takes the maximum chunk while ignoring the non-scan operator.

- [ ] **Step 3: Define the exact CSV**

Keep the original 15 fields in their original order, then append:

```text
user_cpu_ms,system_cpu_ms,voluntary_csw,involuntary_csw,
storage_read_ops,storage_read_mib,local_read_ops,local_read_mib,
prefetch_ops,prefetch_mib,
enqueue_count,enqueue_mib,next_count,returned_mib,seek_count,max_chunk_kib,
passthrough_read_mib
```

This is 32 fields total. Update the header/row field-count tests to require 31
commas.

- [ ] **Step 4: Implement process and scan snapshots**

`computeRusageDelta` converts `timeval` to nanoseconds with checked,
nonnegative subtraction. Context-switch deltas are checked nonnegative.

`collectScanIoStats` walks every pipeline/operator, selects
`operatorType == "TableScan"`, and merges exact runtime-stat keys. Missing keys
contribute zero. A present key with the wrong `RuntimeCounter::Unit` is a
`VELOX_CHECK` failure, not a silent conversion.

Around only the timed `run` call:

```cpp
rusage usageBefore{};
rusage usageAfter{};
VELOX_CHECK_EQ(getrusage(RUSAGE_SELF, &usageBefore), 0);
const auto wallStart = std::chrono::steady_clock::now();
auto [cursor, results] = run(plans[i], queryConfigs_);
const auto wallEnd = std::chrono::steady_clock::now();
VELOX_CHECK_EQ(getrusage(RUSAGE_SELF, &usageAfter), 0);
```

Populate process deltas from `computeRusageDelta`. Populate scan deltas after
obtaining `cursor->task()->taskStats()`. Keep `bytes_read` as the existing
logical `TableScan::rawInputBytes` sum.

- [ ] **Step 5: Run Task-4 GREEN and aggregation mutation**

Run all schema tests. Mutate `collectScanIoStats` to stop after the first
TableScan operator; the two-scan test must fail. Restore and rerun.

Final logs:

```text
build_018s_task4_green.log
test_018s_task4_green.log
test_018s_task4_mutation.log
test_018s_task4_restore.log
```

Exact GREEN command:

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    --target velox_ab_benchmark_schema_test
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_task4_green.log 2>&1

/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tests/velox_ab_benchmark_schema_test \
  --gtest_filter='AbBenchmarkSchemaTest.*:AbBenchmarkStatsTest.*' \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_task4_green.log 2>&1
```

For mutation and restore, rebuild the target and rerun the same filter.

---

## Task 5: Add the fail-close focused matrix runner and analyzer

**Files:**
- Create:
  `/root/oss/velox/velox/benchmarks/scripts/run_tpch_buffered_input_matrix.sh`
- Create:
  `/root/oss/velox/velox/benchmarks/scripts/analyze_tpch_buffered_input_matrix.py`
- Create:
  `/root/oss/velox/velox/benchmarks/scripts/tests/test_tpch_buffered_input_matrix.sh`
- Create:
  `/root/oss/velox/velox/benchmarks/scripts/tests/test_analyze_tpch_buffered_input_matrix.py`

**Interfaces:**
- Consumes: 32-field CSV from Task 4
- Produces: raw sample tree, `run_manifest.json`, `summary.csv`,
  `order_block_summary.csv`, `validity.json`
- Produces nonzero exit for invalid/noisy matrix

- [ ] **Step 1: Write analyzer RED fixtures**

The Python test creates temporary sample trees for:

1. valid A/B/C with five warm rows per cell;
2. result mismatch;
3. nonempty error;
4. FileCache hit below 100%;
5. FileCache warm predownload;
6. FileCache warm eviction;
7. nonzero FileCache counters in A/B;
8. zero passthrough bytes in B;
9. nonzero passthrough bytes in A/C;
10. zero enqueue/`Next`/returned/max-chunk metrics in any A/B/C cell;
11. A or B has zero storage read operations/bytes;
12. C has zero local read operations/bytes;
13. opposite `B-A` signs between order blocks;
14. opposite `C-B` signs between order blocks;
15. missing sample/cell/query;
16. metadata driver/query/cell/input-source mismatch;
17. metadata block/sample/warm-round/probe-enabled mismatch;
18. wrong metadata `schema_version`;
19. mixed binary path/build ID, Velox HEAD, Gluten HEAD, ClickHouse HEAD, build
    type, Arrow library, dataset path, or fixed benchmark settings.

The test file ends with:

```python
if __name__ == "__main__":
    unittest.main()
```

Add separate fixtures for:

- `--smoke`: exactly one valid A/B/C sample passes; a missing cell fails;
- `--probe-validation`: forward and reverse probe-off/probe-on A/C samples with
  matching `C-A` signs pass; one sign flip fails.

Expected RED: analyzer module does not exist.

- [ ] **Step 2: Implement analyzer contracts**

Use only `argparse`, `csv`, `json`, `pathlib`, and `statistics`.

CLI:

```text
analyze_tpch_buffered_input_matrix.py
  --input-root <absolute path>
  --drivers <1|4>
  --queries 9,20,17,21,4
  [--smoke]
  [--probe-validation]
```

Require exactly:

```text
blocks:
  forward: A,B,C with samples 1,2,3
  reverse: C,B,A with samples 1,2
cells:
  A -> direct
  B -> filecache_passthrough
  C -> filecache
```

Each sample CSV must have exactly two rows and the exact Task-4 header. Retain
round 2 only.

Validity rules are the 19 fixtures above. For each driver/query/cell emit five
raw samples, block medians, pooled median, min, and max. Emit:

```text
b_minus_a_ms
b_minus_a_ratio
c_minus_b_ms
c_minus_b_ratio
c_minus_a_ms
c_minus_a_ratio
```

Do not emit `root_cause` or an automatic layer classification. Emit:

```text
decomposition_status: measured
next_plan_selection: user_review_required
```

The summary reports the observed per-query differences only. The Controller
and user select the one-variable mutation after reviewing both driver phases;
Task 018S does not invent a materiality threshold.

- [ ] **Step 3: Write shell runner RED**

The test supplies a fake executable that:

- records every argument vector;
- writes a valid two-row CSV to `--out`;
- emits cell-specific wall/metric values;
- refuses concurrent invocation using an exclusive lock file.

Assert:

```text
75 launches per driver:
  5 queries * 3 cells * 5 samples
first block per query:
  A,B,C repeated for sample slots 1..3
second block per query:
  C,B,A repeated for sample slots 1..2
every launch:
  rounds=2
  num_splits_per_file=1
  reference_num_drivers=1
  query_mem_gb=32
only C:
  filecache_root and filecache_disk_gib=80
```

Also assert an unauthenticated cache directory is never deleted and a
sentinel-authenticated child is removed by the shared cleanup helper
immediately after its sample subprocess returns.

Run the fake binary through normal, smoke, and probe-validation modes. Require
the exact launch counts and orders for each mode.

- [ ] **Step 4: Implement the shell runner**

Required environment:

```text
TPCH_APPROVED=1
BIN=<RelWithDebInfo/Release velox_tpch_benchmark>
TPCH_DATA=/root/oss/test-data/tpch-sf100-parquet-double
DRIVERS=1|4
OUT_ROOT=<absolute result root>
LOG_ROOT=<absolute build-dir log root>
```

Defaults:

```text
QUERIES=9,20,17,21,4
CACHE_ROOT=$OUT_ROOT/cache
QUERY_MEM_GB=32
FILECACHE_DISK_GIB=80
NUM_SPLITS_PER_FILE=1
REFERENCE_NUM_DRIVERS=1
```

Special modes:

```text
TPCH_SMOKE=1:
  requires one query
  runs one A/B/C sample
  invokes analyzer --smoke

PROBE_VALIDATION=1:
  requires the exact focused query list
  runs A/C with --buffered_input_perf_probe=false and true
  forward order: A_off,C_off,A_on,C_on
  reverse order: C_on,A_on,C_off,A_off
  collects one warm sample per order/configuration
  invokes analyzer --probe-validation
```

In probe-validation mode, probe-off rows must have zero probe-only fields and
probe-on rows must have positive enqueue/`Next`/returned/max-chunk fields.
Correctness and FileCache path gates apply to both.

Reject any other query list or driver count unless the task file is amended.
Validate `BIN` with `validate_benchmark_binary`.

Per sample paths:

```text
$OUT_ROOT/drivers_$DRIVERS/q<QQ>/<cell>/<block>/sample_<N>/result.csv
$OUT_ROOT/drivers_$DRIVERS/q<QQ>/<cell>/<block>/sample_<N>/meta.json
$LOG_ROOT/drivers_$DRIVERS/q<QQ>/<cell>/<block>/sample_<N>.log
```

Every `meta.json` has this exact schema:

```text
schema_version: 1
drivers: 1|4
query_id: q09|q20|q17|q21|q04
cell: A|B|C
input_source: direct|filecache_passthrough|filecache
block: forward|reverse
sample: integer
warm_round: 2
probe_enabled: true|false
binary_realpath: absolute path
binary_build_id: ELF build ID
velox_head: full SHA
gluten_head: full SHA
clickhouse_head: full SHA
cmake_build_type: RelWithDebInfo
arrow_lib: absolute vcpkg libarrow.a path
dataset_realpath: absolute path
num_splits_per_file: 1
reference_num_drivers: 1
query_mem_gb: 32
filecache_disk_gib: 80 for C, 0 for A/B
result_csv: absolute path
run_log: absolute path under LOG_ROOT
```

`run_manifest.json` repeats the invariant identity fields
(`schema_version`, binary path/build ID, three repository SHAs, build type,
Arrow path, dataset path, fixed settings, driver count, query list) and lists
every sample `meta.json` in launch order. The analyzer requires every sample
identity to equal the manifest and requires the CSV row's round/query to equal
its metadata. Identity mismatch is invalid evidence, not a warning.

For C only:

```text
$CACHE_ROOT/drivers_$DRIVERS/q<QQ>/<block>/sample_<N>
```

Execute each C sample in its own subshell. Inside that subshell, create and
register the sentinel immediately before the binary invocation. Its EXIT trap
must remove that one cache child before the runner starts the next sample.
Never use a glob or unresolved path in cleanup. After every C sample, assert
the cache child no longer exists.

Each binary command uses:

```text
--data_path=$TPCH_DATA
--data_format=parquet
--query_id=<query>
--rounds=2
--num_splits_per_file=1
--num_drivers=$DRIVERS
--reference_num_drivers=1
--query_mem_gb=32
--cache_gb=0
--out=<sample result.csv>
```

Cell input sources:

```text
A: --input_source=direct
B: --input_source=filecache_passthrough
C: --input_source=filecache
   --filecache_root=<sentinel child>
   --filecache_disk_gib=80
```

Every Task-018S matrix invocation also passes:

```text
--buffered_input_perf_probe=true
```

After all 75 launches, invoke the analyzer. A nonzero analyzer result makes the
runner nonzero while preserving all CSV/log/metadata artifacts.

The analyzer supports a separate `--smoke` mode. Smoke mode requires exactly
one A, one B, and one C sample for one query, applies all per-row/path validity
checks, and skips the normal 3+2 sample-count and order-block sign gates.

- [ ] **Step 5: Run Task-5 GREEN and false-green tests**

```bash
bash -n \
  /root/oss/velox/velox/benchmarks/scripts/run_tpch_buffered_input_matrix.sh \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_runner_syntax.log 2>&1

bash \
  /root/oss/velox/velox/benchmarks/scripts/tests/test_tpch_buffered_input_matrix.sh \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_runner_green.log 2>&1

python3 \
  /root/oss/velox/velox/benchmarks/scripts/tests/test_analyze_tpch_buffered_input_matrix.py \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_analyzer_green.log 2>&1
```

Mutate the analyzer to accept B with zero passthrough bytes; the corresponding
fixture must fail. Restore and rerun.

---

## Task 6: Build, regression-test, and run focused smoke gates

**Files:**
- All Task 018S Velox files
- Logs under:
  `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/`

**Interfaces:**
- Produces: reviewed, executable measurement harness before long runs

- [ ] **Step 1: Build all affected targets**

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  export VELOX_DEPENDENCY_SOURCE=SYSTEM
  export Arrow_SOURCE=SYSTEM
  export simdjson_SOURCE=SYSTEM
  export GLUTEN_VCPKG_PREFER_CONFIG=OFF
  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    --target \
      velox_ab_benchmark_schema_test \
      velox_dwio_common_test \
      velox_hive_filecache_buffered_input_test \
      velox_ch_filecache_buffered_input_test \
      velox_tpch_benchmark
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018s_all.log 2>&1

bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/debug \
    --target velox_ch_filecache_buffered_input_test
' > /root/oss/velox/_build/debug/build_018s_ch_fcbi_all.log 2>&1
```

- [ ] **Step 2: Run focused C++ tests**

Run the four test binaries to unique logs:

```bash
/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tests/velox_ab_benchmark_schema_test \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_schema_all.log 2>&1

/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/dwio/common/tests/velox_dwio_common_test \
  --gtest_filter='DirectBufferedInputTest.*' \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_direct_all.log 2>&1

/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/connectors/hive/tests/velox_hive_filecache_buffered_input_test \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_hive_fcbi_all.log 2>&1

/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/ch/Disks/IO/tests/velox_ch_filecache_buffered_input_test \
  --gtest_filter='-FileCacheBufferedInputTest.DiskFailurePropagatesWithoutSkip:FileCacheBufferedInputTest.DiskFailureSkipContinuesAcrossSegments:FileCacheBufferedInputTest.CancellationDeferredUntilAfterSegmentWriteCompletes:FileCacheBufferedInputTest.CacheRenameOpenRaceRetries:FileCacheBufferedInputTest.CancellationDuringSegmentWaitThrows' \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_ch_fcbi_release_safe.log 2>&1

/root/oss/velox/_build/debug/velox/ch/Disks/IO/tests/velox_ch_filecache_buffered_input_test \
  > /root/oss/velox/_build/debug/test_018s_ch_fcbi_all.log 2>&1
```

Expected:

```text
schema: all registered tests pass in RelWithDebInfo
DirectBufferedInput: all selected tests pass in RelWithDebInfo
Hive FCBI: all registered tests pass in RelWithDebInfo
CH FCBI release-safe: 34/34 pass in RelWithDebInfo
CH FCBI full behavior: 39/39 pass in Debug
```

The five named RelWithDebInfo exclusions cannot satisfy behavior evidence; only
their Debug passes do.

- [ ] **Step 3: Run q04 one-sample cell smoke**

Use the real runner with a task-only override:

```text
QUERIES=4
BLOCK1_SAMPLES=1
BLOCK2_SAMPLES=0
```

This override is allowed only under `TPCH_SMOKE=1`; normal execution rejects
it. The runner invokes the analyzer with `--smoke`. Run at one driver.

```bash
TPCH_APPROVED=1 \
TPCH_SMOKE=1 \
BIN=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tpch/velox_tpch_benchmark \
TPCH_DATA=/root/oss/test-data/tpch-sf100-parquet-double \
DRIVERS=1 \
QUERIES=4 \
OUT_ROOT=/root/oss/velox/tmp/tpch_buffered_input_smoke \
LOG_ROOT=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_smoke \
bash /root/oss/velox/velox/benchmarks/scripts/run_tpch_buffered_input_matrix.sh \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_q04_smoke.log 2>&1
```

Require:

```text
A: result_match=1; FileCache and passthrough metrics zero
B: result_match=1; passthrough_read_mib>0; FileCache metrics zero
C: result_match=1; round-2 hit_pct=100; no predownload/eviction
all: enqueue_count>0, next_count>0, returned_mib>0, max_chunk_kib>0
A/B: storage_read_ops>0 and storage_read_mib>0
C: local_read_ops>0 and local_read_mib>0
```

- [ ] **Step 4: Prove instrumentation does not reverse A/C direction**

Add a runner mode `PROBE_VALIDATION=1`. At one driver it runs all focused
queries through A/C with the probe off and on in both forward and reverse
orders:

```text
--buffered_input_perf_probe=true|false
```

The gflag is declared in Task 1 and defaults false. It only controls the
connector session property and is not read by Hive production code.

For every focused query, the sign of `C-A` must match between probe-off and
probe-on in both execution orders. If any query flips, Task 018S is blocked and
the probe stats must be reduced before the one-driver matrix.

```bash
TPCH_APPROVED=1 \
PROBE_VALIDATION=1 \
BIN=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tpch/velox_tpch_benchmark \
TPCH_DATA=/root/oss/test-data/tpch-sf100-parquet-double \
DRIVERS=1 \
QUERIES=9,20,17,21,4 \
OUT_ROOT=/root/oss/velox/tmp/tpch_buffered_input_probe_1driver \
LOG_ROOT=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_probe_1driver \
bash /root/oss/velox/velox/benchmarks/scripts/run_tpch_buffered_input_matrix.sh \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_probe_1driver.log 2>&1
```

- [ ] **Step 5: Independent read-only review**

Launch exactly one review subagent over the complete Task-018S diff. Give it:

- binding design;
- this task;
- C++ and script tests;
- q04 smoke outputs;
- instrumentation on/off evidence.

Resolve all Critical/Important findings and rerun affected gates. Worker leaves
all changes unstaged.

---

## Task 7: Run the one-driver matrix and stop

**Files:**
- Output:
  `/root/oss/velox/tmp/tpch_buffered_input_matrix_1driver/`
- Logs:
  `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_matrix_1driver/`
- Result receipt:
  `/root/oss/clickhouse/port/task/result/018s-filecache-tpch-buffered-input-isolation-result.md`

- [ ] **Step 1: Capture run identity**

Record:

```text
Velox branch/HEAD/diff
Gluten branch/HEAD (local; no push)
benchmark path
ELF build ID
CMake build type
CMake Arrow source/path
dataset realpath
driver count
focused query list
host/kernel/container identity
```

- [ ] **Step 2: Run one-driver matrix**

```bash
TPCH_APPROVED=1 \
BIN=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tpch/velox_tpch_benchmark \
TPCH_DATA=/root/oss/test-data/tpch-sf100-parquet-double \
DRIVERS=1 \
QUERIES=9,20,17,21,4 \
OUT_ROOT=/root/oss/velox/tmp/tpch_buffered_input_matrix_1driver \
LOG_ROOT=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_matrix_1driver \
bash /root/oss/velox/velox/benchmarks/scripts/run_tpch_buffered_input_matrix.sh \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_matrix_1driver.log 2>&1
```

Expected: exit 0, 75 valid warm samples, and generated summaries.

- [ ] **Step 3: Apply one-driver validity gates**

Require:

```text
25 warm samples per cell
5 warm samples per query/cell
all result_match=1
all errors empty
B passthrough_read_mib>0 and FileCache metrics zero
C hit_pct=100 and predownload/eviction zero
forward/reverse B-A signs agree per query
forward/reverse C-B signs agree per query
```

If any gate fails, preserve artifacts, write `worker_status: blocked`, identify
the exact driver/query/cell/block/sample, and stop. Do not run four drivers.

- [ ] **Step 4: Write checkpoint receipt**

Append Worker attempt 1 with:

```text
worker_status: waiting_for_four_driver_approval
environment_profile: root-oss
task: 018S
phase: one_driver
one_driver_validity: passed
four_driver_executed: false
root_cause_claimed: false
```

Include raw and summary artifact paths, every command/log, test counts, review
findings/resolutions, and pooled/order-block A/B/C differences. Do not select a
mutation layer at this checkpoint. Stop immediately.

---

## Task 8: Controller checkpoint and four-driver approval

The Controller:

1. independently reviews the complete diff and all Task-1 through Task-6 logs;
2. verifies all 75 one-driver samples and summary calculations;
3. rejects any hidden mode fallback or missing actual-path metric;
4. appends:

```text
controller_status: one_driver_checkpoint_accepted
four_driver_authorized: false
```

5. asks the user to review the one-driver decomposition;
6. after explicit approval, appends:

```text
four_driver_authorized: true
```

No implementation commit is created at the checkpoint.

---

## Task 9: Run the four-driver matrix

A fresh Worker reads the checkpoint and verifies
`four_driver_authorized: true`.

- [ ] **Step 1: Verify unchanged implementation**

Require the same Velox HEAD and exact unstaged diff as the one-driver
checkpoint. If any task-owned source changed, stop for Controller review.

- [ ] **Step 2: Re-run probe-overhead validation at four drivers**

Run `PROBE_VALIDATION=1` for all five focused queries at four drivers, with the
same forward/reverse probe-off/probe-on A/C ordering used at one driver. Every
query must preserve the `C-A` sign in both orders. Stop as blocked on any flip.

```bash
TPCH_APPROVED=1 \
PROBE_VALIDATION=1 \
BIN=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tpch/velox_tpch_benchmark \
TPCH_DATA=/root/oss/test-data/tpch-sf100-parquet-double \
DRIVERS=4 \
QUERIES=9,20,17,21,4 \
OUT_ROOT=/root/oss/velox/tmp/tpch_buffered_input_probe_4driver \
LOG_ROOT=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_probe_4driver \
bash /root/oss/velox/velox/benchmarks/scripts/run_tpch_buffered_input_matrix.sh \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_018s_probe_4driver.log 2>&1
```

- [ ] **Step 3: Run four-driver matrix**

Use the Task-7 command with:

```text
DRIVERS=4
OUT_ROOT=/root/oss/velox/tmp/tpch_buffered_input_matrix_4driver
LOG_ROOT=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/tpch_buffered_input_matrix_4driver
top-level log:
  test_018s_matrix_4driver.log
```

- [ ] **Step 4: Apply the same validity gates**

Require 75 valid four-driver warm samples and the same correctness/path/order
gates. Invalid/noisy results are `blocked`; never average them into a
decomposition.

- [ ] **Step 5: Produce cross-driver decomposition**

For each query report:

```text
one driver:
  A, B, C, B-A, C-B, C-A
four drivers:
  A, B, C, B-A, C-B, C-A
driver interaction:
  change in B-A ratio
  change in C-B ratio
```

Do not classify a layer automatically and do not state a concrete root cause.
Report all per-query and cross-driver differences. The Controller and user
select the next one-variable mutation after reviewing the evidence.

- [ ] **Step 6: Final review and receipt**

Launch one read-only evidence review for Worker attempt 2. Append:

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 018S
phase: four_driver_complete
one_driver_validity: passed
four_driver_validity: passed
root_cause_claimed: false
recommended_next_task:
  user-selected one-variable mutation plan based on Task-018S evidence
```

Then stop. Do not run all 22 queries, implement a mutation, start Task 017B,
stage, commit, or push.

---

## Task 10: Controller acceptance

The Controller independently:

1. reads every Worker attempt/checkpoint/review;
2. verifies exact source scope and default-off production behavior;
3. recomputes all one/four-driver medians/ranges/differences from raw CSVs;
4. verifies order-block sign gates;
5. verifies actual B-path and C-warm metrics;
6. confirms no root-cause or final-overhead claim exceeds evidence;
7. appends `accepted` or `changes_requested`;
8. if accepted, commits only Task-018S Velox implementation files with a
   `Task 018S:` subject;
9. records the Velox implementation SHA in the receipt;
10. commits the receipt/state update separately in ClickHouse;
11. writes a new design amendment and plan for the selected one-variable
    mutation;
12. keeps Task 017B paused.

Task 018S acceptance authorizes only the next mutation plan. It does not
authorize a production performance fix or full 22-query rerun.
