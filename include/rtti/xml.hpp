#pragma once
// XML format adapter for the RTTI reflection library.
//
// Implements rtti::value_io over a lightweight, self-owning XML tree:
//   - reading a parsed document through the value_io object/array/scalar
//     mapping (attributes, child-element text, repeated-child collections,
//     element-text binding),
//   - building a document through the value_io sink interface,
//   - parsing (elements, attributes, text, entities, CDATA, comments, PIs,
//     declarations, DTD internal subsets, namespaces) with an error_code,
//   - serialization with configurable declaration and indentation.
//
// Mapping rules (see also the task specification):
//   - root element      <-> object;      field name <-> key.
//   - attribute         <-> scalar field (attributes win over child elements).
//   - child element text<-> scalar field ("name" style binding).
//   - field named "value"/"_text" binds to the element's own text content.
//   - repeated same-name child elements form an array.
//   - struct fields map to child elements parsed as objects.
//
// Enum types registered with RTTI_DEFINE_ENUM serialize as their label text
// and parse back from the label. The library's extension point
// (has_adl_to/has_adl_from three-argument hooks) routes enum conversion, so
// enum members inside reflected structs work through the generic converters.
//
// English comments only; C++17; no third-party dependencies.

#include <rtti/rtti_fwd.hpp>

#if !defined(RTTI_ENABLE_XML) || !RTTI_ENABLE_XML
#  error "RTTI: XML format adapter not enabled (compile with -DRTTI_ENABLE_XML=1)"
#endif

#include <rtti/convert.hpp>
#include <rtti/memory.hpp>
#include <rtti/naming.hpp>
#include <rtti/detail/scan.hpp>

#include <cctype>
#include <cerrno>
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
namespace xml {

// Re-exported so cx.key_case = rtti::xml::key_case::snake reads naturally.
using rtti::detail::key_case;

// Serialization options.
struct write_config {
    bool with_declaration = true;  // emit "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    int indent = 4;                // 0 = compact; >0 = pretty with that many spaces
    // Reflected-field key naming (Task D1/D2). Defaults keep existing call
    // sites unchanged. New fields are appended AFTER the original members so
    // legacy positional aggregate initialization (e.g. write_config{true, 4})
    // keeps mapping with_declaration/indent exactly as before.
    char indent_char = ' ';        // indentation fill character (e.g. '\t')
    detail::key_case key_case = detail::key_case::identity;
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
};

// Parse options.
struct parse_config {
    std::size_t max_depth = 512;                 // element nesting depth limit (too_deep)
    std::size_t max_entity_depth = 32;           // DTD entity expansion depth limit
    std::size_t max_entity_expansions = 10000;   // total DTD entity expansion budget
};

// Streaming event callbacks (SAX). Returning false cancels the parse early;
// the document is not built in that case and parse_stream returns ok.
// Only non-empty (assigned) callbacks are invoked. XML-specific events map
// directly onto the parser's constructs; JSON/INI-specific events do not
// apply to this format and are intentionally left out of this struct.
struct parse_events {
    std::function<bool(string_view)> element_start;      // element name
    std::function<bool(string_view)> element_end;        // element name
    std::function<bool(string_view, string_view)> attribute;  // name, value
    std::function<bool(string_view)> text;               // accumulated text run
    std::function<bool(string_view)> cdata;              // CDATA content
    std::function<bool(string_view)> comment;            // comment content
    std::function<bool(string_view)> pi;                 // "target" or "target content"
};

class document;

namespace detail {

class node;         // concrete tree node; defined in src/xml.cpp
struct xml_parser;  // XML parser; defined in src/xml.cpp (document befriends it)

// Non-template implementation (the concrete detail::node tree node and the
// detail::xml_parser) lives in src/xml.cpp; this header keeps only the tree
// node's external shape relevant to the document interface below.

} // namespace detail

// An XML document. Implements rtti::value_io so it can be both parsed into
// and serialized from through the generic converters. Move-only; not
// copyable.
//
// Zero-copy contract: the document owns its nodes and any arena-interned
// strings, but element/attribute names, attribute values, text and CDATA are
// string_views into the parse source buffer. The caller must keep that source
// buffer alive for the whole document lifetime, unlike the json/ini/toml
// adapters, which copy every stored string into the document's own arena so
// their views stay valid after the source is destroyed. Destroying the XML
// source first leaves the document referencing freed memory (C-2).
class document : public rtti::value_io {
public:
    document() = default;
    ~document() override;
    document(document&&) noexcept;
    document& operator=(document&&) noexcept;
    document(const document&) = delete;
    document& operator=(const document&) = delete;

    // ---- value_io: read side (delegated to the root element) ---------------
    rtti::value_io::kind type() const noexcept override;
    bool as_bool(bool& v) const noexcept override;
    bool as_integer(std::int64_t& v) const noexcept override;
    bool as_real(double& v) const noexcept override;
    bool as_string(string_view& v) const noexcept override;
    std::size_t size() const noexcept override;
    value_io* at(std::size_t index) override;
    value_io* at(std::size_t index) const override;
    bool get_key(string_view key, value_io*& out) override;
    bool get_key(string_view key, value_io*& out) const override;
    bool get_key_at(std::size_t index, string_view& key, value_io*& out) const override;

    // ---- value_io: write side ----------------------------------------------
    value_io* make_array() override;
    void push_back(value_io* v) override;
    value_io* make_object() override;
    void set_key(string_view key, value_io* v) override;
    value_io* make_scalar(rtti::field_value v) override;
    value_io* make_node() override;

    // Copy `s` into this document's arena and return a stable NUL-terminated
    // pointer. Write-side element names (assigned through set_key) are stored
    // as string_views, so transient keys (e.g. a named-conversion std::string
    // temporary) are interned here to outlive the document.
    const char* intern(string_view s);

    // Returns the in-scope namespace URI bound to `prefix` at `elem`
    // (nullptr meaning the root element); empty string when unbound.
    // xmlns attributes are also preserved verbatim as ordinary attributes.
    std::string ns_uri(const rtti::value_io* elem, string_view prefix) const;

    // Allocates an unattached node owned by this document.
    detail::node* create_node();

    // Serializer needs access to the tree container lists.
    const std::vector<detail::node*>& prolog_nodes() const noexcept;
    const std::vector<detail::node*>& epilog_nodes() const noexcept;
    const detail::node* root_plus() const noexcept;

    // Allocates a lazily-created read-side view node from the arena. View
    // nodes are not part of the document's node pool; the owning element
    // tracks them in its own `views` vector and destroys them when it is torn
    // down, so they must stay out of `owned_` to avoid double destruction.
    detail::node* alloc_view_node() noexcept;

private:
    friend struct detail::xml_parser;

    void ensure_root();
    const char* dup_string(const std::string& s);

    // Run the node destructors in document order. The arena reclaims the
    // backing bytes later without calling destructors, and the node members
    // (children/attrs/strings) own their own heap buffers, so each node needs
    // an explicit destructor call before the blocks are freed.
    void destroy_nodes() noexcept;

    detail::node* root_ = nullptr;
    std::vector<detail::node*> owned_;
    memory_arena arena_;                  // decoded strings; document-owned
    std::vector<detail::node*> prolog_;   // comments/PIs before the root
    std::vector<detail::node*> epilog_;   // comments/PIs after the root
    string_view declaration_;             // raw "<?xml ...?>" text, informational
};

// Non-template members of document and detail::node (move constructor/move
// assignment/destructor and the view/attach helpers that need the complete
// document type) are defined in src/xml.cpp.

// --- Parsing ----------------------------------------------------------------

result<document> parse(string_view src);  // convenience overload
result<document> parse(string_view src, const parse_config& cfg);

// Streaming SAX parse: invokes the non-empty `events` callbacks as the source
// is scanned without building a tree. Returns ok on a full parse or on an
// early cancel (a callback returning false); returns error_info on a syntax
// error. No document is constructed in either outcome.
result<void> parse_stream(string_view src, const parse_events& events);

// --- Serialization ----------------------------------------------------------

// Serialize into a preallocated user buffer. The buffer's content is replaced
// (not appended): each call leaves `out` holding exactly this document's text,
// identical to what serialize(doc, cfg) returns. Previously reserved capacity
// is kept, so callers can reserve() once and reuse the buffer.
void serialize(const document& doc, std::string& out,
               const write_config& cfg = {});

std::string serialize(const document& doc, const write_config& cfg = {});

// --- reflect bridge ---------------------------------------------------------
template <typename T>
inline result<T> parse_as(string_view src) {
    result<T> out;
    const result<document> rd = parse(src);
    if (!rd.ok()) {
        out.err = rd.err;
        return out;
    }
    out.err.code = rtti::from_value(&rd.value, out.value);
    if (out.err.code != error_code::ok) {
        out.err.src = src.data();
        out.err.src_len = src.size();
    }
    return out;
}

// Config-aware parse: output keys written under the same naming policy map
// back to field names through from_value_named.
template <typename T>
inline result<T> parse_as(string_view src, const write_config& cfg) {
    result<T> out;
    const result<document> rd = parse(src);
    if (!rd.ok()) {
        out.err = rd.err;
        return out;
    }
    error_code e = error_code::ok;
    if (cfg.rename_map == nullptr && cfg.key_case == rtti::detail::key_case::identity &&
        !cfg.skip_empty) {
        e = rtti::from_value(&rd.value, out.value);
    } else {
        const rtti::detail::naming_policy np{cfg.key_case, cfg.rename_map,
                                       cfg.skip_empty};
        e = rtti::from_value_named(&rd.value, out.value, np);
    }
    out.err.code = e;
    if (e != error_code::ok) {
        out.err.src = src.data();
        out.err.src_len = src.size();
    }
    return out;
}

template <typename T>
inline std::string serialize(const T& obj, const write_config& cfg = {}) {
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

} // namespace xml
} // namespace rtti
