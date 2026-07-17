# 08. Metadata文件迁移设计

## 结论

这一组严格按三个文件 review：

```text
src/Interpreters/FileCache/FileSegmentInfo.h
src/Interpreters/FileCache/Metadata.h
src/Interpreters/FileCache/Metadata.cpp
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/FileSegmentInfo.h
velox/ch/Interpreters/FileCache/Metadata.h
velox/ch/Interpreters/FileCache/Metadata.cpp
```

三个文件的算法和生命周期语义都直接迁移。只允许替换已经 review 的基础设施：

This batch is an exact algorithm and lifecycle-semantics port, not a source-level copy or redesign.

这里的 exact 指 behavioral equivalence，不要求逐行复制源码。只允许下文列出的、已经
review 的基础设施替换和依赖注入调整。`FileSegmentInfo` snapshot、metadata
key/segment 状态转换、文件路径布局、cleanup/download queues 和 workers、iterator
locking 以及 `LockedKey` 生命周期的行为都不能改变。`Metadata.cpp`、
`FileSegment.cpp` 和 `FileCache.cpp` 必需的 origin API 构成同一个实现 SCC，因此
本批次不允许用 placeholder 接口拆开它们。

```text
CH basic aliases -> ClickHouseAliases.h
CH locks         -> Guards.h / folly::SharedMutex
CH metrics       -> no-op compatibility shims
CH logging       -> no-op compatibility shim
ThreadFromGlobalPool -> FileCacheWorker alias
std hash containers -> folly F14 after checking reference/pointer lifetime across rehash
OpenedFileCache singleton -> FileCacheManager-owned opened-file cache
global Context setting -> injected FileCache config
```

## 文件依赖结论

虽然按文件 review，但不能按以下顺序独立链接：

```text
Metadata.cpp
then FileSegment.cpp
then FileCache.cpp
```

原因：

```text
Metadata.h includes FileSegment.h
Metadata.cpp calls FileSegment state/range/write/reserve/detach/getInfo APIs
Metadata.cpp uses FileCache::getInternalOrigin and injected commonUserId
FileSegment.cpp calls LockedKey and CacheMetadata APIs
FileCache.cpp owns CacheMetadata and creates FileSegment
```

所以这些文件属于真实中心 SCC。正确做法是：

```text
逐文件 review
同一实现批次补齐 Metadata.cpp + FileSegment.cpp + 必要 FileCache static origin API
整个闭环完成后再要求 target link
```

不要为了让 `Metadata.cpp` 提前独立编译而创造假的 `FileSegment` 接口。

## `FileSegmentInfo.h`

### 迁移方式

这个文件直接迁移：

```text
FileSegmentState enum
FileSegmentKind enum
toString(FileSegmentKind) declaration
FileSegmentInfo struct
```

保留状态值和顺序：

```cpp
enum class FileSegmentState : uint8_t
{
    DOWNLOADED,
    EMPTY,
    DOWNLOADING,
    PARTIALLY_DOWNLOADED_NO_CONTINUATION,
    PARTIALLY_DOWNLOADED,
    DETACHED,
};
```

保留 kind：

```cpp
enum class FileSegmentKind : uint8_t
{
    Regular,
    Ephemeral,
};
```

即使 `WriteBufferToFileSegment` / `TemporaryDataOnDisk` 第一阶段后置，也保留
`FileSegmentKind::Ephemeral`。原因：

```text
metadata path parser recognizes "<offset>_temporary"
startup metadata cleanup must understand ephemeral files
FileSegment state machine already distinguishes unbound/ephemeral segments
```

`FileSegmentInfo` 字段直接迁移，不重新组织：

```text
key
offset
path
range_left / range_right
kind / state
size / downloaded_size
download_finished_time
cache_hits
references
is_unbound
queue_entry_type
origin
```

这个 struct 是 introspection snapshot，不拥有 `FileSegment`。

### 实现归属

`toString(FileSegmentKind)` 当前定义在 `FileSegment.cpp`，不是
`FileSegmentInfo.cpp`。第一阶段保持这个实现归属，避免新增第四个文件。

## `Metadata.h`

### 迁移方式

`Metadata.h` 直接迁移公开接口、成员顺序和 lock-bearing wrapper 类型。不要按类重新拆文件。

必须保留：

```text
FileSegmentMetadata
KeyMetadata
CacheMetadata
CacheMetadata::Iterator
LockedKey
CleanupQueue / DownloadQueue forward declarations
```

### `FileSegmentMetadata`

保持：

```text
owns shared_ptr<FileSegment>
releasable iff FileSegment shared_ptr is unique from metadata point of view
removed flag means queue iterator is already invalid
evicting state comes from priority entry state
```

`releasable` 目前依赖 CH `isSharedPtrUnique`。Velox port 可以直接写：

```cpp
bool releasable() const
{
    return file_segment.use_count() == 1;
}
```

但必须核对调用点对引用计数的预期；不要改成只检查 segment state。

### `KeyMetadata`

保留 private `std::map<size_t, FileSegmentMetadataPtr>` 继承。这里不能换 F14：

```text
offset 有序
lower_bound 用于 range intersection
相邻 segment 查找依赖顺序
iterator stability 被 LockedKey methods 使用
```

保留：

```text
key
immutable shared origin
KeyState: ACTIVE / REMOVING / REMOVED
KeyGuard
created_base_directory atomic
lock / tryLock / lockNoStateCheck
path builders
access check
download / cleanup queue submission
```

### `CacheMetadata`

保留：

```text
1024 metadata buckets
per-bucket CacheMetadataGuard
origin dedup pool
cleanup queue
download queue
download workers
cleanup worker
client-access callback
```

`MetadataBucket` 的底层 map 可从 `std::unordered_map` 换成
`folly::F14FastMap`，但仍保留 bucket wrapper 和独立锁：

```cpp
struct MetadataBucket : public folly::F14FastMap<FileCacheKey, KeyMetadataPtr, FileCacheKeyHash>
```

如果继承 F14 容器导致接口/维护问题，也可以改为 composition，但那会增加算法 diff。
第一阶段优先保持现有 wrapper 形态。

`IteratorImpl` 虽然跨 `next` 调用保存 `MetadataBucket::iterator`，但同时跨调用持有
`bucket_lock`。iterator 存活期间该 bucket 不会插入/删除，也不会 rehash，所以这里不要求
F14 提供跨 rehash 的 iterator 稳定性。

固定 `buckets_num = 1024`，不要重新调参。

### `DownloadThread`

按[线程池设计](../1-dependencies/04-filecache-thread-pool-design.md)：

```cpp
struct DownloadThread
{
    std::unique_ptr<ThreadFromGlobalPool> thread;
    bool stopFlag = false;
};

std::vector<std::shared_ptr<DownloadThread>> download_threads;
```

保留外层 stop state；缩容在 `download_queue` mutex下设置 `stopFlag`，再 notify并 join。
generic `FileCacheWorker` 不拥有业务 stop token。

### `LockedKey`

保留 `LockedKey` 成员声明顺序：

```cpp
const std::shared_ptr<KeyMetadata> key_metadata;
KeyGuard::Lock lock;
```

`lock` 必须先析构，因而声明在 `key_metadata` 后。不要交换顺序。

保留：

```text
map iteration and lower_bound
get/tryGet by offset
remove segment variants
remove all releasable segments
download queue submission
range intersection
empty-key delayed cleanup
metadata/file sync
```

## `Metadata.cpp`

`Metadata.cpp` 按以下代码区段 review 和迁移，不拆成新文件。

### 1. metadata wrappers and origin pool

直接迁移：

```text
FileSegmentMetadata constructor/state validation
FileSegmentMetadata::size
KeyMetadata constructor validation
checkAccess / assertAccess
getOrCreateSharedOrigin
removeSharedOrigins
```

保留访问语义：

```text
origin user can access
internal origin can access all keys
other user cannot access
```

保留构造约束：

```text
cannot create key metadata with internal origin
origin weight must exist
created_base_directory implies path exists
```

### 2. key locking and path layout

直接迁移：

```text
KeyMetadata::lock / tryLock / lockNoStateCheck
createBaseDirectory
getPath
getFileSegmentPath overloads
CacheMetadata::getFileNameForFileSegment
CacheMetadata::getKeyPath
```

path layout 必须保持：

```text
Regular downloading: <offset>
Regular downloaded:  <offset>_<size>
Ephemeral:           <offset>_temporary
```

key path：

```text
without per-user:
  <base>/<segment-prefix>/<first-3-key-chars>/<full-key>

with per-user:
  <base>/<segment-prefix>/<user-id>.<weight>/<first-3-key-chars>/<full-key>
```

`origin.weight.value()` 的前置约束由 `KeyMetadata` 构造器保证。

### 3. bucket lookup and key state recovery

直接迁移：

```text
getMetadataBucket
getKeyMetadata
lockKeyMetadata
isEmpty
iterate
```

`KeyNotFoundPolicy` 四种行为必须保持：

```text
THROW
THROW_LOGICAL
CREATE_EMPTY
RETURN_NULL
```

当 key 处于 `REMOVING` 且调用方要求 `CREATE_EMPTY`：

```text
cancel delayed removal
restore ACTIVE
return same locked key
```

当 key 已 `REMOVED`：

```text
release old lock
retry bucket lookup/create
```

client-access callback 必须在 bucket lock 释放后执行，避免与 eviction 的 lock order
反转。

### 4. iterators

保留两种 iterator：

```text
IteratorImpl: one segment per next; not thread-safe
BatchedIteratorImpl: one non-empty bucket batch per nextBatch; sequential calls may run on different threads
```

不要把两者合并。它们有不同锁持有方式和 API contract。

`Metadata.h` 对 `nextBatch` 的注释是 “Safe to be used from different threads”。这里不是指
同一个 iterator 可以被多个线程并发调用。当前调用方 `SystemFilesystemCacheSource` 是
单个 `ISource` 对象；`IProcessor` 明确保证：

```text
no methods (prepare, work, schedule) of single object can be executed in parallel
```

所以相邻两次 `generate` / `nextBatch` 可能由不同 executor thread 执行，但同一时刻不会
并发进入。`BatchedIteratorImpl::bucket_it` 不需要额外 mutex。迁移时保持这个实际契约，
不要为了误读注释增加锁。

### 5. key removal and directory cleanup

直接迁移：

```text
removeAllKeys
removeKey
removeEmptyKey
```

保留行为：

```text
non-releasable segment prevents full client purge
fully removed client drops shared origins
key state set REMOVED before bucket erase
key directory removed recursively
empty prefix/user directories removed under key_prefix_directory_mutex
```

`fs::` 直接使用 `std::filesystem`，错误上下文使用
[基础 shims](../1-dependencies/02-filecache-basic-shims-design.md)的薄 wrapper。

### 6. cleanup queue

完整迁移：

```text
deduplicate keys in queue
cancel prevents new additions
notify_one on first insertion
notify_all on cancel
cleanup worker waits for cancelled or non-empty
only REMOVING key is physically removed
```

不要把 cleanup queue 改成同步删除或 no-op。`LockedKey` 析构依赖 delayed cleanup 来避免
每次 segment 删除都获取 metadata bucket lock。

### 7. download queue and workers

完整迁移：

```text
bounded queue
weak_ptr<FileSegment> identity check
queue cancel
live queue-limit update
per-worker stop and resize
background downloader ownership acquisition
reserve/write/complete loop
```

`DownloadInfo` 同时保存：

```text
key
offset
weak_ptr<FileSegment>
```

weak pointer 不能删。只用 key/offset 重新查 metadata 会把“旧 segment 已删除、同 offset
新 segment 已创建”的情况误认成原任务。

背景下载的 reserve timeout 不再从 global `Context` 取。把
`reserve_space_wait_lock_timeout_milliseconds` 从 `FileCacheConfig` 注入
`CacheMetadata`。

### 8. worker shutdown and resize

按[线程池设计](../1-dependencies/04-filecache-thread-pool-design.md)的顺序完整迁移：

```text
cancel download/cleanup queues
join download workers
join cleanup worker
then destroy executor/resources
```

download worker 缩容：

```text
under download queue mutex:
  set selected workers' stopFlag
notify_all download queue
join selected workers
erase selected workers
```

### 9. `LockedKey` state and removal

直接迁移：

```text
constructor takes KeyGuard
destructor: empty ACTIVE key -> REMOVING -> enqueue cleanup
removeFromCleanupQueue: REMOVING -> ACTIVE
markAsRemoved: -> REMOVED
removeAllFileSegments skips held/evicting segments
removeFileSegment variants
range intersection
sync broken local files
```

删除 segment 文件后必须 invalidates manager-owned opened-file handles。把：

```text
OpenedFileCache::instance().remove(...)
```

替换成注入的 opened-file cache invalidation callback/reference，不创建新的 singleton。

## 容器映射

按[`FileCache` 底层设施替换矩阵](../1-dependencies/01-filecache-infra-mapping.md)：

```text
MetadataBucket std::unordered_map
  -> folly::F14FastMap

CleanupQueue std::unordered_set
  -> folly::F14FastSet

origin ShardedMap inner std::unordered_map
  -> folly::F14FastMap

KeyMetadata std::map
  -> keep std::map

DownloadQueue std::queue
  -> keep std::queue
```

`KeyMetadata` 必须保持 ordered map。

选择 `F14FastMap` / `F14FastSet` 的具体依据：

```text
MetadataBucket:
  保存 iterator 时同时持有 bucket lock，不会发生 rehash

CleanupQueue:
  不把 set element 的 iterator/reference 保存到锁外

origin ShardedMap:
  callback 内查找/插入，返回的是拷贝后的 shared_ptr，不返回 map value 地址
```

如果未来代码把 map value 的指针/reference 保存到插入操作之外，再改成
`F14NodeMap` / `F14NodeSet`。普通 iterator 无论在 `std::unordered_map` 还是 F14 中都不应
被假定为跨 rehash 有效。

## 第一阶段不允许 stub 的内容

```text
key state machine
path layout
origin dedup
bucket locking
LockedKey delayed cleanup
cleanup queue/worker
download queue/worker
download worker resize
background download identity check
file removal + opened handle invalidation
shutdown ordering
```

metrics/logging/tracing/failpoints 继续使用 no-op shim。

## 测试要求

### `FileSegmentInfo.h`

```text
state enum values preserved
kind enum values preserved
info snapshot fields compile with expected types
```

### `Metadata.h` / `Metadata.cpp`

```text
path layout for General/System/Data
regular downloading filename <offset>
regular downloaded filename <offset>_<size>
ephemeral filename <offset>_temporary
per-user path includes user.weight
same OriginPoolKey deduplicates origin instance
different weight/type keeps distinct origin instance
KeyNotFoundPolicy behavior
REMOVING key can be reactivated by CREATE_EMPTY
empty LockedKey destructor queues delayed cleanup
removeAllKeys keeps held segments
cleanup queue deduplicates keys and exits on cancel
download queue enforces limit
stale weak segment is rejected after key/offset reuse
download worker grow/shrink
shutdown wakes and joins workers
range intersection uses ordered offsets
sync removes missing/wrong-size local segments
```

部分测试需要 `FileSegment` 文件组一起完成；不要为单测制造假的 segment 实现。

## Review 状态

本文档已完成 review。关键决策：

```text
review exactly three files
port all metadata algorithms and worker lifecycle
do not stub cleanup/download queues
keep KeyMetadata as ordered std::map
use F14 for unordered buckets/sets
preserve exact path layout and key-state transitions
implement Metadata.cpp in the same SCC batch as FileSegment.cpp and required FileCache origins
```
