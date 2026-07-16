# 06. `FileCacheKey` / `sipHash128` 设计

## 结论

`FileCacheKey` 必须保留 ClickHouse 语义：

```text
FileCacheKey::fromPath(path) = sipHash128(path.data(), path.size())
FileCacheKey::toString() = 32 位小写十六进制 UInt128
FileCacheKey::fromKeyString(hex) = 解析 32 位小写/大小写十六进制 UInt128
```

不要把它替换成 Velox `cache::FileCacheKey`，也不要直接换成
`folly::hash::SpookyHashV2`。

原因：

- `FileCacheKey` 参与本地 cache 目录布局。
- `FileCacheKey` 参与 metadata 恢复。
- hash 变化会导致既有 cache 目录不可识别。
- `FileCacheKey` 不是简单的内存 hash key，而是持久化路径的一部分。

## ClickHouse 当前实现

ClickHouse 当前代码：

```cpp
struct FileCacheKey
{
    using KeyHash = UInt128;
    KeyHash key;

    std::string toString() const;

    static FileCacheKey random();
    static FileCacheKey fromPath(const std::string & path);
    static FileCacheKey fromKey(const UInt128 & key);
    static FileCacheKey fromKeyString(const std::string & key_str);
};
```

核心实现：

```cpp
FileCacheKey FileCacheKey::fromPath(const std::string & path)
{
    return FileCacheKey(sipHash128(path.data(), path.size()));
}

std::string FileCacheKey::toString() const
{
    return getHexUIntLowercase(key);
}
```

`fromKeyString` 要求输入长度为 32：

```text
32 hex chars -> UInt128
```

## Velox 侧类型

Velox 已有 128-bit 基础类型：

```cpp
#include "velox/type/HugeInt.h"

using uint128_t = __uint128_t;
```

因此不要自定义新的 `UInt128Value`。`FileCacheKey` 直接使用 Velox `uint128_t`：

```cpp
struct FileCacheKey
{
    using KeyHash = uint128_t;

    KeyHash key;

    std::string toString() const;

    static FileCacheKey random();
    static FileCacheKey fromPath(std::string_view path);
    static FileCacheKey fromKey(KeyHash key);
    static FileCacheKey fromKeyString(std::string_view key);

    bool operator==(const FileCacheKey &) const = default;
    bool operator<(const FileCacheKey & other) const { return key < other.key; }
};
```

### 十六进制格式

`toString` 必须输出：

```text
high 64 bits + low 64 bits
```

每段固定 16 个 hex 字符，总长度 32：

```cpp
fmt::format(
    "{:016x}{:016x}",
    static_cast<uint64_t>(key >> 64),
    static_cast<uint64_t>(key))
```

`fromKeyString` 反向解析：

```text
key[0..16)  -> high
key[16..32) -> low
```

这样等价于 ClickHouse `UInt128` 的数值十六进制表达。

## `sipHash128` 迁移

直接迁移 ClickHouse `SipHash` 的最小实现，不引入完整 `Common/SipHash.h` 依赖树。

需要保留：

```text
SipHash state
update(data, size)
get128(low, high)
sipHash128(data, size)
```

可以放在：

```text
velox/ch/Common/SipHash128.h
velox/ch/Common/SipHash128.cpp
```

建议接口：

```cpp
class SipHash128
{
public:
    void update(const char * data, uint64_t size);
    uint128_t get128();
};

uint128_t sipHash128(const char * data, size_t size);
```

实现应保留 ClickHouse 的 SipHash 2-4 变体：

```text
key0 = 0
key1 = 0
128-bit result:
  low  = v0 ^ v1
  high = v2 ^ v3
```

组装结果时使用 Velox `uint128_t`：

```cpp
return (static_cast<uint128_t>(high) << 64) | low;
```

## 内存 hash 容器

Velox 侧优先使用 Folly `F14` 容器，不沿用 `std::unordered_map` 作为默认选择。
在 Velox 代码里，`F14FastMap` 是最常见默认选择；`F14NodeMap` 主要用于需要 value
引用/地址稳定性的场景，例如 Velox `WriterContext` 里有注释说明“Map needs
referential stability because reference to map value is stored by another class”。

建议把 CH 里的容器替换为：

| CH | Velox |
|---|---|
| `std::unordered_map<FileCacheKey, KeyMetadataPtr>` | `folly::F14FastMap<FileCacheKey, KeyMetadataPtr, FileCacheKeyHash>` |
| `absl::flat_hash_map<FileCacheKey, KeyCandidates, std::hash<FileCacheKey>>` | `folly::F14FastMap<FileCacheKey, KeyCandidates, FileCacheKeyHash>` |
| `std::unordered_map<FileCacheKeyAndOffset, IteratorPtr, FileCacheKeyAndOffsetHash>` | `folly::F14FastMap<FileCacheKeyAndOffset, IteratorPtr, FileCacheKeyAndOffsetHash>` |

这些 map 的 value 是 `shared_ptr`、`unique_ptr` 或临时聚合对象，当前没有发现需要把
value 引用/地址长期交给其他对象保存的语义，因此默认用 `F14FastMap`。如果后续迁移
某个 map 时发现外部保存了 value 引用或 iterator 并跨 rehash 使用，再改成
`F14NodeMap`。

`FileCacheKeyHash` 只影响内存 hash table，不影响持久化路径。它可以用 Velox
`bits::hashMix`：

```cpp
struct FileCacheKeyHash
{
    size_t operator()(const FileCacheKey & key) const
    {
        return bits::hashMix(
            static_cast<uint64_t>(key.key >> 64),
            static_cast<uint64_t>(key.key));
    }
};
```

`FileCacheKeyAndOffsetHash`：

```cpp
struct FileCacheKeyAndOffsetHash
{
    size_t operator()(const FileCacheKeyAndOffset & value) const
    {
        return bits::hashMix(
            FileCacheKeyHash{}(value.first),
            value.second);
    }
};
```

这部分不要求与 ClickHouse `std::hash<UInt128>` 完全一致，因为它不落盘；只要同一
进程内稳定且分布合理即可。

## `random`

`FileCacheKey::random` 只用于临时/ephemeral key，不需要与 CH 完全一致，但仍要生成
128-bit 随机值并输出 32 位 hex。

可选实现：

```cpp
FileCacheKey FileCacheKey::random()
{
    return FileCacheKey::fromKey({
        folly::Random::rand64(),
        folly::Random::rand64(),
    });
}
```

如果 Velox 环境里已有 UUID helper，也可以使用 UUID v4；关键是保持 `FileCacheKey`
外部表现为 128-bit key。

## 测试

必须加和 ClickHouse 对齐的 golden tests：

```text
fromPath("")                -> CH 输出一致
fromPath("abc")             -> CH 输出一致
fromPath("s3://bucket/key") -> CH 输出一致
fromKeyString(toString(x))  -> round trip
fromKeyString 长度不是 32   -> 抛错
operator<                  -> high 优先，再 low
FileCacheKeyHash          -> 可用于 folly::F14FastMap
```

golden 值应从 ClickHouse 当前实现生成一次后固化到 Velox 测试里。

## 与其他设计的关系

`FileCacheKey` 属于第一批落地文件：

```text
velox/ch/Interpreters/FileCache/FileCacheKey.h
velox/ch/Interpreters/FileCache/FileCacheKey.cpp
velox/ch/Common/SipHash128.h
velox/ch/Common/SipHash128.cpp
```

它是以下模块的基础依赖：

- `FileCacheOriginInfo`
- `FileSegmentInfo`
- `Metadata`
- `IFileCachePriority::Entry`
- `FileCache::get` / `getOrSet` / `set`

因此它必须在中心 SCC 之前完成。
