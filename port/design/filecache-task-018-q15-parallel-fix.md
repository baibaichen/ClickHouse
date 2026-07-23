# Task 018 TPCH Q15 Parallel Fix Design

## Status

```text
decision_date: 2026-07-23
decision: approved
stop_after: focused q15 verification
```

## Problem

The Velox q15 plan independently computes supplier revenue twice:

```text
left subtree:  group by supplier -> sum(revenue)
right subtree: group by supplier -> sum(revenue) -> max
join key:      total_revenue = max_revenue
```

With four drivers, the two floating-point sums use different partial
aggregation and merge orders. Values differ at tiny relative scale, but the
hash join requires exact double equality. The query then returns zero rows in
some runs instead of the expected supplier row.

## Design

Compute supplier revenue once:

```text
lineitem scan
filter ship date
project part_revenue
partial/final aggregation by supplier_no
topNRank dense_rank over total_revenue DESC with limit 1
join supplier table
final order by supplier key
```

`topNRank("dense_rank", ..., limit=1)` retains all rows whose dense rank is 1.
It is equivalent to:

```sql
total_revenue = (SELECT max(total_revenue) FROM revenue)
```

and preserves all suppliers tied for maximum revenue. It removes the duplicated
floating-point aggregation and exact join on independently computed doubles.
Unlike a generic window expression, `TopNRowNumberNode` implements rank
directly and needs no `velox_window` function registration.

## Files

```text
velox/exec/tests/utils/TpchQueryBuilder.cpp
  Rewrite getQ15Plan.
```

No FileCache, benchmark verifier, script, Gluten, or Spark file changes.

## Tests

Behavioral RED is preserved in:

```text
tmp/parallel_verified4_ab_results/tpch_direct.csv
```

where q15 has two `result_match=0` rows with zero output rows.

After the change:

1. build and run `ParquetTpchTest.Q15` against its DuckDB oracle;
2. build `velox_tpch_benchmark`;
3. run direct q15 with one driver and verify one result row;
4. run direct q15 with four drivers/reference one for at least three rounds;
5. run CBI and FileCache with the same four/reference-one settings;
6. require every timed row:
   - `rows=1`;
   - `result_match=1`;
   - empty error;
7. require FileCache miss/fill then warm-hit metrics;
8. run a mutation restoring the duplicate aggregation/exact join and confirm
   q15 failure, then restore the fix.

The Worker stops after focused q15 verification. It does not continue the full
four-driver H2 run.

## Non-goals

```text
changing floating-point comparison globally
adding epsilon joins
excluding q15
running full H2
changing q15 output schema or tie semantics
```
