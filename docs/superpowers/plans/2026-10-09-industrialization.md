# 工业化闭环实施计划(反射完整化 + CI/CD + Benchmark + 工程基础设施)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在本库闭环前补齐反射遍历 API、GitHub CI/CD、严谨 benchmark 与工程基础设施,产出事实可复现,GCC/Linux 与 MSVC/Windows 零警告全绿,中英双语文档互引。

**Architecture:** 反射遍历 API 直接构建在既有 `enum_table`/`fields<T>()`/`template_info` 之上(零新增存储);CI 用 GitHub Actions 多平台矩阵;benchmark 扩充既有 `bench/`;基础设施加版本头、LICENSE、CMake install/export。

**Tech Stack:** C++17;MSVC 2022 `/W4 /permissive- /Zc:preprocessor`;GCC/Clang `-Wall -Wextra -Wpedantic -Werror`;CMake ≥3.15;GitHub Actions。

**Spec:** `docs/superpowers/specs/2026-10-09-industrialization-design.md`

## Global Constraints

- C++17;无第三方运行时依赖。
- 注释全英文;块注释引用 spec 章节。
- MSVC 与 GCC/Clang 均零警告(MSVC `/W4 /permissive- /Zc:preprocessor`;GCC/Clang `-Wall -Wextra -Wpedantic -Werror`)。
- 改码前备份到 `.opencode/backups/`;禁 `checkout`/`reset`/`restore`/`merge`;`git add` 逐个文件,禁 `git add .`。
- 本地仅 MSVC(无 g++/clang++)→ GCC/Clang 由 CI ubuntu runner 验证,本地只跑 MSVC 三档。
- 主基线:rtti_tests Debug 55823/0;proto_ext 160/0;flat_schema 408/0;build-off 32849/0。
- cmake 完整路径:`C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`;生成器 `Visual Studio 17 2022 -A x64`。
- TDD:先红后绿;C10 只跑相关 target;全量门仅收尾一次。
- 版本单一来源:CMake `project(RTTI VERSION x.y.z)` → 生成 `include/rtti/version.hpp`。
- 文档双语互引:README.md(英文默认)/README.en.md(英文)/README.zh-CN.md(中文),三者互相链接。

## Review Focus

- **未注册枚举的遍历**:`enum_count<E>()` 对未 `RTTI_DEFINE_ENUM` 的 E 必须返回 0 且 `for_each_enum` 不迭代(不得硬编译错误或 UB)。
- **`enum_value_at` 越界**:索引 ≥ count 必须返回 `E{}` 而非越界读。
- **`for_each_field` 对未注册类型**:硬编译错误(与 `field_count` 一致),但运行期 `type_desc` 路径对 `as_reflected==nullptr` 必须安全跳过。
- **继承字段顺序**:`for_each_field` 必须基类字段在前(与 `fields<T>()` 一致)。
- **`enum_desc` 扩展的 ABI/兼容**:新增 `values`/`value_size` 成员不得破坏既有仅读 `names` 的调用点;`type_of` 构造 `enum_desc` 处必须同步初始化新成员。
- **CI 事实性**:workflow 必须真实可跑(路径、命令与仓库一致),不得引用不存在的脚本;ubuntu runner 无 `cl`,不得假设 MSVC 专属路径。
- **benchmark 不伪造数字**:所有报告数字必须来自实际运行并附环境;README 不得写入未实测的吞吐。

---

## Task B1: 枚举遍历 API

**Files:**
- Modify: `include/rtti/reflection.hpp`(enum_desc 扩展、enum 遍历函数)
- Test: `tests/type_judge_test.cpp`(或新建 `tests/enum_reflect_test.cpp` 并注册)

**Interfaces:**
- Consumes: `enum_table<E>{count, array}`, `enum_label<E>{name,value}`(reflection.hpp:1248-1257),`enum_desc`(reflection.hpp:1340)。
- Produces:
  - `template <typename E> constexpr std::size_t enum_count() noexcept`
  - `template <typename E> const char* enum_name_at(std::size_t) noexcept`
  - `template <typename E> E enum_value_at(std::size_t) noexcept`
  - `template <typename E, typename Fn> void for_each_enum(Fn&&)`
  - `enum_desc` 增加 `const void* values; std::size_t value_size;`

- [ ] **Step 1: 写失败测试**(新建 `tests/enum_reflect_test.cpp`)

```cpp
// Enum-traversal tests (industrialization spec B1).
#include "test_harness.hpp"
#include <rtti/reflection.hpp>

enum class Color { red = 1, green = 2, blue = 4 };
enum class Empty {};
RTTI_DEFINE_ENUM(Color, red, green, blue)
RTTI_DEFINE_ENUM(Empty)

RTTI_TEST_CASE(EnumTraversalBasics) {
    RTTI_CHECK_EQ(rtti::enum_count<Color>(), std::size_t(3));
    RTTI_CHECK_EQ(std::string(rtti::enum_name_at<Color>(0)), std::string("red"));
    RTTI_CHECK_EQ(rtti::enum_name_at<Color>(1), std::string("green"));
    RTTI_CHECK(rtti::enum_value_at<Color>(2) == Color::blue);
    // Out of range -> safe defaults.
    RTTI_CHECK(rtti::enum_name_at<Color>(3) == nullptr);
    RTTI_CHECK(rtti::enum_value_at<Color>(3) == Color{});
    // Unregistered enum -> count 0, no iteration.
    RTTI_CHECK_EQ(rtti::enum_count<Empty>(), std::size_t(0));
    std::size_t n = 0;
    rtti::for_each_enum<Empty>([&](const char*, Empty) { ++n; });
    RTTI_CHECK_EQ(n, std::size_t(0));
}

RTTI_TEST_CASE(EnumForEachOrder) {
    std::string acc;
    rtti::for_each_enum<Color>([&](const char* name, Color v) {
        acc += name;
        acc += ':';
        acc += std::to_string(static_cast<int>(v));
        acc += ';';
    });
    RTTI_CHECK_EQ(acc, std::string("red:1;green:2;blue:4;"));
}

RTTI_TEST_CASE(EnumDescValues) {
    const rtti::type_desc* td = rtti::type_of<Color>();
    RTTI_CHECK(td != nullptr && td->as_enum != nullptr);
    RTTI_CHECK_EQ(td->as_enum->count, std::size_t(3));
    RTTI_CHECK(td->as_enum->values != nullptr);
    RTTI_CHECK_EQ(td->as_enum->value_size, sizeof(Color));
}
```

- [ ] **Step 2: 运行确认失败**

Run: `& "<cmake>" --build build --config Debug --target rtti_tests`
Expected: 编译失败(`enum_count` 等未定义)。

- [ ] **Step 3: 扩展 enum_desc 并实现遍历**

在 `reflection.hpp:1340` 的 `enum_desc` 增加成员:
```cpp
struct enum_desc {
    std::size_t count;
    const char* const* names;
    const void* values = nullptr;      // parallel array of E values (raw bytes)
    std::size_t value_size = 0;        // sizeof(E)
};
```
在 `enum_names()`(reflection.hpp:1441)附近新增:
```cpp
// Raw value array for the runtime enum_desc (parallel to enum_names).
template <typename E, std::size_t... I>
const void* enum_value_array(std::index_sequence<I...>) {
    static const E values[] = { enum_table<E>::array[I].value... };
    return values;
}
template <typename E>
const void* enum_values() {
    if constexpr (enum_table<E>::count == 0) {
        return nullptr;
    } else {
        return enum_value_array<E>(std::make_index_sequence<enum_table<E>::count>());
    }
}
```
新增公开遍历 API(放在 `enum_from_string` 之后、reflection.hpp:1278 之前):
```cpp
template <typename E>
constexpr std::size_t enum_count() noexcept { return enum_table<E>::count; }

template <typename E>
const char* enum_name_at(std::size_t i) noexcept {
    return i < enum_table<E>::count ? enum_table<E>::array[i].name : nullptr;
}

template <typename E>
E enum_value_at(std::size_t i) noexcept {
    return i < enum_table<E>::count ? enum_table<E>::array[i].value : E{};
}

template <typename E, typename Fn>
void for_each_enum(Fn&& fn) {
    for (std::size_t i = 0; i < enum_table<E>::count; ++i) {
        fn(enum_table<E>::array[i].name, enum_table<E>::array[i].value);
    }
}
```
同步 `type_of_impl` 的 enum 分支(reflection.hpp:1624):
```cpp
static const enum_desc e{enum_table<T>::count, enum_names<T>(),
                         enum_values<T>(), sizeof(T)};
```

- [ ] **Step 4: 运行确认通过**

Run: `& "<cmake>" --build build --config Debug --target rtti_tests` 然后 `.\build\tests\Debug\rtti_tests.exe`
Expected: 0 warning 0 error;新增断言全绿,`0 failures`。

- [ ] **Step 5: 注册测试并 Commit**

在 `tests/CMakeLists.txt` 的 `RTTI_TEST_SOURCES` 追加 `enum_reflect_test.cpp`。
```bash
git add include/rtti/reflection.hpp tests/enum_reflect_test.cpp tests/CMakeLists.txt
git commit -m "feat(reflect): enum traversal API (count/name_at/value_at/for_each_enum)"
```

---

## Task B2: 字段遍历 for_each_field

**Files:**
- Modify: `include/rtti/reflection.hpp`
- Test: `tests/enum_reflect_test.cpp` 或新建 `tests/field_iteration_test.cpp`

**Interfaces:**
- Consumes: `fields<T>()`、`field_count<T>()`、`field_access_base`(name/type/get_value)。
- Produces:
  - `template <typename T, typename Fn> void for_each_field(Fn&&)` — `fn(const char* name, type_id type)`
  - `template <typename T, typename Fn> void for_each_field(T& obj, Fn&&)` — `fn(const char* name, type_id, const field_value&)`

- [ ] **Step 1: 写失败测试**

```cpp
RTTI_TEST_CASE(ForEachFieldNamesAndOrder) {
    std::string acc;
    rtti::for_each_field<JudgeDerived>([&](const char* name, rtti::type_id) {
        acc += name; acc += ';';
    });
    // Base field first (merged order), then own.
    RTTI_CHECK_EQ(acc, std::string("x;y;"));
}

RTTI_TEST_CASE(ForEachFieldValues) {
    JudgeDerived d; d.x = 7; d.y = 9;
    int sum = 0;
    rtti::for_each_field(d, [&](const char*, rtti::type_id, const rtti::field_value& v) {
        if (v.is_integer()) sum += static_cast<int>(v.as_integer());
    });
    RTTI_CHECK_EQ(sum, 16);
}

RTTI_TEST_CASE(ForEachFieldEmptyType) {
    // A reflected type with zero fields must iterate nothing without crashing.
    std::size_t n = 0;
    rtti::for_each_field<JudgeOther>([&](const char*, rtti::type_id) { ++n; });
    RTTI_CHECK_EQ(n, rtti::field_count<JudgeOther>());
}
```

- [ ] **Step 2: 运行确认失败** (for_each_field 未定义)。

- [ ] **Step 3: 实现**

在 `fields<T>()`(reflection.hpp:823)之后新增:
```cpp
// Visit each reflected field of T in table order (merged base fields first).
// fn(const char* name, type_id type). No allocation.
template <typename T, typename Fn>
void for_each_field(Fn&& fn) {
    const field_access_base* const* arr = fields<T>();
    const std::size_t n = field_count<T>();
    for (std::size_t i = 0; i < n; ++i) {
        fn(arr[i]->name, arr[i]->type);
    }
}

// Visit each field of a concrete object with its current value.
// fn(const char* name, type_id type, const field_value& value).
template <typename T, typename Fn>
void for_each_field(T& obj, Fn&& fn) {
    const field_access_base* const* arr = fields<T>();
    const std::size_t n = field_count<T>();
    for (std::size_t i = 0; i < n; ++i) {
        fn(arr[i]->name, arr[i]->type, arr[i]->get(&obj));
    }
}
```

- [ ] **Step 4: 运行确认通过**。
- [ ] **Step 5: Commit**
```bash
git add include/rtti/reflection.hpp tests/field_iteration_test.cpp tests/CMakeLists.txt
git commit -m "feat(reflect): for_each_field traversal (names/types and values)"
```

---

## Task B3: 模板实参遍历增强

**Files:**
- Modify: `include/rtti/reflection.hpp`
- Test: `tests/template_reflect_test.cpp`(追加)

**Interfaces:**
- Consumes: `template_info<T>`、`template_arg_count<T>()`、`template_arg_desc_of<T>(i)`。
- Produces: `template <typename T, typename Fn> void for_each_template_arg(Fn&&)` — `fn(std::size_t i, const type_desc*)`。

- [ ] **Step 1: 写失败测试**

```cpp
RTTI_TEST_CASE(ForEachTemplateArg) {
    using P = std::pair<int, std::string>;
    std::size_t n = 0;
    rtti::for_each_template_arg<P>([&](std::size_t i, const rtti::type_desc* td) {
        RTTI_CHECK(td != nullptr);
        RTTI_CHECK_EQ(i, n);
        ++n;
    });
    RTTI_CHECK_EQ(n, rtti::template_arg_count<P>());
}
RTTI_TEST_CASE(ForEachTemplateArgNonTemplate) {
    std::size_t n = 0;
    rtti::for_each_template_arg<int>([&](std::size_t, const rtti::type_desc*) { ++n; });
    RTTI_CHECK_EQ(n, std::size_t(0));
}
```

- [ ] **Step 2: 运行确认失败**。
- [ ] **Step 3: 实现**(在 `template_arg_desc_of` reflection.hpp:1725 之后):
```cpp
// Visit each registered template argument in declaration order.
// fn(std::size_t index, const type_desc* arg_type).
template <typename T, typename Fn>
void for_each_template_arg(Fn&& fn) {
    const std::size_t n = template_info<T>::count;
    for (std::size_t i = 0; i < n; ++i) {
        fn(i, template_arg_desc_of<T>(i));
    }
}
```
- [ ] **Step 4: 运行确认通过**。
- [ ] **Step 5: Commit**
```bash
git add include/rtti/reflection.hpp tests/template_reflect_test.cpp
git commit -m "feat(reflect): for_each_template_arg traversal"
```

---

## Task E1: 版本单一来源 + version.hpp

**Files:**
- Create: `include/rtti/version.hpp`
- Create: `cmake/version.hpp.in`
- Modify: `CMakeLists.txt`

**Interfaces:** Produces `RTTI_VERSION_MAJOR/MINOR/PATCH`, `RTTI_VERSION_STRING`, `rtti::version()`.

- [ ] **Step 1: 写模板** `cmake/version.hpp.in`:
```cpp
#pragma once
#define RTTI_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define RTTI_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define RTTI_VERSION_PATCH @PROJECT_VERSION_PATCH@
#define RTTI_VERSION_STRING "@PROJECT_VERSION@"
namespace rtti { inline const char* version() noexcept { return RTTI_VERSION_STRING; } }
```
- [ ] **Step 2: CMake 配置生成**(在 `project(...)` 后):
```cmake
configure_file(cmake/version.hpp.in ${CMAKE_CURRENT_SOURCE_DIR}/include/rtti/version.hpp @ONLY)
```
- [ ] **Step 3: 测试**:`tests/type_support_test.cpp` 追加
```cpp
#include <rtti/version.hpp>
RTTI_TEST_CASE(VersionMacros) {
    RTTI_CHECK(std::string(rtti::version()) == std::string(RTTI_VERSION_STRING));
}
```
- [ ] **Step 4: 配置+构建+运行**;确认 0 警告 0 失败。
- [ ] **Step 5: Commit**
```bash
git add cmake/version.hpp.in include/rtti/version.hpp CMakeLists.txt tests/type_support_test.cpp
git commit -m "build: single-source version header"
```

---

## Task E2: LICENSE (MIT)

- [ ] **Step 1:** 创建 `LICENSE`,MIT 全文,版权行 `Copyright (c) 2026 <版权人>`(占位待用户填)。
- [ ] **Step 2:** README.md/README.en.md/README.zh-CN.md 各加 "License / 许可证" 章节,链接 `LICENSE`,中英互引。
- [ ] **Step 3: Commit**
```bash
git add LICENSE README.md README.en.md README.zh-CN.md
git commit -m "docs: add MIT LICENSE and license sections"
```

---

## Task E3: CMake install / find_package

**Files:** Modify `CMakeLists.txt`;Create `cmake/rtti-config.cmake.in`。

- [ ] **Step 1: 安装规则**
```cmake
include(GNUInstallDirs)
install(TARGETS rtti EXPORT rttiTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/rtti DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT rttiTargets NAMESPACE rtti:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rtti)
```
- [ ] **Step 2: 配置包文件** `cmake/rtti-config.cmake.in` + `configure_package_config_file`。
- [ ] **Step 3: 验证**:本地 `cmake --install build --prefix %TEMP%\rtti-inst` 后,写一个最小下游 CMake 项目 `find_package(rtti CONFIG REQUIRED)` + `target_link_libraries(app rtti::rtti)` 编译通过。
- [ ] **Step 4: Commit**
```bash
git add CMakeLists.txt cmake/rtti-config.cmake.in
git commit -m "build: install rules and find_package support"
```

---

## Task C: GitHub Actions CI/CD

**Files:** Create `.github/workflows/ci.yml`。

- [ ] **Step 1: 写 workflow**(矩阵:windows MSVC Debug/Release;ubuntu GCC Debug/Release -Werror;ubuntu Clang Debug/Release -Werror;每 job 构建 + ctest/运行四套件断言 0 failures;ubuntu 加 ASan+UBSan job)。
```yaml
name: CI
on: [push, pull_request]
jobs:
  windows-msvc:
    runs-on: windows-latest
    strategy: { matrix: { cfg: [Debug, Release] } }
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S . -B build -G "Visual Studio 17 2022" -A x64
      - run: cmake --build build --config ${{ matrix.cfg }}
      - run: ctest --test-dir build -C ${{ matrix.cfg }} --output-on-failure
  linux-gcc:
    runs-on: ubuntu-latest
    strategy: { matrix: { cfg: [Debug, Release] } }
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S . -B build -DCMAKE_BUILD_TYPE=${{ matrix.cfg }} -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Werror"
      - run: cmake --build build -j
      - run: ctest --test-dir build --output-on-failure
  linux-clang:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Werror"
      - run: cmake --build build -j
      - run: ctest --test-dir build --output-on-failure
  linux-asan:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
      - run: cmake --build build -j
      - run: ctest --test-dir build --output-on-failure
```
- [ ] **Step 2:** README 三文件顶部加 CI 徽章。
- [ ] **Step 3:** 本地无法跑 CI(GitHub 托管);用 `act` 或直接 push 后看结果(标记「未本地验证」)。语法用 `actionlint`(若有)或人工核对。
- [ ] **Step 4: Commit**
```bash
git add .github/workflows/ci.yml README.md README.en.md README.zh-CN.md
git commit -m "ci: multi-platform GitHub Actions (MSVC/GCC/Clang, sanitizers)"
```

---

## Task D: 严谨 Benchmark

**Files:** Modify `bench/bench_reflect.cpp`;Create `bench/bench_new_apis.cpp`;Create `bench/README.md`;Create `scripts/run_bench.ps1`。

- [ ] **Step 1:** 扩充 `bench_timer.hpp`/bench 输出环境头(编译器、优化档、CPU、迭代数、预热)。
- [ ] **Step 2:** 为 B1/B2/B3 新 API 加微基准(`bench_new_apis.cpp`),CMake 注册 `rtti_bench_new`。
- [ ] **Step 3:** `bench/README.md`(双语)写明运行方法、环境要求、如何解读。
- [ ] **Step 4:** 实跑并记录结果到 `docs/benchmarks/2026-10-09-msvc.md`,含真实数字与环境(禁止编造)。
- [ ] **Step 5: Commit**
```bash
git add bench/ scripts/run_bench.ps1 docs/benchmarks/2026-10-09-msvc.md
git commit -m "bench: environment-tagged benchmarks for new reflection APIs"
```

---

## Task E4: 文档双语互引 + 新 API 章节

- [ ] **Step 1:** README 三文件加「反射遍历」章节(枚举/字段/模板),中英对照,互相链接。
- [ ] **Step 2:** 抽查所有 API 名与 `include/rtti/` 实际一致(禁编造)。
- [ ] **Step 3: Commit**
```bash
git add README.md README.en.md README.zh-CN.md
git commit -m "docs: bilingual traversal API sections"
```

---

## Self-Review

- **Spec coverage**: B1/B2/B3→B 各 Task;B4 已裁为 B1-B3;E1 版本→Task E1;E2 许可→E2;E3 打包→E3;E4 文档→E4;C→Task C;D→Task D。全覆盖。
- **Placeholder scan**: 代码步骤均含具体代码;`<cmake>`/`<版权人>` 为需填的实际值(执行者按 Global Constraints 路径与用户版权信息替换)。
- **Type consistency**: `enum_count`/`enum_name_at`/`enum_value_at`/`for_each_enum`/`for_each_field`/`for_each_template_arg` 命名在各任务一致;`enum_desc` 新成员 `values`/`value_size` 在 B1 定义、B1 初始化点同步。
- **Review Focus**: 未注册枚举(B1 Empty 用例)、越界(B1)、未注册类型 for_each_field(编译期)、继承顺序(B2)、enum_desc 兼容(B1 EnumDescValues)、CI 真实性(Task C)、benchmark 不伪造(Task D)。
