# Task 013: `FileCacheFactory` and `FileCacheManager`

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes one
> result file under this ClickHouse checkout. Do not modify ClickHouse source
> files. Do not commit or stage either repository.

## Pre-execution source-contract amendment

The comment-only `FileCacheFactoryManagerTest.cpp` skeleton later in this file is
invalid and must not be copied. Every listed test must execute production code and
contain assertions. The fixture must return a real initialized Manager/Factory, not
null or a fake registry.

Required executable coverage:

```text
Factory:
  create/get/missing behavior
  same-name equal/different settings
  same-path alias dedup and different-settings rejection
  existing-name create rejection
  alias enumeration and remove-all-aliases
  name-rebind conflict preserves the original binding

Manager:
  worker budget grows once per unique cache
  initialize and shutdown run once per unique cache
  explicit global install/get/uninstall and live replacement rejection
  equal reload is a no-op
  reload deduplicates aliases and rejects path changes
  applySettingsIfPossible runs outside the registry lock
  clear deactivates outside the registry lock and leaves Manager reusable
  shutdown is idempotent and later operations fail
  resource shutdown order is cache workers -> timers -> physical pool -> handles
```

Lock-order tests must use barriers, callbacks, or try-lock instrumentation; do not use
sleep. Each regression must have a behavioral RED that reaches the relevant
production path.

Reuse `checkedAdd` from `FileCacheUtils.h`; do not define a private Manager-only
overflow helper.

Controller acceptance requires reading every test body and mapping each promised
contract to at least one assertion. A green executable containing empty/comment-only
tests is an automatic `changes_requested`.

## Goal

Implement the real `FileCacheFactory` (sole registry/name/path aliasing) and
`FileCacheManager` (runtime resource owner, worker budget, singletons).

The deliverable is a compiled and tested `velox_ch_filecache_manager` library
and a `velox_ch_filecache_manager_test` executable.

## Starting Point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected predecessors:
  Task 012: FileCache center SCC — FileSegmentInfo, FileSegment,
            Metadata, FileCache, QueryLimit, all .cpp files linked
```

Do not require a clean worktree. Stop if the branch is not `filecache`.

## Design References

Read before editing:

```text
port/task/ENVIRONMENT.md
port/2-file-cache/12-filecache-factory-files-design.md
port/3-consumers/02-filecache-manager-design.md
port/2-file-cache/06-filecache-settings-files-design.md
port/1-dependencies/04-filecache-thread-pool-design.md
port/1-dependencies/05-filecache-scheduler-design.md
```

Use ClickHouse source only as behavioral reference:

```text
src/Interpreters/FileCache/FileCacheFactory.h
src/Interpreters/FileCache/FileCacheFactory.cpp
```

## File Scope

Modify:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheFactory.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheFactory.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheManager.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheManager.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp
<clickhouse_repo>/port/task/result/013-filecache-factory-manager-result.md
```

Every new Velox C++ and CMake file must begin with the Apache 2.0 license
header. Use `/* ... */` for C++ and `#` for CMake, matching the repository style.

## Architecture: One Factory, One Manager, No Duplicate Registry

The dependency direction is strict and must not be reversed:

```text
FileCacheManager
  -> owns runtime resources (scheduler, worker pool, opened-file cache)
  -> owns one FileCacheFactory

FileCacheFactory
  -> owns the sole name/path registry
  -> constructs FileCache with Manager-owned runtime references

FileCache
  -> never calls or stores FileCacheManager / FileCacheFactory
```

`FileCacheFactory` accepts Manager-owned service references in its constructor
and uses them when constructing `FileCache` objects. It does not own those
resources and does not store a `FileCacheManager*`. `FileCacheManager` does not
implement a second name/path map.

`FileCacheManager` exposes `FileCacheFactory &` for callers that need registry
operations. The registry API itself lives only on `FileCacheFactory`.

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected: branch `filecache`, `velox_ch_filecache_core` target exists and
links. Record pre-existing dirty files in the result file.

- [ ] **Step 2: Add the failing test file (red)**

Append to `velox/ch/Interpreters/FileCache/tests/CMakeLists.txt`:

```cmake
add_executable(
  velox_ch_filecache_manager_test
  FileCacheFactoryManagerTest.cpp
)
add_test(velox_ch_filecache_manager_test velox_ch_filecache_manager_test)

target_link_libraries(
  velox_ch_filecache_manager_test
  PRIVATE
    velox_ch_filecache_manager
    velox_ch_filecache_core
    velox_ch_filecache
    velox_test_util
    velox_exception
    velox_file
    velox_memory
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

Create `velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp`:

Implement the complete executable test matrix from the pre-execution amendment.
Do not create test declarations until their fixture and assertions are complete.
The RED revision must compile and fail through production behavior; after recording
that evidence, implement Factory/Manager until the same tests pass.

- [ ] **Step 3: Verify the red build**

Reconfigure (same command as Task 012 Step 8, updating the log name):

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`. For `home-chang`, also add
`-DVELOX_BUILD_TESTING=ON` (already present in the `root-oss` effective
configuration). Redirect output to `<velox_build_dir>/configure_task_013_factory_mgr.log`.

Then:

```bash
if <ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_manager_test \
  > <velox_build_dir>/build_task_013_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected: configure succeeds, build fails because
`velox_ch_filecache_manager` library and its headers do not exist.

- [ ] **Step 4: Implement `FileCacheFactory.h`**

`FileCacheFactory.h` must declare all registry API, the singleton entry
point, and the nested `FileCacheData` class. The Factory constructor takes
Manager-owned service references:

```cpp
class FileCacheFactory final
{
public:
    class FileCacheData
    {
    public:
        FileCacheData(
            FileCachePtr cache_,
            const FileCacheConfig & config_,
            std::string config_path_);

        FileCacheConfig getSettings() const;
        void setSettings(FileCacheConfig config);

        const FileCachePtr cache;
        const std::string config_path;

    private:
        mutable std::mutex settings_mutex_;
        FileCacheConfig settings_;
    };
    using FileCacheDataPtr = std::shared_ptr<FileCacheData>;

    // Manager-owned service references; Factory does not own them.
    struct RuntimeServices
    {
        FileCacheWorkerPool & workerPool;
        FileCacheScheduler & scheduler;
        OpenedFileCache & openedFileCache;
        filesystems::FileSystem & localFileSystem;
        memory::MemoryPool & memoryPool;
        std::string commonUserId;
    };

    explicit FileCacheFactory(RuntimeServices services);

    FileCacheFactory(const FileCacheFactory &) = delete;
    FileCacheFactory & operator=(const FileCacheFactory &) = delete;

    // Singleton: backed by Manager-installed atomic pointer.
    static FileCacheFactory & instance();

    // Registry API.
    FileCachePtr getOrCreate(
        const std::string & name,
        const FileCacheConfig & settings,
        const std::string & config_path);

    FileCachePtr create(
        const std::string & name,
        const FileCacheConfig & settings,
        const std::string & config_path);

    FileCachePtr get(const std::string & name) const;

    using CacheByName = folly::F14FastMap<std::string, FileCacheDataPtr>;
    using Caches = folly::F14FastSet<FileCacheDataPtr>;

    CacheByName getAll() const;
    Caches getUniqueInstances() const;
    FileCacheDataPtr getByName(const std::string & name) const;

    void remove(const FileCachePtr & cache);
    void clear();

    // Called by FileCacheManager only.
    static void setInstance(FileCacheFactory * factory);

private:
    RuntimeServices services_;
    mutable std::mutex registry_mutex_;
    CacheByName cache_by_name_;
    std::string default_cache_name_;
    bool shutdown_ = false;

    FileCacheDataPtr findByPath(const std::string & normalized_path) const;
    size_t growWorkerBudget(const FileCacheConfig & config);
    void rollbackWorkerBudget(size_t budget_delta);

    static std::atomic<FileCacheFactory *> global_instance_;
};
```

Invariants:

```text
cache and canonical config_path in FileCacheData are immutable after construction.
settings_ in FileCacheData is protected by settings_mutex_, separate from registry_mutex_.
Lock order: registry_mutex_ -> settings_mutex_ (never reversed).
global_instance_ uses acquire/release ordering.
```

- [ ] **Step 5: Implement `FileCacheFactory.cpp`**

Implement all registry operations with the following behavioral contracts:

**`getOrCreate`**

```text
Under registry_mutex_:
  1. If name exists:
     a. If effective settings == requested settings: return existing cache.
     b. Otherwise: VELOX_FAIL (name rebind conflict).
  2. Find existing unique cache by normalized path.
  3. If path exists:
     a. If settings equal: add name alias, return shared cache.
     b. Otherwise: VELOX_FAIL (path/settings conflict).
  4. Otherwise:
     a. Grow worker-pool budget.
     b. Construct FileCacheData / FileCache.
     c. Insert name.
     d. On failure: rollback budget, erase partial entries, VELOX_RETHROW.
```

**`create`** — same as `getOrCreate` except an existing name always fails
(step 1b always rejects, not only when settings differ).

**`remove`**

```text
Under registry_mutex_:
  Erase all name entries pointing to this cache.
  Retain FileCacheDataPtr snapshot.

Outside lock:
  deactivateBackgroundOperations on the cache if it has no more registry entries.
  Release snapshot.
  Then lower worker-pool max.
```

Must not call cache shutdown under registry lock.

**`clear`**

```text
Under registry_mutex_:
  Snapshot unique FileCacheData.
  Clear cache_by_name_.

Outside lock:
  deactivateBackgroundOperations on each unique cache.
  Release snapshots.
  Lower worker-pool max to the minimum (1).
```

**Settings equality** must not include cache name or config source path. All
other `FileCacheConfig` effective fields participate in equality.

**`getUniqueInstances`**: deduplicate by `FileCacheDataPtr` address
(same `shared_ptr` control block), returning one entry per actual `FileCache`.

- [ ] **Step 6: Implement `FileCacheManager.h`**

```cpp
class FileCacheManager
{
public:
    struct NamedFileCacheConfig
    {
        std::string name;
        FileCacheConfig config;
        std::string configPath;
    };

    struct FileCacheManagerStats
    {
        folly::F14FastMap<std::string, FileCacheStats> cachesByName;
        FileHandleCacheStats openedFileCache;
        size_t uniqueCaches = 0;
        size_t workerPoolMax = 0;
        size_t workerPoolActive = 0;
    };

    struct Options
    {
        std::vector<NamedFileCacheConfig> caches;
        std::string defaultCacheName;
        std::string commonUserId;
        std::string cachePathPrefix;
        std::string allowedCacheRoot;
        std::shared_ptr<filesystems::FileSystem> localFileSystem;
        memory::MemoryPool * memoryPool = nullptr;
        std::shared_ptr<folly::Timekeeper> timekeeper;
        bool initializeOnCreate = true;
    };

    static std::shared_ptr<FileCacheManager> create(Options options);

    static FileCacheManager * getInstance();
    static FileCacheManager & instance();
    static void setInstance(FileCacheManager * manager);

    FileCacheFactory & factory();
    const FileCacheFactory & factory() const;
    FileCachePtr get(const std::string & name) const;
    FileCachePtr getDefault() const;
    const std::string & commonUserId() const { return commonUserId_; }

    void initialize();
    void applyConfigs(const std::vector<NamedFileCacheConfig> & configs);
    void shutdown();

    FileCacheManagerStats refreshStats() const;
    std::string toString(bool details = true) const;

    OpenedFileCache & openedFileCache();
    FileCacheWorkerPool & workerPool();
    FileCacheScheduler & scheduler();

    FileCacheManager(const FileCacheManager &) = delete;
    FileCacheManager & operator=(const FileCacheManager &) = delete;

private:
    explicit FileCacheManager(Options options);

    enum class State
    {
        Created,
        Initialized,
        ShuttingDown,
        Shutdown,
    };

    // Member declaration order determines destruction order (C++ reverse).
    // factory_ must be declared AFTER resources so resources outlive factory.
    std::shared_ptr<filesystems::FileSystem> localFileSystem_;
    memory::MemoryPool * const memoryPool_;
    std::shared_ptr<folly::Timekeeper> timekeeper_;
    const std::string commonUserId_;

    FileCacheWorkerPool workerPool_;
    FileCacheScheduler scheduler_;
    OpenedFileCache openedFileCache_;
    FileCacheFactory factory_;

    mutable std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_cv_;
    State state_ = State::Created;

    static std::atomic<FileCacheManager *> global_instance_;
};
```

Destruction ordering invariant:

```text
factory_        destroyed before openedFileCache_
openedFileCache_ destroyed before scheduler_ and workerPool_
scheduler_      destroyed before workerPool_
```

Because C++ destroys members in reverse declaration order, `factory_` must be
declared last among the resource members.

- [ ] **Step 7: Implement `FileCacheManager.cpp`**

### Two-Phase `create`

```text
validate options (commonUserId non-empty and != "internal",
  paths absolute/normalized, localFileSystem/memoryPool/timekeeper non-null)

compute workerPoolMax:
  for each unique NamedFileCacheConfig (deduplicated by normalized path + settings):
    cacheWorkerMax =
      loadMetadataThreads
      + (loadMetadataAsynchronously ? 1 : 0)
      + backgroundDownloadThreads
      + 1  (metadata cleanup worker)
      + 1  (scheduled background-cleanup callback)
      + (freeSpaceKeepingEnabled
          ? 1 + keepFreeSpaceEvictionThreads
          : 0)
  workerPoolMax = max(1, checkedSum(cacheWorkerMax))

construct:
  workerPool(workerPoolMax, 1, "FileCache")
  scheduler(timekeeper, workerPool)
  openedFileCache(localFileSystem, memoryPool)
  factory(RuntimeServices{...})

register caches via factory_.getOrCreate for each NamedFileCacheConfig

if initializeOnCreate:
  call initialize()

return shared_ptr<FileCacheManager>
```

Construction must not start any background work that captures `this` before the
constructor completes.

### `setInstance`

```text
install:
  1. store Manager pointer (acquire/release)
  2. store &manager.factory() as FileCacheFactory global pointer

uninstall:
  1. clear FileCacheFactory global pointer first
  2. clear FileCacheManager global pointer second
```

Atomic ordering ensures new `FileCacheFactory::instance()` callers see a null
or fully-constructed Factory, never a partially-torn-down one.

```text
replace live different manager -> VELOX_FAIL
set same manager -> allowed (no-op, use_count check)
```

### `initialize`

```text
under lifecycle_mutex_:
  if state == ShuttingDown or Shutdown: VELOX_FAIL
  if state == Initialized: return

snapshot = factory_.getUniqueInstances()

outside lock:
  for each unique cache in snapshot:
    cache->initialize()

under lifecycle_mutex_:
  state = Initialized
  lifecycle_cv_.notify_all()
```

On any `initialize` exception:

```text
deactivate already-initialized caches outside lock
keep state in Created (or transition to Shutdown on fatal error)
propagate exception
```

### `applyConfigs`

```text
under lifecycle_mutex_:
  reject ShuttingDown / Shutdown

Factory:
  validate names/path aliases
  snapshot affected unique FileCacheData

compute desired worker max

for each unique cache outside lock:
  if background threads grow: workerPool_.setNumThreads(newMax) first
  call cache->applySettingsIfPossible(new_config, actual_config)
  store actual_config snapshot even if exception thrown

after decreases / workers joined:
  lower workerPool_ max to actual aggregate budget
```

On partial failure: persist truthful actual snapshots; recompute worker max;
propagate the first exception without pretending rollback succeeded.

### `shutdown`

```text
under lifecycle_mutex_:
  if Shutdown: wait on lifecycle_cv_, return
  if ShuttingDown: wait on lifecycle_cv_, return
  state = ShuttingDown

outside lock:
  factory_.clear() (snapshots and clears registry)
  deactivate all unique caches
  scheduler_.shutdown()
  workerPool_.shutdown()
  openedFileCache_.clear()

under lifecycle_mutex_:
  state = Shutdown
  lifecycle_cv_.notify_all()
```

Concurrent callers block on `lifecycle_cv_` until state reaches `Shutdown`.

Destruction ordering is always:

```text
cache background workers stop
-> scheduler timer chains cancel
-> physical worker pool drains and stops
-> opened-file cache clears
```

### Worker budget formula

Use checked addition; overflow is a configuration error:

```cpp
size_t checkedAdd(size_t a, size_t b)
{
    VELOX_CHECK_LE(b, std::numeric_limits<size_t>::max() - a,
        "Worker budget overflow");
    return a + b;
}
```

- [ ] **Step 8: Update `CMakeLists.txt`**

Add `velox_ch_filecache_manager` to
`velox/ch/Interpreters/FileCache/CMakeLists.txt`:

```cmake
velox_add_library(
  velox_ch_filecache_manager
  FileCacheFactory.cpp
  FileCacheManager.cpp
)

target_link_libraries(
  velox_ch_filecache_manager
  PUBLIC
    velox_ch_filecache_core
    velox_ch_filecache
    velox_file
    velox_memory
    Folly::folly
    fmt::fmt
)
```

The existing `velox_ch_filecache_core` and `velox_ch_filecache_core_scc_test`
targets must remain unchanged.

- [ ] **Step 9: Build**

Reconfigure (same CMake command, updated log path), then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_filecache_manager_test \
  > <velox_build_dir>/build_task_013_factory_mgr.log 2>&1
```

Expected: exit code 0.

- [ ] **Step 10: Run the focused tests**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_filecache_manager_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_013_factory_mgr.log 2>&1
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 11: Inspect task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Interpreters/FileCache/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/FileCacheFactory.h \
  velox/ch/Interpreters/FileCache/FileCacheFactory.cpp \
  velox/ch/Interpreters/FileCache/FileCacheManager.h \
  velox/ch/Interpreters/FileCache/FileCacheManager.cpp \
  velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp
```

Expected: no whitespace errors, no files outside the declared scope changed by
this task, changes remain unstaged and uncommitted.

- [ ] **Step 12: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/013-filecache-factory-manager-result.md
```

Use exactly this structure:

````markdown
# Task 013 Result: `FileCacheFactory` and `FileCacheManager`

## Status

status: success

## Velox status

```text
<paste branch, HEAD, and final `git status --short`>
```

## Files changed

```text
<list only task-owned files>
```

## Commands run

```text
<paste configure, build, test, and verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_task_013_factory_mgr.log
<velox_build_dir>/build_task_013_red.log
<velox_build_dir>/build_task_013_factory_mgr.log
<velox_build_dir>/test_task_013_factory_mgr.log
```

## Verification

```text
Red build failed because FileCacheFactory / FileCacheManager headers were absent.
Final build exit code:
Focused test result:
git diff --check result:
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 014: FileCacheReadOptions / RequestContext / FileIdentity helpers,
FileCacheBufferedInput, FileCacheInputStream (Velox only, no Gluten edits).
```
````

## Explicit Exclusions

Do not implement in this task:

```text
FileCacheReadOptions / FileCacheRequestContext / FileCacheFileIdentity
FileCacheBufferedInput / FileCacheInputStream
FileCacheSettingsLoader (used by future Gluten integration only)
Gluten VeloxBackend / GlutenBufferedInputBuilder integration
CacheFileSystem / CachedReadFile
Prometheus / custom metrics (keep no-op shims)
```

These belong to Task 014 and the subsequent Gluten integration task.
