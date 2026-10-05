#pragma once
// Format-agnostic conversion for RTTI.
// value_io is the abstract source/sink that future format adapters
// (JSON/XML/INI) will implement. from_value/to_value route scalars, strings,
// and the four container families (vector/list/map/set + unordered twins)
// with transparent allocators/comparators; nested structs and ADL override
// arrive in Task 7.
// English comments only; C++17; no third-party dependencies.

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <rtti/error.hpp>
#include <rtti/reflection.hpp>

namespace rtti {

// value_io (the abstract source/sink tree interface) is defined in
// <rtti/reflection.hpp> so the reflect-core accessors can call into it.
// convert.hpp implements the converters that consume it.

// --- Decomposition traits ---------------------------------------------------
// The container shape traits (is_sequence / is_associative / is_map / is_set /
// is_queue / is_stack / is_priority_queue), the key_traits helper and
// is_reflected are core type features and live in <rtti/reflection.hpp>;
// convert.hpp consumes them through its #include.

// --- Scalar converter -------------------------------------------------------

template <typename T, typename = void>
struct converter {
    static error_code from(const value_io* src, T& out) {
        if constexpr (std::is_same<T, bool>::value) {
            bool v = false;
            if (!src->as_bool(v)) {
                return error_code::kind_mismatch;
            }
            out = v;
            return error_code::ok;
        } else if constexpr (std::is_integral<T>::value) {
            int64_t v = 0;
            if (!src->as_integer(v)) {
                return error_code::kind_mismatch;
            }
            out = static_cast<T>(v);
            return error_code::ok;
        } else if constexpr (std::is_floating_point<T>::value) {
            double v = 0.0;
            if (!src->as_real(v)) {
                return error_code::kind_mismatch;
            }
            out = static_cast<T>(v);
            return error_code::ok;
        } else {
            return error_code::kind_mismatch;
        }
    }

    static error_code to(value_io* dst, const T& in) {
        dst->make_scalar(scalar_of(in));
        return error_code::ok;
    }

private:
    static field_value scalar_of(const T& v) {
        if constexpr (std::is_same<T, bool>::value) {
            return field_value::of(v);
        } else if constexpr (std::is_integral<T>::value) {
            return field_value::of(static_cast<int64_t>(v));
        } else if constexpr (std::is_floating_point<T>::value) {
            return field_value::of(static_cast<double>(v));
        } else {
            return field_value::of_object();
        }
    }
};

template <typename Char, typename Traits, typename Alloc>
struct converter<std::basic_string<Char, Traits, Alloc>> {
    static error_code from(const value_io* src,
                           std::basic_string<Char, Traits, Alloc>& out) {
        string_view v;
        if (!src->as_string(v)) {
            return error_code::kind_mismatch;
        }
        out.assign(v.data(), v.size());
        return error_code::ok;
    }
    static error_code to(value_io* dst,
                         const std::basic_string<Char, Traits, Alloc>& in) {
        dst->make_scalar(
            field_value::of(rtti::string_view(in.data(), in.size())));
        return error_code::ok;
    }
};

// Registered enums (RTTI_DEFINE_ENUM) convert through their label text.
// Specialized here (not in any format adapter header) so every format shares
// one definition and multiple adapters can be included in the same TU.
template <typename T, typename = void>
struct is_registered_enum : std::false_type {};
template <typename T>
struct is_registered_enum<T, std::void_t<decltype(enum_table<T>::count)>>
    : std::true_type {};

template <typename T>
struct converter<T, std::enable_if_t<std::is_enum<T>::value &&
                                     is_registered_enum<T>::value>> {
    static error_code from(const value_io* src, T& out) {
        string_view s;
        if (!src->as_string(s)) {
            return error_code::kind_mismatch;
        }
        for (std::size_t i = 0; i < enum_table<T>::count; ++i) {
            if (string_view(enum_table<T>::array[i].name) == s) {
                out = enum_table<T>::array[i].value;
                return error_code::ok;
            }
        }
        return error_code::kind_mismatch;
    }
    static error_code to(value_io* dst, const T& in) {
        const char* name = enum_to_string(in);
        if (name == nullptr || name[0] == '\0') {
            return error_code::kind_mismatch;
        }
        dst->make_scalar(field_value::of(string_view(name)));
        return error_code::ok;
    }
};

// --- Container converters ---------------------------------------------------

// Sequence family (vector/list/deque): array <-> elements.
template <typename Seq>
error_code sequence_from(const value_io* src, Seq& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Seq::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.push_back(std::move(v));
    }
    return error_code::ok;
}

template <typename Seq>
error_code sequence_to(value_io* dst, const Seq& in) {
    dst->make_array();
    for (const auto& item : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, item);
        if (e != error_code::ok) {
            return e;
        }
        dst->push_back(child);
    }
    return error_code::ok;
}

// Map family: object <-> key/value pairs (key type must be string-convertible).
template <typename Map>
error_code map_from(const value_io* src, Map& out) {
    if (src->type() != value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        string_view key;
        value_io* child = nullptr;
        if (!src->get_key_at(i, key, child) || child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Map::mapped_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        typename Map::key_type k{};
        key_traits<typename Map::key_type>::assign(k, key);
        out.emplace(std::move(k), std::move(v));
    }
    return error_code::ok;
}

template <typename Map>
error_code map_to(value_io* dst, const Map& in) {
    dst->make_object();
    for (const auto& kv : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, kv.second);
        if (e != error_code::ok) {
            return e;
        }
        dst->set_key(key_traits<typename Map::key_type>::view(kv.first), child);
    }
    return error_code::ok;
}

// Set family: array <-> elements (no value_type pair distinction).
template <typename Set>
error_code set_from(const value_io* src, Set& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Set::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.insert(std::move(v));
    }
    return error_code::ok;
}

template <typename Set>
error_code set_to(value_io* dst, const Set& in) {
    dst->make_array();
    for (const auto& item : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, item);
        if (e != error_code::ok) {
            return e;
        }
        dst->push_back(child);
    }
    return error_code::ok;
}

// --- Container-adaptor converters ------------------------------------------
// queue/stack/priority_queue expose no iterators; reading them requires a
// copy, then draining front/top one at a time. The array emitted follows the
// adapter's order semantics; rebuild restores that order.

// queue: FIFO. Array order = front..back; rebuild pushes in the same order.
template <typename Q>
error_code queue_from(const value_io* src, Q& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    std::queue<typename Q::value_type> empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Q::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

template <typename Q>
error_code queue_to(value_io* dst, const Q& in) {
    dst->make_array();
    Q copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, copy.front());
        if (e != error_code::ok) {
            return e;
        }
        dst->push_back(child);
        copy.pop();
    }
    return error_code::ok;
}

// stack: LIFO. Array order = top..bottom; rebuild pushes in reverse order so
// the top stays on top.
template <typename S>
error_code stack_from(const value_io* src, S& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    std::stack<typename S::value_type> empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = n; i-- > 0;) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename S::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

template <typename S>
error_code stack_to(value_io* dst, const S& in) {
    dst->make_array();
    S copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, copy.top());
        if (e != error_code::ok) {
            return e;
        }
        dst->push_back(child);
        copy.pop();
    }
    return error_code::ok;
}

// priority_queue: heap order. Array order = pop order (largest first for the
// default less<int>); rebuild pushes in reverse order so the priority queue
// re-heapifies to the same top.
template <typename PQ>
error_code priority_queue_from(const value_io* src, PQ& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    PQ empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = n; i-- > 0;) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename PQ::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

template <typename PQ>
error_code priority_queue_to(value_io* dst, const PQ& in) {
    dst->make_array();
    PQ copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, copy.top());
        if (e != error_code::ok) {
            return e;
        }
        dst->push_back(child);
        copy.pop();
    }
    return error_code::ok;
}

// --- Reflected-struct walk (field-table path) ------------------------------
// A type registered with RTTI_DEFINE_TYPE is read/written field by field.
// Missing-key behavior depends on has_default<T>: the plain macro returns
// missing_key; the _WITH_DEFAULT variant keeps the member's current value.

template <typename T>
error_code reflect_from(const value_io* src, T& out) {
    if (src->type() != value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    const auto* const* f = fields<T>();
    for (std::size_t i = 0; i < field_count<T>(); ++i) {
        const field_access_base* fa = f[i];
        value_io* sub = nullptr;
        if (!src->get_key(fa->name, sub)) {
            if (has_default<T>::value) {
                continue; // keep existing member value
            }
            return error_code::missing_key;
        }
        // Nested/container/scalar members all recurse via the accessor.
        const error_code e = fa->set_value(static_cast<void*>(&out), sub);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename T>
error_code reflect_to(value_io* dst, const T& in) {
    dst->make_object();
    const auto* const* f = fields<T>();
    for (std::size_t i = 0; i < field_count<T>(); ++i) {
        const field_access_base* fa = f[i];
        value_io* child = dst->make_node();
        const error_code e = fa->get_value(static_cast<const void*>(&in), child);
        if (e != error_code::ok) {
            return e;
        }
        dst->set_key(fa->name, child);
    }
    return error_code::ok;
}

// --- Entry points -----------------------------------------------------------

// Detection: does the type's associated namespace provide an ADL
// from_value/to_value hook with the 3-argument signature (plan contract)?
// The rtti two-argument templates never match the 3-argument detection, so
// ordinary lookup cannot pick them; only a user hook survives overload
// resolution. Users write, in the type's namespace:
//   void from_value(const value_io*, T&, error_code&);
//   void to_value(value_io*, const T&, error_code&);

template <typename T, typename = void>
struct has_adl_from : std::false_type {};
template <typename T>
struct has_adl_from<T, std::void_t<decltype(from_value(std::declval<const value_io*>(),
                                                        std::declval<T&>(),
                                                        std::declval<error_code&>()))>>
    : std::true_type {};

template <typename T, typename = void>
struct has_adl_to : std::false_type {};
template <typename T>
struct has_adl_to<T, std::void_t<decltype(to_value(std::declval<value_io*>(),
                                                    std::declval<const T&>(),
                                                    std::declval<error_code&>()))>>
    : std::true_type {};

// Read `src` into object `out`. Returns an error_code.
template <typename T>
error_code from_value(const value_io* src, T& out) {
    if (src == nullptr) {
        return error_code::no_such_field;
    }
    if constexpr (is_map<T>::value) {
        return map_from(src, out);
    } else if constexpr (is_set<T>::value) {
        return set_from(src, out);
    } else if constexpr (is_priority_queue<T>::value) {
        // Before is_stack: a priority_queue also has push/pop/top.
        return priority_queue_from(src, out);
    } else if constexpr (is_queue<T>::value) {
        return queue_from(src, out);
    } else if constexpr (is_stack<T>::value) {
        return stack_from(src, out);
    } else if constexpr (is_sequence<T>::value) {
        return sequence_from(src, out);
    } else if constexpr (is_reflected<T>::value) {
        return reflect_from(src, out);
    } else if constexpr (has_adl_from<T>::value) {
        error_code e = error_code::ok;
        from_value(src, out, e); // found via ADL in the type's namespace
        return e;
    } else {
        return converter<T>::from(src, out);
    }
}

// Write `in` into sink `dst`.
template <typename T>
error_code to_value(value_io* dst, const T& in) {
    if (dst == nullptr) {
        return error_code::no_such_field;
    }
    if constexpr (is_map<T>::value) {
        return map_to(dst, in);
    } else if constexpr (is_set<T>::value) {
        return set_to(dst, in);
    } else if constexpr (is_priority_queue<T>::value) {
        // Before is_stack: a priority_queue also has push/pop/top.
        return priority_queue_to(dst, in);
    } else if constexpr (is_queue<T>::value) {
        return queue_to(dst, in);
    } else if constexpr (is_stack<T>::value) {
        return stack_to(dst, in);
    } else if constexpr (is_sequence<T>::value) {
        return sequence_to(dst, in);
    } else if constexpr (is_reflected<T>::value) {
        return reflect_to(dst, in);
    } else if constexpr (has_adl_to<T>::value) {
        error_code e = error_code::ok;
        to_value(dst, in, e); // found via ADL in the type's namespace
        return e;
    } else {
        return converter<T>::to(dst, in);
    }
}

} // namespace rtti