# RTTI Performance & Benchmark Report (2026-10-11, MSVC)

> English edition. Chinese: [`../../zh/performance-report.md`](../zh/performance-report.md).
> Raw verbatim outputs: `docs/benchmarks/2026-10-11-155020-{Debug,Release}-*.txt`,
> `2026-10-11-155207-Release-rtti_bench_parse.txt`, `2026-10-11-155719-LTCG-rtti_bench.txt`,
> `2026-10-11-155753-LTCG-rtti_bench_new.txt`. Benchmark HEAD: `23f2aa1`.

## 1. Executive summary

Three microbenchmark groups were run at HEAD `23f2aa1` in a controlled
environment:

- **rtti_bench_new** (`bench/bench_new_apis.cpp`): the industrialisation
  traversal APIs (enum traversal, field traversal, template-argument traversal,
  spec B1–B3).
- **rtti_bench** (`bench/bench_reflect.cpp`): existing core reflection
  benchmarks (field walk, get/set by name, recursive reflect roundtrip).
- **rtti_bench_parse** (`bench/bench_parse.cpp`): JSON / XML document parse
  throughput on a ~1 MB source.

Key findings (Release `/O2`+`NDEBUG`+LTO only):

- The new traversal APIs run in the **1–10 ns/op** range; `enum_count` /
  `template_arg_count` — constexpr table reads — sit at ~**1 ns/op** (the
  timing loop's floor).
- Value field traversal `for_each_field(obj, ...)`: Release **11.40 ns/op**;
  Debug **521.16 ns/op** (includes per-iteration object+`std::string`
  construction, see §6).
- Existing rtti_bench: `field_walk` Release ~**781M ops/s**, `get_set_by_name`
  ~**116M ops/s**, recursive roundtrip ~**879K ops/s**.
- Parse throughput (Release): json **64.3 MB/s**, xml **45.2 MB/s** (median of
  7 runs); checksums **341910** / **314550** unchanged vs the 2026-10-10
  baseline.

Every number below is transcribed verbatim from `docs/benchmarks/2026-10-11-*.txt`;
no estimates, interpolation or extrapolation. Numbers are machine/toolchain/
configuration dependent — **do not quote without this environment**.

## 2. Methodology (spec D1)

| Item | Value |
| --- | --- |
| Date | 2026-10-11 |
| Git HEAD | 23f2aa1 |
| Host | Windows 11 (build 26200), AMD Ryzen 9 7945HX (16 physical / 32 logical) |
| Toolchain | Microsoft Visual Studio 2022 Enterprise, MSVC `cl` **19.44.35229** (x64) |
| Generator | VS17 2022 x64, CMake multi-config (Debug / Release) |
| Flags | `/W4 /permissive- /Zc:preprocessor /std:c++17`; Debug = `/Od` + `_DEBUG`; Release = `/O2` + `NDEBUG` (+ `/GL /LTCG`) |
| Build | Release uses LTO (IPO; root CMakeLists `RTTI_ENABLE_LTO`) |
| Timing | `std::chrono::steady_clock` wall time (`bench/bench_timer.hpp`) |
| Sampling | rtti_bench_new: median of **7 timed samples** after warm-up, ns/op; **20,000,000 iters**/sample, warm-up **312,500**. rtti_bench: median of **7 samples**, ops/s (min/max reported). rtti_bench_parse: **median of 7 runs**, 30 iters/run |

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
| `enum_count<Color>` | 2.25 |
| `enum_name_at<Color>` | 2.85 |
| `enum_value_at<Color>` | 3.06 |
| `for_each_enum<Color>` | 18.37 |
| `enum_count<Empty>` (empty table) | 2.98 |
| `for_each_enum<Empty>` (empty table) | 12.61 |
| `for_each_field<Derived> names` | 24.33 |
| `for_each_field(obj, values)` | 521.16 |
| `template_arg_count<pair<int,string>>` | 2.79 |
| `for_each_template_arg<pair<int,string>>` | 26.61 |
| `template_arg_count<int>` (unregistered) | 3.22 |

### 3.2 Release (`/O2`, `NDEBUG`, LTO)

| Benchmark | ns/op (median) |
| --- | ---: |
| `enum_count<Color>` | 1.02 |
| `enum_name_at<Color>` | 1.63 |
| `enum_value_at<Color>` | 1.63 |
| `for_each_enum<Color>` | 1.30 |
| `enum_count<Empty>` (empty table) | 1.63 |
| `for_each_enum<Empty>` (empty table) | 1.60 |
| `for_each_field<Derived> names` | 1.83 |
| `for_each_field(obj, values)` | 11.40 |
| `template_arg_count<pair<int,string>>` | 1.21 |
| `for_each_template_arg<pair<int,string>>` | 5.10 |
| `template_arg_count<int>` (unregistered) | 1.94 |

Source: `docs/benchmarks/2026-10-11-155020-Debug-rtti_bench_new.txt` (Debug),
`2026-10-11-155020-Release-rtti_bench_new.txt` (Release).

### 3.3 LTCG (build-opt `Release`, explicit `/GL` + `/LTCG`)

Same binary, captured again from the separate `build-opt` LTO configuration
(`RTTI_ENABLE_LTO` + IPO enabled, CMake `INTERPROCEDURAL_OPTIMIZATION` applied
to the bench targets):

| Benchmark | ns/op (median) |
| --- | ---: |
| `enum_count<Color>` | 1.02 |
| `enum_name_at<Color>` | 1.62 |
| `enum_value_at<Color>` | 1.63 |
| `for_each_enum<Color>` | 2.02 |
| `enum_count<Empty>` (empty table) | 1.61 |
| `for_each_enum<Empty>` (empty table) | 1.61 |
| `for_each_field<Derived> names` | 2.03 |
| `for_each_field(obj, values)` | 11.44 |
| `template_arg_count<pair<int,string>>` | 1.86 |
| `for_each_template_arg<pair<int,string>>` | 3.44 |
| `template_arg_count<int>` (unregistered) | 1.93 |

Same overall envelope as the Release table in §3.2; run-to-run variance is
normal. Source: `docs/benchmarks/2026-10-11-155753-LTCG-rtti_bench_new.txt`.

## 4. Results — rtti_bench (existing core)

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

Source: `docs/benchmarks/2026-10-11-155020-Debug-rtti_bench.txt`,
`2026-10-11-155020-Release-rtti_bench.txt`.

### 4.1 LTCG (build-opt `Release`, explicit `/GL` + `/LTCG`)

Captured from the `build-opt` LTO configuration (same binary family, 7-sample
median as in §4):

```
field_walk                   median   866039308 ops/s  (min   819694975, max   880331286)  0.114-0.122 s, 7 samples, total 39400000000
get_set_by_name              median    94030829 ops/s  (min    90446089, max    96022646)  0.104-0.111 s, 7 samples, total 9999999
reflect_roundtrip            median      823474 ops/s  (min      741937, max      987924)  1.012-1.348 s, 7 samples, total 500001500000
```

Within the same overall envelope as the §4 Release capture. Source:
`docs/benchmarks/2026-10-11-155719-LTCG-rtti_bench.txt`.

### 4.2 rtti_bench_parse (JSON / XML document parse throughput, Release)

~1 MB source (json=1048525 bytes, xml=1048513 bytes), 30 iters/run; values are
the median of 7 consecutive runs:

| Benchmark | MB/s (median of 7) | it/s (median) | checksum |
| --- | ---: | ---: | ---: |
| `json parse` | 64.3 | 64.3 | **341910** |
| `xml parse` | 45.2 | 45.2 | **314550** |

Speedup vs the 2026-10-10 Release baseline (json 10.6 MB/s, xml 35.3 MB/s):
json ~6×, xml ~1.3×. Checksums identical to that snapshot, confirming the same
document produces the same node pool. Source:
`docs/benchmarks/2026-10-11-155207-Release-rtti_bench_parse.txt`.

## 5. Assertion baselines by build mode (test-suite cross-check)

The benchmark numbers above come from the `bench/` microbenchmarks. The
industrial build matrix is cross-checked by the test suites; every binary below
exits `0` with zero failures (assertion totals unchanged since 2026-10-09):

| Suite | Debug `build/` | Release `build/` | LTCG `build-opt/` | Gates-off `build-off/` |
| --- | ---: | ---: | ---: | ---: |
| `rtti_tests` | 55847 | 55847 | 55847 | 32872 |
| `rtti_proto_extension_tests` | 160 | 160 | 160 | — |
| `rtti_flat_schema_tests` | 408 | 408 | 408 | — |

Release-over-Debug speedups (2026-10-11): `field_walk` ~4.5×,
`get_set_by_name` ~7.1×, `reflect_roundtrip` ~16.8× (roundtrip benefits most
from inlining/LTO).

## 6. Interpretation & limitations (spec D3)

- **~1 ns/op is the measurement floor**: `enum_count` / `template_arg_count`
  are compile-time table reads; 1.02 ns/op in Release reflects the loop-bound
  measurement floor, not a meaningful per-op cost.
- **`for_each_field(obj, values)` Debug cost includes construction**: each
  iteration constructs a fresh `BenchDerived` (with a `std::string`), so Debug
  521.16 ns/op includes per-iteration object construction; Release 11.40 ns/op
  reflects the tuned traversal path.
- **Empty / unregistered paths**: `enum_count<Empty>` (empty table),
  `for_each_enum<Empty>`, `template_arg_count<int>` (unregistered) exercise the
  safe "no iteration" path — cost is call+loop overhead, no out-of-range access.
- **Run-to-run variance**: each line is a median of 7 samples / runs, which
  resists scheduler noise; a single machine/day number is not a universal claim
  — re-run on the target machine before quoting.
- **Reproduction**: `scripts/run_bench.ps1 -Config Debug|Release` (optional
  `-Iterations N`) reproduces the rtti_bench / rtti_bench_new captures and
  writes environment-headed raw output into
  `docs/benchmarks/<stamp>-<Config>-<target>.txt` (see `bench/README.md`);
  `rtti_bench_parse` is run directly from `build/bench/<Config>/`.
- **Coverage note**: this record is Windows/MSVC only. GCC/Clang and Linux
  build/zero-warning gates are verified by GitHub Actions (ubuntu runner), not
  by local measurement.

## 7. Artefacts (2026-10-11)

| File | Content |
| --- | --- |
| `docs/benchmarks/2026-10-11-155020-Debug-rtti_bench_new.txt` | rtti_bench_new Debug raw output (11 rows, median of 7) |
| `docs/benchmarks/2026-10-11-155020-Release-rtti_bench_new.txt` | rtti_bench_new Release raw output |
| `docs/benchmarks/2026-10-11-155753-LTCG-rtti_bench_new.txt` | rtti_bench_new LTCG (build-opt) raw output |
| `docs/benchmarks/2026-10-11-155020-Debug-rtti_bench.txt` | rtti_bench Debug raw output (3 rows ops/s, median/min/max of 7) |
| `docs/benchmarks/2026-10-11-155020-Release-rtti_bench.txt` | rtti_bench Release raw output |
| `docs/benchmarks/2026-10-11-155719-LTCG-rtti_bench.txt` | rtti_bench LTCG (build-opt) raw output |
| `docs/benchmarks/2026-10-11-155207-Release-rtti_bench_parse.txt` | rtti_bench_parse Release raw output (7 runs; checksums 341910 / 314550) |
| `bench/bench_reflect.cpp` / `bench/bench_new_apis.cpp` / `bench/bench_parse.cpp` / `bench/bench_timer.hpp` | benchmark sources |
| `scripts/run_bench.ps1` | reproduction script (rtti_bench / rtti_bench_new) |

Historical captures (kept): `docs/benchmarks/2026-10-09-*.txt`,
`2026-10-09-LTCG-*.txt`, `2026-10-10-112330-Release-*.txt`, `2026-10-09-msvc.md`.