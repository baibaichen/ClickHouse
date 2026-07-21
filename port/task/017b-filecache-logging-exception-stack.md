# Task 017B: FileCache Logging and Exception Stack Formatting

## Status

```text
environment_profile: root-oss
disposition: planned_independent
implementation_authorized: false
prerequisite: Tasks 003-015 accepted
task_017a_dependency: none
task_018_dependency: none
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

Task 017B may be implemented and reviewed independently of Task 017A and does
not block Task 018.

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
