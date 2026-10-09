# RTTI — Runtime Type Information for C++17

[![CI](https://github.com/OWNER/REPO/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/ci.yml)

[简体中文](README.zh-CN.md) | 中文版见 [README.zh-CN.md](README.zh-CN.md)

RTTI is a dependency-free, portable (MSVC / GCC / Clang), C++17 runtime
reflection library that compensates for C++'s lack of first-class reflection.
What used to require tedious boilerplate — walking fields, (de)serializing,
introspecting at runtime — is now one macro away.

## Table of Contents

- [Introduction](#introduction)
- [Quick Start](#quick-start)
- [Core Capabilities](#core-capabilities)
- [Variance casts](#variance-casts)
- [Reflection traversal](#reflection-traversal)
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

The reflect core and its template bridges are header-delivered; every
non-template implementation — the four text adapters' DOM/parser/serialize
bodies, the `.proto` text parser, and all remaining support code — is compiled
once into the `rtti` static library's 19 translation units (`src/*.cpp`). No
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
stand-alone convenience surfaces. All of them are failure-safe
(`error_code` / `result<T>`, never throw) and format-independent; their
template shells stay header-only, with non-template bodies compiled once
into the `src/` library.

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

### String formatting — `<rtti/number_format.hpp>`, `<rtti/date_time.hpp>`

Format-driven text rendering (spec §7), all fail-safe with `error_code`:

- `format_number<T>(v, spec)` — Python-format-style specs for integers
  (base `d/b/o/x/X`, thousands `,`, `#` alternate form, width, fill, align,
  sign, zero-pad) and reals (`f/e/g` plus precision, grouping, width);
  shortest round-trip text by default;
- `to_string<T>(v, format_spec)` — thin overload that routes integers/reals
  through `format_number` and `date_time` through its `format(pattern)`;
- `date_time::format(pattern)` — strftime-style pattern
  (`%Y %y %m %d %H %M %S %f %F %T %R %z %Z %%`); an unknown directive
  reports `unexpected_token`;
- `node_to_string(const value_io*, spec)` — any value_io node (object/array/
  scalar) to one compact line, numbers formatted by the spec;
- `format_reflected<T>(obj, spec)` — walk the `RTTI_DEFINE_TYPE` field table
  and render a compact object line, applying the spec to every numeric field.
  Malformed specs never throw: they return an empty string plus
  `error_code::unexpected_token` through the optional `error_info*`.

### Node-level conversion — `<rtti/node_io.hpp>`

Format-independent path navigation over any `value_io` DOM:

- `rtti::locate(root, "user.addresses[1].city")` — dot-separated keys plus
  `[index]` groups; a missing key / out-of-range index / malformed path
  reports `no_such_field` or `kind_mismatch` (never crashes). Both const
  and mutable overloads are provided;
- `rtti::node_as<T>(node)` — convert a located node to a C++ value through
  the converter layer (`result<T>`);
- `rtti::to_node(target, value)` — replace the target node's subtree from a
  C++ value through `from_value`, so the owning document serializes the new
  contents. `locate` is implemented in `src/reflect.cpp`; the two templates
  are thin shells over `convert.hpp`.

### `rtti::str` — full-type string conversion + string ops

`<rtti/string_ops.hpp>` bundles conversion and manipulation under
`namespace rtti::str`:

- conversion shells `to_string` / `from_string` are using-aliases of the
  unified `rtti::to_string` / `from_string` layer above, so scalars, enums,
  `date_time`, containers, optional/variant/tuple/pair/array and reflected
  structs all convert through the same implementation; `to_base` / `from_base`
  (and signed twins) bridge the arbitrary-base helpers (bases 2..64);
- string operations (non-template, in `src/string_ops.cpp`):
  `split` / `join`, `trim` / `ltrim` / `rtrim`, `to_lower` / `to_upper`,
  `starts_with` / `ends_with`, `replace` / `replace_all`, `repeat`,
  `pad_left` / `pad_right`, `reverse`, `quote` / `unquote`,
  `escape` / `unescape`;
- pure string transforms are `noexcept`; parsing (`unquote` / `unescape` /
  `from_base` / `from_string`) reports `error_code` and never crashes (spec
  §0 fail-safe).

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

## Variance casts

`rtti::as<T>` and `rtti::cast<T>` mirror C# reference conversions:

- `as<T>(x)` returns `T*` or `nullptr` when the conversion is not possible.
- `cast<T>(x)` returns `result<T*>`; a failed conversion sets
  `error_code::kind_mismatch` (a null source sets `no_such_field`).

They accept a raw pointer, an `object_ref<U>`, or a type-erased `instance`.
Covariant upcasts (derived to base) are compile-time correct, including
virtual bases and multiple inheritance, regardless of polymorphism.
Contravariant downcasts are runtime-checked and require a polymorphic
source; a non-polymorphic source fails cleanly. Const-qualified source or
target types are rejected at compile time.

For a type-erased `instance`, the runtime dynamic type resolves through the
reflected base chain (offset-correct). When the dynamic type's chain is
incomplete (its descriptor was never materialized) the conversion falls back
to the stored static type's chain, so legal upcasts to a stored static base
never fail spuriously; an unreflected dynamic type is reachable only as its
exact type. Because the erased source type is not a compile-time polymorphic
class type, `dynamic_cast` cannot be used on the `instance` path; such
unreachable conversions fail cleanly and never return a wrong pointer.

## Reflection traversal

The reflection core's registration tables are directly iterable. All three
visitors live in `<rtti/reflection.hpp>` and are header-only, allocation-free
and fail-safe.

### Enum traversal — `<rtti/reflection.hpp>`

An enum registered with `RTTI_DEFINE_ENUM` exposes its enumerator table in
registration order:

- `enum_count<E>()` — the number of enumerators;
- `enum_name_at<E>(i)` — the name of the `i`-th enumerator, or `nullptr`
  out of range;
- `enum_value_at<E>(i)` — the value of the `i`-th enumerator, or `E{}`
  out of range;
- `for_each_enum<E>(fn)` — calls `fn(const char* name, E value)` for every
  enumerator, in registration order.

```cpp
enum class Priority { low = 1, normal = 2, high = 3 };
RTTI_DEFINE_ENUM(Priority, low, normal, high)

std::string text;
rtti::for_each_enum<Priority>([&](const char* name, Priority v) {
    text += std::string(name) + "=" + std::to_string(int(v)) + ",";
});
// text == "low=1,normal=2,high=3,"
```

An unregistered enum has no `enum_table<E>` specialization, so these entry
points are a compile-time hard error for it — the same fail-fast convention
as `fields_of<T>`. There is no silent empty iteration for unregistered enums.

### Field traversal — `<rtti/reflection.hpp>`

- `for_each_field<T>(fn)` — calls `fn(const char* name, type_id type)` for
  every field of `T`, in table order (merged base-class fields first);
- `for_each_field(obj, fn)` — the same walk over a concrete object, calling
  `fn(const char* name, type_id type, const field_value& value)` so the
  visitor also receives each field's current value.

```cpp
struct Point { int x = 0; int y = 0; RTTI_DEFINE_TYPE_INTRUSIVE(Point, x, y) };

int sum = 0;
Point p{2, 3};
rtti::for_each_field(p, [&](const char*, rtti::type_id, const rtti::field_value& v) {
    if (v.is_integer()) {
        sum += static_cast<int>(v.as_integer());
    }
});
// sum == 5
```

As with `fields<T>()` / `field_count<T>()`, an unregistered type is a
compile-time error, never a silent empty walk.

### Template-argument traversal — `<rtti/reflection.hpp>`

`for_each_template_arg<T>(fn)` visits each registered template argument of
`T` in declaration order, calling `fn(std::size_t index,
const type_desc* arg_type)`.

```cpp
template <typename A, typename B> struct Box {};

RTTI_TEMPLATE_INFO(Box, int, std::string)

std::size_t n = 0;
rtti::for_each_template_arg<Box<int, std::string>>([&](std::size_t, const rtti::type_desc*) {
    ++n;
});
// n == 2
```

Unlike the enum and field visitors, an unregistered template (or a plain
non-template type) iterates zero times — `template_info<T>` falls back to a
safe empty table (count `0`), never a compile-time error.

## Format Adapters

JSON/XML/INI/TOML/FlatBuffers are optional build gates. When enabled the four
text adapters share the same shape — an owned DOM (`document : value_io`), a
`parse`/`serialize` pair, a typed bridge `parse_as<T>` / `serialize<T>(obj)`,
a streaming SAX `parse_stream` and a `write_config` (key naming / rename /
skip_empty). The protobuf and flatbuffers adapters (`<rtti/proto.hpp>`,
`<rtti/flat.hpp>`) are gated the same way but configure themselves through
`parse_config` (they have no `write_config`). The template bridge layer stays
header-only; the four text adapters and the `.proto` parser are
non-template implementations compiled once into `src/*.cpp`. The protobuf
wire codecs remain header-inline templates (`<rtti/proto.hpp>`); the
flatbuffers adapter is the exception — its wire codec and `.fbs` parser are
non-template bodies compiled from `src/flat.cpp` / `src/flat_schema.cpp`.
None of these adapters is ever needed by the reflect core.

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

### protobuf — `<rtti/proto.hpp>`, `<rtti/proto_schema.hpp>`

A full protobuf 2/3 adapter, official-`protoc`-fixture verified:

- wire codecs: varint (wire 0, zig-zag for `sint32`/`sint64`), fixed 64-bit
  (1), length-delimited (2), fixed 32-bit (5); bounded encode/decode,
  truncated input → `proto_truncated`;
- explicit `rtti::proto::schema<T>` maps each reflected field to a
  `field_desc` (protobuf field number, `wire_type`, `field_mode`
  optional/required/repeated, `packed`, proto2 `with_default_int/real/str`,
  `with_zigzag` for signed zig-zag varints);
- typed bridge `serialize<T>`/`parse_as<T>`: proto2 required-missing →
  `missing_key`, explicit defaults applied on absent optional fields, proto3
  scalar defaults omitted on the wire; repeated fields accept both packed
  and unpacked layout;
- **.proto text parser** `<rtti/proto_schema.hpp>`: `parse_schema` +
  `compile_schema` handle the full proto2/v3 grammar — scalars (all types
  incl. `sint`), `map<K,V>`, `oneof`, `import`, `reserved`, enum default
  values, byte escapes (`\xNN` / `\uXXXX`), nested messages and field
  number validation (`proto_bad_field` on conflicts/reuse);
- **extensions**: proto2 `extensions` ranges + `extend` declarations register
  into a runtime `rtti::proto::extension_registry` (`register_extension`);
  unknown wire field numbers are preserved / skipped, registered ones parse
  as named fields (official `ExtensionRegistry` semantics);
- tree bridge `wire_to_tree` / `tree_to_wire` glues any value_io tree to the
  wire; `parse` (schema-less or schema-aware) and `parse_stream` provide SAX
  events (`message_start` / `message_end` / typed field events);
- DoS guards: nesting `max_depth` and length-prefix bounds; field number 0
  and group wire types 3/4 are rejected with `proto_bad_field`, and a
  schema/stream wire mismatch yields `proto_type_mismatch` (never crashes).

### FlatBuffers — `<rtti/flat.hpp>`, `<rtti/flat_schema.hpp>`

A FlatBuffers wire-format adapter (spec §14), official-`flatc`-fixture
verified:

- **wire codec** (non-template, in `src/flat.cpp`): root uoffset, table vtable
  walk, scalars aligned 1/2/4/8 with schema defaults, strings, vectors,
  inline structs, unions (U8 type slot + paired variant table) and optional
  file identifier. Malformed/truncated/out-of-range input returns an
  `error_code` and never crashes;
- `rtti::flat::schema<T>` registration: each reflected field NAME maps to a
  vtable SLOT and wire `fkind` (scalars, `string`, `vector`, `table`, struct,
  union); absent vtable slots fill schema/universal defaults;
- typed bridge `flat::serialize<T>` / `flat::parse_as<T>` walk the converter
  reflection chain; `flat::wire_to_tree` / `tree_to_wire` glue any value_io
  tree;
- **.fbs text parser** `<rtti/flat_schema.hpp>`: `flat::parse_schema` +
  `compile_schema` compile a `.fbs` descriptor (namespace / `table` / `struct`
  / `enum` / `union` / `root_type` / `file_identifier` / attributes /
  defaults) into a runtime `schema_info` that drives the same codec chain as
  a `schema<T>` registration;
- DoS guards: `max_depth` caps nested tables (`too_deep`);
  `verify_file_identifier` checks the 4-byte magic.

## Architecture

Three cooperating layers, with a strict dependency direction — adapters only
ever `#include` the core, never the reverse.

| Layer | Headers | Responsibility |
| --- | --- | --- |
| Reflect core | `reflection.hpp` `reflect.hpp` `convert.hpp` `memory.hpp` `error.hpp` `rtti_fwd.hpp` `date_time.hpp` `base_convert.hpp` `to_string.hpp` `number_format.hpp` `string_ops.hpp` `node_io.hpp` `naming.hpp` `ioc.hpp` | field tables, macros, query API, recursive conversion, type judgement, attributes, reflective creation, iterators, date/time, base conversion, unified to_string + format specs, string ops, node-level locate/node_as/to_node, naming, IoC |
| `value_io` abstraction | `convert.hpp` | format-agnostic tree interface (`kind`, `at`, `get_key`, `make_scalar`, …) implemented by each adapter |
| Format adapters | `json.hpp` `xml.hpp` `ini.hpp` `toml.hpp` `proto.hpp` `proto_schema.hpp` `flat.hpp` `flat_schema.hpp` | JSON / XML / INI / TOML / protobuf / FlatBuffers DOM + serializers |

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
| `format_demo.cpp` | `date_time::format`, `format_number`, `to_string(v,spec)`, `node_to_string`, `format_reflected`, `rtti::str` string ops |
| `flatbuffers_demo.cpp` | `.fbs` text parse (`parse_schema`/`compile_schema`), `flat::schema<T>` typed bridge, wire parse/serialize |

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

Six independent feature gates are defined (default `1`) in
`<rtti/rtti_fwd.hpp>` (flatbuffers defaults through `<rtti/flat.hpp>`):

| Gate | Enables |
| --- | --- |
| `RTTI_ENABLE_JSON` | `<rtti/json.hpp>` |
| `RTTI_ENABLE_XML` | `<rtti/xml.hpp>` |
| `RTTI_ENABLE_INI` | `<rtti/ini.hpp>` |
| `RTTI_ENABLE_TOML` | `<rtti/toml.hpp>` |
| `RTTI_ENABLE_PROTO` | `<rtti/proto.hpp>` `<rtti/proto_schema.hpp>` |
| `RTTI_ENABLE_FLAT` | `<rtti/flat.hpp>` `<rtti/flat_schema.hpp>` — header strip macro `RTTI_ENABLE_FLATBUFFERS` (not driven by CMake, see caveat below) |

**FLAT gate caveat**: `RTTI_ENABLE_FLAT` only controls registration of the
flat tests (`flat_test.cpp`, the `rtti_flat_schema_tests` executable). The
`src/flat.cpp` / `src/flat_schema.cpp` wire codecs are always compiled into the
`rtti` static library and `<rtti/flat.hpp>` remains includable — the header
strip macro `RTTI_ENABLE_FLATBUFFERS` simply defaults to enabled and is never
set by CMake, unlike the other five gates where `OFF` makes the adapter header
`#error`. To fully strip FlatBuffers, configure CMake with
`-DRTTI_ENABLE_FLAT=0` and also pass `-DRTTI_ENABLE_FLATBUFFERS=0`.

The gates are CMake options (`-DRTTI_ENABLE_*=0/1`); the compile-time macros
are propagated to every target and default to `1` in the headers. The reflect
core has zero format-header dependencies, so it compiles with all six gates
off and the stripped build passes the full test suite (`strip_matrix_test`
verifies the core's zero format-header dependency at the source level).

The six gates form a 2^6 = 64-combination strip matrix; **every combination
is explicitly configured, built and run against the full test suite
independently** (each in its own scratch directory, with the `CMakeCache.txt`
`RTTI_ENABLE_*` values verified right after configure). Assertions grow
monotonically as gates are added. The 2026-10-08 full 64-combo sweep (all
combinations `0 failures`, zero warnings) ran from `32790` all-off to `55696`
all-on; the current tree (with the cast/as variance tests and the reflection
traversal APIs) measures `32872` all-off and `55847` all-on. The sweep
(`.opencode/batch_strip.ps1` + `.opencode/strip_matrix.ps1`)
grouped by gate count:

| Gates on | Combinations run | rtti_tests assertions span |
| --- | --- | --- |
| 0 | 1 | 32790 |
| 1 | 6 | 32949 – 38062 |
| 2 | 15 | 34192 – 43324 |
| 3 | 20 | 39392 – 48584 |
| 4 | 15 | 44631 – 54153 |
| 5 | 6 | 49903 – 55537 |
| 6 | 1 | 55696 |

(Intermediate spans are the 2026-10-08 sweep snapshot; only the all-off and
all-on endpoints have been re-measured since the variance-cast work.)

(In addition, the standalone `rtti_proto_extension_tests` / `rtti_flat_schema_tests`
executables add `160` / `408` assertions whenever the PROTO / FLAT gate is on;
they are built and run inside the sweep as well.)

## Portability

- **C++17 only** — no C++20 features are required.
- Compilers: MSVC 2019 16.4+ (with `/Zc:preprocessor`), GCC 11+ and
  Clang/LLVM 14+ — the floating-point `std::to_chars` / `std::from_chars`
  used for shortest round-trip float text require libstdc++ 11 and
  libc++ 14; earlier GCC/Clang lack them.
- No `__declspec`, no GNU extensions, no compiler-private builtins.
- `std::chrono::steady_clock` timers, RAII mechanics, standard containers only.
- The library is a header-driven design with a single `src/` static-library
  build: the reflect core compiles with all six format gates off and the
  stripped build passes the full test suite.

## Documentation

- [English](README.en.md) — this file.
- [中文](README.zh-CN.md).
- [Docs home](../docs/README.md) — index of all interfaces, design and
  benchmark documents (bilingual).

### Interface reference (API Reference, bilingual)

- [Reflection core `reflection.hpp`](./docs/interface/en/reflection.md) ·
  [中文](./docs/interface/zh/reflection.md) — `type_id` / `type_desc` /
  `instance` / `object_ref` / registration macros / enums / fields / template
  args / attributes / bases / methods / ctors / `as`/`cast`.
- [Value conversion `convert.hpp`](./docs/interface/en/convert.md) ·
  [中文](./docs/interface/zh/convert.md) — `field_value` / `value_io` /
  `to_value`/`from_value` (+ named) / `converter<T>` / date-time & base lanes /
  error codes.
- [Format adapters JSON/XML/INI/TOML](./docs/interface/en/formats.md) ·
  [中文](./docs/interface/zh/formats.md) — `parse`/`serialize`/
  `parse_as`/`serialize`/`parse_stream` per format, configs, semantics.
- [Protocols protobuf / FlatBuffers](./docs/interface/en/proto-flat.md) ·
  [中文](./docs/interface/zh/proto-flat.md) — `schema<T>` registration, wire
  codecs, tree bridge, extensions, `.fbs` schema reader.
- [Tools & support cluster](./docs/interface/en/utils.md) ·
  [中文](./docs/interface/zh/utils.md) — base_convert / date_time / naming /
  version / rtti_fwd; also reflect-tools (ORM / IoC / to_string / string_ops).

### Design

- [Design & architecture](./docs/design/en/architecture.md) ·
  [中文](./docs/design/zh/architecture.md) — layers, core mechanisms
  (registration model, type erasure, format gating, error model, memory
  strategy, number formatting), key decisions & rationale.

### Performance & benchmarks

- [Performance report (EN)](./docs/benchmarks/en/performance-report.md) ·
  [中文](./docs/benchmarks/zh/performance-report.md) — methodology,
  environment, verbatim numbers (Debug/Release × new/legacy benches),
  reproduction steps (`scripts/run_bench.ps1`).
- Raw outputs: `docs/benchmarks/2026-10-09-*.txt`; concise bilingual record:
  `docs/benchmarks/2026-10-09-msvc.md`.

Cross-referenced between editions; every section above has a matching section
in the Chinese edition.

## License

Licensed under the MIT License — see [LICENSE](LICENSE).

Edition cross-links: [README.md](README.md),
[README.zh-CN.md](README.zh-CN.md).