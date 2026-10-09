// Review Focus cases. Task 9 of the RTTI reflect-core plan.
// Pins every Review Focus line from the plan header:
// unregistered type, empty struct, custom-allocator containers,
// default-vs-strict missing key, ADL override inside a container,
// and unknown-name runtime safety.

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include "box.hpp"
#include <string>
#include <vector>
#include <set>

struct Empty {};

RTTI_DEFINE_TYPE(Empty);

namespace rtti_test_lib {

struct Unregistered {
    int a = 0;
};

template <typename T>
struct custom_alloc {
    using value_type = T;
    custom_alloc() = default;
    template <typename U>
    custom_alloc(const custom_alloc<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
    template <typename U>
    bool operator==(const custom_alloc<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const custom_alloc<U>&) const noexcept { return false; }
};

struct WithAlloc {
    std::vector<int, custom_alloc<int>> v;
    std::set<std::string, std::less<std::string>, custom_alloc<std::string>> s;
    RTTI_DEFINE_TYPE_INTRUSIVE(WithAlloc, v, s)
};

struct ReviewDefaults {
    int t = 5;
    std::string h;
    RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ReviewDefaults, t, h)
};

struct Strict {
    int t = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(Strict, t)
};

// ADL-converted element type (NOT registered); used inside a container.
struct AdlElem {
    int v = 0;
};
void from_value(const rtti::value_io* src, AdlElem& e, rtti::error_code& ec) {
    int64_t i = 0;
    if (src->as_integer(i)) {
        e.v = static_cast<int>(i);
        ec = rtti::error_code::ok;
    } else {
        ec = rtti::error_code::kind_mismatch;
    }
}
void to_value(rtti::value_io* dst, const AdlElem& e, rtti::error_code& ec) {
    dst->make_scalar(rtti::field_value::of(int64_t(e.v)));
    ec = rtti::error_code::ok;
}

RTTI_TEST_CASE(unregistered_type_compile_error) {
    static_assert(!rtti::is_reflected<Unregistered>::value,
                  "unregistered must be detected");
}

RTTI_TEST_CASE(empty_struct_ok) {
    static_assert(rtti::field_count<Empty>() == 0, "empty struct has zero fields");

    Empty e;
    box* src = make_obj();
    RTTI_CHECK_EQ(rtti::from_value(src, e), rtti::error_code::ok);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, e), rtti::error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::object);
    RTTI_CHECK_EQ(dst->size(), size_t(0));
    delete dst;
    delete src;
}

RTTI_TEST_CASE(custom_allocator_containers) {
    box* src = make_obj();
    auto* v = make_arr();
    v->push_back(make_int(7));
    src->set_key("v", v);
    auto* s = make_arr();
    s->push_back(make_str("x"));
    s->push_back(make_str("y"));
    src->set_key("s", s);

    WithAlloc out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.v.size(), size_t(1));
    RTTI_CHECK_EQ(out.v[0], 7);
    RTTI_CHECK_EQ(out.s.size(), size_t(2));
    RTTI_CHECK(out.s.count("x") == 1);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(dst->size(), size_t(2));
    delete dst;
    delete src;
}

RTTI_TEST_CASE(default_variant_missing_key) {
    box* src = make_obj();
    src->set_key("h", make_str("hi"));
    ReviewDefaults d;
    d.t = 99;
    RTTI_CHECK_EQ(rtti::from_value(src, d), rtti::error_code::ok);
    RTTI_CHECK_EQ(d.t, 99);
    RTTI_CHECK_EQ(d.h, std::string("hi"));
    delete src;
}

RTTI_TEST_CASE(strict_variant_missing_key) {
    box* src = make_obj();
    src->set_key("h", make_str("hi"));
    Strict st;
    auto e = rtti::from_value(src, st);
    RTTI_CHECK(e == rtti::error_code::missing_key);
    delete src;
}

RTTI_TEST_CASE(adl_handwritten_inside_container) {
    box* src = make_arr();
    src->push_back(make_int(11));
    src->push_back(make_int(22));
    std::vector<AdlElem> out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.size(), size_t(2));
    RTTI_CHECK_EQ(out[0].v, 11);
    RTTI_CHECK_EQ(out[1].v, 22);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(dst->size(), size_t(2));
    delete dst;
    delete src;
}

RTTI_TEST_CASE(unknown_name_runtime_safe) {
    RTTI_CHECK(!rtti::find_field<Strict>("nope"));
    rtti::field_value fv;
    Strict st;
    RTTI_CHECK(!rtti::get_field(st, "nope", fv));
}

} // namespace rtti_test_lib