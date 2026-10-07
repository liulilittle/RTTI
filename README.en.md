# RTTI — Runtime Type Information for C++17

[简体中文](README.zh-CN.md) | 中文版见 [README.zh-CN.md](README.zh-CN.md)

RTTI is a dependency-free, portable (MSVC / GCC / Clang), C++17 runtime
reflection library that compensates for C++'s lack of first-class reflection.
What used to require tedious boilerplate — walking fields, (de)serializing,
introspecting at runtime — is now one macro away.

## Table of Contents

- [Introduction](#introduction)
- [Quick Start](#quick-start)
- [Core Capabilities](#core-capabilities)
- [Format Adapters](#format-adapters)
- [Architecture](#architecture)
- [Supported Containers](#supported-containers)
- [Examples](#examples)
- [Build Configuration & Strip Matrix](#build-configuration--strip-matrix)
- [Portability](#portability)
- [Documentation](#documentation)

## Introduction

Standard C++11 `typeid` gives you only a mangled name. RTTI builds on it a
practical field-reflection layer:

- **Register** structs with the `RTTI_DEFINE_TYPE` macro family (external,
  intrusive, derived, with-default variants) and enums with `RTTI_DEFINE_ENUM`.
- **Introspect** at runtime: field count, names, types (`.NET`-grade
  `rtti::get_type`, `find_field`, `get_field`, `set_field`, and range-for over
  fields).
- **Convert** recursively between your structs and a format-agnostic
  `value_io` tree (`from_value` / `to_value`), covering scalars, `std::string`,
  and the standard container families — allocator- and comparator-transparent.
- **Decouple** formats: the reflect core never includes a format header;
  JSON/XML/INI/TOML/protobuf sit behind optional strip gates.

Beyond the basic registry, the library ships a set of standalone core
capabilities (type judgement, attributes, reflective instantiation, iterators,
`date_time`, arbitrary-base conversion, unified `to_string`, key naming and an
IoC container) — see [Core Capabilities](#core-capabilities).

The whole library is header-delivered logic plus tiny `src/` support; no
third-party dependencies, no non-standard compiler intrinsics.

## Quick Start

Define a struct and register it — that is everything:

```cpp
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include <string>

struct Person {
    std::string name;
    int age = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(Person, name, age);   // register in class body
};
```

Introspect and convert:

```cpp
rtti::get_type<Person>().find_field("age");          // const field_access_base*

Person p{"Ada", 36};
rtti::field_value v;
rtti::get_field(p, "age", v);                        // false when name unknown

// Recursive conversion through a value_io test double:
rtti_test_lib::box* src = rtti_test_lib::make_obj(); // any value_io impl
auto e = rtti::to_value(src, p);                     // error_code
```

Nested structs, containers, enums and custom ADL conversions all flow through
the same two entry points. Non-intrusive registration lives at namespace
scope:

```cpp
RTTI_DEFINE_TYPE(Person, name, age)                  // outside the class
```

Missing keys behave strictly by default (error); the `_WITH_DEFAULT` macro
keeps the existing member value instead. Derived registration
(`RTTI_DEFINE_TYPE_DERIVED`) opts a single-inheritance base chain into the
reflected `is-a` machinery.

## Core Capabilities

Beyond the field registry, the reflect core exposes a portfolio of
stand-alone convenience surfaces. All of them are header-only, failure-safe
(`error_code` / `result<T>`, never throw) and format-independent.

### Type judgement — `<rtti/reflect.hpp>`

Compile-time aliases inside `namespace rtti`:

- `is_same<T,U>`, `is_base_of<Base,Derived>`, `is_convertible<From,To>` and
  `can_cast<From,To>` (with their `_v` variable templates).

Runtime judgement over reflected objects (spec 19.2):

- `is_instance_of<T>(obj, type_id)` — dynamic or static exact-instance check;
- `is<T>(instance)` — exact runtime type equality on a handle;
- `is_derived_from<T>(instance)` — true when the instance's runtime type is T
  or transitively derived through the reflected base chain;
- `dynamic_cast_ref<T>(instance)` — reflect a handle to its T view after the
  type-id chain is verified; unsafe casts fail with an `error_code`;
- `can_convert(src, target)` / `can_convert(instance, target)` — type_desc /
  instance convertibility (same, derived, or scalar-convertible).

`type_desc` also exposes `is_abstract()` / `is_polymorphic()` accessors, and
`reflection.hpp` adds the compile-time aliases `is_abstract` /
`is_polymorphic` / `has_virtual_destructor` (with `_v` variants).

Note: `dynamic_cast_ref` is only guaranteed along the registered non-virtual
single-inheritance chain (virtual- or multiple-base targets are an unsupported
corner); runtime field access on intrusive (`RTTI_DEFINE_TYPE_INTRUSIVE`)
class templates is only reliable in `main` context, not during static
initialization.

### Attribute system — `<rtti/reflection.hpp>`

A compile-time annotation framework with runtime querying. Register
type-level attributes with `RTTI_DEFINE_TYPE_ATTRS(T, ...)` and per-field
attributes with `RTTI_DEFINE_FIELD_ATTRS(T, ...)`, each attribute declared as
`RTTI_ATTR("Name", args...)` / `RTTI_FIELD_ATTR_ENTRY(field, attrs...)`.
Arguments are polymorphic integers / reals / strings / booleans
(`attr_arg`), in any mix.

Type-level query: `attr_count<T>()`, `attr_at<T>(i)`, `find_attr<T>(name)`,
`has_attr<T>(name)`. Field-level query: `field_attr<T>(field|index)`,
`field_attr_count<T>`, `field_attr_at<T>`, `field_find_attr<T>`,
`field_has_attr<T>`.

### Reflective instantiation — `<rtti/reflection.hpp>`

Build reflected types at runtime.

- `RTTI_DEFINE_CTOR(T, ("param", Type, optional), ...)` registers a
  reflective constructor registry (`ctor_registry<T>`).
- `make_object<T>()`, `make_object<T>({ctor_arg::...})` and
  `create_instance<T>` return an owning `object_ref<T>`; `ctor_arg::scalar`
  and `ctor_arg::node` support positional or named (out-of-order) injection.
- `make_object<T>(alloc, ...)` constructs into raw memory from a caller
  allocator (placement-new; manual, idempotent `destroy()`).
- The `object_ref<T>` handle covers `get`/`set` field ops, `field_size`,
  `add`/`erase_at`, element iteration, attribute queries and `type_of()`.
- All failures arrive as `error_code` (`missing_key`, `kind_mismatch`, ...),
  never as exceptions.

### Reflection iterators

- Field arrays: `field_begin` / `field_end`, `field_rbegin` / `field_rend`
  (reverse), plus the bare `begin`/`end`/`rbegin`/`rend` aliases. Boundary
  positions dereference safely to `nullptr`.
- Container element iterators: `element_begin` / `element_end` /
  `element_rbegin` / `element_rend` iterate container fields with three access
  forms — scalar `element_value()`, generic `element_to(value_io*)`, and
  position `index()` / `key()`. Out-of-range access is safe, never crashes.

### Method & template-argument reflection — `<rtti/reflection.hpp>`

- Register reflected methods with `RTTI_DEFINE_METHOD` (member) /
  `RTTI_DEFINE_STATIC_METHOD` (static), enumerate them via `methods<T>()` /
  `method_count<T>()`, and dispatch with the `reflect_method` overload family
  (instance, `object_ref<T>`, `instance` handle, and static
  `reflect_method<T>(name, args...)`).
- `RTTI_TEMPLATE_INFO(T, ...)` records a template instance's explicit argument
  list: query it with `template_arg_count<T>()`, `template_args<T>`,
  `template_arg_at<T, I>`, `template_args_of<T>()`,
  `template_arg_desc_of<T>(i)`.
- ORM convenience (`<rtti/reflect.hpp>`): a `row_cell` row plus
  `bind<T>(obj, row, count)` / `bind<T>(obj, {row_cell...})` fills a reflected
  object from field/value cells.

See `examples/reflection_demo.cpp` and the header comments for code.

### `date_time` — `<rtti/date_time.hpp>`

A value type holding Unix epoch nanoseconds (`int64`):

- precision-aware factories `from_unix_seconds` / `_ms` / `_us` / `_ns`
  (range-checked, report `overflow`);
- ISO 8601 `parse(string_view)` and canonical UTC `format()` / `to_string()`
  with exact round-trip (civil calendar conversion);
- `epoch_ns_from_digits` maps a numeric epoch to nanoseconds by decimal digit
  count (10 = seconds, 13 = ms, 16 = us, 19/20 = ns);
- read-side accessors `to_unix_seconds` / `_ms` / `_us` / `_ns` (floor
  division toward negative infinity) and the raw `epoch_ns()` accessor —
  `rtti::is_date_time<T>` and the `to_value_dt` bridge integrate the type
  with the convert layer;
- full comparisons and nanosecond arithmetic.

### `base_convert` — arbitrary-base integers

`<rtti/base_convert.hpp>` (`rtti::detail`) plus to_string wrappers:

- `to_base` / `to_base_signed` — `uint64_t` / `int64_t` to a digit string in
  any base 2..64;
- `from_base` / `from_base_signed` — string to integer with overflow
  detection and illegal-character reporting;
- custom digit tables (`default_digits()`, `rfc4648_digits()`) and case policy;
- signed variants carry a leading `-`.

### `to_string` — `<rtti/to_string.hpp>`

- unified `T ↔ std::string` for scalars, enums, `date_time`, arbitrary-base
  integers, containers and reflected structs;
- `to_string(v)` and `from_string<T>(s, out)` / `from_string<T>(s)`
  (the latter returns `std::optional<T>`);
- convenience wrappers `to_string_base` / `from_string_base` and signed
  variants over the base-convert layer.

### `naming` — `<rtti/naming.hpp>`

A format-agnostic key naming policy carried by each text adapter's `write_config`:

- `key_case`: `identity` (field name as-is), `snake`, or `camel`;
- `rename_map`: an explicit field-name → output-key override that always wins;
- `skip_empty`: drop fields whose value is empty (empty string / container,
  numeric zero, or null) on output;
- helpers `to_snake_case` / `to_camel_case`, `naming_policy`, `apply_naming`.

### IoC container — `<rtti/ioc.hpp>`

`rtti::ioc::container`, a dependency-injection container:

- `register_type<T>`, `register_as<Iface,Impl>` (interface-to-implementation
  binding), `register_factory<T>`;
- `resolve<T>` / `get<T>` return `result<object_ref<T>>`, with
  `singleton` / `transient` lifetimes;
- constructor dependency injection: `T`, `T*`/`T&`, `std::shared_ptr<T>`;
  cycle detection fails safely with `kind_mismatch`.

## Format Adapters

JSON/XML/INI/TOML are optional build gates. When enabled the four text
adapters share the same shape — an owned DOM (`document : value_io`), a
`parse`/`serialize` pair, a typed bridge `parse_as<T>` / `serialize<T>(obj)`,
a streaming SAX `parse_stream` and a `write_config` (key naming / rename /
skip_empty). The protobuf adapter (`<rtti/proto.hpp>`) is gated the same way
but configures itself through `parse_config` (it has no `write_config`). Each
is header-only and never needed by the reflect core.

### JSON — `<rtti/json.hpp>`

Full JSON DOM: `rtti::json::document`, `parse` / `serialize`,
`parse_as<T>` / `serialize<T>`, `parse_stream`, `write_config` with
indent / `sort_keys` / `indent_char` and key naming (`key_case` / `rename_map`
/ `skip_empty`).

### XML — `<rtti/xml.hpp>`

Owned XML DOM with prolog/epilog nodes: `parse` / `serialize`,
`parse_as<T>`, `parse_stream`, `write_config` (pretty / indent / naming).

### INI — `<rtti/ini.hpp>`

Section-based DOM: `parse` / `serialize`, `parse_as<T>`, `parse_stream`,
`write_config` naming policy.

### TOML — `<rtti/toml.hpp>`

A TOML v1.0 core-subset adapter:

- DOM `parse`, typed `parse_as<T>`, streaming `parse_stream` (SAX);
- `serialize` overloads: `(doc)`, `(doc, out, cfg)` buffer form (clears then
  writes), and `serialize<T>(obj)`; date-time scalars round-trip verbatim,
  unquoted;
- scalars (basic / multi-line / literal strings, decimal/hex/oct/bin
  integers, floats including inf/nan, booleans), arrays, inline tables,
  dotted keys and `[table]` / `[[array-of-tables]]` sections; duplicate keys
  later-win; malformed input returns `error_code` and `parse_config::max_depth`
  guards against `too_deep`.

### protobuf — `<rtti/proto.hpp>`

A protobuf 2/3 wire-format adapter (`.proto` file parsing is out of scope):

- wire codecs: varint (wire 0), fixed 64-bit (1), length-delimited (2),
  fixed 32-bit (5); bounded encode/decode, truncated input →
  `proto_truncated`;
- explicit `rtti::proto::schema<T>` maps each reflected field to a
  `field_desc` (protobuf field number, `wire_type`, `field_mode`
  optional/required/repeated, `packed`, proto2 `with_default_int/real/str`);
- typed bridge `serialize<T>`/`parse_as<T>`: proto2 required-missing →
  `missing_key`, explicit defaults applied on absent optional fields, proto3
  scalar defaults omitted on the wire; repeated fields accept both packed
  and unpacked layout;
- maps, repeated containers and nested messages supported;
- tree bridge `wire_to_tree` / `tree_to_wire` glues any value_io tree to the
  wire; `parse` (schema-less or schema-aware) and `parse_stream` provide SAX
  events (`message_start` / `message_end` / typed field events);
- DoS guards: nesting `max_depth` and length-prefix bounds; field number 0
  and group wire types 3/4 are rejected with `proto_bad_field`, and a
  schema/stream wire mismatch yields `proto_type_mismatch` (never crashes).

## Architecture

Three cooperating layers, with a strict dependency direction — adapters only
ever `#include` the core, never the reverse.

| Layer | Headers | Responsibility |
| --- | --- | --- |
| Reflect core | `reflection.hpp` `reflect.hpp` `convert.hpp` `memory.hpp` `error.hpp` `rtti_fwd.hpp` `date_time.hpp` `base_convert.hpp` `to_string.hpp` `naming.hpp` `ioc.hpp` | field tables, macros, query API, recursive conversion, type judgement, attributes, reflective creation, iterators, date/time, base conversion, naming, IoC |
| `value_io` abstraction | `convert.hpp` | format-agnostic tree interface (`kind`, `at`, `get_key`, `make_scalar`, …) implemented by each adapter |
| Format adapters | `json.hpp` `xml.hpp` `ini.hpp` `toml.hpp` `proto.hpp` | JSON / XML / INI / TOML / protobuf DOM + serializers |

`fields_of<T>` holds the compile-time field table (count + array of typed
accessors). The `RTTI_DEFINE_TYPE` macro specializes it; intrusive variants
register a nested `rtti_registration` that `fields_of` inherits. The runtime
query API and the recursive conversion walk exactly this table, so registration
is the single source of truth. `RTTI_DEFINE_CTOR` feeds `ctor_registry` (used
by reflective instantiation and the IoC container); `RTTI_DEFINE_TYPE_ATTRS`
and `RTTI_DEFINE_FIELD_ATTRS` feed the attribute tables queried by
serializers and generators.

The `RTTI_IS_EMPTY` empty-argument detection and bounded `RTTI_NARG` counting
use a two-level indirect paste that stays correct under MSVC's conformant
preprocessor (`/Zc:preprocessor`).

## Supported Containers

| Family | Recognized by | Notes |
| --- | --- | --- |
| `std::vector<T, A>` / `std::list<T, A>` / `deque` | `is_sequence` (`push_back`, iterators) | allocator `A` flows through untouched |
| `std::map<K,V,C,A>` / `std::unordered_map<...>` | `is_map` (`key_type` + `mapped_type`) | key and mapped types resolved |
| `std::set<T,C,A>` / `std::unordered_set<...>` | `is_set` (associative, no `mapped_type`) | |
| `std::queue` / `std::stack` / `std::priority_queue` | `is_queue` / `is_stack` / `is_priority_queue` | adapter containers |
| `std::array<T,N>` / native arrays | `is_fixed_sequence` / `is_c_array`/`is_md_array` | one- and multi-dim `T[N]` |
| `std::vector<bool>` | `is_vector_bool` | dedicated bit-vector lane |
| `std::optional` / `std::variant` / `std::tuple` / `std::pair` | `is_optional` / `is_variant` / `is_tuple` / `is_pair` | new-style C++17 types |
| Custom allocators / comparators | shape-and-traits dispatch | carried as template params, no extra plumbing |

`std::string` is explicitly excluded from sequence handling (it has
`push_back` and iterators) and routes to the dedicated string converter.
Custom allocators and comparators are carried as template parameters with no
extra wiring. Any type outside these families is dispatched in this order:
user ADL hook → `is_reflected` (walk its fields) → scalar fallback.

## Examples

`examples/` is a self-contained teaching suite. Each file compiles standalone
with MSVC `cl` (zero `/W4` warnings) and documents every call in exhaustive
English comments (what / why / return semantics / error behavior). Demo type
names are namespaced so all TUs link together without collisions.

| File | Demonstrates |
| --- | --- |
| `reflection_demo.cpp` | reflect-core census: registration macros, field tables, `type_of`/`type_desc`, runtime field + container ops, field & element iterators, attributes, `make_object`/`create_instance`/placement-new/`object_ref`, `to_value`/`from_value`, error patterns |
| `core_convert_demo.cpp` | `date_time`, `base_convert`, `to_string`, enum label tables (via a value_io test double) |
| `json_demo.cpp` | JSON DOM, parse / serialize, `parse_as` / `serialize<T>`, SAX streaming |
| `xml_demo.cpp` | XML DOM, parse / serialize, SAX, naming config |
| `ini_demo.cpp` | INI sections and dotted keys, parse / serialize |
| `toml_demo.cpp` | TOML parse, DOM build, streaming SAX, key-case policy, "load → tweak → write back" scenario |
| `proto_demo.cpp` | protobuf 2/3 wire, `schema<T>` field registration, wire ↔ tree, `parse_stream` |

To compile one from an x64 MSVC developer prompt at the repo root (adjust the
`/LIBPATH` entries to your own SDK versions):

```bash
cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd
    /I include examples\reflection_demo.cpp /link
    /LIBPATH:"...\VC\Tools\MSVC\<ver>\lib\x64"
    /LIBPATH:"...\Windows Kits\10\Lib\10.0.<ver>\ucrt\x64"
    /LIBPATH:"...\Windows Kits\10\Lib\10.0.<ver>\um\x64" build\Debug\rtti.lib
```

`/Zc:preprocessor` is required for the registration macros; `/MDd` matches the
debug runtime of `build\Debug\rtti.lib`. The reflect-core examples
(`reflection_demo.cpp`, `core_convert_demo.cpp`) compile even with all format
gates off; format examples require their respective `RTTI_ENABLE_*` gate open
(defaults to 1). Expected result: zero C4xxx warnings, every section prints,
ending in PASS markers, exit code 0.

## Build Configuration & Strip Matrix

Five independent feature gates are defined (default `1`) in
`<rtti/rtti_fwd.hpp>`:

| Gate | Enables |
| --- | --- |
| `RTTI_ENABLE_JSON` | `<rtti/json.hpp>` |
| `RTTI_ENABLE_XML` | `<rtti/xml.hpp>` |
| `RTTI_ENABLE_INI` | `<rtti/ini.hpp>` |
| `RTTI_ENABLE_TOML` | `<rtti/toml.hpp>` |
| `RTTI_ENABLE_PROTO` | `<rtti/proto.hpp>` |

The reflect core has zero format-header dependencies, so it compiles with all
five gates off (`-DRTTI_ENABLE_*=0`) and the stripped build passes the full
test suite (`strip_matrix_test` verifies the core's zero format-header
dependency at the source level). The five gates form a 2^5 = 32-combination
strip matrix; every combination is configured, built and run against the full
test suite independently (assertions grow monotonically with the enabled
gates, from 30274 all-off to 32266 all-on).

## Portability

- **C++17 only** — no C++20 features are required.
- Compilers: MSVC 2019 16.4+ (with `/Zc:preprocessor`), GCC 11+ and
  Clang/LLVM 14+ — the floating-point `std::to_chars` / `std::from_chars`
  used for shortest round-trip float text require libstdc++ 11 and
  libc++ 14; earlier GCC/Clang lack them.
- No `__declspec`, no GNU extensions, no compiler-private builtins.
- `std::chrono::steady_clock` timers, RAII mechanics, standard containers only.
- The library is header-delivered; the reflect core compiles with all five
  format gates off and the stripped build passes the full test suite.

## Documentation

- [English](README.en.md) — this file.
- [简体中文](README.zh-CN.md).

Cross-referenced between editions; every section above has a matching section
in the Chinese edition.