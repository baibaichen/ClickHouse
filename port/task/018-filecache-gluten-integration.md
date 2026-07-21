# Task 018: `FileCache` Gluten Integration and Velox Benchmark Suite

## Status

```text
environment_profile: root-oss
disposition: implementation_plan_ready
implementation_authorized: false
prerequisite: accepted Task 017A
execution_after: Task 017A
execution_before: Review 5, then Task 017B
task_019_dependency: Task 019 design waits for accepted Task 017B
implementation_plan: port/task/018-filecache-gluten-benchmark-plan.md
```

Binding design:

```text
port/design/filecache-task-017-018-joint-design.md
```

The executable contract is:

```text
port/task/018-filecache-gluten-benchmark-plan.md
```

It passed source-level and independent plan review. The plan supersedes the old
Task-018 contract. Do not implement until Task 017A is accepted and the
Controller records explicit authorization.

After Task 018 is accepted, stop implementation dispatch and run Review 5 over
Tasks 003–018. Task 017B remains blocked until that review is accepted.

## Scope

Task 018 owns:

```text
Velox byte-level `CacheVerify`;
FileCache core and `FileCacheBufferedInput` microbenchmarks;
same-process direct/CBI/FCBI wrapper A/B;
Velox TPCH correctness and baseline performance;
Gluten configuration and `FileCacheManager` lifecycle ownership;
canonical `GlutenBufferedInputBuilder` selection and cancellation propagation;
`fileCacheWriteBytes` propagation through native, JNI, Java, Scala, and
Spark `SQLMetric`;
safe benchmark orchestration and result receipts.
```

## Binding constraints

```text
Use an isolated Gluten worktree; never modify the dirty `/root/oss/gluten`
working tree.
Use a dedicated leaf `MemoryPool` for FileCache allocations.
Reject simultaneous AsyncDataCache/FileCache configuration before either cache
is initialized.
Use `FileCacheFileIdentity::deriveKey`; do not duplicate path/etag hashing.
Correctness gates run before performance.
TPCH uses one split per file for mode fairness.
The first performance wave establishes a baseline/noise band and has no hard
percentage threshold.
Worker does not stage or commit; Controller owns acceptance commits.
```

## Exclusions

```text
Task 017B logging and exception stacks;
Task 019 Spark end-to-end design or implementation;
Task 016 ephemeral writer;
real kernel `O_DIRECT` integration;
a Gluten HTTP or Prometheus reporter.
```
