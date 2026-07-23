# Task 018-H2 Parallel Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make four-driver TPCH performance correctness-equivalent by counting final result rows and comparing timed results to an untimed one-driver reference with Velox's epsilon-aware comparator.

**Architecture:** `AbBenchmarkBase` optionally collects one-driver result vectors before timed rounds, restores the requested driver count, clears the active application cache, and verifies every timed result in-process. Exact hashes remain diagnostic; `result_match` becomes the parallel correctness gate.

**Tech Stack:** C++20, Velox `assertEqualResults`, GoogleTest, CMake/Ninja, Bash, RelWithDebInfo.

## Global Constraints

```text
Binding design: port/design/filecache-task-018-parallel-tpch-verification.md
Task 018 remains Velox-only.
Do not change TPCH plans, execution semantics, FileCache behavior, Gluten, or Spark.
Four-driver H2 uses num_drivers=4 and reference_num_drivers=1.
Reference work is outside wall_ms and is followed by one application-cache reset.
Rows count final returned RowVectors, never pipeline outputPositions.
Exact result_hash remains diagnostic and is not a four-driver equality gate.
result_match=1 is mandatory for every four-driver timed row.
No fallback from comparison mismatch to timing-only success.
Worker does not stage or commit.
No Ninja -j or nproc; every build/test/query command writes a unique build-dir log.
```

---

### Task 1: Final-row accounting and 15-field schema

**Files:**
- Modify: `/root/oss/velox/velox/benchmarks/AbBenchmarkBase.h`
- Modify: `/root/oss/velox/velox/benchmarks/AbBenchmarkBase.cpp`
- Modify: `/root/oss/velox/velox/benchmarks/tests/AbBenchmarkSchemaTest.cpp`
- Modify: `/root/oss/velox/velox/benchmarks/tests/CMakeLists.txt`
- Modify: `/root/oss/velox/velox/benchmarks/CMakeLists.txt`

**Interfaces:**
- Produces: `countResultRows`, `computeResultHash`, optional `AbCsvRow::resultMatch`
- Produces exact 15-field CSV schema

- [ ] **Step 1: Add RED schema tests**

Change the expected header to:

```text
round,query_id,wall_ms,rows,result_hash,result_match,bytes_read,hit_pct,cache_read_mib,predownload_mib,evict_mib,evict_count,op_p50_us,op_p95_us,error
```

Add tests that serialize:

```text
resultMatch = nullopt -> empty field
resultMatch = true    -> 1
resultMatch = false   -> 0
```

Each row and the header must contain 15 fields (14 commas).

- [ ] **Step 2: Add RED final-row helper test**

Create two result `RowVector`s with sizes 2 and 3 plus one null entry. Assert:

```cpp
EXPECT_EQ(countResultRows({rows2, nullptr, rows3}), 5);
```

This test fails because the helper does not exist. It explicitly proves that
pipeline/operator statistics are not part of final result cardinality.

- [ ] **Step 3: Add RED exact-hash helper test**

Create deterministic rows and assert `computeResultHash` equals the sum of
`RowVector::hashValueAt` across every returned row. Exact hash remains unchanged
for one-driver diagnostics.

- [ ] **Step 4: Run RED**

Build and run `velox_ab_benchmark_schema_test`:

```text
_build/relwithdebinfo/build_parallel_verify_schema_red.log
_build/relwithdebinfo/test_parallel_verify_schema_red.log
```

Expected: compile/test failure for missing fields/helpers and old 14-field
schema.

- [ ] **Step 5: Implement the minimal helpers and schema**

In `AbBenchmarkBase.h`:

```cpp
struct AbCsvRow {
  // existing fields...
  std::optional<bool> resultMatch;
};

uint64_t countResultRows(const std::vector<RowVectorPtr>& results);
uint64_t computeResultHash(const std::vector<RowVectorPtr>& results);
```

In `AbBenchmarkBase.cpp`:

```cpp
uint64_t countResultRows(const std::vector<RowVectorPtr>& results) {
  uint64_t rows = 0;
  for (const auto& result : results) {
    if (result != nullptr) {
      rows += result->size();
    }
  }
  return rows;
}

uint64_t computeResultHash(const std::vector<RowVectorPtr>& results) {
  uint64_t hash = 0;
  for (const auto& result : results) {
    if (result == nullptr) {
      continue;
    }
    for (vector_size_t row = 0; row < result->size(); ++row) {
      hash += result->hashValueAt(row);
    }
  }
  return hash;
}
```

Serialize `result_match` immediately after `result_hash`:

```cpp
const std::string resultMatch = !row.resultMatch.has_value()
    ? ""
    : (*row.resultMatch ? "1" : "0");
```

- [ ] **Step 6: Replace pipeline row accounting**

Inside `runAb`:

```cpp
row.rows = countResultRows(results);
row.resultHash = computeResultHash(results);
```

Delete:

```cpp
row.rows += pipeline.operatorStats.back().outputPositions;
```

Pipeline stats continue to populate only bytes and operator timing.

- [ ] **Step 7: Run GREEN and mutation**

Run the schema target GREEN. Then mutate `countResultRows` to use the first
pipeline/output statistic or return zero; the final-row helper test must fail.
Restore and rerun GREEN.

Expected final log:

```text
_build/relwithdebinfo/test_parallel_verify_schema_restore.log
```

---

### Task 2: In-process one-driver reference verification

**Files:**
- Modify: `/root/oss/velox/velox/benchmarks/AbBenchmarkBase.h`
- Modify: `/root/oss/velox/velox/benchmarks/AbBenchmarkBase.cpp`
- Modify: `/root/oss/velox/velox/benchmarks/tests/AbBenchmarkSchemaTest.cpp`

**Interfaces:**
- Consumes: `exec::test::assertEqualResults`
- Produces: `reference_num_drivers` flag and `result_match`

- [ ] **Step 1: Add RED flag-validation tests**

Add `velox_exec_test_lib` to `velox_benchmark_ab`'s direct link dependencies
because production `AbBenchmarkBase.cpp` calls
`exec::test::assertEqualResults`. The schema test receives the dependency
transitively from the library it tests.

Expose:

```cpp
void validateReferenceDrivers(
    int32_t referenceDrivers,
    int32_t requestedDrivers);
```

Test:

```text
(0, 4) allowed
(1, 4) allowed
(4, 4) allowed
(-1, 4) throws
(5, 4) throws
(1, 0) throws
```

- [ ] **Step 2: Add RED epsilon-comparison tests**

Create reference and actual `RowVector`s with unique BIGINT keys and DOUBLE
values:

```text
reference: (1, 5.660776097195746e12)
actual:    (1, 5.660776097193966e12)
```

Assert `assertEqualResults` returns true.

Also prove false for:

```text
changed BIGINT key
missing row
materially different DOUBLE
```

`assertEqualResults` emits a nonfatal GTest failure on mismatch. Wrap each
expected-false case with `EXPECT_NONFATAL_FAILURE` from
`<gtest/gtest-spi.h>` while also checking the returned boolean.

- [ ] **Step 3: Run RED**

Use unique build/test logs:

```text
_build/relwithdebinfo/build_parallel_verify_reference_red.log
_build/relwithdebinfo/test_parallel_verify_reference_red.log
```

- [ ] **Step 4: Add the flag and validation**

Define:

```cpp
DEFINE_int32(
    reference_num_drivers,
    0,
    "If positive, run each query once with this driver count outside timing "
    "and compare every timed result using Velox epsilon-aware result equality.");
```

Implement the validation exactly as the design requires.

- [ ] **Step 5: Collect references outside timing**

After plans are built and before opening timed rounds:

```cpp
std::vector<std::unique_ptr<exec::TaskCursor>> referenceCursors;
std::vector<std::vector<RowVectorPtr>> referenceResults;
const int32_t requestedDrivers = FLAGS_num_drivers;
validateReferenceDrivers(FLAGS_reference_num_drivers, requestedDrivers);

if (FLAGS_reference_num_drivers > 0) {
  referenceResults.reserve(plans.size());
  referenceCursors.reserve(plans.size());
  FLAGS_num_drivers = FLAGS_reference_num_drivers;
  auto restoreDrivers = folly::makeGuard(
      [&] { FLAGS_num_drivers = requestedDrivers; });

  for (size_t i = 0; i < plans.size(); ++i) {
    auto [cursor, results] = run(plans[i], queryConfigs_);
    VELOX_USER_CHECK(
        cursor != nullptr,
        "Reference query q{:02d} failed",
        queryIds[i]);
    referenceCursors.push_back(std::move(cursor));
    referenceResults.push_back(std::move(results));
  }

  FLAGS_num_drivers = requestedDrivers;
  restoreDrivers.dismiss();
  VELOX_USER_CHECK(
      static_cast<bool>(coldResetFn_),
      "reference_num_drivers requires a backend reset callback");
  coldResetFn_();
}
```

Reference runs are never included in `wall_ms`.

- [ ] **Step 6: Compare every timed result**

After computing actual rows/hash:

```cpp
if (!referenceResults.empty()) {
  const bool matches =
      exec::test::assertEqualResults(referenceResults[i], results);
  row.resultMatch = matches;
  if (!matches) {
    row.error = "result mismatch against one-driver reference";
    ++failed;
  }
}
```

Continue collecting backend and operator metrics for mismatch rows, then emit
the nonempty error and return a nonzero process exit through `abExitCode`.

- [ ] **Step 7: Run q01 four-driver integration GREEN**

Run direct q01:

```text
num_drivers=4
reference_num_drivers=1
rounds=2
```

Require:

```text
rows=4
result_match=1
error empty
exact result_hash may differ between rounds
```

Logs/artifact:

```text
_build/relwithdebinfo/test_parallel_verify_q01_direct.log
tmp/parallel_verify_q01_direct.csv
```

- [ ] **Step 8: Mutation RED**

Disable `assertEqualResults` or force `resultMatch=true`, then perturb an actual
result in the focused unit test. The mismatch test must fail. Restore and rerun
unit plus q01 integration GREEN.

---

### Task 3: Four-driver orchestration

**Files:**
- Modify: `/root/oss/velox/velox/benchmarks/scripts/run_tpch_ab.sh`
- Test: `/root/oss/velox/tmp/task-018-h2/e2e_test.sh`

**Interfaces:**
- Produces: `NUM_DRIVERS=4`, `REFERENCE_NUM_DRIVERS=1`
- Passes both values to every backend

- [ ] **Step 1: Add RED fake-binary assertions**

The existing fake harness must assert every mode receives:

```text
--num_drivers=4
--reference_num_drivers=1
```

Run against the current one-driver script. Expected: six new assertions fail.

Log:

```text
_build/relwithdebinfo/test_parallel_verify_script_red.log
```

- [ ] **Step 2: Implement script defaults and propagation**

Set:

```bash
: "${NUM_DRIVERS:=4}"
: "${REFERENCE_NUM_DRIVERS:=1}"
```

Add to common args:

```bash
--reference_num_drivers="$REFERENCE_NUM_DRIVERS"
```

- [ ] **Step 3: GREEN, gate, cleanup, mutation**

Run the full fake harness. Approval ordering, mode-specific memory, outputs,
sentinel cleanup, and exit-code propagation must remain green.

Mutate `REFERENCE_NUM_DRIVERS=0`; reference assertions must fail. Restore and
rerun GREEN.

- [ ] **Step 4: Syntax and review**

Run `bash -n`, `git diff --check`, independent task review, and leave changes
unstaged for Controller commit.

---

### Task 4: Re-run four-driver correctness and H2

**Files:**
- Append: `/root/oss/clickhouse/port/task/result/018-filecache-velox-benchmark-result.md`
- Artifacts: unique `/root/oss/velox/tmp/parallel_verify_*` paths
- Logs: unique RelWithDebInfo build-directory logs

**Interfaces:**
- Consumes: accepted parallel verifier and script
- Produces: valid four-driver H2 evidence

- [ ] **Step 1: Build focused targets**

Build:

```text
velox_ab_benchmark_schema_test
velox_tpch_benchmark
```

Run focused tests and q01 integration. Stop on any mismatch.

- [ ] **Step 2: Run all 22 × 3 backends × 3 rounds**

Use:

```text
num_drivers=4
reference_num_drivers=1
num_splits_per_file=1
query memory=32 GiB all modes
CBI cache=4 GiB dedicated
FileCache disk=80 GiB
```

For all 198 timed rows require:

```text
15-field schema
error empty
result_match=1
rows equal final reference result rows
real CBI/FileCache metrics
```

Exact `result_hash` is recorded but not compared.

- [ ] **Step 3: Parse performance**

Report R1 cold and R2/R3 warm medians and variation. Keep the valid one-driver
result as a separate baseline; mark only the old pre-adapter and old
unverified-four-driver runs invalid.

- [ ] **Step 4: Independent reviews**

Run:

```text
task correctness/evidence review
local performance review
```

Task 018's existing acceptance is not replaced until Controller accepts the
new four-driver addendum. Any mismatch preserves the one-driver accepted result
and blocks only the four-driver addendum.

- [ ] **Step 5: Commit and push**

Controller commits the benchmark verifier and script as new Velox commits,
commits the receipt/plan addendum in ClickHouse, and pushes both existing
branches only after all gates pass.
