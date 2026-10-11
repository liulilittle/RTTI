# RTTI — Design & Architecture

> Edition: English. Chinese version: [`../../zh/architecture.md`](../zh/architecture.md).
> This document reflects the current source tree (HEAD at the time of writing).
> Mechanism descriptions are grounded in the headers; where a detail is not
> stated, the headers are authoritative.

## 1. Overview & goals

RTTI is a C++17, header-and-library reflection and (de)serialization framework
with **no third-party runtime dependencies**. It provides:

- a compile-time **type registration** system (`RTTI_DEFINE_TYPE*` macros) that
  produces runtime metadata (`type_desc`) for a `struct`/`class`: fields, base
  classes, methods, template arguments, attributes, enum entries, ctors;
- generic **format-agnostic conversion** (`to_value` / `from_value`) over a
  small `value_io` interface, plus a value-semantics strong-cast bridge
  (`rtti::convert::as` / `cast` / `to`) over the scalar lanes;
- reflected field **member-access levels** (`member_access`: public_ /
  protected_ / private_) with private/protected field reflection
  (`RTTI_FRIEND_FIELDS` for non-intrusive registration);
- **five serialization backends** — JSON, XML, INI, TOML, protobuf — and a
  FlatBuffers layout generator, all optional (compile-time strip gates);
- convenience tooling: string building, base-N conversion, date-time,
  basic IoC, ORM-style binding helpers;
- broad C++17 portability, including **non-x86 / Android NDK** targets:
  `<rtti/detail/from_chars_real.hpp>` compile-time probing falls back to a
  `strtod`-based parser when the standard library lacks floating-point
  `std::from_chars` (libc++ < 19, e.g. NDK r26 / LLVM 17).

Non-goals: no RTTI-standard `typeid` dependency for the metadata path (the
library builds its own tables); no exceptions in the hot conversion path
(errors via `error_code`); no vendored third-party parsers.

## 2. Layered architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│  Tool layer          reflect ORM · ioc · to_string · string_ops       │
│                      base_convert · date_time · naming · number_format│
├──────────────────────────────────────────────────────────────────────┤
│  Protocol layer      proto (schema/wire/bridge) · flat/flat_schema    │
├──────────────────────────────────────────────────────────────────────┤
│  Format layer        json · xml · ini · toml  (value_io trees)        │
├──────────────────────────────────────────────────────────────────────┤
│  Conversion layer    convert.hpp : converter<T>, to_value/from_value, │
│                      *_named variants, value_io bridge                │
├──────────────────────────────────────────────────────────────────────┤
│  Core value model    field_value · value_io · error (code/result)     │
├──────────────────────────────────────────────────────────────────────┤
│  Reflection core     reflection.hpp: type_id · type_desc · instance · │
│                      object_ref · fields/enum/template/method/ctor/   │
│                      attribute tables · RTTI_*_DEFINE macros          │
└──────────────────────────────────────────────────────────────────────┘
   Support: memory arena (json/xml/reflection shared) · detail/scan (SIMD)
   version.hpp (generated) · rtti_fwd.hpp (gates + fwd decls)
```

Direction rule: **the reflect core never includes a format/protocol header**.
Adapters implement `rtti::value_io` and are opt-in; a header with its gate off
is a hard `#error`. This keeps the core composition-clean and lets a stripped
build drop any backend.

## 3. Core mechanisms

### 3.1 Type registration model

`RTTI_DEFINE_TYPE_INTRUSIVE(T, ...)` (and friends) expand in the `rtti`
namespace into a specialization of `rtti::detail::fields_of<T>` derived from
`T::rtti_registration` — a nested `registration` type the macro also plants
inside `T` — carrying a `count` and a static read-only array of
`field_access_base*` entries. `fields<T>()` / `field_count<T>()` return table
and count; `type_of<T>()` materializes the `type_desc` for `T` (and, for
templates, `template_info<T>` deriving static `template_arg_desc_of` rows).

Each field's row in `fields_of<T>::desc` also carries a compile-time access
level (`field_type_desc::access`, a `member_access` of public_/protected_/
private_). Non-intrusive registration (`RTTI_DEFINE_TYPE(T, ...)` at namespace
scope) resolves it through a per-type, namespace-scope probe holder
(`detail::rtti_access_holder<T>`), so **private / protected fields are
reflectable** once the class grants the field table access via
`RTTI_FRIEND_FIELDS(T)` (`friend struct ::rtti::fields_of<T>;` inside the
class body). The probes are not friends themselves, so the grant never leaks
into the classification. Intrusive registration forms `&T::f` inside the class
(no friend needed) but cannot classify access from inside the body, so every
intrusive field conservatively reports `private_` (the documented boundary).

Enums register through `RTTI_DEFINE_ENUM(E, ...)` → `enum_table<E>` with
`count` and `enum_label<E>{name, value}[]`; the runtime `enum_desc` (reached via
`type_desc`) now also publishes a parallel values array together with
`value_size` (B1). Template arguments register via `RTTI_TEMPLATE_INFO(...)`.

Cross-TU/ODR: tables are built as `static`/`inline` members in headers so each
TU sharing the header sees the same set; registration is per-type and id
assignment is deterministic per binary. The `fields_of` primary template is
undefined on purpose — an unregistered type is a **compile-time error** (fail
fast), the same convention as `enum_table<E>` for unregistered enums.

### 3.2 Type erasure & the `instance`/`as`/`cast` path

`object_ref<T>` is a typed view; `instance` is a type-erased holder whose data
pointer and dynamic `type_desc` live together. `instance::as<T>()` returns
`T*` (nullptr on failure); `cast<T>()` returns `result<T*>` distinguishing
`kind_mismatch` from `no_such_field` (empty source). Both are `const`-rejected
at compile time via `static_assert`. On the erased path the dynamic type
resolves **through the reflected base chain (offset-correct)**; if the dynamic
descriptor chain was never materialized, conversion falls back to the stored
static type's chain — legal upcasts to a stored static base never fail
spuriously. Because the erased source is not a compile-time polymorphic class,
`dynamic_cast` is inapplicable (C2681) on `instance`; unreachable conversions
fail cleanly. Virtual-base upcasts use `base_adjuster`-derived offsets.

In addition to the pointer casts, `namespace rtti::convert` (`convert.hpp`) is a
**value-semantics strong-cast bridge** (`as<T>` → `optional<T>`, `cast<T>` /
`to<T>` → `result<T>`) that converts native scalars, strings, `field_value` and
`value_io` sources into the scalar family / registered enums / single-layer
`std::vector<scalar>` targets. It deliberately lives in its own namespace and
SFINAEs out non-`value_io` pointers so it never shadows the pointer overloads
above.

### 3.3 Format layering & strip gates

Every adapter (`json/xml/ini/toml/proto/flat`) is a header gated by
`RTTI_ENABLE_<NAME>` (default 1 through `rtti_fwd.hpp`); the default build has
all on, and the strip matrix exercises representative subsets down to
all-off (`build-off`). `to_string.hpp` serializes to a compact single-line
tree **without** depending on the format headers, so string output works in a
fully stripped build.

### 3.4 Error model

`error_code` (error.hpp) is one `uint8_t` enum shared by the core and every
adapter (JSON/XML/INI/TOML/proto/flat): `ok`, syntax families
(`unexpected_token`, `unterminated_string`, `invalid_number`, `invalid_utf8`,
`unterminated_comment`, `mismatched_tag`, `duplicate_attribute`, `bad_entity`,
`unterminated_cdata`, `dtd_missing`, `too_deep`, `invalid_root`,
`ini_unterminated_section`, `ini_bad_line`, `ini_missing_value`), semantic
families (`no_such_field`, `kind_mismatch`, `missing_key`, `no_such_method`,
`overflow`) and proto-specific (`proto_truncated`, `proto_bad_field`,
`proto_type_mismatch`). `error_info` records `src`/`offset` and computes
`line()`/`column()` lazily. `result<T>` is an error-code-plus-value carrier with
`ok()`. The §0 contract is **fail fast, never crash**, and for numeric
conversions `overflow` instead of silent wraparound.

### 3.5 Memory strategy

JSON/XML and the reflection core share `rtti::memory_arena` (arena allocates
blocks; size-0 returns null per comment). JSON/INI/TOML **copy every stored
string into document-owned storage**, so views stay valid after the parse
source dies; **XML keeps zero-copy `string_view`s into the source buffer** and
documented that the caller must keep the source alive (xml.hpp:110-116).
INI/TOML node pools use `std::unique_ptr`-based pooling. Programmatic node
construction is bounds/owning-safe; `destroy_nodes()` runs node destructors in
document order before arena reclamation.

### 3.6 Number formatting

`number_format.hpp` exposes the three-layer syntax and the shared real-formatter
(`detail::scalar_format_real` in convert.hpp): shortest round-tripping text via
`std::to_chars`, `.0` appended when the token lacks `.`/`e`/`E`, and
deterministic tokens for non-finite values. Float rendering is unified across
JSON/XML/INI/TOML (audit C2), and `.0` is decided from the written segment
rather than the whole shared stream (R1-8).

### 3.7 Thread safety & ABI

**Thread safety.** Two global registries are internally synchronized: the
reflection descriptor registry serializes registration and lookup through
`desc_registry_mutex()` (a `lock_guard` guards both entry points), and the
proto registry serializes all descriptor writes with its own `std::mutex`
(reads of stable-node data are lock-free). `derived_pool<D>` builds its merged
tables behind magic statics, which are thread-safe for initialization. Nothing
else is thread-safe: documents, `memory_arena`, format trees, `ioc` containers
and all conversion entry points assume a single thread per object (or external
synchronization). Sharing one container across threads is a caller contract:
the container (and, for `ioc`, its services) must outlive every use and the
caller must serialize access.

**ABI.** The library is header-only; no binary compatibility is promised
across versions, compilers, or standard-library versions. Types documented as
"ABI-stable" (e.g. `field_access` enumerator values 1/2/3) are stable in
semantics only. Mixing translation units built with different compilers or
`_GLIBCXX_USE_CXX11_ABI`/`_ITERATOR_DEBUG_LEVEL` settings is unsupported;
rebuild everything from source together.

## 4. Key design decisions & rationale

| # | Decision | Rationale / trade-off |
| --- | --- | --- |
| D1 | Registered-only reflection; unregistered type/enum is a compile-time error | Fail fast at compile time beats a runtime surprise (`fields_of`, `enum_table` primaries undefined) |
| D2 | Adapters implement `value_io`; core includes none | Composition-clean; one conversion engine, many serializers; strip gates cheap |
| D3 | `error_code` + lazy `line/column` + `result<T>`; no exceptions in hot path | Predictable performance; callers pay for diagnostics only when needed |
| D4 | `as<T>`/`cast<T>` reject `const` via `static_assert`; `cast` returns `result<T*>` | Const-correctness enforced at compile time; failure categories are explicit |
| D5 | `instance` erasure uses static-chain + dynamic-identity fallback, not `dynamic_cast` | `dynamic_cast` inapplicable on erased `void*` (C2681); offset-correct base resolution preserved |
| D6 | Virtual-base upcasts legal; downcasts of non-polymorphic sources fail cleanly | Mirrors `static_cast` guarantees; safety over assumption |
| D7 | XML zero-copy; JSON/INI/TOML copy-all | Zero-copy favors large docs / low alloc; copy-all favors safety after source destructs; contract is explicit per adapter |
| D8 | Numeric overflow → `error_code::overflow`, never silent wrap | §0 honesty; cross-format consistency |
| D9 | `for_each_field(obj, ...)` iterates via accessors (virtual get) | Uniform read across field kinds; no per-field switch in user code |
| D10 | Version single-source: `project(RTTI VERSION 0.1.0)` → `configure_file` → `include/rtti/version.hpp` | One number to bump; CMake, headers and docs agree |
| D11 | Fields-sorted naming policy (`key_case`/`rename_map`/`skip_empty`) lives in `detail::naming_policy`; adapters re-export `key_case` | One policy engine; natural per-format spelling (`rtti::json::key_case::snake`) |
| D12 | Benchmarks emit an environment header and are median-of-N | Numbers are data points, not claims; reproducibility is part of the artifact |
| D13 | Field access level resolved by namespace-scope probes (`rtti_access_holder`); non-intrusive registration can list private/protected fields after `RTTI_FRIEND_FIELDS` | Probes are never the friend, so the grant cannot leak into classification; classification stays a compile-time property even when the class grants access |
| D14 | Intrusive registration cannot classify from inside the class; every intrusive field conservatively reports `private_` | Same header mechanism whether or not friendship is granted; the conservative boundary is documented, and `field_access_of<T>(i)` still reports it |
| D15 | Floating-point parsing probes `std::from_chars` and falls back to a `strtod`-based parser when absent (`from_chars_real.hpp`) | Non-x86 / Android NDK (libc++ < 19) still get correct FP round-trips without a vendor workaround |

## 5. Extensibility & limitations

- **Add a format**: implement `value_io` over an owned tree, gate it with a
  `RTTI_ENABLE_*` switch, provide `parse`/`serialize`/`parse_as`/`serialize`,
  and reuse `to_value`/`from_value`. Core changes needed: none.
- **Add a reflected kind**: extend `fields_of`/`type_desc` (registration),
  `field_value` (value lane) and `converter<T>` (conversion) — the three slots
  the core makes public.
- Documented limits: `.proto` file parser is out of scope (schema registration
  preferred); duplicate-key semantics differ across formats (JSON/XML
  first-match, INI/TOML later-wins); TOML implements a v1.0 core subset;
  INI cannot round-trip a single-element or empty sequence; XML requires the
  parse source to outlive the document; `dynamic_cast_ref` applies only to
  non-virtual single inheritance (offset-0).

## Licence

See [LICENSE](../../../LICENSE) (MIT).