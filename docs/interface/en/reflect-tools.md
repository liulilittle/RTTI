# Reflect Tools — Interface Reference (reflect / ioc / to_string / str)

Tools on top of the reflection core: the runtime query layer
(`<rtti/reflect.hpp>`), the dependency-injection container
(`<rtti/ioc.hpp>`), the unified `T <-> std::string` conversion layer
(`<rtti/to_string.hpp>`) and the string-operation toolkit (`<rtti/string_ops.hpp>`).
All documents are bilingual and mutually linked; the 简体中文 edition lives at
[`zh/reflect-tools.md`](../zh/reflect-tools.md).

- [中文 (简体中文)](../zh/reflect-tools.md)

---

## 0. Scope

| Header | Namespace | Covered in |
| --- | --- | --- |
| `<rtti/reflect.hpp>` | `rtti` | §1–§3 of this document |
| `<rtti/ioc.hpp>` | `rtti::ioc` | §4 of this document |
| `<rtti/to_string.hpp>` | `rtti` | §5 of this document |
| `<rtti/string_ops.hpp>` | `rtti::str` | §6 of this document |

`reflect.hpp` is a thin convenience layer over `fields_of<T>` from
`reflection.hpp`; it adds type introspection, field lookup by name, typed
get/set, runtime container operations, bounds-checked field/container-element
iterators and reflective instantiation. `ioc.hpp` builds the
dependency-injection container on top of the reflective-instantiation layer.
`to_string.hpp` converts any supported type to/from `std::string` through a
single pair of entry points. `string_ops.hpp` re-exports that layer under
`rtti::str` and adds pure string transforms.

### 0.1 Shared conventions (spec §0)

- **Fail-safe**: every runtime entry point returns an `error_code`, a `result`,
  `false`, a null pointer or an empty value on failure. Nothing throws from the
  query layer and nothing crashes.
- `noexcept`: all free functions that cannot fail are marked `noexcept`; object
  construction (`make_object`) is the only allocating surface and reports
  failure through `error_code`.
- Bounds-checked iterators never form or dereference a before-begin /
  one-past-end pointer (spec §0.2; see §2 of this document).

---

## 1. `reflect.hpp` — runtime query layer

### 1.1 Compile-time type-judgment conveniences

`rtti`-namespace aliases over the standard traits, so callers stay inside one
namespace (spec §19.2).

| Entity | Behaves as | reflect.hpp |
| --- | --- | --- |
| `template<class T, class U> struct is_same` | `std::is_same<T, U>` | :22 |
| `is_same_v` | — | :25 |
| `template<class Base, class Derived> struct is_base_of` | `std::is_base_of<Base, Derived>` | :27 |
| `is_base_of_v` | — | :30 |
| `template<class From, class To> struct is_convertible` | `std::is_convertible<From, To>` | :32 |
| `is_convertible_v` | — | :35 |
| `template<class From, class To> struct can_cast` | `std::is_convertible<From, To>` (derived/convertible-to readability) | :38 |
| `can_cast_v` | — | :41 |

### 1.2 Runtime type judgment

```cpp
template <typename T> bool is_instance_of(const void* obj, type_id expected) noexcept;
template <typename T> bool is(const instance& inst) noexcept;
template <typename T> bool is_derived_from(const instance& inst) noexcept;
```

- `is_instance_of<T>` — true when the object at `obj` is an instance of
  `expected`. For a polymorphic static type `T`, `typeid(*obj)` resolves the
  dynamic type at runtime; for a non-polymorphic `T` the comparison is static.
  Null arguments are never an instance of anything.
- `is<T>(inst)` — exact runtime type check on an instance handle (type_of
  equality; `instance::is<T>`).
- `is_derived_from<T>(inst)` — true when the instance's runtime type is `T` or
  transitively derives from `T` through the reflected base chain
  (`detail::derived_from_desc`). An invalid handle reports `false`.

```cpp
template <typename T> result<T*> dynamic_cast_ref(instance& inst) noexcept;
```

Relationships: identical type or derived-from-`T` through the offset-0
(non-virtual, primary) base chain → `ok()` with a pointer that borrows the
instance's object (lifetime == handle lifetime). Unrelated or base-of targets
yield `kind_mismatch` and a null pointer. **Known limitation** (pinned by
`JudgeTestDynamicCastRefVirtualBaseKnownLimit`): the returned pointer is
`static_cast<T*>(inst.data())` — valid only for a non-virtual single-inheritance
chain whose base sits at offset 0. `RTTI_DEFINE_TYPE_DERIVED` + a virtual or
secondary base is an unsupported combination for `dynamic_cast_ref`; it still
reports `ok()`, so the returned view must never be dereferenced.

```cpp
bool can_convert(const type_desc* src, const type_desc* target) noexcept;
bool can_convert(const instance& inst, const type_desc* target) noexcept;
```

`can_convert(desc, desc)` is true for identical type ids, a derived-from
relationship, or an implicitly-convertible scalar (`detail::scalar_convertible`).
The `instance` overload returns false for invalid handles and null targets.

### 1.3 `type_info<T>` — .NET-parity introspection handle

```cpp
template <typename T>
struct type_info {
    using value_type = T;
    static constexpr const char* name() noexcept;              // typeid(T).name()
    static constexpr std::size_t field_count() noexcept;       // rtti::field_count<T>()
    static const field_access_base* const* fields() noexcept;  // rtti::fields<T>()
    static const field_access_base* find_field(const char* field_name) noexcept;
};
template <typename T> type_info<T> get_type() noexcept;                  // akin to typeof
template <typename T> type_info<std::decay_t<T>> get_type(const T&) noexcept;
```

`name()` returns `typeid(T).name()`; `find_field` does a `std::strcmp` walk of
the field table and returns `nullptr` for an absent name or null argument.
`get_type(T)` mirrors `typeof(T)`, `get_type(obj)` mirrors `obj.GetType()`.

### 1.4 Field access by name

```cpp
template <typename T> const field_access_base* find_field(const char* name) noexcept;
template <typename T> bool get_field(const T& obj, const char* name, field_value& out) noexcept;
template <typename T> bool set_field(T& obj, const char* name, const field_value& in) noexcept;
```

- `find_field<T>` — the named field accessor, or `nullptr` when no reflected
  field matches (no throw, no UB).
- `get_field` — reads the field into `out` (`f->get`); returns `false` when the
  name does not match any reflected field.
- `set_field` — `f->set(obj, in)`; returns `false` on a missing field or a type
  mismatch the accessor rejects (a `field_value` carries its own kind, so kind
  mismatch is detected by the accessor).

### 1.5 Runtime container operations

```cpp
template <typename T> error_code add_field(T& obj, const char* name, const value_io* element) noexcept;
template <typename T> error_code add_field(T& obj, const char* name, const field_value& element) noexcept;
template <typename T> error_code erase_field_at(T& obj, const char* name, std::size_t index) noexcept;
template <typename T> error_code erase_field_key(T& obj, const char* name, string_view key) noexcept;
template <typename T> error_code clear_field(T& obj, const char* name) noexcept;
template <typename T> std::size_t field_size(const T& obj, const char* name, error_code* out_err = nullptr) noexcept;
template <typename T, typename Fn>
error_code for_each_element(const T& obj, const char* name, value_io* scratch, Fn&& fn);
```

Error mapping (all entry points): unknown field → `no_such_field`; field is not
a container → `kind_mismatch`; success → `ok`.

- `add_field` — appends one element to a container field. The `value_io*`
  overload reads the element from a caller value_io source (build it via your
  own `value_io` implementation); the `field_value` overload is for scalar
  element containers (`vector`/`list`/`deque` of arithmetic/string, `set`,
  `queue`/`stack`/`priority_queue`). Map fields must use the `value_io` form.
- `erase_field_at` — removes the `index`-th element of a sequence field.
- `erase_field_key` — removes one element by key from a map/set field.
- `clear_field` — empties a container field.
- `field_size` — element count of a container field. For a non-container field
  or unknown field, `*out_err` (nullable) receives the error and `0` is
  returned; on success `out_err` is set to `ok` when non-null.
- `for_each_element` — visits every element. The visitor receives the element
  index, the key (empty for non-associative containers) and a `value_io` node
  holding the element written into `scratch`. Returning `false` from the
  visitor stops the walk early.

```cpp
// scalar-only example: read every element's integer value
std::int64_t total = 0;
rtti::for_each_element(box, "items", &scratch,
    [&](std::size_t i, rtti::string_view key, const rtti::value_io* v) {
        std::int64_t n = 0;
        if (v->as_integer(n)) {
            total += n;
        }
        return i < 10; // stop after ten elements
    });
```

*The `value_io` protocol (kinds, `as_*`, `make_*`) is documented in
`convert.md` §2.1 and `reflection.md` §3.4.*

---

## 2. Field-array and container-element iterators

### 2.1 `field_iterator<T>` (spec §10.2)

```cpp
template <typename T> class field_iterator {
    using field_type   = const field_access_base*;
    using owner_type   = typename std::decay<T>::type;
    std::size_t index() const noexcept;
    bool is_reverse() const noexcept;
    field_type operator*() const noexcept;    // nullptr at boundary positions
    field_type operator->() const noexcept;
    field_iterator& operator++() noexcept;    // pre/post increment & decrement
    field_iterator operator++(int) noexcept;
    field_iterator& operator--() noexcept;
    field_iterator operator--(int) noexcept;
    bool operator==(const field_iterator& o) const noexcept;
    bool operator!=(const field_iterator& o) const noexcept;
};
template <typename T> field_iterator<T> field_begin() noexcept;  // (index 0)
template <typename T> field_iterator<T> field_end() noexcept;    // (index field_count)
template <typename T> field_iterator<T> field_rbegin() noexcept; // (index field_count, rev)
template <typename T> field_iterator<T> field_rend() noexcept;   // (index 0, rev)
template <typename T> field_iterator<T> begin() noexcept;  // == field_begin<T>
template <typename T> field_iterator<T> end() noexcept;    // == field_end<T>
template <typename T> field_iterator<T> rbegin() noexcept; // == field_rbegin<T>
template <typename T> field_iterator<T> rend() noexcept;   // == field_rend<T>
```

The iterator views the reflected field table through `(index, reverse)` and
never through raw array pointers. Every dereference stays inside the table
bounds: `end`, `rend` and a default-constructed iterator yield a null accessor
instead of forming a before-begin / one-past-end pointer. A forward iterator
starts at index 0 and `++` moves toward `field_count`; a reverse iterator
starts at `field_count` (`rbegin`) and `++` moves toward 0 (dereferencing
`index-1`, so the first field is visited before the scan ends at `rend`).
The table stores `const field_access_base*`, so one iterator type covers both
const and non-const `T`.

```cpp
for (auto it = rtti::field_begin<Point>(); it != rtti::field_end<Point>(); ++it) {
    const rtti::field_access_base* f = *it;   // f == nullptr only past the end
    ...
}
for (auto it = rtti::rbegin<Point>(); it != rtti::rend<Point>(); ++it) { ... } // backwards
```

### 2.2 `element_type<T>` and `element_iterator<T>` (spec §10.3/§10.4)

```cpp
template <typename T> const type_desc* element_type(const char* name) noexcept;
```

The element type of a container field, resolved from the field table's
`type_descs`. `nullptr` when the field is absent or not a container. Deliberate
spec deviation: a `const type_desc*` is returned (mirroring `type_of`, avoiding
a copy of a descriptor whose members are pointers) instead of a by-value
`type_desc`.

```cpp
template <typename T> class element_iterator {
    using owner_type = typename std::decay<T>::type;
    const field_access_base* field() const noexcept;
    const owner_type* owner() const noexcept;
    bool is_reverse() const noexcept;
    std::size_t index() const noexcept;          // logical 0-based position
    bool at_begin() const noexcept;
    bool at_end() const noexcept;
    element_iterator& operator++() noexcept;      // pre/post, both clamp
    element_iterator operator++(int) noexcept;
    element_iterator& operator--() noexcept;
    element_iterator operator--(int) noexcept;
    field_value element_value() const noexcept;  // empty / object marker on failure
    error_code  element_to(value_io* scratch) const noexcept;
    string_view key() const noexcept;
};
template <typename T>
error_code element_begin(const T& obj, const char* name, element_iterator<T>& out) noexcept;
template <typename T>
error_code element_end(const T& obj, const char* name, element_iterator<T>& out) noexcept;
template <typename T>
error_code element_rbegin(const T& obj, const char* name, element_iterator<T>& out) noexcept;
template <typename T>
error_code element_rend(const T& obj, const char* name, element_iterator<T>& out) noexcept;
```

The iterator holds the owning object and the field accessor, so it stays valid
while the object lives; the live container size is re-read and bounds-checked
before every dereference (spec §0).

- `element_value()` — scalar/string element value, or an empty `field_value`
  (kind `none`) out of bounds. Nested/container elements yield
  `field_value::of_object()`. String results point into buffers owned by the
  iterator and stay valid until it is advanced or destroyed.
- `element_to(scratch)` — writes the element through the format-agnostic
  `value_io` path. Out-of-bounds dereference returns `no_such_field` (there is
  no `out_of_range` code in `error.hpp`; `no_such_field` expresses "that
  element does not exist").
- `key()` — the element key for associative containers; a `set` reports its
  value as the key (numeric values use their decimal form; reals use the shared
  shortest round-tripping `detail::format_real` instead of `std::to_string`'s
  fixed 6-digit literal). Empty view for sequence fields or out-of-bounds.

### 2.3 Range-for over container elements

```cpp
rtti::element_iterator<Box> it;
for (rtti::element_begin(box, "nums", it);
     !it.at_end();
     ++it) {
    std::int64_t n = 0;
    it.element_to(&scratch);              // or it.element_value().as_integer()
}
```

---

## 3. Reflective construction & ORM bindings (spec §18)

```cpp
template <typename T> object_ref<T> make_object();                             // RAII, new T()
template <typename T> result<object_ref<T>> make_object(const ctor_arg* args, std::size_t count);
template <typename T> result<object_ref<T>> make_object(std::initializer_list<ctor_arg> args);
```

`make_object<T>()` default-constructs a reflected `T` and returns an owning
`object_ref<T>` in RAII mode (auto release). The `args` overloads construct
`T` through the registered constructor table (`ctor_registry<T>`, populated by
`RTTI_DEFINE_CTOR`): `ctor_prepare` maps name/position arguments to the
parameter pack, then `ctor_new_impl` runs the placement construction. On any
registry/argument error (no factory, null `args` with `count > 0`, a missing
required parameter, a kind/type mismatch) the `result` carries the
`error_code` — `kind_mismatch` / `missing_key` — and never throws or crashes
(spec §0).

### 3.1 Custom allocation / release (spec §18.2 placement new)

```cpp
template <typename T, typename Alloc,
          std::enable_if_t<detail::is_raw_allocator_v<Alloc>, int> = 0>
object_ref<T> make_object(Alloc& alloc);                                              // T() placement
template <typename T, typename Alloc, std::enable_if_t<...> = 0>
object_ref<T> make_object(Alloc& alloc, const ctor_arg* args, std::size_t count);
template <typename T, typename Alloc, std::enable_if_t<...> = 0>
object_ref<T> make_object(Alloc& alloc, std::initializer_list<ctor_arg> args);
```

`Alloc` supplies raw memory via `memory_arena`-style `alloc(size, align)` or
`std::pmr`-style `allocate(size, align)` (`detail::has_alloc_bytes` /
`has_allocate_bytes` detect which). `T` is placement-constructed into that
memory and the handle runs in **MANUAL** mode: the caller must call
`object_ref::destroy()` explicitly (no auto release). The two modes are
mutually exclusive by construction, preventing double frees. A null handle is
returned when the registry rejects the arguments (nothing is allocated) or the
allocator returns null.

Release semantics (`detail::placement_releaser`): copyable allocators are
snapshotted into the release closure so `destroy()` stays valid even after the
caller's allocator object has gone out of scope (R2-3). Non-copyable allocators
(pmr `memory_resource`) follow their standard contract — the resource must
outlive every allocation it supplied (closure keeps the reference). An
arena-style allocator exposes no per-object release, so its closure is a no-op
(memory is bulk-freed by the owner).

### 3.2 ORM contract (spec §18.2)

```cpp
template <typename T> result<object_ref<T>> create_instance(const ctor_arg* args, std::size_t count);
template <typename T> result<object_ref<T>> create_instance(std::initializer_list<ctor_arg> args);

struct row_cell { const char* name; field_value value; };   // one Bind row cell

template <typename T>
error_code bind(object_ref<T>& obj, const row_cell* row, std::size_t count) noexcept;
template <typename T>
error_code bind(object_ref<T>& obj, std::initializer_list<row_cell> row) noexcept;
```

`create_instance<T>(args, count)` is `make_object<T>(args, count)` (the
`CreateInstance` contract). `bind<T>` applies a result-set row to the
instance's reflected fields by name (`object_ref::set`); a null `row` with
`count > 0` is `kind_mismatch`, each failed cell stops the scan and returns the
first `no_such_field` / `kind_mismatch`, leaving the already-bound fields on
the instance.

```cpp
rtti::result<rtti::object_ref<Point>> r = rtti::create_instance<Point>({
    { "x", rtti::field_value::of(std::int64_t(3)) },
    { "y", rtti::field_value::of(std::int64_t(4)) },
});
assert(r.ok());
```

---

## 4. `ioc.hpp` — dependency-injection container

`rtti::ioc::container` builds instances through the reflective-instantiation
layer (spec §18) and manages their lifetime. The container owns every singleton
it creates and releases it on destruction (RAII); transient instances are
handed to the caller as an owning `object_ref<T>`.

### 4.1 Instance lifetime policy

```cpp
namespace rtti::ioc {
enum class lifetime {
    transient, // a fresh instance per resolve; the caller owns the handle
    singleton  // created once and reused; the container owns the instance
};
}
```

Transient: every `resolve` builds a fresh instance; the caller owns the
`object_ref<T>` handle. Singleton: created once, reused for the container's
lifetime.

**Singleton borrow handles** are non-owning views. Calling `destroy()` on one
is caller misuse but fails safely (spec §0): the destroyed singleton is marked
dead and the **next** `resolve<T>` re-creates it — the container never hands
out a use-after-free instance and never runs the destructor twice (a dead
singleton's block is freed without a second `~T()`). The borrow handle holds
only the liveness flag (`detail::guarded_singleton` + a shared `live` flag
captured by value into the deleter), so this contract holds only while the
container lives: the borrow must never outlive the container.

### 4.2 Registration

```cpp
template <typename T>    void register_type(lifetime lf = lifetime::singleton);
template <typename T>    void register_type(const char* name, lifetime lf);
template <typename Iface, typename Impl> void register_as(lifetime lf = lifetime::singleton);
template <typename Iface, typename Impl> void register_as(const char* name, lifetime lf);
template <typename T>
void register_factory(std::function<std::unique_ptr<T>(container&)> factory,
                      lifetime lf = lifetime::singleton);
template <typename T>
void register_factory(const char* name,
                      std::function<std::unique_ptr<T>(container&)> factory,
                      lifetime lf);
```

- `register_type<T>` — registers `T` resolvable by type (with an optional
  name). `resolve<T>()` builds it with its reflected constructor (dependency
  injection) or default construction when no constructor registry is present.
- `register_as<Iface, Impl>` — maps an interface/abstract base to a concrete
  implementation. `static_assert(std::is_base_of<Iface, Impl>)`; `resolve<Iface>`
  returns the `Impl` instance viewed as `Iface`. The mapping requires a single
  inheritance chain with an offset-0 base (a plain `static_cast`);
  `resolve` on the transient path deletes through `object_ref<T>`, so the
  interface should declare a **virtual destructor** so deletion is well defined.
- `register_factory<T>` — registers a factory `std::function<std::unique_ptr<T>(container&)>`;
  the factory receives the container so it can resolve its own dependencies. A
  factory returning null reports `kind_mismatch`.

Registration keys on `(std::type_index, name)`; `name != nullptr` disambiguates
multiple entries of the same C++ type. Key for the dependency lookup is the
stripped base type (`detail::dep_target` removes pointers/references/const and
`std::shared_ptr`).

### 4.3 Resolution & queries

```cpp
template <typename T> result<object_ref<T>> resolve();            // == resolve<T>(nullptr)
template <typename T> result<object_ref<T>> resolve(const char* name);
template <typename T> result<object_ref<T>> get();               // == resolve<T>()
template <typename T> result<object_ref<T>> get(const char* name);
template <typename T> bool registered() const noexcept;
std::size_t count() const noexcept;
```

`resolve<T>`: unregistered → `kind_mismatch`; re-entrant cycle → `kind_mismatch`
(the container keeps an in-flight type stack; re-entry of the same type is
rejected instead of recursing forever). Singleton entries return the borrowed
view (see §4.1); transient entries return a caller-owned handle.

### 4.4 Dependency injection

A constructor parameter whose underlying type (`detail::dep_target`) matches a
registered dependency is auto-injected. Supported shapes (default):

| Parameter `D` | Injection |
| --- | --- |
| `X`, `X const&` / `X*` (borrow) | borrow / share the container-owned **singleton** |
| `std::shared_ptr<X>` (share) | aliasing `shared_ptr` into the singleton control block |
| plain `X` (value) | copies a singleton, or moves a fresh transient instance |
| non-dependency scalar | optional parameters value-init (`D{}`); required missing → `missing_key` |

A borrow/share of a *transient* dependency is `kind_mismatch` (nothing to
reuse); a plain `X` against a transient factory creates a fresh instance that
is moved into the constructor argument. Cycle detection applies during
injection as well: re-entry of a type already on the in-flight stack is
`kind_mismatch`.

```cpp
struct Engine { double power = 0.0; };                                // reflected
struct Car {
    Engine engine; std::int64_t seats = 0;
    Car() = default;
    Car(Engine e, std::int64_t s) : engine(e), seats(s) {}
};
RTTI_DEFINE_CTOR(Car, ("engine", Engine, false), ("seats", std::int64_t, true))

rtti::ioc::container c;
c.register_type<Engine>();          // singleton by default
c.register_type<Car>();

auto re = c.resolve<Engine>();      // singleton borrow view
assert(re.ok() && re.value->set("power", rtti::field_value::of(350.0)) == rtti::error_code::ok);

rtti::result<rtti::object_ref<Car>> rc = c.resolve<Car>();
assert(rc.ok());                    // "engine" param injected by DI; "seats" optional -> 0
assert(re.value->get() != nullptr); // borrow view stays valid while c lives
rtti::field_value seats;
assert(rtti::get_field(*rc.value->get(), "seats", seats) && seats.as_integer() == 0);
```

---

## 5. `to_string.hpp` — unified `T <-> std::string`

Every supported type converts directly to/from a `std::string` through one
pair of entry points (spec §16).

```cpp
template <typename T> std::string to_string(const T& v);
template <typename T> error_code from_string(string_view s, T& out);
template <typename T> std::optional<T> from_string(string_view s);
```

### 5.1 Serialization forms

| Type | `to_string` form | `from_string` acceptance |
| --- | --- | --- |
| `bool` | `"true"` / `"false"` | `"true"`/`"false"` else `kind_mismatch` |
| integral | decimal digits (`std::to_string`) | whole-string parse; `overflow` outside range, `invalid_number` on malformed text |
| float / double | shortest round-tripping text via `std::to_chars`, `".0"` suffix when the shortest form has no dot/exponent, deterministic `"inf"`/`"-inf"`/`"nan"` | `from_chars` direct; `inf`/`-inf`/`infinity`/`-infinity`/`nan` tokens; `overflow`/`invalid_number` |
| `std::string` / `string_view` / char `basic_string` | raw bytes, no quoting | copies raw bytes |
| `char[N]` | up to the first `'\0'` | over-long input → `kind_mismatch` (never overruns); buffer zero-filled then copied |
| registered enum | the registered label | label match → value, else `kind_mismatch` |
| `date_time` | canonical ISO 8601 UTC (exact sub-second round-trip) | ISO 8601 or a numeric Unix epoch (10/13/16/19/20 digits infer seconds/ms/us/ns, spec §14.1) |
| unregistered enum | `""` (unsupported type) | `kind_mismatch` |
| containers / reflected structs / `optional` / `variant` / `tuple` / `pair` / `std::array` / `vector<bool>` / native arrays | compact single-line JSON string (see §5.3) | parses the JSON then runs `from_value` |

`to_string` returns an **empty string** when the conversion itself fails (e.g.
an enum whose value has no label). Scalar `from_string` fast paths report
`overflow` for values outside the target range and `invalid_number` for
malformed text; kind mismatches report `kind_mismatch`.

### 5.2 Architecture: self-owning `text_node` tree (spec §16.2)

Container/reflected round trips reuse the existing `to_value` / `from_value`
dispatch chain through a minimal in-header `value_io` tree —
`rtti::detail::text_node` — plus a compact single-line JSON serializer
(`serialize_text`) and compact JSON parser (`parse_text`). The tree is
self-owning (scalars store `std::string` by value; children are held through
`unique_ptr`), so the whole round trip inherits every existing converter
(`optional`, `variant`, `tuple`, `pair`, `array`, `vector<bool>`,
queue/stack/priority_queue adaptors, native/multi-dim C arrays, ADL hooks) for
free. Serialization is deterministic: object key order is source order, never
sorted. Non-finite reals inside containers emit the bare tokens
`"inf"`/`"-inf"`/`"nan"` (the string layer owns its parser, so exact round-trip
matters more than the `null` mapping the JSON adapters use).

The tree and the parser live in `src/text_node.cpp` (compiled once), so the
header stays light: only the class declaration and entry points are in
`to_string.hpp:87-133`. `from_string` error mapping: parser codes
(`invalid_root` / `unexpected_token` / `unterminated_string` / `invalid_number`
/ `invalid_utf8` / `too_deep`) for malformed text, `kind_mismatch` / `missing_key`
for conversion type mismatches, `overflow` for numeric range violations.

### 5.3 Arbitrary-base integer bridge (spec §15.4 / §16.1)

```cpp
std::string      to_string_base(std::uint64_t value, unsigned base, const char* digits = nullptr);
error_code       from_string_base(string_view s, unsigned base, std::uint64_t& out);
std::optional<std::uint64_t> from_string_base(string_view s, unsigned base);
std::string      to_string_base_signed(std::int64_t value, unsigned base);
error_code       from_string_base_signed(string_view s, unsigned base, std::int64_t& out);
std::optional<std::int64_t> from_string_base_signed(string_view s, unsigned base);
```

Thin wrappers over `rtti::detail::to_base`/`from_base` (bases 2..64) with the
optional custom digit table; signed variants carry a leading `-`. Invalid
bases / illegal digits / overflow report exactly like the underlying
`base_convert.hpp` API (documented in `utils.md` §2).

```cpp
assert(rtti::to_string(42) == "42");
assert(rtti::to_string(3.5) == "3.5");
assert(rtti::to_string_base(255, 16) == "ff");
assert(rtti::from_string<int>("-7").value() == -7);
assert(rtti::from_string<std::vector<int>>("[1,2,3]").value() == std::vector<int>({1,2,3}));
```

---

## 6. `string_ops.hpp` — `rtti::str` toolkit

`rtti::str` re-exports the unified conversion layer and adds pure string
transforms (spec §7.6 / Task 8a).

### 6.1 Conversion aliases

```cpp
using rtti::from_string;   // == rtti::from_string (same entity, using-declaration)
using rtti::to_string;     // == rtti::to_string

std::string          to_base(std::uint64_t value, unsigned base, const char* digits = nullptr);
std::optional<std::uint64_t> from_base(string_view s, unsigned base);
error_code           from_base(string_view s, unsigned base, std::uint64_t& out) noexcept;
std::string          to_base_signed(std::int64_t value, unsigned base);
std::optional<std::int64_t> from_base_signed(string_view s, unsigned base);
error_code           from_base_signed(string_view s, unsigned base, std::int64_t& out) noexcept;
```

`rtti::str::to_string` **is** `rtti::to_string` (same entity, brought in by a
using-declaration): the whole supported type surface — scalars, registered
enums, `date_time`, containers, `optional`, `variant`, `tuple`, `pair`,
`std::array`, `vector<bool>`, native arrays, reflected structs — converts with
zero drift. The `from_base` family bridges the existing arbitrary-base helpers
(bases 2..64) unchanged.

### 6.2 String operations

```cpp
std::vector<std::string> split(string_view s, char sep) noexcept;
std::vector<std::string> split(string_view s, string_view sep) noexcept;
std::string join(const std::vector<std::string>& parts, string_view sep) noexcept;

std::string trim(string_view s, string_view chars = " \t\r\n") noexcept;
std::string ltrim(string_view s, string_view chars = " \t\r\n") noexcept;
std::string rtrim(string_view s, string_view chars = " \t\r\n") noexcept;

std::string to_lower(string_view s) noexcept;   // ASCII case fold, byte-wise
std::string to_upper(string_view s) noexcept;

bool starts_with(string_view s, string_view prefix) noexcept;  // true for empty prefix
bool ends_with(string_view s, string_view suffix) noexcept;    // true for empty suffix

std::string replace(string_view s, string_view from, string_view to) noexcept;
std::string replace_all(string_view s, string_view from, string_view to) noexcept;
std::string repeat(string_view s, std::size_t count) noexcept;

std::string pad_left(string_view s, std::size_t width, char fill = ' ') noexcept;
std::string pad_right(string_view s, std::size_t width, char fill = ' ') noexcept;

std::string reverse(string_view s) noexcept;    // byte order

std::string quote(string_view s) noexcept;
std::string unquote(string_view s) noexcept;
error_code  unquote(string_view s, std::string& out) noexcept;

std::string escape(string_view s) noexcept;
std::string unescape(string_view s) noexcept;
error_code  unescape(string_view s, std::string& out) noexcept;
```

Semantics (pure transforms are `noexcept`; parsing reports `error_code` and
never crashes — spec §0):

- `split` — tokens on every `sep` occurrence. Empty input → empty vector;
  leading/trailing separators yield empty edge tokens
  (`split("a,b,,c", ',') == {"a","b","","c"}`). An empty string separator
  treats the whole input as one token.
- `join` — parts joined with `sep`; an empty part list joins to `""`.
- `trim` / `ltrim` / `rtrim` — strip leading/trailing occurrences of `chars`
  (default space, tab, CR, LF). An empty `chars` leaves input unchanged.
- `to_lower` / `to_upper` — ASCII case folding, byte-wise; non-ASCII bytes pass
  through untouched.
- `starts_with` / `ends_with` — both return `true` for an empty
  prefix/suffix.
- `replace` — the **first** occurrence of `from`; `replace_all` — every
  non-overlapping occurrence. An empty `from` returns input unchanged (never an
  infinite loop); an empty `to` removes the matched text.
- `repeat` — `s` repeated `count` times; `count == 0` yields `""` (an
  oversized count that cannot be represented fails-safe to `""`).
- `pad_left` / `pad_right` — pad with `fill` up to `width`; input at or above
  `width` is returned unchanged.
- `quote` — wraps `s` in double quotes and escapes interior JSON/C-style, so
  `unquote(quote(s)) == s` always round-trips.
- `unquote` — `kind_mismatch` when the input does not begin with `"`;
  `unterminated_string` when it does not end with `"`; interior escape errors
  propagate from `unescape`. The `std::string` overload returns `""` on any
  parse failure.
- `escape` — encodes `"`, `\`, `\b`, `\f`, `\n`, `\r`, `\t` and every control
  byte below `0x20` as `\u00XX`.
- `unescape` — decodes those escapes; `\uXXXX` accepts four hex digits, and a
  high surrogate immediately followed by a low surrogate is combined into a
  single code point. A trailing lone backslash → `unterminated_string`, an
  unknown escape → `unexpected_token`, a malformed `\uXXXX` (including an
  unpaired surrogate) → `invalid_utf8`.

```cpp
using rtti::str::split;
using rtti::str::to_string;
assert(to_string(255) == "255");          // rtti::to_string via alias
assert(split("a,b,,c", ',') == (std::vector<std::string>{"a","b","","c"}));
assert(rtti::str::trim("  hi \t\n") == "hi");
assert(rtti::str::replace_all("aXbXc", "X", "--") == "a--b--c");
assert(rtti::str::unquote(rtti::str::quote("a\"b")) == "a\"b");          // round-trip
assert(rtti::str::escape("a\nb") == "a\\nb");
```

---

## 7. Related documentation

- Reflection core: [`reflection.md`](reflection.md) — registration macros,
  `fields_of`, `field_value`, `value_io`, `field_access_base`, `type_desc`,
  `object_ref` / `instance`, `ctor_arg`/`ctor_registry`.
- Value conversion: [`convert.md`](convert.md) — `converter<T>`,
  `from_value`/`to_value`, named conversion, number/node formatting, `memory`.
- Tools & support: [`utils.md`](utils.md) — `base_convert`, `date_time`,
  `naming`, `version`, `rtti_fwd`.
- Design & architecture: [`../design/en/architecture.md`](../design/en/architecture.md).
- 简体中文 edition: [`zh/reflect-tools.md`](../zh/reflect-tools.md).

Constructor params are prepared through the same `ctor_registry<T>` /
`ctor_prepare` machinery as `make_object`: parameter names come from
`RTTI_DEFINE_CTOR` triples, and DI fills a dependency before optional defaults
are consulted. Without a registered constructor registry the container falls
back to `new T()` and no injection happens this way.