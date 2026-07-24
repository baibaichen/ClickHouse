# Task 017A: FileCache Statistics, Cancellation, and Scheduler Parity

## Status

```text
environment_profile: root-oss
disposition: accepted
implementation_authorized: true
authorization_date: 2026-07-21
controller_status: accepted
accepted_velox_head: a856d836c
result: port/task/result/017a-filecache-statistics-cancellation-result.md
prerequisite: Tasks 003-015 accepted
task_016_dependency: none
task_018_dependency: Task 018 requires accepted Task 017A
task_017b_order: Task 017B executes after the Task 018 four-driver addendum and accepted Review 5
implementation_plan: port/task/017a-filecache-statistics-cancellation-plan.md
```

Binding design:

```text
port/design/filecache-task-017-018-joint-design.md
```

The executable contract is:

```text
port/task/017a-filecache-statistics-cancellation-plan.md
```

It passed independent plan review. Do not implement until the Controller records
explicit authorization; after authorization, the plan supersedes this index.

## Scope

Task 017A owns:

```text
real CurrentMetrics and ProfileEvents storage;
the FileCache global stats snapshot/delta provider;
FileCacheInputStream global + query-level statistics wiring;
value-semantic cancellation-token propagation;
safe cancellation checkpoints and the existing FileSegment wait token;
CH-compatible caller identity;
CH-compatible two-plain-lock scheduler structure;
focused mono/non-mono tests and accumulated rebuild-before-CTest gates.
```

## Binding deferred debts

### Task-014 statistics

`FileCacheBufferedInput` stores `IoStatistics`/`IoStats`, but
`FileCacheInputStream` updates neither them nor the CH reader events. A single
completed read/write fact must update both the global and query ledgers.

### Task-014 cancellation

Task 012's `FileSegment::wait` accepts a real token, but Task 014 passed an
empty token. Task 017A owns the Velox-side value propagation and tests; Task
018 supplies `ConnectorQueryCtx::cancellationToken` at the Builder boundary.

### Task-006 scheduler

The recursive mutex is an implementation-induced workaround. Task 017A
restores CH's `schedule_mutex`/`exec_mutex` model and attaches Folly timer
continuations outside the scheduling lock.

## Exclusions

```text
logger implementation and exception stack formatting (Task 017B);
Velox correctness and benchmarks (Task 018);
Gluten configuration/Builder/lifecycle/metrics and Spark E2E (Task 019);
real kernel O_DIRECT integration (deferred);
Task 016 Ephemeral writer.
```
