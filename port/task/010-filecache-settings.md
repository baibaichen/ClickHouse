# Task 010: `FileCache` Settings — `FileCacheConfig` and `FileCacheSettingsLoader`

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes one
> result file under this ClickHouse checkout. Do not modify ClickHouse source
> files. Do not commit or stage either repository.

## Pre-execution source-contract amendment

This section supersedes the three-iterator `std::mismatch` path-containment snippet
later in this file.

After both paths are normalized/canonicalized according to the settings design,
compare path components without ever advancing past either range:

```text
rootIt = allowedRoot.begin
pathIt = resolvedPath.begin

while rootIt != allowedRoot.end:
  if pathIt == resolvedPath.end: reject
  if *rootIt != *pathIt: reject
  increment both

accept
```

This is a component-prefix check, not a string-prefix check. Required RED tests:

1. exact root is accepted;
2. a descendant is accepted;
3. a shorter resolved path is rejected without invalid iterator access;
4. sibling `/cache-other` is rejected for root `/cache`;
5. `..` escape is rejected after normalization;
6. symlink/canonicalization behavior follows the approved settings design and does
   not bypass the allowed root.

Do not execute Task 010 with the old `std::mismatch(root.begin(), root.end(),
resolved.begin())` snippet.

### Mandatory review checkpoint

After Task 010 is accepted, stop. Run a whole-port source-contract review of
Tasks 003-010. Any finding reopens the affected task and stops execution. Task 011
may start only with zero unresolved findings and explicit user approval.

### Controller amendment after Worker attempt 1 — mono/non-mono registration

Add this file to the declared `Modify` scope:

```text
<velox_repo>/velox/ch/Common/CMakeLists.txt
```

The literal Step 7 CMake snippets are superseded.

Register the compiled source through the mono-safe helper:

```cmake
velox_sources(
  velox_ch_filecache
  PRIVATE
  FileCacheSettings.cpp
)
```

Append both public headers to the existing non-mono `PUBLIC HEADERS` file set in
`velox/ch/Interpreters/FileCache/CMakeLists.txt`:

```cmake
${CMAKE_CURRENT_SOURCE_DIR}/FileCacheSettings.h
${CMAKE_CURRENT_SOURCE_DIR}/FileCacheReadOptions.h
```

Do not call `target_sources` on the mono alias and do not create another file
set.

`FileCacheSettings.h` publicly includes `velox/common/config/Config.h`. Add
`velox_common_config` to the existing non-mono
`target_link_libraries(velox_ch_filecache PUBLIC ...)` block in
`velox/ch/Common/CMakeLists.txt`.

The focused settings test must directly link only:

```cmake
velox_ch_filecache
GTest::gtest
GTest::gtest_main
```

so missing public compile/link dependencies are not masked.

Run the normal mono gates and a separate full-profile non-mono build at
`<velox_build_dir>-task010-nonmono` with `VELOX_MONO_LIBRARY=OFF`. Build,
discover, and run `velox_ch_settings_test` as a reduced public-interface
consumer; persist configure/build/test/discovery logs there.

## Goal

Port `FileCacheSettings.h` and `FileCacheSettings.cpp` from ClickHouse to Velox
as an exact effective-configuration and validation-semantics port:

```text
FileCacheConfig struct: all algorithm fields with exact CH defaults
FileCacheSettingsLoader: ConfigBase parsing, presence tracking, path resolution, validation
FileCacheReadOptions: request-scoped values required by core/query-limit APIs
```

The deliverable is:

```text
velox/ch/Interpreters/FileCache/FileCacheSettings.h
velox/ch/Interpreters/FileCache/FileCacheSettings.cpp
velox/ch/Interpreters/FileCache/FileCacheReadOptions.h
```

plus a focused `velox_ch_settings_test` test executable.

Key constraints:
- **Do not** port CH `BaseSettings` macros, `Poco::Util::AbstractConfiguration`,
  `NamedCollection`, `ColumnsDescription`, or system-table exposure.
- **Explicitly reject** `cacheOnWriteOperations = true` and overcommit policies
  (`LRU_OVERCOMMIT` / `SLRU_OVERCOMMIT`) with a clear `VeloxRuntimeError`.
- **Fail fast** on `maxFileSegmentSize == 0` and on a ratio-derived `maxSize == 0`.
- **All** `FileCacheConfig` field defaults must reference the constants from
  `FileCache_fwd.h`, never hardcode numeric literals.

## Starting point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected HEAD:    Task 009 result commit or any direct descendant
```

The following files must already exist (created by Tasks 003, 008, and 009):

```text
velox/ch/Common/ClickHouseAliases.h
velox/ch/Common/FileCacheException.h
velox/ch/Common/FileCacheFilesystem.h
velox/ch/Interpreters/FileCache/FileCache_fwd.h
velox/ch/Interpreters/FileCache/CMakeLists.txt
```

Stop if the branch is not `filecache` or if any of these files is absent.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/2-file-cache/06-filecache-settings-files-design.md
<clickhouse_repo>/port/2-file-cache/01-filecache-fwd-files-design.md
<clickhouse_repo>/port/3-consumers/01-filecache-read-context-design.md
```

Use the ClickHouse files only as behavioral references:

```text
<clickhouse_repo>/src/Interpreters/FileCache/FileCacheSettings.h
<clickhouse_repo>/src/Interpreters/FileCache/FileCacheSettings.cpp
```

Velox config infrastructure reference:

```text
<velox_repo>/velox/common/config/Config.h
```

## File scope

Modify:

```text
<velox_repo>/velox/ch/Common/CMakeLists.txt
<velox_repo>/velox/ch/Interpreters/FileCache/CMakeLists.txt
<velox_repo>/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheSettings.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheSettings.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheReadOptions.h
<velox_repo>/velox/ch/Interpreters/FileCache/tests/FileCacheSettingsTest.cpp
<clickhouse_repo>/port/task/result/010-filecache-settings-result.md
```

Every new Velox C++ file must begin with the Apache 2.0 license header using
`/* ... */` comment form.

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected:

```text
Branch is filecache.
HEAD is Task 009 result or a direct descendant.
Record pre-existing dirty files in result.
```

- [ ] **Step 2: Add a failing focused test**

Append the following target to
`velox/ch/Interpreters/FileCache/tests/CMakeLists.txt`:

```cmake
add_executable(velox_ch_settings_test FileCacheSettingsTest.cpp)
add_test(velox_ch_settings_test velox_ch_settings_test)

target_link_libraries(
  velox_ch_settings_test
  PRIVATE
    velox_ch_filecache
    velox_exception
    velox_common_config
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

Create `velox/ch/Interpreters/FileCache/tests/FileCacheSettingsTest.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */

#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/ch/Interpreters/FileCache/FileCacheReadOptions.h"
#include "velox/common/base/Exceptions.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace facebook::velox::ch
{
namespace
{

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Build a ConfigBase from a flat key-value map for testing.
static std::shared_ptr<config::ConfigBase> makeConfig(
    std::unordered_map<std::string, std::string> kv)
{
    return std::make_shared<config::ConfigBase>(std::move(kv));
}

/// Minimal valid config: one absolute path and explicit max-size.
static std::unordered_map<std::string, std::string> minimalKv(
    const std::string & path = "/tmp/test_cache",
    const std::string & maxSize = "1073741824")
{
    return {
        {"file-cache.path", path},
        {"file-cache.max-size", maxSize},
    };
}

// ── Default value tests ───────────────────────────────────────────────────────

TEST(FileCacheConfigTest, DefaultValues)
{
    FileCacheConfig cfg;
    EXPECT_EQ(cfg.maxSize, 0u);
    EXPECT_EQ(cfg.maxElements, FILECACHE_DEFAULT_MAX_ELEMENTS);
    EXPECT_EQ(cfg.maxFileSegmentSize, FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE);
    EXPECT_EQ(cfg.boundaryAlignment, FILECACHE_DEFAULT_FILE_SEGMENT_ALIGNMENT);
    EXPECT_EQ(cfg.reserveGranularity, FILECACHE_DEFAULT_RESERVE_GRANULARITY);
    EXPECT_EQ(cfg.cacheOnWriteOperations, false);
    EXPECT_EQ(cfg.cachePolicy, FileCachePolicy::SLRU);
    EXPECT_DOUBLE_EQ(cfg.slruSizeRatio, FILECACHE_DEFAULT_SLRU_RATIO);
    EXPECT_EQ(cfg.backgroundDownloadThreads, FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_THREADS);
    EXPECT_EQ(cfg.backgroundDownloadQueueSizeLimit, FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_QUEUE_SIZE_LIMIT);
    EXPECT_EQ(cfg.backgroundDownloadMaxFileSegmentSize,
              FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE_WITH_BACKGROUND_DOWLOAD);
    EXPECT_EQ(cfg.loadMetadataThreads, FILECACHE_DEFAULT_LOAD_METADATA_THREADS);
    EXPECT_EQ(cfg.loadMetadataAsynchronously, false);
    EXPECT_DOUBLE_EQ(cfg.keepFreeSpaceSizeRatio, FILECACHE_DEFAULT_FREE_SPACE_SIZE_RATIO);
    EXPECT_DOUBLE_EQ(cfg.keepFreeSpaceElementsRatio, FILECACHE_DEFAULT_FREE_SPACE_ELEMENTS_RATIO);
    EXPECT_EQ(cfg.keepFreeSpaceRemoveBatch, FILECACHE_DEFAULT_FREE_SPACE_REMOVE_BATCH);
    EXPECT_EQ(cfg.keepFreeSpaceEvictionThreads, FILECACHE_DEFAULT_FREE_SPACE_EVICTION_THREADS);
    EXPECT_EQ(cfg.enableFilesystemQueryCacheLimit, false);
    EXPECT_EQ(cfg.enableBypassCacheWithThreshold, false);
    EXPECT_EQ(cfg.bypassCacheThreshold, FILECACHE_BYPASS_THRESHOLD);
    EXPECT_EQ(cfg.writeCachePerUserIdDirectory, false);
    EXPECT_EQ(cfg.allowDynamicCacheResize, false);
    EXPECT_EQ(cfg.maxSizeRatioToTotalSpace, 0.0);
    EXPECT_DOUBLE_EQ(cfg.checkCacheProbability, 0.001);
    EXPECT_EQ(cfg.useSplitCache, false);
}

TEST(FileCacheConfigTest, FreeSpaceRatiosDisabledByDefault)
{
    FileCacheConfig cfg;
    EXPECT_DOUBLE_EQ(cfg.keepFreeSpaceSizeRatio, 0.0);
    EXPECT_DOUBLE_EQ(cfg.keepFreeSpaceElementsRatio, 0.0);
}

TEST(FileCacheConfigTest, DefaultPolicySLRU)
{
    FileCacheConfig cfg;
    EXPECT_EQ(cfg.cachePolicy, FileCachePolicy::SLRU);
}

TEST(FileCacheConfigTest, DefaultSLRURatio)
{
    FileCacheConfig cfg;
    EXPECT_DOUBLE_EQ(cfg.slruSizeRatio, 0.6);
}

TEST(FileCacheConfigTest, DefaultMetadataThreads)
{
    FileCacheConfig cfg;
    EXPECT_EQ(cfg.loadMetadataThreads, 16u);
}

TEST(FileCacheConfigTest, DefaultBackgroundDownloadThreads)
{
    FileCacheConfig cfg;
    EXPECT_EQ(cfg.backgroundDownloadThreads, 5u);
}

TEST(FileCacheConfigTest, EqualityDefault)
{
    FileCacheConfig a, b;
    EXPECT_EQ(a, b);
}

TEST(FileCacheConfigTest, FileCacheSettingsAliasesConfig)
{
    static_assert(std::is_same_v<FileCacheSettings, FileCacheConfig>);
}

TEST(FileCacheReadOptionsTest, ExactDefaults)
{
    FileCacheReadOptions options;
    EXPECT_FALSE(options.tempCacheOnly);
    EXPECT_FALSE(options.readIfExistsOtherwiseBypass);
    EXPECT_TRUE(options.allowBackgroundDownload);
    EXPECT_EQ(options.segmentsBatchSize, 20);
    EXPECT_EQ(options.maxDownloadSizePerQuery, 0);
    EXPECT_TRUE(options.skipDownloadIfExceedsPerQueryCacheWriteLimit);
}

// ── Parsing tests ─────────────────────────────────────────────────────────────

TEST(FileCacheSettingsLoaderTest, ParseAbsolutePathAndMaxSize)
{
    auto cfg = makeConfig({
        {"file-cache.path", "/data/cache"},
        {"file-cache.max-size", "10737418240"},
    });
    FileCacheConfig result = FileCacheSettingsLoader::load(
        *cfg, "file-cache", "/prefix", "/data");
    EXPECT_EQ(result.path, "/data/cache");
    EXPECT_EQ(result.maxSize, 10'737'418'240u);
}

TEST(FileCacheSettingsLoaderTest, ParseRelativePathUnderPrefix)
{
    auto cfg = makeConfig({
        {"file-cache.path", "my_cache"},
        {"file-cache.max-size", "1073741824"},
    });
    FileCacheConfig result = FileCacheSettingsLoader::load(
        *cfg, "file-cache", "/data/caches", "/data");
    // Relative path resolved under cachePathPrefix.
    EXPECT_EQ(result.path, "/data/caches/my_cache");
}

TEST(FileCacheSettingsLoaderTest, ParseNamedCachePrefix)
{
    auto cfg = makeConfig({
        {"file-cache.my_store.path", "/named/cache"},
        {"file-cache.my_store.max-size", "536870912"},
    });
    FileCacheConfig result = FileCacheSettingsLoader::load(
        *cfg, "file-cache.my_store", "/prefix", "/named");
    EXPECT_EQ(result.path, "/named/cache");
    EXPECT_EQ(result.maxSize, 536'870'912u);
}

TEST(FileCacheSettingsLoaderTest, ParsePolicyCaseInsensitive)
{
    for (const char * spelling : {"lru", "LRU"})
    {
        auto cfg = makeConfig({
            {"file-cache.path", "/c"},
            {"file-cache.max-size", "1073741824"},
            {"file-cache.cache-policy", spelling},
        });
        FileCacheConfig result = FileCacheSettingsLoader::load(
            *cfg, "file-cache", "/c", "/");
        EXPECT_EQ(result.cachePolicy, FileCachePolicy::LRU);
    }
    for (const char * spelling : {"slru", "SLRU"})
    {
        auto cfg = makeConfig({
            {"file-cache.path", "/c"},
            {"file-cache.max-size", "1073741824"},
            {"file-cache.cache-policy", spelling},
        });
        FileCacheConfig result = FileCacheSettingsLoader::load(
            *cfg, "file-cache", "/c", "/");
        EXPECT_EQ(result.cachePolicy, FileCachePolicy::SLRU);
    }
}

TEST(FileCacheSettingsLoaderTest, ParseMaxSizeRatio)
{
    // Ratio-derived max-size test requires the cache directory to exist so that
    // std::filesystem::space() succeeds. Create a temp directory.
    auto tmpDir = fs::temp_directory_path() / "ch_cache_test_ratio";
    fs::create_directories(tmpDir);

    auto cfg = makeConfig({
        {"file-cache.path", tmpDir.string()},
        {"file-cache.max-size-ratio-to-total-space", "0.5"},
    });
    FileCacheConfig result = FileCacheSettingsLoader::load(
        *cfg, "file-cache", tmpDir.string(), tmpDir.string());
    auto totalSpace = fs::space(tmpDir).capacity;
    auto expectedMaxSize = static_cast<uint64_t>(
        std::floor(0.5 * static_cast<double>(totalSpace)));
    EXPECT_EQ(result.maxSize, expectedMaxSize);
    EXPECT_GT(result.maxSize, 0u);

    fs::remove_all(tmpDir);
}

// ── Validation error tests ────────────────────────────────────────────────────

TEST(FileCacheSettingsLoaderTest, MissingPath)
{
    auto cfg = makeConfig({{"file-cache.max-size", "1073741824"}});
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/p", "/"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, NeitherMaxSizeNorRatio)
{
    auto cfg = makeConfig({{"file-cache.path", "/cache"}});
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, BothMaxSizeAndRatio)
{
    auto cfg = makeConfig({
        {"file-cache.path", "/cache"},
        {"file-cache.max-size", "1073741824"},
        {"file-cache.max-size-ratio-to-total-space", "0.5"},
    });
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, ExplicitMaxSizeZero)
{
    auto cfg = makeConfig({
        {"file-cache.path", "/cache"},
        {"file-cache.max-size", "0"},
    });
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, RatioOutOfRange)
{
    for (const char * v : {"0", "1.1", "-0.1"})
    {
        auto cfg = makeConfig({
            {"file-cache.path", "/cache"},
            {"file-cache.max-size-ratio-to-total-space", v},
        });
        EXPECT_THROW(
            FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
            VeloxRuntimeError)
            << "ratio=" << v;
    }
}

TEST(FileCacheSettingsLoaderTest, MaxFileSegmentSizeZero)
{
    auto cfg = makeConfig({
        {"file-cache.path", "/cache"},
        {"file-cache.max-size", "1073741824"},
        {"file-cache.max-file-segment-size", "0"},
    });
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, BoundaryAlignmentExceedsMaxSegmentSize)
{
    auto cfg = makeConfig({
        {"file-cache.path", "/cache"},
        {"file-cache.max-size", "1073741824"},
        // boundary (8 MiB) > max-file-segment-size (4 MiB)
        {"file-cache.boundary-alignment", "8388608"},
        {"file-cache.max-file-segment-size", "4194304"},
    });
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, PathOutsideAllowedRoot)
{
    auto cfg = makeConfig({
        {"file-cache.path", "/etc/cache"},
        {"file-cache.max-size", "1073741824"},
    });
    EXPECT_THROW(
        FileCacheSettingsLoader::load(
            *cfg, "file-cache", "/data/caches", "/data/caches"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, UnknownKeyRejected)
{
    auto cfg = makeConfig({
        {"file-cache.path", "/cache"},
        {"file-cache.max-size", "1073741824"},
        {"file-cache.unknown-setting-xyz", "value"},
    });
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, WriteThoughUnsupported)
{
    auto cfg = makeConfig({
        {"file-cache.path", "/cache"},
        {"file-cache.max-size", "1073741824"},
        {"file-cache.cache-on-write-operations", "true"},
    });
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
        VeloxRuntimeError);
}

TEST(FileCacheSettingsLoaderTest, OvercommitPoliciesUnsupported)
{
    for (const char * policy : {"lru_overcommit", "LRU_OVERCOMMIT",
                                 "slru_overcommit", "SLRU_OVERCOMMIT"})
    {
        auto cfg = makeConfig({
            {"file-cache.path", "/cache"},
            {"file-cache.max-size", "1073741824"},
            {"file-cache.cache-policy", policy},
        });
        EXPECT_THROW(
            FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
            VeloxRuntimeError)
            << "policy=" << policy;
    }
}

TEST(FileCacheSettingsLoaderTest, SplitCacheWithOvercommitRejected)
{
    // useSplitCache cannot combine with overcommit policy. Even if the overcommit
    // rejection fires first, the intent of this test is the combination.
    auto cfg = makeConfig({
        {"file-cache.path", "/cache"},
        {"file-cache.max-size", "1073741824"},
        {"file-cache.use-split-cache", "true"},
        {"file-cache.cache-policy", "lru_overcommit"},
    });
    EXPECT_THROW(
        FileCacheSettingsLoader::load(*cfg, "file-cache", "/cache", "/"),
        VeloxRuntimeError);
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 3: Verify the test fails before implementation**

Reconfigure:

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`. For `home-chang`, also add
`-DVELOX_BUILD_TESTING=ON` (already present in the `root-oss` effective
configuration). Redirect output to `<velox_build_dir>/configure_task_010_settings.log`.

Then attempt to build:

```bash
if <ninja> \
  -C <velox_build_dir> \
  velox_ch_settings_test \
  > <velox_build_dir>/build_task_010_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected:

```text
Build fails because FileCacheSettings.h does not exist yet.
```

- [ ] **Step 4: Implement `FileCacheSettings.h`**

Create `velox/ch/Interpreters/FileCache/FileCacheSettings.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include "velox/ch/Interpreters/FileCache/FileCache_fwd.h"
#include "velox/common/config/Config.h"

#include <cstdint>
#include <string>

namespace facebook::velox::ch
{

/// Effective cache-instance configuration. All field defaults reference the
/// constants in `FileCache_fwd.h` to keep a single source of truth.
///
/// This is a plain value type: compiler-generated copy/move/equality.
/// `FileCacheSettings` is a type alias for this struct so that algorithm
/// files compiled from ClickHouse source change as little as possible.
struct FileCacheConfig
{
    std::string path;

    uint64_t maxSize = 0;
    uint64_t maxElements = FILECACHE_DEFAULT_MAX_ELEMENTS;
    uint64_t maxFileSegmentSize = FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE;
    uint64_t boundaryAlignment = FILECACHE_DEFAULT_FILE_SEGMENT_ALIGNMENT;
    uint64_t reserveGranularity = FILECACHE_DEFAULT_RESERVE_GRANULARITY;

    bool cacheOnWriteOperations = false;
    FileCachePolicy cachePolicy = FILECACHE_DEFAULT_CACHE_POLICY;
    double slruSizeRatio = FILECACHE_DEFAULT_SLRU_RATIO;

    uint64_t backgroundDownloadThreads =
        FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_THREADS;
    uint64_t backgroundDownloadQueueSizeLimit =
        FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_QUEUE_SIZE_LIMIT;
    uint64_t backgroundDownloadMaxFileSegmentSize =
        FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE_WITH_BACKGROUND_DOWLOAD;

    uint64_t loadMetadataThreads = FILECACHE_DEFAULT_LOAD_METADATA_THREADS;
    bool loadMetadataAsynchronously = false;

    double keepFreeSpaceSizeRatio = FILECACHE_DEFAULT_FREE_SPACE_SIZE_RATIO;
    double keepFreeSpaceElementsRatio =
        FILECACHE_DEFAULT_FREE_SPACE_ELEMENTS_RATIO;
    uint64_t keepFreeSpaceRemoveBatch = FILECACHE_DEFAULT_FREE_SPACE_REMOVE_BATCH;
    uint64_t keepFreeSpaceEvictionThreads =
        FILECACHE_DEFAULT_FREE_SPACE_EVICTION_THREADS;

    uint64_t invalidatedEntriesCleanupIntervalMs = 10'000;
    uint64_t invalidatedEntriesCleanupThreshold = 1'000;
    uint64_t invalidatedEntriesCleanupRemoveBatch =
        FILECACHE_DEFAULT_FREE_SPACE_REMOVE_BATCH;

    bool enableFilesystemQueryCacheLimit = false;
    uint64_t cacheHitsThreshold = 0;
    bool enableBypassCacheWithThreshold = false;
    uint64_t bypassCacheThreshold = FILECACHE_BYPASS_THRESHOLD;

    bool writeCachePerUserIdDirectory = false;
    bool allowDynamicCacheResize = false;
    uint64_t dynamicResizeLockWaitMs = 1'000;

    double maxSizeRatioToTotalSpace = 0;
    bool skipCacheOnDiskFailure = false;

    bool useSplitCache = false;
    double splitCacheRatio = 0.1;
    uint64_t overcommitEvictionEvictStep = 10ULL * 1024 * 1024;

    double checkCacheProbability = 0.001;

    uint64_t idleClientTtlSec = 7 * 24 * 60 * 60;
    uint64_t idleClientCheckIntervalSec = 0;
    uint64_t idleClientEvictionThreads = 4;

    bool exposePrometheusEvictionMetrics = false;
    bool exposePrometheusEvictionMetricsPerUser = false;

    bool operator==(const FileCacheConfig &) const = default;
};

/// Algorithm files use the CH name `FileCacheSettings`; the real type is
/// `FileCacheConfig`. This alias lets algorithm files compile with minimal diff.
using FileCacheSettings = FileCacheConfig;

/// Loads and validates a `FileCacheConfig` from a Velox `ConfigBase`.
///
/// Config key layout:
///   canonical:  file-cache.<name>.<key>
///   default:    file-cache.<key>
///
/// `cachePrefix` is the full prefix (e.g. "file-cache" or "file-cache.mystore").
/// `cachePathPrefix` is the directory prepended to relative paths.
/// `allowedCacheRoot` restricts the resolved absolute path; an exception is
/// thrown if the resolved path does not lie under this root.
///
/// Path resolution order:
///   missing path      -> exception
///   relative path     -> cachePathPrefix / path, then lexically normalised
///   verify absolute
///   verify lies under allowedCacheRoot
///
/// Max-size source: exactly one of max-size or max-size-ratio-to-total-space
/// must be present. If the ratio form is used, `std::filesystem::space(path)`
/// is called after the path is authorised and the directory may be created if
/// needed, to obtain total space for derivation.
struct FileCacheSettingsLoader
{
    static FileCacheConfig load(
        const config::ConfigBase & config,
        const std::string & cachePrefix,
        const std::string & cachePathPrefix,
        const std::string & allowedCacheRoot);
};

} // namespace facebook::velox::ch
```

- [ ] **Step 5: Implement `FileCacheReadOptions.h`**

Create `velox/ch/Interpreters/FileCache/FileCacheReadOptions.h` with the exact
request-scoped fields and defaults from
`port/3-consumers/01-filecache-read-context-design.md`:

```text
tempCacheOnly = false
readIfExistsOtherwiseBypass = false
allowBackgroundDownload = true
allowBackgroundDownloadForMetadataFilesInPackedStorage = true
allowBackgroundDownloadDuringFetch = true
preferBiggerBufferSize = true
segmentsBatchSize = 20
boundaryAlignment = nullopt
remoteFsBufferSize = 0
localFsBufferSize = 0
reserveSpaceWaitLockTimeoutMs = 0
maxDownloadSizePerQuery = 0
skipDownloadIfExceedsPerQueryCacheWriteLimit = true
enableFilesystemCacheLog = false
```

This header is a core prerequisite used by `QueryLimit` and `FileCache`; Task
014 adds request identity/file identity but does not redefine this type.

- [ ] **Step 6: Implement `FileCacheSettings.cpp`**

Create `velox/ch/Interpreters/FileCache/FileCacheSettings.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */

#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/ch/Common/FileCacheException.h"
#include "velox/ch/Common/FileCacheFilesystem.h"

#include <velox/common/base/Exceptions.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <unordered_map>

namespace facebook::velox::ch
{

namespace
{

// All config key names recognised under a cache prefix.
// An unknown key under the prefix is rejected.
const std::set<std::string> kKnownKeys = {
    "path",
    "max-size",
    "max-elements",
    "max-file-segment-size",
    "boundary-alignment",
    "reserve-granularity",
    "cache-on-write-operations",
    "cache-policy",
    "slru-size-ratio",
    "background-download-threads",
    "background-download-queue-size-limit",
    "background-download-max-file-segment-size",
    "load-metadata-threads",
    "load-metadata-asynchronously",
    "keep-free-space-size-ratio",
    "keep-free-space-elements-ratio",
    "keep-free-space-remove-batch",
    "keep-free-space-eviction-threads",
    "invalidated-entries-cleanup-interval-ms",
    "invalidated-entries-cleanup-threshold",
    "invalidated-entries-cleanup-remove-batch",
    "enable-filesystem-query-cache-limit",
    "cache-hits-threshold",
    "enable-bypass-cache-with-threshold",
    "bypass-cache-threshold",
    "write-cache-per-user-id-directory",
    "allow-dynamic-cache-resize",
    "dynamic-resize-lock-wait-ms",
    "max-size-ratio-to-total-space",
    "skip-cache-on-disk-failure",
    "use-split-cache",
    "split-cache-ratio",
    "overcommit-eviction-evict-step",
    "check-cache-probability",
    "idle-client-ttl-sec",
    "idle-client-check-interval-sec",
    "idle-client-eviction-threads",
    "expose-prometheus-eviction-metrics",
    "expose-prometheus-eviction-metrics-per-user",
};

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static FileCachePolicy parsePolicy(const std::string & s)
{
    const auto lower = toLower(s);
    if (lower == "lru")
        return FileCachePolicy::LRU;
    if (lower == "slru")
        return FileCachePolicy::SLRU;
    if (lower == "lru_overcommit" || lower == "lru-overcommit")
        return FileCachePolicy::LRU_OVERCOMMIT;
    if (lower == "slru_overcommit" || lower == "slru-overcommit")
        return FileCachePolicy::SLRU_OVERCOMMIT;
    throwFileCacheException(
        "Unknown cache policy '{}'. Supported: lru, slru, "
        "lru_overcommit, slru_overcommit",
        s);
}

template <typename T>
static T getOr(
    const std::unordered_map<std::string, std::string> & kv,
    const std::string & key,
    T defaultVal)
{
    auto it = kv.find(key);
    if (it == kv.end())
        return defaultVal;
    try
    {
        if constexpr (std::is_same_v<T, std::string>)
            return it->second;
        else if constexpr (std::is_same_v<T, bool>)
        {
            const auto & v = toLower(it->second);
            if (v == "true" || v == "1")
                return true;
            if (v == "false" || v == "0")
                return false;
            throwFileCacheException(
                "Cannot parse boolean for key '{}': '{}'", key, it->second);
        }
        else if constexpr (std::is_floating_point_v<T>)
            return static_cast<T>(std::stod(it->second));
        else if constexpr (std::is_integral_v<T>)
            return static_cast<T>(std::stoull(it->second));
    }
    catch (const VeloxRuntimeError &)
    {
        throw;
    }
    catch (const std::exception & e)
    {
        throwFileCacheException(
            "Cannot parse value for key '{}': {} — {}", key, it->second, e.what());
    }
    return defaultVal;
}

} // namespace

FileCacheConfig FileCacheSettingsLoader::load(
    const config::ConfigBase & config,
    const std::string & cachePrefix,
    const std::string & cachePathPrefix,
    const std::string & allowedCacheRoot)
{
    // Strip the prefix and expose the sub-keys as a flat map.
    const auto prefixWithDot = cachePrefix + ".";
    auto rawKv = config.rawConfigsWithPrefix(prefixWithDot);

    // Reject unknown keys.
    for (const auto & [key, _] : rawKv)
    {
        if (kKnownKeys.find(key) == kKnownKeys.end())
            throwFileCacheException(
                "Unknown cache configuration key '{}' under prefix '{}'",
                key, cachePrefix);
    }

    // ── Presence tracking ─────────────────────────────────────────────────────
    const bool hasPath = rawKv.count("path") > 0;
    const bool hasMaxSize = rawKv.count("max-size") > 0;
    const bool hasRatio = rawKv.count("max-size-ratio-to-total-space") > 0;

    // ── Path resolution ───────────────────────────────────────────────────────
    if (!hasPath)
        throwFileCacheException(
            "`path` is required for cache configuration under prefix '{}'",
            cachePrefix);

    std::string rawPath = rawKv.at("path");
    fs::path resolved = rawPath;
    if (resolved.is_relative())
    {
        if (cachePathPrefix.empty())
            throwFileCacheException(
                "Cache path '{}' is relative but no cachePathPrefix was provided",
                rawPath);
        resolved = fs::path(cachePathPrefix) / resolved;
    }
    resolved = resolved.lexically_normal();
    if (!resolved.is_absolute())
        throwFileCacheException(
            "Cache path '{}' did not resolve to an absolute path", rawPath);

    // Verify path lies under allowedCacheRoot before any filesystem side-effect.
    {
        const fs::path root = fs::path(allowedCacheRoot).lexically_normal();
        const auto [rootEnd, _ignore] =
            std::mismatch(root.begin(), root.end(), resolved.begin());
        if (rootEnd != root.end())
            throwFileCacheException(
                "Cache path '{}' (resolved to '{}') must lie under allowed "
                "root '{}'",
                rawPath, resolved.string(), allowedCacheRoot);
    }

    // ── Max-size source ───────────────────────────────────────────────────────
    if (!hasMaxSize && !hasRatio)
        throwFileCacheException(
            "Either `max-size` or `max-size-ratio-to-total-space` must be "
            "defined under cache prefix '{}'",
            cachePrefix);
    if (hasMaxSize && hasRatio)
        throwFileCacheException(
            "`max-size` and `max-size-ratio-to-total-space` cannot both be "
            "specified under cache prefix '{}'",
            cachePrefix);

    // ── Populate config ───────────────────────────────────────────────────────
    FileCacheConfig cfg;
    cfg.path = resolved.string();

    if (hasMaxSize)
    {
        cfg.maxSize = getOr<uint64_t>(rawKv, "max-size", 0);
        if (cfg.maxSize == 0)
            throwFileCacheException(
                "`max-size` cannot be 0 under cache prefix '{}'", cachePrefix);
    }

    cfg.maxElements =
        getOr(rawKv, "max-elements", cfg.maxElements);
    cfg.maxFileSegmentSize =
        getOr(rawKv, "max-file-segment-size", cfg.maxFileSegmentSize);
    cfg.boundaryAlignment =
        getOr(rawKv, "boundary-alignment", cfg.boundaryAlignment);
    cfg.reserveGranularity =
        getOr(rawKv, "reserve-granularity", cfg.reserveGranularity);
    cfg.cacheOnWriteOperations =
        getOr(rawKv, "cache-on-write-operations", cfg.cacheOnWriteOperations);
    cfg.slruSizeRatio = getOr(rawKv, "slru-size-ratio", cfg.slruSizeRatio);
    cfg.backgroundDownloadThreads =
        getOr(rawKv, "background-download-threads", cfg.backgroundDownloadThreads);
    cfg.backgroundDownloadQueueSizeLimit =
        getOr(rawKv, "background-download-queue-size-limit",
              cfg.backgroundDownloadQueueSizeLimit);
    cfg.backgroundDownloadMaxFileSegmentSize =
        getOr(rawKv, "background-download-max-file-segment-size",
              cfg.backgroundDownloadMaxFileSegmentSize);
    cfg.loadMetadataThreads =
        getOr(rawKv, "load-metadata-threads", cfg.loadMetadataThreads);
    cfg.loadMetadataAsynchronously =
        getOr(rawKv, "load-metadata-asynchronously", cfg.loadMetadataAsynchronously);
    cfg.keepFreeSpaceSizeRatio =
        getOr(rawKv, "keep-free-space-size-ratio", cfg.keepFreeSpaceSizeRatio);
    cfg.keepFreeSpaceElementsRatio =
        getOr(rawKv, "keep-free-space-elements-ratio", cfg.keepFreeSpaceElementsRatio);
    cfg.keepFreeSpaceRemoveBatch =
        getOr(rawKv, "keep-free-space-remove-batch", cfg.keepFreeSpaceRemoveBatch);
    cfg.keepFreeSpaceEvictionThreads =
        getOr(rawKv, "keep-free-space-eviction-threads", cfg.keepFreeSpaceEvictionThreads);
    cfg.invalidatedEntriesCleanupIntervalMs =
        getOr(rawKv, "invalidated-entries-cleanup-interval-ms",
              cfg.invalidatedEntriesCleanupIntervalMs);
    cfg.invalidatedEntriesCleanupThreshold =
        getOr(rawKv, "invalidated-entries-cleanup-threshold",
              cfg.invalidatedEntriesCleanupThreshold);
    cfg.invalidatedEntriesCleanupRemoveBatch =
        getOr(rawKv, "invalidated-entries-cleanup-remove-batch",
              cfg.invalidatedEntriesCleanupRemoveBatch);
    cfg.enableFilesystemQueryCacheLimit =
        getOr(rawKv, "enable-filesystem-query-cache-limit",
              cfg.enableFilesystemQueryCacheLimit);
    cfg.cacheHitsThreshold =
        getOr(rawKv, "cache-hits-threshold", cfg.cacheHitsThreshold);
    cfg.enableBypassCacheWithThreshold =
        getOr(rawKv, "enable-bypass-cache-with-threshold",
              cfg.enableBypassCacheWithThreshold);
    cfg.bypassCacheThreshold =
        getOr(rawKv, "bypass-cache-threshold", cfg.bypassCacheThreshold);
    cfg.writeCachePerUserIdDirectory =
        getOr(rawKv, "write-cache-per-user-id-directory",
              cfg.writeCachePerUserIdDirectory);
    cfg.allowDynamicCacheResize =
        getOr(rawKv, "allow-dynamic-cache-resize", cfg.allowDynamicCacheResize);
    cfg.dynamicResizeLockWaitMs =
        getOr(rawKv, "dynamic-resize-lock-wait-ms", cfg.dynamicResizeLockWaitMs);
    cfg.maxSizeRatioToTotalSpace =
        getOr(rawKv, "max-size-ratio-to-total-space", cfg.maxSizeRatioToTotalSpace);
    cfg.skipCacheOnDiskFailure =
        getOr(rawKv, "skip-cache-on-disk-failure", cfg.skipCacheOnDiskFailure);
    cfg.useSplitCache =
        getOr(rawKv, "use-split-cache", cfg.useSplitCache);
    cfg.splitCacheRatio =
        getOr(rawKv, "split-cache-ratio", cfg.splitCacheRatio);
    cfg.overcommitEvictionEvictStep =
        getOr(rawKv, "overcommit-eviction-evict-step", cfg.overcommitEvictionEvictStep);
    cfg.checkCacheProbability =
        getOr(rawKv, "check-cache-probability", cfg.checkCacheProbability);
    cfg.idleClientTtlSec =
        getOr(rawKv, "idle-client-ttl-sec", cfg.idleClientTtlSec);
    cfg.idleClientCheckIntervalSec =
        getOr(rawKv, "idle-client-check-interval-sec", cfg.idleClientCheckIntervalSec);
    cfg.idleClientEvictionThreads =
        getOr(rawKv, "idle-client-eviction-threads", cfg.idleClientEvictionThreads);
    cfg.exposePrometheusEvictionMetrics =
        getOr(rawKv, "expose-prometheus-eviction-metrics",
              cfg.exposePrometheusEvictionMetrics);
    cfg.exposePrometheusEvictionMetricsPerUser =
        getOr(rawKv, "expose-prometheus-eviction-metrics-per-user",
              cfg.exposePrometheusEvictionMetricsPerUser);

    if (rawKv.count("cache-policy") > 0)
        cfg.cachePolicy = parsePolicy(rawKv.at("cache-policy"));

    // ── Ratio-derived max-size ────────────────────────────────────────────────
    if (hasRatio)
    {
        if (cfg.maxSizeRatioToTotalSpace <= 0.0 || cfg.maxSizeRatioToTotalSpace > 1.0)
            throwFileCacheException(
                "`max-size-ratio-to-total-space` must be in (0, 1]; got {}",
                cfg.maxSizeRatioToTotalSpace);

        fs::create_directories(resolved);
        const auto spaceInfo = fs::space(resolved);
        cfg.maxSize = static_cast<uint64_t>(
            std::floor(cfg.maxSizeRatioToTotalSpace
                       * static_cast<double>(spaceInfo.capacity)));
        if (cfg.maxSize == 0)
            throwFileCacheException(
                "Ratio-derived max-size is 0 (ratio={}, total_space={}); "
                "increase max-size-ratio-to-total-space",
                cfg.maxSizeRatioToTotalSpace, spaceInfo.capacity);
    }

    // ── Validation ────────────────────────────────────────────────────────────

    // Phase-1 unsupported features: fail fast with a clear message.
    if (cfg.cacheOnWriteOperations)
        throwFileCacheException(
            "cache_on_write_operations is not supported in the Velox FileCache "
            "port (first phase). Remove it from the cache configuration.");

    if (cfg.cachePolicy == FileCachePolicy::LRU_OVERCOMMIT
        || cfg.cachePolicy == FileCachePolicy::SLRU_OVERCOMMIT)
        throwFileCacheException(
            "Overcommit cache policies (LRU_OVERCOMMIT, SLRU_OVERCOMMIT) are "
            "not supported in the Velox FileCache port (first phase). "
            "Use LRU or SLRU instead.");

    // Fail-fast safety checks.
    if (cfg.maxFileSegmentSize == 0)
        throwFileCacheException(
            "`max-file-segment-size` cannot be 0; it would cause splitRange() "
            "to make no progress");

    if (cfg.boundaryAlignment > cfg.maxFileSegmentSize)
        throwFileCacheException(
            "`boundary-alignment` ({}) must be <= `max-file-segment-size` ({})",
            cfg.boundaryAlignment, cfg.maxFileSegmentSize);

    if (cfg.overcommitEvictionEvictStep == 0)
        throwFileCacheException(
            "`overcommit-eviction-evict-step` cannot be zero");

    if (cfg.useSplitCache
        && (cfg.cachePolicy == FileCachePolicy::LRU_OVERCOMMIT
            || cfg.cachePolicy == FileCachePolicy::SLRU_OVERCOMMIT))
        throwFileCacheException(
            "`use-split-cache` cannot be combined with overcommit policies");

    if (cfg.loadMetadataThreads == 0)
        throwFileCacheException("`load-metadata-threads` cannot be zero");

    if (cfg.keepFreeSpaceEvictionThreads == 0)
        throwFileCacheException("`keep-free-space-eviction-threads` cannot be zero");

    if (cfg.invalidatedEntriesCleanupIntervalMs == 0)
        throwFileCacheException(
            "`invalidated-entries-cleanup-interval-ms` cannot be zero");

    if (cfg.invalidatedEntriesCleanupThreshold == 0)
        throwFileCacheException(
            "`invalidated-entries-cleanup-threshold` cannot be zero");

    if (cfg.invalidatedEntriesCleanupRemoveBatch == 0)
        throwFileCacheException(
            "`invalidated-entries-cleanup-remove-batch` cannot be zero");

    if (cfg.idleClientEvictionThreads == 0)
        throwFileCacheException("`idle-client-eviction-threads` cannot be zero");

    return cfg;
}

} // namespace facebook::velox::ch
```

- [ ] **Step 7: Add the source and request-options header to `CMakeLists.txt`**

Append `FileCacheSettings.cpp` to the source list in
`velox/ch/Interpreters/FileCache/CMakeLists.txt`:

```cmake
target_sources(
  velox_ch_filecache
  PRIVATE
    FileSegmentKeyType.cpp
    FileCacheKey.cpp
    FileCacheSettings.cpp
  PUBLIC
    FILE_SET HEADERS
    FILES
      FileCacheReadOptions.h
)
```

Also add `velox_common_config` to the link dependencies in
`velox/ch/Common/CMakeLists.txt`:

```cmake
velox_link_libraries(
  velox_ch_filecache
  PUBLIC
    velox_common_config
    velox_exception
    Folly::folly
    fmt::fmt
)
```

- [ ] **Step 8: Build the focused test**

Reconfigure with the same command as Step 3, then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_settings_test \
  > <velox_build_dir>/build_task_010_settings.log 2>&1
```

Expected:

```text
Exit code 0.
```

- [ ] **Step 9: Run the focused test**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_settings_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_010_settings.log 2>&1
```

Expected:

```text
100% tests passed, 0 tests failed.
```

- [ ] **Step 10: Inspect only task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/FileCacheSettings.h \
  velox/ch/Interpreters/FileCache/FileCacheSettings.cpp \
  velox/ch/Interpreters/FileCache/FileCacheReadOptions.h \
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/tests/FileCacheSettingsTest.cpp
```

Expected:

```text
No whitespace errors.
No files outside the declared scope were changed by this task.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 11: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/010-filecache-settings-result.md
```

Use exactly this structure:

````markdown
# Task 010 Result: `FileCache` Settings — `FileCacheConfig` and `FileCacheSettingsLoader`

## Status

status: success

## Velox status

```text
<paste branch, HEAD, and final `git status --short`>
```

## Files changed

```text
<list only task-owned files>
```

## Commands run

```text
<paste configure, build, test, and verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_task_010_settings.log
<velox_build_dir>/build_task_010_red.log
<velox_build_dir>/build_task_010_settings.log
<velox_build_dir>/test_task_010_settings.log
```

## Verification

```text
Red build failed because FileCacheSettings.h was absent.
Final build exit code: 0
Focused test result: 100% passed
git diff --check result: no whitespace errors
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 011: port CacheUsage, IFileCachePriority, LRU/SLRU/Split priorities, and EvictionCandidates.
```
````

## Explicit exclusions

Do not implement in this task:

```text
CacheUsage.h
IFileCachePriority.h / .cpp
LRUFileCachePriority.h / .cpp
SLRUFileCachePriority.h / .cpp
SplitFileCachePriority.h / .cpp
EvictionCandidates.h / .cpp
FileSegment / Metadata / FileCache
FileCacheManager (Task 013)
```

Settings reload (`applySettingsIfPossible`) is part of `FileCache.h/.cpp` and
belongs to the center SCC tasks (012+). Do not implement it here.
