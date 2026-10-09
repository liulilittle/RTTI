# RTTI — 设计与架构

> 版本:中文。English edition: [`../../en/architecture.md`](../en/architecture.md)。
> 本文反映撰写时的工作树现状。机制描述均以头文件为准;未言明之处以头文件为权威。

## 1. 总览与目标

RTTI 是一个 **C++17、头文件+静态库**的反射与(反)序列化框架,**零第三方运行时依赖**。它提供:

- 编译期**类型注册**系统(`RTTI_DEFINE_TYPE*` 宏)为 `struct`/`class` 生成运行时元数据(`type_desc`):字段、基类、方法、模板实参、属性、枚举项、构造器;
- 基于小型 `value_io` 接口的**格式无关转换**(`to_value` / `from_value`);
- **五个序列化后端** — JSON、XML、INI、TOML、protobuf,以及 FlatBuffers 布局生成器;全部可选(编译期剥离门控);
- 便捷工具:字符串构建、N 进制转换、日期时间、基础 IoC、ORM 风格绑定辅助。

非目标:元数据路径不依赖标准 `typeid`(库自建表);热转换路径不用异常(以
`error_code` 传递错误);不内置第三方解析器。

## 2. 分层架构

```
┌──────────────────────────────────────────────────────────────────────┐
│  工具层        reflect ORM · ioc · to_string · string_ops             │
│                base_convert · date_time · naming · number_format      │
├──────────────────────────────────────────────────────────────────────┤
│  协议层        proto(schema/wire/桥) · flat/flat_schema               │
├──────────────────────────────────────────────────────────────────────┤
│  格式层        json · xml · ini · toml(value_io 树)                   │
├──────────────────────────────────────────────────────────────────────┤
│  转换层        convert.hpp:converter<T>、to_value/from_value、         │
│                *_named 变体、value_io 桥                               │
├──────────────────────────────────────────────────────────────────────┤
│  核心值模型    field_value · value_io · error(code/result)            │
├──────────────────────────────────────────────────────────────────────┤
│  反射核心      reflection.hpp:type_id · type_desc · instance ·        │
│                object_ref · 字段/枚举/模板/方法/构造/属性表 ·          │
│                RTTI_*_DEFINE 宏                                        │
└──────────────────────────────────────────────────────────────────────┘
   支持:memory arena(json/xml/反射共享)· detail/scan(SIMD)
   version.hpp(生成)· rtti_fwd.hpp(门控+前置声明)
```

方向规则:**反射核心从不包含格式/协议头**。适配器实现 `rtti::value_io` 且可选项;
门控关闭时包含该头是硬 `#error`。这使核心组合干净,也让剥离构建可丢弃任意后端。

## 3. 核心机制

### 3.1 类型注册模型

`RTTI_DEFINE_TYPE_INTRUSIVE(T, ...)`(及同族宏)在 `rtti` 命名空间内展开为
`rtti::detail::fields_of<T>` 的特化,继承自 `T` 内嵌的 `T::rtti_registration`(宏
同样种入 `T`)——携带 `count` 与一个静态只读 `field_access_base*` 数组。
`fields<T>()` / `field_count<T>()` 返回表与计数;`type_of<T>()` 物化 `T` 的
`type_desc`(模板场合为 `template_info<T>`,派生静态 `template_arg_desc_of` 行)。

枚举经 `RTTI_DEFINE_ENUM(E, ...)` → `enum_table<E>`,含 `count` 与
`enum_label<E>{name, value}[]`;运行期 `enum_desc`(经 `type_desc` 可达)现在同时
发布并行的值数组与 `value_size`(B1)。模板实参经 `RTTI_TEMPLATE_INFO(...)` 注册。

跨 TU/ODR:表以 `static`/`inline` 成员写在头内,使共享该头的每个 TU 看到同一集合;
注册按类型进行,id 在二进制内确定分配。`fields_of` 主模板刻意无定义——未注册类型是
**编译期错误**(fail-fast),与未注册枚举的 `enum_table<E>` 是同一约定。

### 3.2 类型擦除与 `instance`/`as`/`cast`

`object_ref<T>` 是类型化视图;`instance` 是类型擦除持有者,数据指针与动态
`type_desc` 同存。`instance::as<T>()` 返回 `T*`(失败为 `nullptr`);
`cast<T>()` 返回 `result<T*>`,区分 `kind_mismatch` 与 `no_such_field`(空源)。
两者都经 `static_assert` 在编译期拒绝 `const`。擦除路径上,动态类型**经反射基类链
(偏移正确)解析**;若动态描述符链从未物化,则回退到存根静态类型的链——到存根静态
基类的合法上转永远不会假失败。因为擦除源不是编译期多态类,`dynamic_cast` 在
`instance` 上不适用(C2681);不可达转换干净失败。虚基上转使用 `base_adjuster`
导出的偏移。

### 3.3 格式分层与剥离门控

每个适配器(`json/xml/ini/toml/proto/flat`)都是被 `RTTI_ENABLE_<NAME>` 门控的头
(默认 1,经 `rtti_fwd.hpp`);默认构建全开,剥离矩阵以代表性子集覆盖到全关
(`build-off`)。`to_string.hpp` 序列化为紧凑单行树而**不依赖格式头**,因此字符串
输出在全剥离构建中也可用。

### 3.4 错误模型

`error_code`(error.hpp)是核心与每个适配器共享的单个 `uint8_t` 枚举:语法族
(`unexpected_token`、`unterminated_string`、`invalid_number`、`invalid_utf8`、
`unterminated_comment`、`mismatched_tag`、`duplicate_attribute`、`bad_entity`、
`unterminated_cdata`、`dtd_missing`、`too_deep`、`invalid_root`、
`ini_unterminated_section`、`ini_bad_line`、`ini_missing_value`),语义族
(`no_such_field`、`kind_mismatch`、`missing_key`、`no_such_method`、
`overflow`)与 proto 专属(`proto_truncated`、`proto_bad_field`、
`proto_type_mismatch`)。`error_info` 记录 `src`/`offset`,`line()`/`column()`
惰性计算。`result<T>` 是错误码+值的载体,带 `ok()`。§0 契约是**快速失败、绝不崩溃**;
数值转换以 `overflow` 报错而非静默回绕。

### 3.5 内存策略

JSON/XML 与反射核心共享 `rtti::memory_arena`(arena 分配;按注释 size-0 返回
null)。JSON/INI/TOML **把每个存储字符串复制进文档自有存储**,故视图在解析源
析构后依然有效;**XML 保持指向源缓冲的零拷贝 `string_view`**,并明确文档化调用方
必须让源比文档活得久(xml.hpp:110-116)。INI/TOML 节点池基于 `std::unique_ptr`
池化。程序化建节点有界且所有权安全;`destroy_nodes()` 在 arena 回收前按文档序
运行节点析构。

### 3.6 数字格式化

`number_format.hpp` 暴露三层语法与共享实数格式化器(`detail::scalar_format_real` 于
convert.hpp):`std::to_chars` 最短往返文本;token 无 `.`/`e`/`E` 时补 `.0`;
非有限值输出确定性 token。浮点渲染在 JSON/XML/INI/TOML 统一(审计 C2);`.0`
依据所写的段而非整条共享流判定(R1-8)。

## 4. 关键设计决策与理由

| # | 决策 | 理由/权衡 |
| --- | --- | --- |
| D1 | 仅已注册可反射;未注册类型/枚举是编译期错误 | 编译期快速失败胜过运行期意外(`fields_of`/`enum_table` 主模板无定义) |
| D2 | 适配器实现 `value_io`;核心不包含它们 | 组合干净;一个转换引擎多序列化器;剥离门控便宜 |
| D3 | `error_code` + 惰性 `line/column` + `result<T>`;热路径无异常 | 性能可预期;调用方只在需要时付费于诊断 |
| D4 | `as<T>`/`cast<T>` 经 `static_assert` 拒绝 `const`;`cast` 返回 `result<T*>` | const 正确性编译期强制;失败类别显式 |
| D5 | `instance` 擦除用静态链+动态恒等回退,而非 `dynamic_cast` | 擦除 `void*` 上 `dynamic_cast` 不适用(C2681);保留偏移正确的基类解析 |
| D6 | 虚基上转合法;非多态源下转干净失败 | 镜像 `static_cast` 保证;安全优先于假设 |
| D7 | XML 零拷贝;JSON/INI/TOML 全复制 | 零拷贝利于大文档/低分配;全复制利于源析构后的安全;契约逐适配器显式 |
| D8 | 数值溢出 → `error_code::overflow`,绝不静默回绕 | §0 诚实;跨格式一致 |
| D9 | `for_each_field(obj, ...)` 经访问器(虚 get)迭代 | 跨字段种类统一读取;用户代码无逐字段 switch |
| D10 | 版本单一来源:`project(RTTI VERSION 0.1.0)` → `configure_file` → `include/rtti/version.hpp` | 只 bump 一个数字;CMake、头与文档一致 |
| D11 | 命名策略(`key_case`/`rename_map`/`skip_empty`)位于 `detail::naming_policy`;适配器再导出 `key_case` | 一个策略引擎;各格式自然拼写(`rtti::json::key_case::snake`) |
| D12 | 基准输出环境头且取多轮中位数 | 数字是数据点而非结论;可复现性是产出一部分 |

## 5. 可扩展性与局限

- **新增格式**:在自托管树上实现 `value_io`,以 `RTTI_ENABLE_*` 门控,提供
  `parse`/`serialize`/`parse_as`/`serialize`,复用 `to_value`/`from_value`。
  核心改动:零。
- **新增反射种类**:扩展 `fields_of`/`type_desc`(注册)、`field_value`(值道)与
  `converter<T>`(转换)——即核心公开的三个插槽。
- 已文档化局限:`.proto` 文件解析器 out of scope(倾向 schema 注册);重复键语义跨
  格式不同(JSON/XML 首匹配,INI/TOML 后写胜);TOML 实现 v1.0 核心子集;INI 无法
  往返单元素或空序列;XML 要求解析源比文档活得久;`dynamic_cast_ref` 仅适用于
  非虚单继承(offset-0)。

## 许可证

见 [LICENSE](../../../LICENSE)(MIT)。