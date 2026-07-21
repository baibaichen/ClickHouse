# Task 016: `WriteBufferToFileSegment` for Ephemeral Temporary Data

> **Post-MVP task.**
>
> This task ports the FileCache write adapter and proves the ClickHouse
> temporary-data consumer contract with a synthetic Velox fixture. Velox has no
> `TemporaryDataOnDisk`-equivalent production consumer today; do not invent or
> wire one in this task.

## Status and gate

```text
environment_profile: root-oss
task_016_allowed: false
reason: rewritten contract is review-ready; implementation requires explicit approval
prerequisite: Task 015 accepted
```

## Why this task exists

Tasks 003-015 implement the read-cache path:

```text
remote file -> FileCacheBufferedInput -> FileCache segment -> later cache hit
```

Task 016 adds the opposite producer-facing primitive:

```text
query-generated temporary bytes
  -> WriteBufferToFileSegment
  -> Ephemeral FileSegment
  -> local temporary cache file
  -> read back while the holder is alive
  -> remove immediately with the last holder
```

This is for sorting/aggregation/spill-style temporary data. It is **not**
`cache_on_write_operations`, not write-through caching for ordinary files, and
not part of the accepted FileCache read path.

## ClickHouse history and source of truth

The feature arrived in three stages:

1. [`#40893`](https://github.com/ClickHouse/ClickHouse/pull/40893), merged
   2022-09-30, introduced the common `TemporaryDataOnDisk` interface for
   sorting and aggregation.
2. [`#43972`](https://github.com/ClickHouse/ClickHouse/pull/43972), merged
   2022-12-23, added local FileCache-backed temporary files and introduced
   `WriteBufferToFileSegment`
   (`182b34c11e4a5efb3fa71636710871cf4145490a`).
3. [`#48664`](https://github.com/ClickHouse/ClickHouse/pull/48664), merged
   2023-04-20, extended the same writer to the HTTP temporary-buffer read-back
   path (`92d0d9d4ff68c0b53e19317ff9218eab44e9588b`).

Authoritative current consumers:

```text
src/Interpreters/TemporaryDataOnDisk.cpp
  TemporaryFileInLocalCache
  FileCache::set(... FileSegmentKind::Ephemeral ...)
  WriteBufferToFileSegment

src/Interpreters/FileCache/WriteBufferToFileSegment.h
src/Interpreters/FileCache/WriteBufferToFileSegment.cpp
```

Current Velox state:

```text
FileSegmentKind::Ephemeral exists.
Ephemeral segments are unbounded and removed with the last holder.
FileSegment::reserve/write/completePartAndResetDownloader exist.
WriteBufferFromFileBase and pool-backed FileCacheBufferState exist.
No TemporaryDataOnDisk-equivalent consumer exists.
No WriteBufferToFileSegment exists.
```

The absence of a Velox consumer is a scope boundary, not permission to create a
fake spill subsystem. This task delivers the reusable writer plus a
consumer-contract fixture only.

## Goal

Create:

```text
velox/ch/IO/WriteBufferToFileSegment.h
velox/ch/IO/WriteBufferToFileSegment.cpp
velox/ch/IO/tests/WriteBufferToFileSegmentTest.cpp
```

Deliver:

```text
velox_ch_write_buffer_to_file_segment_test
```

The writer must:

1. derive from the accepted `WriteBufferFromFileBase`;
2. own a pool-backed `BufferPtr` through `FileCacheBufferState`;
3. accept either one owned `FileSegmentsHolderPtr` or a non-owning
   `FileSegment *`;
4. acquire and release downloader identity for every append;
5. reserve before writing and increment `writtenBytes` only after success;
6. support external-buffer `set` + `next` without copying into the owned
   staging buffer;
7. flush pending bytes before `sync` or `finalize`;
8. finalize/sync the segment's existing local cache writer;
9. cancel without appending pending bytes;
10. support read-back through an injected local `ReadFile` opener after
    finalization.

## Source-truth correction: no swap guard in the Velox adapter

Do **not** port ClickHouse `SwapHelper` mechanically.

In the accepted Velox implementation, `FileSegment::write` already:

```text
attaches the producer buffer to the local cache writer
calls local_writer->next()
detaches it with set(nullptr, 0)
```

Therefore, after the outer writer calls `next`, the local cache writer has no
pending working bytes. `sync` and `finalize` operate directly on its underlying
`WriteFile`; swapping the outer writer's views into it is unnecessary and would
create a second buffer-state protocol.

The earlier `BufferStateSwapGuard` proposal is superseded by this source-truth
mapping. A mutation must prove that `sync`/`finalize` first flush the outer
writer and then invoke the local writer.

## Public shape

The exact class shape may follow repository naming, but it must preserve this
contract:

```cpp
class WriteBufferToFileSegment final : public WriteBufferFromFileBase
{
public:
    using OpenReadFile =
        std::function<std::shared_ptr<velox::ReadFile>(const std::string &)>;

    WriteBufferToFileSegment(
        FileSegment * segment,
        velox::memory::MemoryPool * pool,
        OpenReadFile open_read_file,
        size_t buffer_size = WriteBufferFromVeloxWriteFile::kDefaultBufferSize);

    WriteBufferToFileSegment(
        FileSegmentsHolderPtr holder,
        velox::memory::MemoryPool * pool,
        OpenReadFile open_read_file,
        size_t buffer_size = WriteBufferFromVeloxWriteFile::kDefaultBufferSize);

    ~WriteBufferToFileSegment() override;

    size_t writtenBytes() const;
    std::unique_ptr<ReadBufferFromVeloxReadFile> getReadBuffer();
};
```

Requirements:

- A non-zero owned buffer requires a non-null `MemoryPool`.
- The owning constructor rejects a holder whose size is not exactly one.
- `OpenReadFile` is injected; do not reach through a global filesystem
  singleton from production code.
- `getReadBuffer` calls `finalize` first, opens the segment path, and returns a
  reader over exactly the written file.
- Destruction calls `cancel` and never implicitly flushes.

## Write lifecycle

`nextImpl`:

1. read the pending byte count from the base working view;
2. call `getOrSetDownloader` and require the current caller;
3. install an exception-safe release guard that calls
   `completePartAndResetDownloader`;
4. call `reserve(pending_bytes, timeout, failure_reason)`;
5. on reserve failure, throw and append nothing;
6. call `FileSegment::write(buffer().begin(), pending_bytes, writtenBytes)`;
7. increment `writtenBytes` only after `write` succeeds.

`syncImpl`:

```text
The base sync has already called next.
Get the local cache writer and call sync.
Do not swap buffer state.
```

`finalizeImpl`:

```text
Call next to append pending outer bytes.
Get the local cache writer and finalize it.
Do not swap buffer state.
```

`cancelImpl`:

```text
Append nothing.
Do not finalize.
Let the owned holder destroy/remove the Ephemeral segment.
The non-owning constructor leaves holder cleanup to its caller.
```

## Required consumer scenarios

### 1. `TemporaryDataOnDiskEquivalentLifecycle`

This is the primary scenario introduced by ClickHouse `#43972`.

1. Create a real FileCache fixture and a random key.
2. Call `FileCache::set` with `FileSegmentKind::Ephemeral`.
3. Assert one unbounded segment and that `FileSegment::getPath()` ends in
   `0_temporary`.
4. Construct `WriteBufferToFileSegment` with the holder.
5. Write deterministic spill bytes in multiple chunks, including a chunk larger
   than the owned buffer.
6. Call `sync`, continue writing, then call `finalize`.
7. Read through the injected `ReadFile` opener and assert exact bytes.
8. Destroy the writer/holder.
9. Assert the metadata entry and temporary file are removed.

This proves the ClickHouse temporary-data use case without claiming Velox has a
production spill owner.

### 2. `HttpTemporaryBufferEquivalentReadBack`

This is the readable-writer behavior added by ClickHouse `#48664`.

1. Write deterministic bytes but do not explicitly finalize.
2. Call `getReadBuffer`.
3. Prove it finalizes first and returns exactly `writtenBytes`.
4. Prove a second `finalize` is idempotent.

The test covers the reusable writer API only. Do not wire it into a Velox HTTP
stack.

## Required focused tests

```text
TemporaryDataOnDiskEquivalentLifecycle
HttpTemporaryBufferEquivalentReadBack
OwnedBufferChargedToInjectedPool
ExternalBufferAppendUsesProducerAddress
OwningConstructorRequiresSingleSegment
ReserveFailureAppendsNothingAndReleasesDownloader
WriteFailureDoesNotAdvanceWrittenBytesAndReleasesDownloader
SyncFlushesPendingBytesBeforeUnderlyingSync
FinalizeFlushesPendingBytesBeforeUnderlyingClose
CancelAndDestructorDoNotAppendPendingBytes
NonOwningWriterLeavesCleanupToCaller
```

Use a real temporary FileCache and the existing injectable read/write factories.
Reuse the failure-controlled `WriteFile` pattern from
`FileSegmentTest.cpp`; do not add sleeps, disabled tests, or fake
`FileSegment`/`FileCache` implementations.

## Behavioral RED / mutation matrix

Every row requires: mutate -> build succeeds -> focused test fails for the
declared behavior -> restore -> focused test passes.

| Contract | Required mutation |
|---|---|
| Temporary-data lifecycle | stop removing the last Ephemeral holder; file/metadata cleanup assertion fails |
| Read-back finalization | remove the `finalize` call from `getReadBuffer`; byte-count/read-back assertion fails |
| Pool ownership | allocate outside the injected pool; pool accounting assertion fails |
| External zero-copy append | copy into owned staging storage before append; producer-address assertion fails |
| Single-holder requirement | remove the size check; constructor rejection test fails |
| Reserve failure | call `write` after failed reserve; physical-file/byte assertions fail |
| Write failure | increment `writtenBytes` before `FileSegment::write`; accounting assertion fails |
| Sync ordering | omit outer `next` before local sync; pending bytes are absent |
| Finalize ordering | finalize local writer before outer `next`; pending bytes are absent |
| Cancel/destructor | finalize from the destructor; pending bytes appear unexpectedly |
| Non-owning cleanup | remove/detach the external segment from writer destruction; caller-lifetime assertion fails |

A missing-header compile failure is only the initial TDD compile RED; it does
not satisfy any behavioral row.

## File scope

Modify:

```text
velox/ch/IO/CMakeLists.txt
velox/ch/IO/tests/CMakeLists.txt
```

Create:

```text
velox/ch/IO/WriteBufferToFileSegment.h
velox/ch/IO/WriteBufferToFileSegment.cpp
velox/ch/IO/tests/WriteBufferToFileSegmentTest.cpp
```

Create in ClickHouse:

```text
port/task/result/016-filecache-write-buffer-segment-result.md
```

No modification to `FileSegment`, `FileCache`, `WriteBufferFromFileBase`, or
Gluten is authorized by this contract. Stop and request a contract amendment if
one is required.

Use `velox_sources` in `velox/ch/IO/CMakeLists.txt`; do not call
`target_sources` on a mono alias. Register new public headers in the existing
non-mono `FILE_SET`.

## Execution steps

1. Confirm both repositories are clean and on the expected branches.
2. Confirm Task 015's 15 accumulated `velox_ch_*` CTest targets are green.
3. Add the focused tests first and capture compile RED.
4. Implement the writer within the exact file scope.
5. Execute every behavioral mutation and restore it.
6. Build/run the focused target in mono and non-mono configurations.
7. Run accumulated mono `ctest -R '^velox_ch_'`; expected target count is
   16/16 after registering the new executable.
8. Run `git diff --check` and inspect exact task-owned status.
9. Write the result receipt and stop `ready_for_controller`.

All configure/build/test/mutation commands must source the selected environment,
must not pass `-j`, and must redirect to unique logs under the build directory.

## Stop conditions

Stop as `blocked` instead of improvising if:

```text
the current FileSegment Ephemeral lifecycle differs from the CH contract;
the writer requires a production FileSegment/FileCache API change;
the injected read opener cannot read the finalized path;
the last holder does not remove the Ephemeral file and metadata;
mono or non-mono baseline is not green before implementation;
a behavioral mutation can pass without exercising its claimed branch;
a real Velox temporary-data/spill consumer is required to make the focused
  writer work.
```

## Result receipt

Create `port/task/result/016-filecache-write-buffer-segment-result.md` with:

```text
status: ready_for_controller
environment_profile: root-oss
repository baselines
exact files changed
CH history/consumer mapping
explicit statement that Velox has no production TemporaryDataOnDisk consumer
focused mono/non-mono counts
accumulated CTest count
one RED/mutation/restored-green row per contract
production defects found
review findings/resolutions
blocking errors
```

## Explicit exclusions

```text
No Velox spill policy or TemporaryDataOnDisk-equivalent owner.
No sorting, aggregation, join, or exchange integration.
No HTTP temporary-buffer integration.
No write-through cache or cache_on_write_operations.
No Gluten integration.
No Task 017 observability/cancellation work.
```
