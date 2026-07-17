# 02. Locks / logging / filesystem basic shims设计

## 结论

locks、logging、filesystem 是 `FileCache` 算法迁移时会高频出现的基础依赖，但不是当前
主线。第一阶段采用 CH-compatible shim，尽量保留调用点和名字：

| ClickHouse | Velox port first phase |
|---|---|
| `SharedMutex` | `using SharedMutex = folly::SharedMutex`，功能接口覆盖；性能不声明等价 |
| `CachePriorityGuard` / `CacheStateGuard` / `CacheMetadataGuard` / `KeyGuard` / `FileSegmentGuard` | 直接迁移 CH `Guards.h` 结构；只替换 `SharedMutex` 底层 |
| `LoggerPtr` / `getLogger` / `LOG_*` | 第一阶段全 no-op，后续再考虑真实日志移植 |
| `LOG_TEST` | 第一阶段 no-op，禁止 eager formatting |
| `fs::` | `namespace fs = std::filesystem`，直接迁移本地 metadata/path 操作 |
| local file read/write | 已由 `ReadBufferFromVeloxReadFile` / `WriteBufferFromVeloxWriteFile` 覆盖 |

目标是：`FileCache` 算法文件迁移时不因为锁、日志、路径操作大面积改写。

## Locks / guards

ClickHouse `FileCache` 的锁设计不是普通类型替换，重点是 **锁顺序** 和 **lock 类型不可互换**。

当前锁顺序：

```text
CachePriorityGuard
  > CacheMetadataGuard
  > KeyGuard
  > FileSegmentGuard
```

另外 `CacheStateGuard` 保护 cache total size/elements counters。

这些 guard 本身就在 `FileCache` 模块内，不属于外部基础设施。第一阶段直接迁移 CH
`Guards.h` 的结构：

```text
保留类名
保留嵌套 Lock 类型
保留锁顺序说明
保留 std::mutex / std::timed_mutex
只把 SharedMutex 底层换成 folly::SharedMutex
```

`SharedMutex` 暂用 `folly::SharedMutex`：

```cpp
using SharedMutex = folly::SharedMutex;

struct CachePriorityGuard
{
    using WriteLock = std::unique_lock<SharedMutex>;
    using ReadLock = std::shared_lock<SharedMutex>;

    ReadLock tryReadLock();
    WriteLock tryWriteLock();
    ReadLock readLock();
    WriteLock writeLock();

private:
    SharedMutex mutex;
};

struct CacheStateGuard
{
    using Mutex = std::timed_mutex;

    struct Lock : public std::unique_lock<Mutex>
    {
        using Base = std::unique_lock<Mutex>;
        using Base::Base;
    };

    Lock tryLock();
    Lock lock();
    Lock tryLockFor(const std::chrono::milliseconds & acquireTimeout);

private:
    Mutex mutex;
};

struct CacheMetadataGuard
{
    struct Lock : public std::unique_lock<std::mutex>
    {
        explicit Lock(std::mutex & mutex) : std::unique_lock<std::mutex>(mutex) {}
    };

    Lock lock();

private:
    std::mutex mutex;
};

struct KeyGuard
{
    struct Lock : public std::unique_lock<std::mutex>
    {
        explicit Lock(std::mutex & mutex) : std::unique_lock<std::mutex>(mutex) {}
    };

    Lock lock();

private:
    std::mutex mutex;
};

struct FileSegmentGuard
{
    struct Lock : public std::unique_lock<std::mutex>
    {
        explicit Lock(std::mutex & mutex) : std::unique_lock<std::mutex>(mutex) {}
    };

    Lock lock();

private:
    std::mutex mutex;
};
```

`ProfileEventTimeIncrement` 在
[metrics/debug设计](03-filecache-metrics-debug-design.md)中变成 no-op shim，所以
`readLock` / `writeLock`
可以保留计时 guard 调用点而不引入真实 metrics。

### `SharedMutex` 选择

ClickHouse Linux `DB::SharedMutex` 是自研 futex reader-writer lock。`#87060` 里比较过
`DB::SharedMutex` 和 `absl::Mutex`：

```text
AMD: absl 与 DB::SharedMutex read-only 类似，write-only 明显更快，RW 略快
Intel / Graviton: absl read-only 更慢，write-only 更快，RW 更慢
最终结论: 直接替换成 absl::Mutex 不值得
```

`folly::SharedMutex` 不是 CH `DB::SharedMutex` 的性能等价物，但它是 Velox 代码中已有的
高性能 reader-writer lock，并且接口直接覆盖 `FileCache` 当前用法：

```text
std::unique_lock<SharedMutex>
std::shared_lock<SharedMutex>
lock / try_lock / unlock
lock_shared / try_lock_shared / unlock_shared
```

因此第一阶段选择：

```text
using SharedMutex = folly::SharedMutex
```

不选择 `absl::Mutex` 作为第一阶段主映射，原因是它需要额外 wrapper 才能满足
`std::shared_lock` 风格接口，而且 `#87060` 没有证明它在所有目标硬件和 mixed RW 场景下
优于 CH 自研实现。

如果后续 `CachePriorityGuard` / `key_prefix_directory_mutex` 成为热点，再用 Velox 本地
benchmark 比较：

```text
folly::SharedMutex
absl::Mutex wrapper
std::shared_mutex
必要时自定义 lock
```

再决定是否替换。

### Why not flatten locks?

Do not replace all guards with raw `std::mutex` in call sites. The nested `Lock` types make it
harder to pass a `KeyGuard::Lock` where a `FileSegmentGuard::Lock` is expected. This protects the
lock-ordering contract while porting the center SCC. Because the guard classes are part of
`FileCache` itself, direct migration is preferable to a new abstraction.

## Logging shim

ClickHouse code uses:

```text
LoggerPtr
getLogger
LOG_TEST
LOG_TRACE
LOG_DEBUG
LOG_INFO
LOG_WARNING
LOG_ERROR
tryLogCurrentException
getCurrentExceptionMessage
```

Logging is not on the migration main path. First phase keeps these names in a small no-op shim
only so the `FileCache` algorithm can be ported without log-system work:

```cpp
using LoggerPtr = std::shared_ptr<Logger>;

class Logger
{
public:
    explicit Logger(std::string name);
};

LoggerPtr getLogger(std::string name);
std::string getCurrentExceptionMessage(bool withStackTrace);
void tryLogCurrentException(std::string_view context);
```

Macro mapping:

```cpp
#define LOG_TEST(logger, ...) do {} while (false)
#define LOG_TRACE(logger, ...) do {} while (false)
#define LOG_DEBUG(logger, ...) do {} while (false)
#define LOG_INFO(logger, ...) do {} while (false)
#define LOG_WARNING(logger, ...) do {} while (false)
#define LOG_ERROR(logger, ...) do {} while (false)
```

This intentionally avoids eager formatting. A mapping like:

```cpp
#define LOG_TEST(logger, ...) VLOG(1) << fmt::format(__VA_ARGS__)
```

is not allowed in the first phase because `fmt::format` runs even when `VLOG(1)` is disabled.

Real logging is a follow-up task after the algorithm port is stable. At that point,
debug/trace/test logs must use lazy formatting:

```text
check log level first
format only when enabled
avoid shared state or locks in logging shim
```

## Filesystem shim

ClickHouse uses `fs::` mostly for local cache metadata and local segment files:

```text
exists
remove
remove_all
file_size
create_directories
directory_iterator
is_empty
path parent_path / string
filesystem_error
```

First phase:

```cpp
namespace fs = std::filesystem;
```

This is enough for most metadata directory operations.

For local cache file IO, do not use raw `std::fstream`; use existing wrappers:

```text
ReadBufferFromVeloxReadFile
WriteBufferFromVeloxWriteFile
```

These wrappers sit over Velox `ReadFile` / `WriteFile`, as defined in the
[`FileCache` infrastructure mapping](01-filecache-infra-mapping.md).

### Velox `FileSystem` usage

Velox 没有禁止 `std::filesystem`。Velox 自己的 `FileSystems.cpp` 也直接使用：

```text
std::filesystem::exists
std::filesystem::directory_iterator
std::filesystem::create_directories
std::filesystem::remove_all
```

因此 `FileCache` metadata/cache 目录操作的主路径就是：

```text
fs:: -> namespace fs = std::filesystem
```

Velox `FileSystem` 也有 local APIs：

```text
openFileForRead
openFileForWrite
remove
rename
exists
list
mkdir
rmdir
```

但这些不作为 metadata 目录遍历/清理的主映射。ClickHouse `FileCache` metadata code
频繁需要 `std::filesystem::path`、`directory_iterator`、`parent_path`、`is_empty`；
这些直接用 `std::filesystem` 最接近原实现。

只在已经进入 Velox file handle 的 IO 路径使用 Velox `FileSystem`：

```text
local cache segment read/write
future non-local filesystem integration, if required
```

### `StatusFile`

CH `StatusFile` 是 cache-directory exclusive ownership guard，不是普通日志文件。
Velox 直接迁移 `StatusFile` 小类，内部 fd RAII 使用 `folly::File`：

```cpp
class StatusFile
{
public:
    StatusFile(std::string path, FillFunction fill);
    ~StatusFile();

private:
    const std::string path;
    folly::File file;
};
```

映射：

```text
open/create fd        -> folly::File(path, O_WRONLY | O_CREAT | O_CLOEXEC)
LOCK_EX | LOCK_NB     -> folly::File::try_lock
truncate/write        -> use file.fd
close                 -> explicit folly::File::close in StatusFile destructor
unlink status path    -> after close attempt, regardless of close result
```

`folly::File` 的 `lock` / `try_lock` 底层就是 inter-process `flock`。

必须保持：

```text
second live process/instance on same cache path fails initialization
lock held for full FileCache lifetime
status file contains process diagnostics
destructor explicitly closes lock fd before unlink
constructor failure closes fd
```

不能依赖 member destruction完成 close：C++ member在 destructor body之后才析构，若在
body里先 unlink会颠倒上述顺序。destructor必须显式调用 `file.close()`，再删除 status
path；close/unlink失败按 CH destructor的日志策略处理，不能抛出 destructor。

不能把 `StatusFile` 做成 no-op，也不新增 `FileCacheStatusLock`；它是依赖方的
CH-compatible wrapper。

### Error handling

ClickHouse often catches `fs::filesystem_error` and wraps it with cache state. Preserve that pattern:

```cpp
catch (const fs::filesystem_error & e)
{
    throwFileCacheExceptionFromFilesystemError(e, context);
}
```

First phase helper:

```cpp
[[noreturn]] void throwFileCacheExceptionFromFilesystemError(
    const fs::filesystem_error & error,
    std::string_view context);
```

Implementation can use `VELOX_FAIL` / `VELOX_USER_FAIL`, but should include:

```text
operation context
path if available
error code / message
cache state string when caller provides it
```

Do not silently ignore remove errors. If deletion is best-effort in CH code, preserve the existing
log-and-continue behavior explicitly at that call site.

## Finishable bounded queue

`FileCache.cpp` 对 `ConcurrentBoundedQueue` 的依赖不只是 fixed-capacity MPMC：

```text
capacity 0:
  nonblocking push always fails
  listing thread performs work directly

finish:
  reject new pushes
  wake every blocked producer and consumer
  let consumers drain already-enqueued items before returning false
```

`folly::MPMCQueue` 没有 close/finish，也不能构造 capacity 0。sentinel无法唤醒一个因
full queue阻塞的 producer。因此不使用 MPMC+sentinel模拟完整语义，而是迁移一个最小
CH-compatible wrapper：

```cpp
template <typename T>
class FileCacheBoundedQueue
{
public:
    explicit FileCacheBoundedQueue(size_t capacity);

    bool push(T value);
    bool tryPush(T value);
    bool pop(T & value);
    void finish();

private:
    std::mutex mutex_;
    std::condition_variable producerCv_;
    std::condition_variable consumerCv_;
    std::deque<T> queue_;
    size_t capacity_;
    bool finished_ = false;
};
```

contract：

```text
push waits for space or finished
tryPush never waits
pop waits for data or finished
pop after finish drains queued data, then returns false
finish is idempotent and notifies both condition variables
```

Metadata `DownloadQueue` / `CleanupQueue` 不是 `ConcurrentBoundedQueue`，继续按 CH手写
queue结构迁移；它们保留 runtime resize、dedup、cancel和自己的 condition variable。

## First phase files

```text
velox/ch/Common/SharedMutex.h
velox/ch/Interpreters/FileCache/Guards.h
velox/ch/Common/logger_useful.h
velox/ch/Common/FileCacheFilesystem.h
velox/ch/Common/StatusFile.h / .cpp
velox/ch/Common/FileCacheBoundedQueue.h
```

`FileCacheFilesystem.h` provides:

```cpp
namespace fs = std::filesystem;

[[noreturn]] void throwFileCacheExceptionFromFilesystemError(
    const fs::filesystem_error & error,
    std::string_view context);
```

## Tests

First phase tests should cover compile-time and small behavior:

```text
CachePriorityGuard read/write locks compile and are not interchangeable with KeyGuard::Lock
CacheStateGuard::tryLockFor compiles
LOG_* macros compile with fmt-style arguments and are no-op
LOG_TEST does not eagerly format arguments
fs alias supports path / exists / create_directories / remove_all on a temp directory
throwFileCacheExceptionFromFilesystemError throws a Velox exception with context text
capacity-0 tryPush returns false
finish wakes blocked producer and consumer
finish drains queued values before pop returns false
push after finish returns false
```

## Review 状态

本文档已完成 review。关键决策：

```text
lock guard class names are preserved
Guards.h structure is directly migrated from ClickHouse
logging is not mainline; Logger and LOG_* are no-op compat shims in first phase
fs:: is std::filesystem for local metadata/cache paths
Velox FileSystem is not the primary mapping for metadata directory traversal
Velox FileSystem is used only where the IO path already uses ReadFile / WriteFile
StatusFile is preserved and backed by folly::File inter-process locking
ConcurrentBoundedQueue is preserved by FileCacheBoundedQueue, not MPMC sentinels
```
