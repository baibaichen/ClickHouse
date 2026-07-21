# `FileCache` Tasks 017A, 018, and 017B Execution Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the remaining mainline `FileCache` work in the binding order `017A -> 018 -> 017B`, then open Task 019 design only after all three tasks are accepted.

**Architecture:** Task 017A first establishes the Velox statistics, cancellation, caller identity, and scheduler APIs. Task 018 consumes those accepted APIs in Velox benchmarks and an isolated Gluten worktree. Task 017B then replaces the independent logging/exception shim; no Task 019 implementation is included.

**Tech Stack:** C++20, Folly, Velox, Gluten C++/JNI/Java/Scala, CMake/Ninja, GoogleTest, Maven/ScalaTest, shell benchmark orchestration.

## Global Constraints

- Environment profile: `root-oss`.
- Binding design: `port/design/filecache-task-017-018-joint-design.md`.
- Worker never stages or commits; Controller reviews and commits accepted work.
- Never rebase or amend; corrective work uses new commits.
- Preserve the dirty `/root/oss/gluten` checkout; Task 018 uses an isolated worktree.
- No C++ sleeps, no Ninja `-j`, no `nproc`, and all build/test output goes to build-directory logs.
- A task is not accepted until focused tests, mutations, mono/non-mono or integration gates, accumulated gates, and one read-only task-diff review are green.
- Real kernel `O_DIRECT`, Task 016, and Task 019 implementation remain excluded.

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
- Write: `port/task/result/018-filecache-gluten-benchmark-result.md`
- Update after acceptance: `port/task/CONTROLLER_HANDOFF.md`

**Interfaces:**
- Consumes: every Task-017A interface listed in Task 1
- Produces: Velox correctness/micro/wrapper/TPCH benchmark targets and scripts
- Produces: Gluten `FileCacheManager` lifecycle, Builder selection, cancellation token boundary, and `fileCacheWriteBytes` SQL metric

- [ ] **Step 1: Confirm Task 017A is accepted**

```bash
grep -n 'controller_status: accepted' \
  port/task/result/017a-filecache-statistics-cancellation-result.md
```

Expected: one accepted Controller review and no later reopening.

- [ ] **Step 2: Create the isolated Gluten worktree**

Follow the exact worktree and baseline commands in
`port/task/018-filecache-gluten-benchmark-plan.md`. Do not copy dirty changes
from `/root/oss/gluten`.

- [ ] **Step 3: Execute every Task-018 subtask in order**

Run `018-A -> 018-B -> 018-C/018-D -> 018-E -> 018-F -> 018-G -> 018-H`.
Correctness, content hash, and metric propagation gates must pass before baseline
performance collection.

- [ ] **Step 4: Run the Task-018 cross-repository review and acceptance gate**

Review the complete Velox and isolated-Gluten diffs together. Verify the
dedicated leaf pool, fail-before-allocation mutual exclusion, canonical file
identity, query token copy, every native/JNI/Java/Scala metric carrier, sentinel
cleanup, and benchmark result artifacts.

Expected: Task 018 is accepted in both repositories before Task 017B starts.

---

### Task 3: Execute and accept Task 017B

**Files:**
- Execute: `port/task/017b-filecache-logging-exception-stack-plan.md`
- Write: `port/task/result/017b-filecache-logging-exception-stack-result.md`
- Update after acceptance: `port/task/CONTROLLER_HANDOFF.md`

**Interfaces:**
- Consumes: the accepted Velox branch after Task 018
- Produces: lazy attributed FileCache logging, current-exception formatting, optional Velox stack output, and `noexcept` logging overloads

- [ ] **Step 1: Confirm Task 018 is accepted**

```bash
grep -n 'controller_status: accepted' \
  port/task/result/018-filecache-gluten-benchmark-result.md
```

Expected: accepted Controller review with no unresolved cross-repository finding.

- [ ] **Step 2: Execute every checkbox in the Task-017B plan**

Use `port/task/017b-filecache-logging-exception-stack-plan.md` as the complete contract.

- [ ] **Step 3: Run the Task-017B review and acceptance gate**

The Controller verifies lazy argument evaluation, logger attribution, exception
type/stack formatting, preservation of the original exception, real glog sink
evidence, CMake linkage, mutations, and mono/non-mono accumulated tests.

Expected: Task 017B is accepted with zero unresolved findings.

---

### Task 4: Open Task 019 design only

**Files:**
- Modify after Task 017B acceptance: `port/task/019-filecache-gluten-end-to-end.md`
- Create after Task 017B acceptance: a dedicated Task-019 design/implementation plan

**Interfaces:**
- Consumes: accepted Tasks 017A, 018, and 017B
- Produces: reviewed Spark end-to-end correctness/performance design; no implementation in this execution plan

- [ ] **Step 1: Verify all three prerequisite receipts**

```bash
for task in \
  017a-filecache-statistics-cancellation \
  018-filecache-gluten-benchmark \
  017b-filecache-logging-exception-stack; do
  grep -n 'controller_status: accepted' "port/task/result/${task}-result.md"
done
```

Expected: all three receipts are accepted and none has a later reopening.

- [ ] **Step 2: Start brainstorming and design for Task 019**

Task 019 must cover real Spark correctness and performance on the accepted
Gluten FileCache path. Stop after the reviewed design and executable plan; do not
implement Task 019 under this plan.
