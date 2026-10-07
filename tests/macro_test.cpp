// Macro-family tests. Task 5 of the RTTI reflect-core plan.
// Covers RTTI_DEFINE_TYPE (external / INTRUSIVE / _WITH_DEFAULT variants)
// and RTTI_DEFINE_ENUM.

#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace rtti_test_lib {

// Single-parameter class template exercised by the paren-wrapped template
// argument registry below.
template <typename T>
struct TplSingle {
    T value{};
};

// Two-parameter template for a mixed comma list (a paren-wrapped element side
// by side with a plain element).
template <typename A, typename B>
struct TplDuo {
    A a{};
    B b{};
};

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

// Empty enum: RTTI_DEFINE_ENUM receives zero enumerator arguments. The macro
// must guard the empty argument list (count 0, no malformed tokens).
enum class EmptyColor {};

} // namespace rtti_test_lib

// Explicit specializations of templates in ::rtti must be declared at global
// scope (or inside ::rtti itself) per [temp.expl.spec]; therefore the
// non-intrusive macros are invoked here with fully qualified type names.
RTTI_DEFINE_TYPE_WITH_DEFAULT(rtti_test_lib::Defaults, t, h)
RTTI_DEFINE_ENUM(rtti_test_lib::Color, Red, Green, Blue)
RTTI_DEFINE_ENUM(rtti_test_lib::EmptyColor)
RTTI_DEFINE_TYPE(rtti_test_lib::Point, x, y)
RTTI_TEMPLATE_INFO(rtti_test_lib::TplSingle, (std::pair<int, int>))
RTTI_TEMPLATE_INFO(rtti_test_lib::TplDuo, (std::pair<int, int>), std::string)

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

// An empty enum registration compiles, yields count 0 and all lookups are safe
// (never match, never crash).
RTTI_TEST_CASE(empty_enum_registration_guarded) {
    RTTI_CHECK_EQ(rtti::enum_table<rtti_test_lib::EmptyColor>::count, size_t(0));
    RTTI_CHECK_EQ(std::string(rtti::enum_to_string(rtti_test_lib::EmptyColor{})),
                  std::string());
    RTTI_CHECK_EQ(rtti::enum_from_string<rtti_test_lib::EmptyColor>("Red"),
                  rtti_test_lib::EmptyColor{});
}

// A paren-wrapped template argument such as (std::pair<int,int>) is a single
// argument. RTTI_IS_EMPTY reports the list as empty because it begins with a
// '('; the TEMPLATE_ARGS_OPT selector must treat it as exactly one argument,
// not an empty list (documented usage: wrap comma-bearing type-ids in extra
// parentheses at the call site).
RTTI_TEST_CASE(paren_wrapped_template_arg_not_swallowed) {
    using T1 = rtti_test_lib::TplSingle<std::pair<int, int>>;
    RTTI_CHECK_EQ(rtti::template_arg_count<T1>(), size_t(1));
    RTTI_CHECK((std::is_same<rtti::template_args<T1>,
                             std::tuple<std::pair<int, int>>>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<T1, 0>,
                             std::pair<int, int>>::value));

    // A mixed list keeps counting normally: the wrapped element is one
    // argument and non-comma elements stay separate.
    using T2 = rtti_test_lib::TplDuo<std::pair<int, int>, std::string>;
    RTTI_CHECK_EQ(rtti::template_arg_count<T2>(), size_t(2));
    RTTI_CHECK((std::is_same<rtti::template_args<T2>,
                             std::tuple<std::pair<int, int>, std::string>>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<T2, 1>, std::string>::value));

    // Truly empty registrations still yield count 0 / an empty tuple.
    using T0 = rtti_test_lib::TplSingle<int>;
    RTTI_CHECK_EQ(rtti::template_arg_count<T0>(), size_t(0));
}

} // namespace rtti_test_lib