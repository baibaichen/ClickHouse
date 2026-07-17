# Task 014: `FileCacheBufferedInput` and `FileCacheInputStream` (Velox only)

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `/home/chang/OpenSource/velox` and writes one
> result file under this ClickHouse checkout. Do not modify ClickHouse source
> files. **Do not modify any Gluten file.** Do not commit or stage either
> repository.

## Goal

Implement the Velox scan read path for the ClickHouse `FileCache`:

```text
FileCacheReadOptions          (one-read parameters)
FileCacheRequestContext       (query/user/origin identity)
FileCacheFileIdentity         (path + etag for cache key derivation)
FileCacheBufferedInput        (BufferedInput subclass, Velox layer)
FileCacheInputStream          (SeekableInputStream, streaming state machine)
```

No Gluten file is touched. The `GlutenBufferedInputBuilder` integration is a
separate task. The deliverable is a compiled and tested
`velox_ch_filecache_dwio` library and a
`velox_ch_filecache_buffered_input_test` executable.

## Starting Point

```text
Velox repository: /home/chang/OpenSource/velox
Required branch:  filecache
Expected predecessors:
  Task 007: ReadBufferFromVeloxReadFile, WriteBufferFromVeloxWriteFile
  Task 013: FileCacheFactory, FileCacheManager (and all of 012's SCC)
```

Do not require a clean worktree. Stop if the branch is not `filecache`.

## Design References

Read before editing:

```text
port/task/ENVIRONMENT.md
port/3-consumers/01-filecache-read-context-design.md
port/3-consumers/03-filecache-buffered-input-design.md
port/01-filecache-port-order-design.md  (阶段 6)
port/1-dependencies/01-filecache-infra-mapping.md
```

## File Scope

**This task creates files only inside the Velox repository. No Gluten files are
created or modified.**

Modify:

```text
/home/chang/OpenSource/velox/velox/ch/CMakeLists.txt
```

Create:

```text
/home/chang/OpenSource/velox/velox/ch/Disks/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Disks/IO/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheRequestContext.h
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheFileIdentity.h
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheBufferedInput.h
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheBufferedInput.cpp
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheInputStream.h
/home/chang/OpenSource/velox/velox/ch/Disks/IO/FileCacheInputStream.cpp
/home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/CMakeLists.txt
/home/chang/OpenSource/velox/velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp
/home/chang/SourceCode/ClickHouse/port/task/result/014-filecache-buffered-input-result.md
```

Every new Velox C++ and CMake file must begin with the Apache 2.0 license
header. Use `/* ... */` for C++ and `#` for CMake.

## Key Behavioral Invariants

These invariants are the central correctness constraints for this task.
Every test case below exercises at least one of them.

### Lazy `Next` — `load` never dereferences a stream

`enqueue` transfers ownership of the `FileCacheInputStream` via `unique_ptr` to
the caller. The `load` method must not store or dereference any stream pointer.
An implementation that saves `stream.get()` and calls any method on it in
`load` produces use-after-free when the caller discards the returned pointer.

```text
enqueue(region) -> unique_ptr<SeekableInputStream>
  creates FileCacheInputStream
  records copied region value in requests_
  returns stream to caller

load(logType)
  planning barrier over copied request values only
  does not dereference any stream
  clears or retains requests_ for next cycle
```

`FileSegmentsHolder` is acquired lazily inside `Next` → `initializeIfNeeded`,
preserving ClickHouse's on-demand downloader semantics.

### Region-relative position, absolute cache offset

All position-related stream API (`ByteCount`, `BackUp`, `SkipInt64`,
`seekToPosition`) operate in **region-relative** coordinates.
All `FileCache`/`FileSegment`/`ReadFile` calls use **absolute** file offsets:

```text
absolutePosition = checkedAdd(region_.offset, position_)
absoluteReadUntil = checkedAdd(region_.offset, region_.length)
```

`ReadInfo::readUntilPosition` stores the absolute end of the region, not the
whole-file size.

### `DWRF shouldPrefetchStripes = false`

`shouldPrefetchStripes` must return `false`. DWRF's `StripeMetadataCache`
hard-casts `SeekableInputStream*` to `CacheInputStream*` on the
stripe-prefetch path. A `FileCacheInputStream` is not a `CacheInputStream`; the
cast is undefined behavior. Returning `false` forces DWRF to use the
buffer-copy stripe metadata path, which is compatible.

```text
shouldPrefetchStripes() const -> false
preload()               -> no-op
preloaded() const       -> false
shouldPreload()         -> false
```

### `hasCache = false`

`hasCache()` must return `false`. Returning `true` would advertise
`CachePin`-compatible backing (`cacheRegion`, `findCachedRegion`). Those APIs
use `CachedRegion`, which cannot represent a `FileSegment`. The mutual
exclusion between the `FileCache` and `AsyncDataCache` paths is enforced by
builder selection, not by this flag.

```text
hasCache() const -> false
executor()       -> executor_ (injected at construction)
```

### `isBuffered` uses no-create `get`

`isBuffered(offset, length)` must call `FileCache::get` (not `getOrSet`). It
must not create metadata, acquire a downloader, or reserve space.

### `seekToPosition` seek-buffer fast path

If the new position lands inside the already-filled output buffer, the seek is
O(1) and must not reset the `FileSegmentsHolder` or downloader:

```text
outputBufferStart_ <= newPosition
  && newPosition < outputBufferStart_ + outputBufferSize_:

  position_ = newPosition
  offsetInOutputBuffer_ = newPosition - outputBufferStart_
  holder / downloader / state_ unchanged
```

If the new position is outside the buffer, reset `ReadInfo`, `state_`,
`initialized_`, and release any held downloader:

```text
position_ = newPosition
readInfo_.reset()
state_.reset()
initialized_ = false
```

`queryContextHolder_` is **never** reset by `seekToPosition`; it is created
once in the constructor and lives until the stream is destroyed.

### Query holder lifetime

`FileCacheInputStream` acquires `QueryContextHolder` in its constructor and
holds it until destruction:

```text
constructor:
  queryContextHolder_ = owner_->fileCache().getQueryContextHolder(
      cacheContext_.queryId, owner_->cacheOptions())

destructor:
  if holding downloader: completePartAndResetDownloader
  readInfo_.reset()   // completeAndPopFront all segments
```

The holder must still be alive during the final `readInfo_.reset()` call because
`QueryContextHolder` outlives individual segment completions.

### Path-only key when etag is empty

```text
etag.empty():
  FileCacheKey::fromPath(path)

etag non-empty:
  FileCacheKey::fromKey(SipHash128(path + etag))
```

Two different non-empty etags for the same path must produce different keys.
The builder test must verify both cases.

## Steps

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd /home/chang/OpenSource/velox
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected: branch `filecache`, `velox_ch_filecache_manager` links.
Record pre-existing dirty files in the result file.

- [ ] **Step 2: Write the failing test file (red)**

Create `velox/ch/Disks/IO/tests/CMakeLists.txt`:

```cmake
add_executable(
  velox_ch_filecache_buffered_input_test
  FileCacheBufferedInputTest.cpp
)
add_test(
  velox_ch_filecache_buffered_input_test
  velox_ch_filecache_buffered_input_test
)

target_link_libraries(
  velox_ch_filecache_buffered_input_test
  PRIVATE
    velox_ch_filecache_dwio
    velox_ch_filecache_manager
    velox_ch_filecache_core
    velox_ch_filecache
    velox_test_util
    velox_exception
    velox_file
    velox_memory
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

Create
`velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp`:

```cpp
#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Disks/IO/FileCacheInputStream.h"
#include "velox/ch/Interpreters/FileCache/FileCacheReadOptions.h"
#include "velox/ch/Disks/IO/FileCacheRequestContext.h"
#include "velox/ch/Disks/IO/FileCacheFileIdentity.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCache.h"
#include "velox/common/testutil/TempDirectoryPath.h"
#include "velox/dwio/common/BufferedInput.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace facebook::velox::ch
{
namespace
{

using common::testutil::TempDirectoryPath;

// ── Identity helpers ─────────────────────────────────────────────────────────

TEST(FileCacheFileIdentityTest, EmptyEtagUsesPathKey)
{
    FileCacheFileIdentity id{"/some/path/data.orc", ""};
    auto key = FileCacheFileIdentity::deriveKey(id);
    EXPECT_EQ(key, FileCacheKey::fromPath("/some/path/data.orc"));
}

TEST(FileCacheFileIdentityTest, NonEmptyEtagUsesSipHashKey)
{
    FileCacheFileIdentity id{"/some/path/data.orc", "etag-v1"};
    auto key1 = FileCacheFileIdentity::deriveKey(id);
    EXPECT_NE(key1, FileCacheKey::fromPath("/some/path/data.orc"));

    FileCacheFileIdentity id2{"/some/path/data.orc", "etag-v2"};
    auto key2 = FileCacheFileIdentity::deriveKey(id2);
    EXPECT_NE(key1, key2);
}

// ── BufferedInput contract ────────────────────────────────────────────────────

class FileCacheBufferedInputTest : public ::testing::Test
{
protected:
    // Create a minimal in-process cache backed by a temp directory.
    std::shared_ptr<FileCacheBufferedInput> makeInput(
        std::shared_ptr<ReadFile> readFile,
        uint64_t fileSize)
    {
        // setup: create FileCacheManager, get default cache, build input
        // omitted for brevity; see design doc for full constructor
        (void)readFile;
        (void)fileSize;
        return nullptr; // replace with real construction
    }
};

TEST_F(FileCacheBufferedInputTest, DiscardEnqueueResultBeforeLoad)
{
    // enqueue returns a unique_ptr; discard it immediately.
    // load() must not access the freed stream.
    // This test verifies no use-after-free or assertion failure.
    auto dir = TempDirectoryPath::create();
    // (construct a FileCacheBufferedInput backed by a mock ReadFile)
    // auto input = makeInput(...);
    // velox::common::Region region{0, 4096};
    // { auto stream = input->enqueue(region); }  // stream destroyed here
    // EXPECT_NO_THROW(input->load(dwio::common::LogType::FOOTER));
}

TEST_F(FileCacheBufferedInputTest, LoadDoesNotCreateFileSegment)
{
    // After enqueue + discard + load, the cache must contain no metadata
    // for the requested region.
}

TEST_F(FileCacheBufferedInputTest, PreloadLeavesPreloadedFalse)
{
    // preload() is a no-op; preloaded() remains false.
    // auto input = makeInput(...);
    // input->preload();
    // EXPECT_FALSE(input->preloaded());
}

TEST_F(FileCacheBufferedInputTest, ShouldPrefetchStripesFalse)
{
    // shouldPrefetchStripes() must return false.
    // EXPECT_FALSE(input->shouldPrefetchStripes());
}

TEST_F(FileCacheBufferedInputTest, HasCacheFalse)
{
    // hasCache() must return false.
    // EXPECT_FALSE(input->hasCache());
}

TEST_F(FileCacheBufferedInputTest, ExecutorReturnsInjected)
{
    // executor() must return the executor passed to the constructor.
}

TEST_F(FileCacheBufferedInputTest, IsBufferedMissDoesNotCreateMetadata)
{
    // isBuffered(offset, length) on a cold cache must return false
    // and must not create any FileSegment metadata.
}

// ── Stream coordinates and seek ──────────────────────────────────────────────

TEST_F(FileCacheBufferedInputTest, NonZeroRegionOffsetBytecountRelative)
{
    // region.offset = 1024; ByteCount() returns region-relative position.
    // FileCache calls use region.offset + position as absolute offset.
}

TEST_F(FileCacheBufferedInputTest, BackUpWithinOutputBuffer)
{
    // BackUp(n) where n <= offsetInOutputBuffer_ succeeds.
    // BackUp beyond the last Next buffer is undefined and must not be called.
}

TEST_F(FileCacheBufferedInputTest, SeekWithinCurrentBufferFastPath)
{
    // seekToPosition to a position inside the current output buffer:
    // position_ updated, holder and downloader NOT reset.
}

TEST_F(FileCacheBufferedInputTest, SeekOutsideBufferResetsState)
{
    // seekToPosition to a position outside the current output buffer:
    // ReadInfo and state_ reset, initialized_ = false.
    // queryContextHolder_ is NOT reset.
}

TEST_F(FileCacheBufferedInputTest, RandomRowGroupSeekCorrectness)
{
    // Simulate an ORC/Parquet reader seeking to non-sequential row groups.
    // After each seek, the next Next() must return the correct bytes at
    // the absolute offset corresponding to the seeked region-relative position.
}

// ── Cache miss -> remote read -> cache write -> later hit ────────────────────

TEST_F(FileCacheBufferedInputTest, CacheMissThenHit)
{
    // First scan: cache miss -> remote read -> data written to cache.
    // Second scan with a new FileCacheInputStream on same region:
    // -> cache hit -> no remote read.
}

TEST_F(FileCacheBufferedInputTest, BypassMode)
{
    // FileCacheReadOptions::readIfExistsOtherwiseBypass = true.
    // On miss, Next() reads remote directly without creating a segment.
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 3: Verify the red build**

Configure:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_BUILD_TESTING=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_014_buffered_input.log 2>&1
```

Then:

```bash
if /home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_filecache_buffered_input_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected: configure succeeds, build fails because the new headers do not exist.

- [ ] **Step 4: Implement helper headers**

`FileCacheReadOptions` already exists under
`velox/ch/Interpreters/FileCache/FileCacheReadOptions.h` from Task 010 because
the core/query-limit APIs require it. Reuse that type; do not create a second
copy under `Disks/IO`.

### `FileCacheRequestContext.h`

```cpp
#pragma once

#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Interpreters/FileCache/FileSegmentKeyType.h"
#include <cstdint>
#include <string>

namespace facebook::velox::ch
{

struct FileCacheRequestContext
{
    std::string queryId;
    std::string userId;
    uint64_t userWeight = 0;
    bool cacheable = true;
    FileSegmentKeyType segmentType = FileSegmentKeyType::Data;
};

} // namespace facebook::velox::ch
```

Default Hive builder mapping:

```text
queryId     <- ConnectorQueryCtx::queryId
userId      <- FileCacheManager::commonUserId (stable process identity)
userWeight  <- 0 (default weight until per-user eviction is enabled)
cacheable   <- ReaderOptions::cacheable
segmentType <- FileSegmentKeyType::Data (default)
```

`userId` must not be derived from `driverId` or `scanId`; `ConnectorQueryCtx`
currently provides no real user identity. Use the stable Manager identity.

### `FileCacheFileIdentity.h`

```cpp
#pragma once

#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Common/SipHash128.h"
#include <string>

namespace facebook::velox::ch
{

struct FileCacheFileIdentity
{
    std::string path;
    std::string etag;

    // Derive cache key:
    //   etag empty  -> FileCacheKey::fromPath(path)
    //   etag non-empty -> FileCacheKey::fromKey(SipHash128(path + etag))
    static FileCacheKey deriveKey(const FileCacheFileIdentity & id);
};

} // namespace facebook::velox::ch
```

The key derivation is the single source of truth for this task.
Any future object-storage plumbing that supplies a real etag must go through
`deriveKey`, not bypass it.

- [ ] **Step 5: Implement `FileCacheBufferedInput.h`**

```cpp
#pragma once

#include "velox/ch/Disks/IO/FileCacheFileIdentity.h"
#include "velox/ch/Interpreters/FileCache/FileCacheReadOptions.h"
#include "velox/ch/Disks/IO/FileCacheRequestContext.h"
#include "velox/ch/Interpreters/FileCache/FileCache.h"
#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h"
#include "velox/dwio/common/BufferedInput.h"
#include "velox/common/io/IoStatistics.h"
#include "velox/dwio/common/Options.h"

#include <folly/container/F14Map.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <memory>
#include <vector>

namespace facebook::velox::ch
{

class FileCacheInputStream;

class FileCacheBufferedInput : public dwio::common::BufferedInput
{
public:
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

    // BufferedInput overrides.
    std::unique_ptr<dwio::common::SeekableInputStream> enqueue(
        velox::common::Region region,
        const dwio::common::StreamIdentifier * sid = nullptr) override;

    void load(dwio::common::LogType logType) override;

    std::unique_ptr<dwio::common::SeekableInputStream>
    read(uint64_t offset, uint64_t length, dwio::common::LogType logType)
        const override;

    bool isBuffered(uint64_t offset, uint64_t length) const override;

    void preload() override {}
    bool preloaded() const override { return false; }
    bool shouldPreload(int32_t numPages = 0) override { return false; }

    // Must return false: prevents DWRF StripeMetadataCache from hard-casting
    // FileCacheInputStream to CacheInputStream.
    bool shouldPrefetchStripes() const override { return false; }

    std::unique_ptr<dwio::common::BufferedInput> clone() const override;

    // Returns the injected executor; does not return a Manager-owned pool.
    folly::Executor * executor() const override { return executor_; }

    // Returns false: FileCacheInputStream is not CachePin-compatible.
    bool hasCache() const override { return false; }

    // Accessors for FileCacheInputStream.
    FileCache & fileCache() const { return *cache_; }
    const std::shared_ptr<ReadFile> & sourceReadFile() const
    {
        return sourceReadFile_;
    }
    const FileCacheKey & cacheKey() const { return cacheKey_; }
    const FileCacheOriginInfo & origin() const { return origin_; }
    const FileCacheReadOptions & cacheOptions() const { return cacheOptions_; }
    uint64_t fileSize() const { return fileSize_; }

private:
    struct Request
    {
        velox::common::Region region;
        const dwio::common::StreamIdentifier * sid = nullptr;
    };

    std::shared_ptr<ReadFile> sourceReadFile_;
    FileCachePtr cache_;
    FileCacheKey cacheKey_;
    FileCacheOriginInfo origin_;
    FileCacheReadOptions cacheOptions_;
    FileCacheRequestContext requestContext_;
    std::shared_ptr<io::IoStatistics> ioStatistics_;
    std::shared_ptr<velox::IoStats> ioStats_;
    folly::Executor * executor_;
    dwio::common::ReaderOptions readerOptions_;
    uint64_t fileSize_;

    // Copied region values only; never stream pointers.
    std::vector<Request> requests_;
};

} // namespace facebook::velox::ch
```

Constructor delegates to `BufferedInput` base:

```cpp
dwio::common::BufferedInput(
    readFile,
    readerOptions.memoryPool(),
    metricsLog,
    ioStatistics.get(),
    ioStats.get(),
    dwio::common::BufferedInput::kMaxMergeDistance,
    std::nullopt,
    fileReadOps,
    requestContext.cacheable)
```

`readerOptions.memoryPool()` must be non-null.

- [ ] **Step 6: Implement `FileCacheInputStream.h`**

```cpp
#pragma once

#include "velox/ch/Disks/IO/FileCacheRequestContext.h"
#include "velox/ch/Interpreters/FileCache/FileCache.h"
#include "velox/ch/Interpreters/FileCache/FileSegment.h"
#include "velox/ch/Interpreters/FileCache/QueryLimit.h"
#include "velox/ch/IO/ReadBufferFromVeloxReadFile.h"
#include "velox/dwio/common/SeekableInputStream.h"
#include "velox/common/file/Region.h"
#include "velox/common/memory/Memory.h"

#include <memory>

namespace facebook::velox::ch
{

class FileCacheBufferedInput;

class FileCacheInputStream : public dwio::common::SeekableInputStream
{
public:
    FileCacheInputStream(
        FileCacheBufferedInput * owner,
        velox::common::Region region,
        FileCacheRequestContext cacheContext,
        dwio::common::LogType logType);

    ~FileCacheInputStream() override;

    bool Next(const void ** data, int * size) override;
    void BackUp(int count) override;
    bool SkipInt64(int64_t count) override;
    int64_t ByteCount() const override;
    void seekToPosition(dwio::common::PositionProvider & position) override;
    std::string getName() const override;
    size_t positionSize() const override;

private:
    struct ReadInfo
    {
        FileSegmentsHolderPtr fileSegments;
        std::shared_ptr<ReadBufferFromVeloxReadFile> remoteReader;
        std::shared_ptr<ReadBufferFromVeloxReadFile> cacheReader;
        // Absolute position: region.offset + region.length.
        uint64_t readUntilPosition = 0;

        void reset();
    };

    enum class ReadType : uint8_t
    {
        CACHED,
        REMOTE_FS_READ_BYPASS_CACHE,
        REMOTE_FS_READ_AND_PUT_IN_CACHE,
        NONE,
    };

    struct ReadFromFileSegmentState
    {
        std::shared_ptr<ReadBufferFromVeloxReadFile> reader;
        ReadType readType = ReadType::NONE;
        uint64_t bytesToPredownload = 0;
        velox::memory::BufferPtr predownloadBuffer;
    };

    void initializeIfNeeded();
    bool nextFileSegmentsBatch();

    std::unique_ptr<ReadFromFileSegmentState> prepareReadFromFileSegmentState(
        FileSegment & fileSegment,
        uint64_t offset);

    std::unique_ptr<ReadFromFileSegmentState> createReadFromFileSegmentState(
        FileSegment & fileSegment,
        uint64_t offset);

    std::shared_ptr<ReadBufferFromVeloxReadFile> getCacheReadBuffer(
        const FileSegment & fileSegment);

    std::shared_ptr<ReadBufferFromVeloxReadFile> getRemoteReadBuffer(
        FileSegment & fileSegment,
        uint64_t offset,
        ReadType readType);

    bool canStartFromCache(
        uint64_t offset,
        const FileSegment & fileSegment) const;

    void updateReadStateIfNeeded(
        FileSegment & fileSegment,
        uint64_t offset);

    size_t readFromCurrentSegment();
    bool predownloadForCurrentSegment(FileSegment & fileSegment);
    bool writeCache(
        char * data,
        size_t size,
        uint64_t offset,
        FileSegment & fileSegment);
    void completeCurrentSegmentAndAdvance();
    void releaseDownloaderIfNeeded();

    uint64_t absolutePosition() const
    {
        return region_.offset + position_;
    }

    FileCacheBufferedInput * owner_;
    velox::common::Region region_;
    FileCacheRequestContext cacheContext_;
    // Acquired once in constructor; never reset by seekToPosition.
    FileCache::QueryContextHolderPtr queryContextHolder_;
    dwio::common::LogType logType_;

    // All positions below are region-relative.
    uint64_t position_ = 0;
    uint64_t outputBufferStart_ = 0;
    velox::memory::BufferPtr outputBuffer_;
    size_t offsetInOutputBuffer_ = 0;
    size_t outputBufferSize_ = 0;

    ReadInfo readInfo_;
    std::unique_ptr<ReadFromFileSegmentState> state_;
    bool initialized_ = false;
};

} // namespace facebook::velox::ch
```

- [ ] **Step 7: Implement `FileCacheBufferedInput.cpp`**

Implement all overrides. Key contracts:

**`enqueue`**

```text
Creates FileCacheInputStream(this, region, requestContext_, logType).
Appends {region, sid} to requests_ (copied region value only; no stream ptr).
Returns the stream.
```

**`load`**

```text
First version: no-op planning barrier.
May clear or retain requests_ for the next cycle.
Must not dereference any SeekableInputStream pointer.
```

Future prefetch extension must work only with copied region values submitted to
`executor_`, never with stream pointers.

**`read`**

```text
return std::make_unique<FileCacheInputStream>(
    const_cast<FileCacheBufferedInput *>(this),
    Region{offset, length},
    requestContext_,
    logType)
```

Must not fall back to a raw `SeekableFileInputStream`; that path bypasses the
cache.

**`isBuffered`**

```text
auto holder = cache_->get(cacheKey_, Range{offset, offset + length - 1}, origin_)
Check every segment in holder is DOWNLOADED or has sufficient downloaded prefix.
Do not call getOrSet. Do not create metadata.
```

- [ ] **Step 8: Implement `FileCacheInputStream.cpp`**

Implement the full streaming state machine from
`port/3-consumers/03-filecache-buffered-input-design.md`. The function-to-function
mapping from ClickHouse `CachedOnDiskReadBufferFromFile`:

| ClickHouse | `FileCacheInputStream` | Caller |
|---|---|---|
| `nextImplStep` | `Next` main body | Velox format reader |
| `initialize` | `initializeIfNeeded` | `Next` on first call |
| `nextFileSegmentsBatch` | `nextFileSegmentsBatch` | `initializeIfNeeded`; batch exhausted |
| `createReadFromFileSegmentState` | `createReadFromFileSegmentState` | `prepareReadFromFileSegmentState` |
| `prepareReadFromFileSegmentState` | `prepareReadFromFileSegmentState` | `Next`; after segment advance |
| `canStartFromCache` | `canStartFromCache` | `createReadFromFileSegmentState` |
| `getCacheReadBuffer` | `getCacheReadBuffer` | `createReadFromFileSegmentState` (CACHED) |
| `getRemoteReadBuffer` | `getRemoteReadBuffer` | `createReadFromFileSegmentState` (REMOTE_FS_*) |
| `predownloadForFileSegment` | `predownloadForCurrentSegment` | `readFromCurrentSegment` |
| `readFromFileSegment` | `readFromCurrentSegment` | `Next` main loop |
| `writeCache` | `writeCache` | `predownloadForCurrentSegment`; `readFromCurrentSegment` |
| `updateReadStateIfNeeded` | `updateReadStateIfNeeded` | `Next` before read |
| `updateImplementationBufferIfNeeded` | `updateCurrentReaderIfNeeded` | `Next` if `state_` exists |
| `completeFileSegmentAndGetNext` | `completeCurrentSegmentAndAdvance` | Segment boundary crossed |
| `~CachedOnDiskReadBufferFromFile` | `~FileCacheInputStream` | stream lifetime end |

### `initializeIfNeeded`

```text
if initialized_: return

readInfo_.readUntilPosition = absolutePosition() + (region_.length - position_)

switch (cacheOptions_.tempCacheOnly / readIfExistsOtherwiseBypass):
  tempCacheOnly:
    readInfo_.fileSegments = cache->getDownloadedContiguousOrEmpty(...)
    if holder.empty(): throw "temp data not in cache"
  readIfExistsOtherwiseBypass:
    readInfo_.fileSegments = cache->get(...)
  default:
    readInfo_.fileSegments = cache->getOrSet(...)

initialized_ = true
```

### `Next`

Three-phase main loop:

```text
1. updateCurrentReaderIfNeeded (advance segment when exhausted)
2. readFromCurrentSegment
3. If REMOTE_FS_READ_AND_PUT_IN_CACHE: reserve + write; on failure switch to bypass
4. Copy result bytes to outputBuffer_; set *data and *size
5. If segment exhausted: completeCurrentSegmentAndAdvance
```

Returns `true` when bytes are available; `false` at end of region.
Must not return partially-consumed remote bytes without writing them to cache
on the `REMOTE_FS_READ_AND_PUT_IN_CACHE` path.

### `BackUp`

```text
VELOX_CHECK_LE(count, offsetInOutputBuffer_)
position_ -= count
offsetInOutputBuffer_ -= count
```

Does not reset `FileCache` state.

### `seekToPosition`

```text
newPosition = position.next()   // region-relative
VELOX_CHECK_LE(newPosition, region_.length)

if outputBufferStart_ <= newPosition
   && newPosition < outputBufferStart_ + outputBufferSize_:
  // fast path: O(1), no holder/downloader change
  position_ = newPosition
  offsetInOutputBuffer_ = newPosition - outputBufferStart_
else:
  // slow path: release everything except queryContextHolder_
  releaseDownloaderIfNeeded()
  readInfo_.reset()
  state_.reset()
  position_ = newPosition
  initialized_ = false
```

### Destructor

```text
releaseDownloaderIfNeeded()   // completePartAndResetDownloader if still held
readInfo_.reset()             // completeAndPopFront all remaining segments
// queryContextHolder_ destroyed last (declared after readInfo_ in class)
```

The destructor must be called while the `FileCacheQueryIdScope` for this
stream's query is still active, because `completeAndPopFront` may trigger
`FileCache::tryReserve` → `QueryLimit::tryGetQueryContext`.

### `getCacheReadBuffer`

Opens the local cache segment file via `owner_->fileCache()`'s local
filesystem reference. Creates `ReadBufferFromVeloxReadFile` wrapping a local
`ReadFile`.

### `getRemoteReadBuffer`

```text
REMOTE_FS_READ_AND_PUT_IN_CACHE:
  fileSegment.getRemoteFileReader() if non-null
  else: create reader and fileSegment.setRemoteFileReader(reader)

REMOTE_FS_READ_BYPASS_CACHE:
  readInfo_.remoteReader if non-null and position matches
  else: fileSegment.extractRemoteFileReader() if available
  else: create new reader
  do not attach back to fileSegment
```

### `writeCache`

```text
fileSegment.write(data, size, offset)
on exception:
  if skip_cache_on_disk_failure (from config):
    switch to REMOTE_FS_READ_BYPASS_CACHE
    return false
  else:
    rethrow
return true
```

- [ ] **Step 9: Update `CMakeLists.txt`**

Replace or create `velox/ch/Disks/IO/CMakeLists.txt`:

```cmake
velox_add_library(
  velox_ch_filecache_dwio
  FileCacheBufferedInput.cpp
  FileCacheInputStream.cpp
)

target_link_libraries(
  velox_ch_filecache_dwio
  PUBLIC
    velox_ch_filecache_manager
    velox_ch_filecache_core
    velox_ch_filecache
    velox_dwio_common
    velox_file
    velox_memory
    Folly::folly
    fmt::fmt
)

if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Create `velox/ch/Disks/CMakeLists.txt`:

```cmake
add_subdirectory(IO)
```

Append to `velox/ch/CMakeLists.txt`:

```cmake
add_subdirectory(Disks)
```

Do not duplicate either `add_subdirectory`.

- [ ] **Step 10: Build**

Reconfigure (same CMake command, updated log path), then build:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_filecache_buffered_input_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_buffered_input.log 2>&1
```

Expected: exit code 0.

- [ ] **Step 11: Run the focused tests**

```bash
ctest \
  --test-dir /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -R '^velox_ch_filecache_buffered_input_test$' \
  --output-on-failure \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_buffered_input.log 2>&1
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 12: Inspect task-owned changes**

```bash
cd /home/chang/OpenSource/velox
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
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
```

Also verify no Gluten files were modified:

```bash
cd /home/chang/SourceCode/gluten1
git --no-pager status --short
```

Expected: no changes in the Gluten repository. If any Gluten file appears
dirty, stop, identify the cause, and report before proceeding.

Expected in Velox: no whitespace errors, no files outside the declared scope
changed by this task, changes remain unstaged and uncommitted.

- [ ] **Step 13: Write the result handoff**

Create:

```text
/home/chang/SourceCode/ClickHouse/port/task/result/014-filecache-buffered-input-result.md
```

Use exactly this structure:

````markdown
# Task 014 Result: `FileCacheBufferedInput` and `FileCacheInputStream`

## Status

status: success

## Velox status

```text
<paste branch, HEAD, and final `git status --short`>
```

## Gluten status

```text
<paste git status --short from /home/chang/SourceCode/gluten1>
```

## Files changed

```text
<list only task-owned files; confirm no Gluten files>
```

## Commands run

```text
<paste configure, build, test, and verification commands>
```

## Generated logs

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_014_buffered_input.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_red.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_014_buffered_input.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_014_buffered_input.log
```

## Verification

```text
Red build failed because helper headers were absent.
Final build exit code:
Focused test result:
git diff --check result:
Gluten repository status:
```

## Behavioral invariants verified

```text
[ ] lazy Next: load never dereferences stream
[ ] region-relative ByteCount / seek
[ ] absolute FileCache / ReadFile offsets
[ ] shouldPrefetchStripes = false
[ ] preloaded = false
[ ] hasCache = false
[ ] executor returns injected executor
[ ] isBuffered uses no-create get
[ ] seekToPosition buffer fast path
[ ] queryContextHolder never reset by seek
[ ] empty etag -> fromPath key
[ ] non-empty etag -> SipHash key; different etags produce different keys
[ ] enqueue result discarded before load -> no crash / use-after-free
[ ] random row-group seek returns correct data
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 015: run Velox-only FileCache E2E tests and the basic random-seek benchmark.
```
````

## Explicit Exclusions

Do not implement in this task:

```text
GlutenBufferedInputBuilder extension (edits Gluten; belongs to Task 018)
CachedReadFile / CacheFileSystem fallback
AsyncDataCache raw-bytes path sharing with FileCacheBufferedInput
FileCacheSettingsLoader Gluten config parsing
Prometheus / custom metrics (keep no-op shims)
DWRF stripe-metadata CacheInputStream hard-cast path
  (already prevented by shouldPrefetchStripes = false)
```
