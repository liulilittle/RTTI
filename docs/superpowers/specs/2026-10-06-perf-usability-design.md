# RTTI 性能与易用性强化阶段设计

日期:2026-10-06
项目:RTTI(header-only C++17 反射 + JSON/XML/INI 三格式序列化库)
基线:主构建 1245 assertions / 0 failures,零警告;剥离矩阵八组合全绿。
前置设计已获用户批准(2026-10-06)。

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
- **浮点与标量根 bug 修复测试**:并入既有 json/xml/ini_test.cpp(补断言)或 naming_config_test(归入修复验证)。
- **全部测试 TU 跨 TU 类型唯一命名**(ODR 教训)。
- **验收标准**:
  - 双构建:主 build(全开)与剥离矩阵八组合全绿,零 C4xxx 警告。
  - bench/bench_parse.cpp 前后对照:性能方向不劣化(浮点/哈希/扫描,以实测为准),checksum 语义一致。
  - 新增断言数:三项新测试文件 + 既有文件补断言,总计 >1245。
  - 文档:README.en.md / README.zh-CN.md 补新能力小节(双语互索引)。

## 8. 非目标(明确排除)

- 不升级 C++ 标准(保持 C++17)。
- 不引入第三方依赖、不引入 modules/coroutines。
- 不支持 wchar_t/u16 字符串类型(本阶段),不支持 std::monostate,不支持 variant 多态访问以外的访问模式。
- 不重写既有 value_io 接口(仅扩展 config 与 convert 分派)。
- 不做破坏性 API 变更(全部向后兼容,新增字段带默认值)。

## 9. 风险与缓解

- **basic_string 特化可能误伤序列容器**:3.2 SFINAE 限制 char;测试覆盖确认。
- **variant/tuple 分派顺序**:置于容器 trait 之前,且 variant 可能含容器元素(递归分派已支持嵌套),用 if constexpr 保证无歧义。
- **哈希索引破坏插入顺序**:索引仅用于 get_key 查找,set_key/get_key_at 顺序路径保持,测试覆盖插入序语义。
- **浮点短表示丢失精度**:用 std::to_chars 最短往返保证(round-trip 无损失),测试断言解析回同值。
- **配置扩展影响既有调用**:全部新字段带默认值(identity/false/nullptr),既有调用不变。
