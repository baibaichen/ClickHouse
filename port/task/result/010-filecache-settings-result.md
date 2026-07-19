# Task 010 Result: `FileCache` Settings — `FileCacheConfig` and `FileCacheSettingsLoader`

## Worker attempt 1

```text
worker_status: blocked
environment_profile: root-oss
task: 010
```

Blocked during the mandatory pre-implementation preflight, before any source
or CMake edit was made. The blocker is a declared-scope / declared-instruction
conflict in Step 7's CMake registration that cannot be resolved without either
modifying a file outside the task's declared scope (`velox/ch/Common/CMakeLists.txt`)
or registering sources/public headers differently from the literal Step 7
instructions. There is no approved amendment covering the CMake changes, so per
the execution protocol (Worker rule 4) and the dispatch instruction ("Do not
silently adapt or expand scope"), the worker stops here and requests a decision.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `096ba0c9ef8d68ca91ca62a7b15cf6a74bbc058a` | clean (ahead 2 of `baibaichen/filecache`; no uncommitted changes) |
| `/root/oss/clickhouse` | `ch-filecache` | `cfd2c0db173a75ccccfea964bc36be831736c14b` | clean (ahead 6 of `baibaichen/ch-filecache`; no uncommitted changes) |

Both baselines match the dispatch. Task 009 is the HEAD in each repository.
The result file `010-filecache-settings-result.md` was absent before this write.

## Files changed

```text
None. No implementation, test, or CMake file was created or modified.
The only file written is this receipt:
  /root/oss/clickhouse/port/task/result/010-filecache-settings-result.md
```

## Commands and outcomes

All preflight commands were read-only (inspection). No configure/build/test ran.

| Command purpose | Exit code | Log |
|---|---:|---|
| `git status/log` baselines in both repos | 0 | inline above |
| Read Task 010, `EXECUTION_PROTOCOL.md`, `ENVIRONMENT.md` | 0 | n/a (viewer) |
| Read designs 06 (settings), 01 (fwd), 01 (read-context) | 0 | n/a (viewer) |
| Read CH `FileCacheSettings.h` / `FileCacheSettings.cpp` | 0 | n/a (viewer) |
| Read Velox `Config.h`, `VeloxUtils.cmake`, both CMakeLists | 0 | n/a (viewer) |
| Confirm to-create files absent; grep `checkedAdd`, mono helpers | 0 | inline above |

## Mandatory preflight — derived contract (recorded for the redispatch)

The behavioral contract was fully derived from CH production source
(`src/Interpreters/FileCache/FileCacheSettings.{h,cpp}`) and the approved
designs. It is sound and does not itself block; only the CMake scope conflict
blocks. Recorded here so the redispatched worker does not repeat the derivation.

### `FileCacheConfig` fields / defaults (all from `FileCache_fwd.h` constants)

Matches CH `LIST_OF_FILE_CACHE_SETTINGS` and design 06 exactly:
`maxSize=0`, `maxElements=FILECACHE_DEFAULT_MAX_ELEMENTS`,
`maxFileSegmentSize=FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE`,
`boundaryAlignment=FILECACHE_DEFAULT_FILE_SEGMENT_ALIGNMENT`,
`reserveGranularity=FILECACHE_DEFAULT_RESERVE_GRANULARITY`,
`cacheOnWriteOperations=false`, `cachePolicy=SLRU`, `slruSizeRatio=0.6`,
`backgroundDownloadThreads=5`, `backgroundDownloadQueueSizeLimit=5000`,
`backgroundDownloadMaxFileSegmentSize=4 MiB`, `loadMetadataThreads=16`,
`loadMetadataAsynchronously=false`, free-space ratios `0.0` (disabled),
`keepFreeSpaceRemoveBatch=250`, `keepFreeSpaceEvictionThreads=1`,
`invalidatedEntriesCleanup{IntervalMs=10000,Threshold=1000,RemoveBatch=250}`,
`bypassCacheThreshold=256 MiB`, `overcommitEvictionEvictStep=10*1_MiB`,
`checkCacheProbability=0.001`, `idleClientTtlSec=7d`,
`idleClientCheckIntervalSec=0`, `idleClientEvictionThreads=4`.
`FileCacheSettings` is a type alias of `FileCacheConfig` (already asserted by
`LeafTypesTest.cpp:290`). CH `10 * 1_MiB` == `10ULL*1024*1024` (10485760).

### Config keys / presence / parsing

- Source: `config::ConfigBase::rawConfigsWithPrefix(prefix + ".")` returns the
  sub-map with the prefix stripped (`Config.h:124-127`), so keys compare against
  the kebab-case `kKnownKeys` set. Unknown key under the prefix -> reject.
- Presence uses `count(key) > 0` on the stripped map (equivalent to CH
  `.changed`); presence is required for `path`, `max-size`,
  `max-size-ratio-to-total-space` and must not leak into `FileCacheConfig`.
- Policy parsing is case-insensitive and accepts `lru/slru/lru_overcommit/
  slru_overcommit`; overcommit spellings parse then fail validation (no silent
  downgrade), per design 06.

### Path resolution / effective size

- CH `loadFromConfig` + `validate` (`FileCacheSettings.cpp:166-284`): missing
  path -> error; relative path -> `cachePathPrefix / path`; `lexically_normal`;
  require absolute. The `allowedCacheRoot` containment check is a reviewed
  Velox-port addition (design 06 "path resolution"), performed before any
  `create_directories` / `space()` side effect.
- Exactly one of `max-size` / `max-size-ratio-to-total-space`; explicit
  `max-size` must be `> 0`; ratio in `(0,1]`; ratio-derived
  `maxSize = floor(ratio * fs::space(path).capacity)`.
  `fs::space(path).capacity` is the exact equivalent of CH
  `statvfs.f_blocks * f_frsize`.
- Fail-fast enhancements over CH (design 06): `maxFileSegmentSize == 0` reject,
  ratio-derived `maxSize == 0` reject. First-phase unsupported rejections:
  `cacheOnWriteOperations == true`, `LRU_OVERCOMMIT` / `SLRU_OVERCOMMIT`.
  Non-zero checks: `loadMetadataThreads`, `keepFreeSpaceEvictionThreads`,
  `invalidatedEntriesCleanup{IntervalMs,Threshold,RemoveBatch}`,
  `idleClientEvictionThreads`, `overcommitEvictionEvictStep`;
  `boundaryAlignment <= maxFileSegmentSize`; `use-split-cache` + overcommit
  reject.

### Path-containment amendment (supersedes the body `std::mismatch` snippet)

The safe component-prefix loop from the top-of-file amendment must be used
instead of `std::mismatch(root.begin(), root.end(), resolved.begin())` (which
overruns `resolved` when it is shorter than `root`). Six RED cases required,
with real temp dirs/symlinks: exact-root accept; descendant accept; shorter
path reject without invalid iterator access; sibling `/cache-other` reject for
root `/cache`; `..`-escape reject after normalization; and a symlink that
resolves outside the root must be rejected (requires resolving symlinks via
`fs::weakly_canonical` for the containment comparison — pure `lexically_normal`
would wrongly accept a symlink escape, giving the behavioral RED against the old
unsafe logic). `cfg.path` stores the `lexically_normal` resolved string;
containment compares canonicalized forms. This part is ready to implement once
the CMake blocker is resolved.

### `FileCacheReadOptions` (from read-context design 01)

Plain request-scoped value type owned by the caller (not shared): `tempCacheOnly=
false`, `readIfExistsOtherwiseBypass=false`, `allowBackgroundDownload=true`,
`allowBackgroundDownloadForMetadataFilesInPackedStorage=true`,
`allowBackgroundDownloadDuringFetch=true`, `preferBiggerBufferSize=true`,
`segmentsBatchSize=20`, `boundaryAlignment=nullopt`, `remoteFsBufferSize=0`,
`localFsBufferSize=0`, `reserveSpaceWaitLockTimeoutMs=0`,
`maxDownloadSizePerQuery=0`, `skipDownloadIfExceedsPerQueryCacheWriteLimit=true`,
`enableFilesystemCacheLog=false`.

### Dependency enumeration (all mapped, none unreviewed)

- `config::ConfigBase` — `velox/common/config/Config.h`: ctor
  `ConfigBase(unordered_map&&, bool=false)` (`Config.h:81-84`) matches the test's
  `make_shared<config::ConfigBase>(std::move(kv))`; `rawConfigsWithPrefix`,
  `valueExists`, `get<T>` present.
- `fs = std::filesystem` and `throwFileCacheExceptionFromFilesystemError` —
  `velox/ch/Common/FileCacheFilesystem.h`.
- `throwFileCacheException(fmt::format_string<Args...>, Args&&...)` ->
  `VELOX_FAIL` -> `VeloxRuntimeError` — `velox/ch/Common/FileCacheException.h`
  (matches every `EXPECT_THROW(..., VeloxRuntimeError)` in the task test).
- `FileCachePolicy { LRU, SLRU, SLRU_OVERCOMMIT, LRU_OVERCOMMIT }` and all
  `FILECACHE_DEFAULT_*` constants — `velox/ch/Interpreters/FileCache/FileCache_fwd.h`.
- `FileCacheUtils::checkedAdd(uint64_t, uint64_t, std::string_view)` exists
  (Task 008, `FileCacheUtils.h:58`); the Task 010 loader performs no size
  addition (ratio is a `double` multiply + `floor`), so no `checkedAdd` call is
  required, but any future size arithmetic must reuse it rather than raw `+`.
- Checked-arithmetic / policy / exception / filesystem semantics are all
  covered by accepted receipts 003-008; none is an unreviewed dependency.

## Blocker (first actionable problem, with evidence)

Step 7's CMake registration conflicts with both the task's declared file scope
and the current, accepted mono/non-mono CMake conventions (Tasks 003-009). It
cannot be executed literally, and correcting it requires an out-of-scope edit
and/or a registration different from the declared instructions — with no
approved amendment.

1. Step 7 (task lines 1132-1143) instructs:
   `target_sources(velox_ch_filecache PRIVATE ... FileCacheSettings.cpp PUBLIC
   FILE_SET HEADERS FILES FileCacheReadOptions.h)` in
   `velox/ch/Interpreters/FileCache/CMakeLists.txt`.
   The acceptance profile is `root-oss` with `VELOX_MONO_LIBRARY=ON`. In mono,
   `velox_ch_filecache` is an **alias** to the `velox` target and **cannot** be
   passed to `target_sources()` — documented at
   `velox/ch/Interpreters/FileCache/CMakeLists.txt:15-24` and enforced by
   `CMake/VeloxUtils.cmake:177-178, 264-270`. The working convention is
   `velox_sources(velox_ch_filecache PRIVATE FileCacheSettings.cpp)` for the
   source, and a `FILE_SET HEADERS` registration guarded by
   `if(NOT VELOX_MONO_LIBRARY)` with `BASE_DIRS ${PROJECT_SOURCE_DIR}` for the
   public headers (existing block at the same file, lines 36-52). Following
   Step 7 literally breaks the mono configure/build; following the convention
   is a registration **different from the declared instruction** (block
   trigger B) with no amendment.

2. Step 7 (task lines 1146-1158) also instructs: "add `velox_common_config` to
   the link dependencies in `velox/ch/Common/CMakeLists.txt`". That file is
   **not** in the task's declared Modify scope (task lines 125-128 list only
   `velox/ch/Interpreters/FileCache/CMakeLists.txt` and
   `.../tests/CMakeLists.txt`). `velox_ch_filecache` is defined in
   `velox/ch/Common/CMakeLists.txt:74-100`, and its link libraries are set there
   under `if(NOT VELOX_MONO_LIBRARY)` at lines 112-120. Because the new public
   header `FileCacheSettings.h` includes `velox/common/config/Config.h`,
   `velox_common_config` becomes a **PUBLIC** dependency of `velox_ch_filecache`;
   declaring it correctly for non-mono requires editing that out-of-scope file
   (block trigger A). The dispatch instruction names this file explicitly.

3. Additional stale form: Step 7 writes `velox_link_libraries(velox_ch_filecache
   PUBLIC velox_common_config ...)`, but the real file uses
   `target_link_libraries(velox_ch_filecache PUBLIC ...)` inside
   `if(NOT VELOX_MONO_LIBRARY)` — so even the helper name in Step 7 is stale.

4. The dispatch requires "Any public-interface CMake change needs mono/nonmono
   proof and reduced consumer." Making `FileCacheSettings.h` publicly include
   `Config.h` is exactly such a public-interface change, but the non-mono proof
   and reduced-consumer test cannot be produced without the out-of-scope
   `Common/CMakeLists.txt` link edit, and the acceptance environment builds only
   mono.

No approved amendment covers items 1-4 (the top-of-file amendment addresses only
the path-containment iterator logic). Per `EXECUTION_PROTOCOL.md` Worker rule 4
and the dispatch ("Do not silently adapt or expand scope"), the worker blocks
before implementation.

## Decision needed from the controller / user

Choose one and record it as a Task 010 amendment before redispatch:

- Option 1 (recommended — correct mono + non-mono, matches Tasks 003-009):
  amend Task 010 to (a) ADD `velox/ch/Common/CMakeLists.txt` to the declared
  Modify scope; (b) replace Step 7's `target_sources(velox_ch_filecache ...)`
  with `velox_sources(velox_ch_filecache PRIVATE FileCacheSettings.cpp)` in
  `velox/ch/Interpreters/FileCache/CMakeLists.txt`; (c) register
  `FileCacheSettings.h` and `FileCacheReadOptions.h` in the existing
  `if(NOT VELOX_MONO_LIBRARY)` `target_sources(... PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR} FILES ...)` block (alongside
  `FileCache_fwd.h`, `ShardedMap.h`, etc.); (d) add `velox_common_config` to the
  existing `if(NOT VELOX_MONO_LIBRARY) target_link_libraries(velox_ch_filecache
  PUBLIC ...)` block in `velox/ch/Common/CMakeLists.txt`; and (e) require a
  reduced-consumer link (the focused test links only `velox_ch_filecache` +
  GTest, as the Task 009 `velox_ch_sharded_map_test` does) plus a note that
  non-mono is validated separately.

- Option 2 (mono-only, minimal scope, defers non-mono): amend Task 010 to
  restrict registration to the two in-scope files — `velox_sources` for
  `FileCacheSettings.cpp` and the guarded `FILE_SET HEADERS` for the two headers
  in `velox/ch/Interpreters/FileCache/CMakeLists.txt`, with the focused test
  linking `velox_common_config` directly (as Step 2 already does) — and
  EXPLICITLY defer the non-mono `velox_ch_filecache` PUBLIC `velox_common_config`
  dependency to a later CMake task, accepting a knowingly-incomplete non-mono
  public interface until then.

Either option requires correcting Step 7's stale `target_sources` /
`velox_link_libraries` text; neither can be executed under the current literal
Step 7 without the deviations above.

## Acceptance evidence

```text
test count: n/a (blocked before implementation; no build/test run)
failed tests: n/a
skipped/disabled tests: n/a
benchmark result, when required: n/a
git diff --check: n/a (no changes staged or unstaged in either repository)
```

## Worker review

```text
review subagent: not launched
reason: the protocol's read-only review runs on the completed task-owned diff.
  This attempt produced no diff (blocked before any edit), so there is nothing
  to review. The review gate will run on the next attempt after the amendment.
findings: n/a
resolutions: n/a
unresolved findings: n/a
```

## Blockers

```text
Declared-scope / declared-instruction conflict in Task 010 Step 7 CMake
registration, with no approved amendment:

- Step 7's literal target_sources(velox_ch_filecache ...) breaks the mono
  acceptance build because velox_ch_filecache is an alias in mono
  (velox/ch/Interpreters/FileCache/CMakeLists.txt:15-24;
   CMake/VeloxUtils.cmake:177-178,264-270). Correct form is velox_sources(...),
  which differs from the declared instruction.
- FileCacheSettings.h publicly includes velox/common/config/Config.h, so
  velox_common_config is a PUBLIC dependency of velox_ch_filecache; its link
  libraries are declared only in velox/ch/Common/CMakeLists.txt:112-120, which
  is outside the task's declared Modify scope (task lines 125-128).

Awaiting a controller/user decision (Option 1 or Option 2 above) recorded as a
Task 010 amendment, then redispatch the same task from the source-contract
check.
```

## Recommended next task

```text
None. Do not start Task 011. Redispatch Task 010 after the CMake scope /
Step 7 instructions are amended per the decision above.
```

## Worker declaration

```text
Only Task 010 was attempted.
No implementation, test, or CMake file was created or modified in either
repository; both worktrees remain clean at their Task 009 baselines.
No changes were staged or committed.
The worker stopped immediately after writing this receipt.
```

## Controller unblock response 1

```text
controller_status: blocker_resolved
environment_profile: root-oss
task: 010
```

## Resolution

```text
root cause:
  Step 7 used target_sources on a mono alias and required a public ConfigBase
  dependency in Common/CMakeLists outside the original file scope.

decision:
  Adopt the existing Tasks 008/009 convention:
    velox_sources for FileCacheSettings.cpp;
    existing non-mono PUBLIC HEADERS file set for both public headers;
    PUBLIC velox_common_config on the non-mono real library;
    reduced focused consumer plus separate non-mono gate.

task or environment update:
  Updated the settings design and Task 010 with the expanded CMake scope,
  mono-safe registration, public dependency, reduced consumer, and non-mono
  proof contract.

evidence:
  This matches every accepted FileCache public header/source registration and
  avoids both the mono alias error and a knowingly incomplete non-mono interface.

redispatch same task: yes
```
