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

## Existing design docs

The ClickHouse-side design docs live under:

```text
/home/chang/SourceCode/ClickHouse/port
```

Start with:

```text
00-filecache-velox-migration.md
05-filecache-port-order-design.md
```

Then read the specific design doc referenced by the task.
