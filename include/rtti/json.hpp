#pragma once
// JSON format adapter for RTTI.
//
// Provides a self-contained JSON DOM document implementing the format-agnostic
// rtti::value_io source/sink interface, a recursive-descent parser, and a
// serializer. parse_as<T> / serialize<T> bridge the reflect core
// (rtti::from_value / rtti::to_value from <rtti/convert.hpp>) so reflected
// structs, the four container families, queue/stack/priority_queue, registered
// enums and ADL hooks all round-trip through JSON strings.
//
// Notes:
//  - English comments only; C++17; no third-party dependencies; no
//    compiler-specific extensions (compiled with MSVC /Zc:preprocessor).
//  - The reflect core NEVER includes this header. The adapter is opt-in and
//    gated by RTTI_ENABLE_JSON (default 1 in <rtti/rtti_fwd.hpp>); compiling
//    with -DRTTI_ENABLE_JSON=0 and including this header is a hard error.
//    Core translation units that never include this header are unaffected by
//    anything declared here, including the enum converter specialization
//    below (which only becomes visible when this header is included).
//  - Every stored string (node payloads and object keys) is copied into the
//    owning document's arena, so views handed out through the value_io
//    interface stay valid after the parsed source buffer or the caller's
//    temporaries are destroyed.
//  - Numbers: a literal without '.', 'e' or 'E' that fits in int64 becomes an
//    integer node (int64 overflow demotes to a real node); any literal with a
//    fraction or exponent is a real node. as_real() also accepts integer
//    nodes so JSON "42" can fill a double field.

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rtti/rtti_fwd.hpp>

#if !defined(RTTI_ENABLE_JSON) || !RTTI_ENABLE_JSON
#  error "RTTI: JSON format adapter not enabled (compile with -DRTTI_ENABLE_JSON=1)"
#endif

#include <rtti/error.hpp>
#include <rtti/memory.hpp>
#include <rtti/naming.hpp>
#include <rtti/reflection.hpp>
#include <rtti/convert.hpp>
#include <rtti/detail/scan.hpp>

namespace rtti {
namespace json {

// Re-exported so cfg.key_case = rtti::json::key_case::snake reads naturally.
using rtti::detail::key_case;

// DOM write options.
struct write_config {
    int indent = 2;          // 0 = compact single line; >0 = pretty with spaces
    bool sort_keys = false;  // optional deterministic key order
    // Reflected-field key naming (Task D1). Fields default to identity /
    // no rename map / keep-empty so existing call sites are unchanged. All
    // new fields are appended AFTER the original members so legacy positional
    // aggregate initialization (e.g. write_config{0, false}) keeps mapping
    // indent/sort_keys exactly as before.
    char indent_char = ' ';  // indentation fill character (e.g. '\t')
    detail::key_case key_case = detail::key_case::identity;
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
};

// Streaming event callbacks (SAX): a single-pass pull of the document without
// building a tree. Callbacks fire in document order. Returning false from any
// callback cancels the parse early; parse_stream then returns ok (the caller
// saw every event it asked for, and nothing was built). XML/INI-specific time
// events have no JSON equivalent; leave the unused slots empty.
struct parse_events {
    std::function<bool()> object_start;
    std::function<bool()> object_end;
    std::function<bool()> array_start;
    std::function<bool()> array_end;
    std::function<bool(string_view)> key;
    std::function<bool(string_view)> string_value;
    std::function<bool(int64_t)> integer_value;
    std::function<bool(double)> real_value;
    std::function<bool(bool)> bool_value;
    std::function<bool()> null_value;
};

class document;
namespace detail {
struct node;
struct parser;
} // namespace detail

// Parsed (or built) JSON document. Owns its arena (string pool) and its node
// pool and root node; implements rtti::value_io so from_value/to_value and
// the reflect core consume it directly. Movable, non-copyable.
class document : public rtti::value_io {
public:
    document() noexcept = default;
    ~document() override;

    document(document&& other) noexcept;
    document& operator=(document&& other) noexcept;
    document(const document&) = delete;
    document& operator=(const document&) = delete;

    // --- value_io read side (root node) -------------------------------------
    value_io::kind type() const noexcept override;
    bool as_bool(bool& out) const noexcept override;
    bool as_integer(int64_t& out) const noexcept override;
    bool as_real(double& out) const noexcept override;
    bool as_string(string_view& out) const noexcept override;
    std::size_t size() const noexcept override;
    value_io* at(std::size_t index) override;
    value_io* at(std::size_t index) const override;
    bool get_key(string_view key, value_io*& out) override;
    bool get_key(string_view key, value_io*& out) const override;
    bool get_key_at(std::size_t index, string_view& key,
                    value_io*& out) const override;

    // --- value_io write side (root node) ------------------------------------
    value_io* make_array() override;
    void push_back(value_io* v) override;
    value_io* make_object() override;
    void set_key(string_view key, value_io* v) override;
    value_io* make_scalar(field_value v) override;
    value_io* make_node() override;

    // Serialize this document's tree. Public so the free `serialize` functions
    // can call it without the friendship MSVC fails to match for functions.
    std::string write(const write_config& cfg) const;

    // Append this document's serialization directly into `out` (avoids an
    // intermediate string). The caller is expected to clear `out` first; the
    // buffer overload of `serialize` does so for consistent overwrite
    // semantics.
    void write(const write_config& cfg, std::string& out) const;

    // Adopt a freshly parsed root node (used by the parser; the node must
    // already be owned by this document's node pool).
    void adopt_root(detail::node* root) noexcept;

private:
    friend struct detail::parser;
    friend struct detail::node;
    detail::node* new_node();
    void destroy_nodes() noexcept;

    std::size_t max_depth_ = 512;
    rtti::memory_arena arena_;                          // string pool
    std::vector<detail::node*> owned_;                  // node pool (arena-backed)
    detail::node* root_ = nullptr;
};

// Non-template implementations (detail::node, detail::parser, document
// members, serializer helpers, parse/parse_stream/serialize free functions)
// live in src/json.cpp; this header keeps only the public interface
// declarations and the reflect-bridge template shells below.

// Free-function entry points. Default arguments appear only in this header.

result<document> parse(string_view src);
result<void> parse_stream(string_view src, const parse_events& events);

// Serialize a document to a string.
std::string serialize(const document& doc, const write_config& cfg = {});

// Serialize a document into a caller-provided buffer, avoiding an intermediate
// std::string. The buffer is cleared first, so repeated calls to the same
// buffer overwrite (never append); no stale trailing bytes survive.
void serialize(const document& doc, std::string& out,
               const write_config& cfg = {});
// --- reflect bridge --------------------------------------------------------

// parse -> rtti::from_value(root, out)
template <typename T>
result<T> parse_as(string_view src) {
    result<T> r;
    result<document> parsed = parse(src);
    if (!parsed.ok()) {
        r.err = parsed.err;
        return r;
    }
    T out{};
    const error_code e = rtti::from_value(&parsed.value, out);
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = 0;
        return r;
    }
    r.value = std::move(out);
    return r;
}

// rtti::to_value(root, obj) -> serialize. Returns an empty string when the
// conversion fails (e.g. an enum value not present in its label table). When
// the config carries a naming policy (key_case / rename_map / skip_empty),
// the conversion goes through to_value_named so reflected field keys follow
// the policy; with a default config the plain to_value path is used unchanged.
template <typename T>
std::string serialize(const T& obj, const write_config& cfg = {}) {
    document doc;
    error_code e = error_code::ok;
    if (cfg.rename_map == nullptr && cfg.key_case == rtti::detail::key_case::identity &&
        !cfg.skip_empty) {
        e = rtti::to_value(&doc, obj);
    } else {
        const rtti::detail::naming_policy np{cfg.key_case, cfg.rename_map,
                                       cfg.skip_empty};
        e = rtti::to_value_named(&doc, obj, np);
    }
    if (e != error_code::ok) {
        return std::string();
    }
    return serialize(doc, cfg);
}

// Config-aware parse: parse `src`, then convert with from_value_named so
// output keys written under the same naming policy map back to field names.
template <typename T>
result<T> parse_as(string_view src, const write_config& cfg) {
    result<T> r;
    result<document> parsed = parse(src);
    if (!parsed.ok()) {
        r.err = parsed.err;
        return r;
    }
    T out{};
    error_code e = error_code::ok;
    if (cfg.rename_map == nullptr && cfg.key_case == rtti::detail::key_case::identity &&
        !cfg.skip_empty) {
        e = rtti::from_value(&parsed.value, out);
    } else {
        const rtti::detail::naming_policy np{cfg.key_case, cfg.rename_map,
                                       cfg.skip_empty};
        e = rtti::from_value_named(&parsed.value, out, np);
    }
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = 0;
        return r;
    }
    r.value = std::move(out);
    return r;
}

} // namespace json
} // namespace rtti
