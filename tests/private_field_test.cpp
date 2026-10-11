// Private/protected field reflection + member-access-level tests.
// Covers RTTI_FRIEND_FIELDS, member_access, field_type_desc::access and
// field_access_of<T>(index).

#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <string>

namespace rtti_test_detail {

struct PrivNonIntrusive {
    int pub = 1;
protected:
    int prot = 2;
private:
    int priv = 3;
    RTTI_FRIEND_FIELDS(PrivNonIntrusive)
};

// Derived registration: the base's private field is deliberately absent from
// its field list, so the merged derived table must NOT reflect it; the
// protected field IS registered and must merge with its access intact.
struct PrivBase {
    int bpub = 1;
protected:
    int bprot = 2;
private:
    [[maybe_unused]] int bpriv = 3;
    RTTI_FRIEND_FIELDS(PrivBase)
};

struct PrivDerived : PrivBase {
    int dpub = 4;
private:
    int dpriv = 5;
    RTTI_FRIEND_FIELDS(PrivDerived)
};

// Intrusive registration (in-class): private/protected fields are reflectable;
// member-access classification is impossible from inside the class body (any
// in-class probe is a member of T and sees all access levels alike), so every
// intrusive field reports private_, the conservative documented boundary.
struct PrivIntrusive {
    int pub = 1;
protected:
    int prot = 2;
private:
    int priv = 3;
public:
    RTTI_DEFINE_TYPE_INTRUSIVE(PrivIntrusive, pub, prot, priv)
};

} // namespace rtti_test_detail

RTTI_DEFINE_TYPE(rtti_test_detail::PrivNonIntrusive, pub, prot, priv)
RTTI_DEFINE_TYPE(rtti_test_detail::PrivBase, bpub, bprot)
RTTI_DEFINE_TYPE_DERIVED(rtti_test_detail::PrivDerived,
                         rtti_test_detail::PrivBase, dpub, dpriv)

RTTI_TEST_CASE(member_access_enum_values) {
    using namespace rtti;
    RTTI_CHECK_EQ(static_cast<int>(member_access::public_), 1);
    RTTI_CHECK_EQ(static_cast<int>(member_access::protected_), 2);
    RTTI_CHECK_EQ(static_cast<int>(member_access::private_), 3);
}

RTTI_TEST_CASE(non_intrusive_access_levels) {
    using namespace rtti;
    RTTI_CHECK_EQ(field_count<rtti_test_detail::PrivNonIntrusive>(),
                  std::size_t(3));
    RTTI_CHECK_EQ(field_access_of<rtti_test_detail::PrivNonIntrusive>(0),
                  member_access::public_);
    RTTI_CHECK_EQ(field_access_of<rtti_test_detail::PrivNonIntrusive>(1),
                  member_access::protected_);
    RTTI_CHECK_EQ(field_access_of<rtti_test_detail::PrivNonIntrusive>(2),
                  member_access::private_);
    RTTI_CHECK_EQ(fields_of<rtti_test_detail::PrivNonIntrusive>::desc[0].access,
                  member_access::public_);
    RTTI_CHECK_EQ(fields_of<rtti_test_detail::PrivNonIntrusive>::desc[1].access,
                  member_access::protected_);
    RTTI_CHECK_EQ(fields_of<rtti_test_detail::PrivNonIntrusive>::desc[2].access,
                  member_access::private_);
}

RTTI_TEST_CASE(non_intrusive_private_get_set) {
    using namespace rtti;
    rtti_test_detail::PrivNonIntrusive p;
    p.pub = 7;
    const auto* f = fields<rtti_test_detail::PrivNonIntrusive>();
    RTTI_CHECK_EQ(f[0]->get(&p).as_integer(), int64_t(7));
    RTTI_CHECK(f[2]->set(&p, field_value::of(int64_t(42))));
    RTTI_CHECK_EQ(f[2]->get(&p).as_integer(), int64_t(42));
    RTTI_CHECK_EQ(f[1]->get(&p).as_integer(), int64_t(2));
}

RTTI_TEST_CASE(compile_time_access_consistent) {
    static_assert(rtti::field_access_of<rtti_test_detail::PrivNonIntrusive>(0) ==
                      rtti::member_access::public_,
                  "field 0 public");
    static_assert(rtti::field_access_of<rtti_test_detail::PrivNonIntrusive>(1) ==
                      rtti::member_access::protected_,
                  "field 1 protected");
    static_assert(rtti::field_access_of<rtti_test_detail::PrivNonIntrusive>(2) ==
                      rtti::member_access::private_,
                  "field 2 private");
}

RTTI_TEST_CASE(derived_does_not_reflect_base_private) {
    using namespace rtti;
    // bpriv is absent from PrivBase's field list and must stay out of the
    // merged derived table.
    RTTI_CHECK_EQ(field_count<rtti_test_detail::PrivDerived>(), std::size_t(4));
    RTTI_CHECK_EQ(std::string(fields<rtti_test_detail::PrivDerived>()[0]->name),
                  std::string("bpub"));
    RTTI_CHECK_EQ(std::string(fields<rtti_test_detail::PrivDerived>()[1]->name),
                  std::string("bprot"));
    RTTI_CHECK_EQ(std::string(fields<rtti_test_detail::PrivDerived>()[2]->name),
                  std::string("dpub"));
    RTTI_CHECK_EQ(std::string(fields<rtti_test_detail::PrivDerived>()[3]->name),
                  std::string("dpriv"));
}

RTTI_TEST_CASE(derived_base_access_levels) {
    using namespace rtti;
    const auto* d = fields_of<rtti_test_detail::PrivDerived>::desc;
    RTTI_CHECK_EQ(d[0].access, member_access::public_);    // bpub
    RTTI_CHECK_EQ(d[1].access, member_access::protected_); // bprot merged intact
    RTTI_CHECK_EQ(d[2].access, member_access::public_);    // dpub
    RTTI_CHECK_EQ(d[3].access, member_access::private_);   // dpriv
}

RTTI_TEST_CASE(derived_get_set_through_merged_table) {
    using namespace rtti;
    rtti_test_detail::PrivDerived v;
    const auto* f = fields<rtti_test_detail::PrivDerived>();
    RTTI_CHECK_EQ(f[1]->get(&v).as_integer(), int64_t(2)); // bprot via offset
    RTTI_CHECK(f[3]->set(&v, field_value::of(int64_t(50))));
    RTTI_CHECK_EQ(f[3]->get(&v).as_integer(), int64_t(50)); // dpriv via reflection
}

RTTI_TEST_CASE(intrusive_reflection_count_and_names) {
    using namespace rtti;
    RTTI_CHECK_EQ(field_count<rtti_test_detail::PrivIntrusive>(), std::size_t(3));
    RTTI_CHECK_EQ(std::string(fields<rtti_test_detail::PrivIntrusive>()[0]->name), std::string("pub"));
    RTTI_CHECK_EQ(std::string(fields<rtti_test_detail::PrivIntrusive>()[1]->name), std::string("prot"));
    RTTI_CHECK_EQ(std::string(fields<rtti_test_detail::PrivIntrusive>()[2]->name), std::string("priv"));
}

RTTI_TEST_CASE(intrusive_get_set_on_private_field) {
    using namespace rtti;
    rtti_test_detail::PrivIntrusive v;
    const auto* f = fields<rtti_test_detail::PrivIntrusive>();
    RTTI_CHECK_EQ(f[2]->get(&v).as_integer(), int64_t(3)); // private read
    RTTI_CHECK(f[2]->set(&v, field_value::of(int64_t(99))));
    RTTI_CHECK_EQ(f[2]->get(&v).as_integer(), int64_t(99)); // private write
}

RTTI_TEST_CASE(intrusive_access_level_boundary) {
    using namespace rtti;
    const auto* d = fields_of<rtti_test_detail::PrivIntrusive>::desc;
    // Intrusive fields cannot be access-classified: an in-class probe is a
    // member of T (nested rtti_registration) and sees every access level as
    // accessible, so all intrusive fields report private_ (conservative
    // documented boundary, matching the field_type_desc::access default).
    RTTI_CHECK_EQ(d[0].access, member_access::private_);
    RTTI_CHECK_EQ(d[1].access, member_access::private_);
    RTTI_CHECK_EQ(d[2].access, member_access::private_);
}
