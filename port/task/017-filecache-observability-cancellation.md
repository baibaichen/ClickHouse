# Task 017 Split Index: FileCache Production Hardening

## Status

```text
environment_profile: root-oss
disposition: superseded_by_split
implementation_authorized: false
```

The former combined observability/cancellation/logging task is superseded by:

```text
Task 017A:
  port/task/017a-filecache-statistics-cancellation.md

Task 017B:
  port/task/017b-filecache-logging-exception-stack.md
```

Binding design:

```text
port/design/filecache-task-017-018-joint-design.md
```

Task 017A owns statistics, cancellation, caller identity, and scheduler lock
parity. Task 017B independently owns logging and exception stack behavior.
Task 018 depends only on accepted Task 017A. Task 017B runs after Task 018 and
must be accepted before Task 019 design.

Do not implement from the historical content of this file.
