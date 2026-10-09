# Microbenchmarks (微基准)

This directory contains self-contained microbenchmarks for the RTTI library.
本目录包含 RTTI 库的独立微基准程序。

| File / 文件 | Target / 目标 | Measures / 测量内容 |
| --- | --- | --- |
| `bench_reflect.cpp` | `rtti_bench` | Field walk / get-set by name / recursive reflect roundtrip |
| `bench_parse.cpp` | – (compiled via bench target sources as needed) | JSON / XML document parse throughput |
| `bench_new_apis.cpp` | `rtti_bench_new` | New traversal APIs: enums, `for_each_field`, `for_each_template_arg` |

## Running / 运行方法

Requires a configured CMake build tree (VS2022 x64 on Windows; the root
`build/` directory on this machine). Build then run:

需要已配置的 CMake 构建树(本机为 VS2022 x64,根目录 `build/`)。构建后运行:

```powershell
cmake --build build --config Release --target rtti_bench_new rtti_bench
.\build\bench\Release\rtti_bench_new.exe
.\build\bench\Release\rtti_bench.exe
```

For Debug builds use `--config Debug` and `.\build\bench\Debug\*.exe`.
Debug 请改用 `--config Debug` 与 `.\build\bench\Debug\*.exe` 路径。

`rtti_bench_new` accepts an optional iteration-count argument
(`rtti_bench_new 100000000`), which overrides the default per-sample count.
`rtti_bench_new` 可接收可选迭代次数参数(如上例),用于覆盖默认每次采样迭代数。

Optionally use `scripts/run_bench.ps1` to run both configs and capture output
into `docs/benchmarks/`:
也可使用 `scripts/run_bench.ps1` 一次运行两种配置并将输出捕获到 `docs/benchmarks/`:

```powershell
.\scripts\run_bench.ps1 -Config Debug
.\scripts\run_bench.ps1 -Config Release
```

## Environment requirements / 环境要求

- C++17 compiler; MSVC 2022 `/W4 /permissive- /Zc:preprocessor` (and
  GCC/Clang `-Wall -Wextra -Wpedantic` in CI) are zero-warning clean.
- No third-party runtime dependencies. / 无第三方运行时依赖。
- Timer is `std::chrono::steady_clock` wall time (see `bench_timer.hpp`).
  计时使用 `std::chrono::steady_clock` 墙钟(见 `bench_timer.hpp`)。

## How to interpret / 如何解读

- Every run prints an **environment header**: compiler + version, build
  configuration (Debug/Release), CPU model, logical CPU count, and the timed
  iteration count. Read every number against that header.
  每次运行都会打印**环境头**:编译器与版本、构建配置(Debug/Release)、CPU 型号、
  逻辑 CPU 数量与计时迭代数。请结合该环境头解读所有数字。
- Results are the **median of several timed samples** after a warm-up pass,
  expressed in ns/op. Medians resist scheduler spikes better than a single run.
  结果为预热后多次计时的**中位数**,以 ns/op 表示;中位数比单次运行更能抵抗
  调度噪声。
- **Numbers are machine, toolchain and configuration dependent.** A number
  measured here is a *data point*, not a universal claim. Any report must state
  its own environment; see `docs/benchmarks/2026-10-09-msvc.md` for a recorded
  run, and re-run locally before quoting throughput.
  **数字受机器、工具链与配置影响。**此处测得的数字只是*数据点*而非普适结论。
  任何报告必须注明自身环境;已记录的一次运行见 `docs/benchmarks/2026-10-09-msvc.md`,
  引用吞吐前请在本机重新实测。