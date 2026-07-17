# 04. `ThreadFromGlobalPool` / `ThreadPool` 迁移设计

## 结论

Velox 侧没有与 ClickHouse `GlobalThreadPool` 等价的业务全局线程池。实际类名使用
Velox / FileCache 风格；为了减少迁移代码里的概念切换，再用 `using` 提供
ClickHouse 名字：

| ClickHouse | Velox port |
|---|---|
| `GlobalThreadPool` | manager-owned `FileCacheWorkerPool` |
| `ThreadFromGlobalPoolImpl` / `ThreadFromGlobalPool` | `FileCacheWorker` + `using ThreadFromGlobalPool = FileCacheWorker` |
| `ThreadPoolImpl<ThreadFromGlobalPool>` / `ThreadPool` | per-cache `FileCacheThreadPool` over `FileCacheWorkerPool` + `using ThreadPool = FileCacheThreadPool` |

也就是说，Velox port 的真实类型是：

```cpp
class FileCacheWorker;
class FileCacheWorkerPool;
class FileCacheThreadPool;

using ThreadFromGlobalPool = FileCacheWorker;
using ThreadPool = FileCacheThreadPool;
```

迁移 `FileCache` 代码时可以继续写 `ThreadFromGlobalPool` 和 `ThreadPool`，但它们只是
`velox/ch/Common/ThreadPool*` 中的别名，不是复用 ClickHouse 原始实现。

核心简化：

```text
ClickHouse:
  GlobalThreadPool
    -> ThreadFromGlobalPoolImpl
      -> ThreadPoolImpl<ThreadFromGlobalPool>

Velox port:
  FileCacheWorkerPool
    -> FileCacheWorker
    -> per-cache FileCacheThreadPool tasks
  using ThreadPool = FileCacheThreadPool
  using ThreadFromGlobalPool = FileCacheWorker
```

`FileCacheWorkerPool` 内部持有唯一的 `folly::CPUThreadPoolExecutor`，对应 CH
`GlobalThreadPool`。`FileCacheManager` 持有它并在 `shutdown` 中统一停止。

manager 可以管理多个 `FileCache`，但只持有一个 shared worker pool。pool capacity 必须
由所有 cache 的真实并发任务推导，不能使用任意默认值。

## ClickHouse 当前含义

### `GlobalThreadPool`

`GlobalThreadPool` 是 ClickHouse 的全局 OS worker 复用池。它解决的是进程级 thread
复用问题：

```text
避免每个子系统频繁创建/销毁 OS thread
复用 thread-local allocator cache
降低 ASan / TSan / gdb 下的线程创建成本
接入 ClickHouse metrics / profiler / tracing / shutdown 诊断
```

### `ThreadFromGlobalPool`

`ThreadFromGlobalPoolImpl` 看起来像 `std::thread`：

```text
constructor(func)
join
detach
joinable
get_id
```

但底层不是直接新建 OS thread，而是把 `func` 投递到 `GlobalThreadPool` 执行。

### `ThreadPool`

ClickHouse `ThreadPool` 是：

```cpp
using ThreadPool = ThreadPoolImpl<ThreadFromGlobalPoolNoTracingContextPropagation>;
```

它是本地 task queue / local pool，负责：

```text
本地 max_threads / queue_size
schedule / trySchedule
wait
异常收集并在 wait 中重抛
priority
```

它自己的 worker 是 `ThreadFromGlobalPool`，所以实际 OS thread 仍来自
`GlobalThreadPool`。

这就是 ClickHouse 的两层分工：

```text
GlobalThreadPool 管进程级 OS thread 复用
ThreadPool 管业务局部队列、限流和 wait
```

## Velox 两层映射

Velox 没有一个等价 `GlobalThreadPool` 的业务全局 singleton 可以直接复用。Velox 常见模式是：

```text
QueryCtx::executor      // query/driver 生命周期，由 query owner 保证
connector ioExecutor    // connector/DWIO IO 生命周期，由外部注入
component-owned executor // 组件自己持有 folly executor
```

`FileCache` 的 background download、cleanup、metadata load、eviction 都是 cache/manager
级后台工作，不属于某个 query。因此不能复用 `QueryCtx::executor`。

Velox port 保留 CH 两层职责，不把 local `ThreadPool` 和 physical worker executor
合并：

```text
FileCacheManager
  -> FileCacheWorkerPool
       -> folly::CPUThreadPoolExecutor

FileCache instance
  -> FileCacheThreadPool
       -> submits bounded local tasks to FileCacheWorkerPool
```

`FileCacheThreadPool` 自己不创建 OS threads。它只保留 CH local pool 的 task queue、
per-cache max concurrency、exception collection 和 `wait`。

## `FileCache` 中的使用点

| 使用点 | ClickHouse 字段 | Velox port 字段 |
|---|---|---|
| async metadata initialization | `FileCache::load_metadata_main_thread` | `std::unique_ptr<ThreadFromGlobalPool>` |
| metadata listing/loading | `listing_threads` / `loading_threads` | `std::vector<ThreadFromGlobalPool>` 或 `ThreadPool::schedule` |
| background download | `CacheMetadata::download_threads` | `std::vector<std::shared_ptr<DownloadThread>>`，其中 `using DownloadThread = ThreadFromGlobalPool` |
| metadata cleanup | `CacheMetadata::cleanup_thread` | `std::unique_ptr<ThreadFromGlobalPool>` |
| free-space eviction | `FileCache::eviction_pool` | `std::unique_ptr<ThreadPool>` |

`BackgroundSchedulePoolTaskHolder` 已由 `FileCacheScheduler` 设计覆盖，不属于本文。

## shared dynamic pool容量 contract

`FileCacheWorkerPool` 只实现 dynamic executor和 resize contract；按 cache配置计算
`workerPoolMax` 是 Manager职责，唯一公式见
[`FileCacheManager` worker max计算](../3-consumers/02-filecache-manager-design.md#worker-max计算)。

Folly dynamic mode：

```text
pending tasks -> create workers on demand, up to max
idle timeout (default 60s) -> retire idle workers, down to min
```

因此 conservative max不会在启动时创建全部 threads。steady state只有
background/cleanup long-running workers保持 active；同 path/config的 name aliases
共享一个 cache，不重复计 budget。

每个 cache 的 free-space `eviction_pool` 是独立的 **logical**
`FileCacheThreadPool`，大小为 `keepFreeSpaceEvictionThreads`；它的 physical workers
仍来自 shared `FileCacheWorkerPool`，所以 Manager预算必须计入这些 slots。

新增 cache 或动态增加 background download threads：

```text
recompute manager worker pool max
FileCacheWorkerPool::setNumThreads(newMax)
create/start new workers
publish applied settings
```

增长必须发生在新 worker/task 提交前；shutdown/remove 后才能降低 max。

## Velox port 接口

### `FileCacheWorker`

`FileCacheWorker` 对齐 `ThreadFromGlobalPool` 的语义：

```text
由 FileCache 专属 ThreadPool 承载的可 join long-running task handle
```

接口：

```cpp
class FileCacheWorker
{
public:
    using StopToken = std::atomic_bool;
    using Function = std::function<void(const StopToken &)>;

    FileCacheWorker(FileCacheThreadPool & pool, Function function);

    FileCacheWorker(FileCacheWorker &&) noexcept;
    FileCacheWorker & operator=(FileCacheWorker &&) noexcept;

    FileCacheWorker(const FileCacheWorker &) = delete;
    FileCacheWorker & operator=(const FileCacheWorker &) = delete;

    ~FileCacheWorker();

    void requestStop();
    void join();
    bool joinable() const;

private:
    std::shared_ptr<State> state_;
};

using ThreadFromGlobalPool = FileCacheWorker;
```

内部 state：

```cpp
struct State
{
    std::atomic_bool stop{false};
    std::atomic_bool joined{false};
    folly::Promise<folly::Unit> finished;
    folly::SemiFuture<folly::Unit> future;
};
```

`FileCacheWorker` 构造时把 worker 投递到 `FileCacheThreadPool` 的 executor：

```text
pool.executor.add(lambda)
lambda:
  run function(state.stop)
  set finished
```

`join`：

```text
wait state.future
propagate exception
mark joined
```

`requestStop`：

```text
state.stop = true
```

析构规则对齐 ClickHouse：如果仍然 `joinable`，说明 owner 没有显式 shutdown/join。
第一版可以 `VELOX_CHECK` 失败，避免静默泄漏后台 worker。

### `FileCacheWorkerPool`

`FileCacheWorkerPool` 对齐 CH `GlobalThreadPool` 的 physical worker 层：

```cpp
class FileCacheWorkerPool
{
public:
    FileCacheWorkerPool(
        size_t maxThreads,
        size_t minThreads,
        std::string threadNamePrefix);

    FileCacheWorker startThread(FileCacheWorker::Function function);
    folly::SemiFuture<folly::Unit> schedule(std::function<void()> task);

    void shutdown();
    void setNumThreads(size_t threads);

private:
    folly::CPUThreadPoolExecutor executor_;
};
```

`FileCacheWorker` 构造时把 long-running function 投递到 `FileCacheWorkerPool`。

### `FileCacheThreadPool`

`FileCacheThreadPool` 对齐 CH per-instance local `ThreadPool`：

```cpp
class FileCacheThreadPool
{
public:
    FileCacheThreadPool(
        FileCacheWorkerPool & workerPool,
        size_t maxThreads,
        size_t queueSize);

    void scheduleOrThrowOnError(std::function<void()> task);
    void wait();

private:
    FileCacheWorkerPool & workerPool_;
    size_t maxThreads_;
    size_t queueSize_;
    // local task/future state; no owned CPUThreadPoolExecutor
};

using ThreadPool = FileCacheThreadPool;
```

`FileCacheWorkerPool::startThread` 用于 CH-style long-running workers：

```text
download worker
cleanup worker
load metadata main task
```

`FileCacheThreadPool::scheduleOrThrowOnError` 用于 per-cache 短任务：

```text
metadata listing/loading subtasks
eviction batches
```

local `wait` 只等本 pool 已提交短任务完成。long-running worker 必须通过对应的
`FileCacheWorker::requestStop` + `join` 退出。

## `download_threads` 替换

ClickHouse 当前字段：

```cpp
struct DownloadThread
{
    std::unique_ptr<ThreadFromGlobalPool> thread;
    bool stop_flag{false};
};

std::vector<std::shared_ptr<DownloadThread>> download_threads;
```

Velox port 中 `stop_flag` 已封装进 `ThreadFromGlobalPool`，所以 `DownloadThread`
不再需要保留 struct，直接用 alias：

```cpp
using DownloadThread = ThreadFromGlobalPool;
std::vector<std::shared_ptr<DownloadThread>> download_threads;
```

差异是：`stop_flag` 不再裸露在 `DownloadThread` 里，而是封装到
`ThreadFromGlobalPool` 内部，通过 `requestStop` 设置，并通过传入 worker loop 的
`StopToken` 读取。`DownloadThread` 只是为了保留 ClickHouse 侧字段语义的别名。

启动：

```cpp
void CacheMetadata::startup()
{
    download_threads.reserve(download_threads_num);
    for (size_t i = 0; i < download_threads_num; ++i)
    {
        download_threads.emplace_back(std::make_shared<DownloadThread>(
            thread_pool,
            [this](const std::atomic_bool & stop)
            {
                downloadThreadFunc(stop);
            }));
    }

    cleanup_thread = std::make_unique<ThreadFromGlobalPool>(
        thread_pool,
        [this](const std::atomic_bool &)
        {
            cleanupThreadFunc();
        });
}
```

缩容：

```cpp
bool CacheMetadata::setBackgroundDownloadThreads(size_t threads_num)
{
    if (threads_num == download_threads.size())
        return false;

    while (download_threads.size() > threads_num)
    {
        auto removed = download_threads.back();
        download_threads.pop_back();

        removed->requestStop();
        download_queue->cv.notify_all();
        removed->join();
    }

    while (download_threads.size() < threads_num)
    {
        download_threads.emplace_back(std::make_shared<DownloadThread>(
            thread_pool,
            [this](const std::atomic_bool & stop)
            {
                downloadThreadFunc(stop);
            }));
    }

    return true;
}
```

shutdown：

```cpp
void CacheMetadata::shutdown()
{
    download_queue->cancel();
    cleanup_queue->cancel();

    for (auto & download_thread : download_threads)
    {
        download_thread->requestStop();
        download_thread->join();
    }

    if (cleanup_thread)
    {
        cleanup_thread->join();
    }
}
```

## `cleanup_thread` 替换

ClickHouse 当前：

```cpp
std::unique_ptr<ThreadFromGlobalPool> cleanup_thread;
```

Velox port 通过 `using ThreadFromGlobalPool = FileCacheWorker` 保持：

```cpp
std::unique_ptr<ThreadFromGlobalPool> cleanup_thread;
```

`cleanupThreadFunc` 不需要改签名。ClickHouse 只有一个 cleanup worker，不支持像
download workers 那样动态缩容，所以不需要 per-worker stop flag。退出仍然依赖：

```text
cleanup_queue.cancel()
cleanup_thread->join()
```

也就是说，`cleanupThreadFunc` 继续只检查 `cleanup_queue->cancelled`。

## `eviction_pool` 替换

ClickHouse 当前：

```cpp
std::unique_ptr<ThreadPool> eviction_pool;
```

Velox port 通过 `using ThreadPool = FileCacheThreadPool` 保持：

```cpp
std::unique_ptr<ThreadPool> eviction_pool;
```

构造时注入 manager shared physical pool：

```cpp
eviction_pool = std::make_unique<ThreadPool>(
    worker_pool,
    keep_up_free_space_eviction_threads,
    keep_up_free_space_eviction_threads);
```

使用方式：

```text
eviction_pool->schedule(task)
eviction_pool->wait()
```

这保持 CH 的 local pool 语义：`keepFreeSpaceRatio` 可以提交多个 eviction batch remover，
然后等待这些短任务完成。

## 为什么不用 `LazyCPUThreadPoolExecutor`

主映射不用 `LazyCPUThreadPoolExecutor`，原因是 `FileCache` 后台能力不是“可能不用”的
可选资源。

配置启用后，这些 worker 会在 startup 或 keep-free-space 逻辑里明确使用：

```text
metadata.startup
  -> background download workers
  -> cleanup worker

loadMetadata
  -> listing/loading subtasks

keepFreeSpaceRatio
  -> eviction tasks
```

lazy 初始化可以作为实现优化，但不能写成主设计要求。

## shutdown 规则

shutdown 要区分 **业务 worker 退出** 和 **线程池销毁**。顺序不能反。

ClickHouse 当前顺序是：

```cpp
download_queue->cancel();
cleanup_queue->cancel();

for (auto & download_thread : download_threads)
    download_thread->thread->join();

cleanup_thread->join();
```

含义是：

```text
1. 先让业务循环有退出条件
2. 再等待 worker 函数真的返回
3. 最后才能销毁线程池和 manager 拥有的资源
```

Velox port 也保持这个三阶段顺序。

### Phase 1: stop producers / cancel queues

```text
FileCache::deactivateBackgroundOperations
  -> shutdown = true
  -> deactivate FileCacheScheduler tasks
  -> download_queue.cancel()
  -> cleanup_queue.cancel()
  -> notify download_queue / cleanup_queue
```

`download_queue.cancel` / `cleanup_queue.cancel` 是业务退出条件。没有这一步，worker
可能仍然阻塞在 `cv.wait`，线程池无法替它改变业务条件。

### Phase 2: join business workers

```text
for download_thread in download_threads:
    download_thread->join()

cleanup_thread->join()

eviction_pool->wait()
```

这一步保证 `downloadThreadFunc` / `cleanupThreadFunc` / eviction tasks 不再访问
`CacheMetadata`、queues、`FileSegment` 或 `FileCache`。

download worker 缩容仍然使用 per-worker stop：

```text
download_thread->requestStop()
download_queue->cv.notify_all()
download_thread->join()
```

这个语义不能用 `executor.stop` 替代，因为 `executor.stop` 是线程池整体操作，无法表达
“只停某几个 download workers，保留其他 workers 继续运行”。

### Phase 3: destroy executors/resources

```text
FileCacheWorkerPool.shutdown()
metadata destroyed
openedFileCache.clear()
```

只有 worker 已经退出后，`FileCacheManager::shutdown` 才能释放 `FileCacheWorkerPool`，从而关闭
底层 `folly::CPUThreadPoolExecutor`。

## 测试要求

需要覆盖：

```text
FileCacheWorker 可以启动 long-running worker
requestStop 后 worker 从 queue wait 中醒来并退出
join 等待 worker 完成并传播异常
析构未 join 的 worker 触发检查失败
setBackgroundDownloadThreads 可以扩容
setBackgroundDownloadThreads 可以缩容且未被缩容 worker 继续运行
shutdown 先 cancel queues，再 join download/cleanup workers
eviction_pool schedule 多个短任务后 wait 全部完成
```

## Review 状态

本文档已完成 review。关键决策：

```text
GlobalThreadPool 不迁移为单独类
ThreadFromGlobalPoolImpl / ThreadFromGlobalPool -> FileCacheWorker
ThreadPoolImpl<ThreadFromGlobalPool> / ThreadPool -> FileCacheThreadPool
using ThreadFromGlobalPool = FileCacheWorker
using ThreadPool = FileCacheThreadPool
FileCacheManager 持有 FileCacheWorkerPool，内部持有 folly::CPUThreadPoolExecutor
each FileCache owns a logical FileCacheThreadPool for free-space eviction
logical FileCacheThreadPool submits to shared FileCacheWorkerPool
shared pool max = sum(conservative per-cache worker budgets)
shared pool min = 1; idle workers retire by Folly timeout
free-space eviction slots are included in each cache worker budget
```

这样真实类名使用 Velox / FileCache 风格，迁移代码仍可通过 `using` 保留 ClickHouse
命名入口，同时去掉 Velox 里不存在的进程级 `GlobalThreadPool` 层。
