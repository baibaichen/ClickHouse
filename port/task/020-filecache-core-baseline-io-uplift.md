# R1–R5 Implementation Plan (FileCache 核心回基线 + Velox IO 功能上移)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按权威方案(设计文档 `port/design/filecache-buffered-input-review-remediation.md` 第 12 节,最新接口修正commit `7567b7c2371`)把 async prefetch/warm 功能从 CH FileCache 核心上移到 Velox 标准 `CoalescedLoad` 模型,并把 CH FileCache/FileSegment 核心恢复到基线 `5785a43a`,只保留默认 writer factory 一处改动。

**Architecture:** load 层不自己做 IO —— `FileCacheCoalescedLoad::loadData` 为每个 request 建一个内部 `FileCacheInputStream` 循环 `Next` 完成真实 IO,downloader election / reserve / write / errno / 统计全部复用既有 stream,零拷贝靠 `takeLastOutputBuffer` move pool-backed `outputBuffer_` → request → 业务 stream。

**Tech Stack:** C++ (Velox), gtest, ninja, `cache::CoalescedLoad`, folly (`Baton`/`SemiFuture`/`Synchronized`/`F14FastMap`), `TestValue` fault injection。

---

## 全局执行纪律(每个 commit 都适用)

- **仓库**:`/home/chang/OpenSource/velox`,分支 `filecache2`。gluten 本轮全程搁置,最后一次性 cherry-pick 到 `filecache2-gluten`。
- **build 目录**:`cmake-build-debug-gcc13`。
- **ninja**:`/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja`(**不加 `-j`,不用 `nproc`**)。
- **跑测试前** export:
  ```bash
  export LD_LIBRARY_PATH="/home/chang/OpenSource/velox/cmake-build-debug-gcc13/icu-bld/lib:/home/chang/OpenSource/velox/cmake-build-debug-gcc13/icu-bld/stubdata:$LD_LIBRARY_PATH"
  ```
- **8 gate**:`velox_ch_filecache_connector_test`、`velox_ch_filecache_buffered_input_test`、`velox_ch_filecache_e2e_test`、`velox_ch_cancellation_test`、`velox_ch_filecache_hit_metrics_test`、`velox_ch_filecache_core_scc_test`、`velox_ch_filecache_manager_test`、`velox_ch_filecache_format_e2e_test`(可能需 `chmod +x`)。
- **构建/测试输出**一律重定向到 build 目录的唯一日志文件,用 subagent 分析日志只回摘要。
- **Controller 协议**:worker 干活 → Controller 亲验 + 全 8 gate 回归 + 审 diff 抓夹带 → commit。**不轻信 worker 的 GREEN。**
- **验证方式分两类,不可混用**:
  - **R2 / R5(引入新行为)**:RED-first TDD。worker 先写失败测试,Controller **独立复现 RED**(中和对应生产逻辑亲眼看红)再验 GREEN。
  - **R1 / R4(纯迁移 / 恢复基线)**:**没有合理的 RED,不得为流程人为破坏生产代码**。验证=迁移/恢复后**全 gate 回归绿 + `git diff` 收敛到预期**(R1 零生产改动;R4 core diff 收敛到 12.8 预期)。迁入的 case 在新 binary PASS、迁走的 case 在旧 binary 消失,即为通过。
- **可直接执行范围**:R1–R5 均已细化到 step 级(带核实过的 file:line),**唯一例外是 R2-6 —— 它是测试矩阵 checklist(每条 material case 作为单独命名 gtest 落地),不是 code-level 生产 step**。但按批准边界,**每个阶段开工前 Controller 仍须核对锚点是否因前序 commit 漂移**(尤其 R2 落地后 `FileCacheInputStream`/`FileCacheBufferedInput` 的行号会变,R3/R4/R5 引用的行号要重新定位)。R2 因引入全新接口,拆成 6 个分子任务(R2-1…R2-6)逐块 RED/GREEN。
- 规矩:C++ 不用 `sleep` 修竞态(用 `folly::Baton` / `executor.join` / `TestValue`);Allman braces;文件末尾留换行(`tail -c1 | xxd` 末字节 `0a`)。
- **文档/代码默认不 push**,仅用户明确要求时 push。分支上**不 rebase / 不 amend**,只加新 commit。

## 硬顺序依赖(从代码核实,不可乱序)

```
R1 ──▶ R2 ──▶ R3
              └─▶ R4 ──▶ R5
```

- **R4 必须晚于 R2**:R4 删 core 的 `submitWarm`/`inflightWarmForTest`/`warm_*`,而当前 `FileCacheBufferedInput::warmSourceGroup`(`FileCacheBufferedInput.cpp:598`)仍是这些 core API 的唯一 caller。R2 用 `FileCacheCoalescedLoad` 替换 `warmSourceGroup` 切断 caller 后,R4 才能安全删。
- **R3 依赖 R2**:R3 的 wait 并发测试要靠 R2 引入的 `CoalescedLoad::loadOrFuture` 才能建立确定性时序。
- **R5 晚于 R4**:C3/C4/C6/C7 都在 Disks/IO+IO,C7 的 mixed-state classifier 语义要在 core 恢复基线后才稳定。

## `takeLastOutputBuffer` buffer 生命周期契约(用户确认,R2 必须 RED 锁死)

现有 `FileCacheInputStream::Next` 在返回前已 detach reader、释放 downloader、把窗口游标推进到末尾 → 支持随后 move。`takeLastOutputBuffer` **必须同时清空窗口元数据**(`outputBufferStart_`/`offsetInOutputBuffer_`/`outputBufferSize_`),且此后**禁止对同一内部 stream 调 `BackUp`**。move 后下一次 `Next` 必须分配/复用新 buffer,不得读到被移走的旧 buffer。

---

## Commit R1: 测试归位,不改生产行为

**Goal:** 把只测 IO 层组件的 case 迁到对应层,把 reserve-hint core case 改为直接调 core API,删掉不再需要的 test util link。**零生产代码改动。**

**Files:**
- Modify: `velox/ch/IO/tests/IoAdaptersTest.cpp`(接收 writer tests)
- Modify: `velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp`(接收 errno policy test)
- Modify: `velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp`(删出 3 个迁走的 case;改写 reserve-hint case)
- Modify: `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt:101`(删 `velox_file_test_utils`,仅当 core test 不再用 `FaultyWriteFile`)

**已核实的代码事实(务必照此,不要凭 R2/其它记忆):**
- `FileCacheLocalWriteFile.h` 真实路径是 **`velox/ch/IO/FileCacheLocalWriteFile.h`**(不是 `Interpreters/FileCache/`)。
- `FileCacheErrnoException.h` 在 `velox/ch/Interpreters/FileCache/FileCacheErrnoException.h`。
- `TempDirectoryPath` 来自 **`velox/common/testutil/TempDirectoryPath.h`**(`using common::testutil::TempDirectoryPath;`)——`IoAdaptersTest.cpp` 现无此 include,必须补;它的 `velox/common/file/File.h` **不**提供 `TempDirectoryPath`。
- `velox/ch/IO/tests/CMakeLists.txt` 的 `velox_ch_io_test` **已 link `velox_ch_filecache`**,该库已提供 `FileCacheLocalWriteFile`/`FileCacheErrnoException`/`classifyCacheWriteError` 符号 → **R1 无需改任何 test CMake 的 link**(除删 `velox_file_test_utils`)。

### 待迁移/改写清单(全部核实过 file:line)

| case | 当前位置 | 目标 | 动作 |
|---|---|---|---|
| `FileCacheLocalWriteFileTest.AppendOrFlushAfterCloseIsNonErrnoLogicError` | `FileSegmentTest.cpp:262` | `IoAdaptersTest.cpp` | move(只测 `FileCacheLocalWriteFile`) |
| `FileCacheLocalWriteFileTest.NormalAppendResumeAndSize` | `FileSegmentTest.cpp:310` | `IoAdaptersTest.cpp` | move |
| `CacheWriteErrorPolicyTest.ClassifiesByErrnoAndSkipFlag` | `FileSegmentTest.cpp:345` | `FileCacheBufferedInputTest.cpp` | move(测 `classifyCacheWriteError`,已 include `FileCacheInputStream.h`) |
| `FileSegmentDownloadTest.ReserveHintCapsReserveAheadToReadHorizon` | `FileSegmentTest.cpp:555` | 留在 `FileSegmentTest.cpp` | **改写**:`reserveAndWriteSegmentChunk` → 直接 `segment.reserve()`+`segment.write()` |

- [ ] **Step 1: 迁 writer tests 到 `IoAdaptersTest.cpp`**

在 `IoAdaptersTest.cpp` 顶部 include 区(现有 `#include "velox/ch/IO/WriteBufferFromVeloxWriteFile.h"` 后)补:
```cpp
#include "velox/ch/IO/FileCacheLocalWriteFile.h"
#include "velox/ch/Interpreters/FileCache/FileCacheErrnoException.h"
#include "velox/common/testutil/TempDirectoryPath.h"
```
把 `FileSegmentTest.cpp:262-333`(`AppendOrFlushAfterCloseIsNonErrnoLogicError` + `NormalAppendResumeAndSize` 两个 `TEST` 完整块,含中间注释)整体移到 `IoAdaptersTest.cpp` 的匿名 namespace 内(现有 tests 之后)。两个 case 用 `TempDirectoryPath::create()`/`fs::path`/`std::ifstream`/`std::istreambuf_iterator`;`TempDirectoryPath` 由上面新 include提供(`File.h` 不含它)。当前文件缺少这些声明，确定性增加 `#include <filesystem>`、`#include <fstream>`、`#include <iterator>`、`using common::testutil::TempDirectoryPath;` 和 `namespace fs = std::filesystem;`。

- [ ] **Step 2: 迁 errno policy test 到 `FileCacheBufferedInputTest.cpp`**

把 `FileSegmentTest.cpp:345-371`(`CacheWriteErrorPolicyTest.ClassifiesByErrnoAndSkipFlag` 完整 `TEST` 块)移到 `FileCacheBufferedInputTest.cpp` 匿名 namespace 内。该 case 用 `ENOSPC`/`EDQUOT`/`EIO`/`EACCES` —— 补 `#include <cerrno>` 若缺。`classifyCacheWriteError`/`CacheWriteErrorAction` 来自已 include 的 `FileCacheInputStream.h`,无需新 include。

- [ ] **Step 3: 改写 reserve-hint core case 为直接 core API**

在 `FileSegmentTest.cpp:555` 的 `ReserveHintCapsReserveAheadToReadHorizon` 中，分别改写
首块和循环写入。

首块从 `reserveAndWriteSegmentChunk` 改为:
```cpp
ASSERT_TRUE(segment.reserve(
    kFirstChunk,
    /*lockWaitMs=*/100,
    reason,
    /*reserveStat=*/nullptr,
    /*reserveHint=*/kHorizon));
segment.write(
    data.data(),
    kFirstChunk,
    segment.getCurrentWriteOffset());
```

循环中的每块改为:
```cpp
ASSERT_TRUE(segment.reserve(
    chunk,
    /*lockWaitMs=*/100,
    reason,
    /*reserveStat=*/nullptr,
    /*reserveHint=*/kHorizon - written));
segment.write(
    data.data() + written,
    chunk,
    segment.getCurrentWriteOffset());
```
复用case中已有的 `std::string reason;`。断言不变(`getReservedSize() <= kHorizon`、逐字节回读)。这是迁移改写,无 RED:改写后该 case 在 core binary PASS、行为等价即可。

- [ ] **Step 4: 清 `FileSegmentTest.cpp` include / CMake**

删掉因迁走 3 个 case 而不再被引用的 include(逐个 grep 确认无其它 case 使用后再删,例如若 `FileCacheLocalWriteFile.h` 仅那两个 case 用到)。检查 `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt:101` 的 `velox_file_test_utils`:仅当 `FileSegmentTest.cpp` 及同 target 其它 core test **不再引用 `FaultyWriteFile`** 时才删(先 `grep -rn FaultyWriteFile velox/ch/Interpreters/FileCache/tests/` 确认)。

- [ ] **Step 5: 构建**

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C cmake-build-debug-gcc13 \
  velox_ch_io_test \
  velox_ch_filecache_buffered_input_test \
  velox_ch_filecache_core_scc_test \
  > cmake-build-debug-gcc13/build_r1_test_relocation.log 2>&1
```
用 subagent 分析日志,预期:link 成功,无 undefined reference。

- [ ] **Step 6: 分别运行三个 binary,验证迁移后覆盖不减少**

```bash
export LD_LIBRARY_PATH="/home/chang/OpenSource/velox/cmake-build-debug-gcc13/icu-bld/lib:/home/chang/OpenSource/velox/cmake-build-debug-gcc13/icu-bld/stubdata:$LD_LIBRARY_PATH"
cmake-build-debug-gcc13/velox/ch/IO/tests/velox_ch_io_test \
  > cmake-build-debug-gcc13/test_r1_io.log 2>&1
cmake-build-debug-gcc13/velox/ch/Disks/IO/tests/velox_ch_filecache_buffered_input_test \
  > cmake-build-debug-gcc13/test_r1_buffered_input.log 2>&1
cmake-build-debug-gcc13/velox/ch/Interpreters/FileCache/tests/velox_ch_filecache_core_scc_test \
  > cmake-build-debug-gcc13/test_r1_core_scc.log 2>&1
```
subagent 分析:两个迁入的 writer test + 一个 errno test 在新 binary 里 PASS;core binary 仍有改写后的 `ReserveHintCaps` PASS;三个迁走的 case 在 core binary 里消失(不再出现)。baseline `PartialPhysicalAppendFailureReconcilesDownloadedToPhysical` 此刻**尚未恢复**(它随 R4 factory API 一起恢复)——R1 不引入它。

- [ ] **Step 7: 全 8 gate 回归 + Controller 审 diff**

跑全 8 gate,确认无回归(纯 test 移动,数值应完全不变)。审 `git diff` 确认:零生产文件改动(只有 3 个 test文件 + 1 个test CMake);无夹带。

- [ ] **Step 8: commit**

```bash
git add velox/ch/IO/tests/IoAdaptersTest.cpp \
        velox/ch/Disks/IO/tests/FileCacheBufferedInputTest.cpp \
        velox/ch/Interpreters/FileCache/tests/FileSegmentTest.cpp \
        velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
git commit -m "FileCache: relocate IO integration tests"
```

---

## Commit R2: C8 `FileCacheCoalescedLoad`(最高风险)

**Goal:** 建 `FileCacheCoalescedLoad`,用内部 `FileCacheInputStream` 复用模型执行 prefetch/demand group;`FileCacheBufferedInput` 建 shared read context + stream↔load bindings(planning/trigger/mapping);`FileCacheInputStream` 加 `takeLastOutputBuffer` 零拷贝 handoff + `installCoalescedBuffers`(buffer 交付语义,归属 stream);删 `warmSourceGroup` 独立 task submission。这是整轮的核心,已拆成 6 个分子任务(R2-1…R2-6)、每个带核实过的 file:line,可直接执行;**唯一开工前动作**是核对锚点未因前序 commit 漂移(R2 的锚点基于 HEAD c609572d)。

**Files(与权威 12.7 R2 清单一致):**
- Create: `velox/ch/Disks/IO/FileCacheCoalescedLoad.h`
- Create: `velox/ch/Disks/IO/FileCacheCoalescedLoad.cpp`
- Modify: `velox/ch/Disks/IO/FileCacheBufferedInput.{h,cpp}`
- Modify: `velox/ch/Disks/IO/FileCacheInputStream.{h,cpp}`
- Modify: `velox/ch/Disks/IO/CMakeLists.txt`
- Modify: `velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp`(demand/prefetch/handoff/lifecycle + 正常/异常矩阵 RED tests)
- Modify: `velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp`(cancel 组合 + 异常竞争 RED tests)

### 权威接口锚点(设计文档 12.4.2 / 12.4.3,已核实类型)

- `FileCacheReadContext`(声明在 `FileCacheInputStream.h`):成员序 `cache / ioStatistics / ioStats / source / pool / key / origin / cacheOptions / requestContext / queryStatus / tracker / fileNum / groupId / fileSize`。**声明序即反向析构序**:`source`(持 `IoStats`/`IoStatistics` raw 指针)必须在 `ioStats`/`ioStatistics` **之后**声明 → 先析构,保 C2 UAF 合同;`cache` 最后析构。
- `FileCachePreparedBuffer { velox::BufferPtr data; velox::common::Region region; }`(声明在 `FileCacheInputStream.h`)。
- `FileCacheLoadRequest { size_t requestIndex; size_t planChunkIndex; Region region; TrackingId trackingId; vector<FileCachePreparedBuffer> buffers; bool ready; bool consumed; }`(声明在 `FileCacheCoalescedLoad.h`;它属于load私有request模型，不放进 `FileCacheInputStream.h`)。
- `class FileCacheCoalescedLoad final : public cache::CoalescedLoad`:
  - `struct Context { shared_ptr<const FileCacheReadContext> readContext; FileCache::QueryContextHolderPtr queryContextHolder; }`(`queryContextHolder` 先于 `readContext` 析构 → 先于 `readContext->cache`)。
  - ctor `(Context, uint64_t groupOffset, uint64_t groupLength, vector<FileCacheLoadRequest> requests)`。
  - `vector<cache::CachePin> loadData(bool prefetch) override`(返回 `{}`,数据落 CH `FileSegment` + request RAM buffers)。
  - `bool isSsdLoad() const override { return false; }`;`int64_t size() const override`。
  - `optional<vector<FileCachePreparedBuffer>> getData(const vector<size_t> & requestIndices)`(`requestMutex_` 下全有或全无ownership move)。这是相对Direct真实签名 `int32_t getData(int64_t offset, Allocation &, string &)` 的**有意偏离**：FileCache交付 `BufferPtr`；一个业务stream可跨多个request/group；duplicate offset用稳定 `requestIndex`；同一binding不得逐offset发布部分结果。只对齐 `getData` 命名、move ownership和一次性消费语义，不对齐参数/返回类型。
  - 私有序:`Context context_; mutable std::mutex requestMutex_; FileSegmentsHolderPtr groupSegments_; vector<FileCacheLoadRequest> requests_;`(`requests_`/`groupSegments_` 先于 `context_` 析构)。
- `FileCacheInputStream` 有两个显式角色:
  - 业务构造:`(FileCacheBufferedInput * bufferedInput, shared_ptr<const FileCacheReadContext> context, Region region, LogType logType, TrackingId trackingId = {})`。成员名 `bufferedInput_` 对齐 `DirectInputStream`；只用于首次 `Next` 调复数 `coalescedLoads(this)`，业务stream不得晚于bufferedInput析构。
  - `createCoalescedInternal(shared_ptr<const FileCacheReadContext>, Region, LogType)`。内部IO stream不持bufferedInput、不查bindings，可以晚于input完成。
- `FileCacheInputStream::takeLastOutputBuffer() -> optional<FileCachePreparedBuffer>`(见上文 buffer 生命周期契约)。
- `FileCacheInputStream::installCoalescedBuffers(vector<FileCachePreparedBuffer>)`:**归属 `FileCacheInputStream`**(权威设计12.4.3,是stream的 `Next` 语义:按绝对offset排序、`Next`优先交付覆盖当前位置的RAM buffer,离开prepared region或消费完继续原状态机)。**不在 `FileCacheBufferedInput`。**
- `FileCacheBufferedInput` 新增(仅 planning/trigger/mapping,不含 buffer 交付语义):`vector<shared_ptr<FileCacheCoalescedLoad>> coalescedLoads_`;`struct LoadBinding { shared_ptr<FileCacheCoalescedLoad> load; vector<size_t> requestIndices; }`;`folly::Synchronized<folly::F14FastMap<const SeekableInputStream*, vector<LoadBinding>>> streamToCoalescedLoads_`;`coalescedLoads(const SeekableInputStream*) -> vector<LoadBinding>`。命名对齐Direct的 `coalescedLoad`/`streamToCoalescedLoad_`，仅因一stream可跨多个group而使用复数。
- `Request` 加 `SeekableInputStream* stream;`(仅作 map key,从不解引用)+ `size_t requestIndex;`。

### grouped chunk → 业务 stream/request 稳定映射(内部 planning carrier,不进 load API)

`PlanChunk`(`FileCacheBufferedInput.h:166-173`,字段 `offset/length/trackingId/state/prefetch`)**是已存在的内部 planning artifact**,不是本轮新公有架构,也**不得**泄漏进 `FileCacheCoalescedLoad` 的公有接口。R2 只在其内部复用 + 加一个私有 owner 索引。

**现状不足(必须修)**:`groupMissChunks`(`.cpp:588-594`)把 `CoalescedGroup{begin,end}` 存成 `items[begin]` / `items[end-1]+1`,即 **plan_ 索引的 bounding box**;但 group 的真实成员是 `missPrefetch`/`missDemand` **先按 offset 排序**(`.cpp:327-331` `std::sort(byOffset)`)后的离散集合 `items[begin..end)`。因此:
- `[begin, end)` 连续 plan_ 区间会夹带**不属于该 group** 的 chunk(hit chunk、其它 request / 其它 group 的 miss chunk)。**禁止**用 begin/end 连续区间判成员归属。
- `CoalescedGroup.ranges` 只存 `{offset,length}`(`.cpp:581-587`);**duplicate offset**(两个业务 request enqueue 同一 region)下,offset/trackingId 都不能区分归属。

**最小内部实现(稳定映射,duplicate / interleave 安全)**:
1. `PlanChunk` 加私有 `size_t requestIndex`(指向 `requests_[k]`),在 plan 构建循环(`.cpp:293-305`,该循环已按 `requests_` 顺序遍历)里赋值。这是**内部 planning carrier**,不进任何公有签名。
2. `CoalescedGroup` 增记**显式成员 plan-chunk 索引** `std::vector<size_t> memberChunks`(即 `items[begin..end)` 精确集合),替代用 begin/end 区间反推成员;begin/end 仅保留作 bounding 字节 extent 计算。
3. 映射链(全程按稳定索引,绝不按 offset / trackingId / 区间):
   ```
   group → memberChunks[j] → PlanChunk.requestIndex → requests_[k].stream + requests_[k].requestIndex
   ```
   每个成员 chunk 生成一个 `FileCacheLoadRequest`,其 `requestIndex` = 业务 `requests_[k].requestIndex`(**全 input 稳定的业务 request id**,不是 load 内 0..N-1 计数),`planChunkIndex` = 该成员 plan-chunk 索引。同一业务 request 跨多个 plan chunk 会产生多个 `FileCacheLoadRequest`,共享同一 `requestIndex`、`planChunkIndex` 不同;duplicate region(不同业务 request enqueue 同 offset)→ **不同** `requestIndex`,靠稳定 `requestIndex` 区分,不靠 offset。`getData(requestIndices)` 返回 `requestIndex ∈ requestIndices` 的全部已备 buffer。
4. `load()` 建完 load 后,把该 load 覆盖的每个业务 stream 写进 `streamToCoalescedLoads_[requests_[k].stream].push_back(LoadBinding{load, {该 stream 在此 load 内的 requestIndices}})`。binding 建好后 `requests_.clear()`(`.cpp:351`)照旧 —— stream 指针已被 binding 捕获(仅作 map key,从不解引用)。

`planChunkIndex` / `requestIndex` 都是普通 `size_t` 内部载体,`PlanChunk` 类型本身不出现在 `FileCacheCoalescedLoad.h` 的公有签名里。

### RED 测试意图 —— 权威 12.6.6 完整矩阵(开工细化时逐条落成 step 级 gtest;异常注入**只能**用 Velox 现有 fault-injection `ReadFile` / `TestValue` / 受控 executor / cancellation / MemoryPool / `FileCache` 失败机制,**禁止**为测试加任何生产 hook / callback / 错误开关 / test-only 接口)

**正常路径:**
- [ ] cold prefetch:load 后 executor 立即执行;业务 `Next` 前 request RAM buffers + segments 均 ready;业务 `Next` 不增 source read count。
- [ ] cold demand:load 后 source read count==0 且 load 为 `kPlanned`;A 首次 `Next` 触发整个 group,B 随后 `Next` 不再次回源。
- [ ] null executor:prefetch load 保持 `kPlanned`,由第一个相关 `Next` 触发。
- [ ] cache 状态:全 hit、全 miss、hit/miss 混合、已有 downloader、partial continuation。
- [ ] region 形状:相邻、带 gap、重叠、重复 offset、非零 offset、跨 segment/loadQuantum、尾部短块。
- [ ] 并发:A/B 并发 `Next` 只执行一次 group load;waiter 唤醒后各取正确 request buffer。
- [ ] ownership:内部 stream output `BufferPtr` 与 request 接收、业务 stream 交付为**同一 allocation**(指针相等断言);正常 request 路径无额外 payload copy。
- [ ] **buffer 生命周期契约**:`takeLastOutputBuffer` move 后窗口元数据清空,下一次内部 `Next` 分配/复用新 buffer 不读旧数据(中和"清空元数据"看红)。
- [ ] gap:不回源、不进 RAM payload、不写 `FileSegment`;EMPTY gap segment 按 core 语义清理。
- [ ] lifecycle:clone、reset、planned load 析构取消、running load 在 input 析构后由 shared context 安全完成、stream 未消费便销毁。
- [ ] role/lifetime:业务stream首次 `Next` 通过 `bufferedInput_` 取得bindings；内部stream无bufferedInput且input析构后仍可完成。业务stream生命周期与 `DirectInputStream` 一致，不晚于其BufferedInput。
- [ ] 格式:DWRF 和 Parquet 真实 clone/enqueue/load/`Next` cold-fill/warm-hit E2E。
- [ ] 统计:physical source/cache bytes、prefetch bytes、delivered bytes、cache-write bytes、latency 不漏不重(内部 stream 用**空 TrackingId** 避免预取字节误记为 delivered)。

**异常路径(每条只覆盖 `CoalescedLoad` 组合后的状态/重试/waiter/buffer 交付;`FileCacheInputStream` 单层异常 case 不重复):**
- [ ] prefetch source 失败:load 转 `kCancelled`;业务 `Next` 走普通 demand 重试;无 downloader/waiter 泄漏。
- [ ] demand source 失败:异常传播给查询;同 group waiter 被唤醒;无 downloader 泄漏。
- [ ] cache 资源/写入:getOrSet/QueryLimit 失败、reserve 失败、ENOSPC/EDQUOT、skip=true 和 strict 写失败 —— 各自严格复用现有 `FileCacheInputStream` 合同,load 层不重新分类。
- [ ] 执行资源:RAM allocation 失败、executor 提交失败。
- [ ] 竞争:query cancellation、reset、析构与 planned/running load 竞争。
- [ ] 部分结果:任一 request 未完整准备时不得发布 success-shaped payload;已准备 buffer 按异常合同释放,后续只能完整 fallback 或传播异常。

**旧 warm-only 测试按新合同改写(不新增,是改写现有):**
- [ ] `PrefetchWarmDownloadsSegmentsAsync` → prefetch 同时准备 RAM request buffers 和 `FileSegment`。
- [ ] `DemandGroupIsNotWarmed` → demand 在首次 `Next` 启动整个 group。
- [ ] `PlainDestructorDoesNotCancelWarm` → 析构取消 planned load,但 running load 由 shared context 安全完成。

### 分子任务(每个独立 RED/GREEN;file:line 均已核实 @ HEAD c609572d)

> **说明**:R2 是全新组件,拆成 6 个可独立编译/验证的子提交(R2-1…R2-6),降低单次 diff 体积、便于 Controller 逐块复现 RED。子提交可合并为一个最终 commit,也可保留为微提交链(不 amend)。每个子任务先写/改测试看红,再实现看绿。

参照物锚点(Velox 标准 Direct,只读参照不改):
- `DirectCoalescedLoad`:`velox/dwio/common/DirectBufferedInput.h:61`(类)、ctor `.h:63-87`(基类 `CoalescedLoad({}, {})`)、`loadData` 实现 `.cpp:377`、`getData` 实现 `.cpp:450-474`、`size()` `.h:108-114`。
- `DirectBufferedInput`:dtor cancel `.h:158-162`、prefetch 提交 `.cpp:227-239`(`AsyncLoadHolder{load,pool}` + `executor_->add`)、`coalescedLoad(stream)` move+erase `.cpp:242-254`、`streamToCoalescedLoad_` `.h:317-321`、`reset()` cancel+clear `.cpp:256-264`。
- `DirectInputStream`:back-pointer `bufferedInput_` `.h:70`、`loadPosition` 首触发 `.cpp:172-193`(`coalescedLoad(this)`→`loadOrFuture(&wait)`→`wait.wait()`→`getData`)。
- 基类 `cache::CoalescedLoad`(`velox/common/caching/AsyncDataCache.h`):类 `:477`、`State` `:480`、ctor `:482`、`loadOrFuture` `:495`、`state()` `:497`、`cancel()` `:502`、`size()=int64_t` `:507`、`isSsdLoad()` `:515`、`loadData()` `:524`。

现有 FileCache IO 层锚点(要改/复用):
- `FileCacheInputStream` ctor `.h:116-122`;窗口成员 `.h:242-246`;`Next` 全文 `.cpp:946-1065`(残留窗口服务 `:950-958`、`ensureOutputBuffer` `:981`、`reader->set(out,cap)` 装载、`readFromCurrentSegment` `:1004`、`reader->set(nullptr,0)` detach `:1009`、发布窗口 `:1027-1030`、释放 downloader `:1034-1035`、`completeCurrentSegmentAndAdvance` `:724`);`ensureOutputBuffer` `.cpp:936-942`;`BackUp` `.cpp:1067-1074`;`owner_` `.h:227`(32 处使用,含 `:305/307 fileCache/cacheOptions`、`:312/328/341 cacheKey`、`:344 fileSize`、`:347 origin`、`:675/910 ioStatistics`、`:930 recordReadBytes`、`:940 memoryPool`)。
- `FileCacheBufferedInput`:类 `.h:48`;`enqueue` `.cpp:251-280`;`load` `.cpp:282-352`(warm 提交 `:338-347`);`warmSourceGroup` `.cpp:598-~900`;planning 结构 `PlanChunk` `.h:166-173`、`CoalescedGroup` `.h:181-195`、`Request` `.h:210-219`;成员 `sourceReadFile_/cache_/cacheKey_/origin_/cacheOptions_/requestContext_/queryStatus_` `.h:280-286`、`executor_` `.h:292`、`plan_` `.h:309`、`sourceGroups_` `.h:314`。
- FileSegment 序列(`FileSegment.h`):`getCallerId` `:118`、`getOrSetDownloader` `:149`、`getCurrentWriteOffset` `:166`、`completePartAndResetDownloader` `:228`、`reserve` `:242-247`、`write` `:250`;`FileCache::getOrSet` `FileCache.h:198-206`、`get`(只读探针)`FileCache.h:217`。
- 自由函数(`FileCacheInputStream.h`):`classifyCacheWriteError` `:60`、`reserveAndWriteSegmentChunk` `:84-91`、`downloadChunkIntoSegment` `:100-107`。

---

- [ ] **R2-1:类型骨架 + CMake(编译占位,无行为)**
  - 在 `FileCacheInputStream.h` 顶部(namespace 内、`FileCacheInputStream` 类前)只加两个由stream生产/消费的共享类型:`FileCacheReadContext`(成员序见上锚点)和 `FileCachePreparedBuffer`。
  - 新建 `FileCacheCoalescedLoad.h`:先定义仅属于load的 `FileCacheLoadRequest`,再定义 `class FileCacheCoalescedLoad final : public cache::CoalescedLoad`,含 `struct Context`、ctor、`loadData`/`isSsdLoad`/`size`/`getData` 声明、私有成员(序见锚点)。基类构造用 `CoalescedLoad({}, {})`(仿 `DirectBufferedInput.h:63`)。
  - 新建 `FileCacheCoalescedLoad.cpp`:`loadData` 先 `return {};`,`size()` 累加 `requests_` 各 `region.length`(仿 `DirectBufferedInput.h:108-114`),`getData` 先 `return std::nullopt;`。
  - `velox/ch/Disks/IO/CMakeLists.txt`:在现有 `velox_sources(velox_ch_filecache PRIVATE ...)` 列表(`FileCacheInputStream.cpp` 同一处)加入 `FileCacheCoalescedLoad.cpp`;不创建新library/target。
  - 验证:`ninja ... velox_ch_filecache_buffered_input_test` 编译通过。**无测试**(纯占位)。这是重构性起步,不需 RED。

- [ ] **R2-2:`FileCacheInputStream` 双角色构造 + `takeLastOutputBuffer`**
  - RED:在 `FileCacheBufferedInputBuilderTest.cpp` 写 `TakeLastOutputBufferClearsWindow` —— 用 `createCoalescedInternal` 建内部 stream,`Next` 一次拿到窗口,`takeLastOutputBuffer()` 返回含 `BufferPtr`+绝对 `Region` 的 optional;断言返回后窗口元数据清零(再 `Next` 分配新 buffer、内容正确、不复用被 move 的旧 buffer)。中和"清空 `outputBufferStart_`/`offsetInOutputBuffer_`/`outputBufferSize_`"看红。
  - 实现:给 `FileCacheInputStream` 加业务 ctor 首参 `FileCacheBufferedInput * bufferedInput`(保留,对齐 `DirectInputStream.h:70`)+ 改为从 `shared_ptr<const FileCacheReadContext>` 取 cache/source/pool/options/stats(替换现 `owner_->fileCache()` 等 32 处 `.cpp` 访问);加 `static createCoalescedInternal(context, region, logType)`(无 bufferedInput);加 `takeLastOutputBuffer()`:move `outputBuffer_`、算绝对 region(`region_.offset + outputBufferStart_`,长度 `outputBufferSize_`)、清窗口元数据。**契约**:调用前窗口须全交 consumer,调用后禁止 `BackUp`(见文档顶部 buffer 生命周期契约)。
  - 验证:该 test GREEN;`buffered_input`+`connector`+`cancellation` 回归绿。

- [ ] **R2-3:`FileCacheCoalescedLoad::loadData` 内部-stream 复用**
  - **顺序说明**:R2-3 时 `getData`(读 request 私有 buffers 的公开接口)尚未实现(在 R2-4);且 `loadOrFuture(nullptr)` 只返回 `bool`,`loadData` 的返回值(空 `CachePin` vector)是 protected、测试**看不到**。所以 R2-3 的 RED **只能**验证可观察副作用:(a) `FileSegment` 状态,(b) source read 计数。request buffers 与"`loadData` 返回 `{}`"都推迟到 R2-4(有 `getData` 后)。
  - RED:`LoadDataFillsSegments` —— 冷 cache,直接构造 `FileCacheCoalescedLoad`(2 个 request),`loadOrFuture(nullptr)` 后断言:对应 `FileSegment` 已 DOWNLOADED(用 `downloadedBytes(cache,key,off,len)==len`,仿 `PrefetchWarmDownloadsSegmentsAsync`)、source `CountingReadFile::preadCount()>0`。中和 loadData 主体(改成 `return {}` 不填)看红(segment 仍 EMPTY)。**不断言 `loadData` 返回值**(protected,测试不可达)。
  - RED(special cache modes 不建 metadata):`CacheOnlyCoalescedLoadDoesNotCreateMetadata` —— 直接执行一个 `tempCacheOnly` miss load,断言按现有cache-only合同抛异常，且执行前后persistent metadata/EMPTY segment数不变;`BypassCoalescedLoadDoesNotCreateMetadata` —— 执行一个 `readIfExistsOtherwiseBypass` miss load,断言source read delta增加且执行前后metadata/EMPTY segment数不变。两例必须真正调用 `loadOrFuture`,不能只停在side-effect-free planning阶段。payload正确性等R2-4实现 `getData` 后验证。中和模式分派退回无脑 `getOrSet` 看红。
  - RED(duplicate materialize once):`DuplicateRegionMaterializedOnce` —— 两个业务 request 同一小于单次remote buffer的miss region(同 offset+length),`loadOrFuture` 后断言source read delta与单request基线相同、local/ssd read delta为0。R2-3不读取private buffers;独立buffer和内容正确性等R2-4实现 `getData` 后验证。中和duplicate copy退回第二个内部stream重读看红。
  - RED(group holder 生命周期):`GroupHolderReleasedAfterLoad` —— group bounding range包含未请求gap;`loadData`完成后断言请求segments按预期持久化，而gap对应EMPTY metadata不再被load holder pin住。中和退出路径的 `groupSegments_.reset()` 看红。
  - 实现:先把 `nextFileSegmentsBatch` 中现有三分支lookup抽成IO层共享helper `getFileSegmentsForRead(const FileCacheReadContext &, uint64_t absPos, uint64_t size)`，由 `FileCacheInputStream::nextFileSegmentsBatch` 与 `FileCacheCoalescedLoad::loadData`共同调用，保证只有一份模式策略:`tempCacheOnly` → `getDownloadedContiguousOrEmpty`并在空批抛现有cache-only错误;`readIfExistsOtherwiseBypass` → `cache.get`且不创建metadata;普通 → `cache.getOrSet`。load持有返回的group holder，并在取得后立即安装scope guard，保证 `loadData`所有success/exception退出路径最终执行 `groupSegments_.reset()`；不得在内部streams取得各自精确holder之前提前释放。随后逐request `createCoalescedInternal`(空 TrackingId)→循环 `Next`+`takeLastOutputBuffer` 收集进request私有临时buffers→全部完成后持 `requestMutex_` 一次性 `ready=true`。**duplicate region只materialize一次**:对齐Direct `duplicateRegion`/`copyDuplicateRegion`;首request走内部stream真读，后续duplicate为每个有效buffer分配独立 `BufferPtr`并只复制有效region bytes，绝不创建第二个内部stream或读取本地 `FileSegment`。异常时不发布部分payload;segment状态机全部留在内部stream。
  - 验证:上述 test 全 GREEN;gap 不回源/不写 segment 的子 case(见测试矩阵)一并绿。

- [ ] **R2-4:`getData` + bindings + prefetch/demand 触发 + `installCoalescedBuffers`**
  - RED-a(demand):`DemandTwoStreamSharedLoad` —— A 首次 `Next` 触发 group,B 随后 `Next` 不再回源(source preadCount 不增)。
  - RED-b(prefetch):`ColdPrefetchExecutesImmediately` —— `load` 后 `executor.join()`,业务 `Next` 前 request buffers+segments ready,业务 `Next` 不增 source read。
  - RED-c(handoff):`InternalBufferIsBusinessAllocation` —— 业务 `Next` 返回的 `data` 指针 == prepared buffer 的数据指针:`prepared.data->as<char>() == nextData`(**不是** `BufferPtr.get()` —— 那是 Buffer 对象地址,与 `Next` 返回的 `outputBuffer_->as<char>()+off` char 数据地址必不相等)。证明同一 allocation、无 copy。
  - RED-d(payload):`GetDataReturnsSourcePayload` —— R2-4有 `getData`后,断言取回buffers内容等于source对应range，补R2-3推迟的payload验证。`loadData`是protected且返回pins不对测试暴露;不得增加白盒accessor，也不写任何 `CachePin`/`AsyncDataCache`观测断言。override中固定 `return {}`由code review和编译保证。
  - RED-e(mapping):`InterleavedChunksMapToOriginalStreams` —— 以enqueue顺序和offset排序顺序不同的两个业务request构造同一/相邻group，并包含相同tracking id；分别消费两个stream，断言每个stream只收到自己region的数据。中和 `memberChunks/requestIndex` 稳定链、退回begin/end连续区间或offset/trackingId反推时看红。
  - RED-f(deferred R2-3 assertions):`BypassGetDataReturnsSourcePayload` 验证bypass request从source取得正确payload且不创建metadata;`DuplicateGetDataReturnsIndependentBuffers` 验证duplicate requests各取得指针不同、内容相同的独立 `BufferPtr`。这两项补R2-3因 `getData` 尚未实现而推迟的payload断言。
  - 实现:
    - `FileCacheBufferedInput` 加 `coalescedLoads_`、`LoadBinding`、`streamToCoalescedLoads_`、`coalescedLoads(stream)`(move+erase,仿 `.cpp:242-254`);`Request` 加 `SeekableInputStream* stream`+`requestIndex`;`PlanChunk` 加私有 `size_t requestIndex`、`CoalescedGroup` 加显式成员 `std::vector<size_t> memberChunks`(见上"grouped chunk → 业务 stream/request 稳定映射"节)。
    - **`load()` 为 prefetch 和 demand group 都建 `FileCacheCoalescedLoad`**(现状 `.cpp:330-347` 只 warm prefetch、demand 靠 Next 同步 —— 这必须改):`groupMissChunks` 后,对**每个** miss group(不分 prefetch/demand)建一个 load 存进 `coalescedLoads_`;按 `group.memberChunks[j] → plan_[.].requestIndex → requests_[k].stream` 的稳定链填 `streamToCoalescedLoads_`(binding 的 requestIndices 用**业务稳定** `requestIndex`(`requests_[k].requestIndex`,即 `FileCacheLoadRequest.requestIndex`),duplicate offset 各自独立,**不按 offset/trackingId/begin-end 区间**);**prefetch group 额外** `executor_->add([load]{ load->loadOrFuture(nullptr); })`(直接 by-value capture `shared_ptr` load —— load 自身已通过 shared context 保活 pool,无需额外 holder)立即执行;**demand group 只留 kPlanned**,由首次 `Next` 触发。null executor 时 prefetch 也留 kPlanned(与现状 skip 一致)。
    - `FileCacheCoalescedLoad::getData(requestIndices)`:`requestMutex_` 下检查全 ready 且未 consumed→move 各 `buffers` 返回并标 `consumed`,否则 `nullopt`(签名是有意偏离,见锚点)。
    - `FileCacheInputStream::installCoalescedBuffers`:按绝对 offset 排序装入,`Next` 优先交付覆盖当前位置的 RAM buffer,离开/消费完继续原状态机;业务 stream 首次 `Next` 经 `bufferedInput_->coalescedLoads(this)`→`loadOrFuture(&wait)`→`getData`→`installCoalescedBuffers`(仿 `DirectInputStream.cpp:172-193`);`getData` 返回 `nullopt` 时不装窗口,继续原 demand 状态机。prepared RAM窗口每次真正从业务 `Next` 返回前按delivered bytes调用一次context中的 `ScanTracker::recordRead`;内部stream使用空TrackingId，不记录业务消费。
  - 验证:a/b/c/d/e/f GREEN;并发"只执行一次 group load"case 绿;demand-only(无 prefetch group)场景首次 `Next` 能触发 load。

- [ ] **R2-5:删 `warmSourceGroup` submission + reset/dtor cancel**
  - 删 `FileCacheBufferedInput::warmSourceGroup`(`.cpp:598-~900`)+ 声明(`.h:278` 附近)+ `load()` 里 `:338-347` 的 warm 提交块(换成 R2-4 的 prefetch load 提交)。reset/dtor 遍历 `coalescedLoads_` 调 `load->cancel()` 并 clear map(仿 `.cpp:256-264`)。
  - **注意**:R2 只切断 Disks/IO 对 core `submitWarm` 的调用;core 的 `submitWarm`/`inflightWarmForTest`/`warm_*` 声明与实现由 **R4** 删。R2 结束后 `grep -rn "submitWarm\|inflightWarmForTest" velox/ch/Disks/IO/` 必须为空(这是 R4 前置断言)。
  - 验证:编译无 `warmSourceGroup` 残留;`WarmTaskContext` 悬空 IoStats 合同(C2)由内部-stream 的 `FileCacheReadContext` 成员序继承。

- [ ] **R2-6:完整测试矩阵 + 旧 warm-only 改写(test-matrix checklist,非 code-level 生产 step)**
  - **说明**:R2-2..R2-4 已覆盖核心正常路径(handoff/demand/prefetch/buffer 契约)。R2-6 **不是** code-level 生产 step,而是一张**测试矩阵 checklist**:每个"material case"作为一个**单独命名的 gtest** 落地(命名建议 `CoalescedLoad_<场景>`),只加测试、不加任何生产代码,注入**只用** Velox 现有机制。因此 R2-6 内的批 A/B/C 是**矩阵桶**(bucket),桶下每条 = 一个独立命名 gtest;它们没有、也不需要逐条 file:line 的生产改动 step。开工前照旧核对锚点是否因前序 commit 漂移(fixture:`FileCacheBufferedInputBuilderTest.cpp:184` 的 `CountingReadFile :93`/`BlockingReadFile :132`/`makeManagerCache :230`;cancellation 用 `FileCacheCancellationTest.cpp:131` 的 `makeInputWithExecutor :294`)。落地时逐条把桶内 case 展开为具名 gtest,并记录该 case 的注入手段 + 期望可观察状态(不得为凑"细节"编造不存在的失败 API)。
  - 批 A 桶(正常路径补齐,每条一个具名 gtest):null executor 保持 kPlanned、cache 状态(全 hit/全 miss/混合/已有 downloader/partial continuation)、region 形状(相邻/gap/重叠/重复 offset/非零 offset/跨 segment/尾部短块)、gap 不回源不写 segment、lifecycle(clone/reset/planned 析构取消/running 析构后安全完成/stream 未消费即销毁)、统计(source/cache/prefetch/delivered/cache-write bytes + latency 不漏不重,内部 stream 空 TrackingId)、DWRF+Parquet format E2E。
  - 批 B 桶(异常路径,只覆盖 coalesced 组合层,单层已有的不重复;每条一个具名 gtest):prefetch source 失败→kCancelled+demand 重试无泄漏;demand source 失败→传播+waiter 唤醒无泄漏;cache 资源/写入(getOrSet/QueryLimit/reserve/ENOSPC/EDQUOT/skip vs strict)复用既有合同;RAM alloc / executor 提交失败;cancellation/reset/析构 与 planned/running load 竞争;部分结果不发布 success-shaped payload。**注入手段限现有** fault-injection ReadFile / `TestValue` / 受控 executor / cancellation / MemoryPool / FileCache 失败机制;若某条 case 现有机制无法确定性触发,记为"当前不可覆盖"而非发明失败 API。
  - 批 C 桶(改写 3 个旧 warm-only test,**改写非新增**,各一个具名 gtest):`PrefetchWarmDownloadsSegmentsAsync`(`Builder…:1394-1432`)→ prefetch 同时准备 RAM buffers+segment;`DemandGroupIsNotWarmed`(`:1761-1800`)→ demand 首次 `Next` 启动 group;`PlainDestructorDoesNotCancelWarm`(`Cancellation…:608-~650`)→ 析构取消 planned load 但 running load 由 shared context 安全完成。
  - **异常注入只能用** Velox 现有 fault-injection ReadFile / `TestValue` / 受控 executor / cancellation / MemoryPool / FileCache 失败机制,**禁止**加任何生产 hook / callback / 错误开关 / test-only 接口。

### 构建与验证(整个 R2)

- [ ] 构建:`ninja -C cmake-build-debug-gcc13 velox_ch_filecache_buffered_input_test velox_ch_filecache_connector_test velox_ch_cancellation_test > cmake-build-debug-gcc13/build_r2_coalesced_load.log 2>&1`(测试输出各写唯一 `test_r2_*.log`)。
- [ ] 全 8 gate 回归 + Controller **独立复现每个 RED**(中和对应生产逻辑看红)+ 审 diff。重点:无第二套 segment 状态机(load 层不碰 FileSegment)、无新增 core 改动、无 test-only 生产 hook、`installCoalescedBuffers` 归属 stream、`getData` 偏离已按文档。
- [ ] commit:`git commit -m "FileCache: execute prefetch through coalesced loads"`(子提交可保留为链,不 amend)。

---

## Commit R3: wait 并发测试改用现有 Velox 机制(只改 tests)

**Goal:** 删除测试对 `FileSegment::wait` 生产 hook 的依赖,改用 R2 引入的 `CoalescedLoad::loadOrFuture` `TestValue` + 测试 double 的 `Baton` 建立确定性时序。**不改任何生产文件。**

**Files:**
- Modify: `velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp`
- Modify: `velox/ch/Disks/IO/tests/FileCacheCancellationTest.cpp`

### 时序合同(设计 12.5,不加生产 hook)

```
测试 source 在 test double 中阻塞 A 的物理 read
  -> 确认 A 已进入 group load
  -> 通过 CoalescedLoad 既有 loadOrFuture hook/状态让 B 进入 wait 路径
  -> 释放 A
  -> 验证 A/B 完成、source 读取未重复、无 downloader/waiter 遗留
```

### 精确锚点(已核实 @ HEAD c609572d)

- **唯一依赖 `beforeWait` 的测试**:`FileCacheBufferedInputBuilderTest.cpp:1630-1700`(Design 7.4 timing test)。它在 `:1667` 注册 `TestValue::adjust("facebook::velox::ch::FileSegment::wait::beforeWait", ...)`,在 demand 进 wait 那刻 post `demandWaiting` baton(`:1630`/`:1700` 注释)。R3 就是把这个 test 改写掉。
- **要删的生产 hook**:`FileSegment.cpp:515-527`(`wait()` 内的 `TestValue::adjust("...FileSegment::wait::beforeWait", this)`)。**注意**:这个删除动作属于 **R4**(core 恢复基线时随 `TestValue.h` include 一起删)。R3 只负责**先让测试不再依赖它**,这样 R4 删 hook 时不会有测试断链。
- **R2 后可用的替代确定性机制**:
  - `BlockingReadFile`(`FileCacheBufferedInputBuilderTest.cpp:132`,双 `folly::Baton` `firstReadStarted`/`releaseFirstRead`)—— 阻塞 A 的物理 source read 一次。
  - 基类 `cache::CoalescedLoad::loadOrFuture` **已自带两个现成 TestValue hook**(`AsyncDataCache.cpp:394-395` 入口 key `"facebook::velox::cache::CoalescedLoad::loadOrFuture"`、`:416-417` `::loading` key),外加 `promise_->getSemiFuture()` 的 wait 语义(`:407`)。B 可用这些既有 hook / `state()==kLoading` 观察点确定性进 wait。**绝不新增生产 hook**(不在 `FileCacheCoalescedLoad::loadData` 或任何生产文件加 TestValue —— 只用基类已有的)。

### Steps

- [ ] **Step 1**:读 `FileCacheBufferedInputBuilderTest.cpp:1630-1700` 全文,搞清它当前证明的并发合同(demand A 持 lease 下载时 B 确定性进 wait,A 完成后 B 命中,不重复 source read)。
- [ ] **Step 2**:改写该 test —— 用 `BlockingReadFile`(:132)确定性阻塞 A 的第一次 source read;用基类 `CoalescedLoad::loadOrFuture` 的既有 TestValue(`AsyncDataCache.cpp:394/416`)或 `state()==kLoading` 让 B 进 wait 路径;`releaseFirstRead` 后 `executor.join()`。删掉对 `FileSegment::wait::beforeWait` 的 `TestValue::adjust`(:1667)。**不新增任何生产 TestValue hook。**
- [ ] **Step 3**:断言 A/B 都完成、source `preadCount()` 未重复(== 预期次数)、无 downloader/waiter 遗留(段状态干净)。若现有机制无法证明某更细内部瞬间,**不为断言改产品代码**,改验可观察并发合同。禁止 `sleep` 和概率性时序断言。
- [ ] **Step 4**:确认本 commit **只改 tests**(`git diff --name-only` 无生产文件);`grep -rn beforeWait velox/ch/Disks/IO/tests/ velox/ch/Interpreters/FileCache/tests/` 应为空(测试侧不再依赖该 hook)。
- [ ] **Step 5**:构建 + 运行(**正确 binary**:`FileCacheBufferedInputBuilderTest.cpp` 属 `velox_ch_filecache_connector_test`,已核实 `tests/CMakeLists.txt:103`):
  ```bash
  ninja -C cmake-build-debug-gcc13 velox_ch_filecache_connector_test > cmake-build-debug-gcc13/build_r3.log 2>&1
  cmake-build-debug-gcc13/velox/ch/Disks/IO/tests/velox_ch_filecache_connector_test \
    --gtest_filter='*Concurrent*:*Wait*:*Demand*' > cmake-build-debug-gcc13/test_r3_coalesced_wait.log 2>&1
  ```
  **验证 filter 真的匹配到测试**(日志里 `[ RUN ]` 计数 > 0,不是 `0 tests ran`),避免 filter 空转伪成功。
- [ ] **Step 6**:全 8 gate 回归。
- [ ] **Step 7**:commit:`git commit -m "FileCache: make coalesced wait tests deterministic"`

---

## Commit R4: FileCache core 恢复基线(高风险)

**Goal:** 把 CH FileCache/FileSegment 核心逐文件恢复到 `5785a43a`,只在 `FileSegment.cpp` 保留 typed writer include + default factory 一行替换;删 3 个 warm core tests,恢复 baseline partial-write case。**依赖 R2 已切断 `submitWarm` caller。**

**Files:**
- Modify: `velox/ch/Interpreters/FileCache/FileCache.h`(当前偏离基线 61 行 → 0)
- Modify: `velox/ch/Interpreters/FileCache/FileCache.cpp`(106 → 0)
- Modify: `velox/ch/Interpreters/FileCache/FileSegment.h`(23 → 0)
- Modify: `velox/ch/Interpreters/FileCache/FileSegment.cpp`(82 → 仅 2 处:include + factory)
- Modify: `velox/ch/Interpreters/FileCache/FileCacheErrnoException.h`(42 → 0)
- Modify: `velox/ch/Interpreters/FileCache/tests/FileCacheTest.cpp` / `FileSegmentTest.cpp`(删 3 warm test,恢复 baseline case)

### 逐 hunk 恢复清单(相对基线 `5785a43a`,已核实 @ HEAD c609572d)

**全部为 warm 相关新增,恢复=删除(除非另注):**

- `FileCache.h`:
  - `:34-35` include hunk(`<folly/CancellationToken.h>`/`<folly/Function.h>`/`<condition_variable>`)→ 删。
  - `:301-321` public API(`submitWarm` / `inflightWarmForTest` + 注释)→ 删。
  - `:430-444` private 成员(`warm_mutex_`/`warm_cv_`/`warm_cancel_source_`/`accepting_warm_tasks_`/`inflight_warm_`)→ 删。
- `FileCache.cpp`:
  - `:1918-2011` `FileCache::submitWarm(...)` + `inflightWarmForTest()` 整段 → 删。
  - `:2013+` `deactivateBackgroundOperations()` 内 `shutdown.store(true)` **之前**插入的 warm 停接/取消/`warm_cv_.wait(inflight_warm_==0)` 段 → 删(恢复基线原顺序 `shutdown.store(true); stop_loading_metadata = true; ...`)。
- `FileSegment.h`:
  - `:117-130` HEAD **删掉了** baseline 的 `using WriteFileFactory`/`setWriteFileFactoryForTesting`/`createWriteFile` 三声明 → R4 **恢复**(加回,baseline 本有)。
- `FileSegment.cpp`(**唯一保留改动的文件**):
  - `:22-24` include hunk:新增 `FileCacheLocalWriteFile.h` + `TestValue.h`。→ **保留** `#include "velox/ch/IO/FileCacheLocalWriteFile.h"`;**删** `TestValue.h`(它只服务下面的 wait hook)。
  - `:232-258` HEAD **删掉了** baseline 的 `writeFileFactoryStorage()`/`setWriteFileFactoryForTesting`/`createWriteFile` factory 块 → R4 **恢复整块**(见下"factory 裁决")。
  - `:438-448` `write()` 内 default writer → **按裁决处理**(见下)。
  - `:515-527` `wait()` 内 `TestValue::adjust("...FileSegment::wait::beforeWait", this)` → 删(R3 已先解除测试依赖)。
- `FileCacheErrnoException.h`:
  - `:26-44` 纯注释替换(旧讲 Task-012 `LocalWriteFile` gap,新讲 `FileCacheLocalWriteFile` 契约)→ **恢复到基线注释**(满足 zero-diff 硬 gate;纯文档无逻辑)。

### factory 冲突裁决(用户已定:恢复 seam + default 换 `FileCacheLocalWriteFile`)

恢复 baseline 的完整 seam(`FileSegment.{h,cpp}`),**唯一改动**是 default factory lambda 的返回类型:

```cpp
// FileSegment.cpp 恢复的 factory 块(基线 :232-256 结构),仅 lambda body 改为 FileCacheLocalWriteFile:
namespace {
FileSegment::WriteFileFactory & writeFileFactoryStorage() {
    static FileSegment::WriteFileFactory factory = [](const std::string & path) -> std::unique_ptr<velox::WriteFile> {
        return std::make_unique<FileCacheLocalWriteFile>(path);   // baseline 此处是 velox::LocalWriteFile(path,false,false,true)
    };
    return factory;
}
}
void FileSegment::setWriteFileFactoryForTesting(WriteFileFactory factory) { writeFileFactoryStorage() = std::move(factory); }
std::unique_ptr<velox::WriteFile> FileSegment::createWriteFile(const std::string & path) { return writeFileFactoryStorage()(path); }
```

`write()`(基线 `:473`)**恢复为调 `createWriteFile(file_segment_path)`**(不是 HEAD 的直接 `make_unique<FileCacheLocalWriteFile>`):
```cpp
download->cache_writer = std::make_shared<WriteBufferFromVeloxWriteFile>(createWriteFile(file_segment_path));
```
三约束同时满足:default writer = `FileCacheLocalWriteFile`(权威 12.2);seam 在(baseline test 可注入 fault writer);结构与基线一致(仅 lambda 返回类型 + 保留 include 两处差异,符合 12.8 "FileSegment.cpp 仅 2 处")。

### 步骤

- [ ] **前置断言**:`grep -rn "submitWarm\|inflightWarmForTest" velox/ch/Disks/IO/` 为空(R2 已切断)。若非空,R2 未完成,**停止**。
- [ ] 按上面逐 hunk 清单恢复 `FileCache.h`/`FileCache.cpp`/`FileSegment.h`/`FileCacheErrnoException.h` 到基线(逐 hunk 审,不盲目 `git checkout` —— 确认没有基线之上本就该保留的非 warm 改动;从 agent 事实看这 4 文件的偏离全是 warm/注释,应可完全归零)。
- [ ] `FileSegment.{h,cpp}`:按"factory 裁决"恢复 seam + default 换 `FileCacheLocalWriteFile` + `write()` 调 `createWriteFile` + 保留 `FileCacheLocalWriteFile.h` include + 删 `TestValue.h` 与 wait hook。
- [ ] `FileSegmentTest.cpp`:恢复 baseline `PartialPhysicalAppendFailureReconcilesDownloadedToPhysical`(baseline `:321`,用 `ScopedWriteFileFactory` RAII seam `:296-314` 注入 `PartialCommitThenThrowWriteFile`;生产不调 setter)。
- [ ] 删 3 个 warm core tests(全在 `FileCacheTest.cpp`):`DeactivateWaitsForInflightWarm`(`:331`)、`SubmitWarmRejectedAfterDeactivate`(`:371`)、`DeactivateWaitsForWarmPayloadDestruction`(`:412`)。对应合同已由 R2 的 `FileCacheCoalescedLoad` tests 重建。
- [ ] 构建 core SCC + 全部 Disks/IO targets,确认 core API 恢复无断链。
- [ ] **12.8 core diff gate(硬)**:
  ```bash
  git diff 5785a43a..HEAD -- \
    velox/ch/Interpreters/FileCache/FileCache.h \
    velox/ch/Interpreters/FileCache/FileCache.cpp \
    velox/ch/Interpreters/FileCache/FileSegment.h \
    velox/ch/Interpreters/FileCache/FileCacheErrnoException.h
  # 预期:空
  git diff 5785a43a..HEAD -- velox/ch/Interpreters/FileCache/FileSegment.cpp
  # 预期仅:include FileCacheLocalWriteFile + factory lambda 返回 FileCacheLocalWriteFile(共 2 处)
  ```
  同时确认无残留:`submitWarm` / `inflightWarmForTest` / `warm_mutex` / `warm_cv` / `warm_cancel_source` / `FileSegment::wait TestValue hook` / `DownloaderLease` / 生产调 `setWriteFileFactoryForTesting`。
- [ ] 全 8 gate 回归。
- [ ] commit:`git commit -m "FileCache: restore core after IO integration"`

---

## Commit R5: 剩余 C3/C4/C6/C7 修复(限 `Disks/IO/**` + `IO/**`)

**Goal:** 完成第 11 节剩余技术条目,全部限制在 IO 层,不碰 core。每条按独立 RED/GREEN 拆 commit,不 amend。

**边界**:所有改动仅在 `velox/ch/Disks/IO/**` 和 `velox/ch/IO/**`。锚点均已核实 @ HEAD c609572d。

### R5-a: C3 补 demand/predownload source latency(11.9 残留)

**关键(避免双计)**:source read 走 base `ReadFileInputStream::read` scalar overload,它**已经**记了 `incRawBytesRead` + `incTotalScanTimeNs`(`InputStream.cpp:88-89`)。现有 `FileCacheInputStream` 注释(`:915-919`)也确认:source reads "already increments raw bytes ... do not double-count"。所以 **`rawBytesRead`/`totalScanTimeNs` 已记全,不得再加 `incTotalScanTimeNs`**。11.9 缺的**只是** `storageReadLatencyUs` / `queryThreadIoLatencyUs`(`IoStatistics.h:81`/`:85`,`IoCounter`)—— 设计 11.9 原文断言的正是 `storageReadLatencyUs().count()>0` / `queryThreadIoLatencyUs().count()>0`。

**timer 必须包住真正 IO(不是包 bytes 统计块)**:
- predownload 真正 IO 在 `reader->eof()`/`reader->next()`(触发底层 read),`got` 已读之后才到 `:665-678` 的 bytes 统计块。timer 要包**读那一步**。
- demand 真正 IO 在 `reader->next()`;`:903-923` 是读完后的 bytes 统计位置。

- [ ] RED:加 demand + predownload 各一个 case,断言的是**operation-local before/after 计数 delta**,不是最终 `count()>0`:在该 demand / predownload 真正 source read **之前**取 `before = ioStats->storageReadLatencyUs().count()`(以及 `queryThreadIoLatencyUs().count()`),读**之后**取 `after`,断言 `after - before >= 1`(该操作自身必须记一次 latency)。只看最终 `count()>0` 不够——最终值可能被同测试里其它无关 IO 抬高,反而掩盖"这次操作没记账"的 bug。用 `BlockingReadFile` 确定性阻塞 source read 一次使 delta 必然为正。**test 必须放进 `FileCacheBufferedInputBuilderTest.cpp`(= `velox_ch_filecache_connector_test`)**,因为 `BlockingReadFile`/`CountingReadFile` 定义在该文件的匿名 namespace(`:132`/`:93`),对 `FileCacheBufferedInputTest.cpp` 跨 binary 不可见 —— 放错文件会编译失败。中和这两个 latency 记账看红(delta 变 0)。**不断言 `totalScanTimeNs`(已由 base 记,双计风险)。**
- [ ] 实现:在 predownload / demand 的**真正 source read 调用**(`reader->eof()`/`reader->next()`,非 bytes 统计块)周围包 `Stopwatch`/`getCurrentTimeMicro`,读完后 `ioStats->storageReadLatencyUs().increment(elapsedUs)` + `ioStats->queryThreadIoLatencyUs().increment(elapsedUs)`,**仅 source(非 cache-hit)路径**。不加 `incTotalScanTimeNs`。
- [ ] 全 8 gate + commit(独立 RED/GREEN)。运行的是 `velox_ch_filecache_connector_test`。

### R5-b: C4 两处清理(11.10/11.5 残留)

精确现状(`FileCacheInputStream.cpp`):
- reader/segment offset 一致性断言分布三处:`getRemoteReadBuffer :243-245`(`getFileOffsetOfBufferEnd == getCurrentWriteOffset`,仅 reuse 分支)、`readFromCurrentSegment :853-855`(`getCurrentWriteOffset == reader->getPosition()`)、`reserveAndWriteSegmentChunk :604`(`getCurrentWriteOffset == offset`)。
- predownload `reserveHint` 注释在 `:680-682`,实际 `bytesToPredownload -= got` 在 `:706`,传给 reserve 的 `reserveHint = state.bytesToPredownload`(`:689`)是 **pre-decrement** 值。

- [ ] reader/segment offset 断言:审这三处,消除冗余/补齐缺失,使 reader offset 与 segment write offset 的一致性断言语义统一(不改行为,只让断言不重不漏)。若纯属重构(无行为变化),按迁移标准验证(回归绿),不强造 RED。
- [ ] predownload hint 注释:修正 `:680-682` 注释,明确 `reserveHint` 传的是 pre-decrement 值(`bytesToPredownload` 减之前),与 `:689`/`:706` 的实际顺序一致。
- [ ] 全 8 gate + commit(两处可合一或拆两个)。

### R5-c: C6 删 `PreloadedRunStream`,统一 `FileCacheInputStream`(11.6)

精确现状(`FileCacheBufferedInput.cpp`):`PreloadedRunStream` 定义 `:100-~195`(**"Zero-copy SeekableInputStream over a preloaded whole-file buffer"**,`Next` 走 `allocation_->findRun/runAt` 返回 `preloadData_` 的零拷贝 run 切片 `:120-147`);两处 `make_unique<PreloadedRunStream>` 在 `makePreloadedStream(offset,length)`(`:1342-1367`)的 `:1361`/`:1364`;`makePreloadedStream` caller 是 `enqueue :257-259`(`if (preloaded()) return makePreloadedStream(...)`,**绕过 `load()`**)+ `:1010-1012`;preload 数据由 `preload() :1079` 填 `preloadData_`(RAM,`:1169` 提交)。

**合同(不可违背)**:`preloaded()==true` 表示该 input **已持有整文件的 RAM 数据**(`preloadData_`)。业务读必须**直接返回 `preloadData_` 的零拷贝 slice**,**不得**回到 `FileSegment` 磁盘读 —— 否则重新发生磁盘 IO 且废弃已在内存的 `preloadData_`,违背 whole-file preload 的意义。

- [ ] **正确方向**:C6 是"统一 stream **类型**",不是"改数据来源"。在 `FileCacheBufferedInput` 增加设计11.6规定的 `preloadedData(offset,length) -> folly::Range<const char *>` accessor(从tinyData或non-contiguous Allocation当前run返回连续slice)。删 `PreloadedRunStream` 后,零拷贝 RAM-slice 语义要**移进 `FileCacheInputStream::Next`**:当 `bufferedInput_` 处于 `preloaded()` 状态时,调用 `bufferedInput_->preloadedData(region_.offset + position_, remaining)`取得当前连续run slice并直接发布，**不走 CACHED/FileSegment 磁盘路径**。`makePreloadedStream` 改为构造业务角色的 `FileCacheInputStream`。
- [ ] **统一published window**:当前pending fast path用 `outputBuffer_->as<char>() + offsetInOutputBuffer_`;preload slice是non-owning pointer，不能假设 `outputBuffer_`存在。增加统一 `const char * publishedData_`(或等价window pointer):owned/coalesced输出指向其 `BufferPtr` payload，preload输出指向 `preloadedData` slice；pending `Next`、`BackUp`、buffer内seek只操作 `publishedData_ + start/size/cursor`。invalidate/seek-outside/takeLast清空该pointer。`takeLastOutputBuffer`仅允许owned内部stream窗口，不能移动preload non-owning slice。
- [ ] **统一业务delivered accounting**:从 `readFromCurrentSegment` 末尾移除 `recordReadBytes`;改为在 `Next` 的每个成功返回点统一记录本次实际交付bytes，包括pending/BackUp后重交付、fresh source/cache、coalesced prepared RAM、preload RAM。`SkipInt64`不记录。内部coalesced stream的空TrackingId保持no-op。
- [ ] RED(**防伪绿**):只断言 source `preadCount` 不增**不够** —— 错误实现从本地 `FileSegment` 磁盘读同样不碰 source,仍会通过。必须同时证明是 RAM 零拷贝而非本地磁盘读:
  - `Next` 返回的 `data` 指针落在 preload buffer 内 —— **用现有 `FileCacheBufferedInput::addressInPreloadData(ptr)` helper**(`FileCacheBufferedInput.cpp:1315-1339`)判定,**不得**用单一 begin/end 指针区间:大文件 preload 是**非连续** `Allocation`(多个 run,见 `makePreloadedStream` `:1359-1365`),tinyData 才是单块;`addressInPreloadData` 已分别覆盖 tinyData 与逐 run 比较,单一首尾比较会漏判非连续 run;
  - **local/ssd read bytes 不增**:`ssdRead()` 计数 / `CachedReadBufferReadFromCacheBytes` ProfileEvent(`FileCacheInputStream.cpp:914`/`:905`)在 preload 命中读取前后不变;
  - source `preadCount` 也不增;delivered bytes / ScanTracker recordRead 语义不变(11.6 §ScanTracker)。
  中和"preloaded 分支返回 RAM slice"退回 FileSegment 磁盘读 → `ssdRead` bytes 递增 + 指针不在 preloadData_ 内,看红。
- [ ] 实现:`FileCacheInputStream` 加上述preloaded-slice、统一published window和统一delivered accounting；`makePreloadedStream`改建该stream；删 `PreloadedRunStream`类(`:100-~195`)+两处caller。
- [ ] 全 8 gate + commit。

### R5-d: C7 按 CH state 语义重修 mixed-state classifier(11.4)

精确现状:`FileCacheBufferedInput.cpp` `classifyChunk(offset,length)` 全文 `:355-495`。用 `cache_->get`(只读探针 `:369-374`);`covered`(初 `offset` `:411`)+ `anyDownloading`(`:405`)推进;段循环 `:412`;hole-before-segment→`kMiss` `:431-433`;`DOWNLOADED` `:438-451`;**`DOWNLOADING` `:454-457` 仅置 `anyDownloading` 不推进 `covered`(BUG)**;`PARTIALLY_*` `:459-476` 按 `getCurrentWriteOffset`;`EMPTY`/`DETACHED`→`kMiss` `:478-482`;尾部 `covered<chunkEnd`→`kMiss` `:489-491`;返回 `:494` `anyDownloading?kDownloading:kHit`。

- [ ] RED(reviewer 抓的真 bug):构造单个 DOWNLOADING 段完全覆盖 chunk 的场景 —— 当前 `:454-457` 只置 `anyDownloading` 不推进 `covered`,尾部 `covered<chunkEnd` 误判 `kMiss`(应 `kDownloading`)。断言应 `kDownloading`,看红。
- [ ] 实现(设计 11.4 语义):`covered` 表**几何覆盖**(非 resident);`DOWNLOADING`(`:454-457`)推进 `covered=max(covered,segEnd)` 且置 `anyDownloading=true`;`PARTIALLY_DOWNLOADED` 不足立即视 miss 的部分正确处理;`NO_CONTINUATION` 缺尾置 `anyDownloading` 但不 warm;state 与 `covered` 分离决定 hit/miss/downloading(即 `covered>=chunkEnd` 才非 miss,再由 `anyDownloading` 决 hit vs downloading)。
- [ ] 全 8 gate + commit。

---

## 最终收尾(R1–R5 全绿后)

- [ ] **12.8 全部 gate 复核**:core diff 空、`FileSegment.cpp` 仅 2 处、禁止残留列表全空、功能 gate 全过、DWRF+Parquet format E2E cold fill/warm hit 过。
- [ ] **第 11 节闭环核对**:11.3/11.4/11.5/11.6/11.7/11.8/11.9/11.10 技术条目逐条确认已由 R1–R5 覆盖(映射见 12.1.2)。
- [ ] **gluten 一次性 cherry-pick**:把 R1–R5 全部 commit cherry-pick 到 `filecache2-gluten`,解决 `downloadChunkIntoSegment`/`writeCache` 的 `std::exception` vs `FileCacheErrnoException` 历史分叉(gluten 侧当前 catch `std::exception`,缺 7.5/8.3 typed 分类),使两分支逻辑一致。切回 `filecache2`。
- [ ] 报告用户,等 push 指令(默认不 push)。

---

## Self-Review 记录

- **Spec coverage**:12.6.1–12.6.5→R1;12.4→R2;12.5→R3;12.2/12.6.1 core→R4;11.4/11.6/11.9/11.10 残留→R5;12.8→收尾。11.3(FileIoContext)/11.7(coalesced 执行模型)已由 C2 及 R2 覆盖。11.8(writer factory 删除)由 C1 完成 + R4 恢复基线时保留 typed factory。11.11(RAII lease)已 revert,基线无此抽象。
- **顺序**:R1→R2→{R3,R4}→R5,R4 晚于 R2(切 caller)、R3 晚于 R2(用新 load)、R5 晚于 R4(core 稳定)——均从代码核实。
- **粒度**:R1–R5 均已细化到 step 级(带核实过的 file:line @ HEAD c609572d5),**R2-6 除外——它是测试矩阵 checklist,每条 material case 落为单独命名 gtest,而非 code-level 生产 step**。R2 拆 6 个分子任务(R2-1…R2-6),各独立 RED/GREEN。开工仍按阶段推进,每阶段前可再核对锚点是否因前序 commit 漂移。
- **裁决固化**:(1) R4 factory —— 恢复 seam + default lambda 换 `FileCacheLocalWriteFile` + `write()` 调 `createWriteFile`(用户已定,commit `e8c2cc1a458`);(2) `getData` —— 保留 `requestIndices` 批量 + `BufferPtr` 版,相对 Velox 标准 `getData(offset,Allocation&,string&)` 的有意偏离(用户已定,commit `7567b7c2371`)。
- **禁止项**:异常测试不加生产 hook/callback/错误开关/test-only 接口;不用 sleep;不 amend/rebase;默认不 push。
