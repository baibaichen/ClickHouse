# Plain `BufferedInput` Baseline Experiment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a benchmark-only plain `BufferedInput` backend and compare it with `DirectBufferedInput` and warm `FileCacheBufferedInput` on the same q01 workload.

**Architecture:** Extend only the `velox_ch_filecache_tpch_ab_benchmark` harness. A private builder in `AbBenchmarkMain.cpp` returns ordinary `dwio::common::BufferedInput` when `--input_source=buffered`; no production Hive source, setting, or default changes. The behavioral RED/GREEN gate is the real benchmark CLI because this harness has no unit-test target.

**Tech Stack:** C++20, Velox Hive `BufferedInputBuilder`, `dwio::common::BufferedInput`, CMake/Ninja, Bash, Python 3.

---

## Global Constraints

```text
ClickHouse repo: /home/chang/SourceCode/ClickHouse
ClickHouse branch: ch-filecache2
Velox repo: /home/chang/OpenSource/velox2
Velox branch: filecache2
Velox starting HEAD: fc37a7eb6
Build:
  /home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13
Binary:
  /home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13/
  velox/ch/benchmarks/velox_ch_filecache_tpch_ab_benchmark
Dataset:
  /home/chang/test/tpch-double/tpch-generated-100.0-parquet-decimal_as_double
Binding design:
  port/design/filecache-buffered-input-baseline-experiment.md
```

- Worker does not stage, commit, amend, rebase, push, or create a PR.
- Modify benchmark files only; do not modify production Hive/DWIO/FileCache files.
- Build exactly one target per invocation; never pass `-j` or `nproc`.
- Redirect every build and execution to unique logs under the RelWithDebInfo build.
- Run with:

```text
LD_LIBRARY_PATH=/home/chang/OpenSource/velox2/
  cmake-build-relwithdebinfo-gcc13/_deps/icu/lib
```

- The local dataset is same-workload IO evidence only, not canonical TPC-H.
- Instrumented results are not required in this task.
- The benchmark code remains uncommitted after the experiment unless the
  Controller separately authorizes it.

## Files

| File | Change |
|---|---|
| `/home/chang/OpenSource/velox2/velox/ch/benchmarks/AbBenchmarkBase.h` | Add `AbBackend::kBuffered` and update backend documentation |
| `/home/chang/OpenSource/velox2/velox/ch/benchmarks/AbBenchmarkBase.cpp` | Keep cache diagnostics zero for `kBuffered` |
| `/home/chang/OpenSource/velox2/velox/ch/benchmarks/AbBenchmarkMain.h` | Document `buffered` in the accepted input-source list |
| `/home/chang/OpenSource/velox2/velox/ch/benchmarks/AbBenchmarkMain.cpp` | Add private plain builder and dispatch branch |
| `/home/chang/SourceCode/ClickHouse/port/task/result/021-filecache-plain-buffered-baseline-result.md` | Record RED/GREEN and matrix evidence |

---

### Task 1: Capture the Behavioral RED

- [ ] **Step 1: Verify the source baseline**

```bash
git -C /home/chang/OpenSource/velox2 --no-pager status --short --branch
git -C /home/chang/OpenSource/velox2 --no-pager log -1 --oneline
git -C /home/chang/OpenSource/velox2 --no-pager diff --check
```

Expected: clean `filecache2` at `fc37a7eb6`.

- [ ] **Step 2: Run the pre-change `buffered` CLI**

```bash
set +e
BUILD=/home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13
BIN="$BUILD/velox/ch/benchmarks/velox_ch_filecache_tpch_ab_benchmark"
DATA=/home/chang/test/tpch-double/tpch-generated-100.0-parquet-decimal_as_double
OUT=/home/chang/OpenSource/velox2/tmp/plain_buffered_red
mkdir -p "$OUT"

GLOG_logtostderr=1 \
LD_LIBRARY_PATH="$BUILD/_deps/icu/lib" \
"$BIN" \
  --input_source=buffered \
  --data_path="$DATA" \
  --data_format=parquet \
  --query_id=1 \
  --rounds=2 \
  --num_repeats=1 \
  --num_splits_per_file=1 \
  --num_drivers=1 \
  --cache_gb=0 \
  --out="$OUT/buffered.csv" \
  > "$BUILD/plain_buffered_red.log" 2>&1
status=$?
printf 'exit=%s\n' "$status" > "$BUILD/plain_buffered_red.status"
test "$status" -ne 0
grep -F 'Unknown --input_source: buffered' "$BUILD/plain_buffered_red.log"
```

Expected: real behavior RED; the process rejects `buffered` before query
execution and no valid CSV is produced.

---

### Task 2: Implement the Benchmark-Only Backend

- [ ] **Step 1: Extend `AbBackend`**

In `AbBenchmarkBase.h`, use:

```cpp
enum class AbBackend
{
    kDirect,
    kBuffered,
    kCbi,
    kFileCache,
};
```

Update the class comment to describe `kBuffered` as plain
`dwio::common::BufferedInput` with no application cache.

- [ ] **Step 2: Keep diagnostics empty**

In both switches in `AbBenchmarkBase.cpp`, group `kBuffered` with `kDirect`:

```cpp
        case AbBackend::kBuffered:
        case AbBackend::kDirect:
            break;
```

Do not add fake hit or cache counters.

- [ ] **Step 3: Add a private plain builder**

Add these includes to `AbBenchmarkMain.cpp`:

```cpp
#include "velox/connectors/hive/BufferedInputBuilder.h"
#include "velox/dwio/common/BufferedInput.h"
```

Inside the existing anonymous namespace, add:

```cpp
class PlainBufferedInputBuilder final
    : public connector::hive::BufferedInputBuilder
{
public:
    std::unique_ptr<dwio::common::BufferedInput> create(
        const connector::hive::FileHandle & fileHandle,
        const dwio::common::ReaderOptions & readerOpts,
        const connector::ConnectorQueryCtx *,
        std::shared_ptr<io::IoStatistics> ioStatistics,
        std::shared_ptr<IoStats> ioStats,
        folly::Executor *,
        const folly::F14FastMap<std::string, std::string> & fileReadOps) override
    {
        return std::make_unique<dwio::common::BufferedInput>(
            fileHandle.file,
            readerOpts.memoryPool(),
            dwio::common::MetricsLog::voidLog(),
            ioStatistics.get(),
            ioStats.get(),
            dwio::common::BufferedInput::kMaxMergeDistance,
            std::nullopt,
            fileReadOps);
    }
};

void registerPlainBufferedInputBuilder()
{
    connector::hive::BufferedInputBuilder::registerBuilder(
        std::make_shared<PlainBufferedInputBuilder>());
}
```

The class stays private to the benchmark TU.

- [ ] **Step 4: Add dispatch**

Before the `cbi` branch in `dispatchAbMain`, add:

```cpp
    else if (FLAGS_input_source == "buffered")
    {
        FLAGS_cache_gb = 0;
        ab.setBackend(AbBackend::kBuffered);
    }
```

After `ab.initialize`, add:

```cpp
    if (FLAGS_input_source == "buffered")
    {
        registerPlainBufferedInputBuilder();
    }
```

Keep the existing non-FileCache reset callback; it is a no-op without CBI.

Update the unknown-source message:

```text
expected buffered, cbi, filecache, or direct
```

Update `AbBenchmarkMain.h` documentation accordingly.

- [ ] **Step 5: Build the benchmark**

```bash
/usr/bin/cmake --build \
  /home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13 \
  --target velox_ch_filecache_tpch_ab_benchmark \
  > /home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13/build_plain_buffered_green.log 2>&1
```

Expected: target links successfully.

---

### Task 3: Run the GREEN Smoke

- [ ] **Step 1: Run `direct`, `buffered`, and `filecache`**

```bash
set -euo pipefail
cd /home/chang/OpenSource/velox2

BUILD=/home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13
BIN="$BUILD/velox/ch/benchmarks/velox_ch_filecache_tpch_ab_benchmark"
DATA=/home/chang/test/tpch-double/tpch-generated-100.0-parquet-decimal_as_double
OUT=/home/chang/OpenSource/velox2/tmp/plain_buffered_smoke
LOG="$BUILD/plain_buffered_smoke"
CACHE=/home/chang/OpenSource/velox2/tmp/plain_buffered_smoke_cache
ICU="$BUILD/_deps/icu/lib"

mkdir -p "$OUT" "$LOG" /home/chang/OpenSource/velox2/tmp

run_mode()
{
  mode="$1"
  args=(
    --input_source="$mode"
    --data_path="$DATA"
    --data_format=parquet
    --query_id=1
    --rounds=2
    --num_repeats=1
    --num_splits_per_file=1
    --num_drivers=1
    --cache_gb=0
    --out="$OUT/$mode.csv"
  )
  if [ "$mode" = filecache ]
  then
    args+=(--filecache_root="$CACHE" --filecache_disk_gib=80)
  fi
  GLOG_logtostderr=1 LD_LIBRARY_PATH="$ICU" \
    "$BIN" "${args[@]}" > "$LOG/$mode.log" 2>&1
}

run_mode direct
run_mode buffered
run_mode filecache
```

- [ ] **Step 2: Validate**

```python
import csv
from pathlib import Path

root = Path("/home/chang/OpenSource/velox2/tmp/plain_buffered_smoke")
rows = {}
for backend in ("direct", "buffered", "filecache"):
    with (root / f"{backend}.csv").open() as source:
        rows[backend] = list(csv.DictReader(source))
    assert len(rows[backend]) == 2
    assert all(row["error"] == "" for row in rows[backend])
    assert all(row["rows"] == "4" for row in rows[backend])

assert rows["direct"][1]["bytes_read"] == rows["buffered"][1]["bytes_read"]
assert rows["filecache"][1]["hit_pct_diag"] == "100.0000"
assert rows["filecache"][1]["bytes_dl_mib_diag"] == "0.0000"
assert rows["filecache"][1]["evict_diag"] == "0.0000"
```

If plain `BufferedInput` reports a different `bytes_read`, preserve the artifact
and stop as `blocked`; do not silently relax the design gate.

- [ ] **Step 3: Confirm unknown input still fails**

```bash
set +e
BUILD=/home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13
GLOG_logtostderr=1 \
LD_LIBRARY_PATH="$BUILD/_deps/icu/lib" \
"$BUILD/velox/ch/benchmarks/velox_ch_filecache_tpch_ab_benchmark" \
  --input_source=bogus \
  --data_path=/home/chang/test/tpch-double/tpch-generated-100.0-parquet-decimal_as_double \
  --query_id=1 \
  --rounds=1 \
  --num_splits_per_file=1 \
  --num_drivers=1 \
  --out=/home/chang/OpenSource/velox2/tmp/plain_buffered_smoke/bogus.csv \
  > "$BUILD/plain_buffered_smoke/bogus.log" 2>&1
status=$?
test "$status" -ne 0
grep -F 'Unknown --input_source: bogus' \
  "$BUILD/plain_buffered_smoke/bogus.log"
```

---

### Task 4: Run the Order-Balanced Matrix

- [ ] **Step 1: Run five blocks**

```bash
set -euo pipefail
cd /home/chang/OpenSource/velox2

BUILD=/home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13
BIN="$BUILD/velox/ch/benchmarks/velox_ch_filecache_tpch_ab_benchmark"
DATA=/home/chang/test/tpch-double/tpch-generated-100.0-parquet-decimal_as_double
OUT=/home/chang/OpenSource/velox2/tmp/plain_buffered_matrix
LOG="$BUILD/plain_buffered_matrix"
CACHE=/home/chang/OpenSource/velox2/tmp/plain_buffered_matrix_cache
ICU="$BUILD/_deps/icu/lib"

mkdir -p "$OUT" "$LOG" /home/chang/OpenSource/velox2/tmp

run_mode()
{
  sample="$1"
  mode="$2"
  args=(
    --input_source="$mode"
    --data_path="$DATA"
    --data_format=parquet
    --query_id=1
    --rounds=2
    --num_repeats=1
    --num_splits_per_file=1
    --num_drivers=1
    --cache_gb=0
    --out="$OUT/${sample}_${mode}.csv"
  )
  if [ "$mode" = filecache ]
  then
    args+=(--filecache_root="$CACHE" --filecache_disk_gib=80)
  fi
  GLOG_logtostderr=1 LD_LIBRARY_PATH="$ICU" \
    /usr/bin/time -v -o "$LOG/${sample}_${mode}.time" \
    "$BIN" "${args[@]}" > "$LOG/${sample}_${mode}.log" 2>&1
}

for sample in b1s1 b1s2 b1s3
do
  run_mode "$sample" direct
  run_mode "$sample" buffered
  run_mode "$sample" filecache
done

for sample in b2s1 b2s2
do
  run_mode "$sample" filecache
  run_mode "$sample" buffered
  run_mode "$sample" direct
done
```

- [ ] **Step 2: Summarize**

```bash
python3 - <<'PY' \
  > /home/chang/OpenSource/velox2/cmake-build-relwithdebinfo-gcc13/plain_buffered_matrix/summary.txt
import csv
import statistics
from pathlib import Path

root = Path("/home/chang/OpenSource/velox2/tmp/plain_buffered_matrix")
samples = ("b1s1", "b1s2", "b1s3", "b2s1", "b2s2")
values = {backend: [] for backend in ("direct", "buffered", "filecache")}
bytes_read = {backend: set() for backend in values}

for sample in samples:
    for backend in values:
        path = root / f"{sample}_{backend}.csv"
        with path.open() as source:
            rows = list(csv.DictReader(source))
        assert len(rows) == 2, (path, len(rows))
        assert all(row["error"] == "" for row in rows), path
        assert all(row["rows"] == "4" for row in rows), path
        warm = rows[1]
        values[backend].append(float(warm["wall_ms"]))
        bytes_read[backend].add(int(warm["bytes_read"]))
        if backend == "filecache":
            assert warm["hit_pct_diag"] == "100.0000", (path, warm)
            assert warm["bytes_dl_mib_diag"] == "0.0000", (path, warm)
            assert warm["evict_diag"] == "0.0000", (path, warm)

assert bytes_read["direct"] == bytes_read["buffered"], bytes_read

for backend, times in values.items():
    print(
        backend,
        "raw=", ",".join(f"{value:.3f}" for value in times),
        "median=", f"{statistics.median(times):.3f}",
        "range=", f"{min(times):.3f}-{max(times):.3f}",
        "bytes_read=", sorted(bytes_read[backend]))

for backend in ("buffered", "filecache"):
    ratios = [
        value / direct
        for value, direct in zip(values[backend], values["direct"])
    ]
    print(
        f"{backend}/direct",
        "raw=", ",".join(f"{ratio:.4f}" for ratio in ratios),
        "median=", f"{statistics.median(ratios):.4f}",
        "block1=", f"{statistics.median(ratios[:3]):.4f}",
        "block2=", f"{statistics.median(ratios[3:]):.4f}")
PY
```

Interpret using the binding design:

```text
Buffered ~= Direct, FileCache slower:
  FileCache adapter/state/local-cache path dominates.

Buffered ~= FileCache, both slower than Direct:
  Direct-specific planning/stream behavior dominates.

Direct < Buffered < FileCache:
  both layers contribute.
```

Do not use instrumented timing or canonical TPC-H claims.

---

### Task 5: Review and Receipt

- [ ] **Step 1: Verify the diff**

```bash
git -C /home/chang/OpenSource/velox2 --no-pager diff --check
git -C /home/chang/OpenSource/velox2 --no-pager status --short --branch
git -C /home/chang/OpenSource/velox2 --no-pager diff -- \
  velox/ch/benchmarks/AbBenchmarkBase.h \
  velox/ch/benchmarks/AbBenchmarkBase.cpp \
  velox/ch/benchmarks/AbBenchmarkMain.h \
  velox/ch/benchmarks/AbBenchmarkMain.cpp
```

- [ ] **Step 2: Run one independent read-only review**

Review correctness, builder lifetime, production isolation, backend selection,
false-green risk, and measurement validity.

- [ ] **Step 3: Write the receipt**

Create:

```text
/home/chang/SourceCode/ClickHouse/
port/task/result/021-filecache-plain-buffered-baseline-result.md
```

Include:

```text
worker_status
repository baseline and dirty files
RED and GREEN evidence
binary build ID
dataset limitation
all raw samples
validity
per-backend medians and paired ratios
both order-block medians
interpretation
review findings
recommended next experiment
```

Do not commit the Velox benchmark diff. Stop for Controller review.
