// Enum-traversal tests (industrialization spec B1).
#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <string>

// Unique enumerator names: other test TUs define global `Color`, and a same-
// named enum with a different enumerator set would collide under the ODR.
enum class EnumReflectColor { red = 1, green = 2, blue = 4 };
enum class EnumReflectEmpty {};
RTTI_DEFINE_ENUM(EnumReflectColor, red, green, blue)
RTTI_DEFINE_ENUM(EnumReflectEmpty)

namespace rtti_test_lib {

RTTI_TEST_CASE(EnumTraversalBasics) {
    RTTI_CHECK_EQ(rtti::enum_count<EnumReflectColor>(), std::size_t(3));
    RTTI_CHECK_EQ(std::string(rtti::enum_name_at<EnumReflectColor>(0)), std::string("red"));
    RTTI_CHECK_EQ(std::string(rtti::enum_name_at<EnumReflectColor>(1)), std::string("green"));
    RTTI_CHECK(rtti::enum_value_at<EnumReflectColor>(2) == EnumReflectColor::blue);
    // Out of range -> safe defaults.
    RTTI_CHECK(rtti::enum_name_at<EnumReflectColor>(3) == nullptr);
    RTTI_CHECK(rtti::enum_value_at<EnumReflectColor>(3) == EnumReflectColor{});
    // Unregistered enum -> count 0, no iteration.
    RTTI_CHECK_EQ(rtti::enum_count<EnumReflectEmpty>(), std::size_t(0));
    std::size_t n = 0;
    rtti::for_each_enum<EnumReflectEmpty>([&](const char*, EnumReflectEmpty) { ++n; });
    RTTI_CHECK_EQ(n, std::size_t(0));
}

RTTI_TEST_CASE(EnumForEachOrder) {
    std::string acc;
    rtti::for_each_enum<EnumReflectColor>([&](const char* name, EnumReflectColor v) {
        acc += name;
        acc += ':';
        acc += std::to_string(static_cast<int>(v));
        acc += ';';
    });
    RTTI_CHECK_EQ(acc, std::string("red:1;green:2;blue:4;"));
}

RTTI_TEST_CASE(EnumDescValues) {
    const rtti::type_desc* td = rtti::type_of<EnumReflectColor>();
    RTTI_CHECK(td != nullptr && td->as_enum != nullptr);
    RTTI_CHECK_EQ(td->as_enum->count, std::size_t(3));
    RTTI_CHECK(td->as_enum->values != nullptr);
    RTTI_CHECK_EQ(td->as_enum->value_size, sizeof(EnumReflectColor));
}

} // namespace rtti_test_lib
