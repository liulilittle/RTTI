#pragma once
// Public runtime reflection query API (.NET-grade ergonomics).
// A thin convenience layer over fields_of<T>: type introspection, field
// lookup by name, typed get/set, and range-for iteration. All entry points
// delegate to fields_of<T> from reflection.hpp.
// English comments only; C++17; no third-party dependencies.

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

#include <rtti/reflection.hpp>

namespace rtti {

// ---------------------------------------------------------------------------
// Compile-time type-judgment conveniences (spec 19.2). Thin rtti-namespace
// aliases over the standard traits so callers stay inside one namespace.
// ---------------------------------------------------------------------------

template <typename T, typename U>
struct is_same : std::is_same<T, U> {};
template <typename T, typename U>
inline constexpr bool is_same_v = is_same<T, U>::value;

template <typename Base, typename Derived>
struct is_base_of : std::is_base_of<Base, Derived> {};
template <typename Base, typename Derived>
inline constexpr bool is_base_of_v = is_base_of<Base, Derived>::value;

template <typename From, typename To>
struct is_convertible : std::is_convertible<From, To> {};
template <typename From, typename To>
inline constexpr bool is_convertible_v = is_convertible<From, To>::value;

// `can_cast` expresses "derived/convertible-to" readability for a value.
template <typename From, typename To>
struct can_cast : std::is_convertible<From, To> {};
template <typename From, typename To>
inline constexpr bool can_cast_v = can_cast<From, To>::value;

// ---------------------------------------------------------------------------
// Runtime type judgment (spec 19.2). All entry points fail safely (false /
// nullptr / error_code), never crash, per spec section 0.
// ---------------------------------------------------------------------------

// True when the object at `obj` is an instance of `expected`: for a
// polymorphic static type T, typeid(*obj) resolves the dynamic type at
// runtime; for a non-polymorphic T the comparison is static. Null arguments
// are never an instance of anything.
template <typename T>
bool is_instance_of(const void* obj, type_id expected) noexcept {
    if (obj == nullptr || expected == nullptr) {
        return false;
    }
    const T* p = static_cast<const T*>(obj);
    return typeid(*p) == *expected;
}

// Exact runtime type check on an instance handle (type_of equality).
template <typename T>
bool is(const instance& inst) noexcept {
    return inst.template is<T>();
}

// True when the instance's runtime type is T or transitively derived from T
// (the reflected base chain); the instance is then usable as a T. A common
// invalid handle reports false.
template <typename T>
bool is_derived_from(const instance& inst) noexcept {
    if (!inst.valid()) {
        return false;
    }
    return detail::derived_from_desc(inst.type_of(), type_of<T>());
}

// Reflectively view the instance's object as T after the type-id chain is
// verified. Succeeds when the instance's runtime type is T or derives from T
// through the offset-0 (primary, non-virtual) base chain, which is the
// single-inheritance case registered by RTTI_DEFINE_TYPE_DERIVED. On success
// the returned pointer borrows the instance's object (lifetime == handle
// lifetime). Unsafe casts are rejected: unrelated or base-of targets yield an
// error and a null pointer.
//
// KNOWN LIMITATION (README-level semantics): dynamic_cast_ref implements an
// offset-0 view -- the returned pointer is static_cast<T*>(inst.data()), the
// object's own address reinterpreted as T. THIS IS ONLY VALID FOR A NON-VIRTUAL
// SINGLE-INHERITANCE CHAIN WHERE THE BASE SUBOBJECT SITS AT OFFSET 0. For a
// virtual base target the real base subobject lives at a compiler-chosen
// offset, so the returned pointer aliases the WRONG memory and MUST NOT be
// dereferenced; for a secondary (non-primary) base the same offset problem
// applies. The library cannot detect or correct this: the runtime base chain
// (RTTI_DEFINE_TYPE_DERIVED) records only type ids -- no offsets, no
// virtual-ness -- and `instance` keeps no typed pointer to the dynamic type on
// which a real dynamic_cast could run. RTTI_DEFINE_TYPE_DERIVED + a virtual
// base (or a multi-base chain) is therefore an UNSUPPORTED COMBINATION for
// dynamic_cast_ref; it still reports ok() (the chain check passes), and it
// spec section 0 requirement is that the returned view is never used. The
// virtual-inheritance behavior is pinned by JudgeTestDynamicCastRefVirtualBaseKnownLimit.
template <typename T>
result<T*> dynamic_cast_ref(instance& inst) noexcept {
    result<T*> r;
    if (!inst.valid()) {
        r.err.code = error_code::no_such_field;
        return r;
    }
    if (!detail::derived_from_desc(inst.type_of(), type_of<T>())) {
        r.err.code = error_code::kind_mismatch;
        return r;
    }
    r.value = static_cast<T*>(inst.data());
    return r;
}

// type_desc-level convertibility (same / derived / scalar), used both directly
// and by the instance overload.
inline bool can_convert(const type_desc* src, const type_desc* target) noexcept {
    if (src == nullptr || target == nullptr) {
        return false;
    }
    if (detail::same_type_id(src->id, target->id)) {
        return true;
    }
    if (detail::derived_from_desc(src, target)) {
        return true;
    }
    return detail::scalar_convertible(src->kind, target->kind);
}

// Reflection-level convertibility of an instance to a target type: identical
// type, a base type in the instance's chain, or an implicitly-convertible
// scalar. Invalid handles and null targets convert to nothing.
inline bool can_convert(const instance& inst, const type_desc* target) noexcept {
    if (!inst.valid() || target == nullptr) {
        return false;
    }
    return can_convert(inst.type_of(), target);
}

// Introspection handle for a reflected type.
template <typename T>
struct type_info {
    using value_type = T;

    static constexpr const char* name() noexcept {
        return typeid(T).name();
    }
    static constexpr std::size_t field_count() noexcept {
        return rtti::field_count<T>();
    }
    static const field_access_base* const* fields() noexcept {
        return rtti::fields<T>();
    }
    // Locate a field by name; nullptr when absent.
    static const field_access_base* find_field(const char* field_name) noexcept {
        if (field_name == nullptr) {
            return nullptr;
        }
        const std::size_t n = rtti::field_count<T>();
        const auto* const* arr = rtti::fields<T>();
        for (std::size_t i = 0; i < n; ++i) {
            const field_access_base* fa = arr[i];
            if (fa != nullptr && fa->name != nullptr &&
                std::strcmp(fa->name, field_name) == 0) {
                return fa;
            }
        }
        return nullptr;
    }
};

// .NET parity: get_type(T) akin to typeof(T); get_type(obj) akin to
// obj.GetType().
template <typename T>
type_info<T> get_type() noexcept {
    return {};
}

template <typename T>
type_info<std::decay_t<T>> get_type(const T&) noexcept {
    return {};
}

// Field access by name at runtime. Returns false (no throw, no UB) when the
// name does not match any reflected field.
template <typename T>
const field_access_base* find_field(const char* name) noexcept {
    return type_info<T>::find_field(name);
}

template <typename T>
bool get_field(const T& obj, const char* name, field_value& out) noexcept {
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        return false;
    }
    out = f->get(static_cast<const void*>(&obj));
    return true;
}

template <typename T>
bool set_field(T& obj, const char* name, const field_value& in) noexcept {
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        return false;
    }
    return f->set(static_cast<void*>(&obj), in);
}

// --- Runtime container operations ------------------------------------------
// Add one element to a container field. `element` is read from the provided
// value_io source (callers build it through their value_io implementation).
template <typename T>
error_code add_field(T& obj, const char* name, const value_io* element) noexcept {
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        return error_code::no_such_field;
    }
    return f->field_add(static_cast<void*>(&obj), element);
}

// Add one scalar element to a container field whose elements are scalars
// (vector/list/deque of arithmetic or string, set, queue/stack/priority_queue).
// map fields are not supported through this overload (use the value_io form).
template <typename T>
error_code add_field(T& obj, const char* name, const field_value& element) noexcept {
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        return error_code::no_such_field;
    }
    return f->field_add_scalar(static_cast<void*>(&obj), element);
}

// Erase the index-th element of a sequence field (vector/list).
template <typename T>
error_code erase_field_at(T& obj, const char* name, std::size_t index) noexcept {
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        return error_code::no_such_field;
    }
    return f->erase_at(static_cast<void*>(&obj), index);
}

// Erase an element by key from a map/set field.
template <typename T>
error_code erase_field_key(T& obj, const char* name, string_view key) noexcept {
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        return error_code::no_such_field;
    }
    return f->erase_key(static_cast<void*>(&obj), key);
}

// Clear a container field.
template <typename T>
error_code clear_field(T& obj, const char* name) noexcept {
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        return error_code::no_such_field;
    }
    return f->clear(static_cast<void*>(&obj));
}

// Number of elements in a container field. For a non-container field or an
// unknown field, `out_err` (nullable) receives the error and 0 is returned.
template <typename T>
std::size_t field_size(const T& obj, const char* name, error_code* out_err = nullptr) noexcept {
    if (out_err != nullptr) {
        *out_err = error_code::ok;
    }
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        if (out_err != nullptr) {
            *out_err = error_code::no_such_field;
        }
        return 0;
    }
    if (!f->is_container()) {
        if (out_err != nullptr) {
            *out_err = error_code::kind_mismatch;
        }
        return 0;
    }
    return f->size(const_cast<void*>(static_cast<const void*>(&obj)));
}

// Visit every element of a container field. The visitor receives the element
// index, the key (empty for non-associative containers), and a value_io node
// holding the element (written into `scratch`). Returning false stops early.
template <typename T, typename Fn>
error_code for_each_element(const T& obj, const char* name, value_io* scratch, Fn&& fn) {
    const field_access_base* f = find_field<std::decay_t<T>>(name);
    if (f == nullptr) {
        return error_code::no_such_field;
    }
    if (!f->is_container()) {
        return error_code::kind_mismatch;
    }
    return f->for_each(
        const_cast<void*>(static_cast<const void*>(&obj)), scratch,
        typename field_access_base::element_visitor(std::forward<Fn>(fn)));
}

// --- Field-array iterators (spec section 10.2) -----------------------------
// field_iterator views the reflected field table through an index plus a
// direction flag, never through raw array pointers. This keeps every
// dereference inside the table bounds: out-of-range positions (end, rend and
// a default-constructed iterator) yield a null field-accessor pointer, so no
// before-begin / one-past-end pointer is ever formed (spec section 0.2).
// A forward iterator starts at index 0 and ++ moves toward field_count; a
// reverse iterator starts at field_count (rbegin) and ++ moves toward 0
// (rend), so `for (it = rbegin; it != rend; ++it)` walks the fields
// backwards (spec 10.2). Reverse dereference reads index-1, which is how the
// table's first field (index 0) is visited before the scan ends at rend.
// The table stores const field_access_base* pointers, so a single iterator
// type covers both const and non-const T.

template <typename T>
class field_iterator {
public:
    using field_type = const field_access_base*;
    using owner_type = typename std::decay<T>::type;

    field_iterator() noexcept = default;

    // Raw-state constructor; normally produced by field_begin/field_end/
    // field_rbegin/field_rend. `index` is the scan cursor (see above).
    explicit field_iterator(std::size_t index, bool reverse = false) noexcept
        : index_(index), reverse_(reverse) {}

    field_type operator*() const noexcept { return deref_(); }
    field_type operator->() const noexcept { return deref_(); }
    // The current scan cursor (forward: 0..field_count; reverse: field_count
    // down to 0, dereferencing index-1).
    std::size_t index() const noexcept { return index_; }
    bool is_reverse() const noexcept { return reverse_; }

    field_iterator& operator++() noexcept {
        const std::size_t n = count_();
        if (reverse_) {
            if (index_ > 0) {
                --index_;
            }
        } else if (index_ < n) {
            ++index_;
        }
        return *this;
    }
    field_iterator operator++(int) noexcept {
        field_iterator t(*this);
        ++(*this);
        return t;
    }
    field_iterator& operator--() noexcept {
        const std::size_t n = count_();
        if (reverse_) {
            if (index_ < n) {
                ++index_;
            }
        } else if (index_ > 0) {
            --index_;
        }
        return *this;
    }
    field_iterator operator--(int) noexcept {
        field_iterator t(*this);
        --(*this);
        return t;
    }

    bool operator==(const field_iterator& o) const noexcept {
        return index_ == o.index_ && reverse_ == o.reverse_;
    }
    bool operator!=(const field_iterator& o) const noexcept {
        return !(*this == o);
    }

private:
    std::size_t count_() const noexcept { return field_count<owner_type>(); }
    // Bounds-checked table access; boundary positions yield nullptr.
    field_type deref_() const noexcept {
        const std::size_t n = count_();
        if (reverse_) {
            if (index_ == 0 || index_ > n) {
                return nullptr; // rend / past-rbegin
            }
            return fields<owner_type>()[index_ - 1];
        }
        if (index_ >= n) {
            return nullptr; // end / past-end
        }
        return fields<owner_type>()[index_];
    }

    std::size_t index_ = 0; // null (default) iterator reads nothing
    bool reverse_ = false;
};

template <typename T>
field_iterator<T> field_begin() noexcept {
    using U = typename std::decay<T>::type;
    return field_iterator<T>(0, false);
}
template <typename T>
field_iterator<T> field_end() noexcept {
    using U = typename std::decay<T>::type;
    return field_iterator<T>(field_count<U>(), false);
}
// Reverse starts at field_count (rbegin, reading the last field) and ends at
// index 0 (rend, a boundary position that yields nullptr when dereferenced).
template <typename T>
field_iterator<T> field_rbegin() noexcept {
    using U = typename std::decay<T>::type;
    return field_iterator<T>(field_count<U>(), true);
}
template <typename T>
field_iterator<T> field_rend() noexcept {
    using U = typename std::decay<T>::type;
    return field_iterator<T>(0, true);
}

// Bare aliases for the field-array iterators. The previous begin/end
// returned raw array pointers; these return field_iterator (spec 10.2).
template <typename T>
field_iterator<T> begin() noexcept {
    return field_begin<T>();
}
template <typename T>
field_iterator<T> end() noexcept {
    return field_end<T>();
}
template <typename T>
field_iterator<T> rbegin() noexcept {
    return field_rbegin<T>();
}
template <typename T>
field_iterator<T> rend() noexcept {
    return field_rend<T>();
}

// --- Container-element iteration (spec sections 10.3/10.4) ------------------

namespace detail {

// Minimal value_io sink used by element_iterator. It captures the scalar
// written most recently (string payloads are copied so views stay valid) and
// accepts object/array writes as no-ops, so any to_value conversion succeeds
// without materializing a tree. The read side always fails; it is not used
// by element iteration.
class passive_node : public value_io {
public:
    value_io::kind type() const noexcept override {
        switch (scalar_.type()) {
            case field_value::kind::boolean: return value_io::kind::boolean;
            case field_value::kind::integer: return value_io::kind::integer;
            case field_value::kind::real: return value_io::kind::real;
            case field_value::kind::string: return value_io::kind::string;
            default: return value_io::kind::null;
        }
    }
    bool as_bool(bool& v) const noexcept override {
        if (scalar_.type() != field_value::kind::boolean) {
            return false;
        }
        v = scalar_.as_bool();
        return true;
    }
    bool as_integer(int64_t& v) const noexcept override {
        if (scalar_.type() != field_value::kind::integer) {
            return false;
        }
        v = scalar_.as_integer();
        return true;
    }
    bool as_real(double& v) const noexcept override {
        if (scalar_.type() != field_value::kind::real) {
            return false;
        }
        v = scalar_.as_real();
        return true;
    }
    bool as_string(string_view& v) const noexcept override {
        if (scalar_.type() != field_value::kind::string) {
            return false;
        }
        v = scalar_.as_string();
        return true;
    }
    std::size_t size() const noexcept override { return 0; }
    value_io* at(std::size_t) override { return nullptr; }
    value_io* at(std::size_t) const override { return nullptr; }
    bool get_key(string_view, value_io*&) override { return false; }
    bool get_key(string_view, value_io*&) const override { return false; }
    bool get_key_at(std::size_t, string_view&, value_io*&) const override {
        return false;
    }
    value_io* make_array() override { return this; }
    void push_back(value_io*) override {}
    value_io* make_object() override { return this; }
    void set_key(string_view, value_io*) override {}
    value_io* make_scalar(field_value v) override {
        if (v.type() == field_value::kind::string) {
            owned_.assign(v.as_string().data(), v.as_string().size());
            scalar_ = field_value::of(string_view(owned_));
        } else {
            scalar_ = v;
        }
        return this;
    }
    value_io* make_node() override { return this; }

    // The scalar captured by the most recent make_scalar; none when the
    // element was an object/container (only no-op writes happened).
    field_value take() const noexcept { return scalar_; }

private:
    field_value scalar_;
    std::string owned_;
};

} // namespace detail

// The element type of a container field, resolved from the field table's
// type_descs (spec 10.4.3). Returns nullptr when the field is absent or not
// a container. Deliberate deviation from the spec sketch (`type_desc` by
// value): a const pointer is returned instead, mirroring type_of and
// avoiding a copy of a descriptor whose members are pointers.
template <typename T>
const type_desc* element_type(const char* name) noexcept {
    using U = typename std::decay<T>::type;
    const field_access_base* f = find_field<U>(name);
    if (f == nullptr || !f->is_container()) {
        return nullptr;
    }
    const auto* const* arr = fields<U>();
    const std::size_t n = field_count<U>();
    for (std::size_t i = 0; i < n; ++i) {
        if (arr[i] == f) {
            const type_desc* fd = fields_of<U>::desc[i].type();
            if (fd != nullptr && fd->kind == type_kind::container &&
                fd->as_container != nullptr) {
                return fd->as_container->element();
            }
            return nullptr;
        }
    }
    return nullptr;
}

// Index-based iterator over the elements of a container field (spec 10.3).
// This is not a standard InputIterator; it exposes the documented
// element_value / element_to / index / key accessors plus at_begin/at_end
// and ++/-- traversal. It holds the owning object and the field accessor, so
// it stays valid while the object lives; the live container size is re-read
// and bounds-checked before every dereference (spec section 0: out-of-bounds
// reads are forbidden). element_value returns an empty field_value and
// element_to returns no_such_field for out-of-bounds positions (error.hpp
// has no out_of_range code; reusing no_such_field expresses "that element
// does not exist").
template <typename T>
class element_iterator {
public:
    using owner_type = typename std::decay<T>::type;

    element_iterator() noexcept = default;

    // Raw-state constructor; normally produced by the element_* free
    // functions below.
    element_iterator(const owner_type* obj, const field_access_base* fa,
                     std::size_t cursor, bool reverse) noexcept
        : obj_(obj), fa_(fa), cursor_(cursor), reverse_(reverse) {}

    // The reflected container field and the owning instance.
    const field_access_base* field() const noexcept { return fa_; }
    const owner_type* owner() const noexcept { return obj_; }
    // True when created via element_rbegin/element_rend.
    bool is_reverse() const noexcept { return reverse_; }

    // Logical 0-based index of the element pointed at. Reverse iterators
    // report the position in reversed order (rbegin -> size-1, ... , 0).
    std::size_t index() const noexcept {
        if (reverse_) {
            return cursor_ == 0 ? size_t(0) : cursor_ - 1;
        }
        return cursor_;
    }

    // Position checks; a forward iterator is at begin/end when the cursor is
    // 0/size, a reverse iterator when the cursor is size/0.
    bool at_begin() const noexcept {
        if (obj_ == nullptr || fa_ == nullptr) {
            return true;
        }
        return cursor_ == (reverse_ ? size_() : 0);
    }
    bool at_end() const noexcept {
        if (obj_ == nullptr || fa_ == nullptr) {
            return true;
        }
        return cursor_ == (reverse_ ? 0 : size_());
    }

    // Forward traversal moves the cursor upward; reverse traversal moves it
    // downward towards rend. Both clamp at the container boundary.
    element_iterator& operator++() noexcept {
        const std::size_t n = size_();
        if (reverse_) {
            if (cursor_ > 0) {
                --cursor_;
            }
        } else if (cursor_ < n) {
            ++cursor_;
        }
        return *this;
    }
    element_iterator& operator--() noexcept {
        const std::size_t n = size_();
        if (reverse_) {
            if (cursor_ < n) {
                ++cursor_;
            }
        } else if (cursor_ > 0) {
            --cursor_;
        }
        return *this;
    }
    element_iterator operator++(int) noexcept {
        element_iterator t(*this);
        ++(*this);
        return t;
    }
    element_iterator operator--(int) noexcept {
        element_iterator t(*this);
        --(*this);
        return t;
    }

    bool operator==(const element_iterator& o) const noexcept {
        return obj_ == o.obj_ && fa_ == o.fa_ && cursor_ == o.cursor_ &&
               reverse_ == o.reverse_;
    }
    bool operator!=(const element_iterator& o) const noexcept {
        return !(*this == o);
    }

    // Element value for scalar/string elements: out-of-bounds dereference
    // yields an empty (kind none) field_value; nested/container elements
    // yield the object marker (kind object). String results point into
    // buffers owned by the iterator and stay valid until it is advanced or
    // destroyed.
    field_value element_value() const noexcept {
        if (!readable_()) {
            return field_value();
        }
        const type_desc* et = rtti::element_type<owner_type>(fa_->name);
        if (et != nullptr) {
            switch (et->kind) {
                case type_kind::boolean:
                case type_kind::integer:
                case type_kind::real:
                case type_kind::string:
                    break;
                default:
                    return field_value::of_object();
            }
        }
        detail::passive_node sink;
        const std::size_t target = logical_index_();
        bool reached = false;
        const error_code e = fa_->for_each(
            const_cast<void*>(static_cast<const void*>(obj_)), &sink,
            [target, &reached](std::size_t i, string_view,
                               const value_io*) -> bool {
                if (i == target) {
                    reached = true;
                    return false;
                }
                return true;
            });
        if (e != error_code::ok || !reached) {
            return field_value();
        }
        field_value result = sink.take();
        if (result.type() == field_value::kind::string) {
            owned_.assign(result.as_string().data(), result.as_string().size());
            result = field_value::of(string_view(owned_));
        }
        if (result.type() == field_value::kind::none) {
            // In-bounds element but no scalar captured: nested structure.
            return field_value::of_object();
        }
        return result;
    }

    // Writes the element into `scratch` through the format-agnostic value_io
    // path (scalars, strings and nested structures). Out-of-bounds
    // dereference returns no_such_field (spec section 0: no OOB reads).
    error_code element_to(value_io* scratch) const noexcept {
        if (scratch == nullptr) {
            return error_code::no_such_field;
        }
        if (!readable_()) {
            return error_code::no_such_field;
        }
        const std::size_t target = logical_index_();
        bool reached = false;
        const error_code e = fa_->for_each(
            const_cast<void*>(static_cast<const void*>(obj_)), scratch,
            [target, &reached](std::size_t i, string_view,
                               const value_io*) -> bool {
                if (i == target) {
                    reached = true;
                    return false;
                }
                return true;
            });
        if (e != error_code::ok) {
            return e;
        }
        return reached ? error_code::ok : error_code::no_such_field;
    }

    // Key of the current element for associative containers; a set reports
    // its value as the key (numeric values use their decimal form). Empty
    // string_view for sequence fields. The view stays valid while the
    // container lives (map keys) or until the iterator is advanced or
    // destroyed (set values, which are copied into the iterator's buffer).
    // Out-of-bounds dereference yields an empty view.
    string_view key() const noexcept {
        if (!readable_()) {
            return string_view();
        }
        const std::size_t target = logical_index_();
        string_view visitor_key;
        detail::passive_node sink;
        bool reached = false;
        fa_->for_each(const_cast<void*>(static_cast<const void*>(obj_)), &sink,
                      [&](std::size_t i, string_view k,
                          const value_io*) -> bool {
                          if (i == target) {
                              visitor_key = k;
                              reached = true;
                              return false;
                          }
                          return true;
                      });
        if (!reached) {
            return string_view();
        }
        if (!visitor_key.empty()) {
            return visitor_key; // map field: the for_each visitor supplies it
        }
        if (!set_field_()) {
            return string_view(); // sequence field: no key
        }
        // Set field: key == value; present the element as a string.
        const field_value fv = sink.take();
        switch (fv.type()) {
            case field_value::kind::string:
                owned_.assign(fv.as_string().data(), fv.as_string().size());
                return string_view(owned_);
            case field_value::kind::integer:
                owned_ = std::to_string(fv.as_integer());
                return string_view(owned_);
            case field_value::kind::boolean:
                owned_ = fv.as_bool() ? "1" : "0";
                return string_view(owned_);
            case field_value::kind::real:
                owned_ = std::to_string(fv.as_real());
                return string_view(owned_);
            default:
                return string_view();
        }
    }

private:
    // Live element count of the container field.
    std::size_t size_() const noexcept {
        if (obj_ == nullptr || fa_ == nullptr) {
            return 0;
        }
        return fa_->size(const_cast<void*>(static_cast<const void*>(obj_)));
    }
    // True when the current cursor points at a dereferencable element.
    bool readable_() const noexcept {
        const std::size_t n = size_();
        if (reverse_) {
            return cursor_ > 0 && cursor_ <= n;
        }
        return cursor_ < n;
    }
    // Position of the element the cursor denotes, in the container's
    // iteration order (reverse iterators read cursor-1).
    std::size_t logical_index_() const noexcept {
        if (reverse_) {
            return cursor_ == 0 ? size_t(0) : cursor_ - 1;
        }
        return cursor_;
    }
    // type_desc of the container field itself (not its element type),
    // resolved from the field table.
    const type_desc* field_desc_() const noexcept {
        if (obj_ == nullptr || fa_ == nullptr) {
            return nullptr;
        }
        const auto* const* arr = fields<owner_type>();
        const std::size_t n = field_count<owner_type>();
        for (std::size_t i = 0; i < n; ++i) {
            if (arr[i] == fa_) {
                return fields_of<owner_type>::desc[i].type();
            }
        }
        return nullptr;
    }
    // True when the container field is a set (its element is the key).
    bool set_field_() const noexcept {
        const type_desc* fd = field_desc_();
        if (fd == nullptr || fd->kind != type_kind::container ||
            fd->as_container == nullptr) {
            return false;
        }
        return fd->as_container->family == container_family::set ||
               fd->as_container->family == container_family::unordered_set;
    }

    const owner_type* obj_ = nullptr;
    const field_access_base* fa_ = nullptr;
    std::size_t cursor_ = 0;
    bool reverse_ = false;
    mutable std::string owned_; // stable buffer for string element_value results
};

template <typename T>
error_code element_begin(const T& obj, const char* name,
                         element_iterator<T>& out) noexcept {
    using U = typename element_iterator<T>::owner_type;
    const field_access_base* fa = find_field<U>(name);
    if (fa == nullptr) {
        return error_code::no_such_field;
    }
    if (!fa->is_container()) {
        return error_code::kind_mismatch;
    }
    out = element_iterator<T>(&obj, fa, 0, false);
    return error_code::ok;
}

template <typename T>
error_code element_end(const T& obj, const char* name,
                       element_iterator<T>& out) noexcept {
    using U = typename element_iterator<T>::owner_type;
    const field_access_base* fa = find_field<U>(name);
    if (fa == nullptr) {
        return error_code::no_such_field;
    }
    if (!fa->is_container()) {
        return error_code::kind_mismatch;
    }
    const std::size_t n =
        fa->size(const_cast<void*>(static_cast<const void*>(&obj)));
    out = element_iterator<T>(&obj, fa, n, false);
    return error_code::ok;
}

template <typename T>
error_code element_rbegin(const T& obj, const char* name,
                          element_iterator<T>& out) noexcept {
    using U = typename element_iterator<T>::owner_type;
    const field_access_base* fa = find_field<U>(name);
    if (fa == nullptr) {
        return error_code::no_such_field;
    }
    if (!fa->is_container()) {
        return error_code::kind_mismatch;
    }
    const std::size_t n =
        fa->size(const_cast<void*>(static_cast<const void*>(&obj)));
    out = element_iterator<T>(&obj, fa, n, true);
    return error_code::ok;
}

template <typename T>
error_code element_rend(const T& obj, const char* name,
                        element_iterator<T>& out) noexcept {
    using U = typename element_iterator<T>::owner_type;
    const field_access_base* fa = find_field<U>(name);
    if (fa == nullptr) {
        return error_code::no_such_field;
    }
    if (!fa->is_container()) {
        return error_code::kind_mismatch;
    }
    out = element_iterator<T>(&obj, fa, 0, true);
    return error_code::ok;
}

// ---------------------------------------------------------------------------
// Reflective instantiation + full operations (spec section 18).
// make_object<T> constructs a reflected T at runtime; create_instance<T> is the
// ORM CreateInstance contract; bind<T> is the ORM Bind<T> contract. Argument
// injection errors always return error_code (never throw, never crash): a
// missing required parameter reports missing_key; a type/unmatched/registry
// problem reports kind_mismatch.
// ---------------------------------------------------------------------------

namespace detail {

// Raw-memory allocation through a caller allocator: memory_arena-style
// alloc(size, align) or std::pmr-style allocate(size, align).
template <typename Alloc, typename = void>
struct has_alloc_bytes : std::false_type {};
template <typename Alloc>
struct has_alloc_bytes<
    Alloc, std::void_t<decltype(std::declval<Alloc&>().alloc(
               std::declval<std::size_t>(), std::declval<std::size_t>()))>>
    : std::true_type {};

template <typename Alloc, typename = void>
struct has_allocate_bytes : std::false_type {};
template <typename Alloc>
struct has_allocate_bytes<
    Alloc, std::void_t<decltype(std::declval<Alloc&>().allocate(
               std::declval<std::size_t>(), std::declval<std::size_t>()))>>
    : std::true_type {};

template <typename Alloc>
inline constexpr bool is_raw_allocator_v =
    has_alloc_bytes<Alloc>::value || has_allocate_bytes<Alloc>::value;

template <typename Alloc, typename = void>
struct allocator_allocate {
    static void* get(Alloc& a, std::size_t size, std::size_t align) {
        return a.allocate(size, align);
    }
};
template <typename Alloc>
struct allocator_allocate<Alloc, std::enable_if_t<has_alloc_bytes<Alloc>::value>> {
    static void* get(Alloc& a, std::size_t size, std::size_t align) {
        return a.alloc(size, align);
    }
};

// Per-object release for the placement path. memory_arena / monotonic pools
// are bulk-freed by their owner, so the default releaser is a no-op and
// destroy() only runs the destructor; std::pmr resources expose deallocate and
// get a real release.
template <typename Alloc, typename = void>
struct allocator_release {
    static void call(Alloc&, void*, std::size_t, std::size_t) noexcept {}
};
template <typename Alloc>
struct allocator_release<
    Alloc, std::void_t<decltype(std::declval<Alloc&>().deallocate(
               std::declval<void*>(), std::declval<std::size_t>(),
               std::declval<std::size_t>()))>> {
    static void call(Alloc& a, void* p, std::size_t size, std::size_t align) noexcept {
        a.deallocate(p, size, align);
    }
};

// R2-3: the placement release closure must not capture the caller's allocator
// by reference. destroy() may legitimately be called after the caller's
// allocator object has gone out of scope (the handle owns the release, not the
// caller), and a by-reference capture would then deallocate through freed
// state (spec section 0: no dangling). Copyable allocators are snapshotted
// into the closure, so the release runs against the handle's own copy and is
// safe no matter when the caller's object dies. Non-copyable allocators (pmr
// memory_resources) follow their standard contract -- the resource must
// outlive every allocation it supplied -- so the release keeps the reference;
// an arena-style allocator exposes no per-object release at all, so its
// closure is a no-op and never dereferences the reference.
template <typename Alloc>
struct placement_releaser {
    template <typename T>
    static std::function<void(void*)> make(Alloc& a) {
        const std::size_t size = sizeof(T);
        const std::size_t align = alignof(T);
        if constexpr (std::is_copy_constructible<Alloc>::value) {
            // Snapshot the allocator BEFORE any allocation exists: the release
            // closure owns its own copy, so destroy() stays valid even when
            // the caller's allocator object has already been destroyed.
            Alloc snapshot = a;
            return [snapshot, size, align](void* q) mutable noexcept {
                detail::allocator_release<Alloc>::call(snapshot, q, size, align);
            };
        } else {
            // Non-copyable (e.g. pmr memory_resource): by standard contract
            // the caller's resource must outlive the allocations it supplied.
            // An arena-style allocator has no per-object release, so its
            // closure is a no-op and never dereferences this reference.
            Alloc& snapshot = a;
            return [&snapshot, size, align](void* q) noexcept {
                detail::allocator_release<Alloc>::call(snapshot, q, size, align);
            };
        }
    }
};

} // namespace detail

// RAII default construction.
template <typename T>
object_ref<T> make_object() {
    return object_ref<T>::adopt(new T());
}

// Construction with runtime parameter injection (spec 18.3). The returned
// handle runs in RAII mode (auto release).
template <typename T>
result<object_ref<T>> make_object(const ctor_arg* args, std::size_t count) {
    result<object_ref<T>> r;
    if ((count > 0 && args == nullptr) || ctor_registry<T>::factory == nullptr) {
        r.err.code = error_code::kind_mismatch;
        return r;
    }
    using Tuple = typename ctor_registry<T>::param_types;
    Tuple values;
    const error_code ec = detail::ctor_prepare<T, Tuple>(args, count, values);
    if (ec != error_code::ok) {
        r.err.code = ec;
        return r;
    }
    r.value = object_ref<T>::adopt(detail::ctor_new_impl<T, Tuple>(
        values, std::make_index_sequence<std::tuple_size<Tuple>::value>()));
    return r;
}

template <typename T>
result<object_ref<T>> make_object(std::initializer_list<ctor_arg> args) {
    return make_object<T>(args.begin(), args.size());
}

// --- Custom memory allocation / release (spec 18.2 placement new) ----------
// The placement overloads take a caller allocator that supplies raw memory
// (memory_arena-style alloc(size,align) or std::pmr-style allocate(size,align)).
// T is placement-constructed into that memory and the handle runs in MANUAL
// mode: the caller must call object_ref::destroy() explicitly (no auto
// release). The no-allocator make_object overloads run in RAII mode. The two
// modes are mutually exclusive by construction, preventing double frees. A
// null handle is returned when the registry rejects the arguments (nothing is
// allocated) or the allocator fails.

template <typename T, typename Alloc,
          std::enable_if_t<detail::is_raw_allocator_v<Alloc>, int> = 0>
object_ref<T> make_object(Alloc& alloc) {
    auto releaser = detail::placement_releaser<Alloc>::template make<T>(alloc);
    void* raw = detail::allocator_allocate<Alloc>::get(alloc, sizeof(T), alignof(T));
    if (raw == nullptr) {
        return object_ref<T>{};
    }
    T* p = ::new (raw) T();
    return object_ref<T>::placement(p, std::move(releaser));
}

template <typename T, typename Alloc,
          std::enable_if_t<detail::is_raw_allocator_v<Alloc>, int> = 0>
object_ref<T> make_object(Alloc& alloc, const ctor_arg* args, std::size_t count) {
    if ((count > 0 && args == nullptr) || ctor_registry<T>::factory == nullptr) {
        return object_ref<T>{};
    }
    using Tuple = typename ctor_registry<T>::param_types;
    Tuple values;
    const error_code ec = detail::ctor_prepare<T, Tuple>(args, count, values);
    if (ec != error_code::ok) {
        return object_ref<T>{};
    }
    auto releaser = detail::placement_releaser<Alloc>::template make<T>(alloc);
    void* raw = detail::allocator_allocate<Alloc>::get(alloc, sizeof(T), alignof(T));
    if (raw == nullptr) {
        return object_ref<T>{};
    }
    T* p = detail::ctor_place_impl<T, Tuple>(
        raw, values, std::make_index_sequence<std::tuple_size<Tuple>::value>());
    return object_ref<T>::placement(p, std::move(releaser));
}

template <typename T, typename Alloc,
          std::enable_if_t<detail::is_raw_allocator_v<Alloc>, int> = 0>
object_ref<T> make_object(Alloc& alloc, std::initializer_list<ctor_arg> args) {
    return make_object<T>(alloc, args.begin(), args.size());
}

// --- ORM contract (spec 18.2) ------------------------------------------------
// CreateInstance(ctor_args): maps columns to constructor parameters through the
// registered ctor table, by name or by position.
template <typename T>
result<object_ref<T>> create_instance(const ctor_arg* args, std::size_t count) {
    return make_object<T>(args, count);
}

template <typename T>
result<object_ref<T>> create_instance(std::initializer_list<ctor_arg> args) {
    return make_object<T>(args);
}

// One Bind row cell: a reflected field name plus its runtime value.
struct row_cell {
    const char* name;
    field_value value;
};

// Bind<T>: apply a result-set row to the instance's reflected fields by name.
// Returns the first error (no_such_field / kind_mismatch) and stops; the
// instance keeps the fields already bound.
template <typename T>
error_code bind(object_ref<T>& obj, const row_cell* row, std::size_t count) noexcept {
    if (row == nullptr && count > 0) {
        return error_code::kind_mismatch;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const error_code e = obj.set(row[i].name, row[i].value);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename T>
error_code bind(object_ref<T>& obj, std::initializer_list<row_cell> row) noexcept {
    return bind(obj, row.begin(), row.size());
}

} // namespace rtti