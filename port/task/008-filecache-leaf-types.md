# Task 008: `FileCache` Leaf Types — `SipHash128`, `FileCacheKey`, `FileSegmentKeyType`, `FileCacheOriginInfo`, Forward Headers, `FileCacheUtils`

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `<velox_repo>` and writes one
> result file under this ClickHouse checkout. Do not modify ClickHouse source
> files. Do not commit or stage either repository.

## Post-acceptance source-contract audit — task reopened

The SipHash128 vectors, persisted key formatting, enum values, aliases, and rounding
helpers remain accepted. Two corrective changes are required.

### Match CH malformed-key parsing

CH `FileCacheKey::fromKeyString` rejects only lengths other than 32 and delegates all
32-byte input to `unhexUInt`. Remove the added per-character exception. Replace
`FromKeyStringInvalidHexChar` with a compatibility test:

```text
input:    g0000000000000000000000000000000
behavior: does not throw
result:   f0000000000000000000000000000000
```

Keep wrong-length rejection and valid lower/upper-hex round trips.

### Shared checked arithmetic

Add overflow-checked unsigned addition to `FileCacheUtils.h` for Tasks 013 and 014:

```cpp
uint64_t checkedAdd(uint64_t lhs, uint64_t rhs, std::string_view operation);
```

It returns the exact sum or throws `VeloxRuntimeError` containing `operation`; it
must not saturate, wrap, or return a fallback. Add boundary tests for zero,
`UINT64_MAX + 0`, and `UINT64_MAX + 1`.

Task 013 and Task 014 must reuse this helper rather than define private variants.

## Goal

Port all leaf-level types that form the dependency base for every `FileCache`
algorithm file:

```text
SipHash 2-4 helper (CH variant, key0=key1=0, v2^=0xff)
FileCacheKey  (persistent 128-bit path hash, 32-char lower-hex string)
FileSegmentKeyType enum + prefix/toString helpers
FileCacheOriginInfo struct + OriginPoolKey + OriginPoolKeyHash
FileCache_fwd.h  (public policy enum + default constants + ownership aliases)
FileCache_fwd_internal.h  (internal ownership aliases + FileSegments container)
FileCacheUtils.h  (roundDownToMultiple / roundUpToMultiple)
```

The deliverable is an expanded `velox_ch_filecache` compiled library (the
conversion from INTERFACE was done in Task 004) with `SipHash128.cpp`,
`FileSegmentKeyType.cpp`, and `FileCacheKey.cpp` added, plus a focused
`velox_ch_leaf_types_test` test executable. The test must include at least one
golden SipHash128 vector derived from the CH implementation.

Note: `Guards.h` was ported in Task 004 and must already exist. This task does
not recreate it.

## Controller amendment before Worker attempt 1

### Mono-safe `FileCache` source registration

This amendment overrides the literal `target_sources(PRIVATE ...)` call in
Step 14. In the default mono build, `velox_ch_filecache` is an alias and cannot
be passed to `target_sources`.

Register `FileSegmentKeyType.cpp` and `FileCacheKey.cpp` through the existing
`velox_sources` helper so both mono and non-mono builds receive the compiled
sources. In non-mono builds only, extend the existing public `HEADERS` file set
with all Task 008 public headers under `velox/ch/Interpreters/FileCache/`:

```text
FileSegmentKeyType.h
FileCacheOriginInfo.h
FileCache_fwd.h
FileCache_fwd_internal.h
FileCacheKey.h
FileCacheUtils.h
```

Keep the existing `Guards.h` registration and `add_subdirectory(tests)` block.
Do not call `target_sources` on the alias in mono mode.

## Controller amendment after Worker attempt 1

### `FileCacheKey` default construction

This amendment overrides the factory-only `FileCacheKey` declaration in
Step 10. Add a public default constructor:

```cpp
FileCacheKey() = default;
```

The default key must have numeric value zero through the existing `key{}`
initializer. This preserves the ClickHouse API and is required by declared
downstream structures in Task 012 (`FileSegmentInfo`, `DownloadInfo`) and the
Task 015 benchmark state, which default-construct `FileCacheKey` members.

Add focused compile/runtime coverage proving:

```text
std::is_default_constructible_v<FileCacheKey>
FileCacheKey{}.key == 0
FileCacheKey{}.toString() == "00000000000000000000000000000000"
```

### Direct non-mono link dependencies

Step 13 requires `velox_ch_filecache` to carry the direct dependencies used by
`FileCacheKey.cpp` and its public headers. Extend the existing non-mono
`target_link_libraries` block in `velox/ch/Common/CMakeLists.txt` to include:

```text
velox_exception
Folly::folly
fmt::fmt
```

Do not rely on each consumer or focused test to link `fmt` and
`velox_exception` separately.

### Hex parser rejection evidence

Add a focused test that passes a 32-character string containing a non-hex
character to `fromKeyString` and proves it throws. The implementation already
contains this validation, but attempt 1 did not exercise it even though the
review receipt claimed the branch was verified.

## Controller amendment after Worker attempt 2

### Preserve public dependency scope

This clarifies the "Direct non-mono link dependencies" amendment above. In the
non-mono `target_link_libraries` block, the dependency scope must be `PUBLIC`,
as required by the original Step 13:

```cmake
if(NOT VELOX_MONO_LIBRARY)
  target_link_libraries(
    velox_ch_filecache
    PUBLIC
      velox_exception
      Folly::folly
      fmt::fmt
  )
endif()
```

`FileCacheKey.h` publicly includes fmt and Folly headers. `PRIVATE` dependencies
of a static library can reach consumers as link-only dependencies, but do not
carry their compile usage requirements. Therefore `PRIVATE` does not satisfy
the public-header contract.

### Remove the focused-test dependency mask

The Task 008 focused test must not list `velox_exception`, `Folly::folly`, or
`fmt::fmt` directly. Link it only to:

```cmake
velox_ch_filecache
GTest::gtest
GTest::gtest_main
```

This makes the test a real consumer of the `velox_ch_filecache` interface rather
than masking missing transitive dependencies.

In addition to the normal mono gates, configure a separate non-mono build with
`-DVELOX_MONO_LIBRARY=OFF` and build and run `velox_ch_leaf_types_test`. Keep its
configure/build/test logs in that separate build directory. This is the focused
proof for the CMake mode changed by this amendment.

## Starting point

```text
Velox repository: <velox_repo>
Required branch:  filecache
Expected HEAD:    Task 007 result commit or any direct descendant
```

The following files must already exist:

```text
velox/ch/Common/ClickHouseAliases.h             (Task 003)
velox/ch/Common/FileCacheException.h            (Task 003)
velox/ch/Common/SharedMutex.h                   (Task 003)
velox/ch/Common/logger_useful.h                 (Task 003)
velox/ch/Common/ProfileEvents.h                 (Task 003)
velox/ch/Common/CurrentMetrics.h                (Task 003)
velox/ch/Common/FailPoint.h                     (Task 003)
velox/ch/Common/StatusFile.h                    (Task 004)
velox/ch/Interpreters/FileCache/Guards.h        (Task 004)
velox/ch/Interpreters/FileCache/CMakeLists.txt  (Task 004)
velox/ch/Interpreters/FileCache/tests/CMakeLists.txt  (Task 004)
```

Stop if the branch is not `filecache` or if any of these files is absent.

## Design references

Read before editing:

```text
<clickhouse_repo>/port/task/ENVIRONMENT.md
<clickhouse_repo>/port/2-file-cache/01-filecache-fwd-files-design.md
<clickhouse_repo>/port/2-file-cache/02-filecache-origin-segment-type-design.md
<clickhouse_repo>/port/2-file-cache/03-filecache-key-hash-design.md
<clickhouse_repo>/port/2-file-cache/04-filecache-utils-design.md
```

Use the ClickHouse implementations only as behavioral references:

```text
<clickhouse_repo>/src/Common/SipHash.h
<clickhouse_repo>/src/Interpreters/FileCache/FileCacheKey.h
<clickhouse_repo>/src/Interpreters/FileCache/FileCacheKey.cpp
<clickhouse_repo>/src/Interpreters/FileCache/FileSegmentKeyType.h
<clickhouse_repo>/src/Interpreters/FileCache/FileSegmentKeyType.cpp
<clickhouse_repo>/src/Interpreters/FileCache/FileCacheOriginInfo.h
<clickhouse_repo>/src/Interpreters/FileCache/FileCache_fwd.h
<clickhouse_repo>/src/Interpreters/FileCache/FileCache_fwd_internal.h
<clickhouse_repo>/src/Interpreters/FileCache/FileCacheUtils.h
```

## File scope

Modify:

```text
<velox_repo>/velox/ch/Common/CMakeLists.txt
<velox_repo>/velox/ch/Interpreters/FileCache/CMakeLists.txt
<velox_repo>/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
```

Create:

```text
<velox_repo>/velox/ch/Common/SipHash128.h
<velox_repo>/velox/ch/Common/SipHash128.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/FileSegmentKeyType.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileSegmentKeyType.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCache_fwd.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCache_fwd_internal.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheKey.h
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheKey.cpp
<velox_repo>/velox/ch/Interpreters/FileCache/FileCacheUtils.h
<velox_repo>/velox/ch/Interpreters/FileCache/tests/LeafTypesTest.cpp
<clickhouse_repo>/port/task/result/008-filecache-leaf-types-result.md
```

Every new Velox C++ and CMake file must begin with the Apache 2.0 license header
using the repository's existing comment form (`/* ... */` for C++, `#` for CMake).

## Steps

> **Environment setup:** Before running any configure, build, or test command in this task,
> follow the selected profile's environment setup from `ENVIRONMENT.md`. For `root-oss`, source
> `<velox_env>` first.

- [ ] **Step 1: Confirm the Velox baseline**

Run:

```bash
cd <velox_repo>
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected:

```text
Branch is filecache.
HEAD is Task 007 result or a direct descendant.
Record all pre-existing dirty files in the result file.
```

- [ ] **Step 2: Record the verified SipHash128 golden vectors**

The CH `sipHash128` function uses SipHash 2-4 with `key0=0, key1=0` and the CH
variant (`v2 ^= 0xff`). `FileCacheKey::fromPath` serialises high 64 bits first,
then low 64 bits. The vectors were generated from that implementation with
`.claude/tools/cppexpr.sh`:

```text
""                                 -> f711edcba8b6b5e5e983a656dbc1b532
"abc"                              -> 53a3124ce5655a686c6b96daa215b4b6
"s3://bucket/key"                  -> 6ba3177b6fbaa4c9f65873033e35aeaa
"0123456789abcdef0123456789abcdef" -> 77dd7dd78fa45ef0b93cc3b8df847cbd
```

Use these literals unchanged in Step 3.

- [ ] **Step 3: Add a failing focused test**

Append the following target to the existing
`velox/ch/Interpreters/FileCache/tests/CMakeLists.txt`:

```cmake
add_executable(velox_ch_leaf_types_test LeafTypesTest.cpp)
add_test(velox_ch_leaf_types_test velox_ch_leaf_types_test)

target_link_libraries(
  velox_ch_leaf_types_test
  PRIVATE
    velox_ch_filecache
    velox_exception
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

Create `velox/ch/Interpreters/FileCache/tests/LeafTypesTest.cpp` with the
complete vectors from Step 2:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */

#include "velox/ch/Common/SipHash128.h"
#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Interpreters/FileCache/FileSegmentKeyType.h"
#include "velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h"
#include "velox/ch/Interpreters/FileCache/FileCache_fwd.h"
#include "velox/ch/Interpreters/FileCache/FileCache_fwd_internal.h"
#include "velox/ch/Interpreters/FileCache/FileCacheUtils.h"
#include "velox/common/base/Exceptions.h"

#include <gtest/gtest.h>

#include <climits>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace facebook::velox::ch
{
namespace
{

// ── SipHash128 ────────────────────────────────────────────────────────────────

TEST(SipHash128Test, IncrementalEqualsOneShot)
{
    const char * data = "hello world";
    const size_t n = strlen(data);
    auto one = sipHash128(data, n);
    SipHash128 inc;
    inc.update(data, 5);
    inc.update(data + 5, n - 5);
    EXPECT_EQ(one, inc.get128());
}

TEST(FileCacheKeyTest, GoldenEmpty)
{
    EXPECT_EQ(
        FileCacheKey::fromPath("").toString(),
        "f711edcba8b6b5e5e983a656dbc1b532");
}

TEST(FileCacheKeyTest, GoldenAbc)
{
    EXPECT_EQ(
        FileCacheKey::fromPath("abc").toString(),
        "53a3124ce5655a686c6b96daa215b4b6");
}

TEST(FileCacheKeyTest, GoldenS3Path)
{
    EXPECT_EQ(
        FileCacheKey::fromPath("s3://bucket/key").toString(),
        "6ba3177b6fbaa4c9f65873033e35aeaa");
}

TEST(FileCacheKeyTest, GoldenLong)
{
    EXPECT_EQ(
        FileCacheKey::fromPath("0123456789abcdef0123456789abcdef").toString(),
        "77dd7dd78fa45ef0b93cc3b8df847cbd");
}

TEST(FileCacheKeyTest, RoundTrip)
{
    const auto key = FileCacheKey::fromPath("some/path/to/data.parquet");
    EXPECT_EQ(FileCacheKey::fromKeyString(key.toString()), key);
}

TEST(FileCacheKeyTest, ToStringLength)
{
    EXPECT_EQ(FileCacheKey::fromPath("x").toString().size(), 32u);
}

TEST(FileCacheKeyTest, FromKeyStringBadLength)
{
    EXPECT_THROW(FileCacheKey::fromKeyString("abc"), VeloxRuntimeError);
    EXPECT_THROW(
        FileCacheKey::fromKeyString(std::string(31, '0')), VeloxRuntimeError);
    EXPECT_THROW(
        FileCacheKey::fromKeyString(std::string(33, '0')), VeloxRuntimeError);
}

TEST(FileCacheKeyTest, OrderHighFirst)
{
    auto a = FileCacheKey::fromKey(
        (static_cast<uint128_t>(1) << 64) | uint64_t{0xFFFFFFFFFFFFFFFFULL});
    auto b = FileCacheKey::fromKey(
        (static_cast<uint128_t>(2) << 64) | uint64_t{0});
    EXPECT_LT(a, b);
}

TEST(FileCacheKeyTest, UsableInF14FastMap)
{
    folly::F14FastMap<FileCacheKey, int, FileCacheKeyHash> map;
    auto k1 = FileCacheKey::fromPath("p1");
    auto k2 = FileCacheKey::fromPath("p2");
    map[k1] = 1;
    map[k2] = 2;
    EXPECT_EQ(map[k1], 1);
    EXPECT_EQ(map[k2], 2);
}

// ── FileSegmentKeyType ────────────────────────────────────────────────────────

TEST(FileSegmentKeyTypeTest, PrefixGeneral)
{
    EXPECT_EQ(getKeyTypePrefix(FileSegmentKeyType::General), "");
}

TEST(FileSegmentKeyTypeTest, PrefixSystem)
{
    EXPECT_EQ(getKeyTypePrefix(FileSegmentKeyType::System), "System");
}

TEST(FileSegmentKeyTypeTest, PrefixData)
{
    EXPECT_EQ(getKeyTypePrefix(FileSegmentKeyType::Data), "Data");
}

TEST(FileSegmentKeyTypeTest, ToStringAll)
{
    EXPECT_EQ(toString(FileSegmentKeyType::General), "General");
    EXPECT_EQ(toString(FileSegmentKeyType::System), "System");
    EXPECT_EQ(toString(FileSegmentKeyType::Data), "Data");
}

TEST(FileSegmentKeyTypeTest, UnderlyingValues)
{
    static_assert(static_cast<uint8_t>(FileSegmentKeyType::General) == 0);
    static_assert(static_cast<uint8_t>(FileSegmentKeyType::System) == 1);
    static_assert(static_cast<uint8_t>(FileSegmentKeyType::Data) == 2);
}

// ── FileCacheOriginInfo ───────────────────────────────────────────────────────

TEST(FileCacheOriginInfoTest, EqualityUserIdOnly)
{
    FileCacheOriginInfo a{"user1", uint64_t{10}, FileSegmentKeyType::Data};
    FileCacheOriginInfo b{"user1", uint64_t{20}, FileSegmentKeyType::System};
    EXPECT_EQ(a, b);
}

TEST(FileCacheOriginInfoTest, InequalityDifferentUser)
{
    EXPECT_NE(FileCacheOriginInfo{"user1"}, FileCacheOriginInfo{"user2"});
}

TEST(FileCacheOriginInfoTest, OriginPoolKeyFullEquality)
{
    OriginPoolKey x{"user1", uint64_t{10}, FileSegmentKeyType::Data};
    OriginPoolKey y{"user1", uint64_t{10}, FileSegmentKeyType::Data};
    OriginPoolKey z{"user1", uint64_t{10}, FileSegmentKeyType::System};
    EXPECT_EQ(x, y);
    EXPECT_NE(x, z);
}

TEST(FileCacheOriginInfoTest, OriginPoolKeyHashSameUserSameBucket)
{
    OriginPoolKeyHash h;
    OriginPoolKey k1{"u", uint64_t{1}, FileSegmentKeyType::Data};
    OriginPoolKey k2{"u", uint64_t{2}, FileSegmentKeyType::System};
    EXPECT_EQ(h(k1), h(k2));
}

TEST(FileCacheOriginInfoTest, UsableInF14FastMap)
{
    folly::F14FastMap<OriginPoolKey, int, OriginPoolKeyHash> map;
    OriginPoolKey k{"u", uint64_t{1}, FileSegmentKeyType::General};
    map[k] = 42;
    EXPECT_EQ(map[k], 42);
}

// ── FileCache_fwd.h ───────────────────────────────────────────────────────────

TEST(FileCacheFwdTest, PolicyEnumValues)
{
    static_assert(static_cast<uint8_t>(FileCachePolicy::LRU) == 0);
    static_assert(static_cast<uint8_t>(FileCachePolicy::SLRU) == 1);
    static_assert(static_cast<uint8_t>(FileCachePolicy::SLRU_OVERCOMMIT) == 2);
    static_assert(static_cast<uint8_t>(FileCachePolicy::LRU_OVERCOMMIT) == 3);
}

TEST(FileCacheFwdTest, DefaultConstants)
{
    EXPECT_EQ(FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE, 32ULL * 1024 * 1024);
    EXPECT_EQ(FILECACHE_DEFAULT_FILE_SEGMENT_ALIGNMENT, 4ULL * 1024 * 1024);
    EXPECT_EQ(FILECACHE_DEFAULT_RESERVE_GRANULARITY, 4ULL * 1024 * 1024);
    EXPECT_EQ(FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_THREADS, 5u);
    EXPECT_EQ(FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_QUEUE_SIZE_LIMIT, 5000u);
    EXPECT_EQ(FILECACHE_DEFAULT_LOAD_METADATA_THREADS, 16u);
    EXPECT_EQ(FILECACHE_DEFAULT_MAX_ELEMENTS, 10'000'000u);
    EXPECT_EQ(FILECACHE_BYPASS_THRESHOLD, 256ULL * 1024 * 1024);
    EXPECT_DOUBLE_EQ(FILECACHE_DEFAULT_FREE_SPACE_SIZE_RATIO, 0.0);
    EXPECT_DOUBLE_EQ(FILECACHE_DEFAULT_FREE_SPACE_ELEMENTS_RATIO, 0.0);
    EXPECT_EQ(FILECACHE_DEFAULT_FREE_SPACE_REMOVE_BATCH, 250u);
    EXPECT_EQ(FILECACHE_DEFAULT_FREE_SPACE_EVICTION_THREADS, 1u);
    EXPECT_EQ(FILECACHE_DEFAULT_CACHE_POLICY, FileCachePolicy::SLRU);
    EXPECT_DOUBLE_EQ(FILECACHE_DEFAULT_SLRU_RATIO, 0.6);
}

TEST(FileCacheFwdTest, FileCachePtrIsShared)
{
    static_assert(std::is_same_v<FileCachePtr, std::shared_ptr<FileCache>>);
}

TEST(FileCacheFwdTest, FileCacheSettingsAliasesConfig)
{
    static_assert(std::is_same_v<FileCacheSettings, FileCacheConfig>);
}

// ── FileCache_fwd_internal.h ──────────────────────────────────────────────────

TEST(FileCacheFwdInternalTest, FileSegmentsIsList)
{
    static_assert(
        std::is_same_v<FileSegments, std::list<std::shared_ptr<FileSegment>>>);
}

TEST(FileCacheFwdInternalTest, KeyMetadataWeakPtrIsWeak)
{
    static_assert(
        std::is_same_v<KeyMetadataWeakPtr, std::weak_ptr<KeyMetadata>>);
}

// ── FileCacheUtils ────────────────────────────────────────────────────────────

TEST(FileCacheUtilsTest, RoundDownZeroMultiple)
{
    EXPECT_EQ(FileCacheUtils::roundDownToMultiple(0, 0), 0u);
    EXPECT_EQ(FileCacheUtils::roundDownToMultiple(123, 0), 123u);
}

TEST(FileCacheUtilsTest, RoundDownBoundaries)
{
    EXPECT_EQ(FileCacheUtils::roundDownToMultiple(7, 8), 0u);
    EXPECT_EQ(FileCacheUtils::roundDownToMultiple(8, 8), 8u);
    EXPECT_EQ(FileCacheUtils::roundDownToMultiple(9, 8), 8u);
}

TEST(FileCacheUtilsTest, RoundUpZeroMultiple)
{
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(0, 0), 0u);
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(123, 0), 123u);
}

TEST(FileCacheUtilsTest, RoundUpBoundaries)
{
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(0, 8), 0u);
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(1, 8), 8u);
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(8, 8), 8u);
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(9, 8), 16u);
}

TEST(FileCacheUtilsTest, RoundUpRepresentableBoundary)
{
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(2, SIZE_MAX), SIZE_MAX);
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(SIZE_MAX, SIZE_MAX), SIZE_MAX);
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(SIZE_MAX - 1, SIZE_MAX), SIZE_MAX);
    EXPECT_EQ(FileCacheUtils::roundUpToMultiple(SIZE_MAX, 1), SIZE_MAX);
}

TEST(FileCacheUtilsTest, RoundUpActualOverflow)
{
    EXPECT_THROW(
        FileCacheUtils::roundUpToMultiple(SIZE_MAX, SIZE_MAX - 1),
        std::overflow_error);
    EXPECT_THROW(
        FileCacheUtils::roundUpToMultiple(SIZE_MAX - 1, 4),
        std::overflow_error);
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 4: Verify the test fails before implementation**

Reconfigure:

Follow the selected profile's environment setup from `ENVIRONMENT.md` (for
`root-oss`, source `<velox_env>` first), then run the selected profile's
configure recipe from `ENVIRONMENT.md`. For `home-chang`, also add
`-DVELOX_BUILD_TESTING=ON` (already present in the `root-oss` effective
configuration). Redirect output to `<velox_build_dir>/configure_task_008_leaf_types.log`.

Then try to build:

```bash
if <ninja> \
  -C <velox_build_dir> \
  velox_ch_leaf_types_test \
  > <velox_build_dir>/build_task_008_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected:

```text
Configure may succeed.
Build fails because header files do not exist yet.
```

- [ ] **Step 5: Create `SipHash128.h` and `SipHash128.cpp`**

Create `velox/ch/Common/SipHash128.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include <cstdint>

namespace facebook::velox::ch
{

/// Minimal SipHash 2-4 helper — CH variant (key0=key1=0, v2^=0xff).
///
/// Result layout matches ClickHouse sipHash128:
///   lo = v0 ^ v1
///   hi = v2 ^ v3
///   uint128_t = (hi << 64) | lo
///
/// Do NOT replace with the reference SipHash-2-4 (v2^=0xee); the CH
/// variant is used in FileCacheKey::fromPath for persistent cache paths.
using uint128_t = __uint128_t;

class SipHash128
{
public:
    SipHash128();

    void update(const char * data, uint64_t size);

    /// Finalises and returns the 128-bit digest.
    /// ATTENTION: call at most once per instance.
    uint128_t get128();

private:
    uint64_t v0_, v1_, v2_, v3_;
    uint64_t cnt_;
    union
    {
        uint64_t current_word_;
        uint8_t current_bytes_[8];
    };
};

/// One-shot helper.
uint128_t sipHash128(const char * data, size_t size);

} // namespace facebook::velox::ch
```

Create `velox/ch/Common/SipHash128.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */

#include "velox/ch/Common/SipHash128.h"
#include <cstring>

namespace facebook::velox::ch
{

namespace
{
inline uint64_t rotl64(uint64_t x, int r) noexcept
{
    return (x << r) | (x >> (64 - r));
}
} // namespace

#define SIPROUND                           \
    do                                     \
    {                                      \
        v0 += v1;                          \
        v1 = rotl64(v1, 13);               \
        v1 ^= v0;                          \
        v0 = rotl64(v0, 32);               \
        v2 += v3;                          \
        v3 = rotl64(v3, 16);               \
        v3 ^= v2;                          \
        v0 += v3;                          \
        v3 = rotl64(v3, 21);               \
        v3 ^= v0;                          \
        v2 += v1;                          \
        v1 = rotl64(v1, 17);               \
        v1 ^= v2;                          \
        v2 = rotl64(v2, 32);               \
    } while (false)

SipHash128::SipHash128()
    : v0_(0x736f6d6570736575ULL)
    , v1_(0x646f72616e646f6dULL)
    , v2_(0x6c7967656e657261ULL)
    , v3_(0x7465646279746573ULL)
    , cnt_(0)
    , current_word_(0)
{
}

void SipHash128::update(const char * data, uint64_t size)
{
    const char * end = data + size;

    if (cnt_ & 7)
    {
        while ((cnt_ & 7) && data < end)
        {
            current_bytes_[cnt_ & 7] = static_cast<uint8_t>(*data++);
            ++cnt_;
        }
        if (cnt_ & 7)
            return;

        uint64_t v0 = v0_, v1 = v1_, v2 = v2_, v3 = v3_;
        v3 ^= current_word_;
        SIPROUND; SIPROUND;
        v0 ^= current_word_;
        v0_ = v0; v1_ = v1; v2_ = v2; v3_ = v3;
    }

    cnt_ += static_cast<uint64_t>(end - data);

    while (end - data >= 8)
    {
        uint64_t word;
        std::memcpy(&word, data, 8);
        uint64_t v0 = v0_, v1 = v1_, v2 = v2_, v3 = v3_;
        v3 ^= word;
        SIPROUND; SIPROUND;
        v0 ^= word;
        v0_ = v0; v1_ = v1; v2_ = v2; v3_ = v3;
        data += 8;
    }

    current_word_ = 0;
    switch (end - data)
    {
        case 7: current_bytes_[6] = static_cast<uint8_t>(data[6]); [[fallthrough]];
        case 6: current_bytes_[5] = static_cast<uint8_t>(data[5]); [[fallthrough]];
        case 5: current_bytes_[4] = static_cast<uint8_t>(data[4]); [[fallthrough]];
        case 4: current_bytes_[3] = static_cast<uint8_t>(data[3]); [[fallthrough]];
        case 3: current_bytes_[2] = static_cast<uint8_t>(data[2]); [[fallthrough]];
        case 2: current_bytes_[1] = static_cast<uint8_t>(data[1]); [[fallthrough]];
        case 1: current_bytes_[0] = static_cast<uint8_t>(data[0]); [[fallthrough]];
        case 0: break;
    }
}

uint128_t SipHash128::get128()
{
    current_bytes_[7] = static_cast<uint8_t>(cnt_);

    uint64_t v0 = v0_, v1 = v1_, v2 = v2_, v3 = v3_;
    v3 ^= current_word_;
    SIPROUND; SIPROUND;
    v0 ^= current_word_;

    v2 ^= 0xff; // CH variant (reference uses 0xee)
    SIPROUND; SIPROUND; SIPROUND; SIPROUND;

    const uint64_t lo = v0 ^ v1;
    const uint64_t hi = v2 ^ v3;
    return (static_cast<uint128_t>(hi) << 64) | lo;
}

#undef SIPROUND

uint128_t sipHash128(const char * data, size_t size)
{
    SipHash128 h;
    h.update(data, size);
    return h.get128();
}

} // namespace facebook::velox::ch
```

- [ ] **Step 6: Create `FileCache_fwd.h`**

Create `velox/ch/Interpreters/FileCache/FileCache_fwd.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include <cstdint>
#include <memory>

namespace facebook::velox::ch
{

enum class FileCachePolicy : uint8_t { LRU, SLRU, SLRU_OVERCOMMIT, LRU_OVERCOMMIT };

inline constexpr uint64_t FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE =
    32ULL * 1024 * 1024;
inline constexpr uint64_t FILECACHE_DEFAULT_FILE_SEGMENT_ALIGNMENT =
    4ULL * 1024 * 1024;
inline constexpr uint64_t FILECACHE_DEFAULT_RESERVE_GRANULARITY =
    4ULL * 1024 * 1024;
// Typo preserved from CH identifier to minimise algorithm-file diff.
inline constexpr uint64_t FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE_WITH_BACKGROUND_DOWLOAD =
    4ULL * 1024 * 1024;
inline constexpr uint64_t FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_THREADS = 5;
inline constexpr uint64_t FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_QUEUE_SIZE_LIMIT = 5000;
inline constexpr uint64_t FILECACHE_DEFAULT_LOAD_METADATA_THREADS = 16;
inline constexpr uint64_t FILECACHE_DEFAULT_MAX_ELEMENTS = 10'000'000;
inline constexpr uint64_t FILECACHE_BYPASS_THRESHOLD = 256ULL * 1024 * 1024;
inline constexpr double FILECACHE_DEFAULT_FREE_SPACE_SIZE_RATIO = 0.0;
inline constexpr double FILECACHE_DEFAULT_FREE_SPACE_ELEMENTS_RATIO = 0.0;
inline constexpr uint64_t FILECACHE_DEFAULT_FREE_SPACE_REMOVE_BATCH = 250;
inline constexpr uint64_t FILECACHE_DEFAULT_FREE_SPACE_EVICTION_THREADS = 1;
inline constexpr FileCachePolicy FILECACHE_DEFAULT_CACHE_POLICY =
    FileCachePolicy::SLRU;
inline constexpr double FILECACHE_DEFAULT_SLRU_RATIO = 0.6;

class FileCache;
using FileCachePtr = std::shared_ptr<FileCache>;

struct FileCacheConfig;
/// Algorithm files use the CH name `FileCacheSettings`.
using FileCacheSettings = FileCacheConfig;

struct FileCacheKey;

} // namespace facebook::velox::ch
```

- [ ] **Step 7: Create `FileCache_fwd_internal.h`**

Create `velox/ch/Interpreters/FileCache/FileCache_fwd_internal.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include <list>
#include <memory>

namespace facebook::velox::ch
{

class FileCache;
using FileCachePtr = std::shared_ptr<FileCache>;

class IFileCachePriority;
using FileCachePriorityPtr = std::shared_ptr<IFileCachePriority>;
using IFileCachePriorityPtr = std::unique_ptr<IFileCachePriority>;

class FileSegment;
using FileSegmentPtr = std::shared_ptr<FileSegment>;
/// Must remain std::list — FileCache splices and iterates simultaneously.
using FileSegments = std::list<FileSegmentPtr>;

struct FileSegmentMetadata;
using FileSegmentMetadataPtr = std::shared_ptr<FileSegmentMetadata>;

struct KeyMetadata;
using KeyMetadataPtr = std::shared_ptr<KeyMetadata>;
/// Weak to break the KeyMetadata ↔ FileSegment ↔ KeyMetadata cycle.
using KeyMetadataWeakPtr = std::weak_ptr<KeyMetadata>;

struct LockedKey;
using LockedKeyPtr = std::shared_ptr<LockedKey>;

} // namespace facebook::velox::ch
```

- [ ] **Step 8: Create `FileSegmentKeyType.h` and `FileSegmentKeyType.cpp`**

Create `velox/ch/Interpreters/FileCache/FileSegmentKeyType.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include <cstdint>
#include <string>

namespace facebook::velox::ch
{

enum class FileSegmentKeyType : uint8_t
{
    General = 0,
    System,
    Data,
};

/// Returns the directory prefix for a key type.
/// General returns "" — this is a cache-path invariant; do not change.
std::string getKeyTypePrefix(FileSegmentKeyType type);

/// Returns the enum name string ("General", "System", "Data").
std::string toString(FileSegmentKeyType type);

} // namespace facebook::velox::ch
```

Create `velox/ch/Interpreters/FileCache/FileSegmentKeyType.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */

#include "velox/ch/Interpreters/FileCache/FileSegmentKeyType.h"
#include "velox/common/base/Exceptions.h"

namespace facebook::velox::ch
{

namespace
{

std::string_view toStringView(FileSegmentKeyType type)
{
    switch (type)
    {
        case FileSegmentKeyType::General: return "General";
        case FileSegmentKeyType::System:  return "System";
        case FileSegmentKeyType::Data:    return "Data";
    }
    VELOX_FAIL("Unknown FileSegmentKeyType: {}", static_cast<uint8_t>(type));
}

} // namespace

std::string getKeyTypePrefix(FileSegmentKeyType type)
{
    if (type == FileSegmentKeyType::General)
        return "";
    return std::string(toStringView(type));
}

std::string toString(FileSegmentKeyType type)
{
    return std::string(toStringView(type));
}

} // namespace facebook::velox::ch
```

- [ ] **Step 9: Create `FileCacheOriginInfo.h`**

Create `velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include "velox/ch/Interpreters/FileCache/FileSegmentKeyType.h"

#include <folly/container/F14Map.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace facebook::velox::ch
{

struct FileCacheOriginInfo
{
    using UserID = std::string;
    using Weight = uint64_t;
    using SegmentKeyType = FileSegmentKeyType;

    UserID user_id;
    std::optional<Weight> weight = std::nullopt;
    SegmentKeyType segment_type = SegmentKeyType::General;

    FileCacheOriginInfo() = default;

    explicit FileCacheOriginInfo(const UserID & user_id_)
        : user_id(user_id_)
    {
    }

    FileCacheOriginInfo(
        const UserID & user_id_,
        const Weight & weight_,
        SegmentKeyType segment_type_ = SegmentKeyType::General)
        : user_id(user_id_), weight(weight_), segment_type(segment_type_)
    {
    }

    /// Equality compares only user_id (matches ClickHouse semantics).
    bool operator==(const FileCacheOriginInfo & other) const
    {
        return user_id == other.user_id;
    }
};

using OriginInfoPtr = std::shared_ptr<const FileCacheOriginInfo>;

struct OriginPoolKey
{
    FileCacheOriginInfo::UserID user_id;
    std::optional<FileCacheOriginInfo::Weight> weight;
    FileCacheOriginInfo::SegmentKeyType segment_type;

    bool operator==(const OriginPoolKey & other) const = default;
};

/// Hash on user_id only — mirrors ClickHouse behaviour (user count is small
/// so collision on weight/type is acceptable).
struct OriginPoolKeyHash
{
    size_t operator()(const OriginPoolKey & key) const noexcept
    {
        return std::hash<FileCacheOriginInfo::UserID>{}(key.user_id);
    }
};

} // namespace facebook::velox::ch
```

- [ ] **Step 10: Create `FileCacheKey.h` and `FileCacheKey.cpp`**

Create `velox/ch/Interpreters/FileCache/FileCacheKey.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include "velox/ch/Common/SipHash128.h"

#include <folly/container/F14Map.h>
#include <velox/common/base/BitUtil.h>
#include <fmt/format.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace facebook::velox::ch
{

struct FileCacheKey
{
    using KeyHash = uint128_t;

    KeyHash key{};

    std::string toString() const;

    static FileCacheKey random();
    static FileCacheKey fromPath(std::string_view path);
    static FileCacheKey fromKey(KeyHash k);
    static FileCacheKey fromKeyString(std::string_view key_str);

    bool operator==(const FileCacheKey & other) const = default;
    bool operator<(const FileCacheKey & other) const { return key < other.key; }

private:
    explicit FileCacheKey(KeyHash k) : key(k) {}
};

using FileCacheKeyAndOffset = std::pair<FileCacheKey, size_t>;

struct FileCacheKeyHash
{
    size_t operator()(const FileCacheKey & k) const noexcept
    {
        return bits::hashMix(
            static_cast<uint64_t>(k.key >> 64),
            static_cast<uint64_t>(k.key));
    }
};

struct FileCacheKeyAndOffsetHash
{
    size_t operator()(const FileCacheKeyAndOffset & v) const noexcept
    {
        return bits::hashMix(
            FileCacheKeyHash{}(v.first), v.second);
    }
};

} // namespace facebook::velox::ch

template <>
struct fmt::formatter<facebook::velox::ch::FileCacheKey>
    : fmt::formatter<std::string>
{
    template <typename FormatCtx>
    auto format(
        const facebook::velox::ch::FileCacheKey & key,
        FormatCtx & ctx) const
    {
        return fmt::formatter<std::string>::format(key.toString(), ctx);
    }
};
```

Create `velox/ch/Interpreters/FileCache/FileCacheKey.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */

#include "velox/ch/Interpreters/FileCache/FileCacheKey.h"
#include "velox/ch/Common/FileCacheException.h"

#include <folly/Random.h>
#include <fmt/format.h>

namespace facebook::velox::ch
{

std::string FileCacheKey::toString() const
{
    return fmt::format(
        "{:016x}{:016x}",
        static_cast<uint64_t>(key >> 64),
        static_cast<uint64_t>(key));
}

FileCacheKey FileCacheKey::random()
{
    return FileCacheKey(
        (static_cast<uint128_t>(folly::Random::rand64()) << 64)
        | folly::Random::rand64());
}

FileCacheKey FileCacheKey::fromPath(std::string_view path)
{
    return FileCacheKey(sipHash128(path.data(), path.size()));
}

FileCacheKey FileCacheKey::fromKey(KeyHash k)
{
    return FileCacheKey(k);
}

FileCacheKey FileCacheKey::fromKeyString(std::string_view key_str)
{
    if (key_str.size() != 32)
        throwFileCacheException(
            "Invalid cache key hex string: expected 32 characters, got {}",
            key_str.size());

    auto hexDigit = [&](char c) -> uint8_t
    {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        throwFileCacheException(
            "Invalid hex character '{}' in cache key string", c);
    };

    uint64_t hi = 0, lo = 0;
    for (size_t i = 0;  i < 16; ++i) hi = (hi << 4) | hexDigit(key_str[i]);
    for (size_t i = 16; i < 32; ++i) lo = (lo << 4) | hexDigit(key_str[i]);
    return FileCacheKey((static_cast<uint128_t>(hi) << 64) | lo);
}

} // namespace facebook::velox::ch
```

- [ ] **Step 11: Create `FileCacheUtils.h`**

Create `velox/ch/Interpreters/FileCache/FileCacheUtils.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include <cstddef>
#include <stdexcept>

namespace facebook::velox::ch::FileCacheUtils
{

inline size_t roundDownToMultiple(size_t num, size_t multiple)
{
    if (!multiple)
        return num;
    return (num / multiple) * multiple;
}

/// Rounds `num` up to the nearest multiple of `multiple`.
/// Throws std::overflow_error when the result exceeds SIZE_MAX.
/// Uses remainder-based formula to avoid false overflow.
inline size_t roundUpToMultiple(size_t num, size_t multiple)
{
    if (!multiple)
        return num;

    const size_t remainder = num % multiple;
    if (remainder == 0)
        return num;

    size_t result = 0;
    if (__builtin_add_overflow(num, multiple - remainder, &result))
        throw std::overflow_error(
            "FileCacheUtils::roundUpToMultiple: "
            "rounded-up value does not fit in size_t");
    return result;
}

} // namespace facebook::velox::ch::FileCacheUtils
```

- [ ] **Step 12: Verify the golden literals are fixed-width lowercase hex**

Run:

```bash
python3 - <<'PY'
values = [
    "f711edcba8b6b5e5e983a656dbc1b532",
    "53a3124ce5655a686c6b96daa215b4b6",
    "6ba3177b6fbaa4c9f65873033e35aeaa",
    "77dd7dd78fa45ef0b93cc3b8df847cbd",
]
assert all(len(value) == 32 for value in values)
assert all(value == value.lower() for value in values)
assert all(set(value) <= set("0123456789abcdef") for value in values)
PY
```

- [ ] **Step 13: Update `velox/ch/Common/CMakeLists.txt`**

Append `SipHash128.cpp` to the compiled sources of `velox_ch_filecache` and
add `SipHash128.h` to the header list. Task 004 established the compiled
library; this task extends it. Also add the link dependencies needed by the
new compiled code.

The exact content after this task should include (among the existing headers
and sources):

```cmake
velox_add_library(
  velox_ch_filecache
  StatusFile.cpp          # from Task 004
  SipHash128.cpp          # ← added by this task
  HEADERS
    ClickHouseAliases.h
    CurrentMetrics.h
    FailPoint.h
    FileCacheBoundedQueue.h
    FileCacheException.h
    FileCacheFilesystem.h
    FilesystemCacheLog.h
    logger_useful.h
    OpenTelemetryTraceContext.h
    ProfileEvents.h
    QueryStatus.h
    SharedMutex.h
    SipHash128.h            # ← added by this task
    StatusFile.h
    ThreadPool.h            # from Task 005
    FileCacheScheduler.h    # from Task 006
    FileCacheQueryIdScope.h # from Task 006
)
velox_link_libraries(
  velox_ch_filecache
  PUBLIC
    velox_exception
    Folly::folly
    fmt::fmt
)
```

Do not break the existing entries from Tasks 004–006. Read the current file
before writing to identify what is already present, then add only what is missing.

- [ ] **Step 14: Update `velox/ch/Interpreters/FileCache/CMakeLists.txt`**

Append compiled sources. Task 004 created this file with only the Guards.h
FILE_SET registration and `add_subdirectory(tests)`. Add a `target_sources`
call:

```cmake
target_sources(
  velox_ch_filecache
  PRIVATE
    FileSegmentKeyType.cpp
    FileCacheKey.cpp
)
```

The `add_subdirectory(tests)` block from Task 004 must remain.

- [ ] **Step 15: Build the focused test**

Reconfigure with the same command as Step 4, then build:

```bash
<ninja> \
  -C <velox_build_dir> \
  velox_ch_leaf_types_test \
  > <velox_build_dir>/build_task_008_leaf_types.log 2>&1
```

Expected:

```text
Exit code 0.
```

- [ ] **Step 16: Run the focused test**

```bash
ctest \
  --test-dir <velox_build_dir> \
  -R '^velox_ch_leaf_types_test$' \
  --output-on-failure \
  > <velox_build_dir>/test_task_008_leaf_types.log 2>&1
```

Expected:

```text
100% tests passed, 0 tests failed.
```

If a golden test fails, the `SipHash128.cpp` diverges from CH. Compare the
Step 2 generator output with the test output and fix the implementation.

- [ ] **Step 17: Inspect only task-owned changes**

```bash
cd <velox_repo>
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Common/CMakeLists.txt \
  velox/ch/Common/SipHash128.h \
  velox/ch/Common/SipHash128.cpp \
  velox/ch/Interpreters/FileCache/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/FileSegmentKeyType.h \
  velox/ch/Interpreters/FileCache/FileSegmentKeyType.cpp \
  velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h \
  velox/ch/Interpreters/FileCache/FileCache_fwd.h \
  velox/ch/Interpreters/FileCache/FileCache_fwd_internal.h \
  velox/ch/Interpreters/FileCache/FileCacheKey.h \
  velox/ch/Interpreters/FileCache/FileCacheKey.cpp \
  velox/ch/Interpreters/FileCache/FileCacheUtils.h \
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/tests/LeafTypesTest.cpp
```

Expected:

```text
No whitespace errors.
No files outside the declared scope were changed by this task.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 18: Write the result handoff**

Create:

```text
<clickhouse_repo>/port/task/result/008-filecache-leaf-types-result.md
```

Use exactly this structure:

````markdown
# Task 008 Result: `FileCache` Leaf Types

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
<paste all configure, build, test, and verification commands>
```

## Generated logs

```text
<velox_build_dir>/configure_task_008_leaf_types.log
<velox_build_dir>/build_task_008_red.log
<velox_build_dir>/build_task_008_leaf_types.log
<velox_build_dir>/test_task_008_leaf_types.log
```

## Verification

```text
Red build failed because header files were absent.
SipHash128 golden vectors match CH output (paste generator output here).
Final build exit code: 0
Focused test result: 100% passed
git diff --check result: no whitespace errors
```

## Blocking errors

```text
None
```

## Recommended next task

```text
Task 009: port ShardedMap with explicit Hash template parameter.
```
````

## Explicit exclusions

Do not implement in this task:

```text
Guards.h               (already created by Task 004)
ShardedMap.h           (Task 009)
FileCacheSettings.h/cpp (Task 010)
CacheUsage.h           (Task 011)
IFileCachePriority and concrete priority classes (Task 011)
EvictionCandidates     (Task 011)
FileSegment/Metadata/FileCache (Tasks 012+)
```
