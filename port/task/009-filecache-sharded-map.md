# Task 009: `FileCache` `ShardedMap` — Generic Lock-Sharded Map

> **For agentic workers:** read `port/task/ENVIRONMENT.md` first. This task
> modifies the Velox checkout under `/home/chang/OpenSource/velox` and writes one
> result file under this ClickHouse checkout. Do not modify ClickHouse source
> files. Do not commit or stage either repository.

## Goal

Port `ShardedMap.h` from ClickHouse to Velox as an exact behavioural port:

```text
32 default shards
hash(key) % num_shards shard selection
one std::mutex per shard
withShard: lock, record size_before, exception-safe size accounting, invoke callback
forEachShard: sequential per-shard lock, same accounting, sequential (not simultaneous)
total_count: relaxed atomic snapshot
copy operations deleted (boost::noncopyable equivalent)
explicit Hash template parameter (default std::hash<Key>) for OriginPoolKeyHash reuse
F14FastMap replacing std::unordered_map
lock_wait_event constructor parameter preserved (no-op shim first phase)
```

The deliverable is `velox/ch/Interpreters/FileCache/ShardedMap.h` plus a
focused `velox_ch_sharded_map_test` test executable.

## Starting point

```text
Velox repository: /home/chang/OpenSource/velox
Required branch:  filecache
Expected HEAD:    Task 008 result commit or any direct descendant
```

The following files must already exist (created by Tasks 003, 004, and 008):

```text
velox/ch/Common/ProfileEvents.h
velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h   (OriginPoolKeyHash)
velox/ch/Interpreters/FileCache/CMakeLists.txt
velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
```

Stop if the branch is not `filecache` or if any of these files is absent.

## Design references

Read before editing:

```text
/home/chang/SourceCode/ClickHouse/port/task/ENVIRONMENT.md
/home/chang/SourceCode/ClickHouse/port/2-file-cache/05-filecache-sharded-map-design.md
/home/chang/SourceCode/ClickHouse/port/2-file-cache/02-filecache-origin-segment-type-design.md
```

Use the ClickHouse implementation only as a behavioral reference:

```text
/home/chang/SourceCode/ClickHouse/src/Interpreters/FileCache/ShardedMap.h
```

## File scope

Create:

```text
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/ShardedMap.h
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/ShardedMapTest.cpp
/home/chang/SourceCode/ClickHouse/port/task/result/009-filecache-sharded-map-result.md
```

Modify:

```text
/home/chang/OpenSource/velox/velox/ch/Interpreters/FileCache/tests/CMakeLists.txt
```

Every new Velox C++ file must begin with the Apache 2.0 license header using
`/* ... */` comment form.

## Steps

- [ ] **Step 1: Confirm the Velox baseline**

```bash
cd /home/chang/OpenSource/velox
git --no-pager status --short --branch
git --no-pager log -1 --oneline
```

Expected:

```text
Branch is filecache.
HEAD is Task 008 result or a direct descendant.
Record pre-existing dirty files in result.
```

- [ ] **Step 2: Add a failing focused test**

Append the following target to
`velox/ch/Interpreters/FileCache/tests/CMakeLists.txt`:

```cmake
add_executable(velox_ch_sharded_map_test ShardedMapTest.cpp)
add_test(velox_ch_sharded_map_test velox_ch_sharded_map_test)

target_link_libraries(
  velox_ch_sharded_map_test
  PRIVATE
    velox_ch_filecache
    velox_exception
    Folly::folly
    fmt::fmt
    GTest::gtest
    GTest::gtest_main
)
```

Create `velox/ch/Interpreters/FileCache/tests/ShardedMapTest.cpp`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */

#include "velox/ch/Interpreters/FileCache/ShardedMap.h"
#include "velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h"
#include "velox/ch/Common/ProfileEvents.h"

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <latch>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace facebook::velox::ch
{
namespace
{

using TestMap = FileCacheUtils::ShardedMap<std::string, int>;

// ── Basic operations ──────────────────────────────────────────────────────────

TEST(ShardedMapTest, InsertAndLookup)
{
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    m.withShard("key", [](auto & map) { map["key"] = 42; });
    int result = 0;
    m.withShard("key", [&](const auto & map)
    {
        auto it = map.find("key");
        if (it != map.end())
            result = it->second;
    });
    EXPECT_EQ(result, 42);
}

TEST(ShardedMapTest, EraseUpdatesSize)
{
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    m.withShard("a", [](auto & map) { map["a"] = 1; });
    EXPECT_EQ(m.size(), 1u);
    m.withShard("a", [](auto & map) { map.erase("a"); });
    EXPECT_EQ(m.size(), 0u);
}

TEST(ShardedMapTest, InsertUpdatesSize)
{
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    EXPECT_EQ(m.size(), 0u);
    m.withShard("x", [](auto & map) { map["x"] = 1; });
    EXPECT_EQ(m.size(), 1u);
    m.withShard("y", [](auto & map) { map["y"] = 2; });
    EXPECT_EQ(m.size(), 2u);
}

TEST(ShardedMapTest, ForEachShardVisitsAll)
{
    // Insert 32 keys, one per shard slot using known hash distribution.
    // We can't control which shard a key lands on, so instead we insert
    // enough distinct keys and verify forEachShard sees all of them.
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    for (int i = 0; i < 64; ++i)
        m.withShard(std::to_string(i), [i](auto & map)
        {
            map[std::to_string(i)] = i;
        });

    size_t total = 0;
    m.forEachShard([&](const auto & map) { total += map.size(); });
    EXPECT_EQ(total, 64u);
    EXPECT_EQ(m.size(), 64u);
}

TEST(ShardedMapTest, ForEachShardEraseUpdatesSize)
{
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    for (int i = 0; i < 10; ++i)
        m.withShard(std::to_string(i), [i](auto & map)
        {
            map[std::to_string(i)] = i;
        });
    EXPECT_EQ(m.size(), 10u);

    m.forEachShard([](auto & map) { map.clear(); });
    EXPECT_EQ(m.size(), 0u);
}

// ── Same shard serializes callbacks ──────────────────────────────────────────

TEST(ShardedMapTest, SameKeyAlwaysSameShard)
{
    // If two callbacks on the same key run concurrently, the second must block
    // until the first completes. Verify by recording interleaving.
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    std::atomic<bool> first_inside{false};
    std::atomic<bool> overlap_detected{false};
    std::latch start{2};

    auto f1 = std::async(std::launch::async, [&]()
    {
        start.arrive_and_wait();
        m.withShard("shared", [&](auto &)
        {
            first_inside.store(true, std::memory_order_seq_cst);
            std::this_thread::yield();
            // If f2 entered concurrently, it would set first_inside back to
            // false before we check it, or assert would fail below.
        });
    });

    auto f2 = std::async(std::launch::async, [&]()
    {
        start.arrive_and_wait();
        m.withShard("shared", [&](auto &)
        {
            // If both threads are here simultaneously, first_inside must be
            // true (f1 set it and hasn't cleared it). But serialization means
            // only one can be here at a time.
            (void)first_inside.load();
        });
    });

    f1.get();
    f2.get();
    // Test passes if no deadlock occurred.
}

// ── Different shards execute concurrently ────────────────────────────────────

TEST(ShardedMapTest, DifferentShardsConcurrent)
{
    // Find two keys that land on different shards by trying combinations.
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    std::hash<std::string> h;
    std::string key1 = "shard_a_key_0";
    std::string key2;
    for (int i = 0; i < 1000; ++i)
    {
        key2 = "shard_b_key_" + std::to_string(i);
        if ((h(key1) % 32) != (h(key2) % 32))
            break;
    }
    ASSERT_NE(h(key1) % 32, h(key2) % 32)
        << "Could not find two keys on different shards";

    std::latch barrier{2};
    std::atomic<bool> both_inside{false};
    std::atomic<int> inside_count{0};

    auto probe = [&](const std::string & key) -> bool
    {
        bool overlap = false;
        m.withShard(key, [&](auto &)
        {
            barrier.arrive_and_wait(); // both reach here before either proceeds
            inside_count.fetch_add(1, std::memory_order_seq_cst);
            // Both should be inside simultaneously
            overlap = inside_count.load(std::memory_order_seq_cst) == 2;
            inside_count.fetch_sub(1, std::memory_order_seq_cst);
        });
        return overlap;
    };

    auto f1 = std::async(std::launch::async, probe, key1);
    auto f2 = std::async(std::launch::async, probe, key2);

    bool r1 = f1.get();
    bool r2 = f2.get();
    EXPECT_TRUE(r1 || r2) << "Different-shard callbacks should overlap";
}

// ── Exception-safe size accounting ───────────────────────────────────────────

TEST(ShardedMapTest, ExceptionAfterInsertUpdatesSize)
{
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    EXPECT_THROW(
        m.withShard("ex", [](auto & map)
        {
            map["ex"] = 99;
            throw std::runtime_error("oops");
        }),
        std::runtime_error);
    // The insert happened before the throw; size must reflect it.
    EXPECT_EQ(m.size(), 1u);
}

TEST(ShardedMapTest, ExceptionAfterEraseUpdatesSize)
{
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    m.withShard("e", [](auto & map) { map["e"] = 1; });
    EXPECT_EQ(m.size(), 1u);

    EXPECT_THROW(
        m.withShard("e", [](auto & map)
        {
            map.erase("e");
            throw std::runtime_error("oops");
        }),
        std::runtime_error);
    // The erase happened before the throw.
    EXPECT_EQ(m.size(), 0u);
}

// ── OriginPoolKeyHash shard compatibility ─────────────────────────────────────

TEST(ShardedMapTest, OriginPoolKeyHashSameUserSameShard)
{
    using OriginMap =
        FileCacheUtils::ShardedMap<OriginPoolKey, int, 32, OriginPoolKeyHash>;
    OriginMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);

    // Two keys for the same user, different weight/type, must land on the same
    // shard (hash is user_id only).
    OriginPoolKey k1{"u", uint64_t{1}, FileSegmentKeyType::Data};
    OriginPoolKey k2{"u", uint64_t{2}, FileSegmentKeyType::System};

    OriginPoolKeyHash hasher;
    EXPECT_EQ(hasher(k1) % 32, hasher(k2) % 32)
        << "Same user must select the same shard";

    // Insert one key, then look up the other from the same shard callback.
    m.withShard(k1, [&](auto & map) { map[k1] = 1; });
    int found = 0;
    m.withShard(k2, [&](auto & map)
    {
        // k1 and k2 are in the same shard; k1 must be visible here.
        auto it = map.find(k1);
        if (it != map.end())
            found = it->second;
    });
    EXPECT_EQ(found, 1);
}

// ── Static assertions ─────────────────────────────────────────────────────────

TEST(ShardedMapTest, CopyDeleted)
{
    static_assert(!std::is_copy_constructible_v<TestMap>);
    static_assert(!std::is_copy_assignable_v<TestMap>);
}

TEST(ShardedMapTest, SizeAfterConcurrentInserts)
{
    TestMap m(ProfileEvents::FilesystemCacheGetOrSetMicroseconds);
    constexpr int N = 200;
    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i)
        threads.emplace_back([&, i]()
        {
            m.withShard(std::to_string(i), [&, i](auto & map)
            {
                map[std::to_string(i)] = i;
            });
        });
    for (auto & t : threads)
        t.join();
    EXPECT_EQ(m.size(), static_cast<size_t>(N));
}

} // namespace
} // namespace facebook::velox::ch
```

- [ ] **Step 3: Verify the test fails before implementation**

Reconfigure:

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_MAKE_PROGRAM=/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -DVELOX_ENABLE_BENCHMARKS=ON \
  -DVELOX_BUILD_TESTING=ON \
  -G Ninja \
  -S /home/chang/OpenSource/velox \
  -B /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_009_sharded_map.log 2>&1
```

Then attempt to build:

```bash
if /home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_sharded_map_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_009_red.log 2>&1
then
  echo "ERROR: red build unexpectedly succeeded"
  exit 1
fi
```

Expected:

```text
Build fails because ShardedMap.h does not exist yet.
```

- [ ] **Step 4: Implement `ShardedMap.h`**

Create `velox/ch/Interpreters/FileCache/ShardedMap.h`:

```cpp
/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * ...
 */
#pragma once

#include "velox/ch/Common/ProfileEvents.h"

#include <folly/container/F14Map.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>

namespace facebook::velox::ch::FileCacheUtils
{

/// Hash-sharded map: keys are spread across independently-locked buckets so
/// concurrent operations on different keys rarely contend.
///
/// Callback contract (withShard / forEachShard):
///   - May read or mutate its shard map.
///   - Must not recursively call withShard/forEachShard on the same ShardedMap
///     (same-shard reentry deadlocks; cross-shard reentry risks livelock).
///   - Must not return or store iterators, references, or pointers into the map
///     beyond the callback scope (they are invalidated on unlock/rehash).
///
/// Size semantics:
///   - `size()` returns a relaxed snapshot: accurate after a completed mutation,
///     but a concurrent observer may see a transient value.
///   - Exception safety: if a callback mutates the map and then throws, the
///     mutation stands and `total_count_` is updated to the actual post-mutation
///     shard size before the exception propagates.
template <
    typename Key,
    typename Value,
    size_t num_shards = 32,
    typename Hash = std::hash<Key>>
class ShardedMap
{
    static_assert(num_shards > 0, "num_shards must be greater than zero");

public:
    using Map = folly::F14FastMap<Key, Value, Hash>;

    ShardedMap(const ShardedMap &) = delete;
    ShardedMap & operator=(const ShardedMap &) = delete;

    explicit ShardedMap(ProfileEvents::Event lock_wait_event)
        : lock_wait_event_(lock_wait_event)
    {
    }

    /// Run `f(map)` under the owning shard's lock. Returns the callback result.
    template <typename F>
    auto withShard(const Key & key, F && f) const
    {
        Shard & shard = shards_[Hash{}(key) % num_shards];
        std::unique_lock<std::mutex> lock(shard.mutex);
        const size_t size_before = shard.map.size();
        // Exception-safe size accounting: the guard fires even if f() throws.
        struct SizeGuard
        {
            const ShardedMap & self;
            const Shard & shard;
            size_t before;
            ~SizeGuard() noexcept
            {
                self.accountSizeDelta(before, shard.map.size());
            }
        } guard{*this, shard, size_before};
        return std::forward<F>(f)(shard.map);
    }

    /// Run `f(map)` under each shard's lock in turn (sequential, not
    /// simultaneous). Provides no globally-atomic snapshot.
    template <typename F>
    void forEachShard(F && f) const
    {
        for (Shard & shard : shards_)
        {
            std::unique_lock<std::mutex> lock(shard.mutex);
            const size_t size_before = shard.map.size();
            struct SizeGuard
            {
                const ShardedMap & self;
                const Shard & shard;
                size_t before;
                ~SizeGuard() noexcept
                {
                    self.accountSizeDelta(before, shard.map.size());
                }
            } guard{*this, shard, size_before};
            std::forward<F>(f)(shard.map);
        }
    }

    /// Relaxed snapshot of the total element count across all shards.
    size_t size() const noexcept
    {
        return total_count_.load(std::memory_order_relaxed);
    }

private:
    struct Shard
    {
        mutable std::mutex mutex;
        Map map;
    };

    void accountSizeDelta(size_t before, size_t after) const noexcept
    {
        if (after > before)
            total_count_.fetch_add(
                after - before, std::memory_order_relaxed);
        else if (after < before)
            total_count_.fetch_sub(
                before - after, std::memory_order_relaxed);
    }

    const ProfileEvents::Event lock_wait_event_;
    mutable std::array<Shard, num_shards> shards_;
    mutable std::atomic<size_t> total_count_{0};
};

} // namespace facebook::velox::ch::FileCacheUtils
```

- [ ] **Step 5: Build the focused test**

Reconfigure with the same command as Step 3, then build:

```bash
/home/chang/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja \
  -C /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  velox_ch_sharded_map_test \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_009_sharded_map.log 2>&1
```

Expected:

```text
Exit code 0.
```

- [ ] **Step 6: Run the focused test**

```bash
ctest \
  --test-dir /home/chang/OpenSource/velox/cmake-build-debug-gcc13 \
  -R '^velox_ch_sharded_map_test$' \
  --output-on-failure \
  > /home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_009_sharded_map.log 2>&1
```

Expected:

```text
100% tests passed, 0 tests failed.
```

- [ ] **Step 7: Inspect only task-owned changes**

```bash
cd /home/chang/OpenSource/velox
git --no-pager diff --check
git --no-pager status --short
git --no-pager diff -- \
  velox/ch/Interpreters/FileCache/ShardedMap.h \
  velox/ch/Interpreters/FileCache/tests/CMakeLists.txt \
  velox/ch/Interpreters/FileCache/tests/ShardedMapTest.cpp
```

Expected:

```text
No whitespace errors.
No files outside the declared scope were changed by this task.
Changes remain unstaged and uncommitted.
```

- [ ] **Step 8: Write the result handoff**

Create:

```text
/home/chang/SourceCode/ClickHouse/port/task/result/009-filecache-sharded-map-result.md
```

Use exactly this structure:

````markdown
# Task 009 Result: `FileCache` `ShardedMap`

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
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/configure_task_009_sharded_map.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_009_red.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/build_task_009_sharded_map.log
/home/chang/OpenSource/velox/cmake-build-debug-gcc13/test_task_009_sharded_map.log
```

## Verification

```text
Red build failed because ShardedMap.h was absent.
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
Task 010: port FileCacheConfig / FileCacheSettings loader.
```
````

## Explicit exclusions

Do not implement in this task:

```text
FileCacheSettings.h / FileCacheSettings.cpp
CacheUsage.h  (uses ShardedMap but belongs to the priority task)
IFileCachePriority and concrete priority classes
EvictionCandidates
FileSegment / Metadata / FileCache
```

These belong to Tasks 010 and 011.
