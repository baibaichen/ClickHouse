# 07. `FileCacheScheduler` 设计

## 结论

`BackgroundSchedulePool` / `BackgroundSchedulePoolTaskHolder` 用
`FileCacheScheduler` 包一层 Velox/Folly 调度设施替代。底层可用
`folly::FunctionScheduler`，但 `FileCache` 算法代码不直接依赖 folly。

已确认 `folly::FunctionScheduler` 提供：

```text
addFunction
addFunctionOnce
resetFunctionTimer
cancelFunction
cancelFunctionAndWait
shutdown
start
```

可以覆盖 ClickHouse `FileCache` 需要的：

```text
定时执行
立即/提前触发
取消任务
shutdown 时停止并等待
```

## ClickHouse 里的使用点

`FileCache` 里主要有两个 schedule task：

```text
background_cleanup_task
keep_up_free_space_ratio_task
```

### `background_cleanup_task`

创建位置：

```text
FileCache::initializeImpl
  -> createTask("FileCacheBackgroundCleanup", backgroundCleanupTaskFunc)
  -> main_priority->setInvalidateNotifier(... schedule())
  -> scheduleAfter(backgroundCleanupIntervalMs())
```

运行逻辑：

```text
backgroundCleanupTaskFunc
  -> main_priority->removeInvalidatedEntries
  -> evictIdleClients
  -> 如果移满一批 invalidated entries:
         schedule()              // 立即再跑
     否则:
         scheduleAfter(interval)  // 延迟再跑
```

shutdown：

```text
deactivateBackgroundOperations
  -> background_cleanup_task->deactivate()
```

### `keep_up_free_space_ratio_task`

创建位置：

```text
FileCache::initializeImpl
  -> if keep_free_space ratios enabled:
         createTask(... freeSpaceRatioKeepingThreadFunc)
         schedule()
```

运行逻辑：

```text
freeSpaceRatioKeepingThreadFunc
  -> freeSpaceRatioImpl(reschedule_ms)
  -> scheduleAfter(reschedule_ms)
```

shutdown：

```text
deactivateBackgroundOperations
  -> keep_up_free_space_ratio_task->deactivate()
  -> eviction_pool->wait()
```

### metadata 线程

除了 schedule task，还有普通后台线程：

```text
load_metadata_main_thread = ThreadFromGlobalPool(...)
Metadata::download_threads
Metadata::cleanup_thread
```

这些不走 `FileCacheScheduler` 的 periodic task 接口。它们应改成 executor-backed
long-running task 或自有 thread wrapper，仍由 `FileCacheManager::shutdown` /
`FileCache::deactivateBackgroundOperations` 统一停止。

## 接口设计

```cpp
class FileCacheScheduledTask
{
public:
    bool schedule();
    bool scheduleAfter(uint64_t delayMs);
    void deactivate();
};

class FileCacheScheduler
{
public:
    FileCacheScheduler();
    ~FileCacheScheduler();

    std::shared_ptr<FileCacheScheduledTask> createTask(
        std::string name,
        std::function<void()> callback);

    void start();
    void shutdown();

private:
    folly::FunctionScheduler scheduler_;
};
```

`FileCacheScheduledTask` 内部保存：

```text
task name
callback
FileCacheScheduler*
active flag
```

## `schedule` 和 `scheduleAfter` 映射

`folly::FunctionScheduler` 的函数是按 name 管理的，所以每个 task 需要一个唯一 name。

### `scheduleAfter(delayMs)`

一个直接映射方案是注册 periodic `addFunction` task，再用 `resetFunctionTimer`
提前触发：

```text
if task 已注册:
    resetFunctionTimer(name)
else:
    addFunction(callback, interval, name, startDelay)
```

但是 `FunctionScheduler::resetFunctionTimer` 会按初次注册时的 `startDelay` 重置 timer，
不能为每次调用设置不同 delay。因此建议 wrapper 使用 **one-shot reschedule 模式**：

```text
scheduleAfter(delay):
    cancelFunction(name)
    addFunctionOnce(callback, name, delay)
```

callback 运行完后由 `FileCache` 自己决定下一次 `schedule` / `scheduleAfter`。
这和 CH 当前逻辑一致：task 函数末尾自己重新调度。

### `schedule`

立即触发：

```text
schedule():
    scheduleAfter(0)
```

或使用极小 delay 的 `addFunctionOnce`。不要让 periodic task 自动循环；循环由
`FileCache` 函数末尾显式决定。

### `deactivate`

```text
deactivate():
    active = false
    cancelFunctionAndWait(name)
```

如果 callback 正在跑，`cancelFunctionAndWait` 等它退出。callback 内部也应检查
`FileCache::shutdown`，避免 shutdown 后重新 schedule。

## 为什么不用 periodic `addFunction`

`FileCache` 的两个任务都不是固定周期自动循环：

- `backgroundCleanupTaskFunc` 根据本轮是否清满 batch，选择立即或延迟。
- `freeSpaceRatioKeepingThreadFunc` 根据执行结果修改 `reschedule_ms`。

所以 one-shot 模式更贴近 CH：

```text
run once
task function decides next schedule time
```

## 与 `FileCache` 的交互

`FileCache` 持有：

```cpp
std::shared_ptr<FileCacheScheduledTask> backgroundCleanupTask_;
std::shared_ptr<FileCacheScheduledTask> keepUpFreeSpaceRatioTask_;
```

初始化：

```text
backgroundCleanupTask_ = scheduler.createTask("FileCacheBackgroundCleanup", ...)
mainPriority->setInvalidateNotifier(threshold, [&] { backgroundCleanupTask_->schedule(); })
backgroundCleanupTask_->scheduleAfter(backgroundCleanupIntervalMs())

keepUpFreeSpaceRatioTask_ = scheduler.createTask("FileCacheFreeSpaceRatio", ...)
keepUpFreeSpaceRatioTask_->schedule()
```

任务内部：

```text
if shutdown:
    return

... do work ...

if shutdown:
    return

task->scheduleAfter(nextDelay)
```

shutdown：

```text
shutdown = true
backgroundCleanupTask_->deactivate()
keepUpFreeSpaceRatioTask_->deactivate()
evictionPool->wait()
metadata.shutdown()
```

## 与 `FileCacheManager` 的交互

`FileCacheManager` 持有一个 scheduler：

```cpp
class FileCacheManager
{
    FileCacheScheduler scheduler_;
};
```

创建 `FileCache` 时注入：

```cpp
FileCache(config, scheduler_)
```

或者给 `FileCache` 一个 `FileCacheScheduler*`。

`FileCacheManager::shutdown`：

```text
for each cache:
    cache->deactivateBackgroundOperations()
scheduler.shutdown()
openedFileCache.clear()
```

## 测试

必须覆盖：

```text
schedule 立即触发一次
scheduleAfter 延迟触发一次
任务函数可以再次 scheduleAfter 自调度
schedule 可提前触发已延迟任务
deactivate 后不会再执行
deactivate 等待 in-flight callback 结束
shutdown 后 schedule/scheduleAfter 返回 false
```

## Review 状态

`BackgroundSchedulePool` -> `FileCacheScheduler(folly::FunctionScheduler)` 已确认可行。
后续实现时需要重点检查 `addFunctionOnce` + `cancelFunctionAndWait` 的线程安全和 callback
内自调度场景。
