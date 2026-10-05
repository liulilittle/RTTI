# RTTI — C++17 运行时类型信息库

[English](README.en.md) | English edition: [README.en.md](README.en.md)

RTTI 是一个零依赖、可移植(MSVC / GCC / Clang)、基于 C++17 的运行时反射库,
弥补 C++ 缺少一等反射的短板。过去需要大量样板代码的工作——遍历字段、
(反)序列化、运行时自省——现在只需一个宏。

## 目录

- [简介](#简介)
- [快速上手](#快速上手)
- [架构](#架构)
- [支持的容器](#支持的容器)
- [可移植性](#可移植性)
- [文档](#文档)

## 简介

标准 C++11 的 `typeid` 只能给出一个 mangled 名称。RTTI 在它之上构建了实用的
字段反射层:

- **注册**结构体使用 `RTTI_DEFINE_TYPE` 宏族(外部 / 侵入式 / 带默认键变体),
  枚举使用 `RTTI_DEFINE_ENUM`。
- **自省**在运行时:字段数量、名称、类型(.NET 级体验的 `rtti::get_type`、
  `find_field`、`get_field`、`set_field`,以及字段的 range-for 遍历)。
- **转换**在结构体与格式无关的 `value_io` 树之间递归进行(`from_value` /
  `to_value`),覆盖标量、`std::string`,以及 `std::map`/`unordered_map`/
  `set`/`unordered_set`/`list`/`vector`——并透传 allocator 与比较器。
- **解耦**格式:反射核心绝不 include 任何格式头;JSON/XML/INI 适配器后续接入,
  并且始终是可选的编译门。

整个库是"头文件逻辑 + 极简 src/ 支撑",无第三方依赖,无非标准编译器内置函数。

## 快速上手

定义一个结构体并注册——仅此而已:

```cpp
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include <string>

struct Person {
    std::string name;
    int age = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(Person, name, age);   // 在类体内注册
};
```

自省与转换:

```cpp
rtti::get_type<Person>().find_field("age");          // const field_access_base*

Person p{"Ada", 36};
rtti::field_value v;
rtti::get_field(p, "age", v);                        // 名称未知时返回 false

// 通过 value_io 测试双递归转换:
rtti_test_lib::box* src = rtti_test_lib::make_obj(); // 任意 value_io 实现
auto e = rtti::to_value(src, p);                     // error_code
```

嵌套结构体、容器、枚举与自定义 ADL 转换都流经同一对入口。非侵入式注册写在
命名空间作用域:

```cpp
RTTI_DEFINE_TYPE(Person, name, age)                  // 类外
```

缺失键默认是严格行为(报错);`_WITH_DEFAULT` 宏改为保留成员的既有值。

## 架构

三个协作层,依赖方向严格单向——适配器只能 `#include` 核心,绝不允许反向。

| 层 | 头文件 | 职责 |
| --- | --- | --- |
| 反射核心 | `reflection.hpp` `reflect.hpp` `convert.hpp` `memory.hpp` `error.hpp` `rtti_fwd.hpp` | 字段表、宏、查询 API、递归转换、arena、result 类型 |
| `value_io` 抽象 | `convert.hpp` | 格式无关的树接口(`kind`、`at`、`get_key`、`make_scalar`、…),由各适配器实现 |
| 格式适配器 | `json.hpp` `xml.hpp` `ini.hpp`(门 stub) | JSON/XML/INI(推迟到后续阶段) |

`fields_of<T>` 持有编译期字段表(count + 类型化访问器数组)。`RTTI_DEFINE_TYPE`
宏特化它;侵入式变体在类内注册嵌套的 `rtti_registration`,`fields_of` 继承之。
运行时查询 API 与递归转换都精确遍历这张表,所以注册是唯一事实来源。

`RTTI_IS_EMPTY` 空参检测与有界的 `RTTI_NARG` 计数采用两级间接粘合,在 MSVC
标准预处理器(`/Zc:preprocessor`)下依然正确。

## 支持的容器

| 家族 | 识别依据 | 备注 |
| --- | --- | --- |
| `std::vector<T, A>` | `push_back`、`data`、sequence | allocator `A` 原样透传 |
| `std::list<T, A>` | `push_back`、sequence | |
| `std::map<K,V,C,A>` / `std::unordered_map<...>` | `mapped_type` | |
| `std::set<T,C,A>` / `std::unordered_set<...>` | associative 且无 `mapped_type` | |

`std::string` 被显式排除出序列处理(它同时有 `push_back` 与迭代器),改走专用
字符串转换器。自定义 allocator 与比较器作为模板参数直接携带,无需额外接线。
家族之外的类型按此顺序分派:用户 ADL 钩子 → `is_reflected`(遍历其字段)→
标量回退。

## 可移植性

- **仅 C++17** —— 不需要任何 C++20 特性。
- 编译器:MSVC(需 `/Zc:preprocessor`)以及 GCC/Clang。
- 无 `__declspec`、无 GNU 扩展、无编译器私有内置函数。
- 计时用 `std::chrono::steady_clock`,RAII 机制,仅用标准容器。
- 反射核心在三个格式门全部关闭(`-DRTTI_ENABLE_* = OFF`)时仍可编译,
  剥离构建通过完整测试套件。

## 文档

- [English](README.en.md) —— 英文版。
- [简体中文](README.zh-CN.md) —— 本文。

两个版本章节结构一一对应,并互相链接;上述每个章节在另一版本中都有
匹配章节。