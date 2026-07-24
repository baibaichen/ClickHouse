# Task 017B — Implementation Plan Review Receipt

```text
scope:                   Task 017B FileCache Logging and Exception Stack
receipt_type:            durable_plan_review_receipt
environment_profile:     root-oss
velox_head:              cda6c03703  (branch filecache, clean)
clickhouse_head:         0d510c37d77 (branch ch-filecache, clean)
binding_spec:            port/design/filecache-task-017b-logging-exception-stack.md
reviewed_plan:           port/task/017b-filecache-logging-exception-stack-plan.md
plan_review_1:           .superpowers/sdd/task017b-plan-independent-review.md
plan_review_2:           .superpowers/sdd/task017b-plan-independent-rereview.md
written_spec_approved:   2026-07-24 (user)
authorization_date:      2026-07-24 (Controller)
plan_compliance:         approved
plan_executability:      approved
critical_open:           0
important_open:          0
minor_open:              0 (both cosmetic minors subsequently corrected in plan)
implementation_status:   NOT STARTED — no Task 017B code has been written
```

## 1. Written Spec Approval

The binding design `port/design/filecache-task-017b-logging-exception-stack.md` was reviewed and
approved by the user on 2026-07-24 as a prerequisite to plan authorship. The design covers:

- `FileCacheLogger` as an immutable name-holder; glog as the only backend
- Lazy three-gate (`GOOGLE_STRIP_LOG`, `FLAGS_minloglevel`, `VLOG_IS_ON`) filter for
  `LOG_TEST`/`TRACE`/`DEBUG` mapped to `VLOG(3/2/1)`; two-gate for `LOG_INFO`/`WARNING`/`ERROR`
- Zero evaluation of all caller expressions for filtered records; zero `LoggerPtr` ownership copy
  via `auto&&` binding in the enabled macro path
- `getCurrentExceptionMessage` declared `noexcept`; safe outside a catch context; never bare `throw`
- `tryLogCurrentException` declared `noexcept`; never replaces the original exception; emergency
  allocation-free `::write(STDERR_FILENO, …)` fallback
- Focused mono/non-mono build matrix; nine spec-mandated mutations; `LogSink`-based capture tests

The spec is authoritative. The implementation plan must not contradict it.

## 2. Initial Plan Review — Findings and Correction Matrix

Initial review conducted: 2026-07-24.
Review file: `.superpowers/sdd/task017b-plan-independent-review.md`
Initial verdict: Plan compliance **REJECTED** / Plan executability **REJECTED** (3C / 2I / 5m).

### Critical Findings (3) — Blocked Executability

| ID | Finding | Fix Applied |
|----|---------|-------------|
| C1 | All 26 mono `ninja`/`ctest` references targeted non-existent `_build/release`; no configure step provided. A fresh Worker could not run any mono gate verbatim. | All 13 `ninja -C`/`ctest` locations and the mutation workflow updated to target the canonical mono dir `_build/debug`. |
| C2 | Non-mono configure (`M5-B3`) omitted every vcpkg/toolchain and feature flag required by root-oss (`CMAKE_TOOLCHAIN_FILE`, `FETCHCONTENT_FULLY_DISCONNECTED`, `VELOX_GFLAGS_TYPE=static`, `VELOX_BUILD_TESTING=ON`, `VELOX_ENABLE_BENCHMARKS=ON`, and others). Without the toolchain, glog/fmt/Folly could not be found; without `VELOX_BUILD_TESTING=ON` the accumulated ctest gate was vacuous. | Non-mono configure (`M5-B3`) rewritten to match `_build/debug/CMakeCache.txt` flag-for-flag (15 flags verified), with only `VELOX_MONO_LIBRARY` flipped OFF. Dir renamed to `_build/debug-task017b-nonmono` per convention. |
| C3 | No configure/build/test command sourced the mandatory `<velox_env>` (`/root/oss/velox-helper/env.sh`), which exports `*_SOURCE=SYSTEM` variables and `-Wno-deprecated-declarations` (needed by the `LogSink::send(const std::tm*)` override). | Every one of the 16 command blocks prefixed with `source /root/oss/velox-helper/env.sh && …`. |

### Important Findings (2) — Blocked Compliance

| ID | Finding | Fix Applied |
|----|---------|-------------|
| I1 | MUT-06 ("bare throw outside catch context") was non-discriminating: the mutant never reached `formatExceptionPtr` in `NoActiveException` and all exception tests still passed — false green for a spec-required property. | MUT-06 retargeted: applies both the removal of the `if (!eptr) return {};` null guard in `getCurrentExceptionMessage` AND replacing `std::rethrow_exception(eptr)` with bare `throw;` in `formatExceptionPtr`, so that calling `getCurrentExceptionMessage` outside a catch context invokes `throw;` with no active exception → `std::terminate`. `NoActiveException` is wrapped in `EXPECT_EXIT(…, ExitedWithCode(0), "")` using the safe forked death-test shape, so signal-death under MUT-06 fails the predicate → RED. Production path retains `std::rethrow_exception(eptr)` + null guard. |
| I2 | The mutation matrix omitted the spec-required "removing a verbosity gate" mutation (spec item 1: delete `VLOG_IS_ON`). MUT-09 was incorrectly consuming a non-spec compile-strip build-config check. | MUT-09 repurposed: deletes `VLOG_IS_ON(vlog_level) &&` from `CH_VLOG_IMPL_`; affected test `VlogLevelsFilteredAtDefaultV`; expected RED when `logger_eval`/`arg_eval` become non-zero at default `FLAGS_v=0` because `if (0 >= FLAGS_minloglevel)` stays true. Compile-strip reclassified as a Milestone-4 configuration verification, explicitly labelled "not one of the nine spec mutations". All nine binding-spec mutations are now mapped and independently verified as discriminating. |

### Minor Findings (5) — All Corrected

| ID | Finding | Disposition |
|----|---------|-------------|
| m1 | M3-C1 library discovery broken (multi-line `grep` pipeline captured nothing). | Fixed: four `velox_ch_filecache*` libraries listed explicitly in `ninja` invocation. |
| m2 | Caller inventory incomplete; missing `Metadata.cpp`, `EvictionCandidates.cpp`, `SLRUFileCachePriority.cpp`, `FileCacheScheduler.cpp`. | Fixed: §1 split into "FileCache callers" (`tryLogCurrentException`) and "Getter-only call sites" (`getCurrentExceptionMessage`), with correct library-ownership notes. |
| m3 | MUT-09 description incorrectly attributed non-strip non-zero result to `LOG_TEST`; should be `LOG_INFO`. | Fixed: folded into I2 resolution; compile-strip blockquote correctly attributes the result to `LOG_INFO`. |
| m4 | No contingency for newly-enabled `fmt::format_string` compile-time checking exposing pre-existing call-site type mismatches (no call-site rewrite permitted per Global Constraint #12). | Fixed: §9 Risk Mitigation adds explicit stop-and-escalate note: if a genuine format/type mismatch surfaces at M2-G1/M3-C1, Worker records the exact site+error and escalates to Controller — no silent rewrite. |
| m5 | `tryLogCurrentException` captured `std::current_exception` twice per invocation (null-check + `getCurrentExceptionMessage` internal re-capture), contradicting the spec's "exactly once" contract. | Fixed: both overloads now capture once and pass `eptr` directly to `detail::formatExceptionPtr(eptr, true)`. |

## 3. Re-Review — Verification of All Corrections

Re-review conducted: 2026-07-24 (same calendar date as initial review, corrected plan submitted immediately).
Review file: `.superpowers/sdd/task017b-plan-independent-rereview.md`
Re-review verdict: Plan compliance **APPROVED** / Plan executability **APPROVED** (0C / 0I / 2 cosmetic minors).

All five Critical+Important corrections were independently re-verified against the live tree at
`cda6c03703` and the installed vcpkg toolchain. Key confirmations:

- `_build/debug` exists and is the canonical mono dir; `_build/release` absent (C1 resolved).
- All 15 non-mono configure flags verified one-for-one against `_build/debug/CMakeCache.txt` (C2 resolved).
- `grep -c 'source.*env.sh'` = 16 — one per command block (C3 resolved).
- MUT-06 safe death-test shape (`EXPECT_EXIT`/`ExitedWithCode(0)`) confirmed discriminating;
  production `std::rethrow_exception(eptr)` + null guard retained (I1 resolved).
- Full spec-mutation ↔ MUT map verified, MUT-01 through MUT-09 each independently traced to a
  discriminating test assertion flip (I2 resolved).
- Single `current_exception` capture per `tryLogCurrentException` invocation confirmed (m5 resolved).

### Residual Cosmetic Minors (2) — Confirmed Corrected in Final Plan

| ID | Finding | Status |
|----|---------|--------|
| mr1 | §1/M3-T8 caller-row labeling conflated `tryLogCurrentException` and `getCurrentExceptionMessage` callers in a single table row. | **Corrected**: §1 split into separate "FileCache callers" and "Getter-only call sites" rows with explicit call-shape columns. |
| mr2 | `NoActiveException` suite lacked `*DeathTest` suffix; child body used `exit(0)`/`exit(1)` instead of `_exit`. | **Corrected**: suite renamed `ExceptionFormattingDeathTest`; child body uses `_exit(0)`/`_exit(1)`. |

## 4. Final Plan State

After all corrections, the plan at `port/task/017b-filecache-logging-exception-stack-plan.md` is:

- **Free of open Critical or Important findings.**
- **Free of open Minor findings** (all five corrected; two cosmetic re-review items also corrected).
- **Fully specified for a fresh Worker** in environment `root-oss` at Velox head `cda6c03703`.
- **Non-self-authorizing**: the plan header and Global Constraint #2 kept `implementation_authorized`
  with the Controller through the review phase; no Worker may flip this gate.
- **Mutation-complete**: all nine binding-spec mutations (MUT-01 through MUT-09) mapped to
  discriminating tests; no false-green mutations remain.
- **Consistent with the binding design** at every verified decision point.

## 5. Controller Authorization

The Controller authorizes Task 017B implementation on **2026-07-24**, having verified:

1. Written spec approved by the user (2026-07-24).
2. Initial plan review: 3 Critical, 2 Important, 5 Minor — all resolved.
3. Re-review: 0 Critical, 0 Important, 2 cosmetic minors — both subsequently corrected.
4. Final plan state: compliant, executable, mutation-complete, non-self-authorizing.
5. No Task 017B implementation code exists; Velox head remains `cda6c03703`.
6. ClickHouse head remains `0d510c37d77` (branch `ch-filecache`, clean).

**Authorization boundary:** A fresh Task 017B Worker may begin implementation by following
`port/task/017b-filecache-logging-exception-stack-plan.md` verbatim. The Worker must not commit;
it must write its result receipt to `port/task/result/017b-filecache-logging-exception-stack-result.md`
with `worker_status: ready_for_controller`, then stop.

Task 019 remains blocked on Task 017B acceptance.

## 6. No Implementation Performed

This receipt records review, correction, re-review, and Controller authorization only. No
`logger_useful.h`, `logger_useful.cpp`, or test file has been created or modified. No Velox source
has been changed. No build has been run. The authorization recorded here allows a future Worker to
begin; it does not itself constitute implementation.
