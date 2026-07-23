# Task 018 Arrow Testing Bypass Design

## Status

```text
decision_date: 2026-07-22
decision: approved
scope: Task 018 TPCH build only
default_behavior: unchanged
addendum_date: 2026-07-23
addendum: track ARROW_ZSTD_USE_SHARED=OFF in ARROW_CMAKE_ARGS (reproducibility fix)
```

## Problem

Task 018 needs bundled Arrow for Parquet, but `velox_tpch_benchmark` links only
`libarrow.a`; it does not consume `libarrow_testing.a`. The bundled Arrow 18
ExternalProject nevertheless hard-codes `ARROW_TESTING=ON`, which compiles
`arrow/testing/process.cc` and exposes an unrelated Boost.Process compatibility
problem after the environment update.

The temporary build-tree `process.cc` stub is not acceptable and must be
removed. Task 018 must bypass Arrow testing without changing vcpkg, Boost, or
Arrow source.

## Design

Add a Velox CMake option:

```cmake
VELOX_ENABLE_ARROW_TESTING
```

It defaults to `ON`, preserving all existing builds. Task 018 passes it as
`OFF` only in the approved RelWithDebInfo TPCH configuration.

When the option is `OFF`:

1. bundled Arrow receives `-DARROW_TESTING=OFF`;
2. the Arrow ExternalProject declares only `libarrow.a` as a byproduct;
3. Velox does not create the imported `arrow_testing` target;
4. Velox skips only tests that require `arrow_testing`;
5. all other Velox tests and test utility libraries remain enabled.

## Files

```text
CMakeLists.txt
  Declare VELOX_ENABLE_ARROW_TESTING next to VELOX_ENABLE_ARROW.

CMake/resolve_dependency_modules/arrow/CMakeLists.txt
  Forward the option to ARROW_TESTING.
  Make libarrow_testing.a, the imported arrow_testing target, and its properties
  conditional.
  Add -DARROW_ZSTD_USE_SHARED=OFF to ARROW_CMAKE_ARGS, next to
  -DARROW_WITH_ZSTD=ON (reproducibility fix; see Addendum below).

velox/vector/arrow/CMakeLists.txt
  Add VELOX_ENABLE_ARROW_TESTING to the Arrow bridge test-directory gate.

velox/dwio/parquet/writer/arrow/CMakeLists.txt
  Add VELOX_ENABLE_ARROW_TESTING to the Arrow Parquet writer test-directory
  gate.

port/task/018-filecache-gluten-benchmark-plan.md
  Pass -DVELOX_ENABLE_ARROW_TESTING=OFF in the post-018-P Parquet configure.
```

## Verification

1. Restore the original Arrow 18 `process.cc` by recreating the Arrow
   ExternalProject source from its verified archive; never retain or commit the
   stub.
2. Configure Task 018 with Parquet enabled, Velox testing enabled, and Arrow
   testing disabled.
3. Verify the Arrow sub-build has `ARROW_TESTING=OFF` and
   `ARROW_ZSTD_USE_SHARED=OFF`, both arriving from the ExternalProject
   `CMAKE_ARGS` (i.e. present in `arrow_ep-build/CMakeCache.txt` without any
   manual `cmake -D... .` edit inside that generated directory).
4. Verify its build graph contains `libarrow.a` but not
   `libarrow_testing.a` or `testing/process.cc`.
5. Build and run the Task-018 schema and `AdaptivePrefetch` tests.
6. Build `velox_tpch_benchmark`.
7. Confirm no vcpkg, Boost, downloaded Arrow source, or Gluten file changed.

## Addendum: ARROW_ZSTD_USE_SHARED reproducibility fix (2026-07-23)

**Concern.** The first Task-018-C reconfigure after this design's Arrow-testing
bypass (recreating the `arrow_ep` prefix from the verified archive) failed
during the Arrow sub-build's zstd resolution: this environment's vcpkg `zstd`
triplet exports only the `zstd::libzstd_static`/`zstd::libzstd` CMake targets,
never `zstd::libzstd_shared`, but Arrow's `ThirdpartyToolchain.cmake` defaults
`ARROW_ZSTD_USE_SHARED=ON` and fails with `Zstandard target doesn't exist:
zstd::libzstd_shared`. The workaround applied in that session — running
`cmake -DARROW_ZSTD_USE_SHARED=OFF .` directly inside the generated,
gitignored `arrow_ep-build` directory — is not reproducible: deleting and
recreating the `arrow_ep` prefix again (as Verification step 1 requires) loses
that manually-cached value and reintroduces the failure.

**Fix.** Track the flag in the tracked source instead of the generated cache:
add `-DARROW_ZSTD_USE_SHARED=OFF` to `ARROW_CMAKE_ARGS` in
`CMake/resolve_dependency_modules/arrow/CMakeLists.txt`, next to
`-DARROW_WITH_ZSTD=ON`. Bundled Arrow is always built `ARROW_BUILD_STATIC=ON`,
so a static zstd link is correct and matches what the vcpkg triplet actually
provides; no generated `CMakeCache.txt` edit is required or permitted going
forward. This is orthogonal to the Arrow-testing bypass — it applies
identically whether `ARROW_TESTING` is `ON` or `OFF` — but is recorded here
because it was discovered while validating that bypass's clean-rebuild path.

## Non-goals

```text
fixing Boost.Process compatibility for Arrow tests
changing the default Velox Arrow-testing behavior
disabling VELOX_BUILD_TESTING
changing Arrow or Boost versions
adding an Arrow Process fallback or stub
```
