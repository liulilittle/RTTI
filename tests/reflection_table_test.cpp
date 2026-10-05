// Field-table reflection tests. Task 4 of the RTTI reflect-core plan.
// Covers field_value, field_access_base, typed_field_access<T,M>, fields_of<T>.

#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <string>

namespace rtti_test_detail {

struct Point {
    int x = 0;
    int y = 0;
};

} // namespace rtti_test_detail

using rtti_test_detail::Point;

template <>
struct rtti::fields_of<Point> {
    static constexpr std::size_t count = 2;
    static const rtti::field_access_base* const array[2];
};

const rtti::field_access_base* const rtti::fields_of<Point>::array[2] = {
    new rtti::typed_field_access<Point, int>("x", &Point::x, &typeid(int)),
    new rtti::typed_field_access<Point, int>("y", &Point::y, &typeid(int)),
};

RTTI_TEST_CASE(field_value_kinds) {
    rtti::field_value none;
    RTTI_CHECK(none.type() == rtti::field_value::kind::none);
    RTTI_CHECK(rtti::field_value::of(false).type() == rtti::field_value::kind::boolean);
    RTTI_CHECK(rtti::field_value::of(int64_t(3)).type() == rtti::field_value::kind::integer);
    RTTI_CHECK(rtti::field_value::of(2.5).type() == rtti::field_value::kind::real);
    RTTI_CHECK(rtti::field_value::of(rtti::string_view("s")).type() == rtti::field_value::kind::string);
}

RTTI_TEST_CASE(field_value_accessors) {
    RTTI_CHECK_EQ(rtti::field_value::of(int64_t(9)).as_integer(), int64_t(9));
    RTTI_CHECK_EQ(rtti::field_value::of(1.5).as_real(), 1.5);
    RTTI_CHECK(rtti::field_value::of(true).as_bool());
    RTTI_CHECK_EQ(rtti::field_value::of(rtti::string_view("hi")).as_string(),
                  rtti::string_view("hi"));
}

RTTI_TEST_CASE(field_count_and_names) {
    RTTI_CHECK_EQ(rtti::field_count<Point>(), size_t(2));
    const auto* f = rtti::fields<Point>();
    RTTI_CHECK_EQ(std::string(f[0]->name), std::string("x"));
    RTTI_CHECK_EQ(std::string(f[1]->name), std::string("y"));
    RTTI_CHECK(f[0]->type == &typeid(int));
}

RTTI_TEST_CASE(typed_get_set) {
    Point p;
    p.x = 3;
    p.y = 5;
    const auto* f = rtti::fields<Point>();
    RTTI_CHECK_EQ(f[0]->get(&p).as_integer(), int64_t(3));
    RTTI_CHECK(f[0]->set(&p, rtti::field_value::of(int64_t(9))));
    RTTI_CHECK_EQ(p.x, 9);
    RTTI_CHECK(!f[0]->set(&p, rtti::field_value::of("oops"))); // kind mismatch
    RTTI_CHECK(!f[0]->set(&p, rtti::field_value::of(2.5)));
}
