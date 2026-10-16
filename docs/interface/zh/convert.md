# 值转换核心 — 接口参考

**模块:** RTTI 值转换核心(`rtti` 命名空间)

**范围:** 本文覆盖 RTTI 库的格式无关转换面:`include/rtti/convert.hpp`(主文件,2,820 行,含第 11 章的 `rtti::convert::as`/`cast`/`to` 值语义强转桥)加四个支持头 `error.hpp`、`number_format.hpp`、`node_io.hpp`、`memory.hpp`。签名单字抄自头文件;行为说明严格取自头注释与共享错误契约。

**覆盖文件**

| 头文件 | 职责 |
| --- | --- |
| `include/rtti/convert.hpp` | 格式无关 `value_io` ↔ C++ 值转换:`converter<T>`、`to_value`/`from_value`(+ `_named`)、日期时间与任意进制桥 |
| `include/rtti/error.hpp` | 共享错误设施:`error_code`、`error_info`、`result<T>` |
| `include/rtti/number_format.hpp` | 基于 `to_string` 之上的数字/反射/节点文本格式化 |
| `include/rtti/node_io.hpp` | 节点级桥:`locate` / `node_as` / `to_node`(spec 4.3) |
| `include/rtti/memory.hpp` | `string_view` 别名与 `memory_arena` |

**命名空间:** 所有公开内容位于 `namespace rtti`。仅形状助手的转换器位于 `namespace rtti::detail`(标量道、进制转换、命名辅助、共享实数格式化)。`naming_policy` 是 `rtti::detail::naming_policy`。

**格式门控:** `convert.hpp` 是格式无关的。它只包含 `base_convert.hpp`、`date_time.hpp`、`error.hpp`、`naming.hpp` 与 `reflection.hpp`,从不包含格式适配器(JSON/XML/INI/TOML/proto/flat)。因此无论哪些 `RTTI_ENABLE_*` 门开启与否,它总能编译。

**交互头(被消费但不在本文详述):** `reflection.hpp` 定义 `value_io`、`field_value`、容器形状 trait、`field_access_base`、`fields<T>`/`field_count<T>`/`has_default<T>` 注册访问器与 `enum_table<E>`/`enum_to_string`。`naming.hpp` 定义 `detail::naming_policy` / `apply_naming` / `key_case`。`date_time.hpp` 定义 `date_time`、`is_date_time` 与 `epoch_ns_from_digits`。`base_convert.hpp` 定义支撑 `to_value_base`/`from_value_base` 的 `detail::to_base`/`from_base` 机制。此处仅概括转换签名引用的部分。

---

## 1. 错误码(`include/rtti/error.hpp`,第 1–103 行)

`error_code` 是反射核心与每个格式适配器共享的唯一错误通道。转换器直接返回它;解析 API 以 `result<T>` 包住它。

### 1.1 `enum class error_code : uint8_t`

每个枚举值按声明顺序自动编号(隐式值 0..23)。

| 枚举 | 值 | 含义 |
| --- | --- | --- |
| `ok` | 0 | 成功。 |
| `unexpected_token` | 1 | 该解析位置出现不应有的 token(格式规格解析器对非法规格也发此码,见第 8 章)。 |
| `unterminated_string` | 2 | 引号字符串直到缓冲末尾。 |
| `invalid_number` | 3 | 畸形数字字面量(也用于:未知纪元数字位数道,见 `epoch_ns_from_digits`;进制桥的非法进制/非法数字)。 |
| `invalid_utf8` | 4 | 字节不是合法 UTF-8。 |
| `unterminated_comment` | 5 | 注释开启符没有闭合。 |
| `mismatched_tag` | 6 | XML 开/闭标签名不匹配。 |
| `duplicate_attribute` | 7 | 同一 XML 属性出现两次。 |
| `bad_entity` | 8 | 未知或畸形 XML 实体。 |
| `unterminated_cdata` | 9 | CDATA 段未闭合。 |
| `dtd_missing` | 10 | 需要 DTD 但缺失。 |
| `too_deep` | 11 | 嵌套超出深度预算——也由转换器深度守卫在读写链超过 512 层时返回(见 §4.12)。 |
| `invalid_root` | 12 | 文档根的类型不对。 |
| `ini_unterminated_section` | 13 | INI `[section]` 没有闭合 `]`。 |
| `ini_bad_line` | 14 | 无法解析的 INI 行。 |
| `ini_missing_value` | 15 | INI 键没有值。 |
| `no_such_field` | 16 | 缺键/缺成员——也由转换器在源或目标节点指针为 `nullptr` 时返回,由 `locate` 在缺键/越界索引时返回。 |
| `kind_mismatch` | 17 | 源节点类别不适合目标类型(或路径步越过非对象/非数组)。转换器最常见错误。 |
| `missing_key` | 18 | 反射结构体字段在源对象中缺失且类型无默认保留行为。 |
| `proto_truncated` | 19 | protobuf wire 数据在字段中间结束。 |
| `proto_bad_field` | 20 | 畸形 protobuf 字段编码。 |
| `proto_type_mismatch` | 21 | 解码出的 protobuf 类别与 schema 不符。 |
| `overflow` | 22 | 数值超出目标可表示范围(转换器绝不静默回绕);也用于实数→整数中 NaN/inf。 |
| `no_such_method` | 23 | 方法反射:无匹配方法。 |

### 1.2 `const char* to_string(error_code code) noexcept`

任一代码的可读英文消息。**绝不返回 `nullptr`**(未知代码也给出非空串)。

### 1.3 `struct error_info`

```cpp
struct error_info {
    error_code code = error_code::ok;
    const char* src = nullptr;   // 整个源缓冲(可为 null)
    size_t src_len = 0;
    size_t offset = 0;           // 错误字节偏移

    size_t line() const noexcept;    // 1 基;自 src/offset 惰性计算
    size_t column() const noexcept;  // 1 基;惰性计算
    const char* message() const noexcept;  // 等于 to_string(code)
};
```

`line()`/`column()` 自源缓冲惰性计算,调用方只在真正需要时付费。`operator==`/`operator!=` 只比较 `code` 与 `offset`。

### 1.4 `template <typename T> struct result<T>` 与 `result<void>`

```cpp
template <typename T>
struct result {
    T value{};              // T 必须可默认构造
    error_info err;
    bool ok() const noexcept;              // err.code == error_code::ok
    explicit operator bool() const noexcept;
    T& operator*() noexcept;
    const T& operator*() const noexcept;
};

template <> struct result<void> {          // 无值负载
    error_info err;
    bool ok() const noexcept;
    explicit operator bool() const noexcept;
};
```

辅助:`result<T> make_ok(T v)` 与 `result<T> make_error(error_code code)`。

**转换器 API 中:** `node_io` 桥的 `locate` 返回 `result<value_io*>`,`node_as` 返回 `result<T>`。核心 `from_value`/`to_value` 函数则返回裸 `error_code`。

*覆盖:`error.hpp` 第 1–103 行。*

## 2. 转换签名用到的类型(来自 `reflection.hpp` / `naming.hpp` / `date_time.hpp`)

### 2.1 `class value_io`(抽象源/汇,`reflection.hpp` 第 409–444 行)

转换函数接受并产出 `value_io` 节点。每个格式适配器(JSON/XML/INI/TOML/proto/flat)在自己的 DOM 上实现此接口;因此本头与格式无关。类别镜像 JSON 风格的值范畴。

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
    virtual value_io* make_node() = 0;   // 由树所有、独立的子节点
};
```

转换器语义依赖每个标量类别的读侧道序——见 §5。

### 2.2 `class field_value`(类变体的标量负载,`reflection.hpp` 第 284–338 行)

`make_scalar(field_value)` 是转换器把标量交给汇的方式。类别:`none | boolean | integer | real | string | object`。工厂:`of(bool)`、`of(int64_t)`、`of(double)`、`of(string_view)`、`of_object()`;默认构造产生 `kind::none`(空 `std::optional` 就写这个,适配器把 `none` 序列化为 null)。访问器 `as_bool()/as_integer()/as_real()/as_string()` 在类别不匹配时返回回退值(false / 0 / 0.0 / 空视图);`type()` 报告存储类别。`field_value_to(M&, const field_value&)` 是标量类型化分派助手。

### 2.3 容器形状 trait(`reflection.hpp` 第 103–258 行)

这些门控 `from_value`/`to_value` 中的 `if constexpr` 分派。

| Trait | 为真时 |
| --- | --- |
| `is_sequence<T>` | 有 `value_type` + `push_back` + `begin`/`end` 且**不是** `std::basic_string`(vector/list/deque)。 |
| `is_associative<T>` | 有 `key_type` + `value_type` + `insert`。 |
| `is_map<T>` | 关联**且**有 `mapped_type`。 |
| `is_set<T>` | `is_associative && !is_map`。 |
| `is_queue<T>` | 有 `value_type` + `push`/`pop`/`front`/`back`。 |
| `is_stack<T>` | 有 `value_type` + `push`/`pop`/`top`。 |
| `is_priority_queue<T>` | 有 `value_type` + `value_compare` + `push`/`pop`/`top`。 |
| `is_optional<T>` | T 恰为 `std::optional<U>`。 |
| `is_variant<T>` | T 暴露 `std::variant_size`。 |
| `is_tuple<T>` | 有 `std::tuple_size` 且无 `data()`(排除 `std::array`)。 |
| `is_pair<T>` | 有 `first`/`second` 成员。 |
| `is_fixed_sequence<T>` | 有 `value_type` + `begin`/`end`,无 `push_back`,非字符串,非关联(`std::array`)。 |
| `is_c_array<T>` | 原生数组 `T[N][M]...`,**排除** `char[N]`。 |
| `is_md_array<T>` | 元素类型也是原生数组的原生数组。 |
| `is_reflected<T>` | `fields_of<T>::count` 存在(经 `RTTI_DEFINE_TYPE*` 注册)。 |
| `is_char_string<T>` | 任意 `std::basic_string` 特化。 |
| `has_push_back<T>` | 有 `value_type` + `push_back`。 |

相关访问器:`field_count<T>()`、`fields<T>()`(`field_access_base const*` 数组)、`for_each_field`、`has_default<T>`(用 `_WITH_DEFAULT` 宏注册时为真——源缺键时保留成员现值)、`key_traits<K>::assign/ view`(可字符串化 map 键)。`field_access_base`(第 451 行起)提供 `get_value`/`set_value`、命名变体 `get_value_named`/`set_value_named`、`is_null_value`、`is_container`、`size` 与容器变更钩子。

注册宏(都在 `reflection.hpp`):`RTTI_DEFINE_TYPE(T, ...)`(字段列表)、`RTTI_DEFINE_TYPE_DERIVED(T, Base, ...)`、`RTTI_DEFINE_TYPE_WITH_DEFAULT(T, ...)`、`RTTI_DEFINE_TYPE_INTRUSIVE`、`RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT`、`RTTI_DEFINE_ENUM(T, ...)`。

### 2.4 `detail::naming_policy`(`naming.hpp` 第 41–45 行)

```cpp
struct naming_policy {
    key_case kc = key_case::identity;     // identity | snake | camel
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
};
```

`key_case`(第 26 行):`identity`、`snake`、`camel`。`apply_naming(field, np)` 计算输出键:`rename_map` 命中优先于 `key_case`。`skip_empty` 在序列化时丢弃值为空的字段(见 §6.1)。

### 2.5 `is_date_time<T>` 与纪元单位映射(`date_time.hpp`)

`is_date_time<T>` 恰对 `rtti::date_time` 为真(第 185–186 行)。`epoch_ns_from_digits(value, out)`(第 192–245 行)按数值的**绝对十进制位数**推断数值纪元的单位:10 = 秒,13 = 毫秒,16 = 微秒,19 或 20 = 纳秒;零映射到纪元零;任何其它位数是 `invalid_number`;缩放乘积超出 int64 范围是 `overflow`。

转换器用到的 `date_time` 读写钩子:`date_time(int64_t ns)`、静态 `result<date_time> parse(string_view iso)`、`to_unix_seconds/ms/us/ns(int64_t&)`、`epoch_ns()`、`format()`。

### 2.6 进制转换核心(`base_convert.hpp`,`namespace rtti::detail`)

`to_base(value, base, digits=nullptr)` → 字符串(非法进制/表过小返回空);`to_base_signed(value, base)`;`from_base(s, base, out [, digits] [, case_insensitive])` 与 `from_base_signed(s, base, out)` → `error_code`(坏输入/非法数字为 `invalid_number`,量级超出目标道为 `overflow`)。`default_digits()`、`rfc4648_digits()`、`digit_table_len`、`default_case_insensitive`、`base_digit`。

默认数字表(spec §15.2):进制 2..36 = `0-9a-z`;进制 37..62 = `0-9a-zA-Z`;进制 63..64 在索引 62 处加 `+`、索引 63 处加 `/`。默认表对进制 ≤ 36 大小写不敏感;进制 > 36 大小写敏感。

*覆盖:`reflection.hpp` 第 103–338、409–518、874–917、1836–1857 行;`naming.hpp` 第 1–53 行;`date_time.hpp` 第 34–245 行;`base_convert.hpp` 第 1–112 行。*

## 3. `converter<T>` 表面(`convert.hpp` 第 303–678 行)

### 3.1 主模板 `template <typename T, typename = void> struct converter`

```cpp
template <typename T, typename = void>
struct converter {
    static error_code from(const value_io* src, T& out);
    static error_code to(value_io* dst, const T& in);
};
```

主模板实现**标量**道(bool / 整型 / 浮点):

- `from`:`bool` → 读 bool 道,否则整型道(`0 → false`,任何非零 → `true`),否则 `kind_mismatch`;整型(非 bool)→ `detail::scalar_integral_from`;浮点 → `detail::scalar_real_from`;到达此叶的其它类型 → `kind_mismatch`。
- `to`:对 `sizeof(T) >= sizeof(uint64_t)` 的 `unsigned T`,高于 `INT64_MAX` 的值报 **`overflow`** 而非静默回绕 `static_cast`(spec §0 禁止静默丢失);否则 `dst->make_scalar(scalar_of(in))`。`scalar_of` 映射:bool → `field_value::of(bool)`;纯净 `char` → 其 ASCII 字节值以 `int64_t` 表示(0..255);其它整型 → `field_value::of(int64_t)`;浮点 → `field_value::of(double)`;其它 → `field_value::of_object()`。

### 3.2 标量转换语义(`detail`,第 55–301 行)

这是支撑主模板与字符串转换器的、带范围检查的道。

| 道 | 签名 | 规则 |
| --- | --- | --- |
| `is_plain_char<T>` | trait,第 63 行 | 恰对 `char` 为真(spec §23.1 把 `char` 当 0..255 ASCII 字节,区别于 `signed char`/`unsigned char`)。 |
| `scalar_int64_to_integral` | `error_code (int64_t v, T& out)` | 无符号道/范围:负或超 max → `overflow`;有符号取正确 int64 界。失败时绝不改 `out`。 |
| `scalar_uint64_to_integral` | `error_code (uint64_t v, T& out)` | 精确 uint64 → 整型,故 `UINT64_MAX` 无损往返,无双精度损失。 |
| `scalar_real_to_integral` | `error_code (double v, T& out)` | 要求 `isfinite`(NaN/inf → `overflow`),向零截断,再范围检查(文档化的 2^64 / 2^63 边界处理)。绝无 UB。 |
| `scalar_text_to_integral` | `error_code (string_view s, T& out)` | 整段文本必须是数字。空/非数字 → `kind_mismatch`;越界 → `overflow`。整数字面量经 uint64 与 int64 道精确读取;带小数/指数文本经截断实数路径。 |
| `scalar_text_to_real` | `error_code (string_view s, T& out)` | 确定性 token `inf`/`infinity`/`-inf`/`-infinity`/`nan` 往返;其它非数字文本 → `kind_mismatch`;量级越界 → `overflow`。 |
| `scalar_format_real` | `void (double, std::string& out)` | 经 `std::to_chars` 生成最短往返文本;非有限值 → `nan`/`-inf`/`inf`;token 无 `.`/`e`/`E` 时补 `.0`。 |
| `scalar_integral_from` | `error_code (const value_io* src, T& out)` | 按 整型→实数→字符串→bool 道序尝试(bool → 0/1)。范围违例绝不回绕。 |
| `scalar_real_from` | `error_code (const value_io* src, T& out)` | 直接读实数道;整型道(IEEE 精度损失可接受);文本道解析。 |

### 3.3 `converter<std::basic_string<Char, Traits, Alloc>>`(第 367–400 行)

字符串是标量(文本)类型,不是序列。

- `from`:字符串道 → 赋值;整型道 → 十进制文本(`std::to_string`);实数道 → 最短往返文本(`detail::scalar_format_real`);否则 `kind_mismatch`。
- `to`:`dst->make_scalar(field_value::of(string_view(in.data(), in.size())))` → `ok`。

### 3.4 已注册枚举(第 429–459 行)

`is_registered_enum<T>`(第 429–433 行)在 `enum_table<T>::count` 存在(经 `RTTI_DEFINE_ENUM` 注册)时为真。特化 `converter<T, enable_if_t<is_enum && is_registered_enum>>` **经标签文本**转换:

- `from`:要求字符串道;线性搜索 `enum_table<T>::array` 找与文本相等的 `name`;命中 → `out = array[i].value`;未命中 → `kind_mismatch`。
- `to`:`enum_to_string(in)`;若结果为 null 或空(`name[0] == '\0'`)→ `kind_mismatch`;否则把标签写成字符串标量。

在此特化(而非任何格式适配器头)意味着所有格式共享同一份定义。

### 3.5 `date_time`(第 461–511 行)

`converter<T, enable_if_t<is_date_time<T>::value>>` 把 `date_time` 变成标量时间戳:

- `from`:**null `src` → `no_such_field`**。整型道 → `epoch_ns_from_digits(num, ns)` 再 `out = T(ns)`(按位数自动检测单位);字符串道 → `T::parse(text)`(其 `error_code` 传播,如 `invalid_number`);其它 → `kind_mismatch`。
- `to`:`in.to_unix_seconds(sec)`(错误传播,如 `overflow`);`make_scalar(field_value::of(sec))` — **数值纪元秒**(spec §14.2 默认)。非默认输出走 `to_value_dt`(§3.6)。

注意:`is_date_time` 检查在入口分派中**先于** `is_reflected`,所以用户注册过的 `date_time` 也按标量时间戳转换,而非被字段遍历的对象。

### 3.6 `enum class date_time_output` 与 `to_value_dt`(第 467–564 行)

```cpp
enum class date_time_output {
    numeric_seconds,
    numeric_ms,
    numeric_us,
    numeric_ns,
    iso_string
};

inline error_code to_value_dt(value_io* dst, const date_time& in,
                              date_time_output mode);
```

文档化的可选写表示入口。`mode` 选输出:`numeric_seconds`(默认)/ `numeric_ms` / `numeric_us` / `numeric_ns`(各经匹配的 `to_unix_*`,传播其 `error_code`)或 `iso_string`(`in.format()`,精确保留亚秒精度)。要求 `dst != nullptr`,否则 `no_such_field`。成功写一个标量节点并返回 `ok`。

**读回注意:** 公开表面**没有 `from_value_dt`**。读侧由 `date_time` 转换器特化(§3.5)服务,它自动检测数值纪元单位并接受 ISO 文本。

### 3.7 反射成员写钩子 `member_to_value`(第 571–589 行)

```cpp
template <typename M>
inline error_code member_to_value(value_io* dst, const M& v);
template <typename M>
inline error_code member_to_value_named(value_io* dst, const M& v,
                                        const detail::naming_policy& np);
```

反射结构体访问器用来序列化单个成员。`date_time` 成员经 `to_value_dt(dst, v, date_time_output::iso_string)` 路由,使反射写保留亚秒精度;其它成员类型走普通转换器链。`_named` 变体对日期时间忽略 `np`、其余用 `to_value_named`(非日期时间成员仍递归应用命名策略)。

### 3.8 任意进制整型桥(第 600–648 行)

```cpp
inline error_code to_value_base(value_io* dst, std::uint64_t value,
                                unsigned base, const char* digits = nullptr);
inline error_code from_value_base(const value_io* src, unsigned base,
                                  std::uint64_t& out);
inline error_code to_value_base_signed(value_io* dst, std::int64_t value,
                                       unsigned base);
inline error_code from_value_base_signed(const value_io* src, unsigned base,
                                         std::int64_t& out);
```

把整型作为 `N` 进制字符串(2..64)序列化/反序列化——经显式入口,镜像日期时间的设计(普通 `to_value`/`from_value` 继续发数值标量,四格式断言依赖之;文本经这些函数显式选入)。

- `to_value_base`:null `dst` → `no_such_field`;`detail::to_base(value, base, digits)`;空结果(非法进制/自定义表过小)→ `invalid_number`;否则写字符串标量并返回 `ok`。
- `from_value_base`:null `src` → `no_such_field`;非字符串源 → `kind_mismatch`;然后 `detail::from_base(s, base, out)` — 空/非法输入为 `invalid_number`,量级溢出为 `overflow`。注意三参 `from_base` 用默认表 + strtol 风格大小写策略。
- 有符号对:同形;`detail::to_base_signed`/`from_base_signed` 接受可选 `+`/`-` 前缀并把量级限制在 int64 道(`INT64_MIN` 精确可表)。这是日期时间纪元的钩子(spec §15.4)。

示例(来自 `tests/base_convert_test.cpp:321-351`):`to_value_base(node, 255, 16)` 写文本 `"ff"`;`from_value_base(node, 16, back)` 还原 255。可传 RFC 4648 风格自定义表到 64 进制:`to_value_base(node, 63, 64, kBaseConvertUrlSafe)` 写 `"_"`。

### 3.9 `converter<char[N], void>`(第 654–678 行)

定长 C `char` 数组是**字符串目标**,不是数值序列(`is_c_array` 排除 `char[N]`,spec §12.2):

- `from`:要求字符串道;若 `v.size() >= N` → `kind_mismatch`(必须给 NUL 留空间);清零缓冲再复制。绝不越界。
- `to`:序列化到第一个 NUL 为止作为字符串标量。

*覆盖:`convert.hpp` 第 55–678 行。*

## 4. 容器、复合与反射转换器(`convert.hpp` 第 680–1297 行)

下列所有转换函数返回 `error_code`。`from_*` 把源节点读入既有目标对象;`to_*` 把值写入汇节点。写侧转换器经 `make_array()/make_object()/make_node()/push_back()/set_key()` 建树;**写中途失败会留下部分填充的树**(失败元素之前创建的子节点仍附着),并传播第一个失败元素的错误码。

### 4.1 `std::vector<bool>` 处理(第 686–689 行)

`is_vector_bool<std::vector<bool, Alloc>>` 检测位打包特化(其迭代器在某些标准库上产生代理对象)。序列写器在 `to_value` 前把每个代理复制成真 `bool`,使路径在 libstdc++/libc++/MSVC 间可移植。

### 4.2 序列族 — `vector` / `list` / `deque`(第 692–737 行)

```cpp
template <typename Seq> error_code sequence_from(const value_io* src, Seq& out);
template <typename Seq> error_code sequence_to(value_io* dst, const Seq& in);
```

- `sequence_from`:源必须是 `kind::array`,否则 `kind_mismatch`;`out.clear()`;经 `at(i)` 每子节点一个元素(`nullptr` 子节点 → `kind_mismatch`),递归 `from_value`。
- `sequence_to`:`make_array()`;每个元素经 `make_node()` 上的 `to_value` 后 `push_back`。对 `vector<bool>` 用代理复制循环。

### 4.3 Map 族(第 740–777 行)

```cpp
template <typename Map> error_code map_from(const value_io* src, Map& out);
template <typename Map> error_code map_to(value_io* dst, const Map& in);
```

- `map_from`:源必须是 `kind::object`;`out.clear()`;经 `get_key_at(i, key, child)` 枚举键值对(false 或 null 子节点 → `kind_mismatch`);值递归 `from_value`;键用 `key_traits<key_type>::assign` 转换并 `emplace`。
- `map_to`:`make_object()`;每个映射值经 `to_value`;键用 `key_traits<key_type>::view` 写。

### 4.4 Set 族(第 780–814 行)

```cpp
template <typename Set> error_code set_from(const value_io* src, Set& out);
template <typename Set> error_code set_to(value_io* dst, const Set& in);
```

`set_from`:源必须是 `kind::array`;`out.clear()`;每个元素 `from_value` 后 `insert`。`set_to`:`make_array()`;每个元素 `to_value` + `push_back`。

### 4.5 容器适配器 — `queue` / `stack` / `priority_queue`(第 822–942 行)

这些不暴露迭代器,所以读取复制并逐个排空 `front`/`top`。

- `queue_from`(FIFO):源 `kind::array`;换入空队列;按序 push 子节点(数组序 = front..back)。`queue_to`:排空副本,push `front` 元素(数组序 = front..back)。
- `stack_from`(LIFO):源 `kind::array`;子节点**从索引 n-1 到 0** 消费(数组序 = top..bottom;反向重建使 top 保持在顶)。`stack_to`:经 `top` 排空副本,故数组是 top..bottom。
- `priority_queue_from`:源 `kind::array`;子节点从索引 n-1 到 0 消费,逐个 push —— 堆重新堆化到同一 top。`priority_queue_to`:经 `top` 排空副本,故数组按**弹出序**(默认 `less` 比较器为最大在前)。

三者都以 `kind_mismatch` 拒绝非数组源,并按同样方式处理 null 子节点。

### 4.6 `std::optional`(第 951–976 行)

```cpp
template <typename T> error_code optional_from(const value_io* src, std::optional<T>& out);
template <typename T> error_code optional_to(value_io* dst, const std::optional<T>& in);
```

- `optional_from`:null `src` → `no_such_field`;`null` 源 → `out.reset()` 且 `ok`;否则 `from_value` 进临时并启用之。
- `optional_to`:未启用 → `make_scalar(field_value{})`(类别 `none`,序列化为 null)且 `ok`;启用 → `to_value(dst, *in)`。

### 4.7 `std::variant`(第 983–1047 行)

Wire 形状:一个**对象** `{"index":N,"value":V}`,其中 `N` 是活跃备选索引,为整型标量。

```cpp
template <typename V> error_code variant_to(value_io* dst, const V& in);
template <typename V> error_code variant_from(const value_io* src, V& out);
```

- `variant_to`:`valueless_by_exception()` → `kind_mismatch`;`make_object()`;写 `"index"` = `int64(in.index())`;写 `"value"` 经 `to_value`(子失败传播,而非留下静默的空 `value` 节点)。
- `variant_from`:源必须是对象;`"index"` 必须在并经整型道可转、位于 `[0, variant_size)`;`"value"` 必须在;经 `variant_from_at` / `variant_from_indexed` 分派到该备选(对备选集展开——每个备选读都是通用 `from_value<Alt>`,因此实例化该函数即安全实例化所有分支)。

### 4.8 `std::tuple` / `std::pair`(第 1051–1100 行)

`std::pair` 是二元 tuple(`std::get`/`std::tuple_size` 可用),完全由 tuple 路径覆盖。

```cpp
template <typename Tuple> error_code tuple_to(value_io* dst, const Tuple& in);
template <typename Tuple> error_code tuple_from(const value_io* src, Tuple& out);
```

- `tuple_to`:`make_array()`;展开 `index_sequence`,每元素经 `to_value(std::get<I>)` 一个子节点,传播**第一个**子失败同时继续转换其余。
- `tuple_from`:源必须是 `kind::array`;逐元素展开,读 `at(I)`(null 子节点 → `kind_mismatch`),各经 `from_value(std::get<I>)`。源可比 tuple 长(多余元素忽略);源较短在第一个缺失子节点处报 `kind_mismatch`。

### 4.9 定长序列 — `std::array`(第 1103–1139 行)

```cpp
template <typename A> error_code fixed_sequence_to(value_io* dst, const A& in);
template <typename A> error_code fixed_sequence_from(const value_io* src, A& out);
```

- `fixed_sequence_to`:`make_array()`;每元素一个子节点。
- `fixed_sequence_from`:源必须是 `kind::array`;**严格**长度匹配 — `src->size()` 必须等于 `out.size()`,否则 `kind_mismatch`(绝不静默截断或残留陈旧尾部,镜像 `carray_from` 契约)。然后逐槽 `from_value`。

### 4.10 C 风格数组 `T[N]` / `T[N][M]` / `T[A][B][C]`(第 1149–1252 行)

Trait:`carray_total<T>`(维度积;rank 0 ⇒ 1)、`carray_rank<T>()`、`carray_extent<T, I>()`。

```cpp
template <typename T, std::size_t N>
error_code carray_to(value_io* dst, const T (&in)[N]);
template <typename T, std::size_t N>
error_code carray_from(const value_io* src, T (&out)[N]);
```

- `carray_to`:嵌套数组,每维一层。
- `carray_from`:源必须是 `kind::array`;令 `total = carray_total<T[N]>`。对多维目标**先试嵌套形式**(每外维一个子数组——这也在 M==1 且平展与嵌套计数重合时消歧);若嵌套走失败且 `sz == total`,接受**平展行主序**形式(`carray_from_flat`);否则当 `sz == total` 时试平展形式;其它计数 → `kind_mismatch`。写绝不越界。
- `carray_from_flat(src, base, out)`:把源当作覆盖完整维度积的单条平展元素序列,按行主序偏移线性填充;null 子节点 → `kind_mismatch`。

### 4.11 反射结构体遍历(第 1259–1297 行)

```cpp
template <typename T> error_code reflect_from(const value_io* src, T& out);
template <typename T> error_code reflect_to(value_io* dst, const T& in);
```

经 `RTTI_DEFINE_TYPE*` 注册的类型按字段逐读逐写,通过 `fields<T>()` / `field_count<T>()`:

- `reflect_from`:源必须是 `kind::object`;对表序中每个字段访问器 `fa`,查 `fa->name`。`has_default<T>::value` 时缺键**保留成员现值**(`_WITH_DEFAULT` 注册);否则缺键返回 `missing_key`。存在的键经 `fa->set_value(&out, sub)` 递归——嵌套/容器/标量成员都过访问器,其 `error_code` 传播。
- `reflect_to`:`make_object()`;每个字段经 `fa->get_value(&in, child)` 存于 `fa->name`。

### 4.12 深度守卫(第 1306–1326 行)

每个 `from_value`/`to_value` 入口带线程局部深度守卫:

```cpp
inline constexpr std::size_t kConversionMaxDepth = 512;
inline int& conversion_depth() noexcept;              // thread_local 计数器
struct conversion_depth_guard { /* RAII: ++/-- */ };
```

递归超过 512 层的读写链(例如写侧自引用数据结构)报 `error_code::too_deep` 而非栈溢出。格式解析器同样把树深上限设为 512。

*覆盖:`convert.hpp` 第 680–1326 行。*

## 5. 入口 `from_value` / `to_value`(`convert.hpp` 第 1328–1457 行)

### 5.1 ADL 钩子检测(第 1336–1350 行)

```cpp
template <typename T, typename = void> struct has_adl_from;  // 当类型
template <typename T, typename = void> struct has_adl_to;    // 的关联命名空间
                                                              // 提供三参钩子时为真
```

自定义类型通过**在类型自身命名空间**写如下代码接入:

```cpp
void from_value(const value_io*, T&, error_code&);
void to_value(value_io*, const T&, error_code&);
```

rtti 的两参模板与三参检测永不匹配,普通查找找不到它们;只有用户钩子能活过重载决议。

### 5.2 `from_value`

```cpp
template <typename T>
error_code from_value(const value_io* src, T& out);
```

把 `src` 读入既有对象 `out`。错误码:`nullptr src` → `no_such_field`;超过深度预算 → `too_deep`;然后**按此顺序**分派:

```
optional -> variant -> tuple (pair 是二元 tuple) -> C array -> fixed_sequence
-> map -> set -> priority_queue (先于 stack) -> queue -> stack -> sequence
-> date_time (先于 reflected) -> reflected -> ADL 钩子 -> converter<T>
```

保证顺序安全的约束:`is_fixed_sequence` 永不匹配 map/set(关联类型已在 `reflection.hpp` 排除),原生 C 数组没有 `value_type`,故既不进 `is_sequence` 也不进 `is_fixed_sequence`。`date_time`(即使经 `RTTI_DEFINE_TYPE` 注册)按标量转换,不做字段遍历。最终叶 `converter<T>` 是 §3.1 的标量模板。

### 5.3 `to_value`

```cpp
template <typename T>
error_code to_value(value_io* dst, const T& in);
```

把 `in` 写入汇 `dst`。错误:`nullptr dst` → `no_such_field`;超深度预算 → `too_deep`;分派顺序与 `from_value` 完全镜像。

### 5.4 用法注记

- `node_io` 的 `node_as<T>` 结果负载必须可默认构造;`result<T>` 值被值初始化。
- `from_value`/`to_value` 对数据形状问题绝不抛异常;失败以 `error_code` 返回。分配仍可能因容器/字符串增长抛 `std::bad_alloc`,这在错误通道契约之外。
- 转换器可重入,深度记账线程局部;每线程一个 arena/document 是预期用法(见第 10 章)。

## 6. 命名转换(`*_named`,第 1459–2176 行)

`to_value_named` / `from_value_named` 镜像普通分派链并在**反射字段边界**应用命名策略:键经 `detail::apply_naming(fa->name, np)` 计算;序列化侧 `np.skip_empty` 丢弃空字段。

```cpp
template <typename T>
error_code to_value_named(value_io* dst, const T& in,
                          const detail::naming_policy& np);
template <typename T>
error_code from_value_named(const value_io* src, T& out,
                            const detail::naming_policy& np);
```

分派顺序与普通入口相同。反射对象内部,容器元素递归走命名变体,因此**任何容器内的嵌套反射结构体也被改名**;关联容器键是用户数据、保持不动。

### 6.1 空字段谓词(第 1483–1503 行)

`detail::field_is_empty(fa, obj)` 在 `skip_empty` 下字段值为「空」时为真:空等价(`fa->is_null_value(obj)` — 空 optional / null 指针)、`size()==0` 的容器、空字符串、数值零或 `false` bool。容器经类型擦除访问器探测;标量经 `get() -> field_value`,故检查格式无关。

### 6.2 各族的命名变体

第 4 章每一族都有经 `to_value_named`/`from_value_named` 递归的命名孪生:

- `reflect_to_named` / `reflect_from_named`(1508–1552)— 唯一对键应用 `apply_naming` 者;`skip_empty` 只影响序列化;读侧缺键行为与 `reflect_from` 一致(`missing_key`,或 `has_default` 时保留默认)。
- `optional_to_named` / `optional_from_named`(1555–1581)。
- `variant_to_named` / `variant_from_named`(+ `variant_from_at_named`、`variant_from_indexed_named`,1584–1651)。
- `tuple_to_named` / `tuple_from_named`(+ `tuple_to_seq_named`、`tuple_from_seq_named`,1654–1710)。
- `fixed_sequence_to_named` / `fixed_sequence_from_named`(1713–1750)— **注意:** 命名 `from` 不严格;填充 `min(src->size(), out.size())` 个元素,源较短时返回 `ok`(部分填充无错误),不同于普通严格 `fixed_sequence_from`。
- `carray_to_named` / `carray_from_named`(+ `carray_from_flat_named`,1755–1832)— 同嵌套后平展读出策略。
- `sequence_to_named` / `sequence_from_named`(1835–1881)、`map_to_named` / `map_from_named`(1884–1922)、`set_to_named` / `set_from_named`(1925–1960)。
- `queue_to_named` / `queue_from_named`(1963–2001)、`stack_to_named` / `stack_from_named`(2004–2042)、`priority_queue_to_named` / `priority_queue_from_named`(2045–2083)。

经命名入口触达的 ADL 钩子保留自己的键命名(以用户钩子 `from_value(src, out, e)` / `to_value(dst, in, e)` 调用,它拥有策略)。

## 7. 共享实数文本格式化(第 2178–2218 行)

`namespace detail { void format_real(double value, std::string& out); }` — 把 `double` 变成序列化文本的唯一地点,供每个格式适配器使用。用 `std::to_chars`(能往返的最短十进制,故 `3.14` 保持 `"3.14"`),最短 token 无 `.`/`e`/`E` 时补 `.0`,非有限值映射为确定性 token `nan` / `-inf` / `inf`(匹配旧 `%.17g` 输出,绝不空串)。JSON 在调用前把非有限值映射为 null;XML/INI 保留 `inf`/`nan` 文本。`detail::scalar_format_real`(第 227–253 行)为标量字符串转换器镜像此行为。

## 8. 数字/节点/反射格式化(`include/rtti/number_format.hpp`,第 1–162 行)

### 8.1 格式规格语法(spec §7.2)

```
format_spec := [[fill]align] [sign] ['#'] ['0'] [width] [','] ['.' precision] [type]
```

- `fill`:任意单字节后跟一个 `align` 字节。
- `align`:`<` 左,`>` 右(默认),`^` 居中,`=` 在符号后填充。
- `sign`:`+` 恒打符号;`-` 仅负数(默认);` ` 非负数以空格代符号。
- `#`:替代表现 — 非十进制整型基加 `0b`/`0o`/`0x`/`0X` 前缀(十进制或实型无效果)。
- `0`:零填充到 `width`(等价 fill `'0'` + align `=`)。
- `width`:最小总宽度(十进制位数)。
- `,`:千位分组;应用于十进制整数部分与定点实数(其它基/科学计数忽略)。
- `.precision`:整型 = 最小位数(零填充);实型 = `f`/`e` 小数点后位数(默认 6)。
- `type`:整型 `d`(默认)`b` `o` `x` `X`;实型 `f`/`F`(定点)、`e`/`E`(科学)、`g`/`G` 或空(最短往返文本)。

非法规格经可选 `error_info*` 出参报 `error_code::unexpected_token`,格式化函数返回**空串**(spec §0 失败安全,绝不崩溃)。

### 8.2 非模板核心(`namespace rtti::detail`,定义于 `src/number_format.cpp`)

```cpp
std::string format_number_unsigned(std::uint64_t value, string_view spec, error_info* err);
std::string format_number_signed(std::int64_t value, string_view spec, error_info* err);
std::string format_number_real(double value, string_view spec, error_info* err);
struct number_parts;   // 定义于 src/number_format.cpp(对头用户不透明)
number_parts parse_number_spec(string_view spec, bool is_floating, error_info* err);
```

`format_number_signed` 把 `-` 放在数字前(或零填充/align `=` 时放在零填充前)。

### 8.3 公开数字格式化

```cpp
template <typename T,
          std::enable_if_t<std::is_integral<T>::value &&
                               !std::is_same<T, bool>::value, int> = 0>
std::string format_number(T value, string_view spec = string_view(),
                          error_info* err = nullptr);
template <typename T,
          std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
std::string format_number(T value, string_view spec = string_view(),
                          error_info* err = nullptr);
```

按符号/浮点分派到核心的薄模板壳。来自 `tests/format_test.cpp` 的示例:`format_number(1234567, "#,d") == "1,234,567"`、`format_number(42, "08d") == "00000042"`、`format_number(255, "#x") == "0xff"`、`format_number(3.14159, ".2f") == "3.14"`、`format_number(1234.5678, "^10.2f")` 在宽度 10 内居中。

### 8.4 `to_string(v, format_spec)` 重载

```cpp
template <typename T, std::enable_if_t<
    (std::is_integral<T>::value && !std::is_same<T, bool>::value) ||
     std::is_floating_point<T>::value || is_date_time<T>::value, int> = 0>
std::string to_string(const T& v, string_view format_spec);
```

在既有标量转换表面之上应用格式规格的薄重载。规格是**必选**参数(无默认),故既有 `to_string<T>(v)`(`to_string.hpp`)永不遮蔽或歧义。整型/浮点走 `format_number`;`date_time` 走其 strftime 风格 `format(pattern)`(`<rtti/date_time.hpp>`)。

### 8.5 节点级字符串化

```cpp
std::string node_to_string(const value_io* node, string_view spec = string_view(),
                           error_info* err = nullptr);
```

定义于 `src/format_reflected.cpp`。把任意 `value_io` 节点序列化为单行紧凑文本(spec §7.3):对象 `{"k":v,...}`,数组 `[a,b,...]`,布尔/字符串/null 紧凑形,数字经格式规格(空规格 = 最短文本)。null 节点参数报 `no_such_field`。分配失败可抛 `std::bad_alloc`;其它失败不抛(错误时空串)。

### 8.6 反射驱动格式化

```cpp
template <typename T>
std::string format_reflected(const T& obj, string_view spec = string_view(),
                             error_info* err = nullptr);
```

遍历 T 的反射字段表(`RTTI_DEFINE_TYPE*`)并渲染一行紧凑对象;可选规格应用于每个数值字段值。零字段表渲染 `"{}"`。定义于 `src/format_reflected.cpp`(通用实现是非模板 `detail::reflected_fields_to_string`)。来自 `tests/format_test.cpp:329-359` 的示例:`format_reflected(pt) == "{\"x\":1,\"y\":2}"`、`format_reflected(st, ".2f") == "{\"id\":7,\"score\":3.50}"`、`format_reflected(big, "#,d") == "{\"x\":1,234,567,\"y\":2}"`。

*覆盖:`number_format.hpp` 第 1–162 行。*

## 9. 节点级转换桥(`include/rtti/node_io.hpp`,第 1–65 行)

每个格式文档都派生自 `value_io`,所以这些可直接用于 json/xml/ini/toml/proto 文档。

### 9.1 `locate`

```cpp
result<const value_io*> locate(const value_io& root, string_view path) noexcept;
result<value_io*> locate(value_io& root, string_view path) noexcept;
```

沿点分键加 `[index]` 路径的 const/可变树导航。路径文法:`step ('.' step)*`,其中 step 是一个键可选后跟一个或多个 `[index]` 组(`"a.b[2].c"`)或一个或多个前导根索引(`"[0][1].x"`)。空路径不选任何东西并返回错误。错误映射:缺键/越界索引 → `no_such_field`;把键导进非对象、把索引导进非数组,或任何畸形路径形状(空段、游离点、`.` 后的索引步、畸形括号)→ `kind_mismatch`。返回的指针借用树,树存活期间有效。`locate` 是 `noexcept`;其分词器分配小 scratch 向量,故 OOM(`std::bad_alloc`)会终止而非抛出。非模板,实现于 `src/reflect.cpp`。

示例(来自 `tests/node_locate_test.cpp:66-91`):

```cpp
auto d = rtti::json::parse("{\"user\":{\"name\":\"ada\",\"age\":42,"
    "\"addresses\":[{\"city\":\"Milan\",\"zip\":20121},{\"city\":\"Rome\",\"zip\":100}]}}");
const auto city = rtti::locate(d.value, "user.addresses[1].city");
// city.ok() true; node_as<std::string>(*city).value == "Rome"
```

### 9.2 `node_as<T>`

```cpp
template <typename T>
result<T> node_as(const value_io* node);
```

经既有 `to_value` 分派把定位节点转成现代 C++ 值(标量、容器、反射结构体)。null `node` 报 `no_such_field`;类别不匹配报转换器错误码。`T` 必须可默认构造(结果负载值初始化)。实现:`r.err.code = from_value(node, r.value);`(以 `*city` 调用,定位指针已解引用)。

### 9.3 `to_node`

```cpp
template <typename T>
error_code to_node(value_io& target, const T& v);
```

用 `from_value` 替换 `target` 下的子树;适配器 `make_*` 写侧重置目标先前子节点,故定位的标量/对象/数组节点被完全替换,拥有文档把新值序列化出来。返回转换器错误码。实现:`return to_value(&target, v);`。

## 10. 内存设施(`include/rtti/memory.hpp`,第 1–85 行)

### 10.1 `string_view` 别名

```cpp
using string_view = std::string_view;
```

零拷贝视图别名。所有字符串数据由解析缓冲或 arena 拥有;视图从不拥有内存。

### 10.2 `class memory_arena`(高吞吐 bump/块 arena)

```cpp
class memory_arena {
public:
    memory_arena() noexcept = default;
    ~memory_arena();
    memory_arena(memory_arena&& other) noexcept;
    memory_arena& operator=(memory_arena&& other) noexcept;
    memory_arena(const memory_arena&) = delete;              // 不可拷贝
    memory_arena& operator=(const memory_arena&) = delete;

    void* alloc(size_t size, size_t align = alignof(std::max_align_t));
    template <typename T, typename... Args> T* alloc_obj(Args&&... args);
    const char* dup(const char* data, size_t n);             // NUL 结尾复制
    const char* dup(const char* s);                          // strlen 复制,NUL 结尾
    void reset() noexcept;                                   // 释放全部块;可复用
    size_t total_bytes() const noexcept;                     // 块间预留总量
};
```

语义:

- 内存从链块(默认每块 **64 KiB**)切出。小分配在当前块内 bump 游标;超大请求拿独立块。整个 arena 在析构或 `reset()` 时一次性释放,使解析期分配成本近零。
- `alloc` 对零尺寸与分配失败(底层 `malloc` 返 null)返回 `nullptr`;**调用方必须检查结果**。
- `alloc_obj<T>` 原位值初始化对象(带转发参数的 placement-new);分配失败可返 null。
- `dup` 把 `n` 字节/一个 C 字符串复制入 arena 并返回 NUL 结尾指针。
- 可移动构造/赋值;复制被删除。
- **线程:非线程安全** — 每文档一个 arena。

*覆盖:`memory.hpp` 第 1–85 行。*

---

## 11. 值语义强转桥 — `rtti::convert::as` / `cast` / `to`(`convert.hpp` 第 2244–2818 行)

`namespace rtti::convert` 暴露*值语义强转桥*——覆盖第 3–4 章标量道的数值/文本
「值桥」。它转换**值**而非指针,因此绝不干扰 `reflection.md` §14 中 C# 风格的
指针 `rtti::as<T>(U*)` / `rtti::cast<T>(U*)` 转换(该桥 SFINAE 掉非 `value_io`
指针;无关指针源是硬编译错误,而非静默通道)。

```cpp
// 宽松转换:任何失败均返回 nullopt,绝不抛异常。
template <typename T, typename Src,
          std::enable_if_t<detail::bridge_ok<T, Src>::value, int> = 0>
std::optional<T> as(Src&& src) noexcept;

// 严格转换:result<T>,带回可区分的 error_code。
template <typename T, typename Src,
          std::enable_if_t<detail::bridge_ok<T, Src>::value, int> = 0>
result<T> cast(Src&& src) noexcept;

// 统一入口:与 cast 相同。
template <typename T, typename Src,
          std::enable_if_t<detail::bridge_ok<T, Src>::value, int> = 0>
result<T> to(Src&& src) noexcept;
```

| 入口 | 返回 | 失败语义 |
| --- | --- | --- |
| `as<T>(src)` | `std::optional<T>` | 任何失败均返回 `nullopt`(宽松)。 |
| `cast<T>(src)` | `result<T>` | `kind_mismatch`(道不兼容)、`overflow`(数值范围,向零截断,绝不回绕)、`invalid_number`(畸形数字 / 枚举标签文本)、`no_such_field`(null / null-kind 源)。 |
| `to<T>(src)` | `result<T>` | `cast` 的别名(单一统一入口)。 |

**源**(由 `detail::extract_lane` 决定):原生标量(`bool`、整型、浮点)、
`string_view` / `std::string`、`field_value`,以及 `value_io` 派生指针(含
`const`)。**目标**(`detail::bridge_ok` + `detail::is_scalar_target` /
`detail::is_vector_of_scalar`):标量族(`bool`、整型、浮点、`std::string` /
`string_view`、已注册枚举)与单层 `std::vector<scalar>`。

**宽松 vs 严格** — 刻意分层:`rtti::from_string<T>`(to_string.hpp)是*严格解析器*
(整个输入必须是良构字面量,故 `from_string<int>("3.9")` 报 `invalid_number`);
该桥刻意*宽松*(它转换一个值,故 `as<int>("3.9")` 截断实数道返回 `3`)。两个入口
对同一输入有意分歧(见 to_string.hpp 中镜像注释)。

示例(来自 `tests/convert_cast_test.cpp`):

```cpp
auto a = rtti::convert::as<int>(" 42 ");   // std::optional<int>{42};文本已修剪
auto b = rtti::convert::cast<int>(3.9);    // result<int>,b.value == 3(截断)
auto c = rtti::convert::cast<std::int8_t>(std::int64_t(300)); // !c.ok():overflow
auto d = rtti::convert::cast<int>(nullptr); // !d.ok():no_such_field
auto e = rtti::convert::to<int>("42");      // cast 的别名
rtti::convert::as<std::vector<int>>(value_io_array_node); // 单层 vector
```

## 交叉引用

- 英文版:[`docs/interface/en/convert.md`](../en/convert.md)(English version)