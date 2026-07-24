# Task 017B: FileCache Logging and Exception Stack Formatting

## Status

```text
environment_profile: root-oss
disposition: design_approved_plan_revision_required
review_5_status: accepted
task_017b_authorized: true
implementation_authorized: false
prerequisite: Task 018 four-driver addendum and Review 5 accepted [DONE]
task_017a_dependency: none
execution_after: accepted Review 5 for Tasks 003-018 [DONE]
task_019_dependency: Task 019 implementation requires accepted Task 017B
implementation_plan_status: stale_do_not_execute
implementation_plan: port/task/017b-filecache-logging-exception-stack-plan.md
```

Binding design:

```text
port/design/filecache-task-017b-logging-exception-stack.md
```

There is no executable Task 017B contract yet. The previous plan at:

```text
port/task/017b-filecache-logging-exception-stack-plan.md
```

is stale and must not be executed. It predates the approved lazy-gate,
`LOG_TEST`→`VLOG(3)`, ownership-copy, and emergency-logging decisions. Rewrite
and independently review it from the binding design before requesting
implementation authorization.

## Scope

Task 017B owns only:

```text
real lazy FileCache logging behind logger_useful.h;
LOG_TEST/trace/debug lazy VLOG formatting;
info/warning/error native glog severity;
zero evaluation for every filtered level;
zero LoggerPtr ownership copy in enabled macros;
logger attribution;
current Velox/std/unknown exception formatting;
optional Velox exception stack output;
LoggerPtr and function/log-name tryLogCurrentException overloads;
noexcept behavior that never replaces the original exception;
emergency stderr diagnostics when exception logging itself fails;
focused mono/non-mono tests and mutation evidence.
```

Task 017B is independent of Task 017A and does not block Task 018. Per user
decision it is executed after the accepted Task 018 four-driver addendum and
Review 5, and becomes a mandatory gate before Task 019 implementation and
production readiness.

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
