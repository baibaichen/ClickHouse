# Task 018 Parallel TPCH Verification Design

## Status

```text
decision_date: 2026-07-23
decision: approved
selected_approach: in-process one-driver reference plus epsilon comparison
```

## Problem

The first four-driver H2 run was rejected because its CSV `rows` and
`result_hash` did not match the accepted one-driver values. Investigation
separated two benchmark-accounting issues from query correctness:

1. `AbBenchmarkBase` computes `rows` by summing the last operator's
   `outputPositions` in every pipeline. For q01 this counts the PARTIAL
   aggregation output: four groups per driver × four drivers = 16. The actual
   FINAL aggregation and `OrderBy` output remain four rows.
2. `result_hash` hashes exact floating-point output values. Parallel partial
   aggregation changes addition order, so large double sums differ at roughly
   `1e-10` relative scale between one-driver and four-driver runs and between
   independent four-driver runs. Integer sums and group keys remain equal.

Direct verbose q01 evidence confirms the actual result shape is correct:

```text
one driver: 4 final rows
four drivers: 4 final rows
four-driver PARTIAL aggregation: 16 intermediate rows
```

Velox already provides `assertEqualResults`, which compares aggregation
floating-point output with epsilon while requiring exact non-floating values
and matching row sets.

## Selected architecture

The A/B process optionally builds an in-process correctness reference before
timed rounds:

```text
build plans
if reference_num_drivers > 0:
  save requested num_drivers
  run each query once with reference_num_drivers
  retain returned RowVectors
  restore requested num_drivers
  reset the active application cache
run timed rounds with requested num_drivers
compare each timed result to its query reference with assertEqualResults
```

Task 018 four-driver H2 uses:

```text
num_drivers=4
reference_num_drivers=1
```

The reference run is outside every `wall_ms` measurement. After all references
are collected, the existing backend reset callback clears CBI or recreates
FileCache, so performance round 1 remains application-cache cold. Direct reset
is a no-op. Reference reads may warm the OS page cache for every backend; H2 is
an application-cache comparison, not a physical-device-cold benchmark.

## Result accounting

`rows` becomes the true final result count:

```text
sum(rv->size()) for all non-null returned result RowVectors
```

Pipeline/operator statistics remain the source for `bytes_read` and operator
timing only. Intermediate operator `outputPositions` never contributes to
final result rows.

`result_hash` remains the exact commutative hash of the actual timed result.
It is useful diagnostic evidence and remains stable for one-driver runs, but
four-driver gates do not compare it exactly.

Add a CSV field:

```text
result_match
```

Its values are:

```text
empty  reference verification disabled
1      actual results equal reference using assertEqualResults
0      mismatch
```

The exact schema becomes:

```text
round,query_id,wall_ms,rows,result_hash,result_match,bytes_read,hit_pct,cache_read_mib,predownload_mib,evict_mib,evict_count,op_p50_us,op_p95_us,error
```

A result mismatch sets `result_match=0`, writes a nonempty error, increments the
failed-query count, and makes the process exit nonzero. There is no fallback to
exact hash or timing-only success.

## Reference comparison

Use the existing:

```cpp
exec::test::assertEqualResults(reference, actual)
```

The comparator:

```text
requires matching row-set cardinality;
compares non-floating values exactly;
groups aggregation rows by non-floating keys when those keys are unique;
compares REAL/DOUBLE values using Variant::equalsWithEpsilon;
falls back to exact comparison when epsilon comparison is unsupported.
```

Do not add a Task-specific floating-point tolerance, rounded hash, or result
serialization format.

## Flags

Add:

```text
reference_num_drivers
```

Default is `0` (disabled). Validation:

```text
0                           allowed
1..num_drivers              allowed
negative                    rejected
greater than num_drivers    rejected
```

The H2 orchestrator defaults to:

```text
NUM_DRIVERS=4
REFERENCE_NUM_DRIVERS=1
```

Every backend receives both values. Explicit overrides remain possible, but a
Task-018 H2 result is accepted only with 4/1.

## Failure and lifecycle behavior

If a reference query fails, execution stops before performance rounds.

The reference `RowVector` objects own their result buffers and remain valid for
the process lifetime memory pool. Reference cursors/tasks are released after
their results are retained.

After reference collection:

```text
FileCache: shutdown old manager, withdraw singleton, recreate empty cache
CBI: clear AsyncDataCache
Direct: no-op
```

Timed round errors, comparator mismatch, malformed result vectors, or cache
reset failure all propagate as task failure.

## Tests

Focused tests prove:

1. final result row count sums returned `RowVector::size`, independent of
   pipeline stats;
2. q01-style one-driver and parallel floating aggregates compare equal within
   the existing epsilon;
3. changed group keys, integer values, row count, or materially different
   doubles compare unequal;
4. `result_match` serializes as empty/1/0 in the 15-field schema;
5. reference flag validation rejects invalid combinations;
6. cache reset executes exactly once after reference collection and before the
   first timed round;
7. removing reference comparison produces mutation RED.

Integration gates:

```text
q01 direct four-driver:
  final rows = 4
  result_match = 1
  exact result_hash may differ

all 22 × 3 backends × 3 rounds:
  errors empty
  result_match = 1
  rows equal accepted final output cardinality
  real CBI/FileCache metrics present
```

Only after these gates pass is four-driver H2 timing interpreted.

## Alternatives rejected

### Cross-process result serialization

Rejected because it requires a new stable serialization format and an external
tolerance-aware comparator. The in-process path reuses Velox's tested result
comparison.

### Rounded floating-point hash

Rejected because quantization boundaries, nested types, NaN/infinity, and
query-specific scale create false matches or false mismatches. Velox already
has a richer epsilon comparator.

## Scope

This changes only benchmark correctness accounting and H2 orchestration. It
does not change TPCH plans, Velox execution semantics, FileCache behavior,
Gluten, or Spark.
