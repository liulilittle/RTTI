// Compile-time shape-trait detection tests for the extended type families
// introduced by the perf/usability plan (Task B1): optional, variant, tuple,
// pair and fixed-size sequences (std::array). Task B2 then converts each new
// family through the format-agnostic core (rtti::from_value / rtti::to_value)
// and round-trips them through the JSON adapter.

#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <rtti/version.hpp>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace rtti_test_lib {

RTTI_TEST_CASE(new_type_traits_detect) {
    static_assert(rtti::is_optional<std::optional<int>>::value, "");
    static_assert(!rtti::is_optional<int>::value, "");
    static_assert(rtti::is_variant<std::variant<int, std::string>>::value, "");
    static_assert(!rtti::is_variant<int>::value, "");
    static_assert(rtti::is_tuple<std::tuple<int, char>>::value, "");
    static_assert(!rtti::is_tuple<std::vector<int>>::value, "");
    static_assert(!rtti::is_tuple<std::array<int, 3>>::value, "array must NOT be tuple");
    static_assert(rtti::is_pair<std::pair<int, int>>::value, "");
    static_assert(rtti::is_fixed_sequence<std::array<int, 3>>::value, "");
    static_assert(!rtti::is_fixed_sequence<std::vector<int>>::value, "");
    static_assert(!rtti::is_fixed_sequence<std::string>::value, "");
    RTTI_CHECK(true);
}

// The generated version header must exist and advertise the single-sourced
// version string (industrialization plan Task E1).
RTTI_TEST_CASE(VersionMacros) {
    RTTI_CHECK(std::string(rtti::version()) == std::string(RTTI_VERSION_STRING));
}

} // namespace rtti_test_lib

// Task B2 round-trips go through the JSON adapter; gate the include and tests
// so a stripped build (RTTI_ENABLE_JSON = 0) skips them without tripping
// json.hpp's #error guard.
#if (!defined(RTTI_ENABLE_JSON) || RTTI_ENABLE_JSON)

#include <rtti/json.hpp>

namespace rtti_test_lib {

// An alternative whose copy constructor throws for a specific value and whose
// move constructor is potentially-throwing (not noexcept): assigning it over a
// live int alternative takes the standard destructive path (int destroyed
// first, then in-place copy), so the copy throw leaves the variant
// valueless_by_exception(). A noexcept move would instead let the library use
// the strong-guarantee path (construct-then-destroy), which never goes
// valueless.
struct BoomType {
    int v = 0;
    BoomType() = default;
    BoomType(const BoomType& o) : v(o.v) {
        if (v == 42) {
            throw std::runtime_error("boom");
        }
    }
    BoomType(BoomType&& o) : v(o.v) {}
    BoomType& operator=(BoomType&&) = default;
    BoomType& operator=(const BoomType&) = default;
};

RTTI_TEST_CASE(b2_variant_valueless_safe_conversion) {
    using V = std::variant<int, BoomType>;
    V v = 7;
    BoomType boom;
    boom.v = 42;
    try {
        v = boom;  // copy ctor throws after the int is destroyed
    } catch (const std::runtime_error&) {
    }
    RTTI_CHECK(v.valueless_by_exception());

    // Serialization of a valueless variant is safe: the write path reports an
    // error and the format adapter returns an empty string (never crashes).
    const std::string j = rtti::json::serialize(v);
    RTTI_CHECK(j.empty());

    // The direct converter reports the same error code instead of crashing.
    rtti::json::document doc;
    RTTI_CHECK_EQ(rtti::to_value(&doc, v), rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(b2_optional_roundtrip_json) {
    std::optional<int> oi = 42;
    const std::string j = rtti::json::serialize(oi);
    RTTI_CHECK(j.find("42") != std::string::npos);
    auto back = rtti::json::parse_as<std::optional<int>>(j);
    RTTI_CHECK(back.ok() && back.value && *back.value == 42);
    // Empty optional serializes to JSON null and parses back as empty.
    std::optional<int> none;
    const std::string jn = rtti::json::serialize(none);
    RTTI_CHECK(jn.find("null") != std::string::npos);
    auto empty = rtti::json::parse_as<std::optional<int>>("null");
    RTTI_CHECK(empty.ok() && !empty.value.has_value());
}

RTTI_TEST_CASE(b2_variant_roundtrip_json) {
    // Active alternative index 1 (std::string) round-trips as {"index":N,"value":V}.
    std::variant<int, std::string> v = std::string("hi");
    const std::string j = rtti::json::serialize(v);
    RTTI_CHECK(j.find("\"index\"") != std::string::npos);
    auto back = rtti::json::parse_as<std::variant<int, std::string>>(j);
    RTTI_CHECK(back.ok() && back.value.index() == 1 &&
               std::get<1>(back.value) == "hi");
    // Index-0 alternative (int) travels through the same object shape.
    std::variant<int, std::string> vi = 5;
    auto back2 = rtti::json::parse_as<std::variant<int, std::string>>(
        rtti::json::serialize(vi));
    RTTI_CHECK(back2.ok() && back2.value.index() == 0 &&
               std::get<0>(back2.value) == 5);
}

RTTI_TEST_CASE(b2_tuple_roundtrip_json) {
    std::tuple<int, std::string, double> t{1, "a", 2.5};
    const std::string j = rtti::json::serialize(t);
    RTTI_CHECK(j.find("2.5") != std::string::npos);
    auto back = rtti::json::parse_as<std::tuple<int, std::string, double>>(j);
    RTTI_CHECK(back.ok() && std::get<0>(back.value) == 1 &&
               std::get<1>(back.value) == "a" &&
               std::get<2>(back.value) == 2.5);
}

RTTI_TEST_CASE(b2_pair_roundtrip_json) {
    std::pair<int, std::string> p{7, "x"};
    auto back = rtti::json::parse_as<std::pair<int, std::string>>(
        rtti::json::serialize(p));
    RTTI_CHECK(back.ok() && back.value.first == 7 && back.value.second == "x");
}

RTTI_TEST_CASE(b2_array_roundtrip_json) {
    std::array<int, 3> a{1, 2, 3};
    auto back = rtti::json::parse_as<std::array<int, 3>>(
        rtti::json::serialize(a));
    RTTI_CHECK(back.ok() && back.value[0] == 1 && back.value[2] == 3);
}

RTTI_TEST_CASE(b2_vector_bool_roundtrip_json) {
    // vector<bool> is a bit-packed specialization: the sequence path must
    // copy each proxy element into a real bool before conversion.
    std::vector<bool> vb{true, false, true};
    const std::string j = rtti::json::serialize(vb);
    RTTI_CHECK(j.find("true") != std::string::npos);
    auto back = rtti::json::parse_as<std::vector<bool>>(j);
    RTTI_CHECK(back.ok() && back.value.size() == 3 && back.value[0] &&
               !back.value[1] && back.value[2]);
}

} // namespace rtti_test_lib

#endif
