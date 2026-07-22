# Task 018 — Gluten / Velox-backend FileCache integration: findings (read-only)

Scope: map how Apache Gluten's Velox backend initializes Velox and wires the read path,
to plan integrating our ClickHouse-ported `FileCache` (already built in the Velox fork,
`velox/ch/`) as a THIRD buffered-input path. All `file:line` citations are in
`/home/chang/SourceCode/gluten1` unless noted `[velox-fork]`.

Repos:
- Gluten: `/home/chang/SourceCode/gluten1`, branch `main`, HEAD `23ed0c154`.
- Velox fork: `/home/chang/OpenSource/velox`, branch `filecache2`.

---

## (a) Feasibility gate — can Gluten consume our Velox fork?

**Yes.** Gluten already builds against a *local* Velox source tree via `VELOX_HOME`;
there is a first-class `--velox_home=` override.

- `dev/builddeps-veloxbe.sh:45` — `VELOX_HOME="$GLUTEN_DIR/ep/build-velox/build/velox_ep"` (default vendored checkout).
- `dev/builddeps-veloxbe.sh:130-133` — `--velox_home=*` arg overrides `VELOX_HOME`.
- `dev/builddeps-veloxbe.sh:188-190` — `VELOX_HOME` forwarded as `--velox_home=` to child scripts (`get-velox.sh`, `build-velox.sh`).
- `dev/builddeps-veloxbe.sh:227` — `get_velox` is only called (and thus `get-velox.sh` only fetches the vendored EP) when `[ ! -d "$VELOX_HOME" ]`. If `VELOX_HOME` already points at an existing tree (our fork), the fetch is skipped and Gluten builds directly against it.
- `dev/builddeps-veloxbe.sh:260` — `-DVELOX_HOME=$VELOX_HOME` passed to the Gluten C++ CMake, so headers/libs resolve from our fork too.

**Mechanism (one sentence):** Point Gluten at our fork by running
`./dev/builddeps-veloxbe.sh --velox_home=/home/chang/OpenSource/velox ...` (with the fork
checked out on `filecache2`); because the directory already exists, `get_velox`/`get-velox.sh`
is skipped and both the Velox build and the Gluten C++ build use our fork.

Caveat to verify at build time: our fork must be a superset of the Velox revision Gluten's
`main` expects (Gluten `main` normally pins a specific Velox SHA via `ep/build-velox`). Any
Velox-API drift between the pinned SHA and our fork surfaces here — see (c) and (f).

---

## (b) `VeloxBackend::init` lifecycle + insertion points

`cpp/velox/compute/VeloxBackend.cc`, `init` at `:125`. Ordered sequence:

1. `:128` build `backendConf_` (a `velox::config::ConfigBase` wrapping the Spark conf map).
2. `:131-143` glog init.
3. `:145` build `globalMemoryManager_` (`VeloxMemoryManager`).
4. `:148-150` register MemoryManager / ThreadManager / Runtime factories.
5. `:157-175` set Velox FLAGS from conf.
6. `:177` `hiveConnectorConfig_ = createHiveConnectorConfig(backendConf_)`.
7. `:180-194` register filesystems (local, HDFS/S3/GCS/ABFS under ifdefs).
8. `:217-241` compute task slots, spill executor, `ioExecutor_`.
9. `:243` `initJolFilesystem()`.
10. `:245-249` register file sinks, Parquet/ORC readers, subfield-filter parser.
11. **`:250` `BufferedInputBuilder::registerBuilder(std::make_shared<GlutenBufferedInputBuilder>())`** — the read-path builder registration. **This is the (i) builder-registration site.**
12. `:253-261` register Velox functions + serde.
13. `:263` `initUdf()`.
14. `:289-291` `initializeMemoryManager(options)` (global Velox memory manager).
15. **`:295` `initCache()`** — builds `AsyncDataCache` (mem-only or mem+SSD) *only if* `kVeloxCacheEnabled` (`initCache` body `:361-382`; `initSsdCache` `:326-359`).
16. `:297` register shuffle dictionary writer factory.

Backend-member / shutdown model to mirror:
- `asyncDataCache_` is a `VeloxBackend` member (`VeloxBackend.h:123`), built in `initCache`,
  torn down in `tearDown` (`:463-472`, `asyncDataCache_->shutdown()` at `:471`).
- Supporting members: `cacheAllocator_` (`.h:129`), `ssdCacheExecutor_` (`.h:128`),
  `cachePathPrefix_`/`cacheFilePrefix_` (`.h:132-133`).

**Where a `FileCacheManager` would be constructed and held:**
- Hold it as a new `VeloxBackend` member next to `asyncDataCache_` (`VeloxBackend.h:123`),
  e.g. `std::shared_ptr<facebook::velox::ch::FileCacheManager> fileCacheManager_;`
  (the fork's `FileCacheManager::create` returns `shared_ptr` — `[velox-fork] FileCacheManager.h:85`).
- Construct it inside a new `initFileCache()` helper (mirroring `initCache`), invoked from
  `init` **immediately after `:295 initCache()`** (both need the memory manager already
  initialized; the Manager owns its own worker pool / scheduler and does not depend on
  `AsyncDataCache`).
- Register the fork builder from that same helper via
  `facebook::velox::ch::registerFileCacheBufferedInputBuilder(*fileCacheManager_)`
  (`[velox-fork] FileCacheBufferedInputBuilder.cpp:68`). NOTE: this **replaces** the
  process-wide builder registered at `:250` (`BufferedInputBuilder::registerBuilder` installs a
  single global builder — last writer wins). See (g) for how to reconcile that with
  `GlutenBufferedInputBuilder`.
- Shut it down in `tearDown` (`:443-473`) alongside `asyncDataCache_->shutdown()`, calling
  `fileCacheManager_->shutdown()` and resetting the member. Manager MUST outlive the builder
  (`[velox-fork] FileCacheBufferedInputBuilder.h:35-36,70`), so shut down / reset it after the
  builder is no longer used.

`FileCacheManager::Options` fields to populate (`[velox-fork] FileCacheManager.h:75-83`):
`caches` (vector of `NamedFileCacheConfig{name, FileCacheConfig, configPath}`),
`defaultCacheName`, `commonUserId`, `localFileSystem`
(reuse `velox::filesystems::getFileSystem`/the local FS registered at `:180`),
`timekeeper` (optional), `initializeOnCreate` (default true). Lifecycle API:
`create` / `getInstance` / `instance` / `setInstance` / `getDefault` / `hasDefault` /
`initialize` / `shutdown` (`.h:85-104`). The Manager internally owns `workerPool_`,
`scheduler_`, `openedFileCache_`, `factory_` (`.h:136-139`) — Gluten does not wire those.

---

## (c) Builder-signature compatibility

**Match: YES — exact.** Both override the same virtual with identical parameter lists.

Gluten `GlutenBufferedInputBuilder::create` (`cpp/velox/memory/GlutenBufferedInputBuilder.h:29-36`):
```
create(const FileHandle&, const dwio::common::ReaderOptions&,
       const connector::ConnectorQueryCtx*, shared_ptr<io::IoStatistics>,
       shared_ptr<IoStats>, folly::Executor*,
       const folly::F14FastMap<std::string,std::string>& = {})
```
Fork `FileCacheBufferedInputBuilder::create` (`[velox-fork] FileCacheBufferedInputBuilder.h:45-52`):
identical parameter list (fork uses the `facebook::velox::ch` namespace so writes the types
unqualified, same underlying types). Both derive from
`velox::connector::hive::BufferedInputBuilder` and both `registerBuilder` through the same
static registry (`velox/connectors/hive/BufferedInputBuilder.h`).

Two-way branch in the existing Gluten builder (`GlutenBufferedInputBuilder.h:37-62`):
- `connectorQueryCtx->cache() != nullptr` → `CachedBufferedInput` (native AsyncDataCache path).
- else → `GlutenDirectBufferedInput` (direct path; `GlutenDirectBufferedInput.h`).

The fork builder asserts `connectorQueryCtx->cache() == nullptr`
(`[velox-fork] FileCacheBufferedInputBuilder.cpp:40-42`) — i.e. FileCache and AsyncDataCache
are mutually exclusive, matching the design that a third branch is chosen only when the
native cache is absent.

**No signature drift blocks reuse.** The only compatibility risk is *whole-Velox-API* drift
between Gluten `main`'s pinned Velox SHA and our fork (the `BufferedInputBuilder` base, the
`create` virtual, `FileHandle`, `ConnectorQueryCtx`, `ReaderOptions` shapes). Since our fork
descends from Velox, this is expected to be compatible but must be confirmed by an actual
build (see (e), (f)).

---

## (d) Config-key plan

`backendConf_` (`VeloxBackend.cc:128`) is a `velox::config::ConfigBase` over the Spark conf
map; typed reads via `backendConf_->get<T>(key, default)`. Cache keys are declared in
`cpp/velox/config/VeloxConfig.h` as `spark.gluten.sql.columnar.backend.velox.*` string
constants (e.g. `kVeloxCacheEnabled` `:126`; SSD keys `kVeloxSsdCacheSize` `:135`,
`kVeloxSsdCachePath` `:137`, `kVeloxSsdCacheShards` `:139`, etc.). Prefix constants:
`kDynamicBackendConfPrefix = "spark.gluten.sql.columnar.backend.velox."` (`:243`).

**Idiomatic add:** append new `const std::string` keys (+ typed defaults) in
`VeloxConfig.h`, following the SSD block, and read them in the new `initFileCache()`. Proposed:
- `spark.gluten.sql.columnar.backend.velox.fileCacheEnabled` (bool, default false) — on/off switch.
- `spark.gluten.sql.columnar.backend.velox.fileCachePath` (string) — cache root dir.
- `spark.gluten.sql.columnar.backend.velox.fileCacheSize` (uint64, bytes) — disk budget.
- (optional) name / commonUserId / shards keys mapping onto `FileCacheManager::Options`
  and `FileCacheConfig`. Confirm the exact `FileCacheConfig`/`FileCacheSettings` field names
  in the fork before finalizing (`[velox-fork] velox/ch/Interpreters/FileCache/FileCacheSettings.h`).

The switch is host-side native config only; no Scala change is strictly required to prove
routing (values can be passed through the existing `spark.gluten.*` conf plumbing).

---

## (e) Smallest build + E2E validation path

Build (native, Velox backend only):
```
./dev/builddeps-veloxbe.sh --velox_home=/home/chang/OpenSource/velox \
    --build_type=Release --run_setup_script=OFF
```
(`build_velox_backend` → `build_velox` + `build_gluten_cpp`, `builddeps-veloxbe.sh:292-298`;
Gluten C++ CMake at `:250-290`). Maven builds the Scala/JVM side separately.

Smallest E2E proof that a query routes through FileCache:
- Scala suite: `backends-velox/src/test/scala/org/apache/gluten/execution/VeloxTPCHSuite.scala`
  (a single Parquet TableScan query, e.g. TPC-H Q6/Q1, exercises the Hive-connector read path
  → the registered `BufferedInputBuilder`). Run with `fileCacheEnabled=true` and assert cache
  hit/populate via `FileCacheManager::refreshStats` (`[velox-fork] FileCacheManager.h:105`)
  or the Task 017 observability counters.
- Native C++ tests live under `cpp/velox/tests/` (built when `--build_tests=ON`); the fork's
  own FileCache unit/E2E tests (Task 015) already prove routing at the Velox layer, so the
  Gluten-side proof is the TPC-H scan above.

---

## (f) Open risks / unknowns

1. **Velox-API drift** between Gluten `main`'s pinned Velox SHA and our `filecache2` fork —
   the #1 build risk; only a real compile confirms the whole Velox surface (not just the
   builder virtual) matches. Gluten `main` normally fetches a pinned Velox via
   `ep/build-velox/src/get-velox.sh`; overriding `VELOX_HOME` bypasses that pin.
2. **Single global builder registry.** `BufferedInputBuilder::registerBuilder` installs ONE
   process-wide builder; registering the fork builder overrides `GlutenBufferedInputBuilder`
   (`:250`), losing Gluten's `CachedBufferedInput`/`GlutenDirectBufferedInput` branches. Must
   be reconciled (see (g)).
3. **Mutual exclusion with AsyncDataCache.** Fork builder hard-asserts
   `connectorQueryCtx->cache()==nullptr` (`FileCacheBufferedInputBuilder.cpp:40`). If both
   `kVeloxCacheEnabled` and `fileCacheEnabled` are on, reads would `VELOX_CHECK`-fail. Need a
   config guard that rejects enabling both.
4. **`GlutenDirectBufferedInput` cancellation semantics** (`GlutenDirectBufferedInput.h:49-69`)
   — Gluten added load-cancellation in the dtor to avoid IO after task teardown. The fork's
   `FileCacheBufferedInput` must not regress that behavior; verify its teardown path.
5. **Manager lifetime vs. builder.** Manager must outlive the builder and the process's last
   read; ordering of `shutdown` in `tearDown` vs. executor `.reset()` (`:456-460`) needs care.
6. **`FileCacheConfig`/`FileCacheSettings` exact fields** not yet read here — confirm before
   writing config-key mapping (d).
7. **Cudf/GPU path** (`createCudfHiveConnector`, ifdef `GLUTEN_ENABLE_GPU`) uses a different
   connector; FileCache routing there is out of scope for a first cut.

---

## (g) Recommended 018 shape — two options (NOT decided)

**Option A — extend `GlutenBufferedInputBuilder` with a 3rd branch.**
Keep the single builder registered at `:250`; add a `FileCacheManager*` (or a resolved
default `FileCachePtr`) to `GlutenBufferedInputBuilder`, and in `create` branch:
`ctx->cache()!=nullptr` → CachedBufferedInput; else if FileCache installed →
`FileCacheBufferedInput` (construct as in `[velox-fork] FileCacheBufferedInputBuilder.cpp:49-65`);
else → `GlutenDirectBufferedInput`.
- Pros: single builder, no registry-override problem (risk 2 gone); preserves Gluten's direct
  path + dtor cancellation (risk 4 handled) for the no-FileCache case; branch selection is
  per-read and explicit.
- Cons: Gluten's builder must `#include` fork FileCache headers and construct
  `FileCacheBufferedInput` directly, duplicating the fork's `create` body — tighter coupling
  to fork internals (`FileCacheRequestContext`, `FileCacheKey`, `FileCacheReadOptions`); the
  fork's `registerFileCacheBufferedInputBuilder` install helper goes unused.

**Option B — config-switch which builder to register.**
In `init`, register `GlutenBufferedInputBuilder` at `:250` as today, but if
`fileCacheEnabled`, instead call the fork's
`registerFileCacheBufferedInputBuilder(*fileCacheManager_)` from `initFileCache()` (after
`:295`), overriding the global builder.
- Pros: reuses the fork's install helper and `create` verbatim (least new Gluten code, no
  header duplication); clean separation — FileCache is a drop-in replacement builder.
- Cons: the fork builder has no direct/native-cache branch, so when FileCache is on, Gluten's
  `GlutenDirectBufferedInput` (and its teardown cancellation, risk 4) and the
  `CachedBufferedInput` branch are unavailable for that process; requires a hard config guard
  that `kVeloxCacheEnabled` and `fileCacheEnabled` are mutually exclusive (risk 3).

Tradeoff summary: A maximizes behavioral fidelity and per-read flexibility at the cost of
code duplication/coupling; B maximizes reuse and simplicity at the cost of losing Gluten's
direct-path niceties whenever FileCache is enabled. Decision deferred.

---

*Read-only pass. Nothing built, run, or modified. The single Gluten dirty entry
(`tools/gluten-it/spark-home/`, untracked) was not touched.*
