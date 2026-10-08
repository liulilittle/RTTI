// rtti::proto .proto text-descriptor parser + runtime schema bridge implementation.
// The non-template definitions declared by <rtti/proto_schema.hpp> live here: the
// token scanner / recursive-descent parser, the type-resolution and packed-wire
// derivation passes, the compile bridge (compiled_message / dynamic_field + the
// codec tree callbacks) and the free parse_schema / merge_schemas /
// register_schema_extensions / compile_schema entry points. Behavior is
// identical to the former inline header definitions. English comments only;
// C++17; no third-party dependencies; built only under the RTTI_ENABLE_PROTO
// gate (see CMakeLists.txt).

#include <rtti/proto_schema.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rtti {
namespace proto {

// Natural wire type for a scalar kind per the protobuf spec. message refs use
// length (encoded as a nested message); enum refs use varint.
wire_type wire_of_scalar(scalar_type t) noexcept {
    switch (t) {
        case scalar_type::fixed32:
        case scalar_type::sfixed32:
        case scalar_type::float_val:
            return wire_type::fixed32;
        case scalar_type::fixed64:
        case scalar_type::sfixed64:
        case scalar_type::double_val:
            return wire_type::fixed64;
        case scalar_type::string_val:
        case scalar_type::bytes_val:
        case scalar_type::message_val:
            return wire_type::length;
        default:
            return wire_type::varint; // ints, bool, enum
    }
}
namespace detail {

// ---------------------------------------------------------------------------
// Token scanner / recursive-descent parser for the supported subset
// ---------------------------------------------------------------------------

struct token {
    enum class kind : std::uint8_t {
        eof,
        ident,
        number, // integer (ival) or real (dval, is_real)
        str,    // string literal (sval carries the unescaped payload)
        lbrace,
        rbrace,
        lbracket,
        rbracket,
        lparen,
        rparen,
        eq,
        semi,
        dot,
        comma,
        minus,
        less,
        greater
    };
    kind k = kind::eof;
    rtti::string_view text; // ident
    std::uint64_t ival = 0;
    double dval = 0.0;
    bool is_real = false;
    std::string sval; // string literal payload
    const char* begin = nullptr;
};

inline bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
inline bool is_name(char c) noexcept {
    return is_alpha(c) || is_digit(c);
}

struct scanner {
    const char* p;
    const char* e;
    error_code fail = error_code::ok;
    const char* fail_pos = nullptr;

    explicit scanner(rtti::string_view s) noexcept
        : p(s.data()), e(s.data() + s.size()) {}

    void skip_trivia() {
        for (;;) {
            while (p != e && (*p == ' ' || *p == '\t' || *p == '\r' ||
                              *p == '\n')) {
                ++p;
            }
            if (p != e && *p == '/') {
                if (p + 1 < e && p[1] == '/') {
                    p += 2;
                    while (p != e && *p != '\n') {
                        ++p;
                    }
                    continue;
                }
                if (p + 1 < e && p[1] == '*') {
                    const char* const start = p;
                    p += 2;
                    bool closed = false;
                    while (p != e) {
                        if (p + 1 < e && *p == '*' && p[1] == '/') {
                            p += 2;
                            closed = true;
                            break;
                        }
                        ++p;
                    }
                    if (!closed) {
                        fail = error_code::unterminated_comment;
                        fail_pos = start;
                        return;
                    }
                    continue;
                }
            }
            return;
        }
    }

    token next() {
        token t;
        skip_trivia();
        if (fail != error_code::ok) {
            t.k = token::kind::eof;
            return t;
        }
        t.begin = p;
        if (p == e) {
            t.k = token::kind::eof;
            return t;
        }
        const char c = *p;
        if (is_alpha(c)) {
            const char* s = p;
            while (p != e && is_name(*p)) {
                ++p;
            }
            t.k = token::kind::ident;
            t.text = rtti::string_view(s, static_cast<std::size_t>(p - s));
            return t;
        }
        if (is_digit(c)) {
            std::uint64_t v = 0;
            bool overflow = false;
            while (p != e && is_digit(*p)) {
                const std::uint64_t digit =
                    static_cast<std::uint64_t>(*p - '0');
                if (v > ((std::numeric_limits<std::uint64_t>::max)() - digit) /
                            10) {
                    overflow = true;
                } else {
                    v = v * 10 + digit;
                }
                ++p;
            }
            if (p != e && *p == '.') {
                ++p;
                while (p != e && is_digit(*p)) {
                    ++p;
                }
                const std::string num(t.begin, p);
                t.is_real = true;
                t.dval = std::strtod(num.c_str(), nullptr);
                t.k = token::kind::number;
                return t;
            }
            if (overflow) {
                fail = error_code::invalid_number;
                fail_pos = t.begin;
                t.k = token::kind::eof;
                return t;
            }
            t.ival = v;
            t.k = token::kind::number;
            return t;
        }
        switch (c) {
            case '{':
                t.k = token::kind::lbrace;
                ++p;
                break;
            case '}':
                t.k = token::kind::rbrace;
                ++p;
                break;
            case '[':
                t.k = token::kind::lbracket;
                ++p;
                break;
            case ']':
                t.k = token::kind::rbracket;
                ++p;
                break;
            case '(':
                t.k = token::kind::lparen;
                ++p;
                break;
            case ')':
                t.k = token::kind::rparen;
                ++p;
                break;
            case '=':
                t.k = token::kind::eq;
                ++p;
                break;
            case ';':
                t.k = token::kind::semi;
                ++p;
                break;
            case '.':
                t.k = token::kind::dot;
                ++p;
                break;
            case ',':
                t.k = token::kind::comma;
                ++p;
                break;
            case '-':
                t.k = token::kind::minus;
                ++p;
                break;
            case '<':
                t.k = token::kind::less;
                ++p;
                break;
            case '>':
                t.k = token::kind::greater;
                ++p;
                break;
            case '"': {
                ++p;
                std::string tmp;
                bool closed = false;
                while (p != e) {
                    const char ch = *p;
                    if (ch == '"') {
                        ++p;
                        closed = true;
                        break;
                    }
                    if (ch == '\\') {
                        if (p + 1 >= e) {
                            p = e;
                            break;
                        }
                        const char esc = p[1];
                        auto is_hex = [](char c) noexcept -> bool {
                            return (c >= '0' && c <= '9') ||
                                   (c >= 'a' && c <= 'f') ||
                                   (c >= 'A' && c <= 'F');
                        };
                        auto hex_val = [](char c) noexcept -> unsigned {
                            if (c >= '0' && c <= '9') {
                                return static_cast<unsigned>(c - '0');
                            }
                            if (c >= 'a' && c <= 'f') {
                                return static_cast<unsigned>(c - 'a' + 10);
                            }
                            return static_cast<unsigned>(c - 'A' + 10);
                        };
                        auto is_oct = [](char c) noexcept -> bool {
                            return c >= '0' && c <= '7';
                        };
                        switch (esc) {
                            case 'a':
                                tmp.push_back('\a');
                                p += 2;
                                break;
                            case 'b':
                                tmp.push_back('\b');
                                p += 2;
                                break;
                            case 'e':
                                tmp.push_back('\x1B');
                                p += 2;
                                break;
                            case 'f':
                                tmp.push_back('\f');
                                p += 2;
                                break;
                            case 'n':
                                tmp.push_back('\n');
                                p += 2;
                                break;
                            case 'r':
                                tmp.push_back('\r');
                                p += 2;
                                break;
                            case 't':
                                tmp.push_back('\t');
                                p += 2;
                                break;
                            case 'v':
                                tmp.push_back('\v');
                                p += 2;
                                break;
                            case '\\':
                                tmp.push_back('\\');
                                p += 2;
                                break;
                            case '"':
                                tmp.push_back('"');
                                p += 2;
                                break;
                            case '\'':
                                tmp.push_back('\'');
                                p += 2;
                                break;
                            case '?':
                                tmp.push_back('?');
                                p += 2;
                                break;
                            case 'x': {
                                // \xHH: up to two hex digits (proto byte
                                // literal; the value must fit in a byte).
                                p += 2;
                                unsigned v = 0;
                                int digits = 0;
                                while (p != e && digits < 2 &&
                                       is_hex(*p)) {
                                    v = (v << 4) | hex_val(*p);
                                    ++p;
                                    ++digits;
                                }
                                if (digits == 0) {
                                    fail = error_code::unexpected_token;
                                    fail_pos = t.begin;
                                    break;
                                }
                                tmp.push_back(static_cast<char>(
                                    static_cast<unsigned char>(v & 0xFFu)));
                                break;
                            }
                            case 'u': {
                                // \uXXXX: 4 hex digits; the code point is
                                // UTF-8 encoded into the string payload.
                                p += 2;
                                std::uint32_t cp = 0;
                                int digits = 0;
                                while (p != e && digits < 4 && is_hex(*p)) {
                                    cp = (cp << 4) | hex_val(*p);
                                    ++p;
                                    ++digits;
                                }
                                if (digits < 4) {
                                    fail = error_code::unexpected_token;
                                    fail_pos = t.begin;
                                }
                                if (cp <= 0x7Fu) {
                                    tmp.push_back(static_cast<char>(cp));
                                } else if (cp <= 0x7FFu) {
                                    tmp.push_back(static_cast<char>(
                                        0xC0u | (cp >> 6)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | (cp & 0x3Fu)));
                                } else if (cp <= 0xFFFFu) {
                                    tmp.push_back(static_cast<char>(
                                        0xE0u | (cp >> 12)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | ((cp >> 6) & 0x3Fu)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | (cp & 0x3Fu)));
                                } else {
                                    tmp.push_back(static_cast<char>(
                                        0xF0u | (cp >> 18)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | ((cp >> 12) & 0x3Fu)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | ((cp >> 6) & 0x3Fu)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | (cp & 0x3Fu)));
                                }
                                break;
                            }
                            case 'U': {
                                // \UXXXXXXXX: 8 hex digits, UTF-8 encoded.
                                p += 2;
                                std::uint32_t cp = 0;
                                int digits = 0;
                                while (p != e && digits < 8 && is_hex(*p)) {
                                    cp = (cp << 4) | hex_val(*p);
                                    ++p;
                                    ++digits;
                                }
                                if (digits < 8) {
                                    fail = error_code::unexpected_token;
                                    fail_pos = t.begin;
                                }
                                if (cp > 0x10FFFFu) {
                                    // Outside the Unicode scalar range.
                                    fail = error_code::unexpected_token;
                                    fail_pos = t.begin;
                                }
                                if (cp <= 0x7Fu) {
                                    tmp.push_back(static_cast<char>(cp));
                                } else if (cp <= 0x7FFu) {
                                    tmp.push_back(static_cast<char>(
                                        0xC0u | (cp >> 6)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | (cp & 0x3Fu)));
                                } else if (cp <= 0xFFFFu) {
                                    tmp.push_back(static_cast<char>(
                                        0xE0u | (cp >> 12)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | ((cp >> 6) & 0x3Fu)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | (cp & 0x3Fu)));
                                } else {
                                    tmp.push_back(static_cast<char>(
                                        0xF0u | (cp >> 18)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | ((cp >> 12) & 0x3Fu)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | ((cp >> 6) & 0x3Fu)));
                                    tmp.push_back(static_cast<char>(
                                        0x80u | (cp & 0x3Fu)));
                                }
                                break;
                            }
                            default: {
                                // 1-3 octal digits \NNN (proto octal escape)
                                // when the next char is octal; otherwise the
                                // escaped character pass through verbatim.
                                if (is_oct(esc)) {
                                    unsigned v = static_cast<unsigned>(esc - '0');
                                    p += 2;
                                    int digits = 1;
                                    while (p != e && digits < 3 && is_oct(*p)) {
                                        v = (v << 3) |
                                            static_cast<unsigned>(*p - '0');
                                        ++p;
                                        ++digits;
                                    }
                                    tmp.push_back(static_cast<char>(
                                        static_cast<unsigned char>(v & 0xFFu)));
                                } else {
                                    tmp.push_back(esc);
                                    p += 2;
                                }
                                break;
                            }
                        }
                    } else {
                        tmp.push_back(ch);
                        ++p;
                    }
                }
                if (!closed) {
                    fail = error_code::unterminated_string;
                    fail_pos = t.begin;
                }
                t.k = token::kind::str;
                t.sval = std::move(tmp);
                break;
            }
            default:
                fail = error_code::unexpected_token;
                fail_pos = t.begin;
                ++p;
                t.k = token::kind::eof;
                break;
        }
        return t;
    }
};
struct parser {
    scanner sc;
    token cur;
    token look;
    error_info err;
    const char* src_begin;
    syntax syn_ = syntax::proto2;
    int nesting = 0;
    static constexpr int kMaxNesting = 128;

    explicit parser(rtti::string_view s)
        : sc(s), src_begin(s.data()) {
        err.src = s.data();
        err.src_len = s.size();
        cur = sc.next();
        look = sc.next();
        if (err.code == error_code::ok && sc.fail != error_code::ok) {
            err.code = sc.fail;
            err.offset = sync_offset(sc.fail_pos);
        }
    }

    std::size_t sync_offset(const char* pos) const noexcept {
        if (pos == nullptr || src_begin == nullptr) {
            return 0;
        }
        return static_cast<std::size_t>(pos - src_begin);
    }

    bool failed() const noexcept { return err.code != error_code::ok; }

    void set_error(error_code c, const char* pos) {
        if (err.code != error_code::ok) {
            return;
        }
        err.code = c;
        err.offset = sync_offset(pos);
    }

    void adv() {
        cur = look;
        look = sc.next();
        if (err.code == error_code::ok && sc.fail != error_code::ok) {
            err.code = sc.fail;
            err.offset = sync_offset(sc.fail_pos);
        }
    }

    bool have_ident(const char* kw) const {
        return cur.k == token::kind::ident &&
               cur.text == rtti::string_view(kw);
    }

    bool expect_kind(token::kind k) {
        if (failed()) {
            return false;
        }
        if (cur.k == k) {
            adv();
            return true;
        }
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }

    bool parse_file(schema_desc& d);
    bool parse_message(schema_desc& d, std::vector<std::string>& stack,
                       int depth);
    bool parse_oneof(schema_desc& d, schema_message& sm,
                     const std::vector<std::string>& stack);
    bool parse_reserved(schema_message& sm);
    bool parse_extensions_range(schema_message& sm);
    bool parse_extend(schema_desc& d, std::vector<std::string>& stack,
                      int depth);
    bool parse_field(schema_desc& d, schema_message& sm,
                     const std::vector<std::string>& stack);
    bool parse_map_type(schema_desc& d, schema_field& f,
                        schema_message& sm,
                        const std::vector<std::string>& stack);
    bool parse_type_ref(schema_field& f);
    bool parse_options(schema_field& f);
    bool parse_option_value(schema_field& f, const std::string& name,
                            const char* pos);
    bool parse_enum(schema_desc& d, std::vector<std::string>& stack);
    bool parse_import(schema_desc& d);
    bool skip_statement();
    bool skip_paren_group();
};

inline bool parser::parse_file(schema_desc& d) {
    syn_ = syntax::proto2;
    d.syn = syntax::proto2;
    if (have_ident("syntax")) {
        adv();
        if (!expect_kind(token::kind::eq)) {
            return false;
        }
        if (cur.k == token::kind::str) {
            if (cur.sval == "proto3") {
                syn_ = syntax::proto3;
            } else if (cur.sval == "proto2") {
                syn_ = syntax::proto2;
            } else {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            adv();
        } else if (cur.k == token::kind::ident &&
                   (cur.text == rtti::string_view("proto3") ||
                    cur.text == rtti::string_view("proto2"))) {
            syn_ = (cur.text == rtti::string_view("proto3"))
                       ? syntax::proto3
                       : syntax::proto2;
            adv();
        } else {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (!expect_kind(token::kind::semi)) {
            return false;
        }
        d.syn = syn_;
    }
    std::vector<std::string> stack;
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == token::kind::eof) {
            break;
        }
        if (have_ident("message")) {
            if (!parse_message(d, stack, 0)) {
                return false;
            }
            continue;
        }
        if (have_ident("enum")) {
            if (!parse_enum(d, stack)) {
                return false;
            }
            continue;
        }
        if (have_ident("import")) {
            if (!parse_import(d)) {
                return false;
            }
            continue;
        }
        if (have_ident("extend")) {
            if (!parse_extend(d, stack, 0)) {
                return false;
            }
            continue;
        }
        if (have_ident("package") || have_ident("option") ||
            have_ident("syntax")) {
            if (!skip_statement()) {
                return false;
            }
            continue;
        }
        if (cur.k == token::kind::semi) {
            adv();
            continue;
        }
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    (void)nesting;
    return true;
}

inline bool parser::parse_message(schema_desc& d,
                                  std::vector<std::string>& stack,
                                  int depth) {
    if (failed()) {
        return false;
    }
    if (depth >= kMaxNesting) {
        set_error(error_code::too_deep, cur.begin);
        return false;
    }
    adv(); // 'message'
    if (cur.k != token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    schema_message sm;
    sm.name = std::string(cur.text);
    sm.full_name = sm.name;
    if (!stack.empty()) {
        sm.full_name = stack.back() + "." + sm.name;
    }
    adv();
    if (!expect_kind(token::kind::lbrace)) {
        return false;
    }
    stack.push_back(sm.full_name);
    for (;;) {
        if (failed()) {
            stack.pop_back();
            return false;
        }
        if (cur.k == token::kind::rbrace) {
            adv();
            break;
        }
        if (cur.k == token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            stack.pop_back();
            return false;
        }
        if (have_ident("message")) {
            if (!parse_message(d, stack, depth + 1)) {
                stack.pop_back();
                return false;
            }
            continue;
        }
        if (have_ident("enum")) {
            if (!parse_enum(d, stack)) {
                stack.pop_back();
                return false;
            }
            continue;
        }
        if (have_ident("oneof")) {
            if (!parse_oneof(d, sm, stack)) {
                stack.pop_back();
                return false;
            }
            continue;
        }
        if (have_ident("reserved")) {
            if (!parse_reserved(sm)) {
                stack.pop_back();
                return false;
            }
            continue;
        }
        if (have_ident("extensions")) {
            if (!parse_extensions_range(sm)) {
                stack.pop_back();
                return false;
            }
            continue;
        }
        if (have_ident("extend")) {
            if (!parse_extend(d, stack, depth + 1)) {
                stack.pop_back();
                return false;
            }
            continue;
        }
        if (cur.k == token::kind::semi) {
            adv();
            continue;
        }
        if (have_ident("option")) {
            if (!skip_statement()) {
                stack.pop_back();
                return false;
            }
            continue;
        }
        if (!parse_field(d, sm, stack)) {
            stack.pop_back();
            return false;
        }
    }
    stack.pop_back();
    d.messages.push_back(std::move(sm));
    return true;
}

inline bool parser::parse_field(schema_desc& d, schema_message& sm,
                                const std::vector<std::string>& stack) {
    if (failed()) {
        return false;
    }
    schema_field f;
    if (have_ident("required")) {
        f.mode = field_mode::required;
        adv();
    } else if (have_ident("optional")) {
        f.mode = field_mode::optional;
        adv();
    } else if (have_ident("repeated")) {
        f.mode = field_mode::repeated;
        adv();
    }
    if (failed()) {
        return false;
    }
    if (syn_ == syntax::proto3 && f.mode == field_mode::required) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    if (have_ident("map") && look.k == token::kind::less) {
        if (f.mode == field_mode::repeated ||
            f.mode == field_mode::required) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        // parse_map_type consumes `map<K,V> name = N [options] ;` in full (the
        // entry-message name depends on the field name).
        if (!parse_map_type(d, f, sm, stack)) {
            return false;
        }
        if (!expect_kind(token::kind::semi)) {
            return false;
        }
    } else {
        if (!parse_type_ref(f)) {
            return false;
        }
        if (cur.k != token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        f.name = std::string(cur.text);
        adv();
        if (!expect_kind(token::kind::eq)) {
            return false;
        }
        if (cur.k != token::kind::number || cur.is_real) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.ival == 0 || cur.ival > 0x1FFFFFFFull) {
            set_error(error_code::proto_bad_field, cur.begin);
            return false;
        }
        f.number = static_cast<std::uint32_t>(cur.ival);
        adv();
        if (cur.k == token::kind::lbracket) {
            if (!parse_options(f)) {
                return false;
            }
        }
        if (!expect_kind(token::kind::semi)) {
            return false;
        }
    }
    // A field reusing a reserved field number or name is rejected by parse
    // (protoc semantics), so a later wire writer can never collide with a
    // reserved slot.
    for (const auto& rng : sm.reserved_numbers) {
        if (f.number >= rng.first && f.number <= rng.second) {
            set_error(error_code::proto_bad_field, cur.begin);
            return false;
        }
    }
    for (const std::string& rn : sm.reserved_names) {
        if (f.name == rn) {
            set_error(error_code::proto_bad_field, cur.begin);
            return false;
        }
    }
    // A field number inside the message's own extension range is rejected
    // (protoc message -- extension ranges belong to extensions, not to regular
    // fields). Only ranges declared BEFORE the field are visible to parse_field;
    // the order-independent full pass in parse_schema catches the rest.
    for (const auto& rng : sm.extension_ranges) {
        if (f.number >= rng.first && f.number <= rng.second) {
            set_error(error_code::proto_bad_field, cur.begin);
            return false;
        }
    }
    sm.fields.push_back(std::move(f));
    return true;
}

// Recognize the 15 protobuf scalar type names. Returns false for a type that
// is not a built-in scalar (a message/enum reference instead).
inline bool scalar_type_from_name(const std::string& ref,
                                  scalar_type& st) noexcept {
    if (ref == "int32") {
        st = scalar_type::int32;
    } else if (ref == "int64") {
        st = scalar_type::int64;
    } else if (ref == "uint32") {
        st = scalar_type::uint32;
    } else if (ref == "uint64") {
        st = scalar_type::uint64;
    } else if (ref == "sint32") {
        st = scalar_type::sint32;
    } else if (ref == "sint64") {
        st = scalar_type::sint64;
    } else if (ref == "bool") {
        st = scalar_type::bool_val;
    } else if (ref == "float") {
        st = scalar_type::float_val;
    } else if (ref == "double") {
        st = scalar_type::double_val;
    } else if (ref == "string") {
        st = scalar_type::string_val;
    } else if (ref == "bytes") {
        st = scalar_type::bytes_val;
    } else if (ref == "fixed32") {
        st = scalar_type::fixed32;
    } else if (ref == "fixed64") {
        st = scalar_type::fixed64;
    } else if (ref == "sfixed32") {
        st = scalar_type::sfixed32;
    } else if (ref == "sfixed64") {
        st = scalar_type::sfixed64;
    } else {
        return false;
    }
    return true;
}

inline bool parser::parse_map_type(schema_desc& d, schema_field& f,
                                   schema_message& sm,
                                   const std::vector<std::string>& stack) {
    // map<K,V> => synthetic entry message { K key = 1; V value = 2; } whose
    // full name lives under the enclosing message. The field itself becomes a
    // repeated message-typed field referencing that entry.
    adv(); // 'map'
    if (!expect_kind(token::kind::less)) {
        return false;
    }
    const char* const key_pos = cur.begin;
    std::string kref;
    std::string vref;
    if (cur.k == token::kind::dot) {
        kref.push_back('.');
        adv();
    }
    if (cur.k != token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    kref += std::string(cur.text);
    adv();
    if (!expect_kind(token::kind::comma)) {
        return false;
    }
    const char* const value_pos = cur.begin;
    if (cur.k == token::kind::dot) {
        vref.push_back('.');
        adv();
    }
    if (cur.k != token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    vref += std::string(cur.text);
    adv();
    while (cur.k == token::kind::dot) {
        adv();
        if (cur.k != token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        vref.push_back('.');
        vref += std::string(cur.text);
        adv();
    }
    if (!expect_kind(token::kind::greater)) {
        return false;
    }
    // The entry-message full name derives from the field name, so the map
    // field's own `name = <number> [options]` is consumed here.
    if (cur.k != token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    f.name = std::string(cur.text);
    adv();
    if (!expect_kind(token::kind::eq)) {
        return false;
    }
    if (cur.k != token::kind::number || cur.is_real) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    if (cur.ival == 0 || cur.ival > 0x1FFFFFFFull) {
        set_error(error_code::proto_bad_field, cur.begin);
        return false;
    }
    f.number = static_cast<std::uint32_t>(cur.ival);
    adv();
    if (cur.k == token::kind::lbracket) {
        if (!parse_options(f)) {
            return false;
        }
    }
    scalar_type kst = scalar_type::message_val;
    schema_field kf;
    kf.name = "key";
    kf.number = 1;
    kf.emit_present = true;
    if (!scalar_type_from_name(kref, kst)) {
        set_error(error_code::unexpected_token, key_pos);
        return false;
    }
    // Map keys are limited to integral types, bool and string (protoc rule:
    // no float/double/bytes, no message/enum).
    if (kst == scalar_type::float_val || kst == scalar_type::double_val ||
        kst == scalar_type::bytes_val) {
        set_error(error_code::unexpected_token, key_pos);
        return false;
    }
    kf.type = kst;

    schema_field vf;
    vf.name = "value";
    vf.number = 2;
    vf.emit_present = true;
    scalar_type vst = scalar_type::message_val;
    if (!scalar_type_from_name(vref, vst)) {
        // value reference: a message or enum type (resolved later).
        vf.type = scalar_type::message_val;
        vf.is_message = true;
        vf.type_ref = vref;
        if (src_begin != nullptr) {
            vf.type_offset =
                static_cast<std::size_t>(value_pos - src_begin);
        }
    } else {
        vf.type = vst;
    }

    f.is_map = true;
    f.mode = field_mode::repeated;
    f.type = scalar_type::message_val;
    f.is_message = true;

    const std::string entry_component = f.name + "_entry";
    schema_message entry;
    entry.name = entry_component;
    entry.full_name = (stack.empty() ? sm.name : stack.back()) + "." +
                      entry_component;
    // Ensure the entry name is unique: append a numbered suffix when the
    // enclosing message already produced this name (multiple maps in one
    // message share the field-scoped name otherwise).
    bool dup = false;
    do {
        dup = false;
        for (const auto& m : d.messages) {
            if (m.full_name == entry.full_name) {
                dup = true;
                break;
            }
        }
        if (dup) {
            entry.full_name += "x";
            entry.name += "x";
        }
    } while (dup);
    f.type_ref = entry.full_name;
    f.type_offset = 0;
    entry.fields.push_back(std::move(kf));
    entry.fields.push_back(std::move(vf));
    d.messages.push_back(std::move(entry));
    return true;
}

inline bool parser::parse_oneof(schema_desc& d, schema_message& sm,
                                const std::vector<std::string>& stack) {
    if (failed()) {
        return false;
    }
    adv(); // 'oneof'
    if (cur.k != token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    // oneof name (unused further: members carry their own names/numbers)
    adv();
    if (!expect_kind(token::kind::lbrace)) {
        return false;
    }
    const std::uint32_t group = ++sm.oneof_counter;
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == token::kind::rbrace) {
            adv();
            break;
        }
        if (cur.k == token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == token::kind::semi) {
            adv();
            continue;
        }
        const std::size_t before = sm.fields.size();
        if (!parse_field(d, sm, stack)) {
            return false;
        }
        if (sm.fields.size() == before) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        schema_field& member = sm.fields.back();
        if (member.mode == field_mode::repeated) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (member.mode == field_mode::required) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        member.oneof_group = group;
    }
    // A trailing ';' after the oneof block is tolerated.
    if (cur.k == token::kind::semi) {
        adv();
    }
    return true;
}

inline bool parser::parse_reserved(schema_message& sm) {
    if (failed()) {
        return false;
    }
    adv(); // 'reserved'
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == token::kind::number && !cur.is_real) {
            const std::uint64_t lo = cur.ival;
            if (lo == 0 || lo > 0x1FFFFFFFull) {
                set_error(error_code::proto_bad_field, cur.begin);
                return false;
            }
            adv();
            std::uint64_t hi = lo;
            if (have_ident("to")) {
                adv();
                if (cur.k == token::kind::ident &&
                    cur.text == rtti::string_view("max")) {
                    hi = 0x1FFFFFFFull;
                    adv();
                } else if (cur.k == token::kind::number && !cur.is_real) {
                    hi = cur.ival;
                    if (hi == 0 || hi > 0x1FFFFFFFull || hi < lo) {
                        set_error(error_code::proto_bad_field, cur.begin);
                        return false;
                    }
                    adv();
                } else {
                    set_error(error_code::unexpected_token, cur.begin);
                    return false;
                }
            }
            sm.reserved_numbers.emplace_back(
                static_cast<std::uint32_t>(lo),
                static_cast<std::uint32_t>(hi));
        } else if (cur.k == token::kind::str) {
            sm.reserved_names.push_back(cur.sval);
            adv();
        } else {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == token::kind::comma) {
            adv();
            continue;
        }
        if (!expect_kind(token::kind::semi)) {
            return false;
        }
        return true;
    }
}

inline bool parser::parse_extensions_range(schema_message& sm) {
    if (failed()) {
        return false;
    }
    adv(); // 'extensions'
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k != token::kind::number || cur.is_real) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        const std::uint64_t lo = cur.ival;
        if (lo == 0 || lo > 0x1FFFFFFFull) {
            set_error(error_code::proto_bad_field, cur.begin);
            return false;
        }
        adv();
        std::uint64_t hi = lo;
        if (have_ident("to")) {
            adv();
            if (cur.k == token::kind::ident &&
                cur.text == rtti::string_view("max")) {
                hi = 0x1FFFFFFFull;
                adv();
            } else if (cur.k == token::kind::number && !cur.is_real) {
                hi = cur.ival;
                if (hi == 0 || hi > 0x1FFFFFFFull || hi < lo) {
                    set_error(error_code::proto_bad_field, cur.begin);
                    return false;
                }
                adv();
            } else {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
        }
        sm.extension_ranges.emplace_back(
            static_cast<std::uint32_t>(lo), static_cast<std::uint32_t>(hi));
        if (cur.k == token::kind::comma) {
            adv();
            continue;
        }
        if (!expect_kind(token::kind::semi)) {
            return false;
        }
        return true;
    }
}

namespace {

// Extend-block scope side channel: schema_extension carries only `extendee` +
// `field` (stable public API), so the innermost enclosing message name of each
// `extend` block is recorded here at parse time and consumed by the
// type-resolution pass (resolve_types) within the same parse_schema call.
// Keyed by the schema_desc being parsed; an extend_scope_guard erases the
// entry on every exit path so a later stack-address reuse (e.g. a compile-time
// re-resolution on a copy) never reads a stale scope.
thread_local std::unordered_map<const schema_desc*, std::vector<std::string>>
    g_extend_scopes;

// Erases the side-channel entry for `d` on destruction (all exit paths).
struct extend_scope_guard {
    explicit extend_scope_guard(const schema_desc* d) noexcept : d_(d) {
        g_extend_scopes[d_].clear();
    }
    ~extend_scope_guard() { g_extend_scopes.erase(d_); }
    const schema_desc* d_;
};

} // namespace

inline bool parser::parse_extend(schema_desc& d,
                                 std::vector<std::string>& stack,
                                 int depth) {
    if (failed()) {
        return false;
    }
    if (depth >= kMaxNesting) {
        set_error(error_code::too_deep, cur.begin);
        return false;
    }
    adv(); // 'extend'
    // The extendee message type: a dotted name, resolved relative to the
    // current scope by the type-resolution pass (like a normal type ref).
    std::string target;
    if (cur.k == token::kind::dot) {
        target.push_back('.');
        adv();
    }
    if (cur.k != token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    target += std::string(cur.text);
    adv();
    while (cur.k == token::kind::dot) {
        adv();
        if (cur.k != token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        target.push_back('.');
        target += std::string(cur.text);
        adv();
    }
    if (!expect_kind(token::kind::lbrace)) {
        return false;
    }
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == token::kind::rbrace) {
            adv();
            break;
        }
        if (cur.k == token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == token::kind::semi) {
            adv();
            continue;
        }
        // Extension fields reuse the ordinary field parser; map/oneof members are
        // rejected (protoc forbids them inside extend blocks). The extendee and
        // the field number are validated when the schema is compiled:
        // register_schema_extensions requires the extendee to exist in the
        // descriptor and the number to fall inside one of its declared
        // extension ranges, otherwise proto_bad_field.
        schema_message tmp;
        const std::size_t before = tmp.fields.size();
        if (!parse_field(d, tmp, stack)) {
            return false;
        }
        if (tmp.fields.size() == before) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        schema_extension se;
        se.extendee = target;
        se.field = std::move(tmp.fields.back());
        if (se.field.is_map) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        d.extensions.push_back(std::move(se));
        // Record the enclosing message scope for G-1 type resolution (the
        // extend block's innermost enclosing message full name, "" at top
        // level); resolve_types reads it back through g_extend_scopes.
        const std::string scope =
            stack.empty() ? std::string() : stack.back();
        g_extend_scopes[&d].push_back(scope);
    }
    if (cur.k == token::kind::semi) {
        adv();
    }
    return true;
}

inline bool parser::parse_import(schema_desc& d) {
    if (failed()) {
        return false;
    }
    adv(); // 'import'
    // protoc allows `import public "..."` / `import weak "..."`; consume a
    // modifier identifier if present.
    if (cur.k == token::kind::ident &&
        (cur.text == rtti::string_view("public") ||
         cur.text == rtti::string_view("weak"))) {
        adv();
    }
    if (cur.k != token::kind::str) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    d.imports.push_back(cur.sval);
    adv();
    if (!expect_kind(token::kind::semi)) {
        return false;
    }
    return true;
}

// Recognize the 15 protobuf scalar type names. Returns false for a type that
// is not a built-in scalar (a message/enum reference instead).
inline bool parser::parse_type_ref(schema_field& f) {
    if (failed()) {
        return false;
    }
    const char* const type_pos = cur.begin;
    std::string ref;
    if (cur.k == token::kind::dot) {
        ref.push_back('.');
        adv();
    }
    if (cur.k != token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    ref += std::string(cur.text);
    adv();
    while (cur.k == token::kind::dot) {
        adv();
        if (cur.k != token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        ref.push_back('.');
        ref += std::string(cur.text);
        adv();
    }
    if (src_begin != nullptr) {
        f.type_offset = static_cast<std::size_t>(type_pos - src_begin);
    }
    if (!ref.empty() && ref.front() != '.') {
        scalar_type st = scalar_type::message_val;
        if (scalar_type_from_name(ref, st) && !ref.empty()) {
            f.type = st;
            return true;
        }
    }
    f.type = scalar_type::message_val;
    f.is_message = true;
    f.type_ref = ref;
    return true;
}
inline bool parser::parse_options(schema_field& f) {
    if (failed()) {
        return false;
    }
    adv(); // '['
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == token::kind::rbracket) {
            adv();
            return true;
        }
        if (cur.k == token::kind::comma) {
            adv();
            continue;
        }
        if (cur.k == token::kind::lparen) {
            if (!skip_paren_group()) {
                return false;
            }
            continue;
        }
        if (cur.k != token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        const std::string name(cur.text);
        const char* const pos = cur.begin;
        adv();
        if (cur.k == token::kind::eq) {
            adv();
            if (!parse_option_value(f, name, pos)) {
                return false;
            }
        } else if (name == "packed") {
            // bare `packed` means packed = true
            f.packed_explicit = true;
            f.packed = true;
        }
    }
}

inline void apply_default_int(schema_field& f, std::int64_t v) {
    if (f.mode == field_mode::repeated || f.is_message) {
        return;
    }
    f.has_default = true;
    if (f.type == scalar_type::float_val ||
        f.type == scalar_type::double_val) {
        f.dflt_real = static_cast<double>(v);
    } else {
        f.dflt_int = v;
    }
}

inline void apply_default_real(schema_field& f, double v) {
    if (f.mode == field_mode::repeated || f.is_message) {
        return;
    }
    f.has_default = true;
    if (f.type == scalar_type::float_val ||
        f.type == scalar_type::double_val) {
        f.dflt_real = v;
    } else {
        f.dflt_int = static_cast<std::int64_t>(v);
    }
}

inline void apply_default_str(schema_field& f, std::string v) {
    if (f.mode == field_mode::repeated || f.is_message) {
        return;
    }
    if (f.type == scalar_type::string_val ||
        f.type == scalar_type::bytes_val) {
        f.has_default = true;
        f.dflt_str = std::move(v);
    }
}

inline bool parser::parse_option_value(schema_field& f,
                                       const std::string& name,
                                       const char* pos) {
    bool neg = false;
    if (cur.k == token::kind::minus) {
        neg = true;
        adv();
    }
    if (cur.k == token::kind::number) {
        if (cur.is_real) {
            double d = cur.dval;
            adv();
            if (neg) {
                d = -d;
            }
            if (name == "default") {
                apply_default_real(f, d);
            }
            return !failed();
        }
        const std::uint64_t v = cur.ival;
        adv();
        if (name == "packed") {
            f.packed_explicit = true;
            f.packed = (v != 0);
        } else if (name == "default") {
            std::int64_t nv = 0;
            if (neg) {
                nv = (v == 0x8000000000000000ull)
                         ? (std::numeric_limits<std::int64_t>::min)()
                         : -static_cast<std::int64_t>(v);
            } else {
                if (v > 0x7FFFFFFFFFFFFFFFull) {
                    set_error(error_code::invalid_number, pos);
                    return false;
                }
                nv = static_cast<std::int64_t>(v);
            }
            f.dflt_pending_set = true;
            f.dflt_pending_int = nv;
            apply_default_int(f, nv);
        }
        return !failed();
    }
    if (cur.k == token::kind::str) {
        std::string s = cur.sval;
        adv();
        if (name == "default") {
            apply_default_str(f, std::move(s));
        }
        return !failed();
    }
    if (cur.k == token::kind::ident) {
        const std::string id(cur.text);
        adv();
        if (name == "packed") {
            f.packed_explicit = true;
            f.packed = (id == "true");
        } else if (name == "default") {
            if (id == "true") {
                apply_default_int(f, 1);
            } else if (id == "false") {
                apply_default_int(f, 0);
            } else if (id == "nan") {
                apply_default_real(f,
                                   std::numeric_limits<double>::quiet_NaN());
            } else if (id == "inf") {
                apply_default_real(
                    f, neg ? -std::numeric_limits<double>::infinity()
                           : std::numeric_limits<double>::infinity());
            } else {
                // An enum value name: keep the symbol so the resolution pass
                // can compute its numeric value via the enum table.
                f.dflt_enum_name = id;
                f.has_default = true;
            }
        }
        return !failed();
    }
    set_error(error_code::unexpected_token, cur.begin);
    return false;
}

inline bool parser::parse_enum(schema_desc& d,
                               std::vector<std::string>& stack) {
    if (failed()) {
        return false;
    }
    adv(); // 'enum'
    if (cur.k != token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    const std::string name(cur.text);
    std::string full = name;
    if (!stack.empty()) {
        full = stack.back() + "." + name;
    }
    d.enums.push_back(full);
    schema_enum ed;
    ed.full_name = full;
    // First value defaults to 0; each later value defaults to (prev + 1).
    std::int64_t next_default = 0;
    adv();
    if (!expect_kind(token::kind::lbrace)) {
        return false;
    }
    int depth = 0;
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == token::kind::lbrace ||
            cur.k == token::kind::lbracket) {
            ++depth;
            adv();
            continue;
        }
        if (cur.k == token::kind::rbracket) {
            if (depth > 0) {
                --depth;
            }
            adv();
            continue;
        }
        if (cur.k == token::kind::rbrace) {
            if (depth == 0) {
                adv();
                break;
            }
            --depth;
            adv();
            continue;
        }
        if (cur.k == token::kind::semi) {
            adv();
            continue;
        }
        if (have_ident("option")) {
            if (depth == 0 && !skip_statement()) {
                return false;
            }
            continue;
        }
        if (cur.k == token::kind::ident) {
            const std::string vname(cur.text);
            adv();
            std::int64_t v = next_default;
            const char* num_pos = cur.begin;
            if (cur.k == token::kind::eq) {
                adv();
                if (cur.k == token::kind::minus) {
                    adv();
                    if (cur.k != token::kind::number || cur.is_real) {
                        set_error(error_code::unexpected_token, cur.begin);
                        return false;
                    }
                    const std::uint64_t uv = cur.ival;
                    v = (uv == 0x8000000000000000ull)
                            ? (std::numeric_limits<std::int64_t>::min)()
                            : -static_cast<std::int64_t>(uv);
                    num_pos = cur.begin;
                    adv();
                } else if (cur.k == token::kind::number && !cur.is_real) {
                    const std::uint64_t uv = cur.ival;
                    if (uv > 0x7FFFFFFFFFFFFFFFull) {
                        set_error(error_code::invalid_number, num_pos);
                        return false;
                    }
                    v = static_cast<std::int64_t>(uv);
                    adv();
                } else {
                    set_error(error_code::unexpected_token, cur.begin);
                    return false;
                }
            }
            // G-2: enum constants are int32 in both proto2 and proto3 (protoc
            // rejects wider values). Reject before truncation so a constant
            // outside [INT32_MIN, INT32_MAX] is an error instead of silently
            // wrapping (e.g. 2147483648 -> -2147483648).
            if (v > (std::numeric_limits<std::int32_t>::max)() ||
                v < (std::numeric_limits<std::int32_t>::min)()) {
                set_error(error_code::proto_bad_field, num_pos);
                return false;
            }
            next_default = (v == (std::numeric_limits<std::int64_t>::max)())
                               ? v
                               : v + 1;
            schema_enum_value ev;
            ev.name = vname;
            ev.value = static_cast<std::int32_t>(v);
            // proto3 requires the first enum value to be zero.
            if (syn_ == syntax::proto3 && ed.values.empty() && ev.value != 0) {
                set_error(error_code::unexpected_token, num_pos);
                return false;
            }
            // Duplicate numeric values are rejected (option allow_alias is not
            // implemented; protoc forbids them without it).
            for (const schema_enum_value& prior : ed.values) {
                if (prior.value == ev.value) {
                    set_error(error_code::proto_bad_field, num_pos);
                    return false;
                }
            }
            // Duplicate value names are rejected.
            for (const schema_enum_value& prior : ed.values) {
                if (prior.name == ev.name) {
                    set_error(error_code::proto_bad_field, num_pos);
                    return false;
                }
            }
            ed.values.push_back(std::move(ev));
            if (cur.k == token::kind::semi) {
                adv();
            }
            continue;
        }
        adv();
    }
    d.enum_defs.push_back(std::move(ed));
    return true;
}

inline bool parser::skip_statement() {
    int depth = 0;
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == token::kind::lbrace ||
            cur.k == token::kind::lbracket ||
            cur.k == token::kind::lparen) {
            ++depth;
            adv();
            continue;
        }
        if (cur.k == token::kind::rbrace ||
            cur.k == token::kind::rbracket ||
            cur.k == token::kind::rparen) {
            if (depth > 0) {
                --depth;
                adv();
                continue;
            }
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == token::kind::semi && depth == 0) {
            adv();
            return true;
        }
        adv();
    }
}

inline bool parser::skip_paren_group() {
    int depth = 0;
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == token::kind::lparen ||
            cur.k == token::kind::lbracket ||
            cur.k == token::kind::lbrace) {
            ++depth;
            adv();
            continue;
        }
        if (cur.k == token::kind::rparen ||
            cur.k == token::kind::rbracket ||
            cur.k == token::kind::rbrace) {
            if (depth > 0) {
                --depth;
                adv();
                continue;
            }
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        adv();
    }
}

// ---------------------------------------------------------------------------
// Type resolution and packed-wire derivation
// ---------------------------------------------------------------------------

inline bool nends_with(const std::string& s,
                       const std::string& suf) noexcept {
    return s.size() >= suf.size() &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

inline bool resolve_message_ref(
    const schema_desc& d,
    const std::unordered_map<std::string, std::size_t>& full,
    const std::unordered_map<std::string, std::size_t>& simple,
    const std::string& scope, const std::string& ref, std::size_t& out) {
    // Scope-aware relative-name resolution (protoc semantics): walk the
    // enclosing message scope chain from the innermost level outward and
    // prefer the nearest complete match. For scope "A.B" and ref "Inner" try
    // "A.B.Inner", then "A.Inner", then "Inner". This binds a shadowed name
    // to the field's own message (A.Inner / B.Inner) instead of whichever
    // same-named message happened to be registered first globally.
    {
        std::string s = scope;
        for (;;) {
            const std::string cand = s.empty() ? ref : s + "." + ref;
            const auto sit = full.find(cand);
            if (sit != full.end()) {
                out = sit->second;
                return true;
            }
            if (s.empty()) {
                break;
            }
            const std::size_t dot = s.rfind('.');
            s = (dot == std::string::npos) ? "" : s.substr(0, dot);
        }
    }
    // Lenient fallbacks preserved for forward/map-entry/cross-scope refs: a
    // bare simple-name global lookup first, then a dotted-suffix scan.
    if (ref.find('.') == std::string::npos) {
        const auto it2 = simple.find(ref);
        if (it2 != simple.end()) {
            out = it2->second;
            return true;
        }
    }
    const std::string dotref = "." + ref;
    for (std::size_t i = 0; i < d.messages.size(); ++i) {
        if (nends_with(d.messages[i].full_name, dotref) ||
            d.messages[i].full_name == ref) {
            out = i;
            return true;
        }
    }
    return false;
}

inline std::size_t resolve_enum_ref(const schema_desc& d,
                                    const std::string& scope,
                                    const std::string& ref) {
    // Same scope-aware walk as resolve_message_ref: a relative enum reference
    // binds to the nearest enclosing message scope's enum first.
    {
        std::string s = scope;
        for (;;) {
            const std::string cand = s.empty() ? ref : s + "." + ref;
            for (std::size_t i = 0; i < d.enum_defs.size(); ++i) {
                if (d.enum_defs[i].full_name == cand) {
                    return i;
                }
            }
            if (s.empty()) {
                break;
            }
            const std::size_t dot = s.rfind('.');
            s = (dot == std::string::npos) ? "" : s.substr(0, dot);
        }
    }
    const std::string dotref = "." + ref;
    for (std::size_t i = 0; i < d.enum_defs.size(); ++i) {
        const std::string& e = d.enum_defs[i].full_name;
        if (e == ref || nends_with(e, dotref)) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

// Resolve an `extend` block's extendee name to a message index: scope-aware
// relative resolution (protoc semantics -- the extendee binds inside the
// extend block's own enclosing scope first) with the the lenient
// simple-name/dotted-suffix fallback of a regular type reference. A leading
// dot is an absolute reference and resolves from the global scope only.
// G-5: when the name still does not bind, its leading components are reduced
// one by one ("foo.Baz" binds the message registered as just "Baz"), matching
// package prefixes this parser does not record.
inline bool resolve_extendee(
    const schema_desc& d,
    const std::unordered_map<std::string, std::size_t>& full,
    const std::unordered_map<std::string, std::size_t>& simple,
    const std::string& scope, const std::string& ref, std::size_t& out) {
    if (resolve_message_ref(d, full, simple, scope, ref, out)) {
        return true;
    }
    std::string cand = ref;
    for (std::size_t dot = cand.find('.');
         dot != std::string::npos; dot = cand.find('.')) {
        cand = cand.substr(dot + 1);
        const auto it = full.find(cand);
        if (it != full.end()) {
            out = it->second;
            return true;
        }
    }
    return false;
}

// Apply enum default semantics to a field the resolution pass has confirmed as
// an enum reference: an explicit `[default = NAME]` / `[default = N]` wins,
// otherwise the implicit default is the enum's first value (protoc semantics --
// proto2 uses the first listed value, proto3 requires it to be zero).
inline bool resolve_enum_defaults(schema_desc& d,
                                  const std::string& enum_full,
                                  schema_field& f, error_info& err) {
    std::size_t idx = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < d.enum_defs.size(); ++i) {
        if (d.enum_defs[i].full_name == enum_full) {
            idx = i;
            break;
        }
    }
    if (idx == static_cast<std::size_t>(-1)) {
        err.code = error_code::unexpected_token;
        err.offset = f.type_offset;
        return false;
    }
    const schema_enum& ed = d.enum_defs[idx];
    if (!f.dflt_enum_name.empty()) {
        for (const schema_enum_value& ev : ed.values) {
            if (ev.name == f.dflt_enum_name) {
                f.dflt_int = ev.value;
                f.has_default = true;
                return true;
            }
        }
        // Unknown enum value name in [default = NAME].
        err.code = error_code::unexpected_token;
        err.offset = f.type_offset;
        return false;
    }
    if (f.dflt_pending_set) {
        f.dflt_int = f.dflt_pending_int;
        f.has_default = true;
        return true;
    }
    if (!f.has_default && !ed.values.empty()) {
        f.dflt_int = ed.values.front().value;
        f.has_default = true;
    }
    return true;
}

inline bool resolve_types(schema_desc& d, error_info& err) {
    std::unordered_map<std::string, std::size_t> full;
    std::unordered_map<std::string, std::size_t> simple;
    for (std::size_t i = 0; i < d.messages.size(); ++i) {
        full.emplace(d.messages[i].full_name, i);
        if (simple.find(d.messages[i].name) == simple.end()) {
            simple.emplace(d.messages[i].name, i);
        }
    }
    for (std::size_t i = 0; i < d.messages.size(); ++i) {
        for (auto& f : d.messages[i].fields) {
            // Already-resolved enum fields (is_enum) keep their tabulated
            // default and are never re-examined.
            if (f.is_enum) {
                continue;
            }
            // A symbol default can only be valid on a field that resolves to
            // an enum; on a plain scalar it is a malformed (or misquoted)
            // declaration and is rejected instead of being silently dropped.
            if (!f.dflt_enum_name.empty() &&
                (!f.is_message || f.type_ref.empty())) {
                err.code = error_code::unexpected_token;
                err.offset = f.type_offset;
                return false;
            }
            if (!f.is_message || f.type_ref.empty()) {
                continue;
            }
            std::string norm = f.type_ref;
            // A leading dot is an absolute (global) reference: it resolves at
            // the top scope only, never relative to the enclosing message.
            const bool absolute = (!norm.empty() && norm.front() == '.');
            if (absolute) {
                norm.erase(0, 1);
            }
            const std::string scope =
                absolute ? std::string() : d.messages[i].full_name;
            std::size_t target = 0;
            if (resolve_message_ref(d, full, simple, scope, norm, target)) {
                // A message-typed field carrying a symbol default is rejected
                // (protoc forbids defaults on message fields).
                if (!f.dflt_enum_name.empty()) {
                    err.code = error_code::unexpected_token;
                    err.offset = f.type_offset;
                    return false;
                }
                f.type = scalar_type::message_val;
                f.type_index = target;
                f.is_enum = false;
                continue;
            }
            const std::size_t eidx = resolve_enum_ref(d, scope, norm);
            if (eidx != static_cast<std::size_t>(-1)) {
                f.type = scalar_type::enum_val;
                f.type_index = static_cast<std::size_t>(-1);
                f.is_message = false;
                f.is_enum = true;
                if (!resolve_enum_defaults(d, d.enum_defs[eidx].full_name, f,
                                           err)) {
                    return false;
                }
                continue;
            }
            // A type reference that resolves to neither a local message nor a
            // local enum. When the file declares imports, the reference may
            // come from an imported (not yet merged) schema: keep it as an
            // opaque external message -- length-delimited, byte-preserving at
            // encode/decode time, with no field table of its own. Without any
            // import, an unresolved name stays a hard parse error.
            if (!d.imports.empty()) {
                f.type = scalar_type::message_val;
                f.type_index = static_cast<std::size_t>(-1);
                f.is_enum = false;
                continue;
            }
            err.code = error_code::unexpected_token;
            err.offset = f.type_offset;
            return false;
        }
    }
    // Resolve `extend` block field types the same way (message refs stay
    // message_val, enum refs become enum_val; unresolved names are opaque when
    // the file imports, a hard error otherwise). Unlike regular message
    // fields, the scope is NOT derivable from the extension's own data, so it
    // comes from the scope recorded at parse time (g_extend_scopes). The
    // extendee name is resolved and canonicalized to the target's full name so
    // compile-time registration can look it up exactly.
    const auto ext_scope = g_extend_scopes.find(&d);
    for (std::size_t i = 0; i < d.extensions.size(); ++i) {
        schema_extension& se = d.extensions[i];
        const std::string scope =
            (ext_scope != g_extend_scopes.end() && i < ext_scope->second.size())
                ? ext_scope->second[i]
                : std::string();
        // Extendee (G-1/G-5): resolve relative to the extend block's scope; a
        // leading dot is absolute. When it resolves, the stored extendee is
        // canonicalized to the message's full name; an unresolved name is kept
        // raw and rejected (or bound after a multi-file merge) at compile time.
        {
            std::string norm = se.extendee;
            const bool absolute = (!norm.empty() && norm.front() == '.');
            if (absolute) {
                norm.erase(0, 1);
            }
            std::size_t target = static_cast<std::size_t>(-1);
            if (resolve_extendee(d, full, simple,
                                 absolute ? std::string() : scope, norm,
                                 target)) {
                se.extendee = d.messages[target].full_name;
            }
        }
        schema_field& f = se.field;
        if (!f.is_message || f.type_ref.empty()) {
            continue;
        }
        // Already resolved at parse time: the type_index stays valid (merging
        // only appends messages) and must not be re-resolved with an empty
        // scope, which would break scope-shadowed binds.
        if (f.type_index != static_cast<std::size_t>(-1)) {
            continue;
        }
        std::string norm = f.type_ref;
        const bool absolute = (!norm.empty() && norm.front() == '.');
        if (absolute) {
            norm.erase(0, 1);
        }
        const std::string fscope =
            absolute ? std::string() : scope;
        std::size_t target = 0;
        if (resolve_message_ref(d, full, simple, fscope, norm, target)) {
            f.type = scalar_type::message_val;
            f.type_index = target;
            f.is_enum = false;
            continue;
        }
        const std::size_t eidx = resolve_enum_ref(d, fscope, norm);
        if (eidx != static_cast<std::size_t>(-1)) {
            f.type = scalar_type::enum_val;
            f.type_index = static_cast<std::size_t>(-1);
            f.is_message = false;
            f.is_enum = true;
            if (!resolve_enum_defaults(d, d.enum_defs[eidx].full_name, f,
                                       err)) {
                return false;
            }
            continue;
        }
        if (!d.imports.empty()) {
            f.type = scalar_type::message_val;
            f.type_index = static_cast<std::size_t>(-1);
            f.is_enum = false;
            continue;
        }
        err.code = error_code::unexpected_token;
        err.offset = f.type_offset;
        return false;
    }
    return true;
}

inline void finalize_packed(schema_field& f, syntax s) {
    if (f.mode != field_mode::repeated) {
        f.packed = false;
        return;
    }
    const wire_type w = wire_of_scalar(f.type);
    if (!is_numeric_wire(w)) {
        f.packed = false;
        return;
    }
    f.packed = f.packed_explicit ? f.packed : (s == syntax::proto3);
}

// Compare two map-entry "key" scalar nodes (string / integer / boolean). The
// map-key merge (last-wins) uses this to drop earlier duplicate entries.
inline bool proto_node_keys_equal(const rtti::value_io* a,
                                  const rtti::value_io* b) noexcept {
    if (a == nullptr || b == nullptr || a->type() != b->type()) {
        return false;
    }
    if (a->type() == rtti::value_io::kind::string) {
        rtti::string_view sa;
        rtti::string_view sb;
        return a->as_string(sa) && b->as_string(sb) && sa == sb;
    }
    if (a->type() == rtti::value_io::kind::integer) {
        std::int64_t ia = 0;
        std::int64_t ib = 0;
        return a->as_integer(ia) && b->as_integer(ib) && ia == ib;
    }
    if (a->type() == rtti::value_io::kind::boolean) {
        bool ba = false;
        bool bb = false;
        return a->as_bool(ba) && b->as_bool(bb) && ba == bb;
    }
    return false;
}

struct compiled_message;
struct dynamic_field;

// One compiled message; the tree-path fields own their dynamic_field objects.
// The copy operations are deleted because dynamic_field holds pointers into
// non-relocatable memory; move is fine (nodes are heap-allocated).
struct compiled_message {
    std::string name;
    schema_info si{};
    std::vector<const field_base*> field_ptrs;
    std::vector<std::unique_ptr<dynamic_field>> fields;

    compiled_message();
    compiled_message(compiled_message&&) noexcept = default;
    compiled_message& operator=(compiled_message&&) noexcept = default;
    compiled_message(const compiled_message&) = delete;
    compiled_message& operator=(const compiled_message&) = delete;
    ~compiled_message();
};

struct dynamic_field : field_base {
    scalar_type st = scalar_type::int32;
    bool is_message = false;
    bool is_enum = false;
    bool is_map = false;
    const compiled_message* child = nullptr; // for message-typed fields
    std::string name_storage;
    std::string dflt_storage;
    std::string type_ref;

    explicit dynamic_field(const schema_field& f) : field_base(field_desc{}) {
        name_storage = f.name;
        info.name = name_storage.c_str();
        info.number = f.number;
        info.wire = wire_of_scalar(f.type);
        info.mode = f.mode;
        info.packed = f.packed;
        info.oneof_group = f.oneof_group;
        info.emit_present = f.emit_present;
        st = f.type;
        is_message = f.is_message;
        is_enum = f.is_enum;
        type_ref = f.type_ref;
        is_map = f.is_map;
        if (f.has_default) {
            info.has_default = true;
            info.dflt_int = f.dflt_int;
            info.dflt_real = f.dflt_real;
            if (f.type == scalar_type::string_val ||
                f.type == scalar_type::bytes_val) {
                dflt_storage = f.dflt_str;
                info.dflt_str = dflt_storage.c_str();
                info.dflt_str_len = dflt_storage.size();
            }
        }
    }

    bool is_sint() const noexcept {
        return st == scalar_type::sint32 || st == scalar_type::sint64;
    }

    error_code tree_write_varint(const rtti::value_io* node,
                                 detail::wire_writer& w) const {
        const rtti::value_io::kind k = node->type();
        if (k == rtti::value_io::kind::boolean) {
            bool b = false;
            if (!node->as_bool(b)) {
                return error_code::kind_mismatch;
            }
            w.varint(b ? 1 : 0);
            return error_code::ok;
        }
        if (k == rtti::value_io::kind::integer) {
            std::int64_t i = 0;
            if (!node->as_integer(i)) {
                return error_code::kind_mismatch;
            }
            w.varint(is_sint() ? zigzag_encode(i)
                               : static_cast<std::uint64_t>(i));
            return error_code::ok;
        }
        return error_code::kind_mismatch;
    }

    error_code tree_write_fixed64(const rtti::value_io* node,
                                  detail::wire_writer& w) const {
        if (st == scalar_type::double_val) {
            double d = 0.0;
            if (node->type() == rtti::value_io::kind::integer) {
                std::int64_t i = 0;
                if (!node->as_integer(i)) {
                    return error_code::kind_mismatch;
                }
                d = static_cast<double>(i);
            } else if (node->type() == rtti::value_io::kind::real) {
                if (!node->as_real(d)) {
                    return error_code::kind_mismatch;
                }
            } else {
                return error_code::kind_mismatch;
            }
            std::uint64_t bits = 0;
            std::memcpy(&bits, &d, sizeof(double));
            w.fixed64(bits);
            return error_code::ok;
        }
        std::int64_t i = 0;
        if (node->type() == rtti::value_io::kind::integer) {
            if (!node->as_integer(i)) {
                return error_code::kind_mismatch;
            }
        } else if (node->type() == rtti::value_io::kind::real) {
            double d = 0.0;
            if (!node->as_real(d)) {
                return error_code::kind_mismatch;
            }
            i = static_cast<std::int64_t>(d);
        } else {
            return error_code::kind_mismatch;
        }
        w.fixed64(static_cast<std::uint64_t>(i));
        return error_code::ok;
    }

    error_code tree_write_fixed32(const rtti::value_io* node,
                                  detail::wire_writer& w) const {
        if (st == scalar_type::float_val) {
            double d = 0.0;
            if (node->type() == rtti::value_io::kind::integer) {
                std::int64_t i = 0;
                if (!node->as_integer(i)) {
                    return error_code::kind_mismatch;
                }
                d = static_cast<double>(i);
            } else if (node->type() == rtti::value_io::kind::real) {
                if (!node->as_real(d)) {
                    return error_code::kind_mismatch;
                }
            } else {
                return error_code::kind_mismatch;
            }
            const float f = static_cast<float>(d);
            std::uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(float));
            w.fixed32(bits);
            return error_code::ok;
        }
        std::int64_t i = 0;
        if (node->type() == rtti::value_io::kind::integer) {
            if (!node->as_integer(i)) {
                return error_code::kind_mismatch;
            }
        } else if (node->type() == rtti::value_io::kind::real) {
            double d = 0.0;
            if (!node->as_real(d)) {
                return error_code::kind_mismatch;
            }
            i = static_cast<std::int64_t>(d);
        } else {
            return error_code::kind_mismatch;
        }
        w.fixed32(static_cast<std::uint32_t>(i));
        return error_code::ok;
    }

    error_code tree_write_length(const rtti::value_io* node,
                                 detail::wire_writer& w, std::size_t depth,
                                 std::size_t max_depth) const {
        if (is_message) {
            if (child == nullptr) {
                // Opaque external message (a type from an imported file whose
                // schema was not merged in): the tree carries its raw payload
                // verbatim, so the bytes pass through untouched.
                rtti::string_view raw;
                if (node->type() != rtti::value_io::kind::string ||
                    !node->as_string(raw)) {
                    return error_code::kind_mismatch;
                }
                w.varint(raw.size());
                w.bytes(raw.data(), raw.size());
                return error_code::ok;
            }
            if (node->type() != rtti::value_io::kind::object) {
                return error_code::kind_mismatch;
            }
            std::string payload;
            detail::wire_writer pw(payload);
            const error_code e = detail::encode_message_tree(
                node, pw, child->si, depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            w.varint(payload.size());
            w.bytes(payload.data(), payload.size());
            return error_code::ok;
        }
        rtti::string_view s;
        if (node->type() != rtti::value_io::kind::string ||
            !node->as_string(s)) {
            return error_code::kind_mismatch;
        }
        w.varint(s.size());
        w.bytes(s.data(), s.size());
        return error_code::ok;
    }

    error_code tree_set_length(rtti::value_io* node, rtti::string_view data,
                               std::size_t depth,
                               std::size_t max_depth) const {
        if (is_message) {
            if (child == nullptr) {
                // Opaque external message: keep the raw payload so a later
                // encode reproduces the exact imported-file bytes.
                node->make_scalar(rtti::field_value::of(data));
                return error_code::ok;
            }
            detail::wire_reader sub(data);
            node->make_object();
            const error_code e = detail::decode_message_tree(
                node, sub, child->si, depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            if (sub.remaining() != 0) {
                return error_code::proto_truncated;
            }
            return error_code::ok;
        }
        node->make_scalar(rtti::field_value::of(data));
        return error_code::ok;
    }

    // --- field_base overrides -------------------------------------------------

    error_code encode_obj(const void*, detail::wire_writer&, std::size_t,
                          std::size_t) const override {
        return error_code::kind_mismatch; // no C++ member behind a text schema
    }
    error_code decode_obj(void*, detail::wire_reader&, std::size_t,
                          std::size_t) const override {
        return error_code::kind_mismatch;
    }
    error_code decode_packed(void*, detail::wire_reader&, std::size_t,
                             std::size_t) const override {
        return error_code::kind_mismatch;
    }
    error_code decode_repeated(void*, detail::wire_reader&, std::size_t,
                               std::size_t) const override {
        return error_code::kind_mismatch;
    }
    bool should_encode(const void*) const override { return true; }
    error_code set_default(void*) const override { return error_code::ok; }

    error_code tree_encode(const rtti::value_io* node,
                           detail::wire_writer& w, std::size_t depth,
                           std::size_t max_depth) const override {
        if (node == nullptr) {
            return error_code::ok;
        }
        switch (info.wire) {
            case wire_type::varint:
                return tree_write_varint(node, w);
            case wire_type::fixed64:
                return tree_write_fixed64(node, w);
            case wire_type::fixed32:
                return tree_write_fixed32(node, w);
            case wire_type::length:
                return tree_write_length(node, w, depth, max_depth);
            default:
                return error_code::proto_bad_field;
        }
    }

    error_code tree_decode(detail::wire_reader& r, rtti::value_io* node,
                           std::size_t depth,
                           std::size_t max_depth) const override {
        switch (info.wire) {
            case wire_type::varint: {
                std::uint64_t v = 0;
                if (!r.read_varint(v)) {
                    return error_code::proto_truncated;
                }
                if (st == scalar_type::bool_val) {
                    // Match the schema<T> tree bridge: bool decodes to a
                    // boolean node, not an integer.
                    node->make_scalar(rtti::field_value::of(v != 0));
                } else if (is_sint()) {
                    node->make_scalar(rtti::field_value::of(zigzag_decode(v)));
                } else {
                    node->make_scalar(rtti::field_value::of(
                        static_cast<std::int64_t>(v)));
                }
                return error_code::ok;
            }
            case wire_type::fixed64: {
                std::uint64_t v = 0;
                if (!r.read_fixed64(v)) {
                    return error_code::proto_truncated;
                }
                if (st == scalar_type::double_val) {
                    double d = 0.0;
                    std::memcpy(&d, &v, sizeof(double));
                    node->make_scalar(rtti::field_value::of(d));
                } else {
                    node->make_scalar(rtti::field_value::of(
                        static_cast<std::int64_t>(v)));
                }
                return error_code::ok;
            }
            case wire_type::fixed32: {
                std::uint32_t v = 0;
                if (!r.read_fixed32(v)) {
                    return error_code::proto_truncated;
                }
                if (st == scalar_type::float_val) {
                    float f = 0.0f;
                    std::memcpy(&f, &v, sizeof(float));
                    node->make_scalar(
                        rtti::field_value::of(static_cast<double>(f)));
                } else {
                    node->make_scalar(rtti::field_value::of(
                        static_cast<std::int64_t>(v)));
                }
                return error_code::ok;
            }
            case wire_type::length: {
                std::uint64_t len = 0;
                if (!r.read_length(len)) {
                    return error_code::proto_truncated;
                }
                const rtti::string_view data = r.take(len);
                return tree_set_length(node, data, depth, max_depth);
            }
            default:
                return error_code::proto_bad_field;
        }
    }

    error_code tree_decode_packed(rtti::value_io* obj,
                                  detail::wire_reader& r,
                                  std::size_t depth,
                                  std::size_t max_depth) const override {
        std::uint64_t len = 0;
        if (!r.read_length(len)) {
            return error_code::proto_truncated;
        }
        if (len > r.remaining()) {
            return error_code::proto_truncated;
        }
        const rtti::string_view payload = r.take(len);
        detail::wire_reader sub(payload);
        rtti::value_io* const arr = detail::ensure_array(obj, info.name);
        while (sub.remaining() > 0) {
            rtti::value_io* el = obj->make_node();
            const error_code e = tree_decode(sub, el, depth, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            arr->push_back(el);
        }
        return error_code::ok;
    }

    error_code tree_decode_repeated(rtti::value_io* obj,
                                    detail::wire_reader& r,
                                    std::size_t depth,
                                    std::size_t max_depth) const override {
        rtti::value_io* const arr = detail::ensure_array(obj, info.name);
        rtti::value_io* el = obj->make_node();
        const error_code e = tree_decode(r, el, depth, max_depth);
        if (e != error_code::ok) {
            return e;
        }
        if (is_map && child != nullptr) {
            // Merge duplicate map keys like protoc (last value wins): drop any
            // earlier array entry whose "key" node equals the new entry's key
            // before appending. The array holds proto document nodes, so
            // detail::node::remove_at is safe here.
            rtti::value_io* new_key = nullptr;
            (void)el->get_key("key", new_key);
            rtti::value_io* const base = arr;
            for (std::size_t i = arr->size(); i > 0; --i) {
                rtti::value_io* existing = arr->at(i - 1);
                rtti::value_io* ek = nullptr;
                if (existing != nullptr &&
                    existing->get_key("key", ek) && ek != nullptr &&
                    new_key != nullptr &&
                    proto_node_keys_equal(ek, new_key)) {
                    static_cast<detail::node*>(base)->remove_at(i - 1);
                }
            }
        }
        arr->push_back(el);
        return error_code::ok;
    }

    bool tree_equals_default(const rtti::value_io* node) const override {
        if (node == nullptr) {
            return false;
        }
        const rtti::value_io::kind k = node->type();
        const std::int64_t di = info.has_default ? info.dflt_int : 0;
        switch (info.wire) {
            case wire_type::varint: {
                if (k == rtti::value_io::kind::boolean) {
                    bool b = false;
                    if (!node->as_bool(b)) {
                        return false;
                    }
                    return (b ? 1 : 0) == di;
                }
                if (k == rtti::value_io::kind::integer) {
                    std::int64_t i = 0;
                    if (!node->as_integer(i)) {
                        return false;
                    }
                    return i == di;
                }
                return false;
            }
            case wire_type::fixed64: {
                if (st == scalar_type::double_val) {
                    double d = 0.0;
                    if (k == rtti::value_io::kind::real) {
                        if (!node->as_real(d)) {
                            return false;
                        }
                    } else if (k == rtti::value_io::kind::integer) {
                        std::int64_t i = 0;
                        if (!node->as_integer(i)) {
                            return false;
                        }
                        d = static_cast<double>(i);
                    } else {
                        return false;
                    }
                    return d == (info.has_default ? info.dflt_real : 0.0);
                }
                if (k == rtti::value_io::kind::integer) {
                    std::int64_t i = 0;
                    if (!node->as_integer(i)) {
                        return false;
                    }
                    return i == di;
                }
                return false;
            }
            case wire_type::fixed32: {
                if (st == scalar_type::float_val) {
                    double d = 0.0;
                    if (k == rtti::value_io::kind::real) {
                        if (!node->as_real(d)) {
                            return false;
                        }
                    } else if (k == rtti::value_io::kind::integer) {
                        std::int64_t i = 0;
                        if (!node->as_integer(i)) {
                            return false;
                        }
                        d = static_cast<double>(i);
                    } else {
                        return false;
                    }
                    return d == (info.has_default ? info.dflt_real : 0.0);
                }
                if (k == rtti::value_io::kind::integer) {
                    std::int64_t i = 0;
                    if (!node->as_integer(i)) {
                        return false;
                    }
                    return i == di;
                }
                return false;
            }
            case wire_type::length: {
                if (is_message) {
                    return false;
                }
                if (k != rtti::value_io::kind::string) {
                    return false;
                }
                rtti::string_view s;
                if (!node->as_string(s)) {
                    return false;
                }
                if (!info.has_default || info.dflt_str == nullptr) {
                    return s.empty();
                }
                return s ==
                       rtti::string_view(info.dflt_str, info.dflt_str_len);
            }
            default:
                return false;
        }
    }

    error_code tree_set_default(rtti::value_io* node) const override {
        const std::int64_t di = info.has_default ? info.dflt_int : 0;
        switch (info.wire) {
            case wire_type::varint:
                if (st == scalar_type::bool_val) {
                    node->make_scalar(
                        rtti::field_value::of(info.has_default ? (di != 0)
                                                               : false));
                } else {
                    node->make_scalar(rtti::field_value::of(di));
                }
                return error_code::ok;
            case wire_type::fixed64:
                if (st == scalar_type::double_val) {
                    node->make_scalar(rtti::field_value::of(
                        info.has_default ? info.dflt_real : 0.0));
                } else {
                    node->make_scalar(rtti::field_value::of(di));
                }
                return error_code::ok;
            case wire_type::fixed32:
                if (st == scalar_type::float_val) {
                    node->make_scalar(rtti::field_value::of(
                        info.has_default ? info.dflt_real
                                         : static_cast<double>(0)));
                } else {
                    node->make_scalar(rtti::field_value::of(di));
                }
                return error_code::ok;
            case wire_type::length:
                if (is_message) {
                    node->make_scalar(rtti::field_value{});
                    return error_code::ok;
                }
                if (info.has_default && info.dflt_str != nullptr) {
                    node->make_scalar(rtti::field_value::of(rtti::string_view(
                        info.dflt_str, info.dflt_str_len)));
                } else {
                    node->make_scalar(
                        rtti::field_value::of(rtti::string_view("", 0)));
                }
                return error_code::ok;
            default:
                return error_code::proto_bad_field;
        }
    }
};

// Out-of-line so dynamic_field is complete when the unique_ptr<dynamic_field>
// deleter is instantiated.
inline compiled_message::compiled_message() = default;
inline compiled_message::~compiled_message() = default;

} // namespace detail

// ---------------------------------------------------------------------------
// Public: parse .proto text into a schema_desc
// ---------------------------------------------------------------------------

// Parse a .proto descriptor. Returns error_code (with err.offset / line() /
// column()) instead of throwing ever. Empty input parses to an empty schema.
result<schema_desc> parse_schema(rtti::string_view src) {
    result<schema_desc> r;
    schema_desc d;
    // Scope side channel: reset for this descriptor and erased on every exit
    // path, so a reused stack address can never observe a stale entry.
    detail::extend_scope_guard guard(&d);
    detail::parser p(src);
    if (!p.parse_file(d)) {
        r.err = p.err;
        r.err.src = src.data();
        r.err.src_len = src.size();
        return r;
    }
    // Dependency-free ordering: top-level messages first (declaration order),
    // nested messages after them, so the default root (index 0) is the first
    // top-level message.
    std::stable_partition(
        d.messages.begin(), d.messages.end(),
        [](const schema_message& m) {
            return m.full_name.find('.') == std::string::npos;
        });
    error_info err;
    err.src = src.data();
    err.src_len = src.size();
    if (!detail::resolve_types(d, err)) {
        r.err = err;
        return r;
    }
    for (auto& m : d.messages) {
        for (auto& f : m.fields) {
            detail::finalize_packed(f, d.syn);
        }
    }
    for (auto& se : d.extensions) {
        detail::finalize_packed(se.field, d.syn);
    }
    // Order-independent extension-range check: every regular field of a message
    // must lie outside its own `extensions` ranges (protoc rejects a collision
    // regardless of whether the range is declared before or after the field).
    for (const schema_message& m : d.messages) {
        for (const schema_field& f : m.fields) {
            for (const auto& rng : m.extension_ranges) {
                if (f.number >= rng.first && f.number <= rng.second) {
                    err.code = error_code::proto_bad_field;
                    err.offset = f.type_offset;
                    r.err = err;
                    r.err.src = src.data();
                    r.err.src_len = src.size();
                    return r;
                }
            }
        }
    }
    r.value = std::move(d);
    return r;
}

// ---------------------------------------------------------------------------
// Public: multi-file dependency merging
// ---------------------------------------------------------------------------

// Merge `src` into `dst` for a multi-file build: messages are appended when
// their full name is not already present, enum tables deduplicated the same
// way, and import paths unioned. After merging, compile_schema sees types from
// every file. Nothing is loaded or resolved here -- merging is a pure data
// operation (import file paths are the caller's dependency table).
void merge_schemas(schema_desc& dst, const schema_desc& src) {
    for (const std::string& f : src.imports) {
        if (std::find(dst.imports.begin(), dst.imports.end(), f) ==
            dst.imports.end()) {
            dst.imports.push_back(f);
        }
    }
    for (const schema_enum& e : src.enum_defs) {
        bool dup = false;
        for (const schema_enum& de : dst.enum_defs) {
            if (de.full_name == e.full_name) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            dst.enum_defs.push_back(e);
            dst.enums.push_back(e.full_name);
        }
    }
    for (const schema_message& m : src.messages) {
        bool dup = false;
        for (const schema_message& dm : dst.messages) {
            if (dm.full_name == m.full_name) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            dst.messages.push_back(m);
        }
    }
}

// ---------------------------------------------------------------------------
// Public: compile a schema_desc into a codec-ready schema_info
// ---------------------------------------------------------------------------

// Register the `extend` blocks recorded in a resolved descriptor into the
// process-wide ExtensionRegistry (proto.hpp) under the global semantics: the
// lookup keys are the field number (decode) and the field name (encode). A
// conflict (same number or same name with a different layout) yields
// error_code::proto_bad_field; re-registering identical descriptors is a
// no-op. Every extension is validated against its extendee first (protoc
// semantics): the extendee must resolve to a message in the descriptor and the
// extension field number must fall inside one of that message's declared
// `extensions` ranges, otherwise error_code::proto_bad_field and nothing is
// registered. Returns error_code::ok on success.
error_code register_schema_extensions(const schema_desc& d) {
    // Extendee name resolution maps (simple + fully-qualified + dotted-suffix),
    // mirroring the regular type-reference resolver.
    std::unordered_map<std::string, std::size_t> full;
    std::unordered_map<std::string, std::size_t> simple;
    for (std::size_t i = 0; i < d.messages.size(); ++i) {
        full.emplace(d.messages[i].full_name, i);
        if (simple.find(d.messages[i].name) == simple.end()) {
            simple.emplace(d.messages[i].name, i);
        }
    }
    for (const schema_extension& se : d.extensions) {
        std::string norm = se.extendee;
        if (!norm.empty() && norm.front() == '.') {
            norm.erase(0, 1);
        }
        std::size_t target = static_cast<std::size_t>(-1);
        // Canonical (parse-resolved) extendees match exactly by full name;
        // raw leftovers (unresolved at parse, e.g. cross-file refs) use the
        // same scope-aware / component-reduced resolution as parse time.
        if (!detail::resolve_extendee(d, full, simple, std::string(), norm,
                                      target)) {
            // protoc: "<extendee> is not defined."
            return error_code::proto_bad_field;
        }
        const schema_message& extendee = d.messages[target];
        bool in_range = false;
        for (const auto& rng : extendee.extension_ranges) {
            if (se.field.number >= rng.first &&
                se.field.number <= rng.second) {
                in_range = true;
                break;
            }
        }
        if (!in_range) {
            // protoc: "<extendee> does not declare <number> as an extension
            // number." (covers missing extension ranges as well).
            return error_code::proto_bad_field;
        }
        const schema_field& f = se.field;
        extension_desc ed;
        ed.number = f.number;
        ed.name = f.name;
        ed.wire = wire_of_scalar(f.type);
        ed.mode = f.mode;
        ed.packed = f.packed;
        ed.zigzag = (f.type == scalar_type::sint32 ||
                     f.type == scalar_type::sint64);
        ed.is_message = f.is_message;
        ed.is_enum = f.is_enum;
        ed.as_bool = (f.type == scalar_type::bool_val);
        ed.as_float = (f.type == scalar_type::float_val);
        ed.as_double = (f.type == scalar_type::double_val);
        ed.extendee = se.extendee;
        if (f.has_default) {
            ed.has_default = true;
            ed.dflt_int = f.dflt_int;
            ed.dflt_real = f.dflt_real;
            ed.dflt_str = f.dflt_str;
        }
        const error_code e = extension_registry::global().add(ed);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

// ---------------------------------------------------------------------------
// compiled_schema members (declared in the header)
// ---------------------------------------------------------------------------

compiled_schema::compiled_schema() = default;
compiled_schema::compiled_schema(compiled_schema&&) noexcept = default;
compiled_schema& compiled_schema::operator=(compiled_schema&&) noexcept = default;
compiled_schema::~compiled_schema() = default;

const schema_info* compiled_schema::info() const noexcept {
        if (messages_.empty() || root_ >= messages_.size()) {
            return nullptr;
        }
        return &messages_[root_]->si;
    }

const schema_info* compiled_schema::find_message(rtti::string_view name) const noexcept {
        for (const auto& m : messages_) {
            if (m != nullptr && m->name == name) {
                return &m->si;
            }
        }
        return nullptr;
    }

syntax compiled_schema::syn() const noexcept { return syn_; }

std::size_t compiled_schema::size() const noexcept { return messages_.size(); }


bool compiled_schema::build(const schema_desc& desc, const compile_config& cfg,
                            compiled_schema& out, error_info& err) {
        if (desc.messages.empty()) {
            err.code = error_code::invalid_root;
            return false;
        }
        std::size_t root = cfg.root;
        if (!cfg.root_name.empty()) {
            bool found = false;
            for (std::size_t i = 0; i < desc.messages.size(); ++i) {
                if (desc.messages[i].full_name == cfg.root_name ||
                    desc.messages[i].name == cfg.root_name) {
                    root = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                err.code = error_code::no_such_field;
                return false;
            }
        }
        if (root >= desc.messages.size()) {
            err.code = error_code::invalid_root;
            return false;
        }
        out.syn_ = desc.syn;
        out.root_ = root;
        out.messages_.clear();
        out.messages_.reserve(desc.messages.size());
        for (std::size_t i = 0; i < desc.messages.size(); ++i) {
            out.messages_.push_back(
                std::unique_ptr<detail::compiled_message>(
                    new detail::compiled_message()));
            out.messages_.back()->name = desc.messages[i].full_name;
        }
        for (std::size_t i = 0; i < desc.messages.size(); ++i) {
            detail::compiled_message& rm = *out.messages_[i];
            const schema_message& sm = desc.messages[i];
            rm.fields.reserve(sm.fields.size());
            rm.field_ptrs.reserve(sm.fields.size());
            for (const schema_field& f : sm.fields) {
                // An opaque external message (an unresolved type from an
                // imported file) keeps type_index == -1 and no child schema.
                if (f.is_message &&
                    f.type_index != static_cast<std::size_t>(-1) &&
                    f.type_index >= desc.messages.size()) {
                    err.code = error_code::unexpected_token;
                    return false;
                }
                std::unique_ptr<detail::dynamic_field> df(
                    new detail::dynamic_field(f));
                if (f.is_message &&
                    f.type_index != static_cast<std::size_t>(-1)) {
                    df->child = out.messages_[f.type_index].get();
                }
                rm.field_ptrs.push_back(df.get());
                rm.fields.push_back(std::move(df));
            }
            rm.si.syn = desc.syn;
            rm.si.count = rm.field_ptrs.size();
            rm.si.fields =
                rm.field_ptrs.empty() ? nullptr : rm.field_ptrs.data();
        }
        return true;
    }

result<compiled_schema> compile_schema(
    const schema_desc& desc, const compile_config& cfg) {
    result<compiled_schema> r;
    error_info err;
    err.src = nullptr;
    err.src_len = 0;
    // Re-run type resolution on a copy: after multi-file merge_schemas a
    // reference may have been opaque (unresolved at its own parse time) and
    // now binds to a message introduced by another file; it also normalizes
    // enum defaults. Idempotent for already-resolved descriptors.
    schema_desc resolved = desc;
    error_info rerr;
    rerr.src = nullptr;
    rerr.src_len = 0;
    if (!detail::resolve_types(resolved, rerr)) {
        r.err = rerr;
        return r;
    }
    if (!compiled_schema::build(resolved, cfg, r.value, err)) {
        r.err = err;
    }
    // Compiling a descriptor with `extend` blocks registers the extensions in
    // the process-wide ExtensionRegistry, so wire decode of the extended
    // message(s) recognizes them as named fields (re-registering an identical
    // extension is an idempotent no-op; a conflicting duplicate fails).
    if (r.ok()) {
        const error_code ec = register_schema_extensions(resolved);
        if (ec != error_code::ok) {
            r.err.code = ec;
            r.err.src = err.src;
            r.err.src_len = err.src_len;
            r.err.offset = 0;
        }
    }
    return r;
}

// Parse-then-compile convenience overload.
result<compiled_schema> compile_schema(
    rtti::string_view src, const compile_config& cfg) {
    const result<schema_desc> p = parse_schema(src);
    if (!p.ok()) {
        result<compiled_schema> r;
        r.err = p.err;
        return r;
    }
    return compile_schema(p.value, cfg);
}

} // namespace proto
} // namespace rtti
