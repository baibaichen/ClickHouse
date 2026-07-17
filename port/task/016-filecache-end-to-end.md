# Task 016: `FileCache` Full End-to-End Validation

> **MVP task.**
>
> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> creates test and benchmark files in both the Velox checkout and the Gluten
> checkout. It writes one result file under this ClickHouse checkout. Do not
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

Deliverables:
- Velox focused test binary `velox_ch_filecache_e2e_test`.
- Gluten focused test binary `velox_file_cache_e2e_gluten_test`.
- Velox benchmark binary `velox_ch_filecache_seek_benchmark`.
- All test scenarios pass; benchmark builds and runs without crash.

## Starting point

```text
Velox repository:    /home/chang/OpenSource/velox
Required branch:     filecache
Expected HEAD:       descendant of the task-014 result commit

Gluten repository:   /home/chang/SourceCode/gluten1
Required branch:     any
Expected Gluten HEAD: descendant of the task-015 result commit

(Task 015 must be complete in the Gluten checkout before starting this task.)
```

## Design references

Read before editing:

```text
/home/chang/SourceCode/ClickHouse/port/task/ENVIRONMENT.md
/home/chang/SourceCode/ClickHouse/port/3-consumers/03-filecache-buffered-input-design.md
/home/chang/SourceCode/ClickHouse/port/3-consumers/01-filecache-read-context-design.md
/home/chang/SourceCode/ClickHouse/port/task/result/014-filecache-buffered-input-result.md
/home/chang/SourceCode/ClickHouse/port/task/result/015-filecache-gluten-integration-result.md
```

## File scope

Create in the Velox checkout:

```text
/home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp
/home/chang/OpenSource/velox/velox/ch/benchmarks/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/benchmarks/FileCacheSeekBenchmark.cpp
```

Modify in the Velox checkout:

```text
/home/chang/OpenSource/velox/velox/ch/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Disks/IO/CMakeLists.txt
```

Create in the Gluten checkout:

```text
/home/chang/SourceCode/gluten1/cpp/velox/tests/FileCacheE2EGlutenTest.cpp
```

Modify in the Gluten checkout:

```text
/home/chang/SourceCode/gluten1/cpp/velox/tests/CMakeLists.txt
```

Create in the ClickHouse checkout:

```text
/home/chang/SourceCode/ClickHouse/port/task/result/016-filecache-e2e-result.md
```

Every new Velox C++ file must begin with the Apache 2.0 Facebook license
header from `port/task/003-filecache-basic-common-shims.md`. Every new
Gluten C++ file must begin with the Apache 2.0 ASF header used in
`cpp/velox/compute/VeloxBackend.cc`.

## Steps

- [ ] **Step 1: Confirm the baselines**

```bash
cd /home/chang/OpenSource/velox
git --no-pager status --short --branch
git --no-pager log -1 --oneline

cd /home/chang/SourceCode/gluten1
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected: Velox on `filecache` after task 014; Gluten after task 015.
Record all pre-existing dirty files in the result file.

- [ ] **Step 2: Create the `velox/ch/Disks/IO/tests` directory and skeleton CMakeLists.txt**

If `velox/ch/Disks/IO/CMakeLists.txt` does not already include a
`tests` subdirectory, append to it:

```cmake
if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Create `velox/ch/Disks/IO/tests/CMakeLists.txt`:

```cmake
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_filecache_e2e_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_016_e2e.log 2>&1
echo "exit: $?"

ctest \
  --test-dir /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -R '^velox_ch_filecache_e2e_test$' \
  --output-on-failure \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_016_e2e.log 2>&1
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
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_filecache_seek_benchmark \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_016_benchmark.log 2>&1
echo "exit: $?"
```

Run the benchmark (short warmup to confirm it does not crash):

```bash
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/velox/ch/benchmarks/velox_ch_filecache_seek_benchmark \
  --bm_min_iters=3 \
  --file_size_mb=8 \
  --cache_size_mb=32 \
  --cache_dir=/home/chang/OpenSource/velox/cmake-build-debug-gcc13/fc_bench \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/bench_016_seek.log 2>&1
echo "exit: $?"
```

Expected: exit code 0; the log contains timing rows for the three benchmark
variants.

- [ ] **Step 7: Create the Gluten E2E test**

Add to `cpp/velox/tests/CMakeLists.txt`:

```cmake
add_velox_test(
  velox_file_cache_e2e_gluten_test
  SOURCES FileCacheE2EGlutenTest.cpp)
```

Create `cpp/velox/tests/FileCacheE2EGlutenTest.cpp` with real `FileHandle`,
`ConnectorQueryCtx`, memory pool, optional `AsyncDataCache`, and
`FileCacheManager` fixtures. Drive the stack from
`GlutenBufferedInputBuilder::create` through `FileCacheBufferedInput` into
`FileCacheInputStream`. Implement these tests with real assertions:

```text
BuilderProducesFileCacheInputWhenManagerInstalled
BuilderFallsBackToCachedInputWhenNoFileCache
BuilderFallsBackToDirectInputWhenNoCache
MissFillHitViaBuilder
FileCacheExcludesAsyncDataCacheOnSamePath
```

Reject false-green skipped tests before building:

```bash
if rg -n 'GTEST_SKIP|DISABLED_' \
  /home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/FileCacheE2ETest.cpp \
  /home/chang/SourceCode/gluten1/cpp/velox/tests/FileCacheE2EGlutenTest.cpp
then
  echo "ERROR: skipped E2E test remains"
  exit 1
fi
```

Then build:

```bash
cmake --build /home/chang/SourceCode/gluten1/cpp/build \
  --target velox_file_cache_e2e_gluten_test \
  > /home/chang/SourceCode/gluten1/cpp/build/build_016_gluten_e2e.log 2>&1
echo "exit: $?"
```

Run:

```bash
ctest --test-dir /home/chang/SourceCode/gluten1/cpp/build \
  -R '^velox_file_cache_e2e_gluten_test$' \
  --output-on-failure \
  > /home/chang/SourceCode/gluten1/cpp/build/test_016_gluten_e2e.log 2>&1
echo "exit: $?"
```

Expected:

```text
Build exit code: 0.
100% tests passed, 0 tests failed.
```

- [ ] **Step 8: Verify that no production defects required changes**

Inspect the diff for production files in both repos:

```bash
cd /home/chang/OpenSource/velox
git --no-pager diff -- \
  velox/ch/Disks/IO/FileCacheBufferedInput.cpp \
  velox/ch/Disks/IO/FileCacheInputStream.cpp \
  velox/ch/Interpreters/FileCache/FileCache.cpp \
  velox/ch/Interpreters/FileCache/FileCacheManager.cpp

cd /home/chang/SourceCode/gluten1
git --no-pager diff -- \
  cpp/velox/compute/VeloxBackend.cc \
  cpp/velox/memory/GlutenBufferedInputBuilder.h
```

If any production file changed, describe the defect and fix in the result
file before stating `status: success`.

- [ ] **Step 9: Inspect all task-owned changes**

```bash
cd /home/chang/OpenSource/velox
git --no-pager diff --check
git --no-pager status --short

cd /home/chang/SourceCode/gluten1
git --no-pager diff --check
git --no-pager status --short
```

Expected:

```text
No whitespace errors in either repo.
Only task-owned files appear in each diff.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 10: Write the result handoff**

Create:

```text
/home/chang/SourceCode/ClickHouse/port/task/result/016-filecache-e2e-result.md
```

Use exactly this structure:

````markdown
# Task 016 Result: `FileCache` Full E2E Validation

## Status

status: success

## Repository state

```text
Velox branch, HEAD, git status --short
Gluten branch, HEAD, git status --short
```

## Files changed

```text
<list only task-owned files in both repos>
```

## Commands run

```text
<configure, build, test, benchmark commands>
```

## Generated logs

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_016_e2e.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_016_e2e.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_016_benchmark.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/bench_016_seek.log
/home/chang/SourceCode/gluten1/cpp/build/build_016_gluten_e2e.log
/home/chang/SourceCode/gluten1/cpp/build/test_016_gluten_e2e.log
```

## Test results

```text
velox_ch_filecache_e2e_test:         <N> tests, 0 failed
velox_file_cache_e2e_gluten_test:    <N> tests, 0 failed
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
Final Gluten E2E build exit code: 0
Benchmark build exit code: 0
git diff --check: no whitespace errors in either repo
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 017 (optional post-MVP): WriteBufferToFileSegment for Ephemeral segments.
Task 018 (optional post-MVP): Observability and cancellation hardening.
```
````

## Explicit exclusions

Do not implement in this task:

```text
Parquet / ORC / DWRF format reader integration tests — those require
  Velox format-reader fixtures and fall under Task 016 follow-up work once
  the core read-state-machine tests here pass cleanly.

Background prefetch via executor — load() is a no-op planning barrier;
  prefetch wiring is a post-MVP follow-up.

Per-query cache limit tests — those depend on QueryLimit (task 011) and the
  enableFilesystemQueryCacheLimit setting being wired into the Gluten
  request context, which is deferred.

SsdCache / checkpoint tests — the E2E suite uses a memory-backed cache only;
  SsdCache durability tests belong to a dedicated cache-persistence task.

Maven / Scala integration tests — the builder selection is fully covered by
  native C++ tests; a JVM-level integration test can be added when the Spark
  e2e test harness for FileCache is established in a later task.

format-cpp-code.sh global run — do not run the formatter globally; apply
  clang-format-15 only to the new files created by this task if needed.
```
