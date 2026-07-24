# Tasks 003–018 Whole-Port Review — Final Verdict

```text
reviewer_role:                          Controller — canonical Review 5 final verdict
review_scope:                           Tasks 003–018 whole FileCache port
ch_head:                                31aa159dd61 (ch-filecache, clean)
velox_head:                             cda6c03703 (filecache, clean)
task_012_corrective:                    26325e8a32 (folly::call_once, included)
task_014_corrective:                    cda6c03703 (truncation/rename recovery, included)
independent_report:                     .superpowers/sdd/review5-final-independent-report.md
evidence_baselines:                     port/task/fullreview/root-oss/5/evidence/003-018-baselines.md
evidence_review4_closure:               port/task/fullreview/root-oss/5/evidence/review-4-closure.md
evidence_contract_ledger:               port/task/fullreview/root-oss/5/evidence/017a-018-contract-ledger.md
evidence_parity_matrix:                 port/task/fullreview/root-oss/5/evidence/017a-018-parity-matrix.md
```

---

## Summary

The ClickHouse FileCache port to Velox (living under `velox/ch/`) is a faithful,
well-tested port whose production read path is genuinely wired and exercised
end-to-end. An independent reviewer re-derived the CH FileCache contract from CH
production source, traced every highest-risk surface through real Velox source,
real callers, behavior-discriminating tests, and the accepted Task 018 CSV
artifacts, and found **no Critical and no Important implementation defects**. The
two Review-5 correctives (`folly::call_once` at `26325e8a32`, truncation/rename
recovery at `cda6c03703`) are present at source and match their approved designs.
Three Low, non-blocking observations and the pre-agreed six D4/D6 `UNPROVEN`
forward-debt rows are the only open items.

**Verdict: `accepted`.**

---

## Contract

The promised behavior was re-derived from CH production source (oracle) and
every promise mapped to a Velox carrier. All accepted claims are present at
source; no claim is claimed-but-absent.

**Statistics.** CH `CachedOnDiskReadBufferFromFile.cpp` records physical
cache/source bytes at the current read step *before* the last-segment clamp, and
never lets predownload bytes reach the caller's logical read accounting. The
Velox port reproduces this exactly: physical hit/miss + bytes recorded pre-clamp
at `FileCacheInputStream.cpp:849-862`; last-segment clamp affects only the
returned (logical) size (`:884-893`); logical `rawBytesRead` incremented
post-clamp only (`:899`); predownload counts physical source + predownload
counters and query `read()`/`prefetch()` but never `rawBytesRead`
(`:767-774`). This matches CH's separate physical/logical accounting streams.

**Cancellation.** CH observes cancellation via `throwIfKilled` at 1s slices
inside a 60s `FileSegment::wait` deadline. Velox copies the query token by value
(`FileCacheInputStream.cpp:85`) and checks it at three safe points — pre-lookup
(`:264-268`), inside `FileSegment::wait` 60s/1s slices (`FileSegment.cpp`
wait loop), and post-segment (`:941-942`) — passing the real token to
`FileSegment::wait`. Mechanism differs (`folly::CancellationToken` vs
`throwIfKilled`); observable behavior is identical.

**`FileSegment` downloader state machine.** CH `getOrSetDownloader` /
`resetDownloadingStateUnlocked` / `resetDownloader` are faithfully ported at
`FileSegment.cpp:273-345` (EMPTY/PARTIALLY_DOWNLOADED → DOWNLOADING under
`downloader_id = caller_id`; DOWNLOADING → DOWNLOADED/PARTIALLY/EMPTY on reset;
downloader-only assertion).

**External truncation / rename self-heal (Task 014).** CH bypasses a cache file
shorter than its recorded downloaded size and re-fetches. Velox reproduces this
at `FileCacheInputStream.cpp:376-403` (physical-size < `getDownloadedSize()` →
warn + reset + return `nullptr` → source bypass) and adds the concurrent
`<offset>→<offset>_<size>` rename retry at `:340-360` (catch `kFileNotFound`,
recompute path under `fileSegment.lock()`, retry once, else rethrow).

**Init once-guard (Task 012).** CH `std::call_once` is mapped to
`folly::once_flag initialize_once_flag` (`FileCache.h:372`) + `folly::call_once`
(`FileCache.cpp:433`), with the old `std::mutex initialize_mutex` / `bool
initialize_completed` fully removed. Retry-on-exception semantics documented at
`FileCache.cpp:430-433`.

Evidence: `evidence/017a-018-contract-ledger.md` §A–C; `evidence/017a-018-parity-matrix.md` §4; independent report §Contract.

---

## Impacted Surface

**Production caller is real and wired.** `createBufferedInput` in
`velox/connectors/hive/HiveConnectorUtil.cpp:655-713` routes Hive scans through
`ch::FileCacheBufferedInput` whenever `ch::FileCacheManager::getInstance()` is
non-null, constructing the request context, origin, and file identity and passing
the real `connectorQueryCtx->cancellationToken()`. It fails closed on
double-install: `VELOX_USER_CHECK_NULL(connectorQueryCtx->cache(), "FileCache
and AsyncDataCache cannot both be installed")` (`:659-661`). The connector links
the four filecache libraries (`velox/connectors/hive/CMakeLists.txt:92-95`). This
is genuine wiring, not test-only.

**The Hive dispatch is exercised by a real integration test AND the benchmark.**
`velox/connectors/hive/tests/HiveFileCacheBufferedInputTest.cpp` drives the
production `createBufferedInput` across every branch:
`NoManagerNoCbiSelectsDirect` (`:245`), `NoManagerWithCbiSelectsCachedBufferedInput`
(`:263`), `ManagerSelectsFileCacheBufferedInput` (`:284`), `ManagerAndCbiFailClosed`
(`:303-326`, asserts the fail-closed throw), `MappingTest` (`:333`), and
`RealMissFillHit` (`:382-433`, real miss→source→write→hit). The Task 018
benchmark installs a real manager (`velox/benchmarks/AbBenchmarkMain.cpp:94-95`)
so TPC-H scans route through this same dispatch, producing positive
`cache_read_mib` and 100% `hit_pct` in warm rounds.

**Manager / factory ownership and shared services.** `FileCacheManager` owns the
worker pool, scheduler, opened-file cache, and factory in a deliberate member
order so destruction is `factory_` (and its `FileCache`s) first, then services,
with `mutation_mutex_` destroyed last (`FileCacheManager.h:165-191`). The
constructor starts no `this`-capturing background work
(`FileCacheManager.cpp:153-167`). The non-owning global pointer contract (caller
does `shutdown()` → `setInstance(nullptr)` → drop `shared_ptr`) is documented
(`FileCacheManager.h:112-122`) and followed by the benchmark
(`AbBenchmarkMain.cpp:112-118`).

**Manager-owned `OpenedFileCache` trace.** The manager constructs
`openedFileCache_` (`FileCacheManager.cpp:161`) and injects it into the factory's
`RuntimeServices` (`:162-163`). The factory wires **only the invalidator**:
`[opened](const std::string & path){ opened->remove(path); }`
(`FileCacheFactory.cpp:71,91`). The production cache-read open goes through
`FileCache::createCacheReadBuffer` → the injected `open_read_file` =
`fs->openFileForRead(path)` (`FileCache.cpp:396-401`, `FileCacheFactory.cpp:89-90`),
i.e. **`OpenedFileCache::get()` is never called on the production read path**; the
invalidation callback therefore runs against an always-empty cache. This is a
documented design choice (`port/2-file-cache/09-filecache-file-segment-design.md:254-256`)
and no accepted parity row claims read-path wiring. See Findings (Low-1).

**Reader / owned-buffer / query-pool carriers.** The stream captures the query
pool and query-context holder once and never resets them on seek
(`FileCacheInputStream.cpp:62,75-76`); the holder outlives `readInfo_` by
declaration order. The three segment-batch acquisition modes (`tempCacheOnly` /
`readIfExistsOtherwiseBypass` / default `getOrSet`) are all present (`:276-303`).

Evidence: `evidence/003-018-baselines.md` §4; `evidence/017a-018-contract-ledger.md` §B; independent report §Impacted Surface.

---

## Failure / Lifecycle

- **Startup / init failure.** `initialize()` holds `mutation_mutex_` for the
  whole operation, initializes each unique cache once, and on any throw
  deactivates the already-initialized caches and leaves state `Created` before
  re-throwing (`FileCacheManager.cpp:258-294`). Un-initialized caches hold no
  background work; the create-failure destruction path is safe.
- **Config reload.** `applyConfigs` runs registry mutation, new-cache init,
  per-cache apply, budget recompute, and pool resize under the single
  `mutation_mutex_` (`FileCacheManager.cpp:296-...`), so no concurrent
  `getOrCreate`/remove/clear/shutdown can overwrite a newer budget or resize a
  stopped pool. (D4 mechanism — implemented, user-deferred.)
- **Shutdown.** Serialized under `mutation_mutex_`, strict order caches →
  scheduler → worker pool → handles, idempotent on `Shutdown`
  (`FileCacheManager.cpp:454-483`). Diagnostic `isMutationLockFree()` probes from
  a separate thread with `ret`/`try_lock` (`:232-252`).
- **Cancellation / partial progress.** Three safe points plus the inner
  `FileSegment::wait` slice; each safe point placed where no downloader lease or
  in-flight reserve/write is held (`FileCacheInputStream.cpp:264-268,941-942`).
- **Exception during read.** `readNextChunk` never returns a canceled/failed
  reader to the segment: on throw it drops the reader, releases the downloader
  with the reader withdrawn, and propagates (`:1034-1046`).
- **Downloader handoff / reader detach (D6).** Output buffer borrowed for the
  read and un-borrowed immediately after (`:1029,1032`). On the reusable-download
  handoff the reader's owned buffer is released
  (`state_->reader->releaseOwnedBuffer()`, `:1052-1063`) precisely because a
  background-download worker may reuse the reader after the query pool is torn
  down. Background download is enabled by default
  (`FileCacheReadOptions.h:42 allowBackgroundDownload = true`), so this reuse
  surface is genuinely reachable — consistent with D6 being real (not
  theoretical) forward debt. (D6 mechanism implemented, user-deferred.)
- **Concurrent rename / external truncation.** Handled by the Task 014
  corrective (rename retry under segment lock; truncation bypass leaving the
  segment in place to avoid racing `tryIncreasePriority` without the priority
  lock — `FileCacheInputStream.cpp:387-402`).
- **Cache write failure.** `writeCache` bypasses on `ENOSPC`/`EDQUOT`
  unconditionally and on other errors only when `skipCacheOnDiskFailure()` is set
  (setting-gated, matches CH), else re-throws (`:679-694`). Not a broad silent
  fallback.

Evidence: `evidence/017a-018-contract-ledger.md` §C; `evidence/review-4-closure.md` §2.2–2.8; independent report §Failure / Lifecycle.

---

## Evidence

- **Statistics proven by behavior-discriminating tests.**
  `FileCacheBufferedInputTest.LastSegmentClampSeparatesPhysicalAndLogicalBytes`
  reads 7 logical bytes from an 8-byte physical segment and asserts
  `CacheWriteBytes`/`ReadFromSourceBytes`/`read()` advance by 8 but
  `rawBytesRead` by 7. `PredownloadUpdatesReadPrefetchButNotRawBytes` asserts the
  3 predownload bytes hit the predownload/source/`prefetch()` counters but
  `rawBytesRead` advances only by the returned bytes. Both are true behavior
  discriminators.
- **E2E read path.** `FileCacheE2ETest` installs a real manager and exercises
  miss/fill/hit, cache-only-miss, bypass mode, back-up, skip across boundary, and
  a discarded-enqueue no-UAF case through the real reader stack.
- **Benchmark correctness gate (four-driver addendum).** All 198 rows have
  `result_match=1` (verified directly via
  `tmp/parallel_verified4_q15fixed_results/tpch_{direct,cbi,filecache}.csv`),
  which proves FileCache == direct == CBI == reference (I/O transparency). q15
  returns exactly 1 row per cell after the `aggregate-once-then-topNRank("dense_rank",…,1)`
  fix (`velox/exec/tests/utils/TpchQueryBuilder.cpp:1727-1752`). Fairness: same
  binary and plan, only `--input_source`/cache args differ; sentinel-authenticated
  cleanup; RelWithDebInfo build (`_build/relwithdebinfo/CMakeCache.txt`).
- **q11 caveat.** Four-driver q11 = 0 rows with `result_match=1`. One-driver
  q11 = 3,203,218 rows. The 0-vs-3,203,218 disparity is a TPC-H/parallel
  floating-point aggregation artifact outside the FileCache contract. `result_match`
  compares each backend against a common in-process reference exhibiting the same
  artifact, so `result_match=1` validly proves FileCache does not alter results
  versus direct/CBI. The Low-2 finding corrects the receipt misstatement.
- **Registration.** All new 017A/018 tests are registered:
  `FileCacheBufferedInputTest`, `velox_ch_filecache_e2e_test`,
  `velox_ch_scheduler_test`, `velox_ch_metrics_snapshot_test`,
  `velox_ab_benchmark_schema_test`, `velox_hive_filecache_buffered_input_test`.
- **Build plumbing.** The Arrow/Parquet/CMake changes add only
  `VELOX_ENABLE_ARROW_TESTING` (default ON, no behavior change) and static-zstd
  linkage — benign, no format or correctness impact.
- **Review-4 closure.** `L-CALLONCE-01` closed (UNPROVEN → EQUIVALENT) at
  `26325e8a32`; `G-CACHEBUF-01` and `G-CACHEOPEN-RENAME-01` closed
  (UNPROVEN → EQUIVALENT) at `cda6c03703`. All other Review-4 findings are
  dispositioned per `evidence/review-4-closure.md`.

Evidence provenance: `evidence/003-018-baselines.md`, `evidence/review-4-closure.md`,
`evidence/017a-018-contract-ledger.md`, `evidence/017a-018-parity-matrix.md`;
independent review: `.superpowers/sdd/review5-final-independent-report.md`.

---

## Findings

No **Critical** and no **Important** implementation defects.

### 💡 Low-1 — Manager-owned `OpenedFileCache` is not on the production read path (Owner: Task 013)

`OpenedFileCache::get()` is never invoked in production: cache-file reads open a
fresh `velox::ReadFile` via `FileCache::createCacheReadBuffer` →
`fs->openFileForRead` (`FileCache.cpp:396-401`, `FileCacheFactory.cpp:89-90`),
while only the invalidator `opened->remove(path)` is wired
(`FileCacheFactory.cpp:91`; called from `FileCache.cpp:407-408`,
`Metadata.cpp:1219-1220`). Consequently the CH cross-reader FD-sharing
optimization is not delivered and the wired invalidation runs against an
always-empty cache (dead in practice). No correctness impact; no accepted parity
row claims read-path wiring; within documented design latitude
(`port/2-file-cache/09-filecache-file-segment-design.md:254-256`). **Non-blocking.**
Forward to Task 013: either wire `OpenedFileCache::get()` into
`createCacheReadBuffer` for CH-parity FD sharing, or document the component and
invalidation as intentionally read-unwired so the dead complexity is not mistaken
for wired behavior.

### 💡 Low-2 — Task 018 receipt misstates the one-driver q11 row count (Owner: Task 018) — **resolved by this change**

`port/task/result/018-filecache-velox-benchmark-result.md:935` previously stated
"q11 returns zero rows in the accepted one-driver baseline", but
`tmp/one_driver_ab_results/tpch_{direct,filecache}.csv` show q11 = **3,203,218**
rows at one driver. Four-driver q11 = 0 / `result_match=1` remains valid
backend-equivalence evidence; the misstatement only made q11 look more stable
across driver counts than it is and did not change the correctness verdict.
The receipt sentence has been corrected to reflect the actual one-driver count
and the true 0-vs-3,203,218 instability as part of this Review 5 verdict change.

### 💡 Low-3 — CSV `bytes_read` sources leaf `rawInputBytes`, not stream `rawBytesRead` (Owner: Task 018)

`AbBenchmarkBase.cpp:345-354` fills `bytes_read` from `TaskStats` leaf-scan
`rawInputBytes` (physical scan I/O), which differs from the stream-level
`rawBytesRead`. This is intentional (scan-level bytes reflect true storage I/O)
and documented in the integrated ledger. **Non-blocking.** No change required.

---

## Missing Context / Blind Spots

- ⚠️ **D4 / D6 (six `UNPROVEN` rows) — waiting_for_user, non-blocking forward
  debt.** `D-INIT-01`, `E-GETORCREATE-01`, `E-CREATE-01`, `E-UPDCFG-01`
  (governed by R2-D4) and `P-RB-SETDETACH-01`, `G-NEXTIMPL-01` (governed by
  R2-D6). Implementations are present and correct-looking at source
  (`FileCacheManager.cpp:296-...,454-483`; `FileCacheInputStream.cpp:1029-1063`),
  but R2-D4/R2-D6 have no user approve/reject/modify decision, so the rows stay
  UNPROVEN by design. Per binding user decision these are **not** a failed verdict
  gate; they remain in the 215-row denominator as non-blocking forward debt.
  Closing them requires the user's R2-D4 / R2-D6 decision plus the focused
  serialization/lifetime tests that decision would authorize.
- ⚠️ **`SD4-EVIDENCE` — evidence debt (unproven).** No focused test proves that
  no iterator / mapped-value reference / address survives an F14 `MetadataBucket`
  mutation (`MetadataTest.cpp` has no SD4 probe). `R-BUCKETMAP-01` stays
  EQUIVALENT resting on the by-construction `shared_ptr<KeyMetadata>` stability
  argument. **Non-blocking**; a focused no-reference-across-mutation test would
  close it.
- ⚠️ **Priority / eviction core (LRU/SLRU/EvictionCandidates).** Unchanged by
  017A/018 and validated under Review 4 + the Review-5 closure ledger. The
  independent reviewer confirmed the reader respects the priority lock but did
  not re-audit the frozen eviction core, which is out of the 017A/018 delta.
- ⚠️ **Performance is local-only.** FileCache/Direct ≈ 1.062, FileCache/CBI ≈
  1.116 warm on an unisolated WSL2 host with two warm samples — direction is
  consistent, magnitude is low-confidence. Honestly disclosed; not a
  production-representative claim.

---

## Final Verdict

```text
review_status:                          accepted
review_scope:                           Tasks 003-018
ch_head:                                31aa159dd61
velox_head:                             cda6c03703
parallel_four_driver_addendum_status:   accepted
review_4_closure_status:               accepted (L-CALLONCE-01 closed at 26325e8a32;
                                        G-CACHEBUF-01 and G-CACHEOPEN-RENAME-01 closed
                                        at cda6c03703; all other Review-4 debt
                                        dispositioned in evidence/review-4-closure.md)
critical_findings:                      0
important_findings:                     0
low_findings:                           3 (Low-1: OpenedFileCache read-path unwired,
                                         non-blocking, forward Task 013;
                                         Low-2: receipt q11 misstatement, corrected
                                         by this change;
                                         Low-3: bytes_read leaf vs stream metric,
                                         documented intentional, non-blocking)
unproven_rows:                          6 (R2-D4 × 4, R2-D6 × 2) — non-blocking
                                        forward debt, waiting_for_user; preserved in
                                        215-row denominator; not reclassified
false_green_detected:                   0
task_017b_authorized:                   true
implementation_authorized:              false (stale plan must be rewritten and independently reviewed)
```

Tasks 003–018 are **accepted**. Production wiring is real and exercised, the CH
contract is reproduced with source-level fidelity, the accepted Review-5
correctives are present, and every material correctness/performance claim maps to
a behavior-discriminating test, log, or CSV artifact. The six D4/D6 rows remain
`UNPROVEN` strictly because their governing user decisions are pending; they are
reported as non-blocking forward debt and do not gate the verdict.

`task_017b_authorized: true`. Task 017B implementation may **not** begin
immediately. The next required steps before implementation are: (1) written-spec
review of the binding design
(`port/design/filecache-task-017b-logging-exception-stack.md`), (2) stale plan
rewrite of `port/task/017b-filecache-logging-exception-stack-plan.md`, (3)
independent plan review, and (4) explicit Controller authorization of
`implementation_authorized: true`. See
`port/task/017b-filecache-logging-exception-stack.md`.
