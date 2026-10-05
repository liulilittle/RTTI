// Runtime reflection query API tests. Task 8 of the RTTI reflect-core plan.
// Covers type_info/get_type, find_field/get_field/set_field and range-for.

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <string>

namespace rtti_test_lib {

struct Config {
    std::string host;
    int port = 0;
};

} // namespace rtti_test_lib

RTTI_DEFINE_TYPE(rtti_test_lib::Config, host, port)

namespace rtti_test_lib {

RTTI_TEST_CASE(get_type_like_dotnet) {
    Config c;
    c.host = "x";
    c.port = 80;
    auto t = rtti::get_type(c);
    RTTI_CHECK_EQ(t.field_count(), size_t(2));
    const auto* f = rtti::find_field<Config>("port");
    RTTI_CHECK(f != nullptr);
    RTTI_CHECK(f->type == &typeid(int));
}

RTTI_TEST_CASE(runtime_get_set_by_name) {
    Config c;
    c.host = "a";
    c.port = 1;
    rtti::field_value v;
    RTTI_CHECK(rtti::get_field(c, "port", v));
    RTTI_CHECK_EQ(v.as_integer(), int64_t(1));
    RTTI_CHECK(rtti::set_field(c, "port", rtti::field_value::of(int64_t(8080))));
    RTTI_CHECK_EQ(c.port, 8080);
    RTTI_CHECK(!rtti::get_field(c, "nope", v)); // unknown name -> false, no UB
    RTTI_CHECK(!rtti::set_field(c, "nope", rtti::field_value::of(int64_t(1))));
}

RTTI_TEST_CASE(type_info_static_accessors) {
    RTTI_CHECK_EQ(rtti::type_info<Config>::field_count(), size_t(2));
    RTTI_CHECK(rtti::type_info<Config>::name() != nullptr);
    const auto* f = rtti::type_info<Config>::find_field("host");
    RTTI_CHECK(f != nullptr);
    RTTI_CHECK_EQ(std::string(f->name), std::string("host"));
}

RTTI_TEST_CASE(range_for_over_fields) {
    Config c;
    c.host = "h";
    c.port = 2;
    int seen = 0;
    std::string first_name;
    for (const auto& f : rtti::fields_of<Config>::array) {
        ++seen;
        if (seen == 1) {
            first_name = f->name;
        }
        (void)f;
    }
    RTTI_CHECK_EQ(seen, 2);
    RTTI_CHECK_EQ(first_name, std::string("host"));
}

} // namespace rtti_test_lib