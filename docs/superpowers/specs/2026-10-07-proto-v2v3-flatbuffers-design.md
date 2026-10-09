# proto V2-V3 补全 + FlatBuffers 格式适配器设计

日期:2026-10-07
状态:草案(待用户评审)
项目:RTTI 反射库(命名空间 `rtti`,C++17)

## 1. 背景与目标

深度验证已收尾(主 54738/0、剥离 32462/0、5 修复组全绿)。用户追加需求:

1. **proto/protobuf V2-V3 必须全版本支持,补全语法项并绝对正确**(官方工具对拍验证)。
2. **新增 FlatBuffers 格式适配器**,含 .fbs 文本解析与官方 flatc 互操作验证。
3. **库形态**:静态库(.lib)+ 头文件接口;非模板实现尽量编入 src/*.cpp 编译;**新增实现必须非模板 cpp;存量可改则改**。官方工具生成的代码仅作测试对拍夹具,不进核心库。
4. **文档对称**:README.en.md / README.zh-CN.md 同步新增格式章节;examples 补齐。
5. **剥离矩阵**:现有 5 门(JSON/XML/INI/TOML/PROTO)32 组合;加 FlatBuffers 为 6 门 **64 组合全验证**。
6. 可利用团队并行推进(经 task 派发 + W11 双审)。

## 2. 形态与编译架构

### 2.1 静态库 + 头文件
- 库保持 `rtti::` namespace;C++17;header-only 既有头不强制迁移。
- **新增**解析器/编解码器优先做非模板,编入 `src/*.cpp`,由现有 CMake 静态库 `rtti` 目标吸收;头文件保留模板壳(converter 特化、schema 注册宏、typed 桥)。
- **存量非模板化迁移纳入本轮**:plan 阶段对既有头(proto.hpp/proto_schema.hpp 解析器、to_string 文本树、scan 等)做影响评估,凡可非模板化的解析/序列化主体渐进迁入 src/*.cpp(经评估确定优先级,不引入行为回归)。
- 官方 platforms:Windows/MSVC 2022(当前验证),保持 GCC/Clang 兼容(无 MSVC 特有 UB)。

### 2.2 官方工具(fixtures 生成 + 对拍)
已部署:
- `C:\Users\liulilte\.rtti-tools\protoc\bin\protoc.exe`(36.2)
- `C:\Users\liulilte\.rtti-tools\flatc\flatc.exe`(25.12.19)

用途:
- **fixtures 生成**:用标准 `.proto` / `.fbs` 描述文件 + 官方工具编译,输出二进制 wire 字节与生成代码产物,作为测试夹具**提交仓库**(`tests/fixtures/proto/`、`tests/fixtures/flat/`)。
- **对拍验证**:测试读取官方产出的 wire 字节,以 rtti 解析/序列化后逐字节/逐字段比对,保证互操作绝对正确。
- 工具本体不进仓库;仅 fixtures 与描述文件入仓。测试运行不依赖工具存在(fixtures 内嵌)。

## 3. proto V2-V3 补全

### 3.1 现状
- `proto.hpp`:wire 0/1/2/5 编解码、schema<T> 注册、proto2 required/默认值、proto3 packed/默认省略、map 条目、typed/树桥、SAX。已验收。
- `proto_schema.hpp`(近期新增):.proto 文本解析(proto2/proto3 子集) + compile_schema 桥接。**缺少**:sint zigzag、map 声明、oneof、group(拒绝)、reserved、extensions、enum 默认值名、\xNN 转义补全、union/import 语义。

### 3.2 补全范围(全部支持)
1. `syntax`(proto2/proto3)、message/嵌套/前向引用/绝对名(已支持)。
2. 全部标量,含 **sint32/sint64 zigzag**(补全)。
3. `map<K,V>` 声明(条目语义 key=1/value=2)。
4. `oneof name { ... }`(单一活跃字段;wire 与普通字段同)。
5. `optional`(proto3 显式 presence)、`required`(proto2)、`repeated`、[packed=]、[default=](已支持)。
6. `enum`(含默认值名解析、allow_alias 句法接受)。
7. **import**(语义=声明引入,解析后续引用)。
8. `reserved 1,2;` / `reserved "x";` 句法接受,并拒绝字段号/名冲突。
9. `extensions 100 to 199;`(proto2)**运行时扩展读写,按官方扩展语义**(protoc ExtensionRegistry):扩展是消息可声明 reserve 区间,扩展字段在 wire 上以(全局字段号 + 扩展类型)编码;解析时遇未知字段号 → 查"扩展注册表"(由扩展的 `.proto` 声明编译所得 schema 片段构成),命中则按该扩展的类型解析为具名字段,未命中则保留为未知字段(可原样跳过/回写)。提供 `register_extension(...)` 或等价 API,与 protoc 官方 ExtensionRegistry 对齐。
10. `\xNN` 转义(proto2 字节字面量)、`\uXXXX`(字符串)。
11. 注释(已支持)、命令行 option 跳过。
12. group(proto2 已弃用):明确拒绝并给明确错误码。

### 3.3 运行正确性契约(官方对拍)
- fixtures 覆盖:null/0、varint 最短编码、zigzag、嵌套、packed repeated、map 条目、proto2 默认值/required 缺失、proto3 默认省略、oneof 切换、extensions 未知字段、非法 wire。
- 每个 fixture 断言:parse(wire)→ 值与官方 read 一致;serialize(value)→ 与官方字节一致(或规范允许等价字节)。
- 错误路径:残缺/越界/未知字段 → error_code,不崩溃(§0 安全)。

## 4. FlatBuffers 适配器(新增)

### 4.1 组件
- **`include/rtti/flat.hpp`**:wire 编解码(小端标量、对齐 4/8、table vtable/offset、vector、string、struct、union、root offset、file_identifier)。schema 注册:`schema<F>` 形态与 proto 对齐(字段 id→offset/vtable slot)。
- **`include/rtti/flat_schema.hpp`**:`.fbs` 文本解析器——`namespace`、`table`、`struct`、`enum`、`union`、`root_type`、`file_identifier`、标量类型、vector `[T]`、string、嵌套 table 引用、`deprecated`/`required` 属性。
- **`include/rtti/flat_impl.cpp`**(或并入 src):非模板解析器与编解码器主体,编入静态库。
- typed/树桥:复用 `value_io` 与既有 convert 反射链路(schema<F> → wire → tree → value_io)。

### 4.2 官方互操作验证
- 编写标准 `.fbs`(含 table/struct/enum/union/vector/string/nested)，用 flatc --cpp 生成官方头 + --binary 生成 wire。
- 对拍:rtti 解析官方 wire → 值与官方生成代码读取一致;rtti 序列化 → 官方 flatc 可读回一致。
- fixtures 提交入 `tests/fixtures/flat/`。

### 4.3 节点级转换与序列化(所有格式统一能力)

用户追加:JSON/XML/INI/TOML/proto(及新增 flatbuffers)都应支持"**获取具体节点→转换对应 modern C++ 类型**"与"**C++ 值→序列化/写入到指定节点**"。设计:

- 既有基础已具备:各格式 DOM `node` 派生自 `value_io`(get_key/at/make_node/set_key),`from_value/to_value` 全类型转换,`parse_as<T>`(整文档)。缺**统一节点定位 + 节点级双向转换** API。
- 新增(格式无关,入 reflect 核心或 convert 层):
  1. `result<const value_io*> locate(const document&, string_view path)` / `result<value_io*> locate(document&, string_view path)` — 路径定位(点分键/索引,如 `"a.b[2]"`),覆盖所有 DOM 格式;
  2. 节点 → C++:`to_value(located, out)` 既有,补便捷 `node_as<T>(node)` → `result<T>`;
  3. C++ → 指定节点:`to_node(value_io& target, const T& v)`(写/替换 node 子树,经 from_value 反向),供"更新某点后整文档序列化"。
- 各格式适配器暴露 `locate`;路径语法统一(点分+`[n]`),失败返回 error_code(§0 不崩溃)。
- 测试:`tests/node_locate_test.cpp`(五格式 + flatbuffers 统一探测),官方 fixtures 亦覆盖。

## 5. 门控与剥离矩阵

- 新增门 `RTTI_ENABLE_FLATBUFFERS`(默认 ON;与既有门模式一致,`rtti_fwd.hpp` 声明 + CMake option)。
- 剥离矩阵:6 门 2^6=**64 组合**逐一 configure+build+test(沿用 `.opencode/batch_strip.ps1`,扩展 6 门)。
- 断言基线:主 54738/0 → 新增格式断言;剥离 32462/0 → 随门扩展。

## 6. 文档与示例(对称)

- README.en.md / README.zh-CN.md:新增 `## protobuf(v2/v3)` 补全说明、`## flatbuffers` 章节、**节点级转换/locate**、**字符串格式化(日期/数字/反射驱动)** 章节;既有的"五个门"表述改为"六个门"。
- examples:新增 `examples/flatbuffers_demo.cpp` 与 `examples/format_demo.cpp`;proto_demo 补 zigzag/map/oneof/extensions 用例;格式 demo 补节点 locate/格式化用例。
- 头文件注释英文;README 双语互索引。

## 7. 字符串格式化能力(补:日期/数字/节点级/反射驱动)

用户追加:反射/节点转换须具备**字符串格式化**能力,且**演示中可见**(日期格式化、数字格式化)。设计:

### 7.1 日期格式化
- 现状:date_time 仅 `format()`/`to_string()` 输出 ISO 8601 UTC 规范形式。
- 新增:`date_time::format(string_view pattern)` 支持 strftime 风格子集:`%Y %y %m %d %H %M %S %f`(纳秒 9 位)`%F %T %R %z %Z` 等;非法模式返回空串或 error_code(保持 §0 fail-safe)。
- 对应解析:不变(parse 仍接受 ISO 子集与偏移)。

### 7.2 数字格式化
- 现状:整型 `std::to_string`、float/double `format_real`(最短往返),无宽度/精度/千分位/进制选项。
- 新增:`rtti::format_number`(或 `to_string<T>(v, format_spec)`):format_spec 支持
  - 整型:`{base}{,min_width}{fill}{align}{+}{#}{,}`(千分位分组用可选 `grouping` 参数);
  - 浮点:`{precision}{fixed}{sci}{,}{width}{fill}{+}`;
  - 例:`format_number(1234567.0, ".2f")`→"1234567.00"、`format_number(1234567, "#,d")`→"1,234,567"。
- 薄封装于 to_string<T> 重载,不破坏既有 to_string(T) 行为。

### 7.3 节点级字符串化(与 4.3 打通)
- `rtti::node_to_string(const value_io* node, format_spec=默认)` → 把定位得到的节点(或其 C++ 值)格式化为字符串;
- 反方向:字符串 → 节点已有(parse_as 族)。

### 7.4 反射驱动格式化
- 基于 type_desc/reflect 字段表,`rtti::format_reflected<T>(obj, spec)` 输出反射对象为字符串(类 std::format 但反射驱动);作为既有 to_string<T> 的互补。

### 7.5 演示(可见性)
- 新增 `examples/format_demo.cpp`:日期格式化(各模式)、数字格式化(整数/浮点/千分位/宽度/精度/进制)、节点级字符串化、反射驱动格式化,注释细致;并入 examples 编译与运行验证。

### 7.6 字符串转换全家福 + 字符串操作封装(用户追加)
- **转换全覆盖**:`rtti::to_string` / `from_string` 覆盖全部类型——标量(整型/浮点/布尔/字符)、string/wstring、枚举(标签)、date_time、进制(2..64)、容器(vector/map/set/queue/stack/pq)、optional/variant/tuple/pair/array/vector<bool/原生数组、反射对象(嵌套)、方法反射返回等;与既有 to_string<T> 打通(薄重载,不破坏既有行为)。
- **格式重载**:`to_string<T>(v, format_spec)` 与 `from_string<T>(str, format_spec)`(数字 spec、日期模式、进制)彼此对称。
- **全套字符串操作封装**(`include/rtti/string_ops.hpp` + `src/string_ops.cpp` 非模板):split/join(自定义分隔)、trim/lstrip/rstrip、to_lower/to_upper、starts_with/ends_with、replace/replace_all、substr、repeat、pad_left/pad_right、反转、取长整数安全 parse(溢出→error_code)、URL/JSON/XML 转义与反转义;操作全部 noexcept 或 error_code,§0 fail-safe。
- **简单**:一个头引入 `rtti/string_ops.hpp`;命名统一;demo/README 演示。
- 测试 `tests/string_ops_test.cpp` 全覆盖 + `examples/format_demo.cpp` 补字符串节。

### 7.6 示例程序接口全覆盖(硬性原则,不可辩驳)
- **凡是库公开的接口(函数/类型/宏/配置选项/重载/桥),examples 必须至少演示一次**;每新格式/新能力交付即补配套 demo。
- 机制:每个示例程序内置"接口覆盖清单"(声明清单 + 运行断言),验收时以清单为准核对"零遗漏"。
- 本原则适用于既有全部 demo(reflection/core_convert/json/xml/ini/toml/proto)与新增(flatbuffers_demo/format_demo):新接口并入既有 demo 对应节,不另起重复 demo。

## 8. 明确排除(YAGNI)

- 不实现 FlatBuffers 代码生成(无 flatc 等价 API);新增格式的"无 schema 需自描述"不在本轮。
- proto `group` wire 编码不实现(显式拒绝);message_set、proto1 不支持。
- 官方工具的在线对拍仅作开发期验证;提交的 fixtures 保证测试无外部依赖、可复现。
- 库核心仍为可剥离门控;静态库形态不改变既有 header-only 接口兼容性。

## 9. 工作拆分(团队并行候选)

- T-P1:proto_schema 语法补全(sint zigzag/map/oneof/reserved/import/enum 默认名/\xNN)+ fixtures 对拍。
- T-P2:protoc 扩展运行时 + extensions 注册表 + 错误路径。
- T-F1:flat.hpp wire 编解码 + schema<F> 注册 + 官方 flatc fixtures 对拍。
- T-F2:flat_schema.hpp .fbs 解析器(非模板 cpp)+ flat 节点/桥。
- T-L:locate/node_as/to_node 统一节点级转换(五格式 + flat)+ tests 探测。
- T-S:数字/日期格式化(format_number/date_time::format)+ 反射驱动格式化 + format_demo。
- T-A:src/*.cpp 非模板化迁移影响评估 + 渐进迁移(经评估不回归)。
- T-D:README 双语对称 + examples 补齐(含接口覆盖清单核对)+ 剥离矩阵 6 门 64 组合脚本扩展 + 双构建收尾。
- 每任务 TDD 红→绿;完成后 W11 双审(审计与执行不同 AI、双向磋商、独立盖章);全量 64 组合 + 双构建确认。

## 10. 验收标准

1. proto V2-V3 fixtures 全过(官方 protoc 产出 wire ↔ rtti 双向一致)。
2. flatbuffers fixtures 全过(官方 flatc 产出 wire ↔ rtti 双向一致)。
3. 主构建 0 error 0 warning;rtti_tests.exe 0 failures(54738→新基线)。
4. 剥离 6 门 64 组合全绿零警告零失败。
5. README 双语对称;examples 编译零警告运行通过。
6. 新实现非模板部分确在 src/*.cpp 编译(静态库吸收),头文件仅模板壳。
7. **示例程序接口全覆盖**:每个 example 覆盖清单与库公开接口对照零遗漏(7.6 硬性核对)。