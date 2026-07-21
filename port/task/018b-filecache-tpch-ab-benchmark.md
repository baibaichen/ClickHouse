# Task 018b: 移植 TPCH 三引擎 AbBenchmark，接 018a filecache 引擎

> **纯 Velox benchmark 任务。不碰 Gluten、不碰 ClickHouse 源、不碰 velox 主干。**
> 是 018a（连接器集成）的**端到端验证**：让真实 TPCH 查询经 Hive 连接器走到我们的
> FileCache。worker 读 `port/task/EXECUTION_PROTOCOL.md` + `ENVIRONMENT.md`（profile
> home-chang）。不 stage/commit/push。保留无关脏改动。

## Why（背景）

微基准（seek/wrapper）只能测缓存层裸开销，且 fcbi vs cbi 强行同层比无意义（cbi 是
驱逐式 RAM+SSD、fcbi 是磁盘持久缓存，算法不同）。**真正能回答 "fcbi vs cbi 谁快" 的是
TPCH 端到端** —— 让两种缓存各按自己算法在真实查询里发挥，且这**顺带验证了 018a**（TPCH
读真经连接器 builder 走到 `FileCacheBufferedInput`）。

前置已就绪：018a（`registerFileCacheBufferedInputBuilder`）已接受；命中三件套
ProfileEvents 已填（`f76379397`）→ fcbi 命中率可算。

## 要移植的源（ch-filecache → filecache2）

ch-filecache 分支有一套 A/B 对比 benchmark（~563 行），`--input_source=cbi/filecache/direct`
选引擎，端到端跑真实 TPCH SF100 parquet。用 `git show ch-filecache:<path>` 读，不切分支：
```text
velox/benchmarks/AbBenchmarkBase.{h,cpp}   velox/benchmarks/AbBenchmarkMain.{h,cpp}
（TPCH 套件：velox/benchmarks/tpch/TpchBenchmark.{h,cpp} + TpchBenchmarkMain.cpp）
```
数据集（已确认在）：`/home/chang/test/tpch-double/tpch-generated-100.0-parquet-decimal_as_double`（34G）。

### ch-filecache 原版关键事实（已核实，勿臆想）

- 三引擎**都不直接构造 BufferedInput**；靠设进程全局状态 + 跑真 TPCH plan，由连接器
  `createBufferedInput` 自己挑 reader（`dispatchAbMain`，AbBenchmarkMain.cpp:99-129）。
- **filecache**：`FLAGS_cache_gb=0` + `installFileCache()`（裸 `ch::FileCache::setInstance`）。
- **cbi**：`cache_gb>0` → QueryBenchmarkBase 建 AsyncDataCache。
- **direct**：两个 cache 都不装。
- 基类 `QueryBenchmarkBase`；TPCH plan 经 `TpchQueryBuilder`；stats 从 `taskStats()` 读。
- `--num_splits_per_file` 是 gflag（默认 10），**必须传 1**（默认 10 假读放大坑 fcbi）。

## 三处适配（关键：filecache 引擎改走 018a builder，不用裸单例）

- **(a) include 路径**：ch-filecache 用 `velox/common/caching/filecache/FileCache.h`。换成
  我们的 `velox/ch/Interpreters/FileCache/` + `velox/ch/Disks/IO/`。
- **(b) filecache 引擎接线改造**：ch-filecache 装裸 `ch::FileCache::setInstance` 单例；
  **我们改成**：建 FileCache-配置的 `FileCacheManager`（复用 wrapper benchmark /
  `FileCacheSeekBenchmark.cpp` 的 Manager 建法）+ `registerFileCacheBufferedInputBuilder(manager)`
  + 保证 `connectorQueryCtx->cache()==nullptr`（`cache_gb=0`，让 018a builder 选中 fcbi）。
  **禁止裸 `new FileCache` / 裸 `ch::FileCache::getInstance`。**
- **(c) 三引擎命中读取**：
  - **fcbi** → 读命中三件套 ProfileEvents 差值（`ProfileEvents::get(CachedReadBufferReadFromCacheBytes)`
    等，每查询前后快照相减）→ 命中率 = `ReadFromCache/(ReadFromCache+ReadFromSource)`。
  - **cbi** → 读原生 `AsyncDataCache::refreshStats()` 差值（`numHit/hitBytes`）。
  - **direct** → 无缓存列。
  - 命中列跨引擎不同源，表头标注"仅供诊断、口径不同"（design §8）。
- cbi、direct 两条路不依赖我们的 FileCache，近原样移植。

## 文件范围

放 `velox/ch/benchmarks/`（与其它 benchmark 一起，**不污染 velox 主干 `velox/benchmarks/`**）：
```text
创建: velox/ch/benchmarks/AbBenchmarkBase.{h,cpp}  velox/ch/benchmarks/AbBenchmarkMain.{h,cpp}
      velox/ch/benchmarks/TpchAbBenchmark.cpp（或合并，worker 择优）
修改: velox/ch/benchmarks/CMakeLists.txt（+ velox_ch_filecache_tpch_ab_benchmark target，
      链 velox_ch_filecache / velox_hive_connector / velox_exec / velox_exec_test_lib /
      velox_dwio_parquet_reader / velox_tpch_connector 等，照 ch-filecache 的
      velox_tpch_benchmark_lib 链表）
```
每新文件 Apache 2.0 头。**不碰** Gluten、ClickHouse 源。

### 已批准的单处 velox 主干破例（用户 2026-07-21，选甲）

允许修改**一个** velox 主干测试工具文件：
```text
velox/exec/tests/utils/TpchQueryBuilder.cpp  —— initialize() 增补"跳过 size=0 空文件"
```
**背景**：数据集是 Spark 写出的，每表目录含一个 size=0 的 `_SUCCESS` 空标记；filecache2
的 `TpchQueryBuilder::initialize` 只跳过 `.` 隐藏文件（`:117`），不跳过非隐藏的空文件，于是
把 `_SUCCESS` 当 parquet 读、无 footer → abort（三引擎皆炸，与 FileCache 无关）。
**ch-filecache 分支早有此补丁**（`git show ch-filecache:velox/exec/tests/utils/TpchQueryBuilder.cpp`
的 `initialize` 里 `if (dirEntry.file_size(sizeError) == 0) continue;`），是已验证的正解。
**破例理由**：(1) 是**测试工具**（`exec/tests/utils/`），非生产代码；(2) 补的是一个真实
缺陷（非法空文件应跳过，比要求用户清数据更健壮）；(3) 有 ch-filecache 先例、逐字可移植；
(4) 用户明确"不改数据"。**移植方式**：把 ch-filecache 的那段 `file_size(sizeError)==0 →
continue`（含 `error_code` 重载，避免 stat 失败抛出）逐字补进 filecache2 的 `initialize`，
位置在"跳过隐藏文件"之后、`readFileSchema` 之前。不改该文件其它任何逻辑。
**这是本 task 唯一允许的主干改动**；其余仍 velox 主干零改动。

## split=1 硬前提

所有 TPCH 命令**必带 `--num_splits_per_file=1`**（默认 10 制造假读放大、坑 fcbi）。
不改上游默认值（改默认动 velox 主干）。worker 验收/receipt 里所有 TPCH 命令都要带它。

## 验收 gate

- 构建 `velox_ch_filecache_tpch_ab_benchmark` exit 0。
- **冒烟跑**（小范围，别跑满 SF100×22×3——太慢）：挑 1-2 个 query、1 轮，三引擎
  （`--input_source=direct/cbi/filecache`）各 exit 0、出端到端时间。命令必带
  `--num_splits_per_file=1`；`--input_source=filecache` 时 fcbi 经 Manager + 018a builder。
- **fcbi 经 Manager/builder 证据**：grep 确认 benchmark 无裸 `new FileCache`、无裸
  `ch::FileCache::getInstance`/`setInstance`；filecache 路确实调
  `registerFileCacheBufferedInputBuilder`。冒烟时 fcbi 命中三件套 ProfileEvents 非零
  （证明 TPCH 真走了我们的缓存 = 018a 端到端通）。
- 既有 gate（e2e 17 / buffered_input 19 / manager 20 / core_scc 47 / observability 14 /
  cancellation 5 / connector 4 / hit_metrics 5）+ 两个已接受 benchmark 仍构建、不回归。
- velox 主干 diff：**仅允许** `velox/exec/tests/utils/TpchQueryBuilder.cpp` 的 size=0 跳过
  一处（见文件范围破例）；其余主干零改动（git diff 只应出现该文件 + `velox/ch/`）。不 push；
  无 -j；日志入 build 目录。数据集大（34G），冒烟用 `--query_id` 限定少量 query。
- **完整 SF100×22×3 正式跑数由用户手动**（太耗时，不在 worker 冒烟范围）；worker 只需
  证明框架能三引擎端到端跑通、fcbi 真走缓存。

## 若遇 blocker

若移植撞到未审依赖（TpchQueryBuilder / 某 velox 库链接 / cbi refreshStats API 差异等）
无既有审定映射，按 EXECUTION_PROTOCOL 依赖门槛停为 blocked 记录，不臆造/不加 shim。

## Result receipt

写 `port/task/result/018b-filecache-tpch-ab-benchmark-result.md`，EXECUTION_PROTOCOL
worker-receipt 格式：baselines、文件、命令（含 `--num_splits_per_file=1`）+exit+日志、
三引擎冒烟端到端时间、fcbi-经-Manager/builder 证据（grep + 命中非零）、split=1 已应用、
velox 主干零改动。

## 主干破例 2 — readFileSchema 按列名解析（用户 2026-07-21 授权移植）

第二处 velox 主干测试工具改动，同 `velox/exec/tests/utils/TpchQueryBuilder.cpp`：
`readFileSchema` 由**按位置 zip**（`std::transform(columns, fileType->names())` +
`types = fileType->children()`）改为**按列名解析**（对每个 TPCH 列 `fileType->findChild(column)`
取真实类型）。**背景**：Spark 生成的 TPC-H parquet 列序 ≠ dbgen 标准列序（如 `part`
的 `p_brand` 被挪到末尾），位置映射把列名绑到错的物理类型，导致 Q2 等 `like(INTEGER,VARCHAR)`
abort（22 条里 11 条崩）。**逐字移植自 ch-filecache**（同分支同函数已有此补丁，注释一致）。
**验证**：补丁前 q2/q4 signal 6（like(INTEGER)）；补丁后 q2 exit 0（12382098 行）、q4 exit 0。
性质同破例 1（size=0 跳过）：测试工具、补 Spark-数据兼容真实缺陷、有 ch-filecache 先例。
