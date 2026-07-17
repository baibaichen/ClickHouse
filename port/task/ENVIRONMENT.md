# `FileCache` Velox Port Task Environment

这个文件是所有 `port/task` handoff task 的共享环境说明。每个 task 都应先读这个文件，
再读自己的任务文件。

## Repositories

```text
ClickHouse design repo: /home/chang/SourceCode/ClickHouse
Velox source repo:      /home/chang/OpenSource/velox
Velox build dir:        /home/chang/OpenSource/velox/cmake-build-debug-gcc13
```

## CMake configuration

Velox 当前使用这个 CMake 配置：

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13
```

## Tools

```text
CMake: /usr/bin/cmake
Ninja: /home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja
```

## Rules

- Do not modify ClickHouse source files from Velox implementation tasks unless the task explicitly says so.
- Do not commit anything unless explicitly asked.
- Do not delete or recreate `/home/chang/OpenSource/velox`.
- Do not use `-j` with Ninja; let Ninja decide parallelism.
- Put build/configuration logs under `/home/chang/OpenSource/velox/cmake-build-debug-gcc13`.
- For long builds/tests, redirect output to a log in the build directory and report the log path.
- If a command fails, stop and report the first actionable error plus the log path.
- Result files under `port/task/result/` are handoff artifacts. Do not commit them unless explicitly asked.

## Result handoff

Each task must write its final result back under:

```text
/home/chang/SourceCode/ClickHouse/port/task/result/
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
/home/chang/SourceCode/ClickHouse/port
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
| 016 | post-MVP `WriteBufferToFileSegment` | focused ephemeral writer test |
| 017 | post-MVP observability and cancellation | focused + E2E regression tests |
| 018 | future Gluten `VeloxBackend` ownership and Builder integration | Gluten focused native test |
| 019 | future Gluten builder/lifecycle E2E | Gluten integration acceptance gate |

Tasks 011 and 012 are one atomic implementation stage. Task 011 must not create
fake center-SCC definitions or claim a build; Task 012 immediately completes
the real types, registers all priority/core sources, and runs the green gate.

Tasks 003-015 are the current Velox MVP path. The execution protocol stops after
Task 015. Tasks 016-017 are optional Velox post-MVP work. Tasks 018-019 are
deferred Gluten integration work and are not dispatched in the current phase.
