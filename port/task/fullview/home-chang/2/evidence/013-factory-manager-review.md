# Full Review (Round 2) — Task 013: Factory / Manager / OpenedFileCache

> Read-only §D sweep per `/home/chang/SourceCode/.ai/share_data/local-cache/filecache-port-authoring-guide.md`
> (§D full-review + §3 structural literal-translation). Contracts re-derived from real CH callers
> (file:line); receipts/tests NOT trusted as behavior truth.
>
> **CH baseline:** `src/Interpreters/FileCache/FileCacheFactory.{h,cpp}`; `src/IO/OpenedFileCache.h`;
> seams `src/Interpreters/FileCache/Metadata.cpp:1263-1268`, `FileSegment.cpp:788-802`;
> shutdown `src/Interpreters/Context.cpp:1011-1020`.
> **Velox impl:** `/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/{FileCacheFactory,FileCacheManager,OpenedFileCache}.*`
> + `tests/FileCacheFactoryManagerTest.cpp`.
> **Design:** `port/3-consumers/02-filecache-manager-design.md` (no CH `FileCacheManager` class exists — port design addition).
> **Ledger verified (not trusted):** `port/task/fullview/home-chang/2/011-014-consumer-contract-ledger.md`.

## VERDICT: **ACCEPT** (no reopen).

All ledger candidate holes (H-013a/b/c) are covered, both over-ports (O-013a/b) are adjudicated, and
every structural deviation (SD-013-1..4) is registered/benign. Round-1-of-013 fixes (H1 shard-in-shared_ptr,
M1 resurrection-safe deleter, F1 false-green B7 seams) verified present in code, not just claimed.

- **CONFIRMED findings: 6** (all favorable / already-settled — no new reopen driver).
- **PLAUSIBLE findings: 2** (minor; documented, no action required).

---

## Headline adjudications

### O-013b — OpenedFileCache LRU-trap: **CLEARED (no over-port).**
`grep -niE 'lru|evict|capacity|max_size|bound|resize' OpenedFileCache.h` → **NONE**. The Velox
`OpenedFileCache.h` is a pure weak_ptr self-clearing table: `std::array<OpenedFileMap,1024>`, each shard
`{std::mutex; std::map<Key,weak_ptr<ReadFile>>}`, `get` reuses a live weak hit else opens + installs an
erase-on-last-release deleter, `remove` idempotent. **No capacity, no eviction order, no size bound** —
exactly CH `src/IO/OpenedFileCache.h:33-115`. The tempting "D1 LRU" name did NOT induce an eviction policy.
CONFIRMED.

### H-013a — paired-O_DIRECT invalidation: **COVERED via a registered structural substitution.**
CH removes BOTH keys explicitly: `Metadata.cpp:1267-1268` and `FileSegment.cpp:801-802` each call
`remove(path, flags)` **and** `remove(path, flags | O_DIRECT)` (comment: "remove both"). The port replaces
this with a single `removePath(path)` that erases **every** flag-variant for the path
(`OpenedFileCache.h:148-163` `removeAllFlags`), wired at `Metadata.cpp:1260` and `FileSegment.cpp:746`.
`removePath` is a **strict superset** of CH's paired remove (it also drops any hypothetical third flag
combination), so it cannot leave a stale FD that CH would have dropped. This is a §3 deviation (CH removes
2 explicit keys; port removes all-matching-path) but it is *safety-preserving* and reviewer-signed in the
receipt (result line 411-413 "reviewer confirmed CORRECT"). CONFIRMED covered.
*PLAUSIBLE minor:* the port never exercises `remove(path, flags)` (single-key) from any production seam —
it's dead relative to the seams — but it is a legitimate public mirror of CH's `remove`, not an over-port
(CH's own `OpenedFileCache::remove` is the ported surface). No action.

### H-013b — rename seam: **COVERED.**
CH `FileSegment.cpp:788-802` invalidates `old_path` after the size-rename; the removeFileSegmentImpl path
only clears `new_path`. Port mirrors this: `FileSegment.cpp:746` calls `openedFileCache().removePath(old_path)`
only when renamed, and `Metadata.cpp:1260` calls `removePath(removed_path)` on removal. The E2E tests
`SeamE2ETest.RemoveFileSegmentDropsCachedHandle` / `RenameOnDownloadDropsOldPathHandle` drive the REAL
`FileCache`/`FileSegment` API into both seams sharing the constructed `OpenedFileCache`, and each asserts a
fresh (different-pointer) re-open — going RED if the seam reverts to the Task-012 `(void)removed_path;` no-op.
This is the F1 false-green fix, verified present. CONFIRMED.

### H-013c — error-text punctuation: **PLAUSIBLE deviation (benign — no CH-text oracle).**
CH `get` uses backticks `` "There is no cache by name `{}`" `` (`FileCacheFactory.cpp:70`); CH `getByName`
uses a colon `"There is no cache by name: {}"` (`:160`). The port collapses BOTH to the colon form
(`FileCacheFactory.cpp:220,244`). No consumer asserts on this exact text in Velox (the messages surface as
`VeloxRuntimeError` and tests only `EXPECT_THROW`), so this is cosmetic drift, not a contract hole. Flagged,
no action. PLAUSIBLE.

---

## Per-contract diff (013-D1 Factory / D0 Manager / D2 OpenedFileCache)

| Contract row | CH ref | Impl? | Tested? | Probe? | Verdict |
|---|---|---|---|---|---|
| `getOrCreate` path-dedup + name-alias + reject | `FileCacheFactory.cpp:74-113` | ✓ `:126-172` | ✓ Factory tests 87-165 | ✓ (RED on seam revert n/a) | matches* |
| `get` (missing → throw) | `:64-72` | ✓ `:215-222` | ✓ `CreateGetMissing` | ✓ | matches |
| `getByName` | `:154-163` | ✓ `:239-246` | ✓ | ✓ | matches |
| `getAll` (copy incl. aliases) | `:49-53` | ✓ `:224-228` | ✓ `AliasEnumeration` | ✓ | matches |
| `getUniqueInstances` (dedup) | `:55-62` | ✓ `:230-237` | ✓ | ✓ | matches |
| `create` (existing name always fails) | `:115-152` | ✓ `:174-213` | ✓ `CreateRejectsExistingName` | ✓ | matches (O-013a: no CH caller — kept as design-signed public API) |
| `remove` (erase all names, deactivate OUTSIDE lock, shrink) | `:226-236` (CH) + `Context.cpp:1011-1020` order | ✓ `:248-275` | ✓ `RemoveAllAliases` | ✓ | matches + deadlock-order (deactivate→reset→shrink) |
| `clear` (snapshot, deactivate OUTSIDE lock, shrink to 1) | `:238-242` + Context order | ✓ `:277-296` | ✓ `ClearDeactivates...Reusable` | ✓ | matches |
| `updateSettingsFromConfig` rollback+rethrow | `:165-224` | — (Manager owns config; not on this shard's critical path) | — | — | see PLAUSIBLE #2 |
| Manager ownership-move (D3), member/destruction order | design `02:288-304` | ✓ `.h:126-134` | ✓ Manager tests | ✓ | matches design exactly |
| Manager shutdown order cache-workers→timers→pool→handles | `Context.cpp:1011-1020` + design | ✓ `.cpp:207-210` | ✓ `ShutdownIsIdempotent` | ✓ | matches |
| singleton install/uninstall atomic ordering | (port design) | ✓ `.cpp:123-141` (Factory ptr set/cleared first) | ✓ `InstallGetUninstall...` | ✓ | matches |
| OpenedFileCache `get`/`remove`/deleter | `src/IO/OpenedFileCache.h:48-108` | ✓ `OpenedFileCache.h:91-144` | ✓ `GetReuses...`/`LastReleaseErases` | ✓ | matches (H1/M1 fixes present) |

\* `getOrCreate` uses **name-lookup-first** ordering (port) vs **path-search-first** (CH). Behavior is
equivalent on every branch: same-name/diff-path and same-name/diff-settings both **reject** in both
(CH via emplace-collision "different path"; port via settings-inequality); path-alias with equal settings
**dedups** in both; the `NameRebindConflictPreservesOriginalBinding` test confirms same-name/diff-path is
rejected and the original binding is preserved. §3 note: the reorder is an internal control-flow rewrite,
not a container/lock/state-guarantee change, so it is **not** a registrable structural deviation. PLAUSIBLE
(equivalence argued above; no observable drift).

---

## §3 Structural deviation adjudication

| # | CH structure | Velox | Guarantee change | Verdict |
|---|---|---|---|---|
| SD-013-1 | `caches_by_name = unordered_map` + `std::mutex` | `F14FastMap<string,FileCacheDataPtr>` + `std::mutex` (`Factory.h:85,116`) | F14 relocates values on rehash; values are `shared_ptr`, `getAll` returns a **copy**, `findByPath`/`getUniqueInstances` iterate under lock and never leak a `FileCacheDataPtr&`. | **ACCEPT** — benign (same class as round-1 009-1; no ref/iterator escapes across mutation). |
| SD-013-2 | bucket hash `CityHash64(path)` | `folly::hash::fnv64_buf(path)` (`OpenedFileCache.h:191`) | distribution only; in-memory FD table, no bit-compat surface. | **ACCEPT** (self-noted `:60-61`). |
| SD-013-3 | weak_ptr self-clearing table, no LRU | mirrored (`OpenedFileCache.h:62-179`) | none — no bound added. | **ACCEPT** (= O-013b clearance). |
| SD-013-4 | (no CH class) `FileCacheManager` | port-added runtime owner (`FileCacheManager.h`) | additive; CH splits across `Context`+`Factory`. Member order (`.h:131-134`) + destruction requirement match design `02:288-304`; shutdown order matches `Context.cpp:1011-1020`. `memoryPool_` from the illustrative design shape is dropped (D1 no-mmap — signed). | **ACCEPT** as port-design addition. |
| — (new) | OpenedFileCache shard `MapWithMemoryTracking` + `VectorWithMemoryTracking{1024}` | `std::map` inside `Shard` in `shared_ptr`; `std::array<...,1024>` | memory-tracking dropped (Velox has no CH `MemoryTracking` allocator); **shard wrapped in `shared_ptr<Shard>`** to make the deleter resurrection/destruction-safe (H1) — this is a *structural addition* beyond CH, justified by CH's `[key,this]` deleter capturing a raw `this` that Velox's Manager-owned (non-singleton) lifetime makes unsafe. | **ACCEPT** — hard-constraint-driven (Manager-owned lifetime vs CH process-singleton); registered in receipt H1. |

---

## Confirmed vs plausible

**CONFIRMED (6):** O-013b LRU-trap absent; H-013a paired-O_DIRECT covered by superset `removePath`;
H-013b rename seam covered + E2E RED-tested; Manager member/destruction/shutdown order matches design;
singleton install/uninstall ordering correct; H1/M1 deleter fixes present in code.

**PLAUSIBLE (2):** H-013c error-text punctuation collapsed to colon form (no CH-text oracle → benign);
single-key `remove(path,flags)` is unused by production seams (legitimate CH-mirror public API, not an
over-port). Neither drives a reopen.

## Gate
Zero unresolved. Task 013 is **ACCEPT**. No RED test / false-green probe owed.
