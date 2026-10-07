#pragma once
// Format-agnostic key naming policy for RTTI.
//
// Declares the reflected-field key transformation knobs used by the optional
// format adapters (JSON/XML/INI) when they serialize a reflected object
// through to_value_named / parse it back through from_value_named:
//   - key_case: identity (field name as-is), snake_case, or camelCase;
//   - rename_map: an optional field-name -> output-key overrides table that
//     always wins over key_case;
//   - skip_empty: drop fields whose value is empty (empty string, empty
//     container, numeric zero, or null) during serialization.
// The transformations are pure functions (no allocations beyond the returned
// std::string) and carry no dependency on any format adapter.
//
// English comments only; C++17; no third-party dependencies.

#include <map>
#include <string>

#include <rtti/memory.hpp>

namespace rtti {
namespace detail {

// Reflected field name -> output key casing policy.
enum class key_case { identity, snake, camel };

// Convert "fieldName" / "FieldName" to snake_case ("field_name"). An
// uppercase run only gets an underscore before it when the previous byte is a
// lowercase letter, so already-snake names and acronyms stay unchanged.
inline std::string to_snake_case(rtti::string_view s) {
    std::string o;
    o.reserve(s.size() + 4);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0 && s[i - 1] >= 'a' && s[i - 1] <= 'z') {
                o += '_';
            }
            o += static_cast<char>(c - 'A' + 'a');
        } else {
            o += c;
        }
    }
    return o;
}

// Convert "first_name" to camelCase ("firstName"). Each underscore that is
// followed by a lowercase letter promotes that letter to uppercase and is
// dropped; other underscores and non-letter bytes are passed through.
inline std::string to_camel_case(rtti::string_view s) {
    std::string o;
    o.reserve(s.size());
    bool up = false;
    for (const char c : s) {
        if (c == '_') {
            up = true;
            continue;
        }
        if (up && c >= 'a' && c <= 'z') {
            o += static_cast<char>(c - 'a' + 'A');
            up = false;
        } else {
            o += c;
        }
    }
    return o;
}

// Aggregated naming policy carried from a format write_config through the
// convert chain. rename_map is an optional borrowed pointer (nullptr = none);
// the map itself is owned by the caller.
struct naming_policy {
    key_case kc = key_case::identity;
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
};

// Compute the output key for a reflected field name under `np`. A rename_map
// hit wins; otherwise the key_case transformation applies; identity returns
// the field name unchanged (as a fresh std::string).
inline std::string apply_naming(rtti::string_view field, const naming_policy& np) {
    if (np.rename_map != nullptr) {
        const auto it = np.rename_map->find(std::string(field));
        if (it != np.rename_map->end()) {
            return it->second;
        }
    }
    switch (np.kc) {
        case key_case::snake:
            return to_snake_case(field);
        case key_case::camel:
            return to_camel_case(field);
        default:
            return std::string(field);
    }
}

} // namespace detail
} // namespace rtti