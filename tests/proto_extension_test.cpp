// proto2/3 extensions runtime tests (Task 4): `extend Target { ... }` block
// parsing (extension descriptors + extension ranges), the ExtensionRegistry
// (field-number registration, name/type/packing info, number/name conflict
// detection), wire-side extension encode/decode via the registry (a registered
// field number decodes to a NAMED field, an unregistered one passes through as
// an unknown field), message-typed and repeated extensions (minimal support),
// and the §0 error paths (truncated payload -> error_code, wire-type mismatch
// tolerated as unknown). The golden bytes are the official protoc 36.2 fixture
// tests/fixtures/proto/ext.Base.wire (embedded copy + disk read).
// English comments only. Cross-TU unique names use the ProtoExt prefix.

#include "test_harness.hpp"
#include <rtti/proto_schema.hpp>
#include <rtti/error.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace rtti_test_lib {

namespace {

std::string ex_read_file(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string ex_pick_path(std::initializer_list<const char*> paths) {
    for (const char* p : paths) {
        if (ex_read_file(p).empty()) {
            continue;
        }
        return std::string(p);
    }
    return std::string();
}

inline std::string ex_sv(rtti::string_view s) {
    return std::string(s.data(), s.size());
}

// Handy document node setters.
inline void ex_set_int(rtti::value_io& doc, const char* key, std::int64_t v) {
    rtti::value_io* n = doc.make_node();
    n->make_scalar(rtti::field_value::of(v));
    doc.set_key(rtti::string_view(key), n);
}
inline void ex_set_str(rtti::value_io& doc, const char* key,
                       const char* s) {
    rtti::value_io* n = doc.make_node();
    n->make_scalar(rtti::field_value::of(rtti::string_view(s)));
    doc.set_key(rtti::string_view(key), n);
}
inline void ex_set_bool(rtti::value_io& doc, const char* key, bool b) {
    rtti::value_io* n = doc.make_node();
    n->make_scalar(rtti::field_value::of(b));
    doc.set_key(rtti::string_view(key), n);
}
inline void ex_set_real(rtti::value_io& doc, const char* key, double d) {
    rtti::value_io* n = doc.make_node();
    n->make_scalar(rtti::field_value::of(d));
    doc.set_key(rtti::string_view(key), n);
}
inline void ex_set_raw(rtti::value_io& doc, const char* key, const char* p,
                       std::size_t n) {
    rtti::value_io* nd = doc.make_node();
    nd->make_scalar(rtti::field_value::of(rtti::string_view(p, n)));
    doc.set_key(rtti::string_view(key), nd);
}

inline bool ex_get_int(const rtti::value_io* doc, const char* key,
                       std::int64_t& out) {
    rtti::value_io* n = nullptr;
    if (doc == nullptr || !doc->get_key(rtti::string_view(key), n) ||
        n == nullptr) {
        return false;
    }
    if (n->type() == rtti::value_io::kind::boolean) {
        bool b = false;
        if (!n->as_bool(b)) {
            return false;
        }
        out = b ? 1 : 0;
        return true;
    }
    if (n->type() == rtti::value_io::kind::integer) {
        return n->as_integer(out);
    }
    return false;
}
inline bool ex_get_str(const rtti::value_io* doc, const char* key,
                       std::string& out) {
    rtti::value_io* n = nullptr;
    if (doc == nullptr || !doc->get_key(rtti::string_view(key), n) ||
        n == nullptr) {
        return false;
    }
    rtti::string_view s;
    if (!n->as_string(s)) {
        return false;
    }
    out.assign(s.data(), s.size());
    return true;
}
inline bool ex_get_real(const rtti::value_io* doc, const char* key,
                        double& out) {
    rtti::value_io* n = nullptr;
    if (doc == nullptr || !doc->get_key(rtti::string_view(key), n) ||
        n == nullptr) {
        return false;
    }
    return n->as_real(out);
}
inline bool ex_get_bool(const rtti::value_io* doc, const char* key,
                        bool& out) {
    rtti::value_io* n = nullptr;
    if (doc == nullptr || !doc->get_key(rtti::string_view(key), n) ||
        n == nullptr) {
        return false;
    }
    return n->as_bool(out);
}

// The official protoc 36.2 encode of rtti.fixt.Base (tests/fixtures/proto/
// ext.Base.wire) for the fixture textbin: own=7, extra=42, who="rtti",
// tags=[1,2,3] (packed), ptr{x:9}, kind=KIND_ONE, z=-5, ratio=1.5, ok=true.
static const unsigned char kOfficialWire[] = {
    0x08, 0x07,                               // own: 7
    0xa0, 0x06, 0x2a,                         // extra: 42
    0xaa, 0x06, 0x04, 'r', 't', 't', 'i',     // who: "rtti"
    0xb2, 0x06, 0x03, 0x01, 0x02, 0x03,       // tags: [1,2,3] packed
    0xba, 0x06, 0x02, 0x08, 0x09,             // ptr: { x: 9 }
    0xc0, 0x06, 0x01,                         // kind: KIND_ONE
    0xc8, 0x06, 0x09,                         // z: -5 (zigzag -> 9)
    0xd1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xf8, 0x3f,                         // ratio: 1.5 (fixed64)
    0xd8, 0x06, 0x01                          // ok: true
};

} // namespace

RTTI_TEST_CASE(ProtoExtParseExtendBlock) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto2\";\n"
        "package p;\n"
        "message Ptr { optional int32 x = 1; }\n"
        "enum Kind { ZERO = 0; ONE = 1; }\n"
        "message Base { extensions 100 to 199; optional int32 own = 1; }\n"
        "extend Base {\n"
        "  optional int32 extra = 100;\n"
        "  repeated string tags = 101;\n"
        "  optional Ptr ptr = 102;\n"
        "  optional Kind kind = 103;\n"
        "}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    // Extension range recorded on Base (messages keep declaration order).
    RTTI_CHECK_EQ(r.value.messages.size(), size_t(2));
    const schema_message* base = nullptr;
    for (const schema_message& m : r.value.messages) {
        if (m.name == "Base") {
            base = &m;
            break;
        }
    }
    RTTI_CHECK(base != nullptr);
    RTTI_CHECK_EQ(base->full_name, "Base");
    RTTI_CHECK_EQ(base->extension_ranges.size(), size_t(1));
    RTTI_CHECK_EQ(base->extension_ranges[0].first, uint32_t(100));
    RTTI_CHECK_EQ(base->extension_ranges[0].second, uint32_t(199));
    // Extend blocks captured as extension descriptors.
    RTTI_CHECK_EQ(r.value.extensions.size(), size_t(4));
    RTTI_CHECK_EQ(r.value.extensions[0].extendee, "Base");
    RTTI_CHECK_EQ(r.value.extensions[0].field.name, "extra");
    RTTI_CHECK_EQ(r.value.extensions[0].field.number, uint32_t(100));
    RTTI_CHECK_EQ(static_cast<int>(r.value.extensions[0].field.type),
                  static_cast<int>(scalar_type::int32));
    RTTI_CHECK(r.value.extensions[0].field.is_message == false);
    RTTI_CHECK_EQ(r.value.extensions[1].field.name, "tags");
    RTTI_CHECK_EQ(static_cast<int>(r.value.extensions[1].field.mode),
                  static_cast<int>(field_mode::repeated));
    RTTI_CHECK_EQ(static_cast<int>(r.value.extensions[1].field.type),
                  static_cast<int>(scalar_type::string_val));
    // Message-typed extension.
    RTTI_CHECK(r.value.extensions[2].field.is_message);
    RTTI_CHECK_EQ(r.value.extensions[2].field.type_ref, "Ptr");
    RTTI_CHECK_EQ(r.value.extensions[2].extendee, "Base");
    // Enum-typed extension.
    RTTI_CHECK(r.value.extensions[3].field.is_enum);
}

RTTI_TEST_CASE(ProtoExtParseNestedExtendAndRanges) {
    using namespace rtti;
    using namespace rtti::proto;
    const char* text =
        "syntax = \"proto2\";\n"
        "package p;\n"
        "message Base { extensions 1000 to max; }\n"
        "message Holder {\n"
        "  extend Base { optional string s = 1000; }\n"
        "}\n";
    const result<schema_desc> r = parse_schema(text);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.extensions.size(), size_t(1));
    RTTI_CHECK_EQ(r.value.extensions[0].extendee, "Base");
    RTTI_CHECK_EQ(r.value.extensions[0].field.name, "s");
    RTTI_CHECK_EQ(r.value.extensions[0].field.number, uint32_t(1000));
    RTTI_CHECK_EQ(static_cast<int>(r.value.extensions[0].field.type),
                  static_cast<int>(scalar_type::string_val));
    // `to max` expands to the maximum field number.
    const schema_message* base = nullptr;
    for (const schema_message& m : r.value.messages) {
        if (m.name == "Base") {
            base = &m;
            break;
        }
    }
    RTTI_CHECK(base != nullptr);
    RTTI_CHECK_EQ(base->extension_ranges.size(), size_t(1));
    RTTI_CHECK_EQ(base->extension_ranges[0].first, uint32_t(1000));
    RTTI_CHECK_EQ(base->extension_ranges[0].second, uint32_t(0x1FFFFFFF));
}

RTTI_TEST_CASE(ProtoExtRegistryRegisterFind) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    RTTI_CHECK_EQ(extension_registry::global().size(), size_t(0));

    extension_desc d;
    d.number = 100;
    d.name = "extra";
    d.wire = wire_type::varint;
    const error_code e = register_extension(d);
    RTTI_CHECK_EQ(e, error_code::ok);
    RTTI_CHECK_EQ(extension_registry::global().size(), size_t(1));

    const extension_desc* f = extension_registry::global().find(100);
    RTTI_CHECK(f != nullptr);
    RTTI_CHECK_EQ(f->name, "extra");
    RTTI_CHECK_EQ(f->number, uint32_t(100));
    RTTI_CHECK(extension_registry::global().find(99) == nullptr);

    const extension_desc* fb = extension_registry::global().find_by_name("extra");
    RTTI_CHECK(fb != nullptr);
    RTTI_CHECK_EQ(fb->number, uint32_t(100));
    RTTI_CHECK(extension_registry::global().find_by_name("nope") == nullptr);
    extension_registry::global().clear();
    RTTI_CHECK_EQ(extension_registry::global().size(), size_t(0));
}

RTTI_TEST_CASE(ProtoExtRegistryConflictDetection) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    extension_desc a;
    a.number = 100;
    a.name = "extra";
    a.wire = wire_type::varint;
    RTTI_CHECK_EQ(register_extension(a), error_code::ok);
    // Registering the identical descriptor again is a no-op (idempotent).
    RTTI_CHECK_EQ(register_extension(a), error_code::ok);
    RTTI_CHECK_EQ(extension_registry::global().size(), size_t(1));
    // Same field number, different name -> conflict.
    extension_desc b = a;
    b.name = "other";
    RTTI_CHECK_EQ(register_extension(b), error_code::proto_bad_field);
    // Same name, different field number -> conflict.
    extension_desc c = a;
    c.number = 101;
    RTTI_CHECK_EQ(register_extension(c), error_code::proto_bad_field);
    RTTI_CHECK_EQ(extension_registry::global().size(), size_t(1));
    // A completely new number/name registers fine.
    extension_desc d;
    d.number = 101;
    d.name = "who";
    d.wire = wire_type::length;
    RTTI_CHECK_EQ(register_extension(d), error_code::ok);
    RTTI_CHECK_EQ(extension_registry::global().size(), size_t(2));
    extension_registry::global().clear();
}

RTTI_TEST_CASE(ProtoExtRegisterBadNumber) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    extension_desc z;
    z.number = 0;
    z.name = "bad";
    RTTI_CHECK_EQ(register_extension(z), error_code::proto_bad_field);
    extension_desc big;
    big.number = 0x20000000u; // > 2^29 - 1
    big.name = "big";
    RTTI_CHECK_EQ(register_extension(big), error_code::proto_bad_field);
    RTTI_CHECK_EQ(extension_registry::global().size(), size_t(0));
}

RTTI_TEST_CASE(ProtoExtWireDecodeRegisteredByFieldNumber) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    // Schema knows only `own`; the registry supplies `extra` = field 100.
    const char* text = "syntax = \"proto2\"; message B { optional int32 own = 1; }\n";
    const auto p = parse_schema(text);
    RTTI_CHECK(p.ok());
    const auto cs = compile_schema(p.value);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();
    RTTI_CHECK(si != nullptr);

    extension_desc d;
    d.number = 100;
    d.name = "extra";
    d.wire = wire_type::varint;
    d.extendee = "B";
    RTTI_CHECK_EQ(register_extension(d), error_code::ok);

    // own=7 (08 07) + field-100 varint 42 (a0 06 2a).
    static const unsigned char wire[] = {0x08, 0x07, 0xa0, 0x06, 0x2a};
    const result<document> r =
        parse(rtti::string_view(reinterpret_cast<const char*>(wire), sizeof(wire)),
              si);
    RTTI_CHECK(r.ok());
    std::int64_t own = 0;
    RTTI_CHECK(ex_get_int(&r.value, "own", own));
    RTTI_CHECK_EQ(own, std::int64_t(7));
    // The extension decodes as a NAMED field, not a numeric "100" key.
    std::int64_t extra = 0;
    RTTI_CHECK(ex_get_int(&r.value, "extra", extra));
    RTTI_CHECK_EQ(extra, std::int64_t(42));
    rtti::value_io* numeric = nullptr;
    RTTI_CHECK(!r.value.get_key(rtti::string_view("100"), numeric));

    // Re-encoding the tree reproduces the same wire bytes.
    std::string out;
    RTTI_CHECK_EQ(tree_to_wire(&r.value, si, out), error_code::ok);
    RTTI_CHECK_EQ(out.size(), sizeof(wire));
    RTTI_CHECK_EQ(std::memcmp(out.data(), wire, sizeof(wire)), 0);
    extension_registry::global().clear();
}

RTTI_TEST_CASE(ProtoExtUnregisteredUnknownPassthrough) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    const char* text = "syntax = \"proto2\"; message B { optional int32 own = 1; }\n";
    const auto p = parse_schema(text);
    RTTI_CHECK(p.ok());
    const auto cs = compile_schema(p.value);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // Field 100 is NOT registered: it is tolerated as an unknown field (the
    // current unknown-field semantics), so parsing succeeds and the field is
    // absent from the tree.
    static const unsigned char wire[] = {0x08, 0x07, 0xa0, 0x06, 0x2a};
    const result<document> r =
        parse(rtti::string_view(reinterpret_cast<const char*>(wire), sizeof(wire)),
              si);
    RTTI_CHECK(r.ok());
    std::int64_t own = 0;
    RTTI_CHECK(ex_get_int(&r.value, "own", own));
    RTTI_CHECK_EQ(own, std::int64_t(7));
    std::int64_t extra = 0;
    RTTI_CHECK(!ex_get_int(&r.value, "extra", extra));
}

RTTI_TEST_CASE(ProtoExtWireTypeMismatchTolerated) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    const char* text = "syntax = \"proto2\"; message B { optional int32 own = 1; }\n";
    const auto p = parse_schema(text);
    RTTI_CHECK(p.ok());
    const auto cs = compile_schema(p.value);
    RTTI_CHECK(cs.ok());

    // Registered as varint but the wire carries field 100 as length-delimited:
    // protoc-style tolerance -> treated as unknown, no error, no crash.
    extension_desc d;
    d.number = 100;
    d.name = "extra";
    d.wire = wire_type::varint;
    RTTI_CHECK_EQ(register_extension(d), error_code::ok);

    static const unsigned char wire[] = {0x08, 0x07, 0xa2, 0x06, 0x01, 0x2a};
    const result<document> r =
        parse(rtti::string_view(reinterpret_cast<const char*>(wire), sizeof(wire)),
              cs.value.info());
    RTTI_CHECK(r.ok());
    std::int64_t extra = 0;
    RTTI_CHECK(!ex_get_int(&r.value, "extra", extra));
    extension_registry::global().clear();
}

RTTI_TEST_CASE(ProtoExtMalformedExtensionPayloadTruncated) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    const char* text = "syntax = \"proto2\"; message B { optional int32 own = 1; }\n";
    const auto p = parse_schema(text);
    RTTI_CHECK(p.ok());
    const auto cs = compile_schema(p.value);
    RTTI_CHECK(cs.ok());

    extension_desc d;
    d.number = 100;
    d.name = "extra";
    d.wire = wire_type::varint;
    RTTI_CHECK_EQ(register_extension(d), error_code::ok);

    // Field-100 tag with a truncated varint payload: error_code, no crash.
    static const unsigned char wire[] = {0xa0, 0x06};
    const result<document> r =
        parse(rtti::string_view(reinterpret_cast<const char*>(wire), sizeof(wire)),
              cs.value.info());
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ(r.err.code, error_code::proto_truncated);
    extension_registry::global().clear();
}

RTTI_TEST_CASE(ProtoExtMessageTypedExtensionRoundtrip) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    // Minimal support: a message-typed extension round-trips its raw payload
    // (opaque length-delimited bytes), matching the imported-file behavior.
    extension_desc d;
    d.number = 103;
    d.name = "ptr";
    d.wire = wire_type::length;
    d.is_message = true;
    d.extendee = "Base";
    RTTI_CHECK_EQ(register_extension(d), error_code::ok);

    const char* text = "syntax = \"proto2\"; message B { optional int32 own = 1; }\n";
    const auto p = parse_schema(text);
    RTTI_CHECK(p.ok());
    const auto cs = compile_schema(p.value);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // ptr = { x: 9 } => length payload [08 09].
    static const unsigned char wire[] = {0xba, 0x06, 0x02, 0x08, 0x09};
    const result<document> r =
        parse(rtti::string_view(reinterpret_cast<const char*>(wire), sizeof(wire)),
              si);
    RTTI_CHECK(r.ok());
    std::string raw;
    RTTI_CHECK(ex_get_str(&r.value, "ptr", raw));
    RTTI_CHECK_EQ(raw.size(), size_t(2));
    RTTI_CHECK_EQ(static_cast<unsigned char>(raw[0]), 0x08u);
    RTTI_CHECK_EQ(static_cast<unsigned char>(raw[1]), 0x09u);
    // Re-encode reproduces the exact bytes.
    std::string out;
    RTTI_CHECK_EQ(tree_to_wire(&r.value, si, out), error_code::ok);
    RTTI_CHECK_EQ(out.size(), sizeof(wire));
    RTTI_CHECK_EQ(std::memcmp(out.data(), wire, sizeof(wire)), 0);
    extension_registry::global().clear();
}

RTTI_TEST_CASE(ProtoExtRepeatedExtensionRoundtrip) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();
    extension_desc d;
    d.number = 102;
    d.name = "tags";
    d.wire = wire_type::varint;
    d.mode = field_mode::repeated;
    d.packed = true;
    d.extendee = "Base";
    RTTI_CHECK_EQ(register_extension(d), error_code::ok);

    const char* text = "syntax = \"proto2\"; message B { optional int32 own = 1; }\n";
    const auto p = parse_schema(text);
    RTTI_CHECK(p.ok());
    const auto cs = compile_schema(p.value);
    RTTI_CHECK(cs.ok());
    const schema_info* si = cs.value.info();

    // Packed repeated extension: field 102, length 3, [1,2,3].
    static const unsigned char wire[] = {0xb2, 0x06, 0x03, 0x01, 0x02, 0x03};
    const result<document> r =
        parse(rtti::string_view(reinterpret_cast<const char*>(wire), sizeof(wire)),
              si);
    RTTI_CHECK(r.ok());
    rtti::value_io* arr = nullptr;
    RTTI_CHECK(r.value.get_key(rtti::string_view("tags"), arr));
    RTTI_CHECK(arr != nullptr);
    RTTI_CHECK_EQ(arr->size(), size_t(3));
    std::int64_t v = 0;
    if (arr != nullptr && arr->size() == size_t(3)) {
        RTTI_CHECK(arr->at(0)->as_integer(v) && v == 1);
        RTTI_CHECK(arr->at(1)->as_integer(v) && v == 2);
        RTTI_CHECK(arr->at(2)->as_integer(v) && v == 3);
    }
    std::string out;
    RTTI_CHECK_EQ(tree_to_wire(&r.value, si, out), error_code::ok);
    RTTI_CHECK_EQ(out.size(), sizeof(wire));
    RTTI_CHECK_EQ(std::memcmp(out.data(), wire, sizeof(wire)), 0);
    extension_registry::global().clear();
}

RTTI_TEST_CASE(ProtoExtOfficialFixtureRoundtrip) {
    using namespace rtti;
    using namespace rtti::proto;
    extension_registry::global().clear();

    // .proto text mirrors tests/fixtures/proto/ext.proto (built-in copy).
    const char* proto =
        "syntax = \"proto2\";\n"
        "package rtti.fixt;\n"
        "message Ptr { optional int32 x = 1; }\n"
        "enum Kind { KIND_ZERO = 0; KIND_ONE = 1; }\n"
        "message Base { extensions 100 to 199; optional int32 own = 1; }\n"
        "extend Base {\n"
        "  optional int32 extra = 100;\n"
        "  optional string who = 101;\n"
        "  repeated int32 tags = 102 [packed = true];\n"
        "  optional Ptr ptr = 103;\n"
        "  optional Kind kind = 104;\n"
        "  optional sint64 z = 105;\n"
        "  optional double ratio = 106;\n"
        "  optional bool ok = 107;\n"
        "}\n";
    const auto p = parse_schema(proto);
    RTTI_CHECK(p.ok());
    RTTI_CHECK_EQ(p.value.extensions.size(), size_t(8));
    compile_config cfg;
    cfg.root_name = "Base";
    const auto cs = compile_schema(p.value, cfg);
    RTTI_CHECK(cs.ok());
    // Compile registers the parsed extensions into the global registry.
    RTTI_CHECK_EQ(extension_registry::global().size(), size_t(8));

    // Load the official wire fixture (disk, with embedded fallback).
    const std::string wire_path = ex_pick_path(
        {"tests/fixtures/proto/ext.Base.wire",
         "./tests/fixtures/proto/ext.Base.wire",
         "../tests/fixtures/proto/ext.Base.wire"});
    std::string wirestr = wire_path.empty()
                              ? std::string(reinterpret_cast<const char*>(
                                                kOfficialWire),
                                            sizeof(kOfficialWire))
                              : ex_read_file(wire_path);
    RTTI_CHECK_EQ(wirestr.size(), sizeof(kOfficialWire));
    RTTI_CHECK_EQ(std::memcmp(wirestr.data(), kOfficialWire, sizeof(kOfficialWire)),
                  0);

    const result<document> r =
        parse(rtti::string_view(wirestr.data(), wirestr.size()), cs.value.info());
    RTTI_CHECK(r.ok());
    std::int64_t vi = 0;
    RTTI_CHECK(ex_get_int(&r.value, "own", vi));
    RTTI_CHECK_EQ(vi, std::int64_t(7));
    RTTI_CHECK(ex_get_int(&r.value, "extra", vi));
    RTTI_CHECK_EQ(vi, std::int64_t(42));
    std::string vs;
    RTTI_CHECK(ex_get_str(&r.value, "who", vs));
    RTTI_CHECK_EQ(vs, "rtti");
    rtti::value_io* arr = nullptr;
    RTTI_CHECK(r.value.get_key(rtti::string_view("tags"), arr));
    RTTI_CHECK(arr != nullptr && arr->size() == size_t(3));
    if (arr != nullptr && arr->size() == size_t(3)) {
        RTTI_CHECK(arr->at(0)->as_integer(vi) && vi == 1);
        RTTI_CHECK(arr->at(2)->as_integer(vi) && vi == 3);
    }
    std::string raw;
    RTTI_CHECK(ex_get_str(&r.value, "ptr", raw));
    RTTI_CHECK_EQ(raw.size(), size_t(2));
    RTTI_CHECK_EQ(static_cast<unsigned char>(raw[0]), 0x08u);
    RTTI_CHECK(ex_get_int(&r.value, "kind", vi));
    RTTI_CHECK_EQ(vi, std::int64_t(1));
    RTTI_CHECK(ex_get_int(&r.value, "z", vi));
    RTTI_CHECK_EQ(vi, std::int64_t(-5));
    double vd = 0.0;
    RTTI_CHECK(ex_get_real(&r.value, "ratio", vd));
    RTTI_CHECK(vd == 1.5);
    bool okf = false;
    RTTI_CHECK(ex_get_bool(&r.value, "ok", okf));
    RTTI_CHECK(okf);

    // Encoding the freshly decoded tree reproduces the exact official bytes
    // (the wire is decoded in wire order and re-encoded in the same order).
    std::string back;
    RTTI_CHECK_EQ(tree_to_wire(&r.value, cs.value.info(), back), error_code::ok);
    RTTI_CHECK_EQ(back.size(), wirestr.size());
    RTTI_CHECK_EQ(std::memcmp(back.data(), wirestr.data(), wirestr.size()), 0);

    extension_registry::global().clear();
    // After clearing the registry the same wire parses with the extension
    // tolerated as an unknown field (no named key, no error).
    const result<document> r2 =
        parse(rtti::string_view(wirestr.data(), wirestr.size()), cs.value.info());
    RTTI_CHECK(r2.ok());
    std::int64_t gone = 0;
    RTTI_CHECK(!ex_get_int(&r2.value, "extra", gone));
    extension_registry::global().clear();
}

} // namespace rtti_test_lib

RTTI_TEST_MAIN()