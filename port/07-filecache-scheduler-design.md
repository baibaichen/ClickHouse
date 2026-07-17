# 07. `FileCacheScheduler` 设计

## 结论

`BackgroundSchedulePool` / `BackgroundSchedulePoolTaskHolder` 用
`FileCacheScheduler` / `FileCacheScheduledTaskHolder` compatibility wrapper 替代。

底层组合：

```text
folly::Timekeeper::after
  -> cancelable delayed Future

FileCacheWorkerPool
  -> parallel callback execution
```

保留 CH 命名入口：

```cpp
using BackgroundSchedulePool = FileCacheScheduler;
using BackgroundSchedulePoolTaskHolder = FileCacheScheduledTaskHolder;
```

不直接使用 `folly::FunctionScheduler`。它只有一个线程并在 timer thread 内执行 callback；
`FileCache::freeSpaceRatioKeepingThreadFunc` 可能长时间运行并等待 remover workers，会阻塞
其他 cache 的全部 scheduled tasks。

也不直接使用 `folly::TimekeeperScheduledExecutor`。它能把 callback dispatch 到 parent
executor，但 `scheduleAt` 返回 `void`，不提供本路径需要的 per-task cancel handle。

## ClickHouse contract

CH `BackgroundSchedulePool` 同时提供：

```text
timer scheduling
parallel worker execution
```

`FileCache` 需要的 task contract：

```text
createTask
schedule
scheduleAfter
deactivate
same task never executes concurrently with itself
schedule while running coalesces one next execution
callback may schedule itself again
```

不迁移 CH schedule pool 的其他通用能力：

```text
StorageID task grouping
system.background_schedule_pool introspection
max_parallel_tasks_per_type
pausable-task API
query/thread logging context
```

这些能力没有 `FileCache` caller。

## `FileCache` 使用点

`FileCache` 有两个 scheduled tasks：

```text
background_cleanup_task
keep_up_free_space_ratio_task
```

### `background_cleanup_task`

创建：

```text
FileCache::initializeImpl
  -> createTask("<cache-name>:background-cleanup", backgroundCleanupTaskFunc)
  -> main_priority invalidate notifier calls schedule
  -> initial scheduleAfter(backgroundCleanupIntervalMs)
```

运行：

```text
remove invalidated entries
run idle-client hook when supported

full cleanup batch:
  schedule immediately
otherwise:
  scheduleAfter(next interval)
```

### `keep_up_free_space_ratio_task`

仅在 free-space ratios 启用时创建：

```text
FileCache::initializeImpl
  -> createTask("<cache-name>:free-space", freeSpaceRatioKeepingThreadFunc)
  -> schedule
```

运行：

```text
collect free-space eviction batches
dispatch remover tasks
wait/finalize batches
scheduleAfter(next delay)
```

该 callback 可能运行较久，必须在 shared dynamic worker pool 执行，不能占用 timer thread。

### metadata workers

以下不是 scheduled tasks：

```text
async metadata main worker
metadata listing/loading workers
background download workers
metadata cleanup worker
```

它们继续通过 `FileCacheWorkerPool` / `FileCacheWorker` 运行，详见 `09`。

## aliases 和接口

```cpp
class FileCacheScheduledTask;

class FileCacheScheduledTaskHolder
{
public:
    FileCacheScheduledTaskHolder() = default;
    explicit FileCacheScheduledTaskHolder(
        std::shared_ptr<FileCacheScheduledTask> task);

    FileCacheScheduledTaskHolder(const FileCacheScheduledTaskHolder &) = delete;
    FileCacheScheduledTaskHolder & operator=(
        const FileCacheScheduledTaskHolder &) = delete;

    FileCacheScheduledTaskHolder(FileCacheScheduledTaskHolder &&) noexcept;
    FileCacheScheduledTaskHolder & operator=(
        FileCacheScheduledTaskHolder &&) noexcept;

    ~FileCacheScheduledTaskHolder();

    explicit operator bool() const;
    FileCacheScheduledTask * operator->();
    const FileCacheScheduledTask * operator->() const;

private:
    std::shared_ptr<FileCacheScheduledTask> task_;
};

class FileCacheScheduler
{
public:
    FileCacheScheduler(
        std::shared_ptr<folly::Timekeeper> timekeeper,
        FileCacheWorkerPool & worker_pool);

    FileCacheScheduledTaskHolder createTask(
        std::string name,
        std::function<void()> callback);

    void shutdown();

private:
    std::shared_ptr<folly::Timekeeper> timekeeper_;
    FileCacheWorkerPool & worker_pool_;
    std::mutex mutex_;
    bool shutdown_ = false;
    std::vector<std::weak_ptr<FileCacheScheduledTask>> tasks_;
};

using BackgroundSchedulePool = FileCacheScheduler;
using BackgroundSchedulePoolTaskHolder = FileCacheScheduledTaskHolder;
```

`FileCacheScheduledTask` public API：

```cpp
class FileCacheScheduledTask
{
public:
    bool schedule();
    bool scheduleAfter(uint64_t delay_ms);
    bool deactivate();
};
```

`FileCache.h/.cpp` 可以继续保留：

```cpp
BackgroundSchedulePoolTaskHolder background_cleanup_task;
BackgroundSchedulePoolTaskHolder keep_up_free_space_ratio_task;
```

`createTask` 在 scheduler shutdown 后失败。scheduler registry 保存 task weak pointers；
`shutdown` snapshot 所有 live tasks、逐个 deactivate，并等待完成。

## task state machine

每个 task 独立维护：

```text
Idle
Delayed
Queued
Running
Deactivated
```

内部 state 至少包含：

```text
mutex
condition_variable
active flag
generation
current state
optional pending next-run request
cancelable timer Future
callback
```

### `schedule`

```text
Deactivated:
  return false

Idle / Delayed:
  cancel/invalidate old delayed timer
  state = Queued
  dispatch one callback to worker pool

Queued:
  coalesce; return false

Running:
  remember one immediate next-run request
  return true
```

立即 request 优先于已有 delayed next-run request。

### `scheduleAfter`

```text
Deactivated:
  return false

Idle / Delayed:
  cancel/invalidate prior timer
  timer = timekeeper.after(delay)
  timer continuation runs via worker pool
  state = Delayed

Queued:
  keep already queued immediate run

Running:
  remember/overwrite one delayed next-run request
```

使用 one-shot self-reschedule，不创建 fixed periodic timer：

```text
callback runs once
callback decides schedule or scheduleAfter
```

这保留：

```text
background cleanup chooses immediate vs delayed based on batch fullness
free-space keeper chooses normal vs retry delay based on outcome
```

### timer cancellation

`folly::Timekeeper::after` 返回 cancelable `SemiFuture`。task 保存对应 future chain：

```text
after(delay)
  -> via(FileCacheWorkerPool)
  -> generation/active check
  -> run callback
```

取消 delayed task：

```text
increment generation
cancel timer Future
wait for cancellation completion when required by deactivate
```

不能只用 generation no-op 而保留 pending timer；否则 manager shutdown 可能被 timer chain
持有的 executor keep-alive 延迟。

### callback execution

worker closure 在运行 callback 前，在 task mutex 下检查：

```text
active
generation matches
state allows execution
not already Running
```

然后设置 `Running`，释放 mutex，再调用 callback。

callback invocation 必须 catch all exceptions，执行 task-state cleanup 后记录/上报；异常不能
逃出 worker closure，使 task 永久停在 `Running`。这与 CH
`BackgroundSchedulePoolTaskInfo::execute` 一致。

callback 返回后：

```text
if Deactivated:
  signal waiter
else if pending immediate request:
  queue one run
else if pending delayed request:
  create one timer
else:
  state = Idle
```

同一个 task 永远不会并发进入 callback；不同 cache/task 可以在 worker pool 中并行。

### `deactivate`

```text
lock task state
active = false
state = Deactivated
increment generation
cancel pending timer
invalidate queued-but-not-started closure
wait until running callback returns
clear callback
return
```

`deactivate` 返回后，不得再访问 callback 捕获的 `FileCache`。

queued closure 可能稍后在 worker pool 被取出，但 generation/active check 只能走 no-op，
且不能保留 `FileCache` capture。

## task holder

`FileCacheScheduledTaskHolder` 保持 CH RAII：

```text
default constructible
move-only
operator bool
operator->
destructor deactivates task
```

显式 `deactivateBackgroundOperations` 仍应先 deactivate；holder destructor 只是兜底。

## manager interaction

`FileCacheManager` 持有：

```cpp
FileCacheWorkerPool worker_pool_;
FileCacheScheduler scheduler_;
```

声明顺序必须是 worker pool 在 scheduler 前，使逆序析构先销毁 scheduler。

创建：

```text
worker_pool(maxThreads, minThreads=1)
scheduler(timekeeper, worker_pool)
```

shutdown：

```text
for each FileCache:
  deactivateBackgroundOperations

scheduler.shutdown
worker_pool.shutdown
openedFileCache.clear
```

所有 cache tasks deactivate 后，scheduler 才释放 timer resources；worker pool 最后停止。

## worker pool budget

scheduled callback 在 shared physical pool 执行，所以每个 cache 的 conservative max
必须包括：

```text
+ 1 background-cleanup callback
+ (freeSpaceKeepingEnabled
     ? 1 free-space collector callback + keepFreeSpaceEvictionThreads
     : 0)
```

完整公式见 `09`。

## tests

必须覆盖：

```text
schedule dispatches exactly one immediate callback
scheduleAfter runs after delay
schedule advances an existing delayed task
scheduleAfter overwrites an existing delayed timer
multiple schedule calls while Queued coalesce
schedule while Running produces exactly one next run
callback can self-scheduleAfter
callback exception does not strand task in Running state
same task never executes concurrently
different tasks can execute concurrently
timer cancellation reclaims pending timer
deactivate prevents queued callback from touching captured state
deactivate waits for running callback
holder destructor deactivates
shutdown cancels all timers and waits all callbacks
two caches use unique task names without collision
```

tests 应注入 controllable/manual `Timekeeper`，不使用 sleep。

## Review 状态

`BackgroundSchedulePool` 的 FileCache-required subset 映射为：

```text
timer: folly::Timekeeper cancelable futures
execution: shared dynamic FileCacheWorkerPool
compat names:
  BackgroundSchedulePool
  BackgroundSchedulePoolTaskHolder
```

不迁移完整 CH `BackgroundSchedulePool` 的 1000+ 行通用实现，也不使用单线程
`folly::FunctionScheduler` 直接执行 callback。
