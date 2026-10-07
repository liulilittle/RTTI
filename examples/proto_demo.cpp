// =============================================================================
// proto_demo.cpp -- end-to-end application example for the RTTI protobuf
// wire-format adapter (<rtti/proto.hpp>).
//
// PURPOSE
//   This is a *readable teaching program*, not a benchmark. Every public API
//   call of the protobuf adapter is exercised in a small realistic scenario
//   ("network telemetry + user profile") and every call is preceded by a
//   comment block explaining: what it does, why we call it, what it returns,
//   and how errors are reported and handled. The library never throws for
//   malformed input; every failure comes back as rtti::error_code and is
//   printed, never crashed.
//
// DEMONSTRATED CAPABILITIES
//   1. schema<T> explicit registration (make_field / make_field_desc:
//      field number + wire type + mode + packed flag + proto2 default).
//   2. serialize<T> / parse_as<T> wire-byte round trip, with a hex dump of
//      the produced wire bytes.
//   3. wire_to_tree / tree_to_wire and JSON interop through the shared
//      rtti::value_io tree interface (both directions).
//   4. repeated (packed and unpacked) numeric fields and repeated message
//      fields; protobuf map entries (key=1 / value=2).
//   5. proto2 required fields and explicit defaults; proto3 scalar default
//      omission on the wire ("setdefault" behavior).
//   6. SAX streaming parse_stream wire events, including early cancellation.
//   7. Malformed-wire error handling: truncated varint, over-long length
//      prefix, wire-type mismatch, illegal field number 0, group wire types,
//      and the deep-nesting DoS guard (too_deep).
//
// DEPENDENCIES
//   <rtti/proto.hpp>   -- the protobuf adapter (pulls in convert/error/
//                         reflection from the reflect core)
//   <rtti/json.hpp>    -- only used as a *value_io tree* (the JSON adapter's
//                         document implements the same rtti::value_io
//                         interface), to demonstrate C++ <-> wire <-> JSON
//                         interop. JSON is enabled by default (RTTI_ENABLE_JSON).
//
// COMPILE GATE
//   proto.hpp #errors unless RTTI_ENABLE_PROTO evaluates true (default 1 via
//   <rtti/rtti_fwd.hpp>). This demo therefore requires the default gate; no
//   extra -D is needed. The reflect core itself never includes proto.hpp.
//
// BUILD / RUN (verified with MSVC 14.44.35207, Debug, x64):
//   call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
//   cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd ^
//      /I F:\json_xml\include examples\proto_demo.cpp /link ^
//      /LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\lib\x64" ^
//      /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" ^
//      /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" ^
//      build\Debug\rtti.lib
//   examples\proto_demo.exe
//
//   Expected: zero compiler warnings (/W4), program prints each section, all
//   round trips verify, exit code 0.
//
// ALL TYPE NAMES ARE PREFIXED "DemoProto" SO THIS TU NEVER COLLIDES WITH THE
// PARALLEL EXAMPLE PROGRAMS (json/xml/ini/toml/proto/core) AT LINK TIME.
// English comments only; C++17; zero third-party dependencies.
// =============================================================================

#include <rtti/proto.hpp>
#include <rtti/json.hpp>

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// =============================================================================
// SECTION 1 -- message types (plain C++ structs)
// =============================================================================
//
// protobuf does not extend the RTTI_DEFINE_TYPE macro family. A struct is
// registered for *reflection* with the normal macro (so convert/to_value/
// from_value and the JSON adapters can walk it), and the *protobuf schema* is
// registered separately below by specializing rtti::proto::schema<T>. The two
// tables are independent: reflection knows field names/order, the proto
// schema knows field numbers/wire types/modes.

// -----------------------------------------------------------------------------
// Enum labels used by the telemetry message (declared BEFORE the structs that
// reference them).
// RTTI_DEFINE_ENUM builds rtti::enum_table<DemoProtoKind>, giving:
//   * rtti::enum_to_string(value)  -> const char* label (or "" when unknown)
//   * rtti::enum_from_string(label) -> enum value (first match / default)
//   * the convert dispatch that lets a format tree store enums as their label
//     text (e.g. JSON "kind": "fail") instead of a raw number.
// -----------------------------------------------------------------------------
enum class DemoProtoKind : int { ok = 0, warn = 1, fail = 2 };

// The enum label table must be registered at namespace scope (an explicit
// specialization cannot live inside a function or class).
RTTI_DEFINE_ENUM(DemoProtoKind, ok, warn, fail)

// A proto3 "scalar" message that covers all four wire kinds the adapter
// supports (varint, fixed64, fixed32, length-delimited), plus a registered
// enum member and a nested-message slot.
struct DemoProtoTelemetry {
    bool flag = false;             // wire type varint  (field 1)
    std::int32_t i32 = 0;          // wire type varint  (field 2)
    std::int64_t i64 = 0;          // wire type varint  (field 3)
    std::uint64_t u64 = 0;         // wire type varint  (field 4)
    double ratio = 0.0;            // wire type fixed64 (field 5)
    float scale = 0.0f;            // wire type fixed32 (field 6)
    std::string label;             // wire type length  (field 7)
    DemoProtoKind kind;            // wire type varint  (field 8), enum
    DemoProtoTelemetry* self;      // NOT registered: proves only listed members
                                   // participate in the proto wire format.
    RTTI_DEFINE_TYPE_INTRUSIVE(DemoProtoTelemetry, flag, i32, i64, u64, ratio,
                               scale, label, kind)
};

// A proto3 message with packed repeated numeric fields. "packed" means the
// adapter writes ONE length-delimited payload holding all element varints
// (the standard protobuf optimization for repeated scalars).
struct DemoProtoWave {
    std::vector<std::int32_t> samples;  // field 1, repeated, packed
    std::vector<std::int64_t> stamps;   // field 2, repeated, packed

    RTTI_DEFINE_TYPE_INTRUSIVE(DemoProtoWave, samples, stamps)
};

// A proto3 message that nests another message, carries a repeated *message*
// list (unpacked: each element keeps its own tag + length prefix) and a
// protobuf map (repeated length-delimited entry messages, key field 1 /
// value field 2).
struct DemoProtoEnvelope {
    std::int32_t tag = 0;
    DemoProtoTelemetry inner;                       // field 2, nested message
    std::vector<DemoProtoTelemetry> events;         // field 3, repeated message
    std::map<std::string, std::int32_t> counters;   // field 4, protobuf map

    RTTI_DEFINE_TYPE_INTRUSIVE(DemoProtoEnvelope, tag, inner, events, counters)
};

// A proto2 message: proto2 semantics differ from proto3 in that optional
// fields may carry an explicit default value, required fields MUST be present
// on the wire, and defaults are applied to absent optional fields at decode.
struct DemoProtoProfile {
    std::int32_t id = 0;       // field 1, REQUIRED
    std::string name;          // field 2, optional, default "anon"
    std::int32_t age = 0;      // field 3, optional, default 42
    bool active = false;       // field 4, optional, default true
    double score = 0.0;        // field 5, optional, default 1.5

    RTTI_DEFINE_TYPE_INTRUSIVE(DemoProtoProfile, id, name, age, active, score)
};

// A self-referential message used only to demonstrate the depth DoS guard.
struct DemoProtoNode {
    std::int32_t id = 0;
    std::vector<DemoProtoNode> children;
};

// -----------------------------------------------------------------------------
// Non-intrusive reflection registration for the self-referential node type
// (kept POD-clean; the enum table was registered in Section 1).
// -----------------------------------------------------------------------------
RTTI_DEFINE_TYPE(DemoProtoNode, id, children)

// =============================================================================
// SECTION 2 -- explicit protobuf schema registration
// =============================================================================
//
// rtti::proto::schema<T> is a template that the user specializes for each
// message struct. The adapter reads `syn` (proto2 vs proto3), `count` and
// `array` (the ordered field descriptors). The PRIMARY template describes an
// empty proto3 message, so an unregistered struct serializes to zero bytes
// and parses back as its default-constructed value -- registration is
// strictly opt-in.
//
// make_field<T, &T::member>(field_desc)  binds one reflected member to its
// descriptor. Each distinct (T, member pointer) owns a static descriptor
// object, so two same-typed members never alias.
//
// make_field_desc(name, number, wire_type [, mode] [, packed]) builds the
// descriptor; with_default_int/with_default_real/with_default_str attach a
// proto2 explicit default value to it.
//
// field_mode::optional   -- absent on the wire -> default applied at decode
// field_mode::required   -- absence is an ERROR (missing_key)
// field_mode::repeated   -- container member (vector/set/map)
//
// wire_type::varint      -- bool/int32/int64/uint32/uint64/enum
// wire_type::fixed64     -- double / int64-as-fixed
// wire_type::fixed32     -- float / int32-as-fixed
// wire_type::length      -- string / nested message / bytes

namespace rtti {
namespace proto {

// --- proto3 telemetry: every scalar is optional (the default proto3 rule) ---
template <>
struct schema<DemoProtoTelemetry> {
    static constexpr std::size_t count = 8;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        // bool on the wire is a single varint byte 0 or 1.
        make_field<DemoProtoTelemetry, &DemoProtoTelemetry::flag>(
            make_field_desc("flag", 1, wire_type::varint)),
        // signed 32-bit integer encoded as a plain varint.
        make_field<DemoProtoTelemetry, &DemoProtoTelemetry::i32>(
            make_field_desc("i32", 2, wire_type::varint)),
        make_field<DemoProtoTelemetry, &DemoProtoTelemetry::i64>(
            make_field_desc("i64", 3, wire_type::varint)),
        // uint64 values above INT64_MAX survive the *typed* wire path exactly
        // (the value_io tree bridge stores int64 scalars and would lose them;
        // this is documented in proto.hpp).
        make_field<DemoProtoTelemetry, &DemoProtoTelemetry::u64>(
            make_field_desc("u64", 4, wire_type::varint)),
        // IEEE double is 8 little-endian bytes on wire type 1.
        make_field<DemoProtoTelemetry, &DemoProtoTelemetry::ratio>(
            make_field_desc("ratio", 5, wire_type::fixed64)),
        // IEEE float is 4 little-endian bytes on wire type 5.
        make_field<DemoProtoTelemetry, &DemoProtoTelemetry::scale>(
            make_field_desc("scale", 6, wire_type::fixed32)),
        // std::string uses a length prefix (wire type 2).
        make_field<DemoProtoTelemetry, &DemoProtoTelemetry::label>(
            make_field_desc("label", 7, wire_type::length)),
        // Enums encode as their underlying integer via a varint.
        make_field<DemoProtoTelemetry, &DemoProtoTelemetry::kind>(
            make_field_desc("kind", 8, wire_type::varint)),
    };
};

// --- proto3 packed repeated scalars ------------------------------------------
// The 5th make_field_desc argument (packed=true) selects the packed
// representation for numeric repeated members. proto_test shows the exact
// byte layout; here we demonstrate the API and the round trip.
template <>
struct schema<DemoProtoWave> {
    static constexpr std::size_t count = 2;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<DemoProtoWave, &DemoProtoWave::samples>(
            make_field_desc("samples", 1, wire_type::varint,
                            field_mode::repeated, true)),
        make_field<DemoProtoWave, &DemoProtoWave::stamps>(
            make_field_desc("stamps", 2, wire_type::varint,
                            field_mode::repeated, true)),
    };
};

// --- proto3 nested message + repeated message + map ---------------------------
template <>
struct schema<DemoProtoEnvelope> {
    static constexpr std::size_t count = 4;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<DemoProtoEnvelope, &DemoProtoEnvelope::tag>(
            make_field_desc("tag", 1, wire_type::varint)),
        // Nested message members are always structurally present and use the
        // length wire type (their payload is a recursively encoded message).
        make_field<DemoProtoEnvelope, &DemoProtoEnvelope::inner>(
            make_field_desc("inner", 2, wire_type::length)),
        // Repeated MESSAGE member: unpacked by default (packed is only legal
        // for numeric wire types).
        make_field<DemoProtoEnvelope, &DemoProtoEnvelope::events>(
            make_field_desc("events", 3, wire_type::length,
                            field_mode::repeated)),
        // std::map members MUST be registered as repeated; each entry is a
        // length-delimited message with key=1 and value=2.
        make_field<DemoProtoEnvelope, &DemoProtoEnvelope::counters>(
            make_field_desc("counters", 4, wire_type::length,
                            field_mode::repeated)),
    };
};

// --- proto2 required + explicit defaults -------------------------------------
// Note the two different descriptor forms:
//   * plain make_field_desc(...)          -> proto2 default = the C++ zero
//   * with_default_int/real/str(...)      -> an explicit proto2 default
// The adapter applies the default to an ABSENT optional field at decode, and
// omits a field whose current value equals its default at encode.
template <>
struct schema<DemoProtoProfile> {
    static constexpr std::size_t count = 5;
    static constexpr syntax syn = syntax::proto2;
    static inline const field_base* const array[count] = {
        // required: decoding a wire stream without field 1 is missing_key.
        make_field<DemoProtoProfile, &DemoProtoProfile::id>(
            make_field_desc("id", 1, wire_type::varint,
                            field_mode::required)),
        // default string "anon" (length 4).
        make_field<DemoProtoProfile, &DemoProtoProfile::name>(
            with_default_str(make_field_desc("name", 2, wire_type::length),
                             "anon", 4)),
        // default integer 42.
        make_field<DemoProtoProfile, &DemoProtoProfile::age>(
            with_default_int(make_field_desc("age", 3, wire_type::varint), 42)),
        // default bool true (stored as default int 1).
        make_field<DemoProtoProfile, &DemoProtoProfile::active>(
            with_default_int(make_field_desc("active", 4, wire_type::varint),
                             1)),
        // default double 1.5.
        make_field<DemoProtoProfile, &DemoProtoProfile::score>(
            with_default_real(make_field_desc("score", 5, wire_type::fixed64),
                              1.5)),
    };
};

// --- self-referential message for the depth guard -----------------------------
template <>
struct schema<DemoProtoNode> {
    static constexpr std::size_t count = 2;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<DemoProtoNode, &DemoProtoNode::id>(
            make_field_desc("id", 1, wire_type::varint)),
        make_field<DemoProtoNode, &DemoProtoNode::children>(
            make_field_desc("children", 2, wire_type::length,
                            field_mode::repeated)),
    };
};

} // namespace proto
} // namespace rtti

// =============================================================================
// SECTION 3 -- small output helpers (teaching only)
// =============================================================================

namespace demo_proto_ns {

// Print `bytes` as a compact hex dump. The wire buffer is opaque binary data;
// a hex dump is the fastest way for a human to verify the layout the schema
// produces (tags, length prefixes, payload bytes).
static void print_hex(const char* tag, const std::string& bytes) {
    std::printf("%s [%zu bytes]  ", tag, bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::printf("%02x ", static_cast<unsigned>(
                                  static_cast<unsigned char>(bytes[i])));
    }
    std::printf("\n");
}

// Print every field of a telemetry object with an optional indentation label.
// We read the enum label through enum_to_string so output shows "fail" rather
// than the raw integer 2.
static void print_telemetry(const char* tag, const DemoProtoTelemetry& t) {
    std::printf("%s flag=%s i32=%d i64=%lld u64=%llu ratio=%.6g scale=%.6g "
                "label=\"%s\" kind=%s\n",
                tag, t.flag ? "true" : "false", t.i32,
                static_cast<long long>(t.i64),
                static_cast<unsigned long long>(t.u64), t.ratio, t.scale,
                t.label.c_str(),
                rtti::enum_to_string<DemoProtoKind>(t.kind));
}

// Recursively print any value_io tree. This is the generic way to inspect any
// adapter's document (JSON/XML/INI/proto) because every document implements
// the same rtti::value_io interface: type(), as_*(), size(), at(), get_key(),
// get_key_at(). It is used below to print the result of a schema-less parse
// and the schema-aware wire_to_tree document.
static void print_tree(const rtti::value_io* n, int depth) {
    for (int i = 0; i < depth; ++i) {
        std::printf("  ");
    }
    switch (n->type()) {
        case rtti::value_io::kind::null:
            std::printf("null\n");
            break;
        case rtti::value_io::kind::boolean: {
            bool b = false;
            (void)n->as_bool(b);
            std::printf("bool %s\n", b ? "true" : "false");
            break;
        }
        case rtti::value_io::kind::integer: {
            std::int64_t i = 0;
            (void)n->as_integer(i);
            std::printf("int %lld\n", static_cast<long long>(i));
            break;
        }
        case rtti::value_io::kind::real: {
            double d = 0.0;
            (void)n->as_real(d);
            std::printf("real %.6g\n", d);
            break;
        }
        case rtti::value_io::kind::string: {
            rtti::string_view s;
            (void)n->as_string(s);
            std::printf("string \"%.*s\"\n", static_cast<int>(s.size()),
                        s.data());
            break;
        }
        case rtti::value_io::kind::array:
            std::printf("array[%zu]\n", n->size());
            for (std::size_t i = 0; i < n->size(); ++i) {
                print_tree(n->at(i), depth + 1);
            }
            break;
        case rtti::value_io::kind::object:
            std::printf("object[%zu]\n", n->size());
            for (std::size_t i = 0; i < n->size(); ++i) {
                rtti::string_view key;
                rtti::value_io* child = nullptr;
                if (n->get_key_at(i, key, child) && child != nullptr) {
                    for (int k = 0; k <= depth; ++k) {
                        std::printf("  ");
                    }
                    std::printf("\"%.*s\" =>\n", static_cast<int>(key.size()),
                                key.data());
                    print_tree(child, depth + 1);
                }
            }
            break;
    }
}

// Deep-copy one value_io tree into another document. This is how two adapters
// exchange trees: any document is a value_io sink, so the copy below reads a
// proto::document and rebuilds it inside a json::document (or vice versa).
// The generic switch on kind()/as_*()/size()/at()/get_key_at() is exactly the
// abstract interface every adapter shares.
static void deep_copy_into(const rtti::value_io* src, rtti::value_io* dst) {
    switch (src->type()) {
        case rtti::value_io::kind::null:
            dst->make_scalar(rtti::field_value{});
            break;
        case rtti::value_io::kind::boolean: {
            bool b = false;
            (void)src->as_bool(b);
            dst->make_scalar(rtti::field_value::of(b));
            break;
        }
        case rtti::value_io::kind::integer: {
            std::int64_t i = 0;
            (void)src->as_integer(i);
            dst->make_scalar(rtti::field_value::of(i));
            break;
        }
        case rtti::value_io::kind::real: {
            double d = 0.0;
            (void)src->as_real(d);
            dst->make_scalar(rtti::field_value::of(d));
            break;
        }
        case rtti::value_io::kind::string: {
            rtti::string_view s;
            (void)src->as_string(s);
            dst->make_scalar(rtti::field_value::of(s));
            break;
        }
        case rtti::value_io::kind::array: {
            dst->make_array();
            for (std::size_t i = 0; i < src->size(); ++i) {
                rtti::value_io* child = dst->make_node();
                deep_copy_into(src->at(i), child);
                dst->push_back(child);
            }
            break;
        }
        case rtti::value_io::kind::object: {
            dst->make_object();
            for (std::size_t i = 0; i < src->size(); ++i) {
                rtti::string_view key;
                rtti::value_io* child = nullptr;
                if (src->get_key_at(i, key, child) && child != nullptr) {
                    rtti::value_io* out = dst->make_node();
                    deep_copy_into(child, out);
                    dst->set_key(key, out);
                }
            }
            break;
        }
    }
}

// Human-readable one-liner for an error_code. rtti::error_info::message() is
// a thin wrapper over the same table.
static const char* err_name(rtti::error_code e) {
    return rtti::to_string(e);
}

} // namespace demo_proto_ns

// =============================================================================
// SECTION 4 -- main: run every demonstration
// =============================================================================

int main() {
    using namespace demo_proto_ns;

    // =========================================================================
    // 4.1  serialize<T>/parse_as<T> wire round trip with a hex dump
    // =========================================================================

    // Build a telemetry object with NON-default values so every field hits the
    // wire. u64 is deliberately set above INT64_MAX to show the typed path
    // keeps unsigned 64-bit exactness.
    DemoProtoTelemetry t;
    t.flag = true;
    t.i32 = -7;
    t.i64 = (std::int64_t(1) << 40) - 3;
    t.u64 = std::uint64_t(1) << 63;
    t.ratio = 3.14;
    t.scale = -0.5f;
    t.label = "sensor-a";
    t.kind = DemoProtoKind::fail;

    std::printf("== [4.1] serialize / parse_as wire round trip ==\n");
    print_telemetry("source obj :", t);

    // --- serialize<T>(obj, out) ----------------------------------------------
    // Fills `out` with the wire bytes by walking the schema<T> field table.
    // Returns rtti::error_code (too_deep on runaway nesting, etc.); on error
    // `out` holds an unspecified partial buffer. We ignore the return here
    // only because a flat object like this cannot fail; 4.9 checks errors.
    std::string wire;
    const rtti::error_code se = rtti::proto::serialize(t, wire);
    std::printf("serialize<T>(obj,out) -> %s\n", err_name(se));
    print_hex("wire        :", wire);

    // --- serialize<T>(obj) convenience overload ------------------------------
    // Same as above but returns the buffer by value (empty on error). We use
    // it below for the second object so both API forms are shown.
    std::string wire2 = rtti::proto::serialize(t);
    print_hex("wire(conv)  :", wire2);
    std::printf("both overloads agree: %s\n",
                wire == wire2 ? "yes" : "NO");

    // --- parse_as<T>(string_view) --------------------------------------------
    // Decodes a wire message into a T{} (value-initialized) object, enforcing
    // required presence and applying defaults. Returns result<T> whose ok()
    // flag tells success; on failure err carries {code, offset}.
    const auto parsed = rtti::proto::parse_as<DemoProtoTelemetry>(
        rtti::string_view(wire.data(), wire.size()));
    if (!parsed.ok()) {
        std::printf("parse_as FAILED: %s @%zu\n", err_name(parsed.err.code),
                    parsed.err.offset);
        return 1;
    }
    const DemoProtoTelemetry& back = parsed.value;
    print_telemetry("parsed back :", back);

    // Verify every member survived the round trip exactly.
    const bool rt_ok = back.flag == t.flag && back.i32 == t.i32 &&
                       back.i64 == t.i64 && back.u64 == t.u64 &&
                       back.ratio == t.ratio && back.scale == t.scale &&
                       back.label == t.label && back.kind == t.kind;
    std::printf("round trip exact: %s\n\n", rt_ok ? "yes" : "NO");
    if (!rt_ok) {
        return 1;
    }

    // =========================================================================
    // 4.2  proto3 scalar default omission ("setdefault")
    // =========================================================================
    // proto3 defines the default for every scalar (0/false/""/label-of-zero).
    // The adapter OMITS any scalar equal to its default on the wire, and
    // absent scalars read as their default at decode -- so the all-default
    // object serializes to ZERO bytes and the empty buffer parses back to the
    // all-default object. This is what keeps proto3 payloads minimal.

    std::printf("== [4.2] proto3 scalar default omission ==\n");
    // Braced value-initialization: MSVC requires an explicit initializer for a
    // const object of this aggregate-with-defaults type (C2737 otherwise).
    const DemoProtoTelemetry all_defaults{};
    std::string empty_wire;
    const rtti::error_code de = rtti::proto::serialize(all_defaults, empty_wire);
    std::printf("serialize(all-default obj) -> %s, bytes=%zu\n",
                err_name(de), empty_wire.size());
    print_hex("empty wire  :", empty_wire);

    // Parse the empty buffer: every absent scalar falls back to its default.
    const auto dback = rtti::proto::parse_as<DemoProtoTelemetry>(
        rtti::string_view(empty_wire.data(), empty_wire.size()));
    if (!dback.ok() || dback.value.flag || dback.value.i32 != 0 ||
        dback.value.u64 != 0 || !dback.value.label.empty() ||
        dback.value.kind != DemoProtoKind::ok) {
        std::printf("proto3 default fill FAILED\n");
        return 1;
    }
    std::printf("empty wire parsed back to an all-default object (flag=%s "
                "label=\"%s\" kind=%s)\n\n",
                dback.value.flag ? "true" : "false",
                dback.value.label.c_str(),
                rtti::enum_to_string<DemoProtoKind>(dback.value.kind));

    // =========================================================================
    // 4.3  repeated numeric fields: packed vs unpacked
    // =========================================================================
    // A numeric repeated member registered with packed=true emits ONE
    // length-delimited payload; the same data with packed=false emits one
    // tag+value pair per element. The decoder accepts BOTH encodings for the
    // same field (protobuf-compatible), so wire from either form parses back.

    std::printf("== [4.3] repeated packed/unpacked ==\n");
    DemoProtoWave w;
    w.samples = {1, 150, 300, -1};
    w.stamps = {0, (std::int64_t(1) << 60)};

    // --- serialize<T>(obj, out) (packed schema) ------------------------------
    std::string pw;
    (void)rtti::proto::serialize(w, pw);
    print_hex("packed wire :", pw);

    // Parse it back and verify the element order/content is intact.
    const auto pr = rtti::proto::parse_as<DemoProtoWave>(
        rtti::string_view(pw.data(), pw.size()));
    if (!pr.ok() || pr.value.samples.size() != w.samples.size() ||
        pr.value.samples[0] != 1 || pr.value.samples[2] != 300 ||
        pr.value.stamps[1] != (std::int64_t(1) << 60)) {
        std::printf("packed round trip FAILED\n");
        return 1;
    }
    std::printf("packed round trip ok (samples=%zu, stamps=%zu)\n",
                pr.value.samples.size(), pr.value.stamps.size());

    // Now build the UNPACKED encoding by hand (each element is
    // tag(0x08/0x10) + varint) and feed it to the SAME parse_as<DemoProtoWave>.
    // The decoder must accept it because a packed field also admits the
    // unpacked form on the wire.
    std::string unpacked;
    rtti::proto::encode_varint(0x08u, unpacked);            // field 1 tag
    rtti::proto::encode_varint(7u, unpacked);               // element 7
    rtti::proto::encode_varint(0x08u, unpacked);            // field 1 tag
    rtti::proto::encode_varint(300u, unpacked);             // element 300
    const auto ur = rtti::proto::parse_as<DemoProtoWave>(
        rtti::string_view(unpacked.data(), unpacked.size()));
    if (!ur.ok() || ur.value.samples.size() != 2 ||
        ur.value.samples[0] != 7 || ur.value.samples[1] != 300) {
        std::printf("unpacked decode FAILED\n");
        return 1;
    }
    print_hex("unpacked wire:", unpacked);
    std::printf("unpacked bytes decode into the same repeated field ok "
                "(samples=%d,%d)\n\n",
                ur.value.samples[0], ur.value.samples[1]);

    // =========================================================================
    // 4.4  nested message + repeated message + protobuf map
    // =========================================================================

    std::printf("== [4.4] nested / repeated message / map ==\n");
    DemoProtoEnvelope env;
    env.tag = 9;
    env.inner.i32 = 7;
    env.inner.label = "inner-1";
    env.inner.kind = DemoProtoKind::warn;
    env.events.push_back(env.inner);
    DemoProtoTelemetry ev2;
    ev2.flag = true;
    ev2.label = "inner-2";
    env.events.push_back(ev2);
    env.counters["tx"] = 120;
    env.counters["rx"] = 34000;

    std::string env_wire;
    (void)rtti::proto::serialize(env, env_wire);
    print_hex("envelope wire:", env_wire);

    const auto er = rtti::proto::parse_as<DemoProtoEnvelope>(
        rtti::string_view(env_wire.data(), env_wire.size()));
    if (!er.ok() || er.value.tag != 9 || er.value.inner.i32 != 7 ||
        er.value.events.size() != 2 || er.value.events[1].label != "inner-2" ||
        er.value.counters.size() != 2 || er.value.counters.at("rx") != 34000) {
        std::printf("envelope round trip FAILED\n");
        return 1;
    }
    std::printf("envelope round trip ok: events=%zu map=%zu "
                "(rx=%d tx=%d)\n\n",
                er.value.events.size(), er.value.counters.size(),
                er.value.counters.at("rx"), er.value.counters.at("tx"));

    // =========================================================================
    // 4.5  proto2 required fields + explicit defaults
    // =========================================================================

    std::printf("== [4.5] proto2 required + explicit defaults ==\n");

    // --- wire with ONLY the required id field --------------------------------
    // Fields 2..5 are absent, so the decoder applies the explicit defaults
    // registered in schema<DemoProtoProfile> (name="anon", age=42, active,
    // score=1.5).
    static const unsigned char id_only[] = {0x08, 0x07};  // field 1 varint 7
    const auto p2 = rtti::proto::parse_as<DemoProtoProfile>(
        rtti::string_view(reinterpret_cast<const char*>(id_only),
                          sizeof(id_only)));
    if (!p2.ok() || p2.value.id != 7 || p2.value.name != "anon" ||
        p2.value.age != 42 || !p2.value.active || p2.value.score != 1.5) {
        std::printf("proto2 default fill FAILED\n");
        return 1;
    }
    std::printf("wire {id=7} parsed -> id=7 name=\"%s\" age=%d active=%s "
                "score=%.6g (defaults applied)\n",
                p2.value.name.c_str(), p2.value.age,
                p2.value.active ? "true" : "false", p2.value.score);

    // --- required field MISSING is an error ----------------------------------
    // An empty buffer has no field 1. decode_message reports
    // error_code::missing_key instead of fabricating a value.
    const auto p2err = rtti::proto::parse_as<DemoProtoProfile>(
        rtti::string_view("", 0));
    if (p2err.ok()) {
        std::printf("missing required id should NOT parse ok\n");
        return 1;
    }
    std::printf("empty wire -> error_code::%s (missing required field)\n",
                err_name(p2err.err.code));

    // --- proto2 encode-side default omission ---------------------------------
    // At the proto2 default, optional fields are omitted (like proto3), while
    // the required id is ALWAYS written. So serialize of the default-profile
    // object emits exactly the required field.
    DemoProtoProfile prof;
    prof.id = 1;
    prof.name = "anon";  // equals its explicit default -> omitted
    prof.age = 42;       // equals its explicit default -> omitted
    prof.active = true;  // equals its explicit default -> omitted
    prof.score = 1.5;    // equals its explicit default -> omitted
    std::string prof_wire;
    (void)rtti::proto::serialize(prof, prof_wire);
    print_hex("proto2 at-default wire:", prof_wire);
    const auto pr2 = rtti::proto::parse_as<DemoProtoProfile>(
        rtti::string_view(prof_wire.data(), prof_wire.size()));
    if (!pr2.ok() || pr2.value.id != 1 || pr2.value.name != "anon") {
        std::printf("proto2 round trip FAILED\n");
        return 1;
    }
    std::printf("proto2 default omission round trip ok\n\n");

    // =========================================================================
    // 4.6  value_io tree bridge (wire <-> tree <-> JSON)
    // =========================================================================
    // Every format adapter exposes a document that implements rtti::value_io.
    // The proto adapter bridges C++ <-> wire <-> tree through that interface:
    //   * rtti::to_value(&protoDoc, obj)     C++ object -> value_io tree
    //   * proto::tree_to_wire(&tree, wire)   ANY value_io tree -> wire
    //   * proto::wire_to_tree(wire, doc)     wire -> value_io tree
    //   * rtti::from_value(&tree, obj)       value_io tree -> C++ object
    // A JSON document is just another value_io tree, so JSON text can drive
    // wire encoding directly and a decoded wire tree can be re-rendered as
    // JSON -- no copying of schema knowledge between the two formats.

    std::printf("== [4.6] value_io tree bridge + JSON interop ==\n");

    // --- C++ -> proto::document tree -> wire ---------------------------------
    DemoProtoTelemetry tt;
    tt.i32 = -9;
    tt.label = "tree-bridge";
    tt.kind = DemoProtoKind::fail;
    rtti::proto::document doc;                 // an owned, empty object tree
    const rtti::error_code tv =
        rtti::to_value(&doc, tt);              // reflect walk into value_io
    std::string tw;
    const rtti::error_code tw_e =
        rtti::proto::tree_to_wire<DemoProtoTelemetry>(&doc, tw);
    std::printf("to_value+tree_to_wire -> %s/%s\n", err_name(tv),
                err_name(tw_e));
    print_hex("tree->wire   :", tw);

    // The tree-bridge bytes must equal the direct typed serializer output.
    std::string td = rtti::proto::serialize(tt);
    std::printf("matches typed serialize: %s\n",
                tw == td ? "yes" : "NO");

    // --- wire -> tree -> from_value (schema-aware) ----------------------------
    // wire_to_tree fills the tree with field NAMES and applies scalar defaults
    // for absent fields; from_value then reads the tree back into a C++ object.
    rtti::proto::document doc2;
    const rtti::error_code wt =
        rtti::proto::wire_to_tree<DemoProtoTelemetry>(
            rtti::string_view(td.data(), td.size()), doc2);
    DemoProtoTelemetry tt2;
    const rtti::error_code fv = rtti::from_value(&doc2, tt2);
    std::printf("wire_to_tree+from_value -> %s/%s\n", err_name(wt),
                err_name(fv));
    print_telemetry("tree->object :", tt2);
    std::printf("round trip equals source: %s\n",
                (tt2.label == tt.label && tt2.i32 == tt.i32 &&
                 tt2.kind == tt.kind)
                    ? "yes"
                    : "NO");

    // --- JSON TEXT -> JSON document tree -> wire ------------------------------
    // A JSON document produced by rtti::json::parse is a value_io tree whose
    // object keys are field NAMES. Feed it straight into tree_to_wire: the
    // proto adapter looks fields up by name, so no schema conversion is
    // needed. Note the enum arrives as its LABEL string "fail" and the adapter
    // maps it to the wire varint via enum_from_string.
    const auto jdoc = rtti::json::parse(
        R"({"flag":true,"i32":7,"i64":0,"u64":0,"ratio":0.0,"scale":0.0,"label":"json-fed","kind":"fail"})");
    if (!jdoc.ok()) {
        std::printf("json parse FAILED: %s\n", err_name(jdoc.err.code));
        return 1;
    }
    std::string jw;
    const rtti::error_code jw_e =
        rtti::proto::tree_to_wire<DemoProtoTelemetry>(&jdoc.value, jw);
    print_hex("json->wire   :", jw);
    const auto jb = rtti::proto::parse_as<DemoProtoTelemetry>(
        rtti::string_view(jw.data(), jw.size()));
    if (jw_e != rtti::error_code::ok || !jb.ok() || jb.value.flag != true ||
        jb.value.i32 != 7 || jb.value.label != "json-fed" ||
        jb.value.kind != DemoProtoKind::fail) {
        std::printf("json->wire->object FAILED\n");
        return 1;
    }
    std::printf("json-fed object -> flag=true i32=7 label=\"%s\" kind=%s\n",
                jb.value.label.c_str(),
                rtti::enum_to_string<DemoProtoKind>(jb.value.kind));

    // --- wire -> tree -> deep copy -> JSON text -------------------------------
    // Decode the ORIGINAL wire into a proto tree, deep-copy the tree into a
    // JSON document through the generic value_io interface, then re-render it
    // as JSON text with json::serialize. This completes the bidirectional
    // interop story (JSON <-> wire) without either adapter knowing about the
    // other's schema representation.
    rtti::proto::document pdoc;
    (void)rtti::proto::wire_to_tree<DemoProtoTelemetry>(
        rtti::string_view(td.data(), td.size()), pdoc);
    rtti::json::document jdoc2;
    deep_copy_into(&pdoc, &jdoc2);
    rtti::json::write_config jcfg;
    jcfg.indent = 0;  // compact single-line output
    const std::string json_text = rtti::json::serialize(jdoc2, jcfg);
    std::printf("wire->tree->json: %s\n", json_text.c_str());

    // --- schema-less parse (generic wire inspection) --------------------------
    // proto::parse(sv) decodes wire WITHOUT a schema: object keys are the
    // decimal FIELD NUMBERS and length fields become string nodes. Useful for
    // debugging unknown payloads before a schema exists.
    const auto g = rtti::proto::parse(rtti::string_view(td.data(), td.size()));
    if (!g.ok()) {
        std::printf("schema-less parse FAILED\n");
        return 1;
    }
    std::printf("schema-less parse tree:\n");
    print_tree(&g.value, 1);
    std::printf("\n");

    // =========================================================================
    // 4.7  SAX parse_stream wire events
    // =========================================================================
    // parse_stream walks the wire WITHOUT building a tree. Each hook fires in
    // document order: message_start, field(number,wire_type), one of the
    // typed value hooks, message_end. A callback returning false cancels the
    // walk; parse_stream then returns ok (nothing was built, so there is no
    // half-parsed state). Malformed wire still surfaces error_code.

    std::printf("== [4.7] SAX parse_stream ==\n");
    // A small hand-built message: field1 varint 42, field2 length "abc".
    static const unsigned char sax_wire[] = {0x08, 0x2A, 0x12, 0x03,
                                             'a', 'b', 'c'};
    std::string event_log;
    rtti::proto::parse_events ev;
    ev.message_start = [&] {
        event_log += "[start]";
        return true;
    };
    ev.message_end = [&] {
        event_log += "[end]";
        return true;
    };
    ev.field = [&](std::uint32_t number, rtti::proto::wire_type wt) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "(field %u wt %u)",
                      static_cast<unsigned>(number),
                      static_cast<unsigned>(wt));
        event_log += buf;
        return true;
    };
    ev.varint_value = [&](std::uint64_t v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "=%llu", static_cast<unsigned long long>(v));
        event_log += buf;
        return true;
    };
    ev.bytes_value = [&](rtti::string_view b) {
        event_log += "='";
        event_log.append(b.data(), b.size());
        event_log += "'";
        return true;
    };
    const auto sax = rtti::proto::parse_stream(
        rtti::string_view(reinterpret_cast<const char*>(sax_wire),
                          sizeof(sax_wire)),
        ev);
    if (!sax.ok()) {
        std::printf("parse_stream FAILED: %s\n", err_name(sax.err.code));
        return 1;
    }
    std::printf("events: %s\n", event_log.c_str());

    // --- early cancellation ---------------------------------------------------
    // Returning false from the `field` hook stops the walk cleanly: ok() and
    // only the events before the cancellation were delivered.
    std::string cancel_log;
    rtti::proto::parse_events cancel_ev;
    cancel_ev.message_start = [&] {
        cancel_log += "S";
        return true;
    };
    cancel_ev.field = [&](std::uint32_t, rtti::proto::wire_type) {
        cancel_log += "F";
        return false;  // cancel after the first field tag
    };
    const auto cancelled = rtti::proto::parse_stream(
        rtti::string_view(reinterpret_cast<const char*>(sax_wire),
                          sizeof(sax_wire)),
        cancel_ev);
    if (!cancelled.ok()) {
        std::printf("cancelled parse should be ok\n");
        return 1;
    }
    std::printf("cancel at first field -> %s (ok, early stop)\n\n",
                cancel_log.c_str());

    // =========================================================================
    // 4.8  schema-less SAX: same parse_stream over a packed payload
    // =========================================================================
    // parse_stream is generic wire walking, so a packed repeated payload shows
    // up as one length-delimited bytes_value event. We just log it here to
    // contrast with the typed decode in 4.3.
    {
        std::printf("== [4.8] schema-less SAX over a packed payload ==\n");
        std::string pw2;
        (void)rtti::proto::serialize(w, pw2);
        std::string blob;
        rtti::proto::parse_events ev3;
        ev3.field = [&](std::uint32_t n, rtti::proto::wire_type wt) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "field %u(wt%u) ",
                          static_cast<unsigned>(n),
                          static_cast<unsigned>(wt));
            blob += buf;
            return true;
        };
        ev3.bytes_value = [&](rtti::string_view b) {
            blob += "[packed ";
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%zu bytes] ",
                          b.size());
            blob += buf;
            return true;
        };
        (void)rtti::proto::parse_stream(
            rtti::string_view(pw2.data(), pw2.size()), ev3);
        std::printf("packed stream: %s\n", blob.c_str());
    }
    std::printf("\n");

    // =========================================================================
    // 4.9  malformed wire error handling
    // =========================================================================
    // The wire reader is bounded: every read is checked against the buffer end
    // BEFORE touching memory, so malformed input can never read out of range
    // or crash. parse_as surfaces one of four codes:
    //   proto_truncated      varint continues past EOF / length prefix bigger
    //                        than the remaining buffer
    //   proto_bad_field      field number 0, or an unsupported group wire type
    //   proto_type_mismatch  wire type on the stream differs from the schema
    //   missing_key          (proto2) required field absent

    std::printf("== [4.9] malformed wire error handling ==\n");

    auto probe = [](const unsigned char* bytes, std::size_t n,
                    const char* what) {
        // Parse an arbitrary buffer against the DemoProtoTelemetry schema.
        const auto r = rtti::proto::parse_as<DemoProtoTelemetry>(
            rtti::string_view(reinterpret_cast<const char*>(bytes), n));
        std::printf("  %-34s -> %s (offset %zu)\n", what,
                    r.ok() ? "ok" : err_name(r.err.code), r.err.offset);
        return r.ok();
    };

    // (a) Truncated varint: tag 0x08 (field 1) followed by 0x80 which still
    //     has its continuation bit set but no more bytes follow.
    static const unsigned char trunc_varint[] = {0x08, 0x80};
    if (probe(trunc_varint, sizeof(trunc_varint),
              "truncated varint (0x08 0x80)")) {
        std::printf("truncated varint should have failed\n");
        return 1;
    }

    // (b) Over-long length prefix: field 7 (string, tag 0x3A) claims 0x7F
    //     bytes but only one byte follows.
    static const unsigned char overlong[] = {0x3A, 0x7F, 'a'};
    if (probe(overlong, sizeof(overlong),
              "length prefix overruns buffer")) {
        std::printf("overlong length should have failed\n");
        return 1;
    }

    // (c) Wire type mismatch: field 2 (i32, varint in the schema) is sent with
    //     wire type fixed64 (tag 0x11 = field 2 | wire 1).
    static const unsigned char wrong_wt[] = {0x11, 0, 0, 0, 0, 0, 0, 0, 0};
    if (probe(wrong_wt, sizeof(wrong_wt),
              "wire type mismatch (field2 fixed64)")) {
        std::printf("wire type mismatch should have failed\n");
        return 1;
    }

    // (d) Field number 0 is illegal on the wire (tag 0x00).
    static const unsigned char field_zero[] = {0x00};
    if (probe(field_zero, sizeof(field_zero), "field number 0")) {
        std::printf("field number 0 should have failed\n");
        return 1;
    }

    // (e) Group wire types 3 and 4 are not supported -> proto_bad_field.
    static const unsigned char group_wt[] = {0x0B};  // field 1, wire type 3
    if (probe(group_wt, sizeof(group_wt), "group wire type 3")) {
        std::printf("group wire type should have failed\n");
        return 1;
    }

    // (f) A truncated STREAM (parse_stream) reports the same code.
    const auto sbad = rtti::proto::parse_stream(
        rtti::string_view(reinterpret_cast<const char*>(trunc_varint),
                          sizeof(trunc_varint)),
        rtti::proto::parse_events{});
    if (sbad.ok()) {
        std::printf("stream truncated varint should have failed\n");
        return 1;
    }
    std::printf("  parse_stream on truncated varint      -> %s\n",
                err_name(sbad.err.code));
    std::printf("\n");

    // =========================================================================
    // 4.10 deep-nesting DoS guard
    // =========================================================================
    // Encode/Decode are depth-limited (kDefaultMaxDepth = 512). A chain 600
    // levels deep fails with error_code::too_deep instead of exhausting the
    // stack -- the same guard as the JSON/XML/INI adapters.
    std::printf("== [4.10] deep-nesting DoS guard ==\n");
    DemoProtoNode root;
    DemoProtoNode* cur = &root;
    for (int i = 0; i < 600; ++i) {
        cur->children.emplace_back();
        cur = &cur->children.back();
    }
    std::string deep_wire;
    const rtti::error_code deep_e = rtti::proto::serialize(root, deep_wire);
    std::printf("serialize 600-deep chain -> %s\n", err_name(deep_e));
    if (deep_e != rtti::error_code::too_deep) {
        std::printf("depth guard should have reported too_deep\n");
        return 1;
    }

    // Manually build the same chain as wire (wrap a 1-field message in 600
    // nested length-delimited children) and confirm parse_as refuses it too.
    std::string deep = "\x08\x01";
    for (int i = 0; i < 600; ++i) {
        std::string wrap;
        wrap.push_back(static_cast<char>(0x12));  // field 2, length
        (void)rtti::proto::encode_varint(deep.size(), wrap);
        wrap += deep;
        deep = std::move(wrap);
    }
    const auto deep_parse = rtti::proto::parse_as<DemoProtoNode>(
        rtti::string_view(deep.data(), deep.size()));
    std::printf("parse 600-deep wire   -> %s\n",
                deep_parse.ok() ? "ok" : err_name(deep_parse.err.code));
    if (deep_parse.ok() || deep_parse.err.code != rtti::error_code::too_deep) {
        std::printf("depth guard decode should have reported too_deep\n");
        return 1;
    }

    std::printf("\nproto_demo: ALL SECTIONS PASSED\n");
    return 0;
}
