# 17. `QueryLimit` 文件迁移设计

## 结论

这一组严格按两个文件 review：

```text
src/Interpreters/FileCache/QueryLimit.h
src/Interpreters/FileCache/QueryLimit.cpp
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/QueryLimit.h
velox/ch/Interpreters/FileCache/QueryLimit.cpp
```

This batch is an exact algorithm and lifecycle-semantics port, not a source-level copy or redesign.

本批次精确迁移 query-local accounting、并发和 holder 生命周期语义，不要求逐行复制源码。
只允许下文列出的、已经 review 的基础设施替换。以下行为都不能改变：

```text
one query context per non-empty query id
query-local LRU accounting
query/main priority coordination
concurrent holder revival/release
last-holder map removal
out-of-lock QueryContext destruction
```

允许的替换：

```text
CurrentThread query id -> FileCacheQueryIdScope::currentQueryId
FilesystemCacheSettings -> FileCacheReadOptions
std::unordered_map -> folly::F14FastMap
boost::noncopyable -> explicitly deleted copy operations
Exception -> FileCache exception compatibility layer
```

## 功能

`FileCacheQueryLimit` 防止单个 query 的远端扫描把整个共享 `FileCache` 填满。

它限制的是：

```text
one query newly reserves/downloads into FileCache
```

它不限制：

```text
query total read bytes
remote reads that bypass cache
reads from segments already present in FileCache
```

例如全局 cache 上限为 100 GiB，per-query download limit 为 1 GiB：

```text
query A scans 200 GiB
  -> it may read all 200 GiB
  -> it may newly retain at most 1 GiB under its query-local accounting
```

同一 query 的多个 driver/stream 共享一个 query context，因此共同使用这 1 GiB limit。

不同 query 访问同一张表时，实际 cache segment 仍全局共享，但 query-local accounting
独立：

```text
query A misses range X and downloads it
  -> A accounts the new cache write

query B later reads range X
  -> cache hit
  -> B does not consume query-local download budget

query B misses range Y
  -> B can use its own independent budget
```

两个 query 同时 miss 同一个 segment 时，只有抢到 downloader lease 并实际
`reserve` / `write` 的 query 消耗 budget；另一个 query 等待或复用下载结果。

超过 query limit 后：

```text
skipDownloadIfExceedsPerQueryCacheWriteLimit = true:
  stop writing new bytes to cache
  continue reading the remote source

skipDownloadIfExceedsPerQueryCacheWriteLimit = false:
  evict older entries attributed to this query by query-local LRU
  cache the new segment
```

query context 生命周期结束时只删除 query-local LRU/accounting。已经成功写入的 segment
继续留在 global `FileCache`，后续 query 可以命中；最终仍受 global priority 和容量限制。

该能力只有同时满足以下条件才启用：

```text
FileCacheConfig.enableFilesystemQueryCacheLimit = true
FileCacheReadOptions.maxDownloadSizePerQuery > 0
query id is non-empty
```

## 文件依赖

`QueryLimit.h` 依赖：

```text
Guards.h
LRUFileCachePriority.h
FileCacheKey / FileCacheKeyAndOffset
FileCache fwd types
```

`QueryLimit.cpp` 依赖：

```text
FileCache::lockCache
KeyMetadata
FileCacheQueryIdScope
FileCacheReadOptions
```

它不依赖 Velox query executor 或 `ConnectorQueryCtx` 本体。query id 已由
`FileCacheInputStream` 同步入口放入 `FileCacheQueryIdScope`。

## `QueryLimit.h`

### `FileCacheQueryLimit`

直接迁移：

```text
query id -> shared QueryContext
get/create/remove context
dedicated query-map mutex
```

query limit object 由一个 `FileCache` 独占：

```cpp
using FileCacheQueryLimitPtr = std::unique_ptr<FileCacheQueryLimit>;
```

setting 禁用时 `FileCache` 不创建该对象。

### `QueryContext`

每个 query context 保存：

```text
query-local LRU priority
key+offset -> query priority iterator records
recache-on-limit-exceeded flag
```

query-local priority 直接使用：

```cpp
LRUFileCachePriority(
    IFileCachePriority::QueueType::Query,
    query_cache_size,
    /* max_elements */ 0);
```

不能替换成 Velox cache 或简单 byte counter。query priority 必须参与与 main priority 相同的
candidate/iterator accounting。

### `records`

CH：

```cpp
std::unordered_map<
    FileCacheKeyAndOffset,
    Priority::IteratorPtr,
    FileCacheKeyAndOffsetHash>
```

Velox：

```cpp
folly::F14FastMap<
    FileCacheKeyAndOffset,
    Priority::IteratorPtr,
    FileCacheKeyAndOffsetHash>
```

安全依据：

```text
all accesses happen while CachePriorityGuard::WriteLock is held
no map iterator/reference escapes
stored Priority::IteratorPtr has independent shared ownership
```

### `QueryContextHolder`

直接迁移 move-only RAII：

```text
query_id
non-owning FileCache*
non-owning FileCacheQueryLimit*
shared QueryContext
```

用 deleted copy operations 替代 `boost::noncopyable`。

raw pointers 的 lifetime contract：

```text
FileCacheInputStream owns FileCachePtr
QueryContextHolder is declared after FileCachePtr
reverse destruction destroys holder before FileCachePtr
FileCache owns FileCacheQueryLimit
```

目标 `FileCacheInputStream` 必须保持相同成员顺序。不能让 holder 比 cache 活得更久。

### query map

CH：

```cpp
std::unordered_map<String, QueryContextPtr>
```

Velox：

```cpp
folly::F14FastMap<String, QueryContextPtr>
```

value address/iterator 不逃出 `query_map_mutex`，所以使用 `F14FastMap`。

### dedicated mutex

必须保留 `query_map_mutex`。query map 从两种不同 cache lock路径访问：

```text
reserve lookup:
  CacheStateGuard::Lock
    -> query_map_mutex

holder create/remove:
  CachePriorityGuard::WriteLock
    -> query_map_mutex
```

`CacheStateGuard` 和 `CachePriorityGuard` 互不序列化，不能依赖其中任意一个单独保护 map。

不能把 mutex 删除，也不能把 query map 操作统一塞进更高层全局 cache write lock；reserve
lookup 需要保持现有 lock phase。

## `QueryLimit.cpp`

### current query identity

CH：

```text
CurrentThread initialized
query context exists
query id non-empty
```

Velox：

```text
FileCacheQueryIdScope::currentQueryId non-empty
```

`FileCacheInputStream` 已显式拥有 query id 和 `QueryContextHolder`。`QueryLimit` 不需要第二套
thread-local query context。

background workers 没有 `FileCacheQueryIdScope`，因此不应用 foreground per-query limit。

query id 来源：

```text
core::QueryCtx::queryId
  -> ConnectorQueryCtx::queryId
  -> FileCacheRequestContext::queryId
  -> FileCacheInputStream
```

宿主必须保证：

```text
all drivers/streams of one query use the same query id
different concurrently-live queries use different query ids
```

如果两个并发 query 复用同一个 id，它们会共享一个 query-local limit context；这是宿主
identity contract 违反，不应在 `QueryLimit` 内猜测或重新生成 id。

### `tryGetQueryContext`

直接迁移：

```text
empty current query id -> null
lock query_map_mutex
lookup current query id
return shared QueryContext or null
```

保留 `CacheStateGuard::Lock` 参数，表达 caller 已处于 reservation state-accounting phase，
即使 query map 自身由独立 mutex 保护。

### `getOrSetQueryContext`

目标签名：

```cpp
QueryContextPtr getOrSetQueryContext(
    const std::string & query_id,
    const FileCacheReadOptions & options,
    const CachePriorityGuard::WriteLock &);
```

仅使用：

```text
options.maxDownloadSizePerQuery
options.skipDownloadIfExceedsPerQueryCacheWriteLimit
```

映射：

```text
query cache size = maxDownloadSizePerQuery
recache on exceeded = !skipDownloadIfExceedsPerQueryCacheWriteLimit
```

empty query id 返回 null。

同 query id 的后续 holder 复用已有 context；不按后续 options 重建 priority。这保留 CH
“一个 query id 一个 limit context”的语义。

### query-local records

`add`：

```text
add zero/current size entry to query LRU
insert key+offset record
duplicate -> remove just-added priority entry and throw
```

`remove`：

```text
record must exist
remove priority entry
erase record
```

`tryGet`：

```text
return iterator when this query already accounts the key+offset
otherwise null
```

三者都要求 `CachePriorityGuard::WriteLock`。

### concurrent holder lifecycle

map 本身持有一个 `shared_ptr<QueryContext>`；每个 live holder再持有一个。

创建：

```text
query_map + holder1                  -> use_count 2
query_map + holder1 + holder2        -> use_count 3
```

release 必须在 `query_map_mutex` 下先 reset 当前 holder 的 reference，再判定：

```text
if map entry still points to this context
and map entry is now sole owner:
  move context out of map
  erase map entry
else:
  keep map entry
```

不能在 reset 前读 `use_count`，也不能把 holder reference 在 mutex 外 reset。否则两个
concurrent releases 都可能认为“还有其他 holder”，最终只剩 map reference并永久泄漏
stale query context。

### transient lookup reference invariant

`tryGetQueryContext` 会返回一个 transient `shared_ptr` 给当前 reservation。该 reference
在 mutex 外释放。

基于 `use_count` 的 last-holder 算法依赖：

```text
every foreground reservation that owns a transient QueryContextPtr
is covered by at least one live QueryContextHolder for the same query id
```

当前调用链满足：

```text
FileCacheInputStream construction
  -> creates QueryContextHolder

FileCacheInputStream synchronous operation
  -> FileCacheQueryIdScope
  -> FileSegment::reserve
  -> FileCacheQueryLimit::tryGetQueryContext
```

stream 不能在自己的 reserve still-in-flight 时并发销毁 holder。

如果未来新增无 holder 的 foreground reserve入口，不能复用当前 last-holder 判定；必须先
扩展 lifetime protocol，而不是让 transient reference成为最后一个外部 owner。

### out-of-lock destruction

`removeQueryContext` 返回被移出 map 的 `doomed` context，而不是在 map/cache lock内销毁。

holder destructor：

```text
declare doomed outside lock scope
acquire FileCache cache write lock
removeQueryContext
release cache write lock
destroy doomed
```

`QueryContext` 可能包含大量 records 和 query-LRU entries。必须在 cache write lock释放后
销毁，避免长时间阻塞不相关 reserve/eviction。

### destructor exception safety

`QueryContextHolder` destructor 隐式 `noexcept`。以下路径不得抛出：

```text
cache write lock acquisition under valid lifetime
query map reference release
map erase
out-of-lock QueryContext destruction
```

invalid lifetime 是程序错误，不能通过 catch-and-ignore 隐藏。

## 与 `FileCache` 的协同

`FileCache::getQueryContextHolder`：

```text
query limit disabled -> null holder
maxDownloadSizePerQuery == 0 -> null holder
otherwise create/reuse query context under cache write lock
```

`FileCache::doTryReserve`：

```text
under cache-state lock:
  find current query context
  evaluate query priority limit
  optionally collect query eviction info

then:
  collect main priority eviction info
  evict according to query priority first when required
  maintain both main and query iterators
```

query-local LRU 只限制当前 query 下载进 cache 的内容，不替代全局 LRU/SLRU。

## 测试要求

### identity/options

```text
empty query id returns null
FileCacheQueryIdScope selects matching context
background/no-scope lookup returns null
maxDownloadSizePerQuery creates correct query LRU limit
skipDownloadIfExceeds maps to recache flag inversion
```

### records

```text
add / tryGet / remove
duplicate add rolls back priority entry
remove missing record throws
```

### holder lifecycle

```text
single holder release removes map entry
two holders share one context
first release keeps revived context
last release removes exactly once
concurrent release does not leak map entry
same query id can create a fresh context after full release
doomed context destruction occurs outside cache write lock
```

### integration

```text
query limit rejects reserve when recache disabled
query limit evicts query-local older segment when recache enabled
main cache limit still applies
two query ids have independent query-local LRUs
parallel streams of one query share accounting
```

## Review 状态

`QueryLimit.h` 和 `QueryLimit.cpp` 已按文件 review。query-local LRU、records、dedicated map
mutex、concurrent holder release和 out-of-lock destruction直接迁移。差异只限
`FileCacheQueryIdScope`、`FileCacheReadOptions`、F14 containers和基础兼容类型。
