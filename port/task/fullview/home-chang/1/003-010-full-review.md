# Full Review: Tasks 003–010 (Controller D-section source-contract sweep)

> D-section artifact per `/home/chang/SourceCode/.ai/share_data/local-cache/filecache-port-authoring-guide.md`.
> Read-only Controller sweep. Input: `003-010-consumer-contract-ledger.md` (§A artifact).
> This round emphasizes **implementation alignment** (§3 structural transliteration) with FULL
> strictness per user directive: any unsigned/unjustified consumer-invisible structural deviation → reopen.
>
> CH baseline: `ch-filecache` @ da28e83e8b3 · Velox baseline: `filecache` @ 89039901a

## 0. Scope & inputs

- CH source of truth: `src/Interpreters/FileCache/*` + real callers (`CachedOnDiskReadBufferFromFile.cpp`, `WriteBufferToFileSegment.cpp`).
- Velox impl: `velox/ch/Interpreters/FileCache/` + `velox/ch/Common/`, plus tests.
- Independently re-opened both repos' Guards.h, StatusFile.cpp, ShardedMap.h, FileCacheKey.cpp, FileCacheUtils.h, FileCacheSettings.cpp, FileCacheBoundedQueue.h, FileCacheScheduler.cpp, CH FileCache.cpp shutdown (2666–2684), CH Metadata.cpp shutdown (1030–1042), Velox tests + both CMakeLists.txt.
- **Could not fully verify (declared):** live cross-impl `sipHash128` golden recomputation blocked by build `-Werror` on generated prototype (environmental). 008-4 adjudicated from source-faithfulness + receipt, NOT a live recomputation → treated as unproven HARD constraint.

## 1. Confirmations of note

- **Lock ordering** CH `Guards.h:53` (`CachePriority > CacheMetadata > Key > FileSegment`; `CacheState` independent) reproduced exactly in Velox `Guards.h:29-43`. `CacheStateGuard::tryLockFor` present at Velox `Guards.h:107` (G-004-verify RESOLVED).
- **Three-phase shutdown** CH `FileCache.cpp:2666-2684` and **cancel-before-join** CH `Metadata.cpp:1030-1042` confirmed in CH; they are re-instantiated under **012** (leaves supply primitives only).
- **immediate>delayed** reproduced Velox `FileCacheScheduler.cpp:152-153` + coalescing `:119` + Running→pendingImmediate `:123`.

## 2. Per-task verdict

- **Task 003** — accept (deviation 003-1 error-code collapse logged; O1/O2 over-port flagged).
- **Task 004** — **REOPEN (004-1 / H2)**: verbatim StatusFile diagnostics not reproduced; PLUS newly-found drift — Velox `ftruncate`s to 0 (`StatusFile.cpp:121`) before reading, so CH's unclean-restart "read old contents then log" is unrecoverable. Fix: reproduce CH `:60,62,76,90` text, move truncate after read-and-log. RED test + emit-line probe.
- **Task 005** — accept. 3-phase/cancel-before-join ordering owned by 012, not the leaf.
- **Task 006** — accept (006-2 activate-revive drop needs sign-off; O5 `setCallback` test-only).
- **Task 007** — accept. Boundary correct: `getRemoteFileMetadata`→nullopt & partial-write reconcile owned by 012/014. 007-2 (no-errno→`fs::file_size`) is the sole HARD-constraint deviation → accept-with-signoff.
- **Task 008** — **REOPEN (008-4 HARD)**: `sipHash128` bit-identity unproven by CH-derived vectors; receipt only checked "32-char lowercase hex" format, so golden vectors may be self-generated (self-confirming). Impl is faithful (seeds + CH `v2^=0xff` at `SipHash128.cpp:118`) but §3 forbids "probably" on a persistence hard constraint. Fix: golden test with CH-computed expected values + `0xff`→`0xee` probe. Secondary 008-2: malformed-char parity asserted by self-oracle, needs differential fuzz vs CH `unhexUInt`.
- **Task 009** — **REOPEN (009-1 headline §3)**: `folly::F14FastMap` (relocates values on rehash) vs CH `std::unordered_map` (stable refs). NO hard constraint (unordered_map compiles in Velox). Neutralized only because sole consumer `Metadata.cpp:108` copies out a shared_ptr; tests use `int` values and never assert the node-stability hazard. Fix: human sign-off + registered "no ref/iterator escapes mutation" invariant enforced in 011/012 review, OR revert to `std::unordered_map`. Also add missing throwing-`emplace` size-invariant test + `SizeGuard` probe.
- **Task 010** — **REOPEN (010-1 / H3 §3 state-representation)**: CH `BaseSettings` per-field `.changed` collapsed to 3 presence bools (path/max_size/ratio). Safe for current consumers (grep-confirmed) but 011/012 read ~30 more fields and CH `FileCacheFactory.cpp:186,202` already reasons about changed-ness for dynamic resize. Fix: restore per-field presence (`present_keys` set) OR sign off no other field needs presence. 010-2 (added allowed-root authorization, no CH origin) → accept-with-signoff (confirm intended additive, not scope creep). H1 (validation text kebab-case) low.

## 3. Structural deviation verdicts

| # | Deviation | Hard constraint? | Verdict |
|---|---|---|---|
| 009-1 | unordered_map → F14FastMap (relocates) | No | **REOPEN** — unsigned §3, untested hazard |
| 009-2 | ProfiledMutex → plain; unused `lock_wait_event_` | No | signoff (if 011 needs no telemetry) |
| 009-3 | added `Hash` param | No | accept |
| 003-1 | typed ErrorCodes → single VELOX_FAIL | No | accept-with-signoff (deliberate, 114 sites) |
| 004-1 (H2) | StatusFile diagnostics not reproduced + truncate-before-read | No | **REOPEN** |
| 004-2/003 | fd→folly::File; SharedMutex→SharedMutex | infra | accept |
| 005-1/2 | local pool; VELOX_CHECK stricter | design/none | accept |
| 006-1 | TLS qid → RAII scope | infra | accept |
| 006-2 | central timer+activate → per-task future, no activate | No | signoff (confirm no activate-revive reliance) |
| 007-1 | BufferBase → FileCacheBufferState | soft | accept |
| 007-2 | typed errno → fs::file_size reconcile | **YES** | accept-with-signoff |
| 007-3 | getRemoteFileMetadata → nullopt | soft | accept (deferred 012/014) |
| 008-1 (H1) | fromKeyString error text differs | No | accept (low) |
| 008-2 | unhexUInt → nibble+add, self-oracle | No | **REOPEN (secondary)** |
| 008-3 (O3) | added checkedAdd/VELOX_FAIL | No | over-port, flag until 013/014 |
| 008-4 | sipHash128 bit-identity unproven | **YES** | **REOPEN** |
| 010-1 (H3) | per-field .changed → 3 bools | No | **REOPEN** |
| 010-2 | added allowed-root authorization | No | accept-with-signoff |

## 4. Conclusion

**Reopen list (zero-unresolved gate NOT met):**
1. **Task 004** — H2 verbatim StatusFile diagnostics + move truncate after read-and-log.
2. **Task 008** — 008-4 (HARD) CH-computed sipHash128 golden test; secondary 008-2 differential fuzz.
3. **Task 009** — 009-1 (headline §3) F14 sign-off + enforced invariant OR revert to `std::unordered_map`; add throwing-`emplace` size test.
4. **Task 010** — 010-1/H3 per-field presence tracking OR sign-off; 010-2 confirm additive.

**Sign-off-required (record before 011/012, not defaults):** 003-1, 006-2, 007-2 (hard), 009-2, 010-2.

**Over-ports to trim/justify:** O1/O2 (queue overloads), O3 (checkedAdd), O4 (Hash param + dead lock_wait_event_), O5 (setCallback).

**Test-suite integrity:** all five leaf test binaries registered via `add_test`; no DISABLED_/if(false)/GTEST_SKIP/#if 0. Two false-green risks: 008-4 (self-generated golden vectors) and 009 (node-stability + throwing-emplace paths untested).

**Proceed to 011/012?** **NO.** Four tasks (004, 008, 009, 010) unresolved; two (008-4, 009-1) sit directly on SCC-consumed structures. `EXECUTION_PROTOCOL.md` "After Task 010" gate (zero-unresolved + explicit user approval) NOT satisfied.
