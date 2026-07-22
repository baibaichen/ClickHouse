# Tasks 016-019 Contract Review — `root-oss` Review 3

> **Post-review ownership amendment (2026-07-22):**
> `port/design/filecache-task-018-019-hard-split.md` supersedes this review's
> Task-018/019 ownership. Task 018 is Velox-only; former 018-E/F/G and all
> Gluten/Spark E2E work belong to Task 019.

## Status

```text
environment_profile: root-oss
review_scope: task contracts 016-019
clickhouse_head: f6c8e398abe
velox_head: aadc10db7bffbbc49ee9d7dcee1e01e78bbadfff
implementation_performed: false
gluten_modified: false
```

Tasks 003-015 are accepted. This review inspected future task contracts against
the accepted Velox implementation, authoritative ClickHouse source, and the
current Gluten checkout. It did not implement Task 016 or modify Gluten's dirty
worktree.

## Verdicts

| Task | Verdict | Blocking reason |
|---|---|---|
| 016 — write buffer to segment | **DEFERRED** | The corrected contract is ready, but Velox has no temporary-data spill consumer and the user marked it non-mainline. |
| 017A — statistics/cancellation/scheduler | **PLANNED; DESIGN APPROVED** | Supplies the global/query statistics and cancellation APIs required by Task 018. |
| 017B — logging/exception stacks | **PLANNED AFTER REVIEW 5** | Independent scope; Task 018 is followed by the Tasks 003–018 whole-port Review 5; accepted Review 5 gates Task 017B, which then gates Task 019 implementation/production readiness. |
| 018 — Velox correctness and benchmarks | **PLANNED; DESIGN APPROVED** | The complete Velox correctness/micro/wrapper/TPCH benchmark suite consumes accepted Task-017A APIs. |
| 019 — Gluten integration and Spark E2E | **REVISE; dependency-blocked** | Accepted Task 018, Review 5, and Task 017B are prerequisites; the post-review hard-split design and rewritten Task-019 plan supersede this review's former scope. |

Task 016 is deferred. User-selected order is Task 017A, Task 018, then Task
017B. No implementation is authorized.

The Task-017A/018 design records real kernel `O_DIRECT`
integration test before performance claims or Gluten rollout. The existing
strict direct-IO mocks are logic coverage only. The user subsequently deferred
this non-mainline gate, so it does not block Tasks 017A/018.

## Task 016 follow-up resolution

Task 016 was rewritten after this review and independently re-reviewed as
technically ready. The user then explicitly deferred it because Velox has no
temporary-data spill consumer and the feature is not on the mainline. The
corrected contract:

- records ClickHouse `#40893`, `#43972`, and `#48664`;
- states explicitly that Velox has no production `TemporaryDataOnDisk`
  consumer;
- adds synthetic `TemporaryDataOnDiskEquivalentLifecycle` and
  `HttpTemporaryBufferEquivalentReadBack` scenarios without wiring a fake
  spill/HTTP subsystem;
- uses the accepted `WriteBufferFromFileBase` and pool-backed buffer state;
- removes the obsolete `BufferStateSwapGuard` requirement because the Velox
  local cache writer is external-only and is detached after every
  `FileSegment::write`;
- uses `velox_sources`, a non-mono header `FILE_SET`, focused mono/non-mono and
  accumulated gates, and an eleven-row behavioral mutation matrix.

The contract is preserved for a future real consumer, but
`task_016_allowed: false` and `disposition: deferred` are binding.

## Task 017A/017B findings

### User disposition

```text
decision: do Task 017A and Task 017B
next_action: rewrite the split task contracts from the approved joint design
implementation_authorized: false
co_design_with: Task 017A and Task 018 statistics/benchmarks
```

### Critical

1. **The proposed `CurrentMetrics` replacement truncates accepted names.**
   Existing production uses eleven names, including
   `FilesystemCacheElements`, `FilesystemCacheInvalidatedElements`,
   `FilesystemCachePriorityQueueElements`, `FilesystemCacheSize`, and
   `FilesystemCacheKeys`. Keep every existing enumerator and append only a
   storage sentinel.
2. **The proposed `ProfileEvents` replacement truncates accepted names.**
   Existing production and
   `ProfileEventsCoverageTest.AllRequiredEventNamesCompile` use the full event
   set. Keep all existing events and append only a storage sentinel.
3. **The new `tryLogCurrentException(LoggerPtr, ...)` shape breaks existing
   `__PRETTY_FUNCTION__` callers.** Preserve/add the string-name overload as
   well as the logger overload.

### Important

- Close the explicit Task-014 statistics wiring debt:
  `FileCacheBufferedInput` stores `IoStatistics`/`IoStats`, but
  `FileCacheInputStream` never updates them, and the three CH read-path byte
  events have no call sites in Velox.
- Close the Task-014 cancellation wiring debt: Task 012's
  `FileSegment::wait` accepts and checks a token, but Task 014 hard-coded an
  empty token instead of forwarding one through the Velox consumer API.
- Add `FileCacheBufferedInput.h/.cpp` and the Task-015 shared test helper to file
  scope if the constructor changes.
- Store a copied `folly::CancellationToken`, not a raw
  `ConnectorQueryCtx *`, in the buffered input. This avoids a lifetime hazard
  and a heavyweight public-header dependency.
- Review-2 explicitly deferred F-CALLERID and scheduler recursive-mutex
  resolution to Task 017A, but the old task omitted both. Add the work and evidence or
  record a new user decision moving it elsewhere.
- Add the full accumulated `ctest -R '^velox_ch_'` gate; changing compiled
  metric/event storage affects every linked target.
- Task 016 is not a code prerequisite for Task 017A/017B. If sequencing is desired,
  state it as policy rather than a nonexistent dependency.
- Preserve logger attribution in warning/error macros.

### Split resolution

The approved design is:

```text
Task 017A:
  CurrentMetrics/ProfileEvents, global snapshot, IoStatistics/IoStats wiring,
  cancellation, caller identity, scheduler two-lock parity.

Task 017B:
  logger laziness/attribution, current exception formatting, optional Velox
  exception stacks, logger/name overloads.

Task 018 depends only on Task 017A. After Task 018, dispatch stops for Review 5
over Tasks 003–018. Task 017B runs after accepted Review 5 and must be accepted
before Task 019 implementation.
```

## Task 018 findings

### User disposition

```text
decision: do Task 018
scope_addition: adapt CacheVerify, core/buffered-input/wrapper microbenchmarks,
  and TPCH from baibaichen/ch-filecache
co_design_with: Task 017A statistics/cancellation
implementation_authorized: false
```

### Critical

1. **The fixture lacks Velox runtime initialization.**
   `memoryManager()->addLeafPool` and `getFileSystem` require memory-manager and
   filesystem setup, which the prescribed fixture omits
   (`018-filecache-gluten-integration.md:211-219`). Add an explicit suite setup
   or use the accepted default-leaf-pool fixture pattern plus
   `registerLocalFileSystem`.
2. **The `root-oss` Gluten build directory does not exist.**
   Step 1 says to reconfigure an existing cache and omits the vcpkg toolchain
   (`:115-143`). Specify a complete first configure using
   `<vcpkg_toolchain>`, build type, and required Gluten options.
3. **`BuilderSelectsFileCacheInputWhenManagerInstalled` proves only that the
   singleton is non-null** (`:292-307`). It must call the real builder and
   assert that the returned input is a `FileCacheBufferedInput`.

### Important

- Use `FileCacheFileIdentity::deriveKey` for both empty and non-empty etags;
  do not duplicate path/SipHash logic in Gluten.
- Store `auto cache = manager->getDefault()` once. The current proposal takes
  `.get()` from a temporary and calls `getDefault` again (`:550-571`).
- Use request-context weight or a named policy constant instead of a literal
  origin weight.
- Do not add the already-present `FileSystems.h` include to `VeloxBackend.cc`.

## Task 019 findings

### Dependency block

Task 019 cannot start before a corrected Task 018 is accepted. Tasks 016 and
017 are not prerequisites for either Gluten task; Task 015 provides the needed
Velox read path.

### Important

- The proposed teardown test cannot observe
  `FileCacheManager::shutdown` relative to executor teardown without a hook.
  Either add deterministic lifecycle instrumentation or narrow the assertion
  to the provable postcondition: the singleton is null and the retained manager
  rejects access after backend teardown.
- Specify suite initialization for real `ConnectorQueryCtx`, memory-pool,
  filesystem, connector, and optional `AsyncDataCache` construction.
- Rephrase the `ep/build-velox` guard as a post-Task-018 CMake-cache
  verification; no Gluten build cache exists before the first configure.

## Cross-task boundaries

- **016 vs 017:** Task 016 owns the Ephemeral writer; Task 017A owns statistics
  and read-path cancellation; Task 017B owns logging/exception stacks. Writer
  cancellation requires a separate explicit decision.
- **018 vs 019:** Task 018 is Velox-only and owns correctness and benchmarks.
  Task 019 owns config, manager lifecycle, builder selection, the metric bridge,
  and native/Spark E2E. The hard-split design is the binding decomposition.
- **Velox vs Gluten:** Tasks 018-019 consume the accepted Task-015 public API.
  They must not reopen the FileCache core or modify the existing dirty Gluten
  worktree until their corrected contracts are approved.

## Required authoring wave before implementation

1. Leave Task 016 deferred until a real Velox spill consumer and shared-disk
   pressure requirement exist.
2. Rewrite Task 017A from the approved joint design; rewrite independent Task
   017B for logging/exception stacks.
3. Replace Task 018's configure/fixture/builder-test instructions with runnable
   `root-oss` commands, real builder assertions, and the adapted Velox
   correctness/micro/wrapper/TPCH suite using the Task-017A statistics API.
4. Make Task 019's fixture and lifecycle evidence deterministic, then keep it
   blocked until Task 018 is accepted.

```text
task_016_allowed: false
task_017_allowed: false
task_018_allowed: false
task_019_allowed: false
```
