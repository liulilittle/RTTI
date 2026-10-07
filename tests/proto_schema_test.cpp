// .proto text-descriptor parser tests for the RTTI protobuf adapter (spec
// section 13.2). Covers the proto2/proto3 syntax subset (syntax declaration,
// message/nested-message, field labels optional/required/repeated, all scalar
// types, [packed=...]/[default=...] options, // and /* */ comments), error
// paths (unknown token, bad number, missing semicolon, unknown type,
// unterminated comment/string, malformed truncated input), packed wire
// derivation, and the runtime schema -> wire_to_tree/tree_to_wire/parse
// bridge round trip. English comments only. Cross-TU unique names use the
// ProtoSchema prefix.

#include "test_harness.hpp"
#include <rtti/proto_schema.hpp>
#include <rtti/error.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace rtti_test_lib {

namespace {

inline void ps_set_int(rtti::value_io& d, const char* key,
                       std::int64_t v) {
    rtti::value_io* n = d.make_node();
    n->make_scalar(rtti::field_value::of(v));
    d.set_key(rtti::string_view(key), n);
}

inline void ps_set_bool(rtti::value_io& d, const char* key, bool b) {
    rtti::value_io* n = d.make_node();
    n->make_scalar(rtti::field_value::of(b));
    d.set_key(rtti::string_view(key), n);
}

inline void ps_set_str(rtti::value_io& d, const char* key,
                       const char* s) {
    rtti::value_io* n = d.make_node();
    n->make_scalar(rtti::field_value::of(rtti::string_view(s)));
    d.set_key(rtti::string_view(key), n);
}

inline void ps_set_arr(rtti::value_io& d, const char* key,
                       const std::vector<std::int64_t>& vals) {
    rtti::value_io* a = d.make_node();
    a->make_array();
    for (const std::int64_t v : vals) {
        rtti::value_io* e = d.make_node();
        e->make_scalar(rtti::field_value::of(v));
        a->push_back(e);
    }
    d.set_key(rtti::string_view(key), a);
}

inline bool ps_get_int(const rtti::value_io* node, std::int64_t& out) {
    if (node == nullptr) {
        return false;
    }
    if (node->type() == rtti::value_io::kind::integer) {
        return node->as_integer(out);
    }
    if (node->type() == rtti::value_io::kind::boolean) {
        bool b = false;
        if (!node->as_bool(b)) {
            return false;
        }
        out = b ? 1 : 0;
        return true;
    }
    return false;
}

inline bool ps_get_str(const rtti::value_io* node, std::string& out) {
    if (node == nullptr || node->type() != rtti::value_io::kind::string) {
        return false;
    }
    rtti::string_view s;
    if (!node->as_string(s)) {
        return false;
    }
    out.assign(s.data(), s.size());
    return true;
}

inline bool ps_get_real(const rtti::value_io* node, double& out) {
    if (node == nullptr) {
        return false;
    }
    return node->as_real(out);
}

// Build one `map` entry object node (key/value fields) under `arr`.
inline void ps_map_entry(rtti::value_io* arr,
                         const char* key, std::int64_t value) {
    rtti::value_io* e = arr->make_node();
    e->make_object();
    rtti::value_io* kn = arr->make_node();
    kn->make_scalar(rtti::field_value::of(rtti::string_view(key)));
    e->set_key("key", kn);
    rtti::value_io* vn = arr->make_node();
    vn->make_scalar(rtti::field_value::of(value));
    e->set_key("value", vn);
    arr->push_back(e);
}

inline void ps_str_entry(rtti::value_io* arr,
                         const char* key, const char* value) {
    rtti::value_io* e = arr->make_node();
    e->make_object();
    rtti::value_io* kn = arr->make_node();
    kn->make_scalar(rtti::field_value::of(rtti::string_view(key)));
    e->set_key("key", kn);
    rtti::value_io* vn = arr->make_node();
    vn->make_scalar(rtti::field_value::of(rtti::string_view(value)));
    e->set_key("value", vn);
    arr->push_back(e);
}

} // namespace

RTTI_TEST_CASE(ProtoSchemaSyntaxAndScalars) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto3\";\n"
        "message M {\n"
        "  int32 a = 1;\n"
        "  int64 b = 2;\n"
        "  uint32 c = 3;\n"
        "  uint64 d = 4;\n"
        "  sint32 e = 5;\n"
        "  sint64 f = 6;\n"
        "  bool g = 7;\n"
        "  float h = 8;\n"
        "  double i = 9;\n"
        "  string j = 10;\n"
        "  bytes k = 11;\n"
        "  fixed32 l = 12;\n"
        "  fixed64 m = 13;\n"
        "  sfixed32 n = 14;\n"
        "  sfixed64 o = 15;\n"
        "}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ((int)r.value.syn, (int)syntax::proto3);
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(1));
    RTTI_CHECK_EQ(r.value.messages[0].name, "M");
    RTTI_CHECK_EQ(r.value.messages[0].full_name, "M");
    const auto& f = r.value.messages[0].fields;
    RTTI_CHECK_EQ(f.size(), size_t(15));
    RTTI_CHECK_EQ(f[0].name, "a");
    RTTI_CHECK_EQ((int)f[0].number, 1);
    RTTI_CHECK_EQ((int)f[0].type, (int)scalar_type::int32);
    RTTI_CHECK_EQ(f[0].mode, field_mode::optional);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[0].type), (int)wire_type::varint);
    RTTI_CHECK_EQ(f[5].name, "f");
    RTTI_CHECK_EQ((int)wire_of_scalar(f[5].type), (int)wire_type::varint);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[7].type), (int)wire_type::fixed32);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[8].type), (int)wire_type::fixed64);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[9].type), (int)wire_type::length);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[10].type), (int)wire_type::length);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[11].type), (int)wire_type::fixed32);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[12].type), (int)wire_type::fixed64);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[14].type), (int)wire_type::fixed64);
    RTTI_CHECK_EQ((int)f[14].number, 15);
}

RTTI_TEST_CASE(ProtoSchemaProto2RequiredDefault) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto2\";\n"
        "message P {\n"
        "  required int32 id = 1;\n"
        "  optional int32 age = 2 [default = 42];\n"
        "  optional string name = 3 [default = \"anon\"];\n"
        "  repeated int32 items = 4;\n"
        "  repeated int32 tags = 5 [packed = true];\n"
        "  optional double ratio = 6 [default = 1.5];\n"
        "  optional bool active = 7 [default = true];\n"
        "}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ((int)r.value.syn, (int)syntax::proto2);
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(1));
    const auto& f = r.value.messages[0].fields;
    RTTI_CHECK_EQ(f.size(), size_t(7));
    RTTI_CHECK_EQ(f[0].mode, field_mode::required);
    RTTI_CHECK_EQ(f[0].number, uint32_t(1));
    RTTI_CHECK(!f[1].packed);
    RTTI_CHECK(f[1].has_default);
    RTTI_CHECK_EQ((int)f[1].dflt_int, 42);
    RTTI_CHECK(f[2].has_default);
    RTTI_CHECK_EQ(f[2].dflt_str, "anon");
    RTTI_CHECK(!f[3].packed); // proto2 repeated numeric: packed off by default
    RTTI_CHECK(f[4].packed);  // explicit [packed = true]
    RTTI_CHECK(f[5].has_default);
    RTTI_CHECK_EQ(f[5].dflt_real, 1.5);
    RTTI_CHECK(f[6].has_default);
    RTTI_CHECK_EQ((int)f[6].dflt_int, 1); // `true`
}

RTTI_TEST_CASE(ProtoSchemaProto3Packed) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto3\";\n"
        "message P {\n"
        "  repeated int32 nums = 1;\n"
        "  repeated string names = 2;\n"
        "  repeated int32 raw = 3 [packed = false];\n"
        "  Inner inner = 4;\n"
        "}\n"
        "message Inner { int32 x = 1; }\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(2));
    const auto& f = r.value.messages[0].fields;
    RTTI_CHECK(f[0].packed);  // proto3 numeric repeated: packed by default
    RTTI_CHECK(!f[1].packed); // string is never packed
    RTTI_CHECK(!f[2].packed); // explicit [packed = false] wins
    RTTI_CHECK(!f[3].packed); // message is never packed
    RTTI_CHECK(f[3].is_message);
    RTTI_CHECK_EQ(f[3].type_ref, "Inner");
    RTTI_CHECK_EQ(f[3].type_index, size_t(1));
    RTTI_CHECK_EQ((int)wire_of_scalar(f[3].type), (int)wire_type::length);
}

RTTI_TEST_CASE(ProtoSchemaNestedAndComments) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "// leading line comment\n"
        "/* block\n"
        "   comment */\n"
        "syntax = \"proto3\";\n"
        "message Outer {\n"
        "  /* inner definition */\n"
        "  message Inner { int32 v = 1; } // trailing\n"
        "  Inner inner = 1;\n"
        "  string s = 2;\n"
        "}\n"
        "message Second {\n"
        "  Outer.Inner deep = 1;\n"
        "  enum Color { RED = 0; GREEN = 1; }\n"
        "  Color color = 2;\n"
        "}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(3));
    // dependency: Outer, Second (top-level) come first in declaration order.
    RTTI_CHECK_EQ(r.value.messages[0].name, "Outer");
    RTTI_CHECK_EQ(r.value.messages[1].name, "Second");
    RTTI_CHECK_EQ(r.value.messages[2].name, "Inner");
    RTTI_CHECK_EQ(r.value.messages[2].full_name, "Outer.Inner");
    const auto& outer = r.value.messages[0].fields;
    RTTI_CHECK_EQ(outer.size(), size_t(2));
    RTTI_CHECK(outer[0].is_message);
    RTTI_CHECK_EQ(outer[0].type_ref, "Inner");
    RTTI_CHECK_EQ(outer[0].type_index, size_t(2));
    RTTI_CHECK(!outer[0].is_enum);
    const auto& second = r.value.messages[1].fields;
    RTTI_CHECK_EQ(second.size(), size_t(2));
    RTTI_CHECK(second[0].is_message); // dotted reference resolves
    RTTI_CHECK_EQ(second[0].type_index, size_t(2));
    RTTI_CHECK(second[1].is_enum); // enum reference resolves
    RTTI_CHECK(!second[1].is_message);
    RTTI_CHECK_EQ((int)second[1].type, (int)scalar_type::enum_val);
    RTTI_CHECK_EQ((int)wire_of_scalar(second[1].type),
                  (int)wire_type::varint);
    RTTI_CHECK_EQ(r.value.enums.size(), size_t(1));
    RTTI_CHECK_EQ(r.value.enums[0], "Second.Color");
}

RTTI_TEST_CASE(ProtoSchemaCommentOnlyFile) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "/* only\n * comments\n */\n// and a line\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(0));
    RTTI_CHECK_EQ((int)r.value.syn, (int)syntax::proto2); // absent -> proto2
}

RTTI_TEST_CASE(ProtoSchemaEmptyInput) {
    using namespace rtti;
    using namespace rtti::proto;
    const result<schema_desc> r = parse_schema("");
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(0));
}

RTTI_TEST_CASE(ProtoSchemaErrors) {
    using namespace rtti;
    using namespace rtti::proto;
    // unknown token
    {
        const result<schema_desc> r = parse_schema("@#$");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // bad field number 0
    {
        const result<schema_desc> r =
            parse_schema("message M { int32 x = 0; }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::proto_bad_field);
    }
    // field number beyond the 2^29-1 range
    {
        const result<schema_desc> r =
            parse_schema("message M { int32 x = 5000000000; }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::proto_bad_field);
    }
    // field number numeric overflow
    {
        const result<schema_desc> r = parse_schema(
            "message M { int32 x = 99999999999999999999999; }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::invalid_number);
    }
    // missing semicolon
    {
        const result<schema_desc> r =
            parse_schema("message M { int32 x = 1 }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // unknown field type
    {
        const result<schema_desc> r =
            parse_schema("message M { frob x = 1; }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // unterminated block comment
    {
        const result<schema_desc> r = parse_schema("/* never closed");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code,
                      (int)error_code::unterminated_comment);
    }
    // unterminated string literal in an option
    {
        const result<schema_desc> r =
            parse_schema("message M { string s = 1 [default = \"oops]; }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code,
                      (int)error_code::unterminated_string);
    }
    // bad syntax value
    {
        const result<schema_desc> r = parse_schema("syntax = \"proto4\";");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // required is illegal in proto3
    {
        const result<schema_desc> r = parse_schema(
            "syntax = \"proto3\"; message M { required int32 x = 1; }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // missing field number
    {
        const result<schema_desc> r =
            parse_schema("message M { int32 x = ; }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // message without a name
    {
        const result<schema_desc> r = parse_schema("message { }");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // truncated message body
    {
        const result<schema_desc> r = parse_schema("message M {");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
}

RTTI_TEST_CASE(ProtoSchemaErrorOffsetAndLine) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text = "message M {\n  int32 x = 0;\n}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ((int)r.err.code, (int)error_code::proto_bad_field);
    RTTI_CHECK_EQ(r.err.line(), size_t(2)); // error is on line 2
    RTTI_CHECK(r.err.offset > size_t(0));
}

RTTI_TEST_CASE(ProtoSchemaBridgeRoundTrip) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto3\";\n"
        "message Inner { bool flag = 1; }\n"
        "message M {\n"
        "  int32 a = 1;\n"
        "  string b = 2;\n"
        "  repeated int32 nums = 3;\n"
        "  Inner inner = 4;\n"
        "}\n";
    compile_config cfg;
    cfg.root_name = "M";
    const result<compiled_schema> cs = compile_schema(text, cfg);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    RTTI_CHECK(si != nullptr);
    RTTI_CHECK_EQ((int)si->syn, (int)syntax::proto3);
    RTTI_CHECK_EQ(si->count, size_t(4));

    // build a document and encode
    document doc;
    ps_set_int(doc, "a", 10);
    ps_set_str(doc, "b", "hi");
    ps_set_arr(doc, "nums", {1, 2, 3});
    rtti::value_io* o = doc.make_node();
    o->make_object();
    ps_set_bool(*o, "flag", true);
    doc.set_key("inner", o);

    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    RTTI_CHECK(wire.size() > size_t(0));

    // decode back and verify every field
    document out;
    RTTI_CHECK_EQ(wire_to_tree(wire, si, out), error_code::ok);
    rtti::value_io* na = nullptr;
    RTTI_CHECK(out.get_key("a", na));
    std::int64_t ia = 0;
    RTTI_CHECK(ps_get_int(na, ia));
    RTTI_CHECK_EQ(ia, int64_t(10));
    rtti::value_io* nb = nullptr;
    RTTI_CHECK(out.get_key("b", nb));
    std::string sb;
    RTTI_CHECK(ps_get_str(nb, sb));
    RTTI_CHECK_EQ(sb, "hi");
    rtti::value_io* nn = nullptr;
    RTTI_CHECK(out.get_key("nums", nn));
    RTTI_CHECK_EQ(nn->size(), size_t(3));
    std::int64_t e0 = 0, e2 = 0;
    RTTI_CHECK(ps_get_int(nn->at(0), e0));
    RTTI_CHECK(ps_get_int(nn->at(2), e2));
    RTTI_CHECK_EQ(e0, int64_t(1));
    RTTI_CHECK_EQ(e2, int64_t(3));
    rtti::value_io* ni = nullptr;
    RTTI_CHECK(out.get_key("inner", ni));
    RTTI_CHECK_EQ(ni->type(), rtti::value_io::kind::object);
    rtti::value_io* nf = nullptr;
    RTTI_CHECK(ni->get_key("flag", nf));
    std::int64_t flag = 0;
    RTTI_CHECK(ps_get_int(nf, flag));
    RTTI_CHECK_EQ(flag, int64_t(1));

    // re-encode the decoded tree; bytes must be identical
    std::string wire2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, wire2), error_code::ok);
    RTTI_CHECK_EQ(wire, wire2);

    // schema-aware parse returns a valid document
    const result<document> pr = parse(wire, si);
    RTTI_CHECK(pr.ok());
    rtti::value_io* pa = nullptr;
    RTTI_CHECK(pr.value.get_key("a", pa));
    std::int64_t pa_val = 0;
    RTTI_CHECK(ps_get_int(pa, pa_val));
    RTTI_CHECK_EQ(pa_val, int64_t(10));

    // the compiled schema exposes a named non-root message as well
    const schema_info* inner = cs.value.find_message("Inner");
    RTTI_CHECK(inner != nullptr);
    RTTI_CHECK_EQ(inner->count, size_t(1));
}

RTTI_TEST_CASE(ProtoSchemaPackedWireLayout) {
    using namespace rtti;
    using namespace rtti::proto;
    const result<compiled_schema> cs = compile_schema(
        "syntax = \"proto3\"; message M { repeated int32 nums = 1; }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    document doc;
    ps_set_arr(doc, "nums", {1, 2, 3});
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    // tag field 1 wire 2 -> 0x0A, length 3, payload 01 02 03
    const std::string expected("\x0A\x03\x01\x02\x03", 5);
    RTTI_CHECK_EQ(wire, expected);
}

RTTI_TEST_CASE(ProtoSchemaRequiredProto2) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto2\";\n"
        "message R {\n"
        "  required int32 id = 1;\n"
        "  optional string name = 2;\n"
        "}\n";
    const result<compiled_schema> cs = compile_schema(text);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // present required field round-trips
    document doc;
    ps_set_int(doc, "id", 7);
    ps_set_str(doc, "name", "zoe");
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    document out;
    RTTI_CHECK_EQ(wire_to_tree(wire, si, out), error_code::ok);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(out.get_key("id", v));
    std::int64_t id = 0;
    RTTI_CHECK(ps_get_int(v, id));
    RTTI_CHECK_EQ(id, int64_t(7));

    // missing required field -> error on decode
    document empty;
    std::string wire2;
    RTTI_CHECK_EQ(tree_to_wire(&empty, si, wire2), error_code::ok);
    document out2;
    RTTI_CHECK_EQ(wire_to_tree(wire2, si, out2), error_code::missing_key);
}

RTTI_TEST_CASE(ProtoSchemaDefaultsFill) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto2\";\n"
        "message P {\n"
        "  optional int32 a = 1 [default = 42];\n"
        "  optional string s = 2 [default = \"hey\"];\n"
        "  optional double d = 3 [default = 1.5];\n"
        "  optional bool q = 4 [default = true];\n"
        "}\n";
    const result<compiled_schema> cs = compile_schema(text);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // empty document encodes to zero bytes (all defaults are omitted)
    document empty;
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&empty, si, wire), error_code::ok);
    RTTI_CHECK_EQ(wire.size(), size_t(0));

    // decoding an empty wire fills the explicit defaults
    document out;
    RTTI_CHECK_EQ(wire_to_tree(wire, si, out), error_code::ok);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(out.get_key("a", v));
    std::int64_t ai = 0;
    RTTI_CHECK(ps_get_int(v, ai));
    RTTI_CHECK_EQ(ai, int64_t(42));
    RTTI_CHECK(out.get_key("s", v));
    std::string ss;
    RTTI_CHECK(ps_get_str(v, ss));
    RTTI_CHECK_EQ(ss, "hey");
    RTTI_CHECK(out.get_key("d", v));
    double dd = 0.0;
    RTTI_CHECK(ps_get_real(v, dd));
    RTTI_CHECK_EQ(dd, 1.5);
    RTTI_CHECK(out.get_key("q", v));
    RTTI_CHECK_EQ(v->type(), rtti::value_io::kind::boolean);
}

RTTI_TEST_CASE(ProtoSchemaSelfReferential) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "message Node { int32 id = 1; Node next = 2; }\n";
    const result<compiled_schema> cs = compile_schema(text);
    RTTI_CHECK(cs.ok());

    // shallow encoding / decoding round trip
    const schema_info* si = cs.value.info();
    document doc;
    ps_set_int(doc, "id", 5);
    rtti::value_io* inner = doc.make_node();
    inner->make_object();
    ps_set_int(*inner, "id", 6);
    doc.set_key("next", inner);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    document out;
    RTTI_CHECK_EQ(wire_to_tree(wire, si, out), error_code::ok);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(out.get_key("id", v));
    std::int64_t id = 0;
    RTTI_CHECK(ps_get_int(v, id));
    RTTI_CHECK_EQ(id, int64_t(5));
    RTTI_CHECK(out.get_key("next", v));
    RTTI_CHECK_EQ(v->type(), rtti::value_io::kind::object);
}

RTTI_TEST_CASE(ProtoSchemaDeepNestingGuard) {
    using namespace rtti;
    using namespace rtti::proto;
    const result<compiled_schema> cs =
        compile_schema("message Node { Node next = 1; }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // moderate-depth chain decodes fine through the runtime bridge
    {
        std::string inner;
        std::string scratch;
        for (int i = 0; i < 300; ++i) {
            scratch.clear();
            scratch.push_back('\x0A'); // (1 << 3) | 2
            (void)encode_varint(inner.size(), scratch);
            scratch += inner;
            inner.swap(scratch);
        }
        const result<document> ok_r = parse(inner, si);
        RTTI_CHECK(ok_r.ok());
    }

    // Build a wire message nested ~700 deep (field 1 length-delimited).
    std::string inner;
    std::string scratch;
    for (int i = 0; i < 700; ++i) {
        scratch.clear();
        scratch.push_back('\x0A'); // (1 << 3) | 2
        (void)encode_varint(inner.size(), scratch);
        scratch += inner;
        inner.swap(scratch);
    }
    const result<document> r = parse(inner, si);
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ((int)r.err.code, (int)error_code::too_deep);
}

RTTI_TEST_CASE(ProtoSchemaMalformedWire) {
    using namespace rtti;
    using namespace rtti::proto;
    const result<compiled_schema> cs =
        compile_schema("message M { int32 x = 1; }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // truncated varint payload
    {
        const rtti::string_view wire("\x08", 1);
        document out;
        RTTI_CHECK_EQ(wire_to_tree(wire, si, out),
                      error_code::proto_truncated);
    }
    // invalid field number 0
    {
        const rtti::string_view wire("\x00", 1);
        document out;
        RTTI_CHECK_EQ(wire_to_tree(wire, si, out),
                      error_code::proto_bad_field);
    }
}

RTTI_TEST_CASE(ProtoSchemaCompileModes) {
    using namespace rtti;
    using namespace rtti::proto;
    // empty schema_desc cannot compile
    {
        schema_desc desc;
        const result<compiled_schema> cs = compile_schema(desc);
        RTTI_CHECK(!cs.ok());
        RTTI_CHECK_EQ((int)cs.err.code, (int)error_code::invalid_root);
    }
    // unknown root name
    {
        compile_config cfg;
        cfg.root_name = "NoSuch";
        const result<compiled_schema> cs =
            compile_schema("message A { int32 x = 1; }", cfg);
        RTTI_CHECK(!cs.ok());
        RTTI_CHECK_EQ((int)cs.err.code, (int)error_code::no_such_field);
    }
}

RTTI_TEST_CASE(ProtoSchemaSintZigzagWireMatchesOfficial) {
    using namespace rtti;
    using namespace rtti::proto;
    // message S { sint64 z = 1; }: z = -1 must encode as tag 0x08 + varint
    // 0x01 (zigzag(-1) = 1), byte-identical to protoc --encode.
    const result<compiled_schema> cs =
        compile_schema("syntax = \"proto3\"; message S { sint64 z = 1; }");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    document doc;
    ps_set_int(doc, "z", -1);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    const std::string expected("\x08\x01", 2);
    RTTI_CHECK_EQ(wire, expected);

    // Decode the official bytes back into a tree whose z value is -1.
    document out;
    RTTI_CHECK_EQ(wire_to_tree(expected, si, out), error_code::ok);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(out.get_key("z", v));
    std::int64_t z = 0;
    RTTI_CHECK(ps_get_int(v, z));
    RTTI_CHECK_EQ(z, int64_t(-1));

    // Re-encoding the decoded tree reproduces the same bytes.
    std::string wire2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, wire2), error_code::ok);
    RTTI_CHECK_EQ(wire2, expected);
}

RTTI_TEST_CASE(ProtoSchemaSintOfficialFixtureValue) {
    using namespace rtti;
    using namespace rtti::proto;
    // Official protoc 36.2 fixture tests/fixtures/proto/basic.Scalars.wire:
    // basic.proto declares `sint64 z = 2;` and basic.textbin sets `z: -456`,
    // producing field 2 (tag 0x10) + varint(zigzag(-456) = 911) = 0x10 0x8F 0x07.
    static const unsigned char official[] = {0x10, 0x8F, 0x07};
    const result<compiled_schema> cs =
        compile_schema("syntax = \"proto3\"; message S { sint64 z = 2; }");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    document out;
    RTTI_CHECK_EQ(wire_to_tree(rtti::string_view(
                                   reinterpret_cast<const char*>(official),
                                   sizeof(official)),
                               si, out),
                  error_code::ok);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(out.get_key("z", v));
    std::int64_t z = 0;
    RTTI_CHECK(ps_get_int(v, z));
    RTTI_CHECK_EQ(z, int64_t(-456));

    // Round trip: re-encoding the decoded tree produces identical bytes.
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, wire), error_code::ok);
    RTTI_CHECK_EQ(wire.size(), size_t(3));
    RTTI_CHECK_EQ(wire, std::string(reinterpret_cast<const char*>(official),
                                    sizeof(official)));
}

RTTI_TEST_CASE(ProtoSchemaSint32ZigzagWire) {
    using namespace rtti;
    using namespace rtti::proto;
    // sint32 uses the same zig-zag mapping; expected bytes verified with
    // protoc --encode: s = -2 -> tag(1) 0x08 + zigzag(3); s = 300 -> zigzag(600)
    // = varint 0xD8 0x04.
    const result<compiled_schema> cs =
        compile_schema("syntax = \"proto3\"; message S { sint32 s = 1; }");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    document doc;
    ps_set_int(doc, "s", -2);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    RTTI_CHECK_EQ(wire, std::string("\x08\x03", 2));

    document doc2;
    ps_set_int(doc2, "s", 300);
    std::string wire2;
    RTTI_CHECK_EQ(tree_to_wire(&doc2, si, wire2), error_code::ok);
    RTTI_CHECK_EQ(wire2, std::string("\x08\xD8\x04", 3));

    // Decode back from the official bytes.
    document out;
    RTTI_CHECK_EQ(wire_to_tree(wire2, si, out), error_code::ok);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(out.get_key("s", v));
    std::int64_t s = 0;
    RTTI_CHECK(ps_get_int(v, s));
    RTTI_CHECK_EQ(s, int64_t(300));
}

RTTI_TEST_CASE(ProtoSchemaBoolTreeAlignsWithSchemaT) {
    using namespace rtti;
    using namespace rtti::proto;
    // The schema<T> tree bridge decodes varint bool fields to boolean nodes;
    // the .proto-text path must produce the same tree shape (A2 ③b).
    const result<compiled_schema> cs =
        compile_schema("syntax = \"proto3\"; message B { bool flag = 1; }");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    document out;
    RTTI_CHECK_EQ(wire_to_tree(std::string("\x08\x01", 2), si, out),
                  error_code::ok);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(out.get_key("flag", v));
    RTTI_CHECK_EQ(v->type(), rtti::value_io::kind::boolean);
    bool b = false;
    RTTI_CHECK(v->as_bool(b));
    RTTI_CHECK_EQ(b, true);

    // Re-encoding a boolean node emits the same wire bytes.
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, wire), error_code::ok);
    RTTI_CHECK_EQ(wire, std::string("\x08\x01", 2));

    // A false bit stays a boolean node too.
    document out2;
    RTTI_CHECK_EQ(wire_to_tree(std::string("\x08\x00", 2), si, out2),
                  error_code::ok);
    RTTI_CHECK(out2.get_key("flag", v));
    RTTI_CHECK_EQ(v->type(), rtti::value_io::kind::boolean);
    RTTI_CHECK(v->as_bool(b));
    RTTI_CHECK_EQ(b, false);

    // Encoding a boolean true from a fresh document round-trips.
    document doc;
    ps_set_bool(doc, "flag", true);
    std::string w2;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, w2), error_code::ok);
    RTTI_CHECK_EQ(w2, std::string("\x08\x01", 2));
}

RTTI_TEST_CASE(ProtoSchemaMapDeclParse) {
    using namespace rtti;
    using namespace rtti::proto;
    const result<schema_desc> r =
        parse_schema("syntax = \"proto3\"; message M { map<string, int32> "
                     "counts = 1; }\n");
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(2)); // M + M.counts_entry
    const auto& f = r.value.messages[0].fields;
    RTTI_CHECK_EQ(f.size(), size_t(1));
    RTTI_CHECK(f[0].is_map);
    RTTI_CHECK(f[0].is_message);
    RTTI_CHECK(f[0].mode == field_mode::repeated);
    RTTI_CHECK(f[0].type == scalar_type::message_val);
    RTTI_CHECK_EQ((int)wire_of_scalar(f[0].type), (int)wire_type::length);
    RTTI_CHECK(f[0].type_index < r.value.messages.size());
    // The entry message carries key=1 / value=2.
    const auto& entry = r.value.messages[f[0].type_index];
    RTTI_CHECK_EQ(entry.full_name, "M.counts_entry");
    RTTI_CHECK_EQ(entry.fields.size(), size_t(2));
    RTTI_CHECK_EQ(entry.fields[0].name, "key");
    RTTI_CHECK_EQ((int)entry.fields[0].number, 1);
    RTTI_CHECK(entry.fields[0].type == scalar_type::string_val);
    RTTI_CHECK(entry.fields[0].emit_present);
    RTTI_CHECK_EQ(entry.fields[1].name, "value");
    RTTI_CHECK_EQ((int)entry.fields[1].number, 2);
    RTTI_CHECK(entry.fields[1].type == scalar_type::int32);
    RTTI_CHECK(entry.fields[1].emit_present);
}

RTTI_TEST_CASE(ProtoSchemaMapWireRoundTrip) {
    using namespace rtti;
    using namespace rtti::proto;
    // map<string, int32>: official protoc 36.2 bytes for {"alpha":1} are
    // 0A 05 0A 05 "alpha" 10 01 (field 1 = entry, key=1 len, value=2 varint).
    const result<compiled_schema> cs = compile_schema(
        "syntax = \"proto3\"; message M { map<string, int32> m = 1; }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    RTTI_CHECK(si != nullptr);

    document doc;
    rtti::value_io* arr = doc.make_node();
    arr->make_array();
    ps_map_entry(arr, "alpha", 1);
    doc.set_key("m", arr);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    // field 1 tag 0x0A, outer length 9 (key tag+len+5 + value tag+1).
    const std::string expected("\x0A\x09\x0A\x05\x61\x6C\x70\x68\x61\x10\x01",
                               11);
    RTTI_CHECK_EQ(wire, expected);

    // Decoding the official bytes reproduces the entry tree; re-encoding is
    // byte-identical.
    document out;
    RTTI_CHECK_EQ(wire_to_tree(expected, si, out), error_code::ok);
    rtti::value_io* nm = nullptr;
    RTTI_CHECK(out.get_key("m", nm));
    RTTI_CHECK_EQ(nm->type(), rtti::value_io::kind::array);
    RTTI_CHECK_EQ(nm->size(), size_t(1));
    rtti::value_io* e0 = nm->at(0);
    RTTI_CHECK(e0 != nullptr);
    rtti::value_io* nk = nullptr;
    RTTI_CHECK(e0->get_key("key", nk));
    std::string ks;
    RTTI_CHECK(ps_get_str(nk, ks));
    RTTI_CHECK_EQ(ks, "alpha");
    rtti::value_io* nv = nullptr;
    RTTI_CHECK(e0->get_key("value", nv));
    std::int64_t vv = 0;
    RTTI_CHECK(ps_get_int(nv, vv));
    RTTI_CHECK_EQ(vv, int64_t(1));
    std::string wire2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, wire2), error_code::ok);
    RTTI_CHECK_EQ(wire2, expected);

    // A map value of zero is still written on the wire (presence-tracked):
    // protoc 36.2 emits `1A 05 0A 01 61 10 00` for field-3 map {"a":0}; with
    // this schema's field number 1 the tag is 0x0A instead of 0x1A.
    document z;
    rtti::value_io* zarr = z.make_node();
    zarr->make_array();
    ps_map_entry(zarr, "a", 0);
    z.set_key("m", zarr);
    std::string zwire;
    RTTI_CHECK_EQ(tree_to_wire(&z, si, zwire), error_code::ok);
    RTTI_CHECK_EQ(zwire, std::string("\x0A\x05\x0A\x01\x61\x10\x00", 7));
}

RTTI_TEST_CASE(ProtoSchemaMapStringString) {
    using namespace rtti;
    using namespace rtti::proto;
    // map<string, string>: entry key=1 len, value=2 len. protoc encoding of
    // {"en":"bob"} is 0A 09 0A 02 "en" 12 03 "bob".
    const result<compiled_schema> cs = compile_schema(
        "syntax = \"proto3\"; message M { map<string, string> l = 1; }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    document doc;
    rtti::value_io* arr = doc.make_node();
    arr->make_array();
    ps_str_entry(arr, "en", "bob");
    doc.set_key("l", arr);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    const std::string expected("\x0A\x09\x0A\x02\x65\x6E\x12\x03\x62\x6F\x62",
                               11);
    RTTI_CHECK_EQ(wire, expected);

    document out;
    RTTI_CHECK_EQ(wire_to_tree(expected, si, out), error_code::ok);
    std::string wire2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, wire2), error_code::ok);
    RTTI_CHECK_EQ(wire2, expected);
}

RTTI_TEST_CASE(ProtoSchemaMapNestedValueMessage) {
    using namespace rtti;
    using namespace rtti::proto;
    // map<string, Inner>: the entry value (field 2) is a length-delimited
    // nested message. Official protoc 36.2 encoding of {"k": {label:"L"}} is
    // 0A 08 0A 01 6B 12 03 0A 01 4C.
    const result<compiled_schema> cs = compile_schema(
        "syntax = \"proto3\"; message M { map<string, Inner> m = 1; } "
        "message Inner { string label = 1; }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    document doc;
    rtti::value_io* arr = doc.make_node();
    arr->make_array();
    rtti::value_io* e = doc.make_node();
    e->make_object();
    rtti::value_io* kn = doc.make_node();
    kn->make_scalar(rtti::field_value::of(rtti::string_view("k")));
    e->set_key("key", kn);
    rtti::value_io* vn = doc.make_node();
    vn->make_object();
    rtti::value_io* ln = doc.make_node();
    ln->make_scalar(rtti::field_value::of(rtti::string_view("L")));
    vn->set_key("label", ln);
    e->set_key("value", vn);
    arr->push_back(e);
    doc.set_key("m", arr);

    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    const std::string expected("\x0A\x08\x0A\x01\x6B\x12\x03\x0A\x01\x4C", 10);
    RTTI_CHECK_EQ(wire, expected);

    document out;
    RTTI_CHECK_EQ(wire_to_tree(expected, si, out), error_code::ok);
    rtti::value_io* nm = nullptr;
    RTTI_CHECK(out.get_key("m", nm));
    RTTI_CHECK_EQ(nm->size(), size_t(1));
    rtti::value_io* v = nullptr;
    RTTI_CHECK(nm->at(0)->get_key("value", v));
    RTTI_CHECK_EQ(v->type(), rtti::value_io::kind::object);
    rtti::value_io* lab = nullptr;
    RTTI_CHECK(v->get_key("label", lab));
    std::string ls;
    RTTI_CHECK(ps_get_str(lab, ls));
    RTTI_CHECK_EQ(ls, "L");
}

RTTI_TEST_CASE(ProtoSchemaOneofParseAndPresence) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto3\";\n"
        "message O {\n"
        "  oneof choice { int32 n = 3; string s = 4; }\n"
        "}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    const auto& fs = r.value.messages[0].fields;
    RTTI_CHECK_EQ(fs.size(), size_t(2));
    RTTI_CHECK(fs[0].oneof_group != 0);
    RTTI_CHECK_EQ(fs[0].oneof_group, fs[1].oneof_group);

    // Oneof members have explicit presence: a zero-valued scalar is still
    // written (protoc emits `n:0` => 18 00).
    const result<compiled_schema> cs = compile_schema(text);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    document doc;
    ps_set_int(doc, "n", 0);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    RTTI_CHECK_EQ(wire, std::string("\x18\x00", 2));

    // Decoding the official bytes yields the member node.
    document out;
    RTTI_CHECK_EQ(wire_to_tree(std::string("\x18\x00", 2), si, out),
                  error_code::ok);
    rtti::value_io* nn = nullptr;
    RTTI_CHECK(out.get_key("n", nn));
    std::int64_t ni = 0;
    RTTI_CHECK(ps_get_int(nn, ni));
    RTTI_CHECK_EQ(ni, int64_t(0));
    // ... and the sibling member is not materialized.
    rtti::value_io* ns = nullptr;
    RTTI_CHECK(!out.get_key("s", ns));

    // With neither member present, no spurious member keys appear.
    document empty;
    std::string w0;
    RTTI_CHECK_EQ(tree_to_wire(&empty, si, w0), error_code::ok);
    RTTI_CHECK_EQ(w0.size(), size_t(0));
    document out0;
    RTTI_CHECK_EQ(wire_to_tree(std::string("", 0), si, out0), error_code::ok);
    RTTI_CHECK(!out0.get_key("n", nn));
    RTTI_CHECK(!out0.get_key("s", ns));
}

RTTI_TEST_CASE(ProtoSchemaOneofMutexLastWins) {
    using namespace rtti;
    using namespace rtti::proto;
    const result<compiled_schema> cs = compile_schema(
        "syntax = \"proto3\"; message O { oneof c { int32 n = 3; string s = 4; "
        "} }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // Wire carrying both members: protoc --decode collapses to the LAST one
    // (n=5 then s="x" => only `s: "x"` survives). `18 05 22 01 78`.
    static const unsigned char both[] = {0x18, 0x05, 0x22, 0x01, 'x'};
    document out;
    RTTI_CHECK_EQ(wire_to_tree(
                      rtti::string_view(reinterpret_cast<const char*>(both),
                                        sizeof(both)),
                      si, out),
                  error_code::ok);
    rtti::value_io* ns = nullptr;
    RTTI_CHECK(out.get_key("s", ns));
    std::string ss;
    RTTI_CHECK(ps_get_str(ns, ss));
    RTTI_CHECK_EQ(ss, "x");
    rtti::value_io* nn = nullptr;
    RTTI_CHECK(!out.get_key("n", nn));

    // Re-encoding a decoded single-member tree reproduces the official wire
    // for that member.
    std::string w2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, w2), error_code::ok);
    RTTI_CHECK_EQ(w2, std::string("\x22\x01\x78", 3));

    // Reverse order: the later `n` clears `s`.
    static const unsigned char rev[] = {0x22, 0x01, 'x', 0x18, 0x05};
    document out2;
    RTTI_CHECK_EQ(wire_to_tree(
                      rtti::string_view(reinterpret_cast<const char*>(rev),
                                        sizeof(rev)),
                      si, out2),
                  error_code::ok);
    rtti::value_io* n2 = nullptr;
    RTTI_CHECK(out2.get_key("n", n2));
    std::int64_t v = 0;
    RTTI_CHECK(ps_get_int(n2, v));
    RTTI_CHECK_EQ(v, int64_t(5));
    RTTI_CHECK(!out2.get_key("s", n2));
}

RTTI_TEST_CASE(ProtoSchemaNestedOneofOfficialFixture) {
    using namespace rtti;
    using namespace rtti::proto;
    // Oneof with a message member, decoded from the official protoc 36.2
    // fixture tests/fixtures/proto/advanced.MsgChoice.wire: `msg {label:"L"}`
    // => field 5 length-delimited (2A 03 0A 01 4C).
    const result<compiled_schema> cs = compile_schema(
        "syntax = \"proto3\"; message All { oneof choice { int32 n = 3; "
        "string s = 4; Inner msg = 5; } } message Inner { string label = 1; "
        "}\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    static const unsigned char official[] = {0x2A, 0x03, 0x0A, 0x01, 0x4C};
    document out;
    RTTI_CHECK_EQ(wire_to_tree(rtti::string_view(
                                   reinterpret_cast<const char*>(official),
                                   sizeof(official)),
                               si, out),
                  error_code::ok);
    rtti::value_io* nm = nullptr;
    RTTI_CHECK(out.get_key("msg", nm));
    RTTI_CHECK_EQ(nm->type(), rtti::value_io::kind::object);
    rtti::value_io* lab = nullptr;
    RTTI_CHECK(nm->get_key("label", lab));
    std::string ls;
    RTTI_CHECK(ps_get_str(lab, ls));
    RTTI_CHECK_EQ(ls, "L");
    rtti::value_io* tmp = nullptr;
    RTTI_CHECK(!out.get_key("n", tmp));
    RTTI_CHECK(!out.get_key("s", tmp));

    // Re-encoding reproduces the official bytes exactly.
    std::string w2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, w2), error_code::ok);
    RTTI_CHECK_EQ(w2, std::string(reinterpret_cast<const char*>(official),
                                  sizeof(official)));
}

RTTI_TEST_CASE(ProtoSchemaReservedParseAndConflict) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* ok =
        "message M { reserved 1, 5 to 8, 20 to max; reserved \"legacy\"; "
        "int32 x = 2; }\n";
    const result<schema_desc> r = parse_schema(ok);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.messages[0].reserved_numbers.size(), size_t(3));
    RTTI_CHECK_EQ(r.value.messages[0].reserved_names.size(), size_t(1));
    RTTI_CHECK_EQ(r.value.messages[0].reserved_names[0], "legacy");
    RTTI_CHECK_EQ(r.value.messages[0].fields.size(), size_t(1));

    // Field number in a reserved range -> error.
    {
        const result<schema_desc> e = parse_schema(
            "message M { reserved 5 to 8; int32 x = 6; }\n");
        RTTI_CHECK(!e.ok());
        RTTI_CHECK_EQ((int)e.err.code, (int)error_code::proto_bad_field);
    }
    // Field number reserved as a singleton -> error.
    {
        const result<schema_desc> e = parse_schema(
            "message M { reserved 3; int32 x = 3; }\n");
        RTTI_CHECK(!e.ok());
        RTTI_CHECK_EQ((int)e.err.code, (int)error_code::proto_bad_field);
    }
    // Reserved field name -> error.
    {
        const result<schema_desc> e = parse_schema(
            "message M { reserved \"old\"; int32 old = 1; }\n");
        RTTI_CHECK(!e.ok());
        RTTI_CHECK_EQ((int)e.err.code, (int)error_code::proto_bad_field);
    }
    // Malformed reserved range (descending) -> error.
    {
        const result<schema_desc> e = parse_schema(
            "message M { reserved 8 to 5; }\n");
        RTTI_CHECK(!e.ok());
        RTTI_CHECK_EQ((int)e.err.code, (int)error_code::proto_bad_field);
    }
}

RTTI_TEST_CASE(ProtoSchemaImportStatements) {
    using namespace rtti;
    using namespace rtti::proto;
    const result<schema_desc> r = parse_schema(
        "syntax = \"proto3\";\n"
        "import \"other.proto\";\n"
        "import public \"pub.proto\";\n"
        "import weak \"weakly.proto\";\n"
        "message M { int32 x = 1; }\n");
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.imports.size(), size_t(3));
    RTTI_CHECK_EQ(r.value.imports[0], "other.proto");
    RTTI_CHECK_EQ(r.value.imports[1], "pub.proto");
    RTTI_CHECK_EQ(r.value.imports[2], "weakly.proto");

    // Missing import path -> error.
    {
        const result<schema_desc> e = parse_schema("import ;\n");
        RTTI_CHECK(!e.ok());
        RTTI_CHECK_EQ((int)e.err.code, (int)error_code::unexpected_token);
    }
    // Unquoted import path -> error.
    {
        const result<schema_desc> e = parse_schema("import foo.proto;\n");
        RTTI_CHECK(!e.ok());
        RTTI_CHECK_EQ((int)e.err.code, (int)error_code::unexpected_token);
    }
}

RTTI_TEST_CASE(ProtoSchemaImportExternalTypeOpaque) {
    using namespace rtti;
    using namespace rtti::proto;
    // A field whose type is declared by an imported file is accepted as an
    // opaque external message when the import is declared (the dependency is
    // not loaded): wire is length-delimited and byte-preserving.
    const result<compiled_schema> cs = compile_schema(
        "syntax = \"proto3\";\n"
        "import \"dep.proto\";\n"
        "message M { dep.External x = 1; int32 y = 2; }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    document doc;
    rtti::value_io* xn = doc.make_node();
    xn->make_scalar(rtti::field_value::of(rtti::string_view("\x01\x02")));
    doc.set_key("x", xn);
    ps_set_int(doc, "y", 7);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    // field 1 length 2 payload 01 02, field 2 varint 7
    RTTI_CHECK_EQ(wire, std::string("\x0A\x02\x01\x02\x10\x07", 6));

    document out;
    RTTI_CHECK_EQ(wire_to_tree(wire, si, out), error_code::ok);
    std::string wire2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, wire2), error_code::ok);
    RTTI_CHECK_EQ(wire2, wire);

    // Without any import the same reference stays a hard error.
    {
        const result<schema_desc> e =
            parse_schema("message M { dep.External x = 1; }\n");
        RTTI_CHECK(!e.ok());
        RTTI_CHECK_EQ((int)e.err.code, (int)error_code::unexpected_token);
    }
}

RTTI_TEST_CASE(ProtoSchemaMultiFileMerge) {
    using namespace rtti;
    using namespace rtti::proto;
    // Multi-file workflow: parse each file, merge the dependency tables, then
    // compile one descriptor that references types across files.
    const result<schema_desc> a =
        parse_schema("syntax = \"proto3\"; message A { int32 x = 1; }\n");
    RTTI_CHECK(a.ok());
    const result<schema_desc> b = parse_schema(
        "syntax = \"proto3\"; import \"a.proto\"; message B { A a = 1; int32 "
        "y = 2; }\n");
    RTTI_CHECK(b.ok());
    schema_desc merged = a.value;
    rtti::proto::merge_schemas(merged, b.value);
    RTTI_CHECK_EQ(merged.messages.size(), size_t(2));
    RTTI_CHECK_EQ(merged.imports.size(), size_t(1));
    RTTI_CHECK_EQ(merged.imports[0], "a.proto");

    compile_config cfg;
    cfg.root_name = "B";
    const result<compiled_schema> cs = compile_schema(merged, cfg);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    RTTI_CHECK_EQ(si->count, size_t(2));

    document doc;
    rtti::value_io* ain = doc.make_node();
    ain->make_object();
    ps_set_int(*ain, "x", 5);
    doc.set_key("a", ain);
    ps_set_int(doc, "y", 9);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    document out;
    RTTI_CHECK_EQ(wire_to_tree(wire, si, out), error_code::ok);
    rtti::value_io* na = nullptr;
    RTTI_CHECK(out.get_key("a", na));
    RTTI_CHECK_EQ(na->type(), rtti::value_io::kind::object);
}

RTTI_TEST_CASE(ProtoSchemaEnumDefaultResolve) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto2\";\n"
        "enum Kind { KIND_ZERO = 0; KIND_ONE = 1; KIND_TWO = 2; }\n"
        "message E {\n"
        "  optional Kind first = 1;\n"
        "  optional Kind named = 2 [default = KIND_TWO];\n"
        "  optional Kind numeric = 3 [default = 1];\n"
        "}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.enum_defs.size(), size_t(1));
    RTTI_CHECK_EQ(r.value.enum_defs[0].full_name, "Kind");
    RTTI_CHECK_EQ(r.value.enum_defs[0].values.size(), size_t(3));
    RTTI_CHECK_EQ(r.value.enum_defs[0].values[2].value, 2);

    const auto& fs = r.value.messages[0].fields;
    RTTI_CHECK_EQ(fs.size(), size_t(3));
    RTTI_CHECK(fs[0].is_enum);
    // first value is the implicit default (proto2 semantics)
    RTTI_CHECK(fs[0].has_default);
    RTTI_CHECK_EQ((int)fs[0].dflt_int, 0);
    // named default resolves via the enum table to its numeric value
    RTTI_CHECK(fs[1].has_default);
    RTTI_CHECK_EQ((int)fs[1].dflt_int, 2);
    // numeric default on an enum-typed field applies directly
    RTTI_CHECK(fs[2].has_default);
    RTTI_CHECK_EQ((int)fs[2].dflt_int, 1);

    // An unknown enum value name in [default = NAME] is an error.
    {
        const result<schema_desc> e = parse_schema(
            "syntax = \"proto2\"; message M { enum C { A = 0; } C c = 1 "
            "[default = NOPE]; }\n");
        RTTI_CHECK(!e.ok());
        RTTI_CHECK_EQ((int)e.err.code, (int)error_code::unexpected_token);
    }
}

RTTI_TEST_CASE(ProtoSchemaEnumDefaultWireAndTree) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto2\";\n"
        "enum Kind { KIND_ZERO = 0; KIND_ONE = 1; KIND_TWO = 2; }\n"
        "message E { optional Kind named = 2 [default = KIND_TWO]; }\n";
    const result<compiled_schema> cs = compile_schema(text);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // Absent field decodes to the resolved default integer.
    document out;
    RTTI_CHECK_EQ(wire_to_tree(std::string("", 0), si, out), error_code::ok);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(out.get_key("named", v));
    std::int64_t iv = 0;
    RTTI_CHECK(ps_get_int(v, iv));
    RTTI_CHECK_EQ(iv, int64_t(2));

    // A non-default enum value on the wire decodes to an integer node and
    // re-encodes byte-identically (official protoc: `named: 1` => 10 01).
    // proto2 omits a field that sits AT its declared default, so `named: 2`
    // would encode to zero bytes; use 1 (KIND_ONE) to exercise emission.
    document doc;
    ps_set_int(doc, "named", 1);
    std::string wire;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, wire), error_code::ok);
    RTTI_CHECK_EQ(wire, std::string("\x10\x01", 2));
    document out2;
    RTTI_CHECK_EQ(wire_to_tree(wire, si, out2), error_code::ok);
    std::string w2;
    RTTI_CHECK_EQ(tree_to_wire(&out2, si, w2), error_code::ok);
    RTTI_CHECK_EQ(w2, wire);
}

RTTI_TEST_CASE(ProtoSchemaEscapeSequences) {
    using namespace rtti;
    using namespace rtti::proto;
    // \xNN hex, \uXXXX unicode (UTf-8), octal, and the common letter escapes.
    const char* text =
        "message S {\n"
        "  bytes b = 1 [default = \"\\x00\\x41\\xff\\n\\t\\\\\\\"\\101\"];\n"
        "  string u = 2 [default = \"\\u4f60\\u597d\\x21\"];\n"
        "}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(1));
    const auto& fs = r.value.messages[0].fields;
    RTTI_CHECK(fs[0].has_default);
    const std::string b(fs[0].dflt_str.data(), fs[0].dflt_str.size());
    // "\x00\x41\xff\n\t\\\"\101" (octal 101 = 'A') => 8 bytes.
    const std::string eb("\x00\x41\xFF\n\t\\\"A", 8);
    RTTI_CHECK_EQ(b, eb);
    RTTI_CHECK(fs[1].has_default);
    // "\u4f60\u597d\x21" => UTF-8 "你好!" (6 + 1 bytes)
    const std::string eu("\xE4\xBD\xA0\xE5\xA5\xBD\x21", 7);
    const std::string u(fs[1].dflt_str.data(), fs[1].dflt_str.size());
    RTTI_CHECK_EQ(u, eu);
}

RTTI_TEST_CASE(ProtoSchemaMapDuplicateKeyLastWins) {
    using namespace rtti;
    using namespace rtti::proto;
    // The dynamic path mirrors protoc for duplicate map keys too: merging two
    // entries with the same key keeps only the last one (mirrors the typed
    // insert_or_assign behaviour, I-2).
    const result<compiled_schema> cs = compile_schema(
        "syntax = \"proto3\"; message M { map<string, int32> m = 1; }\n");
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    static const unsigned char w[] = {
        0x0A, 0x05, 0x0A, 0x01, 'a', 0x10, 0x01,       // {"a":1}
        0x0A, 0x06, 0x0A, 0x01, 'a', 0x10, 0xAC, 0x02, // {"a":300}
    };
    document out;
    RTTI_CHECK_EQ(wire_to_tree(
                      rtti::string_view(reinterpret_cast<const char*>(w),
                                        sizeof(w)),
                      si, out),
                  error_code::ok);
    rtti::value_io* nm = nullptr;
    RTTI_CHECK(out.get_key("m", nm));
    RTTI_CHECK_EQ(nm->size(), size_t(1)); // merged, not two entries
    rtti::value_io* e = nm->at(0);
    RTTI_CHECK(e != nullptr);
    rtti::value_io* v = nullptr;
    RTTI_CHECK(e->get_key("value", v));
    std::int64_t val = 0;
    RTTI_CHECK(ps_get_int(v, val));
    RTTI_CHECK_EQ(val, int64_t(300)); // last wins

    // Re-encoding emits the single surviving entry.
    std::string w2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, w2), error_code::ok);
    static const unsigned char one[] = {0x0A, 0x06, 0x0A, 0x01, 'a', 0x10, 0xAC, 0x02};
    RTTI_CHECK_EQ(w2.size(), size_t(sizeof(one)));
    RTTI_CHECK_EQ(std::memcmp(w2.data(), one, sizeof(one)), 0);
}

RTTI_TEST_CASE(ProtoSchemaMinorGuardTests) {
    using namespace rtti;
    using namespace rtti::proto;
    // Octal \0NN: greedy 1-3 octal digits; "\01" is one byte 0x01, not
    // NUL followed by '1'.
    {
        const result<schema_desc> r = parse_schema(
            "message S { bytes b = 1 [default = \"\\01\"]; }\n");
        RTTI_CHECK(r.ok());
        const std::string b(r.value.messages[0].fields[0].dflt_str.data(),
                            r.value.messages[0].fields[0].dflt_str.size());
        RTTI_CHECK_EQ(b, std::string("\x01", 1));
    }
    // \x with no hex digit is an error.
    {
        const result<schema_desc> r = parse_schema(
            "message S { bytes b = 1 [default = \"\\x\"]; }\n");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // \U beyond the Unicode range is an error.
    {
        const result<schema_desc> r = parse_schema(
            "message S { string s = 1 [default = \"\\UFFFFFFFF\"]; }\n");
        RTTI_CHECK(!r.ok());
    }
    // A default symbol on a non-enum scalar is an error.
    {
        const result<schema_desc> r = parse_schema(
            "message M { int32 x = 1 [default = FOO]; }\n");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ((int)r.err.code, (int)error_code::unexpected_token);
    }
    // proto3 requires the first enum value to be zero.
    {
        const result<schema_desc> r = parse_schema(
            "syntax = \"proto3\"; enum E { A = 5; } message M { E e = 1; }\n");
        RTTI_CHECK(!r.ok());
    }
    // map keys must be integral / bool / string (float and bytes rejected).
    {
        const result<schema_desc> r = parse_schema(
            "message M { map<float, int32> m = 1; }\n");
        RTTI_CHECK(!r.ok());
    }
    {
        const result<schema_desc> r = parse_schema(
            "message M { map<bytes, int32> m = 1; }\n");
        RTTI_CHECK(!r.ok());
    }
    // Duplicate enum value numbers (no allow_alias support) are rejected.
    {
        const result<schema_desc> r = parse_schema(
            "enum E { A = 0; B = 0; } message M { E e = 1; }\n");
        RTTI_CHECK(!r.ok());
    }
    // Duplicate enum value names are rejected.
    {
        const result<schema_desc> r = parse_schema(
            "enum E { A = 0; A = 1; } message M { E e = 1; }\n");
        RTTI_CHECK(!r.ok());
    }
}

RTTI_TEST_CASE(ProtoSchemaAdvancedOfficialFixtureRoundTrip) {
    using namespace rtti;
    using namespace rtti::proto;
    // The official protoc 36.2 fixture tests/fixtures/proto/advanced.All.wire
    // exercises map<string,int32> + map<string,string> + oneof scalar zero +
    // bytes + enum + packed in one message. Pin the full 49-byte buffer.
    static const unsigned char official[] = {
        0x0A, 0x09, 0x0A, 0x05, 0x61, 0x6C, 0x70, 0x68, 0x61, 0x10, 0x01, // a:1
        0x0A, 0x09, 0x0A, 0x04, 0x62, 0x65, 0x74, 0x61, 0x10, 0xAC, 0x02, // beta:300
        0x12, 0x0B, 0x0A, 0x02, 0x65, 0x6E, 0x12, 0x05, 0x68, 0x65, 0x6C,
        0x6C, 0x6F,                   // labels["en"]="hello"
        0x18, 0x00,                   // oneof n = 0 (presence)
        0x32, 0x03, 0x01, 0x02, 0xFF, // blob
        0x38, 0x02,                   // kind = KIND_TWO
        0x42, 0x03, 0x01, 0x02, 0x03  // packed [1,2,3]
    };
    const char* text =
        "syntax = \"proto3\";\n"
        "enum Kind { KIND_ZERO = 0; KIND_ONE = 1; KIND_TWO = 2; }\n"
        "message Inner { string label = 1; }\n"
        "message All {\n"
        "  map<string, int32> counters = 1;\n"
        "  map<string, string> labels = 2;\n"
        "  oneof choice { int32 n = 3; string s = 4; Inner msg = 5; }\n"
        "  bytes blob = 6;\n"
        "  Kind kind = 7;\n"
        "  repeated int32 packed = 8 [packed = true];\n"
        "}\n";
    const result<compiled_schema> cs = compile_schema(text,
                                                      [] {
                                                          compile_config c;
                                                          c.root_name = "All";
                                                          return c;
                                                      }());
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    document out;
    RTTI_CHECK_EQ(wire_to_tree(rtti::string_view(
                                   reinterpret_cast<const char*>(official),
                                   sizeof(official)),
                               si, out),
                  error_code::ok);

    // Spot-check the decoded tree: counts, labels, oneof n, blob, kind.
    rtti::value_io* nm = nullptr;
    RTTI_CHECK(out.get_key("counters", nm));
    RTTI_CHECK_EQ(nm->size(), size_t(2));
    RTTI_CHECK(out.get_key("labels", nm));
    RTTI_CHECK_EQ(nm->size(), size_t(1));
    rtti::value_io* nn = nullptr;
    RTTI_CHECK(out.get_key("n", nn));
    std::int64_t nval = -1;
    RTTI_CHECK(ps_get_int(nn, nval));
    RTTI_CHECK_EQ(nval, int64_t(0));
    RTTI_CHECK(!out.get_key("s", nn));
    RTTI_CHECK(!out.get_key("msg", nn));
    rtti::value_io* nb = nullptr;
    RTTI_CHECK(out.get_key("blob", nb));
    std::string blob;
    RTTI_CHECK(ps_get_str(nb, blob));
    RTTI_CHECK_EQ(blob, std::string("\x01\x02\xFF", 3));
    rtti::value_io* nk = nullptr;
    RTTI_CHECK(out.get_key("kind", nk));
    std::int64_t kind = 0;
    RTTI_CHECK(ps_get_int(nk, kind));
    RTTI_CHECK_EQ(kind, int64_t(2));

    // Re-encoding the decoded tree is byte-identical to the official wire.
    std::string wire2;
    RTTI_CHECK_EQ(tree_to_wire(&out, si, wire2), error_code::ok);
    RTTI_CHECK_EQ(wire2.size(), sizeof(official));
    RTTI_CHECK_EQ(std::memcmp(wire2.data(), official, sizeof(official)), 0);

    // Building a fresh tree with the same fields (in wire order) also matches.
    document doc;
    rtti::value_io* carr = doc.make_node();
    carr->make_array();
    ps_map_entry(carr, "alpha", 1);
    ps_map_entry(carr, "beta", 300);
    doc.set_key("counters", carr);
    rtti::value_io* larr = doc.make_node();
    larr->make_array();
    ps_str_entry(larr, "en", "hello");
    doc.set_key("labels", larr);
    ps_set_int(doc, "n", 0);
    rtti::value_io* bn = doc.make_node();
    bn->make_scalar(rtti::field_value::of(rtti::string_view("\x01\x02\xFF")));
    doc.set_key("blob", bn);
    ps_set_int(doc, "kind", 2);
    rtti::value_io* parr = doc.make_node();
    parr->make_array();
    {
        rtti::value_io* e1 = doc.make_node();
        e1->make_scalar(rtti::field_value::of(std::int64_t(1)));
        parr->push_back(e1);
        rtti::value_io* e2 = doc.make_node();
        e2->make_scalar(rtti::field_value::of(std::int64_t(2)));
        parr->push_back(e2);
        rtti::value_io* e3 = doc.make_node();
        e3->make_scalar(rtti::field_value::of(std::int64_t(3)));
        parr->push_back(e3);
    }
    doc.set_key("packed", parr);
    std::string w3;
    RTTI_CHECK_EQ(tree_to_wire(&doc, si, w3), error_code::ok);
    RTTI_CHECK_EQ(w3.size(), sizeof(official));
    RTTI_CHECK_EQ(std::memcmp(w3.data(), official, sizeof(official)), 0);
}

} // namespace rtti_test_lib