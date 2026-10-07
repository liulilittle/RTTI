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
std::string to_snake_case(rtti::string_view s);

// Convert "first_name" to camelCase ("firstName"). Each underscore that is
// followed by a lowercase letter promotes that letter to uppercase and is
// dropped; other underscores and non-letter bytes are passed through.
std::string to_camel_case(rtti::string_view s);

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
std::string apply_naming(rtti::string_view field, const naming_policy& np);

} // namespace detail
} // namespace rtti