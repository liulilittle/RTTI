# RTTI Performance & Benchmark Report (2026-10-09, MSVC)

> English edition. Chinese: [`../../zh/performance-report.md`](../zh/performance-report.md).
> Raw verbatim outputs: `docs/benchmarks/2026-10-09-172835-{Debug,Release}-*.txt`,
> `2026-10-09-173125-Release-*.txt`. Concise bilingual record:
> `docs/benchmarks/2026-10-09-msvc.md`.

## 1. Executive summary

Two microbenchmark groups were run once in a controlled environment:

- **rtti_bench_new** (`bench/bench_new_apis.cpp`): the industrialisation
  traversal APIs (enum traversal, field traversal, template-argument traversal,
  spec B1–B3).
- **rtti_bench** (`bench/bench_reflect.cpp`): existing core reflection
  benchmarks (field walk, get/set by name, recursive reflect roundtrip).

Key findings (Release `/O2`+`NDEBUG`+LTO only):

- The new traversal APIs run in the **1–10 ns/op** range; `enum_count` /
  `template_arg_count` — constexpr table reads — sit at ~**1 ns/op** (the
  timing loop's floor).
- Value field traversal `for_each_field(obj, ...)`: Release **10.12 ns/op**;
  Debug **555.47 ns/op** (includes per-iteration object+`std::string`
  construction, see §5).
- Existing rtti_bench: `field_walk` Release ~**954M ops/s**, `get_set_by_name`
  ~**121M ops/s**, recursive roundtrip ~**1.49M ops/s**.

Every number below is transcribed verbatim from `docs/benchmarks/2026-10-09-*.txt`;
no estimates, interpolation or extrapolation. Numbers are machine/toolchain/
configuration dependent — **do not quote without this environment**.

## 2. Methodology (spec D1)

| Item | Value |
| --- | --- |
| Date | 2026-10-09 |
| Host | Windows 11 (build 26200), AMD Ryzen 9 7945HX (16 physical / 32 logical) |
| Toolchain | Microsoft Visual Studio 2022 Enterprise, MSVC `cl` **19.44.35229** (x64) |
| Generator | VS17 2022 x64, CMake multi-config (Debug / Release) |
| Flags | `/W4 /permissive- /Zc:preprocessor /std:c++17`; Debug = `/Od` + `_DEBUG`; Release = `/O2` + `NDEBUG` (+ `/GL /LTCG`) |
| Build | Release uses LTO (IPO; root CMakeLists `RTTI_ENABLE_LTO`) |
| Timing | `std::chrono::steady_clock` wall time (`bench/bench_timer.hpp`) |
| Sampling | rtti_bench_new: median of **7 timed samples** after warm-up, ns/op; **20,000,000 iters**/sample, warm-up **312,500** |
| rtti_bench | single wall-clock pass (1e6–5e8 totals per line) |

Environment header printed by each binary:

```
benchmark   : rtti_bench_new
compiler    : MSVC 19.44.35229
config      : Debug (_DEBUG defined) / Release (NDEBUG defined)
cpu         : AMD Ryzen 9 7945HX with Radeon Graphics
logical cpus: 32
method      : steady_clock wall time; 7 timed samples, median ns/op
timed iters : 20000000 per sample (warm-up 312500)
```

## 3. Results — rtti_bench_new (new traversal APIs)

### 3.1 Debug (`/Od`, `_DEBUG`)

| Benchmark | ns/op (median) |
| --- | ---: |
| `enum_count<Color>` | 2.37 |
| `enum_name_at<Color>` | 3.82 |
| `enum_value_at<Color>` | 2.64 |
| `for_each_enum<Color>` | 21.68 |
| `enum_count<Empty>` (empty table) | 3.16 |
| `for_each_enum<Empty>` (empty table) | 12.91 |
| `for_each_field<Derived> names` | 28.75 |
| `for_each_field(obj, values)` | 555.47 |
| `template_arg_count<pair<int,string>>` | 3.24 |
| `for_each_template_arg<pair<int,string>>` | 31.73 |
| `template_arg_count<int>` (unregistered) | 3.48 |

### 3.2 Release (`/O2`, `NDEBUG`, LTO)

| Benchmark | ns/op (median) |
| --- | ---: |
| `enum_count<Color>` | 1.02 |
| `enum_name_at<Color>` | 1.03 |
| `enum_value_at<Color>` | 1.49 |
| `for_each_enum<Color>` | 2.04 |
| `enum_count<Empty>` (empty table) | 1.02 |
| `for_each_enum<Empty>` (empty table) | 1.02 |
| `for_each_field<Derived> names` | 1.82 |
| `for_each_field(obj, values)` | 10.12 |
| `template_arg_count<pair<int,string>>` | 1.03 |
| `for_each_template_arg<pair<int,string>>` | 4.90 |
| `template_arg_count<int>` (unregistered) | 1.30 |

Source: `docs/benchmarks/2026-10-09-172835-Debug-rtti_bench_new.txt` (Debug),
`2026-10-09-173125-Release-rtti_bench_new.txt` (Release).

## 4. Results — rtti_bench (existing core)

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

Source: `docs/benchmarks/2026-10-09-172835-Debug-rtti_bench.txt`,
`2026-10-09-173125-Release-rtti_bench.txt`.

Release-over-Debug speedups: `field_walk` ~6.1×, `get_set_by_name` ~5.6×,
`reflect_roundtrip` ~27.1× (roundtrip benefits most from inlining/LTO).

## 5. Interpretation & limitations (spec D3)

- **~1 ns/op is the measurement floor**: `enum_count` / `template_arg_count`
  are compile-time table reads; 1.02 ns/op in Release reflects the loop-bound
  measurement floor, not a meaningful per-op cost.
- **`for_each_field(obj, values)` Debug cost includes construction**: each
  iteration constructs a fresh `BenchDerived` (with a `std::string`), so Debug
  555.47 ns/op includes per-iteration object construction; Release 10.12 ns/op
  reflects the tuned traversal path.
- **Empty / unregistered paths**: `enum_count<Empty>` (empty table),
  `for_each_enum<Empty>`, `template_arg_count<int>` (unregistered) exercise the
  safe "no iteration" path — cost is call+loop overhead, no out-of-range access.
- **Run-to-run variance**: an independent re-test once observed
  `for_each_field(obj, values)` at ~377 ns/op Debug (vs 555.47; median resists
  scheduler noise better than a single run). **A single machine/day number is
  not a universal claim**; re-run on the target machine before quoting.
- **Reproduction**: `scripts/run_bench.ps1 -Config Debug|Release` (optional
  `-Iterations N`) reproduces this capture; the script writes environment-headed
  raw output into `docs/benchmarks/<stamp>-<Config>-<target>.txt` (see
  `bench/README.md`).
- **Coverage note**: this record is Windows/MSVC only. GCC/Clang and Linux
  build/zero-warning gates are verified by GitHub Actions (ubuntu runner), not
  by local measurement.

## 6. Artefacts (2026-10-09)

| File | Content |
| --- | --- |
| `docs/benchmarks/2026-10-09-172835-Debug-rtti_bench_new.txt` | rtti_bench_new Debug raw output (11 rows, with header) |
| `docs/benchmarks/2026-10-09-173125-Release-rtti_bench_new.txt` | rtti_bench_new Release raw output |
| `docs/benchmarks/2026-10-09-172835-Debug-rtti_bench.txt` | rtti_bench Debug raw output (3 rows ops/s) |
| `docs/benchmarks/2026-10-09-173125-Release-rtti_bench.txt` | rtti_bench Release raw output |
| `docs/benchmarks/2026-10-09-msvc.md` | concise bilingual record of the same run |
| `bench/bench_reflect.cpp` / `bench/bench_new_apis.cpp` / `bench/bench_timer.hpp` | benchmark sources |
| `scripts/run_bench.ps1` | one-command reproduction script |