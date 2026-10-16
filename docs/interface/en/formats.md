# Format Adapters — Interface Reference (JSON / XML / INI / TOML)

> Module: `rtti::json`, `rtti::xml`, `rtti::ini`, `rtti::toml`
> Files: `include/rtti/json.hpp`, `xml.hpp`, `ini.hpp`, `toml.hpp`
> Edition: English. Chinese version: [`../../zh/formats.md`](../zh/formats.md).
> APIs in this chapter are extracted from the headers named above; line ranges reflect the current tree.

## 0. Shared design

All four adapters implement `rtti::value_io` over an owned tree, so the
reflect-core converters (`rtti::to_value` / `rtti::from_value` from
`<rtti/convert.hpp>`) read and write documents through one generic interface.
Each format is **opt-in**: gated by `RTTI_ENABLE_JSON` / `RTTI_ENABLE_XML` /
`RTTI_ENABLE_INI` / `RTTI_ENABLE_TOML` (default 1 in `<rtti/rtti_fwd.hpp>`);
including a header with its gate off is a hard `#error`. The reflect core never
includes a format header, so the core stays adapter-free.

Common entry points per format (JSON/XML/INI/TOML):

| Operation | Signature (representative) |
| --- | --- |
| Parse to DOM | `result<document> parse(string_view src[, const parse_config& cfg])` |
| Streaming SAX | `result<void> parse_stream(string_view src, const parse_events& events)` |
| Serialize DOM → string | `std::string serialize(const document&, const write_config& cfg = {})` |
| Serialize DOM → buffer | `void serialize(const document&, std::string& out, const write_config& cfg = {})` (overwrites, never appends; keeps reserved capacity) |
| Reflect bridge parse | `template<typename T> result<T> parse_as(string_view src[, const write_config&])` |
| Reflect bridge serialize | `template<typename T> std::string serialize(const T& obj, const write_config& cfg = {})` |

Naming policy: when `cfg.rename_map == nullptr && cfg.key_case ==
`rtti::detail::key_case::identity` `&& !cfg.skip_empty`, the plain
`to_value`/`from_value` path is used; otherwise the `*_named` variant applies
`key_case` / `rename_map` / `skip_empty` to reflected field keys.

---

## 1. JSON (`rtti::json`)

`include/rtti/json.hpp` (260 lines). Gated by `RTTI_ENABLE_JSON`.

### 1.1 `write_config` (json.hpp:64-76)

| Member | Type | Default | Meaning |
| --- | --- | --- | --- |
| `indent` | `int` | `2` | `0` = compact single line; `>0` = pretty with spaces |
| `sort_keys` | `bool` | `false` | deterministic key order |
| `indent_char` | `char` | `' '` | indentation fill character |
| `key_case` | `detail::key_case` | `identity` | reflected-field key naming |
| `rename_map` | `const std::map<std::string,std::string>*` | `nullptr` | name remap |
| `skip_empty` | `bool` | `false` | drop null-equivalent members |

### 1.2 `parse_events` (json.hpp:83-94)

SAX callbacks, all optional `std::function`; returning `false` cancels the
parse early (`parse_stream` then returns `ok`): `object_start()`,
`object_end()`, `array_start()`, `array_end()`, `key(string_view)`,
`string_value(string_view)`, `integer_value(int64_t)`, `real_value(double)`,
`bool_value(bool)`, `null_value()`.

### 1.3 `document` (json.hpp:105-161)

Implements `rtti::value_io`; owns its string arena, node pool and root.
Movable, non-copyable. Read side proxies the root node and provides
`type()/as_bool/as_integer/as_real/as_string/size()/at()/get_key()/get_key_at()`.
Write side: `make_array()/push_back/make_object()/set_key/make_scalar(field_value)/make_node()`.

Document-specific members:

- `std::string write(const write_config& cfg) const` — public serializer.
- `void write(const write_config& cfg, std::string& out) const` — append directly into `out`.
- `void adopt_root(detail::node* root) noexcept` — adopt a freshly parsed root.

### 1.4 Free functions

```cpp
result<document> parse(string_view src);
result<void> parse_stream(string_view src, const parse_events& events);
std::string serialize(const document& doc, const write_config& cfg = {});
void serialize(const document& doc, std::string& out, const write_config& cfg = {});
// reflect bridge (template, in-header):
template <typename T> result<T> parse_as(string_view src);
template <typename T> result<T> parse_as(string_view src, const write_config& cfg);
template <typename T> std::string serialize(const T& obj, const write_config& cfg = {});
```

### 1.5 Semantics (json.hpp:20-27)

- Every stored string (payloads and keys) is copied into the document's arena;
  views remain valid after the source buffer / temporaries are destroyed.
- Numbers: a literal without `.`/`e`/`E` that fits `int64` is an integer node
  (`int64` overflow demotes to a real node); a fraction or exponent makes a
  real node. `as_real()` also accepts integer nodes (JSON `"42"` fills a
  `double` field).

---

## 2. XML (`rtti::xml`)

`include/rtti/xml.hpp` (281 lines). Gated by `RTTI_ENABLE_XML`.

### 2.1 `write_config` (xml.hpp:58-69)

| Member | Type | Default | Meaning |
| --- | --- | --- | --- |
| `with_declaration` | `bool` | `true` | emit `<?xml version="1.0" encoding="UTF-8"?>` |
| `indent` | `int` | `4` | `0` = compact; `>0` = pretty with that many spaces |
| `indent_char` | `char` | `' '` | indentation fill |
| `key_case` | `detail::key_case` | `identity` | naming policy |
| `rename_map` | pointer | `nullptr` | name remap |
| `skip_empty` | `bool` | `false` | drop null-equivalent members |

### 2.2 `parse_config` (xml.hpp:72-76)

- `max_depth` (`size_t`, default `512`) — element nesting depth limit
  (`too_deep`).
- `max_entity_depth` (`size_t`, default `32`) — DTD entity expansion depth.
- `max_entity_expansions` (`size_t`, default `10000`) — total DTD entity
  expansion budget.

### 2.3 `parse_events` (xml.hpp:83-91)

`element_start(string_view)`, `element_end(string_view)`,
`attribute(string_view, string_view)`, `text(string_view)`,
`cdata(string_view)`, `comment(string_view)`, `pi(string_view)`.

### 2.4 `document` (xml.hpp:117-190)

Like JSON, implements `value_io`; move-only, non-copyable. Additional members:

- `const char* intern(string_view s)` — copy `s` into the arena, return stable NUL-terminated pointer (write-side keys outlive temporaries).
- `std::string ns_uri(const rtti::value_io* elem, string_view prefix) const` — in-scope namespace URI bound to `prefix` at `elem`; `""` when unbound; `xmlns` attributes preserved verbatim.
- `detail::node* create_node()` / `alloc_view_node()` — tree/view node allocation.
- inspect helpers: `prolog_nodes()`, `epilog_nodes()`, `root_plus()`.

**Zero-copy contract (xml.hpp:110-116) — read carefully**: the document owns its
nodes and arena-interned strings, but element/attribute names, attribute values,
text and CDATA are `string_view`s **into the parse source buffer**. The caller
must keep the source buffer alive for the whole document lifetime — unlike
json/ini/toml, which copy every stored string into the document's arena.

### 2.5 Mapping rules (xml.hpp:13-24)

Root element ↔ object; field name ↔ key; attribute ↔ scalar field (attributes
win over child elements); child-element text ↔ scalar field; a field named
`value`/`_text` binds to the element's own text; repeated same-name child
elements form an array; struct fields map to child elements parsed as objects.

### 2.6 Free functions

```cpp
result<document> parse(string_view src);
result<document> parse(string_view src, const parse_config& cfg);
result<void> parse_stream(string_view src, const parse_events& events);
void serialize(const document& doc, std::string& out, const write_config& cfg = {});
std::string serialize(const document& doc, const write_config& cfg = {});
template <typename T> inline result<T> parse_as(string_view src);
template <typename T> inline result<T> parse_as(string_view src, const write_config& cfg);
template <typename T> inline std::string serialize(const T& obj, const write_config& cfg = {});
```

---

## 3. INI (`rtti::ini`)

`include/rtti/ini.hpp` (391 lines). Gated by `RTTI_ENABLE_INI`.

### 3.1 `kMaxSectionDepth` (ini.hpp:77)

`inline constexpr std::size_t kMaxSectionDepth = 512;` — dotted section-header
component cap, aligned with the JSON/XML/TOML parse-side `max_depth`. Deeper
headers return `error_code::too_deep`.

### 3.2 `write_config` (ini.hpp:82-94)

`with_comments` (`bool`, default `false`) — emit comments preserved during
parsing; plus `key_case` / `rename_map` / `skip_empty` / `indent_char`
(accepted for symmetry; line-based writer ignores indentation).

### 3.3 `detail::node` (ini.hpp:103-194)

Public read/write `value_io` methods, plus parser helpers: `set_scalar_text`,
`find_child(name)`, `set_key_impl` (a repeated scalar replaces itself with an
array slot; a repeated array appends), `replace_child`.

### 3.4 `document` (ini.hpp:215-260)

Owned tree; `move`-defaulted `move` ctor/assignment; non-copyable. Root is an
object (the global, section-less area). Delegates all `value_io` to the root.

### 3.5 `open_section` (ini.hpp:269-271)

```cpp
detail::node* open_section(detail::node* current, rtti::string_view name,
                           detail::pool* pool, std::size_t max_depth, bool& too_deep);
```

Open (or extend) the section selected by a dotted path. Repeated final
components accumulate into an array slot; nested paths under repeated sections
attach to the last element. Deeper than `max_depth` returns `nullptr` with
`too_deep` set (instead of building an unbounded tree that `serialize()` could
later recurse into).

### 3.6 Live `parse_events` (ini.hpp:283-290)

`section_start(string_view)` (name, `""` for global), `section_end(string_view)`,
`key_value(string_view, string_view)`. Comments never fire events; a dotted
header like `[a.b]` is reported as a single section named `a.b` (streaming does
not reconstruct the tree). Callback returning `false` stops immediately and
`parse_stream` returns `ok`.

### 3.7 Semantics & limits (ini.hpp:7-44)

- Sections `[name]` and dotted nested names can contain blanks (trimmed).
- Keys: `key = value` and `key: value` (first `=` wins), whole-line `;`/`#`
  comments preserved (optional on output); inline comments NOT recognized.
- Quoted values/keys `"..."`/`'...'` with escapes `\" ' \\`.
- Storage: every scalar text, key and comment is copied on parse — documents
  are self-contained, movable, returnable.
- Duplicate keys: value becomes an array slot holding every occurrence; scalar
  reads return the LAST (later-wins); sequences read all values.
- Duplicate sections: the slot becomes an ARRAY of section objects.
- Known limits: a single-element sequence cannot round-trip (one key/section ⇒
  scalar/object slot, not array); an EMPTY sequence emits nothing →
  strict types report `missing_key`; empty/blank-only input is an error
  (`ini_bad_line`); literal-newline strings do not round-trip.

### 3.8 Free functions

```cpp
result<document> parse(rtti::string_view src);
result<void> parse_stream(rtti::string_view src, const parse_events& events);
void serialize(const document& doc, std::string& out, const write_config& cfg = {});
std::string serialize(const document& doc, const write_config& cfg = {});
template <typename T> std::string serialize(const T& obj, const write_config& cfg = {});
template <typename T> result<T> parse_as(rtti::string_view src);
template <typename T> result<T> parse_as(rtti::string_view src, const write_config& cfg);
```

---

## 4. TOML (`rtti::toml`)

`include/rtti/toml.hpp` (343 lines). Gated by `RTTI_ENABLE_TOML`. TOML v1.0 core
subset.

### 4.1 `write_config` (toml.hpp:80-85)

`key_case` / `rename_map` / `skip_empty` / `indent_char` (accepted for symmetry;
line-based writer ignores indentation).

### 4.2 `parse_config` (toml.hpp:87-89)

`max_depth` (`size_t`, default `512`) — array/inline-table nesting limit
(`too_deep`).

### 4.3 `parse_events` (toml.hpp:95-107)

`table_start(string_view)`, `table_end(string_view)`,
`array_table_start(string_view)`, `array_table_end(string_view)`,
`key_value(string_view, string_view)` (reports the **raw** source slice of a
value — quotes/brackets included), `array_start()`, `array_end()`,
`string_value(string_view)`, `integer_value(int64_t)`, `real_value(double)`,
`bool_value(bool)`. Typed scalar events fire before that key's `key_value`.
Section-less keys are bracketed by an implicit `table_start("")/table_end("")`.

### 4.4 `document` (toml.hpp:204-251)

Owned tree; move-only. Root is always an object. Delegates `value_io` to root.
`detail::node` stores scalar text owned, plus `is_datetime` (date/time kept
unquoted on output).

### 4.5 Semantics & limits (toml.hpp:9-47)

- `key = value`; dotted keys `a.b.c`; quoted keys `"k"`/`'k'`.
- Tables `[a.b]`; array tables `[[a.b]]` mapped to arrays of objects.
- Scalars: strings (basic/multi-line/literal), integers (dec/hex/oct/bin with
  optional underscores), floats (decimals, exponents, `inf`/`nan`/`±inf`),
  booleans, date/time mapped to canonical strings (exact source text kept,
  round-trip unquoted, lossless). Integers normalize to canonical decimal.
- Arrays allow trailing comma, mixed types, nested arrays, inline tables
  `{k = v, ...}`.
- Comments (full-line and trailing `#`) skipped, not preserved.
- Duplicate keys: **later-wins**. Reopening an existing `[table]` continues it;
  `[[t]]` over an existing table turns it into an array holding the old table
  first.
- Malformed input returns an `error_code` (allocation may throw `std::bad_alloc`;
  never crashes). Deep nesting yields `too_deep`.
- Serialization writes a table's scalars before sub-tables; parse→serialize→
  parse round-trips.
- Known limits: strict TOML v1.0 corner cases simplified (redefining a table as
  an array table follows later-wins/reuse-last rather than erroring); edge
  quoting follows simplified reader rules; comments not re-emitted.

### 4.6 Free functions

```cpp
result<document> parse(string_view src, const parse_config& cfg);
result<document> parse(string_view src);
result<void> parse_stream(string_view src, const parse_events& events);
void serialize(const document& doc, std::string& out, const write_config& cfg = {});
std::string serialize(const document& doc, const write_config& cfg = {});
template <typename T> std::string serialize(const T& obj, const write_config& cfg = {});
template <typename T> result<T> parse_as(string_view src);
template <typename T> result<T> parse_as(string_view src, const write_config& cfg);
```

---

## 5. Cross-format notes

- **Buffer serialize semantics**: XML/INI/TOML `serialize(doc, std::string&)`
  *replace* `out` (never append; keep reserved capacity). JSON's `write(cfg,
  out)` is append-style, but its `serialize(doc, out, cfg)` overload clears
  `out` first for the same overwrite contract.
- **Source lifetime**: json/ini/toml copy all strings; **XML is zero-copy**
  (source buffer must outlive the document).
- **Comment/entity handling** differs per format as documented above.
- **Depth guards**: JSON/XML/TOML parse-side `max_depth` (512) and INI
  `kMaxSectionDepth` (512) bound hostile nesting to `too_deep`; XML adds DTD
  entity depth/expansion budgets.

## Licence notice

Both editions of this reference describe the public interface only. See
[LICENSE](../../../LICENSE) for the project licence.