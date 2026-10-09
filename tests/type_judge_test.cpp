// Reflection type-judgment tests. Task N of the RTTI perf-usability plan
// (spec section 19). Covers compile-time conveniences (is_same / is_base_of /
// is_convertible / can_cast), type_of<T>() value equality, inherited field
// merging (a derived type's field table contains its base fields, base-first,
// including virtual inheritance), the runtime type-desc base chain
// (type_desc::bases), and the dynamic judgment APIs on instance handles
// (is<T>, is_derived_from, dynamic_cast_ref, can_convert). Error paths must
// fail safely (error_code / nullptr / false), never crash. Core-only: no
// format adapter headers.

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include <cstdint>
#include <string>

namespace rtti_test_detail {

// Single-level non-virtual inheritance.
struct JudgeBase {
    int x = 0;
};

// Non-virtual inheritance: base fields merge into the derived table.
struct JudgeDerived : JudgeBase {
    int y = 0;
};

// Multi-level inheritance: JudgeDeep : JudgeDerived : JudgeBase.
struct JudgeDeep : JudgeDerived {
    int z = 0;
};

// Virtual inheritance: base fields merge through a virtual base subobject.
struct JudgeVBase {
    int vx = 0;
};

struct JudgeVDerived : virtual JudgeVBase {
    int vy = 0;
};

// Polymorphic type for the runtime is_instance_of check (typeid of *p yields
// the dynamic type only when the static type is polymorphic).
struct JudgePoly {
    virtual ~JudgePoly() {}
};

struct JudgePolyD : JudgePoly {
    int pd = 0;
};

// Unrelated reflected type used as a negative target.
struct JudgeOther {
    int w = 0;
};

} // namespace rtti_test_detail

// Field registrations. Bases are registered before the types that derive from
// them; the derived macro merges the base table.
RTTI_DEFINE_TYPE(rtti_test_detail::JudgeBase, x)
RTTI_DEFINE_TYPE_DERIVED(rtti_test_detail::JudgeDerived, rtti_test_detail::JudgeBase, y)
RTTI_DEFINE_TYPE_DERIVED(rtti_test_detail::JudgeDeep, rtti_test_detail::JudgeDerived, z)
RTTI_DEFINE_TYPE(rtti_test_detail::JudgeVBase, vx)
RTTI_DEFINE_TYPE_DERIVED(rtti_test_detail::JudgeVDerived, rtti_test_detail::JudgeVBase, vy)
// JudgePoly is the polymorphic base used by the type-erased downcast test; it
// must be reflected because instance::from<T> instantiates the field ops.
RTTI_DEFINE_TYPE(rtti_test_detail::JudgePoly)
RTTI_DEFINE_TYPE(rtti_test_detail::JudgePolyD, pd)
RTTI_DEFINE_TYPE(rtti_test_detail::JudgeOther, w)

namespace rtti_test_lib {

// --- Type equality ---------------------------------------------------------

RTTI_TEST_CASE(JudgeTestTypeEquality) {
    // Same type: the same lazily-created type_desc instance.
    RTTI_CHECK(rtti::type_of<rtti_test_detail::JudgeBase>() ==
               rtti::type_of<rtti_test_detail::JudgeBase>());
    RTTI_CHECK(rtti::type_of<rtti_test_detail::JudgeDerived>() ==
               rtti::type_of<rtti_test_detail::JudgeDerived>());
    // Distinct types differ.
    RTTI_CHECK(rtti::type_of<rtti_test_detail::JudgeBase>() !=
               rtti::type_of<rtti_test_detail::JudgeDerived>());
    RTTI_CHECK(rtti::type_of<int>() == rtti::type_of<int>());
    RTTI_CHECK(rtti::type_of<int>() != rtti::type_of<long>());
    RTTI_CHECK(rtti::type_of<double>() != rtti::type_of<int>());
    // Value overload.
    rtti_test_detail::JudgeBase b;
    RTTI_CHECK(rtti::type_of(b) == rtti::type_of<rtti_test_detail::JudgeBase>());

    // Compile-time convenience aliases. Expressions containing template
    // argument commas are parenthesized so RTTI_CHECK sees one macro argument.
    RTTI_CHECK((rtti::is_same<rtti_test_detail::JudgeBase,
                              rtti_test_detail::JudgeBase>::value));
    RTTI_CHECK((!rtti::is_same<rtti_test_detail::JudgeBase,
                               rtti_test_detail::JudgeDerived>::value));
    RTTI_CHECK((rtti::is_same<int, int>::value));
    RTTI_CHECK((!rtti::is_same_v<int, long>));
    RTTI_CHECK((rtti::is_same_v<double, double>));
}

// --- Compile-time inheritance / conversion traits --------------------------

RTTI_TEST_CASE(JudgeTestStaticTraits) {
    RTTI_CHECK((rtti::is_base_of<rtti_test_detail::JudgeBase,
                                 rtti_test_detail::JudgeDerived>::value));
    RTTI_CHECK((!rtti::is_base_of<rtti_test_detail::JudgeDerived,
                                  rtti_test_detail::JudgeBase>::value));
    RTTI_CHECK((rtti::is_base_of<rtti_test_detail::JudgeBase,
                                 rtti_test_detail::JudgeDeep>::value));
    RTTI_CHECK((rtti::is_base_of<rtti_test_detail::JudgeDerived,
                                 rtti_test_detail::JudgeDeep>::value));
    RTTI_CHECK((rtti::is_base_of<rtti_test_detail::JudgeVBase,
                                 rtti_test_detail::JudgeVDerived>::value));
    RTTI_CHECK((!rtti::is_base_of<rtti_test_detail::JudgeOther,
                                  rtti_test_detail::JudgeDerived>::value));
    RTTI_CHECK((!rtti::is_base_of<rtti_test_detail::JudgeBase,
                                  rtti_test_detail::JudgeOther>::value));

    RTTI_CHECK((rtti::is_convertible<int, double>::value));
    // double -> int is an implicit (narrowing) conversion, so it is
    // convertible; a string -> int is not (asserted below).
    RTTI_CHECK((rtti::is_convertible<double, int>::value));
    RTTI_CHECK((rtti::is_convertible<const char*, std::string>::value));
    RTTI_CHECK((rtti::is_convertible<bool, int>::value));
    RTTI_CHECK((rtti::is_convertible_v<int, int64_t>));
    RTTI_CHECK((!rtti::is_convertible_v<std::string, int>));

    RTTI_CHECK((rtti::can_cast<int, double>::value));
    RTTI_CHECK((rtti::can_cast<double, int>::value));
    RTTI_CHECK((!rtti::can_cast<std::string, int>::value));
    RTTI_CHECK((rtti::can_cast<rtti_test_detail::JudgeDerived,
                              rtti_test_detail::JudgeBase>::value));
}

// --- Inherited field merge (non-virtual) -----------------------------------

RTTI_TEST_CASE(JudgeTestInheritedFieldMerge) {
    // The base table itself is untouched.
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::JudgeBase>(), size_t(1));
    // The derived table holds base fields first, then own fields.
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::JudgeDerived>(), size_t(2));
    RTTI_CHECK_EQ(std::string(rtti::fields<rtti_test_detail::JudgeDerived>()[0]->name),
                  std::string("x"));
    RTTI_CHECK_EQ(std::string(rtti::fields<rtti_test_detail::JudgeDerived>()[1]->name),
                  std::string("y"));

    // Field-table iteration covers the merged table in order.
    std::size_t n = 0;
    std::string names;
    for (auto it = rtti::field_begin<rtti_test_detail::JudgeDerived>();
         it != rtti::field_end<rtti_test_detail::JudgeDerived>(); ++it) {
        ++n;
        const rtti::field_access_base* fa = *it;
        RTTI_CHECK(fa != nullptr);
        names += fa->name;
    }
    RTTI_CHECK_EQ(n, size_t(2));
    RTTI_CHECK_EQ(names, std::string("xy"));

    // Get/set by name reach base fields through the merged table.
    rtti_test_detail::JudgeDerived d;
    d.x = 3;
    d.y = 4;
    rtti::field_value fv;
    RTTI_CHECK(rtti::get_field(d, "x", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(3));
    RTTI_CHECK(rtti::get_field(d, "y", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(4));
    RTTI_CHECK(rtti::set_field(d, "x", rtti::field_value::of(int64_t(9))));
    RTTI_CHECK_EQ(d.x, 9);
    // A name that exists on neither type fails safely.
    RTTI_CHECK(!rtti::get_field(d, "zz", fv));
    RTTI_CHECK(!rtti::set_field(d, "zz", rtti::field_value::of(int64_t(1))));
    // The base-field lookup on a derived instance routes through the derived
    // table only: find_field<JudgeBase> still returns just the base table.
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::JudgeBase>(), size_t(1));

    // type_of<T> reflects the merged field table.
    const rtti::type_desc* td = rtti::type_of<rtti_test_detail::JudgeDerived>();
    RTTI_CHECK(td != nullptr);
    RTTI_CHECK(td->kind == rtti::type_kind::reflected);
    RTTI_CHECK(td->as_reflected != nullptr);
    RTTI_CHECK_EQ(td->as_reflected->field_count, size_t(2));
}

// --- Inherited field merge (virtual inheritance) ----------------------------

RTTI_TEST_CASE(JudgeTestVirtualBaseFieldMerge) {
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::JudgeVDerived>(), size_t(2));
    RTTI_CHECK_EQ(std::string(rtti::fields<rtti_test_detail::JudgeVDerived>()[0]->name),
                  std::string("vx"));
    RTTI_CHECK_EQ(std::string(rtti::fields<rtti_test_detail::JudgeVDerived>()[1]->name),
                  std::string("vy"));

    // Get/set through the merged table reaches the virtual base subobject
    // (member-pointer indirection performs the correct offset adjustment).
    rtti_test_detail::JudgeVDerived vd;
    vd.vx = 7;
    vd.vy = 8;
    rtti::field_value fv;
    RTTI_CHECK(rtti::get_field(vd, "vx", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(7));
    RTTI_CHECK(rtti::get_field(vd, "vy", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(8));
    RTTI_CHECK(rtti::set_field(vd, "vx", rtti::field_value::of(int64_t(5))));
    RTTI_CHECK_EQ(vd.vx, 5);

    // The instance handle also reaches base fields on the virtual hierarchy.
    auto h = rtti::make_object<rtti_test_detail::JudgeVDerived>();
    RTTI_CHECK(h.valid());
    RTTI_CHECK_EQ(h.set("vx", rtti::field_value::of(int64_t(11))), rtti::error_code::ok);
    RTTI_CHECK_EQ(h->vx, 11);
    RTTI_CHECK_EQ(h.get("vy", fv), rtti::error_code::ok);
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(0));
}

// --- Multi-level inherited field merge --------------------------------------

RTTI_TEST_CASE(JudgeTestMultiLevelFieldMerge) {
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::JudgeDeep>(), size_t(3));
    RTTI_CHECK_EQ(std::string(rtti::fields<rtti_test_detail::JudgeDeep>()[0]->name),
                  std::string("x"));
    RTTI_CHECK_EQ(std::string(rtti::fields<rtti_test_detail::JudgeDeep>()[1]->name),
                  std::string("y"));
    RTTI_CHECK_EQ(std::string(rtti::fields<rtti_test_detail::JudgeDeep>()[2]->name),
                  std::string("z"));

    rtti_test_detail::JudgeDeep dp;
    dp.x = 1;
    dp.y = 2;
    dp.z = 3;
    rtti::field_value fv;
    RTTI_CHECK(rtti::get_field(dp, "x", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(1));
    RTTI_CHECK(rtti::get_field(dp, "z", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(3));
    RTTI_CHECK(rtti::set_field(dp, "y", rtti::field_value::of(int64_t(20))));
    RTTI_CHECK_EQ(dp.y, 20);

    // object_ref handle set through merged base fields.
    auto h = rtti::make_object<rtti_test_detail::JudgeDeep>();
    RTTI_CHECK(h.valid());
    RTTI_CHECK_EQ(h.set("x", rtti::field_value::of(int64_t(30))), rtti::error_code::ok);
    RTTI_CHECK_EQ(h->x, 30);
}

// --- Runtime type-desc base chain ------------------------------------------

RTTI_TEST_CASE(JudgeTestBasesChain) {
    // A non-derived type reports an empty (null) base chain.
    const rtti::type_desc* bd = rtti::type_of<rtti_test_detail::JudgeBase>();
    RTTI_CHECK(bd->bases() == nullptr);

    // A derived type records its direct base through lazy resolvers.
    const rtti::type_desc* dd = rtti::type_of<rtti_test_detail::JudgeDerived>();
    RTTI_CHECK(dd != nullptr);
    RTTI_CHECK(dd->bases() != nullptr);
    RTTI_CHECK_EQ(dd->bases()->count, size_t(1));
    const rtti::type_desc* base_desc = dd->bases()->resolve[0]();
    RTTI_CHECK(base_desc != nullptr);
    RTTI_CHECK(base_desc->id != nullptr);
    RTTI_CHECK(base_desc->id == &typeid(rtti_test_detail::JudgeBase));

    // Multi-level: the deep type's direct base is JudgeDerived.
    const rtti::type_desc* dpd = rtti::type_of<rtti_test_detail::JudgeDeep>();
    RTTI_CHECK(dpd->bases() != nullptr);
    RTTI_CHECK_EQ(dpd->bases()->count, size_t(1));
    RTTI_CHECK(dpd->bases()->resolve[0]() != nullptr);
    RTTI_CHECK(dpd->bases()->resolve[0]()->id == &typeid(rtti_test_detail::JudgeDerived));

    // Virtual hierarchy records the virtual base in the chain.
    const rtti::type_desc* vdd = rtti::type_of<rtti_test_detail::JudgeVDerived>();
    RTTI_CHECK(vdd->bases() != nullptr);
    RTTI_CHECK_EQ(vdd->bases()->count, size_t(1));
    RTTI_CHECK(vdd->bases()->resolve[0]() != nullptr);
    RTTI_CHECK(vdd->bases()->resolve[0]()->id == &typeid(rtti_test_detail::JudgeVBase));
}

// --- Runtime is_instance_of (polymorphic objects) ---------------------------

RTTI_TEST_CASE(JudgeTestInstanceOfPolymorphic) {
    rtti_test_detail::JudgePolyD d;
    rtti_test_detail::JudgePoly p;
    rtti_test_detail::JudgeBase b;

    // Through a polymorphic static type, typeid(*p) yields the dynamic type.
    RTTI_CHECK(rtti::is_instance_of<rtti_test_detail::JudgePoly>(
        &d, &typeid(rtti_test_detail::JudgePolyD)));
    RTTI_CHECK(!rtti::is_instance_of<rtti_test_detail::JudgePoly>(
        &d, &typeid(rtti_test_detail::JudgePoly)));
    RTTI_CHECK(rtti::is_instance_of<rtti_test_detail::JudgePoly>(
        &p, &typeid(rtti_test_detail::JudgePoly)));

    // Non-polymorphic static type: comparison is static.
    RTTI_CHECK(rtti::is_instance_of<rtti_test_detail::JudgeBase>(
        &b, &typeid(rtti_test_detail::JudgeBase)));

    // Null arguments are never an instance of anything.
    RTTI_CHECK(!rtti::is_instance_of<rtti_test_detail::JudgePoly>(
        nullptr, &typeid(rtti_test_detail::JudgePoly)));
    RTTI_CHECK(!rtti::is_instance_of<rtti_test_detail::JudgePoly>(
        &d, nullptr));
}

// --- Runtime is_derived_from on instance handles ----------------------------

RTTI_TEST_CASE(JudgeTestRuntimeDerivedFrom) {
    auto r = rtti::make_object<rtti_test_detail::JudgeDerived>();
    RTTI_CHECK(r.valid());
    rtti::instance inst = rtti::instance::from(std::move(r));
    RTTI_CHECK(inst.valid());

    RTTI_CHECK(rtti::is_derived_from<rtti_test_detail::JudgeBase>(inst));
    RTTI_CHECK(rtti::is_derived_from<rtti_test_detail::JudgeDerived>(inst));
    RTTI_CHECK(!rtti::is_derived_from<rtti_test_detail::JudgeOther>(inst));
    RTTI_CHECK(!rtti::is_derived_from<rtti_test_detail::JudgeDeep>(inst));

    // Exact type equality (is<T>) vs is-a (is_derived_from).
    RTTI_CHECK(inst.is<rtti_test_detail::JudgeDerived>());
    RTTI_CHECK(!inst.is<rtti_test_detail::JudgeBase>());
    RTTI_CHECK(rtti::is<rtti_test_detail::JudgeDerived>(inst));
    RTTI_CHECK(!rtti::is<rtti_test_detail::JudgeBase>(inst));

    // Invalid handle: safe false.
    rtti::instance empty;
    RTTI_CHECK(!rtti::is_derived_from<rtti_test_detail::JudgeBase>(empty));
    RTTI_CHECK(!rtti::is<rtti_test_detail::JudgeBase>(empty));
}

RTTI_TEST_CASE(JudgeTestRuntimeDerivedFromVirtual) {
    auto r = rtti::make_object<rtti_test_detail::JudgeVDerived>();
    RTTI_CHECK(r.valid());
    rtti::instance inst = rtti::instance::from(std::move(r));
    RTTI_CHECK(inst.valid());
    RTTI_CHECK(rtti::is_derived_from<rtti_test_detail::JudgeVBase>(inst));
    RTTI_CHECK(rtti::is_derived_from<rtti_test_detail::JudgeVDerived>(inst));
}

// --- dynamic_cast_ref -------------------------------------------------------

RTTI_TEST_CASE(JudgeTestDynamicCastRefSuccess) {
    auto r = rtti::make_object<rtti_test_detail::JudgeDerived>();
    RTTI_CHECK(r.valid());
    r->x = 41;
    r->y = 42;
    rtti::instance inst = rtti::instance::from(std::move(r));
    RTTI_CHECK(inst.valid());

    // Upcast to the base through the type-id chain: valid view of the object.
    auto up = rtti::dynamic_cast_ref<rtti_test_detail::JudgeBase>(inst);
    RTTI_CHECK(up.ok());
    RTTI_CHECK(up.value != nullptr);
    RTTI_CHECK_EQ(up.value->x, 41);

    // Same-type cast succeeds too.
    auto same = rtti::dynamic_cast_ref<rtti_test_detail::JudgeDerived>(inst);
    RTTI_CHECK(same.ok());
    RTTI_CHECK(same.value != nullptr);
    RTTI_CHECK_EQ(same.value->y, 42);

    // A type that is not an ancestor fails safely.
    auto other = rtti::dynamic_cast_ref<rtti_test_detail::JudgeOther>(inst);
    RTTI_CHECK(!other.ok());
    RTTI_CHECK(other.value == nullptr);
    RTTI_CHECK(other.err.code == rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(JudgeTestDynamicCastRefFailure) {
    // A base instance cannot be viewed as its derived type.
    auto r = rtti::make_object<rtti_test_detail::JudgeBase>();
    RTTI_CHECK(r.valid());
    rtti::instance inst = rtti::instance::from(std::move(r));
    RTTI_CHECK(inst.valid());

    auto down = rtti::dynamic_cast_ref<rtti_test_detail::JudgeDerived>(inst);
    RTTI_CHECK(!down.ok());
    RTTI_CHECK(down.value == nullptr);
    RTTI_CHECK(down.err.code == rtti::error_code::kind_mismatch);

    auto unrelated = rtti::dynamic_cast_ref<rtti_test_detail::JudgeOther>(inst);
    RTTI_CHECK(!unrelated.ok());
    RTTI_CHECK(unrelated.value == nullptr);

    // Invalid handle reports no_such_field and never touches memory.
    rtti::instance empty;
    auto invalid = rtti::dynamic_cast_ref<rtti_test_detail::JudgeBase>(empty);
    RTTI_CHECK(!invalid.ok());
    RTTI_CHECK(invalid.value == nullptr);
    RTTI_CHECK(invalid.err.code == rtti::error_code::no_such_field);
}

// R2-4: KNOWN LIMITATION -- dynamic_cast_ref implements an offset-0 view
// (static_cast<T*>(inst.data())). For a VIRTUAL base target the real base
// subobject lives at a compiler-chosen offset, so the returned pointer is NOT
// the base's address. The runtime descriptor records only type ids (no base
// offset / virtual-ness) and `instance` keeps no typed pointer that would let
// the library perform a real dynamic_cast, so general detection is not
// possible from this API surface. This test PINS the current behavior (the
// cast "succeeds" and returns the UN-adjusted derived address) and verifies it
// is distinguishable from the correct virtual-base subobject -- callers must
// not dereference the borrowed view for virtual bases. Never dereferenced
// here; address comparison only (spec §0: no crash).
RTTI_TEST_CASE(JudgeTestDynamicCastRefVirtualBaseKnownLimit) {
    auto r = rtti::make_object<rtti_test_detail::JudgeVDerived>();
    RTTI_CHECK(r.valid());
    r->vx = 77;
    r->vy = 88;
    rtti::instance inst = rtti::instance::from(std::move(r));
    RTTI_CHECK(inst.valid());

    // The same object behind the instance, typed, so the test can compute the
    // compiler-correct virtual base subobject address (no dereference).
    auto* d = static_cast<rtti_test_detail::JudgeVDerived*>(inst.data());
    RTTI_CHECK(d != nullptr);
    void* correct_vbase = static_cast<rtti_test_detail::JudgeVBase*>(d);

    auto hit = rtti::dynamic_cast_ref<rtti_test_detail::JudgeVBase>(inst);
    // Chain check passes (is_derived_from reports true for virtual bases), so
    // the library returns its offset-0 view: the DERIVED address, not the
    // virtual base subobject.
    RTTI_CHECK(hit.ok());
    RTTI_CHECK(hit.value ==
               static_cast<rtti_test_detail::JudgeVBase*>(inst.data()));
    RTTI_CHECK(hit.value != correct_vbase);

    // Documented limitation: for virtual bases this view aliases the wrong
    // memory. The library cannot produce a correct virtual-base pointer from
    // its type-id-only chain; RTTI_DEFINE_TYPE_DERIVED + a virtual base is an
    // unsupported combination for dynamic_cast_ref (see reflect.hpp).
}

// --- can_convert ------------------------------------------------------------

RTTI_TEST_CASE(JudgeTestCanConvertInstance) {
    auto r = rtti::make_object<rtti_test_detail::JudgeDerived>();
    RTTI_CHECK(r.valid());
    rtti::instance inst = rtti::instance::from(std::move(r));
    RTTI_CHECK(inst.valid());

    // Same type and base-of-target convert.
    RTTI_CHECK(rtti::can_convert(inst, rtti::type_of<rtti_test_detail::JudgeDerived>()));
    RTTI_CHECK(rtti::can_convert(inst, rtti::type_of<rtti_test_detail::JudgeBase>()));
    // Unrelated and scalar targets do not.
    RTTI_CHECK(!rtti::can_convert(inst, rtti::type_of<rtti_test_detail::JudgeOther>()));
    RTTI_CHECK(!rtti::can_convert(inst, rtti::type_of<int>()));
    RTTI_CHECK(!rtti::can_convert(inst, nullptr));

    // Invalid handle: safe false.
    rtti::instance empty;
    RTTI_CHECK(!rtti::can_convert(empty, rtti::type_of<rtti_test_detail::JudgeBase>()));
}

RTTI_TEST_CASE(JudgeTestCanConvertScalars) {
    // type_desc-level convertibility: same type, implicit scalar widening and
    // the reflected hierarchy.
    RTTI_CHECK(rtti::can_convert(rtti::type_of<int>(), rtti::type_of<int>()));
    RTTI_CHECK(rtti::can_convert(rtti::type_of<bool>(), rtti::type_of<int>()));
    RTTI_CHECK(rtti::can_convert(rtti::type_of<int>(), rtti::type_of<double>()));
    RTTI_CHECK(rtti::can_convert(rtti::type_of<bool>(), rtti::type_of<double>()));
    RTTI_CHECK(!rtti::can_convert(rtti::type_of<double>(), rtti::type_of<int>()));
    RTTI_CHECK(!rtti::can_convert(rtti::type_of<int>(), rtti::type_of<std::string>()));
    RTTI_CHECK(!rtti::can_convert(rtti::type_of<int>(), nullptr));
    RTTI_CHECK(!rtti::can_convert(nullptr, rtti::type_of<int>()));

    // Reflected hierarchy: derived -> base converts, base -> derived does not.
    RTTI_CHECK(rtti::can_convert(rtti::type_of<rtti_test_detail::JudgeDerived>(),
                                 rtti::type_of<rtti_test_detail::JudgeBase>()));
    RTTI_CHECK(!rtti::can_convert(rtti::type_of<rtti_test_detail::JudgeBase>(),
                                  rtti::type_of<rtti_test_detail::JudgeDerived>()));
    RTTI_CHECK(rtti::can_convert(rtti::type_of<rtti_test_detail::JudgeDeep>(),
                                 rtti::type_of<rtti_test_detail::JudgeBase>()));
}

// --- Error paths never crash ------------------------------------------------

RTTI_TEST_CASE(JudgeTestErrorPathsNoCrash) {
    rtti_test_detail::JudgeOther other;
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::JudgeOther>(), size_t(1));
    rtti::field_value fv;
    RTTI_CHECK(rtti::get_field(other, "w", fv));
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(0));
    RTTI_CHECK(!rtti::get_field(other, "x", fv));
    RTTI_CHECK(!rtti::set_field(other, "x", rtti::field_value::of(int64_t(1))));

    // instance::get/set on an invalid handle fail cleanly.
    rtti::instance empty;
    RTTI_CHECK_EQ(empty.get("w", fv), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(empty.set("w", fv), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(empty.to_value(nullptr), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(empty.from_value(nullptr), rtti::error_code::no_such_field);
    RTTI_CHECK(!empty.valid());
}

RTTI_TEST_CASE(JudgeTestBaseAdjustThunk) {
    using namespace rtti_test_detail;
    // Non-virtual single inheritance: D -> Base offset thunk returns a usable
    // base pointer whose merged field is readable.
    JudgeDerived d;
    d.x = 7;
    d.y = 9;
    const rtti::type_desc* dd = rtti::type_of<JudgeDerived>();
    RTTI_CHECK(dd->bases() != nullptr && dd->bases()->count >= 1);
    RTTI_CHECK(dd->bases()->adjust != nullptr);
    void* bp = dd->bases()->adjust[0](static_cast<void*>(&d));
    RTTI_CHECK(bp != nullptr);
    // The base subobject pointer must alias the base part of d (x == 7).
    RTTI_CHECK(static_cast<JudgeBase*>(bp)->x == 7);
}

RTTI_TEST_CASE(JudgeTestAsCastRawPointer) {
    using namespace rtti_test_detail;
    JudgeDerived d;
    d.x = 3;
    d.y = 4;

    // Covariant upcast (derived -> base): always safe.
    JudgeBase* b = rtti::as<JudgeBase>(&d);
    RTTI_CHECK(b != nullptr && b->x == 3);

    // Exact type: identity view.
    JudgeDerived* same = rtti::as<JudgeDerived>(&d);
    RTTI_CHECK(same == &d);

    // Downcast to a type the object is not: clean failure.
    RTTI_CHECK(rtti::as<JudgeOther>(&d) == nullptr);
    auto bad = rtti::cast<JudgeOther>(&d);
    RTTI_CHECK(!bad.ok() && bad.err.code == rtti::error_code::kind_mismatch);

    // Null source.
    JudgeDerived* nullp = nullptr;
    RTTI_CHECK(rtti::as<JudgeBase>(nullp) == nullptr);
    auto nul = rtti::cast<JudgeBase>(nullp);
    RTTI_CHECK(!nul.ok() && nul.err.code == rtti::error_code::no_such_field);

    // Successful cast returns ok with the value set.
    auto up = rtti::cast<JudgeBase>(&d);
    RTTI_CHECK(up.ok() && up.value == b);
}

RTTI_TEST_CASE(JudgeTestAsCastObjectRef) {
    using namespace rtti_test_detail;
    auto h = rtti::make_object<JudgeDerived>();
    RTTI_CHECK(h.valid());
    h->x = 11;
    JudgeBase* b = h.as<JudgeBase>();
    RTTI_CHECK(b != nullptr && b->x == 11);
    RTTI_CHECK(h.as<JudgeOther>() == nullptr);
    auto c = h.cast<JudgeBase>();
    RTTI_CHECK(c.ok() && c.value == b);

    rtti::object_ref<JudgeDerived> empty;
    RTTI_CHECK(empty.as<JudgeBase>() == nullptr);
    RTTI_CHECK(empty.cast<JudgeBase>().err.code == rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(JudgeTestAsCastPolymorphic) {
    using namespace rtti_test_detail;

    // Polymorphic downcast hit: dynamic_cast succeeds on the exact dynamic type.
    JudgePolyD pd;
    pd.pd = 42;
    JudgePoly* pbase = &pd;
    JudgePolyD* hit = rtti::as<JudgePolyD>(pbase);
    RTTI_CHECK(hit != nullptr && hit->pd == 42);
    RTTI_CHECK(rtti::cast<JudgePolyD>(pbase).ok());

    // Polymorphic downcast miss: dynamic type is the base, not the derived.
    JudgePoly base_obj;
    RTTI_CHECK(rtti::as<JudgePolyD>(&base_obj) == nullptr);
    RTTI_CHECK(rtti::cast<JudgePolyD>(&base_obj).err.code == rtti::error_code::kind_mismatch);

    // Unrelated polymorphic cross-cast: Right* -> Left* succeeds via dynamic_cast.
    struct Left  { virtual ~Left() {} };
    struct Right { virtual ~Right() {} };
    struct Both  : Left, Right { int tag = 0; };
    Both both;
    Right* right = &both;
    Left* left = rtti::as<Left>(right);
    RTTI_CHECK(left != nullptr);
    RTTI_CHECK(rtti::as<Right>(left) == right);

    // Multiple inheritance upcast to a non-first base: offset must be correct.
    struct A { int a = 0; };
    struct B { int b = 0; };
    struct AB : A, B { int c = 0; };
    AB ab;
    ab.a = 1; ab.b = 2; ab.c = 3;
    B* pb = rtti::as<B>(&ab);
    RTTI_CHECK(pb != nullptr && pb->b == 2);
}

RTTI_TEST_CASE(JudgeTestInstanceAsCast) {
    using namespace rtti_test_detail;
    // Force the derived descriptors to be materialized/registered.
    (void)rtti::type_of<JudgeDerived>();
    (void)rtti::type_of<JudgeDeep>();

    auto h = rtti::make_object<JudgeDeep>();
    h->x = 1;
    h->y = 2;
    h->z = 3;
    rtti::instance inst = rtti::instance::from(std::move(h));

    // Upcast through two levels (Deep -> Derived -> Base) with correct offsets.
    JudgeBase* b = inst.as<JudgeBase>();
    RTTI_CHECK(b != nullptr && b->x == 1);
    JudgeDerived* d = inst.as<JudgeDerived>();
    RTTI_CHECK(d != nullptr && d->y == 2);
    // Exact dynamic type.
    RTTI_CHECK(inst.as<JudgeDeep>() != nullptr);
    // Unrelated: clean failure.
    RTTI_CHECK(inst.as<JudgeOther>() == nullptr);
    RTTI_CHECK(inst.cast<JudgeOther>().err.code == rtti::error_code::kind_mismatch);
    // Successful cast.
    auto c = inst.cast<JudgeDerived>();
    RTTI_CHECK(c.ok() && c.value == d);

    // Empty handle.
    rtti::instance empty;
    RTTI_CHECK(empty.as<JudgeBase>() == nullptr);
    RTTI_CHECK(empty.cast<JudgeBase>().err.code == rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(JudgeTestInstanceDowncastPolymorphic) {
    using namespace rtti_test_detail;
    (void)rtti::type_of<JudgePolyD>();
    // Store a derived object behind a base handle, then recover the derived
    // view through the type-erased instance (downcast).
    rtti::object_ref<JudgePoly> base_handle = rtti::object_ref<JudgePoly>::adopt(new JudgePolyD());
    JudgePolyD* raw = dynamic_cast<JudgePolyD*>(base_handle.get());
    RTTI_CHECK(raw != nullptr);
    raw->pd = 42;
    rtti::instance inst = rtti::instance::from(std::move(base_handle));
    JudgePolyD* rec = inst.as<JudgePolyD>();
    RTTI_CHECK(rec != nullptr && rec->pd == 42);
}

} // namespace rtti_test_lib
