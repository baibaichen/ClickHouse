# 13. Priority / eviction 文件迁移设计

## 结论

priority / eviction 是 `FileCache` 本体的第一批算法文件。第一阶段按文件直接迁移
ClickHouse OSS 中存在的实现，不引入 Velox `AsyncDataCache` / `SsdCache` eviction 语义：

本批次是算法的精确迁移，不是重新设计。只允许替换已经 review 的基础设施。
`LRUFileCachePriority`、`SLRUFileCachePriority`、`SplitFileCachePriority`、
`EvictionCandidates` 和 `CacheUsage` 的行为都不能改变。
当前 OSS checkout 中不存在 `OvercommitFileCachePriority` 的实现，因此本批次不包含
overcommit。

```text
src/Interpreters/FileCache/CacheUsage.h
src/Interpreters/FileCache/EvictionCandidates.h
src/Interpreters/FileCache/EvictionCandidates.cpp
src/Interpreters/FileCache/IFileCachePriority.h
src/Interpreters/FileCache/IFileCachePriority.cpp
src/Interpreters/FileCache/LRUFileCachePriority.h
src/Interpreters/FileCache/LRUFileCachePriority.cpp
src/Interpreters/FileCache/SLRUFileCachePriority.h
src/Interpreters/FileCache/SLRUFileCachePriority.cpp
src/Interpreters/FileCache/SplitFileCachePriority.h
src/Interpreters/FileCache/SplitFileCachePriority.cpp
```

`FileCache` 在非 distributed-cache / Cloud 路径下会拒绝 overcommit policy。
第一阶段不为 `OvercommitFileCachePriority` 制造假实现。

## 文件分组

### 接口与通用状态

```text
IFileCachePriority.h
IFileCachePriority.cpp
EvictionCandidates.h
EvictionCandidates.cpp
CacheUsage.h
```

职责：

```text
priority queue entry / iterator 抽象
entry state machine
space hold RAII
eviction info aggregation
eviction candidate collection/finalization
per-user usage structure, kept for later overcommit / idle-client support
```

### 算法实现

```text
LRUFileCachePriority.h / .cpp
SLRUFileCachePriority.h / .cpp
SplitFileCachePriority.h / .cpp
```

职责：

```text
LRU: single queue, head lowest priority, tail highest priority
SLRU: protected + probationary queues, promotion on repeated access
Split: Data/System sub-priorities, General routes to Data
```

## `IFileCachePriority`

直接迁移接口。不要缩小接口，即使第一批暂时只测 LRU/SLRU 的一部分。

必须保留：

```text
QueueType
QueueEntryType
Entry
Iterator
InvalidatedEntryInfo
HoldSpace
EvictionCursor
CollectStatus
collectEvictionInfo
collectCandidatesForEviction
add / addForRestore
tryIncreasePriority
removeInvalidatedEntries
setInvalidateNotifier
setOnEvictCallback
```

关键语义：

```text
Entry::State:
  Active
  PreActive
  Evicting
  Moving
  Invalidated
  Removed

PreActive:
  SLRU queue transition 中的临时状态
  必须和 SLRUIterator pointer update 一起变为 Active

HoldSpace:
  constructor holds freed space
  destructor releases if not explicitly released
```

第一阶段 `ProfileEvents` / logging / correctness check 走 no-op shim，不改变接口。

## `LRUFileCachePriority`

直接迁移 CH 算法：

```text
queue head = lowest priority
queue tail = highest priority
add inserts at tail
tryIncreasePriority moves entry toward tail
collectCandidatesForEviction scans from eviction cursor / head
removeInvalidatedEntries lazily removes invalidated refs
```

必须保留两个 eviction cursors：

```text
reserve_eviction_pos
background_eviction_pos
```

原因：

```text
foreground reserve eviction 和 background free-space keeping 不应互相重置扫描位置
```

`State` 里的 size/elements 计数直接迁移。`CurrentMetrics` 调用走 no-op shim。

容器选择：

```text
std::list<EntryPtr> 保留
std::deque<InvalidatedRef> 保留
```

不要替换成 vector/F14。迭代器稳定性是算法语义的一部分。

## `SLRUFileCachePriority`

直接迁移 CH 算法：

```text
probationary queue: 第一次访问 / 新进入
protected queue: 多次访问后提升
size_ratio: protected/probationary 容量比例
```

必须保留：

```text
SLRUIterator
entry_mutex
is_protected atomic
PreActive -> Active 过渡
addForRestore 根据 original_queue_type 恢复 protected/probationary
```

不要简化成“两个普通 LRU 队列”后让上层重新找 entry。`SLRUIterator` 负责在 queue move
时维护外部 iterator 语义。

`FailPoint` 走 no-op shim。迁移时保留调用点即可。

## `SplitFileCachePriority`

直接迁移 CH 算法：

```text
Data priority
System priority
```

路由规则必须保持：

```text
Data    -> Data priority
General -> Data priority
System  -> System priority
```

容量拆分：

```text
data_size   = max_size * (1 - system_segment_size_ratio)
system_size = max_size * system_segment_size_ratio
```

`modifySizeLimits` 必须保持 rollback 语义：

```text
先改 Data
System 修改失败时 rollback Data
再更新 wrapper 自己的 max_size / max_elements
```

`SplitIterator::getType` 保持：

```text
Data -> SplitCache_Data
System -> SplitCache_System
```

因为 `General` 被路由到 Data，`SplitIterator` 中不会把 `General` 作为独立 queue type。

## `EvictionCandidates`

直接迁移两阶段 eviction 语义：

```text
evict():
  删除 filesystem / metadata 中的 segment
  不直接删除 priority queue entry

afterEvictWrite():
  在 priority write lock 下运行 write callbacks

afterEvictState():
  在 cache state lock 下 invalidate queue entries / state callbacks
```

这个分离很重要：实际文件删除发生在不持 priority lock 的阶段，queue/state 更新在锁下完成。

必须保留：

```text
queue_entries_to_invalidate
after_evict_write_callbacks
after_evict_state_callbacks
original_queue_types
failed_candidates
on_evict_callback
```

`original_queue_types` 用于 dynamic resize 失败后恢复 entry 原队列类型，尤其是 SLRU
protected/probationary。

容器替换按 `04` 的总映射执行：

```text
absl::flat_hash_map<FileCacheKey, KeyCandidates, std::hash<FileCacheKey>>
  -> folly::F14FastMap<FileCacheKey, KeyCandidates, FileCacheKeyHash>

absl::flat_hash_set<CacheUsagePtr>
  -> folly::F14FastSet<CacheUsagePtr>
```

如果某个 value 地址需要跨 rehash 稳定，再改 `F14NodeMap`。当前设计不预设需要。

## `CacheUsage`

`CacheUsage` / `CacheUsagePerUser` 主要服务 overcommit 和 idle-client tracking。

第一阶段保留文件和类型，但 overcommit 算法后置：

```text
CacheUsage
CacheUsagePtr
CacheUsagePerUser
CacheUsageStatGuard
```

保留原因：

```text
IFileCachePriority 接口已经引用 CacheUsagePtr
EvictionInfo 保存 kept_alive_cache_usage，防止 per-user priority 被并发释放
FileCache 的 idle-client eviction 入口依赖 collectIdleClients
```

可以先保持方法结构和 no-op/最小实现；不要实现新的 overcommit policy。

## `OvercommitFileCachePriority`

当前 OSS checkout 中：

```text
FileCache.cpp include OvercommitFileCachePriority.h
gtest_filecache.cpp 在 CLICKHOUSE_CLOUD 下 include 它
但 src/Interpreters/FileCache 目录没有该文件
非 distributed-cache / Cloud 路径下 overcommit policy 会抛 BAD_ARGUMENTS
```

Velox 第一阶段：

```text
不迁移 OvercommitFileCachePriority
保留 FileCachePolicy::LRU_OVERCOMMIT / SLRU_OVERCOMMIT enum 支持解析/校验
使用时报 unsupported / not implemented
```

不要为了接口完整性造假的 overcommit 类。

## 与前置设计的关系

依赖已 review：

```text
FileCacheKey / hash
FileSegmentKeyType / FileCacheOriginInfo
Guards
metrics/debug no-op shims
logging no-op shim
fs:: std::filesystem shim
```

因此 priority 文件迁移时应少改算法，只替换：

```text
String -> ClickHouseAliases.h: using String = std::string
UInt64 / Int64 -> ClickHouseAliases.h aliases to uint64_t / int64_t
UInt8 -> uint8_t, do not copy CH char8_t alias unless a specific overload requires it
Exception -> VELOX_FAIL / compat exception wrapper
absl containers -> folly F14 containers
logger / metrics / failpoint -> no-op shims
```

## 测试要求

第一阶段测试重点：

```text
IFileCachePriority::Entry state transitions
LRU add/remove/increase priority
LRU collectCandidatesForEviction respects eviction cursor
LRU invalidated entries cleanup
SLRU add to probationary
SLRU second access promotes to protected
SLRU addForRestore restores protected/probationary
SplitFileCachePriority routes General/Data to Data and System to System
EvictionInfo add/addOrUpdate/releaseHoldSpace
EvictionCandidates evict failure records failed candidates and resets state in destructor
CacheUsagePerUser snapshot/getOrSet/touch/collectIdleClients minimal behavior
```

## Review 状态

本文档待 review。当前决策：

```text
directly port IFileCachePriority / LRU / SLRU / Split / EvictionCandidates
keep iterator/state-machine semantics
keep LRU std::list for iterator stability
replace absl flat containers with folly F14 containers
keep CacheUsage types but defer overcommit algorithm
do not invent OvercommitFileCachePriority in first phase
```
