# RTTI — Runtime Type Information for C++17

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
    `date_time.hpp`, `base_convert.hpp`, `to_string.hpp`, `naming.hpp`,
    `ioc.hpp`.
  - format adapters: `json.hpp`, `xml.hpp`, `ini.hpp`, `toml.hpp`,
    `proto.hpp` — each behind its own `RTTI_ENABLE_*` gate.
- `src/` — static-library sources.
- `tests/` — self-contained zero-dependency test harness.
- `bench/` — ops/s microbenchmarks.
- `examples/` — 7 documented demo programs (`reflection_demo`,
  `core_convert_demo`, `json_demo`, `xml_demo`, `ini_demo`, `toml_demo`,
  `proto_demo`), each compilable standalone with MSVC `cl`.

Format adapters (JSON/XML/INI/TOML/protobuf) are optional build gates. The
reflect core compiles with all five gates off, and the stripped build passes
the full test suite.
