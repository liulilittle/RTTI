#pragma once
// TOML format adapter for RTTI (TOML v1.0 core subset).
//
// Mirrors the JSON/XML/INI adapter architecture: an owned DOM (value_io tree)
// for parse/serialize, parse_as<T>/serialize<T> reflect bridging, a streaming
// SAX parse_stream and a RTTI_ENABLE_TOML strip gate (default 1 through
// <rtti/rtti_fwd.hpp>).
//
// Feature summary (documented contract)
//   * key = value statements; dotted keys (a.b.c); quoted keys ("k" / 'k').
//   * Basic tables [a.b]; array tables [[a.b]] mapped to arrays of objects.
//   * Scalars: strings (basic/multi-line/literal), integers (dec/hex/oct/bin
//     with optional underscores), floats (decimals, exponents, inf/nan/+-inf),
//     booleans, and date/time values mapped to canonical strings. Date-time
//     scalars keep their exact source text and round-trip unquoted (text
//     lossless). Integers are normalized to canonical decimal text.
//   * Arrays [1, 2, 3] (trailing comma allowed) with mixed value types,
//     nested arrays and inline tables {k = v, ...}.
//   * Comments: full-line and trailing "# ..." are skipped (not preserved).
//   * Duplicate keys: later-wins (the last occurrence replaces the earlier),
//     matching the spec section 11.3 mapping. Reopening an existing [table]
//     continues it; an existing table converted by [[t]] turns into an array
//     holding the old table first.
//   * value_io mapping: document <-> object; dotted path / section -> nested
//     objects; array table -> array of objects; array value -> array.
//   * Malformed input returns an error_code (allocation failure may throw
//     std::bad_alloc via the node pool; never crashes): unterminated
//     strings/arrays/tables, invalid numbers, empty paths and overly deep
//     value nesting (parse_config::max_depth, defaults to 512; deep nesting
//     yields error_code::too_deep).
//   * Streaming (parse_stream) contract: table_start/table_end fire for [x]
//     and [[x]] blocks; section-less keys are bracketed by an implicit
//     table_start("")/table_end(""). key_value(path, value) reports the raw
//     source slice of a value (quotes/brackets included); typed scalar events
//     (string/integer/real/bool) and array_start/array_end fire while the
//     value is read, before that key's key_value event.
//   * Serialization writes a table's scalars before its sub-tables; the DOM
//     is unchanged, so parse -> serialize -> parse round-trips.
//
// Known limits (documented)
//   * Strict TOML v1.0 corner cases are simplified: dotted-key and table
//     nesting edge rules (e.g. redefining a table as an array table) follow
//     the later-wins / reuse-last conventions above rather than erroring.
//   * Edge quoting (e.g. multi-line strings ending in many quotes) follows
//     the simplified rules in the reader. Comments are not re-emitted.
//
// English comments only; C++17; no third-party dependencies. This header is
// never included by a reflect-core header, so the core stays adapter-free.

#include <rtti/rtti_fwd.hpp>
#if !defined(RTTI_ENABLE_TOML) || !RTTI_ENABLE_TOML
#  error "RTTI: TOML format adapter not enabled (compile with -DRTTI_ENABLE_TOML=1)"
#endif

#include <rtti/convert.hpp>
#include <rtti/error.hpp>
#include <rtti/naming.hpp>

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rtti {
namespace toml {

// Re-exported so ct.key_case = rtti::toml::key_case::snake reads naturally.
using rtti::detail::key_case;

struct write_config {
    detail::key_case key_case = detail::key_case::identity;
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
    char indent_char = ' ';  // accepted for config symmetry; line-based (unused)
};

struct parse_config {
    std::size_t max_depth = 512;  // array/inline-table nesting limit (too_deep)
};

// Streaming event callbacks (SAX). Returning false cancels the parse early;
// the document is not built in that case and parse_stream returns ok. Only
// non-empty (assigned) callbacks are invoked. See the header comment for the
// event ordering and value-text contract.
struct parse_events {
    std::function<bool(string_view)> table_start;       // [path] begins
    std::function<bool(string_view)> table_end;         // [path] ends
    std::function<bool(string_view)> array_table_start; // [[path]] begins
    std::function<bool(string_view)> array_table_end;   // [[path]] ends
    std::function<bool(string_view, string_view)> key_value;  // path, value text
    std::function<bool()> array_start;                  // a value array begins
    std::function<bool()> array_end;                    // a value array ends
    std::function<bool(string_view)> string_value;
    std::function<bool(std::int64_t)> integer_value;
    std::function<bool(double)> real_value;
    std::function<bool(bool)> bool_value;
};

namespace detail {

class node;
struct pool;
struct parser;

class node : public rtti::value_io {
public:
    enum class role_t { unset, scalar, object, array };

    struct entry {
        std::string name;
        node* child = nullptr;
    };

    role_t r = role_t::unset;
    rtti::field_value::kind skind = rtti::field_value::kind::none;
    std::string owned;          // scalar text (all scalar payloads copied here)
    bool is_datetime = false;   // parsed date/time kept unquoted on output
    std::vector<entry> entries; // object children (ordered)
    std::vector<node*> elems;   // array elements
    pool* owner = nullptr;

    explicit node(pool* o) noexcept;

    bool is_scalar() const noexcept;
    bool is_object() const noexcept;
    bool is_array() const noexcept;

    rtti::string_view text() const noexcept;

    node* find_child(const std::string& name);
    void set_child(const std::string& name, node* child);

    // --- value_io read ------------------------------------------------------

    rtti::value_io::kind type() const noexcept override;
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

    // Parser path: store an already-classified scalar.
    void set_scalar_parsed(rtti::field_value::kind k, bool dt,
                           rtti::string_view s);

private:
    bool get_key_impl(rtti::string_view name, rtti::value_io*& out) const;
};

struct pool {
    std::vector<std::unique_ptr<node>> nodes;
};

} // namespace detail

// TOML document: an owned tree implementing rtti::value_io. The root is always
// an object (the global, table-less area). Move-only; not copyable.
class document : public rtti::value_io {
public:
    document();

    document(document&&) noexcept;
    document& operator=(document&&) noexcept;
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

    friend struct detail::parser;
    friend result<document> parse(rtti::string_view src, const parse_config& cfg);
    friend result<void> parse_stream(rtti::string_view src,
                                     const parse_events& events);
    friend std::string serialize(const document& doc, const write_config& cfg);
    friend void serialize(const document& doc, std::string& out,
                          const write_config& cfg);
};

// Parse `src` into a document. Returns error_info with a byte offset on
// failure.
result<document> parse(string_view src, const parse_config& cfg);
result<document> parse(string_view src);

// Streaming parse: `src` is pulled through `events` without building a tree.
// Returns ok on a full successful parse; a callback returning false cancels
// the parse and also yields ok (an early stop is not an error). Syntax errors
// yield the usual error_info.
result<void> parse_stream(string_view src, const parse_events& events);

// Serialize into a preallocated user buffer. The buffer's content is replaced
// (not appended): each call leaves `out` holding exactly this document's text,
// identical to what serialize(doc, cfg) returns. Previously reserved capacity
// is kept, so callers can reserve() once and reuse the buffer.
void serialize(const document& doc, std::string& out,
               const write_config& cfg = {});

std::string serialize(const document& doc, const write_config& cfg = {});

// Serialize a reflected object through to_value into a temporary document.
// Returns an empty string when conversion fails (error codes are not surfaced
// by this convenience overload). A non-default naming policy is honored on the
// reflected field keys via to_value_named.
template <typename T>
std::string serialize(const T& obj, const write_config& cfg = {}) {
    document doc;
    error_code e = error_code::ok;
    if (cfg.rename_map == nullptr &&
        cfg.key_case == rtti::detail::key_case::identity && !cfg.skip_empty) {
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
result<T> parse_as(string_view src) {
    result<T> r;
    result<document> d = parse(src);
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
result<T> parse_as(string_view src, const write_config& cfg) {
    result<T> r;
    result<document> d = parse(src);
    if (!d.ok()) {
        r.err = d.err;
        return r;
    }
    error_code e = error_code::ok;
    if (cfg.rename_map == nullptr &&
        cfg.key_case == rtti::detail::key_case::identity && !cfg.skip_empty) {
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

} // namespace toml
} // namespace rtti
