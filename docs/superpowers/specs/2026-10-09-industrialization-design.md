# 工业化闭环设计:反射完整化 + CI/CD + Benchmark + 工程基础设施

- 日期:2026-10-09
- 状态:草稿(待用户评审)
- 基线:HEAD=bc0bda5;主 rtti_tests 55819/0(Debug);proto_ext 160/0;flat_schema 408/0;build-off 存在。C++17;MSVC `/W4 /permissive- /Zc:preprocessor`。
- 前置:A(cast/as 收尾,Task5)由既有 plan `2026-10-08-cast-as-variance.md` 负责,本文档不含 A。

## 背景与目标

库即将闭环。用户要求在本库交付前补齐工业化能力,产出必须**事实性、严谨、可复现**:

1. **反射完整化**:枚举遍历(全部值/名/元数据)、字段遍历 `for_each_field`、模板类型反射增强,以及配套「全新操作」。
2. **完整 GitHub 自动化 CI/CD 流水线**。
3. **超级详细严谨、严重尊重事实的 benchmark**。
4. **测试/构建/文档/版本管理等工业化基础设施**。

硬性纪律:
- GCC 编译零警告;Linux 与 Windows 均可构建/测试通过。
- 中英双语文档互相索引。
- 不编造接口/数字;benchmark 结果必须附环境与方法。

### 事实基线(探勘所得,非假设)

- 本机**仅 MSVC `cl`(14.44.35207)**,无 `g++`/`clang++` → GCC/Linux 只能由 CI(ubuntu runner)验证。
- 无 `.github/`;无 `LICENSE`;无独立版本文件(仅 CMake `project(RTTI VERSION 0.1.0)`)。
- `scripts/` 仅 `gen_fixtures.ps1`;`bench/` 有 `bench_parse.cpp`、`bench_reflect.cpp`、`bench_timer.hpp`(基础 micro-bench,输出 ops/s,无环境记录、无回归追踪)。
- CMake 无 `install`/`export`/`CPack`;`.gitignore` 存在且已忽略 `build/`、`build-off/`、`.superpowers/`。
- 枚举:`enum_table<E>`(count + `enum_label<E>{name,value}[]`)、`enum_to_string`/`enum_from_string`;`enum_desc` 目前**仅暴露 names**,值数组未经 type_desc 暴露。
- 字段:`fields<T>()`、`field_count<T>()`、`field_begin/end<T>()`、`field_access_base`(name + 类型/访问虚接口);无 `for_each_field`。
- 模板:`template_info<T>`、`is_template_instance_v`、`template_arg_count<T>()`、`template_args<T>`、`template_args_of<T>()`、`template_arg_desc_of<T>(i)`。

## 工作流 B:反射完整化

### B1. 枚举遍历

新增只读遍历 API(不破坏既有 `enum_to_string`/`enum_from_string`):

```cpp
namespace rtti {
template <typename E>
struct enum_entry { const char* name; E value; };

// Number of registered enumerators; 0 if E unregistered.
template <typename E> constexpr std::size_t enum_count() noexcept;

// By index (0..count-1): name/value; out-of-range -> nullptr / value-initialized E.
template <typename E> const char* enum_name_at(std::size_t i) noexcept;
template <typename E> E enum_value_at(std::size_t i) noexcept;

// Iterate every registered enumerator in declaration order.
template <typename E, typename Fn> void for_each_enum(Fn&& fn);

// Metadata via the runtime descriptor (no template needed): iterate names+values.
const enum_desc* /*as_enum*/ ...;  // 扩展 enum_desc 增加 values 数组
}
```

- 扩展 `enum_desc` 增加 `const void* values;` 与 `std::size_t value_size;`(或 `const long long* values`),使运行期(经 `type_desc::as_enum`)可同时取名字与值。**不破坏**现有仅用 names 的调用点。
- `RTTI_DEFINE_ENUM` 宏需同步发射 values 数组(现值已在 `enum_table<E>::array[i].value`,仅需再暴露一个 parallel 数组)。
- 覆盖:空枚举(未注册)、单值、含重复值别名、负数、`enum class`、底层类型为 8/16/32/64 位。

### B2. 字段遍历 `for_each_field`

```cpp
namespace rtti {
// Visit each reflected field (own + merged base fields, declaration order).
// fn(name, const type_desc* type) -> void or bool(stop). No allocation.
template <typename T, typename Fn> void for_each_field(Fn&& fn);
// Runtime variant on a value: name + type + read access.
template <typename T, typename Fn> void for_each_field(T& obj, Fn&& fn);
}
```

- 基于既有 `fields<T>()`/`field_count<T>()`/`field_access_base`,零新增存储。
- 覆盖:无字段类型、继承合并字段顺序(基类在前)、虚基合并、回调提前停止语义。

### B3. 模板类型反射增强

在既有 `template_info` 基础上增强:

- 递归解析模板实参:`template_arg_desc_of<T>(i)` 已存在;新增便捷的 `for_each_template_arg<T>(fn)`。
- 嵌套模板实例的实参 type_desc 正确解析(如 `std::vector<std::pair<int,std::string>>` 注册后逐层可取)。
- 与 `type_of` 集成:`is_template_instance_v<T>` 与 `template_arg_count<T>()` 的一致性断言。
- 覆盖:0 参模板、非模板类型(安全空表)、越界索引(编译期 `template_arg_at` 静态断言 + 运行期 `template_arg_desc_of` 返回 nullptr)。

### B4. 「全新操作」(待用户补充具体定义后细化)

用户原话「这里还要全新的操作」。当前理解:即上述枚举遍历/字段遍历/模板遍历的**新公开操作**。若用户另有所指,须在评审时补充;未明确前本项按 B1-B3 交付。

## 工作流 C:GitHub CI/CD

### C1. 构建与测试矩阵

`.github/workflows/ci.yml`:

- `windows-latest` + MSVC:Debug、Release、LTCG(build-opt);`/W4 /permissive- /Zc:preprocessor`,**警告即错误门**(`/WX` 或对诊断 grep 断言 0)。
- `ubuntu-latest` + GCC:`-Wall -Wextra -Wpedantic -Werror`;Debug + Release。
- `ubuntu-latest` + Clang:同 GCC 标志。
- 每 job:配置 → 构建 → 运行四套件(rtti_tests / proto_extension / flat_schema / build-off)→ 断言 `0 failures`。

### C2. 剥离矩阵

- 运行 6 门(JSON/XML/INI/TOML/PROTO/FLAT)组合的代表性子集(全关 = build-off;全开 = 默认);断言单调性与零警告。
- 若 64 组合耗时过长,CI 跑全关 + 全开 + 若干关键组合(如仅 JSON、仅 PROTO),完整矩阵留本地/可选 job。

### C3. 质量门

- Sanitizer job(Linux):ASan+UBSan 跑核心套件(`-fsanitize=address,undefined`)。
- 静态检查:至少零警告门;可选 clang-tidy(若时间允许,非阻塞)。

### C4. 产物与发布

- 打 tag 时(或手动触发)构建 Release 并上传 CMake 安装包/头文件 bundle 为 artifact。
- README 顶部加 CI 状态徽章(中英双份)。

## 工作流 D:严谨 Benchmark

### D1. 方法论(事实性)

- 每个 benchmark 结果必须记录:编译器+版本、优化档、OS、CPU、构建类型、迭代数、测量方式(壁钟/CPU 时间)、预热策略。
- 固定种子/输入规模;报告多轮取中位数与分散度,禁止单次数字当结论。
- 明确区分 micro-bench(单操作)与 macro-bench(端到端解析/序列化)。

### D2. 覆盖范围

- 反射核心:字段遍历、按名 get/set、to_value/from_value 往返。
- 格式适配器:JSON/XML/INI/TOML 的 parse 与 serialize 吞吐(与既有 `bench_parse.cpp` 对齐)。
- 新增 API(枚举/字段遍历)的微基准。
- 与基线对照(如 SIMD scan 的既有 +46%/+24% 数字)保持可复现。

### D3. 可复现与报告

- `bench/README.md`(双语)记录如何运行、环境、预期量级。
- 结果以 Markdown 表输出(脚本或程序生成),入库到 `docs/benchmarks/YYYY-MM-DD-<env>.md`。
- 禁止把某台机器的一次结果当成普遍结论;标注硬件与不确定性。

## 工作流 E:工程基础设施

### E1. 版本管理

- 单一版本来源(CMake `project(VERSION)`),生成 `include/rtti/version.hpp`(版本宏 + 字符串),CMake 与文档引用同源。
- 语义化版本策略说明。

### E2. 许可与元数据

- 新增 `LICENSE`(需用户确认许可证类型,默认建议 MIT)。
- README 加许可章节(中英)。

### E3. 打包与安装

- CMake `install(TARGETS ...)` + `install(DIRECTORY include/)`。
- 导出 `rtti-config.cmake`(或 `find_package(rtti)`)供下游 `target_link_libraries`。
- 可选 CPack 生成包。

### E4. 文档

- 中英双语 README 互相索引(已部分存在,需补齐新 API 章节)。
- 新增 API(枚举/字段/模板遍历、cast/as)的中英对照文档。
- 可选 Doxygen 配置(非阻塞)。

### E5. 测试基础设施

- 已有 42 个测试 TU + 自定义 harness;补充新 API 的测试,保持零警告。
- 统一测试运行说明;CI 与本地一致。

## 分解与顺序

独立子系统,各自 spec→plan→实现周期。建议顺序(依赖优先):

1. **B 反射完整化**(新 API + 测试)。
2. **E1/E3 版本与打包**(CI 依赖版本与 install 才能验证)。
3. **C CI/CD**(依赖 B 的测试与 E 的 install)。
4. **D Benchmark**(依赖 B 的新 API 与 C 的环境一致性)。
5. **E4/E2 文档与许可**(可与其他并行,收尾)。

各子项均遵守:TDD 红→绿;`/W4 /permissive- /Zc:preprocessor` 与 GCC `-Werror` 零警告;改码前备份;禁回退类 git 操作;全量门仅收尾一次。

## 验收(W6)

- B:枚举/字段/模板遍历 API 有测试且全绿;MSVC 与 CI 的 GCC/Clang 零警告。
- C:CI 在 windows+ubuntu(GCC/Clang)全绿;剥离矩阵代表组合通过;sanitizer 无告警。
- D:benchmark 可复现、含环境与方法、结果入库;数字有对照。
- E:版本单一来源、LICENSE、install/find_package 可用、双语互引文档齐全。
- 全部:三档(Debug/Release/LTCG)四套件零失败零警告。

## 未决问题(需用户裁决)

1. **许可证类型**(默认 MIT?)。
2. **「全新操作」的准确含义**(若不同于 B1-B3)。
3. CI 是否需要 clang-cl 或指定 GCC/Clang 版本下限(当前文档称 GCC 11+/Clang 14+)。
4. 是否需要 Doxygen/在线文档站(还是 README 双语足够)。
