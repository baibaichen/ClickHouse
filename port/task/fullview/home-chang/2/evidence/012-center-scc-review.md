# Full Review (R2, §D + §3) — Task 012: Center SCC (`FileCache`/`FileSegment`/`Metadata`/`QueryLimit`)

> READ-ONLY controller sweep. Contracts re-derived from real CH callers (file:line); receipts/tests
> NOT trusted as truth. New R2 axis: BOTH consumer-observable semantics AND §3 internal structure
> (state representation, lock structure + ORDER, container stability, ownership/lifetime, async shape).
>
> CH baseline: `/home/chang/SourceCode/ClickHouse/src/Interpreters/FileCache/`
> Velox impl:  `/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/`
> Inputs read: authoring guide §3/§D, R2 ledger `011-014-consumer-contract-ledger.md`,
> signed context `012-filecache-core-scc.md`, round-1 decisions `1/003-010-review-decisions.md`.

## 0. Verdict

**Task 012 — ACCEPT.** The Velox center SCC is a faithful, near-line-for-line literal translation of
the CH source. Every §3 structural invariant that the round-1 sign-off pinned (SD3 `std::map`
KeyMetadata, SD5 `std::list`, SD4 F14 metadata bucket under the no-escape invariant, SD1 ShardedMap,
LockedKey member order) is preserved and — critically — the SD4/SD1 invariant is not merely asserted
in a comment but **actually holds in every consumer I traced**. The errno reconcile contract, the
hold-space split, the FileSegment state machine, and the QueryLimit doomed-context ordering all match
CH. No reopen required.

- **CONFIRMED findings: 0 defects** (0 drift, 0 hole, 0 unregistered structural deviation).
- **PLAUSIBLE / advisory notes: 4** (all benign or already-signed; itemized in §4). None block accept.
- **SD-012-3 adjudicated** (see §3): benign, same class as signed SD1 — accept.

## 1. §3 Structural adjudication (the R2 point)

### SD-012-1 — `MetadataBucket` → `folly::F14FastMap` (headline; self-labelled "SD4") — **ACCEPT**
`Metadata.h:331` `struct MetadataBucket : folly::F14FastMap<FileCacheKey, KeyMetadataPtr, FileCacheKeyHash>`.
CH is `std::unordered_map<FileCacheKey, KeyMetadataPtr>` (`Metadata.h:269`, node-stable). This is a
guarantee-changing deviation (F14 relocates the mapped `KeyMetadataPtr` slot on rehash) and is exactly
the registered SD4 item: allowed ONLY if review proves no iterator / mapped-value reference /
mapped-value address survives a bucket mutation.

**I read every `MetadataBucket` consumer in `Metadata.cpp` and the invariant HOLDS (CONFIRMED):**
- `getKeyMetadata` (`Metadata.cpp:326-368`): under `bucket.lock()`, on miss does `bucket.emplace(...)`,
  then immediately **copies** `it->second` into `KeyMetadataPtr result` (line 352). The iterator `it`
  is never dereferenced after the emplace that could rehash; the escaping value is a `shared_ptr`
  copy, and `KeyMetadata` pointee is stable. The `on_client_access` callback runs **after releasing
  the bucket lock** using only `origin`/`result`, no iterator.
- `removeEmptyKey`/`removeKey`/`removeAllKeys`/`cleanupThreadFunc` (`:622,595,560,717`): hold
  `bucket.lock()` across the whole sequence; the only bucket mutation is the terminal
  `bucket.erase(it)` whose return value `next_it` is used to continue — the classic node-stable-safe
  pattern that is also correct for F14 (erase returns the next valid iterator, and nothing is
  dereferenced across an intervening insert).
- `IteratorImpl`/`BatchedIteratorImpl` (`:404,486`) and `iterate` (`:378`): hold `bucket.lock()` for
  the whole batch, copy `it->second` into a `key_metadata` local before touching key/segment state;
  no insert/erase happens while a bucket iterator is live.
- `isEmpty`/`removeSharedOrigins` do not touch the bucket via escaping references.

No consumer retains a `KeyMetadataPtr&` or `MetadataBucket::iterator` across an insert/rehash of the
same bucket. **SD1-class invariant NOT violated. SD4 is satisfied — accept.**

### SD-012-2 — `KeyMetadata : private std::map<size_t, FileSegmentMetadataPtr>` — **ACCEPT (preserved)**
`Metadata.h:124` keeps the ordered node-stable `std::map` (SD3). `lower_bound` (`:442`),
`hasIntersectingRange` (`Metadata.cpp:1275` with `std::prev(it)`), range adjacency, and iterator
stability across sibling insert/erase are all load-bearing and preserved. Matches CH exactly.

### SD-012-3 — `FileCacheQueryLimit::query_map` container — **ADJUDICATED: ACCEPT (benign)**
The ledger left this UNVERIFIED. I read both `QueryLimit.h`:
- Velox `QueryLimit.h:115` `using QueryContextMap = folly::F14FastMap<String, QueryContextPtr>`;
  CH `QueryLimit.h:84` `std::unordered_map<String, QueryContextPtr>`.
- Also `QueryContext::Records` (`QueryLimit.h:89`) is `folly::F14FastMap<FileCacheKeyAndOffset,
  IteratorPtr, ...>` vs CH `std::unordered_map` (`QueryLimit.h:62`).

Both are guarantee-changing (F14 relocates values on rehash) but **benign, same class as the SIGNED
SD1/SD2 ShardedMap ruling**: (1) mapped values are `shared_ptr` (`QueryContextPtr`/`IteratorPtr`), so
the pointee is stable; (2) `query_map` is guarded by a dedicated leaf `query_map_mutex` and every
access holds it; (3) no consumer escapes a `QueryContextPtr&`/iterator across a mutation —
`getOrSetQueryContext` (`QueryLimit.cpp:88`) copies out under the mutex, `removeQueryContext`
(`:49-82`) does `doomed = std::move(iter->second); query_map.erase(iter)` under the mutex and returns
the `shared_ptr` by value; `QueryContext::remove` does `records.erase({key,offset})` (`:140`) with no
live reference across it. **Adjudication: registerable but benign; accept under the SD1 no-escape
contract.** (Recommend adding a one-line row to the structural-deviation ledger for bookkeeping
parity with SD1, but this is not a defect.)

### Lock structure + ORDER — **ACCEPT**
Global order `CachePriorityGuard > CacheMetadataGuard > KeyGuard > FileSegmentGuard` with the
independent `CacheStateGuard` is respected in `doTryReserve`/`doEviction` (`FileCache.cpp:1036+`):
state lock taken for fast-path size accounting, write lock for `afterEvictWrite` + `removeEntries` +
`add`, then state lock again for `afterEvictState` + `incrementSize`. `LockedKey` member order
(`Metadata.h:498-499`: `key_metadata` before `lock`) is preserved so the KeyGuard releases before the
`shared_ptr` drops — a MetadataTest static_assert also guards it. `removeFileSegmentImpl` accesses
`file_segment->queue_iterator` **directly** (comment `Metadata.cpp:1197-1199`) to avoid re-entering
the non-recursive `FileSegmentGuard` it already holds — matches CH's direct-member access.

### FileSegment state machine — **ACCEPT (byte-faithful)**
`getOrSetDownloader` election, `resetDownloadingStateUnlocked` (EMPTY/PARTIAL/DOWNLOADED branch),
`setDownloadedUnlocked` → `renameToIncludeSizeInNameUnlocked` (best-effort rename, keeps legacy name
on failure, `size_in_filename` false→true only), `setDownloadFailedUnlocked` (keeps downloader_id,
drops writer+reader, → NO_CONTINUATION), `detach`/`setDetachedState` (DETACHED → reset key_metadata →
reset queue_iterator → cancel writer → reset download_data), and `shrinkFileSegmentToDownloadedSize`
all match CH line-for-line (`FileSegment.cpp` Velox vs CH). `download_data` lazy struct, atomics, and
member order match CH `FileSegment.h`. `FileSegmentInfo.h` enum order and `time_t
download_finished_time` are exact (contradicting the illustrative Step-9 `steady_clock` spec — CH
header wins per the signed amendment).

## 2. Ledger candidate resolution (H-012*/O-012 + already-signed items)

| Item | Adjudication |
|---|---|
| **H-012-a** `getSizeForBackgroundDownload` public wrapper | **CLOSED.** Present: `FileSegment.h:229` + `FileSegment.cpp:862`, delegates to `...Unlocked`. |
| **H-012-b** Metadata reconciliation `removeEmptyKey`/`removeFileSegmentImpl` | **CLOSED.** Both ported and faithful (`Metadata.cpp:622,1183`). |
| **H-011-b** hold-space split (consumed in 012 tryReserve) | **CLOSED.** `collectEvictionInfo`→`afterEvictWrite`(write lock)→`afterEvictState`(state lock)→`releaseHoldSpace` sequence preserved in `doTryReserve`/`doEviction`; freed space stays invisible until `afterEvictState`. No double-reserve collapse. |
| **O-012** `getDownloadedContiguousOrEmpty`, `trySet`, `removeFileSegment*`, `removePathIfExists`, `tryGetCachePaths` | **Accept as ported.** These are declared in `FileCache.h`; they are part of the CH public API surface (the ledger's "no caller *this sweep*" was scoped to the 011-014 consumer set, not all of CH). Not over-port — they mirror CH `FileCache.h` and the SCC compiles/links as one unit. Non-blocking. |
| errno reconcile contract (R6/007-2/E1) | **CLOSED / matches.** `FileSegment::write` catch (`FileSegment.cpp:486-521`): on `FileCacheErrnoException` ENOSPC(28)/EDQUOT(122) reads `fs::file_size`, `chassert(downloaded <= physical <= reserved)`, sets `downloaded = physical`, `setDownloadFailedUnlocked`, rethrows; other exceptions → setDownloadFailed + rethrow. Drops only the CH `e.addMessage(...)` text plumbing (accepted R7 collapse) and the `fs::filesystem_error`→ErrnoException producer clause (accepted pre-release producer gap). |
| recache-disabled path (ledger row 144 phrased as "error") | **Verified NOT a throw.** CH `FileCache.cpp:1345-1355` does `failure_reason="query limit exceeded"; return false` with the verbatim string only in a `LOG_TEST`. Velox `FileCache.cpp:1070-1072` matches (`return false`, same failure_reason), dropping only the LOG_TEST. Match. |
| SD1/SD3/SD4/SD5 (signed) | Still hold; CODE does not diverge from the sign-off (see §1). |
| B2b opened-handle seam | **Superseded/better.** Task 013 has landed `OpenedFileCache`; Velox now performs the REAL invalidation (`removeFileSegmentImpl` → `key_metadata->openedFileCache().removePath(removed_path)`, `Metadata.cpp:1260`; rename seam → `FileSegment.cpp:746`) instead of the B2b no-op. Semantically stronger than the amendment required. See §4 note N3. |

## 3. Coverage matrix (012 call site → behavior → impl / test)

| Consumer call site (CH) | Required behavior | Impl? | Tested? | Probe? |
|---|---|---|---|---|
| `ReadBuffer:246,258` / `WriteBuffer:276,333` | get/getOrSet/set | ✓ `FileCache.cpp` | ✓ FileCacheTest | RED via pre-impl |
| `FileSegment.cpp:710` tryReserve (sole caller) | reserve→evict (hold-space split) | ✓ `doTryReserve/doEviction` | ✓ FileCacheTest TryReserveEvictsReleasable | ✓ |
| `ReadBuffer` write/predownload | `FileSegment::write` + errno reconcile | ✓ `FileSegment.cpp:410` | ✓ FileSegmentTest partial-append-failure (real fault WriteFile, production reconcile) | ✓ (fault injected only in WriteFile) |
| resume (007 integration half) | partial-file resume, non-truncating append | ✓ | ✓ FileSegmentTest resume | ✓ |
| `FileCache.cpp:421,1342-1351` | QueryLimit gate/get/recache/doomed-ctx | ✓ `QueryLimit.cpp` | ✓ QueryLimitTest | ✓ doomed-ctx via lock-reacquire callback |
| `Metadata.cpp` iterate/removeKey/reconcile | metadata SM + F14 bucket no-escape | ✓ | ✓ MetadataTest (path layout, LockedKey member order) | static_assert |

## 4. Advisory notes (PLAUSIBLE — non-blocking)

- **N1 (bookkeeping).** SD-012-3 (`query_map`/`records` F14) is benign but currently unlisted in the
  structural-deviation ledger; add a row for parity with SD1 so future reviewers see it was
  adjudicated, not overlooked.
- **N2 (paired O_DIRECT — 013's shard, surfaced here).** CH invalidates BOTH `remove(path, flags)`
  and `remove(path, flags|O_DIRECT)` (`Metadata.cpp:1267-1268`, `FileSegment.cpp:801-802`). Velox
  collapses to a single `openedFileCache().removePath(old_path/removed_path)` that drops all
  flag-variants (`Metadata.cpp:1260`, `FileSegment.cpp:746`). Semantically equivalent (drops every
  descriptor for the path), but the exact CH pairing is worth confirming in the Task-013
  `OpenedFileCache::removePath` implementation. Owned by 013 (H-013-a), not a 012 defect.
- **N3.** `downloadImpl` (`Metadata.cpp:913`) drops CH's reused `Memory<>` external buffer (D3) and
  reads through the reader's own pool-charged buffer (`buf->set(nullptr,0)`), keeping the `memory`
  param unused for signature fidelity. This is the signed SD9/D3 mapping — accept.
- **N4.** `wait` (`FileSegment.cpp:526`) uses a 60s bounded cv-slice loop with NO cancellation token
  (the signed S1 header takes none); CH threads OpenTelemetry + query cancellation. Accepted MVP
  simplification (matches the signed FileSegment.h `wait(size_t)` signature); no fixed sleep, no
  indefinite hang. Non-blocking; revisit if consumer cancellation lands.

## 5. Conclusion

Task 012 = **ACCEPT**, zero unresolved. The SCC is an exact §3 literal translation; the two
guarantee-changing container deviations that fall to this shard (SD-012-1 metadata bucket F14,
SD-012-3 query-limit F14) are both proven to satisfy the no-escape invariant against every real
consumer, so they are accepted under the same discipline as the signed SD1. No hole, no drift, no
unregistered/self-invented equivalent. Recommend only the N1 ledger bookkeeping row.
