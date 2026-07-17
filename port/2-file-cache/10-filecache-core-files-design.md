# 10. `FileCache` 核心文件迁移设计

## 结论

这一组严格按两个文件 review：

```text
src/Interpreters/FileCache/FileCache.h
src/Interpreters/FileCache/FileCache.cpp
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/FileCache.h
velox/ch/Interpreters/FileCache/FileCache.cpp
```

This batch is an exact algorithm and lifecycle-semantics port, not a source-level copy or redesign.

本批次精确迁移算法和生命周期语义，不要求逐行复制源码。只允许下文列出的、已经
review 的基础设施替换。以下行为都不能改变：

```text
range lookup, alignment, split and hole filling
get / getOrSet / set / trySet contracts
reservation accounting and query/main priority coordination
foreground and background eviction phases
metadata load and recovery
dynamic resize and failed-eviction rollback
initialization publication and shutdown order
```

这里的 exact 指 behavioral equivalence，不是 source-level identity。允许的基础设施
替换：

```text
FileCacheSettings -> FileCacheConfig
BackgroundSchedulePool -> FileCacheScheduler
ThreadFromGlobalPool / ThreadPool -> FileCacheWorker / FileCacheThreadPool
ConcurrentBoundedQueue -> folly::MPMCQueue with sentinel termination
StatusFile fd ownership -> StatusFile compatibility class backed by folly::File
ServerUUID/common cache user -> manager-injected stable commonUserId
OpenedFileCache singleton -> manager-owned opened-file cache
CH metrics/logging/failpoints/correctness checks -> reviewed shims
boost::noncopyable -> explicitly deleted copy operations
```

`LRU_OVERCOMMIT` / `SLRU_OVERCOMMIT` 仍按
[priority / eviction设计](07-filecache-priority-eviction-design.md)排除：当前 OSS checkout不包含实现，
Velox 第一阶段必须显式拒绝这些 policy，不创建假实现。

## 文件依赖结论

`FileCache.h` 是中心 SCC 的顶层 public API：

```text
FileCache.h
  -> FileSegment.h
  -> Metadata.h
  -> QueryLimit.h
  -> FileCacheSettings.h
  -> FileCacheOriginInfo.h
  -> SplitFileCachePriority.h
```

`FileCache.cpp` 组合此前 review 的算法：

```text
FileCache
  -> CacheMetadata
  -> LRU / SLRU / Split priority
  -> EvictionCandidates
  -> FileSegment
  -> FileCacheQueryLimit
```

实现时 `FileCache.cpp`、`FileSegment.cpp`、`Metadata.cpp` 必须形成同一可链接闭环。
`QueryLimit.h/.cpp` 仍是下一组独立文件 review；本批次只 review `FileCache` 对它的调用
contract。

## `FileCache.h`

### `FileCacheReserveStat`

直接迁移统计结构和累加语义：

```text
releasable
non-releasable
evicting
moving
invalidated
candidates iteration steps
clients iterated
```

总统计和按 `FileSegmentKind` 统计必须同时更新。`magic_enum::enum_count` 可以替换为明确的
kind count，但 array index 仍由 `static_cast<uint8_t>(kind)` 决定。

这些统计主要用于 reservation failure reason；metrics shim 是 no-op 也不能删除统计
control flow。

### public API

直接保留 API 分组：

```text
lifecycle:
  initialize / isInitialized / deactivateBackgroundOperations

origin/path:
  getCommonOrigin / getInternalOrigin
  getCommonOriginWithSegmentKeyType
  getFileSegmentPath / getKeyPath

lookup/create:
  getOrSet / get / getDownloadedContiguousOrEmpty
  set / trySet

reservation/priority:
  tryReserve / tryIncreasePriority / lockCache

remove/admin:
  removeFileSegment / removeKey / removePathIfExists / removeAllReleasable
  iterate / getCacheIterator / getFileSegmentInfos / dumpQueue / sync

settings/stats:
  applySettingsIfPossible
  size/element limits and usage snapshots
```

`FileCache` 不继承 Velox `memory::Cache`，也不改用 `AsyncDataCache` eviction。

### manager-injected runtime dependencies

CH 通过 global `Context` / global thread pool / singleton opened-file cache取得运行时资源。
Velox 由 `FileCacheManager` 显式注入：

```text
FileCacheScheduler
shared physical FileCacheWorkerPool
manager-owned OpenedFileCache
local Velox FileSystem
memory::MemoryPool
stable commonUserId
```

这些对象由 singleton `FileCacheManager` 持有，但 manager 可以管理多个 `FileCache`
实例。manager 的资源必须在所有 cache shutdown/destruction 之后才销毁。

不要把 `FileCacheManager *` 直接传进算法类；构造时传入明确的 non-owning dependencies，
避免 `FileCache` 反向依赖 manager API。

### common/internal origin

CH common origin 的 user id 来自：

```text
configured filesystem_cache_user
or persistent ServerUUID
```

Velox 没有 CH `ServerUUID`。`FileCacheManager::Options.commonUserId` 必须由宿主显式提供，
且满足：

```text
non-empty
stable across process restarts that reuse the cache path
different from reserved "internal"
shared by all FileCache instances in one manager
```

每个 `FileCache` 保存：

```cpp
const OriginInfo common_origin;
```

因此 `getCommonOrigin` 从 static 改成 instance method。所有生产调用点都已经持有具体
cache，可以改为：

```text
FileCache::getCommonOrigin()
  -> cache->getCommonOrigin()
```

system-wide clear 遍历多个 cache 时，逐 cache 使用自己的 common origin。

`getInternalOrigin` 保留固定 `"internal"` maintenance identity。internal origin 可以访问
所有 keys，但不能用于创建普通 key metadata。

`CacheMetadata` 注入 `commonUserId`，用于：

```text
skip common/internal ids in client-access tracking
preserve access filtering
avoid static common-origin global state
```

### member order

保留关键析构顺序：

```text
main_priority declared before metadata
  -> metadata destroyed first
  -> priority iterators remain valid during metadata destruction

StatusFile held for full FileCache lifetime
  -> cache directory remains exclusively locked until background workers and metadata stop
```

manager-owned runtime dependencies比所有 cache 活得更久。

### policy construction

直接迁移：

```text
LRU  -> LRUFileCachePriority
SLRU -> SLRUFileCachePriority
useSplitCache -> SplitFileCachePriority wrapping LRU/SLRU
```

保留：

```text
SLRU ratio
split Data/System routing
main priority eviction callback
query-limit optional construction
```

overcommit policy 在 constructor 直接报 unsupported。因为 overcommit 不存在，
`client_tracking_possible` 始终为 false；idle-client API 可以保留 base no-op contract，
但不能声称该能力已启用。

## `FileCache.cpp`

### initialization

直接迁移 `initialize`：

```text
std::call_once
  -> check whether base path already exists
  -> create base directory if needed
  -> check disk capacity >= configured cache limit
  -> acquire <base>/status exclusive process lock
  -> initialize sync or dispatch async metadata initialization
```

`std::call_once` 在 callback 抛异常时允许后续 retry，和 CH `callOnce` 一致。

`StatusFile` 是 correctness dependency，不是普通日志文件。详细映射见
[基础 shims](../1-dependencies/02-filecache-basic-shims-design.md)：

```text
open <base>/status
folly::File::try_lock
truncate/write process diagnostics
hold fd lock for FileCache lifetime
close and unlink on destruction
```

第二个进程或第二个不同实例不能同时使用同一路径。

`initializeImpl` 保留顺序：

```text
install callbacks/notifiers
create and schedule background cleanup task
load metadata when directory already existed
start metadata download/cleanup workers
create free-space eviction pool/task when enabled
publish is_initialized last
```

初始化异常必须：

```text
deactivate any already-created scheduled task
store init_exception
propagate
```

### scheduler task names

多个 cache 共享一个 `FileCacheScheduler`，而 Folly task name 必须唯一。task name 包含
cache name：

```text
FileCache:<cache-name>:background-cleanup
FileCache:<cache-name>:free-space
```

callback 保留 CH 的 self-reschedule control flow。

### shared worker pool sizing

多个 cache共享 manager-owned dynamic worker pool。`FileCache` 只持有 logical
free-space eviction pool；具体预算公式和 aggregate规则由
[`FileCacheManager`](../3-consumers/02-filecache-manager-design.md#worker-max计算)
统一定义。

dynamic background-thread increase 必须先调用 `setNumThreads` 扩大 max，再创建新的
workers。

每个 cache的 free-space `eviction_pool` 是独立 logical local pool，但不拥有
executor；tasks仍提交到 manager shared `FileCacheWorkerPool`。

### `getImpl`

直接迁移 ordered metadata lookup：

```text
input range is inclusive FileSegment::Range
find lower_bound(range.left)
include previous segment when it overlaps
append ascending intersecting segments
respect fileSegmentsLimit
```

保留 bypass-threshold shortcut：

```text
large range -> one synthetic DETACHED segment
```

`getDownloadedContiguousOrEmpty` 和 write-time `trySet` 必须传
`ignore_bypass_threshold=true`，因为它们需要检查真实 metadata。

evicting/removed metadata 返回 synthetic `DETACHED` segment，不能把正在删除的 segment
重新暴露给 caller。

### range split and hole filling

`splitRange` 直接迁移：

```text
requested size controls whether another segment is needed
aligned size controls the final segment's possible range
every created segment <= maxFileSegmentSize
```

`fillHolesWithEmptyFileSegments` 保留两种行为：

```text
getOrSet:
  holes -> metadata-owned EMPTY segments

get:
  holes -> synthetic DETACHED segments
```

同时保留：

```text
prefix/suffix alignment
fileSegmentsLimit truncation
non-aligned requested right boundary
ordered, non-overlapping output
```

不能把 inclusive ranges 局部改成 half-open ranges。

### `trySet` / `set`

直接迁移：

```text
ignore bypass threshold
reject any intersecting existing segment
unbounded -> one segment
bounded -> split by maxFileSegmentSize
set throws when trySet returns null
```

即使 write-through 和 `TemporaryDataOnDisk` 第一阶段后置，也保留这两个 `FileCache`
文件内 API；不为当前 caller 范围删除源码表面。

### `getOrSet`

保持完整算法：

```text
clip requested range by known file size
compute aligned left/right
lock/create key metadata
get existing intersections
respect fileSegmentsLimit
extend only through uncovered aligned prefix/suffix
create EMPTY segments for holes
return one contiguous ordered holder
```

必须保持关键不变量：

```text
front covers requested offset
back covers requested right unless fileSegmentsLimit truncates the batch
new segments do not intersect existing metadata
holder references prevent removal while in use
```

### `get`

`get` 只查询，不创建 metadata：

```text
existing intersections returned
holes filled with DETACHED placeholders
missing key/range -> one DETACHED placeholder
```

placeholder 生命周期只到 holder destruction，状态不可变。

### `getDownloadedContiguousOrEmpty`

cache-only lookup 必须：

```text
never create key/segment
ignore bypass threshold
require full contiguous coverage
require downloaded prefix to reach every requested byte
return EMPTY holder on any hole/missing prefix
```

不能只接受 `DOWNLOADED` state：committed unbounded/ephemeral segment 可以保持
`PARTIALLY_DOWNLOADED`。

### `addFileSegment`

直接迁移：

```text
size > 0
reject intersecting range
create FileSegment with metadata background-download capability
insert exactly once by offset
```

`FileSegment` 保存 non-owning `FileCache *`，因此 cache 必须比所有 metadata/holders
活得更久。

### reservation

`tryReserve` 保留：

```text
initialization check
dynamic-resize shared try-lock
active reserver count
failure reason
```

`doTryReserve` 保留 query/main 两层 priority：

```text
optional per-query limit
main cache limit
HoldSpace reservation under cache-state lock
fast path for existing iterator with no eviction
candidate collection
filesystem deletion without cache locks
queue/state finalization
priority entry create/increment
FileSegment reserved_size update
key base-directory creation
```

锁和 phase 顺序不能扁平化：

```text
collect accounting under state/priority guards
mark/remove queue entries
release locks
delete files
finalize queue and state
```

reservation failure不能留下未 invalidated zombie queue entry。

### foreground eviction

直接迁移：

```text
query priority candidates first when query limit requires eviction
main priority candidates second
separate foreground/reserve eviction cursor
collect invalidated entries
delete without lock
afterEvictWrite under cache write lock
afterEvictState under state lock
```

failed filesystem eviction 必须 finalize candidate state后抛异常，不能 success-shaped fallback。

### background free-space keeper

该能力不能 disabled/stub。配置中的：

```text
keepFreeSpaceSizeRatio
keepFreeSpaceElementsRatio
keepFreeSpaceRemoveBatch
keepFreeSpaceEvictionThreads
```

已经定义真实行为。

保持 collector/remover/finalizer pipeline：

```text
scheduled collector:
  compute desired live size/elements
  collect batches
  push to remover queue

eviction_pool removers:
  delete files without cache locks
  push completed batches to finalization queue

collector:
  finalize queue/state
  recompute target with in-flight discount
```

两条 queue使用 `folly::MPMCQueue<std::optional<EvictionBatchPtr>>`。详细映射见
[`FileCache` 底层设施替换矩阵](../1-dependencies/01-filecache-infra-mapping.md)：

```text
std::nullopt is finish sentinel
input sentinel propagates through all removers
last remover sends finalization sentinel
collector drains finalization queue before waiting for eviction_pool
```

`running_removers` 必须在 scheduled task 对 worker 可见之前递增；schedule 失败时回滚。
不能照抄“schedule 返回后再递增”的竞态，否则 worker 可能错误地把自己判断为 last remover。

所有异常路径必须保证：

```text
input queue terminated
all scheduled removers exit
all completed batches finalized
eviction_pool wait completes
```

### background cleanup

保留一个 scheduled task：

```text
remove invalidated priority entries
optionally run idle-client sweep
full cleanup batch -> schedule immediately
otherwise -> schedule after computed interval
```

第一阶段没有 overcommit policy，因此 client tracking 不会启用；base priority 的 idle-client
API 是 no-op。不能把该路径宣称为已支持的 per-client eviction。

### metadata load

保留 parallel listing/loading 算法。CH bounded queue映射详见
[`FileCache` 底层设施替换矩阵](../1-dependencies/01-filecache-infra-mapping.md)：

```text
loadMetadataThreads == 1 / no loading workers:
  do not construct zero-capacity MPMCQueue
  listing worker loads directly

loading workers present:
  folly::MPMCQueue<std::optional<KeyDirectoryWork>>
  producers use nonblocking write
  full queue -> producer loads key directly
  one propagated nullopt sentinel terminates all consumers after drain
```

first exception 必须：

```text
store first exception
set stop flag
start sentinel termination exactly once
join all workers
rethrow first exception
```

不能让 consumer 永久阻塞在 `blockingRead`。

### `loadMetadataForKey`

保持三阶段恢复：

```text
1. scan/parse files without cache lock
2. add priority entries under one write/state lock pair
3. construct and insert FileSegment objects
```

保留文件名规则：

```text
<offset>             -> legacy regular, stat for size
<offset>_<size>      -> regular, trust size from name
<offset>_temporary   -> remove on startup
<offset>_persistent  -> compatibility removal
```

保留 duplicate-offset recovery：

```text
legacy <offset> wins over suffixed duplicate
ignored duplicate is not deleted on recovery path
```

不 fit 新配置容量的 segment：

```text
do not add priority/metadata
remove file
```

load 完成后检查 priority state并 shuffle startup order。

### metadata queues: Folly termination protocol

`folly::MPMCQueue` 没有 native close。sentinel 是基础设施替换，不改变业务队列元素顺序：

```text
all real work is enqueued before normal finish sentinel
consumer that reads sentinel re-enqueues it, then exits
the sentinel walks through every consumer
```

异常 close 使用 `std::once_flag` 保证只注入一个 sentinel。work item 已验证满足
`MPMCQueue` 的 nothrow move/destruct requirements。

### settings reload

`applySettingsIfPossible` 属于本文件真实算法，不能只留空接口。直接迁移：

```text
background download queue limit
background download thread count
background download max segment size
reserve granularity
max size/elements dynamic resize
max file segment size
idle-client settings
metrics flags
```

dynamic background-thread increase：

```text
manager expands shared worker pool budget first
metadata creates workers second
actual settings update last
```

### dynamic resize

保持 resize exclusive lock 与 reservation shared lock的互斥：

```text
reserve/priority increase skip while resize active
resize waits up to dynamicResizeLockWaitMs
```

扩容/无需 eviction：

```text
modify limits under state lock
```

缩容：

```text
collect candidates
remove queue entries
modify limits
release locks
delete files
```

filesystem deletion失败时必须：

```text
restore previous limits
restore failed candidates to original queue type
restore removed flags and queue iterators
keep successfully evicted candidates removed
```

SLRU restore 必须保留原 protected/probationary queue type。

### shutdown

直接迁移顺序：

```text
set shutdown
stop/join async metadata main worker
deactivate and wait scheduled tasks
wait per-cache eviction_pool
cancel metadata queues
requestStop/join background download workers
join metadata cleanup worker
```

manager 之后才能：

```text
shutdown shared worker pool
shutdown scheduler
clear opened-file cache
```

`FileCache` destructor 仍兜底调用 `deactivateBackgroundOperations`，但 manager 应显式
shutdown。

### introspection and admin APIs

保留：

```text
remove key/segment/path
remove all releasable
iterate / iterator
segment info snapshots
priority dump
usage stats
cache paths
sync broken files
approximate size/elements metrics getters
```

metrics implementation可 no-op，但不能删除会被 manager/system integration 使用的
snapshot API。

## 外部依赖映射

详细映射的 single source of truth：

```text
scheduler                   -> ../1-dependencies/05
query/TID caller scope      -> ../1-dependencies/06
worker/local thread pools   -> ../1-dependencies/04
metrics/debug no-op shims   -> ../1-dependencies/03
locks/filesystem/StatusFile -> ../1-dependencies/02
priority/eviction           -> 07
metadata                    -> 08
FileSegment                 -> 09
MPMC bounded queues         -> ../1-dependencies/01
```

本文件不重复定义这些 helper 的实现。

## 测试要求

### lifecycle

```text
initialize once
failed initialize can retry only from clean metadata state
sync and async metadata initialization
second live cache on same path fails status lock
status lock released/unlinked after destruction
is_initialized published only after metadata/workers are ready
shutdown stops and joins every worker/task
```

### multi-cache manager resources

```text
manager can hold multiple named/path caches
same path + same config can reuse one cache
same path + different config is rejected
task names are unique per cache
shared worker pool max equals sum of conservative per-cache budgets
dynamic executor grows on demand and retires idle workers
dynamic background-thread increase expands pool before worker creation
one cache's long-running workers do not starve another cache
```

### lookup/ranges

```text
getOrSet alignment and hole creation
fileSegmentsLimit truncation
get never creates metadata
get uses DETACHED placeholders for holes
getDownloadedContiguousOrEmpty ignores bypass threshold
cache-only lookup rejects holes and insufficient downloaded prefixes
trySet rejects intersections
unbounded set creates one segment
```

### reservation/eviction

```text
existing-entry reserve fast path
first reserve creates priority iterator
query limit and main limit coordination
foreground eviction releases enough size/elements
failed deletion finalizes candidate state and propagates
concurrent reserve cursors make progress
```

### free-space keeper

```text
size-ratio cleanup
elements-ratio cleanup
multiple remover workers
MPMC input sentinel drains all batches
last remover terminates finalization queue
push timeout still finalizes already removed batches
schedule failure rolls back running-remover count
shutdown during collection drains and joins
```

### metadata load

```text
single-thread direct-load path without zero-capacity queue
parallel listing/loading
first-exception propagation without blocked consumers
legacy and size-suffixed names
temporary/persistent cleanup
duplicate offset recovery
capacity overflow removal
common user and split key-type path layouts
```

### dynamic resize/reload

```text
grow limits
shrink with eviction
reserve skipped during resize
timeout leaves previous limits
failed eviction restores limits and original queue types
background download thread increase/decrease
```

## Review 状态

`FileCache.h` 和 `FileCache.cpp` 已按文件 review。核心算法可做 exact behavioral port。
不能 stub metadata load、background free-space keeping、dynamic resize、reservation、
eviction 或 shutdown。差异只限已明确的 Velox/Folly runtime dependencies、无 overcommit
实现的显式拒绝，以及 no-op observability shims。
