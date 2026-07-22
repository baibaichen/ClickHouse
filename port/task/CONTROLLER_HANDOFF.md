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
active environment_profile (re-verify after acceptance or interruption)
current dirty files, logs, and receipt status when work is in progress
new Controller amendments or unresolved blockers
```

Include the handoff update in the same ClickHouse commit as the accepted task's
receipt. Also update it immediately when a Worker is interrupted or a task
becomes blocked, so a replacement Controller never depends on conversation
history.

````text
Continue the FileCache port as the Controller, using file-only coordination.

Before acting on any instruction below: read `port/task/ENVIRONMENT.md` in this
ClickHouse checkout (the directory you are currently in — no absolute path
required), select the active profile, and resolve all logical keys
(`<clickhouse_repo>`, `<velox_repo>`, `<ninja>`, etc.) to their concrete values.

Repositories:
- Protocol and receipts: <clickhouse_repo>
- Implementation:       <velox_repo>
- Future Gluten work:   <gluten_repo>

Read before any action:
- <clickhouse_repo>/port/task/EXECUTION_PROTOCOL.md
- <clickhouse_repo>/port/task/ENVIRONMENT.md
- <clickhouse_repo>/port/task/CONTROLLER_HANDOFF.md
- the current numbered task file
- every accepted prerequisite receipt under
  <clickhouse_repo>/port/task/result/
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

Current verified snapshot after Task 015 acceptance:

environment_profile: root-oss

- ClickHouse branch: ch-filecache
- ClickHouse accepted receipt HEAD:
    the latest planning/receipt commit containing this handoff update; resolve
    with `git log -1 --oneline` because a commit cannot contain its own SHA.
- Velox branch: filecache
- Velox accepted implementation HEAD:
    43a9e6f75ffb94be38836b45fd476325665f50be (Task-015 direct-I/O adapter
    contract correction and fresh mono/non-mono accumulated rebuild)
- Accepted tasks:
    Task 003
      Velox:      4bea8d15e
      ClickHouse: c30a218c481
      Corrective Velox: c755512a8
      Corrective ClickHouse: 172d11361df
      Cross-profile corrective Velox: 1b41f73382668ffdc8d902e6dc5268e2e22832e2
      Cross-profile corrective ClickHouse: this acceptance commit
    Task 004
      Velox:      f948fb6a4
      ClickHouse: d20e9b241d4
      Corrective Velox: 5ed26f9413f4e52ef95830b8e4d6a1d91d1a7fe7
      Corrective ClickHouse: 6c57283d31775c1bcf8e53b251829a0c4a767a3c
    Task 005
      Velox:      b21177a51
      ClickHouse: dbe95a0fc7b
    Task 006
      Velox:      d9f4517c5
      ClickHouse: 5af6ab908fe
      Corrective Velox: b3c2832e18f76b574faf74e2d6ba05c2da741efd
      Corrective ClickHouse: this acceptance commit
    Task 007
      Velox:      711a84850
      ClickHouse: c9a5c35be06
      Corrective Velox: 7e7f157fc50c0945067184dd2ac55be82213bc1b
      Corrective ClickHouse: this acceptance commit
    Task 008
      Velox:      4b14de7f1
      ClickHouse: 1275836e76a
      Corrective Velox: 24686d2c68831566439911eec8a69287e6fa39e3
      Corrective ClickHouse: this acceptance commit
    Task 009
      Velox:      096ba0c9ef8d68ca91ca62a7b15cf6a74bbc058a
      ClickHouse: this acceptance commit
    Task 010
      Velox:      89039901aa4287ce811a3b1628867b0796c76678
      ClickHouse: this acceptance commit
    Task 011
      Velox:      72b77cc2f995c9a6e1d3bb82cd28bfc0beade9a4
      ClickHouse: this acceptance commit
    Task 012
      Velox:      a46ff4716cf9656be6d89562ed4b8ba40b0bba18
      ClickHouse: this acceptance commit
    Task 013
      Velox:      bbda44d2531af0235851bc069fd2d583762d8d96
      ClickHouse: this acceptance commit
    Task 014
      Velox:      b92a0ae3a96493aa63df44bc38514c68003db28e
      Corrective Task 007 Velox: 1e3cc3209087ad05765e03569314ba186f40ab07
      ClickHouse: this acceptance commit
- Tasks 003, 004, 006, 007, and 008 corrected and accepted; Tasks 009-014 accepted.
- Task 005 remains accepted with no confirmed defect in its current consumer path.
- Tasks 011-014 are accepted.
- The mandatory Tasks 003-010 full review completed with
  `controller_status: reopen_proposed`.
- The user approved the final cross-profile decisions in
  `port/task/fullreview/cross-profile/1/003-010-review-decisions.md`.
- Task 003 B1/B2 is corrected and accepted. Tasks 006 and 009 remain accepted
  under the approved defer/deviation decisions.
- Tasks 011, 012, 013, and 014 are accepted.
- The Task 003 accepted implementation preserves:
   exact CH timed/non-blocking queue and move-or-copy behavior;
   direct `VELOX_USER_FAIL` / `VELOX_FAIL` category mapping;
   Velox-style filesystem exceptions without structured errno;
   a non-null name-only logger;
   ordinary-Release `chassert` non-evaluation of both expression and message;
   and deferring non-empty current-exception formatting to Task 017B.

Current task:

- Task 003 B1/B2 is corrected and accepted.
- Velox is clean at `b92a0ae3a96493aa63df44bc38514c68003db28e`.
- Task 005 remains accepted.
- User approved the Task-007/Task-012 boundary:
    Task 007 proves already-open adapter behavior;
    Task 012 must prove production `FileSegment` append-mode resume and partial
    physical-write reconciliation.
- Canonical design and Tasks 007/012 record this split; Tasks 012/014/015 also
  record the approved CH test migration ownership.
- The mandatory Tasks 003-010 full review is complete. Its durable artifacts are:
    `port/task/fullreview/root-oss/1/003-010-review-decisions.md`;
    `port/task/fullreview/root-oss/1/evidence/011-012-consumer-contract-ledger.md`;
    `port/task/fullreview/root-oss/1/evidence/003-010-full-review-result.md`.
- Task 003 added the approved no-op `ProfileEvents` and `CurrentMetrics` name
  surfaces with compile-coverage and false-green evidence.
- Task 006 remains accepted. F-CALLERID and SD8 are deferred to Task 017A.
- Task 009 remains accepted. SD1 is an explicitly approved `F14FastMap`
  deviation; SD2 is confirmed as the Task-011 flat-container mapping.
- SD6, SD7, and SD9 are approved platform mappings recorded by the decisions.
- Task 012 requires the approved mappings for `Memory<>`, `SCOPE_EXIT`,
  `Stopwatch`, and `callOnce`, plus the typed `FileCacheErrnoException`
  consumer path. It must not add an errno-unavailable fallback.
- Structured errno production in the FileCache concrete writer is a separate
  pre-release gate and does not block Tasks 011/012 development.
- Task 003's accepted cross-profile corrective implementation is
  `1b41f73382668ffdc8d902e6dc5268e2e22832e2`.
- Tasks 011, 012, 013, and 014 have been amended with dependency pre-checks,
  CH consumer-contract excerpts (file:line), structure-deviation
  registrations (SD1-SD5), exact `ProfileEvents`/`CurrentMetrics` name lists
  where applicable, RED matrices, and false-green probe requirements. Stale
  pseudo-code that contradicted the cross-profile decisions (Task-011's
  full `CacheUsagePerUser` interface listing, Task-012's reconcile-every-
  exception bullet and `offsetof`-based `LockedKey` test, Task-013's private
  `checkedAdd`, Task-014's dangling placeholder-fixture warning) has been
  removed or superseded in place.
- The cross-profile review has zero unresolved findings.
- The user explicitly approved continuous execution through Task 014.
- Task 012 closes the full SCC with 101/101 focused tests in both mono and
  non-mono builds and 11/11 accumulated mono CTests.
- Task 013 is accepted with 42/42 focused tests in mono and non-mono.
- Task 014 is accepted with 24/24 focused tests in mono and non-mono, including
  corrected background reader handoff.
- The mandatory Tasks 003-014 full review is complete. Its artifacts are:
    `port/task/fullreview/root-oss/2/003-014-review-decisions.md`;
    `port/task/fullreview/root-oss/2/evidence/003-014-consumer-contract-ledger.md`;
    `port/task/fullreview/root-oss/2/evidence/003-014-full-review-result.md`;
    `port/task/fullreview/root-oss/2/evidence/014-senior-review-corrective-result.md`.
- The user approved the Review-2 decisions:
    B1 direct-IO/background alignment is deferred to Task 015 E2E/hardening;
    B2/B3 Task-011 cursor/SLRU evidence is required now;
    B4 Task-012/014 handoff-race evidence is required now;
    B5 Task-012 SCC-owned queue-pipeline evidence is required now.
- The Review-2 authoring wave is complete: Tasks 011, 012, 014, and 015 are
  amended in place with the binding B2-B5/B1 corrective contracts (exact
  CH/Velox source citations, exact test owners, RED/false-green matrices,
  mono/non-mono/accumulated gates, and stop conditions), and receipts
  011/012/014 each carry a `## Post-acceptance contract audit 1`
  (`controller_status: reopened_by_contract_audit`) recording the exact
  reopened findings and pointing at the corrective task sections. Task 015
  additionally carries the strengthened E2E scenario matrix (covering every
  scenario in its `## Goal` and the CH migration matrix) and a binding
  benchmark specification replacing the former comment-only skeleton.
- Next dispatch order:
    1. Task 011 B2/B3 is corrected and accepted at
       `b18a8d039904a0421011f6d5a47bcefa1669185b`;
    2. Task 012 B4/B5 is corrected and accepted at
       `ad1a13c37e87cecda464ac8dfcc9fee57c093eb6`;
    3. Task 014's B4 caller-order confirmation is accepted with no Velox source
       change;
    4. the targeted B2-B5 re-review is approved with zero unresolved findings;
    5. the user's instruction to continue through Task 015 satisfies the
       explicit-approval gate;
    6. Task 015 is implemented and accepted at
       `aadc10db7bffbbc49ee9d7dcee1e01e78bbadfff`;
       its direct-IO adapter test contract was corrected at
       `43a9e6f75ffb94be38836b45fd476325665f50be`, with all 15 registered
       `velox_ch_*` targets rebuilt and green in mono/non-mono;
       real kernel `O_DIRECT` integration is deferred by user decision because
       it is not the main path; mock logic coverage does not authorize claiming
       kernel-`O_DIRECT` coverage;
    7. Tasks 016-019 contract review is recorded in
       `port/task/fullreview/root-oss/3/016-019-task-review.md`;
    8. Task 016 was rewritten and re-reviewed, then explicitly deferred by the
       user as unnecessary/non-mainline;
    9. Task 017 was split: Task 017A owns statistics/cancellation/scheduler/
       caller-id; independent Task 017B owns logging and exception stacks;
    10. Task 018 was selected as mainline work and now owns adaptation of
        `CacheVerify`, core/buffered-input/wrapper microbenchmarks, and TPCH from
        `baibaichen/ch-filecache`;
    11. Tasks 017A/018 are jointly designed because benchmark output consumes
        Task-017A statistics;
    12. user-selected execution order is Task 017A -> Task 018 -> Review 5 ->
        Task 017B -> Task 019;
    13. executable plans are independently reviewed and stored at
        `port/task/017a-filecache-statistics-cancellation-plan.md`,
        `port/task/018-filecache-gluten-benchmark-plan.md`, and
        `port/task/017b-filecache-logging-exception-stack-plan.md`; the binding
        orchestration is `port/task/017a-018-017b-execution-plan.md`;
    14. Review 4's unresolved parity items are explicitly non-blocking for Tasks
        017A/018 by user decision. After Task 018, dispatch stops for Review 5:
        a Tasks 003–018 whole-port review whose first section closes the Review-4
        corrective/decision debt. No complete-parity or production-ready claim
        is allowed before Review 5 acceptance;
    15. Task 017A is accepted at Velox `a856d836c`. Task 018 non-TPCH work is
        authorized through 018-P. TPCH retains its separate explicit checkpoint
        approval; Review 5 and Task 017B retain their gates.
    16. Task 018 is Velox-only and stops after non-TPCH correctness,
        micro/wrapper/orchestration work and Waves 1–3. No TPCH source copy,
        target build, data requirement, or run is
        allowed until the user explicitly approves the pre-TPCH checkpoint.
        Every benchmark result must come from a fresh RelWithDebInfo or Release
        build; Debug benchmark results are invalid.
    17. Task-017A statistics use CH physical-I/O semantics: global cache/source
        byte events and query `ssdRead`/`read` record pre-clamp physical bytes;
        `rawBytesRead` alone records post-clamp logical bytes; predownload also
        contributes to the global source total and query `read`/`prefetch`.
    18. The approved hard split
        `port/design/filecache-task-018-019-hard-split.md` moves former
        018-E/F/G to Task 019. Task 019 is the full Gluten integration owner and
        begins with a compatible-Velox hard gate; the uncommitted Gluten WIP is
        preserved but is not Task-018 work.
- Tasks 003-015 are accepted.
- Persistent logs for corrective tasks belong under `<velox_build_dir>`.

Resume procedure:

1. Task 003 B1/B2 is corrected and accepted.
2. The approved Task-011/012/013/014 authoring amendments and structure
   deviation registrations are written in place (done by the Task-1
   authoring wave).
3. Tasks 011, 012, 014, and 015 plus receipts 011/012/014 are amended with
   B2-B5/B1 and marked `reopened_by_contract_audit` (done by this Review-2
   authoring wave; see `port/task/011-filecache-priority-eviction.md`,
   `port/task/012-filecache-core-scc.md`, `port/task/014-filecache-buffered-input.md`,
   and `port/task/015-filecache-velox-end-to-end.md`).
4. Tasks 011 B2/B3, 012 B4/B5, and 014's B4 caller-order confirmation are
   corrected and accepted.
5. The targeted re-review records zero unresolved findings.
6. Task 015 and Task 017A are complete and accepted. Task 018-A/B/D are accepted
   at Velox `9850a70fa`, `df9091e78`, and `5ae39651b`; Task 018-H1 is next.

Continuous execution target:

- Current state: Task 017A is accepted at Velox `a856d836c`; Task 018-A/B/D are
  accepted and Task 018-H1 may be dispatched. The mandatory stop before TPCH is
  018-P. After complete Task 018, the next mandatory stop is Review 5.
- Tasks 003-015 and Task 017A are accepted. Task 016 is deferred. Planned order
  continues with Task 018, Review 5, Task 017B, then Task 019 Gluten/Spark
  integration. Task 019 is blocked on its prerequisites and compatible Velox
  baseline.
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
    a. the mandatory Tasks 003-010 review has any finding or is awaiting user
       approval;
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

- Follow <clickhouse_repo>/.claude/skills/review/SKILL.md.
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
