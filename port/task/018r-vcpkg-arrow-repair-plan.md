# Task 018R: vcpkg `Arrow` Repair Handoff

## Goal

Update the `root-oss` build environment before Task 017B:

1. switch `/root/oss/gluten` to
   `baibaichen/fix/vcpkg-arrow-squashed`;
2. reverse the complete temporary Velox commit
   `d52f069e9b1d24c6a2ee8086c3774cc45672fc0a`;
3. build Velox against vcpkg `Arrow`, including both Arrow test targets and
   `velox_tpch_benchmark`.

This task does not compile or modify Gluten C++ code. Gluten compilation belongs
to Task 019.

## Fixed baselines

```text
environment_profile: root-oss

ClickHouse:
  path:   /root/oss/clickhouse
  branch: ch-filecache

Velox:
  path:   /root/oss/velox
  branch: filecache
  head:   cda6c03703cf4ed0b1b515465915dbfd599bcb6c

Gluten current:
  path:   /root/oss/gluten
  branch: chang/velox-vector-zerocopy
  head:   52dd77c6ef1a0158e2f950fd2d8cc30f1212cbc1

Gluten target:
  remote: baibaichen
  branch: fix/vcpkg-arrow-squashed
  head:   4b77376dea1733538e169c71bb6e301ab5ded608

Fresh Velox build:
  /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow

Result:
  /root/oss/clickhouse/port/task/result/018r-vcpkg-arrow-repair-result.md
```

Stop if either implementation repository is dirty or its starting branch/HEAD
does not match. Do not reset, rebase, amend, commit, or push.

## Step 1: Verify and switch Gluten

```bash
git -C /root/oss/velox status --short --branch
git -C /root/oss/velox rev-parse HEAD
git -C /root/oss/gluten status --short --branch
git -C /root/oss/gluten rev-parse HEAD

git -C /root/oss/gluten fetch baibaichen \
  refs/heads/fix/vcpkg-arrow-squashed

test "$(git -C /root/oss/gluten rev-parse FETCH_HEAD)" = \
  4b77376dea1733538e169c71bb6e301ab5ded608

git -C /root/oss/gluten switch --create fix/vcpkg-arrow-squashed \
  --track baibaichen/fix/vcpkg-arrow-squashed

test "$(git -C /root/oss/gluten rev-parse HEAD)" = \
  4b77376dea1733538e169c71bb6e301ab5ded608
git -C /root/oss/gluten diff --quiet
git -C /root/oss/gluten diff --cached --quiet
```

If the local target branch already exists, switch to it only when it already
points to the exact target SHA. Do not reset it.

## Step 2: Install and verify vcpkg `Arrow`

Create the fresh build directory only for logs and the later configure:

```bash
test ! -e /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/CMakeCache.txt
mkdir -p /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow
```

Install the target branch's manifest with test dependencies:

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/vcpkg_install_018r.log 2>&1
```

Verify the installed files:

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  test -f "$VCPKG_TRIPLET_INSTALL_DIR/include/arrow/api.h"
  test -f "$VCPKG_TRIPLET_INSTALL_DIR/lib/libarrow.a"
  test -f "$VCPKG_TRIPLET_INSTALL_DIR/lib/libarrow_testing.a"
  printf "triplet=%s\n" "$VCPKG_TRIPLET"
  printf "arrow=%s\n" \
    "$VCPKG_TRIPLET_INSTALL_DIR/lib/libarrow.a"
  printf "arrow_testing=%s\n" \
    "$VCPKG_TRIPLET_INSTALL_DIR/lib/libarrow_testing.a"
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/vcpkg_arrow_018r.log 2>&1
```

The target branch already solves module-mode discovery in its Velox build path
with `GLUTEN_VCPKG_PREFER_CONFIG=OFF`. Reuse that setting; do not add another
Arrow workaround.

## Step 3: Reverse the temporary Velox commit

Verify the complete reverse applies:

```bash
git -C /root/oss/velox show \
  d52f069e9b1d24c6a2ee8086c3774cc45672fc0a |
  git -C /root/oss/velox apply -R --check
```

Apply the complete reverse patch without touching the index:

```bash
git -C /root/oss/velox show \
  d52f069e9b1d24c6a2ee8086c3774cc45672fc0a |
  git -C /root/oss/velox apply -R
```

Verify that the worktree exactly matches the parent of the temporary commit:

```bash
git -C /root/oss/velox diff --cached --quiet

git -C /root/oss/velox diff --exit-code \
  d52f069e9b1d24c6a2ee8086c3774cc45672fc0a^ -- \
  CMakeLists.txt \
  CMake/resolve_dependency_modules/arrow/CMakeLists.txt \
  velox/dwio/parquet/writer/arrow/CMakeLists.txt \
  velox/vector/arrow/CMakeLists.txt

git -C /root/oss/velox diff --check
```

Expected changed files:

```text
CMake/resolve_dependency_modules/arrow/CMakeLists.txt
CMakeLists.txt
velox/dwio/parquet/writer/arrow/CMakeLists.txt
velox/vector/arrow/CMakeLists.txt
```

## Step 4: Configure Velox with vcpkg `Arrow`

Use the same system-dependency behavior already implemented by the target
Gluten branch:

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  export VELOX_DEPENDENCY_SOURCE=SYSTEM
  export Arrow_SOURCE=SYSTEM
  export simdjson_SOURCE=SYSTEM
  export GLUTEN_VCPKG_PREFER_CONFIG=OFF

  exec /usr/bin/cmake \
    -S /root/oss/velox \
    -B /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DVELOX_GFLAGS_TYPE=static \
    -DVELOX_BUILD_TESTING=ON \
    -DVELOX_ENABLE_BENCHMARKS=ON \
    -DVELOX_ENABLE_EXEC=ON \
    -DVELOX_ENABLE_PARQUET=ON \
    -DVELOX_ENABLE_ARROW=ON \
    -DVELOX_ENABLE_REMOTE_FUNCTIONS=ON \
    -DVELOX_ENABLE_GROUPED_TESTS=OFF \
    -DVELOX_MONO_LIBRARY=ON \
    -DVELOX_BUILD_RUNNER=OFF \
    -DVELOX_ENABLE_GEO=OFF \
    -DVELOX_BUILD_MINIMAL=OFF \
    -DVELOX_SIMDJSON_SKIPUTF8VALIDATION=ON \
    -DMAX_HIGH_MEM_JOBS=16 \
    -DMAX_LINK_JOBS=16 \
    -DVELOX_FORCE_COLORED_OUTPUT=ON
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/configure_018r.log 2>&1
```

Do not pass the reverted `VELOX_ENABLE_ARROW_TESTING` option.

Verify the resulting configuration:

```bash
build=/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow

grep -F 'Setting Arrow source to SYSTEM' "$build/configure_018r.log"
grep '^VELOX_ENABLE_ARROW:BOOL=ON$' "$build/CMakeCache.txt"
grep '^VELOX_ENABLE_PARQUET:BOOL=ON$' "$build/CMakeCache.txt"
grep '^ARROW_LIB:FILEPATH=.*/vcpkg_installed/.*/lib/libarrow.a$' \
  "$build/CMakeCache.txt"
grep '^ARROW_TESTING_LIB:FILEPATH=.*/vcpkg_installed/.*/lib/libarrow_testing.a$' \
  "$build/CMakeCache.txt"
test ! -e "$build/CMake/resolve_dependency_modules/arrow/arrow_ep"
```

## Step 5: Build Velox

Build the mono Velox library, both Arrow test binaries, the FileCache E2E
binary, and the TPCH benchmark:

```bash
bash -lc '
  set -euo pipefail
  cd /root/oss/gluten
  source dev/vcpkg/env.sh --build_tests=ON
  source /root/oss/velox-helper/env.sh
  export VELOX_DEPENDENCY_SOURCE=SYSTEM
  export Arrow_SOURCE=SYSTEM
  export simdjson_SOURCE=SYSTEM
  export GLUTEN_VCPKG_PREFER_CONFIG=OFF

  exec /usr/bin/cmake --build \
    /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
    --target \
      velox \
      velox_arrow_bridge_test \
      velox_dwio_arrow_parquet_writer_test \
      velox_ch_filecache_e2e_test \
      velox_tpch_benchmark
' > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018r.log 2>&1
```

Expected: exit `0`. This task proves compilation and linkage only; it does not
run Gluten, TPCH performance, or the full Velox test suite.

## Step 6: Verify linkage and write the result

Capture the Arrow writer link graph:

```bash
/usr/local/bin/ninja \
  -C /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow \
  -t commands velox_dwio_arrow_parquet_writer_test \
  > /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/link_arrow_018r.log

grep -E '/vcpkg_installed/.*/lib/libarrow_testing\.a' \
  /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/link_arrow_018r.log
grep -E '/vcpkg_installed/.*/lib/libarrow\.a' \
  /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/link_arrow_018r.log
! grep -F '/arrow_ep/' \
  /root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/link_arrow_018r.log
```

Write:

```text
/root/oss/clickhouse/port/task/result/018r-vcpkg-arrow-repair-result.md
```

Record repository baselines/final states, commands, logs, exact four-file
reverse proof, vcpkg Arrow paths, built targets, and the first actionable error
if blocked. Use:

```text
worker_status: ready_for_controller | blocked
environment_profile: root-oss
task: 018R
```

Then stop. Do not stage, commit, push, start Task 017B, or compile Gluten.

The Controller reviews the four-file Velox diff and logs, commits the Velox
reverse with a `Task 018R:` subject, records the implementation SHA in the
receipt, and commits the receipt separately in ClickHouse.
