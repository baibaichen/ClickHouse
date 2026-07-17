# 09. `FileSegment` 文件迁移设计

## 结论

这一组严格按两个文件 review：

```text
src/Interpreters/FileCache/FileSegment.h
src/Interpreters/FileCache/FileSegment.cpp
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/FileSegment.h
velox/ch/Interpreters/FileCache/FileSegment.cpp
```

This batch is an exact algorithm and lifecycle-semantics port, not a source-level copy or redesign.

本批次精确迁移算法和生命周期语义，不要求逐行复制源码。只允许下文列出的、已经
review 的基础设施替换。以下行为都不能改变：

```text
inclusive Range semantics
file-segment state machine
physical-thread downloader ownership
reservation and write-offset invariants
remote-reader handoff
completion / shrink / detach behavior
FileSegmentsHolder RAII cleanup
partial-write bookkeeping and failure propagation
```

不能逐行复制的部分仅限基础设施：

```text
CH basic aliases -> ClickHouseAliases.h
CurrentThread query id -> FileCacheQueryIdScope
getThreadId -> folly::getOSThreadID
ReadBufferFromFileBase -> ReadBufferFromVeloxReadFile
WriteBufferFromFile -> WriteBufferFromVeloxWriteFile
QueryStatus cancellation -> folly::CancellationToken
OpenedFileCache singleton -> manager-owned opened-file cache
CH metrics/tracing/failpoints/logging -> reviewed compatibility shims
Exception / ErrnoException -> FileCache exception compatibility layer
boost::noncopyable -> explicitly deleted copy operations
POSIX local-read flags -> manager-owned opened-file cache options
```

这些替换可以改变类型、include、异常类型/文本和资源注入方式，但不能改变正常路径或失败
路径的 control flow。特别是：

```text
caller identity remains query-id + current physical TID
wait still observes cancellation while blocked
short writes still reconcile the actual on-disk prefix before propagating failure
opened handles are still invalidated before a path can be reused
```

所以这里的 exact 含义是 behavioral equivalence，不是 source-level identity。

## 文件依赖结论

`FileSegment.h` 主要依赖已经 review 的叶子类型：

```text
FileCacheKey.h
FileSegmentInfo.h
IFileCachePriority.h
Guards.h
FileCache_fwd_internal.h
ReadBufferFromVeloxReadFile.h
WriteBufferFromVeloxWriteFile.h
```

`FileSegment.cpp` 属于中心 SCC：

```text
FileSegment.cpp
  -> FileCache::tryReserve / tryIncreasePriority / config getters
  -> LockedKey remove/shrink/download-queue APIs
  -> KeyMetadata path/origin APIs

Metadata.cpp
  -> FileSegment state/range/downloader/write/complete APIs

FileCache.cpp
  -> creates and owns FileSegment through metadata
```

因此两个文件按文件 review，但实现时仍要和 `Metadata.cpp`、必要的 `FileCache.cpp` API
在同一批次形成可链接闭环。不能为了提前编译 `FileSegment.cpp` 而制造假的
`LockedKey` / `FileCache` 接口。

## `FileSegment.h`

### `CreateFileSegmentSettings`

直接迁移：

```cpp
struct CreateFileSegmentSettings
{
    FileSegmentKind kind = FileSegmentKind::Regular;
    bool unbounded = false;

    CreateFileSegmentSettings() = default;

    explicit CreateFileSegmentSettings(FileSegmentKind kind_)
        : kind(kind_)
        , unbounded(kind == FileSegmentKind::Ephemeral)
    {
    }
};
```

保留：

```text
Regular  -> bounded by the requested segment range
Ephemeral -> unbounded and removed when the last holder completes
```

即使第一阶段后置 `WriteBufferToFileSegment`，也不能删除 `Ephemeral` / `unbounded`
语义；metadata startup 仍需识别和清理 temporary segment。

### `Range`

`Range` 是 inclusive `[left, right]`，直接迁移：

```text
size = right - left + 1
contains(point) includes both boundaries
contains(range) includes both boundaries
operator< means strictly non-overlapping and ordered
```

不能改成 Velox 常见的 `[offset, offset + length)` 而只在部分调用点转换；这会影响
metadata adjacency、write offset、segment shrink 和 completion 判断。

### 类接口

保留现有 API 分组：

```text
constant state:
  range / key / offset / kind / path

any holder:
  getOrSetDownloader / isDownloader / wait
  downloaded/reserved/write offsets
  detach / complete / increasePriority

cache:
  segment lock
  priority queue iterator
  key metadata access

downloader only:
  reserve / write
  remote reader get/set/reset/extract
  completePartAndResetDownloader / resetDownloader
```

`FileSegment` 不变成 Velox stream 或 cache entry 基类，也不继承 `AsyncDataCache`
类型。

### 非拷贝语义

Velox 没有使用 `boost::noncopyable` 的现有惯例。可以机械替换为：

```cpp
FileSegment(const FileSegment &) = delete;
FileSegment & operator=(const FileSegment &) = delete;
```

`FileSegmentsHolder` 同样显式删除 copy constructor / copy assignment。这个替换不改变
ownership。

### reader / writer ownership

保持 CH 两层 ownership：

```cpp
using RemoteFileReaderPtr = std::shared_ptr<ReadBufferFromVeloxReadFile>;
using LocalCacheWriterPtr = std::shared_ptr<WriteBufferFromVeloxWriteFile>;
```

```cpp
class WriteBufferFromVeloxWriteFile
{
    std::unique_ptr<WriteFile> file_;
};
```

含义：

```text
FileSegment / WriteBufferToFileSegment can share the writer wrapper
the wrapper exclusively owns the underlying Velox WriteFile
```

第一阶段虽然后置 `WriteBufferToFileSegment`，也保留 `LocalCacheWriterPtr` 和
`getLocalCacheWriter` 的 CH API，避免为了当前未使用而改变 `FileSegment.h`。

### downloader caller identity

保留无参静态 `FileSegment::getCallerId`。按
[caller identity设计](../1-dependencies/06-filecache-caller-token-design.md)：

```text
query path:
  FileCacheQueryIdScope(queryId)
  getCallerId -> <query-id>:<folly::getOSThreadID()>

background path:
  no query scope
  getCallerId -> None:<background-worker-tid>
```

caller id 表示当前物理执行线程的短期 downloader lease，不是 driver/stream identity。
不向 `FileSegmentsHolder` 添加 token，也不修改全部 downloader-only 方法签名。

### `wait` cancellation

CH `wait` 从 `CurrentThread` 取得 `QueryStatus`。Velox 不迁移完整 `CurrentThread`，因此
只对 `wait` 显式注入 cancellation token：

```cpp
State wait(
    size_t offset,
    const folly::CancellationToken & cancellation_token);
```

保留：

```text
condition-variable predicate
one-second cancellation check slices
60-second overall wait deadline
offset < current write offset early completion
```

token 被取消时抛出明确的 Velox cancellation exception，不能继续使用 no-op
`QueryStatus`。background download 不调用 `wait`。

### `getFlagsForLocalRead`

CH `getFlagsForLocalRead` 同时服务 local reader 构造和按 `(path, flags)` 失效
`OpenedFileCache`。Velox manager-owned opened-file cache 按 path 管理所有 handle 变体，
因此不把 POSIX flags 暴露为 `FileSegment` 公共 API：

```text
local open options -> OpenedFileCache / local FileSystem wrapper
invalidate         -> openedFileCache.remove(path)
```

这只是 file-handle 基础设施替换，不改变 segment path 或删除时机。

### 成员与不变量

成员语义直接迁移：

```text
file_key                    immutable
segment_range               changes only for unbound resize or final shrink
segment_kind                immutable
is_unbound                  immutable
background_download_enabled immutable
size_in_filename            atomic false -> true only
download_state              atomic; terminal state published last
downloaded_size             atomic
reserved_size               atomic; always >= downloaded_size
download_data               lazy, only needed while downloading
segment_guard               serializes mutable segment state
key_metadata                weak_ptr to avoid ownership cycle
queue_iterator              priority entry after first successful reserve
cache                       non-owning pointer
cv                          wakes waiters on progress/lease release/failure
increasing_priority         deduplicates concurrent priority bumps
hits_count                  cache-hit snapshot
on_delayed_removal          guarded by segment_guard
```

`DownloadState` 直接保留：

```text
downloader_id
remote_file_reader
cache_writer
debug/sanitizer write mutex
```

它在 downloader 被设置时 lazy create；segment 进入 `DOWNLOADED` / `DETACHED` 后释放。
已经下载完成的常见 segment 不承担 download-only state 的内存。

### `FileSegmentsHolder`

直接迁移 holder 的 RAII 语义：

```text
owns a list of shared FileSegment references
destructor calls reset
reset completes and pops every segment
completeAndPopFront completes exactly one segment
```

holder 不是 downloader owner。`FileSegment::complete` 通过当前
`FileSegment::getCallerId` 判断析构线程是否是 downloader：

```text
same physical caller -> release its downloader lease
different caller     -> do not disturb the active downloader
```

`FileCacheInputStream` 析构时必须在 `FileCacheQueryIdScope` 生效期间显式完成/reset
holder，不能只依赖成员在析构函数体之后自动析构。

## `FileSegment.cpp`

### 构造

只允许初始状态：

```text
EMPTY
DOWNLOADED
DETACHED
```

保留：

```text
EMPTY:
  key metadata exists
  no file / queue iterator / downloaded bytes

DOWNLOADED:
  downloaded_size = reserved_size = range.size
  queue iterator exists
  key metadata exists

DETACHED:
  no cache ownership requirements
```

`size_in_filename == true` 只允许和 `DOWNLOADED` 一起从 startup metadata 创建。

### 状态机

直接迁移以下转换：

```text
EMPTY
  -- getOrSetDownloader --> DOWNLOADING

PARTIALLY_DOWNLOADED
  -- getOrSetDownloader --> DOWNLOADING

DOWNLOADING
  -- full complete --> DOWNLOADED
  -- partial release --> PARTIALLY_DOWNLOADED
  -- zero-byte release --> EMPTY
  -- failure/no continuation --> PARTIALLY_DOWNLOADED_NO_CONTINUATION

any non-completed state
  -- forced metadata removal --> DETACHED
```

`DOWNLOADED` 和 `DETACHED` 是 terminal states。terminal state 必须在 writer/reader、
range、size、metadata references 全部 final 后最后发布。

### downloader lease

直接迁移：

```text
getOrSetDownloader:
  only EMPTY / PARTIALLY_DOWNLOADED can elect a new downloader
  election and state transition happen under segment_guard

assertIsDownloaderUnlocked:
  current query-id:TID must equal downloader_id

resetDownloader:
  derive EMPTY / PARTIALLY_DOWNLOADED / DOWNLOADED
  clear downloader id
  notify waiters
```

同一 query 的不同 physical workers 是不同 callers。driver 在新线程 resume 后重新竞争，
不能继承旧线程的 lease。

### remote reader handoff

保持 reader ownership：

```text
REMOTE_FS_READ_AND_PUT_IN_CACHE:
  downloader can attach shared remote reader to segment

PARTIALLY_DOWNLOADED:
  background downloader can continue using the attached reader

PARTIALLY_DOWNLOADED_NO_CONTINUATION / DOWNLOADED:
  bypass reader can extract the reader
```

在发布 `PARTIALLY_DOWNLOADED_NO_CONTINUATION` 或释放 downloader 前，必须先撤回仍借用调用方
buffer 的 remote reader。不能删除现有 `resetRemoteFileReader` / `extractRemoteFileReader`
顺序约束。

### `wait`

保持：

```text
no downloader or requested offset already downloaded -> return immediately
EMPTY -> logical error
DOWNLOADING -> wait for state change or requested offset progress
```

每一秒检查 `folly::CancellationToken`。达到 60 秒 deadline 后返回当前 state，调用方按
现有状态机继续处理；不能把 deadline 改成永久阻塞。

### `reserve`

直接迁移：

```text
zero reservation -> logical error
only downloader can reserve
downloaded_size + requested size <= bounded range
reuse already reserved but not downloaded bytes first
reserve granularity can reserve ahead
reserve_hint caps reserve-ahead at current read horizon
never reserve less than the current write requires
unbounded segment can extend inclusive range
FileCache::tryReserve owns eviction/quota decision
failed reserve -> PARTIALLY_DOWNLOADED_NO_CONTINUATION
```

不能把 `reserve_hint` 或 reserve-ahead 逻辑移动到 reader；它属于 segment reservation
contract。

### local writer open

`FileSegment::write` 第一次需要 writer 时，通过 local Velox `FileSystem` 创建
`WriteFile`：

```text
downloaded_size == 0:
  create a new file; existing path is an error

downloaded_size > 0:
  open existing partial file with shouldThrowOnFileAlreadyExists = false
  LocalWriteFile seeks to the file end
```

随后包装成 `std::shared_ptr<WriteBufferFromVeloxWriteFile>`。正常写路径保持：

```text
cache_writer.set(from, size, size)
cache_writer.next()
downloaded_size += size
```

只有当前 downloader 可以写，并且必须同时满足：

```text
size > 0
state == DOWNLOADING
offset == range.left + downloaded_size
reserved_size - downloaded_size >= size
bounded writes stay inside range
```

### short write / disk-full 失败路径

`WriteBufferFromVeloxWriteFile` 的 short-write 场景、retry 和正常路径性能 contract
统一定义在 [`FileCache` 底层设施替换矩阵](../1-dependencies/01-filecache-infra-mapping.md)。

`FileSegment::write` 只依赖 wrapper 的结果：

```text
full requested buffer written:
  increment downloaded_size by requested size

write exception after partial progress:
  receive/read actual on-disk file size
  validate downloaded_size <= actual file size <= reserved_size
  reconcile downloaded_size with actual file size
  set PARTIALLY_DOWNLOADED_NO_CONTINUATION
  rethrow the write exception
```

当原 `downloaded_size == 0` 时，保持 CH 行为并删除失败的新文件。已有 partial segment
则保留成功写入的前缀，后续可以重新竞争 downloader。

### completion

直接迁移 completion matrix：

| state | last holder | behavior |
|---|---:|---|
| `DOWNLOADED` | any | verify final size/state; no mutation |
| `DOWNLOADING` | false | active downloader continues |
| `EMPTY` | true | remove empty segment |
| `PARTIALLY_DOWNLOADED` | true | enqueue background continuation or finalize and shrink |
| `PARTIALLY_DOWNLOADED_NO_CONTINUATION` | true | remove zero-byte segment or finalize and shrink |
| `Ephemeral` | true | remove segment |

如果 completion caller 是 downloader，先根据当前 downloaded size 结束
`DOWNLOADING`，再 clear downloader。

### shrink

只允许最后一个 holder shrink。保留：

```text
result_size = downloaded_size when force shrink
otherwise round up to boundary alignment and cap at original range
return reserved-but-not-downloaded surplus to priority quota
shrink inclusive range.right
downloaded_size == result_size -> DOWNLOADED
otherwise -> PARTIALLY_DOWNLOADED
```

不能漏掉 reserve-ahead surplus 的 `queue_iterator->decrementSize`，即使 alignment 后
`result_size == old range.size`。

### writer finalization 和最终文件名

writer finalize/reset 完成后，regular downloaded segment 从：

```text
<offset>
```

rename 为：

```text
<offset>_<size>
```

`Ephemeral` 保持 `<offset>_temporary`。

rename 是 startup metadata load 的优化，不是 segment correctness 前提。保留 best-effort
行为：

```text
rename success:
  size_in_filename = true
  invalidate old path in manager-owned opened-file cache

rename failure:
  keep legacy <offset> path
  size_in_filename remains false
  startup loader falls back to file_size
```

不能在发布 `DOWNLOADED` 之后再 rename；否则 lock-free `getPath` 可能观察到 state/path
不一致。

### failure

写失败必须：

```text
set PARTIALLY_DOWNLOADED_NO_CONTINUATION
cancel/reset cache writer
reset remote reader
notify waiters
propagate the exception
```

不能新增 success-shaped fallback。rename failure 是现有、明确的 metadata-load
optimization fallback，和 data write failure 不同。

### detach

直接迁移：

```text
clear downloader
publish DETACHED
reset key_metadata weak_ptr
reset priority iterator
cancel writer
release DownloadState
```

`DETACHED` segment 不再属于 `FileCache` metadata，但已有 holder 仍可持有 shared pointer。
任何后续 stateful operation 必须失败。

### priority

保留 `increasePriority` 的 atomic-flag try-lock：

```text
concurrent calls are deduplicated
queue iterator missing -> no priority mutation
FileCache::tryIncreasePriority owns policy-specific operation
hits_count increments only for a real queue entry
```

不能用 mutex 阻塞所有 concurrent priority bumps。

### 析构与 holder cleanup

`FileSegment` 析构保留 best-effort writer finalize 和 metrics shim 调用。

`FileSegmentsHolder::reset` 必须遍历并完成全部 segments；单个 completion exception
仍按 CH destructor-safe 路径记录并继续清理其余 segments。第一阶段 logging shim 是 no-op，
但不能删除 catch/continue 的异常安全结构。

## 基础设施处理

### 直接替换

```text
String -> ClickHouseAliases.h String
magic_enum FileSegmentKind string -> reviewed switch helper
getThreadId -> folly::getOSThreadID
ReadBufferFromFileBase -> ReadBufferFromVeloxReadFile
WriteBufferFromFile -> WriteBufferFromVeloxWriteFile
```

### compatibility shims

```text
ProfileEvents / CurrentMetrics
OpenTelemetry::SpanHolder
FailPoint
Logger / LOG_* / tryLogCurrentException
scope guards
FileCache exception context
```

这些 shim 不得改变 control flow，也不得在 disabled logging path eager format。

### manager/config injection

```text
OpenedFileCache::instance().remove(path, flags)
  -> manager-owned openedFileCache.remove(path)

CurrentThread QueryStatus lookup in wait
  -> explicit folly::CancellationToken
```

不创建新的 singleton。

## 测试要求

### `FileSegment.h` / basic state

```text
inclusive Range boundaries and size
Ephemeral implies unbounded
only EMPTY / DOWNLOADED / DETACHED can be constructed
downloaded startup segment initializes downloaded/reserved sizes
```

### caller / downloader

```text
same query and same physical thread has stable caller id inside scope
same query on two physical threads has different caller ids
same driver on a new worker gets a different caller id
only current downloader can reserve/write/reset
release allows another physical thread to acquire
background worker acquires its own lease
```

### reserve / write

```text
zero reserve/write rejected
write without reserve rejected
wrong write offset rejected
bounded write beyond range rejected
reserve granularity and reserve_hint
unbounded range growth
append to existing partial file
successful write adds no file-size syscall beyond existing assertions/build mode
short write followed by successful retry
short write followed by ENOSPC/EDQUOT-style failure
on-disk partial size is reconciled before failure propagates
```

### wait / reader handoff

```text
wait returns when requested offset becomes available
wait returns on state transition
wait observes folly cancellation token
remote reader can continue in background
reader is withdrawn before no-continuation state is published
eligible reader can be extracted for bypass
```

### completion / lifetime

```text
EMPTY -> removed by last holder
PARTIALLY_DOWNLOADED -> background queue or shrink
PARTIALLY_DOWNLOADED_NO_CONTINUATION zero bytes -> removed
partial reserve surplus returned to priority quota
full segment -> DOWNLOADED and download_data released
Ephemeral -> removed by last holder
detach makes state immutable
holder destructor cleans all segments
other-thread holder destruction does not clear active downloader
```

### path / opened handles

```text
regular final rename <offset> -> <offset>_<size>
ephemeral keeps <offset>_temporary
rename success invalidates old opened handle
rename failure keeps legacy path and still completes
segment removal invalidates current path handles
```

## Review 状态

`FileSegment.h` 和 `FileSegment.cpp` 已按文件 review。迁移保持 CH state machine、
downloader lease、write/reserve、completion 和 holder RAII；差异仅限已明确的 Velox
基础设施替换，包括 physical-thread caller scope、I/O wrappers、cancellation token、
manager-owned opened-file cache，以及 short-write exception reconciliation。
