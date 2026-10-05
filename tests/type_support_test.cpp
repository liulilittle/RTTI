// Compile-time shape-trait detection tests for the extended type families
// introduced by the perf/usability plan (Task B1): optional, variant, tuple,
// pair and fixed-size sequences (std::array).

#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <array>
#include <optional>
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

} // namespace rtti_test_lib
