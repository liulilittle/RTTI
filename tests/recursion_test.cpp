// Recursion / ADL / default-key tests. Task 7 of the RTTI reflect-core plan.
// Covers nested-struct walk (reflect_from/reflect_to), ADL override for
// hand-written conversions, and _WITH_DEFAULT missing-key behavior.

#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include <rtti/reflection.hpp>
#include <string>
#include <vector>

#include "box.hpp"

namespace rtti_test_lib {

struct Inner {
    int a = 0;
    std::string b;
};

struct Outer {
    int n = 0;
    Inner in;
    std::vector<Inner> list;
};

struct Defaults {
    int t = 5;
    std::string h;
};

// Custom type with hand-written ADL conversions (NOT registered).
// 3-argument hook per the plan contract; wins over built-in paths.
struct Custom {
    int v = 0;
};
void from_value(const rtti::value_io* src, Custom& c, rtti::error_code& e) {
    int64_t i = 0;
    if (src->as_integer(i)) {
        c.v = static_cast<int>(i);
        e = rtti::error_code::ok;
    } else {
        e = rtti::error_code::kind_mismatch;
    }
}
void to_value(rtti::value_io* dst, const Custom& c, rtti::error_code& e) {
    dst->make_scalar(rtti::field_value::of(int64_t(c.v)));
    e = rtti::error_code::ok;
}

} // namespace rtti_test_lib

RTTI_DEFINE_TYPE(rtti_test_lib::Inner, a, b)
RTTI_DEFINE_TYPE(rtti_test_lib::Outer, n, in, list)
RTTI_DEFINE_TYPE_WITH_DEFAULT(rtti_test_lib::Defaults, t, h)

namespace rtti_test_lib {

RTTI_TEST_CASE(nested_struct_roundtrip) {
    Outer out;
    box* src = make_obj();
    auto* n = make_int(5);
    src->set_key("n", n);
    auto* in = make_obj();
    in->set_key("a", make_int(1));
    in->set_key("b", make_str("x"));
    src->set_key("in", in);
    auto* lst = make_arr();
    auto* e1 = make_obj();
    e1->set_key("a", make_int(9));
    e1->set_key("b", make_str("y"));
    lst->push_back(e1);
    src->set_key("list", lst);

    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.n, 5);
    RTTI_CHECK_EQ(out.in.a, 1);
    RTTI_CHECK_EQ(out.in.b, std::string("x"));
    RTTI_CHECK_EQ(out.list.size(), size_t(1));
    RTTI_CHECK_EQ(out.list[0].a, 9);
    RTTI_CHECK_EQ(out.list[0].b, std::string("y"));

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK(dst->is_object);
    delete dst;
    delete src;
}

RTTI_TEST_CASE(adl_override_wins) {
    box* src = make_int(42);
    rtti_test_lib::Custom c;
    RTTI_CHECK_EQ(rtti::from_value(src, c), rtti::error_code::ok);
    RTTI_CHECK_EQ(c.v, 42);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, c), rtti::error_code::ok);
    int64_t chk = 0;
    RTTI_CHECK(dst->as_integer(chk));
    RTTI_CHECK_EQ(chk, int64_t(42));
    delete dst;
    delete src;
}

RTTI_TEST_CASE(strict_missing_key_reports_error) {
    box* src = make_obj();
    src->set_key("t", make_int(3));
    // Outer requires field "t" (present) but also "h" absent -> missing_key.
    rtti_test_lib::Outer out;
    auto e = rtti::from_value(src, out);
    RTTI_CHECK(e == rtti::error_code::no_such_field || e == rtti::error_code::missing_key);
    delete src;
}

RTTI_TEST_CASE(default_variant_keeps_value_on_missing_key) {
    box* src = make_obj();
    src->set_key("h", make_str("hello"));
    rtti_test_lib::Defaults d;
    d.t = 99;
    RTTI_CHECK_EQ(rtti::from_value(src, d), rtti::error_code::ok);
    RTTI_CHECK_EQ(d.t, 99);       // kept (missing)
    RTTI_CHECK_EQ(d.h, std::string("hello"));
    delete src;
}

} // namespace rtti_test_lib
