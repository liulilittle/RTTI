#pragma once
// .proto text-descriptor parser + runtime schema bridge for the protobuf
// wire-format adapter (spec section 13.2).
//
// proto.hpp builds a proto schema from explicit C++ schema<T> registrations.
// This header adds the missing text side: parsing a .proto file
// (messages/nested messages/fields/labels/scalars/comments) into a plain
// schema_desc, then compiling it into a runtime schema_info that drives the
// SAME wire_to_tree / tree_to_wire / parse codec chain as a schema<T>. So a
// .proto descriptor and a C++ registration both produce equivalent wire
// behaviour.
//
// Supported syntax subset (proto2 and proto3):
//   * `syntax = "proto2" | "proto3";` (absent -> proto2, like protoc).
//   * `message Name { ... }` and nested messages (dotted fully-qualified
//     names, e.g. Outer.Inner). Forward references are allowed.
//   * Fields: `[label] type name = number [options] ;` where label is
//     optional/required/repeated; `required` is rejected inside proto3.
//   * Scalar types int32/int64/uint32/uint64/sint32/sint64/bool/float/double/
//     string/bytes/fixed32/fixed64/sfixed32/sfixed64.
//   * Message and enum type references (dotted names allowed; a leading '.'
//     is absolute). Enum-typed fields take int32/varint wire semantics.
//   * Field options `[packed = true|false]` and `[default = <value>]`
//     (integer, real, string, true/false, inf/nan). Unknown options are
//     skipped. Other option forms ([(custom.option)], etc.) are skipped as
//     balanced groups.
//   * `//` line and `/* */` block comments.
//   * Top-level `import` / `package` / `option` statements are consumed and
//     ignored.
//
// Wire derivation follows the protoc rules: repeated numeric fields are
// packed by default under proto3 and unpacked under proto2 unless
// `[packed = ...]` says otherwise; string/bytes/message are never packed.
//
// Safety (spec section 0): malformed, truncated, absurdly deep or absurdly
// long input returns an error_code (offset + line/column via error_info) and
// never crashes. Message nesting in the descriptor is capped, field numbers
// must be 1..(2^29-1), and integer literals overflow-check.
//
// English comments only; C++17; no third-party dependencies. The non-template
// parser/compiler lives in src/proto_schema.cpp (built into the rtti static
// library); this header keeps the declarations, the plain schema_desc data
// structures and the compiled_schema class shell. Gated by RTTI_ENABLE_PROTO
// exactly like <rtti/proto.hpp>.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rtti/proto.hpp>

namespace rtti {
namespace proto {

// ---------------------------------------------------------------------------
// Parsed text schema (plain data, no codec binding)
// ---------------------------------------------------------------------------

// The 15 protobuf scalar kinds plus the reference kinds. message_val/enum_val
// are produced by the type-resolution pass for references.
enum class scalar_type : std::uint8_t {
    int32,
    int64,
    uint32,
    uint64,
    sint32,
    sint64,
    bool_val,
    float_val,
    double_val,
    string_val,
    bytes_val,
    fixed32,
    fixed64,
    sfixed32,
    sfixed64,
    message_val,
    enum_val
};

// Natural wire type for a scalar kind per the protobuf spec. message refs use
// length (encoded as a nested message); enum refs use varint.
wire_type wire_of_scalar(scalar_type t) noexcept;

// One parsed field. `type` holds the resolved scalar kind; `type_ref` +
// `type_index` describe a message/enum reference (message_val/enum_val).
struct schema_field {
    std::string name;
    std::uint32_t number = 0;
    field_mode mode = field_mode::optional;
    bool packed = false;
    bool packed_explicit = false;

    scalar_type type = scalar_type::int32;
    std::string type_ref;
    bool is_message = false;
    bool is_enum = false;
    std::size_t type_index = static_cast<std::size_t>(-1); // message index
    std::size_t type_offset = 0; // byte offset of the type token

    bool has_default = false;
    std::int64_t dflt_int = 0;
    double dflt_real = 0.0;
    std::string dflt_str;
    // Unresolved enum default name captured from `[default = NAME]`; resolved
    // to dflt_int by the type-resolution pass (see resolve_types).
    std::string dflt_enum_name;
    // A numeric [default = N] on a type-reference field (a message/enum
    // candidate) is captured here until the pass knows it is an enum; symbol
    // defaults ride in dflt_enum_name.
    bool dflt_pending_set = false;
    std::int64_t dflt_pending_int = 0;

    // `map<K,V>` field: expanded to a synthetic entry message
    // (key = 1 / value = 2) referenced as a repeated message field.
    bool is_map = false;
    // Explicit-presence scalar (map-entry key/value): encode even when the
    // value equals the default, matching protoc's map-entry presence tracking.
    bool emit_present = false;
    // Non-zero when the field is a member of a `oneof` group; every member of
    // the same group shares the id (single active field on the wire).
    std::uint32_t oneof_group = 0;
};

struct schema_message {
    std::string name;      // last path component
    std::string full_name; // dotted, e.g. "Outer.Inner"
    std::vector<schema_field> fields;
    // `reserved 1, 5 to 8;` numbers (inclusive ranges) and `reserved "x";`
    // names. A field reusing a reserved number or name is rejected by parse.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> reserved_numbers;
    std::vector<std::string> reserved_names;
    // `extensions 100 to 199;` (proto2): the field-number ranges the message
    // reserves for extensions. A regular field whose number falls inside one of
    // them is rejected by parse (proto2/proto3 protoc semantics); `extend`
    // blocks must use numbers from these ranges, which is validated when the
    // descriptor is compiled.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> extension_ranges;
    // Next oneof group id handed out by parse_oneof (starts at 1).
    std::uint32_t oneof_counter = 0;
};

// One `enum` definition: ordered value entries (name -> number).
struct schema_enum_value {
    std::string name;
    std::int32_t value = 0;
};

struct schema_enum {
    std::string full_name; // dotted, e.g. "Outer.Color"
    std::vector<schema_enum_value> values;
};

// One `extend <target> { ... }` field: the extended message plus the field
// descriptor. The field carries the same name/number/type/options data as a
// regular schema_field; wire semantics derive from field.type in the same way.
struct schema_extension {
    std::string extendee; // target message (dotted; resolved by resolve_types)
    schema_field field;
};

struct schema_desc {
    syntax syn = syntax::proto2;
    std::vector<schema_message> messages; // top-level first, then nested
    std::vector<std::string> enums;       // dotted enum full names
    std::vector<schema_enum> enum_defs;   // full enum tables (values)
    // import "file.proto"; declarations (dependency table; files not loaded).
    std::vector<std::string> imports;
    // `extend Target { ... }` declarations (proto2 extensions). compile-time
    // registration turns them into extension_desc into the ExtensionRegistry.
    std::vector<schema_extension> extensions;
};

namespace detail {

// One compiled message. The full definition (dynamic_field and the codec
// callbacks, plus the whole .proto scanner/parser and type-resolution pass)
// lives in src/proto_schema.cpp; the header only needs the name to declare
// compiled_schema::messages_.
struct compiled_message;

} // namespace detail

// ---------------------------------------------------------------------------
// Public: parse .proto text into a schema_desc
// ---------------------------------------------------------------------------

// Parse a .proto descriptor. Returns error_code (with err.offset / line() /
// column()) instead of throwing ever. Empty input parses to an empty schema.
result<schema_desc> parse_schema(rtti::string_view src);

// ---------------------------------------------------------------------------
// Public: multi-file dependency merging
// ---------------------------------------------------------------------------

// Merge `src` into `dst` for a multi-file build: messages are appended when
// their full name is not already present, enum tables deduplicated the same
// way, and import paths unioned. After merging, compile_schema sees types from
// every file. Nothing is loaded or resolved here -- merging is a pure data
// operation (import file paths are the caller's dependency table).
void merge_schemas(schema_desc& dst, const schema_desc& src);

// ---------------------------------------------------------------------------
// Public: compile a schema_desc into a codec-ready schema_info
// ---------------------------------------------------------------------------

// Register the `extend` blocks recorded in a resolved descriptor into the
// process-wide ExtensionRegistry (proto.hpp) under the global semantics: the
// lookup keys are the field number (decode) and the field name (encode). A
// conflict (same number or same name with a different layout) yields
// error_code::proto_bad_field; re-registering identical descriptors is a
// no-op. Every extension is validated against its extendee first (protoc
// semantics): the extendee must resolve to a message in the descriptor and the
// extension field number must fall inside one of that message's declared
// `extensions` ranges, otherwise error_code::proto_bad_field and nothing is
// registered. Returns error_code::ok on success.
error_code register_schema_extensions(const schema_desc& d);

struct compile_config {
    std::size_t root = 0; // index into schema_desc::messages
    std::string root_name; // alternative: full name or last component
};

// Owned runtime schema: mutable-only, outlives every use. `info()` yields the
// root message's schema_info for wire_to_tree / tree_to_wire / parse.
class compiled_schema {
public:
    compiled_schema();
    compiled_schema(compiled_schema&&) noexcept;
    compiled_schema& operator=(compiled_schema&&) noexcept;
    compiled_schema(const compiled_schema&) = delete;
    compiled_schema& operator=(const compiled_schema&) = delete;
    ~compiled_schema();

    const schema_info* info() const noexcept;
    const schema_info* find_message(rtti::string_view name) const noexcept;
    syntax syn() const noexcept;
    std::size_t size() const noexcept;

    // Build from a parsed (or hand-made) schema_desc. root_name wins over root.
    static bool build(const schema_desc& desc, const compile_config& cfg,
                      compiled_schema& out, error_info& err);

private:
    syntax syn_ = syntax::proto3;
    std::size_t root_ = 0;
    std::vector<std::unique_ptr<detail::compiled_message>> messages_;
};

// Compile a parsed descriptor into a codec-ready schema. Errors surface as an
// invalid_root / no_such_field result, never a crash.
result<compiled_schema> compile_schema(
    const schema_desc& desc, const compile_config& cfg = compile_config{});

// Parse-then-compile convenience overload.
result<compiled_schema> compile_schema(
    rtti::string_view src, const compile_config& cfg = compile_config{});

} // namespace proto
} // namespace rtti
