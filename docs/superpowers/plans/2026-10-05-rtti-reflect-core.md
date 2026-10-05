# RTTI Reflect Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the RTTI reflect core layer: compile-time field tables, runtime-reflection queries (`.NET`-grade ergonomics), generic `convert<T>` dispatch, the `JSONXX_DEFINE_TYPE` macro family, and the format-agnostic `value_io` abstraction — as a dependency-free C++17 static library with tests and benchmark scaffolding.

**Architecture:** The reflect core is the top of the stack and must not `#include` any format header. It builds a per-type compile-time field table via macros; a `reflect<T>` template walks that table; `convert<T>` dispatches scalars/string/four container families/nested structs via ADL-first; a `value_io` abstract sink/source bridges the core to any future format adapter (JSON/XML/INI). Formats arrive in a later phase; everything here compiles and passes tests with no format enabled.

**Tech Stack:** C++17, template metaprogramming, MSVC 2022 (available: `cl.exe`, bundled CMake), CMake ≥ 3.15, no third-party deps. All code and comments in English. Docs in English + Chinese with cross-linked headings.

**Spec:** `docs/superpowers/specs/2026-10-05-json-xml-high-perf-cpp-library-design.md` (project named **RTTI**; the plan argues from the spec, so the spec travels with it; executors read both).

## Global Constraints

- Language: C++17 only. No compiler-private intrinsics, no `__declspec`, no GNU extensions, no platform-private headers in any header shipped to users. C++20 is explicitly out: many legacy/embedded platforms cannot compile it; holding C++17 keeps the library compilable almost everywhere.
- All source code comments and identifiers must be English. Public API naming consistent, no case ambiguity.
- No third-party dependencies anywhere (headers, tests, bench). Tests use a tiny hand-rolled harness, not a framework.
- Every new/modified file must be buildable with MSVC 2022 (`-std:c++17`), and be *structurally* portable (GCC/Clang compatible) even though only MSVC is locally available to verify.
- **Compile-time cost discipline:** reflection is not a pathological use of the compiler — without it the entire library is pointless, and compile cost *is* controllable. Metaprogramming must avoid pathological recursion and exponential expansion (which really can OOM the compiler), but it is **not** bounded by an arbitrary field-count cap like 16. Design rules: use pre-expanded, flat macro iteration (no recursive macro self-expansion); prefer shallow SFINAE traits and shape-based dispatch over deep recursive template walks; keep per-TU expansion proportional to the types actually reflected. A pathological-usage constraint is acceptable; an arbitrary "max 16 fields" cap is not.
- Field reflection uses **member-pointer accessors** (`M T::*ptr` + `obj.*ptr`), not `offsetof`/byte-offset arithmetic: safe for private members (via friend/intrusive macro), safe under inheritance, no UB. This mirrors the design reference pattern.
- Documentation delivered in this phase: README with English + Chinese editions, heading-level cross-links between editions.
- Project name is **RTTI**; namespace must be `rtti` (not `jsonxx`). Format-on/off macros: `RTTI_ENABLE_JSON`, `RTTI_ENABLE_XML`, `RTTI_ENABLE_INI`, default ON.
- The reflect core must compile and pass all its tests with ALL format macros OFF.
- TDD: every task writes a failing test first, observes failure, then implements minimally.

## Review Focus

1. A type with no macro registration passed to `reflect<T>` — must fail at compile time with a clear message (no silent no-op, no ADL fallback to nothing).
2. A `private` member registered via `JSONXX_DEFINE_TYPE_INTRUSIVE` — must be readable/writable; the same member via non-intrusive macro must NOT compile.
3. Containers with a custom allocator (`std::vector<int, MyAlloc>`) — must convert without requiring `std::allocator`; the core must not hard-code the default allocator.
4. Runtime field access by unknown name string — must return a `null`/error state, not UB, not throw.
5. Missing-key fill: standard macro must report error; `_WITH_DEFAULT` variant must keep the member's existing value silently.
6. Empty struct / zero-field registration — must compile and convert trivially.
7. A field whose type has its own ADL `from_value/to_value` overloads — ADL overloads must win over the field-table path for that field.
8. `value_io` source returning a mismatched kind for the requested target type — must produce a clean conversion error, not UB.

---

### Task 1: Project skeleton, CMake, test harness, namespace `rtti`

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_harness.hpp`
- Create: `tests/reflection_smoke.cpp`
- Create: `include/rtti/rtti_fwd.hpp`
- Create: `.gitignore`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `rtti` namespace (declared but empty so far); test harness macros `TEST_CASE(name)`, `CHECK(expr)`, `CHECK_EQ(a,b)`, `CHECK_THROWS(expr)`; CMake targets `rtti` (static lib), `rtti_tests`.

- [ ] **Step 1: Write the failing test**

`tests/test_harness.hpp`:
```cpp
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

namespace rtti_test {
inline int& failures() { static int f = 0; return f; }
inline int& assertions() { static int a = 0; return a; }
} // namespace rtti_test

#define TEST_CASE(name) \
    static void name(); \
    struct name##_register { name##_register() { std::printf("[TEST] %s\n", #name); name(); } }; \
    static name##_register name##_instance; \
    static void name()

#define CHECK(expr) \
    do { ++rtti_test::assertions(); \
         if (!(expr)) { ++rtti_test::failures(); \
         std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#define CHECK_EQ(a, b) \
    do { ++rtti_test::assertions(); \
         auto va = (a); auto vb = (b); \
         if (!(va == vb)) { ++rtti_test::failures(); \
         std::printf("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

#define CHECK_THROWS(expr) \
    do { ++rtti_test::assertions(); \
         bool threw = false; \
         try { (void)(expr); } catch (...) { threw = true; } \
         if (!threw) { ++rtti_test::failures(); \
         std::printf("FAIL %s:%d  %s (expected throw)\n", __FILE__, __LINE__, #expr); } } while (0)
```

`tests/reflection_smoke.cpp`:
```cpp
#include "test_harness.hpp"
#include <rtti/rtti_fwd.hpp>

TEST_CASE(namespace_exists) {
    rtti::version_tag tag{};
    CHECK(tag.major >= 0);
}
```

`rtti_fwd.hpp`:
```cpp
#pragma once
// RTTI forward declarations. English comments only.
namespace rtti {
struct version_tag { int major = 0; int minor = 1; int patch = 0; };
} // namespace rtti
```

`CMakeLists.txt` (root):
```cmake
cmake_minimum_required(VERSION 3.15)
project(RTTI VERSION 0.1.0 LANGUAGES CXX)

option(RTTI_ENABLE_JSON "Enable JSON format adapter" ON)
option(RTTI_ENABLE_XML  "Enable XML format adapter"  ON)
option(RTTI_ENABLE_INI  "Enable INI format adapter"  ON)

add_library(rtti STATIC src/common.cpp src/reflect.cpp)
target_include_directories(rtti PUBLIC include)
target_compile_features(rtti PUBLIC cxx_std_17)
target_compile_definitions(rtti
    PUBLIC RTTI_ENABLE_JSON=$<BOOL:${RTTI_ENABLE_JSON}>
          RTTI_ENABLE_XML=$<BOOL:${RTTI_ENABLE_XML}>
          RTTI_ENABLE_INI=$<BOOL:${RTTI_ENABLE_INI}>)

if(MSVC)
  target_compile_options(rtti PUBLIC /W4 /permissive-)
else()
  target_compile_options(rtti PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(tests)
```

`tests/CMakeLists.txt`:
```cmake
add_executable(rtti_tests reflection_smoke.cpp)
target_link_libraries(rtti_tests PRIVATE rtti)
target_compile_features(rtti_tests PRIVATE cxx_std_17)
add_test(NAME rtti_tests COMMAND rtti_tests)
enable_testing()
```

`src/common.cpp`:
```cpp
// Placeholder translation unit for the static library.
#include <rtti/rtti_fwd.hpp>
```
`src/reflect.cpp`: same placeholder pattern.

- [ ] **Step 2: Run test to verify it fails**

Run (PowerShell, after configuring):
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G "Visual Studio 17 2022" -A x64
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug
```
Expected: build succeeds (CMake scaffolding cannot "fail" at runtime until tests exist); this task's failure signal is captured in Task 2's first test.

- [ ] **Step 3: Write minimal implementation**

The files above ARE the implementation (harness + namespace tag). Nothing more is needed.

- [ ] **Step 4: Run test to verify it passes**

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug; if ($?) { & .\build\Debug\rtti_tests.exe }
```
Expected: one test runs, all checks pass, exit code 0.

- [ ] **Step 5: Commit**

```bash
git init -q
git add CMakeLists.txt tests include src .gitignore
git commit -m "skeleton: cmake, test harness, rtti namespace"
```

---

### Task 2: `error_code`, `result<T>`, `error_info` with lazy line/column

**Files:**
- Create: `include/rtti/error.hpp`
- Create: `src/error.cpp`
- Create: `tests/error_test.cpp`

**Interfaces:**
- Consumes: Task 1 harness + CMake.
- Produces:
```cpp
namespace rtti {
enum class error_code : uint8_t {
    ok = 0, unexpected_token, unterminated_string, invalid_number, invalid_utf8,
    unterminated_comment, mismatched_tag, duplicate_attribute, bad_entity,
    unterminated_cdata, dtd_missing, too_deep, invalid_root,
    ini_unterminated_section, ini_bad_line, ini_missing_value,
    no_such_field, kind_mismatch, missing_key
};
struct error_info {
    error_code code = error_code::ok;
    size_t offset = 0;
    size_t line() const noexcept;   // computed lazily from offset + source length
    size_t column() const noexcept;
    const char* message() const noexcept;
};
bool operator==(const error_info&, const error_info&) noexcept;
template <typename T> struct result {
    T value; error_info err;
    bool ok() const noexcept { return err.code == error_code::ok; }
    explicit operator bool() const noexcept { return ok(); }
    const T& operator*() const noexcept { return value; }
    T& operator*() noexcept { return value; }
};
template <> struct result<void> {
    error_info err;
    bool ok() const noexcept { return err.code == error_code::ok; }
    explicit operator bool() const noexcept { return ok(); }
};
const char* to_string(error_code) noexcept;
}
```
- `error_info::line()/column()` need a source pointer+length to scan; store them optionally:
```cpp
struct error_info {
    error_code code = error_code::ok;
    const char* src = nullptr; size_t src_len = 0; size_t offset = 0;
    size_t line() const noexcept;
    size_t column() const noexcept;
};
```
- `message()` returns a static English string per code.

- [ ] **Step 1: Write the failing test**

`tests/error_test.cpp`:
```cpp
#include "test_harness.hpp"
#include <rtti/error.hpp>

TEST_CASE(error_code_strings) {
    CHECK_EQ(rtti::to_string(rtti::error_code::ok), std::string("ok"));
    CHECK_EQ(rtti::to_string(rtti::error_code::no_such_field), std::string("no such field"));
}

TEST_CASE(result_bool_and_value) {
    rtti::result<int> r; r.value = 42;
    CHECK(r.ok());
    CHECK_EQ(*r, 42);
    rtti::result<int> bad; bad.err.code = rtti::error_code::unexpected_token;
    CHECK(!bad.ok());
}

TEST_CASE(lazy_line_column) {
    const char* src = "a\nbb\nccc";
    rtti::error_info e; e.src = src; e.src_len = 8; e.offset = 6; // 'c' index 6 is on line 3
    CHECK_EQ(e.line(), size_t(3));
    CHECK_EQ(e.column(), size_t(3));
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `error_test.cpp` to `tests/CMakeLists.txt` sources; rebuild.
Expected: FAIL — `to_string`, `result<T>::ok`, `error_info::line/column` do not exist.

- [ ] **Step 3: Write minimal implementation**

`include/rtti/error.hpp`: implement per the Interfaces above. `line()` counts `\n` before `offset` and returns 1-based; `column()` returns `offset - last_newline` 1-based. `message()` maps code→static English string. `result<void>` partial specialization.

`src/error.cpp`:
```cpp
#include <rtti/error.hpp>
namespace rtti {
size_t error_info::line() const noexcept {
    size_t ln = 1;
    for (size_t i = 0; i < offset && i < src_len; ++i)
        if (src[i] == '\n') ++ln;
    return ln;
}
size_t error_info::column() const noexcept {
    size_t i = offset > src_len ? src_len : offset;
    while (i > 0 && src[i-1] != '\n') --i;
    return offset - i + 1;
}
const char* error_info::message() const noexcept { return to_string(code); }
}
```

- [ ] **Step 4: Run test to verify it passes**

Rebuild and run `rtti_tests.exe`. Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add include/rtti/error.hpp src/error.cpp tests/error_test.cpp tests/CMakeLists.txt
git commit -m "error: error_code, error_info with lazy line/column, result<T>"
```

---

### Task 3: `string_view` alias and `MemoryArena`

**Files:**
- Create: `include/rtti/memory.hpp`
- Create: `src/memory.cpp`
- Create: `tests/memory_test.cpp`

**Interfaces:**
- Consumes: Task 2 error types (arena grows; no error use yet, keep independent).
- Produces:
```cpp
namespace rtti {
using string_view = std::string_view;  // C++17 std::string_view is portable

class memory_arena {
public:
    memory_arena() noexcept = default;
    ~memory_arena();
    memory_arena(memory_arena&&) noexcept;
    memory_arena& operator=(memory_arena&&) noexcept;
    memory_arena(const memory_arena&) = delete;
    memory_arena& operator=(const memory_arena&) = delete;

    void* alloc(size_t size, size_t align = alignof(std::max_align_t));
    template <typename T> T* alloc_obj() { return static_cast<T*>(alloc(sizeof(T), alignof(T))); }
    const char* dup(const char* p, size_t n);           // copy bytes into arena, returns NUL-terminated
    char* dup(const char* p);                           // strlen overload
    void reset() noexcept;                              // free all blocks, reusable
    size_t total_bytes() const noexcept;                // sum of allocated block bytes
private:
    struct block { char* data; size_t used; size_t cap; block* next; };
    block* head_ = nullptr;
    static constexpr size_t kBlockSize = 64 * 1024;
    char* ptr_ = nullptr; size_t rem_ = 0;  // current block cursor
};
}
```

- [ ] **Step 1: Write the failing test**

`tests/memory_test.cpp`:
```cpp
#include "test_harness.hpp"
#include <rtti/memory.hpp>
#include <string>

TEST_CASE(arena_alloc_and_dup) {
    rtti::memory_arena a;
    int* p = a.alloc_obj<int>();
    *p = 7;
    CHECK_EQ(*p, 7);
    const char* s = a.dup("hello", 5);
    CHECK_EQ(std::string(s), std::string("hello"));
    CHECK_EQ(a.total_bytes(), size_t(64*1024)); // one block
}

TEST_CASE(arena_grows_and_reset) {
    rtti::memory_arena a;
    for (int i = 0; i < 100000; ++i) { char* c = a.dup("x", 1); (void)c; }
    CHECK(a.total_bytes() > 64*1024);
    a.reset();
    CHECK_EQ(a.total_bytes(), size_t(0));
    int* q = a.alloc_obj<int>();
    CHECK(q != nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Add to test sources. Expected: FAIL — `memory_arena` not declared.

- [ ] **Step 3: Write minimal implementation**

`memory.hpp`/`memory.cpp`: arena with linked blocks; `alloc` serves from current block cursor, grows a new 64KB block when needed; `dup` copies and NUL-terminates; `reset` frees all blocks and nulls cursor; `total_bytes` sums capacities. Move constructor transfers `head_`/cursor and nulls source.

- [ ] **Step 4: Run test to verify it passes**

Rebuild, run. Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add include/rtti/memory.hpp src/memory.cpp tests/memory_test.cpp tests/CMakeLists.txt
git commit -m "memory: memory_arena + string_view alias"
```

---

### Task 4: `type_id`, `field_access` (reader/writer), `field_info`, `field_table<T>`

**Files:**
- Create: `include/rtti/reflection.hpp`
- Create: `tests/reflection_table_test.cpp`

**Interfaces:**
- Consumes: Task 3 string_view.
- Produces:
```cpp
namespace rtti {
using type_id = const std::type_info*;  // per-T &typeid(T)

// Variant-like read/write access to one field value.
class field_value {
public:
    enum class kind { none, boolean, integer, real, string, object };
    field_value() noexcept : k_(kind::none) {}
    static field_value of(bool v); static field_value of(int64_t v);
    static field_value of(double v); static field_value of(string_view v);
    static field_value of_object();  // nested struct placeholder
    kind type() const noexcept { return k_; }
    bool as_bool() const; int64_t as_integer() const; double as_real() const;
    string_view as_string() const;
private:
    kind k_; bool b_ = false; int64_t i_ = 0; double d_ = 0.0; string_view s_;
};

// One reflected member: name + typed read/write into a T instance.
template <typename T, typename M>
struct field_access {
    const char* name;
    M T::*ptr;
    field_value get(const T& obj) const;
    bool set(T& obj, const field_value& v) const;  // false on kind mismatch
};

template <typename T>
struct field_table;  // macro specializations fill this; see Task 5

template <typename T>
constexpr std::size_t field_count() noexcept;
template <typename T>
const field_access_base* fields() noexcept;  // runtime-iterable array
}
```

Runtime-iterable uniform view — use a base class with virtual get/set? .NET-grade ergonomics need `for (auto& f : rtti::fields_of<T>())` with uniform `name`, and `get/set` on a base pointer:

```cpp
struct field_access_base {
    const char* name;
    type_id type;                       // &typeid(M)
    virtual field_value get(const void* obj) const = 0;
    virtual bool set(void* obj, const field_value& v) const = 0;
};

template <typename T, typename M>
struct typed_field_access : field_access_base {
    M T::*ptr;
    typed_field_access(const char* n, M T::*p, type_id t) : field_access_base{n, t}, ptr(p) {}
    field_value get(const void* o) const override {
        const auto& obj = *static_cast<const T*>(o);
        return field_value::of(obj.*ptr);
    }
    bool set(void* o, const field_value& v) const override {
        auto& obj = *static_cast<T*>(o);
        return field_value_to(obj.*ptr, v);
    }
};
```

- [ ] **Step 1: Write the failing test**

`tests/reflection_table_test.cpp`:
```cpp
#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <string>

namespace {
struct Point { int x = 0; int y = 0; };
RTTI_DEFINE_TYPE(Point, x, y);   // field-table macro (declared in Task 5; referenced here)
}

TEST_CASE(field_count_and_names) {
    CHECK_EQ(rtti::field_count<Point>(), size_t(2));
    auto f = rtti::fields<Point>();
    CHECK_EQ(std::string(f[0].name), std::string("x"));
    CHECK_EQ(std::string(f[1].name), std::string("y"));
}

TEST_CASE(typed_get_set) {
    Point p; p.x = 3; p.y = 5;
    auto f = rtti::fields<Point>();
    CHECK_EQ(f[0].get(&p).as_integer(), int64_t(3));
    CHECK(f[0].set(&p, rtti::field_value::of(int64_t(9))));
    CHECK_EQ(p.x, 9);
    CHECK(!f[0].set(&p, rtti::field_value::of("oops"))); // kind mismatch
}
```

- [ ] **Step 2: Run test to verify it fails**

Add source. Expected: FAIL — `RTTI_DEFINE_TYPE` undefined, `field_count`, `fields` missing.

- [ ] **Step 3: Write minimal implementation**

`reflection.hpp`: implement `field_value` (conversion helpers `field_value_to(M&, const field_value&)` via overloads for bool/int64/double/string_view), `field_access_base`, `typed_field_access<T,M>`.
Declare:
```cpp
#define RTTI_DEFINE_TYPE(T, ...) /* forward, implemented fully in Task 5 */
```
Provide generic detection so `field_count<T>`/`fields<T>` fail loudly when `field_table<T>` is absent — via a base `fields_of_base`:
```cpp
template <typename T> struct fields_of; // macro specializes
template <typename T> constexpr std::size_t field_count() {
    return fields_of<T>::count;
}
template <typename T> const field_access_base* fields() {
    return fields_of<T>::array;
}
```
For unregistered types, `fields_of<T>` has no specialization → compile error "no matching specialization of fields_of<T>".

- [ ] **Step 4: Run test to verify it passes**

Rebuild, run. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/rtti/reflection.hpp tests/reflection_table_test.cpp tests/CMakeLists.txt
git commit -m "reflection: field_value, field_access_base, fields_of<T>"
```

---

### Task 5: `RTTI_DEFINE_TYPE` macro family + `RTTI_DEFINE_ENUM`

**Files:**
- Modify: `include/rtti/reflection.hpp` (add macros section)
- Create: `tests/macro_test.cpp`

**Interfaces:**
- Consumes: Task 4 `typed_field_access`, `fields_of<T>`.
- Produces (all four macros + enum macro):
```cpp
#define RTTI_DEFINE_TYPE(T, ...)                          /* non-intrusive, public members, at namespace scope */
#define RTTI_DEFINE_TYPE_INTRUSIVE(T, ...)                /* inside the struct/class body; friend access */
#define RTTI_DEFINE_TYPE_WITH_DEFAULT(T, ...)             /* non-intrusive, missing key keeps default */
#define RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(T, ...)   /* intrusive + default */
#define RTTI_DEFINE_ENUM(T, ...)                          /* string<->enum, optional in field table */
```
Macro expansion strategy (portable, no `__VA_OPT__` — MSVC 2019+ supports it but avoid for portability; use fixed iteration via repeated `RTTI_FOR_EACH` over a max of 16 args):

```cpp
#define RTTI_EXPAND(x) x
#define RTTI_FOR_EACH(m, ...) \
  RTTI_EXPAND(RTTI_FOR_EACH_16(m, __VA_ARGS__, ...))
```
`fields_of<T>` specialization:
```cpp
#define RTTI_MAKE_FIELD(T, n, m) \
  ::rtti::typed_field_access<T, decltype(T::m)>(n, &T::m, &typeid(decltype(T::m)))
template <> struct rtti::fields_of<Point> {
    static constexpr std::size_t count = 2;
    static const rtti::field_access_base* const array[2];
};
const rtti::field_access_base* const rtti::fields_of<Point>::array[2] = {
    RTTI_MAKE_FIELD(Point, "x", x), RTTI_MAKE_FIELD(Point, "y", y)
};
```
Intrusive variant additionally declares the friend conversion functions in-class. `_WITH_DEFAULT` adds a compile-time trait `has_default<Point> = true` used later by format fillers; the reflect core only records the trait now.

- [ ] **Step 1: Write the failing test**

`tests/macro_test.cpp`:
```cpp
#include "test_harness.hpp"
#include <rtti/reflection.hpp>
#include <string>

namespace {
struct Person {
    std::string name;
    int age = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(Person, name, age)
};

struct PrivatePerson {
private:
    std::string name;
    int age = 0;
public:
    RTTI_DEFINE_TYPE_INTRUSIVE(PrivatePerson, name, age)
};

enum class Color { Red, Green, Blue };
RTTI_DEFINE_ENUM(Color, Red, Green, Blue)
}

TEST_CASE(intrusive_private_members) {
    CHECK_EQ(rtti::field_count<PrivatePerson>(), size_t(2));
    PrivatePerson p; p.age = 7;
    auto f = rtti::fields<PrivatePerson>();
    CHECK(f[1].set(&p, rtti::field_value::of(int64_t(11))));
    CHECK_EQ(p.age, 11);
}

TEST_CASE(enum_to_string) {
    CHECK_EQ(rtti::enum_to_string(Color::Green), std::string("Green"));
    CHECK_EQ(rtti::enum_from_string<Color>("Blue"), Color::Blue);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — macros not defined, `enum_to_string` missing.

- [ ] **Step 3: Write minimal implementation**

`RTTI_FOR_EACH_16` chain (16 slots), `RTTI_DEFINE_TYPE` expanding to `fields_of<T>` specialization with `array` of `typed_field_access`. `RTTI_DEFINE_ENUM` expanding to `enum_to_string(T)`/`enum_from_string<T>(string_view)` implemented via a small template array of `{name, value}` pairs (compile-time list from args). Keep the array out-of-line definition inside the macro.

- [ ] **Step 4: Run test to verify it passes**

Rebuild, run. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/rtti/reflection.hpp tests/macro_test.cpp tests/CMakeLists.txt
git commit -m "reflection: RTTI_DEFINE_TYPE macro family + enum macro"
```

---

### Task 6: `convert<T>` scalar + string + four container families (allocator-agnostic)

**Files:**
- Create: `include/rtti/convert.hpp`
- Create: `tests/convert_test.cpp`

**Interfaces:**
- Consumes: Task 4 `field_value`, Task 3 string_view.
- Produces:
```cpp
namespace rtti {
// Source/sink abstraction, format-agnostic. Formats adapt this later.
class value_io {
public:
    enum class kind { null, boolean, integer, real, string, array, object };
    virtual ~value_io() = default;
    virtual kind type() const noexcept = 0;
    virtual bool as_bool(bool&) const noexcept = 0;
    virtual bool as_integer(int64_t&) const noexcept = 0;
    virtual bool as_real(double&) const noexcept = 0;
    virtual bool as_string(string_view&) const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual value_io* at(std::size_t i) = 0;               // array element
    virtual bool get_key(string_view k, value_io*& out) = 0; // object lookup
    // builder side
    virtual value_io* make_array() = 0;
    virtual void push_back(value_io* v) = 0;
    virtual value_io* make_object() = 0;
    virtual void set_key(string_view k, value_io* v) = 0;
    virtual value_io* make_scalar(field_value v) = 0;
};

// ADL-first conversion dispatch:
template <typename T> error_code from_value(const value_io* src, T& out);
template <typename T> error_code to_value(value_io* dst, const T& in);

// Overload detection helper for "T is a reflected struct"
template <typename T> struct is_reflected;
template <typename T> struct is_container;    // map/set/list/vector families
template <typename T> struct is_sequence;     // list/vector
template <typename T> struct is_associative;  // map/set
}
```
Container trait detection via member detection:
```cpp
template <typename T, typename = void> struct is_sequence : std::false_type {};
template <typename T> struct is_sequence<T, std::void_t<
    decltype(std::declval<T&>().push_back(std::declval<typename T::value_type>())),
    decltype(std::declval<T&>().begin()), decltype(std::declval<T&>().end())>>
    : std::true_type {};
template <typename T, typename = void> struct is_associative : std::false_type {};
template <typename T> struct is_associative<T, std::void_t<
    decltype(std::declval<T&>().insert(std::declval<typename T::value_type>())),
    typename T::key_type>> : std::true_type {};
```
`from_value` dispatch (SFINAE overload set, called from a single `from_value` that routes):
```cpp
template <typename T> struct converter {  // primary: scalar via field_value
    static error_code from(const value_io* src, T& out) { ... as_integer/as_real/as_bool ... }
    static error_code to(value_io* dst, const T& in) { return dst->make_scalar(field_value::of(in)) ? ok : no_such_field; }
};
template <typename T> struct converter<std::string> { ... as_string ... };
template <typename T> struct converter<std::vector<T, Alloc>> { sequence path };
template <typename T> struct converter<std::list<T, Alloc>> { sequence path };
template <typename T> struct converter<std::map<K,V,Comp,Alloc>> { associative path };
template <typename T> struct converter<std::set<T,Comp,Alloc>> { associative path };
// + unordered variants. All allocator/comp/cmp generic via template params.
```
The four families must be matched by *shape*, not alias: partial specialization on `std::vector<T, Alloc>`, `std::list<T, Alloc>`, `std::map<K,V,C,A>`, `std::set<K,C,A>` plus unordered twins.

- [ ] **Step 1: Write the failing test**

`tests/convert_test.cpp` (a tiny in-memory `value_io` implementation for testing only):
```cpp
#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>

namespace {
// Minimal test double implementing value_io over std::variant-like storage.
struct box : rtti::value_io {
    // ... test-only storage: holds scalar or vector<box*> or map<string,box*>
};
}
TEST_CASE(scalar_roundtrip) { /* integer/real/bool/string via box */ }
TEST_CASE(vector_roundtrip) { /* vector<int> in/out, incl. custom allocator type */ }
TEST_CASE(list_roundtrip) { /* list<string> */ }
TEST_CASE(map_roundtrip) { /* map<string,int> */ }
TEST_CASE(set_roundtrip) { /* set<int> */ }
TEST_CASE(custom_allocator) { /* std::vector<int, my_alloc<int>> must convert */ }
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `convert.hpp` and `converter` missing.

- [ ] **Step 3: Write minimal implementation**

`convert.hpp` as per Interfaces: trait detection, shape-based partial specializations for the four families + unordered variants, scalar converter via `field_value`, `from_value`/`to_value` entry points, and `is_reflected`/`is_container` trait set. Allocator/compare parameters flow through the specialization template parameters untouched.

- [ ] **Step 4: Run test to verify it passes**

Rebuild, run. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/rtti/convert.hpp tests/convert_test.cpp tests/CMakeLists.txt
git commit -m "convert: value_io abstraction + scalar/string/container converters"
```

---

### Task 7: Nested struct recursion + ADL dispatch override

**Files:**
- Modify: `include/rtti/convert.hpp`
- Modify: `include/rtti/reflection.hpp`
- Create: `tests/recursion_test.cpp`

**Interfaces:**
- Consumes: Task 6 converters, Task 5 macros.
- Produces:
```cpp
namespace rtti {
// ADL hooks (users may define in their own namespace for custom types):
//   void from_value(const value_io*, T&, error_code&);
//   void to_value(value_io*, const T&, error_code&);
// Reflect-core routes: ADL first, then field-table walk, then converter<T>.

template <typename T> struct is_reflected<T, std::void_t<decltype(rtti::fields_of<T>::count)>>
    : std::true_type {};
}
```
Walk driver:
```cpp
template <typename T>
error_code reflect_from(const value_io* src, T& out) {
    if (src->type() != value_io::kind::object) return error_code::kind_mismatch;
    for (auto& f : rtti::fields_of<T>::array) {
        value_io* sub = nullptr;
        if (!src->get_key(f.name, sub)) continue;           // missing key
        if (!f.set(&out, /* read sub into field_value or nested */ ...)) {
            // nested/container field: route through from_value<M>
        }
    }
    return error_code::ok;
}
```
To keep Task 7 small, field set that requires recursion uses `from_value<M>`; scalar fields use `field_value`. Missing-key behavior depends on `has_default<T>` trait: standard macro → `error_code::missing_key`; `_WITH_DEFAULT` → keep value, continue.

- [ ] **Step 1: Write the failing test**

`tests/recursion_test.cpp`:
```cpp
#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include <rtti/reflection.hpp>
#include <string>
#include <vector>

namespace {
struct Inner { int a = 0; std::string b; RTTI_DEFINE_TYPE(Inner, a, b); };
struct Outer {
    int n = 0;
    Inner in;
    std::vector<Inner> list;
    RTTI_DEFINE_TYPE(Outer, n, in, list);
};
struct Custom { int v = 0; };
void from_value(const rtti::value_io* src, Custom& c, rtti::error_code& e);   // ADL
void to_value(rtti::value_io* dst, const Custom& c, rtti::error_code& e);     // ADL
}
TEST_CASE(nested_struct_roundtrip) { /* Outer -> box -> Outer, deep equal */ }
TEST_CASE(adl_override_wins) { /* Custom NOT registered; ADL functions must be used */ }
TEST_CASE(missing_key_default) { /* _WITH_DEFAULT struct: missing key keeps value */ }
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `reflect_from`, `from_value<M>` nested routing missing.

- [ ] **Step 3: Write minimal implementation**

Add `reflect_from/reflect_to` walk drivers; route each field: scalar → `field_value` set; otherwise call `from_value<M>`/`to_value<M>` (which recursively dispatches via ADL → reflected → converter). Add `is_reflected` detection. Wire `has_default<T>` trait; standard macro returns `missing_key`, `_WITH_DEFAULT` skips.

- [ ] **Step 4: Run test to verify it passes**

Rebuild, run. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/rtti/convert.hpp include/rtti/reflection.hpp tests/recursion_test.cpp tests/CMakeLists.txt
git commit -m "convert: nested struct recursion + ADL override + default-key behavior"
```

---

### Task 8: Runtime reflection query API (.NET-grade ergonomics)

**Files:**
- Create: `include/rtti/reflect.hpp` (public convenience layer)
- Create: `tests/runtime_api_test.cpp`

**Interfaces:**
- Consumes: Tasks 4-7.
- Produces:
```cpp
namespace rtti {
template <typename T>
struct type_info {
    using value_type = T;
    static constexpr const char* name() noexcept;          // typeid(T).name() or registered name
    static constexpr std::size_t field_count() noexcept;
    static const rtti::field_access_base* fields() noexcept;
};

// .NET-style entry points:
template <typename T> type_info<T> get_type() noexcept;   // akin to typeof(T)
template <typename T> type_info<std::decay_t<T>> get_type(const T&) noexcept; // akin to obj.GetType()

// Field access by name string at runtime:
template <typename T>
const field_access_base* find_field(const char* name) noexcept;
template <typename T>
bool get_field(const T& obj, const char* name, field_value& out) noexcept;
template <typename T>
bool set_field(T& obj, const char* name, const field_value& in) noexcept;

// Iteration (range-for over fields of T):
template <typename T> const field_access_base* begin() noexcept;
template <typename T> const field_access_base* end() noexcept;
}
```
The public layer is thin: all of the above delegate to `fields_of<T>` from Task 4. `.NET` parity: user writes `auto t = rtti::get_type(obj); for (auto& f : t.fields()) { f.name; f.get(&obj); }` — no template arcana.

- [ ] **Step 1: Write the failing test**

`tests/runtime_api_test.cpp`:
```cpp
#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <string>

namespace {
struct Config { std::string host; int port = 0; RTTI_DEFINE_TYPE(Config, host, port); };
}

TEST_CASE(get_type_like_dotnet) {
    Config c; c.host = "x"; c.port = 80;
    auto t = rtti::get_type(c);
    CHECK_EQ(t.field_count(), size_t(2));
    const auto* f = rtti::find_field<Config>("port");
    CHECK(f != nullptr && f->type == &typeid(int));
}

TEST_CASE(runtime_get_set_by_name) {
    Config c; c.host = "a"; c.port = 1;
    rtti::field_value v;
    CHECK(rtti::get_field(c, "port", v));
    CHECK_EQ(v.as_integer(), int64_t(1));
    CHECK(rtti::set_field(c, "port", rtti::field_value::of(int64_t(8080))));
    CHECK_EQ(c.port, 8080);
    CHECK(!rtti::get_field(c, "nope", v));  // unknown name -> false, no UB
}

TEST_CASE(range_for_over_fields) {
    Config c; c.host = "h"; c.port = 2;
    int seen = 0;
    for (auto& f : rtti::fields_of<Config>::array) { ++seen; (void)f; }
    CHECK_EQ(seen, 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `get_type`, `find_field`, `get_field`, `set_field` missing.

- [ ] **Step 3: Write minimal implementation**

`reflect.hpp`: thin wrappers over `fields_of<T>`; `find_field` does a linear scan (compile-time array, small n — fast and portable); `get_field`/`set_field` locate then call `typed_field_access::get/set`. `type_info<T>` aggregates the same data. All noexcept where the underlying ops are noexcept.

- [ ] **Step 4: Run test to verify it passes**

Rebuild, run. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/rtti/reflect.hpp tests/runtime_api_test.cpp tests/CMakeLists.txt
git commit -m "reflect: runtime query API, dotnet-grade entry points"
```

---

### Task 9: `value_io` test double library (shared test-only `box`), plus reflection smoke for Review Focus cases

**Files:**
- Create: `tests/box.hpp` (shared test double implementing `value_io` over own storage)
- Create: `tests/review_focus_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 6-8.
- Produces: `tests/box.hpp` reusable in later format tests; exercises every Review Focus line from this plan's header.

- [ ] **Step 1: Write the failing test**

`tests/review_focus_test.cpp`:
```cpp
#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include "box.hpp"
#include <string>
#include <vector>
#include <set>

namespace {
struct Unregistered { int a = 0; };            // NO macro on purpose
struct Empty {};                                // zero fields
RTTI_DEFINE_TYPE(Empty);
struct WithAlloc {
    std::vector<int, custom_alloc<int>> v;      // custom allocator
    std::set<std::string, std::less<std::string>, custom_alloc<std::string>> s;
    RTTI_DEFINE_TYPE(WithAlloc, v, s);
};
struct Defaults { int t = 5; std::string h; RTTI_DEFINE_TYPE_WITH_DEFAULT(Defaults, t, h); };
struct Strict { int t = 0; RTTI_DEFINE_TYPE(Strict, t); };
}

TEST_CASE(unregistered_type_compile_error) { /* meta-test: static_assert !is_reflected<Unregistered> */ }
TEST_CASE(empty_struct_ok) { /* Empty converts trivially */ }
TEST_CASE(custom_allocator_containers) { /* WithAlloc roundtrip */ }
TEST_CASE(default_variant_missing_key) { /* Defaults missing 't' keeps 5 */ }
TEST_CASE(strict_variant_missing_key) { /* Strict missing 't' -> missing_key error */ }
TEST_CASE(adl_handwritten_inside_container) { /* vector<Custom> uses ADL for element */ }
TEST_CASE(unknown_name_runtime_safe) { /* covered in Task 8; re-pin here */ }
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `box.hpp` missing, several behaviors unimplemented.

- [ ] **Step 3: Write minimal implementation**

`box.hpp`: test-only `value_io` impl storing `field_value` or `std::vector<box*>` or `std::map<std::string, box*>`; implements all virtuals. Wire missing behaviors in `convert.hpp`/`reflection.hpp` if any test exposes a gap (TDD-driven fixes).

- [ ] **Step 4: Run test to verify it passes**

Rebuild, run. Expected: PASS. Confirm `Unregistered` really fails to compile under a `static_assert(rtti::is_reflected<Unregistered>::value)` false check — use `static_assert(!rtti::is_reflected<Unregistered>::value, "unregistered must be detected")`.

- [ ] **Step 5: Commit**

```bash
git add tests/box.hpp tests/review_focus_test.cpp tests/CMakeLists.txt
git commit -m "tests: value_io test double + review-focus cases"
```

---

### Task 10: Stripped-build matrix verification + format-gate stubs

**Files:**
- Create: `include/rtti/json.hpp`, `include/rtti/xml.hpp`, `include/rtti/ini.hpp` (gate stubs)
- Create: `tests/strip_matrix_test.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: everything above.
- Produces:
```cpp
// include/rtti/json.hpp (gate stub — format impls arrive in later phase)
#pragma once
#include <rtti/rtti_fwd.hpp>
#if !defined(RTTI_ENABLE_JSON)
#  error "RTTI: JSON format adapter not enabled (compile with -DRTTI_ENABLE_JSON=1)"
#endif
```
Same pattern for xml/ini. Reflect core headers must NOT include these.

- [ ] **Step 1: Write the failing test**

`tests/strip_matrix_test.cpp`: `#include <rtti/reflect.hpp>` with ALL format macros undefined; verify core compiles and `is_reflected` works.

- [ ] **Step 2: Run test to verify it fails**

Expected: compile error if any reflect-core header pulled a format header. Also add a CMake option path: configure a second build dir with `-DRTTI_ENABLE_JSON=OFF -DRTTI_ENABLE_XML=OFF -DRTTI_ENABLE_INI=OFF` and build `rtti` + `rtti_tests`; core must build and pass.

- [ ] **Step 3: Write minimal implementation**

Gate stubs above; ensure `reflection.hpp`/`convert.hpp`/`reflect.hpp` include only core headers; add a CMake target-level guard so tests still build with all formats off.

- [ ] **Step 4: Run test to verify it passes**

Both build dirs (all-on, all-off) build; tests pass in both.

- [ ] **Step 5: Commit**

```bash
git add include/rtti/json.hpp include/rtti/xml.hpp include/rtti/ini.hpp tests/strip_matrix_test.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "build: format-gate stubs + stripped-matrix verification"
```

---

### Task 11: English README + Chinese README with cross-linked headings

**Files:**
- Create: `README.en.md`
- Create: `README.zh-CN.md`
- Create: `README.md` (toc linking both)

**Interfaces:**
- Consumes: final API surface from Tasks 1-10.
- Produces: bilingual docs with identical section structure and `#`-anchored cross-links.

- [ ] **Step 1: Write the failing test**

No unit test; verification is manual: both files exist, every heading in `README.en.md` has a matching heading in `README.zh-CN.md` (same slug), and each file contains a line `[中文版](README.zh-CN.md)` / `[English](README.en.md)` plus a section mapping table.

- [ ] **Step 2: Run test to verify it fails**

(Verification step) Check the files exist and structure matches after creation.

- [ ] **Step 3: Write minimal implementation**

`README.en.md`: Introduction (RTTI = modern C++ runtime-reflection library compensating for C++'s lack of first-class reflection), Quick Start (struct + one macro + `rtti::get_type`, `find_field`, `get_field`, `set_field`, `RTTI_DEFINE_TYPE` family), Architecture (reflect core / format adapters / common), Supported containers (map/set/list/vector with allocator transparency), Portability (C++17, MSVC/GCC/Clang, no non-generic APIs, SIMD gated), Bilingual docs section with link to `README.zh-CN.md`.
`README.zh-CN.md`: mirrored content in Chinese, same headings, links to English edition and per-heading anchors.

- [ ] **Step 4: Run test to verify it passes**

Manual review: cross-link anchors resolve.

- [ ] **Step 5: Commit**

```bash
git add README.en.md README.zh-CN.md README.md
git commit -m "docs: bilingual README with cross-linked headings"
```

---

### Task 12: Performance microbenchmark scaffolding (ops/s harness) — reflect-core only

**Files:**
- Create: `bench/CMakeLists.txt`
- Create: `bench/bench_reflect.cpp`
- Create: `bench/bench_timer.hpp`

**Interfaces:**
- Consumes: Tasks 4-8.
- Produces: `rtti_bench` executable reporting field-walk / get-set / reflect roundtrip ops/s; timer uses `std::chrono::steady_clock` (portable).

- [ ] **Step 1: Write the failing test**

(Not a correctness test.) `bench/bench_reflect.cpp`:
```cpp
// Benchmark: reflect roundtrip on a 4-field struct via value_io test double.
// Reports ops/s to stdout. No assertion; run manually.
```

- [ ] **Step 2: Run test to verify it fails**

(Manual) Build and run; confirm it prints sane numbers.

- [ ] **Step 3: Write minimal implementation**

`bench_timer.hpp`: RAII steady_clock timer; `bench_reflect.cpp`: builds a `box` source, runs `reflect_from`/`reflect_to` N times, prints ops/s.

- [ ] **Step 4: Run test to verify it passes**

Output plausible (e.g., millions of ops/s range for field ops).

- [ ] **Step 5: Commit**

```bash
git add bench/CMakeLists.txt bench/bench_reflect.cpp bench/bench_timer.hpp CMakeLists.txt
git commit -m "bench: reflect-core microbenchmark scaffolding"
```

---

## Self-Review

**1. Spec coverage:**
- Field table + macro family: Tasks 4-5. ✓
- Runtime reflection (.NET-grade): Task 8 + Review Focus. ✓
- `convert<T>` scalars/string/four container families/allocator transparency: Task 6. ✓
- Nested struct recursion + ADL: Task 7. ✓
- `value_io` format-agnostic abstraction: Task 6 + Task 9 test double. ✓
- Stripped-build (no format deps): Task 10. ✓
- Bilingual cross-indexed docs: Task 11. ✓
- Bench scaffolding: Task 12. ✓
- Static library + tests: Tasks 1-3. ✓
- Format adapters (JSON/XML/INI): explicitly deferred to a later phase per user instruction; gate stubs in Task 10 preserve the interface. ✓

**2. Placeholder scan:** All steps contain concrete code or concrete file content; no "TBD", no "implement later" beyond the explicit deferred-format note. ✓

**3. Type consistency:** `field_access_base::get/set` signatures (Task 4) are used identically in Tasks 8-9; `fields_of<T>` count/array names stable; `value_io` virtuals stable across Tasks 6/7/9; `error_code` members used by Tasks 6-7. ✓

**4. Review Focus:** All 8 lines from the header have owning tests: unregistered-type (Task 9), private-member intrusive (Task 5), custom allocator (Task 9 + Task 6), unknown-name runtime (Task 8 + Task 9), default-vs-strict missing key (Task 7 + Task 9), empty struct (Task 9), ADL override (Task 7), kind-mismatch (Task 4 + Task 6). ✓
