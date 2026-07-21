# Tasks 016-019 Contract Review — `root-oss` Review 3

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
| 017 — observability/cancellation | **PLANNED; REVISE FIRST** | The user marked it mainline work, but the current shim/cancellation contract remains unsafe and must be redesigned before implementation. |
| 018 — Gluten integration | **REVISE** | The prescribed fixture/configure path cannot run on `root-oss`, and the builder-selection test is false-green. |
| 019 — Gluten E2E | **REVISE; dependency-blocked** | Task 018 must be accepted first; lifecycle ordering and real fixture setup are under-specified. |

Task 016 is deferred. Task 017 is selected for later design. No later
implementation is authorized.

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

## Task 017 findings

### User disposition

```text
decision: do Task 017
next_action: redesign the contract later
implementation_authorized: false
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

- Add `FileCacheBufferedInput.h/.cpp` and the Task-015 shared test helper to file
  scope if the constructor changes.
- Store a copied `folly::CancellationToken`, not a raw
  `ConnectorQueryCtx *`, in the buffered input. This avoids a lifetime hazard
  and a heavyweight public-header dependency.
- Review-2 explicitly deferred F-CALLERID and scheduler recursive-mutex
  resolution to Task 017, but the task omits both. Add the work and evidence or
  record a new user decision moving it elsewhere.
- Add the full accumulated `ctest -R '^velox_ch_'` gate; changing compiled
  metric/event storage affects every linked target.
- Task 016 is not a code prerequisite for Task 017. If sequencing is desired,
  state it as policy rather than a nonexistent dependency.
- Preserve logger attribution in warning/error macros.

## Task 018 findings

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

- **016 vs 017:** Task 016 owns the Ephemeral writer; Task 017 owns real
  observability and read-path cancellation. Writer cancellation requires a
  separate explicit decision.
- **018 vs 019:** Task 018 owns config, manager lifecycle wiring, and builder
  type selection. Task 019 owns real host-path miss/fill/hit and teardown E2E.
- **Velox vs Gluten:** Tasks 018-019 consume the accepted Task-015 public API.
  They must not reopen the FileCache core or modify the existing dirty Gluten
  worktree until their corrected contracts are approved.

## Required authoring wave before implementation

1. Leave Task 016 deferred until a real Velox spill consumer and shared-disk
   pressure requirement exist.
2. Rewrite Task 017's shim replacements as additive storage implementations,
   choose the cancellation-token ownership model, and resolve its deferred
   F-CALLERID/recursive-mutex obligations.
3. Replace Task 018's configure/fixture/builder-test instructions with runnable
   `root-oss` commands and real builder assertions.
4. Make Task 019's fixture and lifecycle evidence deterministic, then keep it
   blocked until Task 018 is accepted.

```text
task_016_allowed: false
task_017_allowed: false
task_018_allowed: false
task_019_allowed: false
```
