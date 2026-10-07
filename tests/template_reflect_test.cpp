// Abstract / polymorphic judgment + template-argument reflection tests. Task N
// of the RTTI perf-usability plan (spec section 20). Covers the compile-time
// traits rtti::is_abstract / is_polymorphic / has_virtual_destructor (plus the
// _v forms), the runtime type_desc::is_abstract()/is_polymorphic() queries, the
// explicit template-argument registry (RTTI_TEMPLATE_INFO -> template_info<T>),
// and the introspection API (is_template_instance / template_arg_count /
// template_args / template_arg_at / template_args_of). Registered template
// types coexist with the existing container / reflected machinery (spec 20.3);
// unregistered templates and illegal indices fail safely (empty table /
// nullptr / compile-time rejection), never crash (spec 0). Names are prefixed
// Template/Abstract to stay unique across the test TUs. Core-only: no format
// adapter headers.

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include <tuple>
#include <type_traits>
#include <string>
#include <vector>

namespace rtti_test_detail {

// Abstract class (pure virtual member): std::is_abstract is true.
struct AbstractShape {
    virtual int area() const = 0;
    virtual ~AbstractShape() {}
};

// Concrete polymorphic class (virtual destructor only).
struct ConcreteShape {
    virtual ~ConcreteShape() {}
};

// Plain struct: no virtual member functions at all.
struct PlainStruct {
    int x = 0;
};

// Template instance used for argument reflection (single type parameter).
template <typename T>
struct TemplateBox {
    T value{};
};

// Template whose sole parameter is defaulted so `RTTI_TEMPLATE_INFO(Name)` (an
// empty argument list) forms Name<> legally and must register a count-0 table.
template <typename T = int>
struct EmptyTpl {
    T value{};
};

// Template instance with three type parameters.
template <typename T1, typename T2, typename T3>
struct TemplateTriple {
    T1 a{};
    T2 b{};
    T3 c{};
};

// A template that is also a reflected struct (intrusive registration), proving
// template_info and fields_of coexist on one type (spec 20.3). Runtime field
// access on class-template static data members is checked from main-like
// contexts only (their dynamic initialization is unordered, so static-init
// field access would be a test-timing hazard, not a library defect); the
// coexistence checks below are compile-time / type_desc-level.
template <typename T>
struct TemplateRec {
    T payload{};
    RTTI_DEFINE_TYPE_INTRUSIVE(TemplateRec, payload)
};

// Non-template reflected struct for the runtime field-access coverage in this
// file (registered via RTTI_DEFINE_TYPE, whose statics are namespace-scope and
// therefore initialized before the test-registration statics run).
struct TemplateRecPlain {
    int payload = 0;
    std::string note;
};

// Polymorphic hierarchy for the is_polymorphic-gated dynamic check (spec 20.2
// combined with spec 19 / is_instance_of).
struct TemplateShapeBase {
    virtual ~TemplateShapeBase() {}
};

struct TemplateShapeDerived : TemplateShapeBase {
    int d = 0;
};

} // namespace rtti_test_detail

// Explicit template-argument registrations (spec 20.3: a registry, not auto
// deduction). T names the class template; the remaining arguments are its type
// arguments. template_info<T<Args...>> is specialized with the argument list.
RTTI_TEMPLATE_INFO(rtti_test_detail::TemplateBox, int)
RTTI_TEMPLATE_INFO(rtti_test_detail::TemplateBox, std::string)
RTTI_TEMPLATE_INFO(rtti_test_detail::TemplateTriple, int, double, std::string)
RTTI_TEMPLATE_INFO(std::vector, int)
RTTI_TEMPLATE_INFO(rtti_test_detail::TemplateRec, int)
// Empty registration: guarded macro must yield count 0 / empty args tuple.
RTTI_TEMPLATE_INFO(rtti_test_detail::EmptyTpl)

RTTI_DEFINE_TYPE(rtti_test_detail::TemplateRecPlain, payload, note)

namespace rtti_test_lib {

// --- Compile-time abstract / polymorphic / virtual-destructor traits --------

RTTI_TEST_CASE(TemplateTestAbstractTraits) {
    RTTI_CHECK((rtti::is_abstract<rtti_test_detail::AbstractShape>::value));
    RTTI_CHECK((!rtti::is_abstract<rtti_test_detail::ConcreteShape>::value));
    RTTI_CHECK((!rtti::is_abstract<rtti_test_detail::PlainStruct>::value));
    RTTI_CHECK((rtti::is_abstract_v<rtti_test_detail::AbstractShape>));
    RTTI_CHECK((!rtti::is_abstract_v<int>));
    RTTI_CHECK((!rtti::is_abstract_v<std::string>));
}

RTTI_TEST_CASE(TemplateTestPolymorphicTraits) {
    // Any class with virtual functions is polymorphic (vtable present).
    RTTI_CHECK((rtti::is_polymorphic<rtti_test_detail::AbstractShape>::value));
    RTTI_CHECK((rtti::is_polymorphic<rtti_test_detail::ConcreteShape>::value));
    RTTI_CHECK((!rtti::is_polymorphic<rtti_test_detail::PlainStruct>::value));
    RTTI_CHECK((rtti::is_polymorphic_v<rtti_test_detail::ConcreteShape>));
    RTTI_CHECK((!rtti::is_polymorphic_v<int>));

    RTTI_CHECK((rtti::has_virtual_destructor<rtti_test_detail::ConcreteShape>::value));
    RTTI_CHECK((!rtti::has_virtual_destructor<rtti_test_detail::PlainStruct>::value));
    RTTI_CHECK((rtti::has_virtual_destructor_v<rtti_test_detail::AbstractShape>));
    RTTI_CHECK((!rtti::has_virtual_destructor_v<int>));
}

// --- Runtime type_desc::is_abstract / is_polymorphic queries ---------------

RTTI_TEST_CASE(TemplateTestTypeDescFlags) {
    const rtti::type_desc* shape = rtti::type_of<rtti_test_detail::AbstractShape>();
    RTTI_CHECK(shape != nullptr);
    RTTI_CHECK(shape->is_abstract());
    RTTI_CHECK(shape->is_polymorphic());

    const rtti::type_desc* concrete = rtti::type_of<rtti_test_detail::ConcreteShape>();
    RTTI_CHECK(concrete != nullptr);
    RTTI_CHECK(!concrete->is_abstract());
    RTTI_CHECK(concrete->is_polymorphic());

    const rtti::type_desc* plain = rtti::type_of<rtti_test_detail::PlainStruct>();
    RTTI_CHECK(plain != nullptr);
    RTTI_CHECK(!plain->is_abstract());
    RTTI_CHECK(!plain->is_polymorphic());

    // Scalars / strings / containers are neither abstract nor polymorphic.
    RTTI_CHECK(!rtti::type_of<int>()->is_abstract());
    RTTI_CHECK(!rtti::type_of<int>()->is_polymorphic());
    RTTI_CHECK(!rtti::type_of<std::string>()->is_abstract());
    RTTI_CHECK(!rtti::type_of<std::string>()->is_polymorphic());
    RTTI_CHECK(!rtti::type_of<std::vector<int>>()->is_abstract());
    RTTI_CHECK(!rtti::type_of<std::vector<int>>()->is_polymorphic());
}

// A polymorphic class satisfies the pre-check before the runtime dynamic
// comparison (spec 20.2: verify is_polymorphic before dynamic dispatch).
RTTI_TEST_CASE(TemplateTestPolyGateDynamicCheck) {
    RTTI_CHECK((rtti::is_polymorphic<rtti_test_detail::TemplateShapeBase>::value));
    rtti_test_detail::TemplateShapeDerived d;
    RTTI_CHECK(rtti::is_instance_of<rtti_test_detail::TemplateShapeBase>(
        &d, rtti::type_of<rtti_test_detail::TemplateShapeDerived>()->id));
}

// --- Template-argument count ------------------------------------------------

RTTI_TEST_CASE(TemplateTestCount) {
    RTTI_CHECK_EQ(rtti::template_arg_count<rtti_test_detail::TemplateBox<int>>(), size_t(1));
    RTTI_CHECK_EQ(rtti::template_arg_count<rtti_test_detail::TemplateBox<std::string>>(), size_t(1));
    RTTI_CHECK_EQ((rtti::template_arg_count<rtti_test_detail::TemplateTriple<int, double, std::string>>()),
                  size_t(3));
    RTTI_CHECK_EQ(rtti::template_arg_count<std::vector<int>>(), size_t(1));
    // Unregistered instances and non-templates: safe empty table (count 0).
    RTTI_CHECK_EQ(rtti::template_arg_count<rtti_test_detail::TemplateBox<double>>(), size_t(0));
    RTTI_CHECK_EQ(rtti::template_arg_count<int>(), size_t(0));
}

// An empty registration (RTTI_TEMPLATE_INFO with no type arguments) compiles,
// is detected as a template instance, reports count 0 and stays safe to query.
RTTI_TEST_CASE(TemplateTestEmptyRegistrationSafe) {
    using T = rtti_test_detail::EmptyTpl<int>;
    RTTI_CHECK((rtti::is_template_instance<T>::value));
    RTTI_CHECK_EQ(rtti::template_arg_count<T>(), size_t(0));
    RTTI_CHECK((std::is_same<rtti::template_args<T>, std::tuple<>>::value));
    RTTI_CHECK(rtti::template_args_of<T>() == nullptr);
    RTTI_CHECK(rtti::template_arg_desc_of<T>(0) == nullptr);
}

// --- is_template_instance discrimination ------------------------------------

RTTI_TEST_CASE(TemplateTestIsInstance) {
    RTTI_CHECK((rtti::is_template_instance<rtti_test_detail::TemplateBox<int>>::value));
    RTTI_CHECK((rtti::is_template_instance<rtti_test_detail::TemplateTriple<int, double, std::string>>::value));
    RTTI_CHECK((rtti::is_template_instance<std::vector<int>>::value));
    // Unregistered instances and non-templates are not template instances.
    RTTI_CHECK((!rtti::is_template_instance<rtti_test_detail::TemplateBox<double>>::value));
    RTTI_CHECK((!rtti::is_template_instance<int>::value));
    RTTI_CHECK((!rtti::is_template_instance_v<std::string>));
}

// --- template_args<T>: the tuple of template argument types ----------------

RTTI_TEST_CASE(TemplateTestArgsTuple) {
    RTTI_CHECK((std::is_same<rtti::template_args<rtti_test_detail::TemplateBox<int>>,
                             std::tuple<int>>::value));
    RTTI_CHECK((std::is_same<rtti::template_args<rtti_test_detail::TemplateBox<std::string>>,
                             std::tuple<std::string>>::value));
    RTTI_CHECK((std::is_same<rtti::template_args<rtti_test_detail::TemplateTriple<int, double, std::string>>,
                             std::tuple<int, double, std::string>>::value));
    RTTI_CHECK((std::is_same<rtti::template_args<std::vector<int>>,
                             std::tuple<int>>::value));
    // Unregistered instances: empty tuple (safe empty table).
    RTTI_CHECK((std::is_same<rtti::template_args<rtti_test_detail::TemplateBox<double>>,
                             std::tuple<>>::value));
}

// --- template_arg_at<T, I>: the I-th template argument type ----------------

RTTI_TEST_CASE(TemplateTestArgAt) {
    RTTI_CHECK((std::is_same<rtti::template_arg_at<rtti_test_detail::TemplateBox<int>, 0>, int>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<rtti_test_detail::TemplateTriple<int, double, std::string>, 0>, int>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<rtti_test_detail::TemplateTriple<int, double, std::string>, 1>, double>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<rtti_test_detail::TemplateTriple<int, double, std::string>, 2>, std::string>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<std::vector<int>, 0>, int>::value));
}

// --- Runtime template_args_of<T> vs type_of comparison ---------------------

RTTI_TEST_CASE(TemplateTestArgsOfRuntime) {
    const rtti::type_desc* const* box =
        rtti::template_args_of<rtti_test_detail::TemplateBox<int>>();
    RTTI_CHECK(box != nullptr);
    RTTI_CHECK(box[0] == rtti::type_of<int>());

    const rtti::type_desc* const* triple =
        rtti::template_args_of<rtti_test_detail::TemplateTriple<int, double, std::string>>();
    RTTI_CHECK(triple != nullptr);
    RTTI_CHECK(triple[0] == rtti::type_of<int>());
    RTTI_CHECK(triple[1] == rtti::type_of<double>());
    RTTI_CHECK(triple[2] == rtti::type_of<std::string>());

    const rtti::type_desc* const* vec = rtti::template_args_of<std::vector<int>>();
    RTTI_CHECK(vec != nullptr);
    RTTI_CHECK(vec[0] == rtti::type_of<int>());

    // Safe per-index accessor: valid index returns the desc; out-of-range and
    // unregistered templates return nullptr (never crash).
    RTTI_CHECK((rtti::template_arg_desc_of<rtti_test_detail::TemplateTriple<int, double, std::string>>(1) ==
               rtti::type_of<double>()));
    RTTI_CHECK((rtti::template_arg_desc_of<rtti_test_detail::TemplateTriple<int, double, std::string>>(3) ==
               nullptr));
    RTTI_CHECK(rtti::template_arg_desc_of<rtti_test_detail::TemplateBox<double>>(0) == nullptr);
    RTTI_CHECK(rtti::template_arg_desc_of<int>(0) == nullptr);

    // Unregistered templates: safe empty table (nullptr array).
    RTTI_CHECK(rtti::template_args_of<rtti_test_detail::TemplateBox<double>>() == nullptr);
    RTTI_CHECK(rtti::template_args_of<int>() == nullptr);
}

// --- Registered templates coexist with container / reflected machinery ------

RTTI_TEST_CASE(TemplateTestCoexists) {
    // std::vector<int> is both a reflected container (shape detection) and a
    // registered template.
    const rtti::type_desc* v = rtti::type_of<std::vector<int>>();
    RTTI_CHECK(v->kind == rtti::type_kind::container);
    RTTI_CHECK(v->as_container != nullptr);
    RTTI_CHECK(v->as_container->element() == rtti::type_of<int>());
    RTTI_CHECK_EQ(rtti::template_arg_count<std::vector<int>>(), size_t(1));

    // TemplateRec<int> is both a reflected struct (intrusive fields) and a
    // registered template; the reflection and template registries coexist on
    // one type (compile-time / type_desc-level checks; runtime field access on
    // class-template statics is checked from main-like contexts only).
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::TemplateRec<int>>(), size_t(1));
    RTTI_CHECK_EQ(rtti::template_arg_count<rtti_test_detail::TemplateRec<int>>(), size_t(1));
    const rtti::type_desc* rec = rtti::type_of<rtti_test_detail::TemplateRec<int>>();
    RTTI_CHECK(rec->kind == rtti::type_kind::reflected);

    // Runtime field access still works on a plain reflected struct in this
    // file (namespace-scope statics, safe during static init).
    rtti_test_detail::TemplateRecPlain plain{};
    plain.payload = 7;
    plain.note = "hi";
    rtti::field_value fv;
    RTTI_CHECK(rtti::get_field(plain, "payload", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(7));
    RTTI_CHECK(rtti::get_field(plain, "note", fv));
    RTTI_CHECK_EQ(fv.as_string(), std::string("hi"));
}

} // namespace rtti_test_lib