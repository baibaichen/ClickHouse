# `FileCache` Port Task Execution Protocol

This protocol coordinates one worker agent and one controller through files.
The worker implements exactly one numbered task and stops. The controller owns
cross-task correctness, acceptance, and commits.

## Current execution boundary

```text
Dispatch now:        Tasks 003-015, in numeric order
Optional Velox work: Tasks 016-017, only after a separate decision
Deferred Gluten:     Tasks 018-019, not in the current phase
```

Task 011 and Task 012 form one atomic SCC migration stage. Run them
consecutively, but still use separate workers and receipts. Task 011 is
intentionally not a green build gate; Task 012 must restore the compile/link
closure.

## Source-of-truth files

For task `NNN`, the worker reads:

```text
port/task/EXECUTION_PROTOCOL.md
port/task/ENVIRONMENT.md
port/task/NNN-*.md
port/task/result/*-result.md for earlier accepted dependencies
```

The numbered task file defines scope, commands, acceptance criteria, and the
receipt filename. If this protocol conflicts with a task file, stop and record
the conflict rather than guessing.

## State machine

```text
not_started
  -> worker_running
  -> ready_for_controller
  -> accepted

ready_for_controller
  -> changes_requested
  -> worker_running

worker_running
  -> blocked
  -> worker_running
```

Only the worker writes implementation changes and worker-attempt sections.
Only the controller writes controller-review sections and creates commits.
An `accepted` receipt is immutable.
A `blocked` receipt is escalated to the controller. After the external blocker
is resolved, the controller redispatches the same task number for a new worker
attempt.

## Worker rules

1. Execute exactly one task number. Do not begin its recommended next task.
2. Do not stage, commit, amend, rebase, push, or create a pull request.
3. Before editing, capture branch, HEAD, and dirty status in every repository
   named by the task. Preserve unrelated existing changes.
4. Modify only the task's declared file scope. If another file is required,
   stop as `blocked` and explain why instead of silently expanding scope.
5. Follow the task steps and run every applicable acceptance gate. Redirect
   build and test output to the exact log files required by the task.
6. A skipped, disabled, unbuilt, or unregistered test is not a passing test.
   Do not weaken assertions or acceptance criteria to obtain green output.
7. After implementation and local validation, launch exactly one read-only
   code-review subagent for the complete task-owned diff across all affected
   repositories. Give it the task file, relevant design references, complete
   diffs, and test outcomes. Ask only for correctness, concurrency, lifetime,
   integration, and false-green findings; it must not edit files.
8. Resolve every actionable in-scope review finding and rerun affected gates.
   Record findings and resolutions. Unresolved findings make the task
   `blocked`, not successful.
9. Write or append the task's declared result receipt, set
   `worker_status: ready_for_controller`, and stop immediately.

The worker is responsible for correctness inside the assigned task. It is not
responsible for approving the overall port architecture or earlier tasks.

## Worker receipt format

The task file's result path is authoritative. Create the parent directory if
needed. Use one `Worker attempt` section per attempt; never erase prior
controller feedback.

````markdown
# Task NNN Result: <title>

## Worker attempt 1

```text
worker_status: ready_for_controller | blocked
task: NNN
```

## Repository baselines

| Repository | Branch | HEAD | Initial dirty status |
|---|---|---|---|
| `<path>` | `<branch>` | `<sha>` | `<status>` |

## Files changed

```text
<absolute or repository-relative paths>
```

## Commands and outcomes

| Command purpose | Exit code | Log |
|---|---:|---|
| configure/build/test/benchmark | 0 | `<absolute log path>` |

## Acceptance evidence

```text
test count:
failed tests:
skipped/disabled tests:
benchmark result, when required:
git diff --check:
```

## Worker review

```text
review subagent:
findings:
resolutions:
unresolved findings:
```

## Blockers

```text
None, or the first actionable error and exact evidence.
```

## Worker declaration

```text
Only Task NNN was attempted.
Changes are unstaged and uncommitted.
The worker stopped after writing this receipt.
```
````

For a rework attempt, append `## Worker attempt 2` with fresh baselines,
commands, evidence, and review. Do not alter the previous attempt or the
controller's request.

## Controller rules

The controller performs these checks after the worker stops:

1. Confirm the receipt says `ready_for_controller`, names the requested task,
   and contains no unresolved finding or blocker.
2. Inspect branch, HEAD, status, and complete diffs in every affected
   repository. Separate task-owned changes from pre-existing user changes.
3. Check exact file scope, API compatibility, dependency direction, ownership,
   concurrency, shutdown order, error propagation, and consistency with all
   previously accepted tasks.
4. Read the referenced logs and verify commands, exit codes, test discovery,
   test counts, and benchmark evidence. Rerun a focused gate when evidence is
   incomplete or suspicious.
5. Perform the overall architecture review that the worker intentionally does
   not own. In particular, check that the accumulated implementation still
   matches the dependency DAG and accepted design documents.
6. Append one controller-review section to the receipt.
7. If changes are required, set `controller_status: changes_requested`, do not
   stage or commit, and dispatch the same task number again.
8. If accepted, commit task-owned implementation separately in every affected
   implementation repository, then commit the receipt in the ClickHouse
   repository. Never include unrelated changes.

Every physical commit for one logical task starts its subject with
`Task NNN:`. A cross-repository task therefore has multiple commits linked by
the same task number. Do not amend or rebase. Do not commit on `master`.

## Controller receipt format

Append:

````markdown
## Controller review 1

```text
controller_status: accepted | changes_requested
task: NNN
```

## Review evidence

```text
scope review:
implementation review:
cross-task architecture review:
log and test review:
unresolved findings:
```

## Required changes

```text
None, or precise actionable findings for the next worker attempt.
```

## Commits

| Repository | Commit |
|---|---|
| `<path>` | `<sha>` |

````

For `changes_requested`, leave the commit table empty. For `accepted`, append
the implementation commit IDs before committing the receipt itself. The
ClickHouse receipt commit cannot list its own SHA; identify it in the
controller's external task ledger or next accepted receipt if needed.

## Copyable worker prompt

Replace only `NNN`:

```text
Execute FileCache port Task NNN using file-only coordination.

Repository containing the task protocol:
/home/chang/SourceCode/ClickHouse

You must first read:
- port/task/EXECUTION_PROTOCOL.md
- port/task/ENVIRONMENT.md
- the single file matching port/task/NNN-*.md
- accepted prerequisite receipts under port/task/result/

Execute exactly Task NNN. Follow its declared scope and gates. Do not start
another task. Do not stage, commit, amend, rebase, push, or create a PR.
Preserve unrelated dirty changes.

After implementation and validation, launch exactly one read-only code-review
subagent for the complete Task NNN diff. Fix all actionable in-scope findings
and rerun affected gates. Then write or append the result receipt required by
the task, using the format and state machine in EXECUTION_PROTOCOL.md. Stop
immediately after the receipt is written, whether the task is ready or blocked.

Your final response must contain only:
Task NNN stopped; receipt: <absolute path>; worker_status: <status>
```

## Copyable controller prompt

Replace only `NNN`:

```text
Review FileCache port Task NNN as controller.

Read port/task/EXECUTION_PROTOCOL.md, port/task/ENVIRONMENT.md, the Task NNN
file, its result receipt, and accepted dependency receipts. Inspect the full
task-owned diff and referenced logs in every affected repository. Review both
task correctness and accumulated architecture correctness.

Append a controller review to the receipt. If changes are needed, set
controller_status to changes_requested, do not stage or commit, and stop. If
accepted, commit only task-owned implementation changes separately in each
affected implementation repository with a `Task NNN:` subject, record those
SHAs in the receipt, and then commit only the receipt in the ClickHouse
repository. Never amend, rebase, or commit on master.
```
