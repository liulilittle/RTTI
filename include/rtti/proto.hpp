#pragma once
// protobuf 2/3 wire-format adapter for RTTI.
//
// Mirrors the JSON/XML/INI/TOML adapter architecture: a value_io tree bridge,
// parse/serialize, parse_as<T>/serialize<T> reflect bridging, a streaming SAX
// parse_stream and an RTTI_ENABLE_PROTO strip gate (default 1 through
// <rtti/rtti_fwd.hpp>). Zero third-party dependencies; C++17; header-only.
//
// Design (spec section 13):
//   * wire codecs      : varint (wire 0), fixed 64-bit LE (wire 1),
//                        length-delimited (wire 2), fixed 32-bit LE (wire 5).
//                        Encode/decode are bounded and truncated input fails
//                        with error_code::proto_truncated, never out of range.
//   * schema<T>        : explicit proto::schema<T> registration maps each C++
//                        reflected field NAME to a protobuf field NUMBER, wire
//                        type, mode (optional/required/repeated), packed flag
//                        and optional proto2 default. The RTTI_DEFINE_TYPE
//                        macro family is NOT extended (spec section 13.3).
//   * typed bridge     : serialize<T>(obj)/parse_as<T>(wire) walk the schema
//                        field descriptors (member-pointer accessors) directly.
//                        proto2 required-missing -> error_code::missing_key,
//                        explicit defaults applied on absent optional fields;
//                        proto3 scalar default values are omitted on the wire.
//   * tree bridge      : wire <-> value_io tree (document). A JSON/XML/INI
//                        tree read through rtti::value_io can be encoded to
//                        wire (tree_to_wire) and wire decodes into a document
//                        (wire_to_tree / parse). Unknown wire fields are
//                        skipped, never crash (spec section 13.3).
//   * SAX parse_stream : flat wire walk (message_start/message_end/field and
//                        typed value events), isomorphic to the other adapters;
//                        a callback returning false cancels with ok.
//   * DoS guards       : nesting depth cap (too_deep), length prefixes bounded
//                        against the remaining buffer, invalid field number 0
//                        and group wire types 3/4 rejected (proto_bad_field),
//                        schema/stream wire mismatch (proto_type_mismatch).
//
// Known limits (documented):
//   * sint32/sint64 zig-zag and importer .proto file parsing are out of scope;
//     int32/int64/uint32/uint64/bool/enum map to plain varint.
//   * Nested message members are encoded whenever structurally present (plain
//     C++ structs carry no presence bit); absent nested fields in a decoded
//     value_io tree are omitted (use the typed parse_as<T> path or a
//     _WITH_DEFAULT reflection registration to fill them via from_value).
//   * uint64 values above INT64_MAX round-trip through the typed path but not
//     through the value_io tree bridge (value_io stores int64 scalars).
//   * std::map members map to protobuf map entries (key=1 / value=2) using a
//     natural per-type wire mapping and must be registered with
//     field_mode::repeated.
//
// English comments only; C++17; no third-party dependencies. This header is
// never included by a reflect-core header, so the core stays adapter-free.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <rtti/rtti_fwd.hpp>
#if !defined(RTTI_ENABLE_PROTO) || !RTTI_ENABLE_PROTO
#  error "RTTI: protobuf format adapter not enabled (compile with -DRTTI_ENABLE_PROTO=1)"
#endif

#include <rtti/convert.hpp>
#include <rtti/error.hpp>
#include <rtti/reflection.hpp>

namespace rtti {
namespace proto {

// ---------------------------------------------------------------------------
// Wire-level declarations
// ---------------------------------------------------------------------------

// protobuf wire syntax. proto2 allows required fields/explicit default values;
// proto3 scalar defaults are omitted on the wire and absent scalars read zero.
enum class syntax : std::uint8_t { proto2, proto3 };
enum class field_mode : std::uint8_t { optional, required, repeated };
enum class wire_type : std::uint8_t {
    varint = 0,
    fixed64 = 1,
    length = 2,
    fixed32 = 5
};

// Field tag: (number << 3) | wire type. Field numbers are 1..(2^29 - 1).
inline std::uint32_t make_tag(std::uint32_t number, wire_type wt) noexcept {
    return (number << 3) | static_cast<std::uint32_t>(wt);
}

// Encode a uint64 as a base-128 varint. Appends to `out`.
inline bool encode_varint(std::uint64_t value, std::string& out) {
    for (;;) {
        const unsigned char b = static_cast<unsigned char>(value & 0x7Fu);
        value >>= 7;
        if (value == 0) {
            out.push_back(static_cast<char>(b));
            return true;
        }
        out.push_back(static_cast<char>(b | 0x80u));
    }
}

// Decode a varint from the start of `src`. On success `value` holds the number
// and `used` the number of consumed bytes. Returns false on a truncated varint
// (continuation bit set at EOF) or when bits beyond the 64-bit range appear.
inline bool decode_varint(rtti::string_view src, std::uint64_t& value,
                          std::size_t& used) {
    const unsigned char* const start =
        reinterpret_cast<const unsigned char*>(src.data());
    const unsigned char* p = start;
    const unsigned char* const end = start + src.size();
    std::uint64_t v = 0;
    for (int shift = 0; shift <= 63; shift += 7) {
        if (p >= end) {
            return false;
        }
        const unsigned char b = *p++;
        // On the 10th byte only bit 63 is meaningful; bits 64..69 must be 0.
        if (shift == 63 && (b & 0x7Eu) != 0u) {
            return false;
        }
        v |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0u) {
            value = v;
            used = static_cast<std::size_t>(p - start);
            return true;
        }
    }
    return false; // continuation runs past the 10th byte
}

// Zig-zag mapping for the protobuf sint32/sint64 scalar types: signed values
// of small magnitude map to small positive varints. Encode is bijective on the
// full int64 range; decode is its exact inverse. The left shift runs in
// unsigned space, so the mapping is well-defined for every int64 input
// (a signed E1 << E2 would be UB in C++17 when the result is not
// representable); the canonical (v << 1) ^ (v >> 63) and
// (u >> 1) ^ -(u & 1) mappings are reproduced bit-for-bit.
inline std::uint64_t zigzag_encode(std::int64_t v) noexcept {
    const std::uint64_t u = static_cast<std::uint64_t>(v);
    const std::uint64_t sign = static_cast<std::uint64_t>(v >> 63);
    return (u << 1) ^ sign;
}
inline std::int64_t zigzag_decode(std::uint64_t u) noexcept {
    return static_cast<std::int64_t>((u >> 1) ^ (0ULL - (u & 1ULL)));
}

namespace detail {

// Bounded wire reader over a source buffer. Every read checks against the end
// pointer first, so malformed input never reads out of range.
struct wire_reader {
    const unsigned char* cur;
    const unsigned char* end;
    const unsigned char* begin;

    explicit wire_reader(rtti::string_view src) noexcept
        : cur(reinterpret_cast<const unsigned char*>(src.data())),
          end(cur + src.size()),
          begin(cur) {}

    std::size_t remaining() const noexcept {
        return static_cast<std::size_t>(end - cur);
    }
    std::size_t offset() const noexcept {
        return static_cast<std::size_t>(cur - begin);
    }

    bool read_varint(std::uint64_t& out) {
        const unsigned char* next = nullptr;
        if (!decode_varint_bytes(cur, end, out, next)) {
            return false;
        }
        cur = next;
        return true;
    }
    bool read_fixed64(std::uint64_t& out) {
        if (remaining() < 8) {
            return false;
        }
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out |= static_cast<std::uint64_t>(cur[i]) << (8 * i);
        }
        cur += 8;
        return true;
    }
    bool read_fixed32(std::uint32_t& out) {
        if (remaining() < 4) {
            return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            out |= static_cast<std::uint32_t>(cur[i]) << (8 * i);
        }
        cur += 4;
        return true;
    }
    // Read a length prefix and validate it against the remaining bytes.
    bool read_length(std::uint64_t& len) {
        if (!read_varint(len)) {
            return false;
        }
        if (len > remaining()) {
            return false;
        }
        return true;
    }
    rtti::string_view take(std::uint64_t len) {
        rtti::string_view s(reinterpret_cast<const char*>(cur),
                            static_cast<std::size_t>(len));
        cur += len;
        return s;
    }

private:
    static bool decode_varint_bytes(const unsigned char* p,
                                    const unsigned char* end,
                                    std::uint64_t& out,
                                    const unsigned char*& next) {
        std::uint64_t v = 0;
        for (int shift = 0; shift <= 63; shift += 7) {
            if (p >= end) {
                return false;
            }
            const unsigned char b = *p++;
            if (shift == 63 && (b & 0x7Eu) != 0u) {
                return false;
            }
            v |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0u) {
                next = p;
                out = v;
                return true;
            }
        }
        return false;
    }
};

// Wire writer appending into a caller-owned std::string.
struct wire_writer {
    std::string* out;

    explicit wire_writer(std::string& o) noexcept : out(&o) {}

    void varint(std::uint64_t v) { (void)encode_varint(v, *out); }
    void fixed32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out->push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
        }
    }
    void fixed64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            out->push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
        }
    }
    void bytes(const char* p, std::size_t n) { out->append(p, n); }
    bool tag(std::uint32_t number, wire_type wt) {
        if (number == 0 || number > 0x1FFFFFFFu) {
            return false;
        }
        varint(static_cast<std::uint64_t>(make_tag(number, wt)));
        return true;
    }
};

inline bool is_numeric_wire(wire_type wt) noexcept {
    return wt == wire_type::varint || wt == wire_type::fixed32 ||
           wt == wire_type::fixed64;
}

inline bool valid_wire_type(wire_type wt) noexcept {
    return wt == wire_type::varint || wt == wire_type::fixed64 ||
           wt == wire_type::length || wt == wire_type::fixed32;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Schema registration
// ---------------------------------------------------------------------------

// One registered proto field: the reflected C++ field NAME plus the protobuf
// field number, wire type, mode, packed flag and optional proto2 default.
struct field_desc {
    const char* name = nullptr;
    std::uint32_t number = 0;
    wire_type wire = wire_type::varint;
    field_mode mode = field_mode::optional;
    bool packed = false;
    bool zigzag = false; // sint32/sint64: zig-zag the signed value before varint
    bool has_default = false;
    std::int64_t dflt_int = 0;
    double dflt_real = 0.0;
    const char* dflt_str = nullptr;
    std::size_t dflt_str_len = 0;
};

inline field_desc make_field_desc(const char* name, std::uint32_t number,
                                  wire_type wt,
                                  field_mode mode = field_mode::optional,
                                  bool packed = false) {
    field_desc d;
    d.name = name;
    d.number = number;
    d.wire = wt;
    d.mode = mode;
    d.packed = packed;
    return d;
}
inline field_desc with_default_int(field_desc d, std::int64_t v) {
    d.has_default = true;
    d.dflt_int = v;
    return d;
}
// Mark a varint field as a protobuf sint32/sint64: the signed value is
// zig-zag encoded (x -> (x<<1)^(x>>63)) before the varint on the wire.
inline field_desc with_zigzag(field_desc d) {
    d.zigzag = true;
    return d;
}
inline field_desc with_default_real(field_desc d, double v) {
    d.has_default = true;
    d.dflt_real = v;
    return d;
}
inline field_desc with_default_str(field_desc d, const char* s,
                                   std::size_t n) {
    d.has_default = true;
    d.dflt_str = s;
    d.dflt_str_len = n;
    return d;
}

// A member is a protobuf repeated container when it is a sequence or a set
// (char strings are excluded -- they are scalar text).
template <typename V, typename = void>
struct is_proto_repeated : std::false_type {};
template <typename V>
struct is_proto_repeated<
    V, std::void_t<typename V::value_type,
                   decltype(std::declval<V&>().begin()),
                   decltype(std::declval<V&>().end())>>
    : std::bool_constant<rtti::is_sequence<V>::value || rtti::is_set<V>::value> {
};

// Type-erased per-field codec bound to a reflected C++ member.
struct field_base {
    field_desc info;

    explicit field_base(field_desc d) : info(std::move(d)) {}
    virtual ~field_base() = default;

    // --- typed object path ------------------------------------------------
    // Encode the whole field (all occurrences) from a C++ object.
    virtual error_code encode_obj(const void* obj, detail::wire_writer& w,
                                  std::size_t depth,
                                  std::size_t max_depth) const = 0;
    // Decode ONE occurrence (singular scalar/optional/message) into object.
    virtual error_code decode_obj(void* obj, detail::wire_reader& r,
                                  std::size_t depth,
                                  std::size_t max_depth) const = 0;
    // Decode a packed payload into a repeated member.
    virtual error_code decode_packed(void* obj, detail::wire_reader& r,
                                     std::size_t depth,
                                     std::size_t max_depth) const = 0;
    // Decode one tagged occurrence into a repeated member.
    virtual error_code decode_repeated(void* obj, detail::wire_reader& r,
                                       std::size_t depth,
                                       std::size_t max_depth) const = 0;
    // True when the field has anything to encode for this object.
    virtual bool should_encode(const void* obj) const = 0;
    // Apply the proto2 explicit default (or zero default) to an absent field.
    virtual error_code set_default(void* obj) const = 0;

    // --- value_io tree path ----------------------------------------------
    // Encode ONE value read from a value_io node.
    virtual error_code tree_encode(const rtti::value_io* node,
                                   detail::wire_writer& w, std::size_t depth,
                                   std::size_t max_depth) const = 0;
    // Decode ONE occurrence from the reader into a fresh value_io node.
    virtual error_code tree_decode(detail::wire_reader& r,
                                   rtti::value_io* node, std::size_t depth,
                                   std::size_t max_depth) const = 0;
    // Decode a packed payload, appending elements to the array under obj.
    virtual error_code tree_decode_packed(rtti::value_io* obj,
                                          detail::wire_reader& r,
                                          std::size_t depth,
                                          std::size_t max_depth) const = 0;
    // Decode one occurrence into the array under a repeated field of obj.
    virtual error_code tree_decode_repeated(rtti::value_io* obj,
                                            detail::wire_reader& r,
                                            std::size_t depth,
                                            std::size_t max_depth) const = 0;
    // True when the node holds this field's default value.
    virtual bool tree_equals_default(const rtti::value_io* node) const = 0;
    // Write this field's default value into a fresh node.
    virtual error_code tree_set_default(rtti::value_io* node) const = 0;
};

// The registered schema of a type. Specialize for each message struct (see
// make_field / make_field_desc). The primary template is an empty proto3
// message, so unregistered types serialize to zero bytes and parse as their
// default-constructed object.
template <typename T>
struct schema {
    static constexpr std::size_t count = 0;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[1] = {nullptr};
};

// Type-erased view of a schema used by the tree bridge and the free codecs.
struct schema_info {
    syntax syn;
    std::size_t count;
    const field_base* const* fields;
};

template <typename T>
inline const schema_info* schema_of() {
    static const schema_info si{schema<T>::syn, schema<T>::count,
                                schema<T>::array};
    return &si;
}

namespace detail {

inline const field_base* find_field(const schema_info& si,
                                    std::uint32_t number) noexcept {
    for (std::size_t i = 0; i < si.count; ++i) {
        const field_base* f = si.fields[i];
        if (f != nullptr && f->info.number == number) {
            return f;
        }
    }
    return nullptr;
}

inline const field_base* find_field_by_name(const schema_info& si,
                                            rtti::string_view name) noexcept {
    for (std::size_t i = 0; i < si.count; ++i) {
        const field_base* f = si.fields[i];
        if (f != nullptr && f->info.name != nullptr &&
            rtti::string_view(f->info.name) == name) {
            return f;
        }
    }
    return nullptr;
}

// Consume one unknown (or unsupported) field's payload so parsing continues.
inline error_code skip_field(detail::wire_reader& r, wire_type wt) {
    switch (wt) {
        case wire_type::varint: {
            std::uint64_t v = 0;
            if (!r.read_varint(v)) {
                return error_code::proto_truncated;
            }
            return error_code::ok;
        }
        case wire_type::fixed64: {
            std::uint64_t v = 0;
            if (!r.read_fixed64(v)) {
                return error_code::proto_truncated;
            }
            return error_code::ok;
        }
        case wire_type::fixed32: {
            std::uint32_t v = 0;
            if (!r.read_fixed32(v)) {
                return error_code::proto_truncated;
            }
            return error_code::ok;
        }
        case wire_type::length: {
            std::uint64_t len = 0;
            if (!r.read_length(len)) {
                return error_code::proto_truncated;
            }
            (void)r.take(len);
            return error_code::ok;
        }
        default:
            return error_code::proto_bad_field;
    }
}

// Encode every field of `obj` (as declared by `si`) into `w`.
template <typename T>
error_code encode_message(const T& obj, detail::wire_writer& w,
                          const schema_info& si, std::size_t depth,
                          std::size_t max_depth) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    for (std::size_t i = 0; i < si.count; ++i) {
        const field_base* f = si.fields[i];
        if (f == nullptr || !f->should_encode(&obj)) {
            continue;
        }
        const error_code e = f->encode_obj(&obj, w, depth, max_depth);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

// Decode every field of the wire message into `obj`, enforcing required
// presence and applying defaults to absent optional scalar fields.
template <typename T>
error_code decode_message(T& obj, detail::wire_reader& r,
                          const schema_info& si, std::size_t depth,
                          std::size_t max_depth) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    std::vector<std::uint32_t> seen;
    seen.reserve(si.count);
    while (r.remaining() > 0) {
        std::uint64_t tagv = 0;
        if (!r.read_varint(tagv)) {
            return error_code::proto_truncated;
        }
        const std::uint32_t field_no = static_cast<std::uint32_t>(tagv >> 3);
        const wire_type wt = static_cast<wire_type>(tagv & 7u);
        if (field_no == 0u || !valid_wire_type(wt)) {
            return error_code::proto_bad_field;
        }
        const field_base* f = find_field(si, field_no);
        if (f == nullptr) {
            const error_code e = skip_field(r, wt);
            if (e != error_code::ok) {
                return e;
            }
            continue;
        }
        error_code e = error_code::ok;
        if (f->info.mode == field_mode::repeated) {
            if (wt == wire_type::length && is_numeric_wire(f->info.wire)) {
                // A numeric repeated field also accepts a packed payload.
                e = f->decode_packed(&obj, r, depth, max_depth);
            } else if (wt == f->info.wire) {
                e = f->decode_repeated(&obj, r, depth, max_depth);
            } else {
                return error_code::proto_type_mismatch;
            }
        } else {
            if (wt != f->info.wire) {
                return error_code::proto_type_mismatch;
            }
            e = f->decode_obj(&obj, r, depth, max_depth);
        }
        if (e != error_code::ok) {
            return e;
        }
        seen.push_back(field_no);
    }
    for (std::size_t i = 0; i < si.count; ++i) {
        const field_base* f = si.fields[i];
        if (f == nullptr) {
            continue;
        }
        if (std::find(seen.begin(), seen.end(), f->info.number) != seen.end()) {
            continue;
        }
        if (f->info.mode == field_mode::required) {
            return error_code::missing_key;
        }
        if (f->info.mode == field_mode::optional) {
            const error_code e = f->set_default(&obj);
            if (e != error_code::ok) {
                return e;
            }
        }
    }
    return error_code::ok;
}

// Create (or reuse) the array node under a repeated field of a message node.
inline rtti::value_io* ensure_array(rtti::value_io* obj, const char* name) {
    rtti::value_io* out = nullptr;
    if (obj->get_key(rtti::string_view(name), out) && out != nullptr) {
        return out;
    }
    rtti::value_io* arr = obj->make_node();
    arr->make_array();
    obj->set_key(rtti::string_view(name), arr);
    return arr;
}

// Encode a value_io message node (object) into `w`, applying proto2/proto3
// default-omission and repeated packing rules.
error_code encode_message_tree(const rtti::value_io* obj, detail::wire_writer& w,
                               const schema_info& si, std::size_t depth,
                               std::size_t max_depth);

// Decode a wire message into a value_io object node, filling scalar defaults
// and empty arrays for absent fields.
error_code decode_message_tree(rtti::value_io* obj, detail::wire_reader& r,
                               const schema_info& si, std::size_t depth,
                               std::size_t max_depth);

// Schema-less wire -> tree: object keys are decimal field numbers.
error_code decode_generic(rtti::value_io* obj, detail::wire_reader& r,
                          std::size_t depth, std::size_t max_depth);

} // namespace detail

// ---------------------------------------------------------------------------
// Typed field descriptor
// ---------------------------------------------------------------------------

// Binds one reflected member `T::*` to its field_desc and implements every
// wire/tree operation for the member's concrete C++ type through if-constexpr
// dispatch on the member traits.
template <typename T, typename M>
struct field : field_base {
    M T::*ptr;

    field(field_desc d, M T::*p) : field_base(std::move(d)), ptr(p) {}

    M& member(void* obj) const { return (*static_cast<T*>(obj)).*ptr; }
    const M& cmember(const void* obj) const {
        return (*static_cast<const T*>(obj)).*ptr;
    }

    // --- scalar wire writers ----------------------------------------------

    template <typename V>
    error_code write_varint(const V& val, detail::wire_writer& w) const {
        if constexpr (std::is_same<V, bool>::value) {
            w.varint(val ? 1 : 0);
            return error_code::ok;
        } else if constexpr (std::is_enum<V>::value) {
            // Enums encode as their underlying value (sign-extended to 64-bit
            // when the underlying type is signed).
            w.varint(static_cast<std::uint64_t>(
                static_cast<std::int64_t>(val)));
            return error_code::ok;
        } else if constexpr (std::is_integral<V>::value) {
            if constexpr (std::is_signed<V>::value) {
                const std::int64_t sv = static_cast<std::int64_t>(val);
                w.varint(info.zigzag ? zigzag_encode(sv)
                                     : static_cast<std::uint64_t>(sv));
            } else {
                w.varint(static_cast<std::uint64_t>(val));
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    template <typename V>
    error_code write_fixed64(const V& val, detail::wire_writer& w) const {
        if constexpr (std::is_same<V, double>::value) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &val, sizeof(double));
            w.fixed64(bits);
            return error_code::ok;
        } else if constexpr (std::is_integral<V>::value) {
            if constexpr (std::is_signed<V>::value) {
                w.fixed64(static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(val)));
            } else {
                w.fixed64(static_cast<std::uint64_t>(val));
            }
            return error_code::ok;
        } else if constexpr (std::is_enum<V>::value) {
            w.fixed64(static_cast<std::uint64_t>(
                static_cast<std::int64_t>(val)));
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    template <typename V>
    error_code write_fixed32(const V& val, detail::wire_writer& w) const {
        if constexpr (std::is_same<V, float>::value) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &val, sizeof(float));
            w.fixed32(bits);
            return error_code::ok;
        } else if constexpr (std::is_integral<V>::value) {
            if constexpr (std::is_signed<V>::value) {
                w.fixed32(static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(val)));
            } else {
                w.fixed32(static_cast<std::uint32_t>(val));
            }
            return error_code::ok;
        } else if constexpr (std::is_enum<V>::value) {
            w.fixed32(static_cast<std::uint32_t>(
                static_cast<std::int64_t>(val)));
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    template <typename V>
    error_code write_length(const V& val, detail::wire_writer& w,
                            std::size_t depth, std::size_t max_depth) const {
        if constexpr (rtti::is_char_string<V>::value) {
            w.varint(val.size());
            w.bytes(val.data(), val.size());
            return error_code::ok;
        } else if constexpr (rtti::is_reflected<V>::value) {
            std::string payload;
            detail::wire_writer pw(payload);
            const error_code e =
                detail::encode_message(val, pw, *schema_of<V>(), depth + 1,
                                       max_depth);
            if (e != error_code::ok) {
                return e;
            }
            w.varint(payload.size());
            w.bytes(payload.data(), payload.size());
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    // Encode one element with the member's declared wire type.
    template <typename V>
    error_code encode_element(const V& val, detail::wire_writer& w,
                              std::size_t depth,
                              std::size_t max_depth) const {
        switch (info.wire) {
            case wire_type::varint:
                return write_varint(val, w);
            case wire_type::fixed64:
                return write_fixed64(val, w);
            case wire_type::fixed32:
                return write_fixed32(val, w);
            case wire_type::length:
                return write_length(val, w, depth, max_depth);
        default:
            return error_code::proto_bad_field;
    }
}

    // --- scalar wire readers ----------------------------------------------

    template <typename V>
    error_code read_varint(V& val, detail::wire_reader& r) const {
        std::uint64_t v = 0;
        if (!r.read_varint(v)) {
            return error_code::proto_truncated;
        }
        return assign_varint(val, v);
    }

    template <typename V>
    error_code assign_varint(V& val, std::uint64_t v) const {
        if constexpr (std::is_same<V, bool>::value) {
            val = (v != 0);
            return error_code::ok;
        } else if constexpr (std::is_enum<V>::value) {
            val = static_cast<V>(static_cast<std::int32_t>(v));
            return error_code::ok;
        } else if constexpr (std::is_integral<V>::value) {
            if constexpr (std::is_signed<V>::value) {
                const std::int64_t dv =
                    info.zigzag ? zigzag_decode(v)
                                : static_cast<std::int64_t>(v);
                if constexpr (sizeof(V) <= 4) {
                    val = static_cast<V>(static_cast<std::int32_t>(dv));
                } else {
                    val = static_cast<V>(dv);
                }
            } else {
                val = static_cast<V>(v);
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    template <typename V>
    error_code read_fixed64(V& val, detail::wire_reader& r) const {
        std::uint64_t v = 0;
        if (!r.read_fixed64(v)) {
            return error_code::proto_truncated;
        }
        if constexpr (std::is_same<V, double>::value) {
            std::memcpy(&val, &v, sizeof(double));
            return error_code::ok;
        } else if constexpr (std::is_integral<V>::value ||
                             std::is_enum<V>::value) {
            if constexpr (std::is_signed<V>::value) {
                val = static_cast<V>(static_cast<std::int64_t>(v));
            } else {
                val = static_cast<V>(v);
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    template <typename V>
    error_code read_fixed32(V& val, detail::wire_reader& r) const {
        std::uint32_t v = 0;
        if (!r.read_fixed32(v)) {
            return error_code::proto_truncated;
        }
        if constexpr (std::is_same<V, float>::value) {
            std::memcpy(&val, &v, sizeof(float));
            return error_code::ok;
        } else if constexpr (std::is_integral<V>::value ||
                             std::is_enum<V>::value) {
            if constexpr (std::is_signed<V>::value && sizeof(V) <= 4) {
                val = static_cast<V>(static_cast<std::int32_t>(v));
            } else if constexpr (std::is_signed<V>::value) {
                val = static_cast<V>(static_cast<std::int64_t>(v));
            } else {
                val = static_cast<V>(v);
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    template <typename V>
    error_code read_length(V& val, detail::wire_reader& r, std::size_t depth,
                           std::size_t max_depth) const {
        std::uint64_t len = 0;
        if (!r.read_length(len)) {
            return error_code::proto_truncated;
        }
        const rtti::string_view data = r.take(len);
        if constexpr (rtti::is_char_string<V>::value) {
            val.assign(data.data(), data.size());
            return error_code::ok;
        } else if constexpr (rtti::is_reflected<V>::value) {
            detail::wire_reader sub(data);
            const error_code e =
                detail::decode_message(val, sub, *schema_of<V>(), depth + 1,
                                       max_depth);
            if (e != error_code::ok) {
                return e;
            }
            if (sub.remaining() != 0) {
                return error_code::proto_truncated;
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    template <typename V>
    error_code decode_element(V& val, detail::wire_reader& r,
                              std::size_t depth,
                              std::size_t max_depth) const {
        switch (info.wire) {
            case wire_type::varint:
                return read_varint(val, r);
            case wire_type::fixed64:
                return read_fixed64(val, r);
            case wire_type::fixed32:
                return read_fixed32(val, r);
            case wire_type::length:
                return read_length(val, r, depth, max_depth);
        default:
            return error_code::proto_bad_field;
    }
}

    // --- presence / defaults ----------------------------------------------

    template <typename V>
    bool equals_default_val(const V& v) const {
        if constexpr (std::is_same<V, bool>::value) {
            return (v ? 1 : 0) == (info.has_default ? info.dflt_int : 0);
        } else if constexpr (std::is_enum<V>::value) {
            return static_cast<std::int64_t>(v) ==
                   (info.has_default ? info.dflt_int : 0);
        } else if constexpr (std::is_integral<V>::value) {
            if constexpr (std::is_signed<V>::value) {
                return static_cast<std::int64_t>(v) ==
                       (info.has_default ? info.dflt_int : 0);
            } else {
                return static_cast<std::uint64_t>(v) ==
                       static_cast<std::uint64_t>(
                           info.has_default ? info.dflt_int : 0);
            }
        } else if constexpr (std::is_floating_point<V>::value) {
            return static_cast<double>(v) ==
                   (info.has_default ? info.dflt_real : 0.0);
        } else if constexpr (rtti::is_char_string<V>::value) {
            if (!info.has_default || info.dflt_str == nullptr) {
                return v.empty();
            }
            return rtti::string_view(v.data(), v.size()) ==
                   rtti::string_view(info.dflt_str, info.dflt_str_len);
        } else {
            return false;
        }
    }

    bool should_encode(const void* obj) const override {
        const M& val = cmember(obj);
        if constexpr (rtti::is_optional<M>::value) {
            return val.has_value();
        } else if constexpr (rtti::is_map<M>::value ||
                             is_proto_repeated<M>::value) {
            return !val.empty();
        } else if constexpr (rtti::is_char_string<M>::value ||
                             std::is_same<M, bool>::value ||
                             std::is_integral<M>::value ||
                             std::is_floating_point<M>::value ||
                             std::is_enum<M>::value) {
            if (info.mode == field_mode::required) {
                return true;
            }
            return !equals_default_val(val);
        } else {
            // Nested message members are always structurally present.
            return true;
        }
    }

    error_code set_default(void* obj) const override {
        M& val = member(obj);
        if constexpr (rtti::is_optional<M>::value) {
            val.reset();
        } else if constexpr (rtti::is_char_string<M>::value) {
            val.assign(info.dflt_str ? info.dflt_str : "", info.dflt_str_len);
        } else if constexpr (std::is_same<M, bool>::value) {
            val = info.has_default ? (info.dflt_int != 0) : false;
        } else if constexpr (std::is_floating_point<M>::value) {
            val = static_cast<M>(info.has_default ? info.dflt_real : 0.0);
        } else if constexpr (std::is_integral<M>::value) {
            val = static_cast<M>(info.has_default ? info.dflt_int : 0);
        } else if constexpr (std::is_enum<M>::value) {
            val = static_cast<M>(info.has_default ? info.dflt_int : 0);
        }
        return error_code::ok;
    }

    // --- typed object overrides -------------------------------------------

    error_code encode_obj(const void* obj, detail::wire_writer& w,
                          std::size_t depth,
                          std::size_t max_depth) const override {
        return encode_field(cmember(obj), w, depth, max_depth);
    }

    template <typename V>
    error_code encode_field(const V& val, detail::wire_writer& w,
                            std::size_t depth, std::size_t max_depth) const {
        if constexpr (rtti::is_map<V>::value) {
            return encode_map_values(val, w, depth, max_depth);
        } else if constexpr (is_proto_repeated<V>::value) {
            return encode_repeated_values(val, w, depth, max_depth);
        } else if constexpr (rtti::is_optional<V>::value) {
            return encode_single(*val, w, depth, max_depth);
        } else {
            return encode_single(val, w, depth, max_depth);
        }
    }

    template <typename V>
    error_code encode_single(const V& val, detail::wire_writer& w,
                             std::size_t depth,
                             std::size_t max_depth) const {
        if (!w.tag(info.number, info.wire)) {
            return error_code::proto_bad_field;
        }
        return encode_element(val, w, depth, max_depth);
    }

    template <typename C>
    error_code encode_repeated_values(const C& c, detail::wire_writer& w,
                                      std::size_t depth,
                                      std::size_t max_depth) const {
        if (c.empty()) {
            return error_code::ok;
        }
        if (info.packed && detail::is_numeric_wire(info.wire)) {
            std::string payload;
            detail::wire_writer pw(payload);
            for (const auto& e : c) {
                const error_code e2 = encode_element(e, pw, depth, max_depth);
                if (e2 != error_code::ok) {
                    return e2;
                }
            }
            if (!w.tag(info.number, wire_type::length)) {
                return error_code::proto_bad_field;
            }
            w.varint(payload.size());
            w.bytes(payload.data(), payload.size());
            return error_code::ok;
        }
        for (const auto& e : c) {
            if (!w.tag(info.number, info.wire)) {
                return error_code::proto_bad_field;
            }
            const error_code e2 = encode_element(e, w, depth, max_depth);
            if (e2 != error_code::ok) {
                return e2;
            }
        }
        return error_code::ok;
    }

    template <typename V>
    static constexpr wire_type natural_wire() noexcept {
        if constexpr (rtti::is_char_string<V>::value ||
                      rtti::is_reflected<V>::value) {
            return wire_type::length;
        } else if constexpr (std::is_same<V, double>::value) {
            return wire_type::fixed64;
        } else if constexpr (std::is_same<V, float>::value) {
            return wire_type::fixed32;
        } else {
            return wire_type::varint;
        }
    }

    template <typename V>
    error_code write_natural(const V& val, detail::wire_writer& w,
                             std::size_t depth,
                             std::size_t max_depth) const {
        switch (natural_wire<V>()) {
            case wire_type::varint:
                return write_varint(val, w);
            case wire_type::fixed64:
                return write_fixed64(val, w);
            case wire_type::fixed32:
                return write_fixed32(val, w);
            case wire_type::length:
                return write_length(val, w, depth, max_depth);
        default:
            return error_code::proto_bad_field;
    }
}

    template <typename V>
    error_code read_natural(V& val, detail::wire_reader& r,
                            std::size_t depth,
                            std::size_t max_depth) const {
        switch (natural_wire<V>()) {
            case wire_type::varint:
                return read_varint(val, r);
            case wire_type::fixed64:
                return read_fixed64(val, r);
            case wire_type::fixed32:
                return read_fixed32(val, r);
            case wire_type::length:
                return read_length(val, r, depth, max_depth);
        default:
            return error_code::proto_bad_field;
    }
}

    template <typename Map>
    error_code encode_map_values(const Map& m, detail::wire_writer& w,
                                 std::size_t depth,
                                 std::size_t max_depth) const {
        using K = typename Map::key_type;
        using V = typename Map::mapped_type;
        for (const auto& kv : m) {
            std::string payload;
            detail::wire_writer pw(payload);
            if (!pw.tag(1, natural_wire<K>())) {
                return error_code::proto_bad_field;
            }
            const error_code ek = write_natural(kv.first, pw, depth, max_depth);
            if (ek != error_code::ok) {
                return ek;
            }
            if (!pw.tag(2, natural_wire<V>())) {
                return error_code::proto_bad_field;
            }
            const error_code ev =
                write_natural(kv.second, pw, depth, max_depth);
            if (ev != error_code::ok) {
                return ev;
            }
            if (!w.tag(info.number, wire_type::length)) {
                return error_code::proto_bad_field;
            }
            w.varint(payload.size());
            w.bytes(payload.data(), payload.size());
        }
        return error_code::ok;
    }

    error_code decode_map_entry(void* obj, detail::wire_reader& r,
                                std::size_t depth,
                                std::size_t max_depth) const {
        std::uint64_t len = 0;
        if (!r.read_length(len)) {
            return error_code::proto_truncated;
        }
        if (len > r.remaining()) {
            return error_code::proto_truncated;
        }
        const rtti::string_view payload = r.take(len);
        detail::wire_reader sub(payload);
        typename M::key_type key{};
        typename M::mapped_type value{};
        while (sub.remaining() > 0) {
            std::uint64_t tagv = 0;
            if (!sub.read_varint(tagv)) {
                return error_code::proto_truncated;
            }
            const std::uint32_t no = static_cast<std::uint32_t>(tagv >> 3);
            const wire_type wt = static_cast<wire_type>(tagv & 7u);
            if (no == 0u || !detail::valid_wire_type(wt)) {
                return error_code::proto_bad_field;
            }
            error_code e = error_code::ok;
            if (no == 1) {
                if (wt != natural_wire<typename M::key_type>()) {
                    return error_code::proto_type_mismatch;
                }
                e = read_natural(key, sub, depth, max_depth);
            } else if (no == 2) {
                if (wt != natural_wire<typename M::mapped_type>()) {
                    return error_code::proto_type_mismatch;
                }
                e = read_natural(value, sub, depth, max_depth);
            } else {
                e = detail::skip_field(sub, wt);
            }
            if (e != error_code::ok) {
                return e;
            }
        }
        member(obj).emplace(std::move(key), std::move(value));
        return error_code::ok;
    }

    template <typename C, typename V>
    static void push_element(C& c, V&& v) {
        if constexpr (rtti::is_set<C>::value) {
            c.insert(std::forward<V>(v));
        } else {
            c.push_back(std::forward<V>(v));
        }
    }

    error_code decode_obj(void* obj, detail::wire_reader& r,
                          std::size_t depth,
                          std::size_t max_depth) const override {
        M& val = member(obj);
        if constexpr (rtti::is_optional<M>::value) {
            typename M::value_type v{};
            const error_code e = decode_element(v, r, depth, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            val = std::move(v);
            return error_code::ok;
        } else {
            return decode_element(val, r, depth, max_depth);
        }
    }

    error_code decode_packed(void* obj, detail::wire_reader& r,
                             std::size_t depth,
                             std::size_t max_depth) const override {
        if constexpr (is_proto_repeated<M>::value) {
            std::uint64_t len = 0;
            if (!r.read_length(len)) {
                return error_code::proto_truncated;
            }
            if (len > r.remaining()) {
                return error_code::proto_truncated;
            }
            const rtti::string_view payload = r.take(len);
            detail::wire_reader sub(payload);
            while (sub.remaining() > 0) {
                typename M::value_type v{};
                const error_code e =
                    decode_element(v, sub, depth, max_depth);
                if (e != error_code::ok) {
                    return e;
                }
                push_element(member(obj), std::move(v));
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    error_code decode_repeated(void* obj, detail::wire_reader& r,
                               std::size_t depth,
                               std::size_t max_depth) const override {
        if constexpr (rtti::is_map<M>::value) {
            return decode_map_entry(obj, r, depth, max_depth);
        } else if constexpr (is_proto_repeated<M>::value) {
            typename M::value_type v{};
            const error_code e = decode_element(v, r, depth, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            push_element(member(obj), std::move(v));
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    // --- tree path overrides ----------------------------------------------

    error_code tree_encode(const rtti::value_io* node, detail::wire_writer& w,
                           std::size_t depth,
                           std::size_t max_depth) const override {
        if (node == nullptr) {
            return error_code::ok;
        }
        switch (info.wire) {
            case wire_type::varint:
                return tree_write_varint(node, w);
            case wire_type::fixed64:
                return tree_write_fixed64(node, w);
            case wire_type::fixed32:
                return tree_write_fixed32(node, w);
            case wire_type::length:
                return tree_write_length(node, w, depth, max_depth);
        default:
            return error_code::proto_bad_field;
    }
}

    error_code tree_decode(detail::wire_reader& r, rtti::value_io* node,
                           std::size_t depth,
                           std::size_t max_depth) const override {
        switch (info.wire) {
            case wire_type::varint: {
                std::uint64_t v = 0;
                if (!r.read_varint(v)) {
                    return error_code::proto_truncated;
                }
                return tree_set_varint(node, v);
            }
            case wire_type::fixed64: {
                std::uint64_t v = 0;
                if (!r.read_fixed64(v)) {
                    return error_code::proto_truncated;
                }
                return tree_set_fixed64(node, v);
            }
            case wire_type::fixed32: {
                std::uint32_t v = 0;
                if (!r.read_fixed32(v)) {
                    return error_code::proto_truncated;
                }
                return tree_set_fixed32(node, v);
            }
            case wire_type::length: {
                std::uint64_t len = 0;
                if (!r.read_length(len)) {
                    return error_code::proto_truncated;
                }
                if (len > r.remaining()) {
                    return error_code::proto_truncated;
                }
                const rtti::string_view data = r.take(len);
                return tree_set_length(node, data, depth, max_depth);
            }
        default:
            return error_code::proto_bad_field;
    }
}

    error_code tree_decode_packed(rtti::value_io* obj, detail::wire_reader& r,
                                  std::size_t depth,
                                  std::size_t max_depth) const override {
        std::uint64_t len = 0;
        if (!r.read_length(len)) {
            return error_code::proto_truncated;
        }
        if (len > r.remaining()) {
            return error_code::proto_truncated;
        }
        const rtti::string_view payload = r.take(len);
        detail::wire_reader sub(payload);
        rtti::value_io* arr = detail::ensure_array(obj, info.name);
        while (sub.remaining() > 0) {
            rtti::value_io* el = obj->make_node();
            const error_code e = tree_decode(sub, el, depth, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            arr->push_back(el);
        }
        return error_code::ok;
    }

    error_code tree_decode_repeated(rtti::value_io* obj, detail::wire_reader& r,
                                    std::size_t depth,
                                    std::size_t max_depth) const override {
        rtti::value_io* arr = detail::ensure_array(obj, info.name);
        rtti::value_io* el = obj->make_node();
        const error_code e = tree_decode(r, el, depth, max_depth);
        if (e != error_code::ok) {
            return e;
        }
        arr->push_back(el);
        return error_code::ok;
    }

    error_code tree_write_varint(const rtti::value_io* node,
                                 detail::wire_writer& w) const {
        const rtti::value_io::kind k = node->type();
        if constexpr (std::is_enum<M>::value) {
            if (k == rtti::value_io::kind::string) {
                rtti::string_view name;
                if (!node->as_string(name)) {
                    return error_code::kind_mismatch;
                }
                const M ev = rtti::enum_from_string<M>(name);
                w.varint(static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(ev)));
                return error_code::ok;
            }
        }
        if (k == rtti::value_io::kind::boolean) {
            bool b = false;
            if (!node->as_bool(b)) {
                return error_code::kind_mismatch;
            }
            w.varint(b ? 1 : 0);
            return error_code::ok;
        }
        if (k == rtti::value_io::kind::integer) {
            std::int64_t i = 0;
            if (!node->as_integer(i)) {
                return error_code::kind_mismatch;
            }
            w.varint(info.zigzag ? zigzag_encode(i)
                                 : static_cast<std::uint64_t>(i));
            return error_code::ok;
        }
        return error_code::kind_mismatch;
    }

    error_code tree_write_fixed64(const rtti::value_io* node,
                                  detail::wire_writer& w) const {
        if constexpr (std::is_same<M, double>::value) {
            double d = 0.0;
            if (node->type() == rtti::value_io::kind::integer) {
                std::int64_t i = 0;
                if (!node->as_integer(i)) {
                    return error_code::kind_mismatch;
                }
                d = static_cast<double>(i);
            } else if (node->type() == rtti::value_io::kind::real) {
                if (!node->as_real(d)) {
                    return error_code::kind_mismatch;
                }
            } else {
                return error_code::kind_mismatch;
            }
            std::uint64_t bits = 0;
            std::memcpy(&bits, &d, sizeof(double));
            w.fixed64(bits);
            return error_code::ok;
        } else {
            std::int64_t i = 0;
            if (node->type() == rtti::value_io::kind::integer) {
                if (!node->as_integer(i)) {
                    return error_code::kind_mismatch;
                }
            } else if (node->type() == rtti::value_io::kind::real) {
                double d = 0.0;
                if (!node->as_real(d)) {
                    return error_code::kind_mismatch;
                }
                i = static_cast<std::int64_t>(d);
            } else {
                return error_code::kind_mismatch;
            }
            w.fixed64(static_cast<std::uint64_t>(i));
            return error_code::ok;
        }
    }

    error_code tree_write_fixed32(const rtti::value_io* node,
                                  detail::wire_writer& w) const {
        if constexpr (std::is_same<M, float>::value) {
            double d = 0.0;
            if (node->type() == rtti::value_io::kind::integer) {
                std::int64_t i = 0;
                if (!node->as_integer(i)) {
                    return error_code::kind_mismatch;
                }
                d = static_cast<double>(i);
            } else if (node->type() == rtti::value_io::kind::real) {
                if (!node->as_real(d)) {
                    return error_code::kind_mismatch;
                }
            } else {
                return error_code::kind_mismatch;
            }
            const float f = static_cast<float>(d);
            std::uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(float));
            w.fixed32(bits);
            return error_code::ok;
        } else {
            std::int64_t i = 0;
            if (node->type() == rtti::value_io::kind::integer) {
                if (!node->as_integer(i)) {
                    return error_code::kind_mismatch;
                }
            } else if (node->type() == rtti::value_io::kind::real) {
                double d = 0.0;
                if (!node->as_real(d)) {
                    return error_code::kind_mismatch;
                }
                i = static_cast<std::int64_t>(d);
            } else {
                return error_code::kind_mismatch;
            }
            w.fixed32(static_cast<std::uint32_t>(i));
            return error_code::ok;
        }
    }

    error_code tree_write_length(const rtti::value_io* node,
                                 detail::wire_writer& w, std::size_t depth,
                                 std::size_t max_depth) const {
        if constexpr (rtti::is_char_string<M>::value) {
            rtti::string_view s;
            if (node->type() != rtti::value_io::kind::string ||
                !node->as_string(s)) {
                return error_code::kind_mismatch;
            }
            w.varint(s.size());
            w.bytes(s.data(), s.size());
            return error_code::ok;
        } else if constexpr (rtti::is_reflected<M>::value) {
            if (node->type() != rtti::value_io::kind::object) {
                return error_code::kind_mismatch;
            }
            std::string payload;
            detail::wire_writer pw(payload);
            const error_code e = detail::encode_message_tree(
                node, pw, *schema_of<M>(), depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            w.varint(payload.size());
            w.bytes(payload.data(), payload.size());
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    error_code tree_set_varint(rtti::value_io* node, std::uint64_t v) const {
        if constexpr (std::is_enum<M>::value) {
            const M ev = static_cast<M>(static_cast<std::int32_t>(v));
            const char* name = rtti::enum_to_string<M>(ev);
            if (name != nullptr && name[0] != '\0') {
                node->make_scalar(
                    rtti::field_value::of(rtti::string_view(name)));
            } else {
                node->make_scalar(
                    rtti::field_value::of(static_cast<std::int64_t>(v)));
            }
            return error_code::ok;
        } else if constexpr (std::is_same<M, bool>::value) {
            node->make_scalar(rtti::field_value::of(v != 0));
            return error_code::ok;
        } else {
            node->make_scalar(rtti::field_value::of(
                info.zigzag ? zigzag_decode(v)
                            : static_cast<std::int64_t>(v)));
            return error_code::ok;
        }
    }

    error_code tree_set_fixed64(rtti::value_io* node, std::uint64_t v) const {
        if constexpr (std::is_same<M, double>::value) {
            double d = 0.0;
            std::memcpy(&d, &v, sizeof(double));
            node->make_scalar(rtti::field_value::of(d));
            return error_code::ok;
        } else {
            node->make_scalar(
                rtti::field_value::of(static_cast<std::int64_t>(v)));
            return error_code::ok;
        }
    }

    error_code tree_set_fixed32(rtti::value_io* node, std::uint32_t v) const {
        if constexpr (std::is_same<M, float>::value) {
            float f = 0.0f;
            std::memcpy(&f, &v, sizeof(float));
            node->make_scalar(rtti::field_value::of(static_cast<double>(f)));
            return error_code::ok;
        } else {
            node->make_scalar(
                rtti::field_value::of(static_cast<std::int64_t>(v)));
            return error_code::ok;
        }
    }

    error_code tree_set_length(rtti::value_io* node, rtti::string_view data,
                               std::size_t depth, std::size_t max_depth) const {
        if constexpr (rtti::is_char_string<M>::value) {
            node->make_scalar(rtti::field_value::of(data));
            return error_code::ok;
        } else if constexpr (rtti::is_reflected<M>::value) {
            detail::wire_reader sub(data);
            node->make_object();
            const error_code e = detail::decode_message_tree(
                node, sub, *schema_of<M>(), depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            if (sub.remaining() != 0) {
                return error_code::proto_truncated;
            }
            return error_code::ok;
        } else {
            node->make_scalar(rtti::field_value::of(data));
            return error_code::ok;
        }
    }

    bool tree_equals_default(const rtti::value_io* node) const override {
        if (node == nullptr) {
            return false;
        }
        const rtti::value_io::kind k = node->type();
        if constexpr (std::is_enum<M>::value) {
            if (k == rtti::value_io::kind::string) {
                rtti::string_view s;
                if (!node->as_string(s)) {
                    return false;
                }
                const char* name = rtti::enum_to_string<M>(
                    static_cast<M>(info.has_default ? info.dflt_int : 0));
                return name != nullptr && s == rtti::string_view(name);
            }
            return false;
        } else if constexpr (std::is_same<M, bool>::value) {
            if (k == rtti::value_io::kind::boolean) {
                bool b = false;
                if (!node->as_bool(b)) {
                    return false;
                }
                return (b ? 1 : 0) == (info.has_default ? info.dflt_int : 0);
            }
            return false;
        } else if constexpr (std::is_integral<M>::value) {
            if (k == rtti::value_io::kind::integer) {
                std::int64_t i = 0;
                if (!node->as_integer(i)) {
                    return false;
                }
                return i == (info.has_default ? info.dflt_int : 0);
            }
            return false;
        } else if constexpr (std::is_floating_point<M>::value) {
            if (k == rtti::value_io::kind::real) {
                double d = 0.0;
                if (!node->as_real(d)) {
                    return false;
                }
                return d == (info.has_default ? info.dflt_real : 0.0);
            }
            return false;
        } else if constexpr (rtti::is_char_string<M>::value) {
            if (k == rtti::value_io::kind::string) {
                rtti::string_view s;
                if (!node->as_string(s)) {
                    return false;
                }
                if (!info.has_default || info.dflt_str == nullptr) {
                    return s.empty();
                }
                return s == rtti::string_view(info.dflt_str, info.dflt_str_len);
            }
            return false;
        } else {
            return false;
        }
    }

    error_code tree_set_default(rtti::value_io* node) const override {
        if constexpr (rtti::is_optional<M>::value) {
            node->make_scalar(rtti::field_value{});
            return error_code::ok;
        } else if constexpr (rtti::is_char_string<M>::value) {
            node->make_scalar(rtti::field_value::of(rtti::string_view(
                info.dflt_str ? info.dflt_str : "", info.dflt_str_len)));
            return error_code::ok;
        } else if constexpr (std::is_same<M, bool>::value) {
            node->make_scalar(rtti::field_value::of(
                info.has_default ? (info.dflt_int != 0) : false));
            return error_code::ok;
        } else if constexpr (std::is_floating_point<M>::value) {
            node->make_scalar(rtti::field_value::of(
                info.has_default ? info.dflt_real : 0.0));
            return error_code::ok;
        } else if constexpr (std::is_integral<M>::value) {
            node->make_scalar(
                rtti::field_value::of(info.has_default ? info.dflt_int : 0));
            return error_code::ok;
        } else if constexpr (std::is_enum<M>::value) {
            const char* name = rtti::enum_to_string<M>(
                static_cast<M>(info.has_default ? info.dflt_int : 0));
            if (name != nullptr && name[0] != '\0') {
                node->make_scalar(
                    rtti::field_value::of(rtti::string_view(name)));
            } else {
                node->make_scalar(rtti::field_value::of(
                    info.has_default ? info.dflt_int : 0));
            }
            return error_code::ok;
        } else {
            // Message / repeated members: represent absence as null.
            node->make_scalar(rtti::field_value{});
            return error_code::ok;
        }
    }
};

// Registration helper: returns a pointer to a shared field<T,M> descriptor.
// Each distinct (T, member pointer) yields its own static field object, so two
// same-typed members never alias.
template <typename T, auto P>
inline field_base* make_field(const field_desc& d) {
    using M = std::remove_cv_t<
        std::remove_reference_t<decltype((std::declval<T>().*P))>>;
    static field<T, M> f{d, P};
    return &f;
}

// ---------------------------------------------------------------------------
// value_io document (owned tree)
// ---------------------------------------------------------------------------

class document;

namespace detail {

struct node : public rtti::value_io {
    rtti::value_io::kind k = rtti::value_io::kind::null;
    bool b = false;
    std::int64_t i = 0;
    double d = 0.0;
    std::string str;
    std::vector<node*> arr;
    std::vector<std::pair<std::string, node*>> obj;
    document* owner = nullptr;

    node() noexcept = default;

    rtti::value_io::kind type() const noexcept override { return k; }
    bool as_bool(bool& out) const noexcept override {
        if (k != rtti::value_io::kind::boolean) {
            return false;
        }
        out = b;
        return true;
    }
    bool as_integer(std::int64_t& out) const noexcept override {
        if (k != rtti::value_io::kind::integer) {
            return false;
        }
        out = i;
        return true;
    }
    bool as_real(double& out) const noexcept override {
        if (k == rtti::value_io::kind::integer) {
            out = static_cast<double>(i);
            return true;
        }
        if (k != rtti::value_io::kind::real) {
            return false;
        }
        out = d;
        return true;
    }
    bool as_string(rtti::string_view& out) const noexcept override {
        if (k != rtti::value_io::kind::string) {
            return false;
        }
        out = rtti::string_view(str.data(), str.size());
        return true;
    }
    std::size_t size() const noexcept override {
        return k == rtti::value_io::kind::array   ? arr.size()
               : k == rtti::value_io::kind::object ? obj.size()
                                                   : 0;
    }
    rtti::value_io* at(std::size_t index) override {
        return (k == rtti::value_io::kind::array && index < arr.size())
                   ? arr[index]
                   : nullptr;
    }
    rtti::value_io* at(std::size_t index) const override {
        return (k == rtti::value_io::kind::array && index < arr.size())
                   ? arr[index]
                   : nullptr;
    }
    bool get_key(rtti::string_view key, rtti::value_io*& out) override {
        return get_key_impl(key, out);
    }
    bool get_key(rtti::string_view key, rtti::value_io*& out) const override {
        return get_key_impl(key, out);
    }
    bool get_key_at(std::size_t index, rtti::string_view& key,
                    rtti::value_io*& out) const override {
        if (k != rtti::value_io::kind::object || index >= obj.size()) {
            return false;
        }
        key =
            rtti::string_view(obj[index].first.data(), obj[index].first.size());
        out = obj[index].second;
        return true;
    }

    rtti::value_io* make_array() override {
        k = rtti::value_io::kind::array;
        arr.clear();
        obj.clear();
        return this;
    }
    void push_back(rtti::value_io* v) override {
        if (k == rtti::value_io::kind::array) {
            arr.push_back(static_cast<node*>(v));
        }
    }
    rtti::value_io* make_object() override {
        k = rtti::value_io::kind::object;
        arr.clear();
        obj.clear();
        return this;
    }
    void set_key(rtti::string_view key, rtti::value_io* v) override {
        if (k != rtti::value_io::kind::object) {
            return;
        }
        obj.emplace_back(std::string(key.data(), key.size()),
                         static_cast<node*>(v));
    }
    rtti::value_io* make_scalar(rtti::field_value v) override {
        k = rtti::value_io::kind::null;
        b = false;
        i = 0;
        d = 0.0;
        str.clear();
        switch (v.type()) {
            case rtti::field_value::kind::boolean:
                k = rtti::value_io::kind::boolean;
                b = v.as_bool();
                break;
            case rtti::field_value::kind::integer:
                k = rtti::value_io::kind::integer;
                i = v.as_integer();
                break;
            case rtti::field_value::kind::real:
                k = rtti::value_io::kind::real;
                d = v.as_real();
                break;
            case rtti::field_value::kind::string: {
                const rtti::string_view sv = v.as_string();
                str.assign(sv.data(), sv.size());
                k = rtti::value_io::kind::string;
                break;
            }
            default:
                break; // none/object -> null
        }
        return this;
    }
    rtti::value_io* make_node() override;

private:
    bool get_key_impl(rtti::string_view key, rtti::value_io*& out) const {
        if (k != rtti::value_io::kind::object) {
            return false;
        }
        for (const auto& e : obj) {
            if (rtti::string_view(e.first.data(), e.first.size()) == key) {
                out = e.second;
                return true;
            }
        }
        return false;
    }
};

} // namespace detail

// Owned value_io tree produced by wire_to_tree / parse. The root is always an
// object; scalar payloads and keys are copied into the node, so the tree
// outlives the source wire buffer.
class document : public rtti::value_io {
public:
    document();
    document(document&&) noexcept = default;
    document& operator=(document&&) noexcept = default;
    document(const document&) = delete;
    document& operator=(const document&) = delete;
    ~document() override = default;

    rtti::value_io::kind type() const noexcept override {
        return root_->type();
    }
    bool as_bool(bool& out) const noexcept override {
        return root_->as_bool(out);
    }
    bool as_integer(std::int64_t& out) const noexcept override {
        return root_->as_integer(out);
    }
    bool as_real(double& out) const noexcept override {
        return root_->as_real(out);
    }
    bool as_string(rtti::string_view& out) const noexcept override {
        return root_->as_string(out);
    }
    std::size_t size() const noexcept override { return root_->size(); }
    rtti::value_io* at(std::size_t index) override {
        return root_->at(index);
    }
    rtti::value_io* at(std::size_t index) const override {
        return root_->at(index);
    }
    bool get_key(rtti::string_view key, rtti::value_io*& out) override {
        return root_->get_key(key, out);
    }
    bool get_key(rtti::string_view key, rtti::value_io*& out) const override {
        return root_->get_key(key, out);
    }
    bool get_key_at(std::size_t index, rtti::string_view& key,
                    rtti::value_io*& out) const override {
        return root_->get_key_at(index, key, out);
    }
    rtti::value_io* make_array() override { return root_->make_array(); }
    void push_back(rtti::value_io* v) override { root_->push_back(v); }
    rtti::value_io* make_object() override { return root_->make_object(); }
    void set_key(rtti::string_view key, rtti::value_io* v) override {
        root_->set_key(key, v);
    }
    rtti::value_io* make_scalar(rtti::field_value v) override {
        return root_->make_scalar(v);
    }
    rtti::value_io* make_node() override { return root_->make_node(); }

    friend struct detail::node;

private:
    detail::node* alloc_node() {
        pool_.push_back(std::unique_ptr<detail::node>(new detail::node()));
        detail::node* n = pool_.back().get();
        n->owner = this;
        return n;
    }

    std::vector<std::unique_ptr<detail::node>> pool_;
    detail::node* root_ = nullptr;
};

inline document::document() {
    root_ = alloc_node();
    root_->make_object();
}

namespace detail {

inline rtti::value_io* node::make_node() {
    if (owner == nullptr) {
        return nullptr;
    }
    return owner->alloc_node();
}

// Encode a value_io message node (object) into `w`, applying proto2/proto3
// default-omission and repeated packing rules.
inline error_code encode_message_tree(const rtti::value_io* obj,
                                      detail::wire_writer& w,
                                      const schema_info& si, std::size_t depth,
                                      std::size_t max_depth) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    if (obj->type() != rtti::value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    for (std::size_t ix = 0; ix < obj->size(); ++ix) {
        rtti::string_view key;
        rtti::value_io* child = nullptr;
        if (!obj->get_key_at(ix, key, child)) {
            return error_code::kind_mismatch;
        }
        if (child == nullptr || child->type() == rtti::value_io::kind::null) {
            continue;
        }
        const field_base* f = find_field_by_name(si, key);
        if (f == nullptr) {
            continue; // unknown key: ignored
        }
        if (f->info.mode == field_mode::repeated) {
            if (child->type() != rtti::value_io::kind::array) {
                return error_code::kind_mismatch;
            }
            const std::size_t n = child->size();
            if (n == 0) {
                continue;
            }
            if (f->info.packed && is_numeric_wire(f->info.wire)) {
                std::string payload;
                detail::wire_writer pw(payload);
                for (std::size_t j = 0; j < n; ++j) {
                    const error_code e =
                        f->tree_encode(child->at(j), pw, depth, max_depth);
                    if (e != error_code::ok) {
                        return e;
                    }
                }
                if (!w.tag(f->info.number, wire_type::length)) {
                    return error_code::proto_bad_field;
                }
                w.varint(payload.size());
                w.bytes(payload.data(), payload.size());
            } else {
                for (std::size_t j = 0; j < n; ++j) {
                    if (!w.tag(f->info.number, f->info.wire)) {
                        return error_code::proto_bad_field;
                    }
                    const error_code e =
                        f->tree_encode(child->at(j), w, depth, max_depth);
                    if (e != error_code::ok) {
                        return e;
                    }
                }
            }
        } else {
            if (f->info.mode != field_mode::required &&
                f->tree_equals_default(child)) {
                continue;
            }
            if (!w.tag(f->info.number, f->info.wire)) {
                return error_code::proto_bad_field;
            }
            const error_code e = f->tree_encode(child, w, depth, max_depth);
            if (e != error_code::ok) {
                return e;
            }
        }
    }
    return error_code::ok;
}

// Decode a wire message into a value_io object node, filling scalar defaults
// and empty arrays for absent fields.
inline error_code decode_message_tree(rtti::value_io* obj, detail::wire_reader& r,
                                      const schema_info& si, std::size_t depth,
                                      std::size_t max_depth) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    std::vector<std::uint32_t> seen;
    seen.reserve(si.count);
    while (r.remaining() > 0) {
        std::uint64_t tagv = 0;
        if (!r.read_varint(tagv)) {
            return error_code::proto_truncated;
        }
        const std::uint32_t field_no = static_cast<std::uint32_t>(tagv >> 3);
        const wire_type wt = static_cast<wire_type>(tagv & 7u);
        if (field_no == 0u || !valid_wire_type(wt)) {
            return error_code::proto_bad_field;
        }
        const field_base* f = find_field(si, field_no);
        if (f == nullptr) {
            const error_code e = skip_field(r, wt);
            if (e != error_code::ok) {
                return e;
            }
            continue;
        }
        error_code e = error_code::ok;
        if (f->info.mode == field_mode::repeated) {
            if (wt == wire_type::length && is_numeric_wire(f->info.wire)) {
                e = f->tree_decode_packed(obj, r, depth, max_depth);
            } else if (wt == f->info.wire) {
                e = f->tree_decode_repeated(obj, r, depth, max_depth);
            } else {
                return error_code::proto_type_mismatch;
            }
        } else {
            if (wt != f->info.wire) {
                return error_code::proto_type_mismatch;
            }
            rtti::value_io* child = obj->make_node();
            e = f->tree_decode(r, child, depth, max_depth);
            if (e == error_code::ok) {
                obj->set_key(f->info.name, child);
            }
        }
        if (e != error_code::ok) {
            return e;
        }
        seen.push_back(field_no);
    }
    for (std::size_t i = 0; i < si.count; ++i) {
        const field_base* f = si.fields[i];
        if (f == nullptr) {
            continue;
        }
        if (std::find(seen.begin(), seen.end(), f->info.number) != seen.end()) {
            continue;
        }
        if (f->info.mode == field_mode::repeated) {
            rtti::value_io* arr = obj->make_node();
            arr->make_array();
            obj->set_key(f->info.name, arr);
        } else if (f->info.mode == field_mode::required) {
            return error_code::missing_key;
        } else {
            rtti::value_io* child = obj->make_node();
            const error_code e = f->tree_set_default(child);
            if (e != error_code::ok) {
                return e;
            }
            obj->set_key(f->info.name, child);
        }
    }
    return error_code::ok;
}

// Schema-less wire -> tree: object keys are decimal field numbers.
inline error_code decode_generic(rtti::value_io* obj, detail::wire_reader& r,
                                 std::size_t depth, std::size_t max_depth) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    while (r.remaining() > 0) {
        std::uint64_t tagv = 0;
        if (!r.read_varint(tagv)) {
            return error_code::proto_truncated;
        }
        const std::uint32_t field_no = static_cast<std::uint32_t>(tagv >> 3);
        const wire_type wt = static_cast<wire_type>(tagv & 7u);
        if (field_no == 0u || !valid_wire_type(wt)) {
            return error_code::proto_bad_field;
        }
        const std::string key = std::to_string(field_no);
        rtti::value_io* child = obj->make_node();
        switch (wt) {
            case wire_type::varint: {
                std::uint64_t v = 0;
                if (!r.read_varint(v)) {
                    return error_code::proto_truncated;
                }
                child->make_scalar(
                    rtti::field_value::of(static_cast<std::int64_t>(v)));
                break;
            }
            case wire_type::fixed64: {
                std::uint64_t v = 0;
                if (!r.read_fixed64(v)) {
                    return error_code::proto_truncated;
                }
                child->make_scalar(
                    rtti::field_value::of(static_cast<std::int64_t>(v)));
                break;
            }
            case wire_type::fixed32: {
                std::uint32_t v = 0;
                if (!r.read_fixed32(v)) {
                    return error_code::proto_truncated;
                }
                child->make_scalar(
                    rtti::field_value::of(static_cast<std::int64_t>(v)));
                break;
            }
            case wire_type::length: {
                std::uint64_t len = 0;
                if (!r.read_length(len)) {
                    return error_code::proto_truncated;
                }
                const rtti::string_view data = r.take(len);
                child->make_scalar(rtti::field_value::of(data));
                break;
            }
        }
        obj->set_key(rtti::string_view(key.data(), key.size()), child);
    }
    return error_code::ok;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// DoS guard: maximum nested message depth before error_code::too_deep.
struct parse_config {
    std::size_t max_depth = 512;
};

inline constexpr std::size_t kDefaultMaxDepth = 512;

// Serialize `obj` into `out` (replaces the buffer) per the schema<T> field
// table. Returns an error_code (e.g. too_deep on runaway nesting); on error
// `out` holds an unspecified partial buffer.
template <typename T>
error_code serialize(const T& obj, std::string& out) {
    out.clear();
    detail::wire_writer w(out);
    return detail::encode_message(obj, w, *schema_of<T>(), 0, kDefaultMaxDepth);
}

// Convenience overload returning the wire bytes (partial/empty on error).
template <typename T>
std::string serialize(const T& obj) {
    std::string out;
    (void)serialize(obj, out);
    return out;
}

// Parse a wire message into a T{}-initialized object.
template <typename T>
result<T> parse_as(rtti::string_view src) {
    return parse_as<T>(src, parse_config{});
}

template <typename T>
result<T> parse_as(rtti::string_view src, const parse_config& cfg) {
    result<T> r;
    T out{};
    detail::wire_reader reader(src);
    const error_code e =
        detail::decode_message(out, reader, *schema_of<T>(), 0, cfg.max_depth);
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = reader.offset();
        return r;
    }
    if (reader.remaining() != 0) {
        r.err.code = error_code::proto_truncated;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = reader.offset();
        return r;
    }
    r.value = std::move(out);
    return r;
}

// Decode a wire message into a value_io tree named by the schema fields.
inline error_code wire_to_tree(rtti::string_view src, const schema_info* si,
                               document& doc) {
    if (si == nullptr) {
        return error_code::kind_mismatch;
    }
    detail::wire_reader reader(src);
    return detail::decode_message_tree(&doc, reader, *si, 0, kDefaultMaxDepth);
}

template <typename T>
error_code wire_to_tree(rtti::string_view src, document& doc) {
    return wire_to_tree(src, schema_of<T>(), doc);
}

// Encode a value_io object tree (any adapter's document) into wire bytes.
inline error_code tree_to_wire(const rtti::value_io* root,
                               const schema_info* si, std::string& out) {
    out.clear();
    if (si == nullptr) {
        return error_code::kind_mismatch;
    }
    detail::wire_writer w(out);
    return detail::encode_message_tree(root, w, *si, 0, kDefaultMaxDepth);
}

template <typename T>
error_code tree_to_wire(const rtti::value_io* root, std::string& out) {
    return tree_to_wire(root, schema_of<T>(), out);
}

// Schema-less parse: keys are decimal field numbers, length fields become
// string nodes. Useful for generic wire inspection / JSON interop.
inline result<document> parse(rtti::string_view src) {
    result<document> r;
    document doc;
    detail::wire_reader reader(src);
    const error_code e =
        detail::decode_generic(&doc, reader, 0, kDefaultMaxDepth);
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = reader.offset();
        return r;
    }
    r.value = std::move(doc);
    return r;
}

// Schema-aware parse: keys are field names, nested messages recurse, defaults
// are filled for absent scalar fields.
inline result<document> parse(rtti::string_view src, const schema_info* si) {
    result<document> r;
    document doc;
    const error_code e = wire_to_tree(src, si, doc);
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = 0;
        return r;
    }
    r.value = std::move(doc);
    return r;
}

// SAX streaming events for a flat wire walk. A callback returning false
// cancels the parse early; parse_stream then returns ok (like the other
// format adapters). malformed wire yields the usual error_info.
struct parse_events {
    std::function<bool()> message_start;
    std::function<bool()> message_end;
    std::function<bool(std::uint32_t, wire_type)> field;
    std::function<bool(std::uint64_t)> varint_value;
    std::function<bool(std::uint64_t)> fixed64_value;
    std::function<bool(std::uint32_t)> fixed32_value;
    std::function<bool(rtti::string_view)> bytes_value;
};

namespace detail {

template <typename Fn, typename... Args>
inline bool fire_event(const Fn& fn, bool& cancelled, Args&&... args) {
    if (!fn) {
        return true;
    }
    if (fn(std::forward<Args>(args)...)) {
        return true;
    }
    cancelled = true;
    return false;
}

} // namespace detail

inline result<void> parse_stream(rtti::string_view src,
                                 const parse_events& events) {
    result<void> r;
    detail::wire_reader reader(src);
    bool cancelled = false;
    if (!detail::fire_event(events.message_start, cancelled)) {
        return r;
    }
    while (reader.remaining() > 0) {
        std::uint64_t tagv = 0;
        if (!reader.read_varint(tagv)) {
            r.err.code = error_code::proto_truncated;
            r.err.src = src.data();
            r.err.src_len = src.size();
            r.err.offset = reader.offset();
            return r;
        }
        const std::uint32_t field_no = static_cast<std::uint32_t>(tagv >> 3);
        const wire_type wt = static_cast<wire_type>(tagv & 7u);
        if (field_no == 0u || !detail::valid_wire_type(wt)) {
            r.err.code = error_code::proto_bad_field;
            r.err.src = src.data();
            r.err.src_len = src.size();
            r.err.offset = reader.offset();
            return r;
        }
        if (!detail::fire_event(events.field, cancelled, field_no, wt)) {
            return r;
        }
        error_code e = error_code::ok;
        switch (wt) {
            case wire_type::varint: {
                std::uint64_t v = 0;
                if (!reader.read_varint(v)) {
                    e = error_code::proto_truncated;
                    break;
                }
                (void)detail::fire_event(events.varint_value, cancelled, v);
                break;
            }
            case wire_type::fixed64: {
                std::uint64_t v = 0;
                if (!reader.read_fixed64(v)) {
                    e = error_code::proto_truncated;
                    break;
                }
                (void)detail::fire_event(events.fixed64_value, cancelled, v);
                break;
            }
            case wire_type::fixed32: {
                std::uint32_t v = 0;
                if (!reader.read_fixed32(v)) {
                    e = error_code::proto_truncated;
                    break;
                }
                (void)detail::fire_event(events.fixed32_value, cancelled, v);
                break;
            }
            case wire_type::length: {
                std::uint64_t len = 0;
                if (!reader.read_length(len)) {
                    e = error_code::proto_truncated;
                    break;
                }
                const rtti::string_view data = reader.take(len);
                (void)detail::fire_event(events.bytes_value, cancelled, data);
                break;
            }
        }
        if (cancelled) {
            return r;
        }
        if (e != error_code::ok) {
            r.err.code = e;
            r.err.src = src.data();
            r.err.src_len = src.size();
            r.err.offset = reader.offset();
            return r;
        }
    }
    if (cancelled) {
        return r;
    }
    if (!detail::fire_event(events.message_end, cancelled)) {
        return r;
    }
    return r;
}

} // namespace proto
} // namespace rtti