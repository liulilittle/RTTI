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

// Serialization options.
struct write_config {
    bool with_declaration = true;  // emit "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    int indent = 4;                // 0 = compact; >0 = pretty with that many spaces
    // Future extension points: entity-escaping policy, line terminator, etc.
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

struct xml_parser;  // forward declaration (document befriends it); defined below

enum class nkind { element, text, cdata, comment, pi, attr_view, text_view, array_view };

// A single tree node. Element nodes hold a name, attributes and an ordered
// child list (text/cdata/comment/pi mixed freely). The value_io implementation
// exposes object semantics on element nodes, array semantics on array views and
// scalar semantics on attribute/text views and scalar-mode elements. Read-only
// view nodes are allocated lazily and cached in `views` (their lifetime is the
// owning element's, so returned value_io pointers stay valid).
class node : public rtti::value_io {
public:
    nkind kind = nkind::element;

    // Element identity / PI target.
    string_view name;

    // Element attributes. Values are zero-copy views into the parse buffer or
    // decoded into the document arena.
    std::vector<std::pair<string_view, string_view>> attrs;

    // Mixed child sequence (document order).
    std::vector<node*> children;

    // Leaf payload (text/cdata/comment content or PI body).
    string_view data;

    // Owning document (used to allocate child nodes on the write side).
    document* owner = nullptr;

    // Parent element (used for in-scope namespace lookups).
    node* parent = nullptr;

    // --- Write-side content mode -------------------------------------------
    enum class wmode { none, scalar, object, array } wm = wmode::none;
    rtti::field_value scalar;
    std::string scalar_str;  // owned scalar payload rendered as text

    // --- Array-view parameters ---------------------------------------------
    node* target = nullptr;  // element being queried
    string_view qkey;        // queried child-element name
    std::size_t attr_index = 0;  // attribute index for attr views

    // --- Read-side scratch (mutable for const reads) -----------------------
    mutable std::string tc;
    mutable bool tc_ok = false;
    mutable std::vector<node*> views;

    node() = default;

    // Views are carved from the owning document's arena; the arena reclaims
    // their bytes without running destructors, so the element's destructor
    // must tear the cached views down explicitly.
    ~node() {
        for (node* v : views) {
            if (v != nullptr) {
                v->~node();
            }
        }
    }

    // ---- value_io: read side ----------------------------------------------

    rtti::value_io::kind type() const noexcept override {
        switch (kind) {
            case nkind::array_view: return rtti::value_io::kind::array;
            case nkind::attr_view:
            case nkind::text_view: return rtti::value_io::kind::string;
            default: return rtti::value_io::kind::object;  // elements bind as objects
        }
    }

    bool as_bool(bool& out) const noexcept override {
        string_view v;
        if (!scalar_span(v)) {
            return false;
        }
        if (v.size() == 1) {
            if (v[0] == '1') {
                out = true;
                return true;
            }
            if (v[0] == '0') {
                out = false;
                return true;
            }
            return false;
        }
        if (ci_equal(v, "true")) {
            out = true;
            return true;
        }
        if (ci_equal(v, "false")) {
            out = false;
            return true;
        }
        return false;
    }

    bool as_integer(std::int64_t& out) const noexcept override {
        string_view v;
        if (!scalar_span(v) || v.empty()) {
            return false;
        }
        const std::string tmp(v);
        char* endp = nullptr;
        errno = 0;
        const long long r = std::strtoll(tmp.c_str(), &endp, 10);
        if (endp == tmp.c_str() || errno == ERANGE) {
            return false;
        }
        for (; *endp != '\0'; ++endp) {
            if (!std::isspace(static_cast<unsigned char>(*endp))) {
                return false;
            }
        }
        out = static_cast<std::int64_t>(r);
        return true;
    }

    bool as_real(double& out) const noexcept override {
        string_view v;
        if (!scalar_span(v) || v.empty()) {
            return false;
        }
        const std::string tmp(v);
        char* endp = nullptr;
        errno = 0;
        const double r = std::strtod(tmp.c_str(), &endp);
        if (endp == tmp.c_str() || errno == ERANGE) {
            return false;
        }
        for (; *endp != '\0'; ++endp) {
            if (!std::isspace(static_cast<unsigned char>(*endp))) {
                return false;
            }
        }
        out = r;
        return true;
    }

    bool as_string(string_view& out) const noexcept override { return scalar_span(out); }

    std::size_t size() const noexcept override {
        if (kind == nkind::array_view) {
            return count_named_children();
        }
        if (kind != nkind::element) {
            return 0;
        }
        return element_child_count();
    }

    value_io* at(std::size_t index) override {
        if (kind != nkind::array_view) {
            return nullptr;
        }
        std::size_t seen = 0;
        for (node* c : child_list()) {
            if (c->kind != nkind::element || c->name != qkey) {
                continue;
            }
            if (seen == index) {
                return c;
            }
            ++seen;
        }
        return nullptr;
    }

    value_io* at(std::size_t index) const override {
        return const_cast<node*>(this)->at(index);
    }

    bool get_key(string_view key, value_io*& out) override { return get_key_impl(key, out); }
    bool get_key(string_view key, value_io*& out) const override {
        return get_key_impl(key, out);
    }

    bool get_key_at(std::size_t index, string_view& key, value_io*& out) const override {
        if (kind != nkind::element) {
            return false;
        }
        std::size_t seen = 0;
        for (const node* c : children) {
            if (c->kind != nkind::element) {
                continue;
            }
            if (seen == index) {
                key = c->name;
                out = const_cast<node*>(c);
                return true;
            }
            ++seen;
        }
        return false;
    }

    // ---- value_io: write side ---------------------------------------------

    value_io* make_array() override {
        wm = wmode::array;
        children.clear();
        scalar = rtti::field_value();
        scalar_str.clear();
        return this;
    }

    void push_back(value_io* v) override {
        node* c = static_cast<node*>(v);
        if (c == nullptr) {
            return;
        }
        c->parent = this;
        children.push_back(c);
    }

    value_io* make_object() override {
        wm = wmode::object;
        children.clear();
        scalar = rtti::field_value();
        scalar_str.clear();
        return this;
    }

    void set_key(string_view key, value_io* v) override;

    value_io* make_scalar(rtti::field_value v) override {
        wm = wmode::scalar;
        children.clear();
        scalar = v;
        switch (v.type()) {
            case rtti::field_value::kind::string:
                scalar_str.assign(v.as_string().data(), v.as_string().size());
                break;
            case rtti::field_value::kind::boolean:
                scalar_str = v.as_bool() ? "true" : "false";
                break;
            case rtti::field_value::kind::integer:
                scalar_str = std::to_string(v.as_integer());
                break;
            case rtti::field_value::kind::real:
                scalar_str = format_real(v.as_real());
                break;
            default:
                scalar_str.clear();
                break;
        }
        return this;
    }

    value_io* make_node() override;

    // Hooks used by the parser and serializer.
    bool has_element_children() const {
        for (const node* c : children) {
            if (c->kind == nkind::element) {
                return true;
            }
        }
        return false;
    }

    bool has_text_child() const {
        for (const node* c : children) {
            if (c->kind == nkind::text || c->kind == nkind::cdata) {
                return true;
            }
        }
        return false;
    }

    // The element's own text content (text + CDATA children, in order).
    string_view merged_text() const {
        if (!tc_ok) {
            tc_ok = true;
            tc.clear();
            for (const node* c : children) {
                if (c->kind == nkind::text || c->kind == nkind::cdata) {
                    tc.append(c->data.data(), c->data.size());
                }
            }
        }
        return string_view(tc);
    }

private:
    // Attaches `v` under key `key` following the value_io write mapping.
    // Defined out-of-line after document (needs create_node).
    void attach_under_key(string_view key, node* v);

    // Returns the scalar text this node reads as. Attribute views return the
    // attribute value; single-element array views delegate to their child.
    bool scalar_span(string_view& out) const {
        switch (kind) {
            case nkind::attr_view:
                if (target == nullptr || attr_index >= target->attrs.size()) {
                    return false;
                }
                out = target->attrs[attr_index].second;
                return true;
            case nkind::text_view:
                if (target == nullptr) {
                    return false;
                }
                out = target->merged_text();
                return true;
            case nkind::array_view: {
                node* c = first_named_child();
                if (c != nullptr && count_named_children() == 1 && !c->has_element_children()) {
                    return c->scalar_span(out);
                }
                return false;
            }
            default:
                if (wm == wmode::scalar) {
                    out = string_view(scalar_str);
                    return true;
                }
                out = merged_text();
                return true;
        }
    }

    node* first_named_child() const {
        for (node* c : child_list()) {
            if (c->kind == nkind::element && c->name == qkey) {
                return c;
            }
        }
        return nullptr;
    }

    std::size_t count_named_children() const {
        std::size_t n = 0;
        for (const node* c : child_list()) {
            if (c->kind == nkind::element && c->name == qkey) {
                ++n;
            }
        }
        return n;
    }

    // Array views iterate their target element's children; elements use their
    // own. Detached views (no target) yield an empty list.
    const std::vector<node*>& child_list() const noexcept {
        if (kind == nkind::array_view && target != nullptr) {
            return target->children;
        }
        return children;
    }

    std::size_t element_child_count() const {
        std::size_t n = 0;
        for (const node* c : children) {
            if (c->kind == nkind::element) {
                ++n;
            }
        }
        return n;
    }

    bool get_key_impl(string_view key, value_io*& out) const {
        if (kind != nkind::element) {
            return false;
        }
        // 1. Attributes win over child elements (spec 3.4).
        for (std::size_t i = 0; i < attrs.size(); ++i) {
            if (attrs[i].first == key) {
                node* v = make_view(nkind::attr_view);
                v->target = const_cast<node*>(this);
                v->attr_index = i;
                out = v;
                return true;
            }
        }
        // 2. "value" / "_text" bind to the element's own text content.
        if (key == "value" || key == "_text") {
            node* v = make_view(nkind::text_view);
            v->target = const_cast<node*>(this);
            out = v;
            return true;
        }
        // 3. Same-name child elements.
        std::size_t n = 0;
        node* sole = nullptr;
        for (node* c : children) {
            if (c->kind == nkind::element && c->name == key) {
                ++n;
                if (sole == nullptr) {
                    sole = c;
                }
            }
        }
        if (n == 0) {
            return false;
        }
        if (n == 1 && sole->has_element_children()) {
            out = sole;
            return true;
        }
        node* v = make_view(nkind::array_view);
        v->target = const_cast<node*>(this);
        v->qkey = key;
        out = v;
        return true;
    }

    // Creates a lazily-cached view node from the owning document's arena.
    // Defined out-of-line after document (needs alloc_view_node).
    node* make_view(nkind k) const;

    static bool ci_equal(string_view v, const char* s) {
        const std::size_t n = std::strlen(s);
        if (v.size() != n) {
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (static_cast<char>(std::tolower(static_cast<unsigned char>(v[i]))) != s[i]) {
                return false;
            }
        }
        return true;
    }

    static std::string format_real(double d) {
        char buf[64];
        const int n = std::snprintf(buf, sizeof(buf), "%.17g", d);
        return std::string(buf, n > 0 ? static_cast<std::size_t>(n) : 0);
    }
};

} // namespace detail

// An XML document. Owns its entire tree (arena-backed strings plus nodes);
// implements rtti::value_io so it can be both parsed into and serialized from
// through the generic converters. Move-only; not copyable.
class document : public rtti::value_io {
public:
    document() = default;
    ~document() override;
    document(document&&) noexcept;
    document& operator=(document&&) noexcept;
    document(const document&) = delete;
    document& operator=(const document&) = delete;

    // ---- value_io: read side (delegated to the root element) ---------------
    rtti::value_io::kind type() const noexcept override {
        return root_ != nullptr ? root_->type() : rtti::value_io::kind::null;
    }
    bool as_bool(bool& v) const noexcept override {
        return root_ != nullptr && root_->as_bool(v);
    }
    bool as_integer(std::int64_t& v) const noexcept override {
        return root_ != nullptr && root_->as_integer(v);
    }
    bool as_real(double& v) const noexcept override {
        return root_ != nullptr && root_->as_real(v);
    }
    bool as_string(string_view& v) const noexcept override {
        return root_ != nullptr && root_->as_string(v);
    }
    std::size_t size() const noexcept override { return root_ != nullptr ? root_->size() : 0; }
    value_io* at(std::size_t index) override {
        return root_ != nullptr ? root_->at(index) : nullptr;
    }
    value_io* at(std::size_t index) const override {
        return root_ != nullptr ? root_->at(index) : nullptr;
    }
    bool get_key(string_view key, value_io*& out) override {
        return root_ != nullptr && root_->get_key(key, out);
    }
    bool get_key(string_view key, value_io*& out) const override {
        return root_ != nullptr && root_->get_key(key, out);
    }
    bool get_key_at(std::size_t index, string_view& key, value_io*& out) const override {
        return root_ != nullptr && root_->get_key_at(index, key, out);
    }

    // ---- value_io: write side ----------------------------------------------
    value_io* make_array() override {
        ensure_root();
        return root_->make_array();
    }
    void push_back(value_io* v) override {
        ensure_root();
        root_->push_back(v);
    }
    value_io* make_object() override {
        ensure_root();
        return root_->make_object();
    }
    void set_key(string_view key, value_io* v) override {
        ensure_root();
        root_->set_key(key, v);
    }
    value_io* make_scalar(rtti::field_value v) override {
        ensure_root();
        return root_->make_scalar(v);
    }
    value_io* make_node() override { return create_node(); }

    // Returns the in-scope namespace URI bound to `prefix` at `elem`
    // (nullptr meaning the root element); empty string when unbound.
    // xmlns attributes are also preserved verbatim as ordinary attributes.
    std::string ns_uri(const rtti::value_io* elem, string_view prefix) const {
        const detail::node* n = static_cast<const detail::node*>(elem);
        if (n == nullptr) {
            n = root_;
        }
        const std::string want =
            prefix.empty() ? std::string("xmlns")
                           : (std::string("xmlns:") + std::string(prefix));
        while (n != nullptr) {
            for (const auto& a : n->attrs) {
                if (a.first == want) {
                    return std::string(a.second);
                }
            }
            n = n->parent;
        }
        return std::string();
    }

    // Allocates an unattached node owned by this document.
    detail::node* create_node() {
        detail::node* p = arena_.alloc_obj<detail::node>();
        if (p == nullptr) {
            return nullptr;
        }
        p->owner = this;
        p->name = string_view("item");  // default; overridden by set_key
        owned_.push_back(p);
        return p;
    }

    // Serializer needs access to the tree container lists.
    const std::vector<detail::node*>& prolog_nodes() const noexcept { return prolog_; }
    const std::vector<detail::node*>& epilog_nodes() const noexcept { return epilog_; }
    const detail::node* root_plus() const noexcept { return root_; }

    // Allocates a lazily-created read-side view node from the arena. View
    // nodes are not part of the document's node pool; the owning element
    // tracks them in its own `views` vector and destroys them when it is torn
    // down, so they must stay out of `owned_` to avoid double destruction.
    detail::node* alloc_view_node() noexcept {
        return arena_.alloc_obj<detail::node>();
    }

private:
    friend struct detail::xml_parser;

    void ensure_root() {
        if (root_ == nullptr) {
            root_ = create_node();
            root_->name = string_view("root");
        }
    }

    const char* dup_string(const std::string& s) {
        return arena_.dup(s.data(), s.size());
    }

    // Run the node destructors in document order. The arena reclaims the
    // backing bytes later without calling destructors, and the node members
    // (children/attrs/strings) own their own heap buffers, so each node needs
    // an explicit destructor call before the blocks are freed.
    void destroy_nodes() noexcept {
        for (detail::node* p : owned_) {
            if (p != nullptr) {
                p->~node();
            }
        }
        owned_.clear();
    }

    detail::node* root_ = nullptr;
    std::vector<detail::node*> owned_;
    memory_arena arena_;                  // decoded strings; document-owned
    std::vector<detail::node*> prolog_;   // comments/PIs before the root
    std::vector<detail::node*> epilog_;   // comments/PIs after the root
    string_view declaration_;             // raw "<?xml ...?>" text, informational
};

inline document::~document() {
    destroy_nodes();
}

document::document(document&& other) noexcept
    : root_(other.root_),
      owned_(std::move(other.owned_)),
      arena_(std::move(other.arena_)),
      prolog_(std::move(other.prolog_)),
      epilog_(std::move(other.epilog_)),
      declaration_(other.declaration_) {
    other.root_ = nullptr;
    other.declaration_ = string_view();
    // The arena/node blocks move, but node back-pointers must track the new
    // document object so make_node keeps working after a move.
    for (detail::node* p : owned_) {
        if (p != nullptr) {
            p->owner = this;
        }
    }
}

document& document::operator=(document&& other) noexcept {
    if (this != &other) {
        destroy_nodes();  // teardown the current node pool
        root_ = other.root_;
        owned_ = std::move(other.owned_);
        arena_ = std::move(other.arena_);
        prolog_ = std::move(other.prolog_);
        epilog_ = std::move(other.epilog_);
        declaration_ = other.declaration_;
        other.root_ = nullptr;
        other.declaration_ = string_view();
        for (detail::node* p : owned_) {
            if (p != nullptr) {
                p->owner = this;
            }
        }
    }
    return *this;
}

// Out-of-line node members that need the complete document type.
inline value_io* detail::node::make_node() {
    return owner != nullptr ? owner->create_node() : nullptr;
}

inline detail::node* detail::node::make_view(nkind k) const {
    if (owner == nullptr) {
        return nullptr;
    }
    node* raw = owner->alloc_view_node();
    if (raw == nullptr) {
        return nullptr;
    }
    raw->kind = k;
    views.push_back(raw);
    return raw;
}

inline void detail::node::set_key(string_view key, value_io* v) {
    attach_under_key(key, static_cast<node*>(v));
}

inline void detail::node::attach_under_key(string_view key, node* v) {
    if (v == nullptr) {
        return;
    }
    if (v->wm == wmode::array) {
        for (node* c : v->children) {
            c->name = key;
            c->parent = this;
            children.push_back(c);
        }
        v->children.clear();
        return;
    }
    const bool text_key = (key == "value" || key == "_text");
    if (text_key && v->wm == wmode::scalar) {
        node* t = owner != nullptr ? owner->create_node() : nullptr;
        if (t != nullptr) {
            t->kind = nkind::text;
            t->data = string_view(v->scalar_str);
            t->parent = this;
            children.push_back(t);
        }
        return;
    }
    v->name = key;
    v->parent = this;
    children.push_back(v);
}

namespace detail {

// Attribute/entity scans fold into the minimal masks below; the masks are
// only assembled when a span is long enough for scan.hpp's SIMD block, so
// short runs never pay for them.

// Mask over an attribute-value run: the active delimiter quote plus the two
// markup bytes that also end or abort the value.
inline rtti::detail::char_mask xml_attr_value_mask(char quote) noexcept {
    rtti::detail::char_mask m;
    m.set(quote);
    m.set('<');
    m.set('&');
    return m;
}

// Entities are bounded by ';' at the end and '<' (invalid, aborts) inside.
inline rtti::detail::char_mask xml_entity_scan_mask() noexcept {
    rtti::detail::char_mask m;
    m.set(';');
    m.set('<');
    return m;
}

// First byte in [begin, end) that ends an attribute-value run: the active
// delimiter quote, '<' (aborts the value) or '&' (entity reference).
inline const char* find_attr_mark(const char* begin, const char* end,
                                  char quote) noexcept {
    if (end - begin >= 16) {
        return rtti::detail::find_first_of(begin, end,
                                           xml_attr_value_mask(quote));
    }
    for (const char* p = begin; p < end; ++p) {
        if (*p == quote || *p == '<' || *p == '&') {
            return p;
        }
    }
    return end;
}

// First ';' or '<' in [begin, end) bounding an entity reference, or end.
inline const char* find_entity_mark(const char* begin,
                                    const char* end) noexcept {
    if (end - begin >= 16) {
        return rtti::detail::find_first_of(begin, end, xml_entity_scan_mask());
    }
    for (const char* p = begin; p < end; ++p) {
        if (*p == ';' || *p == '<') {
            return p;
        }
    }
    return end;
}

// Parser producing a document (or, when `events` is provided, a streaming SAX
// front end that fires callbacks without building the tree). English comments.
struct xml_parser {
public:
    xml_parser(string_view src, document& doc, const parse_config& cfg,
               const parse_events* events = nullptr)
        : p_(src.data()),
          end_(src.data() + src.size()),
          src_start_(src.data()),
          doc_(doc),
          cfg_(cfg),
          ev_(events),
          streaming_(events != nullptr) {}

    error_code run() {
        skip_utf8_bom();
        bool root_seen = false;
        for (;;) {
            skip_ws();
            if (p_ >= end_) {
                return root_seen ? error_code::ok : error_code::invalid_root;
            }
            if (*p_ != '<') {
                // Top-level text: whitespace only.
                const char* const q = rtti::detail::find_first_of(
                    p_, end_, rtti::detail::char_mask::single('<'));
                const char* const nws = rtti::detail::skip_whitespace(p_, q);
                if (nws != q) {
                    p_ = nws;
                    return error_code::invalid_root;
                }
                p_ = q;
                continue;
            }
            if (starts_with("<?xml") && !root_seen && doc_.prolog_.empty()) {
                error_code e = parse_pi(true, nullptr, nullptr);
                if (e != error_code::ok) {
                    return e;
                }
                continue;
            }
            if (starts_with("<?")) {
                bool is_decl = false;
                detail::node* pi = nullptr;
                error_code e = parse_pi(false, &is_decl, &pi);
                if (e != error_code::ok) {
                    return e;
                }
                if (cancelled_) {
                    return error_code::ok;
                }
                if (pi != nullptr) {
                    (root_seen ? doc_.epilog_ : doc_.prolog_).push_back(pi);
                }
                continue;
            }
            if (starts_with("<!--")) {
                detail::node* c = nullptr;
                error_code e = parse_comment(&c);
                if (e != error_code::ok) {
                    return e;
                }
                if (cancelled_) {
                    return error_code::ok;
                }
                if (c != nullptr) {
                    (root_seen ? doc_.epilog_ : doc_.prolog_).push_back(c);
                }
                continue;
            }
            if (starts_with("<!DOCTYPE")) {
                if (root_seen) {
                    return error_code::invalid_root;
                }
                error_code e = parse_doctype();
                if (e != error_code::ok) {
                    return e;
                }
                continue;
            }
            if (starts_with("<![CDATA[")) {
                return error_code::unexpected_token;
            }
            if (starts_with("</")) {
                return error_code::invalid_root;
            }
            if (root_seen) {
                return error_code::invalid_root;
            }
            error_code e = parse_element(nullptr, 0);
            if (e != error_code::ok) {
                return e;
            }
            if (cancelled_) {
                return error_code::ok;
            }
            root_seen = true;
        }
    }

    std::size_t error_offset() const noexcept {
        return static_cast<std::size_t>(p_ - src_start_);
    }

private:
    // Parses one element (attributes and, unless self-closing, children) and
    // attaches it to `parent` (nullptr => document root). In streaming mode no
    // nodes are allocated; the events are fired and `parent` is unused.
    error_code parse_element(detail::node* parent, std::size_t depth) {
        if (depth >= cfg_.max_depth) {
            return error_code::too_deep;
        }
        if (p_ >= end_ || *p_ != '<') {
            return error_code::unexpected_token;
        }
        ++p_;
        const string_view nm = read_name();
        if (nm.empty()) {
            return error_code::unexpected_token;
        }
        if (streaming_ && !fire_event(ev_->element_start, nm)) {
            cancelled_ = true;
            return error_code::ok;
        }

        detail::node* el = nullptr;
        if (!streaming_) {
            el = doc_.create_node();
            el->kind = nkind::element;
            el->name = nm;
            el->parent = parent;
        }

        std::vector<string_view> seen_attrs;  // streaming duplicate-attribute check
        bool self_closing = false;
        for (;;) {
            skip_ws();
            if (p_ >= end_) {
                return error_code::unexpected_token;
            }
            if (*p_ == '>') {
                ++p_;
                break;
            }
            if (p_[0] == '/' && p_ + 1 < end_ && p_[1] == '>') {
                p_ += 2;
                self_closing = true;
                break;
            }
            const string_view an = read_name();
            if (an.empty()) {
                return error_code::unexpected_token;
            }
            if (streaming_) {
                for (const string_view& n : seen_attrs) {
                    if (n == an) {
                        return error_code::duplicate_attribute;
                    }
                }
            } else {
                for (const auto& a : el->attrs) {
                    if (a.first == an) {
                        return error_code::duplicate_attribute;
                    }
                }
            }
            skip_ws();
            if (p_ >= end_ || *p_ != '=') {
                return error_code::unexpected_token;
            }
            ++p_;
            skip_ws();
            if (p_ >= end_ || (*p_ != '"' && *p_ != '\'')) {
                return error_code::unexpected_token;
            }
            const char quote = *p_;
            ++p_;
            const char* vstart = p_;
            std::string vbuf;
            bool vdec = false;
            for (;;) {
                const char* const chunk_start = p_;
                const char* q = find_attr_mark(chunk_start, end_, quote);
                if (q == end_) {
                    p_ = q;
                    return error_code::unterminated_string;
                }
                if (*q == '<') {
                    p_ = q;
                    return error_code::unterminated_string;
                }
                if (*q == '&') {
                    if (vdec) {
                        vbuf.append(chunk_start,
                                    static_cast<std::size_t>(q - chunk_start));
                    } else {
                        vbuf.append(vstart,
                                    static_cast<std::size_t>(q - vstart));
                        vdec = true;
                    }
                    p_ = q;
                    error_code e = decode_entity(vbuf);
                    if (e != error_code::ok) {
                        return e;
                    }
                    continue;
                }
                // *q == quote: end of the value.
                if (vdec) {
                    vbuf.append(chunk_start,
                                static_cast<std::size_t>(q - chunk_start));
                }
                p_ = q + 1;  // closing quote
                break;
            }
            if (streaming_) {
                seen_attrs.push_back(an);
                if (!fire_event(ev_->attribute, an,
                                vdec ? string_view(vbuf)
                                     : string_view(vstart, p_ - 1 - vstart))) {
                    cancelled_ = true;
                    return error_code::ok;
                }
            } else if (vdec) {
                const char* duped = doc_.dup_string(vbuf);
                el->attrs.emplace_back(an, string_view(duped, vbuf.size()));
            } else {
                el->attrs.emplace_back(an, string_view(vstart, p_ - 1 - vstart));
            }
        }

        if (el != nullptr) {
            if (parent != nullptr) {
                parent->children.push_back(el);
            } else {
                doc_.root_ = el;
            }
        }
        if (self_closing) {
            // A self-closing element yields both start and end events.
            if (streaming_ && !fire_event(ev_->element_end, nm)) {
                cancelled_ = true;
            }
            return error_code::ok;
        }

        for (;;) {
            if (p_ >= end_) {
                return error_code::unexpected_token;  // unterminated element
            }
            if (*p_ != '<') {
                error_code e = parse_text(el);
                if (e != error_code::ok) {
                    return e;
                }
                if (streaming_ && cancelled_) {
                    return error_code::ok;
                }
                continue;
            }
            if (p_ + 1 >= end_) {
                return error_code::unexpected_token;
            }
            if (p_[1] == '/') {
                p_ += 2;
                const string_view cn = read_name();
                if (cn.empty()) {
                    return error_code::unexpected_token;
                }
                skip_ws();
                if (p_ >= end_ || *p_ != '>') {
                    return error_code::unexpected_token;
                }
                ++p_;
                if (cn != nm) {
                    return error_code::mismatched_tag;
                }
                if (streaming_ && !fire_event(ev_->element_end, nm)) {
                    cancelled_ = true;
                }
                return error_code::ok;
            }
            if (p_[1] == '!') {
                if (starts_with("<![CDATA[")) {
                    error_code e = parse_cdata(el);
                    if (e != error_code::ok) {
                        return e;
                    }
                    if (streaming_ && cancelled_) {
                        return error_code::ok;
                    }
                } else if (starts_with("<!--")) {
                    detail::node* c = nullptr;
                    error_code e = parse_comment(&c);
                    if (e != error_code::ok) {
                        return e;
                    }
                    if (streaming_ && cancelled_) {
                        return error_code::ok;
                    }
                    if (c != nullptr) {
                        c->parent = el;
                        el->children.push_back(c);
                    }
                } else {
                    return error_code::unexpected_token;
                }
                continue;
            }
            if (p_[1] == '?') {
                bool is_decl = false;
                detail::node* pi = nullptr;
                error_code e = parse_pi(false, &is_decl, &pi);
                if (e != error_code::ok) {
                    return e;
                }
                if (streaming_ && cancelled_) {
                    return error_code::ok;
                }
                if (pi != nullptr) {
                    pi->parent = el;
                    el->children.push_back(pi);
                }
                continue;
            }
            error_code e = parse_element(el, depth + 1);
            if (e != error_code::ok) {
                return e;
            }
            if (streaming_ && cancelled_) {
                return error_code::ok;
            }
        }
    }

    error_code parse_text(detail::node* el) {
        const char* start = p_;
        std::string buf;
        bool decoded = false;
        // Text runs in typical documents are a few bytes long: the shared
        // scanner (whose per-call setup only pays off at or beyond its 16-byte
        // SIMD block) measured no faster than this direct loop on the standard
        // dense benchmark, so the original per-byte body is kept here.
        while (p_ < end_ && *p_ != '<') {
            const unsigned char c = static_cast<unsigned char>(*p_);
            if (c == '&') {
                if (!decoded) {
                    buf.append(start, p_ - start);
                    decoded = true;
                }
                error_code e = decode_entity(buf);
                if (e != error_code::ok) {
                    return e;
                }
                continue;
            }
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                return error_code::unexpected_token;  // illegal character
            }
            if (decoded) {
                buf.push_back(static_cast<char>(c));
            }
            ++p_;
        }
        const std::size_t len = static_cast<std::size_t>(p_ - start);
        if (streaming_) {
            // Streaming: one accumulated-text event per contiguous text run in
            // an element (adjacent runs are coalesced by the parser itself).
            const string_view run = decoded ? string_view(buf) : string_view(start, len);
            if (!run.empty() && !fire_event(ev_->text, run)) {
                cancelled_ = true;
                return error_code::ok;
            }
            return error_code::ok;
        }
        if (decoded) {
            if (!buf.empty()) {
                detail::node* t = doc_.create_node();
                t->kind = nkind::text;
                const char* duped = doc_.dup_string(buf);
                t->data = string_view(duped, buf.size());
                t->parent = el;
                el->children.push_back(t);
            }
        } else if (len > 0) {
            detail::node* t = doc_.create_node();
            t->kind = nkind::text;
            t->data = string_view(start, len);
            t->parent = el;
            el->children.push_back(t);
        }
        return error_code::ok;
    }

    // Decodes the entity starting at p_ ('&' ... ';') into `out`.
    error_code decode_entity(std::string& out) {
        ++p_;  // past '&'
        const char* semi = find_entity_mark(p_, end_);
        if (semi == end_ || *semi != ';') {
            return error_code::bad_entity;
        }
        const string_view name(p_, static_cast<std::size_t>(semi - p_));
        error_code e = decode_reference(name, out, 0);
        if (e != error_code::ok) {
            return e;
        }
        p_ = semi + 1;
        return error_code::ok;
    }

    error_code decode_reference(string_view name, std::string& out, std::size_t depth) {
        if (name.empty()) {
            return error_code::bad_entity;
        }
        if (name[0] == '#') {
            unsigned cp = 0;
            if (name.size() >= 2 && (name[1] == 'x' || name[1] == 'X')) {
                for (std::size_t i = 2; i < name.size(); ++i) {
                    const unsigned d = hexval(name[i]);
                    if (d == 0xFFu) {
                        return error_code::bad_entity;
                    }
                    cp = cp * 16u + d;
                }
            } else {
                for (std::size_t i = 1; i < name.size(); ++i) {
                    const char c = name[i];
                    if (c < '0' || c > '9') {
                        return error_code::bad_entity;
                    }
                    cp = cp * 10u + static_cast<unsigned>(c - '0');
                }
            }
            if (!valid_xml_char(cp)) {
                return error_code::bad_entity;
            }
            utf8_encode(cp, out);
            return error_code::ok;
        }
        if (name == "amp") {
            out += '&';
            return error_code::ok;
        }
        if (name == "lt") {
            out += '<';
            return error_code::ok;
        }
        if (name == "gt") {
            out += '>';
            return error_code::ok;
        }
        if (name == "apos") {
            out += '\'';
            return error_code::ok;
        }
        if (name == "quot") {
            out += '"';
            return error_code::ok;
        }
        const auto it = entities_.find(std::string(name));
        if (it == entities_.end()) {
            return error_code::bad_entity;
        }
        if (depth + 1 > cfg_.max_entity_depth || ++expansions_ > cfg_.max_entity_expansions) {
            return error_code::too_deep;
        }
        return decode_string(it->second, out, depth + 1);
    }

    error_code decode_string(string_view s, std::string& out, std::size_t depth) {
        // DTD entity values are bounded by max_entity_expansions; the loop only
        // ever sees short pre-registered strings, so it keeps the direct scan
        // rather than paying scan.hpp's setup cost.
        std::size_t i = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c != '&') {
                out.push_back(c);
                ++i;
                continue;
            }
            const std::size_t semi = s.find(';', i);
            if (semi == string_view::npos) {
                return error_code::bad_entity;
            }
            const string_view name = s.substr(i + 1, semi - i - 1);
            error_code e = decode_reference(name, out, depth);
            if (e != error_code::ok) {
                return e;
            }
            i = semi + 1;
        }
        return error_code::ok;
    }

    error_code parse_cdata(detail::node* el) {
        p_ += 9;  // "<![CDATA["
        const char* st = p_;
        const char* q = find_substr("]]>");
        if (q == nullptr) {
            return error_code::unterminated_cdata;
        }
        if (streaming_) {
            p_ = q + 3;
            if (!fire_event(ev_->cdata, string_view(st, static_cast<std::size_t>(q - st)))) {
                cancelled_ = true;
                return error_code::ok;
            }
            return error_code::ok;
        }
        detail::node* n = doc_.create_node();
        n->kind = nkind::cdata;
        n->data = string_view(st, static_cast<std::size_t>(q - st));
        n->parent = el;
        el->children.push_back(n);
        p_ = q + 3;
        return error_code::ok;
    }

    error_code parse_comment(detail::node** out) {
        p_ += 4;  // "<!--"
        const char* st = p_;
        const char* q = find_substr("-->");
        if (q == nullptr) {
            return error_code::unterminated_comment;
        }
        if (streaming_) {
            p_ = q + 3;
            if (out != nullptr) {
                *out = nullptr;
            }
            if (!fire_event(ev_->comment, string_view(st, static_cast<std::size_t>(q - st)))) {
                cancelled_ = true;
                return error_code::ok;
            }
            return error_code::ok;
        }
        detail::node* n = doc_.create_node();
        n->kind = nkind::comment;
        n->data = string_view(st, static_cast<std::size_t>(q - st));
        *out = n;
        p_ = q + 3;
        return error_code::ok;
    }

    // Parses a processing instruction. When `as_decl` is set the markup is the
    // XML declaration (informational only). Otherwise `pi` receives the node.
    error_code parse_pi(bool as_decl, bool* is_decl, detail::node** pi) {
        if (pi != nullptr) {
            *pi = nullptr;
        }
        if (is_decl != nullptr) {
            *is_decl = false;
        }
        const char* start = p_;
        p_ += 2;  // "<?"
        const string_view target = read_name();
        if (target.empty()) {
            return error_code::unexpected_token;
        }
        const char* q = find_substr("?>");
        if (q == nullptr) {
            return error_code::unexpected_token;
        }
        const bool treat_as_decl = as_decl;
        if (treat_as_decl) {
            if (is_decl != nullptr) {
                *is_decl = true;
            }
            doc_.declaration_ = string_view(start, static_cast<std::size_t>(q + 2 - start));
            p_ = q + 2;
            return error_code::ok;
        }
        string_view body;
        if (p_ < q && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) {
            ++p_;
        }
        body = string_view(p_, static_cast<std::size_t>(q - p_));
        if (streaming_) {
            p_ = q + 2;
            // PI event payload: "target" or "target content".
            std::string joined;
            joined.append(target.data(), target.size());
            if (!body.empty()) {
                joined.push_back(' ');
                joined.append(body.data(), body.size());
            }
            if (!fire_event(ev_->pi, string_view(joined))) {
                cancelled_ = true;
                return error_code::ok;
            }
            return error_code::ok;
        }
        detail::node* n = doc_.create_node();
        n->kind = nkind::pi;
        n->name = target;
        n->data = body;
        *pi = n;
        p_ = q + 2;
        return error_code::ok;
    }

    error_code parse_doctype() {
        p_ += 9;  // "<!DOCTYPE"
        skip_ws();
        if (read_name().empty()) {
            return error_code::unexpected_token;
        }
        skip_ws();
        if (starts_with("SYSTEM") || starts_with("PUBLIC")) {
            while (p_ < end_ && *p_ != '[' && *p_ != '>') {
                ++p_;
            }
            if (p_ >= end_) {
                return error_code::unexpected_token;
            }
            if (*p_ == '>') {
                ++p_;
                return error_code::ok;
            }
        }
        skip_ws();
        if (p_ < end_ && *p_ == '[') {
            ++p_;
            for (;;) {
                if (p_ >= end_) {
                    return error_code::unexpected_token;
                }
                if (*p_ == ']') {
                    ++p_;
                    break;
                }
                if (*p_ == '<') {
                    if (starts_with("<!ENTITY")) {
                        error_code e = parse_entity_decl();
                        if (e != error_code::ok) {
                            return e;
                        }
                    } else if (starts_with("<!DOCTYPE")) {
                        return error_code::unexpected_token;
                    } else {
                        skip_declaration();
                    }
                } else {
                    ++p_;
                }
            }
        }
        skip_ws();
        if (p_ >= end_ || *p_ != '>') {
            return error_code::unexpected_token;
        }
        ++p_;
        return error_code::ok;
    }

    // Registers an internal general entity ("<!ENTITY name 'value'>").
    error_code parse_entity_decl() {
        p_ += 8;  // "<!ENTITY"
        skip_ws();
        if (p_ < end_ && *p_ == '%') {
            skip_declaration();  // parameter entity: not supported
            return error_code::ok;
        }
        const string_view nm = read_name();
        if (nm.empty()) {
            return error_code::unexpected_token;
        }
        skip_ws();
        if (p_ >= end_) {
            return error_code::unexpected_token;
        }
        if (*p_ != '"' && *p_ != '\'') {
            // External (SYSTEM/PUBLIC) entity: not fetched.
            skip_declaration();
            return error_code::ok;
        }
        const char quote = *p_;
        ++p_;
        const char* vstart = p_;
        while (p_ < end_ && *p_ != quote) {
            ++p_;
        }
        if (p_ >= end_) {
            return error_code::unexpected_token;
        }
        const std::string value(vstart, p_ - vstart);
        ++p_;
        skip_ws();
        if (p_ >= end_ || *p_ != '>') {
            return error_code::unexpected_token;
        }
        ++p_;
        entities_[std::string(nm)] = value;
        return error_code::ok;
    }

    void skip_declaration() {
        // Quote-tracking state machine, not a flat membership scan: scan.hpp
        // cannot express "skip until '>' unless inside a quoted literal".
        while (p_ < end_ && *p_ != '>') {
            if (*p_ == '"' || *p_ == '\'') {
                const char q = *p_;
                ++p_;
                while (p_ < end_ && *p_ != q) {
                    ++p_;
                }
                if (p_ < end_) {
                    ++p_;
                }
            } else {
                ++p_;
            }
        }
        if (p_ < end_) {
            ++p_;
        }
    }

    // --- low-level helpers --------------------------------------------------

    // Invokes a non-empty streaming callback. Returns true to continue or
    // false when the callback requested an early cancel.
    template <typename Fn, typename... Args>
    bool fire_event(Fn& cb, Args&&... args) {
        if (!static_cast<bool>(cb)) {
            return true;
        }
        return cb(std::forward<Args>(args)...);
    }

    const char* find_substr(const char* needle) const {
        // A char_mask cannot express multi-byte sequences ("]]>", "-->", "?>"),
        // so the substring scan stays as a memcmp walk.
        const std::size_t n = std::strlen(needle);
        const char* it = p_;
        while (it + n <= end_) {
            if (std::memcmp(it, needle, n) == 0) {
                return it;
            }
            ++it;
        }
        return nullptr;
    }

    bool starts_with(const char* s) const {
        const std::size_t n = std::strlen(s);
        if (static_cast<std::size_t>(end_ - p_) < n) {
            return false;
        }
        return std::memcmp(p_, s, n) == 0;
    }

    void skip_utf8_bom() {
        if (end_ - p_ >= 3 &&
            static_cast<unsigned char>(p_[0]) == 0xEF &&
            static_cast<unsigned char>(p_[1]) == 0xBB &&
            static_cast<unsigned char>(p_[2]) == 0xBF) {
            p_ += 3;
        }
    }

    void skip_ws() {
        // Fast path: a non-whitespace first byte means nothing to skip,
        // avoiding the scanner's per-call setup on the tight-token norm.
        if (p_ != end_ && *p_ != ' ' && *p_ != '\t' && *p_ != '\n' &&
            *p_ != '\r') {
            return;
        }
        p_ = rtti::detail::skip_whitespace(p_, end_);
    }

    string_view read_name() {
        // Name characters (letters, digits, '.', '-', '_', ':', high bytes)
        // have no single char-mask in scan.hpp, so the per-byte loop stays.
        if (p_ >= end_) {
            return string_view();
        }
        const unsigned char c0 = static_cast<unsigned char>(*p_);
        if (c0 >= 0x80) {
            // High bytes are accepted as name characters (UTF-8 lenient).
            while (p_ < end_ && (static_cast<unsigned char>(*p_) & 0x80u) != 0u) {
                ++p_;
            }
            return string_view();
        }
        if (!(std::isalpha(c0) || c0 == '_' || c0 == ':')) {
            return string_view();
        }
        const char* s = p_;
        ++p_;
        while (p_ < end_) {
            const unsigned char c = static_cast<unsigned char>(*p_);
            if (c >= 0x80) {
                ++p_;
            } else if (std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':') {
                ++p_;
            } else {
                break;
            }
        }
        return string_view(s, static_cast<std::size_t>(p_ - s));
    }

    static unsigned hexval(char c) {
        if (c >= '0' && c <= '9') {
            return static_cast<unsigned>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<unsigned>(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<unsigned>(c - 'A' + 10);
        }
        return 0xFFu;
    }

    static bool valid_xml_char(unsigned cp) {
        if (cp == 0x9u || cp == 0xAu || cp == 0xDu) {
            return true;
        }
        if (cp >= 0x20u && cp <= 0xD7FFu) {
            return true;
        }
        if (cp >= 0xE000u && cp <= 0xFFFDu) {
            return true;
        }
        if (cp >= 0x10000u && cp <= 0x10FFFFu) {
            return true;
        }
        return false;
    }

    static void utf8_encode(unsigned cp, std::string& out) {
        if (cp < 0x80u) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000u) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    const char* p_;
    const char* end_;
    const char* src_start_;
    document& doc_;
    const parse_config& cfg_;
    const parse_events* ev_ = nullptr;  // non-null => streaming SAX mode
    bool streaming_ = false;
    bool cancelled_ = false;
    std::map<std::string, std::string> entities_;
    std::size_t expansions_ = 0;
};

} // namespace detail

// --- Parsing ----------------------------------------------------------------

inline result<document> parse(string_view src);  // convenience overload
inline result<document> parse(string_view src, const parse_config& cfg);

inline result<document> parse(string_view src) { return parse(src, parse_config{}); }

inline result<document> parse(string_view src, const parse_config& cfg) {
    result<document> r;
    detail::xml_parser parser(src, r.value, cfg);
    const error_code e = parser.run();
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = parser.error_offset();
    }
    return r;
}

// Streaming SAX parse: invokes the non-empty `events` callbacks as the source
// is scanned without building a tree. Returns ok on a full parse or on an
// early cancel (a callback returning false); returns error_info on a syntax
// error. No document is constructed in either outcome.
inline result<void> parse_stream(string_view src, const parse_events& events) {
    result<void> r;
    document scratch;
    detail::xml_parser parser(src, scratch, parse_config{}, &events);
    const error_code e = parser.run();
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        r.err.offset = parser.error_offset();
    }
    return r;
}

// --- Serialization ----------------------------------------------------------

namespace detail {

inline void append_view(std::string& out, string_view s) {
    out.append(s.data(), s.size());
}

inline void escape_text(std::string& out, string_view s) {
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out.push_back(c); break;
        }
    }
}

inline void escape_attr(std::string& out, string_view s) {
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
}

inline void render_element(const node* n, std::string& out, std::size_t depth, bool pretty,
                           std::size_t indent);

inline void render_node(const node* n, std::string& out, std::size_t depth, bool pretty,
                        std::size_t indent) {
    switch (n->kind) {
        case nkind::text:
            escape_text(out, n->data);
            break;
        case nkind::cdata:
            out += "<![CDATA[";
            append_view(out, n->data);
            out += "]]>";
            break;
        case nkind::comment:
            out += "<!--";
            append_view(out, n->data);
            out += "-->";
            break;
        case nkind::pi:
            out += "<?";
            append_view(out, n->name);
            if (!n->data.empty()) {
                out.push_back(' ');
                append_view(out, n->data);
            }
            out += "?>";
            break;
        case nkind::element:
            render_element(n, out, depth, pretty, indent);
            break;
        default:
            break;
    }
}

inline void render_element(const node* n, std::string& out, std::size_t depth, bool pretty,
                           std::size_t indent) {
    out.push_back('<');
    append_view(out, n->name);
    for (const auto& a : n->attrs) {
        out.push_back(' ');
        append_view(out, a.first);
        out += "=\"";
        escape_attr(out, a.second);
        out.push_back('"');
    }
    if (n->wm == node::wmode::scalar) {
        out.push_back('>');
        escape_text(out, n->scalar_str);
        out += "</";
        append_view(out, n->name);
        out.push_back('>');
        return;
    }
    if (n->children.empty()) {
        out += "/>";
        return;
    }
    out.push_back('>');
    const bool inline_content = n->has_text_child();
    for (const node* c : n->children) {
        if (!inline_content && pretty) {
            out.push_back('\n');
            out.append((depth + 1) * indent, ' ');
        }
        render_node(c, out, depth + 1, pretty, indent);
    }
    if (!inline_content && pretty) {
        out.push_back('\n');
        out.append(depth * indent, ' ');
    }
    out += "</";
    append_view(out, n->name);
    out.push_back('>');
}

} // namespace detail

namespace detail {

inline void render_misc(const std::vector<node*>& misc, std::string& out, bool pretty,
                        std::size_t indent) {
    for (const node* n : misc) {
        if (pretty) {
            out.push_back('\n');
            (void)indent;
        }
        render_node(n, out, 0, pretty, indent);
    }
}

} // namespace detail

namespace detail {

// Appends the serialized form of `doc` to `out` under the given config.
// Append (rather than overwrite) semantics keeps this usable as the
// preallocated-buffer entry point: callers that want an overwrite call
// out.clear() first, and repeated calls concatenate into the same buffer.
inline void serialize_into(const document& doc, const write_config& cfg,
                           std::string& out) {
    const bool pretty = cfg.indent > 0;
    const std::size_t indent = cfg.indent > 0 ? static_cast<std::size_t>(cfg.indent) : 0;
    if (cfg.with_declaration) {
        out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        if (pretty) {
            out.push_back('\n');
        }
    }
    render_misc(doc.prolog_nodes(), out, pretty, indent);
    if (doc.root_plus() != nullptr) {
        if (pretty && (!doc.prolog_nodes().empty() || cfg.with_declaration)) {
            out.push_back('\n');
        }
        render_element(doc.root_plus(), out, 0, pretty, indent);
        if (pretty) {
            out.push_back('\n');
        }
    }
    render_misc(doc.epilog_nodes(), out, pretty, indent);
}

} // namespace detail

inline std::string serialize(const document& doc, const write_config& cfg = {}) {
    std::string out;
    detail::serialize_into(doc, cfg, out);
    return out;
}

// Preallocated-buffer entry point: appends the serialized form to `out`
// (repeated calls concatenate; call out.clear() first to overwrite). The
// appended bytes are byte-for-byte identical to the std::string overload.
inline void serialize(const document& doc, std::string& out,
                      const write_config& cfg = {}) {
    detail::serialize_into(doc, cfg, out);
}

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

template <typename T>
inline std::string serialize(const T& obj, const write_config& cfg = {}) {
    document doc;
    const error_code e = rtti::to_value(&doc, obj);
    if (e != error_code::ok) {
        return std::string();
    }
    return serialize(doc, cfg);
}

} // namespace xml
} // namespace rtti