#pragma once
// Compile-time field tables + runtime-iterable field access for RTTI.
// A reflected type registers a specialization of rtti::fields_of<T> (usually
// via the RTTI_DEFINE_TYPE macro from Task 5; manual specializations are
// supported too). Fields are read/written through member-pointer accessors,
// never byte offsets: safe for private members and for inheritance.
// English comments only; C++17; no third-party dependencies.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <typeinfo>
#include <utility>
#include <vector>

#include <rtti/memory.hpp>
#include <rtti/naming.hpp>

namespace rtti {

// Forward declare the format-agnostic sink/source (defined in convert.hpp).
// field_access_base references it only through pointers so the reflect core
// header stays free of any format dependency.
class value_io;

// Forward declare the format-agnostic converters (defined in convert.hpp).
// typed_field_access bridges to them for nested/container members; the
// declarations are enough for the reflect core header to stay dependency-free.
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

// Reflected-member hookup (defined in convert.hpp). Letting convert.hpp shape
// the member write makes it possible to give date_time members a
// sub-second-preserving output lane (ISO text) while the scalar default stays
// numeric epoch seconds, keeping this core header dependency-free.
template <typename M>
error_code member_to_value(value_io* dst, const M& v);
template <typename M>
error_code member_to_value_named(value_io* dst, const M& v,
                                 const rtti::detail::naming_policy& np);

// Reflected type registry. A type participates in reflection iff this
// template is specialized (by the RTTI_DEFINE_TYPE macro or by hand).
template <typename T, typename = void>
struct fields_of;

// Direct base types of a reflected type (spec 19). RTTI_DEFINE_TYPE_DERIVED
// specializes this to record the base whose fields merge into the derived
// type's field table and whose type_id appears in the runtime base chain.
// The primary template is an empty list (a type with no reflected bases).
template <typename T, typename = void>
struct base_types {
    using list = std::tuple<>;
};

// --- Container shape traits (pure type features; core, not format-specific) --
// Match containers by shape with member detection so custom allocators,
// comparators and hashers flow through untouched.

// Matches any std::basic_string specialization (any char type, traits,
// allocator). Excluded from the sequence family because a string is a
// scalar (text) type for conversion purposes.
template <typename T>
struct is_char_string : std::false_type {};
template <typename Char, typename Traits, typename Alloc>
struct is_char_string<std::basic_string<Char, Traits, Alloc>> : std::true_type {};

// Has push_back(value_type) + begin/end: vector, list, deque.
// std::basic_string is excluded via is_char_string: it satisfies the shape
// but is a scalar (string) type for conversion purposes.
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

// Has key_type + insert(value_type): both map and set families.
template <typename T, typename = void>
struct is_associative : std::false_type {};
template <typename T>
struct is_associative<T, std::void_t<typename T::key_type,
                                     typename T::value_type,
                                     decltype(std::declval<T&>().insert(
                                         std::declval<typename T::value_type>()))>>
    : std::true_type {};

// Map family additionally exposes mapped_type (value_type is a pair).
template <typename T, typename = void>
struct is_map : std::false_type {};
template <typename T>
struct is_map<T, std::void_t<typename T::mapped_type, typename T::key_type>>
    : std::true_type {};

// Set family is associative but has no mapped_type.
template <typename T>
struct is_set : std::bool_constant<is_associative<T>::value && !is_map<T>::value> {};

// queue: push/pop/front/back.
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

// stack: push/pop/top, no front/back.
template <typename T, typename = void>
struct is_stack : std::false_type {};
template <typename T>
struct is_stack<T, std::void_t<typename T::value_type,
                               decltype(std::declval<T&>().push(
                                   std::declval<typename T::value_type>())),
                               decltype(std::declval<T&>().pop()),
                               decltype(std::declval<T&>().top())>>
    : std::true_type {};

// priority_queue: push/pop/top plus a value_compare member type.
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

// Has push_back(value_type): vector, deque, list, basic_string.
template <typename T, typename = void>
struct has_push_back : std::false_type {};
template <typename T>
struct has_push_back<T, std::void_t<decltype(std::declval<T&>().push_back(
    std::declval<typename T::value_type>()))>> : std::true_type {};

// Forward declaration: has_data is defined later (with the container-family
// discrimination traits); is_tuple needs it to tell std::array from tuple.
// The default argument is supplied here so has_data<T> is valid where the
// trait is used; the later definition does not repeat it.
template <typename T, typename = void>
struct has_data;

// T is std::optional<U> (std::is_same against T's own value_type).
template <typename T, typename = void>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<T, std::void_t<typename T::value_type>> : std::bool_constant<
    std::is_same_v<T, std::optional<typename T::value_type>>> {};

// T exposes std::variant_size (any std::variant specialization).
template <typename T, typename = void>
struct is_variant : std::false_type {};
template <typename T>
struct is_variant<T, std::void_t<typename std::variant_size<T>::type>>
    : std::true_type {};

// T exposes std::tuple_size but is not std::array. std::array also
// specializes std::tuple_size, so exclude it via has_data: arrays are
// contiguous (data()), tuples are not.
template <typename T, typename = void>
struct is_tuple : std::false_type {};
template <typename T>
struct is_tuple<T, std::void_t<typename std::tuple_size<T>::type>>
    : std::bool_constant<!has_data<T>::value> {};

// T has first/second members (std::pair).
template <typename T, typename = void>
struct is_pair : std::false_type {};
template <typename T>
struct is_pair<T, std::void_t<decltype(std::declval<T&>().first),
                              decltype(std::declval<T&>().second)>>
    : std::true_type {};

// Fixed-size sequence: value_type + begin/end but no push_back (std::array).
// basic_string is excluded via is_char_string; optional/variant/tuple have no
// begin/end, so they cannot match. Associative containers (map/set) expose
// value_type + begin/end without push_back too, but they are key-based, not
// index-addressable sequences, so is_associative excludes them (otherwise a
// fixed-sequence converter would try out[i] on std::map and fail to compile).
template <typename T, typename = void>
struct is_fixed_sequence : std::false_type {};
template <typename T>
struct is_fixed_sequence<T, std::enable_if_t<
    !is_char_string<T>::value && !has_push_back<T>::value &&
        !is_associative<T>::value,
    std::void_t<typename T::value_type,
                decltype(std::declval<T&>().begin()),
                decltype(std::declval<T&>().end())>>> : std::true_type {};

// C-style array (T[N], T[N][M], T[A][B][C]): a native array shape with no
// value_type/push_back/begin, so is_sequence/is_fixed_sequence cannot see it
// (std::begin would compile but would change element semantics). char[N] is
// excluded: a C char array is a fixed string target, not an index-addressable
// numeric sequence (spec §12.2).
template <typename T>
struct is_c_array
    : std::bool_constant<
          std::is_array<T>::value &&
          !std::is_same<std::remove_extent_t<T>, char>::value> {};

// Multidimensional C array: a native array whose element type is itself a
// native array (T[N][M], T[A][B][C], ...). One-dimensional T[N] is not.
template <typename T>
struct is_md_array
    : std::bool_constant<
          is_c_array<T>::value && is_c_array<std::remove_extent_t<T>>::value> {};

// A reflected struct participates in the field-table path. Detects both the
// intrusive nested rtti_registration and the non-intrusive fields_of<T>
// specialization (which exposes a static `count`).
template <typename T, typename = void>
struct is_reflected : std::false_type {};
template <typename T>
struct is_reflected<T, std::void_t<decltype(fields_of<T>::count)>>
    : std::true_type {};

// Key handling for associative containers (string keys today; the generic
// fallback supports any key constructible from a string_view).
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

// Runtime type identifier: the address of a std::type_info.
using type_id = const std::type_info*;

// Variant-like value that can hold one reflected field's scalar payload.
class field_value {
public:
    enum class kind { none, boolean, integer, real, string, object };

    field_value() noexcept : k_(kind::none) {}

    static field_value of(bool v) noexcept {
        field_value f;
        f.k_ = kind::boolean;
        f.b_ = v;
        return f;
    }
    static field_value of(int64_t v) noexcept {
        field_value f;
        f.k_ = kind::integer;
        f.i_ = v;
        return f;
    }
    static field_value of(double v) noexcept {
        field_value f;
        f.k_ = kind::real;
        f.d_ = v;
        return f;
    }
    static field_value of(string_view v) noexcept {
        field_value f;
        f.k_ = kind::string;
        f.s_ = v;
        return f;
    }
    static field_value of_object() noexcept {
        field_value f;
        f.k_ = kind::object;
        return f;
    }

    kind type() const noexcept { return k_; }
    bool is_none() const noexcept { return k_ == kind::none; }

    // Accessors; each returns a fallback when the stored kind differs.
    bool as_bool() const noexcept { return k_ == kind::boolean ? b_ : false; }
    int64_t as_integer() const noexcept { return k_ == kind::integer ? i_ : 0; }
    double as_real() const noexcept { return k_ == kind::real ? d_ : 0.0; }
    string_view as_string() const noexcept {
        return k_ == kind::string ? s_ : string_view();
    }

private:
    kind k_;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0.0;
    string_view s_;
};

// Converts a field_value into a typed destination. Returns false on kind
// mismatch. Single template with compile-time dispatch (no overload sets, so
// MSVC cannot report ambiguous calls for int-like member types).
template <typename M>
inline bool field_value_to(M& out, const field_value& v) noexcept {
    if constexpr (std::is_same<M, bool>::value) {
        if (v.type() != field_value::kind::boolean) {
            return false;
        }
        out = v.as_bool();
        return true;
    } else if constexpr (std::is_integral<M>::value) {
        if (v.type() != field_value::kind::integer) {
            return false;
        }
        out = static_cast<M>(v.as_integer());
        return true;
    } else if constexpr (std::is_floating_point<M>::value) {
        if (v.type() != field_value::kind::real) {
            return false;
        }
        out = static_cast<M>(v.as_real());
        return true;
    } else if constexpr (std::is_same<M, string_view>::value) {
        if (v.type() != field_value::kind::string) {
            return false;
        }
        out = v.as_string();
        return true;
    } else if constexpr (std::is_same<M, std::string>::value) {
        if (v.type() != field_value::kind::string) {
            return false;
        }
        out.assign(v.as_string().data(), v.as_string().size());
        return true;
    } else {
        return false;
    }
}

// Abstract source/sink bridge between the reflect core and any storage model
// (a parse tree, a DOM, a builder). Kind mirrors JSON-ish value categories.
// Defined here (not in convert.hpp) because field_access_base's runtime
// container operations call into it; convert.hpp consumes it via #include.
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
    // Enumerate the index-th key/value pair of an object. Required to read
    // associative containers without leaking format details. Returns false
    // when index is out of range or the value is not an object.
    virtual bool get_key_at(std::size_t index, string_view& key,
                            value_io*& out) const = 0;

    // Write side (sink). make_* return the node itself for chaining.
    virtual value_io* make_array() = 0;
    virtual void push_back(value_io* v) = 0;
    virtual value_io* make_object() = 0;
    virtual void set_key(string_view key, value_io* v) = 0;
    virtual value_io* make_scalar(field_value v) = 0;
    // Create an independent child node owned by the same tree. Used by the
    // core container converters to build element/value nodes.
    virtual value_io* make_node() = 0;
};

// Uniform, runtime-iterable view of one reflected member. The derived
// typed_field_access<T, M> supplies the concrete read/write. The value_io*
// overloads bridge to the format-agnostic converters (defined in convert.hpp)
// for nested/container members; they are two-phase lookup so the core header
// stays free of any format dependency.
struct field_access_base {
    const char* name;
    type_id type;

    field_access_base(const char* n, type_id t) noexcept : name(n), type(t) {}

    virtual field_value get(const void* obj) const = 0;
    virtual bool set(void* obj, const field_value& v) const = 0;

    // Format-agnostic read: write this member into sink `dst`.
    virtual error_code get_value(const void* obj, value_io* dst) const = 0;
    // Format-agnostic write: read this member from source `src`.
    virtual error_code set_value(void* obj, const value_io* src) const = 0;

    // Named-conversion read/write: like get_value/set_value but routed through
    // to_value_named / from_value_named so the naming policy applies
    // recursively to nested reflected structs (direct struct members and
    // container elements). The base default falls back to the plain accessors,
    // so a type-erased member keeps the no-naming behavior when it is not
    // overridden.
    virtual error_code get_value_named(const void* obj, value_io* dst,
                                       const rtti::detail::naming_policy& np) const {
        (void)np;
        return get_value(obj, dst);
    }
    virtual error_code set_value_named(void* obj, const value_io* src,
                                       const rtti::detail::naming_policy& np) const {
        (void)np;
        return set_value(obj, src);
    }

    // True when this member's value is null-equivalent (an empty optional or a
    // null pointer). Used by skip_empty to drop null fields. Default false.
    virtual bool is_null_value(const void* obj) const {
        (void)obj;
        return false;
    }

    // True when the member is one of the supported container families.
    virtual bool is_container() const = 0;

    // --- Runtime container operations --------------------------------------
    // Add one element (read from `element`) to a container member. Returns
    // kind_mismatch when the member is not a supported container.
    virtual error_code field_add(void* obj, const value_io* element) const = 0;
    // Add one scalar element to a container member whose value_type converts
    // from the field_value. map members are unsupported here (use field_add
    // with a key/value object node); kind_mismatch otherwise.
    virtual error_code field_add_scalar(void* obj, const field_value& element) const = 0;
    // Erase the index-th element of a sequence member (vector/list). Returns
    // kind_mismatch for non-sequence members, no_such_field when out of range.
    virtual error_code erase_at(void* obj, std::size_t index) const = 0;
    // Erase by key from a map/set member. kind_mismatch for non-associative
    // members, no_such_field when the key is absent.
    virtual error_code erase_key(void* obj, string_view key) const = 0;
    // Clear a container member. kind_mismatch for non-container members.
    virtual error_code clear(void* obj) const = 0;
    // Number of elements in a container member.
    virtual std::size_t size(void* obj) const = 0;
    // Visit each element of a container member. Each element is written into
    // the caller-provided scratch node before the visitor runs; returning
    // false stops iteration early (not an error). kind_mismatch when the
    // member is not a supported container.
    using element_visitor = std::function<bool(std::size_t, string_view,
                                               const value_io*)>;
    virtual error_code for_each(void* obj, value_io* scratch,
                                const element_visitor& fn) const = 0;
};

// Typed accessor for member M T::*ptr on type T. Uses member-pointer
// indirection so private members still work when the macro is emitted inside
// the class (intrusive variant).
template <typename T, typename M>
struct typed_field_access : field_access_base {
    M T::*ptr;

    typed_field_access(const char* n, M T::*p, type_id t) noexcept
        : field_access_base{n, t}, ptr(p) {}

    field_value get(const void* obj) const override {
        const auto& o = *static_cast<const T*>(obj);
        return to_field_value(o.*ptr);
    }

    bool set(void* obj, const field_value& v) const override {
        auto& o = *static_cast<T*>(obj);
        return field_value_to(o.*ptr, v);
    }

    error_code get_value(const void* obj, value_io* dst) const override {
        const auto& o = *static_cast<const T*>(obj);
        return rtti::member_to_value(dst, o.*ptr);
    }

    error_code set_value(void* obj, const value_io* src) const override {
        auto& o = *static_cast<T*>(obj);
        return rtti::from_value(src, o.*ptr);
    }

error_code get_value_named(const void* obj, value_io* dst,
                           const rtti::detail::naming_policy& np) const override {
        const auto& o = *static_cast<const T*>(obj);
        return rtti::member_to_value_named(dst, o.*ptr, np);
    }

    error_code set_value_named(void* obj, const value_io* src,
                               const rtti::detail::naming_policy& np) const override {
        auto& o = *static_cast<T*>(obj);
        return rtti::from_value_named(src, o.*ptr, np);
    }

    bool is_null_value(const void* obj) const override {
        const auto& o = *static_cast<const T*>(obj);
        return member_is_null(o.*ptr);
    }

    error_code field_add(void* obj, const value_io* element) const override {
        auto& o = *static_cast<T*>(obj);
        if constexpr (is_map<M>::value) {
            string_view key;
            value_io* child = nullptr;
            if (!element->get_key_at(0, key, child) || child == nullptr) {
                return error_code::kind_mismatch;
            }
            typename M::mapped_type v{};
            const error_code e = rtti::from_value(child, v);
            if (e != error_code::ok) {
                return e;
            }
            typename M::key_type k{};
            key_traits<typename M::key_type>::assign(k, key);
            (o.*ptr).emplace(std::move(k), std::move(v));
            return error_code::ok;
        } else if constexpr (is_set<M>::value) {
            typename M::value_type v{};
            const error_code e = rtti::from_value(element, v);
            if (e != error_code::ok) {
                return e;
            }
            (o.*ptr).insert(std::move(v));
            return error_code::ok;
        } else if constexpr (is_sequence<M>::value) {
            typename M::value_type v{};
            const error_code e = rtti::from_value(element, v);
            if (e != error_code::ok) {
                return e;
            }
            (o.*ptr).push_back(std::move(v));
            return error_code::ok;
        } else if constexpr (is_queue<M>::value || is_stack<M>::value ||
                             is_priority_queue<M>::value) {
            typename M::value_type v{};
            const error_code e = rtti::from_value(element, v);
            if (e != error_code::ok) {
                return e;
            }
            (o.*ptr).push(std::move(v));
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    error_code field_add_scalar(void* obj, const field_value& element) const override {
        auto& o = *static_cast<T*>(obj);
        if constexpr (is_map<M>::value) {
            return error_code::kind_mismatch;
        } else if constexpr (is_set<M>::value || is_sequence<M>::value ||
                             is_queue<M>::value || is_stack<M>::value ||
                             is_priority_queue<M>::value) {
            typename M::value_type v{};
            if (!field_value_to(v, element)) {
                return error_code::kind_mismatch;
            }
            if constexpr (is_sequence<M>::value) {
                (o.*ptr).push_back(std::move(v));
            } else if constexpr (is_set<M>::value) {
                (o.*ptr).insert(std::move(v));
            } else {
                (o.*ptr).push(std::move(v));
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    error_code erase_at(void* obj, std::size_t index) const override {
        auto& o = *static_cast<T*>(obj);
        if constexpr (is_sequence<M>::value) {
            if (index >= (o.*ptr).size()) {
                return error_code::no_such_field;
            }
            auto it = (o.*ptr).begin();
            std::advance(it, static_cast<std::ptrdiff_t>(index));
            (o.*ptr).erase(it);
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    error_code erase_key(void* obj, string_view key) const override {
        auto& o = *static_cast<T*>(obj);
        if constexpr (is_map<M>::value) {
            typename M::key_type k{};
            key_traits<typename M::key_type>::assign(k, key);
            const auto it = (o.*ptr).find(k);
            if (it == (o.*ptr).end()) {
                return error_code::no_such_field;
            }
            (o.*ptr).erase(it);
            return error_code::ok;
        } else if constexpr (is_set<M>::value) {
            typename M::value_type v{};
            if constexpr (std::is_same<typename M::value_type, std::string>::value) {
                v.assign(key.data(), key.size());
            } else {
                // Numeric set keys: parse the decimal string form with
                // full-input validation. The whole string must be consumed and
                // stay in range; a malformed or out-of-range key is
                // no_such_field (never a silent strtoll coercion that erases
                // the wrong element, e.g. "foo" -> 0 or "34x" -> 34).
                const std::string ks(key.data(), key.size());
                const char* const b = ks.data();
                const char* const e = ks.data() + ks.size();
                bool parsed = false;
                if constexpr (std::is_unsigned<typename M::value_type>::value) {
                    std::uint64_t u = 0;
                    const auto r = std::from_chars(b, e, u);
                    if (r.ec == std::errc() && r.ptr == e) {
                        const std::uint64_t maxv = static_cast<std::uint64_t>(
                            (std::numeric_limits<typename M::value_type>::max)());
                        if (u <= maxv) {
                            v = static_cast<typename M::value_type>(u);
                            parsed = true;
                        }
                    }
                } else {
                    std::int64_t i = 0;
                    const auto r = std::from_chars(b, e, i);
                    if (r.ec == std::errc() && r.ptr == e) {
                        if (i >= static_cast<std::int64_t>(
                                     (std::numeric_limits<typename M::value_type>::min)()) &&
                            i <= static_cast<std::int64_t>(
                                     (std::numeric_limits<typename M::value_type>::max)())) {
                            v = static_cast<typename M::value_type>(i);
                            parsed = true;
                        }
                    }
                }
                if (!parsed) {
                    return error_code::no_such_field;
                }
            }
            if ((o.*ptr).find(v) == (o.*ptr).end()) {
                return error_code::no_such_field;
            }
            (o.*ptr).erase(v);
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    error_code clear(void* obj) const override {
        auto& o = *static_cast<T*>(obj);
        if constexpr (is_map<M>::value || is_set<M>::value || is_sequence<M>::value) {
            (o.*ptr).clear();
            return error_code::ok;
        } else if constexpr (is_queue<M>::value || is_stack<M>::value ||
                             is_priority_queue<M>::value) {
            while (!(o.*ptr).empty()) {
                (o.*ptr).pop();
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    std::size_t size(void* obj) const override {
        auto& o = *static_cast<T*>(obj);
        if constexpr (is_map<M>::value || is_set<M>::value || is_sequence<M>::value ||
                      is_queue<M>::value || is_stack<M>::value ||
                      is_priority_queue<M>::value) {
            return (o.*ptr).size();
        } else {
            return 0;
        }
    }

    bool is_container() const override {
        return is_map<M>::value || is_set<M>::value || is_sequence<M>::value ||
               is_queue<M>::value || is_stack<M>::value ||
               is_priority_queue<M>::value;
    }

    error_code for_each(void* obj, value_io* scratch,
                        const element_visitor& fn) const override {
        auto& o = *static_cast<T*>(obj);
        std::size_t index = 0;
        if constexpr (is_map<M>::value) {
            for (const auto& kv : o.*ptr) {
                const error_code e = rtti::to_value(scratch, kv.second);
                if (e != error_code::ok) {
                    return e;
                }
                if (!fn(index, key_traits<typename M::key_type>::view(kv.first),
                        scratch)) {
                    break;
                }
                ++index;
            }
            return error_code::ok;
        } else if constexpr (is_set<M>::value || is_sequence<M>::value) {
            for (const auto& item : o.*ptr) {
                const error_code e = rtti::to_value(scratch, item);
                if (e != error_code::ok) {
                    return e;
                }
                if (!fn(index, string_view(), scratch)) {
                    break;
                }
                ++index;
            }
            return error_code::ok;
        } else if constexpr (is_queue<M>::value) {
            M copy = o.*ptr;
            while (!copy.empty()) {
                const error_code e = rtti::to_value(scratch, copy.front());
                if (e != error_code::ok) {
                    return e;
                }
                if (!fn(index, string_view(), scratch)) {
                    break;
                }
                copy.pop();
                ++index;
            }
            return error_code::ok;
        } else if constexpr (is_stack<M>::value || is_priority_queue<M>::value) {
            M copy = o.*ptr;
            while (!copy.empty()) {
                const error_code e = rtti::to_value(scratch, copy.top());
                if (e != error_code::ok) {
                    return e;
                }
                if (!fn(index, string_view(), scratch)) {
                    break;
                }
                copy.pop();
                ++index;
            }
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

private:
    // True when a member value is null-equivalent under skip_empty: an empty
    // optional or a null pointer. Everything else is never "null".
    template <typename U>
    static bool member_is_null(const U& v) noexcept {
        if constexpr (rtti::is_optional<U>::value) {
            return !v.has_value();
        } else if constexpr (std::is_pointer<U>::value) {
            return v == nullptr;
        } else {
            return false;
        }
    }

    // Convert a member value to a field_value. Single template with
    // compile-time dispatch (avoids MSVC ambiguous-overload errors).
    template <typename U>
    static field_value to_field_value(const U& v) noexcept {
        if constexpr (std::is_same<U, bool>::value) {
            return field_value::of(v);
        } else if constexpr (std::is_integral<U>::value) {
            return field_value::of(static_cast<int64_t>(v));
        } else if constexpr (std::is_floating_point<U>::value) {
            return field_value::of(static_cast<double>(v));
        } else if constexpr (std::is_same<U, string_view>::value) {
            return field_value::of(v);
        } else if constexpr (std::is_same<U, std::string>::value) {
            return field_value::of(string_view(v));
        } else {
            // Nested-object / unsupported members are flagged as objects.
            return field_value::of_object();
        }
    }
};

// Intrusive registration: the macro places a nested rtti_registration struct
// inside T; fields_of then picks it up by inheritance (works inside the class
// body where an explicit template specialization is illegal).
template <typename T>
struct fields_of<T, std::void_t<typename T::rtti_registration>>
    : T::rtti_registration {};

// Compile-time number of reflected fields. Hard error for unregistered T:
// "incomplete type 'fields_of<T>'".
template <typename T>
constexpr std::size_t field_count() noexcept {
    return fields_of<T>::count;
}

// Runtime-iterable array of field accessors for T.
// The array decays to a pointer to its first element (a field_access_base*).
template <typename T>
const field_access_base* const* fields() noexcept {
    return fields_of<T>::array;
}

// True if the type was registered with a _WITH_DEFAULT macro, meaning missing
// keys keep the member's existing value (used by format fillers).
template <typename T, typename = void>
struct has_default : std::false_type {};

template <typename T>
struct has_default<T, std::void_t<typename T::rtti_has_default>>
    : std::true_type {};

// ---------------------------------------------------------------------------
// Attribute / annotation system (spec section 17). Attributes attach extra
// metadata to a reflected type or to individual fields and are queried at
// runtime like C# attributes. A type participates by specializing
// rtti::attr_table<T> (type level) and/or rtti::field_attr_table<T>
// (per-field), which the RTTI_DEFINE_TYPE_ATTRS / RTTI_DEFINE_FIELD_ATTRS
// macros generate at namespace scope. Attribute declarations are optional: an
// unregistered type gets a type-safe empty table (count 0, null accesses).
// ---------------------------------------------------------------------------

// One attribute argument: a small tag-union of the four supported payload
// kinds. String payloads are views (rtti::string_view) and must outlive the
// table; attribute declarations normally pass string literals (static
// storage), so no ownership or copying is required.
class attr_arg {
public:
    enum class kind { none, integer, real, string, boolean };

    attr_arg() noexcept : k_(kind::none) {}

    static attr_arg integer(std::uint64_t v) noexcept {
        attr_arg a;
        a.k_ = kind::integer;
        a.i_ = v;
        return a;
    }
    static attr_arg real(double v) noexcept {
        attr_arg a;
        a.k_ = kind::real;
        a.d_ = v;
        return a;
    }
    static attr_arg text(string_view v) noexcept {
        attr_arg a;
        a.k_ = kind::string;
        a.s_ = v;
        return a;
    }
    static attr_arg boolean(bool v) noexcept {
        attr_arg a;
        a.k_ = kind::boolean;
        a.b_ = v;
        return a;
    }

    kind type() const noexcept { return k_; }
    bool is_none() const noexcept { return k_ == kind::none; }

    // Accessors; each returns a zero/empty fallback on kind mismatch.
    std::uint64_t as_integer() const noexcept {
        return k_ == kind::integer ? i_ : 0;
    }
    double as_real() const noexcept { return k_ == kind::real ? d_ : 0.0; }
    string_view as_string() const noexcept {
        return k_ == kind::string ? s_ : string_view();
    }
    bool as_bool() const noexcept { return k_ == kind::boolean ? b_ : false; }

private:
    kind k_;
    std::uint64_t i_ = 0;
    double d_ = 0.0;
    string_view s_;
    bool b_ = false;
};

// One attribute: a name plus an ordered argument list.
struct attr {
    const char* name = nullptr;
    std::vector<attr_arg> args;
};

// One reflected field's attribute list as registered by RTTI_DEFINE_FIELD_ATTRS.
struct field_attr_entry {
    const char* field = nullptr;
    std::vector<attr> attrs;
};

// Result view returned by field_attr<T>.
struct field_attr_list {
    std::size_t count = 0;
    const attr* items = nullptr;
};

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

// True when the type registered a (possibly empty) attribute table.
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

namespace detail {

// Converts one attribute-argument expression to the matching attr_arg kind:
// integral -> integer, floating -> real, bool -> boolean, string-ish (string
// literal / const char* / string_view / std::string) -> string. Unhandled
// argument types are rejected at compile time.
template <typename T>
attr_arg make_attr_arg(const T& v) noexcept {
    using U = std::decay_t<T>;
    if constexpr (std::is_same<U, bool>::value) {
        return attr_arg::boolean(v);
    } else if constexpr (std::is_integral<U>::value) {
        using S =
            std::conditional_t<std::is_signed<U>::value, std::intmax_t,
                               std::uintmax_t>;
        return attr_arg::integer(static_cast<std::uint64_t>(static_cast<S>(v)));
    } else if constexpr (std::is_floating_point<U>::value) {
        return attr_arg::real(static_cast<double>(v));
    } else if constexpr (std::is_same<U, const char*>::value ||
                         std::is_same<U, char*>::value) {
        return attr_arg::text(v);
    } else if constexpr (std::is_same<U, string_view>::value) {
        return attr_arg::text(v);
    } else if constexpr (std::is_same<U, std::string>::value) {
        return attr_arg::text(string_view(v));
    } else {
        static_assert(!std::is_same<U, U>::value,
                      "rtti::make_attr_arg: unsupported attribute argument type");
        return attr_arg{};
    }
}

} // namespace detail

// Runtime factories. The variadic overloads are the public construction API;
// the initializer-list overloads are what the RTTI_ATTR / RTTI_FIELD_ATTR_ENTRY
// macros expand to (a braced list is immune to preprocessor comma splitting).
template <typename... Args>
inline attr make_attr(const char* name, const Args&... args) {
    attr a;
    a.name = name;
    a.args.reserve(sizeof...(Args));
    (a.args.push_back(detail::make_attr_arg(args)), ...);
    return a;
}

inline attr make_attr(const char* name, std::initializer_list<attr_arg> args) {
    attr a;
    a.name = name;
    a.args.assign(args.begin(), args.end());
    return a;
}

template <typename... A>
inline field_attr_entry make_field_attr(const char* field, const A&... attrs) {
    field_attr_entry e;
    e.field = field;
    e.attrs.reserve(sizeof...(A));
    (e.attrs.push_back(attrs), ...);
    return e;
}

inline field_attr_entry make_field_attr(const char* field,
                                        std::initializer_list<attr> attrs) {
    field_attr_entry e;
    e.field = field;
    e.attrs.assign(attrs.begin(), attrs.end());
    return e;
}

// --- Type-level attribute queries ------------------------------------------

template <typename T>
std::size_t attr_count() noexcept {
    return attr_table<T>::count;
}

template <typename T>
const attr* attr_at(std::size_t index) noexcept {
    if (index >= attr_table<T>::count) {
        return nullptr;
    }
    return &attr_table<T>::array[index];
}

template <typename T>
const attr* find_attr(string_view name) noexcept {
    for (std::size_t i = 0; i < attr_table<T>::count; ++i) {
        if (string_view(attr_table<T>::array[i].name) == name) {
            return &attr_table<T>::array[i];
        }
    }
    return nullptr;
}

template <typename T>
bool has_attr(string_view name) noexcept {
    return find_attr<T>(name) != nullptr;
}

// --- Field-level attribute queries ------------------------------------------

template <typename T>
field_attr_list field_attr(string_view field_name) noexcept {
    for (std::size_t f = 0; f < field_attr_table<T>::count; ++f) {
        const field_attr_entry& e = field_attr_table<T>::array[f];
        if (string_view(e.field) == field_name) {
            field_attr_list r;
            r.count = e.attrs.size();
            r.items = e.attrs.empty() ? nullptr : e.attrs.data();
            return r;
        }
    }
    return field_attr_list{};
}

template <typename T>
field_attr_list field_attr(std::size_t field_index) noexcept {
    if (field_index >= rtti::field_count<T>()) {
        return field_attr_list{};
    }
    return field_attr<T>(string_view(rtti::fields<T>()[field_index]->name));
}

template <typename T>
std::size_t field_attr_count(string_view field_name) noexcept {
    return field_attr<T>(field_name).count;
}

template <typename T>
std::size_t field_attr_count(std::size_t field_index) noexcept {
    return field_attr<T>(field_index).count;
}

template <typename T>
const attr* field_attr_at(string_view field_name, std::size_t index) noexcept {
    const field_attr_list l = field_attr<T>(field_name);
    if (index >= l.count) {
        return nullptr;
    }
    return &l.items[index];
}

template <typename T>
const attr* field_attr_at(std::size_t field_index, std::size_t index) noexcept {
    const field_attr_list l = field_attr<T>(field_index);
    if (index >= l.count) {
        return nullptr;
    }
    return &l.items[index];
}

template <typename T>
const attr* field_find_attr(string_view field_name, string_view name) noexcept {
    const field_attr_list l = field_attr<T>(field_name);
    for (std::size_t i = 0; i < l.count; ++i) {
        if (string_view(l.items[i].name) == name) {
            return &l.items[i];
        }
    }
    return nullptr;
}

template <typename T>
const attr* field_find_attr(std::size_t field_index, string_view name) noexcept {
    const field_attr_list l = field_attr<T>(field_index);
    for (std::size_t i = 0; i < l.count; ++i) {
        if (string_view(l.items[i].name) == name) {
            return &l.items[i];
        }
    }
    return nullptr;
}

template <typename T>
bool field_has_attr(string_view field_name, string_view name) noexcept {
    return field_find_attr<T>(field_name, name) != nullptr;
}

template <typename T>
bool field_has_attr(std::size_t field_index, string_view name) noexcept {
    return field_find_attr<T>(field_index, name) != nullptr;
}

} // namespace rtti

// ---------------------------------------------------------------------------
// Macro infrastructure: bounded argument counting + flat iteration.
// Iteration is pre-expanded and non-recursive; each RTTI_APPLY_N expands M
// once per argument. The ceiling exists only to keep macros finite; the
// field-count cap is not a design limit for reflected types.
// ---------------------------------------------------------------------------

#define RTTI_CAT_IMPL(a, b) a##b
#define RTTI_CAT(a, b) RTTI_CAT_IMPL(a, b)

#define RTTI_EXPAND(...) __VA_ARGS__

// --- Empty-argument detection (Boost PP is_begin_parens idiom) ------------
// Detects whether the variadic list is empty. Required because MSVC's NARG
// cannot distinguish `()` from `(x)`. Works with the conformant preprocessor
// (/Zc:preprocessor); the reflect core always compiles under it.

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

// Argument count (0..16). The single-level form (RTTI_NARG -> RTTI_NARG_I,
// no intermediate variadic macro) is required for MSVC's conformant
// preprocessor (/Zc:preprocessor). Empty lists currently yield 1; the empty
// RTTI_DEFINE_TYPE case (zero fields) is handled in Task 9 separately.
#define RTTI_NARG_I(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, \
                    _14, _15, _16, N, ...)                                   \
    N
#define RTTI_NARG(...)                                                        \
    RTTI_NARG_I(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define RTTI_APPLY_0(M, T)
#define RTTI_APPLY_1(M, T, a1) M(T, a1)
#define RTTI_APPLY_2(M, T, a1, a2) M(T, a1) M(T, a2)
#define RTTI_APPLY_3(M, T, a1, a2, a3) M(T, a1) M(T, a2) M(T, a3)
#define RTTI_APPLY_4(M, T, a1, a2, a3, a4) M(T, a1) M(T, a2) M(T, a3) M(T, a4)
#define RTTI_APPLY_5(M, T, a1, a2, a3, a4, a5) \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5)
#define RTTI_APPLY_6(M, T, a1, a2, a3, a4, a5, a6)                            \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6)
#define RTTI_APPLY_7(M, T, a1, a2, a3, a4, a5, a6, a7)                        \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7)
#define RTTI_APPLY_8(M, T, a1, a2, a3, a4, a5, a6, a7, a8)                    \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)
#define RTTI_APPLY_9(M, T, a1, a2, a3, a4, a5, a6, a7, a8, a9)                \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)  \
        M(T, a9)
#define RTTI_APPLY_10(M, T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)          \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)  \
        M(T, a9) M(T, a10)
#define RTTI_APPLY_11(M, T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11)     \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)  \
        M(T, a9) M(T, a10) M(T, a11)
#define RTTI_APPLY_12(M, T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12)\
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)  \
        M(T, a9) M(T, a10) M(T, a11) M(T, a12)
#define RTTI_APPLY_13(M, T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,\
                      a13)                                                     \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)  \
        M(T, a9) M(T, a10) M(T, a11) M(T, a12) M(T, a13)
#define RTTI_APPLY_14(M, T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,\
                      a13, a14)                                                \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)  \
        M(T, a9) M(T, a10) M(T, a11) M(T, a12) M(T, a13) M(T, a14)
#define RTTI_APPLY_15(M, T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,\
                      a13, a14, a15)                                           \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)  \
        M(T, a9) M(T, a10) M(T, a11) M(T, a12) M(T, a13) M(T, a14) M(T, a15)
#define RTTI_APPLY_16(M, T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,\
                      a13, a14, a15, a16)                                      \
    M(T, a1) M(T, a2) M(T, a3) M(T, a4) M(T, a5) M(T, a6) M(T, a7) M(T, a8)  \
        M(T, a9) M(T, a10) M(T, a11) M(T, a12) M(T, a13) M(T, a14) M(T, a15) \
        M(T, a16)

#define RTTI_APPLY(M, T, ...) \
    RTTI_EXPAND(RTTI_CAT(RTTI_APPLY_, RTTI_NARG(__VA_ARGS__))(M, T, __VA_ARGS__))

// Apply M over the field list, expanding to nothing when the list is empty.
#define RTTI_APPLY_OPT_SEL_1(M, T, ...)
#define RTTI_APPLY_OPT_SEL_0(M, T, ...) RTTI_APPLY(M, T, __VA_ARGS__)
#define RTTI_APPLY_OPT(M, T, ...) \
    RTTI_CAT2(RTTI_APPLY_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(M, T, __VA_ARGS__)

// <rtti/memory.hpp> provides string_view; enum support is self-contained.
namespace rtti {

template <typename E>
struct enum_label {
    const char* name;
    E value;
};

// Enum label table. A type participates in enum reflection iff this template
// is specialized (by RTTI_DEFINE_ENUM).
template <typename E>
struct enum_table;

template <typename E>
const char* enum_to_string(E value) noexcept {
    for (std::size_t i = 0; i < enum_table<E>::count; ++i) {
        if (enum_table<E>::array[i].value == value) {
            return enum_table<E>::array[i].name;
        }
    }
    return "";
}

template <typename E>
E enum_from_string(string_view name) noexcept {
    for (std::size_t i = 0; i < enum_table<E>::count; ++i) {
        if (string_view(enum_table<E>::array[i].name) == name) {
            return enum_table<E>::array[i].value;
        }
    }
    return static_cast<E>(0);
}

// ---------------------------------------------------------------------------
// Runtime type description system. rtti::type_of<T>() returns a stable,
// lazily-created type_desc describing the category of T: scalar, string,
// one of the container families (including queue/stack/priority_queue), a
// reflected struct (with its field table), or an enum. Nested element/key
// and field types are resolved on demand through resolver function pointers,
// so self-referential structures cannot recurse forever at construction time.
// ---------------------------------------------------------------------------

enum class type_kind { boolean, integer, real, string, container, reflected, enum_type };

enum class container_family {
    vector, list, map, unordered_map, set, unordered_set,
    queue, stack, priority_queue
};

struct type_desc;
struct container_desc;
struct struct_desc;
struct enum_desc;

using type_resolver = const type_desc* (*)();

// One field of a reflected struct: name plus an on-demand type resolver.
struct field_type_desc {
    const char* name;
    type_resolver resolve;

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
// into a pointer to the base subobject. Up-casts (including to a virtual base)
// are always legal via static_cast and produce the correct offset, so the
// specialization below always matches. The primary base_adjuster is a
// defensive fallback that only matches when static_cast is ill-formed (e.g. an
// inaccessible or ambiguous base); it returns nullptr so the caller fails
// cleanly rather than aliasing the wrong memory.
using base_adjust_fn = void* (*)(void*);

// Direct base types of a reflected type (spec 19): an array of lazy
// resolvers, one per registered base, in declaration order. The type_id chain
// for runtime is_derived_from / dynamic_cast_ref walks these resolvers.
struct base_type_list {
    std::size_t count;
    const type_resolver* resolve;   // array of `count` lazy resolvers
    base_adjust_fn const* adjust;   // array of `count` adjusters (parallel)
};

struct enum_desc {
    std::size_t count;
    const char* const* names;
};

struct type_desc {
    type_kind kind;
    const std::type_info* id;   // typeid(T)
    const char* type_name;      // typeid(T).name()
    const container_desc* as_container;
    const struct_desc* as_reflected;
    const enum_desc* as_enum;
    const base_type_list* as_bases = nullptr;  // direct bases (spec 19)

    // Compile-time abstract / polymorphic classification of T (spec 20.2),
    // captured from std::is_abstract / std::is_polymorphic when the descriptor
    // is built by type_of<T>. The defaults keep descriptors safe (false) for
    // scalar / string / container / enum categories, none of which can be
    // abstract or polymorphic.
    bool abstract = false;
    bool polymorphic = false;

    // True when T is an abstract class (has pure virtual functions) and thus
    // cannot be instantiated.
    bool is_abstract() const noexcept { return abstract; }

    // True when T is polymorphic (has at least one virtual function, i.e. a
    // vtable); a prerequisite for the runtime dynamic checks (spec 19/20).
    bool is_polymorphic() const noexcept { return polymorphic; }

    // The direct base types of this type; nullptr when none are registered.
    const base_type_list* bases() const noexcept { return as_bases; }
};

// ---------------------------------------------------------------------------
// Compile-time abstract / polymorphic / virtual-destructor judgment (spec 20).
// Thin rtti-namespace aliases over the standard traits so callers stay inside
// one namespace, mirroring is_same / is_base_of / is_convertible in reflect.hpp.
// ---------------------------------------------------------------------------

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

// --- Compile-time family discrimination -------------------------------------

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

// Map: unordered (has a hasher) vs ordered; Set likewise.
template <typename T>
constexpr container_family map_family() noexcept {
    return has_hasher<T>::value ? container_family::unordered_map : container_family::map;
}
template <typename T>
constexpr container_family set_family() noexcept {
    return has_hasher<T>::value ? container_family::unordered_set : container_family::set;
}
// Sequence: vector has data(); deque has operator[] but no data(); list neither.
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

// --- Enum helpers ------------------------------------------------------------

// Builds a null-terminated name array for a registered enum.
template <typename E, std::size_t... I>
const char* const* enum_name_array(std::index_sequence<I...>) {
    static const char* const names[] = { enum_table<E>::array[I].name... };
    return names;
}
template <typename E>
const char* const* enum_names() {
    return enum_name_array<E>(std::make_index_sequence<enum_table<E>::count>());
}

// --- type_of ----------------------------------------------------------------

namespace detail {
template <typename T>
const type_desc* type_of_impl() noexcept;
}

namespace detail {

// Base-pointer adjuster. Up-casts (including to a virtual base) are always
// legal via static_cast and offset-correct ([conv.ptr]/3), so the
// specialization below always matches for a registered base. The primary
// template is a defensive fallback that only matches when
// static_cast<Base*>(Derived*) is ill-formed (e.g. an inaccessible or
// ambiguous base); it returns nullptr so the caller fails safely. A virtual
// base up-cast never reaches the primary template.
template <typename Derived, typename Base, typename = void>
struct base_adjuster {
    static void* run(void*) noexcept {
        return nullptr;  // no legal static_cast adjustment: fail safely
    }
};
template <typename Derived, typename Base>
struct base_adjuster<Derived, Base,
    std::void_t<decltype(static_cast<Base*>(std::declval<Derived*>()))>> {
    static void* run(void* p) noexcept {
        return static_cast<Base*>(static_cast<Derived*>(p));
    }
};

// Build the lazy base-type resolver list for a reflected type. A type with no
// registered bases reports nullptr (no as_bases chain).
template <typename Derived, typename Bs, std::size_t... I>
const base_type_list* base_list_impl(std::index_sequence<I...>) noexcept {
    static const type_resolver resolvers[sizeof...(I) > 0 ? sizeof...(I) : 1] = {
        &detail::type_of_impl<std::tuple_element_t<I, Bs>>...,
    };
    static const base_adjust_fn adjusters[sizeof...(I) > 0 ? sizeof...(I) : 1] = {
        &base_adjuster<Derived, std::tuple_element_t<I, Bs>>::run...,
    };
    static const base_type_list list{sizeof...(I), resolvers, adjusters};
    return &list;
}

template <typename T>
const base_type_list* base_type_list_of() noexcept {
    using Bs = typename base_types<T>::list;
    if constexpr (std::tuple_size<Bs>::value == 0) {
        return nullptr;
    } else {
        return base_list_impl<T, Bs>(std::make_index_sequence<std::tuple_size<Bs>::value>{});
    }
}

// Process-wide registry of reflected type descriptors, keyed by std::type_info
// value. Populated lazily the first time type_of<T>() materializes a reflected
// descriptor; used by the type-erased instance cast to resolve a runtime
// dynamic type to its descriptor (base chain + adjusters). Thread-safe.
//
// Declared before type_of_impl below so the reflected branch can register its
// descriptor at definition-time lookup (spec section: type-erased instance
// path, full-precision).
inline std::vector<const type_desc*>& desc_registry() {
    static std::vector<const type_desc*> v;
    return v;
}
inline std::mutex& desc_registry_mutex() {
    static std::mutex m;
    return m;
}
inline void register_desc(const type_desc* d) noexcept {
    if (d == nullptr || d->id == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lk(desc_registry_mutex());
    for (const type_desc* e : desc_registry()) {
        if (e != nullptr && e->id != nullptr && *e->id == *d->id) {
            return;
        }
    }
    desc_registry().push_back(d);
}
inline const type_desc* find_desc(const std::type_info& ti) noexcept {
    std::lock_guard<std::mutex> lk(desc_registry_mutex());
    for (const type_desc* e : desc_registry()) {
        if (e != nullptr && e->id != nullptr && *e->id == ti) {
            return e;
        }
    }
    return nullptr;
}

} // namespace detail

template <typename T>
const type_desc* type_of() noexcept {
    return detail::type_of_impl<T>();
}

// Value overload: rtti::type_of(obj).
template <typename T>
const type_desc* type_of(const T&) noexcept {
    return type_of<T>();
}

namespace detail {

template <typename T>
const type_desc* type_of_impl() noexcept {
    if constexpr (std::is_same<T, bool>::value) {
        static const type_desc d{type_kind::boolean, &typeid(T), typeid(T).name()};
        return &d;
    } else if constexpr (std::is_integral<T>::value) {
        static const type_desc d{type_kind::integer, &typeid(T), typeid(T).name()};
        return &d;
    } else if constexpr (std::is_floating_point<T>::value) {
        static const type_desc d{type_kind::real, &typeid(T), typeid(T).name()};
        return &d;
    } else if constexpr (std::is_same<T, std::string>::value ||
                         std::is_same<T, string_view>::value) {
        static const type_desc d{type_kind::string, &typeid(T), typeid(T).name()};
        return &d;
    } else if constexpr (is_map<T>::value) {
        static const container_desc c{
            map_family<T>(),
            []() -> const type_desc* { return type_of<typename T::key_type>(); },
            []() -> const type_desc* { return type_of<typename T::mapped_type>(); }};
        static const type_desc d{type_kind::container, &typeid(T), typeid(T).name(), &c};
        return &d;
    } else if constexpr (is_set<T>::value) {
        static const container_desc c{
            set_family<T>(),
            nullptr,
            []() -> const type_desc* { return type_of<typename T::value_type>(); }};
        static const type_desc d{type_kind::container, &typeid(T), typeid(T).name(), &c};
        return &d;
    } else if constexpr (is_sequence<T>::value) {
        static const container_desc c{
            sequence_family<T>(),
            nullptr,
            []() -> const type_desc* { return type_of<typename T::value_type>(); }};
        static const type_desc d{type_kind::container, &typeid(T), typeid(T).name(), &c};
        return &d;
    } else if constexpr (is_priority_queue<T>::value) {
        static const container_desc c{
            container_family::priority_queue,
            nullptr,
            []() -> const type_desc* { return type_of<typename T::value_type>(); }};
        static const type_desc d{type_kind::container, &typeid(T), typeid(T).name(), &c};
        return &d;
    } else if constexpr (is_queue<T>::value) {
        static const container_desc c{
            container_family::queue,
            nullptr,
            []() -> const type_desc* { return type_of<typename T::value_type>(); }};
        static const type_desc d{type_kind::container, &typeid(T), typeid(T).name(), &c};
        return &d;
    } else if constexpr (is_stack<T>::value) {
        static const container_desc c{
            container_family::stack,
            nullptr,
            []() -> const type_desc* { return type_of<typename T::value_type>(); }};
        static const type_desc d{type_kind::container, &typeid(T), typeid(T).name(), &c};
        return &d;
    } else if constexpr (is_reflected<T>::value) {
        static const struct_desc s{field_count<T>(), fields_of<T>::desc};
        static const type_desc d{type_kind::reflected, &typeid(T), typeid(T).name(),
                                 nullptr, &s, nullptr,
                                 detail::base_type_list_of<T>(),
                                 std::is_abstract<T>::value,
                                 std::is_polymorphic<T>::value};
        // Register the reflected descriptor once, on first materialization, so
        // the type-erased instance cast can resolve a runtime dynamic type to
        // its base chain + offset-correct adjusters (spec type-erased path).
        static const bool registered = (detail::register_desc(&d), true);
        (void)registered;
        return &d;
    } else if constexpr (std::is_enum<T>::value) {
        static const enum_desc e{enum_table<T>::count, enum_names<T>()};
        static const type_desc d{type_kind::enum_type, &typeid(T), typeid(T).name(), nullptr, nullptr, &e};
        return &d;
    } else {
        // Unknown/unregistered type: report it as reflected of zero fields is
        // wrong; use the container kind only as a safe placeholder. The class
        // flags are still captured so polymorphic / abstract types report
        // truthfully regardless of registration.
        static const type_desc d{type_kind::string, &typeid(T), typeid(T).name(),
                                 nullptr, nullptr, nullptr, nullptr,
                                 std::is_abstract<T>::value,
                                 std::is_polymorphic<T>::value};
        return &d;
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// Template-argument reflection (spec 20).
//
// C++17 cannot introspect the type arguments of an arbitrary template
// instantiation, so the argument list is registered explicitly: specializing
// rtti::template_info<T> (via the RTTI_TEMPLATE_INFO macro) stores the
// argument types as a std::tuple plus their count. Unregistered template
// instances (and non-template types) fall back to the primary template's safe
// empty table: count 0, empty tuple, nullptr array. The runtime entry points
// (template_args_of / template_arg_desc_of) convert each argument type to its
// type_desc via type_of so callers can compare against targets (type_judge
// integration, spec 20.2).
// ---------------------------------------------------------------------------

template <typename T, typename = void>
struct template_info {
    static constexpr std::size_t count = 0;
    using args = std::tuple<>;
};

// True when T is a template instance whose arguments were registered with
// RTTI_TEMPLATE_INFO. Detection is marker-based (a `registered` typedef) so the
// safe empty primary template does not count as registered.
template <typename T, typename = void>
struct is_template_instance : std::false_type {};
template <typename T>
struct is_template_instance<T, std::void_t<typename template_info<T>::registered>>
    : std::true_type {};
template <typename T>
inline constexpr bool is_template_instance_v = is_template_instance<T>::value;

// The number of registered template arguments of T (0 for non-templates).
template <typename T>
constexpr std::size_t template_arg_count() noexcept {
    return template_info<T>::count;
}

// The template argument types of T as a std::tuple (empty for unregistered).
template <typename T>
using template_args = typename template_info<T>::args;

namespace detail {

// Build the lazily-resolved type_desc array for the template arguments of T.
// Instantiated only when the registration is non-empty (count > 0), so the
// static array always receives a non-empty pack.
template <typename T, std::size_t... I>
const type_desc* const* template_args_array_impl(std::index_sequence<I...>) noexcept {
    static const type_desc* const refs[] = {
        rtti::type_of<std::tuple_element_t<I, template_args<T>>>()...,
    };
    return refs;
}

} // namespace detail

// The I-th template argument type of T. Out-of-range / unregistered indices
// are rejected at compile time with a clear message (spec 21.4).
template <typename T, std::size_t I>
struct template_arg_at_holder {
    static_assert(I < template_info<T>::count,
                  "RTTI: template argument index out of range");
    using type = std::tuple_element_t<I, template_args<T>>;
};
template <typename T, std::size_t I>
using template_arg_at = typename template_arg_at_holder<T, I>::type;

// Runtime array of type_desc pointers, one per registered template argument, in
// declaration order. Returns nullptr (safe empty table) for unregistered
// templates; callers combine with template_arg_count<T>().
template <typename T>
const type_desc* const* template_args_of() noexcept {
    if constexpr (template_info<T>::count == 0) {
        return nullptr;
    } else {
        return detail::template_args_array_impl<T>(
            std::make_index_sequence<template_info<T>::count>{});
    }
}

// Safe runtime per-index accessor: the type_desc of the I-th template argument,
// or nullptr when T is unregistered / the index is out of range (spec 0).
template <typename T>
const type_desc* template_arg_desc_of(std::size_t index) noexcept {
    if (index >= template_info<T>::count) {
        return nullptr;
    }
    return template_args_of<T>()[index];
}

// ---------------------------------------------------------------------------
// Derived-type merge machinery (spec 19). The macro family emits, for every
// registered type, a `members` tuple holding the typed member pointers of the
// type's OWN reflected fields (in declaration order), next to the base_types<T>
// list. A derived type registered with RTTI_DEFINE_TYPE_DERIVED merges each
// base's fields into its table through D-anchored accessors: the base member
// pointer is converted to a member pointer of the derived type
// (M Base::* -> M Derived::*), so member-pointer dispatch performs the correct
// subobject offset for virtual and non-virtual bases alike. The merged
// accessor/descriptor arrays are built once into function-local statics; the
// base tables feeding them are constant-initialized, so the build is safe
// under any static-initialization order.
// ---------------------------------------------------------------------------

namespace detail {

// Extract the value/owner components of a pointer-to-data-member type.
template <typename>
struct member_pointer_traits;

template <typename M, typename C>
struct member_pointer_traits<M C::*> {
    using value = M;
    using owner = C;
};

// Sum of fields_of<B>::count over the base pack (each base's count is already
// recursively merged, so this is the base-portion size for a derived table).
// Struct recursion with a partial-specialization terminal; MSVC's recursive
// constexpr function overloads fail to constant-evaluate this pattern.
template <typename... B>
struct base_total_impl;

template <typename B0, typename... Bs>
struct base_total_impl<B0, Bs...> {
    static constexpr std::size_t value =
        fields_of<B0>::count + base_total_impl<Bs...>::value;
};

template <>
struct base_total_impl<> {
    static constexpr std::size_t value = 0;
};

template <typename Bs, std::size_t... I>
constexpr std::size_t base_total_seq(std::index_sequence<I...>) noexcept {
    return base_total_impl<std::tuple_element_t<I, Bs>...>::value;
}

template <typename Bs>
constexpr std::size_t base_total() noexcept {
    return base_total_seq<Bs>(std::make_index_sequence<std::tuple_size<Bs>::value>{});
}

template <typename T>
using base_list_t = typename base_types<T>::list;

// Base-contributed field count of a type (fields merged from its direct
// bases), and its own-field count (the macro's field list).
template <typename T>
constexpr std::size_t base_field_count() noexcept {
    return base_total<base_list_t<T>>();
}
template <typename T>
constexpr std::size_t own_member_count() noexcept {
    return std::tuple_size<typename fields_of<T>::members_type>::value;
}

// Invoke `fn` with each type in the base tuple Bs.
template <typename Bs, typename Fn, std::size_t... I>
void for_each_base_impl(Fn&& fn, std::index_sequence<I...>) {
    (fn(std::tuple_element_t<I, Bs>{}), ...);
}
template <typename Bs, typename Fn>
void for_each_base(Fn&& fn) {
    if constexpr (std::tuple_size<Bs>::value > 0) {
        for_each_base_impl<Bs>(std::forward<Fn>(fn),
                               std::make_index_sequence<std::tuple_size<Bs>::value>{});
    }
}

// The D-anchored accessor for the OwnIndex-th OWN field of the base type C:
// a typed_field_access<D, M> over the converted member pointer. One static
// instance per (D, C, OwnIndex), so fields sharing a member type stay
// distinct.
template <typename D, typename C, std::size_t OwnIndex>
const field_access_base* derived_accessor() noexcept {
    constexpr std::size_t off = base_field_count<C>();
    using Mp = std::tuple_element_t<OwnIndex, typename fields_of<C>::members_type>;
    using M = typename member_pointer_traits<Mp>::value;
    static_assert(std::is_base_of<C, D>::value,
                  "rtti: a reflected derived type must derive from its base");
    static const typed_field_access<D, M> inst{
        fields_of<C>::desc[off + OwnIndex].name,
        static_cast<M D::*>(std::get<OwnIndex>(fields_of<C>::members)),
        &typeid(M)};
    return &inst;
}

template <typename D, typename C, std::size_t... I>
void append_own_fields_impl(std::vector<const field_access_base*>& out,
                            std::index_sequence<I...>) {
    ((out.push_back(derived_accessor<D, C, I>())), ...);
}

// Append D-anchored accessors for C's OWN fields. C's own fields sit after
// its (recursively merged) base fields in desc, at [base_field_count, count).
template <typename D, typename C>
void append_own_fields(std::vector<const field_access_base*>& out) {
    constexpr std::size_t own = own_member_count<C>();
    if constexpr (own > 0) {
        append_own_fields_impl<D, C>(out, std::make_index_sequence<own>{});
    }
}

// Append copies of C's OWN descriptor entries (names + lazy resolvers; the
// field type is independent of the owning type).
template <typename C>
void append_own_desc(std::vector<field_type_desc>& out) {
    constexpr std::size_t own = own_member_count<C>();
    if constexpr (own > 0) {
        constexpr std::size_t off = base_field_count<C>();
        for (std::size_t i = 0; i < own; ++i) {
            out.push_back(fields_of<C>::desc[off + i]);
        }
    }
}

// Append D-anchored accessors / descriptor copies for EVERY field of base
// type C: first C's own bases (recursively), then C's own fields. This keeps
// the merged ordering identical to fields_of<C>'s own table, base-first.
template <typename D, typename C>
struct derived_field_appender {
    static void run(std::vector<const field_access_base*>& out) {
        for_each_base<base_list_t<C>>([&](auto Bt) {
            using B = decltype(Bt);
            derived_field_appender<D, B>::run(out);
        });
        append_own_fields<D, C>(out);
    }
};

template <typename D, typename C>
struct derived_desc_appender {
    static void run(std::vector<field_type_desc>& out) {
        for_each_base<base_list_t<C>>([&](auto Bt) {
            using B = decltype(Bt);
            derived_desc_appender<D, B>::run(out);
        });
        append_own_desc<C>(out);
    }
};

// Once-built merged tables of a derived type D. The vectors are function-local
// statics (magic statics: one instance, thread-safe, destroyed at exit), and
// the returned pointers are the merged tables with base fields first.
template <typename D>
struct derived_pool {
    static const field_access_base* const* accessors() noexcept {
        const std::vector<const field_access_base*>& v = accessor_vec();
        return const_cast<const field_access_base* const*>(v.data());
    }
    static const field_type_desc* descriptors() noexcept {
        const std::vector<field_type_desc>& v = desc_vec();
        return v.data();
    }

private:
    static std::vector<const field_access_base*> build_accessors() {
        std::vector<const field_access_base*> out;
        out.reserve(fields_of<D>::count);
        for_each_base<base_list_t<D>>([&](auto Bt) {
            using B = decltype(Bt);
            derived_field_appender<D, B>::run(out);
        });
        constexpr std::size_t own = own_member_count<D>();
        for (std::size_t i = 0; i < own; ++i) {
            out.push_back(fields_of<D>::own_table[i]);
        }
        return out;
    }
    static std::vector<field_type_desc> build_desc() {
        std::vector<field_type_desc> out;
        out.reserve(fields_of<D>::count);
        for_each_base<base_list_t<D>>([&](auto Bt) {
            using B = decltype(Bt);
            derived_desc_appender<D, B>::run(out);
        });
        constexpr std::size_t own = own_member_count<D>();
        for (std::size_t i = 0; i < own; ++i) {
            out.push_back(fields_of<D>::own_desc[i]);
        }
        return out;
    }
    static const std::vector<const field_access_base*>& accessor_vec() noexcept {
        static const std::vector<const field_access_base*> v = build_accessors();
        return v;
    }
    static const std::vector<field_type_desc>& desc_vec() noexcept {
        static const std::vector<field_type_desc> v = build_desc();
        return v;
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// Reflective instantiation + instance handles (spec section 18).
// A type opts into reflective construction by specializing rtti::ctor_registry<T>
// (usually via the RTTI_DEFINE_CTOR macro, mirroring the explicit proto schema
// specialization style). make_object<T> / create_instance<T> live in
// <rtti/reflect.hpp>; this header supplies the parameter descriptors, the
// runtime argument holder, the factory glue and the instance handle.
// ---------------------------------------------------------------------------

// One runtime constructor argument (spec 18.3). A value comes from a scalar
// field_value, a value_io node (nested/container/scalar), or a parameter name
// matching a registered ctor parameter name. Arguments are matched to
// parameters by name when any argument carries a name, otherwise by position;
// named arguments fill their parameter and the remaining parameters receive
// the unnamed ("positional") arguments in declaration order (mixed mode).
class ctor_arg {
public:
    ctor_arg() noexcept = default;

    static ctor_arg scalar(const field_value& v) noexcept {
        ctor_arg a;
        a.scalar_ = v;
        return a;
    }
    static ctor_arg scalar(const char* name, const field_value& v) noexcept {
        ctor_arg a;
        a.name_ = name;
        a.scalar_ = v;
        return a;
    }
    static ctor_arg node(const value_io* v) noexcept {
        ctor_arg a;
        a.node_ = v;
        return a;
    }
    static ctor_arg node(const char* name, const value_io* v) noexcept {
        ctor_arg a;
        a.name_ = name;
        a.node_ = v;
        return a;
    }

    const char* name() const noexcept { return name_; }
    bool named() const noexcept { return name_ != nullptr; }
    // A node argument carries a value_io source (nested/container/scalar).
    bool is_node() const noexcept { return node_ != nullptr; }
    bool is_scalar() const noexcept { return node_ == nullptr; }
    const field_value& scalar() const noexcept { return scalar_; }
    const value_io* node() const noexcept { return node_; }

private:
    const char* name_ = nullptr;
    field_value scalar_;
    const value_io* node_ = nullptr;
};

// One constructor parameter descriptor of a ctor registry.
struct ctor_param {
    const char* name;
    type_id type;      // typeid of the C++ parameter type
    bool optional;     // omitted args fall back to def (or a value-init value)
    field_value def;   // optional default; empty (kind none) -> value-init
};

// Constructor registry of T (spec 18.3). The primary template is an empty,
// safe table: make_object<T>(args) reports an error for an unregistered type.
// A specialization (RTTI_DEFINE_CTOR or by hand) supplies the ordered
// parameter descriptors, the parameter type list (std::tuple) and a factory
// that builds T from the matched runtime arguments.
template <typename T, typename = void>
struct ctor_registry {
    static constexpr std::size_t count = 0;
    static inline const ctor_param params[1] = {{nullptr, nullptr, false, {}}};
    using param_types = std::tuple<>;
    using factory_fn = T* (*)(const ctor_arg*, std::size_t, error_code&);
    static inline factory_fn factory = nullptr;
};

// True when the type registered a non-empty constructor table.
template <typename T, typename = void>
struct has_ctor_registry : std::false_type {};
template <typename T>
struct has_ctor_registry<T, std::void_t<typename ctor_registry<T>::param_types>>
    : std::bool_constant<(ctor_registry<T>::count > 0)> {};

namespace detail {

// True when a parameter is filled from a scalar field_value (the lanes
// field_value_to accepts plus enums). Other parameter types require a value_io
// node and are filled through rtti::from_value (containers, reflected
// structs, strings via nodes, optional/variant/tuple, ...).
template <typename D, typename = void>
struct is_scalar_param : std::false_type {};
template <typename D>
struct is_scalar_param<
    D, std::enable_if_t<std::is_integral<D>::value ||
                        std::is_floating_point<D>::value ||
                        std::is_enum<D>::value ||
                        std::is_same<D, string_view>::value ||
                        std::is_same<D, std::string>::value>>
    : std::true_type {};

// Convert a scalar field_value into D. Lenient for bool (accepts integer
// 0/1); enums come from the integer lane; everything else uses the strict
// field_value_to lanes.
template <typename D>
inline error_code scalar_assign(D& out, const field_value& v) noexcept {
    if constexpr (std::is_same<D, bool>::value) {
        switch (v.type()) {
            case field_value::kind::boolean:
                out = v.as_bool();
                return error_code::ok;
            case field_value::kind::integer:
                out = (v.as_integer() != 0);
                return error_code::ok;
            default:
                return error_code::kind_mismatch;
        }
    } else if constexpr (std::is_enum<D>::value) {
        if (v.type() != field_value::kind::integer) {
            return error_code::kind_mismatch;
        }
        out = static_cast<D>(v.as_integer());
        return error_code::ok;
    } else {
        return field_value_to(out, v) ? error_code::ok : error_code::kind_mismatch;
    }
}

// Read a scalar out of a value_io node. Returns false for null/array/object.
inline bool node_to_field_value(const value_io* node, field_value& out) noexcept {
    if (node == nullptr) {
        return false;
    }
    bool b = false;
    std::int64_t i = 0;
    double d = 0.0;
    string_view s;
    switch (node->type()) {
        case value_io::kind::boolean:
            if (!node->as_bool(b)) return false;
            out = field_value::of(b);
            return true;
        case value_io::kind::integer:
            if (!node->as_integer(i)) return false;
            out = field_value::of(i);
            return true;
        case value_io::kind::real:
            if (!node->as_real(d)) return false;
            out = field_value::of(d);
            return true;
        case value_io::kind::string:
            if (!node->as_string(s)) return false;
            out = field_value::of(s);
            return true;
        default:
            return false;
    }
}

// Convert one matched constructor argument into the destination parameter type
// D. Scalars accept either a scalar argument or a scalar value_io node;
// non-scalars accept a value_io node and convert through rtti::from_value.
template <typename D>
inline error_code ctor_arg_to(const ctor_arg& a, D& out) noexcept {
    if constexpr (is_scalar_param<D>::value) {
        field_value fv;
        if (a.is_node()) {
            if (!node_to_field_value(a.node(), fv)) {
                return error_code::kind_mismatch;
            }
        } else {
            fv = a.scalar();
        }
        return scalar_assign(out, fv);
    } else {
        if (!a.is_node()) {
            return error_code::kind_mismatch;
        }
        return rtti::from_value(a.node(), out);
    }
}

// Runtime argument/parameter matcher (spec 18.3). Named arguments (any
// argument carrying a parameter name) are matched to parameters by name; the
// remaining parameters receive the unnamed ("positional") arguments in
// declaration order. With no named arguments this is pure positional matching.
class arg_matcher {
public:
    arg_matcher(const ctor_arg* args, std::size_t count) noexcept
        : args_(args), count_(count) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (args_[i].named()) {
                ++named_total_;
            } else {
                ++positional_total_;
            }
        }
    }

    std::size_t named_total() const noexcept { return named_total_; }
    std::size_t named_matched() const noexcept { return named_matched_; }
    std::size_t positional_total() const noexcept { return positional_total_; }
    std::size_t positional_used() const noexcept { return positional_used_; }
    bool any_named() const noexcept { return named_total_ > 0; }

    // The next unused positional argument, skipping named ones. Returns
    // nullptr when the positional pool is exhausted.
    const ctor_arg* take_positional() noexcept {
        while (next_ < count_ && args_[next_].named()) {
            ++next_;
        }
        if (next_ >= count_) {
            return nullptr;
        }
        ++positional_used_;
        return &args_[next_++];
    }

    // A named argument matching `name`; nullptr when none. Once matched, the
    // argument is counted so unmatched named arguments can be reported.
    const ctor_arg* find_named(const char* name) noexcept {
        if (name == nullptr) {
            return nullptr;
        }
        for (std::size_t i = 0; i < count_; ++i) {
            if (args_[i].named() && std::strcmp(args_[i].name(), name) == 0) {
                ++named_matched_;
                return &args_[i];
            }
        }
        return nullptr;
    }

private:
    const ctor_arg* args_;
    std::size_t count_;
    std::size_t named_total_ = 0;
    std::size_t named_matched_ = 0;
    std::size_t positional_total_ = 0;
    std::size_t positional_used_ = 0;
    std::size_t next_ = 0;
};

// True when one of the registered parameters carries the given name.
inline bool param_has_name(const ctor_param* params, std::size_t pcount,
                           const char* name) noexcept {
    if (name == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < pcount; ++i) {
        if (params[i].name != nullptr &&
            std::strcmp(params[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// Assign the I-th parameter from the matched arguments. Optional parameters
// fall back to their registered default (or a value-initialized value).
template <std::size_t I, typename Tuple>
inline bool assign_param(Tuple& values, arg_matcher& m, const ctor_param* params,
                         error_code& ec) noexcept {
    using D = std::tuple_element_t<I, Tuple>;
    const ctor_arg* a = nullptr;
    if (m.any_named()) {
        a = m.find_named(params[I].name);
    }
    if (a == nullptr) {
        a = m.take_positional();
    }
    if (a == nullptr) {
        if (params[I].optional) {
            if (params[I].def.type() != field_value::kind::none) {
                const error_code e = scalar_assign(std::get<I>(values), params[I].def);
                if (e != error_code::ok) {
                    ec = e;
                    return false;
                }
            } else {
                std::get<I>(values) = D{};
            }
            return true;
        }
        ec = error_code::missing_key;
        return false;
    }
    const error_code e = ctor_arg_to<D>(*a, std::get<I>(values));
    if (e != error_code::ok) {
        ec = e;
        return false;
    }
    return true;
}

// Convert all runtime arguments into a typed value tuple. Sequential template
// recursion (MSVC folds with a mid-expression pack fail with C2760/C3878).
template <std::size_t I, typename Tuple, std::size_t N>
struct ctor_fill {
    static bool run(Tuple& values, arg_matcher& m, const ctor_param* params,
                    error_code& ec) noexcept {
        if (!assign_param<I>(values, m, params, ec)) {
            return false;
        }
        return ctor_fill<I + 1, Tuple, N>::run(values, m, params, ec);
    }
};
template <typename Tuple, std::size_t N>
struct ctor_fill<N, Tuple, N> {
    static bool run(Tuple&, arg_matcher&, const ctor_param*, error_code&) noexcept {
        return true;
    }
};

template <typename Tuple>
inline error_code ctor_values(const ctor_arg* args, std::size_t count,
                              const ctor_param* params, Tuple& values) noexcept {
    constexpr std::size_t kParams = std::tuple_size<Tuple>::value;
    arg_matcher m(args, count);
    error_code ec = error_code::ok;
    if (m.any_named()) {
        // Reject unknown parameter names up front (a typo beats a missing-param
        // report and keeps "name not found" distinct from "value absent").
        for (std::size_t i = 0; i < count; ++i) {
            if (args[i].named() && !param_has_name(params, kParams, args[i].name())) {
                return error_code::kind_mismatch;
            }
        }
    }
    if (!ctor_fill<0, Tuple, kParams>::run(values, m, params, ec)) {
        return ec;
    }
    if (m.any_named()) {
        // Every named and every positional argument must have been consumed.
        if (m.named_matched() != m.named_total() ||
            m.positional_used() != m.positional_total()) {
            return error_code::kind_mismatch;
        }
        return error_code::ok;
    }
    // Pure positional mode: at most one argument per parameter.
    if (m.positional_total() > kParams) {
        return error_code::kind_mismatch;
    }
    return error_code::ok;
}

// Construct T on the heap from the converted value tuple.
template <typename T, typename Tuple, std::size_t... I>
inline T* ctor_new_impl(Tuple& values, std::index_sequence<I...>) {
    return new T(std::move(std::get<I>(values))...);
}

// Placement-construct T into preallocated raw memory.
template <typename T, typename Tuple, std::size_t... I>
inline T* ctor_place_impl(void* raw, Tuple& values, std::index_sequence<I...>) {
    return ::new (raw) T(std::move(std::get<I>(values))...);
}

// Validate the registry and convert all arguments up front. Returns
// kind_mismatch for an unregistered/inconsistent table.
template <typename T, typename Tuple>
inline error_code ctor_prepare(const ctor_arg* args, std::size_t count,
                               Tuple& values) noexcept {
    constexpr std::size_t kParams = std::tuple_size<Tuple>::value;
    if constexpr (kParams == 0 || ctor_registry<T>::count != kParams) {
        return error_code::kind_mismatch;
    } else {
        return ctor_values<Tuple>(args, count, ctor_registry<T>::params, values);
    }
}

// The generic registry factory entry point referenced by RTTI_DEFINE_CTOR.
template <typename T, typename Tuple>
inline T* ctor_factory(const ctor_arg* args, std::size_t count, error_code& ec) {
    Tuple values;
    ec = ctor_prepare<T, Tuple>(args, count, values);
    if (ec != error_code::ok) {
        return nullptr;
    }
    return ctor_new_impl<T, Tuple>(values,
                                   std::make_index_sequence<std::tuple_size<Tuple>::value>());
}

} // namespace detail

// ---------------------------------------------------------------------------
// C#-style reference conversions (spec: cast/as variance design).
//   as<T>(p)   : nullptr on failure; no error payload.
//   cast<T>(p) : result<T*> with kind_mismatch on failure and no_such_field
//                for a null source.
// Covariant upcasts use the compiler's offset-correct static_cast, including
// virtual bases and multiple inheritance (see detail::base_adjuster).
// Contravariant downcasts require the static source type to be polymorphic
// (dynamic_cast); a
// non-polymorphic source cannot be checked at runtime and fails cleanly.
// Unrelated polymorphic types may still succeed through a dynamic_cast
// cross-cast. All entry points are failure-safe (spec section 0).
// ---------------------------------------------------------------------------

template <typename T, typename U>
T* as(U* p) noexcept {
    static_assert(!std::is_const<T>::value && !std::is_const<U>::value,
        "rtti::as/cast do not accept const-qualified source or target types");
    if (p == nullptr) {
        return nullptr;
    }
    if constexpr (std::is_base_of<T, U>::value) {
        // Covariant direction (or same type): the source statically derives
        // from the target, so the adjustment is known at compile time.
        return static_cast<T*>(detail::base_adjuster<U, T>::run(static_cast<void*>(p)));
    } else if constexpr (std::is_base_of<U, T>::value) {
        // Contravariant downcast: runtime-checked, needs a polymorphic source.
        if constexpr (std::is_polymorphic<U>::value) {
            return dynamic_cast<T*>(p);
        } else {
            return nullptr;
        }
    } else {
        // Unrelated: only a polymorphic cross-cast can succeed.
        if constexpr (std::is_polymorphic<U>::value && std::is_polymorphic<T>::value) {
            return dynamic_cast<T*>(p);
        } else {
            return nullptr;
        }
    }
}

template <typename T, typename U>
result<T*> cast(U* p) {
    result<T*> r;
    if (p == nullptr) {
        r.err.code = error_code::no_such_field;
        return r;
    }
    T* q = as<T>(p);
    if (q == nullptr) {
        r.err.code = error_code::kind_mismatch;
        return r;
    }
    r.value = q;
    return r;
}

// Handle to a reflectively-created T instance (spec 18.2). Two lifetime modes:
//  - RAII (default): the handle owns the heap instance and destroys it on its
//    own destruction (created by make_object<T> / make_object<T>(args)).
//  - Manual (allocator path): the caller owns the raw memory (make_object<T>
//    with an allocator); destroy() must be called explicitly. The handle
//    destructor leaves the manual instance untouched, preventing double frees.
// All operations delegate to the reflect core (field table, to_value /
// from_value, attributes, type_of); no format adapter is needed to build a
// handle.
template <typename T>
class object_ref {
public:
    using value_type = T;

    object_ref() noexcept = default;
    ~object_ref() noexcept {
        if (owning_ && p_ != nullptr) {
            delete p_;
        }
        // Manual mode: lifetime is the creator's (explicit destroy()).
    }
    object_ref(object_ref&& o) noexcept
        : p_(o.p_), release_(std::move(o.release_)), owning_(o.owning_) {
        o.p_ = nullptr;
        o.owning_ = false;
    }
    object_ref& operator=(object_ref&& o) noexcept {
        if (this != &o) {
            destroy();
            p_ = o.p_;
            release_ = std::move(o.release_);
            owning_ = o.owning_;
            o.p_ = nullptr;
            o.owning_ = false;
        }
        return *this;
    }
    object_ref(const object_ref&) = delete;
    object_ref& operator=(const object_ref&) = delete;

    T* get() noexcept { return p_; }
    const T* get() const noexcept { return p_; }
    T* operator->() noexcept { return p_; }
    const T* operator->() const noexcept { return p_; }
    T& operator*() noexcept { return *p_; }
    const T& operator*() const noexcept { return *p_; }
    bool valid() const noexcept { return p_ != nullptr; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

    // C#-style variance conversions: see rtti::as / rtti::cast. `as` returns
    // nullptr on failure; `cast` returns a result with an error_code. The
    // returned pointer borrows the handle's object.
    template <typename Target>
    Target* as() noexcept {
        return rtti::as<Target>(p_);
    }
    template <typename Target>
    result<Target*> cast() noexcept {
        return rtti::cast<Target>(p_);
    }
    template <typename Target>
    Target* as() const noexcept {
        return rtti::as<Target>(p_);
    }
    template <typename Target>
    result<Target*> cast() const noexcept {
        return rtti::cast<Target>(p_);
    }

    // Manual-mode destroy: runs the destructor and returns the raw memory to
    // the allocator's releaser. Idempotent (a second call is a no-op). In
    // RAII mode this releases the owned instance.
    void destroy() noexcept {
        if (p_ == nullptr) {
            return;
        }
        if (owning_) {
            delete p_;
        } else {
            p_->~T();
            if (release_) {
                release_(p_);
            }
        }
        release_ = nullptr;
        p_ = nullptr;
        owning_ = false;
    }

    // --- Construction helpers (advanced) ------------------------------------
    // Adopt a heap instance (RAII mode; ownership transfers to the handle).
    static object_ref adopt(T* p) noexcept {
        object_ref r;
        r.p_ = p;
        r.owning_ = true;
        return r;
    }
    // Wrap a placement-constructed instance (manual mode). `release` (optional,
    // owner-supplied) returns the raw memory after destroy() ran the
    // destructor; arena/monotonic allocators pass no releaser.
    static object_ref placement(T* p, std::function<void(void*)> release) noexcept {
        object_ref r;
        r.p_ = p;
        r.release_ = std::move(release);
        r.owning_ = false;
        return r;
    }

    // --- Field get/set (spec 18.2) ------------------------------------------
    error_code get(const char* name, field_value& out) const noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr) {
            return error_code::no_such_field;
        }
        out = f->get(static_cast<const void*>(p_));
        return error_code::ok;
    }
    error_code set(const char* name, const field_value& v) noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr) {
            return error_code::no_such_field;
        }
        return f->set(p_, v) ? error_code::ok : error_code::kind_mismatch;
    }

    // --- Serialization bridge (format-agnostic value_io path) ---------------
    error_code to_value(value_io* dst) const { return rtti::to_value(dst, *p_); }
    error_code from_value(const value_io* src) { return rtti::from_value(src, *p_); }
    error_code to_value(value_io* dst, const rtti::detail::naming_policy& np) const {
        return rtti::to_value_named(dst, *p_, np);
    }
    error_code from_value(const value_io* src, const rtti::detail::naming_policy& np) {
        return rtti::from_value_named(src, *p_, np);
    }

    // --- Container operations (spec 18.2) -----------------------------------
    error_code add(const char* name, const value_io* element) noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr) {
            return error_code::no_such_field;
        }
        return f->field_add(p_, element);
    }
    error_code add(const char* name, const field_value& element) noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr) {
            return error_code::no_such_field;
        }
        return f->field_add_scalar(p_, element);
    }
    error_code erase_at(const char* name, std::size_t index) noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr) {
            return error_code::no_such_field;
        }
        return f->erase_at(p_, index);
    }
    error_code erase_key(const char* name, string_view key) noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr) {
            return error_code::no_such_field;
        }
        return f->erase_key(p_, key);
    }
    error_code clear(const char* name) noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr) {
            return error_code::no_such_field;
        }
        return f->clear(p_);
    }
    std::size_t field_size(const char* name, error_code* out_err = nullptr) const noexcept {
        if (out_err != nullptr) {
            *out_err = error_code::ok;
        }
        const field_access_base* f = find_(name);
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
        return f->size(p_);
    }
    template <typename Fn>
    error_code for_each_element(const char* name, value_io* scratch, Fn&& fn) const noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr) {
            return error_code::no_such_field;
        }
        if (!f->is_container()) {
            return error_code::kind_mismatch;
        }
        return f->for_each(p_, scratch,
                           typename field_access_base::element_visitor(std::forward<Fn>(fn)));
    }
    // Element type of a container field (spec 18.2 type query).
    const type_desc* element_type(const char* name) const noexcept {
        const field_access_base* f = find_(name);
        if (f == nullptr || !f->is_container()) {
            return nullptr;
        }
        const std::size_t n = rtti::field_count<T>();
        const auto* const* arr = rtti::fields<T>();
        for (std::size_t i = 0; i < n; ++i) {
            if (arr[i] == f) {
                const type_desc* fd = fields_of<T>::desc[i].type();
                if (fd != nullptr && fd->kind == type_kind::container &&
                    fd->as_container != nullptr) {
                    return fd->as_container->element();
                }
                return nullptr;
            }
        }
        return nullptr;
    }

    // --- Attributes (spec 18.2 / section 17) --------------------------------
    std::size_t attr_count() const noexcept { return rtti::attr_count<T>(); }
    bool has_attr(string_view name) const noexcept { return rtti::has_attr<T>(name); }
    const attr* find_attr(string_view name) const noexcept {
        return rtti::find_attr<T>(name);
    }
    field_attr_list field_attr(string_view name) const noexcept {
        return rtti::field_attr<T>(name);
    }
    bool field_has_attr(string_view name, string_view attribute) const noexcept {
        return rtti::field_has_attr<T>(name, attribute);
    }

    // --- Type query (spec 18.2) ---------------------------------------------
    const type_desc* type_of() const noexcept { return rtti::type_of<T>(); }

private:
    // Locate a reflected field by name through the field table.
    const field_access_base* find_(const char* name) const noexcept {
        if (name == nullptr) {
            return nullptr;
        }
        const std::size_t n = rtti::field_count<T>();
        const auto* const* arr = rtti::fields<T>();
        for (std::size_t i = 0; i < n; ++i) {
            const field_access_base* f = arr[i];
            if (f != nullptr && f->name != nullptr &&
                std::strcmp(f->name, name) == 0) {
                return f;
            }
        }
        return nullptr;
    }

    T* p_ = nullptr;
    std::function<void(void*)> release_;
    bool owning_ = false;
};

namespace detail {
// Forward declaration: instance::as (below) composes the base-chain adjusters
// through this helper, whose definition follows the class. The call's argument
// types are non-dependent, so the name must be visible at definition time;
// depth is passed explicitly (the definition supplies the default).
inline void* adjust_to(void* ptr, const type_desc* from,
                       const type_desc* target, std::size_t depth) noexcept;
} // namespace detail

// Type-erased instance handle for heterogeneous ORM pools (spec 18.2).
// Wraps any object_ref<T> and exposes a small uniform operation set: type and
// descriptor query, field get/set by name, and the value_io serialization
// bridge. Move-only; an explicit destroy() forwards to the wrapped handle.
class instance {
public:
    instance() noexcept = default;
    ~instance() noexcept {
        if (release_) {
            release_(obj_);
        }
    }
    instance(instance&& o) noexcept
        : obj_(o.obj_), op_(o.op_), type_(o.type_), type_of_(o.type_of_),
          release_(std::move(o.release_)) {
        o.obj_ = nullptr;
        o.op_ = nullptr;
        o.type_ = nullptr;
        o.type_of_ = nullptr;
    }
    instance& operator=(instance&& o) noexcept {
        if (this != &o) {
            if (release_) {
                release_(obj_);
            }
            obj_ = o.obj_;
            op_ = o.op_;
            type_ = o.type_;
            type_of_ = o.type_of_;
            release_ = std::move(o.release_);
            o.obj_ = nullptr;
            o.op_ = nullptr;
            o.type_ = nullptr;
            o.type_of_ = nullptr;
        }
        return *this;
    }
    instance(const instance&) = delete;
    instance& operator=(const instance&) = delete;

    // Wrap a typed handle (make_object<T> / create_instance<T>).
    template <typename T>
    static instance from(object_ref<T> handle) {
        instance r;
        auto* holder = new object_ref<T>(std::move(handle));
        r.obj_ = holder;
        r.op_ = &ops_impl<T>::table;
        r.type_ = &typeid(T);
        r.type_of_ = rtti::type_of<T>();
        r.release_ = [](void* p) { delete static_cast<object_ref<T>*>(p); };
        return r;
    }

    type_id type() const noexcept { return type_; }
    const type_desc* type_of() const noexcept { return type_of_; }
    bool valid() const noexcept { return obj_ != nullptr && op_ != nullptr; }
    explicit operator bool() const noexcept { return valid(); }

    // Exact runtime type equality: true when the wrapped object's type is T.
    // Use is_derived_from<T> (reflect.hpp) for the "is-a" relationship.
    template <typename T>
    bool is() const noexcept {
        return type_ != nullptr && *type_ == typeid(T);
    }

    // The wrapped object pointer (type-erased). Valid while the handle is
    // valid; used by dynamic_cast_ref after the type-id chain is verified.
    void* data() noexcept { return valid() ? op_->storage(obj_) : nullptr; }
    const void* data() const noexcept {
        return valid() ? op_->storage(obj_) : nullptr;
    }

    error_code get(const char* name, field_value& out) const noexcept {
        if (!valid()) {
            return error_code::no_such_field;
        }
        return op_->get(obj_, name, out);
    }
    error_code set(const char* name, const field_value& v) noexcept {
        if (!valid()) {
            return error_code::no_such_field;
        }
        return op_->set(obj_, name, v);
    }
    error_code to_value(value_io* dst) const {
        if (!valid()) {
            return error_code::no_such_field;
        }
        return op_->to(obj_, dst);
    }
    error_code from_value(const value_io* src) {
        if (!valid()) {
            return error_code::no_such_field;
        }
        return op_->from(obj_, src);
    }
    // Explicit destroy for manual (allocator) instances; RAII handles are
    // released by the wrapped object_ref.
    void destroy() noexcept {
        if (!valid()) {
            return;
        }
        op_->destroy(obj_);
        release_(obj_);
        obj_ = nullptr;
        op_ = nullptr;
        release_ = nullptr;
    }

    // C#-style variance conversions through the type-erased handle. Full
    // precision: the runtime dynamic type (when the stored type is
    // polymorphic) resolves to its reflected descriptor, and the base chain's
    // offset-correct adjusters compute the target subobject. A downcast whose
    // dynamic descriptor was never materialized (type_of<D>() never called)
    // falls back to the static chain, which only reaches bases. Failures
    // return nullptr / error_code, never a wrong pointer (spec section 0).
    template <typename T>
    T* as() noexcept {
        static_assert(!std::is_const<T>::value,
            "rtti::as/cast do not accept const-qualified source or target types");
        if (!valid()) {
            return nullptr;
        }
        // rtti:: qualification is required: this class has a non-template
        // member type_of() that would otherwise shadow the free function.
        const type_desc* target = rtti::type_of<T>();
        if (target == nullptr) {
            return nullptr;
        }
        const std::type_info* dt = op_->dynamic_type(obj_);
        const type_desc* root = type_of_;
        void* root_ptr = data();
        if (dt != nullptr && type_of_ != nullptr && type_of_->id != nullptr &&
            *dt != *type_of_->id) {
            const type_desc* dd = detail::find_desc(*dt);
            if (dd != nullptr) {
                root = dd;
                root_ptr = op_->most_derived(obj_);
            }
        }
        return static_cast<T*>(detail::adjust_to(root_ptr, root, target, 0));
    }

    template <typename T>
    result<T*> cast() noexcept {
        static_assert(!std::is_const<T>::value,
            "rtti::as/cast do not accept const-qualified source or target types");
        result<T*> r;
        if (!valid()) {
            r.err.code = error_code::no_such_field;
            return r;
        }
        T* q = as<T>();
        if (q == nullptr) {
            r.err.code = error_code::kind_mismatch;
            return r;
        }
        r.value = q;
        return r;
    }

private:
    struct ops {
        error_code (*get)(void* obj, const char* name, field_value& out) noexcept;
        error_code (*set)(void* obj, const char* name, const field_value& v) noexcept;
        error_code (*to)(const void* obj, value_io* dst);
        error_code (*from)(void* obj, const value_io* src);
        void (*destroy)(void* obj) noexcept;
        void* (*storage)(void* obj) noexcept;
        void* (*most_derived)(void* obj) noexcept;             // dynamic_cast<void*>
        const std::type_info* (*dynamic_type)(void* obj) noexcept;
    };
    template <typename T>
    struct ops_impl {
        static error_code get(void* obj, const char* name, field_value& out) noexcept {
            return static_cast<object_ref<T>*>(obj)->get(name, out);
        }
        static error_code set(void* obj, const char* name, const field_value& v) noexcept {
            return static_cast<object_ref<T>*>(obj)->set(name, v);
        }
        static error_code to(const void* obj, value_io* dst) {
            return static_cast<const object_ref<T>*>(obj)->to_value(dst);
        }
        static error_code from(void* obj, const value_io* src) {
            return static_cast<object_ref<T>*>(obj)->from_value(src);
        }
        static void destroy(void* obj) noexcept {
            static_cast<object_ref<T>*>(obj)->destroy();
        }
        static void* storage(void* obj) noexcept {
            return static_cast<object_ref<T>*>(obj)->get();
        }
        // The most-derived object address (dynamic_cast<void*> for polymorphic
        // T, else the static address). Used to re-root the base-chain walk at
        // the runtime dynamic type for a full-precision downcast.
        static void* most_derived(void* obj) noexcept {
            T* p = static_cast<object_ref<T>*>(obj)->get();
            if (p == nullptr) {
                return nullptr;
            }
            if constexpr (std::is_polymorphic<T>::value) {
                return dynamic_cast<void*>(p);
            } else {
                return static_cast<void*>(p);
            }
        }
        // The runtime dynamic std::type_info: typeid(*p) when the stored type
        // is polymorphic (yields the most-derived type), else the static type.
        static const std::type_info* dynamic_type(void* obj) noexcept {
            T* p = static_cast<object_ref<T>*>(obj)->get();
            if (p == nullptr) {
                return nullptr;
            }
            if constexpr (std::is_polymorphic<T>::value) {
                return &typeid(*p);
            } else {
                return &typeid(T);
            }
        }
        static inline const ops table = {get, set, to, from, destroy, storage,
                                         most_derived, dynamic_type};
    };

    void* obj_ = nullptr;
    const ops* op_ = nullptr;
    type_id type_ = nullptr;
    const type_desc* type_of_ = nullptr;
    std::function<void(void*)> release_;
};

namespace detail {

// True when lhs and rhs name the same type (typeid value comparison).
inline bool same_type_id(type_id a, type_id b) noexcept {
    return a == b || (a != nullptr && b != nullptr && *a == *b);
}

// Depth-guarded walk of the runtime base chain: true when `self` is `target`
// or transitively derived from it through the reflected base_type_list links.
// The reflection graph is acyclic by construction, but a depth cap keeps an
// adversarial/malformed chain from recursing forever.
inline bool derived_from_desc(const type_desc* self, const type_desc* target,
                              std::size_t depth = 0) noexcept {
    if (self == nullptr || target == nullptr) {
        return false;
    }
    if (same_type_id(self->id, target->id)) {
        return true;
    }
    if (depth >= 64 || self->as_bases == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < self->as_bases->count; ++i) {
        if (self->as_bases->resolve[i]() != nullptr &&
            derived_from_desc(self->as_bases->resolve[i](), target, depth + 1)) {
            return true;
        }
    }
    return false;
}

// Walk the reflected base chain from `from` toward `target`, composing
// offset-correct adjusters from `ptr` (which points to the `from` subobject).
// Returns the target subobject pointer, or nullptr when the target is not
// reachable through a chain of legal adjustments. Depth-guarded like
// derived_from_desc.
inline void* adjust_to(void* ptr, const type_desc* from,
                       const type_desc* target, std::size_t depth = 0) noexcept {
    if (ptr == nullptr || from == nullptr || target == nullptr) {
        return nullptr;
    }
    if (same_type_id(from->id, target->id)) {
        return ptr;
    }
    if (depth >= 64 || from->as_bases == nullptr || from->as_bases->adjust == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < from->as_bases->count; ++i) {
        const type_desc* b = from->as_bases->resolve[i]();
        if (b == nullptr) {
            continue;
        }
        void* bp = from->as_bases->adjust[i](ptr);
        if (bp == nullptr) {
            continue;
        }
        void* r = adjust_to(bp, b, target, depth + 1);
        if (r != nullptr) {
            return r;
        }
    }
    return nullptr;
}

// Runtime scalar-convertibility (spec 19.2: same type / implicitly-convertible
// scalar). Mirrors std::is_convertible for the scalar kind pairs the runtime
// type_desc can distinguish (integral promotion/widening, bool widening, real
// to bool); strings convert only to strings. Only called after identical-type
// and base-chain checks, so same-kind scalar pairs that are genuinely
// convertible (e.g. int -> int) still return true here. Non-scalar kinds
// (reflected structs, containers) never convert by kind: a reflected target
// must match by type or ancestry, which the caller already checked.
inline bool scalar_convertible(type_kind from, type_kind to) noexcept {
    if (type_kind::boolean == from) {
        return type_kind::boolean == to || type_kind::integer == to ||
               type_kind::real == to;
    }
    if (type_kind::integer == from) {
        return type_kind::boolean == to || type_kind::integer == to ||
               type_kind::real == to;
    }
    if (type_kind::real == from) {
        return type_kind::boolean == to || type_kind::real == to;
    }
    if (type_kind::enum_type == from) {
        return type_kind::integer == to;
    }
    if (type_kind::string == from) {
        return type_kind::string == to;
    }
    return false;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Method reflection (spec section 22).
//
// reflect_method<T>("Name", args...) dispatches a REGISTERED STATIC method
// (no instance); reflect_method(obj, "Name", args...) dispatches a member
// method through its instance (a T object, an object_ref<T>, or an `instance`
// handle). Methods are registered one per macro invocation
// (RTTI_DEFINE_METHOD / RTTI_DEFINE_STATIC_METHOD): each links a method_entry
// into the per-type table read by methods<T>() / method_count<T>(). Parameters
// travel through the ctor_arg family (scalar field_value or value_io node,
// positional binding); results come back as a scalar field_value or, for
// composite types, through a caller supplied value_io node. Every failure
// returns an error_code: unregistered => no_such_method, argument
// count/type mismatch => kind_mismatch, missing instance => no_such_field.
// Never throws, never crashes (spec 0).
// ---------------------------------------------------------------------------

// One reflected method of a type (as returned by methods<T>()).
struct method_info {
    const char* name;
    bool is_static;
    const char* signature;
};

namespace detail {

// A registered method entry stored in the per-type table.
struct method_entry {
    const char* name;
    bool is_static;
    const char* signature;
    error_code (*call)(void* self, const ctor_arg* args, std::size_t count,
                       field_value* out, value_io* node);
    const std::type_info* owner;   // typeid(T)
    const method_entry* next;
};

// Per-type list head (function-local static: one instance, lazily built).
template <typename T>
inline const method_entry*& method_head() noexcept {
    static const method_entry* head = nullptr;
    return head;
}

template <typename T>
inline void method_link(const method_entry* e) noexcept {
    const_cast<method_entry*>(e)->next = method_head<T>();
    method_head<T>() = e;
}

// Program-wide list for type-erased `instance` lookups (owner typeid matches).
inline const method_entry*& method_global_head() noexcept {
    static const method_entry* head = nullptr;
    return head;
}

inline void method_global_link(const method_entry* e) noexcept {
    const_cast<method_entry*>(e)->next = method_global_head();
    method_global_head() = e;
}

// Decompose a member-function pointer into return type and parameter tuple.
template <typename M>
struct member_traits;
template <typename R, typename C, typename... A>
struct member_traits<R (C::*)(A...)> {
    using ret = R;
    using owner = C;
    using params = std::tuple<A...>;
};
template <typename R, typename C, typename... A>
struct member_traits<R (C::*)(A...) const> {
    using ret = R;
    using owner = C;
    using params = std::tuple<A...>;
};
template <typename R, typename C, typename... A>
struct member_traits<R (C::*)(A...) noexcept> {
    using ret = R;
    using owner = C;
    using params = std::tuple<A...>;
};
template <typename R, typename C, typename... A>
struct member_traits<R (C::*)(A...) const noexcept> {
    using ret = R;
    using owner = C;
    using params = std::tuple<A...>;
};

// Decompose a plain function pointer (static method) into return + params.
template <typename F>
struct static_traits;
template <typename R, typename... A>
struct static_traits<R (*)(A...)> {
    using ret = R;
    using params = std::tuple<A...>;
};
template <typename R, typename... A>
struct static_traits<R (*)(A...) noexcept> {
    using ret = R;
    using params = std::tuple<A...>;
};

// Reference-stripped parameter tuple (decayed: const& / reference parameters
// are materialized by value before the call, so a reference to a temporary is
// never created and std::tuple default-constructs cleanly).
template <typename Tuple>
struct decay_tuple;
template <typename... A>
struct decay_tuple<std::tuple<A...>> {
    using type = std::tuple<std::decay_t<A>...>;
};

// Convert the I-th runtime argument into the I-th parameter (positional).
// Returns the first conversion error. Sequential recursion (no pack fold): an
// MSVC mid-expression pack fails with C2760/C3878.
template <std::size_t I, typename Tuple, std::size_t N>
struct method_fill {
    static error_code run(const ctor_arg* args, Tuple& values) noexcept {
        using D = std::tuple_element_t<I, Tuple>;
        const error_code e = ctor_arg_to<D>(args[I], std::get<I>(values));
        if (e != error_code::ok) {
            return e;
        }
        return method_fill<I + 1, Tuple, N>::run(args, values);
    }
};
template <typename Tuple, std::size_t N>
struct method_fill<N, Tuple, N> {
    static error_code run(const ctor_arg*, Tuple&) noexcept {
        return error_code::ok;
    }
};

// Materialize the stored scalar result as a field_value.
template <typename D>
inline field_value scalar_field(D&& v) noexcept {
    using U = std::decay_t<D>;
    if constexpr (std::is_same<U, bool>::value) {
        return field_value::of(v);
    } else if constexpr (std::is_enum<U>::value) {
        return field_value::of(static_cast<std::int64_t>(v));
    } else if constexpr (std::is_integral<U>::value) {
        return field_value::of(static_cast<std::int64_t>(v));
    } else if constexpr (std::is_floating_point<U>::value) {
        return field_value::of(static_cast<double>(v));
    } else if constexpr (std::is_same<U, string_view>::value) {
        return field_value::of(v);
    } else if constexpr (std::is_same<U, const char*>::value) {
        return field_value::of(string_view(v == nullptr ? "" : v));
    } else {
        return field_value{};
    }
}

// Deliver a non-void method result: scalar lanes into the field_value sink,
// composite values (containers, reflected structs, std::string, ...) into the
// value_io node sink. A scalar with neither sink returns kind_mismatch.
template <typename R>
inline error_code result_assign(field_value* out, value_io* node, R&& value) noexcept {
    using D = std::decay_t<R>;
    if constexpr (std::is_arithmetic<D>::value || std::is_enum<D>::value ||
                  std::is_same<D, string_view>::value ||
                  std::is_same<D, const char*>::value) {
        if (out != nullptr) {
            *out = scalar_field(std::forward<R>(value));
            return error_code::ok;
        }
        if (node != nullptr) {
            return ::rtti::to_value(node, value);
        }
        return error_code::kind_mismatch;
    } else {
        if (node == nullptr) {
            return error_code::kind_mismatch;
        }
        return ::rtti::to_value(node, value);
    }
}

// Compile-time helper: MSVC warns C4127 on a direct constant `if constexpr`
// condition after template instantiation; a constexpr function call keeps the
// condition dependent-looking and the branch selection warning-free.
template <typename Ret>
inline constexpr bool is_void_ret() noexcept {
    return std::is_void<Ret>::value;
}

// Unpack the parameter tuple and call the member function on `obj`.
template <typename Owner, typename M, typename Values, std::size_t... I>
inline error_code member_apply(M mp, Owner* obj, Values& values, field_value* out,
                               value_io* node, std::index_sequence<I...>) noexcept {
    using Tr = member_traits<M>;
    using Ret = typename Tr::ret;
    if constexpr (is_void_ret<Ret>()) {
        (obj->*mp)(std::get<I>(values)...);
        return error_code::ok;
    } else {
        auto r = (obj->*mp)(std::get<I>(values)...);
        return result_assign(out, node, r);
    }
}

// Type-erased member-method entry point (wrap any member function pointer,
// const / noexcept included, through member_traits dispatch).
template <typename Owner, typename M>
inline error_code member_invoke(M mp, void* self, const ctor_arg* args,
                                std::size_t count, field_value* out,
                                value_io* node) noexcept {
    using Tr = member_traits<M>;
    using Ret = typename Tr::ret;
    using ParamSeq = typename Tr::params;
    using Values = typename decay_tuple<ParamSeq>::type;
    constexpr std::size_t N = std::tuple_size<ParamSeq>::value;
    if (self == nullptr) {
        return error_code::no_such_field;
    }
    Owner* obj = static_cast<Owner*>(self);
    if (count != N) {
        return error_code::kind_mismatch;
    }
    if constexpr (N > 0) {
        if (args == nullptr) {
            return error_code::kind_mismatch;
        }
    }
    Values values;
    const error_code ec = method_fill<0, Values, N>::run(args, values);
    if (ec != error_code::ok) {
        return ec;
    }
    return member_apply<Owner, M, Values>(mp, obj, values, out, node,
                                          std::make_index_sequence<N>{});
}

// Unpack the parameter tuple and call the static method.
template <typename F, typename Values, std::size_t... I>
inline error_code static_apply(F fp, Values& values, field_value* out,
                               value_io* node, std::index_sequence<I...>) noexcept {
    using Tr = static_traits<F>;
    using Ret = typename Tr::ret;
    if constexpr (is_void_ret<Ret>()) {
        fp(std::get<I>(values)...);
        return error_code::ok;
    } else {
        auto r = fp(std::get<I>(values)...);
        return result_assign(out, node, r);
    }
}

// Type-erased static-method entry point.
template <typename F>
inline error_code static_invoke(F fp, const ctor_arg* args, std::size_t count,
                                field_value* out, value_io* node) noexcept {
    using Tr = static_traits<F>;
    using Ret = typename Tr::ret;
    using ParamSeq = typename Tr::params;
    using Values = typename decay_tuple<ParamSeq>::type;
    constexpr std::size_t N = std::tuple_size<ParamSeq>::value;
    if (count != N) {
        return error_code::kind_mismatch;
    }
    if constexpr (N > 0) {
        if (args == nullptr) {
            return error_code::kind_mismatch;
        }
    }
    Values values;
    const error_code ec = method_fill<0, Values, N>::run(args, values);
    if (ec != error_code::ok) {
        return ec;
    }
    return static_apply<F, Values>(fp, values, out, node,
                                   std::make_index_sequence<N>{});
}

// Locate a registered method by name in T's table.
template <typename T>
inline const method_entry* find_method(const char* name) noexcept {
    if (name == nullptr) {
        return nullptr;
    }
    for (const method_entry* e = method_head<T>(); e != nullptr; e = e->next) {
        if (e->name != nullptr && std::strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return nullptr;
}

// Locate a registered method by name through the global table (instance path).
inline const method_entry* find_method_global(const std::type_info* owner,
                                              const char* name) noexcept {
    if (owner == nullptr || name == nullptr) {
        return nullptr;
    }
    for (const method_entry* e = method_global_head(); e != nullptr; e = e->next) {
        if (e->name != nullptr && std::strcmp(e->name, name) == 0 &&
            e->owner != nullptr && *e->owner == *owner) {
            return e;
        }
    }
    return nullptr;
}

// Reflect T's method table into a stable array (built once on first use).
template <typename T>
inline const std::vector<method_info>& method_table() noexcept {
    static const std::vector<method_info> table = [] {
        std::vector<method_info> out;
        for (const method_entry* e = method_head<T>(); e != nullptr; e = e->next) {
            method_info m;
            m.name = e->name;
            m.is_static = e->is_static;
            m.signature = e->signature;
            out.push_back(m);
        }
        return out;
    }();
    return table;
}

} // namespace detail

// Static-method call: reflect_method<T>("Name", args, count[, node]).
template <typename T>
inline result<field_value> reflect_method(const char* name, const ctor_arg* args,
                                          std::size_t count,
                                          value_io* node = nullptr) noexcept {
    result<field_value> r;
    if (name == nullptr || (count > 0 && args == nullptr)) {
        r.err.code = error_code::kind_mismatch;
        return r;
    }
    const detail::method_entry* e = detail::find_method<T>(name);
    if (e == nullptr) {
        r.err.code = error_code::no_such_method;
        return r;
    }
    if (!e->is_static) {
        // A member method called without an instance: invalid instance.
        r.err.code = error_code::no_such_field;
        return r;
    }
    r.err.code = e->call(nullptr, args, count, &r.value, node);
    return r;
}

template <typename T>
inline result<field_value> reflect_method(const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept {
    return reflect_method<T>(name, args.begin(), args.size(), node);
}

// Member-method call through an object reference.
template <typename T>
inline result<field_value> reflect_method(T& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept {
    result<field_value> r;
    if (name == nullptr || (count > 0 && args == nullptr)) {
        r.err.code = error_code::kind_mismatch;
        return r;
    }
    const detail::method_entry* e = detail::find_method<T>(name);
    if (e == nullptr) {
        r.err.code = error_code::no_such_method;
        return r;
    }
    r.err.code = e->call(static_cast<void*>(&obj), args, count, &r.value, node);
    return r;
}

template <typename T>
inline result<field_value> reflect_method(T& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept {
    return reflect_method(obj, name, args.begin(), args.size(), node);
}

// Member-method call through an object_ref<T> handle.
template <typename T>
inline result<field_value> reflect_method(object_ref<T>& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept {
    result<field_value> r;
    if (!obj.valid()) {
        r.err.code = error_code::no_such_field;
        return r;
    }
    return reflect_method(*obj.get(), name, args, count, node);
}

template <typename T>
inline result<field_value> reflect_method(object_ref<T>& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept {
    return reflect_method(obj, name, args.begin(), args.size(), node);
}

// Member-method call through a type-erased instance handle.
inline result<field_value> reflect_method(instance& obj, const char* name,
                                          const ctor_arg* args, std::size_t count,
                                          value_io* node = nullptr) noexcept {
    result<field_value> r;
    if (!obj.valid()) {
        r.err.code = error_code::no_such_field;
        return r;
    }
    if (name == nullptr || (count > 0 && args == nullptr)) {
        r.err.code = error_code::kind_mismatch;
        return r;
    }
    const detail::method_entry* e = detail::find_method_global(obj.type(), name);
    if (e == nullptr) {
        r.err.code = error_code::no_such_method;
        return r;
    }
    r.err.code = e->call(obj.data(), args, count, &r.value, node);
    return r;
}

inline result<field_value> reflect_method(instance& obj, const char* name,
                                          std::initializer_list<ctor_arg> args,
                                          value_io* node = nullptr) noexcept {
    return reflect_method(obj, name, args.begin(), args.size(), node);
}

// Query T's method table (name + static flag), alongside its field table.
template <typename T>
inline std::size_t method_count() noexcept {
    return detail::method_table<T>().size();
}

template <typename T>
inline const method_info* methods() noexcept {
    const std::vector<method_info>& v = detail::method_table<T>();
    return v.empty() ? nullptr : v.data();
}

} // namespace rtti

#define RTTI_FIELD_DECL(T, f)                                                 \
    static inline const ::rtti::typed_field_access<T, decltype(T::f)> rtti_acc_##f{ \
        #f, &T::f, &typeid(decltype(T::f))};

#define RTTI_FIELD_REF(T, f) &rtti_acc_##f,

#define RTTI_FIELD_TYPE_DESC(T, f) { #f, &::rtti::type_of<decltype(T::f)> },

// Typed member-pointer table of a type's OWN fields (spec 19). Declared by the
// registration macros next to the accessor tables; the derived-merge machinery
// converts these member pointers to a derived type to build offset-correct
// base accessors. MSVC rejects trailing commas in both template-argument and
// function-call argument lists, so the member tables are emitted through
// per-count comma-joining macros (the same pattern as RTTI_CTOR_TYPES_N).
#define RTTI_MEMBER_TYPES_0(T)
#define RTTI_MEMBER_TYPES_1(T, a1) decltype(&T::a1)
#define RTTI_MEMBER_TYPES_2(T, a1, a2) decltype(&T::a1), decltype(&T::a2)
#define RTTI_MEMBER_TYPES_3(T, a1, a2, a3) decltype(&T::a1), decltype(&T::a2), decltype(&T::a3)
#define RTTI_MEMBER_TYPES_4(T, a1, a2, a3, a4) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4)
#define RTTI_MEMBER_TYPES_5(T, a1, a2, a3, a4, a5) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5)
#define RTTI_MEMBER_TYPES_6(T, a1, a2, a3, a4, a5, a6) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6)
#define RTTI_MEMBER_TYPES_7(T, a1, a2, a3, a4, a5, a6, a7) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7)
#define RTTI_MEMBER_TYPES_8(T, a1, a2, a3, a4, a5, a6, a7, a8) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8)
#define RTTI_MEMBER_TYPES_9(T, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8), decltype(&T::a9)
#define RTTI_MEMBER_TYPES_10(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8), decltype(&T::a9), decltype(&T::a10)
#define RTTI_MEMBER_TYPES_11(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8), decltype(&T::a9), decltype(&T::a10), \
    decltype(&T::a11)
#define RTTI_MEMBER_TYPES_12(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8), decltype(&T::a9), decltype(&T::a10), \
    decltype(&T::a11), decltype(&T::a12)
#define RTTI_MEMBER_TYPES_13(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8), decltype(&T::a9), decltype(&T::a10), \
    decltype(&T::a11), decltype(&T::a12), decltype(&T::a13)
#define RTTI_MEMBER_TYPES_14(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8), decltype(&T::a9), decltype(&T::a10), \
    decltype(&T::a11), decltype(&T::a12), decltype(&T::a13), decltype(&T::a14)
#define RTTI_MEMBER_TYPES_15(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8), decltype(&T::a9), decltype(&T::a10), \
    decltype(&T::a11), decltype(&T::a12), decltype(&T::a13), decltype(&T::a14), decltype(&T::a15)
#define RTTI_MEMBER_TYPES_16(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) \
    decltype(&T::a1), decltype(&T::a2), decltype(&T::a3), decltype(&T::a4), decltype(&T::a5), \
    decltype(&T::a6), decltype(&T::a7), decltype(&T::a8), decltype(&T::a9), decltype(&T::a10), \
    decltype(&T::a11), decltype(&T::a12), decltype(&T::a13), decltype(&T::a14), decltype(&T::a15), \
    decltype(&T::a16)

#define RTTI_MEMBER_VALUES_0(T)
#define RTTI_MEMBER_VALUES_1(T, a1) &T::a1
#define RTTI_MEMBER_VALUES_2(T, a1, a2) &T::a1, &T::a2
#define RTTI_MEMBER_VALUES_3(T, a1, a2, a3) &T::a1, &T::a2, &T::a3
#define RTTI_MEMBER_VALUES_4(T, a1, a2, a3, a4) &T::a1, &T::a2, &T::a3, &T::a4
#define RTTI_MEMBER_VALUES_5(T, a1, a2, a3, a4, a5) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5
#define RTTI_MEMBER_VALUES_6(T, a1, a2, a3, a4, a5, a6) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6
#define RTTI_MEMBER_VALUES_7(T, a1, a2, a3, a4, a5, a6, a7) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7
#define RTTI_MEMBER_VALUES_8(T, a1, a2, a3, a4, a5, a6, a7, a8) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8
#define RTTI_MEMBER_VALUES_9(T, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8, &T::a9
#define RTTI_MEMBER_VALUES_10(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8, &T::a9, &T::a10
#define RTTI_MEMBER_VALUES_11(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8, &T::a9, &T::a10, \
    &T::a11
#define RTTI_MEMBER_VALUES_12(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8, &T::a9, &T::a10, \
    &T::a11, &T::a12
#define RTTI_MEMBER_VALUES_13(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8, &T::a9, &T::a10, \
    &T::a11, &T::a12, &T::a13
#define RTTI_MEMBER_VALUES_14(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8, &T::a9, &T::a10, \
    &T::a11, &T::a12, &T::a13, &T::a14
#define RTTI_MEMBER_VALUES_15(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8, &T::a9, &T::a10, \
    &T::a11, &T::a12, &T::a13, &T::a14, &T::a15
#define RTTI_MEMBER_VALUES_16(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) \
    &T::a1, &T::a2, &T::a3, &T::a4, &T::a5, &T::a6, &T::a7, &T::a8, &T::a9, &T::a10, \
    &T::a11, &T::a12, &T::a13, &T::a14, &T::a15, &T::a16

// Empty-aware selector: the empty field list yields the _0 (empty) entries,
// otherwise the per-count joining macro for the real field count.
#define RTTI_MEMBER_TYPES_OPT_SEL_1(T, ...)
#define RTTI_MEMBER_TYPES_OPT_SEL_0(T, ...) \
    RTTI_CAT(RTTI_MEMBER_TYPES_, RTTI_NARG(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_MEMBER_TYPES_OPT(T, ...) \
    RTTI_CAT2(RTTI_MEMBER_TYPES_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_MEMBER_VALUES_OPT_SEL_1(T, ...)
#define RTTI_MEMBER_VALUES_OPT_SEL_0(T, ...) \
    RTTI_CAT(RTTI_MEMBER_VALUES_, RTTI_NARG(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_MEMBER_VALUES_OPT(T, ...) \
    RTTI_CAT2(RTTI_MEMBER_VALUES_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(T, __VA_ARGS__)

// Non-intrusive registration at namespace scope (public members).
// The specialization is wrapped in an rtti namespace block so MSVC accepts
// explicit-specialization-while-inside-another-namespace.
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

// Non-intrusive registration of a derived type (spec 19). `Base`'s reflected
// fields merge into T's field table, base-first, through offset-correct
// D-anchored accessors (virtual/non-virtual inheritance both work), and
// Base's type_id is recorded in T's runtime base chain.
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

// Non-intrusive + default-keep fill behavior at namespace scope.
#define RTTI_DEFINE_TYPE_WITH_DEFAULT(T, ...)                                 \
    RTTI_DEFINE_TYPE(T, __VA_ARGS__)                                          \
    namespace rtti {                                                          \
    template <>                                                               \
    struct has_default<T> : std::true_type {};                                \
    }

// Intrusive registration placed INSIDE the class body. The macro defines a
// nested rtti_registration struct (no template specialization is legal at
// class scope); fields_of inherits it. The friend declaration is not needed
// for the member-pointer accessors (access is checked at &T::f, which is
// inside the class), but documents intent and guards future reach-in.
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

#define RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(T, ...)                       \
    using rtti_has_default = void;                                            \
    RTTI_DEFINE_TYPE_INTRUSIVE(T, __VA_ARGS__)

#define RTTI_ENUM_ENTRY(T, e) ::rtti::enum_label<T>{#e, T::e},

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

// ---------------------------------------------------------------------------
// Template-argument registration macro (spec section 20).
//
// Comma-joining per-count macros (mirroring RTTI_MEMBER_TYPES_N / RTTI_CTOR_
// TYPES_N): MSVC rejects trailing commas in template-argument lists, so the
// argument list is expanded without a trailing comma.
// ---------------------------------------------------------------------------

#define RTTI_TEMPLATE_ARGS_1(a1) a1
#define RTTI_TEMPLATE_ARGS_2(a1, a2) a1, a2
#define RTTI_TEMPLATE_ARGS_3(a1, a2, a3) a1, a2, a3
#define RTTI_TEMPLATE_ARGS_4(a1, a2, a3, a4) a1, a2, a3, a4
#define RTTI_TEMPLATE_ARGS_5(a1, a2, a3, a4, a5) a1, a2, a3, a4, a5
#define RTTI_TEMPLATE_ARGS_6(a1, a2, a3, a4, a5, a6) a1, a2, a3, a4, a5, a6
#define RTTI_TEMPLATE_ARGS_7(a1, a2, a3, a4, a5, a6, a7) a1, a2, a3, a4, a5, a6, a7
#define RTTI_TEMPLATE_ARGS_8(a1, a2, a3, a4, a5, a6, a7, a8) \
    a1, a2, a3, a4, a5, a6, a7, a8
#define RTTI_TEMPLATE_ARGS_9(a1, a2, a3, a4, a5, a6, a7, a8, a9) \
    a1, a2, a3, a4, a5, a6, a7, a8, a9
#define RTTI_TEMPLATE_ARGS_10(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10
#define RTTI_TEMPLATE_ARGS_11(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11
#define RTTI_TEMPLATE_ARGS_12(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12
#define RTTI_TEMPLATE_ARGS_13(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13
#define RTTI_TEMPLATE_ARGS_14(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14
#define RTTI_TEMPLATE_ARGS_15(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15
#define RTTI_TEMPLATE_ARGS_16(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16

// Empty-aware selector: the empty argument list yields no arguments (forming
// T<>, valid for a template with all-defaulted parameters); any non-empty list
// (including one whose wrapped, comma-bearing argument sits at a non-first
// position) is expanded element-wise through the paren stripper, so
// `RTTI_TEMPLATE_INFO(Fn, int, (std::pair<int,int>))` yields
// `Fn<int, std::pair<int,int>>`. The selector base is reused by the
// begin-with-paren guard below (RTTI_TAO_OPT_SEL_0).
#define RTTI_TEMPLATE_ARGS_OPT_SEL_1(T, ...)
#define RTTI_TEMPLATE_ARGS_OPT_SEL_0(T, ...) RTTI_TAO_STRIP(__VA_ARGS__)

// --- Paren-wrapped template arguments (A2 residual) ---
//
// A type-id that itself contains commas (e.g. std::pair<int,int>) is passed as
// a single argument wrapped in an extra pair of parentheses:
//   RTTI_TEMPLATE_INFO(FnPair, (std::pair<int,int>))
// RTTI_IS_EMPTY cannot tell `(...)` from `()`, so the naive empty-aware
// selector above would swallow the wrapped argument as an empty list. The
// selectors below check begin-with-parenthesis FIRST: any argument list that
// starts with '(' is non-empty by definition and forwards every element
// through a conditional one-level parenthesis stripper. Both the empty case
// and ordinary comma lists keep their previous behaviour.

#define RTTI_TAO_UNPAREN_I(...) __VA_ARGS__
#define RTTI_TAO_UNPAREN(...) RTTI_TAO_UNPAREN_I __VA_ARGS__
#define RTTI_TAO_ELEM_II_0(x) x
#define RTTI_TAO_ELEM_II_1(x) RTTI_TAO_UNPAREN(x)
#define RTTI_TAO_ELEM_I(p, x) RTTI_CAT(RTTI_TAO_ELEM_II_, p)(x)
#define RTTI_TAO_ELEM(x) RTTI_TAO_ELEM_I(RTTI_IS_BEGIN_PARENS(x), x)
#define RTTI_TAO_STRIP_1(a1) RTTI_TAO_ELEM(a1)
#define RTTI_TAO_STRIP_2(a1, a2) RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2)
#define RTTI_TAO_STRIP_3(a1, a2, a3) RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3)
#define RTTI_TAO_STRIP_4(a1, a2, a3, a4) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4)
#define RTTI_TAO_STRIP_5(a1, a2, a3, a4, a5) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5)
#define RTTI_TAO_STRIP_6(a1, a2, a3, a4, a5, a6) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6)
#define RTTI_TAO_STRIP_7(a1, a2, a3, a4, a5, a6, a7) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7)
#define RTTI_TAO_STRIP_8(a1, a2, a3, a4, a5, a6, a7, a8) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8)
#define RTTI_TAO_STRIP_9(a1, a2, a3, a4, a5, a6, a7, a8, a9) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8), \
        RTTI_TAO_ELEM(a9)
#define RTTI_TAO_STRIP_10(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8), \
        RTTI_TAO_ELEM(a9), RTTI_TAO_ELEM(a10)
#define RTTI_TAO_STRIP_11(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8), \
        RTTI_TAO_ELEM(a9), RTTI_TAO_ELEM(a10), RTTI_TAO_ELEM(a11)
#define RTTI_TAO_STRIP_12(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8), \
        RTTI_TAO_ELEM(a9), RTTI_TAO_ELEM(a10), RTTI_TAO_ELEM(a11), RTTI_TAO_ELEM(a12)
#define RTTI_TAO_STRIP_13(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8), \
        RTTI_TAO_ELEM(a9), RTTI_TAO_ELEM(a10), RTTI_TAO_ELEM(a11), RTTI_TAO_ELEM(a12), \
        RTTI_TAO_ELEM(a13)
#define RTTI_TAO_STRIP_14(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8), \
        RTTI_TAO_ELEM(a9), RTTI_TAO_ELEM(a10), RTTI_TAO_ELEM(a11), RTTI_TAO_ELEM(a12), \
        RTTI_TAO_ELEM(a13), RTTI_TAO_ELEM(a14)
#define RTTI_TAO_STRIP_15(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8), \
        RTTI_TAO_ELEM(a9), RTTI_TAO_ELEM(a10), RTTI_TAO_ELEM(a11), RTTI_TAO_ELEM(a12), \
        RTTI_TAO_ELEM(a13), RTTI_TAO_ELEM(a14), RTTI_TAO_ELEM(a15)
#define RTTI_TAO_STRIP_16(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) \
    RTTI_TAO_ELEM(a1), RTTI_TAO_ELEM(a2), RTTI_TAO_ELEM(a3), RTTI_TAO_ELEM(a4), \
        RTTI_TAO_ELEM(a5), RTTI_TAO_ELEM(a6), RTTI_TAO_ELEM(a7), RTTI_TAO_ELEM(a8), \
        RTTI_TAO_ELEM(a9), RTTI_TAO_ELEM(a10), RTTI_TAO_ELEM(a11), RTTI_TAO_ELEM(a12), \
        RTTI_TAO_ELEM(a13), RTTI_TAO_ELEM(a14), RTTI_TAO_ELEM(a15), RTTI_TAO_ELEM(a16)
#define RTTI_TAO_STRIP(...) \
    RTTI_CAT(RTTI_TAO_STRIP_, RTTI_NARG(__VA_ARGS__))(__VA_ARGS__)

// Template-argument selector with the paren-wrapped-argument guard.
#define RTTI_TAO_OPT_SEL_0(T, ...) \
    RTTI_CAT2(RTTI_TEMPLATE_ARGS_OPT_SEL_, RTTI_IS_EMPTY(__VA_ARGS__))(T, __VA_ARGS__)
#define RTTI_TAO_OPT_SEL_1(T, ...) RTTI_TAO_STRIP(__VA_ARGS__)
#define RTTI_TEMPLATE_ARGS_OPT(T, ...) \
    RTTI_CAT(RTTI_TAO_OPT_SEL_, RTTI_IS_BEGIN_PARENS(__VA_ARGS__))(T, __VA_ARGS__)

// Argument count mirroring the selector: begin-with-paren counts as a non-empty
// list so wrapped singles report 1 instead of the empty-table 0.
#define RTTI_TAO_COUNT_SEL_0(...) \
    (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__))
#define RTTI_TAO_COUNT_SEL_1(...) RTTI_NARG(__VA_ARGS__)
#define RTTI_TEMPLATE_ARGS_COUNT(...) \
    RTTI_CAT(RTTI_TAO_COUNT_SEL_, RTTI_IS_BEGIN_PARENS(__VA_ARGS__))(__VA_ARGS__)

// Non-intrusive registration of a template instance's argument list at
// namespace scope. T names the class template; the remaining arguments are its
// type arguments. Example:
//   RTTI_TEMPLATE_INFO(MyBox, int, double, std::string)
// specializes template_info<MyBox<int, double, std::string>> with an std::tuple
// of the argument types and their count. An empty argument list (the template
// has all-defaulted parameters and registers as T<>) is allowed and yields
// count 0 with an empty tuple. Individual arguments that themselves contain
// commas (e.g. std::pair<int,int>) must be wrapped in extra parentheses at the
// call site. An unregistered template instance (or non-template type) reports a
// safe empty table: count 0, empty tuple, nullptr type_desc array.
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

// ---------------------------------------------------------------------------
// Attribute registration macros (spec section 17).
//
// Attribute tables are explicit specializations of rtti::attr_table<T> /
// rtti::field_attr_table<T> at namespace scope, so they compose with both the
// intrusive and non-intrusive field-registration macro families without
// touching them. Attributes are optional: a type with no registration simply
// reports count 0 / null pointers (type-safe empty table).
//
// Each RTTI_ATTR(...) expands to a factory call (make_attr) so every comma
// nests inside parentheses; the preprocessor then never mis-counts the
// trailing commas of braced initializer lists.
// ---------------------------------------------------------------------------

// Turn one attribute-argument expression into an attr_arg factory call.
#define RTTI_ATTR_ARG_MAKE(_, a) ::rtti::detail::make_attr_arg(a),
#define RTTI_ATTR_ARGS(...) RTTI_APPLY_OPT(RTTI_ATTR_ARG_MAKE, _, __VA_ARGS__)

// One attribute value: { name, { args... } }.
#define RTTI_ATTR(name, ...) ::rtti::make_attr(name, {RTTI_ATTR_ARGS(__VA_ARGS__)})

#define RTTI_ATTR_REF(_, a) a,
#define RTTI_ATTR_LIST(...) RTTI_APPLY_OPT(RTTI_ATTR_REF, _, __VA_ARGS__)

// One field's attribute list: { "field", { attrs... } }.
#define RTTI_FIELD_ATTR_ENTRY(f, ...) \
    ::rtti::make_field_attr(#f, {RTTI_ATTR_LIST(__VA_ARGS__)})

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

// ---------------------------------------------------------------------------
// Constructor registry macro (spec section 18).
// One parenthesized (name, type, optional) triple per constructor parameter.
// The macro counts the parameters, builds the descriptor array, the parameter
// type list (std::tuple) and wires the generic runtime factory, mirroring the
// explicit proto schema specialization style. Template-type parameters such as
// std::map<std::string, int> are fine because the comma lives inside the
// parentheses.
// ---------------------------------------------------------------------------

#define RTTI_CTOR_GET_1(a, b, c) a
#define RTTI_CTOR_GET_2(a, b, c) b
#define RTTI_CTOR_GET_3(a, b, c) c
#define RTTI_CTOR_NAME(triple) RTTI_CTOR_GET_1 triple
#define RTTI_CTOR_TYPE(triple) RTTI_CTOR_GET_2 triple
#define RTTI_CTOR_OPTIONAL(triple) RTTI_CTOR_GET_3 triple

#define RTTI_CTOR_PARAM_DECL(T, triple)                                       \
    {RTTI_CTOR_NAME(triple), &typeid(RTTI_CTOR_TYPE(triple)),                 \
     RTTI_CTOR_OPTIONAL(triple), {}},

// Parameter type lists joined with commas (no trailing comma: MSVC rejects a
// trailing comma inside a template-argument-list).
#define RTTI_CTOR_TYPES_0(T)
#define RTTI_CTOR_TYPES_1(T, a1) RTTI_CTOR_TYPE(a1)
#define RTTI_CTOR_TYPES_2(T, a1, a2) RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2)
#define RTTI_CTOR_TYPES_3(T, a1, a2, a3) RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3)
#define RTTI_CTOR_TYPES_4(T, a1, a2, a3, a4) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4)
#define RTTI_CTOR_TYPES_5(T, a1, a2, a3, a4, a5) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5)
#define RTTI_CTOR_TYPES_6(T, a1, a2, a3, a4, a5, a6) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6)
#define RTTI_CTOR_TYPES_7(T, a1, a2, a3, a4, a5, a6, a7) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7)
#define RTTI_CTOR_TYPES_8(T, a1, a2, a3, a4, a5, a6, a7, a8) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8)
#define RTTI_CTOR_TYPES_9(T, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8), \
    RTTI_CTOR_TYPE(a9)
#define RTTI_CTOR_TYPES_10(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8), \
    RTTI_CTOR_TYPE(a9), RTTI_CTOR_TYPE(a10)
#define RTTI_CTOR_TYPES_11(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8), \
    RTTI_CTOR_TYPE(a9), RTTI_CTOR_TYPE(a10), RTTI_CTOR_TYPE(a11)
#define RTTI_CTOR_TYPES_12(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8), \
    RTTI_CTOR_TYPE(a9), RTTI_CTOR_TYPE(a10), RTTI_CTOR_TYPE(a11), RTTI_CTOR_TYPE(a12)
#define RTTI_CTOR_TYPES_13(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8), \
    RTTI_CTOR_TYPE(a9), RTTI_CTOR_TYPE(a10), RTTI_CTOR_TYPE(a11), RTTI_CTOR_TYPE(a12), \
    RTTI_CTOR_TYPE(a13)
#define RTTI_CTOR_TYPES_14(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8), \
    RTTI_CTOR_TYPE(a9), RTTI_CTOR_TYPE(a10), RTTI_CTOR_TYPE(a11), RTTI_CTOR_TYPE(a12), \
    RTTI_CTOR_TYPE(a13), RTTI_CTOR_TYPE(a14)
#define RTTI_CTOR_TYPES_15(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8), \
    RTTI_CTOR_TYPE(a9), RTTI_CTOR_TYPE(a10), RTTI_CTOR_TYPE(a11), RTTI_CTOR_TYPE(a12), \
    RTTI_CTOR_TYPE(a13), RTTI_CTOR_TYPE(a14), RTTI_CTOR_TYPE(a15)
#define RTTI_CTOR_TYPES_16(T, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) \
    RTTI_CTOR_TYPE(a1), RTTI_CTOR_TYPE(a2), RTTI_CTOR_TYPE(a3), RTTI_CTOR_TYPE(a4), \
    RTTI_CTOR_TYPE(a5), RTTI_CTOR_TYPE(a6), RTTI_CTOR_TYPE(a7), RTTI_CTOR_TYPE(a8), \
    RTTI_CTOR_TYPE(a9), RTTI_CTOR_TYPE(a10), RTTI_CTOR_TYPE(a11), RTTI_CTOR_TYPE(a12), \
    RTTI_CTOR_TYPE(a13), RTTI_CTOR_TYPE(a14), RTTI_CTOR_TYPE(a15), RTTI_CTOR_TYPE(a16)

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

// ---------------------------------------------------------------------------
// Method registration macros (spec section 22).
//
// One macro invocation registers ONE method of type T at namespace scope:
//   RTTI_DEFINE_METHOD(T, MethodName)          -> member method
//   RTTI_DEFINE_STATIC_METHOD(T, MethodName)   -> static method
// Each links a method_entry into T's table (method_link) and into the
// program-wide table used by the type-erased instance path (method_global_link).
// The registration struct is unique per (method, line) so several methods of
// one type, and same-named methods of different types on different lines,
// coexist in one TU. The struct is defined inside an anonymous namespace, so
// the same macro in a header used by many TUs links one entry per TU. The
// per-method call function is a stateless type-erased entry point forwarding
// to member_invoke / static_invoke; the name string and member pointer are
// captured by the registration (method_entry is built at static-init).
// ---------------------------------------------------------------------------

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

// Tail include of the conversion layer. The reflect core's inline accessors
// (typed_field_access::get_value/get_value_named and the container for_each
// paths) call the format-agnostic converters (to_value / from_value /
// member_to_value / member_to_value_named), whose inline definitions live in
// convert.hpp. A TU that includes only this header (or reflect.hpp)
// instantiates those templates without the definitions in sight, which leaves
// undefined references at link time (LNK2019). Pushing the include to the end
// of the file breaks the would-be cycle (convert.hpp includes reflection.hpp
// at its top, so the nested include is suppressed by #pragma once) while still
// making every reflect-core TU see the converter definitions.
#include <rtti/convert.hpp>
