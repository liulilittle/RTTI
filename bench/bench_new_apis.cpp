// Microbenchmarks for the traversal APIs added by the industrialization plan
// (spec B1/B2/B3): enum traversal (enum_count / enum_name_at / enum_value_at /
// for_each_enum), field traversal (for_each_field, both overloads) and
// template-argument traversal (for_each_template_arg). Each measurement
// reports the MEDIAN of several timed samples taken after a warm-up pass, in
// nanoseconds per operation. The environment header (compiler, config, CPU,
// iteration count) is printed before the table so the numbers stay honest.
//
// This file covers only the registered-type and empty-table paths that are
// legal at compile time; unregistered non-empty types are a compile-time error
// for the enum/field tables by design (see the test suite), so the "unregistered"
// proxy here is the registered-but-empty table (enum_count<E> == 0).
//
// English comments only; C++17; no third-party dependencies.

#include "bench_timer.hpp"

#include <rtti/reflection.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace rtti_bench_detail {

// A registered enum with several enumerators (typical case).
enum class BenchColor { red = 1, green = 2, blue = 4, alpha = 8 };

// Registered-but-empty enum: count 0, no iteration.
enum class BenchEmpty {};

// A typical reflected structure with a base class (merged base-first fields),
// mirroring the type_judge fixture shape used across the test suite.
struct BenchBase {
    int id = 0;
    double score = 0.0;
};

struct BenchDerived : BenchBase {
    std::string name;
    bool active = false;
};

} // namespace rtti_bench_detail

RTTI_DEFINE_ENUM(rtti_bench_detail::BenchColor, red, green, blue, alpha)
RTTI_DEFINE_ENUM(rtti_bench_detail::BenchEmpty)
RTTI_DEFINE_TYPE(rtti_bench_detail::BenchBase, id, score)
RTTI_DEFINE_TYPE_DERIVED(rtti_bench_detail::BenchDerived,
                         rtti_bench_detail::BenchBase, name, active)
RTTI_TEMPLATE_INFO(std::pair, int, std::string)

namespace {

using namespace rtti_bench_detail;

constexpr std::size_t kSamples = 7;      // timed samples per row; median used
constexpr std::size_t kWarmupMult = 64;  // warm-up = iters / kWarmupMult

// Returns the median ns/op over `samples` timed runs, each running `iters`
// calls of `op`. The operation must return a data-dependent value (driven by
// position) so the optimizer cannot hoist it out of the timed loop.
template <typename Op>
double measure_ns_per_op(std::size_t iters, std::size_t samples, Op op) {
    // Warm-up pass: fault in code paths / static tables before timing.
    volatile long long warm = 0;
    const std::size_t warm_iters = iters / kWarmupMult == 0 ? 1 : iters / kWarmupMult;
    for (std::size_t w = 0; w < warm_iters; ++w) {
        warm += op(w);
    }
    (void)warm;

    std::vector<long long> samples_ns;
    samples_ns.reserve(samples);
    for (std::size_t s = 0; s < samples; ++s) {
        rtti_bench::timer tm;
        volatile long long sink = 0;
        for (std::size_t i = 0; i < iters; ++i) {
            sink += op(i);
        }
        (void)sink;
        samples_ns.push_back(tm.elapsed_ns());
    }
    std::sort(samples_ns.begin(), samples_ns.end());
    const double median_ns = static_cast<double>(samples_ns[samples / 2]);
    return median_ns / static_cast<double>(iters);
}

template <typename Op>
void report(const char* name, std::size_t iters, Op op) {
    const double ns = measure_ns_per_op(iters, kSamples, op);
    std::printf("%-34s %10.2f ns/op  (%zu iters, median of %zu)\n", name, ns,
                iters, kSamples);
    std::fflush(stdout);
}

// --- new-API benchmarks ----------------------------------------------------

long long bench_enum_count(std::size_t i) {
    // constexpr table lookup; the result is position-independent on purpose.
    (void)i;
    return static_cast<long long>(rtti::enum_count<BenchColor>());
}

long long bench_enum_name_at(std::size_t i) {
    const char* n = rtti::enum_name_at<BenchColor>(i & 3u);
    return n != nullptr ? static_cast<long long>(n[0]) : 0;
}

long long bench_enum_value_at(std::size_t i) {
    return static_cast<long long>(static_cast<int>(rtti::enum_value_at<BenchColor>(i & 3u)));
}

long long bench_for_each_enum(std::size_t i) {
    long long acc = 0;
    rtti::for_each_enum<BenchColor>([&](const char* name, BenchColor v) {
        acc += static_cast<long long>(static_cast<int>(v));
        if (name != nullptr) {
            acc += static_cast<long long>(name[0]);
        }
    });
    (void)i;
    return acc;
}

long long bench_enum_empty_count(std::size_t i) {
    (void)i;
    return static_cast<long long>(rtti::enum_count<BenchEmpty>());
}

long long bench_for_each_enum_empty(std::size_t i) {
    long long acc = 0;
    rtti::for_each_enum<BenchEmpty>([&](const char*, BenchEmpty) { acc += 1; });
    (void)i;
    return acc;
}

long long bench_for_each_field_names(std::size_t i) {
    long long acc = 0;
    rtti::for_each_field<BenchDerived>([&](const char* name, rtti::type_id) {
        if (name != nullptr) {
            acc += static_cast<long long>(name[0]);
        }
    });
    (void)i;
    return acc;
}

long long bench_for_each_field_values(std::size_t i) {
    BenchDerived obj;
    obj.id = static_cast<int>(i);
    obj.score = static_cast<double>(i);
    obj.name = "bench";
    obj.active = true;
    long long acc = 0;
    rtti::for_each_field(obj, [&](const char* name, rtti::type_id,
                                  const rtti::field_value& v) {
        if (name != nullptr) {
            acc += static_cast<long long>(name[0]);
        }
        if (v.is_integer()) {
            acc += v.as_integer();
        } else if (v.type() == rtti::field_value::kind::real) {
            acc += static_cast<long long>(v.as_real());
        }
    });
    return acc;
}

long long bench_template_arg_count(std::size_t i) {
    (void)i;
    return static_cast<long long>(rtti::template_arg_count<std::pair<int, std::string>>());
}

long long bench_for_each_template_arg(std::size_t i) {
    long long acc = 0;
    rtti::for_each_template_arg<std::pair<int, std::string>>(
        [&](std::size_t, const rtti::type_desc* td) {
            acc += td != nullptr ? 1 : 0;
        });
    (void)i;
    return acc;
}

long long bench_template_arg_count_int(std::size_t i) {
    (void)i;
    return static_cast<long long>(rtti::template_arg_count<int>());
}

} // namespace

int main(int argc, char** argv) {
    std::size_t iters = 20000000;
    if (argc > 1) {
        iters = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    }

    rtti_bench::print_environment("rtti_bench_new", kSamples);
    std::printf("timed iters: %zu per sample (warm-up %zu)\n\n",
                iters, iters / kWarmupMult == 0 ? 1 : iters / kWarmupMult);

    std::printf("== enum traversal (count/name/value) ==\n");
    report("enum_count<Color>", iters, bench_enum_count);
    report("enum_name_at<Color>", iters, bench_enum_name_at);
    report("enum_value_at<Color>", iters, bench_enum_value_at);
    report("for_each_enum<Color>", iters, bench_for_each_enum);

    std::printf("\n== empty enums (count 0) ==\n");
    report("enum_count<Empty>", iters, bench_enum_empty_count);
    report("for_each_enum<Empty>", iters, bench_for_each_enum_empty);

    std::printf("\n== for_each_field (base+derived, 4 merged fields) ==\n");
    report("for_each_field<Derived> names", iters, bench_for_each_field_names);
    report("for_each_field(obj, values)", iters, bench_for_each_field_values);

    std::printf("\n== for_each_template_arg ==\n");
    report("template_arg_count<pair<int,string>>", iters, bench_template_arg_count);
    report("for_each_template_arg<pair<int,string>>", iters, bench_for_each_template_arg);
    report("template_arg_count<int> (unreg.)", iters, bench_template_arg_count_int);

    return 0;
}