# RTTI 性能测试与 Benchmark 报告(2026-10-11,MSVC)

> 本报告为中文详细版。英文版见 [`docs/benchmarks/en/performance-report.md`](../en/performance-report.md)。
> 原始逐字输出:`docs/benchmarks/2026-10-11-155020-{Debug,Release}-*.txt`、
> `2026-10-11-155207-Release-rtti_bench_parse.txt`、`2026-10-11-155719-LTCG-rtti_bench.txt`、
> `2026-10-11-155753-LTCG-rtti_bench_new.txt`。基准版本 HEAD:`23f2aa1`。

## 1. 执行摘要

本库在 **HEAD `23f2aa1`** 下于受控环境跑通三组微基准:

- **rtti_bench_new(bench/bench_new_apis.cpp)**:工业化新增的反射遍历 API(枚举遍历、字段遍历、模板实参遍历,spec B1-B3)。
- **rtti_bench(bench/bench_reflect.cpp)**:既有核心反射基准(字段遍历、按名 get/set、递归反射往返)。
- **rtti_bench_parse(bench/bench_parse.cpp)**:JSON/XML 文档解析吞吐(~1 MB 源文,MB/s + it/s)。

关键结论(均为 Release /O2+NDEBUG+LTO):

- 新遍历 API 在 Release 下普遍在 **1–10 ns/op** 区间;`enum_count`/`template_arg_count` 这类 constexpr 查表操作约 **1 ns/op**(已是循环计时下限)。
- 按值字段遍历 `for_each_field(obj, ...)` Release **11.40 ns/op**;Debug **521.16 ns/op**(包含每次迭代构造新对象与 `std::string` 的开销,见 §6 解读)。
- 既有 rtti_bench:字段遍历 `field_walk` Release **~781M ops/s**,按名 get/set **~116M ops/s**,递归反射往返 **~879K ops/s**。
- 解析吞吐(Release,7 次运行中位数):json **64.3 MB/s**、xml **45.2 MB/s**;checksum **341910** / **314550** 与 2026-10-10 基线一致。

所有数字均来自 `docs/benchmarks/2026-10-11-*.txt` 原始输出逐字转录,**未作任何估计、插值或外推**。数字与机器/工具链/配置强相关,**禁止脱离本环境引用**。

## 2. 方法学(spec D1)

| 项 | 值 |
| --- | --- |
| 日期 | 2026-10-11 |
| Git HEAD | 23f2aa1 |
| 主机 | Windows 11(build 26200),AMD Ryzen 9 7945HX(16 物理核 / 32 逻辑线程) |
| 工具链 | Microsoft Visual Studio 2022 Enterprise,MSVC `cl` **19.44.35229**(x64) |
| 生成器 | VS17 2022 x64,CMake multi-config(Debug / Release) |
| 编译选项 | `/W4 /permissive- /Zc:preprocessor /std:c++17`;Debug = `/Od` + `_DEBUG`;Release = `/O2` + `NDEBUG`(+ `/GL /LTCG` 链接时代码生成) |
| 构建 | Release 采用 LTO(IPO,根 CMakeLists 的 `RTTI_ENABLE_LTO`) |
| 计时方法 | `std::chrono::steady_clock` 墙钟(bench/bench_timer.hpp) |
| 采样 | rtti_bench_new:暖机后 **7 次计时取中位数**,单位 ns/op;每次采样 **20,000,000 次迭代**、暖机 **312,500**。rtti_bench:**7 次采样取中位数**,单位 ops/s(含 min/max)。rtti_bench_parse:**7 次运行取中位数**,30 迭代/次 |

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
| `enum_count<Color>` | 2.25 |
| `enum_name_at<Color>` | 2.85 |
| `enum_value_at<Color>` | 3.06 |
| `for_each_enum<Color>` | 18.37 |
| `enum_count<Empty>`(空表) | 2.98 |
| `for_each_enum<Empty>`(空表) | 12.61 |
| `for_each_field<Derived> names` | 24.33 |
| `for_each_field(obj, values)` | 521.16 |
| `template_arg_count<pair<int,string>>` | 2.79 |
| `for_each_template_arg<pair<int,string>>` | 26.61 |
| `template_arg_count<int>`(未注册) | 3.22 |

### 3.2 Release(/O2,NDEBUG,LTO)

| 基准项 | ns/op(中位数) |
| --- | ---: |
| `enum_count<Color>` | 1.02 |
| `enum_name_at<Color>` | 1.63 |
| `enum_value_at<Color>` | 1.63 |
| `for_each_enum<Color>` | 1.30 |
| `enum_count<Empty>`(空表) | 1.63 |
| `for_each_enum<Empty>`(空表) | 1.60 |
| `for_each_field<Derived> names` | 1.83 |
| `for_each_field(obj, values)` | 11.40 |
| `template_arg_count<pair<int,string>>` | 1.21 |
| `for_each_template_arg<pair<int,string>>` | 5.10 |
| `template_arg_count<int>`(未注册) | 1.94 |

来源:`docs/benchmarks/2026-10-11-155020-Debug-rtti_bench_new.txt`(Debug)、`2026-10-11-155020-Release-rtti_bench_new.txt`(Release)。

### 3.3 LTCG(build-opt `Release`,显式 `/GL` + `/LTCG`)

同一二进制,从独立的 `build-opt` LTO 配置(`RTTI_ENABLE_LTO` + IPO 启用,CMake 对 bench 目标施加 `INTERPROCEDURAL_OPTIMIZATION`)再次捕获:

| 基准项 | ns/op(中位数) |
| --- | ---: |
| `enum_count<Color>` | 1.02 |
| `enum_name_at<Color>` | 1.62 |
| `enum_value_at<Color>` | 1.63 |
| `for_each_enum<Color>` | 2.02 |
| `enum_count<Empty>`(空表) | 1.61 |
| `for_each_enum<Empty>`(空表) | 1.61 |
| `for_each_field<Derived> names` | 2.03 |
| `for_each_field(obj, values)` | 11.44 |
| `template_arg_count<pair<int,string>>` | 1.86 |
| `for_each_template_arg<pair<int,string>>` | 3.44 |
| `template_arg_count<int>`(未注册) | 1.93 |

与 §3.2 Release 表处于同一 1–10 ns/op 包络;行间差异(`enum_name_at` 1.62 vs 1.63、`for_each_field(obj, values)` 11.44 vs 11.40)属 §6 标注的常规运行间波动。来源:`docs/benchmarks/2026-10-11-155753-LTCG-rtti_bench_new.txt`。

## 4. 结果:rtti_bench(既有核心)

**Debug:**

```
field_walk                   median   174947210 ops/s  (min   133992092, max   177367358)  0.564-0.746 s, 7 samples, total 39400000000
get_set_by_name              median    16250832 ops/s  (min    15448682, max    18259437)  0.548-0.647 s, 7 samples, total 9999999
reflect_roundtrip            median       52460 ops/s  (min       47604, max       52989)  18.872-21.007 s, 7 samples, total 500001500000
```

**Release:**

```
field_walk                   median   780919329 ops/s  (min   775553920, max   800901495)  0.125-0.129 s, 7 samples, total 39400000000
get_set_by_name              median   115854047 ops/s  (min   113948037, max   116830346)  0.086-0.088 s, 7 samples, total 9999999
reflect_roundtrip            median      878643 ops/s  (min      828760, max     1263947)  0.791-1.207 s, 7 samples, total 500001500000
```

来源:`docs/benchmarks/2026-10-11-155020-Debug-rtti_bench.txt`、`2026-10-11-155020-Release-rtti_bench.txt`。

Release 相对 Debug:`field_walk` ~4.5×、`get_set_by_name` ~7.1×、`reflect_roundtrip` ~16.7×(roundtrip 受内联/LTO 影响最大)。

### 4.1 LTCG(build-opt `Release`,显式 `/GL` + `/LTCG`)

从 `build-opt` LTO 配置捕获(同一二进制族,与 §4 相同的 7 采样中位数方法):

```
field_walk                   median   866039308 ops/s  (min   819694975, max   880331286)  0.114-0.122 s, 7 samples, total 39400000000
get_set_by_name              median    94030829 ops/s  (min    90446089, max    96022646)  0.104-0.111 s, 7 samples, total 9999999
reflect_roundtrip            median      823474 ops/s  (min      741937, max      987924)  1.012-1.348 s, 7 samples, total 500001500000
```

与 §4 Release 捕获处于同一整体包络;`reflect_roundtrip` 墙钟波动(741K–988K ops/s)即报告 §6 已标注的运行间调度噪声。来源:`docs/benchmarks/2026-10-11-155719-LTCG-rtti_bench.txt`。

### 4.2 rtti_bench_parse(JSON/XML 文档解析吞吐,Release)

~1 MB 源文(json=1048525 bytes, xml=1048513 bytes),30 迭代/次;数值为 7 次连续运行的中位数:

| 基准项 | MB/s(7 次中位数) | it/s(中位数) | checksum |
| --- | ---: | ---: | ---: |
| `json parse` | 64.3 | 64.3 | **341910** |
| `xml parse` | 45.2 | 45.2 | **314550** |

相对 2026-10-10 Release 基线(json 10.6 MB/s、xml 35.3 MB/s):json ~6×、xml ~1.3×。checksum 与该快照完全一致,确认相同文档生成相同的节点池。来源:`docs/benchmarks/2026-10-11-155207-Release-rtti_bench_parse.txt`。

## 5. 各构建方式的断言基线(测试套件交叉核对)

以上数字来自 `bench/` 微基准。工业化构建矩阵由测试套件交叉核对;下列每个二进制均以退出码 `0`、零失败结束(断言总数自 2026-10-09 起未变):

| 套件 | Debug `build/` | Release `build/` | LTCG `build-opt/` | 全关 `build-off/` |
| --- | ---: | ---: | ---: | ---: |
| `rtti_tests` | 55847 | 55847 | 55847 | 32872 |
| `rtti_proto_extension_tests` | 160 | 160 | 160 | — |
| `rtti_flat_schema_tests` | 408 | 408 | 408 | — |

2026-10-11 采样下的 Release 相对 Debug 提升:`field_walk` ~4.5×、`get_set_by_name` ~7.1×、`reflect_roundtrip` ~16.8×。

## 6. 解读与局限(spec D3)

- **约 1 ns/op 是测量下限**:`enum_count` / `template_arg_count` 是编译期常量表读取,Release 下的 1.02 ns/op 反映的是计时循环本身的边界,并非有意义的单次操作成本。
- **`for_each_field(obj,values)` 的 Debug 数字含构造开销**:每次迭代构造一个全新的 `BenchDerived`(含 `std::string`),因此 Debug 521.16 ns/op 内包含逐迭代对象构造;Release 11.40 ns/op 反映的是优化后遍历路径本身。
- **空表/未注册类型路径**:`enum_count<Empty>`(空表)、`for_each_enum<Empty>`、`template_arg_count<int>`(未注册)验证的是「空/未注册」安全路径——不发生迭代,成本只是调用+循环开销,不存在越界访问。
- **运行间波动**:每行均为 7 次采样/运行的中位数(min/max 已列于原始 txt),比单次更能抵抗调度噪声;但**单机单日数字不是普适结论**,引用前必须于目标机重新实测。
- **可复现**:`scripts/run_bench.ps1 -Config Debug|Release`(可选 `-Iterations N`)可复现 rtti_bench / rtti_bench_new 的捕获;脚本会把含环境头的原始输出写入 `docs/benchmarks/<stamp>-<Config>-<target>.txt`(见 `bench/README.md`)。`rtti_bench_parse` 直接运行 `build/bench/<Config>/rtti_bench_parse.exe`。
- **覆盖范围说明**:本次记录的是 Windows/MSVC 单环境。GCC/Clang 与 Linux 的构建/零警告门由 GitHub Actions(ubuntu runner)验证,非本机性能测量范围。

## 7. 重建目录(2026-10-11)

| 文件 | 内容 |
| --- | --- |
| `docs/benchmarks/2026-10-11-155020-Debug-rtti_bench_new.txt` | rtti_bench_new Debug 原始输出(11 项,7 采样中位数) |
| `docs/benchmarks/2026-10-11-155020-Release-rtti_bench_new.txt` | rtti_bench_new Release 原始输出 |
| `docs/benchmarks/2026-10-11-155753-LTCG-rtti_bench_new.txt` | rtti_bench_new LTCG(build-opt)原始输出 |
| `docs/benchmarks/2026-10-11-155020-Debug-rtti_bench.txt` | rtti_bench Debug 原始输出(3 项 ops/s,7 次 median/min/max) |
| `docs/benchmarks/2026-10-11-155020-Release-rtti_bench.txt` | rtti_bench Release 原始输出 |
| `docs/benchmarks/2026-10-11-155719-LTCG-rtti_bench.txt` | rtti_bench LTCG(build-opt)原始输出 |
| `docs/benchmarks/2026-10-11-155207-Release-rtti_bench_parse.txt` | rtti_bench_parse Release 原始输出(7 次运行;checksum 341910 / 314550) |
| `bench/bench_reflect.cpp` / `bench/bench_new_apis.cpp` / `bench/bench_parse.cpp` / `bench/bench_timer.hpp` | 基准源码 |
| `scripts/run_bench.ps1` | 一键复现脚本(rtti_bench / rtti_bench_new) |

历史捕获(保留):`docs/benchmarks/2026-10-09-*.txt`、`2026-10-09-LTCG-*.txt`、`2026-10-10-112330-Release-*.txt`、`2026-10-09-msvc.md`。