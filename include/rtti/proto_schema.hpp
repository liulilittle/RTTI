#pragma once
// .proto text-descriptor parser + runtime schema bridge for the protobuf
// wire-format adapter (spec section 13.2).
//
// proto.hpp builds a proto schema from explicit C++ schema<T> registrations.
// This header adds the missing text side: parsing a .proto file
// (messages/nested messages/fields/labels/scalars/comments) into a plain
// schema_desc, then compiling it into a runtime schema_info that drives the
// SAME wire_to_tree / tree_to_wire / parse codec chain as a schema<T>. So a
// .proto descriptor and a C++ registration both produce equivalent wire
// behaviour.
//
// Supported syntax subset (proto2 and proto3):
//   * `syntax = "proto2" | "proto3";` (absent -> proto2, like protoc).
//   * `message Name { ... }` and nested messages (dotted fully-qualified
//     names, e.g. Outer.Inner). Forward references are allowed.
//   * Fields: `[label] type name = number [options] ;` where label is
//     optional/required/repeated; `required` is rejected inside proto3.
//   * Scalar types int32/int64/uint32/uint64/sint32/sint64/bool/float/double/
//     string/bytes/fixed32/fixed64/sfixed32/sfixed64.
//   * Message and enum type references (dotted names allowed; a leading '.'
//     is absolute). Enum-typed fields take int32/varint wire semantics.
//   * Field options `[packed = true|false]` and `[default = <value>]`
//     (integer, real, string, true/false, inf/nan). Unknown options are
//     skipped. Other option forms ([(custom.option)], etc.) are skipped as
//     balanced groups.
//   * `//` line and `/* */` block comments.
//   * Top-level `import` / `package` / `option` statements are consumed and
//     ignored.
//
// Wire derivation follows the protoc rules: repeated numeric fields are
// packed by default under proto3 and unpacked under proto2 unless
// `[packed = ...]` says otherwise; string/bytes/message are never packed.
//
// Safety (spec section 0): malformed, truncated, absurdly deep or absurdly
// long input returns an error_code (offset + line/column via error_info) and
// never crashes. Message nesting in the descriptor is capped, field numbers
// must be 1..(2^29-1), and integer literals overflow-check.
//
// English comments only; C++17; zero third-party dependencies; header-only;
// gated by RTTI_ENABLE_PROTO exactly like <rtti/proto.hpp>.

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

#include <rtti/proto.hpp>

namespace rtti {
namespace proto {

// ---------------------------------------------------------------------------
// Parsed text schema (plain data, no codec binding)
// ---------------------------------------------------------------------------

// The 15 protobuf scalar kinds plus the reference kinds. message_val/enum_val
// are produced by the type-resolution pass for references.
enum class scalar_type : std::uint8_t {
    int32,
    int64,
    uint32,
    uint64,
    sint32,
    sint64,
    bool_val,
    float_val,
    double_val,
    string_val,
    bytes_val,
    fixed32,
    fixed64,
    sfixed32,
    sfixed64,
    message_val,
    enum_val
};

// Natural wire type for a scalar kind per the protobuf spec. message refs use
// length (encoded as a nested message); enum refs use varint.
inline wire_type wire_of_scalar(scalar_type t) noexcept {
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

// One parsed field. `type` holds the resolved scalar kind; `type_ref` +
// `type_index` describe a message/enum reference (message_val/enum_val).
struct schema_field {
    std::string name;
    std::uint32_t number = 0;
    field_mode mode = field_mode::optional;
    bool packed = false;
    bool packed_explicit = false;

    scalar_type type = scalar_type::int32;
    std::string type_ref;
    bool is_message = false;
    bool is_enum = false;
    std::size_t type_index = static_cast<std::size_t>(-1); // message index
    std::size_t type_offset = 0; // byte offset of the type token

    bool has_default = false;
    std::int64_t dflt_int = 0;
    double dflt_real = 0.0;
    std::string dflt_str;
};

struct schema_message {
    std::string name;      // last path component
    std::string full_name; // dotted, e.g. "Outer.Inner"
    std::vector<schema_field> fields;
};

struct schema_desc {
    syntax syn = syntax::proto2;
    std::vector<schema_message> messages; // top-level first, then nested
    std::vector<std::string> enums;       // dotted enum full names
};

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
        minus
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
                        switch (esc) {
                            case 'n':
                                tmp.push_back('\n');
                                break;
                            case 'r':
                                tmp.push_back('\r');
                                break;
                            case 't':
                                tmp.push_back('\t');
                                break;
                            case '0':
                                tmp.push_back('\0');
                                break;
                            case '\\':
                                tmp.push_back('\\');
                                break;
                            case '"':
                                tmp.push_back('"');
                                break;
                            case '\'':
                                tmp.push_back('\'');
                                break;
                            default:
                                tmp.push_back(esc);
                                break;
                        }
                        p += 2;
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
    bool parse_field(schema_message& sm);
    bool parse_type_ref(schema_field& f);
    bool parse_options(schema_field& f);
    bool parse_option_value(schema_field& f, const std::string& name,
                            const char* pos);
    bool parse_enum(schema_desc& d, std::vector<std::string>& stack);
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
        if (have_ident("import") || have_ident("package") ||
            have_ident("option") || have_ident("syntax")) {
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
        if (!parse_field(sm)) {
            stack.pop_back();
            return false;
        }
    }
    stack.pop_back();
    d.messages.push_back(std::move(sm));
    return true;
}

inline bool parser::parse_field(schema_message& sm) {
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
    sm.fields.push_back(std::move(f));
    return true;
}

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
        }
        if (st != scalar_type::message_val) {
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
            if (neg) {
                const std::int64_t nv =
                    (v == 0x8000000000000000ull)
                        ? (std::numeric_limits<std::int64_t>::min)()
                        : -static_cast<std::int64_t>(v);
                apply_default_int(f, nv);
            } else {
                if (v > 0x7FFFFFFFFFFFFFFFull) {
                    set_error(error_code::invalid_number, pos);
                    return false;
                }
                apply_default_int(f, static_cast<std::int64_t>(v));
            }
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
            }
            // other identifiers are enum value names; ignored.
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
    d.enums.push_back(std::move(full));
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
                return true;
            }
            --depth;
            adv();
            continue;
        }
        adv();
    }
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
    const std::string& ref, std::size_t& out) {
    const auto it = full.find(ref);
    if (it != full.end()) {
        out = it->second;
        return true;
    }
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

inline bool resolve_enum_ref(const schema_desc& d, const std::string& ref) {
    const std::string dotref = "." + ref;
    for (const auto& e : d.enums) {
        if (e == ref || nends_with(e, dotref)) {
            return true;
        }
    }
    return false;
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
            if (!f.is_message || f.type_ref.empty()) {
                continue;
            }
            std::string norm = f.type_ref;
            if (!norm.empty() && norm.front() == '.') {
                norm.erase(0, 1);
            }
            std::size_t target = 0;
            if (resolve_message_ref(d, full, simple, norm, target)) {
                f.type = scalar_type::message_val;
                f.type_index = target;
                f.is_enum = false;
                continue;
            }
            if (resolve_enum_ref(d, norm)) {
                f.type = scalar_type::enum_val;
                f.type_index = static_cast<std::size_t>(-1);
                f.is_message = false;
                f.is_enum = true;
                continue;
            }
            err.code = error_code::unexpected_token;
            err.offset = f.type_offset;
            return false;
        }
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

} // namespace detail

// ---------------------------------------------------------------------------
// Public: parse .proto text into a schema_desc
// ---------------------------------------------------------------------------

// Parse a .proto descriptor. Returns error_code (with err.offset / line() /
// column()) instead of throwing ever. Empty input parses to an empty schema.
inline result<schema_desc> parse_schema(rtti::string_view src) {
    result<schema_desc> r;
    schema_desc d;
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
    r.value = std::move(d);
    return r;
}
// ---------------------------------------------------------------------------
// Public: compile a schema_desc into a codec-ready schema_info
// ---------------------------------------------------------------------------

namespace detail {

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
        st = f.type;
        is_message = f.is_message;
        is_enum = f.is_enum;
        type_ref = f.type_ref;
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
            if (node->type() != rtti::value_io::kind::object) {
                return error_code::kind_mismatch;
            }
            if (child == nullptr) {
                return error_code::unexpected_token;
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
                return error_code::unexpected_token;
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

struct compile_config {
    std::size_t root = 0; // index into schema_desc::messages
    std::string root_name; // alternative: full name or last component
};

// Owned runtime schema: mutable-only, outlives every use. `info()` yields the
// root message's schema_info for wire_to_tree / tree_to_wire / parse.
class compiled_schema {
public:
    compiled_schema() = default;
    compiled_schema(compiled_schema&&) noexcept = default;
    compiled_schema& operator=(compiled_schema&&) noexcept = default;
    compiled_schema(const compiled_schema&) = delete;
    compiled_schema& operator=(const compiled_schema&) = delete;
    ~compiled_schema() = default;

    const schema_info* info() const noexcept {
        if (messages_.empty() || root_ >= messages_.size()) {
            return nullptr;
        }
        return &messages_[root_]->si;
    }
    const schema_info* find_message(rtti::string_view name) const noexcept {
        for (const auto& m : messages_) {
            if (m != nullptr && m->name == name) {
                return &m->si;
            }
        }
        return nullptr;
    }
    syntax syn() const noexcept { return syn_; }
    std::size_t size() const noexcept { return messages_.size(); }

    // Build from a parsed (or hand-made) schema_desc. root_name wins over root.
    static bool build(const schema_desc& desc, const compile_config& cfg,
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
                if (f.is_message &&
                    f.type_index >= desc.messages.size()) {
                    err.code = error_code::unexpected_token;
                    return false;
                }
                std::unique_ptr<detail::dynamic_field> df(
                    new detail::dynamic_field(f));
                if (f.is_message) {
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

private:
    syntax syn_ = syntax::proto3;
    std::size_t root_ = 0;
    std::vector<std::unique_ptr<detail::compiled_message>> messages_;
};

// Compile a parsed descriptor into a codec-ready schema. Errors surface as an
// invalid_root / no_such_field result, never a crash.
inline result<compiled_schema> compile_schema(
    const schema_desc& desc, const compile_config& cfg = compile_config{}) {
    result<compiled_schema> r;
    error_info err;
    err.src = nullptr;
    err.src_len = 0;
    if (!compiled_schema::build(desc, cfg, r.value, err)) {
        r.err = err;
    }
    return r;
}

// Parse-then-compile convenience overload.
inline result<compiled_schema> compile_schema(
    rtti::string_view src, const compile_config& cfg = compile_config{}) {
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