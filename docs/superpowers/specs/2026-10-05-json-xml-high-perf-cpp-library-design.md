# jsonxx — Modern C++ 运行时反射 + JSON/XML/INI 适配器 高性能无依赖库 — 设计文档

日期:2026-10-05
路径:architectural(新项目)

## 1. 目标与成功标准

构建一个 C++17 无第三方依赖的**现代 C++ 运行时反射库**:弥补 C++ 缺乏高阶反射的短板。核心是反射(字段表 + 通用遍历,编译期生成、运行时可用),JSON、XML、INI 是三种**配套格式适配器**,挂接在同一反射核心上。本库毫不避讳地定位为 C++ Modern(Model) 运行时反射高性能库。

- **核心定位**:用户声明结构体 + 一个宏,即自动获得编译期字段表(字段名↔成员指针)。字段表既驱动与全部三种格式的双向转换(序列化 + 反序列化),也提供**运行时反射能力**:按名访问字段、遍历字段、类型查询。零手写转换代码。
- **格式定位**:JSON / XML / INI 均为反射核心的**可选适配器**,各自独立 DOM + 流式解析,共享基础设施。
- **可剥离性**:反射核心不依赖任何格式;JSON / XML / INI 三适配器各自独立编译单元,可通过编译开关/CMake 选项单独启用或剥离,默认全开。剥离后反射核心 + 其余格式照常工作。
- **交付形态**:编译为静态库(头文件 + lib),头文件不依赖任何第三方头。
- **XML 范围**:全特性 DOM —— 元素/属性/文本(混合内容)/CDATA/注释/处理指令/命名空间/DTD 实体展开(安全)。
- **JSON 范围**:标准 JSON(DOM + 流式),区分 integer/real,strict 输出。
- **INI 范围**:节(section)/键值对/注释(; #)/引号值/转义;DOM + 流式。
- **性能目标**:主流 x86-64 单线程,三种格式百字节级小文档解析均达 3-8M 次/秒;大文档按吞吐 MB/s 计;序列化吞吐同量级;反射转换与手写代码同量级。
- **约束**:
  - 无运行时依赖,支持 MSVC/GCC/Clang/CUDA NVCC 等主流编译器,覆盖 x86-64/ARM64/AArch64 平台。
  - **全平台可移植**:禁止非通用 API(无 `__declspec`/GNU 扩展/编译器私有内置函数/平台私有头);SIMD 仅经标准 intrinsics 头(SSE/AVX2/NEON 条件编译),不可用时自动回落标量;所有标准库调用均为 C++17 标准可移植子集。
  - **代码与注释一律英文**;公开 API 命名规范统一、无大小写歧义。
  - **文档中英双语**:每份文档含英文主版与中文版,内容一一对应,中英章节标题可互相索引(锚点互链)。

## 2. 整体架构

反射核心为顶层,格式适配器与共享基础设施为下层。

```
┌─────────────────────────────────────────┐
│  反射核心 reflect<T>(编译期字段表 + 通用遍历) │
│  JSONXX_DEFINE_TYPE / _INTRUSIVE /       │
│  _WITH_DEFAULT / _INTRUSIVE_WITH_DEFAULT │
└──────────────┬──────────────────────────┘
               │ 统一遍历驱动双向转换 + 运行时反射查询
   ┌───────────┼───────────┐
   ▼           ▼           ▼
 JSON 适配器  XML 适配器   INI 适配器
 (DOM+流式)  (DOM+流式)   (DOM+流式)
   └───────────┼───────────┘
               ▼
      共享基础设施 common/
   MemoryArena · string_view · Result<T>
   SIMD 扫描器 · UTF-8 校验 · 错误码
```

```
include/jsonxx/
  reflect.hpp    // 反射核心:字段表、reflect<T>、convert<T>、ADL 分派(不依赖任何格式)
  macros.hpp     // 四个宏(JSONXX_DEFINE_TYPE 族)与 JSONXX_DEFINE_ENUM
  common.hpp     // string_view、Result<T>、错误码、MemoryArena、SIMD 扫描器
  json.hpp       // [可选] JSON 适配器:DOM 节点、parse/流式、修改 API、从/到 reflect
  xml.hpp        // [可选] XML 适配器:DOM 节点、parse、命名空间、实体、从/到 reflect
  ini.hpp        // [可选] INI 适配器:DOM 节点、parse/流式、从/到 reflect
  serializer.hpp // [可选] 三种格式序列化(strict/compact 配置)
src/
  common.cpp
  reflect.cpp
  json.cpp       // [可选编译单元]
  xml.cpp        // [可选编译单元]
  ini.cpp        // [可选编译单元]
  serializer.cpp // [可选编译单元]
bench/           // 基准(小文档 ops/s、大文档 MB/s)
tests/           // 单元/集成/边界测试
docs/            // 中英双语文档(章节互相索引)
```

- **剥离规则**:`common` 与 `reflect` 是强制编译的底座;`json`/`xml`/`ini` 三个适配器为独立可选编译单元,各由独立编译开关控制(如 `JSONXX_ENABLE_JSON`/`JSONXX_ENABLE_XML`/`JSONXX_ENABLE_INI` 或等效 CMake 选项)。
- **反射核心零格式依赖**:`reflect.hpp`/`macros.hpp` 不含任何格式头;格式适配器单向依赖反射核心,反向禁止(反射不得 `#include` 任何格式)。
- **全平台**:所有头不依赖编译器私有特性;SIMD 代码经 `#if defined(JSONXX_SSE2)/JSONXX_AVX2/JSONXX_NEON` 条件编译,其余走可移植标量路径。
- 剥离后的头部:`#include <jsonxx/json.hpp>` 等仍可用,未启用的格式编译报错提示「该格式未启用」。

### 2.1 共享基础设施(common)

- `string_view`:零拷贝传递,所有字符串数据归 parse 输入缓冲或 arena 所有。
- `MemoryArena`:一次分配大块,节点/字符串按池内偏移分配,整文档一次释放;按需增长,`new` 仅在扩容时发生。
- `Result<T>`:携带值或错误码 + 字节偏移 + 惰性行列换算。
- `ErrorCode` 枚举(见 §6.2)。
- `utf8_validate_simd / utf8_validate_scalar`:strict 模式校验。
- SIMD 结构字符扫描:JSON `{}[],:`;XML `<>"'` 与实体起始;INI 行首定位与分隔符 `=` `:`。

## 3. 反射核心(库的本体)

C++17 无原生反射,本库用宏 + 模板建立**编译期反射抽象**:任何用户类型只要声明一个宏,即自动获得与全部格式的双向转换,无需手写任何转换代码。

**易用性目标(.NET 反射级)**:反射 API 的设计以"简单好用、与 .NET 反射同等直觉"为第一原则:
- 一个类型对象(`type_info<T>` / 运行时按类型名查)即可获取字段元数据。
- 字段可遍历:`for (auto& f : fields_of<T>)` 得到每个字段的名字/类型/读写器。
- 运行时按名字符串访问:`obj[name]` 取值、`set(name, value)` 写值,与 .NET `PropertyInfo.GetValue/SetValue` 同直觉。
- 类型安全由模板承担,常见类型零样板;用户只见"声明一个宏 + 用类型对象",不见任何模板元编程细节。

### 3.1 编译期字段表

- 宏展开为静态字段表 `{ {"name", &T::name}, {"address", &T::address}, {"age", &T::age} }`:字段名↔成员指针,编译期确定、零运行时查找。
- 通用模板 `reflect<T>` 按字段表做**自动遍历**:同一次遍历同时驱动反序列化(格式树 → 字段)与序列化(字段 → 格式树),对三种格式各挂一套适配器回调。
- 用户只写类型声明 + 一个宏;反射、遍历、格式适配全部由库完成。

### 3.2 宏族用法

```cpp
struct person {
    std::string name;
    std::string address;
    int age;
};

// 在结构体外部、同一命名空间内定义(全局命名空间也可)
JSONXX_DEFINE_TYPE(person, name, address, age);
```

- `JSONXX_DEFINE_TYPE(T, m1, m2, ...)`:结构体外部,只能访问 `public` 成员(对应 NON_INTRUSIVE)。
- `JSONXX_DEFINE_TYPE_INTRUSIVE(T, m1, m2, ...)`:写在结构体/类**内部**,宏在类内生成 `to_json`/`from_json`(及 `to_xml`/`from_xml`/`to_ini`/`from_ini`)的 **friend 函数**,因此可访问 `private/protected` 成员;放在结构体内部、后面通常不加分号。
- `JSONXX_DEFINE_TYPE_WITH_DEFAULT(T, m1, m2, ...)`:外部版,缺失键用成员默认值,不报错。
- `JSONXX_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(T, m1, m2, ...)`:内部版 + 缺失键用默认值。
- 四个宏语义与 nlohmann 一一对应,均同时生成**双向**转换(见 §3.6);同命名空间内 ADL 查找,任意类型嵌套自动递归。
- 枚举:`JSONXX_DEFINE_ENUM(MyEnum, A, B, C)`,字符串↔枚举值双向。

### 3.3 转换模板(convert<T> 分派)

- 内建 `convert<T>` 覆盖:基本类型(int/double/bool/enum/整数/实数)、`std::string`、四类容器 —— **map 类**(键值对语义,↔ JSON 对象 / XML 子元素组 / INI 节键)、**set 类**(元素语义,↔ JSON 数组)、**list 类**(顺序语义,↔ JSON 数组)、**vector 类**(顺序语义,↔ JSON 数组 / XML 同名子元素集合 / INI 重复节)、嵌套结构体(检测到字段表或 ADL 即递归)。
- **容器以通用模板类形态识别**:转换代码按容器**特征**匹配而非具体类型别名 —— 检测 `push_back`/`insert`/`size`/`begin`/`end`/键值对 `value_type` 等接口。因此用户重载 `Allocator` 的 `std::vector<T, MyAlloc>`、`std::list<T, MyAlloc>`、`std::map<K,V,Compare,MyAlloc>`、`std::set<T,Compare,MyAlloc>` 及 unordered 变体均自动支持,分配器参数天然透传。
- 分派顺序:先查用户 ADL 手写函数,再查字段表自动;任意未覆盖类型可由用户手写 `from_json/to_json/from_xml/to_xml/from_ini/to_ini` 覆盖。
- 容器填充:顺序容器先取长度再 `reserve` 预分配(list 不 reserve 直接 push_back);关联容器直接 insert;丢失键保留成员默认值(可配置严格模式报错),多余键默认忽略。

### 3.4 XML 绑定映射规则

- 字段 `id` ↔ 同名**属性**优先(`<x id="1">`)。
- 字段 `name` ↔ 同名**子元素**文本(`<name>xxx</name>`);无对应子元素/属性时映射元素整体文本内容。
- 专用字段名 `value` 或 `_text` ↔ 元素整体文本内容(含 CDATA 合并,同 DOM 序列化规则)。
- `std::vector<T>` 字段 ↔ 同名子元素集合(`<item>…</item><item>…</item>`)。
- 结构体字段 ↔ 同名子元素解析。

### 3.5 INI 绑定映射规则

- 标量/字符串字段 ↔ 当前节的键(`[global]` 节或命名节);成员名即键名。
- 结构体字段 ↔ 同名子节(`[name]`),该节内键映射其成员;嵌套结构体递归为嵌套节。
- `std::vector<T>` 字段 ↔ 重复节名集合,每节填一项(T 必须为结构体类型)。
- 缺失键:标准宏报错,`_WITH_DEFAULT` 变体保留成员默认值。

### 3.6 双向序列化与反序列化

- 四个宏均同时生成**双向**转换函数:反序列化 `from_json/from_xml/from_ini`(格式树→结构体)与序列化 `to_json/to_xml/to_ini`(结构体→格式树)。
- 反向路径与正向完全对称:JSON 键/XML 属性·元素/INI 键名由同一成员名推导,容器/嵌套结构体/可选字段/枚举按同一 `convert` 模板递归写出。
- 提供便利入口:`parse<T>(str)`(反序列化)、`serialize(obj)`(序列化),以及 `obj.to_json()`/`json.to(obj)` 风格 API。
- 往返一致:structure → serialize → parse → structure 深度相等(测试覆盖,见 §8)。

### 3.7 反射性能

- 宏生成函数体无查找表、无虚函数,单成员直接内联赋值。
- 数组预分配避免重分配放大;整数字段走快路径。
- 反射转换与手写转换同量级,是序列化管线的顶层;底层解析速度由各格式适配器保证。

## 4. 格式适配器数据模型

### 4.1 JSON 节点(扁平数组 + arena)

```
enum class jtype : uint8_t { null, boolean, integer, real, string, array, object };
struct jvalue {
  jtype type;
  union {
    bool b;
    int64_t i;
    double d;
    struct { const char* s; size_t len; } str;
    struct { uint32_t child; uint32_t count; } arr;   // 子索引连续区间
    struct { uint32_t child; uint32_t count; } obj;   // 键值对连续存储
  };
};
```

- 数字区分 integer/real,整数字面快路径不经过 `strtod`。
- 对象子区间为连续 {key_view, value_index} 对;键默认零拷贝视图。
- 树根返回 `const jvalue&`;修改通过 DOM 修改 API,修改后偏移结构由 API 维护。

### 4.2 XML 节点(pugixml 式链式 + arena)

```
enum class xtype : uint8_t { document, element, pcdata, cdata, comment, pi };
struct xnode {
  xtype type;
  const char* name; const char* value; size_t len;
  xnode* first_child; xnode* last_child;
  xnode* next_sibling; xnode* prev_sibling;
  xattr* first_attr;
};
struct xattr { const char* name, * value; size_t value_len; xattr* next; };
```

- 命名空间:解析时建立 `xmlns` 前缀→URI 绑定(URI 由 arena 持有),作用域栈下推/弹出;解析完成后通过 `xattr` 链旁的 ns 链可查询元素的 in-scope 命名空间。
- 混合内容:文本与子元素同链,序列化时合并相邻文本输出。
- 实体:预定义 5 个(`&amp; &lt; &gt; &apos; &quot;`)+ 数字实体 + 自定义(来自内部 DTD 子集)。

### 4.3 INI 节点(扁平对象 + arena)

```
struct isection {
  const char* name; size_t name_len;   // 节名(无节时 name 为空)
  uint32_t first_key; uint32_t key_count;  // 连续键区间
};
struct ikey {
  const char* name; size_t name_len;
  const char* value; const size_t value_len;
};
```

- 文档 = 节数组(含一个全局无节区 `[global]`,置于节列表尾部);每节内键值对连续存储,查键用线性扫描或哈希(小键表线性,大键表建索引)。
- 键名/值零拷贝视图(去解析期可见的前后空白);引号/转义值则进 arena 解码。
- 重复键:默认后者覆盖;重复节:键合并(可配置严格模式报错)。

## 5. 性能设计

1. **SIMD 结构字符扫描**:JSON/XML/INI 均用 AVX2/SSE 一次比较 32/16 字节定位结构字符,减少分支与逐字符跳转。
2. **整数组装路径**:手工解析整数返回 int64,不落浮点。
3. **字符串零拷贝**:无转义字符串直接指向输入缓冲视图;转义字符串一次性进 arena。
4. **arena 一次分配**:整文档节点/字符串单次 malloc(按需增长),释放整文档单次 free。
5. **栈机单遍**:JSON 用显式栈跟踪深度与节点索引,规避递归与二次遍历。
6. **惰性 UTF-8**:默认宽松模式零校验开销;strict 用 SIMD。
7. **预分配序列化缓冲**:序列化先估长再填充,避免多次 realloc。
8. **INI 单遍行扫描**:按行 SIMD 定位行首与注释起点,键/值区间零拷贝,节切换仅移动指针。
9. **反射零开销**:字段表编译期索引,转换内联,无虚函数无查找。

## 6. 错误处理与安全

### 6.1 错误策略

- 解析失败立即停止,返回 `Result<T>` 的 `ErrorCode` + 字节偏移;行/列惰性换算。
- 提供错误上下文片段(错误位置前后 ≤32 字节)。
- 流式模式下回调返回 `false` 即取消解析,不建树。

### 6.2 错误码

```
ok, unexpected_token, unterminated_string, invalid_number, invalid_utf8,
unterminated_comment, mismatched_tag, duplicate_attribute, bad_entity,
unterminated_cdata, dtd_missing, too_deep, invalid_root,
ini_unterminated_section, ini_bad_line, ini_missing_value
```

### 6.3 安全防护

- 实体展开:深度上限(默认 8)+ 展开次数上限,防 Billion Laughs(实体扩展炸弹)。
- 最大嵌套深度:默认 512,可配置。
- DTD:默认只处理内部子集;`dtd_missing` 错误覆盖缺失声明;不拉外部实体。
- XML 常规宽松模式不做逐字节 UTF-8 校验;strict 模式 SIMD 校验(可配置)。

## 7. 序列化与流式 API

### 7.1 序列化

- JSON strict 输出:UTF-8 转义(`\" \\ \b \f \n \r \t` + 控制字符 `\uXXXX`),对象键排序可选(默认不排序)。
- XML 输出:声明、命名空间(必要时)、属性转义、文本转义(`& < >`)、CDATA/注释/PI 原样;相邻文本合并输出。
- INI 输出:节头 `[name]`、`key = value`,值/键按需引号与转义,注释配置可选输出。
- 三者都提供 `serialize(root, buffer)` 与返回 `std::string` 两个入口;配置项:缩进(indent)/紧凑(strict JSON)/XML 声明/INI 注释保留。

### 7.2 流式 API(SAX)

- 统一事件回调接口:`object_start/object_end/array_start/array_end/key/string_value/integer_value/real_value/bool_value/null_value/text/cdata/comment/pi/element_start/element_end/attribute/section_start/section_end`。
- 三种格式的 DOM 解析器内部均构建于流式之上,保证行为一致;流式 API 独立暴露供零树高吞吐路径使用。

## 8. 测试与验证

- 单元测试:反射字段表、节点模型、arena、SIMD 扫描(含无 SIMD 回退)、UTF-8。
- 解析正确性:标准 JSON 用例、XML 全特性(命名空间/DTD 实体/CDTA/PI/混合内容)、INI(节/注释/引号/转义/重复键覆盖)、错误路径与边界(空输入/超深/超长/实体炸弹)。
- 序列化往返:parse → serialize → parse 一致;结构体往返:structure → serialize → parse → structure 深度相等。
- 反射测试:四个宏全部双向转换、枚举宏、容器/嵌套/可选字段、ADL 手写覆盖、严格模式报错、XML 属性-元素-文本映射规则、INI 节映射规则。
- 剥离矩阵测试:仅反射核心无任何格式、核心+JSON、核心+XML、核心+INI、核心+JSON+XML、核心+JSON+INI、核心+XML+INI、全开八种组合逐一编译并跑反射测试。
- 性能基准:三种格式各自百字节级小文档 ops/s(均 3-8M 目标)、大文档 MB/s;反射转换 vs 手写代码对比;MSVC/GCC 各跑一轮;SIMD 与标量对比。

## 9. 范围排除(明确不包含)

- XML↔JSON 自动互转(但反射核心使其天然可达)。
- XPath/XSLT/流式 DOM(SAX 与 DOM 分离)。
- 外部 DTD/实体拉取(XML 安全默认关闭)。
- 非 x86-64 架构优化(平台扩展留接口)。
- 基于 C++26 P2996 反射的零宏自动推导(约束为 C++17,宏方案是 C++17 可行上限)。

## 10. 目录与构建

- `CMakeLists.txt`:`add_library(jsonxx STATIC)`;`tests`、`bench` 独立 target。
- 格式适配器开关:`JSONXX_ENABLE_JSON` / `JSONXX_ENABLE_XML` / `JSONXX_ENABLE_INI`(CMake 选项 + 编译宏),默认全 `ON`;关闭某格式即从构建中剔除其头文件引用与 `.cpp` 编译单元。
- `-O2`(bench/release 用 `-O3 -march=native` 可选)。
- 无第三方依赖。
