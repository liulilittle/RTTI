# `rtti::reflection.hpp` — 反射核心 API 参考(中文版)

- **模块**:反射核心(反射引擎)
- **头文件**:`include/rtti/reflection.hpp`
- **包含依赖**:
  - 系统/STL:`<charconv>`、`<cstddef>`、`<cstdint>`、`<cstdlib>`、`<cstring>`、`<functional>`、`<initializer_list>`、`<limits>`、`<mutex>`、`<optional>`、`<string>`、`<tuple>`、`<type_traits>`、`<variant>`、`<typeinfo>`、`<utility>`、`<vector>`
  - 项目:`<rtti/memory.hpp>`(提供 `rtti::string_view`)、`<rtti/naming.hpp>`(提供 `rtti::detail::naming_policy`、`key_case`、`apply_naming`)
  - 尾部包含:`<rtti/convert.hpp>`(格式无关转换器 `from_value` / `to_value` / `from_value_named` / `to_value_named` / `member_to_value` / `member_to_value_named`,置于文件末尾以打破包含环——见 §26)
  - 传递:`<rtti/error.hpp>`(经 `memory.hpp`,定义 `rtti::error_code`、`rtti::result<T>`、`rtti::error_info`)
- **命名空间**:`rtti`(注册宏在 `namespace rtti { ... }` 内部以命名空间作用域展开)
- **语言/标准**:C++17,英文内部注释,无第三方依赖
- **职责**:本头是**反射核心**。实现编译期字段表与运行期可迭代字段访问、运行期类型描述系统、枚举反射、属性、模板实参反射、派生类型基类合并、反射构造、C# 风格 `as`/`cast` 转换、`object_ref` / `instance` 句柄、方法反射与完整注册宏族。格式适配器(JSON/XML/INI/TOML/proto/flat)通过 `value_io` 抽象桥在其上构建。

> 本文档所有签名逐字抄自头文件。行为说明与行号对应 `include/rtti/reflection.hpp`。
> English edition: [`docs/interface/en/reflection.md`](../en/reflection.md)

## 目录

1. 容器形状 trait(类型谓词)
2. 注册核心:`fields_of`、`base_types`、`is_reflected`
3. 类型化标识与标量值:`type_id`、`field_value`、`field_value_to`、`value_io`
4. 字段访问模型:`field_access_base`、`typed_field_access`
5. 反射字段查询:`field_count`、`fields`、`for_each_field`、`has_default`
6. 属性 / 注解系统
7. 枚举反射
8. 类型描述系统:`type_kind`、`container_family`、`type_desc` 族、`type_of`
9. 编译期类型判定
10. 家族判别与枚举辅助
11. 模板实参反射
12. 派生类型合并机制(concepts)
13. 反射构造:`ctor_arg`、`ctor_param`、`ctor_registry`
14. 类型转换:`as` / `cast`
15. `object_ref<T>` 实例句柄
16. `instance` 类型擦除句柄
17. 方法反射
18. 宏设施(`RTTI_NARG`、`RTTI_APPLY`,…)
19. 字段注册辅助宏
20. 类型注册宏(`RTTI_DEFINE_TYPE*`)
21. 枚举注册宏
22. 模板信息注册宏
23. 属性注册宏
24. 构造器注册宏
25. 方法注册宏
26. 头结构与会 include 环说明
27. 其它语言

---

## 1. 容器形状 trait(类型谓词)

这些编译期谓词按类型**形状**(成员检测)分类,与任何格式无关。自定义分配器、比较器与哈希器原样流过,因为匹配基于所需成员签名而非 std 特化。

### 1.1 `is_char_string<T>`

```cpp
template <typename T>
struct is_char_string : std::false_type {};
template <typename Char, typename Traits, typename Alloc>
struct is_char_string<std::basic_string<Char, Traits, Alloc>> : std::true_type {};
```

对任意 `std::basic_string` 特化(任何字符类型、traits、分配器)为真。字符串被排除出*序列*族,因为转换上它是标量(文本)类型。

### 1.2 `is_sequence<T>`

```cpp
template <typename T, typename = void>
struct is_sequence : std::false_type {};
template <typename T>
struct is_sequence<
    T, std::enable_if_t<!is_char_string<T>::value,
                        std::void_t<typename T::value_type,
                                    decltype(std::declval<T&>().push_back(
                                        std::declval<typename T::value_type>())),
                                    decltype(std::declval<T&>().begin()),
                                    decltype(std::declval<T&>().end())>>>
    : std::true_type {};
```

当 `T` 有 `push_back(value_type)` 加 `begin()`/`end()` 时为真 —— vector/list/deque 族。`std::basic_string` 经 `is_char_string` 排除。

### 1.3 `is_associative<T>`

```cpp
template <typename T, typename = void>
struct is_associative : std::false_type {};
template <typename T>
struct is_associative<T, std::void_t<typename T::key_type,
                                     typename T::value_type,
                                     decltype(std::declval<T&>().insert(
                                         std::declval<typename T::value_type>()))>>
    : std::true_type {};
```

当 `T` 有 `key_type` 与 `insert(value_type)` 时为真 —— map 与 set 两族都满足。

### 1.4 `is_map<T>`

```cpp
template <typename T, typename = void>
struct is_map : std::false_type {};
template <typename T>
struct is_map<T, std::void_t<typename T::mapped_type, typename T::key_type>>
    : std::true_type {};
```

map 族额外暴露 `mapped_type`(其 `value_type` 是 `std::pair`)。

### 1.5 `is_set<T>`

```cpp
template <typename T>
struct is_set : std::bool_constant<is_associative<T>::value && !is_map<T>::value> {};
```

set 族是关联的但没有 `mapped_type`。

### 1.6 `is_queue<T>`

```cpp
template <typename T, typename = void>
struct is_queue : std::false_type {};
template <typename T>
struct is_queue<T, std::void_t<typename T::value_type,
                               decltype(std::declval<T&>().push(
                                   std::declval<typename T::value_type>())),
                               decltype(std::declval<T&>().pop()),
                               decltype(std::declval<T&>().front()),
                               decltype(std::declval<T&>().back())>>
    : std::true_type {};
```

当 `T` 有 `push`/`pop`/`front`/`back` 时为真 —— `std::queue` 形状。

### 1.7 `is_stack<T>`

```cpp
template <typename T, typename = void>
struct is_stack : std::false_type {};
template <typename T>
struct is_stack<T, std::void_t<typename T::value_type,
                               decltype(std::declval<T&>().push(
                                   std::declval<typename T::value_type>())),
                               decltype(std::declval<T&>().pop()),
                               decltype(std::declval<T&>().top())>>
    : std::true_type {};
```

当 `T` 有 `push`/`pop`/`top` 且**无** `front`/`back` 时为真 —— `std::stack` 形状。

### 1.8 `is_priority_queue<T>`

```cpp
template <typename T, typename = void>
struct is_priority_queue : std::false_type {};
template <typename T>
struct is_priority_queue<
    T, std::void_t<typename T::value_type, typename T::value_compare,
                   decltype(std::declval<T&>().push(
                       std::declval<typename T::value_type>())),
                   decltype(std::declval<T&>().pop()),
                   decltype(std::declval<T&>().top())>>
    : std::true_type {};
```

当 `T` 有 `push`/`pop`/`top` 加 `value_compare` 成员类型时为真 —— `std::priority_queue` 形状。

### 1.9 `has_push_back<T>`

```cpp
template <typename T, typename = void>
struct has_push_back : std::false_type {};
template <typename T>
struct has_push_back<T, std::void_t<decltype(std::declval<T&>().push_back(
    std::declval<typename T::value_type>()))>> : std::true_type {};
```

当 `T` 有 `push_back(value_type)` 时为真:vector、deque、list、`basic_string`。

### 1.10 `has_data<T>`(前向声明)

```cpp
template <typename T, typename = void>
struct has_data;
```

此处前向声明;定义在 §10(`has_data<T, std::void_t<decltype(std::declval<T&>().data())>>`)。`is_tuple` 用该前向声明区分 `std::array` 与 tuple。

### 1.11 `is_optional<T>`

```cpp
template <typename T, typename = void>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<T, std::void_t<typename T::value_type>> : std::bool_constant<
    std::is_same_v<T, std::optional<typename T::value_type>>> {};
```

当 `T` 是 `std::optional<U>` 时为真(对照 `T` 自己的 `value_type` 检查)。

### 1.12 `is_variant<T>`

```cpp
template <typename T, typename = void>
struct is_variant : std::false_type {};
template <typename T>
struct is_variant<T, std::void_t<typename std::variant_size<T>::type>>
    : std::true_type {};
```

当 `T` 暴露 `std::variant_size` 时为真 —— 任意 `std::variant` 特化。

### 1.13 `is_tuple<T>`

```cpp
template <typename T, typename = void>
struct is_tuple : std::false_type {};
template <typename T>
struct is_tuple<T, std::void_t<typename std::tuple_size<T>::type>>
    : std::bool_constant<!has_data<T>::value> {};
```

当 `T` 暴露 `std::tuple_size` 但**不是** `std::array` 时为真(array 也特化 `tuple_size`,故经 `has_data` 排除;数组连续,tuple 不连续)。

### 1.14 `is_pair<T>`

```cpp
template <typename T, typename = void>
struct is_pair : std::false_type {};
template <typename T>
struct is_pair<T, std::void_t<decltype(std::declval<T&>().first),
                              decltype(std::declval<T&>().second)>>
    : std::true_type {};
```

当 `T` 有 `first`/`second` 成员时为真 —— `std::pair`。

### 1.15 `is_fixed_sequence<T>`

```cpp
template <typename T, typename = void>
struct is_fixed_sequence : std::false_type {};
template <typename T>
struct is_fixed_sequence<T, std::enable_if_t<
    !is_char_string<T>::value && !has_push_back<T>::value &&
        !is_associative<T>::value,
    std::void_t<typename T::value_type,
                decltype(std::declval<T&>().begin()),
                decltype(std::declval<T&>().end())>>> : std::true_type {};
```

定长序列:`value_type` + `begin`/`end` 但无 `push_back` —— `std::array`。`basic_string` 经 `is_char_string` 排除;optional/variant/tuple 无 `begin`/`end`;关联容器排除,使定长序列转换器绝不对 `std::map` 尝试 `out[i]`。

### 1.16 `is_c_array<T>`

```cpp
template <typename T>
struct is_c_array
    : std::bool_constant<
          std::is_array<T>::value &&
          !std::is_same<std::remove_extent_t<T>, char>::value> {};
```

对原生 C 数组(`T[N]`、`T[N][M]`,…)为真。`char[N]` 排除:C `char` 数组是定长字符串目标,不是数值序列(spec §12.2)。

### 1.17 `is_md_array<T>`

```cpp
template <typename T>
struct is_md_array
    : std::bool_constant<
          is_c_array<T>::value && is_c_array<std::remove_extent_t<T>>::value> {};
```

对多维原生数组(`T[N][M]`、`T[A][B][C]`,…)为真。一维 `T[N]` 不是。

### 1.18 `is_reflected<T>`

```cpp
template <typename T, typename = void>
struct is_reflected : std::false_type {};
template <typename T>
struct is_reflected<T, std::void_t<decltype(fields_of<T>::count)>>
    : std::true_type {};
```

当 `T` 参与反射时为真 —— 经侵入式嵌套 `rtti_registration` 或非侵入式 `fields_of<T>` 特化暴露静态 `count`。

### 1.19 `key_traits<K>`

```cpp
template <typename K, typename = void>
struct key_traits {
    static void assign(K& out, string_view key) {
        out = K(key.data(), key.size());
    }
    static string_view view(const K& key) { return string_view(key); }
};

template <>
struct key_traits<std::string> {
    static void assign(std::string& out, string_view key) {
        out.assign(key.data(), key.size());
    }
    static string_view view(const std::string& s) noexcept {
        return string_view(s.data(), s.size());
    }
};
```

关联容器的键处理策略。通用回退支持任何可从 `string_view` 构造的键;`std::string` 有专门特化。`assign` 把运行期键复制进 `K`;`view` 把存键当作 `string_view` 呈现。

> 本章 API 提取自 `reflection.hpp:75-264`。

---

## 2. 注册核心:`fields_of`、`base_types`、`is_reflected`

### 2.1 `fields_of<T>`

```cpp
template <typename T, typename = void>
struct fields_of;
```

反射类型注册表。一个类型参与反射,当且仅当此模板被特化(由 `RTTI_DEFINE_TYPE` 宏族或手写)。特化暴露:

- `static constexpr std::size_t count` — 反射字段数。
- `using members_type = std::tuple<...>` — 类型**自身**字段的类型化成员指针。
- `static inline const members_type members` — 类型自身字段的成员指针(声明序)。
- `static inline const field_access_base* const array[N]` — 运行期可迭代的字段访问器数组(仅前 `count` 项有效)。
- `static inline const field_type_desc desc[N]` — 编译期字段描述符(name + 按需类型解析器)。

宏生成的特化体见 §20。手写特化支持同组成员。

### 2.2 侵入式拾取特化

```cpp
// 侵入式注册:宏在 T 内部放置嵌套 rtti_registration 结构体;
// fields_of 以继承方式拾取它(在类体内无法写显式模板特化)。
template <typename T>
struct fields_of<T, std::void_t<typename T::rtti_registration>>
    : T::rtti_registration {};
```

任何暴露嵌套 `rtti_registration` 结构体(由 `RTTI_DEFINE_TYPE_INTRUSIVE*` 创建)的类型被自动反射:`fields_of<T>` 继承该结构体,故其 `count`/`members`/`array`/`desc`/`members_type` 被拾取。这适用于显式模板特化非法的类作用域。

### 2.3 `base_types<T>`

```cpp
template <typename T, typename = void>
struct base_types {
    using list = std::tuple<>;
};
```

声明反射类型的直接基类。`RTTI_DEFINE_TYPE_DERIVED` 特化它以记录基类——其字段合并进派生类型字段表,其 `type_id` 出现在运行期基类链。主模板是空表(无反射基类)。

### 2.4 `is_reflected<T>`

见 §1.18。

> 本章 API 提取自 `reflection.hpp:61-73` 与 `reflection.hpp:807-812`。

---

## 3. 类型化标识与标量值

### 3.1 `type_id`

```cpp
// 运行期类型标识:一个 std::type_info 的地址。
using type_id = const std::type_info*;
```

运行期类型标识是 `std::type_info` 的地址。`typeid(T)` 左值有静态存储期,故解引 `std::type_info` 对象比较是规范恒等测试。

### 3.2 `field_value`

可容纳一个反射字段标量负载的类变体值。

```cpp
class field_value {
public:
    enum class kind { none, boolean, integer, real, string, object };

    field_value() noexcept : k_(kind::none) {}

    static field_value of(bool v) noexcept;               // kind::boolean
    static field_value of(int64_t v) noexcept;            // kind::integer
    static field_value of(double v) noexcept;             // kind::real
    static field_value of(string_view v) noexcept;        // kind::string
    static field_value of_object() noexcept;              // kind::object

    kind type() const noexcept;
    bool is_none() const noexcept;
    bool is_integer() const noexcept;

    // 访问器;存储类别不同时各返回回退值。
    bool as_bool() const noexcept;
    int64_t as_integer() const noexcept;
    double as_real() const noexcept;
    string_view as_string() const noexcept;
};
```

| 成员 | 含义 |
|---|---|
| `of(...)` 工厂 | 构造匹配类别的 `field_value`。 |
| `of_object()` | 标记嵌套对象/不支持的成员;无负载。 |
| `type()` | 存储的 `kind`。 |
| `is_none()` / `is_integer()` | 类别检查。 |
| `as_bool()` / `as_integer()` / `as_real()` / `as_string()` | 类别检查的访问器;类别不匹配时各返回零/空回退(`false`/`0`/`0.0`/空视图),绝不转换。 |

示例:

```cpp
field_value v = field_value::of(int64_t(42));
assert(v.is_integer());
assert(v.as_integer() == 42);
assert(v.as_string().empty());   // 类别不匹配 -> 空回退
```

### 3.3 `field_value_to<M>(out, v)`

```cpp
// 把 field_value 转成类型化目标。类别不匹配返回 false。
// 单模板编译期分派(无重载集,故 MSVC 不会对 int 类成员类型报歧义)。
template <typename M>
inline bool field_value_to(M& out, const field_value& v) noexcept;
```

对目标标量类别编译期分派:

- `M == bool` → 要求 `kind::boolean`。
- `M` 整型 → 要求 `kind::integer`;值为 `static_cast<M>(v.as_integer())`。
- `M` 浮点 → 要求 `kind::real`;值为 `static_cast<M>(v.as_real())`。
- `M == string_view` → 要求 `kind::string`。
- `M == std::string` → 要求 `kind::string`;经 `assign` 复制视图。
- 其它 → 返回 `false`。

成功返回 `true`,类别不匹配返回 `false`(目标保持不动)。

### 3.4 `value_io`

反射核心与任何存储模型(解析树、DOM、构建器)之间的抽象、格式无关源/汇桥。类别镜像 JSON 风格值范畴。定义在此(而非 `convert.hpp`),因为 `field_access_base` 的运行期容器操作要调用它;`convert.hpp` 经 `#include` 使用。

```cpp
class value_io {
public:
    enum class kind { null, boolean, integer, real, string, array, object };

    virtual ~value_io() = default;

    virtual kind type() const noexcept = 0;

    // 读侧(源)。每个在类别不匹配时返回 false。
    virtual bool as_bool(bool& out) const noexcept = 0;
    virtual bool as_integer(int64_t& out) const noexcept = 0;
    virtual bool as_real(double& out) const noexcept = 0;
    virtual bool as_string(string_view& out) const noexcept = 0;

    // 容器读侧。
    virtual std::size_t size() const noexcept = 0;
    virtual value_io* at(std::size_t index) = 0;                 // 数组元素
    virtual value_io* at(std::size_t index) const = 0;
    virtual bool get_key(string_view key, value_io*& out) = 0;   // 对象查找
    virtual bool get_key(string_view key, value_io*& out) const = 0;
    virtual bool get_key_at(std::size_t index, string_view& key,
                            value_io*& out) const = 0;

    // 写侧(汇)。make_* 返回节点自身以支持链式。
    virtual value_io* make_array() = 0;
    virtual void push_back(value_io* v) = 0;
    virtual value_io* make_object() = 0;
    virtual void set_key(string_view key, value_io* v) = 0;
    virtual value_io* make_scalar(field_value v) = 0;
    virtual value_io* make_node() = 0;
};
```

| 组 | 成员 | 语义 |
|---|---|---|
| 类型 | `type()` | 节点值类别。 |
| 读标量 | `as_bool/as_integer/as_real/as_string(out)` | 解入 `out`;类别不匹配返回 `false`。 |
| 读容器 | `size()` | 数组/对象元素数。 |
| | `at(index)` | 索引元素访问(数组元素);`const` 与非 `const` 重载。 |
| | `get_key(key, out)` | 按键对象查找;`const` 与非 `const` 重载。 |
| | `get_key_at(index, key, out)` | 枚举对象第 `index` 个键值对;越界或非对象返回 `false`。 |
| 写 | `make_array()` / `make_object()` / `make_scalar(v)` | 建节点;返回节点供链式。 |
| | `push_back(v)` / `set_key(key, v)` | 追加/赋值子节点。 |
| | `make_node()` | 创建由同一棵树拥有的独立子节点(核心容器转换器用它建元素/值节点)。 |

本头声明的公开转换器入口(定义于 `convert.hpp`):

```cpp
template <typename T>
error_code from_value(const value_io* src, T& out);
template <typename T>
error_code to_value(value_io* dst, const T& in);
template <typename T>
error_code from_value_named(const value_io* src, T& out,
                            const rtti::detail::naming_policy& np);
template <typename T>
error_code to_value_named(value_io* dst, const T& in,
                          const rtti::detail::naming_policy& np);
template <typename M>
error_code member_to_value(value_io* dst, const M& v);
template <typename M>
error_code member_to_value_named(value_io* dst, const M& v,
                                 const rtti::detail::naming_policy& np);
```

> 本章 API 提取自 `reflection.hpp:266-405`(声明于 `reflection.hpp:35-59`)。

---

## 4. 字段访问模型

### 4.1 `field_access_base`

一个反射成员的统一、运行期可迭代视图。派生的 `typed_field_access<T, M>` 供应具体读写。`value_io*` 重载桥接到嵌套/容器成员的格式无关转换器(两阶段查找使核心头免于格式依赖)。

```cpp
struct field_access_base {
    const char* name;
    type_id type;

    field_access_base(const char* n, type_id t) noexcept : name(n), type(t) {}

    virtual field_value get(const void* obj) const = 0;
    virtual bool set(void* obj, const field_value& v) const = 0;

    // 格式无关读:把本成员写入汇 `dst`。
    virtual error_code get_value(const void* obj, value_io* dst) const = 0;
    // 格式无关写:从源 `src` 读本成员。
    virtual error_code set_value(void* obj, const value_io* src) const = 0;

    // 命名转换读写:如 get_value/set_value 但经 to_value_named /
    // from_value_named 路由,使命名策略递归应用到嵌套反射结构体
    // (直接结构体成员与容器元素)。基类默认回退到普通访问器。
    virtual error_code get_value_named(const void* obj, value_io* dst,
                                       const rtti::detail::naming_policy& np) const;
    virtual error_code set_value_named(void* obj, const value_io* src,
                                       const rtti::detail::naming_policy& np) const;

    // 当成员值为空等价(空 optional 或 null 指针)时为真。skip_empty 用它
    // 丢弃 null 字段。默认 false。
    virtual bool is_null_value(const void* obj) const;

    // 当成员是受支持容器族之一时为真。
    virtual bool is_container() const = 0;
};
```

容器操作(运行时):`field_add`、`field_add_scalar`、`erase_at`、`erase_key`、`clear`、`size`、`element_visitor` 等——见头文件 `reflection.hpp:452-475` 起。

### 4.2 `typed_field_access<T, M>`

模板化的具体访问器:把 `field_access_base` 的虚方法绑定到一个类型化成员指针 `M T::*`,实现 `get`/`set`(经 `field_value_to`)与 `get_value`/`set_value`(经 `converter` 链)、命名变体与容器备忘。用户通常不直接实例化——`RTTI_FIELD_DECL` 等宏生成之。

---

## 5. 反射字段查询

```cpp
// 编译期字段数。未注册 T 为硬错误("incomplete type 'fields_of<T>'").
template <typename T>
constexpr std::size_t field_count() noexcept {
    return fields_of<T>::count;
}

// 可运行期迭代的字段访问器数组。
template <typename T>
const field_access_base* const* fields() noexcept {
    return fields_of<T>::array;
}
```

### 5.3 `for_each_field<T, Fn>(fn)`(类型级)

```cpp
template <typename T, typename Fn>
void for_each_field(Fn&& fn);
```

按表序访问每个反射字段(合并基类字段在前)。回调 `fn(const char* name, type_id type)`。零分配。

### 5.4 `for_each_field<T, Fn>(obj, fn)`(实例级)

```cpp
template <typename T, typename Fn>
void for_each_field(T& obj, Fn&& fn);
```

访问具体对象的每个字段并给出当前值。回调 `fn(const char* name, type_id type, const field_value& value)`。

### 5.5 `has_default<T>`

```cpp
template <typename T, typename = void>
struct has_default : std::false_type {};
template <typename T>
struct has_default<T, std::void_t<typename T::rtti_has_default>>
    : std::true_type {};
```

当类型以 `_WITH_DEFAULT` 宏注册时为真,表示缺键保留成员现值(格式填充器使用)。

### 5.6 成员访问级别 — `member_access`、`field_access_of<T>(index)`

每个反射字段暴露其编译期访问级别:

```cpp
enum class member_access : uint8_t { public_ = 1, protected_ = 2, private_ = 3 };

template <typename T>
constexpr member_access field_access_of(std::size_t index) noexcept {
    return fields_of<T>::desc[index].access;
}
```

- `member_access`(`reflection.hpp:1399`)是反射字段的访问级别;尾下划线命名避开
  `public`/`protected`/`private` 关键字,1/2/3 取值 ABI 稳定。
- `field_access_of<T>(i)`(`reflection.hpp:3697`)返回第 `i` 个字段的访问级别——
  `constexpr`,且恒等于 `fields_of<T>::desc[i].access`。
- **非侵入式**注册(`RTTI_DEFINE_TYPE` / `RTTI_DEFINE_TYPE_DERIVED`)在命名空间
  作用域按每字段探针对(`detail::rtti_access_holder<T>`)解析访问级别;类在体内
  授予 `RTTI_FRIEND_FIELDS(T)`(`friend struct ::rtti::fields_of<T>;`)后可列出
  private/protected 字段(§20、§19.4)。
- **侵入式**注册(`RTTI_DEFINE_TYPE_INTRUSIVE`)可反射 private/protected 字段,
  但类体内无法分类访问级别,每个字段一律保守报 `private_`(文档化边界)。
- 反射出的 private/protected 字段与公开字段一样经访问器读写
  (`field_access_base::get/set`、`get_field` / `set_field`、`from_value` /
  `to_value`);只有级别分类由注册方式决定。

> 本章 API 提取自 `reflection.hpp:814-857`;`field_access_of` 于 `reflection.hpp:3697`;`member_access` 于 `reflection.hpp:1399`。

---

## 6. 属性 / 注解系统

属性把附加元数据挂到反射类型或单个字段,运行期可查(如 C# attribute)。参与方式:

- `attr_arg` — 属性参数的一个值(kind 化负载,工厂为 `attr_arg::integer` / `attr_arg::real` / `attr_arg::text` / `attr_arg::boolean`;通用转换经 `detail::make_attr_arg`)。
- `attr` — 一个属性:`name` + 参数数组。
- `field_attr_entry` / `field_attr_list` — 字段级属性条目/列表。
- `attr_table<T>` / `field_attr_table<T>` — 类型级/字段级属性表的静态注册表。
- `RTTI_DEFINE_TYPE_ATTRS(T, ...)` / `RTTI_DEFINE_FIELD_ATTRS(T, ...)` — 注册宏。
- 查询:`attr` 查找函数族(按名/索引/键),见头文件 `reflection.hpp` §6。

> 中文版以下各节与英文版结构一一对应,已含完整逐项签名、示例与提取行号(中文文档索引中文文档)。如需对照英文术语,可经每章末语言切换链接或本页头部 [`English edition`](../en/reflection.md) 查看平行英文版。

---

## 7. 枚举反射

枚举支持在本头内自足。一个类型参与枚举反射当且仅当 `rtti::enum_table<E>` 被特化(由 `RTTI_DEFINE_ENUM`)。

### 7.1 `enum_label<E>` 与 `enum_table<E>`

```cpp
template <typename E>
struct enum_label {
    const char* name;
    E value;
};

template <typename E>
struct enum_table;
```

已注册的 `enum_table<E>` 暴露 `count` 与 `array`(按注册序排列的 `enum_label<E>` 数组)。

### 7.2 `enum_to_string<E>(value)` / `enum_from_string<E>(name)`

```cpp
template <typename E>
const char* enum_to_string(E value) noexcept;

template <typename E>
E enum_from_string(string_view name) noexcept;
```

| 函数 | 语义 |
|---|---|
| `enum_to_string<E>(value)` | `value` 的标签名,无枚举器匹配时返回 `""`。 |
| `enum_from_string<E>(name)` | `name` 的枚举值,无标签匹配时返回 `static_cast<E>(0)`。 |

### 7.3 `enum_count`、`enum_name_at`、`enum_value_at`、`for_each_enum`

```cpp
template <typename E>
constexpr std::size_t enum_count() noexcept;

template <typename E>
const char* enum_name_at(std::size_t i) noexcept;

template <typename E>
E enum_value_at(std::size_t i) noexcept;

template <typename E, typename Fn>
void for_each_enum(Fn&& fn);
```

| 函数 | 语义 |
|---|---|
| `enum_count<E>()` | 已注册枚举器数(注册序)。 |
| `enum_name_at<E>(i)` | 第 `i` 个枚举器名,越界返回 `nullptr`。 |
| `enum_value_at<E>(i)` | 第 `i` 个枚举器值,越界返回 `E{}`。 |
| `for_each_enum<E>(fn)` | 按注册序对每个枚举器调用 `fn(name, value)`。 |

示例:

```cpp
enum class Color { Red, Green, Blue };
RTTI_DEFINE_ENUM(Color, Red, Green, Blue)   // namespace scope
assert(rtti::enum_count<Color>() == 3);
assert(rtti::enum_to_string<Color>(Color::Green) == std::string_view("Green"));
assert(rtti::enum_from_string<Color>("Blue") == Color::Blue);
```

---

## 8. 类型描述系统

`rtti::type_of<T>()` 返回稳定、惰性创建的 `type_desc`,描述 `T` 的类别:标量、字符串、某一容器族(含 queue/stack/priority_queue)、反射结构体(带其字段表)或枚举。嵌套元素/键与字段类型经解析器函数指针按需解析,因此自引用结构在构造期不会无限递归。

### 8.1 `type_kind` 与 `container_family`

```cpp
enum class type_kind { boolean, integer, real, string, container, reflected, enum_type };

enum class container_family {
    vector, list, map, unordered_map, set, unordered_set,
    queue, stack, priority_queue
};
```

### 8.2 解析器类型与字段/容器/结构体/枚举描述符

```cpp
struct type_desc;
struct container_desc;
struct struct_desc;
struct enum_desc;

using type_resolver = const type_desc* (*)();

// 反射字段的编译期访问级别。尾下划线命名避开 public/protected/private
// 关键字;取值 1/2/3,ABI 稳定。
enum class member_access : uint8_t { public_ = 1, protected_ = 2, private_ = 3 };

// 反射结构体的一个字段:name + 按需类型解析器 + 编译期访问级别
// (public_ / protected_ / private_)。
struct field_type_desc {
    const char* name;
    type_resolver resolve;
    member_access access = member_access::private_;
    const type_desc* type() const noexcept { return resolve(); }
};

struct container_desc {
    container_family family;
    type_resolver key;       // 仅 map 族非 null
    type_resolver element;
};

struct struct_desc {
    std::size_t field_count;
    const field_type_desc* fields;
};

// 偏移正确的基指针调整器:把指向拥有类型的指针转为指向基子对象。
using base_adjust_fn = void* (*)(void*);

// 反射类型的直接基类(spec 19):惰性解析器数组,每注册基一个,声明序。
struct base_type_list {
    std::size_t count;
    const type_resolver* resolve;   // `count` 个惰性解析器数组
    base_adjust_fn const* adjust;   // `count` 个调整器数组(并行)
};

struct enum_desc {
    std::size_t count;
    const char* const* names;
    const void* values = nullptr;   // 并行 E 值数组(原始字节)
    std::size_t value_size = 0;     // sizeof(E)
};
```

**成员访问级别。** 每个 `field_type_desc` 都在 `access` 中携带字段的编译期访问
级别(`member_access::public_` / `protected_` / `private_`,取值 1/2/3)。非侵入式
注册(`RTTI_DEFINE_TYPE` / `RTTI_DEFINE_TYPE_DERIVED`)经命名空间作用域的逐字段
探针对解析(见 §19.4),因此 public/protected/private 字段被精确分类;侵入式注册
(`RTTI_DEFINE_TYPE_INTRUSIVE`)无法在类体内分类,每个字段保守报 `private_`。经
`rtti::field_access_of<T>(index)` 或裸 `desc[i].access` 查询。

### 8.3 `type_desc`

```cpp
struct type_desc {
    type_kind kind;
    const std::type_info* id;   // typeid(T)
    const char* type_name;      // typeid(T).name()
    const container_desc* as_container;
    const struct_desc* as_reflected;
    const enum_desc* as_enum;
    const base_type_list* as_bases = nullptr;  // 直接基类(spec 19)

    bool abstract = false;
    bool polymorphic = false;

    bool is_abstract() const noexcept { return abstract; }
    bool is_polymorphic() const noexcept { return polymorphic; }
    const base_type_list* bases() const noexcept { return as_bases; }
};
```

| 成员 | 含义 |
|---|---|
| `kind` | `T` 的类别。 |
| `id` | `typeid(T)`。 |
| `type_name` | `typeid(T).name()`。 |
| `as_container` / `as_reflected` / `as_enum` | 族专属描述符,仅对匹配 `kind` 非 null。 |
| `as_bases` / `bases()` | 直接基类;无注册基时为 `nullptr`。 |
| `abstract` / `polymorphic` | 由 `type_of<T>` 建描述符时从 `std::is_abstract` / `std::is_polymorphic` 捕获的编译期分类;默认保持标量/字符串/容器/枚举类别安全(`false`)。 |
| `is_abstract()` | `T` 是抽象类(有纯虚函数,不能实例化)时为真。 |
| `is_polymorphic()` | `T` 是多态的(至少一个虚函数,即有 vtable)时为真;是运行期动态检查的前提(spec 19/20)。 |

### 8.4 `type_of<T>()` 与 `type_of(obj)`

```cpp
template <typename T>
const type_desc* type_of() noexcept;

// 值重载:rtti::type_of(obj)。
template <typename T>
const type_desc* type_of(const T&) noexcept;
```

返回 `T` 的稳定、惰性创建的 `type_desc*`。`detail::type_of_impl` 的类别映射(按序):

1. `bool` → `kind::boolean`
2. `T` 整型 → `kind::integer`
3. `T` 浮点 → `kind::real`
4. `std::string` / `string_view` → `kind::string`
5. `is_map<T>` → `kind::container`,`container_desc{map_family<T>(), key_resolver, mapped_type_resolver}`
6. `is_set<T>` → `kind::container`,`container_desc{set_family<T>(), nullptr, value_type_resolver}`
7. `is_sequence<T>` → `kind::container`,`container_desc{sequence_family<T>(), nullptr, value_type_resolver}`
8. `is_priority_queue<T>` → `kind::container` family `priority_queue`
9. `is_queue<T>` → `kind::container` family `queue`
10. `is_stack<T>` → `kind::container` family `stack`
11. `is_reflected<T>` → `kind::reflected`,`struct_desc{field_count<T>(), fields_of<T>::desc}`,加 `base_type_list_of<T>()` 与 abstract/polymorphic 标志。描述符在首次物化时一次性注册到进程级 `detail::desc_registry`,使类型擦除 `instance` 转换可解析运行期动态类型。
12. `std::is_enum<T>` → `kind::enum_type`,`enum_desc` 由 `enum_table<T>::count`、`enum_names<T>()`、`enum_values<T>()`、`sizeof(T)` 构建。
13. 否则(未知/未注册类型)→ 仅以 `kind::string` 报告为安全占位;`abstract` / `polymorphic` 类标志仍如实捕获。

返回指针对给定 `T` 稳定(函数局部静态);`const T&` 重载只是转发到 `type_of<T>()`。

示例:

```cpp
assert(rtti::type_of<int>()->kind == rtti::type_kind::integer);
struct Point { int x; int y; };
RTTI_DEFINE_TYPE(Point, x, y)              // namespace scope
const rtti::type_desc* d = rtti::type_of<Point>();
assert(d->kind == rtti::type_kind::reflected);
assert(d->as_reflected->field_count == 2);
```

### 8.5 进程级描述符注册表(detail)

内部但经 `instance` 转换路径可观察:

```cpp
inline std::vector<const type_desc*>& desc_registry();
inline std::mutex& desc_registry_mutex();
inline void register_desc(const type_desc* d) noexcept;
inline const type_desc* find_desc(const std::type_info& ti) noexcept;
```

`register_desc` 在互斥锁下跳过空指针并按 `std::type_info` 等值去重;`find_desc` 查找动态类型。用于完整精度 `instance::as` 的基链遍历重定根。

---

## 9. 编译期类型判定

标准 trait 的 `rtti` 命名空间薄别名,使调用方留在单一命名空间,镜像 `reflect.hpp` 中的 `is_same` / `is_base_of` / `is_convertible`。

```cpp
template <typename T>
struct is_abstract : std::is_abstract<T> {};
template <typename T>
inline constexpr bool is_abstract_v = is_abstract<T>::value;

template <typename T>
struct is_polymorphic : std::is_polymorphic<T> {};
template <typename T>
inline constexpr bool is_polymorphic_v = is_polymorphic<T>::value;

template <typename T>
struct has_virtual_destructor : std::has_virtual_destructor<T> {};
template <typename T>
inline constexpr bool has_virtual_destructor_v = has_virtual_destructor<T>::value;
```

| Trait | 含义 |
|---|---|
| `is_abstract<T>` / `is_abstract_v<T>` | `T` 有纯虚函数(不能实例化)时为真。 |
| `is_polymorphic<T>` / `is_polymorphic_v<T>` | `T` 至少一个虚函数(有 vtable)时为真。 |
| `has_virtual_destructor<T>` / `has_virtual_destructor_v<T>` | `T` 有虚析构时为真。 |

---

## 10. 家族判别与枚举辅助

### 10.1 `has_hasher<T>`、`has_data<T>`、`has_bracket<T>`

```cpp
template <typename T, typename = void>
struct has_hasher : std::false_type {};
template <typename T>
struct has_hasher<T, std::void_t<typename T::hasher>> : std::true_type {};

template <typename T, typename>
struct has_data : std::false_type {};
template <typename T>
struct has_data<T, std::void_t<decltype(std::declval<T&>().data())>> : std::true_type {};

template <typename T, typename = void>
struct has_bracket : std::false_type {};
template <typename T>
struct has_bracket<T, std::void_t<decltype(std::declval<T&>()[size_t(0)])>> : std::true_type {};
```

| Trait | 含义 |
|---|---|
| `has_hasher<T>` | `T` 暴露 `hasher` 成员类型(无序容器)时为真。 |
| `has_data<T>` | `T` 有 `data()` 成员(连续存储:vector、`std::array`、`std::string`,…)时为真。 |
| `has_bracket<T>` | `T` 在 `size_t` 上支持 `operator[]` 时为真。 |

### 10.2 `map_family`、`set_family`、`sequence_family`

```cpp
template <typename T>
constexpr container_family map_family() noexcept {
    return has_hasher<T>::value ? container_family::unordered_map : container_family::map;
}
template <typename T>
constexpr container_family set_family() noexcept {
    return has_hasher<T>::value ? container_family::unordered_set : container_family::set;
}
template <typename T>
constexpr container_family sequence_family() noexcept {
    if constexpr (has_data<T>::value) {
        return container_family::vector;
    } else if constexpr (has_bracket<T>::value) {
        return container_family::vector;  // deque 视为 vector 族
    } else {
        return container_family::list;
    }
}
```

- `map_family<T>()`:无序(有 `hasher`)vs 有序 map。
- `set_family<T>()`:无序 vs 有序 set。
- `sequence_family<T>()`:vector 有 `data()`;deque 有 `operator[]` 但无 `data()`(视为 vector 族);list 两者皆无 → `list`。

### 10.3 `enum_names`、`enum_values` 与辅助

```cpp
template <typename E, std::size_t... I>
const char* const* enum_name_array(std::index_sequence<I...>);

template <typename E>
const char* const* enum_names();

template <typename E, std::size_t... I>
const void* enum_value_array(std::index_sequence<I...>);

template <typename E>
const void* enum_values();
```

- `enum_name_array<E>(seq)` 为已注册枚举构建 NUL 结尾名字数组。
- `enum_names<E>()` 返回该数组(用于构建运行期 `enum_desc`)。
- `enum_value_array<E>(seq)` / `enum_values<E>()` 返回并行的原始字节值数组;`enum_values<E>()` 在枚举注册零枚举器时返回 `nullptr`。

---

## 11. 模板实参反射

C++17 无法内省任意模板实例化的类型实参,故实参列表显式注册:特化 `rtti::template_info<T>`(经 `RTTI_TEMPLATE_INFO` 宏)把实参类型存为 `std::tuple` 加其计数。未注册模板实例(与非模板类型)回退到主模板的安全空表:count 0、空 tuple、nullptr 数组。

### 11.1 `template_info<T>`

```cpp
template <typename T, typename = void>
struct template_info {
    static constexpr std::size_t count = 0;
    using args = std::tuple<>;
};
```

已注册特化额外声明 `using registered = void;`。

### 11.2 `is_template_instance<T>`

```cpp
template <typename T, typename = void>
struct is_template_instance : std::false_type {};
template <typename T>
struct is_template_instance<T, std::void_t<typename template_info<T>::registered>>
    : std::true_type {};
template <typename T>
inline constexpr bool is_template_instance_v = is_template_instance<T>::value;
```

基于标记的检测:`template_info<T>` 特化携带 `registered` 时为真。安全空主模板不视为已注册。

### 11.3 注册查询

```cpp
template <typename T>
constexpr std::size_t template_arg_count() noexcept;

template <typename T>
using template_args = typename template_info<T>::args;

template <typename T, std::size_t I>
struct template_arg_at_holder {
    static_assert(I < template_info<T>::count,
                  "RTTI: template argument index out of range");
    using type = std::tuple_element_t<I, template_args<T>>;
};
template <typename T, std::size_t I>
using template_arg_at = typename template_arg_at_holder<T, I>::type;
```

| 成员 | 含义 |
|---|---|
| `template_arg_count<T>()` | 已注册模板实参数(非模板为 0)。 |
| `template_args<T>` | 模板实参类型为 `std::tuple`(未注册为空)。 |
| `template_arg_at<T, I>` | 第 `I` 个模板实参类型;越界/未注册索引是硬编译错误(`static_assert` "RTTI: template argument index out of range")。 |

### 11.4 运行期描述符访问

```cpp
template <typename T>
const type_desc* const* template_args_of() noexcept;

template <typename T>
const type_desc* template_arg_desc_of(std::size_t index) noexcept;

template <typename T, typename Fn>
void for_each_template_arg(Fn&& fn);
```

| 函数 | 语义 |
|---|---|
| `template_args_of<T>()` | `type_desc*` 运行期数组,每已注册实参一个,声明序。未注册模板为 `nullptr`(安全空表)。 |
| `template_arg_desc_of<T>(index)` | 第 `I` 个实参的 `type_desc`,未注册/越界返回 `nullptr`。 |
| `for_each_template_arg<T>(fn)` | 按声明序对每个已注册实参调用 `fn(std::size_t index, const type_desc* arg_type)`。 |

示例:

```cpp
template <typename A, typename B> struct Box { A a; B b; };
RTTI_TEMPLATE_INFO(Box, int, std::string)     // namespace scope
assert(rtti::is_template_instance_v<Box<int, std::string>>);
assert(rtti::template_arg_count<Box<int, std::string>>() == 2);
using T0 = rtti::template_arg_at<Box<int, std::string>, 0>;  // int
```

---

## 12. 派生类型合并机制(spec 19)

宏族为每个已注册类型发射 `members` tuple(类型**自身**反射字段的类型化成员指针,声明序),与 `base_types<T>` 表并列。以 `RTTI_DEFINE_TYPE_DERIVED` 注册的派生类型把每个基类字段经 D 锚定访问器合并进其表:基成员指针转成派生成员指针(`M Base::*` → `M Derived::*`),使成员指针分派对虚基与非虚基都执行正确子对象偏移。合并的访问器/描述符数组一次性构建进函数局部静态;输入它们的基表常量初始化,故任何静态初始化序下构建都安全。

公开可观察行为:

- 派生类型的 `fields<D>()` 返回合并表,**基字段在前**,随后派生类型自身字段。
- `field_count<D>()` 等于 `base_count + own_count`。
- 派生类型的 `type_desc` 经 `as_bases`/`bases()` 携带 `base_type_list`(直接基类),并通过 `detail::register_desc` 注册,使类型擦除 `instance` 转换可遍历它。
- `is_reflected<D>()`、`is_field` 查询与 `type_of<D>()` 都在合并表上工作。

机制位于 `namespace rtti::detail`:成员指针 trait、`base_total`(基 tuple 上 `fields_of<B>::count` 之和)、`derived_accessor`(D 锚定访问器)、`derived_field_appender`/`derived_desc_appender`(先基后己)、`derived_pool<D>::accessors()/descriptors()`(魔法静态一次性构建)。

---

## 13. 反射构造

类型通过特化 `rtti::ctor_registry<T>`(通常经 `RTTI_DEFINE_CTOR` 宏,镜像显式 proto schema 特化风格)选择反射构造。`make_object<T>` / `create_instance<T>` 位于 `<rtti/reflect.hpp>`;本头供应参数描述符、运行期实参持有者、工厂胶水与实例句柄。

### 13.1 `ctor_arg`

一个运行期构造实参。值来自标量 `field_value`、`value_io` 节点(嵌套/容器/标量)或匹配已注册构造参数名的参数名。当**任一**实参带名时按名匹配参数,否则按位置;命名实参填充其参数,其余参数接收未命名(「位置」)实参,按声明序(混合模式)。

```cpp
class ctor_arg {
public:
    ctor_arg() noexcept = default;

    static ctor_arg scalar(const field_value& v) noexcept;
    static ctor_arg scalar(const char* name, const field_value& v) noexcept;
    static ctor_arg node(const value_io* v) noexcept;
    static ctor_arg node(const char* name, const value_io* v) noexcept;

    const char* name() const noexcept;
    bool named() const noexcept;
    bool is_node() const noexcept;
    bool is_scalar() const noexcept;
    const field_value& scalar() const noexcept;
    const value_io* node() const noexcept;
};
```

| 成员 | 含义 |
|---|---|
| `scalar(v)` / `scalar(name, v)` | 标量负载(可选命名)。 |
| `node(v)` / `node(name, v)` | `value_io` 源(嵌套/容器/标量),可选命名。 |
| `name()` | 参数名或 `nullptr`。 |
| `named()` | `name() != nullptr` 时为真。 |
| `is_node()` / `is_scalar()` | `node_ != nullptr` vs 标量负载。 |
| `scalar()` / `node()` | 负载访问器。 |

### 13.2 `ctor_param`

```cpp
// 一个构造器注册表参数描述符。
struct ctor_param {
    const char* name;
    type_id type;      // C++ 参数类型的 typeid
    bool optional;     // 省略实参回退到 def(或值初始化值)
    field_value def;   // 可选默认;空(kind none)-> 值初始化
};
```

### 13.3 `ctor_registry<T>` 与 `has_ctor_registry<T>`

```cpp
template <typename T, typename = void>
struct ctor_registry {
    static constexpr std::size_t count = 0;
    static inline const ctor_param params[1] = {{nullptr, nullptr, false, {}}};
    using param_types = std::tuple<>;
    using factory_fn = T* (*)(const ctor_arg*, std::size_t, error_code&);
    static inline factory_fn factory = nullptr;
};

template <typename T, typename = void>
struct has_ctor_registry : std::false_type {};
template <typename T>
struct has_ctor_registry<T, std::void_t<typename ctor_registry<T>::param_types>>
    : std::bool_constant<(ctor_registry<T>::count > 0)> {};
```

- 主 `ctor_registry<T>` 是空、安全表:`make_object<T>(args)` 对未注册类型报错。
- 特化(经 `RTTI_DEFINE_CTOR` 或手写)供应有序参数描述符(`params`)、参数类型表(`param_types` 为 `std::tuple`)与从匹配运行期实参构建 `T` 的 `factory`(`factory_fn = T*(*)(const ctor_arg*, std::size_t, error_code&)`)。
- `has_ctor_registry<T>` 在注册了非空构造表时为真。

### 13.4 运行期实参/参数匹配(detail)

内部机制(`namespace rtti::detail`)实现 spec 18.3 匹配:标量参数填充(`is_scalar_param<D>`)、`scalar_assign<D>`(bool 宽容接受整型 0/1;枚举取自整型道;其余用严格 `field_value_to` 道)、`node_to_field_value`、`arg_matcher`(计数命名/位置实参)、`assign_param<I, Tuple>`(按名或按置;省略的 optional 参数回退默认或值初始化;缺失必选参数为 `missing_key`)、`ctor_values`(未知参数名以 `kind_mismatch` 拒绝——拼写错误胜过缺参报告)、`ctor_new_impl`/`ctor_place_impl`(堆/placement 构造)、`ctor_prepare`、`ctor_factory`(宏引用的通用工厂入口)。

---

## 14. 类型转换:`as` / `cast`

C# 风格引用转换。

- `as<T>(p)`:失败返回 `nullptr`;**无错误负载**。
- `cast<T>(p)`:`result<T*>`,失败设 `kind_mismatch`,null 源设 `no_such_field`。
- 协变上转用编译器的偏移正确 `static_cast`,含虚基与多继承(见 `detail::base_adjuster`)。
- 逆变下转要求静态源类型多态(`dynamic_cast`);非多态源无法运行期检查,干净失败(`nullptr` / `kind_mismatch`)。
- 无关多态类型可经 `dynamic_cast` 交叉转成功。
- 所有入口失败安全;**`const` 限定的源或目标类型经 `static_assert` 编译期拒绝**。

```cpp
template <typename T, typename U>
T* as(U* p) noexcept {
    static_assert(!std::is_const<T>::value && !std::is_const<U>::value,
        "rtti::as/cast do not accept const-qualified source or target types");
    // p == nullptr → nullptr
    // is_base_of<T, U>    → 协变(或同型):经 detail::base_adjuster<U, T>::run 在 void* 源上 static_cast。
    // is_base_of<U, T>    → 逆变下转:is_polymorphic<U> 时 dynamic_cast<T*>(p),否则 nullptr。
    // 无关               → 两者都多态时 dynamic_cast<T*>(p),否则 nullptr。
}

template <typename T, typename U>
result<T*> cast(U* p) {
    // p == nullptr → r.err.code = no_such_field
    // q = as<T>(p); q == nullptr → r.err.code = kind_mismatch
    // else → r.value = q
}
```

| 情形 | `as<T>(p)` | `cast<T>(p)` |
|---|---|---|
| `p == nullptr` | `nullptr` | `no_such_field` |
| `is_base_of<T, U>`(上转/同型) | 偏移正确 `static_cast` | 指针 |
| `is_base_of<U, T>`(下转),`U` 多态 | `dynamic_cast<T*>(p)` | 指针或 `kind_mismatch` |
| `is_base_of<U, T>`,`U` 非多态 | `nullptr` | `kind_mismatch` |
| 无关,两者多态 | `dynamic_cast<T*>(p)` | 指针或 `kind_mismatch` |
| 无关,非同多态 | `nullptr` | `kind_mismatch` |
| `T` 或 `U` const 限定 | 编译错误(`static_assert`) | 编译错误(`static_assert`) |

示例:

```cpp
struct Base { virtual ~Base() = default; };
struct Derived : Base {};
Derived d;
Base* b = &d;
assert(rtti::as<Derived>(b) == &d);   // 下转,多态源
assert(!rtti::cast<int>(b).ok());     // kind_mismatch
```

---

## 15. `object_ref<T>` 实例句柄

反射创建的 `T` 实例句柄(spec 18.2)。两种生命周期模式:

- **RAII(默认)**:句柄拥有堆实例并自身析构时销毁(由 `make_object<T>` / `make_object<T>(args)` 创建)。
- **手动(分配器路径)**:调用方拥有裸内存(`make_object<T>` 带分配器);必须显式调用 `destroy()`。句柄析构不动手动实例,防双释放。

所有操作委托反射核心(字段表、`to_value` / `from_value`、属性、`type_of`);建句柄无需格式适配器。

```cpp
template <typename T>
class object_ref {
public:
    using value_type = T;

    object_ref() noexcept = default;
    ~object_ref() noexcept;
    object_ref(object_ref&& o) noexcept;
    object_ref& operator=(object_ref&& o) noexcept;
    object_ref(const object_ref&) = delete;
    object_ref& operator=(const object_ref&) = delete;

    T* get() noexcept;
    const T* get() const noexcept;
    T* operator->() noexcept;
    const T* operator->() const noexcept;
    T& operator*() noexcept;
    const T& operator*() const noexcept;
    bool valid() const noexcept;
    explicit operator bool() const noexcept;

    // C# 风格变体转换:见 rtti::as / rtti::cast。
    template <typename Target>
    Target* as() noexcept;
    template <typename Target>
    result<Target*> cast() noexcept;
    template <typename Target>
    Target* as() const noexcept;
    template <typename Target>
    result<Target*> cast() const noexcept;

    void destroy() noexcept;

    // --- 构造助手(高级) ---
    static object_ref adopt(T* p) noexcept;
    static object_ref placement(T* p, std::function<void(void*)> release) noexcept;

    // --- 字段 get/set(spec 18.2) ---
    error_code get(const char* name, field_value& out) const noexcept;
    error_code set(const char* name, const field_value& v) noexcept;

    // --- 序列化桥(格式无关 value_io 路径) ---
    error_code to_value(value_io* dst) const;
    error_code from_value(const value_io* src);
    error_code to_value(value_io* dst, const rtti::detail::naming_policy& np) const;
    error_code from_value(const value_io* src, const rtti::detail::naming_policy& np);

    // --- 容器操作(spec 18.2) ---
    error_code add(const char* name, const value_io* element) noexcept;
    error_code add(const char* name, const field_value& element) noexcept;
    error_code erase_at(const char* name, std::size_t index) noexcept;
    error_code erase_key(const char* name, string_view key) noexcept;
    error_code clear(const char* name) noexcept;
    std::size_t field_size(const char* name, error_code* out_err = nullptr) const noexcept;
    template <typename Fn>
    error_code for_each_element(const char* name, value_io* scratch, Fn&& fn) const noexcept;
    // 容器字段元素类型(spec 18.2 类型查询)。
    const type_desc* element_type(const char* name) const noexcept;

    // --- 属性(spec 18.2 / 第 17 节) ---
    std::size_t attr_count() const noexcept;
    bool has_attr(string_view name) const noexcept;
    const attr* find_attr(string_view name) const noexcept;
    field_attr_list field_attr(string_view name) const noexcept;
    bool field_has_attr(string_view name, string_view attribute) const noexcept;

    // --- 类型查询(spec 18.2) ---
    const type_desc* type_of() const noexcept;
};
```

| 成员 | 语义 |
|---|---|
| `valid()` / `operator bool` | `p_ != nullptr`。 |
| `get()` / `operator->` / `operator*` | 到包装实例的裸访问。 |
| `as<Target>()` / `cast<Target>()` | 包装指针上的变体转换(见 §14);返回指针借用句柄对象。 |
| `destroy()` | 手动模式销毁:运行析构并把裸内存还给分配器释放器。幂等(二次调用空操作)。RAII 模式释放拥有的实例。 |
| `adopt(p)` | 收养堆实例(RAII 模式;所有权转移给句柄)。 |
| `placement(p, release)` | 包装 placement 构造的实例(手动模式)。`release`(可选,拥有者提供)在 `destroy()` 运行析构后归还裸内存;arena/单调分配器可不传释放器。 |
| `get(name, out)` | 按名读反射字段;字段缺席 `no_such_field`。 |
| `set(name, v)` | 按名写反射字段;缺席 `no_such_field`,值类别不匹配 `kind_mismatch`。 |
| `to_value(dst)` / `from_value(src)` | 经格式无关 `value_io` 桥序列化。 |
| `to_value / from_value(dst/src, np)` | 遵守命名策略的命名转换变体。 |
| `add(name, element)` | 从 `value_io` 节点把元素追加到容器字段;缺席 `no_such_field`。 |
| `add(name, element)` | 把一个标量元素追加到容器字段(map 不支持 → `kind_mismatch`)。 |
| `erase_at(name, index)` / `erase_key(name, key)` | 从容器字段按索引/键擦除。 |
| `clear(name)` | 清空容器字段。 |
| `field_size(name, out_err)` | 容器字段元素数;给出 `out_err` 时设置之(失败回 0)。 |
| `for_each_element(name, scratch, fn)` | 访问每个元素(带调用方提供的 scratch 节点)。 |
| `element_type(name)` | 容器字段元素类型描述符,字段缺席/非容器返回 `nullptr`。 |
| `attr_count` / `has_attr` / `find_attr` / `field_attr` / `field_has_attr` | 委托 §6 类型级函数的属性查询。 |
| `type_of()` | `rtti::type_of<T>()`。 |

---

## 16. `instance` 类型擦除句柄

异构 ORM 池的类型擦除实例句柄(spec 18.2)。包装任意 `object_ref<T>` 并暴露一套小型统一操作:类型与描述符查询、按名字段 get/set、`value_io` 序列化桥。仅可移动;显式 `destroy()` 转发给被包装句柄。

```cpp
class instance {
public:
    instance() noexcept = default;
    ~instance() noexcept;
    instance(instance&& o) noexcept;
    instance& operator=(instance&& o) noexcept;
    instance(const instance&) = delete;
    instance& operator=(const instance&) = delete;

    template <typename T>
    static instance from(object_ref<T> handle);

    type_id type() const noexcept;
    const type_desc* type_of() const noexcept;
    bool valid() const noexcept;
    explicit operator bool() const noexcept;

    template <typename T>
    bool is() const noexcept;

    void* data() noexcept;
    const void* data() const noexcept;

    error_code get(const char* name, field_value& out) const noexcept;
    error_code set(const char* name, const field_value& v) noexcept;
    error_code to_value(value_io* dst) const;
    error_code from_value(const value_io* src);
    void destroy() noexcept;

    // 经类型擦除句柄的 C# 风格变体转换。
    template <typename T>
    T* as() noexcept;
    template <typename T>
    result<T*> cast() noexcept;
};
```

| 成员 | 语义 |
|---|---|
| `from<T>(handle)` | 包装类型化 `object_ref<T>` 句柄。 |
| `type()` | 被包装 `type_id`(存句柄的静态类型)。 |
| `type_of()` | 被包装类型描述符。 |
| `valid()` / `operator bool` | `obj_ != nullptr && op_ != nullptr`。 |
| `is<T>()` | 精确运行期类型相等:包装对象类型为 `T` 时为真。「is-a」关系用 `is_derived_from<T>`(reflect.hpp)。 |
| `data()` | 被包装对象指针(类型擦除),句柄有效期间有效。 |
| `get(name, out)` / `set(name, v)` | 经操作表按名字段 get/set;句柄无效 `no_such_field`。 |
| `to_value(dst)` / `from_value(src)` | `value_io` 序列化桥。 |
| `destroy()` | 手动(分配器)实例的显式销毁;RAII 句柄由被包装 `object_ref` 释放。`destroy()` 后句柄为空。 |
| `as<T>()` | 经类型擦除句柄的变体转换。完整精度:运行期动态类型(存类型多态时)解析为它的反射描述符,基链的偏移正确调整器计算目标子对象。动态描述符无法组成目标时,遍历回退到 (a) 目标正是该动态类型时的运行期动态类型精确地址,再 (b) 静态链(到达存静态类型的基)。失败返回 `nullptr`。 |
| `cast<T>()` | `as<T>()` 包进 `result<T*>`:无效/空句柄 `no_such_field`,`as` 失败 `kind_mismatch`。 |

`as<T>()` 转换行为细节:

```cpp
// 1. !valid() 或 null 包装指针 → nullptr
// 2. target = rtti::type_of<T>()(须 rtti:: 限定:类成员 type_of() 会遮蔽自由函数)
// 3. 存类型多态且其动态类型比静态类型更派生:
//      a. dyn_ptr = most_derived(polymorphic T 经 dynamic_cast<void*>)
//      b. dd = detail::find_desc(*dt);找到则
//         result = detail::adjust_to(dyn_ptr, dd, target, 0)
//      c. result == nullptr 且 target 恰为动态类型时,result = dyn_ptr
// 4. 回退:result = detail::adjust_to(static_ptr, type_of_, target, 0)
//    (组成静态链,到达存静态类型的基)
// 5. 返回 static_cast<T*>(result)
```

内部助手 `detail::same_type_id`、`detail::derived_from_desc`(运行期基链深度守卫遍历,最大深 64)、`detail::adjust_to`(从 `from` 向 `target` 组成偏移正确调整器,深度守卫)与 `detail::scalar_convertible`(上游转换器用的运行期标量可转换判定)支持这些转换。

---

## 17. 方法反射

`reflect_method<T>("Name", args...)` 分派**已注册静态**方法(无实例);`reflect_method(obj, "Name", args...)` 经其实例(`T` 对象、`object_ref<T>` 或 `instance` 句柄)分派成员方法。方法逐个宏一次注册(`RTTI_DEFINE_METHOD` / `RTTI_DEFINE_STATIC_METHOD`):每个把 `method_entry` 链入 `methods<T>()` / `method_count<T>()` 读取的按类型表。参数经 `ctor_arg` 族旅行(标量 `field_value` 或 `value_io` 节点,位置绑定);结果回标量 `field_value`,复合类型则经调用方提供的 `value_io` 节点。

错误码:未注册 → `no_such_method`;实参数/类型不匹配 → `kind_mismatch`;缺实例 → `no_such_field`。绝不抛、绝不崩溃。

### 17.1 `method_info`

```cpp
// 类型的一个反射方法(由 methods<T>() 返回)。
struct method_info {
    const char* name;
    bool is_static;
    const char* signature;
};
```

### 17.2 `reflect_method` 重载

静态方法调用:

```cpp
template <typename T>
inline result<field_value> reflect_method(const char* name, const ctor_arg* args,
                                          std::size_t count,
                                          value_io* node = nullptr) noexcept;

template <typename T>
inline result<field_value> reflect_method(const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept;
```

经对象引用的成员方法调用(`T& obj`、`object_ref<T>&` 或类型擦除 `instance&`):

```cpp
template <typename T>
inline result<field_value> reflect_method(T& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept;

template <typename T>
inline result<field_value> reflect_method(T& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept;

template <typename T>
inline result<field_value> reflect_method(object_ref<T>& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept;

template <typename T>
inline result<field_value> reflect_method(object_ref<T>& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept;

inline result<field_value> reflect_method(instance& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept;

inline result<field_value> reflect_method(instance& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept;
```

| 重载族 | 失败条件 |
|---|---|
| 静态(`reflect_method<T>`) | `name == nullptr`,或 `count > 0 && args == nullptr` → `kind_mismatch`;方法找不到 → `no_such_method`;无实例调成员方法 → `no_such_field`;调用错误透传。 |
| `T& obj` | 同静态,但走成员路径;实例指针恒在。 |
| `object_ref<T>& obj` | 无效句柄 → `no_such_field`;然后委托 `T&` 重载。 |
| `instance& obj` | 无效句柄 → `no_such_field`;`name == nullptr` 或坏 `args` → `kind_mismatch`;全局表方法找不到 → `no_such_method`。 |

结果:标量结果(`bool`、枚举、整型、浮点、`string_view`、`const char*`)落在 `result.value`(`field_value`);复合结果(容器、反射结构体、`std::string`,…)要求 `node`,经 `rtti::to_value` 写入;标量且无任何汇 → `kind_mismatch`。

### 17.3 `method_count<T>()` / `methods<T>()`

```cpp
template <typename T>
inline std::size_t method_count() noexcept;

template <typename T>
inline const method_info* methods() noexcept;
```

`method_count<T>()` 是已注册方法数;`methods<T>()` 返回反射表(空为 `nullptr`),首次使用时一次性构建。

内部记账在 `namespace rtti::detail`:`method_entry` 链表节点、`method_head<T>()`/`method_link<T>`(按类型表头/链)、`method_global_head()`/`method_global_link`(类型擦除 `instance` 查找用程序级表)、`member_traits`/`static_traits`(成员/静态方法签名分解)、`decay_tuple`(剥引用的参数 tuple)、`method_fill`(运行期实参转参数)、`scalar_field`/`result_assign`(结果投递)、`is_void_ret`、`member_apply`/`member_invoke` 与 `static_apply`/`static_invoke`(调用封装)、`find_method`/`find_method_global`、`method_table<T>`(按类型方法表)。

示例:

```cpp
struct Calc {
    static int add(int a, int b) { return a + b; }
    int mul(int a, int b) { return a * b; }
};
RTTI_DEFINE_STATIC_METHOD(Calc, add)   // namespace scope
RTTI_DEFINE_METHOD(Calc, mul)

rtti::ctor_arg args[] = {
    rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(3))),
    rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(4))),
};
auto r = rtti::reflect_method<Calc>("add", args, 2);
assert(r.ok() && r.value.as_integer() == 7);

Calc c;
auto r2 = rtti::reflect_method(c, "mul", args, 2);
assert(r2.ok() && r2.value.as_integer() == 12);
```

---

## 18. 宏设施

有界实参计数 + 平展迭代。迭代预展开且非递归;每个 `RTTI_APPLY_N` 每实参展开一次 `M`。上限(64)仅为保持宏有界;字段计数上限不是反射类型的设计限制。

### 18.1 基本助手

```cpp
#define RTTI_CAT_IMPL(a, b) a##b
#define RTTI_CAT(a, b) RTTI_CAT_IMPL(a, b)

#define RTTI_EXPAND(...) __VA_ARGS__
```

`RTTI_CAT` 是间接(两级)token 粘贴;`RTTI_EXPAND` 强制实参展开。

### 18.2 空实参检测

```cpp
#define RTTI_IBP_SPLIT(i, ...) RTTI_IBP_SPLIT_I(i, __VA_ARGS__)
#define RTTI_IBP_SPLIT_I(i, ...) RTTI_IBP_PRIMITIVE_CAT(RTTI_IBP_SPLIT_, i)(__VA_ARGS__)
#define RTTI_IBP_SPLIT_0(a, ...) a
#define RTTI_IBP_SPLIT_1(a, ...) __VA_ARGS__
#define RTTI_IBP_PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__
#define RTTI_IBP_CAT(a, ...) RTTI_IBP_PRIMITIVE_CAT(a, __VA_ARGS__)
#define RTTI_IBP_IS_VARIADIC_C(...) 1
#define RTTI_IBP_IS_VARIADIC_R_1 1,
#define RTTI_IBP_IS_VARIADIC_R_RTTI_IBP_IS_VARIADIC_C 0,
#define RTTI_IS_BEGIN_PARENS(...)                                              \
    RTTI_IBP_SPLIT(0, RTTI_IBP_CAT(RTTI_IBP_IS_VARIADIC_R_,                   \
                                   RTTI_IBP_IS_VARIADIC_C __VA_ARGS__))

#define RTTI_CAT2_II(res) res
#define RTTI_CAT2_I(a, b) RTTI_CAT2_II(a ## b)
#define RTTI_CAT2(a, b) RTTI_CAT2_I(a, b)

#define RTTI_IS_EMPTY_APPLY_1 1
#define RTTI_IS_EMPTY_APPLY_0 0

// 当 variadic 列表为空时为 1,否则 0。
#define RTTI_IS_EMPTY(...)                                                      \
    RTTI_CAT2(RTTI_IS_EMPTY_APPLY_, RTTI_IS_BEGIN_PARENS(__VA_ARGS__ ()))
```

`RTTI_IS_BEGIN_PARENS(...)` 检测 variadic 列表是否为空(Boost PP `is_begin_parens` 习语;要求合规预处理器 `/Zc:preprocessor`,反射核心总在其下编译)。`RTTI_IS_EMPTY(...)` 空为 1,否则 0。

### 18.3 实参计数

```cpp
// The real definitions enumerate all 64 counting slots (_1 .. _64) and the
// full 64..0 default-count tail; abbreviated here for readability.
#define RTTI_NARG_I(_1, _2, _3, ..., _63, _64, N, ...) \
    N
#define RTTI_NARG(...) \
    RTTI_NARG_I(__VA_ARGS__, 64, 63, ..., 1, 0)
```

计数实参 0..64。单级形式(`RTTI_NARG` → `RTTI_NARG_I`,无中间 variadic 宏)是 MSVC 合规预处理器所需。注意:**空列表目前得 1**;空 `RTTI_DEFINE_TYPE`(零字段)情形经 `RTTI_IS_EMPTY` 另行处理。超过 64 个实参为硬编译错：第 65 个 token 被当作计数，按计数的展开宏（`RTTI_APPLY_f65`、`RTTI_MEMBER_TYPES_f65` 等）未定义（C2061/C3861 类诊断）。

### 18.4 平展迭代:`RTTI_APPLY_0` .. `RTTI_APPLY_64`、`RTTI_APPLY`、`RTTI_APPLY_OPT`

```cpp
#define RTTI_APPLY_0(M, T)
#define RTTI_APPLY_1(M, T, a1) M(T, a1)
#define RTTI_APPLY_2(M, T, a1, a2) M(T, a1) M(T, a2)
// ... 直到 RTTI_APPLY_64(M, T, a1, ..., a64)

#define RTTI_APPLY(M, T, ...) \
    RTTI_EXPAND(RTTI_CAT(RTTI_APPLY_, RTTI_NARG(__VA_ARGS__))(M, T, __VA_ARGS__))

#define RTTI_APPLY_OPT_SEL_1(M, T, ...)
#define RTTI_APPLY_OPT_SEL_0(M, T, ...) RTTI_APPLY(M, T, __VA_ARGS__)
#define RTTI_APPLY_OPT(M, T, ...) \
    RTTI_CAT2(RTTI_APPLY_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(M, T, __VA_ARGS__)
```

`RTTI_APPLY(M, T, a...)` 对每个实参展开 `M(T, a1) M(T, a2) ...`(预展开、非递归)。`RTTI_APPLY_OPT` 在列表为空时展开为空。

---

## 19. 字段注册辅助宏

### 19.1 `RTTI_FIELD_DECL`、`RTTI_FIELD_REF`、`RTTI_FIELD_TYPE_DESC`

```cpp
#define RTTI_FIELD_DECL(T, f)                                                 \
    static inline const ::rtti::typed_field_access<T, decltype(T::f)> rtti_acc_##f{ \
        #f, &T::f, &typeid(decltype(T::f))};

#define RTTI_FIELD_REF(T, f) &rtti_acc_##f,

#define RTTI_FIELD_TYPE_DESC(T, f) { #f, &::rtti::type_of<decltype(T::f)> },
```

- `RTTI_FIELD_DECL` 每字段声明一个静态访问器(`typed_field_access<T, decltype(T::f)>`)。
- `RTTI_FIELD_REF` 把其地址贡献给 `array` 表。
- `RTTI_FIELD_TYPE_DESC` 给 `desc` 表贡献一个 `{name, resolver}` 条目。非侵入
  式注册宏会换成解析访问级别的变体(`RTTI_FIELD_ACCESS_VALUE`,见 §19.4),使
  `desc` 行携带 `member_access`(public_/protected_/private_,§5.6)。

### 19.4 `RTTI_FRIEND_FIELDS(T)` 与成员访问探针

```cpp
// 授予非侵入式字段表(fields_of<T>)对 T 的友元访问,以便取 private/protected
// 字段的 `&T::f`。放在类体内;与命名空间作用域的 RTTI_DEFINE_TYPE(T, ...) 搭配。
#define RTTI_FRIEND_FIELDS(T) friend struct ::rtti::fields_of<T>;
```

非侵入式注册在命名空间作用域发出按类型的 `rtti::detail::rtti_access_holder<T>`,
其逐字段探针对(`rtti_access_ext_<f>` / `rtti_access_der_<f>`,reflection.hpp:3673-3686)
经 `decltype(&U::f)` 的 SFINAE 判断字段是否可以外部访问(`ext`,→ `public_`)、仅
派生上下文可访问(`der`,→ `protected_`)、或均不可访问(→ `private_`)。该 holder
**不是** `fields_of` 的成员、也**不是** `T` 的友元,因此用于形成 `&T::f` 的
`RTTI_FRIEND_FIELDS` 授予绝不会泄漏进探针结果。

访问判定:

| 组合 | `member_access` |
| --- | --- |
| `ext` 为真 | `public_` |
| `ext` 假、`der` 真 | `protected_` |
| 二者皆假 | `private_` |

侵入式注册(`RTTI_DEFINE_TYPE_INTRUSIVE`)在类体内形成 `&T::f`,无需友元,但无法
从 `T` 内部运行命名空间作用域探针;因此每个侵入式字段都报 `private_` —— 保守的
文档化边界(与 `field_type_desc::access` 的默认值一致)。

### 19.2 `RTTI_MEMBER_TYPES_*` / `RTTI_MEMBER_TYPES_OPT`

按计数逗号连接的宏,产出 `decltype(&T::aN)` 成员指针类型表(无尾逗号;MSVC 拒绝模板实参表尾逗号)。整套覆盖计数 0..64:

```cpp
#define RTTI_MEMBER_TYPES_0(T)
#define RTTI_MEMBER_TYPES_1(T, a1) decltype(&T::a1)
#define RTTI_MEMBER_TYPES_2(T, a1, a2) decltype(&T::a1), decltype(&T::a2)
// ... 直到 RTTI_MEMBER_TYPES_64(T, a1, ..., a64)

#define RTTI_MEMBER_TYPES_OPT_SEL_1(T, ...)
#define RTTI_MEMBER_TYPES_OPT_SEL_0(T, ...) \
    RTTI_CAT(RTTI_MEMBER_TYPES_, RTTI_NARG(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_MEMBER_TYPES_OPT(T, ...) \
    RTTI_CAT2(RTTI_MEMBER_TYPES_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(T, __VA_ARGS__)
```

### 19.3 `RTTI_MEMBER_VALUES_*` / `RTTI_MEMBER_VALUES_OPT`

同样模式,产出成员指针值表(`&T::a1, &T::a2, ...`):

```cpp
#define RTTI_MEMBER_VALUES_0(T)
#define RTTI_MEMBER_VALUES_1(T, a1) &T::a1
#define RTTI_MEMBER_VALUES_2(T, a1, a2) &T::a1, &T::a2
// ... 直到 RTTI_MEMBER_VALUES_64(T, a1, ..., a64)

#define RTTI_MEMBER_VALUES_OPT_SEL_1(T, ...)
#define RTTI_MEMBER_VALUES_OPT_SEL_0(T, ...) \
    RTTI_CAT(RTTI_MEMBER_VALUES_, RTTI_NARG(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_MEMBER_VALUES_OPT(T, ...) \
    RTTI_CAT2(RTTI_MEMBER_VALUES_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(T, __VA_ARGS__)
```

这些发射一个类型**自身**字段的 `members` tuple(spec 19),派生合并机制把它转成派生类型以构建偏移正确的基访问器。

---

## 20. 类型注册宏(`RTTI_DEFINE_TYPE*`)

### 20.1 `RTTI_DEFINE_TYPE(T, ...)`

命名空间作用域的非侵入式注册(公共成员)。特化包在 `rtti` 命名空间块内,使 MSVC 接受在另一命名空间内显式特化。

```cpp
#define RTTI_DEFINE_TYPE(T, ...)                                              \
    namespace rtti {                                                          \
    template <>                                                               \
    struct fields_of<T> {                                                     \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        using members_type = std::tuple<                                      \
            RTTI_MEMBER_TYPES_OPT(T, __VA_ARGS__)>;                           \
        static inline const members_type members =                            \
            members_type{ RTTI_MEMBER_VALUES_OPT(T, __VA_ARGS__) };           \
        RTTI_APPLY_OPT(RTTI_FIELD_DECL, T, __VA_ARGS__)                       \
        static inline const field_access_base* const array[                    \
            (count > 0 ? count : 1)] = {                                      \
            RTTI_APPLY_OPT(RTTI_FIELD_REF, T, __VA_ARGS__)                    \
        };                                                                     \
        static inline const field_type_desc desc[(count > 0 ? count : 1)] = { \
            RTTI_APPLY_OPT(RTTI_FIELD_TYPE_DESC, T, __VA_ARGS__)              \
        };                                                                     \
    };                                                                          \
    }
```

**参数**:`T` — 反射类型;`...` — 公共成员名,声明序(至多 64;空列表合法,得 `count == 0`)。

**效果**:以 `count`、`members_type`、`members`、访问器 `array` 与 `desc` 表特化 `rtti::fields_of<T>`。此后 `is_reflected<T>`、`field_count<T>`、`fields<T>`、`for_each_field`、`type_of<T>()` 等都可用。

**私有 / 受保护字段**:`...` 可列出 private/protected 成员,前提是类在体内授予
`RTTI_FRIEND_FIELDS(T)`(见 §19.4)。该宏还会在命名空间作用域发出逐字段的
`detail::rtti_access_holder<T>` 探针对,使每个 `field_type_desc` 行的 `access`
成员被精确解析(`member_access::public_` / `protected_` / `private_`)。

**private/protected 字段**:`...` 可列出 private/protected 成员——在类体内授予
`RTTI_FRIEND_FIELDS(T)`(见 §19.4)即可。该宏还会在命名空间作用域发出逐字段
`detail::rtti_access_holder<T>` 探针对,使每个 `field_type_desc` 行的 `access`
成员被精确解析(`member_access::public_` / `protected_` / `private_`)。

示例:

```cpp
struct Point { int x; int y; };
RTTI_DEFINE_TYPE(Point, x, y)     // namespace scope
assert(rtti::field_count<Point>() == 2);
```

### 20.2 `RTTI_DEFINE_TYPE_DERIVED(T, Base, ...)`

派生类型的非侵入式注册(spec 19)。`Base` 的反射字段合并进 `T` 字段表、基在前,经偏移正确的 D 锚定访问器(虚/非虚继承都正确),且 `Base` 的 `type_id` 记入 `T` 运行期基链。

```cpp
#define RTTI_DEFINE_TYPE_DERIVED(T, Base, ...)                                \
    namespace rtti {                                                          \
    template <>                                                               \
    struct base_types<T> {                                                    \
        using list = std::tuple<Base>;                                        \
    };                                                                         \
    }                                                                          \
    namespace rtti {                                                          \
    template <>                                                               \
    struct fields_of<T> {                                                     \
        static constexpr std::size_t base_count =                             \
            ::rtti::detail::base_total<::rtti::detail::base_list_t<T>>();     \
        static constexpr std::size_t own_count =                              \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));        \
        static constexpr std::size_t count = base_count + own_count;          \
        using members_type = std::tuple<                                      \
            RTTI_MEMBER_TYPES_OPT(T, __VA_ARGS__)>;                           \
        static inline const members_type members =                            \
            members_type{ RTTI_MEMBER_VALUES_OPT(T, __VA_ARGS__) };           \
        RTTI_APPLY_OPT(RTTI_FIELD_DECL, T, __VA_ARGS__)                       \
        static inline const field_access_base* const own_table[              \
            (own_count > 0 ? own_count : 1)] = {                              \
            RTTI_APPLY_OPT(RTTI_FIELD_REF, T, __VA_ARGS__)                    \
        };                                                                    \
        static inline const field_type_desc own_desc[                        \
            (own_count > 0 ? own_count : 1)] = {                              \
            RTTI_APPLY_OPT(RTTI_FIELD_TYPE_DESC, T, __VA_ARGS__)              \
        };                                                                    \
        static inline const field_access_base* const* const array =           \
            ::rtti::detail::derived_pool<T>::accessors();                     \
        static inline const field_type_desc* const desc =                     \
            ::rtti::detail::derived_pool<T>::descriptors();                   \
    };                                                                          \
    }
```

**参数**:`T` — 派生类型;`Base` — 单一直接注册基;`...` — `T` 自身公共成员(可空)。

**效果**:特化 `base_types<T>`(单基)与 `fields_of<T>`(合并表)。`array`/`desc` 指向一次性构建的 `detail::derived_pool<T>`。

### 20.3 `RTTI_DEFINE_TYPE_WITH_DEFAULT(T, ...)`

```cpp
#define RTTI_DEFINE_TYPE_WITH_DEFAULT(T, ...)                                 \
    RTTI_DEFINE_TYPE(T, __VA_ARGS__)                                          \
    namespace rtti {                                                          \
    template <>                                                               \
    struct has_default<T> : std::true_type {};                                \
    }
```

`RTTI_DEFINE_TYPE` 加 `has_default<T>` 特化,使格式填充器在缺键时保留成员现值。

### 20.4 `RTTI_DEFINE_TYPE_INTRUSIVE(T, ...)`(类内)

```cpp
#define RTTI_DEFINE_TYPE_INTRUSIVE(T, ...)                                    \
    struct rtti_registration {                                                \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        using members_type = std::tuple<                                      \
            RTTI_MEMBER_TYPES_OPT(T, __VA_ARGS__)>;                           \
        static inline const members_type members =                            \
            members_type{ RTTI_MEMBER_VALUES_OPT(T, __VA_ARGS__) };           \
        RTTI_APPLY_OPT(RTTI_FIELD_DECL, T, __VA_ARGS__)                       \
        static inline const ::rtti::field_access_base* const array[            \
            (count > 0 ? count : 1)] = {                                      \
            RTTI_APPLY_OPT(RTTI_FIELD_REF, T, __VA_ARGS__)                    \
        };                                                                     \
        static inline const ::rtti::field_type_desc desc[                      \
            (count > 0 ? count : 1)] = {                                      \
            RTTI_APPLY_OPT(RTTI_FIELD_TYPE_DESC, T, __VA_ARGS__)              \
        };                                                                     \
    };
```

用于类体内;`rtti_registration` 嵌套结构体经继承被 `fields_of<T>` 拾取(见 §2.2)。支持私有字段。

### 20.5 `RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(T, ...)`

```cpp
#define RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(T, ...)                       \
    using rtti_has_default = void;                                            \
    RTTI_DEFINE_TYPE_INTRUSIVE(T, __VA_ARGS__)
```

侵入式变体加 `rtti_has_default`(启用 `has_default<T>`)。

---

## 21. 枚举注册宏

### 21.1 `RTTI_ENUM_ENTRY(T, e)`

```cpp
#define RTTI_ENUM_ENTRY(T, e) ::rtti::enum_label<T>{#e, T::e},
```

一个表条目:`{ "e", T::e }`。由 `RTTI_DEFINE_ENUM` 内部使用。

### 21.2 `RTTI_DEFINE_ENUM(T, ...)`

```cpp
// 空实参守卫:`enum E {}`(零枚举器实参)注册空表(count 0,空数组)。
// RTTI_APPLY_OPT 在列表空时把枚举器表展开为空,故绝无畸形
// `RTTI_ENUM_ENTRY(T,)` token 形成。
#define RTTI_DEFINE_ENUM(T, ...)                                              \
    namespace rtti {                                                          \
    template <>                                                               \
    struct enum_table<T> {                                                    \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        static inline const enum_label<T> array[(count > 0 ? count : 1)] =    \
            { RTTI_APPLY_OPT(RTTI_ENUM_ENTRY, T, __VA_ARGS__) };              \
    };                                                                          \
    }
```

**参数**:`T` — 枚举类型;`...` — 枚举器名,注册序。**效果**:特化 `rtti::enum_table<T>` 的 `count` 与 `array`。此后 `enum_count<T>()`、`enum_to_string`、`enum_from_string`、`enum_name_at`、`enum_value_at`、`for_each_enum` 都可用。

---

## 22. 模板信息注册宏

### 22.1 实参表助手 `RTTI_TEMPLATE_ARGS_1` .. `RTTI_TEMPLATE_ARGS_64`

按计数逗号连接的宏,展开模板实参表而无尾逗号(MSVC 拒绝模板实参表尾逗号)。

### 22.2 空感知选择器与括号包裹实参

含逗号的类型-id(如 `std::pair<int,int>`)作为单实参以额外括号包裹传入:`RTTI_TEMPLATE_INFO(FnPair, (std::pair<int,int>))`。`RTTI_IS_EMPTY` 无法区分 `(...)` 与 `()`,故选择器**先**查是否以括号开头;每个元素经条件单层括号剥除器(`RTTI_TAO_UNPAREN`、`RTTI_TAO_ELEM`、按计数的 `RTTI_TAO_STRIP_1`..`RTTI_TAO_STRIP_64`、`RTTI_TAO_STRIP`)。于是 `RTTI_TEMPLATE_INFO(Fn, int, (std::pair<int,int>))` 产出 `Fn<int, std::pair<int,int>>`。

### 22.3 `RTTI_TEMPLATE_INFO(T, ...)`

```cpp
// 示例:
//   RTTI_TEMPLATE_INFO(MyBox, int, double, std::string)
// 特化 template_info<MyBox<int, double, std::string>>,含实参类型 std::tuple 与其计数。
#define RTTI_TEMPLATE_INFO(T, ...)                                            \
    namespace rtti {                                                          \
    template <>                                                               \
    struct template_info<T<RTTI_TEMPLATE_ARGS_OPT(T, __VA_ARGS__)>> {         \
        static constexpr std::size_t count =                                  \
            RTTI_TEMPLATE_ARGS_COUNT(__VA_ARGS__);                            \
        using args = std::tuple<RTTI_TEMPLATE_ARGS_OPT(T, __VA_ARGS__)>;      \
        using registered = void;                                              \
    };                                                                         \
    }
```

**参数**:`T` — 类模板;`...` — 其类型实参(至多 64;空列表允许——全默认参数的模板注册为 `T<>`,count 0 空 tuple)。含逗号实参须额外括号包裹。

**效果**:特化 `template_info<Inst>` 的 `count`、`args`(`std::tuple`)与 `registered`。此后 `is_template_instance`、`template_arg_count`、`template_args`、`template_arg_at`、`template_args_of`、`template_arg_desc_of`、`for_each_template_arg` 都可用。未注册模板实例(或非模板类型)报安全空表。

---

## 23. 属性注册宏

属性表是命名空间作用域 `rtti::attr_table<T>` / `rtti::field_attr_table<T>` 的显式特化,因此与侵入式/非侵入式字段注册宏族都组合,而不改动它们。属性可选:无注册的类型直接报 count 0 / null 指针。

每个 `RTTI_ATTR(...)` 展开为工厂调用(`make_attr`),故每个逗号都嵌套在括号内;预处理器永不误计花括号初始化表的尾逗号。

### 23.1 实参助手

```cpp
#define RTTI_ATTR_ARG_MAKE(_, a) ::rtti::detail::make_attr_arg(a),
#define RTTI_ATTR_ARGS(...) RTTI_APPLY_OPT(RTTI_ATTR_ARG_MAKE, _, __VA_ARGS__)

// 一个属性值:{ name, { args... } }。
#define RTTI_ATTR(name, ...) ::rtti::make_attr(name, {RTTI_ATTR_ARGS(__VA_ARGS__)})

#define RTTI_ATTR_REF(_, a) a,
#define RTTI_ATTR_LIST(...) RTTI_APPLY_OPT(RTTI_ATTR_REF, _, __VA_ARGS__)

// 一个字段的属性表:{ "field", { attrs... } }。
#define RTTI_FIELD_ATTR_ENTRY(f, ...) \
    ::rtti::make_field_attr(#f, {RTTI_ATTR_LIST(__VA_ARGS__)})
```

- `RTTI_ATTR(name, args...)` — 一个属性展开:`make_attr(name, {args...})`。
- `RTTI_FIELD_ATTR_ENTRY(f, attrs...)` — 一个字段属性表条目:`make_field_attr("f", {attrs...})`。

### 23.2 `RTTI_DEFINE_TYPE_ATTRS(T, ...)`

```cpp
// 命名空间作用域类型级属性表特化。空声明(无属性)合法,count 0。
#define RTTI_DEFINE_TYPE_ATTRS(T, ...)                                        \
    namespace rtti {                                                          \
    template <>                                                               \
    struct attr_table<T> {                                                    \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        static inline const attr array[(count > 0 ? count : 1)] = {            \
            RTTI_EXPAND(__VA_ARGS__)                                          \
        };                                                                     \
        using registered = void;                                              \
    };                                                                          \
    }
```

**参数**:`T` — 反射类型;`...` — `RTTI_ATTR(...)` 展开(可空)。**效果**:特化 `attr_table<T>`;启用 `attrs_defined<T>()`、`attr_count<T>()`、`attr_at`、`find_attr`、`has_attr`。

### 23.3 `RTTI_DEFINE_FIELD_ATTRS(T, ...)`

```cpp
// 命名空间作用域逐字段属性表特化。
#define RTTI_DEFINE_FIELD_ATTRS(T, ...)                                       \
    namespace rtti {                                                          \
    template <>                                                               \
    struct field_attr_table<T> {                                              \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        static inline const field_attr_entry array[                           \
            (count > 0 ? count : 1)] = { RTTI_EXPAND(__VA_ARGS__) };           \
        using registered = void;                                              \
    };                                                                          \
    }
```

**参数**:`T` — 反射类型;`...` — `RTTI_FIELD_ATTR_ENTRY(f, attrs...)` 展开(可空)。**效果**:特化 `field_attr_table<T>`;启用字段级属性查询(`field_attr`、`field_attr_count`、`field_attr_at`、`field_find_attr`、`field_has_attr`)。

示例:

```cpp
struct Point { int x; int y; };
RTTI_DEFINE_TYPE(Point, x, y)                       // namespace scope
RTTI_DEFINE_TYPE_ATTRS(Point, RTTI_ATTR("color", "red"))
RTTI_DEFINE_FIELD_ATTRS(Point,
    RTTI_FIELD_ATTR_ENTRY(x, RTTI_ATTR("unit", "mm")))
assert(rtti::attrs_defined<Point>());
assert(rtti::field_has_attr<Point>("x", "unit"));
```

---

## 24. 构造器注册宏

每个构造参数一个括号 `(name, type, optional)` 三元组。宏计数参数、构建描述符数组、参数类型表(`std::tuple`)并接线通用运行期工厂,镜像显式 proto schema 特化风格。`std::map<std::string, int>` 这类模板类型参数没问题——逗号在括号内。

### 24.1 三元组访问器

```cpp
#define RTTI_CTOR_GET_1(a, b, c) a
#define RTTI_CTOR_GET_2(a, b, c) b
#define RTTI_CTOR_GET_3(a, b, c) c
#define RTTI_CTOR_NAME(triple) RTTI_CTOR_GET_1 triple
#define RTTI_CTOR_TYPE(triple) RTTI_CTOR_GET_2 triple
#define RTTI_CTOR_OPTIONAL(triple) RTTI_CTOR_GET_3 triple

#define RTTI_CTOR_PARAM_DECL(T, triple)                                       \
    {RTTI_CTOR_NAME(triple), &typeid(RTTI_CTOR_TYPE(triple)),                 \
     RTTI_CTOR_OPTIONAL(triple), {}},
```

`RTTI_CTOR_NAME/TYPE/OPTIONAL` 解包 `(name, type, optional)` 三元组;`RTTI_CTOR_PARAM_DECL` 发射一个 `rtti::ctor_param` 初始化器。

### 24.2 参数类型逗号连接器

```cpp
#define RTTI_CTOR_TYPES_0(T)
#define RTTI_CTOR_TYPES_1(T, a1) RTTI_CTOR_TYPE(a1)
#define RTTI_CTOR_TYPES_2(T, a1, a2) RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2)
// ... 直到 RTTI_CTOR_TYPES_64(T, a1, ..., a64)
```

逗号连接的参数类型表(无尾逗号;MSVC 拒绝模板实参表内尾逗号)。

### 24.3 `RTTI_DEFINE_CTOR(T, ...)`

```cpp
// 命名空间作用域为 T 注册反射构造器。至少一个 (name, type, optional) 参数。
// 示例:
//   RTTI_DEFINE_CTOR(MyType, ("x", int, false), ("y", std::string, true))
#define RTTI_DEFINE_CTOR(T, ...)                                              \
    namespace rtti {                                                          \
    template <>                                                               \
    struct ctor_registry<T> {                                                 \
        static constexpr std::size_t count = RTTI_NARG(__VA_ARGS__);           \
        static inline const ::rtti::ctor_param params[                        \
            (count > 0 ? count : 1)] = {                                      \
            RTTI_APPLY(RTTI_CTOR_PARAM_DECL, T, __VA_ARGS__)                  \
        };                                                                     \
        using param_types = std::tuple<RTTI_CAT(RTTI_CTOR_TYPES_,            \
            RTTI_NARG(__VA_ARGS__))(T, __VA_ARGS__)>;                        \
        using factory_fn = T* (*)(const ::rtti::ctor_arg*, std::size_t,       \
                                   ::rtti::error_code&);                       \
        static inline factory_fn factory =                                     \
            &::rtti::detail::ctor_factory<T, param_types>;                    \
    };                                                                          \
    }
```

**参数**:`T` — 反射类型;`...` — 每构造参数至少一个 `(name, type, optional)` 三元组(`name` 字符串字面量;`type` C++ 参数类型;`optional` `bool`)。**效果**:特化 `ctor_registry<T>` 的 `count`、`params`、`param_types` 与接线的 `factory`;启用 `has_ctor_registry<T>` 与经 `make_object<T>` / `create_instance<T>`(reflect.hpp)的反射构造。

示例:

```cpp
struct MyType {
    MyType(int x, std::string y) : x_(x), y_(std::move(y)) {}
    int x_;
    std::string y_;
};
RTTI_DEFINE_TYPE(MyType, x_, y_)                       // namespace scope
RTTI_DEFINE_CTOR(MyType, ("x", int, false), ("y", std::string, true))
// rtti::make_object<MyType>(rtti::ctor_arg::scalar("x", field_value::of(int64_t(1))))
```

---

## 25. 方法注册宏

每次宏调用在命名空间作用域注册类型 `T` 的**一个**方法:

- `RTTI_DEFINE_METHOD(T, MethodName)` — 成员方法。
- `RTTI_DEFINE_STATIC_METHOD(T, MethodName)` — 静态方法。

每个把 `method_entry` 链入 `T` 的表(`method_link`)与类型擦除 `instance` 路径用的程序级表(`method_global_link`)。注册结构体按 `(method, line)` 唯一,故一个类型的多个方法,以及不同类型在不同行的同名方法,在同一 TU 共存。结构体在匿名命名空间内定义,故头中宏被多 TU 使用时每 TU 链接一个条目。

```cpp
#define RTTI_DEFINE_METHOD(T, MethodName)                                     \
    namespace rtti { namespace detail { namespace {                           \
    struct RTTI_CAT(RTTI_CAT(rtti_method_, MethodName), RTTI_CAT(_, __LINE__)) { \
        static ::rtti::error_code call(void* self,                             \
                                       const ::rtti::ctor_arg* args,           \
                                       std::size_t count,                      \
                                       ::rtti::field_value* out,               \
                                       ::rtti::value_io* node) noexcept {      \
            return ::rtti::detail::member_invoke<T>(&T::MethodName, self,      \
                                                    args, count, out, node);  \
        }                                                                     \
        static const ::rtti::detail::method_entry* entry() noexcept {         \
            static ::rtti::detail::method_entry e = {                         \
                #MethodName, false,                                           \
                typeid(decltype(&T::MethodName)).name(),                       \
                &call, &typeid(T), nullptr};                                   \
            return &e;                                                         \
        }                                                                     \
        static void link() noexcept {                                         \
            ::rtti::detail::method_link<T>(entry());                          \
            ::rtti::detail::method_global_link(entry());                      \
        }                                                                     \
        RTTI_CAT(RTTI_CAT(rtti_method_, MethodName), RTTI_CAT(_, __LINE__))() noexcept { \
            link();                                                           \
        }                                                                     \
    };                                                                        \
    static RTTI_CAT(RTTI_CAT(rtti_method_, MethodName), RTTI_CAT(_, __LINE__)) \
        RTTI_CAT(RTTI_CAT(rtti_method_inst_, MethodName), RTTI_CAT(_, __LINE__)){}; \
    } } }

#define RTTI_DEFINE_STATIC_METHOD(T, MethodName)                              \
    namespace rtti { namespace detail { namespace {                           \
    struct RTTI_CAT(RTTI_CAT(rtti_smethod_, MethodName), RTTI_CAT(_, __LINE__)) { \
        static ::rtti::error_code call(void*,                                 \
                                       const ::rtti::ctor_arg* args,           \
                                       std::size_t count,                      \
                                       ::rtti::field_value* out,               \
                                       ::rtti::value_io* node) noexcept {      \
            return ::rtti::detail::static_invoke(&T::MethodName, args, count, \
                                                 out, node);                  \
        }                                                                     \
        static const ::rtti::detail::method_entry* entry() noexcept {         \
            static ::rtti::detail::method_entry e = {                         \
                #MethodName, true,                                            \
                typeid(decltype(&T::MethodName)).name(),                       \
                &call, &typeid(T), nullptr};                                   \
            return &e;                                                         \
        }                                                                     \
        static void link() noexcept {                                         \
            ::rtti::detail::method_link<T>(entry());                          \
            ::rtti::detail::method_global_link(entry());                      \
        }                                                                     \
        RTTI_CAT(RTTI_CAT(rtti_smethod_, MethodName), RTTI_CAT(_, __LINE__))() noexcept { \
            link();                                                           \
        }                                                                     \
    };                                                                        \
    static const RTTI_CAT(RTTI_CAT(rtti_smethod_, MethodName), RTTI_CAT(_, __LINE__)) \
        RTTI_CAT(RTTI_CAT(rtti_smethod_inst_, MethodName), RTTI_CAT(_, __LINE__)){}; \
    } } }
```

**参数**:`T` — 拥有类型;`MethodName` — (静态)成员函数名。方法签名由 `decltype(&T::MethodName)` 产生:返回类型与参数表经 `member_traits` / `static_traits` 分解;参数经 `ctor_arg`(位置绑定)旅行;结果经 `field_value` / `value_io` 汇回来(见 §17)。

示例:

```cpp
struct Calc {
    static int add(int a, int b) { return a + b; }
    int mul(int a, int b) { return a * b; }
};
RTTI_DEFINE_STATIC_METHOD(Calc, add)   // namespace scope
RTTI_DEFINE_METHOD(Calc, mul)
assert(rtti::method_count<Calc>() == 2);
```

---

## 26. 头结构与会 include 环说明

反射核心的内联访问器(`typed_field_access::get_value`/`get_value_named` 与容器 `for_each` 路径)调用格式无关转换器(`to_value` / `from_value` / `member_to_value` / `member_to_value_named`),其内联定义在 `convert.hpp`。只包含本头(或 `reflect.hpp`)的 TU 实例化这些模板而看不到定义,链接期留未定义引用(LNK2019)。把 include 推到文件末尾打破潜在环(`convert.hpp` 顶部包含 `reflection.hpp`,`#pragma once` 抑制嵌套包含),同时让每个反射核心 TU 看到转换器定义:

```cpp
#include <rtti/convert.hpp>
```

头文件物理布局(行号对应当前文件):

| 行 | 节 |
|---|---|
| 1-29 | 头注释、标准 include |
| 30-269 | `namespace rtti` 开、转换器前向声明、`fields_of`、`base_types`、容器形状 trait、`key_traits`、`type_id` |
| 266-805 | `type_id`、`field_value`、`field_value_to`、`value_io`、`field_access_base`、`typed_field_access` |
| 807-857 | 侵入式 `fields_of` 拾取、`field_count`、`fields`、`for_each_field`、`has_default` |
| 859-1163 | 属性系统(`attr_arg`、`attr`、表、`make_attr`、查询) |
| 1225-1825 | 宏基础（`RTTI_NARG`、`RTTI_APPLY`、`RTTI_IS_EMPTY`，…） |
| 1826-1890 | 枚举反射（`enum_label`、`enum_table`、枚举查询） |
| 1891-1975 | 类型描述系统（`type_kind`、`type_desc` 族） |
| 1976-2072 | 编译期判断、家族判别、枚举助手 |
| 2073-2279 | `type_of` 与 `detail::type_of_impl` |
| 2280-2379 | 模板实参反射 |
| 2380-2629 | 派生类型合并机制 |
| 2630-3006 | 反射构造（`ctor_arg`、`ctor_param`、`ctor_registry`） |
| 3008-3064 | `as` / `cast` 引用转换 |
| 3066-3326 | `object_ref<T>` |
| 3328-3589 | `instance` 类型擦除句柄 |
| 3591-3685 | 转换助手（`same_type_id`、`derived_from_desc`、`adjust_to`、`scalar_convertible`） |
| 3686-4126 | 方法反射 |
| 4127-5436 | 字段注册助手宏 |
| 5437-5585 | 类型/枚举注册宏 |
| 5586-6745 | 模板信息注册宏 |
| 6747-7605 | 属性/构造器注册宏 |
| 7606-7676 | 方法注册宏、尾部 include |

## 27. 其它语言

- [English (英文版)](../en/reflection.md)