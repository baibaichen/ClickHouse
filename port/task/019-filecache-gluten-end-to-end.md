# Task 019: Gluten `FileCache` Builder and Lifecycle End-to-End Validation

> **Deferred Gluten task.** Do not dispatch in the current Velox-only phase.
>
> Read `port/task/ENVIRONMENT.md` first. This task modifies only Gluten tests
> and writes one result file under the ClickHouse checkout. Do not stage or
> commit either repository.

## Goal

Validate the Task-018 Gluten integration through the real
`GlutenBufferedInputBuilder` and `VeloxBackend` lifecycle:

```text
FileCache Manager installed -> FileCacheBufferedInput
no FileCache + AsyncDataCache -> CachedBufferedInput
no cache -> GlutenDirectBufferedInput
miss -> fill -> later hit through Builder
FileCache takes precedence over AsyncDataCache on the same scan path
VeloxBackend tearDown shuts Manager before executors and memory pool
```

## Prerequisites

```text
Task 015 Velox E2E/benchmark succeeded.
Task 018 Gluten integration succeeded.
Gluten CMakeCache points VELOX_HOME and VELOX_BUILD_PATH at
<velox_repo> and its <velox_build_dir> build.
```

Read:

```text
port/task/ENVIRONMENT.md
port/task/015-filecache-velox-end-to-end.md
port/task/018-filecache-gluten-integration.md
port/task/result/015-filecache-velox-e2e-result.md
port/task/result/018-filecache-gluten-integration-result.md
```

## File scope

Modify:

```text
<gluten_repo>/cpp/velox/tests/CMakeLists.txt
```

Create:

```text
<gluten_repo>/cpp/velox/tests/FileCacheE2EGlutenTest.cpp
<clickhouse_repo>/port/task/result/019-filecache-gluten-e2e-result.md
```

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm both repository baselines**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline

cd <gluten_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline

grep -E '^(VELOX_HOME|VELOX_BUILD_PATH):' \
  <gluten_repo>/cpp/build/CMakeCache.txt
```

Stop if Gluten still points at its bundled `ep/build-velox` checkout.

- [ ] **Step 2: Register the focused Gluten test**

Append to `cpp/velox/tests/CMakeLists.txt`:

```cmake
add_velox_test(
  velox_file_cache_e2e_gluten_test
  SOURCES FileCacheE2EGlutenTest.cpp)
```

- [ ] **Step 3: Implement real Builder fixtures and tests**

Create `FileCacheE2EGlutenTest.cpp` with the ASF license and real
`FileHandle`, `ConnectorQueryCtx`, memory pool, optional `AsyncDataCache`,
`FileCacheManager`, and `GlutenBufferedInputBuilder` objects.

Implement these tests without `GTEST_SKIP`, `DISABLED_`, fake connectors, or
empty assertions:

```text
BuilderProducesFileCacheInputWhenManagerInstalled
BuilderFallsBackToCachedInputWhenNoFileCache
BuilderFallsBackToDirectInputWhenNoCache
MissFillHitViaBuilder
FileCacheExcludesAsyncDataCacheOnSamePath
VeloxBackendTearDownStopsManagerBeforeRuntimeResources
```

Every test owns its memory-pool `shared_ptr` for at least as long as its
Manager. `TearDown` must call Manager shutdown and clear the singleton before
releasing the pool.

- [ ] **Step 4: Reject false-green tests**

```bash
if rg -n 'GTEST_SKIP|DISABLED_' \
  <gluten_repo>/cpp/velox/tests/FileCacheE2EGlutenTest.cpp
then
  echo "ERROR: skipped Gluten FileCache test remains"
  exit 1
fi
```

- [ ] **Step 5: Build the external Velox library and Gluten test**

```bash
<ninja> \
  -C <velox_build_dir> \
  velox \
  > <velox_build_dir>/build_task_019_external_velox.log 2>&1

<cmake> --build <gluten_repo>/cpp/build \
  --target velox_file_cache_e2e_gluten_test \
  > <gluten_repo>/cpp/build/build_task_019_gluten_e2e.log 2>&1
```

Do not add `-j`.

- [ ] **Step 6: Run the focused Gluten test**

```bash
ctest \
  --test-dir <gluten_repo>/cpp/build \
  -R '^velox_file_cache_e2e_gluten_test$' \
  --output-on-failure \
  > <gluten_repo>/cpp/build/test_task_019_gluten_e2e.log 2>&1
```

Expected:

```text
100% tests passed, 0 tests failed.
```

- [ ] **Step 7: Inspect task-owned changes**

```bash
cd <gluten_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  cpp/velox/tests/CMakeLists.txt \
  cpp/velox/tests/FileCacheE2EGlutenTest.cpp
```

Changes remain unstaged and uncommitted.

- [ ] **Step 8: Write the result handoff**

Create `port/task/result/019-filecache-gluten-e2e-result.md` with:

```text
status: success / blocked / failed
Velox and Gluten branch, HEAD, dirty status
files changed
commands run
three log paths
test count and failures
first actionable error if blocked/failed
recommended next task: none; Gluten integration acceptance complete
```

Stop after writing the result file.

## Explicit exclusions

```text
Spark/Scala application-level integration suite
write-through cache
overcommit priority
new FileCache production behavior
```
