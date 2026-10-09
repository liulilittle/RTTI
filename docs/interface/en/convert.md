# Value Conversion Core — API Reference

**Module:** RTTI value conversion core (`rtti` namespace)

**Scope:** this document covers the format-agnostic conversion surface of the RTTI library: `include/rtti/convert.hpp` (the primary file, 2,189 lines) plus the four supporting headers `error.hpp`, `number_format.hpp`, `node_io.hpp` and `memory.hpp`. Signatures are transcribed verbatim from the headers; behaviour notes are quoted or inferred strictly from header comments and the shared error contract.

**Files covered**

| Header | Role |
| --- | --- |
| `include/rtti/convert.hpp` | Format-agnostic `value_io` ↔ C++ value conversion: `converter<T>`, `to_value`/`from_value` (+ `_named`), date-time and arbitrary-base bridges |
| `include/rtti/error.hpp` | Shared error infrastructure: `error_code`, `error_info`, `result<T>` |
| `include/rtti/number_format.hpp` | Number/reflected/node text formatting on top of `to_string` |
| `include/rtti/node_io.hpp` | Node-level bridge: `locate` / `node_as` / `to_node` (spec 4.3) |
| `include/rtti/memory.hpp` | `string_view` alias and `memory_arena` |

**Namespace:** everything public lives in `namespace rtti`. Converters that are only shape helpers live in `namespace rtti::detail` (scalar lanes, base conversion, naming helpers, shared real formatting). `naming_policy` is `rtti::detail::naming_policy`.

**Format gating:** `convert.hpp` is format-agnostic. It includes only `base_convert.hpp`, `date_time.hpp`, `error.hpp`, `naming.hpp` and `reflection.hpp`, and never a format adapter (JSON/XML/INI/TOML/proto/flat). It is therefore always compilable regardless of which `RTTI_ENABLE_*` format gates are on.

**Interacting headers (consumed, not documented in full):** `reflection.hpp` defines `value_io`, `field_value`, the container-shape traits, `field_access_base`, the `fields<T>`/`field_count<T>`/`has_default<T>` registry accessors and `enum_table<E>`/`enum_to_string`. `naming.hpp` defines `detail::naming_policy` / `apply_naming` / `key_case`. `date_time.hpp` defines `date_time`, `is_date_time` and `epoch_ns_from_digits`. `base_convert.hpp` defines the `detail::to_base`/`from_base` machinery that backs `to_value_base`/`from_value_base`. Only the pieces referenced by the conversion signatures are summarized here.

---

## 1. Error codes (`include/rtti/error.hpp`, lines 1–102)

`error_code` is the single error channel shared by the reflect core and every format adapter. Converters return it directly; parse APIs return it wrapped in `result<T>`.

### 1.1 `enum class error_code : uint8_t`

Every enumerator is an automatically numbered value (implicit values 0..23 in declaration order).

| Enumerator | Value | Meaning |
| --- | --- | --- |
| `ok` | 0 | Success. |
| `unexpected_token` | 1 | Token not expected at this parse position (also emitted by the format-spec parser for illegal specs, see chapter 8). |
| `unterminated_string` | 2 | A quoted string runs to the end of the buffer. |
| `invalid_number` | 3 | Ill-formed numeric literal (also: unknown epoch digit-count lane, see `epoch_ns_from_digits`; invalid base / illegal digit for the base bridges). |
| `invalid_utf8` | 4 | The bytes are not valid UTF-8. |
| `unterminated_comment` | 5 | A comment opener has no closer. |
| `mismatched_tag` | 6 | XML open/close tag names do not match. |
| `duplicate_attribute` | 7 | The same XML attribute appears twice. |
| `bad_entity` | 8 | Unknown or malformed XML entity. |
| `unterminated_cdata` | 9 | A CDATA section is not closed. |
| `dtd_missing` | 10 | A DTD is required but absent. |
| `too_deep` | 11 | Nesting exceeded the depth budget — also returned by the converter depth guard when a write/read chain recurses past 512 levels (see §4.10). |
| `invalid_root` | 12 | The document root has the wrong kind. |
| `ini_unterminated_section` | 13 | INI `[section]` without a closing `]`. |
| `ini_bad_line` | 14 | Unparseable INI line. |
| `ini_missing_value` | 15 | INI key with no value. |
| `no_such_field` | 16 | Missing key / missing member — also returned by the converters when the source or destination node pointer is `nullptr`, and by `locate` for missing keys / out-of-range indexes. |
| `kind_mismatch` | 17 | The source node's kind does not fit the target type (or a path step crosses a non-object/non-array). The converters' most common error. |
| `missing_key` | 18 | A reflected-struct field is absent from the source object and the type has no default-keep behaviour. |
| `proto_truncated` | 19 | protobuf wire data ends mid-field. |
| `proto_bad_field` | 20 | Malformed protobuf field coding. |
| `proto_type_mismatch` | 21 | Decoded protobuf kind does not match the schema. |
| `overflow` | 22 | Numeric value out of the target's representable range (converters never wrap silently); also real→integral of NaN/infinity. |
| `no_such_method` | 23 | Method reflection: no matching method. |

### 1.2 `const char* to_string(error_code code) noexcept`

Human-readable English message for a code. **Never returns `nullptr`** (an unknown code still yields a non-null string).

### 1.3 `struct error_info`

```cpp
struct error_info {
    error_code code = error_code::ok;
    const char* src = nullptr;   // entire source buffer (may be null)
    size_t src_len = 0;
    size_t offset = 0;           // byte offset of the error

    size_t line() const noexcept;    // 1-based; computed lazily from src/offset
    size_t column() const noexcept;  // 1-based; computed lazily
    const char* message() const noexcept;  // equals to_string(code)
};
```

`line()`/`column()` are computed lazily from the source buffer, so callers pay only when they actually need them. `operator==` / `operator!=` compare `code` and `offset` only.

### 1.4 `template <typename T> struct result<T>` and `result<void>`

```cpp
template <typename T>
struct result {
    T value{};              // T must be default-constructible
    error_info err;
    bool ok() const noexcept;              // err.code == error_code::ok
    explicit operator bool() const noexcept;
    T& operator*() noexcept;
    const T& operator*() const noexcept;
};

template <> struct result<void> {          // no value payload
    error_info err;
    bool ok() const noexcept;
    explicit operator bool() const noexcept;
};
```

Helpers: `result<T> make_ok(T v)` and `result<T> make_error(error_code code)`.

**In the converter API:** the `node_io` bridge returns `result<value_io*>` from `locate` and `result<T>` from `node_as`. The core `from_value`/`to_value` functions return a bare `error_code` instead.

*Coverage: `error.hpp` lines 1–102.*

## 2. Types used by the conversion signatures (from `reflection.hpp` / `naming.hpp` / `date_time.hpp`)

### 2.1 `class value_io` (abstract source/sink, `reflection.hpp` lines 370–405)

The conversion functions accept and produce `value_io` nodes. Each format adapter (JSON/XML/INI/TOML/proto/flat) implements this interface over its own DOM; this header is therefore format-independent. Kind mirrors JSON-ish value categories.

```cpp
class value_io {
public:
    enum class kind { null, boolean, integer, real, string, array, object };

    virtual ~value_io() = default;

    virtual kind type() const noexcept = 0;

    // Read side (source). Each returns false on kind mismatch.
    virtual bool as_bool(bool& out) const noexcept = 0;
    virtual bool as_integer(int64_t& out) const noexcept = 0;
    virtual bool as_real(double& out) const noexcept = 0;
    virtual bool as_string(string_view& out) const noexcept = 0;

    // Container read side.
    virtual std::size_t size() const noexcept = 0;
    virtual value_io* at(std::size_t index) = 0;                 // array element
    virtual value_io* at(std::size_t index) const = 0;
    virtual bool get_key(string_view key, value_io*& out) = 0;   // object lookup
    virtual bool get_key(string_view key, value_io*& out) const = 0;
    virtual bool get_key_at(std::size_t index, string_view& key,
                            value_io*& out) const = 0;

    // Write side (sink). make_* return the node itself for chaining.
    virtual value_io* make_array() = 0;
    virtual void push_back(value_io* v) = 0;
    virtual value_io* make_object() = 0;
    virtual void set_key(string_view key, value_io* v) = 0;
    virtual value_io* make_scalar(field_value v) = 0;
    virtual value_io* make_node() = 0;   // independent child owned by the tree
};
```

Converter semantics rely on the read-side lane order per scalar kind — see §5.

### 2.2 `class field_value` (variant-like scalar payload, `reflection.hpp` lines 270–324)

`make_scalar(field_value)` is how converters hand a scalar to a sink. Kind: `none | boolean | integer | real | string | object`. Factories: `of(bool)`, `of(int64_t)`, `of(double)`, `of(string_view)`, `of_object()`; the default constructor produces `kind::none` (this is what an empty `std::optional` writes, and adapters serialize `none` as null). Accessors `as_bool()/as_integer()/as_real()/as_string()` return a fallback (false / 0 / 0.0 / empty view) on kind mismatch; `type()` reports the stored kind. `field_value_to(M&, const field_value&)` is the scalar typed-dispatch helper.

### 2.3 Container-shape traits (`reflection.hpp` lines 90–244)

These gate the `if constexpr` dispatch in `from_value`/`to_value`.

| Trait | True when |
| --- | --- |
| `is_sequence<T>` | `value_type` + `push_back` + `begin`/`end` and **not** a `std::basic_string` (vector/list/deque). |
| `is_associative<T>` | `key_type` + `value_type` + `insert`. |
| `is_map<T>` | associative **plus** `mapped_type`. |
| `is_set<T>` | `is_associative && !is_map`. |
| `is_queue<T>` | `value_type` + `push`/`pop`/`front`/`back`. |
| `is_stack<T>` | `value_type` + `push`/`pop`/`top`. |
| `is_priority_queue<T>` | `value_type` + `value_compare` + `push`/`pop`/`top`. |
| `is_optional<T>` | T is exactly `std::optional<U>`. |
| `is_variant<T>` | T exposes `std::variant_size`. |
| `is_tuple<T>` | `std::tuple_size` present and no `data()` (excludes `std::array`). |
| `is_pair<T>` | `first`/`second` members. |
| `is_fixed_sequence<T>` | `value_type` + `begin`/`end`, no `push_back`, not a string, not associative (`std::array`). |
| `is_c_array<T>` | native array `T[N][M]...`, **excluding** `char[N]`. |
| `is_md_array<T>` | native array whose element type is also a native array. |
| `is_reflected<T>` | `fields_of<T>::count` exists (registered via `RTTI_DEFINE_TYPE*`). |
| `is_char_string<T>` | any `std::basic_string specialization`. |
| `has_push_back<T>` | `value_type` + `push_back`. |

Related accessors: `field_count<T>()`, `fields<T>()` (array of `field_access_base const*`), `for_each_field`, `has_default<T>` (true when registered with a `_WITH_DEFAULT` macro — missing source keys then keep the member's current value), `key_traits<K>::assign/ view` (string-convertible map keys). `field_access_base` (lines 412+) offers `get_value`/`set_value`, named-variant `get_value_named`/`set_value_named`, `is_null_value`, `is_container`, `size`, and container mutation hooks.

Registration macros (all in `reflection.hpp`): `RTTI_DEFINE_TYPE(T, ...)` (field list), `RTTI_DEFINE_TYPE_DERIVED(T, Base, ...)`, `RTTI_DEFINE_TYPE_WITH_DEFAULT(T, ...)`, `RTTI_DEFINE_TYPE_INTRUSIVE`, `RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT`, `RTTI_DEFINE_ENUM(T, ...)`.

### 2.4 `detail::naming_policy` (`naming.hpp` lines 41–45)

```cpp
struct naming_policy {
    key_case kc = key_case::identity;     // identity | snake | camel
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
};
```

`key_case` (line 26): `identity`, `snake`, `camel`. `apply_naming(field, np)` computes the output key: a `rename_map` hit wins over `key_case`. `skip_empty` drops fields whose value is empty during serialization (see §7.2).

### 2.5 `is_date_time<T>` and epoch-unit mapping (`date_time.hpp`)

`is_date_time<T>` is true exactly for `rtti::date_time` (line 185–186). `epoch_ns_from_digits(value, out)` (lines 192–245) infers the unit of a numeric epoch from the **absolute decimal digit count** of the value: 10 = seconds, 13 = ms, 16 = us, 19 or 20 = ns; zero maps to epoch zero; any other count is `invalid_number`; a scaled product outside the int64 range is `overflow`.

`date_time` read/write hooks used by the converter: `date_time(int64_t ns)`, static `result<date_time> parse(string_view iso)`, `to_unix_seconds/ms/us/ns(int64_t&)`, `epoch_ns()`, `format()`.

### 2.6 Base conversion cores (`base_convert.hpp`, `namespace rtti::detail`)

`to_base(value, base, digits=nullptr)` → string (empty on invalid base / undersized table); `to_base_signed(value, base)`; `from_base(s, base, out [, digits] [, case_insensitive])` and `from_base_signed(s, base, out)` → `error_code` (`invalid_number` for bad input / illegal digits, `overflow` when the magnitude exceeds the target lane). `default_digits()`, `rfc4648_digits()`, `digit_table_len`, `default_case_insensitive`, `base_digit`.

Default digit table (spec §15.2): base 2..36 = `0-9a-z`; base 37..62 = `0-9a-zA-Z`; base 63..64 adds `+` at index 62 and `/` at index 63. Default table parses base ≤ 36 case-insensitively; bases > 36 are case-sensitive.

*Coverage: `reflection.hpp` lines 90–324, 370–430, 816–857, 1279–1300; `naming.hpp` lines 1–53; `date_time.hpp` lines 34–245; `base_convert.hpp` lines 1–112.*

## 3. The `converter<T>` surface (`convert.hpp` lines 299–650)

### 3.1 Primary template `template <typename T, typename = void> struct converter`

```cpp
template <typename T, typename = void>
struct converter {
    static error_code from(const value_io* src, T& out);
    static error_code to(value_io* dst, const T& in);
};
```

The primary template implements the **scalar** lanes (bool / integral / floating-point):

- `from`: `bool` → reads the bool lane, else the integer lane (`0 → false`, any nonzero → `true`), else `kind_mismatch`; integral (non-bool) → `detail::scalar_integral_from`; floating-point → `detail::scalar_real_from`; any other type reaching this leaf → `kind_mismatch`.
- `to`: for `unsigned T` with `sizeof(T) >= sizeof(uint64_t)`, a value above `INT64_MAX` is reported as **`overflow`** instead of a silent wrapping `static_cast` (spec §0 forbids silent loss); otherwise `dst->make_scalar(scalar_of(in))`. `scalar_of` maps: bool → `field_value::of(bool)`; plain `char` → its ASCII byte value as `int64_t` (0..255); other integers → `field_value::of(int64_t)`; floating-point → `field_value::of(double)`; anything else → `field_value::of_object()`.

### 3.2 Scalar conversion semantics (`detail`, lines 51–297)

These are the range-checked lanes that back the primary template and the string converter.

| Lane | Signature | Rules |
| --- | --- | --- |
| `is_plain_char<T>` | trait, line 58 | true exactly for `char` (spec §23.1 treats `char` as an ASCII byte 0..255, distinct from `signed char`/`unsigned char`). |
| `scalar_int64_to_integral` | `error_code (int64_t v, T& out)` | unsigned lane/ranges: negative or above max → `overflow`; correct int64 bounds for signed. Never modifies `out` on failure. |
| `scalar_uint64_to_integral` | `error_code (uint64_t v, T& out)` | exact uint64 → integral, so `UINT64_MAX` round-trips without double precision loss. |
| `scalar_real_to_integral` | `error_code (double v, T& out)` | `isfinite` required (NaN/inf → `overflow`), truncate-toward-zero, then range check (documented 2^64 / 2^63 boundary handling). Never UB. |
| `scalar_text_to_integral` | `error_code (string_view s, T& out)` | whole text must be a number. Empty / non-numeric → `kind_mismatch`; out-of-range → `overflow`. Integer text read via uint64 then int64 lanes exactly; fractional/exponent text via the truncated real path. |
| `scalar_text_to_real` | `error_code (string_view s, T& out)` | deterministic tokens `inf`/`infinity`/`-inf`/`-infinity`/`nan` round-trip; other non-numeric text → `kind_mismatch`; out-of-range magnitude → `overflow`. |
| `scalar_format_real` | `void (double, std::string& out)` | shortest round-tripping text via `std::to_chars`; non-finite → `nan`/`-inf`/`inf`; appends `.0` when the token has no `.`/`e`/`E`. |
| `scalar_integral_from` | `error_code (const value_io* src, T& out)` | tries lanes in order integer → real → string → bool (bool → 0/1). Range violations never wrap. |
| `scalar_real_from` | `error_code (const value_io* src, T& out)` | real lane directly; integer lane (IEEE precision loss accepted); text lane parsed. |

### 3.3 `converter<std::basic_string<Char, Traits, Alloc>>` (lines 363–396)

A string is a scalar (text) type, not a sequence.

- `from`: string lane → assign; integer lane → decimal text (`std::to_string`); real lane → shortest round-tripping text (`detail::scalar_format_real`); otherwise `kind_mismatch`.
- `to`: `dst->make_scalar(field_value::of(string_view(in.data(), in.size())))` → `ok`.

### 3.4 Registered enums (lines 401–431)

`is_registered_enum<T>` (lines 401–405) is true when `enum_table<T>::count` exists (registered with `RTTI_DEFINE_ENUM`). The partial specialization `converter<T, enable_if_t<is_enum && is_registered_enum>>` converts **through the label text**:

- `from`: requires the string lane; linear search of `enum_table<T>::array` for a `name` equal to the text; match → `out = array[i].value`; no match → `kind_mismatch`.
- `to`: `enum_to_string(in)`; if the result is null or empty (`name[0] == '\0'`) → `kind_mismatch`; else write the label as a string scalar.

Specializing here (not in any format adapter header) means every format shares one definition.

### 3.5 `date_time` (lines 433–483)

`converter<T, enable_if_t<is_date_time<T>::value>>` turns `date_time` into a scalar timestamp:

- `from`: **null `src` → `no_such_field`**. Integer lane → `epoch_ns_from_digits(num, ns)` then `out = T(ns)` (unit auto-detected by digit count); string lane → `T::parse(text)` (its `error_code` propagates, e.g. `invalid_number`); anything else → `kind_mismatch`.
- `to`: `in.to_unix_seconds(sec)` (error propagates, e.g. `overflow`); `make_scalar(field_value::of(sec))` — **numeric epoch seconds** (spec §14.2 default). Non-default output goes through `to_value_dt` (§3.6).

Note: the `is_date_time` check runs **before** `is_reflected` in the entry-point dispatch, so a user-registered `date_time` still converts as a scalar timestamp, not a field-walked object.

### 3.6 `enum class date_time_output` and `to_value_dt` (lines 439–536)

```cpp
enum class date_time_output {
    numeric_seconds,
    numeric_ms,
    numeric_us,
    numeric_ns,
    iso_string
};

inline error_code to_value_dt(value_io* dst, const date_time& in,
                              date_time_output mode);
```

The documented opt-in surface for a non-default date-time write representation. `mode` selects the output: `numeric_seconds` (default) / `numeric_ms` / `numeric_us` / `numeric_ns` (each via the matching `to_unix_*`, propagating its `error_code`) or `iso_string` (`in.format()`, preserving sub-second precision exactly). Requires `dst != nullptr`, else `no_such_field`. On success writes a scalar node and returns `ok`.

**Read-back note:** there is no `from_value_dt` in the public surface. The read side is served by the `date_time` converter specialization (§3.5), which auto-detects the numeric epoch unit and also accepts ISO text.

### 3.7 Reflected-member write hookup `member_to_value` (lines 543–561)

```cpp
template <typename M>
inline error_code member_to_value(value_io* dst, const M& v);
template <typename M>
inline error_code member_to_value_named(value_io* dst, const M& v,
                                        const detail::naming_policy& np);
```

Used by the reflected-struct accessors to serialize a single member. A `date_time` member is routed through `to_value_dt(dst, v, date_time_output::iso_string)` so reflected writes keep sub-second precision; every other member type routes through the plain converter chain. The `_named` variant ignores `np` for date-time and uses `to_value_named` otherwise (a non-date_time member still applies the naming policy recursively).

### 3.8 Arbitrary-base integer bridges (lines 572–620)

```cpp
inline error_code to_value_base(value_io* dst, std::uint64_t value,
                                unsigned base, const char* digits = nullptr);
inline error_code from_value_base(const value_io* src, unsigned base,
                                  std::uint64_t& out);
inline error_code to_value_base_signed(value_io* dst, std::int64_t value,
                                       unsigned base);
inline error_code from_value_base_signed(const value_io* src, unsigned base,
                                         std::int64_t& out);
```

Serialize/deserialize an integer as a base-`N` string (`base` 2..64) through explicit entry points — mirroring the date-time design where the plain `to_value`/`from_value` keeps emitting numeric scalars (the four-format assertions depend on that) and text is opted into through these functions.

- `to_value_base`: null `dst` → `no_such_field`; `detail::to_base(value, base, digits)`; an empty result (invalid base / undersized custom table) → `invalid_number`; else writes the string scalar and returns `ok`.
- `from_value_base`: null `src` → `no_such_field`; a non-string source → `kind_mismatch`; then `detail::from_base(s, base, out)` — `invalid_number` for empty/illegal input, `overflow` for magnitude overflow. Note the 3-argument `from_base` uses the default table with strtol-style case policy.
- Signed pair: same shape; `detail::to_base_signed`/`from_base_signed` accept an optional `+`/`-` prefix and bound magnitudes to the int64 lane (`INT64_MIN` representable exactly). This is the date-time-epoch hookup (spec §15.4).

Example (from `tests/base_convert_test.cpp:321-351`): `to_value_base(node, 255, 16)` writes text `"ff"`; `from_value_base(node, 16, back)` restores 255. A custom RFC 4648-style table can be passed for bases up to 64: `to_value_base(node, 63, 64, kBaseConvertUrlSafe)` writes `"_"`.

### 3.9 `converter<char[N], void>` (lines 626–650)

A fixed-size C `char` array is a **string target**, not a numeric sequence (`is_c_array` excludes `char[N]`, spec §12.2):

- `from`: requires the string lane; if `v.size() >= N` → `kind_mismatch` (must leave room for the NUL); zero-fills the buffer then copies. Never overruns.
- `to`: serializes up to the first NUL as a string scalar.

*Coverage: `convert.hpp` lines 51–650.*

## 4. Container, composite and reflected converters (`convert.hpp` lines 652–1269)

All converter functions below return `error_code`. `from_*` reads a source node into an existing target object; `to_*` writes a value into a sink node. Write-side converters build the tree via `make_array()/make_object()/make_node()/push_back()/set_key()`; a failure **mid-write leaves the tree partially populated** (children created before the failing element stay attached), and the error code of the first failing element is propagated.

### 4.1 `std::vector<bool>` handling (lines 658–661)

`is_vector_bool<std::vector<bool, Alloc>>` detects the bit-packed specialization whose iterators yield proxy objects on some standard libraries. The sequence writer copies each proxy into a real `bool` before `to_value`, keeping the path portable across libstdc++/libc++/MSVC.

### 4.2 Sequence family — `vector` / `list` / `deque` (lines 664–709)

```cpp
template <typename Seq> error_code sequence_from(const value_io* src, Seq& out);
template <typename Seq> error_code sequence_to(value_io* dst, const Seq& in);
```

- `sequence_from`: source must be `kind::array`, else `kind_mismatch`; `out.clear()`; one element per child via `at(i)` (`nullptr` child → `kind_mismatch`), recursing through `from_value`.
- `sequence_to`: `make_array()`; each element through `to_value` on a fresh `make_node()`, then `push_back`. Uses the proxy-copy loop for `vector<bool>`.

### 4.3 Map family (lines 712–749)

```cpp
template <typename Map> error_code map_from(const value_io* src, Map& out);
template <typename Map> error_code map_to(value_io* dst, const Map& in);
```

- `map_from`: source must be `kind::object`; `out.clear()`; enumerates key/value pairs via `get_key_at(i, key, child)` (false or null child → `kind_mismatch`); value recurses through `from_value`; the key is converted with `key_traits<key_type>::assign` and the pair `emplace`d.
- `map_to`: `make_object()`; each mapped value through `to_value`; the key is written with `key_traits<key_type>::view`.

### 4.4 Set family (lines 752–786)

```cpp
template <typename Set> error_code set_from(const value_io* src, Set& out);
template <typename Set> error_code set_to(value_io* dst, const Set& in);
```

`set_from`: source must be `kind::array`; `out.clear()`; each element `insert`ed after `from_value`. `set_to`: `make_array()`; each element `to_value` + `push_back`.

### 4.5 Container adaptors — `queue` / `stack` / `priority_queue` (lines 794–914)

These expose no iterators, so reading copies and drains `front`/`top` one at a time.

- `queue_from` (FIFO): source `kind::array`; swap in an empty queue; push children in order (array order = front..back). `queue_to`: drains a copy, pushing `front` elements (array order = front..back).
- `stack_from` (LIFO): source `kind::array`; children consumed **from index n-1 down to 0** (array order = top..bottom; rebuild in reverse so the top stays on top). `stack_to`: drains a copy via `top`, so the array is top..bottom.
- `priority_queue_from`: source `kind::array`; children consumed from index n-1 down to 0, pushing each — the heap re-heapifies to the same top. `priority_queue_to`: drains a copy via `top`, so the array is in **pop order** (largest first for the default `less` comparator).

All three reject non-array sources with `kind_mismatch` and treat a null child the same way.

### 4.6 `std::optional` (lines 923–948)

```cpp
template <typename T> error_code optional_from(const value_io* src, std::optional<T>& out);
template <typename T> error_code optional_to(value_io* dst, const std::optional<T>& in);
```

- `optional_from`: null `src` → `no_such_field`; a `null` source → `out.reset()` and `ok`; otherwise `from_value` into a temporary and engage it.
- `optional_to`: disengaged → `make_scalar(field_value{})` (kind `none`, serialized as null) and `ok`; engaged → `to_value(dst, *in)`.

### 4.7 `std::variant` (lines 955–1019)

Wire shape: an **object** `{"index":N,"value":V}` where `N` is the active alternative's index as an integer scalar.

```cpp
template <typename V> error_code variant_to(value_io* dst, const V& in);
template <typename V> error_code variant_from(const value_io* src, V& out);
```

- `variant_to`: `valueless_by_exception()` → `kind_mismatch`; `make_object()`; writes `"index"` = `int64(in.index())`; writes `"value"` through `to_value` (a child failure is propagated instead of leaving a silent empty `value` node).
- `variant_from`: source must be an object; `"index"` must be present and convertible via the integer lane, in `[0, variant_size)`; `"value"` must be present; dispatch to that alternative via `variant_from_at` / `variant_from_indexed` (a pack expansion over the alternatives — every alternative read is a generic `from_value<Alt>` call, so instantiating the function instantiates all branches safely).

### 4.8 `std::tuple` / `std::pair` (lines 1023–1072)

`std::pair` is a 2-element tuple (`std::get`/`std::tuple_size` work), so it is fully covered by the tuple path.

```cpp
template <typename Tuple> error_code tuple_to(value_io* dst, const Tuple& in);
template <typename Tuple> error_code tuple_from(const value_io* src, Tuple& out);
```

- `tuple_to`: `make_array()`; expands `index_sequence`, one child per element via `to_value(std::get<I>)`, propagating the **first** child failure while still converting the rest.
- `tuple_from`: source must be `kind::array`; expands per element, reading `at(I)` (null child → `kind_mismatch`), each through `from_value(std::get<I>)`. The source may be longer than the tuple (extra elements ignored); a shorter source reports `kind_mismatch` at the first missing child.

### 4.9 Fixed-size sequence — `std::array` (lines 1075–1111)

```cpp
template <typename A> error_code fixed_sequence_to(value_io* dst, const A& in);
template <typename A> error_code fixed_sequence_from(const value_io* src, A& out);
```

- `fixed_sequence_to`: `make_array()`; one child per element.
- `fixed_sequence_from`: source must be `kind::array`; **strict** extent match — `src->size()` must equal `out.size()`, else `kind_mismatch` (never a silent truncation or a leftover stale tail, mirroring the `carray_from` contract). Then `from_value` per slot.

### 4.10 C-style arrays `T[N]` / `T[N][M]` / `T[A][B][C]` (lines 1121–1224)

Traits: `carray_total<T>` (product of extents; rank 0 ⇒ 1), `carray_rank<T>()`, `carray_extent<T, I>()`.

```cpp
template <typename T, std::size_t N>
error_code carray_to(value_io* dst, const T (&in)[N]);
template <typename T, std::size_t N>
error_code carray_from(const value_io* src, T (&out)[N]);
```

- `carray_to`: nested arrays, one level per dimension.
- `carray_from`: source must be `kind::array`; let `total = carray_total<T[N]>`. For a multidimensional target the **nested form** is attempted first (one child array per outer dimension — this also disambiguates the M==1 case where flat and nested counts coincide); if the nested walk fails and `sz == total`, the **flat row-major** form (`carray_from_flat`) is accepted; otherwise the flat form is tried when `sz == total`; any other count → `kind_mismatch`. Writes never go out of bounds.
- `carray_from_flat(src, base, out)`: treats the source as one flat element sequence spanning the full extent product, filling linearly with row-major offsets; null child → `kind_mismatch`.

### 4.11 Reflected-struct walk (lines 1231–1269)

```cpp
template <typename T> error_code reflect_from(const value_io* src, T& out);
template <typename T> error_code reflect_to(value_io* dst, const T& in);
```

A type registered with `RTTI_DEFINE_TYPE*` is read/written field by field through `fields<T>()` / `field_count<T>()`:

- `reflect_from`: source must be `kind::object`; for each field accessor `fa` in table order, look up `fa->name`. A missing key with `has_default<T>::value` **keeps the member's current value** (the `_WITH_DEFAULT` registration); a missing key otherwise returns `missing_key`. Present keys recurse via `fa->set_value(&out, sub)` — nested/container/scalar members all go through the accessor, and its `error_code` propagates.
- `reflect_to`: `make_object()`; each field through `fa->get_value(&in, child)`, stored under `fa->name`.

### 4.12 Depth guard (lines 1278–1298)

Every `from_value`/`to_value` entry carries a thread-local depth guard:

```cpp
inline constexpr std::size_t kConversionMaxDepth = 512;
inline int& conversion_depth() noexcept;              // thread_local counter
struct conversion_depth_guard { /* RAII: ++/-- */ };
```

A read/write chain recursing past 512 levels (e.g. a self-referential data structure on the write side) reports `error_code::too_deep` instead of overflowing the stack. Format parsers cap tree depth at 512 as well.

*Coverage: `convert.hpp` lines 652–1298.*

## 5. Entry points `from_value` / `to_value` (`convert.hpp` lines 1300–1429)

### 5.1 ADL hook detection (lines 1308–1322)

```cpp
template <typename T, typename = void> struct has_adl_from;  // true when the type's
template <typename T, typename = void> struct has_adl_to;    // associated namespace
                                                              // provides the 3-arg hook
```

A custom type integrates by writing, **in the type's own namespace**:

```cpp
void from_value(const value_io*, T&, error_code&);
void to_value(value_io*, const T&, error_code&);
```

The rtti two-argument templates never match the 3-argument detection, so ordinary lookup cannot pick them; only a user hook survives overload resolution.

### 5.2 `from_value`

```cpp
template <typename T>
error_code from_value(const value_io* src, T& out);
```

Reads `src` into the existing object `out`. Error codes: `nullptr src` → `no_such_field`; past the depth budget → `too_deep`; then dispatch in **this order**:

```
optional -> variant -> tuple (pair is a 2-tuple) -> C array -> fixed_sequence
-> map -> set -> priority_queue (before stack) -> queue -> stack -> sequence
-> date_time (before reflected) -> reflected -> ADL hook -> converter<T>
```

Constraints that make the order safe: `is_fixed_sequence` never matches map/set (associative types are excluded in `reflection.hpp`), and native C arrays have no `value_type`, so they reach neither `is_sequence` nor `is_fixed_sequence`. A `date_time` (even if registered with `RTTI_DEFINE_TYPE`) converts as a scalar, not a field-walked object. The final leaf `converter<T>` is the scalar template from §3.1.

### 5.3 `to_value`

```cpp
template <typename T>
error_code to_value(value_io* dst, const T& in);
```

Writes `in` into the sink `dst`. Errors: `nullptr dst` → `no_such_field`; `too_deep` past the depth budget; dispatch order mirrors `from_value` exactly.

### 5.4 Usage notes

- The result payload for `node_io`'s `node_as<T>` must be default-constructible; the `result<T>` value is value-initialized.
- `from_value`/`to_value` never throw for data-shape problems; failures come back as `error_code`. Allocation can still throw `std::bad_alloc` from container/string growth, which is outside the error-channel contract.
- The converters are re-entrant and thread-local in depth bookkeeping; one arena/document per thread is the intended usage (see chapter 10).

## 6. Named conversion (`*_named`, lines 1431–2143)

`to_value_named` / `from_value_named` mirror the plain dispatch chain and apply the **naming policy** at the reflected-field boundary: keys are computed via `detail::apply_naming(fa->name, np)` and, on the serialize side, `np.skip_empty` drops empty fields.

```cpp
template <typename T>
error_code to_value_named(value_io* dst, const T& in,
                          const detail::naming_policy& np);
template <typename T>
error_code from_value_named(const value_io* src, T& out,
                            const detail::naming_policy& np);
```

Dispatch order is identical to the plain entry points. Inside a reflected object, container-element recursion goes through the named variants, so a **nested reflected struct inside any container is renamed too**; associative-container keys are user data and stay untouched.

### 6.1 Empty-field predicate (lines 1455–1475)

`detail::field_is_empty(fa, obj)` is true when a reflected field's value is "empty" under `skip_empty`: a null-equivalent value (`fa->is_null_value(obj)` — empty optional / null pointer), a container with `size()==0`, an empty string, a numeric zero, or a `false` bool. Containers are probed through the type-erased accessor; scalars go through `get() -> field_value` so the check is format-independent.

### 6.2 Per-family named variants

Every family from chapter 4 has a named twin that recurses through `to_value_named`/`from_value_named`:

- `reflect_to_named` / `reflect_from_named` (1479–1524) — the only ones that apply `apply_naming` to the key; `skip_empty` only affects serialization; missing-key behaviour on read matches `reflect_from` (`missing_key`, or keep-on-default when `has_default`).
- `optional_to_named` / `optional_from_named` (1526–1553).
- `variant_to_named` / `variant_from_named` (+ `variant_from_at_named`, `variant_from_indexed_named`, 1555–1623).
- `tuple_to_named` / `tuple_from_named` (+ `tuple_to_seq_named`, `tuple_from_seq_named`, 1625–1682).
- `fixed_sequence_to_named` / `fixed_sequence_from_named` (1684–1717) — **note:** the named `from` is not strict; it fills `min(src->size(), out.size())` elements and returns `ok` on a shorter source (partial fill without an error), unlike the plain strict `fixed_sequence_from`.
- `carray_to_named` / `carray_from_named` (+ `carray_from_flat_named`, 1721–1799) — same nested-then-flat read strategy.
- `sequence_to_named` / `sequence_from_named` (1801–1848), `map_to_named` / `map_from_named` (1850–1889), `set_to_named` / `set_from_named` (1891–1927).
- `queue_to_named` / `queue_from_named` (1929–1968), `stack_to_named` / `stack_from_named` (1970–2009), `priority_queue_to_named` / `priority_queue_from_named` (2011–2050).

ADL hooks reached through the named entry points keep their own key naming (`from_value(src, out, e)` / `to_value(dst, in, e)` are called with the user's hook, which owns the policy).

## 7. Shared real-text formatting (lines 2145–2187)

`namespace detail { void format_real(double value, std::string& out); }` — the single place that turns a `double` into its serialized text for every format adapter. Uses `std::to_chars` (shortest decimal that round-trips, so `3.14` stays `"3.14"`), appends `.0` when the shortest token has no `.`/`e`/`E`, and maps non-finite values to the deterministic `nan` / `-inf` / `inf` tokens (matching the older `%.17g` output, never an empty string). JSON maps non-finite to null before calling; XML/INI keep the `inf`/`nan` text. `detail::scalar_format_real` (lines 223–249) mirrors this behaviour for the scalar string converter.

## 8. Number / node / reflected formatting (`include/rtti/number_format.hpp`, lines 1–162)

### 8.1 Format-spec grammar (spec §7.2)

```
format_spec := [[fill]align] [sign] ['#'] ['0'] [width] [','] ['.' precision] [type]
```

- `fill`: any single byte followed by an `align` byte.
- `align`: `<` left, `>` right (default), `^` center, `=` pad after the sign.
- `sign`: `+` always print the sign; `-` negatives only (default); ` ` a space instead of a sign for non-negatives.
- `#`: alternate form — `0b`/`0o`/`0x`/`0X` prefix for non-decimal integer bases (no effect for decimal or reals).
- `0`: zero-pad to `width` (equivalent to fill `'0'` with align `=`).
- `width`: minimum total width (decimal digits).
- `,`: thousands grouping; applies to the decimal integer part and to fixed-notation reals (ignored for other bases / scientific).
- `.precision`: for integers = minimum digit count (zero padded); for reals = digits after the point for `f`/`e` (default 6).
- `type`: integer `d` (default) `b` `o` `x` `X`; real `f`/`F` (fixed), `e`/`E` (scientific), `g`/`G` or empty (shortest round-tripping text).

An illegal spec reports `error_code::unexpected_token` through the optional `error_info*` out-parameter and the formatting function returns an **empty string** (spec §0 fail-safe, never a crash).

### 8.2 Non-template cores (`namespace rtti::detail`, defined in `src/number_format.cpp`)

```cpp
std::string format_number_unsigned(std::uint64_t value, string_view spec, error_info* err);
std::string format_number_signed(std::int64_t value, string_view spec, error_info* err);
std::string format_number_real(double value, string_view spec, error_info* err);
struct number_parts;   // defined in src/number_format.cpp (opaque to header users)
number_parts parse_number_spec(string_view spec, bool is_floating, error_info* err);
```

`format_number_signed` places a `-` before the digits (or before the zero-padding when zero-padding / align `=`).

### 8.3 Public number formatting

```cpp
template <typename T,
          std::enable_if_t<std::is_integral<T>::value &&
                               !std::is_same<T, bool>::value, int> = 0>
std::string format_number(T value, string_view spec = string_view(),
                          error_info* err = nullptr);
template <typename T,
          std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
std::string format_number(T value, string_view spec = string_view(),
                          error_info* err = nullptr);
```

Thin template shells dispatching to the cores by signedness / floating-ness. Examples from `tests/format_test.cpp`: `format_number(1234567, "#,d") == "1,234,567"`, `format_number(42, "08d") == "00000042"`, `format_number(255, "#x") == "0xff"`, `format_number(3.14159, ".2f") == "3.14"`, `format_number(1234.5678, "^10.2f")` centers within width 10.

### 8.4 `to_string(v, format_spec)` overload

```cpp
template <typename T, std::enable_if_t<
    (std::is_integral<T>::value && !std::is_same<T, bool>::value) ||
     std::is_floating_point<T>::value || is_date_time<T>::value, int> = 0>
std::string to_string(const T& v, string_view format_spec);
```

A thin overload that applies a format spec on top of the existing scalar conversion surface. The spec is a **required** argument (no default), so the existing `to_string<T>(v)` from `to_string.hpp` is never shadowed or made ambiguous. Integers/floats go through `format_number`; `date_time` goes through its strftime-style `format(pattern)` (`<rtti/date_time.hpp>`).

### 8.5 Node-level stringification

```cpp
std::string node_to_string(const value_io* node, string_view spec = string_view(),
                           error_info* err = nullptr);
```

Defined in `src/format_reflected.cpp`. Serializes any `value_io` node to one compact line (spec §7.3): objects `{"k":v,...}`, arrays `[a,b,...]`, booleans/strings/null in their compact forms, numbers through the format spec (empty spec = shortest texts). A null node argument reports `no_such_field`. Allocation failure may throw `std::bad_alloc`; no other failure throws (empty string on error).

### 8.6 Reflection-driven formatting

```cpp
template <typename T>
std::string format_reflected(const T& obj, string_view spec = string_view(),
                             error_info* err = nullptr);
```

Walks the reflected field table of T (`RTTI_DEFINE_TYPE*`) and renders one compact object line; the optional spec is applied to every numeric field value. A zero-field table renders `"{}"`. Defined in `src/format_reflected.cpp` (the generic implementation is the non-template `detail::reflected_fields_to_string`). Examples from `tests/format_test.cpp:329-359`: `format_reflected(pt) == "{\"x\":1,\"y\":2}"`, `format_reflected(st, ".2f") == "{\"id\":7,\"score\":3.50}"`, `format_reflected(big, "#,d") == "{\"x\":1,234,567,\"y\":2}"`.

*Coverage: `number_format.hpp` lines 1–162.*

## 9. Node-level conversion bridge (`include/rtti/node_io.hpp`, lines 1–65)

Every format document derives from `value_io`, so these work directly on json/xml/ini/toml/proto documents.

### 9.1 `locate`

```cpp
result<const value_io*> locate(const value_io& root, string_view path) noexcept;
result<value_io*> locate(value_io& root, string_view path) noexcept;
```

Const/mutable tree navigation along a dot-separated key plus `[index]` path. Path grammar: `step ('.' step)*`, where a step is a key optionally followed by one or more `[index]` groups (`"a.b[2].c"`) or one or more leading root indexes (`"[0][1].x"`). An empty path selects nothing and returns an error. Error mapping: missing key / out-of-range index → `no_such_field`; navigating a key into a non-object, an index into a non-array, or any malformed path shape (empty segment, stray dot, an index step after `.`, malformed brackets) → `kind_mismatch`. The returned pointer borrows the tree and stays valid while the tree lives. `locate` is `noexcept`; its tokenizer allocates a small scratch vector, so an OOM (`std::bad_alloc`) terminates instead of throwing. Non-template, implemented in `src/reflect.cpp`.

Example (from `tests/node_locate_test.cpp:66-91`):

```cpp
auto d = rtti::json::parse("{\"user\":{\"name\":\"ada\",\"age\":42,"
    "\"addresses\":[{\"city\":\"Milan\",\"zip\":20121},{\"city\":\"Rome\",\"zip\":100}]}}");
const auto city = rtti::locate(d.value, "user.addresses[1].city");
// city.ok() true; node_as<std::string>(*city).value == "Rome"
```

### 9.2 `node_as<T>`

```cpp
template <typename T>
result<T> node_as(const value_io* node);
```

Converts a located node into a modern C++ value through the existing `to_value` dispatch (scalars, containers, reflected structs). A null `node` reports `no_such_field`; a kind mismatch reports the converter's code. `T` must be default-constructible (the result payload is value-initialized). Implementation: `r.err.code = from_value(node, r.value);` (called with `*city`, where the located pointer has been dereferenced).

### 9.3 `to_node`

```cpp
template <typename T>
error_code to_node(value_io& target, const T& v);
```

Replaces the subtree under `target` with `v` through `from_value`; the adapters' `make_*` write side resets the target's previous children, so a located scalar/object/array node is fully replaced and the owning document serializes the new value. Returns the converter's error code. Implementation: `return to_value(&target, v);`.

## 10. Memory infrastructure (`include/rtti/memory.hpp`, lines 1–85)

### 10.1 `string_view` alias

```cpp
using string_view = std::string_view;
```

Zero-copy view alias. All string data is owned by the parse buffer or the arena; views never own memory.

### 10.2 `class memory_arena` (high-throughput bump/block arena)

```cpp
class memory_arena {
public:
    memory_arena() noexcept = default;
    ~memory_arena();
    memory_arena(memory_arena&& other) noexcept;
    memory_arena& operator=(memory_arena&& other) noexcept;
    memory_arena(const memory_arena&) = delete;              // non-copyable
    memory_arena& operator=(const memory_arena&) = delete;

    void* alloc(size_t size, size_t align = alignof(std::max_align_t));
    template <typename T, typename... Args> T* alloc_obj(Args&&... args);
    const char* dup(const char* data, size_t n);             // NUL-terminated copy
    const char* dup(const char* s);                          // strlen copy, NUL-terminated
    void reset() noexcept;                                   // free all blocks; reusable
    size_t total_bytes() const noexcept;                     // reserved across blocks
};
```

Semantics:

- Memory is carved from linked blocks (default **64 KiB** each). Small allocations bump a cursor inside the current block; oversized requests get their own block. The whole arena is freed at once on destruction or `reset()`, keeping parse-time allocation costs near zero.
- `alloc` returns `nullptr` for a zero size and on allocation failure (underlying `malloc` returned null); **callers must check the result**.
- `alloc_obj<T>` value-initializes the object in place (placement-new with forwarded args); can return null on allocation failure.
- `dup` copies `n` bytes / a C string into the arena and returns a NUL-terminated pointer.
- Move constructible/assignable; copy is deleted.
- **Threading: not thread-safe** — one arena per document.

*Coverage: `memory.hpp` lines 1–85.*

---

## Cross-reference

- Chinese version: [`docs/interface/zh/convert.md`](../zh/convert.md) (中文版)
