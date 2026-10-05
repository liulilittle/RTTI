# RTTI — Runtime Type Information for C++17

[简体中文](README.zh-CN.md) | 中文版见 [README.zh-CN.md](README.zh-CN.md)

RTTI is a dependency-free, portable (MSVC / GCC / Clang), C++17 runtime
reflection library that compensates for C++'s lack of first-class reflection.
What used to require tedious boilerplate — walking fields, (de)serializing,
introspecting at runtime — is now one macro away.

## Table of Contents

- [Introduction](#introduction)
- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Supported Containers](#supported-containers)
- [Portability](#portability)
- [Documentation](#documentation)

## Introduction

Standard C++11 `typeid` gives you only a mangled name. RTTI builds on it a
practical field-reflection layer:

- **Register** structs with the `RTTI_DEFINE_TYPE` macro family (external,
  intrusive, with-default variants) and enums with `RTTI_DEFINE_ENUM`.
- **Introspect** at runtime: field count, names, types (`.NET`-grade
  `rtti::get_type`, `find_field`, `get_field`, `set_field`, range-for over
  fields).
- **Convert** recursively between your structs and a format-agnostic
  `value_io` tree (`from_value` / `to_value`), covering scalars, `std::string`,
  and `std::map`/`unordered_map`/`set`/`unordered_set`/`list`/`vector` —
  allocator- and comparator-transparent.
- **Decouple** formats: the reflect core never includes a format header;
  JSON/XML/INI adapters plug in later and stay optional build gates.

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
keeps the existing member value instead.

## Architecture

Three cooperating layers, with a strict dependency direction — adapters only
ever `#include` the core, never the reverse.

| Layer | Headers | Responsibility |
| --- | --- | --- |
| Reflect core | `reflection.hpp` `reflect.hpp` `convert.hpp` `memory.hpp` `error.hpp` `rtti_fwd.hpp` | field tables, macros, query API, recursive conversion, arenas, result types |
| `value_io` abstraction | `convert.hpp` | format-agnostic tree interface (`kind`, `at`, `get_key`, `make_scalar`, …) implemented by each adapter |
| Format adapters | `json.hpp` `xml.hpp` `ini.hpp` (gate stubs) | JSON/XML/INI (deferred to a later phase) |

`fields_of<T>` holds the compile-time field table (count + array of typed
accessors). The `RTTI_DEFINE_TYPE` macro specializes it; intrusive variants
register a nested `rtti_registration` that `fields_of` inherits. The runtime
query API and the recursive conversion walk exactly this table, so registration
is the single source of truth.

The `RTTI_IS_EMPTY` empty-argument detection and bounded `RTTI_NARG` counting
use a two-level indirect paste that stays correct under MSVC's conformant
preprocessor (`/Zc:preprocessor`).

## Supported Containers

| Family | Recognized by | Notes |
| --- | --- | --- |
| `std::vector<T, A>` | `push_back`, `data`, sequence | allocator `A` flows through untouched |
| `std::list<T, A>` | `push_back`, sequence | |
| `std::map<K,V,C,A>` / `std::unordered_map<...>` | `mapped_type` | |
| `std::set<T,C,A>` / `std::unordered_set<...>` | associative, no `mapped_type` | |

`std::string` is explicitly excluded from sequence handling (it has
`push_back` and iterators) and routes to the dedicated string converter.
Custom allocators and comparators are carried as template parameters with no
extra plumbing. Any type outside these families is dispatched in this order:
user ADL hook → `is_reflected` (walk its fields) → scalar fallback.

## Portability

- **C++17 only** — no C++20 features are required.
- Compilers: MSVC (with `/Zc:preprocessor`) and GCC/Clang.
- No `__declspec`, no GNU extensions, no compiler-private builtins.
- `std::chrono::steady_clock` timers, RAII mechanics, standard containers only.
- The reflect core compiles with all three format gates off
  (`-DRTTI_ENABLE_* = OFF`); the stripped build passes the full test suite.

## Documentation

- [English](README.en.md) — this file.
- [简体中文](README.zh-CN.md).

Cross-referenced between editions; every section above has a matching section
in the Chinese edition.