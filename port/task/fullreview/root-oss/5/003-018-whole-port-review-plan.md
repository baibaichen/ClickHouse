# Tasks 003–018 Whole-Port Review Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Review the accepted Tasks 003–018 implementation as one `FileCache` system, close or disposition Review-4 debt, and decide whether Task 017B may start.

**Architecture:** Freeze exact ClickHouse, Velox, and isolated-Gluten baselines after Task 018. First reconcile every unresolved Review-4 decision/finding against the accepted implementation, then trace Task-017A statistics/cancellation/scheduler contracts through Task-018 Gluten lifecycle, Builder, metrics, correctness, and benchmark consumers. An independent reviewer validates the evidence and verdict.

**Tech Stack:** Git, ClickHouse and Velox source-contract analysis, C++/CMake, Folly, Gluten C++/JNI/Java/Scala, GTest, CTest, Maven/ScalaTest, benchmark artifacts.

## Global Constraints

- Run only after accepted Task 018 and before Task 017B.
- Environment profile is `root-oss`; never mix profile paths.
- Review CH production source and real callers as the behavior oracle.
- Read all task-owned tracked and untracked files; default `git diff` is insufficient.
- Do not modify implementation while gathering evidence.
- Any corrective implementation uses a separately reopened numbered task, a fresh Worker, and new commits; never amend or rebase.
- Real kernel `O_DIRECT` is deferred and non-blocking, but no real-`O_DIRECT` claim is permitted.
- Task 017B remains blocked until this review records `review_status: accepted`.

---

### Task 1: Freeze baselines and evidence

**Files:**
- Create: `port/task/fullreview/root-oss/5/evidence/003-018-baselines.md`
- Read: `port/task/result/*-result.md`
- Read: `port/task/fullreview/root-oss/4/003-015-ch-parity-audit.md`

**Interfaces:**
- Consumes: accepted implementation and receipt commits through Task 018
- Produces: immutable baseline table for all three repositories

- [ ] **Step 1: Record repository state**

```bash
git -C /root/oss/clickhouse --no-pager status --short --branch
git -C /root/oss/clickhouse --no-pager log -5 --oneline
git -C /root/oss/velox --no-pager status --short --branch
git -C /root/oss/velox --no-pager log -5 --oneline
git -C /root/oss/gluten-018 --no-pager status --short --branch
git -C /root/oss/gluten-018 --no-pager log -5 --oneline
```

Expected: exact accepted Task-018 SHAs are identifiable and no unexplained task-owned changes remain.

- [ ] **Step 2: Write the baseline evidence file**

Record repository, branch, accepted HEAD, dirty files, accepted receipt, and
build/test/benchmark log locations. Stop if any receipt or Git state disagrees.

---

### Task 2: Close Review-4 debt

**Files:**
- Create: `port/task/fullreview/root-oss/5/evidence/review-4-closure.md`
- Read: `port/task/fullreview/root-oss/4/003-015-parity-decisions-needed.md`
- Read: `port/task/fullreview/root-oss/4/003-015-parity-user-decisions.md`
- Read: `port/task/fullreview/root-oss/4/evidence/003-015-velox-parity-matrix.md`

**Interfaces:**
- Consumes: every Review-4 `UNPROVEN`, pending decision, and evidence debt
- Produces: row-by-row verified disposition without silently changing classifications

- [ ] **Step 1: Re-evaluate every unresolved Review-4 item**

For each item, record current implementation SHA, exact source/test evidence,
user decision, and one of: `closed`, `accepted_deviation`, `forward_deferred`,
`reopen_task`, or `waiting_for_user`.

- [ ] **Step 2: Reopen corrective owners where required**

Approved but absent implementation must reopen its original task owner. Stop
Review 5 while the fresh Worker implements, tests, reviews, and receives
Controller acceptance; then resume from the frozen updated baseline.

- [ ] **Step 3: Reclassify only verified affected rows**

Do not recompute unaffected rows. Record old status, new status, reason, and
evidence for every changed parity row.

---

### Task 3: Review Task 017A and Task 018 end to end

**Files:**
- Create: `port/task/fullreview/root-oss/5/evidence/017a-018-contract-ledger.md`
- Create: `port/task/fullreview/root-oss/5/evidence/017a-018-parity-matrix.md`

**Interfaces:**
- Consumes: Task-017A global/query statistics, cancellation, caller identity, scheduler APIs
- Consumes: Task-018 Velox benchmarks and Gluten lifecycle/Builder/metric carriers
- Produces: integrated contract and evidence matrix

- [ ] **Step 1: Trace statistics facts**

Trace cache hits, source reads, predownloads, cache writes, logical bytes, and
timings from their completion points through `ProfileEvents`,
`IoStatistics`/`IoStats`, `RuntimeMetric`, JNI, Java, Scala, and Spark
`SQLMetric`. Require one failing mutation per independent carrier.

- [ ] **Step 2: Trace cancellation and ownership**

Verify copied token ownership, safe cancellation points, `FileSegment::wait`,
downloader lease completion, query-pool lifetime, Builder lifetime, Manager
shutdown, dedicated leaf pool lifetime, and executor ordering.

- [ ] **Step 3: Trace correctness and benchmark claims**

Verify byte/content hashes before performance, direct/CBI/FCBI fairness,
one-split TPCH runs, cache cleanup sentinels, result artifacts, and the explicit
absence of a hard first-wave performance threshold. Verify that every benchmark
binary was freshly built in RelWithDebInfo or Release, no Debug benchmark result
was accepted, and the pre-TPCH receipt proves no TPCH source copy/build/run
occurred before explicit user approval.

---

### Task 4: Independent review and verdict

**Files:**
- Create: `port/task/fullreview/root-oss/5/003-018-whole-port-review.md`
- Create: `port/task/fullreview/root-oss/5/003-018-review-decisions-needed.md`

**Interfaces:**
- Consumes: all Review-5 evidence
- Produces: final accepted/blocked verdict and the exact next dispatch

- [ ] **Step 1: Dispatch one read-only independent reviewer**

Give the reviewer the frozen baselines, CH contracts, Review-4 closure,
Task-017A/018 matrices, complete diffs, receipts, and log summaries. Require
contract, impacted-surface, failure/lifecycle, evidence, and false-green gates.

- [ ] **Step 2: Resolve every actionable finding**

Implementation findings reopen their numbered task. Decision findings stop at
`waiting_for_user`. Rerun only affected evidence after accepted corrections.

- [ ] **Step 3: Write the verdict**

The main review file must contain:

```text
review_status: accepted | blocked | waiting_for_user
review_scope: Tasks 003-018
review_4_closure_status:
critical_findings:
important_findings:
unproven_rows:
task_017b_authorized: true | false
```

Expected: Task 017B starts only when `review_status: accepted` and
`task_017b_authorized: true`.
