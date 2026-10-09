// Field-traversal tests (industrialization spec B2). Uses locally-defined
// fixtures (unique names) instead of the type_judge_test.cpp ones to avoid
// cross-TU ODR/redefinition hazards.
#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <string>

namespace rtti_test_detail {

// Single-level inheritance fixture for the merged (base-first) field order.
struct FieldIterBase {
    int x = 0;
};

struct FieldIterDerived : FieldIterBase {
    int y = 0;
};

// Unrelated reflected type used as a negative / single-field case.
struct FieldIterOther {
    int w = 0;
};

} // namespace rtti_test_detail

RTTI_DEFINE_TYPE(rtti_test_detail::FieldIterBase, x)
RTTI_DEFINE_TYPE_DERIVED(rtti_test_detail::FieldIterDerived,
                         rtti_test_detail::FieldIterBase, y)
RTTI_DEFINE_TYPE(rtti_test_detail::FieldIterOther, w)

namespace rtti_test_lib {

RTTI_TEST_CASE(ForEachFieldNamesAndOrder) {
    std::string acc;
    rtti::for_each_field<rtti_test_detail::FieldIterDerived>(
        [&](const char* name, rtti::type_id) { acc += name; acc += ';'; });
    // Base field first (merged order), then own.
    RTTI_CHECK_EQ(acc, std::string("x;y;"));
}

RTTI_TEST_CASE(ForEachFieldValues) {
    rtti_test_detail::FieldIterDerived d;
    d.x = 7;
    d.y = 9;
    int sum = 0;
    rtti::for_each_field(
        d, [&](const char*, rtti::type_id, const rtti::field_value& v) {
            if (v.is_integer()) {
                sum += static_cast<int>(v.as_integer());
            }
        });
    RTTI_CHECK_EQ(sum, 16);
}

RTTI_TEST_CASE(ForEachFieldCountMatches) {
    std::size_t n = 0;
    rtti::for_each_field<rtti_test_detail::FieldIterOther>(
        [&](const char*, rtti::type_id) { ++n; });
    RTTI_CHECK_EQ(n, rtti::field_count<rtti_test_detail::FieldIterOther>());
}

} // namespace rtti_test_lib
