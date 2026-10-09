# RTTI — Runtime Type Information for C++17

<!-- CI badge: replace OWNER/REPO with the actual repository slug after the
     workflow is pushed. Keep this line above the paragraph below. -->
[![CI](https://github.com/OWNER/REPO/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/ci.yml)

A dependency-free, standard portable C++17 runtime-reflection library that
compensates for C++'s lack of first-class reflection: register your structs
with a single macro, then introspect fields, read and write them by name,
recursively convert to/from a value tree, and serialize through a
format-agnostic `value_io` abstraction.

## Documentation

| Edition | Language | Link |
| --- | --- | --- |
| English | English | [README.en.md](README.en.md) |
| 简体中文 | Chinese | [README.zh-CN.md](README.zh-CN.md) |

Both editions mirror the same section structure and cross-link each heading.

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

## License

Licensed under the MIT License — see [LICENSE](LICENSE).

Edition cross-links: [README.en.md](README.en.md),
[README.zh-CN.md](README.zh-CN.md).
