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

### Post-Task-012 amendment: SCC-phase state Task 013 must reconcile (2026-07-20)

Task 012 landed the center SCC as a green build. To do so without the Manager
(which is this task), `FileCache` **temporarily owns**, in the SCC phase, the
runtime resources this task's design assigns to the Manager. Task 013 must take
these over so the Manager becomes the real owner and `FileCache` receives them by
injection:

- `FileCacheWorkerPool` — currently a `FileCache` member (`FileCache.h`), injected
  into `CacheMetadata` by reference. Move ownership to the Manager (design 04);
  `FileCache`/`CacheMetadata` receive it by reference from the Manager.
- `folly::Timekeeper` + `FileCacheScheduler` — currently `FileCache` members
  (B6). Design 05/02 puts the shared timer/worker resources under the Manager;
  reconcile ownership per the Manager design without changing the observable
  scheduling semantics.
- `commonUserId` — currently a `FileCache` ctor-injected member (B5). The Manager
  supplies the stable `commonUserId` (design 10:164, `FileCacheManager::Options
  .commonUserId`); host provides it to the Manager.
- **Opened-file-handle invalidation (B7 seam).** Task 012 left two `no-op +
  TODO(Task 013)` seams where CH invalidates cached open handles after a physical
  file change (`fs::remove` in `Metadata.cpp` `removeFileSegmentImpl`, and
  `fs::rename` in `FileSegment.cpp` `renameToIncludeSizeInNameUnlocked`). Task 013
  owns `OpenedFileCache` (design 02:35,89,244; this task's `openedFileCache_`
  member). Wire the REAL Manager-backed invalidation into those two seams
  (replacing the no-ops), and add a test that a removed/renamed path's cached
  handle is dropped. `fs::remove`/`fs::rename` already run in the SCC phase;
  Task 013 adds the handle invalidation that was correctly deferred.

Where this amendment and the original Step text below disagree, this amendment
and the CH source win.

### D1/D2/D3 CH-aligned contracts (2026-07-20, user-decided: do it in Task 013)

The first Task-013 worker correctly stopped at the unreviewed-dependency gate on
three items (D1 OpenedFileCache internal contract, D2 the B7 injection shape, D3
the FileCache ownership-move signature). User decision: implement all three in
Task 013, aligned to CH. These are the fixed contracts — the worker must follow
them and not re-derive:

**D1 — `OpenedFileCache` internal contract (port CH `src/IO/OpenedFileCache.h`,
118 lines, faithfully; only the two forced substitutions below).**
- Structure 1:1 with CH: `static constexpr size_t buckets = 1024;` a
  vector/array of `buckets` shards, each a `{ std::mutex; map<Key, WeakPtr> }`.
  `Key = std::pair<std::string /*path*/, int /*flags*/>`. Bucket =
  `hash(path) % buckets` (CH uses CityHash64; Velox may use `folly::hash` /
  `std::hash<std::string>` — pure in-memory bucket distribution, no persistence,
  no bit-compat requirement; register as an infra substitution).
- **Handle substitution (user-confirmed):** CH caches
  `std::shared_ptr<OpenedFile>` (fd + mmap). The Velox port caches the read
  handle for a cache-segment file: `std::shared_ptr<velox::ReadFile>` (value
  stored as `std::weak_ptr<velox::ReadFile>`). `get(path, flags)` returns a
  `shared_ptr<ReadFile>`: on a live weak hit, reuse it (increment a hit event);
  else open via the injected `filesystems::FileSystem::openFileForRead(path)`
  and install it with a custom deleter that erases the map entry on last release
  (mirroring CH's deleter at `OpenedFileCache.h:65-76`).
- `remove(path, flags)`: erase the key (idempotent), same as CH `:82-87`.
- **Ownership substitution (design 02):** NOT a process singleton and NOT the
  Hive `FileHandleCache`. `FileCacheManager` owns one `OpenedFileCache`
  (`openedFileCache_`), constructed with the injected `filesystems::FileSystem&`
  (for opening) — CH's `memoryPool` is for mmap accounting and is NOT needed for
  the ReadFile-based port; if the illustrative header lists a `memoryPool&`
  param, drop it unless a concrete use appears (record the drop).
- Stats: `FileHandleCacheStats` / the `FileCacheStats` fields that
  `refreshStats`/`FileCacheManagerStats` need — define minimal real fields
  (e.g. cached-handle count, hits, misses); do not invent unused surface.

**D2 — B7 injection shape (aligned to CH remove/rename sites).**
- Inject `OpenedFileCache &` (a reference to the Manager-owned instance), NOT a
  `std::function`. Thread it: `FileCacheManager` -> `FileCache` ctor ->
  `CacheMetadata` ctor (reference member) -> reached by `FileSegment` via its
  key-metadata back-reference for the rename seam.
- Wire the two Task-012 no-op+TODO seams to call it after the physical change:
  - removal seam `Metadata.cpp removeFileSegmentImpl` (after `fs::remove`):
    invalidate the removed path. CH pairs `remove(path, flags)` and
    `remove(path, flags | O_DIRECT)` (`Metadata.cpp:1267-1268`); the Velox port
    invalidates the path's cached handle(s) — since local cache-segment reads do
    not use O_DIRECT, a single `remove(path, flags)` is sufficient; if the key
    keeps `flags`, drop all flag-variants for the path.
  - rename seam `FileSegment.cpp renameToIncludeSizeInNameUnlocked` (after
    `fs::rename`): invalidate the OLD path (CH `FileSegment.cpp:800-802`).
- Add a real test: a path's cached handle is dropped after remove/rename (this
  replaces the Task-012 "S4 tests must avoid the opened-handle path" note).

**D3 — FileCache resource-ownership move (own -> injected reference).**
- Move ownership of `FileCacheWorkerPool`, `folly::Timekeeper` +
  `FileCacheScheduler`, and `commonUserId` from `FileCache` (where Task 012 put
  them as owned members) to the Manager. `FileCache` receives them by reference
  via its constructor; per design `02:373-382` each `FileCache` is constructed
  with explicit references: `workerPool`, `scheduler`, `openedFileCache`,
  `localFileSystem`, `memoryPool` (drop `memoryPool` if unused per D1),
  `commonUserId`.
- The Manager owns these and MUST outlive every `FileCache`. Manager
  destruction order (design 02:299-300, 013 shutdown order
  "cache workers -> timers -> physical pool -> handles"): destroy the
  factory/registry (and thus all `FileCache`s) BEFORE the opened-file cache,
  scheduler/timekeeper, and worker pool. Encode this via Manager member
  declaration order so the owned resources are declared BEFORE the factory and
  destroyed after it.
- After the move, `velox_ch_filecache_core_scc_test` MUST still pass
  (0 failed / 0 skipped): the SCC test constructs a `FileCache`, so provide/adapt
  a test fixture that supplies the now-required injected references (a minimal
  Manager or a direct-injection helper). The observable scheduling / worker /
  eviction semantics must not change.

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

---

## Post-acceptance amendment 1 — `FileCacheManager::hasDefault` (reopened 2026-07-21)

```text
state: reopened_by_contract_audit
owner_task: 013
environment_profile: home-chang
```

### Why

Task 018a needs to decide, at builder-install time, whether a `FileCacheManager`
is configured with a default cache — **without throwing**. The Manager's only
default accessor `getDefault()` (`FileCacheManager.cpp:148-153`) throws when
`defaultCacheName_` is empty, and there is no non-throwing predicate. Using a
`try/catch` around `getDefault()` at the install boundary is a generic-catch
fallback the project rules forbid (it would also swallow a genuine
"default name set but its cache missing" error).

The `default` concept lives in `FileCacheManager` (introduced by Task 013;
ClickHouse has no default-cache concept — this is a port-side addition). The
non-throwing predicate therefore belongs to Task 013, not Task 018a. Task 018a
only consumes it (separate amendment on the 018a task). A later Task 020 will
remove the default-cache concept entirely and switch consumers to
name-based cache selection to match CH; this amendment is the minimal
non-throwing predicate needed until then.

### Scope (this amendment only)

Modify in the Velox checkout:

```text
velox/ch/Interpreters/FileCache/FileCacheManager.h    (declare hasDefault)
velox/ch/Interpreters/FileCache/FileCacheManager.cpp  (define hasDefault; optional getDefault reuse)
velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp  (RED test)
```

Do not change any other file. Do not touch `FileCacheFactory`, the `default`
semantics of `getDefault()` (it must still throw on a missing default — design 02
"missing default = error / never fall back"), or Task 018a files.

### Contract

Add a `const`, non-throwing, thread-consistent predicate:

```cpp
/// True iff a default cache name is configured. Non-throwing companion to
/// `getDefault` (which throws when no default is configured). Does NOT verify
/// the named cache still exists in the factory — it reflects configuration, the
/// same field `getDefault` guards on.
bool hasDefault() const;   // returns !defaultCacheName_.empty()
```

- `hasDefault()` must be a pure read of `defaultCacheName_` (the same field
  `getDefault()` guards on): returns `false` when the Manager was built with an
  empty `defaultCacheName`, `true` otherwise. It must not call `factory_.get`,
  must not throw, and must not depend on initialize/shutdown state.
- `getDefault()` behavior is unchanged (still throws "no default cache
  configured" when empty). The two must agree: `hasDefault()` is `false` exactly
  when `getDefault()` would throw the "no default cache configured" exception.

### Acceptance

Extend `velox_ch_filecache_manager_test` (currently 19) with a real RED test:

1. A Manager built with a non-empty `defaultCacheName` → `hasDefault()` is
   `true` **and** `getDefault()` returns that cache (no throw).
2. A Manager built with an empty `defaultCacheName` → `hasDefault()` is `false`
   **and** `getDefault()` throws.
3. **RED requirement.** The test must fail against the pre-change implementation
   for the expected reason (i.e. before `hasDefault` exists it does not compile /
   with a stubbed `return true;` the empty-default case turns red). Prove
   falsifiability: a `hasDefault()` that always returns `true` must fail case 2.

Regression: `velox_ch_filecache_manager_test` and
`velox_ch_filecache_core_scc_test` stay green (0 failed / 0 skipped) with the new
test included. No `-j`. Nothing staged/committed. Logs under `<velox_build_dir>`.

### Result receipt

Append `## Worker attempt` sections to
`port/task/result/013-filecache-factory-manager-result.md` (do not erase the
original accepted result), in the EXECUTION_PROTOCOL worker-receipt format.
