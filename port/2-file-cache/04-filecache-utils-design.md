# 04. `FileCacheUtils.h` 迁移设计

## 结论

本批次严格按一个文件 review：

```text
src/Interpreters/FileCache/FileCacheUtils.h
```

Velox 目标文件：

```text
velox/ch/Interpreters/FileCache/FileCacheUtils.h
```

This batch is an exact arithmetic-helper port, not a redesign.

本文件可以近乎逐行迁移。只替换 namespace和 CH overflow helper include；以下行为都不能
改变：

```text
multiple == 0 means no alignment
round-down behavior
round-up behavior
representable SIZE_MAX boundary results
actual overflow throws std::overflow_error
```

## 文件功能

文件提供两个 alignment helpers：

```cpp
roundDownToMultiple(num, multiple)
roundUpToMultiple(num, multiple)
```

调用位置：

```text
FileCache::getOrSet:
  align requested left/right range

FileSegment::shrinkFileSegmentToDownloadedSize:
  align retained partial segment size

FileSegment::getSizeForBackgroundDownload:
  align background continuation target
```

错误会直接影响：

```text
segment boundaries
hole creation
reserved/retained bytes
background download size
```

因此不替换成语义相似但边界/overflow contract 不同的通用 helper。

## namespace

CH 文件位于 global：

```cpp
namespace FileCacheUtils
```

Velox target 使用：

```cpp
namespace facebook::velox::ch::FileCacheUtils
```

算法文件在 `facebook::velox::ch` 内仍可写：

```cpp
FileCacheUtils::roundUpToMultiple(...)
```

## `roundDownToMultiple`

直接迁移：

```cpp
inline size_t roundDownToMultiple(size_t num, size_t multiple)
{
    if (!multiple)
        return num;
    return (num / multiple) * multiple;
}
```

contract：

```text
multiple == 0 -> num
num < multiple -> 0
num already aligned -> num
otherwise -> greatest representable multiple below num
```

division happens before multiplication，因此 multiplication不能 overflow：

```text
(num / multiple) * multiple <= num
```

## `roundUpToMultiple`

直接迁移 remainder-based 算法：

```cpp
inline size_t roundUpToMultiple(size_t num, size_t multiple)
{
    if (!multiple)
        return num;

    const size_t remainder = num % multiple;
    if (remainder == 0)
        return num;

    size_t result = 0;
    if (__builtin_add_overflow(
            num, multiple - remainder, &result))
    {
        throw std::overflow_error(
            "FileCacheUtils::roundUpToMultiple: "
            "rounded-up value does not fit in size_t");
    }

    return result;
}
```

只把：

```cpp
common::addOverflow(...)
```

替换为同语义的：

```cpp
__builtin_add_overflow(...)
```

Velox 代码库已有该 builtin 的大量使用，不需要迁移 CH
`base/arithmeticOverflow.h`。

## 为什么不用常见公式

禁止改成：

```cpp
((num + multiple - 1) / multiple) * multiple
```

因为中间加法可能 false overflow，即使最终结果可表示：

```text
num = 2
multiple = SIZE_MAX
correct result = SIZE_MAX
```

remainder-based 版本只在最终 rounded value 真正超过 `SIZE_MAX` 时抛异常。

## 为什么不使用 Velox `checkedPlus`

Velox `checkedPlus` 底层也使用 `__builtin_add_overflow`，但抛
`VELOX_ARITHMETIC_ERROR`。

本 helper 当前 public contract和测试明确要求：

```cpp
std::overflow_error
```

直接使用 builtin 可以：

```text
保持原 exception type
保持原 message
避免引入重的 Velox Exceptions.h 到 lightweight header
减少 source diff
```

因此不使用 `checkedPlus`。

## caller preconditions

helper 只能检测自身执行的 round-up addition。caller 在调用前构造 `num` 时也必须避免
overflow：

```text
offset + size
range.right + 1
```

`FileCache.h/.cpp` 的 range contract由
[`FileCache` 核心文件设计](10-filecache-core-files-design.md)负责；不能假设本 helper能修复在参数传入前
已经发生的 wraparound。

## include surface

target header只需要：

```cpp
#include <cstddef>
#include <stdexcept>
```

不 include：

```text
base/arithmeticOverflow.h
velox/common/base/CheckedArithmetic.h
velox/common/base/Exceptions.h
```

保持高频算法 header 轻量。

## 测试要求

现有 `gtest_file_cache_utils.cpp` 的用例直接迁移：

```text
roundDown(0, 0) == 0
roundDown(123, 0) == 123
roundDown around 8-byte boundaries

roundUp(0, 0) == 0
roundUp(123, 0) == 123
roundUp around 8-byte boundaries

roundUp(2, SIZE_MAX) == SIZE_MAX
roundUp(SIZE_MAX, SIZE_MAX) == SIZE_MAX
roundUp(SIZE_MAX - 1, SIZE_MAX) == SIZE_MAX
roundUp(SIZE_MAX, 1) == SIZE_MAX

roundUp(SIZE_MAX, SIZE_MAX - 1) throws std::overflow_error
roundUp(SIZE_MAX - 1, 4) throws std::overflow_error
```

增加 compile-time/include test：

```text
FileCacheUtils.h compiles without CH base headers
```

## Review 状态

`FileCacheUtils.h` 已按文件 review。两个 arithmetic helpers可近乎逐行 exact port；唯一
实现替换是 `common::addOverflow` 到 `__builtin_add_overflow` 和 target namespace。
