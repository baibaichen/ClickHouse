# 08. `getCallerId` / downloader ownership token 设计

## 结论

ClickHouse `FileCache` 里的 caller id 业务含义是 **downloader ownership token**：

```text
某个读/写 execution 成为当前 file segment 的 downloader
在 downloader lease 释放前，只有同一个 caller 才能 reserve / write / complete
lease 释放后，其他 caller 可以重新抢占 downloader
```

它不是用户身份，也不是“这个 file segment 以后永远只能由某个固定线程执行”。
ClickHouse 当前把 caller id 实现为 `queryId:threadId`，只是用物理线程区分同一个
query 内的并发 reader。

Velox 迁移不要声称 `driverId` 等价于 `threadId`。`driverId` 是逻辑 driver 实例，
同一时刻通常只会 on-thread 在一个 OS thread 上执行，但 driver 下一次 resume
可能换到另一个 executor thread。

因此建议新增显式 `FileCacheCallerToken`，表达 downloader lease owner，而不是依赖
Velox 全局线程状态。

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

## “固定线程”问题

这个 token 在当前 lease 期间必须稳定。

如果同一个下载 continuation 在 lease 未释放前换到另一个 OS thread，ClickHouse 的
`getCallerId` 会算出不同字符串：

```text
old: query-1:thread-10
new: query-1:thread-42
```

此时新线程不再是 downloader，继续 `reserve` / `write` / `complete` 会触发
downloader 校验失败。

但是这不表示 segment 永久绑定原线程：

```text
downloader complete/reset
  -> downloader_id 清空
另一个 caller getOrSetDownloader
  -> 成为新的 downloader
```

background download 也是重新抢 downloader lease，而不是继承原 reader 的物理线程。

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
executor thread。

## 设计选择

### 方案 A：模拟 ClickHouse `queryId:currentOsThreadId`

```text
caller = ConnectorQueryCtx::queryId + ":" + current_os_thread_id
```

优点：

- 形态最接近 ClickHouse。
- 对纯同步读路径容易理解。

问题：

- Velox prefetch、async IO、background continuation 可能跑在 executor 线程上。
- 如果 downloader lease 跨线程 continuation，caller 会变化。
- 需要强约束：downloader-only 操作必须在同一个 OS thread 内完成。

### 方案 B：使用 `queryId:driverId`

```text
caller = queryId + ":" + driverId
```

优点：

- 对 Velox driver 生命周期更稳定。
- 不受 executor 物理线程切换影响。

问题：

- `driverId` 不是 `threadId`。
- 同一个 driver 里可能创建多个 `FileCacheInputStream`，只用 `driverId` 粒度偏粗。
- 容易掩盖同一 driver 内多个 stream 的 ownership bug。

### 方案 C：显式 `FileCacheCallerToken`

推荐方案：

```text
FileCacheCallerToken {
    queryId
    scanId
    driverId
    filePath
    split range
    stream sequence
}
```

字符串化时可以保留可读形态：

```text
<queryId>:<scanId>:driver-<driverId>:stream-<seq>
```

其中：

- `queryId` 对齐 ClickHouse query 维度。
- `scanId` 表达 Velox scan 维度，已经用于 scan 内共享状态。
- `driverId` 表达并发 driver 维度，但不宣称它是 OS thread。
- `stream sequence` 区分同一 driver 内多个 `FileCacheInputStream`。
- `filePath` / split range 可以用于日志和 debug，也可以参与 token 生成以避免跨 split
  混淆。

## 推荐落地

在 Velox port 里不要保留静态 `FileSegment::getCallerId` 查询全局状态的模式。
改成显式传参：

```cpp
struct FileCacheCallerToken
{
    std::string queryId;
    std::string scanId;
    int driverId;
    std::string filePath;
    uint64_t splitStart;
    uint64_t splitLength;
    uint64_t streamSequence;

    std::string toString() const;
};
```

调用链：

```text
BufferedInputBuilder::create
  -> build FileCacheRequestContext
  -> FileCacheBufferedInput
  -> FileCacheInputStream creates FileCacheCallerToken
  -> FileSegment::getOrSetDownloader(token)
  -> FileSegment::isDownloader(token)
  -> FileSegment::write(token, ...)
  -> FileSegment::complete(token, ...)
```

background continuation 要么：

```text
重新生成自己的 background caller token
```

要么：

```text
显式携带原 token，并保证 downloader-only 操作仍用同一个 token
```

不要隐式依赖当前 executor thread id。

## 与 ClickHouse 行为的对应关系

| ClickHouse | Velox port |
|---|---|
| `queryId` | `ConnectorQueryCtx::queryId` |
| `threadId` | 不直接等价；可用 `driverId + streamSequence` 表达逻辑 reader |
| `downloader_id` string | `FileCacheCallerToken::toString` |
| `getCallerId` 静态读取线程状态 | 从调用链显式传入 token |
| background downloader 重新抢 lease | background task 使用自己的 explicit token |

核心不变：

```text
同一时刻一个 segment 只能有一个 downloader
downloader-only 操作必须由当前 lease owner 执行
lease reset/complete 后其他 owner 可以接手
```

## 测试要求

需要覆盖：

```text
同一 stream 抢到 downloader 后可以 reserve/write/complete
另一个 stream 不能写当前 downloader 拥有的 segment
complete/reset 后另一个 stream 可以重新抢 downloader
同一 driver 的两个 stream token 不相等
同一个 driver resume 到不同 OS thread 时 token 不变化
background downloader 使用独立 token 可以重新抢 PARTIALLY_DOWNLOADED segment
```

## Review 状态

`getCallerId` / `getThreadId` 不直接映射到 Velox OS thread 状态。当前设计采用
显式 `FileCacheCallerToken` 表达 downloader ownership，避免把 `driverId` 误写成
`threadId` 等价物。
