# `FileCache` Tasks 017A, 018, and 017B Execution Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the remaining mainline `FileCache` work in the binding order `017A -> 018 -> Review 5 -> 017B -> 019`, with Task 018 Velox-only and Task 019 owning all Gluten/Spark integration.

**Architecture:** Task 017A establishes the Velox statistics, cancellation, caller identity, and scheduler APIs. Task 018 consumes those APIs only in Velox correctness and benchmark binaries. Review 5 reviews Tasks 003–018 as a Velox system and closes Review-4 findings; Task 017B then replaces the logging/exception shim. Task 019 finally establishes a Gluten-compatible Velox baseline and owns lifecycle, Builder, metric bridge, native E2E, and Spark E2E.

**Tech Stack:** C++20, Folly, Velox, Gluten C++/JNI/Java/Scala, CMake/Ninja, GoogleTest, Maven/ScalaTest, shell benchmark orchestration.

## Global Constraints

- Environment profile: `root-oss`.
- Binding designs: `port/design/filecache-task-017-018-joint-design.md` and the
  ownership amendment `port/design/filecache-task-018-019-hard-split.md`.
- Worker never stages or commits; Controller reviews and commits accepted work.
- Never rebase or amend; corrective work uses new commits.
- Preserve the dirty `/root/oss/gluten` checkout; only Task 019 uses an isolated Gluten worktree.
- No C++ sleeps, no Ninja `-j`, no `nproc`, and all build/test output goes to build-directory logs.
- A task is not accepted until focused tests, mutations, mono/non-mono or integration gates, accumulated gates, and one read-only task-diff review are green.
- Real kernel `O_DIRECT` and Task 016 remain excluded. Task 019 implementation is
  excluded from Tasks 017A/018, Review 5, and Task 017B; it begins only in Task 5
  after all prerequisites are accepted.
- Review 4 remains `PARITY_BLOCKED` during Tasks 017A/018 but is explicitly non-blocking for those two tasks by user decision. No complete-parity or production-ready claim is allowed before Review 5 closes or dispositionally accepts its remaining rows.

---

### Task 1: Execute and accept Task 017A

**Files:**
- Execute: `port/task/017a-filecache-statistics-cancellation-plan.md`
- Write: `port/task/result/017a-filecache-statistics-cancellation-result.md`
- Update after acceptance: `port/task/CONTROLLER_HANDOFF.md`

**Interfaces:**
- Produces: `FileCacheStatsSnapshot`, `takeFileCacheStatsSnapshot`, `kFileCacheWriteBytes`
- Produces: `FileCacheBufferedInput` constructor ending in `fileReadOps, cancellationToken`
- Produces: real `CurrentMetrics`/`ProfileEvents`, reader double-accounting, safe cancellation, CH caller ID, two-lock scheduler

- [ ] **Step 1: Verify the execution baseline**

```bash
git -C /root/oss/clickhouse --no-pager status --short --branch
git -C /root/oss/velox --no-pager status --short --branch
```

Expected: ClickHouse branch `ch-filecache`; Velox branch `filecache`; unrelated changes are recorded and preserved.

- [ ] **Step 2: Execute every checkbox in the Task-017A plan**

Use `port/task/017a-filecache-statistics-cancellation-plan.md` as the complete implementation contract. Stop on the first unreviewed dependency or contract conflict.

- [ ] **Step 3: Run the Task-017A review and acceptance gate**

The Worker writes the receipt and stops at `ready_for_controller`. The Controller checks the complete Velox diff, log summaries, mutation RED evidence, and fresh mono/non-mono accumulated CTest results.

Expected: Task 017A is accepted before any Task-018 implementation starts.

---

### Task 2: Execute and accept Task 018

**Files:**
- Execute: `port/task/018-filecache-gluten-benchmark-plan.md`
- Write: `port/task/result/018-filecache-velox-benchmark-result.md`
- Update after acceptance: `port/task/CONTROLLER_HANDOFF.md`

**Interfaces:**
- Consumes: every Task-017A interface listed in Task 1
- Produces: Velox correctness, micro, wrapper, orchestration, and TPCH benchmark artifacts

- [ ] **Step 1: Confirm Task 017A is accepted**

```bash
grep -n 'controller_status: accepted' \
  port/task/result/017a-filecache-statistics-cancellation-result.md
```

Expected: one accepted Controller review and no later reopening.

- [ ] **Step 2: Confirm the Velox workspace**

Use `/root/oss/velox` only. Task 018 does not create or modify a Gluten worktree.

- [ ] **Step 3: Execute the non-TPCH Task-018 phase**

Run `018-A -> 018-B -> 018-D -> 018-H1`.
Every benchmark target is freshly built in RelWithDebInfo or Release; Debug
benchmark output is invalid.

- [ ] **Step 4: Stop at the mandatory pre-TPCH checkpoint**

The Worker records non-TPCH evidence and stops. The Controller reviews the
Velox changes and Waves 1–3 artifacts. No TPCH source is copied, no TPCH
target is built, and no TPCH command runs before explicit user approval.

- [ ] **Step 5: After approval, execute TPCH and finish Task 018**

Dispatch a fresh Task-018 Worker for `018-C -> 018-H2`. Build and run TPCH only
from RelWithDebInfo or Release.

- [ ] **Step 6: Run the Task-018 Velox acceptance gate**

Review the complete Velox diff. Verify byte/content correctness, physical/logical
statistics, sentinel cleanup, benchmark build type, result schema, one-split
fairness, and benchmark artifacts.

Expected: Velox-only Task 018 is accepted, then execution stops for Review 5.
Task 017B must not start yet.

---

### Task 3: Run Review 5 — Tasks 003–018 whole-port review

**Files:**
- Execute: `port/task/fullreview/root-oss/5/003-018-whole-port-review-plan.md`
- Read: `port/task/fullreview/root-oss/4/003-015-ch-parity-audit.md`
- Read: `port/task/fullreview/root-oss/4/003-015-parity-user-decisions.md`
- Produce: `port/task/fullreview/root-oss/5/003-018-whole-port-review.md`
- Produce: `port/task/fullreview/root-oss/5/003-018-review-decisions-needed.md`

**Interfaces:**
- Consumes: accepted Tasks 003–018 Velox commits, receipts, designs, tests, and benchmark evidence
- Produces: Review-4 corrective disposition plus an integrated Tasks 017A/018 contract/parity verdict

- [ ] **Step 1: Stop dispatch after Task 018**

Do not start Task 017B. Freeze exact ClickHouse and Velox baselines and run the
Review-5 plan.

- [ ] **Step 2: Close the Review-5 gate**

Every Critical/Important finding must be fixed, explicitly accepted by the user,
or recorded as a non-blocking forward item. The review must independently verify
the affected implementation and evidence before changing any Review-4
classification.

Expected: Review 5 records an accepted verdict and explicitly authorizes Task
017B. Otherwise the pipeline remains stopped.

---

### Task 4: Execute and accept Task 017B

**Files:**
- Execute: `port/task/017b-filecache-logging-exception-stack-plan.md`
- Write: `port/task/result/017b-filecache-logging-exception-stack-result.md`
- Update after acceptance: `port/task/CONTROLLER_HANDOFF.md`

**Interfaces:**
- Consumes: the accepted Velox branch after Task 018
- Produces: lazy attributed FileCache logging, current-exception formatting, optional Velox stack output, and `noexcept` logging overloads

- [ ] **Step 1: Confirm Task 018 and Review 5 are accepted**

```bash
grep -n 'controller_status: accepted' \
  port/task/result/018-filecache-velox-benchmark-result.md
grep -n 'review_status: accepted' \
  port/task/fullreview/root-oss/5/003-018-whole-port-review.md
```

Expected: accepted Task-018 Controller review and accepted Review-5 verdict with
no unresolved blocking cross-repository finding.

- [ ] **Step 2: Execute every checkbox in the Task-017B plan**

Use `port/task/017b-filecache-logging-exception-stack-plan.md` as the complete contract.

- [ ] **Step 3: Run the Task-017B review and acceptance gate**

The Controller verifies lazy argument evaluation, logger attribution, exception
type/stack formatting, preservation of the original exception, real glog sink
evidence, CMake linkage, mutations, and mono/non-mono accumulated tests.

Expected: Task 017B is accepted with zero unresolved findings.

---

### Task 5: Execute Task 019 Gluten integration and Spark E2E

**Files:**
- Execute: `port/task/019-filecache-gluten-integration-spark-e2e-plan.md`
- Write: `port/task/result/019-filecache-gluten-integration-spark-e2e-result.md`
- Update after acceptance: `port/task/CONTROLLER_HANDOFF.md`

**Interfaces:**
- Consumes: accepted Tasks 017A/018/017B and accepted Review 5
- Produces: compatible Velox baseline, Gluten lifecycle/Builder/metric bridge,
  native E2E, and Spark correctness/performance E2E

- [ ] **Step 1: Verify all prerequisites**

```bash
for task in \
  017a-filecache-statistics-cancellation \
  018-filecache-velox-benchmark \
  017b-filecache-logging-exception-stack; do
  grep -n 'controller_status: accepted' "port/task/result/${task}-result.md"
done
grep -n 'review_status: accepted' \
  port/task/fullreview/root-oss/5/003-018-whole-port-review.md
```

Expected: all three receipts and Review 5 are accepted and none has a later
reopening.

- [ ] **Step 2: Execute Task 019-A compatibility hard gate**

Do not continue to Gluten implementation until a Velox baseline containing both
the accepted FileCache and every selected-Gluten API compiles and links the
Gluten native targets.

- [ ] **Step 3: Execute Task 019-B through 019-F**

Use fresh Workers and task reviews for lifecycle, Builder, metrics, native E2E,
and Spark E2E. Task 019 is accepted only after all six subtasks are green.
