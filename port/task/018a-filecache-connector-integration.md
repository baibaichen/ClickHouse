# Task 018a: Pure-Velox Connector Integration — Route Hive Reads Through `FileCache`

> **Pure-Velox task. No Gluten, no ClickHouse source changes.** This is the
> **first phase of Task 018**. Task 018 (original) is now the **second phase**:
> Gluten-side assembly/configuration that reuses the builder produced here.
>
> **For agentic workers:** read `port/task/EXECUTION_PROTOCOL.md` and
> `port/task/ENVIRONMENT.md` first. Select one profile and use only its values.
> Do not stage, commit, amend, rebase, push, or create a PR. Preserve unrelated
> dirty changes.

## Why (Context)

Tasks 003-017 ported the ClickHouse `FileCache` into Velox (`filecache2`,
under `velox/ch/`), but **no real query read path reaches it yet**. When the
Hive connector reads Parquet, it selects `CachedBufferedInput` (native
`AsyncDataCache`) or `DirectBufferedInput` via
`createBufferedInput` — never our `FileCacheBufferedInput`. This blocks the
TPCH three-engine comparison (currently parked).

The user split connector integration from Gluten explicitly:
- **This task (018a) = pure Velox.** Make the connector read path able to
  select `FileCacheBufferedInput`. Every Velox query (including the TPCH
  benchmark) benefits. Independent of Gluten.
- **Task 018 (deferred) = Gluten config/assembly.** Install `FileCacheManager`
  into `VeloxBackend`, add config switches and lifecycle, reusing this builder.

## Approach — register our own `BufferedInputBuilder` (velox trunk unchanged)

Velox exposes an official extension point,
`BufferedInputBuilder::registerBuilder`
(`velox/connectors/hive/BufferedInputBuilder.h:38`; the class comment states
registering a different implementation is allowed). Using it means **velox
trunk `HiveConnectorUtil.cpp` is not touched — zero lines**. All new code lives
in `velox/ch/`.

Verified facts (do not re-derive; re-confirm only if code moved):

```text
- Extension point:  BufferedInputBuilder::getInstance()->create(...) is called
  in the connector read path at FileSplitReader.cpp:319-326 (7-arg form).
- Default already registered: DefaultBufferInputBuilder in
  BufferedInputBuilder.cpp:22-45 forwards to the free createBufferedInput(...).
  getInstance() is never null. registerBuilder(...) overwrites builder_.
- Fallback target: free function createBufferedInput(...) is public,
  declared HiveConnectorUtil.h:142, namespace facebook::velox::connector::hive.
- Native cache selection keys off connectorQueryCtx->cache()
  (HiveConnectorUtil.cpp:661). This is what the mutual-exclusion guard checks.
- FileHandle: velox/common/caching/FileHandle.h:38-50 — field
  `std::shared_ptr<ReadFile> file;`. Path = fileHandle.file->getName().
- ReaderOptions exposes memoryPool() (io::ReaderOptions, Options.h) and
  ioExecutor(); the `executor` create() arg == readerOpts.ioExecutor() at the
  call site.
```

### Routing model — validate at install, take at `create` (user decision 2026-07-21)

The builder does **not** make a per-call "is FileCache installed?" decision.
`FileCacheManager` has **no non-throwing "has a default cache" predicate**
(verified: the only cache accessors are `getDefault()` / `get(name)`, both of
which **throw** when unresolved — `FileCacheManager.h:93-94`,
`FileCacheManager.cpp:148-153`, `FileCacheFactory::get` throws; the only
non-throwing view `factory().getAll()` cannot answer "which is the default"
because `defaultCacheName_` is private with no accessor, `FileCacheManager.h:129`).
Adding such a predicate would widen scope into `FileCacheManager.h` and collide
with design 02's "missing default = error / never fall back to another cache"
contract (`port/3-consumers/02-filecache-manager-design.md:668-685`), which
Task 013 implemented and the Controller accepted.

The resolution moves the "is the config correct?" check to **install time**,
leaving `create()` clean:

- **Install-time validation (fail-fast, via the non-throwing predicate).**
  `registerFileCacheBufferedInputBuilder` checks `manager.hasDefault()` **once**
  at registration to confirm the Manager is configured for FileCache. `hasDefault`
  is the const, non-throwing predicate added in Task 013 (`FileCacheManager.h`),
  true exactly when a default cache name is configured (i.e. exactly when
  `getDefault` would NOT throw). If `hasDefault()` is false, raise a clear error
  (`VELOX_CHECK(manager.hasDefault(), "...")`) — **this is the wanted early
  failure**: calling the install function *declares* "I am installing FileCache",
  so a Manager without a default cache is a caller configuration error that must
  surface immediately, not silently degrade. Do **not** call `getDefault()` at
  install time (that would use a throwing accessor as a validator); use the
  non-throwing `hasDefault()` predicate.
- **Manager still by reference.** The builder **holds a `FileCacheManager&`**
  captured at registration (as originally planned — ownership unchanged). The
  Manager must **outlive** the builder. Do **not** cache the `FileCachePtr` in
  the builder; take it per call in `create()`.
- **No per-call no-default branch.** Because only a FileCache-configured Manager
  can pass install-time validation, `manager_.getDefault()` inside `create()`
  **cannot throw** for the "no default" reason. `create()` therefore has no
  fallback branch on the Manager.

`create(...)` overrides the 7-arg base signature (`BufferedInputBuilder.h:43-50`):

1. **Mutual-exclusion guard** (per-call runtime check, copied intent from the
   native path): `VELOX_CHECK_NULL(connectorQueryCtx->cache(), "FileCache and AsyncDataCache cannot both be installed")`.
   Both active would double-cache. This is independent of install-time config.
2. **Construct `FileCacheBufferedInput`** with its full 12-arg ctor
   (`velox/ch/Disks/IO/FileCacheBufferedInput.h:46-58`). Argument sourcing:
   ```text
   readFile        = fileHandle.file                         (shared_ptr<ReadFile>)
   cache           = manager_.getDefault()                   (FileCachePtr; per-call; cannot throw post-validation; no bare getInstance)
   cacheKey        = FileCacheKey::fromPath(fileHandle.file->getName())
   origin          = cache->getCommonOrigin()                (instance method, not static)
   cacheOptions    = FileCacheReadOptions{}                  (default)
   requestContext  = FileCacheRequestContext{}               (userId = manager_.commonUserId())
   metricsLog      = dwio::common::MetricsLog::voidLog()
   ioStatistics    = ioStatistics                            (create() arg, forwarded)
   ioStats         = ioStats                                 (create() arg, forwarded)
   executor        = executor                                (create() arg, forwarded)
   readerOptions   = readerOpts                              (create() arg)
   fileReadOps     = fileReadOps                             (create() arg)
   ```

**"Not installed" deployments** never call `registerFileCacheBufferedInputBuilder`
at all — they keep the statically-registered `DefaultBufferInputBuilder`
(`BufferedInputBuilder.cpp:44-45`) and read through the native
`createBufferedInput`. This *is* the fallback: it lives at the registration
boundary ("don't install us"), not as an internal `create()` branch. The builder
never needs to forward to `createBufferedInput` itself.

### Install function

Provide `void registerFileCacheBufferedInputBuilder(FileCacheManager& manager);`
that:
1. checks `manager.hasDefault()` (the Task-013 non-throwing predicate) and raises
   a clear error via `VELOX_CHECK` when it is false — fail-fast; do NOT call the
   throwing `getDefault()` as a validator;
2. calls `BufferedInputBuilder::registerBuilder(std::make_shared<FileCacheBufferedInputBuilder>(manager))`.

The host (benchmark / future Gluten Task 018 / pure-Velox tests) calls it once
after building a FileCache-configured Manager. Document: **Manager must outlive
the builder.**

## Design references

Read before editing:

```text
port/task/EXECUTION_PROTOCOL.md
port/task/ENVIRONMENT.md
port/task/result/014-filecache-buffered-input-result.md
port/task/result/013-filecache-factory-manager-result.md
port/3-consumers/02-filecache-manager-design.md
port/3-consumers/03-filecache-buffered-input-design.md
<velox_repo>/velox/connectors/hive/BufferedInputBuilder.h
<velox_repo>/velox/connectors/hive/BufferedInputBuilder.cpp
<velox_repo>/velox/connectors/hive/HiveConnectorUtil.h        (createBufferedInput decl)
<velox_repo>/velox/connectors/hive/HiveConnectorUtil.cpp      (createBufferedInput + guard, lines 653-700)
<velox_repo>/velox/ch/Disks/IO/FileCacheBufferedInput.h       (12-arg ctor)
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheManager.h  (getDefault / commonUserId)
<velox_repo>/velox/ch/benchmarks/FileCacheSeekBenchmark.cpp   (reference Manager build)
```

Do not modify any velox trunk header or source. They are read-only inputs.

## File scope

Create in the Velox checkout:

```text
<velox_repo>/velox/ch/Disks/IO/FileCacheBufferedInputBuilder.h
<velox_repo>/velox/ch/Disks/IO/FileCacheBufferedInputBuilder.cpp
<velox_repo>/velox/ch/Disks/IO/tests/FileCacheBufferedInputBuilderTest.cpp
```

Modify in the Velox checkout:

```text
<velox_repo>/velox/ch/Disks/IO/CMakeLists.txt        (compile builder into velox_ch_filecache; link velox_hive_connector)
<velox_repo>/velox/ch/Disks/IO/tests/CMakeLists.txt  (register the new test binary)
```

**Must NOT change** (verify with `git diff` at review — zero trunk diff):

```text
<velox_repo>/velox/connectors/hive/HiveConnectorUtil.cpp
<velox_repo>/velox/connectors/hive/HiveConnectorUtil.h
<velox_repo>/velox/connectors/hive/BufferedInputBuilder.h
<velox_repo>/velox/connectors/hive/BufferedInputBuilder.cpp
any other file outside velox/ch/
```

Create in the ClickHouse checkout:

```text
<clickhouse_repo>/port/task/result/018a-filecache-connector-integration-result.md
```

Every new Velox C++ file starts with the Apache 2.0 header used by other files
under `velox/ch/` (copy the exact form from `FileCacheBufferedInput.h`).

## Steps

> **Environment setup:** before any configure/build/test command, follow the
> selected profile's setup in `ENVIRONMENT.md`. `home-chang` needs no env
> sourcing; use the recorded configure command. Do not pass `-j`. Redirect each
> build/test command to a unique log under `<velox_build_dir>`.

- [ ] **Step 1 — baselines.** Record Velox branch/HEAD/dirty. Stop if the Velox
  branch is not `filecache2` (home-chang) / `filecache` (root-oss). Confirm
  `FileCacheBufferedInput.h` has the 12-arg ctor and `FileCacheManager` exposes
  `getDefault()` + `commonUserId()`; if a name moved, stop and record it (do not
  guess an alternate API).

- [ ] **Step 2 — write the builder** (`.h` + `.cpp`) per the Approach section.
  Author the `.cpp` incrementally if large. No bare singleton; Manager by ref.

- [ ] **Step 3 — CMake.** Add the builder `.cpp` to the `velox_ch_filecache`
  target sources in `velox/ch/Disks/IO/CMakeLists.txt`; ensure the target links
  `velox_hive_connector` (for `createBufferedInput` / `BufferedInputBuilder`).
  Register the test binary in `velox/ch/Disks/IO/tests/CMakeLists.txt` following
  the existing `*_buffered_input_test` pattern.

- [ ] **Step 4 — tests.** See Acceptance. Build and run; logs under
  `<velox_build_dir>`.

- [ ] **Step 5 — regression gates.** Rebuild and rerun the existing gates and
  both benchmarks (see Acceptance). All stay green / still build.

- [ ] **Step 6 — trunk-diff proof.** `git diff --stat` must show zero changes
  outside `velox/ch/`. Record the output in the receipt.

- [ ] **Step 7 — self-review.** Launch exactly one read-only code-review
  subagent over the full task diff. Fix actionable in-scope findings, rerun
  affected gates.

- [ ] **Step 8 — receipt.** Write the result file per EXECUTION_PROTOCOL format,
  set `worker_status`, stop.

## Acceptance

New test binary `velox_ch_filecache_connector_test`
(`FileCacheBufferedInputBuilderTest`), every case with real assertions:

1. **FileCache selected.** Build a `FileCacheManager` with a default cache,
   `registerFileCacheBufferedInputBuilder(manager)`, then call
   `BufferedInputBuilder::getInstance()->create(...)` with a real `FileHandle`
   and `connectorQueryCtx->cache() == nullptr`. Assert the returned object is a
   `FileCacheBufferedInput` (e.g. `dynamic_cast` non-null) **and** that reading a
   region through it populates/hits our cache (drive the real read path — not a
   bare `new`; prove a cache segment appears or a second read serves from cache).
2. **Not-installed deployment keeps native.** Without calling
   `registerFileCacheBufferedInputBuilder`, `BufferedInputBuilder::getInstance()`
   returns the statically-registered `DefaultBufferInputBuilder`, so `create(...)`
   returns the native buffered input (not a `FileCacheBufferedInput`) and a read
   still succeeds. This proves non-FileCache deployments are unbroken and that
   "fallback" lives at the registration boundary. (If a prior test registered our
   builder in the same process, restore the default first.)
3. **Install-time validation (fail-fast).** Build a `FileCacheManager` with an
   **empty** `defaultCacheName` (allowed at create, so `hasDefault()` is false).
   Assert `registerFileCacheBufferedInputBuilder(manager)` **throws** (the
   `VELOX_CHECK(manager.hasDefault(), ...)` fires); assert it is NOT
   caught/swallowed and no builder is registered as a side effect.
4. **Mutual-exclusion guard.** With our builder installed (valid Manager) and
   `connectorQueryCtx->cache()` non-null, `create(...)` throws with
   `"cannot both be installed"`.
5. **RED requirement.** Neutralize the FileCache construction in `create()`
   (force it to return the native `createBufferedInput` result / a non-FileCache
   input instead of `FileCacheBufferedInput`); case-1's `dynamic_cast` +
   cache-hit assertion **must turn red**. This proves the test truly exercises
   the FileCache path (no false-green). Restore and reconfirm green.

Regression gates (must stay green, 0 failed / 0 skipped):

```text
velox_ch_filecache_e2e_test              17
velox_ch_filecache_buffered_input_test   19
velox_ch_filecache_manager_test          19
velox_ch_filecache_core_scc_test         47
velox_ch_filecache_observability_test    14
velox_ch_filecache_cancellation_test      5
```

Benchmarks must still build (no run required):

```text
velox_ch_filecache_seek_benchmark
velox_ch_filecache_wrapper_benchmark
```

Trunk-diff proof: `git diff --stat` shows changes only under `velox/ch/`.

`git diff --check` clean. No `-j`. Nothing staged/committed. Logs under
`<velox_build_dir>`.

## Notes / boundaries

- This unblocks the TPCH wrapper's `filecache` engine (parked in Task 015). A
  full SF100×22×3 TPCH run stays a manual, user-driven step; 018a only proves
  the wiring is connectable. Split hard-requirement for TPCH runs:
  `--num_splits_per_file=1` (default 10 causes false read amplification).
- If the worker reaches any unreviewed CH dependency or velox API whose behavior
  is not covered by an approved design/task, stop as `blocked` per the
  EXECUTION_PROTOCOL dependency gate — do not infer a mapping or add a shim.
- `FileCacheRequestContext` field names: confirm from
  `velox/ch/Disks/IO/FileCacheRequestContext.h` (the benchmark sets `queryId` +
  `userId`); use the real fields, do not invent.

## Result receipt

Write `port/task/result/018a-filecache-connector-integration-result.md` in the
EXECUTION_PROTOCOL worker-receipt format.
