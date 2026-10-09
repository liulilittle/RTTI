// rtti::toml implementation. The non-template definitions declared by
// <rtti/toml.hpp>: the internal node/pool/parser machinery, the text/parse/
// serialize helpers, the document value_io overrides and the free parse/
// parse_stream/serialize entry points. The parser's fire() member template
// stays class-inline (in this TU) to avoid the MSVC out-of-class template
// limitation; the module-level templates parse_as<T>/serialize<T> remain in
// the header. Behavior is identical to the former inline header definitions.
// English comments only; C++17; no third-party dependencies.

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

#include <rtti/toml.hpp>

namespace rtti {
namespace toml {
namespace detail {

node::node(pool* o) noexcept : owner(o) {}

bool node::is_scalar() const noexcept { return r == role_t::scalar; }

bool node::is_object() const noexcept { return r == role_t::object; }

bool node::is_array() const noexcept { return r == role_t::array; }

rtti::string_view node::text() const noexcept {
    return rtti::string_view(owned.data(), owned.size());
}

node* node::find_child(const std::string& name) {
    for (auto& e : entries) {
        if (e.name == name) {
            return e.child;
        }
    }
    return nullptr;
}

// Replace an existing child (later-wins) or append a new one.
void node::set_child(const std::string& name, node* child) {
    for (auto& e : entries) {
        if (e.name == name) {
            e.child = child;
            return;
        }
    }
    entries.push_back(entry{name, child});
}

node* pool_alloc(pool* p) {
    p->nodes.push_back(std::unique_ptr<node>(new node(p)));
    return p->nodes.back().get();
}

// --- node value_io read -----------------------------------------------------

rtti::value_io::kind node::type() const noexcept {
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

bool node::as_bool(bool& out) const noexcept {
    if (!is_scalar() || skind != rtti::field_value::kind::boolean) {
        return false;
    }
    if (owned == "true") {
        out = true;
        return true;
    }
    if (owned == "false") {
        out = false;
        return true;
    }
    return false;
}
bool node::as_integer(std::int64_t& out) const noexcept {
    if (!is_scalar() || skind != rtti::field_value::kind::integer) {
        return false;
    }
    if (owned.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(owned.c_str(), &end, 10);
    if (errno == ERANGE || end == owned.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<std::int64_t>(v);
    return true;
}

bool node::as_real(double& out) const noexcept {
    if (!is_scalar() || skind != rtti::field_value::kind::real) {
        return false;
    }
    if (owned == "inf") {
        out = HUGE_VAL;
        return true;
    }
    if (owned == "-inf") {
        out = -HUGE_VAL;
        return true;
    }
    if (owned == "nan") {
        out = NAN;
        return true;
    }
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(owned.c_str(), &end);
    if (errno == ERANGE || end == owned.c_str() || *end != '\0') {
        return false;
    }
    out = v;
    return true;
}

bool node::as_string(rtti::string_view& out) const noexcept {
    if (!is_scalar() || skind != rtti::field_value::kind::string) {
        return false;
    }
    out = text();
    return true;
}

std::size_t node::size() const noexcept {
    if (is_object()) {
        return entries.size();
    }
    if (is_array()) {
        return elems.size();
    }
    return 0;
}

rtti::value_io* node::at(std::size_t index) {
    return (is_array() && index < elems.size()) ? elems[index] : nullptr;
}

rtti::value_io* node::at(std::size_t index) const {
    return (is_array() && index < elems.size()) ? elems[index] : nullptr;
}

bool node::get_key(rtti::string_view key, rtti::value_io*& out) {
    return get_key_impl(key, out);
}

bool node::get_key(rtti::string_view key, rtti::value_io*& out) const {
    return get_key_impl(key, out);
}

bool node::get_key_at(std::size_t index, rtti::string_view& key,
                      rtti::value_io*& out) const {
    if (!is_object() || index >= entries.size()) {
        return false;
    }
    key = rtti::string_view(entries[index].name.data(),
                            entries[index].name.size());
    out = entries[index].child;
    return true;
}
// --- node value_io write ----------------------------------------------------

rtti::value_io* node::make_array() {
    r = role_t::array;
    skind = rtti::field_value::kind::none;
    is_datetime = false;
    owned.clear();
    entries.clear();
    elems.clear();
    return this;
}

void node::push_back(rtti::value_io* v) {
    if (is_array()) {
        elems.push_back(static_cast<node*>(v));
    }
}

rtti::value_io* node::make_object() {
    r = role_t::object;
    skind = rtti::field_value::kind::none;
    is_datetime = false;
    owned.clear();
    entries.clear();
    elems.clear();
    return this;
}

void node::set_key(rtti::string_view name, rtti::value_io* v) {
    set_child(std::string(name.data(), name.size()), static_cast<node*>(v));
}

rtti::value_io* node::make_scalar(rtti::field_value v) {
    r = role_t::scalar;
    is_datetime = false;
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
            r = role_t::unset;
            break;
    }
    return this;
}

rtti::value_io* node::make_node() { return detail::pool_alloc(owner); }

// Parser path: store an already-classified scalar.
void node::set_scalar_parsed(rtti::field_value::kind k, bool dt,
                             rtti::string_view s) {
    r = role_t::scalar;
    skind = k;
    is_datetime = dt;
    owned.assign(s.data(), s.size());
}

bool node::get_key_impl(rtti::string_view name, rtti::value_io*& out) const {
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
// --- small text helpers -----------------------------------------------------

bool is_horiz(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r';
}

bool is_bare_key(char c) noexcept {
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
           (u >= '0' && u <= '9') || u == '_' || u == '-';
}

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

void utf8_append(std::uint32_t cp, std::string& out) {
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

std::string strip_underscores(rtti::string_view t) {
    std::string s;
    s.reserve(t.size());
    for (const char c : t) {
        if (c != '_') {
            s.push_back(c);
        }
    }
    return s;
}
// Parse a TOML integer token into its canonical decimal text. Accepts decimal
// (with optional +/- sign) and 0x/0o/0b prefixed forms; underscores between
// digits are tolerated (stripped). Rejects anything else.
bool toml_int_text(const std::string& t, std::string& dec) {
    if (t.empty()) {
        return false;
    }
    const bool signed_tok = (t[0] == '+' || t[0] == '-');
    int base = 10;
    std::size_t digits = signed_tok ? 1 : 0;  // first byte to validate as a digit
    if (!signed_tok && t.size() >= 2 && t[0] == '0') {
        const char p = t[1];
        if (p == 'x' || p == 'X') {
            base = 16;
        } else if (p == 'o' || p == 'O') {
            base = 8;
        } else if (p == 'b' || p == 'B') {
            base = 2;
        }
        if (base != 10) {
            digits = 2;
        }
    }
    for (std::size_t j = digits; j < t.size(); ++j) {
        const unsigned char c = static_cast<unsigned char>(t[j]);
        const bool ok = base == 2   ? (c == '0' || c == '1')
                        : base == 8 ? (c >= '0' && c <= '7')
                        : base == 16
                            ? ((c >= '0' && c <= '9') ||
                               (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
                            : (c >= '0' && c <= '9');
        if (!ok) {
            return false;
        }
    }
    if (t.size() == digits) {
        return false;  // sign with no digits, or empty after a prefix
    }
    // Skip a base prefix: strtoll only understands "0x" itself.
    const char* p = t.c_str();
    const char* num = (base == 10) ? p : p + digits;
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(num, &end, base);
    if (errno == ERANGE || end == num) {
        return false;
    }
    dec = std::to_string(v);
    return true;
}
// Validate a TOML float token grammar (optional sign, integer part, optional
// fraction, optional exponent). Underscores must be stripped first. inf/nan
// are handled by the classify path before this function.
bool toml_float_grammar(rtti::string_view t) {
    std::size_t i = 0;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
        ++i;
    }
    std::size_t ds = i;
    while (i < t.size() && is_digit(t[i])) {
        ++i;
    }
    if (i == ds) {
        return false;  // sign only, no integer part
    }
    if (i < t.size() && t[i] == '.') {
        ++i;
        std::size_t fs = i;
        while (i < t.size() && is_digit(t[i])) {
            ++i;
        }
        if (i == fs) {
            return false;  // '.' with no fractional digits
        }
    }
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
        ++i;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
            ++i;
        }
        std::size_t es = i;
        while (i < t.size() && is_digit(t[i])) {
            ++i;
        }
        if (i == es) {
            return false;  // exponent with no digits
        }
    }
    return i == t.size();
}

// Recognize TOML date/time tokens: full date, local time, offset/local
// datetime, all with optional fractional seconds; text is kept verbatim.
bool is_datetime_token(rtti::string_view s) {
    // Local time HH:MM:SS[.fraction] (no date part).
    if (s.size() >= 8 && is_digit(s[0]) && is_digit(s[1]) && s[2] == ':' &&
        is_digit(s[3]) && is_digit(s[4]) && s[5] == ':' && is_digit(s[6]) &&
        is_digit(s[7])) {
        std::size_t j = 8;
        if (s.size() == j) {
            return true;
        }
        if (s[j] == '.') {
            ++j;
            std::size_t st = j;
            while (j < s.size() && is_digit(s[j])) {
                ++j;
            }
            return j > st && j == s.size();
        }
        return false;
    }
    // Full date YYYY-MM-DD.
    if (s.size() < 10 || !is_digit(s[0]) || !is_digit(s[1]) ||
        !is_digit(s[2]) || !is_digit(s[3]) || s[4] != '-' ||
        !is_digit(s[5]) || !is_digit(s[6]) || s[7] != '-' ||
        !is_digit(s[8]) || !is_digit(s[9])) {
        return false;
    }
    std::size_t i = 10;
    if (s.size() == i) {
        return true;  // bare date
    }
    if (s[i] == 'T' || s[i] == 't' || s[i] == ' ') {
        ++i;
    } else {
        return false;
    }
    // Time HH:MM:SS.
    if (s.size() < i + 8 || !is_digit(s[i]) || !is_digit(s[i + 1]) ||
        s[i + 2] != ':' || !is_digit(s[i + 3]) || !is_digit(s[i + 4]) ||
        s[i + 5] != ':' || !is_digit(s[i + 6]) || !is_digit(s[i + 7])) {
        return false;
    }
    i += 8;
    if (s.size() == i) {
        return true;  // local datetime
    }
    if (s[i] == '.') {
        ++i;
        std::size_t st = i;
        while (i < s.size() && is_digit(s[i])) {
            ++i;
        }
        if (i == st) {
            return false;
        }
        if (s.size() == i) {
            return true;
        }
    }
    // Optional UTC marker or numeric offset.
    if (s[i] == 'Z' || s[i] == 'z') {
        return i + 1 == s.size();
    }
    if (s[i] == '+' || s[i] == '-') {
        return s.size() == i + 6 && is_digit(s[i + 1]) && is_digit(s[i + 2]) &&
               s[i + 3] == ':' && is_digit(s[i + 4]) && is_digit(s[i + 5]);
    }
    return s.size() == i;
}
// --- serializer helpers -----------------------------------------------------

void quote_string_into(rtti::string_view s, std::string& out) {
    static const char hex[] = "0123456789ABCDEF";
    out.push_back('"');
    for (const char b : s) {
        const unsigned char c = static_cast<unsigned char>(b);
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
            case '\t':
                out += "\\t";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                if (c < 0x20 || c == 0x7f) {
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

std::string quote_key(rtti::string_view k) {
    std::string out;
    if (k.empty()) {
        return "\"\"";
    }
    for (const char c : k) {
        if (!is_bare_key(c)) {
            quote_string_into(k, out);
            return out;
        }
    }
    out.assign(k.data(), k.size());
    return out;
}

void scalar_text_into(const node* n, std::string& out) {
    if (n->skind == rtti::field_value::kind::string && !n->is_datetime) {
        quote_string_into(n->text(), out);
    } else {
        out += n->owned;
    }
}
void inline_value_into(const node* n, std::string& out);

void inline_array_into(const node* n, std::string& out) {
    if (!n->is_array()) {
        return;
    }
    out.push_back('[');
    for (std::size_t i = 0; i < n->elems.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        inline_value_into(n->elems[i], out);
    }
    out.push_back(']');
}

void inline_object_into(const node* n, std::string& out) {
    out.push_back('{');
    for (std::size_t i = 0; i < n->entries.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += quote_key(rtti::string_view(n->entries[i].name.data(),
                                           n->entries[i].name.size()));
        out += " = ";
        inline_value_into(n->entries[i].child, out);
    }
    out.push_back('}');
}

void inline_value_into(const node* n, std::string& out) {
    if (n->is_scalar()) {
        scalar_text_into(n, out);
    } else if (n->is_array()) {
        inline_array_into(n, out);
    } else if (n->is_object()) {
        inline_object_into(n, out);
    }
}

bool is_table_array(const node* n) noexcept {
    if (!n->is_array() || n->elems.empty()) {
        return false;
    }
    for (const node* e : n->elems) {
        if (!e->is_object()) {
            return false;
        }
    }
    return true;
}
// --- table/path navigation helpers ------------------------------------------

// Descend into (or create) an intermediate object for a dotted path/header
// under an existing node. Mirrors INI: arrays of tables descend into their
// last element; scalar collisions are replaced by a fresh object.
node* descend_object(node* parent, const std::string& name, pool* pool) {
    node* child = parent->find_child(name);
    if (child == nullptr) {
        node* o = pool_alloc(pool);
        o->make_object();
        parent->set_child(name, o);
        return o;
    }
    if (child->is_object()) {
        return child;
    }
    if (child->is_array()) {
        if (!child->elems.empty() && child->elems.back()->is_object()) {
            return child->elems.back();
        }
        node* o = pool_alloc(pool);
        o->make_object();
        parent->set_child(name, o);
        return o;
    }
    node* o = pool_alloc(pool);
    o->make_object();
    parent->set_child(name, o);
    return o;
}

// Open/reopen the table selected by a dotted path (basic table [a.b]).
node* open_table(node* root, const std::vector<std::string>& comps,
                 pool* pool) {
    node* cur = root;
    for (std::size_t i = 0; i + 1 < comps.size(); ++i) {
        cur = descend_object(cur, comps[i], pool);
    }
    const std::string& last = comps.back();
    node* child = cur->find_child(last);
    if (child == nullptr) {
        node* o = pool_alloc(pool);
        o->make_object();
        cur->set_child(last, o);
        return o;
    }
    if (child->is_object()) {
        return child;  // reopen an existing table
    }
    if (child->is_array() && !child->elems.empty()) {
        return child->elems.back();  // continue the last array-table element
    }
    node* o = pool_alloc(pool);
    o->make_object();
    cur->set_child(last, o);
    return o;
}

// Open the array table named by a dotted path ([[a.b]]): append a new object
// element to the array at the final component.
node* open_array_table(node* root,
                       const std::vector<std::string>& comps,
                       pool* pool) {
    node* cur = root;
    for (std::size_t i = 0; i + 1 < comps.size(); ++i) {
        cur = descend_object(cur, comps[i], pool);
    }
    const std::string& last = comps.back();
    node* child = cur->find_child(last);
    node* arr = nullptr;
    if (child == nullptr) {
        arr = pool_alloc(pool);
        arr->make_array();
        cur->set_child(last, arr);
    } else if (child->is_array()) {
        arr = child;
    } else if (child->is_object()) {
        // Convert an existing single table into an array holding it first.
        arr = pool_alloc(pool);
        arr->make_array();
        arr->elems.push_back(child);
        cur->set_child(last, arr);
    } else {
        arr = pool_alloc(pool);
        arr->make_array();
        cur->set_child(last, arr);
    }
    node* el = pool_alloc(pool);
    el->make_object();
    arr->elems.push_back(el);
    return el;
}

// Attach a value under a dotted key path inside the current table.
void set_dotted_value(node* base,
                      const std::vector<std::string>& comps, node* value,
                      pool* pool) {
    node* cur = base;
    for (std::size_t i = 0; i + 1 < comps.size(); ++i) {
        cur = descend_object(cur, comps[i], pool);
    }
    cur->set_child(comps.back(), value);
}

std::string join_path(const std::vector<std::string>& comps) {
    std::string out;
    for (std::size_t i = 0; i < comps.size(); ++i) {
        if (i != 0) {
            out.push_back('.');
        }
        out += comps[i];
    }
    return out;
}
// --- parser ----------------------------------------------------------------

// Recursive-descent parser. Built in two modes: DOM build (doc != nullptr) and
// streaming (events != nullptr, no tree built). `marker` is the success
// sentinel node in streaming mode. The fire() member template stays class-
// inline because MSVC rejects out-of-class member-template definitions.
struct parser {
    string_view src;
    std::size_t pos = 0;
    document* doc = nullptr;
    const parse_events* events = nullptr;
    bool cancelled = false;
    std::size_t max_depth = 512;
    node* current = nullptr;  // DOM: active table
    node stream_marker{nullptr};
    error_info err;

    // Streaming section tracking.
    std::string cur_tab;
    bool in_tab = false;
    bool arr_tab = false;

    error_info fail(error_code code, std::size_t off) const;

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

    node* new_value_node();
    void skip_horiz();
    void skip_comment();
    void skip_ws_comments();
    bool expect_eol();
    bool read_component(std::string& out);
    bool read_path(std::vector<std::string>& comps);
    bool decode_u_hex(std::string& out);
    bool decode_escape(std::string& out);
    bool read_single_basic(std::string& out);
    bool read_multi_basic(std::string& out);
    bool read_single_literal(std::string& out);
    bool read_multi_literal(std::string& out);
    bool classify_bare(const std::string& token, node* out);
    bool fire_scalar_node(const node* n);
    double node_real(const node* n) const;
    bool parse_bare_scalar(node*& out);
    bool parse_array(node*& out, std::size_t depth);
    bool parse_inline_table(node*& out, std::size_t depth);
    bool parse_value(node*& out, std::size_t depth);
    bool parse_key_value();
    bool parse_table_header();
    bool open_stream_table(const std::string& name, bool arr);
    bool close_stream_table();
    bool run();
};

error_info parser::fail(error_code code, std::size_t off) const {
    error_info e;
    e.code = code;
    e.src = src.data();
    e.src_len = src.size();
    e.offset = off;
    return e;
}

node* parser::new_value_node() {
    return events != nullptr ? &stream_marker : pool_alloc(doc->pool_.get());
}

// Skip spaces/tabs/CR (structural whitespace; never newlines).
void parser::skip_horiz() {
    while (pos < src.size() && is_horiz(src[pos])) {
        ++pos;
    }
}

void parser::skip_comment() {
    while (pos < src.size() && src[pos] != '\n') {
        ++pos;
    }
}

// Skip whitespace (including newlines) and full-line/trailing comments.
void parser::skip_ws_comments() {
    for (;;) {
        while (pos < src.size() &&
               (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' ||
                src[pos] == '\n')) {
            ++pos;
        }
        if (pos < src.size() && src[pos] == '#') {
            skip_comment();
            continue;
        }
        break;
    }
}

// A key/value statement or a table header must end at a comment, a newline
// or the end of input.
bool parser::expect_eol() {
    skip_horiz();
    if (pos < src.size() && src[pos] == '#') {
        skip_comment();
        skip_horiz();
    }
    if (pos >= src.size()) {
        return true;
    }
    if (src[pos] == '\n') {
        ++pos;
        return true;
    }
    err = fail(error_code::unexpected_token, pos);
    return false;
}
// Read one key component: bare or quoted (single-line) form.
bool parser::read_component(std::string& out) {
    if (pos >= src.size()) {
        return false;
    }
    const char c = src[pos];
    if (c == '"') {
        return read_single_basic(out);
    }
    if (c == '\'') {
        return read_single_literal(out);
    }
    if (c == '[' || c == '{' || c == ']' || c == '}' || c == '=' ||
        c == '.' || c == ',') {
        return false;
    }
    out.clear();
    while (pos < src.size() && is_bare_key(src[pos])) {
        out.push_back(src[pos]);
        ++pos;
    }
    return true;
}

// Read a dotted key path (components separated by '.'; whitespace around
// dots allowed). Returns false on an empty component or a truncated path.
// The component count is capped at the parser max_depth: a deeper table or
// key path would create nesting that emit_object() recurse into, so it is
// reported as error_code::too_deep instead of building the tree.
bool parser::read_path(std::vector<std::string>& comps) {
    for (;;) {
        skip_horiz();
        std::string c;
        if (!read_component(c)) {
            return false;
        }
        if (c.empty()) {
            return false;  // empty component (e.g. "a..b", "[]")
        }
        comps.push_back(std::move(c));
        if (comps.size() > max_depth) {
            err = fail(error_code::too_deep, pos);
            return false;
        }
        skip_horiz();
        if (pos < src.size() && src[pos] == '.') {
            ++pos;
            continue;
        }
        break;
    }
    return true;
}

// --- strings ------------------------------------------------------------

bool parser::decode_u_hex(std::string& out) {
    const std::size_t esc = pos - 2;  // backslash of "\u"
    if (pos + 4 > src.size()) {
        err = fail(error_code::invalid_utf8, esc);
        return false;
    }
    std::uint32_t cp = 0;
    for (int k = 0; k < 4; ++k) {
        const char h = src[pos + static_cast<std::size_t>(k)];
        std::uint32_t v = 0;
        if (h >= '0' && h <= '9') {
            v = static_cast<std::uint32_t>(h - '0');
        } else if (h >= 'a' && h <= 'f') {
            v = static_cast<std::uint32_t>(h - 'a' + 10);
        } else if (h >= 'A' && h <= 'F') {
            v = static_cast<std::uint32_t>(h - 'A' + 10);
        } else {
            err = fail(error_code::invalid_utf8,
                       pos + static_cast<std::size_t>(k));
            return false;
        }
        cp = (cp << 4) | v;
    }
    pos += 4;
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (pos + 6 > src.size() || src[pos] != '\\' || src[pos + 1] != 'u') {
            err = fail(error_code::invalid_utf8, esc);
            return false;
        }
        std::uint32_t lo = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = src[pos + 2 + static_cast<std::size_t>(k)];
            std::uint32_t v = 0;
            if (h >= '0' && h <= '9') {
                v = static_cast<std::uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                v = static_cast<std::uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                v = static_cast<std::uint32_t>(h - 'A' + 10);
            } else {
                err = fail(error_code::invalid_utf8,
                           pos + 2 + static_cast<std::size_t>(k));
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
        err = fail(error_code::invalid_utf8, esc);
        return false;
    }
    utf8_append(cp, out);
    return true;
}
bool parser::decode_escape(std::string& out) {
    if (pos >= src.size()) {
        err = fail(error_code::unterminated_string, pos);
        return false;
    }
    const char e = src[pos];
    ++pos;
    switch (e) {
        case 'b':
            out.push_back('\b');
            return true;
        case 't':
            out.push_back('\t');
            return true;
        case 'n':
            out.push_back('\n');
            return true;
        case 'f':
            out.push_back('\f');
            return true;
        case 'r':
            out.push_back('\r');
            return true;
        case '"':
            out.push_back('"');
            return true;
        case '\\':
            out.push_back('\\');
            return true;
        case 'u':
            return decode_u_hex(out);
    }
    err = fail(error_code::unexpected_token, pos - 1);
    return false;
}

bool parser::read_single_basic(std::string& out) {
    const std::size_t open = pos;
    ++pos;  // opening quote
    out.clear();
    for (;;) {
        if (pos >= src.size()) {
            err = fail(error_code::unterminated_string, open);
            return false;
        }
        const char c = src[pos];
        if (c == '"') {
            ++pos;
            return true;
        }
        if (c == '\\') {
            ++pos;
            if (!decode_escape(out)) {
                return false;
            }
            continue;
        }
        if (c == '\n' || c == '\r' ||
            (static_cast<unsigned char>(c) < 0x20 && c != '\t')) {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        out.push_back(c);
        ++pos;
    }
}

bool parser::read_multi_basic(std::string& out) {
    const std::size_t open = pos;
    pos += 3;  // opening triple quote
    if (pos < src.size() && src[pos] == '\r') {
        ++pos;
    }
    if (pos < src.size() && src[pos] == '\n') {
        ++pos;  // trim the newline right after the opening delimiter
    }
    out.clear();
    for (;;) {
        if (pos >= src.size()) {
            err = fail(error_code::unterminated_string, open);
            return false;
        }
        const char c = src[pos];
        if (c == '"') {
            std::size_t q = pos;
            while (q < src.size() && src[q] == '"') {
                ++q;
            }
            const std::size_t run = q - pos;
            if (run >= 3) {
                pos = q;  // closing delimiter
                return true;
            }
            out.append(run, '"');
            pos = q;
            continue;
        }
        if (c == '\\') {
            ++pos;
            if (pos < src.size() && src[pos] == '\r') {
                ++pos;
            }
            if (pos < src.size() && src[pos] == '\n') {
                ++pos;  // line-ending backslash trims following whitespace
                while (pos < src.size() && (src[pos] == ' ' ||
                                            src[pos] == '\t' ||
                                            src[pos] == '\r' ||
                                            src[pos] == '\n')) {
                    ++pos;
                }
                continue;
            }
            if (!decode_escape(out)) {
                return false;
            }
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20 && c != '\n' &&
            c != '\t' && c != '\r') {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        out.push_back(c);
        ++pos;
    }
}
bool parser::read_single_literal(std::string& out) {
    const std::size_t open = pos;
    ++pos;  // opening quote
    out.clear();
    for (;;) {
        if (pos >= src.size()) {
            err = fail(error_code::unterminated_string, open);
            return false;
        }
        const char c = src[pos];
        if (c == '\'') {
            ++pos;
            return true;
        }
        if (c == '\n' || c == '\r' ||
            (static_cast<unsigned char>(c) < 0x20 && c != '\t')) {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        out.push_back(c);
        ++pos;
    }
}

bool parser::read_multi_literal(std::string& out) {
    const std::size_t open = pos;
    pos += 3;  // opening triple quote
    if (pos < src.size() && src[pos] == '\r') {
        ++pos;
    }
    if (pos < src.size() && src[pos] == '\n') {
        ++pos;  // trim the newline right after the opening delimiter
    }
    out.clear();
    for (;;) {
        if (pos >= src.size()) {
            err = fail(error_code::unterminated_string, open);
            return false;
        }
        const char c = src[pos];
        if (c == '\'') {
            std::size_t q = pos;
            while (q < src.size() && src[q] == '\'') {
                ++q;
            }
            const std::size_t run = q - pos;
            if (run >= 3) {
                pos = q;  // closing delimiter
                return true;
            }
            out.append(run, '\'');
            pos = q;
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20 && c != '\n' &&
            c != '\t' && c != '\r') {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        out.push_back(c);
        ++pos;
    }
}

// --- scalar classification --------------------------------------------

// Classify a bare (unquoted) TOML value token: boolean, integer (kept as
// canonical decimal text), float (kept as format_real text) or date/time
// (kept verbatim, unquoted on output).
bool parser::classify_bare(const std::string& token, node* out) {
    std::string text;
    if (token == "true") {
        out->set_scalar_parsed(rtti::field_value::kind::boolean, false,
                               rtti::string_view("true"));
        return true;
    }
    if (token == "false") {
        out->set_scalar_parsed(rtti::field_value::kind::boolean, false,
                               rtti::string_view("false"));
        return true;
    }
    text = strip_underscores(rtti::string_view(token.data(), token.size()));
    std::string dec;
    if (toml_int_text(text, dec)) {
        out->set_scalar_parsed(rtti::field_value::kind::integer, false,
                               rtti::string_view(dec.data(), dec.size()));
        return true;
    }
    if (text == "inf" || text == "-inf") {
        out->set_scalar_parsed(rtti::field_value::kind::real, false,
                               rtti::string_view(text.data(), text.size()));
        return true;
    }
    if (text == "+inf") {
        out->set_scalar_parsed(rtti::field_value::kind::real, false,
                               rtti::string_view("inf"));
        return true;
    }
    if (text == "nan") {
        out->set_scalar_parsed(rtti::field_value::kind::real, false,
                               rtti::string_view("nan"));
        return true;
    }
    if (toml_float_grammar(text)) {
        const double v = std::strtod(text.c_str(), nullptr);
        std::string canon;
        rtti::detail::format_real(v, canon);
        out->set_scalar_parsed(rtti::field_value::kind::real, false,
                               rtti::string_view(canon.data(), canon.size()));
        return true;
    }
    if (is_datetime_token(token)) {
        // Date/time values are kept verbatim as string scalars (marked so
        // the serializer emits them unquoted, preserving the text exactly).
        out->set_scalar_parsed(rtti::field_value::kind::string, true,
                               rtti::string_view(token.data(), token.size()));
        return true;
    }
    return false;
}

// Fire the typed scalar event for a scalar node (streaming mode).
bool parser::fire_scalar_node(const node* n) {
    if (events == nullptr) {
        return true;
    }
    switch (n->skind) {
        case rtti::field_value::kind::boolean:
            return fire(events->bool_value, n->owned == "true");
        case rtti::field_value::kind::integer:
            return fire(events->integer_value, static_cast<std::int64_t>(
                          std::strtoll(n->owned.c_str(), nullptr, 10)));
        case rtti::field_value::kind::real:
            return fire(events->real_value, node_real(n));
        case rtti::field_value::kind::string:
            return fire(events->string_value, n->text());
        default:
            return true;
    }
}

double parser::node_real(const node* n) const {
    if (n->owned == "inf") {
        return HUGE_VAL;
    }
    if (n->owned == "-inf") {
        return -HUGE_VAL;
    }
    if (n->owned == "nan") {
        return NAN;
    }
    return std::strtod(n->owned.c_str(), nullptr);
}
bool parser::parse_bare_scalar(node*& out) {
    const std::size_t start = pos;
    std::string token;
    while (pos < src.size() &&
           !(is_horiz(src[pos]) || src[pos] == '\n' || src[pos] == '#' ||
             src[pos] == ',' || src[pos] == ']' || src[pos] == '}')) {
        token.push_back(src[pos]);
        ++pos;
    }
    if (token.empty()) {
        err = fail(error_code::unexpected_token, pos);
        return false;
    }
    node* n = new_value_node();
    if (!classify_bare(token, n)) {
        const bool num_like =
            is_digit(token[0]) || token[0] == '+' || token[0] == '-';
        err = fail(num_like ? error_code::invalid_number
                            : error_code::unexpected_token,
                   start);
        return false;
    }
    if (!fire_scalar_node(n)) {
        return false;
    }
    out = n;
    return true;
}

bool parser::parse_array(node*& out, std::size_t depth) {
    if (depth >= max_depth) {
        err = fail(error_code::too_deep, pos);
        return false;
    }
    ++pos;  // '['
    node* n = new_value_node();
    if (events != nullptr) {
        if (!fire(events->array_start)) {
            return false;
        }
    } else {
        n->make_array();
    }
    for (;;) {
        skip_ws_comments();
        if (pos >= src.size()) {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        if (src[pos] == ',' || src[pos] == '}') {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        if (src[pos] == ']') {
            ++pos;
            if (events != nullptr && !fire(events->array_end)) {
                return false;
            }
            out = n;
            return true;
        }
        node* child = nullptr;
        if (!parse_value(child, depth + 1)) {
            return false;
        }
        if (events == nullptr) {
            n->push_back(child);
        }
        skip_ws_comments();
        if (pos >= src.size()) {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        if (src[pos] == ',') {
            ++pos;
            continue;
        }
        if (src[pos] == ']') {
            ++pos;
            if (events != nullptr && !fire(events->array_end)) {
                return false;
            }
            out = n;
            return true;
        }
        err = fail(error_code::unexpected_token, pos);
        return false;
    }
}

bool parser::parse_inline_table(node*& out, std::size_t depth) {
    if (depth >= max_depth) {
        err = fail(error_code::too_deep, pos);
        return false;
    }
    ++pos;  // '{'
    node* n = new_value_node();
    if (events == nullptr) {
        n->make_object();
    }
    skip_ws_comments();
    if (pos < src.size() && src[pos] == '}') {
        ++pos;
        out = n;
        return true;
    }
    for (;;) {
        skip_ws_comments();
        if (pos >= src.size() || src[pos] == ',' || src[pos] == '}') {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        std::vector<std::string> comps;
        if (!read_path(comps) || comps.empty()) {
            if (err.code != error_code::too_deep) {
                err = fail(error_code::unexpected_token, pos);
            }
            return false;
        }
        skip_horiz();
        if (pos >= src.size() || src[pos] != '=') {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        ++pos;
        skip_ws_comments();
        node* child = nullptr;
        if (!parse_value(child, depth + 1)) {
            return false;
        }
        if (events == nullptr) {
            set_dotted_value(n, comps, child, doc->pool_.get());
        }
        skip_ws_comments();
        if (pos >= src.size()) {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        if (src[pos] == ',') {
            ++pos;
            continue;
        }
        if (src[pos] == '}') {
            ++pos;
            out = n;
            return true;
        }
        err = fail(error_code::unexpected_token, pos);
        return false;
    }
}

bool parser::parse_value(node*& out, std::size_t depth) {
    if (pos >= src.size()) {
        err = fail(error_code::unexpected_token, pos);
        return false;
    }
    const char c = src[pos];
    if (c == '[') {
        return parse_array(out, depth);
    }
    if (c == '{') {
        return parse_inline_table(out, depth);
    }
    if (c == '"') {
        std::string decoded;
        if (pos + 2 < src.size() && src[pos + 1] == '"' &&
            src[pos + 2] == '"') {
            if (!read_multi_basic(decoded)) {
                return false;
            }
        } else if (!read_single_basic(decoded)) {
            return false;
        }
        node* n = new_value_node();
        if (events == nullptr) {
            n->set_scalar_parsed(rtti::field_value::kind::string, false,
                                 rtti::string_view(decoded.data(),
                                                   decoded.size()));
        }
        if (events != nullptr &&
            !fire(events->string_value,
                  rtti::string_view(decoded.data(), decoded.size()))) {
            return false;
        }
        out = n;
        return true;
    }
    if (c == '\'') {
        std::string decoded;
        if (pos + 2 < src.size() && src[pos + 1] == '\'' &&
            src[pos + 2] == '\'') {
            if (!read_multi_literal(decoded)) {
                return false;
            }
        } else if (!read_single_literal(decoded)) {
            return false;
        }
        node* n = new_value_node();
        if (events == nullptr) {
            n->set_scalar_parsed(rtti::field_value::kind::string, false,
                                 rtti::string_view(decoded.data(),
                                                   decoded.size()));
        }
        if (events != nullptr &&
            !fire(events->string_value,
                  rtti::string_view(decoded.data(), decoded.size()))) {
            return false;
        }
        out = n;
        return true;
    }
    return parse_bare_scalar(out);
}
// --- statements -----------------------------------------------------------

bool parser::parse_key_value() {
    std::vector<std::string> comps;
    if (!read_path(comps) || comps.empty()) {
        if (err.code != error_code::too_deep) {
            err = fail(error_code::unexpected_token, pos);
        }
        return false;
    }
    skip_horiz();
    if (pos >= src.size() || src[pos] != '=') {
        err = fail(error_code::unexpected_token, pos);
        return false;
    }
    ++pos;
    skip_ws_comments();
    if (events != nullptr && !in_tab) {
        if (!open_stream_table(std::string(), false)) {
            return false;
        }
    }
    const std::size_t v0 = pos;
    node* v = nullptr;
    if (!parse_value(v, 0)) {
        return false;
    }
    const rtti::string_view raw = src.substr(v0, pos - v0);
    if (!expect_eol()) {
        return false;
    }
    if (events != nullptr) {
        const std::string path = join_path(comps);
        const rtti::string_view pv(path.data(), path.size());
        if (!fire(events->key_value, pv, raw)) {
            return false;
        }
    } else {
        set_dotted_value(current, comps, v, doc->pool_.get());
    }
    return true;
}

bool parser::parse_table_header() {
    const std::size_t hdr = pos;
    ++pos;  // '['
    bool is_arr = false;
    skip_horiz();
    if (pos < src.size() && src[pos] == '[') {
        is_arr = true;
        ++pos;
        skip_horiz();
    }
    std::vector<std::string> comps;
    if (!read_path(comps) || comps.empty()) {
        if (err.code != error_code::too_deep) {
            err = fail(error_code::unexpected_token, hdr);
        }
        return false;
    }
    skip_horiz();
    if (pos >= src.size() || src[pos] != ']') {
        err = fail(error_code::unexpected_token, pos);
        return false;
    }
    ++pos;
    if (is_arr) {
        skip_horiz();
        if (pos >= src.size() || src[pos] != ']') {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        ++pos;
    }
    if (!expect_eol()) {
        return false;
    }
    if (events != nullptr) {
        if (!close_stream_table()) {
            return false;
        }
        if (!open_stream_table(join_path(comps), is_arr)) {
            return false;
        }
    } else {
        current = is_arr
                      ? open_array_table(doc->root_, comps, doc->pool_.get())
                      : open_table(doc->root_, comps, doc->pool_.get());
    }
    return true;
}

bool parser::open_stream_table(const std::string& name, bool arr) {
    in_tab = true;
    arr_tab = arr;
    cur_tab = name;
    const rtti::string_view v(cur_tab.data(), cur_tab.size());
    if (arr) {
        return fire(events->array_table_start, v);
    }
    return fire(events->table_start, v);
}

bool parser::close_stream_table() {
    if (!in_tab) {
        return true;
    }
    in_tab = false;
    const rtti::string_view v(cur_tab.data(), cur_tab.size());
    if (arr_tab) {
        return fire(events->array_table_end, v);
    }
    return fire(events->table_end, v);
}

bool parser::run() {
    skip_ws_comments();
    if (pos >= src.size()) {
        err = fail(error_code::invalid_root, 0);
        return false;
    }
    if (events == nullptr) {
        current = doc->root_;
    }
    bool any = false;
    for (;;) {
        skip_ws_comments();
        if (pos >= src.size()) {
            break;
        }
        any = true;
        if (src[pos] == '[') {
            if (!parse_table_header()) {
                return false;
            }
        } else if (!parse_key_value()) {
            return false;
        }
    }
    if (!any) {
        err = fail(error_code::invalid_root, 0);
        return false;
    }
    if (events != nullptr) {
        return close_stream_table();
    }
    return true;
}
// --- serialization ----------------------------------------------------------

// Recursively write an object node as a TOML table body. Scalar keys and
// inline array values are written first, then sub-tables ([path]) and array
// tables ([[path]]).
void emit_object(std::string& out, const node* sec,
                 const std::string& path) {
    for (const auto& e : sec->entries) {
        const node* c = e.child;
        if (c->is_scalar() || (c->is_array() && !is_table_array(c))) {
            out += quote_key(
                rtti::string_view(e.name.data(), e.name.size()));
            out += " = ";
            if (c->is_scalar()) {
                scalar_text_into(c, out);
            } else {
                inline_array_into(c, out);
            }
            out.push_back('\n');
        }
    }
    for (const auto& e : sec->entries) {
        const node* c = e.child;
        const std::string full =
            path.empty()
                ? quote_key(rtti::string_view(e.name.data(), e.name.size()))
                : (path + "." +
                   quote_key(rtti::string_view(e.name.data(), e.name.size())));
        if (c->is_object()) {
            out.push_back('[');
            out += full;
            out += "]\n";
            emit_object(out, c, full);
        } else if (is_table_array(c)) {
            for (const node* el : c->elems) {
                out += "[[";
                out += full;
                out += "]]\n";
                emit_object(out, el, full);
            }
        }
    }
}

} // namespace detail

document::document() {
    pool_ = std::make_unique<detail::pool>();
    root_ = detail::pool_alloc(pool_.get());
    root_->make_object();
}

document::~document() = default;

document::document(document&&) noexcept = default;

document& document::operator=(document&&) noexcept = default;

rtti::value_io::kind document::type() const noexcept { return root_->type(); }

bool document::as_bool(bool& out) const noexcept { return root_->as_bool(out); }

bool document::as_integer(std::int64_t& out) const noexcept {
    return root_->as_integer(out);
}

bool document::as_real(double& out) const noexcept { return root_->as_real(out); }

bool document::as_string(rtti::string_view& out) const noexcept {
    return root_->as_string(out);
}

std::size_t document::size() const noexcept { return root_->size(); }

rtti::value_io* document::at(std::size_t i) { return root_->at(i); }

rtti::value_io* document::at(std::size_t i) const { return root_->at(i); }

bool document::get_key(rtti::string_view k, rtti::value_io*& out) {
    return root_->get_key(k, out);
}

bool document::get_key(rtti::string_view k, rtti::value_io*& out) const {
    return root_->get_key(k, out);
}

bool document::get_key_at(std::size_t i, rtti::string_view& k,
                          rtti::value_io*& out) const {
    return root_->get_key_at(i, k, out);
}

rtti::value_io* document::make_array() { return root_->make_array(); }

void document::push_back(rtti::value_io* v) { root_->push_back(v); }

rtti::value_io* document::make_object() { return root_->make_object(); }

void document::set_key(rtti::string_view k, rtti::value_io* v) {
    root_->set_key(k, v);
}

rtti::value_io* document::make_scalar(rtti::field_value v) {
    return root_->make_scalar(v);
}

rtti::value_io* document::make_node() { return root_->make_node(); }
// Parse `src` into a document. Returns error_info with a byte offset on
// failure.
result<document> parse(string_view src, const parse_config& cfg) {
    result<document> r;
    document doc;
    detail::parser p;
    p.src = src;
    p.doc = &doc;
    p.events = nullptr;
    p.max_depth = cfg.max_depth;
    if (!p.run()) {
        r.err = p.err;
        return r;
    }
    r.value = std::move(doc);
    return r;
}

result<document> parse(string_view src) { return parse(src, parse_config{}); }

// Streaming parse: `src` is pulled through `events` without building a tree.
// Returns ok on a full successful parse; a callback returning false cancels
// the parse and also yields ok (an early stop is not an error). Syntax errors
// yield the usual error_info.
result<void> parse_stream(string_view src, const parse_events& events) {
    result<void> r;
    detail::parser p;
    p.src = src;
    p.doc = nullptr;
    p.events = &events;
    p.max_depth = 512;
    if (!p.run()) {
        if (p.cancelled) {
            return r;
        }
        r.err = p.err;
        return r;
    }
    return r;
}

// Serialize into a preallocated user buffer. The buffer's content is replaced
// (not appended): each call leaves `out` holding exactly this document's text,
// identical to what serialize(doc, cfg) returns. Previously reserved capacity
// is kept, so callers can reserve() once and reuse the buffer.
void serialize(const document& doc, std::string& out,
               const write_config& cfg) {
    out.clear();
    (void)cfg;
    if (doc.root_->is_object()) {
        detail::emit_object(out, doc.root_, std::string());
    }
}

std::string serialize(const document& doc, const write_config& cfg) {
    std::string out;
    serialize(doc, out, cfg);
    return out;
}

} // namespace toml
} // namespace rtti
