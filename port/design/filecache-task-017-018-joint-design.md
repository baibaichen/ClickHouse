# FileCache Tasks 017A/017B-018 Joint Design

## Status

```text
environment_profile: root-oss
design_scope: Tasks 017A, 017B, and 018
task_016: deferred
task_019: design deferred until Task 018 is accepted
implementation_authorized: false
```

This design replaces the current executable instructions in
`port/task/017-filecache-observability-cancellation.md` and
`port/task/018-filecache-gluten-integration.md`. The numbered tasks must be
rewritten from this design before implementation.

## 1. Goals

Task 017A makes the accepted Velox FileCache implementation production-visible
and cancellation-aware:

```text
real CH-compatible global metrics/events;
real per-query Velox IO statistics;
query cancellation propagated into FileCache waits and safe read boundaries;
CH-compatible background caller identity;
CH-compatible non-recursive scheduler locking.
```

Task 017B independently replaces the logging compatibility shell:

```text
lazy, attributed FileCache logging;
current-exception formatting;
optional Velox exception stack output;
both logger and function-name exception logging overloads.
```

Task 018 consumes Task-017A APIs:

```text
Gluten configuration and FileCacheManager ownership;
real GlutenBufferedInputBuilder selection;
query statistics propagated through RuntimeMetric -> Spark SQLMetric;
the complete Velox correctness/micro/wrapper/TPCH benchmark suite.
```

Task 019 is explicitly excluded. Spark end-to-end correctness and performance
remain required, but they will be designed only after Task 018, Review 5, and
Task 017B are accepted.

## 2. Existing debts closed by this design

### 2.1 Task-014 statistics wiring

`FileCacheBufferedInput` already owns `ioStatistics_` and `ioStats_`, but
`FileCacheInputStream` never updates them. It also never increments the CH
reader events represented by:

```text
CachedReadBufferReadFromCacheBytes
CachedReadBufferReadFromSourceBytes
CachedReadBufferCacheWriteBytes
```

Task 014 deliberately deferred raw-byte integration, but the debt was not made
an explicit gate. Task 017A closes the Velox half; Task 018 proves propagation
through Gluten.

### 2.2 Task-014 cancellation wiring

Task 012 already implemented cancellation-aware
`FileSegment::wait(offset, token)`. Task 014 hard-coded:

```cpp
fileSegment.wait(offset, folly::CancellationToken{});
```

Task 017A adds value-semantic token propagation and tests. Task 018 extracts the
real token from `ConnectorQueryCtx`.

### 2.3 Task-006 scheduler structure

Task 006 introduced `std::recursive_mutex` because it attached a Folly future
continuation while holding the task lock. An already-ready future may run that
continuation inline and reacquire the same lock.

This is implementation-induced reentrancy, not a required Velox semantic.
Task 017A restores the ClickHouse two-lock model and moves future attachment out
of the scheduling lock.

## 3. Statistics architecture

### 3.1 One fact, two ledgers

Every completed FileCache read/write fact updates two independent ledgers at
the same source location:

```text
completed fact
  -> CH-compatible global CurrentMetrics/ProfileEvents
  -> current-query Velox IoStatistics/IoStats
```

Neither ledger is derived from the other.

Example:

```text
8 MiB returned from a local FileCache file
  -> ProfileEvents::CachedReadBufferReadFromCacheBytes += 8 MiB
  -> IoStatistics::ssdRead += 8 MiB

2 MiB returned from source
  -> ProfileEvents::CachedReadBufferReadFromSourceBytes += 2 MiB
  -> IoStatistics::read += 2 MiB

2 MiB successfully appended to FileCache
  -> ProfileEvents::CachedReadBufferCacheWriteBytes += 2 MiB
  -> IoStats["fileCacheWriteBytes"] += 2 MiB
```

The global ledger answers process/cache questions. The query ledger flows to
`FileDataSource`, `RuntimeMetric`, `OperatorStats`, `TaskStats`, Gluten JNI,
and Spark SQL metrics.

### 3.2 Global storage

Preserve every existing public enum name. Do not replace the lists with a
smaller hand-selected subset.

```text
CurrentMetrics:
  signed atomic gauge per metric;
  add/sub/get/set and RAII Increment;
  current value, may increase and decrease.

ProfileEvents:
  unsigned atomic cumulative counter per event;
  increment/get and RAII elapsed-time accumulator;
  process-lifetime monotone values.
```

Use `std::memory_order_relaxed`, matching ClickHouse. Statistics never publish
or synchronize production state.

The existing 50-event Velox list is not the final reader contract. Task 017A
must preserve it and add every event used by the current CH
`CachedOnDiskReadBufferFromFile` consumer, including:

```text
cache hit/miss count;
cache/source read bytes and time;
predownload bytes and time;
cache-write bytes and time;
buffer creation and wait time.
```

The exact final list comes from the Review-4 CH consumer ledger.

### 3.3 Snapshot API

Follow the verified Presto/AsyncDataCache pattern:

```text
component owns counters;
refreshStats returns a point-in-time snapshot;
cumulative fields support subtraction;
periodic/reporting consumers retain the previous snapshot and emit deltas.
```

Task 017A provides a stable public FileCache stats type containing:

```text
snapshot gauges:
  cache size and limit;
  keys and segments;
  held/invalidated/priority entries;
  download/cleanup queue elements;
  active reserve threads.

cumulative counters:
  cache/source/write bytes;
  hit/miss count;
  predownload bytes;
  reserve attempts/failures;
  eviction bytes/segments/tries;
  important wait/write/lock timings.
```

Each field is loaded atomically, but the struct is a partially atomic snapshot,
not a transaction across every field. There is no runtime global reset.
Benchmarks calculate before/after deltas.

### 3.4 Query-level mapping

`FileCacheInputStream` updates the existing `IoStatistics` object:

| FileCache fact | Velox query statistic | Runtime metric |
|---|---|---|
| logical bytes returned after the final range clamp | `incRawBytesRead` | existing raw input metric |
| physical bytes read from local FileCache before the final range clamp | `ssdRead` | `localReadBytes` |
| physical bytes read from source before the final range clamp | `read` | `storageReadBytes` |
| physical predownload source bytes | `read` and `prefetch` | storage/prefetch metrics |
| cache write bytes | `IoStats["fileCacheWriteBytes"]` | new free-form metric |
| local/source waiting | existing IO latency counters | existing wait metrics |
| scan time | `incTotalScanTimeNs` | `totalScanTime` |

Only successfully read/returned/written bytes are recorded in their respective
fields. `CachedReadBufferReadFromCacheBytes`,
`CachedReadBufferReadFromSourceBytes`, `ssdRead`, and `read` describe physical
I/O before the final requested-range clamp, matching CH. `incRawBytesRead`
describes only the post-clamp logical bytes returned to the caller. Predownload
bytes are included in both the global source total and query `read`, additionally
increment query `prefetch`, and never increment `incRawBytesRead`. A failed
reserve may record source bytes but records zero cache-write bytes.

Task 018 extends Gluten's metric bridge for `fileCacheWriteBytes`; existing
`storageReadBytes` and `localReadBytes` continue through their current path.

The existing Gluten `pageLoadTimeNs` versus Velox `pageLoadTimeNanos` mismatch
is a separate bug and is outside Task 018.

### 3.5 Global export

Presto implements a Prometheus `BaseStatsReporter`, periodically polls
`AsyncDataCache::refreshStats`, reports current gauges directly and cumulative
deltas, and exposes `/v1/info/metrics`.

Use the same boundary, not the Presto server implementation:

```text
Task 017A:
  component-owned snapshot/delta provider compatible with StatsReporter.

Task 018:
  benchmark reads snapshots directly;
  teardown logs a final snapshot.

Deferred:
  concrete Gluten Prometheus/Spark-executor process-global Reporter.
```

Task 018 does not add a native HTTP metrics server.

## 4. Cancellation

### 4.1 Ownership

Store a copied `folly::CancellationToken`, never a raw
`ConnectorQueryCtx *`.

```text
ConnectorQueryCtx::cancellationToken
  -> GlutenBufferedInputBuilder copies token
  -> FileCacheBufferedInput stores token by value
  -> FileCacheInputStream stores/uses token
  -> FileSegment::wait receives the same token
```

### 4.2 Safe check points

Cancellation may throw only where no downloader lease or incomplete
reserve/write transaction is held:

```text
before the first FileCache lookup;
between completed segment batches;
after completing/advancing the current segment;
inside the existing FileSegment wait loop.
```

Do not add a check:

```text
between downloader election and release;
between reserve and write;
inside a cache write;
inside predownload after reservation.
```

The existing wait loop checks once per second. No new cancellation callback or
condition-variable registration mechanism is needed.

## 5. Task 017B: logging and exception stacks

Task 017B is independent of Task 017A and does not block Task 018. By user
decision Task 018 acceptance is followed by Review 5, a Tasks 003–018 whole-port
review that also closes Review-4 debt. Task 017B runs only after Review 5 is
accepted, and must complete before Task 019 design and before the overall
FileCache integration is declared production-ready.

Preserve the current logger type and both exception-logging call shapes:

```text
tryLogCurrentException(LoggerPtr, ...)
tryLogCurrentException(const char * name, ...)
```

`LOG_TEST` remains non-evaluating. Trace/debug/info formatting is lazy.
Warning/error messages retain logger-name attribution.

`getCurrentExceptionMessage(with_stacktrace)` must:

```text
format the currently handled Velox/std exception without changing it;
include the Velox exception stack only when requested and available;
return a useful fallback for unknown exceptions;
never throw while formatting/logging an existing exception.
```

`tryLogCurrentException` is `noexcept`, preserves the original exception, and
logs through either the supplied `LoggerPtr` or function/log name.

Task 017B owns only:

```text
logger_useful implementation;
exception formatting and stack behavior;
logger macro laziness and attribution;
focused tests and mutation evidence.
```

It does not own metrics, cancellation, scheduler, caller identity, Gluten, or
benchmark changes.

## 6. Task 017A: caller identity and scheduler

### 6.1 Caller identity

Restore CH format:

```text
query context:     <query-id>:<os-thread-id>
no query context:  None:<thread-name>:<os-thread-id>
```

Thread name improves background downloader diagnostics without changing the
ownership comparison model.

### 6.2 Scheduler lock model

Replace the single recursive mutex with the CH two-lock structure:

```text
schedule_mutex:
  scheduled/delayed/deactivated state;
  pending immediate/delayed request;
  generation and timer handle state.

exec_mutex:
  serializes callback execution;
  lets deactivate wait for a running callback.
```

Create timer intent under `schedule_mutex`, release the lock, attach the Folly
future continuation, then reacquire the lock to publish the handle if the
generation remains current.

The continuation may run inline, but it no longer runs while the attaching
thread holds `schedule_mutex`.

Retain `weak_ptr + generation` as a Velox asynchronous-lifetime adaptation.
ClickHouse's central queue owns a strong task reference; Folly futures do not.

## 7. Gluten ownership and configuration

### 7.1 Configuration

Use AsyncDataCache-style naming:

```text
spark.gluten.sql.columnar.backend.velox.fileCacheEnabled=false
spark.gluten.sql.columnar.backend.velox.fileCachePath=<required when enabled>
spark.gluten.sql.columnar.backend.velox.fileCacheSize=<capacity limit>
spark.gluten.sql.columnar.backend.velox.fileCacheMaxSegmentSize=<segment limit>
spark.gluten.sql.columnar.backend.velox.fileCacheBackgroundDownloadThreads=<default>
```

Do not expose the complete CH settings surface in the first integration.
Benchmark binaries construct their own detailed configs.

`fileCachePath` is an absolute, stable, dedicated directory. Do not append the
random `cache.<uuid>` suffix used by AsyncDataCache: FileCache reloads metadata
across process restarts. Concurrent executors misconfigured to use the same
directory fail closed through initialization/status ownership.

`fileCacheSize` is a logical upper bound, not preallocated disk space. Do not
copy AsyncDataCache's startup check requiring all configured capacity to be
currently free.

### 7.2 Mutual exclusion

If both existing `cacheEnabled` and new `fileCacheEnabled` are true, fail
backend initialization with a clear configuration exception.

Do not:

```text
silently choose one cache;
stack two cache layers;
fall back to direct input after an initialization failure.
```

### 7.3 Ownership and initialization

`VeloxBackend` owns:

```text
shared_ptr<FileCacheManager> fileCacheManager_;
shared_ptr<MemoryPool> fileCacheMemoryPool_;
```

Initialize after the Velox memory manager and local filesystem are ready.
Create and initialize all FileCache instances before publishing the process
singleton. An exception leaves no global pointer and propagates to startup.

### 7.4 Teardown

Follow the accepted Manager contract:

```text
fileCacheManager_->shutdown();
FileCacheManager::setInstance(nullptr);
fileCacheManager_.reset();
fileCacheMemoryPool_.reset();
```

This runs before Gluten executor and global memory-manager destruction.

## 8. Builder selection

At the builder boundary:

```text
installed FileCacheManager
  -> derive key via FileCacheFileIdentity
  -> copy ConnectorQueryCtx cancellation token
  -> pass existing IoStatistics/IoStats/executor/options
  -> return FileCacheBufferedInput

no FileCacheManager and AsyncDataCache exists
  -> return CachedBufferedInput

neither exists
  -> return GlutenDirectBufferedInput
```

Store `manager->getDefault` once as a `shared_ptr`; never take a raw pointer
from a temporary.

Tests must call the real builder and assert the dynamic result type. Checking
only that the singleton exists is false-green.

## 9. Benchmark architecture

### 9.1 Placement

All correctness and performance binaries live in Velox:

```text
CacheVerify;
FileCache core/seek microbenchmarks;
FileCacheBufferedInput microbenchmark;
same-process direct/cbi/filecache wrapper A/B;
TPCH benchmark.
```

Gluten owns only configuration, Manager lifecycle, Builder selection, and
native integration tests.

Task 019 later owns real Spark-to-Gluten end-to-end correctness/performance.

Every benchmark target must be freshly built and run from a RelWithDebInfo or
Release build. Debug builds are valid for focused functional tests only and
must never supply benchmark results.

### 9.2 Reuse policy

Use `baibaichen/ch-filecache` and
`/root/chang/OneDrive/share_data/local-cache/benchmark` as references.

Reuse workload generation, result tables, phase handling, and script fairness
controls. Do not cherry-pick stale FileCache APIs or machine-specific paths.

Extend the existing `QueryBenchmarkBase` with explicit:

```text
direct
cbi
filecache
```

input modes and shared lifecycle/reset logic. Do not create a duplicate TPCH
harness.

### 9.3 Correctness gate

Before performance:

```text
read deterministic ranges through direct/cbi/filecache;
compare exact returned bytes or streaming checksums;
on mismatch report the first differing range/byte;
do not publish performance output after a correctness failure.
```

TPCH validates result rows and result checksum against direct input.

Before any TPCH source is copied, target is built, data is required, or command
is run, stop after the completed non-TPCH Task-018 phase and obtain explicit
user approval. Resume TPCH in a fresh Task-018 Worker only after that approval.

### 9.4 Performance waves

Use a dedicated RelWithDebInfo benchmark build.

```text
Wave 1: existing seek and FileCache core microbenchmarks.
Wave 2: FileCacheBufferedInput microbenchmark.
Wave 3: same-process direct/cbi/filecache wrapper A/B.
Wave 4: TPCH q01/q09/q21 smoke, then all 22 queries.
```

Wrapper dimensions:

```text
sequential and zipfian;
1 MiB and 8 MiB reads;
cold and hot cache phases;
at least 3-5 measured passes;
median and within-run delta.
```

TPCH:

```text
num_splits_per_file=1;
num_drivers=4;
three rounds: one cold, two hot;
all engines use the same queries/data/output schema.
```

### 9.5 Cache-directory cleanup

Orchestration scripts own run-specific cache directories.

```text
canonicalize root and run path;
require the run path to be a child of the configured root;
require an ownership sentinel before deletion;
delete before the cold phase;
use shell traps for normal exit and handled signals;
delete after the cold+hot run;
clean validated leftovers on the next startup.
```

Benchmark binaries never recursively delete an arbitrary user path.

### 9.6 Acceptance

Stage one:

```text
correctness and path attribution are hard gates;
performance runs establish baseline distributions and noise bands;
no arbitrary percentage regression threshold.
```

Stage two begins only after repeated stable history and adds explicit
performance thresholds.

## 10. Testing and mutation gates

### 10.1 Task 017A

Focused tests cover:

```text
all existing CurrentMetrics names and add/sub/get/RAII behavior;
all existing and reader-required ProfileEvents;
timer accumulation;
snapshot current values and cumulative subtraction;
one fact updating both global and query ledgers;
cache/source/predownload/write mapping;
copied cancellation-token lifetime;
cancellation before lookup and during FileSegment wait;
no cancellation while downloader lease/reserve-write is active;
caller-id formats;
plain two-lock scheduler behavior and ready-future inline completion;
```

Every material behavior receives a buildable mutation RED. Both mono and
non-mono builds must freshly rebuild every registered `velox_ch_*` target before
accumulated CTest.

### 10.2 Task 017B

Focused tests cover:

```text
LOG_TEST argument non-evaluation;
lazy trace/debug/info argument evaluation;
warning/error logger attribution;
VeloxException message with and without stack;
std::exception formatting;
unknown-exception fallback;
LoggerPtr and function-name tryLogCurrentException overloads;
formatting/logging failure never replaces the original exception.
```

Every behavior receives a buildable mutation RED. Task 017B must pass in mono
and non-mono builds and preserve all Task-017A tests.

### 10.3 Task 018

Gluten native tests cover:

```text
disabled config leaves FileCache absent;
missing/invalid path and conflicting cache configs fail startup;
initialization failure publishes no singleton;
real builder returns FileCacheBufferedInput;
AsyncDataCache and direct paths remain correct when FileCache is absent;
key derivation uses FileCacheFileIdentity;
IoStatistics, IoStats, cancellation token, executor, and options reach the
  constructed input;
teardown order and idempotence;
first-time root-oss configure uses the vcpkg toolchain and current Velox mono
  build.
```

Benchmark correctness tests and smoke runs are separate from the Gluten native
lifecycle tests.

## 11. Error handling

```text
configuration errors: fail startup;
FileCache initialization errors: propagate, no fallback;
builder construction errors: propagate;
statistics updates: must not throw or alter read/write state;
cancellation: throw only at lease-free safe points;
shutdown: idempotent, preserve strict resource order;
benchmark correctness failure: stop before timing;
cleanup validation failure: refuse deletion.
```

No broad catch or success-shaped fallback is introduced.

## 12. Implementation order

1. Rewrite Task 017A and Task 017B from this design.
2. Implement and independently accept Task 017A.
3. Rewrite Task 018 against the accepted Task-017A API.
4. Implement Velox benchmark adaptations and Gluten integration under Task 018.
5. Independently accept Task 018.
6. Implement and independently accept Task 017B.
7. Only after Task 017B is accepted, design Task 019.

Real kernel `O_DIRECT` integration remains a recorded forward obligation but is
deferred and does not block Tasks 017-018.

## 13. Explicit exclusions

```text
Task 016 Ephemeral writer;
Task 019 Spark end-to-end design or implementation;
concrete Gluten Prometheus/native HTTP metrics server;
existing Gluten pageLoadTimeNs/pageLoadTimeNanos bug;
real kernel O_DIRECT integration;
simultaneous/double-layer AsyncDataCache + FileCache;
all CH settings not listed in the minimal Gluten configuration;
hard performance regression thresholds before baseline history exists.
```
