# Task 018: `FileCache` Velox Correctness and Benchmark Suite

## Status

```text
environment_profile: root-oss
disposition: implementation_in_progress
scope: Velox only
implementation_authorized: true
authorization_scope: non-TPCH phase through 018-P
tpch_authorized: true
h1_checkpoint_status: accepted
next_subtask: 018-C
prerequisite: accepted Task 017A at a856d836c
execution_before: Review 5, then Task 017B, then Task 019
implementation_plan: port/task/018-filecache-gluten-benchmark-plan.md
result: port/task/result/018-filecache-velox-benchmark-result.md
```

The filename is retained for handoff compatibility, but Task 018 no longer owns
or modifies Gluten. The hard split is binding:

```text
port/design/filecache-task-018-019-hard-split.md
```

## Scope

Task 018 owns:

```text
018-A byte-exact direct/CBI/FCBI correctness;
018-B FileCache core and FileCacheBufferedInput micros plus wrapper A/B;
018-D sentinel-safe benchmark orchestration;
018-H1 non-TPCH RelWithDebInfo/Release baseline waves;
018-P mandatory user checkpoint;
018-C TPCH result correctness after approval;
018-H2 TPCH baseline performance after correctness.
```

Current accepted Velox commits:

```text
9850a70fa  018-A correctness harness
df9091e78  018-B FileCacheBufferedInput micro
5ae39651b  018-D safe orchestration
```

## Binding constraints

```text
No Gluten, JNI, Java, Scala, or Spark build/change belongs to Task 018.
Every benchmark binary is freshly built and run with RelWithDebInfo or Release.
Debug benchmark evidence is invalid.
No TPCH source copy/build/data inspection/run occurs before explicit 018-P
approval.
Correctness precedes performance.
TPCH correctness uses one driver and one split per file; performance may use
multiple drivers but keeps one split per file.
```

## Moved to Task 019

Task 019 is the full Gluten integration owner:

```text
compatible Velox baseline;
VeloxBackend/FileCacheManager config and lifecycle;
GlutenBufferedInputBuilder selection and cancellation token;
native/JNI/Java/Scala/Spark metric bridge;
native Gluten E2E;
Spark -> Gluten -> Velox -> FileCache E2E.
```
