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

// Range-for support: for (auto& f : rtti::fields_of<T>::array).
template <typename T>
const field_access_base* const* begin() noexcept {
    return fields<T>();
}
template <typename T>
const field_access_base* const* end() noexcept {
    return fields<T>() + field_count<T>();
}

} // namespace rtti