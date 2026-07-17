# 12. `FileSegmentKeyType` / `FileCacheOriginInfo` 文件迁移设计

## 结论

这一组不是重新设计两个抽象，而是 **精确迁移三个 ClickHouse 文件**：

```text
src/Interpreters/FileCache/FileSegmentKeyType.h
src/Interpreters/FileCache/FileSegmentKeyType.cpp
src/Interpreters/FileCache/FileCacheOriginInfo.h
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/FileSegmentKeyType.h
velox/ch/Interpreters/FileCache/FileSegmentKeyType.cpp
velox/ch/Interpreters/FileCache/FileCacheOriginInfo.h
```

除基础类型和 include 路径外，语义应保持与 ClickHouse 一致。

## `FileSegmentKeyType.h`

直接迁移 enum：

```cpp
enum class FileSegmentKeyType : uint8_t
{
    General = 0,
    System,
    Data,
};
```

保留函数声明：

```cpp
std::string getKeyTypePrefix(FileSegmentKeyType type);
std::string toString(FileSegmentKeyType type);
```

类型替换：

```text
String -> std::string
UInt8 / uint8_t -> uint8_t
```

## `FileSegmentKeyType.cpp`

ClickHouse 实现依赖 `magic_enum::enum_name`：

```cpp
String getKeyTypePrefix(FileSegmentKeyType type)
{
    if (type == FileSegmentKeyType::General)
        return "";
    return String(magic_enum::enum_name(type));
}

String toString(FileSegmentKeyType type)
{
    return String(magic_enum::enum_name(type));
}
```

Velox 可以精确保留语义，但不强依赖 `magic_enum`。建议用 `switch`：

```cpp
namespace
{
std::string_view toStringView(FileSegmentKeyType type)
{
    switch (type)
    {
        case FileSegmentKeyType::General:
            return "General";
        case FileSegmentKeyType::System:
            return "System";
        case FileSegmentKeyType::Data:
            return "Data";
    }
    VELOX_FAIL("Unknown FileSegmentKeyType: {}", static_cast<uint8_t>(type));
}
}

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
```

必须保持：

```text
getKeyTypePrefix(General) == ""
getKeyTypePrefix(System) == "System"
getKeyTypePrefix(Data) == "Data"
toString(General) == "General"
```

`General` prefix 为空会影响 cache path layout，不能改成 `"General"`。

## `FileCacheOriginInfo.h`

直接迁移结构：

```cpp
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
        : user_id(user_id_)
        , weight(weight_)
        , segment_type(segment_type_)
    {
    }

    bool operator==(const FileCacheOriginInfo & other) const
    {
        return user_id == other.user_id;
    }
};
```

保留 `operator==` 只比较 `user_id`。不要在这一步改成比较 `weight` 或
`segment_type`。

同时迁移 `OriginPoolKey`：

```cpp
struct OriginPoolKey
{
    FileCacheOriginInfo::UserID user_id;
    std::optional<FileCacheOriginInfo::Weight> weight;
    FileCacheOriginInfo::SegmentKeyType segment_type;

    bool operator==(const OriginPoolKey & other) const = default;
};
```

以及 hash：

```cpp
struct OriginPoolKeyHash
{
    size_t operator()(const OriginPoolKey & key) const noexcept
    {
        return std::hash<FileCacheOriginInfo::UserID>{}(key.user_id);
    }
};
```

ClickHouse 是 `std::hash<DB::OriginPoolKey>` specialization。Velox port 更建议显式
`OriginPoolKeyHash`，用于 `folly::F14FastMap`：

```cpp
folly::F14FastMap<OriginPoolKey, OriginInfoPtr, OriginPoolKeyHash>
```

hash 只用 `user_id`，equality 比较全部字段。这个策略和 ClickHouse 一致：

```text
origin 数量很少
user_id 主导分布
hash collision 不影响正确性
```

## 语义依赖

### path prefix

`FileSegmentKeyType` 影响本地 cache path。必须保持：

```text
General -> empty prefix
System  -> System prefix
Data    -> Data prefix
```

### priority routing

`SplitFileCachePriority` 使用 `origin->segment_type`：

```text
Data    -> Data priority
General -> Data priority
System  -> System priority
```

这个文件组只提供类型和值；routing 行为在 priority 迁移时验证。

### origin identity

`FileCacheOriginInfo` 表示 key 的 origin：

```text
user_id
optional weight
segment_type
```

`KeyMetadata` 持有 immutable `shared_ptr<const FileCacheOriginInfo>`。
`OriginPoolKey` 用于 dedup shared origin。

## 测试要求

文件级测试即可：

```text
toString(General/System/Data)
getKeyTypePrefix(General) == ""
getKeyTypePrefix(System) == "System"
getKeyTypePrefix(Data) == "Data"
FileCacheOriginInfo equality compares only user_id
OriginPoolKey equality compares user_id + weight + segment_type
OriginPoolKeyHash works in folly::F14FastMap
```

priority routing 测试放到 priority 迁移阶段。

## Review 状态

本文档已 review。当前决策：

```text
exactly port FileSegmentKeyType.h
exactly port FileSegmentKeyType.cpp semantics
exactly port FileCacheOriginInfo.h semantics
use explicit OriginPoolKeyHash instead of std::hash specialization
do not change General empty prefix
do not change FileCacheOriginInfo user-id-only equality
```
