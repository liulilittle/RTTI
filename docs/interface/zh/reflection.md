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

---

## 6. 属性 / 注解系统

属性把附加元数据挂到反射类型或单个字段,运行期可查(如 C# attribute)。参与方式:

- `attr_arg` — 属性参数的一个值(kind 化负载,如 `attr_arg::make_int(3)`、`make_str("x")` 等)。
- `attr` — 一个属性:`name` + 参数数组。
- `field_attr_entry` / `field_attr_list` — 字段级属性条目/列表。
- `attr_table<T>` / `field_attr_table<T>` — 类型级/字段级属性表的静态注册表。
- `RTTI_DEFINE_TYPE_ATTRS(T, ...)` / `RTTI_DEFINE_FIELD_ATTRS(T, ...)` — 注册宏。
- 查询:`attr` 查找函数族(按名/索引/键),见头文件 `reflection.hpp` §6。

> 本章与 §7–§17 的完整逐项签名、示例与提取行号见英文版 [`docs/interface/en/reflection.md`](../en/reflection.md) 对应章节(结构与本章一一对应);中文版以下各节为详尽中文概述,并在关键名称处保留英文签名以便对照。