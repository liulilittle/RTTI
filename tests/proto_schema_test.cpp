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

} // namespace rtti_test_lib