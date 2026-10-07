// Method reflection tests. Spec section 22: reflect_method<T>("Name", args...)
// calls registered member/static methods through the method_info registry;
// methods<T>() exposes the table (name + static flag) alongside the field
// table. Parameters are injected with the same ctor_arg family used by
// constructors (scalar field_value / value_io node); results travel back as a
// scalar field_value or a value_io node; void methods return ok without a
// value. All error paths return error_code and never throw or crash (spec 0).
// Names are prefixed Method/MethodX to stay unique across the test TUs.
// Core-only: no format adapter headers.

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include <string>
#include <vector>
#include <cstdint>

#include "box.hpp"

namespace rtti_test_detail {

struct MethodSimple {
    int value = 0;

    int get() const { return value; }
    void set(int v) { value = v; }
    void noop() {}
    int add(int a, int b) { return a + b; }
    double scale(double x) const { return x * 2.0; }
    std::string_view label() const { return "simple"; }
    void concatenate(std::string s) { value = static_cast<int>(s.size()); }

    static int sq(int x) { return x * x; }
    static std::string greeting() { return "hi"; }
    static int counter() { return 7; }
    static void reset_counter() {}
};

// Container parameter + composite return paths (value_io node injection).
struct MethodBoxed {
    std::vector<int> items;

    void push_all(const std::vector<int>& v) {
        items.insert(items.end(), v.begin(), v.end());
    }
    std::vector<int> snapshot() const { return items; }
    std::size_t count() const { return items.size(); }
};

// A reflected template instance: field table + template info + method table
// all coexist on one type (spec 20.3 / 22 coexistence).
template <typename T>
struct MethodTempl {
    T v{};
    int amount() const { return static_cast<int>(v); }
    void store(T x) { v = x; }
    RTTI_DEFINE_TYPE_INTRUSIVE(MethodTempl, v)
};

} // namespace rtti_test_detail

// Registry specializations live at namespace scope (the macros open an rtti
// namespace block, so calling them inside rtti_test_detail would specialize
// rtti_test_detail::rtti::fields_of instead of ::rtti::fields_of).
RTTI_DEFINE_TYPE(rtti_test_detail::MethodSimple, value)

RTTI_DEFINE_METHOD(rtti_test_detail::MethodSimple, get)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodSimple, set)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodSimple, noop)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodSimple, add)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodSimple, scale)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodSimple, label)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodSimple, concatenate)
RTTI_DEFINE_STATIC_METHOD(rtti_test_detail::MethodSimple, sq)
RTTI_DEFINE_STATIC_METHOD(rtti_test_detail::MethodSimple, greeting)
RTTI_DEFINE_STATIC_METHOD(rtti_test_detail::MethodSimple, counter)
RTTI_DEFINE_STATIC_METHOD(rtti_test_detail::MethodSimple, reset_counter)

RTTI_DEFINE_METHOD(rtti_test_detail::MethodBoxed, push_all)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodBoxed, snapshot)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodBoxed, count)

RTTI_TEMPLATE_INFO(rtti_test_detail::MethodTempl, int)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodTempl<int>, amount)
RTTI_DEFINE_METHOD(rtti_test_detail::MethodTempl<int>, store)

namespace rtti_test_lib {

RTTI_TEST_CASE(MethodMemberCallNoArgsAndVoid) {
    rtti_test_detail::MethodSimple obj;
    obj.value = 3;
    {
        rtti::field_value fv;
        auto r = rtti::reflect_method(obj, "get", {});
        RTTI_CHECK(r.ok());
        RTTI_CHECK_EQ(r.value.as_integer(), int64_t(3));
        RTTI_CHECK(r.err.code == rtti::error_code::ok);
    }
    {
        auto r = rtti::reflect_method(obj, "noop", {});
        RTTI_CHECK(r.ok());
        RTTI_CHECK_EQ(r.value.type(), rtti::field_value::kind::none);
        RTTI_CHECK_EQ(obj.value, 3);
    }
}

RTTI_TEST_CASE(MethodMemberCallWithArgsAndReturn) {
    rtti_test_detail::MethodSimple obj;
    auto r = rtti::reflect_method(obj, "add",
                                  {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(2))),
                                   rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(3)))});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_integer(), int64_t(5));
}

RTTI_TEST_CASE(MethodMemberCallSetThenGetState) {
    rtti_test_detail::MethodSimple obj;
    auto s = rtti::reflect_method(obj, "set",
                                  {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(41)))});
    RTTI_CHECK(s.ok());
    RTTI_CHECK_EQ(obj.value, 41);
    auto g = rtti::reflect_method(obj, "get", {});
    RTTI_CHECK(g.ok());
    RTTI_CHECK_EQ(g.value.as_integer(), int64_t(41));
}

RTTI_TEST_CASE(MethodRealReturnDouble) {
    rtti_test_detail::MethodSimple obj;
    auto r = rtti::reflect_method(obj, "scale",
                                  {rtti::ctor_arg::scalar(rtti::field_value::of(2.5))});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_real(), 5.0);
}

RTTI_TEST_CASE(MethodStringViewReturn) {
    rtti_test_detail::MethodSimple obj;
    auto r = rtti::reflect_method(obj, "label", {});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_string(), rtti::string_view("simple"));
}

RTTI_TEST_CASE(MethodStringParamInjection) {
    rtti_test_detail::MethodSimple obj;
    auto r = rtti::reflect_method(obj, "concatenate",
                                  {rtti::ctor_arg::scalar(
                                      rtti::field_value::of(rtti::string_view("hello")))});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(obj.value, 5);
}

RTTI_TEST_CASE(MethodStaticCallNoArgs) {
    auto r = rtti::reflect_method<rtti_test_detail::MethodSimple>("counter", {});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_integer(), int64_t(7));
}

RTTI_TEST_CASE(MethodStaticCallWithArgs) {
    auto r = rtti::reflect_method<rtti_test_detail::MethodSimple>(
        "sq", {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(6)))});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_integer(), int64_t(36));
}

RTTI_TEST_CASE(MethodStaticVoidOkNoValue) {
    auto r = rtti::reflect_method<rtti_test_detail::MethodSimple>("reset_counter", {});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.type(), rtti::field_value::kind::none);
}

RTTI_TEST_CASE(MethodStaticCompositeViaNode) {
    rtti_test_lib::box* sink = new rtti_test_lib::box;
    auto r = rtti::reflect_method<rtti_test_detail::MethodSimple>("greeting", {},
                                                                  sink);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(sink->type(), rtti::value_io::kind::string);
    rtti::string_view got;
    RTTI_CHECK(sink->as_string(got));
    RTTI_CHECK_EQ(got, rtti::string_view("hi"));
    delete sink;
}

RTTI_TEST_CASE(MethodUnregisteredNameNoSuchMethod) {
    rtti_test_detail::MethodSimple obj;
    auto r = rtti::reflect_method(obj, "does_not_exist", {});
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::no_such_method);
    auto s = rtti::reflect_method<rtti_test_detail::MethodSimple>("nope", {});
    RTTI_CHECK(!s.ok());
    RTTI_CHECK(s.err.code == rtti::error_code::no_such_method);
    RTTI_CHECK_EQ(std::string(rtti::to_string(rtti::error_code::no_such_method)),
                  std::string("no such method"));
}

RTTI_TEST_CASE(MethodArgCountMismatchKindMismatch) {
    rtti_test_detail::MethodSimple obj;
    {
        auto r = rtti::reflect_method(obj, "add",
                                      {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1)))});
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
    }
    {
        auto r = rtti::reflect_method(obj, "add",
                                      {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1))),
                                       rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(2))),
                                       rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(3)))});
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
    }
}

RTTI_TEST_CASE(MethodTypeMismatchKindMismatch) {
    rtti_test_detail::MethodSimple obj;
    auto r = rtti::reflect_method(obj, "add",
                                  {rtti::ctor_arg::scalar(
                                      rtti::field_value::of(rtti::string_view("boom"))),
                                   rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1)))});
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(MethodMemberViaStaticEntryNeedsInstance) {
    // Calling a member method through the no-instance entry reports an
    // invalid instance.
    auto r = rtti::reflect_method<rtti_test_detail::MethodSimple>("get", {});
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(MethodInvalidObjectRefSafeError) {
    rtti::object_ref<rtti_test_detail::MethodSimple> bad;
    auto r = rtti::reflect_method(bad, "get", {});
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(MethodObjectRefPath) {
    auto handle = rtti::make_object<rtti_test_detail::MethodSimple>();
    handle->set(11);
    auto r = rtti::reflect_method(handle, "get", {});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_integer(), int64_t(11));
}

RTTI_TEST_CASE(MethodInstanceHandlePath) {
    auto handle = rtti::make_object<rtti_test_detail::MethodSimple>();
    handle->set(5);
    rtti::instance inst = rtti::instance::from(std::move(handle));
    auto r = rtti::reflect_method(inst, "get", {});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_integer(), int64_t(5));
}

RTTI_TEST_CASE(MethodStaticThroughObjectRef) {
    auto handle = rtti::make_object<rtti_test_detail::MethodSimple>();
    auto r = rtti::reflect_method(handle, "sq",
                                  {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(9)))});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_integer(), int64_t(81));
}

RTTI_TEST_CASE(MethodTableQuery) {
    const rtti::method_info* table = rtti::methods<rtti_test_detail::MethodSimple>();
    std::size_t n = rtti::method_count<rtti_test_detail::MethodSimple>();
    RTTI_CHECK_EQ(n, size_t(11));
    RTTI_CHECK(table != nullptr);

    auto has = [&](const char* nm, bool st) {
        for (std::size_t i = 0; i < n; ++i) {
            if (std::string(table[i].name) == nm) {
                RTTI_CHECK(table[i].is_static == st);
                return true;
            }
        }
        return false;
    };
    RTTI_CHECK(has("get", false));
    RTTI_CHECK(has("set", false));
    RTTI_CHECK(has("noop", false));
    RTTI_CHECK(has("add", false));
    RTTI_CHECK(has("scale", false));
    RTTI_CHECK(has("label", false));
    RTTI_CHECK(has("concatenate", false));
    RTTI_CHECK(has("sq", true));
    RTTI_CHECK(has("greeting", true));
    RTTI_CHECK(has("counter", true));
    RTTI_CHECK(has("reset_counter", true));
}

RTTI_TEST_CASE(MethodCoexistWithFieldTable) {
    // The same type has a field table and a method table side by side.
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::MethodSimple>(), size_t(1));
    rtti_test_detail::MethodSimple obj;
    obj.value = 20;
    rtti::field_value fv;
    RTTI_CHECK(rtti::get_field(obj, "value", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(20));

    auto r = rtti::reflect_method(obj, "get", {});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_integer(), int64_t(20));
}

RTTI_TEST_CASE(MethodCoexistWithTemplateInfo) {
    RTTI_CHECK_EQ(rtti::template_arg_count<rtti_test_detail::MethodTempl<int>>(),
                  size_t(1));
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::MethodTempl<int>>(), size_t(1));

    rtti_test_detail::MethodTempl<int> t;
    t.v = 13;
    auto r = rtti::reflect_method(t, "amount", {});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.as_integer(), int64_t(13));

    auto s = rtti::reflect_method(t, "store",
                                  {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(99)))});
    RTTI_CHECK(s.ok());
    RTTI_CHECK_EQ(t.v, 99);
}

RTTI_TEST_CASE(MethodNodeParamInjection) {
    rtti_test_detail::MethodBoxed boxed;
    rtti_test_lib::box* src = make_arr();
    for (int i = 1; i <= 3; ++i) {
        src->push_back(make_int(i));
    }
    auto r = rtti::reflect_method(boxed, "push_all", {rtti::ctor_arg::node(src)});
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(boxed.items.size(), size_t(3));
    RTTI_CHECK_EQ(boxed.items[0], 1);
    RTTI_CHECK_EQ(boxed.items[1], 2);
    RTTI_CHECK_EQ(boxed.items[2], 3);
    delete src;
}

RTTI_TEST_CASE(MethodNodeReturnComposite) {
    rtti_test_detail::MethodBoxed boxed;
    boxed.items.push_back(4);
    boxed.items.push_back(5);
    rtti_test_lib::box* sink = new rtti_test_lib::box;
    auto r = rtti::reflect_method(boxed, "snapshot", {}, sink);
    RTTI_CHECK(r.ok());
    RTTI_CHECK(sink->size() == size_t(2));
    int64_t a = 0, b = 0;
    RTTI_CHECK(sink->at(0)->as_integer(a));
    RTTI_CHECK(sink->at(1)->as_integer(b));
    RTTI_CHECK_EQ(a, int64_t(4));
    RTTI_CHECK_EQ(b, int64_t(5));
    delete sink;
}

RTTI_TEST_CASE(MethodCompositeReturnWithoutNode) {
    rtti_test_detail::MethodBoxed boxed;
    boxed.items.push_back(1);
    auto r = rtti::reflect_method(boxed, "snapshot", {});
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(MethodErrorsDoNotCrash) {
    rtti_test_detail::MethodSimple obj;
    // A swath of failing calls must keep returning error codes.
    RTTI_CHECK(!rtti::reflect_method(obj, "", {}).ok());
    RTTI_CHECK(!rtti::reflect_method(obj, "missing", {}).ok());
    auto r = rtti::reflect_method(obj, "set", {rtti::ctor_arg::node(nullptr)});
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
    // Null node source must fail cleanly (no crash) for a scalar target too.
    auto s = rtti::reflect_method(obj, "concatenate", {rtti::ctor_arg::node(nullptr)});
    RTTI_CHECK(!s.ok());
    RTTI_CHECK(s.err.code == rtti::error_code::kind_mismatch);
}

} // namespace rtti_test_lib