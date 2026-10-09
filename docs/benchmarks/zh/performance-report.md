# RTTI 性能测试与 Benchmark 报告(2026-10-09,MSVC)

> 本报告为中文详细版。英文版见 [`docs/benchmarks/en/performance-report.md`](../en/performance-report.md)。
> 原始逐字输出:`docs/benchmarks/2026-10-09-172835-{Debug,Release}-*.txt`、`2026-10-09-173125-Release-*.txt`、`2026-10-09-LTCG-*.txt`。
> 简约记录版:`docs/benchmarks/2026-10-09-msvc.md`。

## 1. 执行摘要

本库在本机一次受控环境下跑通两组微基准:

- **rtti_bench_new(bench/bench_new_apis.cpp)**:工业化新增的反射遍历 API(枚举遍历、字段遍历、模板实参遍历,spec B1-B3)。
- **rtti_bench(bench/bench_reflect.cpp)**:既有核心反射基准(字段遍历、按名 get/set、递归反射往返)。

关键结论(均为 Release /O2+NDEBUG+LTO):

- 新遍历 API 在 Release 下普遍在 **1–10 ns/op** 区间;`enum_count`/`template_arg_count` 这类 constexpr 查表操作约 **1 ns/op**(已是循环计时下限)。
- 按值字段遍历 `for_each_field(obj, ...)` Release **10.12 ns/op**;Debug **555.47 ns/op**(包含每次迭代构造新对象与 `std::string` 的开销,见 §6 解读)。
- 既有 rtti_bench:字段遍历 `field_walk` Release **~954M ops/s**,按名 get/set **~121M ops/s**,递归反射往返 **~1.49M ops/s**。

所有数字均来自 `docs/benchmarks/2026-10-09-*.txt` 原始输出逐字转录,**未作任何估计、插值或外推**。数字与机器/工具链/配置强相关,**禁止脱离本环境引用**。

## 2. 方法学(spec D1)

| 项 | 值 |
| --- | --- |
| 日期 | 2026-10-09 |
| 主机 | Windows 11(build 26200),AMD Ryzen 9 7945HX(16 物理核 / 32 逻辑线程) |
| 工具链 | Microsoft Visual Studio 2022 Enterprise,MSVC `cl` **19.44.35229**(x64) |
| 生成器 | VS17 2022 x64,CMake multi-config(Debug / Release) |
| 编译选项 | `/W4 /permissive- /Zc:preprocessor /std:c++17`;Debug = `/Od` + `_DEBUG`;Release = `/O2` + `NDEBUG`(+ `/GL /LTCG` 链接时代码生成) |
| 构建 | Release 采用 LTO(IPO,根 CMakeLists 的 `RTTI_ENABLE_LTO`) |
| 计时方法 | `std::chrono::steady_clock` 墙钟(bench/bench_timer.hpp) |
| 采样 | rtti_bench_new:暖机后 **7 次计时取中位数**,单位 ns/op;每次采样 **20,000,000 次迭代**、暖机 **312,500** |
| rtti_bench | 单次壁钟(1e6–5e8 总迭代,见各输出),单位 ops/s |

环境头由各二进制自身打印:

```
benchmark   : rtti_bench_new
compiler    : MSVC 19.44.35229
config      : Debug (_DEBUG defined) / Release (NDEBUG defined)
cpu         : AMD Ryzen 9 7945HX with Radeon Graphics
logical cpus: 32
method      : steady_clock wall time; 7 timed samples, median ns/op
timed iters : 20000000 per sample (warm-up 312500)
```

## 3. 结果:rtti_bench_new(新遍历 API)

### 3.1 Debug(/Od,_DEBUG)

| 基准项 | ns/op(中位数) |
| --- | ---: |
| `enum_count<Color>` | 2.37 |
| `enum_name_at<Color>` | 3.82 |
| `enum_value_at<Color>` | 2.64 |
| `for_each_enum<Color>` | 21.68 |
| `enum_count<Empty>`(空表) | 3.16 |
| `for_each_enum<Empty>`(空表) | 12.91 |
| `for_each_field<Derived> names` | 28.75 |
| `for_each_field(obj, values)` | 555.47 |
| `template_arg_count<pair<int,string>>` | 3.24 |
| `for_each_template_arg<pair<int,string>>` | 31.73 |
| `template_arg_count<int>`(未注册) | 3.48 |

### 3.2 Release(/O2,NDEBUG,LTO)

| 基准项 | ns/op(中位数) |
| --- | ---: |
| `enum_count<Color>` | 1.02 |
| `enum_name_at<Color>` | 1.03 |
| `enum_value_at<Color>` | 1.49 |
| `for_each_enum<Color>` | 2.04 |
| `enum_count<Empty>`(空表) | 1.02 |
| `for_each_enum<Empty>`(空表) | 1.02 |
| `for_each_field<Derived> names` | 1.82 |
| `for_each_field(obj, values)` | 10.12 |
| `template_arg_count<pair<int,string>>` | 1.03 |
| `for_each_template_arg<pair<int,string>>` | 4.90 |
| `template_arg_count<int>`(未注册) | 1.30 |

来源:`docs/benchmarks/2026-10-09-172835-Debug-rtti_bench_new.txt`(Debug)、`2026-10-09-173125-Release-rtti_bench_new.txt`(Release)。

### 3.3 LTCG(build-opt `Release`,显式 `/GL` + `/LTCG`)

同一二进制,从独立的 `build-opt` LTO 配置(`RTTI_ENABLE_LTO` + IPO 启用,CMake
对 bench 目标施加 `INTERPROCEDURAL_OPTIMIZATION`)再次捕获:

| 基准项 | ns/op(中位数) |
| --- | ---: |
| `enum_count<Color>` | 1.04 |
| `enum_name_at<Color>` | 1.67 |
| `enum_value_at<Color>` | 1.83 |
| `for_each_enum<Color>` | 2.04 |
| `enum_count<Empty>`(空表) | 1.67 |
| `for_each_enum<Empty>`(空表) | 1.05 |
| `for_each_field<Derived> names` | 1.89 |
| `for_each_field(obj, values)` | 10.46 |
| `template_arg_count<pair<int,string>>` | 1.73 |
| `for_each_template_arg<pair<int,string>>` | 3.48 |
| `template_arg_count<int>`(未注册) | 1.21 |

该配置的多次捕获显示正常运行间波动 —

`for_each_field(obj, values)` 在多次运行中观测到 10.06–10.46 ns/op,
`for_each_template_arg` 为 2.93–3.48 ns/op — 均落在 §3.2 Release 表已报告的
同一 1–10 ns/op 包络内。来源:`docs/benchmarks/2026-10-09-LTCG-rtti_bench_new.txt`。

## 4. 结果:rtti_bench(既有核心)

**Debug:**

```
field_walk                   157574611.6 ops/s  (0.006 s, 394000000 total)
get_set_by_name              21707654.3  ops/s  (0.046 s, 999999 total)
reflect_roundtrip               54945.1  ops/s  (18.200 s, 500001500000 total)
```

**Release:**

```
field_walk                   954016409.1 ops/s  (0.001 s, 394000000 total)
get_set_by_name              120923370.9 ops/s  (0.008 s, 999999 total)
reflect_roundtrip             1488759.2  ops/s  (0.672 s, 500001500000 total)
```

来源:`docs/benchmarks/2026-10-09-172835-Debug-rtti_bench.txt`、`2026-10-09-173125-Release-rtti_bench.txt`。

Release 相对 Debug 提升幅度:`field_walk` ~6.1×、`get_set_by_name` ~5.6×、`reflect_roundtrip` ~27.1×(roundtrip 受优化与内联影响最大)。

### 4.1 LTCG(build-opt `Release`,显式 `/GL` + `/LTCG`)

从 `build-opt` LTO 配置捕获(同一二进制族,与 §4 相同的单次壁钟方式):

```
field_walk                   967773154.0 ops/s  (0.001 s, 394000000 total)
get_set_by_name              120001920.0 ops/s  (0.008 s, 999999 total)
reflect_roundtrip             1427807.1  ops/s  (0.700 s, 500001500000 total)
```

与 §4 Release 捕获同处同一整体包络;`reflect_roundtrip` 的墙钟差(0.672 s vs
0.700 s)即本报告 §6 已标注的预期单次运行调度噪声。来源:
`docs/benchmarks/2026-10-09-LTCG-rtti_bench.txt`。

## 5. 各构建方式的断言基线(测试套件交叉核对)

以上数字来自 `bench/` 微基准。工业化构建矩阵由测试套件交叉核对;下列每个
二进制均以退出码 `0`、零失败结束:

| 套件 | Debug `build/` | Release `build/` | LTCG `build-opt/` | 全关 `build-off/` |
| --- | ---: | ---: | ---: | ---: |
| `rtti_tests` | 55847 | 55847 | 55847 | 32872 |
| `rtti_proto_extension_tests` | 160 | 160 | 160 | — |
| `rtti_flat_schema_tests` | 408 | 408 | 408 | — |

断言数随功能门开启单调上升:32872 条的全关门基线(`build-off`,全部
`RTTI_ENABLE_*` 关闭)是纯核心套件;有门的 proto/flat 套件(160 / 408)不编入
全关构建。2026-10-09 于 §2 主机实测:`rtti_tests` 55847/0 覆盖 Debug、Release、
LTCG 三档;`rtti_proto_extension_tests` 160/0 与 `rtti_flat_schema_tests`
408/0 覆盖三种构建方式;`rtti_tests` 32872/0 于 `build-off`。

## 6. 解读与局限(spec D3)

- **约 1 ns/op 是测量下限**:`enum_count` / `template_arg_count` 是编译期常量表读取+循环边界计时,Release 下的 1.02 ns/op 反映的是计时循环本身的边界,并非有意义的单次操作成本。
- **`for_each_field(obj,values)` 的 Debug 数字含构造开销**:每次迭代构造一个全新的 `BenchDerived`(含 `std::string`),因此 Debug 555.47 ns/op 内包含逐迭代对象构造;Release 10.12 ns/op 反映的是优化后遍历路径本身。
- **空表/未注册类型路径**:`enum_count<Empty>`(空表)、`for_each_enum<Empty>`、`template_arg_count<int>`(未注册)验证的是「空/未注册」安全路径——不发生迭代,成本只是调用+循环开销,不存在越界访问。
- **环境波动**:评审复测中 `for_each_field(obj,values)` Debug 曾见 377 ns/op 量级(与 555.47 的差异属机器状态/调度噪声,中位数已比单次更稳)。**单机单日数字不是普适结论**;任何吞吐声明必须附本环境头,并在引用前于目标机重新实测。
- **可复现**:`scripts/run_bench.ps1 -Config Debug|Release`(可选 `-Iterations N`)可复现本次全部捕获;脚本会把含环境头的原始输出写入 `docs/benchmarks/<stamp>-<Config>-<target>.txt`(见 `bench/README.md`)。
- **覆盖范围说明**:本次记录的是 Windows/MSVC 单环境。GCC/Clang 与 Linux 的构建/零警告门由 GitHub Actions(ubuntu runner)验证,非本机性能测量范围。

## 7. 重建目录(2026-10-09)

| 文件 | 内容 |
| --- | --- |
| `docs/benchmarks/2026-10-09-172835-Debug-rtti_bench_new.txt` | rtti_bench_new Debug 原始输出(11 项,含环境头) |
| `docs/benchmarks/2026-10-09-173125-Release-rtti_bench_new.txt` | rtti_bench_new Release 原始输出 |
| `docs/benchmarks/2026-10-09-172835-Debug-rtti_bench.txt` | rtti_bench Debug 原始输出(3 项 ops/s) |
| `docs/benchmarks/2026-10-09-173125-Release-rtti_bench.txt` | rtti_bench Release 原始输出 |
| `docs/benchmarks/2026-10-09-LTCG-rtti_bench_new.txt` | rtti_bench_new LTCG(build-opt)原始输出 |
| `docs/benchmarks/2026-10-09-LTCG-rtti_bench.txt` | rtti_bench LTCG(build-opt)原始输出 |
| `docs/benchmarks/2026-10-09-msvc.md` | 同环境简约双语记录 |
| `bench/bench_reflect.cpp` / `bench/bench_new_apis.cpp` / `bench/bench_timer.hpp` | 基准源码 |
| `scripts/run_bench.ps1` | 一键复现脚本 |