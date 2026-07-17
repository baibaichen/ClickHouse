# `FileCache` Port Controller Handoff

Copy the prompt below into any agent that must take over the current
`FileCache` port work. The repository is the source of truth; the current-state
snapshot is only a bootstrap hint and must be verified before acting.

## Maintenance rule

The Controller must update this file after every accepted task. The update must
replace stale snapshot data with:

```text
latest accepted task
latest implementation and receipt commit SHAs
next task and its state
current dirty files, logs, and receipt status when work is in progress
new Controller amendments or unresolved blockers
```

Include the handoff update in the same ClickHouse commit as the accepted task's
receipt. Also update it immediately when a Worker is interrupted or a task
becomes blocked, so a replacement Controller never depends on conversation
history.

````text
Continue the FileCache port as the Controller, using file-only coordination.

Repositories:
- Protocol and receipts: /home/chang/SourceCode/ClickHouse
- Implementation:       /home/chang/OpenSource/velox
- Future Gluten work:   /home/chang/SourceCode/gluten1

Read before any action:
- /home/chang/SourceCode/ClickHouse/port/task/EXECUTION_PROTOCOL.md
- /home/chang/SourceCode/ClickHouse/port/task/ENVIRONMENT.md
- /home/chang/SourceCode/ClickHouse/port/task/CONTROLLER_HANDOFF.md
- the current numbered task file
- every accepted prerequisite receipt under
  /home/chang/SourceCode/ClickHouse/port/task/result/
- applicable repository-local instruction files

Authority order:
1. Current user instructions.
2. EXECUTION_PROTOCOL.md and ENVIRONMENT.md.
3. The current numbered task file, including Controller amendments.
4. Accepted result receipts and actual Git state.
5. This handoff snapshot.

Do not trust the snapshot blindly. First inspect both repositories:

  git status --short --branch
  git log -5 --oneline
  git diff --check

Then enumerate result receipts and determine the first task without an accepted
Controller review. If Git state or receipts disagree with this snapshot, stop
and resolve the discrepancy from repository evidence before editing.

Current verified snapshot at handoff creation:

- ClickHouse branch: ch-filecache
- ClickHouse accepted receipt HEAD:
    dbe95a0fc7b Task 005: Record `FileCache` thread pools
- Velox branch: filecache
- Velox accepted implementation HEAD:
    b21177a51 Task 005: Add `FileCache` thread pools
- Accepted tasks:
    Task 003
      Velox:      4bea8d15e
      ClickHouse: c30a218c481
    Task 004
      Velox:      f948fb6a4
      ClickHouse: d20e9b241d4
    Task 005
      Velox:      b21177a51
      ClickHouse: dbe95a0fc7b
- Task 004 and Task 005 each required a Controller amendment and a second
  Worker attempt. Their committed task files and receipts contain the final
  authoritative contracts. Do not restore the original literal snippets.

Current interrupted task:

- Task 006 Worker execution was interrupted by the agent runtime.
- It did not return `blocked`.
- There is no Task 006 receipt and no live Worker agent.
- The Velox checkout contains unstaged Task 006 work:
    velox/ch/Common/CMakeLists.txt
    velox/ch/Common/tests/CMakeLists.txt
    velox/ch/Common/FileCacheQueryIdScope.cpp
    velox/ch/Common/FileCacheQueryIdScope.h
    velox/ch/Common/FileCacheScheduler.cpp
    velox/ch/Common/FileCacheScheduler.h
    velox/ch/Common/tests/SchedulerAndScopeTest.cpp
- Persistent Task 006 logs exist under:
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13/
- A temporary full-diff artifact exists at:
    /home/chang/SourceCode/ClickHouse/tmp/task006_full_diff_for_review.txt
- The interrupted Worker reached implementation and validation preparation,
  but did not complete the mandatory read-only self-review or write its result
  receipt. Treat Task 006 as `worker_running`, not accepted.

Resume procedure:

1. Redispatch a fresh Worker for exactly Task 006.
2. Tell it to inspect and preserve the existing unstaged Task 006 work rather
   than restarting or deleting it.
3. It must verify every existing change against the current Task 006 file,
   rerun required gates, launch exactly one read-only review subagent, resolve
   in-scope findings, write the declared Task 006 receipt, and stop.
4. It must not stage, commit, amend, rebase, push, create a PR, create another
   worktree, or start Task 007.
5. After the Worker stops, perform the Controller review defined by
   EXECUTION_PROTOCOL.md. Do not accept the Worker's summary without reading
   the complete diff and logs.

Continuous execution target:

- Complete Tasks 006 through 014 in numeric order without pausing for routine
  confirmation.
- For every task:
    a. Dispatch one fresh Worker for exactly that task.
    b. Worker implements, validates, launches one read-only self-review,
       writes/appends the receipt, and stops.
    c. Controller inspects all tracked and untracked task-owned files, reads
       logs directly, checks task and accumulated architecture correctness,
       and appends a Controller review.
    d. If changes are required, amend the numbered task contract when the
       specification itself is wrong, append `changes_requested`, and
       redispatch the same task for another Worker attempt.
    e. If accepted, commit only task-owned implementation changes in each
       implementation repository with a `Task NNN:` subject, record those
       commit SHAs in the receipt, update CONTROLLER_HANDOFF.md to the newly
       accepted state, then commit the task amendment (if any), receipt, and
       handoff update together in ClickHouse.
    f. Confirm affected repositories are clean before dispatching the next
       task.
- Stop only when:
    a. Task 014 is accepted and both implementation and receipt commits exist;
    b. a Worker writes `blocked` and the Controller cannot resolve it from
       repository evidence; or
    c. a real product/architecture decision requires the user.

Special task rules:

- Task 011 and Task 012 are one atomic SCC migration stage.
- Task 011 intentionally has no green build gate. Accept it only if its exact
  migration-only contract is satisfied, then immediately start Task 012.
- Task 012 must restore the full compile/link/test closure before proceeding.
- Do not start Task 015 in this handoff. The requested stopping point is
  accepted Task 014.

Controller review requirements:

- Follow /home/chang/SourceCode/ClickHouse/.claude/skills/review/SKILL.md.
- Verify contract, impacted surface, failure paths, ownership, concurrency,
  lifecycle, error propagation, CMake registration, test discovery, skipped or
  disabled tests, and false-green risks.
- Default `git diff` omits untracked files. Read every task-owned untracked
  file explicitly.
- Build and test output must remain in the declared build-directory logs.
- Use a log-analysis subagent and verify the concise result against source and
  Git state.
- Do not dismiss a real defect merely because it was copied from a task's
  literal code block. Correct the task specification, request rework, and keep
  the history in the receipt.
- Preserve unrelated user changes.
- Never use rebase or amend.
- Never commit on `master`.
- Never use `-j` with Ninja or `nproc`.

When the continuous run stops, report:

- last accepted task;
- implementation and receipt commit SHA for each newly accepted task;
- any task amendments made by the Controller;
- verification evidence for the stopping task;
- exact blocker, if execution did not reach accepted Task 014.
````
