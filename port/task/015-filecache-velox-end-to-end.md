# Task 015: `FileCache` Velox End-to-End Validation and Basic Benchmark

> **MVP task.**
>
> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> creates test and benchmark files in the Velox checkout. It writes one result
> file under this ClickHouse checkout. Do not
> modify ClickHouse source files. Do not commit or stage either repository.
>
> Production changes are permitted **only** when a concrete integration defect
> is found during testing. Each such defect must be described in the result
> file before the fix is applied.

## Goal

Validate the complete `FileCacheBufferedInput` → `FileCacheInputStream` →
`FileCache` integration end-to-end. Cover all required read scenarios:

```text
miss → fill → hit
bypass / cache-only
BackUp within output buffer
SkipInt64 across segment boundary
seekToPosition region-relative coordinates
non-zero region.offset absolute coordinate mapping
discarded enqueue before load (no use-after-free)
load is a no-op planning barrier (does not dereference stream)
DWRF stripe-metadata path (shouldPrefetchStripes = false)
path-only key when etag is empty
different non-empty etags → different keys
manager shutdown while stream is alive but not actively reading
```

### ClickHouse integration-test migration ownership

Audit the applicable scenarios in:

```text
tests/integration/test_filesystem_cache/test.py
tests/integration/test_cache_bypass_on_disk_failure/test.py
tests/queries/0_stateless/*filesystem_cache*
```

Port selected end-to-end behavior, not ClickHouse server/configuration plumbing.
The Velox E2E fixture must cover at least:

```text
cold miss -> cache fill -> later hit
partial segment continuation across readers
cache write failure -> configured bypass or propagated failure
truncated/invalid cached data -> source recovery without stale reader state
reserve-ahead/downloaded-size accounting at the public FileCache boundary
random seeks across hit/miss/bypass paths
```

Record a migration matrix in the Task-015 receipt: original CH test/scenario,
Velox test name, and any explicit exclusion. Task 012 remains the owner of
focused `FileSegment` resume/reconciliation UTs, and Task 014 remains the owner
of focused reader/handoff tests. Task 015 must exercise those behaviors through
the assembled public path without duplicating their internal test logic.

Deliverables:
- Velox focused test binary `velox_ch_filecache_e2e_test`.
- Velox benchmark binary `velox_ch_filecache_seek_benchmark`.
- All test scenarios pass; benchmark builds and runs without crash.

## Starting point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected HEAD:    descendant of the task-014 result commit
```

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/3-consumers/03-filecache-buffered-input-design.md
<clickhouse_repo>/port/3-consumers/01-filecache-read-context-design.md
<clickhouse_repo>/port/task/result/014-filecache-buffered-input-result.md
```

## File scope

Create in the Velox checkout:

```text
<velox_repo>/velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
<velox_repo>/velox/ch/benchmarks/CMakeLists.txt
<velox_repo>/velox/ch/benchmarks/FileCacheSeekBenchmark.cpp
```

Modify in the Velox checkout:

```text
<velox_repo>/velox/ch/CMakeLists.txt
<velox_repo>/velox/ch/Disks/IO/CMakeLists.txt
<velox_repo>/velox/ch/Disks/IO/tests/CMakeLists.txt
```

Create in the ClickHouse checkout:

```text
<clickhouse_repo>/port/task/result/015-filecache-velox-e2e-result.md
```

Every new Velox C++ file must begin with the Apache 2.0 Facebook license
header from `port/task/003-filecache-basic-common-shims.md`.

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the baselines**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected: Velox on `filecache` after Task 014.
Record all pre-existing dirty files in the result file.

- [ ] **Step 2: Create the `velox/ch/Disks/IO/tests` directory and skeleton CMakeLists.txt**

If `velox/ch/Disks/IO/CMakeLists.txt` does not already include a
`tests` subdirectory, append to it:

```cmake
if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Append the E2E target to the existing
`velox/ch/Disks/IO/tests/CMakeLists.txt` created by Task 014:

```cmake
add_executable(velox_ch_filecache_e2e_test FileCacheE2ETest.cpp)
add_test(velox_ch_filecache_e2e_test velox_ch_filecache_e2e_test)

target_link_libraries(
  velox_ch_filecache_e2e_test
  PRIVATE
    velox_ch_filecache_dwio
    velox_ch_filecache_manager
    velox_ch_filecache_core
    velox_ch_filecache
    velox_dwio_common
    velox_hive_connector
    velox_test_util
    velox_exception
    velox_memory
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

If `velox/ch/CMakeLists.txt` does not yet include the `Disks` subdirectory,
append:

```cmake
add_subdirectory(Disks)
```

and ensure `velox/ch/Disks/CMakeLists.txt` contains:

```cmake
add_subdirectory(IO)
```

- [ ] **Step 3: Create the complete Velox E2E test file**

Create `velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp` with the repository
license, the production headers, the fixture from Step 4, and every test listed
in that step. Do not add `GTEST_SKIP`, empty bodies, fake cache classes, or
compile-only assertions. The first build is expected to expose real
API/integration errors.

- [ ] **Step 4: Complete the Velox E2E fixture and test cases**

Use the following fixture to set up a per-test `FileCacheManager` and an in-memory
`ReadFile` backed by a fixed byte pattern:

```cpp
class FileCacheE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        filesystems::registerLocalFileSystem();
        tempDir_ = common::testutil::TempDirectoryPath::create();
        pool_ = memory::memoryManager()->addLeafPool("e2e").get();
        auto opts = makeOptions(tempDir_->getPath(), pool_);
        manager_ = FileCacheManager::create(std::move(opts));
        FileCacheManager::setInstance(manager_.get());
        cache_ = manager_->getDefault();
        ASSERT_NE(cache_, nullptr);
    }

    void TearDown() override {
        if (FileCacheManager::getInstance()) {
            FileCacheManager::getInstance()->shutdown();
            FileCacheManager::setInstance(nullptr);
        }
        manager_.reset();
    }

    // Create a ReadFile backed by `data` (copied, not referenced).
    std::shared_ptr<ReadFile> makeMemoryReadFile(std::vector<char> data);

    // Create a FileCacheBufferedInput for the given ReadFile.
    std::unique_ptr<FileCacheBufferedInput> makeInput(
        std::shared_ptr<ReadFile> file,
        FileCacheKey key);

    // Read all bytes from a FileCacheInputStream via Next().
    std::vector<char> readAll(SeekableInputStream& stream);

    std::shared_ptr<common::testutil::TempDirectoryPath> tempDir_;
    memory::MemoryPool* pool_{nullptr};
    std::shared_ptr<FileCacheManager> manager_;
    FileCachePtr cache_;
};
```

Implement each test case with concrete assertions. The key behavioral
contracts to verify per test:

**`MissFillHit`:**
1. Write a 256 KiB byte pattern to a `MemoryReadFile`.
2. `enqueue` a region covering 0..128 KiB.
3. Call `Next()` to exhaustion; assert bytes match the pattern.
4. `isBuffered(0, 128*1024)` on a freshly constructed `FileCacheBufferedInput`
   for the same key returns `true`.
5. `Next()` on the second stream reads the same bytes without remote I/O
   (assert `MemoryReadFile::preadv` call count does not increase).

**`CacheOnlyMissFails`:**
1. Set `FileCacheReadOptions::tempCacheOnly = true`.
2. Attempt to read a key that has never been cached.
3. Assert `Next()` throws `VeloxRuntimeError`.

**`ReadIfExistsBypassMode`:**
1. Set `readIfExistsOtherwiseBypass = true`.
2. Read an uncached key; assert bytes equal the source data (bypass path).
3. No new `FileSegment` must remain in the cache after the read completes.

**`BackUpWithinOutputBuffer`:**
1. Read 64 KiB via `Next()`.
2. Call `BackUp(1024)`.
3. `ByteCount()` must equal `64 * 1024 - 1024`.
4. Re-reading those 1024 bytes via `Next()` must return the same bytes.

**`SkipAcrossSegmentBoundary`:**
1. Enqueue a 512 KiB region; `Next()` to consume the first segment.
2. `SkipInt64` to skip past the first segment boundary.
3. `Next()` must return data starting from the correct absolute offset.

**`SeekToPositionRegionRelative`:**
1. Enqueue a region starting at absolute offset 4096.
2. Seek to region-relative position 256.
3. `Next()` must return data starting at absolute file offset 4096 + 256.
4. `ByteCount()` must equal 256 after seek before any further `Next()`.

**`NonzeroRegionOffsetAbsoluteCoordinates`:**
1. Enqueue region `{offset=65536, length=32768}`.
2. Read all bytes via `Next()`.
3. Assert that file bytes [65536, 98303] are returned, not [0, 32767].

**`DiscardedEnqueueNoUseAfterFree`:**
1. `enqueue` a region; immediately discard the returned
   `unique_ptr<SeekableInputStream>` without calling `Next()`.
2. Call `load()`.
3. Verify no crash and no sanitizer error.

**`LoadIsNopPlanningBarrier`:**
1. `enqueue` three regions.
2. Discard all three returned streams.
3. Call `load()`.
4. Verify no crash (load must not dereference any stream pointer).

**`DWRFShouldPrefetchStripesIsFalse`:**
1. Construct a `FileCacheBufferedInput`.
2. Assert `shouldPrefetchStripes()` returns `false`.
3. Assert `preloaded()` returns `false`.

**`PathOnlyKeyWhenEtagEmpty`:**
1. Derive a key via `FileCacheKey::fromPath(path)`.
2. Read through `FileCacheBufferedInput` using that key.
3. Re-derive the key with the same path; assert cache hit.

**`DifferentEtagsDifferentKeys`:**
1. Create two `FileCacheBufferedInput` instances for the same path but etags
   `"v1"` and `"v2"`, using `FileCacheKey::fromKey(sipHash128(path+etag))`.
2. Fill both caches.
3. Assert the two keys compare not-equal.
4. Assert each stream hits its own segment, not the other's.

**`ShutdownWhileStreamAliveNotReading`:**
1. `enqueue` a region; hold the stream alive.
2. Call `FileCacheManager::shutdown()` without the stream calling `Next()`.
3. Assert shutdown completes without deadlock or crash.
4. The held stream must be allowed to destruct without further reads.

- [ ] **Step 5: Build and run the Velox E2E tests**

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_e2e_test \
  > <velox_build_dir>/build_015_e2e.log 2>&1
echo "exit: $?"

ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_filecache_e2e_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_015_e2e.log 2>&1
echo "exit: $?"
```

Expected:

```text
Build exit code: 0.
100% tests passed, 0 tests failed.
```

- [ ] **Step 6: Add the Velox random-seek benchmark**

Create the benchmark directory and CMakeLists:

```cmake
# velox/ch/benchmarks/CMakeLists.txt
# Copyright (c) Facebook, Inc. and its affiliates.
# Licensed under the Apache License, Version 2.0 ...

add_executable(velox_ch_filecache_seek_benchmark FileCacheSeekBenchmark.cpp)

target_link_libraries(
  velox_ch_filecache_seek_benchmark
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
    gflags::gflags
)
```

Append to `velox/ch/CMakeLists.txt` (inside an `if(VELOX_ENABLE_BENCHMARKS)` guard):

```cmake
if(${VELOX_ENABLE_BENCHMARKS})
  add_subdirectory(benchmarks)
endif()
```

Create `velox/ch/benchmarks/FileCacheSeekBenchmark.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

// Benchmark: random seekToPosition access patterns via FileCacheInputStream.
//
// Usage:
//   velox_ch_filecache_seek_benchmark \
//     --bm_min_iters=10 \
//     --file_size_mb=256 \
//     --cache_dir=/tmp/fc_bench \
//     --cache_size_mb=512
//
// Metrics emitted (per iteration):
//   seek_cache_hit_ns   — seek + one Next() when segment is already DOWNLOADED
//   seek_cache_miss_ns  — seek + one Next() on first access (miss → fill)
//   seek_bypass_ns      — seek + one Next() with readIfExistsOtherwiseBypass

#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/time/Timer.h"
#include "velox/dwio/common/ReaderOptions.h"

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>

DEFINE_int32(file_size_mb, 64, "Synthetic file size in MiB");
DEFINE_int32(cache_size_mb, 128, "FileCache max size in MiB");
DEFINE_string(cache_dir, "/tmp/fc_seek_bench", "FileCache directory path");
DEFINE_int32(seek_count, 1000, "Number of random seeks per benchmark run");

namespace facebook::velox::ch
{
namespace
{

struct BenchmarkFixture;

static std::unique_ptr<BenchmarkFixture> gFixture;

struct BenchmarkFixture
{
    std::shared_ptr<FileCacheManager> manager;
    FileCachePtr cache;
    std::shared_ptr<ReadFile> sourceFile;
    FileCacheKey cacheKey;
    std::vector<uint64_t> seekOffsets; // pre-generated random offsets
    memory::MemoryPool* pool;
};

void setupFixture();

} // namespace
} // namespace facebook::velox::ch

BENCHMARK(FileCacheSeekCacheHit) {
    facebook::velox::ch::setupFixture();
    // Warm up: fill all segments referenced by seekOffsets.
    // Then benchmark seeks on already-cached regions.
    // Exact implementation fills gFixture->cache then measures Next() latency.
    folly::doNotOptimizeAway(gFixture.get());
}

BENCHMARK(FileCacheSeekCacheMiss) {
    facebook::velox::ch::setupFixture();
    // Clear cache between iterations; each seek triggers a miss-fill cycle.
    folly::doNotOptimizeAway(gFixture.get());
}

BENCHMARK(FileCacheSeekBypass) {
    facebook::velox::ch::setupFixture();
    // readIfExistsOtherwiseBypass = true; segments never created.
    folly::doNotOptimizeAway(gFixture.get());
}

int main(int argc, char** argv) {
    folly::init(&argc, &argv);
    facebook::velox::ch::setupFixture();
    folly::runBenchmarks();
    if (facebook::velox::ch::gFixture &&
        facebook::velox::ch::gFixture->manager) {
        facebook::velox::ch::gFixture->manager->shutdown();
        facebook::velox::ch::FileCacheManager::setInstance(nullptr);
    }
    return 0;
}
```

Implement `setupFixture()` to initialize the benchmark context, then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_seek_benchmark \
  > <velox_build_dir>/build_015_benchmark.log 2>&1
echo "exit: $?"
```

Run the benchmark (short warmup to confirm it does not crash):

```bash
<velox_build_dir>/velox/ch/benchmarks/velox_ch_filecache_seek_benchmark \
  --bm_min_iters=3 \
  --file_size_mb=8 \
  --cache_size_mb=32 \
  --cache_dir=<velox_build_dir>/fc_bench \
  > <velox_build_dir>/bench_015_seek.log 2>&1
echo "exit: $?"
```

Expected: exit code 0; the log contains timing rows for the three benchmark
variants.

- [ ] **Step 7: Verify that no production defects required changes**

Inspect the diff for Velox production files:

```bash
cd <velox_repo>
git --no-pager diff -- \
  velox/ch/Disks/IO/FileCacheBufferedInput.cpp \
  velox/ch/Disks/IO/FileCacheInputStream.cpp \
  velox/ch/Interpreters/FileCache/FileCache.cpp \
  velox/ch/Interpreters/FileCache/FileCacheManager.cpp
```

If any production file changed, describe the defect and fix in the result
file before stating `status: success`.

- [ ] **Step 8: Inspect all task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short

```

Expected:

```text
No whitespace errors in Velox.
Only task-owned files appear in the diff.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 9: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/015-filecache-velox-e2e-result.md
```

Use exactly this structure:

````markdown
# Task 015 Result: `FileCache` Velox E2E Validation and Basic Benchmark

## Status

status: success

## Repository state

```text
Velox branch, HEAD, git status --short
```

## Files changed

```text
<list only task-owned Velox files>
```

## Commands run

```text
<configure, build, test, benchmark commands>
```

## Generated logs

```text
<velox_build_dir>/build_015_e2e.log
<velox_build_dir>/test_015_e2e.log
<velox_build_dir>/build_015_benchmark.log
<velox_build_dir>/bench_015_seek.log
```

## Test results

```text
velox_ch_filecache_e2e_test:         <N> tests, 0 failed
velox_ch_filecache_seek_benchmark:   3 iterations, no crash,
                                     timing rows for cache-hit / miss / bypass
```

## Production defects found

```text
None
(or describe each defect and the fix applied)
```

## Verification

```text
No E2E source contains GTEST_SKIP or DISABLED_ tests.
Final Velox E2E build exit code: 0
Benchmark build exit code: 0
git diff --check: no whitespace errors in Velox
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 016 (optional post-MVP): WriteBufferToFileSegment for Ephemeral segments.
Task 017 (optional post-MVP): Observability and cancellation hardening.
Task 018 (future): Gluten host integration.
Task 019 (future): Gluten builder and lifecycle E2E validation.
```
````

## Explicit exclusions

Do not implement in this task:

```text
Parquet / ORC / DWRF format reader integration tests — those require
  Velox format-reader fixtures and fall under Task 015 follow-up work once
  the core read-state-machine tests here pass cleanly.

Background prefetch via executor — load() is a no-op planning barrier;
  prefetch wiring is a post-MVP follow-up.

Per-query cache limit tests — those depend on QueryLimit (task 011) and the
  enableFilesystemQueryCacheLimit setting being wired into the Gluten
  request context, which is deferred.

SsdCache / checkpoint tests — the E2E suite uses a memory-backed cache only;
  SsdCache durability tests belong to a dedicated cache-persistence task.

Gluten builder and lifecycle integration — deferred to Tasks 018-019.

format-cpp-code.sh global run — do not run the formatter globally; apply
  clang-format-15 only to the new files created by this task if needed.
```

## Post-MVP 性能测试扩展 (whole-port review 2 后, user 2026-07-20)

Task 015 的 `FileCacheSeekBenchmark` 只是冒烟级微基准 (本地源、无对照、无并发)。
本扩展补一套真正的性能测试，回答:(1) 缓存层自身开销;(2) 多线程并发;(3) 我们的
FileCache (fcbi) vs Velox 原生 AsyncDataCache (cbi) vs 直读 (dbi) 的对比。

参考规程 (只读，脚本内硬编码路径跑前须改):
`/home/chang/SourceCode/.ai/share_data/local-cache/benchmark/HANDOFF-how-to-benchmark-filecache.md`
参考实现 (只读, ch-filecache 分支):
`velox/dwio/common/benchmarks/{CacheReadHarness.{h,cpp},BufferedInputWrapperBenchmark.cpp}`

### 关键背景:两条移植线不同

- 参考的 benchmark 是为 **ch-filecache 分支** (fcbi, 移植到 `velox/common/caching/filecache/`)
  写的。那套 fcbi **不 exactly 对齐 CH** — 它让 `FileCache` 裸构造 (`new FileCache(name, settings)`)。
- 我们的 **filecache2 分支** (`velox/ch/`) 严格对齐 CH:`FileCache` 不能裸构造, 必须经
  `FileCacheManager::create(Options)` / `FileCacheFactory::getOrCreate(name, FileCacheConfig,
  config_path)` 拿 `FileCachePtr` (注入 workerPool/scheduler/openedFileCache/localFileSystem/
  commonUserId)。这是 fcbi harness 移植时唯一的实质改动点。

### 结构 (user 定:嫌重复→共享基类)

单个可执行 bin，`--wrappers=fcbi/cbi/dbi/all` 切引擎。**一个共享抽象基类** `CacheHarnessBase`
承载全部共用逻辑 (WorkloadDriver: sequential/zipfian/uniform + 随机偏移;sweep: 循环+计时+
统计+PassResult;多线程并发驱动;结果打印+相对 delta;取中位数)。**三个薄子类**只重写
`buildCache()` + `readBatch()` 两个虚函数:
- `DbiHarness` — 直读无缓存, 下限基线 (从参考移植 buildCache/readBatch, 不依赖我们的 FileCache)。
- `CbiHarness` — 原生 `cache::AsyncDataCache` + 可选 SSD, 正面对手 (从参考移植, 不依赖我们的 FileCache)。
- `FcbiHarness` — 我们的 filecache2。**buildCache 经 FileCacheManager/Factory 拿 FileCachePtr,
  禁止裸 `new FileCache`** (复用本 task 已跑通的 `FileCacheSeekBenchmark.cpp` 的 Manager 建法)。
  readBatch 用我们的 `FileCacheBufferedInput` 签名 (`FileCacheReadOptions`/`FileCacheRequestContext`/
  双 IoStats、实例 `getCommonOrigin()`);include `velox/ch/Disks/IO/FileCacheBufferedInput.h`。

相对参考实现的改进:参考版三个 harness 各自复制了 sweep/driver/统计;我们**去重**上提到基类。

### 文件范围 (只在 velox/ch/benchmarks/, 不污染 velox 主干)

创建:
```text
velox/ch/benchmarks/CacheReadHarness.h
velox/ch/benchmarks/CacheReadHarness.cpp
velox/ch/benchmarks/FileCacheWrapperBenchmark.cpp
```
修改:
```text
velox/ch/benchmarks/CMakeLists.txt   (+ velox_ch_filecache_wrapper_benchmark target, guard VELOX_ENABLE_BENCHMARKS)
velox/ch/benchmarks/FileCacheSeekBenchmark.cpp   (微基准打牢, 见下)
```
每个新 C++ 文件加 Apache 2.0 头。链接实际存在的 target (mono build:`velox_ch_filecache`,
不是 `_dwio`/`_manager`/`_core` 分库);cbi 需 `velox_common_caching`/AsyncDataCache 相关库。

### gflags (对齐参考 + 补并发)

保留参考的关键开关:`--wrappers`、`--workloads=sequential,zipfian,uniform`、`--read_sizes_kib`、
`--target_ws_gb`、`--batch`、`--measure_passes` (取中位数)、`--cold_each_pass`、`--fcbi_segment_mb`、
`--filecache_root`、`--ram_cache_gb`/`--ssd_cache_gb`/`--ram_num_shards` (cbi)、`--out`。
**补 `--num_threads`** (并发变体, 参照原生 AsyncDataCacheTest 的多线程负载;线程预算 ≤16 避免超订)。

### FileCacheSeekBenchmark 微基准打牢

- miss/bypass 改 `BENCHMARK_RELATIVE` 相对 hit 基线 (直接读出相对倍率)。
- 把 `makeInput` 建对象开销移出计时段 (只计 enqueue+drain)。
- hit 变体加「不读源」断言:用 pread 计数的 ReadFile 包装, 命中路径 preadCount 不增 —
  hit 若退化成读源即失败 (防性能假绿)。

### 验收 gate

- `velox_ch_filecache_wrapper_benchmark --wrappers=all --workloads=sequential,zipfian --target_ws_gb=1
  --measure_passes=1`:三引擎 (fcbi/cbi/dbi) 都构造成功、跑完输出吞吐+相对 delta, exit 0。
- 多线程变体 (`--num_threads=4`) 跑不崩, exit 0。
- fcbi harness grep 确认**无裸 `new FileCache`** — 经 Manager/Factory 构造。
- 强化后的 `FileCacheSeekBenchmark` 构建+短跑 exit 0, hit 的不读源断言生效 (可 RED 验证:
  故意让 hit 读源则断言失败)。
- 三个既有 gate (buffered_input 19 / manager 19 / core_scc 47) + e2e 17 **不回归**。
- **生产文件 diff 为空** (除非发现真集成 bug — 须先在 receipt 描述再改)。
- 不 push;无 -j;日志入 build 目录。

### receipt

追加到 `port/task/result/015-filecache-velox-e2e-result.md` (保留原 attempt + review):
一节 "## Perf 扩展 (wrapper benchmark)", 含 baselines、文件、命令+exit+日志、三引擎背靠背
数字表 (吞吐+delta)、多线程结果、fcbi-经-Manager 证据、微基准打牢的 RED 证据、生产零改动确认。
