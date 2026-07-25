# Task 018S Implementation Plan Review

## Status

```text
review_date: 2026-07-25
environment_profile: root-oss
plan: port/task/018s-filecache-tpch-buffered-input-isolation-plan.md
binding_design: port/design/filecache-tpch-buffered-input-performance-investigation.md
velox_baseline: 0c5b5918eb8374f39b09248be091da94bf4d72f0
review_status: approved
plan_status: reviewed_executable
critical_findings: 0
important_findings: 0
implementation_authorized: false
execution_authorized: false
```

## Review history

The initial executable-plan review found eight Important issues:

1. immutable `ConfigBase` fixture assignment;
2. smoke samples incompatible with the normal 3+2 analyzer contract;
3. missing performance-build directory in `ENVIRONMENT.md`;
4. instrumentation on/off validation covered only q04;
5. positive actual-path probe metrics were not mandatory;
6. `io_wait_ms` was not comparable across A/B/C;
7. FileCache sample cleanup was deferred until the whole runner exited;
8. automatic layer classification lacked a reviewed materiality rule.

The first re-review found seven remaining Important issues:

1. connector-session helper qualification/string conversion;
2. test `MemoryManager` initialization;
3. incomplete `IoStatistics::merge` proof;
4. no Direct planned/unplanned probe wiring test;
5. incomplete per-sample identity schema and mismatch fixtures;
6. receipt template missing the Task-018S checkpoint state;
7. non-executable build/test command prose.

The second re-review found five remaining Important issues:

1. passthrough remained selectable through a production Hive session property;
2. nullable statistics could be dereferenced;
3. the merge test did not prove two nonempty ledgers combine;
4. the Python unit-test command treated a path as a module;
5. one C++ snippet violated Allman-style braces.

The final pre-approval review found three remaining documentation gaps:

1. the architecture summary retained the superseded session-gated passthrough
   wording and lacked a property-only negative test;
2. metadata negative fixtures did not individually cover block/sample/probe,
   schema version, and ClickHouse identity;
3. `BackUp` fast-return `Next` accounting lacked a binding test.

All findings were corrected in place.

## Final review evidence

```text
passthrough selection:
  process-scoped C++ RAII benchmark/test override only
  no user-configurable Hive passthrough property

metrics selection:
  connector session property controls default-off metrics only
  null-safe A/B/C recording
  merge combines nonempty ledgers and enables a disabled destination

path evidence:
  Direct planned and unplanned streams covered
  FCBI cache and passthrough streams covered
  BackUp fast-return and new-chunk Next branches covered

CSV:
  exact 32 fields / 31 commas
  process, storage/local I/O, probe, and passthrough deltas

runner:
  normal 3+2, smoke, and probe-validation modes
  exact binary/build/repository/dataset/settings identity
  immediate per-FileCache-sample sentinel cleanup

execution:
  one-driver mandatory checkpoint
  fresh Worker and explicit approval before four drivers
  no automatic root-cause/materiality classification
  no full 22-query run or Task 017B implementation

final unresolved findings:
  none
```

## Decision

The plan is complete and executable. Execution remains unauthorized until the
user explicitly approves Task 018S implementation.
