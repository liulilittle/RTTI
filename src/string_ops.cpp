// rtti::str string-operation implementations (spec 7.6 / Task 8a).
// All non-template code lives here so the library keeps a real cpp in the
// static build; the header only carries declarations and the thin conversion
// shells. Pure string transforms are noexcept; parse surfaces (unquote /
// unescape / from_base) report error_code and never crash on malformed input
// (spec 0 fail-safe).
//
// English comments only; C++17; no third-party dependencies.

#include <rtti/string_ops.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace rtti {
namespace str {

// ---------------------------------------------------------------------------
// Base conversion bridge (forward to the existing arbitrary-base layer).
// ---------------------------------------------------------------------------

std::string to_base(std::uint64_t value, unsigned base, const char* digits) {
    return rtti::to_string_base(value, base, digits);
}

std::optional<std::uint64_t> from_base(string_view s, unsigned base) {
    return rtti::from_string_base(s, base);
}

error_code from_base(string_view s, unsigned base, std::uint64_t& out) noexcept {
    return rtti::from_string_base(s, base, out);
}

std::string to_base_signed(std::int64_t value, unsigned base) {
    return rtti::to_string_base_signed(value, base);
}

std::optional<std::int64_t> from_base_signed(string_view s, unsigned base) {
    return rtti::from_string_base_signed(s, base);
}

error_code from_base_signed(string_view s, unsigned base,
                            std::int64_t& out) noexcept {
    return rtti::from_string_base_signed(s, base, out);
}

// ---------------------------------------------------------------------------
// split / join
// ---------------------------------------------------------------------------

std::vector<std::string> split(string_view s, char sep) noexcept {
    std::vector<std::string> out;
    if (s.empty()) {
        return out;
    }
    std::size_t start = 0;
    while (start < s.size()) {
        const std::size_t pos = s.find(sep, start);
        if (pos == string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    if (start >= s.size()) {
        // A trailing separator contributes a final empty token.
        out.emplace_back(std::string());
    }
    return out;
}

std::vector<std::string> split(string_view s, string_view sep) noexcept {
    std::vector<std::string> out;
    if (sep.empty()) {
        // Empty separator: the whole input is a single token.
        if (!s.empty()) {
            out.emplace_back(s);
        }
        return out;
    }
    if (s.empty()) {
        return out;
    }
    std::size_t start = 0;
    while (start < s.size()) {
        const std::size_t pos = s.find(sep, start);
        if (pos == string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + sep.size();
    }
    if (start >= s.size()) {
        out.emplace_back(std::string());
    }
    return out;
}

std::string join(const std::vector<std::string>& parts, string_view sep) noexcept {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out.append(sep.data(), sep.size());
        }
        out.append(parts[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// trim
// ---------------------------------------------------------------------------

namespace {
bool is_in_chars(char c, string_view chars) noexcept {
    return chars.find(c) != string_view::npos;
}
} // namespace

std::string ltrim(string_view s, string_view chars) noexcept {
    std::size_t b = 0;
    while (b < s.size() && is_in_chars(s[b], chars)) {
        ++b;
    }
    return std::string(s.substr(b));
}

std::string rtrim(string_view s, string_view chars) noexcept {
    std::size_t e = s.size();
    while (e > 0 && is_in_chars(s[e - 1], chars)) {
        --e;
    }
    return std::string(s.substr(0, e));
}

std::string trim(string_view s, string_view chars) noexcept {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_in_chars(s[b], chars)) {
        ++b;
    }
    while (e > b && is_in_chars(s[e - 1], chars)) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

// ---------------------------------------------------------------------------
// case
// ---------------------------------------------------------------------------

std::string to_lower(string_view s) noexcept {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string to_upper(string_view s) noexcept {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// starts_with / ends_with
// ---------------------------------------------------------------------------

bool starts_with(string_view s, string_view prefix) noexcept {
    if (prefix.size() > s.size()) {
        return false;
    }
    return s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(string_view s, string_view suffix) noexcept {
    if (suffix.size() > s.size()) {
        return false;
    }
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ---------------------------------------------------------------------------
// replace / replace_all
// ---------------------------------------------------------------------------

std::string replace(string_view s, string_view from, string_view to) noexcept {
    if (from.empty()) {
        return std::string(s);
    }
    const std::size_t pos = s.find(from);
    if (pos == string_view::npos) {
        return std::string(s);
    }
    std::string out;
    out.reserve(s.size() + to.size() - from.size());
    out.append(s.data(), pos);
    out.append(to.data(), to.size());
    out.append(s.data() + pos + from.size(), s.size() - pos - from.size());
    return out;
}

std::string replace_all(string_view s, string_view from, string_view to) noexcept {
    if (from.empty()) {
        return std::string(s);
    }
    std::string out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t pos = s.find(from, start);
        if (pos == string_view::npos) {
            out.append(s.data() + start, s.size() - start);
            break;
        }
        out.append(s.data() + start, pos - start);
        out.append(to.data(), to.size());
        start = pos + from.size();
    }
    return out;
}

// ---------------------------------------------------------------------------
// repeat
// ---------------------------------------------------------------------------

std::string repeat(string_view s, std::size_t count) noexcept {
    if (count == 0 || s.empty()) {
        return std::string();
    }
    if (s.size() > (std::numeric_limits<std::size_t>::max)() / count) {
        return std::string();
    }
    std::string out;
    out.reserve(s.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out.append(s.data(), s.size());
    }
    return out;
}

// ---------------------------------------------------------------------------
// pad
// ---------------------------------------------------------------------------

std::string pad_left(string_view s, std::size_t width, char fill) noexcept {
    if (s.size() >= width) {
        return std::string(s);
    }
    std::string out;
    out.reserve(width);
    out.append(width - s.size(), fill);
    out.append(s.data(), s.size());
    return out;
}

std::string pad_right(string_view s, std::size_t width, char fill) noexcept {
    if (s.size() >= width) {
        return std::string(s);
    }
    std::string out;
    out.reserve(width);
    out.append(s.data(), s.size());
    out.append(width - s.size(), fill);
    return out;
}

// ---------------------------------------------------------------------------
// reverse
// ---------------------------------------------------------------------------

std::string reverse(string_view s) noexcept {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = s.size(); i > 0; --i) {
        out.push_back(s[i - 1]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// escape / unescape (JSON/C-style; control bytes < 0x20 become \u00XX)
// ---------------------------------------------------------------------------

std::string escape(string_view s) noexcept {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
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
                    out.push_back(kHex[(c >> 4) & 0xF]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

namespace {
bool hex_digit(char c, unsigned& v) noexcept {
    if (c >= '0' && c <= '9') {
        v = static_cast<unsigned>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        v = static_cast<unsigned>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        v = static_cast<unsigned>(c - 'A' + 10);
        return true;
    }
    return false;
}

void encode_utf8(unsigned long cp, std::string& out) noexcept {
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
} // namespace

std::string unescape(string_view s) noexcept {
    std::string out;
    if (unescape(s, out) != error_code::ok) {
        return std::string();
    }
    return out;
}

error_code unescape(string_view s, std::string& out) noexcept {
    out.clear();
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c != '\\') {
            out.push_back(c);
            ++i;
            continue;
        }
        if (i + 1 >= s.size()) {
            return error_code::unterminated_string;
        }
        const char e = s[i + 1];
        if (e == 'u') {
            if (i + 6 > s.size()) {
                return error_code::invalid_utf8;
            }
            unsigned cp = 0;
            for (int k = 0; k < 4; ++k) {
                unsigned v = 0;
                if (!hex_digit(s[i + 2 + static_cast<std::size_t>(k)], v)) {
                    return error_code::invalid_utf8;
                }
                cp = (cp << 4) | v;
            }
            i += 6;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                // High surrogate: require a low-surrogate pair.
                if (i + 6 > s.size() || s[i] != '\\' || s[i + 1] != 'u') {
                    return error_code::invalid_utf8;
                }
                unsigned lo = 0;
                for (int k = 0; k < 4; ++k) {
                    unsigned v = 0;
                    if (!hex_digit(s[i + 2 + static_cast<std::size_t>(k)], v)) {
                        return error_code::invalid_utf8;
                    }
                    lo = (lo << 4) | v;
                }
                if (lo < 0xDC00 || lo > 0xDFFF) {
                    return error_code::invalid_utf8;
                }
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 6;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return error_code::invalid_utf8;
            }
            encode_utf8(cp, out);
            continue;
        }
        i += 2;
        switch (e) {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                return error_code::unexpected_token;
        }
    }
    return error_code::ok;
}

// ---------------------------------------------------------------------------
// quote / unquote
// ---------------------------------------------------------------------------

std::string quote(string_view s) noexcept {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    out += escape(s);
    out.push_back('"');
    return out;
}

std::string unquote(string_view s) noexcept {
    std::string out;
    if (unquote(s, out) != error_code::ok) {
        return std::string();
    }
    return out;
}

error_code unquote(string_view s, std::string& out) noexcept {
    out.clear();
    if (s.size() < 2 || s.front() != '"') {
        return error_code::kind_mismatch;
    }
    if (s.back() != '"') {
        return error_code::unterminated_string;
    }
    return unescape(s.substr(1, s.size() - 2), out);
}

} // namespace str
} // namespace rtti