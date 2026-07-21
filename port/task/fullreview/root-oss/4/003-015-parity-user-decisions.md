# Tasks 003–015 Parity Audit — User Decisions (Review round 4)

```text
decision_date: 2026-07-21
scope: decisions only
implementation_status: deferred_to_separate_change
audit_reclassification_status: deferred_until_verified
```

This file records only the decisions made after
[`003-015-ch-parity-audit.md`](003-015-ch-parity-audit.md). Implementation,
tests, task receipts, and parity classifications must be updated and verified
separately.

| decision_id | status | user decision |
|---|---|---|
| `R2-D2` | `modify` | Replace the hand-written initialization mutex+completed flag with `folly::once_flag` and `folly::call_once`. |
| `R2-D4` | `pending` | Do not approve, reject, modify, or reclassify the Manager mutation-serialization/transactional-reload decision yet. |
| `R2-D6` | `pending` | Do not approve, reject, modify, or reclassify the reader-detach/query-pool-lifetime decision yet. |
| `G-CACHEBUF-01` | `approve_fix` | Restore the ClickHouse external-truncation self-heal; do not accept the gap as a permanent deviation. |

## Scheduling decision — defer corrective closure until after Task 018

```text
decision_date: 2026-07-21
review_4_pipeline_blocking_for_017a_018: false
next_review: Review 5 — Tasks 003-018 whole-port review
review_5_position: after accepted Task 018, before Task 017B
```

The remaining Review-4 corrective work and pending decisions do not block Tasks
017A or 018. This is a scheduling exception only: the Review-4 audit remains
`PARITY_BLOCKED`, its row classifications remain unchanged, and no complete CH
parity or production-ready claim is authorized during Tasks 017A/018.

After Task 018 is accepted, implementation dispatch stops. Review 5 must:

1. close or disposition the remaining Review-4 decisions and evidence debts;
2. implement and verify any approved Review-4 corrective work that is still
   absent from the accepted Velox branch;
3. review Task 017A and Task 018 as an integrated statistics, cancellation,
   lifecycle, Builder, metrics, correctness, and benchmark path;
4. issue the next whole-port verdict before Task 017B starts.

Real kernel `O_DIRECT` remains deferred as a non-main-path item by user
decision. Tasks 017A/018 and their baseline benchmarks must not claim real
kernel-`O_DIRECT` coverage, but its absence does not block their execution or
Review-5 scheduling.

## `R2-D2` — use Folly once semantics

Canonical mapping:

```text
CH callOnce / OnceFlag
  -> folly::call_once / folly::once_flag
```

Required guarantees:

- concurrent callers execute the initialization callback successfully once;
- successful completion is safely published to later callers;
- a throwing callback does not mark the flag complete, so a later call retries;
- the static `libstdc++`/`glibc` `std::call_once` path through `pthread_once`
  is not used;
- the local mutex+completed-flag replacement is removed.

Required evidence in the current toolchain:

1. the first callback invocation throws without terminating the process;
2. the second invocation succeeds;
3. later invocations do not rerun the callback;
4. the existing `FileCache` initialize-once behavior remains covered in mono
   and non-mono builds.

## `R2-D4` — pending

The user has not approved the `FileCacheManager` shared-worker-pool mutation
serialization or transactional reload design. Keep its implementation,
decision status, and affected parity rows unchanged until this item is
revisited.

## `R2-D6` — pending

The user has not approved the reader detach and query-scoped `MemoryPool`
lifetime design. Keep its implementation, decision status, and affected parity
rows unchanged until this item is revisited.

## `G-CACHEBUF-01` — restore external-truncation self-heal

Required behavior:

1. compare the cache file's physical size with the segment's recorded
   downloaded size when the recorded final size is trustworthy;
2. if the physical file is shorter, log a warning and do not serve it as a
   complete cache hit;
3. bypass to the remote source and rebuild or re-fetch the cache content as
   ClickHouse does;
4. correct any source comment that claims this behavior before it exists.

Required evidence:

1. create and fully cache a segment;
2. truncate its physical cache file;
3. prove the next read returns the complete remote bytes through the
   warning/bypass/re-fetch path;
4. prove a mutation that removes the physical-size check makes the focused test
   fail for the expected reason.
