# Tasks 003–010 Full Review — 裁决与交接

> 本文件是 Task 010 后强制 whole-port review 的裁决记录与交接说明。
> 面向两类读者：(1) 决定进 011/012 的 controller；(2) 后续复查本轮结论的 review agent。
>
> **执行方式**：A 段（Consumer 合同台账）+ D 段（source-contract 清扫）各派一个只读 agent，
> 本轮特别强调**实现对齐**（guide §3 结构直译），不只是外接口语义对齐。用户逐条裁决。
> 本轮**不建 task、不派 worker、不改实现**。
>
> 关联工件：
> - A 台账（含结构偏离台账 + 签字列）：[`003-010-consumer-contract-ledger.md`](003-010-consumer-contract-ledger.md)
> - D 清扫原始报告：[`003-010-full-review.md`](003-010-full-review.md)
>
> CH 基线：`ch-filecache` @ da28e83e8b3 · Velox 基线：`filecache` @ 89039901a
> 裁决日期：2026-07-19

---

## 0. 一句话结论

进 011/012 的 gate：**R1、R2、R6–R10 已由用户签字**（记入结构偏离台账），**R3 是上线前必修的 release blocker 但不阻塞 011/012 开发**，**R4/R5 推迟到 019 之后**，**R11 待随手清理**。没有任何一项要求在动 011/012 之前改实现。

---

## 1. 每个问题的背景（大白话）与处理意见

### R1 — 009 `ShardedMap` 用了 `F14FastMap` 而非 `std::unordered_map`

**背景**：CH 的 `std::unordered_map` 是链式（node-based）容器——插入删除别的元素时，已有元素的**内存地址不变**。Velox 换成了 `folly::F14FastMap`，它 rehash（扩容）时会**搬移 value**，地址会变。这两者的「保证」不同，是 guide §3 明确要抓的那类「行为看着一样、内部结构不一样、会在后续 consumer 悄悄对不齐」的偏离。而且它**不是被硬约束逼的**——Velox 里 `std::unordered_map` 照样能编译。

**为什么现在没炸**：`ShardedMap` 在 CH 里唯一的使用者是 `CacheMetadata::getOrCreateSharedOrigin` / `removeSharedOrigins`（`Metadata.cpp:105-130`）。它 `withShard` 里 `it->second` **立即拷贝出 `shared_ptr` 返回**，不把迭代器或 `Value&` 带出回调；`forEachShard` 的迭代器也只在单次回调内用完。所以 **CH 从不依赖 node 地址稳定**——F14 搬 value 对现有 consumer 无害。

**处理意见（用户裁决）**：**保留 F14 + 签字锁不变式**。因为 CH consumer 已被证明不需要稳定性，保留 F14 合理。但 **011/012 的 review 必须强制一条不变式**：任何 consumer 不得让 `withShard`/`forEachShard` 的 `Value&` 或迭代器逸出回调、跨 mutation 使用。这条写进了结构偏离台账 009-1 签字列，是 011/012 review 的硬检查项。

### R2 — 010 `FileCacheSettings` 丢了每字段的 `.changed` 标记

**背景**：CH 的 settings 每个字段都带一个「这值是用户显式配的，还是走默认」的标记（`.changed`）。Velox 简化成只给 3 个字段（path / max_size / ratio）留了 presence 标记，其余约 35 个字段分不出「配了 0」还是「默认 0」。D agent 担心 011/012 读到第 4 个字段的 presence 时会静默失效。

**核实后修正**：查了 CH 真实代码，这个担心**不成立**。`.changed` 在 CH file cache 里只影响一条路径——**dynamic resize（在线改配置）**。而这条路径的真实实现（`FileCache.cpp:2800 applySettingsIfPossible`）**根本不用 `.changed`，它用逐字段值比较 `new_settings[X] != actual_settings[X]`**。`.changed` 真正被读的地方只有 validate 阶段的 `FileCacheSettings.cpp:185/231/237/240/243/266`，且**全是 path/max_size/ratio 这 3 个**——正好是 Velox 已覆盖的。

**处理意见（用户裁决）**：**签字接受，不 reopen**。恢复每字段 presence 反而是**过度移植**（CH 自己都不用）。**约束**：012 移植 `applySettingsIfPossible` 时必须用逐字段值比较，禁止依赖字段 presence。记入台账 010-1。

### R3 — 004 StatusFile 缺失崩溃恢复诊断（⚠️ 上线前必修）

**背景**：每个 cache 目录下有个 `status` 文件，两个作用：(1) **目录锁**——flock 防止两个实例用同一 cache 目录踩坏数据；(2) **崩溃检测**——如果启动时发现 status 文件已存在，说明上次没干净退出，CH 会**先读出旧内容、打一条 unclean-restart 日志**（含上次的 pid/版本），告诉运维「上次是崩的」。

**Velox 的问题**：核对后发现，准确说法**不是**「truncate 顺序反了」，而是 **Velox 构造函数（`StatusFile.cpp:93-131`）整段崩溃检测分支根本没移植**——它直接 open→flock→ftruncate→写，从来没有「读旧内容」这一步。CH 的诊断分支在 `StatusFile.cpp:50-63`。

**触发场景**：唯一触发条件是**上次进程没干净退出**（kill -9 / OOM / 崩溃 / 断电），status 文件残留，这次启动。正常关闭时析构会 `unlink` 删掉文件，下次启动干净、不触发。

**影响分层**：
- **目录锁（防双实例）**：Velox flock（`StatusFile.cpp:114`）**已对齐，没丢**。核心安全功能没问题。
- **崩溃诊断日志**：**完全缺失**。触发时后果是「少一条运维日志」——不影响正确性、不影响数据安全，但生产环境崩溃重启后运维等于盲重启，无法判断服务器是否崩过、缓存状态是否可信。

**接口影响**：**无**。要补的是构造函数**内部**「open 前先读旧内容 + 打日志」一段，不改构造签名 `StatusFile(std::string, FillFunction)`、不加 public 方法、不改析构。唯一 consumer（012 `FileCache.cpp:517` `make_unique<StatusFile>(path, writeFullInfo)`）调用方式一个字不用改。
- 附带核实了 `FillFunction` 签名从 CH `std::function<void(WriteBuffer&)>` 变成 Velox `std::function<void(int fd)>`（台账 004-4）——grep 确认 CH 全库无任何 consumer 自写 FillFunction，file cache 只用内置 `write_full_info`，故这是 consumer 看不见的等价基础设施替换，**不算偏离**。

**处理意见（用户裁决）**：**上线前必修（release blocker）**，但**推迟**——不阻塞 011/012 开发。理由：StatusFile 是叶子，011/012 是 metadata/priority/core，R3 完全不碰 SCC，也不改接口。需补：构造期加回 CH `:50-63` 的读旧内容 + unclean-restart 日志分支（`ftruncate` 移到读之后），逐字复现 CH 诊断文本（`StatusFile.cpp:60,62,76,90`）；RED 测试用非空已存在 status 文件断言 unclean-restart 日志，false-green 探针删 emit 行验红。记入台账 004-1。

### R4 — 008 `sipHash128` 位一致性未用 CH 真值验证（推迟到 019 后）

**背景**：CH file cache 把缓存文件按 `sipHash128(路径)` 的 hex 存成磁盘目录名（`FileCacheKey::fromPath`）。这个哈希**写进了磁盘**。Velox 重新实现了一份 `sipHash128`，如果和 CH **哪怕差一位**，CH 写的所有 cache 目录名 Velox 全对不上 → 磁盘上所有缓存**静默失效、全部重下**。这是持久化硬约束，要求逐字节相同。

**问题**：Velox 实现是**忠实转写**（种子对、连 CH 特有的 `v2 ^= 0xff` 收尾都照抄，`SipHash128.cpp:118`），看起来对。但现有 golden 向量测试的期望值**可能是跑 Velox 自己的实现生成的**（receipt 只验了「32位小写 hex」格式，不是值），若如此则是**自证循环**，证明不了和 CH 一致。

**处理意见（用户裁决）**：**推迟到 019 之后**。理由：影响局部（最坏是缓存全 miss / 重下，是性能事故不是正确性事故），且不需要读 CH 实现，与 CH 一致只是**便于后续排查问题**的运维收益，不阻塞 011/012。需补（019 后）：用 **CH 计算**的期望值做 golden 向量测试（记下 CH 命令/fixture），`0xff→0xee` 收尾探针验红。记入台账 008-4。

### R5 — 008 malformed-char 解析 parity 未做 CH 差分（推迟到 019 后）

**背景**：CH 的 key 解析器是「长度严格、hex 宽松」——长度必须 32，但非 hex 字符不拒绝、走 hex 表映射（invalid→0）。Velox 重写了这个逻辑（非 hex→`0xFF` + 加法复现溢出）。现测试的 oracle 是实现自己的注释推理，不是对 CH `unhexUInt<UInt128>` 的差分。

**处理意见（用户裁决）**：**推迟到 019 之后**（与 R4 同批）。需补：`'g'`、混合大小写、全非 hex 输入的差分 fuzz，以 CH 为 oracle。记入台账 008-2。

### R6 — 007-2 写失败无 errno → 按物理文件大小 reconcile（签字）

**背景**：CH 写缓存失败时能拿到 errno（ENOSPC/EDQUOT 等）分类。Velox 的 `WriteFile` **不暴露 errno**，只能按写完后的物理文件大小（`fs::file_size`）来核对实际写了多少。这是**唯一有硬约束背书**的偏离——§E 探针证实 Velox 原语确实拿不到 errno。

**处理意见（用户裁决）**：**签字接受**。这是平台原语的硬约束，reconcile-by-physical-size 是唯一可行手段。记入台账 007-2。

### R7 — 003-1 所有 CH 错误码坍缩成单一 `VELOX_FAIL`（签字）

**背景**：CH 有 typed ErrorCodes（LOGICAL_ERROR / BAD_ARGUMENTS / NOT_ENOUGH_SPACE / …），Velox 全部经 `throwFileCacheException` 坍缩成一个 `VELOX_FAIL`，错误码身份丢失。114 处 throw、7 种码。

**处理意见（用户裁决）**：**签字接受**（有意为之，规模大）。**约束**：任何 011/012 路径若需要区分 `NOT_ENOUGH_SPACE` vs `LOGICAL_ERROR`（例如 reserve 失败要走驱逐还是报逻辑错），必须在那个 call site 重新引入 typed subtype。记入台账 003-1。

### R8 — 006-2 丢弃 scheduler 的 `activate`-revive 语义（签字）

**背景**：CH 的 `BackgroundSchedulePool` task 支持 `deactivate` 后再 `activate` 复活。Velox 用 per-task `folly::Future` timer 替代中央 multimap timer，**没有 activate 复活**。

**处理意见（用户裁决）**：**签字接受**——CH file cache 的两个真实 task（`background_cleanup` / `keep_up_free_space`）只用 `schedule`/`scheduleAfter`/`deactivate`，无一依赖 activate-revive。记入台账 006-2。

### R9 — 009-2 丢弃 ShardedMap 的锁等待 telemetry（签字）

**背景**：CH 用 `ProfiledMutexLock` 记录每个分片锁的等待耗时。Velox 用普通 `std::unique_lock`，`lock_wait_event_` 成员存了但从没读。

**处理意见（用户裁决）**：**签字接受**——011 不需要 lock-wait telemetry。附带：那个从没读的 `lock_wait_event_` 成员归入 R11/O4 随手清理。记入台账 009-2。

### R10 — 010-2 Velox 新增 allowed-root 路径授权（签字）

**背景**：CH 没有的东西——Velox 在 settings 里**加了**一层「cache path 必须在允许的根目录下」的授权检查（`FileCacheSettings.cpp:211-263`）。

**处理意见（用户裁决）**：**签字接受**——确认是有意的附加安全策略，不是 scope creep。记入台账 010-2。

### R11 — Over-port 待清理（不阻塞）

无 003–010 调用点的多余实现，随手清理即可，不阻塞任何事：
- **O1**：`FileCacheBoundedQueue::tryPop(T&, ms)` 定时重载——无 CH 调用点
- **O2**：const-ref `tryPush` 重载——CH 只用右值形式
- **O3**：`FileCacheUtils.h:58` `checkedAdd`/`VELOX_FAIL`——013/014 才有 consumer，暂 flag
- **O4**：`ShardedMap` 的 `Hash` 模板参数 + 从没读的 `lock_wait_event_` 死成员
- **O5**：`FileCacheScheduler::setCallback`——test-only，无生产 consumer

**注意非 over-port（有意不移植，别误删）**：`dumpToSystemSettingsColumns` / 结构化 system-table 输出（010，设计明确不移植）；`LOG_FATAL`（003，CH file cache 无此调用点）。

---

## 2. 已核实对齐的关键内部结构（供复查）

这些是 011/012 会直接依赖、本轮确认**已正确移植**的内部结构：

- **锁顺序**：CH `Guards.h:53`（`CachePriority > CacheMetadata > Key > FileSegment`；`CacheState` 独立）在 Velox `Guards.h:29-43` **逐条复现**。`CacheStateGuard::tryLockFor` 在 Velox `Guards.h:107` 存在（timed-reserve 路径不是 hole）。
- **三阶段 shutdown**：CH `FileCache.cpp:2666-2684`（shutdown.store → join load-metadata → deactivate scheduler → `eviction_pool->wait()` → `metadata.shutdown()`）与 **cancel-before-join**（CH `Metadata.cpp:1030-1042` 先 cancel 两个队列唤醒线程再 join）——在 CH 侧确认，将由 **012 在 `FileCache.cpp`/`Metadata.cpp` 内重新实例化**（叶子只提供原语）。这是 012 review 的必查项。
- **immediate > delayed**：CH 语义在 Velox `FileCacheScheduler.cpp:152-153` 复现（`scheduleAfter` 在 `pendingImmediate_` 时返回 false）+ coalescing（`:119`）+ Running→pendingImmediate（`:123`）。

---

## 3. 进 011/012 的 gate 状态

| 前置 | 状态 |
|---|---|
| R1、R2、R6–R10 签字记入结构偏离台账 | ✅ 已完成（本文件 + 台账签字列） |
| R3 登记为 release blocker，明确不阻塞 011/012 | ✅ 已登记（台账 004-1） |
| R4/R5 登记为 019 后任务 | ✅ 已登记（台账 008-2/008-4） |
| R11 登记为待清理 flag | ✅ 已登记 |
| 无需在动 011/012 前改实现 | ✅ 确认（R3 不碰 SCC、不改接口） |

**结论**：本轮 whole-port review 的 findings 均已由用户裁决闭环。011/012 可在下述约束下推进：
1. **011/012 review 强制 R1 不变式**：无 `Value&`/迭代器逸出 `withShard`/`forEachShard`。
2. **012 遵守 R2 约束**：`applySettingsIfPossible` 用逐字段值比较，不依赖 presence。
3. **012 遵守 R7 约束**：需区分 `NOT_ENOUGH_SPACE` 的 call site 重引入 typed subtype。
4. **012 review 必查**：三阶段 shutdown + cancel-before-join 在 SCC 内正确重实例化。
5. **上线前**：R3 必修。**019 后**：R4/R5 补测。**随手**：R11 清理。

---

## 4. 给复查 agent 的说明

- 本文件的裁决是**用户逐条拍板**的结果，不是 agent 自主判断；若要推翻某条裁决，需重新交给用户。
- 每条裁决的 CH 依据都带了 file:line，可独立复核。特别是 R2（`.changed` 只影响 dynamic resize、且该路径用值比较）和 R3（Velox 整段崩溃诊断分支缺失、但 flock 已对齐、接口不变）——这两条纠正了 D agent 原报告的措辞，复查时以本文件为准。
- 结构偏离台账的签字列是权威签字记录；本文件是背景与理由。两者不一致时，以台账签字列为裁决、本文件为解释。
