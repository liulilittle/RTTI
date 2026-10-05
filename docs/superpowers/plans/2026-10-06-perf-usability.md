# RTTI 性能与易用性强化阶段实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在既有 RTTI 库(JSON/XML/INI 三格式,主构建 1245/0 零警告)上实现四项强化:①自定义 allocator/less 完整支持;②新类型 optional/variant/tuple/pair/vector<bool>/array 支持;③性能优化 + 修复标量根序列化丢值与浮点长尾 bug;④配置灵活化(key_case/rename_map/skip_empty/缩进)。

**Architecture:** 核心扩展点在 convert.hpp(类型分派与 converter)、reflection.hpp(shape trait);三格式头仅补 config 字段与格式化核心。A/B 节是纯核心能力(与格式无关,格式经 from_value/to_value 自动获得);C 节修格式内部 bug 与性能;D 节经 convert.hpp 反射映射 + 三格式 config。

**Tech Stack:** C++17,header-only,MSVC 2022(/Zc:preprocessor /std:c++17 /W4 /MDd),自研测试 harness(RTTI_TEST_CASE/RTTI_CHECK),CMake。

**Spec:** `docs/superpowers/specs/2026-10-06-perf-usability-design.md`

## Global Constraints

- C++17 硬约束(禁 C++20 特性);代码/注释全英语;零第三方依赖;全平台可移植(禁 __declspec);header-only。
- 全部新字段带默认值,向后兼容(既有调用不变)。
- 测试类型/枚举跨 TU 唯一命名(ODR 教训:禁止与其他测试 TU 同名全局类型)。
- 测试文件加入 tests/CMakeLists.txt 的 RTTI_TEST_SOURCES(无条件追加,属核心能力;格式测试按 RTTI_ENABLE_* 门)。
- /W4 零 C4xxx 警告;双构建(主 build 全开 + build-off 三门全关)全绿。
- 编译验证模板(独立 cl):
  - `cl /nologo /Zc:preprocessor /std:c++17 /EHsc /W4 /MDd /I F:\json_xml\include /I F:\json_xml\tests /I <MSVC include> /I <ucrt> /I <shared> /I <um> tests\<file>.cpp tests\test_main.cpp build\Debug\rtti.lib /link /LIBPATH:<MSVC lib\x64> /LIBPATH:<ucrt lib\x64> /LIBPATH:<um lib\x64>`
  - MSVC 路径:`C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\{include,lib\x64}`;Kits:`C:\Program Files (x86)\Windows Kits\10\{Include\10.0.26100.0\ucrt,shared,um,Lib\10.0.26100.0\ucrt\x64,um\x64}`
- 全量回归:`cmake --build build --config Debug`;运行 `.\build\tests\Debug\rtti_tests.exe`。

## Review Focus

以下输入类/失效模式 spec 未逐一列举但实际用户会遇到:

1. **自定义 allocator 的字符串**被当字符数组序列化(`[104,105]` 而非 `"hi"`)——A 节测试必须覆盖三格式字符串形态。
2. **`std::array` 被 `is_tuple` 误判**(array 特化 tuple_size)→ `is_fixed_sequence` 判别必须排除 array,测试覆盖 array 三格式往返。
3. **MSVC `std::apply(index_sequence)` 不可靠**(tuple_size C2131)——tuple/variant 展开必须用 `make_index_sequence` 直接展开,测试在 MSVC 实跑。
4. **标量根序列化丢值**(XML 只出声明/INI 空输出)与浮点长尾 `3.1400000000000001`——C 节修复测试断言精确输出。
5. **variant fold 展开 `.size()` 对所有 alternate 实例化**——须 `std::variant_alternative_t` + `if constexpr` 隔离,测试覆盖非目标分支不炸编译。

---

### Task A1: is_sequence 排除任意 basic_string + is_char_string trait

**Files:**
- Modify: `include/rtti/reflection.hpp:47-58`(is_sequence 排除条件)
- Test: `tests/custom_alloc_test.cpp`(新建)

**Interfaces:**
- Consumes: `std::basic_string<Char,Traits,Alloc>`,现有 `is_sequence`。
- Produces: `template<class T> struct is_char_string`(检测 basic_string 特化);`is_sequence` 增加 `!is_char_string<T>::value`。

- [ ] **Step 1: 写失败测试(先建 custom_alloc_test.cpp)**

新建 `tests/custom_alloc_test.cpp`,含自定义 allocator:

```cpp
namespace rtti_test_lib {
template <typename T>
struct ProbeAlloc {
    using value_type = T;
    ProbeAlloc() noexcept = default;
    template <typename U> ProbeAlloc(const ProbeAlloc<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
    template <typename U> struct rebind { using other = ProbeAlloc<U>; };
    bool operator==(const ProbeAlloc&) const noexcept { return true; }
    bool operator!=(const ProbeAlloc&) const noexcept { return false; }
};
using ProbeStr = std::basic_string<char, std::char_traits<char>, ProbeAlloc<char>>;

RTTI_TEST_CASE(custom_alloc_string_is_scalar) {
    static_assert(!rtti::is_sequence<ProbeStr>::value,
                  "custom-alloc basic_string must not be a sequence");
    RTTI_CHECK(!rtti::is_sequence<ProbeStr>::value);
}
} // namespace rtti_test_lib
```

- [ ] **Step 2: 运行测试验证失败**

独立编译(命令模板见 Global Constraints,`tests/custom_alloc_test.cpp`),或全量 cmake 重编后跑 rtti_tests.exe。
Expected: FAIL(`is_sequence<ProbeStr>` 当前为 true)。

- [ ] **Step 3: 实现 is_char_string + 修改 is_sequence**

在 `include/rtti/reflection.hpp` 的 is_sequence 定义前新增:

```cpp
// Matches any std::basic_string specialization (any char type, traits,
// allocator). Excluded from the sequence family because a string is a
// scalar (text) type for conversion purposes.
template <typename T>
struct is_char_string : std::false_type {};
template <typename Char, typename Traits, typename Alloc>
struct is_char_string<std::basic_string<Char, Traits, Alloc>> : std::true_type {};
```

修改 is_sequence 主特化的 enable_if:

```cpp
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

(移除原 `!std::is_same<T, std::string>::value && !std::is_same<T, std::wstring>::value` 条件。)

- [ ] **Step 4: 运行测试验证通过**

Expected: PASS(`is_sequence<ProbeStr>` 现为 false)。
再跑全量 cmake 构建 + rtti_tests.exe:现有测试仍 1245/0(不破坏 std::string 等既有行为)。

- [ ] **Step 5: 提交(此项目非 git 仓库,跳过 commit;改为记录进度至 process.md)**

---

### Task A2: converter 泛化为 basic_string(字符串标量往返)

**Files:**
- Modify: `include/rtti/convert.hpp:89-103`(converter<std::string> 特化)
- Test: `tests/custom_alloc_test.cpp`

**Interfaces:**
- Consumes: `is_char_string<T>`(Task A1)。
- Produces: `converter<Char, Traits, Alloc>` 部分特化(char 字符),三格式自动获得自定义 allocator 字符串标量往返。

- [ ] **Step 1: 写失败测试**

```cpp
RTTI_TEST_CASE(custom_alloc_string_roundtrip_json) {
    using S = rtti_test_lib::ProbeStr;
    S in("hello");
    std::string j = rtti::json::serialize(in);
    RTTI_CHECK(j.find("\"hello\"") != std::string::npos);   // must be a string, not [104,101,...]
    auto back = rtti::json::parse_as<S>(j);
    RTTI_CHECK(back.ok());
    if (back.ok()) { RTTI_CHECK(back.value == S("hello")); }
}
```

- [ ] **Step 2: 运行测试验证失败**

Expected: FAIL(`serialize(in)` 产出 `[104,101,108,108,111]`,断言 `j.find("\"hello\"")` 失败)。

- [ ] **Step 3: 实现 basic_string converter**

将 `converter<std::string>` 特化替换为部分特化(保留 std::string 走同路径):

```cpp
template <typename Char, typename Traits, typename Alloc>
struct converter<std::basic_string<Char, Traits, Alloc>> {
    static error_code from(const value_io* src,
                           std::basic_string<Char, Traits, Alloc>& out) {
        string_view v;
        if (!src->as_string(v)) {
            return error_code::kind_mismatch;
        }
        out.assign(v.data(), v.size());
        return error_code::ok;
    }
    static error_code to(value_io* dst,
                         const std::basic_string<Char, Traits, Alloc>& in) {
        dst->make_scalar(field_value::of(string_view(in)));
        return error_code::ok;
    }
};
```

注:`field_value::of(string_view(in))` 要求 `in` 可隐式转 string_view——`std::basic_string` 有到 `std::string_view` 的转换(需 Char=char 或经 basic_string_view)。为严格,改为 `string_view(in.data(), in.size())` 构造,避免窄字符之外的隐式转换问题。若 MSVC 报 `field_value::of` 重载不匹配,改用:

```cpp
dst->make_scalar(field_value::of(rtti::string_view(in.data(), in.size())));
```

- [ ] **Step 4: 运行测试验证通过**

Expected: PASS。JSON 序列化 `"hello"`;parse_as 还原。全量回归仍绿。

- [ ] **Step 5: 记录进度至 process.md**

---

### Task A3: 容器适配器保留 allocator/less(queue/stack/priority_queue)

**Files:**
- Modify: `include/rtti/convert.hpp:262-283,303-340,344-379`(queue/stack/pq from)
- Test: `tests/custom_alloc_test.cpp`

**Interfaces:**
- Consumes: 现有容器 shape trait。
- Produces: queue/stack/pq 从 value_io 重建时保留自定义容器/allocator/比较器。

- [ ] **Step 1: 写失败测试(自定义底层容器适配器)**

```cpp
RTTI_TEST_CASE(custom_alloc_queue_roundtrip) {
    using Q = std::queue<int, std::deque<int, rtti_test_lib::ProbeAlloc<int>>>;
    Q q; q.push(1); q.push(2); q.push(3);
    std::string j = rtti::json::serialize(q);
    auto back = rtti::json::parse_as<Q>(j);
    RTTI_CHECK(back.ok());
    if (back.ok()) { RTTI_CHECK(back.value.size() == 3); }
}
```

(序列化端 queue_to 已用 `Q copy = in` 复制,可编译;from 端当前 `std::queue<typename Q::value_type> empty` 与 Q 模板参数不同,swap 需同类型——先确认当前编译失败点。)

- [ ] **Step 2: 运行测试验证失败/编译失败**

Expected: 编译错误(`std::queue<value_type>` 与 `Q` 类型不同无法 swap)或 FAIL。

- [ ] **Step 3: 实现保留容器类型**

queue_from:

```cpp
template <typename Q>
error_code queue_from(const value_io* src, Q& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    Q empty;                      // default-constructed with Q's own container/allocator
    out.swap(empty);
    // ... 其余不变
}
```

stack_from 同理(`Q empty; out.swap(empty);`)。priority_queue_from 同理(保留 `typename PQ::container_type`)。检查既有代码:原用 `std::queue<typename Q::value_type> empty`,替换为 `Q empty`(Q 自带 container_type + allocator,default 构造可编译)。

- [ ] **Step 4: 运行测试验证通过**

Expected: PASS。全量回归仍绿。

- [ ] **Step 5: 记录进度至 process.md**

---

### Task A4: 自定义 less map/set + allocator 键三格式往返测试

**Files:**
- Test: `tests/custom_alloc_test.cpp`

**Interfaces:**
- Consumes: A1-A3 成果。
- Produces: 无新接口;验证 map/set 自定义比较器 + allocator 键三格式(JSON/XML/INI)往返。

- [ ] **Step 1: 写测试(自定义 less + allocator 键三格式)**

```cpp
namespace rtti_test_lib {
struct ProbeCiLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    }
};
using ProbeLessMap = std::map<std::string, int, ProbeCiLess>;
using ProbeAllocMap = std::map<ProbeStr, int>;   // ProbeStr = custom-alloc string key
}

RTTI_TEST_CASE(custom_less_map_three_formats) {
    rtti_test_lib::ProbeLessMap m;
    m["Alpha"] = 1; m["beta"] = 2; m["ALPHA"] = 3;   // ci dedup -> size 2
    RTTI_CHECK(rtti::json::parse_as<rtti_test_lib::ProbeLessMap>(
                   rtti::json::serialize(m)).value.size() == 2);
    RTTI_CHECK(rtti::xml::parse_as<rtti_test_lib::ProbeLessMap>(
                   rtti::xml::serialize(m)).value.size() == 2);
    RTTI_CHECK(rtti::ini::parse_as<rtti_test_lib::ProbeLessMap>(
                   rtti::ini::serialize(m)).value.size() == 2);
}
RTTI_TEST_CASE(custom_alloc_key_map_three_formats) {
    rtti_test_lib::ProbeAllocMap m;
    m[rtti_test_lib::ProbeStr("k")] = 42;
    RTTI_CHECK(rtti::json::parse_as<rtti_test_lib::ProbeAllocMap>(
                   rtti::json::serialize(m)).value.at(rtti_test_lib::ProbeStr("k")) == 42);
    RTTI_CHECK(rtti::xml::parse_as<rtti_test_lib::ProbeAllocMap>(
                   rtti::xml::serialize(m)).value.at(rtti_test_lib::ProbeStr("k")) == 42);
    RTTI_CHECK(rtti::ini::parse_as<rtti_test_lib::ProbeAllocMap>(
                   rtti::ini::serialize(m)).value.at(rtti_test_lib::ProbeStr("k")) == 42);
}
```

- [ ] **Step 2: 运行测试验证通过(依赖 A1-A3)**

Expected: PASS(JSON 已验;XML/INI 若 fail 属既有 bug,需在 A4 内修复或记录进 C 节)。

- [ ] **Step 3: 将 custom_alloc_test.cpp 加入 tests/CMakeLists.txt RTTI_TEST_SOURCES**

在 `tests/CMakeLists.txt` 第 16 行 `type_desc_test.cpp` 后追加 `custom_alloc_test.cpp`。

- [ ] **Step 4: 全量回归**

cmake 构建 + rtti_tests.exe → 断言数 1245 + 新增 >0,0 failures,零警告。

- [ ] **Step 5: 记录进度至 process.md**

---

### Task B1: 新类型 shape trait(is_optional/is_variant/is_tuple/is_pair/is_fixed_sequence)

**Files:**
- Modify: `include/rtti/reflection.hpp`(shape trait 区,after line 114)
- Test: `tests/type_support_test.cpp`(新建)

**Interfaces:**
- Consumes: `std::optional/variant/tuple/pair/array`,is_char_string(A1)。
- Produces:
  - `is_optional<T>`(T=std::optional<U>)
  - `is_variant<T>`(有 std::variant_size<T>::type)
  - `is_tuple<T>`(有 std::tuple_size<T>::type 且 **非 std::array**——array 也特化 tuple_size)
  - `is_pair<T>`(有 first/second 成员)
  - `is_fixed_sequence<T>`(有 value_type+begin/end,无 push_back,非 basic_string)

- [ ] **Step 1: 写失败 trait 静态断言测试**

```cpp
#include <rtti/reflection.hpp>
#include <optional>
#include <variant>
#include <tuple>
#include <array>
#include <utility>

RTTI_TEST_CASE(new_type_traits_detect) {
    static_assert(rtti::is_optional<std::optional<int>>::value, "");
    static_assert(!rtti::is_optional<int>::value, "");
    static_assert(rtti::is_variant<std::variant<int, std::string>>::value, "");
    static_assert(!rtti::is_variant<int>::value, "");
    static_assert(rtti::is_tuple<std::tuple<int, char>>::value, "");
    static_assert(!rtti::is_tuple<std::vector<int>>::value, "");
    static_assert(!rtti::is_tuple<std::array<int, 3>>::value, "array must NOT be tuple");
    static_assert(rtti::is_pair<std::pair<int, int>>::value, "");
    static_assert(rtti::is_fixed_sequence<std::array<int, 3>>::value, "");
    static_assert(!rtti::is_fixed_sequence<std::vector<int>>::value, "");
    static_assert(!rtti::is_fixed_sequence<std::string>::value, "");
    RTTI_CHECK(true); // reachable
}
```

- [ ] **Step 2: 运行测试验证失败**

Expected: 编译错误(这些 trait 尚不存在)。

- [ ] **Step 3: 实现 trait**

```cpp
template <typename T, typename = void>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<T, std::void_t<typename T::value_type>> : std::bool_constant<
    std::is_same_v<T, std::optional<typename T::value_type>>> {};

template <typename T, typename = void>
struct is_variant : std::false_type {};
template <typename T>
struct is_variant<T, std::void_t<typename std::variant_size<T>::type>> : std::true_type {};

// std::array also specializes std::tuple_size, so exclude it explicitly.
template <typename T, typename = void>
struct is_tuple : std::false_type {};
template <typename T>
struct is_tuple<T, std::enable_if_t<is_variant<T>::value ? false : true, std::void_t<
    typename std::tuple_size<T>::type>>> : std::bool_constant<
        !std::is_same_v<T, std::array<typename std::tuple_element<0, T>::type,
                                      std::tuple_size<T>::value>>> {};
```

注:`is_tuple` 判别 std::array 需能取 `std::tuple_element<0,T>`——但 `std::tuple_size<array>` 存在,故 `std::tuple_element<0, array>` 也成立,无法用 `tuple_element` 区分。更稳判别:**`std::array` 有 `static constexpr std::size_t size()` 成员而 tuple 无,且 array 有 `data()` 而 tuple 无**。用 has_data(已有 `has_data` trait,见 reflection.hpp:854)判别:

```cpp
template <typename T, typename = void>
struct is_tuple : std::false_type {};
template <typename T>
struct is_tuple<T, std::void_t<typename std::tuple_size<T>::type>> : std::bool_constant<
    !rtti::has_data<T>::value> {};   // array has data(), tuple does not
```

(确认 `has_data` 在 reflection.hpp 中先于本 trait 定义,或本 trait 后置/前向声明。)

```cpp
template <typename T, typename = void>
struct is_pair : std::false_type {};
template <typename T>
struct is_pair<T, std::void_t<decltype(std::declval<T&>().first),
                              decltype(std::declval<T&>().second)>> : std::true_type {};

template <typename T, typename = void>
struct is_fixed_sequence : std::false_type {};
template <typename T>
struct is_fixed_sequence<T, std::enable_if_t<
    !is_char_string<T>::value && !has_push_back<T>::value, std::void_t<
        typename T::value_type,
        decltype(std::declval<T&>().begin()),
        decltype(std::declval<T&>().end())>>> : std::true_type {};
```

(需 `has_push_back` trait——若不存在,在 reflection.hpp 补:

```cpp
template <typename T, typename = void>
struct has_push_back : std::false_type {};
template <typename T>
struct has_push_back<T, std::void_t<decltype(std::declval<T&>().push_back(
    std::declval<typename T::value_type>()))>> : std::true_type {};
```
)

- [ ] **Step 4: 运行测试验证通过**

Expected: PASS(全部 static_assert 满足)。

- [ ] **Step 5: 记录进度至 process.md**

---

### Task B2: 分派顺序 + optional/variant/tuple/pair/array 转换实现

**Files:**
- Modify: `include/rtti/convert.hpp`(from_value/to_value 分派 + 新 converter 函数)
- Test: `tests/type_support_test.cpp`

**Interfaces:**
- Consumes: B1 trait。
- Produces:
  - `optional_from/optional_to`
  - `variant_from/variant_to`(JSON `{"index":N,"value":V}`;XML/INI 按 index/value 字段)
  - `tuple_from/tuple_to`(make_index_sequence 展开)
  - `pair_from/pair_to`(2 元素数组)
  - `fixed_sequence_from/fixed_sequence_to`(std::array)
  - from_value/to_value 分派新增分支,顺序:optional → variant → tuple → pair → fixed_sequence → map → set → priority_queue → queue → stack → sequence → reflected → adl → converter

- [ ] **Step 1: 写失败测试(optional)**

```cpp
RTTI_TEST_CASE(optional_roundtrip_json) {
    std::optional<int> oi = 42;
    auto j = rtti::json::serialize(oi);
    RTTI_CHECK(j.find("42") != std::string::npos);
    auto back = rtti::json::parse_as<std::optional<int>>("42");
    RTTI_CHECK(back.ok() && back.value && *back.value == 42);
    // empty optional -> null (JSON)
    std::optional<int> none;
    auto jn = rtti::json::serialize(none);
    RTTI_CHECK(jn.find("null") != std::string::npos);
}
```

- [ ] **Step 2: 运行测试验证失败**

Expected: 编译错误(分派无 optional 分支)。

- [ ] **Step 3: 实现 optional converter**

```cpp
template <typename T>
error_code optional_from(const value_io* src, std::optional<T>& out) {
    if (src == nullptr) return error_code::no_such_field;
    if (src->type() == value_io::kind::null) { out.reset(); return error_code::ok; }
    T v{};
    const error_code e = from_value(src, v);
    if (e != error_code::ok) return e;
    out = std::move(v);
    return error_code::ok;
}
template <typename T>
error_code optional_to(value_io* dst, const std::optional<T>& in) {
    if (!in.has_value()) { dst->make_scalar(field_value{}); return error_code::ok; } // null
    return to_value(dst, *in);
}
```

- [ ] **Step 4: 运行测试验证通过(optional)**

Expected: PASS。

- [ ] **Step 5: 扩展测试:variant/tuple/pair/array 往返**

```cpp
RTTI_TEST_CASE(variant_roundtrip_json) {
    std::variant<int, std::string> v = std::string("hi");
    auto j = rtti::json::serialize(v);
    RTTI_CHECK(j.find("\"index\"") != std::string::npos);
    auto back = rtti::json::parse_as<std::variant<int, std::string>>(j);
    RTTI_CHECK(back.ok() && back.value.index() == 1 && std::get<1>(back.value) == "hi");
}
RTTI_TEST_CASE(tuple_roundtrip_json) {
    std::tuple<int, std::string, double> t{1, "a", 2.5};
    auto j = rtti::json::serialize(t);
    RTTI_CHECK(j.find("2.5") != std::string::npos);
    auto back = rtti::json::parse_as<std::tuple<int, std::string, double>>(j);
    RTTI_CHECK(back.ok() && std::get<0>(back.value) == 1 &&
               std::get<1>(back.value) == "a" && std::get<2>(back.value) == 2.5);
}
RTTI_TEST_CASE(pair_roundtrip_json) {
    std::pair<int, std::string> p{7, "x"};
    auto back = rtti::json::parse_as<std::pair<int, std::string>>(
        rtti::json::serialize(p));
    RTTI_CHECK(back.ok() && back.value.first == 7 && back.value.second == "x");
}
RTTI_TEST_CASE(array_roundtrip_json) {
    std::array<int, 3> a{1, 2, 3};
    auto back = rtti::json::parse_as<std::array<int, 3>>(
        rtti::json::serialize(a));
    RTTI_CHECK(back.ok() && back.value[0] == 1 && back.value[2] == 3);
}
```

- [ ] **Step 6: 实现 variant/tuple/pair/array converter**

variant(variant_to 用 std::visit + index tag;variant_from 读 index → `std::get<I>` 经 make_index_sequence 展开,用 `std::variant_alternative_t` + `if constexpr` 隔离):

```cpp
template <typename V, std::size_t... I>
void variant_to_indexed(value_io* dst, const V& v, std::index_sequence<I...>) {
    std::visit([&](const auto& val) {
        dst->make_object();
        value_io* idx = dst->make_node();
        idx->make_scalar(field_value::of(static_cast<int64_t>(v.index())));
        dst->set_key("index", idx);
        value_io* valn = dst->make_node();
        (void)to_value(valn, val);
        dst->set_key("value", valn);
    }, v);
}
template <typename V>
error_code variant_to(value_io* dst, const V& in) {
    variant_to_indexed(dst, in, std::make_index_sequence<std::variant_size<V>::value>{});
    return error_code::ok;
}
```

variant_from(按 index 编译期分派):

```cpp
template <typename V, std::size_t I>
error_code variant_from_at(const value_io* src, V& out) {
    using Alt = std::variant_alternative_t<I, V>;
    Alt a{};
    const error_code e = from_value(src, a);
    if (e != error_code::ok) return e;
    out = std::move(a);
    return error_code::ok;
}
template <typename V, std::size_t... I>
error_code variant_from_indexed(const value_io* src, V& out, std::size_t idx,
                                std::index_sequence<I...>) {
    error_code e = error_code::kind_mismatch;
    (void)std::initializer_list<int>{
        (idx == I ? (e = variant_from_at<V, I>(src, out)) : e, 0)...};
    return e;
}
template <typename V>
error_code variant_from(const value_io* src, V& out) {
    if (src->type() != value_io::kind::object) return error_code::kind_mismatch;
    value_io* idxv = nullptr;
    if (!src->get_key("index", idxv)) return error_code::kind_mismatch;
    int64_t idx = 0;
    if (!idxv->as_integer(idx)) return error_code::kind_mismatch;
    if (idx < 0 || static_cast<std::size_t>(idx) >= std::variant_size<V>::value)
        return error_code::kind_mismatch;
    value_io* valv = nullptr;
    if (!src->get_key("value", valv)) return error_code::kind_mismatch;
    return variant_from_indexed(valv, out, static_cast<std::size_t>(idx),
                                std::make_index_sequence<std::variant_size<V>::value>{});
}
```

tuple(tuple_from/to 用 make_index_sequence 直接展开,禁 std::apply(index_sequence)):

```cpp
template <typename Tuple, std::size_t... I>
error_code tuple_to_seq(value_io* dst, const Tuple& t, std::index_sequence<I...>) {
    dst->make_array();
    (void)std::initializer_list<int>{
        (dst->push_back((void(dst->make_node()), [&]{ value_io* c = dst->make_node(); (void)to_value(c, std::get<I>(t)); return c; }()), 0))...};
    return error_code::ok;
}
```

(此 lambda 写法复杂,简化:展开 I 逐个 to_value:

```cpp
template <typename Tuple, std::size_t... I>
error_code tuple_to_seq(value_io* dst, const Tuple& t, std::index_sequence<I...>) {
    dst->make_array();
    (void)std::initializer_list<int>{
        ([&]{ value_io* c = dst->make_node();
              (void)to_value(c, std::get<I>(t));
              dst->push_back(c);
              return 0; }())...};
    return error_code::ok;
}
```
)

tuple_from 对称(按 I 逐个 from_value 到 `std::get<I>(out)`)。

pair:

```cpp
template <typename P>
error_code pair_to(value_io* dst, const P& in) {
    dst->make_array();
    value_io* f = dst->make_node();
    (void)to_value(f, in.first);
    dst->push_back(f);
    value_io* s = dst->make_node();
    (void)to_value(s, in.second);
    dst->push_back(s);
    return error_code::ok;
}
template <typename P>
error_code pair_from(const value_io* src, P& out) {
    if (src->type() != value_io::kind::array || src->size() < 2)
        return error_code::kind_mismatch;
    error_code e = from_value(src->at(0), out.first);
    if (e != error_code::ok) return e;
    return from_value(src->at(1), out.second);
}
```

array(fixed_sequence):

```cpp
template <typename A>
error_code fixed_sequence_to(value_io* dst, const A& in) {
    dst->make_array();
    for (const auto& item : in) {
        value_io* c = dst->make_node();
        (void)to_value(c, item);
        dst->push_back(c);
    }
    return error_code::ok;
}
template <typename A>
error_code fixed_sequence_from(const value_io* src, A& out) {
    if (src->type() != value_io::kind::array) return error_code::kind_mismatch;
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) return error_code::kind_mismatch;
        error_code e = from_value(child, out[i]);
        if (e != error_code::ok) return e;
    }
    return error_code::ok;
}
```

from_value/to_value 分派(在 is_map 之前插入):

```cpp
    if constexpr (is_optional<T>::value) {
        return optional_from(src, out);
    } else if constexpr (is_variant<T>::value) {
        return variant_from(src, out);
    } else if constexpr (is_tuple<T>::value) {
        return tuple_from(src, out);
    } else if constexpr (is_pair<T>::value) {
        return pair_from(src, out);
    } else if constexpr (is_fixed_sequence<T>::value) {
        return fixed_sequence_from(src, out);
    } else if constexpr (is_map<T>::value) {
        // ... 既有链
```

(to_value 对称。)

- [ ] **Step 7: 运行测试验证通过(全部新类型)**

Expected: PASS。全量回归仍绿,零警告。

- [ ] **Step 8: 将 type_support_test.cpp 加入 tests/CMakeLists.txt;记录进度**

---

### Task C1: 修复标量根序列化丢值(XML/INI)

**Files:**
- Modify: `include/rtti/xml.hpp`(serialize_into 对根标量)、`include/rtti/ini.hpp`(serialize 对根标量)
- Test: `tests/json_test.cpp`(或新 `tests/naming_config_test.cpp` 的 bug 区,遵循既有格式测试门)

**Interfaces:**
- Consumes: 既有 document/serialize。
- Produces: XML 根标量输出为 `<root>3.14</root>` 形式;INI 根标量输出标量原文。

- [ ] **Step 1: 写失败测试**

```cpp
RTTI_TEST_CASE(xml_scalar_root_serialize) {
    rtti::xml::document d;
    d.make_scalar(rtti::field_value::of(3.14));
    std::string o = rtti::xml::serialize(d);
    RTTI_CHECK(o.find("3.14") != std::string::npos);   // 当前只出声明丢值
}
RTTI_TEST_CASE(ini_scalar_root_serialize) {
    rtti::ini::document d;
    d.make_scalar(rtti::field_value::of(3.14));
    std::string o = rtti::ini::serialize(d);
    RTTI_CHECK(o.find("3.14") != std::string::npos);    // 当前空输出
}
```

(放入 `tests/ini_test.cpp`/`tests/xml_test.cpp` 或新建命名唯一测试 TU。)

- [ ] **Step 2: 运行测试验证失败**

Expected: FAIL(实测 XML 只出声明、INI 空输出)。

- [ ] **Step 3: 修复 xml serialize_into 根标量路径**

读 xml.hpp serialize_into(1850 附近),根节点为标量时(非元素)输出转义文本或 `<root>` 包装。定义:**标量根输出为 `<root><value>3.14</value></root>`?** 不——保持最小:`<root>3.14</root>`(序列化根标量为单元素文本)。实现:serialize_into 检测 root_ 非元素节点时,输出 `<root>` + 标量文本(浮点/整数/字符串转义)+ `</root>`。

具体:在 render_element 前判断 `doc.root_` 的 kind,若为标量则 `out += "<root>"; append_view(...标量文本...); out += "</root>";`。

- [ ] **Step 4: 运行测试验证通过**

Expected: PASS(`o.find("3.14")` 命中)。

- [ ] **Step 5: 修复 ini serialize 根标量路径**

ini serialize(1177 附近):root_ 非节/非对象时输出标量文本(经 format_real/字符串原样)。

- [ ] **Step 6: 运行测试验证通过;全量回归;记录进度**

---

### Task C2: 浮点序列化统一短表示(to_chars 核心)

**Files:**
- Modify: `include/rtti/json.hpp`(append_real 1138 附近)、`include/rtti/xml.hpp`、`include/rtti/ini.hpp`(各自浮点输出)
- Test: `tests/naming_config_test.cpp`(新建,含浮点精确断言)

**Interfaces:**
- Consumes: 无。
- Produces: `detail::format_real(double, std::string&)` 统一核心(三格式共用),输出最短往返(如 `3.14` 非 `3.1400000000000001`)。

- [ ] **Step 1: 写失败测试**

```cpp
RTTI_TEST_CASE(float_short_format_three_formats) {
    auto short_fmt = [](double v) {
        std::string s;
        rtti::detail::format_real(v, s);
        return s;
    };
    RTTI_CHECK(short_fmt(3.14) == "3.14");
    RTTI_CHECK(short_fmt(1.0) == "1.0");
    RTTI_CHECK(short_fmt(0.5) == "0.5");
    // round-trip: parse back equals input
    RTTI_CHECK(std::strtod(short_fmt(3.14).c_str(), nullptr) == 3.14);
}
```

- [ ] **Step 2: 运行测试验证失败**

Expected: FAIL(format_real 不存在或输出长尾)。

- [ ] **Step 3: 实现 format_real(放 include/rtti/convert.hpp 或新 detail 头)**

```cpp
namespace rtti { namespace detail {
inline void format_real(double value, std::string& out) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, res.ptr);
    // ensure a decimal point or exponent exists so it reads back as float
    if (out.find('.') == std::string::npos && out.find('e') == std::string::npos &&
        out.find('E') == std::string::npos) {
        out += ".0";
    }
}
} }
```

需 `<charconv>`。若 MSVC 对 to_chars 最短模式 OK(已探针验证 `to_chars(3.14)`='3.14'),可行。

- [ ] **Step 4: 三格式换入 format_real**

json.hpp append_real、xml.hpp 浮点序列化、ini.hpp 浮点序列化均改调 `rtti::detail::format_real`。

- [ ] **Step 5: 运行测试验证通过;全量回归(JSON 既有浮点断言保持);记录进度**

---

### Task C3: JSON 对象键哈希索引(惰性,大对象)

**Files:**
- Modify: `include/rtti/json.hpp`(node obj 查找)
- Test: `tests/json_test.cpp`(补大对象 get_key 断言)

**Interfaces:**
- Consumes: 既有 node 结构。
- Produces: `node::get_key` 大对象(O(n)→O(1))。保持插入序(set_key 仍 push_back)。

- [ ] **Step 1: 写失败测试(性能语义测试——插入序保持 + 查找正确)**

```cpp
RTTI_TEST_CASE(json_large_object_key_lookup) {
    rtti::json::document d;
    d.make_object();
    for (int i = 0; i < 200; ++i) {
        std::string k = "key" + std::to_string(i);
        value_io* v = d.make_node();
        v->make_scalar(rtti::field_value::of(static_cast<int64_t>(i)));
        d.set_key(k, v);
    }
    value_io* found = nullptr;
    RTTI_CHECK(d.get_key("key199", found));
    if (found) {
        int64_t val = 0; found->as_integer(val);
        RTTI_CHECK(val == 199);
    }
    // insertion order preserved via get_key_at
    string_view k0; value_io* v0 = nullptr;
    RTTI_CHECK(d.get_key_at(0, k0, v0));
    if (v0) { int64_t val = 0; v0->as_integer(val); RTTI_CHECK(val == 0); }
}
```

- [ ] **Step 2: 运行测试验证通过(当前线性扫描也通过,属性能回归保护)**

Expected: PASS(行为不变)。

- [ ] **Step 3: 实现惰性哈希索引**

node 增成员 `mutable std::unordered_map<string_view, node*> hash_`(惰性构建:首次 get_key 且 obj.size()>8 时填充;之后 get_key O(1))。`string_view` 键指针指向 arena dup 的 key,生命周期 = document,安全。

```cpp
bool get_key_impl(string_view key, value_io*& out) const {
    if (k != rtti::value_io::kind::object) return false;
    if (hash_indexed_) {
        auto it = hash_.find(key);
        if (it != hash_.end()) { out = it->second; return true; }
        return false;
    }
    for (const auto& e : obj) { ... }
    return false;
}
```

set_key 时若已索引则同步插入(或失效重建)。用惰性:`set_key` 后标记 hash_indexed_=false(重建),get_key 重建。对象不可变后首次 get_key 建索引。为简单可靠:**首次 get_key 若 obj.size()>8 建 unordered_map,后续 set_key 同步插入**,否则保持线性。

- [ ] **Step 4: 运行测试验证通过 + 全量回归**

Expected: PASS。

- [ ] **Step 5: 记录进度至 process.md**

---

### Task C4: SIMD 扫描扩展(JSON 引号/转义,基准不劣化即接受)

**Files:**
- Modify: `include/rtti/json.hpp`(scan_string)、`include/rtti/detail/scan.hpp`(补 json_string_mask)
- Test: `tests/scan_test.cpp`(补 mask 测试)、bench/bench_parse.cpp(前后对照)

**Interfaces:**
- Consumes: scan.hpp 现有 find_first_of。
- Produces: `json_string_mask()`(引号+反斜杠掩码)。

- [ ] **Step 1: 写失败测试(scan 补 json_string_mask)**

```cpp
RTTI_TEST_CASE(scan_json_string_mask) {
    auto m = rtti::detail::json_string_mask();
    RTTI_CHECK(m.test('"'));
    RTTI_CHECK(m.test('\\'));
    const char s[] = "abc\"def\\ghi";
    const char* hit = rtti::detail::find_first_of(s, s + 11, m);
    RTTI_CHECK(hit != nullptr && *hit == '"');
}
```

- [ ] **Step 2: 运行测试验证失败**

Expected: 编译失败(json_string_mask 不存在)。

- [ ] **Step 3: 实现 mask + 换入 scan_string**

scan.hpp 补:

```cpp
// Mask of the characters that terminate a JSON string literal: the closing
// quote and the escape backslash.
inline char_mask json_string_mask() noexcept {
    char_mask m;
    m.set('"');
    m.set('\\');
    return m;
}
```

json.hpp scan_string 内部用 `find_first_of(begin, end, json_string_mask())` 定位引号/转义,逐字节逻辑保留边界处理(转义后字符跳过)。

- [ ] **Step 4: 运行测试验证通过**

Expected: PASS。

- [ ] **Step 5: 基准对照**

运行 bench/bench_parse.cpp(独立 cl 编译),记录优化前后(或与既有 6.4/2.7 对照)。若劣化 >5% 回退该换入点(保留原循环)并记录。全量回归保持绿。

- [ ] **Step 6: 记录进度至 process.md**

---

### Task D1: naming.hpp + key_case/rename_map/skip_empty 支持

**Files:**
- Create: `include/rtti/naming.hpp`(naming 转换纯函数 + naming_policy 结构)
- Modify: `include/rtti/convert.hpp`(reflect_to/reflect_from 应用 naming)
- Modify: `include/rtti/json.hpp`(write_config 增 key_case/rename_map/skip_empty;serialize 传入 naming)
- Modify: `include/rtti/xml.hpp`、`include/rtti/ini.hpp`(write_config 同步;serialize(const T&, cfg) 经 naming)
- Test: `tests/naming_config_test.cpp`(新建)

**Interfaces:**
- Consumes: 无。
- Produces:
  - `enum class key_case { identity, snake, camel }`
  - `detail::to_snake_case(string_view) / to_camel_case(string_view)` 纯函数
  - `naming_policy{ key_case; const std::map<std::string,std::string>* rename_map; bool skip_empty; }`
  - `detail::apply_naming(string_view field, const naming_policy&) -> string_view/std::string`

- [ ] **Step 1: 写失败测试(snake 键名)**

```cpp
RTTI_TEST_CASE(naming_snake_json) {
    struct P { int fieldName = 5; };
    RTTI_DEFINE_TYPE(P, fieldName);
    rtti::json::write_config cfg;
    cfg.key_case = rtti::json::key_case::snake;
    std::string j = rtti::json::serialize(P{5}, cfg);
    RTTI_CHECK(j.find("field_name") != std::string::npos);
}
```

- [ ] **Step 2: 运行测试验证失败**

Expected: 编译错误(cfg.key_case 不存在)。

- [ ] **Step 3: 实现 naming.hpp**

```cpp
#pragma once
#include <rtti/memory.hpp>
#include <map>
#include <string>

namespace rtti {
namespace detail {
enum class key_case { identity, snake, camel };

inline std::string to_snake_case(rtti::string_view s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0 && s[i-1] >= 'a' && s[i-1] <= 'z') o += '_';
            o += static_cast<char>(c - 'A' + 'a');
        } else {
            o += c;
        }
    }
    return o;
}
inline std::string to_camel_case(rtti::string_view s) {
    std::string o;
    bool up = false;
    for (char c : s) {
        if (c == '_') { up = true; continue; }
        if (up && c >= 'a' && c <= 'z') { o += static_cast<char>(c - 'a' + 'A'); up = false; }
        else { o += c; }
    }
    return o;
}

struct naming_policy {
    key_case kc = key_case::identity;
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
};

inline std::string apply_naming(rtti::string_view field, const naming_policy& np) {
    if (np.rename_map != nullptr) {
        auto it = np.rename_map->find(std::string(field));
        if (it != np.rename_map->end()) return it->second;
    }
    switch (np.kc) {
        case key_case::snake: return to_snake_case(field);
        case key_case::camel: return to_camel_case(field);
        default: return std::string(field);
    }
}
} // namespace detail
} // namespace rtti
```

- [ ] **Step 4: 实现 convert.hpp 反射命名应用**

reflect_to(reflection.hpp 或 convert.hpp 中反射字段序列化处)在 set_key 前对字段名应用 naming。需将 naming_policy 从 cfg 传入 to_value 链——架构决策:**命名仅经反射顶层 serialize(const T&, cfg) 应用**,经一个线程局部/参数传递?为避免污染 from_value/to_value 签名,采用:**serialize(const T&, cfg) 内部先构造带 naming 的 document(经 reflect_to),再 serialize doc**。具体:在 `to_value` 顶层(convert.hpp)不受 cfg——cfg 属于格式层。

**架构简化决策**:命名/映射/跳过仅作用于格式层 `serialize(const T&, cfg)` / `parse_as<T>(src, cfg)`,实现为:**格式 serialize 先经命名策略生成 JSON 文本中键名**?不可行(反射字段遍历在核心)。

**最终方案**:在 convert.hpp 的 reflect_to/reflect_from 增加可选命名参数,经重载传递。具体:
- `error_code to_value(value_io* dst, const T& in)` 保持(核心,无 cfg)。
- 新增 `error_code reflect_to_named(value_io* dst, const T& in, const detail::naming_policy&)` 及嵌套传播(字段遍历时把 np 传下去)。
- 格式 `serialize(const T&, cfg)` 提取 cfg 的 naming_policy,调 reflect_to_named,再 serialize doc。

简化落地:命名策略影响范围 = 反射字段键名。在 reflection.hpp 的 field_access 遍历处(set_key 调用点)是核心——为避免核心签名膨胀,把 naming_policy 存于 **value_io 派生文档的配置**(每格式 document 已有 cfg?无)。**更简方案:格式层 serialize(const T&, cfg) 生成临时 document 后,遍历替换键名**?低效。

**收敛决策**:本任务实现命名经**格式层预重命名映射**——serialize(const T&, cfg) 中,若 cfg 有命名策略,先生成命名映射表(字段名→输出名),经新增的 `reflect_to` 命名重载应用。具体新增 convert.hpp:

```cpp
inline error_code to_value_named(value_io* dst, const T& in, const detail::naming_policy& np);
```

递归实现(与 to_value 同结构,但在 set_key 处用 apply_naming)。格式层 serialize(const T&, cfg) 调 to_value_named。parse_as 对称 `from_value_named`(反查:输出名→字段名,经 rename_map 反查 + snake/camel 逆变换)。

- [ ] **Step 5: 运行测试验证通过(snake)**

Expected: PASS(`field_name` 出现在 JSON)。

- [ ] **Step 6: 扩展测试:rename_map / skip_empty / camel / XML / INI**

```cpp
RTTI_TEST_CASE(naming_rename_map_and_skip) {
    struct Q { int a = 1; std::string empty = ""; std::vector<int> vec; };
    RTTI_DEFINE_TYPE(Q, a, empty, vec);
    std::map<std::string, std::string> rm;
    rm["a"] = "alpha";
    rtti::json::write_config cfg;
    cfg.rename_map = &rm;
    cfg.skip_empty = true;
    std::string j = rtti::json::serialize(Q{}, cfg);
    RTTI_CHECK(j.find("alpha") != std::string::npos);
    RTTI_CHECK(j.find("empty") == std::string::npos);   // skipped
    RTTI_CHECK(j.find("vec") == std::string::npos);     // empty array skipped
}
```

- [ ] **Step 7: 实现 rename_map/skip_empty 逻辑**

reflect_to_named 中:rename_map 命中用映射名;skip_empty 时检查字段值等价(空 string/空容器/0/null)则跳过。判断空值经 field_access 的 size/get 查询(容器 size==0、字符串空、数值 0、指针 null)。

- [ ] **Step 8: 运行测试验证通过;三格式同步(cfg 字段);加入 CMakeLists;全量回归;记录进度**

---

### Task D2: XML/INI write_config 同步命名策略 + 缩进自定义

**Files:**
- Modify: `include/rtti/xml.hpp`(write_config 增 key_case/rename_map/skip_empty)、`include/rtti/ini.hpp`(同)
- Test: `tests/naming_config_test.cpp`

**Interfaces:**
- Consumes: D1 naming.hpp。
- Produces: XML/INI 的 serialize(const T&, cfg) 支持命名策略与缩进字符。

- [ ] **Step 1: 写失败测试**

```cpp
RTTI_TEST_CASE(naming_snake_xml_ini) {
    struct P { int fieldName = 5; };
    RTTI_DEFINE_TYPE(P, fieldName);
    rtti::xml::write_config cx;
    cx.key_case = rtti::xml::key_case::snake;
    RTTI_CHECK(rtti::xml::serialize(P{5}, cx).find("field_name") != std::string::npos);
    rtti::ini::write_config ci;
    ci.key_case = rtti::ini::key_case::snake;
    RTTI_CHECK(rtti::ini::serialize(P{5}, ci).find("field_name") != std::string::npos);
}
```

- [ ] **Step 2: 运行测试验证失败**

Expected: 编译错误(key_case 未定义)。

- [ ] **Step 3: 实现 XML/INI cfg 扩展**

两个头各补:

```cpp
using detail::key_case;  // or re-export enum class key_case { identity, snake, camel }
```

write_config 增:

```cpp
detail::key_case key_case_ = detail::key_case::identity;  // 或字段名 key_case
const std::map<std::string, std::string>* rename_map = nullptr;
bool skip_empty = false;
char indent_char = ' ';
```

serialize(const T&, cfg) 内组装 naming_policy 并调 to_value_named。

- [ ] **Step 4: 运行测试验证通过 + 全量回归 + 记录进度**

---

### Task R1: 回归与验收(双构建 + 剥离矩阵 + bench)

**Files:**
- Modify: `tests/CMakeLists.txt`(确保全部新测试 TU 已加入)
- Test: 全量

**Interfaces:**
- Consumes: A-D 全部。
- Produces: 验收证据。

- [ ] **Step 1: 主构建全量回归**

`cmake --build build --config Debug`;运行 rtti_tests.exe。
Expected: 断言数 = 1245 + 新增(各测试文件断言数累加),0 failures,零 C4xxx 警告。

- [ ] **Step 2: 剥离矩阵八组合**

跑八组合(仅核心/核心+JSON/核心+XML/核心+INI/核心+JSON+XML/核心+JSON+INI/核心+XML+INI/全开),断言数按格式门递减但全 0 failures。PowerShell 传参须用 `$($c.j)`(avoid `$c.j` 字面 bug),配置后读 CMakeCache 验证 RTTI_ENABLE_* 实际值。

- [ ] **Step 3: bench 前后对照**

运行 bench/bench_parse.cpp,与基线(json 6.4 / xml 2.7 MB/s)对照:性能方向不劣化(浮点/哈希/SIMD 换入以实测为准),checksum 341910/314550 保持一致(语义零变化)。

- [ ] **Step 4: 更新 README.en.md / README.zh-CN.md**

补四节新能力小节(自定义 allocator/less、新类型、性能与修复、配置灵活化),中英互索引。

- [ ] **Step 5: 更新四记忆文件(process.md 标全部任务完成;memory.md 沉淀新经验:basic_string 排除、array tuple_size 误判、MSVC apply 坑、naming 经格式层);W6 验收报告**

---

## Self-Review

**1. Spec coverage:**
- A 节(allocator/less):Task A1(is_char_string+is_sequence)、A2(basic_string converter)、A3(适配器保留 allocator)、A4(less map/alloc 键三格式测试)✓
- B 节(新类型):Task B1(trait)、B2(转换实现+分派)✓(optional/variant/tuple/pair/array/vector<bool>——vector<bool> 归入既有 sequence 路径,在 B2 Step 7 后补一条 vector<bool> 往返断言)
- C 节(性能+bug):C1(标量根丢值)、C2(浮点统一)、C3(键哈希)、C4(SIMD 扩展)✓
- D 节(配置):D1(naming.hpp+key_case/rename_map/skip_empty)、D2(XML/INI 同步+缩进)✓
- 验收:R1 ✓

**2. Placeholder scan:** 无 TBD/TODO;每步含具体代码或明确检查点。D1 的 to_value_named 架构决策已收敛(经格式层预重命名,新增命名重载)。

**3. Type consistency:** 
- `is_char_string`(A1)被 B1 is_fixed_sequence 使用,已注明。
- `has_push_back`(B1 新增)被 is_fixed_sequence 使用,已注明需补。
- `format_real`(C2)被三格式使用,签名 `void(double, std::string&)` 一致。
- `naming_policy{kc, rename_map, skip_empty}`(D1)被 D2 三格式 cfg 复用,字段名统一(`key_case`/`rename_map`/`skip_empty`)。
- `json_string_mask()`(C4)与既有 json_structural_mask 命名一致。

**4. Review Focus:**
- RF1 自定义 allocator 字符串形态:A2 Step 1(JSON `"hello"`)+ A4 Step 1(三格式)✓
- RF2 array 非 tuple:B1 Step 1 static_assert `!is_tuple<array>` + B2 array 往返 ✓
- RF3 MSVC apply 坑:B2 tuple 用 make_index_sequence 展开 ✓
- RF4 标量根丢值/浮点长尾:C1 Step 1 + C2 Step 1 ✓
- RF5 variant fold 实例化:B2 variant_from 用 variant_alternative_t + if constexpr 隔离 ✓
