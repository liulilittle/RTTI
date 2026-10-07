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
void serialize_node(const node& n, const write_config& cfg, int level,
                    std::string& out);
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
    void adopt_root(detail::node* root) noexcept { root_ = root; }

private:
    friend struct detail::parser;
    detail::node* new_node();
    void destroy_nodes() noexcept;

    std::size_t max_depth_ = 512;
    rtti::memory_arena arena_;                          // string pool
    std::vector<detail::node*> owned_;                  // node pool (arena-backed)
    detail::node* root_ = nullptr;
};

namespace detail {

// Concrete DOM node. Implements value_io so children can be handed straight
// to the reflect converters. Scalar payload is a tagged union; string payload
// and object keys point into the owning document's arena (NUL-terminated).
// The document's node pool owns the node objects; parent-child links here are
// non-owning raw pointers.
struct node : public rtti::value_io {
    rtti::value_io::kind k = rtti::value_io::kind::null;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    const char* s = nullptr;                             // string payload (arena)
    std::size_t slen = 0;                                // payload length in bytes
    std::vector<node*> arr;                              // array children
    std::vector<std::pair<const char*, node*>> obj;      // object entries, key in arena
    // Lazy key index for large objects. Keys are string_views over the
    // arena-duplicated key bytes, so they stay valid for the document
    // lifetime; the index is built on first get_key once the object grows
    // past kHashThreshold and stays in sync on set_key. Small objects keep
    // the linear scan (no per-node allocation overhead).
    mutable std::unordered_map<string_view, node*> hash_;
    mutable bool hash_valid_ = false;

    rtti::memory_arena* arena = nullptr;                 // owning document's arena
    std::vector<node*>* owned = nullptr;                 // document's node pool

    node() noexcept = default;

    value_io::kind type() const noexcept override { return k; }

    bool as_bool(bool& out) const noexcept override {
        if (k != rtti::value_io::kind::boolean) {
            return false;
        }
        out = b;
        return true;
    }
    bool as_integer(int64_t& out) const noexcept override {
        if (k != rtti::value_io::kind::integer) {
            return false;
        }
        out = i;
        return true;
    }
    bool as_real(double& out) const noexcept override {
        // Integer nodes demote to real so JSON "42" fills double fields.
        if (k == rtti::value_io::kind::integer) {
            out = static_cast<double>(i);
            return true;
        }
        if (k != rtti::value_io::kind::real) {
            return false;
        }
        out = d;
        return true;
    }
    bool as_string(string_view& out) const noexcept override {
        if (k != rtti::value_io::kind::string) {
            return false;
        }
        out = string_view(s, slen);
        return true;
    }
    std::size_t size() const noexcept override {
        return k == rtti::value_io::kind::array   ? arr.size()
             : k == rtti::value_io::kind::object  ? obj.size()
                                                  : 0;
    }
    value_io* at(std::size_t index) override {
        return (k == rtti::value_io::kind::array && index < arr.size())
                   ? arr[index]
                   : nullptr;
    }
    value_io* at(std::size_t index) const override {
        return (k == rtti::value_io::kind::array && index < arr.size())
                   ? arr[index]
                   : nullptr;
    }
    bool get_key(string_view key, value_io*& out) override {
        return get_key_impl(key, out);
    }
    bool get_key(string_view key, value_io*& out) const override {
        return get_key_impl(key, out);
    }
    bool get_key_at(std::size_t index, string_view& key,
                    value_io*& out) const override {
        if (k != rtti::value_io::kind::object || index >= obj.size()) {
            return false;
        }
        const char* kp = obj[index].first;
        key = string_view(kp, std::char_traits<char>::length(kp));
        out = obj[index].second;
        return true;
    }

    value_io* make_array() override {
        reset();
        k = rtti::value_io::kind::array;
        return this;
    }
    void push_back(value_io* v) override {
        if (k == rtti::value_io::kind::array) {
            arr.push_back(static_cast<node*>(v));
        }
    }
    value_io* make_object() override {
        reset();
        k = rtti::value_io::kind::object;
        return this;
    }
    void set_key(string_view key, value_io* v) override {
        if (k != rtti::value_io::kind::object || arena == nullptr) {
            return;
        }
        const char* kp = arena->dup(key.data(), key.size());
        obj.emplace_back(kp, static_cast<node*>(v));
        if (hash_valid_) {
            // Keep the lazy index in sync so a later get_key stays O(1).
            hash_.emplace(string_view(kp, key.size()), static_cast<node*>(v));
        }
    }
    value_io* make_scalar(field_value v) override {
        reset();
        switch (v.type()) {
            case field_value::kind::boolean:
                k = rtti::value_io::kind::boolean;
                b = v.as_bool();
                break;
            case field_value::kind::integer:
                k = rtti::value_io::kind::integer;
                i = v.as_integer();
                break;
            case field_value::kind::real:
                k = rtti::value_io::kind::real;
                d = v.as_real();
                break;
            case field_value::kind::string:
                if (arena != nullptr) {
                    const string_view sv = v.as_string();
                    s = arena->dup(sv.data(), sv.size());
                    slen = sv.size();
                    k = rtti::value_io::kind::string;
                }
                break;
            default:
                break; // none / object -> null
        }
        return this;
    }
    value_io* make_node() override {
        if (arena == nullptr) {
            return nullptr;
        }
        node* raw = arena->alloc_obj<node>();
        if (raw == nullptr) {
            return nullptr;
        }
        raw->arena = arena;
        raw->owned = owned;
        if (owned != nullptr) {
            owned->push_back(raw);
        }
        return raw;
    }

private:
    void reset() noexcept {
        k = rtti::value_io::kind::null;
        b = false;
        i = 0;
        d = 0.0;
        s = nullptr;
        slen = 0;
        arr.clear();
        obj.clear();
        hash_.clear();
        hash_valid_ = false;
    }
    void build_index() const {
        hash_.reserve(obj.size());
        for (const auto& e : obj) {
            // emplace keeps the first entry on duplicate keys, matching the
            // first-match semantics of the linear scan.
            hash_.emplace(
                string_view(e.first, std::char_traits<char>::length(e.first)),
                e.second);
        }
        hash_valid_ = true;
    }
    bool get_key_impl(string_view key, value_io*& out) const {
        if (k != rtti::value_io::kind::object) {
            return false;
        }
        if (!hash_valid_ && obj.size() > kHashThreshold) {
            build_index();
        }
        if (hash_valid_) {
            const auto it = hash_.find(key);
            if (it != hash_.end()) {
                out = it->second;
                return true;
            }
            return false;
        }
        for (const auto& e : obj) {
            if (string_view(e.first, std::char_traits<char>::length(e.first)) ==
                key) {
                out = e.second;
                return true;
            }
        }
        return false;
    }
    static constexpr std::size_t kHashThreshold = 8;
};

} // namespace detail

// --- document implementation ----------------------------------------------

inline detail::node* document::new_node() {
    detail::node* raw = arena_.alloc_obj<detail::node>();
    if (raw == nullptr) {
        return nullptr;
    }
    raw->arena = &arena_;
    raw->owned = &owned_;
    owned_.push_back(raw);
    return raw;
}

inline document::~document() {
    destroy_nodes();
}

inline void document::destroy_nodes() noexcept {
    // Nodes are carved from arena blocks; the arena reclaims the bytes without
    // running destructors, and the two child vectors (arr/obj) hold heap
    // buffers of their own, so each node gets an explicit destructor call.
    for (detail::node* p : owned_) {
        if (p != nullptr) {
            p->~node();
        }
    }
    owned_.clear();
}

inline document::document(document&& other) noexcept
    : max_depth_(other.max_depth_),
      arena_(std::move(other.arena_)),
      owned_(std::move(other.owned_)),
      root_(other.root_) {
    other.root_ = nullptr;
    // The arena/node blocks move, but node back-pointers must track the new
    // document object so make_node keeps working after a move.
    for (const auto& p : owned_) {
        if (p != nullptr) {
            p->arena = &arena_;
            p->owned = &owned_;
        }
    }
}

inline document& document::operator=(document&& other) noexcept {
    if (this != &other) {
        destroy_nodes();    // teardown the current node pool
        root_ = nullptr;
        max_depth_ = other.max_depth_;
        arena_ = std::move(other.arena_);
        owned_ = std::move(other.owned_);
        root_ = other.root_;
        other.root_ = nullptr;
        for (const auto& p : owned_) {
            if (p != nullptr) {
                p->arena = &arena_;
                p->owned = &owned_;
            }
        }
    }
    return *this;
}

inline value_io::kind document::type() const noexcept {
    return root_ != nullptr ? root_->type() : value_io::kind::null;
}
inline bool document::as_bool(bool& out) const noexcept {
    return root_ != nullptr && root_->as_bool(out);
}
inline bool document::as_integer(int64_t& out) const noexcept {
    return root_ != nullptr && root_->as_integer(out);
}
inline bool document::as_real(double& out) const noexcept {
    return root_ != nullptr && root_->as_real(out);
}
inline bool document::as_string(string_view& out) const noexcept {
    return root_ != nullptr && root_->as_string(out);
}
inline std::size_t document::size() const noexcept {
    return root_ != nullptr ? root_->size() : 0;
}
inline value_io* document::at(std::size_t index) {
    return root_ != nullptr ? root_->at(index) : nullptr;
}
inline value_io* document::at(std::size_t index) const {
    return root_ != nullptr ? root_->at(index) : nullptr;
}
inline bool document::get_key(string_view key, value_io*& out) {
    return root_ != nullptr && root_->get_key(key, out);
}
inline bool document::get_key(string_view key, value_io*& out) const {
    return root_ != nullptr && root_->get_key(key, out);
}
inline bool document::get_key_at(std::size_t index, string_view& key,
                          value_io*& out) const {
    return root_ != nullptr && root_->get_key_at(index, key, out);
}
inline value_io* document::make_array() {
    if (root_ == nullptr) {
        root_ = new_node();
        if (root_ == nullptr) {
            return nullptr;  // arena allocation failed (C-4)
        }
    }
    return root_->make_array();
}
inline void document::push_back(value_io* v) {
    if (root_ != nullptr) {
        root_->push_back(v);
    }
}
inline value_io* document::make_object() {
    if (root_ == nullptr) {
        root_ = new_node();
        if (root_ == nullptr) {
            return nullptr;  // arena allocation failed (C-4)
        }
    }
    return root_->make_object();
}
inline void document::set_key(string_view key, value_io* v) {
    if (root_ != nullptr) {
        root_->set_key(key, v);
    }
}
inline value_io* document::make_scalar(field_value v) {
    if (root_ == nullptr) {
        root_ = new_node();
        if (root_ == nullptr) {
            return nullptr;  // arena allocation failed (C-4)
        }
    }
    return root_->make_scalar(v);
}
inline value_io* document::make_node() { return new_node(); }

inline std::string document::write(const write_config& cfg) const {
    std::string out;
    if (root_ != nullptr) {
        detail::serialize_node(*root_, cfg, 0, out);
    } else {
        out += "null";
    }
    return out;
}

inline void document::write(const write_config& cfg, std::string& out) const {
    if (root_ != nullptr) {
        detail::serialize_node(*root_, cfg, 0, out);
    } else {
        out += "null";
    }
}

// --- parser ----------------------------------------------------------------

namespace detail {

// Recursive-descent parser. On failure, `err` carries the code and byte offset
// (relative to src). Parsed strings are copied into the document's arena so
// the tree outlives the source buffer.
struct parser {
    // Streaming mode is off when `events` is null (plain DOM build) and on
    // when it points at a parse_events set (no document is built). Cancel is
    // signalled through `cancelled`, which parse_stream translates into an ok
    // result so an early exit is not confused with a syntax error.
    static constexpr std::size_t stream_max_depth = 512;
    string_view src;
    std::size_t pos = 0;
    document* doc = nullptr;
    const parse_events* events = nullptr;
    bool cancelled = false;
    node stream_marker; // non-null success sentinel in streaming mode
    error_info err;

    error_info fail(error_code code, std::size_t off) const {
        error_info e;
        e.code = code;
        e.src = src.data();
        e.src_len = src.size();
        e.offset = off;
        return e;
    }

    // Invoke a streaming callback. Returns false (and sets `cancelled`) when
    // the callback asks to stop early; true otherwise (including when no
    // streaming mode is active or the slot is empty, so the DOM build path is
    // never slowed down or altered.
    template <typename Fn, typename... Args>
    bool fire(const Fn& fn, Args&&... args) {
        if (events == nullptr || !fn) {
            return true;
        }
        if (fn(std::forward<Args>(args)...)) {
            return true;
        }
        cancelled = true;
        return false;
    }

    // Bulk whitespace skip. A non-whitespace first byte (the compact-document
    // norm) returns without touching the scanner; real whitespace runs go
    // through scan.hpp, whose SIMD path pays off once they exceed 16 bytes.
    void skip_ws() noexcept {
        const char* const begin = src.data() + pos;
        const char* const end = src.data() + src.size();
        if (begin != end &&
            (*begin == ' ' || *begin == '\t' || *begin == '\n' ||
             *begin == '\r')) {
            pos = static_cast<std::size_t>(
                rtti::detail::skip_whitespace(begin, end) - src.data());
        }
    }

    static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    node* parse_value(std::size_t depth);
    node* parse_object(std::size_t depth);
    node* parse_array(std::size_t depth);
    node* parse_number();
    node* parse_literal();
    node* parse_string_node();
    bool scan_string(string_view& direct, bool& escaped, std::string& decoded);
    bool decode_u_hex(std::string& out);
    void utf8_encode(std::uint32_t cp, std::string& out);
};

inline node* parser::parse_value(std::size_t depth) {
    skip_ws();
    if (pos >= src.size()) {
        err = fail(error_code::unexpected_token, pos);
        return nullptr;
    }
    const char c = src[pos];
    switch (c) {
        case '{':
            return parse_object(depth);
        case '[':
            return parse_array(depth);
        case '"':
            return parse_string_node();
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return parse_number();
        default:
            return parse_literal();
    }
}

inline node* parser::parse_object(std::size_t depth) {
    const std::size_t limit =
        (doc != nullptr) ? doc->max_depth_ : stream_max_depth;
    if (depth >= limit) {
        err = fail(error_code::too_deep, pos);
        return nullptr;
    }
    ++pos; // '{'
    const bool stream = (events != nullptr);
    if (stream && !fire(events->object_start)) {
        return nullptr;
    }
    node* n = stream ? &stream_marker : doc->new_node();
    if (!stream && n == nullptr) {
        // No dedicated out-of-memory code exists in error.hpp; overflow is
        // the closest general resource-limit code for arena exhaustion (C-4).
        err = fail(error_code::overflow, pos);
        return nullptr;
    }
    if (!stream) {
        n->k = value_io::kind::object;
    }
    skip_ws();
    if (pos < src.size() && src[pos] == '}') {
        ++pos;
        if (stream && !fire(events->object_end)) {
            return nullptr;
        }
        return n;
    }
    for (;;) {
        skip_ws();
        if (pos >= src.size() || src[pos] != '"') {
            err = fail(error_code::unexpected_token, pos);
            return nullptr;
        }
        string_view kdirect;
        bool kescap = false;
        std::string kdecoded;
        if (!scan_string(kdirect, kescap, kdecoded)) {
            return nullptr;
        }
        const char* kp = nullptr;
        if (stream) {
            const string_view ksv = kescap ? string_view(kdecoded.data(), kdecoded.size())
                                           : kdirect;
            if (!fire(events->key, ksv)) {
                return nullptr;
            }
        } else {
            kp = kescap ? doc->arena_.dup(kdecoded.data(), kdecoded.size())
                        : doc->arena_.dup(kdirect.data(), kdirect.size());
            if (kp == nullptr) {
                err = fail(error_code::unexpected_token, pos);
                return nullptr;
            }
        }
        skip_ws();
        if (pos >= src.size() || src[pos] != ':') {
            err = fail(error_code::unexpected_token, pos);
            return nullptr;
        }
        ++pos;
        node* child = parse_value(depth + 1);
        if (child == nullptr) {
            return nullptr;
        }
        if (!stream) {
            n->obj.emplace_back(kp, child);
        }
        skip_ws();
        if (pos >= src.size()) {
            err = fail(error_code::unexpected_token, pos);
            return nullptr;
        }
        const char c = src[pos];
        if (c == ',') {
            ++pos;
            continue;
        }
        if (c == '}') {
            ++pos;
            if (stream && !fire(events->object_end)) {
                return nullptr;
            }
            return n;
        }
        err = fail(error_code::unexpected_token, pos);
        return nullptr;
    }
}

inline node* parser::parse_array(std::size_t depth) {
    const std::size_t limit =
        (doc != nullptr) ? doc->max_depth_ : stream_max_depth;
    if (depth >= limit) {
        err = fail(error_code::too_deep, pos);
        return nullptr;
    }
    ++pos; // '['
    const bool stream = (events != nullptr);
    if (stream && !fire(events->array_start)) {
        return nullptr;
    }
    node* n = stream ? &stream_marker : doc->new_node();
    if (!stream && n == nullptr) {
        // No dedicated out-of-memory code exists in error.hpp; overflow is
        // the closest general resource-limit code for arena exhaustion (C-4).
        err = fail(error_code::overflow, pos);
        return nullptr;
    }
    if (!stream) {
        n->k = value_io::kind::array;
    }
    skip_ws();
    if (pos < src.size() && src[pos] == ']') {
        ++pos;
        if (stream && !fire(events->array_end)) {
            return nullptr;
        }
        return n;
    }
    for (;;) {
        skip_ws();
        node* child = parse_value(depth + 1);
        if (child == nullptr) {
            return nullptr;
        }
        if (!stream) {
            n->arr.push_back(child);
        }
        skip_ws();
        if (pos >= src.size()) {
            err = fail(error_code::unexpected_token, pos);
            return nullptr;
        }
        const char c = src[pos];
        if (c == ',') {
            ++pos;
            continue;
        }
        if (c == ']') {
            ++pos;
            if (stream && !fire(events->array_end)) {
                return nullptr;
            }
            return n;
        }
        err = fail(error_code::unexpected_token, pos);
        return nullptr;
    }
}

inline node* parser::parse_literal() {
    if (pos + 4 <= src.size() && src.compare(pos, 4, "true") == 0) {
        pos += 4;
        if (events != nullptr) {
            if (!fire(events->bool_value, true)) {
                return nullptr;
            }
            return &stream_marker;
        }
        node* n = doc->new_node();
        if (n == nullptr) {
            err = fail(error_code::overflow, pos);  // arena exhaustion, see C-4
            return nullptr;
        }
        n->k = value_io::kind::boolean;
        n->b = true;
        return n;
    }
    if (pos + 5 <= src.size() && src.compare(pos, 5, "false") == 0) {
        pos += 5;
        if (events != nullptr) {
            if (!fire(events->bool_value, false)) {
                return nullptr;
            }
            return &stream_marker;
        }
        node* n = doc->new_node();
        if (n == nullptr) {
            err = fail(error_code::overflow, pos);  // arena exhaustion, see C-4
            return nullptr;
        }
        n->k = value_io::kind::boolean;
        n->b = false;
        return n;
    }
    if (pos + 4 <= src.size() && src.compare(pos, 4, "null") == 0) {
        pos += 4;
        if (events != nullptr) {
            if (!fire(events->null_value)) {
                return nullptr;
            }
            return &stream_marker;
        }
        node* n = doc->new_node();
        if (n == nullptr) {
            err = fail(error_code::overflow, pos);  // arena exhaustion, see C-4
            return nullptr;
        }
        n->k = value_io::kind::null;
        return n;
    }
    err = fail(error_code::unexpected_token, pos);
    return nullptr;
}

inline node* parser::parse_string_node() {
    string_view direct;
    bool escaped = false;
    std::string decoded;
    if (!scan_string(direct, escaped, decoded)) {
        return nullptr;
    }
    if (events != nullptr) {
        if (!fire(events->string_value,
                  escaped ? string_view(decoded.data(), decoded.size())
                          : direct)) {
            return nullptr;
        }
        return &stream_marker;
    }
    node* n = doc->new_node();
    if (n == nullptr) {
        err = fail(error_code::overflow, pos);  // arena exhaustion, see C-4
        return nullptr;
    }
    n->k = value_io::kind::string;
    if (!escaped) {
        n->s = doc->arena_.dup(direct.data(), direct.size());
        n->slen = direct.size();
    } else {
        n->s = doc->arena_.dup(decoded.data(), decoded.size());
        n->slen = decoded.size();
    }
    if (n->s == nullptr) {
        err = fail(error_code::unexpected_token, pos);
        return nullptr;
    }
    return n;
}

inline node* parser::parse_number() {
    const std::size_t start = pos;
    // The digit runs below stay as the original byte loops: scan.hpp exposes
    // structural-character/whitespace masks only, not a digit mask, and the
    // number token also needs its exact span for from_chars afterwards.
    if (pos < src.size() && src[pos] == '-') {
        ++pos;
    }
    // Integer part: '0' alone, or [1-9][0-9]*.
    if (pos >= src.size() || !is_digit(src[pos])) {
        err = fail(error_code::invalid_number, start);
        return nullptr;
    }
    if (src[pos] == '0') {
        ++pos;
        if (pos < src.size() && is_digit(src[pos])) {
            err = fail(error_code::invalid_number, start);
            return nullptr;
        }
    } else {
        while (pos < src.size() && is_digit(src[pos])) {
            ++pos;
        }
    }
    bool is_real = false;
    // Fraction part.
    if (pos < src.size() && src[pos] == '.') {
        is_real = true;
        ++pos;
        if (pos >= src.size() || !is_digit(src[pos])) {
            err = fail(error_code::invalid_number, start);
            return nullptr;
        }
        while (pos < src.size() && is_digit(src[pos])) {
            ++pos;
        }
    }
    // Exponent part.
    if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
        is_real = true;
        ++pos;
        if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) {
            ++pos;
        }
        if (pos >= src.size() || !is_digit(src[pos])) {
            err = fail(error_code::invalid_number, start);
            return nullptr;
        }
        while (pos < src.size() && is_digit(src[pos])) {
            ++pos;
        }
    }

    const char* begin = src.data() + start;
    const std::size_t n = pos - start;
    if (events != nullptr) {
        if (!is_real) {
            int64_t v = 0;
            const auto res = std::from_chars(begin, begin + n, v);
            if (res.ec == std::errc()) {
                if (!fire(events->integer_value, v)) {
                    return nullptr;
                }
                return &stream_marker;
            }
            // int64 overflow: demote to a real event.
        }
        double dv = 0.0;
        const auto rd = std::from_chars(begin, begin + n, dv);
        if (rd.ec != std::errc()) {
            err = fail(error_code::invalid_number, start);
            return nullptr;
        }
        if (!fire(events->real_value, dv)) {
            return nullptr;
        }
        return &stream_marker;
    }
    node* nd = doc->new_node();
    if (nd == nullptr) {
        err = fail(error_code::overflow, pos);  // arena exhaustion, see C-4
        return nullptr;
    }
    if (!is_real) {
        int64_t v = 0;
        const auto res = std::from_chars(begin, begin + n, v);
        if (res.ec == std::errc()) {
            nd->k = value_io::kind::integer;
            nd->i = v;
            return nd;
        }
        // int64 overflow: demote to a real node.
    }
    double dv = 0.0;
    const auto rd = std::from_chars(begin, begin + n, dv);
    if (rd.ec != std::errc()) {
        err = fail(error_code::invalid_number, start);
        return nullptr;
    }
    nd->k = value_io::kind::real;
    nd->d = dv;
    return nd;
}

// Consume the quoted string at src[pos] == '"'. On success `direct` points
// into the source when no escapes were present, otherwise `decoded` holds the
// decoded bytes (escaped flag tells the caller which one to use).
//
// The literal stretch between the opening quote (or an escape) and the next
// quote/backslash is located with scan.hpp's find_first_of + json_string_mask
// (SIMD-accelerated on x86); the control-character check and the escape
// decoding stay per-byte because only they need byte-level decisions.
// Interleaved bench_parse measurements on the standard dense 1MB document
// showed no measurable regression from this swap (median 2.1 vs 2.1 MB/s,
// Debug, contested machine), so it is kept; only whitespace skipping shares
// the scanner today.
inline bool parser::scan_string(string_view& direct, bool& escaped,
                         std::string& decoded) {
    const std::size_t open = pos;
    ++pos;
    const std::size_t start = pos;
    const char* body = rtti::detail::find_first_of(
        src.data() + pos, src.data() + src.size(),
        rtti::detail::json_string_mask());
    for (const char* p = src.data() + pos; p < body; ++p) {
        if (static_cast<unsigned char>(*p) < 0x20) {
            err = fail(error_code::unexpected_token,
                       static_cast<std::size_t>(p - src.data()));
            return false;
        }
    }
    pos = static_cast<std::size_t>(body - src.data());
    if (pos >= src.size()) {
        err = fail(error_code::unterminated_string, open);
        return false;
    }
    if (src[pos] != '\\') {
        escaped = false;
        direct = string_view(src.data() + start, pos - start);
        ++pos; // closing quote
        return true;
    }
    escaped = true;
    decoded.append(src.data() + start, pos - start);
    bool closed = false;
    while (pos < src.size() && !closed) {
        const char c = src[pos];
        if (c == '"') {
            ++pos;
            closed = true;
            break;
        }
        if (c != '\\') {
            if (static_cast<unsigned char>(c) < 0x20) {
                err = fail(error_code::unexpected_token, pos);
                return false;
            }
            decoded.push_back(c);
            ++pos;
            continue;
        }
        if (pos + 1 >= src.size()) {
            err = fail(error_code::unterminated_string, open);
            return false;
        }
        const char e = src[pos + 1];
        switch (e) {
            case '"':
            case '\\':
            case '/':
                decoded.push_back(e);
                pos += 2;
                continue;
            case 'b':
                decoded.push_back('\b');
                pos += 2;
                continue;
            case 'f':
                decoded.push_back('\f');
                pos += 2;
                continue;
            case 'n':
                decoded.push_back('\n');
                pos += 2;
                continue;
            case 'r':
                decoded.push_back('\r');
                pos += 2;
                continue;
            case 't':
                decoded.push_back('\t');
                pos += 2;
                continue;
            case 'u':
                pos += 2; // past "\u"
                if (!decode_u_hex(decoded)) {
                    return false;
                }
                continue;
            default:
                err = fail(error_code::unexpected_token, pos);
                return false;
        }
    }
    if (!closed) {
        err = fail(error_code::unterminated_string, open);
        return false;
    }
    return true;
}

// Decode a \uXXXX escape (surrogate pairs supported) at src[pos].
inline bool parser::decode_u_hex(std::string& out) {
    const std::size_t esc = pos - 2; // position of the backslash
    if (pos + 4 > src.size()) {
        err = fail(error_code::invalid_utf8, esc);
        return false;
    }
    std::uint32_t cp = 0;
    for (int i = 0; i < 4; ++i) {
        const char h = src[pos + static_cast<std::size_t>(i)];
        std::uint32_t v = 0;
        if (h >= '0' && h <= '9') {
            v = static_cast<std::uint32_t>(h - '0');
        } else if (h >= 'a' && h <= 'f') {
            v = static_cast<std::uint32_t>(h - 'a' + 10);
        } else if (h >= 'A' && h <= 'F') {
            v = static_cast<std::uint32_t>(h - 'A' + 10);
        } else {
            err = fail(error_code::invalid_utf8, pos + static_cast<std::size_t>(i));
            return false;
        }
        cp = (cp << 4) | v;
    }
    pos += 4;
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        // High surrogate: must be followed by a low-surrogate \u escape.
        if (pos + 6 > src.size() || src[pos] != '\\' || src[pos + 1] != 'u') {
            err = fail(error_code::invalid_utf8, esc);
            return false;
        }
        std::uint32_t lo = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = src[pos + 2 + static_cast<std::size_t>(i)];
            std::uint32_t v = 0;
            if (h >= '0' && h <= '9') {
                v = static_cast<std::uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                v = static_cast<std::uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                v = static_cast<std::uint32_t>(h - 'A' + 10);
            } else {
                err = fail(error_code::invalid_utf8, pos + 2 + static_cast<std::size_t>(i));
                return false;
            }
            lo = (lo << 4) | v;
        }
        if (!(lo >= 0xDC00 && lo <= 0xDFFF)) {
            err = fail(error_code::invalid_utf8, pos);
            return false;
        }
        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
        pos += 6;
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        // Lone low surrogate is invalid.
        err = fail(error_code::invalid_utf8, pos - 6);
        return false;
    }
    utf8_encode(cp, out);
    return true;
}

inline void parser::utf8_encode(std::uint32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

} // namespace detail

// --- serializer ------------------------------------------------------------

namespace detail {

inline void append_string_escaped(const char* data, std::size_t len,
                                  std::string& out) {
    static const char hex[] = "0123456789ABCDEF";
    out.push_back('"');
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0xF]);
                    out.push_back(hex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

inline void append_newline_indent(const write_config& cfg, int level,
                                  std::string& out) {
    if (cfg.indent <= 0) {
        return;
    }
    out.push_back('\n');
    out.append(static_cast<std::size_t>(cfg.indent) *
                   static_cast<std::size_t>(level),
               cfg.indent_char);
}

// Shortest round-tripping real; JSON has no Inf/NaN, so those become null.
inline void append_real(double value, std::string& out) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    rtti::detail::format_real(value, out);
}

inline void serialize_node(const node& n, const write_config& cfg, int level,
                           std::string& out) {
    switch (n.k) {
        case value_io::kind::null:
            out += "null";
            break;
        case value_io::kind::boolean:
            out += n.b ? "true" : "false";
            break;
        case value_io::kind::integer:
            out += std::to_string(n.i);
            break;
        case value_io::kind::real:
            append_real(n.d, out);
            break;
        case value_io::kind::string:
            append_string_escaped(n.s, n.slen, out);
            break;
        case value_io::kind::array: {
            out.push_back('[');
            for (std::size_t i = 0; i < n.arr.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                append_newline_indent(cfg, level + 1, out);
                serialize_node(*n.arr[i], cfg, level + 1, out);
            }
            if (!n.arr.empty()) {
                append_newline_indent(cfg, level, out);
            }
            out.push_back(']');
            break;
        }
        case value_io::kind::object: {
            out.push_back('{');
            const std::size_t count = n.obj.size();
            std::vector<std::size_t> order;
            if (cfg.sort_keys && count > 1) {
                order.resize(count);
                for (std::size_t i = 0; i < count; ++i) {
                    order[i] = i;
                }
                std::sort(order.begin(), order.end(),
                          [&n](std::size_t a, std::size_t b) {
                              return std::strcmp(n.obj[a].first,
                                                 n.obj[b].first) < 0;
                          });
            }
            for (std::size_t k = 0; k < count; ++k) {
                const std::size_t idx = order.empty() ? k : order[k];
                if (k != 0) {
                    out.push_back(',');
                }
                append_newline_indent(cfg, level + 1, out);
                const char* kp = n.obj[idx].first;
                append_string_escaped(kp, std::char_traits<char>::length(kp), out);
                out.push_back(':');
                if (cfg.indent > 0) {
                    out.push_back(' ');
                }
                serialize_node(*n.obj[idx].second, cfg, level + 1, out);
            }
            if (count != 0) {
                append_newline_indent(cfg, level, out);
            }
            out.push_back('}');
            break;
        }
    }
}

} // namespace detail

// Parse `src` into a document. Returns error_info with a byte offset on
// failure.
inline result<document> parse(string_view src) {
    result<document> r;
    document doc;
    detail::parser p;
    p.src = src;
    p.pos = 0;
    p.doc = &doc;
    p.skip_ws();
    if (p.pos >= src.size()) {
        r.err = p.fail(error_code::invalid_root, 0);
        return r;
    }
    detail::node* root = p.parse_value(0);
    if (root == nullptr) {
        r.err = p.err;
        return r;
    }
    p.skip_ws();
    if (p.pos != src.size()) {
        r.err = p.fail(error_code::unexpected_token, p.pos);
        return r;
    }
    doc.adopt_root(root);
    r.value = std::move(doc);
    return r;
}

// Streaming parse: `src` is pulled through `events` without building a tree.
// Returns ok on a full successful parse; a callback returning false cancels
// the parse and also yields ok (an early stop is not an error). Syntax errors
// yield the usual error_info.
inline result<void> parse_stream(string_view src, const parse_events& events) {
    result<void> r;
    detail::parser p;
    p.src = src;
    p.pos = 0;
    p.doc = nullptr;
    p.events = &events;
    p.skip_ws();
    if (p.pos >= src.size()) {
        r.err = p.fail(error_code::invalid_root, 0);
        return r;
    }
    detail::node* root = p.parse_value(0);
    if (p.cancelled) {
        return r;
    }
    if (root == nullptr) {
        r.err = p.err;
        return r;
    }
    p.skip_ws();
    if (p.pos != src.size()) {
        r.err = p.fail(error_code::unexpected_token, p.pos);
        return r;
    }
    return r;
}

// Serialize a document to a string.
inline std::string serialize(const document& doc, const write_config& cfg = {}) {
    return doc.write(cfg);
}

// Serialize a document into a caller-provided buffer, avoiding an intermediate
// std::string. The buffer is cleared first, so repeated calls to the same
// buffer overwrite (never append); no stale trailing bytes survive.
inline void serialize(const document& doc, std::string& out,
               const write_config& cfg = {}) {
    out.clear();
    doc.write(cfg, out);
}

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
