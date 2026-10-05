// Macro-family tests. Task 5 of the RTTI reflect-core plan.
// Covers RTTI_DEFINE_TYPE (external / INTRUSIVE / _WITH_DEFAULT variants)
// and RTTI_DEFINE_ENUM.

#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <string>

namespace rtti_test_lib {

struct Person {
    std::string name;
    int age = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(Person, name, age)
};

struct PrivatePerson {
private:
    std::string name;
    int age = 0;

public:
    RTTI_DEFINE_TYPE_INTRUSIVE(PrivatePerson, name, age)
};

struct Defaults {
    int t = 5;
    std::string h;
};

struct Point {
    int x = 0;
    int y = 0;
};

enum class Color { Red, Green, Blue };

} // namespace rtti_test_lib

// Explicit specializations of templates in ::rtti must be declared at global
// scope (or inside ::rtti itself) per [temp.expl.spec]; therefore the
// non-intrusive macros are invoked here with fully qualified type names.
RTTI_DEFINE_TYPE_WITH_DEFAULT(rtti_test_lib::Defaults, t, h)
RTTI_DEFINE_ENUM(rtti_test_lib::Color, Red, Green, Blue)
RTTI_DEFINE_TYPE(rtti_test_lib::Point, x, y)

namespace rtti_test_lib {

RTTI_TEST_CASE(external_macro_registers_fields) {
    RTTI_CHECK_EQ(rtti::field_count<Point>(), size_t(2));
    const auto* f = rtti::fields<Point>();
    RTTI_CHECK_EQ(std::string(f[0]->name), std::string("x"));
    RTTI_CHECK_EQ(std::string(f[1]->name), std::string("y"));
}

RTTI_TEST_CASE(intrusive_macro_works) {
    RTTI_CHECK_EQ(rtti::field_count<Person>(), size_t(2));
    Person p;
    p.name = "alice";
    p.age = 30;
    const auto* f = rtti::fields<Person>();
    RTTI_CHECK_EQ(f[1]->get(&p).as_integer(), int64_t(30));
    RTTI_CHECK(f[1]->set(&p, rtti::field_value::of(int64_t(31))));
    RTTI_CHECK_EQ(p.age, 31);
}

RTTI_TEST_CASE(intrusive_private_members) {
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_lib::PrivatePerson>(), size_t(2));
    rtti_test_lib::PrivatePerson p;
    const auto* f = rtti::fields<rtti_test_lib::PrivatePerson>();
    RTTI_CHECK(f[1]->set(&p, rtti::field_value::of(int64_t(11))));
    RTTI_CHECK_EQ(f[1]->get(&p).as_integer(), int64_t(11));
}

RTTI_TEST_CASE(with_default_sets_trait) {
    static_assert(rtti::has_default<Defaults>::value,
                  "WITH_DEFAULT must mark has_default=true");
    static_assert(!rtti::has_default<Point>::value,
                  "plain macro must mark has_default=false");
    RTTI_CHECK_EQ(rtti::field_count<Defaults>(), size_t(2));
}

RTTI_TEST_CASE(enum_to_string) {
    RTTI_CHECK_EQ(rtti::enum_to_string(rtti_test_lib::Color::Green),
                  std::string("Green"));
    RTTI_CHECK_EQ(rtti::enum_to_string(rtti_test_lib::Color::Red),
                  std::string("Red"));
}

RTTI_TEST_CASE(enum_from_string) {
    RTTI_CHECK(rtti::enum_from_string<rtti_test_lib::Color>("Blue") ==
               rtti_test_lib::Color::Blue);
    RTTI_CHECK(rtti::enum_from_string<rtti_test_lib::Color>("Green") ==
               rtti_test_lib::Color::Green);
}

} // namespace rtti_test_lib