# Task 017A Result: `FileCache` Statistics, Cancellation, and Scheduler Parity

## Worker attempt 1

```text
worker_status: ready_for_controller
environment_profile: root-oss
task: 017A
velox_base: 43a9e6f75ffb94be38836b45fd476325665f50be
velox_head: a856d836c
```

## Repository baselines

| Repository | Branch | Initial state |
|---|---|---|
| `/root/oss/velox` | `filecache` | clean at `43a9e6f75` |
| `/root/oss/clickhouse` | `ch-filecache` | clean; task plans committed |
| `/root/oss/gluten` | `main` | not modified; existing user changes preserved |

## Velox commits

```text
e68690f  Task 017A: Implement `FileCache` metrics storage
74165cd  Task 017A: Wire `FileCache` reader statistics
72d485c  Task 017A: Propagate `FileCache` cancellation
4a2b0f8  Task 017A: Align `FileCache` scheduler locking
4bfe970  Task 017A: Update caller-id acceptance coverage
a856d83  Task 017A: Document caller-id format
```

## Implemented contracts

- Replaced no-op `CurrentMetrics` and `ProfileEvents` with process-wide,
  out-of-line relaxed atomic storage.
- Preserved all existing metric/event names and appended the 10 required CH
  reader events.
- Added `FileCacheStatsSnapshot`, `takeFileCacheStatsSnapshot`, delta
  subtraction, and `kFileCacheWriteBytes`.
- Wired independent global and query I/O ledgers at successful completion
  points.
- Recorded cache/source I/O as pre-clamp physical bytes and `rawBytesRead` as
  post-clamp logical bytes.
- Counted predownload in global source/predownload bytes and query
  `read`/`prefetch`, never in `rawBytesRead`.
- Copied `folly::CancellationToken` by value into each stream, passed it to
  `FileSegment::wait`, and checked it only at safe ownership boundaries.
- Restored caller IDs to `<query-id>:<os-tid>` and
  `None:<thread-name>:<os-tid>`.
- Replaced the recursive scheduler lock with `execMutex_` and
  `scheduleMutex_`, preserving weak-pointer and generation guards.

## Focused evidence

| Area | Result |
|---|---|
| Metrics/snapshot | 9/9 passed; no-op counter mutation failed |
| Reader accounting | 33/33 passed; physical/logical, predownload, hit/miss, timing, and cache-write mutations failed |
| Cancellation | 38/38 passed; empty wait token and removed pre-lookup check failed |
| Scheduler/caller ID | 25/25 passed; attach-before-unlock timed out; 200 stress iterations passed |
| Caller-ID compatibility | 3/3 passed in freshly rebuilt mono and non-mono binaries |

## Accumulated gates

| Gate | Result | Log |
|---|---|---|
| Non-mono configure | exit 0 | `/root/oss/velox/_build/debug-task017a-nonmono/configure_nonmono_pt5.log` |
| Build 16 mono targets | exit 0 | `/root/oss/velox/_build/debug/build_all_mono_pt5.log` |
| Build 16 non-mono targets | exit 0 | `/root/oss/velox/_build/debug-task017a-nonmono/build_all_nonmono_pt5.log` |
| Mono CTest | 16/16 passed | `/root/oss/velox/_build/debug/ctest_mono_pt5_r2.log` |
| Non-mono CTest | 16/16 passed | `/root/oss/velox/_build/debug-task017a-nonmono/ctest_nonmono_pt5_r2.log` |

```text
mono/non-mono target lists: identical
failed tests: 0
skipped tests: 0
not-run tests: 0
Debug benchmark targets built: 0
git diff --check: clean
placeholder scan: clean; matches were legitimate C++ `catch (...)`
```

## Worker reviews

```text
Task 1: approved
Task 2: approved after CH physical-I/O decision and two fix rounds
Task 3: approved
Task 4: approved after deterministic inline-completion test fix
Caller-ID accumulated-test closure: approved
```

## Blocker resolution

The first accumulated run found one pre-existing test that still expected the
old `None:<tid>` format. The test and two stale comments were updated and
committed. Both target binaries were freshly rebuilt before rerunning tests.
The complete mono and non-mono CTest sets then passed 16/16.

## Minor findings retained for Review 5

- Add direct tests for `ProfileEventTimeIncrement::elapsed`,
  `CachedReadBufferReadFromCacheMicroseconds`, and
  `CachedReadBufferCacheWriteMicroseconds`.
- Mark the DataSource-to-Spark flow comment as an intended Task-018 consumer.
- Document the unnamed-thread fallback `None:unknown:<tid>`.
- The timer supersede generation guard has race/stress evidence; no production
  test seam is added before Review 5 resolves the test-util decision.

## Controller review 1

```text
controller_status: accepted
task: 017A
review_scope: 43a9e6f75ffb94be38836b45fd476325665f50be..a856d836c
critical_findings: 0
important_findings: 0
minor_findings: 5
final_review: ready
```

The final Senior review traced metric storage, physical/logical byte
boundaries, predownload, write failures, token lifetime, downloader ownership,
timer interleavings, deactivate/shutdown, and mono/non-mono linkage. It
approved the complete six-commit range.

## Controller declaration

```text
Task 017A is accepted.
Velox HEAD: a856d836c
Next task: Task 018 non-TPCH phase.
TPCH remains blocked by checkpoint 018-P.
```

## Post-acceptance Task-018/019 ownership amendment

The accepted receipt above is immutable historical evidence. The later approved
hard split in `port/design/filecache-task-018-019-hard-split.md` moves the
DataSource-to-Spark consumer from Task 018 to Task 019; Task 018 is Velox-only.
