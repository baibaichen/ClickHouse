# `FileCache` Tasks 003-010 Full-Review Design

## Purpose

Before starting the center-SCC implementation in Tasks 011 and 012, re-establish
that every accepted dependency from Tasks 003-010 matches ClickHouse in both:

1. consumer-visible behavior; and
2. consumer-invisible implementation guarantees, including state representation,
   lock structure and order, container stability, and destruction order.

The review decides only whether Tasks 003-010 remain accepted or must be
reopened. Tasks 011 and 012 are a review lens, not review subjects.

## Frozen baselines

```text
ClickHouse:
  branch: ch-filecache
  commit: da28e83e8b3cb69090624b0a0b1f13cd78c13279

Velox:
  branch: filecache
  commit: 89039901aa4287ce811a3b1628867b0796c76678
```

The review is read-only with respect to implementation code. It must not start
Task 011, modify an accepted implementation, or propose a fix before the
findings have been validated and discussed.

## Outputs

The review produces two durable artifacts:

```text
port/task/011-012-consumer-contract-ledger.md
port/task/result/003-010-full-review-result.md
```

The first artifact is the CH-derived source of truth for the Task 011/012 stage.
The second records the comparison against the accepted Tasks 003-010 Velox
implementation.

## Exact dispatch prompts

### A agent

```text
Read section A of
/root/chang/OneDrive/share_data/local-cache/filecache-port-authoring-guide.md.
Using CH commit da28e83e8b3cb69090624b0a0b1f13cd78c13279, produce the
Consumer contract ledger for every Tasks 003-010 dependency reachable from the
CH priority/eviction and center-SCC implementation that Tasks 011/012 will port.
Use Tasks 011/012 only to bound the source set; do not use them as contract
authority. Do not inspect Velox implementation, receipts, or Velox tests.
Include the per-call-site contract tables, coverage matrix, CH structure
baseline, owner task, and section-E candidates. Every row must cite CH
file:line. Read-only; return only the ledger.
```

### D agent

```text
Read section D of
/root/chang/OneDrive/share_data/local-cache/filecache-port-authoring-guide.md
and the approved Tasks 011/012 Consumer contract ledger. Review accepted Tasks
003-010 at ClickHouse commit da28e83e8b3cb69090624b0a0b1f13cd78c13279
against Velox commit 89039901aa4287ce811a3b1628867b0796c76678, using
Task 011/012 only as the consumer and structure lens. Independently reconstruct
the CH contracts before reconciling with the ledger. Check both public behavior
and internal implementation structure, plus failure/concurrency paths, CMake,
tests, RED evidence, and false-green probes. Classify every row as matches,
drift, hole, over-port, or unproven and identify the owner task for every
non-match. Read-only; return only the full-review report.
```

### E agent

```text
Read section E of
/root/chang/OneDrive/share_data/local-cache/filecache-port-authoring-guide.md.
Probe only the undocumented Velox primitive semantic named by the Controller in
this dispatch.
Use a minimal empirical program or an existing repository probe facility. Do
not modify product source. Return only the observed result, exact command,
output evidence, platform/build context, and the contract-ledger row it closes.
```

## Phase A: restore the consumer contract

Dispatch one read-only agent with a fresh context. The agent must read section A
of `/root/chang/OneDrive/share_data/local-cache/filecache-port-authoring-guide.md`.

Task 011 and Task 012 may be used only to identify the intended center-SCC scope.
They are not contract authorities. The agent must not inspect the Velox
implementation, accepted receipts, or existing Velox tests before producing the
CH contract.

The source traversal begins at the CH priority/eviction and center-SCC files:

```text
IFileCachePriority
LRUFileCachePriority
SLRUFileCachePriority
SplitFileCachePriority
EvictionCandidates
FileSegment
Metadata
FileCache
QueryLimit
```

From those consumers and their real callers, the agent enumerates every reachable
Tasks 003-010 dependency and produces:

1. a reachable dependency inventory;
2. one contract row per call site, including signature and overload, state
   transition, error behavior, ownership, lifetime, concurrency, persistence,
   and owner task;
3. a CH structure baseline covering state representation, locks and lock order,
   containers and stability guarantees, and destruction order;
4. a call-site-to-contract-to-owner-task coverage matrix; and
5. a list of semantics that cannot be proved from source and may require a
   section-E primitive probe.

Every contract and structure row must cite a CH `file:line`.

### A completeness gate

The Controller checks the returned ledger before Phase D:

- every reachable call site maps to at least one contract row;
- every contract row maps back to a real call site;
- every structure guarantee has a CH source citation;
- every dependency row has one Tasks 003-010 owner;
- no behavior was inferred only from a leaf header; and
- no unsupported behavior was added because it looked generally useful.

Failure of this gate returns the ledger to the same A agent for correction.

## Section-E primitive probe

If A or D encounters an undocumented Velox primitive guarantee, the main review
pauses. One focused probe agent receives the section-E prompt from the guide and
tests only that semantic question with a minimal program or existing repository
probe facility.

The probe conclusion and evidence are written back to the consumer contract
ledger. No review row may be marked `matches` while a behavior-affecting primitive
assumption remains unproven.

## Phase D: compare the accepted implementation

Dispatch one new read-only agent after the A completeness gate. It must read
section D of the authoring guide and independently reconstruct the CH contracts
before comparing its reconstruction with the A ledger. This prevents the D agent
from inheriting an A omission.

The D agent reviews all accepted Tasks 003-010 from the Task 011/012 consumer
perspective along five planes:

1. **Behavioral parity:** signatures, overloads, state transitions, ordering,
   priorities, errors, cleanup, ownership, and lifetime.
2. **Structural parity:** state-machine shape, locks and lock order, containers
   and their stability guarantees, member and destruction order.
3. **Center-SCC readiness:** every Tasks 003-010 API and guarantee required by a
   Task 011/012 CH call path is available without a fake type, fallback, or
   weakened contract.
4. **Failure and concurrency:** exceptions, cancellation, partial progress,
   shutdown, in-flight callbacks, lock release, and destructor paths.
5. **Evidence:** focused tests, behavioral RED evidence, false-green probes,
   CMake registration, and mono/non-mono test discovery.

Each contract row receives exactly one status:

```text
matches
drift
hole
over-port
unproven
```

Any implementation that is not a direct CH structural translation or an already
approved infrastructure substitution must appear in the structure-deviation
ledger with:

```text
CH structure
Velox replacement
guarantee difference
hard platform constraint
section-E evidence
human approval
```

Without all required evidence, the row remains unresolved.

## Controller validation gate

Delegated output is evidence, not the verdict. The Controller validates:

1. every difference between the A and D contract reconstructions;
2. every `drift`, `hole`, `over-port`, and `unproven` row;
3. every structural deviation;
4. every `matches` row on a path that can block or corrupt Task 011/012; and
5. every source, test, RED, and false-green citation used for the final verdict.

Suspicious behavior is traced with concrete inputs and state transitions. A
finding is not accepted merely because an agent reported it, and a high-risk
row is not accepted merely because an existing test is green.

Invalid citations or incomplete coverage return the report to the same D agent.

## Final report

`port/task/result/003-010-full-review-result.md` contains:

1. frozen baselines and blind spots;
2. the approved A ledger and A/D reconciliation;
3. one verdict table for each Task 003-010;
4. the full call-site coverage matrix;
5. the structure-deviation ledger;
6. missing tests, RED evidence, and false-green probes;
7. section-E probe results or unresolved probe requirements;
8. the proposed reopen list; and
9. the zero-unresolved gate state.

### Findings exist

The report ends with `reopen proposed`. No implementation is changed and Task
011 is not started. The findings are discussed with the user and grouped into
corrective work. Validated findings are then recorded as
`reopened_by_contract_audit` in the affected task and receipt before repair.

### No unresolved findings

The report ends with `ready for user approval`. Task 011 still does not start
until the user explicitly approves continuation.

## Follow-up for Tasks 011 and 012

After the full-review result is accepted, use sections B and C of the authoring
guide to compare the approved A ledger with the existing Task 011 and Task 012
contracts and RED-test matrices. That authoring pass is separate from this
Tasks 003-010 verdict and occurs before either worker starts.
