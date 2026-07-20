# Full Review (R2, §D + §3): Task 014 — DWIO reader / downloader handoff

Verdict: **reopen** (one CONFIRMED reachable hole + one unregistered structural
deviation needing §3 sign-off). Everything else matches or is a legitimate,
already-adjudicated exclusion.

## 0. Scope & inputs

- CH source of truth: `src/Disks/IO/CachedOnDiskReadBufferFromFile.{h,cpp}`
  (the reader lives under `Disks/IO`, not `Interpreters/Cache`).
- Velox port diffed: `velox/ch/Disks/IO/{FileCacheBufferedInput,FileCacheInputStream}.{h,cpp}`
  + `FileCacheRequestContext.h` + `FileCacheFileIdentity.h` + test
  `Disks/IO/tests/FileCacheBufferedInputTest.cpp`.
- Re-derived contracts from real CH `nextImplStep`/`readFromFileSegment`/
  `createReadFromFileSegmentState`/`getCacheReadBuffer` — not from receipts.
- Method existence checked directly against Velox `FileSegment.h` and
  `ReadBufferFromVeloxReadFile.h`.

## 1. State-machine / handoff diff (CH → Velox)

| CH phase (file:line) | Velox mapping | Judgment |
|---|---|---|
| `nextImplStep` main loop `cpp:1316-1496` | `Next` `InputStream.cpp:704-815` | matches (see structural notes) |
| `initialize`/`nextFileSegmentsBatch` `:272-294,224-270` | `initializeIfNeeded`/`nextFileSegmentsBatch` `:273-287,217-271` | matches (tempCacheOnly throw, get/getOrSet, boundary_alignment) |
| `createReadFromFileSegmentState` `:577-745` | same `:291-380` | matches incl. `wait(offset)` DOWNLOADING loop, downloader election, predownload gap `offset-currentWriteOffset` |
| `getRemoteReadBuffer` (Q1/Q2 handoff) `:480-555` | same `:148-194` | matches: `getRemoteFileReader`/`setRemoteFileReader`/`extractRemoteFileReader`, offset==bufferEnd reuse, "race with background downloader" comment ported |
| `getCacheReadBuffer` `:315-478` | same `:123-146` | **DRIFT — self-heal-on-truncation dropped (H-014c)** |
| `predownloadForFileSegment` `:934-1193` | `predownloadForCurrentSegment` `:472-545` | matches core loop; buffer re-install ported to `readFromCurrentSegment:643`; truncation sub-path excluded (legit, see §3-exclusions) |
| withdraw-before-publish `:1024,1174,1740` | `resetRemoteFileReader`→state-publish `:505-506,534-535` | matches (H-014a satisfied) |
| `completeFileSegmentAndGetNext` `:875-915` | `completeCurrentSegmentAndAdvance` `:560-579` | matches |
| `nextImplStep` SCOPE_EXIT downloader release `:1350-1385` | `Next` catch + post-advance guard + dtor `:763-810,89-106` | matches in effect (defense-in-depth), see §2 |
| `readBigAt` whole-call downloader `:1805-1998` | (not ported) | legit exclusion (H-014b), verified |
| `seek`/`setReadUntilPosition` `:2000-2091` | `seekToPosition` `:871-898` | matches fast/slow path; `queryContextHolder_` never reset |

## 2. Confirmed findings

### F-014-1 (CONFIRMED, MEDIUM→reopen) — self-heal on external truncation is NOT ported, and it IS reachable in Velox

CH `getCacheReadBuffer` (`cpp:448-472`): after opening the local cache file it
compares `getFileSizeFromReadBuffer(reader) < file_segment.getDownloadedSize()`
gated on `hasSizeInFileName() && state∈{DOWNLOADED,DETACHED}`, and on a short
file **returns `nullptr`** to make the caller (`createReadFromFileSegmentState:592-602`)
silently switch `CACHED → REMOTE_FS_READ_BYPASS_CACHE`. The verbatim `LOG_WARNING`
"Cache file {} is shorter than its recorded size ({} < {}); it was likely
truncated outside ClickHouse..." and the empty-file `LOGICAL_ERROR` at `:474`
also live here.

Velox `getCacheReadBuffer` (`InputStream.cpp:123-146`) opens the local file and
returns it unconditionally — **no size-vs-`getDownloadedSize` check, no
nullptr-bypass, no empty-file guard.**

The ledger listed H-014c as "hole-risk". This review **confirms it as a real
hole, not merely a risk**, because the exclusion premise (the one used for the
predownload/readBigAt truncation cases — "no Velox source for the size") does
**not** hold here:
- `FileSegment::hasSizeInFileName()` exists (`FileSegment.h:150`),
  `getDownloadedSize()` exists (`:177`), `state()` exists.
- `ReadBufferFromVeloxReadFile` exposes `size()` and `tryGetFileSize()`
  (`ReadBufferFromVeloxReadFile.h:47,242`) — the on-disk short size IS
  observable with no extra `stat`, exactly as CH intends.

So every input CH needs for this self-heal is present in Velox; the behavior was
simply omitted. Consequence (CH rationale `:465-469`): when a cache file backing
a MergeTree mark/metadata file is truncated outside ClickHouse, CH re-fetches
from source; the Velox port instead serves a short read from the truncated
segment-relative cache file (`prepareReadFromFileSegmentState` CACHED path bounds
to `getDownloadedSize()`, so it will read fewer bytes than the segment claims),
surfacing later as a corrupt/short read rather than transparent recovery. The
empty-file case (`cache_file_size==0`) additionally loses the CACHED
`LOGICAL_ERROR` guard.

- CH ref: `cpp:448-478`, `:592-602`.
- Severity: MEDIUM (correctness on a rare-but-real corruption path; silent).
- RED test: pre-populate a DOWNLOADED size-in-filename segment, truncate its
  on-disk cache file to < downloadedSize out-of-band, then read the region;
  assert the stream returns the FULL correct bytes (re-fetched from source),
  not a short read. Also an empty-file variant asserting the CACHED empty-file
  error/bypass.
- false-green probe: neutralize the (to-be-added) nullptr-bypass in
  `getCacheReadBuffer`; the truncation test must go RED (short/garbage read).
- Classification: **CONFIRMED**.

### F-014-2 (CONFIRMED — unregistered §3 structural deviation, needs sign-off, likely accept)

CACHED read-until coordinates. CH `prepareReadFromFileSegmentState:796` sets
`setReadUntilPosition(min(range.right+1, file_size))` for **all** read types
(including CACHED) and seeks CACHED to `offset - range.left` (`:820-829`) — i.e.
CH bounds the segment-relative cache file with an **absolute** limit and relies
on the file being exactly `downloaded_size` long. The Velox port bounds CACHED
with `setReadUntilPosition(getDownloadedSize())` (segment-relative) instead
(`InputStream.cpp:404-407`).

This is a **guarantee-preserving-but-different** structural choice (the Velox
form is arguably more correct for a purely segment-relative local file, and the
worker RED-verified it fixed a real mixed-coordinate over-read). Per §3 it is a
non-infra deviation that should be entered in the "结构偏离台账" (CH structure →
Velox substitute → why → evidence) and human-signed, not silently taken. It is
**not** a correctness reopen on its own — flag for registration.
- Classification: **CONFIRMED deviation**, adjudicate as accept-with-registration.

## 3. Re-verified exclusions (both still LEGITIMATE — do not re-litigate)

- **Remote-object-truncation `CANNOT_READ_ALL_DATA`** (CH `:1028-1061,1722-1753`):
  depends on `state.buf->getRemoteFileMetadata()` (actual object size + last
  modified). `grep` confirms `ReadBufferFromVeloxReadFile` has **no**
  `getRemoteFileMetadata`; the "object shrank between listing and reading"
  discriminator has no Velox source. Legit exclusion (deferred to 015). Note it
  is a DIFFERENT mechanism from F-014-1: F-014-1's size source is the *local
  cache file* (available), this one's is *remote object metadata* (absent).
- **`readBigAt` source-failure downloader release** (CH `:1805-1998`):
  `supportsReadAt`/`readBigAt` deliberately not ported (design 03). Downloader-
  release-on-failure covered by `Next` catch + dtor. Legit.

## 4. Over-port (O-014) — confirmed appropriately trimmed

Giant `LOGICAL_ERROR` field-list diagnostics (`:1755-1795`),
S3-specific `getReadUntilPosition`/`getStopReason`, and `predownload_memory`
small-buffer optimization: the port kept the **classification** (bypass on
reserve/write failure; VELOX_FAIL on source-exhausted predownload) and owned
scratch (`predownloadBuffer`) without byte-identical diag strings. Correct per
O-014 guidance. No action.

## 5. Plausible / non-blocking

- **P-014-a (PLAUSIBLE, low)** F3 catch-block isolation: the receipt itself
  records that neutralizing ONLY the `Next` catch-block `releaseDownloaderIfNeeded`
  does not flip `MidDownloadCacheWriteFailureReleasesDownloaderNoLeak` RED
  because the dtor is a second net. The controller accepted this as a Task-015
  deferral. Agreed — the catch block IS proven reached with a downloader held
  (probe, `isDownloader=true`); the finer isolated-RED needs a live-stream
  downloader-state probe the MVP reader does not expose. Not a reopen.
- **P-014-b (PLAUSIBLE, low)** `getFinishedDownloadTime` exists on Velox
  `FileSegment.h:171` but the zero-read-unfinished diagnostic block that used it
  (`:1697-1704`) is part of the excluded truncation diagnostics — consistent
  with O-014. No action.

## 6. Coverage matrix (reachable CH behavior → impl? test? probe?)

| Behavior | CH ref | Impl | Test | Probe | Verdict |
|---|---|---|---|---|---|
| read-type decision SM | `:577-745` | ✓ | Miss/Hit/Bypass/Predownload | partial | match |
| Q1/Q2 remote-reader reuse | `:480-555` | ✓ | Q1Q2Handoff | (identity not asserted, F4 accepted) | match |
| withdraw-before-publish (H-014a) | `:1024,1174,1740` | ✓ | MidDownload…NoLeak | ✓(probe) | match |
| predownload gap | `:934-1193` | ✓ | PredownloadFromMidSegment | ✓(bytes=12) | match |
| reserve-fail → bypass | `:1651-1655` | ✓ | ReserveFailure… | ✓(RED) | match |
| self-heal on truncation (H-014c) | `:448-472,592-602` | **✗** | **✗** | — | **HOLE F-014-1** |
| CACHED empty-file guard | `:474` | ✗ | ✗ | — | folded into F-014-1 |
| seek fast/slow, holder lifetime | `:2000-2091` | ✓ | SeekWithin/Outside | — | match |
| dtor completes front + holder outlives | `:917-932` | ✓ | (order-by-declaration) | — | match |
| remote-truncation CANNOT_READ_ALL_DATA | `:1028-1061,1722-1753` | excluded | — | — | legit exclusion |
| readBigAt | `:1805-1998` | excluded | — | — | legit exclusion |

## 7. Conclusion

- reopen Task 014 for **F-014-1** (self-heal-on-truncation hole — CONFIRMED
  reachable, all inputs present in Velox, silent-corruption class).
- register **F-014-2** in the §3 structural-deviation ledger (CACHED read-until
  segment-relative vs CH absolute) with human sign-off.
- Both documented exclusions (remote-metadata truncation, readBigAt) remain
  legitimate — no re-litigation.
- CONFIRMED: 2 (1 hole, 1 unregistered deviation). PLAUSIBLE: 2 (both
  non-blocking). zero-unresolved gate: **NOT met** until F-014-1 is closed.
