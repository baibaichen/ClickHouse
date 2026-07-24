# Task 017B: FileCache Logging and Exception Stack Formatting

## Status

```text
environment_profile: root-oss
disposition: implementation_plan_ready
written_spec_review: approved
review_5_status: accepted
task_017b_authorized: true
implementation_authorized: true
implementation_plan_status: reviewed_executable
implementation_plan: port/task/017b-filecache-logging-exception-stack-plan.md
plan_review_receipt: port/task/fullreview/root-oss/5/017b-implementation-plan-review.md
prerequisite: Task 018 four-driver addendum and Review 5 accepted [DONE]
task_017a_dependency: none
execution_after: accepted Review 5 for Tasks 003-018 [DONE]
task_019_dependency: Task 019 implementation requires accepted Task 017B
authorization_date: 2026-07-24
```

Binding design:

```text
port/design/filecache-task-017b-logging-exception-stack.md
```

The binding design was reviewed and approved by the user on 2026-07-24. The reviewed executable
implementation plan is at:

```text
port/task/017b-filecache-logging-exception-stack-plan.md
```

The plan was independently reviewed twice. Initial review (3 Critical / 2 Important / 5 Minor)
rejected the plan. All Critical, Important, and Minor findings were corrected. The re-review
returned 0 Critical, 0 Important, 2 cosmetic minors, both subsequently corrected. The durable
review receipt is at:

```text
port/task/fullreview/root-oss/5/017b-implementation-plan-review.md
```

Controller authorization of `implementation_authorized: true` was recorded on 2026-07-24.
A fresh Task 017B Worker may begin implementation by following the reviewed plan verbatim.
The Worker must write its result receipt with `worker_status: ready_for_controller`, then stop.
No Task 017B implementation has been performed yet.

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
