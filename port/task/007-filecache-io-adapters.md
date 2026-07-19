# Task 007: Add `ReadBufferFromVeloxReadFile` and `WriteBufferFromVeloxWriteFile`

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes one
> result file under this ClickHouse checkout. Do not modify any ClickHouse source
> files outside `port/task/result/`. Do not commit or stage either repository.

## Whole-port review registration (2026-07-20)

The post-Task-010 whole-port review recorded two items; Task 007 **remains
accepted** and neither blocks Tasks 011/012.

- **SD9 (forced mapping, no change).** The owned IO buffer maps CH
  `Memory<>` / `std::vector<char>` to a MemoryPool-charged `BufferPtr`. The
  buffer-state layout (`internal_`/`working_`/`position_`/`bytes_`) is 1:1; the
  only delta is that owned memory is now Velox-MemoryPool-charged (accounting +
  lifetime). Forced by the Velox MemoryPool allocation model. Task 012's D3
  download-scratch mapping must stay consistent with this.
- **R6 / 007-2 (signed hard-constraint deviation).** Velox `WriteFile` exposes no
  errno, so a failed append is reconciled by physical file size (`fs::file_size`)
  rather than typed errno. This is the one deviation backed by a hard Velox
  primitive constraint (§E probe). The production reconcile + typed
  `FileCacheErrnoException` consumer path is owned by Task 012.

Authoritative record: `port/task/fullreview/root-oss/1/003-010-review-decisions.md`.

## Post-acceptance source-contract audit — task reopened and contract replaced

The original Task 007 implementation and acceptance remain in history, but the task
is reopened. This section supersedes all reader/writer API, pseudo-code, lifecycle,
and test instructions later in this file.

Canonical design:

```text
port/design/filecache-reader-handoff-and-contract-recovery.html
```

### Architecture boundary

Implement the `FileCache`-consumed CH contract without porting the complete general
CH IO hierarchy:

```text
FileCacheBufferState
  -> optional pool-backed BufferPtr
  -> non-owning working view / position / bytes
  -> set / offset / available / count / swapWorkingState

ch::ReadBufferFromFileBase compatibility contract
  -> ReadBufferFromVeloxReadFile
       -> shared_ptr<velox::ReadFile>
       -> velox::memory::MemoryPool

ch::WriteBufferFromFileBase compatibility contract
  -> WriteBufferFromVeloxWriteFile
       -> unique_ptr<velox::WriteFile>
       -> optional BufferPtr from velox::memory::MemoryPool
```

`FileSegment::RemoteFileReaderPtr` must be able to store the compatibility base.
`FileCacheInputStream` must not compensate for a weaker Task 007 contract by
inventing alternate cursor, retry, detach, or handoff semantics.

### Exact reader contract

The compatibility reader must expose the `FileCache`-used behavior of CH
`BufferBase`, `ReadBuffer`, `SeekableReadBuffer`, and `ReadBufferFromFileBase`:

```text
internalBuffer / buffer / mutable position
offset / available / hasPendingData / count
set(ptr, size) / set(nullptr, 0)
next / eof / cancel / isCanceled
seek / getPosition / getFileOffsetOfBufferEnd
setReadUntilPosition / setReadUntilEnd
supportsExternalBufferMode / supportsRightBoundedReads
getFileName / tryGetFileSize
```

State transitions are exact:

```text
next:
  require no pending data
  settle consumed bytes with bytes += offset
  call nextImpl
  on exception: cancel, then rethrow
  on false: publish an empty working buffer
  on true: publish the new working buffer and reset position to its begin

eof:
  return !hasPendingData && !next

set(ptr, size):
  replace internal/working memory and expose an empty working window
  keep that target until another explicit set

set(nullptr, 0):
  detach all caller memory and leave a coherent empty window
```

After handoff:

```text
available == 0
getFileOffsetOfBufferEnd == FileSegment::getCurrentWriteOffset
no pointer in the reader references the caller-owned output buffer
```

The exception path is not retryable. Remove tests and implementation that restore a
reader for retry after `ReadFile::pread` throws.

### Memory and direct IO

Owned reader/writer buffers must be allocated through `MemoryPool`; do not use
`std::vector<char>`. Query `ReadFile::directIo` at construction. If direct IO is
enabled, allocate owned memory with the returned power-of-two alignment and reject
an external buffer whose address, offset, or length violates the same requirement.
Do not silently switch to buffered IO or another fallback path.

### Exact writer contract

`WriteBufferFromVeloxWriteFile` must directly support the existing
`FileSegment::write` call shape:

```text
construct with buffer size 0
set(from, size, offset=size)
next                         # append exactly offset bytes
set(nullptr, 0)              # detach
```

It must also provide:

```text
finalize: idempotent; on failure transition to canceled before rethrow
cancel: noexcept and idempotent; no-op after finalize
sync
getFileName
```

The external path must append caller memory directly. Do not introduce a mandatory
copy through writer-owned memory.

All writer-owned memory must be `BufferPtr` / `AlignedBuffer` allocated from the
injected `MemoryPool`. A writer constructed with buffer size 0 has no owned
`BufferPtr` and is external-only. Raw pointers are synchronous non-owning views;
the caller's `BufferPtr` remains the lifetime owner until `next` returns and the
writer is detached.

Application-level zero-copy is mandatory:

```text
reader writes directly into FileCacheInputStream BufferPtr B
FileSegment::write passes B to writer.set
nextImpl passes string_view(B) to synchronous WriteFile::append
scope exit detaches B
```

There is no reader-to-writer staging memcpy. Kernel/file IO copying is outside this
contract; `splice`/`sendfile` behavior is not required.

Method mapping:

```text
nextImpl:
  WriteFile::append(string_view(workingBegin, offset))
  do not call WriteFile::flush

sync:
  next
  WriteFile::flush
  remain active

finalizeImpl:
  next
  WriteFile::close
  do not add a separate flush/fsync

cancelImpl:
  append nothing
  discard pending working state
  release the owned WriteFile through a non-throwing destruction path
```

`FileSegment` owns a `shared_ptr<WriteBufferFromVeloxWriteFile>` wrapper, while the
wrapper owns exactly one `unique_ptr<WriteFile>`. When resuming a partial segment,
open the existing file in append/no-truncate mode and verify its physical size
matches `downloadedSize` before the next write.

If `WriteFile::append` throws after a partial physical local write, reconcile from
`std::filesystem::file_size(path)`, require
`downloadedSize <= physicalSize <= reservedSize`, update `downloadedSize` to the
physical size, mark the download failed, and propagate the original exception.
Do not rely on `WriteFile::size`, whose logical counter may not advance before an
exception. This is an explicit infrastructure adaptation: CH can branch on typed
ENOSPC/EDQUOT, while the current Velox `WriteFile` interface does not expose errno,
so every local append exception uses physical file size as the authority.

### Mandatory RED/green evidence

Reader tests:

1. `eof` fills an empty reader and reports false while bytes become available.
2. `next` settles consumed offset before the next `nextImpl`.
3. a `pread` exception cancels the reader and a second read is rejected.
4. external memory remains the target across reads until explicit detach.
5. `set(nullptr, 0)` removes every reference to caller memory.
6. attach/read/write-cache/detach/handoff satisfies both `FileSegment` invariants.
7. right-bound shrink/extend, seek, buffer-end offset, and pending-data behavior match CH.
8. pool accounting observes owned-buffer allocation and release.
9. direct-IO alignment accepts aligned owned/external memory and rejects misalignment.

Writer tests:

1. buffer-size-zero external attach + no-arg `next` appends without an intermediate copy;
   the address observed by the mock `WriteFile` equals the caller `BufferPtr` address.
2. non-zero owned memory is a `BufferPtr` charged to the injected `MemoryPool`.
3. `set(nullptr, 0)` detaches and leaves no caller pointer.
4. `next` appends exactly `offset` bytes, settles count, and resets position.
5. `sync` performs append then flush without close.
6. `finalize` performs append then close without an extra flush; repeated `finalize` is a no-op.
7. `cancel` is noexcept before/after finalize and when repeated, and never appends pending bytes.
8. `next`/finalize failure leaves the writer canceled and prevents a second write.
9. `getFileName` delegates to `WriteFile`.
10. given an already-open `WriteFile` containing a downloaded prefix, appending
    through the adapter preserves that prefix; this proves the adapter never
    truncates an already-open file, but does not claim to prove the opening mode.
11. a `WriteFile` test double physically commits a strict prefix and then throws;
    the adapter propagates the original exception, becomes canceled, performs no
    retry, and leaves the physical prefix observable for `FileSegment` to
    reconcile.

The old Task 007 focused tests may remain only when they assert this replacement
contract. Tests that require retryable reader exceptions, null/zero rejection, or
single-cycle external-buffer auto-revert must be removed or rewritten.

### Controller amendment after Worker attempt 4 — approved task boundary

The user approved the adapter/FileSegment split:

```text
Task 007:
  prove only behavior observable from an already-open ReadFile/WriteFile
  do not open/reopen paths or duplicate FileSegment reconciliation in tests

Task 012:
  production FileSegment opens an existing partial file in append/no-truncate mode
  production FileSegment verifies the existing physical size before continuation
  production FileSegment reconciles a partial physical append from filesystem::file_size
  downloadedSize <= physicalSize <= reservedSize
  reserved-but-unwritten bytes never become downloaded
```

The following Worker-attempt-4 findings are mandatory Task-007 rework:

1. `FileCacheBufferState` must represent null/zero as an explicit coherent empty
   state without null pointer arithmetic. `CacheBuffer::size`,
   `FileCacheBufferState::offset`, and `available` must be null-safe, and `set`
   must reject `offset > size`.
2. `cancel` must discard the pending cursor and detach caller memory while
   remaining noexcept and idempotent. The writer must retain no external pointer
   after explicit cancel or append failure.
3. Direct-IO reads fail closed. Before `ReadFile::pread`, validate the actual
   destination address, file offset, and requested length against the reported
   alignment. `seek` must reject an unaligned next-read offset. A right bound or
   file tail that produces an unaligned request must be rejected explicitly;
   do not silently use buffered IO or round into an unreviewed over-read.
4. Add genuine RED tests for null-detach accessors/finalize, invalid `set` offset,
   cancel/append-failure detachment, and actual direct-IO read attempts with an
   alignment-enforcing mock. Correct the attempt-4 receipt inaccuracies.

## Goal

Implement the two IO adapters that bridge Velox `ReadFile` / `WriteFile` with
the CH-style streaming buffer protocol used throughout the `FileCache` state
machine.

`ReadBufferFromVeloxReadFile` — wraps a `velox::ReadFile` into a stateful
streaming reader with:
- internal or external (caller-supplied) memory
- `setReadUntilPosition` / `seek` / `getPosition` / `getFileOffsetOfBufferEnd`
- `next` to advance the buffer one chunk at a time

`WriteBufferFromVeloxWriteFile` — wraps a `velox::WriteFile` into a buffered
streaming writer with:
- `write(data, len)` / `next(data&, size&)` for accumulating data
- `flush` / `finalize` / `cancel` lifecycle

These adapters have no dependency on `FileCache`, `FileSegment`, or any
scheduling component; they only require Task 003 shims and the Velox file
interfaces.

Deliverables: `velox/ch/IO/ReadBufferFromVeloxReadFile.h/.cpp`,
`velox/ch/IO/WriteBufferFromVeloxWriteFile.h/.cpp`, a new `IO/CMakeLists.txt`,
and a focused test executable `velox_ch_io_test`.

## Controller amendment after Worker attempt 1

This amendment overrides the conflicting CMake, read-buffer pseudo-code, and
ownership-test snippets below.

### Build registration

`velox_ch_filecache` is an alias in the default mono build. Register the `.cpp`
files through the existing `velox_sources` helper. Register the public header
file set with `target_sources` only when `VELOX_MONO_LIBRARY` is disabled and
the target is a real library.

### External-buffer lifecycle

An external buffer installed by `set` is valid for exactly one read cycle.
Every read must be bounded by the active buffer's actual capacity. After that
cycle, `next` must restore the internal buffer before another read; it must
never retain or write through stale caller-owned storage.

### Offset and boundary safety

```text
advance(n):
  reject n < 0 before any pointer arithmetic
  compare n with bufEnd_ - pos_ before computing pos_ + n
  reject attempts to advance past the current buffer

seek(offset, SEEK_SET):
  reject negative offsets

seek(offset, SEEK_CUR):
  support valid negative seeks
  reject positions before zero
  reject positive overflow before addition

setReadUntilPosition(filePos):
  update atEof_ in both directions
  extending the boundary beyond currentOffset_ must allow next() again

set(data, size):
  reject a null pointer or zero capacity

WriteBufferFromVeloxWriteFile::advance(n):
  compare n with buffer_.size() - writePos_ so the validation itself cannot
  overflow
```

Add focused tests for negative and oversized read advances, valid and invalid
negative seeks, resuming after extending `readUntil`, invalid external buffers,
and write-advance overflow.

### Shared-ownership test

Keep the caller's `shared_ptr` outside the writer scope. Prove the file remains
alive after the writer is destroyed while the caller still owns it, then reset
the caller pointer and prove the writer did not leak ownership.

## Controller amendment after Worker attempt 2

This amendment adds lifecycle rules found while tracing every exit from `next`
and every terminal writer transition.

### External-buffer completion

Every attempted `next` call ends the lifetime promised by the preceding `set`,
whether `next` succeeds, returns `false` because of a boundary or physical EOF,
or propagates an exception from `ReadFile::pread`. On every non-success path:

```text
- disarm the external buffer
- restore bufData_ and bufCapacity_ to the internal allocation
- make pos_ and bufEnd_ a coherent empty internal-buffer window
- preserve currentOffset_
- propagate any ReadFile exception unchanged
```

This also applies when `set` is called while `atEof_` is already true. A later
`setReadUntilPosition` extension or retry must never write through the expired
external pointer.

### Boundary changes with buffered data

Shrinking `readUntil_` must also constrain an already-loaded window. After
`setReadUntilPosition(filePos)`, `bufferEnd` must not expose bytes at or beyond
`filePos`. If the new boundary is at or behind the current position, expose an
empty window and mark EOF. Extending a previously reached boundary must still
allow a later `next` to resume from `currentOffset_`.

### Terminal writer state

`advance` is a write operation: after either `finalize` or `cancel`, it must
throw without changing `writePos_` or `totalWritten_`, just like `write` and
`next`.

Add focused TDD coverage for:

```text
- set(external), next() returning false at a boundary, extending the boundary,
  then next() reading into internal memory without modifying external memory
- set(external), ReadFile::pread throwing, then a retry using internal memory
  without modifying external memory
- shrinking readUntil_ while a larger buffer window is already loaded
- advance after finalize
- advance after cancel
```

## Starting point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected HEAD:    Task 006 completed (FileCacheScheduler + QueryIdScope added)
```

Do not require a clean worktree but do not overwrite unrelated changes. Stop
if the branch is not `filecache`.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/01-filecache-port-order-design.md
<clickhouse_repo>/port/3-consumers/03-filecache-buffered-input-design.md
<clickhouse_repo>/port/task/result/006-filecache-scheduler-and-caller-scope-result.md
```

Use the ClickHouse implementations only as behavioral references:

```text
<clickhouse_repo>/src/IO/ReadBufferFromFileBase.h
<clickhouse_repo>/src/IO/WriteBufferFromFile.h
```

Velox APIs:

```text
<velox_repo>/velox/common/file/File.h   (ReadFile, WriteFile)
```

## File scope

Modify:

```text
<velox_repo>/velox/ch/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/IO/CMakeLists.txt
<velox_repo>/velox/ch/IO/ReadBufferFromVeloxReadFile.h
<velox_repo>/velox/ch/IO/ReadBufferFromVeloxReadFile.cpp
<velox_repo>/velox/ch/IO/WriteBufferFromVeloxWriteFile.h
<velox_repo>/velox/ch/IO/WriteBufferFromVeloxWriteFile.cpp
<velox_repo>/velox/ch/IO/tests/CMakeLists.txt
<velox_repo>/velox/ch/IO/tests/IoAdaptersTest.cpp
<clickhouse_repo>/port/task/result/007-filecache-io-adapters-result.md
```

Every new Velox C++ and CMake file must begin with the Apache 2.0 license
header in the repository's comment form.

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected: branch is `filecache`, HEAD is the Task 006 commit or a descendant.
Record all pre-existing dirty files in the result file.

- [ ] **Step 2: Add a failing focused test**

Create `velox/ch/IO/tests/CMakeLists.txt`:

```cmake
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# ...

add_executable(velox_ch_io_test IoAdaptersTest.cpp)
add_test(velox_ch_io_test velox_ch_io_test)

target_link_libraries(
  velox_ch_io_test
  PRIVATE
    velox_ch_filecache
    velox_test_util
    velox_exception
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

Create `velox/ch/IO/tests/IoAdaptersTest.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/ch/IO/ReadBufferFromVeloxReadFile.h"
#include "velox/ch/IO/WriteBufferFromVeloxWriteFile.h"
#include "velox/common/base/Exceptions.h"
#include "velox/common/file/File.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace facebook::velox::ch
{
namespace
{

// ---------------------------------------------------------------------------
// In-memory mock ReadFile
// ---------------------------------------------------------------------------

class MockReadFile : public velox::ReadFile
{
public:
    explicit MockReadFile(std::string data) : data_(std::move(data)) {}

    std::string_view pread(
        uint64_t offset,
        uint64_t length,
        void * buf,
        const velox::FileIoContext &) const override
    {
        const uint64_t available =
            offset < data_.size() ? data_.size() - offset : 0;
        const uint64_t toRead = std::min(length, available);
        if (toRead > 0)
            std::memcpy(buf, data_.data() + offset, toRead);
        bytesRead_ += toRead;
        return {static_cast<const char *>(buf), toRead};
    }

    bool shouldCoalesce() const override { return false; }
    uint64_t size() const override { return data_.size(); }
    uint64_t memoryUsage() const override { return data_.size(); }
    std::string getName() const override { return "MockReadFile"; }
    uint64_t getNaturalReadSize() const override { return 4096; }

    uint64_t callCount() const { return callCount_; }

private:
    std::string data_;
    mutable uint64_t callCount_{0};
};

// ---------------------------------------------------------------------------
// In-memory mock WriteFile
// ---------------------------------------------------------------------------

class MockWriteFile : public velox::WriteFile
{
public:
    void append(std::string_view data) override
    {
        VELOX_CHECK(!closed_, "WriteFile already closed");
        VELOX_CHECK(!cancelled_, "WriteFile already cancelled");
        content_.append(data);
    }

    void flush() override
    {
        flushed_ = true;
    }

    void close() override
    {
        closed_ = true;
    }

    uint64_t size() const override { return content_.size(); }

    const std::string getName() const override
    {
        return "MockWriteFile";
    }

    const std::string & content() const { return content_; }
    bool isClosed() const { return closed_; }
    bool isFlushed() const { return flushed_; }
    bool isCancelled() const { return cancelled_; }

    void setCancelled() { cancelled_ = true; }

private:
    std::string content_;
    bool closed_{false};
    bool flushed_{false};
    bool cancelled_{false};
};

// ---------------------------------------------------------------------------
// ReadBufferFromVeloxReadFile tests
// ---------------------------------------------------------------------------

TEST(ReadBufferFromVeloxReadFileTest, NextReadsDataInChunks)
{
    const std::string data(8192, 'A');
    auto rf = std::make_shared<MockReadFile>(data);
    ReadBufferFromVeloxReadFile reader(rf, /*bufferSize=*/4096);

    std::string result;
    while (reader.next())
    {
        result.append(reader.position(), reader.bufferEnd() - reader.position());
        reader.advance(reader.bufferEnd() - reader.position());
    }
    EXPECT_EQ(result, data);
}

TEST(ReadBufferFromVeloxReadFileTest, NextReturnsFalseAtEof)
{
    auto rf = std::make_shared<MockReadFile>("hello");
    ReadBufferFromVeloxReadFile reader(rf);

    ASSERT_TRUE(reader.next());
    reader.advance(reader.bufferEnd() - reader.position());
    EXPECT_FALSE(reader.next());
    EXPECT_TRUE(reader.eof());
}

TEST(ReadBufferFromVeloxReadFileTest, GetPositionTracksConsumption)
{
    const std::string data(1024, 'B');
    auto rf = std::make_shared<MockReadFile>(data);
    ReadBufferFromVeloxReadFile reader(rf, 512);

    EXPECT_EQ(reader.getPosition(), 0);
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.getFileOffsetOfBufferEnd(), 512);

    reader.advance(256);
    EXPECT_EQ(reader.getPosition(), 256);
}

TEST(ReadBufferFromVeloxReadFileTest, SetReadUntilPositionLimitsReads)
{
    const std::string data(4096, 'C');
    auto rf = std::make_shared<MockReadFile>(data);
    ReadBufferFromVeloxReadFile reader(rf, 4096);

    reader.setReadUntilPosition(512);

    ASSERT_TRUE(reader.next());
    // Must not read past the readUntil boundary.
    EXPECT_LE(
        static_cast<size_t>(reader.bufferEnd() - reader.position()), 512u);
}

TEST(ReadBufferFromVeloxReadFileTest, SeekRepositionsToAbsoluteOffset)
{
    const std::string data = "0123456789";
    auto rf = std::make_shared<MockReadFile>(data);
    ReadBufferFromVeloxReadFile reader(rf);

    reader.seek(5);
    EXPECT_EQ(reader.getPosition(), 5);

    ASSERT_TRUE(reader.next());
    EXPECT_EQ(std::string(reader.position(), reader.bufferEnd()), "56789");
}

TEST(ReadBufferFromVeloxReadFileTest, ExternalBufferIsUsedForReads)
{
    const std::string data = "external-buffer-test";
    auto rf = std::make_shared<MockReadFile>(data);
    ReadBufferFromVeloxReadFile reader(rf);

    // Provide an external buffer; next() should write into it directly.
    std::vector<char> externalBuf(data.size());
    reader.set(externalBuf.data(), externalBuf.size());

    ASSERT_TRUE(reader.next());
    EXPECT_EQ(
        std::string(externalBuf.data(), data.size()),
        data);
}

TEST(ReadBufferFromVeloxReadFileTest, NonOwningConstructorDoesNotDelete)
{
    const std::string data = "non-owning";
    MockReadFile rawFile(data);

    {
        ReadBufferFromVeloxReadFile reader(&rawFile);
        ASSERT_TRUE(reader.next());
    }
    // rawFile must still be usable after reader is destroyed.
    EXPECT_EQ(rawFile.size(), data.size());
}

TEST(ReadBufferFromVeloxReadFileTest, GetFileNameDelegates)
{
    auto rf = std::make_shared<MockReadFile>("x");
    ReadBufferFromVeloxReadFile reader(rf);
    EXPECT_EQ(reader.getFileName(), "MockReadFile");
}

// ---------------------------------------------------------------------------
// WriteBufferFromVeloxWriteFile tests
// ---------------------------------------------------------------------------

TEST(WriteBufferFromVeloxWriteFileTest, WriteAccumulatesAndFlushCommits)
{
    auto wf = std::make_shared<MockWriteFile>();
    WriteBufferFromVeloxWriteFile writer(wf, 4096);

    writer.write("hello", 5);
    writer.write(" world", 6);

    EXPECT_TRUE(wf->content().empty()); // buffered, not yet flushed
    writer.flush();
    EXPECT_EQ(wf->content(), "hello world");
    EXPECT_TRUE(wf->isFlushed());
}

TEST(WriteBufferFromVeloxWriteFileTest, FinalizeFlushesAndCloses)
{
    auto wf = std::make_shared<MockWriteFile>();
    WriteBufferFromVeloxWriteFile writer(wf, 4096);

    writer.write("data", 4);
    writer.finalize();

    EXPECT_EQ(wf->content(), "data");
    EXPECT_TRUE(wf->isClosed());
}

TEST(WriteBufferFromVeloxWriteFileTest, CancelAbandonsBufferedData)
{
    auto wf = std::make_shared<MockWriteFile>();
    WriteBufferFromVeloxWriteFile writer(wf, 4096);

    writer.write("discard", 7);
    writer.cancel();

    EXPECT_TRUE(wf->content().empty());
    EXPECT_FALSE(wf->isClosed());
}

TEST(WriteBufferFromVeloxWriteFileTest, NextGetWritableChunkAndShortWrite)
{
    auto wf = std::make_shared<MockWriteFile>();
    WriteBufferFromVeloxWriteFile writer(wf, 64);

    // next() provides a writable chunk.
    char * data = nullptr;
    int64_t size = 0;
    writer.next(data, size);
    ASSERT_NE(data, nullptr);
    ASSERT_GT(size, 0);

    // Write only 3 bytes (short write).
    std::memcpy(data, "abc", 3);
    writer.advance(3);

    // Flush should commit only the 3 bytes actually written.
    writer.flush();
    EXPECT_EQ(wf->content(), "abc");
}

TEST(WriteBufferFromVeloxWriteFileTest, BufferAutoFlushesWhenFull)
{
    auto wf = std::make_shared<MockWriteFile>();
    // Small buffer to force auto-flush.
    WriteBufferFromVeloxWriteFile writer(wf, 8);

    const std::string payload(16, 'Z');
    writer.write(payload.data(), payload.size());

    // At least the first 8 bytes must have been flushed already.
    EXPECT_GE(wf->content().size(), 8u);
}

TEST(WriteBufferFromVeloxWriteFileTest, GetPositionTracksWrittenBytes)
{
    auto wf = std::make_shared<MockWriteFile>();
    WriteBufferFromVeloxWriteFile writer(wf, 4096);

    EXPECT_EQ(writer.getPosition(), 0u);
    writer.write("abcde", 5);
    EXPECT_EQ(writer.getPosition(), 5u);
}

TEST(WriteBufferFromVeloxWriteFileTest, OwnershipTransferSharedPtr)
{
    std::weak_ptr<MockWriteFile> weak;
    {
        auto wf = std::make_shared<MockWriteFile>();
        weak = wf;
        WriteBufferFromVeloxWriteFile writer(wf);
        writer.write("x", 1);
        writer.finalize();
    }
    // writer released its shared_ptr; the original wf is still alive here
    // because we hold wf on the stack (it was moved into writer).
    EXPECT_FALSE(weak.expired());
}

} // namespace
} // namespace facebook::velox::ch
```

Create `velox/ch/IO/CMakeLists.txt`:

```cmake
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# ...

target_sources(
  velox_ch_filecache
  PRIVATE
    ReadBufferFromVeloxReadFile.cpp
    WriteBufferFromVeloxWriteFile.cpp
  PUBLIC
    FILE_SET HEADERS
    FILES
      ReadBufferFromVeloxReadFile.h
      WriteBufferFromVeloxWriteFile.h
)

if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

- [ ] **Step 3: Wire the IO subdirectory**

Append `add_subdirectory(IO)` to `velox/ch/CMakeLists.txt` so the file reads:

```cmake
add_subdirectory(Common)
add_subdirectory(Interpreters)
add_subdirectory(IO)
```

- [ ] **Step 4: Verify the test fails before implementation**

Reconfigure:

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`. For `home-chang`, also add
`-DVELOX_BUILD_TESTING=ON` (already present in the `root-oss` effective
configuration). Redirect output to `<velox_build_dir>/configure_task_007_io.log`.

Try to build:

```bash
if <ninja> \
  -C <velox_build_dir> \
  velox_ch_io_test \
  > <velox_build_dir>/build_task_007_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected: configure succeeds; build fails because
`ReadBufferFromVeloxReadFile.h` does not exist.

- [ ] **Step 5: Implement `ReadBufferFromVeloxReadFile.h`**

Create `velox/ch/IO/ReadBufferFromVeloxReadFile.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

#pragma once

#include "velox/ch/Common/FileCacheException.h"
#include "velox/common/file/File.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace facebook::velox::ch
{

/// CH-style streaming read buffer over a Velox `ReadFile`.
///
/// Buffer layout and semantics mirror `ReadBufferFromFileBase` in ClickHouse:
///
///   [bufferData_  ...  pos_  ...  bufEnd_]
///              internal allocation
///
/// - `next()` reads the next chunk from the underlying `ReadFile::pread`,
///   starting at `currentOffset_`, and returns true if any bytes were loaded.
/// - The buffer window is bounded by `readUntil_` (set via
///   `setReadUntilPosition`); reads stop at or before that offset.
/// - `seek(offset)` repositions `currentOffset_` and clears the buffer so
///   the next `next()` reads from the new position.
/// - External buffer support: `set(data, size)` points the reader at
///   caller-owned memory for the next read cycle (used by `FileCacheInputStream`
///   for zero-copy cache segment writing).
///
/// The reader may be constructed with shared or raw (non-owning) pointer
/// ownership; callers are responsible for ensuring the `ReadFile` outlives
/// this reader when using the raw-pointer form.
class ReadBufferFromVeloxReadFile
{
public:
    static constexpr size_t kDefaultBufferSize = 1u << 20; // 1 MiB

    // Shared-ownership constructor.
    explicit ReadBufferFromVeloxReadFile(
        std::shared_ptr<velox::ReadFile> readFile,
        size_t bufferSize = kDefaultBufferSize);

    // Non-owning (raw pointer) constructor.
    ReadBufferFromVeloxReadFile(
        velox::ReadFile * readFile,
        size_t bufferSize = kDefaultBufferSize);

    ReadBufferFromVeloxReadFile(const ReadBufferFromVeloxReadFile &) = delete;
    ReadBufferFromVeloxReadFile &
    operator=(const ReadBufferFromVeloxReadFile &) = delete;

    ~ReadBufferFromVeloxReadFile() = default;

    /// Load the next chunk into the buffer.
    /// Returns true if at least one byte was loaded; false on EOF or if
    /// the `readUntil` boundary has been reached.
    bool next();

    /// Pointer to the first unread byte in the current buffer.
    const char * position() const { return pos_; }

    /// Pointer one past the last available byte in the current buffer.
    const char * bufferEnd() const { return bufEnd_; }

    /// True when the buffer is exhausted and `next()` returned false.
    bool eof() const { return atEof_; }

    /// Advance the read pointer by `n` bytes within the current buffer.
    void advance(ptrdiff_t n);

    /// Absolute file offset corresponding to `position()`.
    off_t getPosition() const;

    /// Absolute file offset corresponding to `bufferEnd()`.
    off_t getFileOffsetOfBufferEnd() const;

    /// Seek to absolute file offset `offset` (SEEK_SET) or relative to the
    /// current logical position (SEEK_CUR).  Clears the buffer so the next
    /// `next()` reads from the new position.
    void seek(off_t offset, int whence = SEEK_SET);

    /// Limit reads to [currentOffset_, filePos).  Reads stop when
    /// `currentOffset_ >= readUntil_`.  Pass `fileSize()` to clear the limit.
    void setReadUntilPosition(size_t filePos);

    /// Switch to an external caller-owned buffer for the next read cycle.
    /// The caller must guarantee that `data` remains valid until the next
    /// `next()` call, `seek()`, or `set()` call.
    void set(char * data, size_t size);

    std::string getFileName() const;

private:
    velox::ReadFile * readFile_{nullptr};
    std::shared_ptr<velox::ReadFile> ownedReadFile_;

    // Internal buffer.
    std::vector<char> internalBuffer_;

    // Buffer pointers.
    char * bufData_{nullptr};  // buffer start (internal or external)
    char * pos_{nullptr};
    char * bufEnd_{nullptr};
    size_t bufCapacity_{0};

    // Read state.
    size_t currentOffset_{0}; // file offset of pos_
    size_t readUntil_{static_cast<size_t>(-1)};
    bool atEof_{false};
    bool externalBuffer_{false};

    void initBuffer(size_t bufferSize);
};

} // namespace facebook::velox::ch
```

- [ ] **Step 6: Implement `ReadBufferFromVeloxReadFile.cpp`**

Create `velox/ch/IO/ReadBufferFromVeloxReadFile.cpp`.  Implement each method
as described below.

**Constructor (shared-ownership)**

```text
ownedReadFile_ = readFile
readFile_ = readFile.get()
initBuffer(bufferSize)
readUntil_ = readFile_->size()
```

**Constructor (non-owning)**

```text
readFile_ = readFile
initBuffer(bufferSize)
readUntil_ = readFile_->size()
```

**`initBuffer`**

```text
internalBuffer_.resize(bufferSize)
bufData_ = internalBuffer_.data()
bufCapacity_ = bufferSize
pos_ = bufData_
bufEnd_ = bufData_
externalBuffer_ = false
```

**`next`**

```text
if atEof_: return false
if currentOffset_ >= readUntil_:
    atEof_ = true
    return false

const toRead = min(
    externalBuffer_ ? bufCapacity_ : internalBuffer_.size(),
    readUntil_ - currentOffset_)

// pread into buffer
string_view sv = readFile_->pread(currentOffset_, toRead, bufData_)

if sv.empty():
    atEof_ = true
    return false

bufEnd_ = bufData_ + sv.size()
pos_    = bufData_
externalBuffer_ = false   // reset external flag after one use
return true
```

**`advance(n)`**

```text
VELOX_CHECK(pos_ + n <= bufEnd_, "advance past buffer end")
pos_ += n
currentOffset_ = bufferStartOffset() + (pos_ - bufData_)
```

where `bufferStartOffset()` returns the file offset at `bufData_` (maintained
as a field `bufStartOffset_` updated on each `next()` or `seek()`).

**`getPosition`**

```text
return static_cast<off_t>(currentOffset_)
```

**`getFileOffsetOfBufferEnd`**

```text
return static_cast<off_t>(bufStartOffset_ + (bufEnd_ - bufData_))
```

**`seek(offset, whence)`**

```text
if whence == SEEK_SET:
    currentOffset_ = static_cast<size_t>(offset)
elif whence == SEEK_CUR:
    currentOffset_ = static_cast<size_t>(getPosition() + offset)
else:
    throw

// Invalidate buffer.
pos_ = bufData_
bufEnd_ = bufData_
bufStartOffset_ = currentOffset_
atEof_ = false
```

**`setReadUntilPosition(filePos)`**

```text
readUntil_ = filePos
if currentOffset_ >= readUntil_:
    atEof_ = true
```

**`set(data, size)`**

```text
bufData_ = data
bufCapacity_ = size
pos_ = data
bufEnd_ = data   // not yet filled; next() will fill on the next call
bufStartOffset_ = currentOffset_
externalBuffer_ = true
```

**`getFileName`**

```text
return readFile_ ? readFile_->getName() : ""
```

Add the `bufStartOffset_` field to the class and keep it in sync:
- Initialised to `0`.
- Updated in `next()`: set to `currentOffset_` before calling `pread`.
- Updated in `seek()`: set to the new `currentOffset_`.

- [ ] **Step 7: Implement `WriteBufferFromVeloxWriteFile.h`**

Create `velox/ch/IO/WriteBufferFromVeloxWriteFile.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

#pragma once

#include "velox/ch/Common/FileCacheException.h"
#include "velox/common/file/File.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace facebook::velox::ch
{

/// CH-style buffered streaming writer over a Velox `WriteFile`.
///
/// Data is accumulated in an internal buffer and flushed to the underlying
/// `WriteFile::append` either explicitly (`flush` / `finalize`) or
/// automatically when the buffer becomes full.
///
/// Short-write reconciliation contract:
///   `next(data, size)` exposes the remaining capacity of the internal
///   buffer.  If the caller writes fewer than `size` bytes, it must call
///   `advance(n)` with the actual count before calling `next` or `flush`
///   again.  `flush` only commits `writePos_` bytes, not the whole capacity.
///
/// Lifecycle:
///   `flush`    — commit buffered bytes to WriteFile; leave buffer ready.
///   `finalize` — flush + close; further writes throw.
///   `cancel`   — discard buffered bytes; do not call append; do not close.
class WriteBufferFromVeloxWriteFile
{
public:
    static constexpr size_t kDefaultBufferSize = 1u << 20; // 1 MiB

    explicit WriteBufferFromVeloxWriteFile(
        std::shared_ptr<velox::WriteFile> writeFile,
        size_t bufferSize = kDefaultBufferSize);

    WriteBufferFromVeloxWriteFile(const WriteBufferFromVeloxWriteFile &) = delete;
    WriteBufferFromVeloxWriteFile &
    operator=(const WriteBufferFromVeloxWriteFile &) = delete;

    ~WriteBufferFromVeloxWriteFile() = default;

    /// Write `len` bytes from `buf` into the buffer; auto-flush if full.
    void write(const char * buf, size_t len);

    /// Expose the remaining write capacity: sets `data` to the first
    /// unused byte in the buffer and `size` to the number of bytes available.
    /// The caller must call `advance(n)` with the bytes actually written.
    void next(char *& data, int64_t & size);

    /// Advance the write position by `n` bytes (used after `next`).
    void advance(size_t n);

    /// Flush the buffer: call `WriteFile::append` with all pending bytes,
    /// then reset the write position to 0.
    void flush();

    /// Flush and close the `WriteFile`.  Further writes throw.
    void finalize();

    /// Discard buffered bytes without flushing.  The `WriteFile` is not
    /// closed or otherwise modified.
    void cancel();

    /// Total bytes written so far (includes already-flushed bytes).
    size_t getPosition() const { return totalWritten_; }

private:
    std::shared_ptr<velox::WriteFile> writeFile_;
    std::vector<char> buffer_;
    size_t writePos_{0};      // bytes in buffer pending flush
    size_t totalWritten_{0};  // cumulative bytes passed to append
    bool finalized_{false};
    bool cancelled_{false};

    void flushInternal();
};

} // namespace facebook::velox::ch
```

- [ ] **Step 8: Implement `WriteBufferFromVeloxWriteFile.cpp`**

Create `velox/ch/IO/WriteBufferFromVeloxWriteFile.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * (license header)
 */

#include "velox/ch/IO/WriteBufferFromVeloxWriteFile.h"
#include "velox/ch/Common/FileCacheException.h"

#include <cstring>

namespace facebook::velox::ch
{

WriteBufferFromVeloxWriteFile::WriteBufferFromVeloxWriteFile(
    std::shared_ptr<velox::WriteFile> writeFile,
    size_t bufferSize)
    : writeFile_(std::move(writeFile))
    , buffer_(bufferSize)
{
    VELOX_CHECK_NOT_NULL(writeFile_);
    VELOX_CHECK_GT(bufferSize, 0u, "WriteBuffer size must be > 0");
}

void WriteBufferFromVeloxWriteFile::write(const char * buf, size_t len)
{
    VELOX_CHECK(!finalized_, "write after finalize");
    VELOX_CHECK(!cancelled_, "write after cancel");

    size_t written = 0;
    while (written < len)
    {
        const size_t capacity = buffer_.size();
        if (writePos_ == capacity)
            flushInternal();

        const size_t available = capacity - writePos_;
        const size_t chunk = std::min(available, len - written);
        std::memcpy(buffer_.data() + writePos_, buf + written, chunk);
        writePos_ += chunk;
        written += chunk;
    }
    totalWritten_ += len;
}

void WriteBufferFromVeloxWriteFile::next(char *& data, int64_t & size)
{
    VELOX_CHECK(!finalized_, "next after finalize");
    VELOX_CHECK(!cancelled_, "next after cancel");

    if (writePos_ == buffer_.size())
        flushInternal();

    data = buffer_.data() + writePos_;
    size = static_cast<int64_t>(buffer_.size() - writePos_);
}

void WriteBufferFromVeloxWriteFile::advance(size_t n)
{
    VELOX_CHECK_LE(
        writePos_ + n,
        buffer_.size(),
        "advance past buffer capacity");
    writePos_ += n;
    totalWritten_ += n;
}

void WriteBufferFromVeloxWriteFile::flush()
{
    if (!finalized_ && !cancelled_)
    {
        flushInternal();
        writeFile_->flush();
    }
}

void WriteBufferFromVeloxWriteFile::finalize()
{
    VELOX_CHECK(!finalized_, "finalize called twice");
    VELOX_CHECK(!cancelled_, "finalize after cancel");
    flushInternal();
    writeFile_->flush();
    writeFile_->close();
    finalized_ = true;
}

void WriteBufferFromVeloxWriteFile::cancel()
{
    VELOX_CHECK(!finalized_, "cancel after finalize");
    writePos_ = 0;
    cancelled_ = true;
}

void WriteBufferFromVeloxWriteFile::flushInternal()
{
    if (writePos_ == 0)
        return;
    writeFile_->append(std::string_view(buffer_.data(), writePos_));
    writePos_ = 0;
}

} // namespace facebook::velox::ch
```

Note: `totalWritten_` is updated in `write()` and `advance()`, which are the
only callers that add bytes.  `flushInternal()` only transfers the already-
counted bytes to the `WriteFile`; it must not update `totalWritten_`.

- [ ] **Step 9: Build the focused test**

Reconfigure using the same command as Step 4, then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_io_test \
  > <velox_build_dir>/build_task_007_io.log 2>&1
```

Expected: exit code 0.  Do not add `-j`.

- [ ] **Step 10: Run the focused test**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_io_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_007_io.log 2>&1
```

Expected: `100% tests passed, 0 tests failed.`

- [ ] **Step 11: Regression check**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^(velox_ch_common_test|velox_ch_guards_test|velox_ch_threadpool_test|velox_ch_scheduler_test)$' \
  --output-on-failure \
  >> <velox_build_dir>/test_task_007_io.log 2>&1
```

Expected: all four tests pass.

- [ ] **Step 12: Inspect only task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/CMakeLists.txt \
  velox/ch/IO/CMakeLists.txt \
  velox/ch/IO/ReadBufferFromVeloxReadFile.h \
  velox/ch/IO/ReadBufferFromVeloxReadFile.cpp \
  velox/ch/IO/WriteBufferFromVeloxWriteFile.h \
  velox/ch/IO/WriteBufferFromVeloxWriteFile.cpp \
  velox/ch/IO/tests/CMakeLists.txt \
  velox/ch/IO/tests/IoAdaptersTest.cpp
```

Expected: no whitespace errors; no files outside the declared scope changed;
changes remain unstaged.

- [ ] **Step 13: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/007-filecache-io-adapters-result.md
```

Use exactly this structure:

````markdown
# Task 007 Result: Add `ReadBufferFromVeloxReadFile` and `WriteBufferFromVeloxWriteFile`

## Status

status: success

## Velox status

```text
<paste branch, HEAD, and final `git status --short`>
```

## Files changed

```text
<list only task-owned files>
```

## Commands run

```text
<paste configure, build, test, and verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_task_007_io.log
<velox_build_dir>/build_task_007_red.log
<velox_build_dir>/build_task_007_io.log
<velox_build_dir>/test_task_007_io.log
```

## Verification

```text
Red build failed because ReadBufferFromVeloxReadFile.h was absent.
Final build exit code:
Focused test result:
Regression test result:
git diff --check result:
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 008: port SipHash128, FileCacheKey, FileSegmentKeyType,
FileCacheOriginInfo, forward files, and FileCacheUtils
leaf types, and FileCacheSettings (port-order stage 1).
```
````

## Explicit exclusions

Do not implement in this task:

```text
FileCacheInputStream / FileCacheBufferedInput — these use ReadBufferFromVeloxReadFile
    as a dependency but are stage-6 consumers; port them later
CachedReadFile / CachedWriteFile wrappers
WriteBufferToFileSegment — serves TemporaryDataOnDisk / Ephemeral segments,
    not the read-miss cache-fill path; deferred
ReadFile::preadv / preadvAsync optimisations
FileCache, FileSegment, Metadata algorithms
SipHash128
Gluten integration
```
