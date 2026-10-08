#pragma once
// FlatBuffers wire-format adapter for RTTI.
//
// Mirrors the proto/JSON/XML/INI/TOML adapter architecture: a value_io tree
// bridge, parse/serialize, typed parse_as<T>/serialize<T> reflect bridging and
// an RTTI_ENABLE_FLATBUFFERS strip gate (default 1). The template shell lives
// in this header; every non-template wire op (root uoffset, vtable walk,
// vectors/strings/structs, unions, the owned tree document and the buffer
// builder) is implemented in <src/flat.cpp>. Zero third-party dependencies;
// C++17.
//
// Design (spec section 14):
//   * schema<T>       : explicit flat::schema<T> specialization maps each
//                         reflected C++ field NAME to its FlatBuffers vtable
//                         SLOT and wire KIND. The primary (unregistered)
//                         template is an empty table, so unregistered types
//                         parse as an empty object / serialize to header bytes.
//   * wire reading    : root uoffset (4 bytes, buffer-relative), table soffset
//                         walk to the vtable (vtable_size/table_size u16s then
//                         one u16 field offset per slot, 0 = absent), scalars
//                         aligned 1/2/4/8, strings (uoffset -> u32 length +
//                         UTF-8 bytes + trailing NUL), vectors (uoffset -> u32
//                         count + elements; scalars/structs inline, strings and
//                         tables as per-element uoffsets), inline structs,
//                         unions (a u8 type slot + a paired uoffset value slot)
//                         and an optional file_identifier at buffer[4,8).
//                         Malformed input (truncation, out-of-range offsets,
//                         over-long lengths, misalignment) returns an
//                         error_code and never crashes (§0 safety).
//   * defaults        : an absent vtable slot reads the schema default (if
//                         registered via with_default_int/real) else the
//                         universal FlatBuffers default (0/false/""). The
//                         decode tree fills defaults for absent scalar/string/
//                         vector fields and empty objects for absent tables and
//                         structs, so a value_io tree round-trips losslessly.
//   * u64 boundary    : the value_io integer node is int64, so a u64 wire
//                         value >= 2^63 (bit 63 set) cannot be represented
//                         faithfully: decode clamps such values to INT64_MAX
//                         (2^63 - 1). Values below 2^63 round-trip exactly.
//                         Typed parse_as<T>/serialize<T> shares this limit.
//   * wire writing    : forward, alignment-padded builder. Table bodies carry
//                         inline scalars/structs and 4-byte offsets for
//                         out-of-line strings/vectors/nested tables; each
//                         offset is patched after its target is built. Vtable
//                         entries cover the highest present slot. The output is
//                         spec-equivalent to the official back-to-front builder
//                         (same accessors read the same values), not
//                         byte-identical, since vtable deduplication and
//                         creation order are not reproduced.
//   * unions          : a union field registers as two schema entries: a
//                         union_type u8 slot plus a union_value slot carrying a
//                         variant table (enum value -> schema_info). Decode
//                         reads the paired type slot to select the variant; an
//                         unregistered variant is surfaced as an opaque byte
//                         string. None (enum 0) is represented by absent/null
//                         value keys.
//   * file_identifier : when schema<T>::identifier is set the decoder verifies
//                         buffer[4,8) and the encoder emits the 4 bytes.
//   * typed bridge    : serialize<T>/parse_as<T> walk the converter reflection
//                         chain (rtti::to_value/from_value) through the tree,
//                         so the same C++ structs round-trip; field names in
//                         RTTI_DEFINE_TYPE must match schema field_desc names.
//   * DoS guards      : max_depth caps nested tables (too_deep). All reads are
//                         bounds-checked, count/length prefixes are bounded
//                         against the remaining buffer (proto_truncated) and
//                         nonsensical tables/uoffsets fail with proto_bad_field
//                         rather than dereferencing out of range.
//
// Reuse of shared error codes: the flat adapter uses the generic wire codes
// too_deep / proto_truncated / proto_bad_field / kind_mismatch / invalid_root /
// overflow rather than introducing adapter-local codes.
//
// English comments only; C++17; no third-party dependencies. This header is
// never included by a reflect-core header, so the core stays adapter-free.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <rtti/rtti_fwd.hpp>
#if !defined(RTTI_ENABLE_FLATBUFFERS)
#define RTTI_ENABLE_FLATBUFFERS 1
#endif
#if !RTTI_ENABLE_FLATBUFFERS
#  error "RTTI: flatbuffers format adapter not enabled (compile with -DRTTI_ENABLE_FLATBUFFERS=1)"
#endif

#include <rtti/convert.hpp>
#include <rtti/error.hpp>
#include <rtti/reflection.hpp>

namespace rtti {
namespace flat {

// Wire kinds, mirroring the FlatBuffers type system. Scalars are read/written
// little-endian, aligned to their own size. string/vector/table/union_value are
// stored as uoffsets (4-byte, Table-relative distances). stct is a fixed-size
// inline structure described by a nested schema_info.
enum class fkind : std::uint8_t {
    none = 0,
    bool_,  // u8, nonzero = true
    i8,     // int8 (FlatBuffers "byte")
    u8,     // uint8
    i16,    // int16 ("short")
    u16,    // uint16 ("ushort")
    i32,    // int32 ("int")
    u32,    // uint32 ("uint")
    i64,    // int64 ("long")
    u64,    // uint64 ("ulong")
    f32,    // float
    f64,    // double
    string,     // offset to length-prefixed UTF-8 + trailing NUL
    vector,     // offset to length-prefixed element array (element kind in elem)
    table,      // offset to a nested table (schema in sub)
    stct,       // inline fixed-size struct (schema in sub)
    union_type, // inline u8 enum that selects the active union variant
    union_value // offset to a variant table (variants maps value -> schema)
};

struct schema_info;

// One name -> (slot, kind) mapping. `slot` is the vtable index (0-based
// FlatBuffers field id); for struct schemas the slot is unused and layout is
// declaration order. union_value fields pair with a union_type field through
// union_type_slot and a variant table selected by the wire enum value.
struct union_variant_entry {
    std::uint32_t value = 0;
    const schema_info* schema = nullptr;
};

struct field_desc {
    const char* name = nullptr;      // value_io tree key
    std::uint16_t slot = 0;          // vtable slot (union value/type are two slots)
    fkind kind = fkind::none;
    fkind elem = fkind::none;              // vector element kind
    const schema_info* sub = nullptr;      // stct/table schema, or vector element schema
    std::uint16_t union_type_slot = 0;     // union_value: paired union_type field slot
    const union_variant_entry* variants = nullptr; // union_value variant table
    std::size_t variant_count = 0;         // union_value variant table size
    bool has_default = false;              // explicit .fbs default (scalars)
    std::int64_t dflt_int = 0;
    double dflt_real = 0.0;
};

inline field_desc make_field_desc(const char* name, std::uint16_t slot,
                                  fkind kind) {
    field_desc d;
    d.name = name;
    d.slot = slot;
    d.kind = kind;
    return d;
}
inline field_desc make_field_desc(const char* name, std::uint16_t slot,
                                  fkind kind, fkind elem) {
    field_desc d = make_field_desc(name, slot, kind);
    d.elem = elem;
    return d;
}
inline field_desc make_field_desc(const char* name, std::uint16_t slot,
                                  fkind kind, fkind elem,
                                  const schema_info* sub) {
    field_desc d = make_field_desc(name, slot, kind, elem);
    d.sub = sub;
    return d;
}
// Descriptor for the value half of a union field. `type_slot` is the slot of
// the paired union_type field; `vars`/`n` map the union enum value to the C++
// schema of each variant.
inline field_desc make_union_value(const char* name, std::uint16_t slot,
                                   std::uint16_t type_slot,
                                   const union_variant_entry* vars,
                                   std::size_t n) {
    field_desc d = make_field_desc(name, slot, fkind::union_value);
    d.union_type_slot = type_slot;
    d.variants = vars;
    d.variant_count = n;
    return d;
}
inline field_desc with_default_int(field_desc d, std::int64_t v) {
    d.has_default = true;
    d.dflt_int = v;
    return d;
}
inline field_desc with_default_real(field_desc d, double v) {
    d.has_default = true;
    d.dflt_real = v;
    return d;
}

// The registered schema of a type. The primary template is an empty table, so
// unregistered types serialize to a header-only buffer and parse to an empty
// object. Each specialization sets count/fields (and optionally identifier).
template <typename T>
struct schema {
    static constexpr std::size_t count = 0;
    static inline const field_desc fields[1] = {};
    static inline const char* identifier = nullptr; // optional 4-byte file id
};

struct schema_info {
    const field_desc* fields = nullptr;
    std::size_t count = 0;
    const char* identifier = nullptr; // 4 bytes, e.g. "MONS"; null = none
};

namespace detail {
// schema<T>::identifier is optional in specializations; fall back to none.
template <typename T, typename = void>
struct schema_identifier_of {
    static inline const char* value() noexcept { return nullptr; }
};
template <typename T>
struct schema_identifier_of<T, std::void_t<decltype(schema<T>::identifier)>> {
    static inline const char* value() noexcept { return schema<T>::identifier; }
};
} // namespace detail

template <typename T>
inline const schema_info* schema_of() {
    static const schema_info si{schema<T>::fields, schema<T>::count,
                                detail::schema_identifier_of<T>::value()};
    return &si;
}

// Build one union variant entry: enum value `e` decodes/encodes with the
// schema of C++ type V.
template <typename V, typename E>
inline union_variant_entry variant_of(E e) {
    union_variant_entry en;
    en.value = static_cast<std::uint32_t>(e);
    en.schema = schema_of<V>();
    return en;
}

// Owned value_io tree produced by parse / wire_to_tree. The root is always an
// object; keys and scalar payloads are copied into the tree so it outlives the
// source wire buffer. Defined in <src/flat.cpp>.
class document : public rtti::value_io {
public:
    document();
    document(document&&) noexcept;
    document& operator=(document&&) noexcept;
    document(const document&) = delete;
    document& operator=(const document&) = delete;
    ~document() override;

    rtti::value_io::kind type() const noexcept override;
    bool as_bool(bool& out) const noexcept override;
    bool as_integer(std::int64_t& out) const noexcept override;
    bool as_real(double& out) const noexcept override;
    bool as_string(rtti::string_view& out) const noexcept override;
    std::size_t size() const noexcept override;
    rtti::value_io* at(std::size_t index) override;
    rtti::value_io* at(std::size_t index) const override;
    bool get_key(rtti::string_view key, rtti::value_io*& out) override;
    bool get_key(rtti::string_view key, rtti::value_io*& out) const override;
    bool get_key_at(std::size_t index, rtti::string_view& key,
                    rtti::value_io*& out) const override;
    rtti::value_io* make_array() override;
    void push_back(rtti::value_io* v) override;
    rtti::value_io* make_object() override;
    void set_key(rtti::string_view key, rtti::value_io* v) override;
    rtti::value_io* make_scalar(rtti::field_value v) override;
    rtti::value_io* make_node() override;

private:
    struct node;
    std::vector<std::unique_ptr<node>> pool_;
    node* root_ = nullptr;
    node* alloc_node();
    void reset() noexcept;
};

// Parse configuration. max_depth caps nested tables (too_deep on exceed);
// buffer-level malformed input always yields an error_code, never a crash.
struct parse_config {
    std::size_t max_depth = 512;
};

// --- Non-template wire layer (implemented in src/flat.cpp) ----------------

// Decode a FlatBuffers buffer whose root table matches `si` into `doc`.
error_code wire_to_tree(rtti::string_view src, const schema_info* si,
                        document& doc, const parse_config& cfg);
// Encode the object tree `root` (field keys per `si`) into out.
error_code tree_to_wire(const rtti::value_io* root, const schema_info* si,
                        std::string& out, const parse_config& cfg);
// True when src has at least 8 bytes and buffer[4,8) equals the 4-byte id.
bool verify_file_identifier(rtti::string_view src, const char* identifier);
// Schema-aware parse: returns an owned document tree (nil error on ok).
result<document> parse(rtti::string_view src, const schema_info* si,
                       const parse_config& cfg);

// --- Template wrappers ------------------------------------------------------

template <typename T>
inline error_code wire_to_tree(rtti::string_view src, document& doc,
                               const parse_config& cfg) {
    return wire_to_tree(src, schema_of<T>(), doc, cfg);
}
template <typename T>
inline error_code wire_to_tree(rtti::string_view src, document& doc) {
    return wire_to_tree(src, schema_of<T>(), doc, parse_config{});
}
template <typename T>
inline error_code tree_to_wire(const rtti::value_io* root, std::string& out,
                               const parse_config& cfg) {
    return tree_to_wire(root, schema_of<T>(), out, cfg);
}
template <typename T>
inline error_code tree_to_wire(const rtti::value_io* root, std::string& out) {
    return tree_to_wire(root, schema_of<T>(), out, parse_config{});
}
template <typename T>
inline result<document> parse(rtti::string_view src, const parse_config& cfg) {
    return parse(src, schema_of<T>(), cfg);
}
template <typename T>
inline result<document> parse(rtti::string_view src) {
    return parse(src, schema_of<T>(), parse_config{});
}

// Typed bridge: serialize a reflected object through the converter reflection
// chain (field names must match the schema), and parse_as restores it.
template <typename T>
inline error_code serialize(const T& obj, std::string& out) {
    document doc;
    const error_code e = rtti::to_value(&doc, obj);
    if (e != error_code::ok) {
        return e;
    }
    return tree_to_wire(&doc, schema_of<T>(), out, parse_config{});
}
// Convenience overload returning the wire bytes (partial/empty on error).
template <typename T>
inline std::string serialize(const T& obj) {
    std::string out;
    (void)serialize(obj, out);
    return out;
}
template <typename T>
inline result<T> parse_as(rtti::string_view src, const parse_config& cfg) {
    result<T> r;
    document doc;
    const error_code e = wire_to_tree(src, schema_of<T>(), doc, cfg);
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        return r;
    }
    const error_code e2 = rtti::from_value(&doc, r.value);
    if (e2 != error_code::ok) {
        r.err.code = e2;
        r.err.src = src.data();
        r.err.src_len = src.size();
        return r;
    }
    return r;
}
template <typename T>
inline result<T> parse_as(rtti::string_view src) {
    return parse_as<T>(src, parse_config{});
}

} // namespace flat
} // namespace rtti