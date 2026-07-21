# `FileCache` Velox Port Task Environment

This file is the shared environment source for `port/task` handoff tasks.
Before acting, an executor must explicitly name one profile and then use every
value from that same profile. Do not mix values between profiles and do not
infer a profile from hostname.

## Profiles

| Key | `home-chang` | `root-oss` |
|---|---|---|
| `<clickhouse_repo>` | `/home/chang/SourceCode/ClickHouse` | `/root/oss/clickhouse` |
| `<velox_repo>` | `/home/chang/OpenSource/velox` | `/root/oss/velox` |
| `<gluten_repo>` | `/home/chang/SourceCode/gluten1` | `/root/oss/gluten` |
| `<velox_build_dir>` | `/home/chang/OpenSource/velox/cmake-build-debug-gcc13` | `/root/oss/velox/_build/debug` |
| `<cmake>` | `/usr/bin/cmake` | `/usr/bin/cmake` |
| `<ninja>` | `/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja` | `/usr/local/bin/ninja` |
| `<velox_env>` | Not required by the existing profile | `/root/oss/velox-helper/env.sh` |
| `<velox_helper>` | Not used | `/root/oss/velox-helper/build.sh` |
| `<vcpkg_toolchain>` | Not used by the existing profile | `/root/oss/gluten/dev/vcpkg/toolchain.cmake` |

## `home-chang`

Keep the existing configure command:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13
```

## `root-oss`

The environment source is `/root/oss/velox-helper/README.md`, backed by
`/root/oss/velox-helper/build.sh` and `/root/oss/velox-helper/env.sh`.
`build.sh` is the canonical daily developer convenience documented there; it
selects parallelism internally.

Canonical helper commands:

```bash
bash /root/oss/velox-helper/build.sh config
bash /root/oss/velox-helper/build.sh <target>
```

For FileCache port Worker/Controller acceptance executions, source
`<velox_env>` and run the helper-equivalent CMake/configuration directly
instead of using `build.sh` as evidence. Do not pass `-j`; redirect each
configure/build/test command to a unique log under `<velox_build_dir>` and
report the log path in the handoff.

Effective `root-oss` configuration:

```text
CMAKE_TOOLCHAIN_FILE=/root/oss/gluten/dev/vcpkg/toolchain.cmake
FETCHCONTENT_FULLY_DISCONNECTED=ON
VELOX_GFLAGS_TYPE=static
VELOX_BUILD_TESTING=ON
VELOX_ENABLE_BENCHMARKS=ON
VELOX_ENABLE_EXEC=ON
VELOX_ENABLE_PARQUET=OFF
VELOX_ENABLE_REMOTE_FUNCTIONS=ON
VELOX_ENABLE_GROUPED_TESTS=OFF
VELOX_MONO_LIBRARY=ON
VELOX_BUILD_RUNNER=OFF
VELOX_ENABLE_GEO=OFF
VELOX_BUILD_MINIMAL=OFF
VELOX_SIMDJSON_SKIPUTF8VALIDATION=ON
MAX_HIGH_MEM_JOBS=16
MAX_LINK_JOBS=16
VELOX_FORCE_COLORED_OUTPUT=ON
```

Port execution commands must source `<velox_env>` and use the same
configuration. Keep the port protocol's stricter rules: no `-j` argument and
configure/build/test logs under `<velox_build_dir>`.

`root-oss` uses Gluten's vcpkg dependencies even before Tasks 018-019, but
selecting this profile does not authorize modifying the existing dirty Gluten
worktree. Tasks 018-019 are still the only tasks that modify Gluten.

## Rules

- Do not modify ClickHouse source files from Velox implementation tasks unless
  the task explicitly says so.
- Do not commit anything unless explicitly asked.
- Do not delete or recreate `<velox_repo>`.
- Do not use `-j` with Ninja; let Ninja decide parallelism.
- For long builds/tests, redirect output to a unique log under
  `<velox_build_dir>` and report the log path.
- If a command fails, stop and report the first actionable error plus the log
  path.
- Result files under `port/task/result/` are handoff artifacts. Do not commit
  them unless explicitly asked.
- CH production source and its real `FileCache` callers are authoritative for
  behavior. A task snippet or accepted receipt cannot weaken that contract.
- If migration reaches a CH dependency, macro, type, API, no-op, fallback, or
  Velox substitution that has not been explicitly reviewed in an approved
  design/task, stop the current task and the task pipeline. Record the source
  and callers, then wait for user review. Do not infer a mapping or continue
  an independent later task.
- Comment-only tests, fixtures that return null, disabled tests, and
  unregistered tests are false-green evidence and cannot satisfy a task gate.

## Result handoff

Each task must write its final result back under:

```text
<clickhouse_repo>/port/task/result/
```

Use this naming pattern:

```text
<task-number>-<short-task-name>-result.md
```

The result file must include:

```text
status: success / blocked / failed
Velox git branch and dirty status
commands run
generated files or logs
first actionable error, if blocked or failed
recommended next task
```

## Existing design docs

The ClickHouse-side design docs live under:

```text
<clickhouse_repo>/port
```

Start with:

```text
00-filecache-velox-migration.md
01-filecache-port-order-design.md
```

Then read the specific design doc referenced by the task.

## Implementation task sequence

Run tasks in this order in the same Velox `filecache` worktree:

| Task | Scope | Build gate |
|---|---|---|
| 003 | aliases, exception/log/fs shims, finishable bounded queue | `velox_ch_common_test` |
| 004 | `StatusFile` and `Guards.h` | `velox_ch_guards_test` |
| 005 | shared physical and logical thread pools | `velox_ch_threadpool_test` |
| 006 | scheduler and caller query/TID scope | `velox_ch_scheduler_test` |
| 007 | Velox `ReadFile` / `WriteFile` adapters | `velox_ch_io_test` |
| 008 | hash, key/origin/segment types, forwards, utils | `velox_ch_leaf_types_test` |
| 009 | `ShardedMap` | `velox_ch_sharded_map_test` |
| 010 | instance settings plus `FileCacheReadOptions` | `velox_ch_settings_test` |
| 011 | priority/eviction source migration, center-SCC Part A | structural check only |
| 012 | priority + center SCC compile/link closure | `velox_ch_filecache_core_scc_test` |
| 013 | Factory and Manager | `velox_ch_filecache_manager_test` |
| 014 | `FileCacheBufferedInput` / `FileCacheInputStream` | `velox_ch_filecache_buffered_input_test` |
| 015 | Velox-only E2E and random-seek benchmark | current MVP acceptance gate |
| 016 | deferred `WriteBufferToFileSegment` | not dispatched |
| 017A | statistics, cancellation, caller identity, scheduler parity | focused + mono/non-mono accumulated gates |
| 018 | Gluten lifecycle/Builder/metrics plus Velox benchmark suite | correctness, native/JNI/Java/Scala, and benchmark gates |
| 017B | logging and exception stack formatting | focused + mono/non-mono accumulated gates |
| 019 | Spark end-to-end design after accepted Task 017B | design gate only |

Tasks 011 and 012 are one atomic implementation stage. Task 011 must not create
fake center-SCC definitions or claim a build; Task 012 immediately completes
the real types, registers all priority/core sources, and runs the green gate.

After Task 010 is accepted, stop and run a whole-port source-contract review of
Tasks 003-010. Continue to Task 011 only when the review has zero unresolved
findings and the user explicitly approves.

After Task 014 is accepted, stop and run a whole-port source-contract review of
Tasks 003-014. Continue to Task 015 only when the review has zero unresolved
findings and the user explicitly approves.

Tasks 003-015 are accepted. Task 016 and real kernel `O_DIRECT` integration are
deferred by user decision. The reviewed mainline order is Task 017A, Task 018,
then Task 017B; Task 019 design starts only after Task 017B is accepted.
Implementation remains gated by `EXECUTION_PROTOCOL.md` and the current
Controller authorization.
