// Node-level and reflection-driven string formatting (spec §7.3/§7.4). The
// non-template implementations behind rtti::node_to_string and
// format_reflected<T>:
//   * node_to_string walks any value_io node (a located DOM node from any
//     format adapter) and produces one compact text line; numbers go through
//     the number format spec, everything else uses the compact JSON-ish forms.
//   * reflected_fields_to_string walks a RTTI_DEFINE_TYPE field table, reads
//     every field through the runtime field accessors and renders the object
//     with node_to_string so the spec reaches each numeric field.
// Both are fail-safe: null nodes, walk errors and malformed specs return an
// empty string plus an error_code instead of crashing (spec §0).
//
// English comments only; C++17; no third-party dependencies.

#include <cstddef>
#include <cstdint>
#include <string>

#include <rtti/number_format.hpp>
#include <rtti/to_string.hpp>

namespace rtti {
namespace detail {

namespace {

// Compact quoted string (the same escaping the string layer emits).
inline void append_quoted(const char* data, std::size_t len, std::string& out) {
    rtti::detail::append_text_string(data, len, out);
}

// Recursive node serializer with depth guarding (self-referential trees cannot
// recurse forever). Numbers route through the format spec; other scalar kinds
// and containers use their compact single-line forms.
inline std::string node_to_string_impl(const value_io* node, string_view spec,
                                       error_info* err, std::size_t depth) {
    if (node == nullptr) {
        if (err != nullptr) {
            err->code = error_code::no_such_field;
        }
        return std::string();
    }
    if (depth > 512) {
        if (err != nullptr) {
            err->code = error_code::too_deep;
        }
        return std::string();
    }
    switch (node->type()) {
        case value_io::kind::null:
            return std::string("null");
        case value_io::kind::boolean: {
            bool b = false;
            (void)node->as_bool(b);
            return b ? std::string("true") : std::string("false");
        }
        case value_io::kind::integer: {
            std::int64_t i = 0;
            if (!node->as_integer(i)) {
                return std::string("null");
            }
            std::string t = rtti::detail::format_number_signed(i, spec, err);
            if (t.empty() && !spec.empty()) {
                // A spec valid for reals but not for integers (e.g. ".2f"):
                // fall back to the plain decimal form instead of emitting "".
                // The first (rejected) attempt already recorded its error; the
                // graceful fallback is a success, so its outcome must win
                // ("write err only on failure").
                const std::string d = rtti::detail::format_number_signed(
                    i, string_view(), err);
                if (err != nullptr && !d.empty()) {
                    err->code = error_code::ok;
                }
                return d;
            }
            return t;
        }
        case value_io::kind::real: {
            double d = 0.0;
            if (!node->as_real(d)) {
                return std::string("null");
            }
            std::string t = rtti::detail::format_number_real(d, spec, err);
            if (t.empty() && !spec.empty()) {
                const std::string fallback = rtti::detail::format_number_real(
                    d, string_view(), err);
                if (err != nullptr && !fallback.empty()) {
                    err->code = error_code::ok;
                }
                return fallback;
            }
            return t;
        }
        case value_io::kind::string: {
            string_view sv;
            if (!node->as_string(sv)) {
                return std::string("null");
            }
            std::string out;
            append_quoted(sv.data(), sv.size(), out);
            return out;
        }
        case value_io::kind::array: {
            const std::size_t n = node->size();
            std::string out;
            out.reserve(2 + n * 8);
            out.push_back('[');
            for (std::size_t i = 0; i < n; ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                out += node_to_string_impl(node->at(i), spec, err, depth + 1);
            }
            out.push_back(']');
            return out;
        }
        case value_io::kind::object: {
            const std::size_t n = node->size();
            std::string out;
            out.reserve(4 + n * 16);
            out.push_back('{');
            for (std::size_t i = 0; i < n; ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                string_view key;
                value_io* child = nullptr;
                if (!node->get_key_at(i, key, child) || child == nullptr) {
                    continue;
                }
                append_quoted(key.data(), key.size(), out);
                out.push_back(':');
                out += node_to_string_impl(child, spec, err, depth + 1);
            }
            out.push_back('}');
            return out;
        }
    }
    return std::string("null");
}

} // namespace

std::string reflected_fields_to_string(const field_access_base* const* fields,
                                       std::size_t count, const void* obj,
                                       string_view spec, error_info* err) {
    if (obj == nullptr) {
        if (err != nullptr) {
            err->code = error_code::no_such_field;
        }
        return std::string();
    }
    // Build the object tree through the same value_io sink the converters use,
    // then hand it to node_to_string so nested reflected members and container
    // fields serialize through one code path.
    rtti::detail::text_node root;
    root.make_object();
    if (fields != nullptr) {
        for (std::size_t i = 0; i < count; ++i) {
            const field_access_base* fa = fields[i];
            if (fa == nullptr || fa->name == nullptr) {
                continue;
            }
            value_io* child = root.make_node();
            const error_code e = fa->get_value(obj, child);
            if (e != error_code::ok) {
                if (err != nullptr) {
                    err->code = e;
                }
                return std::string();
            }
            root.set_key(string_view(fa->name), child);
        }
    }
    return node_to_string_impl(&root, spec, err, 0);
}

} // namespace detail

std::string node_to_string(const value_io* node, string_view spec,
                           error_info* err) {
    return rtti::detail::node_to_string_impl(node, spec, err, 0);
}

} // namespace rtti