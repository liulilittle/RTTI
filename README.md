# RTTI — Runtime Type Information for C++17

<!-- CI badge: replace OWNER/REPO with the actual repository slug after the
     workflow is pushed. Keep this line above the paragraph below. -->
[![CI](https://github.com/liulilittle/RTTI/actions/workflows/ci.yml/badge.svg)](https://github.com/liulilittle/RTTI/actions/workflows/ci.yml)

A dependency-free, standard portable C++17 runtime-reflection library that
compensates for C++'s lack of first-class reflection: register your structs
with a single macro, then introspect fields (public, protected or private),
read and write them by name, recursively convert to/from a value tree, and
serialize through a format-agnostic `value_io` abstraction. Format adapters
(JSON / XML / INI / TOML / protobuf / FlatBuffers) mount on the same
conversion core as optional gates, and the library builds on MSVC / GCC /
Clang — including Android NDK arm64-v8a / armeabi-v7a targets.

## Documentation

| Edition | Language | Link |
| --- | --- | --- |
| English | English | [README.en.md](README.en.md) |
| 简体中文 | Chinese | [README.zh-CN.md](README.zh-CN.md) |
| Docs home | bilingual | [docs/README.md](docs/README.md) |

Both editions mirror the same section structure and cross-link each heading.

Full documentation lives under `docs/` (bilingual, mutually linked):

- **Interface reference** — `docs/interface/en|zh/`:
  `reflection.md` (core + macros + `as`/`cast`), `convert.md` (value model +
  `to_value`/`from_value`), `formats.md` (JSON/XML/INI/TOML), `proto-flat.md`
  (protobuf + FlatBuffers), `utils.md` (base_convert / date_time / naming /
  version / gates), `reflect-tools.md` (runtime query / ORM / IoC / to_string /
  string_ops).
- **Design & architecture** — `docs/design/en|zh/architecture.md`: layers,
  core mechanisms, key decisions & rationale.
- **Performance & benchmarks** — `docs/benchmarks/en|zh/performance-report.md`:
  methodology, environment, verbatim numbers (Debug / Release / LTCG, median
  of 7 samples), reproduction (`scripts/run_bench.ps1`); latest capture
  **2026-10-11**, raw outputs under `docs/benchmarks/2026-10-11-*.txt`.

## Layout

- `include/rtti/` — headers:
  - reflect core: `reflect.hpp` (public query API + type judgement),
    `reflection.hpp` (field tables + macro family + attributes +
    reflective instantiation), `convert.hpp` (`value_io` +
    `from_value`/`to_value`), `memory.hpp`, `error.hpp`, `rtti_fwd.hpp`
    (feature-gate defaults), plus the standalone core capabilities
    `date_time.hpp`, `base_convert.hpp`, `to_string.hpp`,
    `number_format.hpp` (format-number/date/reflected/node formatting),
    `string_ops.hpp` (`rtti::str`), `node_io.hpp` (locate/node_as/to_node),
    `naming.hpp`, `ioc.hpp`.
  - format adapters: `json.hpp`, `xml.hpp`, `ini.hpp`, `toml.hpp`,
    `proto.hpp` (+ `proto_schema.hpp`), `flat.hpp` (+ `flat_schema.hpp`)
    — each behind its own `RTTI_ENABLE_*` gate.
- `src/` — static-library sources.
- `tests/` — self-contained zero-dependency test harness.
- `bench/` — ops/s microbenchmarks.
- `examples/` — 9 documented demo programs (`reflection_demo`,
  `core_convert_demo`, `json_demo`, `xml_demo`, `ini_demo`, `toml_demo`,
  `proto_demo`, `format_demo`, `flatbuffers_demo`), each compilable
  standalone with MSVC `cl`.

Format adapters (JSON/XML/INI/TOML/protobuf/FlatBuffers) are optional build
gates. The reflect core compiles with all six gates off, and the stripped
build passes the full test suite.

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

### Value-semantics strong cast (`rtti::convert::as` / `cast` / `to`)

Distinct from the pointer casts above, `rtti::convert::as<T>(src)` /
`cast<T>(src)` / `to<T>(src)` are a numeric strong-cast bridge over the scalar
conversion lanes (<rtti/convert.hpp>, `namespace rtti::convert`):

- `as<T>(src)` returns `std::optional<T>` — `nullopt` on any failure;
- `cast<T>(src)` / `to<T>(src)` return `result<T>` with distinguishable
  `error_code`s (`kind_mismatch` for incompatible lanes, `overflow` for a
  numeric range violation, `invalid_number` for malformed numeric/label text,
  `no_such_field` for a null / null-kind source).

Sources cover native scalars (`bool` / integers / floating point), strings
(`string_view` / `std::string`), `field_value`, and `value_io`-derived
pointers (including `const`). Targets cover the full scalar family, registered
enums (via their labels / integer values) and single-layer
`std::vector<scalar>`. The bridge is deliberately **loose**: it converts values
rather than parsing literals, so `as<int>("3.9")` truncates the real lane to
`3` (contrast `from_string<int>("3.9")`, which reports `invalid_number`). See
`docs/interface/{en,zh}/convert.md`.

## Reflection traversal

The registration tables are directly iterable, with header-only,
allocation-free visitors in `<rtti/reflection.hpp>`:

- Enum traversal — `enum_count<E>()`, `enum_name_at<E>(i)`,
  `enum_value_at<E>(i)` and `for_each_enum<E>(fn)` walk a
  `RTTI_DEFINE_ENUM`-registered enum's enumerators in registration order.
  An unregistered enum is a compile-time hard error (fail-fast, like
  `fields_of<T>`), never a silent empty iteration.
- Field traversal — `for_each_field<T>(fn)` and `for_each_field(obj, fn)`
  walk every reflected field (merged base fields first); the object overload
  also delivers each field's current value. Unregistered types are
  compile-time errors.
- Template-argument traversal — `for_each_template_arg<T>(fn)` visits each
  `RTTI_TEMPLATE_INFO`-registered argument in declaration order; unregistered
  templates and non-template types iterate zero times (no hard error).

### Private / protected field reflection

The `RTTI_DEFINE_TYPE` macro family now captures each field's compile-time
access level (`rtti::member_access`: `public_` / `protected_` / `private_`,
stored on `field_type_desc::access` and queried via
`field_access_of<T>(index)`), and private / protected members are reflectable:

- **Non-intrusive** registration (`RTTI_DEFINE_TYPE` /
  `RTTI_DEFINE_TYPE_DERIVED`) may list private / protected fields — declare
  `RTTI_FRIEND_FIELDS(T)` (expanding to
  `friend struct ::rtti::fields_of<T>;`) inside the class body to grant the
  field table access. The access probe pair is evaluated at namespace scope
  (not from a friend), so a friend grant never leaks into the probe results.
- **Intrusive** registration (`RTTI_DEFINE_TYPE_INTRUSIVE`) reflects
  private / protected fields with no extra declaration; access level
  classification is impossible inside the class body, so every intrusive field
  conservatively reports `private_` (matching `field_type_desc::access`'s
  default).

Reflected private / protected fields read and write through the same APIs as
public ones (`get_field` / `set_field` / `fields<T>` accessors /
`for_each_field`), including through the merged derived table. See
`docs/interface/{en,zh}/reflection.md`.

```cpp
enum class Priority { low = 1, normal = 2, high = 3 };
RTTI_DEFINE_ENUM(Priority, low, normal, high)

rtti::for_each_enum<Priority>([](const char* name, Priority v) { /* ... */ });
// Point registered with RTTI_DEFINE_TYPE_INTRUSIVE(Point, x, y):
Point p{2, 3};
rtti::for_each_field(p, [](const char* n, rtti::type_id, const rtti::field_value& v) { /* ... */ });
// Box<int, std::string> registered with RTTI_TEMPLATE_INFO(Box, int, std::string):
rtti::for_each_template_arg<Box<int, std::string>>([](std::size_t i, const rtti::type_desc* t) { /* ... */ });
```

## License

Licensed under the MIT License — see [LICENSE](LICENSE).

Edition cross-links: [README.en.md](README.en.md),
[README.zh-CN.md](README.zh-CN.md).
