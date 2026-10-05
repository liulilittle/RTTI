// JSON/XML parser microbenchmark: document parse only (no serialization).
// Builds a ~1 MB source string and times rtti::json::parse / rtti::xml::parse,
// reporting throughput in MB/s and iterations/s. Standalone executable with
// its own main(); NOT part of the test suite. Run manually:
//   bench_parse.exe
// English comments only; C++17; no third-party dependencies.

#include <cstdio>
#include <cstddef>
#include <string>

#include "bench_timer.hpp"
#include <rtti/json.hpp>
#include <rtti/xml.hpp>

namespace {

// A large JSON array of small objects. Repeated item keeps the source cheap to
// construct while still exercising a dense node pool (array + 6 objects each).
std::string make_json_doc(std::size_t target_bytes) {
    const std::string item =
        "{\"id\":1,\"name\":\"alpha\",\"score\":123.5,\"ok\":true,"
        "\"tags\":[\"x\",\"y\",\"z\"],\"nested\":{\"a\":1,\"b\":2}}";
    std::string s = "[";
    while (s.size() + item.size() + 1 < target_bytes) {
        if (s.size() > 1) {
            s += ",";
        }
        s += item;
    }
    s += "]";
    return s;
}

// A large XML document: one root with many repeated child elements that carry
// attributes plus text, exercising element/attribute/text node allocation.
std::string make_xml_doc(std::size_t target_bytes) {
    const std::string item =
        "<item id=\"1\"><name>alpha</name><score>123.5</score>"
        "<ok>true</ok><tags><t>x</t><t>y</t></tags></item>";
    std::string s = "<root>";
    while (s.size() + item.size() < target_bytes) {
        s += item;
    }
    s += "</root>";
    return s;
}

// Parse `src` `iters` times, keep a cheap checksum so the tree cannot be
// optimized away, and report throughput. `parse_fn` is a lambda over result.
template <typename ParseFn>
void report(const char* name, const std::string& src, int iters, ParseFn parse_fn) {
    // Warm-up: one parse to fault in code paths and allocator arenas.
    parse_fn(src);

    rtti_bench::timer tm;
    long long checksum = 0;
    for (int i = 0; i < iters; ++i) {
        checksum += parse_fn(src);
    }
    const double sec = tm.elapsed_sec();
    const double bytes = static_cast<double>(src.size()) * iters;
    const double mb_s = bytes / (1024.0 * 1024.0) / sec;
    const double ops_s = iters / sec;
    std::printf("%-22s %8.1f MB/s  %9.1f it/s  (%.3f s, %lld iters, checksum %lld)\n",
                name, mb_s, ops_s, sec, static_cast<long long>(iters), checksum);
}

long long bench_json_parse(const std::string& src) {
    const auto r = rtti::json::parse(src);
    if (!r.ok()) {
        std::fprintf(stderr, "JSON parse failed: %s\n",
                     rtti::to_string(r.err.code));
        return 0;
    }
    return static_cast<long long>(r.value.size());
}

long long bench_xml_parse(const std::string& src) {
    const auto r = rtti::xml::parse(src);
    if (!r.ok()) {
        std::fprintf(stderr, "XML parse failed: %s\n",
                     rtti::to_string(r.err.code));
        return 0;
    }
    return static_cast<long long>(r.value.size());
}

} // namespace

int main() {
    const std::size_t target = 1024 * 1024; // ~1 MB
    const std::string json_src = make_json_doc(target);
    const std::string xml_src = make_xml_doc(target);
    std::printf("sources: json=%zu bytes, xml=%zu bytes\n",
                json_src.size(), xml_src.size());

    constexpr int kIters = 30;
    report("json parse", json_src, kIters, bench_json_parse);
    report("xml parse", xml_src, kIters, bench_xml_parse);
    return 0;
}
