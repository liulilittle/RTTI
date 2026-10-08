// rtti::ini implementation. The non-template definitions declared by
// <rtti/ini.hpp>: the detail text helpers and pool_alloc, the value_io member
// functions of detail::node and document, open_section, the document parser,
// the streaming (SAX) parse_stream, and the document serialization helpers and
// overloads. Behavior is identical to the former inline header definitions.
// English comments only; C++17; no third-party dependencies.

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <rtti/ini.hpp>

namespace rtti::ini {
namespace detail {

bool is_ws(char c) noexcept {
    return c == ' ' || c == '\t';
}

rtti::string_view trim_ws(rtti::string_view s) noexcept {
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
bool parse_quoted(rtti::string_view s, std::string& out,
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

bool key_needs_quote(rtti::string_view k) noexcept {
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

bool value_needs_quote(rtti::string_view s) noexcept {
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

std::string quote_string(rtti::string_view s) {
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

std::string quote_key(rtti::string_view k) {
    return key_needs_quote(k) ? quote_string(k) : static_cast<std::string>(k);
}

std::string quote_value(rtti::string_view s) {
    return value_needs_quote(s) ? quote_string(s) : static_cast<std::string>(s);
}

node* pool_alloc(pool* p) {
    p->nodes.push_back(std::unique_ptr<node>(new node(p)));
    return p->nodes.back().get();
}

node::node(pool* o) noexcept : owner(o) {}

bool node::is_scalar() const noexcept {
    return r == role_t::scalar;
}

bool node::is_object() const noexcept {
    return r == role_t::object;
}

bool node::is_array() const noexcept {
    return r == role_t::array;
}

rtti::string_view node::text() const noexcept {
    return rtti::string_view(owned.data(), owned.size());
}

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

const node* node::scalar_view() const noexcept {
    if (is_scalar()) {
        return this;
    }
    if (is_array() && !elems.empty() && elems.back()->is_scalar()) {
        return elems.back();  // later-wins for scalar reads on array slots
    }
    return nullptr;
}

bool node::as_bool(bool& out) const noexcept {
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

bool node::as_integer(std::int64_t& out) const noexcept {
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
    if (errno == ERANGE || end == t.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<std::int64_t>(v);
    return true;
}

bool node::as_real(double& out) const noexcept {
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
    if (errno == ERANGE || end == t.c_str() || *end != '\0') {
        return false;
    }
    out = v;
    return true;
}

bool node::as_string(rtti::string_view& out) const noexcept {
    const node* n = scalar_view();
    if (n == nullptr) {
        return false;
    }
    out = n->text();
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
}rtti::value_io* node::make_array() {
    r = role_t::array;
    skind = rtti::field_value::kind::none;
    owned.clear();
    entries.clear();
    elems.clear();
    return this;
}

void node::push_back(rtti::value_io* v) {
    elems.push_back(static_cast<node*>(v));
}

rtti::value_io* node::make_object() {
    r = role_t::object;
    skind = rtti::field_value::kind::none;
    owned.clear();
    entries.clear();
    elems.clear();
    return this;
}

void node::set_key(rtti::string_view name, rtti::value_io* v) {
    set_key_impl(name, static_cast<node*>(v));
}

rtti::value_io* node::make_scalar(rtti::field_value v) {
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

rtti::value_io* node::make_node() {
    return detail::pool_alloc(owner);
}

// Parse a scalar text value into owned storage (parser path).
void node::set_scalar_text(rtti::string_view s) {
    r = role_t::scalar;
    skind = rtti::field_value::kind::string;
    owned.assign(s.data(), s.size());
}

node* node::find_child(const std::string& name) {
    for (auto& e : entries) {
        if (e.name == name) {
            return e.child;
        }
    }
    return nullptr;
}

// Register a child under `name`. A repeated scalar replaces itself with an
// array slot holding every occurrence; a repeated array appends.
void node::set_key_impl(rtti::string_view name, node* child) {
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

void node::replace_child(const std::string& name, node* child) {
    for (auto& e : entries) {
        if (e.name == name) {
            e.child = child;
            return;
        }
    }
    entries.push_back(entry{name, child});
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

} // namespace detail

document::document() {
    pool_ = std::make_unique<detail::pool>();
    root_ = detail::pool_alloc(pool_.get());
    root_->make_object();
}

document::~document() = default;

rtti::value_io::kind document::type() const noexcept {
    return root_->type();
}

bool document::as_bool(bool& out) const noexcept {
    return root_->as_bool(out);
}

bool document::as_integer(std::int64_t& out) const noexcept {
    return root_->as_integer(out);
}

bool document::as_real(double& out) const noexcept {
    return root_->as_real(out);
}

bool document::as_string(rtti::string_view& out) const noexcept {
    return root_->as_string(out);
}

std::size_t document::size() const noexcept {
    return root_->size();
}

rtti::value_io* document::at(std::size_t i) {
    return root_->at(i);
}

rtti::value_io* document::at(std::size_t i) const {
    return root_->at(i);
}

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

rtti::value_io* document::make_array() {
    return root_->make_array();
}

void document::push_back(rtti::value_io* v) {
    root_->push_back(v);
}

rtti::value_io* document::make_object() {
    return root_->make_object();
}

void document::set_key(rtti::string_view k, rtti::value_io* v) {
    root_->set_key(k, v);
}

rtti::value_io* document::make_scalar(rtti::field_value v) {
    return root_->make_scalar(v);
}

rtti::value_io* document::make_node() {
    return root_->make_node();
}

// Open (or extend) the section selected by a dotted path. Repeated final
// components accumulate into an array slot; nested paths under repeated
// sections attach to the last element. `max_depth` caps the number of dotted
// components: a header deeper than the shared parse limit returns nullptr with
// `too_deep` set instead of building an unbounded tree that serialize() would
// later recurse into (stack overflow). On any other failure `too_deep` stays
// false (empty/blank component etc).
detail::node* open_section(detail::node* current, rtti::string_view name,
                           detail::pool* pool, std::size_t max_depth,
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
        if (child == nullptr) {
            detail::node* o = detail::pool_alloc(pool);
            o->make_object();
            current->set_key_impl(comp, o);
            return o;
        }
        if (child->is_object()) {
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
        detail::node* o = detail::pool_alloc(pool);
        o->make_object();
        current->replace_child(cname, o);
        return o;
    }
    return nullptr;
}
// Parse a whole INI document. Scalar text, keys and comments are copied into
// owned storage, so the document owns all of its data and outlives the input.
result<document> parse(rtti::string_view src) {
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
result<void> parse_stream(rtti::string_view src, const parse_events& events) {
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

void emit_comment(std::string& out, const write_config& cfg,
                  rtti::string_view comment) {
    if (cfg.with_comments) {
        out.append(comment.data(), comment.size());
        out.push_back('\n');
    }
}

void write_section_body(std::string& out, const write_config& cfg,
                        const node* sec, const std::string& path);
void write_section_header(std::string& out, const write_config& cfg,
                          const node* sec, rtti::string_view name,
                          const std::string& parent_path);

void write_key_value(std::string& out, rtti::string_view key,
                     const node* n) {
    out += quote_key(key);
    out += " = ";
    out += quote_value(n->text());
    out.push_back('\n');
}

void write_section_header(std::string& out, const write_config& cfg,
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

void write_section_body(std::string& out, const write_config& cfg,
                        const node* sec, const std::string& path) {
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
void serialize(const document& doc, std::string& out, const write_config& cfg) {
    out.clear();
    const detail::node* root = doc.root_;
    for (const std::string& c : root->comments) {
        detail::emit_comment(out, cfg, rtti::string_view(c.data(), c.size()));
    }
    if (root->is_scalar()) {
        out += root->text();
        out.push_back('\n');
        return;
    }
    detail::write_section_body(out, cfg, root, std::string());
}

std::string serialize(const document& doc, const write_config& cfg) {
    std::string out;
    serialize(doc, out, cfg);
    return out;
}

} // namespace rtti::ini
