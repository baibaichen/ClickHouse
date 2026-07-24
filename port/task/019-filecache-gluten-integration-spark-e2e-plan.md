# Task 019 — Gluten `FileCache` Integration + Spark End-to-End — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate the accepted Velox `FileCache` into Gluten end-to-end — a Velox baseline compatible with the selected Gluten commit, `VeloxBackend`/`FileCacheManager` lifecycle and config, `GlutenBufferedInputBuilder` selection, the `fileCacheWriteBytes` C++/JNI/Java/Scala/Spark metric bridge, a native Builder/lifecycle miss-fill-hit test, and a Spark → Gluten → Velox → `FileCache` correctness and performance suite.

**Architecture:** Task 019 is the full Gluten integration owner (Task 018 is Velox-only). All Gluten C++/JNI/Java/Scala changes live in an isolated Gluten worktree (`/root/oss/gluten-019`, branch `task-019-filecache-gluten`) built against a Velox baseline that contains both the accepted `FileCache` commits and every Velox core API the selected Gluten commit (`4017ec94d`) requires. `FileCache` is installed as a process singleton during `VeloxBackend::init`, selected ahead of `AsyncDataCache` inside the Builder, and its write-byte counter is propagated out through `IoStats` → `RuntimeMetric` → Gluten JNI → Java → Scala → Spark `SQLMetric`. Establishing the compatible Velox baseline is a hard gate (019-A) that produces a reviewed compatibility contract consumed by every later task.

**Tech Stack:** C++20 (Velox / Gluten C++, `CMAKE_CXX_STANDARD 20`), CMake/Ninja, vcpkg, GoogleTest, folly (futures/`CancellationToken`/`ThreadWheelTimekeeper`), Java 8/11 (Gluten JNI), Scala 2.12 (Gluten Spark), Maven/ScalaTest, Spark 3.5.

## Global Constraints

```text
Environment profile: root-oss (port/task/ENVIRONMENT.md). Name it and use only its values.
Binding design: port/design/filecache-task-018-019-hard-split.md.
Task 019 is the full Gluten integration owner. Task 018 is Velox-only and does not build Gluten.
Prerequisites (all accepted before dispatch): Task 017A, the Velox-only Task 018
four-driver addendum, Review 5, and Task 017B.
019-A (compatible Velox baseline) is a HARD GATE. 019-B..019-F do not start until 019-A is green.
No Gluten source fallback, feature removal, or out-of-scope compatibility shim may hide a mismatched Velox baseline.
Worker never stages or commits; Controller reviews and commits accepted work.
Never rebase or amend; corrective work uses new commits only.
The dirty /root/oss/gluten checkout (branch main) is never modified. Task 019 uses an isolated worktree.
No C++ sleeps to fix races. Allman braces in C++.
No -j argument to ninja; no nproc. Let ninja/maven decide parallelism.
All build/test output is redirected to a log file in the build directory; a subagent analyzes each log and returns a concise summary.
All temp/cache directories use tmp/ relative to CWD or a build/target directory; never /tmp.
The 019-F state (performance) benchmark must run against a RelWithDebInfo or Release Velox+Gluten build. Debug native output is invalid as performance evidence.
```

---

## Consumed Contract

### Accepted Task 017A Velox API (consumed, not implemented here)

Confirmed present on the accepted `FileCache` Velox branch. 019-A re-verifies these signatures on the compatible baseline and records them in its contract; 019-B..019-F consume exactly these.

`velox/ch/Common/FileCacheStats.h`:

```cpp
namespace facebook::velox::ch
{
/// RuntimeMetric key for bytes written to the FileCache.
inline constexpr const char * kFileCacheWriteBytes = "fileCacheWriteBytes";

/// Point-in-time snapshot of FileCache gauges + cumulative counters.
struct FileCacheStatsSnapshot
{
    // Gauges (from CurrentMetrics)
    int64_t cacheSize = 0;
    int64_t cacheSizeLimit = 0;
    int64_t cacheKeys = 0;
    int64_t cacheElements = 0;
    int64_t cacheFileSegments = 0;
    int64_t holdFileSegments = 0;
    int64_t invalidatedElements = 0;
    int64_t priorityQueueElements = 0;
    int64_t downloadQueueElements = 0;
    int64_t delayedCleanupElements = 0;
    int64_t reserveThreads = 0;

    // Cumulative counters (from ProfileEvents)
    uint64_t cacheReadBytes = 0;
    uint64_t sourceReadBytes = 0;
    uint64_t cacheWriteBytes = 0;
    uint64_t cacheHitCount = 0;
    uint64_t cacheMissCount = 0;
    uint64_t predownloadedFromSourceBytes = 0;
    uint64_t predownloadedBytes = 0;
    uint64_t reserveAttempts = 0;
    uint64_t reserveFailures = 0;
    uint64_t evictedBytes = 0;
    uint64_t evictedSegments = 0;
    uint64_t evictionTries = 0;
    uint64_t waitReadBufferMicroseconds = 0;
    uint64_t readFromSourceMicroseconds = 0;
    uint64_t predownloadedFromSourceMicroseconds = 0;
    uint64_t readFromCacheMicroseconds = 0;
    uint64_t cacheWriteMicroseconds = 0;
    uint64_t createBufferMicroseconds = 0;

    /// Subtract a previous snapshot to get deltas for cumulative counters.
    FileCacheStatsSnapshot operator-(const FileCacheStatsSnapshot & prev) const;
};

/// Loads a point-in-time snapshot from the global metrics storage.
FileCacheStatsSnapshot takeFileCacheStatsSnapshot();
} // namespace facebook::velox::ch
```

`IoStats::addCounter` (`velox/common/file/File.h:57`), used by `FileCacheInputStream` (Task 017A code):

```cpp
void addCounter(const std::string& name, RuntimeCounter counter);
// ioStats_->addCounter(std::string(kFileCacheWriteBytes),
//     RuntimeCounter(bytesWritten, RuntimeCounter::Unit::kBytes));
```

`FileCacheBufferedInput` constructor (complete Task-017A order, `velox/ch/Disks/IO/FileCacheBufferedInput.h`):

```cpp
FileCacheBufferedInput(
    std::shared_ptr<ReadFile> readFile,
    FileCachePtr cache,
    FileCacheKey cacheKey,
    FileCacheOriginInfo origin,
    FileCacheReadOptions cacheOptions,
    FileCacheRequestContext requestContext,
    const dwio::common::MetricsLogPtr & metricsLog,
    std::shared_ptr<io::IoStatistics> ioStatistics,
    std::shared_ptr<velox::IoStats> ioStats,
    folly::Executor * executor,
    const dwio::common::ReaderOptions & readerOptions,
    folly::F14FastMap<std::string, std::string> fileReadOps = {},
    folly::CancellationToken cancellationToken = {});

// Public accessor produced by Task 017A (consumed by the 019-C builder test):
const folly::CancellationToken & cancellationToken() const;
```

`ConnectorQueryCtx::cancellationToken` (`velox/connectors/Connector.h:558`):

```cpp
const folly::CancellationToken& cancellationToken() const { return cancellationToken_; }
```

### Accepted Velox-only Task 018 (consumed)

The accepted Velox-only Task 018 (correctness harness, dedicated `FileCacheBufferedInput` micro/wrapper, sentinel-safe orchestration, non-TPCH and TPCH baselines) is a prerequisite. Task 019 does not rebuild or re-review it; it consumes the accepted `FileCache` Velox branch as the FileCache half of the 019-A compatible baseline. The byte-level FileCache write correctness mutation lives in the accepted Velox Task 018 harness (`velox/dwio/common/benchmarks/CacheReadHarness.cpp`); Task 017A owns the `FileCacheInputStream.cpp` `ioStats_->addCounter` line. Task 019 owns no line in either.

---

## Subtask Decomposition

| ID | Title | Scope | Hard Gate | BLOCKED if |
|---|---|---|---|---|
| 019-A | Compatible Velox baseline for the selected Gluten commit | Velox repo + Gluten worktree | Gluten `cpp` (`velox` + `gluten` targets) fully compiles/links against the constructed baseline **and** the `FileCache` API surface is confirmed; reviewed compatibility contract produced | any prerequisite not accepted |
| 019-B | Gluten lifecycle + config (fail-close) | Gluten worktree | `velox_file_cache_support_test` + `velox_file_cache_gluten_lifecycle_test` green (incl. one real cold `FileCacheBufferedInput` read) | 019-A not green |
| 019-C | Gluten Builder adapter | Gluten worktree | `dynamic_cast<ch::FileCacheBufferedInput*>` succeeds; direct/CBI/FCBI + copied-token tests green | 019-B not green |
| 019-D | Complete Gluten metric bridge | Gluten worktree | native `sumRuntimeMetric` + Java carrier + Scala updater tests green; JNI/Java/Scala compile with the new arity | 019-C not green |
| 019-E | Native Gluten Builder/lifecycle miss-fill-hit E2E | Gluten worktree | `velox_file_cache_e2e_gluten_test` green (miss→fill→hit, FileCache precedence, teardown order) | 019-D not green |
| 019-F | Spark → Gluten → Velox → FileCache correctness + performance E2E | Gluten worktree + Spark | `VeloxFileCacheSuite` green: Gluten result equals vanilla Spark, `fileCacheWriteBytes` `SQLMetric` > 0 on cold fill, warm run shows cache hits, negative control stays 0; non-Debug state benchmark recorded | 019-E not green |

Execution order:

```text
019-A -> 019-B -> 019-C -> 019-D -> 019-E -> 019-F
```

019-A is a hard gate. If a compatible Velox baseline cannot be constructed without a Gluten source fallback, feature removal, or out-of-scope shim, STOP the whole Task-019 pipeline and redispatch to design (see 019-A Step 8). Do not proceed to 019-B on a mismatched baseline.

---

## Environment Setup

Fixed values for the `root-oss` profile:

```text
<clickhouse_repo>      = /root/oss/clickhouse
<velox_repo>           = /root/oss/velox
<accepted_fc_branch>   = filecache
<accepted_fc_head>     = resolved from the accepted Task-018 receipt at execution time
<compat_velox_worktree> = /root/oss/velox-gluten-compat
<gluten_repo>          = /root/oss/gluten      (dirty branch main @ 4017ec94d; NEVER modified)
<gluten_commit>        = 4017ec94d             (the selected Gluten baseline)
<compat_velox_branch>  = filecache-gluten-compat   (constructed in 019-A)
<compat_velox_build>   = /root/oss/velox-gluten-compat/_build/relwithdebinfo
<gluten_worktree>      = /root/oss/gluten-019
<gluten_branch>        = task-019-filecache-gluten
<velox_env>            = /root/oss/velox-helper/env.sh
```

> **Environment setup:** Before any configure/build/test command, `source /root/oss/velox-helper/env.sh` and use `-DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE"` (`/root/oss/gluten/dev/vcpkg/toolchain.cmake`), `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`, `-DVELOX_GFLAGS_TYPE=static`, `-DVELOX_BUILD_TESTING=ON`. Never pass `-j`.

### Isolated Gluten Task-019 worktree (rename-in-place of the 018 WIP)

The uncommitted former-018-E files in `/root/oss/gluten-018` are preserved as Task-019 work in progress. They are **not** accepted Task-018 changes. Move the worktree and rename its branch to Task-019 naming with no rebase and no amend; the uncommitted WIP moves with the working tree.

```bash
cd /root/oss/gluten
git --no-pager worktree list
# Move the existing worktree; uncommitted WIP is carried along.
git worktree move /root/oss/gluten-018 /root/oss/gluten-019
# Rename the branch in place (no history rewrite).
git -C /root/oss/gluten-019 branch -m task-018-filecache task-019-filecache-gluten
git -C /root/oss/gluten-019 --no-pager status --short --branch
```

This leaves the dirty `/root/oss/gluten` (branch `main`) untouched. If `/root/oss/gluten-018` no longer exists (fresh environment), create the worktree directly:

```bash
cd /root/oss/gluten
git worktree add -b task-019-filecache-gluten /root/oss/gluten-019 4017ec94d
```

Configure the worktree against the compatible Velox baseline built in 019-A (do this only after 019-A is green):

```bash
cd /root/oss/gluten-019
source /root/oss/velox-helper/env.sh
cmake -S cpp -B cpp/build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
  -DVELOX_HOME=/root/oss/velox-gluten-compat \
  -DVELOX_BUILD_PATH=/root/oss/velox-gluten-compat/_build/relwithdebinfo \
  > cpp/build/configure_019.log 2>&1
echo "exit: $?"
```

---

## Task 019-A: Compatible Velox Baseline (Hard Gate)

**Files:**
- Read: `/root/oss/gluten/ep/build-velox/src/get-velox.sh` (the Velox pin: `VELOX_REPO`, `VELOX_BRANCH`)
- Construct: `/root/oss/velox-gluten-compat` branch `filecache-gluten-compat` (new commits only; no rebase, no amend)
- Build: `/root/oss/velox-gluten-compat/_build/relwithdebinfo` (compatible Velox library)
- Build: `/root/oss/gluten-019/cpp/build` (Gluten `velox` + `gluten` targets)
- Produce: `<clickhouse_repo>/port/task/result/019a-compatible-velox-baseline-contract.md` (the reviewed compatibility contract)

**Interfaces:**
- Consumes: accepted `FileCache` Velox branch `filecache`; the Gluten-pinned Velox baseline (`IBM/velox` tag `dft-2026_07_03`); Gluten `4017ec94d` C++ sources
- Produces: the compatible Velox branch and the **reviewed compatibility contract** that 019-B..019-F consume — the confirmed `FileCache` API surface (`FileCacheManager` methods, `FileCacheBufferedInput` constructor + `cancellationToken` accessor, `takeFileCacheStatsSnapshot`) and the confirmed Gluten-required Velox core API surface

**Why this is a hard gate.** Gluten `4017ec94d` expects Velox core APIs from its paired `IBM/velox` fork, while the accepted `FileCache` lives on the `baibaichen` Velox branch `filecache`. Building Gluten `cpp` against `/root/oss/velox` (`filecache`) fails to compile Gluten's own operators. The observed failure is `cpp/velox/operators/hashjoin/HashTableSerializer.cc` calling `HashTable<T>::serializedSize` / `serializeTo` / `deserializeFrom`, which do not exist on the `FileCache` Velox baseline. 019-B..019-F must not run against this mismatched baseline, and no Gluten source fallback, feature removal, or shim may hide the mismatch.

- [ ] **Step 1: Verify all four prerequisites are accepted**

```bash
cd /root/oss/clickhouse
grep -n 'controller_status: accepted' \
  port/task/result/017a-filecache-statistics-cancellation-result.md
grep -n 'controller_status: accepted' \
  port/task/result/018-filecache-velox-benchmark-result.md
grep -n 'review_status: accepted' \
  port/task/fullreview/root-oss/5/003-018-whole-port-review.md
grep -n 'controller_status: accepted' \
  port/task/result/017b-filecache-logging-exception-stack-result.md
ACCEPTED_TASK_018_HEAD=$(
  sed -n 's/^velox_head: //p' \
    port/task/result/018-filecache-velox-benchmark-result.md | tail -1
)
test -n "$ACCEPTED_TASK_018_HEAD"
export ACCEPTED_TASK_018_HEAD
```

Expected: each grep prints one accepted line and none is later reopened. Stop the task if any is missing.

- [ ] **Step 2: Record the two source baselines exactly**

```bash
# The Velox baseline name in the selected Gluten checkout.
grep -nE '^VELOX_REPO=|^VELOX_BRANCH=' /root/oss/gluten/ep/build-velox/src/get-velox.sh
# Expected name at authoring time (the public ref is a tag, not a head):
#   VELOX_REPO=https://github.com/IBM/velox.git
#   VELOX_BRANCH=dft-2026_07_03
git ls-remote https://github.com/IBM/velox.git refs/tags/dft-2026_07_03

# The accepted FileCache Velox baseline.
git -C /root/oss/velox --no-pager status --short --branch | head -1   # expected branch: filecache
git -C /root/oss/velox --no-pager log -1 --oneline                    # accepted FileCache HEAD

# The selected Gluten commit.
git -C /root/oss/gluten --no-pager log -1 --oneline                   # expected: 4017ec94d
```

Record the FileCache branch/HEAD, IBM tag/SHA, and Gluten HEAD verbatim in the
contract (Step 9).

- [ ] **Step 3: Reproduce the observed blocker and enumerate every missing Velox API**

Configure the Task-019 worktree against the accepted `FileCache` Velox first, then build the `velox` target with keep-going (`-k 0`, not a parallelism flag) so a single pass collects **all** missing-symbol errors, not just the first:

```bash
cd /root/oss/gluten-019
source /root/oss/velox-helper/env.sh
cmake -S cpp -B cpp/build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
  -DVELOX_HOME=/root/oss/velox \
  -DVELOX_BUILD_PATH=/root/oss/velox/_build/relwithdebinfo \
  > cpp/build/configure_019a_reproduce.log 2>&1
ninja -C cpp/build -k 0 velox > cpp/build/build_019a_reproduce_blocker.log 2>&1
echo "exit: $?"
grep -nE "error:|no member named|no type named|is not a member of" \
  cpp/build/build_019a_reproduce_blocker.log | sort -u > cpp/build/019a_missing_apis.txt
```

A subagent analyzes `build_019a_reproduce_blocker.log` and returns the complete list of missing Velox symbols. The observed set includes at minimum:

```text
facebook::velox::exec::HashTable<T>::serializedSize      (HashTableSerializer.cc)
facebook::velox::exec::HashTable<T>::serializeTo         (HashTableSerializer.cc)
facebook::velox::exec::HashTable<T>::deserializeFrom     (HashTableSerializer.cc)
OpaqueHashTable                                          (HashTable serialization surface)
Parquet reader/writer session key                        (Parquet connector)
Iceberg split/data-source constructor surface            (compute/iceberg/IcebergPlanConverter.cc)
```

Confirm each named API is genuinely absent on the `FileCache` baseline (proves the mismatch is real, not a build-flag artifact):

```bash
grep -rn "serializedSize\|serializeTo\|deserializeFrom" /root/oss/velox/velox/exec/HashTable.h || echo "ABSENT: HashTable serialization"
grep -rn "OpaqueHashTable" /root/oss/velox/velox || echo "ABSENT: OpaqueHashTable"
```

Add any additional missing symbol reported by the full `-k 0` build to the contract's required-API list.

- [ ] **Step 4: Construct the compatible baseline branch (merge, no rebase)**

Create a separate Velox worktree/branch that contains **both** the accepted
`FileCache` commits and the `IBM/velox` tag `dft-2026_07_03` APIs, using merge
commits only. Never switch `/root/oss/velox` away from its accepted `filecache`
branch and never rebase/amend accepted history:

```bash
cd /root/oss/velox
git fetch https://github.com/IBM/velox.git \
  refs/tags/dft-2026_07_03:refs/tags/ibm-dft-2026_07_03
git worktree add -b filecache-gluten-compat \
  /root/oss/velox-gluten-compat \
  "${ACCEPTED_TASK_018_HEAD:?set from the accepted Task-018 receipt}"
cd /root/oss/velox-gluten-compat
mkdir -p _build
git merge --no-edit refs/tags/ibm-dft-2026_07_03 \
  > _build/merge_019a.log 2>&1
echo "merge exit: $?"
git --no-pager status --short | head -40
```

Resolve conflicts by **keeping both** the `FileCache` `velox/ch/**` implementation and the `IBM` core APIs (`HashTable` serialization, `OpaqueHashTable`, Parquet session key, Iceberg constructor). Do not delete or weaken any `FileCache` feature and do not stub any `IBM` API. Commit only the merge resolution as new commits.

- [ ] **Step 5: Build the compatible Velox library (RelWithDebInfo)**

```bash
cd /root/oss/velox-gluten-compat
source /root/oss/velox-helper/env.sh
cmake -S . -B _build/relwithdebinfo -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DVELOX_ENABLE_PARQUET=ON \
  -DVELOX_BUILD_TESTING=ON \
  -DVELOX_MONO_LIBRARY=ON \
  -DVELOX_GFLAGS_TYPE=static \
  > _build/relwithdebinfo/configure_019a.log 2>&1
ninja -C _build/relwithdebinfo velox > _build/relwithdebinfo/build_019a_compat_velox.log 2>&1
echo "exit: $?"
```

- [ ] **Step 6: Build Gluten against the compatible baseline (must fully compile and link)**

Reconfigure the worktree at the RelWithDebInfo compatible build and build the previously failing `velox` target plus the full `gluten` library:

```bash
cd /root/oss/gluten-019
source /root/oss/velox-helper/env.sh
cmake -S cpp -B cpp/build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
  -DVELOX_HOME=/root/oss/velox-gluten-compat \
  -DVELOX_BUILD_PATH=/root/oss/velox-gluten-compat/_build/relwithdebinfo \
  > cpp/build/configure_019a_compat.log 2>&1
ninja -C cpp/build gluten > cpp/build/build_019a_gluten_native.log 2>&1
echo "exit: $?"
```

A subagent analyzes `build_019a_gluten_native.log`. Expected: `HashTableSerializer.cc`, `compute/iceberg/IcebergPlanConverter.cc`, and the Parquet connector all compile; the `gluten` library links.

- [ ] **Step 7: Confirm the FileCache API surface survived the merge**

The merge must not have dropped the `FileCache` half. Confirm the symbols 019-B..019-F consume are present on the compatible baseline:

```bash
grep -n "class FileCacheManager" /root/oss/velox-gluten-compat/velox/ch/Interpreters/FileCache/FileCacheManager.h
grep -nE "create|setInstance|getInstance|getDefault|commonUserId|shutdown|refreshStats" \
  /root/oss/velox-gluten-compat/velox/ch/Interpreters/FileCache/FileCacheManager.h
grep -n "cancellationToken" /root/oss/velox-gluten-compat/velox/ch/Disks/IO/FileCacheBufferedInput.h
grep -n "takeFileCacheStatsSnapshot" /root/oss/velox-gluten-compat/velox/ch/Common/FileCacheStats.h
```

Expected: each symbol resolves. Any signature that differs from the Consumed Contract above is recorded in the contract and consumed as-is by later tasks (do not guess; if a signature differs, stop and redispatch — Step 8).

- [ ] **Step 8: Hard-gate decision (stop / redispatch, not a deferred placeholder)**

- If Steps 5–7 all succeed: 019-A is **green**. Proceed to write the contract (Step 9), then 019-B.
- If a compatible baseline cannot be built without (a) editing Gluten source to route around a missing Velox API, (b) removing or weakening a `FileCache` feature, or (c) adding an out-of-scope compatibility shim: **STOP the entire Task-019 pipeline.** Record the exact unresolved API/conflict and the first actionable error in the contract with `disposition: blocked`, and redispatch to design (`port/design/filecache-task-018-019-hard-split.md` §5). Do not start 019-B on a mismatched baseline.

- [ ] **Step 9: Write the reviewed compatibility contract**

After green Steps 5–7, create the contract from verified runtime values:

```bash
COMPAT_HEAD=$(git -C /root/oss/velox-gluten-compat rev-parse HEAD)
FILECACHE_HEAD=$ACCEPTED_TASK_018_HEAD
IBM_TAG_SHA=$(git -C /root/oss/velox-gluten-compat rev-parse refs/tags/ibm-dft-2026_07_03)
GLUTEN_HEAD=$(git -C /root/oss/gluten rev-parse HEAD)
CONTRACT=/root/oss/clickhouse/port/task/result/019a-compatible-velox-baseline-contract.md
cat > "$CONTRACT" <<EOF
# Task 019-A Compatible Velox Baseline Contract

status: green
compatible_velox: /root/oss/velox-gluten-compat branch filecache-gluten-compat HEAD ${COMPAT_HEAD}
accepted_filecache: /root/oss/velox branch filecache HEAD ${FILECACHE_HEAD}
gluten_velox_pin: IBM/velox tag dft-2026_07_03 SHA ${IBM_TAG_SHA}
selected_gluten: /root/oss/gluten HEAD ${GLUTEN_HEAD}

## Confirmed Gluten-required Velox APIs

- HashTable serialization: serializedSize / serializeTo / deserializeFrom
- OpaqueHashTable
- Parquet session key
- Iceberg constructor surface

## Confirmed FileCache APIs

- FileCacheManager create/setInstance/getInstance/getDefault/commonUserId/shutdown/refreshStats and Options fields
- FileCacheBufferedInput 13-parameter constructor ending in cancellationToken and cancellationToken accessor
- takeFileCacheStatsSnapshot and FileCacheStatsSnapshot

## Build directories

- /root/oss/velox-gluten-compat/_build/relwithdebinfo
- /root/oss/gluten-019/cpp/build

## Logs

- reproduce: /root/oss/gluten-019/cpp/build/build_019a_reproduce_blocker.log
- compatible Velox: /root/oss/velox-gluten-compat/_build/relwithdebinfo/build_019a_compat_velox.log
- Gluten native: /root/oss/gluten-019/cpp/build/build_019a_gluten_native.log

recommended_next_task: 019-B
EOF
{
  printf '\n## Observed mismatch evidence\n\n```\n'
  cat /root/oss/gluten-019/cpp/build/019a_missing_apis.txt
  printf '```\n'
} >> "$CONTRACT"
```

If Step 8 is blocked, write the same file with `status: blocked`, omit
`recommended_next_task: 019-B`, add the exact first actionable error and
unresolved API/conflict, then stop. The blocked contract must not claim any API
was confirmed.

**Gate:** Gluten `cpp` (`velox` + `gluten`) fully compiles and links against `filecache-gluten-compat`; the `FileCache` API surface is confirmed present; the contract is written and reviewed. No Gluten source was edited to work around a missing Velox API; no `FileCache` feature was removed.

---

## Task 019-B: Gluten Lifecycle + Config (Fail-Close)

> Consumes the 019-A compatibility contract. Do not start until 019-A is green. All paths are in `/root/oss/gluten-019`.

**Files (in `/root/oss/gluten-019`):**
- Modify: `cpp/velox/config/VeloxConfig.h` — add the five FileCache config keys
- Create: `cpp/velox/compute/FileCacheSupport.h` — testable `validateFileCacheConfig` + `buildFileCacheManager`
- Create: `cpp/velox/compute/FileCacheSupport.cc`
- Modify: `cpp/velox/CMakeLists.txt` — add `compute/FileCacheSupport.cc` to `VELOX_SRCS`
- Modify: `cpp/velox/compute/VeloxBackend.h` — add `fileCacheManager_` + `fileCacheMemoryPool_` + `fileCacheTimekeeper_` fields and `initFileCache()` declaration
- Modify: `cpp/velox/compute/VeloxBackend.cc` — implement `initFileCache()`, add mutual-exclusion dispatch in `init()`, extend `tearDown()`
- Create: `cpp/velox/tests/FileCacheSupportTest.cc` — helper unit tests (no `VeloxBackend`, no global re-init)
- Create: `cpp/velox/tests/FileCacheGlutenLifecycleTest.cc` — one end-to-end lifecycle test through `VeloxBackend` with a real cold read
- Modify: `cpp/velox/tests/CMakeLists.txt` — register both test targets

**Interfaces:**
- Consumes (from the 019-A contract): `FileCacheManager::create`, `setInstance`, `getInstance`, `getDefault`, `shutdown`, `refreshStats`; `FileCacheManager::Options` fields `localFileSystem`/`memoryPool`/`timekeeper`; `VeloxMemoryManager::getAggregateMemoryPool` (`cpp/velox/memory/VeloxMemoryManager.h:70`); `AllocationListener::noop` (`cpp/core/memory/AllocationListener.h:28`); `filesystems::getFileSystem`; `folly::ThreadWheelTimekeeper`; `takeFileCacheStatsSnapshot`
- Produces: `gluten::validateFileCacheConfig`, `gluten::buildFileCacheManager`, `VeloxBackend::initFileCache`, the five config keys

**All five approved configuration keys (exact, `namespace gluten`, after `kVeloxSsdCheckSumReadVerificationEnabled` at `VeloxConfig.h:148`):**

```cpp
const std::string kVeloxFileCacheEnabled =
    "spark.gluten.sql.columnar.backend.velox.fileCacheEnabled";
const bool kVeloxFileCacheEnabledDefault = false;

const std::string kVeloxFileCachePath =
    "spark.gluten.sql.columnar.backend.velox.fileCachePath";

const std::string kVeloxFileCacheSize =
    "spark.gluten.sql.columnar.backend.velox.fileCacheSize";
const uint64_t kVeloxFileCacheSizeDefault = 10ULL << 30;

const std::string kVeloxFileCacheMaxSegmentSize =
    "spark.gluten.sql.columnar.backend.velox.fileCacheMaxSegmentSize";
const uint64_t kVeloxFileCacheMaxSegmentSizeDefault = 8ULL << 20;

const std::string kVeloxFileCacheBackgroundDownloadThreads =
    "spark.gluten.sql.columnar.backend.velox.fileCacheBackgroundDownloadThreads";
const uint64_t kVeloxFileCacheBackgroundDownloadThreadsDefault = 4;
```

**Testable helper (`cpp/velox/compute/FileCacheSupport.h`):**

Factored out of `VeloxBackend` so the config→manager path is unit-testable without constructing a `VeloxBackend` (which would re-init glog + the global Velox memory manager on every test — invalid global state). `VeloxBackend` becomes a thin caller that supplies production values. The timekeeper is forward-declared so the header pulls in no folly futures headers.

```cpp
#pragma once

#include <memory>

namespace folly { class Timekeeper; }

namespace facebook::velox {
namespace config { class ConfigBase; }
namespace memory { class MemoryPool; }
namespace filesystems { class FileSystem; }
namespace ch { class FileCacheManager; }
} // namespace facebook::velox

namespace gluten {

/// Throws (`VELOX_USER_FAIL`) if the FileCache config is invalid: both
/// AsyncDataCache and FileCache enabled, or FileCache enabled with empty path.
/// No-op when FileCache is disabled. Called before either cache is allocated.
void validateFileCacheConfig(const facebook::velox::config::ConfigBase& conf);

/// Builds and initializes a `FileCacheManager` from `conf`, mapping all five
/// keys into `FileCacheConfig`/`Options`. Returns nullptr if FileCache is
/// disabled. Does NOT install the process singleton. The caller keeps
/// `memoryPool`/`localFileSystem`/`timekeeper` alive for the manager's lifetime.
std::shared_ptr<facebook::velox::ch::FileCacheManager> buildFileCacheManager(
    const facebook::velox::config::ConfigBase& conf,
    facebook::velox::memory::MemoryPool* memoryPool,
    std::shared_ptr<facebook::velox::filesystems::FileSystem> localFileSystem,
    std::shared_ptr<folly::Timekeeper> timekeeper);

} // namespace gluten
```

**`cpp/velox/compute/FileCacheSupport.cc`:**

```cpp
#include "compute/FileCacheSupport.h"

#include "config/VeloxConfig.h"

#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/common/config/Config.h"
#include "velox/common/memory/MemoryPool.h"

#include <filesystem>

namespace gluten {

void validateFileCacheConfig(const facebook::velox::config::ConfigBase& conf) {
  if (!conf.get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    return;
  }
  // Mutual exclusion: reject both caches enabled BEFORE either is allocated.
  VELOX_USER_CHECK(
      !conf.get<bool>(kVeloxCacheEnabled, false),
      "Cannot enable both AsyncDataCache ({}) and FileCache ({}) simultaneously",
      kVeloxCacheEnabled,
      kVeloxFileCacheEnabled);
  const auto path = conf.get<std::string>(kVeloxFileCachePath, "");
  VELOX_USER_CHECK(
      !path.empty(), "{} must be set when FileCache is enabled", kVeloxFileCachePath);
}

std::shared_ptr<facebook::velox::ch::FileCacheManager> buildFileCacheManager(
    const facebook::velox::config::ConfigBase& conf,
    facebook::velox::memory::MemoryPool* memoryPool,
    std::shared_ptr<facebook::velox::filesystems::FileSystem> localFileSystem,
    std::shared_ptr<folly::Timekeeper> timekeeper) {
  validateFileCacheConfig(conf);
  if (!conf.get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    return nullptr;
  }
  VELOX_CHECK_NOT_NULL(memoryPool);
  VELOX_CHECK(
      memoryPool->kind() == facebook::velox::memory::MemoryPool::Kind::kLeaf,
      "FileCache requires a leaf memory pool for direct buffer allocations");

  // Stable, dedicated, absolute directory (no random cache.<uuid> suffix): the
  // FileCache reloads metadata across restarts.
  const auto cachePath =
      std::filesystem::absolute(conf.get<std::string>(kVeloxFileCachePath, "")).string();
  const auto cacheSize = conf.get<uint64_t>(kVeloxFileCacheSize, kVeloxFileCacheSizeDefault);
  const auto maxSegmentSize =
      conf.get<uint64_t>(kVeloxFileCacheMaxSegmentSize, kVeloxFileCacheMaxSegmentSizeDefault);
  const auto bgThreads = conf.get<uint64_t>(
      kVeloxFileCacheBackgroundDownloadThreads, kVeloxFileCacheBackgroundDownloadThreadsDefault);

  facebook::velox::ch::FileCacheConfig cfg;
  cfg.path = cachePath;
  cfg.maxSize = cacheSize; // logical upper bound; no all-free-space precheck.
  cfg.maxFileSegmentSize = maxSegmentSize;
  cfg.backgroundDownloadThreads = bgThreads; // maps the 5th key into FileCacheConfig.

  facebook::velox::ch::FileCacheManager::Options opts;
  opts.caches = {{.name = "default", .config = cfg, .configPath = cachePath}};
  opts.defaultCacheName = "default";
  opts.commonUserId = "gluten";
  opts.cachePathPrefix = cachePath;
  opts.allowedCacheRoot = cachePath;
  opts.localFileSystem = std::move(localFileSystem); // validateOptions: non-null.
  opts.memoryPool = memoryPool;                      // validateOptions: non-null.
  opts.timekeeper = std::move(timekeeper);           // validateOptions: non-null.
  opts.initializeOnCreate = true;
  return facebook::velox::ch::FileCacheManager::create(std::move(opts));
}

} // namespace gluten
```

**`VeloxBackend.h` additions:**

Forward-declare the manager and memory pool, and include the timekeeper header (the fields are `shared_ptr`, so forward declarations suffice even with the inline destructor). Near the top:

```cpp
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace facebook::velox::ch {
class FileCacheManager;
}

namespace facebook::velox::memory {
class MemoryPool;
}
```

In the private data section, after `std::shared_ptr<facebook::velox::config::ConfigBase> backendConf_;`:

```cpp
  std::shared_ptr<facebook::velox::ch::FileCacheManager> fileCacheManager_;
  // Dedicated leaf pool: FileCache performs direct allocations and Velox
  // rejects allocations on aggregate pools.
  std::shared_ptr<facebook::velox::memory::MemoryPool> fileCacheMemoryPool_;
  // Owned production timekeeper (real HHWheelTimer thread) the manager co-owns.
  std::shared_ptr<folly::ThreadWheelTimekeeper> fileCacheTimekeeper_;
```

In the private methods, after `void initCache();`:

```cpp
  void initFileCache();
```

**`initFileCache()` (`VeloxBackend.cc`), supplying exact production values:**

```cpp
#include "compute/FileCacheSupport.h"

#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Common/FileCacheStats.h"
#include "velox/common/file/FileSystems.h"

#include <folly/futures/ThreadWheelTimekeeper.h>

void VeloxBackend::initFileCache() {
  if (!backendConf_->get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    return;
  }
  const auto cachePath = std::filesystem::absolute(
      backendConf_->get<std::string>(kVeloxFileCachePath, "")).string();

  // Registered local filesystem for the cache path (registerLocalFileSystem()
  // already ran earlier in init()).
  auto localFs = facebook::velox::filesystems::getFileSystem(cachePath, nullptr);
  // Owned production timekeeper.
  fileCacheTimekeeper_ = std::make_shared<folly::ThreadWheelTimekeeper>();
  fileCacheMemoryPool_ =
      globalMemoryManager_->getAggregateMemoryPool()->addLeafChild("filecache");

  fileCacheManager_ = buildFileCacheManager(
      *backendConf_,
      fileCacheMemoryPool_.get(),
      localFs,
      fileCacheTimekeeper_);
  facebook::velox::ch::FileCacheManager::setInstance(fileCacheManager_.get());
  LOG(INFO) << "FileCache is ready at " << cachePath;
}
```

**Mutual-exclusion dispatch in `init()` (fail BEFORE `initCache()`):**

Replace the single `initCache();` call at `VeloxBackend.cc:295` with a validated one-path dispatch. Validation runs before either cache is allocated, so a misconfiguration fails fast with no partial startup state, and exactly one cache path is initialized:

```cpp
  // Validate FileCache/AsyncDataCache mutual exclusion BEFORE allocating either
  // cache. On conflict this throws and no AsyncDataCache is ever constructed.
  validateFileCacheConfig(*backendConf_);
  if (backendConf_->get<bool>(kVeloxFileCacheEnabled, kVeloxFileCacheEnabledDefault)) {
    initFileCache(); // exactly the FileCache path
  } else {
    initCache();     // existing AsyncDataCache path (no-op when disabled)
  }
```

**Teardown (`VeloxBackend::tearDown`), strict order, before `globalMemoryManager_.reset()`:**

Insert at the very start of `tearDown()` (before `executor_.reset()` and the existing `globalMemoryManager_.reset()` at `VeloxBackend.cc:450`):

```cpp
  // FileCache shutdown BEFORE executors, AsyncDataCache, and the global memory
  // manager: the manager holds a raw pointer to fileCacheMemoryPool_ and co-owns
  // fileCacheTimekeeper_. Documented strict order is
  //   shutdown -> setInstance(nullptr) -> drop the owning shared_ptr.
  if (fileCacheManager_) {
    const auto snapshot = facebook::velox::ch::takeFileCacheStatsSnapshot();
    LOG(INFO) << "FileCache teardown: cacheSize=" << snapshot.cacheSize
              << " cacheReadBytes=" << snapshot.cacheReadBytes
              << " sourceReadBytes=" << snapshot.sourceReadBytes
              << " cacheWriteBytes=" << snapshot.cacheWriteBytes
              << " hitCount=" << snapshot.cacheHitCount
              << " missCount=" << snapshot.cacheMissCount;
    fileCacheManager_->shutdown();
    facebook::velox::ch::FileCacheManager::setInstance(nullptr);
    fileCacheManager_.reset();
    // The manager's raw pool pointer is gone, so the dedicated leaf can now be
    // released before its aggregate parent/global memory manager.
    fileCacheMemoryPool_.reset();
    fileCacheTimekeeper_.reset();
  }
```

The existing teardown body (executors, AsyncDataCache dump, `globalMemoryManager_.reset()`) is unchanged and still runs after this block.

- [ ] **Step 1: Add the five config keys to `cpp/velox/config/VeloxConfig.h`**

Insert the exact block above after `kVeloxSsdCheckSumReadVerificationEnabled` (line 148), inside `namespace gluten`.

- [ ] **Step 2: Create `cpp/velox/compute/FileCacheSupport.h` and `.cc`**

Create both files with the exact contents shown above, then add `compute/FileCacheSupport.cc` to `VELOX_SRCS` in `cpp/velox/CMakeLists.txt` (after `compute/VeloxBackend.cc`, line 159).

- [ ] **Step 3: Add fields + `initFileCache()` declaration + includes to `cpp/velox/compute/VeloxBackend.h`**

- [ ] **Step 4: Implement `initFileCache()` in `cpp/velox/compute/VeloxBackend.cc`** (exact code above)

- [ ] **Step 5: Replace the `initCache();` call at `VeloxBackend.cc:295` with the validated one-path dispatch** (exact code above)

- [ ] **Step 6: Extend `VeloxBackend::tearDown()` with the FileCache shutdown block** (exact code above, at the start of `tearDown`)

- [ ] **Step 7: Create `cpp/velox/tests/FileCacheSupportTest.cc` (helper unit tests, no `VeloxBackend`)**

These tests exercise the factored helpers directly with a test-constructed pool/filesystem/timekeeper, so they never re-init glog or the global memory manager. `deprecatedAddDefaultLeafMemoryPool` lazily initializes the default memory manager (same approach as `FileCacheSeekBenchmark.cpp`).

```cpp
#include <gtest/gtest.h>

#include <filesystem>

#include "compute/FileCacheSupport.h"
#include "config/VeloxConfig.h"

#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/common/config/Config.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/memory/Memory.h"

#include <folly/futures/ThreadWheelTimekeeper.h>

namespace fs = std::filesystem;
using facebook::velox::config::ConfigBase;

class FileCacheSupportTest : public ::testing::Test {
 protected:
  std::string testDir_;
  std::shared_ptr<facebook::velox::memory::MemoryPool> pool_;
  std::shared_ptr<facebook::velox::filesystems::FileSystem> fs_;
  std::shared_ptr<folly::ThreadWheelTimekeeper> tk_;

  void SetUp() override {
    facebook::velox::filesystems::registerLocalFileSystem();
    testDir_ = fs::absolute("tmp/fc_support_test").string();
    fs::create_directories(testDir_);
    pool_ = facebook::velox::memory::deprecatedAddDefaultLeafMemoryPool("fc_support_test");
    fs_ = facebook::velox::filesystems::getFileSystem(testDir_, nullptr);
    tk_ = std::make_shared<folly::ThreadWheelTimekeeper>();
  }

  void TearDown() override {
    facebook::velox::ch::FileCacheManager::setInstance(nullptr);
    fs::remove_all(testDir_);
  }

  std::shared_ptr<ConfigBase> conf(
      bool fc, bool adc, const std::string& path, uint64_t bgThreads = 4) {
    return std::make_shared<ConfigBase>(std::unordered_map<std::string, std::string>{
        {gluten::kVeloxFileCacheEnabled, fc ? "true" : "false"},
        {gluten::kVeloxCacheEnabled, adc ? "true" : "false"},
        {gluten::kVeloxFileCachePath, path},
        {gluten::kVeloxFileCacheSize, std::to_string(64ULL << 20)},
        {gluten::kVeloxFileCacheBackgroundDownloadThreads, std::to_string(bgThreads)}});
  }
};

TEST_F(FileCacheSupportTest, DisabledReturnsNull) {
  auto c = conf(false, false, testDir_);
  EXPECT_EQ(gluten::buildFileCacheManager(*c, pool_.get(), fs_, tk_), nullptr);
}

TEST_F(FileCacheSupportTest, MissingPathThrows) {
  auto c = conf(true, false, "");
  EXPECT_ANY_THROW(gluten::validateFileCacheConfig(*c));
  EXPECT_ANY_THROW(gluten::buildFileCacheManager(*c, pool_.get(), fs_, tk_));
}

TEST_F(FileCacheSupportTest, BothCachesEnabledThrows) {
  auto c = conf(true, true, testDir_);
  EXPECT_ANY_THROW(gluten::validateFileCacheConfig(*c));
}

TEST_F(FileCacheSupportTest, ValidConfigBuildsManager) {
  auto c = conf(true, false, testDir_ + "/valid");
  ASSERT_EQ(
      pool_->kind(),
      facebook::velox::memory::MemoryPool::Kind::kLeaf);
  auto mgr = gluten::buildFileCacheManager(*c, pool_.get(), fs_, tk_);
  ASSERT_NE(mgr, nullptr);
  ASSERT_NE(mgr->getDefault(), nullptr);
  mgr->shutdown();
}

TEST_F(FileCacheSupportTest, AggregatePoolIsRejectedBeforeManagerCreation) {
  auto c = conf(true, false, testDir_ + "/aggregate");
  ASSERT_NE(pool_->parent(), nullptr);
  ASSERT_NE(
      pool_->parent()->kind(),
      facebook::velox::memory::MemoryPool::Kind::kLeaf);
  EXPECT_ANY_THROW(
      gluten::buildFileCacheManager(*c, pool_->parent(), fs_, tk_));
}

TEST_F(FileCacheSupportTest, BackgroundThreadsMappedToConfig) {
  // backgroundDownloadThreads feeds FileCacheFactory::computeCacheWorkerMax
  // (velox/ch/Interpreters/FileCache/FileCacheFactory.cpp:411), so it shows up
  // in refreshStats().workerPoolMax. Two builds differing only in the thread
  // count must differ in workerPoolMax by exactly that delta.
  auto c2 = conf(true, false, testDir_ + "/two", /*bgThreads=*/2);
  auto c8 = conf(true, false, testDir_ + "/eight", /*bgThreads=*/8);
  auto m2 = gluten::buildFileCacheManager(*c2, pool_.get(), fs_, tk_);
  auto m8 = gluten::buildFileCacheManager(*c8, pool_.get(), fs_, tk_);
  ASSERT_NE(m2, nullptr);
  ASSERT_NE(m8, nullptr);
  EXPECT_EQ(m8->refreshStats().workerPoolMax - m2->refreshStats().workerPoolMax, 6u);
  m8->shutdown();
  m2->shutdown();
}
```

- [ ] **Step 8: Create `cpp/velox/tests/FileCacheGlutenLifecycleTest.cc` (one end-to-end lifecycle test with a real cold read)**

Exactly one `VeloxBackend::create` call (in the single test), so glog and the global memory manager are initialized once. Uses `AllocationListener::noop()`, following the live pattern at `cpp/velox/tests/BufferOutputStreamTest.cc:32` and `cpp/velox/tests/MemoryManagerTest.cc:53`.

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "compute/VeloxBackend.h"
#include "config/VeloxConfig.h"

#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Disks/IO/FileCacheFileIdentity.h"
#include "velox/ch/Disks/IO/FileCacheRequestContext.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h"
#include "velox/ch/Interpreters/FileCache/FileCacheReadOptions.h"
#include "velox/common/file/LocalFile.h"
#include "velox/common/io/IoStatistics.h"
#include "velox/common/memory/Memory.h"
namespace fs = std::filesystem;
using namespace facebook::velox;

// Performs one real cold FileCacheBufferedInput read over a small local file
// through the installed manager's default cache and dedicated leaf pool, and
// returns the number of bytes read. This must reach the FileCache
// allocation/write path; passing the aggregate pool instead of the dedicated
// leaf pool throws "Memory operation is only allowed on leaf memory pool".
static uint64_t readOneColdFileThroughInstalledManager(const std::string& dir) {
  const std::string dataFile = dir + "/cold.bin";
  const std::string payload(4096, 'Z');
  {
    std::ofstream ofs(dataFile, std::ios::binary);
    ofs.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  }
  auto* mgr = ch::FileCacheManager::getInstance();
  auto cache = mgr->getDefault();

  // Leaf pool for the reader's own BufferedInput allocations; the FileCache
  // buffer allocations go through the manager's dedicated leaf pool. The global
  // memory manager is initialized by VeloxBackend::create before this runs.
  auto readerPool = memory::memoryManager()->addLeafPool("fc_lifecycle_reader");

  auto readFile = std::make_shared<LocalReadFile>(dataFile);
  auto cacheKey = ch::FileCacheFileIdentity::deriveKey({dataFile, ""});
  ch::FileCacheRequestContext reqCtx;
  reqCtx.userId = mgr->commonUserId();
  reqCtx.cacheable = true;
  ch::FileCacheOriginInfo origin(reqCtx.userId);
  origin.segment_type = reqCtx.segmentType;
  ch::FileCacheReadOptions cacheOpts;
  cacheOpts.remoteFsBufferSize = payload.size();
  cacheOpts.localFsBufferSize = payload.size();
  dwio::common::ReaderOptions readerOpts(readerPool.get());

  ch::FileCacheBufferedInput input(
      readFile,
      std::move(cache),
      std::move(cacheKey),
      std::move(origin),
      std::move(cacheOpts),
      std::move(reqCtx),
      dwio::common::MetricsLog::voidLog(),
      std::make_shared<io::IoStatistics>(),
      std::make_shared<velox::IoStats>(),
      nullptr,
      readerOpts);
  auto stream = input.read(0, payload.size(), dwio::common::LogType::FILE);
  const void* buf = nullptr;
  int32_t len = 0;
  uint64_t total = 0;
  while (stream->Next(&buf, &len)) {
    total += static_cast<uint64_t>(len);
  }
  return total;
}

// Single test: create the backend once with FileCache enabled, assert the
// manager is installed with a default cache, perform one real cold read, tear
// down, and assert the global manager is withdrawn.
TEST(FileCacheGlutenLifecycleTest, InstallAndTeardownThroughBackend) {
  const std::string dir = fs::absolute("tmp/fc_lifecycle_test").string();
  fs::create_directories(dir);

  std::unordered_map<std::string, std::string> conf{
      {gluten::kVeloxFileCacheEnabled, "true"},
      {gluten::kVeloxFileCachePath, dir},
      {gluten::kVeloxFileCacheSize, std::to_string(64ULL << 20)}};

  gluten::VeloxBackend::create(gluten::AllocationListener::noop(), conf);
  // Scope guard: tearDown on every post-create assertion/exception path.
  struct Guard {
    gluten::VeloxBackend* b;
    bool active{true};
    ~Guard() {
      if (active) {
        b->tearDown();
      }
    }
  } guard{gluten::VeloxBackend::get()};

  EXPECT_NE(ch::FileCacheManager::getInstance(), nullptr);
  EXPECT_NE(ch::FileCacheManager::getInstance()->getDefault(), nullptr);

  EXPECT_EQ(readOneColdFileThroughInstalledManager(dir), 4096u);

  guard.active = false;
  guard.b->tearDown();
  EXPECT_EQ(ch::FileCacheManager::getInstance(), nullptr);

  fs::remove_all(dir);
}
```

The `readOneColdFileThroughInstalledManager` helper performs a real read through the installed manager's default cache and must not call a counter/helper proxy. Confirm the `FileCacheBufferedInput::read` and `ReaderOptions` signatures against the 019-A contract before building.

- [ ] **Step 9: Register both tests in `cpp/velox/tests/CMakeLists.txt`**

```cmake
add_velox_test(velox_file_cache_support_test SOURCES FileCacheSupportTest.cc)
add_velox_test(velox_file_cache_gluten_lifecycle_test SOURCES FileCacheGlutenLifecycleTest.cc)
```

- [ ] **Step 10: Build and run both test targets**

```bash
cd /root/oss/gluten-019
ninja -C cpp/build velox_file_cache_support_test velox_file_cache_gluten_lifecycle_test \
  > cpp/build/build_019b.log 2>&1
ctest --test-dir cpp/build \
  -R '^velox_file_cache_(support|gluten_lifecycle)_test$' --output-on-failure \
  > cpp/build/test_019b.log 2>&1
echo "exit: $?"
```

A subagent analyzes `build_019b.log`/`test_019b.log` and returns a concise summary.

- [ ] **Step 11: Mutation — remove the mutual-exclusion check**

**File:** `cpp/velox/compute/FileCacheSupport.cc`
**Function:** `gluten::validateFileCacheConfig`

Comment out the two-line mutual-exclusion `VELOX_USER_CHECK` on `kVeloxCacheEnabled` in `validateFileCacheConfig` (the check shown in the `FileCacheSupport.cc` source above).

Re-run: `ctest --test-dir cpp/build -R '^velox_file_cache_support_test$' --output-on-failure`

**Expected failed assertion:** `FileCacheSupportTest.BothCachesEnabledThrows`'s `EXPECT_ANY_THROW(gluten::validateFileCacheConfig(*c))` sees no throw — test reports FAILED. Restore after confirming RED.

- [ ] **Step 12: Mutation — drop the background-threads mapping**

**File:** `cpp/velox/compute/FileCacheSupport.cc`
**Function:** `gluten::buildFileCacheManager`

Comment out `cfg.backgroundDownloadThreads = bgThreads;`.

Re-run: `ctest --test-dir cpp/build -R '^velox_file_cache_support_test$' --output-on-failure`

**Expected failed assertion:** `FileCacheSupportTest.BackgroundThreadsMappedToConfig`'s `EXPECT_EQ(delta, 6u)` fails (both builds now report the default thread count). Restore after confirming RED.

**Gate:** Both test targets pass; the two mutations prove mutual-exclusion and the 5th-key mapping are covered. The `MissingPathThrows` and `ValidConfigBuildsManager` cases cover path validation and successful construction; the lifecycle test covers end-to-end install/teardown ordering with a real cold `FileCacheBufferedInput` read through the dedicated leaf pool.

---

## Task 019-C: Gluten Builder Adapter

> Consumes the 019-A contract and 019-B config keys. All paths are in `/root/oss/gluten-019`.

**Files (in `/root/oss/gluten-019`):**
- Modify: `cpp/velox/memory/GlutenBufferedInputBuilder.h` — add FileCache selection logic
- Create: `cpp/velox/tests/FileCacheGlutenBuilderTest.cc`
- Modify: `cpp/velox/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FileCacheManager::getInstance`, `FileCacheManager::getDefault`, `FileCacheManager::commonUserId` (`FileCacheManager.h:128`), `FileCacheFileIdentity::deriveKey` (`FileCacheFileIdentity.h:44`), `ReaderOptions::cacheable` (`Options.h:857`), `FileCacheRequestContext::segmentType`/`FileCacheOriginInfo::segment_type`, `FileCacheBufferedInput` constructor + `cancellationToken()` accessor (Task 017A version with token), `ConnectorQueryCtx::cancellationToken` (`:558`) — all confirmed in the 019-A contract
- Produces: Builder that returns `FileCacheBufferedInput` when the manager is installed, preserving the CBI and direct paths unchanged

**Builder implementation (exact, based on live `GlutenBufferedInputBuilder.h:27-62`):**

The `create` method signature is:

```cpp
std::unique_ptr<facebook::velox::dwio::common::BufferedInput> create(
    const facebook::velox::FileHandle& fileHandle,
    const facebook::velox::dwio::common::ReaderOptions& readerOpts,
    const facebook::velox::connector::ConnectorQueryCtx* connectorQueryCtx,
    std::shared_ptr<facebook::velox::io::IoStatistics> ioStatistics,
    std::shared_ptr<facebook::velox::IoStats> ioStats,
    folly::Executor* executor,
    const folly::F14FastMap<std::string, std::string>& fileReadOps = {}) override
```

Add FileCache branch BEFORE the existing CBI check:

```cpp
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Disks/IO/FileCacheFileIdentity.h"
#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Disks/IO/FileCacheRequestContext.h"
#include "velox/ch/Interpreters/FileCache/FileCacheReadOptions.h"
#include "velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h"

// In GlutenBufferedInputBuilder::create(), as first branch:
auto* fcManager = facebook::velox::ch::FileCacheManager::getInstance();
if (fcManager != nullptr)
{
    auto defaultCache = fcManager->getDefault();
    VELOX_CHECK_NOT_NULL(defaultCache.get(), "FileCacheManager has no default cache");

    const std::string path = fileHandle.file->getName();
    auto cacheKey = facebook::velox::ch::FileCacheFileIdentity::deriveKey({path, ""});

    facebook::velox::ch::FileCacheRequestContext reqCtx;
    reqCtx.queryId = connectorQueryCtx->queryId();
    reqCtx.userId = fcManager->commonUserId();
    reqCtx.cacheable = readerOpts.cacheable(); // ReaderOptions::cacheable (Options.h:857)
    // reqCtx.segmentType stays FileSegmentKeyType::Data (default).

    facebook::velox::ch::FileCacheReadOptions cacheOpts;
    cacheOpts.remoteFsBufferSize = readerOpts.loadQuantum();
    cacheOpts.localFsBufferSize = readerOpts.loadQuantum();

    // Origin: weight left as std::nullopt; segment type taken from the request.
    // (The 3-arg FileCacheOriginInfo ctor would force a concrete weight, so set
    // the field directly to keep weight == std::nullopt.)
    facebook::velox::ch::FileCacheOriginInfo origin(reqCtx.userId);
    origin.segment_type = reqCtx.segmentType;

    folly::CancellationToken token = connectorQueryCtx->cancellationToken();

    // Constructor order (Task 017A): readFile, cache, key, origin, cacheOptions,
    // requestContext, metricsLog, ioStatistics, ioStats, executor,
    // readerOptions, fileReadOps, cancellationToken
    return std::make_unique<facebook::velox::ch::FileCacheBufferedInput>(
        fileHandle.file,
        std::move(defaultCache),
        std::move(cacheKey),
        std::move(origin),
        std::move(cacheOpts),
        std::move(reqCtx),
        facebook::velox::dwio::common::MetricsLog::voidLog(),
        std::move(ioStatistics),
        std::move(ioStats),
        executor,
        readerOpts,
        fileReadOps,
        std::move(token));
}
// --- Existing CBI and direct paths below are the current live create() body,
// --- copied verbatim so the non-FileCache behavior is preserved exactly. ---
if (connectorQueryCtx->cache())
{
    return std::make_unique<facebook::velox::dwio::common::CachedBufferedInput>(
        fileHandle.file,
        dwio::common::MetricsLog::voidLog(),
        fileHandle.uuid,
        connectorQueryCtx->cache(),
        facebook::velox::connector::Connector::getTracker(connectorQueryCtx->scanId(), readerOpts.loadQuantum()),
        fileHandle.groupId,
        std::move(ioStatistics),
        std::move(ioStats),
        executor,
        readerOpts,
        fileReadOps);
}
return std::make_unique<GlutenDirectBufferedInput>(
    fileHandle.file,
    dwio::common::MetricsLog::voidLog(),
    fileHandle.uuid,
    facebook::velox::connector::Connector::getTracker(connectorQueryCtx->scanId(), readerOpts.loadQuantum()),
    fileHandle.groupId,
    std::move(ioStatistics),
    std::move(ioStats),
    executor,
    readerOpts,
    fileReadOps);
```

`FileCacheManager::commonUserId()` is a public accessor (`velox/ch/Interpreters/FileCache/FileCacheManager.h:128`), returning the `commonUserId` supplied to `Options`.

- [ ] **Step 1: Add FileCache selection logic to `cpp/velox/memory/GlutenBufferedInputBuilder.h`**
- [ ] **Step 2: Create `cpp/velox/tests/FileCacheGlutenBuilderTest.cc`**

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "memory/GlutenBufferedInputBuilder.h"

#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/common/caching/FileHandle.h"
#include "velox/common/caching/FileIds.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/file/LocalFile.h"
#include "velox/common/io/IoStatistics.h"
#include "velox/common/memory/Memory.h"
#include "velox/connectors/Connector.h"

#include <folly/CancellationToken.h>
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace fs = std::filesystem;
using namespace facebook::velox;

class FileCacheGlutenBuilderTest : public ::testing::Test {
 protected:
  std::string testDir_;
  std::string testFile_;
  std::string cacheDir_;
  std::shared_ptr<memory::MemoryPool> pool_;
  std::shared_ptr<filesystems::FileSystem> fs_;
  std::shared_ptr<folly::ThreadWheelTimekeeper> tk_;
  std::shared_ptr<config::ConfigBase> sessionProps_;
  std::shared_ptr<ch::FileCacheManager> fcManager_;

  void SetUp() override {
    filesystems::registerLocalFileSystem();
    testDir_ = fs::absolute("tmp/fc_builder_test").string();
    cacheDir_ = testDir_ + "/cache";
    testFile_ = testDir_ + "/data.bin";
    fs::create_directories(testDir_);
    {
      std::ofstream ofs(testFile_, std::ios::binary);
      const std::string data(4096, 'X');
      ofs.write(data.data(), data.size());
    }
    pool_ = memory::deprecatedAddDefaultLeafMemoryPool("fc_builder_test");
    fs_ = filesystems::getFileSystem(cacheDir_, nullptr);
    tk_ = std::make_shared<folly::ThreadWheelTimekeeper>();
    // Non-null session properties: ConnectorQueryCtx VELOX_CHECK_NOT_NULLs it.
    sessionProps_ =
        std::make_shared<config::ConfigBase>(std::unordered_map<std::string, std::string>{});
  }

  void TearDown() override {
    ch::FileCacheManager::setInstance(nullptr);
    fcManager_.reset();
    fs::remove_all(testDir_);
  }

  void installFileCache() {
    ch::FileCacheConfig cfg;
    cfg.path = cacheDir_;
    cfg.maxSize = 64ULL << 20;
    ch::FileCacheManager::Options opts;
    opts.caches = {{.name = "default", .config = cfg, .configPath = cacheDir_}};
    opts.defaultCacheName = "default";
    opts.commonUserId = "test";
    opts.cachePathPrefix = cacheDir_;
    opts.allowedCacheRoot = cacheDir_;
    opts.localFileSystem = fs_;
    opts.memoryPool = pool_.get();
    opts.timekeeper = tk_;
    opts.initializeOnCreate = true;
    fcManager_ = ch::FileCacheManager::create(std::move(opts));
    ch::FileCacheManager::setInstance(fcManager_.get());
  }

  // Single shared helper: builds a real FileHandle + ConnectorQueryCtx (real
  // constructor order; cancellationToken is the 14th ctor arg) and runs the
  // builder. The returned input copies what it needs, so the local connCtx may
  // be destroyed on return.
  std::unique_ptr<dwio::common::BufferedInput> runBuilder(
      folly::CancellationToken token = {}) {
    gluten::GlutenBufferedInputBuilder builder;
    auto readFile = std::make_shared<facebook::velox::LocalReadFile>(testFile_);
    FileHandle fileHandle;
    fileHandle.file = readFile;
    fileHandle.uuid = StringIdLease(fileIds(), testFile_);
    fileHandle.groupId = StringIdLease(fileIds(), testFile_);
    dwio::common::ReaderOptions readerOpts(pool_.get());
    auto ioStats = std::make_shared<io::IoStatistics>();
    auto ioStatsObj = std::make_shared<velox::IoStats>();
    connector::ConnectorQueryCtx connCtx(
        pool_.get(),                // operatorPool
        pool_.get(),                // connectorPool
        sessionProps_.get(),        // sessionProperties (non-null)
        nullptr,                    // spillConfig
        common::PrefixSortConfig{}, // prefixSortConfig
        nullptr,                    // expressionEvaluator
        nullptr,                    // cache (no AsyncDataCache)
        "test-query",               // queryId
        "task-0",                   // taskId
        "plan-0",                   // planNodeId
        0,                          // driverId
        "UTC",                      // sessionTimezone
        /*adjustTimestampToTimezone=*/false,
        token);                     // cancellationToken (14th param)
    return builder.create(fileHandle, readerOpts, &connCtx, ioStats, ioStatsObj, nullptr);
  }
};

TEST_F(FileCacheGlutenBuilderTest, ReturnsFileCacheBufferedInputWhenManagerInstalled) {
  installFileCache();
  auto result = runBuilder();
  auto* fcbi = dynamic_cast<ch::FileCacheBufferedInput*>(result.get());
  ASSERT_NE(fcbi, nullptr);
}

TEST_F(FileCacheGlutenBuilderTest, ReturnsDirectWhenNoCache) {
  // No FileCacheManager installed and no AsyncDataCache: expect the direct path.
  auto result = runBuilder();
  ASSERT_NE(result.get(), nullptr);
  EXPECT_EQ(dynamic_cast<ch::FileCacheBufferedInput*>(result.get()), nullptr);
}

TEST_F(FileCacheGlutenBuilderTest, CopiedCancellationTokenReachesInput) {
  installFileCache();
  folly::CancellationSource src;
  auto result = runBuilder(src.getToken());
  auto* fcbi = dynamic_cast<ch::FileCacheBufferedInput*>(result.get());
  ASSERT_NE(fcbi, nullptr);
  // The token is stored by value; cancelling the source is observed through the
  // FCBI accessor (FileCacheBufferedInput::cancellationToken(), Task 017A).
  EXPECT_FALSE(fcbi->cancellationToken().isCancellationRequested());
  src.requestCancellation();
  EXPECT_TRUE(fcbi->cancellationToken().isCancellationRequested());
}
```

- [ ] **Step 3: Register test target in `cpp/velox/tests/CMakeLists.txt`**

```cmake
add_velox_test(velox_file_cache_gluten_builder_test SOURCES FileCacheGlutenBuilderTest.cc)
```

- [ ] **Step 4: Build and run**

```bash
cd /root/oss/gluten-019
ninja -C cpp/build velox_file_cache_gluten_builder_test \
  > cpp/build/build_019c.log 2>&1
ctest --test-dir cpp/build -R '^velox_file_cache_gluten_builder_test$' \
  --output-on-failure > cpp/build/test_019c.log 2>&1
echo "exit: $?"
```

A subagent analyzes `build_019c.log`/`test_019c.log` and returns a concise summary.

- [ ] **Step 5: Mutation — remove FileCache branch**

**File:** `cpp/velox/memory/GlutenBufferedInputBuilder.h`
**Function:** `GlutenBufferedInputBuilder::create`

Comment out the entire `if (fcManager != nullptr)` block.

Re-run: `ctest --test-dir cpp/build -R '^velox_file_cache_gluten_builder_test$' --output-on-failure`

**Expected failed assertions:** `ReturnsFileCacheBufferedInputWhenManagerInstalled` and `CopiedCancellationTokenReachesInput` both `dynamic_cast` to `ch::FileCacheBufferedInput*` and get `nullptr` — their `ASSERT_NE(fcbi, nullptr)` report FAILED. Restore after confirming RED.

**Gate:** All three builder tests pass (FCBI selection, direct fallback, and copied-cancellation-token observed via `fcbi->cancellationToken()`); the mutation proves the FileCache branch is covered.

---

## Task 019-D: Complete Gluten Metric Bridge

> Consumes the 019-A contract, 019-C Builder, and Task 017A's `IoStats` counter. All paths are in `/root/oss/gluten-019`.

**Goal:** Wire `fileCacheWriteBytes` end-to-end from C++ `IoStats` through JNI to Spark `SQLMetric`.

**Metric propagation path (traced from live code):**

```text
FileCacheInputStream::read
  -> ioStats_->addCounter("fileCacheWriteBytes", RuntimeCounter(bytesWritten, RuntimeCounter::Unit::kBytes))
     [velox/common/file/File.h:57, Task 017A code]

FileDataSource collects RuntimeMetrics from IoStats into operator customStats
  -> OperatorStats.customStats["fileCacheWriteBytes"]

exec::toPlanStats aggregates per-node
  -> PlanNodeStats.operatorStats[i].customStats["fileCacheWriteBytes"]

WholeStageResultIterator::collectMetrics  [cpp/velox/compute/WholeStageResultIterator.cc, collectMetrics()]
  -> metrics_->get(Metrics::kFileCacheWriteBytes)[metricIndex] =
       gluten::sumRuntimeMetric(second->customStats, kFileCacheWriteBytes);
     [extracted testable helper, cpp/velox/compute/RuntimeMetricUtil.h]

JniWrapper.cc NewObject call  [cpp/core/jni/JniWrapper.cc, longArray after kLoadLazyVectorTime]
  -> longArray[Metrics::kFileCacheWriteBytes], (after kLoadLazyVectorTime, before the taskStats string)

Java Metrics.java constructor  [backends-velox/src/main/java/org/apache/gluten/metrics/Metrics.java:119]
  -> receives long[] fileCacheWriteBytes parameter (after loadLazyVectorTime, before taskStats)
  -> stores in public long[] fileCacheWriteBytes field

Java Metrics.getOperatorMetrics  [Metrics.java:170]
  -> passes fileCacheWriteBytes[index] to OperatorMetrics constructor (after loadLazyVectorTime[index])

Java OperatorMetrics.java  [OperatorMetrics.java:112]
  -> constructor receives long fileCacheWriteBytes (after long loadLazyVectorTime)
  -> stores in public long fileCacheWriteBytes field

Scala MetricsUtil.mergeMetrics  [backends-velox/src/main/scala/org/apache/gluten/metrics/MetricsUtil.scala:185]
  -> a SECOND `new OperatorMetrics` call site: accumulate fileCacheWriteBytes across
     the merged suites and pass it as the last constructor argument (mandatory — otherwise
     backends-velox fails to compile once OperatorMetrics gains the parameter)

Scala FileSourceScanMetricsUpdater.updateNativeMetrics  [FileSourceScanMetricsUpdater.scala]
  -> ScanMetricsUtil.inc(fileCacheWriteBytes, operatorMetrics.fileCacheWriteBytes)

Scala VeloxMetricsApi.genFileSourceScanTransformerMetricsFull  [VeloxMetricsApi.scala:219]
  -> "fileCacheWriteBytes" -> SQLMetrics.createSizeMetric(sparkContext, "file cache write bytes")
```

**Files (in `/root/oss/gluten-019`):**

| Layer | File | Change |
|---|---|---|
| C++ enum | `cpp/core/utils/Metrics.h` | Add `kFileCacheWriteBytes` before `kEnd` (after `kLoadLazyVectorTime`) |
| C++ helper | `cpp/velox/compute/RuntimeMetricUtil.h` / `.cc` (NEW) | Testable `gluten::sumRuntimeMetric(customStats, key)` used by `collectMetrics` |
| C++ constant | `cpp/velox/compute/WholeStageResultIterator.cc` | Add `const std::string kFileCacheWriteBytes = "fileCacheWriteBytes";` at file scope |
| C++ collect | `cpp/velox/compute/WholeStageResultIterator.cc` | Add propagation line in `collectMetrics` using `gluten::sumRuntimeMetric` |
| C++ build | `cpp/velox/CMakeLists.txt` | Add `compute/RuntimeMetricUtil.cc` to `VELOX_SRCS` |
| JNI call | `cpp/core/jni/JniWrapper.cc` | Add `longArray[Metrics::kFileCacheWriteBytes],` after the `kLoadLazyVectorTime` line (684) |
| JNI signature | `cpp/core/jni/JniWrapper.cc:316` | Update `"<init>"` signature (one more `[J` before `Ljava/lang/String;`) |
| Java Metrics | `backends-velox/src/main/java/org/apache/gluten/metrics/Metrics.java` | Add `public long[] fileCacheWriteBytes;` field, constructor param, assignment, getOperatorMetrics pass |
| Java OperatorMetrics | `backends-velox/src/main/java/org/apache/gluten/metrics/OperatorMetrics.java` | Add `public long fileCacheWriteBytes;` field, constructor param, assignment |
| Scala MetricsUtil | `backends-velox/src/main/scala/org/apache/gluten/metrics/MetricsUtil.scala` | `mergeMetrics`: accumulate `fileCacheWriteBytes`, pass to the 2nd `new OperatorMetrics` |
| Scala VeloxMetricsApi | `backends-velox/src/main/scala/org/apache/gluten/backendsapi/velox/VeloxMetricsApi.scala` | Add entry in `genFileSourceScanTransformerMetricsFull` Map |
| Scala FileSourceScanMetricsUpdater | `backends-velox/src/main/scala/org/apache/gluten/metrics/FileSourceScanMetricsUpdater.scala` | Add field + `ScanMetricsUtil.inc` call |
| C++ test | `cpp/velox/tests/FileCacheGlutenMetricsTest.cc` (NEW) | Native carrier gate: `sumRuntimeMetric` + enum order |
| Java test | `backends-velox/src/test/java/org/apache/gluten/metrics/MetricsCarrierTest.java` (NEW) | Java carrier gate: `getOperatorMetrics` carries the exact value |
| Scala test | `backends-velox/src/test/scala/org/apache/gluten/metrics/FileSourceScanMetricsUpdaterSuite.scala` (NEW) | Scala gate: updater increments the `SQLMetric` |

Every `new OperatorMetrics` / `new Metrics` call site in `backends-velox` was searched; the two `new OperatorMetrics` sites are `Metrics.java:170` (`getOperatorMetrics`) and `MetricsUtil.scala:185` (`mergeMetrics`) — both are updated. (`TestSparkDataFile.java`'s `new Metrics` call is Iceberg's own `org.apache.iceberg.Metrics`, unrelated.)

**Detailed changes:**

### 1. C++ `Metrics.h` — add enum entry (after `kLoadLazyVectorTime`, before `kEnd`)

```cpp
    kLoadLazyVectorTime,

    // FileCache metrics.
    kFileCacheWriteBytes,

    // The end of enum items.
    kEnd,
```

### 2. C++ `RuntimeMetricUtil` helper + `WholeStageResultIterator.cc` constant and propagation

`WholeStageResultIterator::collectMetrics` and `runtimeMetric` are private, so the FileCache carrier is exercised through a small extracted free helper that `collectMetrics` also uses. Create `cpp/velox/compute/RuntimeMetricUtil.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace facebook::velox {
struct RuntimeMetric;
}

namespace gluten {

/// Sum aggregation of a named Velox runtime metric from operator customStats,
/// returning 0 when the key is absent. Extracted from
/// WholeStageResultIterator::collectMetrics so the FileCache byte-counter
/// propagation is unit-testable without constructing a Task/plan.
int64_t sumRuntimeMetric(
    const std::unordered_map<std::string, facebook::velox::RuntimeMetric>& customStats,
    const std::string& key);

} // namespace gluten
```

and `cpp/velox/compute/RuntimeMetricUtil.cc`:

```cpp
#include "compute/RuntimeMetricUtil.h"

#include "velox/common/base/RuntimeMetrics.h"

namespace gluten {

int64_t sumRuntimeMetric(
    const std::unordered_map<std::string, facebook::velox::RuntimeMetric>& customStats,
    const std::string& key) {
  const auto it = customStats.find(key);
  return it == customStats.end() ? 0 : it->second.sum;
}

} // namespace gluten
```

Add `compute/RuntimeMetricUtil.cc` to `VELOX_SRCS` in `cpp/velox/CMakeLists.txt` (after `compute/WholeStageResultIterator.cc`, line 162).

In `WholeStageResultIterator.cc`, at file scope (alongside `kLocalReadBytes` at line 67):

```cpp
const std::string kFileCacheWriteBytes = "fileCacheWriteBytes";
```

Add `#include "compute/RuntimeMetricUtil.h"` to the includes. In `collectMetrics`, after the `kWriteIOTime` line (~line 592):

```cpp
      metrics_->get(Metrics::kFileCacheWriteBytes)[metricIndex] =
          gluten::sumRuntimeMetric(second->customStats, kFileCacheWriteBytes);
```

### 3. JNI `JniWrapper.cc` — add to NewObject call and update signature

After `longArray[Metrics::kLoadLazyVectorTime],` (line 684), add:

```cpp
      longArray[Metrics::kFileCacheWriteBytes],
```

Update the `"<init>"` signature string (line 316). The current signature has 44 `[J` array groups:

```text
"([J[J[J[J[J[J[J[J[J[JJ[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[JLjava/lang/String;)V"
```

Insert one additional `[J` immediately before `Ljava/lang/String;` (array count 44 → 45; the single scalar `J` for `veloxToArrow` is unchanged):

```text
"([J[J[J[J[J[J[J[J[J[JJ[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[JLjava/lang/String;)V"
```

Verify the new signature has 45 array parameters (must equal the number of `long[]` args passed to `NewObject`):

```bash
printf '%s' '([J[J[J[J[J[J[J[J[J[JJ[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[J[JLjava/lang/String;)V' \
  | grep -o '\[J' | wc -l   # -> 45
```

### 4. Java `Metrics.java` — field, constructor param, assignment, getOperatorMetrics

Field declaration (after `public long[] loadLazyVectorTime;` at line 67, before `public String taskStats;`):

```java
  public long[] fileCacheWriteBytes;
```

Constructor parameter (after `long[] loadLazyVectorTime` at line 119, before `String taskStats`):

```java
      long[] loadLazyVectorTime,
      long[] fileCacheWriteBytes,
      String taskStats) {
```

Assignment in constructor body (after `this.loadLazyVectorTime = loadLazyVectorTime;` at line 166):

```java
    this.fileCacheWriteBytes = fileCacheWriteBytes;
```

In `getOperatorMetrics` method (after `loadLazyVectorTime[index]` at line 219, the last argument):

```java
        loadLazyVectorTime[index],
        fileCacheWriteBytes[index]);
```

### 5. Java `OperatorMetrics.java` — field and constructor param

Field (after `public long loadLazyVectorTime;` at line 65):

```java
  public long fileCacheWriteBytes;
```

Constructor parameter (after `long loadLazyVectorTime` at line 112):

```java
      long loadLazyVectorTime,
      long fileCacheWriteBytes) {
```

Assignment (after `this.loadLazyVectorTime = loadLazyVectorTime;` at line 156):

```java
    this.fileCacheWriteBytes = fileCacheWriteBytes;
```

### 6. Scala `MetricsUtil.scala` — `mergeMetrics` aggregation (2nd `new OperatorMetrics`)

`mergeMetrics` is the SECOND `new OperatorMetrics` call site (`MetricsUtil.scala:185`). It MUST also pass `fileCacheWriteBytes` or `backends-velox` fails to compile once `OperatorMetrics` gains the parameter.

Accumulator (after `var loadLazyVectorTime: Long = 0` at line 144):

```scala
    var fileCacheWriteBytes: Long = 0
```

Inside the `while (metricsIterator.hasNext)` loop (after `loadLazyVectorTime += metrics.loadLazyVectorTime` at line 182):

```scala
      fileCacheWriteBytes += metrics.fileCacheWriteBytes
```

In the `new OperatorMetrics` argument list (after `loadLazyVectorTime` at line 229 — the last argument):

```scala
      loadLazyVectorTime,
      fileCacheWriteBytes
    )
```

### 7. Scala `VeloxMetricsApi.scala` — register SQLMetric

In `genFileSourceScanTransformerMetricsFull` Map (after the `"loadLazyVectorTime"` entry at line 263):

```scala
      "fileCacheWriteBytes" -> SQLMetrics.createSizeMetric(sparkContext, "file cache write bytes")
```

### 8. Scala `FileSourceScanMetricsUpdater.scala` — field + update

Field (after `private val loadLazyVectorTime` at line 57):

```scala
  private val fileCacheWriteBytes: Option[SQLMetric] = metric("fileCacheWriteBytes")
```

In `updateNativeMetrics`, after existing `ScanMetricsUtil.inc(loadLazyVectorTime, operatorMetrics.loadLazyVectorTime)`:

```scala
      ScanMetricsUtil.inc(fileCacheWriteBytes, operatorMetrics.fileCacheWriteBytes)
```

### Steps

- [ ] **Step 1: Add `kFileCacheWriteBytes` to C++ `Metrics.h` enum**
- [ ] **Step 2: Create `RuntimeMetricUtil.h`/`.cc`, register in `VELOX_SRCS`, add the constant + `collectMetrics` propagation in `WholeStageResultIterator.cc`**
- [ ] **Step 3: Update JNI NewObject call and signature in `JniWrapper.cc`**
- [ ] **Step 4: Add field/param to Java `Metrics.java` (field, constructor, assignment, getOperatorMetrics)**
- [ ] **Step 5: Add field/param to Java `OperatorMetrics.java`**
- [ ] **Step 6: Aggregate `fileCacheWriteBytes` in `MetricsUtil.scala` `mergeMetrics` (2nd `new OperatorMetrics`)**
- [ ] **Step 7: Add SQLMetric in `VeloxMetricsApi.scala`**
- [ ] **Step 8: Add field + update in `FileSourceScanMetricsUpdater.scala`**
- [ ] **Step 9: Create the native carrier test `cpp/velox/tests/FileCacheGlutenMetricsTest.cc`**

Exercises the extracted production helper `gluten::sumRuntimeMetric` (the exact aggregation `collectMetrics` uses to move `fileCacheWriteBytes` out of `customStats`) and the enum order — NOT merely `IoStats`:

```cpp
#include <gtest/gtest.h>

#include <unordered_map>

#include "compute/RuntimeMetricUtil.h"
#include "core/utils/Metrics.h"

#include "velox/common/base/RuntimeMetrics.h"

using namespace facebook::velox;

TEST(FileCacheGlutenMetricsTest, SumRuntimeMetricReadsFileCacheWriteBytes) {
  // customStats holds an aggregated (sum=12288, count=2) fileCacheWriteBytes.
  std::unordered_map<std::string, RuntimeMetric> customStats;
  customStats.emplace(
      "fileCacheWriteBytes",
      RuntimeMetric(
          /*sum=*/12288, /*count=*/2, /*min=*/4096, /*max=*/8192,
          RuntimeCounter::Unit::kBytes));
  EXPECT_EQ(gluten::sumRuntimeMetric(customStats, "fileCacheWriteBytes"), 12288);
  EXPECT_EQ(gluten::sumRuntimeMetric(customStats, "absent"), 0);
}

TEST(FileCacheGlutenMetricsTest, MetricsEnumOrderCorrect) {
  EXPECT_EQ(
      static_cast<int>(gluten::Metrics::kFileCacheWriteBytes),
      static_cast<int>(gluten::Metrics::kLoadLazyVectorTime) + 1);
  EXPECT_EQ(
      static_cast<int>(gluten::Metrics::kEnd),
      static_cast<int>(gluten::Metrics::kFileCacheWriteBytes) + 1);
}
```

Register and build/run:

```cmake
add_velox_test(velox_file_cache_gluten_metrics_test SOURCES FileCacheGlutenMetricsTest.cc)
```

```bash
cd /root/oss/gluten-019
ninja -C cpp/build velox_file_cache_gluten_metrics_test > cpp/build/build_019d.log 2>&1
ctest --test-dir cpp/build -R '^velox_file_cache_gluten_metrics_test$' \
  --output-on-failure > cpp/build/test_019d.log 2>&1
echo "exit: $?"
```

- [ ] **Step 10: Create the Java carrier test `backends-velox/src/test/java/org/apache/gluten/metrics/MetricsCarrierTest.java`**

Constructs `Metrics` arrays and proves `getOperatorMetrics` carries the exact `fileCacheWriteBytes` value (this is also the target of the statistics mutation in Step 13). Uses length-1 arrays; only `fileCacheWriteBytes` is non-zero:

```java
package org.apache.gluten.metrics;

import org.junit.Assert;
import org.junit.Test;

public class MetricsCarrierTest {
  private static long[] a(long v) {
    return new long[] {v};
  }

  @Test
  public void getOperatorMetricsCarriesFileCacheWriteBytes() {
    final long expected = 987654L;
    // Constructor order matches Metrics.java: 10 arrays, veloxToArrow (scalar),
    // then 34 arrays, then fileCacheWriteBytes, then taskStats.
    Metrics metrics =
        new Metrics(
            a(1), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0),
            0L,
            a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0),
            a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0),
            a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0), a(0),
            a(0), a(0), a(0),
            a(0),
            a(expected),
            "");
    OperatorMetrics op = metrics.getOperatorMetrics(0);
    Assert.assertEquals(expected, op.fileCacheWriteBytes);
  }
}
```

Run (surefire selects the JUnit test; scalatest is disabled with an empty `wildcardSuites`; profiles follow `docs/developers/HowTo.md`):

```bash
cd /root/oss/gluten-019
mvn test -Pspark-3.5 -Pbackends-velox -pl backends-velox \
  -Dtest=MetricsCarrierTest -DwildcardSuites= -DfailIfNoTests=false -q \
  > cpp/build/test_019d_java.log 2>&1
echo "Java carrier test exit: $?"
```

- [ ] **Step 11: Create the Scala updater test `backends-velox/src/test/scala/org/apache/gluten/metrics/FileSourceScanMetricsUpdaterSuite.scala`**

Constructs an `OperatorMetrics`, invokes `FileSourceScanMetricsUpdater`, and proves the `SQLMetric` value increments:

```scala
package org.apache.gluten.metrics

import org.apache.spark.sql.execution.metric.SQLMetric

import org.scalatest.funsuite.AnyFunSuite

class FileSourceScanMetricsUpdaterSuite extends AnyFunSuite {

  test("updateNativeMetrics increments the fileCacheWriteBytes SQLMetric") {
    val sqlMetric = new SQLMetric("size", 0L)
    val updater =
      new FileSourceScanMetricsUpdater(Map("fileCacheWriteBytes" -> sqlMetric))

    // OperatorMetrics: 44 zero longs then fileCacheWriteBytes = 12345.
    val op = new OperatorMetrics(
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0,
      12345L)

    updater.updateNativeMetrics(op)
    assert(sqlMetric.value == 12345L)
  }
}
```

Run (scalatest-maven-plugin selects the suite via `wildcardSuites`; surefire is disabled with `-Dtest=none`):

```bash
cd /root/oss/gluten-019
mvn test -Pspark-3.5 -Pbackends-velox -pl backends-velox \
  -DwildcardSuites=org.apache.gluten.metrics.FileSourceScanMetricsUpdaterSuite \
  -Dtest=none -DfailIfNoTests=false -q \
  > cpp/build/test_019d_scala.log 2>&1
echo "Scala updater test exit: $?"
```

- [ ] **Step 12: JNI signature / full-bridge compile gate**

The native library must compile with the new JNI signature and the Java/Scala bridge must compile with the new constructor arity:

```bash
cd /root/oss/gluten-019
ninja -C cpp/build gluten > cpp/build/build_019d_native.log 2>&1
echo "native build exit: $?"
mvn compile -pl backends-velox -am -DskipTests -q > cpp/build/build_019d_javac.log 2>&1
echo "Java/Scala compile exit: $?"
```

A subagent analyzes each log and returns a concise summary.

- [ ] **Step 13: Statistics mutation — drop the 019-owned Java carrier pass**

**File:** `backends-velox/src/main/java/org/apache/gluten/metrics/Metrics.java`
**Function:** `getOperatorMetrics`

This is the 019-owned statistics carrier. Change the `fileCacheWriteBytes[index]` argument passed to the `new OperatorMetrics` constructor to a wrong constant, e.g.:

```java
        loadLazyVectorTime[index],
        0L); // MUTATION: was fileCacheWriteBytes[index]
```

Re-run the Java carrier test:

```bash
mvn test -Pspark-3.5 -Pbackends-velox -pl backends-velox \
  -Dtest=MetricsCarrierTest -DwildcardSuites= -DfailIfNoTests=false -q
```

**Expected failed assertion:** `MetricsCarrierTest.getOperatorMetricsCarriesFileCacheWriteBytes`'s `assertEquals(expected, op.fileCacheWriteBytes)` fails (`0 != 987654`). Restore after confirming RED.

This mutation stays within Task-019-owned source. The Task-017A `FileCacheInputStream.cpp` `ioStats_->addCounter` line is NOT mutated here (Task 019 owns no line there); the byte-level correctness mutation lives in the accepted Velox-only Task 018 harness (`CacheReadHarness.cpp`).

**Gate (all three carriers + compile gate required):**
- Native: `velox_file_cache_gluten_metrics_test` proves `sumRuntimeMetric` reads the correct key/sum and the enum order.
- Java: `MetricsCarrierTest` proves `getOperatorMetrics` carries the value; the Step 13 mutation confirms coverage.
- Scala: `FileSourceScanMetricsUpdaterSuite` proves the `SQLMetric` increments.
- Compile gate: native `gluten` + `mvn compile` succeed with the new JNI signature and constructor arity.

---

## Task 019-E: Native Gluten Builder / Lifecycle Miss-Fill-Hit E2E

> Migrates the existing Task-019 native Builder/lifecycle E2E contract. Validates the integrated flow through the real `GlutenBufferedInputBuilder` and `VeloxBackend` lifecycle. All paths are in `/root/oss/gluten-019`.

The six E2E behaviors from the old Task-019 contract are:

```text
BuilderProducesFileCacheInputWhenManagerInstalled
BuilderFallsBackToCachedInputWhenNoFileCache
BuilderFallsBackToDirectInputWhenNoCache
MissFillHitViaBuilder
FileCacheExcludesAsyncDataCacheOnSamePath
VeloxBackendTearDownStopsManagerBeforeRuntimeResources
```

They are split across **two** CTest targets so exactly one binary calls `VeloxBackend::create` (glog + the global memory manager initialize once), mirroring the 019-B separation of helper tests from the single-`VeloxBackend` lifecycle test. The five Builder-level behaviors install `FileCacheManager` directly (and, where needed, a real `AsyncDataCache`); the teardown-ordering behavior uses one `VeloxBackend::create`.

**Files (in `/root/oss/gluten-019`):**
- Create: `cpp/velox/tests/FileCacheE2EGlutenTest.cpp` — the five Builder-level E2E tests (fixture, direct install + real `AsyncDataCache`)
- Create: `cpp/velox/tests/FileCacheE2EGlutenLifecycleTest.cpp` — `VeloxBackendTearDownStopsManagerBeforeRuntimeResources` (single `VeloxBackend::create`)
- Modify: `cpp/velox/tests/CMakeLists.txt` — register both targets

**Interfaces:**
- Consumes: 019-C `GlutenBufferedInputBuilder` FileCache branch; `FileCacheManager` install/`getDefault`; `takeFileCacheStatsSnapshot`/`FileCacheStatsSnapshot::operator-` (Task 017A); `cache::AsyncDataCache::create`; `VeloxBackend::create`/`get`/`tearDown`; `AllocationListener::noop`
- Produces: the integrated native E2E gate for the Gluten FileCache path

- [ ] **Step 1: Create `cpp/velox/tests/FileCacheE2EGlutenTest.cpp`**

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "memory/GlutenBufferedInputBuilder.h"

#include "velox/ch/Common/FileCacheStats.h"
#include "velox/ch/Disks/IO/FileCacheBufferedInput.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/ch/Interpreters/FileCache/FileCacheSettings.h"
#include "velox/common/caching/AsyncDataCache.h"
#include "velox/common/caching/FileHandle.h"
#include "velox/common/caching/FileIds.h"
#include "velox/common/file/FileSystems.h"
#include "velox/common/file/LocalFile.h"
#include "velox/common/io/IoStatistics.h"
#include "velox/common/memory/Memory.h"
#include "velox/connectors/Connector.h"
#include "velox/dwio/common/CachedBufferedInput.h"

#include <folly/CancellationToken.h>
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace fs = std::filesystem;
using namespace facebook::velox;

class FileCacheE2EGlutenTest : public ::testing::Test {
 protected:
  std::string testDir_;
  std::string testFile_;
  std::string cacheDir_;
  std::shared_ptr<memory::MemoryPool> pool_;
  std::shared_ptr<filesystems::FileSystem> fs_;
  std::shared_ptr<folly::ThreadWheelTimekeeper> tk_;
  std::shared_ptr<config::ConfigBase> sessionProps_;
  std::shared_ptr<ch::FileCacheManager> fcManager_;
  std::shared_ptr<cache::AsyncDataCache> asyncCache_;

  void SetUp() override {
    filesystems::registerLocalFileSystem();
    testDir_ = fs::absolute("tmp/fc_e2e_gluten_test").string();
    cacheDir_ = testDir_ + "/cache";
    testFile_ = testDir_ + "/data.bin";
    fs::create_directories(testDir_);
    {
      std::ofstream ofs(testFile_, std::ios::binary);
      const std::string data(1u << 16, 'E'); // 64 KiB, larger than one segment read
      ofs.write(data.data(), data.size());
    }
    pool_ = memory::deprecatedAddDefaultLeafMemoryPool("fc_e2e_gluten_test");
    fs_ = filesystems::getFileSystem(cacheDir_, nullptr);
    tk_ = std::make_shared<folly::ThreadWheelTimekeeper>();
    sessionProps_ =
        std::make_shared<config::ConfigBase>(std::unordered_map<std::string, std::string>{});
  }

  void TearDown() override {
    ch::FileCacheManager::setInstance(nullptr);
    fcManager_.reset();
    asyncCache_.reset();
    fs::remove_all(testDir_);
  }

  void installFileCache() {
    ch::FileCacheConfig cfg;
    cfg.path = cacheDir_;
    cfg.maxSize = 64ULL << 20;
    ch::FileCacheManager::Options opts;
    opts.caches = {{.name = "default", .config = cfg, .configPath = cacheDir_}};
    opts.defaultCacheName = "default";
    opts.commonUserId = "test";
    opts.cachePathPrefix = cacheDir_;
    opts.allowedCacheRoot = cacheDir_;
    opts.localFileSystem = fs_;
    opts.memoryPool = pool_.get();
    opts.timekeeper = tk_;
    opts.initializeOnCreate = true;
    fcManager_ = ch::FileCacheManager::create(std::move(opts));
    ch::FileCacheManager::setInstance(fcManager_.get());
  }

  void installAsyncDataCache() {
    asyncCache_ = cache::AsyncDataCache::create(memory::memoryManager()->allocator());
  }

  std::unique_ptr<dwio::common::BufferedInput> runBuilder(
      cache::AsyncDataCache* adc = nullptr,
      folly::CancellationToken token = {}) {
    gluten::GlutenBufferedInputBuilder builder;
    auto readFile = std::make_shared<LocalReadFile>(testFile_);
    FileHandle fileHandle;
    fileHandle.file = readFile;
    fileHandle.uuid = StringIdLease(fileIds(), testFile_);
    fileHandle.groupId = StringIdLease(fileIds(), testFile_);
    dwio::common::ReaderOptions readerOpts(pool_.get());
    auto ioStats = std::make_shared<io::IoStatistics>();
    auto ioStatsObj = std::make_shared<velox::IoStats>();
    connector::ConnectorQueryCtx connCtx(
        pool_.get(),
        pool_.get(),
        sessionProps_.get(),
        nullptr,
        common::PrefixSortConfig{},
        nullptr,
        adc,                        // AsyncDataCache (may be non-null)
        "test-query",
        "task-0",
        "plan-0",
        0,
        "UTC",
        /*adjustTimestampToTimezone=*/false,
        token);
    return builder.create(fileHandle, readerOpts, &connCtx, ioStats, ioStatsObj, nullptr);
  }

  // Reads [0, len) through the given input, draining every buffer so the read
  // actually reaches the FileCache fill/hit path. Returns bytes read.
  static uint64_t drainRead(dwio::common::BufferedInput& input, uint64_t len) {
    auto stream = input.read(0, len, dwio::common::LogType::FILE);
    const void* buf = nullptr;
    int32_t got = 0;
    uint64_t total = 0;
    while (stream->Next(&buf, &got)) {
      total += static_cast<uint64_t>(got);
    }
    return total;
  }
};

TEST_F(FileCacheE2EGlutenTest, BuilderProducesFileCacheInputWhenManagerInstalled) {
  installFileCache();
  auto input = runBuilder();
  ASSERT_NE(dynamic_cast<ch::FileCacheBufferedInput*>(input.get()), nullptr);
}

TEST_F(FileCacheE2EGlutenTest, BuilderFallsBackToCachedInputWhenNoFileCache) {
  // No FileCacheManager; a real AsyncDataCache is present in the query ctx.
  installAsyncDataCache();
  auto input = runBuilder(asyncCache_.get());
  ASSERT_NE(dynamic_cast<dwio::common::CachedBufferedInput*>(input.get()), nullptr);
  EXPECT_EQ(dynamic_cast<ch::FileCacheBufferedInput*>(input.get()), nullptr);
}

TEST_F(FileCacheE2EGlutenTest, BuilderFallsBackToDirectInputWhenNoCache) {
  auto input = runBuilder();
  ASSERT_NE(input.get(), nullptr);
  EXPECT_EQ(dynamic_cast<ch::FileCacheBufferedInput*>(input.get()), nullptr);
  EXPECT_EQ(dynamic_cast<dwio::common::CachedBufferedInput*>(input.get()), nullptr);
}

TEST_F(FileCacheE2EGlutenTest, MissFillHitViaBuilder) {
  installFileCache();
  const uint64_t len = 1u << 16;

  const auto snap0 = ch::takeFileCacheStatsSnapshot();
  {
    auto first = runBuilder();
    ASSERT_NE(dynamic_cast<ch::FileCacheBufferedInput*>(first.get()), nullptr);
    EXPECT_EQ(drainRead(*first, len), len);
  }
  const auto snap1 = ch::takeFileCacheStatsSnapshot();
  const auto fill = snap1 - snap0;
  EXPECT_GT(fill.cacheMissCount, 0u); // cold miss
  EXPECT_GT(fill.cacheWriteBytes, 0u); // filled into cache

  {
    auto second = runBuilder();
    ASSERT_NE(dynamic_cast<ch::FileCacheBufferedInput*>(second.get()), nullptr);
    EXPECT_EQ(drainRead(*second, len), len);
  }
  const auto snap2 = ch::takeFileCacheStatsSnapshot();
  const auto hit = snap2 - snap1;
  EXPECT_GT(hit.cacheHitCount, 0u);   // served from cache
  EXPECT_GT(hit.cacheReadBytes, 0u);  // read from cache
  EXPECT_EQ(hit.cacheWriteBytes, 0u); // no re-fill on the hit
}

TEST_F(FileCacheE2EGlutenTest, FileCacheExcludesAsyncDataCacheOnSamePath) {
  // Both a FileCacheManager and an AsyncDataCache are available; FileCache wins.
  installFileCache();
  installAsyncDataCache();
  auto input = runBuilder(asyncCache_.get());
  ASSERT_NE(dynamic_cast<ch::FileCacheBufferedInput*>(input.get()), nullptr);
  EXPECT_EQ(dynamic_cast<dwio::common::CachedBufferedInput*>(input.get()), nullptr);
}
```

- [ ] **Step 2: Create `cpp/velox/tests/FileCacheE2EGlutenLifecycleTest.cpp`**

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "compute/VeloxBackend.h"
#include "config/VeloxConfig.h"

#include "velox/ch/Common/FileCacheStats.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"

namespace fs = std::filesystem;
using namespace facebook::velox;

// One VeloxBackend::create in this binary. FileCache is enabled and actively
// installed (the manager holds a raw pointer to the dedicated leaf pool). After
// tearDown the singleton must be withdrawn and the process must not abort from a
// dangling pool pointer, proving the manager is shut down before the runtime
// memory resources are released.
TEST(FileCacheE2EGlutenLifecycleTest, VeloxBackendTearDownStopsManagerBeforeRuntimeResources) {
  const std::string dir = fs::absolute("tmp/fc_e2e_lifecycle_test").string();
  fs::create_directories(dir);

  std::unordered_map<std::string, std::string> conf{
      {gluten::kVeloxFileCacheEnabled, "true"},
      {gluten::kVeloxFileCachePath, dir},
      {gluten::kVeloxFileCacheSize, std::to_string(64ULL << 20)}};

  gluten::VeloxBackend::create(gluten::AllocationListener::noop(), conf);
  struct Guard {
    gluten::VeloxBackend* b;
    bool active{true};
    ~Guard() {
      if (active) {
        b->tearDown();
      }
    }
  } guard{gluten::VeloxBackend::get()};

  ASSERT_NE(ch::FileCacheManager::getInstance(), nullptr);
  ASSERT_NE(ch::FileCacheManager::getInstance()->getDefault(), nullptr);

  guard.active = false;
  guard.b->tearDown();
  EXPECT_EQ(ch::FileCacheManager::getInstance(), nullptr);

  fs::remove_all(dir);
}
```

- [ ] **Step 3: Register both targets in `cpp/velox/tests/CMakeLists.txt`**

```cmake
add_velox_test(velox_file_cache_e2e_gluten_test SOURCES FileCacheE2EGlutenTest.cpp)
add_velox_test(velox_file_cache_e2e_gluten_lifecycle_test SOURCES FileCacheE2EGlutenLifecycleTest.cpp)
```

- [ ] **Step 4: Reject false-green tests**

```bash
if rg -n 'GTEST_SKIP|DISABLED_' \
  /root/oss/gluten-019/cpp/velox/tests/FileCacheE2EGlutenTest.cpp \
  /root/oss/gluten-019/cpp/velox/tests/FileCacheE2EGlutenLifecycleTest.cpp
then
  echo "ERROR: skipped Gluten FileCache E2E test remains"
  exit 1
fi
```

- [ ] **Step 5: Build and run both targets**

```bash
cd /root/oss/gluten-019
ninja -C cpp/build velox_file_cache_e2e_gluten_test velox_file_cache_e2e_gluten_lifecycle_test \
  > cpp/build/build_019e.log 2>&1
ctest --test-dir cpp/build \
  -R '^velox_file_cache_e2e_gluten(_lifecycle)?_test$' --output-on-failure \
  > cpp/build/test_019e.log 2>&1
echo "exit: $?"
```

A subagent analyzes `build_019e.log`/`test_019e.log` and returns a concise summary. Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 6: Mutation — remove the FileCache precedence branch**

**File:** `cpp/velox/memory/GlutenBufferedInputBuilder.h`
**Function:** `GlutenBufferedInputBuilder::create`

Comment out the entire `if (fcManager != nullptr)` block, then re-run the E2E fixture target:

```bash
ctest --test-dir cpp/build -R '^velox_file_cache_e2e_gluten_test$' --output-on-failure
```

**Expected failed assertions:** `BuilderProducesFileCacheInputWhenManagerInstalled`, `MissFillHitViaBuilder`, and `FileCacheExcludesAsyncDataCacheOnSamePath` fail (no `FileCacheBufferedInput` is produced; with an AsyncDataCache present the exclusion test now gets a `CachedBufferedInput`). Restore after confirming RED.

**Gate:** Both targets pass; the miss→fill→hit deltas come from the accepted Task-017A `FileCacheStatsSnapshot`; the precedence test proves FileCache wins over a real `AsyncDataCache`; the lifecycle target proves `VeloxBackend::tearDown` withdraws the manager without a dangling-pool abort; the mutation proves the FileCache branch is covered.

---

## Task 019-F: Spark → Gluten → Velox → FileCache Correctness + Performance E2E

> The application-level end-to-end gate. Built on the existing Gluten Spark test harness (`VeloxWholeStageTransformerSuite` → `WholeStageTransformerSuite`) and the exact local metric-inspection idiom used by `backends-velox/src/test/scala/org/apache/gluten/execution/VeloxMetricsSuite.scala` (locating the `FileSourceScanExecTransformer` in `df.queryExecution.executedPlan` and reading its `metrics` map). No cluster-specific or Fabric details are introduced. All paths are in `/root/oss/gluten-019`.

**Files (in `/root/oss/gluten-019`):**
- Create: `backends-velox/src/test/scala/org/apache/gluten/execution/VeloxFileCacheSuite.scala` — two suites: FileCache-enabled correctness/performance and a FileCache-disabled negative control

**Interfaces:**
- Consumes: the 019-B config keys (`spark.gluten.sql.columnar.backend.velox.fileCache*`) and `kVeloxCacheEnabled` (`spark.gluten.sql.columnar.backend.velox.cacheEnabled`); the 019-D `fileCacheWriteBytes` `SQLMetric` registered in `genFileSourceScanTransformerMetricsFull`; the freshly built RelWithDebInfo native `gluten` library
- Produces: the Spark → Gluten → Velox → FileCache correctness + baseline-performance gate

**Prerequisite native build.** The suite runs through the standard Gluten Maven test harness, which loads the native library built in `cpp/build`. The 019-F **state (performance) benchmark must run against a RelWithDebInfo or Release native build** (`ninja -C cpp/build gluten` from 019-A/019-D, configured `-DCMAKE_BUILD_TYPE=RelWithDebInfo`). Debug native output is invalid as performance evidence.

- [ ] **Step 1: Create `backends-velox/src/test/scala/org/apache/gluten/execution/VeloxFileCacheSuite.scala`**

```scala
/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.apache.gluten.execution

import org.apache.spark.SparkConf
import org.apache.spark.sql.DataFrame
import org.apache.spark.sql.execution.adaptive.AdaptiveSparkPlanHelper

import java.io.{File, FileWriter}

object VeloxFileCacheSuite {
  def deleteRecursively(f: File): Unit = {
    if (f.isDirectory) {
      Option(f.listFiles()).foreach(_.foreach(deleteRecursively))
    }
    f.delete()
  }

  def scanWriteBytes(helper: AdaptiveSparkPlanHelper, df: DataFrame): Long = {
    val scan = helper.find(df.queryExecution.executedPlan) {
      case _: FileSourceScanExecTransformer => true
      case _ => false
    }
    assert(scan.isDefined, "expected a native FileSourceScanExecTransformer")
    val metrics = scan.get.metrics
    assert(metrics.contains("fileCacheWriteBytes"), "fileCacheWriteBytes SQLMetric missing")
    metrics("fileCacheWriteBytes").value
  }
}

class VeloxFileCacheSuite extends VeloxWholeStageTransformerSuite with AdaptiveSparkPlanHelper {
  import VeloxFileCacheSuite._

  override protected val resourcePath: String = "/tpch-data-parquet"
  override protected val fileFormat: String = "parquet"

  private val cacheDir: String = new File("target/fc_e2e_cache").getAbsolutePath

  override protected def sparkConf: SparkConf = {
    // Cold cache: remove any prior contents so the first scan is a real miss.
    deleteRecursively(new File(cacheDir))
    new File(cacheDir).mkdirs()
    super.sparkConf
      .set("spark.gluten.sql.columnar.backend.velox.cacheEnabled", "false")
      .set("spark.gluten.sql.columnar.backend.velox.fileCacheEnabled", "true")
      .set("spark.gluten.sql.columnar.backend.velox.fileCachePath", cacheDir)
      .set("spark.gluten.sql.columnar.backend.velox.fileCacheSize", (1L << 30).toString)
  }

  override def beforeAll(): Unit = {
    super.beforeAll()
    spark
      .range(100000)
      .selectExpr("id as c1", "id % 7 as c2")
      .write
      .format("parquet")
      .saveAsTable("fc_e2e_t1")
  }

  override protected def afterAll(): Unit = {
    spark.sql("drop table if exists fc_e2e_t1")
    super.afterAll()
  }

  test("FileCache fills on the cold scan and serves the warm re-scan") {
    val query = "SELECT c2, count(*) FROM fc_e2e_t1 GROUP BY c2"

    // Cold run: Gluten result equals vanilla Spark (runQueryAndCompare) AND the
    // FileCache is filled (positive fileCacheWriteBytes).
    var coldWrite = 0L
    runQueryAndCompare(query) { df => coldWrite = scanWriteBytes(this, df) }
    assert(coldWrite > 0, s"cold scan must fill the FileCache, got $coldWrite")

    // Warm run over the same files: still correct, and no new bytes written
    // (served from the cache).
    var warmWrite = -1L
    runQueryAndCompare(query) { df => warmWrite = scanWriteBytes(this, df) }
    assert(warmWrite == 0, s"warm scan must be served from cache with no new writes, got $warmWrite")
  }

  test("state benchmark: record cold vs warm scan wall-time (requires non-Debug build)") {
    // Distinct table so the first scan is a genuine miss regardless of test order.
    val perfTable = "fc_e2e_perf_t1"
    spark
      .range(2000000)
      .selectExpr("id as c1", "id % 13 as c2")
      .write
      .format("parquet")
      .mode("overwrite")
      .saveAsTable(perfTable)
    val query = s"SELECT c2, count(*) FROM $perfTable GROUP BY c2"

    def timedScan(): (Long, Long, Long) = {
      val df = spark.sql(query)
      val t0 = System.nanoTime()
      val rows = df.collect()
      val nanos = System.nanoTime() - t0
      (nanos, rows.length.toLong, scanWriteBytes(this, df))
    }

    val (coldNanos, coldRows, coldWrite) = timedScan() // miss -> fill
    val (warmNanos, warmRows, warmWrite) = timedScan() // served from cache

    assert(coldRows == warmRows, "cold/warm result cardinality must match")
    assert(coldWrite > 0, s"cold scan must fill the FileCache, got $coldWrite")
    assert(warmWrite == 0, s"warm scan must be served from cache, got $warmWrite")

    // Baseline-only evidence (no hard threshold): record both timings.
    val csv = new File("target/fc_e2e_perf.csv")
    val w = new FileWriter(csv, true)
    try {
      w.write(s"$perfTable,cold_ns=$coldNanos,warm_ns=$warmNanos,cold_write=$coldWrite\n")
    } finally {
      w.close()
    }
    spark.sql(s"drop table if exists $perfTable")
  }
}

class VeloxFileCacheDisabledSuite extends VeloxWholeStageTransformerSuite with AdaptiveSparkPlanHelper {
  import VeloxFileCacheSuite._

  override protected val resourcePath: String = "/tpch-data-parquet"
  override protected val fileFormat: String = "parquet"

  override protected def sparkConf: SparkConf = {
    super.sparkConf
      .set("spark.gluten.sql.columnar.backend.velox.cacheEnabled", "false")
      .set("spark.gluten.sql.columnar.backend.velox.fileCacheEnabled", "false")
  }

  override def beforeAll(): Unit = {
    super.beforeAll()
    spark
      .range(100000)
      .selectExpr("id as c1", "id % 7 as c2")
      .write
      .format("parquet")
      .saveAsTable("fc_e2e_off_t1")
  }

  override protected def afterAll(): Unit = {
    spark.sql("drop table if exists fc_e2e_off_t1")
    super.afterAll()
  }

  test("negative control: fileCacheWriteBytes stays zero when FileCache is disabled") {
    runQueryAndCompare("SELECT c2, count(*) FROM fc_e2e_off_t1 GROUP BY c2") { df =>
      assert(scanWriteBytes(this, df) == 0, "FileCache disabled must not write any bytes")
    }
  }
}
```

- [ ] **Step 2: Reject false-green tests**

```bash
if rg -n 'ignore\(|pending|assume\(' \
  /root/oss/gluten-019/backends-velox/src/test/scala/org/apache/gluten/execution/VeloxFileCacheSuite.scala
then
  echo "ERROR: skipped/ignored Spark FileCache E2E test remains"
  exit 1
fi
```

- [ ] **Step 3: Build the RelWithDebInfo native library the suite loads**

```bash
cd /root/oss/gluten-019
ninja -C cpp/build gluten > cpp/build/build_019f_native.log 2>&1
echo "native build exit: $?"
```

Confirm this is a non-Debug build (`grep CMAKE_BUILD_TYPE cpp/build/CMakeCache.txt` → `RelWithDebInfo` or `Release`). A subagent analyzes `build_019f_native.log`.

- [ ] **Step 4: Run both Spark suites**

```bash
cd /root/oss/gluten-019
mvn test -Pspark-3.5 -Pbackends-velox -pl backends-velox \
  -DwildcardSuites=org.apache.gluten.execution.VeloxFileCacheSuite,org.apache.gluten.execution.VeloxFileCacheDisabledSuite \
  -Dtest=none -DfailIfNoTests=false -q \
  > cpp/build/test_019f_scala.log 2>&1
echo "Spark E2E exit: $?"
```

A subagent analyzes `test_019f_scala.log` and returns a concise summary. Expected: all three tests pass; `target/fc_e2e_perf.csv` contains one baseline row.

- [ ] **Step 5: Mutation — disable the FileCache branch in the Builder**

**File:** `cpp/velox/memory/GlutenBufferedInputBuilder.h`
**Function:** `GlutenBufferedInputBuilder::create`

Comment out the entire `if (fcManager != nullptr)` block, rebuild native, and re-run `VeloxFileCacheSuite`:

```bash
ninja -C cpp/build gluten > cpp/build/build_019f_mutation.log 2>&1
mvn test -Pspark-3.5 -Pbackends-velox -pl backends-velox \
  -DwildcardSuites=org.apache.gluten.execution.VeloxFileCacheSuite \
  -Dtest=none -DfailIfNoTests=false -q
```

**Expected failure:** `FileCache fills on the cold scan and serves the warm re-scan` fails its `coldWrite > 0` assertion (the scan no longer produces a `FileCacheBufferedInput`, so `fileCacheWriteBytes` stays 0). Restore after confirming RED.

**Gate:** `VeloxFileCacheSuite` passes (Gluten result equals vanilla Spark; cold scan fills the FileCache with positive `fileCacheWriteBytes`; warm scan is served from cache with zero new writes); `VeloxFileCacheDisabledSuite` proves the metric stays 0 when disabled (non-vacuous positive test); the state benchmark records a cold/warm baseline row from a non-Debug build; the mutation proves the end-to-end FileCache path is what the test exercises.

---

## Prerequisites

Task 019 starts only after **all** of the following are accepted, verified by 019-A Step 1:

```text
Task 017A (statistics, cancellation, caller identity, scheduler parity) — accepted
Velox-only Task 018 (correctness harness + benchmarks; no Gluten) — accepted
Review 5 (Tasks 003-018 Velox whole-port review) — accepted
Task 017B (logging + exception stack formatting) — accepted
```

Additionally, 019-A (compatible Velox baseline) is a hard gate for 019-B..019-F.

## Existing work in progress

The uncommitted former-018-E files in `/root/oss/gluten-018` are preserved as Task-019 WIP; they are **not** accepted Task-018 changes. Before execution (Environment Setup) the worktree/branch is moved/renamed to Task-019 naming with no rebase and no amend. After the move:

```text
review the WIP diff against the 019-A compatible Velox baseline before building;
rerun all lifecycle tests, including the real cold FileCacheBufferedInput read (019-B Step 8);
keep WIP that satisfies or strengthens this plan's behavioral contract; do not
replace stronger evidence merely to match the snippets textually. Re-derive only
behavior that conflicts with the reviewed contract.
```

## Worktree isolation and commit rule

Task 019 uses an isolated Gluten worktree (`/root/oss/gluten-019`). The original dirty `/root/oss/gluten` (branch `main`) is never modified. The Worker never stages or commits in either repository; the Controller reviews the complete Velox and isolated-Gluten diffs together and commits accepted work with new commits only (never rebase or amend).

## Result handoff

Write `<clickhouse_repo>/port/task/result/019-filecache-gluten-integration-spark-e2e-result.md` with:

```text
status: success / blocked / failed
compatible Velox branch/HEAD and Gluten worktree branch/HEAD, dirty status
per-subtask gate results (019-A..019-F) with log paths
mutation RED evidence per subtask
first actionable error if blocked/failed
recommended next task
```

## Explicit exclusions

```text
write-through cache
overcommit priority
new FileCache production behavior beyond the accepted Task 017A/018 surface
```

The Spark/Scala application-level integration suite is **no longer excluded** — it is delivered by 019-F.

## Plan self-review

- **Spec coverage:** hard-split design §4 (019-A..019-F), §5 (compatible Velox), §6 (WIP move/rename, rerun cold read), §7 (owner) each map to a task/section above; former 018-E→019-B, 018-F→019-C, 018-G→019-D, old-Task-019 six behaviors→019-E, Spark E2E→019-F.
- **Type consistency:** the FileCache config keys, `FileCacheManager`/`FileCacheBufferedInput`/`takeFileCacheStatsSnapshot` surface, the `kFileCacheWriteBytes` metric name, the `OperatorMetrics`/`Metrics` constructor arity, and the JNI 45-array signature are used consistently across 019-B..019-F and match the live Gluten sources verified at authoring time.
- **No placeholders:** every code step shows complete code; every command shows the exact invocation and expected result. Baseline-dependent uncertainty is confined to 019-A, which produces a reviewed compatibility contract with an explicit stop/redispatch decision rather than a deferred placeholder.
