# RTTI 性能与易用性强化阶段设计

日期:2026-10-06
项目:RTTI(header-only C++17 反射 + JSON/XML/INI 三格式序列化库)
基线:主构建 1245 assertions / 0 failures,零警告;剥离矩阵八组合全绿。
前置设计已获用户批准(2026-10-06)。

## 00. 产品愿景(用户最终定位,C++ 反射/序列化领域前所未有)

用户明确要求:**C++ 没有好用的反射/序列化库,本库要与用户一起造出来——强大无可匹敌、顶级性能、顶级易用性、人类历史上最强大的反射库**。

- **无可匹敌的反射面**:§17-21(.NET System.Reflection 级:特性/实例化/类型判断/模板反射/方法反射/全量操作)+ §0 绝对安全。
- **顶级的性能**:§C 键哈希/SIMD/arena 节点池/零拷贝路径 + bench 可量化验证。
- **顶级的易用性**:§16 全类型↔字符串、§23 转换宽容、§21.4 编译期宽容/杜绝 ADL 错误堆、反射总纲。
- **全面格式**:JSON/XML/INI/TOML/protobuf 2/3 + 结构体↔格式直接往返。
- **最终意义(用户)**:真正让 C++ 成为最强现代编程语言——补齐 C++ 长期缺失的顶级反射与序列化基座,使 C++ 在元编程/数据互操作/ORM 领域达到 .NET/Java 级体验。
- 本 spec 全部节(§0-§23)即该愿景的落地点;验收以"双构建全绿零警告 + 剥离矩阵 + bench + fuzz 探针"为硬门。

## 0. 工程质量原则(用户明确要求,为全局硬性约束,优先级高于一切功能需求)

以下为用户反复强调的**原则问题**,任何功能实现均不得违反:

1. **绝无问题**:正确性是硬门;错误码路径、边界条件、往返一致性必须经测试证明,禁止"差不多能用"。
2. **绝不崩溃**:禁止未定义行为、越界访问、解引用空指针、悬垂引用;所有 API 对非法输入返回 error_code 而非崩溃;C++17 `if constexpr`/SFINAE 静态判别,运行期错误走错误码返回。
3. **绝不泄露**:header-only 库须 RAII 全覆盖;arena 分配必须配对释放;move 语义必须正确(避免 double-free/悬垂);文档析构必须释放全部节点与字符串;禁止裸 new/delete 不配对;禁止手动资源管理路径。
4. **绝对安全**:类型安全(禁止 void* 滥用转强制的静态类型破坏)、内存安全(引用生命周期约束)、线程安全(文档级隔离,每个实例独立状态)、异常安全(noexcept 标注 + RAII 保证异常传播无泄漏);禁止格式解析器被畸形输入击穿(深度/长度/次数上限防 DoS)。
5. **验收强制**:每个新功能必须附带测试证明不崩溃/不泄露/正确;全量回归(git 化后每次变更后跑主构建 + 剥离矩阵)是代合入前提;Vireak 探针(畸形输入/超大深度/空输入/部分 UTF-8)必须通过。
6. **反射/数组越界禁止(用户追加硬性原则)**:反射对容器/数组的全部操作(索引访问、迭代器解引用、fixed_sequence 读写、原生数组/多维数组转换、元素 get/set/erase/for_each)一律不得越过目标数组/容器的实际大小:
   - 编译期:已知固定大小(std::array/原生数组 extent)用 `std::get<I>`/`std::extent` 保证,越界索引在编译期经 if constexpr/边界校验拒绝或走运行期安全。
   - 运行期:越界访问返回 error_code::out_of_range / 返回 nullptr / 安全短路(绝不读越界内存)。
   - 转换路径:from_value 写入固定大小数组前校验 `src->size() <= out.size()`(超长截断或返回错误,spec §12 fixed_sequence 已有 min 截断,全路径保持一致);to_value 读数组用容器真实 begin/end(不越界)。
   - 迭代器:解引用前校验 index < size;rbegin/rend 反向迭代同样受 size 约束。
   - 任何反射 API 禁止在数组/容器越界时产生 UB、断言崩溃或读取越界字节;全部返回错误码/安全空值。
7. **底层责任原则(用户追加澄清)**:上述全部安全性(类型不得写错、内存安全、越界防护、错误码)由**库底层实现保证**,而非要求上层调用方严格遵循使用规范:
   - 调用方即使传入错误类型、错误字段名、错误维度、越界索引,底层也必须安全运行期失败(返回 error_code/空值/不生效),绝不让上层"小心使用"成为安全前提。
   - 底层 API 实现侧是强约束(内部必须正确),调用侧无先决条件(用户不需要预先验证类型)。
   - "类型不能写错"指底层类型系统/转换保证类型正确性(经 §22 转换宽容 + §19 类型判断 + trait 静态校验);上层只是受益者。
8. **不存在目标禁止读写(用户追加安全基本保证)**:反射目标(字段/容器元素/方法/构造器/类型/特性)不存在时,必须返回错误——**禁止写不存在目标、禁止读不存在目标**:
   - 写路径:set_field/add_field/erase_field/构造注入/方法调用对不存在目标返回 error_code::no_such_field(或等价),操作不生效,绝不在无效地址写。
   - 读路径:get_field/find_field/迭代/类型判断对不存在目标返回 false/nullptr/error_code,绝不解引用无效指针。
   - 实现:所有反射入口先经字段表/类型表查名,查不到即返回错误;查表命中才进行读写。禁止裸指针猜测/假定目标存在。
9. **编译/链接无条件通过(用户追加最低级门槛)**:禁止任何"无法编译/无法连接"的低级问题——header-only 库在任何合法使用组合下都必须可编译可链接:
   - **ODR 合规**:所有 header 中的类外模板/非模板函数定义必须 inline(或模板内联),任意多个 TU include 同一头、链接同一 exe 不得 LNK2005 重复符号(教训:json.hpp 曾全为非 inline 类外定义,单 TU 侥幸通过,多 TU 即 LNK2005)。
   - **全组合编译**:新增测试 TU 加入主回归(CMakeLists)即验证;剥离矩阵每种门组合也必须可编译可链接。
   - **每变更回归**:任何代码变更后必须跑主构建(编译+链接)+ 全量测试,确认零编译错误、零链接错误、零警告;编译/链接失败即阻塞合入,禁止带病推进。
   - **CI 契约**:编译/链接通过是最低门槛,任何功能验收以此为前提。

## 1. 目标与范围

在既有库之上实现四个方向,保持双构建全绿零警告、C++17 硬约束、零第三方依赖、全平台可移植、header-only、全英语注释:

- **A. 自定义 std::allocator / std::less 完整支持**:自定义分配器的字符串(basic_string 特化)、容器(序列/map/set/适配器)的序列化/反序列化正确性;自定义比较器 map/set 三格式往返。
- **B. 更丰富类型支持**:`std::optional` / `std::variant` / `std::tuple` / `std::pair` / `std::vector<bool>` / `std::array` 在三格式下的往返。
- **C. 性能提升与正确性修复**:JSON 对象键哈希索引、浮点序列化统一短表示、SIMD 扫描扩展、减少临时分配;修复探针发现的真实 bug(标量根序列化丢值、XML/INI 浮点 `3.1400000000000001`)。
- **D. 配置/映射灵活化**:JSON 键命名策略(identity/snake/camel)、字段改名映射(rename_map)、忽略缺省/空字段(skip_empty)、自定义缩进;XML/INI 同步命名策略。

## 2. 背景事实(探针验证,附证据)

以下事实全部经编译+运行探针验证:

1. **自定义 allocator 字符串缺陷**:`std::basic_string<char, traits, CustomAlloc>` 被 `is_sequence` 匹配 → 序列化为字符数组 `[104,105]`。根因 `is_sequence`(reflection.hpp:47-58)仅硬编码排除 `std::string`/`std::wstring`。
2. **自定义 allocator/less 容器**:`std::map<..., ci_less>`、`std::vector<T, CustomAlloc>`、自定义 allocator 键 map 已可往返(shape 成员检测生效)。
3. **`std::array` 与 `std::tuple` 不被 `is_sequence` 匹配**(探针 is_sequence=0),需新增处理路径。
4. **`std::optional<int>` 当前 from_value 返回 err=17(kind_mismatch)**,需新 converter。
5. **JSON `get_key` 为线性扫描**(json.hpp:316-321 `for (const auto& e : obj)`),大对象查找可哈希优化。
6. **真实 bug——标量根序列化丢值**:`rtti::xml::parse("...<root>3.14</root>")` 后 `serialize` 仅输出 XML 声明、丢 `<root>` 内容;`ini::document` 直接 `make_scalar(3.14)` 后 `serialize` 输出为空。XML/INI 对**文档级标量根**(非反射结构体包装)序列化路径有缺陷。
7. **真实 bug——浮点长尾**:XML/INI 序列化 `3.14` → `3.1400000000000001`(JSON 为 `3.14`),浮点格式化路径不一致。

## 3. A 节:自定义 allocator/less 完整支持

### 3.1 is_sequence 排除所有 basic_string 特化(reflection.hpp)

将 `is_sequence` 的排除条件从"硬编码 std::string/wstring"改为检测**任意 basic_string 特化**:

```cpp
template <typename T>
struct is_char_string : std::false_type {};
template <typename Char, typename Traits, typename Alloc>
struct is_char_string<std::basic_string<Char, Traits, Alloc>> : std::true_type {};
```

`is_sequence` 主特化条件加 `!is_char_string<T>::value`。C++17 不特化 `std::is_same` 对模板模板参数的任意匹配,须用上述 trait。

### 3.2 converter 泛化为 basic_string(convert.hpp)

`converter<std::string>` 改为部分特化 `converter<Char, Traits, Alloc>`(或保留 `std::string` 特化 + 新增 basic_string 通用特化,后者优先级低不冲突):

```cpp
template <typename Char, typename Traits, typename Alloc>
struct converter<std::basic_string<Char, Traits, Alloc>> {
    static error_code from(const value_io* src, std::basic_string<Char, Traits, Alloc>& out);
    static error_code to(value_io* dst, const std::basic_string<Char, Traits, Alloc>& in);
};
```

约束:仅 `char` 字符类型进入字符串路径(`wchar_t`/`char16_t` 不在本阶段范围,返回 kind_mismatch 或编译期排除——采用 SFINAE `std::enable_if_t<std::is_same_v<Char,char>>`,非 char 的 basic_string 保持现状不误伤)。

### 3.3 容器适配器保留 allocator/less(convert.hpp)

queue_from(267)/stack_from(308)/priority_queue_from(346) 当前用 `std::queue<typename Q::value_type>` 临时交换,丢失自定义容器/分配器。改为保留完整模板参数:

```cpp
std::queue<typename Q::value_type, typename Q::container_type> empty;
```

(container_type 已含 allocator;`std::queue<T>` 默认 deque<T, allocator<T>>,显式 container_type 保留自定义分配器。)priority_queue 同理用 `typename PQ::container_type`。

### 3.4 测试

新增 `tests/custom_alloc_test.cpp`(独立 TU,类型/枚举跨 TU 唯一命名,遵循 ODR 教训):

- 自定义 allocator 的 `basic_string` 三格式往返(必须为字符串非字符数组)。
- 自定义 allocator 的 `vector<int>` / `map<AStr,int>` / `set<int>` 三格式往返。
- 自定义 less 的 `map<string,int>`/`set<string>` 三格式往返(大小写不敏感,去重语义保持)。
- 自定义 allocator+less 组合容器。
- queue/stack/priority_queue 自定义底层容器/分配器往返。
- 注册入 tests/CMakeLists(RTTI_TEST_SOURCES 无条件,核心能力)。

## 4. B 节:更丰富类型支持

### 4.1 std::optional<T>

- 语义:**空 optional** 从缺失字段/null 读取 → `nullopt`;有值 → 解析入 `*out`。序列化:空 → null(JSON)/省略(XML/INI);有值 → 常规序列化。
- 实现:convert.hpp 新增 `is_optional` trait(`T::value_type` + `std::is_same_v<T, std::optional<typename T::value_type>>`),在 `from_value/to_value` 分派中置于容器 trait 之前(optional 无 push_back 等,不冲突)。
- 无空 optional 概念(缺失字段语义由调用方用 has/get 判断;`*out = std::nullopt` 后返回 ok)。

### 4.2 std::variant<Ts...>

- 语义:序列化为带索引 tag 的对象,JSON `{"index":N,"value":V}`;XML `<root><index>N</index><value>V</value></root>`;INI `index=N` + `value=V`。反序列化:读 index 字段 → 编译期 `std::get<N>` 分派。
- 实现:convert.hpp `is_variant` trait;`variant_to` 用 `std::visit`(C++17)+ `std::variant_size`;`variant_from` 读 index(整数)→ `std::get<index>`(运行时索引用 if constexpr 递归或 std::index_sequence 展开)。
- 不支持 `std::variant_monostate`(本阶段外,kind_mismatch)。

### 4.3 std::tuple<Ts...>

- 语义:序列化为数组/元素列(JSON `[a,b,c]`,XML/INI 同序列语义,与 vector 并列元素一致)。反序列化按 index 写回。
- 实现:`is_tuple` trait(`std::tuple_size`,但须排除 `std::array`——其也特化 tuple_size,见 4.6);`tuple_to`/`tuple_from` 用 `std::make_index_sequence` 展开(探针验证:MSVC 上 `std::apply(index_sequence)` 因 tuple_size 特化不可靠,C2131)。

### 4.4 std::pair

- 标量 pair(非 map value_type 语境):序列化为 2 元素数组 `[first,second]`,反序列化同。map 的 pair 已内建,本项仅补泛型 pair converter(trait `is_pair`,置于 is_map 之后——pair 有 first/second 无 key_type/mapped_type,不冲突)。

### 4.5 std::vector<bool>

- 已验证 vector<bool> 有 value_type/push_back/begin/end,应已走 sequence 路径;补三格式往返测试(注意 proxy reference,序列化用 const auto& 读取 bool,反序列化 push_back(bool))。
- 序列化表现沿用序列族语义:JSON `[true,false]`;XML/INI 与 int vector 一致(重复元素列)。

### 4.6 std::array<T,N>

- 新增定长序列 trait:`is_fixed_sequence`(有 `value_type`+`begin`/`end` 但**无 push_back** 且非 basic_string),置于 is_sequence 之前。from:按 size 逐 index from_value;to:make_array + 逐元素。
- **实现注意(探针验证)**:`std::array` 含 `std::tuple_size` 特化,因此 **不能用 `!is_tuple_t<T>` 排除 array**——tuple 判别改为"可被 `std::get` 解构且非 array"(用 `std::tuple_element` + 排除 `std::array` 特化,或用 `T::push_back` 缺失 + 非 basic_string + `std::is_base_of<...>` 强判别)。`is_fixed_seq_t` 满足条件:has_val=1、has_begin_end=1、has_pb=0(探针实测 std::array<int,3>)。

### 4.7 分派顺序(convert.hpp from_value/to_value)

```
is_optional → is_variant → is_tuple → is_pair → is_fixed_sequence →
is_map → is_set → is_priority_queue → is_queue → is_stack →
is_sequence → is_reflected → has_adl → converter
```

(optional/variant/tuple 在容器之前;现有容器顺序保持。)

## 5. C 节:性能提升与正确性修复

### 5.1 JSON 对象键哈希索引

- 现状:json.hpp node `obj` 为 `std::vector<std::pair<const char*, node*>>`,`get_key_impl` 线性扫描。
- 改法:node 增可选哈希索引(惰性构建,`std::unordered_map` 或排序+二分)。**必须保持插入顺序**(set_key 仍 push_back,哈希仅加速 get_key 查找)。C++17 实现:惰性构建哈希,`get_key` 命中即返回;顺序遍历 get_key_at 不变。
- 权衡:小对象(≤8 键)线性扫描更快,哈希仅当 obj.size()>阈值(如 16)时惰性构建。性能基准前后对照。

### 5.2 浮点序列化统一短表示

- 现状:JSON 走 `to_chars` 短表示(3.14);XML/INI 走 `snprintf("%g" 或默认流)` 产出 `3.1400000000000001`。
- 改法:三格式共用同一 `detail::format_real(double, std::string&)` 核心(`std::to_chars` 最短往返 + 确保小数点存在)。移除三处各自实现。
- 修复对象:xml.hpp serialize、ini.hpp serialize、json.hpp append_real(统一入口)。

### 5.3 SIMD 扫描扩展

- 现状:scan.hpp 门控设计;JSON scan_string/XML 文本为上一轮保留原循环处。
- 改法:评估将 JSON 引号/转义扫描换入 scan.hpp 掩码(新增 `json_string_mask`:`"`+`\`),**基准不劣化即接受**;XML 文本 `<` 扫描已换入。以 bench 实测为准,不强制。

### 5.4 减少临时分配

- 解析侧:连续性文本段直接 append(避免中间 std::string);序列化侧缓冲预分配(`reserve`)。
- 以 bench 前后对照验证,不劣化即接受。

### 5.5 正确性修复(探针发现的真实 bug)

- **标量根序列化丢值**:XML `serialize(document)` 对根标量(无元素包装)必须输出标量文本(如 `<root>3.14</root>` 或根标量转义输出),INI 对根标量必须输出 `key` 值(定义:INI 无根节键时输出标量原文)。修 xml.hpp serialize_into / ini.hpp serialize 对 `root_ 标量` 的路径。
- **XML/INI 浮点长尾**:归入 5.2 统一格式化。

## 6. D 节:配置/映射灵活化

### 6.1 键命名策略(JSON)

`json::write_config` 扩展字段:

```cpp
enum class key_case { identity, snake, camel };   // 反射字段名 → 输出键名
```

- `identity`:原样(默认,向后兼容)。
- `snake`:`fieldName` → `field_name`(camelCase→snake_case)。
- `camel`:`field_name` → `fieldName`。
- 实现:核心头提供 `detail::to_snake_case(string_view)` / `to_camel_case(string_view)` 纯函数(零依赖);在 `reflect_to`(convert.hpp)输出键名时应用。

### 6.2 字段改名映射(rename_map)

```cpp
struct write_config {
    // ...现有字段...
    const std::map<std::string, std::string>* rename_map = nullptr; // 字段名 → 输出键名
};
```

有映射则优先用映射名,否则按 key_case 策略。parse 侧对称:`parse_config` 提供反查(输出名 → 字段名),从 JSON/XML/INI 读取时先查映射。

### 6.3 忽略缺省/空字段(skip_empty)

- JSON write_config `bool skip_empty = false`:`null`/空数组/空对象/空字符串/0 值字段跳过。
- 语义:反射字段经 `field_access_base::is_default` 或值等价判断。仅序列化侧(parse 侧无缺省概念)。

### 6.4 自定义缩进(JSON/XML/INI)

- JSON 已有 `indent`(int,0=compact);扩展 `indent_char`(默认空格,可选 `\t`)。XML 同。INI 无缩进概念(节/键值结构),跳过。
- 三格式 write_config 保持各自结构,不强制统一基类(header-only 轻量)。

### 6.5 三格式对称命名策略

- JSON 与 XML 共用反射字段命名策略(经 convert.hpp reflect_to/reflect_from 应用);INI 同。naming 转换函数放核心头(reflection.hpp 或新 `naming.hpp`),零格式依赖。
- 配置入口:各格式 parse_as<T>/serialize(const T&, cfg) 的 cfg 扩展(JSON/XML/INI 各自 struct 增字段,保持向后兼容默认值)。

## 7. 测试与验收

- **新增测试文件**:
  - `tests/custom_alloc_test.cpp`(A 节)
  - `tests/type_support_test.cpp`(B 节:optional/variant/tuple/pair/vector<bool>/array)
  - `tests/naming_config_test.cpp`(D 节:key_case/rename_map/skip_empty/缩进)
  - `tests/static_array_test.cpp`(G 节:原生 [N]/多维/std::array 多维/维度元信息)
  - `tests/fuzz_test.cpp`(畸形/极端输入硬化探针 + 结构体随机往返)
  - `tests/toml_test.cpp`(F 节,if RTTI_ENABLE_TOML)
  - `tests/proto_test.cpp`(H 节,if RTTI_ENABLE_PROTO)
- **浮点与标量根 bug 修复测试**:并入既有 json/xml/ini_test.cpp(补断言)或 naming_config_test(归入修复验证)。
- **全部测试 TU 跨 TU 类型唯一命名**(ODR 教训)。
- **畸形/极端输入验证(fuzz 硬化,用户追加要求)**:每个格式适配器必须经过畸形/边界输入验证——截断输入、超深嵌套(>上限)、超大整/浮点、非法 UTF-8、空输入、未闭合括号/引号、多余尾部、重复键、null 字节、随机字节流(确定性种子伪 fuzz)。全部应返回 error_code(绝不崩溃/绝不泄露,spec §0),而非 UB/断言失败。新增 `tests/fuzz_test.cpp`(确定性伪随机输入 + 逐格式 parse + ASan 风格守卫由 assert 验证内存安全)。
- **结构体往返(fuzz 种子回读)**:同一 C++ 结构体经 JSON/XML/INI/TOML 各格式 `serialize` → `parse_as<T>` 往返值相等(含该结构体经随机字段变异后的往返,fuzz 驱动反射结构体往返)。
- **验收标准**:
  - 双构建:主 build(全开)与剥离矩阵(格式门扩展后 16/32 组合)全绿,零 C4xxx 警告。
  - bench/bench_parse.cpp 前后对照:性能方向不劣化(浮点/哈希/扫描,以实测为准),checksum 语义一致。
  - 新增断言数:各新测试文件 + 既有文件补断言,总计 >1245。
  - 文档:README.en.md / README.zh-CN.md 补新能力小节(双语互索引)。
  - fuzz_test 畸形输入探针:全部格式对随机/截断输入 0 崩溃 0 泄露,错误码返回正确。

## 8. 非目标(明确排除)

- 不升级 C++ 标准(保持 C++17)。
- 不引入第三方依赖、不引入 modules/coroutines。
- 不支持 wchar_t/u16 字符串类型(本阶段),不支持 std::monostate,不支持 variant 多态访问以外的访问模式。
- 不重写既有 value_io 接口(仅扩展 config 与 convert 分派)。
- 不做破坏性 API 变更(全部向后兼容,新增字段带默认值)。

## 9. 风险与缓解

- **basic_string 特化可能误伤序列容器**:3.2 SFINAE 限制 char;测试覆盖确认。
- **variant/tuple 分派顺序**:置于容器 trait 之前,且 variant 可能含容器元素(递归分派已支持嵌套),用 if constexpr 保证无歧义。
- **`is_tuple` 命中 `std::pair`**:pair 也特化 tuple_size 且无 data(),B1 探针实测 `is_tuple<pair>`=true。B2 分派须处理:pair 走 tuple 路径可正确往返(2 元素元组语义一致)则保持;否则 is_tuple 排除 pair 或调分派序。**裁决(B1 审阅携带):B2 实现时验证 pair 经 tuple 路径往返正确性,正确则接受 tuple 覆盖 pair(删除独立 pair_from/to 死代码),不正确则 is_tuple 排除 pair。**
- **哈希索引破坏插入顺序**:索引仅用于 get_key 查找,set_key/get_key_at 顺序路径保持,测试覆盖插入序语义。
- **浮点短表示丢失精度**:用 std::to_chars 最短往返保证(round-trip 无损失),测试断言解析回同值。
- **配置扩展影响既有调用**:全部新字段带默认值(identity/false/nullptr),既有调用不变。

## 10. E 节:反射迭代器/遍历与全容器兼容(用户追加需求)

### 10.1 目标

反射需支持完整的运行时遍历与元素操作,作用于**反射字段数组**与**容器字段的元素**,并覆盖**自定义 key / 自定义 less / 自定义 allocator 的全部容器**。

### 10.2 字段数组迭代器(reflect.hpp)

- 现有:`begin<T>()`/`end<T>()`(reflect.hpp:183-189)返回字段数组指针。
- 新增:
  - `rbegin<T>()`/`rend<T>()` 反向迭代(字段数组反向遍历)。
  - 迭代器对象类型(非裸指针):`field_iterator`(const field_access_base* 包装,支持 ++/--/解引用/比较),及 const 版本。begin/end/rbegin/rend 返回 field_iterator。
- range-for `for (const field_access_base* f : rtti::fields_of<T>::array)` 保持;新增 `for (auto it = rtti::rbegin<T>(); it != rtti::rend<T>(); ++it)` 反向。

### 10.3 容器元素迭代器

- 现有容器访问:回调式 `for_each_element`(reflect.hpp:167-179)与索引式 `field_size`/`erase_field_at`/`add_field`。
- 新增**元素迭代器**对象,对容器字段的元素提供:
  - `element_iterator`(begin(字段)/end、rbegin(字段)/rend 反向);
  - 解引用返回**元素值**(`field_value`,标量/字符串/嵌套结构体经 field_access 读取);
  - 元素访问含索引与键(关联容器);
  - 对 map 的键/值访问。
- 语义:迭代器须在容器有效期内有效;元素读取经 `typed_field_access` 的 get_value(经 value_io 中转或直接标量)。

### 10.4 元素访问三种形态

1. **元素值访问**:解引用直接返回 `field_value`(标量/字符串)。
2. **value_io 节点访问**:解引用经 value_io 节点承载元素(与 for_each 一致,通用)。
3. **元素类型获取**:`element_type<T>(name)` 返回 `type_desc`/`type_id`(经 type_of 解析容器元素类型)。

### 10.5 全容器自定义 key/less/allocator 兼容

- 元素迭代器/增删查必须对 `std::map<K,V,Less,Alloc>`、`std::vector<T,Alloc>`、自定义 allocator string 等**全部容器**可用。
- 增删查语义保持:A 节 map/set 自定义比较器经 insert/erase_key,序列经 push_back/erase_at;迭代器读取经 begin/end/rbegin/rend(容器原生),与 allocator 无关。
- 测试:自定义 key(自定义 string allocator 键)+ 自定义 less + 自定义 allocator 容器的迭代遍历/增删查/长度/类型获取全覆盖。

### 10.6 接口清单(reflect.hpp 新增)

```cpp
// 字段数组迭代器
template <typename T> class field_iterator;              // wrap const field_access_base*
template <typename T> field_iterator<T> rbegin();        // 反向开始
template <typename T> field_iterator<T> rend();

// 容器元素迭代器(经字段访问)
template <typename T> class element_iterator;            // index-based, forward + reverse
template <typename T> error_code element_begin(const T& obj, const char* name, element_iterator& out);
template <typename T> error_code element_end(const T& obj, const char* name, element_iterator& out);
template <typename T> error_code element_rbegin(const T& obj, const char* name, element_iterator& out);
template <typename T> error_code element_rend(const T& obj, const char* name, element_iterator& out);
// 解引用: field_value 元素值; value_io* 节点; std::size_t 索引; string_view 键(关联)
// 类型获取
template <typename T> type_desc element_type(const char* name);   // 经 type_of 解析容器元素类型
```

### 10.7 验收

- 字段数组正向+反向迭代测试;容器元素正向+反向迭代/解引用三形态测试;自定义 key/less/allocator 容器迭代与增删查测试。
- 三格式无关(纯核心 reflect 能力),不影响双构建断言基线(新增测试 TU 或并入既有 runtime_api/type_desc 测试)。

## 11. F 节:TOML 配置格式适配器(用户追加需求)

### 11.1 目标

第四个格式适配器,与 JSON/XML/INI 同架构:DOM(value_io 树)+ parse/serialize + parse_as<T>/serialize<T> 反射桥接 + SAX 流式 + 剥离门(RTTI_ENABLE_TOML)。

### 11.2 范围(TOML v1.0 核心子集)

- **键值对**:`key = value`;点分隔键 `a.b.c`;引号键。
- **基本表**:`[table]`(一维命名空间)。
- **数组表**:`[[table.of.arr]]`(元素为表,映射 array)。
- **标量类型**:字符串(基本/多行/字面)、整数(十进/十六进/八进/二进)、浮点(含 inf/nan)、布尔、日期时间(映射字符串 or 原生?→ 映射字符串,见 11.4)。
- **数组**:`[1, 2, 3]`(值混合;含嵌套表数组)。
- **注释/空白/转义字符串**。
- 不支持:严格 v1.0 的全部边角(dotted keys 与表嵌套的复杂规则可简化),优先级是主流配置用法可用。

### 11.3 value_io 映射

同 INI 模式:节→对象/嵌套;数组表→array;重复键→后者覆盖(与 INI 一致,文档化)。

### 11.4 日期时间 → 字符串(设计决策)

TOML 日期时间(offset datetime / local datetime / local date / local time)映射为规范字符串(如 `1979-05-27T07:32:00Z`),往返无损文本。标量解析为 string kind。测试覆盖 round-trip 文本一致性。

### 11.5 文件

- Create: `include/rtti/toml.hpp`、`tests/toml_test.cpp`
- Modify: `CMakeLists.txt`(RTTI_ENABLE_TOML option 默认 ON)、`tests/CMakeLists.txt`(if(RTTI_ENABLE_TOML) 追加)、`README.en.md`/`README.zh-CN.md`、剥离矩阵验证组合扩展为 16 种(多一个门)。

### 11.6 验收

- toml 测试 TU 独立编译全绿、零警告;主构建断言数增加;剥离矩阵 16 组合全绿;SAX 流式 + serialize(buffer) 入口与三格式一致。

## 12. G 节:[N] 静态数组 / 多维数组完美支持(用户追加需求)

### 12.1 目标

三种形态全部完美支持:

1. **原生 C 风格数组**:`T array[N]`、`T matrix[N][M]`、多维任意(如 `T cube[A][B][C]`)。
2. **std::array 多维**:`std::array<std::array<T,N>,M>` 多维嵌套(单维已在 B 节)。
3. **自定义维度元信息**:调用方声明维度(如 `[N,M]` 扁平存储 + 维元信息),value_io 树中表达维度。

### 12.2 原生数组支持(核心新能力)

- C 风格 `T[N]` 无 value_type/push_back/begin(但 `T*` 可 begin/end via std::begin(array))——**不被既有 is_sequence/is_fixed_sequence 匹配**。
- 新增 trait:`is_c_array<T>`(`std::is_array<T>::value` 且元素非 char(字符串字面量排除))、`is_md_array<T>`(嵌套数组)。
- 转换:`carray_to(frame 元素→make_array 递归)`、`carray_from(逐元素 from_value)`,经 `dimension_of<T>()`(编译期 `std::extent` 各维)。
- **std::array 多维**:嵌套 std::array 走已有 fixed_sequence 路径递归(B 节 fixed_sequence_from/to 已支持元素递归),补多级嵌套测试。

### 12.3 维度元信息(value_io 表达)

- 设计:序列化时数组节点携带维度元信息(经 naming/value_io 扩展或文档级 cfg,含 N/M/L 各自 extent)。JSON 表达为多维嵌套数组(自然);响应 JSON/XML/INI 各自的嵌套表达。
- 反序列化:按维度恢复(扁平数据按维元重组)。
- 实现:编译期 `extent 数组`(std::extent<T, I>)→ 运行时维度 vector,经 document 的根元信息或 value_io 扩展携带。**最小方案**:多维数组序列化为嵌套数组(JSON 天然;XML/INI 递归元素);反序列化支持 `int matrix[N][M]` 目标类型从扁平或嵌套数组读取。

### 12.4 分派

- 置于 fixed_sequence 之前的 `is_c_array`/`is_md_array` 分支;原生数组目标从 value_io 数组读取(known extent 校验)。

### 12.5 测试

- tests/static_array_test.cpp(新建):原生 `int[5]`、`int[2][3]`、`int[2][2][2]` 三格式往返;nested std::array 往返;[N,M] 维度元信息往返;越界/维度不匹配 → kind_mismatch(绝不崩溃,spec §0)。

## 13. H 节:protobuf 2/3 格式适配器(用户追加需求,定位=格式适配器)

### 13.1 目标

作为格式适配器与 value_io 树桥接:**解析 .proto 描述/schema 到反射表 + 经 value_io 树做 wire-format 序列化/反序列化**,与 JSON/XML/INI 同架构,不引入第三方依赖。

### 13.2 架构

- **proto 描述解析器**:解析 `.proto` 文件(message/enum/字段/编号/类型/repeated/optional/required/packed;proto2 与 proto3 语法;import 可简化),构建 schema 表(字段名→字段号→wire 类型→类型)。
- **反射表**:schema 经注册映射到 `fields_of<T>` 式反射(proto 字段 ↔ C++ 结构体字段,tag schema)。
- **wire 格式编解码**:varint/固定 64/长度前缀/固定 32(wire types 0/1/2/5),proto2 可选/必选/默认值,proto3 标量默认值省略,repeated packed。
- **反射桥接**:`parse_as<T>(wire) / serialize<T>(obj) → wire bytes`;与 value_io 树互转(JSON 侧可读 proto 消息等)。
- **SAX 流式**:wire 流式事件(字段号/类型/值),与三格式 parse_stream 同构。

### 13.3 关键设计决策

- **C++ 结构体定义 proto**:用既有 RTTI_DEFINE_TYPE 宏 + 字段号注解(新宏 `RTTI_DEFINE_TYPE_PROTO(字段号顺序)` 或 schema 注册表 `proto_schema<T>` 描述字段号/wire 类型)。**倾向后置**:用 `proto::schema<T>` 显式注册(字段名→号→类型),不扩展宏,保宏族稳定。
- **proto3 默认值省略组**:optional/repeated 经 value_io 空/缺检测。
- **压缩 packed**:repeated 标量 packed wire 类型 2 变体,解包/打包。
- **性能**:wire varint 快路径 + 长度前缀拷贝。
- **绝不崩溃**:畸形 wire(截断 varint/超长 length/未知字段号)→ error_code,防 DoS(深度/长度上限,spec §0)。

### 13.4 文件

- Create: `include/rtti/proto.hpp`、`tests/proto_test.cpp`
- Modify: `CMakeLists.txt`(RTTI_ENABLE_PROTO option 默认 ON)、`tests/CMakeLists.txt`、README 双语、剥离矩阵组合扩展到 32 种(4 门)。

### 13.5 验收

- proto_test 独立编译全绿零警告;主构建断言增加;剥离矩阵 32 组合全绿;proto2(optional/required/default)与 proto3(repeated packed/默认值省略)往返;畸形 wire 探针(via spec §0)全过。

## 14. I 节:日期时间序列化(用户追加需求)

### 14.1 目标

字符串 ↔ 数字时间戳双向序列化,类似 C# .NET DateTime 格式化:数字位数决定精度单位——

- **10 位**:Unix 秒(epoch seconds)
- **13 位**:Unix 毫秒(epoch ms)
- **16 位**:Unix 微秒(epoch µs)
- **19 / 20 位**:Unix 纳秒(epoch ns)

字符串形态(ISO 8601 子集):`2026-10-06T12:34:56Z`(带/不带毫秒小数)。数字与字符串可互转,精度按位数自动识别。

### 14.2 核心能力

- `date_time` 类型(值类型,封装 epoch 纳秒整数,无依赖,可 RTTI_DEFINE_TYPE):
  - `from_unix(seconds/ms/µs/ns)`、`to_unix_seconds/ms/µs/ns`(溢出检测,spec §0)。
  - `parse(string_view)` → ISO 8601 字符串 → date_time(错误返回 error_code)。
  - `format()/to_string()` → date_time → ISO 8601 字符串(round-trip 精确)。
  - 比较/加减/epoch 查询。
- **converter 桥接**:反射结构体字段类型为 `date_time` 时,JSON/XML/INI/TOML:
  - `to_value`:输出为**数字**(按 cfg 精度,默认秒,可选 ms/µs/ns)或字符串(ISO 8601)——由 config 选择。
  - `from_value`:输入数字(按位数识别 10/13/16/19/20)或字符串(ISO 8601)均解析。
- 数字位数识别:10 位 [1e9, 1e10) → 秒;13 位 → ms;16 位 → µs;19/20 位 → ns(负 epoch 前可选 `-` 号)。

### 14.3 文件与测试

- Create: `include/rtti/date_time.hpp`(值类型+parse/format,核心零格式依赖)、`tests/date_time_test.cpp`。
- 测试:四精度往返(秒/ms/µs/ns)、ISO 字符串 ↔ date_time 往返、数字位数自动识别、溢出/非法输入 → error_code(不崩溃)、边界(epoch 0/负/最大值)。

### 14.4 验收

- date_time_test 独立编译全绿零警告;主构建断言增加;反射结构体含 date_time 字段三格式往返;畸形输入探针(spec §0)全过。

## 15. J 节:2~64 位字符进制转换(用户追加需求)

### 15.1 目标

整数 ↔ 字符串的任意进制转换,底数范围 **2~64**(以字符集表示进制)。

### 15.2 字符集

- 底数 2~36:标准小写字母表 `0-9a-z`(36 全覆盖,与 C 库 strtol base 2-36 一致)。
- 底数 37~62:`0-9a-zA-Z`(62 个符号)。
- 底数 63~64:`0-9a-zA-Z` + 额外符号(63:+ / 64:- _ 或依应用选——**实现配置化字符集**,默认 RFC 4648 base64 风格 `A-Z a-z 0-9 + /`,留扩展点)。
- 对 base>36 提供**大小写敏感**;大小写不敏感解析可选(配置)。

### 15.3 API(核心纯函数,零格式依赖)

```cpp
namespace rtti { namespace detail {
// 数字 → 字符串(任意底 2..64,符号表可定制)
std::string to_base(uint64_t value, unsigned base, const char* digits = nullptr);
// 字符串 → 数字(溢出检测:超 uint64 上限 → error_code::overflow)
error_code from_base(string_view s, unsigned base, uint64_t& out);
// 便捷:负数(带符号,base 2..64,符号位 '-' 前缀)
std::string to_base_signed(int64_t value, unsigned base);
error_code from_base_signed(string_view s, unsigned base, int64_t& out);
} }
```

- `digits` 符号表默认按 §15.2 字符集(长度 ≥ base)。负号仅 signed 版本。
- 溢出/非法字符/logo 底数(0/1/>64)→ error_code(spec §0,绝不崩溃)。

### 15.4 反射桥接

- converter 扩展:`to_value` 对整数经 cfg 指定进制输出字符串;`from_value` 读字符串按 cfg 进制解析整数。
- 可挂接 date_time(数字时间戳 ↔ 任意进制字符串)。

### 15.5 文件与测试

- Create `include/rtti/base_convert.hpp`、`tests/base_convert_test.cpp`。
- 测试:断言 base 2/8/10/16/32/36(小写字母表)/62/64 往返;边界值(0/UINT64_MAX/负);溢出返回 error;非法字符/底 0/1/70;自定义符号表;round-trip 一致性(fuzz 随机整数 ↔ 字符串往返,spec §0)。

### 15.6 验收

- base_convert_test 独立编译全绿零警告;主构建断言增加;随机数与各底数往返全过;畸形输入探针全过。

## 16. K 节:全类型 ↔ 字符串统一互转层(用户追加需求)

### 16.1 目标

RTTI 对**所有支持的类型**提供 `T` 与字符串间的直接双向转换函数(统一 API 层,`rtti::to_string<T>` / `rtti::from_string<T>`),覆盖:

- 标量(int/uint/float/double/bool)、字符串本身、枚举(已有 enum↔label)。
- 容器(vector/map/set/list/deque/…,含自定义 allocator/less 全类型)。
- 新类型(optional/variant/tuple/pair/array/vector<bool>/原生 c 数组/多维数组)。
- 反射结构体(经字段串行化)。
- `date_time`(ISO 8601 或数字时间戳)。
- 任意进制整数(`base_convert`,2..64)。

### 16.2 设计

- **顶层 API**(核心头 `rtti/to_string.hpp` / 并入 convert.hpp):

```cpp
namespace rtti {
// 任何支持类型 → 字符串(经 JSON 单值编解码;标量经最小文本表示;反射/容器经紧凑 JSON)
template <typename T> std::string to_string(const T& v);
// 字符串 → 类型(错误返回 error_code;类型不匹配/超界 → error)
template <typename T> error_code from_string(string_view s, T& out);
// 便捷:返回 optional
template <typename T> std::optional<T> from_string(string_view s);
}
```

- **实现策略**:复用既有 `to_value/from_value` + 一个内置"文本 value_io"(string 源/汇),或对标量直接 `std::to_chars`/解析。**倾向复用 value_io 桥接**:`to_string<T>` 构造最小 JSON 树 → serialize(紧凑);`from_string<T>` 解析 → from_value。反射/容器/新类型自动继承;标量走直接快路径(不建树)。
- **确定性**:反射/容器字符串化用紧凑单行确定性格式(键序=声明序,不做 sort_keys)。

### 16.3 文件与测试

- Create `tests/string_convert_test.cpp`。
- 测试:全类型标量/容器/枚举/反射结构体/optional/variant/tuple/array/date_time/进制 ↔ to_string/from_string 往返;类型不匹配 error;超大值溢出 error;畸形字符串 error(spec §0)。

### 16.4 验收

- string_convert_test 独立编译全绿零警告;主构建断言增加;round-trip 一致(含 fuzz 随机结构体,spec §0);不崩溃不泄露。

## 17. L 节:反射特性/Attribute(用户追加需求,类 C# Attribute)

### 17.1 目标

反射提供**自定义特性(Attribute/annotation)系统**,类似 C# .NET:用户可为类型/字段声明额外元数据,运行时经反射查询。

### 17.2 能力

- **特性声明**:类型级与字段级特性(如 `[Serializable]`、`[ColumnName("user_name")]`、`[Ignore]`、`[MaxLength(128)]`、`[Range(1,100)]` 等)。
- **宏扩展**:侵入式宏 `RTTI_DEFINE_TYPE_ATTR` 或附加特性表(list-of-attribute 字段数组),与既有 `fields_of<T>` 表并列。
- **运行时查询**:
  - `attr_count<T>()` / `attr_at<T>(i)`。
  - `find_attr<T>("AttrName")` → 特性对象(名称 + 参数(uint64/字符串/浮点/标志))。
  - `field_attr<T>(field_index/name)` → 该字段的特性表。
  - `has_attr<T>(name)` / `is_defined` 快速判定。
- **实现**:特性 = `{ const char* name; std::vector<attr_arg>; }`,`attr_arg` 是 tag-union(uint64/double/string_view/bool)。类型级特性数组 + 每字段特性数组,经宏或显式注册注入 `fields_of<T>`。

### 17.3 文件与测试

- Modify `include/rtti/reflection.hpp`(attr 表 + 查询 API)、`include/rtti/rtti_fwd.hpp`(宏声明)。
- Create `tests/attr_test.cpp`。
- 测试:类型级/字段级特性声明与查询;参数多形态;无特性类型安全空表;仅核心零格式依赖。

### 17.4 验收

- attr_test 独立编译全绿零警告;主构建断言增加;与既有反射(字段表/迭代器/序列化)协同;不崩溃不泄露(spec §0)。

## 18. M 节:反射实例化创建 + 全量操作(用户追加需求,为 ORM 奠基)

### 18.1 目标

反射支持**运行时创建对象**(反射实例化),可经构造函数注入参数,并在创建后全量操作——这是后续 ORM 实现的根基。

### 18.2 能力

- **无参构造创建**:`make_object<T>()` → 反射创建 T(默认构造),返回 `std::unique_ptr<value_io>` 或经字段表访问的实例句柄。
- **带参构造注入**:`make_object<T>(args...)` → 经反射按参数调用 T 的构造函数(构造参数注入)。
  - 参数来源:标量(field_value)、value_io 节点、反射字段参数名。
  - 设计:构造器表(经宏/显式注册 `ctor<T>` 描述参数类型/名/可选),运行时按名称或位置匹配注入。
- **自定义内存分配/释放(用户追加要求,实现=placement new)**:实例创建支持调用方指定分配/释放策略——
  - `make_object<T>(allocator, args...)`:对象内存经调用方提供的分配器(如 `memory_arena`、monotonic 池、`std::pmr` 风格)取得**原始内存** → **`placement new` 就地构造** → 返回实例句柄。
  - 释放:显式调用 `instance.destroy()` = 析构函数 `obj->~T()` + 归还内存(经释放器);创建者手动管理生命周期(配合 arena/池复用)。
  - 语义:默认无分配器参数时走 RAII(unique_ptr 自动释放);提供分配器时转手动管理(placement new 构造 + 显式 destroy,文档化契约,防双释放)。
- **实例句柄**:`object_ref<T>`/`instance` 包装(持有 T 实例 + 反射表指针 + 可选分配/释放器),供后续字段读/写/遍历/序列化统一操作。
- **反射全量操作**(在实例上):
  - 字段 get/set(field_value、value_io、经迭代器)。
  - 容器增删查/长度/遍历(E 节迭代器)。
  - 特性查询(L 节 Attribute)。
  - 序列化/反序列化(JSON/XML/INI/TOML 反射桥接)。
  - 类型查询(type_of/type_desc/元素类型)。
- **ORM 预备语义**:对象存活期与池管理;构造参数经反射映射(列→字段→构造参数);`CreateInstance(ctor_args)` 与 `Bind<T>`(结果集行→对象)API 契约。

### 18.3 实现要点

- 构造器元数据:反射宏扩展(构造器表含参数名/类型/可选标志),或 `ctor_registry<T>` 显式注册(倾向后者,与 proto schema 同理,保宏族稳定)。
- 参数注入:编译期签名 + 运行期值(std::any 风格或 field_value 变体 + 类型检查);缺参/类型错 → error_code(绝不崩溃)。
- 零格式依赖:核心能力,不碰 value_io 实现(实例句柄为独立类型)。

### 18.4 文件与测试

- Modify `include/rtti/reflection.hpp`(ctor 表 + instance)、`include/rtti/reflect.hpp`(make_object/create_instance 便捷层)。
- Create `tests/instance_test.cpp`。
- 测试:无参/带参构造创建;参数注入错误(error);全量操作(字段/迭代/特性/序列化)在实例上工作;内存安全(unique_ptr RAII,无泄露)。

### 18.5 验收

- instance_test 独立编译全绿零警告;主构建断言增加;ORM 契约 API 可用;不崩溃不泄露(spec §0)。

## 19. N 节:反射类型判断(用户追加需求)

### 19.1 目标

反射提供运行时**类型判断**能力,类似 C#/C++ RTTI:

- **类型相等**:某类型是否等于 T(`is<T>(obj)`/`type_of<T>() == type_of<U>()`)。
- **继承关系**:某类型是否是 T 的派生(`is_base_of<T>(obj)` / `is_derived_from<T>(obj)`),运行时多态类型判断。
- **可转换(逆变)**:某类型是否能转换为 X(`is_convertible_to<T,U>` / `can_cast<U>(obj)`),含静态(编译期 trait)与动态(运行时多态向下转型)两层。

### 19.2 能力

- **静态(编译期)类型判断**(零开销,沿用既有 trait):
  - `rtti::is_same<T,U>`(std 已有,反射层提供便捷)、`rtti::is_base_of<Base,Derived>`(std 已有)。
  - `rtti::is_convertible<T,U>`(std::is_convertible 便捷)。
  - 与反射表联动:`type_of<T>() == type_of<U>()` 值相等判断。
- **动态(运行时)类型判断**(多态对象):
  - `rtti::is_instance_of<T>(void* obj, type_id expected)` — 运行时对象类型 vs 期望类型。
  - `rtti::dynamic_cast_ref<T>(instance&)` — 反射实例句柄向下转型(继承关系,失败返回 nullptr/error_code)。
  - `rtti::is_derived_from<T>(instance&)` — 运行时判断实例类型是否派生自 T(经反射 type_id 链或虚表)。
  - `rtti::can_convert(instance&, type_desc target)` — 反射层级内类型可转换性(同类型/基类/可隐式转换标量)。
- **多态支持**:反射结构体继承时,`fields_of<Derived>` 应含基类字段(继承字段合并),type_id 链记录 base 关系;动态判断经此链。

### 19.3 实现要点

- 继承字段合并:反射宏/注册使 Derived 的 fields_of 拼接基类字段(编译期 trait `base_types<T>` 列表 + 递归合并)。
- type_id 链:`type_desc` 扩展 `bases()`(基类 type_id 数组),运行时 is_derived_from 沿链查。
- 动态转换:经反射句柄 + type_id 链安全向下转型(禁裸 reinterpret_cast;经构造/字段表验证,spec §0)。

### 19.4 文件与测试

- Modify `include/rtti/reflection.hpp`(type_desc bases + 继承字段合并)、`include/rtti/reflect.hpp`(is_instance_of/dynamic_cast_ref/can_convert 便捷)。
- Create `tests/type_judge_test.cpp`。
- 测试:类型相等(值/type_id);继承字段合并(Derived 字段表含 Base 字段);is_base_of/is_convertible 便捷;运行时 is_derived_from(多态实例);dynamic_cast_ref 成功/失败;can_convert 同型/基类/标量转换;错误路径(非继承转型 → 安全失败,不崩溃)。

### 19.5 验收

- type_judge_test 独立编译全绿零警告;主构建断言增加;多态继承场景全过;不崩溃不泄露(spec §0)。

## 20. O 节:抽象类/虚函数判断 + 模板类型反射(用户追加需求)

### 20.1 目标

反射扩展类型内省:

- **抽象类判断**:某类型是否为抽象类(含纯虚函数,不可实例化)。
- **虚函数判断**:某类型是否含虚函数(多态类型,有 vtable)。
- **模板类型反射**:模板实例化的模板参数 `<T1,T2,T3>` 可被反射获取(参数类型列表、参数个数)。

### 20.2 能力

- **抽象/虚函数判断**(编译期零开销,基于标准 trait):
  - `rtti::is_abstract<T>`(std::is_abstract)、`rtti::has_virtual_destructor<T>`(std 已有)、`rtti::is_polymorphic<T>`(std::is_polymorphic)。
  - 反射便捷封装 + 经 type_desc 运行时查询(`type_desc::is_abstract()/is_polymorphic()` 编译期 bool)。
  - 结合 §19 动态判断:多态实例的动态转型须先验 is_polymorphic。
- **模板类型反射**:
  - `rtti::template_args<T>` — T 为模板实例(如 `std::vector<int>`、`MyBox<T1,T2,T3>`)时,反射其模板参数类型列表。
  - `template_arg_count<T>`(constexpr)、`template_arg_at<T, I>`(类型)、`template_args_of<T>()`(type_id/type_desc 数组)。
  - 判别:`is_template_instance<T>`(检测模板参数结构;经特化 trait 或 `template_info<T>` 注册)。
  - 编译期:C++17 无法直接取任意模板参数列表——用 `template_info<T>` 特化注册(类模板特化提供 args 类型包),宏辅助 `RTTI_TEMPLATE_INFO(T, A1, A2, ...)` 声明。
  - 运行期:template_arg 数组 → type_desc(type_of<TemplateArg>)→ type_judge 联动(与目标类型比较)。

### 20.3 实现要点

- is_abstract/is_polymorphic 走 std trait 便捷 + type_desc 封装(零新机制)。
- 模板反射经 `template_info<T>` 显式注册(倾向注册表而非自动推导,保宏族稳定;C++17 无 C++20 模板约束/概念便利)。
- 反射联动:template_info 与 fields_of/convert 并存(模板类型可同时是容器/反射结构体)。

### 20.4 文件与测试

- Modify `include/rtti/reflection.hpp`(is_abstract/is_polymorphic 便捷 + type_desc 扩展)、`include/rtti/rtti_fwd.hpp`(template_info 宏)。
- Create `tests/template_reflect_test.cpp`。
- 测试:is_abstract(抽象/非抽象类);is_polymorphic(含虚函数/不含);template_arg_count/args_of(单/多参模板如 Box<T1,T2,T3>);template_arg_at 类型正确;运行时与 type_of 比较;未注册模板安全空表;不崩溃(spec §0)。

### 20.5 验收

- template_reflect_test 独立编译全绿零警告;主构建断言增加;模板多参数场景全过;不崩溃不泄露(spec §0)。

## 21. 反射总纲(用户定位:以 .NET System.Reflection 为参照的超级强反射能力)

### 21.1 定位

用户明确要求:**打造人类历史上最强大的反射库**。以 `System.Reflection` 能力清单为参照(且超越),本项目反射面覆盖(下表);本 spec §0-§23 全部需求服务于该总纲。强度由 §0 安全性(绝不崩溃/泄露/越界/目标不存在)、§21.4 接口契约(编译期宽容/运行期错误码/杜绝 ADL 错误堆)、§22 转换宽容、§19/§20 类型与模板判断、以及全类型序列化桥接共同构成。

| .NET System.Reflection 能力 | 本项目对应(spec 节) |
|---|---|
| Type 内省(类型元数据) | type_of<T>/type_desc/type_id(§核心)、模板参数反射(§20) |
| 字段/成员枚举与访问 | fields_of 表 + 字段迭代器(begin/end/rbegin/rend, §E) |
| 属性/元素访问 | field_value/value_io 访问 + 容器迭代器(§E) |
| 方法/构造调用 | 构造注入创建 make_object(§18);方法反射待 §22 |
| 动态创建实例 | make_object<T>(args...) + placement new 自定义分配/释放(§18) |
| 类型判断 | is_base_of/is_convertible/逆变(§19)、抽象/虚函数/多态(§20)、类型相等 |
| Attribute 特性 | 类型级/字段级特性查询(§17) |
| 泛型/模板反射 | template_info<T> 模板参数列表(§20) |
| 序列化互操作 | value_io 树 ↔ JSON/XML/INI/TOML/protobuf(§11/13 等) |
| 字符串互转 | to_string/from_string 全类型(§16) |

### 21.2 剩余差距(§22 方法/事件反射)

.NET 还有**方法调用反射**(Invoke:名称→绑定参数→调用)与**事件/委托反射**。为达到 .NET 级:

- **§22 方法反射(待补)**:`reflect_method<T>("Name", args...)` 经注册表(method_info 表:名称→签名→函数指针)调用成员/静态方法;参数注入/错误码;与 ORM/全量操作联动。
- 事件/委托反射不在本阶段范围(标为未来方向,spec 明确排除避免范围蔓延)。

### 21.3 验收基准

- 反射能力对照 §21.1 表逐项有测试;核心能力零格式依赖;不崩溃不泄露(spec §0)。

### 21.4 反射接口契约:编译期宽容,运行期错误码(用户追加原则)用户明确要求:**无论调用方对反射 API 做什么调用,编译期都必须通过;操作在运行期因不匹配返回 error_code,不生效、不崩溃。**

- **编译期宽容**:所有反射入口(find_field/get_field/set_field/容器操作/方法调用/构造注入/类型判断/特性查询等)对任意名字/任意目标类型在编译期均可调用——不因字段不存在、类型不匹配、非容器、目标不可转换而在编译期拒绝。
- **运行期错误码**:上述不匹配一律运行期返回 error_code(no_such_field/kind_mismatch/not_abstract/not_convertible 等),操作不生效(不改状态),绝无崩溃/UB。
- **实现路径**:
  - 运行期查表(find_field 返回 nullptr→false;容器操作经 field_access 虚方法运行期分派,非容器返回 error_code::kind_mismatch)。
  - 类型判断(§19/§20):静态 trait 编译期可查(is_base_of 等);动态判断对不匹配返回 false/error,不做断言。
  - 构造/方法注入:参数不匹配/构造器缺失 → 运行期 error_code,不抛异常不崩溃。
  - 禁止:对反射 API 用 static_assert 硬拒绝(违编译期宽容),禁止编译期 SFINAE 静默移除用户可见调用(违可编译性)。
- **例外**:标量/容器转换等"类型级"接口(to_value/from_value)遵循类型系统强约束(它们不是反射 API);反射层 API 全部宽容。
- **杜绝 ADL/模板编译错误堆(用户追加原则)**:用户指出 C++ 模板一旦 ADL 不匹配会喷出成堆难懂错误。本项目全部模板接口(反射/转换/容器/格式)必须做到:
  - **显式限定**:模板调用路径全部限定在 `rtti` 命名空间内解析(如 `rtti::to_value`/`rtti::from_value`),避免依赖 ADL 在调用点意外找函数;ADL 钩子(has_adl_to/from)作为扩展点保留,但主分派永不依赖调用点 ADL。
  - **SFINAE 友好**:所有 trait/重载用 `if constexpr`/`enable_if` 干净降级,遇不支持类型走**运行期 error_code 或安全默认**,而非编译期硬错;禁止在调用方可见处产生"找不到匹配重载"的原生错误堆。
  - **友好诊断**:必须产生模板错误时,用 `static_assert(失败, "RTTI: <一句英文解释>")` 给出一条可读信息(如 "RTTI: type T is not convertible by this path — see spec §0"),而非让编译器展开几十层内部实例化;仍遵循"编译期宽容"主契约(反射层),此条仅约束必须拒绝的编译期路径。
  - **禁闭 ADL 反模式**:禁止 `using namespace` 污染调用点、禁止在全局命名空间引入会与用户类型 ADL 冲突的同名函数、禁止裸 `unqualified` 调用点依赖隐式查找。

## 23. P 节:类型安全转换宽容(用户追加原则)

### 23.1 目标

**保证类型操作安全**:能安全转换的类型间即使 `T` 不同也执行转换;可安全转换(不丢数据/不 UB)即转换,否则返回 error_code。覆盖标量间全部安全组合:

- **整数族**:`int/uint/char/unsigned char/short/ushort/long/ulong/long long/ulong long` 互相转换(范围检查,溢出→error_code::overflow)。
- **整数 ↔ 布尔**:`0→false`、`非0→true`;`false→0`、`true→1`。
- **整数 ↔ 浮点**:整数→浮点(精度损失按 IEEE 规则接受,不误报);浮点→整数(小数截断 + 范围检查,超范围→overflow)。
- **整数/浮点 ↔ 字符串**:数值→数字文本;字符串→数值(解析,非法→kind_mismatch)。
- **字符 ↔ 整数**:char 值 ↔ 对应 ASCII 整数;整数→char 需范围检查(0..255,超→overflow)。

### 23.2 契约

- **安全转换优先**:from_value 读目标类型时,若源 kind 可安全转换(范围/语义允许)则转换成功,不必严格同 kind。
- **错误返回**:不可安全转换(溢出/非法解析/类型完全无关)→ error_code,不崩溃、不改状态。
- **实现**:converter 主模板扩展——`from` 对每个目标标量类型:尝试 `as_integer`/`as_real`/`as_string`/`as_bool` 并按 §23.1 规则转换;`as_*` 返回 false 时走次候选。范围检查用 `std::numeric_limits`(编译期 if constexpr 分派)。
- **与 §21.4 关系**:本契约作用于**类型级转换** API(to_value/from_value,value_io 标量路径)——§21.4 的"编译期宽容"作用反射 API 层;此处是转换层"可转换即转换"的运行期宽容。两者独立、不冲突。

### 23.3 文件与测试

- Modify `include/rtti/convert.hpp`(converter 主模板 + scalar 转换规则)。
- Create `tests/scalar_convert_test.cpp`。
- 测试:int→char/bool/uint/float/string 全组合;uint→int(溢出 error);float→int(截断+范围);字符串→数值(合法/非法);bool→int;边界(UINT64_MAX→int 溢出;INT64_MIN);char↔int;round-trip;全部错误路径返回 error 不崩溃(spec §0)。

### 23.4 验收

- scalar_convert_test 独立编译全绿零警告;主构建断言增加;转换矩阵全过;错误路径安全;不崩溃不泄露(spec §0)。


