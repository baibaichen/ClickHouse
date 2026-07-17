# 06. `getCallerId` / downloader execution owner设计

## 结论

ClickHouse `FileCache` 里的 caller id 业务含义是 **downloader ownership token**：

```text
某个读/写 execution 成为当前 file segment 的 downloader
在 downloader lease 释放前，只有同一个 caller 才能 reserve / write / complete
lease 释放后，其他 caller 可以重新抢占 downloader
```

它不是用户身份、stream identity，也不是“这个 file segment 以后永远只能由某个固定
线程执行”。ClickHouse 当前把 caller id 实现为 `queryId:threadId`，用物理执行线程
区分同一个 query 内的并发 reader。

Velox 迁移保留这个物理线程语义。`driverId` 是逻辑 driver 实例，不等价于
`threadId`，不能替代 downloader owner。第一阶段使用 `FileCacheQueryIdScope` 给当前
线程提供 query id，`FileSegment::getCallerId` 继续组合 query id 和当前 OS thread id。

## ClickHouse 当前语义

ClickHouse 当前实现：

```text
getCallerId:
  if no CurrentThread query id:
      "None:<thread-name>:<thread-id>"
  else:
      "<query-id>:<thread-id>"
```

`FileSegment::getOrSetDownloader` 的语义：

```text
if downloader_id empty and state allows new downloader:
    downloader_id = getCallerId()
    state = DOWNLOADING
return downloader_id
```

后续只允许同一个 caller 执行 downloader-only 操作：

```text
reserve
write
completePartAndResetDownloader
resetDownloader
complete
```

`isDownloader` / `assertIsDownloaderUnlocked` 本质都是：

```text
getCallerId() == downloader_id
```

所以 caller id 是一个短期 lease owner。它保证不会有两个并发 reader 同时写同一个
cache segment。

## 同步执行区间

caller id 在当前 downloader lease 期间必须稳定，因此 lease 不能跨物理线程迁移。

如果 execution 在 lease 未释放前换到另一个 OS thread，ClickHouse 的 `getCallerId`
会算出不同字符串：

```text
old: query-1:thread-10
new: query-1:thread-42
```

此时新线程不再是 downloader，继续 `reserve` / `write` / `complete` 会触发校验失败。
ClickHouse 的调用方通过把 lease 限制在一次同步执行区间内避免这种情况：

```text
CachedOnDiskReadBufferFromFile::nextImplStep:
  每次同步调用结束前 completePartAndResetDownloader

CachedOnDiskReadBufferFromFile::readBigAt:
  在整个同步调用内持有 downloader，返回前释放

CacheMetadata::downloadThreadFunc:
  background worker 在本线程内获取、下载并释放
```

同一个 processor/driver 下一次可能在另一个 worker 上执行，但上一次 lease 已经释放。
新的物理线程重新竞争 downloader。background download 同样使用自己的 worker identity，
不继承 foreground reader 的 caller id。

## Velox 里可用的身份

`BufferedInputBuilder::create` 已经拿到 `ConnectorQueryCtx`。其中可用：

```text
queryId
taskId
planNodeId
driverId
scanId = taskId.planNodeId
cancellationToken
```

`FileConnectorSplit` 里还有：

```text
filePath
start
length
splitWeight
cacheable
```

Velox `Driver` 里也有当前 OS thread 状态：

```text
ThreadState.thread = 当前 std::thread::id
ThreadState.tid    = 当前 OS tid
```

但 `driverId` 和 OS `threadId` 不是同一个概念：

```text
driverId:
  逻辑 driver 实例 id，创建 DriverCtx 时固定

threadId / tid:
  当前执行这个 driver 的物理线程
```

同一个 `driverId` 同一时刻不应同时出现在两个线程上；Velox 用 `ThreadState` 的
on-thread 状态防止这种情况。但是同一个 `driverId` 后续 resume 时可能换到另一个
executor thread。这个 thread change 应产生新的 caller id；前一次 downloader lease
必须在 driver off-thread 前结束。

## 设计选择

### 方案 A：保留 ClickHouse `queryId:currentOsThreadId`

```text
caller = FileCacheQueryIdScope::queryId + ":" + folly::getOSThreadID()
```

这是推荐方案。它保持 CH 的 execution ownership 粒度：

```text
same query + same physical thread  -> same caller during synchronous call
same query + different thread      -> different caller
same driver resumed on new thread  -> different caller
background worker                  -> its own thread caller
```

`FileCacheQueryIdScope` 只负责把 `ConnectorQueryCtx::queryId` 暴露给当前同步调用：

```cpp
class FileCacheQueryIdScope
{
public:
    explicit FileCacheQueryIdScope(std::string_view query_id);
    ~FileCacheQueryIdScope();

    FileCacheQueryIdScope(const FileCacheQueryIdScope &) = delete;
    FileCacheQueryIdScope & operator=(const FileCacheQueryIdScope &) = delete;

    static std::string_view currentQueryId();

private:
    std::string previous_query_id;
};
```

thread-local 状态隐藏在 `.cpp` 中并支持嵌套 scope；析构恢复之前的 query id。
`FileCacheQueryLimit` 也通过 `currentQueryId` 复用同一 execution identity，不再引入第二套
thread-local query context。
`FileSegment::getCallerId` 保留静态无参 API：

```text
if FileCacheQueryIdScope has query id:
    "<query-id>:<os-thread-id>"
else:
    "None:<os-thread-id>"
```

thread name 只用于诊断，不参与 correctness；实现时如果 Folly 已有可靠的当前 thread-name
API，可以保留 CH 的 `None:<thread-name>:<thread-id>` 形态。

#### 为什么需要 `FileCacheQueryIdScope`

`FileCacheQueryIdScope` 的目的不是保存 downloader，而是让无参的
`FileSegment::getCallerId` 能取得当前 query id。

Velox executor thread 会被不同 query 重复使用。只用 TID 会把同一个 worker 上先后执行
的两个 query 变成相同 caller：

```text
TID only:
  Q1 on thread 101 -> 101
  Q2 on thread 101 -> 101

CH identity:
  Q1 on thread 101 -> Q1:101
  Q2 on thread 101 -> Q2:101
```

正常路径会在同步调用结束前释放 downloader，但 query id 仍可防止后续 query 在同一
worker 上误认异常遗留的 lease，并保留 CH 的诊断信息。

Velox 的 query id 位于 `ConnectorQueryCtx` / `FileCacheInputStream`，`FileSegment`
无法像 CH 那样从 `CurrentThread` 直接读取，因此有两个选择：

```text
1. 给所有 downloader-only FileSegment API 显式传 query id
2. 在同步入口用 FileCacheQueryIdScope 临时设置 thread-local query id
```

第一阶段选择 `FileCacheQueryIdScope`，因为它只替代 CH `CurrentThread` 的这一个能力，
不修改 `FileSegment` / `FileSegmentsHolder` 的 API 和 cleanup 结构。scope 退出时恢复
之前的 query id；它不跨 executor thread，也不让 downloader lease 跨同步调用。

### 方案 B：显式 operation token

也可以在每次同步 `Next` / positioned read / background download 开始时生成唯一 operation
token，并把它显式传给所有 downloader-only API。

这个方案能表达相同粒度，但会修改 `FileSegment` 的全部相关方法签名，并需要重新处理
`FileSegmentsHolder` 析构时的异常清理。第一阶段不采用。

### 不采用：稳定 driver / stream token

以下 identity 不符合 CH：

```text
queryId + driverId
queryId + scanId + driverId + streamSequence
```

它们会让同一 logical stream 在换到另一个物理线程后仍被视为原 downloader，扩大 lease
范围。如果同一个 stream 出现并发调用，还可能让两个物理执行线程通过同一个 token
同时通过 downloader 校验。

## 推荐落地

保留 `FileSegment` API 和 holder cleanup 形态：

```text
BufferedInputBuilder::create
  -> build FileCacheRequestContext
  -> FileCacheBufferedInput
  -> FileCacheInputStream stores queryId

FileCacheInputStream synchronous entry
  -> FileCacheQueryIdScope(queryId)
  -> FileSegment::getOrSetDownloader()
  -> FileSegment::isDownloader()
  -> FileSegment::reserve/write/complete()
  -> release downloader before scope exits
```

必须在所有可能进入 file-segment 状态机的同步入口建立 scope，包括：

```text
FileCacheInputStream::Next
future explicit prefetch task if it can elect a downloader
seek/reset path if it completes a held segment
FileCacheInputStream destructor
```

第一版 `FileCacheBufferedInput::load` 不解引用 stream且不进入 segment状态机，因此不建立
query scope。

析构函数必须在 scope 仍然生效时显式 complete/reset holder；不能只依赖成员在析构函数体
之后自动析构。

background download 不建立 query scope，直接使用当前 background worker 的 thread id。
未来如果增加 async/future API，不能让 downloader lease 跨 future suspension；必须在
返回 future 前释放 lease，resume 后重新竞争。

## 与 ClickHouse 行为的对应关系

| ClickHouse | Velox port |
|---|---|
| `CurrentThread` query id | `FileCacheQueryIdScope` 中的 `ConnectorQueryCtx::queryId` |
| `getThreadId` | `folly::getOSThreadID` |
| `downloader_id` string | 保持 string |
| `FileSegment::getCallerId` | 保持静态无参 API |
| background downloader | 使用 background worker 自己的 TID |

核心不变：

```text
同一时刻一个 segment 只能有一个 downloader
downloader-only 操作必须由当前物理执行线程完成
lease 不跨同步调用的调度边界
lease reset/complete 后其他 owner 可以接手
```

## 测试要求

需要覆盖：

```text
同一 query、同一 physical thread 在 scope 内 caller id 稳定
同一 query 的两个 physical threads caller id 不同
同一 driver 换 OS thread 后 caller id 变化
一次同步调用结束前 downloader 已 reset/complete
另一个 physical thread 不能 reserve/write 当前 downloader 的 segment
complete/reset 后另一个 physical thread 可以重新抢 downloader
background worker 使用自己的 caller id 抢 PARTIALLY_DOWNLOADED segment
holder 在 downloader 所在线程析构时可以完成清理
其他线程析构 holder 不会清理当前 downloader
```

## Review 状态

`getCallerId` 保留 CH 的 physical-thread execution ownership。Velox port 使用
`FileCacheQueryIdScope` 提供 query id，并用 `folly::getOSThreadID` 提供物理线程维度。
不使用 `driverId` / stream identity 替代 thread id。
