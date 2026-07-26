# `FileCacheBufferedInput` Velox 接入设计

## 1. 目标

让 `FileCacheBufferedInput` 在保留 ClickHouse `FileCache` 语义的前提下，更完整地接入
Velox `BufferedInput` 体系：

- 对齐 `ScanTracker`、read-planning chunk、coalesced load、异步 prefetch 和 demand load。
- 保留磁盘优先、稳定 key/path、`FileSegment` 状态机、SLRU、`QueryLimit` 和跨重启恢复。
- 支持两种独立 prefetch：
  - 从 source 下载并填充持久 `FileSegment`。
  - 对已缓存的本地 segment 文件调用 `posix_fadvise(POSIX_FADV_WILLNEED)`。
- 对齐 `DirectBufferedInput` 的 whole-file 内存 `preload` 和 `isBuffered` 语义。
- 保持当前 Parquet reader 源码不变，适配其现有 `clone` / `enqueue` / `load` /
  `isBuffered` 调用流程。

本设计不把 `FileCache` 改造成 `AsyncDataCache`，也不让同一 read path 同时经过两套
raw-byte cache。

## 2. 已批准的范围

### 2.1 当前阶段包含

- `FileCacheBufferedInput` request planning。
- `FileCacheLoadGroup` 的共享加载状态、future、取消和 stream handoff。
- `ScanTracker` 接入。
- source read coalescing。
- demand-triggered next-window prefetch。
- 本地 cache-segment `fadvise`。
- whole-file 同步内存 `preload`。
- `ReadFile` 的通用 read-advice API 和 `LocalReadFile` 实现。
- 现有 Parquet reader 零改动下的兼容。

### 2.2 当前阶段不包含

- Parquet reader 迁移到 `dwio::common::LoadUnit` / `UnitLoader`。
- DWRF `StripeMetadataCache` 对 `CacheInputStream` 假设的解除。
- 把 `FileCacheInputStream` 伪装成 `CacheInputStream`。
- `cacheRegion` / `findCachedRegion` 的 `CachePin` 兼容层。
- 用 `AsyncDataCache` / `SsdCache` 保存 `FileCache` 数据。
- write-through cache 或 `cache_on_write_operations`。

DWRF 在本阶段继续让 `FileCacheBufferedInput::shouldPrefetchStripes` 返回 false。解除
`CacheInputStream` 假设后，再单独开启 DWRF stripe prefetch。这个限制只关闭DWRF的
stripe-metadata prefetch；本设计对共享 `FileCacheBufferedInput` 的 `preload` 和
`isBuffered` 修改仍会影响DWRF/Text等其他format调用方，必须按第20节验证。

`cacheRegion` / `findCachedRegion` 继续遵循 `BufferedInput` 的现有合同：当
`hasCache=false` 时，直接调用是caller contract violation并抛
`VELOX_UNSUPPORTED`。不改为fail-soft：

- `cacheRegion` 返回 `void`，静默no-op会让caller误以为raw bytes已经进入backing
  cache；
- `findCachedRegion` 返回 `nullopt` 会把“该实现没有 `CachePin` API”伪装成一次普通
  cache miss；
- 当前Velox生产源码没有这两个API的调用点，只有 `CachedBufferedInput` 实现和测试；
- 未来caller必须先检查 `hasCache`，不能依赖subclass私自弱化基类合同。

本阶段增加合同测试：`hasCache=false`，直接调用 `cacheRegion` /
`findCachedRegion` 必须抛；所有新增真实调用点必须先由 `hasCache` gate保护。

## 3. 现有实现差异

| 实现 | request planning | 数据落点 | stream 消费 | 跨 reader 复用 |
|---|---|---|---|---|
| `BufferedInput` | 排序、合并，同步 `vread` 或连续读 | 当前 input 的内存 buffer | `SeekableArrayInputStream` | 否 |
| `DirectBufferedInput` | `ScanTracker`、概率分类、`coalesceIo`、可异步 | 当前 reader/stream 的临时 buffer | `DirectInputStream` 接管 buffer | 否 |
| `CachedBufferedInput` | 同上，并按 density 切片，区分 RAM/SSD/source | `AsyncDataCache` entry | `CacheInputStream` 持有 `CachePin` | 当前进程内可以 |
| 当前 `FileCacheBufferedInput` | `load` 是 no-op | demand read 时写 `FileSegment` | `FileCacheInputStream` 状态机 | 持久磁盘复用 |

`DirectBufferedInput` 和 `CachedBufferedInput` 都使用 access pattern：

1. `enqueue` 通过 `ScanTracker::recordReference` 记录 stream 引用。
2. `load` 使用 `adjustedReadPct` 区分 prefetch 和 demand。
3. 大请求按 `loadQuantum` 切成 read-planning chunks。
4. dense/sequential 请求允许更积极地 coalesce 和异步加载。
5. sparse 请求避免大范围 over-read。
6. stream 真正消费字节后，`recordRead` 更新历史。

`FileCacheBufferedInput` 应复用这套 policy，不复用 `CachePin`、exclusive/shared entry
转换或 `SsdPin`。

## 4. 三个容易混淆的 API

### 4.1 `load`

`load` 表示 request 集合已经完整，可以建立优化后的读取计划。它可能启动 IO，但不保证
同步完成：

- 普通 `BufferedInput` 在 `load` 内同步完成 IO。
- `DirectBufferedInput` / `CachedBufferedInput` 在有 executor 时提交 prefetch group；
  无 executor 时由首个 stream 触发。
- 新 `FileCacheBufferedInput` 与 `DirectBufferedInput` / `CachedBufferedInput` 对齐。

`stream::Next` 才保证当前请求的字节在返回时可用。

### 4.2 `loadCompleteFile`

`loadCompleteFile` 不调用 `clone`，也不等价于 `preload`。它只是：

```text
enqueue([0, file_size))
load(FILE)
return stream
```

不同 subclass 通过虚函数得到各自的 load 行为，但 `loadCompleteFile` 不把
`preloaded` 设为 true。

### 4.3 `preload` 与 `isBuffered`

`isBuffered` 必须与 `DirectBufferedInput` 对齐：

```text
isBuffered(offset, length) == preloaded()
```

它只表示 whole-file 数据已驻留在当前 input 持有的内存中，不表示：

- request 已被 planner 规划；
- coalesced load 已完成；
- `AsyncDataCache` 中存在普通 region entry；
- 持久 `FileSegment` 已下载；
- OS page cache 中可能存在数据。

普通磁盘 `FileSegment` 命中不能让 `isBuffered` 返回 true。

`FileCacheBufferedInput::preload` 按 `DirectBufferedInput` / `CachedBufferedInput` 的同步
合同实现：

1. 只能调用一次。
2. 必须发生在普通 `enqueue` 之前。
3. 只接受 `fileSize <= ReaderOptions::filePreloadThreshold`；超出时fail fast，不能
   通过一次无界whole-file allocation绕过preload admission。默认threshold为8 MiB，
   由Hive `file-preload-threshold`配置。这是相对 `DirectBufferedInput::preload` 的
   有意加强；正常DWRF caller已用同一threshold gate，本地直接调用也必须遵守。
4. 整文件allocation计入caller的MemoryPool；allocation失败直接传播，不降级为隐藏的
   untracked buffer。
5. 返回时整文件已驻留 MemoryPool-backed buffer。
6. `preloaded` 和 `isBuffered` 返回 true。
7. 如果 source 数据尚未进入 `FileCache`，同一批字节也写入 `FileSegment`，避免第二次
   source read。

MemoryPool bytes和FileCache disk bytes属于两个不同资源预算，不是同一个counter的双计：

- preload buffer只计MemoryPool；
- `FileSegment` fill只计FileCache容量和一次 `QueryLimit` download/write；
- segment写入直接读取whole-file preload buffer的slice，不再分配第二份whole-file
  staging buffer；
- preload input析构后释放MemoryPool bytes，持久segment继续存在；
- cache fill失败按第17.4节处理：skip开启时保留内存preload；严格模式清理未提交的
  preload状态并抛出。

当前 Parquet 小文件路径调用 `loadCompleteFile`，不是 `preload`，因此不会自动进入
whole-file 内存 preload 状态。DWRF 显式调用 `preload` 时可获得该能力。

## 5. 当前 Parquet reader 合同

本阶段不修改 Parquet reader。

### 5.1 Footer / small file

```text
loadCompleteFile
  -> enqueue whole file
  -> load
  -> returned stream
```

### 5.2 Row group

Parquet 对未 buffered 的 row group 使用：

```text
clone
  -> enqueue selected column chunks
  -> load once after all columns are registered
  -> keep cloned input in ReaderBase::inputs_
  -> PageReader later consumes the returned streams
```

`FileCacheBufferedInput::isBuffered` 与 `DirectBufferedInput` 对齐后，普通持久磁盘命中
返回 false。因此 Parquet 会继续走现有 `clone` / `enqueue` / `load` 流程：

- cache miss 生成 download-prefetch plan；
- cache hit 生成 local-advice plan；
- 不需要修改 Parquet 源码；
- 不需要让 `isBuffered` 产生副作用。

`clone` 共享 immutable context：

- source `ReadFile`；
- `FileCachePtr`；
- `FileCacheKey` / origin；
- `FileCacheReadOptions` / request context；
- file id / group id / `ScanTracker`；
- metrics、executor 和 reader options。

`clone` 不共享 request、load group、临时 buffer 或取消状态。

## 6. Request 状态与 stream 生命周期

每次 `enqueue`：

1. 把 `StreamIdentifier` 立即复制成 tracking id，不保存 caller 的裸指针。
2. 调用 `ScanTracker::recordReference`。
3. 创建 `shared_ptr<FileCacheRequestState>` 并交给返回的
   `FileCacheInputStream`。
4. input request 列表只保存该 state 的 `weak_ptr`、region 和 tracking id。

`load` 锁定仍然存活的 weak state。若 enqueue 返回的 stream 已在 `load` 前销毁，
planner 跳过该 request，不产生 UAF 或无效 prefetch。

每个存活 request 在 `load` 后关联一个 `FileCacheLoadGroup`。stream 首次读取时：

- group 已加载：取得 demand handoff buffer 或读取持久 segment；
- group 正在加载：等待同一个 future；
- group 仍是 demand-planned：当前 stream 成为触发者；
- group 失败：按第 11 节传播；
- group 被取消：检查 query cancellation 或重新按 demand 路径建立状态。

## 7. `ScanTracker` 与 read-planning chunks

`readerOptions.loadQuantum` 是 Velox 的访问预测和临时 buffer 单位。默认值为 8 MiB。
它不是 FileCache 的持久 segment size。

对每个 region：

1. 按 `loadQuantum` 切成 read-planning chunks。
2. 使用 `adjustedReadPct` 和 read density 分类。
3. empty tracking id和 `StreamIdentifier::sequentialFile` 按原生 policy 进入
   prefetch 候选；不新增独立的 metadata tracking category。
4. sparse 大 column 的 chunks 保持 demand，避免 over-read。
5. stream 实际交付字节时调用 `ScanTracker::recordRead`。

后台下载不能调用 `recordRead`，否则会把“下载了”错误地记成“消费了”。

tracking identity与原生 `DirectBufferedInput` / `CachedBufferedInput` 完全一致：

```text
file lease   = FileHandle::uuid
file id      = FileHandle::uuid.id()       // uint64_t
group lease  = FileHandle::groupId
group id     = FileHandle::groupId.id()    // uint64_t
tracking id  = TrackingId(StreamIdentifier::getId())
```

`StringIdLease` 是可复制lease；`FileCacheBufferedInput` 和clone持有副本，保证传给
`recordReference` / `recordRead` 的process-local `uint64_t` id在input生命周期内仍有
有效mapping。它只服务 `ScanTracker`，不参与持久 `FileCacheKey`。

## 8. FileCache 状态映射

每个 read-planning chunk 查询 `FileCache`，再按实际 segment 状态拆成：

```text
已下载部分
  -> local read-ahead range

缺失、EMPTY 或可续传部分
  -> cache-fill range

由其他 downloader 写入的部分
  -> join/wait range

未被消费的 demand request
  -> planned only
```

例如：

```text
Parquet region: [0, 20 MiB)
loadQuantum:    8 MiB

planning chunks:
  [0, 8), [8, 16), [16, 20)

FileCache state:
  [0, 8)   downloaded -> local advice
  [8, 16)  miss       -> source read + FileSegment write
  [16, 20) downloaded -> local advice
```

cache hit 和 miss 在同一次 planning 中识别，但进入不同的执行 group。

## 9. `loadQuantum` 与 FileSegment size

默认值：

```text
ReaderOptions::loadQuantum           = 8 MiB
FileCache maxFileSegmentSize         = 32 MiB
FileCache boundaryAlignment          = 4 MiB
```

这些值不要求相等：

- `loadQuantum` 控制访问概率、临时内存和 stream next-window。
- `maxFileSegmentSize` 是持久 segment 的大小上限。
- `boundaryAlignment` 控制持久 range 对齐。

对新的 miss，planner 按 read-planning chunk 调用 `getOrSet`。实际 segment range仍由
alignment、已有 metadata和 `maxFileSegmentSize` 决定，不保证 chunk与segment一一
对应。相邻 chunk可能返回同一个已存在/新建 segment；一个 8 MiB chunk经4 MiB对齐后
也可能扩张。

对已有的大 segment：

- 不重新切分或改名；
- 写入必须从 `currentWriteOffset` 连续推进；
- 若选中的 chunk 位于未下载 prefix 之后，physical cache-fill range 必须包含该 prefix；
- alignment/prefix 扩张计入 physical over-read。

访问 policy 始终使用原始 `loadQuantum`。coalescing 和内存预算使用扩张后的 physical
bytes，避免配置差异绕过内存上限。

## 10. Coalesced groups

### 10.1 Source groups

只包含 cache-fill ranges：

1. 按 source offset 排序。
2. 复用 `coalesceIo`、`moveCoalesced`、`maxCoalesceDistance` 和
   `maxCoalesceBytes`。
3. planning阶段只形成候选 group，不固定最终 source offset或buffer layout。
4. group执行时，先逐segment重新读取状态并进行 downloader election：
   - election成功：读取执行时的 `currentWriteOffset`；
   - 已被其他downloader拥有：从source group移除并转为join/wait；
   - 已在并发期间完成：转为local-advice/hit；
   - 无法reserve：按现有bypass/throw合同处理。
5. 对election成功的segment先按执行时range完成 `reserve`，再根据实际
   `currentWriteOffset` 生成最终 positioned-read vectors。
6. 对最终保留的ranges执行一次 positioned batch read / `preadv`。
7. 直接从group buffer调用 `FileSegment::write`，随后 `complete` /
   `completePartAndResetDownloader`；该路径不使用
   `getRemoteFileReader` / `setRemoteFileReader` handoff。
8. source IO或写入异常时，对所有已election/reserve的segment执行现有异常清理和
   downloader释放协议，不能留下reserved bytes或lease。

planner 不复用 `CachePin`-specific `CoalescedLoad` 数据结构，但应对齐其状态机和 future
语义。

downloader election、执行时cursor和reserve必须发生在source IO之前。否则另一query
可能先取得downloader，导致本group读完source后无法写入，或partial segment的
`currentWriteOffset` 已变化，使计划时buffer layout失效。

### 10.2 Local-advice groups

只包含 downloaded segment ranges：

- 只能合并同一个 cache-segment 文件内的重复或相邻区间；
- offset 转换为 segment-file-relative 坐标；
- 不跨不同 segment 文件合并；
- 不分配数据 buffer；
- 不占 `QueryLimit` 下载额度。

## 11. Static prefetch 与 demand prefetch

### 11.1 Static prefetch

`load` 完成 tracking 分类后：

- 有 executor：提交高概率 source/local groups。
- 无 executor：保留为 `Planned`，由首个 stream 触发。

`load` 不等待异步 group 完成。

### 11.2 Demand group

未被预测为高概率的 group 不在 `load` 时产生 IO。第一次 `Next` 触发当前 chunk。

与 `DirectBufferedInput` 对齐：

- 多个相关demand requests若通过 `coalesceIo` 规则，可以在 `load` 时形成一个
  demand coalesced group，由其中第一个stream触发；
- 单个或无法coalesce的demand request只读取当前chunk；
- demand group的physical bytes同样受 `maxCoalesceBytes` 约束；
- dynamic next-window prefetch是新的单窗口任务，不无限扩张当前demand group。

stream 消费达到当前 `loadQuantum` 的阈值后，对下一窗口动态 prefetch：

- 下一窗口已缓存：local `fadvise`；
- 下一窗口缺失：调度 cache-fill；
- stream 不继续消费：不再向后预取。

这对齐 `CacheInputStream` 的 `prefetchPct` 思路，避免仅 enqueue 但从未消费的 stream
产生无效 IO。

阈值来源必须明确，不在实现时临时选择：

- 新增 `FileCacheReadOptions::nextWindowPrefetchPct`；
- 默认值为200，与 `CacheInputStream::prefetchPct_` 一致；100或更大表示关闭dynamic
  next-window prefetch，保持默认行为不变；
- 取值0到99时，在当前quantum已交付bytes达到
  `loadQuantum * nextWindowPrefetchPct / 100` 后调度下一quantum；
- 首次启用和性能实验使用50，但生产默认是否从200改为50必须有benchmark和host配置
  决定；
- static `load` prefetch仍由 `ScanTracker` policy控制，不受该阈值影响。

## 12. Demand buffer handoff

cache miss 的当前 query 不能在下载后立即从本地文件重读同一批数据。

错误实现：

```text
source -> temporary buffer -> FileSegment -> discard
current stream -> local FileSegment read -> decoder
```

正确实现：

```text
demand-triggered source read
  -> group-owned temporary buffer
       -> write FileSegment
       -> hand off matching slice to current stream
  -> stream consumes and releases temporary buffer
```

后台提前完成的 prefetch group：

```text
source -> temporary buffer -> FileSegment -> release temporary buffer
```

因此：

- demand cold path只有一次 source read，不增加本地重读；
- future reader/query 从持久 `FileSegment` 读取；
- 临时 buffer 不构成进程级 memory cache；
- 临时内存受 `loadQuantum`、physical bytes和 `maxCoalesceBytes` 约束；
- duplicate request各自获得安全的 slice/copy，不能重复 move 同一 buffer。

## 13. 本地磁盘 read advice

### 13.1 通用 `ReadFile` API

Velox `ReadFile` 增加通用 read-advice API，支持：

```text
WILL_NEED
```

结果必须显式区分：

- applied；
- unsupported；
- failed，并携带系统错误。

默认 `ReadFile` 返回 unsupported。`LocalReadFile` 使用其私有 fd 调用
`posix_fadvise(..., POSIX_FADV_WILLNEED)`。不支持该系统调用的平台保留默认
unsupported，不引入无效fallback。

`FileCache` 不强转 `LocalReadFile`，不读取私有 fd，也不为 advice 重复打开文件。

这是本设计唯一有意修改Velox主干抽象的部分，文件范围限于：

```text
velox/common/file/File.h
velox/common/file/LocalFile.h
velox/common/file/LocalFile.cpp
对应的 common/file tests
```

它属于通用、可上游化的 `ReadFile` 能力，不包含 `FileCache` 类型。实现提交和后续
upstream sync必须显式登记该主干偏离。

拒绝只在 `velox/ch` 中增加path-based advice helper：`LocalReadFile` 是 `final`，
fd是private且没有native-handle API；helper只能为同一路径再open一个fd。这会绕开
`OpenedFileCache`、增加open/rename race和额外系统调用，不满足本设计的句柄handoff
合同。

### 13.2 句柄获取和 handoff

对immutable `DOWNLOADED` segment，本地 prefetch 的实际顺序：

```text
load group
  -> OpenedFileCache::get(path)
  -> ReadFile advice
  -> group 保持 shared_ptr<ReadFile>
  -> demand stream 接管或复用同一 handle
  -> pread
```

`posix_fadvise` 必须在 open 后调用，但整个 `open + advice` 发生在 demand read 之前。

当前 `FileCacheInputStream` 的 cache-file open 也应统一经过 Manager-owned
`OpenedFileCache`，与 planner 共用句柄和 remove invalidation。

仍在 `DOWNLOADING` 的segment不进入local-advice group，也不改用
`OpenedFileCache`。它继续保留当前 `getPath` 重采样和rename-race retry路径：
`<offset>` 可能在open期间被rename为 `<offset>_<size>`，不能把旧路径handle提前缓存。
只有观察到terminal `DOWNLOADED` / immutable path后，planner和demand reader才复用
`OpenedFileCache` handle。

### 13.3 Advice 失败

read advice 是性能提示：

- unsupported 和 failed 都记录独立 metric/log；
- 不让 query 失败；
- 不把 advice bytes 记成实际 read bytes；
- demand read仍通过正常 `pread` 保证正确性。

## 14. Whole-file 内存 preload

为对齐 `DirectBufferedInput`：

- 抽取或复用其 MemoryPool-backed `PreloadData` 机制；
- 小文件可使用 inline/tiny storage，大文件使用 MemoryPool allocation；
- 同步读完整文件；
- 若 FileCache 尚未命中，同一批数据写入 `FileSegment`；
- 返回后 `preloaded=true`；
- `enqueue` / `read` 直接返回内存 region view；
- `clone` 不继承 active preload buffer，除非原生 `DirectBufferedInput` 的最终合同明确
  要求共享；本设计默认 clone 是 clean input。

磁盘 segment 或 demand handoff buffer都不能单独把 `preloaded` 设为 true。

## 15. Builder 映射

`FileCacheBufferedInputBuilder` 已拥有所需上游对象，不需要修改 builder 虚接口。

必须传入：

```text
file lease                  <- FileHandle::uuid
file id for tracking        <- FileHandle::uuid.id()
file group                 <- FileHandle::groupId
group id for tracking       <- FileHandle::groupId.id()
scan tracker               <- Connector::getTracker(
                                  ConnectorQueryCtx::scanId,
                                  ReaderOptions::loadQuantum)
query id                   <- ConnectorQueryCtx::queryId
cancellation token         <- ConnectorQueryCtx::cancellationToken
cacheable                  <- ReaderOptions::cacheable
```

持久 `FileCacheKey` 仍来自 path/etag，不得改用进程内 `FileHandle::uuid`。

FileCache 与 `AsyncDataCache` 的 mutual-exclusion guard保持不变。

## 16. `FileCacheBufferedInput` API 行为

| API | 目标行为 |
|---|---|
| `enqueue` | 记录 tracking reference，创建 stream/request state，不做 IO |
| `load` | 建立 groups；有 executor 时提交高概率 prefetch |
| `read` | unplanned read仍走 FileCache 状态机 |
| `clone` | 复制 immutable context，创建 clean planner |
| `reset` | 取消未开始 group，释放 planner state，不删除持久 segment |
| `preload` | 同步 whole-file 内存 preload，并可同时填 FileCache |
| `preloaded` | 仅表示 whole-file 内存 preload完成 |
| `isBuffered` | 与 `DirectBufferedInput` 对齐，等于 `preloaded` |
| `executor` | 返回 caller-injected executor |
| `hasCache` | false；不支持 `CachePin` region API |
| `cacheRegion` | 继承基类fail-fast合同；`hasCache=false`时直接调用抛异常 |
| `findCachedRegion` | 继承基类fail-fast合同；`hasCache=false`时直接调用抛异常 |
| `shouldPrefetchStripes` | 本阶段 false |

## 17. 并发、取消和错误传播

### 17.1 Load group 状态

```text
Planned -> Loading -> Loaded
                   -> Failed
                   -> Cancelled
```

同一 group 的 stream共享完成状态和 waiters。

### 17.2 Downloader 仲裁

`FileSegment` downloader election是唯一下载仲裁。planner 不创建第二套 key/range 锁。
不同 query/driver规划到同一 segment 时：

- 一个 owner下载；
- 其他 group join/wait；
- owner结束、取消或异常时，必须通过现有 API 释放 lease。

### 17.3 Cancellation

builder 传入真实 cancellation token。安全检查点：

- downloader 获取前；
- wait 内；
- reserve 前；
- group 之间；
- source IO 前后。

不能在持有未释放 downloader lease 的位置直接抛异常。

### 17.4 错误

- source read失败：group 保存异常；消费该 group 的 stream观察到异常。
- reserve/cache write失败：遵循现有 `skipCacheOnDiskFailure` 合同。
- 同步 `preload` 的 source read失败：直接抛出。
- 同步 `preload` 的 cache write失败：
  - skip关闭：抛出；
  - skip开启：保留有效内存 preload，记录 cache fill失败。
- advice失败：只影响性能，按第 13.3 节处理。

## 18. Metrics

现有实际 IO 统计保持：

- source bytes；
- local cache read bytes；
- cache write bytes；
- raw bytes；
- storage/local latency。

新增或补充：

- download-prefetch planned/completed/cancelled/wasted bytes；
- local-advice planned/applied/unsupported/failed bytes；
- coalesced group数量、gap over-read、alignment/prefix over-read；
- group wait latency；
- demand handoff bytes；
- downloader join次数。

规则：

- prefetch 下载不能调用 `ScanTracker::recordRead`。
- demand handoff不能再记一次 local read。
- advice bytes不是实际 read bytes。
- 同一 source read不能在 planner和 stream重复记账。

## 19. 实现阶段

### 阶段 A：合同对齐

- builder 传入 file/group/tracker/cancellation/cacheable。
- `isBuffered` 与 `DirectBufferedInput` 对齐。
- `clone` / `reset` / request state生命周期。
- whole-file 同步内存 `preload`。

### 阶段 B：Planning policy

- `ScanTracker`。
- read-planning chunks。
- prefetch/demand分类。
- `coalesceIo` / `moveCoalesced`。
- `FileCacheLoadGroup` 状态机。

### 阶段 C：Download prefetch

- segment状态映射。
- downloader join/election。
- source `preadv`。
- `reserve` / `write` / `complete`。
- demand buffer handoff。
- dynamic next-window download。

### 阶段 D：Local read advice

- `ReadFile` advice API。
- `LocalReadFile` `posix_fadvise`。
- `OpenedFileCache` 统一 immutable `DOWNLOADED` cache-file open。
- static和 dynamic next-window local advice。

### 阶段 E：格式和性能验证

- Parquet current-interface E2E。
- DWRF保持关闭并运行回归。
- Direct / Cached / FileCache 三路径 benchmark。

## 20. 验证计划

### 20.1 Contract tests

- `preload` 同步完成、只能一次、必须早于普通 `enqueue`。
- `preload` 超过 `filePreloadThreshold` fail fast。
- preload allocation完整计入MemoryPool，cache fill不分配第二份whole-file buffer。
- preload fill只计一次FileCache容量和 `QueryLimit` write。
- `preloaded` / `isBuffered` 与 `DirectBufferedInput` oracle一致。
- 持久 segment命中但无 whole-file内存 preload时，`isBuffered=false`。
- `clone` 不携带 active request/group/buffer。
- `reset` 不删除持久 segment。
- `hasCache=false`时直接调用 `cacheRegion` / `findCachedRegion` 抛异常；真实caller由
  `hasCache` gate保护。

### 20.2 Planning tests

- `recordReference` / `recordRead` 时机。
- dense / sparse / empty-id / sequential分类。
- `loadQuantum` 切片。
- duplicate和overlap去重。
- `maxCoalesceDistance` / `maxCoalesceBytes`。
- alignment/prefix over-read。

### 20.3 Download tests

- demand miss只产生一次 source read。
- demand buffer直接交 stream，不发生本地重读。
- background prefetch完成后，后续 stream不回源。
- 一个 group跨多个 segment。
- 一个已有大 segment覆盖多个 planning chunks。
- 并发同 segment只有一个 downloader。
- partial download续传。

### 20.4 Local-advice tests

- warm segment在 static prefetch group调用 advice。
- 未消费 demand group不 open、不 advice。
- dynamic threshold默认200时不预取；配置50时消费一半quantum后只预取下一窗口。
- offset转换为 segment-file-relative。
- 不跨 segment文件合并。
- unsupported/failed不会让 query失败，且 metric可见。
- rename/remove后不复用失效 handle。

### 20.5 Lifecycle tests

- enqueue返回的 stream在 `load` 前销毁。
- input在 async group完成前 reset/shutdown。
- cancellation不泄漏 downloader、future、buffer或handle。
- source read、reserve、write、advice各失败点。

### 20.6 Format tests

- 不修改 Parquet reader源码。
- 实现开始前重新核对当前Velox版本仍满足：
  `ReaderBase::inputs_`持有clone，`StructColumnReader::loadRowGroup`执行
  `clone/enqueue/load`，`PageReader`随后接管enqueue返回的stream；若源码漂移则停止并
  修订本设计。
- Parquet footer `loadCompleteFile`。
- Parquet row-group `clone` / `enqueue` / `load`。
- cold miss、warm disk hit、whole-file内存 preload。
- projection/filter导致的 sparse column请求。
- DWRF现有路径保持 `shouldPrefetchStripes=false` 并通过回归。
- DWRF small-file显式 `preload` 验证内存驻留、FileSegment fill和内存上限。
- DWRF stripe在磁盘FileCache hit但未memory preload时，`isBuffered=false` 后仍正确走
  clone/load路径。
- Text `loadCompleteFile` 在新planner下保持内容和生命周期正确。

### 20.7 Benchmark

比较：

```text
DirectBufferedInput
CachedBufferedInput
FileCacheBufferedInput
```

场景：

- cold sequential；
- cold sparse projection；
- warm FileCache + cold OS page cache；
- warm FileCache + warm OS page cache；
- demand first-read；
- background prefetch；
- concurrent readers共享同一 segment。

重点指标：

- source read次数和bytes；
- local read次数和bytes；
- coalesced group数；
- over-read；
- first-byte latency；
- scan throughput；
- peak temporary memory；
- prefetch waste ratio。
