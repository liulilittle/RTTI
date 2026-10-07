#pragma once
// FlatBuffers .fbs text-descriptor parser + runtime schema bridge for the
// flatbuffers wire-format adapter (spec section 4.1).
//
// flat.hpp builds a flat schema from explicit C++ schema<T> registrations.
// This header adds the missing text side: parsing a .fbs file
// (namespace/table/struct/enum/union/fields/defaults/attributes/comments) into
// a queryable fbs_schema_desc, then compiling it into a runtime schema_info
// that drives the SAME wire_to_tree / tree_to_wire / parse codec chain as a
// schema<T>. So a .fbs descriptor and a C++ registration both produce
// equivalent wire behaviour.
//
// Supported syntax (mirrors flatc 25.12.19 semantics):
//   * `namespace a.b.c;`          -- dotted namespace for the following defs.
//   * `include "file.fbs";`       -- recorded dependency; not loaded.
//   * `table Name { ... }` / `struct Name { ... }` with fields
//       `name:type [= default] [(attrs)];`
//       type := scalar | "string" | "[" (scalar|string|enum|ref) "]" |
//               table/struct/enum/union reference. References may be bare or
//               dot-qualified (`foo.Inner`) and resolve relative to the current
//               namespace chain, then absolutely; forward references are
//               allowed. A vector of an enum type reads as a vector of the
//               enum's underlying scalar.
//   * scalar types bool/byte/ubyte/short/ushort/int/uint/long/ulong/float/
//       double; `[T]` vectors; `string`.
//   * defaults: integral (decimal/hex/negative), real (e-notation), inf/-inf,
//       nan, bool true/false (and integer 0/1 like flatc), enum value names,
//       resolved by the enum table (flatc: enum fields default to 0 unless an
//       explicit `= NAME` / `= N` is given).
//   * attributes `(deprecated, required, id: N, key, ...)`; unknown attributes
//       are skipped as balanced groups. flatc rules enforced: ids appear on all
//       or no fields and then must be consecutive from 0; a union id N covers
//       type slot N-1 and value slot N; `required` only on non-scalar table
//       fields; struct fields reject defaults/vectors/strings/unions/attrs.
//   * `enum E[:underlying] { A[=N], B, ... }`; underlying defaults to int; the
//       first value defaults to 0 and each later one to prev+1; hex literals
//       allowed.
//   * `union U { A[=N], B, ... }`; NONE=0 implicit; members must be tables.
//       A table field `u:U` occupies two vtable slots: `u_type` (u8 wire enum)
//       and `u` (variant table offset).
//   * `root_type Name;` and `file_identifier "XXXX";` (exactly 4 bytes).
//
// Safety (spec section 0): malformed, truncated, unclosed or absurdly long
// input returns an error_code with offset/line/column (via error_info) and
// never crashes. Token/nesting depth is capped, integer and hex literals
// overflow-check, defaults are range-checked against their scalar kind.
//
// Error-code mapping (English messages via rtti::to_string):
//   unterminated_string / unterminated_comment / invalid_number / unexpected_token
//       lexical errors and structural mismatches (missing ';', unclosed '{',
//       unknown top-level word, duplicate field/type, unknown type reference,
//       unknown enum default name, bad enum underlying); no_such_field -- a
//       root_type that resolves to no table; proto_type_mismatch -- a default
//       literal of the wrong kind (real for an integer field, etc.);
//       proto_bad_field -- a default on a non-scalar field, an attribute on a
//       struct field, `required` on a scalar, or an invalid id set; overflow --
//       a default outside its scalar range; invalid_root -- compile with no
//       selectable root table.
//
// English comments only; C++17; zero third-party dependencies; the non-template
// parser/compiler lives in src/flat_schema.cpp (absorbed by the rtti static
// library); this header keeps only declarations and the thin template bridge.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <rtti/flat.hpp>

#if !defined(RTTI_ENABLE_FLATBUFFERS)
#define RTTI_ENABLE_FLATBUFFERS 1
#endif
#if !RTTI_ENABLE_FLATBUFFERS
#  error "RTTI: flatbuffers format adapter not enabled (compile with -DRTTI_ENABLE_FLATBUFFERS=1)"
#endif

namespace rtti {
namespace flat {

// ---------------------------------------------------------------------------
// Parsed text schema (plain data, no codec binding)
// ---------------------------------------------------------------------------

// Semantic field type in the parsed IR. `scalar`/`string` are built-ins;
// `table`/`stct`/`enumref`/`unionref` reference named definitions resolved by
// the type-resolution pass; `vector` wraps an element type.
enum class fbs_type : std::uint8_t {
    none = 0,
    scalar,
    string,
    vector,
    table,
    stct,
    enumref,
    unionref
};

// One parsed field (table or struct). `kind` holds the scalar kind for
// scalar/enum fields and the vector element kind for scalar/string vectors;
// `elem` holds the vector element kind (table/stct when the element is a
// reference, in which case `ref`/`type_index` name and locate it). `slot` is
// NOT stored here; it is derived by the compile pass from `id` (or declaration
// order), so the text descriptor stays flatc-neutral.
struct fbs_field_desc {
    std::string name;
    fbs_type type = fbs_type::none;
    fkind kind = fkind::none;          // scalar kind / enum underlying / vector elem scalar
    fkind elem = fkind::none;          // vector element kind (table/stct/string/scalar)
    std::string ref;                   // table/struct/union/enum name reference
    std::size_t type_index = static_cast<std::size_t>(-1); // table/struct/union / vector-elem index
    std::size_t enum_index = static_cast<std::size_t>(-1); // enum index for enumref
    std::size_t type_offset = 0;   // byte offset of the type token (diagnostics)
    bool has_default = false;
    std::int64_t dflt_int = 0;
    double dflt_real = 0.0;
    std::string dflt_enum_name;        // enumref default symbol, resolved later
    bool dflt_real_lit = false;        // default literal was a real number
    bool deprecated = false;
    bool required = false;
    bool has_id = false;
    std::uint16_t id = 0;              // declared vtable id (union: value part)
};

struct fbs_enum_value_desc {
    std::string name;
    std::int64_t value = 0;
};

struct fbs_enum_desc {
    std::string name;
    std::string full_name;
    fkind underlying = fkind::none;
    std::vector<fbs_enum_value_desc> values;
};

struct fbs_union_variant_desc {
    std::string name;
    std::int64_t value = 0;            // wire enum value (NONE=0 implicit)
    std::string type_ref;              // member table reference
    std::size_t type_index = static_cast<std::size_t>(-1);
};

struct fbs_union_desc {
    std::string name;
    std::string full_name;
    std::vector<fbs_union_variant_desc> variants;
};

// One table or struct definition (stct distinguishes the two).
struct fbs_agg_desc {
    std::string name;
    std::string full_name;
    bool stct = false;                 // true: struct (fixed inline layout)
    std::vector<fbs_field_desc> fields;
};

// The full parsed schema. `types` holds tables then structs in declaration
// order; `root_type` is the resolved fully-qualified root table ("" when none
// was declared); `file_identifier` is "" when no identifier was declared.
struct fbs_schema_desc {
    std::string ns;                    // last declared namespace
    std::string root_type;
    std::string file_identifier;
    std::vector<std::string> includes;
    std::vector<fbs_enum_desc> enums;
    std::vector<fbs_union_desc> unions;
    std::vector<fbs_agg_desc> types;
};

// Query helpers: match a fully-qualified name or the last path component.
const fbs_agg_desc* find_type(const fbs_schema_desc& desc,
                              rtti::string_view name) noexcept;
const fbs_enum_desc* find_enum(const fbs_schema_desc& desc,
                               rtti::string_view name) noexcept;
const fbs_union_desc* find_union(const fbs_schema_desc& desc,
                                 rtti::string_view name) noexcept;

// Parse a .fbs descriptor text into a fbs_schema_desc. Implemented in
// src/flat_schema.cpp. Empty input parses to an empty (compile-invalid) schema.
result<fbs_schema_desc> parse_schema(rtti::string_view src);

// ---------------------------------------------------------------------------
// Compile: schema_desc -> codec-ready schema_info
// ---------------------------------------------------------------------------

struct compile_config {
    std::string root_name; // overrides root_type; full name or last component
};

// Owned runtime schema: mutable-only, outlives every use. `info()` yields the
// root table's schema_info for wire_to_tree / tree_to_wire / parse.
class compiled_schema {
public:
    compiled_schema();
    compiled_schema(compiled_schema&&) noexcept;
    compiled_schema& operator=(compiled_schema&&) noexcept;
    compiled_schema(const compiled_schema&) = delete;
    compiled_schema& operator=(const compiled_schema&) = delete;
    ~compiled_schema();

    const schema_info* info() const noexcept;      // root table
    const schema_info* find_type(rtti::string_view name) const noexcept;
    rtti::string_view root_name() const noexcept;
    std::size_t size() const noexcept;

private:
    friend result<compiled_schema> compile_schema(const fbs_schema_desc&,
                                                  const compile_config&);
    struct compiled_type;
    std::vector<std::unique_ptr<compiled_type>> types_;
    std::string file_identifier_;      // stable storage for si->identifier
    std::string root_name_;
    std::size_t root_index_ = 0;
};

// Compile a parsed descriptor (or text) into a codec schema. Errors surface as
// invalid_root (no root selectable) / no_such_field (unknown root_name), never
// a crash.
result<compiled_schema> compile_schema(const fbs_schema_desc& desc,
                                       const compile_config& cfg = {});
result<compiled_schema> compile_schema(rtti::string_view src,
                                       const compile_config& cfg = {});

} // namespace flat
} // namespace rtti