// Format-agnostic key naming policy implementation. The non-template
// transformation functions declared by <rtti/naming.hpp> (to_snake_case /
// to_camel_case / apply_naming). Behavior is identical to the former inline
// header definitions.
// English comments only; C++17; no third-party dependencies.

#include <map>
#include <string>

#include <rtti/naming.hpp>

namespace rtti {
namespace detail {

std::string to_snake_case(rtti::string_view s) {
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

std::string to_camel_case(rtti::string_view s) {
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

std::string apply_naming(rtti::string_view field, const naming_policy& np) {
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