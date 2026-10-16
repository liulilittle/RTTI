# Protocol Adapters — Interface Reference (protobuf / FlatBuffers)

> Module: `rtti::proto` (protobuf), `rtti::flat` / `rtti::flat_schema` (FlatBuffers)
> Files: `include/rtti/proto.hpp`, `proto_schema.hpp`, `flat.hpp`, `flat_schema.hpp`
> Edition: English. Chinese version: [`../../zh/proto-flat.md`](../zh/proto-flat.md).
> APIs in this chapter are extracted from the headers named above; line ranges
> reflect the current tree.

## 0. Overview

The protobuf adapter registers a **schema** for a reflected C++ struct and
provides typed (object) and tree (`value_io`) codecs, plus a SAX streaming
parse. The FlatBuffers adapter builds a flat schema from a `.fbs`-style source
and (de)serializes both typed objects and `value_io` trees with official
FlatBuffers wire layout.

**Gates**: `RTTI_ENABLE_PROTO` (`proto.hpp`, `proto_schema.hpp`),
`RTTI_ENABLE_FLAT` (CMake option; flat.hpp/flat_schema.hpp). `.proto` file
parsing is **out of scope** — messages are registered in C++ (schema
registration preferred per spec §13.3).

---

## 1. protobuf (`rtti::proto`)

`include/rtti/proto.hpp` (2910 lines). Gated by `RTTI_ENABLE_PROTO`.

### 1.1 Wire primitives (proto.hpp:85-155)

```cpp
enum class syntax : std::uint8_t { proto2, proto3 };
enum class field_mode : std::uint8_t { optional, required, repeated };
enum class wire_type : std::uint8_t { varint = 0, fixed64 = 1, length = 2, fixed32 = 5 };

std::uint32_t make_tag(std::uint32_t number, wire_type wt) noexcept;  // (number<<3)|wt
bool encode_varint(std::uint64_t value, std::string& out);
bool decode_varint(rtti::string_view src, std::uint64_t& value, std::size_t& used);
std::uint64_t zigzag_encode(std::int64_t v) noexcept;   // canonical (v<<1)^(v>>63)
std::int64_t  zigzag_decode(std::uint64_t u) noexcept;
```

Varint decoding is bounds-checked; on the 10th byte only bit 63 is meaningful,
bits 64..69 must be 0, and a continuation beyond 10 bytes fails (`proto_truncated`).
Zig-zag runs in unsigned space so it is well-defined for every `int64_t` input.

### 1.2 `field_desc` & descriptor builders (proto.hpp:295-359)

```cpp
struct field_desc {
    const char* name; std::uint32_t number; wire_type wire;
    field_mode mode; bool packed; bool zigzag;
    bool has_default; std::int64_t dflt_int; double dflt_real;
    const char* dflt_str; std::size_t dflt_str_len;
    std::uint32_t oneof_group;   // non-zero = member of this oneof group
    bool emit_present;           // explicit-presence (map key/value, proto2 optional)
};

field_desc make_field_desc(const char* name, std::uint32_t number, wire_type wt,
                           field_mode mode = field_mode::optional, bool packed = false);
field_desc with_default_int(field_desc d, std::int64_t v);
field_desc with_zigzag(field_desc d);               // sint32/sint64
field_desc in_oneof(field_desc d, std::uint32_t g); // single-active-field group
field_desc with_default_real(field_desc d, double v);
field_desc with_default_str(field_desc d, const char* s, std::size_t n);
```

### 1.3 `schema<T>` / `schema_info` / `schema_of` (proto.hpp:431-450)

```cpp
template <typename T>
struct schema {
    static constexpr std::size_t count = 0;           // specialize for messages
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[1] = {nullptr};
};
struct schema_info { syntax syn; std::size_t count; const field_base* const* fields; };
template <typename T> inline const schema_info* schema_of();
```

The primary `schema<T>` is an empty proto3 message: unregistered types serialize
to zero bytes and parse as their default-constructed object. Register a message
by specializing `schema<T>` with an array of `field_base*` built via
`make_field`/`make_field_desc` (field members decode into a `std::optional` /
a `std::vector<T>` for repeated, etc.).

`field_base` (proto.hpp:374-425) is the type-erased per-field codec with typed
path (`encode_obj`/`decode_obj`/`decode_packed`/`decode_repeated`/
`should_encode`/`set_default`) and tree path (`tree_encode`/`tree_decode`/
`tree_decode_packed`/`tree_decode_repeated`/`tree_equals_default`/
`tree_set_default`).

### 1.4 Typed codecs (proto.hpp:516-621)

```cpp
template <typename T> error_code encode_message(const T& obj, detail::wire_writer& w,
                                                const schema_info& si, std::size_t depth,
                                                std::size_t max_depth);
template <typename T> error_code decode_message(T& obj, detail::wire_reader& r,
                                                const schema_info& si, std::size_t depth,
                                                std::size_t max_depth);
```

`detail::wire_reader` / `detail::wire_writer` (proto.hpp:161-276) offer bounded
reads (`remaining()`, `offset()`, `read_varint`, etc.) and writes
(`write_varint`, `write_tag`, `write_bytes`, `write_fixed*`, …). Every read is
end-checked, so malformed input never reads out of range.

### 1.5 Tree bridge (`value_io`) (proto.hpp:637-655, 2110-2660)

```cpp
error_code encode_message_tree(const rtti::value_io* obj, detail::wire_writer& w,
                               const schema_info& si, std::size_t depth,
                               std::size_t max_depth);
error_code decode_message_tree(rtti::value_io* obj, detail::wire_reader& r,
                               const schema_info& si, std::size_t depth,
                               std::size_t max_depth);
error_code decode_generic(rtti::value_io* obj, detail::wire_reader& r,
                          std::size_t depth, std::size_t max_depth);
// extension-path helpers:
error_code ext_read_value(...);   error_code ext_write_value(...);
error_code decode_extension_field(rtti::value_io* obj, ...);
error_code encode_extension_field(const rtti::value_io* node, ...);
```

### 1.6 Extensions (proto.hpp:1716-1832)

```cpp
struct extension_desc;                      // field number in a foreign message
class extension_registry;                   // decode/encode lookups
inline error_code register_extension(const extension_desc& e);
inline void clear_extensions() noexcept;
```

Loading balance: extensions registered this way participate in
`tree`-decode/encode of the target message.

### 1.7 `document` & `node` (proto.hpp:1842-2108)

`detail::node` and `class document` are `value_io` trees used by the wire↔tree
bridge and the DOM parse result.

### 1.8 Public entry points (proto.hpp:2669-2802)

```cpp
struct parse_config { /* depth budget, e.g. max_depth */ };

template <typename T> error_code serialize(const T& obj, std::string& out);
template <typename T> std::string serialize(const T& obj);
template <typename T> result<T> parse_as(rtti::string_view src);
template <typename T> result<T> parse_as(rtti::string_view src, const parse_config& cfg);

inline error_code wire_to_tree(rtti::string_view src, const schema_info* si,
                               rtti::value_io* out);   // into a tree
template <typename T> error_code wire_to_tree(rtti::string_view src, document& doc);
template <typename T> error_code tree_to_wire(const rtti::value_io* root, std::string& out);
inline result<document> parse(rtti::string_view src);
inline result<document> parse(rtti::string_view src, const schema_info* si);

struct parse_events { /* SAX: message_start/message_end/field_*/ };  // proto.hpp:2794
```

Wire types 0 (varint), 1 (fixed64), 2 (length), 5 (fixed32) are supported;
packed numeric fields and oneof groups follow protobuf semantics.

---

## 2. `.fbs` schema reader (`rtti::flat_schema`)

`include/rtti/flat_schema.hpp`. Gated by `RTTI_ENABLE_FLAT`.

### 2.1 Schema descriptors (flat_schema.hpp:89-182)

```cpp
enum class fbs_type : std::uint8_t { /* scalar/struct/table/vector/union/string/… */ };

struct fbs_field_desc;      struct fbs_enum_value_desc;
struct fbs_enum_desc;       struct fbs_union_variant_desc;
struct fbs_union_desc;      struct fbs_agg_desc;
struct fbs_schema_desc;

result<fbs_schema_desc> parse_schema(rtti::string_view src);
```

### 2.2 Compile & query (flat_schema.hpp:188-224)

```cpp
struct compile_config;                    // e.g. root_type, identifier
class compiled_schema;                    // resolved schema + offset tables
result<compiled_schema> compile_schema(const fbs_schema_desc& desc,
                                       const compile_config& cfg);
result<compiled_schema> compile_schema(rtti::string_view src,
                                       const compile_config& cfg);
```

### 2.3 `schema<T>` in `flat.hpp` (flat.hpp:103-366)

```cpp
enum class fkind : std::uint8_t { /* scalar kinds */ };
struct union_variant_entry { /* enum -> field_desc */ };
struct field_desc { /* name, slot (vtable index), type, default */ };

field_desc make_field_desc(const char* name, std::uint16_t slot, …);
field_desc make_union_value(const char* name, std::uint16_t slot, …);
field_desc with_default_int(field_desc d, std::int64_t v);
field_desc with_default_real(field_desc d, double v);

template <typename T> struct schema;
template <typename T> struct schema_identifier_of;   // T::identifier
template <typename T> inline const schema_info* schema_of();
template <typename E> inline union_variant_entry variant_of(E e);

class document : public rtti::value_io;               // flat tree (flat.hpp:242)
struct parse_config;                                   // root type / identifier

result<document> parse(rtti::string_view src, const schema_info* si, …);
inline error_code wire_to_tree(rtti::string_view src, document& doc, …);
inline error_code tree_to_wire(const rtti::value_io* root, std::string& out, …);
template <typename T> inline error_code serialize(const T& obj, std::string& out);
template <typename T> inline std::string serialize(const T& obj);
template <typename T> inline result<T> parse_as(rtti::string_view src);
template <typename T> inline result<T> parse_as(rtti::string_view src, const parse_config& cfg);
```

The generated layout follows official FlatBuffers wire rules (vtables,
SOFFSET/offset fields, 8-byte alignment for 64-bit scalars, table slot-0
presence), so buffers round-trip with the reference `flatc` (`--json`).

---

## 3. Error codes used here

- proto: `proto_truncated`, `proto_bad_field`, `proto_type_mismatch`, plus the
  shared `too_deep` / `no_such_field` / `kind_mismatch`.
- flat: the shared `error_code` set (invalid_root / too_deep / no_such_field /
  kind_mismatch).

See [error.hpp](../../../include/rtti/error.hpp) for the full enum.

## Notes / limitations (documented)

- `.proto` file parser is out of scope; `schema<T>` registration is the
  supported route.
- Zig-zag and varint decode are exactly bounded (no out-of-range reads, no
  silent 64-bit overflow).
- For cross-namespace / dotted references and reassignment rules, the official
  protobuf / FlatBuffers references win; deviations (redefine-vs-error
  conventions) are documented in the header comments.

## Licence notice

Both editions describe public interfaces only. See
[LICENSE](../../../LICENSE) for the project licence.