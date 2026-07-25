# Task 018R Result: vcpkg `Arrow` Repair Handoff

## Worker attempt 1

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 018R
```

## Repository baselines

| Repository | Branch | HEAD (initial) | Initial dirty status |
|---|---|---|---|
| `/root/oss/clickhouse` | `ch-filecache` | `6b9bce64041bdcab20901d2ed36e988796e71301` | clean |
| `/root/oss/velox` | `filecache` | `cda6c03703cf4ed0b1b515465915dbfd599bcb6c` | clean |
| `/root/oss/gluten` | `chang/velox-vector-zerocopy` | `52dd77c6ef1a0158e2f950fd2d8cc30f1212cbc1` | clean |

Final states after this task:

| Repository | Branch | HEAD (final) | Note |
|---|---|---|---|
| `/root/oss/velox` | `filecache` | `0c5b5918eb8374f39b09248be091da94bf4d72f0` | new commit reversing `d52f069e` (Step 3) |
| `/root/oss/gluten` | `fix/vcpkg-arrow-squashed` | `c44409a7c3d8fab17ac5369b7cad8b3c80f5a437` | new commit: disable Arrow Brotli/BZ2 in vcpkg port (deviation, see below) |

Gluten switch (Step 2): `chang/velox-vector-zerocopy`@`52dd77c6` →
`fix/vcpkg-arrow-squashed`. The target tip advanced past the plan's pinned
`4b77376d` to `cb24e4f5c` via two branch-owner fix commits
(`aa0b1a1f8 [VL] Use Boost CMake packages with vcpkg`,
`cb24e4f5c [VL] Isolate pkg-config during vcpkg install`) that unblocked the
`folly`/`Boost` and pkg-config vcpkg-install failures. This task's fix commit
`c44409a7c` is on top of `cb24e4f5c`.

## Files changed

Velox — the exact four-file reverse of `d52f069e` (Step 3), committed as
`0c5b5918eb`:

```text
/root/oss/velox/CMakeLists.txt
/root/oss/velox/CMake/resolve_dependency_modules/arrow/CMakeLists.txt
/root/oss/velox/velox/dwio/parquet/writer/arrow/CMakeLists.txt
/root/oss/velox/velox/vector/arrow/CMakeLists.txt
```

Gluten — vcpkg Arrow port fix (deviation from plan), committed as `c44409a7c`:

```text
/root/oss/gluten/dev/vcpkg/ports/arrow/portfile.cmake
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| vcpkg install (`source dev/vcpkg/env.sh --build_tests=ON`), first pass after branch fixes | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/vcpkg_install_018r.log` |
| vcpkg reinstall after portfile Brotli/BZ2=OFF (Arrow rebuilt in 8.5 s) | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/vcpkg_reinstall_018r.log` |
| vcpkg Arrow build detail | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/vcpkg_arrow_018r.log` |
| CMake configure (RelWithDebInfo, Arrow=SYSTEM, `GLUTEN_VCPKG_PREFER_CONFIG=OFF`) | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/configure_018r.log` |
| CMake reconfigure after portfile fix | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/reconfigure_018r.log` |
| Build all targets (`velox`, both Arrow tests, FileCache E2E, TPCH benchmark) | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/build_018r.log` |
| Arrow writer test link-graph capture (`ninja -t commands`) | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/link_arrow_018r.log` |
| Run `velox_arrow_bridge_test` | 0 | `/root/oss/velox/_build/relwithdebinfo-vcpkg-arrow/test_arrow_bridge_018r.log` |

## Acceptance evidence

```text
test count: velox_arrow_bridge_test — 74 tests from 6 suites
failed tests: 0 (PASSED 74/74)
skipped/disabled tests: none reported
benchmark result: velox_tpch_benchmark linked (build_018r.log final line:
  "[18/18] Linking CXX executable velox/benchmarks/tpch/velox_tpch_benchmark")
git diff --check: clean (Velox HEAD~1..HEAD)
```

Built target binaries (all present, RelWithDebInfo):

```text
_build/relwithdebinfo-vcpkg-arrow/velox/vector/arrow/tests/velox_arrow_bridge_test
_build/relwithdebinfo-vcpkg-arrow/velox/dwio/parquet/writer/arrow/tests/velox_dwio_arrow_parquet_writer_test
_build/relwithdebinfo-vcpkg-arrow/velox/ch/Disks/IO/tests/velox_ch_filecache_e2e_test
_build/relwithdebinfo-vcpkg-arrow/velox/benchmarks/tpch/velox_tpch_benchmark
libvelox.a (mono lib) built
```

vcpkg Arrow paths (SYSTEM, triplet `x64-linux-avx`):

```text
/root/oss/gluten/dev/vcpkg/vcpkg_installed/x64-linux-avx/lib/libarrow.a          (33,460,080 bytes)
/root/oss/gluten/dev/vcpkg/vcpkg_installed/x64-linux-avx/lib/libarrow_testing.a  ( 2,711,928 bytes)
```

Link graph uses vcpkg Arrow only, no bundled `arrow_ep` (Step 6 checks):

```text
grep libarrow_testing.a: /vcpkg_installed/x64-linux-avx/lib/libarrow_testing.a  (found)
grep libarrow.a:         /vcpkg_installed/x64-linux-avx/lib/libarrow.a          (found)
grep -F '/arrow_ep/':    0 matches (no bundled Arrow ExternalProject)
```

Codec verification of the rebuilt `libarrow.a` (the fix):

```text
nm -C libarrow.a | grep ' U ' | grep -E 'BZ2_|Brotli' : 0 undefined symbols
ar t libarrow.a | grep compression : only
  compression.cc.o compression_lz4.cc.o compression_snappy.cc.o
  compression_zlib.cc.o compression_zstd.cc.o
  (compression_brotli.cc.o and compression_bz2.cc.o are gone)
```

Exact four-file reverse proof (Step 3):

```text
git -C /root/oss/velox diff d52f069e^ HEAD -- \
  CMake/resolve_dependency_modules/arrow/CMakeLists.txt CMakeLists.txt \
  velox/dwio/parquet/writer/arrow/CMakeLists.txt velox/vector/arrow/CMakeLists.txt
=> empty diff (HEAD restores the four files to their pre-d52f069e state)

git -C /root/oss/velox merge-base --is-ancestor d52f069e HEAD => true
(d52f069e is baked into baseline cda6c03703; commit 0c5b5918eb re-enables the
 four files as a forward commit — no rebase/amend, per repo rules)
```

## Plan deviation

The plan stated "This task does not compile or modify Gluten C++ code" and
expected a clean Velox build against the vcpkg `Arrow` port as-is. In practice
the reversed build failed to link with undefined `BZ2_bz*` / `BrotliEncoder*` /
`BrotliDecoder*` symbols from `libarrow.a`'s `compression_bz2.cc.o` and
`compression_brotli.cc.o`.

Root cause: the vcpkg Arrow overlay port inherited the upstream default
`ARROW_WITH_BROTLI=ON` / `ARROW_WITH_BZ2=ON`, but Velox links Brotli/BZ2 nowhere
else. Under `GLUTEN_VCPKG_PREFER_CONFIG=OFF` Velox resolves Arrow through
find-module mode (`CMake/FindArrow.cmake`), which builds a bare imported target
that does not propagate Arrow's transitive compression dependencies (unlike
config-mode `ArrowConfig.cmake`'s `ARROW_SYSTEM_DEPENDENCIES`). LZ4/Snappy/
ZLIB/ZSTD already resolve because Velox links them directly; only Brotli/BZ2
were left unresolved.

Evidence that Gluten+Velox never needs Arrow Brotli/BZ2:

- Velox's bundled Arrow enables only LZ4/Snappy/ZLIB/ZSTD
  (`CMake/resolve_dependency_modules/arrow/CMakeLists.txt`).
- `arrowCompressionTypeToVelox` (`cpp/velox/utils/VeloxArrowUtils.cc`) maps only
  UNCOMPRESSED/LZ4_FRAME/ZSTD/GZIP/SNAPPY/LZO.
- Parquet write with `brotli` throws
  "Gluten+velox does not support write parquet using brotli"
  (`cpp/velox/utils/VeloxWriterUtils.cc`). GZIP uses zlib, which stays ON.

Fix (this task, deviation): disable the two unused codecs in the vcpkg port so
its `libarrow.a` matches Velox's bundled Arrow —
`dev/vcpkg/ports/arrow/portfile.cmake`: `ARROW_WITH_BROTLI=OFF`,
`ARROW_WITH_BZ2=OFF`. This is a Gluten *vcpkg port* (CMake build recipe) change,
not Gluten C++ source; no supported compression path is affected. The Velox
reverse and this Gluten fix are coupled: the Velox reverse alone would not link
without the Brotli/BZ2-free `libarrow.a`.

A Velox-side alternative (making `CMake/FindArrow.cmake` propagate the codec
dependencies) was prototyped and then reverted in favor of the vcpkg-layer fix,
which keeps the dead codecs out of the artifact entirely.

## Worker review

```text
review subagent: build/link/test verification via targeted commands (no
  separate review subagent; changes are CMake/portfile flags, not C++ logic)
findings:
  - reversed build failed to link (undefined BZ2_/Brotli symbols) — root-caused
    to vcpkg Arrow codecs unused by Gluten+Velox.
resolutions:
  - disabled Arrow Brotli/BZ2 in the vcpkg port; rebuilt Arrow (8.5 s); all 5
    targets link; velox_arrow_bridge_test passes 74/74; link graph uses vcpkg
    Arrow with no arrow_ep; libarrow.a has 0 undefined codec symbols.
unresolved findings: none
```

## Blockers

```text
None. All Step 1-6 acceptance criteria are green (with the vcpkg codec fix
substituting for the plan's expected as-is clean build).
```

## Worker declaration

```text
Only Task 018R was attempted.
Deviation from the standard worker-no-commit flow: at the user's (branch owner's)
explicit direction, the changes were committed rather than left unstaged:
  - Velox reverse committed as 0c5b5918eb on branch filecache
    (subject "Task 018R: Revert temporary ...").
  - Gluten vcpkg port fix committed as c44409a7c on branch fix/vcpkg-arrow-squashed
    and exported as a patch to
    /root/chang/OneDrive/share_data/local-cache/0001-VL-Disable-Arrow-Brotli-BZ2-codecs-in-vcpkg-port-to-.patch
No push was performed. The ClickHouse receipt remains for the Controller to
review and commit. The worker stopped after writing this receipt.
```

## Controller review 1

```text
controller_status: accepted
environment_profile: root-oss
task: 018R
review_authority: user manual review
```

## Review evidence

```text
scope review:
  The Velox commit changes exactly the four files owned by the reverse of
  d52f069e9b and restores each file byte-for-byte to d52f069e9b^.

implementation review:
  The Gluten vcpkg port disables Arrow Brotli/BZ2 support that is not exposed by
  the Gluten Velox backend. This removes the unresolved static codec symbols
  while retaining LZ4, Snappy, zlib/GZIP, and ZSTD.

dependency provenance:
  CMake resolves Arrow_SOURCE=SYSTEM to vcpkg libarrow.a and
  libarrow_testing.a. The build and link graph contain no bundled arrow_ep.

log and build review:
  The fresh RelWithDebInfo build linked all five task targets. The task contract
  requires compilation/linkage, not execution of every built test binary.
  velox_arrow_bridge_test was additionally run and passed 74/74.

review findings:
  Two deviations were identified: implementation commits were created by the
  Worker, and the successful build required the additional Gluten vcpkg codec
  commit on top of the advanced target branch. The user explicitly accepted
  both deviations by manual review on 2026-07-25.

unresolved findings:
  None.
```

## Required changes

```text
None.
```

## Commits

| Repository | Commit |
|---|---|
| `/root/oss/velox` | `0c5b5918eb8374f39b09248be091da94bf4d72f0` |
| `/root/oss/gluten` | `c44409a7c3d8fab17ac5369b7cad8b3c80f5a437` |

```text
gluten_push: prohibited_by_user
next_task: 017B
next_task_plan: port/task/017b-filecache-logging-exception-stack-plan.md
task_017b_implementation_authorized: true
```
