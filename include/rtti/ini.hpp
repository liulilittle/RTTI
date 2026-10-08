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

    explicit node(pool* o) noexcept;

    bool is_scalar() const noexcept;
    bool is_object() const noexcept;
    bool is_array() const noexcept;

    rtti::string_view text() const noexcept;

    // --- value_io read ------------------------------------------------------

    rtti::value_io::kind type() const noexcept override;

    const node* scalar_view() const noexcept;

    bool as_bool(bool& out) const noexcept override;

    bool as_integer(std::int64_t& out) const noexcept override;

    bool as_real(double& out) const noexcept override;

    bool as_string(rtti::string_view& out) const noexcept override;

    std::size_t size() const noexcept override;

    rtti::value_io* at(std::size_t index) override;
    rtti::value_io* at(std::size_t index) const override;

    bool get_key(rtti::string_view key, rtti::value_io*& out) override;
    bool get_key(rtti::string_view key, rtti::value_io*& out) const override;

    bool get_key_at(std::size_t index, rtti::string_view& key,
                    rtti::value_io*& out) const override;

    // --- value_io write -----------------------------------------------------

    rtti::value_io* make_array() override;

    void push_back(rtti::value_io* v) override;

    rtti::value_io* make_object() override;

    void set_key(rtti::string_view name, rtti::value_io* v) override;

    rtti::value_io* make_scalar(rtti::field_value v) override;

    rtti::value_io* make_node() override;

    // --- helpers used by the parser and serializer --------------------------

    // Parse a scalar text value into owned storage (parser path).
    void set_scalar_text(rtti::string_view s);

    node* find_child(const std::string& name);

    // Register a child under `name`. A repeated scalar replaces itself with an
    // array slot holding every occurrence; a repeated array appends.
    void set_key_impl(rtti::string_view name, node* child);

    void replace_child(const std::string& name, node* child);

private:
    bool get_key_impl(rtti::string_view name, rtti::value_io*& out) const;
};

struct pool {
    std::vector<std::unique_ptr<node>> nodes;
};

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

    ~document() override;

    // --- value_io (delegates to the root object) ----------------------------

    rtti::value_io::kind type() const noexcept override;

    bool as_bool(bool& out) const noexcept override;
    bool as_integer(std::int64_t& out) const noexcept override;
    bool as_real(double& out) const noexcept override;
    bool as_string(rtti::string_view& out) const noexcept override;

    std::size_t size() const noexcept override;
    rtti::value_io* at(std::size_t i) override;
    rtti::value_io* at(std::size_t i) const override;

    bool get_key(rtti::string_view k, rtti::value_io*& out) override;
    bool get_key(rtti::string_view k, rtti::value_io*& out) const override;
    bool get_key_at(std::size_t i, rtti::string_view& k,
                    rtti::value_io*& out) const override;

    rtti::value_io* make_array() override;
    void push_back(rtti::value_io* v) override;
    rtti::value_io* make_object() override;
    void set_key(rtti::string_view k, rtti::value_io* v) override;
    rtti::value_io* make_scalar(rtti::field_value v) override;
    rtti::value_io* make_node() override;

private:
    std::unique_ptr<detail::pool> pool_;
    detail::node* root_ = nullptr;

    friend result<document> parse(rtti::string_view src);
    friend std::string serialize(const document& doc,
                                 const write_config& cfg);
    friend void serialize(const document& doc, std::string& out,
                          const write_config& cfg);
};

// Open (or extend) the section selected by a dotted path. Repeated final
// components accumulate into an array slot; nested paths under repeated
// sections attach to the last element. `max_depth` caps the number of dotted
// components: a header deeper than the shared parse limit returns nullptr with
// `too_deep` set instead of building an unbounded tree that serialize() would
// later recurse into (stack overflow). On any other failure `too_deep` stays
// false (empty/blank component etc).
detail::node* open_section(detail::node* current, rtti::string_view name,
                           detail::pool* pool, std::size_t max_depth,
                           bool& too_deep);

// Parse a whole INI document. Scalar text, keys and comments are copied into
// owned storage, so the document owns all of its data and outlives the input.
result<document> parse(rtti::string_view src);

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
result<void> parse_stream(rtti::string_view src, const parse_events& events);

// --- serialization ----------------------------------------------------------

// Serialize into a preallocated user buffer. The buffer's content is replaced
// (not appended): each call leaves `out` holding exactly this document's text,
// identical to what serialize(doc, cfg) returns. Previously reserved capacity
// is kept, so callers can reserve() once and reuse the buffer across many
// serializations without reallocation.
void serialize(const document& doc, std::string& out,
               const write_config& cfg = {});

std::string serialize(const document& doc, const write_config& cfg = {});

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