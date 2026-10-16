# `rtti::reflection.hpp` — Reflection Core API Reference

- **Module**: reflection core (reflect engine)
- **Header**: `include/rtti/reflection.hpp`
- **Include dependencies**:
  - System/STL: `<charconv>`, `<cstddef>`, `<cstdint>`, `<cstdlib>`, `<cstring>`, `<functional>`, `<initializer_list>`, `<limits>`, `<mutex>`, `<optional>`, `<string>`, `<tuple>`, `<type_traits>`, `<variant>`, `<typeinfo>`, `<utility>`, `<vector>`
  - Project: `<rtti/memory.hpp>` (provides `rtti::string_view`), `<rtti/naming.hpp>` (provides `rtti::detail::naming_policy`, `key_case`, `apply_naming`)
  - Tail include: `<rtti/convert.hpp>` (the format-agnostic converters `from_value` / `to_value` / `from_value_named` / `to_value_named` / `member_to_value` / `member_to_value_named`, pushed to the end of the file to break the include cycle — see §23)
  - Transitive: `<rtti/error.hpp>` via `memory.hpp` (defines `rtti::error_code`, `rtti::result<T>`, `rtti::error_info`)
- **Namespace**: `rtti` (registration macros are emitted at namespace scope inside `namespace rtti { ... }`)
- **Language / standard**: C++17, English-internal comments, no third-party dependencies
- **Role**: this header is the **reflection core**. It implements the compile-time field tables and runtime-iterable field access, the runtime type-description system, enum reflection, attributes, template-argument reflection, derived-type base merging, reflective construction, C#-style `as`/`cast` conversions, the `object_ref` / `instance` handles, method reflection, and the full registration macro family. Format adapters (JSON/XML/INI/TOML/proto/flat) build on top of it through the `value_io` abstract bridge.

> Every signature in this document is transcribed verbatim from the header. Behavior notes and line ranges refer to `include/rtti/reflection.hpp`.

## Table of contents

1. Container shape traits (type predicates)
2. Registration core: `fields_of`, `base_types`, `is_reflected`
3. Typed identifiers and scalar values: `type_id`, `field_value`, `field_value_to`, `value_io`
4. Field access model: `field_access_base`, `typed_field_access`
5. Reflective field queries: `field_count`, `fields`, `for_each_field`, `has_default`
6. Attribute / annotation system
7. Enum reflection
8. Type description system: `type_kind`, `container_family`, `type_desc` family, `type_of`
9. Compile-time type judgments
10. Family discrimination and enum helpers
11. Template-argument reflection
12. Derived-type merge machinery (concepts)
13. Reflective construction: `ctor_arg`, `ctor_param`, `ctor_registry`
14. Type conversion: `as` / `cast`
15. `object_ref<T>` instance handle
16. `instance` type-erased handle
17. Method reflection
18. Macro infrastructure (`RTTI_NARG`, `RTTI_APPLY`, …)
19. Field-registration helper macros
20. Type registration macros (`RTTI_DEFINE_TYPE*`)
21. Enum registration macros
22. Template-info registration macros
23. Attribute registration macros
24. Constructor registration macros
25. Method registration macros
26. Header structure and include cycle note
27. Other languages

---

## 1. Container shape traits (type predicates)

These compile-time predicates classify types by *shape* (member detection), independent of any format. Custom allocators, comparators and hashers flow through untouched because matching is done on the required member signatures, not on the std specialization.

### 1.1 `is_char_string<T>`

```cpp
template <typename T>
struct is_char_string : std::false_type {};
template <typename Char, typename Traits, typename Alloc>
struct is_char_string<std::basic_string<Char, Traits, Alloc>> : std::true_type {};
```

True for any `std::basic_string` specialization (any char type, traits, allocator). A string is excluded from the *sequence* family because it is a scalar (text) type for conversion purposes.

### 1.2 `is_sequence<T>`

```cpp
template <typename T, typename = void>
struct is_sequence : std::false_type {};
template <typename T>
struct is_sequence<
    T, std::enable_if_t<!is_char_string<T>::value,
                        std::void_t<typename T::value_type,
                                    decltype(std::declval<T&>().push_back(
                                        std::declval<typename T::value_type>())),
                                    decltype(std::declval<T&>().begin()),
                                    decltype(std::declval<T&>().end())>>>
    : std::true_type {};
```

True when `T` has `push_back(value_type)` plus `begin()`/`end()` — the vector/list/deque family. `std::basic_string` is excluded via `is_char_string`.

### 1.3 `is_associative<T>`

```cpp
template <typename T, typename = void>
struct is_associative : std::false_type {};
template <typename T>
struct is_associative<T, std::void_t<typename T::key_type,
                                     typename T::value_type,
                                     decltype(std::declval<T&>().insert(
                                         std::declval<typename T::value_type>()))>>
    : std::true_type {};
```

True when `T` has `key_type` and `insert(value_type)` — both the map and the set families.

### 1.4 `is_map<T>`

```cpp
template <typename T, typename = void>
struct is_map : std::false_type {};
template <typename T>
struct is_map<T, std::void_t<typename T::mapped_type, typename T::key_type>>
    : std::true_type {};
```

The map family additionally exposes `mapped_type` (its `value_type` is a `std::pair`).

### 1.5 `is_set<T>`

```cpp
template <typename T>
struct is_set : std::bool_constant<is_associative<T>::value && !is_map<T>::value> {};
```

The set family is associative but has no `mapped_type`.

### 1.6 `is_queue<T>`

```cpp
template <typename T, typename = void>
struct is_queue : std::false_type {};
template <typename T>
struct is_queue<T, std::void_t<typename T::value_type,
                               decltype(std::declval<T&>().push(
                                   std::declval<typename T::value_type>())),
                               decltype(std::declval<T&>().pop()),
                               decltype(std::declval<T&>().front()),
                               decltype(std::declval<T&>().back())>>
    : std::true_type {};
```

True when `T` has `push`/`pop`/`front`/`back` — the `std::queue` shape.

### 1.7 `is_stack<T>`

```cpp
template <typename T, typename = void>
struct is_stack : std::false_type {};
template <typename T>
struct is_stack<T, std::void_t<typename T::value_type,
                               decltype(std::declval<T&>().push(
                                   std::declval<typename T::value_type>())),
                               decltype(std::declval<T&>().pop()),
                               decltype(std::declval<T&>().top())>>
    : std::true_type {};
```

True when `T` has `push`/`pop`/`top` and **no** `front`/`back` — the `std::stack` shape.

### 1.8 `is_priority_queue<T>`

```cpp
template <typename T, typename = void>
struct is_priority_queue : std::false_type {};
template <typename T>
struct is_priority_queue<
    T, std::void_t<typename T::value_type, typename T::value_compare,
                   decltype(std::declval<T&>().push(
                       std::declval<typename T::value_type>())),
                   decltype(std::declval<T&>().pop()),
                   decltype(std::declval<T&>().top())>>
    : std::true_type {};
```

True when `T` has `push`/`pop`/`top` plus a `value_compare` member type — the `std::priority_queue` shape.

### 1.9 `has_push_back<T>`

```cpp
template <typename T, typename = void>
struct has_push_back : std::false_type {};
template <typename T>
struct has_push_back<T, std::void_t<decltype(std::declval<T&>().push_back(
    std::declval<typename T::value_type>()))>> : std::true_type {};
```

True when `T` has `push_back(value_type)`: vector, deque, list, `basic_string`.

### 1.10 `has_data<T>` (forward declaration)

```cpp
template <typename T, typename = void>
struct has_data;
```

Forward-declared here; the definition lives in §10 (`has_data<T, std::void_t<decltype(std::declval<T&>().data())>>`). `is_tuple` uses the forward declaration to tell `std::array` from a tuple.

### 1.11 `is_optional<T>`

```cpp
template <typename T, typename = void>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<T, std::void_t<typename T::value_type>> : std::bool_constant<
    std::is_same_v<T, std::optional<typename T::value_type>>> {};
```

True when `T` is `std::optional<U>` (checked against `T`'s own `value_type`).

### 1.12 `is_variant<T>`

```cpp
template <typename T, typename = void>
struct is_variant : std::false_type {};
template <typename T>
struct is_variant<T, std::void_t<typename std::variant_size<T>::type>>
    : std::true_type {};
```

True when `T` exposes `std::variant_size` — any `std::variant` specialization.

### 1.13 `is_tuple<T>`

```cpp
template <typename T, typename = void>
struct is_tuple : std::false_type {};
template <typename T>
struct is_tuple<T, std::void_t<typename std::tuple_size<T>::type>>
    : std::bool_constant<!has_data<T>::value> {};
```

True when `T` exposes `std::tuple_size` but is **not** `std::array` (array also specializes `tuple_size`, so it is excluded via `has_data`; arrays are contiguous, tuples are not).

### 1.14 `is_pair<T>`

```cpp
template <typename T, typename = void>
struct is_pair : std::false_type {};
template <typename T>
struct is_pair<T, std::void_t<decltype(std::declval<T&>().first),
                              decltype(std::declval<T&>().second)>>
    : std::true_type {};
```

True when `T` has `first`/`second` members — `std::pair`.

### 1.15 `is_fixed_sequence<T>`

```cpp
template <typename T, typename = void>
struct is_fixed_sequence : std::false_type {};
template <typename T>
struct is_fixed_sequence<T, std::enable_if_t<
    !is_char_string<T>::value && !has_push_back<T>::value &&
        !is_associative<T>::value,
    std::void_t<typename T::value_type,
                decltype(std::declval<T&>().begin()),
                decltype(std::declval<T&>().end())>>> : std::true_type {};
```

Fixed-size sequence: `value_type` + `begin`/`end` but no `push_back` — `std::array`. `basic_string` is excluded via `is_char_string`; optional/variant/tuple have no `begin`/`end`; associative containers are excluded so a fixed-sequence converter never tries `out[i]` on a `std::map`.

### 1.16 `is_c_array<T>`

```cpp
template <typename T>
struct is_c_array
    : std::bool_constant<
          std::is_array<T>::value &&
          !std::is_same<std::remove_extent_t<T>, char>::value> {};
```

True for a native C array (`T[N]`, `T[N][M]`, …). `char[N]` is excluded: a C char array is a fixed string target, not a numeric sequence (spec §12.2).

### 1.17 `is_md_array<T>`

```cpp
template <typename T>
struct is_md_array
    : std::bool_constant<
          is_c_array<T>::value && is_c_array<std::remove_extent_t<T>>::value> {};
```

True for a multidimensional C array (`T[N][M]`, `T[A][B][C]`, …). One-dimensional `T[N]` is not.

### 1.18 `is_reflected<T>`

```cpp
template <typename T, typename = void>
struct is_reflected : std::false_type {};
template <typename T>
struct is_reflected<T, std::void_t<decltype(fields_of<T>::count)>>
    : std::true_type {};
```

True when `T` participates in reflection — either through the intrusive nested `rtti_registration` or a non-intrusive `fields_of<T>` specialization exposing a static `count`.

### 1.19 `key_traits<K>`

```cpp
template <typename K, typename = void>
struct key_traits {
    static void assign(K& out, string_view key) {
        out = K(key.data(), key.size());
    }
    static string_view view(const K& key) { return string_view(key); }
};

template <>
struct key_traits<std::string> {
    static void assign(std::string& out, string_view key) {
        out.assign(key.data(), key.size());
    }
    static string_view view(const std::string& s) noexcept {
        return string_view(s.data(), s.size());
    }
};
```

Key-handling policy for associative containers. The generic fallback supports any key constructible from a `string_view`; `std::string` has a dedicated specialization. `assign` copies a runtime key into `K`; `view` presents a stored key as `string_view`.

> APIs in this chapter are extracted from `reflection.hpp:75-264`.

---

## 2. Registration core: `fields_of`, `base_types`, `is_reflected`

### 2.1 `fields_of<T>`

```cpp
template <typename T, typename = void>
struct fields_of;
```

The reflected type registry. A type participates in reflection **iff** this template is specialized (by the `RTTI_DEFINE_TYPE` macro family or by hand). A specialization exposes:

- `static constexpr std::size_t count` — number of reflected fields.
- `using members_type = std::tuple<...>` — the typed member-pointer types of the type's OWN fields.
- `static inline const members_type members` — the member pointers of the type's own fields (declaration order).
- `static inline const field_access_base* const array[N]` — runtime-iterable array of field accessors (only the first `count` entries are valid).
- `static inline const field_type_desc desc[N]` — compiler-time field descriptors (name + on-demand type resolver + `member_access` access level).

The macro-generated specialization bodies are shown in §20. Manual specializations are supported with the same members.

### 2.2 The intrusive pickup specialization

```cpp
// Intrusive registration: the macro places a nested rtti_registration struct
// inside T; fields_of then picks it up by inheritance (works inside the class
// body where an explicit template specialization is illegal).
template <typename T>
struct fields_of<T, std::void_t<typename T::rtti_registration>>
    : T::rtti_registration {};
```

Any type exposing a nested `rtti_registration` struct (created by `RTTI_DEFINE_TYPE_INTRUSIVE*`) is automatically reflected: `fields_of<T>` inherits that struct, so its `count`/`members`/`array`/`desc`/`members_type` are picked up. This works at class scope where an explicit template specialization is illegal.

### 2.3 `base_types<T>`

```cpp
template <typename T, typename = void>
struct base_types {
    using list = std::tuple<>;
};
```

Declares the direct base types of a reflected type. `RTTI_DEFINE_TYPE_DERIVED` specializes this to record the base whose fields merge into the derived type's field table and whose `type_id` appears in the runtime base chain. The primary template is an empty list (no reflected bases).

### 2.4 `is_reflected<T>`

Covered in §1.18.

> APIs in this chapter are extracted from `reflection.hpp:61-73` and `reflection.hpp:807-812`.

---

## 3. Typed identifiers and scalar values

### 3.1 `type_id`

```cpp
// Runtime type identifier: the address of a std::type_info.
using type_id = const std::type_info*;
```

A runtime type identifier is the address of a `std::type_info`. `typeid(T)` lvalues have static storage duration, so comparing pointed-to `std::type_info` objects is the canonical identity test.

### 3.2 `field_value`

A variant-like value that can hold one reflected field's scalar payload.

```cpp
class field_value {
public:
    enum class kind { none, boolean, integer, real, string, object };

    field_value() noexcept : k_(kind::none) {}

    static field_value of(bool v) noexcept;               // kind::boolean
    static field_value of(int64_t v) noexcept;            // kind::integer
    static field_value of(double v) noexcept;             // kind::real
    static field_value of(string_view v) noexcept;        // kind::string
    static field_value of_object() noexcept;              // kind::object

    kind type() const noexcept;
    bool is_none() const noexcept;
    bool is_integer() const noexcept;

    // Accessors; each returns a fallback when the stored kind differs.
    bool as_bool() const noexcept;
    int64_t as_integer() const noexcept;
    double as_real() const noexcept;
    string_view as_string() const noexcept;
};
```

| Member | Meaning |
|---|---|
| `of(...)` factories | Build a `field_value` of the matching kind. |
| `of_object()` | Marks a nested-object / unsupported member; no payload. |
| `type()` | The stored `kind`. |
| `is_none()` / `is_integer()` | Kind checks. |
| `as_bool()` / `as_integer()` / `as_real()` / `as_string()` | Kind-checked accessors; each returns a zero/empty fallback (`false`/`0`/`0.0`/empty view) on kind mismatch, never a conversion. |

Example:

```cpp
field_value v = field_value::of(int64_t(42));
assert(v.is_integer());
assert(v.as_integer() == 42);
assert(v.as_string().empty());   // kind mismatch -> empty fallback
```

### 3.3 `field_value_to<M>(out, v)`

```cpp
// Converts a field_value into a typed destination. Returns false on kind
// mismatch. Single template with compile-time dispatch (no overload sets, so
// MSVC cannot report ambiguous calls for int-like member types).
template <typename M>
inline bool field_value_to(M& out, const field_value& v) noexcept;
```

Compile-time dispatch over the destination scalar kind:

- `M == bool` → requires `kind::boolean`.
- `M` integral → requires `kind::integer`; value is `static_cast<M>(v.as_integer())`.
- `M` floating point → requires `kind::real`; value is `static_cast<M>(v.as_real())`.
- `M == string_view` → requires `kind::string`.
- `M == std::string` → requires `kind::string`; copies the view via `assign`.
- Anything else → returns `false`.

Returns `true` on success, `false` on kind mismatch (the destination is left untouched).

### 3.4 `value_io`

The abstract, format-agnostic source/sink bridge between the reflect core and any storage model (a parse tree, a DOM, a builder). Kind mirrors JSON-ish value categories. Defined here (not in `convert.hpp`) because `field_access_base`'s runtime container operations call into it; `convert.hpp` consumes it via `#include`.

```cpp
class value_io {
public:
    enum class kind { null, boolean, integer, real, string, array, object };

    virtual ~value_io() = default;

    virtual kind type() const noexcept = 0;

    // Read side (source). Each returns false on kind mismatch.
    virtual bool as_bool(bool& out) const noexcept = 0;
    virtual bool as_integer(int64_t& out) const noexcept = 0;
    virtual bool as_real(double& out) const noexcept = 0;
    virtual bool as_string(string_view& out) const noexcept = 0;

    // Container read side.
    virtual std::size_t size() const noexcept = 0;
    virtual value_io* at(std::size_t index) = 0;                 // array element
    virtual value_io* at(std::size_t index) const = 0;
    virtual bool get_key(string_view key, value_io*& out) = 0;   // object lookup
    virtual bool get_key(string_view key, value_io*& out) const = 0;
    virtual bool get_key_at(std::size_t index, string_view& key,
                            value_io*& out) const = 0;

    // Write side (sink). make_* return the node itself for chaining.
    virtual value_io* make_array() = 0;
    virtual void push_back(value_io* v) = 0;
    virtual value_io* make_object() = 0;
    virtual void set_key(string_view key, value_io* v) = 0;
    virtual value_io* make_scalar(field_value v) = 0;
    virtual value_io* make_node() = 0;
};
```

| Group | Member | Semantics |
|---|---|---|
| Type | `type()` | The node's value category. |
| Read scalar | `as_bool/as_integer/as_real/as_string(out)` | Decode into `out`; return `false` on kind mismatch. |
| Read container | `size()` | Element count for arrays/objects. |
| | `at(index)` | Indexed element access (array element); `const` and non-`const` overloads. |
| | `get_key(key, out)` | Object lookup by key; `const` and non-`const` overloads. |
| | `get_key_at(index, key, out)` | Enumerate the `index`-th key/value pair of an object; `false` when out of range or not an object. |
| Write | `make_array()` / `make_object()` / `make_scalar(v)` | Build nodes; return the node for chaining. |
| | `push_back(v)` / `set_key(key, v)` | Append/assign children. |
| | `make_node()` | Create an independent child node owned by the same tree (used by the core container converters to build element/value nodes). |

The public converter entry points declared in this header (defined in `convert.hpp`) are:

```cpp
template <typename T>
error_code from_value(const value_io* src, T& out);
template <typename T>
error_code to_value(value_io* dst, const T& in);
template <typename T>
error_code from_value_named(const value_io* src, T& out,
                            const rtti::detail::naming_policy& np);
template <typename T>
error_code to_value_named(value_io* dst, const T& in,
                          const rtti::detail::naming_policy& np);
template <typename M>
error_code member_to_value(value_io* dst, const M& v);
template <typename M>
error_code member_to_value_named(value_io* dst, const M& v,
                                 const rtti::detail::naming_policy& np);
```

> APIs in this chapter are extracted from `reflection.hpp:266-405` (declarations at `reflection.hpp:35-59`).

---

## 4. Field access model

### 4.1 `field_access_base`

A uniform, runtime-iterable view of one reflected member. The derived `typed_field_access<T, M>` supplies the concrete read/write. The `value_io*` overloads bridge to the format-agnostic converters for nested/container members (two-phase lookup keeps the core header free of format dependencies).

```cpp
struct field_access_base {
    const char* name;
    type_id type;

    field_access_base(const char* n, type_id t) noexcept;

    virtual field_value get(const void* obj) const = 0;
    virtual bool set(void* obj, const field_value& v) const = 0;

    virtual error_code get_value(const void* obj, value_io* dst) const = 0;
    virtual error_code set_value(void* obj, const value_io* src) const = 0;

    virtual error_code get_value_named(const void* obj, value_io* dst,
                                       const rtti::detail::naming_policy& np) const;
    virtual error_code set_value_named(void* obj, const value_io* src,
                                       const rtti::detail::naming_policy& np) const;

    virtual bool is_null_value(const void* obj) const;

    virtual bool is_container() const = 0;

    // --- Runtime container operations ---
    virtual error_code field_add(void* obj, const value_io* element) const = 0;
    virtual error_code field_add_scalar(void* obj, const field_value& element) const = 0;
    virtual error_code erase_at(void* obj, std::size_t index) const = 0;
    virtual error_code erase_key(void* obj, string_view key) const = 0;
    virtual error_code clear(void* obj) const = 0;
    virtual std::size_t size(void* obj) const = 0;
    using element_visitor = std::function<bool(std::size_t, string_view,
                                               const value_io*)>;
    virtual error_code for_each(void* obj, value_io* scratch,
                                const element_visitor& fn) const = 0;
};
```

| Member | Semantics |
|---|---|
| `name`, `type` | Field name and `type_id`. |
| `get(obj)` / `set(obj, v)` | Raw scalar read/write through a `field_value`. |
| `get_value` / `set_value` | Format-agnostic read/write through a `value_io` sink/source. |
| `get_value_named` / `set_value_named` | Named-conversion read/write routed through `to_value_named` / `from_value_named` so the naming policy applies recursively. The base default falls back to the plain accessors (a type-erased member keeps no-naming behavior when not overridden). |
| `is_null_value(obj)` | True when the member's value is null-equivalent (an empty optional or a null pointer), used by `skip_empty`. Default `false`. |
| `is_container()` | True when the member is one of the supported container families. |
| `field_add(obj, element)` | Add one element (read from a `value_io` node) to a container member. `kind_mismatch` when the member is not a supported container. |
| `field_add_scalar(obj, element)` | Add one scalar element. `map` members are unsupported here (use `field_add` with a key/value object node); `kind_mismatch` otherwise. |
| `erase_at(obj, index)` | Erase the `index`-th element of a sequence member; `kind_mismatch` for non-sequences, `no_such_field` when out of range. |
| `erase_key(obj, key)` | Erase by key from a map/set member; `kind_mismatch` for non-associative members, `no_such_field` when the key is absent. |
| `clear(obj)` | Clear a container member; `kind_mismatch` for non-containers. |
| `size(obj)` | Number of elements in a container member. |
| `for_each(obj, scratch, fn)` | Visit each element; each element is written into the caller-provided scratch node before `fn` runs; returning `false` from `fn` stops iteration early (not an error); `kind_mismatch` when not a container. `fn(index, key, element_node)` — `key` is empty for non-map containers. |

### 4.2 `typed_field_access<T, M>`

Typed accessor for member `M T::*ptr` on type `T`. Uses member-pointer indirection so private members still work when the macro is emitted inside the class (intrusive variant).

```cpp
template <typename T, typename M>
struct typed_field_access : field_access_base {
    M T::*ptr;

    typed_field_access(const char* n, M T::*p, type_id t) noexcept;
    field_value get(const void* obj) const override;
    bool set(void* obj, const field_value& v) const override;
    error_code get_value(const void* obj, value_io* dst) const override;
    error_code set_value(void* obj, const value_io* src) const override;
    error_code get_value_named(const void* obj, value_io* dst,
                               const rtti::detail::naming_policy& np) const override;
    error_code set_value_named(void* obj, const value_io* src,
                               const rtti::detail::naming_policy& np) const override;
    bool is_null_value(const void* obj) const override;
    error_code field_add(void* obj, const value_io* element) const override;
    error_code field_add_scalar(void* obj, const field_value& element) const override;
    error_code erase_at(void* obj, std::size_t index) const override;
    error_code erase_key(void* obj, string_view key) const override;
    error_code clear(void* obj) const override;
    std::size_t size(void* obj) const override;
    bool is_container() const override;
    error_code for_each(void* obj, value_io* scratch,
                        const element_visitor& fn) const override;
};
```

Behavior highlights:

- `get`/`get_value`/`get_value_named` read `o.*ptr`; `set`/`set_value`/`set_value_named` write it (`field_value_to` / `from_value` / `from_value_named` respectively; `member_to_value` / `member_to_value_named` on the read side).
- `is_null_value`: true when the stored value is an empty `std::optional` or a null pointer; everything else is "never null".
- Container dispatch is done with `if constexpr` over `is_map` / `is_set` / `is_sequence` / `is_queue` / `is_stack` / `is_priority_queue`:
  - `field_add`: map → reads element `{key, value}` via `get_key_at(0, ...)`, converts the child through `from_value`, emplaces `{key, value}`; set → converts and inserts; sequence → converts and `push_back`; queue/stack/priority_queue → converts and `push`. Otherwise `kind_mismatch`. The child must not be `nullptr` for maps, else `kind_mismatch`.
  - `field_add_scalar`: set/sequence/queue/stack/priority_queue convert the `field_value` into `value_type` and insert/push_back/push; map → `kind_mismatch`. Non-container → `kind_mismatch`.
  - `erase_at`: sequence only; out-of-range index → `no_such_field`.
  - `erase_key`: map → find by converted key, `no_such_field` if absent; set → parse key from the string form with full-input validation (`from_chars` on integer/signed/unsigned value types; a malformed or out-of-range key is `no_such_field`, never a silent coercion), `no_such_field` if absent; other → `kind_mismatch`.
  - `clear`: map/set/sequence → `.clear()`; queue/stack/priority_queue → drain with `pop()`; other → `kind_mismatch`.
  - `size`: element count for every supported family, else `0`.
  - `for_each`: map → iterates key/value pairs, writes each value into `scratch`, invokes `fn(index, key_view, scratch)`; set/sequence → each element, `fn(index, string_view(), scratch)`; queue → a copy drained with `front()`/`pop()`; stack/priority_queue → a copy drained with `top()`/`pop()`. A conversion error aborts with that `error_code`. Non-container → `kind_mismatch`.

> APIs in this chapter are extracted from `reflection.hpp:407-805`.

---

## 5. Reflective field queries

### 5.1 `field_count<T>()`

```cpp
// Compile-time number of reflected fields. Hard error for unregistered T:
// "incomplete type 'fields_of<T>'".
template <typename T>
constexpr std::size_t field_count() noexcept {
    return fields_of<T>::count;
}
```

The compile-time number of reflected fields (including merged base fields for derived types). For an unregistered `T` this is a hard error ("incomplete type 'fields_of<T>'").

### 5.2 `fields<T>()`

```cpp
// Runtime-iterable array of field accessors for T.
// The array decays to a pointer to its first element (a field_access_base*).
template <typename T>
const field_access_base* const* fields() noexcept {
    return fields_of<T>::array;
}
```

Returns the runtime-iterable array of field accessors; decays to a pointer to its first element. Valid entries cover `[0, field_count<T>())`.

### 5.3 `for_each_field<T, Fn>(fn)` (type-level)

```cpp
// Visit each reflected field of T in table order (merged base fields first).
// fn(const char* name, type_id type). No allocation.
template <typename T, typename Fn>
void for_each_field(Fn&& fn);
```

Visits each reflected field of `T` in table order (merged base fields first), invoking `fn(name, type)`.

### 5.4 `for_each_field<T, Fn>(obj, fn)` (instance-level)

```cpp
// Visit each field of a concrete object with its current value.
// fn(const char* name, type_id type, const field_value& value).
template <typename T, typename Fn>
void for_each_field(T& obj, Fn&& fn);
```

Visits each field of a concrete object, invoking `fn(name, type, current_value)`.

### 5.5 `has_default<T>`

```cpp
// True if the type was registered with a _WITH_DEFAULT macro, meaning missing
// keys keep the member's existing value (used by format fillers).
template <typename T, typename = void>
struct has_default : std::false_type {};

template <typename T>
struct has_default<T, std::void_t<typename T::rtti_has_default>>
    : std::true_type {};
```

True when the type registered with a `_WITH_DEFAULT` macro (i.e. the macro set `rtti_has_default`), meaning missing keys keep the member's existing value during format fills.

### 5.6 Member access level — `member_access`, `field_access_of<T>(index)`

Each reflected field exposes its compile-time access level:

```cpp
enum class member_access : uint8_t { public_ = 1, protected_ = 2, private_ = 3 };

template <typename T>
constexpr member_access field_access_of(std::size_t index) noexcept {
    return fields_of<T>::desc[index].access;
}
```

- `member_access` (reflection.hpp:1399) is the access level of a reflected
  field; the trailing underscore names dodge the `public`/`protected`/`private`
  keywords, and the 1/2/3 values are ABI-stable.
- `field_access_of<T>(i)` (reflection.hpp:3697) returns the `i`-th field's
  access level — `constexpr`, and identical to `fields_of<T>::desc[i].access`.
- For a **non-intrusive** registration (`RTTI_DEFINE_TYPE` /
  `RTTI_DEFINE_TYPE_DERIVED`), the access level is resolved at namespace scope
  by a per-field probe pair (`detail::rtti_access_holder<T>`); the class may
  list private/protected fields when it grants `RTTI_FRIEND_FIELDS(T)`
  (`friend struct ::rtti::fields_of<T>;`) inside its body (§20, §19.4).
- For an **intrusive** registration (`RTTI_DEFINE_TYPE_INTRUSIVE`), private /
  protected fields are reflectable, but the access level cannot be classified
  from inside the class body and every field conservatively reports
  `private_` (the documented boundary).

Reflected private/protected fields are read and written through the same
accessors as public ones (`field_access_base::get/set`, `get_field` /
`set_field`, `from_value` / `to_value`); only the classification is fixed by the
registration mode.

> APIs in this chapter are extracted from `reflection.hpp:814-857`.
> `field_access_of` extracted from `reflection.hpp:3697`; `member_access` from
> `reflection.hpp:1399`.

---

## 6. Attribute / annotation system

Attributes attach extra metadata to a reflected type or to individual fields and are queried at runtime like C# attributes. A type participates by specializing `rtti::attr_table<T>` (type level) and/or `rtti::field_attr_table<T>` (per-field), which the `RTTI_DEFINE_TYPE_ATTRS` / `RTTI_DEFINE_FIELD_ATTRS` macros generate at namespace scope. Attribute declarations are optional: an unregistered type gets a type-safe empty table (count 0, null accesses).

### 6.1 `attr_arg`

One attribute argument: a small tag-union of the four supported payload kinds. String payloads are views (`rtti::string_view`) and must outlive the table; attribute declarations normally pass string literals (static storage), so no ownership or copying is required.

```cpp
class attr_arg {
public:
    enum class kind { none, integer, real, string, boolean };

    attr_arg() noexcept;

    static attr_arg integer(std::uint64_t v) noexcept;
    static attr_arg real(double v) noexcept;
    static attr_arg text(string_view v) noexcept;
    static attr_arg boolean(bool v) noexcept;

    kind type() const noexcept;
    bool is_none() const noexcept;

    // Accessors; each returns a zero/empty fallback on kind mismatch.
    std::uint64_t as_integer() const noexcept;
    double as_real() const noexcept;
    string_view as_string() const noexcept;
    bool as_bool() const noexcept;
};
```

| Member | Meaning |
|---|---|
| `integer/real/text/boolean` | Factories for the four payload kinds. |
| `type()` / `is_none()` | Kind query. |
| `as_*()` | Kind-checked accessors with zero/empty fallbacks on mismatch. |

### 6.2 `attr`

```cpp
// One attribute: a name plus an ordered argument list.
struct attr {
    const char* name = nullptr;
    std::vector<attr_arg> args;
};
```

### 6.3 `field_attr_entry`

```cpp
// One reflected field's attribute list as registered by RTTI_DEFINE_FIELD_ATTRS.
struct field_attr_entry {
    const char* field = nullptr;
    std::vector<attr> attrs;
};
```

### 6.4 `field_attr_list`

```cpp
// Result view returned by field_attr<T>.
struct field_attr_list {
    std::size_t count = 0;
    const attr* items = nullptr;
};
```

### 6.5 `attr_table<T>` and `field_attr_table<T>`

```cpp
// Type-level attribute table. The primary template is an empty table; a
// specialization (via RTTI_DEFINE_TYPE_ATTRS) supplies the registered list.
template <typename T, typename = void>
struct attr_table {
    static constexpr std::size_t count = 0;
    static inline const attr array[1] = {};
};

// Per-field attribute table, keyed by field name, one entry per decorated
// field. Empty by default; specialized via RTTI_DEFINE_FIELD_ATTRS.
template <typename T, typename = void>
struct field_attr_table {
    static constexpr std::size_t count = 0;
    static inline const field_attr_entry array[1] = {};
};
```

### 6.6 Registration detection traits

```cpp
template <typename T, typename = void>
struct type_attr_registered : std::false_type {};
template <typename T>
struct type_attr_registered<T, std::void_t<typename attr_table<T>::registered>>
    : std::true_type {};

template <typename T, typename = void>
struct field_attr_registered : std::false_type {};
template <typename T>
struct field_attr_registered<T,
                             std::void_t<typename field_attr_table<T>::registered>>
    : std::true_type {};

// True when the type participates in attribute reflection at all.
template <typename T>
inline constexpr bool attrs_defined() noexcept {
    return type_attr_registered<T>::value || field_attr_registered<T>::value;
}
```

`type_attr_registered<T>` / `field_attr_registered<T>` are true when the type registered an attribute table (marker `registered` typedef). `attrs_defined<T>()` is the combined query.

### 6.7 `make_attr(name, args...)` and `make_field_attr(field, attrs...)`

```cpp
// Runtime factories. The variadic overloads are the public construction API;
// the initializer-list overloads are what the RTTI_ATTR / RTTI_FIELD_ATTR_ENTRY
// macros expand to (a braced list is immune to preprocessor comma splitting).
template <typename... Args>
inline attr make_attr(const char* name, const Args&... args);

inline attr make_attr(const char* name, std::initializer_list<attr_arg> args);

template <typename... A>
inline field_attr_entry make_field_attr(const char* field, const A&... attrs);

inline field_attr_entry make_field_attr(const char* field,
                                        std::initializer_list<attr> attrs);
```

- `make_attr(name, args...)` builds an `attr`; each argument goes through `detail::make_attr_arg`.
- `detail::make_attr_arg(v)` (namespace `rtti::detail`) maps an argument expression to the matching `attr_arg` kind: `bool` → `boolean`; integral → `integer` (sign-normalized then widened to `std::uint64_t`); floating → `real`; `const char*` / `char*` / `string_view` / `std::string` → `string`. Unhandled types fail to compile with a `static_assert`.
- The initializer-list overloads exist because a braced list is immune to preprocessor comma splitting.

### 6.8 Type-level attribute queries

```cpp
template <typename T>
std::size_t attr_count() noexcept;

template <typename T>
const attr* attr_at(std::size_t index) noexcept;

template <typename T>
const attr* find_attr(string_view name) noexcept;

template <typename T>
bool has_attr(string_view name) noexcept;
```

| Function | Semantics |
|---|---|
| `attr_count<T>()` | Number of registered type attributes. |
| `attr_at<T>(index)` | Pointer to the `index`-th attribute, or `nullptr` when out of range. |
| `find_attr<T>(name)` | Pointer to the attribute with the given name, or `nullptr`. |
| `has_attr<T>(name)` | `true` iff `find_attr<T>(name) != nullptr`. |

### 6.9 Field-level attribute queries

```cpp
template <typename T>
field_attr_list field_attr(string_view field_name) noexcept;

template <typename T>
field_attr_list field_attr(std::size_t field_index) noexcept;

template <typename T>
std::size_t field_attr_count(string_view field_name) noexcept;

template <typename T>
std::size_t field_attr_count(std::size_t field_index) noexcept;

template <typename T>
const attr* field_attr_at(string_view field_name, std::size_t index) noexcept;

template <typename T>
const attr* field_attr_at(std::size_t field_index, std::size_t index) noexcept;

template <typename T>
const attr* field_find_attr(string_view field_name, string_view name) noexcept;

template <typename T>
const attr* field_find_attr(std::size_t field_index, string_view name) noexcept;

template <typename T>
bool field_has_attr(string_view field_name, string_view name) noexcept;

template <typename T>
bool field_has_attr(std::size_t field_index, string_view name) noexcept;
```

| Function | Semantics |
|---|---|
| `field_attr<T>(field_name)` | The attribute list of the named field (empty instance when the field has none). |
| `field_attr<T>(field_index)` | Same, by field index; an out-of-range index returns an empty `field_attr_list`. |
| `field_attr_count<T>(...)` | Number of attributes on the field. |
| `field_attr_at<T>(field..., index)` | `index`-th attribute of the field, or `nullptr` when out of range. |
| `field_find_attr<T>(field..., name)` | The field's attribute with the given name, or `nullptr`. |
| `field_has_attr<T>(field..., name)` | `true` iff `field_find_attr` succeeds. |

Example (illustrative; requires a full registration):

```cpp
RTTI_DEFINE_TYPE_ATTRS(Point2D,
    RTTI_ATTR("color", "red"),
    RTTI_ATTR("units", 1));
assert(rtti::attr_count<Point2D>() == 2);
assert(rtti::has_attr<Point2D>("color"));
assert(rtti::find_attr<Point2D>("color")->args[0].as_string() == "red");
```

> APIs in this chapter are extracted from `reflection.hpp:859-1163`.

---

## 7. Enum reflection

Enum support is self-contained in this header. A type participates in enum reflection iff `rtti::enum_table<E>` is specialized (by `RTTI_DEFINE_ENUM`).

### 7.1 `enum_label<E>` and `enum_table<E>`

```cpp
template <typename E>
struct enum_label {
    const char* name;
    E value;
};

// Enum label table. A type participates in enum reflection iff this template
// is specialized (by RTTI_DEFINE_ENUM).
template <typename E>
struct enum_table;
```

A registered `enum_table<E>` exposes `count` and `array` (an array of `enum_label<E>`) in registration order.

### 7.2 `enum_to_string<E>(value)` / `enum_from_string<E>(name)`

```cpp
template <typename E>
const char* enum_to_string(E value) noexcept;

template <typename E>
E enum_from_string(string_view name) noexcept;
```

| Function | Semantics |
|---|---|
| `enum_to_string<E>(value)` | The label name for `value`, or `""` when no enumerator matches. |
| `enum_from_string<E>(name)` | The enumerator value for `name`, or `static_cast<E>(0)` when no label matches. |

### 7.3 `enum_count`, `enum_name_at`, `enum_value_at`, `for_each_enum`

```cpp
template <typename E>
constexpr std::size_t enum_count() noexcept;

template <typename E>
const char* enum_name_at(std::size_t i) noexcept;

template <typename E>
E enum_value_at(std::size_t i) noexcept;

template <typename E, typename Fn>
void for_each_enum(Fn&& fn);
```

| Function | Semantics |
|---|---|
| `enum_count<E>()` | Number of registered enumerators (registration order). |
| `enum_name_at<E>(i)` | Name of the `i`-th enumerator, or `nullptr` when out of range. |
| `enum_value_at<E>(i)` | Value of the `i`-th enumerator, or `E{}` when out of range. |
| `for_each_enum<E>(fn)` | Invokes `fn(name, value)` for each enumerator in registration order. |

Example:

```cpp
enum class Color { Red, Green, Blue };
RTTI_DEFINE_ENUM(Color, Red, Green, Blue)   // namespace scope
assert(rtti::enum_count<Color>() == 3);
assert(rtti::enum_to_string<Color>(Color::Green) == std::string_view("Green"));
assert(rtti::enum_from_string<Color>("Blue") == Color::Blue);
```

> APIs in this chapter are extracted from `reflection.hpp:1268-1323`.

---

## 8. Type description system

`rtti::type_of<T>()` returns a stable, lazily-created `type_desc` describing the category of `T`: scalar, string, one of the container families (including queue/stack/priority_queue), a reflected struct (with its field table), or an enum. Nested element/key and field types are resolved on demand through resolver function pointers, so self-referential structures cannot recurse forever at construction time.

### 8.1 `type_kind` and `container_family`

```cpp
enum class type_kind { boolean, integer, real, string, container, reflected, enum_type };

enum class container_family {
    vector, list, map, unordered_map, set, unordered_set,
    queue, stack, priority_queue
};
```

### 8.2 Resolver types and field/container/struct/enum descriptors

```cpp
struct type_desc;
struct container_desc;
struct struct_desc;
struct enum_desc;

using type_resolver = const type_desc* (*)();

// Compile-time access level of a reflected field. The trailing underscore names
// dodge the public/protected/private keywords. Values 1/2/3 are ABI-stable.
enum class member_access : uint8_t { public_ = 1, protected_ = 2, private_ = 3 };

// One field of a reflected struct: name plus an on-demand type resolver, plus
// the compile-time member-access level (public_ / protected_ / private_).
struct field_type_desc {
    const char* name;
    type_resolver resolve;
    member_access access = member_access::private_;

    // Resolves (and caches via the target's static) the field's type_desc.
    const type_desc* type() const noexcept { return resolve(); }
};

struct container_desc {
    container_family family;
    type_resolver key;       // null except for the map family
    type_resolver element;
};

struct struct_desc {
    std::size_t field_count;
    const field_type_desc* fields;
};

// Offset-correct base-pointer adjuster: converts a pointer to the owning type
// into a pointer to the base subobject.
using base_adjust_fn = void* (*)(void*);

// Direct base types of a reflected type (spec 19): an array of lazy
// resolvers, one per registered base, in declaration order.
struct base_type_list {
    std::size_t count;
    const type_resolver* resolve;   // array of `count` lazy resolvers
    base_adjust_fn const* adjust;   // array of `count` adjusters (parallel)
};

struct enum_desc {
    std::size_t count;
    const char* const* names;
    const void* values = nullptr;   // parallel array of E values (raw bytes)
    std::size_t value_size = 0;     // sizeof(E)
};
```

Each `field_type_desc` row carries a compile-time `member_access` (
`public_` = 1 / `protected_` = 2 / `private_` = 3, `reflection.hpp:1399`).
The access level is also reachable through the public query
`rtti::field_access_of<T>(index)` (`reflection.hpp:3697`), and it is
evaluated at namespace scope through a per-field probe pair, never from a
friend context — see §20.

### 8.3 `type_desc`

```cpp
struct type_desc {
    type_kind kind;
    const std::type_info* id;   // typeid(T)
    const char* type_name;      // typeid(T).name()
    const container_desc* as_container;
    const struct_desc* as_reflected;
    const enum_desc* as_enum;
    const base_type_list* as_bases = nullptr;  // direct bases (spec 19)

    bool abstract = false;
    bool polymorphic = false;

    bool is_abstract() const noexcept { return abstract; }
    bool is_polymorphic() const noexcept { return polymorphic; }
    const base_type_list* bases() const noexcept { return as_bases; }
};
```

| Member | Meaning |
|---|---|
| `kind` | Category of `T`. |
| `id` | `typeid(T)`. |
| `type_name` | `typeid(T).name()`. |
| `as_container` / `as_reflected` / `as_enum` | Family-specific descriptor, non-null only for the matching `kind`. |
| `as_bases` / `bases()` | The direct base types; `nullptr` when none are registered. |
| `abstract` / `polymorphic` | Compile-time classification captured from `std::is_abstract` / `std::is_polymorphic` when the descriptor is built by `type_of<T>`; defaults keep scalar/string/container/enum categories safe (`false`). |
| `is_abstract()` | True when `T` is an abstract class (has pure virtual functions) and thus cannot be instantiated. |
| `is_polymorphic()` | True when `T` is polymorphic (has at least one virtual function, i.e. a vtable); a prerequisite for the runtime dynamic checks (spec 19/20). |

### 8.4 `type_of<T>()` and `type_of(obj)`

```cpp
template <typename T>
const type_desc* type_of() noexcept;

// Value overload: rtti::type_of(obj).
template <typename T>
const type_desc* type_of(const T&) noexcept;
```

Returns a stable, lazily-created `type_desc*` for `T`. Category mapping in `detail::type_of_impl` (in order):

1. `bool` → `kind::boolean`
2. `T` integral → `kind::integer`
3. `T` floating point → `kind::real`
4. `std::string` / `string_view` → `kind::string`
5. `is_map<T>` → `kind::container` with `container_desc{map_family<T>(), key_resolver, mapped_type_resolver}`
6. `is_set<T>` → `kind::container` with `container_desc{set_family<T>(), nullptr, value_type_resolver}`
7. `is_sequence<T>` → `kind::container` with `container_desc{sequence_family<T>(), nullptr, value_type_resolver}`
8. `is_priority_queue<T>` → `kind::container` family `priority_queue`
9. `is_queue<T>` → `kind::container` family `queue`
10. `is_stack<T>` → `kind::container` family `stack`
11. `is_reflected<T>` → `kind::reflected` with `struct_desc{field_count<T>(), fields_of<T>::desc}`, plus `base_type_list_of<T>()` and abstract/polymorphic flags. The descriptor is registered once (on first materialization) in the process-wide `detail::desc_registry` so the type-erased `instance` cast can resolve a runtime dynamic type.
12. `std::is_enum<T>` → `kind::enum_type` with `enum_desc` built from `enum_table<T>::count`, `enum_names<T>()`, `enum_values<T>()`, `sizeof(T)`.
13. Otherwise (unknown/unregistered type) → reported with `kind::string` only as a safe placeholder; the `abstract` / `polymorphic` class flags are still captured truthfully.

The returned pointer is stable for a given `T` (function-local static); the `const T&` overload simply forwards to `type_of<T>()`.

Example:

```cpp
assert(rtti::type_of<int>()->kind == rtti::type_kind::integer);
struct Point { int x; int y; };
RTTI_DEFINE_TYPE(Point, x, y)              // namespace scope
const rtti::type_desc* d = rtti::type_of<Point>();
assert(d->kind == rtti::type_kind::reflected);
assert(d->as_reflected->field_count == 2);
```

### 8.5 Process-wide descriptor registry (detail)

Internal but observable through the `instance` cast path:

```cpp
inline std::vector<const type_desc*>& desc_registry();
inline std::mutex& desc_registry_mutex();
inline void register_desc(const type_desc* d) noexcept;
inline const type_desc* find_desc(const std::type_info& ti) noexcept;
```

`register_desc` skips nulls and deduplicates by `std::type_info` equality under a mutex; `find_desc` looks a dynamic type up. Used to re-root the base-chain walk for the full-precision `instance::as`.

> APIs in this chapter are extracted from `reflection.hpp:1325-1420` and `reflection.hpp:1509-1704`.

---

## 9. Compile-time type judgments

Thin `rtti`-namespace aliases over the standard traits so callers stay inside one namespace, mirroring `is_same` / `is_base_of` / `is_convertible` in `reflect.hpp`.

```cpp
template <typename T>
struct is_abstract : std::is_abstract<T> {};
template <typename T>
inline constexpr bool is_abstract_v = is_abstract<T>::value;

template <typename T>
struct is_polymorphic : std::is_polymorphic<T> {};
template <typename T>
inline constexpr bool is_polymorphic_v = is_polymorphic<T>::value;

template <typename T>
struct has_virtual_destructor : std::has_virtual_destructor<T> {};
template <typename T>
inline constexpr bool has_virtual_destructor_v = has_virtual_destructor<T>::value;
```

| Trait | Meaning |
|---|---|
| `is_abstract<T>` / `is_abstract_v<T>` | True when `T` has pure virtual functions (cannot be instantiated). |
| `is_polymorphic<T>` / `is_polymorphic_v<T>` | True when `T` has at least one virtual function (has a vtable). |
| `has_virtual_destructor<T>` / `has_virtual_destructor_v<T>` | True when `T` has a virtual destructor. |

> APIs in this chapter are extracted from `reflection.hpp:1422-1441`.

---

## 10. Family discrimination and enum helpers

### 10.1 `has_hasher<T>`, `has_data<T>`, `has_bracket<T>`

```cpp
template <typename T, typename = void>
struct has_hasher : std::false_type {};
template <typename T>
struct has_hasher<T, std::void_t<typename T::hasher>> : std::true_type {};

template <typename T, typename>
struct has_data : std::false_type {};
template <typename T>
struct has_data<T, std::void_t<decltype(std::declval<T&>().data())>> : std::true_type {};

template <typename T, typename = void>
struct has_bracket : std::false_type {};
template <typename T>
struct has_bracket<T, std::void_t<decltype(std::declval<T&>()[size_t(0)])>> : std::true_type {};
```

| Trait | Meaning |
|---|---|
| `has_hasher<T>` | True when `T` exposes a `hasher` member type (unordered containers). |
| `has_data<T>` | True when `T` has a `data()` member (contiguous storage: vector, `std::array`, `std::string`, …). |
| `has_bracket<T>` | True when `T` supports `operator[]` on `size_t`. |

### 10.2 `map_family`, `set_family`, `sequence_family`

```cpp
template <typename T>
constexpr container_family map_family() noexcept {
    return has_hasher<T>::value ? container_family::unordered_map : container_family::map;
}
template <typename T>
constexpr container_family set_family() noexcept {
    return has_hasher<T>::value ? container_family::unordered_set : container_family::set;
}
template <typename T>
constexpr container_family sequence_family() noexcept {
    if constexpr (has_data<T>::value) {
        return container_family::vector;
    } else if constexpr (has_bracket<T>::value) {
        return container_family::vector;  // deque treated as vector-family
    } else {
        return container_family::list;
    }
}
```

- `map_family<T>()`: unordered (a `hasher`) vs ordered map.
- `set_family<T>()`: unordered vs ordered set.
- `sequence_family<T>()`: vector has `data()`; deque has `operator[]` but no `data()` (treated as vector-family); list has neither → `list`.

### 10.3 `enum_names`, `enum_values` and helpers

```cpp
template <typename E, std::size_t... I>
const char* const* enum_name_array(std::index_sequence<I...>);

template <typename E>
const char* const* enum_names();

template <typename E, std::size_t... I>
const void* enum_value_array(std::index_sequence<I...>);

template <typename E>
const void* enum_values();
```

- `enum_name_array<E>(seq)` builds a null-terminated name array for a registered enum.
- `enum_names<E>()` returns that array (used to build the runtime `enum_desc`).
- `enum_value_array<E>(seq)` / `enum_values<E>()` return the parallel raw-byte value array; `enum_values<E>()` returns `nullptr` when the enum registers zero enumerators.

> APIs in this chapter are extracted from `reflection.hpp:1443-1507`.

---

## 11. Template-argument reflection

C++17 cannot introspect the type arguments of an arbitrary template instantiation, so the argument list is registered explicitly: specializing `rtti::template_info<T>` (via the `RTTI_TEMPLATE_INFO` macro) stores the argument types as a `std::tuple` plus their count. Unregistered template instances (and non-template types) fall back to the primary template's safe empty table: count 0, empty tuple, nullptr array.

### 11.1 `template_info<T>`

```cpp
template <typename T, typename = void>
struct template_info {
    static constexpr std::size_t count = 0;
    using args = std::tuple<>;
};
```

A registered specialization additionally declares `using registered = void;`.

### 11.2 `is_template_instance<T>`

```cpp
template <typename T, typename = void>
struct is_template_instance : std::false_type {};
template <typename T>
struct is_template_instance<T, std::void_t<typename template_info<T>::registered>>
    : std::true_type {};
template <typename T>
inline constexpr bool is_template_instance_v = is_template_instance<T>::value;
```

Marker-based detection: true when the `template_info<T>` specialization carries `registered`. The safe empty primary template does **not** count as registered.

### 11.3 Registration queries

```cpp
template <typename T>
constexpr std::size_t template_arg_count() noexcept;

template <typename T>
using template_args = typename template_info<T>::args;

template <typename T, std::size_t I>
struct template_arg_at_holder {
    static_assert(I < template_info<T>::count,
                  "RTTI: template argument index out of range");
    using type = std::tuple_element_t<I, template_args<T>>;
};
template <typename T, std::size_t I>
using template_arg_at = typename template_arg_at_holder<T, I>::type;
```

| Member | Meaning |
|---|---|
| `template_arg_count<T>()` | Number of registered template arguments (0 for non-templates). |
| `template_args<T>` | The template argument types as a `std::tuple` (empty for unregistered). |
| `template_arg_at<T, I>` | The `I`-th template argument type; an out-of-range/unregistered index is a hard compile error (`static_assert` "RTTI: template argument index out of range"). |

### 11.4 Runtime descriptor access

```cpp
template <typename T>
const type_desc* const* template_args_of() noexcept;

template <typename T>
const type_desc* template_arg_desc_of(std::size_t index) noexcept;

template <typename T, typename Fn>
void for_each_template_arg(Fn&& fn);
```

| Function | Semantics |
|---|---|
| `template_args_of<T>()` | Runtime array of `type_desc*`, one per registered argument, in declaration order. `nullptr` (safe empty table) for unregistered templates. |
| `template_arg_desc_of<T>(index)` | The `type_desc` of the `I`-th argument or `nullptr` when unregistered / out of range. |
| `for_each_template_arg<T>(fn)` | Invokes `fn(std::size_t index, const type_desc* arg_type)` for each registered argument in declaration order. |

Example:

```cpp
template <typename A, typename B> struct Box { A a; B b; };
RTTI_TEMPLATE_INFO(Box, int, std::string)     // namespace scope
assert(rtti::is_template_instance_v<Box<int, std::string>>);
assert(rtti::template_arg_count<Box<int, std::string>>() == 2);
using T0 = rtti::template_arg_at<Box<int, std::string>, 0>;  // int
```

> APIs in this chapter are extracted from `reflection.hpp:1706-1804`.

---

## 12. Derived-type merge machinery (concepts)

Spec 19. The macro family emits, for every registered type, a `members` tuple holding the typed member pointers of the type's OWN reflected fields (in declaration order), next to the `base_types<T>` list. A derived type registered with `RTTI_DEFINE_TYPE_DERIVED` merges each base's fields into its table through D-anchored accessors: the base member pointer is converted to a member pointer of the derived type (`M Base::*` → `M Derived::*`), so member-pointer dispatch performs the correct subobject offset for virtual and non-virtual bases alike. The merged accessor/descriptor arrays are built once into function-local statics; the base tables feeding them are constant-initialized, so the build is safe under any static-initialization order.

Public observable behavior:

- `fields<D>()` for a derived type returns the merged table with **base fields first**, then the derived type's own fields.
- `field_count<D>()` equals `base_count + own_count`.
- The `type_desc` of a derived type carries `base_type_list` (direct bases) via `as_bases`/`bases()`, registered through `detail::register_desc` so the type-erased `instance` cast can walk it.
- `is_reflected<D>()`, `is_field` queries and `type_of<D>()` all work on merged tables.

The machinery is implemented in `namespace rtti::detail`:

- `member_pointer_traits<M C::*>` → `{value = M, owner = C}`.
- `base_total_impl<...>` / `base_total_seq<Bs, I...>` / `base_total<Bs>()` — sum of `fields_of<B>::count` over the base tuple (each base's count already recursively merged).
- `base_field_count<T>()` — base-contributed field count; `own_member_count<T>()` — the macro's own field count (`std::tuple_size<fields_of<T>::members_type>`).
- `for_each_base<Bs>(fn)` — invoke `fn` with each base type in the tuple; a no-op for an empty tuple.
- `derived_accessor<D, C, OwnIndex>()` — the D-anchored accessor for the `OwnIndex`-th own field of base type `C`: a `typed_field_access<D, M>` over the converted member pointer (a `static_assert(is_base_of<C, D>)` guards misuse).
- `derived_field_appender<D, C>` / `derived_desc_appender<D, C>` — recursively append C's own bases' fields, then C's own fields (base-first ordering identical to `fields_of<C>`).
- `derived_pool<D>::accessors()` / `derived_pool<D>::descriptors()` — once-built merged tables (magic statics, thread-safe), returned for `fields<D>()` / `desc`.

> APIs in this chapter are extracted from `reflection.hpp:1806-2010`.

---

## 13. Reflective construction

A type opts into reflective construction by specializing `rtti::ctor_registry<T>` (usually via the `RTTI_DEFINE_CTOR` macro, mirroring the explicit proto schema specialization style). `make_object<T>` / `create_instance<T>` live in `<rtti/reflect.hpp>`; this header supplies the parameter descriptors, the runtime argument holder, the factory glue and the instance handle.

### 13.1 `ctor_arg`

One runtime constructor argument. A value comes from a scalar `field_value`, a `value_io` node (nested/container/scalar), or a parameter name matching a registered ctor parameter name. Arguments are matched to parameters by name when **any** argument carries a name, otherwise by position; named arguments fill their parameter and the remaining parameters receive the unnamed ("positional") arguments in declaration order (mixed mode).

```cpp
class ctor_arg {
public:
    ctor_arg() noexcept = default;

    static ctor_arg scalar(const field_value& v) noexcept;
    static ctor_arg scalar(const char* name, const field_value& v) noexcept;
    static ctor_arg node(const value_io* v) noexcept;
    static ctor_arg node(const char* name, const value_io* v) noexcept;

    const char* name() const noexcept;
    bool named() const noexcept;
    bool is_node() const noexcept;
    bool is_scalar() const noexcept;
    const field_value& scalar() const noexcept;
    const value_io* node() const noexcept;
};
```

| Member | Meaning |
|---|---|
| `scalar(v)` / `scalar(name, v)` | A scalar payload (optionally named). |
| `node(v)` / `node(name, v)` | A `value_io` source (nested/container/scalar), optionally named. |
| `name()` | Parameter name or `nullptr`. |
| `named()` | `true` iff `name() != nullptr`. |
| `is_node()` / `is_scalar()` | `node_ != nullptr` vs scalar payload. |
| `scalar()` / `node()` | Payload accessors. |

### 13.2 `ctor_param`

```cpp
// One constructor parameter descriptor of a ctor registry.
struct ctor_param {
    const char* name;
    type_id type;      // typeid of the C++ parameter type
    bool optional;     // omitted args fall back to def (or a value-init value)
    field_value def;   // optional default; empty (kind none) -> value-init
};
```

### 13.3 `ctor_registry<T>` and `has_ctor_registry<T>`

```cpp
template <typename T, typename = void>
struct ctor_registry {
    static constexpr std::size_t count = 0;
    static inline const ctor_param params[1] = {{nullptr, nullptr, false, {}}};
    using param_types = std::tuple<>;
    using factory_fn = T* (*)(const ctor_arg*, std::size_t, error_code&);
    static inline factory_fn factory = nullptr;
};

template <typename T, typename = void>
struct has_ctor_registry : std::false_type {};
template <typename T>
struct has_ctor_registry<T, std::void_t<typename ctor_registry<T>::param_types>>
    : std::bool_constant<(ctor_registry<T>::count > 0)> {};
```

- The primary `ctor_registry<T>` is an empty, safe table: `make_object<T>(args)` reports an error for an unregistered type.
- A specialization (via `RTTI_DEFINE_CTOR` or by hand) supplies the ordered parameter descriptors (`params`), the parameter type list (`param_types` as `std::tuple`), and a `factory` that builds `T` from the matched runtime arguments (`factory_fn = T*(*)(const ctor_arg*, std::size_t, error_code&)`).
- `has_ctor_registry<T>` is true when a non-empty constructor table was registered.

### 13.4 Runtime argument/parameter matching (detail)

The internal machinery (`namespace rtti::detail`) implements spec 18.3 matching:

- `is_scalar_param<D>` — true when a parameter is filled from a scalar `field_value` (integral, floating, enum, `string_view`, `std::string`). Other parameter types require a `value_io` node and are filled through `rtti::from_value` (containers, reflected structs, strings via nodes, optional/variant/tuple, …).
- `scalar_assign<D>(out, v)` — lenient for `bool` (accepts integer 0/1); enums come from the integer lane; everything else uses the strict `field_value_to` lanes.
- `node_to_field_value(node, out)` — reads a scalar out of a `value_io` node; `false` for null/array/object.
- `ctor_arg_to<D>(a, out)` — converts one matched argument into the destination parameter type.
- `arg_matcher` — counts named/positional arguments, `take_positional()` supplies the next unused positional, `find_named(name)` matches a named argument.
- `assign_param<I, Tuple>` — fills the `I`-th parameter: match by name (when any argument is named), else positionally; an omitted optional parameter falls back to its registered default (or a value-initialized value); a missing required parameter is `missing_key`.
- `ctor_values` — validates the registry and converts all arguments up front; unknown parameter names are rejected with `kind_mismatch` (a typo beats a missing-param report); every named and positional argument must be consumed, and pure-positional mode accepts at most one argument per parameter.
- `ctor_new_impl<T, Tuple>` — heap-constructs `T` from the converted value tuple.
- `ctor_place_impl<T, Tuple>(raw, values, ...)` — placement-constructs `T` into preallocated raw memory (`::new (raw) T(...)`).
- `ctor_prepare<T, Tuple>` — validates the table (`kind_mismatch` for an unregistered/inconsistent table) then fills `values`.
- `ctor_factory<T, Tuple>(args, count, ec)` — the generic registry factory entry point referenced by `RTTI_DEFINE_CTOR`; returns a heap `T*` or `nullptr` with the `error_code` set.

> APIs in this chapter are extracted from `reflection.hpp:2012-2397`.

---

## 14. Type conversion: `as` / `cast`

C#-style reference conversions.

- `as<T>(p)`: `nullptr` on failure; **no error payload**.
- `cast<T>(p)`: `result<T*>`, `kind_mismatch` on failure, `no_such_field` for a null source.
- Covariant upcasts use the compiler's offset-correct `static_cast`, including virtual bases and multiple inheritance (see `detail::base_adjuster`).
- Contravariant downcasts require the static source type to be polymorphic (`dynamic_cast`); a non-polymorphic source cannot be checked at runtime and fails cleanly (`nullptr` / `kind_mismatch`).
- Unrelated polymorphic types may still succeed through a `dynamic_cast` cross-cast.
- All entry points are failure-safe; **const-qualified source or target types are rejected at compile time** via `static_assert`.

```cpp
template <typename T, typename U>
T* as(U* p) noexcept {
    static_assert(!std::is_const<T>::value && !std::is_const<U>::value,
        "rtti::as/cast do not accept const-qualified source or target types");
    // p == nullptr → nullptr
    // is_base_of<T, U>    → covariant (or same type): static_cast via
    //                       detail::base_adjuster<U, T>::run on the void* source.
    // is_base_of<U, T>    → contravariant downcast: dynamic_cast<T*>(p) when
    //                       is_polymorphic<U>, else nullptr.
    // unrelated           → dynamic_cast<T*>(p) when both are polymorphic,
    //                       else nullptr.
}

template <typename T, typename U>
result<T*> cast(U* p) {
    // p == nullptr → r.err.code = no_such_field
    // q = as<T>(p); q == nullptr → r.err.code = kind_mismatch
    // else → r.value = q
}
```

Semantics:

| Case | `as<T>(p)` | `cast<T>(p)` |
|---|---|---|
| `p == nullptr` | `nullptr` | `no_such_field` |
| `is_base_of<T, U>` (upcast / same type) | offset-correct `static_cast` | pointer |
| `is_base_of<U, T>` (downcast), `U` polymorphic | `dynamic_cast<T*>(p)` | pointer or `kind_mismatch` |
| `is_base_of<U, T>`, `U` not polymorphic | `nullptr` | `kind_mismatch` |
| unrelated, both polymorphic | `dynamic_cast<T*>(p)` | pointer or `kind_mismatch` |
| unrelated, not both polymorphic | `nullptr` | `kind_mismatch` |
| `T` or `U` const-qualified | compile error (`static_assert`) | compile error (`static_assert`) |

Example:

```cpp
struct Base { virtual ~Base() = default; };
struct Derived : Base {};
Derived d;
Base* b = &d;
assert(rtti::as<Derived>(b) == &d);   // downcast, polymorphic source
assert(!rtti::cast<int>(b).ok());     // kind_mismatch
```

> APIs in this chapter are extracted from `reflection.hpp:2399-2455`.

---

## 15. `object_ref<T>` instance handle

Handle to a reflectively-created `T` instance (spec 18.2). Two lifetime modes:

- **RAII (default)**: the handle owns the heap instance and destroys it on its own destruction (created by `make_object<T>` / `make_object<T>(args)`).
- **Manual (allocator path)**: the caller owns the raw memory (`make_object<T>` with an allocator); `destroy()` must be called explicitly. The handle destructor leaves the manual instance untouched, preventing double frees.

All operations delegate to the reflect core (field table, `to_value` / `from_value`, attributes, `type_of`); no format adapter is needed to build a handle.

```cpp
template <typename T>
class object_ref {
public:
    using value_type = T;

    object_ref() noexcept = default;
    ~object_ref() noexcept;
    object_ref(object_ref&& o) noexcept;
    object_ref& operator=(object_ref&& o) noexcept;
    object_ref(const object_ref&) = delete;
    object_ref& operator=(const object_ref&) = delete;

    T* get() noexcept;
    const T* get() const noexcept;
    T* operator->() noexcept;
    const T* operator->() const noexcept;
    T& operator*() noexcept;
    const T& operator*() const noexcept;
    bool valid() const noexcept;
    explicit operator bool() const noexcept;

    // C#-style variance conversions: see rtti::as / rtti::cast.
    template <typename Target>
    Target* as() noexcept;
    template <typename Target>
    result<Target*> cast() noexcept;
    template <typename Target>
    Target* as() const noexcept;
    template <typename Target>
    result<Target*> cast() const noexcept;

    void destroy() noexcept;

    // --- Construction helpers (advanced) ---
    static object_ref adopt(T* p) noexcept;
    static object_ref placement(T* p, std::function<void(void*)> release) noexcept;

    // --- Field get/set (spec 18.2) ---
    error_code get(const char* name, field_value& out) const noexcept;
    error_code set(const char* name, const field_value& v) noexcept;

    // --- Serialization bridge (format-agnostic value_io path) ---
    error_code to_value(value_io* dst) const;
    error_code from_value(const value_io* src);
    error_code to_value(value_io* dst, const rtti::detail::naming_policy& np) const;
    error_code from_value(const value_io* src, const rtti::detail::naming_policy& np);

    // --- Container operations (spec 18.2) ---
    error_code add(const char* name, const value_io* element) noexcept;
    error_code add(const char* name, const field_value& element) noexcept;
    error_code erase_at(const char* name, std::size_t index) noexcept;
    error_code erase_key(const char* name, string_view key) noexcept;
    error_code clear(const char* name) noexcept;
    std::size_t field_size(const char* name, error_code* out_err = nullptr) const noexcept;
    template <typename Fn>
    error_code for_each_element(const char* name, value_io* scratch, Fn&& fn) const noexcept;
    // Element type of a container field (spec 18.2 type query).
    const type_desc* element_type(const char* name) const noexcept;

    // --- Attributes (spec 18.2 / section 17) ---
    std::size_t attr_count() const noexcept;
    bool has_attr(string_view name) const noexcept;
    const attr* find_attr(string_view name) const noexcept;
    field_attr_list field_attr(string_view name) const noexcept;
    bool field_has_attr(string_view name, string_view attribute) const noexcept;

    // --- Type query (spec 18.2) ---
    const type_desc* type_of() const noexcept;
};
```

| Member | Semantics |
|---|---|
| `valid()` / `operator bool` | `p_ != nullptr`. |
| `get()` / `operator->` / `operator*` | Raw access to the wrapped instance. |
| `as<Target>()` / `cast<Target>()` | Variance conversions on the wrapped pointer (see §14); the returned pointer borrows the handle's object. |
| `destroy()` | Manual-mode destroy: runs the destructor and returns the raw memory to the allocator's releaser. Idempotent (a second call is a no-op). In RAII mode this releases the owned instance. |
| `adopt(p)` | Adopt a heap instance (RAII mode; ownership transfers to the handle). |
| `placement(p, release)` | Wrap a placement-constructed instance (manual mode). `release` (optional, owner-supplied) returns the raw memory after `destroy()` ran the destructor; arena/monotonic allocators pass no releaser. |
| `get(name, out)` | Read a reflected field by name; `no_such_field` when the field is absent. |
| `set(name, v)` | Write a reflected field by name; `no_such_field` if absent, `kind_mismatch` when the value kind does not fit. |
| `to_value(dst)` / `from_value(src)` | Serialize through the format-agnostic `value_io` bridge. |
| `to_value / from_value(dst/src, np)` | Named-conversion variants honoring the naming policy. |
| `add(name, element)` | Append an element (from a `value_io` node) to a container field; `no_such_field` if absent. |
| `add(name, element)` | Append one scalar element to a container field (map unsupported → `kind_mismatch`). |
| `erase_at(name, index)` / `erase_key(name, key)` | Erase by index/key from a container field. |
| `clear(name)` | Clear a container field. |
| `field_size(name, out_err)` | Element count of a container field; sets `out_err` when provided (`no_such_field` / `kind_mismatch`) and returns 0 on failure. |
| `for_each_element(name, scratch, fn)` | Visit each element (with a caller-provided scratch node); `fn` is adapted to `field_access_base::element_visitor`. |
| `element_type(name)` | The element type descriptor of a container field, or `nullptr` if the field is absent/not a container. |
| `attr_count` / `has_attr` / `find_attr` / `field_attr` / `field_has_attr` | Attribute queries delegated to the type-level functions of §6. |
| `type_of()` | `rtti::type_of<T>()`. |

Example:

```cpp
// Illustrative: requires make_object<T> (reflect.hpp) and registration.
rtti::object_ref<MyType> obj = rtti::make_object<MyType>();
rtti::field_value v;
if (obj.get("x", v) == rtti::error_code::ok) { /* ... */ }
if (obj.set("x", rtti::field_value::of(int64_t(7))) == rtti::error_code::ok) { /* ... */ }
```

> APIs in this chapter are extracted from `reflection.hpp:2457-2717`.

---

## 16. `instance` type-erased handle

Type-erased instance handle for heterogeneous ORM pools (spec 18.2). Wraps any `object_ref<T>` and exposes a small uniform operation set: type and descriptor query, field get/set by name, and the `value_io` serialization bridge. Move-only; an explicit `destroy()` forwards to the wrapped handle.

```cpp
class instance {
public:
    instance() noexcept = default;
    ~instance() noexcept;
    instance(instance&& o) noexcept;
    instance& operator=(instance&& o) noexcept;
    instance(const instance&) = delete;
    instance& operator=(const instance&) = delete;

    template <typename T>
    static instance from(object_ref<T> handle);

    type_id type() const noexcept;
    const type_desc* type_of() const noexcept;
    bool valid() const noexcept;
    explicit operator bool() const noexcept;

    template <typename T>
    bool is() const noexcept;

    void* data() noexcept;
    const void* data() const noexcept;

    error_code get(const char* name, field_value& out) const noexcept;
    error_code set(const char* name, const field_value& v) noexcept;
    error_code to_value(value_io* dst) const;
    error_code from_value(const value_io* src);
    void destroy() noexcept;

    // C#-style variance conversions through the type-erased handle.
    template <typename T>
    T* as() noexcept;
    template <typename T>
    result<T*> cast() noexcept;
};
```

| Member | Semantics |
|---|---|
| `from<T>(handle)` | Wrap a typed `object_ref<T>` handle. |
| `type()` | The wrapped `type_id` (static type of the stored handle). |
| `type_of()` | The wrapped type descriptor. |
| `valid()` / `operator bool` | `obj_ != nullptr && op_ != nullptr`. |
| `is<T>()` | Exact runtime type equality: true when the wrapped object's type is `T`. Use `is_derived_from<T>` (reflect.hpp) for the "is-a" relationship. |
| `data()` | The wrapped object pointer (type-erased), valid while the handle is valid. |
| `get(name, out)` / `set(name, v)` | Field get/set by name through the operation table; `no_such_field` when the handle is invalid. |
| `to_value(dst)` / `from_value(src)` | `value_io` serialization bridge. |
| `destroy()` | Explicit destroy for manual (allocator) instances; RAII handles are released by the wrapped `object_ref`. After `destroy()` the handle is empty. |
| `as<T>()` | Variance conversion through the type-erased handle. Full precision: the runtime dynamic type (when the stored type is polymorphic) resolves to its reflected descriptor, and the base chain's offset-correct adjusters compute the target subobject. When the dynamic descriptor cannot compose the target, the walk falls back to (a) the runtime dynamic type's exact address when the target IS that type, then (b) the static chain, which reaches the stored static type's bases. Failures return `nullptr`. |
| `cast<T>()` | `as<T>()` wrapped in a `result<T*>`: `no_such_field` for an invalid/empty handle, `kind_mismatch` when `as` fails. |

Conversion behavior detail of `as<T>()`:

```cpp
// 1. !valid() or a null wrapped pointer → nullptr
// 2. target = rtti::type_of<T>() (rtti:: qualification required: the class
//    member type_of() would otherwise shadow the free function)
// 3. If the stored type is polymorphic and its dynamic type is more derived
//    than the static type:
//      a. dyn_ptr = most_derived (dynamic_cast<void*> for polymorphic T)
//      b. dd = detail::find_desc(*dt); if found,
//         result = detail::adjust_to(dyn_ptr, dd, target, 0)
//      c. if result == nullptr and target is the dynamic type exactly,
//         result = dyn_ptr
// 4. Fallback: result = detail::adjust_to(static_ptr, type_of_, target, 0)
//    (compose the static chain, reaching the stored static type's bases)
// 5. return static_cast<T*>(result)
```

The internal helpers `detail::same_type_id`, `detail::derived_from_desc` (depth-guarded walk of the runtime base chain, max depth 64), `detail::adjust_to` (compose offset-correct adjusters from `from` toward `target`, depth-guarded) and `detail::scalar_convertible` (runtime scalar-convertibility judge used by upstream converters) support these conversions.

> APIs in this chapter are extracted from `reflection.hpp:2719-2980` (helpers at `reflection.hpp:2982-3075`).

---

## 17. Method reflection

`reflect_method<T>("Name", args...)` dispatches a **registered static** method (no instance); `reflect_method(obj, "Name", args...)` dispatches a member method through its instance (a `T` object, an `object_ref<T>`, or an `instance` handle). Methods are registered one per macro invocation (`RTTI_DEFINE_METHOD` / `RTTI_DEFINE_STATIC_METHOD`): each links a `method_entry` into the per-type table read by `methods<T>()` / `method_count<T>()`. Parameters travel through the `ctor_arg` family (scalar `field_value` or `value_io` node, positional binding); results come back as a scalar `field_value` or, for composite types, through a caller-supplied `value_io` node.

Error codes: unregistered → `no_such_method`; argument count/type mismatch → `kind_mismatch`; missing instance → `no_such_field`. Never throws, never crashes.

### 17.1 `method_info`

```cpp
// One reflected method of a type (as returned by methods<T>()).
struct method_info {
    const char* name;
    bool is_static;
    const char* signature;
};
```

### 17.2 `reflect_method` overloads

Static-method call:

```cpp
template <typename T>
inline result<field_value> reflect_method(const char* name, const ctor_arg* args,
                                          std::size_t count,
                                          value_io* node = nullptr) noexcept;

template <typename T>
inline result<field_value> reflect_method(const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept;
```

Member-method call through an object reference (`T& obj`), an `object_ref<T>&`, or a type-erased `instance&`:

```cpp
template <typename T>
inline result<field_value> reflect_method(T& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept;

template <typename T>
inline result<field_value> reflect_method(T& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept;

template <typename T>
inline result<field_value> reflect_method(object_ref<T>& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept;

template <typename T>
inline result<field_value> reflect_method(object_ref<T>& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept;

inline result<field_value> reflect_method(instance& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept;

inline result<field_value> reflect_method(instance& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept;
```

| Overload family | Failure conditions |
|---|---|
| Static (`reflect_method<T>`) | `name == nullptr`, or `count > 0 && args == nullptr` → `kind_mismatch`; method not found → `no_such_method`; a member method called without an instance → `no_such_field`; call errors are passed through. |
| `T& obj` | Same as static, but invoking the member path; the instance pointer is always present. |
| `object_ref<T>& obj` | Invalid handle → `no_such_field`; then delegates to the `T&` overload. |
| `instance& obj` | Invalid handle → `no_such_field`; `name == nullptr` or bad `args` → `kind_mismatch`; method not found in the global table → `no_such_method`. |

Results: scalar results (`bool`, enum, integral, floating, `string_view`, `const char*`) land in `result.value` (a `field_value`); composite results (container, reflected struct, `std::string`, …) require `node` and are written through `rtti::to_value`. A scalar with neither sink returns `kind_mismatch`.

### 17.3 `method_count<T>()` / `methods<T>()`

```cpp
template <typename T>
inline std::size_t method_count() noexcept;

template <typename T>
inline const method_info* methods() noexcept;
```

`method_count<T>()` is the number of registered methods; `methods<T>()` returns the reflected table (`nullptr` when empty), built once on first use.

The internal bookkeeping lives in `namespace rtti::detail`:

- `method_entry` — `{name, is_static, signature, call, owner, next}` linked list node.
- `method_head<T>()` / `method_link<T>` — per-type list head (function-local static) and link.
- `method_global_head()` / `method_global_link` — program-wide list for type-erased `instance` lookups (owner typeid matches).
- `member_traits<R (C::*)(A...)>` (+ `const`, `noexcept`, `const noexcept` variants) — return/owner/params decomposition.
- `static_traits<R (*)(A...)>` (+ `noexcept`) — return/params decomposition for static methods.
- `decay_tuple<Tuple>` — reference-stripped parameter tuple (decayed so a reference to a temporary is never created; `const&`/reference parameters materialize by value before the call).
- `method_fill<I, Tuple, N>` — converts the `I`-th runtime argument into the `I`-th parameter (positional); sequential recursion to avoid MSVC mid-expression pack failure.
- `scalar_field(v)` / `result_assign(out, node, value)` — deliver the method result.
- `is_void_ret<Ret>()` — branch-selection helper avoiding an MSVC C4127 warning.
- `member_apply` / `member_invoke<Owner, M>` — unpack the parameter tuple and call the member function; type-erased entry point (const / noexcept included).
- `static_apply` / `static_invoke<F>` — same for static methods.
- `find_method<T>(name)` / `find_method_global(owner, name)` — locate a registered method.
- `method_table<T>()` — reflect the per-type method list into a stable vector, built once on first use.

Example:

```cpp
struct Calc {
    static int add(int a, int b) { return a + b; }
    int mul(int a, int b) { return a * b; }
};
RTTI_DEFINE_STATIC_METHOD(Calc, add)   // namespace scope
RTTI_DEFINE_METHOD(Calc, mul)

rtti::ctor_arg args[] = {
    rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(3))),
    rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(4))),
};
auto r = rtti::reflect_method<Calc>("add", args, 2);
assert(r.ok() && r.value.as_integer() == 7);

Calc c;
auto r2 = rtti::reflect_method(c, "mul", args, 2);
assert(r2.ok() && r2.value.as_integer() == 12);
```

> APIs in this chapter are extracted from `reflection.hpp:3077-3517`.

---

## 18. Macro infrastructure

Bounded argument counting + flat iteration. Iteration is pre-expanded and non-recursive; each `RTTI_APPLY_N` expands `M` once per argument. The ceiling (`64`) exists only to keep macros finite; the field-count cap is not a design limit for reflected types.

### 18.1 Basic helpers

```cpp
#define RTTI_CAT_IMPL(a, b) a##b
#define RTTI_CAT(a, b) RTTI_CAT_IMPL(a, b)

#define RTTI_EXPAND(...) __VA_ARGS__
```

`RTTI_CAT` is the indirect (two-level) token-paste; `RTTI_EXPAND` forces argument expansion.

### 18.2 Empty-argument detection

```cpp
#define RTTI_IBP_SPLIT(i, ...) RTTI_IBP_SPLIT_I(i, __VA_ARGS__)
#define RTTI_IBP_SPLIT_I(i, ...) RTTI_IBP_PRIMITIVE_CAT(RTTI_IBP_SPLIT_, i)(__VA_ARGS__)
#define RTTI_IBP_SPLIT_0(a, ...) a
#define RTTI_IBP_SPLIT_1(a, ...) __VA_ARGS__
#define RTTI_IBP_PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__
#define RTTI_IBP_CAT(a, ...) RTTI_IBP_PRIMITIVE_CAT(a, __VA_ARGS__)
#define RTTI_IBP_IS_VARIADIC_C(...) 1
#define RTTI_IBP_IS_VARIADIC_R_1 1,
#define RTTI_IBP_IS_VARIADIC_R_RTTI_IBP_IS_VARIADIC_C 0,
#define RTTI_IS_BEGIN_PARENS(...)                                              \
    RTTI_IBP_SPLIT(0, RTTI_IBP_CAT(RTTI_IBP_IS_VARIADIC_R_,                   \
                                   RTTI_IBP_IS_VARIADIC_C __VA_ARGS__))

#define RTTI_CAT2_II(res) res
#define RTTI_CAT2_I(a, b) RTTI_CAT2_II(a ## b)
#define RTTI_CAT2(a, b) RTTI_CAT2_I(a, b)

#define RTTI_IS_EMPTY_APPLY_1 1
#define RTTI_IS_EMPTY_APPLY_0 0

// 1 when the variadic list is empty, 0 otherwise.
#define RTTI_IS_EMPTY(...)                                                      \
    RTTI_CAT2(RTTI_IS_EMPTY_APPLY_, RTTI_IS_BEGIN_PARENS(__VA_ARGS__ ()))
```

`RTTI_IS_BEGIN_PARENS(...)` detects whether the variadic list is empty (Boost PP `is_begin_parens` idiom; requires the conformant preprocessor `/Zc:preprocessor`, which the reflect core always compiles under). `RTTI_IS_EMPTY(...)` is 1 when the list is empty, 0 otherwise.

### 18.3 Argument counting

```cpp
// The real definitions enumerate all 64 counting slots (_1 .. _64) and the
// full 64..0 default-count tail; abbreviated here for readability.
#define RTTI_NARG_I(_1, _2, _3, ..., _63, _64, N, ...) \
    N
#define RTTI_NARG(...) \
    RTTI_NARG_I(__VA_ARGS__, 64, 63, ..., 1, 0)
```

Counts arguments 0..64. The single-level form (`RTTI_NARG` → `RTTI_NARG_I`, no intermediate variadic macro) is required for MSVC's conformant preprocessor. Note: **empty lists currently yield 1**; the empty `RTTI_DEFINE_TYPE` case (zero fields) is handled separately via `RTTI_IS_EMPTY`. More than 64 arguments is a hard compile error: the 65th token is taken as the count, so the per-count expansion macro (`RTTI_APPLY_f65`, `RTTI_MEMBER_TYPES_f65`, ...) is undefined (C2061/C3861-style diagnostics).

### 18.4 Flat iteration: `RTTI_APPLY_0` .. `RTTI_APPLY_64`, `RTTI_APPLY`, `RTTI_APPLY_OPT`

```cpp
#define RTTI_APPLY_0(M, T)
#define RTTI_APPLY_1(M, T, a1) M(T, a1)
#define RTTI_APPLY_2(M, T, a1, a2) M(T, a1) M(T, a2)
// ... up to RTTI_APPLY_64(M, T, a1, ..., a64)

#define RTTI_APPLY(M, T, ...) \
    RTTI_EXPAND(RTTI_CAT(RTTI_APPLY_, RTTI_NARG(__VA_ARGS__))(M, T, __VA_ARGS__))

#define RTTI_APPLY_OPT_SEL_1(M, T, ...)
#define RTTI_APPLY_OPT_SEL_0(M, T, ...) RTTI_APPLY(M, T, __VA_ARGS__)
#define RTTI_APPLY_OPT(M, T, ...) \
    RTTI_CAT2(RTTI_APPLY_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(M, T, __VA_ARGS__)
```

`RTTI_APPLY(M, T, a...)` expands `M(T, a1) M(T, a2) ...` for each argument (pre-expanded, non-recursive). `RTTI_APPLY_OPT` expands to nothing when the list is empty.

> APIs in this chapter are extracted from `reflection.hpp:1165-1266`.

---

## 19. Field-registration helper macros

### 19.1 `RTTI_FIELD_DECL`, `RTTI_FIELD_REF`, `RTTI_FIELD_TYPE_DESC`

```cpp
#define RTTI_FIELD_DECL(T, f)                                                 \
    static inline const ::rtti::typed_field_access<T, decltype(T::f)> rtti_acc_##f{ \
        #f, &T::f, &typeid(decltype(T::f))};

#define RTTI_FIELD_REF(T, f) &rtti_acc_##f,

#define RTTI_FIELD_TYPE_DESC(T, f) { #f, &::rtti::type_of<decltype(T::f)> },
```

- `RTTI_FIELD_DECL` declares one static accessor (`typed_field_access<T, decltype(T::f)>`) per field.
- `RTTI_FIELD_REF` contributes its address to the `array` table.
- `RTTI_FIELD_TYPE_DESC` contributes a `{name, resolver}` entry to the `desc`
  table. The non-intrusive registration macros substitute a
  member-access-resolving variant (`RTTI_FIELD_ACCESS_VALUE`, see §19.4) so the
  `desc` rows carry `member_access` (public_/protected_/private_, §5.6). (Intrusive variants use `RTTI_FIELD_ACCESS_INTRUSIVE` → `{name, resolver, private_}`; see §20.4.)

### 19.2 `RTTI_MEMBER_TYPES_*` / `RTTI_MEMBER_TYPES_OPT`

Per-count comma-joining macros producing the `decltype(&T::aN)` member-pointer-type list (no trailing comma; MSVC rejects trailing commas in template-argument lists). The full set covers counts 0..64:

```cpp
#define RTTI_MEMBER_TYPES_0(T)
#define RTTI_MEMBER_TYPES_1(T, a1) decltype(&T::a1)
#define RTTI_MEMBER_TYPES_2(T, a1, a2) decltype(&T::a1), decltype(&T::a2)
// ... up to RTTI_MEMBER_TYPES_64(T, a1, ..., a64)

#define RTTI_MEMBER_TYPES_OPT_SEL_1(T, ...)
#define RTTI_MEMBER_TYPES_OPT_SEL_0(T, ...) \
    RTTI_CAT(RTTI_MEMBER_TYPES_, RTTI_NARG(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_MEMBER_TYPES_OPT(T, ...) \
    RTTI_CAT2(RTTI_MEMBER_TYPES_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(T, __VA_ARGS__)
```

### 19.3 `RTTI_MEMBER_VALUES_*` / `RTTI_MEMBER_VALUES_OPT`

The same pattern producing the member-pointer value list (`&T::a1, &T::a2, ...`):

```cpp
#define RTTI_MEMBER_VALUES_0(T)
#define RTTI_MEMBER_VALUES_1(T, a1) &T::a1
#define RTTI_MEMBER_VALUES_2(T, a1, a2) &T::a1, &T::a2
// ... up to RTTI_MEMBER_VALUES_64(T, a1, ..., a64)

#define RTTI_MEMBER_VALUES_OPT_SEL_1(T, ...)
#define RTTI_MEMBER_VALUES_OPT_SEL_0(T, ...) \
    RTTI_CAT(RTTI_MEMBER_VALUES_, RTTI_NARG(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_MEMBER_VALUES_OPT(T, ...) \
    RTTI_CAT2(RTTI_MEMBER_VALUES_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(T, __VA_ARGS__)
```

These emit the `members` tuple of a type's OWN fields (spec 19), which the derived-merge machinery converts to a derived type to build offset-correct base accessors.

### 19.4 `RTTI_FRIEND_FIELDS(T)` and the member-access probes

```cpp
// Grants the non-intrusive field-table (fields_of<T>) friend access to T, so
// `&T::f` may be taken for private/protected fields. Place INSIDE the class
// body; pairs with a namespace-scope RTTI_DEFINE_TYPE(T, ...).
#define RTTI_FRIEND_FIELDS(T) friend struct ::rtti::fields_of<T>;
```

Non-intrusive registration emits, at namespace scope, a per-type
`rtti::detail::rtti_access_holder<T>` whose per-field probe pair
(`rtti_access_ext_<f>` / `rtti_access_der_<f>`, reflection.hpp:3673-3686)
checks, via SFINAE on `decltype(&U::f)`, whether the field is accessible from
outside (`ext`, → `public_`) or from a derived context (`der`, → `protected_`),
or neither (→ `private_`). The holder is **not** a member of `fields_of` and is
**not** a friend of `T`, so a `RTTI_FRIEND_FIELDS` grant used to form `&T::f`
never leaks into the probe results.

Access decisions:

| Combination | `member_access` |
| --- | --- |
| `ext` true | `public_` |
| `ext` false, `der` true | `protected_` |
| both false | `private_` |

Intrusive registration (`RTTI_DEFINE_TYPE_INTRUSIVE`) forms `&T::f` inside the
class body and needs no friend, but cannot run a namespace-scope probe from
inside `T`; every intrusive field therefore reports `private_` — the
conservative documented boundary (matching `field_type_desc::access`'s
default).

> APIs in this chapter are extracted from `reflection.hpp:3520-3630`.

---

## 20. Type registration macros (`RTTI_DEFINE_TYPE*`)

### 20.1 `RTTI_DEFINE_TYPE(T, ...)`

Non-intrusive registration at namespace scope (public members). The specialization is wrapped in an `rtti` namespace block so MSVC accepts explicit-specialization-while-inside-another-namespace.

```cpp
#define RTTI_DEFINE_TYPE(T, ...)                                              \
    namespace rtti {                                                          \
    template <>                                                               \
    struct fields_of<T> {                                                     \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        using members_type = std::tuple<                                      \
            RTTI_MEMBER_TYPES_OPT(T, __VA_ARGS__)>;                           \
        static inline const members_type members =                            \
            members_type{ RTTI_MEMBER_VALUES_OPT(T, __VA_ARGS__) };           \
        RTTI_APPLY_OPT(RTTI_FIELD_DECL, T, __VA_ARGS__)                       \
        static inline const field_access_base* const array[                    \
            (count > 0 ? count : 1)] = {                                      \
            RTTI_APPLY_OPT(RTTI_FIELD_REF, T, __VA_ARGS__)                    \
        };                                                                     \
        static inline const field_type_desc desc[(count > 0 ? count : 1)] = { \
            RTTI_APPLY_OPT(RTTI_FIELD_TYPE_DESC, T, __VA_ARGS__)              \
        };                                                                     \
    };                                                                          \
    }
```

**Parameters**: `T` — the reflected type; `...` — the public member names, in declaration order (up to 64; an empty list is legal and yields `count == 0`).

**Effect**: specializes `rtti::fields_of<T>` with `count`, `members_type`, `members`, the `array` of accessors, and the `desc` table. After use, `is_reflected<T>`, `field_count<T>`, `fields<T>`, `for_each_field`, `type_of<T>()` etc. all work.

**Private / protected fields**: `...` may name private / protected members when
the class grants `RTTI_FRIEND_FIELDS(T)` (see §19.4) inside its body. The macro
additionally emits, at namespace scope, a `detail::rtti_access_holder<T>`
probe pair per field so each `field_type_desc` row's `access` member is
resolved exactly (`member_access::public_` / `protected_` / `private_`).

Example:

```cpp
struct Point { int x; int y; };
RTTI_DEFINE_TYPE(Point, x, y)     // namespace scope
assert(rtti::field_count<Point>() == 2);
```

### 20.2 `RTTI_DEFINE_TYPE_DERIVED(T, Base, ...)`

Non-intrusive registration of a derived type (spec 19). `Base`'s reflected fields merge into `T`'s field table, base-first, through offset-correct D-anchored accessors (virtual/non-virtual inheritance both work), and `Base`'s `type_id` is recorded in `T`'s runtime base chain.

```cpp
#define RTTI_DEFINE_TYPE_DERIVED(T, Base, ...)                                \
    namespace rtti {                                                          \
    template <>                                                               \
    struct base_types<T> {                                                    \
        using list = std::tuple<Base>;                                        \
    };                                                                         \
    }                                                                          \
    namespace rtti {                                                          \
    template <>                                                               \
    struct fields_of<T> {                                                     \
        static constexpr std::size_t base_count =                             \
            ::rtti::detail::base_total<::rtti::detail::base_list_t<T>>();     \
        static constexpr std::size_t own_count =                              \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));        \
        static constexpr std::size_t count = base_count + own_count;          \
        using members_type = std::tuple<                                      \
            RTTI_MEMBER_TYPES_OPT(T, __VA_ARGS__)>;                           \
        static inline const members_type members =                            \
            members_type{ RTTI_MEMBER_VALUES_OPT(T, __VA_ARGS__) };           \
        RTTI_APPLY_OPT(RTTI_FIELD_DECL, T, __VA_ARGS__)                       \
        static inline const field_access_base* const own_table[              \
            (own_count > 0 ? own_count : 1)] = {                              \
            RTTI_APPLY_OPT(RTTI_FIELD_REF, T, __VA_ARGS__)                    \
        };                                                                    \
        static inline const field_type_desc own_desc[                        \
            (own_count > 0 ? own_count : 1)] = {                              \
            RTTI_APPLY_OPT(RTTI_FIELD_TYPE_DESC, T, __VA_ARGS__)              \
        };                                                                    \
        static inline const field_access_base* const* const array =           \
            ::rtti::detail::derived_pool<T>::accessors();                     \
        static inline const field_type_desc* const desc =                     \
            ::rtti::detail::derived_pool<T>::descriptors();                   \
    };                                                                          \
    }
```

**Parameters**: `T` — the derived type; `Base` — the single direct registered base; `...` — `T`'s own public members (may be empty).

**Effect**: specializes `base_types<T>` (single base) and `fields_of<T>` with the merged table. `array`/`desc` point into the once-built `detail::derived_pool<T>`.

### 20.3 `RTTI_DEFINE_TYPE_WITH_DEFAULT(T, ...)`

```cpp
#define RTTI_DEFINE_TYPE_WITH_DEFAULT(T, ...)                                 \
    RTTI_DEFINE_TYPE(T, __VA_ARGS__)                                          \
    namespace rtti {                                                          \
    template <>                                                               \
    struct has_default<T> : std::true_type {};                                \
    }
```

`RTTI_DEFINE_TYPE` plus a `has_default<T>` specialization, so format fillers keep the member's existing value on missing keys.

### 20.4 `RTTI_DEFINE_TYPE_INTRUSIVE(T, ...)`

Intrusive registration placed **inside** the class body. The macro defines a nested `rtti_registration` struct (no template specialization is legal at class scope); `fields_of` inherits it. The friend declaration is not needed for the member-pointer accessors (access is checked at `&T::f`, which is inside the class).

```cpp
#define RTTI_DEFINE_TYPE_INTRUSIVE(T, ...)                                    \
    struct rtti_registration {                                                \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        using members_type = std::tuple<                                      \
            RTTI_MEMBER_TYPES_OPT(T, __VA_ARGS__)>;                           \
        static inline const members_type members =                            \
            members_type{ RTTI_MEMBER_VALUES_OPT(T, __VA_ARGS__) };           \
        RTTI_APPLY_OPT(RTTI_FIELD_DECL, T, __VA_ARGS__)                       \
        static inline const ::rtti::field_access_base* const array[            \
            (count > 0 ? count : 1)] = {                                      \
            RTTI_APPLY_OPT(RTTI_FIELD_REF, T, __VA_ARGS__)                    \
        };                                                                     \
        static inline const ::rtti::field_type_desc desc[                      \
            (count > 0 ? count : 1)] = {                                      \
            RTTI_APPLY_OPT(RTTI_FIELD_TYPE_DESC, T, __VA_ARGS__)              \
        };                                                                     \
    };
```

Used inside a class body; the `rtti_registration` nested struct is picked up by `fields_of<T>` via inheritance (see §2.2). Private fields are supported — the member-pointer accessors are formed inside the class, so no friend declaration is needed. Member-access classification, however, is not possible from inside the class body (any in-class probe is a member of `T` and sees every access level alike), so every intrusive field conservatively reports `private_` via `RTTI_FIELD_ACCESS_INTRUSIVE` — the documented boundary (`unknown == private_`, matching `field_type_desc::access`'s default). See §19.4.

### 20.5 `RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(T, ...)`

```cpp
#define RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(T, ...)                       \
    using rtti_has_default = void;                                            \
    RTTI_DEFINE_TYPE_INTRUSIVE(T, __VA_ARGS__)
```

The intrusive variant plus `rtti_has_default` (enables `has_default<T>`).

> APIs in this chapter are extracted from `reflection.hpp:3632-3729`.

---

## 21. Enum registration macros

### 21.1 `RTTI_ENUM_ENTRY(T, e)`

```cpp
#define RTTI_ENUM_ENTRY(T, e) ::rtti::enum_label<T>{#e, T::e},
```

One table entry: `{ "e", T::e }`. Used internally by `RTTI_DEFINE_ENUM`.

### 21.2 `RTTI_DEFINE_ENUM(T, ...)`

```cpp
// Empty-argument guard: `enum E {}` (zero enumerator arguments) registers an
// empty table (count 0, empty array). RTTI_APPLY_OPT expands the enumerator
// list to nothing when it is empty, so no malformed `RTTI_ENUM_ENTRY(T,)`
// token is ever formed.
#define RTTI_DEFINE_ENUM(T, ...)                                              \
    namespace rtti {                                                          \
    template <>                                                               \
    struct enum_table<T> {                                                    \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        static inline const enum_label<T> array[(count > 0 ? count : 1)] =    \
            { RTTI_APPLY_OPT(RTTI_ENUM_ENTRY, T, __VA_ARGS__) };              \
    };                                                                          \
    }
```

**Parameters**: `T` — the enum type; `...` — the enumerator names, in registration order.

**Effect**: specializes `rtti::enum_table<T>` with `count` and `array`. After use, `enum_count<T>()`, `enum_to_string`, `enum_from_string`, `enum_name_at`, `enum_value_at`, `for_each_enum` all work.

Example:

```cpp
enum class Color { Red, Green, Blue };
RTTI_DEFINE_ENUM(Color, Red, Green, Blue)   // namespace scope
```

> APIs in this chapter are extracted from `reflection.hpp:3731-3746`.

---

## 22. Template-info registration macros

### 22.1 Argument-list helpers `RTTI_TEMPLATE_ARGS_1` .. `RTTI_TEMPLATE_ARGS_64`

Per-count comma-joining macros expanding the template argument list without a trailing comma (MSVC rejects trailing commas in template-argument lists):

```cpp
#define RTTI_TEMPLATE_ARGS_1(a1) a1
#define RTTI_TEMPLATE_ARGS_2(a1, a2) a1, a2
// ... up to RTTI_TEMPLATE_ARGS_64(a1, ..., a64)
```

### 22.2 Empty-aware selector and paren-wrapped arguments

```cpp
#define RTTI_TEMPLATE_ARGS_OPT_SEL_1(T, ...)
#define RTTI_TEMPLATE_ARGS_OPT_SEL_0(T, ...) RTTI_TAO_STRIP(__VA_ARGS__)
```

A type-id that itself contains commas (e.g. `std::pair<int,int>`) is passed as a single argument wrapped in an extra pair of parentheses: `RTTI_TEMPLATE_INFO(FnPair, (std::pair<int,int>))`. `RTTI_IS_EMPTY` cannot tell `(...)` from `()`, so the selectors check begin-with-parenthesis **first**; every element is pushed through a conditional one-level parenthesis stripper (`RTTI_TAO_UNPAREN`, `RTTI_TAO_ELEM`, per-count `RTTI_TAO_STRIP_1`..`RTTI_TAO_STRIP_64`, `RTTI_TAO_STRIP`). Then:

```cpp
#define RTTI_TAO_OPT_SEL_0(T, ...) \
    RTTI_CAT2(RTTI_TEMPLATE_ARGS_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_TAO_OPT_SEL_1(T, ...) RTTI_TAO_STRIP(__VA_ARGS__)
#define RTTI_TEMPLATE_ARGS_OPT(T, ...) \
    RTTI_CAT(RTTI_TAO_OPT_SEL_, RTTI_IS_BEGIN_PARENS(__VA_ARGS__))(T, __VA_ARGS__)

#define RTTI_TAO_COUNT_SEL_0(...) \
    (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__))
#define RTTI_TAO_COUNT_SEL_1(...) RTTI_NARG(__VA_ARGS__)
#define RTTI_TEMPLATE_ARGS_COUNT(...) \
    RTTI_CAT(RTTI_TAO_COUNT_SEL_, RTTI_IS_BEGIN_PARENS(__VA_ARGS__))(__VA_ARGS__)
```

So `RTTI_TEMPLATE_INFO(Fn, int, (std::pair<int,int>))` yields `Fn<int, std::pair<int,int>>`.

### 22.3 `RTTI_TEMPLATE_INFO(T, ...)`

Non-intrusive registration of a template instance's argument list at namespace scope. `T` names the class template; the remaining arguments are its type arguments.

```cpp
// Example:
//   RTTI_TEMPLATE_INFO(MyBox, int, double, std::string)
// specializes template_info<MyBox<int, double, std::string>> with an std::tuple
// of the argument types and their count.
#define RTTI_TEMPLATE_INFO(T, ...)                                            \
    namespace rtti {                                                          \
    template <>                                                               \
    struct template_info<T<RTTI_TEMPLATE_ARGS_OPT(T, __VA_ARGS__)>> {         \
        static constexpr std::size_t count =                                  \
            RTTI_TEMPLATE_ARGS_COUNT(__VA_ARGS__);                            \
        using args = std::tuple<RTTI_TEMPLATE_ARGS_OPT(T, __VA_ARGS__)>;      \
        using registered = void;                                              \
    };                                                                         \
    }
```

**Parameters**: `T` — the class template; `...` — its type arguments (up to 64; an empty list is allowed for a template with all-defaulted parameters, registering as `T<>` and yielding count 0 with an empty tuple). Arguments containing commas must be wrapped in extra parentheses.

**Effect**: specializes `template_info<Inst>` with `count`, `args` (a `std::tuple`), and `registered`. After use, `is_template_instance`, `template_arg_count`, `template_args`, `template_arg_at`, `template_args_of`, `template_arg_desc_of`, `for_each_template_arg` all work. An unregistered template instance (or non-template type) reports a safe empty table.

> APIs in this chapter are extracted from `reflection.hpp:3748-3901`.

---

## 23. Attribute registration macros

Attribute tables are explicit specializations of `rtti::attr_table<T>` / `rtti::field_attr_table<T>` at namespace scope, so they compose with both the intrusive and non-intrusive field-registration macro families without touching them. Attributes are optional: a type with no registration simply reports count 0 / null pointers.

Each `RTTI_ATTR(...)` expands to a factory call (`make_attr`) so every comma nests inside parentheses; the preprocessor then never mis-counts the trailing commas of braced initializer lists.

### 23.1 Argument helpers

```cpp
#define RTTI_ATTR_ARG_MAKE(_, a) ::rtti::detail::make_attr_arg(a),
#define RTTI_ATTR_ARGS(...) RTTI_APPLY_OPT(RTTI_ATTR_ARG_MAKE, _, __VA_ARGS__)

// One attribute value: { name, { args... } }.
#define RTTI_ATTR(name, ...) ::rtti::make_attr(name, {RTTI_ATTR_ARGS(__VA_ARGS__)})

#define RTTI_ATTR_REF(_, a) a,
#define RTTI_ATTR_LIST(...) RTTI_APPLY_OPT(RTTI_ATTR_REF, _, __VA_ARGS__)

// One field's attribute list: { "field", { attrs... } }.
#define RTTI_FIELD_ATTR_ENTRY(f, ...) \
    ::rtti::make_field_attr(#f, {RTTI_ATTR_LIST(__VA_ARGS__)})
```

- `RTTI_ATTR(name, args...)` — one attribute expansion: `make_attr(name, {args...})`.
- `RTTI_FIELD_ATTR_ENTRY(f, attrs...)` — one field attribute-list entry: `make_field_attr("f", {attrs...})`.

### 23.2 `RTTI_DEFINE_TYPE_ATTRS(T, ...)`

```cpp
// Type-level attribute table specialization at namespace scope. An empty
// declaration (no attributes) is legal and yields count 0.
#define RTTI_DEFINE_TYPE_ATTRS(T, ...)                                        \
    namespace rtti {                                                          \
    template <>                                                               \
    struct attr_table<T> {                                                    \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        static inline const attr array[(count > 0 ? count : 1)] = {            \
            RTTI_EXPAND(__VA_ARGS__)                                          \
        };                                                                     \
        using registered = void;                                              \
    };                                                                          \
    }
```

**Parameters**: `T` — the reflected type; `...` — `RTTI_ATTR(...)` expansions (may be empty). **Effect**: specializes `attr_table<T>`; enables `attrs_defined<T>()`, `attr_count<T>()`, `attr_at`, `find_attr`, `has_attr`.

### 23.3 `RTTI_DEFINE_FIELD_ATTRS(T, ...)`

```cpp
// Per-field attribute table specialization at namespace scope.
#define RTTI_DEFINE_FIELD_ATTRS(T, ...)                                       \
    namespace rtti {                                                          \
    template <>                                                               \
    struct field_attr_table<T> {                                              \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
        static inline const field_attr_entry array[                           \
            (count > 0 ? count : 1)] = { RTTI_EXPAND(__VA_ARGS__) };           \
        using registered = void;                                              \
    };                                                                          \
    }
```

**Parameters**: `T` — the reflected type; `...` — `RTTI_FIELD_ATTR_ENTRY(f, attrs...)` expansions (may be empty). **Effect**: specializes `field_attr_table<T>`; enables the field-level attribute queries (`field_attr`, `field_attr_count`, `field_attr_at`, `field_find_attr`, `field_has_attr`).

Example:

```cpp
struct Point { int x; int y; };
RTTI_DEFINE_TYPE(Point, x, y)                       // namespace scope
RTTI_DEFINE_TYPE_ATTRS(Point, RTTI_ATTR("color", "red"))
RTTI_DEFINE_FIELD_ATTRS(Point,
    RTTI_FIELD_ATTR_ENTRY(x, RTTI_ATTR("unit", "mm")))
assert(rtti::attrs_defined<Point>());
assert(rtti::field_has_attr<Point>("x", "unit"));
```

> APIs in this chapter are extracted from `reflection.hpp:3903-3957`.

---

## 24. Constructor registration macros

One parenthesized `(name, type, optional)` triple per constructor parameter. The macro counts the parameters, builds the descriptor array, the parameter type list (`std::tuple`) and wires the generic runtime factory, mirroring the explicit proto schema specialization style. Template-type parameters such as `std::map<std::string, int>` are fine because the comma lives inside the parentheses.

### 24.1 Triple accessors

```cpp
#define RTTI_CTOR_GET_1(a, b, c) a
#define RTTI_CTOR_GET_2(a, b, c) b
#define RTTI_CTOR_GET_3(a, b, c) c
#define RTTI_CTOR_NAME(triple) RTTI_CTOR_GET_1 triple
#define RTTI_CTOR_TYPE(triple) RTTI_CTOR_GET_2 triple
#define RTTI_CTOR_OPTIONAL(triple) RTTI_CTOR_GET_3 triple

#define RTTI_CTOR_PARAM_DECL(T, triple)                                       \
    {RTTI_CTOR_NAME(triple), &typeid(RTTI_CTOR_TYPE(triple)),                 \
     RTTI_CTOR_OPTIONAL(triple), {}},
```

`RTTI_CTOR_NAME/TYPE/OPTIONAL` unpack a `(name, type, optional)` triple; `RTTI_CTOR_PARAM_DECL` emits one `rtti::ctor_param` initializer.

### 24.2 Parameter-type comma joiners

```cpp
#define RTTI_CTOR_TYPES_0(T)
#define RTTI_CTOR_TYPES_1(T, a1) RTTI_CTOR_TYPE(a1)
#define RTTI_CTOR_TYPES_2(T, a1, a2) RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2)
// ... up to RTTI_CTOR_TYPES_64(T, a1, ..., a64)
```

Comma-joined parameter type lists (no trailing comma; MSVC rejects a trailing comma inside a template-argument-list).

### 24.3 `RTTI_DEFINE_CTOR(T, ...)`

```cpp
// Register a reflected constructor for T at namespace scope. At least one
// (name, type, optional) parameter is required. Example:
//   RTTI_DEFINE_CTOR(MyType, ("x", int, false), ("y", std::string, true))
#define RTTI_DEFINE_CTOR(T, ...)                                              \
    namespace rtti {                                                          \
    template <>                                                               \
    struct ctor_registry<T> {                                                 \
        static constexpr std::size_t count = RTTI_NARG(__VA_ARGS__);           \
        static inline const ::rtti::ctor_param params[                        \
            (count > 0 ? count : 1)] = {                                      \
            RTTI_APPLY(RTTI_CTOR_PARAM_DECL, T, __VA_ARGS__)                  \
        };                                                                     \
        using param_types = std::tuple<RTTI_CAT(RTTI_CTOR_TYPES_,            \
            RTTI_NARG(__VA_ARGS__))(T, __VA_ARGS__)>;                        \
        using factory_fn = T* (*)(const ::rtti::ctor_arg*, std::size_t,       \
                                   ::rtti::error_code&);                       \
        static inline factory_fn factory =                                     \
            &::rtti::detail::ctor_factory<T, param_types>;                    \
    };                                                                          \
    }
```

**Parameters**: `T` — the reflected type; `...` — at least one `(name, type, optional)` triple per constructor parameter (`name` — a string literal; `type` — the C++ parameter type; `optional` — `bool`). **Effect**: specializes `ctor_registry<T>` with `count`, `params`, `param_types` and a wired `factory`; enables `has_ctor_registry<T>` and reflective construction via `make_object<T>` / `create_instance<T>` (reflect.hpp).

Example:

```cpp
struct MyType {
    MyType(int x, std::string y) : x_(x), y_(std::move(y)) {}
    int x_;
    std::string y_;
};
RTTI_DEFINE_TYPE(MyType, x_, y_)                       // namespace scope
RTTI_DEFINE_CTOR(MyType, ("x", int, false), ("y", std::string, true))
// rtti::make_object<MyType>(rtti::ctor_arg::scalar("x", field_value::of(int64_t(1))))
```

> APIs in this chapter are extracted from `reflection.hpp:3959-4056`.

---

## 25. Method registration macros

One macro invocation registers **one** method of type `T` at namespace scope:

- `RTTI_DEFINE_METHOD(T, MethodName)` — member method.
- `RTTI_DEFINE_STATIC_METHOD(T, MethodName)` — static method.

Each links a `method_entry` into `T`'s table (`method_link`) and into the program-wide table used by the type-erased `instance` path (`method_global_link`). The registration struct is unique per `(method, line)` so several methods of one type, and same-named methods of different types on different lines, coexist in one TU. The struct is defined inside an anonymous namespace, so the same macro in a header used by many TUs links one entry per TU.

```cpp
#define RTTI_DEFINE_METHOD(T, MethodName)                                     \
    namespace rtti { namespace detail { namespace {                           \
    struct RTTI_CAT(RTTI_CAT(rtti_method_, MethodName), RTTI_CAT(_, __LINE__)) { \
        static ::rtti::error_code call(void* self,                             \
                                       const ::rtti::ctor_arg* args,           \
                                       std::size_t count,                      \
                                       ::rtti::field_value* out,               \
                                       ::rtti::value_io* node) noexcept {      \
            return ::rtti::detail::member_invoke<T>(&T::MethodName, self,      \
                                                    args, count, out, node);  \
        }                                                                     \
        static const ::rtti::detail::method_entry* entry() noexcept {         \
            static ::rtti::detail::method_entry e = {                         \
                #MethodName, false,                                           \
                typeid(decltype(&T::MethodName)).name(),                       \
                &call, &typeid(T), nullptr};                                   \
            return &e;                                                         \
        }                                                                     \
        static void link() noexcept {                                         \
            ::rtti::detail::method_link<T>(entry());                          \
            ::rtti::detail::method_global_link(entry());                      \
        }                                                                     \
        RTTI_CAT(RTTI_CAT(rtti_method_, MethodName), RTTI_CAT(_, __LINE__))() noexcept { \
            link();                                                           \
        }                                                                     \
    };                                                                        \
    static RTTI_CAT(RTTI_CAT(rtti_method_, MethodName), RTTI_CAT(_, __LINE__)) \
        RTTI_CAT(RTTI_CAT(rtti_method_inst_, MethodName), RTTI_CAT(_, __LINE__)){}; \
    } } }

#define RTTI_DEFINE_STATIC_METHOD(T, MethodName)                              \
    namespace rtti { namespace detail { namespace {                           \
    struct RTTI_CAT(RTTI_CAT(rtti_smethod_, MethodName), RTTI_CAT(_, __LINE__)) { \
        static ::rtti::error_code call(void*,                                 \
                                       const ::rtti::ctor_arg* args,           \
                                       std::size_t count,                      \
                                       ::rtti::field_value* out,               \
                                       ::rtti::value_io* node) noexcept {      \
            return ::rtti::detail::static_invoke(&T::MethodName, args, count, \
                                                 out, node);                  \
        }                                                                     \
        static const ::rtti::detail::method_entry* entry() noexcept {         \
            static ::rtti::detail::method_entry e = {                         \
                #MethodName, true,                                            \
                typeid(decltype(&T::MethodName)).name(),                       \
                &call, &typeid(T), nullptr};                                   \
            return &e;                                                         \
        }                                                                     \
        static void link() noexcept {                                         \
            ::rtti::detail::method_link<T>(entry());                          \
            ::rtti::detail::method_global_link(entry());                      \
        }                                                                     \
        RTTI_CAT(RTTI_CAT(rtti_smethod_, MethodName), RTTI_CAT(_, __LINE__))() noexcept { \
            link();                                                           \
        }                                                                     \
    };                                                                        \
    static const RTTI_CAT(RTTI_CAT(rtti_smethod_, MethodName), RTTI_CAT(_, __LINE__)) \
        RTTI_CAT(RTTI_CAT(rtti_smethod_inst_, MethodName), RTTI_CAT(_, __LINE__)){}; \
    } } }
```

**Parameters**: `T` — the owner type; `MethodName` — the (static) member function name. The method signature is produced from `decltype(&T::MethodName)`: return type and parameter list are decomposed by `member_traits` / `static_traits`; parameters travel through `ctor_arg` (positional binding); results come back through the `field_value` / `value_io` sinks (see §17).

Example:

```cpp
struct Calc {
    static int add(int a, int b) { return a + b; }
    int mul(int a, int b) { return a * b; }
};
RTTI_DEFINE_STATIC_METHOD(Calc, add)   // namespace scope
RTTI_DEFINE_METHOD(Calc, mul)
assert(rtti::method_count<Calc>() == 2);
```

> APIs in this chapter are extracted from `reflection.hpp:4058-4133`.

---

## 26. Header structure and include-cycle note

The reflect core's inline accessors (`typed_field_access::get_value`/`get_value_named` and the container `for_each` paths) call the format-agnostic converters (`to_value` / `from_value` / `member_to_value` / `member_to_value_named`), whose inline definitions live in `convert.hpp`. A TU that includes only this header (or `reflect.hpp`) instantiates those templates without the definitions in sight, which leaves undefined references at link time (LNK2019). Pushing the include to the end of the file breaks the would-be cycle (`convert.hpp` includes `reflection.hpp` at its top, so the nested include is suppressed by `#pragma once`) while still making every reflect-core TU see the converter definitions:

```cpp
#include <rtti/convert.hpp>
```

Physical layout of the header (all line numbers refer to the current file):

| Lines | Section |
|---|---|
| 1-29 | Header comment, standard includes |
| 30-269 | `namespace rtti` open, converter forward declarations, `fields_of`, `base_types`, container-shape traits, `key_traits`, `type_id` |
| 266-805 | `type_id`, `field_value`, `field_value_to`, `value_io`, `field_access_base`, `typed_field_access` |
| 807-857 | Intrusive `fields_of` pickup, `field_count`, `fields`, `for_each_field`, `has_default` |
| 859-1163 | Attribute system (`attr_arg`, `attr`, tables, `make_attr`, queries) |
| 1225-1825 | Macro infrastructure (`RTTI_NARG`, `RTTI_APPLY`, `RTTI_IS_EMPTY`, …) |
| 1826-1890 | Enum reflection (`enum_label`, `enum_table`, enum queries) |
| 1891-1975 | Type-description system (`type_kind`, `type_desc` family) |
| 1976-2072 | Compile-time judgments, family discrimination, enum helpers |
| 2073-2279 | `type_of` and `detail::type_of_impl` |
| 2280-2379 | Template-argument reflection |
| 2380-2629 | Derived-type merge machinery |
| 2630-3006 | Reflective construction (`ctor_arg`, `ctor_param`, `ctor_registry`) |
| 3008-3064 | `as` / `cast` reference conversions |
| 3066-3326 | `object_ref<T>` |
| 3328-3589 | `instance` type-erased handle |
| 3591-3685 | Conversion helpers (`same_type_id`, `derived_from_desc`, `adjust_to`, `scalar_convertible`) |
| 3686-4126 | Method reflection |
| 4127-5436 | Field-registration helper macros |
| 5437-5585 | Type / enum registration macros |
| 5586-6745 | Template-info registration macros |
| 6747-7605 | Attribute / constructor registration macros |
| 7606-7676 | Method registration macros, tail include |

---

## 27. Other languages

- [中文版 (Chinese)](../zh/reflection.md)