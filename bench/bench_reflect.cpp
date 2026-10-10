// Reflect-core microbenchmark: field walk, get/set by name, and recursive
// reflect roundtrip through the value_io test double (tests/box.hpp).
// Each row reports the MEDIAN ops/s of 7 timed samples taken after a warm-up
// pass, together with the fastest/slowest of those samples (min/max) so drift
// stays visible. Iteration counts are set per case so every timed window lands
// well above the timer resolution / scheduler quantum (>=50-100 ms); sub-ms
// benches were previously dominated by measurement noise.
// Run manually: ./rtti_bench
// English comments only; C++17.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "bench_timer.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>

#include "../tests/box.hpp"

namespace {

struct BenchType {
    int a = 0;
    double b = 0.0;
    std::string c;
    std::vector<int> d;
    RTTI_DEFINE_TYPE_INTRUSIVE(BenchType, a, b, c, d)
};

// Timed samples per row; median reported, min/max for scatter.
constexpr std::size_t kSamples = 7;

// Per-case iteration counts: each timed window must land >=50-100 ms. The old
// single 1e6 constant left field_walk at ~1-3 ms (sub-ms, timer-resolition
// bound) and get_set_by_name at ~9 ms; both were noise-dominated. Per-case
// counts keep the (already ~0.8 s) roundtrip from being repeated 1e8 times.
constexpr long long kFieldWalkIters = 100000000;   // ~0.2-0.5 s (4 accesses/iter)
constexpr long long kGetSetByNameIters = 10000000; // ~0.1 s
constexpr long long kRoundtripIters = 1000000;     // ~0.8 s already

// Field walk over the registered table.
long long bench_field_walk() {
    // The reflection table carries three parallel static arrays; this bench
    // walks only the accessor table, so reference the metadata statics too
    // (odr-use) to keep clang's -Wunused-const-variable quiet in a TU that
    // never touches them otherwise.
    (void)&BenchType::rtti_registration::members;
    (void)&BenchType::rtti_registration::desc[0];
    const auto* const* arr = rtti::fields<BenchType>();
    const std::size_t n = rtti::field_count<BenchType>();
    long long acc = 0;
    for (long long i = 0; i < kFieldWalkIters; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            const rtti::field_access_base* fa = arr[j];
            acc += fa != nullptr ? static_cast<long long>(fa->name[0]) : 0;
        }
    }
    return acc;
}

// get/set by name through the runtime query API.
long long bench_get_set_by_name() {
    BenchType t;
    long long acc = 0;
    for (long long i = 0; i < kGetSetByNameIters; ++i) {
        rtti::field_value v;
        if (rtti::get_field(t, "a", v)) {
            acc += static_cast<long long>(v.as_integer());
        }
        (void)rtti::set_field(t, "a", rtti::field_value::of(int64_t(1)));
    }
    return acc;
}

// Full recursive reflect roundtrip through the value_io test double.
long long bench_reflect_roundtrip() {
    long long acc = 0;
    for (long long i = 0; i < kRoundtripIters; ++i) {
        BenchType t;
        t.a = static_cast<int>(i);
        t.c = "bench";
        t.d.push_back(3);

        rtti_test_lib::box* dst = new rtti_test_lib::box;
        auto e = rtti::to_value(dst, t);
        acc += static_cast<long long>(e == rtti::error_code::ok);

        BenchType back;
        auto e2 = rtti::from_value(dst, back);
        acc += static_cast<long long>(e2 == rtti::error_code::ok) + back.a;
        delete dst;
    }
    return acc;
}

// Times `fn` kSamples times after one warm-up call; prints median/min/max as
// ops/s. `iters` must match the loop count inside `fn` so ops/s is correct.
template <typename Fn>
void report(const char* name, long long iters, Fn fn) {
    volatile long long warm = fn();
    (void)warm;

    long long samples_ns[kSamples] = {};
    for (std::size_t s = 0; s < kSamples; ++s) {
        rtti_bench::timer tm;
        volatile long long sink = fn();
        (void)sink;
        samples_ns[s] = tm.elapsed_ns();
    }
    std::sort(samples_ns, samples_ns + kSamples);
    const double min_s = samples_ns[0] / 1e9;
    const double median_s = samples_ns[kSamples / 2] / 1e9;
    const double max_s = samples_ns[kSamples - 1] / 1e9;
    std::printf("%-28s median %11.0f ops/s  (min %11.0f, max %11.0f)  "
                "%.3f-%.3f s, %zu samples, total %lld\n",
                name, iters / median_s, iters / max_s, iters / min_s,
                min_s, max_s, kSamples, static_cast<long long>(warm));
    std::fflush(stdout);
}

} // namespace

int main() {
    rtti_bench::print_environment("rtti_bench", static_cast<int>(kSamples));
    std::printf("timed iters per sample: field_walk %lld, get_set_by_name %lld, "
                "reflect_roundtrip %lld (warm-up run first)\n",
                kFieldWalkIters, kGetSetByNameIters, kRoundtripIters);
    std::printf("min/max = fastest/slowest of %zu samples, median = middle\n\n",
                kSamples);

    report("field_walk", kFieldWalkIters, bench_field_walk);
    report("get_set_by_name", kGetSetByNameIters, bench_get_set_by_name);
    report("reflect_roundtrip", kRoundtripIters, bench_reflect_roundtrip);
    return 0;
}
