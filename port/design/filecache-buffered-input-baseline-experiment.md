# FileCache Plain `BufferedInput` Baseline Experiment

## Status

```text
decision_date: 2026-07-27
design_status: approved
scope: benchmark-only
production_behavior_change: false
```

## Question

The q01 local workload shows a persistent warm `FileCacheBufferedInput`
slowdown relative to `DirectBufferedInput`:

```text
5785a43a4, before FileCacheCoalescedLoad:  FileCache / Direct = 1.0701
fc37a7eb6, with FileCacheCoalescedLoad:    FileCache / Direct = 1.0846
```

Both results are order-balanced local evidence. Warm `FileCache` reads are
100% hits with no download or eviction. The default malloc allocator used by
this implementation does not reproduce the earlier Mmap page-fault mechanism.

This experiment adds plain `dwio::common::BufferedInput` as a fourth benchmark
backend to determine whether the remaining difference comes primarily from:

1. `FileCacheBufferedInput` / `FileCacheInputStream` state, metadata, and local
   cache-file handling; or
2. `DirectBufferedInput`'s more specialized request planning and stream model.

## Backend Matrix

The benchmark accepts:

```text
--input_source=direct
--input_source=buffered
--input_source=cbi
--input_source=filecache
```

The new `buffered` mode:

- installs no `AsyncDataCache`;
- creates no `FileCacheManager`;
- registers a benchmark-only `BufferedInputBuilder`;
- returns an ordinary `dwio::common::BufferedInput`;
- passes through the same `ReadFile`, `MemoryPool`, `MetricsLog`,
  `IoStatistics`, `IoStats`, and `fileReadOps` used by the other Hive paths;
- uses `BufferedInput::kMaxMergeDistance` and default `wsVRLoad` selection;
- changes no production Hive selection, setting, or default.

## Implementation Boundary

### `velox/ch/benchmarks/AbBenchmarkBase.h`

Add `AbBackend::kBuffered`. Its cache diagnostic columns remain zero, like
`kDirect`.

### `velox/ch/benchmarks/AbBenchmarkBase.cpp`

Handle `kBuffered` in `snapshotBackend` and `populateBackendDelta` as a
no-application-cache backend.

### `velox/ch/benchmarks/AbBenchmarkMain.cpp`

Add a benchmark-private `PlainBufferedInputBuilder` implementing
`connector::hive::BufferedInputBuilder`.

Its `create` returns:

```cpp
std::make_unique<dwio::common::BufferedInput>(
    fileHandle.file,
    readerOpts.memoryPool(),
    dwio::common::MetricsLog::voidLog(),
    ioStatistics.get(),
    ioStats.get(),
    dwio::common::BufferedInput::kMaxMergeDistance,
    std::nullopt,
    fileReadOps);
```

For `--input_source=buffered`, `dispatchAbMain`:

1. forces `FLAGS_cache_gb=0`;
2. selects `AbBackend::kBuffered`;
3. calls the ordinary benchmark initialization;
4. registers `PlainBufferedInputBuilder`;
5. uses the same no-op cache reset callback as `direct`.

The builder is process-global because `BufferedInputBuilder` itself is
process-global. The benchmark is one backend per process, so no restoration API
is needed inside a process. No symbol is added to a production header.

### Tests

Extend the benchmark helper tests to prove:

- `buffered` is accepted as an input source;
- its backend diagnostics stay zero;
- `direct`, `buffered`, and `filecache` remain distinct enum values;
- unknown input sources still fail.

Run one real q01 smoke and require `rows`, `bytes_read`, and errors to match the
Direct baseline. This benchmark schema does not contain result hashes, so the
experiment cannot claim canonical query-result equivalence beyond the existing
row/error gate.

## Measurement Protocol

Use:

```text
branch: filecache2
head: fc37a7eb6
binary:
  /home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13/
  velox/ch/benchmarks/velox_ch_filecache_tpch_ab_benchmark
dataset:
  /home/chang/test/tpch-double/
  tpch-generated-100.0-parquet-decimal_as_double
query: q01
rounds: 2
num_drivers: 1
num_splits_per_file: 1
cache_gb: 0 for direct/buffered/filecache
filecache_disk_gib: 80
```

Each process runs one cold round and one warm round. Retain round 2.

Run five order-balanced blocks:

```text
block 1, three samples:
  Direct -> Buffered -> FileCache

block 2, two samples:
  FileCache -> Buffered -> Direct
```

For each backend record:

- raw warm wall time;
- median and range;
- paired ratio to Direct;
- `rows`, `bytes_read`, and `error`;
- FileCache warm hit/download/eviction diagnostics.

Instrumentation, if needed, runs separately. Instrumented wall time is not
mixed with the primary timing samples.

## Interpretation

```text
Buffered ~= Direct, FileCache slower:
  FileCache adapter/state/local-cache path dominates.

Buffered ~= FileCache, both slower than Direct:
  DirectBufferedInput-specific planning/stream behavior dominates.

Direct < Buffered < FileCache:
  Direct planning and FileCache handling both contribute.

Buffered faster than Direct:
  investigate DBI planning/prefetch overhead before drawing a FileCache result.
```

No fixed percentage defines `~=`. Compare both order blocks, ranges, and paired
ratios. A direction that reverses by execution order is inconclusive.

## Non-Goals

- changing the production Hive builder;
- replacing FileCache with plain `BufferedInput`;
- changing cache policy, segment size, or buffer size;
- using the local Spark-generated dataset as canonical TPC-H evidence;
- combining this experiment with a FileCache fix;
- committing the benchmark-only backend after the experiment unless separately
  reviewed and explicitly authorized.
