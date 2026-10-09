// Reflect-core microbenchmark: field walk, get/set by name, and recursive
// reflect roundtrip through the value_io test double (tests/box.hpp).
// Reports ops/s to stdout; no assertions. Run manually: ./rtti_bench
// English comments only; C++17.

#include <cstdio>
#include <string>

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

constexpr long long kIterations = 1000000;

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
    for (long long i = 0; i < kIterations; ++i) {
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
    for (long long i = 0; i < kIterations; ++i) {
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
    for (long long i = 0; i < kIterations; ++i) {
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

template <typename Fn>
void report(const char* name, Fn fn) {
    rtti_bench::timer tm;
    volatile long long sink = fn();
    (void)sink;
    const double sec = tm.elapsed_sec();
    const double ops = kIterations / sec;
    std::printf("%-28s %10.1f ops/s  (%.3f s, %lld total)\n", name, ops, sec, sink);
}

} // namespace

int main() {
    report("field_walk", bench_field_walk);
    report("get_set_by_name", bench_get_set_by_name);
    report("reflect_roundtrip", bench_reflect_roundtrip);
    return 0;
}
