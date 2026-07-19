# 05. `ShardedMap.h` 迁移设计

## 结论

本批次严格按一个文件 review：

```text
src/Interpreters/FileCache/ShardedMap.h
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/ShardedMap.h
```

This batch is an exact sharding, locking, callback, and size-accounting port, not a redesign.

本批次精确迁移：

```text
32 default shards
hash modulo shard selection
one mutex per shard
callback executes under shard lock
sequential all-shard traversal
exception-safe total size accounting
relaxed atomic size snapshot
```

只允许：

```text
std::unordered_map -> folly::F14FastMap
ProfiledMutexLock -> std mutex lock + no-op profile shim
boost::noncopyable -> explicitly deleted copy operations
std::hash specialization -> optional explicit Hash template parameter
scope guard include -> Folly/std scope guard with same exception behavior
```

## 文件功能

`ShardedMap` 是一个小型 lock-sharded map：

```text
hash(key) % shardCount
  -> lock one shard
  -> execute callback against that shard's map
```

不同 keys落到不同 shards 时可以并发操作，避免一个 global map mutex。

当前使用方：

```text
CacheMetadata::origins:
  shared FileCacheOriginInfo dedup pool

CacheUsagePerUser::clients_map:
  overcommit per-user usage map
```

第一阶段 overcommit后置，但 origin pool是 metadata 主路径，不能 stub `ShardedMap`。

## template

CH：

```cpp
template <typename Key, typename Value, size_t num_shards = 32>
class ShardedMap;
```

Velox target：

```cpp
template <
    typename Key,
    typename Value,
    size_t num_shards = 32,
    typename Hash = std::hash<Key>>
class ShardedMap;
```

增加 `Hash` 只是为了复用
[`FileCacheOriginInfo` 设计](02-filecache-origin-segment-type-design.md)中已 review 的显式
`OriginPoolKeyHash`，不改变 sharding
算法。

必须：

```cpp
static_assert(num_shards > 0);
```

避免 `% 0`。默认仍是 32，不重新调参。

## map mapping

CH：

```cpp
using Map = std::unordered_map<Key, Value>;
```

Velox：

```cpp
using Map = folly::F14FastMap<Key, Value, Hash>;
```

`F14FastMap` 安全依据：

```text
all map operations happen under owning shard mutex
no current caller returns map iterator/reference/value address
origin caller returns a copied shared_ptr
all-shard cleanup erases only inside callback
```

如果未来 callback 需要把 map element pointer/reference留到锁外，必须重新 review并考虑
`F14NodeMap`；当前不要提前使用 node container。

## build registration

`ShardedMap.h` 是 later `Metadata` / `CacheUsagePerUser` 直接 include 的 public
header。创建时即加入 `velox_ch_filecache` 现有 non-mono `PUBLIC HEADERS`
file set，与 `FileCacheOriginInfo.h` / `FileCacheUtils.h` 一致：

```text
mono:
  source-tree include path; do not call target_sources on the alias

non-mono:
  existing FILE_SET HEADERS += ShardedMap.h
  focused consumer links velox_ch_filecache + GTest only
```

必须运行独立 non-mono focused build/test，防止 mono alias 或 focused test 的
直接 Folly/fmt/exception 依赖掩盖 public interface 缺失。

## hash consistency

target保存/构造同一种 `Hash` 用于：

```text
shard selection
F14FastMap internal hashing
```

`withShard`：

```cpp
Shard & shard = shards_[Hash{}(key) % num_shards];
```

origin pool实例：

```cpp
ShardedMap<
    OriginPoolKey,
    OriginInfoPtr,
    32,
    OriginPoolKeyHash>
```

`OriginPoolKeyHash` 按
[`FileCacheOriginInfo` 设计](02-filecache-origin-segment-type-design.md)保持 CH 行为：只 hash
`user_id`，equality仍比较
`user_id` / `weight` / `segment_type` 全字段。因此同一 user的不同 weight/type会落到同一
shard；不能擅自改成 hash 全字段。

string client map继续使用：

```cpp
std::hash<std::string>
```

## shard structure

直接迁移：

```cpp
struct Shard
{
    mutable std::mutex mutex;
    Map map;
};
```

以及：

```cpp
mutable std::array<Shard, num_shards> shards_;
mutable std::atomic<size_t> total_count_{0};
```

`withShard` / `forEachShard` 是 const但允许 mutation，这就是该 wrapper 的 API contract；
mutation被内部 mutex序列化。

## lock profiling

CH 构造函数接收：

```cpp
ProfileEvents::Event lock_wait_event
```

并使用 `ProfiledMutexLock`。

第一阶段 metrics 是 no-op，但保留 constructor参数和调用形态：

```cpp
explicit ShardedMap(ProfileEvents::Event lock_wait_event);
```

target lock helper可以是：

```text
no-op timing guard
std::unique_lock<std::mutex>
```

不能删除参数，否则 `CacheMetadata` / `CacheUsagePerUser` 算法文件产生额外 diff，也失去
未来接 metrics 的位置。

锁只覆盖 callback；不跨不同 shards。

## `withShard`

直接迁移 contract：

```cpp
template <typename F>
auto withShard(const Key & key, F && f) const;
```

步骤：

```text
compute shard
lock shard
record map.size before callback
install exception-safe size-accounting guard
invoke f(map)
return callback result by value
unlock
```

当前 origin dedup：

```text
find/emplace OriginInfoPtr
return copied shared_ptr
```

callback contract：

```text
may read or mutate its shard map
must not recursively call withShard/forEachShard on the same ShardedMap
must not return iterator/reference/pointer into map
```

same-shard reentry会死锁；escaping map internals会在 unlock/rehash后失效。接口文档必须明确
这些限制。

## `forEachShard`

直接迁移：

```text
iterate shards in array order
lock one shard
run callback
unlock
move to next shard
```

它不同时锁所有 shards，因此：

```text
callback does not get a globally atomic map snapshot
other shards may mutate between iterations
```

这符合当前使用：

```text
remove all shared origins for one user
best-effort per-shard cleanup/snapshot
```

禁止为了“全局一致”一次锁住全部 shards；会改变 contention/deadlock behavior。

## size accounting

每次 callback前后比较 shard map size：

```text
after > before -> total_count.fetch_add(delta, relaxed)
after < before -> total_count.fetch_sub(delta, relaxed)
```

accounting guard必须在 callback 抛异常时仍执行。不能把 update放在正常 return之后。

`size`：

```cpp
size_t size() const
{
    return total_count_.load(std::memory_order_relaxed);
}
```

语义是 lock-free concurrent snapshot：

```text
after a completed mutation callback, its delta is reflected
during concurrent operations, caller may observe a transient snapshot
no ordering with map element contents is promised
```

该值用于 client/origin count，不用于 correctness decision。

## exception safety

如果 callback：

```text
inserts/erases
then throws
```

map mutation已经发生，`total_count` 必须按实际 final size更新后再传播原异常。

scope guard本身不得抛出。

## include surface

target header需要：

```text
array
atomic
cstddef
functional
mutex
type_traits/hash support
folly/container/F14Map.h
profile-event shim
scope guard
```

不引入 metadata、priority或完整 `FileCache` 定义。

## 测试要求

```text
same key always selects same shard
different shards can execute concurrently
same shard serializes callbacks
withShard insert/lookup returns copied value
forEachShard visits all 32 shards
forEachShard erase updates size
insert updates size
erase updates size
callback throw after insert still updates size and propagates
callback throw after erase still updates size and propagates
size is correct after completed concurrent inserts/erases
OriginPoolKeyHash preserves same-user shard behavior
header/API rejects zero shard count
copy operations are deleted
```

不写依赖 sleep 的并发测试；使用 barriers/latches控制 callback overlap。

## Review 状态

`ShardedMap.h` 已按文件 review。shard count、hash modulo、per-shard lock、callback
contract和 exception-safe size accounting直接迁移。容器替换为 `F14FastMap`，并增加
显式 `Hash` 模板参数以复用已 review 的 `OriginPoolKeyHash`。
