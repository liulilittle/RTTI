// Custom-allocator support tests.
// Task A1: is_sequence must exclude any std::basic_string specialization,
// even one built on a custom allocator (a string is a scalar/text type,
// not a character sequence, for conversion purposes).

#include "test_harness.hpp"

#include <rtti/json.hpp>
#include <rtti/reflection.hpp>

#include <cstddef>
#include <new>
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

} // namespace rtti_test_lib
