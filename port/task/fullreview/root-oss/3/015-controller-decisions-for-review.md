# Task 015 Controller Decisions for User Post-Review

## Status

```text
environment_profile: root-oss
review_status: user_post_review
blocks_current_execution: false
```

These decisions were made during Task 015 execution to resolve source-truth or
testability gaps that were not separately presented for user approval.

## D1 — Shared E2E/benchmark fixture

**Decision:** Add a header-only `FileCacheTestHelpers` surface shared by the E2E
binary and benchmark instead of duplicating manager/cache/source construction.

**Reason:** The original file scope had no reusable fixture even though the
benchmark contract prohibited a divergent second implementation.

**User decision:** approved.

## D2 — Executable Task-015 contract corrections

**Decision:** Store the test `MemoryPool` as a `shared_ptr`, link `velox_file`
instead of the unrelated Hive connector, and treat the E2E executable as one
accumulated CTest target.

**Reason:** The original raw-pointer fixture destroyed its pool immediately,
the Hive dependency was unnecessary, and CTest counts executables rather than
individual GTest cases with the repository's `add_test` pattern.

**User decision:** approved.

## D3 — Foreground direct-IO tail scope expansion

**Decision:** Modify `ReadBufferFromVeloxReadFile.cpp` even though the original
B1 scope expected only the accessor and `Metadata.cpp`.

**Reason:** E2E proved that strict direct-IO validation rejected the unaligned
logical EOF/right-bound tail before `pread`. The accepted fix keeps validation
strict, issues an aligned physical request, and publishes only logical bytes.
No buffered fallback was added.

**Coverage correction:** the two pre-existing adapter unit tests still asserted
the old rejection behavior. They were updated after a fresh rebuild exposed the
stale-binary false green; both now have a direct RED mutation and pass in mono
and non-mono builds.

**User decision:** conditionally accepted only as logic coverage. A real kernel
`O_DIRECT` integration test is mandatory; mock-only coverage does not close the
production-validation gate.

## D4 — Background aligned-body/pure-tail behavior

**Decision:** Let the background worker download only the alignment-multiple
body, then leave a pure sub-alignment tail for foreground completion. Add a
release-inert `TestValue` notification at the pure-tail skip.

**Reason:** Continuing the loop into the tail reproduced the alignment
exception B1 was intended to fix. The notification makes the absence of a
background tail read deterministically observable.

**User decision:** non-direct-I/O behavior is accepted as CH-aligned. The
direct-I/O aligned-body/tail-skip policy remains conditional on real kernel
`O_DIRECT` evidence and a final parity decision.

## D5 — Non-destructive benchmark freshness

**Decision:** Use fresh random cache keys for benchmark hit setup, misses, and
bypass reads instead of deleting the user-supplied cache directory.

**Reason:** Persisted metadata made counter-based keys collide across process
runs. Destructively clearing an arbitrary flag-supplied path was unsafe; fresh
128-bit keys preserve cold-miss semantics without deletion.

## D6 — Grouped mutation evidence

**Decision:** Permit one production mutation to serve multiple Task-015 tests
only when the log shows every named test executing and failing at its own
behavioral assertion. Each test remains a separate receipt row.

**Reason:** Several tests share the same cache-hit decision branch. Rebuilding
the identical mutation once preserves evidence quality while avoiding
duplicate builds.

**User decision:** approved.

## User review record

```text
D1: approve
D2: approve
D3: conditional — require real O_DIRECT integration
D4: conditional — non-DIO approved; DIO policy pending real O_DIRECT
D5: approve / modify / reject
D6: approve
```
