# FileCache 可观测性设计 — `home-chang`

## Status

```text
environment_profile: home-chang
scope: FileCache 指标体系 — 监控链路（地图）+ 指标盘点（货单）+ 待填清单
decision_status: pending_user_review
baseline: Velox filecache2（HEAD 含 017 观测 / 018a 连接器 / 013 hasDefault）
```

只出设计与清单，**不改任何代码**。本文档两部分：
**第一部分（链路/地图）**回答"指标从哪走、能走到哪"（通用，任何 Velox 指标适用）；
**第二部分（指标/货单）**回答"我们 FileCache 的 59 个指标现状如何、哪些该填、能算什么、
走地图上哪条路"。第二部分依赖第一部分。

盘点核实方法：对每个枚举 grep 其真实 `increment`/`add`/`sub` 调用点（排除定义文件与
tests），用调用点数判定"已填 vs 空壳"。

## 0. 总览

### 0.1 Velox 的监控现状

Velox 有**四套互不统一的监控**：

1. **算子级 `runtimeStats`**（每算子/每查询的 key→值 map，查询后从 `TaskStats` 读）；
2. **`AsyncDataCache::CacheStats`**（整个缓存实例的命中/淘汰快照，`refreshStats` 拉）；
3. **`StatsReporter`**（进程全局注册表，push 式，推给外部后端）；
4. **DWIO `IoStatistics`**（读取器级 IO 计数，**汇进第 1 套算子 `runtimeStats`**）。

能透到 Spark（经 Gluten）的**只有算子级 `runtimeStats` 一条路**（第 4 套是喂它的支流）；
第 2 套 `CacheStats`、第 3 套 `StatsReporter` 都到不了（详见第一部分）。

### 0.2 ClickHouse 的指标设计（两类计数器）

我们移植的 59 个指标源自 ClickHouse 的两类计数器，语义、读法、聚合方式都不同——盘点与
消费必须区分。

- **ProfileEvents（累计事件计数，48 个）**：进程内**单调递增**的累计量
  （`std::atomic<uint64_t>`，只 `increment`）。**读法取差值**（区间量 = 末读数 − 初读数）。
  - `CachedReadBufferReadFromCacheBytes` = 累计从缓存读的字节；某查询命中量 = 查询前后
    快照相减。
  - `FilesystemCacheReserveAttempts` / `FailedReserveAttempts`：预留成功率 =
    `1 − ΔFailed / ΔAttempts`（比率须由两个累计**差值**相除，非单点）。
  - **可聚合**（跨线程/算子求和有意义）——这是它能进算子级 `runtimeStats` 的前提。
- **CurrentMetrics（瞬时量表，11 个）**：**可增可减**的瞬时状态量
  （`std::atomic<int64_t>`，`add`/`sub` 或 RAII `Increment` 网关）。**读法直接读当前值**。
  - `FilesystemCacheSize` = 当前占用字节；占用率 = `Size / SizeLimit`（两个**同时刻**
    瞬时值相除）。
  - `FilesystemCacheElements` / `CacheFileSegments` = 当前元素数 / 段数。
  - **不可累加**（瞬时值跨时间点求和无意义）——天然是全局量，不进算子级、到不了 Spark。

一句话选型：**流量/速率**（命中率、淘汰量、预留成功率）用 ProfileEvents 累计差值；
**状态**（占用率、当前规模）用 CurrentMetrics 瞬时直读。

### 0.3 当前移植状态与问题

- **绝大多数是空壳**：移植时把 CH 指标名照搬了 59 个，但真填（有 `increment`/`add` 调用点）
  的只有 **ProfileEvents 12 / 48、CurrentMetrics 4 / 11**；其余 43 个只有枚举名、无人写数。
- **命中率三件套（ReadFromCache/Source/CacheWrite Bytes）全是空壳**——这是"马上要填"的
  核心。填它时在读路径**同一处补全局 ProfileEvents + 记算子级 `IoStatistics`**，后者经
  Gluten 到 Spark（值天然一致，仅聚合范围不同）。
- **淘汰量/占用等全局量到不了 Spark**（`CacheStats` 不被 Gluten 消费、CurrentMetrics
  瞬时量不进算子级），留进程内 / 日志 / benchmark。

---

# 第一部分：监控链路（地图）

## 1. Velox 原生的四套监控

| 系统 | 作用域 | 读法 | 报缓存命中吗 |
|---|---|---|---|
| `RuntimeMetrics`/`runtimeStats`（算子级 map） | 每算子/每查询 | 查询后读 `TaskStats` → `printPlanWithStats` | 间接：读取器记的 `ramHit`（`CacheInputStream.cpp:222`） |
| `AsyncDataCache::CacheStats` | 整个缓存实例 | `refreshStats()` 拉快照（`AsyncDataCache.h:929`）；全局单例 `getInstance()`（:860） | 是：`numHit/numNew/numEvict/hitBytes` + SSD |
| `StatsReporter`（全局注册表） | 进程全局 | **push 式**，无拉快照（`fetchMetrics()` 返回不透明串，`StatsReporter.h:199`） | 否 |
| DWIO `IoStatistics`（读取器级） | 每读取流 | **汇进算子 `runtimeStats`**（`ramHit`/`storageRead` 等） | 部分：命中/回源字节 |

要点：`IoStatistics`（第 4 套）是喂给算子 `runtimeStats`（第 1 套）的支流——**两者其实
是同一条主干的两端**。原生缓存命中的权威快照口径是 `CacheStats`（pull）。

## 2. 透出到 Gluten / Spark（唯一通路）

- **能到 Spark（每查询/每算子）= 算子 `runtimeStats`**：读路径往手里的 `IoStatistics`
  打计数 → Velox 汇入算子 `customStats` → Gluten `WholeStageResultIterator::collectMetrics`
  原样序列化成 JSON 过 JNI → Scala 五层 → Spark SQL UI。**Gluten C++ 侧不丢任何
  customStat**（`WholeStageResultIterator.cc:488-503`）。
- **到不了 Spark**：
  - `CacheStats`：Gluten **不读**，仅关机 `LOG(INFO) << toString()`（`VeloxBackend.cc:464`）；
    全仓无 `refreshStats` 上报路。
  - `StatsReporter`：Gluten **没装后端**，push 全丢。
  - 我们的进程全局 `ProfileEvents`/`CurrentMetrics`：不接进算子 `runtimeStats` 就到不了。
  - Spark SQL UI **无"缓存全局快照"位置**；driver metrics system 那条 Gluten 未接缓存，
    要用需另造。

## 3. Scala 五层落地（新增一个算子级指标要改哪些）

Velox customStat key → Spark SQL UI，需改 Gluten Scala 5 处（以新 key `fileCacheXxx` 为例）：
1. `OperatorMetrics.java`：加 `public long fileCacheXxx;`（固定字段类，非 map）。
2. `MetricsUtil.scala` `operatorMetricFromJson`：`metrics.fileCacheXxx = customMetricSum(node, "fileCacheXxx")`（字符串须与 C++ key 一字不差）。
3. `VeloxMetricsApi.scala` 三个 scan 的 `*MetricsFull`：`"fileCacheXxx" -> SQLMetrics.createSizeMetric(sc, "filecache xxx")`（label = UI 显示文字）。
4. 三个 scan updater（FileSource/BatchScan/HiveTableScan）：capture + `ScanMetricsUtil.inc(...)`。
5. `ScanMetricsUtil` minimal set：不加则默认隐藏（需 `detailedScanMetricsEnabled`）。

**设计决定：走"专属字段"，不借原生 `ramReadBytes`。** 我们是磁盘缓存，借 `ramReadBytes`
语义歪（UI 显示"ram read bytes"）；专属字段语义准、标签对。原生 `ramReadBytes` 我们不打
→ 保持其 RAM 语义为 0（如实反映"没走 RAM 缓存"）。

---

# 第二部分：指标盘点（货单）

## 4. 马上要填（命中率三件套）——全部空壳

| 全局枚举（ProfileEvents） | 算子级字段（去 Spark） | 单位 | 现状 |
|---|---|---|---|
| `CachedReadBufferReadFromCacheBytes` | `fileCacheReadFromCacheBytes` | bytes | **空壳（0 调用点）** |
| `CachedReadBufferReadFromSourceBytes` | `fileCacheReadFromSourceBytes` | bytes | **空壳（0）** |
| `CachedReadBufferCacheWriteBytes` | `fileCacheWriteBytes` | bytes | **空壳（0）** |

**能算什么：**
- **命中率（字节口径，对齐 CH）** = `ReadFromCacheBytes / (ReadFromCacheBytes + ReadFromSourceBytes)`。
- **省下的远端 IO** = `ReadFromCacheBytes`；**冷启动/写放大成本** = `CacheWriteBytes`。
- **只报三个原始量，不报"率"字段**（累加量可聚合，率不能加；率由消费端相除）。

对齐 CH 权威口径 `src/Common/ProfileEvents.cpp:850/852/854`。CH 另有次数口径
`ReadFromCacheHits/Misses`（:844/845），我们**未移植**；字节口径已足够。

**记录点**：命中/回源判断在 `FileCacheInputStream` 读路径**同一处**发生一次 →
同处两行：`ProfileEvents::increment(...)`（填全局空壳）+ `ioStats_->...`（算子级去
Spark）。同处记录，值天然一致；差别仅聚合范围（全局累计 vs 算子隔离），非记录点不同。

## 5. 已填（真有 increment 调用点）

### ProfileEvents 已填 12/48
```text
淘汰过程（非量）：EvictionTries / EvictionReusedIterator /
  EvictionSkipped{Evicting,,Moving}FileSegments / FailedEvictionCandidates
段/后台：DowngradedFileSegments / BackgroundDownloadQueuePush /
  BackgroundRemovedInvalidatedEntries / HoldFileSegments /
  UnusedHoldFileSegments / CreatedKeyDirectories / FileSegmentFailToIncreasePriority
```
能算：淘汰压力画像（尝试 vs 跳过比）、SLRU 降级频率、后台下载排队。
**注意：是淘汰*过程*，不是淘汰*量*——量本身空壳（见 §6）。**

### CurrentMetrics 已填 4/11
```text
FilesystemCacheSize（占用字节）  FilesystemCacheElements（元素数）
FilesystemCacheInvalidatedElements  FilesystemCachePriorityQueueElements
```
能算：元素/失效画像。**占用率**需 `SizeLimit`（空壳）先填才能算。

## 6. 空壳（0 调用点）——按价值分层

### 6a. 高价值空壳（值得后续填；全局量，到不了 Spark）
```text
FilesystemCacheEvictedBytes / EvictedFileSegments        —— 淘汰真实量
FilesystemCacheBackgroundEvictedBytes / BackgroundEvictedFileSegments
FilesystemCacheSizeLimit（CurrentMetrics）               —— 缺它算不了占用率
FilesystemCacheReserveAttempts / FailedReserveAttempts   —— 预留成功率
FilesystemCacheKeys  CacheFileSegments                   —— 键/段规模
```
能算（填后）：真实淘汰量/压力、**占用率** = `Size/SizeLimit`、**预留成功率** =
`1 - Failed/Attempts`。归属 017 进程计数器 + 日志 + benchmark 进程内。

### 6b. 时延类空壳（诊断，优先级低）
```text
Reserve/GetOrSet/Get Microseconds、FileSegment{Wait,Write,Complete,Lock,IncreasePriority}Microseconds、
Evict/LoadMetadata Microseconds、各 *LockMicroseconds、CheckCorrectness(+Microseconds)
```

### 6c. 后台线程/杂项空壳（最低优先级）
```text
FreeSpaceKeepingThread{Errors,Run,WorkMs}、IdleClientEvictions、
InvalidatedEntriesCleanupThreadWorkMs、FailToReserveSpaceBecauseOfCacheResize
CurrentMetrics: HoldFileSegments / DownloadQueueElements / DelayedCleanupElements / ReserveThreads
```

## 7. 分层结论（算子级 vs 全局级）

| 类别 | 指标 | 归宿 | 现状 |
|---|---|---|---|
| **命中/回源/写盘（3）** | ReadFromCache/Source/Write Bytes | 算子级→Spark **+** 全局 | **空壳，马上填** |
| **淘汰量/占用/预留** | EvictedBytes/Segments、SizeLimit、ReserveAttempts | 全局（**到不了 Spark**） | 多空壳（6a） |
| **淘汰过程/段/后台** | EvictionTries/Skipped、Downgraded | 全局 | 已填 12 |
| **占用/元素瞬时** | Size/Elements | 全局 | 部分已填 4 |
| **时延/锁** | *Microseconds | 全局诊断 | 空壳（6b） |

## 8. 命中率的权威定义

```text
命中率(字节) = ReadFromCacheBytes / (ReadFromCacheBytes + ReadFromSourceBytes)
```
- 报原始量，不报率；率由 benchmark/Spark/看板相除。
- 三引擎对比：cbi 等价口径在 Velox `CacheStats`（numHit/hitBytes），fcbi 用上式；
  benchmark 层若求与 Spark 一致，三方都走算子级 `IoStatistics`。

## 9. 后续任务边界（不在本文档做）

- **可观测面对齐 task（独立，非阻塞）**：`FileCacheInputStream` 读路径一处填命中三件套
  全局 ProfileEvents + 记算子级 `IoStatistics`；Gluten Scala 五层（§3，专属字段）。
- **Task 018b（benchmark）**：三引擎端到端时间为主口径；缓存命中在 Velox 层直接读
  `IoStatistics`/ProfileEvents，不依赖 Gluten Scala。
- **高价值全局空壳（6a）**：淘汰量/占用率/预留成功率，单独一轮填，到不了 Spark，
  仅进程内/日志/benchmark。
- 时延（6b）、杂项（6c）：按需，最低优先级。
