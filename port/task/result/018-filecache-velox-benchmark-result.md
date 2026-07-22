# Task 018 Result: FileCache Velox Benchmark — Non-TPCH Checkpoint (018-H1)

## Worker attempt 1 (Task 018-H1)

```text
worker_status: waiting_for_pre_tpch_approval
environment_profile: root-oss
task: 018-H1
tpch_sources_copied: false
tpch_target_built: false
tpch_commands_run: false
```

This receipt covers **only** the non-TPCH performance waves (018-H1). Task 018
is **not** accepted. The Controller must review this checkpoint and the user
must explicitly approve TPCH work before any Task-018-C/018-H2 Worker is
dispatched. No TPCH source was copied, no TPCH target was built, and no TPCH
command was run.

## Accepted prerequisites (as dispatched to this Worker)

| Task | Commit | Description |
|---|---|---|
| 018-A | `9850a70fa` | `FileCache` correctness harness |
| 018-B | `df9091e78` | `FileCacheBufferedInput` microbenchmark |
| 018-D | `5ae39651b` | Safe benchmark orchestration (`run_wrapper_ab.sh`) |

## Repository baseline

| Repository | Branch | State at Worker start |
|---|---|---|
| `/root/oss/velox` | `filecache` | clean at `5ae39651b8772116c33fd2f0fdbd388f55f5fd15` (ahead 19 of `baibaichen/filecache`) |
| `/root/oss/clickhouse` | `ch-filecache` | clean at `46e8675762e` (this Worker only added the current receipt file; no staging/commit performed) |

`git log --oneline -5` at `/root/oss/velox` before any Worker action:

```text
5ae39651b Task 018: Add safe benchmark orchestration
df9091e78 Task 018: Add `FileCacheBufferedInput` microbenchmark
9850a70fa Task 018: Add `FileCache` correctness harness
a856d836c Task 017A: Document caller-id format
4bfe9709a Task 017A: Update caller-id acceptance coverage
```

Velox HEAD was unchanged after all configure/build/benchmark steps
(`git rev-parse HEAD` == `5ae39651b8772116c33fd2f0fdbd388f55f5fd15`,
`git status --short` clean, `git diff --stat` empty). No Velox source file was
modified. The Gluten checkout at `/root/oss/gluten` was not touched by this
Worker (pre-existing local modifications there were left untouched, per the
task's "do not modify Gluten checkouts" constraint).

## Configure (RelWithDebInfo, non-TPCH)

```bash
cmake -S /root/oss/velox -B /root/oss/velox/_build/relwithdebinfo -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_ENABLE_PARQUET=OFF \
  -DVELOX_BUILD_TESTING=ON \
  -DVELOX_MONO_LIBRARY=ON \
  -DVELOX_GFLAGS_TYPE=static \
  > /root/oss/velox/_build/relwithdebinfo/configure_018.log 2>&1
```

- Exit code: `0`
- Log: `/root/oss/velox/_build/relwithdebinfo/configure_018.log`
- `CMakeCache.txt` confirms `CMAKE_BUILD_TYPE:STRING=RelWithDebInfo`.
- Log tail confirms vcpkg-provided Boost/wangle/folly/fizz/mvfst/FBThrift found,
  `Setting GTest source to AUTO` / `[GTest] Using SYSTEM GTest`, and
  `Configuring done` / `Generating done` / `Build files have been written to:
  /root/oss/velox/_build/relwithdebinfo` with no error lines.

## Step 1: Build non-TPCH benchmark targets

```bash
ninja -C /root/oss/velox/_build/relwithdebinfo \
  velox_ch_filecache_seek_benchmark \
  velox_ch_fcbi_benchmark \
  velox_bufferedinput_wrapper_benchmark \
  > /root/oss/velox/_build/relwithdebinfo/build_018h1.log 2>&1
```

- Exit code: `0`
- Log: `/root/oss/velox/_build/relwithdebinfo/build_018h1.log` (7 lines total)
- Log body:
  ```text
  ninja: Entering directory `/root/oss/velox/_build/relwithdebinfo'
  [0/2] Re-checking globbed directories...
  [1/6] Building CXX object velox/buffer/CMakeFiles/velox.dir/__/ch/Common/StatusFile.cpp.o
  [2/6] Linking CXX static library lib/libvelox.a
  [3/6] Linking CXX executable velox/ch/benchmarks/velox_ch_filecache_seek_benchmark
  [4/6] Linking CXX executable velox/ch/benchmarks/velox_ch_fcbi_benchmark
  [5/6] Linking CXX executable velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark
  ```
- No `-j` flag was passed; ninja used its automatic parallelism.
- `grep -iE "error|failed"` on the log (excluding "0 errors") returned nothing.

### RelWithDebInfo proof — every binary is under `_build/relwithdebinfo`, none Debug

All three binaries were freshly linked in this build (`Jul 22 15:56`, matching
this session), confirmed by `file(1)`:

| Binary | Path | `file` output |
|---|---|---|
| `velox_ch_filecache_seek_benchmark` | `/root/oss/velox/_build/relwithdebinfo/velox/ch/benchmarks/velox_ch_filecache_seek_benchmark` | `ELF 64-bit LSB pie executable, x86-64, ..., with debug_info, not stripped` |
| `velox_ch_fcbi_benchmark` | `/root/oss/velox/_build/relwithdebinfo/velox/ch/benchmarks/velox_ch_fcbi_benchmark` | `ELF 64-bit LSB pie executable, x86-64, ..., with debug_info, not stripped` |
| `velox_bufferedinput_wrapper_benchmark` | `/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark` | `ELF 64-bit LSB pie executable, x86-64, ..., with debug_info, not stripped` |

`CMakeCache.txt` for this build directory records
`CMAKE_BUILD_TYPE:STRING=RelWithDebInfo`. No Debug binary was invoked at any
point in this task — the `_build/debug*` directories were never referenced.

## Step 2: Wave 1 — core seek microbenchmark (`velox_ch_filecache_seek_benchmark`)

```bash
mkdir -p tmp/fc_w1
/root/oss/velox/_build/relwithdebinfo/velox/ch/benchmarks/velox_ch_filecache_seek_benchmark \
  --bm_min_iters=5 --file_size_mb=64 --cache_dir=tmp/fc_w1 --cache_size_mb=128 \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w1.log 2>&1
```

- Exit code: `0`
- Log: `/root/oss/velox/_build/relwithdebinfo/test_018h_w1.log`
- Folly benchmark table (baseline, no threshold applied):

```text
============================================================================
[...]benchmarks/FileCacheSeekBenchmark.cpp     relative  time/iter   iters/s
============================================================================
FileCacheSeekCacheHit                                      44.13ms     22.66
FileCacheSeekCacheMiss                                    835.04ms      1.20
FileCacheSeekBypass                                         4.38ms    228.06
```

- `tmp/fc_w1` was inspected before use (did not exist prior to this run;
  created fresh by this Worker). After the run it holds the `FileCache`'s own
  region/segment files (~55 MB across ~6000 files) — expected on-disk cache
  state for a 64 MB file / 128 MB cache, left in place as run evidence; no
  cleanup was needed given 661 GB free on `/root/oss`.

## Step 3: Wave 2 — dedicated FCBI micro (`velox_ch_fcbi_benchmark`)

```bash
mkdir -p tmp/fc_w2
/root/oss/velox/_build/relwithdebinfo/velox/ch/benchmarks/velox_ch_fcbi_benchmark \
  --bm_min_iters=10 --file_size_mb=128 --cache_dir=tmp/fc_w2 --cache_size_mb=256 \
  --region_size_kib=1024 --regions_per_iter=16 \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w2.log 2>&1
```

- Exit code: `0`
- Log: `/root/oss/velox/_build/relwithdebinfo/test_018h_w2.log`
- Folly benchmark table (baseline, no threshold applied):

```text
============================================================================
[...]s/FileCacheBufferedInputBenchmark.cpp     relative  time/iter   iters/s
============================================================================
FCBI_SequentialHot                                          8.33ms    120.11
FCBI_RandomHot                                              8.34ms    119.96
FCBI_SequentialCold                                        13.36ms     74.83
```

- `tmp/fc_w2` was inspected before use (did not exist prior; created fresh).
  After the run it holds ~257 MB of `FileCache` region data across 256 files
  — expected for a 128 MB file / 256 MB cache across 16 regions/iter; left in
  place as run evidence.

## Step 4: Wave 3 — wrapper A/B, all read paths (cbi/fcbi/dbi)

```bash
mkdir -p tmp/fc_w3
BIN=/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
CACHE_ROOT="$(pwd)/tmp/fc_w3_cache" \
OUT="$(pwd)/tmp/fc_w3/wrapper_all.md" \
RAM_CACHE_GB=4 SSD_CACHE_GB=10 FILECACHE_DISK_GB=10 \
TARGET_WS_GB=8 REMOTE_GB=9 \
READ_SIZES_KIB=1024,8192 WORKLOADS=sequential,zipfian \
MEASURE_PASSES=3 \
  bash velox/benchmarks/scripts/run_wrapper_ab.sh \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w3.log 2>&1
```

- Exit code: `0`
- Log: `/root/oss/velox/_build/relwithdebinfo/test_018h_w3.log` (739 lines)
- Log tail confirms the script's own self-validation succeeded:
  ```text
  I ... BufferedInputWrapperBenchmark.cpp:795]   cbi wall=165.743918ms ssd_MB=1224 src_MB=0 | fcbi wall=2524.810621ms ssd_MB=7352 src_MB=840 | dbi wall=5513.114780ms src_MB=5656
  W ... BufferedInputWrapperBenchmark.cpp:800]   non-zero source bytes: target not fully cache-resident; increase ssd/disk size or lower target_ws_gb
  I ... BufferedInputWrapperBenchmark.cpp:819] Wrote 4 cells to /root/oss/velox/tmp/fc_w3/wrapper_all.md
  Wrapper A/B complete; validated /root/oss/velox/tmp/fc_w3/wrapper_all.md (cbi/fcbi/dbi rows present).
  ```
  The one `W...non-zero source bytes` warning is expected/benign: with
  `TARGET_WS_GB=8` and `REMOTE_GB=9` (a working set close to the configured
  cache sizes), the zipfian workloads are not fully cache-resident by design —
  this is informational, not an error, and the script still validated and
  exited 0.
- Output Markdown: `tmp/fc_w3/wrapper_all.md` (via absolute path
  `/root/oss/velox/tmp/fc_w3/wrapper_all.md`), produced with `--out`, no CSV,
  no `--input_source` flag. Full contents:

```text
| pattern | read | wrapper | wall_ms | MB/s | ram_MB | ssd_MB | src_MB | Δ vs cbi |
|---|---|---|---:|---:|---:|---:|---:|---:|
| seq | 1024K | cbi | 1516.7 | 5401 | 0 | 8192 | 0 | — |
| seq | 1024K | fcbi | 6802.5 | 1204 | 0 | 0 | 8192 | +348.5% |
| seq | 1024K | dbi | 1503.5 | 5449 | 0 | 0 | 8192 | -0.9% |
| seq | 8192K | cbi | 2145.6 | 3818 | 0 | 8312 | 0 | — |
| seq | 8192K | fcbi | 5686.7 | 1441 | 0 | 0 | 8192 | +165.0% |
| seq | 8192K | dbi | 1548.9 | 5289 | 0 | 0 | 8192 | -27.8% |
| zipf | 1024K | cbi | 431.6 | 18982 | 6217 | 1983 | 0 | — |
| zipf | 1024K | fcbi | 3006.8 | 2724 | 0 | 7770 | 774 | +596.7% |
| zipf | 1024K | dbi | 1539.9 | 5320 | 0 | 0 | 6433 | +256.8% |
| zipf | 8192K | cbi | 165.7 | 49426 | 6968 | 1224 | 0 | — |
| zipf | 8192K | fcbi | 2524.8 | 3245 | 0 | 7352 | 840 | +1423.3% |
| zipf | 8192K | dbi | 5513.1 | 1486 | 0 | 0 | 5656 | +3226.3% |
```

- The common header line `| pattern | read | wrapper | wall_ms | MB/s | ram_MB
  | ssd_MB | src_MB | Δ vs cbi |` is present, and all three wrapper labels
  (`cbi`, `fcbi`, `dbi`) appear across all four `pattern`/`read` cells (seq
  1024K, seq 8192K, zipf 1024K, zipf 8192K) — 12 data rows total. This is
  baseline-only evidence; no performance threshold is applied per the 018-H1
  gate.
- `tmp/fc_w3_cache` was inspected before and after the run: it was empty
  before (freshly created), and empty again after the run (`4.0K`, no
  children) — the accepted `run_wrapper_ab.sh` sentinel/trap cleanup removed
  the `cbi_ssd` and `fcbi_cache` child directories on exit, as designed. No
  manual deletion was performed or required.
- `tmp/fc_w3` retains only `wrapper_all.md` (888 bytes) — no cleanup needed.
- The benchmark also created `tmp/velox_wrapper_bench_remote.bin` (9,663,676,416
  bytes, matching `REMOTE_GB=9`), the synthetic remote-blob source file used by
  the harness; left in place as run evidence (661 GB free on `/root/oss`, no
  disk pressure).

## Step 5: Non-TPCH artifact summary (handoff to 018-P)

| Wave | Binary (RelWithDebInfo) | Log | Exit |
|---|---|---|---|
| Configure | n/a | `configure_018.log` | 0 |
| Build | 3 targets via `ninja` | `build_018h1.log` | 0 |
| Wave 1 | `velox_ch_filecache_seek_benchmark` | `test_018h_w1.log` | 0 |
| Wave 2 | `velox_ch_fcbi_benchmark` | `test_018h_w2.log` | 0 |
| Wave 3 | `velox_bufferedinput_wrapper_benchmark` via `run_wrapper_ab.sh` | `test_018h_w3.log` + `tmp/fc_w3/wrapper_all.md` | 0 |

All logs live under `/root/oss/velox/_build/relwithdebinfo/`. All `tmp/`
artifacts live under `/root/oss/velox/tmp/` (never `/tmp`).

## Gate (018-H1) self-check

- Waves 1–3 all exit 0. ✅
- Every benchmark binary path is under
  `/root/oss/velox/_build/relwithdebinfo` (RelWithDebInfo); no Debug binary
  was built or invoked. ✅
- The wrapper Markdown carries the common header and `cbi`/`fcbi`/`dbi` rows
  (12 rows total across 4 pattern/read cells). ✅
- No performance threshold was applied — this is baseline-only evidence. ✅
- No TPCH source was copied, no TPCH target was built/registered, no TPCH
  data was inspected or required, and no TPCH command was run. ✅

## Status and next step

```text
worker_status: waiting_for_pre_tpch_approval
environment_profile: root-oss
tpch_sources_copied: false
tpch_target_built: false
tpch_commands_run: false
```

**Task 018 is NOT accepted.** This is the mandatory pre-TPCH checkpoint
(018-P). The Controller must review this non-TPCH receipt, and the user must
explicitly approve TPCH work, before any Task-018-C (TPCH correctness) or
018-H2 (TPCH performance wave 4) Worker is dispatched.

## Controller checkpoint review 1 — Wave-3 residency decision required

```text
controller_status: waiting_for_user
task_018_h1_accepted: false
tpch_authorized: false
```

The Worker satisfied the written structural gate, but the Controller did not
accept Wave 3 as a hot-tier A/B result. With `TARGET_WS_GB=8` and
`FILECACHE_DISK_GB=10`, sequential FCBI rows report `src_MB=8192`,
`hits=0`, and resident bytes plateau at 4 GiB.

Root-cause tracing confirms this is the default `SLRU` policy, not a wiring
failure: `slruSizeRatio=0.6` gives new first-touch entries only the 40%
probationary queue, so a 10 GiB cache has 4 GiB effective sequential-warm
capacity. A focused 20 GiB diagnostic probe kept the same 8 GiB target and
reported `hits=8192`, `misses=0`, `src_MB=0`, and 8 GiB resident:

```text
log: /root/oss/velox/_build/relwithdebinfo/test_018h_probe_slru20.log
result: /root/oss/velox/tmp/fc_h1_slru20.md
```

Wave 1 and Wave 2 remain valid. Wave 3 waits for a user decision: increase the
FileCache disk size and rerun, change the SLRU warm procedure, or explicitly
accept the 10 GiB result as a cache-thrashing baseline rather than a hot-tier
comparison.

## Worker attempt 2

```text
worker_status: waiting_for_pre_tpch_approval
environment_profile: root-oss
task: 018-H1
tpch_sources_copied: false
tpch_target_built: false
tpch_commands_run: false
```

### User-approved SLRU capacity decision

The user approved the Controller's Wave-3 recommendation from attempt 1
(cache-thrashing finding: default `SLRU` gives new first-touch entries only
its 40% probationary queue, so a `FILECACHE_DISK_GB=10` cache had just 4 GiB
effective warm capacity for an 8 GiB target). The binding amendment is
committed in ClickHouse at `9a8e08b9fb61a6703eff496ed55820803a3bd79d`
("Account for SLRU in FileCache benchmark capacity"), which raises
`FILECACHE_DISK_GB` from `10` to `20` in
`port/task/018-filecache-gluten-benchmark-plan.md` so the probationary queue
alone (`20 * 0.4 = 8` GiB) can hold the complete 8 GiB first-touch working
set. This rework Worker re-ran **only** Wave 3 with the amended knob; Waves 1
and 2 were not re-run, and no Velox source, build target, TPCH path, or
Gluten path was touched. No stage/commit/amend/rebase/push was performed in
either `/root/oss/velox` or `/root/oss/clickhouse`.

### Old (attempt 1) vs. accepted-candidate (attempt 2) Wave-3 artifacts

| Artifact | `FILECACHE_DISK_GB` | Path | FCBI `src_MB` (seq/1024K, seq/8192K, zipf/1024K, zipf/8192K) |
|---|---|---|---|
| Old (superseded) | `10` | `/root/oss/velox/tmp/fc_w3/wrapper_all_slru10.md` (preserved copy of attempt-1 output; attempt-1's `test_018h_w3.log` also left untouched) | `8192`, `8192`, `774`, `840` — non-zero, cache-thrashing baseline |
| Accepted candidate | `20` | `/root/oss/velox/tmp/fc_w3/wrapper_all.md` (this rerun's output) | `0`, `0`, `0`, `0` — all four FCBI rows are zero-source-byte hot-tier reads, as required |

The old 10-GiB table was copied (not moved/deleted) to the sibling path
`tmp/fc_w3/wrapper_all_slru10.md` before the rerun, since that sibling did
not already exist. `tmp/fc_w3/wrapper_all.md` now holds only the 20-GiB
accepted-candidate result.

### Command (Wave 3 rerun, `FILECACHE_DISK_GB=20`)

```bash
cd /root/oss/velox
mkdir -p tmp/fc_w3
BIN=/root/oss/velox/_build/relwithdebinfo/velox/dwio/common/benchmarks/velox_bufferedinput_wrapper_benchmark \
CACHE_ROOT="$(pwd)/tmp/fc_w3_cache" \
OUT="$(pwd)/tmp/fc_w3/wrapper_all.md" \
RAM_CACHE_GB=4 SSD_CACHE_GB=10 FILECACHE_DISK_GB=20 \
TARGET_WS_GB=8 REMOTE_GB=9 \
READ_SIZES_KIB=1024,8192 WORKLOADS=sequential,zipfian \
MEASURE_PASSES=3 \
  bash velox/benchmarks/scripts/run_wrapper_ab.sh \
  > /root/oss/velox/_build/relwithdebinfo/test_018h_w3_slru20.log 2>&1
```

- Exit code: `0`.
- Log: `/root/oss/velox/_build/relwithdebinfo/test_018h_w3_slru20.log` (new,
  unique — `test_018h_w3.log` from attempt 1 was not overwritten), 754 lines,
  no error lines beyond benign info-level "cbi warm" statistics. The
  attempt-1 `W...non-zero source bytes` warning no longer appears anywhere
  in this rerun.
- Binary reused unchanged from attempt 1's build (`Jul 22 15:56`,
  RelWithDebInfo per `file(1)`); not rebuilt, per rework scope (Waves 1/2 and
  the build step were explicitly out of scope for this rework).

### Result: `tmp/fc_w3/wrapper_all.md` (accepted-candidate, 20 GiB)

```text
| pattern | read | wrapper | wall_ms | MB/s | ram_MB | ssd_MB | src_MB | Δ vs cbi |
|---|---|---|---:|---:|---:|---:|---:|---:|
| seq | 1024K | cbi | 1591.5 | 5147 | 0 | 8192 | 0 | — |
| seq | 1024K | fcbi | 1954.2 | 4192 | 0 | 8192 | 0 | +22.8% |
| seq | 1024K | dbi | 4326.7 | 1893 | 0 | 0 | 8192 | +171.9% |
| seq | 8192K | cbi | 1174.3 | 6976 | 0 | 8264 | 0 | — |
| seq | 8192K | fcbi | 1497.1 | 5472 | 0 | 8192 | 0 | +27.5% |
| seq | 8192K | dbi | 4814.0 | 1702 | 0 | 0 | 8192 | +309.9% |
| zipf | 1024K | cbi | 255.7 | 32032 | 7085 | 1115 | 0 | — |
| zipf | 1024K | fcbi | 1856.2 | 4413 | 0 | 8192 | 0 | +625.8% |
| zipf | 1024K | dbi | 3216.0 | 2547 | 0 | 0 | 6433 | +1157.5% |
| zipf | 8192K | cbi | 242.3 | 33807 | 6920 | 1280 | 0 | — |
| zipf | 8192K | fcbi | 1390.7 | 5891 | 0 | 8192 | 0 | +473.9% |
| zipf | 8192K | dbi | 4700.2 | 1743 | 0 | 0 | 5656 | +1839.7% |
```

- Common header matches exactly:
  `| pattern | read | wrapper | wall_ms | MB/s | ram_MB | ssd_MB | src_MB |
  Δ vs cbi |`.
- All 12 `cbi`/`fcbi`/`dbi` data rows present across all 4 `pattern`/`read`
  cells.
- **All four measured FCBI rows report `src_MB=0`** (seq/1024K,
  seq/8192K, zipf/1024K, zipf/8192K) — the binding requirement from the
  `9a8e08b9fb6` amendment is satisfied; no non-zero FCBI source byte was
  observed, so nothing here is BLOCKED.

### Cleanup / Git-state checks

- `tmp/fc_w3_cache`: empty before the rerun and empty after (`4.0K`, no
  children) — sentinel/trap cleanup from the accepted `run_wrapper_ab.sh`
  removed `cbi_ssd`/`fcbi_cache` on exit, as designed; no manual deletion.
- No `bench_run_*.md` file was auto-created anywhere in `/root/oss/velox`
  (`find` returned nothing); no such file needed removal.
- `/root/oss/velox`: `git status --short` clean, `git diff --stat` empty,
  `git rev-parse HEAD` unchanged at
  `5ae39651b8772116c33fd2f0fdbd388f55f5fd15`, both before and after this
  rework. No Velox source file, build target, TPCH path, or Gluten checkout
  was touched. No stage/commit/amend/rebase/push was performed.
- `/root/oss/clickhouse`: no stage/commit/amend/rebase/push performed; only
  this result file and the companion report were appended (both were already
  untracked working-tree files from attempt 1).

### Status and next step

```text
worker_status: waiting_for_pre_tpch_approval
environment_profile: root-oss
tpch_sources_copied: false
tpch_target_built: false
tpch_commands_run: false
```

**Task 018 is still NOT accepted.** This rework closes out the Wave-3 SLRU
capacity finding only. The mandatory 018-P pre-TPCH checkpoint (Controller
review of this rework plus explicit user approval) is still required before
any Task-018-C (TPCH correctness) or 018-H2 (TPCH performance wave 4) Worker
is dispatched.

## Controller checkpoint review 2

```text
controller_checkpoint_status: accepted
controller_status: waiting_for_user
task_018_h1_accepted: true
task_018_accepted: false
tpch_authorized: false
critical_findings: 0
important_findings: 0
minor_findings: 1
```

The Controller and an independent reviewer verified the amended Wave-3 log and
artifacts directly. Every FCBI cell has three measured passes with
`hits=8192`, `misses=0`, `writeBytes=0`, and 8 GiB resident; all four Markdown
rows report `src_MB=0`. The old 10 GiB result remains preserved as superseded
diagnostic evidence, the new sentinel-managed cache directories are removed,
and Velox remains clean at `5ae39651b`.

Velox already tracks an upstream `velox/benchmarks/tpch` directory at this
accepted HEAD. It is byte-for-byte unchanged from `HEAD`, the RelWithDebInfo
`velox_tpch_benchmark` binary does not exist, and the build cache still records
`VELOX_ENABLE_PARQUET:BOOL=OFF`; therefore no Task-018-C source copy or target
build occurred before this checkpoint.

The one Minor finding is pre-existing harness diagnostics: `[fcbi-stats]`
uses error log level for non-error statistics. It does not affect this result.

The complete non-TPCH phase is accepted at checkpoint 018-P. Execution stops
here until the user explicitly authorizes TPCH source copy, target build, data
inspection, and 018-C/H2 execution.

## Controller approval — 018-P passed

```text
approval_date: 2026-07-22
controller_status: worker_dispatch_authorized
task_018_h1_accepted: true
task_018_accepted: false
tpch_authorized: true
tpch_data: /root/oss/test-data/tpch-sf100-parquet-double
next_task: 018-C
```

The user explicitly approved checking the dataset, reconfiguring the
RelWithDebInfo tree with Parquet enabled, building the Task-018 TPCH target, and
executing 018-C correctness followed by 018-H2 performance. A fresh Worker must
run 018-C before any TPCH performance measurement.
