// Custom-allocator support tests.
// Task A1: is_sequence must exclude any std::basic_string specialization,
// even one built on a custom allocator (a string is a scalar/text type,
// not a character sequence, for conversion purposes).

#include "test_harness.hpp"

#include <rtti/reflection.hpp>

#include <cstddef>
#include <cstring>
#include <deque>
#include <map>
#include <new>
#include <queue>
#include <string>

namespace rtti_test_lib {

template <typename T>
struct ProbeAlloc {
    using value_type = T;
    ProbeAlloc() noexcept = default;
    template <typename U> ProbeAlloc(const ProbeAlloc<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
    template <typename U> struct rebind { using other = ProbeAlloc<U>; };
    bool operator==(const ProbeAlloc&) const noexcept { return true; }
    bool operator!=(const ProbeAlloc&) const noexcept { return false; }
};
using ProbeStr = std::basic_string<char, std::char_traits<char>, ProbeAlloc<char>>;

RTTI_TEST_CASE(custom_alloc_string_is_scalar) {
    static_assert(!rtti::is_sequence<ProbeStr>::value,
                  "custom-alloc basic_string must not be a sequence");
    RTTI_CHECK(!rtti::is_sequence<ProbeStr>::value);
}

} // namespace rtti_test_lib

// Format adapter round-trips (custom allocators through JSON/XML/INI).
// Gated so a stripped build (all RTTI_ENABLE_* = 0) skips this section
// without tripping the format headers' #error guards.
#if (!defined(RTTI_ENABLE_JSON) || RTTI_ENABLE_JSON) && \
    (!defined(RTTI_ENABLE_XML) || RTTI_ENABLE_XML) && \
    (!defined(RTTI_ENABLE_INI) || RTTI_ENABLE_INI)

#include <rtti/ini.hpp>
#include <rtti/json.hpp>
#include <rtti/xml.hpp>

// Reflected holder carrying every custom-allocator container under test. The
// format adapters bind reflected struct roots to objects with array children,
// so the containers round trip through JSON/XML/INI the same way native
// std::vector/std::set members do (bare container roots are not a binding the
// XML/INI adapters expose).
struct CustomContainers {
    std::vector<int, rtti_test_lib::ProbeAlloc<int>> v;
    std::set<int, std::less<int>, rtti_test_lib::ProbeAlloc<int>> s;
    std::stack<int, std::deque<int, rtti_test_lib::ProbeAlloc<int>>> st;
    std::priority_queue<int, std::deque<int, rtti_test_lib::ProbeAlloc<int>>> pq;
    RTTI_DEFINE_TYPE_INTRUSIVE(CustomContainers, v, s, st, pq)
};

namespace rtti_test_lib {

// Task A2: the custom-alloc string must serialize as a JSON string scalar
// ("hello"), not as a character array ([104,101,...]), and parse back.
RTTI_TEST_CASE(custom_alloc_string_roundtrip_json) {
    using S = ProbeStr;
    S in("hello");
    std::string j = rtti::json::serialize(in);
    RTTI_CHECK(j.find("\"hello\"") != std::string::npos);
    auto back = rtti::json::parse_as<S>(j);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK(back.value == S("hello"));
    }
}

// Task A3: queue/stack/priority_queue adapters must rebuild with the type's
// own container/allocator, not a defaulted temporary that cannot swap.
RTTI_TEST_CASE(custom_alloc_queue_roundtrip) {
    using Q = std::queue<int, std::deque<int, ProbeAlloc<int>>>;
    Q q;
    q.push(1);
    q.push(2);
    q.push(3);
    std::string j = rtti::json::serialize(q);
    auto back = rtti::json::parse_as<Q>(j);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK(back.value.size() == 3);
    }
}

// Task A4: case-insensitive map comparator and custom-alloc string keys round
// trip through all three formats.
struct ProbeCiLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return rtti_test::ci_stricmp(a.c_str(), b.c_str()) < 0;
    }
};
using ProbeLessMap = std::map<std::string, int, ProbeCiLess>;
using ProbeAllocMap = std::map<ProbeStr, int>;

RTTI_TEST_CASE(custom_less_map_three_formats) {
    ProbeLessMap m;
    m["Alpha"] = 1;
    m["beta"] = 2;
    m["ALPHA"] = 3;
    RTTI_CHECK(rtti::json::parse_as<ProbeLessMap>(rtti::json::serialize(m)).value.size() == 2);
    RTTI_CHECK(rtti::xml::parse_as<ProbeLessMap>(rtti::xml::serialize(m)).value.size() == 2);
    RTTI_CHECK(rtti::ini::parse_as<ProbeLessMap>(rtti::ini::serialize(m)).value.size() == 2);
}

RTTI_TEST_CASE(custom_alloc_key_map_three_formats) {
    ProbeAllocMap m;
    m[ProbeStr("k")] = 42;
    RTTI_CHECK(rtti::json::parse_as<ProbeAllocMap>(rtti::json::serialize(m)).value.at(ProbeStr("k")) == 42);
    RTTI_CHECK(rtti::xml::parse_as<ProbeAllocMap>(rtti::xml::serialize(m)).value.at(ProbeStr("k")) == 42);
    RTTI_CHECK(rtti::ini::parse_as<ProbeAllocMap>(rtti::ini::serialize(m)).value.at(ProbeStr("k")) == 42);
}

// Custom-allocator vector<int> / set<int> / stack / priority_queue round trip
// through all three formats as reflected-struct members (allocator-agnostic
// container conversion; the adapters bind the struct root to an object).
RTTI_TEST_CASE(custom_alloc_containers_three_formats) {
    using Deq = std::deque<int, ProbeAlloc<int>>;
    using Vec = std::vector<int, ProbeAlloc<int>>;
    using Set = std::set<int, std::less<int>, ProbeAlloc<int>>;

    CustomContainers in;
    in.v = {1, 2, 3};
    in.s = {5, 9};
    in.st.push(1);
    in.st.push(2);
    in.st.push(3);
    in.pq.push(4);
    in.pq.push(1);
    in.pq.push(9);

    const auto check = [](const rtti::result<CustomContainers>& r,
                          const CustomContainers& in) {
        RTTI_CHECK(r.ok());
        if (!r.ok()) {
            return;
        }
        RTTI_CHECK_EQ(r.value.v.size(), size_t(3));
        RTTI_CHECK_EQ(r.value.v[1], in.v[1]);
        RTTI_CHECK_EQ(r.value.s.count(9), size_t(1));
        RTTI_CHECK_EQ(r.value.st.size(), size_t(3));
        RTTI_CHECK_EQ(r.value.pq.size(), size_t(3));
    };

    const auto bj = rtti::json::parse_as<CustomContainers>(
        rtti::json::serialize(in));
    check(bj, in);
    const auto bx = rtti::xml::parse_as<CustomContainers>(
        rtti::xml::serialize(in));
    check(bx, in);
    const auto bi = rtti::ini::parse_as<CustomContainers>(
        rtti::string_view(rtti::ini::serialize(in)));
    check(bi, in);
}

} // namespace rtti_test_lib

#endif
