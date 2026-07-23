# Task 018-H2 Q15 Parallel Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make TPCH q15 return the same supplier result under one-driver and four-driver execution without exact equality between independently computed double aggregates.

**Architecture:** Replace the duplicated revenue aggregation and floating hash join with one supplier-revenue aggregation followed by `topNRank("dense_rank", ..., limit=1)` over descending revenue.

**Tech Stack:** C++20, Velox `PlanBuilder`, TPCH SF100, RelWithDebInfo.

## Global Constraints

```text
Binding design: port/design/filecache-task-018-q15-parallel-fix.md
Modify only TpchQueryBuilder.cpp.
Preserve q15 tie semantics with dense_rank.
Do not use epsilon join, rounded values, or q15 exclusion.
Do not run full H2 after verification.
Worker does not stage or commit.
No Ninja -j or nproc; logs use unique RelWithDebInfo build paths.
```

---

### Task 1: Rewrite q15 to aggregate revenue once

**Files:**
- Modify: `/root/oss/velox/velox/exec/tests/utils/TpchQueryBuilder.cpp:1709-1791`

**Interfaces:**
- Consumes: existing q15 file metadata and `PlanBuilder`
- Produces: one `lineitem` scan ID and a `supplier_no,total_revenue` rank-1 plan

- [ ] **Step 1: Preserve behavioral RED evidence**

Record:

```text
tmp/parallel_verified4_ab_results/tpch_direct.csv
q15 rounds 1-2: rows=0, result_match=0
q15 round 3: rows=1, result_match=1
```

- [ ] **Step 2: Replace duplicate q15 subtrees**

Use one lineitem scan:

```cpp
core::PlanNodeId lineitemScanNodeId;
core::PlanNodeId supplierScanNodeId;

auto supplierWithMaxRevenue =
    PlanBuilder(planNodeIdGenerator, pool_.get())
        .filtersAsNode(filtersAsNode_)
        .tableScan(
            kLineitem,
            lineitemSelectedRowType,
            lineitemFileColumns,
            {shipDateFilter})
        .captureScanNodeId(lineitemScanNodeId)
        .project(
            {"l_suppkey as supplier_no",
             "l_extendedprice * (1.0 - l_discount) as part_revenue"})
        .partialAggregation(
            {"supplier_no"}, {"sum(part_revenue) as total_revenue"})
        .localPartition(std::vector<std::string>{})
        .finalAggregation()
        .topNRank(
            "dense_rank",
            {},
            {"total_revenue DESC"},
            1,
            false)
        .planNode();
```

Keep the supplier join and final ordering unchanged.

- [ ] **Step 3: Register one lineitem input**

Replace the two lineitem data-file mappings with:

```cpp
context.dataFiles[lineitemScanNodeId] = getTableFilePaths(kLineitem);
context.dataFiles[supplierScanNodeId] = getTableFilePaths(kSupplier);
```

- [ ] **Step 4: Build**

```bash
ninja -C /root/oss/velox/_build/relwithdebinfo velox_tpch_benchmark \
  > /root/oss/velox/_build/relwithdebinfo/build_q15_parallel_fix.log 2>&1
```

Expected: exit 0, no warnings/errors.

---

### Task 2: Focused q15 verification

**Files:**
- Artifacts: `/root/oss/velox/tmp/q15_parallel_fix/`
- Logs: `/root/oss/velox/_build/relwithdebinfo/test_q15_parallel_fix_*.log`

**Interfaces:**
- Consumes: fixed q15 plan and parallel verifier
- Produces: one/four-driver equivalence evidence

- [ ] **Step 1: Run one-driver direct baseline**

First build and run the existing DuckDB correctness oracle:

```bash
ninja -C /root/oss/velox/_build/relwithdebinfo \
  velox_dwio_parquet_tpch_test \
  > /root/oss/velox/_build/relwithdebinfo/build_q15_duckdb_oracle.log 2>&1

/root/oss/velox/_build/relwithdebinfo/velox/dwio/parquet/tests/velox_dwio_parquet_tpch_test \
  --gtest_filter='ParquetTpchTest.Q15' \
  > /root/oss/velox/_build/relwithdebinfo/test_q15_duckdb_oracle.log 2>&1
```

Expected: `ParquetTpchTest.Q15` passes against DuckDB.

Use:

```text
input_source=direct
query_id=15
num_drivers=1
reference_num_drivers=0
rounds=1
```

Require one row, empty error.

- [ ] **Step 2: Run direct four-driver verification**

Use:

```text
input_source=direct
query_id=15
num_drivers=4
reference_num_drivers=1
rounds=3
```

Require all rounds:

```text
rows=1
result_match=1
error empty
```

- [ ] **Step 3: Run CBI and FileCache**

Use four/reference-one, query memory 32 GiB, CBI cache 4 GiB, FileCache disk
80 GiB. Require the same result gates and real cache metrics.

- [ ] **Step 4: Mutation RED**

Temporarily restore the previous duplicate revenue aggregations and exact
`total_revenue=max_revenue` join. Rebuild and run direct four-driver q15 for
three rounds. At least one row must report `result_match=0` and nonempty error.
Restore the dense-rank plan, rebuild, and rerun direct GREEN.

- [ ] **Step 5: Final gates**

Run `git diff --check`, confirm only `TpchQueryBuilder.cpp` changed, dispatch an
independent review, and leave the change unstaged.

Controller commits the q15 fix only after review. Stop after focused q15
verification; do not run full four-driver H2.
