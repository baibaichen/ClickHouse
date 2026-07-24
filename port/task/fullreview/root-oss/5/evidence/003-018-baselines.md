# Review 5 Task 1 — Baseline Freeze Evidence

Frozen accepted heads from the task brief:

- ClickHouse accepted/pushed HEAD: `03f88d4e3e6`
- Velox accepted/pushed HEAD: `7c52b47ecb`

This file records the exact baseline state, receipt index, and retained evidence
paths needed by later Review 5 work. It is read-only evidence; no source or
build files were modified.

## 1. Repository state

| repo | branch | HEAD | upstream | ahead/behind | dirty/untracked | last commits |
|---|---|---|---|---|---|---|
| `/root/oss/clickhouse` | `ch-filecache` | `03f88d4e3e672eea95f90e348c1c533958d63e0d` | `baibaichen/ch-filecache` | `0/0` | clean | `03f88d4e3e6` Accept Task 018 four-driver addendum · `053e2c30bf8` Design Task 017B lazy `FileCache` logging · `0d342607162` Plan TPCH q15 parallel fix · `a8ec735e122` Plan parallel TPCH verification · `064700b804c` Design parallel TPCH verification |
| `/root/oss/velox` | `filecache` | `7c52b47ecbe6799df37966bd07b03899e0612d66` | `baibaichen/filecache` | `0/0` | clean | `7c52b47ecb` Fix TPCH q15 parallel revenue selection · `7e3b5b72fe` Task 018: Run verified four-driver TPCH · `2814eb7dcf` Task 018: Verify parallel TPCH results · `4f3cb3c047` Task 018: Default TPCH performance to one driver · `609cf21da9` Task 018: Route Hive reads through FileCache |

Observed `git status --short --branch`:

```text
## ch-filecache...baibaichen/ch-filecache
## filecache...baibaichen/filecache
```

No dirty paths or untracked files were present before this evidence file was
written.

## 2. Task 018 addendum gate

`port/task/result/018-filecache-velox-benchmark-result.md` contains the required
later Controller acceptance:

- earlier historical marker: `parallel_four_driver_addendum_status: pending`
  at line `857`
- later Controller acceptance: `parallel_four_driver_addendum_status:
  accepted` at line `871`

The accepted line appears once, in the later
`## Controller review — parallel four-driver addendum accepted` section. The
earlier `pending` marker is preserved historical evidence and is superseded by
the later Controller section.

## 3. Receipt index for Tasks 003–018

| receipt | latest authoritative status | historical / contradictory markers |
|---|---|---|
| `000-prepare-velox-dev-environment-result.md` | `success` | none |
| `001-velox-ch-skeleton-result.md` | `success` | none |
| `002-common-noop-shims-result.md` | `success` | none |
| `003-filecache-basic-common-shims-result.md` | `accepted` | historical `reopened_by_contract_audit`; earlier accepted markers remain in the file |
| `004-filecache-status-and-guards-result.md` | `accepted` | historical `changes_requested` then later acceptance |
| `005-filecache-thread-pools-result.md` | `accepted` | historical `changes_requested`; later worker-ready / accepted completion present |
| `006-filecache-scheduler-and-caller-scope-result.md` | `accepted` | historical `changes_requested` |
| `007-filecache-io-adapters-result.md` | `accepted` | historical `changes_requested` / `blocker_resolved` sections |
| `008-filecache-leaf-types-result.md` | `accepted` | historical `changes_requested` |
| `009-filecache-sharded-map-result.md` | `accepted` | historical `blocked`, `blocker_resolved`, and `changes_requested` sections |
| `010-filecache-settings-result.md` | `accepted` | historical `blocked`, `blocker_resolved`, and `changes_requested` sections |
| `011-filecache-priority-eviction-result.md` | `accepted` | historical `reopened_by_contract_audit`; final acceptance remains in file |
| `012-filecache-core-scc-result.md` | `accepted` | historical `reopened_by_contract_audit`; final acceptance remains in file |
| `013-filecache-factory-manager-result.md` | `accepted` | none |
| `014-filecache-buffered-input-result.md` | `accepted` | historical `reopened_by_contract_audit`; final acceptance remains in file |
| `015-filecache-velox-e2e-result.md` | `accepted` | none |
| `017a-filecache-statistics-cancellation-result.md` | `accepted` | none |
| `018-filecache-velox-benchmark-result.md` | `accepted` | historical `waiting_for_user`, `changes_requested`, and earlier `pending` addendum marker; final Controller section accepts the addendum |

Missing result receipts in the indexed range:

- no `016-*` receipt exists in `port/task/result/`
- no `017-*` receipt exists; the actual receipt is `017a-*`
- no `017b-*` receipt exists yet

No contradictory final-state receipt was found. Historical interim states are
kept as provenance and are superseded by the later acceptance sections in the
same files.

## 4. Retained Task 017A / Task 018 evidence

### 017A logs retained under `_build`

All of these files exist:

- `/root/oss/velox/_build/debug-task017a-nonmono/configure_nonmono_pt5.log`
- `/root/oss/velox/_build/debug/build_all_mono_pt5.log`
- `/root/oss/velox/_build/debug-task017a-nonmono/build_all_nonmono_pt5.log`
- `/root/oss/velox/_build/debug/ctest_mono_pt5_r2.log`
- `/root/oss/velox/_build/debug-task017a-nonmono/ctest_nonmono_pt5_r2.log`

### 018 build / test / benchmark logs retained under `_build`

All of these files exist:

- `/root/oss/velox/_build/relwithdebinfo/configure_018.log`
- `/root/oss/velox/_build/relwithdebinfo/build_018h1.log`
- `/root/oss/velox/_build/relwithdebinfo/test_018h_w1.log`
- `/root/oss/velox/_build/relwithdebinfo/test_018h_w2.log`
- `/root/oss/velox/_build/relwithdebinfo/test_018h_w3.log`
- `/root/oss/velox/_build/relwithdebinfo/test_018h_probe_slru20.log`
- `/root/oss/velox/_build/relwithdebinfo/test_018h_w3_slru20.log`
- `/root/oss/velox/_build/relwithdebinfo/build_one_driver_tpch.log`
- `/root/oss/velox/_build/relwithdebinfo/test_one_driver_focused_q01.log`
- `/root/oss/velox/_build/relwithdebinfo/test_one_driver_smoke.log`
- `/root/oss/velox/_build/relwithdebinfo/test_one_driver_full_fc.log`
- `/root/oss/velox/_build/relwithdebinfo/test_one_driver_ab.log`
- `/root/oss/velox/_build/relwithdebinfo/build_q15_parallel_fix.log`
- `/root/oss/velox/_build/relwithdebinfo/build_parallel_verified4_q15fixed.log`
- `/root/oss/velox/_build/relwithdebinfo/test_parallel_verified4_q15fixed.log`

### Accepted result CSV / markdown evidence retained under `tmp`

All of these files exist:

- `/root/oss/velox/tmp/fc_w3/wrapper_all.md`
- `/root/oss/velox/tmp/fc_h1_slru20.md`
- `/root/oss/velox/tmp/one_driver_focused/q01.csv`
- `/root/oss/velox/tmp/one_driver_smoke/q01.csv`
- `/root/oss/velox/tmp/one_driver_smoke/q09.csv`
- `/root/oss/velox/tmp/one_driver_smoke/q21.csv`
- `/root/oss/velox/tmp/one_driver_full_fc/filecache_full.csv`
- `/root/oss/velox/tmp/one_driver_ab_results/tpch_direct.csv`
- `/root/oss/velox/tmp/one_driver_ab_results/tpch_cbi.csv`
- `/root/oss/velox/tmp/one_driver_ab_results/tpch_filecache.csv`
- `/root/oss/velox/tmp/parallel_verified4_q15fixed_results/tpch_direct.csv`
- `/root/oss/velox/tmp/parallel_verified4_q15fixed_results/tpch_cbi.csv`
- `/root/oss/velox/tmp/parallel_verified4_q15fixed_results/tpch_filecache.csv`

Selected artifact identities:

- `parallel_verified4_q15fixed_results/*.csv` are the accepted four-driver
  addendum outputs.
- `one_driver_*` artifacts are the accepted one-driver Task 018 baseline.
- `fc_w3/wrapper_all.md` is the accepted Wave-3 baseline-only wrapper report.
- `fc_h1_slru20.md` is the preserved diagnostic SLRU probe evidence.

## 5. Review-4 parity audit anchor

The required Review-4 synthesis file exists at:

- `port/task/fullreview/root-oss/4/003-015-ch-parity-audit.md`

Its recorded state is:

```text
audit_round: 4
phase_a_status: complete, validated
phase_b_status: complete, validated
phase_c_status: complete — independent reviewer returned
verdict: PARITY_BLOCKED
```

It freezes the earlier Tasks 003–015 baseline and remains unchanged.

## 6. Evidence summary

- ClickHouse and Velox are both clean at the frozen accepted heads.
- The Task 018 four-driver addendum gate is satisfied exactly once.
- All required Task 017A / Task 018 logs and accepted CSV evidence files exist.
- Historical receipt states remain in the markdown receipts, but the latest
  authoritative state for the indexed receipts is accepted / success as listed
  above.

## 7. Task 012 corrective baseline transition (Review 5, 2026-07-24)

ClickHouse `ch-filecache` HEAD is unchanged at `03f88d4e3e6`.

Velox `filecache` HEAD advanced from frozen baseline `7c52b47ecb` to:

```text
26325e8a32  Use Folly once guard for `FileCache` initialization
```

Branch `baibaichen/filecache` is clean and pushed at `26325e8a32`.

Receipt section accepting this commit:

```text
port/task/result/012-filecache-core-scc-result.md
  → ## Controller review 8 — Review-5 Task 012 corrective accepted (2026-07-24)
```

Controller log paths (all under `/root/oss/velox/`):

```text
_build/debug/build_review5_task012_controller_final.log         EXIT:0
_build/debug/test_review5_task012_controller_final.log          3/3
_build/debug-task017a-nonmono/build_review5_task012_controller_final.log  EXIT:0
_build/debug-task017a-nonmono/test_review5_task012_controller_final.log   3/3
```

Effect on parity: `L-CALLONCE-01` moves from UNPROVEN to EQUIVALENT. `R2-D2`
moves from `reopen_task` to `closed`. See
`port/task/fullreview/root-oss/5/evidence/review-4-closure.md` §2.1, §3.2, §7.

## 8. Task 014 corrective baseline transition (Review 5, 2026-07-24)

Velox `filecache` advanced from `26325e8a32` to:

```text
cda6c03703  Task 014: Recover truncated and renamed cache reads
```

The accepted corrective closes both:

```text
G-CACHEBUF-01:
  externally shortened terminal cache files bypass and re-fetch
G-CACHEOPEN-RENAME-01:
  an old cache path renamed before open is recomputed under segment lock and
  retried once only for kFileNotFound
```

Controller logs:

```text
/root/oss/velox/_build/debug/test_task014_controller_final_selected.log
  2/2
/root/oss/velox/_build/debug/test_task014_controller_final_accumulated.log
  16/16
/root/oss/velox/_build/debug-task012-nonmono/test_task014_controller_final_selected.log
  2/2
```

Receipt:

```text
port/task/result/014-filecache-buffered-input-result.md
  → ## Controller review 7 — Review-5 Task-014 corrective accepted
```
