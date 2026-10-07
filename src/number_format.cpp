// Number formatting core (spec §7.2). The non-template implementation of the
// public format_number family: parses the format-spec grammar declared in
// <rtti/number_format.hpp> and emits grouped / padded / aligned / base-converted
// integers and fixed / scientific / shortest reals.
//
// The spec grammar (documented in the header):
//     [[fill]align] [sign] ['#'] ['0'] [width] [','] ['.' precision] [type]
// A malformed spec reports error_code::unexpected_token through the optional
// error_info and the caller receives an empty string (spec §0 fail-safe).
//
// English comments only; C++17; no third-party dependencies.

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <string>

#include <rtti/base_convert.hpp>
#include <rtti/number_format.hpp>

namespace rtti {
namespace detail {

// Parsed component fields of one format spec (defined out of line; the header
// only forward-declares it so the parser can live in the static library).
enum class align_kind { left, right, center, pad_after_sign };

struct number_parts {
    char fill = ' ';
    align_kind align = align_kind::right;
    char sign = 0; // 0 = sign only for negatives, '+' always, ' ' for positives
    bool alt = false;   // '#' alternate form for non-decimal integer bases
    bool zero = false;  // '0' zero-pad flag
    bool grouping = false; // ',' thousands grouping
    bool has_precision = false;
    int precision = 0;
    std::size_t width = 0;
    char type = 0; // 0 = family default; see parse
    bool ok = true;
    error_code err = error_code::ok;
};

namespace {

inline align_kind align_from(char c) noexcept {
    switch (c) {
        case '<': return align_kind::left;
        case '^': return align_kind::center;
        case '=': return align_kind::pad_after_sign;
        default: return align_kind::right;
    }
}

// Uppercase the a-f hex digits of a base-16 literal in place.
inline std::string upper_hex(const std::string& s) {
    std::string r = s;
    for (char& c : r) {
        if (c >= 'a' && c <= 'f') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return r;
}

// Insert a ',' between every three digits counted from the right.
inline std::string group_thousands(const std::string& digits) {
    const std::size_t n = digits.size();
    if (n <= 3) {
        return digits;
    }
    std::string out;
    out.reserve(n + n / 3);
    for (std::size_t i = 0; i < n; ++i) {
        if (i != 0 && (n - i) % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(digits[i]);
    }
    return out;
}

// Group only the integer part of a fixed-notation real body ("1234567.00" ->
// "1,234,567.00").
inline std::string group_fixed_real(const std::string& body) {
    const std::size_t dot = body.find('.');
    if (dot == std::string::npos) {
        return group_thousands(body);
    }
    return group_thousands(body.substr(0, dot)) + body.substr(dot);
}

// Ensure a scientific exponent has at least two digits ("1.2e+6" -> "1.2e+06").
inline std::string normalize_exponent(std::string s) {
    const std::size_t ep = s.find_first_of("eE");
    if (ep == std::string::npos || ep + 2 >= s.size()) {
        return s;
    }
    const std::string exp = s.substr(ep + 2);
    if (exp.size() < 2) {
        s.insert(ep + 2, 2 - exp.size(), '0');
    }
    return s;
}

// Fixed-notation body with an explicit precision (rounds to nearest, ties to
// even like printf's f). MSVC's <charconv> extension carries the precision
// overload; other toolchains fall back to snprintf (still valid C).
inline bool fixed_body(double value, int precision, std::string& out) {
    char buf[4096];
#if defined(_MSC_VER)
    const auto r = std::to_chars(buf, buf + sizeof(buf), value,
                                 std::chars_format::fixed, precision);
    if (r.ec != std::errc()) {
        return false;
    }
    out.append(buf, static_cast<std::size_t>(r.ptr - buf));
    return true;
#else
    const int n = std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buf)) {
        return false;
    }
    out.append(buf, static_cast<std::size_t>(n));
    return true;
#endif
}

// Scientific body "d.dddde±XX" with an explicit precision and a normalized
// (>= 2 digit) exponent.
inline bool scientific_body(double value, int precision, std::string& out) {
    char buf[4096];
#if defined(_MSC_VER)
    const auto r = std::to_chars(buf, buf + sizeof(buf), value,
                                 std::chars_format::scientific, precision);
    if (r.ec != std::errc()) {
        return false;
    }
    out.append(buf, static_cast<std::size_t>(r.ptr - buf));
#else
    const int n = std::snprintf(buf, sizeof(buf), "%.*e", precision, value);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buf)) {
        return false;
    }
    out.append(buf, static_cast<std::size_t>(n));
#endif
    out = normalize_exponent(out);
    return true;
}

// Shortest round-tripping body (std::to_chars general).
inline bool shortest_body(double value, std::string& out) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), value);
    if (r.ec != std::errc()) {
        return false;
    }
    out.append(buf, static_cast<std::size_t>(r.ptr - buf));
    return true;
}

// Render the sign/alt-prefix/digits triplet under the spec's width/align/fill
// and finally the padding rules (spec §7.2). The `zero` flag and the '=' align
// both pad after the sign; regular aligns pad around the whole content.
inline void emit_number(std::string& out, const number_parts& p,
                        const std::string& sign_text,
                        const std::string& alt_prefix,
                        const std::string& body) {
    const std::string content = sign_text + alt_prefix + body;
    const std::size_t w = p.width;
    if (p.align == align_kind::pad_after_sign || p.zero) {
        const std::size_t pad = w > content.size() ? w - content.size() : 0;
        out += sign_text;
        out += alt_prefix;
        out.append(pad, p.zero ? '0' : p.fill);
        out += body;
        return;
    }
    if (content.size() >= w) {
        out += content;
        return;
    }
    const std::size_t pad = w - content.size();
    switch (p.align) {
        case align_kind::left:
            out += content;
            out.append(pad, p.fill);
            break;
        case align_kind::center: {
            const std::size_t left = pad / 2;
            out.append(left, p.fill);
            out += content;
            out.append(pad - left, p.fill);
            break;
        }
        default:
            out.append(pad, p.fill);
            out += content;
            break;
    }
}

// Map the spec type to its base and optional hex uppercasing; produce the
// base-converted / zero-min-padded / grouped digit string plus the '#'-form
// prefix. Shared by the signed and unsigned entry points.
inline void build_integral_text(std::uint64_t magnitude, const number_parts& p,
                                std::string& digits, std::string& alt_prefix) {
    unsigned base = 10;
    bool upper = false;
    switch (p.type) {
        case 'b': base = 2; break;
        case 'o': base = 8; break;
        case 'x': base = 16; break;
        case 'X': base = 16; upper = true; break;
        default: base = 10; break;
    }
    digits = rtti::detail::to_base(magnitude, base);
    if (upper) {
        digits = upper_hex(digits);
    }
    if (p.has_precision) {
        const std::size_t min = static_cast<std::size_t>(p.precision);
        if (digits.size() < min) {
            digits.insert(digits.begin(), min - digits.size(), '0');
        }
    }
    if (p.grouping && base == 10) {
        digits = group_thousands(digits);
    }
    alt_prefix.clear();
    if (p.alt && base != 10) {
        if (base == 2) {
            alt_prefix = "0b";
        } else if (base == 8) {
            alt_prefix = "0o";
        } else {
            alt_prefix = upper ? "0X" : "0x";
        }
    }
}

// True when the byte is a legal format-spec type char for the family.
inline bool type_allowed(char c, bool is_floating) noexcept {
    if (is_floating) {
        return c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' ||
               c == 'G';
    }
    return c == 'd' || c == 'b' || c == 'o' || c == 'x' || c == 'X';
}

} // namespace

number_parts parse_number_spec(string_view spec, bool is_floating,
                               error_info* err) {
    number_parts p;
    const auto fail = [&p, err]() {
        p.ok = false;
        p.err = error_code::unexpected_token;
        if (err != nullptr) {
            err->code = error_code::unexpected_token;
        }
    };
    const std::size_t n = spec.size();
    std::size_t i = 0;
    // Optional leading "fill align": any non-structural byte followed by an
    // align byte. Digits are valid fills ("1<6"), so the marker is the align.
    // "08x" stays zero-flag + width because '8' is not an align byte.
    if (n >= 2) {
        const char a = spec[0];
        const char b = spec[1];
        const bool b_align = b == '<' || b == '>' || b == '^' || b == '=';
        const bool a_struct = a == '<' || a == '>' || a == '^' || a == '=' ||
                              a == '+' || a == '-' || a == ' ' || a == ',' ||
                              a == '.' || a == '#';
        if (b_align && !a_struct) {
            p.fill = a;
            p.align = align_from(b);
            i = 2;
        }
    }
    bool align_seen = (i == 2);
    bool sign_seen = false;
    bool zero_seen = false;
    bool width_started = false;
    for (; i < n; ++i) {
        const char c = spec[i];
        if ((c == '<' || c == '>' || c == '^' || c == '=') && !align_seen) {
            p.align = align_from(c);
            align_seen = true;
        } else if ((c == '+' || c == '-' || c == ' ') && !sign_seen) {
            p.sign = c;
            sign_seen = true;
        } else if (c == '#') {
            if (p.alt) {
                fail();
                return p;
            }
            p.alt = true;
        } else if (c == ',' ) {
            if (p.grouping) {
                fail();
                return p;
            }
            p.grouping = true;
        } else if (c == '0' && !zero_seen && !width_started && !p.has_precision) {
            p.zero = true;
            zero_seen = true;
        } else if (c == '.') {
            p.has_precision = true;
            ++i;
            int prec = 0;
            bool any = false;
            while (i < n && spec[i] >= '0' && spec[i] <= '9') {
                if (prec > 1024) { // absurd precision would overflow the buffer
                    fail();
                    return p;
                }
                prec = prec * 10 + (spec[i] - '0');
                ++i;
                any = true;
            }
            --i;
            if (!any) {
                fail();
                return p;
            }
            p.precision = prec;
        } else if (c >= '0' && c <= '9') {
            width_started = true;
            const std::size_t d = static_cast<std::size_t>(c - '0');
            const std::size_t max = (std::numeric_limits<std::size_t>::max)();
            if (p.width > (max - d) / 10) {
                p.width = max; // clamp instead of wrapping
            } else {
                p.width = p.width * 10 + d;
            }
        } else if (type_allowed(c, is_floating)) {
            p.type = c;
            if (i + 1 != n) { // the type must be the last component
                fail();
                return p;
            }
            return p;
        } else {
            fail();
            return p;
        }
    }
    return p;
}

std::string format_number_unsigned(std::uint64_t value, string_view spec,
                                   error_info* err) {
    const number_parts p = parse_number_spec(spec, false, err);
    if (!p.ok) {
        return std::string();
    }
    std::string digits;
    std::string alt;
    build_integral_text(value, p, digits, alt);
    std::string sign_text;
    if (p.sign == '+') {
        sign_text = "+";
    } else if (p.sign == ' ') {
        sign_text = " ";
    }
    std::string out;
    out.reserve(digits.size() + 32);
    emit_number(out, p, sign_text, alt, digits);
    return out;
}

std::string format_number_signed(std::int64_t value, string_view spec,
                                 error_info* err) {
    const number_parts p = parse_number_spec(spec, false, err);
    if (!p.ok) {
        return std::string();
    }
    const bool negative = value < 0;
    // Magnitude of INT64_MIN fits uint64 (same idiom as date_time/base_convert).
    const std::uint64_t magnitude =
        negative
            ? (static_cast<std::uint64_t>(-(value + 1)) + 1u)
            : static_cast<std::uint64_t>(value);
    std::string digits;
    std::string alt;
    build_integral_text(magnitude, p, digits, alt);
    std::string sign_text;
    if (negative) {
        sign_text = "-";
    } else if (p.sign == '+') {
        sign_text = "+";
    } else if (p.sign == ' ') {
        sign_text = " ";
    }
    std::string out;
    out.reserve(digits.size() + 32);
    emit_number(out, p, sign_text, alt, digits);
    return out;
}

std::string format_number_real(double value, string_view spec,
                               error_info* err) {
    const number_parts p = parse_number_spec(spec, true, err);
    if (!p.ok) {
        return std::string();
    }
    const bool negative = std::signbit(value);
    const double magnitude = std::fabs(value);

    // Non-finite tokens keep the deterministic "inf"/"-inf"/"nan" forms and
    // only apply the sign/width/align/fill machinery.
    std::string body;
    if (std::isnan(value)) {
        body = "nan";
    } else if (std::isinf(value)) {
        body = "inf";
    } else {
        const char t = p.type == 0 ? '\0' : p.type;
        if (t == 'f' || t == 'F') {
            const int precision = p.has_precision ? p.precision : 6;
            if (!fixed_body(magnitude, precision, body)) {
                goto fail;
            }
            if (p.grouping) {
                body = group_fixed_real(body);
            }
        } else if (t == 'e' || t == 'E') {
            const int precision = p.has_precision ? p.precision : 6;
            if (!scientific_body(magnitude, precision, body)) {
                goto fail;
            }
            if (t == 'E') {
                const std::size_t ep = body.find('e');
                if (ep != std::string::npos) {
                    body[ep] = 'E';
                }
            }
        } else {
            // 'g'/'G'/empty: shortest round-tripping text.
            if (!shortest_body(magnitude, body)) {
                goto fail;
            }
        }
    }

    {
        std::string sign_text;
        if (negative) {
            sign_text = "-";
        } else if (p.sign == '+') {
            sign_text = "+";
        } else if (p.sign == ' ') {
            sign_text = " ";
        }
        std::string out;
        out.reserve(body.size() + 32);
        emit_number(out, p, sign_text, std::string(), body);
        return out;
    }

fail:
    if (err != nullptr) {
        err->code = error_code::invalid_number;
    }
    return std::string();
}

} // namespace detail
} // namespace rtti