# 协议适配器 — 接口参考(protobuf / FlatBuffers)

> 模块:`rtti::proto`(protobuf)、`rtti::flat` / `rtti::flat_schema`(FlatBuffers)
> 文件:`include/rtti/proto.hpp`、`proto_schema.hpp`、`flat.hpp`、`flat_schema.hpp`
> 版本:中文。English edition: [`../../en/proto-flat.md`](../en/proto-flat.md)。
> 本章 API 提取自上述头文件;行号对应当前工作树。

## 0. 总览

protobuf 适配器为反**射 C++ struct 注册一个 schema**,提供类型化(对象)与树
(`value_io`)两类编解码器,外加 SAX 流式解析。FlatBuffers 适配器从 `.fbs` 风格
源码构建 flat schema,并按官方 FlatBuffers wire 布局(反)序列化类型化对象与
`value_io` 树。

**门控**:`RTTI_ENABLE_PROTO`(`proto.hpp`、`proto_schema.hpp`)、
`RTTI_ENABLE_FLAT`(CMake 选项;flat.hpp/flat_schema.hpp)。`.proto` 文件解析
**不在范围内** — 消息以 C++ 注册(spec §13.3 倾向 schema 注册)。

---

## 1. protobuf(`rtti::proto`)

`include/rtti/proto.hpp`(2910 行)。由 `RTTI_ENABLE_PROTO` 门控。

### 1.1 Wire 原语(proto.hpp:85-155)

```cpp
enum class syntax : std::uint8_t { proto2, proto3 };
enum class field_mode : std::uint8_t { optional, required, repeated };
enum class wire_type : std::uint8_t { varint = 0, fixed64 = 1, length = 2, fixed32 = 5 };

std::uint32_t make_tag(std::uint32_t number, wire_type wt) noexcept;  // (number<<3)|wt
bool encode_varint(std::uint64_t value, std::string& out);
bool decode_varint(rtti::string_view src, std::uint64_t& value, std::size_t& used);
std::uint64_t zigzag_encode(std::int64_t v) noexcept;   // 规范 (v<<1)^(v>>63)
std::int64_t  zigzag_decode(std::uint64_t u) noexcept;
```

Varint 解码全程边界检查;第 10 字节只允许 bit 63 有效、bit 64..69 必须为 0,
超过 10 字节的连续位失败(`proto_truncated`)。zig-zag 在无符号域运算,故对每个
`int64_t` 输入定义良好。

### 1.2 `field_desc` 与描述符构建器(proto.hpp:295-359)

```cpp
struct field_desc {
    const char* name; std::uint32_t number; wire_type wire;
    field_mode mode; bool packed; bool zigzag;
    bool has_default; std::int64_t dflt_int; double dflt_real;
    const char* dflt_str; std::size_t dflt_str_len;
    std::uint32_t oneof_group;   // 非零 = 该 oneof 组成员
    bool emit_present;           // 显式存在性(map 键值、proto2 optional)
};

field_desc make_field_desc(const char* name, std::uint32_t number, wire_type wt,
                           field_mode mode = field_mode::optional, bool packed = false);
field_desc with_default_int(field_desc d, std::int64_t v);
field_desc with_zigzag(field_desc d);               // sint32/sint64
field_desc in_oneof(field_desc d, std::uint32_t g); // 单一活跃字段组
field_desc with_default_real(field_desc d, double v);
field_desc with_default_str(field_desc d, const char* s, std::size_t n);
```

### 1.3 `schema<T>` / `schema_info` / `schema_of`(proto.hpp:431-450)

```cpp
template <typename T>
struct schema {
    static constexpr std::size_t count = 0;           // 消息需特化
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[1] = {nullptr};
};
struct schema_info { syntax syn; std::size_t count; const field_base* const* fields; };
template <typename T> inline const schema_info* schema_of();
```

主 `schema<T>` 是空 proto3 消息:未注册类型序列化为零字节、解析为默认构造对象。
特化 `schema<T>` 注册消息——填入由 `make_field`/`make_field_desc` 构建的
`field_base*` 数组(repeated 成员解码进 `std::vector` 等)。

`field_base`(proto.hpp:374-425)是类型擦除的逐字段编解码器,含类型化路径
(`encode_obj`/`decode_obj`/`decode_packed`/`decode_repeated`/`should_encode`/
`set_default`)与树路径(`tree_encode`/`tree_decode`/`tree_decode_packed`/
`tree_decode_repeated`/`tree_equals_default`/`tree_set_default`)。

### 1.4 类型化编解码(proto.hpp:516-538)

```cpp
template <typename T> error_code encode_message(const T& obj, detail::wire_writer& w,
                                                std::size_t depth, std::size_t max_depth);
template <typename T> error_code decode_message(T& obj, detail::wire_reader& r,
                                                std::size_t depth, std::size_t max_depth);
```

`detail::wire_reader` / `detail::wire_writer`(proto.hpp:161-276)提供有界读取
(`remaining()`、`offset()`、`read_varint` 等)与写入(`write_varint`、
`write_tag`、`write_bytes`、`write_fixed*` 等)。每次读取都先查末端,畸形输入
绝不越界读。

### 1.5 树桥(`value_io`)(proto.hpp:637-655,2110-2530)

```cpp
error_code encode_message_tree(const rtti::value_io* obj, detail::wire_writer& w,
                               std::size_t depth, std::size_t max_depth);
error_code decode_message_tree(rtti::value_io* obj, detail::wire_reader& r,
                               std::size_t depth, std::size_t max_depth);
error_code decode_generic(rtti::value_io* obj, detail::wire_reader& r,
                          std::size_t depth, std::size_t max_depth);
// 扩展路径助手:
error_code ext_read_value(...);   error_code ext_write_value(...);
error_code decode_extension_field(rtti::value_io* obj, ...);
error_code encode_extension_field(const rtti::value_io* node, ...);
```

### 1.6 扩展(proto.hpp:1716-1832)

```cpp
struct extension_desc;                      // 外部消息中的字段号
class extension_registry;                   // 解码/编码查找
inline error_code register_extension(const extension_desc& e);
inline void clear_extensions() noexcept;
```

以此注册的扩展会参与目标消息的 `tree` 解码/编码。

### 1.7 `document` 与 `node`(proto.hpp:1842-2108)

`detail::node` 与 `class document` 是 wire↔树桥与 DOM 解析结果所用的 `value_io` 树。

### 1.8 公共入口(proto.hpp:2669-2814)

```cpp
struct parse_config { /* 深度预算,如 max_depth */ };

template <typename T> error_code serialize(const T& obj, std::string& out);
template <typename T> std::string serialize(const T& obj);
template <typename T> result<T> parse_as(rtti::string_view src);
template <typename T> result<T> parse_as(rtti::string_view src, const parse_config& cfg);

inline error_code wire_to_tree(rtti::string_view src, const schema_info* si,
                               rtti::value_io* out);   // 进树
template <typename T> error_code wire_to_tree(rtti::string_view src, document& doc);
template <typename T> error_code tree_to_wire(const rtti::value_io* root, std::string& out);
inline result<document> parse(rtti::string_view src);
inline result<document> parse(rtti::string_view src, const schema_info* si);

struct parse_events { /* SAX:message_start/message_end/field_*/ };  // proto.hpp:2794
```

支持 wire 0(varint)、1(fixed64)、2(length)、5(fixed32);packed 数值字段与
oneof 组遵循 protobuf 语义。

---

## 2. `.fbs` schema 读取器(`rtti::flat_schema`)

`include/rtti/flat_schema.hpp`。由 `RTTI_ENABLE_FLAT` 门控。

### 2.1 Schema 描述符(flat_schema.hpp:89-180)

```cpp
enum class fbs_type : std::uint8_t { /* 标量/struct/table/vector/union/string/… */ };

struct fbs_field_desc;      struct fbs_enum_value_desc;
struct fbs_enum_desc;       struct fbs_union_variant_desc;
struct fbs_union_desc;      struct fbs_agg_desc;
struct fbs_schema_desc;

result<fbs_schema_desc> parse_schema(rtti::string_view src);
```

### 2.2 编译与查询(flat_schema.hpp:188-223)

```cpp
struct compile_config;                    // 如 root_type、identifier
class compiled_schema;                    // 已解析 schema + 偏移表
result<compiled_schema> compile_schema(const fbs_schema_desc& desc,
                                       const compile_config& cfg);
result<compiled_schema> compile_schema(rtti::string_view src,
                                       const compile_config& cfg);
```

### 2.3 `flat.hpp` 中的 `schema<T>`(flat.hpp:103-366)

```cpp
enum class fkind : std::uint8_t { /* 标量种类 */ };
struct union_variant_entry { /* enum -> field_desc */ };
struct field_desc { /* name、slot(vtable 索引)、type、default */ };

field_desc make_field_desc(const char* name, std::uint16_t slot, …);
field_desc make_union_value(const char* name, std::uint16_t slot, …);
field_desc with_default_int(field_desc d, std::int64_t v);
field_desc with_default_real(field_desc d, double v);

template <typename T> struct schema;
template <typename T> struct schema_identifier_of;   // T::identifier
template <typename T> inline const schema_info* schema_of();
template <typename E> inline union_variant_entry variant_of(E e);

class document : public rtti::value_io;               // flat 树(flat.hpp:242)
struct parse_config;                                   // root type / identifier

result<document> parse(rtti::string_view src, const schema_info* si, …);
inline error_code wire_to_tree(rtti::string_view src, document& doc, …);
inline error_code tree_to_wire(const rtti::value_io* root, std::string& out, …);
template <typename T> inline error_code serialize(const T& obj, std::string& out);
template <typename T> inline std::string serialize(const T& obj);
template <typename T> inline result<T> parse_as(rtti::string_view src);
template <typename T> inline result<T> parse_as(rtti::string_view src, const parse_config& cfg);
```

生成布局遵循官方 FlatBuffers wire 规则(vtable、SOFFSET/offset 字段、64 位
标量 8 字节对齐、table slot-0 存在性),因此缓冲可与参考 `flatc`(`--json`)往返。

---

## 3. 此处使用的错误码

- proto:`proto_truncated`、`proto_bad_field`、`proto_type_mismatch`,以及共享的
  `too_deep` / `no_such_field` / `kind_mismatch`。
- flat:共享 `error_code` 集合(invalid_root / too_deep / no_such_field /
  kind_mismatch)。

完整枚举见 [error.hpp](../../../include/rtti/error.hpp)。

## 备注/局限(已文档化)

- `.proto` 文件解析器 out of scope;`schema<T>` 注册是支持路径。
- zig-zag 与 varint 解码严格有界(无越界读、无静默 64 位溢出)。
- 跨命名空间/点引用与重定义规则以官方 protobuf / FlatBuffers 为权威;差异
  (重定义 vs 报错约定)已在头注释文档化。

## 版权说明

本参考两版仅描述公开接口。许可证见 [LICENSE](../../../LICENSE)。