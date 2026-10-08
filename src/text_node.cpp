// Non-template implementation of rtti::detail::text_node and its compact
// single-line serializer / JSON parser (spec §16.2). These definitions were
// moved out of include/rtti/to_string.hpp so the string layer's own minimal
// value_io tree compiles once instead of once per translation unit.
//
// English comments only; C++17; no third-party dependencies.

#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <rtti/to_string.hpp>

namespace rtti {
namespace detail {

// --- text_node value_io members ----------------------------------------------

text_node::~text_node() = default;

value_io::kind text_node::type() const noexcept {
    return k;
}

bool text_node::as_bool(bool& out) const noexcept {
    if (k != value_io::kind::boolean) {
        return false;
    }
    out = b;
    return true;
}

bool text_node::as_integer(std::int64_t& out) const noexcept {
    if (k != value_io::kind::integer) {
        return false;
    }
    out = i;
    return true;
}

bool text_node::as_real(double& out) const noexcept {
    // Integer nodes demote to real so "42" fills a double field (same lane
    // demotion as json.hpp's node).
    if (k == value_io::kind::integer) {
        out = static_cast<double>(i);
        return true;
    }
    if (k != value_io::kind::real) {
        return false;
    }
    out = d;
    return true;
}

bool text_node::as_string(string_view& out) const noexcept {
    if (k != value_io::kind::string) {
        return false;
    }
    out = string_view(s.data(), s.size());
    return true;
}

std::size_t text_node::size() const noexcept {
    return k == value_io::kind::array   ? arr.size()
           : k == value_io::kind::object ? obj.size()
                                         : 0;
}

value_io* text_node::at(std::size_t index) {
    return (k == value_io::kind::array && index < arr.size())
               ? arr[index].get()
               : nullptr;
}

value_io* text_node::at(std::size_t index) const {
    return (k == value_io::kind::array && index < arr.size())
               ? arr[index].get()
               : nullptr;
}

bool text_node::get_key(string_view key, value_io*& out) {
    return get_key_impl(key, out);
}

bool text_node::get_key(string_view key, value_io*& out) const {
    return get_key_impl(key, out);
}

bool text_node::get_key_at(std::size_t index, string_view& key,
                           value_io*& out) const {
    if (k != value_io::kind::object || index >= obj.size()) {
        return false;
    }
    key = string_view(obj[index].first);
    out = obj[index].second.get();
    return true;
}

value_io* text_node::make_array() {
    k = value_io::kind::array;
    return this;
}

void text_node::push_back(value_io* v) {
    if (k == value_io::kind::array) {
        arr.emplace_back(adopt_child(static_cast<text_node*>(v)));
    }
}

value_io* text_node::make_object() {
    k = value_io::kind::object;
    return this;
}

void text_node::set_key(string_view key, value_io* v) {
    if (k == value_io::kind::object) {
        obj.emplace_back(std::string(key),
                         adopt_child(static_cast<text_node*>(v)));
    }
}

value_io* text_node::make_scalar(field_value v) {
    switch (v.type()) {
        case field_value::kind::boolean:
            k = value_io::kind::boolean;
            b = v.as_bool();
            break;
        case field_value::kind::integer:
            k = value_io::kind::integer;
            i = v.as_integer();
            break;
        case field_value::kind::real:
            k = value_io::kind::real;
            d = v.as_real();
            break;
        case field_value::kind::string: {
            const string_view sv = v.as_string();
            if (sv.size() == 0) {
                s.clear();
            } else {
                s.assign(sv.data(), sv.size());
            }
            k = value_io::kind::string;
            break;
        }
        default:
            k = value_io::kind::null;
            break; // none / object -> null
    }
    return this;
}

value_io* text_node::make_node() {
    pending_.emplace_back(new text_node);
    return pending_.back().get();
}

std::unique_ptr<text_node> text_node::adopt_child(text_node* c) {
    // A child created by make_node() is owned by this node from birth (held in
    // pending_); push_back/set_key transfer that ownership to the arr/obj
    // containers. A conversion failure between make_node() and the attach
    // (convert.hpp returns its error without attaching) therefore cannot leak
    // the child: it is reclaimed with this node. Nodes built by the parser
    // (parse_text) arrive with ownership already transferred (child.release())
    // and are adopted through the fallback path.
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->get() == c) {
            std::unique_ptr<text_node> owned = std::move(*it);
            pending_.erase(it);
            return owned;
        }
    }
    return std::unique_ptr<text_node>(c);
}

bool text_node::get_key_impl(string_view key, value_io*& out) const {
    if (k != value_io::kind::object) {
        return false;
    }
    for (const auto& e : obj) {
        if (e.first == key) {
            out = e.second.get();
            return true;
        }
    }
    return false;
}
// --- Compact single-line serializer ------------------------------------------
// Deterministic: arrays/objects emit in source order, no sort_keys.
// Non-finite reals emit the bare tokens "inf"/"-inf"/"nan" so container
// floats round-trip exactly (the JSON adapters map them to null; here exact
// round-trip is more useful and the string layer owns its parser).

void append_text_string(const char* data, std::size_t len, std::string& out) {
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    out.push_back('"');
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char c = p[i];
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

void serialize_text(const text_node& n, std::string& out) {
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
            rtti::detail::format_real(n.d, out);
            break;
        case value_io::kind::string:
            append_text_string(n.s.data(), n.s.size(), out);
            break;
        case value_io::kind::array: {
            out.push_back('[');
            for (std::size_t i = 0; i < n.arr.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                serialize_text(*n.arr[i], out);
            }
            out.push_back(']');
            break;
        }
        case value_io::kind::object: {
            out.push_back('{');
            for (std::size_t i = 0; i < n.obj.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                const char* kp = n.obj[i].first.data();
                append_text_string(kp, n.obj[i].first.size(), out);
                out.push_back(':');
                serialize_text(*n.obj[i].second, out);
            }
            out.push_back('}');
            break;
        }
    }
}
// --- Compact JSON parser -----------------------------------------------------
// Recursive descent over the productions the serializer emits (plus tolerant
// whitespace and full \u escape decoding). On failure err carries the error
// code and byte offset; the partially built tree is freed with the unique_ptrs.

struct text_parser {
    static constexpr std::size_t kMaxDepth = 512;

    string_view src;
    std::size_t pos = 0;
    error_info err;

    error_info fail(error_code code, std::size_t off) const {
        error_info e;
        e.code = code;
        e.src = src.data();
        e.src_len = src.size();
        e.offset = off;
        return e;
    }

    void skip_ws() noexcept {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' ||
                                    src[pos] == '\n' || src[pos] == '\r')) {
            ++pos;
        }
    }

    static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    std::unique_ptr<text_node> parse_value(std::size_t depth);
    std::unique_ptr<text_node> parse_object(std::size_t depth);
    std::unique_ptr<text_node> parse_array(std::size_t depth);
    std::unique_ptr<text_node> parse_number();
    std::unique_ptr<text_node> parse_literal();
    std::unique_ptr<text_node> parse_string_node();
    bool scan_string(string_view& direct, bool& escaped, std::string& decoded);
    bool decode_u_hex(std::string& out);
    void utf8_encode(std::uint32_t cp, std::string& out);
};

std::unique_ptr<text_node> text_parser::parse_value(std::size_t depth) {
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
            if (pos + 4 <= src.size() && src.compare(pos, 4, "-inf") == 0) {
                return parse_literal();
            }
            return parse_number();
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

std::unique_ptr<text_node> text_parser::parse_object(std::size_t depth) {
    if (depth >= kMaxDepth) {
        err = fail(error_code::too_deep, pos);
        return nullptr;
    }
    ++pos; // '{'
    skip_ws();
    auto n = std::make_unique<text_node>();
    n->k = value_io::kind::object;
    if (pos < src.size() && src[pos] == '}') {
        ++pos;
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
        const std::string key = kescap ? kdecoded : std::string(kdirect);
        skip_ws();
        if (pos >= src.size() || src[pos] != ':') {
            err = fail(error_code::unexpected_token, pos);
            return nullptr;
        }
        ++pos;
        std::unique_ptr<text_node> child = parse_value(depth + 1);
        if (child == nullptr) {
            return nullptr;
        }
        n->set_key(string_view(key), child.release());
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
            return n;
        }
        err = fail(error_code::unexpected_token, pos);
        return nullptr;
    }
}

std::unique_ptr<text_node> text_parser::parse_array(std::size_t depth) {
    if (depth >= kMaxDepth) {
        err = fail(error_code::too_deep, pos);
        return nullptr;
    }
    ++pos; // '['
    skip_ws();
    auto n = std::make_unique<text_node>();
    n->k = value_io::kind::array;
    if (pos < src.size() && src[pos] == ']') {
        ++pos;
        return n;
    }
    for (;;) {
        skip_ws();
        std::unique_ptr<text_node> child = parse_value(depth + 1);
        if (child == nullptr) {
            return nullptr;
        }
        n->push_back(child.release());
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
            return n;
        }
        err = fail(error_code::unexpected_token, pos);
        return nullptr;
    }
}
std::unique_ptr<text_node> text_parser::parse_number() {
    const std::size_t start = pos;
    if (pos < src.size() && src[pos] == '-') {
        ++pos;
    }
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
    auto n = std::make_unique<text_node>();
    const char* begin = src.data() + start;
    const std::size_t len = pos - start;
    if (!is_real) {
        std::int64_t v = 0;
        const auto res = std::from_chars(begin, begin + len, v);
        if (res.ec == std::errc()) {
            n->k = value_io::kind::integer;
            n->i = v;
            return n;
        }
        // int64 overflow: demote to a real node (json.hpp behavior).
    }
    double dv = 0.0;
    const auto rd = std::from_chars(begin, begin + len, dv);
    if (rd.ec != std::errc()) {
        err = fail(error_code::invalid_number, start);
        return nullptr;
    }
    n->k = value_io::kind::real;
    n->d = dv;
    return n;
}
std::unique_ptr<text_node> text_parser::parse_literal() {
    if (pos + 5 <= src.size() && src.compare(pos, 5, "false") == 0) {
        pos += 5;
        auto n = std::make_unique<text_node>();
        n->k = value_io::kind::boolean;
        n->b = false;
        return n;
    }
    if (pos + 4 <= src.size() && src.compare(pos, 4, "true") == 0) {
        pos += 4;
        auto n = std::make_unique<text_node>();
        n->k = value_io::kind::boolean;
        n->b = true;
        return n;
    }
    if (pos + 4 <= src.size() && src.compare(pos, 4, "null") == 0) {
        pos += 4;
        auto n = std::make_unique<text_node>();
        n->k = value_io::kind::null;
        return n;
    }
    if (pos + 4 <= src.size() && src.compare(pos, 4, "-inf") == 0) {
        pos += 4;
        auto n = std::make_unique<text_node>();
        n->k = value_io::kind::real;
        n->d = -std::numeric_limits<double>::infinity();
        return n;
    }
    if (pos + 3 <= src.size() && src.compare(pos, 3, "inf") == 0) {
        pos += 3;
        auto n = std::make_unique<text_node>();
        n->k = value_io::kind::real;
        n->d = std::numeric_limits<double>::infinity();
        return n;
    }
    if (pos + 3 <= src.size() && src.compare(pos, 3, "nan") == 0) {
        pos += 3;
        auto n = std::make_unique<text_node>();
        n->k = value_io::kind::real;
        n->d = std::numeric_limits<double>::quiet_NaN();
        return n;
    }
    err = fail(error_code::unexpected_token, pos);
    return nullptr;
}

std::unique_ptr<text_node> text_parser::parse_string_node() {
    string_view direct;
    bool escaped = false;
    std::string decoded;
    if (!scan_string(direct, escaped, decoded)) {
        return nullptr;
    }
    auto n = std::make_unique<text_node>();
    n->k = value_io::kind::string;
    if (escaped) {
        n->s.assign(decoded.data(), decoded.size());
    } else if (direct.size() == 0) {
        n->s.clear();
    } else {
        n->s.assign(direct.data(), direct.size());
    }
    return n;
}

bool text_parser::scan_string(string_view& direct, bool& escaped,
                              std::string& decoded) {
    const std::size_t open = pos;
    ++pos; // opening quote
    const std::size_t start = pos;
    escaped = false;
    std::size_t run = pos;
    bool closed = false;
    while (pos < src.size() && !closed) {
        const char c = src[pos];
        if (c == '"') {
            ++pos;
            closed = true;
            break;
        }
        if (c == '\\') {
            if (!escaped) {
                decoded.append(src.data() + start, pos - start);
                escaped = true;
            } else {
                decoded.append(src.data() + run, pos - run);
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
                    break;
                case 'b':
                    decoded.push_back('\b');
                    pos += 2;
                    break;
                case 'f':
                    decoded.push_back('\f');
                    pos += 2;
                    break;
                case 'n':
                    decoded.push_back('\n');
                    pos += 2;
                    break;
                case 'r':
                    decoded.push_back('\r');
                    pos += 2;
                    break;
                case 't':
                    decoded.push_back('\t');
                    pos += 2;
                    break;
                case 'u':
                    pos += 2; // past "\u"
                    if (!decode_u_hex(decoded)) {
                        return false;
                    }
                    break;
                default:
                    err = fail(error_code::unexpected_token, pos);
                    return false;
            }
            run = pos;
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            err = fail(error_code::unexpected_token, pos);
            return false;
        }
        ++pos;
    }
    if (!closed) {
        err = fail(error_code::unterminated_string, open);
        return false;
    }
    if (!escaped) {
        direct = string_view(src.data() + start, (pos - 1) - start);
    } else {
        decoded.append(src.data() + run, (pos - 1) - run);
    }
    return true;
}
bool text_parser::decode_u_hex(std::string& out) {
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
            err = fail(error_code::invalid_utf8,
                       pos + static_cast<std::size_t>(i));
            return false;
        }
        cp = (cp << 4) | v;
    }
    pos += 4;
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        // High surrogate: must be followed by a low-surrogate escape.
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
                err = fail(error_code::invalid_utf8,
                           pos + 2 + static_cast<std::size_t>(i));
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

void text_parser::utf8_encode(std::uint32_t cp, std::string& out) {
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

// Parse a compact single-value JSON document (the form serialize_text emits)
// into an owned tree. Empty input is invalid_root; trailing bytes are
// unexpected_token; malformed structure returns the parser's codes. The tree
// lifetime is owned by the returned unique_ptr.
result<std::unique_ptr<text_node>> parse_text(string_view src) {
    result<std::unique_ptr<text_node>> r;
    text_parser p;
    p.src = src;
    p.pos = 0;
    p.skip_ws();
    if (p.pos >= src.size()) {
        r.err = p.fail(error_code::invalid_root, 0);
        return r;
    }
    std::unique_ptr<text_node> root = p.parse_value(0);
    if (root == nullptr) {
        r.err = p.err;
        return r;
    }
    p.skip_ws();
    if (p.pos != src.size()) {
        r.err = p.fail(error_code::unexpected_token, p.pos);
        return r;
    }
    r.value = std::move(root);
    return r;
}

} // namespace detail
} // namespace rtti
