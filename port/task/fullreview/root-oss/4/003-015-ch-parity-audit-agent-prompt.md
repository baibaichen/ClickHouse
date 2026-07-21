# Agent Prompt: Tasks 003-015 ClickHouse-to-Velox Parity Audit

Copy everything below into a fresh agent session.

---

You are the Controller for a **read-only source-contract parity audit** of the
accepted ClickHouse FileCache port through Task 015.

## Objective

Answer, with source and test evidence:

```text
Of the behavior actually promised by accepted Tasks 003-015, how much matches
ClickHouse exactly, how much is an equivalent Velox adaptation, how much is an
approved intentional deviation, how much is missing, and how much remains
unproven?
```

This is not a line-count or file-similarity exercise. Compare consumer-visible
behavior, state transitions, ownership, concurrency, failure semantics, and
required internal guarantees.

## Mandatory workflow skills

Before any exploration:

1. invoke `using-superpowers`;
2. invoke the ClickHouse `review` skill and follow its **Review Instructions**;
3. use `dispatching-parallel-agents` only for genuinely independent read-only
   research threads;
4. use `verification-before-completion` before declaring the audit complete.

Do **not** invoke implementation, TDD, or debugging skills unless this audit
actually runs a probe and the probe itself fails unexpectedly.

## Repository baselines

```text
ClickHouse repository: /root/oss/clickhouse
branch:                ch-filecache
expected HEAD:         a8f99d0d4f5ad3804e6a1693b5d8374865d0133c

Velox repository:      /root/oss/velox
branch:                filecache
expected HEAD:         43a9e6f75ffb94be38836b45fd476325665f50be

environment_profile:   root-oss
```

At startup, record the actual HEADs and status of both repositories. Stop if
either repository has unexpected source changes under the audit scope. Port
documentation changes in ClickHouse may be written only to the Review-4 output
paths listed below.

Gluten is outside this audit and has unrelated dirty work. Do not inspect,
modify, stage, clean, or reset it.

## Read first

```text
/root/chang/OneDrive/share_data/local-cache/filecache-port-authoring-guide.md

/root/oss/clickhouse/port/task/ENVIRONMENT.md
/root/oss/clickhouse/port/task/EXECUTION_PROTOCOL.md
/root/oss/clickhouse/port/task/CONTROLLER_HANDOFF.md

/root/oss/clickhouse/port/task/003-*.md through 015-*.md
/root/oss/clickhouse/port/task/result/003-*.md through 015-*.md

/root/oss/clickhouse/port/task/fullreview/root-oss/1/
/root/oss/clickhouse/port/task/fullreview/root-oss/2/
/root/oss/clickhouse/port/task/fullreview/root-oss/3/

/root/oss/clickhouse/port/design/
/root/oss/clickhouse/port/2-file-cache/
/root/oss/clickhouse/port/3-consumers/
```

Treat receipts, prior reviews, decisions, and tests as **evidence and declared
intent**, never as the behavior oracle. ClickHouse source and its real callers
are the behavior oracle.

## Scope

Audit the accepted implementation owned by Tasks 003-015:

```text
Velox:
  velox/ch/Common/
  velox/ch/IO/
  velox/ch/Interpreters/FileCache/
  velox/ch/Disks/IO/
  task-owned tests and CMake
  Task-015 E2E and seek benchmark

ClickHouse source-of-truth:
  src/Interpreters/FileCache/
  src/Disks/IO/ FileCache consumers
  src/Interpreters/TemporaryDataOnDisk only as a forward-consumer boundary
  all other real callers reached by the accepted Tasks 003-015 surfaces
```

Tasks 016-019 are **not** accepted implementation and are excluded from the
parity denominator:

```text
Task 016: deferred by user; no Velox temporary-data consumer.
Task 017: planned; observability/cancellation redesign pending.
Task 018: planned; Gluten + complete benchmark-suite redesign pending.
Task 019: blocked on Task 018.
```

Mention their consumer gaps in a separate forward-obligations section; do not
count them as defects in Tasks 003-015.

## Classification vocabulary

Every atomic contract row must receive exactly one primary status:

| Status | Meaning |
|---|---|
| `MATCH` | Same consumer-visible behavior and same relevant guarantees as CH. |
| `EQUIVALENT` | Different Velox primitive/shape, but proven equivalent behavior and guarantees. |
| `INTENTIONAL_DEVIATION` | Behavior or guarantee differs; the deviation is explicitly recorded and approved/conditionally accepted. |
| `MISSING` | An in-scope CH contract row has no Velox implementation. |
| `UNPROVEN` | Implementation appears present, but evidence cannot prove the contract. |
| `VELOX_EXTENSION` | Velox adds behavior not promised by the in-scope CH contract. |
| `OVER_PORT` | Behavior was ported without a real in-scope CH consumer. |

Do not hide an unapproved difference under `EQUIVALENT`. Infrastructure name
changes are equivalent only when their guarantees match.

## Required phases

### Phase A — CH-only consumer contract recovery

Dispatch a fresh read-only agent that may read **only ClickHouse source and CH
history**, not Velox implementation or Velox tests.

For every real caller reachable by Tasks 003-015:

1. enumerate every used method/overload;
2. recover state transitions and error behavior;
3. recover ownership/lifetime and cleanup;
4. recover concurrency, lock type, and lock ordering;
5. recover persistence/path/serialization guarantees;
6. recover setting defaults and unsupported combinations;
7. recover diagnostics whose text/order is a contract;
8. record internal structures whose guarantees affect future consumers.

Produce:

```text
port/task/fullreview/root-oss/4/evidence/003-015-ch-consumer-contract-ledger.md
```

Required columns:

```text
row_id
task_owner
surface
CH caller and file:line
signature/overload
preconditions
state transition
result
error behavior
ownership/lifetime
concurrency/lock guarantee
persistence/path guarantee
setting/default
```

Atomic rows must be fine-grained enough that one row can receive one parity
status. Do not combine several independent behaviors into one row merely to
inflate the matching percentage.

### Phase B — Velox implementation comparison

Only after Phase A is complete, dispatch a fresh read-only agent that reads the
CH ledger, current Velox implementation, task contracts, receipts, tests, and
mutation evidence.

For every CH ledger row:

1. locate the Velox implementation and cite current file:line;
2. locate focused mono/non-mono/E2E/mutation evidence;
3. classify it with the vocabulary above;
4. state whether guarantees, not just outputs, match;
   for a different container, lock, or state representation, verify the change
   appears in the prior structural-deviation ledger with user sign-off;
   an unregistered guarantee-changing replacement is `UNPROVEN` even if current
   output tests pass;
5. assign severity if status is `MISSING`, `UNPROVEN`, unapproved deviation,
   or unsafe `OVER_PORT`;
6. name the task that must reopen if corrective work is needed.

Also inventory Velox-only behavior and classify it as `VELOX_EXTENSION` or
`OVER_PORT`.

Produce:

```text
port/task/fullreview/root-oss/4/evidence/003-015-velox-parity-matrix.md
```

Required columns:

```text
row_id
task
surface
CH contract summary + citation
Velox implementation + citation
focused test evidence
mutation/RED evidence
status
guarantee difference
severity
action/reopen owner
```

### Phase C — independent Controller review

Generate a review package containing the two evidence files and the exact
accepted branch ranges. Dispatch one high-capability read-only reviewer that:

1. checks every `MATCH`/`EQUIVALENT` claim against source;
2. searches for missing callers/sibling paths;
3. checks state/failure/lifecycle divergence;
4. checks structural-deviation approvals;
5. checks that test evidence is freshly built rather than a stale executable;
6. checks the quantitative denominator and counts;
7. reports only Critical/Important corrections.

Fix the audit documents, not production code, then re-review until there are
zero Critical/Important review findings.

## Mandatory cross-cutting investigations

At minimum, audit these explicitly:

### API and structure

- every FileCache type/name/enum required by real callers;
- overload completeness;
- CH containers/iterator stability vs Velox replacements;
- lock types and order;
- mono/non-mono public-header and object-library closure.

### State and lifecycle

- `FileSegment` downloader election/release;
- `EMPTY`, `DOWNLOADING`, partial, downloaded, failed, detached transitions;
- holder destruction and last-holder cleanup;
- Manager/Factory creation, reload, clear, shutdown, and worker-pool ownership;
- opened-file handle lifetime beyond Manager lifetime.

### Read path

- miss/fill/hit;
- cache-only and bypass;
- seek, backup, skip, non-zero region coordinates;
- reader handoff and background completion;
- partial continuation and physical-size reconciliation;
- cache write failure and source recovery.

### Failure semantics

- real errno production vs typed errno consumption;
- ENOSPC/EDQUOT reconciliation;
- generic exceptions;
- retry-on-throw initialization;
- failpoints/TestValue release behavior;
- no fallback paths that hide failures.

### direct I/O

- CH defaults and real caller behavior;
- Velox foreground physical-round-up/logical-clamp extension;
- Velox background aligned-body/pure-tail policy;
- classify D3/D4 as conditional intentional deviations, not `MATCH`;
- record that strict mocks are logic coverage only;
- record real kernel `O_DIRECT` integration as `UNPROVEN` and a mandatory
  forward gate;
- distinguish non-direct-I/O behavior, which should be compared separately.

### Observability/cancellation

- accepted Tasks 003-015 no-op shim contract;
- do not count planned Task 017 behavior as a current missing implementation;
- identify any in-scope caller that already requires non-no-op behavior;
- record F-CALLERID and recursive-mutex decisions as Task-017 forward work.

### Consumers

- accepted FileCache read consumers;
- CH `TemporaryDataOnDisk` as an excluded/deferred Task-016 consumer;
- Gluten as excluded planned Tasks 018-019;
- complete benchmark suite as planned Task-018 work, not accepted parity.

## Approved and conditional decisions to audit

Read and verify every item in:

```text
port/task/fullreview/root-oss/2/003-014-controller-decisions-for-review.md
port/task/fullreview/root-oss/3/015-controller-decisions-for-review.md
port/task/fullreview/root-oss/2/003-014-review-decisions.md
```

Important current dispositions:

```text
D1/D2 Task-015 decisions: approved.
D3: conditional; real kernel O_DIRECT required.
D4: non-DIO accepted; DIO policy pending real O_DIRECT/parity decision.
D5: not finally decided; full benchmark design pending.
D6: approved.
Task 016: deferred.
Tasks 017/018: planned joint design, not implemented.
```

An item recorded only as `user_post_review` is not automatically an approved
deviation. Report it separately in the decision summary.

## Quantification rules

The user wants to know "how much matches CH." Define the denominator before
showing percentages.

Use **atomic in-scope CH consumer contract rows** from Phase A as the
denominator.

Report:

```text
semantic parity =
  (MATCH + EQUIVALENT) / all in-scope CH rows

accepted coverage =
  (MATCH + EQUIVALENT + approved INTENTIONAL_DEVIATION)
  / all in-scope CH rows
```

Also report raw counts for every status. Do not count:

- `VELOX_EXTENSION` in the CH denominator;
- Tasks 016-019 forward consumers;
- duplicate rows for the same behavior;
- compile-only names with no real CH caller.

Do not present a percentage without the row-count table and a link to the
matrix used to calculate it.

Break down counts by:

```text
Tasks 003-010
Task 011
Task 012
Task 013
Task 014
Task 015
cross-cutting
```

`cross-cutting` means shared infrastructure rows spanning at least three tasks
with no truthful single task owner, such as global CMake/header/ODR closure. Do
not move a row there merely because several tasks call one task-owned surface.

## Final outputs

Write:

```text
port/task/fullreview/root-oss/4/evidence/003-015-ch-consumer-contract-ledger.md
port/task/fullreview/root-oss/4/evidence/003-015-velox-parity-matrix.md
port/task/fullreview/root-oss/4/003-015-ch-parity-audit.md
port/task/fullreview/root-oss/4/003-015-parity-decisions-needed.md
```

Create `port/task/fullreview/root-oss/4/evidence/` and
`/root/oss/clickhouse/tmp/` before writing artifacts if they do not exist.

The final audit must contain:

1. exact baselines and scope;
2. methodology and denominator definition;
3. status counts and percentages;
4. per-task summary;
5. Critical/Important findings;
6. all intentional deviations and their approval state;
7. all `UNPROVEN` rows;
8. all Velox extensions/over-port;
9. required task reopen list;
10. Task-017/018 design inputs;
11. forward gates, especially real `O_DIRECT`;
12. final verdict:

```text
PARITY_ACCEPTED
PARITY_ACCEPTED_WITH_DECISIONS
PARITY_BLOCKED
```

The decisions-needed file must contain only items that require user choice,
each with:

```text
decision_id
source rows
plain-language question
option A
option B
review recommendation
risk of each option
```

## Hard constraints

- Read-only for all production code.
- Do not implement or fix findings.
- Do not modify ClickHouse `src/`, Velox, or Gluten.
- Do not stage, commit, push, rebase, amend, reset, or clean.
- Review artifacts may be created only under
  `port/task/fullreview/root-oss/4/`.
- Do not trust stale CTest binaries. If test execution is necessary, first build
  every selected target and redirect logs under the relevant build directory.
- Do not use `/tmp`; use `/root/oss/clickhouse/tmp/` for temporary artifacts.
- Do not use sleep in C++ or add skipped/disabled tests.
- Say "exception", not "crash", for logical errors.
- Stop and report `BLOCKED` if an atomic CH contract cannot be recovered from a
  real caller, rather than guessing.

## Completion report

Return only:

```text
status
artifact paths
baselines
row counts by status
semantic parity percentage
accepted coverage percentage
Critical/Important finding counts
tasks to reopen
decisions required
real O_DIRECT gate status
confirmation that no production code or Gluten files changed
```

Do not paste the full matrices into chat; the files are the durable handoff.

---
