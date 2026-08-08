# Task 021 Result: Plain `BufferedInput` Baseline Experiment

## Worker result

```text
worker_status: ready_for_controller
environment_profile: home-chang
task: 021
```

## Repository baseline

```text
ClickHouse:
  repo: /home/chang/SourceCode/ClickHouse
  branch: ch-filecache2
  design commit: a8670ce7637

Velox:
  repo: /home/chang/OpenSource/velox2
  branch: filecache2
  head: fc37a7eb65a02b519fb0455afa076cb54c928c8e
  changes: four unstaged benchmark files
  staged: none
  commits: none
```

## Scope

The benchmark gained a fourth backend:

```text
--input_source=buffered
```

It registers a benchmark-private `BufferedInputBuilder` which constructs
ordinary `dwio::common::BufferedInput`. It installs no `AsyncDataCache`, creates
no `FileCacheManager`, and changes no production Hive/DWIO/FileCache file.

Changed Velox files:

```text
velox/ch/benchmarks/AbBenchmarkBase.h
velox/ch/benchmarks/AbBenchmarkBase.cpp
velox/ch/benchmarks/AbBenchmarkMain.h
velox/ch/benchmarks/AbBenchmarkMain.cpp
```

## RED and GREEN evidence

Before the change:

```text
--input_source=buffered
exit: 134
reason: Unknown --input_source: buffered
```

After the change:

```text
target: velox_ch_filecache_tpch_ab_benchmark
build: success
buffered q01: exit 0
rows: 4
error: empty
bytes_read: 5688732885
unknown backend: still rejected
```

Logs:

```text
/home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13/
  plain_buffered_red.log
  build_plain_buffered_green.log
  plain_buffered_green.log
  plain_buffered_bogus.log
```

The implementation passed independent specification and code-quality review.

## Hardened smoke

Artifacts:

```text
CSV:
  /home/chang/OpenSource/velox2/tmp/plain_buffered_smoke_v2/
logs and manifest:
  /home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13/
  plain_buffered_smoke_v2/
```

Every backend returned four rows with empty errors. `direct` and `buffered`
reported identical bytes. Warm FileCache diagnostics were 100% hit, zero
download, and zero reported eviction.

## Initial matrix — superseded

The first five-slot matrix always placed `buffered` second. Its
`buffered/direct` direction reversed between order blocks:

```text
block 1: 1.0450
block 2: 0.9653
```

It is retained as exploratory data but is not used for the verdict.

## Refined Latin-square matrix

The refined experiment used:

```text
sequence 1: Direct -> Buffered -> FileCache
sequence 2: Buffered -> FileCache -> Direct
sequence 3: FileCache -> Direct -> Buffered
rounds per process: 6
warm rounds retained: 2-6
query: q01
num_drivers: 1
num_splits_per_file: 1
cache_gb: 0
filecache_disk_gib: 80
```

Artifacts:

```text
CSV:
  /home/chang/OpenSource/velox2/tmp/plain_buffered_latin_matrix/
logs:
  /home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13/
  plain_buffered_latin_matrix/
```

All nine processes exited successfully. All 54 rows returned four rows with
empty errors. `direct` and `buffered` reported identical `bytes_read`.
All 15 FileCache warm rows reported 100% hit and zero download.

### All retained warm rounds (2-6)

| Backend | Median ms | Min ms | Max ms |
|---|---:|---:|---:|
| Direct | 16124.0 | 15412.2 | 18032.3 |
| Buffered | 15698.4 | 15109.7 | 16897.3 |
| FileCache | 16774.5 | 15830.3 | 18058.5 |

Sequence process-median ratios:

```text
Buffered / Direct:
  1.0232, 0.9863, 0.9505
  median: 0.9863

FileCache / Direct:
  1.0544, 1.0020, 1.0478
  median: 1.0478
```

The FileCache processes were still warming down after round 2. Each process
wrote approximately 5.68 GB while filling the cache in round 1, and the kernel
continued writeback during early warm rounds. FileCache warm slopes were
negative in all three processes.

### Settled-window sensitivity (rounds 4-6)

| Backend | Pooled median ms |
|---|---:|
| Direct | 16125.8 |
| Buffered | 15698.4 |
| FileCache | 16370.1 |

Sequence ratios over per-process round-4-to-6 medians:

```text
Buffered / Direct:
  1.0293, 0.9576, 0.9725
  median: 0.9725

FileCache / Direct:
  1.0020, 0.9966, 1.0597
  median: 1.0020
```

## Verdict

```text
verdict: local-only evidence
plain_buffered_vs_direct:
  no material slowdown detected
filecache_vs_direct:
  no stable material slowdown after excluding early writeback-contaminated rounds
```

Plain `BufferedInput` is not materially slower than `DirectBufferedInput` in
this CPU-bound, OS-page-cache-resident q01 workload. Its point estimate is
slightly faster, but the three sequence ratios span both sides of 1.0, so no
precise speedup claim is made.

The apparent FileCache +4-8% early-warm penalty decays as the round-1 cache-fill
writeback settles. In rounds 4-6, the median paired sequence ratio is 1.002.
Therefore this experiment does not support the claim that
`DirectBufferedInput` planning explains a persistent warm FileCache regression.

FileCache reports 6,137,330,624 raw bytes versus 5,688,732,885 for
Direct/Buffered (+7.89%). The current FileCache code records physical,
pre-trim cache bytes, while the earlier implementation recorded delivered
bytes. This is an implementation/accounting difference and must be reconciled
before a per-byte comparison.

## Limitations

- local Spark-generated dataset with non-standard physical column order;
- q01 only;
- one driver and one split per file;
- almost all data served from the OS page cache;
- FileCache fills and writes approximately 5.68 GB in every process before warm
  measurement;
- benchmark CSV has no result hash/reference comparison;
- reported FileCache eviction diagnostic is not wired to a real byte counter.

## Review

```text
implementation_spec_review: passed
implementation_quality_review: passed
smoke_spec_review: passed after hardened evidence rerun
measurement_review:
  initial matrix: inconclusive
  Latin square: valid, with early-writeback caveat
final_code_review:
  code correct
  benchmark-only diff should not be committed after the experiment unless
  separately authorized
```

## Recommended next step

Do not modify production FileCache or Direct planning based on this q01 result.
For a meaningful storage-path comparison, either:

1. pre-populate FileCache without wiping it at process start, then measure in a
   fresh process; or
2. add an explicit settle barrier after cache fill and measure only after
   writeback completes.

Use a workload that performs real physical input and records a result
hash/reference comparison.

## Worker declaration

```text
Velox changes are unstaged and uncommitted.
ClickHouse plan and receipt are uncommitted.
No source changes outside the four benchmark files were made.
```
