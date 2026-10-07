#include <rtti/flat_schema.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace rtti {
namespace flat {

namespace detail {

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

inline bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
inline bool is_name(char c) noexcept {
    return is_alpha(c) || is_digit(c);
}
inline bool is_hex(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
inline unsigned hex_val(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(c - 'a' + 10);
    }
    return static_cast<unsigned>(c - 'A' + 10);
}

// ---------------------------------------------------------------------------
// Token scanner
// ---------------------------------------------------------------------------

struct fbs_token {
    enum class kind : std::uint8_t {
        eof,
        ident,
        number, // integer (ival) or real (dval, is_real); hex integers in ival
        str,    // string literal (sval = unescaped payload)
        lbrace,
        rbrace,
        lbracket,
        rbracket,
        lparen,
        rparen,
        colon,
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
    std::string sval;
    const char* begin = nullptr;
};

struct fbs_scanner {
    const char* p;
    const char* e;
    error_code fail = error_code::ok;
    const char* fail_pos = nullptr;

    explicit fbs_scanner(rtti::string_view s) noexcept
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

    fbs_token next() {
        fbs_token t;
        skip_trivia();
        if (fail != error_code::ok) {
            t.k = fbs_token::kind::eof;
            return t;
        }
        t.begin = p;
        if (p == e) {
            t.k = fbs_token::kind::eof;
            return t;
        }
        const char c = *p;
        if (is_alpha(c)) {
            const char* s = p;
            while (p != e && is_name(*p)) {
                ++p;
            }
            t.k = fbs_token::kind::ident;
            t.text = rtti::string_view(s, static_cast<std::size_t>(p - s));
            return t;
        }
        if (is_digit(c)) {
            std::uint64_t v = 0;
            bool overflow = false;
            const bool hex = c == '0' && p + 1 < e && (p[1] == 'x' || p[1] == 'X');
            if (hex) {
                p += 2;
                const char* const start = p;
                while (p != e && is_hex(*p)) {
                    const unsigned d = hex_val(*p);
                    if (v > ((std::numeric_limits<std::uint64_t>::max)() -
                             d) /
                                16) {
                        overflow = true;
                    } else {
                        v = v * 16 + d;
                    }
                    ++p;
                }
                if (p == start) {
                    fail = error_code::unexpected_token;
                    fail_pos = t.begin;
                    t.k = fbs_token::kind::eof;
                    return t;
                }
            } else {
                while (p != e && is_digit(*p)) {
                    const std::uint64_t digit =
                        static_cast<std::uint64_t>(*p - '0');
                    if (v > ((std::numeric_limits<std::uint64_t>::max)() -
                             digit) /
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
                    t.dval = std::strtod(num.c_str(), nullptr);
                    t.is_real = true;
                    t.k = fbs_token::kind::number;
                    return t;
                }
                if (p != e && (*p == 'e' || *p == 'E')) {
                    const char* save = p;
                    const std::string num(t.begin, p);
                    ++p;
                    if (p != e && (*p == '+' || *p == '-')) {
                        ++p;
                    }
                    bool any = false;
                    while (p != e && is_digit(*p)) {
                        ++p;
                        any = true;
                    }
                    if (any) {
                        const std::string en(t.begin, p);
                        t.dval = std::strtod(en.c_str(), nullptr);
                        t.is_real = true;
                        t.k = fbs_token::kind::number;
                        return t;
                    }
                    p = save; // not an exponent; fall through as integer
                }
            }
            if (overflow) {
                fail = error_code::invalid_number;
                fail_pos = t.begin;
                t.k = fbs_token::kind::eof;
                return t;
            }
            t.ival = v;
            t.k = fbs_token::kind::number;
            return t;
        }
        switch (c) {
            case '{':
                t.k = fbs_token::kind::lbrace;
                ++p;
                break;
            case '}':
                t.k = fbs_token::kind::rbrace;
                ++p;
                break;
            case '[':
                t.k = fbs_token::kind::lbracket;
                ++p;
                break;
            case ']':
                t.k = fbs_token::kind::rbracket;
                ++p;
                break;
            case '(':
                t.k = fbs_token::kind::lparen;
                ++p;
                break;
            case ')':
                t.k = fbs_token::kind::rparen;
                ++p;
                break;
            case ':':
                t.k = fbs_token::kind::colon;
                ++p;
                break;
            case '=':
                t.k = fbs_token::kind::eq;
                ++p;
                break;
            case ';':
                t.k = fbs_token::kind::semi;
                ++p;
                break;
            case '.':
                t.k = fbs_token::kind::dot;
                ++p;
                break;
            case ',':
                t.k = fbs_token::kind::comma;
                ++p;
                break;
            case '-':
                t.k = fbs_token::kind::minus;
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
                            case 'a':
                                tmp.push_back('\a');
                                p += 2;
                                break;
                            case 'b':
                                tmp.push_back('\b');
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
                            case '0':
                                tmp.push_back('\0');
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
                            case '/':
                                tmp.push_back('/');
                                p += 2;
                                break;
                            case 'u': {
                                // \uXXXX -> UTF-8 (up to 4 hex digits).
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
                            default:
                                tmp.push_back(esc);
                                p += 2;
                                break;
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
                t.k = fbs_token::kind::str;
                t.sval = std::move(tmp);
                break;
            }
            default:
                fail = error_code::unexpected_token;
                fail_pos = t.begin;
                ++p;
                t.k = fbs_token::kind::eof;
                break;
        }
        return t;
    }
};
// ---------------------------------------------------------------------------
// Recursive-descent parser
// ---------------------------------------------------------------------------

inline bool scalar_kind_from_name(const std::string& name, fkind& out) noexcept;

struct fbs_parser {
    fbs_scanner sc;
    fbs_token cur;
    fbs_token look;
    error_info err;
    const char* src_begin = nullptr;
    std::string ns_; // current namespace ("" when none)
    int nesting = 0;
    static constexpr int kMaxNesting = 128;

    explicit fbs_parser(rtti::string_view s)
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
        return cur.k == fbs_token::kind::ident &&
               cur.text == rtti::string_view(kw);
    }

bool expect_kind(fbs_token::kind k) {
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

    // ---- top level ----

    bool parse_file(fbs_schema_desc& d);
    bool parse_block(fbs_schema_desc& d, bool stct);
    bool parse_field(fbs_schema_desc& d, fbs_agg_desc& agg, bool stct);
    bool parse_default(fbs_field_desc& f);
    bool assign_scalar_default(fbs_field_desc& f, bool neg);
    bool parse_attrs(fbs_field_desc& f);
    bool parse_enum(fbs_schema_desc& d);
    bool parse_union(fbs_schema_desc& d);
    bool skip_statement();
};

bool fbs_parser::parse_file(fbs_schema_desc& d) {
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == fbs_token::kind::eof) {
            break;
        }
        if (have_ident("namespace")) {
            adv();
            std::string name;
            if (cur.k != fbs_token::kind::ident) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            name = std::string(cur.text);
            adv();
            while (cur.k == fbs_token::kind::dot) {
                adv();
                if (cur.k != fbs_token::kind::ident) {
                    set_error(error_code::unexpected_token, cur.begin);
                    return false;
                }
                name.push_back('.');
                name += std::string(cur.text);
                adv();
            }
            if (!expect_kind(fbs_token::kind::semi)) {
                return false;
            }
            d.ns = name;
            ns_ = name;
            continue;
        }
        if (have_ident("table") || have_ident("struct")) {
            const bool stct = have_ident("struct");
            if (!parse_block(d, stct)) {
                return false;
            }
            continue;
        }
        if (have_ident("enum")) {
            if (!parse_enum(d)) {
                return false;
            }
            continue;
        }
        if (have_ident("union")) {
            if (!parse_union(d)) {
                return false;
            }
            continue;
        }
if (have_ident("root_type")) {
            adv();
            if (cur.k != fbs_token::kind::ident) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            std::string rt(std::string(cur.text));
            adv();
            while (cur.k == fbs_token::kind::dot) {
                adv();
                if (cur.k != fbs_token::kind::ident) {
                    set_error(error_code::unexpected_token, cur.begin);
                    return false;
                }
                rt.push_back('.');
                rt += std::string(cur.text);
                adv();
            }
            d.root_type = rt;
            if (!expect_kind(fbs_token::kind::semi)) {
                return false;
            }
            continue;
        }
        if (have_ident("file_identifier")) {
            adv();
            if (cur.k != fbs_token::kind::str) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            if (cur.sval.size() != 4) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            d.file_identifier = cur.sval;
            adv();
            if (!expect_kind(fbs_token::kind::semi)) {
                return false;
            }
            continue;
        }
        if (have_ident("include")) {
            adv();
            if (cur.k != fbs_token::kind::str) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            d.includes.push_back(cur.sval);
            adv();
            if (!expect_kind(fbs_token::kind::semi)) {
                return false;
            }
            continue;
        }
        if (have_ident("attribute") || have_ident("rpc_service") ||
            have_ident("object")) {
            if (!skip_statement()) {
                return false;
            }
            continue;
        }
        if (cur.k == fbs_token::kind::semi) {
            adv();
            continue;
        }
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    return true;
}

bool fbs_parser::parse_block(fbs_schema_desc& d, bool stct) {
    if (failed()) {
        return false;
    }
    if (nesting >= kMaxNesting) {
        set_error(error_code::too_deep, cur.begin);
        return false;
    }
    ++nesting;
    adv(); // 'table' / 'struct'
    if (cur.k != fbs_token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        --nesting;
        return false;
    }
    fbs_agg_desc agg;
    agg.name = std::string(cur.text);
    agg.full_name = ns_.empty() ? agg.name : ns_ + "." + agg.name;
    agg.stct = stct;
    // Duplicate top-level type names in the same scope are rejected.
    for (const fbs_agg_desc& t : d.types) {
        if (t.full_name == agg.full_name) {
            set_error(error_code::unexpected_token, cur.begin);
            --nesting;
            return false;
        }
    }
    adv();
    if (!expect_kind(fbs_token::kind::lbrace)) {
        --nesting;
        return false;
    }
    for (;;) {
        if (failed()) {
            --nesting;
            return false;
        }
        if (cur.k == fbs_token::kind::rbrace) {
            adv();
            break;
        }
        if (cur.k == fbs_token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            --nesting;
            return false;
        }
        if (cur.k == fbs_token::kind::semi) {
            adv();
            continue;
        }
        if (!parse_field(d, agg, stct)) {
            --nesting;
            return false;
        }
    }
    --nesting;
    // A trailing ';' after '}' is tolerated.
    if (cur.k == fbs_token::kind::semi) {
        adv();
    }
    d.types.push_back(std::move(agg));
    return true;
}

bool fbs_parser::parse_field(fbs_schema_desc& d, fbs_agg_desc& agg,
                             bool stct) {
    (void)d;
    if (failed()) {
        return false;
    }
    if (cur.k != fbs_token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    const std::string fname(cur.text);
    for (const fbs_field_desc& f : agg.fields) {
        if (f.name == fname) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
    }
    adv();
    if (!expect_kind(fbs_token::kind::colon)) {
        return false;
    }

    fbs_field_desc f;
    f.name = fname;
    if (cur.k == fbs_token::kind::lbracket) {
        // vector type [T]
        if (stct) {
            set_error(error_code::proto_bad_field, cur.begin);
            return false;
        }
adv(); // '['
        if (cur.k != fbs_token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        f.type = fbs_type::vector;
        std::string ename(std::string(cur.text));
        f.type_offset = sync_offset(cur.begin);
        adv();
        bool edotted = false;
        while (cur.k == fbs_token::kind::dot) {
            edotted = true;
            adv();
            if (cur.k != fbs_token::kind::ident) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            ename.push_back('.');
            ename += std::string(cur.text);
            adv();
        }
        if (!edotted && ename == "string") {
            f.elem = fkind::string;
        } else if (!edotted && scalar_kind_from_name(ename, f.elem)) {
            // scalar element
        } else {
            f.elem = fkind::table; // reference element (incl. enum); resolved
            f.ref = ename;
        }
        if (!expect_kind(fbs_token::kind::rbracket)) {
            return false;
        }
    } else if (cur.k == fbs_token::kind::ident) {
        std::string tn(std::string(cur.text));
        f.type_offset = sync_offset(cur.begin);
        adv();
        bool dotted = false;
        while (cur.k == fbs_token::kind::dot) {
            dotted = true;
            adv();
            if (cur.k != fbs_token::kind::ident) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            tn.push_back('.');
            tn += std::string(cur.text);
            adv();
        }
        if (!dotted && tn == "string") {
            f.type = fbs_type::string;
            if (stct) {
                set_error(error_code::proto_bad_field, cur.begin);
                return false;
            }
        } else if (!dotted && scalar_kind_from_name(tn, f.kind)) {
            f.type = fbs_type::scalar;
        } else {
            f.type = fbs_type::table; // placeholder ref (dotted or not)
            f.ref = tn;
        }
    } else {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }

    // struct fields: scalars and struct references only.
    if (stct &&
        (f.type == fbs_type::vector || f.type == fbs_type::string)) {
        set_error(error_code::proto_bad_field, cur.begin);
        return false;
    }

    if (cur.k == fbs_token::kind::eq) {
        if (stct) {
            set_error(error_code::proto_bad_field, cur.begin);
            return false;
        }
        adv();
        if (!parse_default(f)) {
            return false;
        }
    }
    if (cur.k == fbs_token::kind::lparen) {
        if (stct) {
            set_error(error_code::proto_bad_field, cur.begin);
            return false;
        }
        if (!parse_attrs(f)) {
            return false;
        }
    }
    if (!expect_kind(fbs_token::kind::semi)) {
        return false;
    }
    // required is only meaningful on non-scalar table fields (flatc rule).
    if (f.required && f.type == fbs_type::scalar) {
        set_error(error_code::proto_bad_field, cur.begin);
        return false;
    }
    agg.fields.push_back(std::move(f));
    return true;
}
// ---------------------------------------------------------------------------
// Type-name and default helpers
// ---------------------------------------------------------------------------

inline bool scalar_kind_from_name(const std::string& name,
                                  fkind& out) noexcept {
    if (name == "bool") {
        out = fkind::bool_;
    } else if (name == "byte") {
        out = fkind::i8;
    } else if (name == "ubyte") {
        out = fkind::u8;
    } else if (name == "short") {
        out = fkind::i16;
    } else if (name == "ushort") {
        out = fkind::u16;
    } else if (name == "int") {
        out = fkind::i32;
    } else if (name == "uint") {
        out = fkind::u32;
    } else if (name == "long") {
        out = fkind::i64;
    } else if (name == "ulong") {
        out = fkind::u64;
    } else if (name == "float") {
        out = fkind::f32;
    } else if (name == "double") {
        out = fkind::f64;
    } else {
        return false;
    }
    return true;
}

// Is `v` representable as the signed/unsigned scalar `k`? bool accepts any.
inline bool fits_integer(fkind k, std::int64_t v) noexcept {
    switch (k) {
        case fkind::bool_:
            return true;
        case fkind::i8:
            return v >= -128 && v <= 127;
        case fkind::u8:
            return v >= 0 && v <= 255;
        case fkind::i16:
            return v >= -32768 && v <= 32767;
        case fkind::u16:
            return v >= 0 && v <= 65535;
        case fkind::i32:
            return v >= (std::numeric_limits<std::int32_t>::min)() &&
                   v <= (std::numeric_limits<std::int32_t>::max)();
        case fkind::u32:
            return v >= 0 &&
                   static_cast<std::uint64_t>(v) <=
                       (std::numeric_limits<std::uint32_t>::max)();
        case fkind::i64:
            return true;
        case fkind::u64:
            return v >= 0; // values > INT64_MAX were clamped on parse
        default:
            return false;
    }
}

bool fbs_parser::parse_attrs(fbs_field_desc& f) {
    if (failed()) {
        return false;
    }
    if (!expect_kind(fbs_token::kind::lparen)) {
        return false;
    }
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == fbs_token::kind::rparen) {
            adv();
            return true;
        }
        if (cur.k == fbs_token::kind::comma) {
            adv();
            continue;
        }
        if (cur.k != fbs_token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
const std::string attr(cur.text);
        const char* const pos = cur.begin;
        adv();
        if (cur.k == fbs_token::kind::eq ||
            cur.k == fbs_token::kind::colon) {
            // flatc writes attribute values as `(id: 5)`; '=' is also accepted.
            adv();
            if (attr == "id") {
                bool neg = false;
                if (cur.k == fbs_token::kind::minus) {
                    neg = true;
                    adv();
                }
                if (cur.k != fbs_token::kind::number || cur.is_real) {
                    set_error(error_code::unexpected_token, cur.begin);
                    return false;
                }
                if (neg || cur.ival > 0xFFFFull) {
                    set_error(error_code::proto_bad_field, pos);
                    return false;
                }
                f.has_id = true;
                f.id = static_cast<std::uint16_t>(cur.ival);
                adv();
            } else {
                // Skip any single-token attribute value.
                if (cur.k == fbs_token::kind::minus) {
                    adv();
                }
                if (cur.k == fbs_token::kind::number ||
                    cur.k == fbs_token::kind::ident ||
                    cur.k == fbs_token::kind::str) {
                    adv();
                } else {
                    set_error(error_code::unexpected_token, cur.begin);
                    return false;
                }
            }
            continue;
        }
        if (attr == "deprecated") {
            f.deprecated = true;
        } else if (attr == "required") {
            f.required = true;
        }
        // Unknown bare attributes are skipped.
    }
}

bool fbs_parser::assign_scalar_default(fbs_field_desc& f, bool neg) {
    const fkind k = f.kind;
    if (cur.k == fbs_token::kind::ident) {
        const std::string id(cur.text);
        if (k == fkind::bool_) {
            if (id == "true" || id == "false") {
                f.has_default = true;
                f.dflt_int = (id == "true") ? 1 : 0;
                adv();
                if (neg) {
                    set_error(error_code::overflow, cur.begin);
                    return false;
                }
                return true;
            }
            f.dflt_enum_name = id; // resolve() reports the mismatch
            return true;
        }
        if (k == fkind::f32 || k == fkind::f64) {
            if (id == "inf") {
                f.has_default = true;
                f.dflt_real = neg ? -std::numeric_limits<double>::infinity()
                                  : std::numeric_limits<double>::infinity();
                adv();
                return true;
            }
            if (id == "nan") {
                f.has_default = true;
                // flatc accepts a leading '-' before nan as well.
                f.dflt_real = std::numeric_limits<double>::quiet_NaN();
                adv();
                return true;
            }
        }
        f.dflt_enum_name = id; // resolve() reports the mismatch
        return true;
    }
    if (cur.k != fbs_token::kind::number) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    if (cur.is_real) {
        if (k != fkind::f32 && k != fkind::f64) {
            set_error(error_code::proto_type_mismatch, cur.begin);
            return false;
        }
        double d = cur.dval;
        if (neg) {
            d = -d;
        }
        f.has_default = true;
        f.dflt_real = d;
        adv();
        return true;
    }
    // Integer literal (or bool accepting any int like flatc).
    const std::uint64_t uv = cur.ival;
    adv();
    std::int64_t v = 0;
    bool overflow = false;
    if (neg) {
        if (uv == 0x8000000000000000ull) {
            v = (std::numeric_limits<std::int64_t>::min)();
        } else if (uv > 0x8000000000000000ull) {
            overflow = true;
        } else {
            v = -static_cast<std::int64_t>(uv);
        }
    } else {
        if (uv > 0x7FFFFFFFFFFFFFFFull) {
            if (k == fkind::u64) {
                v = (std::numeric_limits<std::int64_t>::max)(); // clamp
            } else {
                overflow = true;
            }
        } else {
            v = static_cast<std::int64_t>(uv);
        }
    }
    if (overflow) {
        set_error(error_code::invalid_number, cur.begin);
        return false;
    }
if (k == fkind::bool_) {
        f.has_default = true;
        f.dflt_int = (v != 0) ? 1 : 0;
        return true;
    }
    if (k == fkind::f32 || k == fkind::f64) {
        f.has_default = true;
        f.dflt_real = static_cast<double>(v);
        return true;
    }
    if (!fits_integer(k, v)) {
        set_error(error_code::overflow, cur.begin);
        return false;
    }
    f.has_default = true;
    f.dflt_int = v;
    return true;
}

bool fbs_parser::parse_default(fbs_field_desc& f) {
    bool neg = false;
    if (cur.k == fbs_token::kind::minus) {
        neg = true;
        adv();
    }
    // A field whose type is still an unresolved reference may turn out to be an
    // enum (defaults allowed) or a table/struct/union (defaults forbidden);
    // capture the literal and let the resolution pass decide.
    const bool ref = (f.type == fbs_type::table && f.elem == fkind::none);
    if (ref) {
        if (cur.k == fbs_token::kind::number) {
            if (cur.is_real) {
                f.has_default = true;
                f.dflt_real = neg ? -cur.dval : cur.dval;
                f.dflt_real_lit = true;
                adv();
                return true;
            }
            std::uint64_t uv = cur.ival;
            adv();
            if (neg) {
                if (uv == 0x8000000000000000ull) {
                    f.dflt_int = (std::numeric_limits<std::int64_t>::min)();
                } else if (uv > 0x8000000000000000ull) {
                    set_error(error_code::invalid_number, cur.begin);
                    return false;
                } else {
                    f.dflt_int = -static_cast<std::int64_t>(uv);
                }
            } else {
                if (uv > 0x7FFFFFFFFFFFFFFFull) {
                    f.dflt_int = (std::numeric_limits<std::int64_t>::max)();
                } else {
                    f.dflt_int = static_cast<std::int64_t>(uv);
                }
            }
            f.has_default = true;
            return true;
        }
        if (cur.k == fbs_token::kind::ident) {
            f.dflt_enum_name = std::string(cur.text);
            adv();
            return true;
        }
        if (cur.k == fbs_token::kind::minus) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    if (f.type == fbs_type::string || f.type == fbs_type::vector) {
        set_error(error_code::proto_bad_field, cur.begin);
        return false;
    }
    if (f.type != fbs_type::scalar) {
        set_error(error_code::proto_bad_field, cur.begin);
        return false;
    }
    return assign_scalar_default(f, neg);
}

bool fbs_parser::parse_enum(fbs_schema_desc& d) {
    if (failed()) {
        return false;
    }
    adv(); // 'enum'
    if (cur.k != fbs_token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    fbs_enum_desc ed;
    ed.name = std::string(cur.text);
    ed.full_name = ns_.empty() ? ed.name : ns_ + "." + ed.name;
    for (const fbs_enum_desc& e : d.enums) {
        if (e.full_name == ed.full_name) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
    }
    adv();
    ed.underlying = fkind::i32; // int is the default underlying type
    if (cur.k == fbs_token::kind::colon) {
        adv();
        if (cur.k != fbs_token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        fkind k = fkind::none;
        const std::string u(cur.text);
        if (!scalar_kind_from_name(u, k) || k == fkind::bool_ ||
            k == fkind::f32 || k == fkind::f64) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        ed.underlying = k;
        adv();
    }
    if (!expect_kind(fbs_token::kind::lbrace)) {
        return false;
    }
    std::int64_t next = 0;
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == fbs_token::kind::rbrace) {
            adv();
            break;
        }
        if (cur.k == fbs_token::kind::comma ||
            cur.k == fbs_token::kind::semi) {
            adv();
            continue;
        }
        if (cur.k == fbs_token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k != fbs_token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        fbs_enum_value_desc ev;
        ev.name = std::string(cur.text);
        for (const fbs_enum_value_desc& x : ed.values) {
            if (x.name == ev.name) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
        }
        adv();
        std::int64_t v = next;
        if (cur.k == fbs_token::kind::eq) {
            adv();
            bool neg = false;
            if (cur.k == fbs_token::kind::minus) {
                neg = true;
                adv();
            }
            if (cur.k != fbs_token::kind::number || cur.is_real) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            const std::uint64_t uv = cur.ival;
            adv();
            if (neg) {
                if (uv == 0x8000000000000000ull) {
                    v = (std::numeric_limits<std::int64_t>::min)();
                } else if (uv > 0x8000000000000000ull) {
                    set_error(error_code::invalid_number, cur.begin);
                    return false;
                } else {
                    v = -static_cast<std::int64_t>(uv);
                }
            } else {
                if (uv > 0x7FFFFFFFFFFFFFFFull) {
                    set_error(error_code::invalid_number, cur.begin);
                    return false;
                }
                v = static_cast<std::int64_t>(uv);
            }
        }
        if (!fits_integer(ed.underlying, v)) {
            set_error(error_code::overflow, cur.begin);
            return false;
        }
        ev.value = v;
        next = (v == (std::numeric_limits<std::int64_t>::max)())
                   ? v
                   : v + 1;
        ed.values.push_back(std::move(ev));
    }
    if (cur.k == fbs_token::kind::semi) {
        adv();
    }
    d.enums.push_back(std::move(ed));
    return true;
}

bool fbs_parser::parse_union(fbs_schema_desc& d) {
    if (failed()) {
        return false;
    }
    adv(); // 'union'
    if (cur.k != fbs_token::kind::ident) {
        set_error(error_code::unexpected_token, cur.begin);
        return false;
    }
    fbs_union_desc ud;
    ud.name = std::string(cur.text);
    ud.full_name = ns_.empty() ? ud.name : ns_ + "." + ud.name;
    for (const fbs_union_desc& u : d.unions) {
        if (u.full_name == ud.full_name) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
    }
    adv();
    if (!expect_kind(fbs_token::kind::lbrace)) {
        return false;
    }
    std::int64_t next = 1; // NONE = 0 is implicit
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == fbs_token::kind::rbrace) {
            adv();
            break;
        }
        if (cur.k == fbs_token::kind::comma ||
            cur.k == fbs_token::kind::semi) {
            adv();
            continue;
        }
        if (cur.k == fbs_token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k != fbs_token::kind::ident) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        fbs_union_variant_desc vd;
        vd.name = std::string(cur.text);
        vd.type_ref = vd.name; // the variant name doubles as the table ref
        for (const fbs_union_variant_desc& x : ud.variants) {
            if (x.name == vd.name) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
        }
        adv();
        std::int64_t v = next;
        if (cur.k == fbs_token::kind::eq) {
            adv();
            bool neg = false;
            if (cur.k == fbs_token::kind::minus) {
                neg = true;
                adv();
            }
            if (cur.k != fbs_token::kind::number || cur.is_real) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
            const std::uint64_t uv = cur.ival;
            if (neg || uv > 0x7FFFFFFFull) {
                set_error(error_code::overflow, cur.begin);
                return false;
            }
            v = static_cast<std::int64_t>(uv);
            adv();
        }
        for (const fbs_union_variant_desc& x : ud.variants) {
            if (x.value == v) {
                set_error(error_code::unexpected_token, cur.begin);
                return false;
            }
        }
        vd.value = v;
        next = (v == (std::numeric_limits<std::int64_t>::max)())
                   ? v
                   : v + 1;
        ud.variants.push_back(std::move(vd));
    }
    if (cur.k == fbs_token::kind::semi) {
        adv();
    }
    d.unions.push_back(std::move(ud));
    return true;
}

bool fbs_parser::skip_statement() {
    int depth = 0;
    for (;;) {
        if (failed()) {
            return false;
        }
        if (cur.k == fbs_token::kind::eof) {
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == fbs_token::kind::lbrace ||
            cur.k == fbs_token::kind::lbracket ||
            cur.k == fbs_token::kind::lparen) {
            ++depth;
            adv();
            continue;
        }
        if (cur.k == fbs_token::kind::rbrace ||
            cur.k == fbs_token::kind::rbracket ||
            cur.k == fbs_token::kind::rparen) {
            if (depth > 0) {
                --depth;
                adv();
                continue;
            }
            set_error(error_code::unexpected_token, cur.begin);
            return false;
        }
        if (cur.k == fbs_token::kind::semi && depth == 0) {
            adv();
            return true;
        }
        adv();
    }
}

// ---------------------------------------------------------------------------
// Type resolution (forward/relative references resolved after the full parse)
// ---------------------------------------------------------------------------

// Candidate fully-qualified names for `ref` under `ns`, most nested first.
inline std::vector<std::string> name_candidates(const std::string& ns,
                                                const std::string& ref) {
    std::vector<std::string> out;
    if (!ref.empty() && ref[0] == '.') {
        out.push_back(ref.substr(1));
        return out;
    }
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t dot = ns.find('.', start);
        if (dot == std::string::npos) {
            parts.push_back(ns.substr(start));
            break;
        }
        parts.push_back(ns.substr(start, dot - start));
        start = dot + 1;
    }
    for (std::size_t i = parts.size(); i-- > 0;) {
        std::string prefix;
        for (std::size_t j = 0; j <= i; ++j) {
            if (j) {
                prefix.push_back('.');
            }
            prefix += parts[j];
        }
        out.push_back(prefix + "." + ref);
    }
    out.push_back(ref);
    return out;
}

inline std::string ns_of_full(const std::string& full) {
    const std::size_t dot = full.rfind('.');
    return dot == std::string::npos ? std::string()
                                    : full.substr(0, dot);
}

inline bool resolve_schema(fbs_schema_desc& d, error_info& err) {
    const auto resolve_agg = [&](const std::string& ns,
                                 const std::string& ref) -> std::size_t {
        for (const std::string& cand : name_candidates(ns, ref)) {
            for (std::size_t i = 0; i < d.types.size(); ++i) {
                if (d.types[i].full_name == cand) {
                    return i;
                }
            }
        }
        return kNoIndex;
    };
    const auto resolve_enum = [&](const std::string& ns,
                                  const std::string& ref) -> std::size_t {
        for (const std::string& cand : name_candidates(ns, ref)) {
            for (std::size_t i = 0; i < d.enums.size(); ++i) {
                if (d.enums[i].full_name == cand) {
                    return i;
                }
            }
        }
        return kNoIndex;
    };
    const auto resolve_union = [&](const std::string& ns,
                                   const std::string& ref) -> std::size_t {
        for (const std::string& cand : name_candidates(ns, ref)) {
            for (std::size_t i = 0; i < d.unions.size(); ++i) {
                if (d.unions[i].full_name == cand) {
                    return i;
                }
            }
        }
        return kNoIndex;
    };

    const auto fail = [&](error_code c, std::size_t offset) {
        if (err.code != error_code::ok) {
            return;
        }
        err.code = c;
        err.offset = offset;
    };

    // ---- union members: must reference tables ----
    for (fbs_union_desc& u : d.unions) {
        const std::string ns = ns_of_full(u.full_name);
        for (fbs_union_variant_desc& v : u.variants) {
            const std::size_t tidx = resolve_agg(ns, v.type_ref);
            if (tidx == kNoIndex || d.types[tidx].stct) {
                fail(error_code::unexpected_token, err.offset);
                return false;
            }
            v.type_index = tidx;
        }
    }

    // ---- aggregation fields ----
    for (fbs_agg_desc& agg : d.types) {
        const std::string ns = ns_of_full(agg.full_name);
        for (fbs_field_desc& f : agg.fields) {
            const bool vec_ref =
                f.type == fbs_type::vector && f.elem == fkind::table;
            const bool plain_ref = f.type == fbs_type::table;
if (vec_ref) {
                const std::size_t idx = resolve_agg(ns, f.ref);
                if (idx != kNoIndex) {
                    f.type_index = idx;
                    f.elem = d.types[idx].stct ? fkind::stct : fkind::table;
                    continue;
                }
                // A vector element may also be an enum type: the wire element
                // kind is the enum's underlying scalar (flatc `[Color]`).
                const std::size_t eidx = resolve_enum(ns, f.ref);
                if (eidx != kNoIndex) {
                    f.enum_index = eidx;
                    f.elem = d.enums[eidx].underlying;
                    continue;
                }
                fail(error_code::unexpected_token, f.type_offset);
                return false;
            }
            if (!plain_ref) {
                continue;
            }
const std::size_t aidx = resolve_agg(ns, f.ref);
            if (aidx != kNoIndex) {
                f.type_index = aidx;
                f.type = d.types[aidx].stct ? fbs_type::stct : fbs_type::table;
                if (agg.stct && !d.types[aidx].stct) {
                    fail(error_code::unexpected_token, f.type_offset);
                    return false; // structs may only inline other structs
                }
                if (d.types[aidx].stct && agg.stct) {
                    continue; // struct referencing a struct is valid
                }
                if (f.has_default || !f.dflt_enum_name.empty() ||
                    f.dflt_real_lit) {
                    fail(error_code::proto_bad_field, f.type_offset);
                    return false; // defaults are scalar-only
                }
                continue;
            }
            const std::size_t eidx = resolve_enum(ns, f.ref);
            if (eidx != kNoIndex) {
                f.type = fbs_type::enumref;
                f.enum_index = eidx;
                f.kind = d.enums[eidx].underlying;
                if (agg.stct) {
                    fail(error_code::unexpected_token, f.type_offset);
                    return false; // no enums inside structs
                }
                if (f.dflt_real_lit) {
                    fail(error_code::proto_type_mismatch, f.type_offset);
                    return false;
                }
                if (!f.dflt_enum_name.empty()) {
                    bool found = false;
                    for (const fbs_enum_value_desc& ev :
                         d.enums[eidx].values) {
                        if (ev.name == f.dflt_enum_name) {
                            f.dflt_int = ev.value;
                            f.has_default = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        fail(error_code::unexpected_token, f.type_offset);
                        return false;
                    }
                    continue;
                }
                if (f.has_default && !fits_integer(f.kind, f.dflt_int)) {
                    fail(error_code::overflow, f.type_offset);
                    return false;
                }
                continue;
            }
            const std::size_t uidx = resolve_union(ns, f.ref);
            if (uidx != kNoIndex) {
                f.type = fbs_type::unionref;
                f.type_index = uidx;
                if (agg.stct) {
                    fail(error_code::unexpected_token, f.type_offset);
                    return false;
                }
                if (f.has_default || !f.dflt_enum_name.empty() ||
                    f.dflt_real_lit) {
                    fail(error_code::proto_bad_field, f.type_offset);
                    return false;
                }
                continue;
            }
            fail(error_code::unexpected_token, f.type_offset);
            return false;
        }
    }

// ---- root_type: must resolve to a table ----
    if (!d.root_type.empty()) {
        const std::size_t idx = resolve_agg(d.ns, d.root_type);
        if (idx == kNoIndex || d.types[idx].stct) {
            fail(error_code::no_such_field, 0);
            return false;
        }
        d.root_type = d.types[idx].full_name;
    }

    // ---- id attribute layout (flatc: all or none, consecutive from 0) ----
    for (const fbs_agg_desc& agg : d.types) {
        if (agg.stct) {
            continue;
        }
        bool any = false;
        bool all = true;
        for (const fbs_field_desc& f : agg.fields) {
            any = any || f.has_id;
            all = all && f.has_id;
        }
        if (any && !all) {
            fail(error_code::proto_bad_field, 0);
            return false;
        }
        if (!any) {
            continue;
        }
        std::size_t total = 0;
        for (const fbs_field_desc& f : agg.fields) {
            total += (f.type == fbs_type::unionref) ? 2 : 1;
        }
        std::vector<bool> seen(total, false);
        bool valid = true;
        for (const fbs_field_desc& f : agg.fields) {
            if (f.type == fbs_type::unionref) {
                if (f.id == 0 || f.id > total) {
                    valid = false;
                    break;
                }
                const std::uint16_t ts = static_cast<std::uint16_t>(f.id - 1);
                if (ts >= total || seen[ts] || seen[f.id]) {
                    valid = false;
                    break;
                }
                seen[ts] = true;
                seen[f.id] = true;
            } else {
                if (f.id >= total || seen[f.id]) {
                    valid = false;
                    break;
                }
                seen[f.id] = true;
            }
        }
        for (bool b : seen) {
            valid = valid && b;
        }
        if (!valid) {
            fail(error_code::proto_bad_field, 0);
            return false;
        }
    }
    return true;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public: parse .fbs text into a schema_desc
// ---------------------------------------------------------------------------

result<fbs_schema_desc> parse_schema(rtti::string_view src) {
    result<fbs_schema_desc> r;
    detail::fbs_parser p(src);
    if (!p.parse_file(r.value)) {
        r.err = p.err;
        r.err.src = src.data();
        r.err.src_len = src.size();
        return r;
    }
    error_info err;
    err.src = src.data();
    err.src_len = src.size();
    if (!detail::resolve_schema(r.value, err)) {
        r.err = err;
        return r;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Query helpers
// ---------------------------------------------------------------------------

namespace detail {
const fbs_agg_desc* find_agg(const fbs_schema_desc& desc,
                             rtti::string_view name) noexcept {
    for (const fbs_agg_desc& t : desc.types) {
        if (t.full_name == name || t.name == name) {
            return &t;
        }
    }
    return nullptr;
}
} // namespace detail

const fbs_agg_desc* find_type(const fbs_schema_desc& desc,
                              rtti::string_view name) noexcept {
    return detail::find_agg(desc, name);
}

const fbs_enum_desc* find_enum(const fbs_schema_desc& desc,
                               rtti::string_view name) noexcept {
    for (const fbs_enum_desc& e : desc.enums) {
        if (e.full_name == name || e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

const fbs_union_desc* find_union(const fbs_schema_desc& desc,
                                 rtti::string_view name) noexcept {
    for (const fbs_union_desc& u : desc.unions) {
        if (u.full_name == name || u.name == name) {
            return &u;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Compile: schema_desc -> codec-ready schema_info (drives the flat wire codec)
// ---------------------------------------------------------------------------

struct compiled_schema::compiled_type {
    schema_info si{};
    std::vector<field_desc> fields;     // si.fields -> fields.data()
    std::vector<std::string> names;     // fd.name stable storage
    std::vector<union_variant_entry> variants;
    std::string title;                  // full name for find_type
};

compiled_schema::compiled_schema() = default;
compiled_schema::~compiled_schema() = default;
compiled_schema::compiled_schema(compiled_schema&&) noexcept = default;
compiled_schema& compiled_schema::operator=(compiled_schema&&) noexcept =
    default;

const schema_info* compiled_schema::info() const noexcept {
    if (root_index_ >= types_.size() || types_[root_index_] == nullptr) {
        return nullptr;
    }
    return &types_[root_index_]->si;
}

const schema_info* compiled_schema::find_type(
    rtti::string_view name) const noexcept {
    for (const auto& t : types_) {
        if (t != nullptr && t->title == name) {
            return &t->si;
        }
    }
    for (const auto& t : types_) {
        if (t != nullptr) {
            const std::size_t dot = t->title.rfind('.');
            const rtti::string_view last =
                dot == std::string::npos
                    ? rtti::string_view(t->title.data(), t->title.size())
                    : rtti::string_view(t->title.data() + dot + 1,
                                        t->title.size() - dot - 1);
            if (last == name) {
                return &t->si;
            }
        }
    }
    return nullptr;
}

rtti::string_view compiled_schema::root_name() const noexcept {
    return rtti::string_view(root_name_.data(), root_name_.size());
}

std::size_t compiled_schema::size() const noexcept { return types_.size(); }

namespace detail {

// Apply the parsed .fbs default to a wire field_desc (scalars only).
inline field_desc apply_parsed_default(field_desc fd,
                                       const fbs_field_desc& f) {
    if (!f.has_default) {
        return fd;
    }
    if (f.kind == fkind::f32 || f.kind == fkind::f64) {
        return with_default_real(fd, f.dflt_real);
    }
    return with_default_int(fd, f.dflt_int);
}

// Derive the vtable slot (value slot for unions) of each table field per the
// flatc rules: no `id` attributes -> sequential 0..n-1 (unions occupy the next
// two slots); every field carries an id -> the ids must cover 0..m-1 exactly
// (a union id N covers type slot N-1 and value slot N).
inline bool table_slots(const fbs_agg_desc& agg,
                        std::vector<std::uint16_t>& slots,
                        error_info& err) {
    slots.assign(agg.fields.size(), 0);
    bool any = false;
    bool all = true;
    for (const fbs_field_desc& f : agg.fields) {
        any = any || f.has_id;
        all = all && f.has_id;
    }
    if (any && !all) {
        err.code = error_code::proto_bad_field;
        return false;
    }
if (!any) {
        std::uint16_t cursor = 0;
        for (std::size_t i = 0; i < agg.fields.size(); ++i) {
            // A union occupies two slots: the type gets `cursor`, the value
            // (stored here) gets `cursor + 1`.
            slots[i] = (agg.fields[i].type == fbs_type::unionref)
                           ? static_cast<std::uint16_t>(cursor + 1)
                           : cursor;
            cursor = static_cast<std::uint16_t>(
                cursor + ((agg.fields[i].type == fbs_type::unionref) ? 2 : 1));
        }
        return true;
    }
    std::size_t total = 0;
    for (const fbs_field_desc& f : agg.fields) {
        total += (f.type == fbs_type::unionref) ? 2 : 1;
    }
    std::vector<bool> seen(total, false);
    for (std::size_t i = 0; i < agg.fields.size(); ++i) {
        const fbs_field_desc& f = agg.fields[i];
        if (f.type == fbs_type::unionref) {
            if (f.id == 0) {
                err.code = error_code::proto_bad_field;
                return false;
            }
            const std::uint16_t ts = static_cast<std::uint16_t>(f.id - 1);
            if (ts >= total || f.id >= total || seen[ts] || seen[f.id]) {
                err.code = error_code::proto_bad_field;
                return false;
            }
            seen[ts] = true;
            seen[f.id] = true;
            slots[i] = f.id;
        } else {
            if (f.id >= total || seen[f.id]) {
                err.code = error_code::proto_bad_field;
                return false;
            }
            seen[f.id] = true;
            slots[i] = f.id;
        }
    }
    for (bool b : seen) {
        if (!b) {
            err.code = error_code::proto_bad_field;
            return false;
        }
    }
    return true;
}

} // namespace detail

result<compiled_schema> compile_schema(const fbs_schema_desc& desc,
                                       const compile_config& cfg) {
    result<compiled_schema> r;
    if (desc.types.empty()) {
        r.err.code = error_code::invalid_root;
        return r;
    }
    std::string root_name = cfg.root_name;
    if (root_name.empty()) {
        root_name = desc.root_type;
    }
    std::size_t root = detail::kNoIndex;
    if (!root_name.empty()) {
        for (std::size_t i = 0; i < desc.types.size(); ++i) {
            if (desc.types[i].full_name == root_name ||
                desc.types[i].name == root_name) {
                if (desc.types[i].stct) {
                    r.err.code = error_code::invalid_root; // root must be table
                    return r;
                }
                root = i;
                break;
            }
        }
        if (root == detail::kNoIndex) {
            r.err.code = error_code::no_such_field;
            return r;
        }
    } else {
        r.err.code = error_code::invalid_root; // no root selectable
        return r;
    }

    // Per-table slot assignment (validates any explicit id layout).
    std::vector<std::vector<std::uint16_t>> slots(desc.types.size());
    error_info err;
    for (std::size_t i = 0; i < desc.types.size(); ++i) {
        if (!desc.types[i].stct &&
            !detail::table_slots(desc.types[i], slots[i], err)) {
            r.err = err;
            return r;
        }
    }

    compiled_schema& out = r.value;
    out.file_identifier_ = desc.file_identifier;
    out.root_name_ = desc.types[root].full_name;
    out.root_index_ = root;
    out.types_.reserve(desc.types.size());
    for (const fbs_agg_desc& t : desc.types) {
        out.types_.push_back(
            std::unique_ptr<compiled_schema::compiled_type>(
                new compiled_schema::compiled_type()));
        out.types_.back()->title = t.full_name;
    }

for (std::size_t i = 0; i < desc.types.size(); ++i) {
        compiled_schema::compiled_type& ct = *out.types_[i];
        const fbs_agg_desc& agg = desc.types[i];
        // Reserve exactly up front (review C-1): every field_desc.name is a
        // c_str() into ct.names and every union field_desc stores a pointer
        // into ct.variants; those addresses must not dangle when the vectors
        // grow past a capacity boundary with 2+ union fields.
        std::size_t n_names = 0;
        std::size_t n_variants = 0;
        for (const fbs_field_desc& f : agg.fields) {
            const bool uni = f.type == fbs_type::unionref;
            n_names += uni ? 2 : 1;
            if (uni && f.type_index < desc.unions.size()) {
                n_variants += desc.unions[f.type_index].variants.size();
            }
        }
        ct.fields.reserve(n_names);
        ct.names.reserve(n_names);
        ct.variants.reserve(n_variants);
        std::uint16_t seq = 0;
        for (const fbs_field_desc& f : agg.fields) {
            const char* name_ptr = nullptr;
            ct.names.push_back(f.name);
            name_ptr = ct.names.back().c_str();
            const auto type_at = [&](std::size_t idx) -> const schema_info* {
                return (idx < desc.types.size()) ? &out.types_[idx]->si
                                                 : nullptr;
            };

            if (f.type == fbs_type::unionref) {
                // value slot from the resolved layout; type slot is N-1.
                const std::uint16_t v_slot =
                    agg.stct ? seq : slots[i][static_cast<std::size_t>(
                                        &f - agg.fields.data())];
                if (v_slot == 0) {
                    r.err.code = error_code::proto_bad_field;
                    return r;
                }
                const std::uint16_t t_slot =
                    static_cast<std::uint16_t>(v_slot - 1);
                ct.names.push_back(f.name + "_type");
                const char* const type_name = ct.names.back().c_str();
                ct.fields.push_back(make_field_desc(
                    type_name, t_slot, fkind::union_type));
                const std::size_t vstart = ct.variants.size();
                if (f.type_index < desc.unions.size()) {
                    for (const fbs_union_variant_desc& v :
                         desc.unions[f.type_index].variants) {
                        union_variant_entry en;
                        en.value = static_cast<std::uint32_t>(v.value);
                        en.schema = type_at(v.type_index);
                        ct.variants.push_back(en);
                    }
                }
                ct.fields.push_back(make_union_value(
                    name_ptr, v_slot, t_slot,
                    ct.variants.empty() ? nullptr : &ct.variants[vstart],
                    ct.variants.size() - vstart));
                if (agg.stct) {
                    seq = static_cast<std::uint16_t>(seq + 1);
                }
                continue;
            }

            const std::uint16_t slot =
                agg.stct ? seq
                         : slots[i][static_cast<std::size_t>(
                               &f - agg.fields.data())];
            if (!agg.stct) {
                seq = static_cast<std::uint16_t>(seq + 1);
            } else {
                ++seq;
            }
            field_desc fd;
            switch (f.type) {
                case fbs_type::scalar:
                case fbs_type::enumref:
                    fd = detail::apply_parsed_default(
                        make_field_desc(name_ptr, slot, f.kind), f);
                    break;
                case fbs_type::string:
                    fd = make_field_desc(name_ptr, slot, fkind::string);
                    break;
                case fbs_type::vector: {
                    fd = make_field_desc(name_ptr, slot, fkind::vector, f.elem);
                    if (f.elem == fkind::table || f.elem == fkind::stct) {
                        fd.sub = type_at(f.type_index);
                    }
                    break;
                }
                case fbs_type::table:
                    fd = make_field_desc(name_ptr, slot, fkind::table,
                                         fkind::none, type_at(f.type_index));
                    break;
                case fbs_type::stct:
                    fd = make_field_desc(name_ptr, slot, fkind::stct,
                                         fkind::none, type_at(f.type_index));
                    break;
                default:
                    r.err.code = error_code::kind_mismatch;
                    return r;
            }
            ct.fields.push_back(fd);
        }
        ct.si.fields = ct.fields.empty() ? nullptr : ct.fields.data();
        ct.si.count = ct.fields.size();
        ct.si.identifier = nullptr;
    }

    // The root schema carries the file identifier.
    if (!out.file_identifier_.empty() && root < out.types_.size()) {
        out.types_[root]->si.identifier = out.file_identifier_.c_str();
    }
    return r;
}

result<compiled_schema> compile_schema(rtti::string_view src,
                                       const compile_config& cfg) {
    const result<fbs_schema_desc> p = parse_schema(src);
    if (!p.ok()) {
        result<compiled_schema> r;
        r.err = p.err;
        return r;
    }
    return compile_schema(p.value, cfg);
}

} // namespace flat
} // namespace rtti
