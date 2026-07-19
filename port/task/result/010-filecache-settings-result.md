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

## Worker attempt 2

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 010
```

Executed under the controller amendment (mono/non-mono registration). Preflight
confirmed the amended scope resolves both attempt-1 CMake blockers and that no
other unreviewed dependency remains, so implementation proceeded. TDD-first:
missing-header RED, then a genuine behavioral containment RED against an
old-unsafe string-prefix lexical-only check, then the safe canonicalizing
component-prefix loop to GREEN.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `096ba0c9ef8d68ca91ca62a7b15cf6a74bbc058a` | clean (Task 009 HEAD) |
| `/root/oss/clickhouse` | `ch-filecache` | `212ed62e7520844a0da00496148d50802a83ec89` | clean (Task 010 CMake-scope amendment HEAD) |

All four target Velox files were absent before this attempt. Velox HEAD is
unchanged (no commit); changes are unstaged.

## Preflight (amended-scope confirmation)

```text
- Controller amendment adds velox/ch/Common/CMakeLists.txt to Modify scope and
  supersedes literal Step 7: source via velox_sources(velox_ch_filecache
  PRIVATE FileCacheSettings.cpp); both public headers appended to the existing
  non-mono PUBLIC FILE_SET HEADERS in FileCache/CMakeLists.txt; velox_common_config
  added PUBLIC to the non-mono target_link_libraries(velox_ch_filecache ...) in
  Common/CMakeLists.txt; focused test links only velox_ch_filecache + GTest
  (reduced consumer); mono + separate full non-mono build required. This resolves
  both attempt-1 blockers (mono-alias target_sources error; out-of-scope Common
  CMake edit).
- No unreviewed dependency: config::ConfigBase (Config.h, reviewed), fs +
  throwFileCacheExceptionFromFilesystemError (Task 003), throwFileCacheException
  -> VELOX_FAIL -> VeloxRuntimeError (Task 003), FileCachePolicy + FILECACHE_DEFAULT_*
  (Task 008 fwd), FileCacheUtils::checkedAdd (Task 008; loader performs no size
  addition, so none used and none duplicated). The pre-execution amendment's
  canonicalizing component-prefix containment is the reviewed contract.
```

## Files changed

```text
/root/oss/velox/velox/ch/Common/CMakeLists.txt                                 (M: velox_common_config PUBLIC, non-mono)
/root/oss/velox/velox/ch/Interpreters/FileCache/CMakeLists.txt                 (M: velox_sources + 2 public headers in non-mono FILE_SET)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt           (M: velox_ch_settings_test, reduced consumer)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheSettings.h            (new)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheSettings.cpp          (new)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheReadOptions.h         (new)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/FileCacheSettingsTest.cpp (new)
/root/oss/clickhouse/port/task/result/010-filecache-settings-result.md         (this receipt)
```

Exactly the 7 declared Velox files plus the receipt. No Task 011 file, no
out-of-scope file.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure mono (`VELOX_MONO_LIBRARY=ON`) | 0 | `/root/oss/velox/_build/debug/configure_task_010_settings.log` |
| RED build (missing headers) — expected fail | 1 (expected) | `/root/oss/velox/_build/debug/build_task_010_red.log` |
| reconfigure mono after CMake source registration | 0 | `/root/oss/velox/_build/debug/configure_task_010_containment_red.log` |
| behavioral-RED build (old-unsafe containment) | 0 | `/root/oss/velox/_build/debug/build_task_010_containment_red.log` |
| behavioral-RED test (containment fails) | 8 (expected) | `/root/oss/velox/_build/debug/test_task_010_containment_red.log` |
| GREEN build mono (final, safe containment) | 0 | `/root/oss/velox/_build/debug/build_task_010_settings.log` |
| GREEN test mono (ctest) | 0 | `/root/oss/velox/_build/debug/test_task_010_settings.log` |
| GREEN test mono (direct gtest, 54 tests) | 0 | `/root/oss/velox/_build/debug/test_task_010_settings_direct.log` |
| accumulated Tasks 003-010 regression build | 0 | `/root/oss/velox/_build/debug/build_task_010_regression.log` |
| accumulated Tasks 003-010 regression (8 tests) | 0 | `/root/oss/velox/_build/debug/test_task_010_regression.log` |
| configure non-mono (`VELOX_MONO_LIBRARY=OFF`) | 0 | `/root/oss/velox/_build/debug-task010-nonmono/configure_task_010_settings_nonmono.log` |
| non-mono reduced-consumer build | 0 | `/root/oss/velox/_build/debug-task010-nonmono/build_task_010_settings_nonmono.log` |
| non-mono reduced-consumer test (54 tests) | 0 | `/root/oss/velox/_build/debug-task010-nonmono/test_task_010_settings_nonmono.log` |

All configure/build/test commands sourced `/root/oss/velox-helper/env.sh` and used
the `root-oss` effective configuration; no `-j`, no `build.sh` as evidence.

## Acceptance evidence

```text
test count: 54 gtest cases in 4 suites (mono and non-mono identical)
failed tests: 0 (final GREEN, mono and non-mono)
skipped/disabled tests: 0
accumulated regression: 8/8 focused tests pass
  (velox_ch_common/guards/threadpool/scheduler/io/leaf_types/sharded_map/settings)
RED evidence:
  - missing-header RED: build_task_010_red.log — "FileCacheSettings.h: No such file or directory".
  - genuine behavioral containment RED (build_task_010_containment_red.log build 0;
    test_task_010_containment_red.log ctest 8): against a string-prefix lexical-only
    containment ONLY FileCacheContainmentTest.SiblingPrefixRejected and
    SymlinkEscapeRejected failed ("it throws nothing"); the exact-root, descendant,
    shorter-path, dot-dot-escape and side-effect cases passed even against the unsafe
    impl. The safe weakly_canonical component-prefix loop turns all six GREEN.
six mandatory containment cases (real temp dirs + real symlink escape): all pass —
  ExactRootAccepted, DescendantAccepted, ShorterPathRejectedSafely,
  SiblingPrefixRejected, DotDotEscapeRejected, SymlinkEscapeRejected; plus
  OutsideRootRatioCreatesNoSideEffect proves path authorization precedes create/space.
benchmark result, when required: n/a
git diff --check: clean (no whitespace errors); only the 7 declared Velox files changed.
```

## Worker review

```text
review subagent: one read-only code-review agent over the complete task-owned diff
  (config/path/symlink/side-effects/math/CMake/tests), given the task, designs, CH
  source of truth, and test outcomes.
findings: no blocker/major. Two low-severity latent findings in owned logic:
  (1) containment mishandled a not-yet-existing allowedCacheRoot given with a
      trailing separator (false rejection of a valid descendant) and an empty root
      (fail-open acceptance of every path);
  (2) scalar parsing via std::stoull/std::stod silently accepted a negative
      wrap-around and trailing garbage.
resolutions:
  (1) allowed-root check now rejects an empty/relative allowedCacheRoot (fail closed)
      and drops a trailing empty component on both canonicalized paths before the
      component-prefix loop. New RED-first tests:
      TrailingSlashNonExistentRootAcceptsDescendant, EmptyAllowedRootRejected,
      RelativeAllowedRootRejected.
  (2) integers parse with std::from_chars (rejects sign/trailing/overflow); doubles
      reject trailing characters via the stod position. New tests: NegativeMaxSizeRejected,
      TrailingGarbageInIntegerRejected, TrailingGarbageInDoubleRejected. Ratio-range
      validation is unchanged and no new ratio normalization was added (per design 06).
  Test count grew 48 -> 54; mono + non-mono + regression re-run GREEN after the fixes.
unresolved findings: none.
```

## Blockers

```text
None.
```

## Recommended next task

```text
None to start. Per EXECUTION_PROTOCOL, after Task 010 acceptance the controller
runs the mandatory whole-port Tasks 003-010 source-contract review before any
Task 011 dispatch.
```

## Worker declaration

```text
Only Task 010 was attempted (attempt 2).
Changes are unstaged and uncommitted in both repositories.
The 7 declared Velox files plus this receipt are the only changes; no Task 011
file and no out-of-scope file was touched.
The worker stopped immediately after writing this receipt.
```

## Controller review 1

```text
controller_status: changes_requested
environment_profile: root-oss
task: 010
worker_attempt_reviewed: 2
```

## Review evidence

```text
scope and CMake review:
  Attempt 2 changed exactly the seven amended Task-010 Velox files and appended
  this receipt. Mono-safe source registration, both public headers, PUBLIC
  velox_common_config, and the reduced non-mono consumer follow the accepted
  CMake convention and pass in both modes.

implementation review:
  Field/default/key mapping is a correct 1:1 bijection. Path normalization,
  weak canonicalization, component-prefix containment, shorter-path safety,
  sibling and dot-dot rejection, symlink escape rejection, and authorization
  before create/space side effects match the approved contract.

  max-size-ratio-to-total-space still accepts NaN. std::stod parses "nan"; both
  ordered range comparisons are false, then floor(NaN) is cast to uint64_t.
  That conversion is undefined behavior and can either produce a misleading
  zero error or silently return a huge garbage maxSize depending on build mode.

test and evidence review:
  Worker logs prove 54/54 focused tests in mono/non-mono, zero disabled/skipped,
  the containment RED, and Task 003-010 regression 8/8. No test covers NaN.

  Fifteen loader key/field mappings have only default-struct assertions and are
  never parsed with non-default values. The current mappings are mechanically
  correct, but a key/field swap in any of them would leave the suite green.

independent review:
  A fresh read-only Controller review confirmed the NaN UB and mechanically
  verified all current mappings. It found no other Blocker or Major defect.

unresolved findings:
  1. Non-finite max-size ratio can bypass validation and reach UB.
  2. Fifteen loader mappings lack direct parse round-trip evidence.
```

## Required changes

```text
1. Reject non-finite max-size-ratio-to-total-space before side effects and
   numeric derivation; add genuine RED for NaN and infinity.
2. Add a valid consolidated/parameterized non-default parse test covering all
   fifteen currently unexercised key/field pairs.
3. Capture a behavioral mutation proof for a previously uncovered mapping.
4. Rerun mono/non-mono focused gates and Task 003-010 regression; launch one
   fresh read-only review and append Worker attempt 3.
```

## Commits

No implementation or acceptance commit was created.

## Worker attempt 3

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 010
```

Executed under the controller amendment (finite-ratio guard + complete parsing
evidence). TDD-first: NaN/+inf/-inf RED tests added before the production fix;
NaN was a genuine RED (no throw, directory created). ParseUncoveredKeys
consolidated test added; mutation proof captured. Production fix applied:
`!std::isfinite` guard precedes ordered comparisons. Review finding resolved:
boolean bool-swap symmetry broken by alternating true/false values.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `096ba0c9ef8d68ca91ca62a7b15cf6a74bbc058a` | exactly seven attempt-2 files dirty (M CMakeLists ×3; ?? FileCacheSettings.h/.cpp, FileCacheReadOptions.h, FileCacheSettingsTest.cpp) |
| `/root/oss/clickhouse` | `ch-filecache` | `fffc6280c56` | clean |

## Files changed

```text
/root/oss/velox/velox/ch/Common/CMakeLists.txt                                 (M, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/CMakeLists.txt                 (M, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt           (M, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheSettings.h            (??, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheSettings.cpp          (??, one-line fix: !std::isfinite added to ratio range guard)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheReadOptions.h         (??, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/FileCacheSettingsTest.cpp (??, +4 new tests: NonFiniteRatioNaN/PosInf/NegInf + ParseUncoveredKeys)
/root/oss/clickhouse/port/task/result/010-filecache-settings-result.md         (this receipt)
```

Exactly the 7 declared Velox files plus the receipt. No other file touched.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure mono (attempt3 re-configure) | 0 | `/root/oss/velox/_build/debug/configure_task_010_attempt3_red.log` |
| RED build (NaN/ParseUncovered tests, pre-fix) | 0 | `/root/oss/velox/_build/debug/build_task_010_attempt3_red.log` |
| RED direct test (NaN fails, others pass) | 1 (expected) | `/root/oss/velox/_build/debug/test_task_010_attempt3_nonfinite_red.log` |
| mutation build (idle-client-ttl-sec misrouted) | 0 | `/root/oss/velox/_build/debug/build_task_010_attempt3_mutation.log` |
| mutation test (ParseUncoveredKeys fails) | 1 (expected) | `/root/oss/velox/_build/debug/test_task_010_attempt3_mutation.log` |
| mutation restored; bool-symmetry fix applied; final GREEN build | 0 | `/root/oss/velox/_build/debug/build_task_010_attempt3_green2.log` |
| GREEN direct test (58 tests) | 0 | `/root/oss/velox/_build/debug/test_task_010_attempt3_settings_direct.log` |
| mutation re-verified with bool-asymmetric test | 1 (expected) | `/root/oss/velox/_build/debug/test_task_010_attempt3_mutation2.log` |
| mutation restored; final production build | 0 | `/root/oss/velox/_build/debug/build_task_010_attempt3_final.log` |
| focused CTest mono (velox_ch_settings_test) | 0 | `/root/oss/velox/_build/debug/test_task_010_attempt3_settings_ctest.log` |
| accumulated Tasks 003-010 regression (10 tests) | 0 | `/root/oss/velox/_build/debug/test_task_010_attempt3_regression.log` |
| configure non-mono | 0 | `/root/oss/velox/_build/debug-task010-nonmono/configure_task_010_attempt3_nonmono.log` |
| non-mono build | 0 | `/root/oss/velox/_build/debug-task010-nonmono/build_task_010_attempt3_nonmono.log` |
| non-mono direct test (58 tests) | 0 | `/root/oss/velox/_build/debug-task010-nonmono/test_task_010_attempt3_nonmono_direct.log` |
| non-mono CTest discovery | 0 | `/root/oss/velox/_build/debug-task010-nonmono/test_task_010_attempt3_nonmono_ctest.log` |

## Acceptance evidence

```text
test count: 58 gtest cases (mono and non-mono identical; +4 vs attempt 2)
failed tests: 0 (final GREEN, mono and non-mono)
skipped/disabled tests: 0
accumulated regression: 10/10 CTest pass
  (velox_ch_common, velox_ch_chassert_release_probe, velox_ch_chassert_sanitizer_gate_test,
   velox_ch_threadpool, velox_ch_scheduler, velox_ch_guards, velox_ch_leaf_types,
   velox_ch_sharded_map, velox_ch_settings, velox_ch_io)

RED evidence:
  - NonFiniteRatioNaN: genuinely RED pre-fix — test_task_010_attempt3_nonfinite_red.log
    shows "it throws nothing" AND "Actual: true / Expected: false" (directory created).
    +inf and -inf pass GREEN even pre-fix (caught by existing ordered comparisons).
  - Mutation proof (idle-client-ttl-sec → idleClientCheckIntervalSec misrouted):
    test_task_010_attempt3_mutation.log shows
    "r.idleClientTtlSec which is: 604800 / 3'600u which is: 3600" — FAILED.
    Same mutation confirmed RED after bool-asymmetry fix:
    test_task_010_attempt3_mutation2.log identical failure.

ParseUncoveredKeys covers all 15 controller-listed keys with exact field assertions:
  allow-dynamic-cache-resize → allowDynamicCacheResize (true)
  background-download-max-file-segment-size → backgroundDownloadMaxFileSegmentSize (8388608)
  cache-hits-threshold → cacheHitsThreshold (42)
  check-cache-probability → checkCacheProbability (0.05)
  dynamic-resize-lock-wait-ms → dynamicResizeLockWaitMs (2500)
  enable-filesystem-query-cache-limit → enableFilesystemQueryCacheLimit (false — breaks bool-swap symmetry)
  expose-prometheus-eviction-metrics → exposePrometheusEvictionMetrics (true)
  expose-prometheus-eviction-metrics-per-user → exposePrometheusEvictionMetricsPerUser (false)
  idle-client-check-interval-sec → idleClientCheckIntervalSec (300)
  idle-client-ttl-sec → idleClientTtlSec (3600)
  keep-free-space-elements-ratio → keepFreeSpaceElementsRatio (0.15)
  keep-free-space-remove-batch → keepFreeSpaceRemoveBatch (500)
  skip-cache-on-disk-failure → skipCacheOnDiskFailure (true)
  split-cache-ratio → splitCacheRatio (0.3)
  write-cache-per-user-id-directory → writeCachePerUserIdDirectory (false)

git diff --check: clean (no whitespace errors); only the 7 declared Velox files changed.
```

## Worker review

```text
review subagent: one read-only code-review subagent launched on the complete
  attempt-3 diff (production fix + 4 new tests + full attempt-2 context).
findings:
  1. (medium) ParseUncoveredKeys: 6 all-"true" booleans were symmetric — a swap
     between any two would be invisible (both fields end up true). The review
     correctly identified that the comment's claim was false for intra-group swaps.
resolutions:
  1. Bool-swap symmetry broken by alternating true/false values: 3 booleans set to
     "true" (allowDynamicCacheResize, exposePrometheusEvictionMetrics,
     skipCacheOnDiskFailure) and 3 set to "false" (enableFilesystemQueryCacheLimit,
     exposePrometheusEvictionMetricsPerUser, writeCachePerUserIdDirectory).
     Any swap between a true-keyed and false-keyed field is now detectable.
     Mutation proof re-verified GREEN after fix.
unresolved findings: none.
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 010 was attempted (attempt 3).
Changes are unstaged and uncommitted in both repositories.
The 7 declared Velox files plus this receipt are the only changes;
no Task 011 file and no out-of-scope file was touched.
The worker stopped immediately after writing this receipt.
```

## Controller review 2

```text
controller_status: changes_requested
environment_profile: root-oss
task: 010
worker_attempt_reviewed: 3
```

## Review evidence

```text
implementation review:
  The isfinite guard now rejects NaN before directory/space/floor/conversion and
  closes the UB. All production key/field mappings are mechanically correct.
  Path containment, validations, read options, and CMake interfaces remain
  correct.

test evidence review:
  Attempt 3 adds all fifteen keys to ParseUncoveredKeys, but three booleans are
  set to false, equal to their struct defaults. Deleting or misrouting any of
  those assignments to another false field leaves the test green.

  More generally, the 3-true/3-false grouping cannot prove six boolean mappings:
  swaps inside either same-valued group are invisible. The numeric mutation
  proof exercises no boolean assignment. Therefore the amendment's claim that
  every mapping has non-default direct evidence is not met.

log review:
  NaN RED, final 58/58 mono/non-mono, and the regression gate are otherwise
  green. The newest regression contains 10 CTest entries; the older 8/8 count
  belongs to attempt 2.

independent review:
  A fresh read-only Controller review confirmed the false-green boolean mapping
  gap and found no production defect or other Blocker/Major issue.

unresolved findings:
  Six boolean key/field mappings are not independently distinguishable.
```

## Required changes

```text
1. Replace grouped boolean assertions with one-hot loader cases. For each of the
   six keys, set only it to true, assert only its field is true, and assert the
   other five remain false.
2. Keep non-boolean fields at distinct non-default values.
3. Capture a bool-mapping mutation (route one key into another field) and prove
   the one-hot cases fail; preserve the numeric mutation proof.
4. Correct attempt-3 receipt overstatements, rerun both modes/regression, launch
   one fresh read-only review, and append Worker attempt 4.
```

## Commits

No implementation or acceptance commit was created.

## Worker attempt 4

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 010
```

Executed under controller review 2 amendment (one-hot boolean evidence required).
Test-only change: replaced `ParseUncoveredKeys` with `OneHotBooleanMappings` (loop
over 6 boolean cases) and `ParseUncoveredNonBoolKeys` (9 numeric fields). Production
code not modified. Mutation proof recaptured with the new one-hot structure.

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `096ba0c9ef8d68ca91ca62a7b15cf6a74bbc058a` | exactly seven attempt-2/3 files dirty (M CMakeLists ×3; ?? FileCacheSettings.h/.cpp, FileCacheReadOptions.h, FileCacheSettingsTest.cpp) |
| `/root/oss/clickhouse` | `ch-filecache` | `22a3f1fec07` | clean |

## Files changed

```text
/root/oss/velox/velox/ch/Common/CMakeLists.txt                                 (M, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/CMakeLists.txt                 (M, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt           (M, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheSettings.h            (??, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheSettings.cpp          (??, unchanged from attempt 3)
/root/oss/velox/velox/ch/Interpreters/FileCache/FileCacheReadOptions.h         (??, unchanged from attempt 2)
/root/oss/velox/velox/ch/Interpreters/FileCache/tests/FileCacheSettingsTest.cpp (??, test-only: replaced ParseUncoveredKeys with OneHotBooleanMappings + ParseUncoveredNonBoolKeys)
/root/oss/clickhouse/port/task/result/010-filecache-settings-result.md         (this receipt)
```

Exactly the 7 declared Velox files plus the receipt. No other file touched.

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| GREEN build (new tests) | 0 | `/root/oss/velox/_build/debug/build_task_010_attempt4_green1.log` |
| GREEN direct test 59/59 (pre-mutation) | 0 | `/root/oss/velox/_build/debug/test_task_010_attempt4_green1_direct.log` |
| mutation build (allow-dynamic-cache-resize → enableFilesystemQueryCacheLimit) | 0 | `/root/oss/velox/_build/debug/build_task_010_attempt4_mutation.log` |
| mutation direct test (OneHotBooleanMappings fails) | 1 (expected) | `/root/oss/velox/_build/debug/test_task_010_attempt4_mutation.log` |
| mutation restored; final GREEN build | 0 | `/root/oss/velox/_build/debug/build_task_010_attempt4_final.log` |
| GREEN direct test 59/59 (final) | 0 | `/root/oss/velox/_build/debug/test_task_010_attempt4_final_direct.log` |
| focused CTest mono (velox_ch_settings_test) | 0 | `/root/oss/velox/_build/debug/test_task_010_attempt4_settings_ctest.log` |
| accumulated Tasks 003-010 regression (10 tests) | 0 | `/root/oss/velox/_build/debug/test_task_010_attempt4_regression.log` |
| non-mono build | 0 | `/root/oss/velox/_build/debug-task010-nonmono/build_task_010_attempt4_nonmono.log` |
| non-mono direct test 59/59 | 0 | `/root/oss/velox/_build/debug-task010-nonmono/test_task_010_attempt4_nonmono_direct.log` |
| non-mono CTest discovery | 0 | `/root/oss/velox/_build/debug-task010-nonmono/test_task_010_attempt4_nonmono_ctest.log` |

## Acceptance evidence

```text
test count: 59 gtest cases (mono and non-mono identical; +1 vs attempt 3: removed 1, added 2)
failed tests: 0 (final GREEN, mono and non-mono)
skipped/disabled tests: 0
accumulated regression: 10/10 CTest pass
  (velox_ch_common, velox_ch_chassert_release_probe, velox_ch_chassert_sanitizer_gate_test,
   velox_ch_threadpool, velox_ch_scheduler, velox_ch_guards, velox_ch_leaf_types,
   velox_ch_sharded_map, velox_ch_settings, velox_ch_io)

One-hot boolean evidence: six cases in OneHotBooleanMappings loop:
  i=0: allow-dynamic-cache-resize              → allowDynamicCacheResize
  i=1: enable-filesystem-query-cache-limit     → enableFilesystemQueryCacheLimit
  i=2: expose-prometheus-eviction-metrics      → exposePrometheusEvictionMetrics
  i=3: expose-prometheus-eviction-metrics-per-user → exposePrometheusEvictionMetricsPerUser
  i=4: skip-cache-on-disk-failure              → skipCacheOnDiskFailure
  i=5: write-cache-per-user-id-directory       → writeCachePerUserIdDirectory
  Each case: only the i-th key set to "true"; all other five boolean fields
  asserted false. Any swap — including within a same-valued group — fails at
  least two EXPECT_TRUE/EXPECT_FALSE assertions.

Nine non-boolean fields in ParseUncoveredNonBoolKeys at distinct non-default values:
  backgroundDownloadMaxFileSegmentSize = 8388608 (default 4194304)
  cacheHitsThreshold = 42 (default 0)
  checkCacheProbability = 0.05 (default 0.001)
  dynamicResizeLockWaitMs = 2500 (default 1000)
  idleClientCheckIntervalSec = 300 (default 0)
  idleClientTtlSec = 3600 (default 604800)
  keepFreeSpaceElementsRatio = 0.15 (default 0.0)
  keepFreeSpaceRemoveBatch = 500 (default 250)
  splitCacheRatio = 0.3 (default 0.1)

Mutation proof: routed allow-dynamic-cache-resize → enableFilesystemQueryCacheLimit.
  test_task_010_attempt4_mutation.log shows OneHotBooleanMappings FAILED with 3
  assertion failures spanning cases i=0 and i=1:
    "key allow-dynamic-cache-resize must set its own field to true"
    "field for enable-filesystem-query-cache-limit must remain false when only allow-dynamic-cache-resize is set"
    "key enable-filesystem-query-cache-limit must set its own field to true"
  Exit code 1, 1 test failed (58 passed). Production restored exactly; no marker left.

git diff --check: clean (no whitespace errors); only the 7 declared Velox files changed.
```

## Worker review

```text
review subagent: one read-only code-review subagent launched on the complete
  attempt-4 diff (OneHotBooleanMappings + ParseUncoveredNonBoolKeys replacement).
findings: none. Subagent verified:
  - One-hot loop is logically airtight: any key→wrong-field routing caught by
    EXPECT_TRUE on correct field; any extra field set caught by EXPECT_FALSE loop.
  - Full swap of two entries fails in both loop iterations.
  - All 9 non-boolean values are distinct non-default; assertions name the correct fields.
  - Mutation proof correctly exercises the one-hot test paths; production code
    restored with no leftover mutation markers.
  - No false-green risk; no concurrency/lifetime/integration issues.
unresolved findings: none.
```

## Blockers

```text
None.
```

## Worker declaration

```text
Only Task 010 was attempted (attempt 4).
Changes are unstaged and uncommitted in both repositories.
The 7 declared Velox files (test-only edit to FileCacheSettingsTest.cpp, all
other files unchanged from attempts 2/3) plus this receipt are the only changes.
No Task 011 file and no out-of-scope file was touched.
The worker stopped immediately after writing this receipt.
```

## Controller review 3

```text
controller_status: accepted
environment_profile: root-oss

Scope:
- Inspected the complete Task 010 diff and all seven declared Velox files.
- Confirmed no Task 011 implementation or unrelated source was included.

Implementation evidence:
- `FileCacheSettings` provides the complete 39-field effective configuration and
  `FileCacheReadOptions` provides request-scoped read settings.
- File-system containment is checked by canonical path components before
  `create_directories` or `space` and rejects symlink escapes.
- `max-size-ratio-to-total-space` rejects NaN, infinities, zero, and values above
  one before floating-point to integer conversion.
- All loader keys have direct parse evidence, including six boolean one-hot
  cases that fail for swaps, misroutes, or extra fields.
- Mono and non-mono CMake registrations include the new implementation,
  public headers, `velox_common_config` dependency, and focused test target.

Fresh Controller evidence:
- Mono CTest: 10/10 passed.
- Mono focused direct gtest: 59/59 passed.
- Non-mono CTest: 1/1 passed.
- Non-mono focused direct gtest: 59/59 passed.
- Disabled/skipped focused tests: 0.
- Fresh precommit mono gate: 10/10 passed.
- Fresh precommit non-mono gate: 1/1 passed with `VELOX_MONO_LIBRARY=OFF`.
- `git diff --check` passed.
- Independent final code review reported no significant findings.

Accepted Velox commit:
89039901aa4287ce811a3b1628867b0796c76678 Task 010: Add `FileCache` settings
```
