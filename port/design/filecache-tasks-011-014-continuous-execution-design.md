# `FileCache` Tasks 011-014 Continuous-Execution Design

## Goal

After correcting Task 003 B1/B2, execute Tasks 011 through 014 continuously,
with one Worker and one Controller gate per task, then stop at the mandatory
Tasks 003-014 full-review checkpoint.

## Authority

Workers read behavior in this order:

```text
CH source and real callers
  -> port/task/fullreview/cross-profile/1/003-010-review-decisions.md
  -> numbered task contract
  -> profile review evidence
```

The cross-profile file reconciles root-oss and home-chang. A Worker must not
choose between profile documents or infer a new primitive mapping.

## Phase 0: task-authoring wave

Before dispatching any implementation Worker:

1. amend Task 003 with B1/B2 scope, exact name sets, compile RED, and
   false-green mutation;
2. append `reopened_by_contract_audit` to its receipt;
3. amend Tasks 011-014 with:
   - consumer-contract excerpts and CH `file:line`;
   - dependency pre-check rows;
   - approved structure-deviation citations;
   - one RED and one false-green probe per material contract;
   - exact task boundaries and exclusions;
   - mono/non-mono or migration-only build gates;
4. remove or supersede stale pseudo-code that contradicts the decisions;
5. update protocol and handoff to the first executable corrective task.

This authoring wave changes task contracts and coordination documents only.

## Task 003 corrective stage

The Worker changes only the shared no-op enum surfaces and focused compile
coverage:

- 31 missing `ProfileEvents` names;
- five missing in-scope `CurrentMetrics` names;
- no real counters;
- no overcommit metric;
- compile RED and delete-one-name false-green evidence.

Controller acceptance is required before Task 011.

## Tasks 011 and 012: one atomic SCC stage

### Why Task 011 does not compile alone

Task-011 priority and eviction sources call Task-012 `FileSegment`, `Metadata`,
`LockedKey`, and `FileCache`. Task-012 `FileCache` and `QueryLimit` call the
Task-011 priority implementations. Compiling Task 011 alone would require fake
core definitions, which are prohibited.

### Task 011

- migrate priority/eviction source only;
- run structural and source-parity checks;
- create no CMake/test target;
- stop if a fake core type or unapproved dependency is required.

Controller reviews the migration-only diff and immediately dispatches Task 012.

### Task 012

- add `FileSegmentInfo`, `FileSegment`, `Metadata`, `FileCache`, and
  `QueryLimit`;
- finish Task-011 source integration against the real core types;
- build and link the complete center SCC once;
- run the full priority/core focused suite in mono and non-mono modes;
- prove typed errno handling, resume, partial physical writes, query-limit
  lifetime, shutdown order, queue pipeline, and scheduler integration;
- reject test-side copies of production reconciliation.

Task 012 is the first complete, compilable, usable FileCache implementation.

## Task 013

Add Factory/Manager ownership and lifecycle around the accepted core:

- Manager owns runtime services;
- Factory owns none;
- caches deactivate outside the registry lock;
- registry and settings locks preserve their order;
- all overflow checks reuse the shared `checkedAdd`.

Controller verifies constructor/destructor ordering, global-instance lifetime,
deduplication, dynamic settings, and clean shutdown.

## Task 014

Add buffered input and input stream consumers:

- cache hit/miss and bypass;
- reader attach/detach/handoff;
- seek inside and outside the active buffer;
- source failure and disk failure;
- query-context lifetime;
- downloader cleanup;
- assigned CH gtest migration.

Task 014 changes no Gluten files.

## Controller gates

For every task:

1. inspect every tracked and untracked task-owned file;
2. verify CH contract and structure, not only tests;
3. read build/test logs directly;
4. require behavioral RED and false-green evidence;
5. require mono/non-mono registration where applicable;
6. append a Controller review;
7. redispatch the same task on any finding;
8. commit accepted implementation and receipt separately.

No rebase or amend is used.

## Continuous-execution stop conditions

Do not pause between accepted tasks. Stop only when:

- a Worker reports `blocked`;
- an unreviewed dependency or primitive mapping appears;
- Controller finds a contract, structure, test, or integration defect;
- a product decision requires user input; or
- Task 014 is accepted.

After Task 014:

1. dispatch no Task 015 Worker;
2. run the mandatory Tasks 003-014 full review;
3. include center SCC, Factory/Manager, reader handoff, cache hit/miss, seek,
   source/disk failure, exception cleanup, and shutdown;
4. stop on every finding;
5. continue to Task 015 only after zero unresolved findings and explicit user
   approval.

## Deferred work does not block Tasks 011-014

The following remain explicit later gates:

- Task 017 caller-id diagnostics, scheduler mutex decision, real metrics/events,
  logging, and exception formatting;
- post-Task-019 SipHash/parser evidence;
- pre-release structured errno producer and `StatusFile` restart diagnostics.
