# ClickHouse `FileCache` to Velox migration design

## Goal

Migrate the full ClickHouse `FileCache` functionality and algorithms into
`/home/chang/OpenSource/velox`, while replacing ClickHouse infrastructure with
Velox infrastructure.

The migration should preserve the ClickHouse cache semantics:

- file segment lifecycle and partial download state machine
- metadata loading and recovery from local cache directory
- capacity reservation and eviction
- `LRU`, `SLRU`, split cache, and overcommit eviction policies
- per-query cache write limits
- per-user/origin accounting and idle client eviction
- background download, background free-space keeping, invalidated-entry cleanup
- dynamic resize/reloadable settings
- write-through cache support
- metrics and cache inspection

The migration should not reimplement these algorithms using Velox
`AsyncDataCache` or `SsdCache`. Those Velox classes have different semantics:
they are memory page cache / optional SSD cache built around `CachePin`,
exclusive/shared entries, and score-based eviction. They should not replace the
ClickHouse `FileCache` algorithm classes.

## Recommended integration

Use `CacheFileSystem` as the main Velox integration point.

`CacheFileSystem` should be a filesystem decorator around an already-resolved
real `FileSystem`, not a normal scheme matcher that competes with `s3`, `hdfs`,
`gcs`, or `file`.

Recommended lookup flow:

```text
filesystems::getFileSystem(path, config)
  -> resolve real filesystem by existing scheme registry
     e.g. S3FileSystem / HdfsFileSystem / GcsFileSystem / LocalFileSystem
  -> if filesystem cache is enabled:
         return CacheFileSystem(real_fs, file_cache_manager, config)
     else:
         return real_fs
```

This avoids:

- stealing existing schemes
- recursively resolving `CacheFileSystem` through `getFileSystem`
- forcing callers to rewrite paths to `cache://...`

`BufferedInput` integration should be secondary. It can be used to avoid
double-caching and to preserve Velox coalescing/prefetch behavior, but it should
not own the cache algorithm.

## `CachedReadFile` interface and implementation

`CachedReadFile` is the Velox-side equivalent of ClickHouse
`CachedOnDiskReadBufferFromFile`: it sits between Velox readers and the real
remote/local `ReadFile`, asks `FileCache` for file segments, and fills missing
segments from the wrapped file.

### Public interface

`CachedReadFile` should implement the standard Velox `ReadFile` interface and
hide all cache details from callers:

```cpp
class CachedReadFile final : public ReadFile
{
public:
    CachedReadFile(
        std::shared_ptr<ReadFile> inner,
        FileCachePtr cache,
        FileCacheKey key,
        std::string path,
        FileCacheReadOptions options);

    std::string_view pread(
        uint64_t offset,
        uint64_t length,
        void * buf,
        const FileIoContext & context = {}) const override;

    std::string pread(
        uint64_t offset,
        uint64_t length,
        const FileIoContext & context = {}) const override;

    uint64_t preadv(
        uint64_t offset,
        const std::vector<folly::Range<char *>> & buffers,
        const FileIoContext & context = {}) const override;

    uint64_t preadv(
        folly::Range<const common::Region *> regions,
        folly::Range<folly::IOBuf *> iobufs,
        const FileIoContext & context = {}) const override;

    uint64_t preadv(
        folly::Range<const common::Region *> regions,
        folly::Range<const folly::Range<char *> *> buffers,
        const FileIoContext & context = {}) const override;

    bool hasPreadvAsync() const override;

    folly::SemiFuture<uint64_t> preadvAsync(
        uint64_t offset,
        const std::vector<folly::Range<char *>> & buffers,
        const FileIoContext & context = {}) const override;

    bool directIo(uint64_t & alignment) const override;
    bool shouldCoalesce() const override;
    uint64_t size() const override;
    uint64_t memoryUsage() const override;
    std::string getName() const override;
    uint64_t getNaturalReadSize() const override;

private:
    uint64_t readWithCache(
        folly::Range<const common::Region *> regions,
        folly::Range<const folly::Range<char *> *> buffers,
        const FileIoContext & context) const;

    uint64_t readOneRange(
        uint64_t offset,
        uint64_t length,
        folly::Range<char *> output,
        const FileIoContext & context) const;

    FileCacheRequestContext makeCacheContext(const FileIoContext & context) const;

    std::shared_ptr<ReadFile> inner_;
    FileCachePtr cache_;
    FileCacheKey key_;
    std::string path_;
    FileCacheReadOptions options_;
};
```

Initial `preadvAsync` should not pretend to be natively asynchronous unless the
cache path is explicitly made async. A safe first version can return
`hasPreadvAsync == false` and rely on the base synchronous behavior, or submit
`readWithCache` to an owned executor in a later phase.

`directIo` should delegate to `inner_` unless `CachedReadFile` always reads the
remote file through its own correctly aligned scratch buffers. Delegation is the
safer first design because cache misses still call the wrapped `ReadFile`.

`shouldCoalesce` and `getNaturalReadSize` should delegate to `inner_`; Velox
readers can keep their existing coalescing decisions while the cache handles
segments internally.

### Internal remote reader adapter

`FileSegment` should not store `ReadFile` directly. `ReadFile` is a positioned
read API; it does not represent "a downloader that has already read up to this
offset and still owns an internal buffer".

Use a small adapter to replace ClickHouse `ReadBufferFromFileBase`:

```cpp
class FileSegmentRemoteReader
{
public:
    FileSegmentRemoteReader(
        std::shared_ptr<ReadFile> inner,
        FileIoContext context,
        uint64_t bufferSize);

    void seek(uint64_t offset);
    bool eof() const;
    uint64_t position() const;
    uint64_t bufferEndOffset() const;
    size_t available() const;
    char * data();

    bool next();

private:
    std::shared_ptr<ReadFile> inner_;
    FileIoContext context_;
    std::vector<char> buffer_;
    uint64_t fileOffset_ = 0;
    size_t positionInBuffer_ = 0;
    size_t validBytes_ = 0;
    bool eof_ = false;
};
```

`next` does a positioned read from `inner_` into `buffer_`, updates
`fileOffset_`, `positionInBuffer_`, and `validBytes_`, and exposes the bytes to
the downloader. This preserves the ClickHouse idea that the remote reader can be
attached to a `FileSegment` and later reused by foreground or background
download.

### Read flow

For every requested range:

```text
CachedReadFile::readOneRange
  -> build FileCacheRequestContext from FileIoContext/path/options
  -> if context says non-cacheable:
         inner_->pread directly
  -> holder = FileCache::getOrSet(key, offset, length, fileSize, settings, ...)
  -> for each FileSegment in holder:
         if segment is downloaded:
             read local cache segment into caller buffer
         else:
             use downloader protocol to fill/read the segment
```

Downloader path:

```text
segment.getOrSetDownloader
  -> if this caller becomes downloader:
         get or create FileSegmentRemoteReader
         while caller still needs bytes:
             reader.next / reader.available
             segment.reserve(bytes)
             segment.write(reader.data, bytes, offset)
             copy bytes to caller output
         segment.completePartAndResetDownloader or complete
  -> if another caller is downloader:
         wait/read available cached bytes according to FileSegment state
```

The `FileSegmentRemoteReader` is stored in `FileSegment::DownloadState`, not in
`CachedReadFile`, because the reader belongs to the segment download state. This
is what allows background download to continue a partially downloaded segment
after the foreground read stops.

### Vector read handling

All public `preadv` variants should normalize into:

```text
vector of (Region, output buffer)
```

Then call `readWithCache`. `readWithCache` should preserve Velox's result
contract: return total bytes read and fill outputs in the same order as input
regions.

`pread(offset, length, buf)` is just a single-region wrapper around
`readWithCache`.

`pread(offset, length)` allocates an owned string, calls the buffer version, and
returns the string.

### Error handling

If remote read or cache write fails while this caller is downloader:

```text
segment.setDownloadFailed
propagate the Velox exception
```

Only bypass cache on cache-disk errors when the migrated setting equivalent of
`skip_cache_on_disk_failure` explicitly allows it. Otherwise errors should
surface; do not silently fall back to remote reads because that hides cache
invariant bugs.

### Thread safety

`CachedReadFile` methods must be thread-safe, matching Velox `ReadFile`.

`CachedReadFile` itself should hold only shared immutable state:

```text
inner_
cache_
key_
path_
options_
```

Mutable download state lives in `FileSegment`, guarded by the migrated segment
locks. Mutable cache state lives in `FileCache` and priority/metadata guards.

## Coexistence with Velox cache

Velox already has `AsyncDataCache`, `SsdCache`, `CachedBufferedInput`, and
`CacheInputStream`.

The new `FileCache` should coexist as follows:

```text
CacheFileSystem / CachedReadFile
  owns ClickHouse FileCache semantics

CachedBufferedInput / CacheInputStream
  may still do format-reader coalescing/prefetch
  must avoid retaining the same bytes again in AsyncDataCache
```

When a `ReadFile` is produced by `CacheFileSystem`, `CachedBufferedInput` should
either:

- mark the stream as non-cacheable for `AsyncDataCache`, or
- detect a cached `ReadFile` marker and skip `AsyncDataCache` retention.

This prevents:

```text
remote bytes -> FileCache local segment -> AsyncDataCache memory entry
```

from becoming an accidental double cache.

## New Velox-side modules

Proposed location:

```text
velox/common/file/cache/
```

Main files:

```text
FileCache.h / FileCache.cpp
FileSegment.h / FileSegment.cpp
Metadata.h / Metadata.cpp
IFileCachePriority.h / IFileCachePriority.cpp
LRUFileCachePriority.h / LRUFileCachePriority.cpp
SLRUFileCachePriority.h / SLRUFileCachePriority.cpp
SplitFileCachePriority.h / SplitFileCachePriority.cpp
OvercommitFileCachePriority.h
EvictionCandidates.h / EvictionCandidates.cpp
CacheUsage.h
FileCacheSettings.h / FileCacheSettings.cpp
FileCacheFactory.h / FileCacheFactory.cpp
CacheFileSystem.h / CacheFileSystem.cpp
CachedReadFile.h / CachedReadFile.cpp
CachedWriteFile.h / CachedWriteFile.cpp
FileCacheScheduler.h / FileCacheScheduler.cpp
```

## Replacement map

### IO

| ClickHouse dependency | Velox replacement | 是否人工 review 过 |
|---|---|---|
| `ReadBufferFromFileBase` | `velox::ReadFile` / `ReadFileInputStream` | 否 |
| `WriteBufferFromFile` | `velox::WriteFile` | 否 |
| `WriteBufferToFileSegment` | new `FileSegmentWriter` or `CachedWriteFile` adapter | 否 |
| `CachedOnDiskWriteBufferFromFile` | `WriteFile`-based local cache writer | 否 |
| `OpenedFileCache` | no direct replacement; use `ReadFile` / `WriteFile` handles or a small local handle cache if needed | 否 |

Read path:

```text
CachedReadFile::pread / preadv
  -> FileCache::getOrSet
  -> cache hit: read local cache segment
  -> cache miss: read inner ReadFile, write FileSegment, return bytes
```

Write path:

```text
CachedWriteFile::append / write
  -> write through to inner WriteFile
  -> if cache_on_write_operations is enabled, also populate FileCache segment
```

### Configuration

| ClickHouse dependency | Velox replacement | 是否人工 review 过 |
|---|---|---|
| `Poco::Util::AbstractConfiguration` | `velox::config::ConfigBase` | 否 |
| `NamedCollection` | connector properties / `ConfigBase` prefix | 否 |
| `Settings`, `ReadSettings`, `FilesystemCacheSettings` | new Velox `FileCacheSettings`, plus `ReaderOptions` / `FileOptions` for per-read flags | 否 |

The ClickHouse setting names should be preserved where possible, translated into
Velox config keys with a prefix such as:

```text
file-cache.path
file-cache.max-size
file-cache.max-elements
file-cache.cache-policy
file-cache.background-download-threads
```

### Threading and scheduling

| ClickHouse dependency | Velox replacement | 是否人工 review 过 |
|---|---|---|
| `ThreadPool` | `folly::CPUThreadPoolExecutor` or `folly::IOThreadPoolExecutor` | 否 |
| `ThreadFromGlobalPool` | executor task submitted to owned executor | 否 |
| `BackgroundSchedulePoolTaskHolder` | new `FileCacheScheduler` wrapper | 否 |
| `callOnce`, `OnceFlag` | `folly::once_flag` / `folly::call_once` or `std::once_flag` | 否 |

`FileCacheScheduler` should expose only the operations needed by `FileCache`:

```text
scheduleAfter(delay)
cancel()
shutdown()
```

This keeps ClickHouse scheduling assumptions isolated from Velox.

### Query and user context

| ClickHouse dependency | Velox replacement | 是否人工 review 过 |
|---|---|---|
| `CurrentThread::getQueryId` | explicit query id from Velox connector/query context | 否 |
| `ThreadStatus` | no direct dependency; pass required fields explicitly | 否 |
| `FileCacheOriginInfo::user_id` | user/client id from connector/session context | 否 |
| `FileIoContext::cacheable` | per-read cacheability hint | 否 |

Do not rely on global thread state. The Velox integration should pass a compact
cache context explicitly:

```text
struct FileCacheRequestContext {
  std::string queryId;
  std::string userId;
  uint64_t userWeight;
  bool cacheable;
  FileSegmentKeyType segmentType;
};
```

`CachedReadFile` can derive this from `FileIoContext`, `FileOptions`,
`ReaderOptions`, or connector-specific properties.

### Metrics and observability

| ClickHouse dependency | Velox replacement | 是否人工 review 过 |
|---|---|---|
| `CurrentMetrics` | internal atomics + stats snapshot, optionally `StatsReporter` | 否 |
| `ProfileEvents` | `IoStatistics`, `IoStats`, `RuntimeMetric`, `RuntimeCounter` | 否 |
| `DimensionalMetrics`, `HistogramMetrics` | Velox stats/reporting wrappers | 否 |
| system table dump via `ColumnsDescription` | Velox stats/config dump API | 否 |

The first migration should keep a local `FileCacheStats` struct. Later this can
be wired into Velox runtime stats and exported metrics.

### Exceptions and logging

| ClickHouse dependency | Velox replacement | 是否人工 review 过 |
|---|---|---|
| `Exception`, `ErrorCodes::LOGICAL_ERROR` | `VELOX_FAIL` / `VELOX_CHECK` / `VeloxRuntimeError` | 否 |
| user-facing bad input errors | `VELOX_USER_FAIL` / `VeloxUserError` | 否 |
| `logger_useful` | `LOG`, `VLOG`, `FB_LOG_EVERY_MS` | 否 |

Use Velox exceptions at boundaries, but keep messages close to ClickHouse
messages where they document cache invariants.

### Basic types and helpers

| ClickHouse dependency | Velox replacement | 是否人工 review 过 |
|---|---|---|
| `String` | `std::string` | 否 |
| `UInt64`, `UInt128` | `uint64_t`, custom `FileCacheKeyHash` / 128-bit helper | 否 |
| `UUID`, `ServerUUID` | explicit config or local UUID helper only if still needed | 否 |
| `SipHash`, `hex`, `randomSeed` | Velox/folly helpers or small migrated helper | 否 |
| `base/unit.h` | constants in Velox style | 否 |
| `SharedMutex`, `SharedLockGuard` | `folly::SharedMutex`, `std::shared_mutex`, or thin local wrappers | 否 |
| `scope_guard` | `folly::ScopeGuard` | 否 |

Do not map ClickHouse `FileCacheKey` to Velox `cache::FileCacheKey`.
Velox `cache::FileCacheKey` is `fileNum + offset`, while ClickHouse
`FileCacheKey` identifies an origin object and is used in metadata paths.

## Classes to preserve

These classes should be migrated as algorithm classes, not replaced by Velox
cache classes:

```text
FileCache
FileSegment
FileSegmentsHolder
CacheMetadata
KeyMetadata
FileSegmentMetadata
LockedKey
IFileCachePriority
LRUFileCachePriority
SLRUFileCachePriority
SplitFileCachePriority
OvercommitFileCachePriority
EvictionInfo
EvictionCandidates
CacheUsage
CacheUsagePerUser
FileCacheQueryLimit
FileCacheSettings
```

Velox infrastructure should appear around them, not inside their algorithms
unless required for IO, scheduling, logging, metrics, or errors.

## Landing strategy

Use file-level DAG ordering until reaching the central SCC. At the central SCC,
land by functional SCC slices instead of forcing one file at a time.

### File-level layers

Layer 0:

```text
FileCacheKey
FileCacheUtils
FileCache_fwd
FileCache_fwd_internal
FileSegmentKeyType
Guards
ShardedMap
```

Layer 1:

```text
FileCacheOriginInfo
FileCacheSettings
```

Layer 2:

```text
IFileCachePriority
```

Layer 3:

```text
CacheUsage
FileSegmentInfo
SplitFileCachePriority
```

Layer 4:

```text
EvictionCandidates
FileSegment
LRUFileCachePriority
```

Layer 5:

```text
Metadata
QueryLimit
SLRUFileCachePriority
WriteBufferToFileSegment replacement
```

Layer 6:

```text
FileCache
```

Layer 7:

```text
FileCacheFactory
CacheFileSystem
CachedReadFile
CachedWriteFile
```

### Central SCC functional slices

When reaching the central SCC, land these as coherent units:

1. `Metadata`, `KeyMetadata`, `LockedKey`, `FileSegmentMetadata`
2. `FileSegment` state machine and `FileSegmentsHolder`
3. `FileCache::get`, `FileCache::getOrSet`, `FileCache::set`, segment creation
4. reserve and eviction: `tryReserve`, `doTryReserve`, `doEviction`, `EvictionCandidates`
5. completion and background download
6. query limit, dynamic resize, cleanup, idle client eviction

This avoids creating fake interfaces or partial implementations just to satisfy
temporary compilation.

## Implementation phases

### Phase 1: Core compile skeleton

- create `velox/common/file/cache`
- migrate basic types, guards, settings, key/origin types
- replace ClickHouse types with Velox/basic C++ equivalents
- add CMake targets and minimal tests

### Phase 2: Priority algorithms

- migrate `IFileCachePriority`
- migrate `LRU`, `SLRU`, split, overcommit policy
- migrate `EvictionInfo`, `EvictionCandidates`, `CacheUsage`
- replace metrics/logging/errors only

### Phase 3: Metadata and segment lifecycle

- migrate `Metadata`
- migrate `FileSegment`
- replace ClickHouse buffers with Velox `ReadFile` / `WriteFile` adapters
- keep segment state machine semantics unchanged

### Phase 4: `FileCache` orchestration

- migrate `FileCache`
- wire reserve, eviction, completion, metadata load, background tasks
- implement Velox scheduler and settings reload bridge

### Phase 5: Velox filesystem integration

- add `CacheFileSystem`
- add `CachedReadFile`
- add `CachedWriteFile`
- add filesystem decorator registration
- ensure no scheme conflict with S3/HDFS/GCS/local

### Phase 6: `BufferedInput` coexistence

- detect cached `ReadFile` or pass cacheable=false
- avoid duplicate retention in `AsyncDataCache`
- preserve coalescing/prefetch behavior where useful

### Phase 7: tests and validation

- port ClickHouse `FileCache` unit tests where possible
- add Velox `ReadFile` / `FileSystem` wrapper tests
- add S3/HDFS/local fake filesystem tests
- add concurrency tests for downloader, reserve, eviction, and cleanup
- add restart/metadata-load tests
- add dynamic resize and idle-client eviction tests

## Open design points

1. Exact config prefix and whether settings are global or per filesystem/cache
   instance.
2. How `queryId`, `userId`, and `weight` flow from Prestissimo/connector context
   into `FileIoContext`.
3. Whether `cache_on_write_operations` should be supported for all `WriteFile`
   backends or only local/append-compatible backends.
4. Whether the first landing uses a temporary explicit `cache+scheme` path before
   adding filesystem decorators.
5. How much of ClickHouse system-table output should become Velox runtime stats
   versus debug APIs.

## Recommendation

Proceed with `CacheFileSystem` as the primary integration and keep the
ClickHouse `FileCache` algorithms intact. Replace only infrastructure:
configuration, IO handles, scheduling, metrics, logging, exceptions, and context
propagation.

Land the migration by file DAG until the central SCC, then land the SCC by
functional slices. This keeps early patches reviewable while preserving the
semantic cycles that make `FileCache` correct.
