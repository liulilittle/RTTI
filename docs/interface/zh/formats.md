# 格式适配器 — 接口参考(JSON / XML / INI / TOML)

> 模块:`rtti::json`、`rtti::xml`、`rtti::ini`、`rtti::toml`
> 文件:`include/rtti/json.hpp`、`xml.hpp`、`ini.hpp`、`toml.hpp`
> 版本:中文。English edition: [`../../en/formats.md`](../en/formats.md)。
> 本章 API 提取自上述头文件;行号对应当前工作树。

## 0. 统一设计

四个适配器都在一棵自托管树上实现 `rtti::value_io`,因此反射核心的转换器
(`rtti::to_value` / `rtti::from_value`,来自 `<rtti/convert.hpp>`)可以透过同一
泛型接口读写文档。每种格式都是**可选**:由 `RTTI_ENABLE_JSON` /
`RTTI_ENABLE_XML` / `RTTI_ENABLE_INI` / `RTTI_ENABLE_TOML` 门控(默认 1,见
`<rtti/rtti_fwd.hpp>`);在上门关为 0 时包含该头是硬 `#error`。反射核心从不包含
格式头,因此核心保持「无适配器」的纯粹性。

各格式通用入口(JSON/XML/INI/TOML):

| 操作 | 签名(代表形式) |
| --- | --- |
| 解析到 DOM | `result<document> parse(string_view src[, const parse_config& cfg])` |
| 流式 SAX | `result<void> parse_stream(string_view src, const parse_events& events)` |
| DOM → 字符串 | `std::string serialize(const document&, const write_config& cfg = {})` |
| DOM → 缓冲 | `void serialize(const document&, std::string& out, const write_config& cfg = {})`(覆盖式,绝不追加;保留已预留容量) |
| 反射桥解析 | `template<typename T> result<T> parse_as(string_view src[, const write_config&])` |
| 反射桥序列化 | `template<typename T> std::string serialize(const T& obj, const write_config& cfg = {})` |

命名策略:当 `cfg.rename_map == nullptr && cfg.key_case ==
`rtti::detail::key_case::identity` `&& !cfg.skip_empty` 时走普通
`to_value`/`from_value` 路径;否则用 `*_named` 变体,对反射字段键应用
`key_case` / `rename_map` / `skip_empty`。

---

## 1. JSON(`rtti::json`)

`include/rtti/json.hpp`(259 行)。由 `RTTI_ENABLE_JSON` 门控。

### 1.1 `write_config`(json.hpp:64-76)

| 成员 | 类型 | 默认 | 含义 |
| --- | --- | --- | --- |
| `indent` | `int` | `2` | `0`=紧凑单行;`>0`=带空格美化 |
| `sort_keys` | `bool` | `false` | 确定性键序 |
| `indent_char` | `char` | `' '` | 缩进填充字符 |
| `key_case` | `detail::key_case` | `identity` | 反射字段键命名 |
| `rename_map` | `const std::map<std::string,std::string>*` | `nullptr` | 键改名 |
| `skip_empty` | `bool` | `false` | 丢弃空等价成员 |

### 1.2 `parse_events`(json.hpp:83-94)

SAX 回调,全部可选 `std::function`;任一返回 `false` 即提前取消解析
(`parse_stream` 此时返回 `ok`):`object_start()`、`object_end()`、
`array_start()`、`array_end()`、`key(string_view)`、
`string_value(string_view)`、`integer_value(int64_t)`、
`real_value(double)`、`bool_value(bool)`、`null_value()`。

### 1.3 `document`(json.hpp:105-160)

实现 `rtti::value_io`;持有字符串 arena、节点池与根节点。可移动、不可拷贝。
读侧代理根节点并提供
`type()/as_bool/as_integer/as_real/as_string/size()/at()/get_key()/get_key_at()`。
写侧:`make_array()/push_back/make_object()/set_key/make_scalar(field_value)/make_node()`。

文档专属成员:

- `std::string write(const write_config& cfg) const` — 公开序列化器。
- `void write(const write_config& cfg, std::string& out) const` — 直接追加到 `out`。
- `void adopt_root(detail::node* root) noexcept` — 收养刚解析的根。

### 1.4 自由函数

```cpp
result<document> parse(string_view src);
result<void> parse_stream(string_view src, const parse_events& events);
std::string serialize(const document& doc, const write_config& cfg = {});
void serialize(const document& doc, std::string& out, const write_config& cfg = {});
// 反射桥(头内模板):
template <typename T> result<T> parse_as(string_view src);
template <typename T> result<T> parse_as(string_view src, const write_config& cfg);
template <typename T> std::string serialize(const T& obj, const write_config& cfg = {});
```

### 1.5 语义(json.hpp:20-27)

- 每个存储字符串(负载与键)都复制进文档 arena;视图在源缓冲/临时对象析构后依然有效。
- 数字:不含 `.`/`e`/`E` 且能放入 `int64` 的字面量是整型节点(`int64`
  溢出降级为实型节点);含小数或指数者为实型节点。`as_real()` 也接受整型节点
  (JSON `"42"` 可填充 `double` 字段)。

---

## 2. XML(`rtti::xml`)

`include/rtti/xml.hpp`(281 行)。由 `RTTI_ENABLE_XML` 门控。

### 2.1 `write_config`(xml.hpp:58-69)

| 成员 | 类型 | 默认 | 含义 |
| --- | --- | --- | --- |
| `with_declaration` | `bool` | `true` | 输出 `<?xml version="1.0" encoding="UTF-8"?>` |
| `indent` | `int` | `4` | `0`=紧凑;`>0`=该空格数美化 |
| `indent_char` | `char` | `' '` | 缩进填充 |
| `key_case` | `detail::key_case` | `identity` | 命名策略 |
| `rename_map` | 指针 | `nullptr` | 改名表 |
| `skip_empty` | `bool` | `false` | 丢弃空等价成员 |

### 2.2 `parse_config`(xml.hpp:72-76)

- `max_depth`(`size_t`,默认 `512`)— 元素嵌套深度上限(`too_deep`)。
- `max_entity_depth`(`size_t`,默认 `32`)— DTD 实体展开深度上限。
- `max_entity_expansions`(`size_t`,默认 `10000`)— DTD 实体展开总预算。

### 2.3 `parse_events`(xml.hpp:83-91)

`element_start(string_view)`、`element_end(string_view)`、
`attribute(string_view, string_view)`、`text(string_view)`、
`cdata(string_view)`、`comment(string_view)`、`pi(string_view)`。

### 2.4 `document`(xml.hpp:117-190)

同 JSON,实现 `value_io`;仅可移动、不可拷贝。额外成员:

- `const char* intern(string_view s)` — 复制 `s` 入 arena,返回稳定 NUL 结尾指针(写侧键存活于临时对象之外)。
- `std::string ns_uri(const rtti::value_io* elem, string_view prefix) const` — `prefix` 在 `elem` 处的作用域命名空间 URI;未绑定返回 `""`;`xmlns` 属性原样保留。
- `detail::node* create_node()` / `alloc_view_node()` — 树/视图节点分配。
- 检查助手:`prolog_nodes()`、`epilog_nodes()`、`root_plus()`。

**零拷贝契约(xml.hpp:110-116,务必细读)**:文档持有自己的节点与 arena 字符串,
但元素/属性名、属性值、文本与 CDATA 都是**指向解析源缓冲的 `string_view`**。
调用方必须在文档整个生命周期内保持源缓冲存活——不同于 json/ini/toml(会把
每个存储字符串复制进文档 arena,故其视图在源析构后依然有效)。

### 2.5 映射规则(xml.hpp:13-24)

根元素 ↔ 对象;字段名 ↔ 键;属性 ↔ 标量字段(属性优先于子元素);子元素文本 ↔
标量字段;名为 `value`/`_text` 的字段绑定元素自身文本;同名重复子元素构成数组;
结构体字段映射为按对象解析的子元素。

### 2.6 自由函数

```cpp
result<document> parse(string_view src);
result<document> parse(string_view src, const parse_config& cfg);
result<void> parse_stream(string_view src, const parse_events& events);
void serialize(const document& doc, std::string& out, const write_config& cfg = {});
std::string serialize(const document& doc, const write_config& cfg = {});
template <typename T> inline result<T> parse_as(string_view src);
template <typename T> inline result<T> parse_as(string_view src, const write_config& cfg);
template <typename T> inline std::string serialize(const T& obj, const write_config& cfg = {});
```

---

## 3. INI(`rtti::ini`)

`include/rtti/ini.hpp`(367 行)。由 `RTTI_ENABLE_INI` 门控。

### 3.1 `kMaxSectionDepth`(ini.hpp:76)

`inline constexpr std::size_t kMaxSectionDepth = 512;` — 点分节头组件数上限,
与 JSON/XML/TOML 解析侧 `max_depth` 对齐;超深节头返回
`error_code::too_deep`。

### 3.2 `write_config`(ini.hpp:81-93)

`with_comments`(`bool`,默认 `false`)— 输出解析时保留的注释;另有
`key_case` / `rename_map` / `skip_empty` / `indent_char`(为对称性接受;行式
写回器忽略缩进)。

### 3.3 `detail::node`(ini.hpp:102-181)

公开读写 `value_io` 方法,加解析辅助:`set_scalar_text`、`find_child(name)`、
`set_key_impl`(重复标量将自身替换为容纳全部出现的数组槽;重复数组追加)、
`replace_child`。

### 3.4 `document`(ini.hpp:191-236)

自托管树;`move` 默认;不可拷贝。根是一个对象(全局、无节区)。全部 `value_io`
委托给根。

### 3.5 `open_section`(ini.hpp:245-247)

```cpp
detail::node* open_section(detail::node* current, rtti::string_view name,
                           detail::pool* pool, std::size_t max_depth, bool& too_deep);
```

打开(或扩展)点分路径选择的节。重复的末位分量累积进数组槽;重复节下的嵌套
路径挂在最后一个元素下。比 `max_depth` 更深时返回 `nullptr` 并置 `too_deep`
(不会构建 `serialize()` 日后可能递归越界的无界树)。

### 3.6 `parse_events`(ini.hpp:259-266)

`section_start(string_view)`(节名,全局为 `""`)、`section_end(string_view)`、
`key_value(string_view, string_view)`。注释不产生事件;`[a.b]` 这种点分头作为
单个名为 `a.b` 的节上报(流式不重建树)。回调返回 `false` 立即停止,
`parse_stream` 返回 `ok`。

### 3.7 语义与限制(ini.hpp:7-44)

- 节 `[name]`、点分嵌套名可含空白(裁剪)。
- 键:`key = value` 与 `key: value`(首个 `=` 优先);整行 `;`/`#` 注释保留
  (输出可选);**不识别行内注释**。
- 加引号值/键 `"..."`/`'...'` 支持转义 `\" ' \\`。
- 存储:解析时复制每个标量文本、键与注释——文档自足、可移动、可返回。
- 重复键:值变为容纳每次出现的数组槽;标量读取返回**最后一个**(后写胜);
  序列读取全部值。
- 重复节:槽变为节对象的数组。
- 已知限制:单元素序列无法往返(一个键/节 ⇒ 标量/对象槽,而非数组);
  空序列不输出 → 严格类型报 `missing_key`;空/全空白输入是错误
  (`ini_bad_line`);含字面换行的字符串无法往返。

### 3.8 自由函数

```cpp
result<document> parse(rtti::string_view src);
result<void> parse_stream(rtti::string_view src, const parse_events& events);
void serialize(const document& doc, std::string& out, const write_config& cfg = {});
std::string serialize(const document& doc, const write_config& cfg = {});
template <typename T> std::string serialize(const T& obj, const write_config& cfg = {});
template <typename T> result<T> parse_as(rtti::string_view src);
template <typename T> result<T> parse_as(rtti::string_view src, const write_config& cfg);
```

---

## 4. TOML(`rtti::toml`)

`include/rtti/toml.hpp`(324 行)。由 `RTTI_ENABLE_TOML` 门控。TOML v1.0 核心子集。

### 4.1 `write_config`(toml.hpp:79-84)

`key_case` / `rename_map` / `skip_empty` / `indent_char`(为对称性接受;行式写回器忽略缩进)。

### 4.2 `parse_config`(toml.hpp:86-88)

`max_depth`(`size_t`,默认 `512`)— 数组/内联表嵌套上限(`too_deep`)。

### 4.3 `parse_events`(toml.hpp:94-106)

`table_start(string_view)`、`table_end(string_view)`、
`array_table_start(string_view)`、`array_table_end(string_view)`、
`key_value(string_view, string_view)`(上报值的**原始**源切片——含引号/括号)、
`array_start()`、`array_end()`、`string_value(string_view)`、
`integer_value(int64_t)`、`real_value(double)`、`bool_value(bool)`。
类型化标量事件先于该键的 `key_value` 触发。无节键由隐式
`table_start("")/table_end("")` 括起。

### 4.4 `document`(toml.hpp:185-232)

自托管树;仅可移动。根永远是对象。`value_io` 委托给根。`detail::node` 持有
自有的标量文本,另有 `is_datetime`(日期时间输出时不加引号)。

### 4.5 语义与限制(toml.hpp:9-47)

- `key = value`;点分键 `a.b.c`;加引号键 `"k"`/`'k'`。
- 表 `[a.b]`;数组表 `[[a.b]]` 映射为对象数组。
- 标量:字符串(基本/多行/字面量)、整数(十/十六/八/二进位,可带下划线)、
  浮点(小数、指数、`inf`/`nan`/`±inf`)、布尔、映射为规范字符串的日期时间
  (保留精确源文本、不加引号往返、无损)。整数规范化为规范十进制。
- 数组允许尾逗号、混合类型、嵌套数组、内联表 `{k = v, ...}`。
- 注释(整行与行尾 `#`)跳过、不保留。
- 重复键:**后写胜**。重开既有 `[table]` 继续之;`[[t]]` 在既有表上把它变成
  先含旧表的数组。
- 畸形输入返回 `error_code`(分配可能抛 `std::bad_alloc`;绝不崩溃)。深嵌套
  得 `too_deep`。
- 序列化先写表的标量再写子表;parse→serialize→parse 可往返。
- 已知限制:严格 TOML v1.0 边角简化(把表重定义为数组表按后写胜/复用最后
  约定而非报错);边角引号按简化规则;注释不重发。

### 4.6 自由函数

```cpp
result<document> parse(string_view src, const parse_config& cfg);
result<document> parse(string_view src);
result<void> parse_stream(string_view src, const parse_events& events);
void serialize(const document& doc, std::string& out, const write_config& cfg = {});
std::string serialize(const document& doc, const write_config& cfg = {});
template <typename T> std::string serialize(const T& obj, const write_config& cfg = {});
template <typename T> result<T> parse_as(string_view src);
template <typename T> result<T> parse_as(string_view src, const write_config& cfg);
```

---

## 5. 跨格式注意

- **缓冲序列化语义**:XML/INI/TOML 的 `serialize(doc, std::string&)` 是*替换*
  `out`(绝不追加;保留已预留容量)。JSON 的 `write(cfg, out)` 是追加风格,但其
  `serialize(doc, out, cfg)` 重载先清 `out`,以维持同样的覆盖契约。
- **源生命周期**:json/ini/toml 复制全部字符串;**XML 是零拷贝**(源缓冲必须
  比文档活得久)。
- **注释/实体处理**:各格式如上文所述不同。
- **深度守卫**:JSON/XML/TOML 解析侧 `max_depth`(512)与 INI `kMaxSectionDepth`
  (512)把恶性嵌套限制到 `too_deep`;XML 另加 DTD 实体深度/展开预算。

## 版权说明

本参考的两版仅描述公开接口。许可证见 [LICENSE](../../../LICENSE)。