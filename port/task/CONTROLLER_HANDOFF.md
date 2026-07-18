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
2. CH production source and real callers.
3. Approved design contracts.
4. EXECUTION_PROTOCOL.md and ENVIRONMENT.md.
5. The current numbered task file, including Controller amendments.
6. Accepted result receipts and actual Git state.
7. This handoff snapshot.

Unreviewed dependency rule:
- If the current task reaches any CH dependency/macro/type/API or proposed Velox
  mapping that is not explicitly reviewed in an approved design/task, stop the
  current task and the entire pipeline.
- Record the source, callers, semantic gap, and decision needed; set
  `waiting_for_user`.
- Do not infer a mapping, create a shim/no-op/fallback, or continue another task.
- Resume the same task only after the user reviews the mapping and the decision is
  written into the canonical design and task amendment.

Do not trust the snapshot blindly. First inspect both repositories:

  git status --short --branch
  git log -5 --oneline
  git diff --check

Then enumerate result receipts and determine the first task without an accepted
Controller review. If Git state or receipts disagree with this snapshot, stop
and resolve the discrepancy from repository evidence before editing.

Current verified snapshot after the post-Task-008 source-contract review:

- ClickHouse branch: ch-filecache
- ClickHouse accepted receipt HEAD:
    Task 008 receipt commit containing this handoff update; resolve with
    `git log -1 --oneline` because a commit cannot contain its own SHA.
- Velox branch: filecache
- Velox accepted implementation HEAD:
    4b14de7f1 Task 008: Add `FileCache` leaf types
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
    Task 006
      Velox:      d9f4517c5
      ClickHouse: 5af6ab908fe
    Task 007
      Velox:      711a84850
     ClickHouse: c9a5c35be06
   Task 008
     Velox:      4b14de7f1
     ClickHouse: this Task 008 receipt commit
- The original acceptance history remains in the receipts, but Tasks 003, 004,
  006, 007, and 008 are reopened by the source-contract review.
- Task 005 remains accepted with no confirmed defect in its current consumer path.
- Task 009 and Task 011 task contracts passed the read-only audit, but no Worker
  may start while the documentation repair is under user review.

Current task:

- Task 008 was accepted and is now reopened by contract audit.
- Velox is clean at `4b14de7f1`.
- Task 009 is `not_started`.
- Execution is paused for design/task-document review. Do not modify Velox source
  or dispatch a Worker until the user approves the revised documents.
- Persistent Task 008 logs belong under:
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13/
    /home/chang/OpenSource/velox/cmake-build-debug-gcc13-task008-nonmono/

Resume procedure:

1. Stay paused until the user approves the revised design/task documents.
2. On approval, repair reopened Tasks 003, 004, 006, 007, and 008 with new
   corrective commits; do not rewrite existing commits or receipt history.
3. Run the accumulated Task 003-008 regression and complete Controller review.
4. Only then dispatch Task 009.
5. After Task 010, stop for the mandatory Tasks 003-010 whole-port review.
6. After Task 014, stop for the mandatory Tasks 003-014 whole-port review.

Continuous execution target:

- Current stop condition: documentation review before any corrective coding.
- After user approval, repair reopened Tasks 003, 004, 006, 007, and 008 before
  starting Task 009.
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
- After resume, stop when:
    a. Task 010 is accepted, for the mandatory Tasks 003-010 review;
    b. Task 014 is accepted, for the mandatory Tasks 003-014 review;
    c. either review has any finding;
    d. a Worker writes `blocked` and the Controller cannot resolve it from
       repository evidence;
    e. a real product/architecture decision requires the user; or
    f. the user explicitly requests another pause.

Special task rules:

- Task 011 and Task 012 are one atomic SCC migration stage.
- Do not start Task 011 until the Task 010 whole-port review has zero unresolved
  findings and the user explicitly approves.
- Task 011 intentionally has no green build gate. Accept it only if its exact
  migration-only contract is satisfied, then immediately start Task 012.
- Task 012 must restore the full compile/link/test closure before proceeding.
- Do not start Task 015 until the Task 014 whole-port review has zero unresolved
  findings and the user explicitly approves.

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
