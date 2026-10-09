# RTTI — C++17 运行时类型信息库

[![CI](https://github.com/OWNER/REPO/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/ci.yml)

[English](README.en.md) | English edition: [README.en.md](README.en.md)

RTTI 是一个零依赖、可移植(MSVC / GCC / Clang)、基于 C++17 的运行时反射库,
弥补 C++ 缺少一等反射的短板。过去需要大量样板代码的工作——遍历字段、
(反)序列化、运行时自省——现在只需一个宏。

## 目录

- [简介](#简介)
- [快速上手](#快速上手)
- [核心能力](#核心能力)
- [变体转换](#变体转换)
- [格式适配器](#格式适配器)
- [架构](#架构)
- [支持的容器](#支持的容器)
- [示例程序](#示例程序)
- [构建配置与剥离矩阵](#构建配置与剥离矩阵)
- [可移植性](#可移植性)
- [文档](#文档)

## 简介

标准 C++11 的 `typeid` 只能给出一个 mangled 名称。RTTI 在它之上构建了实用的
字段反射层:

- **注册**结构体使用 `RTTI_DEFINE_TYPE` 宏族(外部 / 侵入式 / 派生 / 带默认键
  变体),枚举使用 `RTTI_DEFINE_ENUM`。
- **自省**在运行时:字段数量、名称、类型(.NET 级体验的 `rtti::get_type`、
  `find_field`、`get_field`、`set_field`,以及字段的 range-for 遍历)。
- **转换**在结构体与格式无关的 `value_io` 树之间递归进行(`from_value` /
  `to_value`),覆盖标量、`std::string`,以及标准容器家族——并透传 allocator
  与比较器。
- **解耦**格式:反射核心绝不 include 任何格式头;JSON/XML/INI/TOML/protobuf
  全部位于可选的编译门之后。

除基础注册之外,库还提供一组独立的核心能力(类型判断、Attribute 特性系统、
反射实例化、反射迭代器、`date_time`、任意进制转换、统一 `to_string`、键命名
策略与 IoC 容器)——见 [核心能力](#核心能力)。

整个库以"反射核心与模板桥接的头文件 + 集中于 `src/` 的一次性静态库实现"交付:所有非模板实现——四个文本适配器的 DOM/解析/序列化实体、`.proto` 文本解析器,以及其余全部支撑代码——都被编入 `rtti` 静态库的 19 个编译单元(`src/*.cpp`)。无第三方依赖,无非标准编译器内置函数。

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
派生注册(`RTTI_DEFINE_TYPE_DERIVED`)把单继承基类链纳入反射的 `is-a` 机制。

## 核心能力

除字段注册之外,反射核心还提供一组独立、开箱即用的便捷能力。它们全部是
失败安全(`error_code` / `result<T>`,绝不抛异常)、与格式无关;其模板壳保持
头文件式,非模板实体一次性编入 `src/` 静态库。

### 类型判断 — `<rtti/reflect.hpp>`

`namespace rtti` 内的编译期别名:

- `is_same<T,U>`、`is_base_of<Base,Derived>`、`is_convertible<From,To>` 与
  `can_cast<From,To>`(均含 `_v` 变量模板)。

针对反射对象的运行期判断(spec 19.2):

- `is_instance_of<T>(obj, type_id)` — 动态或静态的精确实例检查;
- `is<T>(instance)` — 句柄的精确运行期类型比较;
- `is_derived_from<T>(instance)` — 实例运行期类型为 T 或经反射基类链传递派生
  时为真;
- `dynamic_cast_ref<T>(instance)` — 在 type-id 链验证后把句柄反射为 T 视图;
  不安全的转换以 `error_code` 失败;
- `can_convert(src, target)` / `can_convert(instance, target)` — type_desc /
  实例的可转换性(相同、派生或标量可转换)。

`type_desc` 还暴露 `is_abstract()` / `is_polymorphic()` 访问器;`reflection.hpp`
另提供编译期别名 `is_abstract` / `is_polymorphic` / `has_virtual_destructor`
(均含 `_v` 变量模板)。

注:`dynamic_cast_ref` 仅沿注册的非虚单继承链有保证(虚继承 / 多基是未支持的
角落);侵入式(`RTTI_DEFINE_TYPE_INTRUSIVE`)类模板类型的运行期字段访问只在
main 运行之后可靠,静态初始化期内不可靠。

### Attribute 特性系统 — `<rtti/reflection.hpp>`

编译期注解框架,运行期查询。用 `RTTI_DEFINE_TYPE_ATTRS(T, ...)` 注册类型级
特性,用 `RTTI_DEFINE_FIELD_ATTRS(T, ...)` 注册字段级特性,每个特性写作
`RTTI_ATTR("名称", args...)` / `RTTI_FIELD_ATTR_ENTRY(field, attrs...)`。
参数是多态的整数 / 实数 / 字符串 / 布尔(`attr_arg`),可任意混排。

类型级查询:`attr_count<T>()`、`attr_at<T>(i)`、`find_attr<T>(name)`、
`has_attr<T>(name)`。字段级查询:`field_attr<T>(字段|下标)`、
`field_attr_count<T>`、`field_attr_at<T>`、`field_find_attr<T>`、
`field_has_attr<T>`。

### 反射实例化 — `<rtti/reflection.hpp>`

在运行时构建反射类型。

- `RTTI_DEFINE_CTOR(T, ("参数", Type, optional), ...)` 注册反射构造器注册表
  (`ctor_registry<T>`)。
- `make_object<T>()`、`make_object<T>({ctor_arg::...})` 与 `create_instance<T>`
  返回持有式 `object_ref<T>`;`ctor_arg::scalar` 与 `ctor_arg::node` 支持按位置
  或按名称(乱序)注入。
- `make_object<T>(alloc, ...)` 用调用方 allocator 在原始内存上构造
  (placement-new;手动、幂等的 `destroy()`)。
- `object_ref<T>` 句柄覆盖 `get`/`set` 字段操作、`field_size`、
  `add`/`erase_at`、元素迭代、特性查询与 `type_of()`。
- 一切失败都以 `error_code`(`missing_key`、`kind_mismatch`、…)返回,绝不抛
  异常。

### 反射迭代器

- 字段数组: `field_begin` / `field_end`、`field_rbegin` / `field_rend`
  (逆向),另有裸 `begin`/`end`/`rbegin`/`rend` 别名。边界位置解引用安全返回
  `nullptr`。
- 容器元素迭代器:`element_begin` / `element_end` / `element_rbegin` /
  `element_rend` 遍历容器字段,提供三种访问形态——标量 `element_value()`、
  通用 `element_to(value_io*)`、位置 `index()` / `key()`。越界访问安全,绝不
  崩溃。

### 方法反射与模板实参反射 — `<rtti/reflection.hpp>`

- 用 `RTTI_DEFINE_METHOD`(成员)/ `RTTI_DEFINE_STATIC_METHOD`(静态)注册反射
  方法,用 `methods<T>()` / `method_count<T>()` 枚举,再用 `reflect_method`
  重载族(实例、`object_ref<T>`、`instance` 句柄,以及静态
  `reflect_method<T>(name, args...)`)派发调用。
- `RTTI_TEMPLATE_INFO(T, ...)` 记录模板实例的显式实参表:用
  `template_arg_count<T>()`、`template_args<T>`、`template_arg_at<T, I>`、
  `template_args_of<T>()`、`template_arg_desc_of<T>(i)` 查询。
- ORM 便捷能力(`<rtti/reflect.hpp>`):由一列 `row_cell` 加
  `bind<T>(obj, row, count)` / `bind<T>(obj, {row_cell...})` 把字段/值单元格
  填入反射对象。

详见 `examples/reflection_demo.cpp` 与对应头文件注释。

### `date_time` — `<rtti/date_time.hpp>`

以 Unix 纪元纳秒(`int64`)存储的值类型:

- 精度感知工厂 `from_unix_seconds` / `_ms` / `_us` / `_ns`(范围检查,越界报
  `overflow`);
- ISO 8601 `parse(string_view)` 与规范 UTC 的 `format()` / `to_string()`
  (精确往返,公历换算);
- `epoch_ns_from_digits` 按十进制位数把数值纪元映射为纳秒(10=秒、13=毫秒、
  16=微秒、19/20=纳秒);
- 读侧访问器 `to_unix_seconds` / `_ms` / `_us` / `_ns`(向负无穷地板除)与原始
  `epoch_ns()`——`rtti::is_date_time<T>` 与 `to_value_dt` 转换桥把该类型接入
  convert 层;
- 完整比较运算与纳秒级算术。

### `base_convert` — 任意进制整数

`<rtti/base_convert.hpp>`(`rtti::detail`)及其 to_string 包装:

- `to_base` / `to_base_signed` — `uint64_t` / `int64_t` 转为 2~64 任意进制
  数字串;
- `from_base` / `from_base_signed` — 字符串转整数,带溢出检测与非法字符报错;
- 自定义数字表(`default_digits()`、`rfc4648_digits()`)与大小写策略;
- 有符号变体携带前导 `-`。

### `to_string` — `<rtti/to_string.hpp>`

- 标量、枚举、`date_time`、任意进制整数、容器与反射结构体的统一
  `T ↔ std::string` 互转;
- `to_string(v)` 与 `from_string<T>(s, out)` / `from_string<T>(s)`
  (后者返回 `std::optional<T>`);
- 便捷包装 `to_string_base` / `from_string_base` 及其有符号变体,覆盖进制转换
  层。

### 字符串格式化 — `<rtti/number_format.hpp>`,`<rtti/date_time.hpp>`

格式化驱动的文本渲染(spec §7),全部以 `error_code` 失败安全:

- `format_number<T>(v, spec)` — Python format 风格格式串,整数
  (进制 `d/b/o/x/X`、千分位 `,`、`#` 备用形式、宽度、填充、对齐、符号、
  前导零)与实数(`f/e/g` 与精度、分组、宽度);默认最短往返文本;
- `to_string<T>(v, format_spec)` — 薄重载,把整数/实数路由到
  `format_number`,`date_time` 路由到其 `format(pattern)`;
- `date_time::format(pattern)` — strftime 风格模式
  (`%Y %y %m %d %H %M %S %f %F %T %R %z %Z %%`);未知指令报
  `unexpected_token`;
- `node_to_string(const value_io*, spec)` — 任意 value_io 节点(对象/数组/
  标量)到单行文本,数字按 spec 格式化;
- `format_reflected<T>(obj, spec)` — 遍历 `RTTI_DEFINE_TYPE` 字段表渲染
  紧凑对象行,spec 应用于每个数字字段。畸形 spec 绝不抛异常:经可选的
  `error_info*` 返回空串 + `error_code::unexpected_token`。

### 节点级转换 — `<rtti/node_io.hpp>`

在任意 `value_io` DOM 之上的格式无关路径导航:

- `rtti::locate(root, "user.addresses[1].city")` — 点分键 + `[index]`
  组;缺键 / 越界下标 / 畸形路径报 `no_such_field` 或 `kind_mismatch`
  (绝不崩溃)。提供 const 与可变两个重载;
- `rtti::node_as<T>(node)` — 把定位节点经转换层转成 C++ 值
  (`result<T>`);
- `rtti::to_node(target, value)` — 用 C++ 值经 `from_value` 替换目标节点
  的子树,宿主文档随后整体序列化为新内容。`locate` 实现在
  `src/reflect.cpp`;两个模板是 `convert.hpp` 之上的薄壳。

### `rtti::str` — 全类型字符串转换 + 字符串操作

`<rtti/string_ops.hpp>` 在 `namespace rtti::str` 下汇集转换与操作:

- 转换壳 `to_string` / `from_string` 是既有统一 `rtti::to_string` /
  `from_string` 层的 using 别名,因此标量、枚举、`date_time`、容器、
  optional/variant/tuple/pair/array 与反射结构体都经过同一实现互转;
  `to_base` / `from_base`(及有符号孪生)桥接任意进制辅助(bases 2..64);
- 字符串操作(非模板,在 `src/string_ops.cpp`):
  `split` / `join`、`trim` / `ltrim` / `rtrim`、`to_lower` / `to_upper`、
  `starts_with` / `ends_with`、`replace` / `replace_all`、`repeat`、
  `pad_left` / `pad_right`、`reverse`、`quote` / `unquote`、
  `escape` / `unescape`;
- 纯字符串变换 `noexcept`;解析类(`unquote` / `unescape` / `from_base` /
  `from_string`)以 `error_code` 报错,绝不崩溃(spec §0 fail-safe)。

### `naming` — 键命名策略 `<rtti/naming.hpp>`

由各文本适配器 `write_config` 携带、与格式无关的键命名策略:

- `key_case`: `identity`(字段名原样)、`snake` 或 `camel`;
- `rename_map`: 显式的字段名 → 输出键覆盖表,永远优先;
- `skip_empty`: 序列化时丢弃值为空(空字符串 / 空容器、数值零或 null)的字段;
- 辅助函数 `to_snake_case` / `to_camel_case`、`naming_policy`、`apply_naming`。

### IoC 容器 — `<rtti/ioc.hpp>`

`rtti::ioc::container` 依赖注入容器:

- `register_type<T>`、`register_as<Iface,Impl>`(接口到实现绑定)、
  `register_factory<T>`;
- `resolve<T>` / `get<T>` 返回 `result<object_ref<T>>`,支持 `singleton` /
  `transient` 生命周期;
- 构造器依赖注入:`T`、`T*`/`T&`、`std::shared_ptr<T>`;循环检测以
  `kind_mismatch` 安全失败。

## 变体转换

`rtti::as<T>` 与 `rtti::cast<T>` 对应 C# 的引用转换:

- `as<T>(x)` 返回 `T*`,无法转换时返回 `nullptr`。
- `cast<T>(x)` 返回 `result<T*>`;转换失败设 `error_code::kind_mismatch`
  (空源设 `no_such_field`)。

三者均支持:原始指针、`object_ref<U>`、类型擦除的 `instance`。协变上转
(派生到基)编译期正确,含虚基类与多继承,与是否多态无关。逆变下转运行时
检查,要求源多态;非多态源干净失败。const 限定的源或目标类型在编译期被
拒绝,绝不返回错误偏移的指针。

类型擦除 `instance`:运行时动态类型经反射基链解析(偏移正确);动态类型链
不完整(其描述符从未物化)时回退到存储静态类型的基链,故到存储静态基的合法
上转不会误失败;未反射的动态类型仅能按其精确类型命中。因擦除后的源类型非
编译期多态类类型,`instance` 路径无法使用 `dynamic_cast`;此类不可达转换干净
失败,绝不返回错误指针。

## 格式适配器

JSON/XML/INI/TOML/FlatBuffers 是可选的编译门。开启时四个文本适配器形态
一致——持有的 DOM(`document : value_io`)、`parse`/`serialize` 一对、类型桥
`parse_as<T>` / `serialize<T>(obj)`、流式 SAX `parse_stream`,以及
`write_config`(键命名 / rename / skip_empty)。protobuf 与 flatbuffers 适配器
(`<rtti/proto.hpp>`、`<rtti/flat.hpp>`)同样以编译门控制,但通过 `parse_config`
配置(没有 `write_config`)。模板桥接层保持头文件;四个文本适配器与 `.proto`
解析器是非模板实现,一次性编入 `src/*.cpp`。protobuf 的 wire codec 仍为头文件
内联模板(`<rtti/proto.hpp>`);flatbuffers 适配器是例外——其 wire codec 与
`.fbs` 解析器为非模板实体,编入 `src/flat.cpp` / `src/flat_schema.cpp`。
这些适配器反射核心永不依赖。

### JSON — `<rtti/json.hpp>`

完整 JSON DOM:`rtti::json::document`、`parse` / `serialize`、`parse_as<T>` /
`serialize<T>`、`parse_stream`,`write_config` 支持缩进 / `sort_keys` /
`indent_char` 与键命名(`key_case` / `rename_map` / `skip_empty`)。

### XML — `<rtti/xml.hpp>`

持有型 XML DOM,支持 prolog/epilog 节点:`parse` / `serialize`、
`parse_as<T>`、`parse_stream`,`write_config`(美化 / 缩进 / 命名)。

### INI — `<rtti/ini.hpp>`

基于 section 的 DOM:`parse` / `serialize`、`parse_as<T>`、`parse_stream`,
`write_config` 命名策略。

### TOML — `<rtti/toml.hpp>`

TOML v1.0 核心子集适配器:

- DOM `parse`、类型化 `parse_as<T>`、流式 `parse_stream`(SAX);
- `serialize` 重载:`(doc)`、`(doc, out, cfg)` 缓冲区形式(先清空再写入)与
  `serialize<T>(obj)`;日期时间标量原样往返、不加引号;
- 标量(基本 / 多行 / 字面量字符串,十进制/十六进制/八进制/二进制整数,
  含 inf/nan 的浮点,布尔)、数组、内联表、点号键与 `[table]` /
  `[[数组表]]` section;重复键后者胜出;畸形输入返回 `error_code`,
  `parse_config::max_depth` 防护 `too_deep`。

### protobuf — `<rtti/proto.hpp>`,`<rtti/proto_schema.hpp>`

完整的 protobuf 2/3 适配器,以官方 `protoc` fixture 逐字节对拍验证:

- 线格式编解码:varint(线 0,`sint32`/`sint64` 走 zig-zag)、固定 64 位(1)、
  长度前缀(2)、固定 32 位(5);有界编解码,截断输入 → `proto_truncated`;
- 显式 `rtti::proto::schema<T>` 把每个反射字段映射为 `field_desc`
  (protobuf 字段号、`wire_type`、`field_mode` optional/required/repeated、
  `packed`、proto2 `with_default_int/real/str`、`with_zigzag` 有符号
  zig-zag varint);
- 类型桥 `serialize<T>` / `parse_as<T>`:proto2 必填缺失 → `missing_key`,
  缺省字段套用显式默认值,proto3 标量默认值不写上线;repeated 字段同时接受
  packed 与非 packed 布局;
- **.proto 文本解析器** `<rtti/proto_schema.hpp>`:`parse_schema` +
  `compile_schema` 覆盖完整 proto2/v3 语法——标量(全类型,含 `sint`)、
  `map<K,V>`、`oneof`、`import`、`reserved`、枚举默认值、字节转义
  (`\xNN` / `\uXXXX`)、嵌套消息与字段号校验(冲突/重用 →
  `proto_bad_field`);
- **extensions**:proto2 `extensions` 区间 + `extend` 声明注册进运行时
  `rtti::proto::extension_registry`(`register_extension`);未知 wire 字段号
  保留跳过,已注册的按具名字段解析(官方 `ExtensionRegistry` 语义);
- 树桥 `wire_to_tree` / `tree_to_wire` 把任意 value_io 树与线格式互接;
  `parse`(无 schema 或有 schema)与 `parse_stream` 提供 SAX 事件
  (`message_start` / `message_end` / 类型化字段事件);
- DoS 防护:嵌套 `max_depth` 与长度前缀有界;字段号 0 与 group 线类型 3/4 以
  `proto_bad_field` 拒绝,schema/流线格式失配 → `proto_type_mismatch`(绝不
  崩溃)。

### FlatBuffers — `<rtti/flat.hpp>`,`<rtti/flat_schema.hpp>`

FlatBuffers 线格式适配器(spec §14),以官方 `flatc` fixture 对拍验证:

- **线编解码**(非模板,在 `src/flat.cpp`):root uoffset、table vtable 遍历、
  按 1/2/4/8 对齐的标量(带 schema 默认值)、字符串、向量、内联 struct、
  union(U8 type 槽 + 配对的 variant 表)与可选 file identifier。畸形/截断/
  越界输入返回 `error_code`,绝不崩溃;
- `rtti::flat::schema<T>` 注册:每个反射字段 NAME 映射到 vtable SLOT 与 wire
  `fkind`(标量、`string`、`vector`、`table`、struct、union);缺省的 vtable
  槽填充 schema/通用默认值;
- 类型桥 `flat::serialize<T>` / `flat::parse_as<T>` 走转换器反射链;
  `flat::wire_to_tree` / `tree_to_wire` 互接任意 value_io 树;
- **.fbs 文本解析器** `<rtti/flat_schema.hpp>`:`flat::parse_schema` +
  `compile_schema` 把 `.fbs` 描述(namespace / `table` / `struct` / `enum` /
  `union` / `root_type` / `file_identifier` / 属性 / 默认值)编译为运行时的
  `schema_info`,驱动与 `schema<T>` 注册完全相同的编解码链路;
- DoS 防护:`max_depth` 限制嵌套 table(`too_deep`);
  `verify_file_identifier` 校验 4 字节 magic。

## 架构

三个协作层,依赖方向严格单向——适配器只能 `#include` 核心,绝不允许反向。

| 层 | 头文件 | 职责 |
| --- | --- | --- |
| 反射核心 | `reflection.hpp` `reflect.hpp` `convert.hpp` `memory.hpp` `error.hpp` `rtti_fwd.hpp` `date_time.hpp` `base_convert.hpp` `to_string.hpp` `number_format.hpp` `string_ops.hpp` `node_io.hpp` `naming.hpp` `ioc.hpp` | 字段表、宏、查询 API、递归转换、类型判断、特性、反射创建、迭代器、日期时间、进制转换、统一 to_string + 格式串、字符串操作、节点级 locate/node_as/to_node、命名、IoC |
| `value_io` 抽象 | `convert.hpp` | 格式无关的树接口(`kind`、`at`、`get_key`、`make_scalar`、…),由各适配器实现 |
| 格式适配器 | `json.hpp` `xml.hpp` `ini.hpp` `toml.hpp` `proto.hpp` `proto_schema.hpp` `flat.hpp` `flat_schema.hpp` | JSON / XML / INI / TOML / protobuf / FlatBuffers DOM 与序列化器 |

`fields_of<T>` 持有编译期字段表(count + 类型化访问器数组)。`RTTI_DEFINE_TYPE`
宏特化它;侵入式变体在类内注册嵌套的 `rtti_registration`,`fields_of` 继承之。
运行时查询 API 与递归转换都精确遍历这张表,所以注册是唯一事实来源。
`RTTI_DEFINE_CTOR` 供给 `ctor_registry`(供反射实例化与 IoC 容器使用);
`RTTI_DEFINE_TYPE_ATTRS` 与 `RTTI_DEFINE_FIELD_ATTRS` 供给特性表,供序列化器
与生成器查询。

`RTTI_IS_EMPTY` 空参检测与有界的 `RTTI_NARG` 计数采用两级间接粘合,在 MSVC
标准预处理器(`/Zc:preprocessor`)下依然正确。

## 支持的容器

| 家族 | 识别依据 | 备注 |
| --- | --- | --- |
| `std::vector<T, A>` / `std::list<T, A>` / `deque` | `is_sequence`(`push_back`、迭代器) | allocator `A` 原样透传 |
| `std::map<K,V,C,A>` / `std::unordered_map<...>` | `is_map`(`key_type` + `mapped_type`) | 键与映射值类型均可解析 |
| `std::set<T,C,A>` / `std::unordered_set<...>` | `is_set`(associative 且无 `mapped_type`) | |
| `std::queue` / `std::stack` / `std::priority_queue` | `is_queue` / `is_stack` / `is_priority_queue` | 适配器容器 |
| `std::array<T,N>` / 原生数组 | `is_fixed_sequence` / `is_c_array`/`is_md_array` | 一维与多维 `T[N]` |
| `std::vector<bool>` | `is_vector_bool` | 专用位向量通道 |
| `std::optional` / `std::variant` / `std::tuple` / `std::pair` | `is_optional` / `is_variant` / `is_tuple` / `is_pair` | C++17 新式类型 |
| 自定义 allocator / 比较器 | 形状与特征分派 | 作为模板参数直接携带,无需额外接线 |

`std::string` 被显式排除出序列处理(它同时有 `push_back` 与迭代器),改走专用
字符串转换器。自定义 allocator 与比较器作为模板参数直接携带,无需额外接线。
家族之外的类型按此顺序分派:用户 ADL 钩子 → `is_reflected`(遍历其字段)→
标量回退。

## 示例程序

`examples/` 是一套自包含的教学程序。每个文件可用 MSVC `cl` 独立编译
(`/W4` 零警告),每条 API 调用都配有详尽的英文注释(做什么 / 为什么 /
返回语义 / 错误行为)。演示类型名按命名空间隔离,所有 TU 可在一起链接而
不冲突。

| 文件 | 演示内容 |
| --- | --- |
| `reflection_demo.cpp` | 反射核心全览:注册宏、字段表、`type_of`/`type_desc`、运行时字段与容器操作、字段与元素迭代器、特性、`make_object`/`create_instance`/placement-new/`object_ref`、`to_value`/`from_value`、错误模式 |
| `core_convert_demo.cpp` | `date_time`、`base_convert`、`to_string`、枚举标签表(经 value_io 测试双) |
| `json_demo.cpp` | JSON DOM、parse / serialize、`parse_as` / `serialize<T>`、SAX 流式 |
| `xml_demo.cpp` | XML DOM、parse / serialize、SAX、命名配置 |
| `ini_demo.cpp` | INI section 与点号键、parse / serialize |
| `toml_demo.cpp` | TOML parse、DOM 构建、流式 SAX、key_case 策略、"加载 → 修改 → 回写"场景 |
| `proto_demo.cpp` | protobuf 2/3 线格式、`schema<T>` 字段注册、线格式 ↔ 树、`parse_stream` |
| `format_demo.cpp` | `date_time::format`、`format_number`、`to_string(v,spec)`、`node_to_string`、`format_reflected`、`rtti::str` 字符串操作 |
| `flatbuffers_demo.cpp` | `.fbs` 文本解析(`parse_schema`/`compile_schema`)、`flat::schema<T>` 类型桥、wire parse/serialize |

在仓库根目录的 x64 MSVC 开发者命令提示符下编译其中一个(把 `/LIBPATH`
条目改成你本机的 SDK 版本):

```bash
cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd
    /I include examples\reflection_demo.cpp /link
    /LIBPATH:"...\VC\Tools\MSVC\<ver>\lib\x64"
    /LIBPATH:"...\Windows Kits\10\Lib\10.0.<ver>\ucrt\x64"
    /LIBPATH:"...\Windows Kits\10\Lib\10.0.<ver>\um\x64" build\Debug\rtti.lib
```

`/Zc:preprocessor` 是注册宏所必需;`/MDd` 与 `build\Debug\rtti.lib` 的调试
运行时匹配。反射核心示例(`reflection_demo.cpp`、`core_convert_demo.cpp`)
在全部格式门关闭时也能编译;格式示例需要各自 `RTTI_ENABLE_*` 门开启
(默认 1)。预期结果:零 C4xxx 警告,每个 section 打印输出,以 PASS 标记收尾,
退出码 0。

## 构建配置与剥离矩阵

在 `<rtti/rtti_fwd.hpp>` 中定义六个独立特性门(默认 `1`;flatbuffers 默认经
`<rtti/flat.hpp>`):

| 门 | 启用 |
| --- | --- |
| `RTTI_ENABLE_JSON` | `<rtti/json.hpp>` |
| `RTTI_ENABLE_XML` | `<rtti/xml.hpp>` |
| `RTTI_ENABLE_INI` | `<rtti/ini.hpp>` |
| `RTTI_ENABLE_TOML` | `<rtti/toml.hpp>` |
| `RTTI_ENABLE_PROTO` | `<rtti/proto.hpp>` `<rtti/proto_schema.hpp>` |
| `RTTI_ENABLE_FLAT` | `<rtti/flat.hpp>` `<rtti/flat_schema.hpp>` ——头文件剥离宏 `RTTI_ENABLE_FLATBUFFERS`(不受 CMake 驱动,见下方注意) |

**FLAT 门注意**:`RTTI_ENABLE_FLAT` 仅控制 flat 测试注册(`flat_test.cpp` 与
`rtti_flat_schema_tests` 可执行)。`src/flat.cpp` / `src/flat_schema.cpp` 的
wire codec 恒编入 `rtti` 静态库,`<rtti/flat.hpp>` 仍可 include——头文件剥离宏
`RTTI_ENABLE_FLATBUFFERS` 默认启用,且从不被 CMake 设置;这与其余五门不同
(其余五门 `OFF` 时适配器头 `#error`)。如需完全剥离 flat,须以
`-DRTTI_ENABLE_FLAT=0` 配置 CMake,并额外传入 `-DRTTI_ENABLE_FLATBUFFERS=0`。

门是 CMake 选项(`-DRTTI_ENABLE_*=0/1`);编译期宏随 PUBLIC 定义传播到每个
目标,并在头文件里默认 `1`。反射核心零格式头依赖,因此全部六门关闭时仍可
编译,剥离构建通过完整测试套件(`strip_matrix_test` 在源文件层强制验证核心
零格式头依赖)。

六个门构成 2^6 = 64 种组合的剥离矩阵;**每一种组合均显式配置、单独构建并
通过完整测试**(各自使用独立 scratch 目录,configure 后立即读回
`CMakeCache.txt` 核验 `RTTI_ENABLE_*` 实际值)。断言数随门开单调递增——
2026-10-08 完整 64 组合扫描(全部组合 `0 failures`,零警告)从全关 `32790`
到全开 `55696`;当前代码树(含 cast/as 变体转换测试)实测全关 `32849`、全开
`55823`。扫描脚本(`.opencode/batch_strip.ps1` +
`.opencode/strip_matrix.ps1`)按开启门数分组如下:

| 开启门数 | 运行组合数 | rtti_tests 断言数范围 |
| --- | --- | --- |
| 0 | 1 | 32790 |
| 1 | 6 | 32949 – 38062 |
| 2 | 15 | 34192 – 43324 |
| 3 | 20 | 39392 – 48584 |
| 4 | 15 | 44631 – 54153 |
| 5 | 6 | 49903 – 55537 |
| 6 | 1 | 55696 |

(中间范围为 2026-10-08 扫描快照;自变体转换工作以来仅重新实测全关与全开两端点。)

(此外,独立 exe `rtti_proto_extension_tests` / `rtti_flat_schema_tests` 在
PROTO / FLAT 门开启时各增 `160` / `408` 条断言;二者也在扫描中被构建并
运行。)

## 可移植性

- **仅 C++17** —— 不需要任何 C++20 特性。
- 编译器:MSVC 2019 16.4+、GCC 11+ 与 Clang/LLVM 14+(MSVC 需
  `/Zc:preprocessor`)——最短往返浮点文本所用的 `std::to_chars` /
  `std::from_chars` 需要 libstdc++ 11 与 libc++ 14,更早的 GCC/Clang 缺失
  该实现。
- 无 `__declspec`、无 GNU 扩展、无编译器私有内置函数。
- 计时用 `std::chrono::steady_clock`,RAII 机制,仅用标准容器。
- 库为头文件驱动设计,并配套单一 `src/` 静态库构建:反射核心在六个格式门
  全部关闭时仍可编译,剥离构建通过完整测试套件。

## 文档

- [English](README.en.md) —— 英文版。
- [简体中文](README.zh-CN.md) —— 本文。

两个版本章节结构一一对应,并互相链接;上述每个章节在另一版本中都有
匹配章节。

## 许可证

基于 MIT 许可证授权 —— 完整文本见 [LICENSE](LICENSE)。

版本互引:[README.md](README.md)、[README.en.md](README.en.md)。
