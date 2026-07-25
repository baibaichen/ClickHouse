# FileCache TPCH `BufferedInput` Performance Investigation

## Status

```text
decision_date: 2026-07-25
decision: approved for written-spec review
implementation_authorized: false
execution_authorized: false
task_017b_status: authorized_but_paused
```

## Problem

The accepted four-driver SF100 TPCH result reports:

| Comparison | Warm runtime ratio |
|---|---:|
| FileCache / Direct | 1.062 |
| FileCache / CBI | 1.116 |

The investigation uses Direct as the primary baseline. CBI is not the primary
baseline because its 4 GiB cache continues to evict heavily during the measured
rounds, so the CBI comparison mixes a different cache policy and capacity into
the result.

The current FileCache/Direct comparison also changes more than the storage
backend:

- Direct uses `DirectBufferedInput`.
- FileCache uses `FileCacheBufferedInput`.
- `DirectBufferedInput::load` sorts requests, groups adjacent regions,
  coalesces reads, and schedules eligible prefetches.
- `FileCacheBufferedInput::load` clears its request list and performs no
  planning.
- `FileCacheInputStream` defaults to a 1 MiB output buffer and, on a warm cache
  read, refreshes read state and seeks the cache reader for each chunk.

Therefore, the accepted 6.2% difference cannot yet be attributed to the
FileCache core. It includes the cost of replacing the whole `BufferedInput`
implementation and losing Direct's read planning.

## Existing Evidence

The accepted CSVs are:

```text
/root/oss/velox/tmp/parallel_verified4_q15fixed_results/tpch_direct.csv
/root/oss/velox/tmp/parallel_verified4_q15fixed_results/tpch_cbi.csv
/root/oss/velox/tmp/parallel_verified4_q15fixed_results/tpch_filecache.csv
```

They prove:

- 198/198 backend/query/round cells match the one-driver reference.
- Every measured FileCache warm read is a 100% cache hit.
- FileCache is slower than Direct on 21/22 queries.
- q21, q09, q17, and q04 contribute approximately 54% of the absolute
  FileCache-minus-Direct warm time.
- The aggregate FileCache/Direct difference grows from 2.5% at one driver to
  6.2% at four drivers.
- q17 and q21 are not slower at one driver but become material regressions at
  four drivers, so the design must distinguish fixed read-path cost from
  concurrency amplification.

This evidence is local-only. It has two warm samples per query on an unisolated
WSL2 host and uses a fixed Direct-to-CBI-to-FileCache execution order. It is
sufficient to choose focused probes, not to establish an exact regression
magnitude.

## Goal

Determine how much of the FileCache/Direct difference comes from:

1. replacing `DirectBufferedInput` with `FileCacheBufferedInput`; and
2. the FileCache state machine and local cache-file read path.

Then identify a concrete mechanism inside the dominant layer and prove
causality with a one-variable mutation. A flamegraph-like hotspot, correlation,
or timing counter alone is not a root-cause conclusion.

There is no fixed final overhead target in this investigation. The output is a
causal decomposition, a validated mechanism, and a production-fix direction.

## Experiment Matrix

Use two driver settings and three input cells:

| Driver setting | A: Direct | B: FCBI passthrough | C: warm FileCache |
|---|---|---|---|
| 1 driver | existing `DirectBufferedInput` + source file | `FileCacheBufferedInput` API/stream behavior + source file, without FileCache state | existing warm FileCache path |
| 4 drivers | same | same | same |

The measured differences mean:

```text
B - A = BufferedInput replacement cost
C - B = FileCache state/local-cache cost
```

Run the complete one-driver matrix first. Run the four-driver matrix only after
the one-driver results pass the validity gates.

### Cell B contract

Cell B is a benchmark-only passthrough. It must preserve the parts of
`FileCacheBufferedInput` under investigation:

- the same `enqueue`/`load` contract;
- the same stream shape and `Next` behavior;
- the same output-buffer/chunk policy;
- the same query, memory-pool, and cancellation ownership.

It must bypass:

- `FileCache::get` and `FileCache::getOrSet`;
- `FileSegment` state and downloader ownership;
- cache-file open/read;
- priority and eviction structures;
- FileCache hit/miss/statistics updates.

Cell B reads the original source `ReadFile`. It must not silently fall back from
cell C, and it must not be exposed as a production mode. The implementation
plan must choose a benchmark-only injection boundary and remove it or keep it
test-only after the investigation.

## Focused Queries

Use the same focused set in both driver phases:

| Query | Reason |
|---|---|
| q09 | Material positive FileCache difference at one and four drivers |
| q20 | Material positive difference at one and four drivers |
| q17 | Faster/neutral at one driver, slower at four; concurrency discriminator |
| q21 | Faster/neutral at one driver, largest four-driver absolute difference |
| q04 | Long-running query with low one-driver overhead; control for general host drift |

Do not begin with all 22 queries. Full TPCH is the final confirmation after a
focused mutation closes the causal loop.

## Measurement Protocol

Keep these settings fixed:

```text
dataset: /root/oss/test-data/tpch-sf100-parquet-double
num_splits_per_file: 1
reference_num_drivers: 1
query_mem_gb: 32
filecache_disk_gib: 80
build_type: RelWithDebInfo
```

For each driver setting and focused query:

1. Run cells in `A -> B -> C` order and collect three warm samples per cell.
2. Run cells in `C -> B -> A` order and collect two additional warm samples per
   cell.
3. Pool five warm samples per cell, but retain the order-block identity.
4. Report every raw sample, each order-block median, the pooled median, and the
   full range.

Round 1 prepares cell C and is never a warm performance sample. Cell A and cell
B also discard round 1 so the sample positions stay comparable.

Do not run cells concurrently. Do not mix binaries, build types, data paths,
driver counts, or query IDs in one comparison.

## First-Wave Instrumentation

The Docker environment does not provide `perf`. The first wave uses
low-perturbation query-level counters only:

- wall time;
- process user CPU and system CPU;
- voluntary and involuntary context switches;
- logical input bytes;
- `enqueue` request count and requested bytes;
- `Next` call count and returned bytes;
- source/cache `pread` count and bytes;
- seek count;
- average and maximum returned chunk size;
- existing FileCache hit/read/predownload/eviction deltas for cell C.

Do not log per chunk. Collect counters locally and emit one delta per query.
Do not add fine-grained lock timers in the first wave.

## Validity Gates

A sample set is invalid if any of these occurs:

- result mismatch or nonempty query error;
- cell C warm hit rate is not exactly 100%;
- cell C performs warm predownload or eviction;
- the binary, dataset, build type, driver count, or query differs between cells;
- `A -> B -> C` and `C -> B -> A` produce contradictory difference directions;
- instrumentation changes the uninstrumented A/C direction on the focused set.

An invalid set is reported and rerun. It is never averaged into a conclusion.

## Decision Tree

### B is close to C

The `BufferedInput` replacement dominates. Split that layer with two
one-variable probes:

1. disable Direct's `load` planning for the benchmark, while retaining the
   Direct stream, to measure lost sort/coalesce/prefetch behavior;
2. vary only FCBI stream mechanics such as chunk size or per-chunk
   refresh/seek, one at a time.

### B is close to A

The replacement itself is not the dominant cost. Add second-wave scoped timers
around:

- FileCache lookup and `getOrSet`;
- segment-state preparation/wait;
- cache-reader open/reuse/seek;
- priority update;
- cache-file `next`;
- existing statistics accounting.

### B lies materially between A and C

Both layers contribute. Quantify both differences and investigate the larger
one first. Do not combine mutations from both layers.

### Driver interaction

Compare the one-driver and four-driver matrices:

- growth in `B - A` points to concurrency amplification in the FCBI planning or
  stream layer;
- growth in `C - B` points to shared FileCache state, locks, priority, or
  statistics;
- growth in both requires separate mutations and separate attribution.

## Causal Acceptance

A root cause is accepted only when:

1. the A/B/C decomposition is directionally consistent across both execution
   orders;
2. counters or scoped timers identify a concrete operation matching the
   affected layer;
3. changing only that operation reduces the corresponding difference in the
   affected focused queries;
4. correctness, warm-hit, and no-eviction gates remain green;
5. q04 does not show an unrelated change of comparable magnitude.

No fixed percentage such as 50%, 70%, or a final 3% overhead is used as the
definition of root cause.

After focused causal acceptance, rerun all 22 queries with the accepted
production candidate. Report both one-driver and four-driver results and keep
the existing accepted Task-018 CSVs as the baseline.

## Task Ordering

Task 017B is authorized but paused. It replaces no-op logging with real glog and
would change the hot-path environment, so it must not run before this
investigation and any accepted performance corrective.

The order is:

```text
Task 018R [accepted]
-> TPCH BufferedInput performance investigation
-> accepted performance corrective, if required
-> Task 017B
-> Task 019
```

Task 019 remains blocked on Task 017B acceptance.

## Non-Goals

- rerunning the full 22-query suite before focused attribution;
- using CBI as the primary performance baseline;
- treating local-only exact percentages as CI-grade performance claims;
- changing SQL plans or TPCH correctness semantics;
- implementing Task 017B during the investigation;
- adding production fallback paths;
- keeping benchmark-only passthrough behavior as a user-facing feature;
- using a hotspot or correlation without a mutation as the final conclusion.
