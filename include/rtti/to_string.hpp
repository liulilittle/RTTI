#pragma once
// Unified T <-> string conversion layer (spec §16).
//
// Every supported type converts directly to/from a std::string through one
// pair of entry points:
//     rtti::to_string<T>(v)                      any type -> string
//     rtti::from_string<T>(s, T& out)            string  -> type (error_code)
//     rtti::from_string<T>(s)                    string  -> optional<T>
// plus the arbitrary-base integer helpers (spec §15/§16):
//     rtti::to_string_base / from_string_base    uint64 in any base 2..64
//     rtti::to_string_base_signed / from_string_base_signed
//
// Serialization forms:
//   * Scalars (bool / integers / floating point / strings / registered enums /
//     date_time) use a minimal text form and never build a tree:
//       bool       -> "true"/"false"
//       integral   -> decimal digits            (std::to_string)
//       float/dbl  -> shortest round-tripping text via std::to_chars, a ".0"
//                     suffix when the shortest form has no dot/exponent, and
//                     the deterministic tokens "inf"/"-inf"/"nan"
//       string     -> raw bytes, no quoting
//       enum       -> the registered label
//       date_time  -> canonical ISO 8601 UTC (exact sub-second round-trip)
//   * Containers / reflected structs / optional / variant / tuple / pair /
//     std::array / vector<bool> / native C arrays use a compact single-line
//     JSON value built through the existing rtti::to_value / rtti::from_value
//     dispatch chain (spec §16.2 "reuse value_io"). Object key order is the
//     source order (field declaration order for reflected structs); keys are
//     never sorted (spec §16.2 determinism).
//
// Architecture decision (documented per spec §16.2): to_string.hpp carries its
// OWN minimal in-header value_io tree (rtti::detail::text_node plus a
// compact-JSON serializer/parser) instead of reusing json::document. json.hpp
// is a format adapter gated by RTTI_ENABLE_JSON; depending on it would tie the
// string layer to an optional feature and break standalone stripping. There is
// no production zero-format-dependency tree in the reflect core (reflection.hpp
// forward-declares value_io only; memory.hpp provides just an arena;
// tests/box.hpp is a test double), so a small self-owning tree is the smallest
// surface that lets to_value/from_value build and read containers/new types.
// Container/reflected round trips therefore inherit the full existing dispatch
// (optional, variant, tuple, pair, array, vector<bool>, native/multi-dim C
// arrays, queue/stack/priority_queue, ADL hooks) for free.
//
// Error mapping (from_string): the JSON parse path reports the parser's codes
// (invalid_root / unexpected_token / unterminated_string / invalid_number /
// invalid_utf8 / too_deep); conversion kind mismatches report kind_mismatch /
// missing_key and numeric range violations report overflow (spec §0). Scalar
// fast paths report invalid_number for malformed text and overflow for values
// outside the target range. to_string returns an empty string when the
// conversion itself fails (e.g. an enum whose value has no label).
//
// Zero format dependency: this header includes only the reflect core
// (<rtti/convert.hpp> pulls in base_convert, date_time, error, naming and
// reflection). English comments only; C++17; no third-party dependencies.

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <rtti/convert.hpp>

namespace rtti {

namespace detail {

// --- Minimal in-header JSON tree -------------------------------------------
// text_node is the value_io sink/source used by the container/reflected path.
// Scalars hold a std::string payload by value; array/object children are owned
// through unique_ptr, so the whole tree frees itself recursively. Object keys
// are copied by value and stored in insertion order (deterministic, unsorted).

struct text_node : value_io {
    value_io::kind k = value_io::kind::null;
    bool b = false;
    std::int64_t i = 0;
    double d = 0.0;
    std::string s;
    std::vector<std::unique_ptr<text_node>> arr;
    std::vector<std::pair<std::string, std::unique_ptr<text_node>>> obj;

    value_io::kind type() const noexcept override { return k; }

    bool as_bool(bool& out) const noexcept override {
        if (k != value_io::kind::boolean) {
            return false;
        }
        out = b;
        return true;
    }
    bool as_integer(std::int64_t& out) const noexcept override {
        if (k != value_io::kind::integer) {
            return false;
        }
        out = i;
        return true;
    }
    bool as_real(double& out) const noexcept override {
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
    bool as_string(string_view& out) const noexcept override {
        if (k != value_io::kind::string) {
            return false;
        }
        out = string_view(s.data(), s.size());
        return true;
    }
    std::size_t size() const noexcept override {
        return k == value_io::kind::array   ? arr.size()
               : k == value_io::kind::object ? obj.size()
                                             : 0;
    }
    value_io* at(std::size_t index) override {
        return (k == value_io::kind::array && index < arr.size())
                   ? arr[index].get()
                   : nullptr;
    }
    value_io* at(std::size_t index) const override {
        return (k == value_io::kind::array && index < arr.size())
                   ? arr[index].get()
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
        if (k != value_io::kind::object || index >= obj.size()) {
            return false;
        }
        key = string_view(obj[index].first);
        out = obj[index].second.get();
        return true;
    }
    value_io* make_array() override {
        k = value_io::kind::array;
        return this;
    }
    void push_back(value_io* v) override {
        if (k == value_io::kind::array) {
            arr.emplace_back(adopt_child(static_cast<text_node*>(v)));
        }
    }
    value_io* make_object() override {
        k = value_io::kind::object;
        return this;
    }
    void set_key(string_view key, value_io* v) override {
        if (k == value_io::kind::object) {
            obj.emplace_back(std::string(key),
                             adopt_child(static_cast<text_node*>(v)));
        }
    }
    value_io* make_scalar(field_value v) override {
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
    value_io* make_node() override {
        pending_.emplace_back(new text_node);
        return pending_.back().get();
    }

private:
    // A child created by make_node() is owned by this node from birth (held in
    // pending_); push_back/set_key transfer that ownership to the arr/obj
    // containers. A conversion failure between make_node() and the attach
    // (convert.hpp returns its error without attaching) therefore cannot leak
    // the child: it is reclaimed with this node. Nodes built by the parser
    // (parse_text) arrive with ownership already transferred (child.release())
    // and are adopted through the fallback path.
    std::unique_ptr<text_node> adopt_child(text_node* c) {
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->get() == c) {
                std::unique_ptr<text_node> owned = std::move(*it);
                pending_.erase(it);
                return owned;
            }
        }
        return std::unique_ptr<text_node>(c);
    }

    bool get_key_impl(string_view key, value_io*& out) const {
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

    // make_node()-produced children not yet attached to arr/obj. Owning them
    // here closes the failure-path leak: every raw child pointer handed out by
    // make_node() is the unique_ptr below until a successful attach.
    std::vector<std::unique_ptr<text_node>> pending_;
};

// --- Compact single-line serializer ----------------------------------------
// Deterministic: arrays/objects emit in source order, no sort_keys.
// Non-finite reals emit the bare tokens "inf"/"-inf"/"nan" so container
// floats round-trip exactly (the JSON adapters map them to null; here exact
// round-trip is more useful and the string layer owns its parser).

inline void append_text_string(const char* data, std::size_t len,
                               std::string& out) {
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

inline void serialize_text(const text_node& n, std::string& out) {
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
// Recursive descent over the productions this serializer emits (plus tolerant
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

inline std::unique_ptr<text_node> text_parser::parse_value(std::size_t depth) {
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

inline std::unique_ptr<text_node> text_parser::parse_object(std::size_t depth) {
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

inline std::unique_ptr<text_node> text_parser::parse_array(std::size_t depth) {
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

inline std::unique_ptr<text_node> text_parser::parse_number() {
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

inline std::unique_ptr<text_node> text_parser::parse_literal() {
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

inline std::unique_ptr<text_node> text_parser::parse_string_node() {
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

inline bool text_parser::scan_string(string_view& direct, bool& escaped,
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

inline bool text_parser::decode_u_hex(std::string& out) {
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

inline void text_parser::utf8_encode(std::uint32_t cp, std::string& out) {
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
inline result<std::unique_ptr<text_node>> parse_text(string_view src) {
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

// --- Scalar parsing helpers ---------------------------------------------------

// Shortest round-tripping text for float/double (narrow to_chars handles the
// exact float range; format_real double is used for double). A ".0" suffix is
// appended when the shortest form has no dot or exponent.
template <typename F>
inline void append_shortest_real(F value, std::string& out) {
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            out += "nan";
        } else {
            out += std::signbit(value) ? "-inf" : "inf";
        }
        return;
    }
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), value);
    const char* const first = buf;
    const char* const last = res.ptr;
    out.append(first, static_cast<std::size_t>(last - first));
    // Inspect only the token just written; an earlier token's '.' in a shared
    // stream must not suppress this token's ".0".
    bool has_point = false;
    for (const char* p = first; p != last; ++p) {
        if (*p == '.' || *p == 'e' || *p == 'E') {
            has_point = true;
            break;
        }
    }
    if (!has_point) {
        out += ".0";
    }
}

// Integral with full-input validation and range checking: the whole string
// must parse, malformed text is invalid_number and an out-of-range value is
// overflow (spec §0). Narrow types round-trip their cast to detect range loss.
template <typename T>
inline error_code parse_integral(string_view s, T& out) {
    if (s.empty()) {
        return error_code::invalid_number;
    }
    if constexpr (std::is_signed<T>::value) {
        std::int64_t v = 0;
        const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec == std::errc::result_out_of_range) {
            return error_code::overflow;
        }
        if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
            return error_code::invalid_number;
        }
        if (static_cast<std::int64_t>(static_cast<T>(v)) != v) {
            return error_code::overflow;
        }
        out = static_cast<T>(v);
        return error_code::ok;
    } else {
        std::uint64_t v = 0;
        const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec == std::errc::result_out_of_range) {
            return error_code::overflow;
        }
        if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
            return error_code::invalid_number;
        }
        if (static_cast<std::uint64_t>(static_cast<T>(v)) != v) {
            return error_code::overflow;
        }
        out = static_cast<T>(v);
        return error_code::ok;
    }
}

// Floating point with direct from_chars into T (exact float round-trip) and
// explicit non-finite tokens matching the serializer output.
template <typename T>
inline error_code parse_real(string_view s, T& out) {
    if (s == "inf" || s == "infinity") {
        out = std::numeric_limits<T>::infinity();
        return error_code::ok;
    }
    if (s == "-inf" || s == "-infinity") {
        out = -std::numeric_limits<T>::infinity();
        return error_code::ok;
    }
    if (s == "nan") {
        out = std::numeric_limits<T>::quiet_NaN();
        return error_code::ok;
    }
    const auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    if (res.ec == std::errc::result_out_of_range) {
        return error_code::overflow;
    }
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
        return error_code::invalid_number;
    }
    return error_code::ok;
}

// date_time: canonical ISO 8601 or a numeric Unix epoch (10/13/16/19/20
// decimal digits infer seconds/ms/us/ns, spec §14.1).
template <typename T>
inline error_code parse_date_time(string_view s, T& out) {
    const result<date_time> r = date_time::parse(s);
    if (r.ok()) {
        out = r.value;
        return error_code::ok;
    }
    std::size_t i = 0;
    if (i < s.size() && s[i] == '-') {
        ++i;
    }
    const std::size_t first = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
    }
    if (i != s.size() || i == first) {
        return r.err.code;
    }
    std::int64_t v = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec == std::errc::result_out_of_range) {
        return error_code::overflow;
    }
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
        return error_code::invalid_number;
    }
    std::int64_t ns = 0;
    const error_code e = epoch_ns_from_digits(v, ns);
    if (e != error_code::ok) {
        return e;
    }
    out = date_time(ns);
    return error_code::ok;
}

// True for a char-based std::basic_string (any traits/allocator); false for
// native arrays, string_view and non-char strings (wchar_t...). The void_t
// guard keeps typename T::value_type out of the condition for types that lack
// it (an array, for instance, would otherwise be a hard error inside the
// if constexpr condition of to_string/from_string).
template <typename T, typename = void>
struct is_char_basic_string : std::false_type {};
template <typename T>
struct is_char_basic_string<T, std::void_t<typename T::value_type>>
    : std::bool_constant<rtti::is_char_string<T>::value &&
                         std::is_same<typename T::value_type, char>::value> {};

} // namespace detail

// --- Public entry points ------------------------------------------------------

// Any supported type -> minimal text. Scalars direct; containers / reflected
// structs / new types through the compact JSON tree. Returns an empty string
// when the conversion itself fails (e.g. an enum value outside its label
// table).
template <typename T>
std::string to_string(const T& v) {
    if constexpr (std::is_same<T, bool>::value) {
        return v ? std::string("true") : std::string("false");
    } else if constexpr (std::is_integral<T>::value) {
        return std::to_string(v);
    } else if constexpr (std::is_floating_point<T>::value) {
        std::string out;
        rtti::detail::append_shortest_real(v, out);
        return out;
    } else if constexpr (std::is_same<T, std::string>::value ||
                         std::is_same<T, string_view>::value) {
        return std::string(v.data(), v.size());
    } else if constexpr (rtti::detail::is_char_basic_string<T>::value) {
        return std::string(v.data(), v.size());
    } else if constexpr (std::is_array<T>::value &&
                         std::is_same<std::remove_extent_t<T>, char>::value) {
        std::size_t len = 0;
        const std::size_t total = sizeof(v) / sizeof(v[0]);
        while (len < total && v[len] != '\0') {
            ++len;
        }
        return std::string(v, len);
    } else if constexpr (is_registered_enum<T>::value) {
        const char* name = enum_to_string(v);
        return (name != nullptr && name[0] != '\0') ? std::string(name)
                                                    : std::string();
    } else if constexpr (is_date_time<T>::value) {
        return v.format();
    } else if constexpr (std::is_enum<T>::value) {
        // An enum without a label table is not a supported type.
        return std::string();
    } else {
        // Container / reflected / new-type path: build a compact JSON value
        // through the existing to_value dispatch, then serialize one line.
        std::unique_ptr<rtti::detail::text_node> root(
            new rtti::detail::text_node);
        const error_code e = rtti::to_value(root.get(), v);
        if (e != error_code::ok) {
            return std::string();
        }
        std::string out;
        out.reserve(64);
        rtti::detail::serialize_text(*root, out);
        return out;
    }
}

// Federated string -> T. Scalars parse directly; containers / reflected
// structs / new types parse the compact JSON then run from_value.
template <typename T>
error_code from_string(string_view s, T& out) {
    if constexpr (std::is_same<T, bool>::value) {
        if (s == "true") {
            out = true;
            return error_code::ok;
        }
        if (s == "false") {
            out = false;
            return error_code::ok;
        }
        return error_code::kind_mismatch;
    } else if constexpr (std::is_integral<T>::value) {
        return rtti::detail::parse_integral(s, out);
    } else if constexpr (std::is_floating_point<T>::value) {
        return rtti::detail::parse_real(s, out);
    } else if constexpr (std::is_same<T, std::string>::value) {
        out.assign(s.data(), s.size());
        return error_code::ok;
    } else if constexpr (std::is_same<T, string_view>::value) {
        out = s;
        return error_code::ok;
    } else if constexpr (rtti::detail::is_char_basic_string<T>::value) {
        out.assign(s.data(), s.size());
        return error_code::ok;
    } else if constexpr (std::is_array<T>::value &&
                         std::is_same<std::remove_extent_t<T>, char>::value) {
        const std::size_t total = sizeof(out) / sizeof(out[0]);
        if (s.size() >= total) {
            return error_code::kind_mismatch; // over-long input, never overrun
        }
        std::memset(out, 0, sizeof(out));
        if (s.size() != 0) {
            std::memcpy(out, s.data(), s.size());
        }
        return error_code::ok;
    } else if constexpr (is_registered_enum<T>::value) {
        for (std::size_t i = 0; i < enum_table<T>::count; ++i) {
            if (string_view(enum_table<T>::array[i].name) == s) {
                out = enum_table<T>::array[i].value;
                return error_code::ok;
            }
        }
        return error_code::kind_mismatch;
    } else if constexpr (is_date_time<T>::value) {
        return rtti::detail::parse_date_time(s, out);
    } else if constexpr (std::is_enum<T>::value) {
        // An enum without a label table is not a supported type.
        return error_code::kind_mismatch;
    } else {
        // Container / reflected / new-type path.
        const result<std::unique_ptr<rtti::detail::text_node>> parsed =
            rtti::detail::parse_text(s);
        if (!parsed.ok()) {
            return parsed.err.code;
        }
        return rtti::from_value(parsed.value.get(), out);
    }
}

// Convenience: returns nullopt on any parse/conversion error.
template <typename T>
std::optional<T> from_string(string_view s) {
    T out{};
    if (from_string<T>(s, out) != error_code::ok) {
        return std::nullopt;
    }
    return out;
}

// --- Arbitrary-base integer bridge (spec §15.4 / §16.1) ----------------------
// Thin wrappers over rtti::detail::to_base / from_base (bases 2..64) with the
// optional custom digit table; signed variants carry a leading '-'.

inline std::string to_string_base(std::uint64_t value, unsigned base,
                                  const char* digits = nullptr) {
    return rtti::detail::to_base(value, base, digits);
}

inline error_code from_string_base(string_view s, unsigned base,
                                   std::uint64_t& out) {
    return rtti::detail::from_base(s, base, out);
}

inline std::optional<std::uint64_t> from_string_base(string_view s,
                                                     unsigned base) {
    std::uint64_t v = 0;
    if (rtti::detail::from_base(s, base, v) != error_code::ok) {
        return std::nullopt;
    }
    return v;
}

inline std::string to_string_base_signed(std::int64_t value, unsigned base) {
    return rtti::detail::to_base_signed(value, base);
}

inline error_code from_string_base_signed(string_view s, unsigned base,
                                          std::int64_t& out) {
    return rtti::detail::from_base_signed(s, base, out);
}

inline std::optional<std::int64_t> from_string_base_signed(string_view s,
                                                           unsigned base) {
    std::int64_t v = 0;
    if (rtti::detail::from_base_signed(s, base, v) != error_code::ok) {
        return std::nullopt;
    }
    return v;
}

} // namespace rtti