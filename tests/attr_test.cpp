// Attribute/annotation reflection tests. Task L of the RTTI perf-usability
// plan (spec section 17). Covers type-level and field-level attribute
// declaration + query, polymorphic attribute arguments, safe empty tables,
// runtime attribute construction, and coexistence with the existing field
// table/iteration machinery. Core-only: no format adapter headers.

#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <cstdint>
#include <string>

// Included after reflection: provides the format-agnostic scalar converter
// definitions (to_value/from_value) that typed_field_access virtuals call,
// so the test links without any format adapter dependency.
#include <rtti/convert.hpp>

namespace rtti_test_detail {

// Type with type-level attributes only.
struct AttrUser {
    int id = 0;
    std::string name;
    double score = 0.0;
};

// Type with field-level attributes only (no type-level attributes).
struct AttrRecord {
    int a = 0;
    int b = 0;
};

// Type with both type-level and field-level attributes.
struct AttrPoint {
    int x = 0;
    int y = 0;
};

// Type registered with empty attribute declarations (safe empty tables).
struct AttrEmpty {
    int v = 0;
};

// Type with polymorphic (mixed-kind) attribute arguments.
struct AttrMix {
    int v = 0;
};

// Type reflected for fields but never registered for attributes.
struct AttrPlain {
    int v = 0;
};

} // namespace rtti_test_detail

// Register the field tables (existing macro family) so the coexistence
// assertions can exercise field_count/fields on the attribute-decorated types.
RTTI_DEFINE_TYPE(rtti_test_detail::AttrUser, id, name, score)
RTTI_DEFINE_TYPE(rtti_test_detail::AttrRecord, a, b)
RTTI_DEFINE_TYPE(rtti_test_detail::AttrPoint, x, y)
RTTI_DEFINE_TYPE(rtti_test_detail::AttrEmpty, v)
RTTI_DEFINE_TYPE(rtti_test_detail::AttrMix, v)
RTTI_DEFINE_TYPE(rtti_test_detail::AttrPlain, v)

// Type-level attribute declarations. Plain no-arg markers, string/int/real/
// bool arguments, and a multi-argument attribute all through one macro family.
RTTI_DEFINE_TYPE_ATTRS(rtti_test_detail::AttrUser,
    RTTI_ATTR("Serializable"),
    RTTI_ATTR("TableName", "users"),
    RTTI_ATTR("MaxLength", 128),
    RTTI_ATTR("Range", 1, 100))

RTTI_DEFINE_TYPE_ATTRS(rtti_test_detail::AttrPoint,
    RTTI_ATTR("Serializable"),
    RTTI_ATTR("Label", "point_label"),
    RTTI_ATTR("Bounds", 1.5, 9.75),
    RTTI_ATTR("Flags", 0x10),
    RTTI_ATTR("Important", true))

RTTI_DEFINE_TYPE_ATTRS(rtti_test_detail::AttrMix,
    RTTI_ATTR("Value", 42u),
    RTTI_ATTR("Ratio", 0.25),
    RTTI_ATTR("Tag", "mix_tag"),
    RTTI_ATTR("Active", false),
    RTTI_ATTR("Everything", 7, 3.14, "mixed", true))

// Empty registration: declaring the table with no entries stays safe.
RTTI_DEFINE_TYPE_ATTRS(rtti_test_detail::AttrEmpty)

// Field-level attribute declarations: one entry per decorated field; fields
// without a declaration simply return an empty list.
RTTI_DEFINE_FIELD_ATTRS(rtti_test_detail::AttrPoint,
    RTTI_FIELD_ATTR_ENTRY(x, RTTI_ATTR("ColumnName", "pos_x"), RTTI_ATTR("Key")),
    RTTI_FIELD_ATTR_ENTRY(y, RTTI_ATTR("MaxLength", 32)))

RTTI_DEFINE_FIELD_ATTRS(rtti_test_detail::AttrRecord,
    RTTI_FIELD_ATTR_ENTRY(a, RTTI_ATTR("ColumnName", "col_a")))

RTTI_DEFINE_FIELD_ATTRS(rtti_test_detail::AttrEmpty,
    RTTI_FIELD_ATTR_ENTRY(v))

RTTI_TEST_CASE(AttrArgFactories) {
    RTTI_CHECK(rtti::attr_arg().type() == rtti::attr_arg::kind::none);
    RTTI_CHECK(rtti::attr_arg::integer(std::uint64_t(3)).type() == rtti::attr_arg::kind::integer);
    RTTI_CHECK(rtti::attr_arg::real(1.5).type() == rtti::attr_arg::kind::real);
    RTTI_CHECK(rtti::attr_arg::text("s").type() == rtti::attr_arg::kind::string);
    RTTI_CHECK(rtti::attr_arg::boolean(true).type() == rtti::attr_arg::kind::boolean);
    RTTI_CHECK_EQ(rtti::attr_arg::integer(std::uint64_t(9)).as_integer(), std::uint64_t(9));
    RTTI_CHECK_EQ(rtti::attr_arg::real(2.5).as_real(), 2.5);
    RTTI_CHECK_EQ(rtti::attr_arg::text("hi").as_string(), rtti::string_view("hi"));
    RTTI_CHECK(rtti::attr_arg::boolean(true).as_bool());
    RTTI_CHECK(!rtti::attr_arg().as_bool());
    RTTI_CHECK_EQ(rtti::attr_arg().as_string(), rtti::string_view());
    RTTI_CHECK_EQ(rtti::attr_arg().as_integer(), std::uint64_t(0));
}

RTTI_TEST_CASE(AttrTypeLevelDeclareAndQuery) {
    RTTI_CHECK_EQ(rtti::attr_count<rtti_test_detail::AttrUser>(), std::size_t(4));
    const rtti::attr* a0 = rtti::attr_at<rtti_test_detail::AttrUser>(0);
    RTTI_CHECK(a0 != nullptr);
    RTTI_CHECK_EQ(std::string(a0->name), std::string("Serializable"));
    RTTI_CHECK_EQ(a0->args.size(), std::size_t(0));
    const rtti::attr* a1 = rtti::attr_at<rtti_test_detail::AttrUser>(1);
    RTTI_CHECK(a1 != nullptr);
    RTTI_CHECK_EQ(std::string(a1->name), std::string("TableName"));
    RTTI_CHECK_EQ(a1->args.size(), std::size_t(1));
    RTTI_CHECK_EQ(a1->args[0].as_string(), rtti::string_view("users"));
    const rtti::attr* a2 = rtti::attr_at<rtti_test_detail::AttrUser>(2);
    RTTI_CHECK(a2 != nullptr);
    RTTI_CHECK_EQ(std::string(a2->name), std::string("MaxLength"));
    RTTI_CHECK_EQ(a2->args[0].as_integer(), std::uint64_t(128));
    const rtti::attr* a3 = rtti::attr_at<rtti_test_detail::AttrUser>(3);
    RTTI_CHECK(a3 != nullptr);
    RTTI_CHECK_EQ(std::string(a3->name), std::string("Range"));
    RTTI_CHECK_EQ(a3->args.size(), std::size_t(2));
    RTTI_CHECK_EQ(a3->args[0].as_integer(), std::uint64_t(1));
    RTTI_CHECK_EQ(a3->args[1].as_integer(), std::uint64_t(100));
    // Out-of-range accesses return null, never UB.
    RTTI_CHECK(rtti::attr_at<rtti_test_detail::AttrUser>(4) == nullptr);
    RTTI_CHECK(rtti::attr_at<rtti_test_detail::AttrUser>(999) == nullptr);
}

RTTI_TEST_CASE(AttrFindHas) {
    const rtti::attr* t = rtti::find_attr<rtti_test_detail::AttrUser>("TableName");
    RTTI_CHECK(t != nullptr);
    RTTI_CHECK_EQ(std::string(t->name), std::string("TableName"));
    RTTI_CHECK_EQ(t->args.size(), std::size_t(1));
    RTTI_CHECK_EQ(t->args[0].as_string(), rtti::string_view("users"));
    RTTI_CHECK(rtti::find_attr<rtti_test_detail::AttrUser>("Serializable") != nullptr);
    RTTI_CHECK(rtti::find_attr<rtti_test_detail::AttrUser>("Nope") == nullptr);
    RTTI_CHECK(rtti::has_attr<rtti_test_detail::AttrUser>("Serializable"));
    RTTI_CHECK(rtti::has_attr<rtti_test_detail::AttrUser>("Range"));
    RTTI_CHECK(!rtti::has_attr<rtti_test_detail::AttrUser>("Nope"));
}

RTTI_TEST_CASE(AttrArgKinds) {
    const rtti::attr* value = rtti::find_attr<rtti_test_detail::AttrMix>("Value");
    RTTI_CHECK(value != nullptr);
    RTTI_CHECK_EQ(value->args[0].type(), rtti::attr_arg::kind::integer);
    RTTI_CHECK_EQ(value->args[0].as_integer(), std::uint64_t(42));
    const rtti::attr* ratio = rtti::find_attr<rtti_test_detail::AttrMix>("Ratio");
    RTTI_CHECK(ratio != nullptr);
    RTTI_CHECK_EQ(ratio->args[0].type(), rtti::attr_arg::kind::real);
    RTTI_CHECK_EQ(ratio->args[0].as_real(), 0.25);
    const rtti::attr* tag = rtti::find_attr<rtti_test_detail::AttrMix>("Tag");
    RTTI_CHECK(tag != nullptr);
    RTTI_CHECK_EQ(tag->args[0].type(), rtti::attr_arg::kind::string);
    RTTI_CHECK_EQ(tag->args[0].as_string(), rtti::string_view("mix_tag"));
    const rtti::attr* active = rtti::find_attr<rtti_test_detail::AttrMix>("Active");
    RTTI_CHECK(active != nullptr);
    RTTI_CHECK_EQ(active->args[0].type(), rtti::attr_arg::kind::boolean);
    RTTI_CHECK(!active->args[0].as_bool());
    // One attribute carrying arguments of all four kinds.
    const rtti::attr* everything = rtti::find_attr<rtti_test_detail::AttrMix>("Everything");
    RTTI_CHECK(everything != nullptr);
    RTTI_CHECK_EQ(everything->args.size(), std::size_t(4));
    RTTI_CHECK_EQ(everything->args[0].type(), rtti::attr_arg::kind::integer);
    RTTI_CHECK_EQ(everything->args[0].as_integer(), std::uint64_t(7));
    RTTI_CHECK_EQ(everything->args[1].type(), rtti::attr_arg::kind::real);
    RTTI_CHECK_EQ(everything->args[1].as_real(), 3.14);
    RTTI_CHECK_EQ(everything->args[2].type(), rtti::attr_arg::kind::string);
    RTTI_CHECK_EQ(everything->args[2].as_string(), rtti::string_view("mixed"));
    RTTI_CHECK_EQ(everything->args[3].type(), rtti::attr_arg::kind::boolean);
    RTTI_CHECK(everything->args[3].as_bool());
    // Fallback accessors return zero/empty values on kind mismatch.
    RTTI_CHECK_EQ(value->args[0].as_real(), 0.0);
    RTTI_CHECK_EQ(value->args[0].as_string(), rtti::string_view());
}

RTTI_TEST_CASE(AttrFieldLevelQuery) {
    // By field name.
    rtti::field_attr_list lx = rtti::field_attr<rtti_test_detail::AttrPoint>("x");
    RTTI_CHECK_EQ(lx.count, std::size_t(2));
    RTTI_CHECK(lx.items != nullptr);
    RTTI_CHECK_EQ(std::string(lx.items[0].name), std::string("ColumnName"));
    RTTI_CHECK_EQ(lx.items[0].args.size(), std::size_t(1));
    RTTI_CHECK_EQ(lx.items[0].args[0].as_string(), rtti::string_view("pos_x"));
    RTTI_CHECK_EQ(std::string(lx.items[1].name), std::string("Key"));
    RTTI_CHECK_EQ(lx.items[1].args.size(), std::size_t(0));
    // By field index (resolved through the field table names).
    rtti::field_attr_list l0 = rtti::field_attr<rtti_test_detail::AttrPoint>(std::size_t(0));
    RTTI_CHECK_EQ(l0.count, std::size_t(2));
    RTTI_CHECK_EQ(std::string(l0.items[0].name), std::string("ColumnName"));
    rtti::field_attr_list l1 = rtti::field_attr<rtti_test_detail::AttrPoint>(std::size_t(1));
    RTTI_CHECK_EQ(l1.count, std::size_t(1));
    RTTI_CHECK_EQ(std::string(l1.items[0].name), std::string("MaxLength"));
    RTTI_CHECK_EQ(l1.items[0].args[0].as_integer(), std::uint64_t(32));
    // Missing field / out-of-range index / undecorated field stay empty.
    RTTI_CHECK_EQ(rtti::field_attr<rtti_test_detail::AttrPoint>("nope").count, std::size_t(0));
    RTTI_CHECK_EQ(rtti::field_attr<rtti_test_detail::AttrPoint>(std::size_t(5)).count, std::size_t(0));
    // Count / at / find / has helpers.
    RTTI_CHECK_EQ(rtti::field_attr_count<rtti_test_detail::AttrPoint>("y"), std::size_t(1));
    RTTI_CHECK_EQ(rtti::field_attr_count<rtti_test_detail::AttrPoint>(std::size_t(1)), std::size_t(1));
    RTTI_CHECK_EQ(rtti::field_attr_count<rtti_test_detail::AttrPoint>("nope"), std::size_t(0));
    const rtti::attr* maxy = rtti::field_attr_at<rtti_test_detail::AttrPoint>("y", std::size_t(0));
    RTTI_CHECK(maxy != nullptr);
    RTTI_CHECK_EQ(std::string(maxy->name), std::string("MaxLength"));
    RTTI_CHECK_EQ(maxy->args[0].as_integer(), std::uint64_t(32));
    RTTI_CHECK(rtti::field_attr_at<rtti_test_detail::AttrPoint>("y", std::size_t(1)) == nullptr);
    RTTI_CHECK(rtti::field_attr_at<rtti_test_detail::AttrPoint>("nope", std::size_t(0)) == nullptr);
    RTTI_CHECK(rtti::field_find_attr<rtti_test_detail::AttrPoint>(std::size_t(0), "ColumnName") != nullptr);
    RTTI_CHECK(rtti::field_find_attr<rtti_test_detail::AttrPoint>("x", "Key") != nullptr);
    RTTI_CHECK(rtti::field_find_attr<rtti_test_detail::AttrPoint>("x", "Missing") == nullptr);
    RTTI_CHECK(rtti::field_has_attr<rtti_test_detail::AttrPoint>("x", "Key"));
    RTTI_CHECK(rtti::field_has_attr<rtti_test_detail::AttrPoint>(std::size_t(0), "ColumnName"));
    RTTI_CHECK(!rtti::field_has_attr<rtti_test_detail::AttrPoint>("x", "Missing"));
    RTTI_CHECK(!rtti::field_has_attr<rtti_test_detail::AttrPoint>("nope", "Key"));
}

RTTI_TEST_CASE(AttrFieldOnlyType) {
    // Type-level queries are empty; field-level still resolve.
    RTTI_CHECK_EQ(rtti::attr_count<rtti_test_detail::AttrRecord>(), std::size_t(0));
    RTTI_CHECK(rtti::attr_at<rtti_test_detail::AttrRecord>(0) == nullptr);
    RTTI_CHECK(!rtti::has_attr<rtti_test_detail::AttrRecord>("ColumnName"));
    RTTI_CHECK(rtti::attrs_defined<rtti_test_detail::AttrRecord>());
    RTTI_CHECK_EQ(rtti::field_attr_count<rtti_test_detail::AttrRecord>("a"), std::size_t(1));
    RTTI_CHECK(rtti::field_has_attr<rtti_test_detail::AttrRecord>("a", "ColumnName"));
    RTTI_CHECK_EQ(rtti::field_attr_count<rtti_test_detail::AttrRecord>("b"), std::size_t(0));
}

RTTI_TEST_CASE(AttrEmptySafe) {
    // A type never registered for attributes: type-safe empty tables.
    RTTI_CHECK_EQ(rtti::attr_count<rtti_test_detail::AttrPlain>(), std::size_t(0));
    RTTI_CHECK(rtti::attr_at<rtti_test_detail::AttrPlain>(0) == nullptr);
    RTTI_CHECK(rtti::attr_at<rtti_test_detail::AttrPlain>(3) == nullptr);
    RTTI_CHECK(rtti::find_attr<rtti_test_detail::AttrPlain>("X") == nullptr);
    RTTI_CHECK(!rtti::has_attr<rtti_test_detail::AttrPlain>("X"));
    RTTI_CHECK_EQ(rtti::field_attr_count<rtti_test_detail::AttrPlain>("v"), std::size_t(0));
    RTTI_CHECK_EQ(rtti::field_attr<rtti_test_detail::AttrPlain>("v").count, std::size_t(0));
    RTTI_CHECK(rtti::field_attr_at<rtti_test_detail::AttrPlain>("v", std::size_t(0)) == nullptr);
    RTTI_CHECK(!rtti::field_has_attr<rtti_test_detail::AttrPlain>("v", "X"));
    RTTI_CHECK(!rtti::attrs_defined<rtti_test_detail::AttrPlain>());
    // A type registered with empty declarations: still safe, but "defined".
    RTTI_CHECK_EQ(rtti::attr_count<rtti_test_detail::AttrEmpty>(), std::size_t(0));
    RTTI_CHECK(rtti::attr_at<rtti_test_detail::AttrEmpty>(0) == nullptr);
    RTTI_CHECK(!rtti::has_attr<rtti_test_detail::AttrEmpty>("v"));
    RTTI_CHECK_EQ(rtti::field_attr_count<rtti_test_detail::AttrEmpty>("v"), std::size_t(0));
    RTTI_CHECK(rtti::field_attr_at<rtti_test_detail::AttrEmpty>("v", std::size_t(0)) == nullptr);
    RTTI_CHECK(rtti::attrs_defined<rtti_test_detail::AttrEmpty>());
}

RTTI_TEST_CASE(AttrRuntimeBuild) {
    // Runtime attribute construction via the public factories.
    rtti::attr built = rtti::make_attr("Dynamic", 5, 2.5, "sv", false);
    RTTI_CHECK_EQ(std::string(built.name), std::string("Dynamic"));
    RTTI_CHECK_EQ(built.args.size(), std::size_t(4));
    RTTI_CHECK_EQ(built.args[0].as_integer(), std::uint64_t(5));
    RTTI_CHECK_EQ(built.args[1].as_real(), 2.5);
    RTTI_CHECK_EQ(built.args[2].as_string(), rtti::string_view("sv"));
    RTTI_CHECK(!built.args[3].as_bool());
    rtti::field_attr_entry fe = rtti::make_field_attr("f", built);
    RTTI_CHECK_EQ(std::string(fe.field), std::string("f"));
    RTTI_CHECK_EQ(fe.attrs.size(), std::size_t(1));
    RTTI_CHECK_EQ(std::string(fe.attrs[0].name), std::string("Dynamic"));
}

RTTI_TEST_CASE(AttrCoexistWithFieldTable) {
    // Field table stays fully functional on an attribute-decorated type.
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::AttrPoint>(), std::size_t(2));
    const auto* f = rtti::fields<rtti_test_detail::AttrPoint>();
    RTTI_CHECK_EQ(std::string(f[0]->name), std::string("x"));
    RTTI_CHECK_EQ(std::string(f[1]->name), std::string("y"));
    rtti_test_detail::AttrPoint p;
    p.x = 7;
    p.y = 9;
    RTTI_CHECK(f[0]->set(&p, rtti::field_value::of(int64_t(11))));
    RTTI_CHECK_EQ(p.x, 11);
    RTTI_CHECK_EQ(f[1]->get(&p).as_integer(), int64_t(9));
    // Field attribute lookup composes with the field table by name/index.
    for (std::size_t i = 0; i < rtti::field_count<rtti_test_detail::AttrPoint>(); ++i) {
        const char* fname = rtti::fields<rtti_test_detail::AttrPoint>()[i]->name;
        if (std::string(fname) == "x") {
            RTTI_CHECK(rtti::field_has_attr<rtti_test_detail::AttrPoint>(i, "Key"));
        } else {
            RTTI_CHECK(rtti::field_has_attr<rtti_test_detail::AttrPoint>(i, "MaxLength"));
        }
    }
    // type_of still classifies it as a reflected struct.
    const rtti::type_desc* td = rtti::type_of<rtti_test_detail::AttrPoint>();
    RTTI_CHECK(td != nullptr);
    RTTI_CHECK(td->kind == rtti::type_kind::reflected);
    RTTI_CHECK_EQ(td->as_reflected->field_count, std::size_t(2));
    // The plain (never attribute-registered) type still iterates fine too.
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_detail::AttrPlain>(), std::size_t(1));
}
