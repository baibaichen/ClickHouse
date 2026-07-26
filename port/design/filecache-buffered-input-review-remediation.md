# FileCacheBufferedInput 接入 —— Review 修正批次(A/B5a 返工 + B5b 正式接入)

## 0. 背景与范围

外部 agent review 了 velox `filecache2` 分支上已提交的九个 commit(A1-A4 合同对齐、
B1-B4 planning、B5a 异步 warm),提出 10 项问题。本文档把这 10 项落成一个带**依赖顺序**
的分批实施方案,供审阅后再动手。

所有引用的 `file:line` 均已核实(velox 源码 `filecache2` 当前状态)。规矩不变:
worker + RED + Controller 独立复现 + 亲验;每 commit 先落 `filecache2` 再 cherry-pick
到 `filecache2-gluten` 后切回;无 `-j`;push 仅用户明确要求;Allman braces。

Review 明确**无需改动**的两点(保留现状):downloader election + `SCOPE_EXIT` lease
释放逻辑正确;删除旧的 disk-hit `isBuffered=true` 断言是合理的。

---

## 1. 问题清单与核实过的根因

| # | 问题 | 核实过的根因(file:line) |
|---|------|--------------------------|
| 1 | Request 保存失效 `StreamIdentifier*`(UAF) | `enqueue` 存 `requests_.push_back(Request{region, sid})`(`FileCacheBufferedInput.cpp:122`);`load` 里 `request.sid->getId()`(`:141-143`)在 enqueue 返回、栈上临时 sid 销毁后解引用 → UAF。Direct/Cached 都**不**存 sid,enqueue 时立即 `TrackingId(sid->getId())` 取值(`DirectBufferedInput.cpp:48-50`;`CachedBufferedInput.cpp:47-50`)。 |
| 2 | discarded-stream 合同不明确 | 旧 FileCache 测试要求 stream 销毁后不得 warm;但 Velox 原生 enqueue 已登记请求,stream 销毁不撤销 load/prefetch。需对齐原生:discarded stream 仍参与 load/prefetch,删 weak request-state 与"不创建 segment"断言,但不得解引用已销毁 stream(#1 解决后自然满足)。 |
| 3 | warm task 可能活得比 Manager 资源久 | warm 跑在 **connector-owned** executor(`HiveConnector::ioExecutor_` → builder → `FileCacheBufferedInput::executor_`,`.cpp:397` 提交)。`FileCache::deactivateBackgroundOperations`(`FileCache.cpp:1918-1937`)只 join 自己的 download/eviction/metadata 线程,**对 warm task 零感知**。FileCache 无 task 计数、无 cv join、无 `CancellationSource`(仅 `std::atomic<bool> shutdown`,`FileCache.h:411`)。 |
| 4 | 真实查询取消没接入 | stream 收到 `QueryStatus{}`(`FileCacheBufferedInput.cpp:124`)=永不取消;warm task 无取消检查。token 已到达 builder 边界:`ConnectorQueryCtx::cancellationToken()`(`Connector.h:558`)= `Task::getCancellationToken()`(`Task.h:817`)。`ch::QueryStatus` 已接受 `folly::CancellationToken`,`throwIfKilled` 检查 `isCancellationRequested`。 |
| 5 | warm 可能绕过 QueryLimit | `FileCacheQueryIdScope` 只设 thread-local id、**不持账户**(`FileCacheQueryIdScope.cpp:34-43`)。`tryGetQueryContext` 按 id 查 `query_map`,查不到返回 `nullptr`,`tryReserve` 静默跳过 per-query 检查=无限额(`FileCache.cpp:1063-1082`,`QueryLimit.cpp:38-46`)。demand 路径靠 stream ctor 持 `getQueryContextHolder(...)` 整段生命周期(`FileCacheInputStream.cpp:90-91`)。 |
| 6 | region API 被改成 fail-soft | `cacheRegion` no-op / `findCachedRegion=nullopt`(`FileCacheBufferedInput.h:113-118`)把"不支持"伪装成写成功/普通 miss。应恢复基类 fail-fast(`VELOX_UNSUPPORTED`);`hasCache()=false`,caller 必须先查 `hasCache`。 |
| 7 | whole-file preload 不完整且放大内存 | 无 `filePreloadThreshold` 大小上限;不填充 FileSegment;`makePreloadedStream` per-stream `make_unique<char[]>`+memcpy(`FileCacheBufferedInput.cpp:670-694`),复制内存不计入 MemoryPool。Direct 用 run-based 零拷贝切片 `preloadedData(offset,length)`(`DirectBufferedInput.cpp:339-358`)。 |
| 8 | coalesced group 只保存 bounding range | `CoalescedGroup{begin,end,offset,length}` 只存包围盒(`FileCacheBufferedInput.cpp:361-366`);两个相距近的小请求合并后会下载并持久化整个中间区间,可能超 `maxCoalesceBytes` 且缓存未请求数据。Direct 保留子请求边界(`ends`),gap 只当 discard 读、超限 `kNoCoalesce` 断组(`DirectBufferedInput.cpp:171-175, 392-399`)。 |
| 9 | warm 内存/IO 不可观测 | scratch 用普通 `std::vector<char>`(`FileCacheBufferedInput.cpp:441`)绕过 MemoryPool;warm 远端字节不进 source/raw/prefetch 统计 → 后续可能误显示 100% cache hit。 |
| 10 | 异步测试竞态 + 合同冲突 | `load` 提交 task 后立即断言 cache 空,取决于线程调度;discarded-stream 测试与 #2 采用的原生合同冲突。需 null/controlled executor 做 pure-planning、promise/barrier/hook 做并发(不 `sleep_for` 轮询),并补真实 Parquet/取消/shutdown/QueryLimit/gap/preload 测试。 |

---

## 2. 依赖关系与分批顺序

```
批 1(独立、无依赖,先清):  #1 UAF  →  #2 discarded 合同  →  #6 region fail-fast
批 2(地基):               #3 Cache 拥有/取消/等待 warm task   ← #4 #5 #10 的前提
批 3(接入取消/限额):      #4 取消 wiring   #5 warm 持 QueryContextHolder
批 4(A/B 返工):          #7 preload 重做   #8 coalesce vector<Range>   #9 warm 可观测
批 5(测试收尾):          #10 异步测试竞态 + 补全测试(贯穿:每批各带 RED,批 5 补齐缺口)
```

理由:
- #1 是真 bug,且 #2 依赖 #1(不存 sid 后,discarded stream 自然不被解引用),合并处理。
- #6 是纯删 override,与 #1/#2 同区域(`FileCacheBufferedInput`),顺手一批。
- **#3 是 #4/#5/#10 的地基**:取消要有 shutdown-cancel 源,QueryLimit 的 warm holder 生命
  周期要在 Cache 掌控的 task 生命周期内,shutdown 测试要有真正的 join 点。
- #7/#8/#9 是对 A4/B4/B5a 的返工,彼此独立,但都依赖 #3(warm 归属/取消/生命周期已就位)。

---

## 3. 各批设计

### 批 1

**#1 + #2 —— Request 存 TrackingId 值,删 sid 指针,对齐 discarded 语义**
- `struct Request` 去掉 `const StreamIdentifier* sid`,改存 `velox::cache::TrackingId trackingId`。
- `enqueue`:已有的 `trackingId` 计算(`:111-115`)保留,`requests_.push_back(Request{region, trackingId})`。
- `load`:删 `:140-144` 的 `request.sid->getId()`,直接用 `request.trackingId`。
- discarded stream:enqueue 已登记 Request(值语义,不含指针),stream 销毁不影响
  load/prefetch —— 与 Direct 一致。删除任何 weak request-state 设计、"stream 销毁后不
  得 warm""不创建 segment"的旧断言。
- RED:构造 enqueue 传栈上临时 sid → 返回后销毁 → load。当前实现 UAF(ASan 抓);改后绿。
  另加 discarded-stream-still-warms 断言(原生语义)。

**#6 —— 恢复 region API fail-fast**
- 删 `FileCacheBufferedInput.h:113-118` 三个 override,让基类 `VELOX_UNSUPPORTED` 生效。
- 确认 `hasCache()=false` 已在(`:107`);caller 契约:先查 `hasCache` 再碰 region API。
- RED:直接调 `cacheRegion`/`findCachedRegion` 断言抛 `VELOX_UNSUPPORTED`(当前 no-op/nullopt 不抛)。

### 批 2 —— #3 Cache 拥有并取消/等待 warm task(**地基,需你拍板层级选型**)

层级(review 已批准):**Manager 等 Cache;Cache 管理/取消/等待自己的 task;Task 不知道 Manager。**
问题:warm 现跑在 connector-owned executor,Cache 对它零感知。两种落法:

- **选型 A(推荐):FileCache 内新增 warm-task 追踪层**
  - FileCache 增 `folly::CancellationSource warmCancelSource_` + 在途计数
    (`std::atomic<int64_t> inflightWarm_` + `std::mutex` + `std::condition_variable`),
    或用 `folly::coro`/`collectAll` future 汇总(倾向 counter+cv,零协程依赖)。
  - 新增 `FileCache::submitWarm(folly::Executor*, Func)`:登记(++计数)→ 包装 task
    (task 内先查 `warmCancelSource_.getToken()` 与 query token)→ 提交到传入 executor →
    task 结束 --计数 + notify。
  - `warmSourceGroup` 不再直接 `executor_->add`,改走 `cache_->submitWarm(executor_, ...)`。
  - `deactivateBackgroundOperations`(`FileCache.cpp:1918`)开头加:`warmCancelSource_.requestCancellation()`
    → 等 `inflightWarm_==0`(cv wait)→ 再走现有 join。Manager `factory_.clear()`
    已逐 cache 调 deactivate(`FileCacheFactory.cpp:270,292`),故 Manager 释放
    scheduler/workerPool 前,所有 warm 已 cancel+join。
  - 代价:改 FileCache 核心类(加成员 + 一个提交入口 + deactivate 尾部 join)。executor
    仍是 connector-owned(warm 执行载体不变,只是 Cache 掌握 cancel+join 句柄)。**符合
    DirectBufferedInput 的 connector-executor 模型 + review 层级。**

- **选型 B:warm 改提交到 Manager-owned worker_pool / DownloadQueue**
  - warm 不走 connector executor,改用 FileCache 已引用的 `worker_pool`(`FileCache.h:346`)
    或 `Metadata` 的 `download_queue`(`Metadata.h:298`)—— 后者 `metadata.shutdown()`
    已 join。天然被现有 shutdown 覆盖,几乎不改 deactivate。
  - 代价:改变 warm 的执行载体,偏离 Direct 的 connector-executor 模型;background
    download 线程数受 `backgroundDownloadThreads` 限,与 IO executor 并发模型不同;可能
    与 CH 原生 background download 语义纠缠。

  **倾向 A**(隔离清晰、贴合 Direct 模型、层级正确)。请你拍板 A / B。

- RED(两选型通用):提交 warm → 触发 `deactivateBackgroundOperations`(或析构 Cache)→
  断言 deactivate 返回后无在途 warm(计数==0 / 无 ASan use-after-free / 无悬空)。当前实现
  deactivate 不等 warm,构造"warm 慢 + Cache 先析构"用 test hook 让 warm 阻塞在可控点,
  验证 deactivate 会等它。

### 批 3

**#4 —— 取消 wiring**
- builder(`FileCacheBufferedInputBuilder.cpp:create`)已有 `connectorQueryCtx`(#29 IoStatistics
  同路进来);构造 `ch::QueryStatus{connectorQueryCtx->cancellationToken()}` 传入
  `FileCacheBufferedInput`(新增 ctor 参数或成员)。**核实**:确认自定义 builder 的 create
  形参确实拿到了 `connectorQueryCtx`(下一步实现前先 grep 该文件确认)。
- `FileCacheBufferedInput` 持 `QueryStatus queryStatus_`;`enqueue`/`read` 建 stream 时传
  `queryStatus_`(替换 `:124` 的 `QueryStatus{}`)。
- warm task:捕获 query token + Cache 的 `warmCancelSource_` token;检查点(review 指定):
  task 开始、segment 切换、downloader election、source IO 前后、reserve 前后 —— 各处
  `if (queryStatus.isCancelled() || warmToken.isCancellationRequested()) return;`
  (`SCOPE_EXIT` 已保证 lease 释放)。
- RED:cancel token → stream Next 抛 `FileCache query cancelled`;warm 在检查点提前退出且不泄漏 lease。

**#5 —— warm 持 QueryContextHolder**
- warm task 提交前(在 `warmSourceGroup`,提交线程内)调
  `cache_->getQueryContextHolder(requestContext_.queryId, cacheOptions_)`,把
  `QueryContextHolderPtr` **移入** task lambda 捕获,task 结束才析构 → 账户存活整个 warm。
- task 内仍建 `FileCacheQueryIdScope`(稳定 caller id),但账户由 holder 保活(id scope 不持账户)。
- RED:设 `maxDownloadSizePerQuery` 很小 + demand stream 已销毁(账户将被回收)→ warm 大 group。
  当前(仅 id scope)warm 走无限额、reserve 超限也不拒;改后 warm 受 limit 约束(reserve
  在超限时按 `canFit` 拒绝)。

### 批 4

**#7 —— preload 重做**
- 加大小上限:新增 `FileCacheReadOptions::filePreloadThreshold`(或复用现有阈值),
  `preload` 超阈值直接不 preload(或让 `shouldPreload` 据此返回)。语义与 Direct 对齐
  (Direct 靠 caller 驱动,我们显式加阈值,review 要求)。
- run-based 零拷贝:仿 `DirectBufferedInput::preloadedData(offset,length)`(`:339-358`),
  `makePreloadedStream` 改成返回指向 run 的零拷贝切片流,**不 per-stream memcpy 整段**。
  region 跨 run 时,用 run 边界切分的多段流(Direct 的 `findRun` 模型)。
- 同批填 FileSegment:preload 读到的 source bytes 同时 `getOrSet`+write 进 FileSegment
  (一次 source IO 双写:RAM + cache),受 QueryLimit / disk failure / 异常正确处理。
  (注:此项与 A4b 原计划重叠 —— 确认是否纳入本批,还是保留"preload 仅 RAM、不填 segment"
  的更小范围,待你定。)
- RED:大文件超阈值 → 不 preload(`preloaded()==false`);preload 后两个 stream 读同区间
  → 断言内存分配不随 stream 数线性增长(pool 统计);preload 后 classifyChunk 命中(若填 segment)。

**#8 —— coalesce 保留 vector<Range>,只写选中 range**
- `CoalescedGroup` 增 `std::vector<std::pair<uint64_t,uint64_t>> ranges`(或 chunk index
  列表,已有 begin/end 可映射回 plan_ chunk 的精确 [offset,length])。
- warm 执行:source IO 可合并系统调用读 [groupOffset, groupEnd),但**只把选中 range 的
  bytes 写进对应 FileSegment**(加上 FileSegment 顺序写必需的 prefix)。gap 字节读了不写、
  不缓存。超 `maxCoalesceBytes` 由现有 `kNoCoalesce` 断组保证(已在 `:337-341`)。
- RED:两个相距 gap 的小 miss chunk 合并 → 断言只有选中 range 落盘为 segment,gap 区间
  不产生 segment / 不计入 cache size。当前 bounding-range 实现会把整段写进去 → 红。

**#9 —— warm 可观测**
- scratch 改用 MemoryPool-backed buffer(`pool->allocate` / `BufferPtr`),不用裸 vector。
- warm 记录:source bytes、raw bytes、prefetch bytes、latency 到对应统计
  (`ioStatistics_` 的 prefetch 桶 + ProfileEvents);**warm 记 prefetch/source 桶,不调
  demand 专用 `recordReadBytes`**。后续 local hit 读同段时只记 local/cache-hit,不重复
  计 source bytes(避免 100% hit 假象反转成 source 重复计)。
- RED:warm 一个 group → 断言 prefetch/source 统计增加;随后 demand 读同段 → 断言 local
  hit 增加而 source **不**再增加(无重复计)。

### 批 5 —— #10 测试竞态 + 补全

- pure-planning 测试:传 null executor(不 warm)或 controlled/inline executor,断言
  plan_/sourceGroups_ 结构,**不**断言异步 cache 状态。
- 并发测试:promise/barrier/test hook 同步 warm 与 demand,**禁用 `sleep_for` 轮询**
  (CLAUDE.md 铁律:C++ 不用 sleep 修竞态)。
- 补测:真实 Parquet 端到端、查询取消(stream + warm)、Cache shutdown 等待 warm、
  QueryLimit 约束 warm、gap coalescing 只写选中 range、preload 阈值 + 零拷贝 + 填 segment。

---

## 4. 待你拍板的开放项

1. **#3 选型 A vs B**(warm 追踪落点)。倾向 A(FileCache 内加 CancellationSource+counter,
   executor 仍 connector-owned)。
2. **#7 范围**:preload 是否本批就"同批填 FileSegment"(等于并入原 A4b),还是先只做
   "阈值 + run-based 零拷贝 RAM preload",填 segment 单独一批?
3. **批次是否可合并 commit**:批 1 三项(#1+#2+#6)是否合成 1 个 commit,还是 #1+#2 一个、
   #6 一个?我倾向 #1+#2 合一(同一改动链)、#6 独立(纯删 override,便于回溯)。

审阅后告诉我:选型 A/B、#7 范围、commit 粒度,我就按批开工(worker+RED+亲验,逐 commit
落 filecache2 → cherry-pick → 切回)。

---

## 5. Review 后追加的解决方案

本节保留前文原始问题、备选方案和开放项不变,记录后续 review 已确认的解决方案。

### 5.1 `StreamIdentifier` / discarded stream

- `Request` 的tracking实现严格参考 `DirectBufferedInput` /
  `CachedBufferedInput`:在 `enqueue` 时把 `sid->getId()` 复制成
  `TrackingId`,后续 `load` 只读取 `request.trackingId`,不保存或解引用
  `StreamIdentifier *`。
- discarded stream采用Velox原生语义:`enqueue` 已登记的request仍可参与
  `load`/prefetch;stream销毁不撤销request。
- 修改旧的“discarded stream后cache不得创建segment”测试。新测试必须验证:
  - 栈上临时 `StreamIdentifier` 销毁后 `load` 无UAF。
  - discarded stream仍可按原生合同触发prefetch。

### 5.2 warm task 生命周期

已确认采用前文选型A:

```text
执行位置:connector-owned executor
生命周期:FileCache负责
依赖层级:Manager等待FileCache,FileCache等待warm task,task不知道Manager
```

`FileCache` 增加warm-task tracking:

- `folly::CancellationSource warmCancelSource_`。
- `bool acceptingWarmTasks_`。
- `int64_t inflightWarm_`。
- 保护状态的mutex和等待归零的condition variable。
- `FileCache::submitWarm(folly::Executor *, Func)` 作为唯一提交入口。

提交与shutdown必须使用同一把mutex形成原子合同:

1. submit在锁内检查 `acceptingWarmTasks_`。
2. 仍接受任务时在同一临界区执行 `++inflightWarm_`。
3. 包装task持RAII completion guard;正常返回、取消和异常都执行
   `--inflightWarm_` + notify。
4. `executor->add` 自身抛异常时,提交线程回滚计数并notify。
5. shutdown在锁内先设置 `acceptingWarmTasks_=false`,再请求取消并等待
   `inflightWarm_==0`。
6. 所有warm退出后,Cache才继续metadata/download/eviction shutdown。

`FileCacheFactory::remove` / `clear` 已在释放Manager-owned资源前逐cache调用
`deactivateBackgroundOperations`,因此Manager只需等待Cache,不直接知道单个task。

### 5.3 query cancellation / reset cancellation

- builder把 `ConnectorQueryCtx::cancellationToken()` 包装成 `QueryStatus`,
  传入 `FileCacheBufferedInput`。
- input把同一 `QueryStatus` 复制给 `enqueue` / `read` 创建的stream和warm task。
- Cache shutdown cancellation来自 `FileCache::warmCancelSource_`。
- `FileCacheBufferedInput` 另持input-local cancellation source/generation:
  - input普通析构不取消已登记warm,保持discarded-stream原生语义。
  - 显式 `reset` 取消当前generation已提交的warm,再创建新generation供input复用。
- warm task同时检查query、Cache和input-generation三个token。
- downloader election成功后必须先安装lease `SCOPE_EXIT`,之后才能执行任何可能因取消
  返回或抛出的检查。

检查点包括:task开始、segment切换、downloader election前后、source IO前后、
reserve前后。

### 5.4 warm task 的 `QueryLimit`

`FileCacheQueryIdScope` 只提供thread-local query id,不能代替
`QueryContextHolder`。warm task提交前必须取得自己的holder并持有到task结束。

不依赖lambda capture成员的隐式析构顺序,使用显式context:

```cpp
struct WarmTaskContext
{
    FileCachePtr cache;
    FileCache::QueryContextHolderPtr queryContextHolder;
};
```

成员按声明逆序析构,因此先释放holder,再释放cache。task内仍创建
`FileCacheQueryIdScope`,让reserve按id找到由holder保活的同一query账户。

### 5.5 region API

- 删除 `FileCacheBufferedInput` 的fail-soft `cacheRegion` /
  `findCachedRegion` overrides。
- 保留 `hasCache() == false`。
- 直接调用region API继承基类 `VELOX_UNSUPPORTED` fail-fast合同。
- caller必须先检查 `hasCache`。

### 5.6 whole-file preload

- 使用现有 `readerOptions_.filePreloadThreshold()`,不新增重复setting。
- 文件超过threshold时 `preload` fail fast;不能静默跳过,也不能执行无界whole-file
  allocation。
- 参考 `DirectBufferedInput::preloadedData`,使用MemoryPool-backed `PreloadData` 和
  run-based stream,不为每个stream执行 `new char[length]` +整段copy。
- region跨allocation run时,stream按run边界逐段返回。
- 本批使用同一次source read同时完成RAM preload和 `FileSegment` fill,不再保留A4b
  开放项。
- FileSegment fill必须遵守 `QueryLimit`、reserve、disk failure和异常原子性合同。
- 在局部 `PreloadData` 完整读取成功后再提交 `preloadData_`,避免读取异常后错误显示
  `preloaded()==true`。

### 5.7 coalesced ranges

- `CoalescedGroup` 显式保存精确 `vector<Range>` 或精确chunk-index列表。
- 现有 `begin/end` 只是plan_下标包围范围;chunk经过排序/分桶后可能不连续,不能用它
  恢复精确ranges。
- source IO可以合并系统调用,但只把明确cache-fill ranges写入FileSegment。
- 一般gap不持久化;若gap位于某个segment的 `currentWriteOffset` 与目标range之间,
  它属于FileSegment顺序写所必需的prefix,允许写入并单独计入prefix over-read。
- gap RED测试使用 `boundaryAlignment=1` 和可独立成segment的ranges,避免把必要prefix
  误判为过度缓存。

### 5.8 warm 内存与统计

- scratch使用MemoryPool-backed buffer,不使用未计量的 `std::vector<char>`。
- warm记录实际source bytes、raw bytes、prefetch bytes、source latency和对应
  `ProfileEvents`。
- warm不调用demand专用 `recordReadBytes`。
- 后续local hit只增加local/cache-hit统计,不得重复增加source bytes。

### 5.9 确定性测试

- pure-planning测试使用null或controlled executor。
- warm/demand并发使用promise、barrier或test hook控制阶段。
- 不使用 `sleep_for` 轮询异步状态。
- 增加真实Parquet `clone -> enqueue -> load`、query cancellation、reset、
  Cache shutdown等待warm、`QueryLimit`、gap ranges和preload资源计量测试。

### 5.10 commit 粒度

- #1 + #2 一个commit:同一Request/TrackingId/discarded-stream合同。
- #6 单独一个commit:恢复region API fail-fast。
- #3 单独一个commit:异步生命周期地基。
- #4、#5 同批但分别commit。
- #7、#8、#9 各自独立commit。

---

## 6. 第二次 Review

### 6.1 Review 范围

第二次 review 检查 `filecache2` 上 B5a 基线之后的修正提交:

```text
884c1d9e8..8de7d8976
```

覆盖:

```text
eeb91879b  Request 保存 TrackingId
523812117  region API 恢复 fail-fast
7015deb33  FileCache warm-task tracking
5e13f59b7  测试文件末尾换行
269e63e3a  真实 query cancellation wiring
b98485699  warm 持 QueryContextHolder
d82b6f07e  preload 重做
a6fc89239  coalesced exact ranges
d212e62e7  warm MemoryPool/统计 + preload run 直传
8de7d8976  异步测试整顿
```

Review 同时对照:

```text
port/design/filecache-buffered-input-velox-integration.md
port/design/filecache-buffered-input-review-remediation.md §5
```

### 6.2 已确认正确、无需返工

- `Request` 已按 `DirectBufferedInput` / `CachedBufferedInput` 保存
  `TrackingId` 值,不再保存 `StreamIdentifier *`。
- discarded stream采用Velox原生合同:`enqueue` 后request仍可参与
  `load`/prefetch。
- region API已恢复 `hasCache=false` + 基类 `VELOX_UNSUPPORTED` fail-fast。
- downloader election和每segment的 `SCOPE_EXIT` lease释放未发现错误。
- preload填盘已改为直接传allocation run指针;run边界拆成多次顺序write,不再使用
  中间scratch copy。`FileSegment::write` 每次只要求当前slice连续,不要求整个segment
  位于一个连续buffer,因此该实现成立。
- 删除旧 disk-hit `isBuffered=true` 断言仍然正确;`isBuffered` 只表示whole-file
  in-memory preload。

关于preload run直传的讨论说明:

- “写盘需要连续buffer”只约束**单次write调用**的 `[ptr, ptr + size)`,不要求整个
  FileSegment对应一块连续内存。
- `memory::Allocation` 的每个 `PageRun` 本身连续,因此可以直接把run内pointer传给
  `downloadChunkIntoSegment`。
- 遇到run边界时结束本次write,下一次从新run继续;`FileSegment` 本来就支持多次按
  `currentWriteOffset` 顺序写入。
- `downloadChunkIntoSegment` 已接受 `char * + size + offset`,无需改接口。
- 因此中间scratch copy既不是写盘必需,也没有降低接口复杂度;run直传同时消除一次copy
  和一份额外临时内存。

### 6.3 warm completion 必须覆盖 task payload 析构

#### 问题

当前wrapper在 `task(token)` 返回后通过 `SCOPE_EXIT` 把 `inflight_warm_` 减到0,
但executor closure及其捕获的task payload可能尚未析构。shutdown可能因此返回并开始
释放Manager-owned资源,随后 `WarmTaskContext` / `QueryContextHolder` / source file /
MemoryPool才析构。

shutdown合同必须等待:

```text
warm逻辑结束
-> task payload全部析构
-> inflight归零并notify
-> Cache shutdown继续
```

而不是只等待 `task(token)` 返回。

具体时序说明:

```text
task(token)返回
-> 当前实现的SCOPE_EXIT立刻把inflight减到0
-> shutdown线程被唤醒并继续释放Manager-owned资源
-> executor随后才销毁task closure
-> closure中的QueryContextHolder析构,仍会访问FileCache
```

问题不在warm逻辑是否执行完,而在warm closure持有的资源是否已经完成析构。
`inflight==0` 必须表示“task payload已完全退出生命周期”,不能只表示函数body返回。

#### 已批准解决方案

把task payload和生命周期tracker拆开:

```cpp
struct WarmTaskTracker
{
    std::mutex mutex;
    std::condition_variable cv;
    bool accepting{true};
    int64_t inflight{0};
    folly::CancellationSource cancel;
};
```

`FileCache` 持有 `shared_ptr<WarmTaskTracker>`;executor wrapper捕获tracker,
不依赖裸 `FileCache *` 完成计数。

warm payload不再用 `FileCachePtr` 延长Cache生命周期:

```cpp
struct WarmTaskContext
{
    FileCache * cache; // non-owning; Cache shutdown等待tracker保证其有效
    FileCache::QueryContextHolderPtr queryContextHolder;
    std::shared_ptr<ReadFile> source;
    std::shared_ptr<memory::MemoryPool> pool;
};
```

wrapper使用显式内层scope:

```text
执行payload
-> 内层scope结束,先析构holder/source/pool
-> completion guard递减tracker.inflight并notify
```

异常栈展开也必须保持相同顺序。Manager仍只等待Cache;Task不知道Manager。

该方案的通俗层级是:

```text
Manager关停前等待Cache
Cache关停前等待自己的warm payload全部析构
warm task完全不知道Manager
```

tracker独立成shared state,是为了让completion计数不依赖已经进入析构阶段的
`FileCache *`。

#### 实施计划(Controller,待审后执行)

核实过的当前落点(HEAD=418c34bd8):

- `FileCache::submitWarm`(`FileCache.cpp`)当前 wrapper:
  ```cpp
  executor->add([this, token, task, complete]() mutable
  {
      SCOPE_EXIT { complete(); };   // 减 inflight
      task(token);
  });
  ```
  `complete()` 在 `SCOPE_EXIT` 触发时执行 —— 但此刻 lambda closure(含捕获的 `task`,
  即整个 warm payload)**尚未析构**。complete 减 inflight 到 0 → deactivate 被唤醒 →
  释放 Manager 资源 → executor 之后才销毁 closure → `task` 析构 → `WarmTaskContext`
  的 `QueryContextHolder` 析构访问已释放的 FileCache。**这是竞态窗口。**
- 当前成员:`warm_mutex_` / `warm_cv_` / `warm_cancel_source_` /
  `accepting_warm_tasks_` / `inflight_warm_`(`FileCache.h:439-443`),wrapper 捕获裸
  `this`。deactivate 减到 0 即继续 shutdown。
- `warmSourceGroup` 的 `WarmTaskContext { FileCachePtr cache; QueryContextHolderPtr
  queryContextHolder; }` —— `cache` 当前是 **owning** `FileCachePtr`,与"Cache 等 warm"
  层级相反(warm 不应 own Cache)。

改动(最小方案,一个 commit)。**不引入 `WarmTaskTracker`** —— 只要 wrapper 先析构
payload 再 `complete()`,现有 `warm_mutex_` / `inflight_warm_` / 裸 `this` 就是安全的:

```text
FileCache 析构 -> deactivate 等待 inflight -> task 析构 payload
-> complete 用 this 减计数 -> deactivate 返回
```
保留现有五个成员,只做三处最小改动:

**(1) submitWarm wrapper 用内层 scope,保证 payload 先于 complete 析构**
```cpp
executor->add(
    [this, token = std::move(token), task = std::move(task)]() mutable
    {
        // completion guard 在最外层,最后执行
        SCOPE_EXIT { complete(); };   // complete(): 锁 warm_mutex_ 减 inflight + notify
        {
            // 内层 scope:payload 在这里执行并析构。task 是 move-only 的
            // folly::Function,持 WarmTaskContext(holder/source/pool)。
            auto localTask = std::move(task);
            localTask(token);
        } // <- localTask 在此析构 -> holder/source/pool 全部析构
          //    (holder 析构调 removeQueryContext,此刻 FileCache 仍存活:
          //     deactivate 还没被唤醒,因为下面的 SCOPE_EXIT 尚未减 inflight)
    });
```
- 关键顺序:**内层 scope 结束(payload+holder/source/pool 析构完)→ 才轮到最外层
  SCOPE_EXIT 执行 `complete()` 减 inflight+notify**。于是 `inflight==0` 严格表示
  "payload 已完全退出生命周期",deactivate 被唤醒时 holder 早已析构,裸 `this` 仍有效
  (deactivate 尚未返回,FileCache 未析构)。
- 异常路径:`localTask(token)` 抛异常时,内层 scope 栈展开先析构 localTask
  (holder/source/pool),再到外层 SCOPE_EXIT 执行 complete —— 顺序与正常路径一致。
- `executor->add` 本身抛异常(task 没进 executor)：提交线程回滚 `complete()`,
  与现状一致(payload 由未提交的 lambda 就地析构)。

**(2) `deactivateBackgroundOperations`:`requestCancellation()` 必须在锁外**
folly cancellation callback 可能**同步执行**,而 callback / task 完成路径也会锁
`warm_mutex_` —— 持 `warm_mutex_` 调 `requestCancellation()` 可能自锁死。改成三段:
```cpp
{
    std::lock_guard<std::mutex> lock(warm_mutex_);
    accepting_warm_tasks_ = false;
}
warm_cancel_source_.requestCancellation();   // 锁外
{
    std::unique_lock<std::mutex> lock(warm_mutex_);
    warm_cv_.wait(lock, [this] { return inflight_warm_ == 0; });
}
```
之后才走现有 `shutdown.store(true) ... metadata.shutdown()` 串。

**(3) `WarmTaskContext` 改 non-owning `FileCache*` + 收 source/pool**
```cpp
struct WarmTaskContext
{
    FileCache * cache;                       // non-owning; deactivate join 保证有效
    FileCache::QueryContextHolderPtr queryContextHolder;
    std::shared_ptr<ReadFile> source;        // 显式纳入,析构序受控
    std::shared_ptr<memory::MemoryPool> pool;
};
```
- `cache` 从 owning `FileCachePtr` 改 non-owning `FileCache*`:warm 不再延长 Cache 生命周期;
  Cache 的存活由"deactivate 等 inflight 归零"保证(层级正确:Cache 等 warm,warm 不 own Cache)。
- source/pool 从散落捕获收进 `WarmTaskContext`,让它们的析构序与 holder 一起受内层 scope 控制。
- 成员声明序:`queryContextHolder` 在 `cache` 之后(先析构 holder 再"析构"non-owning
  指针,holder 析构时 cache 仍有效)。

**RED**:构造"warm payload 析构慢"(用一个析构时 `post` baton / 递增探针计数的对象塞进
payload,让 holder/payload 析构可观测),另一线程 deactivate;断言 deactivate 返回**之后**
payload 已析构(探针已归零 / baton 已 post)。中和(把 `complete()` 移回 payload 析构之前
= 现状 wrapper)→ deactivate 可能在 payload 析构前返回 → 断言红。修后绿。若"析构可观测
探针"难以稳定,退而验证:`inflightWarmForTest()` 在 payload 析构后才归零。

全 7 套 gate 回归 + warm 并发多跑;逐 commit 落 filecache2 → cherry-pick → 切回。

### 6.4 preload range 校验必须避免无符号溢出

#### 问题

以下校验会先执行可能溢出的加法:

```cpp
offset + length <= preloadSize
```

接近 `UINT64_MAX` 的offset可在加法后回绕成小值,绕过校验并进入非法指针运算或
`Allocation::findRun`。

具体例子:

```text
offset = UINT64_MAX - 10
length = 20
offset + length = 9  // unsigned回绕
preloadSize = 1024
```

原检查会错误接受 `9 <= 1024`,随后使用接近 `UINT64_MAX` 的真实offset做pointer/run
计算。

#### 已批准解决方案

改为两个不会溢出的检查:

```cpp
VELOX_CHECK_LE(offset, preloadData_->size);
VELOX_CHECK_LE(length, preloadData_->size - offset);
```

`PreloadedRunStream::seekToPosition` 同样校验目标position不超过stream region长度。

两个check的含义:

```text
offset <= size           // 保证 size - offset 不下溢
length <= size - offset  // 保证range不越过文件尾
```

不能交换为另一个仍然先做 `offset + length` 的表达式。

### 6.5 `reset` 必须取消旧 input generation

#### 问题

当前 `reset` 只清:

```text
requests_
plan_
sourceGroups_
```

已提交到executor的旧warm仍继续下载和写cache。index lookup复用input时,
reset后的新计划可能与旧计划同时执行,浪费source IO、磁盘、cache容量和
`QueryLimit`。

具体场景:

```text
计划A:预取文件开头8 MiB
load提交warm A
reset:调用方表示计划A作废
计划B:读取文件末尾8 MiB
```

如果没有generation cancellation,warm A仍会在后台下载开头数据。query结果不会错,
但reset没有真正废弃旧计划。

#### 已批准解决方案

`FileCacheBufferedInput` 增加input-local cancellation generation:

```text
构造input -> generation A
warm捕获 A token
reset:
  cancel A
  清planning
  创建 generation B
新warm捕获 B token
```

普通input析构不取消generation,保持discarded request的Velox原生语义。warm同时检查:

```text
query token
FileCache shutdown token
input generation token
```

普通析构和显式reset必须区分:

- stream/input对象暂时销毁不等于撤销已经enqueue的请求;Velox原生仍允许这些request
  参与prefetch。
- `reset` 是调用方明确表示“旧计划不要了”,所以只有reset关闭旧generation。

### 6.6 query cancellation 按 chunk 边界生效

第二次 review最初建议在source IO返回后、reserve/write之前再次取消。讨论后确认不需要
丢弃已经从source完整读回的chunk。

讨论的具体场景:

```text
T0 warm检查未取消
T1 发起S3/HDFS read并等待网络
T2 用户取消query
T3 source read返回一个完整chunk
```

两个可选语义:

```text
立即取消:丢掉T3已经读回的bytes,不写cache
chunk边界取消:把已读chunk写完,下一轮开始时退出
```

最终选择chunk边界取消,因为远端IO已经发生,把该chunk写入cache可避免浪费。该选择只允许
完成当前bounded chunk,不允许继续完成整个segment。

最终合同:

```text
取消在warm chunk边界生效
已经完成source read的chunk允许reserve并写入FileCache
下一轮开始时观察取消并退出
```

理由:

- 已经发生的远端IO不应白费。
- chunk受 `ReadBufferFromVeloxReadFile::kDefaultBufferSize` 限制,不会一次继续填完整
  32 MiB segment。
- Cache shutdown会等待task完成当前chunk并释放downloader lease,生命周期仍安全。

因此此项关闭,当前chunk-boundary cancellation行为可保留。

### 6.7 preload fill 失败语义对齐 CH demand 路径

ClickHouse没有完全相同的“whole-file RAM preload同时填FileCache”路径,但
FileSegment fill失败应复用CH demand缓存合同:

```text
reserve失败:
  不属于磁盘故障
  RAM preload成功
  FileCache不再写
  不读取skipCacheOnDiskFailure
  query成功

实际磁盘写失败:
  FileSegment -> PARTIALLY_DOWNLOADED_NO_CONTINUATION
  释放downloader lease
  skipCacheOnDiskFailure=true  -> 保留RAM preload,query成功
  skipCacheOnDiskFailure=false -> 异常传播,RAM preload不提交,query失败
```

讨论中区分了三层语义:

1. CH demand read已有明确合同:reserve失败旁路;实际磁盘写失败由
   `skipCacheOnDiskFailure` 决定旁路或抛出。
2. Velox `CachedBufferedInput::preload` 的RAM cache entry是preload主体,主体失败会传播。
3. Velox `AsyncDataCache` 向SSD下沉是附加优化,SSD后台写失败只warning。

本项目的RAM preload是主体,FileSegment fill是附加动作;但最终决定不是“所有fill失败都
best-effort”,而是让FileSegment实际写盘继续遵守CH的 `skipCacheOnDiskFailure` 合同。
reserve不足仍不视为磁盘故障。

当前 `fillFileSegmentsFromPreload` 最外层broad catch会吞掉strict模式异常。

已批准修复:

- 删除最外层 `catch (std::exception)` / `catch (...)`。
- reserve失败和 `skip=true` 磁盘失败继续由 `downloadChunkIntoSegment` 转成false,
  preload可提交RAM数据。
- `skip=false` 磁盘失败由 `downloadChunkIntoSegment` 重新抛出并传播出 `preload`。
- `preloadData_` 只在fill成功或允许旁路的失败后赋值;strict失败时保持
  `preloaded()==false`。
- `getOrSet` 等非磁盘逻辑异常同样不静默吞掉。

删除broad catch的原因:

```cpp
try
{
    fill...
}
catch (...)
{
    LOG(WARNING); // 当前会把skip=false异常也吞掉
}
```

`downloadChunkIntoSegment` 已经把允许旁路的失败转换成false:

- reserve失败 -> false。
- disk write失败且skip=true -> false。
- disk write失败且skip=false -> rethrow。

因此外层不需要再次捕获。删除外层catch后,上述三种结果自然得到所需语义,且每segment的
`SCOPE_EXIT` 仍负责释放downloader lease。

### 6.8 多 segment batch 项关闭

第二次 review曾担心warm/preload只调用一次带 `segmentsBatchSize` 的 `getOrSet`,
可能只处理第一批segment。结合目标配置和Velox planning保护后关闭该项:

```text
filePreloadThreshold = 8 MiB
loadQuantum          = 8 MiB
maxCoalesceBytes     = 128 MiB
maxCoalesceDistance  = 512 KiB
maxFileSegmentSize   = 32 MiB
segmentsBatchSize    = 20
```

- preload默认最多8 MiB,通常只对应1个FileSegment。
- 单个warm group默认最多约128 MiB加少量gap,通常约4–5个32 MiB segment。
- 明显小于20个segment的一批上限。

目标实践配置由Velox quantum/coalesce上限和CH max segment共同保护,不增加多batch
循环复杂度。

讨论说明:

- 原生 `DirectBufferedInput::preload` / `CachedBufferedInput::preload` 返回前覆盖整个
  文件,不会把“只做第一批”当作成功。
- 但本项目还存在更强的实际上限:preload只接受默认不超过8 MiB的文件,而FileCache默认
  segment上限是32 MiB。
- warm不受preload阈值控制,但单个group受128 MiB `maxCoalesceBytes` 和512 KiB gap距离
  控制;配合32 MiB max segment,通常只产生4–5个segment。
- 因此默认实践配置显著低于20个segment的holder batch上限。讨论后选择依赖这些已有
  保护,不为非目标配置增加多batch循环。

### 6.9 warm 统计必须按真实 source IO 记账

#### 问题

当前source统计在cache write成功后才增加。若source已经读回bytes,但reserve或磁盘写
失败,实际远端流量会漏记。

重复/重叠requested ranges逐项求和也会把同一物理byte重复计入useful prefetch。
全局 `ProfileEvents::CachedReadBufferReadFromSourceBytes` 也尚未记录warm source IO。

具体例子1:

```text
S3已读回1 MiB
FileCache reserve失败
```

真实source流量是1 MiB,不能因为后续cache未写成功而显示source read为0。source统计必须
在IO返回后立即提交。

具体例子2:

```text
request A = [0, 1 MiB)
request B = [0, 1 MiB)
实际source只读1 MiB
```

逐range相加会把useful prefetch算成2 MiB。useful bytes必须按区间并集计算,不能超过
实际source bytes。

该口径对齐CH原则:统计真实发生的source IO,不以cache write成败决定是否记账。

#### 已批准解决方案

对齐CH“按真实发生的IO记账”:

1. source IO返回后立即记录:
   - `IoStatistics::read`
   - raw bytes
   - source latency
   - `ProfileEvents::CachedReadBufferReadFromSourceBytes`
2. 后续reserve/write失败不能撤销source统计。
3. requested ranges先求区间并集,再计算当前chunk的useful prefetch bytes。
4. gap/顺序写prefix只计over-read,不计useful prefetch。
5. warm不调用demand专用 `ScanTracker::recordRead`。

### 6.10 异步测试必须证明真实时序

#### warm / demand concurrency

当前测试只“提交warm -> 立即demand -> join”,不能证明两者真正同时争用downloader。

为什么 `executor.join()` 不足:

```text
可能时序A:warm全部完成 -> demand读cache hit -> join
可能时序B:demand先成为downloader -> warm跳过 -> join
可能时序C:warm和demand真正重叠
```

三种时序当前都可能绿,但只有C验证了并发合同。

已批准增加确定性测试:

```text
warm取得downloader并进入source read
-> post(warmStarted),wait(allowWarm)
测试等待warmStarted
-> 启动demand
demand进入FileSegment::wait
-> TestValue hook post(demandWaiting)
测试等待demandWaiting
-> post(allowWarm)
等待warm和demand结束
```

使用 `Baton` + `TestValue` hook,不使用sleep或轮询。

最小测试接缝:

- blocking source `ReadFile` 在warm取得downloader、进入source read后
  `post(warmStarted)` 并等待 `allowWarm`。
- `FileSegment::wait` 增一个 `TestValue::adjust` hook,当demand确认看到
  `DOWNLOADING` 并进入wait时 `post(demandWaiting)`。
- 测试严格等待两个Baton后才释放warm,因此确定覆盖warm持lease + demand等待的竞态。

#### discarded stream / pure planning

- discarded stream继续采用Velox原生合同:stream销毁不撤销已登记request,
  `load`仍可prefetch。
- 旧的“discarded后不得创建segment/source read”断言必须删除。
- 新测试验证discarded stream仍可warm且无UAF。
- pure-planning测试使用null/controlled executor,只检查 `plan_` /
  `sourceGroups_`,不检查异步cache状态。

discarded stream的讨论说明:

```cpp
auto stream = input.enqueue(region);
stream.reset();
input.load();
```

采用Velox原生语义后,`enqueue` 已经把region加入input计划;stream只是未来消费数据的
句柄。销毁句柄不撤销request,所以load仍可warm。旧的“cache必须为空”断言与该合同相反。

### 6.11 增加真实 Parquet E2E

当前UAF单测模拟临时 `StreamIdentifier`,但未覆盖真实:

```text
Parquet Reader
-> clone FileCacheBufferedInput
-> column chunks enqueue(stack-local sid)
-> load
-> PageReader消费stream
```

单元UAF测试只能证明 `TrackingId` 已按值保存,不能证明:

- Parquet clone是否复制完整FileCache/tracker/query context。
- 多column chunk enqueue后统一load是否正确。
- stack-local sid在真实reader调用栈中是否安全。
- cold/warm row-group读取是否走预期FileCache路径。

所以需要真实format-level测试,不能只依赖手工构造sid。

已批准增加最小Parquet E2E:

```text
生成至少2 column、2 row group的小Parquet文件
通过安装后的FileCache builder创建Parquet reader
cold scan -> 内容正确、source有读取、FileCache被填充
fresh reader warm scan -> 内容正确、source读取显著减少或为0
```

该测试同时覆盖clone上下文、sid生命周期、row-group planning和warm FileCache读取。

---

## 7. 第三次 Review

### 7.1 Review 范围

第三次 review 检查第二次 review 第6节对应的7个修复commit:

```text
8de7d8976..010b4aad1
```

包含:

```text
47e9a993f  6.4 preload range无符号溢出
d0ac652c6  6.7 preload fill strict异常传播
418c34bd8  6.9 warm真实source IO记账
f33ffc753  6.3 warm payload析构后再完成inflight
4b9d020e9  6.5 reset取消旧input generation
53e8c93cc  6.10 warm/demand确定性并发测试
010b4aad1  6.11 DWRF format E2E
```

6.6 chunk边界取消和6.8多segment batch已在第二次review讨论关闭,本轮不重新打开。

### 7.2 本轮确认通过

- `f33ffc753` 的正常和异常路径都先析构inner `localTask`,再执行outer
  `complete()`;`QueryContextHolder` / source / pool不会晚于 `inflight==0`。
- `requestCancellation()` 已移到 `warm_mutex_` 锁外,消除了同步callback与completion
  争用同一mutex的自锁风险。
- `WarmTaskContext` 使用non-owning `FileCache *`;Cache通过deactivate等待inflight保证
  task期间有效。holder先于Cache生命周期结束。
- `47e9a993f` 使用:

```cpp
offset <= size
length <= size - offset
```

  避免 `offset + length` unsigned回绕;preloaded stream seek也有region上界检查。
- `4b9d020e9` 的input generation token按值捕获;`reset`关闭旧generation并建立新
  generation;普通析构不取消,保持discarded request原生语义。
- `418c34bd8` 的区间并集使用排序+合并,重复/重叠/相邻ranges的物理byte只计一次。
- warm source bytes已移动到cache write之前记账,方向正确;write失败不再抹掉真实source
  IO。
- `d0ac652c6` 删除preload fill最外层broad catch的方向正确;per-segment
  `SCOPE_EXIT` 在异常展开时继续释放downloader lease。
- DWRF E2E是真实format-reader测试,确实覆盖builder选择、
  clone/enqueue(stack sid)/load/consume和cold fill/warm hit。它应保留为额外format
  覆盖。

### 7.3 InlineExecutor 双completion项关闭

review发现理论场景:

```text
InlineExecutor::add在当前线程执行task
task抛异常
-> wrapper SCOPE_EXIT执行complete
-> 异常穿过add进入submitWarm外层catch
-> catch再次complete
```

会使 `inflight` 从1减到0再减到-1。

讨论后关闭该项,不增加幂等completion:

- 生产调用链只有 `FileCacheBufferedInput::warmSourceGroup` 调 `submitWarm`。
- executor来自Hive scan/index/delete-reader的IO executor,目标部署使用异步IO线程池,
  不会传 `InlineExecutor`。
- `submitWarm` 的生产合同限定为异步IO executor。
- 不为非目标executor增加shared atomic/once状态。

若未来新增Inline/同步executor调用方,该调用方必须先扩展 `submitWarm` 合同和测试。

### 7.4 Release构建中的 `TestValue` 测试

#### 问题

`FileSegment::wait` 的并发测试使用:

```cpp
TestValue::adjust("facebook::velox::ch::FileSegment::wait::beforeWait", this);
```

`NDEBUG` 下 `TestValue` registration和adjust都是no-op。Release测试仍执行时:

```text
demandWaiting永远不post
测试线程等待demandWaiting
warm线程等待allowWarm
```

形成确定性死锁。

#### 已批准解决方案

该时序测试只需要Debug覆盖,不增加production test hook。仅修改UT:

```cpp
#ifndef NDEBUG
TEST_F(FileCacheBufferedInputBuilderTest, WarmHoldsLeaseWhileDemandWaits)
{
    ...
}
#endif
```

Debug继续用 `Baton + TestValue` 证明:

```text
warm持downloader lease
-> demand确认DOWNLOADING并进入wait
-> 测试才释放warm
```

Release不注册此测试,因此不会挂死。不得使用 `GTEST_SKIP`、sleep或轮询。

### 7.5 `skipCacheOnDiskFailure` 必须精确对齐CH

#### 问题

`downloadChunkIntoSegment` 当前catch所有 `std::exception`。当
`skipCacheOnDiskFailure=true` 时,offset、state、reserve invariant等逻辑异常也会被
转换成false/bypass。

#### CH source合同

`CachedOnDiskReadBufferFromFile::writeCache` 只catch `ErrnoException`:

```text
ENOSPC(28) / EDQUOT(122):
  无论skip设置,记录空间不足并返回false

其他磁盘errno:
  skip=true  -> 记录disk IO错误并返回false
  skip=false -> 抛 CACHE_CANNOT_WRITE_TO_CACHE_DISK

非ErrnoException:
  不catch,直接传播
```

#### 已批准解决方案

Velox port只catch `FileCacheErrnoException`:

```cpp
catch (const FileCacheErrnoException & e)
{
    if (e.getErrno() == ENOSPC || e.getErrno() == EDQUOT)
        return false;
    if (skipOnDiskFailure)
        return false;
    throw;
}
```

所有非errno逻辑异常继续传播。增加 `skip=true + 非磁盘逻辑异常` RED测试,证明配置不会
吞掉程序错误。

### 7.6 strict preload失败仍需记录source IO

#### 问题

preload当前顺序:

```text
完整source read到localData
-> fill FileSegment
-> fill成功后才记录read/raw统计
-> commit preloadData
```

strict FileSegment写失败在fill阶段抛出时,source read已经真实完成,但统计仍为0。

#### 已批准解决方案

完整source read成功后立即提交:

```text
IoStatistics::read
rawBytesRead
source latency
对应source ProfileEvents
```

然后再执行 `fillFileSegmentsFromPreload`。fill后续是否成功不改变已经发生的source IO
统计。strict fill失败仍不提交 `preloadData_`,但query失败日志和Spark/operator统计必须
包含真实source流量。

增加strict磁盘失败测试,同时断言:

```text
preload抛异常
preloaded()==false
source read/raw/ProfileEvents已增加
```

### 7.7 普通析构不取消warm的测试需消除false-green

#### 问题

当前 `PlainDestructorDoesNotCancelWarm`:

```text
提交warm
立即析构input
等待warm完成
```

warm可能在input析构前已经全部完成。即使未来析构错误地取消generation,测试也可能绿。

#### 已批准解决方案

仅改UT,使用单线程executor前置blocking task:

```text
executor线程先阻塞
-> warm只能排队,尚未开始
-> 析构FileCacheBufferedInput
-> 释放blocking task
-> warm开始执行
-> 断言所有目标segment均完成
```

这样严格证明“input已经析构后,排队中的warm仍未被取消”。使用Baton/controlled
executor,不使用sleep或无界yield。

### 7.8 DWRF保留,仍需Parquet E2E

DWRF E2E覆盖了通用 `BufferedInput` format-reader合同,但不能替代第6.11节明确批准的
Parquet验证。

原因:

- 原生产UAF调用点是 `ParquetData::enqueueRowGroup` 的stack-local
  `StreamIdentifier`。
- Parquet通过 `ReaderBase::inputs_` 持有row-group clone。
- `StructColumnReader::loadRowGroup` 有Parquet特有的
  `isBuffered -> clone/enqueue/load` 分支。
- DWRF使用stripe / `UnitLoader` 生命周期,不能证明Parquet row-group调度。

已批准:

- 保留 `FileCacheFormatE2ETest` 的DWRF cold/warm测试。
- 另增最小Parquet E2E:

```text
至少2 columns、2 row groups
安装FileCacheBufferedInputBuilder
真实Parquet Reader走clone/enqueue(stack sid)/load/PageReader consume
cold scan内容正确、source有读取、FileCache被填充
fresh warm reader内容正确、source读取显著减少或为0
```

Parquet测试完成前,第6.11验收未关闭。

### 7.9 第三次 Review 最低修复集合

```text
1. 6.10并发测试仅在 !NDEBUG 下编译。
2. downloadChunkIntoSegment只catch FileCacheErrnoException,按CH errno语义处理。
3. preload source统计移动到FileSegment fill之前。
4. 重写PlainDestructorDoesNotCancelWarm为阻塞executor确定性测试。
5. 保留DWRF E2E并新增Parquet E2E。
```

本轮明确关闭、不要求修改:

```text
InlineExecutor双completion(生产合同仅异步IO executor)
6.6 chunk边界取消
6.8多segment batch
```

---

## 8. 第四次 Review

### 8.1 Review 范围

第四次 review 检查第三次 review 第7节最低修复集合对应的6个commit:

```text
010b4aad1..97a1a4a6b
```

包含:

```text
0b10608dc  7.4 并发测试仅在 !NDEBUG 下编译
5052224d7  7.5 downloadChunkIntoSegment只catch errno
e24d0e77a  7.5 demand writeCache同样只catch errno
9c0babe64  7.6 preload source统计移到fill之前
62e3eece2  7.7 重写PlainDestructor测试
97a1a4a6b  7.8 新增真实Parquet E2E
```

### 8.2 本轮确认通过

- 依赖 `TestValue` 的warm/demand确定性并发测试已用 `#ifndef NDEBUG` 包住。
  Debug保留时序C覆盖,Release不注册该测试,不会等待一个永远不触发的hook。
- `downloadChunkIntoSegment` 和demand `writeCache` 的消费者分类已改为只catch
  `FileCacheErrnoException`;非errno逻辑异常不会再被 `skipCacheOnDiskFailure` 转成
  bypass。该方向与CH `writeCache` 只catch `ErrnoException` 一致。
- preload source bytes/raw/latency/ProfileEvents已在完整source read后、
  `fillFileSegmentsFromPreload` 前提交。strict fill随后抛异常也不会抹掉已经发生的
  source IO。
- `PlainDestructorDoesNotCancelWarm` 已使用单线程executor前置blocking task,
  保证input析构时warm仍在队列中尚未执行;释放后warm完成,能够证明普通析构没有取消
  generation。
- 原DWRF format E2E保留;新增Parquet E2E使用真实Parquet writer/reader、2 columns、
  2 row groups,通过安装的FileCache builder执行真实
  clone/enqueue(stack sid)/load/PageReader consume链路。
- Parquet cold scan验证source读取和FileCache fill;fresh warm reader验证内容正确且
  source读取为0。

### 8.3 生产结构化 errno producer 缺失

#### 问题

第三次 review 修复了errno**消费者**:

```cpp
catch (const FileCacheErrnoException & e)
```

但FileCache默认生产writer仍是:

```cpp
std::make_unique<velox::LocalWriteFile>(...)
```

`LocalWriteFile::append` 直接调用 `::write`,失败后通过 `VELOX_CHECK_EQ` 抛
`VeloxRuntimeError`。它不产生 `FileCacheErrnoException`,结构化errno在异常边界已经
丢失。

真实磁盘故障时序:

```text
ENOSPC / EDQUOT / EIO
-> LocalWriteFile抛VeloxRuntimeError
-> FileSegment::write catch(...)标记
   PARTIALLY_DOWNLOADED_NO_CONTINUATION并重新抛
-> downloadChunkIntoSegment / writeCache只catch FileCacheErrnoException,接不住
-> skipCacheOnDiskFailure=true仍然query失败
```

当前errno测试使用test factory注入一个主动抛 `FileCacheErrnoException` 的writer,
只能证明消费者分类,不能证明默认生产writer。

`FileCacheErrnoException.h` 也已明确记录该pre-release gap:生产
`LocalWriteFile::append` 尚不产生typed errno。

#### 讨论过的方案

1. 包装现有 `LocalWriteFile`:
   - 不可行。它抛出时errno只存在于异常文本和thread-local `errno`;外层解析字符串或
     假设errno仍保持不变都不可靠。
2. 修改Velox通用 `LocalWriteFile`:
   - 会改变所有Velox本地writer的异常ABI/行为,扩大trunk影响面。
3. FileCache专用typed writer:
   - 改动局限在 `velox/ch`,可以完全复现当前FileCache所需open/append/flush/close语义,
     并在每个系统调用失败点立即保存errno。

#### 已批准解决方案

新增:

```text
velox/ch/IO/FileCacheLocalWriteFile.h
velox/ch/IO/FileCacheLocalWriteFile.cpp
```

实现FileCache实际使用的 `velox::WriteFile` 接口:

```text
append(std::string_view)
flush
close
size
getName
```

构造/open语义与当前默认 `LocalWriteFile` 保持一致:

```text
O_WRONLY | O_CREAT
不使用O_EXCL
open后lseek到文件尾
支持PARTIALLY_DOWNLOADED segment续写
buffered IO
```

每个系统调用失败时立即保存errno并抛:

```cpp
throw FileCacheErrnoException(savedErrno, ...);
```

规则:

- `::write` 返回-1:保存当前errno。
- short write但无有效errno:按 `EIO` 分类。
- open/lseek/flush/close失败同样保留结构化errno。
- destructor必须noexcept;未显式close时执行非抛出清理并记录日志。
- 不解析异常字符串。

`FileSegment` 默认writer factory从 `LocalWriteFile` 改为
`FileCacheLocalWriteFile`;现有test factory注入接口继续保留。

#### 必需测试

- 默认FileCache writer实际产生 `FileCacheErrnoException`,不能只测试注入writer。
- 可通过FileCacheLocalWriteFile的窄syscall seam注入:
  - ENOSPC;
  - EDQUOT;
  - 其他errno(如EIO);
  - short write无errno。
- 验证最终消费者合同:

```text
ENOSPC / EDQUOT:
  skip=false/true均返回false并旁路

其他errno:
  skip=true返回false并旁路
  skip=false传播typed异常

非errno逻辑异常:
  始终传播
```

typed producer完成前,第7.5验收不关闭。

### 8.4 preload latency UT 断言不稳定

#### 问题

`PreloadStrictFillFailureStillRecordsSourceStats` 使用64字节本地文件并断言:

```cpp
EXPECT_GT(io->storageReadLatencyUs().sum(), 0u);
```

计时单位是微秒。快速本地文件系统上合法读取可能落在同一微秒tick内,结果为0,
产生与功能无关的偶发失败。

该测试真正要证明的是:

```text
strict fill抛异常之前
source bytes/raw bytes/ProfileEvents已经记账
```

这些均有确定性精确断言。

#### 已批准解决方案

删除 `latency > 0` 断言,保留:

```text
IoStatistics::read == fileSize
rawBytesRead == fileSize
ProfileEvents source bytes delta == fileSize
preload抛strict异常
preloaded()==false
```

不为单一测试增加可注入时钟。

### 8.5 第四次 Review 最低修复集合

```text
1. 新增FileCacheLocalWriteFile typed errno producer。
2. FileSegment默认writer改用typed writer。
3. 增加默认生产writer的errno producer/consumer测试。
4. 删除64字节preload测试的latency > 0µs断言。
```

typed errno producer完成前,第四次 review结论为:

```text
Status: BLOCK
```

---

## 9. 第五次 Review

### 9.1 Review 范围

第五次 review 检查第四次 review 第8节最低修复集合对应的3个commit:

```text
97a1a4a6b..bcbbdbb5e
```

包含:

```text
28edebc55  8.4 删除preload strict测试的latency > 0不稳定断言
b8f189e43  8.3 新增FileCacheLocalWriteFile typed errno producer
bcbbdbb5e  8.3 修另一处ScopedWriteFileFactory恢复默认writer
```

### 9.2 本轮确认通过

- `PreloadStrictFillFailureStillRecordsSourceStats` 已删除微秒粒度
  `storageReadLatencyUs > 0` 断言,同时保留source bytes、raw bytes、
  `ProfileEvents` delta、strict异常和 `preloaded == false` 的确定性断言。
- `FileSegment` 默认factory已从 `LocalWriteFile` 切换到
  `FileCacheLocalWriteFile`;默认生产路径现在能够产生
  `FileCacheErrnoException`,不再只依赖测试注入writer。
- 新writer的open flags、0600 mode、seek-to-end续写和buffered IO语义与此前
  `LocalWriteFile` 调用保持一致。
- `FileSegmentTest` 和 `FileCacheBufferedInputTest` 两处
  `ScopedWriteFileFactory` 均恢复成 `FileCacheLocalWriteFile`,不会在同一测试
  binary内把默认factory污染回旧 `LocalWriteFile`。
- CMake已将新source和header加入mono及non-mono构建路径。

第四次 review识别出的typed errno producer缺失已经补上,但新writer本身仍有以下
两个阻塞问题。

### 9.3 positive short write处理偏离CH

#### 问题

当前 `FileCacheLocalWriteFile::append` 只调用一次 `::write`:

```cpp
const ssize_t written = ::write(fd_, data.data(), data.size());
if (static_cast<size_t>(written) != data.size())
    throw FileCacheErrnoException(EIO, ...);
```

positive short write不是“没有写入的失败”。例如请求写4000字节时:

```text
::write(fd, 4000) -> 1000
```

表示前1000字节已经物理落盘。当前实现随后直接抛 `EIO`,但:

```text
FileCacheLocalWriteFile::size_ 仍是旧值
FileSegment::downloaded_size 仍是旧值
物理文件已经增加1000字节
```

因此可能形成:

```text
physical file size > downloaded_size
physical file size > accounted reserved size
```

现有 `ShortWriteIsClassifiedAsEio` 也没有模拟这个事实。测试seam在
`ShortWrite` 模式下没有向文件写入strict prefix,而是直接抛 `EIO`,所以无法发现
账实不一致。

#### CH权威行为

CH `WriteBufferFromFileDescriptor::nextImpl` 不把positive short write当异常。
它维护 `bytes_written`,持续写剩余部分:

```cpp
while (bytes_written != offset())
{
    const ssize_t res =
        ::write(fd, working_buffer.begin() + bytes_written, offset() - bytes_written);

    if ((res == -1 || res == 0) && errno != EINTR)
        ErrnoException::throwFromPath(...);

    if (res > 0)
        bytes_written += res;
}
```

即:

```text
positive short write -> 继续写剩余字节
EINTR -> 重试
-1/0且非EINTR -> 保存errno并抛异常
全部写完 -> 提交完整append
```

#### 已批准解决方案

`FileCacheLocalWriteFile::append` 对齐上述CH循环:

1. 循环调用 `::write`,每次只传尚未写入的suffix。
2. `res > 0` 时累计已写字节并继续。
3. `errno == EINTR` 时重试。
4. 只有真正失败时才立即保存errno并抛 `FileCacheErrnoException`。
5. 全部写完后才增加 `size_`。

`ShortWriteIsClassifiedAsEio` 应改为验证:

```text
第一次syscall只写strict prefix
append继续写完剩余字节
最终文件内容和size均等于完整输入
```

不再把positive short write合成为 `EIO`。

### 9.4 close失败后重复关闭fd

#### 问题

当前 `FileCacheLocalWriteFile::close` 只在 `::close` 成功后设置:

```cpp
fd_ = -1;
closed_ = true;
```

失败时先抛异常,对象仍保留原fd和未关闭状态:

```cpp
if (::close(fd_) != 0)
{
    const int saved = errno;
    throw FileCacheErrnoException(saved, ...);
}
```

异常展开后 `WriteBufferFromVeloxWriteFile::cancelImpl` 释放writer,
`FileCacheLocalWriteFile` 析构看到 `fd_ >= 0 && !closed_`,会再次执行
`::close(fd_)`。

Linux上的 `close` 即使返回 `EINTR`、`EIO`、`ENOSPC` 或 `EDQUOT`,fd通常也已经
释放;错误表示关闭或延迟写回阶段报告了失败,不能据此重试同一fd编号。该编号可能已被
其他线程复用,第二次 `close` 可能关闭无关文件。

#### CH权威行为

CH `WriteBufferFromFile::close` 在成功和失败两条路径都会把fd置为-1:

```cpp
if (0 != ::close(fd))
{
    fd = -1;
    throw Exception(...);
}

fd = -1;
```

其析构也明确记录Linux上的 `EINTR` 不应重试。

#### 已批准解决方案

Velox实现直接对齐CH:

```text
::close失败:
  保存errno
  fd_ = -1
  closed_ = true
  抛FileCacheErrnoException

::close成功:
  fd_ = -1
  closed_ = true
```

析构不得重试一次已经调用过 `::close` 的fd。

增加close失败测试,证明异常保留typed errno,且对象析构不会再次关闭同一fd。

### 9.5 errno合同和注释收尾

`append` 或 `flush` 在writer已经close后被调用属于对象状态/调用顺序错误,不是一次
真实系统调用产生的磁盘errno。当前代码主动构造:

```cpp
FileCacheErrnoException(EBADF, ...)
```

会让 `skipCacheOnDiskFailure=true` 的消费者把程序逻辑错误当成磁盘故障旁路。这里
应抛非errno逻辑异常;只有实际open/lseek/write/fsync/close系统调用失败才产生
`FileCacheErrnoException`。

另外,`FileCacheErrnoException.h` 的注释仍写着生产路径没有typed producer。新增
`FileCacheLocalWriteFile` 后该说明已经过期,需要更新为当前producer/consumer合同。

### 9.6 第五次 Review 最低修复集合

```text
1. append按CH语义循环处理positive short write并重试EINTR。
2. 重写short-write测试,真实验证strict prefix后继续写完。
3. close无论成功或失败都清除fd状态,禁止析构重试。
4. 增加close失败且不重复close的回归测试。
5. append/flush after close改为非errno逻辑异常。
6. 更新FileCacheErrnoException中“无生产producer”的过期注释。
```

上述问题关闭前,第五次 review结论为:

```text
Status: BLOCK
```

---

## 10. 第六次 Review

### 10.1 Review 范围

第六次 review 检查第五次 review 第9.6节最低修复集合对应的单个commit:

```text
bcbbdbb5e..3389938b3
```

包含:

```text
3389938b3  FileCacheLocalWriteFile对齐CH权威写行为
```

Gluten对应commit:

```text
845a3f3a4
```

### 10.2 第9.6节验收

第五次 review要求的6项均已完成:

1. `FileCacheLocalWriteFile::append` 已按CH
   `WriteBufferFromFileDescriptor::nextImpl` 语义循环调用 `::write`。
   positive short write累计已写字节并继续写suffix,不再合成 `EIO`。
2. `EINTR` 不再向上传播;append循环继续重试。只有 `-1/0` 且非 `EINTR`
   才保存errno并抛 `FileCacheErrnoException`。
3. short-write测试会先向真实文件写入strict prefix,再由同一append循环写完剩余
   suffix,最终精确验证完整size和文件内容。
4. `FileCacheLocalWriteFile::close` 在成功和失败两条路径均设置
   `fd_ = -1`、`closed_ = true`;失败仍保留并传播typed errno,析构不会重试已经
   释放的fd。
5. `append`/`flush` after close已改用 `VELOX_CHECK` 产生非errno
   `VeloxRuntimeError`,不会被 `skipCacheOnDiskFailure` 误分类为可旁路磁盘故障。
6. `FileCacheErrnoException.h` 已更新为当前producer/consumer合同,删除“生产路径
   没有typed producer”的过期说明。

### 10.3 short-write测试 seam 讨论结论

Review中曾考虑将现有 `setWriteCapForTesting` 换成通用syscall hook,使测试形式严格
表现为:

```text
requested = 4000
syscall result = 1000
```

但这需要为测试继续修改 `FileCacheLocalWriteFile` 生产源码并在append热路径增加
可替换调用层。Velox现有 `FaultyFileSystem` / `FaultyWriteFile` 只能在
`WriteFile::append` API边界注入异常,不能模拟writer内部POSIX `::write` 的返回值,
无法直接复用到这一层。

当前 `setWriteCapForTesting` 虽然通过缩小第一次真实 `::write` 的长度实现prefix,
但它满足本项回归测试的核心证据:

```text
真实文件先只落strict prefix
同一次append必须进入后续循环
删除/中和续写循环后测试RED
最终size和文件内容必须等于完整输入
```

因此不为追求syscall mock形式上的完全等价再增加生产hook。第9.3实现与测试验收通过。

### 10.4 非阻塞收尾

`FileSegmentTest.cpp` 中 `WriteFailureProducesTypedErrno` 前的旧注释仍包含:

```text
a short write into EIO
```

该描述与第9.3的新语义相反。positive short write现在应继续写完,不再转成 `EIO`。
这是测试注释更新遗漏,不影响生产行为或测试有效性;后续代码整理时删除或改写该句。

### 10.5 第六次 Review 结论

第9.6节最低修复集合关闭。`3389938b3` 未发现生产正确性、生命周期或errno合同
blocker。

```text
Status: APPROVE
```

---

## 11. 第七次整体 Review

### 11.1 Review 范围与目标

本轮不再只检查上一轮的单点修复,而是将FileCache接入Velox的阶段A/B及五轮remediation
作为一个整体重新review。代码范围为闭区间:

```text
5785a43a^..506751cc5
```

即:

```text
5785a43a4  FileCache: Thread ScanTracker/fileNum/groupId into FileCacheBufferedInput
...
506751cc5  FileCache: 删除FileCacheLocalWriteFile生产类test seam,
             errno合同改用标准FaultyWriteFile
```

本轮重点:

1. 底层正常/异常路径是否继续对齐CH。
2. `BufferedInput`、format reader、`ScanTracker`、IO context等上层合同是否对齐Velox。
3. 生产代码是否为了测试异常引入特殊分支或进程级测试状态。
4. 是否复制了已有planner、stream、cache-write和downloader协议,并已因此发生语义漂移。

整体结论:

```text
FileCacheLocalWriteFile和FileSegment核心状态机未发现新的数据正确性blocker。
506751cc5删除writer内部test seam的方向正确。
上层Velox合同、reserve hint、planner执行和重复生产逻辑仍需修复。
Status: REQUEST CHANGES
```

---

## 12. FileCache核心回基线与Velox IO功能上移实施方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` or `superpowers:executing-plans`.
> Steps use checkbox syntax for tracking.

**Goal:** 将 `5785a43a` 之后落入
`velox/ch/Interpreters/FileCache` 的IO集成改动全部移出核心,使核心生产代码相对基线
只保留默认local cache writer改为 `FileCacheLocalWriteFile` 这一处获批行为变化。

**Architecture:** CH `FileCache` / `FileSegment` 恢复平行port。异步prefetch的规划、
执行、取消和payload ownership统一由Velox标准 `CoalescedLoad` 模型管理;
wait并发测试只使用Velox现有 `CoalescedLoad` `TestValue` 与测试double/Baton;
typed errno由基线factory的默认writer提供。测试case按被测层归位。

**Tech Stack:** C++20、Velox `BufferedInput` / `CoalescedLoad`、folly executor/future、
ClickHouse `FileCache` port、GTest。

本节是第11节核心边界的最终权威方案。它覆盖:

```text
第11.1.1节中“C1删除global factory”为允许例外的旧描述;
第11.8节中删除基线factory的旧方案;
第11.11节C5 RAII方案;
原FileCache::submitWarm / warm tracking方案。
```

### 12.1 完整审计结果

`5785a43a..HEAD` 中共有11个commit触及
`velox/ch/Interpreters/FileCache`:

| Commit | Core生产影响 | 最终处理 |
|---|---|---|
| `7015deb33` | `FileCache`新增warm submit/inflight/cancel/wait | 全部revert,功能交给C8 load |
| `f33ffc753` | warm payload析构后才减inflight | 核心revert,析构所有权由load对象成员顺序保证 |
| `53e8c93cc` | `FileSegment::wait` 增加 `TestValue` hook | 核心revert,不补替代生产hook;测试复用Velox现有机制 |
| `b8f189e43` | 默认writer换为typed writer | 保留唯一获批行为 |
| `3389938b3` | `FileCacheErrnoException.h` 注释 | core header恢复基线,说明移到IO层 |
| `506751cc5` | tests/CMake only | 按测试归位方案处理 |
| `1b92545ef` | 删除基线factory,直接创建writer | 撤销factory删除;仅保留默认factory换writer |
| `2f2628f04` | core test only | reserve hint case可保留并改为纯core测试 |
| `8cdeb7169` | 新增 `DownloaderLease` | 已由 `1b1c9d1f4` revert |
| `1b1c9d1f4` | revert C5 | 保留 |
| `5e13f59b7` | test文件换行 | 无行为影响 |

没有第五类未归属的core生产diff。

#### 12.1.1 第11节有效性矩阵

第11节保留完整review历史,但实施时按下表判断。若第11节任意文字与第12节冲突,
**以第12节为唯一权威**。

| 第11节 | 状态 | 仍有效内容 | 被第12节替代/废止内容 |
|---|---|---|---|
| 11.1 整体范围 | 历史背景有效 | 四个review目标、整体 `REQUEST CHANGES` | core改动边界以12.2为准 |
| 11.1.1 旧硬约束 | 部分失效 | IO集成不得新增CH核心抽象 | “允许C1删除factory”失效;最终仅允许typed default writer |
| 11.2 `506751cc5` | 部分有效 | writer内部test seam删除;short write/EINTR/close语义 | core factory的最终接法以12.2/12.3为准 |
| 11.3 / 11.3.1 C2 | 有效且已APPROVE | `ReadFileInputStream` / `FileIoContext`;`IoStats` owner UAF修复 | 无 |
| 11.4 mixed-state | 问题与最终规则有效 | 完整chunk覆盖;按CH state语义区分miss/downloading | `c12de374b` 当前实现本身未通过,需按外部review修 |
| 11.5 `reserve_hint` | 有效且C4已APPROVE | 四条IO路径传递剩余horizon | 无;非阻塞清理随C3做 |
| 11.6 preload stream | 有效,待实施 | 删除 `PreloadedRunStream`;统一 `FileCacheInputStream` / tracking | 无 |
| 11.7 coalesced load | 目标有效,旧执行方案失效 | prefetch主动、demand首次 `Next` 懒执行、request RAM buffer交付 | direct `preadv`/staging/downloader/write方案废止;以12.4为准 |
| 11.8 writer factory | **core方案失效** | `classifyCacheWriteError`及上层write helper仍有效 | 删除基线factory、core tests处置方案失效;改按12.2/12.6 |
| 11.9 actual/delivered | 有效,待收尾 | C3字节口径拆分正确 | “source latency可跳过”失效;必须补demand/predownload latency |
| 11.10 write helper | 有效且C4已APPROVE | 单一errno catch;reserve/write helper;删除dead wrapper | 无 |
| 11.11 downloader RAII | **全部失效** | 无 | C5已revert;保留上层显式election + `SCOPE_EXIT` |
| 11.12 intentional duplicates | 有效 | `FileCacheLocalWriteFile`;CH read-buffer adapter;不加syscall test hook | writer接入方式由12.2/12.3固定 |
| 11.13 旧commit顺序 | **失效** | 无 | 全部由12.7的R1–R5顺序替代 |
| 11.14 旧gate | 部分有效 | 8 target、format E2E、`-Werror`等通用gate | core diff和lifecycle gate由12.8替代 |

实施者不得从失效小节恢复代码。review引用旧小节时必须同时检查本矩阵和第12节对应
替代方案。

#### 12.1.2 第11节技术条目 ↔ R1–R5 覆盖映射

R1–R5 全部完成即代表第11节**所有技术条目**闭环。对照:

| 第11节技术条目 | 交付于 | 当前状态 |
|---|---|---|
| 11.3 / 11.3.1 C2 `FileIoContext` + `IoStats` UAF | 已完成 | ✅ APPROVE |
| 11.5 `reserve_hint` | 已完成(C4) | ✅ APPROVE(非阻塞清理随 R5) |
| 11.10 write helper / 单一 errno catch | 已完成(C4) | ✅ APPROVE |
| 11.8 `classifyCacheWriteError`(上层 helper 部分) | 已完成(C1) | ✅ 生产 APPROVE + 测试收尾 |
| 11.8 core factory 处置 | **R4** | core 恢复基线 + 仅默认 writer 换 typed |
| 11.11 downloader lease | **已废止** | C5 已 revert(`1b1c9d1f4`);R2 用上层 election+`SCOPE_EXIT` |
| 11.4 mixed-state classifier | **R5** | 按 CH state 语义重修 C7 |
| 11.9 actual/delivered | 字节口径已做(C3);**R5** | R5 补 demand/predownload latency |
| 11.6 删 `PreloadedRunStream` | **R5** | 统一 `FileCacheInputStream` |
| 11.7 coalesced load 执行模型 | **R2** | `FileCacheCoalescedLoad`调度 + `FileCacheInputStream`真实IO + request RAM ownership |
| 11.2 writer test seam / short-write 语义 | 已定 | 最终接法以 12.2/12.3 为准 |
| 11.12 有意保留的重复 | 有效 | writer 接入以 12.2/12.3 固定 |

被第12节**废止**(非"做完",是"被替代")的:11.1.1 旧硬约束(“允许 C1 删 factory”)、
11.13 旧 commit 顺序(由 R1–R5 替代)。

**R1–R5 之外的最终收尾**:`filecache2` 全 gate + 12.8 core zero-diff 通过后,gluten
分支一次性 cherry-pick 全部 commit 并解决历史 errno 分叉
(`downloadChunkIntoSegment`/`writeCache` 的 `std::exception` vs `FileCacheErrnoException`),
不创建中间 stacked 状态。本轮 R1–R5 期间 gluten 保持搁置。

因此完成判据:**R1–R5 完成 ⟹ 第11节技术条目全闭环 + 12.8 core 硬 gate 达成**;
其后仅剩 gluten 一次性 cherry-pick。

### 12.2 最终core生产diff白名单

最终相对 `5785a43a`:

```text
velox/ch/Interpreters/FileCache/FileCache.h                 zero diff
velox/ch/Interpreters/FileCache/FileCache.cpp               zero diff
velox/ch/Interpreters/FileCache/FileSegment.h               zero diff
velox/ch/Interpreters/FileCache/FileCacheErrnoException.h   zero diff
```

`FileSegment.cpp` 只允许:

```cpp
#include "velox/ch/IO/FileCacheLocalWriteFile.h"
```

和基线default factory中的唯一行为替换:

```cpp
FileSegment::WriteFileFactory & writeFileFactoryStorage()
{
    static FileSegment::WriteFileFactory factory =
        [](const std::string & path) -> std::unique_ptr<velox::WriteFile>
    {
        return std::make_unique<FileCacheLocalWriteFile>(path);
    };
    return factory;
}
```

基线以下API全部恢复:

```text
FileSegment::WriteFileFactory
FileSegment::setWriteFileFactoryForTesting
FileSegment::createWriteFile
writeFileFactoryStorage
```

生产不调用 `setWriteFileFactoryForTesting`;它仅保留为基线test seam。默认factory在
进程启动后不再被生产修改。

### 12.3 typed errno合同

`FileCacheLocalWriteFile` 继续位于 `velox/ch/IO` 并保持:

```text
open/lseek/write/fsync/close失败 -> 保存errno -> FileCacheErrnoException
positive short write             -> 循环续写
EINTR                            -> 重试
close成功/失败                   -> 均清fd,析构不重试
append/flush after close         -> 非errno逻辑异常
```

baseline `FileSegment::write` 已存在:

```cpp
catch (const FileCacheErrnoException & e)
```

及 `ENOSPC` / `EDQUOT` partial physical size reconciliation。替换default writer后该
既有CH合同在生产可达,不新增 `FileSegment` state或catch逻辑。

producer/consumer解释写在:

```text
velox/ch/IO/FileCacheLocalWriteFile.h
velox/ch/Disks/IO/FileCacheInputStream.h
```

`FileCacheErrnoException.h` 本身恢复基线注释。

### 12.4 coalesced load生命周期按Direct/Cached上移

#### 12.4.1 标准模型

Direct/Cached不让底层cache管理reader异步task:

```text
BufferedInput
  -> vector<shared_ptr<CoalescedLoad>>
  -> executor capture shared_ptr<CoalescedLoad>
  -> load对象持有source/stats/pool/request payload
  -> BufferedInput destructor/reset调用load->cancel()
```

FileCache使用同一模型:

```text
FileCacheBufferedInput
  -> vector<shared_ptr<FileCacheCoalescedLoad>>
  -> stream address -> shared load映射
  -> prefetch: executor主动loadOrFuture
  -> demand: 第一个相关FileCacheInputStream::Next懒loadOrFuture
```

删除core:

```text
FileCache::submitWarm
FileCache::inflightWarmForTest
warm_mutex_ / warm_cv_
warm_cancel_source_
accepting_warm_tasks_ / inflight_warm_
deactivateBackgroundOperations中的warm stop/cancel/wait
```

#### 12.4.2 `FileCacheCoalescedLoad`

本节是R2/C8最终权威方案。

职责边界:

```text
FileCacheBufferedInput
  负责:region/chunk规划、分组、prefetch/demand触发时机、stream->load映射

FileCacheCoalescedLoad
  负责:group生命周期、一次执行、request RAM buffer ownership、结果交付

FileCacheInputStream
  负责:get/getOrSet、segment split、downloader election/wait、回源、本地读、
       reserve/write、cache write错误策略

FileSegment/FileCache core
  负责:既有segment状态机和持久化合同;R2不修改
```

`FileCacheCoalescedLoad` 不直接调用 `ReadFileInputStream::read` 的vector overload，
不计算staging坐标，不抢downloader，不调用 `reserveAndWriteSegmentChunk`。这些行为若在
load层重复实现，会形成第二套 `FileCache` 状态机，是明确禁止项。

每个group拥有自己的request对象。request使用稳定的 `requestIndex` 标识，不能只以offset
取数据，因为重复region可以拥有相同offset。

```cpp
// Declared in FileCacheInputStream.h so producer and load share one lightweight
// ownership type without a circular include.
struct FileCachePreparedBuffer
{
    velox::BufferPtr data;
    velox::common::Region region;
};

// Declared in FileCacheInputStream.h and shared by business streams, internal
// coalesced streams and the load. Declaration order is intentional: members
// are destroyed in reverse, so source is destroyed before its raw stats
// pointees, and cache is destroyed last.
struct FileCacheReadContext
{
    FileCachePtr cache;
    std::shared_ptr<io::IoStatistics> ioStatistics;
    std::shared_ptr<velox::IoStats> ioStats;
    std::shared_ptr<dwio::common::ReadFileInputStream> source;
    std::shared_ptr<velox::memory::MemoryPool> pool;
    FileCacheKey key;
    FileCacheOriginInfo origin;
    FileCacheReadOptions cacheOptions;
    FileCacheRequestContext requestContext;
    QueryStatus queryStatus;
    std::shared_ptr<velox::cache::ScanTracker> tracker;
    velox::StringIdLease fileNum;
    velox::StringIdLease groupId;
    uint64_t fileSize;
};

struct FileCacheLoadRequest
{
    size_t requestIndex;
    size_t planChunkIndex;
    velox::common::Region region;
    velox::cache::TrackingId trackingId;
    std::vector<FileCachePreparedBuffer> buffers;
    bool ready{false};
    bool consumed{false};
};

class FileCacheCoalescedLoad final : public cache::CoalescedLoad
{
public:
    struct Context
    {
        std::shared_ptr<const FileCacheReadContext> readContext;
        // Destroyed before readContext and therefore before readContext->cache.
        FileCache::QueryContextHolderPtr queryContextHolder;
    };

    FileCacheCoalescedLoad(
        Context context,
        uint64_t groupOffset,
        uint64_t groupLength,
        std::vector<FileCacheLoadRequest> requests);

    std::vector<cache::CachePin> loadData(bool prefetch) override;
    bool isSsdLoad() const override { return false; }
    int64_t size() const override;

    std::optional<std::vector<FileCachePreparedBuffer>>
    getData(const std::vector<size_t> & requestIndices);

private:
    // Declaration order is intentional. requests_ and groupSegments_ are
    // destroyed before context_, so their buffers/segment refs cannot outlive
    // the MemoryPool/FileCache owners in readContext.
    Context context_;
    mutable std::mutex requestMutex_;
    FileSegmentsHolderPtr groupSegments_;
    std::vector<FileCacheLoadRequest> requests_;
};
```

每个buffer由 `FileCacheInputStream` 直接从source或本地cache读入。cold miss时同一块
buffer先写 `FileSegment`，然后ownership转给request；业务stream再从request取得
ownership。正常路径不得出现source buffer -> staging -> request buffer的额外copy。
重复region按 `DirectCoalescedLoad` 语义为每个request准备独立可消费buffer；这是唯一允许的
request间payload copy。

`FileCacheInputStream` 增加唯一的producer-side handoff:

```cpp
std::optional<FileCachePreparedBuffer> takeLastOutputBuffer();
```

内部stream每次成功 `Next` 后调用 `takeLastOutputBuffer`。它移动本次 `Next` 直接读入的
pool-backed `outputBuffer_`，并返回其绝对文件region；调用前必须满足该窗口已全部交给内部
consumer（内部stream不调用 `BackUp`），移动后下一次 `Next` 分配/复用新的最终buffer。
source/local reader均直接写入该buffer，cache miss时 `FileSegment::write` 从同一buffer读取，
不创建staging payload。

`getData` 在 `requestMutex_` 下执行全有或全无的handoff。与
`DirectCoalescedLoad::getData` 一样，该接口会move已准备的数据；这里只因一个stream可能
覆盖多个request而按 `requestIndices` 批量返回。基类mutex只保护
`CoalescedLoad` state，`loadData` 在该mutex外执行，不能拿它保护request payload:

这是相对Direct的**有意签名偏离**，不得在实现时误改回offset版本:

```cpp
// Direct:单个LoadRequest，Allocation/tinyData，按offset逐个取。
int32_t getData(
    int64_t offset,
    memory::Allocation & data,
    std::string & tinyData);

// FileCache:一个业务stream可跨多个plan chunk/group，按稳定request index批量取。
std::optional<std::vector<FileCachePreparedBuffer>>
getData(const std::vector<size_t> & requestIndices);
```

偏离原因:

```text
1. FileCacheInputStream最终输出所有权是BufferPtr，不是Allocation/tinyData。
2. 一个业务stream可对应同一load中的多个request，也可跨多个load。
3. duplicate region可能有相同offset；requestIndex不依赖“第几个未消费同offset项”的隐式顺序。
4. 同一binding必须先确认全部request ready，再一次性move；逐offset接口会暴露部分结果。
```

因此只复用Direct的 `getData` 命名、ownership move和一次性消费合同，不复制其参数/返回
类型。`bufferConsumed` 的等价状态是每个 `FileCacheLoadRequest::consumed`。

```text
全部requestIndices均ready且未消费:
  将各request的FileCachePreparedBuffer ownership move给调用方，并标记consumed。

任一request尚未ready、load被cancel/异常或任一request已消费:
  返回nullopt，不移动任何buffer；业务stream走原FileCacheInputStream demand路径。
```

`loadData` 先在load私有临时request结果中收集整个group；所有request完整后才持
`requestMutex_` 一次性移动结果并设置全部 `ready=true`。异常时不发布部分RAM payload；
已经写入的部分 `FileSegment` 由既有状态机保留，后续demand可以继续或命中。

group执行:

```text
1. FileCacheCoalescedLoad::loadData按group范围调用一次FileCache lookup。
   普通模式使用getOrSet;cache-only/bypass模式复用FileCacheInputStream的既有策略。
2. load对象持有group FileSegmentsHolder，直到全部内部stream取得自己的精确holder且
   loadData完成。不得在getOrSet后立即释放；load完成后可reset group holder，未请求gap的
   EMPTY segment随既有core语义清理，避免长期pin住整个bounding range。
3. 每个FileCacheLoadRequest创建独立的内部FileCacheInputStream。内部stream使用空
   TrackingId，避免把预取/准备字节误记为业务delivered bytes。
4. 内部stream再次按自己的精确region调用get/getOrSet，取得自己的holder；不得共享或
   pop group holder。
5. 循环调用内部stream的Next完成真实IO。每次Next产生的pool-backed output BufferPtr
   从内部stream转移到request，同时记录对应绝对文件Region。
6. 业务stream按requestIndex取得并安装这些buffers；当逻辑位置落在已准备Region内时
   直接返回RAM，离开已准备Region后继续原有FileCacheInputStream状态机。
7. 业务stream实际交付buffer时才调用ScanTracker::recordRead。物理source/cache read、
   cache write和latency继续由内部FileCacheInputStream记账。
```

`FileCacheInputStream` 为此只增加coalesced业务所需的buffer ownership handoff/install
能力，以及供异步内部stream安全持有的共享只读context；不得增加第二套segment状态判断。
这也是R2唯一允许的生产行为扩展。

group bounding range可能包含未请求gap。group lookup可以为其建立临时EMPTY segment，
但内部stream只读取request精确region，因此gap不回源、不进入RAM payload、不写
`FileSegment`。group holder释放后，无其他引用的EMPTY gap segment按既有core语义清理。

触发与交付:

```text
prefetch + executor:
  BufferedInput::load提交loadOrFuture(nullptr)，立即开始group执行

prefetch + null executor:
  保持kPlanned，由第一个相关业务stream的Next触发

demand:
  BufferedInput::load只建立kPlanned load
  第一个相关业务stream的Next调用loadOrFuture(&wait)
  同group其他stream等待或随后直接取得自己的request buffers
```

业务stream消费request buffers不会再次回源。request结果必须按 `requestIndex` 交付，不能
以offset作为唯一key。一个原始stream可能被拆到多个plan chunk/group，因此映射值必须是
load binding列表，而不是单个 `shared_ptr`。

异常、取消与生命周期:

```text
cancel-before-start:
  loadOrFuture看到kCancelled直接返回，不执行IO。

cancel-during-load:
  不强杀正在执行的loadData；异步任务持有完整context、pool、source、cache和holders，
  可以安全完成。cancel只改变状态并唤醒waiter，行为与Direct一致。

prefetch异常:
  CoalescedLoad转kCancelled；业务stream没有取得完整request payload时，丢弃该request
  payload并走普通FileCacheInputStream demand路径重试。

demand执行异常:
  直接传播给查询；FileCacheInputStream既有catch负责释放downloader。

cache reserve/write异常:
  完全复用FileCacheInputStream既有bypass/strict策略，load层不重新分类。
```

`ReadFileInputStream::read` 已保证source scalar read必须精确返回请求长度；短读会抛异常，
随后由 `FileCacheInputStream::Next` 的既有catch释放downloader。因此R2不增加source EOF
特殊处理。

异步context的声明/析构顺序必须保证 `ReadFileInputStream` 使用的
`IoStats`/`IoStatistics` owner及 `MemoryPool`、`FileCache`、query holder均活到任务与
request buffers之后。不得依赖已析构的 `FileCacheBufferedInput` raw pointer。

异常测试只能使用Velox现有fault-injection `ReadFile`、`TestValue`、受控executor、
cancellation、MemoryPool/FileCache失败机制；禁止为测试增加生产分支、callback、错误开关
或额外test-only接口。

`loadData` 返回空 `CachePin` vector，与 `DirectCoalescedLoad` 一致；数据在request RAM
buffers和CH `FileSegment` 中，不创建 `AsyncDataCache` entry。

##### 12.4.2.1 已废止的直接preadv/staging方案(仅保留review历史)

以下内容来自
`5531f6b222d`、`0fa7dc178b0`、`fa5aabfa4cd`、`f4cb9f3d84f`、
`51d0685e090`，已被本节前述最终方案整体替代。其 `MaterializeSlice`、直接vector read、
上层downloader election和上层写 `FileSegment` 均不得实施。

<!--
The obsolete design is intentionally kept only as source-level review history.
It is hidden from rendered documentation and must not be used for implementation.

**Files:**

```text
Create: velox/ch/Disks/IO/FileCacheCoalescedLoad.h
Create: velox/ch/Disks/IO/FileCacheCoalescedLoad.cpp
Modify: velox/ch/Disks/IO/CMakeLists.txt
```

接口:

```cpp
class FileCacheCoalescedLoad final : public cache::CoalescedLoad
{
public:
    struct RequestRange
    {
        uint64_t offset;
        uint64_t length;
    };

    struct Context
    {
        // Declaration order = construction order; reverse destruction order.
        // `source` (base ReadFileInputStream) holds raw pointers to the
        // IoStats/IoStatistics objects, so those shared owners MUST be declared
        // BEFORE `source` -> they are destroyed AFTER `source`, keeping the raw
        // pointers valid for its whole lifetime (same C2/11.3.1 UAF contract).
        FileCachePtr cache;
        FileCache::QueryContextHolderPtr queryContextHolder;
        std::shared_ptr<io::IoStatistics> ioStatistics;
        std::shared_ptr<velox::IoStats> ioStats;
        std::shared_ptr<dwio::common::ReadFileInputStream> source;
        std::shared_ptr<velox::memory::MemoryPool> pool;
        FileCacheKey key;
        FileCacheOriginInfo origin;
        FileCacheReadOptions cacheOptions;
        QueryStatus queryStatus;
        folly::CancellationToken generationToken;
        std::string queryId;
        uint64_t fileSize;
    };

    FileCacheCoalescedLoad(
        Context context,
        uint64_t groupOffset,
        uint64_t groupLength,
        std::vector<RequestRange> ranges);

    std::vector<cache::CachePin> loadData(bool prefetch) override;
    bool isSsdLoad() const override { return false; }
    int64_t size() const override;
};
```

成员声明顺序保证reverse destruction:

```text
cache
queryContextHolder
ioStatistics
ioStats
source
pool
```

因此 `pool/source/ioStats/ioStatistics/queryContextHolder` 均在 `cache` 前析构。

`loadData` 执行现有 `warmSourceGroup` 的source read + FileSegment fill主体,但:

- 在每个segment/chunk边界检查:
  ```cpp
  state() == State::kCancelled || context_.queryStatus.isCancelled()
      || context_.generationToken.isCancellationRequested()
  ```
- 使用上层 `getOrSetDownloader + SCOPE_EXIT`,不改 `FileSegment`;
- demand异常通过 `loadOrFuture` 传播;
- prefetch异常记录后结束best-effort load;
- 返回空 `CachePin` vector,因为数据落入CH `FileSegment`,不创建
  `AsyncDataCache` entry;
- load对象自身持有完整payload,不再需要 `FileCache` inflight counter。

###### 12.4.2.1.1 当时的Velox标准模型接口调研(已废止)

动手前只读调研 `filecache2` 上 Direct/Cached 的 CoalescedLoad 实现(file:line 精确),
确认 `FileCacheCoalescedLoad` 完全照搬标准模型、不发明新机制。以下为待 review 的接口
锚点,尚未写代码。

**基类 `cache::CoalescedLoad`(`velox/common/caching/AsyncDataCache.h:477-539`)**:

```text
State { kPlanned, kLoading, kCancelled, kLoaded }                     (:480)
bool loadOrFuture(folly::SemiFuture<bool>* wait, bool ssdSavable=true) (:495)
State state() const                                                   (:497)
void cancel() { setEndState(kCancelled); }                            (:502)
virtual int64_t size() const = 0                                      (:507)
virtual bool isSsdLoad() const = 0                                    (:515)
virtual std::vector<CachePin> loadData(bool prefetch) = 0  (protected) (:524)
```

`loadOrFuture` 去重/demand-wait 语义(`AsyncDataCache.cpp:390-436`,已核实):

```text
kCancelled/kLoaded          -> return true(短路)
kLoading + wait==nullptr    -> return false(prefetch 不等)
kLoading + wait!=nullptr    -> *wait = promise 的 SemiFuture; return false(demand 等)
kPlanned                    -> 唯一线程转 kLoading,锁外调 loadData,完成 setEndState(kLoaded)
                               唤醒 waiters
```

即 **prefetch 与 demand 的去重、单次加载、demand 阻塞唤醒全部由基类保证**,
`FileCacheCoalescedLoad` 只实现 `loadData`。

**空 CachePin 合法**:`DirectCoalescedLoad::loadData` 返回 `{}`
(`DirectBufferedInput.h:91`,数据经 `getData` 取,不进 AsyncDataCache);
`DwioCoalescedLoad`/`SsdLoad` 也有 `if (pins.empty()) return pins;`
(`CachedBufferedInput.cpp:460/516`)。故 `FileCacheCoalescedLoad` 数据落 CH
`FileSegment`、`loadData` 返回 `{}` 合法。

**照搬的机制(Direct 为主参照)**:

| 机制 | Velox 标准 file:line | FileCache 照搬 |
|---|---|---|
| load 构造无 key | `CoalescedLoad({}, {})` (`DirectBufferedInput.h:63`) | 同,不建 cache entry |
| stream->load 映射 | `folly::Synchronized<F14FastMap<const SeekableInputStream*, shared_ptr<...>>>` (`DirectBufferedInput.h:318`) | 同 |
| prefetch 提交 | `executor_->add([load=by-value]{ load->loadOrFuture(nullptr); })` (`DirectBufferedInput.cpp:227-239`) | 同(shared_ptr by value 保活) |
| demand wait | `if(!load->loadOrFuture(&wait)) wait.wait();` (`DirectInputStream.cpp:180-182`) | 同 |
| 保活+析构序 | `AsyncLoadHolder{load,pool}` dtor load 先于 pool (`DirectBufferedInput.h:301-315`) | Context 成员序:cache 最后析构 |
| cancel | reset/dtor 遍历 `load->cancel()` (`DirectBufferedInput.cpp:256-262`) | 同 |
| 一次性 handoff | `coalescedLoad(stream)` move+erase (`DirectBufferedInput.cpp:242-254`) | 同 |

**`loadData` 主体 —— 真实 coalesced source IO(vector read + nullptr gap,reviewer Blocker 修正)**:
`cache::CoalescedLoad` 基类**只负责去重和同步,不合并 IO**。且**不得**按 group
bounding range `[groupOffset, groupOffset+groupLength)` 分配一块连续 staging buffer:
group 限制的是 **requested payload 总量**(`maxCoalesceBytes`),**不含 range 之间的
gap**。反例:100 个小 range 各间隔 1 MiB,payload 很小但 bounding range ≈ 100 MiB,
连续 buffer 会分配 ~100 MiB 并读入所有 gap,导致 OOM 且违背 exact-range 设计。

必须照 Direct 的 **vector read + nullptr gap buffer**(`ReadFileInputStream::read(
const vector<folly::Range<char*>>&, offset, LogType)`):

**坐标分离(reviewer Blocker):文件坐标 ≠ 紧凑内存坐标。** gap 不分配后 staging
是**紧凑**的(只含 materialized bytes),不能用文件坐标 `interval.start -
sourceReadStart` 索引紧凑 buffer。反例:interval A `[0,100)`、B `[1000,1100)`,
staging 实际只分配 200 bytes,B 若按文件坐标 `1000` 索引就越界。每个 interval 保存
**独立的紧凑 staging 位置**:

```cpp
struct MaterializeSlice
{
    uint64_t fileOffset;     // 该 slice 在源文件中的绝对起点
    uint64_t length;         // materialized 长度
    uint64_t stagingOffset;  // 在紧凑 staging 中的偏移(累加各 slice length)
};
```

上例 B 的 `stagingOffset` = 100(A 的 length),不是 1000。

```text
1. 对每个 segment 计算 materialize interval [currentWriteOffset,
   lastRequestedEndInSegment)(含顺序写必需 prefix,不含无关尾部)。
   注意 boundaryAlignment 左扩:interval 可能从 groupOffset 之前开始
   (requested [100,200)、aligned segment [0,4096)、currentWriteOffset=0 ->
   interval [0,200))。
2. sourceReadStart = min(所有 interval 的 fileOffset)。按 fileOffset 升序,
   逐 slice 累加 length 得 stagingOffset;slice 之间的文件 gap 记为
   folly::Range<char *>(nullptr, gapLength)(不分配、不读),不占 stagingOffset。
3. 仅为 slice 分配紧凑 pool-backed buffer(总大小 = Σ slice.length);vector
   buffers 按 fileOffset 顺序 = [slice0 buf, gap(nullptr), slice1 buf, ...]。
4. 一次调用:
     source->read(buffers, sourceReadStart, LogType::FILE);   // vector overload
5. 写回:对每个 slice 对应的 FileSegment,
     src = staging + slice.stagingOffset + (currentWriteOffset - slice.fileOffset)
   (并发已推进 currentWriteOffset 时跳过已完成 prefix),再
   reserveAndWriteSegmentChunk(上层 getOrSetDownloader + SCOPE_EXIT,不改
   FileSegment;已下载段跳过)。若实现用多个 pool buffer,则直接保存
   (buffer, offset) 映射,不做 absolute-offset 指针运算。
6. 统计区分:
     requested union   -> useful prefetch
     mandatory prefix  -> raw overread
     nullptr gaps      -> readGap(planning 阶段记一次,不分配、不写 cache)
```

**vector-read 统计(reviewer Major,含 source bytes)**:vector
`ReadFileInputStream::read` **不自动增加** `rawBytesRead` / `incTotalScanTimeNs`
(C2 自动记账只对 scalar overload 成立)。`loadData` 包 timer 手工记:

```text
source read 成功后立即记:
  IoStatistics::read          += 非 nullptr buffers 总长度
  rawBytesRead                += 非 nullptr buffers 总长度
  ProfileEvents source bytes  += 同一长度
  totalScanTimeNs / read latency / storage latency += elapsed
成功写入 FileSegment 后才记:
  requested union   -> prefetch(useful)
  mandatory prefix  -> raw overread
readGap 只在 planning 记一次,不在 loadData 重复统计。
```

目标:两个相邻 range 只产生**一次** `preadv`,且 staging allocation **不超过
materialized bytes**(不含 gap)。若 FileSegment 顺序写约束使得无法一次 `preadv`,
代码与指标必须明确它只是 warm task grouping、**未**提供 source IO coalescing。

**cancel 语义(reviewer Major,完整)**:`CoalescedLoad::cancel()` 只设
`kCancelled` 状态并立即唤醒 waiter,**不会中断正在运行的 `loadData`**;且 `loadData`
正常跑完后基类仍会 `setEndState(kLoaded)`。因此:

```text
cancel-before-start:loadOrFuture 见 kCancelled 直接 return true,loadData 不执行,零 IO。
cancel-during-load :仅靠 loadData 内每个 chunk/segment checkpoint 协作停止
  (查 queryStatus / generationToken);基类不打断。
waiter 可能在 loadData 完全退出前被唤醒(cancel 立即 setValue)。
最终 state()==kLoaded 不能作为"已停止/已完成有效数据"的证明。
shutdown 仍必须 drain executor(cancel 不等于 task 结束);manager/cache/executor
  由集成层保证晚于 load(12.4.4)。
demand waiter 被 cancel 唤醒后须能安全走 FileSegment 状态机(段可能只部分填充或未填,
  按正常 miss/downloading 处理)。
```

上层 election 用 `getOrSetDownloader + SCOPE_EXIT`,**不改 `FileSegment`**;prefetch
异常记录后结束 best-effort、demand 异常经 `loadOrFuture` 传播;`loadData` 返回 `{}`。

**必需测试(含 reviewer 补充)**:

```text
两个相邻 range 只调用一次 preadv。
大 gap group 的 staging allocation 不超过 materialized bytes(不含 gap)。
gap 保持 nullptr 且不写入 FileSegment。
partial segment 推进后正确跳过 staging prefix。
request 从 unaligned offset 开始 + segment 因 boundaryAlignment 向左扩展:
  验证 mandatory prefix 来自 source 正确位置(sourceReadStart,非 groupOffset)、
  warm 后 cache 内容逐字节正确。
vector-read 后 IoStatistics::read / rawBytesRead / ProfileEvents source bytes
  == 非 nullptr buffers 总长度(手工记账);断言 totalScanTimeNs 非零时,test
  ReadFile 须用 Baton 确定性阻塞一次,否则只验 raw/source bytes 不断言时间值
  (极快本地读可能 elapsed==0)。
cancel-before-start 不产生 IO。
cancel-during-load 在下一 checkpoint 停止写 segment。
demand waiter 被 cancel 唤醒后能安全走 FileSegment 状态机。
owner 析构顺序用 weak owner 验证(source 先于 ioStats/ioStatistics 析构)。
```

**结论**:方案照搬标准 `CoalescedLoad` 的去重/同步/生命周期,不新增 Velox 或 CH
抽象;真实 IO 合并由 `loadData` 两阶段实现(基类不提供),cancel 为协作式。满足第
12 节"IO 功能上移到标准模型、不下沉改 CH 核心"的约束。待 review 后进入 R2 实施。

-->

#### 12.4.3 `FileCacheBufferedInput`集成

**Files:**

```text
Modify: velox/ch/Disks/IO/FileCacheBufferedInput.h
Modify: velox/ch/Disks/IO/FileCacheBufferedInput.cpp
Modify: velox/ch/Disks/IO/FileCacheInputStream.h
Modify: velox/ch/Disks/IO/FileCacheInputStream.cpp
```

`FileCacheBufferedInput` 在构造时建立一个 `shared_ptr<const FileCacheReadContext>`，业务stream、
内部coalesced stream及load均复制该shared pointer。与 `DirectInputStream` 一致，业务stream
还保留non-owning `FileCacheBufferedInput * bufferedInput_`，仅用于首次 `Next` 取得planner生成的load
bindings；所有cache/source/pool/options/stats访问均来自shared context。

两个角色使用显式构造入口:

```cpp
// `enqueue` 返回的业务stream。owner必须outlive该stream，与DirectInputStream合同一致。
FileCacheInputStream(
    FileCacheBufferedInput * bufferedInput,
    std::shared_ptr<const FileCacheReadContext> context,
    velox::common::Region region,
    dwio::common::LogType logType,
    velox::cache::TrackingId trackingId = {});

// 仅供FileCacheCoalescedLoad::loadData使用的内部IO stream。
// 不持有bufferedInput，不查询load bindings，可以晚于FileCacheBufferedInput完成。
static std::unique_ptr<FileCacheInputStream> createCoalescedInternal(
    std::shared_ptr<const FileCacheReadContext> context,
    velox::common::Region region,
    dwio::common::LogType logType);
```

业务stream的 `bufferedInput_` 不参与IO资源生命周期，也不被后台task捕获；它只执行
`bufferedInput_->coalescedLoads(this)`，对齐 `DirectInputStream::loadPosition` 调
`bufferedInput_->coalescedLoad(this)` 的现有模型。内部stream没有 `bufferedInput_`，因此running
async load可以晚于input安全完成。每个stream仍在构造时用context中的request信息独立取得
自己的 `QueryContextHolder`。

`Request` 增加non-owning stream identity和稳定request index:

```cpp
dwio::common::SeekableInputStream * stream;
size_t requestIndex;
```

只将地址作为map key，从不解引用。load内部不持有业务stream raw pointer；discarded
stream不会造成UAF。

新增:

```cpp
std::vector<std::shared_ptr<FileCacheCoalescedLoad>> coalescedLoads_;

struct LoadBinding
{
    std::shared_ptr<FileCacheCoalescedLoad> load;
    std::vector<size_t> requestIndices;
};

folly::Synchronized<
    folly::F14FastMap<
        const dwio::common::SeekableInputStream *,
        std::vector<LoadBinding>>>
    streamToCoalescedLoads_;

std::vector<LoadBinding>
coalescedLoads(const dwio::common::SeekableInputStream * stream);
```

一个stream可以跨多个plan chunk/group，因此不能照搬Direct的单
stream -> 单load map。`coalescedLoads` 与Direct的 `coalescedLoad` 一样对当前stream
一次性move+erase；复数只表示返回全部bindings。
每个shared load仍可由同group其他stream持有。

析构/reset对齐Direct/Cached:

```cpp
for (auto & load : coalescedLoads_)
    load->cancel();
```

reset同时清vector/map/request/plan/group。取消只作用于load状态，不再使用旧
`inputGeneration_` 中断已经运行的load。

prefetch group:

```cpp
executor_->add([pendingLoad = load]()
{
    pendingLoad->loadOrFuture(nullptr);
});
```

demand stream首次 `Next`:

```cpp
for (auto & binding : bufferedInput_->coalescedLoads(this))
{
    folly::SemiFuture<bool> wait(false);
    if (!binding.load->loadOrFuture(&wait))
        wait.wait();

    if (auto data = binding.load->getData(binding.requestIndices))
        installCoalescedBuffers(std::move(*data));
    // nullopt: leave no prepared window installed; continue the ordinary
    // FileCacheInputStream demand path below.
}
```

以上代码只在业务stream角色执行。`createCoalescedInternal` 创建的内部stream跳过bindings
lookup，只运行原有 `FileCacheInputStream` IO状态机并由load调用 `takeLastOutputBuffer`。

`installCoalescedBuffers` 按绝对文件offset排序。`Next` 优先交付覆盖当前位置的RAM
buffer；没有完整payload、当前位置不在prepared region或prepared region消费完毕时，
继续原有 `FileCacheInputStream` state machine。不得为了命中prepared data跳过业务
delivered-byte统计，也不得重复统计内部stream已经记录的物理IO。

#### 12.4.4 上层生命周期合同

与Cached的 `AsyncDataCache` 一样,manager/cache/executor由集成层保证晚于load:

```text
停止创建query/input
销毁FileCacheBufferedInput并cancel planned loads
drain connector IO executor
FileCacheManager::shutdown
```

已经进入 `loadData` 的任务不被析构强行中断；它通过shared context保活source、
`IoStats`、`IoStatistics`、pool、query holder、cache和group holder，完成后自行释放。
planned load被cancel后即使executor稍后调用 `loadOrFuture` 也不会发起IO。

业务stream生命周期继续采用Velox现有合同:

```text
FileCacheBufferedInput outlives由它enqueue返回的业务FileCacheInputStream
```

这与 `DirectBufferedInput` / `DirectInputStream` 的non-owning back-pointer一致。只有
coalesced内部IO stream允许晚于input；它不含owner back-pointer。

`FileCacheBufferedInputBuilder.h` 明确:

```text
FileCacheManager和传入executor必须outlive所有builder创建的BufferedInput及其
FileCacheCoalescedLoad。
```

benchmark/Gluten接入点按该顺序关闭。不得通过修改 `FileCacheManager` 增加hook。

### 12.5 wait测试不增加生产hook

恢复 `FileSegment.cpp` 基线,删除:

```cpp
TestValue::adjust(
    "facebook::velox::ch::FileSegment::wait::beforeWait", this);
```

不得在 `FileCacheInputStream` 或其他生产文件补替代hook。并发case使用Velox现有
`CoalescedLoad::loadOrFuture` `TestValue`、测试 `ReadFile`/executor中的Baton和既有
`FileSegment` 状态观测建立确定性顺序:

```text
测试source在已有test double中阻塞A的物理read
-> 确认A已经进入group load
-> 通过CoalescedLoad既有loadOrFuture hook/状态让B进入wait路径
-> 释放A
-> 验证A/B完成、source读取未重复、无downloader/waiter遗留
```

若现有机制无法证明某个更细的内部瞬间，不得为该断言修改产品代码；改为验证可观察的
并发合同。禁止sleep和概率性时序断言。

### 12.6 core测试保留与迁移

#### 12.6.1 恢复基线case

恢复 `5785a43a` 的所有core tests,包括:

```text
PartialPhysicalAppendFailureReconcilesDownloadedToPhysical
```

它继续使用基线factory test seam;生产不调用factory setter。

删除依赖已移除warm core API的:

```text
DeactivateWaitsForInflightWarm
SubmitWarmRejectedAfterDeactivate
DeactivateWaitsForWarmPayloadDestruction
```

对应合同在 `FileCacheCoalescedLoad` tests重建。

#### 12.6.2 writer tests迁到IO层

从 `FileSegmentTest.cpp` 移到 `velox/ch/IO/tests/IoAdaptersTest.cpp`:

```text
AppendOrFlushAfterCloseIsNonErrnoLogicError
NormalAppendResumeAndSize
```

它们只测试 `FileCacheLocalWriteFile`,不属于FileSegment。

#### 12.6.3 errno policy tests迁到Disks/IO

将:

```text
CacheWriteErrorPolicyTest.ClassifiesByErrnoAndSkipFlag
```

移到 `FileCacheBufferedInputTest.cpp`,因为
`classifyCacheWriteError` 位于 `FileCacheInputStream`。

#### 12.6.4 reserve hint tests分层

core保留 `ReserveHintCapsReserveAheadToReadHorizon`,但直接调用:

```cpp
segment.reserve(
    chunk,
    /*lockWaitMs=*/100,
    reason,
    /*reserveStat=*/nullptr,
    /*reserveHint=*/horizon);
segment.write(data, chunk, segment.getCurrentWriteOffset());
```

不得从core test调用 `reserveAndWriteSegmentChunk`。

`Disks/IO/tests` 另保留integration case,验证demand/predownload/warm/preload实际传递
正确hint。

#### 12.6.5 test CMake

core tests不再使用 `FaultyWriteFile` 时删除:

```cmake
velox_file_test_utils
```

IO和Disks/IO测试目标按迁移后的case补精确依赖。

#### 12.6.6 R2/C8完整测试矩阵

测试必须同时验证触发时机、source物理读取次数、RAM buffer交付、业务数据和
`FileSegment` 最终状态。只验证“磁盘segment最终有数据”会漏掉预取buffer被丢弃、业务
`Next` 再次回源的错误实现。

正常路径:

```text
cold prefetch:
  load后executor立即执行;业务Next前request RAM buffers和segments均ready;
  业务Next不增加source read count。

cold demand:
  load后source read count==0且load为kPlanned;
  A首次Next触发整个group;B随后Next不再次回源。

null executor:
  prefetch load保持kPlanned，由第一个相关Next触发。

cache状态:
  全hit、全miss、hit/miss混合、已有downloader、partial continuation。

region形状:
  相邻、带gap、重叠、重复offset、非零offset、跨segment/loadQuantum、尾部短块。

并发:
  A/B并发Next只执行一次group load;waiter唤醒后各自取得正确request buffer。

ownership:
  FileCacheInputStream output BufferPtr与request接收、业务stream交付为同一allocation;
  正常request路径无额外payload copy。

lifecycle:
  clone、reset、planned load析构取消、running load在input析构后安全完成、
  stream未消费便销毁。

格式:
  DWRF和Parquet真实clone/enqueue/load/Next cold-fill/warm-hit E2E。

统计:
  physical source/cache bytes、prefetch bytes、delivered bytes、cache-write bytes和
  latency不漏记、不重复。
```

异常路径:

```text
prefetch source失败:
  load转kCancelled;业务Next走普通demand重试;无downloader/waiter泄漏。

demand source失败:
  异常传播给查询;同group waiter被唤醒;无downloader泄漏。

cache资源/写入:
  getOrSet/QueryLimit失败、reserve失败、ENOSPC/EDQUOT、skip=true和strict写失败。
  各自严格复用现有FileCacheInputStream合同。

执行资源:
  RAM allocation失败、executor提交失败。

竞争:
  query cancellation、reset、析构与planned/running load竞争。

部分结果:
  任一request未完整准备时不得发布success-shaped payload；已准备buffer按异常合同释放，
  后续只能完整fallback或传播异常。
```

异常case不得为生产代码增加hook、callback、错误开关或测试分支。只能使用Velox当前已有的
fault-injection `ReadFile`、`TestValue`、受控executor、cancellation、MemoryPool和
`FileCache` 失败机制。已有 `FileCacheInputStream` 单层异常case不重复；新增case只覆盖
`CoalescedLoad` 组合后的状态、重试、waiter和buffer交付。

旧warm-only测试按新合同改写:

```text
PrefetchWarmDownloadsSegmentsAsync
  -> prefetch同时准备RAM request buffers和FileSegment

DemandGroupIsNotWarmed
  -> demand在首次Next启动整个group

PlainDestructorDoesNotCancelWarm
  -> 析构取消planned load，但running load由shared context安全完成
```

### 12.7 实施commit顺序

#### Commit R1: 测试归位,不改生产行为

**Files:**

```text
Modify: velox/ch/IO/tests/IoAdaptersTest.cpp
Modify: velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp
Modify: velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp
Modify: velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
```

- [ ] 移动writer和errno policy cases。
- [ ] 将reserve hint core case改为直接 `reserve` + `write`。
- [ ] 删除不再需要的test utility link。
- [ ] 构建:

```bash
ninja -C cmake-build-debug-gcc13 \
  velox_ch_io_test \
  velox_ch_filecache_buffered_input_test \
  velox_ch_filecache_core_scc_test \
  > cmake-build-debug-gcc13/build_r1_test_relocation.log 2>&1
```

- [ ] 分别运行三个binary,输出写入:

```text
cmake-build-debug-gcc13/test_r1_io.log
cmake-build-debug-gcc13/test_r1_buffered_input.log
cmake-build-debug-gcc13/test_r1_core_scc.log
```

确认case迁移后总覆盖不减少。baseline partial-write case在R4随factory API一起恢复。
- [ ] commit:

```bash
git commit -m "FileCache: relocate IO integration tests"
```

#### Commit R2: C8 `FileCacheCoalescedLoad`

**Files:**

```text
Create: velox/ch/Disks/IO/FileCacheCoalescedLoad.h
Create: velox/ch/Disks/IO/FileCacheCoalescedLoad.cpp
Modify: velox/ch/Disks/IO/FileCacheBufferedInput.{h,cpp}
Modify: velox/ch/Disks/IO/FileCacheInputStream.{h,cpp}
Modify: velox/ch/Disks/IO/CMakeLists.txt
Modify: velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp
Modify: velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp
```

- [ ] 写demand two-stream shared-load RED test。
- [ ] 写prefetch executor RED test。
- [ ] 写prefetch/demand RAM buffer handoff及业务 `Next` 不二次回源RED test。
- [ ] 写discarded stream、planned cancel、running load保活RED tests。
- [ ] 写正常/异常矩阵中只属于coalesced组合层的RED tests；异常注入不改生产代码。
- [ ] 实现load对象、stream->load bindings和prefetch/demand执行。
- [ ] 实现内部 `FileCacheInputStream` output `BufferPtr` ownership转移及业务stream安装。
- [ ] 删除 `warmSourceGroup` 独立task submission。
- [ ] 验证group只执行一次、组内后续业务stream不再次回源。
- [ ] 验证gap不回源、不进入RAM payload、不写 `FileSegment`。
- [ ] 构建并运行:

```bash
ninja -C cmake-build-debug-gcc13 \
  velox_ch_filecache_buffered_input_test \
  velox_ch_filecache_connector_test \
  velox_ch_cancellation_test \
  > cmake-build-debug-gcc13/build_r2_coalesced_load.log 2>&1
```

测试输出分别写入唯一的 `cmake-build-debug-gcc13/test_r2_*.log`。
- [ ] commit:

```bash
git commit -m "FileCache: execute prefetch through coalesced loads"
```

#### Commit R3: wait并发测试改用现有Velox机制

**Files:**

```text
Modify: velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp
Modify: velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp
```

- [ ] 删除对 `FileSegment::wait` 新hook的依赖。
- [ ] 使用既有 `CoalescedLoad::loadOrFuture` `TestValue` 和测试double Baton确定时序。
- [ ] 验证A/B完成、无重复source read、无downloader/waiter遗留。
- [ ] 确认本commit只改tests，不改生产文件。
- [ ] 运行:

```bash
cmake-build-debug-gcc13/velox/ch/Disks/IO/tests/velox_ch_filecache_connector_test \
  --gtest_filter='*Coalesced*Wait*:*Concurrent*Demand*' \
  > cmake-build-debug-gcc13/test_r3_coalesced_wait.log 2>&1
```

- [ ] commit:

```bash
git commit -m "FileCache: make coalesced wait tests deterministic"
```

#### Commit R4: FileCache core恢复基线

**Files:**

```text
Modify: velox/ch/Interpreters/FileCache/FileCache.h
Modify: velox/ch/Interpreters/FileCache/FileCache.cpp
Modify: velox/ch/Interpreters/FileCache/FileSegment.h
Modify: velox/ch/Interpreters/FileCache/FileSegment.cpp
Modify: velox/ch/Interpreters/FileCache/FileCacheErrnoException.h
Modify: velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp
```

- [ ] 逐文件恢复 `5785a43a` 内容。
- [ ] `FileSegment.cpp`重新应用typed writer include和default factory一行替换。
- [ ] `FileSegmentTest.cpp`恢复baseline
  `PartialPhysicalAppendFailureReconcilesDownloadedToPhysical`。
- [ ] 删除三个warm core tests。
- [ ] 确认Disks/IO无 `submitWarm` / `inflightWarmForTest` caller。
- [ ] 构建并运行core SCC及全部Disks/IO targets,确认core API恢复没有断链。
- [ ] commit:

```bash
git commit -m "FileCache: restore core after IO integration"
```

#### Commit R5: 剩余C3/C4/C6/C7修复

全部限制在:

```text
velox/ch/Disks/IO/**
velox/ch/IO/**
```

- [ ] C3补demand/predownload latency count。
- [ ] C4清理reader/segment offset双重断言并固定pre-decrement hint注释。
- [ ] C6删除 `PreloadedRunStream`,统一 `FileCacheInputStream`。
- [ ] C7按CH state语义修正mixed-state classifier。
- [ ] commit按独立RED/GREEN拆分,不amend。

### 12.8 最终验收

生产core diff:

```bash
git diff 5785a43a..HEAD -- \
  velox/ch/Interpreters/FileCache/FileCache.h \
  velox/ch/Interpreters/FileCache/FileCache.cpp \
  velox/ch/Interpreters/FileCache/FileSegment.h \
  velox/ch/Interpreters/FileCache/FileCacheErrnoException.h
```

预期:空。

```bash
git diff 5785a43a..HEAD -- \
  velox/ch/Interpreters/FileCache/FileSegment.cpp
```

预期仅:

```text
include FileCacheLocalWriteFile.h
default factory LocalWriteFile -> FileCacheLocalWriteFile
```

禁止残留:

```text
submitWarm
inflightWarmForTest
warm_mutex / warm_cv / warm_cancel_source
FileSegment::wait TestValue hook
FileCacheInputStream新增test-only TestValue hook
DownloaderLease
生产调用setWriteFileFactoryForTesting
```

功能gate:

```text
typed errno producer/consumer可达
prefetch/demand共享FileCacheCoalescedLoad
prefetch立即执行,demand首次Next执行
request RAM buffer由load保留并按requestIndex交付
同一BufferPtr完成读入、cache write和业务handoff
业务stream消费prepared buffer不再次回源
input析构/reset取消planned loads,running load由shared context保活
discarded stream合同保持
wait并发测试确定性覆盖
reserve hint / actual-vs-delivered / FileIoContext统计继续通过
DWRF + Parquet真实format E2E cold fill/warm hit
```

R2生产边界gate:

```text
允许的新增行为只有FileCache coalesced业务接入:
  FileCacheBufferedInput planning/trigger/mapping
  FileCacheCoalescedLoad request ownership
  FileCacheInputStream buffer handoff/install及shared read context

异常测试不得引入任何生产hook或失败开关。
FileCache/FileSegment core除12.2批准的typed default writer外保持基线。
```

生命周期gate:

```text
所有FileCacheBufferedInput销毁
-> connector IO executor drain
-> FileCacheManager::shutdown
```

Gluten在filecache2最终gate通过后一次性cherry-pick并解决历史errno分叉,不创建中间
stacked状态。

#### 11.1.1 硬约束:Velox IO集成不得修改CH FileCache核心

本轮Velox IO集成的生产改动边界固定为:

```text
允许:
  velox/ch/Disks/IO/**
  velox/ch/IO/**
  对应tests/benchmarks/CMake

禁止:
  velox/ch/Interpreters/FileCache/** 生产代码
```

即后续C3/C4修复、C6、C7修复和C8不得在CH `FileCache` port中新增或修改:

```text
class / struct
public API
state
lifecycle protocol
error policy
method signature / behavior
```

所需适配必须留在 `FileCacheBufferedInput`、`FileCacheInputStream` 和Velox IO adapter
层。不能因为去重、测试便利或RAII封装将Velox integration抽象下沉到
`Interpreters/FileCache`。

例外边界仅有:

1. `velox/ch/Interpreters/FileCache/tests/**` 可修改,用于验证未改变的CH核心合同;
2. C1 `1b92545ef` 已批准的还原性清理:删除本次历史中为测试引入的global writer
   factory,恢复 `FileSegment` 直接创建 `FileCacheLocalWriteFile`。它只撤销非CH
   test seam,不得借机新增FileCache行为。

C5 `8cdeb7169` 在 `FileSegment` 新增 `DownloaderLease` 违反本约束,必须用新commit
完整revert。warm/preload恢复Velox集成层原有 `getOrSetDownloader + SCOPE_EXIT`。

最终diff gate:

```text
除C1还原性删除和FileCache tests外,
本轮最终commit range对velox/ch/Interpreters/FileCache生产代码必须为零diff。
```

### 11.2 `506751cc5` 单独验收

`506751cc5` 已从 `FileCacheLocalWriteFile` 删除:

```text
SyscallFault
setSyscallFaultForTesting
setWriteCapForTesting
closeCallCountForTesting
thread_local fault/write-cap状态
```

当前writer生产实现:

- positive short write循环续写剩余suffix;
- `EINTR` 重试;
- 真实write失败立即保存errno并抛 `FileCacheErrnoException`;
- `close` 成功/失败均清 `fd_` 和 `closed_`,析构不重试已释放fd;
- `append`/`flush` after close抛非errno逻辑异常。

这些行为继续逐行对齐CH `WriteBufferFromFileDescriptor::nextImpl` 和
`WriteBufferFromFile::close`。用户明确选择:

```text
不为positive short write / close syscall failure恢复生产test seam。
syscall级行为由CH逐行对齐和code review保证。
```

该决定继续有效。

### 11.3 Velox source IO context没有真正接通

#### 问题

Velox标准读链路为:

```text
fileReadOps / IoStats / cacheable
-> ReadFileInputStream::fileIoContext_
-> ReadFile::pread(..., fileIoContext_)
```

`FileCacheBufferedInputBuilder` 接收并传入 `fileReadOps`、`IoStats` 和
`cacheable`,但实际source读取绕过了基类已经构造的 `ReadFileInputStream`:

```text
preload:
  sourceReadFile_->pread(...)

demand/warm:
  ReadBufferFromVeloxReadFile::readInto
  -> readFile_->pread(...),使用默认空FileIoContext
```

结果:

- `ioStats_` 除构造/clone外未实际使用;
- 每次read的 `fileReadOps` 未传给storage;
- `cacheable` 未传;
- storage-specific file IO统计、per-table access token identity和read options可能丢失。

`io::IoStatistics` 的手工read/raw计数不能替代 `FileIoContext`。

#### 已批准方案

不在FileCache中复制第二份 `FileIoContext`。保留
`ReadBufferFromVeloxReadFile` 的CH cursor/buffer兼容职责,但将source IO委托回
基类的 `ReadFileInputStream`:

```text
ReadBufferFromVeloxReadFile
  cursor / working buffer / read-until / handoff
        |
        v
ReadFileInputStream::read
        |
        v
ReadFile::pread(..., FileIoContext)
```

具体边界:

- `ReadBufferFromVeloxReadFile` 新增接受
  `std::shared_ptr<dwio::common::ReadFileInputStream>` 的source构造;
- source `readInto` 调用 `ReadFileInputStream::read`;
- 本地FileSegment cache reader仍使用现有 `ReadFile` 构造,不携带remote source
  context;
- warm task捕获共享的source `ReadFileInputStream`,不再只捕获裸
  `sourceReadFile_`;
- preload使用同一个source `ReadFileInputStream`;
- clone共享同一个source input/context,不得重建一个丢失 `fileReadOps` 的wrapper;
- 切换后删除重复raw bytes/scan-time计数,避免双计。

#### 必需测试

新增捕获 `FileIoContext` 的test `ReadFile`,通过demand、warm、preload三条路径断言:

```text
context.ioStats == builder传入对象
context.fileOpts == builder传入fileReadOps
context.cacheable == requestContext.cacheable
```

同时保留DWRF和Parquet format E2E,证明source wrapper/lifetime改变不影响真实reader。

#### 11.3.1 实施前调研纠正与接法待决(读代码后补,待用户 review)

动手前对 `filecache2` HEAD `506751cc5` 做了只读调研(逐行读全文件,file:line 精确),
纠正本节几处事实,并把已批准方案落到两个可选接法。以下为待 review 内容,尚未改生产
代码。

##### 事实纠正

1. `FileIoContext` 的字段名是 `fileOpts`,不是 `fileReadOps`
   (`velox/common/file/File.h:71-97`)。本节"必需测试"里
   `context.fileOpts == builder传入fileReadOps` 的等式仍成立,但断言字段名应为
   `fileOpts`;没有独立 `options` 字段。

2. `cacheable` 不是独立字段外的东西:它是 `FileIoContext::cacheable`
   (`File.h:84`),由 `requestContext.cacheable` 经 base ctor 传入。

3. **基类 `ReadFileInputStream`(已携带填好的 `FileIoContext`)其实已经存在**,
   由 base `BufferedInput` delegating ctor 构造
   (`velox/dwio/common/BufferedInput.h:76-86`),活在
   `input_` / `getInputStream()`(`BufferedInput.h:93,:191`)里,构造参数正是
   `FileCacheBufferedInput.cpp:219-228` 传入的 `ioStats`/`fileReadOps`/
   `requestContext.cacheable`。也就是说:**context 从未"没构造",只是三条 source
   读取路径绕过了 `input_` 直接裸 `pread`。** 因此 C2 不是"新建第二个 stream",而是
   "把绕过的三条路接回 `input_`"。

4. `velox::ReadFile::pread` 的带 context 重载:
   `pread(uint64_t, uint64_t, void*, const FileIoContext& = {})`(`File.h:107-111`)。
   `ReadFileInputStream::read` 内部 `readFile_->pread(..., fileIoContext_)`
   (`InputStream.cpp:85`)并 `incRawBytesRead` + `incTotalScanTimeNs`
   (`:88-89`)。

##### 绕过 `input_` 的三条 source 路径(现状)

```text
preload:  FileCacheBufferedInput.cpp:1044  sourceReadFile_->pread(0, fileSize_, ...)        3-arg,无 context
          FileCacheBufferedInput.cpp:1062  sourceReadFile_->pread(fileOffset, readSize, ...) 3-arg,无 context
demand:   FileCacheInputStream.cpp:126     ReadBufferFromVeloxReadFile(sourceReadFile(), pool)
warm:     FileCacheBufferedInput.cpp:710   ReadBufferFromVeloxReadFile(ctx.source.get(), pool)
```

`ReadBufferFromVeloxReadFile::readInto`(`ReadBufferFromVeloxReadFile.cpp:378`)对
source 亦是 3-arg `pread`,context 丢失。(注:同类还用于 **local cache-segment**
reader,`FileCacheInputStream.cpp:175`,那条不携带 remote source context,不在 C2
范围。)

##### 双计清单(接回 base read 后必须同 commit 删,否则 raw/read 双计)

base `read` 会 `incRawBytesRead` + `incTotalScanTimeNs`;当前手工计数只做
`incRawBytesRead`/`read()`,**未**做 `incTotalScanTimeNs`。接回后须删:

```text
FileCacheBufferedInput.cpp:862-865   warm/demand-group: read()/incRawBytesRead/queryThreadIoLatencyUs/storageReadLatencyUs
FileCacheBufferedInput.cpp:1079-1082 preload:           read()/incRawBytesRead/queryThreadIoLatencyUs/storageReadLatencyUs
FileCacheInputStream.cpp:689-690     predownload:       read()/incRawBytesRead
FileCacheInputStream.cpp:911-912     demand delivery:   read()/incRawBytesRead(source 分支)
```

`FileCacheInputStream.cpp:909` 的 `ssdRead()`(cache-hit 分支,非 source 读)与
`:918` 的 `recordReadBytes`(ScanTracker,demand 交付计量,非 raw IO)**保留**。
ProfileEvents `CachedReadBufferReadFromSourceBytes`(`:867,:683`)是 FileCache 特有
source 分类,base stream 不产出,**保留**。

##### 两个候选接法(请 review 选一)

**接法 A(严格照本节已批准方案,推荐)**:给 `ReadBufferFromVeloxReadFile` 新增一个
接受 `std::shared_ptr<dwio::common::ReadFileInputStream>` 的 source 构造,`readInto`
改调 `input->read(dest, length, offset)`;demand(`FileCacheInputStream.cpp:126`)、
warm(`FileCacheBufferedInput.cpp:710`)改用该构造。cursor/buffer 兼容层保留,context
自动到位。代价:base read 内部计数生效,上面双计清单必须同 commit 删干净。

**接法 B(最小改动)**:不改 `ReadBufferFromVeloxReadFile` 构造,只给 `readInto`
(`:378`)的 `pread` 传入从 owner 拿到的 `input_->fileIoContext()`;preload 两处裸
`pread`(`:1044/:1062`)同样补传。改动面最小、不动 stream 结构。代价:偏离本节
"委托回 `ReadFileInputStream`"的字面方案,且 `incRawBytesRead`/scan-time 仍需手工补
(因为没走 base `read`),等于把已有手工计数换成手工补 scan-time,重复问题未根除。

**Status: 接法待用户 review 后确定,C2 生产代码尚未改动。**

##### Reviewer复核结论

事实纠正第1-4项成立。接法选择:

```text
采用接法A,不采用接法B。
```

接法A需按以下边界实施。

1. `ReadFileInputStream::read` 的scalar overload自动负责:

   ```text
   incRawBytesRead
   incTotalScanTimeNs
   FileIoContext传递
   MetricsLog
   ```

   它不负责:

   ```text
   IoStatistics::read
   queryThreadIoLatencyUs
   storageReadLatencyUs
   FileCache ProfileEvents
   ```

   因此第11.3.1节“双计清单”不能删除整个手工统计block。source路径只删除重复的
   `incRawBytesRead`;继续保留 `read`、两类latency和FileCache-specific
   `ProfileEvents`。local cache-hit路径没有经过source `ReadFileInputStream`,其
   `ssdRead` 和raw bytes计数继续保留。

2. source adapter调用必须传完整的四参数接口:

   ```cpp
   input->read(dest, length, offset, logType);
   ```

   `ReadBufferFromVeloxReadFile` 的source-input构造需保存 `LogType`:

   ```text
   demand -> FileCacheInputStream的logType_
   warm   -> LogType::FILE
   preload-> LogType::FILE
   ```

3. non-contiguous preload继续逐allocation run调用scalar `input_->read`,使每个run
   自动携带同一个 `FileIoContext` 并记raw bytes/scan-time。不要在本项改用当前不
   自动增加这两项统计的vector overload。

4. 接法B按原描述不可成立。`ReadBufferFromVeloxReadFile::readInto` 当前拿不到owner
   的 `FileIoContext`;即使使用公开的 `ReadFileInputStream::fileIoContext`,仍需
   修改构造或增加成员把context传入adapter,并继续手工维护底层统计。它既没有做到
   “不改构造”,也没有消除重复IO职责,因此拒绝。

最终状态:

```text
C2接法A获批,按上述统计所有权和LogType修正实施。
```

##### 实施结果(commit `574fa6b91`,待 review,未 push)

接法 A 已实施并经 Controller 独立验证。改动 6 文件(生产 5 + 测试 1),
`+270 / -21`。

生产改动:

```text
ReadBufferFromVeloxReadFile.{h,cpp}
  新增接受 shared_ptr<dwio::common::ReadFileInputStream> + LogType 的 source 构造;
  readInto 在 source-input 模式走 input->read(dest,destCapacity,startOffset,logType_),
  否则保持原 readFile_->pread。原两个 readFile_ 构造(local cache-segment reader)不变。
  getFileName() source 模式返回 sourceInput_->getName()。
FileCacheBufferedInput.h
  新增 sourceInputStream() 转发 getInputStream()。
FileCacheInputStream.cpp:126  demand createRemoteReadBuffer 改用 source 构造,logType_。
FileCacheBufferedInput.cpp:550/623/705  warm:WarmTaskContext::source 从 ReadFile
  改为 ReadFileInputStream(shared_ptr 存活整个 task),reader 用 LogType::FILE。
FileCacheBufferedInput.cpp:1044/1062  preload 两处逐 run 调 scalar
  getInputStream()->read(...,LogType::FILE)(不用 vector overload)。
```

只删重复的 `incRawBytesRead`(base read 已自动记 raw bytes + scan time),4 处:

```text
FileCacheBufferedInput.cpp  warm、preload 各一处
FileCacheInputStream.cpp    predownload 一处;demand delivery 一处(见 deviation 1)
```

`read()` / 两类 latency / FileCache ProfileEvents / cache-hit `ssdRead` 全部保留。

`LogType::FILE` = `MetricsLog::Type::FILE`(`velox/dwio/common/MetricsLog.h:140` 的
`using LogType = MetricsLog::Type;`,枚举 `:33`),经新增
`#include "velox/dwio/common/InputStream.h"` 引入。

测试(`FileCacheBufferedInputTest.cpp`,新增 3 个):自定义捕获 `FileIoContext` 的
test `ReadFile`,demand / warm / preload 三路径断言
`context.ioStats/fileOpts/cacheable == builder 传入`。buffered_input 33 → 36。

Controller 独立验证:

```text
RED 独立复现:中和 readInto 的 source 分支(改回裸 pread)后 rebuild,
  demand + warm 两测试变红(context 空)、preload 仍绿(它走 getInputStream()->read
  未被中和)-> 精确证明三测试各绑对应路径,非全红巧合。已恢复。
diff 审查:6 文件无夹带。
全 8 gate 亲验:183/183(connector34/buffered_input36/e2e21/cancellation8/
  hit_metrics7/core_scc55/manager20/format_e2e2),-Werror 干净。
```

##### 实施中的两处 deviation(Controller 复核判定均正确,需 reviewer 知晓)

1. **`FileCacheInputStream.cpp` demand delivery 的 `incRawBytesRead` 挪位而非删除。**
   原代码结构为 `if (servedFromCache) ssdRead(); else read();` 之后一行**公共**的
   `incRawBytesRead(size)`,同时服务 cache-hit 与 source 两分支。source 分支现在走
   base `read` 自动记 raw bytes,但 cache-hit 分支走裸 pread(local cache reader),
   base 不记账。若按"直接删该行"会漏记 cache-hit 的 raw bytes。故把
   `incRawBytesRead` **挪进 cache-hit 分支**,source 分支交给 base read。总账不变。

2. **source `readInto` 短读契约收紧。** base `ReadFileInputStream::read` 对精确长度
   `VELOX_CHECK`(短读抛异常,不续读),而原裸 `pread` 返回实际读到的 `chunk.size()`。
   当前 FileCache 调用已用 `readUntil_` / 文件大小 bound 住 `destCapacity`,全 gate
   通过。**遗留提示**:若未来某个 remote `ReadFile` 返回 positive short read,此路径会
   抛异常而非续读循环,需届时改回带长度校验的续读。本轮不引入该分支。

**Status: C2 实施完成,Controller 验证通过,等外部 reviewer 复核后再定是否进入 C1。**

##### 外部Reviewer复核结论

C2的source读取接法、`FileIoContext` 传递、`LogType` 和raw统计拆分均按获批方案
落地。但warm task新增了一个未保活的raw-pointer owner,当前仍有生命周期blocker。

`ReadFileInputStream` 内部持有:

```text
InputStream::stats_                  -> IoStatistics*
InputStream::ioStats_                -> IoStats*
ReadFileInputStream::fileIoContext_  -> 内含IoStats*
```

这些均是raw pointer。warm task允许outlive `FileCacheBufferedInput`,所以对应shared
owner必须进入warm payload。

当前实现:

```text
ioStatistics_:
  warmSourceGroup复制shared_ptr并capture到lambda
  -> stats_在warm期间有效

ioStats_:
  未复制、未进入WarmTaskContext、未capture
  -> FileCacheBufferedInput及外部query owner销毁后可能提前释放
```

此后warm调用:

```text
sourceInput_->read
-> ReadFile::pread(..., fileIoContext_)
-> storage实现可能访问fileIoContext_.ioStats
```

会读取悬空的 `IoStats*`。

最小修复:

```cpp
std::shared_ptr<velox::IoStats> ioStats = ioStats_;
```

将该owner放入 `WarmTaskContext` 或warm lambda capture,保证它至少与
`sourceInput` 同寿命。推荐放入 `WarmTaskContext`,将source stream依赖的两个raw
pointer owner显式收拢:

```text
shared_ptr<IoStatistics>
shared_ptr<IoStats>
shared_ptr<ReadFileInputStream>
```

必需生命周期测试:

```text
1. 用单线程executor的前置blocking task阻塞worker。
2. submit warm,确认warm已入队但尚未执行。
3. 保存weak_ptr<IoStats>,销毁FileCacheBufferedInput并释放测试侧shared owner。
4. 断言warm仍排队时weak_ptr未expired。
5. 释放blocking task并join executor。
6. warm payload销毁后断言owner可以释放。
```

测试只观察shared ownership,不主动制造悬空访问,不增加生产test seam。

当前结论:

```text
Status: BLOCK
关闭条件: warm payload持有ioStats_ shared owner并通过上述生命周期测试。
```

##### BLOCK 修复结果(commit `4b23ec363`,新增 commit 未 amend,待 review,未 push)

Reviewer 的 UAF 判定成立且为 C2 直接引入(接回 base stream 才使 warm 走上
`fileIoContext_`)。已按最小修复 + 推荐收拢方案实施。

生产修复(`FileCacheBufferedInput.cpp` `warmSourceGroup`):

```text
WarmTaskContext 新增字段 std::shared_ptr<velox::IoStats> ioStats;
  声明在 source 之前 -> 析构顺序 source 先、ioStats owner 后,
  保证任何 in-flight ReadFile::pread 期间 fileIoContext_.ioStats 有效。
warm 入口复制 ioStats_ (ioStatsOwner) 并入 ctx。
```

澄清:此前已复制的是 `ioStatistics_`(`io::IoStatistics`,warm accounting 块自用),
与 `fileIoContext_` 无关;真正进 `fileIoContext_.ioStats` 的 `ioStats_`
(`velox::IoStats`)此前未保活,是本 BLOCK 的根因。

测试 `WarmPayloadKeepsIoStatsAlivePastInputDestruction`(只观察 shared ownership,
不制造悬空访问,无生产 seam,无 sleep):

```text
1. 单线程 executor + 前置 baton 阻塞 task 占住唯一 worker(occupied.wait() 确保已占用)。
2. load() 提交 warm -> 入队但排在阻塞 task 后,未执行。
3. weak_ptr<IoStats> = b.ioStats;销毁 b.input;reset b.ioStats。
4. 断言 warm 排队时 weak 未 expired(warm payload 保活了 IoStats)。
5. release.post();executor_->join()。
6. 断言 warm payload 析构后 weak expired(owner 可释放)。
```

Controller 独立验证:

```text
RED 独立复现:中和 fix(WarmTaskContext 传 nullptr ioStats owner)后 rebuild,
  该测试变红(input 销毁后 weak 立即 expired = 悬空)。已恢复。
全 8 gate 亲验:184/184(connector34/buffered_input37/e2e21/cancellation8/
  hit_metrics7/core_scc55/manager20/format_e2e2),-Werror 干净。
```

cherry-pick 到 `filecache2-gluten`(`b9417e4b8`,无冲突),已切回 `filecache2`。

**Status: BLOCK 关闭条件已满足(warm payload 持 IoStats shared owner + 生命周期
测试通过),Controller 验证通过,等外部 reviewer 复核。**

##### 外部Reviewer验收

复核commit:

```text
4b23ec363  FileCache: C2修复warm payload悬空IoStats
```

确认:

- `ioStats_` 的shared owner进入 `WarmTaskContext`;
- `ioStats` 声明在 `source` 前,按reverse member destruction order由source先析构、
  owner后析构;
- queued warm允许outlive `FileCacheBufferedInput`,但整个task执行期间
  `fileIoContext_.ioStats` 均有有效owner;
- lifecycle test在input和测试侧owner释放后验证warm payload仍保活 `IoStats`,task
  完成后owner正常释放;
- targeted生命周期测试独立运行1/1通过。

C2原BLOCK关闭:

```text
Status: APPROVE
```

### 11.4 mixed-state planning必须检查完整chunk覆盖

#### 问题

plan按 `ReaderOptions::loadQuantum` 切chunk,但一个chunk可能覆盖多个较小的
FileSegment。`classifyChunk` 调用 `FileCache::get` 取得覆盖整个chunk的holder后,
只检查 `holder->front`。

具体例子:

```text
loadQuantum chunk: [0, 8 MiB)
FileSegment 0: [0, 1 MiB), DOWNLOADED
FileSegment 1..7: EMPTY
```

首segment没有覆盖整个chunk,当前代码将chunk判为 `kDownloading`,因此尾部7 MiB
不会进入miss warm group,只能由demand回源。数据仍正确,但prefetch planning因
首segment状态而失效。

#### 已批准方案

`classifyChunk` 必须遍历holder的所有overlap segments,最小分类规则:

```text
存在可填充的EMPTY/DETACHED范围 -> kMiss
否则存在DOWNLOADING/未完整覆盖 -> kDownloading
全部范围已下载 -> kHit
```

将mixed chunk整体标为miss是可接受的:执行warm时已下载segment会被跳过,只填空缺
segment。若后续planner改为精确子区间,可直接生成hit/miss sub-chunks,但本轮不要求
为此扩大重构。

#### 必需测试

```text
segment size = 1 MiB
loadQuantum = 4 MiB
预先只缓存第一个segment
enqueue sequential [0, 4 MiB)
load + executor join
验证剩余三个segment被warm为DOWNLOADED
```

##### 实施结果(commit `c12de374b`,filecache2,待 review,未 push)

按 §11.4 实施。`classifyChunk` 从只看 `holder->front()` 改为遍历 holder 全部 overlap
segments,用 `covered` high-water mark(起点 offset)逐段推进:

```text
每段算与 chunk 的 overlap [segStart=max(range.left,offset),
  segEnd=min(range.right+1, chunkEnd)):
段前有洞(segStart>covered)          -> kMiss
EMPTY / DETACHED                    -> kMiss(立即)
DOWNLOADED/PARTIALLY 写前缀 >= segEnd -> 推进 covered
DOWNLOADED/PARTIALLY 写前缀 < segEnd 或 DOWNLOADING -> anyDownloading,推进到已下载边界
循环后 covered < chunkEnd(尾部 gap)  -> kMiss
否则 anyDownloading ? kDownloading : kHit
```

保留 `length==0`/empty-holder/溢出 DCHECK/read-only `cache_->get` 契约。mixed chunk
整体标 miss 可接受(warm 执行跳过已下载段、只填空缺)。miss-coalescing 未动。

测试 `MixedChunkWarmsUncachedSegments`:segment 1MiB / loadQuantum 4MiB(fixture 新增
`loadQuantum_` 经 `ReaderOptions::setLoadQuantum`,默认 0 不影响既有测试),只预缓存
首段,`enqueue [0,4MiB)` prefetch,`load + join`,断言剩余 3 段 warm 为 DOWNLOADED
(`getFileSegmentsNum` 1→4)。

Controller 独立验证:

```text
RED:C7 有两道防线(EMPTY 立即 kMiss + 尾部 covered<chunkEnd kMiss)。只中和 EMPTY
  一道时测试仍绿(尾部 gap 兜住);同时中和两道 -> 回到 front-only 行为 -> mixed chunk
  判 kDownloading、尾部 3 段不 warm -> 测试红。精确锚定,且证明 C7 比文档要求更 robust。
  已恢复。
全 8 gate 亲验:186(...buffered_input36...),-Werror 干净。
```

gluten cherry-pick 搁置(最终一致性)。

**Status: C7 实施完成,Controller 验证通过,等外部 reviewer 复核。**

##### 外部Reviewer复核结论

C7发现明确state classification问题。当前 `DOWNLOADING` 分支只设置
`anyDownloading=true`,没有推进几何coverage:

```cpp
case FileSegment::State::DOWNLOADING:
{
    anyDownloading = true;
    break;
}
```

单个DOWNLOADING segment完整覆盖chunk时,`covered` 仍停在chunk起点,循环后的
`covered < chunkEnd` 将其误判为 `kMiss`,产生无意义warm task。

CH `CachedOnDiskReadBufferFromFile` 的真实状态语义:

```text
DOWNLOADING:
  prefix可读 -> cache read
  否则 -> wait现有downloader
  不重新当作miss抢downloader

EMPTY / PARTIALLY_DOWNLOADED:
  prefix不可覆盖时允许getOrSetDownloader继续填

PARTIALLY_DOWNLOADED_NO_CONTINUATION:
  prefix可读;prefix用完后bypass,不允许继续填
```

Velox planner应映射为:

```text
DOWNLOADED                             -> kHit
DOWNLOADING                            -> kDownloading
EMPTY                                  -> kMiss
PARTIALLY_DOWNLOADED未覆盖完整subrange  -> kMiss
PARTIALLY_DOWNLOADED_NO_CONTINUATION缺尾 -> kDownloading/不可warm
```

`covered` 只表示segment metadata几何覆盖,不表示resident bytes。每个无gap overlap
segment均推进到 `segEnd`;state另行决定hit/miss/downloading:

```cpp
covered = std::max(covered, segEnd);
```

具体修复:

- `DOWNLOADING`:推进 `covered`,设置 `anyDownloading`;
- `PARTIALLY_DOWNLOADED`:prefix不足时立即 `kMiss`;
- `PARTIALLY_DOWNLOADED_NO_CONTINUATION`:prefix不足时设置
  `anyDownloading`,但不进入warm;
- `DOWNLOADED`:直接推进 `covered`,删除恒不可达else分支。

必需测试:

```text
single DOWNLOADING segment完整覆盖chunk -> kDownloading,无warm group
PARTIALLY_DOWNLOADED缺尾              -> kMiss,可warm续填
PARTIALLY_DOWNLOADED_NO_CONTINUATION  -> 不创建warm group
保留现有DOWNLOADED prefix + EMPTY tail -> kMiss并填尾部
```

本修复仅改Velox集成层 `FileCacheBufferedInput`,不得修改CH `FileSegment`。

```text
Status: REQUEST CHANGES
```

### 11.5 接通CH `reserve_hint`

#### 背景

`FileSegment` 同时跟踪:

```text
downloaded_size: 已真实写入磁盘
reserved_size: 已从FileCache quota提前占用
```

`reserve_granularity` 允许一次reserve较大额度,减少每个小chunk重复抢锁/驱逐。
例如本次写64 KiB、granularity为8 MiB时,可能一次reserve 8 MiB。

如果当前read horizon只剩1 MiB,无hint会多占7 MiB,可能导致无谓驱逐、
QueryLimit提前命中或其他segment reserve失败。CH通过 `reserve_hint` 将
reserve-ahead限制到当前read实际可能消费的范围。

Velox port已经实现 `FileSegment::reserve` 的 `reserve_hint` 参数及内部限制,
但所有调用仍传默认0,因此该CH语义没有接通。

#### 已批准方案

各路径传入:

```text
demand:
  readUntilPosition - currentWriteOffset

predownload:
  bytesToPredownload

warm:
  writeTarget - writeOffset

preload:
  segmentEnd - writeOffset
```

同时将reserve/write收口到第11.10节的共享helper,避免某个新路径再次漏传hint。

#### 必需测试

设置:

```text
reserve granularity = 8 MiB
read horizon = 1 MiB
first write chunk = 64 KiB
```

断言 `reserved_size` 不超过1 MiB,并验证正常继续写完整read horizon。

### 11.6 删除 `PreloadedRunStream`,统一使用 `FileCacheInputStream`

#### 问题

`PreloadedRunStream` 是在 `FileCacheBufferedInput.cpp` 内重新实现的
`SeekableInputStream` 派生类,只负责whole-file preload allocation上的:

```text
Next
BackUp
Skip
seek
ByteCount
```

但 `FileCacheInputStream` 本应是FileCache体系内一个region的统一stream:

```text
RAM preload
local FileSegment
remote source
```

preload只是data source/read state,不应再发明第二套stream。该复制已经导致:

- preload `enqueue` 在 `recordReference` 前提前返回;
- `PreloadedRunStream::Next` 不调用 `ScanTracker::recordRead`;
- preload与普通stream的BackUp/seek/tracking语义漂移。

#### 已批准方案

删除 `PreloadedRunStream`。对齐Direct:

```text
FileCacheBufferedInput::enqueue
  -> 始终创建FileCacheInputStream
       -> owner preloaded: 读preload allocation
       -> cache hit: 读local FileSegment
       -> miss: 读source并填FileSegment
```

修改:

- `FileCacheBufferedInput` 提供
  `preloadedData(offset, length) -> folly::Range<const char *>`,跨allocation run时
  返回当前连续slice;
- `enqueue` 先复制tracking id并 `recordReference`;
- 只有非preload请求才进入planner `requests_`;
- `FileCacheInputStream::Next` 最前面处理preloaded data;
- published output window抽象为“pointer + start + size + cursor”,既可指向owned output
  buffer,也可指向preload allocation;
- `BackUp`、seek、重复 `Next` 复用同一window逻辑。

#### ScanTracker语义

将 `recordReadBytes` 从底层 `readFromCurrentSegment` 移到每次 `Next` 成功交付前:

```text
pending buffer fast path -> recordRead
new output buffer path   -> recordRead
preload path             -> recordRead
```

这与Direct/Cached一致,也会正确记录 `BackUp` 或buffer内seek后的再次交付。
`SkipInt64` 不记录,因为没有向调用方交付数据。

#### 必需测试

- preload `enqueue` 记录reference和read;
- preload跨allocation runs时每次 `Next` 均返回零拷贝slice;
- `BackUp` 后再次 `Next` 增加第二次read计数;
- buffer内seek后再次交付增加read计数;
- cold/warm/preload三种data source使用同一个 `FileCacheInputStream` 类型。

### 11.7 补全Velox coalesced-load执行模型

#### 问题

Direct和Cached的完整模型:

```text
request classification/grouping
-> 创建shared CoalescedLoad
-> stream address -> CoalescedLoad映射
-> prefetch group由executor主动执行
-> demand group由第一个stream::Next懒执行
-> group结果按region/cache key交给各stream
```

当前FileCache只复制了前半段:

```text
classify
groupMissChunks(prefetch)
groupMissChunks(demand)
```

执行时只处理:

```cpp
if (group.prefetch)
    warmSourceGroup(group);
```

因此:

- demand `CoalescedGroup` 生产后从未被消费;
- group没有绑定回 `FileCacheInputStream`;
- 没有Direct/Cached的 `moveCoalesced`;
- demand仍由每个stream独立读取;
- warm group跨FileSegment时仍执行多个source `pread`,不等价于一次coalesced
  `preadv`;
- `maxCoalesceBytes` 更多控制task/holder分组,没有稳定保证减少remote IO。

#### 已批准方案

先删除“生成但不消费”的demand group。完整实现FileCache executor:

```text
FileCacheCoalescedLoad
stream -> FileCacheCoalescedLoad映射
```

行为:

- prefetch group: `load` 后提交executor;
- demand group:首个相关 `FileCacheInputStream::Next` 调用 `loadOrFuture`;
- group load完成后填充持久FileSegments;
- stream随后走正常FileCache state machine并命中已填segment;
- 丢弃stream不取消已规划group,保持Velox native prefetch合同;
- concurrent streams共享同一load/future,不重复source read。

planning policy应从Direct/Cached提取为共享helper,至少共享:

```text
prefetch/demand分类
group边界
maxCoalesceDistance
maxCoalesceBytes / loadQuantum规则
moveCoalesced
read-gap / duplicate-region统计
```

Direct/Cached/FileCache保留各自executor:

```text
Direct -> DirectCoalescedLoad
Cached -> DwioCoalescedLoad / SsdLoad
FileCache -> FileCacheCoalescedLoad
```

`FileCacheCoalescedLoad` 的source阶段必须以一次coalesced read为目标。若受
FileSegment顺序写约束不能对整个group执行单次 `preadv`,代码和指标必须明确它只是
warm task grouping,不得宣称已经提供source IO coalescing。

#### 必需测试

- 两个相邻demand regions映射到同一个load;
- 第一个stream `Next` 触发load,第二个stream不再读source;
- prefetch group在executor上主动执行;
- discarded stream不取消group;
- overlapping prefetch/demand通过 `moveCoalesced` 只产生一次source load;
- 统计source `pread/preadv` 调用次数,证明group确实减少IO;
- FileCache segment填充结果与未coalesce路径内容一致。

### 11.8 删除全局writer test factory

#### 问题

`506751cc5` 已清理writer内部test seam,但 `FileSegment` 仍通过进程级静态factory
创建writer:

```text
FileSegment::WriteFileFactory
writeFileFactoryStorage
setWriteFileFactoryForTesting
createWriteFile
```

测试临时将全局factory从 `FileCacheLocalWriteFile` 换为 `FaultyWriteFile`,
结束后再RAII恢复。该设计使所有FileCache实例共享可变生产状态,并行测试、异步任务
和恢复顺序可能相互污染。

用户选择:

```text
恢复生产代码直接创建FileCacheLocalWriteFile。
不保留进程级或实例级writer factory。
宁可减少syscall/write-fault集成测试,也不让异常测试机制改变生产结构。
```

#### 生产修改

`FileSegment::write` 直接构造:

```cpp
download->cache_writer =
    std::make_shared<WriteBufferFromVeloxWriteFile>(
        std::make_unique<FileCacheLocalWriteFile>(file_segment_path));
```

删除factory alias、storage、setter、creator及两份test RAII guard。

#### 纯errno策略

提取无副作用生产helper,由demand/warm/preload共享:

```cpp
enum class CacheWriteErrorAction
{
    Bypass,
    Rethrow,
};

CacheWriteErrorAction classifyCacheWriteError(
    int error,
    bool skipOnDiskFailure) noexcept;
```

规则:

```text
ENOSPC / EDQUOT, skip=false/true -> Bypass
其他errno, skip=true             -> Bypass
其他errno, skip=false            -> Rethrow
非FileCacheErrnoException         -> 不进入helper,自然传播
```

#### 测试影响

当前有:

```text
13处ScopedWriteFileFactory安装
12个依赖全局factory的测试函数
3个自定义fault writer/helper
2份重复ScopedWriteFileFactory
```

处理:

```text
3个errno分类测试 -> 合并为1个table-driven纯策略测试

4个测试使用标准故障重写:
  MidDownload...ReleasesDownloader
    -> FaultyReadFile / source exception
  PreloadFillBypass...
    -> reserve failure
  PreloadBypass...RecordsSourceStats
    -> reserve failure
  WarmRecordsSourceRead...
    -> reserve failure

5个write/syscall-fault集成测试删除并由CH对齐+review保证:
  PartialPhysicalAppendFailureReconcilesDownloadedToPhysical
  DemandReadNonErrnoLogicErrorAlwaysPropagates
  PreloadFillStrictDiskFailurePropagates
  PreloadStrictFillFailureStillRecordsSourceStats
  PreloadFillNonErrnoLogicErrorAlwaysPropagates
```

12个factory测试最终缩减为:

```text
1个纯errno策略测试
4个标准故障测试
净删除7个test functions
```

#### 其他测试入口

- `FileSegment::wait` 的 `TestValue::adjust` 是Velox标准机制,release为no-op,
  保留;
- `FileCache::inflightWarmForTest` 及无界yield轮询删除,测试改用专用executor
  `join` 或完成Baton;
- planning只读观测getter不改变生产行为,可后续改为friend fixture,不作为本轮
  blocker。

##### 实施结果(commit `1b92545ef`,filecache2,待 review,未 push)

按 §11.8 实施。生产:

```text
FileSegment.{h,cpp}
  删 WriteFileFactory alias / setWriteFileFactoryForTesting / createWriteFile /
  writeFileFactoryStorage(匿名 ns);write() 直接
  make_shared<WriteBufferFromVeloxWriteFile>(make_unique<FileCacheLocalWriteFile>(path))。
FileCacheInputStream.{h,cpp}
  新增纯 helper enum class CacheWriteErrorAction { Bypass, Rethrow };
    classifyCacheWriteError(int error, bool skipOnDiskFailure) noexcept;
    ENOSPC/EDQUOT -> Bypass;其他 errno 看 skip;非 FileCacheErrnoException 不进
    helper 自然传播。
  downloadChunkIntoSegment 与 writeCache 两个 consumer catch 块收口调用它,
  ProfileEvents/PARTIALLY_DOWNLOADED 注释与成功路径 increment 保留。
```

测试处置(实际 23 处 ScopedWriteFileFactory 安装,非文档估的 13;按实际处理):

```text
新增 1: CacheWriteErrorPolicyTest.ClassifiesByErrnoAndSkipFlag
  table-driven 纯策略:ENOSPC/EDQUOT/EIO/EACCES × skip。

改写(可用 reader/reserve 故障表达):
  MidDownload...ReleasesDownloaderNoLeak -> FaultyReadFile 抛 source read;
    靠"独立第二 stream 能重新当选 downloader 读完"验证无泄漏(测行为不测实现)。
  DemandReadErrnoDiskFailureBypassesWhenSkip -> reserve 失败(tiny cache),
    改名 DemandReadDiskFailureBypassesWhenSkip。
  PreloadFillBypassDiskFailureStillCommits -> reserve 失败。
  PreloadBypassFillFailureRecordsSourceStats -> reserve 失败。
  WarmRecordsSourceReadEvenWhenCacheWriteFails -> reserve 失败。

删除(syscall/write-fault 端到端,无 writer seam 不可表达,靠 CH 对齐+review):
  PartialPhysicalAppendFailureReconcilesDownloadedToPhysical
  DemandReadNonErrnoLogicErrorAlwaysPropagates
  PreloadFillStrictDiskFailurePropagates
  PreloadStrictFillFailureStillRecordsSourceStats
  PreloadFillNonErrnoLogicErrorAlwaysPropagates
  + 两份 ScopedWriteFileFactory guard 及自定义 fault writer helper。

保留:FileCacheLocalWriteFileTest.{AppendOrFlushAfterCloseIsNonErrnoLogicError,
  NormalAppendResumeAndSize}(直接测 writer,从未用 factory)。
CMakeLists:buffered_input 加 velox_file_test_utils(FaultyReadFile)。
```

Controller 独立验证:

```text
RED 独立复现:中和 classifyCacheWriteError 恒 Bypass -> policy 测试红
  (EIO+!skip / EACCES+!skip 两个 Rethrow case)。已恢复。
worker flag 复核:MidDownload 重写测试中和 catch 里 releaseDownloader 不变红,
  因 stream 析构也释放 -> 判定它锚定端到端不变量(无泄漏)而非单一代码行,
  是有效的"测行为不测实现",保留。
全 8 gate 亲验:178(connector34/buffered_input33/e2e21/cancellation8/
  hit_metrics7/core_scc53/manager20/format_e2e2),-Werror 干净。
```

**覆盖边界(需 reviewer 知晓)**:errno classify 的 Bypass/Rethrow 两分支现由纯策略
测试覆盖;errno 的**端到端** skip->bypass 与 strict->rethrow 均不再有端到端测试
—— 这是 §11.8 删除 write-fault 集成测试的直接后果(不止结论段提到的 Rethrow,
skip->bypass 端到端也随之降级为纯策略覆盖)。reserve-failure 改写测的是 reserve
早退 bypass 路径,不经过 errno catch,二者覆盖不等价但符合 §11.8 设计取舍。

**gluten cherry-pick 搁置**:cherry-pick 到 `filecache2-gluten` 时冲突暴露 gluten 的
`downloadChunkIntoSegment`/`writeCache` errno catch 仍是 `catch (const std::exception &)`
(无 errno 分类),落后于 filecache2 的 `FileCacheErrnoException`(缺 §7.5/§8.3)。
按用户决定,本轮 C1-C7 只落 filecache2,gluten 的 errno 分叉与全部 cherry-pick 留待
**最终一致性**一次性统一处理。

**Status: C1 实施完成,Controller 验证通过,等外部 reviewer 复核。**

##### 外部Reviewer复核结论

C1生产实现通过:

- `FileSegment` 已恢复直接创建 `FileCacheLocalWriteFile`;
- 全局writer factory、setter、creator和两份test guard均已删除;
- `classifyCacheWriteError` 与CH errno规则一致;
- cache-write consumers统一使用纯策略;
- 非 `FileCacheErrnoException` 不进入策略helper并自然传播;
- write/syscall fault端到端覆盖降级没有超出第11.8节已批准范围。

测试仍需一项去重/澄清。

`DemandReadDiskFailureBypassesWhenSkip` 当前通过tiny cache使 `reserve` 返回false。
该路径在进入errno catch和读取 `skipCacheOnDiskFailure` 之前已无条件bypass,与已有:

```text
ReserveFailureBypassesCacheButReturnsData
```

覆盖同一生产路径。前者的名称和 `skipCacheOnDiskFailure=true` 会误导为“验证
write errno的skip wiring”,但实际上只验证reserve failure。删除该重复测试。

其他使用tiny cache制造reserve failure的测试继续保留各自独立合同:

```text
PreloadFillBypassDiskFailureStillCommits
PreloadBypassFillFailureRecordsSourceStats
WarmRecordsSourceReadEvenWhenCacheWriteFails
```

但删除其中无效的:

```cpp
s.skipCacheOnDiskFailure = true;
```

并将注释/名称中的“disk write failure受skip控制”改为“reserve failure无条件
bypass”。这些测试分别证明RAM preload commit、source accounting和warm accounting,
不与普通demand reserve-failure测试重复。

当前结论:

```text
C1生产代码: APPROVE
C1整体: 待删除1个重复测试并清理3处无效skip设置/误导注释后APPROVE
```

##### C1 收尾结果(commit `4848a5b52`,filecache2,待 review,未 push)

按 reviewer 关闭条件完成纯测试清理(不动生产代码)。reviewer 洞察成立:这批
reserve-failure 改写测试用 tiny cache 让 `reserve` 返回 false,该路径在进入 errno
catch / 读 `skipCacheOnDiskFailure` **之前**已无条件 bypass,故 skip 设置在这些
测试里不起作用。

```text
删除 DemandReadDiskFailureBypassesWhenSkip:与既有
  ReserveFailureBypassesCacheButReturnsData 覆盖同一 reserve-failure 路径,重复;
  且名称/skip 设置误导为"验证 write errno skip wiring"。
清理 3 处(删无效 s.skipCacheOnDiskFailure=true + 注释改"reserve failure 无条件 bypass"):
  PreloadFillBypassDiskFailureStillCommits            (合同:RAM preload commit)
  PreloadBypassFillFailureRecordsSourceStats          (合同:source accounting)
  WarmRecordsSourceReadEvenWhenCacheWriteFails
    -> 改名 WarmRecordsSourceReadEvenWhenCacheFillBypasses (合同:warm accounting)
三者各自合同保留,不与普通 reserve-failure 测试重复。
```

Controller 独立验证:全 8 gate 亲验 185(buffered_input 35,删 1 个重复测试),
-Werror 干净。grep 确认无残留无效 skip 设置。gluten cherry-pick 搁置(最终一致性)。

**Status: C1 收尾完成,满足 reviewer 关闭条件(生产 APPROVE + 测试去重/澄清),
等外部 reviewer 确认整体 APPROVE。**

### 11.9 修正actual IO与delivered bytes统计

#### 问题

CH在reader返回后、最后region trim之前记录actual source/cache bytes和read
latency。当前demand路径:

```text
reader->next
actual size
reserve + writeCache(actual size)
trim到region剩余长度
IoStatistics使用trim后的size
```

例如source实际返回1 MiB、stream只剩100 KiB:

```text
source physical IO: 1 MiB
FileSegment write:  1 MiB
Next delivered:     100 KiB
当前read/raw统计:   100 KiB
```

demand和predownload也没有记录source read latency;warm/preload已有独立计时和统计,
四条路径的复制已经发生语义漂移。

#### 已批准方案

显式区分:

```cpp
const size_t actualBytes = reader.buffer().size();
size_t deliveredBytes = actualBytes;
```

使用:

```text
reserve/write/source IO/ProfileEvents -> actualBytes
region trim/Next返回/ScanTracker      -> deliveredBytes
```

source read计时和统计:

```text
IoStatistics::read(actualBytes)
rawBytesRead(actualBytes)
queryThreadIoLatencyUs
storageReadLatencyUs
ProfileEvents source bytes/microseconds
```

结合第11.3节复用 `ReadFileInputStream` 后:

- `ReadFileInputStream` 负责通用raw bytes/scan-time和 `FileIoContext`;
- FileCache只补source/cache/prefetch分类;
- 提取共享source-accounting helper,供demand、predownload、warm、preload调用;
- 删除四处重复/双计。

#### 必需测试

新增 `DemandTrimAccountsPhysicalRead`:

```text
预先存在1 MiB FileSegment
stream只请求其中100 KiB
source实际返回并填cache 1 MiB
Next返回100 KiB
IoStatistics::read/raw == 1 MiB
ScanTracker read == 100 KiB
```

latency不再断言微秒sum大于0,而断言记账路径执行:

```cpp
EXPECT_GT(storageReadLatencyUs().count(), 0);
EXPECT_GT(queryThreadIoLatencyUs().count(), 0);
```

避免快速本地read合法得到0微秒导致flake。

##### 实施结果(commit `cf8c6fb16`,filecache2,待 review,未 push)

按 §11.9 拆 `actualBytes`(const,物理未 trim)/ `deliveredBytes`(trim 后),
`readFromCurrentSegment`:

```text
actualBytes  -> reserve 前置校验 / reserve / writeCache / ProfileEvents
                (source+cache) / IoStatistics(source read()、cache-hit
                ssdRead+incRawBytesRead)
deliveredBytes -> region trim / buffer resize / VELOX_CHECK guard /
                recordReadBytes(ScanTracker) / 返回值
```

source 分支只 `read().increment(actualBytes)`,不加 `incRawBytesRead`(C2 后 base
`ReadFileInputStream::read` 已按物理读记 raw);cache-hit 分支 `ssdRead` +
`incRawBytesRead(actualBytes)`(裸 pread reader 无自动记账)。

测试(2 个,各锚定一条分支的 actual!=delivered,均 Controller 独立复现 RED):

```text
DemandTrimAccountsPhysicalRead(cache HIT):
  整段 4096 读出、region 只需 100 -> ssdRead/raw==4096,ScanTracker==100,
  source read()==0。
DemandTrimAccountsPhysicalSourceRead(cache MISS):
  boundaryAlignment=segSize 使 segment 对齐为 [0,4095],请求 {0,100} 的 download
  路径(REMOTE_FS_READ_AND_PUT_IN_CACHE, setReadUntilPosition(range.right+1))读到
  4096(actual)、trim 到 100(delivered)-> source read()/raw==4096,
  ScanTracker==100,ssdRead==0。
```

source MISS 的 actual>delivered 场景由 **offset 未对齐到 alignment left** 触发
(reviewer 洞察):segment 按 `boundaryAlignment` 对齐后大于请求区间,download 读到
segment 右界即多读。之前 `boundaryAlignment=1` 使 segment 贴合请求,故造不出差异。

Controller 独立验证:

```text
RED:分别中和 hit / source 分支的 actualBytes -> deliveredBytes,对应测试各自变红
  (source 分支中和后 read().sum() 记 100 而非 4096)。均已恢复。
全 8 gate 亲验:180(connector34/buffered_input35/e2e21/cancellation8/
  hit_metrics7/core_scc53/manager20/format_e2e2),-Werror 干净。
```

**source-read latency 未接**:base 已记 scan time,无干净 seam 加 storage/
queryThread latency 而不引入 flaky timer,§11.9 允许跳过。gluten cherry-pick 搁置
(最终一致性)。

**Status: C3 实施完成,Controller 验证通过,等外部 reviewer 复核。**

##### 外部Reviewer复核结论

`actualBytes` / `deliveredBytes` 拆分及hit/source两条trim测试正确:

```text
physical read / reserve / cache write / IO accounting -> actualBytes
Next return / ScanTracker                              -> deliveredBytes
```

但第11.9节已批准的demand/predownload source latency尚未实施。C2接回的
`ReadFileInputStream::read` 内部timer只增加:

```text
rawBytesRead
totalScanTimeNs
```

不会增加:

```text
queryThreadIoLatencyUs
storageReadLatencyUs
```

Velox Direct/Cached的标准做法是在 `input_->read` / `loadOrFuture` 外层再使用
`MicrosecondWallTimer`,将同一次IO等待记入不同指标。C3按同一模式:

```cpp
uint64_t readUs = 0;
{
    MicrosecondWallTimer timer(&readUs);
    result = state.reader->next();
}
```

source read成功后:

```cpp
ioStats->queryThreadIoLatencyUs().increment(readUs);
ioStats->storageReadLatencyUs().increment(readUs);
```

predownload循环中的source `eof` / `next` 同样计时。cache-hit不增加storage latency。

测试不要求微秒sum大于0;`IoCounter::increment(0)` 仍增加count,所以无flake断言:

```cpp
EXPECT_GT(queryThreadIoLatencyUs().count(), 0);
EXPECT_GT(storageReadLatencyUs().count(), 0);
```

```text
Status: REQUEST CHANGES
关闭条件: demand与predownload补齐source latency并通过count测试。
```

### 11.10 合并cache write异常处理

#### 问题

当前:

```text
downloadChunkIntoSegment
  reserve
  FileSegment::write
  typed errno分类
  ProfileEvents

FileCacheInputStream::writeCache
  FileSegment::write
  同一typed errno分类
  同一ProfileEvents
```

历史上已经先修 `downloadChunkIntoSegment`,后发现demand `writeCache` 仍是旧catch,
再补第二个commit。这是重复生产代码导致真实bug的直接证据。

#### 已批准方案

提取:

```cpp
bool writeSegmentChunk(
    FileSegment & segment,
    char * data,
    size_t size,
    uint64_t offset,
    bool skipOnDiskFailure);

bool reserveAndWriteSegmentChunk(
    FileSegment & segment,
    char * data,
    size_t size,
    uint64_t offset,
    uint64_t reserveTimeoutMs,
    size_t reserveHint,
    bool skipOnDiskFailure);
```

职责:

```text
writeSegmentChunk:
  FileSegment::write
  classifyCacheWriteError
  CachedReadBufferCacheWriteBytes

reserveAndWriteSegmentChunk:
  FileSegment::reserve(..., reserveHint)
  writeSegmentChunk
```

demand、predownload、warm、preload统一调用。不得引入 `std::function` 或测试callback。

##### 实施结果(commit `2f2628f04`,filecache2,待 review,未 push)

按 §11.10 + §11.5 实施。

§11.10 提取两个 free helper(无 `std::function`/callback):

```text
writeSegmentChunk(segment, data, size, offset, skip):
  write + 唯一一处 FileCacheErrnoException catch(classifyCacheWriteError)+
  CachedReadBufferCacheWriteBytes 计量。
reserveAndWriteSegmentChunk(..., reserveTimeoutMs, reserveHint, skip):
  reserve(size, timeout, reason, nullptr, reserveHint) + VELOX_CHECK_EQ offset +
  writeSegmentChunk。
```

`downloadChunkIntoSegment` 转发到 `reserveAndWriteSegmentChunk`(新增 `reserveHint`
参,**无默认值**,强制每个 caller 传);demand/predownload 直接调
`reserveAndWriteSegmentChunk`。**删除现已无 caller 的 `FileCacheInputStream::writeCache`**
(errno 逻辑全收口到 `writeSegmentChunk`,cache-write catch 全局仅剩一处,grep 确认)。

§11.5 各路径传 hint(`reserve` 内部 `read_horizon = current_downloaded_size +
reserve_hint`;granularity 会 balloon `size_to_reserve` 时,若 `read_horizon <
reserved_size + size_to_reserve` 则 clamp 到 `read_horizon - reserved_size`,
`FileSegment.cpp:629-633`):

```text
demand:      readUntilPosition - offset   (offset == currentWriteOffset,剩余到 region 末)
predownload: bytesToPredownload
warm:        writeTarget - writeOffset    (remaining)
preload:     segmentEnd - writeOffset     (remaining)
```

Controller 独立验证:

```text
diff 审查:确认 cache-write 的 FileCacheErrnoException catch 全局仅 1 处;四路径
  hint 表达式与"剩余 horizon"语义一致;demand offset 校验前移到 reserve 前(与 CH 一致);
  保留 C3 的 actualBytes(reserve/write 用 actual)。
Controller 修正 worker 违规:worker 把无 caller 的 writeCache 保留为 thin wrapper,
  违反"确定 unused 即删";已确认无其他 caller 后删除声明+定义。
RED 独立复现:reserve-horizon 测试传 reserveHint=0 -> reserved balloon 到 8MiB
  (granularity)变红(8388608 vs 1048576);hint=1MiB cap 到 1MiB。已恢复。
全 8 gate 亲验:181(connector34/buffered_input35/e2e21/cancellation8/
  hit_metrics7/core_scc54/manager20/format_e2e2),-Werror 干净,删 writeCache 无回归。
```

测试 `ReserveHintCapsReserveAheadToReadHorizon`(core_scc):granularity 8MiB /
horizon 1MiB / 首 chunk 64KiB,断言 `getReservedSize() <= 1MiB` 且完整读回 1MiB。
gluten cherry-pick 搁置(最终一致性)。

**Status: C4 实施完成,Controller 验证通过,等外部 reviewer 复核。**

##### 外部Reviewer复核结论

C4核心实现通过:

- cache-write `FileCacheErrnoException` catch仅剩一处;
- reserve/write helper边界清晰;
- demand、predownload、warm、preload四条hint公式正确;
- C3 `actualBytes` 继续用于reserve/write;
- `writeCache` 无caller后已删除;
- reserve horizon测试真实覆盖granularity clamp。

后续修C3时一并完成两处非阻塞清理。

1. demand外层当前比较:

   ```cpp
   fileSegment.getCurrentWriteOffset() == state.reader->getPosition()
   ```

   helper内部又比较:

   ```cpp
   segment.getCurrentWriteOffset() == offset
   ```

   将外层改为独立reader invariant:

   ```cpp
   static_cast<uint64_t>(state.reader->getPosition()) == offset
   ```

   从而两条分别证明reader position和segment write offset均等于请求offset。

2. predownload使用decrement前 `state.bytesToPredownload` 作为hint是正确的:它表示
   包含当前chunk在内的剩余horizon。补注释固定该顺序,防止未来将decrement前移后
   少算一个chunk。

```text
Status: APPROVE
上述两项随C3修复commit一起清理。
```

### 11.11 downloader lease改为RAII

#### 问题

`warmSourceGroup` 与 `fillFileSegmentsFromPreload` 重复:

```text
getOrSetDownloader
检查caller id
SCOPE_EXIT:
  resetRemoteFileReader
  completePartAndResetDownloader
```

这是有严格析构顺序的生命周期协议,不是普通样板。未来一处增加return、异常或交换
清理顺序,会产生lease泄漏或将task-local remote reader交给其他reader。

#### 已批准方案

在 `FileSegment` 层提供可移动RAII lease:

```text
auto lease = segment.tryAcquireDownloaderLease();
if (!lease)
    continue;
```

lease析构固定:

```text
resetRemoteFileReader
completePartAndResetDownloader
```

warm和preload整个函数不强行合并;只共享:

1. downloader lease RAII;
2. 第11.10节reserve/write helper;
3. `getOrSet` settings/alignment参数构造。

FileCacheInputStream demand路径后续也可复用该lease,但必须保证现有“一个 `Next`
一个downloader term”和remote reader reuse语义不变。

##### 实施结果(commit `8cdeb7169`,filecache2,待 review,未 push)

按 §11.11 实施。`FileSegment` 新增可移动 RAII `DownloaderLease`:

```text
成员 FileSegment* segment_ / bool engaged_;
operator bool() -> engaged_;copy 删除。
move ctor/assign 使 source disengage(segment_=nullptr, engaged_=false)-> moved-from
  不 double-release;move assign 先 release() 自身 + 自赋值 guard,均 noexcept。
release() 幂等:engaged 时先 resetRemoteFileReader() 后 completePartAndResetDownloader(),
  然后 disengage;disengaged no-op。dtor 调 release()。
tryAcquireDownloaderLease():getOrSetDownloader()==getCallerId() 则 engaged,否则 disengaged。
```

warm(`warmSourceGroup`)与 preload(`fillFileSegmentsFromPreload`)两处
`getOrSetDownloader + SCOPE_EXIT{reset;complete}` 改为
`auto lease = seg.tryAcquireDownloaderLease(); if (!lease) continue;`,删两处
SCOPE_EXIT,lease 作用域覆盖整个 per-segment body(dtor 在同一点触发),reset→complete
顺序保留,行为等价。两函数不合并。

测试 `DownloaderLeaseTest`(4,独立线程用不同 caller-id 验证 `getOrSetDownloader`
只在段真正空闲时当选):`NormalScopeExitReleases` / `ExceptionMidBodyStillReleases`
/ `NotElectedLeaseIsDisengaged` / `MovedFromLeaseDoesNotDoubleRelease`。

Controller 独立验证:

```text
RED:中和 release()(engaged 不释放)-> 前 3 测试泄漏红(独立 caller 抢不到 downloader),
  NotElected 仍绿(不依赖 release)。已恢复。
全 8 gate 亲验:185(...core_scc58...),-Werror 干净。
```

gluten cherry-pick 搁置(最终一致性)。

**Status: C5 实施完成,Controller 验证通过,等外部 reviewer 复核。**

##### 外部Reviewer复核结论

C5不应进入最终方案。

该commit的目标只是将warm/preload两处已经正确的:

```text
getOrSetDownloader
SCOPE_EXIT:
  resetRemoteFileReader
  completePartAndResetDownloader
```

抽成RAII,没有新增业务行为。为消除两处少量重复,却在CH核心
`FileSegment.{h,cpp}` 增加 `DownloaderLease`、move/release语义和新的public API,
违反本轮硬约束:

```text
CH FileCache核心保持平行port;
Velox integration适配不得下沉修改CH核心抽象。
```

处理:

```text
新增revert commit撤销8cdeb7169,不rebase/amend。
删除DownloaderLease / tryAcquireDownloaderLease。
删除4个DownloaderLeaseTest。
warm/preload恢复原getOrSetDownloader + SCOPE_EXIT。
```

两处显式协议保留在 `FileCacheBufferedInput.cpp`。不为去重增加local callback/template;
生命周期顺序显式展开更安全。

本轮七个commit对 `velox/ch/Interpreters/FileCache` 生产代码的审计:

```text
C1:删除此前全局test writer factory,恢复直接创建writer -> 正确,更接近CH
C5:新增DownloaderLease核心抽象                         -> 唯一新增CH偏离,必须revert
C2/C3/C4/C7:未修改CH核心生产代码
```

```text
Status: REVERT
```

### 11.12 有意保留的重复

#### `FileCacheLocalWriteFile`

保留。现有Velox `LocalWriteFile` 将syscall失败转换为不含结构化errno的
`VeloxRuntimeError`;外层无法可靠恢复 `ENOSPC`、`EDQUOT`、`EIO`。包装或解析
异常字符串均不可接受。

长期若Velox通用writer提供标准结构化POSIX异常,可再删除该adapter;本轮不改主干
异常ABI。

#### `ReadBufferFromVeloxReadFile`

保留CH cursor/working-buffer/read-until/remote-reader-handoff兼容层。按第11.3节
删除其“直接实现source IO context”职责,但不重写FileCache状态机。

#### syscall异常测试

positive short write、`EINTR`、failing `close` 不增加生产hook。依靠CH逐行对齐、
code review和正常路径真实文件测试。

### 11.13 实施顺序与commit边界

- [ ] **Commit 1: 恢复生产writer创建并提取errno策略**
  - 删除全局writer factory及test guards/custom writers。
  - 增加table-driven `classifyCacheWriteError` 测试。
  - 重写可由reserve/source failure表达的4个测试。

- [ ] **Commit 2: 接通source `ReadFileInputStream` / `FileIoContext`**
  - adapter新增source-input构造。
  - demand source切换并通过context测试。
  - warm/preload切换。
  - 同一commit删除由 `ReadFileInputStream` 已负责的raw bytes/scan-time手工计数,
    保证切换前后现有统计测试仍精确通过,不产生中间双计状态。
  - clone/lifetime测试。

- [ ] **Commit 3: 统一actual IO accounting**
  - 引入actual/delivered双变量。
  - 合并source accounting。
  - 增加 `DemandTrimAccountsPhysicalRead` 和latency count测试。

- [ ] **Commit 4: 合并reserve/write并接通 `reserve_hint`**
  - 引入 `writeSegmentChunk` / `reserveAndWriteSegmentChunk`。
  - 四条路径传精确hint。
  - 增加reserve horizon测试。

- [ ] **Commit 5: downloader lease RAII**
  - warm/preload改用统一lease。
  - 异常、取消、early return测试证明无lease leak。

- [ ] **Commit 6: 删除 `PreloadedRunStream`**
  - preload复用 `FileCacheInputStream`。
  - 统一published window和 `ScanTracker`。
  - preload/BackUp/seek tracking测试。

- [ ] **Commit 7: mixed-state planner**
  - `classifyChunk` 检查完整holder。
  - 增加hit-prefix/miss-tail warm测试。

- [ ] **Commit 8: 完整FileCache coalesced-load执行**
  - 删除未消费demand groups。
  - 提取共享planning policy。
  - 添加stream->load映射和 `FileCacheCoalescedLoad`。
  - prefetch主动执行,demand首个 `Next` 懒执行。
  - IO调用次数和format E2E验证。

每个commit独立RED/GREEN,不使用rebase/amend。相同commit顺序cherry-pick到
`filecache2-gluten`。

### 11.14 验收gate

最低gate:

```text
FileCache IO adapter tests
FileSegment/FileCache unit tests
FileCacheBufferedInput tests
FileCacheBufferedInputBuilder tests
FileCacheCancellation tests
FileCacheHitMetrics tests
FileCache E2E
DWRF + Parquet format E2E
release build tests
-Werror build
```

另外必须验证:

```text
生产代码无FileCache writer fault enum/cap/counter/global factory
FileIoContext三条source路径完整传递
reserve_hint所有reserve路径非默认0
preload/normal stream统一为FileCacheInputStream
无未消费demand sourceGroups
actual source bytes与delivered bytes分别记账
warm/preload异常或取消不泄漏downloader lease
```

硬约束gate:

```text
C5 DownloaderLease完整revert。
除C1删除历史global writer factory并恢复direct writer外,
本轮最终range不得修改velox/ch/Interpreters/FileCache生产代码。
C3/C4/C6/C7/C8全部改动必须留在Disks/IO或IO adapter层。
```

上述8个commit完成前,第七次整体review结论为:

```text
Status: REQUEST CHANGES
```
