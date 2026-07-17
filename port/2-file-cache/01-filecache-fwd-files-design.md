# 01. `FileCache` forward文件迁移设计

## 结论

本批次严格按两个文件 review：

```text
src/Interpreters/FileCache/FileCache_fwd.h
src/Interpreters/FileCache/FileCache_fwd_internal.h
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/FileCache_fwd.h
velox/ch/Interpreters/FileCache/FileCache_fwd_internal.h
```

This batch is an exact constants, enum, ownership-alias, and container-semantics port, not a source-level copy or redesign.

本批次精确迁移 FileCache 默认值、policy enum values、shared/weak ownership aliases和
`FileSegments` 的 `std::list` 语义，不要求逐行复制源码。只允许替换 CH settings
infrastructure和基础整数类型。

## `FileCache_fwd.h`

### 文件功能

虽然文件名是 `fwd`，它实际承担两类职责：

```text
1. FileCache public forward declarations / ownership aliases
2. FileCacheConfig 使用的默认常量
```

它被 reader/writer、settings、factory、`FileCache.h` 和 `FileSegmentInfo.h` 广泛 include，
因此 target header 必须保持轻量，不能引入 Velox connector、filesystem 或完整
`FileCache` 定义。

### `FileCachePolicy`

CH 的 enum 定义在 `Core/SettingsEnums.h`：

```cpp
enum class FileCachePolicy : uint8_t
{
    LRU,
    SLRU,
    SLRU_OVERCOMMIT,
    LRU_OVERCOMMIT,
};
```

Velox 不迁移整个 CH settings-enum framework。把同值、同顺序 enum 放进
`FileCache_fwd.h`：

```cpp
enum class FileCachePolicy : uint8_t
{
    LRU,
    SLRU,
    SLRU_OVERCOMMIT,
    LRU_OVERCOMMIT,
};
```

必须保留 overcommit enum values，以便配置解析给出明确 unsupported error；不因为第一阶段
不实现 overcommit 就静默映射成普通 LRU/SLRU。

### 默认常量

数值直接迁移：

| constant | value | behavior |
|---|---:|---|
| `FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE` | 32 MiB | normal max segment size |
| `FILECACHE_DEFAULT_FILE_SEGMENT_ALIGNMENT` | 4 MiB | range alignment |
| `FILECACHE_DEFAULT_RESERVE_GRANULARITY` | 4 MiB | reserve-ahead granularity |
| `FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE_WITH_BACKGROUND_DOWLOAD` | 4 MiB | background continuation target |
| `FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_THREADS` | 5 | long-running download workers |
| `FILECACHE_DEFAULT_BACKGROUND_DOWNLOAD_QUEUE_SIZE_LIMIT` | 5000 | pending background segments |
| `FILECACHE_DEFAULT_LOAD_METADATA_THREADS` | 16 | startup metadata parallelism |
| `FILECACHE_DEFAULT_MAX_ELEMENTS` | 10,000,000 | file-segment count limit |
| `FILECACHE_BYPASS_THRESHOLD` | 256 MiB | optional large-read bypass |
| `FILECACHE_DEFAULT_FREE_SPACE_SIZE_RATIO` | 0 | disabled |
| `FILECACHE_DEFAULT_FREE_SPACE_ELEMENTS_RATIO` | 0 | disabled |
| `FILECACHE_DEFAULT_FREE_SPACE_REMOVE_BATCH` | 250 | background removal batch |
| `FILECACHE_DEFAULT_FREE_SPACE_EVICTION_THREADS` | 1 | remover concurrency when enabled |
| `FILECACHE_DEFAULT_CACHE_POLICY` | `SLRU` | default priority policy |
| `FILECACHE_DEFAULT_SLRU_RATIO` | 0.6 | 60% protected / 40% probationary |

target 使用明确宽度：

```cpp
inline constexpr uint64_t FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE =
    32ULL * 1024 * 1024;
```

size/count constants统一使用 `uint64_t`；ratio 使用 `double`。数值不能改变。

`FILECACHE_DEFAULT_MAX_FILE_SEGMENT_SIZE_WITH_BACKGROUND_DOWLOAD` 的 `DOWLOAD` 是现有内部
identifier。第一阶段保留拼写以减少算法文件 diff；如以后修正，先提供 alias，不能在同一
迁移批次造成无关 rename。

### config path constant

CH：

```cpp
FILECACHE_DEFAULT_CONFIG_PATH = "filesystem_caches";
```

它只服务 CH `FileCacheFactory` 的 Poco configuration layout，不是 cache algorithm
default。Velox config key layout见
[`FileCacheSettings`](06-filecache-settings-files-design.md#config-key-layout)。

因此 target `FileCache_fwd.h` 不保留这个 CH-specific constant；Velox config prefix 放在
`FileCacheConfig` parser/manager 中。该差异属于已 review 的 config infrastructure
替换，不影响 cache data/path semantics。

### ownership alias

直接迁移：

```cpp
class FileCache;
using FileCachePtr = std::shared_ptr<FileCache>;
```

不能改成 `unique_ptr` 或 raw pointer：

```text
manager owns cache
readers/streams keep cache alive
QueryContextHolder and FileSegment raw back-pointers rely on FileCachePtr ownership elsewhere
```

### settings/config alias

CH：

```cpp
struct FileCacheSettings;
```

Velox 的真实配置类型是 `FileCacheConfig`。为算法迁移保留 CH type name：

```cpp
struct FileCacheConfig;
using FileCacheSettings = FileCacheConfig;
```

这样 `FileCache.h/.cpp` 可以少改类型名，同时 config parser和 manager 使用
`FileCacheConfig` 真实名称。

### 其他 forward declarations

保留：

```cpp
struct FileCacheKey;
```

`FileCacheUserInfo` 在当前 OSS checkout 中只有 declaration、没有任何 use。target 不保留
这个 dead forward declaration。

### include surface

target header 只需要：

```cpp
#include <cstdint>
#include <memory>
```

不再 include：

```text
Core/SettingsEnums.h
connector headers
FileCacheSettings.h
FileCache.h
```

避免让高 fan-out reader/settings headers传递 CH settings machinery。

### 与 `FileCacheConfig` 的关系

`FileCacheConfig` 字段默认值必须引用这里的 constants，而不是复制 numeric literals：

```text
maxFileSegmentSize
boundaryAlignment
reserveGranularity
cachePolicy
slruSizeRatio
backgroundDownloadThreads
backgroundDownloadQueueSizeLimit
backgroundDownloadMaxFileSegmentSize
loadMetadataThreads
keepFreeSpace*
maxElements
```

这样 target 默认值只有一个 source of truth。

## `FileCache_fwd_internal.h`

### 文件功能

这个文件定义中心 SCC 内部共享的 forward declarations、ownership aliases 和
`FileSegments` 容器：

```text
FileCache / priority
FileSegment / FileSegments
FileSegmentMetadata
LockedKey
KeyMetadata
```

它被以下中心文件共同 include：

```text
IFileCachePriority.h
FileSegment.h
Metadata.h
FileCache.h
```

target 直接保留独立 `FileCache_fwd_internal.h`，不把这些内部 aliases 合并到 public
`FileCache_fwd.h`，避免 reader-facing public include surface 暴露 metadata/priority
内部类型。

### cache aliases

保留：

```cpp
class FileCache;
using FileCachePtr = std::shared_ptr<FileCache>;
```

它与 public forward header重复是刻意的轻量 declaration，不改变 ownership。

### priority ownership

保留：

```cpp
class IFileCachePriority;
using FileCachePriorityPtr = std::shared_ptr<IFileCachePriority>;
```

这里不能统一改成 `unique_ptr`。`FileCachePriorityPtr` 用于 `CacheUsagePerUser` 等需要把
priority 与其 usage snapshot/lifetime 关联的共享 ownership 路径。

具体组合类内部仍可使用另一个明确的：

```cpp
using IFileCachePriorityPtr = std::unique_ptr<IFileCachePriority>;
```

两者含义不同，不能因为名字接近而合并：

```text
FileCachePriorityPtr:
  shared ownership where priority lifetime escapes one owner

IFileCachePriorityPtr:
  unique ownership inside Split/LRU/SLRU composition
```

### `FileSegment` ownership和容器

直接迁移：

```cpp
class FileSegment;
using FileSegmentPtr = std::shared_ptr<FileSegment>;
using FileSegments = std::list<FileSegmentPtr>;
```

`FileSegmentPtr` 必须是 `shared_ptr`：

```text
metadata owns the canonical segment
holders keep it alive during reads
eviction candidates and background queues may hold/observe it
detach removes metadata ownership while existing holders remain valid
```

`FileSegments` 必须保持 `std::list`，不能改成 `std::vector` / Folly vector：

```text
FileCache::fillHolesWithEmptyFileSegments inserts in the middle
FileCache::getOrSet splices prefix/hole/suffix lists
FileSegmentsHolder erases while iterating
callers use stable iterators/references to list elements
```

`splice` 是算法的一部分，保持 `std::list` 才能做到 constant-time node transfer并维持
iterator stability。

### `FileSegmentMetadataPtr`

直接迁移：

```cpp
struct FileSegmentMetadata;
using FileSegmentMetadataPtr = std::shared_ptr<FileSegmentMetadata>;
```

同一个 metadata entry 会同时被：

```text
KeyMetadata ordered map
priority iteration/candidate collection
EvictionCandidates
```

持有。eviction 的多阶段流程需要在 queue entry移除、filesystem deletion和 metadata
finalization之间保持对象存活，因此不能改成 raw/unique pointer。

### `LockedKeyPtr`

直接迁移：

```cpp
struct LockedKey;
using LockedKeyPtr = std::shared_ptr<LockedKey>;
```

`LockedKey` 同时封装：

```text
KeyMetadata shared ownership
KeyGuard lock lifetime
```

返回 shared pointer允许 lock lifetime跨 helper/callback传递，并在最后一个引用释放时按
成员析构顺序先释放 lock、再释放 key metadata。不能用裸 pointer，也不能把 lock 从
`LockedKey` 拆开。

### `KeyMetadata` ownership

直接迁移：

```cpp
struct KeyMetadata;
using KeyMetadataPtr = std::shared_ptr<KeyMetadata>;
using KeyMetadataWeakPtr = std::weak_ptr<KeyMetadata>;
```

shared ownership路径：

```text
metadata bucket owns KeyMetadata
priority entries/candidates keep key context alive
LockedKey holds it while the key lock is active
```

weak ownership路径：

```text
FileSegment -> KeyMetadata
priority Entry -> KeyMetadata
```

弱引用打破：

```text
KeyMetadata -> FileSegmentMetadata -> FileSegment -> KeyMetadata
```

以及 priority/metadata之间的 ownership cycle。不能把 `KeyMetadataWeakPtr` 改为
`shared_ptr`。

### include surface

target header只需要：

```cpp
#include <list>
#include <memory>
```

不 include任何完整 class definition。

## 测试要求

```text
FileCachePolicy underlying values/order match CH
all numeric defaults match CH
default free-space ratios remain disabled
default policy is SLRU
SLRU ratio is 0.6
FileCachePtr is shared_ptr<FileCache>
FileCacheSettings aliases FileCacheConfig
header compiles without CH Core/SettingsEnums.h
reader-facing headers can include it without full FileCache definition
internal header compiles with only list/memory
FileSegments is std::list<FileSegmentPtr>
all shared/weak aliases match CH ownership
KeyMetadataWeakPtr remains weak_ptr
public fwd header does not expose internal metadata/priority aliases
```

## Review 状态

`FileCache_fwd.h` 和 `FileCache_fwd_internal.h` 已按文件 review。public defaults/policy、
internal ownership graph、`std::list` container semantics以及 shared/weak aliases直接迁移；
仅移除 CH settings-enum/config-path infrastructure及未使用的
`FileCacheUserInfo` declaration。
