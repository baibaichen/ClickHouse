# Task 016: Port `WriteBufferToFileSegment` — Ephemeral/TemporaryDataOnDisk Use Only

> **Post-MVP optional task.**
>
> **Prerequisite:** Tasks 004–014 (Velox core `FileCache` port including
> `FileSegment`, `reserve`, `write`, `completePartAndResetDownloader`, and
> `getLocalCacheWriter`) must be complete and on the `filecache` branch
> before this task begins.
>
> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes
> one result file under this ClickHouse checkout. Do not modify ClickHouse
> source files. Do not commit or stage either repository.

## Pre-execution compatibility amendment

This task remains post-MVP. It does not move into the current Tasks 003-014 phase.
Its writer contract is nevertheless fixed now by:

```text
port/design/filecache-reader-handoff-and-contract-recovery.html
```

This amendment supersedes every conflicting class declaration, `std::vector` buffer,
standalone cursor/finalize state machine, destructor flush, and swap-less
`sync`/`finalize` pseudo-code in the numbered Steps below. Before Task 016 starts,
those Steps must be rewritten from this amendment; they must not be copied as
implementation instructions.

`WriteBufferToFileSegment` must derive from the same flattened
`ch::WriteBufferFromFileBase` compatibility contract used by
`WriteBufferFromVeloxWriteFile`. It owns a pool-backed Velox `BufferPtr`; do not use
`std::vector<char>` or create an independent cursor/finalize/cancel state machine.

Application-level zero-copy path:

```text
producer writes directly into WriteBufferToFileSegment BufferPtr T
nextImpl reserves offset() bytes
FileSegment::write borrows T
local cache writer borrows the same T and calls WriteFile::append(string_view(T))
local cache writer detaches T
```

No intermediate writer staging memcpy is allowed. Kernel IO copying is outside the
contract.

`nextImpl` must:

1. acquire the downloader;
2. install a scope guard that releases it;
3. reserve exactly `offset` bytes;
4. call `FileSegment::write` with the owned `BufferPtr` address;
5. increment `writtenBytes` only after success.

`finalizeImpl` and `sync` first call `next`, then obtain the segment's local cache
writer and use an RAII `BufferStateSwapGuard`. The guard swaps only working
internal/working views and position. It swaps neither settled-byte counters nor
`BufferPtr`/`WriteFile` ownership, and always swaps back in its non-throwing
destructor.

The executable tests must prove:

```text
owned BufferPtr is charged to and released from the injected MemoryPool
append observes the same address as the producer-owned BufferPtr
reserve failure appends nothing and releases downloader
write failure releases downloader and leaves holder cleanup responsible
finalize and sync use BufferStateSwapGuard and restore both states
read-back finalizes first and returns exactly writtenBytes
```

## Goal

Port `WriteBufferToFileSegment` from ClickHouse to
`velox/ch/IO/WriteBufferToFileSegment.{h,cpp}` for use by
`FileSegmentKind::Ephemeral` segments only (i.e., as a write-back target for
`TemporaryDataOnDisk`-equivalent workloads). This is **not** write-through
cache; it does not implement `cache_on_write_operations`.

The class takes ownership of a single-segment `FileSegmentsHolder` or a
non-owning pointer to an externally held `FileSegment`. It:

1. Claims the downloader identity via `FileSegment::getOrSetDownloader()`.
2. Reserves space via `FileSegment::reserve()` on each flush.
3. Writes bytes via `FileSegment::write()`.
4. Releases the downloader via `FileSegment::completePartAndResetDownloader()`.
5. Finalizes via `FileSegment::getLocalCacheWriter()::finalize()` (or
   the equivalent flush path for the Velox port).
6. Supports `sync()` and read-back via `getReadBufferImpl()`.
7. On failure, the segment is left in a partially-written state; the holder
   destructor handles cleanup.

Deliverable: `velox_ch_write_buffer_test` passes all scenarios.

## Starting point

```text
Velox repository:    <velox_repo>
Required branch:     filecache
Expected HEAD:       descendant of the task-014 result commit
                     (FileSegment, reserve, write, completePartAndResetDownloader,
                     and getLocalCacheWriter must be implemented)
```

Stop if the branch is not `filecache` or if `velox/ch/Interpreters/FileCache/FileSegment.h`
is absent.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/2-file-cache/09-filecache-file-segment-design.md
<clickhouse_repo>/port/01-filecache-port-order-design.md
  (section "阶段 4：中心 SCC 第二组：FileSegment", subsection "可以 stub 的内容")
<clickhouse_repo>/port/task/result/014-filecache-buffered-input-result.md
```

Use the ClickHouse implementations **only** as behavioral references:

```text
<clickhouse_repo>/src/Interpreters/FileCache/WriteBufferToFileSegment.h
<clickhouse_repo>/src/Interpreters/FileCache/WriteBufferToFileSegment.cpp
```

## File scope

Modify in the Velox checkout:

```text
<velox_repo>/velox/ch/IO/CMakeLists.txt
<velox_repo>/velox/ch/IO/tests/CMakeLists.txt
```

Create in the Velox checkout:

```text
<velox_repo>/velox/ch/IO/WriteBufferToFileSegment.h
<velox_repo>/velox/ch/IO/WriteBufferToFileSegment.cpp
<velox_repo>/velox/ch/IO/tests/WriteBufferToFileSegmentTest.cpp
```

Create in the ClickHouse checkout:

```text
<clickhouse_repo>/port/task/result/016-filecache-write-buffer-segment-result.md
```

Every new Velox C++ file must begin with the Apache 2.0 Facebook license
header from `port/task/003-filecache-basic-common-shims.md`.

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline

# Verify that the FileSegment API is present.
grep -n "getOrSetDownloader\|reserve\|getLocalCacheWriter\|completePartAndResetDownloader" \
  velox/ch/Interpreters/FileCache/FileSegment.h | head -20
```

Expected:

```text
Branch is filecache.
All four method names appear in FileSegment.h.
Record pre-existing dirty files in the result file.
```

If any method is absent, stop and report which task owns it.

- [ ] **Step 2: Wire the IO tests subdirectory into CMake**

If `velox/ch/IO/CMakeLists.txt` does not already contain a
`add_subdirectory(tests)` guard, append:

```cmake
if(${VELOX_BUILD_TESTING} OR ${VELOX_BUILD_TEST_UTILS})
  add_subdirectory(tests)
endif()
```

Append the following test target to the existing
`velox/ch/IO/tests/CMakeLists.txt`:

```cmake
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_executable(velox_ch_write_buffer_test WriteBufferToFileSegmentTest.cpp)
add_test(velox_ch_write_buffer_test velox_ch_write_buffer_test)

target_link_libraries(
  velox_ch_write_buffer_test
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

Verify `velox/ch/CMakeLists.txt` includes the IO subdirectory. If it does
not, append:

```cmake
add_subdirectory(IO)
```

- [ ] **Step 3: Create a failing test**

Create
`velox/ch/IO/tests/WriteBufferToFileSegmentTest.cpp` with all cases as
`GTEST_SKIP()` stubs so that the file compiles before the implementation
exists:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

#include "velox/ch/IO/WriteBufferToFileSegment.h"
#include "velox/ch/Interpreters/FileCache/FileCache.h"
#include "velox/ch/Interpreters/FileCache/FileSegment.h"
#include "velox/ch/Interpreters/FileCache/FileCacheManager.h"
#include "velox/common/testutil/TempDirectoryPath.h"

#include <gtest/gtest.h>

namespace facebook::velox::ch
{
namespace
{

TEST(WriteBufferToFileSegmentTest, WriteAndReadBack) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, ReserveFailureThrows) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, DownloaderReleasedOnFlush) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, FinalizeFlushesAndCompletesSegment) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, SyncFlushesBufferedBytes) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, HolderConstructorRequiresSingleSegment) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, ExternalSegmentConstructor) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, FailureLeaveSegmentForHolderCleanup) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, ReadBackAfterFinalizeReturnsWrittenBytes) {
    GTEST_SKIP() << "implement";
}

TEST(WriteBufferToFileSegmentTest, ReadBackAfterFinalizeReturnsEmptyWhenNoBytesWritten) {
    GTEST_SKIP() << "implement";
}

}
}
```

Reconfigure and build the skeleton (must compile):

```bash
# Follow the selected profile's configure recipe from ENVIRONMENT.md.
# For root-oss: source <velox_env> first. For home-chang: add -DVELOX_BUILD_TESTING=ON
# (already included in the root-oss effective configuration).
# Redirect to <velox_build_dir>/configure_016.log.

<ninja> \
  -C <velox_build_dir> \
  velox_ch_write_buffer_test \
  > <velox_build_dir>/build_016_red.log 2>&1
echo "exit: $?"
```

Expected: fails because `WriteBufferToFileSegment.h` does not exist yet.
Record the first error in the result file.

- [ ] **Step 4: Create `WriteBufferToFileSegment.h`**

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

#pragma once

#include "velox/ch/Interpreters/FileCache/FileSegment.h"
#include "velox/ch/IO/ReadBufferFromVeloxReadFile.h"

#include <memory>

namespace facebook::velox::ch
{

/// Streaming write buffer that stores bytes into a single Ephemeral
/// FileSegment.  The segment is reserved before each write and the downloader
/// identity is claimed and released on every flush cycle.
///
/// This class is used by TemporaryDataOnDisk-equivalent workloads in Velox.
/// It is not a write-through cache; bytes written here are never associated
/// with a remote source file.
///
/// Restriction: only FileSegmentKind::Ephemeral segments are supported.
/// Attempting to use it with Data or Temporary segments is a logic error.
class WriteBufferToFileSegment
{
public:
    static constexpr size_t kDefaultBufferSize = 1 << 16; // 64 KiB

    /// Constructor that takes ownership of a single-segment holder.
    /// Throws VeloxRuntimeError if holder contains more than one segment.
    explicit WriteBufferToFileSegment(
        FileSegmentsHolderPtr segmentHolder,
        size_t bufferSize = kDefaultBufferSize);

    /// Constructor that uses an externally managed segment.
    /// The caller must ensure the segment outlives this buffer.
    explicit WriteBufferToFileSegment(
        FileSegment * fileSegment,
        size_t bufferSize = kDefaultBufferSize);

    ~WriteBufferToFileSegment();

    WriteBufferToFileSegment(const WriteBufferToFileSegment &) = delete;
    WriteBufferToFileSegment & operator=(const WriteBufferToFileSegment &) = delete;

    /// Flush pending bytes to the segment and release the downloader.
    /// reserve() + write() sequence is executed once per call.
    void flush();

    /// flush() then sync the underlying local cache file.
    void sync();

    /// Finalize: flush any pending bytes then complete the segment.
    void finalize();

    /// Write raw bytes.  Buffers until the internal buffer is full, then
    /// calls flush().
    void write(const char * data, size_t size);

    /// Number of bytes successfully written to the segment so far.
    size_t writtenBytes() const
    {
        return writtenBytes_;
    }

    /// Path of the local cache segment file.
    std::string getPath() const;

    /// Returns a ReadBuffer backed by the written cache segment.
    /// Calls finalize() if not already finalized.
    std::unique_ptr<ReadBufferFromVeloxReadFile> getReadBuffer();

private:
    void flushBuffer(const char * data, size_t size);

    FileSegment * fileSegment_;
    FileSegmentsHolderPtr segmentHolder_; // non-null if owning

    std::vector<char> buffer_;
    size_t bufferUsed_ = 0;
    size_t writtenBytes_ = 0;
    bool finalized_ = false;

    const size_t reserveWaitTimeoutMs_ = 5'000;
};

} // namespace facebook::velox::ch
```

Register the new source files in `velox/ch/IO/CMakeLists.txt`.  If the
library target that owns the IO files is `velox_ch_filecache` (the
interface library created in earlier tasks), add the two new files as
sources:

```cmake
target_sources(
  velox_ch_filecache
  INTERFACE
    WriteBufferToFileSegment.h
  PRIVATE
    WriteBufferToFileSegment.cpp
)
```

If `velox_ch_filecache` is header-only (INTERFACE) and has a separate
`velox_ch_filecache_io` implementation target, add to that target instead.
If no per-directory sources target exists yet, create one:

```cmake
velox_add_library(
  velox_ch_io
  WriteBufferToFileSegment.cpp
)

target_include_directories(velox_ch_io PUBLIC ${VELOX_CH_INCLUDE_DIR})
target_link_libraries(
  velox_ch_io
  PUBLIC velox_ch_filecache velox_exception Folly::folly
)
```

and link `velox_ch_write_buffer_test` against `velox_ch_io` instead of only
`velox_ch_filecache`.

- [ ] **Step 5: Implement `WriteBufferToFileSegment.cpp`**

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * ...
 */

#include "velox/ch/IO/WriteBufferToFileSegment.h"
#include "velox/ch/Interpreters/FileCache/FileSegment.h"
#include "velox/common/base/Exceptions.h"
#include "velox/common/file/FileSystems.h"

namespace facebook::velox::ch
{

WriteBufferToFileSegment::WriteBufferToFileSegment(
    FileSegmentsHolderPtr segmentHolder,
    size_t bufferSize)
    : segmentHolder_(std::move(segmentHolder))
    , buffer_(bufferSize)
{
    VELOX_CHECK_NOT_NULL(segmentHolder_, "WriteBufferToFileSegment: null holder");
    VELOX_CHECK_EQ(
        segmentHolder_->size(),
        1,
        "WriteBufferToFileSegment requires a single-segment holder");
    fileSegment_ = &segmentHolder_->front();
    VELOX_CHECK_EQ(
        fileSegment_->getKind(),
        FileSegmentKind::Ephemeral,
        "WriteBufferToFileSegment: segment must be Ephemeral");
}

WriteBufferToFileSegment::WriteBufferToFileSegment(
    FileSegment * fileSegment,
    size_t bufferSize)
    : fileSegment_(fileSegment)
    , buffer_(bufferSize)
{
    VELOX_CHECK_NOT_NULL(fileSegment_, "WriteBufferToFileSegment: null segment");
    VELOX_CHECK_EQ(
        fileSegment_->getKind(),
        FileSegmentKind::Ephemeral,
        "WriteBufferToFileSegment: segment must be Ephemeral");
}

WriteBufferToFileSegment::~WriteBufferToFileSegment()
{
    // Do not throw from destructor.  If finalize() was not called, the segment
    // remains partially written; the holder destructor handles cleanup.
    try
    {
        if (!finalized_ && bufferUsed_ > 0)
            flushBuffer(buffer_.data(), bufferUsed_);
    }
    catch (...)
    {
    }
}

void WriteBufferToFileSegment::write(const char * data, size_t size)
{
    size_t written = 0;
    while (written < size)
    {
        const size_t space = buffer_.size() - bufferUsed_;
        const size_t toCopy = std::min(space, size - written);
        std::memcpy(buffer_.data() + bufferUsed_, data + written, toCopy);
        bufferUsed_ += toCopy;
        written += toCopy;
        if (bufferUsed_ == buffer_.size())
            flush();
    }
}

void WriteBufferToFileSegment::flush()
{
    if (bufferUsed_ == 0)
        return;
    flushBuffer(buffer_.data(), bufferUsed_);
    bufferUsed_ = 0;
}

void WriteBufferToFileSegment::flushBuffer(const char * data, size_t size)
{
    // Claim downloader for the duration of this flush.
    auto downloader = fileSegment_->getOrSetDownloader();
    VELOX_CHECK_EQ(
        downloader,
        FileSegment::getCallerId(),
        "WriteBufferToFileSegment: failed to acquire downloader; "
        "segment info: {}",
        fileSegment_->getInfoForLog());

    // RAII: always release downloader on exit.
    SCOPE_EXIT
    {
        if (fileSegment_->isDownloader())
            fileSegment_->completePartAndResetDownloader();
    };

    // Reserve space.
    FileCacheReserveStat stat;
    std::string reason;
    if (!fileSegment_->reserve(size, reserveWaitTimeoutMs_, reason, &stat))
    {
        VELOX_FAIL(
            "WriteBufferToFileSegment: failed to reserve {} bytes: {}; "
            "segment info: {}",
            size,
            reason,
            fileSegment_->getInfoForLog());
    }

    // Write.
    // FileSegment::write keeps the CH char* signature but does not mutate data.
    fileSegment_->write(const_cast<char *>(data), size, writtenBytes_);
    writtenBytes_ += size;
}

void WriteBufferToFileSegment::sync()
{
    flush();
    auto writer = fileSegment_->getLocalCacheWriter();
    if (writer)
        writer->sync();
}

void WriteBufferToFileSegment::finalize()
{
    if (finalized_)
        return;
    flush();
    auto writer = fileSegment_->getLocalCacheWriter();
    if (writer)
        writer->finalize();
    finalized_ = true;
}

std::string WriteBufferToFileSegment::getPath() const
{
    return fileSegment_->getPath();
}

std::unique_ptr<ReadBufferFromVeloxReadFile>
WriteBufferToFileSegment::getReadBuffer()
{
    finalize();
    if (fileSegment_->getDownloadedSize() == 0)
        return nullptr;

    auto readFile = filesystems::getFileSystem(getPath(), nullptr)
        ->openFileForRead(getPath(), {});
    return std::make_unique<ReadBufferFromVeloxReadFile>(
        std::move(readFile));
}

} // namespace facebook::velox::ch
```

- [ ] **Step 6: Implement the test cases**

Replace each `GTEST_SKIP()` in
`velox/ch/IO/tests/WriteBufferToFileSegmentTest.cpp` with real assertions.
Use a per-test fixture that creates a `FileCacheManager`, acquires an
Ephemeral segment via `FileCache::getOrSet` with
`FileSegmentKind::Ephemeral`, and tears it down afterwards.

Key assertions per test:

**`WriteAndReadBack`:**
1. Write 1 MiB of a recognizable byte pattern via `write()`.
2. `finalize()`.
3. `getReadBuffer()` returns a non-null reader.
4. Read all bytes; assert they match the original pattern.

**`ReserveFailureThrows`:**
1. Create an Ephemeral segment inside a tiny FileCache (e.g. 4 KiB max).
2. Attempt to write 8 KiB; assert `flush()` throws `VeloxRuntimeError`.

**`DownloaderReleasedOnFlush`:**
1. Flush once and verify `fileSegment->isDownloader()` is false after flush.
2. A second `write()`→`flush()` cycle must succeed (confirms downloader was
   released and is re-acquirable).

**`FinalizeFlushesAndCompletesSegment`:**
1. Write 64 bytes without calling `flush()` explicitly.
2. Call `finalize()`.
3. `writtenBytes()` must equal 64.
4. `fileSegment->getDownloadedSize()` must equal 64.

**`SyncFlushesBufferedBytes`:**
1. Write 32 bytes.
2. Call `sync()`.
3. `writtenBytes()` must equal 32.
4. The local cache file must exist and contain those 32 bytes.

**`HolderConstructorRequiresSingleSegment`:**
1. Create a two-segment holder.
2. Constructing `WriteBufferToFileSegment` from it must throw
   `VeloxRuntimeError`.

**`ExternalSegmentConstructor`:**
1. Create an external `FileSegment` (no holder ownership).
2. Write 16 bytes and finalize.
3. Confirm `writtenBytes()` is 16 and read-back succeeds.

**`FailureLeaveSegmentForHolderCleanup`:**
1. Wrap the holder in `WriteBufferToFileSegment`.
2. Force `flush()` to fail by making `reserve()` fail (tiny cache).
3. Let the `WriteBufferToFileSegment` destructor run without calling
   `finalize()`.
4. Assert no crash; the holder destructor must complete cleanly.

**`ReadBackAfterFinalizeReturnsWrittenBytes`:**
1. Write "hello, cache" (12 bytes), finalize, read back.
2. Assert the read-back content equals "hello, cache".

**`ReadBackAfterFinalizeReturnsEmptyWhenNoBytesWritten`:**
1. Finalize without writing anything.
2. `getReadBuffer()` must return `nullptr`.

- [ ] **Step 7: Build the test**

Reject any skipped/disabled case before the final build:

```bash
if rg -n 'GTEST_SKIP|DISABLED_' \
  <velox_repo>/velox/ch/IO/tests/WriteBufferToFileSegmentTest.cpp
then
  echo "ERROR: skipped WriteBufferToFileSegment test remains"
  exit 1
fi
```

Then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_write_buffer_test \
  > <velox_build_dir>/build_016_write_buffer.log 2>&1
echo "exit: $?"
```

Expected exit code: 0.

- [ ] **Step 8: Run the test**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_write_buffer_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_016_write_buffer.log 2>&1
echo "exit: $?"
```

Expected:

```text
100% tests passed, 0 tests failed.
```

- [ ] **Step 9: Inspect task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/IO/CMakeLists.txt \
  velox/ch/IO/WriteBufferToFileSegment.h \
  velox/ch/IO/WriteBufferToFileSegment.cpp \
  velox/ch/IO/tests/CMakeLists.txt \
  velox/ch/IO/tests/WriteBufferToFileSegmentTest.cpp \
  velox/ch/CMakeLists.txt
```

Expected:

```text
No whitespace errors.
No files outside the declared scope were changed.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 10: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/016-filecache-write-buffer-segment-result.md
```

Use exactly this structure:

````markdown
# Task 016 Result: `WriteBufferToFileSegment`

## Status

status: success

## Velox status

```text
<branch, HEAD, git status --short>
```

## Files changed

```text
<list only task-owned Velox files>
```

## Commands run

```text
<configure, build, test, verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_016.log
<velox_build_dir>/build_016_red.log
<velox_build_dir>/build_016_write_buffer.log
<velox_build_dir>/test_016_write_buffer.log
```

## Verification

```text
Red build failed with: <first error>
Final build exit code: 0
Test result: 100% passed
git diff --check: no whitespace errors
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 017 (optional post-MVP): observability and cancellation hardening.
```
````

## Explicit exclusions

Do not implement in this task:

```text
WriteBufferToFileSegment for Data or Temporary segment kinds — only
  Ephemeral is in scope; the other kinds require write-through cache or
  explicit download-state wiring that is deferred.

cache_on_write_operations / CachedOnDiskWriteBufferFromFile — explicitly
  deferred from the port (see port/01-filecache-port-order-design.md
  section "当前不做").

Integration with TemporaryDataOnDisk itself — this task only ports the
  write buffer; wiring it into a Velox analog of TemporaryDataOnDisk is a
  separate follow-up.

jumpToPosition — not supported; raises VeloxRuntimeError just as the CH
  source does.

IReadableWriteBuffer::cachingStopped / setFileFinishedForDistributedCache
  — CH-specific distributed-cache hooks; not present in this port.

IFilesystemCacheWriteBuffer interface — CH-specific interface for write
  cache tracking; not ported because there is no equivalent in the Velox
  read path. Write detection is handled by FileSegment::getKind().
```
