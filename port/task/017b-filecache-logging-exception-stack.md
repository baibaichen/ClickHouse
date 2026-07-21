# Task 017B: FileCache Logging and Exception Stack Formatting

## Status

```text
environment_profile: root-oss
disposition: planned_independent
implementation_authorized: false
prerequisite: Tasks 003-015 accepted
task_017a_dependency: none
execution_after: accepted Task 018
task_019_dependency: Task 019 design requires accepted Task 017B
```

Binding design:

```text
port/design/filecache-task-017-018-joint-design.md
```

This task must be rewritten into a complete executable contract and reviewed
before implementation.

## Scope

Task 017B owns only:

```text
real lazy FileCache logging behind logger_useful.h;
LOG_TEST non-evaluation;
trace/debug/info lazy formatting;
warning/error logger attribution;
current Velox/std/unknown exception formatting;
optional Velox exception stack output;
LoggerPtr and function/log-name tryLogCurrentException overloads;
noexcept behavior that never replaces the original exception;
focused mono/non-mono tests and mutation evidence.
```

Task 017B is independent of Task 017A and does not block Task 018. Per user
decision it is executed after Task 018 and becomes a mandatory gate before
Task 019 design and production readiness.

## Exclusions

```text
CurrentMetrics/ProfileEvents storage;
IoStatistics/IoStats wiring;
cancellation;
caller identity;
scheduler locks;
Gluten integration;
benchmarks;
Spark end-to-end work.
```
