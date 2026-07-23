# Task 018-C Arrow Testing Bypass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Task-018 TPCH target with bundled Arrow but without the unused Arrow testing library or its Boost.Process dependency.

**Architecture:** Add a default-on Velox option controlling only Arrow testing. When disabled, bundled Arrow builds `libarrow.a`, Arrow-testing imported targets and dependent tests are omitted, while normal Velox testing and TPCH test utilities remain enabled.

**Tech Stack:** CMake, Ninja, Arrow 18 ExternalProject, Velox RelWithDebInfo.

## Global Constraints

```text
Binding design: port/design/filecache-task-018-arrow-testing-bypass.md
Default Velox behavior remains unchanged: VELOX_ENABLE_ARROW_TESTING=ON.
Task 018-C alone configures VELOX_ENABLE_ARROW_TESTING=OFF.
VELOX_BUILD_TESTING remains ON.
Do not modify vcpkg, Boost, downloaded Arrow source, Gluten, or TPCH behavior.
Remove the temporary Arrow process.cc stub by recreating the exact Arrow
ExternalProject prefix from the verified archive.
Worker does not stage or commit.
Controller creates one independent commit with subject:
![TMP] Disable Arrow testing for TPCH build
No Ninja -j or nproc; all configure/build/test output goes to unique logs under
/root/oss/velox/_build/relwithdebinfo.
```

---

### Task 1: Add the default-on Arrow-testing option

**Files:**
- Modify: `/root/oss/velox/CMakeLists.txt`
- Test: `/root/oss/velox/_build/relwithdebinfo/CMakeCache.txt`

**Interfaces:**
- Produces: CMake boolean `VELOX_ENABLE_ARROW_TESTING`, default `ON`
- Consumed by: bundled Arrow dependency and Arrow-only test-directory gates

- [ ] **Step 1: Record the existing default behavior**

```bash
grep -n 'VELOX_ENABLE_ARROW' /root/oss/velox/CMakeLists.txt
```

Expected: `VELOX_ENABLE_ARROW` exists and no
`VELOX_ENABLE_ARROW_TESTING` option exists.

- [ ] **Step 2: Add the option next to `VELOX_ENABLE_ARROW`**

```cmake
option(VELOX_ENABLE_ARROW "Enable Arrow support" OFF)
option(VELOX_ENABLE_ARROW_TESTING "Build Arrow testing support and dependent tests" ON)
```

- [ ] **Step 3: Verify the default remains enabled**

Configure a clean probe cache under `tmp/arrow_testing_default_probe` with no
override and inspect:

```bash
cmake -S /root/oss/velox \
  -B /root/oss/velox/tmp/arrow_testing_default_probe \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DVELOX_ENABLE_PARQUET=OFF \
  -DVELOX_BUILD_TESTING=OFF \
  > /root/oss/velox/_build/relwithdebinfo/configure_arrow_testing_default_probe.log 2>&1
grep '^VELOX_ENABLE_ARROW_TESTING:BOOL=ON$' \
  /root/oss/velox/tmp/arrow_testing_default_probe/CMakeCache.txt
```

Expected: one exact match.

---

### Task 2: Make bundled Arrow testing conditional

**Files:**
- Modify: `/root/oss/velox/CMake/resolve_dependency_modules/arrow/CMakeLists.txt`

**Interfaces:**
- Consumes: `VELOX_ENABLE_ARROW_TESTING`
- Produces: `arrow` always; `arrow_testing` only when enabled

- [ ] **Step 1: Forward the option**

Replace:

```cmake
-DARROW_TESTING=ON
```

with:

```cmake
-DARROW_TESTING=${VELOX_ENABLE_ARROW_TESTING}
```

- [ ] **Step 2: Pin static zstd linkage (reproducibility fix)**

Add, immediately after `-DARROW_WITH_ZSTD=ON`:

```cmake
-DARROW_ZSTD_USE_SHARED=OFF
```

Bundled Arrow is always built `ARROW_BUILD_STATIC=ON`, and this
environment's vcpkg `zstd` triplet exports only the
`zstd::libzstd_static`/`zstd::libzstd` CMake targets (no
`zstd::libzstd_shared`). Without this flag, Arrow's default
`ARROW_ZSTD_USE_SHARED=ON` makes `ThirdpartyToolchain.cmake` fail with
`Zstandard target doesn't exist: zstd::libzstd_shared` on every fresh
Arrow configure. Tracking the flag here (instead of only caching it with a
manual `cmake -D... .` inside the generated `arrow_ep-build` directory)
makes deleting and recreating the `arrow_ep` prefix (Task 4) reproducible
without any generated-cache edit.

- [ ] **Step 3: Construct the exact byproduct list**

Immediately after `set(ARROW_LIBDIR ...)`, add:

```cmake
set(ARROW_BUILD_BYPRODUCTS ${ARROW_LIBDIR}/libarrow.a)
if(VELOX_ENABLE_ARROW_TESTING)
  list(APPEND ARROW_BUILD_BYPRODUCTS ${ARROW_LIBDIR}/libarrow_testing.a)
endif()
```

Change the ExternalProject declaration to:

```cmake
BUILD_BYPRODUCTS ${ARROW_BUILD_BYPRODUCTS}
```

- [ ] **Step 4: Keep `arrow` unconditional and gate `arrow_testing`**

The target block must have this structure:

```cmake
add_library(arrow STATIC IMPORTED GLOBAL)
add_dependencies(arrow arrow_ep)
file(MAKE_DIRECTORY ${ARROW_PREFIX}/install/include)
set_target_properties(
  arrow
  PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ARROW_PREFIX}/install/include
)
set_target_properties(arrow PROPERTIES IMPORTED_LOCATION ${ARROW_LIBDIR}/libarrow.a)
set_property(TARGET arrow PROPERTY INTERFACE_LINK_LIBRARIES ${RE2})

if(VELOX_ENABLE_ARROW_TESTING)
  add_library(arrow_testing STATIC IMPORTED GLOBAL)
  add_dependencies(arrow_testing arrow)
  set_target_properties(
    arrow_testing
    PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES ${ARROW_PREFIX}/install/include
      IMPORTED_LOCATION ${ARROW_LIBDIR}/libarrow_testing.a
      INTERFACE_LINK_LIBRARIES ${ARROW_LIBDIR}/libarrow.a
  )
endif()
```

- [ ] **Step 5: Run CMake formatting/check**

Use the repository's existing CMake formatting or style check if available.
Do not install a new formatter. `git diff --check` is mandatory.

---

### Task 3: Skip only tests that require `arrow_testing`

**Files:**
- Modify: `/root/oss/velox/velox/vector/arrow/CMakeLists.txt`
- Modify: `/root/oss/velox/velox/dwio/parquet/writer/arrow/CMakeLists.txt`

**Interfaces:**
- Consumes: `VELOX_ENABLE_ARROW_TESTING`
- Preserves: all non-Arrow-testing Velox tests and test utility libraries

- [ ] **Step 1: Gate Arrow bridge tests**

```cmake
if(VELOX_BUILD_TESTING AND VELOX_ENABLE_ARROW AND VELOX_ENABLE_ARROW_TESTING)
  add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Gate Arrow Parquet writer tests**

```cmake
if(VELOX_BUILD_TESTING AND VELOX_ENABLE_ARROW_TESTING)
  add_subdirectory(tests)
endif()
```

- [ ] **Step 3: Prove no other `arrow_testing` consumer remains**

```bash
grep -R -n --include='CMakeLists.txt' 'arrow_testing' \
  /root/oss/velox/velox
```

Expected: references only inside the two gated test directories.

---

### Task 4: Recreate Arrow and verify the Task-018 build

**Files:**
- Modify: `/root/oss/clickhouse/port/task/018-filecache-gluten-benchmark-plan.md`
- Recreate generated directory:
  `/root/oss/velox/_build/relwithdebinfo/CMake/resolve_dependency_modules/arrow/arrow_ep`

**Interfaces:**
- Consumes: `VELOX_ENABLE_ARROW_TESTING=OFF`
- Produces: reproducible `libarrow.a` and `velox_tpch_benchmark` without
  `libarrow_testing.a` or `testing/process.cc`

- [ ] **Step 1: Update the Task-018-C configure command**

Add:

```cmake
-DVELOX_ENABLE_ARROW_TESTING=OFF
```

directly after `-DVELOX_ENABLE_PARQUET=ON`.

- [ ] **Step 2: Inspect and remove only the generated Arrow prefix**

```bash
ARROW_PREFIX=/root/oss/velox/_build/relwithdebinfo/CMake/resolve_dependency_modules/arrow/arrow_ep
test "$(realpath -m -- "$ARROW_PREFIX")" = \
  "/root/oss/velox/_build/relwithdebinfo/CMake/resolve_dependency_modules/arrow/arrow_ep"
find "$ARROW_PREFIX" -maxdepth 2 -type f | head -20
rm -rf -- "$ARROW_PREFIX"
```

This exact generated subtree contains the temporary `process.cc` stub. Do not
delete any parent build directory.

- [ ] **Step 3: Reconfigure**

Run the complete Task-018-C RelWithDebInfo configure command with Parquet ON,
Velox testing ON, and Arrow testing OFF. Redirect output to:

```text
/root/oss/velox/_build/relwithdebinfo/configure_018c_arrow_testing_off.log
```

- [ ] **Step 4: Verify the Arrow sub-build configuration and graph**

```bash
ARROW_BUILD=/root/oss/velox/_build/relwithdebinfo/CMake/resolve_dependency_modules/arrow/arrow_ep/src/arrow_ep-build
grep '^ARROW_TESTING:BOOL=OFF$' "$ARROW_BUILD/CMakeCache.txt"
grep '^ARROW_ZSTD_USE_SHARED:BOOL=OFF$' "$ARROW_BUILD/CMakeCache.txt"
if ninja -C "$ARROW_BUILD" -t commands | grep -q 'testing/process.cc'; then
  echo "unexpected Arrow testing compile command" >&2
  exit 1
fi
```

Expected: `ARROW_TESTING:BOOL=OFF` and `ARROW_ZSTD_USE_SHARED:BOOL=OFF`, both
arriving from the ExternalProject `CMAKE_ARGS` (i.e. present without any
manual `cmake -D... .` invocation inside `arrow_ep-build`), and no
`testing/process.cc` compile command.

- [ ] **Step 5: Build the focused tests and TPCH target**

```bash
ninja -C /root/oss/velox/_build/relwithdebinfo \
  velox_ab_benchmark_schema_test \
  velox_adaptive_prefetch_test \
  velox_tpch_benchmark \
  > /root/oss/velox/_build/relwithdebinfo/build_018c_arrow_testing_off.log 2>&1
```

Expected: exit 0; `libarrow.a` and `velox_tpch_benchmark` exist;
`libarrow_testing.a` does not exist.

- [ ] **Step 6: Run focused tests**

```bash
/root/oss/velox/_build/relwithdebinfo/velox/benchmarks/tests/velox_ab_benchmark_schema_test \
  > /root/oss/velox/_build/relwithdebinfo/test_018c_arrow_off_schema.log 2>&1
/root/oss/velox/_build/relwithdebinfo/velox/exec/tests/velox_adaptive_prefetch_test \
  > /root/oss/velox/_build/relwithdebinfo/test_018c_arrow_off_prefetch.log 2>&1
```

Expected: schema 5/5 and adaptive prefetch 6/6.

- [ ] **Step 7: Verify repository isolation**

```bash
git -C /root/oss/velox --no-pager status --short
git -C /root/oss/clickhouse --no-pager status --short
```

Expected: only declared CMake/task-plan changes plus the pre-existing
Task-018-C diff. No downloaded Arrow, vcpkg, Boost, or Gluten file is changed.

- [ ] **Step 8: Independent review**

Review the exact declared diff and all configure/build/test evidence. Critical
or Important findings block the commit.

- [ ] **Step 9: Controller commit**

Worker leaves all changes unstaged. Controller stages only:

```text
CMakeLists.txt
CMake/resolve_dependency_modules/arrow/CMakeLists.txt
velox/vector/arrow/CMakeLists.txt
velox/dwio/parquet/writer/arrow/CMakeLists.txt
```

Controller creates the standalone Velox commit:

```text
![TMP] Disable Arrow testing for TPCH build
```

The ClickHouse Task-018 plan update is committed separately in the ClickHouse
repository and must not be mixed into the Velox `![TMP]` commit.
