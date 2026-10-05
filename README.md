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

- `include/rtti/` — headers: `reflect.hpp` (public query API),
  `reflection.hpp` (field tables + `RTTI_DEFINE_TYPE` macro family),
  `convert.hpp` (`value_io` + `from_value`/`to_value`), `memory.hpp`,
  `error.hpp`, `rtti_fwd.hpp`, plus format gate stubs `json.hpp`/`xml.hpp`/`ini.hpp`.
- `src/` — static-library sources.
- `tests/` — self-contained zero-dependency test harness.
- `bench/` — ops/s microbenchmarks.

Format adapters (JSON/XML/INI) are deliberately deferred to a later phase;
their gate stubs keep the interface stable. The reflect core compiles with all
format gates off.