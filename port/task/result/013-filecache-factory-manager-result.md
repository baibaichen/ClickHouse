# Task 013 Result: `FileCacheFactory` and `FileCacheManager`

## Status

status: success (senior-review fix wave applied; see "Fix wave" section below)

## Velox status

```text
branch filecache, HEAD a46ff4716cf9656be6d89562ed4b8ba40b0bba18 (unchanged; nothing committed)

git status --short:
 M velox/ch/Interpreters/FileCache/CMakeLists.txt
 M velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
?? velox/ch/Interpreters/FileCache/FileCacheFactory.cpp
?? velox/ch/Interpreters/FileCache/FileCacheFactory.h
?? velox/ch/Interpreters/FileCache/FileCacheManager.cpp
?? velox/ch/Interpreters/FileCache/FileCacheManager.h
?? velox/ch/Interpreters/FileCache/OpenedFileCache.h
?? velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp
```

## Files changed

```text
Modified:
  velox/ch/Interpreters/FileCache/CMakeLists.txt        (add velox_ch_filecache_manager library)
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt  (add velox_ch_filecache_manager_test)
Created:
  velox/ch/Interpreters/FileCache/FileCacheFactory.h
  velox/ch/Interpreters/FileCache/FileCacheFactory.cpp
  velox/ch/Interpreters/FileCache/FileCacheManager.h
  velox/ch/Interpreters/FileCache/FileCacheManager.cpp
  velox/ch/Interpreters/FileCache/OpenedFileCache.h
  velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp
```

## Commands run

```text
# Environment
source /root/oss/velox-helper/env.sh

# Configure (mono, _build/debug)
/usr/bin/cmake -S /root/oss/velox -B /root/oss/velox/_build/debug
  > /root/oss/velox/_build/debug/configure_task_013_factory_mgr.log 2>&1

# RED build (before implementation; headers/library absent)
/usr/local/bin/ninja -C /root/oss/velox/_build/debug velox_ch_filecache_manager_test
  > /root/oss/velox/_build/debug/build_task_013_red.log 2>&1   # failed as expected

# GREEN build + focused test (mono)
/usr/local/bin/ninja -C /root/oss/velox/_build/debug velox_ch_filecache_manager_test
  > /root/oss/velox/_build/debug/build_task_013_factory_mgr.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug -R '^velox_ch_filecache_manager_test$' --output-on-failure
  > /root/oss/velox/_build/debug/test_task_013_factory_mgr.log 2>&1

# Non-mono build + focused test (_build/debug-task012-nonmono, VELOX_MONO_LIBRARY=OFF)
/usr/bin/cmake -S /root/oss/velox -B /root/oss/velox/_build/debug-task012-nonmono
  > /root/oss/velox/_build/debug-task012-nonmono/configure_task_013_nonmono.log 2>&1
/usr/local/bin/ninja -C /root/oss/velox/_build/debug-task012-nonmono velox_ch_filecache_manager_test
  > /root/oss/velox/_build/debug-task012-nonmono/build_task_013_nonmono.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug-task012-nonmono -R '^velox_ch_filecache_manager_test$' --output-on-failure
  > /root/oss/velox/_build/debug-task012-nonmono/test_task_013_nonmono.log 2>&1

# Accumulated velox_ch_ regression (mono): all 12 velox_ch_ tests
ctest --test-dir /root/oss/velox/_build/debug -R '^velox_ch_' --output-on-failure
  > /root/oss/velox/_build/debug/test_task_013_accumulated.log 2>&1

# Verification
git -C /root/oss/velox diff --check
git -C /root/oss/velox status --short
```

## Generated logs

```text
/root/oss/velox/_build/debug/configure_task_013_factory_mgr.log
/root/oss/velox/_build/debug/build_task_013_red.log
/root/oss/velox/_build/debug/build_task_013_factory_mgr.log
/root/oss/velox/_build/debug/test_task_013_factory_mgr.log
/root/oss/velox/_build/debug/test_task_013_accumulated.log
/root/oss/velox/_build/debug-task012-nonmono/configure_task_013_nonmono.log
/root/oss/velox/_build/debug-task012-nonmono/build_task_013_nonmono.log
/root/oss/velox/_build/debug-task012-nonmono/test_task_013_nonmono.log
```

## Verification

```text
Red build failed because FileCacheFactory / FileCacheManager headers were absent.
  (build_task_013_red.log: "fatal error: velox/ch/Interpreters/FileCache/FileCacheManager.h: No such file or directory")
Final build exit code: 0 (mono and non-mono)
Focused test result: 100% tests passed, 0 tests failed out of 1
  (velox_ch_filecache_manager_test: 31 tests PASSED in both mono and non-mono)
Accumulated velox_ch_ mono regression: 100% tests passed, 0 tests failed out of 12
git diff --check result: clean (exit 0), no whitespace errors; only the 8 task-owned files changed.
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

---

# Fix wave (senior-review findings)

## Status

status: success — all 6 findings addressed; mono + non-mono focused suites and
the accumulated `velox_ch_` suite are green; every new behavior has a false-green
mutation that was confirmed to fail and then restored exactly.

## Velox status

```text
branch filecache, HEAD a46ff4716cf9656be6d89562ed4b8ba40b0bba18 (unchanged; nothing committed)

git status --short (only the 8 task-owned files):
 M velox/ch/Interpreters/FileCache/CMakeLists.txt
 M velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
?? velox/ch/Interpreters/FileCache/FileCacheFactory.cpp
?? velox/ch/Interpreters/FileCache/FileCacheFactory.h
?? velox/ch/Interpreters/FileCache/FileCacheManager.cpp
?? velox/ch/Interpreters/FileCache/FileCacheManager.h
?? velox/ch/Interpreters/FileCache/OpenedFileCache.h
?? velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp
```

## Findings and fixes

### 1. `applyConfigs` new caches — create + initialize + fail-close rollback

`FileCacheManager::applyConfigs` now, when the Manager is already `Initialized`,
snapshots the unique instances before registration, registers unknown names via
`getOrCreateLocked`, and then initializes each *genuinely new* unique cache
exactly once (a new alias to an already-initialized cache is not in the
before/after diff, so it never re-initializes). On any initialize failure it
fails closed: every cache created by this call is removed via
`FileCacheFactory::removeLocked` (deactivate outside the registry lock + restore
the truthful worker budget/pool), pre-existing bindings are preserved, and the
original exception is rethrown. This also honors the design "must not continue
initialization with only a subset of requested caches".

### 2. Worker budget reload evidence

The existing "grow before apply, shrink after quiesce, never below aggregate
need" logic is retained and now covered by explicit reload tests (raise/lower
`backgroundDownloadThreads`) plus partial-apply/first-exception/truthful-actual-
snapshot coverage. The `applyConfigs::applyOutsideLock` seam was moved INSIDE the
per-cache `try` so a test can inject an apply failure without changing the
lock-free property the pre-existing test asserts.

### 3. Serialized mutating lifecycle/pool operations

Added one Manager-owned `mutation_mutex_` (also guards `state_`; the now-unused
`lifecycle_cv_` was removed). It is injected into the Factory via
`RuntimeServices::mutationMutex`. The Factory's public
`getOrCreate/create/remove/clear` take it and delegate to private `*Locked`
bodies; `FileCacheManager::initialize/applyConfigs/shutdown` hold it for their
whole body and call the `*Locked` bodies directly (non-recursive mutex). Lock
order is `mutation_mutex_ -> registry_mutex_ -> settings_mutex_`, never inverted,
and cache initialize/apply/deactivate still run OUTSIDE the Factory registry
lock. `remove/clear` skip the shared-pool resize once the Factory is shut down
(`isShutdown()`), so a stopped pool is never resized. A test-only
`FileCacheManager::isMutationLockFree()` probe (mirrors `isRegistryLockFree`)
backs deterministic barrier/future tests for apply-vs-getOrCreate and
apply-vs-shutdown with no sleeps.

### 4. `OpenedFileCache` lifetime UAF

Each bucket's `std::map` + `std::mutex` moved into a heap
`shared_ptr<BucketState>`; the per-entry custom deleter now captures a
`weak_ptr<BucketState>`. On the last strong holder dropping it re-locks the state
only if still alive (normal erase-on-last-holder); if the `OpenedFileCache` was
already destroyed the weak pointer is expired and the deleter just frees the
`OpenedFile` without touching the destroyed map/mutex. 1024 buckets, `std::map`,
`MemoryPool` `StlAllocator` (map nodes stay pool-charged), path-only
`std::hash`, weak entries, and normal sharing/invalidation are all preserved.
`BucketState` is destroyed together with its bucket during `OpenedFileCache`
destruction, while the injected pool is still alive, so pool-charged nodes are
always freed against a live pool.

### 5. Global raw instance

No functional change required. The explicit uninstall contract is retained and
the `setInstance` doc sharpened: `shutdown()` -> `setInstance(nullptr)` -> drop
the owning `shared_ptr`; the Manager never auto-installs on `create`, never
self-uninstalls on destruction, and `setInstance` rejects replacing a live
different Manager (same-Manager re-install is a no-op).

## Files changed

```text
Modified (in place, all six task-owned Velox files; CMakeLists unchanged this wave):
  velox/ch/Interpreters/FileCache/FileCacheFactory.h    (RuntimeServices::mutationMutex; *Locked decls; isShutdown)
  velox/ch/Interpreters/FileCache/FileCacheFactory.cpp  (public mutators lock mutationMutex -> *Locked; isShutdown-guarded pool resize)
  velox/ch/Interpreters/FileCache/FileCacheManager.h    (single mutation_mutex_; drop lifecycle_cv_; isMutationLockFree; setInstance doc)
  velox/ch/Interpreters/FileCache/FileCacheManager.cpp  (applyConfigs new-cache init + fail-close; hold mutation_mutex_ across initialize/applyConfigs/shutdown; seam moved into try; isMutationLockFree)
  velox/ch/Interpreters/FileCache/OpenedFileCache.h     (BucketState shared state + weak_ptr deleter)
  velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp  (+10 tests: 31 -> 41)
```

## New tests (10 added; 31 -> 41)

```text
Finding 1: ApplyConfigsCreatesAndInitializesNewUniqueCache
           ApplyConfigsNewAliasDoesNotReinitialize
           ApplyConfigsNewCacheInitFailureRollsBack
Finding 2: ReloadRaiseGrowsPoolBeforeApply
           ReloadLowerShrinksPoolAfterQuiesce
           ReloadPartialApplyKeepsTruthfulSnapshotsAndFirstException
Finding 3: ApplyConfigsHoldsMutationLockDuringApply
           ApplyVsGetOrCreateSerialized          (barrier/future, no sleep)
           ApplyVsShutdownSerialized             (barrier/future, no sleep)
Finding 4: OpenedFileCacheHandleOutlivesCache
```

## Commands run

```text
source /root/oss/velox-helper/env.sh

# Final mono build + focused + accumulated (no -j)
/usr/local/bin/ninja -C /root/oss/velox/_build/debug velox_ch_filecache_manager_test
  > /root/oss/velox/_build/debug/build_task_013_fixwave_final.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug -R '^velox_ch_filecache_manager_test$'
  > /root/oss/velox/_build/debug/test_task_013_fixwave_final.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug -R '^velox_ch_'
  > /root/oss/velox/_build/debug/test_task_013_fixwave_final_accumulated.log 2>&1

# Final non-mono build + focused (_build/debug-task012-nonmono, VELOX_MONO_LIBRARY=OFF)
/usr/local/bin/ninja -C /root/oss/velox/_build/debug-task012-nonmono velox_ch_filecache_manager_test
  > /root/oss/velox/_build/debug-task012-nonmono/build_task_013_fixwave_final.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug-task012-nonmono -R '^velox_ch_filecache_manager_test$'
  > /root/oss/velox/_build/debug-task012-nonmono/test_task_013_fixwave_final.log 2>&1

# Finding-4 UAF false-green under AddressSanitizer (ASan/UBSan OFF in the debug
# build and glibc-debuginfo/valgrind unavailable, so the single test TU was
# recompiled + relinked with -fsanitize=address; ASan's malloc interceptor
# catches the heap UAF against the otherwise non-ASan static libs).

git -C /root/oss/velox diff --check                      # tracked: clean
git -C /root/oss/velox diff --no-index --check /dev/null <each new file>  # 0 whitespace errors
git -C /root/oss/velox status --short
```

## Generated logs

```text
/root/oss/velox/_build/debug/build_task_013_fixwave_final.log
/root/oss/velox/_build/debug/test_task_013_fixwave_final.log
/root/oss/velox/_build/debug/test_task_013_fixwave_final_accumulated.log
/root/oss/velox/_build/debug-task012-nonmono/build_task_013_fixwave_final.log
/root/oss/velox/_build/debug-task012-nonmono/test_task_013_fixwave_final.log
/root/oss/velox/_build/debug-task012-nonmono/test_task_013_fixwave_asan_full.log                 (41/41 under ASan, 0 errors)
/root/oss/velox/_build/debug-task012-nonmono/test_task_013_fixwave_openedfile_uaf_asan_buggy.log (buggy deleter: heap-use-after-free)
```

## Test results

```text
Mono   focused  velox_ch_filecache_manager_test: 41/41 PASSED (ctest: 100%, 0 failed of 1)
Non-mono focused velox_ch_filecache_manager_test: 41/41 PASSED (ctest: 100%, 0 failed of 1)
Mono   accumulated ^velox_ch_ :                    12/12 PASSED (100%)
ASan   full velox_ch_filecache_manager_test:       41/41 PASSED, 0 AddressSanitizer errors
```

## False-green mutations (each confirmed failing, then restored exactly)

```text
Finding 1a  applyConfigs: `if (manager_initialized)` -> `if (false)`
            => ApplyConfigsCreatesAndInitializesNewUniqueCache FAILED (B not initialized)
Finding 1b  applyConfigs: remove the rollback removeLocked loop (keep throw)
            => ApplyConfigsNewCacheInitFailureRollsBack FAILED ("bad" still retrievable)
Finding 2a  applyConfigs: disable the up-front grow
            => ReloadRaiseGrowsPoolBeforeApply FAILED (poolAtApply 5 vs after 9)
Finding 2b  applyConfigs: shrink the pool one early when lowering
            => ReloadLowerShrinksPoolAfterQuiesce FAILED (poolAtApply != high)
Finding 2c  applyConfigs: swallow the first apply exception
            => ReloadPartialApplyKeepsTruthfulSnapshotsAndFirstException FAILED (no throw)
Finding 3   applyConfigs: disable the mutation lock
            => ApplyConfigsHoldsMutationLockDuringApply, ApplyVsGetOrCreateSerialized,
               ApplyVsShutdownSerialized all FAILED (mutation lock observed free)
Finding 4   OpenedFileMap deleter: capture raw BucketState* and touch it unconditionally
            => OpenedFileCacheHandleOutlivesCache: AddressSanitizer heap-use-after-free
               at FileCacheFactoryManagerTest.cpp handle.reset() -> std::map::erase on the
               freed BucketState (clean pass after restoring the weak_ptr deleter)
```

## Verification

```text
Final build exit code: 0 (mono and non-mono).
Focused test result: 41/41 PASSED (mono and non-mono).
Accumulated ^velox_ch_ (mono): 12/12 PASSED.
ASan full run: 41/41 PASSED, 0 AddressSanitizer errors.
git diff --check: clean (tracked); 0 whitespace errors in the 6 new files.
git status: only the 8 task-owned files changed; nothing staged or committed.
All false-green markers removed; the 3 mutated sources byte-match their pre-mutation state.
```

## Remaining concerns

```text
- The debug profile builds with ASan/UBSan OFF and valgrind is unavailable
  (glibc-debuginfo missing), so the finding-4 UAF false-green was demonstrated by
  recompiling only the test translation unit with -fsanitize=address and relinking
  against the existing non-ASan static libs. Enabling a first-class ASan CI profile
  would let the UAF regression be caught by the normal suite rather than an ad-hoc
  relink.
```

---

# Fix wave 2 (Task-013 remaining Major: `applyConfigs` registration/grow not fail-close)

## Status

status: success — the single remaining Major is fixed; mono + non-mono focused
suites (42/42) and the accumulated `velox_ch_` suite (12/12) are green; the new
production-path test has a confirmed false-green mutation that was restored
exactly. Nothing staged/committed/pushed.

## Finding addressed

`FileCacheManager::applyConfigs` only rolled back new caches when a new cache's
`initialize()` failed. If registering a later config (`getOrCreateLocked`
throwing on a conflicting alias/settings) or the up-front pool grow threw AFTER an
earlier new cache had already been registered, that earlier name/instance/path
binding leaked (retrievable + uninitialized) and the worker budget/pool stayed
grown.

## Fix

The entire new-cache **registration + up-front pool grow + initialize** phase is
now one fail-close transaction relative to a pre-apply snapshot:
- `applyConfigs` snapshots `namesBefore = factory_.getAllNames()` (new Factory
  accessor) and the unique-instance set `before`, then runs registration, grow and
  new-cache initialize inside a single `try`.
- On ANY exception the `catch` calls the new
  `FileCacheFactory::rollbackNewBindingsLocked(namesBefore)` and rethrows the
  original exception. That primitive (under the Manager-owned mutation mutex):
  removes every name binding not in `namesBefore`; deactivates (joins) each unique
  cache that becomes unreferenced — i.e. every instance this call created — outside
  the registry lock; recomputes `worker_budget_` from the surviving pre-existing
  caches; and resizes the shared pool to that truthful budget (skipped when shut
  down). Pre-existing name/instance bindings are preserved.
- The per-cache **apply** phase (settings applied on pre-existing caches) is
  unchanged: its documented partial-apply / first-exception / truthful-actual-
  snapshot behavior is retained; successfully applied settings are not rolled back.
- The prior initialize-only rollback (`removeLocked` per newly created instance)
  was subsumed by the unified transaction, which is strictly more correct because
  it also drops any new alias name that pointed at a pre-existing instance.

Also removed the dead `OpenedFileCache::pool_` member (only ever initialized,
never read; the bucket allocator/maps bind the pool via the ctor parameter). No
other cleanup. Mutation/lifecycle serialization and all previous fixes retained.

## Files changed (all task-owned Velox files, in place)

```text
velox/ch/Interpreters/FileCache/FileCacheFactory.h    (CacheNames type; getAllNames; rollbackNewBindingsLocked decl)
velox/ch/Interpreters/FileCache/FileCacheFactory.cpp  (getAllNames; rollbackNewBindingsLocked impl)
velox/ch/Interpreters/FileCache/FileCacheManager.cpp  (applyConfigs: registration+grow+initialize wrapped in one fail-close try/catch -> rollbackNewBindingsLocked)
velox/ch/Interpreters/FileCache/OpenedFileCache.h     (remove dead pool_ member)
velox/ch/Interpreters/FileCache/tests/FileCacheFactoryManagerTest.cpp  (+1 test: 41 -> 42)
```

## New test (41 -> 42)

```text
ApplyConfigsNewCacheRegistrationFailureRollsBack
  Initialized manager with cache A. applyConfigs reloads A (no-op) + a valid new
  unique cache "fresh" + a later "clash" alias (same path as "fresh", different
  maxSize) that getOrCreateLocked rejects mid-registration. Asserts: applyConfigs
  throws; no leftover "fresh"/"clash" name, no new instance, no new path
  (getUniqueInstances==1, getAll==1); pre-existing "A" binding unchanged and still
  initialized; worker budget == pre-apply budget; pool numThreads == pre-apply
  pool size (exact equality).
```

## Commands run

```text
source /root/oss/velox-helper/env.sh

# Mono (_build/debug)
/usr/local/bin/ninja -C /root/oss/velox/_build/debug velox_ch_filecache_manager_test
  > /root/oss/velox/_build/debug/build_task_013_task_major_final.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug -R '^velox_ch_filecache_manager_test$'
  > /root/oss/velox/_build/debug/test_task_013_task_major_final.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug -R '^velox_ch_'
  > /root/oss/velox/_build/debug/test_task_013_task_major_accumulated.log 2>&1

# Non-mono (_build/debug-task012-nonmono, VELOX_MONO_LIBRARY=OFF)
/usr/local/bin/ninja -C /root/oss/velox/_build/debug-task012-nonmono velox_ch_filecache_manager_test
  > /root/oss/velox/_build/debug-task012-nonmono/build_task_013_task_major_final.log 2>&1
ctest --test-dir /root/oss/velox/_build/debug-task012-nonmono -R '^velox_ch_filecache_manager_test$' --output-on-failure
  > /root/oss/velox/_build/debug-task012-nonmono/test_task_013_task_major_final.log 2>&1

# False-green mutation (registration rollback disabled), then restored
/usr/local/bin/ninja -C /root/oss/velox/_build/debug velox_ch_filecache_manager_test
  > /root/oss/velox/_build/debug/build_task_013_mutation.log 2>&1
velox_ch_filecache_manager_test --gtest_filter='*RegistrationFailureRollsBack*'
  > /root/oss/velox/_build/debug/test_task_013_mutation.log 2>&1

git -C /root/oss/velox diff --check          # tracked clean (exit 0)
git -C /root/oss/velox status --short        # only the 8 task-owned files
```

## Test results

```text
Mono     focused velox_ch_filecache_manager_test: 42/42 PASSED (ctest 100%, 0 of 1 failed)
Non-mono focused velox_ch_filecache_manager_test: 42/42 PASSED (ctest 100%, 0 of 1 failed)
Mono     accumulated ^velox_ch_ :                 12/12 PASSED (100%)
Final build exit code: 0 (mono and non-mono)
git diff --check: clean; 0 whitespace errors in the 6 new files
git status: only the 8 task-owned files changed; nothing staged/committed
```

## False-green mutation (confirmed failing, then restored exactly)

```text
applyConfigs catch: comment out `factory_.rollbackNewBindingsLocked(namesBefore);`
  (keep `throw;`) => ApplyConfigsNewCacheRegistrationFailureRollsBack FAILED:
  manager->get("fresh") / getByName("fresh") retrievable, getUniqueInstances==2,
  getAll==3, worker budget/pool grown past pre-apply values.
  Rollback call restored verbatim; no MUTATION marker remains (git diff clean);
  focused suite back to 42/42.
```

## Generated logs

```text
/root/oss/velox/_build/debug/build_task_013_task_major_final.log
/root/oss/velox/_build/debug/test_task_013_task_major_final.log
/root/oss/velox/_build/debug/test_task_013_task_major_accumulated.log
/root/oss/velox/_build/debug/build_task_013_mutation.log
/root/oss/velox/_build/debug/test_task_013_mutation.log
/root/oss/velox/_build/debug-task012-nonmono/build_task_013_task_major_final.log
/root/oss/velox/_build/debug-task012-nonmono/test_task_013_task_major_final.log
```

## Remaining concerns

```text
- rollbackNewBindingsLocked and remove/clear restore the pool to the TRUTHFUL
  surviving worker budget (matching the existing remove/clear semantics). If a
  pre-apply pool had been over-provisioned above its budget by an earlier failed
  op, a rollback would also correct it down to truthful; in the tested clean state
  pre-apply pool == budget, so the assertion of exact restoration holds.
- Unchanged from wave 1: the debug profile builds with ASan/UBSan OFF, so the
  finding-4 OpenedFileCache UAF false-green still requires an ad-hoc ASan relink of
  the test TU (a first-class ASan CI profile would fold it into the normal suite).
```

## Controller review 1

```text
controller_status: accepted
environment_profile: root-oss
```

Scope:

- Inspected all six Task-013 production/test files and both CMake changes.
- Traced Manager/Factory ownership, worker budgets, registry aliases, settings
  reload, shutdown, global publication, and opened-file handle lifetime.
- Confirmed no Task 014 or Gluten change.

Review iterations:

```text
initial review:
  applyConfigs registered new caches without initializing them
  shared pool mutations were not serialized across lifecycle operations
  OpenedFileCache handle deleter could outlive Manager and use freed state
  thread-count reload paths lacked evidence

fix review:
  new unique caches initialize once; aliases do not
  initialization failure rolls back bindings and worker budget
  one Manager mutation mutex serializes pool/lifecycle transitions
  weak bucket state makes late handle destruction safe
  grow/shrink/partial-failure/concurrency tests added

final review:
  registration/grow failure before initialize left an earlier new binding
  full applyConfigs transaction rollback added and mutation-proven
  Blocker/Major findings: 0
```

Controller final evidence:

```text
mono direct:
  test_task_013_controller_direct_final.log
  42/42 passed
  0 failed, 0 skipped, 0 disabled

mono focused CTest:
  test_task_013_controller_ctest_final.log
  1/1 passed

mono accumulated CTest:
  test_task_013_controller_accumulated_final.log
  12/12 passed

non-mono:
  CMakeCache.txt: VELOX_MONO_LIBRARY=OFF
  test_task_013_controller_direct_final.log
  42/42 passed
  test_task_013_controller_ctest_final.log
  1/1 passed

git diff --check:
  clean
```

Accepted invariants:

- Manager owns worker pool, scheduler, opened-file cache, and Factory lifetime.
- Factory/FileCache own none of those services and hold no Manager pointer.
- Worker capacity is the checked aggregate budget of unique caches and is
  serialized across reload, registry mutation, and shutdown.
- `applyConfigs` is fail-close for registration, grow, and initialization
  failures while retaining documented truthful partial-apply behavior.
- `OpenedFileCache` preserves 1024 MemoryPool-accounted `std::map` buckets and
  safely handles a file handle released after Manager destruction.
- The global Manager pointer remains an explicit install/uninstall contract.

Independent final review:

```text
spec compliance: approved
technical quality: approved
Blocker/Major findings: 0
```

Accepted Velox commit:

```text
bbda44d2531af0235851bc069fd2d583762d8d96
Task 013: Add `FileCache` manager
```

Task 013 is accepted. Task 014 may proceed.
