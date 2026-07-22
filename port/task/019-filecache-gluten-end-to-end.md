# Task 019: Gluten `FileCache` Integration + Spark End-to-End — Index / Status / Ownership

> **Deferred Gluten task.** Do not dispatch during the Velox-only phase. Read
> `port/task/ENVIRONMENT.md` and `port/design/filecache-task-018-019-hard-split.md`
> first.

This document is the **index, status, and ownership record** for Task 019. The
complete executable implementation plan lives in:

```text
port/task/019-filecache-gluten-integration-spark-e2e-plan.md
```

## Status

```text
disposition: planned (blocked)
blocked_on:
  - 019-A compatible Velox baseline (hard gate; see below)
  - Task 017A accepted
  - accepted Velox-only Task 018
  - accepted Review 5 (Tasks 003-018 Velox whole-port review)
  - accepted Task 017B
plan: port/task/019-filecache-gluten-integration-spark-e2e-plan.md
worktree: /root/oss/gluten-019 (branch task-019-filecache-gluten), isolated
```

## Ownership

Task 019 is the **full Gluten integration owner** — not a small test-only
follow-up to an already-integrated Task 018. After the 2026-07-22 hard split
(`port/design/filecache-task-018-019-hard-split.md`), Task 018 is **Velox-only**
and does not build or modify Gluten. Every Gluten C++/JNI/Java/Scala change and
the Spark end-to-end validation belong to Task 019.

## Subtask map

The plan decomposes Task 019 into six subtasks (executed in order):

```text
019-A  Compatible Velox baseline for the selected Gluten commit (HARD GATE)
019-B  Gluten config + VeloxBackend/FileCacheManager lifecycle   (former 018-E)
019-C  GlutenBufferedInputBuilder selection + canonical identity + copied token (former 018-F)
019-D  fileCacheWriteBytes native/JNI/Java/Scala/Spark SQLMetric bridge (former 018-G)
019-E  Native Gluten Builder/lifecycle miss-fill-hit E2E GTest
019-F  Spark -> Gluten -> Velox -> FileCache correctness + performance E2E
```

Execution order: `019-A -> 019-B -> 019-C -> 019-D -> 019-E -> 019-F`.

## Prerequisites

Task 019 starts only after **all** of the following are accepted:

```text
Task 017A (statistics, cancellation, caller identity, scheduler parity)
Velox-only Task 018 (correctness harness + benchmarks; no Gluten)
Review 5 (Tasks 003-018 Velox whole-port review)
Task 017B (logging + exception stack formatting)
```

## Compatible Velox hard gate (019-A)

Task 019 must not build Gluten against a mismatched Velox merely because
`FileCache` compiles there. The selected Gluten commit `4017ec94d` is built
against its paired `IBM/velox` fork (`ep/build-velox/src/get-velox.sh`
names `dft-2026_07_03`; the public ref is tag
`refs/tags/dft-2026_07_03`),
while the accepted `FileCache` lives on `/root/oss/velox` branch `filecache`.
Building Gluten `cpp` against `filecache` fails to compile Gluten's own
operators — observed at `cpp/velox/operators/hashjoin/HashTableSerializer.cc`,
which calls `HashTable<T>::serializedSize` / `serializeTo` / `deserializeFrom`
that do not exist on the `FileCache` baseline. The observed missing Velox APIs
include:

```text
HashTable serialization (serializedSize / serializeTo / deserializeFrom)
OpaqueHashTable
Parquet session key
Iceberg constructor surface
```

019-A must identify or construct one Velox branch containing **both** the
accepted `FileCache` commits and every Velox API the selected Gluten baseline
requires, with **no** Gluten source fallback, feature removal, or out-of-scope
shim. It produces a reviewed compatibility contract
(`port/task/result/019a-compatible-velox-baseline-contract.md`) consumed by
019-B..019-F. If a compatible baseline cannot be constructed without a fallback
or feature deletion, 019-A stops the whole Task-019 pipeline and redispatches to
design — it stops explicitly instead of deferring the decision.

## Existing work in progress

The uncommitted former-018-E files in `/root/oss/gluten-018` are preserved as
Task-019 WIP; they are **not** accepted Task-018 changes. Before execution:

```text
move/rename the worktree and branch to Task-019 naming (git worktree move + git branch -m);
rebase and amend are forbidden — new commits only;
review the retained WIP against the 019-A compatible Velox baseline before building;
rerun all lifecycle tests, including the real cold FileCacheBufferedInput read.
```

## Worktree isolation and commit rule

Task 019 uses an isolated Gluten worktree (`/root/oss/gluten-019`). The original
dirty `/root/oss/gluten` (branch `main`) is never modified. The Worker never
stages or commits; the Controller reviews the complete Velox and isolated-Gluten
diffs together and commits accepted work.

## Explicit exclusions

```text
write-through cache
overcommit priority
new FileCache production behavior beyond the accepted Task 017A/018 surface
```

The Spark/Scala application-level integration suite is **no longer excluded** —
it is delivered by 019-F in the plan.
