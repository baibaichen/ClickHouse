# Task 017B FileCache Logging and Exception Stack Design

## Status

```text
decision_date: 2026-07-24
decision: approved
written_spec_review: pending
selected_architecture: glog backend plus FileCacheLogger name-holder
implementation_authorized: false
prerequisite: accepted Task 018 four-driver addendum and Review 5
successor: Task 019
```

Task 017B replaces the first-phase no-op logging shim. It does not begin until
Review 5 is accepted and the Controller explicitly authorizes implementation.

## Problem

`velox/ch/Common/logger_useful.h` currently preserves the ClickHouse
`FileCacheLogger`, `LoggerPtr`, `getLogger`, and `LOG_*` call shapes, but every
log macro and exception helper is a no-op. This was sufficient to compile the
initial FileCache port, but it has three production gaps:

1. FileCache diagnostics are discarded.
2. `getCurrentExceptionMessage` cannot describe an active exception.
3. `tryLogCurrentException` cannot record failures from initialization,
   background maintenance, or destructor cleanup.

Replacing the shim must not introduce hidden work in filtered logging paths.
Some FileCache log arguments construct paths, format cache state, or inspect
metadata. Evaluating those expressions when glog will discard the record would
create avoidable work in storage hot paths.

## Goals

```text
real FileCache logging through Velox's glog backend;
logger-name attribution without changing existing callers;
zero logger/format/argument evaluation for filtered records;
no shared_ptr ownership copy in the enabled macro path;
safe formatting of the currently handled exception;
best-effort exception logging that never changes control flow;
focused mono and non-mono proof, including mutation evidence.
```

## Non-goals

```text
a new logger backend or registry;
Poco logger compatibility;
ClickHouse system.text_log structured arguments;
runtime log-level configuration owned by FileCache;
exception deduplication or markAsLogged;
metrics, cancellation, scheduler, Gluten, Spark, or benchmark changes.
```

## Selected Architecture

Keep `FileCacheLogger` as an immutable name-holder and use glog as the only
backend:

```text
existing FileCache caller
        |
        v
LOG_* compatibility macro
        |
        +-- filtered -> stop before evaluating logger or message
        |
        `-- enabled -> bind logger once -> fmt formatting -> glog
```

`FileCacheLogger` does not gain virtual methods, sinks, severity state, or
backend ownership. `LoggerPtr` and `getLogger` remain compatibility surfaces so
the imported FileCache code does not need a broad mechanical rewrite.

The implementation is split by responsibility:

```text
logger_useful.h
  FileCacheLogger, LoggerPtr, getLogger
  compile-time formatting helper
  lazy LOG_* macros
  exception-helper declarations

logger_useful.cpp
  internal exception_ptr formatter
  current-exception formatting
  tryLogCurrentException overloads
  glog exception emitter
  emergency stderr writer
```

## Logging Levels

The compatibility levels map to native Velox conventions:

| Compatibility macro | Backend | Default state |
|---|---|---|
| `LOG_TEST` | `VLOG(3)` | disabled |
| `LOG_TRACE` | `VLOG(2)` | disabled |
| `LOG_DEBUG` | `VLOG(1)` | disabled |
| `LOG_INFO` | `LOG(INFO)` | enabled |
| `LOG_WARNING` | `LOG(WARNING)` | enabled |
| `LOG_ERROR` | `LOG(ERROR)` | enabled |

`LOG_TEST` is not a permanent no-op. It represents ClickHouse's level below
TRACE and becomes available through `--v=3`, while remaining zero-cost with
respect to its caller expressions at the default verbosity.

## Lazy-Evaluation Contract

A record is enabled only if every backend gate agrees:

1. `GOOGLE_STRIP_LOG` has not compiled out the severity.
2. `FLAGS_minloglevel` permits the glog severity.
3. For `VLOG`, `VLOG_IS_ON(level)` is true.

Checking only `VLOG_IS_ON` is insufficient. For example, `--v=3` together with
`--minloglevel=1` enables the verbose site but suppresses its underlying INFO
record. The compatibility macro must not format that discarded record.

The gate executes before all caller expressions. When a record is filtered:

```text
logger expression: not evaluated
format expression: not evaluated
format arguments:  not evaluated
```

When a record is enabled:

```text
logger expression: evaluated exactly once
format expression: evaluated exactly once
format arguments:  evaluated exactly once
```

After the gate, the logger expression is bound with `auto&&`. This avoids the
atomic reference-count increment/decrement caused by copying an lvalue
`shared_ptr`, while extending the lifetime of a temporary `LoggerPtr` through
the macro body. The required behavior is no ownership copy; `auto&&` is the
selected implementation mechanism rather than a public API requirement.

The emitted message is:

```text
[logger-name] formatted message
```

A null `LoggerPtr` is attributed as `[null]`.

## Formatting Contract

The imported FileCache calls use ClickHouse-style `{}` placeholders. Velox
already depends on `fmt`, so the compatibility layer uses:

```text
fmt::format_string for compile-time format checking;
fmt::format for enabled records;
a one-message overload for already formatted strings.
```

ClickHouse's logger retains structured format arguments for Poco channels and
`system.text_log`. Velox has no equivalent backend in this port, so retaining
those argument vectors is intentionally out of scope.

Ordinary `LOG_*` calls are not `noexcept`. Exceptions from caller expressions
or `fmt` formatting propagate normally. The special containment contract
applies only to `tryLogCurrentException`.

## Current-Exception Formatting

`getCurrentExceptionMessage` is `noexcept`. It captures
`std::current_exception` exactly once and delegates to an internal formatter
that accepts the resulting `exception_ptr`. It never uses a bare `throw`.

Behavior is:

| Current state | Result |
|---|---|
| no active exception | empty string |
| `VeloxException` | source, code, and message |
| `std::exception` | exception category and `what` text |
| other exception | deterministic unknown-exception text |

For `VeloxException`, stack text is appended only when:

1. `withStackTrace` is true; and
2. the exception already owns a captured `StackTrace`.

The logging layer does not collect a new stack. It respects Velox's
throw-time stack flags and rate limiting.

Formatting failures are contained inside this `noexcept` boundary and produce
category-specific fallback text. A final empty-string return is reserved for
the no-active-exception case or a catastrophic failure that prevents even the
fixed fallback from being represented.

## Exception-Logging Contract

Task 017B preserves the two FileCache call shapes:

```text
tryLogCurrentException(LoggerPtr, optional context)
tryLogCurrentException(const char * log_or_function_name, optional context)
```

Poco/raw logger overloads, a selectable severity argument, and a
`std::string` name overload are not required by FileCache and are not added.

`tryLogCurrentException` is used inside `catch (...)` for three business flows:

1. log and rethrow the original initialization exception;
2. log and recover or continue a background task;
3. log cleanup failures from a destructor or other non-throwing boundary.

Each overload also captures `std::current_exception` exactly once. If no
exception is active, it emits a fixed `LOG(ERROR)` misuse diagnostic instead of
an empty record, then returns without throwing.

Its normal output uses `LOG(ERROR)`:

```text
[logger-name] optional context: formatted current exception
```

Explicit glog filtering remains authoritative. Suppression by
`FLAGS_minloglevel` or compile-time stripping is configuration, not a backend
failure.

The complete helper is `noexcept`. If exception formatting, context assembly,
allocation, or the primary logging path fails, its outer defensive catch writes
a fixed, allocation-free diagnostic directly to `stderr` on a best-effort
basis. It then returns. It never throws a logging exception in place of the
original exception.

The emergency path does not promise to preserve the original exception text:
the failure may itself be an out-of-memory condition. It promises only a fixed
diagnostic attempt without allocation. An unavailable or broken `stderr`
cannot be made reliable by this component.

## Deliberate Differences from ClickHouse

| Area | ClickHouse | Velox port |
|---|---|---|
| Backend | Poco logger and channels | glog |
| Logger object | active named logger | immutable name-holder |
| `LOG_TEST` | logger-configured level below TRACE | `VLOG(3)` |
| Disabled macro | evaluates logger helper before level check | evaluates no caller expression |
| Exception lookup | bare `throw` inside the expected catch context | `std::current_exception`, safe outside catch |
| Exception helper | not declared `noexcept` | explicitly `noexcept` |
| Logging failure | raw `stderr` in parts of `LOG_IMPL`, then outer containment | one explicit emergency `stderr` path |
| Deduplication | `markAsLogged` for ClickHouse exceptions | not available in `VeloxException`; not emulated |
| Structured arguments | retained for ClickHouse logging channels | formatted text only |

## Rejected Alternatives

### Active `FileCacheLogger` abstraction

Adding enabled checks and sink methods to `FileCacheLogger` would duplicate
glog state. Macros would still be necessary because C++ evaluates function
arguments before entering a logger method. The extra abstraction therefore
does not improve laziness.

### Rewrite all FileCache call sites to glog

This removes the compatibility layer but requires a broad rewrite across
FileCache and makes future synchronization with ClickHouse harder. It also
requires rebuilding named attribution at every call site.

### Preserve the permanent no-op `LOG_TEST`

This gives the lowest possible cost but permanently discards useful
fine-grained diagnostics. `VLOG(3)` preserves the intended level while the lazy
gate keeps the default path free of caller-expression work.

## Verification Design

Focused tests must prove behavior, not merely successful compilation.

### Logging tests

1. For every macro, set glog flags so its record is filtered and prove the
   logger expression, format expression, and arguments are not evaluated.
2. Cover the combined `VLOG_IS_ON` and `FLAGS_minloglevel` case.
3. Enable each level and prove every expression is evaluated exactly once.
4. Capture glog records with `google::LogSink` and verify severity, logger
   attribution, and formatted text.
5. Observe `shared_ptr::use_count` while a format argument is evaluated and
   prove the macro does not copy logger ownership.
6. Compile a focused probe with `GOOGLE_STRIP_LOG` enabled and prove stripped
   records evaluate no caller expression.
7. Restore every modified glog flag with RAII to prevent cross-test leakage.

The ownership-copy test observes this invariant:

```text
unique LoggerPtr before logging: use_count == 1
use_count during format argument evaluation: 1
use_count after logging: 1
```

An implementation that captures the logger with `auto` produces a count of 2
during formatting and fails the test.

### Exception tests

1. `VeloxException` with and without an already captured stack.
2. `std::exception`.
3. unknown exception type.
4. no active exception: the formatter returns empty and
   `tryLogCurrentException` emits its fixed misuse diagnostic.
5. both `tryLogCurrentException` call shapes and optional context.
6. logger attribution and original exception text in captured ERROR output.
7. rethrow after logging and prove the original exception remains intact.
8. inject a throwing internal emitter, capture `stderr`, and prove the public
   helper returns without throwing after writing the emergency diagnostic.

The emitter injection point is internal testability plumbing, not a public
FileCache API and not a second production backend.

### Mutation proof

At minimum, buildable mutations must demonstrate RED for:

```text
removing a verbosity gate;
removing the minloglevel half of a VLOG gate;
evaluating the logger before the gate;
copying LoggerPtr ownership;
restoring permanent LOG_TEST no-op behavior;
removing logger-name attribution;
using bare throw outside a catch context;
ignoring the withStackTrace argument;
silently swallowing the emergency logging failure.
```

### Build matrix

The implementation must:

1. add the non-template implementation to the common FileCache library;
2. link direct `fmt`, glog, Velox exception, and stack-trace dependencies in
   non-mono mode;
3. rebuild all registered `velox_ch_*` targets in mono mode;
4. run accumulated `velox_ch_*` tests in mono mode;
5. repeat the build and accumulated tests in non-mono mode.

No timing threshold is required. The performance contract is proved directly
by zero-evaluation and zero-ownership-copy tests, avoiding a flaky
microbenchmark gate.

## Execution Boundary

This design invalidates the previous Task 017B implementation plan. A new plan
must be written from this spec after the user reviews the committed document.
Implementation remains blocked until:

```text
Task 018 four-driver addendum is accepted;
Review 5 is accepted;
the revised Task 017B plan is independently reviewed;
the Controller explicitly authorizes Task 017B implementation.
```
