# Tasks 003-014 Targeted Review-2 Closure — B2-B5

## Status

```text
environment_profile: root-oss
review_scope: Review-2 findings B2-B5 only
velox_head: ad1a13c37e87cecda464ac8dfcc9fee57c093eb6
clickhouse_head: fc8abbe1abe
unresolved_findings: 0
review_status: approved
task_015_allowed: true
```

The targeted review read the corrective tests, the unchanged production
invariants they exercise, and the Task 011/012/014 receipts. It did not reopen
already-accepted Tasks 003-014 rows outside B2-B5.

## Closure

| Finding | Result | Evidence |
|---|---|---|
| B2 — `MoveEvictionPos` | closed | The global-friend test has its own executable, moves both cursors independently, and fails when the cursor update is removed. |
| B3 — SLRU rollback/resize | closed | The production failpoint reaches rollback on unwind; dynamic resize requires both sub-queues; both mutations fail for the declared reason. |
| B4 — reader reset-before-complete | closed | The three-barrier test proves the ownership windows; all `Metadata` and `FileCacheInputStream` callers preserve ordering, including the intentional reusable-reader handoff branch. |
| B5 — SCC queue pipeline | closed | The SCC binary exercises the real bounded queue's timed push, non-blocking FIFO pop, blocking pop, and finish path; the call-shape mutation times out. |

## Evidence

```text
Task 011:
  priority cursor: 1/1 mono and 1/1 non-mono
  core SCC after B3: 103/103 mono and 103/103 non-mono
  accumulated mono CTest: 14/14

Task 012:
  focused B4/B5: 2/2 mono and 2/2 non-mono
  core SCC after B4/B5: 105/105 mono and 105/105 non-mono
  accumulated mono CTest: 14/14

failed/skipped/disabled: 0/0/0
corrective production changes: 0
```

The B4 review confirmed the source-truth correction recorded in Task 012: a
full completion destroys `download_data` before publishing `DOWNLOADED`, so the
reversed-order mutation must use a partial download and detect the retained
reader through shared ownership rather than a non-null terminal extraction.

## Disposition

B1 remains deferred to Task 015 exactly as approved. The targeted review found
zero unresolved B2-B5 findings. The user's instruction, "好，继续；直到做完 task
015", supplies the explicit approval required by the Review-2 gate.

```text
TARGETED REVIEW APPROVED
task_015_allowed: true
```
