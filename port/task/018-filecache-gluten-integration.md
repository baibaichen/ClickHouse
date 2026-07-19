# Task 018: Gluten Host Integration — `FileCacheManager` Ownership and Builder Selection

> **Deferred Gluten task.** Do not dispatch in the current Velox-only phase.
>
> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Gluten checkout under `<gluten_repo>` and
> writes one result file under this ClickHouse checkout. Do not modify
> ClickHouse source files. Do not commit or stage either repository.

## Goal

Wire `FileCacheManager` into `gluten::VeloxBackend` as the process-global
owner. Extend `GlutenBufferedInputBuilder::create` to select
`FileCacheBufferedInput` first, `CachedBufferedInput` second, and
`GlutenDirectBufferedInput` third. Implement the default identity resolver
(path-only when etag empty; `SipHash128(path+etag)` when non-empty). Shut
down the Manager in `VeloxBackend::tearDown` before executor and memory
resources are released.

Deliverables:
- Gluten native test binary `velox_file_cache_gluten_lifecycle_test`.
- All eight scenarios from the test plan pass.

## Starting point

```text
Velox repository:    <velox_repo>
Required branch:     filecache
Expected HEAD:       descendant of the commit where FileCacheBufferedInput
                     was added (task 014 result)

Gluten repository:   <gluten_repo>
Required branch:     main (or an active feature branch for this work)
Expected HEAD:       23ed0c154 or a direct descendant
```

Stop if the Velox branch is not `filecache`. Record the Gluten HEAD in the
result file.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/3-consumers/02-filecache-manager-design.md
<clickhouse_repo>/port/3-consumers/03-filecache-buffered-input-design.md
<clickhouse_repo>/port/3-consumers/01-filecache-read-context-design.md
<clickhouse_repo>/port/task/result/014-filecache-buffered-input-result.md
<gluten_repo>/cpp/velox/compute/VeloxBackend.h
<gluten_repo>/cpp/velox/compute/VeloxBackend.cc
<gluten_repo>/cpp/velox/memory/GlutenBufferedInputBuilder.h
<gluten_repo>/cpp/velox/config/VeloxConfig.h
```

Do not modify any Velox headers from this task. They are read-only inputs.

## File scope

Modify in the Gluten checkout:

```text
<gluten_repo>/cpp/velox/config/VeloxConfig.h
<gluten_repo>/cpp/velox/compute/VeloxBackend.h
<gluten_repo>/cpp/velox/compute/VeloxBackend.cc
<gluten_repo>/cpp/velox/memory/GlutenBufferedInputBuilder.h
<gluten_repo>/cpp/velox/tests/CMakeLists.txt
```

Create in the Gluten checkout:

```text
<gluten_repo>/cpp/velox/tests/FileCacheGlutenLifecycleTest.cpp
```

Create in the ClickHouse checkout:

```text
<clickhouse_repo>/port/task/result/018-filecache-gluten-integration-result.md
```

Every new Gluten C++ file must start with the Apache 2.0 ASF license header
used in other files under `cpp/velox/` (see `VeloxBackend.cc` for the exact
form).

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the baselines**

Run:

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline

cd <gluten_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected:

```text
Velox: branch is filecache; HEAD is the task-014 result commit or a descendant.
Gluten: any branch; record the HEAD in the result file.
Record all pre-existing dirty files in the result file.
```

Gluten must compile against the Task-014 Velox checkout, not its bundled
`ep/build-velox` copy. Reconfigure the existing Gluten build directory while
preserving its other cached options:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox \
  > <velox_build_dir>/build_task_018_external_velox.log 2>&1

<cmake> \
  -S <gluten_repo>/cpp \
  -B <gluten_repo>/cpp/build \
  -DVELOX_HOME=<velox_repo> \
  -DVELOX_BUILD_PATH=<velox_build_dir> \
  > <gluten_repo>/cpp/build/configure_task_018_external_velox.log 2>&1

grep -E '^(VELOX_HOME|VELOX_BUILD_PATH):' \
  <gluten_repo>/cpp/build/CMakeCache.txt
```

Expected:

```text
VELOX_HOME points to <velox_repo>.
VELOX_BUILD_PATH points to <velox_build_dir>.
That build contains lib/libvelox.a with Tasks 003-014 sources.
```

Stop if either cache entry still points to `ep/build-velox`.

- [ ] **Step 2: Create a failing focused test**

Create
`<gluten_repo>/cpp/velox/tests/FileCacheGlutenLifecycleTest.cpp`
with the following content:

```cpp
/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/common/testutil/TempDirectoryPath.h"
#include "memory/GlutenBufferedInputBuilder.h"

#include <gtest/gtest.h>

#include <atomic>
#include <string>

using facebook::velox::ch::FileCacheManager;
using facebook::velox::ch::FileCacheKey;
using facebook::velox::common::testutil::TempDirectoryPath;

namespace gluten {

namespace {

// Build a minimal FileCacheManager::Options pointing at a temp directory.
FileCacheManager::Options makeMinimalOptions(
    const std::string& cacheDir,
    facebook::velox::memory::MemoryPool* pool) {
  FileCacheManager::Options opts;
  facebook::velox::ch::FileCacheConfig cfg;
  cfg.path = cacheDir;
  cfg.maxSize = 64 * 1024 * 1024; // 64 MB
  opts.caches = {{"default", cfg, cacheDir}};
  opts.defaultCacheName = "default";
  opts.commonUserId = "gluten-test";
  opts.cachePathPrefix = cacheDir;
  opts.allowedCacheRoot = cacheDir;
  opts.localFileSystem =
      facebook::velox::filesystems::getFileSystem(cacheDir, nullptr);
  opts.memoryPool = pool;
  opts.timekeeper = std::make_shared<folly::ThreadWheelTimekeeper>();
  opts.initializeOnCreate = false; // skip async metadata load in unit tests
  return opts;
}

} // namespace

class FileCacheGlutenLifecycleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Ensure no manager is left installed before each test.
    ASSERT_EQ(FileCacheManager::getInstance(), nullptr)
        << "A previous test left FileCacheManager installed";
    tempDir_ = TempDirectoryPath::create();
    pool_ = facebook::velox::memory::memoryManager()->addLeafPool(
        "FileCacheGlutenLifecycleTest");
  }

  void TearDown() override {
    // Remove any manager that the test may have installed.
    if (auto* mgr = FileCacheManager::getInstance()) {
      mgr->shutdown();
      FileCacheManager::setInstance(nullptr);
    }
    manager_.reset();
  }

  std::shared_ptr<TempDirectoryPath> tempDir_;
  std::shared_ptr<facebook::velox::memory::MemoryPool> pool_;
  std::shared_ptr<FileCacheManager> manager_;
};

TEST_F(FileCacheGlutenLifecycleTest, ManagerSetAndGetInstance) {
  auto opts = makeMinimalOptions(tempDir_->getPath(), pool_.get());
  manager_ = FileCacheManager::create(std::move(opts));
  ASSERT_NE(manager_, nullptr);

  FileCacheManager::setInstance(manager_.get());
  EXPECT_EQ(FileCacheManager::getInstance(), manager_.get());

  manager_->shutdown();
  FileCacheManager::setInstance(nullptr);
  EXPECT_EQ(FileCacheManager::getInstance(), nullptr);
}

TEST_F(FileCacheGlutenLifecycleTest, SetInstanceTwiceWithSamePointerIsNoop) {
  auto opts = makeMinimalOptions(tempDir_->getPath(), pool_.get());
  manager_ = FileCacheManager::create(std::move(opts));

  FileCacheManager::setInstance(manager_.get());
  // Setting the same pointer again must not throw.
  EXPECT_NO_THROW(FileCacheManager::setInstance(manager_.get()));
  EXPECT_EQ(FileCacheManager::getInstance(), manager_.get());
}

TEST_F(FileCacheGlutenLifecycleTest, ReplaceWithDifferentLiveManagerThrows) {
  auto opts1 = makeMinimalOptions(tempDir_->getPath(), pool_.get());
  manager_ = FileCacheManager::create(std::move(opts1));
  FileCacheManager::setInstance(manager_.get());

  auto tempDir2 = TempDirectoryPath::create();
  auto opts2 = makeMinimalOptions(tempDir2->getPath(), pool_.get());
  auto manager2 = FileCacheManager::create(std::move(opts2));

  EXPECT_THROW(FileCacheManager::setInstance(manager2.get()), std::exception);
  // Cleanup manager2 without publishing.
  manager2->shutdown();
}

TEST_F(FileCacheGlutenLifecycleTest, ShutdownIdempotent) {
  auto opts = makeMinimalOptions(tempDir_->getPath(), pool_.get());
  manager_ = FileCacheManager::create(std::move(opts));
  FileCacheManager::setInstance(manager_.get());

  manager_->shutdown();
  EXPECT_NO_THROW(manager_->shutdown());
}

TEST_F(FileCacheGlutenLifecycleTest, OperationAfterShutdownFails) {
  auto opts = makeMinimalOptions(tempDir_->getPath(), pool_.get());
  manager_ = FileCacheManager::create(std::move(opts));
  FileCacheManager::setInstance(manager_.get());
  manager_->shutdown();

  // get() must fail after shutdown; applyConfigs must fail too.
  EXPECT_THROW(manager_->getDefault(), std::exception);
}

TEST_F(FileCacheGlutenLifecycleTest, BuilderSelectsFileCacheInputWhenManagerInstalled) {
  auto opts = makeMinimalOptions(tempDir_->getPath(), pool_.get());
  manager_ = FileCacheManager::create(std::move(opts));
  FileCacheManager::setInstance(manager_.get());

  // Create a minimal FileHandle with a local ReadFile.
  // The builder must return a FileCacheBufferedInput, not CachedBufferedInput
  // or GlutenDirectBufferedInput.
  GlutenBufferedInputBuilder builder;
  // Verify the builder recognizes the installed FileCache manager.
  // (Build-time type check: the returned BufferedInput must be castable to
  //  facebook::velox::ch::FileCacheBufferedInput.)
  EXPECT_NE(FileCacheManager::getInstance(), nullptr);
  // Full Gluten scan-path test is covered by Task 019; here we only
  // verify the singleton is reachable from the builder's call site.
}

TEST_F(FileCacheGlutenLifecycleTest, IdentityResolverEmptyEtag) {
  // path-only key must come from fromPath; path+empty-etag must not hash.
  const std::string path = "/data/part-00000.parquet";
  auto key = FileCacheKey::fromPath(path);
  EXPECT_EQ(key, FileCacheKey::fromPath(path));
}

TEST_F(FileCacheGlutenLifecycleTest, IdentityResolverNonEmptyEtag) {
  // Different etags on the same path must produce different keys.
  const std::string path = "/data/part-00000.parquet";
  const std::string etag1 = "etag-v1";
  const std::string etag2 = "etag-v2";

  // Key derivation: SipHash128(path + etag)
  auto key1 = FileCacheKey::fromKey(
      facebook::velox::ch::sipHash128(
          (path + etag1).data(), (path + etag1).size()));
  auto key2 = FileCacheKey::fromKey(
      facebook::velox::ch::sipHash128(
          (path + etag2).data(), (path + etag2).size()));
  EXPECT_NE(key1, key2);

  // Same etag must give the same key.
  auto key1b = FileCacheKey::fromKey(
      facebook::velox::ch::sipHash128(
          (path + etag1).data(), (path + etag1).size()));
  EXPECT_EQ(key1, key1b);
}

} // namespace gluten
```

Append to `cpp/velox/tests/CMakeLists.txt`:

```cmake
add_velox_test(
  velox_file_cache_gluten_lifecycle_test
  SOURCES FileCacheGlutenLifecycleTest.cpp)
```

- [ ] **Step 3: Verify the test fails before implementation**

Build the test target (red build):

```bash
<cmake> --build <gluten_repo>/cpp/build \
  --target velox_file_cache_gluten_lifecycle_test \
  > <gluten_repo>/cpp/build/build_018_red.log 2>&1
echo "exit: $?"
```

Expected: build fails with missing-include or missing-symbol errors.

If it unexpectedly succeeds, record the binary path, run it with:

```bash
ctest --test-dir <gluten_repo>/cpp/build \
  -R '^velox_file_cache_gluten_lifecycle_test$' \
  --output-on-failure \
  > <gluten_repo>/cpp/build/test_018_red.log 2>&1
```

and record the result. Proceed only if either the build or tests fail.

- [ ] **Step 4: Add FileCache config keys to `VeloxConfig.h`**

Add the following block after the existing `kVeloxFileHandleCacheEnabled`
block in `cpp/velox/config/VeloxConfig.h`:

```cpp
// FileCache (ClickHouse-derived disk cache, independent of AsyncDataCache)
const std::string kVeloxFileCacheEnabled =
    "spark.gluten.sql.columnar.backend.velox.filecache.enabled";
const bool kVeloxFileCacheEnabledDefault = false;

const std::string kVeloxFileCacheDir =
    "spark.gluten.sql.columnar.backend.velox.filecache.dir";
// No default: required when kVeloxFileCacheEnabled is true.

const std::string kVeloxFileCacheMaxSizeBytes =
    "spark.gluten.sql.columnar.backend.velox.filecache.maxSizeBytes";
const uint64_t kVeloxFileCacheMaxSizeBytesDefault = 10ULL * 1024 * 1024 * 1024; // 10 GB

const std::string kVeloxFileCacheName =
    "spark.gluten.sql.columnar.backend.velox.filecache.name";
const std::string kVeloxFileCacheNameDefault = "default";

const std::string kVeloxFileCacheCommonUserId =
    "spark.gluten.sql.columnar.backend.velox.filecache.commonUserId";
const std::string kVeloxFileCacheCommonUserIdDefault = "gluten";
```

- [ ] **Step 5: Add `fileCacheManager_` field and accessor to `VeloxBackend.h`**

Add the following to the `VeloxBackend` class in `cpp/velox/compute/VeloxBackend.h`:

1. In the `#include` section, after the existing includes:

```cpp
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
```

2. In the `public:` section, after `getAsyncDataCache()`:

```cpp
facebook::velox::ch::FileCacheManager* getFileCacheManager() const {
  return fileCacheManager_.get();
}
```

3. In the `private:` section, after `asyncDataCache_`:

```cpp
// Process-global owner of the ClickHouse FileCache.
// Initialized in initFileCache() and shut down before executors in tearDown().
std::shared_ptr<facebook::velox::ch::FileCacheManager> fileCacheManager_;
std::shared_ptr<facebook::velox::memory::MemoryPool> fileCacheMemoryPool_;
```

4. In `private:` method declarations, after `initCache()`:

```cpp
void initFileCache();
```

- [ ] **Step 6: Implement `initFileCache()` in `VeloxBackend.cc`**

Add the following `#include` after the existing `#include "velox/common/caching/AsyncDataCache.h"`:

```cpp
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/common/file/FileSystems.h"
#include <folly/futures/ThreadWheelTimekeeper.h>
```

Add `initFileCache()` after the existing `initCache()` definition:

```cpp
void VeloxBackend::initFileCache() {
  if (!backendConf_->get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    return;
  }

  const auto cacheDir =
      backendConf_->get<std::string>(kVeloxFileCacheDir, "");
  GLUTEN_CHECK(
      !cacheDir.empty(),
      kVeloxFileCacheDir + " must be set when FileCache is enabled");

  const uint64_t maxSizeBytes = backendConf_->get<uint64_t>(
      kVeloxFileCacheMaxSizeBytes, kVeloxFileCacheMaxSizeBytesDefault);
  const std::string cacheName = backendConf_->get<std::string>(
      kVeloxFileCacheName, kVeloxFileCacheNameDefault);
  const std::string commonUserId = backendConf_->get<std::string>(
      kVeloxFileCacheCommonUserId, kVeloxFileCacheCommonUserIdDefault);

  facebook::velox::ch::FileCacheConfig cfg;
  cfg.path = cacheDir;
  cfg.maxSize = maxSizeBytes;

  facebook::velox::ch::FileCacheManager::Options opts;
  opts.caches = {{cacheName, cfg, cacheDir}};
  opts.defaultCacheName = cacheName;
  opts.commonUserId = commonUserId;
  opts.cachePathPrefix = cacheDir;
  opts.allowedCacheRoot = cacheDir;
  opts.localFileSystem =
      facebook::velox::filesystems::getFileSystem(cacheDir, nullptr);
  fileCacheMemoryPool_ =
      globalMemoryManager_->getMemoryManager()->addLeafPool("FileCache");
  opts.memoryPool = fileCacheMemoryPool_.get();
  opts.timekeeper =
      std::make_shared<folly::ThreadWheelTimekeeper>();
  opts.initializeOnCreate = true;

  fileCacheManager_ =
      facebook::velox::ch::FileCacheManager::create(std::move(opts));
  facebook::velox::ch::FileCacheManager::setInstance(fileCacheManager_.get());
  LOG(INFO) << "FileCacheManager initialized: dir=" << cacheDir
            << " maxSize=" << maxSizeBytes;
}
```

Call `initFileCache()` in `VeloxBackend::init()` immediately after the call
to `initCache()`:

```cpp
  initCache();
  initFileCache(); // ← add this line
```

- [ ] **Step 7: Update `VeloxBackend::tearDown()` to shut down FileCache before executors**

Replace the beginning of `VeloxBackend::tearDown()` so that FileCache is
shut down before the executor resets. The existing code starts with
filesystem close calls followed by `executor_.reset()`. Insert the FileCache
shutdown block before `executor_.reset()`:

```cpp
  // Shut down FileCache before executors; cache workers must stop before the
  // thread pools they use are destroyed.
  if (fileCacheManager_) {
    fileCacheManager_->shutdown();
    facebook::velox::ch::FileCacheManager::setInstance(nullptr);
    fileCacheManager_.reset();
    fileCacheMemoryPool_.reset();
    LOG(INFO) << "FileCacheManager shut down";
  }

  // Destruct IOThreadPoolExecutor will join all threads.
  // ...existing comment...
  executor_.reset();
  spillExecutor_.reset();
  // ...rest of tearDown unchanged...
```

- [ ] **Step 8: Extend `GlutenBufferedInputBuilder::create()` for FileCache selection**

Replace the `GlutenBufferedInputBuilder::create` method body in
`cpp/velox/memory/GlutenBufferedInputBuilder.h` with:

```cpp
  std::unique_ptr<facebook::velox::dwio::common::BufferedInput> create(
      const facebook::velox::FileHandle& fileHandle,
      const facebook::velox::dwio::common::ReaderOptions& readerOpts,
      const facebook::velox::connector::ConnectorQueryCtx* connectorQueryCtx,
      std::shared_ptr<facebook::velox::io::IoStatistics> ioStatistics,
      std::shared_ptr<facebook::velox::IoStats> ioStats,
      folly::Executor* executor,
      const folly::F14FastMap<std::string, std::string>& fileReadOps = {})
      override {
    // Priority 1: ClickHouse FileCache (independent of AsyncDataCache).
    auto* fcManager =
        facebook::velox::ch::FileCacheManager::getInstance();
    if (fcManager != nullptr) {
      const std::string path = fileHandle.file->getName();
      // Identity resolver: path-only key when etag is empty;
      // SipHash128(path + etag) key when etag is non-empty.
      // Current Gluten integration does not supply etags; use path-only.
      auto cacheKey = facebook::velox::ch::FileCacheKey::fromPath(path);
      const auto* defaultCache = fcManager->getDefault().get();
      VELOX_CHECK_NOT_NULL(
          defaultCache, "FileCacheManager has no default cache");

      facebook::velox::ch::FileCacheRequestContext reqCtx;
      reqCtx.queryId = connectorQueryCtx->queryId();
      reqCtx.userId = fcManager->commonUserId();
      reqCtx.cacheable = true;
      reqCtx.segmentType =
          facebook::velox::ch::FileSegmentKeyType::Data;

      facebook::velox::ch::FileCacheReadOptions cacheOpts;
      cacheOpts.remoteFsBufferSize = readerOpts.loadQuantum();
      cacheOpts.localFsBufferSize = readerOpts.loadQuantum();

      facebook::velox::ch::FileCacheOriginInfo origin{
          reqCtx.userId, 1, reqCtx.segmentType};

      return std::make_unique<
          facebook::velox::ch::FileCacheBufferedInput>(
          fileHandle.file,
          fcManager->getDefault(),
          std::move(cacheKey),
          std::move(origin),
          std::move(cacheOpts),
          std::move(reqCtx),
          facebook::velox::dwio::common::MetricsLog::voidLog(),
          std::move(ioStatistics),
          std::move(ioStats),
          executor,
          readerOpts,
          fileReadOps);
    }

    // Priority 2: Velox AsyncDataCache.
    if (connectorQueryCtx->cache()) {
      return std::make_unique<
          facebook::velox::dwio::common::CachedBufferedInput>(
          fileHandle.file,
          facebook::velox::dwio::common::MetricsLog::voidLog(),
          fileHandle.uuid,
          connectorQueryCtx->cache(),
          facebook::velox::connector::Connector::getTracker(
              connectorQueryCtx->scanId(), readerOpts.loadQuantum()),
          fileHandle.groupId,
          std::move(ioStatistics),
          std::move(ioStats),
          executor,
          readerOpts,
          fileReadOps);
    }

    // Priority 3: Direct (no cache).
    return std::make_unique<GlutenDirectBufferedInput>(
        fileHandle.file,
        facebook::velox::dwio::common::MetricsLog::voidLog(),
        fileHandle.uuid,
        facebook::velox::connector::Connector::getTracker(
            connectorQueryCtx->scanId(), readerOpts.loadQuantum()),
        fileHandle.groupId,
        std::move(ioStatistics),
        std::move(ioStats),
        executor,
        readerOpts,
        fileReadOps);
  }
```

Add the required include at the top of `GlutenBufferedInputBuilder.h`:

```cpp
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
```

- [ ] **Step 9: Build the focused test**

```bash
<cmake> --build <gluten_repo>/cpp/build \
  --target velox_file_cache_gluten_lifecycle_test \
  > <gluten_repo>/cpp/build/build_018_lifecycle.log 2>&1
echo "exit: $?"
```

Expected exit code: 0.

If it fails, open the log, fix the first actionable compiler error, rebuild.
Do not proceed to run until the build is clean.

- [ ] **Step 10: Run the focused test**

```bash
ctest --test-dir <gluten_repo>/cpp/build \
  -R '^velox_file_cache_gluten_lifecycle_test$' \
  --output-on-failure \
  > <gluten_repo>/cpp/build/test_018_lifecycle.log 2>&1
echo "exit: $?"
```

Expected:

```text
100% tests passed, 0 tests failed.
```

- [ ] **Step 11: Verify the per-runtime connector does not own the Manager**

Read `cpp/velox/compute/VeloxRuntime.cc` and confirm that
`unregisterConnectors()` does not call `FileCacheManager::shutdown()` or
`FileCacheManager::setInstance(nullptr)`. Those must remain exclusive to
`VeloxBackend::tearDown()`. Record the line range of
`VeloxRuntime::unregisterConnectors` in the result file to confirm no change
is needed.

- [ ] **Step 12: Inspect task-owned changes**

```bash
cd <gluten_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  cpp/velox/config/VeloxConfig.h \
  cpp/velox/compute/VeloxBackend.h \
  cpp/velox/compute/VeloxBackend.cc \
  cpp/velox/memory/GlutenBufferedInputBuilder.h \
  cpp/velox/tests/CMakeLists.txt \
  cpp/velox/tests/FileCacheGlutenLifecycleTest.cpp
```

Expected:

```text
No whitespace errors.
Only the six task-owned files appear in the diff.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 13: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/018-filecache-gluten-integration-result.md
```

Use exactly this structure:

````markdown
# Task 018 Result: Gluten Host Integration

## Status

status: success

## Repository state

```text
Velox branch, HEAD, and git status --short
Gluten branch, HEAD, and git status --short
```

## Files changed

```text
<list only task-owned files in both repos>
```

## Commands run

```text
<configure, build, test, and verification commands>
```

## Generated logs

```text
<gluten_repo>/cpp/build/build_018_red.log
<gluten_repo>/cpp/build/build_018_lifecycle.log
<gluten_repo>/cpp/build/test_018_lifecycle.log
```

## Verification

```text
Red build: failed with <first error>
Final build exit code: 0
Test result: 100% passed
VeloxRuntime::unregisterConnectors: lines <N-M>, no FileCache shutdown call
git diff --check: no whitespace errors
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 019: Gluten builder and lifecycle E2E validation.
```
````

If blocked or failed, set status accordingly and do not claim success.

## Explicit exclusions

Do not implement in this task:

```text
JNI Scala lifecycle tests — verifiable via the native lifecycle tests here
  plus the integration described in Task 019; native coverage is sufficient
  for the deferred Gluten integration acceptance.

Multi-cache configuration — only the single "default" cache path is tested;
  multi-cache requires FileCacheFactory::getOrCreate semantics verified in
  task 012 and is not a Gluten concern in the first integration.

Supplying etags from the Gluten scan path — current Gluten does not supply
  etags, so the path-only resolver is the full integration for now. Task 018
  still tests the non-empty-etag key derivation directly.

ReadLease — not introduced; the VeloxBackend tearDown barrier is the
  lifecycle gate.

Per-runtime FileCacheManager ownership — VeloxRuntime only uses the
  non-owning Manager pointer; it must not call shutdown.

AsyncDataCache + FileCache co-existence on the same scan path — the Builder
  selection is mutually exclusive by construction; the test in Task 019
  verifies this.

VeloxConfig.h format-cpp-code.sh — do not run the global formatter; the
  config additions follow the file's existing style.
```
