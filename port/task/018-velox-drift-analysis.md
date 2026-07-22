# Task 018 — Velox drift analysis: IBM/Gluten pin vs. filecache2 fork base

Read-only. No files edited/built/run in the Velox repo.

## Refs
- `f4042228a` = our filecache2 fork BASE (upstream facebookincubator, 2026-07-16).
- `3cf73e825` (`ibm-dft-0710`) = Velox that Gluten `main` builds (IBM/velox `dft-2026_07_10`, 2026-07-10).
- `267246d0c` = merge-base (2026-07-10 05:16). IBM pin: 4 commits ahead. Our base: 36 ahead.

Method: `git diff 3cf73e825 f4042228a -- <path>` per dependency-surface file; for any file that differs, `git log --oneline 267246d0c..3cf73e825` (IBM side) and `..f4042228a` (upstream window) to attribute the drift.

## Dependency-surface table

| File | Differs? | Who changed it | Signature / behavior impact | Verdict |
|---|---|---|---|---|
| `connectors/hive/BufferedInputBuilder.h` | No | — | `create()` signature byte-identical | SAFE |
| `connectors/hive/BufferedInputBuilder.cpp` | No | — | registerBuilder extension point identical | SAFE |
| `connectors/hive/HiveConnectorUtil.h` | No | — | `createBufferedInput` decl identical | SAFE |
| `connectors/hive/HiveConnectorUtil.cpp` | Yes | upstream window (`332b91f61`) | Adds `&& !nimbleDirectBufferedInputEnabled()` guard to the NIMBLE branch only. Signature of `createBufferedInput` unchanged; Hive/Parquet path our builder routes is untouched. | SAFE |
| `connectors/Connector.h` | No | — | `ConnectorQueryCtx` (`cache()`, `scanId()`), `Connector::getTracker` identical | SAFE |
| `connectors/hive/FileHandle.h` | No | — | `file`, `uuid`, `groupId` fields identical | SAFE |
| `dwio/common/BufferedInput.h` | Yes | upstream window (`c759d48ad`) | Comment-only change to `adjustedReadPct` body reasoning; the protected static's signature/logic (`referencedBytes - lastReferencedBytes`) unchanged. Base-class ctors our `FileCacheBufferedInput` mirrors are unchanged. | SAFE |
| `dwio/common/CachedBufferedInput.h` | No | — | ctor identical | SAFE |
| `dwio/common/DirectBufferedInput.h` | No | — | ctor identical | SAFE |
| `common/caching/ScanTracker.{h,cpp}` | Yes | upstream window (`c759d48ad`) | `.h`: doc comment added to existing `TrackingData::lastReferencedBytes` field (no new/removed/reordered fields — struct layout unchanged). `.cpp`: flat-map prefetch-eligibility fix, internal to ScanTracker. | SAFE |
| `common/caching/` (AsyncDataCache / SsdCache) | No | — | public API identical; our filecache coexists with `connectorQueryCtx->cache()==nullptr` | SAFE |
| `common/file/File.h` (ReadFile/WriteFile) | No | — | identical | SAFE |
| `dwio/common/Reader*.h` ReaderOptions | No (for surface we call) | — | identical | SAFE |
| `common/io/IoStatistics.h` | No | — | identical | SAFE |
| `dwio/common/IoStatistics.h` | No | — | identical (both `create()` stats params intact) | SAFE |

Only **3** surface files differ at all, and **all 3 diffs are attributed entirely to the upstream 07-10→07-16 window** — none is inside the IBM 4-commit pin.

## The 4 IBM-ahead commits (`267246d0c..3cf73e825`)
1. `3cf73e825` [OAP] Change `SpillPartitionId::kMaxSpillLevel` to 7
2. `58beb51df` [OAP] feat: Implement se/dser method for HashTable
3. `65dc857e2` [OAP] [11771] Fix smj result mismatch in semi/anti/full outer join
4. `e303b5e5f` feat: Allow subfield rename and deletion for Parquet format

None touches the FileCache/builder dependency surface (spill, hash-table serde, SMJ, Parquet subfield-mutation — all orthogonal).

## Cross-check: does our code use any changed symbol?
`grep -rn 'adjustedReadPct|lastReferencedBytes|nimbleDirectBufferedInputEnabled'` over `velox/ch/` → **0 matches**. Our integration does not touch any of the three drifted symbols; the changes are internal to Velox's own NIMBLE/flat-map read paths, which our Hive-routed builder does not exercise.

## Bottom line
The dependency surface is **intact (SAFE)**. We can build Gluten against our filecache2 fork: the `create()` signature, `BufferedInput` ctors, `ConnectorQueryCtx`, `FileHandle`, caching/File/IoStatistics APIs, and the `registerBuilder` extension point are all byte-identical between the IBM pin and our base. The only three differences are upstream refinements (one NIMBLE branch guard, two comment/doc + flat-map-internal changes) that don't alter any symbol our FileCache code calls and don't change `TrackingData` layout.

No RISK, no BLOCKER. No reconciliation needed.
