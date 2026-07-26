# `FileCacheBufferedInput` Velox 接入设计

## 0. 文档性质与实现基线

本文是 **as-built 设计**，描述已经落地的实现，而不是待实施的方案。

```text
实现仓库：https://github.com/baibaichen/velox.git
实现分支：filecache2
实现参考：fc37a7eb  `FileCache`: integrate buffered input with Velox IO
核心基线：5785a43a（CH `FileCache` port 的核心冻结点）
```

`fc37a7eb` 是压缩后的单一实现 commit，本文不再引用压缩前的中间 commit 区间。

权威依据与验收记录：

```text
port/design/filecache-buffered-input-review-remediation.md   第 11、12 节
port/task/result/020-filecache-core-baseline-io-uplift-result.md
```

本文以实现参考 commit 的实际代码为事实来源；remediation 第 12 节解释设计动机与验收边界，
Task 020 result 记录验证证据。历史设计草图与代码不一致时，不把草图字段或接口视为当前合同。

生产代码范围：

```text
velox/ch/Disks/IO/FileCacheBufferedInput.{h,cpp}
velox/ch/Disks/IO/FileCacheCoalescedLoad.{h,cpp}
velox/ch/Disks/IO/FileCacheInputStream.{h,cpp}   // 同时定义 FileCacheReadContext
velox/ch/Disks/IO/FileCacheRequestContext.h
velox/ch/Disks/IO/FileCacheBufferedInputBuilder.{h,cpp}
velox/ch/IO/FileCacheLocalWriteFile.{h,cpp}
velox/ch/IO/ReadBufferFromVeloxReadFile.{h,cpp}
```

## 1. 目标

让 `FileCacheBufferedInput` 在保留 ClickHouse `FileCache` 语义的前提下，完整接入 Velox `BufferedInput` 体系：

- 对齐 `ScanTracker`、read-planning chunk、coalesced load、异步 prefetch 与 demand load；
- 保留磁盘优先、稳定 key/path、`FileSegment` 状态机、SLRU、`QueryLimit` 与跨重启恢复；
- 对齐 `DirectBufferedInput` 的 whole-file 内存 `preload` 与 `isBuffered` 语义；
- 保持 Parquet / DWRF reader 源码不变，适配其现有 `clone` / `enqueue` / `load` / `isBuffered` 调用流程；
- CH `FileCache` 核心相对 `5785a43a` 保持零 diff（仅默认 writer 换成 typed writer）。

### 1.1 为什么 `FileCache` 不是 `AsyncDataCache`

`FileCache` 是持久磁盘 cache，条目是 `FileSegment`：有下载者仲裁、状态机、部分下载续传、跨进程重启复用。
`AsyncDataCache` 是进程内 RAM/SSD cache，条目是 `CachePin` 引用的 entry，生命周期随进程结束。两者语义不同，所以本实现：

- 不把 `FileCache` 改造成 `AsyncDataCache`；
- 不让同一 read path 同时经过两套 raw-byte cache（builder 里有互斥断言）；
- 不提供 `CachePin` 兼容层，`hasCache` 恒为 false。

### 1.2 不在范围内

- Parquet reader 迁移到 `dwio::common::LoadUnit` / `UnitLoader`；
- 解除 DWRF `StripeMetadataCache` 对 `CacheInputStream` 的假设；
- 把 `FileCacheInputStream` 伪装成 `CacheInputStream`；
- `cacheRegion` / `findCachedRegion` 的 `CachePin` 兼容层；
- 用 `AsyncDataCache` / `SsdCache` 保存 `FileCache` 数据；
- write-through cache 或 `cache_on_write_operations`。

## 2. 与其他 `BufferedInput` 实现的对比

| 实现 | request planning | 数据落点 | stream 消费 | 跨 reader 复用 |
|---|---|---|---|---|
| `BufferedInput` | 排序、合并，同步 `vread` 或连续读 | 当前 input 的内存 buffer | `SeekableArrayInputStream` | 否 |
| `DirectBufferedInput` | `ScanTracker`、概率分类、`coalesceIo`、可异步 | 当前 reader/stream 的临时 buffer | `DirectInputStream` 接管 buffer | 否 |
| `CachedBufferedInput` | 同上，并按 density 切片，区分 RAM/SSD/source | `AsyncDataCache` entry | `CacheInputStream` 持有 `CachePin` | 当前进程内可以 |
| `FileCacheBufferedInput` | `ScanTracker`、`loadQuantum` 切块、cache 状态分类、`coalesceIo` | 持久 `FileSegment` + per-request RAM buffer | `FileCacheInputStream`（CH 状态机 + RAM window） | 持久磁盘复用 |

复用的 policy：`enqueue` 记 `recordReference`；`load` 用 `adjustedReadPct` 区分 prefetch 与 demand；
大请求按 `loadQuantum` 切块；dense/sequential 更积极 coalesce 与异步；sparse 保持 demand；
stream 真正交付字节后 `recordRead`。不复用 `CachePin`、exclusive/shared entry 转换或 `SsdPin`。

## 3. 三个容易混淆的 API

### 3.1 `load`

`load` 表示 request 集合已完整，可以建立优化后的读取计划。它可能启动 IO，但不保证同步完成：

- 普通 `BufferedInput` 在 `load` 内同步完成 IO；
- `DirectBufferedInput` / `CachedBufferedInput` 有 executor 时提交 prefetch，无 executor 时由
  首个 stream 触发；
- `FileCacheBufferedInput` 与后者对齐：prefetch 组在有 executor 时立即提交，demand 组以及
  无 executor 的 prefetch 组保持 `kPlanned`，由绑定 stream 的首次 `Next` 驱动。

只有 `stream::Next` 保证当前请求的字节在返回时可用。

### 3.2 `loadCompleteFile`

`loadCompleteFile` 不调用 `clone`，也不等价于 `preload`。它只是：

```text
enqueue([0, file_size))
load(FILE)
return stream
```

不同 subclass 通过虚函数得到各自的 load 行为，但 `loadCompleteFile` 不把 `preloaded` 置为
true。

### 3.3 `preload` 与 `isBuffered`

`isBuffered` 与 `DirectBufferedInput` 对齐：

```text
isBuffered(offset, length) == preloaded()
```

它只表示 whole-file 数据已驻留在当前 input 持有的内存中，不表示 request 已被规划、coalesced
load 已完成、持久 `FileSegment` 已下载或 OS page cache 中可能存在数据。普通磁盘
`FileSegment` 命中不会让 `isBuffered` 返回 true。

`shouldPreload` 恒返回 false：`preload` 只由显式调用方（DWRF）触发。

## 4. 格式调用方合同

本实现不修改 Parquet / DWRF reader 源码。

### 4.1 Parquet

metadata读取有两个分支：

- 当文件长度不超过 `max(filePreloadThreshold, footerSpeculativeIoSize)` 时，调用
  `loadCompleteFile`，即 `enqueue` 整文件 → `load` → 返回 stream；
- 更大的文件只用 `read` 读取文件尾部的 speculative footer 区间。

两条路径都不会把 input 置为 whole-file `preloaded` 状态。

row group：

```text
clone
  -> enqueue 选中的 column chunk（sid 是栈上临时对象）
  -> 所有列注册完成后调用一次 load
  -> cloned input 保存在 ReaderBase::inputs_
  -> PageReader 之后消费返回的 stream
```

因为 `isBuffered` 只反映内存 preload，持久磁盘命中仍走上述完整流程；cache 命中的 chunk 被
分类为 `kHit`，不进入 source group，由 stream 的状态机从本地 segment 文件读出。

`clone` 共享 immutable context（source `ReadFile`、`FileCachePtr`、`FileCacheKey` / origin、
`FileCacheReadOptions` / `FileCacheRequestContext`、`QueryStatus`、`fileNum` / `groupId` lease、
`ScanTracker`、`IoStatistics` / `IoStats`、executor、`ReaderOptions`），不共享 request 列表、
plan、coalesced load、binding 和 preload buffer；`MetricsLog` 换成 `voidLog`。构造父input时
传入的 `fileReadOps` 没有在 `clone` 中继续传递，因此clone使用空map；这是当前实现事实，不把
它描述成共享context的一部分。

### 4.2 DWRF

`shouldPrefetchStripes` 恒返回 false。`StripeMetadataCache` 的 stripe-metadata prefetch 会把
input stream 硬转换成 `CacheInputStream`，而 `FileCacheInputStream` 不是它的子类，也不打算
伪装成它。该限制只关闭 DWRF 的 stripe-metadata prefetch，不影响列数据读取路径。

DWRF 是 `preload` 的真实调用方（受 `ReaderOptions::filePreloadThreshold` gate）。

## 5. 组件与所有权

```text
FileCacheBufferedInput
  持有：source ReadFile / FileCachePtr / key / origin / cacheOptions /
        requestContext / QueryStatus / fileNum,groupId lease / ScanTracker /
        IoStatistics / IoStats / executor / ReaderOptions
  产出：requests_ -> plan_ -> sourceGroups_ -> coalescedLoads_ + streamToCoalescedLoads_
  缓存：shared_ptr<const FileCacheReadContext> readContext_
  可选：preloadData_（whole-file RAM）

FileCacheReadContext（immutable）
  被 FileCacheBufferedInput、FileCacheInputStream、FileCacheCoalescedLoad 共享

FileCacheCoalescedLoad : cache::CoalescedLoad
  持有：Context{readContext, queryContextHolder} + vector<FileCacheLoadRequest>
  执行：internal FileCacheInputStream；产出 per-request RAM buffer

FileCacheInputStream
  业务角色：bufferedInput_ != nullptr，有 trackingId
  内部角色：bufferedInput_ == nullptr，trackingId 为空
  共用：CH FileSegment 状态机 / downloader 仲裁 / reserve+write / errno 处理
```

### 5.1 `FileCacheBufferedInput`

职责：收集 request、持有 `ScanTracker` 引用、切块与分类、分组、构建 coalesced load 与
stream binding、提供 whole-file preload buffer。它本身不驱动 `FileSegment` 状态机。

`Request` 只保存 region 值、`TrackingId` 值、稳定的 `requestIndex` 和业务 stream 指针。stream
指针**只**作为 `streamToCoalescedLoads_` 的稳定 map key，从不解引用；`StreamIdentifier` 在
`enqueue` 返回后即失效，因此立即按值复制成 `TrackingId`，不保存裸指针。

`readContext_` 在构造函数体内由 `makeReadContext` 构建**一次**，此后 `enqueue` / `read` /
`load` / `makePreloadedStream` 复用同一实例，而不是每次分配等价对象。

### 5.2 `FileCacheReadContext`

不可变的 per-file 生命周期载体，成员为：

```text
cache, ioStatistics, ioStats, source, pool,
key, origin, cacheOptions, requestContext, queryStatus,
tracker, fileNum, groupId, fileSize
```

成员声明顺序是**载荷性**的：`source`（内部持有 `IoStats` / `IoStatistics` 裸指针，由
`BufferedInput` 构造的 `ReadFileInputStream` 带上真实 `FileIoContext`）声明在
`ioStats` / `ioStatistics` **之后**，因此先析构；`cache` 声明最先、析构最后。这保证
`ReadFileInputStream` 析构时其引用的统计对象仍然存活。

因为 context 由 `shared_ptr<const>` 共享且自持 `cache` / `source` / `pool` / 统计对象，内部
stream 与运行中的 load 可以合法地比创建它们的 `FileCacheBufferedInput` 活得更久。

### 5.3 `FileCacheCoalescedLoad`

继承 `cache::CoalescedLoad`，复用其 `kPlanned / kLoading / kCancelled / kLoaded` 状态机、
`loadOrFuture` 与 `cancel`，但：

- `loadData` 返回空 `vector<CachePin>`：不产生任何 `AsyncDataCache` entry；
- `isSsdLoad` 返回 false；
- `size` 返回各 request region 长度之和；
- 交付通道是 `getData(requestIndices)` 返回的 `FileCachePreparedBuffer`（`BufferPtr` + 绝对
  region），而不是 `CachePin`。这是相对 `DirectCoalescedLoad::getData` 的**有意偏离**（见
  remediation 12.4.2 与 task 020）。

`FileCacheLoadRequest` 是coalesced load对象的私有模型：`{requestIndex, region, trackingId, buffers,
ready, consumed}`。`requestIndex` 在同一次 `load` 规划轮次内标识业务请求，不是单个load对象内的
0..N-1计数；不同轮次可重新从0开始。`region` 对应本组中的一个 `PlanChunk`，恒被组bounding range覆盖；
一个业务请求跨多个chunk时会产生多个相同 `requestIndex` 的load request。`PlanChunk` 不出现在公开签名。

`loadData` 流程：

1. `FileCacheQueryIdScope` 固定 `<queryId>:<os-tid>` 调用者身份，使 executor 线程上的
   `reserve` 能找到 per-query download 账户（`Context::queryContextHolder` 负责让账户在
   map 中存活）；
2. 用 `getFileSegmentsForRead` 取组 bounding range `[groupOffset, groupOffset+groupLength)`
   的 holder，钉住组内 `FileSegment` 元数据（含 EMPTY 空洞段），并用 `SCOPE_EXIT` 在成功与
   异常路径上释放——释放发生在所有内部 stream 取到各自精确 holder **之后**；
3. 对每个 request：若其 region 与已材料化的 request 完全相同（offset 与 length 均相等），
   则**不**二次读 source 也**不**读本地 segment，而是从已材料化的 buffer 复制到新分配的
   独立 buffer；否则用 `FileCacheInputStream::createCoalescedInternal` 创建内部 stream，循环
   `Next` + `takeLastOutputBuffer` 直到读完；
4. 在 `requestMutex_` 保护下**一次性**把所有 request 置为 `ready` 并接管 buffer——中途抛异常
   则根本到不了这里，因此不会发布"部分成功"形状的 payload（已写入的 `FileSegment` 字节由
   状态机保留）；
5. 仅当 `prefetch` 为 true 时，把请求 region 的**区间并集**长度计入
   `IoStatistics::prefetch`（重复/重叠 region 只计一次）。

`getData(requestIndices)` 在 `requestMutex_` 下执行 all-or-nothing 检查：任一 index 未找到、
未 `ready` 或已 `consumed`，则返回 `std::nullopt`（调用方回落到普通 demand 路径）；否则移出
所有匹配 request 的 buffer 所有权并置 `consumed = true`。因此同一 request 的 payload 只能被
消费一次。

### 5.4 `FileCacheInputStream` 的双角色

同一个类承担两种角色，共用 CH `FileSegment` 状态机、downloader 仲裁、predownload、
`reserve`+`write`、bypass 与 errno 处理：

| | 业务角色 | 内部角色 |
|---|---|---|
| 构造 | 公开构造函数 | `createCoalescedInternal` |
| `bufferedInput_` | 指向所属 input | `nullptr` |
| `trackingId_` | 来自 `StreamIdentifier`（`read` 路径为空） | 恒为空 |
| per-stream QueryLimit lease | 构造时取得 `queryContextHolder_`，持有到stream析构 | 同样取得并持有 |
| 首次 `Next` | 触发/等待 coalesced load 并安装 RAM window | 不查 binding |
| RAM 交付 | `serveCoalescedWindow` / `servePreloadWindow` | `takeLastOutputBuffer` 移出 owned buffer |
| 生命周期 | 不得超出所属 input | 可以超出（靠 shared context） |
| `ScanTracker` | 交付时 `recordRead`（`trackingId` 为空时不记） | 永不 `recordRead` |

`bufferedInput_` 只用于 binding 查找、`preloaded` 判定、`preloadedData` 取片和
`recordReadBytes`；一切 cache / source / pool / options 访问都走 `context_`。
每个stream构造时还会按 `requestContext.queryId` 和 `cacheOptions` 取得自己的
`queryContextHolder_`；它晚于 `readInfo_` / reader状态释放，确保stream直接reserve时账户存活。
load级holder则保证尚未建立业务stream demand读时，executor上的prefetch账户仍存活。

### 5.5 CH `FileCache` 核心边界

相对 `5785a43a` 的生产 diff 白名单：

```text
FileCache.h                 零 diff
FileCache.cpp               零 diff
FileSegment.h               零 diff
FileCacheErrnoException.h   零 diff
FileSegment.cpp             仅两处：
                              #include "velox/ch/IO/FileCacheLocalWriteFile.h"
                              默认 writer factory：LocalWriteFile -> FileCacheLocalWriteFile
```

基线的 `FileSegment::WriteFileFactory`、`setWriteFileFactoryForTesting`、`createWriteFile`、
`writeFileFactoryStorage` 全部保留；生产代码不调用 `setWriteFileFactoryForTesting`，它仅作为
基线 test seam 存在。所有 IO 集成逻辑留在 `velox/ch/Disks/IO` 与 `velox/ch/IO`。

## 6. 数据流

### 6.1 `enqueue`

```text
1. sid != nullptr -> trackingId = TrackingId(sid->getId())（按值复制）
2. tracker_ 存在 -> recordReference(trackingId, region.length, fileNum, groupId)
   （不受 preloaded 快返回影响）
3. preloaded()  -> 直接返回 makePreloadedStream(offset, length, trackingId)，
                   不入队、不触碰 FileSegment 状态机
4. 否则 requests_.push_back({region, trackingId})，requestIndex = 原下标
5. 创建业务 FileCacheInputStream（this, readContext_, region, STREAM, trackingId）
6. 把 stream.get() 记为该 request 的稳定 map key
```

### 6.2 `load`

```text
A. 切块：对每个 request，按 readerOptions_.loadQuantum() 从 region.offset 切到 region 末尾，
   chunkLen = min(loadQuantum, regionEnd - off)；每个 PlanChunk 记录
   {offset, length, trackingId, state, prefetch, requestIndex}。
B. 状态分类：state = classifyChunk(offset, length)，只用只读的 FileCache::get（见第 7 节）。
C. prefetch 判定：classifyPrefetch(trackingId) —— 空 trackingId 或
   StreamIdentifier::sequentialFile 的 id 恒为 prefetch；否则有 tracker 时按
   adjustedReadPct(trackingData) 与 FLAGS_cache_prefetch_min_pct 比较；无 tracker 时保守判为
   demand。判定按 request 粒度做一次，同一 request 的所有 chunk 共享结果。
D. 分组：只有 kMiss 的 chunk 参与 coalesce；kHit 由本地读服务，kDownloading 走等待路径，都不
   进 source group。prefetch 与 demand 分成两桶，各自按 offset 排序后调用 coalesceIo：
     maxDistance     = readerOptions_.maxCoalesceDistance()
     maxCoalesceBytes= prefetch ? readerOptions_.maxCoalesceBytes() : loadQuantum
     单个 demand chunk 不成组（chunk 数 < 2 且非 prefetch 时直接返回），单个 prefetch chunk 可成组。
   每个 CoalescedGroup 记录 bounding {offset,length}、精确 ranges 列表和 memberChunks
   （coalesceIo 选中的离散 plan_ 下标集合，而非连续区间）。
E. 建 load 与 binding：对每个 group，按 memberChunks -> plan_[idx].requestIndex -> requests_[k]
   为每个member chunk生成 FileCacheLoadRequest（键为稳定 requestIndex，region为该chunk），
   并把 (coalesced load对象, 去重后的 requestIndices) 记入
   streamToCoalescedLoads_[stream]。一个 stream 可能跨多个 group，因此 map value 是
   vector<LoadBinding>。Context 里带上 cache_->getQueryContextHolder(queryId, cacheOptions)。
F. 提交：group.prefetch && executor_ != nullptr -> executor_->add([load]{ load->loadOrFuture(nullptr); })
   立即执行；其余（demand 组、以及 executor 为空时的 prefetch 组）保持 kPlanned。
G. requests_.clear()：planner 不再持有 request 列表。
```

`load` 全程不为 `kHit` / `kDownloading` chunk 发起 IO，`FileSegmentsHolder` 仍由 `Next` 惰性
获取，保持 CH 的按需下载语义。

### 6.3 业务 stream 的首次 `Next`

```text
Next
 ├ 已发布 window 还有剩余  -> 直接补发（并重新记 recordRead，见第 8 节）
 ├ position_ >= region 长度 -> false
 ├ bufferedInput_->preloaded() -> servePreloadWindow（零拷贝 RAM 切片）
 ├ triggerCoalescedLoadIfNeeded（一次性）
 │    bindings = bufferedInput_->coalescedLoads(this)   // move + erase
 │    对每个 binding：
 │      loadOrFuture(&wait) 返回 false -> wait.wait()   // 别的线程在跑，等它
 │                         返回 true  -> load 已终态：本线程可能已在 loadOrFuture 内
 │                                       同步执行 loadData(prefetch=false)，或此前已
 │                                       kLoaded / kCancelled
 │      getData(requestIndices) 有值 -> 收集 buffer
 │    非空则 installCoalescedBuffers（按绝对 offset 排序）
 ├ serveCoalescedWindow：当前绝对位置落在某个 RAM window 内 -> 发布并返回
 └ 否则 queryStatus_.throwIfKilled -> initializeIfNeeded -> FileSegment 状态机（普通 demand）
```

要点：

- 无 executor 时，planned prefetch load 在首次 `Next` 上同步执行，整组一次读完，而不是退化成
  单段 demand 读；
- load 因异常进入 `kCancelled` 时，`loadOrFuture` 立即返回 true，`getData` 返回
  `std::nullopt`，stream 静默回落到普通 demand 路径重新读；
- demand 组在业务线程同步执行；由首次 `Next` 懒执行的 load（含 executor 为空时的 prefetch 组）
  传入的 `wait` 非空，因而 `loadData` 的 `prefetch` 参数为 false，不计 `IoStatistics::prefetch`；
- 由业务线程同步驱动的 load 若抛异常，异常从首次 `Next` 传播给调用方（未被吞掉）。

## 7. `FileSegment` 状态分类（`classifyChunk`）

只用 `FileCache::get`，**绝不**用 `getOrSet`：`get` 不创建 metadata、不选举 downloader、不预留
空间，对持久状态无副作用（内部的 access-time 提升是 `get` 自身固有行为）。

- holder 为空或无段覆盖 -> `kMiss`；
- 一个 chunk 可能跨**多个**更小的 `FileSegment`，因此遍历每个重叠段，只看它与 chunk 的交集
  子区间，并用 `covered` 追踪**几何覆盖**推进；
- 段间空洞（`segStart > covered`）或末段之后的尾部空洞（`covered < chunkEnd`）-> `kMiss`。

逐状态规则：

```text
DOWNLOADED                              整个子区间常驻      -> 推进 covered
DOWNLOADING                             有下载者在产出字节  -> 几何上已覆盖，推进 covered，
                                                              标记 anyDownloading
PARTIALLY_DOWNLOADED
  getCurrentWriteOffset >= 子区间末尾    已写前缀足够        -> 推进 covered
  否则                                   前缀不足、可续传    -> kMiss（可被 load 续填）
PARTIALLY_DOWNLOADED_NO_CONTINUATION
  getCurrentWriteOffset >= 子区间末尾    已写前缀足够        -> 推进 covered
  否则                                   尾部只能 bypass 读  -> 推进 covered 并标记
                                                              anyDownloading（不是可填 miss）
EMPTY / DETACHED                        可填的缺失          -> kMiss
```

聚合：任一可填缺失 -> `kMiss`；否则 `anyDownloading` -> `kDownloading`；否则 `kHit`。混合
chunk 被判为 `kMiss` 是可接受的：load 执行时内部 stream 会跳过已下载段，只填空缺。

## 8. 已发布 window 与 `BackUp` 语义

`Next` 发布的 window 由一组统一的元数据描述：`outputBufferStart_`（region 相对起点）、
`outputBufferSize_`、`offsetInOutputBuffer_`，基址由 `currentWindowBase` 给出。window 有三种：

| window 类型 | 基址 | 所有权 |
|---|---|---|
| 普通读输出 | `outputBuffer_->as<char>()` | 本 stream 拥有（pool-backed） |
| preload 切片 | `preloadWindow_` | 非拥有，属于 input 的 `preloadData_` |
| coalesced 切片 | `preloadWindow_`（指向 window 内偏移） | 非拥有，属于 `coalescedWindows_` 里的 `BufferPtr` |

三者共用同一套元数据与基址抽象，因此 `BackUp`、`SkipInt64` 快路径、pending-window 补发在三种
window 上行为一致。

- **首次交付**：普通读window记裁剪后的 `deliveredBytes`；preload / coalesced window记整窗一次；
  `trackingId` 为空的业务stream（`read` 路径）不记；
- **replay**：`BackUp(count)` 只回退 `position_` 与 `offsetInOutputBuffer_`，不动 `FileCache`
  状态；随后的 `Next` 走 pending-window 补发分支，把再次交付的字节**再记一次**
  `recordRead`，与 `DirectInputStream::Next` 每次交付都记账一致；buffer 内 `seekToPosition`
  落回同一 window 时同理；
- **`SkipInt64`**：窗口内快路径只推进游标，**不**记 `recordRead`；窗口外慢路径调用
  `invalidateAndReposition`（释放 downloader、丢弃 holder/state/window，保留
  `queryContextHolder_`），由下一次真实 `Next` 重新推导段与 reader；
- **`takeLastOutputBuffer`**：只允许移出 owned buffer，`preloadWindow_ != nullptr` 时断言失败；
  移出后清空 window 元数据，且按契约调用方不得再 `BackUp`。内部 stream 用它把读入 buffer
  零拷贝交给 load。

这套语义是 frozen 回归 `UncompressedDwrfReadFullyBacksUpCoalescedWindow` 的直接来源：
`readFully` 会在 RAM window 内 `BackUp`，若 coalesced 切片不发布 window 元数据就会抛
"BackUp beyond output buffer"，或读到上一块 owned buffer 的陈旧字节。

## 9. Whole-file 内存 `preload`

`preload` 是同步的，调用方合同要求它早于任何 `enqueue` / `read`。运行时只强制"调用一次"和
"当前没有待规划request"；`load` 会清空 `requests_`，`read` 从不写入它，因此更早的调用不可检测：

```text
1. VELOX_CHECK(!preloadData_)        重复调用是调用方错误
2. VELOX_CHECK(requests_.empty())    当前没有待规划 request（不是完整调用历史）
3. VELOX_CHECK_LE(fileSize_, readerOptions_.filePreloadThreshold())
   超阈值 fail-fast，不允许用一次无界 whole-file allocation 绕过 preload 准入
4. 读入本地 PreloadData（成功后才提交）：
     fileSize_ <= DirectBufferedInput::kTinySize -> std::string tinyData 一次读完
     否则 -> memoryPool()->allocateNonContiguous，逐 run pread，run 之间按累计长度推进 offset
   全程只有一次 whole-file source 读，没有第二份 staging buffer
5. 源读成功即刻记账（在 fill 之前）：
     IoStatistics::read += fileSize_
     IoStatistics::queryThreadIoLatencyUs / storageReadLatencyUs += 源读耗时
     ProfileEvents::CachedReadBufferReadFromSourceBytes += fileSize_
6. fillFileSegmentsFromPreload(localData)
7. preloadData_ = std::move(localData)  —— 此时 preloaded() 才为 true
```

`fillFileSegmentsFromPreload` 是**尽力而为**的持久化，用的就是刚才驻留在 RAM 的字节：

```text
QueryContextHolder（per-query download 限额）+ FileCacheQueryIdScope（稳定 caller id）
getOrSet(key, 0, fileSize_, fileSize_, Regular, segmentsBatchSize, origin, alignment)
逐段：
  state 非 EMPTY 且非 PARTIALLY_DOWNLOADED     -> 跳过
  getOrSetDownloader() != getCallerId()         -> 别人在写，跳过（不持有 lease）
  SCOPE_EXIT { resetRemoteFileReader(); completePartAndResetDownloader(); }
  循环 writeOffset -> segEnd：
    residentAt(writeOffset) 给出常驻块指针与连续可用长度（tinyData 或某个 allocation run）
    chunkSize = min(段剩余, 连续可用, ReadBufferFromVeloxReadFile::kDefaultBufferSize)
    reserveAndWriteSegmentChunk(seg, src, chunkSize, writeOffset,
                                reserveSpaceWaitLockTimeoutMs, reserveHint=段剩余, skip)
    返回 false -> break（放弃这一段，RAM preload 不受影响）
```

失败语义（以代码为准，不做过度承诺）：

- **可 bypass 的失败被吞掉**：`reserve` 失败，或 `skipCacheOnDiskFailure=true` 下的物理写失败，
  以及 `ENOSPC` / `EDQUOT`（无论 skip 设置）——`reserveAndWriteSegmentChunk` 返回 false，
  循环 `break`，RAM preload 依旧提交，`preloaded` 为 true；
- **只有严格失败才抛**：`skipCacheOnDiskFailure=false` 下的非空间类物理写失败，或任何非 errno
  的逻辑异常（如 `getOrSet` 失败）会向上传播，从 `preload` 抛出，`preloadData_` 因此**不**提交，
  `preloaded` 保持 false；已选举的段在栈展开时由 `SCOPE_EXIT` 释放 lease；
- `fileSize == 0` 或 `getOrSet` 返回空 holder 时直接结束fill，不视为错误。

内存与磁盘是两个独立预算，不是同一 counter 的双计：preload buffer 只计 `MemoryPool`；
`FileSegment` fill 只计 `FileCache` 容量与一次 `QueryLimit` download/write；input 析构释放
`MemoryPool` 字节，持久 segment 继续存在。

`preloadedData(offset, length)` 返回**零拷贝、非拥有**的连续切片，长度受 preload 存储的 run
边界限制（可能短于 `length`），调用方循环推进。`servePreloadWindow` 直接发布该切片，永不回落
到 `FileSegment` 磁盘读。`addressInPreloadData` 是测试可观测点，用于证明确实是零拷贝而不是
per-stream 拷贝。

## 10. 持久化写入与错误行为

### 10.1 段查找的模式分派

`getFileSegmentsForRead` 是 IO 层唯一的模式策略实现，`FileCacheInputStream::nextFileSegmentsBatch`
与 `FileCacheCoalescedLoad::loadData` 共用它：

```text
tempCacheOnly               -> getDownloadedContiguousOrEmpty；空批次是硬错误
                               （对齐 CH throwTemporaryDataNotInCache）
readIfExistsOtherwiseBypass -> get（只读探测，不建 metadata）
其他                        -> getOrSet（按 boundaryAlignment 对齐，建 metadata）
```

### 10.2 下载者仲裁与写入

`FileSegment` 的 downloader election 是唯一仲裁机制，IO 层不引入第二套 key/range 锁，也没有
`DownloaderLease` 之类的新核心抽象——选举与释放一律用现有 `getOrSetDownloader` /
`completePartAndResetDownloader` 加 `SCOPE_EXIT`。同一段被多方规划时，一个 owner 下载，其他
读者走 CACHED reader 读已写前缀并在前缀耗尽时重新 prepare（`cachedPrefixEndAbsolute`）。

写入统一走：

```text
reserveAndWriteSegmentChunk
  -> FileSegment::reserve(size, timeout, reason, nullptr, reserveHint)
     失败直接返回 false（bypass）
  -> VELOX_CHECK_EQ(getCurrentWriteOffset(), offset)
  -> writeSegmentChunk -> FileSegment::write
       成功 -> ProfileEvents::CachedReadBufferCacheWriteBytes += size
```

`reserveHint` 是"到本次读 horizon 末尾还剩多少字节"，避免预留量超过实际会消费的量：demand 路径
用 `readInfo_.readUntilPosition - offset`，preload fill 用段内剩余。

### 10.3 typed errno 与 skip 策略

`FileCacheLocalWriteFile`（`velox/ch/IO`）是 typed errno 的 producer：`open` / `lseek` / `write` /
`fsync` / `close` 失败先保存 `errno` 再抛 `FileCacheErrnoException`；正向 short write 继续续写；
`EINTR` 重试；`close` 无论成败都清 fd，析构不重试；对已关闭 writer 调用 `append` / `flush` 是
调用序错误，抛普通 `VeloxRuntimeError` 而非 errno 异常，因此逻辑 bug 不会被误判成可 bypass 的
磁盘故障。它是基线默认 factory 唯一被批准的行为替换。

consumer 侧 `classifyCacheWriteError(errno, skipOnDiskFailure)`：

```text
ENOSPC / EDQUOT          -> Bypass（无视 skip 设置；对齐 CH writeCache 记录空间不足后 bypass）
其他 errno, skip=true    -> Bypass
其他 errno, skip=false   -> Rethrow（对齐 CH CACHE_CANNOT_WRITE_TO_CACHE_DISK）
非 FileCacheErrnoException -> 不捕获，自然传播
```

物理写失败时 `FileSegment::write` 已经把段调整为 `PARTIALLY_DOWNLOADED_NO_CONTINUATION` 并完成
downloaded/physical size 对账；bypass 后 demand 路径把 `readType` 改成
`REMOTE_FS_READ_BYPASS_CACHE`，数据照常返回给调用方。

### 10.4 失败与取消的可见性

- prefetch load 内部读失败：异常使 `loadOrFuture` 把状态置为 `kCancelled` 并在 executor 线程
  终止该任务；不发布任何部分 payload；业务 stream 首次 `Next` 拿到 `nullopt` 后走普通 demand
  路径重新读，数据正确，不残留 downloader / waiter；
- demand load 在业务线程同步执行，异常直接传播给调用方；
- 单 stream 读写异常路径：`Next` 的 catch 中先按 CH 顺序释放 downloader 再重抛，不把已取消的
  reader 交还给 `FileSegment`。

## 11. 生命周期与取消

```text
~FileCacheBufferedInput  -> 对 coalescedLoads_ 逐个 cancel
reset                    -> BufferedInput::reset + 逐个 cancel + 清 coalescedLoads_ /
                            streamToCoalescedLoads_ / requests_ / plan_ / sourceGroups_
```

`cancel` 调用 `setEndState(kCancelled)`，会无条件改状态并唤醒waiter；destructor与 `reset` 对全部
coalesced load对象调用它。已运行的 `loadData` 不会被强制终止：它靠共享context与executor捕获的
`shared_ptr<load>` 自然完成，并可能随后置为 `kLoaded`。持久 `FileSegment` 不因取消而删除。

`QueryStatus::throwIfKilled` 有四个安全点：

1. `initializeIfNeeded` 内部、首次 `getOrSet` / `get` 之前；
2. `nextFileSegmentsBatch` 的cache查找之前；
3. `Next` 外层迭代边界、开始或推进segment之前；
4. `FileSegment::wait` 返回之后、重新解释段状态之前。

这些位置都不跨越downloader持有期；第四处保证等待其他downloader期间发生的取消在唤醒后立即
可见。

生产代码**不**包含任何测试专用hook：`FileSegment::wait` 的 `TestValue` 挂钩已回退，
`FileCacheInputStream` 也未补替代hook。并发时序只用测试侧的 `folly::Baton`、测试
`ReadFile` 与专用executor建立；没有测试注册 `CoalescedLoad::loadOrFuture` 的 `TestValue`，
也不使用sleep或概率性时序断言。无法证明的更细内部瞬间改为验证可观察并发合同。

## 12. `ScanTracker` 与统计口径

tracking identity 与原生实现完全一致：

```text
file lease  = FileHandle::uuid            file id  = uuid.id()
group lease = FileHandle::groupId         group id = groupId.id()
tracking id = TrackingId(StreamIdentifier::getId())
```

`StringIdLease` 是可复制 lease，input 与其 clone 各持副本，保证进程内 `uint64_t` id 在生命周期
内有效映射。它只服务 `ScanTracker`，不参与持久 `FileCacheKey`。

- `recordReference`：有 `tracker_` 时在 `enqueue` 里调用，**包括** `preloaded` 快返回之前；
- `recordRead`：只在业务交付时调用——demand 路径记**裁剪后**的 `deliveredBytes`，coalesced /
  preload window 记整窗一次，pending-window 补发（`BackUp` / 窗口内 seek 后的 replay）再记一次；
- 内部 stream 的 `trackingId` 为空、`bufferedInput_` 为空，因此后台下载**永不**记 `recordRead`，
  不会把"下载了"记成"消费了"；`read` 创建的无 sid 业务 stream 同样不记。

物理 IO 归因（按实际物理字节，而非裁剪后的交付字节）：

```text
本地 cache 命中（ReadType::CACHED）
  ProfileEvents::CachedReadBufferReadFromCacheBytes += actualBytes
  IoStatistics::ssdRead += actualBytes
  IoStatistics::incRawBytesRead(actualBytes)   // 本地 reader 不自动记账

source 读（任一 remote ReadType）
  ProfileEvents::CachedReadBufferReadFromSourceBytes += actualBytes
  IoStatistics::read += actualBytes
  IoStatistics::queryThreadIoLatencyUs / storageReadLatencyUs += 本次物理读耗时
  raw bytes 与 totalScanTimeNs 由 ReadFileInputStream::read 记，不重复计
  whole-file preload 例外：它直接读入 PreloadData，按第 9 节步骤 5 单独记账

cache 写
  ProfileEvents::CachedReadBufferCacheWriteBytes += 实际写入段的字节

prefetch
  IoStatistics::prefetch += 该 load 请求 region 的区间并集长度
  （仅当 loadData 的 prefetch 参数为 true，即由 executor 提交的后台执行）
```

`ssdRead` 因此成为"RAM 交付 vs 本地磁盘读"的判别器：coalesced RAM 命中不增加 `ssdRead`，本地
段读会增加。

## 13. `FileCacheBufferedInput` API 行为

| API | 实际行为 |
|---|---|
| `enqueue` | 记 `recordReference`、复制 tracking id、建 request 与业务 stream；不做 IO；`preloaded` 时直接返回 RAM stream |
| `load` | 切块、分类、分组、建 `FileCacheCoalescedLoad` 与binding；prefetch组在有executor时立即提交，其余保持 `kPlanned`；清空 `requests_` |
| `read` | 未规划读仍走 `FileCache` 状态机（空 tracking id）；`preloaded` 时走 RAM |
| `clone` | 复制 immutable context，产生干净 planner；不共享 request / plan / load / binding / preload buffer |
| `reset` | 对全部coalesced load对象调用 `cancel`，再清空load、binding与planner状态；不删除持久 `FileSegment` |
| `preload` | 同步 whole-file 内存 preload（受 `filePreloadThreshold` gate），并尽力回填 `FileSegment` |
| `preloaded` | 仅表示 whole-file 内存 preload 已提交 |
| `isBuffered` | 等于 `preloaded`，与 `DirectBufferedInput` 对齐 |
| `shouldPreload` | 恒 false |
| `shouldPrefetchStripes` | 恒 false（阻止 DWRF 把 stream 硬转成 `CacheInputStream`） |
| `executor` | 返回注入的executor，不返回 `FileCacheManager` 拥有的线程池 |
| `hasCache` | false；不支持 `CachePin` region API |
| `cacheRegion` / `findCachedRegion` | **不 override**，沿用 `BufferedInput` 基类的 `VELOX_UNSUPPORTED` |

关于 region API：保持基类 fail-fast 合同而不是 fail-soft，因为 `cacheRegion` 返回 `void`，静默
no-op 会让调用方误以为字节已进入 backing cache；`findCachedRegion` 返回 `nullopt` 会把"该实现
没有 `CachePin` API"伪装成一次普通 cache miss。调用方必须先检查 `hasCache`。

## 14. Builder 映射

`FileCacheBufferedInputBuilder::create` 不需要修改 builder 虚接口，映射为：

```text
source ReadFile   <- FileHandle::file
file lease        <- FileHandle::uuid           file id  <- uuid.id()
group lease       <- FileHandle::groupId        group id <- groupId.id()
ScanTracker       <- Connector::getTracker(ConnectorQueryCtx::scanId,
                                           ReaderOptions::loadQuantum)
FileCacheKey      <- FileCacheKey::fromPath(file->getName())
origin            <- FileCachePtr::getCommonOrigin
queryId           <- ConnectorQueryCtx::queryId
userId            <- FileCacheManager::commonUserId
cancellation      <- QueryStatus{ConnectorQueryCtx::cancellationToken}
cacheable         <- FileCacheRequestContext 默认值 true（builder 当前未读取 ReaderOptions::cacheable）
```

持久 `FileCacheKey` 来自 path，不使用进程内 `FileHandle::uuid`。`create` 开头断言
`connectorQueryCtx->cache() == nullptr`，即 `AsyncDataCache` 与 `FileCache` 互斥；
`registerFileCacheBufferedInputBuilder` 在安装时用 `hasDefault` 做 fail-fast 校验。

## 15. 验证

承载性证据（不是全部用例，而是关键判别器）：

**三个冻结的 parity 回归**（测试体、fixture、输入、对照实现和断言均不得修改）：

```text
FileCacheFormatE2ETest.UncompressedDwrfReadFullyBacksUpCoalescedWindow
  动态生成 > 1 MiB 的未压缩 DWRF，强制业务读经由 coalesced RAM window；
  BackUp 不得抛异常，行数完整。
FileCacheBufferedInputTest.PreloadedStreamTracksReferenceAndReadLikeDirect
  与 DirectBufferedInput 同输入对照：referencedBytes = 64，readBytes = 64。
FileCacheBufferedInputTest.BackedUpBytesAreRecordedAgainLikeDirect
  Next(64) -> BackUp(16) -> Next(16)：referencedBytes = 64，readBytes = 80。
```

**T1–T5 证据用例**（均在 `velox_ch_filecache_connector_test`）：

```text
T1 PrefetchServesBusinessFromRamNotLocalDisk
   业务 Next 期间 source 读计数与 IoStatistics::ssdRead 增量均为 0。
   ssdRead 是唯一能区分 RAM 交付与本地磁盘命中的判别器（禁用 serveCoalescedWindow 时为
   131072 vs 0）。
T2 PrefetchFailureFallsBackToDemandPath
   三个相邻 64 KiB 请求同组：第一次源读成功并落盘请求 A，第二次源读在材料化请求 B 时失败。
   断言 load = kCancelled、getData({A}) = nullopt、A 落盘 64 KiB、B 落盘 0、
   A 走 64 KiB 本地 ssdRead、B 重走 source；C 只用于取得共享 binding，失败后未材料化。
   使用测试本地 GatedFailReadFile，无生产 hook。
T3 GetDataIsConsumedOnce
   第二次 getData 返回 nullopt，且不产生额外 source / 本地读。
T4 NullExecutorPlannedLoadTriggersOnFirstNext
   null executor 下 load 不做 IO；仅调用一次 Next 后断言 128 KiB 整组已下载完成
   （若退化成单段 64 KiB demand 读则失败）。
T5a ClassifyPartiallyDownloadedInsufficientPrefixIsMiss
T5b ClassifyPartiallyDownloadedNoContinuationInsufficientPrefixIsDownloading
   通过真实 FileSegment API（setDownloadFinishedWithoutContinuation）构造状态并先断言状态。
```

**格式 E2E**：`FileCacheFormatE2ETest.ColdScanFillsCacheWarmScanServesFromCache`（DWRF 两 stripe，
warm 扫描 source 读为 0）与 `ParquetColdScanFillsCacheWarmScanServesFromCache`（真实
`ParquetReader`，断言连接器确实选中了 `FileCacheBufferedInput`，覆盖 row-group clone / enqueue /
load / `PageReader` 路径）。

**生命周期与取消**：`FileCacheCancellationTest` 的 `ResetCancelsPlannedCoalescedLoad`、
`PlainDestructorCancelsPlannedCoalescedLoad`、`RunningCoalescedLoadCompletesAfterDestruction`
（用 Baton 把 load 停在首次源读中，销毁 input 后释放，断言两段都完成下载）。

**QueryLimit**：`velox_ch_filecache_connector_test` 的 `PrefetchWarmHonoursPerQueryDownloadLimit`
把限额设为小于单段的4096字节，断言downloaded bytes不超限；用单线程executor的 `join` 取代sleep。

**最终 gate（全绿，0 失败 0 跳过）**：

```text
velox_ch_filecache_connector_test:       57
velox_ch_filecache_buffered_input_test:  38
velox_ch_filecache_e2e_test:             21
velox_ch_cancellation_test:               9
velox_ch_filecache_hit_metrics_test:      7
velox_ch_filecache_core_scc_test:        49
velox_ch_filecache_manager_test:         20
velox_ch_filecache_format_e2e_test:       3
velox_ch_io_test:                        33
total:                                  237
```

核心边界 gate：`FileCache.{h,cpp}` / `FileSegment.h` / `FileCacheErrnoException.h` 相对
`5785a43a` 零 diff，`FileSegment.cpp` 仅两处获批改动，且不残留 `submitWarm`、
`inflightWarmForTest`、warm 相关成员、`FileSegment::wait` hook、`DownloaderLease`，生产不调用
`setWriteFileFactoryForTesting`。

## 16. 后续工作（不属于当前实现）

以下两项在当前实现中**不存在**，也不构成承诺；它们需要独立设计与证据，不得据本文推断接口：

1. **本地 `FileSegment` 读建议（`posix_fadvise(POSIX_FADV_WILLNEED)`）**：对已缓存的本地段文件
   下发读建议。需要先确定 `ReadFile` 层是否引入通用 read-advice 能力、句柄获取与移交方式、
   建议失败的处理，以及如何度量收益（advice 字节不是实际读字节）。当前实现没有任何
   read-advice API、local-advice 分组或相关统计。
2. **动态 next-window prefetch**：由 demand 消费进度触发下一窗口的预取。需定义触发条件、内存
   预算及与历史的关系。当前prefetch只在一次 `load` 内分类分组，之后不动态扩展窗口。
