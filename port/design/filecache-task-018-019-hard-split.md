# `FileCache` Task 018/019 Hard Split Design

## Status

```text
decision_date: 2026-07-22
decision: approved
task_018_scope: Velox-only correctness and benchmark work
task_019_scope: Gluten integration and Spark end-to-end work
```

## 1. Reason for the split

Task 018 originally combined two independent systems:

1. Velox-native correctness and benchmark binaries;
2. Gluten lifecycle, Builder, JNI/Java/Scala metrics, and Spark integration.

The Velox TPCH benchmark does not run through Spark or Gluten. During 018-E,
the Gluten build also exposed a separate baseline dependency: Gluten
`4017ec94d` expects APIs from its paired IBM/internal Velox fork, while the
accepted FileCache implementation lives on `/root/oss/velox` branch
`filecache`. This Gluten compatibility issue must not block Velox-native
benchmark work.

## 2. Task 018 — Velox only

Task 018 owns:

```text
018-A: byte-exact direct/CBI/FCBI correctness harness;
018-B: dedicated FileCacheBufferedInput micro and wrapper A/B smoke;
018-D: sentinel-safe benchmark orchestration;
018-H1: non-TPCH RelWithDebInfo/Release performance waves;
018-P: mandatory user checkpoint before TPCH;
018-C: TPCH result correctness after approval;
018-H2: TPCH baseline performance after correctness.
```

Task 018 does not modify or build Gluten.

Task 018 order:

```text
018-A -> 018-B -> 018-D -> 018-H1
  -> STOP at 018-P
  -> explicit user approval
  -> 018-C -> 018-H2
```

Every benchmark binary is freshly built and run in RelWithDebInfo or Release.
Debug benchmark evidence is invalid.

## 3. Review 5 — Velox whole-port review

After Task 018 is accepted, Review 5 reviews Tasks 003–018 as a Velox
FileCache system and closes or dispositions Review-4 debt.

Review 5 does not claim to review Gluten integration, because that work now
belongs to Task 019 and has not yet been accepted.

Task 017B remains after accepted Review 5.

## 4. Task 019 — Gluten integration

Task 019 owns the former 018-E/F/G work plus Gluten/Spark end-to-end
validation:

```text
019-A: establish a Velox baseline compatible with the selected Gluten commit
       and replay/merge the accepted FileCache implementation onto it;
019-B: Gluten configuration and VeloxBackend/FileCacheManager lifecycle
       (former 018-E);
019-C: GlutenBufferedInputBuilder selection, canonical identity, statistics
       objects, and copied cancellation token (former 018-F);
019-D: fileCacheWriteBytes native/JNI/Java/Scala/Spark SQLMetric bridge
       (former 018-G);
019-E: native Gluten Builder/lifecycle miss-fill-hit end-to-end tests;
019-F: Spark -> Gluten -> Velox -> FileCache correctness and performance E2E.
```

Task 019 starts only after Task 017B is accepted.

## 5. Compatible Velox prerequisite

Task 019 must not use a mismatched Velox merely because FileCache compiles
there. Before Gluten implementation resumes, 019-A must identify or construct
one Velox branch containing both:

```text
the accepted FileCache commits through Task 017A/018;
the Velox APIs required by the selected Gluten baseline.
```

For Gluten `4017ec94d`, the currently observed missing APIs include HashTable
serialization, `OpaqueHashTable`, a Parquet session key, and the expected
Iceberg constructor surface.

No Gluten source fallback, feature removal, or out-of-scope compatibility shim
may be used to hide a mismatched Velox baseline.

## 6. Current Gluten worktree

The uncommitted former-018-E files in `/root/oss/gluten-018` are preserved as
Task-019 work in progress. They are not accepted Task-018 changes.

Before Task 019 resumes:

```text
rename/move the worktree and branch to Task-019 naming;
rebase is forbidden;
retain the existing WIP only after reviewing it against the compatible Velox
baseline;
rerun all lifecycle tests, including the real cold FileCacheBufferedInput read.
```

## 7. Updated mainline order

```text
Task 017A (accepted)
  -> Task 018 Velox-only non-TPCH
  -> 018-P user checkpoint
  -> Task 018 TPCH
  -> Review 5 (Tasks 003-018 Velox)
  -> Task 017B
  -> Task 019 Gluten integration + Spark E2E
```

Task 019 is no longer described as a small test-only follow-up to an already
integrated Task 018. It is the full Gluten integration owner.
