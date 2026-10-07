#pragma once
// INI format adapter for RTTI.
// Implements rtti::value_io over a small owned tree so the reflect-core
// converters (from_value/to_value) can read and write INI documents through
// the standard sink/source interface.
//
// Feature summary
//   * Sections: `[name]`, dotted nested names (`[a.b]` becomes an object with
//     a nested object under key "b"). Section names may contain blanks (trimmed).
//   * Keys: `key = value` and `key: value` (the first '=' wins over ':').
//   * Comments: whole lines starting with ';' or '#' are preserved (optional
//     on serialization via write_config::with_comments). Inline comments are
//     NOT recognized; an unquoted value keeps everything after the separator.
//   * Quoted values (and keys): "..." and '...' with escapes \" ' and \\.
//     Unquoted values are trimmed; the rest is literal.
//   * Storage: the document owns every scalar text, key and comment (copied
//     on parse), so documents are self-contained and safely movable/returnable.
//   * Duplicate keys: the value becomes an array slot holding every
//     occurrence; scalar reads (as_string/as_integer/...) return the LAST
//     value (later-wins). Sequences (vector<int>, set, ...) read all values.
//   * Duplicate sections: the slot becomes an ARRAY of section objects, one
//     per occurrence (this is the mapping required for std::vector<T> list
//     fields). Keys do not merge across occurrences.
//   * Repeated sections with nested paths attach sub-sections to the LAST
//     element of the enclosing repeated section, so a list of structs that
//     contain struct fields round-trips.
//   * value_io mapping: document <-> object; key -> scalar; single section ->
//     object under its (dotted) name; repeated section -> array of objects;
//     scalar sequence/set -> array slot via repeated keys; map/struct fields
//     -> sections; nested structs -> nested (dotted) sections.
//   * Enums registered with RTTI_DEFINE_ENUM convert to/from their label text
//     through a converter partial specialization provided here (the reflect
//     core has no enum type yet).
//
// Known limits (documented)
//   * A single-element sequence (vector<int> {7} or vector<T> {x}) cannot
//     round-trip: serialization emits one key/section and parsing sees a
//     scalar/object slot, not an array. Lists of two or more elements work.
//   * An EMPTY sequence/set (vector<int> {}, set<string> {}) emits nothing
//     (INI has no empty-list literal), so a strict reflected type reports
//     missing_key for it; maps serialize as an empty section and round-trip.
//   * Empty or blank-only input is an error (ini_bad_line).
//   * Strings containing literal newlines do not round-trip.
//
// English comments only; C++17; no third-party dependencies.
// This header never includes a reflect-core header back into a format header:
// the core headers do not include this file.

#include <rtti/rtti_fwd.hpp>
#if !defined(RTTI_ENABLE_INI) || !RTTI_ENABLE_INI
#  error "RTTI: INI format adapter not enabled (compile with -DRTTI_ENABLE_INI=1)"
#endif

#include <rtti/convert.hpp>
#include <rtti/error.hpp>
#include <rtti/naming.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace rtti::ini {

// Dotted section-name component cap, aligned with the JSON/XML/TOML parse-side
// max_depth (512). Deeper section headers return error_code::too_deep so a
// hostile document cannot build a tree deep enough to overflow serialize().
inline constexpr std::size_t kMaxSectionDepth = 512;

// Re-exported so ci.key_case = rtti::ini::key_case::snake reads naturally.
using rtti::detail::key_case;

struct write_config {
    bool with_comments = false;  // emit comments preserved during parsing
    // Reflected-field key naming (Task D1/D2). Defaults keep existing call
    // sites unchanged. New fields are appended AFTER the original members so
    // legacy positional aggregate initialization (e.g. write_config{true})
    // keeps mapping with_comments exactly as before. INI serialization is
    // line-based (no nesting), so indent_char is accepted for config symmetry
    // but unused by the writer.
    detail::key_case key_case = detail::key_case::identity;
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
    char indent_char = ' ';
};

namespace detail {

class node;
struct pool;

node* pool_alloc(pool* p);

class node : public rtti::value_io {
public:
    enum class role_t { unset, scalar, object, array };

    struct entry {
        std::string name;
        node* child = nullptr;
    };

    role_t r = role_t::unset;
    rtti::field_value::kind skind = rtti::field_value::kind::none;
    std::string owned;       // scalar text (always owned; no source views)
    std::vector<entry> entries;   // object children (ordered)
    std::vector<node*> elems;     // array elements
    std::vector<std::string> comments;  // whole-line comments
    pool* owner = nullptr;

    explicit node(pool* o) noexcept : owner(o) {}

    bool is_scalar() const noexcept { return r == role_t::scalar; }
    bool is_object() const noexcept { return r == role_t::object; }
    bool is_array() const noexcept { return r == role_t::array; }

    rtti::string_view text() const noexcept {
        return rtti::string_view(owned.data(), owned.size());
    }

    // --- value_io read ------------------------------------------------------

    rtti::value_io::kind type() const noexcept override {
        if (is_object()) {
            return rtti::value_io::kind::object;
        }
        if (is_array()) {
            return rtti::value_io::kind::array;
        }
        switch (skind) {
            case rtti::field_value::kind::boolean:
                return rtti::value_io::kind::boolean;
            case rtti::field_value::kind::integer:
                return rtti::value_io::kind::integer;
            case rtti::field_value::kind::real:
                return rtti::value_io::kind::real;
            case rtti::field_value::kind::string:
                return rtti::value_io::kind::string;
            default:
                return rtti::value_io::kind::null;
        }
    }

    const node* scalar_view() const noexcept {
        if (is_scalar()) {
            return this;
        }
        if (is_array() && !elems.empty() && elems.back()->is_scalar()) {
            return elems.back();  // later-wins for scalar reads on array slots
        }
        return nullptr;
    }

    bool as_bool(bool& out) const noexcept override {
        const node* n = scalar_view();
        if (n == nullptr) {
            return false;
        }
        const rtti::string_view s = n->text();
        if (s == "true") {
            out = true;
            return true;
        }
        if (s == "false") {
            out = false;
            return true;
        }
        return false;
    }

    bool as_integer(std::int64_t& out) const noexcept override {
        const node* n = scalar_view();
        if (n == nullptr) {
            return false;
        }
        const std::string t = static_cast<std::string>(n->text());
        if (t.empty()) {
            return false;
        }
        char* end = nullptr;
        errno = 0;
        const long long v = std::strtoll(t.c_str(), &end, 10);
        if (end == t.c_str() || *end != '\0') {
            return false;
        }
        out = static_cast<std::int64_t>(v);
        return true;
    }

    bool as_real(double& out) const noexcept override {
        const node* n = scalar_view();
        if (n == nullptr) {
            return false;
        }
        const std::string t = static_cast<std::string>(n->text());
        if (t.empty()) {
            return false;
        }
        char* end = nullptr;
        errno = 0;
        const double v = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || *end != '\0') {
            return false;
        }
        out = v;
        return true;
    }

    bool as_string(rtti::string_view& out) const noexcept override {
        const node* n = scalar_view();
        if (n == nullptr) {
            return false;
        }
        out = n->text();
        return true;
    }

    std::size_t size() const noexcept override {
        if (is_object()) {
            return entries.size();
        }
        if (is_array()) {
            return elems.size();
        }
        return 0;
    }

    rtti::value_io* at(std::size_t index) override {
        return (is_array() && index < elems.size()) ? elems[index] : nullptr;
    }
    rtti::value_io* at(std::size_t index) const override {
        return (is_array() && index < elems.size()) ? elems[index] : nullptr;
    }

    bool get_key(rtti::string_view key, rtti::value_io*& out) override {
        return get_key_impl(key, out);
    }
    bool get_key(rtti::string_view key, rtti::value_io*& out) const override {
        return get_key_impl(key, out);
    }

    bool get_key_at(std::size_t index, rtti::string_view& key,
                    rtti::value_io*& out) const override {
        if (!is_object() || index >= entries.size()) {
            return false;
        }
        key = rtti::string_view(entries[index].name.data(),
                                entries[index].name.size());
        out = entries[index].child;
        return true;
    }

    // --- value_io write -----------------------------------------------------

    rtti::value_io* make_array() override {
        r = role_t::array;
        skind = rtti::field_value::kind::none;
        owned.clear();
        entries.clear();
        elems.clear();
        return this;
    }

    void push_back(rtti::value_io* v) override {
        elems.push_back(static_cast<node*>(v));
    }

    rtti::value_io* make_object() override {
        r = role_t::object;
        skind = rtti::field_value::kind::none;
        owned.clear();
        entries.clear();
        elems.clear();
        return this;
    }

    void set_key(rtti::string_view name, rtti::value_io* v) override {
        set_key_impl(name, static_cast<node*>(v));
    }

    rtti::value_io* make_scalar(rtti::field_value v) override {
        r = role_t::scalar;
        owned.clear();
        switch (v.type()) {
            case rtti::field_value::kind::boolean:
                skind = rtti::field_value::kind::boolean;
                owned = v.as_bool() ? "true" : "false";
                break;
            case rtti::field_value::kind::integer: {
                skind = rtti::field_value::kind::integer;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(v.as_integer()));
                owned.assign(buf);
                break;
            }
            case rtti::field_value::kind::real: {
                skind = rtti::field_value::kind::real;
                rtti::detail::format_real(v.as_real(), owned);
                break;
            }
            case rtti::field_value::kind::string: {
                skind = rtti::field_value::kind::string;
                owned.assign(v.as_string().data(), v.as_string().size());
                break;
            }
            default:
                skind = rtti::field_value::kind::none;
                break;
        }
        return this;
    }

    rtti::value_io* make_node() override { return detail::pool_alloc(owner); }

    // --- helpers used by the parser and serializer --------------------------

    // Parse a scalar text value into owned storage (parser path).
    void set_scalar_text(rtti::string_view s) {
        r = role_t::scalar;
        skind = rtti::field_value::kind::string;
        owned.assign(s.data(), s.size());
    }

    node* find_child(const std::string& name) {
        for (auto& e : entries) {
            if (e.name == name) {
                return e.child;
            }
        }
        return nullptr;
    }

    // Register a child under `name`. A repeated scalar replaces itself with an
    // array slot holding every occurrence; a repeated array appends.
    void set_key_impl(rtti::string_view name, node* child) {
        for (auto& e : entries) {
            if (e.name == name) {
                if (e.child->is_array()) {
                    e.child->elems.push_back(child);
                    return;
                }
                if (e.child->is_scalar() && child->is_scalar()) {
                    node* arr = detail::pool_alloc(owner);
                    arr->make_array();
                    arr->elems.push_back(e.child);
                    arr->elems.push_back(child);
                    e.child = arr;
                    return;
                }
                e.child = child;  // replace (object/other collision)
                return;
            }
        }
        entries.push_back(entry{std::string(name.data(), name.size()), child});
    }

    void replace_child(const std::string& name, node* child) {
        for (auto& e : entries) {
            if (e.name == name) {
                e.child = child;
                return;
            }
        }
        entries.push_back(entry{name, child});
    }

private:
    bool get_key_impl(rtti::string_view name, rtti::value_io*& out) const {
        if (!is_object()) {
            return false;
        }
        for (const auto& e : entries) {
            if (rtti::string_view(e.name.data(), e.name.size()) == name) {
                out = e.child;
                return true;
            }
        }
        return false;
    }
};

struct pool {
    std::vector<std::unique_ptr<node>> nodes;
};

inline node* pool_alloc(pool* p) {
    p->nodes.push_back(std::unique_ptr<node>(new node(p)));
    return p->nodes.back().get();
}

// --- small text helpers -----------------------------------------------------

inline bool is_ws(char c) noexcept { return c == ' ' || c == '\t'; }

inline rtti::string_view trim_ws(rtti::string_view s) noexcept {
    std::size_t b = 0;
    while (b < s.size() && is_ws(s[b])) {
        ++b;
    }
    std::size_t e = s.size();
    while (e > b && is_ws(s[e - 1])) {
        --e;
    }
    return s.substr(b, e - b);
}

// Decode a quoted string. The opening quote is at s[0]. Returns true and puts
// the decoded text in out on success; sets consumed to the number of bytes
// past the opening quote consumed (includes the closing quote).
inline bool parse_quoted(rtti::string_view s, std::string& out,
                         std::size_t& consumed) noexcept {
    if (s.empty()) {
        return false;
    }
    const char q = s[0];
    if (q != '"' && q != '\'') {
        return false;
    }
    std::size_t i = 1;
    out.clear();
    while (i < s.size()) {
        const char c = s[i];
        if (c == q) {
            if (i + 1 < s.size() && !is_ws(s[i + 1])) {
                return false;  // trailing content after closing quote
            }
            consumed = i + 1;
            return true;
        }
        if (c == '\\' && i + 1 < s.size()) {
            const char n = s[i + 1];
            if (n == '"' || n == '\'' || n == '\\') {
                out.push_back(n);
                i += 2;
                continue;
            }
        }
        out.push_back(c);
        ++i;
    }
    return false;  // unterminated
}

inline bool key_needs_quote(rtti::string_view k) noexcept {
    if (k.empty()) {
        return true;
    }
    const char f = k.front();
    const char l = k.back();
    if (is_ws(f) || is_ws(l) || f == '"' || f == '\'') {
        return true;
    }
    for (const char c : k) {
        if (c == '=' || c == ':') {
            return true;
        }
    }
    return false;
}

inline bool value_needs_quote(rtti::string_view s) noexcept {
    if (s.empty()) {
        return true;
    }
    const char f = s.front();
    const char l = s.back();
    if (is_ws(f) || is_ws(l) || f == '"' || f == '\'') {
        return true;
    }
    for (const char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) {
            return true;
        }
    }
    return false;
}

inline std::string quote_string(rtti::string_view s) {
    std::string out;
    out.push_back('"');
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

inline std::string quote_key(rtti::string_view k) {
    return key_needs_quote(k) ? quote_string(k) : static_cast<std::string>(k);
}

inline std::string quote_value(rtti::string_view s) {
    return value_needs_quote(s) ? quote_string(s) : static_cast<std::string>(s);
}

} // namespace detail

// INI document: an owned tree implementing rtti::value_io. The root is an
// object (the global, section-less area). Move-only; not copyable.
class document : public rtti::value_io {
public:
    document();

    document(document&&) noexcept = default;
    document& operator=(document&&) noexcept = default;
    document(const document&) = delete;
    document& operator=(const document&) = delete;

    ~document() override = default;

    // --- value_io (delegates to the root object) ----------------------------

    rtti::value_io::kind type() const noexcept override { return root_->type(); }

    bool as_bool(bool& out) const noexcept override { return root_->as_bool(out); }
    bool as_integer(std::int64_t& out) const noexcept override {
        return root_->as_integer(out);
    }
    bool as_real(double& out) const noexcept override { return root_->as_real(out); }
    bool as_string(rtti::string_view& out) const noexcept override {
        return root_->as_string(out);
    }

    std::size_t size() const noexcept override { return root_->size(); }
    rtti::value_io* at(std::size_t i) override { return root_->at(i); }
    rtti::value_io* at(std::size_t i) const override { return root_->at(i); }

    bool get_key(rtti::string_view k, rtti::value_io*& out) override {
        return root_->get_key(k, out);
    }
    bool get_key(rtti::string_view k, rtti::value_io*& out) const override {
        return root_->get_key(k, out);
    }
    bool get_key_at(std::size_t i, rtti::string_view& k,
                    rtti::value_io*& out) const override {
        return root_->get_key_at(i, k, out);
    }

    rtti::value_io* make_array() override { return root_->make_array(); }
    void push_back(rtti::value_io* v) override { root_->push_back(v); }
    rtti::value_io* make_object() override { return root_->make_object(); }
    void set_key(rtti::string_view k, rtti::value_io* v) override {
        root_->set_key(k, v);
    }
    rtti::value_io* make_scalar(rtti::field_value v) override {
        return root_->make_scalar(v);
    }
    rtti::value_io* make_node() override { return root_->make_node(); }

private:
    std::unique_ptr<detail::pool> pool_;
    detail::node* root_ = nullptr;

    friend result<document> parse(rtti::string_view src);
    friend std::string serialize(const document& doc,
                                 const write_config& cfg);
    friend void serialize(const document& doc, std::string& out,
                          const write_config& cfg);
};

inline document::document() {
    pool_ = std::make_unique<detail::pool>();
    root_ = detail::pool_alloc(pool_.get());
    root_->make_object();
}

// Open (or extend) the section selected by a dotted path. Repeated final
// components accumulate into an array slot; nested paths under repeated
// sections attach to the last element. `max_depth` caps the number of dotted
// components: a header deeper than the shared parse limit returns nullptr with
// `too_deep` set instead of building an unbounded tree that serialize() would
// later recurse into (stack overflow). On any other failure `too_deep` stays
// false (empty/blank component etc).
inline detail::node* open_section(detail::node* current,
                                  rtti::string_view name,
                                  detail::pool* pool,
                                  std::size_t max_depth,
                                  bool& too_deep) {
    too_deep = false;
    std::size_t depth = 0;
    std::size_t begin = 0;
    while (begin <= name.size()) {
        std::size_t dot = name.find('.', begin);
        const std::size_t end = (dot == rtti::string_view::npos) ? name.size() : dot;
        const rtti::string_view comp =
            detail::trim_ws(name.substr(begin, end - begin));
        const bool last = (dot == rtti::string_view::npos);
        if (comp.empty()) {
            return nullptr;  // empty path component
        }
        ++depth;
        if (depth > max_depth) {
            too_deep = true;
            return nullptr;
        }
        const std::string cname(comp.data(), comp.size());
        detail::node* child = current->find_child(cname);
        if (!last) {
            // Intermediate component: must be (or become) an object; if the
            // existing node is an array, descend into its last element.
            if (child != nullptr && child->is_array()) {
                current = child->elems.back();
                begin = end + 1;
                continue;
            }
            if (child == nullptr || !child->is_object()) {
                detail::node* o = detail::pool_alloc(pool);
                o->make_object();
                current->replace_child(cname, o);
                child = o;
            }
            current = child;
            begin = end + 1;
            continue;
        }
        // Final component: the section object itself.
        if (child == nullptr) {
            detail::node* o = detail::pool_alloc(pool);
            o->make_object();
            current->set_key_impl(comp, o);
            return o;
        }
        if (child->is_object()) {
            // Repeated same-name section: convert to an array of objects.
            detail::node* arr = detail::pool_alloc(pool);
            arr->make_array();
            detail::node* extra = detail::pool_alloc(pool);
            extra->make_object();
            arr->elems.push_back(child);
            arr->elems.push_back(extra);
            current->replace_child(cname, arr);
            return extra;
        }
        if (child->is_array()) {
            detail::node* extra = detail::pool_alloc(pool);
            extra->make_object();
            child->elems.push_back(extra);
            return extra;
        }
        // Scalar collision: section replaces the existing key.
        detail::node* o = detail::pool_alloc(pool);
        o->make_object();
        current->replace_child(cname, o);
        return o;
    }
    return nullptr;
}

// Parse a whole INI document. Scalar text, keys and comments are copied into
// owned storage, so the document owns all of its data and outlives the input.
inline result<document> parse(rtti::string_view src) {
    document doc;

    const char* buf = src.data();
    const std::size_t len = src.size();

    detail::node* current = doc.root_;
    bool any_content = false;  // at least one key, section, or comment

    std::size_t pos = 0;
    while (pos < len) {
        const std::size_t line_start = pos;
        std::size_t line_end = pos;
        while (line_end < len && buf[line_end] != '\n') {
            ++line_end;
        }
        const std::size_t next = (line_end < len) ? line_end + 1 : line_end;
        std::size_t line_len = line_end - line_start;
        if (line_len > 0 && buf[line_start + line_len - 1] == '\r') {
            --line_len;
        }
        const rtti::string_view line(buf + line_start, line_len);

        // Skip leading whitespace.
        std::size_t i = 0;
        while (i < line_len && detail::is_ws(buf[line_start + i])) {
            ++i;
        }

        if (i == line_len) {
            pos = next;  // blank line
            continue;
        }

        const char first = buf[line_start + i];

        if (first == ';' || first == '#') {
            const rtti::string_view ctext = line.substr(i);
            current->comments.push_back(std::string(ctext.data(), ctext.size()));
            any_content = true;
            pos = next;
            continue;
        }

        if (first == '[') {
            // Section header: [name] or ["quoted name"].
            std::size_t j = i + 1;
            rtti::string_view name;
            std::string name_owned;
            std::size_t consumed = 0;
            const rtti::string_view rest = line.substr(j);
            if (!rest.empty() && (rest.front() == '"' || rest.front() == '\'')) {
                if (!detail::parse_quoted(rest, name_owned, consumed)) {
                    result<document> err;
                    err.err.code = error_code::ini_bad_line;
                    err.err.src = src.data();
                    err.err.src_len = src.size();
                    err.err.offset = line_start + j;
                    return err;
                }
                name = rtti::string_view(name_owned.data(), name_owned.size());
                j += consumed;
            } else {
                const std::size_t close = line.find(']', j);
                if (close == rtti::string_view::npos) {
                    result<document> err;
                    err.err.code = error_code::ini_unterminated_section;
                    err.err.src = src.data();
                    err.err.src_len = src.size();
                    err.err.offset = line_start + i;
                    return err;
                }
                name = line.substr(j, close - j);
                j = close + 1;
            }
            while (j < line_len && detail::is_ws(buf[line_start + j])) {
                ++j;
            }
            if (j < line_len) {
                result<document> err;
                err.err.code = error_code::ini_bad_line;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + j;
                return err;
            }
            name = detail::trim_ws(name);
            if (name.empty()) {
                result<document> err;
                err.err.code = error_code::ini_bad_line;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + i;
                return err;
            }
            bool too_deep = false;
            detail::node* sec =
                open_section(doc.root_, name, doc.pool_.get(), kMaxSectionDepth,
                             too_deep);
            if (sec == nullptr) {
                result<document> err;
                err.err.code = too_deep ? error_code::too_deep
                                        : error_code::ini_bad_line;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + i;
                return err;
            }
            current = sec;
            any_content = true;
            pos = next;
            continue;
        }

        // Key/value line. Parse the key (unquoted up to '='/':', or quoted).
        std::string key_owned;
        rtti::string_view key;
        std::size_t sep = 0;
        if (first == '"' || first == '\'') {
            std::size_t consumed = 0;
            const rtti::string_view rest = line.substr(i);
            if (!detail::parse_quoted(rest, key_owned, consumed)) {
                result<document> err;
                err.err.code = error_code::ini_bad_line;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + i;
                return err;
            }
            sep = i + consumed;
            while (sep < line_len && detail::is_ws(buf[line_start + sep])) {
                ++sep;
            }
            if (sep >= line_len || (buf[line_start + sep] != '=' &&
                                    buf[line_start + sep] != ':')) {
                result<document> err;
                err.err.code = error_code::ini_bad_line;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + sep;
                return err;
            }
            key = rtti::string_view(key_owned.data(), key_owned.size());
        } else {
            // Find the first '='; fall back to ':'.
            std::size_t eq = line.find('=', i);
            const std::size_t col = line.find(':', i);
            if (eq != rtti::string_view::npos) {
                sep = eq;
            } else if (col != rtti::string_view::npos) {
                sep = col;
            } else {
                result<document> err;
                err.err.code = error_code::ini_bad_line;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + i;
                return err;
            }
            key = detail::trim_ws(line.substr(i, sep - i));
        }
        if (key.empty()) {
            result<document> err;
            err.err.code = error_code::ini_bad_line;
            err.err.src = src.data();
            err.err.src_len = src.size();
            err.err.offset = line_start + i;
            return err;
        }

        // Parse the value.
        std::size_t vi = sep + 1;
        while (vi < line_len && detail::is_ws(buf[line_start + vi])) {
            ++vi;
        }
        detail::node* value_node = detail::pool_alloc(doc.pool_.get());
        if (vi < line_len &&
            (buf[line_start + vi] == '"' || buf[line_start + vi] == '\'')) {
            std::string decoded;
            std::size_t consumed = 0;
            const rtti::string_view rest = line.substr(vi);
            if (!detail::parse_quoted(rest, decoded, consumed)) {
                result<document> err;
                err.err.code = error_code::ini_bad_line;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + vi;
                return err;
            }
            std::size_t after = vi + consumed;
            while (after < line_len && detail::is_ws(buf[line_start + after])) {
                ++after;
            }
            if (after < line_len) {
                result<document> err;
                err.err.code = error_code::ini_bad_line;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + after;
                return err;
            }
            value_node->make_scalar(rtti::field_value::of(
                rtti::string_view(decoded.data(), decoded.size())));
        } else {
            const rtti::string_view raw = detail::trim_ws(line.substr(vi));
            if (raw.empty()) {
                result<document> err;
                err.err.code = error_code::ini_missing_value;
                err.err.src = src.data();
                err.err.src_len = src.size();
                err.err.offset = line_start + sep + 1;
                return err;
            }
            value_node->set_scalar_text(raw);
        }

        current->set_key_impl(key, value_node);
        any_content = true;
        pos = next;
    }

    if (!any_content) {
        result<document> err;
        err.err.code = error_code::ini_bad_line;
        err.err.src = src.data();
        err.err.src_len = src.size();
        err.err.offset = 0;
        return err;
    }

    result<document> ok;
    ok.value = std::move(doc);
    return ok;
}

// --- streaming (SAX) API ------------------------------------------------------

// Streaming event callbacks (SAX). Returning false cancels the parse early;
// the document is not built in that case and parse_stream returns ok.
// Each callback receives views that are valid only for the duration of the
// call: they point into the source buffer or into a local decoded buffer.
struct parse_events {
    // INI-specific events.
    std::function<bool(rtti::string_view)> section_start;  // section name ("" for global)
    std::function<bool(rtti::string_view)> section_end;    // section name
    std::function<bool(rtti::string_view, rtti::string_view)> key_value;  // name, value
    // JSON/XML-specific events (object_start, cdata, ...) are not used by
    // this format and are intentionally left out.
};

// Parse `src` invoking events without building a tree. Returns ok on a full
// parse or on early cancellation; error_info on a syntax error.
//
// Streaming semantics (documented contract)
//   * The interface is a flat sequence of section headers and key/value
//     documents. Each written "[name]" header fires section_start(name); the
//     previous section fires section_end when a later header appears.
//   * Section-less keys are grouped under an implicit global section reported
//     as section_start("") before the first global key.
//   * Every started section (including the implicit global one) fires a
//     matching section_end at end of input, unless the parse was cancelled.
//   * A dotted header like "[a.b]" is reported as one section named "a.b"
//     (streaming does not reconstruct the tree).
//   * Comments never fire events.
//   * A callback returning false stops the parse immediately (no further
//     events) and parse_stream returns ok.
inline result<void> parse_stream(rtti::string_view src,
                                 const parse_events& events) {
    const char* buf = src.data();
    const std::size_t len = src.size();

    auto fail = [src](error_code code, std::size_t off) {
        result<void> err;
        err.err.code = code;
        err.err.src = src.data();
        err.err.src_len = src.size();
        err.err.offset = off;
        return err;
    };

    std::string cur;       // current section name ("" while in global)
    bool in_section = false;

    auto cur_view = [&cur]() -> rtti::string_view {
        return cur.empty() ? rtti::string_view("")
                           : rtti::string_view(cur.data(), cur.size());
    };
    auto end_current_section = [&]() -> bool {
        if (!in_section) {
            return true;
        }
        if (events.section_end && !events.section_end(cur_view())) {
            return false;  // cancelled
        }
        in_section = false;
        return true;
    };
    auto begin_section = [&](rtti::string_view name) -> bool {
        cur.assign(name.data(), name.size());
        in_section = true;
        if (events.section_start && !events.section_start(name)) {
            return false;  // cancelled
        }
        return true;
    };

    bool any_content = false;  // at least one key, section, or comment

    std::size_t pos = 0;
    while (pos < len) {
        const std::size_t line_start = pos;
        std::size_t line_end = pos;
        while (line_end < len && buf[line_end] != '\n') {
            ++line_end;
        }
        const std::size_t next = (line_end < len) ? line_end + 1 : line_end;
        std::size_t line_len = line_end - line_start;
        if (line_len > 0 && buf[line_start + line_len - 1] == '\r') {
            --line_len;
        }
        const rtti::string_view line(buf + line_start, line_len);

        // Skip leading whitespace.
        std::size_t i = 0;
        while (i < line_len && detail::is_ws(buf[line_start + i])) {
            ++i;
        }

        if (i == line_len) {
            pos = next;  // blank line
            continue;
        }

        const char first = buf[line_start + i];

        if (first == ';' || first == '#') {
            any_content = true;  // comments are not reported as events
            pos = next;
            continue;
        }

        if (first == '[') {
            // Section header: [name] or ["quoted name"].
            std::size_t j = i + 1;
            rtti::string_view name;
            std::string name_owned;
            std::size_t consumed = 0;
            const rtti::string_view rest = line.substr(j);
            if (!rest.empty() && (rest.front() == '"' || rest.front() == '\'')) {
                if (!detail::parse_quoted(rest, name_owned, consumed)) {
                    return fail(error_code::ini_bad_line, line_start + j);
                }
                name = rtti::string_view(name_owned.data(), name_owned.size());
                j += consumed;
            } else {
                const std::size_t close = line.find(']', j);
                if (close == rtti::string_view::npos) {
                    return fail(error_code::ini_unterminated_section,
                                line_start + i);
                }
                name = line.substr(j, close - j);
                j = close + 1;
            }
            while (j < line_len && detail::is_ws(buf[line_start + j])) {
                ++j;
            }
            if (j < line_len) {
                return fail(error_code::ini_bad_line, line_start + j);
            }
            name = detail::trim_ws(name);
            if (name.empty()) {
                return fail(error_code::ini_bad_line, line_start + i);
            }
            if (!end_current_section()) {
                return result<void>{};  // cancelled
            }
            if (!begin_section(name)) {
                return result<void>{};  // cancelled
            }
            any_content = true;
            pos = next;
            continue;
        }

        // Key/value line. Parse the key (unquoted up to '='/':', or quoted).
        std::string key_owned;
        rtti::string_view key;
        std::size_t sep = 0;
        if (first == '"' || first == '\'') {
            std::size_t consumed = 0;
            const rtti::string_view rest = line.substr(i);
            if (!detail::parse_quoted(rest, key_owned, consumed)) {
                return fail(error_code::ini_bad_line, line_start + i);
            }
            sep = i + consumed;
            while (sep < line_len && detail::is_ws(buf[line_start + sep])) {
                ++sep;
            }
            if (sep >= line_len || (buf[line_start + sep] != '=' &&
                                    buf[line_start + sep] != ':')) {
                return fail(error_code::ini_bad_line, line_start + sep);
            }
            key = rtti::string_view(key_owned.data(), key_owned.size());
        } else {
            // Find the first '='; fall back to ':'.
            const std::size_t eq = line.find('=', i);
            const std::size_t col = line.find(':', i);
            if (eq != rtti::string_view::npos) {
                sep = eq;
            } else if (col != rtti::string_view::npos) {
                sep = col;
            } else {
                return fail(error_code::ini_bad_line, line_start + i);
            }
            key = detail::trim_ws(line.substr(i, sep - i));
        }
        if (key.empty()) {
            return fail(error_code::ini_bad_line, line_start + i);
        }

        // Parse the value (decoded when quoted, trimmed otherwise).
        std::string decoded;
        rtti::string_view value;
        std::size_t vi = sep + 1;
        while (vi < line_len && detail::is_ws(buf[line_start + vi])) {
            ++vi;
        }
        if (vi < line_len &&
            (buf[line_start + vi] == '"' || buf[line_start + vi] == '\'')) {
            std::size_t consumed = 0;
            const rtti::string_view rest = line.substr(vi);
            if (!detail::parse_quoted(rest, decoded, consumed)) {
                return fail(error_code::ini_bad_line, line_start + vi);
            }
            std::size_t after = vi + consumed;
            while (after < line_len && detail::is_ws(buf[line_start + after])) {
                ++after;
            }
            if (after < line_len) {
                return fail(error_code::ini_bad_line, line_start + after);
            }
            value = rtti::string_view(decoded.data(), decoded.size());
        } else {
            value = detail::trim_ws(line.substr(vi));
            if (value.empty()) {
                return fail(error_code::ini_missing_value, line_start + sep + 1);
            }
        }

        if (!in_section) {
            // Section-less key: open the implicit global section first.
            if (!begin_section(rtti::string_view(""))) {
                return result<void>{};  // cancelled
            }
        }
        if (events.key_value && !events.key_value(key, value)) {
            return result<void>{};  // cancelled
        }
        any_content = true;
        pos = next;
    }

    if (!end_current_section()) {
        return result<void>{};  // cancelled
    }
    if (!any_content) {
        return fail(error_code::ini_bad_line, 0);
    }
    return result<void>{};
}

// --- serialization ----------------------------------------------------------

namespace detail {

inline void emit_comment(std::string& out, const write_config& cfg,
                         rtti::string_view comment) {
    if (cfg.with_comments) {
        out.append(comment.data(), comment.size());
        out.push_back('\n');
    }
}

inline void write_section_body(std::string& out, const write_config& cfg,
                               const node* sec, const std::string& path);
inline void write_section_header(std::string& out, const write_config& cfg,
                                 const node* sec, rtti::string_view name,
                                 const std::string& parent_path);

inline void write_key_value(std::string& out, rtti::string_view key,
                            const node* n) {
    out += quote_key(key);
    out += " = ";
    out += quote_value(n->text());
    out.push_back('\n');
}

inline void write_section_header(std::string& out, const write_config& cfg,
                                 const node* sec, rtti::string_view name,
                                 const std::string& parent_path) {
    for (const std::string& c : sec->comments) {
        emit_comment(out, cfg, rtti::string_view(c.data(), c.size()));
    }
    std::string full =
        parent_path.empty() ? static_cast<std::string>(name)
                            : (parent_path + "." + std::string(name.data(), name.size()));
    out.push_back('[');
    out += quote_key(name);
    out.append("]\n");
    write_section_body(out, cfg, sec, full);
}

inline void write_section_body(std::string& out, const write_config& cfg,
                               const node* sec, const std::string& path) {
    // Scalars (including scalar arrays as repeated keys) first.
    for (const auto& e : sec->entries) {
        const node* c = e.child;
        if (c->is_scalar() || (!c->is_object() && !c->is_array())) {
            write_key_value(out, rtti::string_view(e.name.data(), e.name.size()), c);
        } else if (c->is_array() && !c->elems.empty() && !c->elems[0]->is_object()) {
            for (const node* el : c->elems) {
                write_key_value(out, rtti::string_view(e.name.data(), e.name.size()), el);
            }
        }
    }
    // Then object sections and arrays of objects (repeated sections).
    for (const auto& e : sec->entries) {
        const node* c = e.child;
        if (c->is_object()) {
            write_section_header(out, cfg, c, rtti::string_view(e.name.data(), e.name.size()), path);
        } else if (c->is_array() && !c->elems.empty() && c->elems[0]->is_object()) {
            for (const node* el : c->elems) {
                write_section_header(out, cfg, el, rtti::string_view(e.name.data(), e.name.size()), path);
            }
        }
    }
}

} // namespace detail

// Serialize into a preallocated user buffer. The buffer's content is replaced
// (not appended): each call leaves `out` holding exactly this document's text,
// identical to what serialize(doc, cfg) returns. Previously reserved capacity
// is kept, so callers can reserve() once and reuse the buffer across many
// serializations without reallocation.
inline void serialize(const document& doc, std::string& out,
                      const write_config& cfg = {}) {
    out.clear();
    const detail::node* root = doc.root_;
    for (const std::string& c : root->comments) {
        detail::emit_comment(out, cfg, rtti::string_view(c.data(), c.size()));
    }
    if (root->is_scalar()) {
        // Scalar root: emit the raw scalar text (no section/key wrapper).
        out += root->text();
        out.push_back('\n');
        return;
    }
    detail::write_section_body(out, cfg, root, std::string());
}

inline std::string serialize(const document& doc, const write_config& cfg = {}) {
    std::string out;
    serialize(doc, out, cfg);
    return out;
}

// Serialize a reflected object through to_value into a temporary document.
// Returns an empty string when conversion fails (error codes are not surfaced
// by this convenience overload). A non-default naming policy is honored on
// the reflected field keys via to_value_named.
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

// Parse then convert with the format-agnostic from_value.
template <typename T>
result<T> parse_as(rtti::string_view src) {
    result<document> d = parse(src);
    result<T> r;
    if (!d.ok()) {
        r.err = d.err;
        return r;
    }
    const error_code e = rtti::from_value(&d.value, r.value);
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = 0;
    }
    return r;
}

// Config-aware parse: output keys written under the same naming policy map
// back to field names through from_value_named.
template <typename T>
result<T> parse_as(rtti::string_view src, const write_config& cfg) {
    result<document> d = parse(src);
    result<T> r;
    if (!d.ok()) {
        r.err = d.err;
        return r;
    }
    error_code e = error_code::ok;
    if (cfg.rename_map == nullptr && cfg.key_case == rtti::detail::key_case::identity &&
        !cfg.skip_empty) {
        e = rtti::from_value(&d.value, r.value);
    } else {
        const rtti::detail::naming_policy np{cfg.key_case, cfg.rename_map,
                                       cfg.skip_empty};
        e = rtti::from_value_named(&d.value, r.value, np);
    }
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = 0;
    }
    return r;
}

} // namespace rtti::ini