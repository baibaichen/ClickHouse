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

Current verified snapshot after Task 010 acceptance:

environment_profile: home-chang

> Execution moved to the `home-chang` machine on 2026-07-20. The `root-oss`
> whole-port review (artifacts under `port/task/fullreview/root-oss/1/`) and its
> approved decisions are carried forward unchanged; only the environment
> coordinates differ. To physically isolate this machine's work from the
> `root-oss` `filecache` mainline, both repositories were renamed to `*2`
> branches. The Velox accepted HEAD (`89039901a`) is identical on both sides;
> only the branch label differs. All subsequent corrective commits happen on
> these `*2` branches on `home-chang`.

- ClickHouse branch: ch-filecache2
- ClickHouse accepted receipt HEAD:
    resolve with `git log -1 --oneline` because a commit cannot contain its own
    SHA. Latest committed: `0f8bb08fbf7 Task 010: Record Tasks 003-010 whole-port
    full review` (home-chang review artifacts).
- Velox branch: filecache2
- Velox accepted implementation HEAD:
    d93fa99e5 Task 003: Add `ProfileEvents`/`CurrentMetrics` enumerator surface (B1/B2)
    (parent 89039901a is the shared root-oss `filecache` baseline; branch renamed only)
- Accepted tasks:
    Task 003
      Velox:      4bea8d15e
      ClickHouse: c30a218c481
      Corrective Velox: c755512a8
      Corrective ClickHouse: 172d11361df
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
- Tasks 003, 004, 006, 007, and 008 corrected and accepted; Tasks 009 and 010 accepted.
- Task 005 remains accepted with no confirmed defect in its current consumer path.
- Task 011 contract passed the read-only audit.
- The mandatory Tasks 003-010 full review completed with
  `controller_status: reopen_proposed`.
- The user approved the final decisions in
  `port/task/fullreview/root-oss/1/003-010-review-decisions.md`.
- Task 003 B1/B2 enumerator-surface reopen is **accepted** (Velox `d93fa99e5`;
  ClickHouse receipt+handoff commit is this one). Tasks 006 and 009 remain
  accepted under the approved defer/deviation decisions.
- Task 011 remains not started. The remaining pre-011 work is the documentation
  registrations (SD1 sign-off, SD6/SD7/SD9, Task-012 D3/D9/D11 mappings, signed
  deviations R2/R6/R7/R8/R10) — **now recorded** into the live task documents
  (Task 010 R2/R10; Task 011 SD1/SD2/SD3/SD4/SD5; Task 012 D3/D9/D11 + errno +
  R2/R7; Task 005 SD6; Task 006 SD7/SD8/F-CALLERID; Task 007 SD9; Task 009
  receipt SD1 sign-off). With Task 003 B1/B2 accepted and these registrations
  landed, the pre-011 whole-port gate is at zero unresolved findings.

- **Task 011 is accepted** (Velox `e5b2af1a9`; ClickHouse receipt+handoff = this
  commit). During Task 011 the Worker twice hit the unreviewed-dependency gate;
  the user reviewed and approved these mappings, now recorded in the Task 011
  file's "## Approved dependency mappings" table:
    D-011-1 ProfileEvents timer -> ProfileEventTimeIncrement no-op (H2 -> Task 003)
    D-011-2 randomSeed/pcg64     -> folly::Random
    D-011-3 LockMemoryExceptionInThread -> omitted (no-op)
    D-011-4 WriteBufferFromOwnString    -> std::ostringstream
    D-011-5 failpoint bernoulli  -> no-op with no-op failpoint
    D-011-6 assert_cast          -> dynamic_cast + VELOX_CHECK
    D-011-7 TSA_* macros         -> velox/ch/Common/ClickHouseTSA.h (verbatim;
                                    Clang honors, GCC ignores without error)
    CacheUsage.h FilesystemCacheOvercommitUsers member dropped (B2/O2).
  Task 011 has no green build gate (atomic-stage Part A). **Task 012 is next and
  must restore the full compile/link/test closure** using the Task-012
  amendments already recorded (D3/D9/D11 mappings, FileCacheErrnoException
  contract, R2 value-comparison, R7 typed-subtype, SD1/SD3/SD4/SD5).

- **Task 012 is accepted** (Velox sub-attempts S1 `853840ae5`, S2 `dd7eaf43f`,
  S3 `16b4fc155`, S4 `13b2dc63d`; ClickHouse receipt+handoff = this commit). The
  atomic 011+012 center SCC compiles, LINKS, and passes 47/47 tests (0 failed /
  0 skipped) via `velox_ch_filecache_core_scc_test`. Task 012 was executed as
  four bounded sub-attempts (S1 headers, S2 FileSegment/Metadata + finish the
  Task-011 .cpp, S3 FileCache/QueryLimit, S4 CMake+tests+green) because a single
  pass could not port ~9k lines truthfully. Structural gaps resolved during 012,
  all recorded in the Task-012 amendment + receipt unblock responses 1-5:
    B1 EvictionCandidates.h C++23/merge portability fix
    B2a reserve timeout injected from FileCacheConfig into CacheMetadata (design 08)
    B2b + B7 opened-file-handle invalidation is a no-op + TODO(Task 013) at the
       removal + rename sites (fs::remove/fs::rename still run); Task 013 owns the
       real Manager-backed invalidation (design 02/013)
    B3 CacheMetadata takes the FileCache/Manager-owned FileCacheWorkerPool by ref (design 04)
    B5 host-injected stable commonUserId replaces ServerUUID (design 10)
    B6 FileCache owns folly::Timekeeper + FileCacheScheduler for createTask (design 05)
  **Task 013 is next** (FileCacheFactory + FileCacheManager). Task 013 must,
  among its own scope, wire the real OpenedFileCache invalidation into the two
  no-op+TODO seams left by B7, and take over ownership of the worker pool /
  timekeeper / scheduler / commonUserId that FileCache holds in the SCC phase.

- **Task 013 is accepted** (Velox `5e3ee1ac9` "Task 013: FileCacheFactory +
  FileCacheManager + OpenedFileCache"; ClickHouse receipt+handoff = this commit).
  Implemented the three CH-aligned contracts:
    D1 `OpenedFileCache` — faithful port of CH `src/IO/OpenedFileCache.h` (1024
       shards, each `{mutex; map<(path,int flags), weak_ptr<ReadFile>>}`), handle
       substituted to `shared_ptr<velox::ReadFile>` opened via injected
       `FileSystem::openFileForRead` with an erase-on-last-release deleter;
       Manager-owned (not a singleton, not the Hive `FileHandleCache`). Shard state
       in `shared_ptr<Shard>` + weak_ptr deleter (outliving-handle-safe); deleter
       erases only a still-`expired()` slot (resurrection-safe). CH `memoryPool`
       param dropped.
    D2 B7 seams wired — inject `OpenedFileCache&` (Manager -> FileCache ctor ->
       `CacheMetadata` member -> `FileSegment` via `KeyMetadata`). Both Task-012
       no-op+TODO seams now invalidate: `Metadata.cpp` `removeFileSegmentImpl`
       (after `fs::remove`, removed path) and `FileSegment.cpp`
       `renameToIncludeSizeInNameUnlocked` (after `fs::rename`, old path).
    D3 ownership move — `FileCacheWorkerPool`, `folly::Timekeeper`+
       `FileCacheScheduler`, and `commonUserId` moved from `FileCache` to the
       Manager; `FileCache` takes them by reference. Manager declares owned
       resources BEFORE `factory_` so the factory (and every `FileCache`) destroys
       first. SCC test fixtures adapted via `FileCacheTestResources` injection helper.
  Gates: `velox_ch_filecache_manager_test` 19/19 and `velox_ch_filecache_core_scc_test`
  47/47 (both 0 failed / 0 skipped). Note: `velox_ch_filecache_manager` was NOT a
  separate library — the mono build (`VELOX_MONO_LIBRARY=ON`) compiles Factory/
  Manager into `velox_ch_filecache` (a second lib would ODR-double-define the SCC).
  Controller found and required fixing one **false-green gap** (the two B7 seams were
  wired correctly but no test drove them end-to-end; a seam revert left all tests
  green); the worker added two seam-reaching E2E tests, and the Controller
  INDEPENDENTLY verified RED-on-revert (neutralized both seams -> both tests FAILED,
  restored -> green).

- **Task 014 is accepted** (Velox `bc78ef541` "Task 014: FileCacheBufferedInput +
  FileCacheInputStream"; ClickHouse receipt+handoff = this commit). Implemented the
  Velox DWIO scan read path: `FileCacheRequestContext`, `FileCacheFileIdentity`
  (key derivation empty-etag->fromPath / non-empty->SipHash128(path+etag)),
  `FileCacheBufferedInput` (lazy load; no-create `isBuffered`; shouldPrefetchStripes/
  preloaded/shouldPreload/hasCache all false; injected executor), and
  `FileCacheInputStream` (SeekableInputStream state machine ported from CH
  `CachedOnDiskReadBufferFromFile`: region-relative stream coords vs absolute
  FileCache/ReadFile offsets, QueryContextHolder held ctor->dtor and never reset by
  seek, downloader released on advance/seek/exception without returning the canceled
  reader, Q1/Q2 handoff from currentWriteOffset, predownload buffer re-install).
  Sources compiled into `velox_ch_filecache` (mono build; no separate
  `velox_ch_filecache_dwio`/`_manager` lib — ODR). Gate
  `velox_ch_filecache_buffered_input_test` 17/17; shared-file regressions
  `velox_ch_filecache_manager_test` 19/19 and `velox_ch_filecache_core_scc_test`
  47/47 stay green. Controller required strengthening two false-green tests: F1
  (`ReserveFailureBypassesCacheButReturnsData` now proves the bypass path via a
  tempCacheOnly re-read that throws — Controller INDEPENDENTLY reproduced the RED
  on a large cache) and F3 (mid-download exception-cleanup catch block proven
  reached with a downloader held; the isolated catch-only RED needs a live-stream
  state probe the MVP reader does not expose — documented, deferred to Task 015).
  Two CH reader cases (remote-object truncation, `readBigAt` source failure)
  excluded with recorded API-limitation justification, deferred to Task 015.

- **NEXT = MANDATORY Tasks 003-014 whole-port source-contract review (STOP POINT).**
  Per EXECUTION_PROTOCOL "After Task 014" and ENVIRONMENT.md: dispatch NO Task 015
  Worker. Review the accumulated Tasks 003-014 (center SCC, Factory/Manager,
  OpenedFileCache, the DWIO reader/handoff, cache miss/hit/seek/bypass, exception
  cleanup, shutdown order) against CH source, real callers, approved designs,
  implementation, tests, failure paths, and cross-task dependencies. Any finding
  reopens the affected task and stops. Continue to Task 015 (Velox-only E2E +
  random-seek benchmark) ONLY with zero unresolved findings AND explicit user
  approval. This checkpoint is the current stop condition — do not proceed past it
  autonomously.
  - Deferred / parked (do NOT lose these): Task 006 F-CALLERID (post/pre-release
    diagnostic); SD8 scheduler recursive_mutex (later task, controller suggests
    off-lock continuation); Task 004 StatusFile crash diagnostics R3 (pre-release
    blocker); Task 008 sipHash CH-oracle + malformed-char R4/R5 (post-019).
  - Execution rules still in force: worker/controller file-only protocol; only
    the Controller commits (user pre-authorized local commits, NEVER push);
    no -j; per-task logs under the velox build dir; big .cpp authored
    incrementally to avoid output-limit interruptions.
- The Task 003 accepted implementation preserves:
   exact CH timed/non-blocking queue and move-or-copy behavior;
   direct `VELOX_USER_FAIL` / `VELOX_FAIL` category mapping;
   Velox-style filesystem exceptions without structured errno;
   a non-null name-only logger;
   ordinary-Release `chassert` non-evaluation of both expression and message;
   and deferring non-empty current-exception formatting to Task 017.

Current task:

- Existing accepted implementation commits remain the baseline. Task 003 is
  selected for a corrective reopen; its task/receipt amendment is the next step.
- Velox is clean at `89039901aa4287ce811a3b1628867b0796c76678`.
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
- Task 003 must add the approved no-op `ProfileEvents` and `CurrentMetrics` name
  surfaces with compile-coverage and false-green evidence.
- Task 006 remains accepted. F-CALLERID and SD8 are deferred to Task 017.
- Task 009 remains accepted. SD1 is an explicitly approved `F14FastMap`
  deviation; SD2 is confirmed as the Task-011 flat-container mapping.
- SD6, SD7, and SD9 are approved platform mappings recorded by the decisions.
- Task 012 requires the approved mappings for `Memory<>`, `SCOPE_EXIT`,
  `Stopwatch`, and `callOnce`, plus the typed `FileCacheErrnoException`
  consumer path. It must not add an errno-unavailable fallback.
- Structured errno production in the FileCache concrete writer is a separate
  pre-release gate and does not block Tasks 011/012 development.
- No implementation was modified and Task 003 has not yet been marked
  `reopened_by_contract_audit` in its task/receipt.
- Task 011 is not started and remains prohibited.
- Persistent logs for corrective tasks belong under `<velox_build_dir>`.

Resume procedure:

1. Mark Task 003 and its receipt `reopened_by_contract_audit`; write the exact
   B1/B2 corrective contract and RED/false-green requirements.
2. Apply the approved Task-012 and Task-017 authoring amendments and structure
   deviation registrations.
3. Dispatch and re-review the Task-003 corrective Worker.
4. Continue to Task 011 only with zero unresolved findings and explicit user
   approval.
5. After Task 014, stop for the mandatory Tasks 003-014 whole-port review.

Continuous execution target:

- Current stop condition: approved Task-003 corrective contract and
  implementation are pending.
- Task 011 and Task 012 are prohibited.
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
