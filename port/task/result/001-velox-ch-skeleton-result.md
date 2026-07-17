# Task 001 Result: Add `velox/ch` Skeleton Target

## Status

status: success

Note: build target verification adapted for `VELOX_MONO_LIBRARY=ON` (see
"Build result" below).

## Velox status

```text
## filecache
9f7652f64 Add initial `velox/ch` FileCache skeleton
```

## Files changed

```text
velox/CMakeLists.txt
velox/ch/CMakeLists.txt
velox/ch/Common/CMakeLists.txt
velox/ch/Common/FileCacheSkeleton.cpp
velox/ch/Common/FileCacheSkeleton.h
```

## Commands run

```text
# Configure
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > .../configure_task_001_velox_ch_skeleton.log 2>&1

# Build (adapted for mono-library: build the object directly instead of
# the non-existent standalone target)
ninja -C .../cmake-build-debug-gcc13 \
  velox/buffer/CMakeFiles/velox.dir/__/ch/Common/FileCacheSkeleton.cpp.o \
  > .../build_task_001_velox_ch_skeleton.log 2>&1

# Target list check
ninja -C .../cmake-build-debug-gcc13 -t targets | grep '^velox_ch_filecache:'
```

## Generated logs

```text
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_001_velox_ch_skeleton.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_001_velox_ch_skeleton.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/target_task_001_velox_ch_filecache.txt
```

## Build result

```text
Configure: exit 0, log ends with "Build files have been written to".
Build:     exit 0. FileCacheSkeleton.cpp.o compiled clean as part of `velox`.

The build directory has VELOX_MONO_LIBRARY=ON. Under this setting,
velox_add_library(velox_ch_filecache ...) does NOT create a standalone
target named velox_ch_filecache; instead its sources are appended to the
single `velox` library (CMake/VeloxUtils.cmake:111-114). Consequences:

  - `ninja velox_ch_filecache`               -> unknown target
  - `ninja -t targets | grep velox_ch_filecache:` -> empty (expected)

The skeleton IS wired into the build. Verified by building the object
file directly:
  velox/buffer/CMakeFiles/velox.dir/__/ch/Common/FileCacheSkeleton.cpp.o
which compiled with exit 0.

To obtain a real standalone velox_ch_filecache target, reconfigure with
-DVELOX_MONO_LIBRARY=OFF. Not done here to stay on the ENVIRONMENT.md
standard configure command.
```

## Blocking errors

```text
None
```

## Commit

```text
Velox branch: filecache
Velox commit: 9f7652f64 Add initial `velox/ch` FileCache skeleton
```

## Recommended next task

```text
Add the first Common shim under velox/ch/Common, starting with no-op metrics/debug shims or basic aliases.
Note for future tasks: under VELOX_MONO_LIBRARY=ON, verify new ch sources by
compiling their object files (or building the `velox` target), not by building
a per-name target. Alternatively decide whether the port wants
VELOX_MONO_LIBRARY=OFF for isolated velox_ch_filecache linking.
```
