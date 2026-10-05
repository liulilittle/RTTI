#pragma once
// Compile-time field tables + runtime-iterable field access for RTTI.
// A reflected type registers a specialization of rtti::fields_of<T> (usually
// via the RTTI_DEFINE_TYPE macro from Task 5; manual specializations are
// supported too). Fields are read/written through member-pointer accessors,
// never byte offsets: safe for private members and for inheritance.
// English comments only; C++17; no third-party dependencies.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <typeinfo>
#include <utility>

#include <rtti/memory.hpp>

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

// Reflected type registry. A type participates in reflection iff this
// template is specialized (by the RTTI_DEFINE_TYPE macro or by hand).
template <typename T, typename = void>
struct fields_of;

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
// begin/end, so they cannot match.
template <typename T, typename = void>
struct is_fixed_sequence : std::false_type {};
template <typename T>
struct is_fixed_sequence<T, std::enable_if_t<
    !is_char_string<T>::value && !has_push_back<T>::value, std::void_t<
        typename T::value_type,
        decltype(std::declval<T&>().begin()),
        decltype(std::declval<T&>().end())>>> : std::true_type {};

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
        return rtti::to_value(dst, o.*ptr);
    }

    error_code set_value(void* obj, const value_io* src) const override {
        auto& o = *static_cast<T*>(obj);
        return rtti::from_value(src, o.*ptr);
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
                // Numeric set keys: parse the decimal string form.
                v = static_cast<typename M::value_type>(
                    std::strtoll(std::string(key).c_str(), nullptr, 10));
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
    RTTI_IBP_SPLIT(0, RTTI_IBP_CAT(RTTI_IBP_IS_VARIADIC_R_,                     \
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
};

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
        static const type_desc d{type_kind::reflected, &typeid(T), typeid(T).name(), nullptr, &s, nullptr};
        return &d;
    } else if constexpr (std::is_enum<T>::value) {
        static const enum_desc e{enum_table<T>::count, enum_names<T>()};
        static const type_desc d{type_kind::enum_type, &typeid(T), typeid(T).name(), nullptr, nullptr, &e};
        return &d;
    } else {
        // Unknown/unregistered type: report it as reflected of zero fields is
        // wrong; use the container kind only as a safe placeholder.
        static const type_desc d{type_kind::string, &typeid(T), typeid(T).name()};
        return &d;
    }
}

} // namespace detail

} // namespace rtti

#define RTTI_FIELD_DECL(T, f)                                                 \
    static inline const ::rtti::typed_field_access<T, decltype(T::f)> rtti_acc_##f{ \
        #f, &T::f, &typeid(decltype(T::f))};

#define RTTI_FIELD_REF(T, f) &rtti_acc_##f,

#define RTTI_FIELD_TYPE_DESC(T, f) { #f, &::rtti::type_of<decltype(T::f)> },

// Non-intrusive registration at namespace scope (public members).
// The specialization is wrapped in an rtti namespace block so MSVC accepts
// explicit-specialization-while-inside-another-namespace.
#define RTTI_DEFINE_TYPE(T, ...)                                              \
    namespace rtti {                                                          \
    template <>                                                               \
    struct fields_of<T> {                                                     \
        static constexpr std::size_t count =                                   \
            (RTTI_IS_EMPTY(__VA_ARGS__) ? 0 : RTTI_NARG(__VA_ARGS__));         \
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

#define RTTI_DEFINE_ENUM(T, ...)                                              \
    namespace rtti {                                                          \
    template <>                                                               \
    struct enum_table<T> {                                                    \
        static constexpr std::size_t count = RTTI_NARG(__VA_ARGS__);          \
        static inline const enum_label<T> array[(count > 0 ? count : 1)] =    \
            { RTTI_APPLY(RTTI_ENUM_ENTRY, T, __VA_ARGS__) };                  \
    };                                                                          \
    }