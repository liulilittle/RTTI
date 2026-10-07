#pragma once
// rtti::date_time: a value type holding Unix epoch nanoseconds with ISO 8601
// parsing/formatting and precision-aware unit conversion (spec §14).
//
// Core design:
//   * int64 nanosecond storage covers roughly 1677..2262 at ns resolution;
//   * from_unix_* range-check the scaled product and report error_code::overflow
//     instead of ever wrapping (spec §0);
//   * parse() accepts the ISO 8601 subset "YYYY-MM-DDTHH:MM:SS[.frac]
//     [Z|+/-HH:MM]" (fraction 1..9 digits, offsets normalized to UTC) and
//     returns error_code::invalid_number on malformed text;
//   * format()/to_string() emit the canonical UTC form, exact round-trip;
//   * format(string_view pattern) emits a strftime-style subset (spec §7.1):
//     %Y %y %m %d %H %M %S %f %F %T %R %z %Z %%; an unknown or malformed
//     directive reports error_code::unexpected_token / returns an empty string
//     (spec §0 fail-safe, never a crash);
//   * numeric epoch -> ns is inferred from the decimal digit count (10 digits
//     = seconds, 13 = ms, 16 = us, 19/20 = ns) via epoch_ns_from_digits.
//
// Zero format dependency: this header never includes a format adapter.
// English comments only; C++17; no third-party dependencies.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include <rtti/error.hpp>
#include <rtti/memory.hpp>

namespace rtti {

class date_time {
public:
    // Epoch zero: 1970-01-01T00:00:00Z.
    date_time() noexcept : ns_(0) {}
    // Construct from an exact epoch nanosecond count.
    explicit date_time(std::int64_t epoch_ns) noexcept : ns_(epoch_ns) {}

    // Coarser Unix epoch factories. The scaled nanosecond product is
    // range-checked; a value out of the int64 range reports overflow.
    static result<date_time> from_unix_seconds(std::int64_t seconds) {
        if (seconds > 9223372036LL || seconds < -9223372036LL) {
            return make_error<date_time>(error_code::overflow);
        }
        return make_ok<date_time>(date_time(seconds * 1000000000LL));
    }
    static result<date_time> from_unix_ms(std::int64_t ms) {
        if (ms > 9223372036854LL || ms < -9223372036854LL) {
            return make_error<date_time>(error_code::overflow);
        }
        return make_ok<date_time>(date_time(ms * 1000000LL));
    }
    static result<date_time> from_unix_us(std::int64_t us) {
        if (us > 9223372036854775LL || us < -9223372036854775LL) {
            return make_error<date_time>(error_code::overflow);
        }
        return make_ok<date_time>(date_time(us * 1000LL));
    }
    static result<date_time> from_unix_ns(std::int64_t ns) {
        return make_ok<date_time>(date_time(ns));
    }

    // Unit accessors. Sub-second counts use floor division toward negative
    // infinity, so a pre-epoch instant with a fractional part maps to the
    // whole second that actually contains it. None of these can overflow the
    // int64 output (the divisor is at least 1000); they return error_code::ok.
    error_code to_unix_seconds(std::int64_t& out) const noexcept {
        out = floor_div(ns_, 1000000000LL);
        return error_code::ok;
    }
    error_code to_unix_ms(std::int64_t& out) const noexcept {
        out = floor_div(ns_, 1000000LL);
        return error_code::ok;
    }
    error_code to_unix_us(std::int64_t& out) const noexcept {
        out = floor_div(ns_, 1000LL);
        return error_code::ok;
    }
    error_code to_unix_ns(std::int64_t& out) const noexcept {
        out = ns_;
        return error_code::ok;
    }

    // Raw epoch nanoseconds.
    std::int64_t epoch_ns() const noexcept { return ns_; }

    // Canonical ISO 8601 UTC text, exact round-trip: whole seconds format as
    // "YYYY-MM-DDTHH:MM:SSZ"; a nonzero sub-second part appends the shortest
    // ".fff"/".ffffff"/".fffffffff" fraction that carries the exact value.
    std::string format() const {
        std::string out;
        out.reserve(32);
        const date_fields f = decompose();
        append_year(out, f.year);
        out.push_back('-');
        append_two(out, f.month);
        out.push_back('-');
        append_two(out, f.day);
        out.push_back('T');
        append_two(out, f.hour);
        out.push_back(':');
        append_two(out, f.minute);
        out.push_back(':');
        append_two(out, f.second);
        if (f.nanos != 0) {
            out.push_back('.');
            char fbuf[10];
            std::int64_t fn = f.nanos;
            fbuf[9] = '\0';
            for (int k = 8; k >= 0; --k) {
                fbuf[k] = static_cast<char>('0' + fn % 10);
                fn /= 10;
            }
            std::size_t len = 9;
            while (len > 0 && fbuf[len - 1] == '0') {
                --len;
            }
            out.append(fbuf, len);
        }
        out.push_back('Z');
        return out;
    }
    // Alias of format().
    std::string to_string() const { return format(); }

    // strftime-style pattern formatting (spec §7.1). Supported directives:
    //   %Y  year as decimal, at least four digits
    //   %y  year mod 100, two digits
    //   %m  month (01..12)
    //   %d  day of month (01..31)
    //   %H  hour (00..23)
    //   %M  minute (00..59)
    //   %S  second (00..59)
    //   %f  nanosecond fraction, exactly nine digits
    //   %F  %Y-%m-%d
    //   %T  %H:%M:%S
    //   %R  %H:%M
    //   %z  UTC offset, "+0000"
    //   %Z  timezone name, "UTC"
    //   %%  a literal '%'
    // Every other byte is copied verbatim. Any unknown directive, a trailing
    // lone '%' and any malformed pattern produce error_code::unexpected_token;
    // the string overload returns an empty string so callers can rely on the
    // boolean "empty == failure" signal (spec §0 fail-safe, never a crash).
    // The instant is UTC-normalized (offsets never survive a parse), so %z and
    // %Z are constants.
    // The out-parameter overload APPENDS into `out` (it never clears the
    // buffer) and, on an invalid directive, `out` holds the partial text
    // produced before the error.
    error_code format(string_view pattern, std::string& out) const {
        const date_fields f = decompose();
        const std::size_t n = pattern.size();
        std::size_t i = 0;
        while (i < n) {
            const char c = pattern[i++];
            if (c != '%') {
                out.push_back(c);
                continue;
            }
            if (i >= n) {
                return error_code::unexpected_token;
            }
            const char d = pattern[i++];
            switch (d) {
                case 'Y':
                    append_year(out, f.year);
                    break;
                case 'y':
                    append_two(out, ((f.year % 100) + 100) % 100);
                    break;
                case 'm':
                    append_two(out, f.month);
                    break;
                case 'd':
                    append_two(out, f.day);
                    break;
                case 'H':
                    append_two(out, f.hour);
                    break;
                case 'M':
                    append_two(out, f.minute);
                    break;
                case 'S':
                    append_two(out, f.second);
                    break;
                case 'f':
                    append_nanos9(out, f.nanos);
                    break;
                case 'F':
                    append_year(out, f.year);
                    out.push_back('-');
                    append_two(out, f.month);
                    out.push_back('-');
                    append_two(out, f.day);
                    break;
                case 'T':
                    append_two(out, f.hour);
                    out.push_back(':');
                    append_two(out, f.minute);
                    out.push_back(':');
                    append_two(out, f.second);
                    break;
                case 'R':
                    append_two(out, f.hour);
                    out.push_back(':');
                    append_two(out, f.minute);
                    break;
                case 'z':
                    out += "+0000";
                    break;
                case 'Z':
                    out += "UTC";
                    break;
                case '%':
                    out.push_back('%');
                    break;
                default:
                    return error_code::unexpected_token;
            }
        }
        return error_code::ok;
    }
    // Convenience overload: the pattern output, or an empty string when the
    // pattern contains an unsupported directive (the error code is available
    // through the out-parameter overload above).
    std::string format(string_view pattern) const {
        std::string out;
        if (format(pattern, out) != error_code::ok) {
            return std::string();
        }
        return out;
    }

    // Parse the ISO 8601 subset the formatter emits, plus offset timezones:
    //   YYYY-MM-DDTHH:MM:SS[.frac][Z | +/-HH:MM]
    // Fractional digits may be 1..9 and are kept verbatim as nanoseconds.
    // Reports overflow when the resulting instant leaves the int64 ns range,
    // invalid_number on any malformed text. Never throws for malformed input.
    static result<date_time> parse(string_view iso) {
        const std::size_t n = iso.size();
        if (n < 20) {
            return make_error<date_time>(error_code::invalid_number);
        }
        std::int64_t y = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const char c = iso[i];
            if (c < '0' || c > '9') {
                return make_error<date_time>(error_code::invalid_number);
            }
            y = y * 10 + (c - '0');
        }
        if (iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' ||
            iso[13] != ':' || iso[16] != ':') {
            return make_error<date_time>(error_code::invalid_number);
        }
        const int mo = read_two(iso, 5);
        const int d = read_two(iso, 8);
        const int hh = read_two(iso, 11);
        const int mi = read_two(iso, 14);
        const int ss = read_two(iso, 17);
        if (mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) ||
            hh < 0 || hh > 23 || mi < 0 || mi > 59 || ss < 0 || ss > 59) {
            return make_error<date_time>(error_code::invalid_number);
        }
        std::size_t i = 19;
        std::int64_t frac_ns = 0;
        if (i < n && iso[i] == '.') {
            ++i;
            std::size_t digits = 0;
            std::int64_t f = 0;
            while (i < n && iso[i] >= '0' && iso[i] <= '9') {
                if (digits < 9) {
                    f = f * 10 + (iso[i] - '0');
                    ++digits;
                } else {
                    ++digits;  // tenth+ digit: counted, then rejected below
                }
                ++i;
            }
            if (digits == 0 || digits > 9) {
                return make_error<date_time>(error_code::invalid_number);
            }
            std::int64_t scale = 1;
            for (std::size_t k = digits; k < 9; ++k) {
                scale *= 10;
            }
            frac_ns = f * scale;
        }
        // Timezone designator: Z or +/-HH:MM.
        if (i >= n) {
            return make_error<date_time>(error_code::invalid_number);
        }
        const char tz = iso[i++];
        std::int64_t offset_sec = 0;
        if (tz == 'Z' || tz == 'z') {
            // UTC, no offset.
        } else if (tz == '+' || tz == '-') {
            if (i + 5 > n) {
                return make_error<date_time>(error_code::invalid_number);
            }
            const int oh = read_two(iso, i);
            const int om = read_two(iso, i + 3);
            if (iso[i + 2] != ':' || oh < 0 || oh > 23 || om < 0 || om > 59) {
                return make_error<date_time>(error_code::invalid_number);
            }
            offset_sec = static_cast<std::int64_t>(oh) * 3600 +
                         static_cast<std::int64_t>(om) * 60;
            if (tz == '-') {
                offset_sec = -offset_sec;
            }
            i += 5;
        } else {
            return make_error<date_time>(error_code::invalid_number);
        }
        if (i != n) {
            return make_error<date_time>(error_code::invalid_number);
        }
        const std::int64_t days = days_from_civil(y, mo, d);
        const std::int64_t secs =
            days * 86400 + hh * 3600 + mi * 60 + ss - offset_sec;
        // The whole-second component must stay within one int64-nanosecond
        // lane: floor(-INT64_MIN ns / 1e9) == -9223372037, so the negative
        // lane gets the extra second. The fraction then decides whether the
        // exact boundary values fit (854775807 above, 145224192 below).
        if (secs > 9223372036LL || secs < -9223372037LL) {
            return make_error<date_time>(error_code::overflow);
        }
        if (secs == 9223372036LL && frac_ns > 854775807LL) {
            return make_error<date_time>(error_code::overflow);
        }
        if (secs == -9223372037LL && frac_ns < 145224192LL) {
            return make_error<date_time>(error_code::overflow);
        }
        return make_ok<date_time>(date_time(secs * 1000000000LL + frac_ns));
    }

    // Comparisons (nanosecond exact).
    friend bool operator==(const date_time& a, const date_time& b) noexcept {
        return a.ns_ == b.ns_;
    }
    friend bool operator!=(const date_time& a, const date_time& b) noexcept {
        return a.ns_ != b.ns_;
    }
    friend bool operator<(const date_time& a, const date_time& b) noexcept {
        return a.ns_ < b.ns_;
    }
    friend bool operator<=(const date_time& a, const date_time& b) noexcept {
        return a.ns_ <= b.ns_;
    }
    friend bool operator>(const date_time& a, const date_time& b) noexcept {
        return a.ns_ > b.ns_;
    }
    friend bool operator>=(const date_time& a, const date_time& b) noexcept {
        return a.ns_ >= b.ns_;
    }

    // Arithmetic in whole nanoseconds. Performed on unsigned counts so an
    // out-of-range operand deterministically wraps (defined behavior, never
    // signed overflow); callers wanting strict range semantics should use the
    // checked from_unix_* / epoch_ns_from_digits paths.
    friend date_time operator+(date_time t, std::int64_t ns) noexcept {
        t.ns_ = static_cast<std::int64_t>(static_cast<std::uint64_t>(t.ns_) +
                                          static_cast<std::uint64_t>(ns));
        return t;
    }
    friend date_time operator-(date_time t, std::int64_t ns) noexcept {
        t.ns_ = static_cast<std::int64_t>(static_cast<std::uint64_t>(t.ns_) -
                                          static_cast<std::uint64_t>(ns));
        return t;
    }
    // Nanosecond difference between two instants.
    friend std::int64_t operator-(const date_time& a,
                                  const date_time& b) noexcept {
        return static_cast<std::int64_t>(static_cast<std::uint64_t>(a.ns_) -
                                         static_cast<std::uint64_t>(b.ns_));
    }

private:
    // The civil-clock breakdown of the stored instant: year/month/day plus the
    // exact time-of-day. Sub-second leftovers use floor division toward
    // negative infinity (see the format() comment), so every field is in its
    // natural range (month 1..12, hour `0..23`, nanos 0..999999999) even for
    // pre-epoch instants.
    struct date_fields {
        std::int64_t year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
        std::int64_t nanos = 0;
    };

    date_fields decompose() const noexcept {
        date_fields f;
        const std::int64_t day_ns = 86400LL * 1000000000LL;
        std::int64_t days = ns_ / day_ns;
        std::int64_t rem = ns_ % day_ns;
        if (rem < 0) {
            --days;
            rem += day_ns;
        }
        // Civil calendar from the day count (Howard Hinnant's algorithm).
        std::int64_t z = days + 719468;
        const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const std::int64_t doe = z - era * 146097;
        const std::int64_t yoe =
            (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        std::int64_t y = yoe + era * 400;
        const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const std::int64_t mp = (5 * doy + 2) / 153;
        const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
        const std::int64_t mo = mp < 10 ? mp + 3 : mp - 9;
        y += (mo <= 2 ? 1 : 0);
        const std::int64_t sec = rem / 1000000000LL;
        const std::int64_t frac = rem % 1000000000LL;
        f.year = y;
        f.month = static_cast<int>(mo);
        f.day = static_cast<int>(d);
        f.hour = static_cast<int>(sec / 3600);
        f.minute = static_cast<int>((sec / 60) % 60);
        f.second = static_cast<int>(sec % 60);
        f.nanos = frac;
        return f;
    }

    // Floor division (divisors are always positive here).
    static std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept {
        const std::int64_t q = a / b;
        const std::int64_t r = a % b;
        return (r != 0 && r < 0) ? q - 1 : q;
    }

    static bool is_leap(std::int64_t y) noexcept {
        return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    }
    static int days_in_month(std::int64_t y, int m) noexcept {
        static const int kDays[12] = {31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31};
        if (m == 2 && is_leap(y)) {
            return 29;
        }
        return kDays[m - 1];
    }
    static std::int64_t days_from_civil(std::int64_t y, int m, int d) noexcept {
        y -= (m <= 2 ? 1 : 0);
        const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
        const std::int64_t yoe = y - era * 400;
        const std::int64_t doy =
            (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    // Two ASCII digits at pos; -1 when either byte is not a digit.
    static int read_two(string_view s, std::size_t pos) noexcept {
        if (pos + 1 >= s.size()) {
            return -1;
        }
        const char a = s[pos];
        const char b = s[pos + 1];
        if (a < '0' || a > '9' || b < '0' || b > '9') {
            return -1;
        }
        return (a - '0') * 10 + (b - '0');
    }

    static void append_two(std::string& out, std::int64_t v) {
        out.push_back(static_cast<char>('0' + v / 10));
        out.push_back(static_cast<char>('0' + v % 10));
    }
    // The %f directive: exactly nine zero-padded nanosecond digits.
    static void append_nanos9(std::string& out, std::int64_t v) {
        char b[9];
        for (int k = 8; k >= 0; --k) {
            b[k] = static_cast<char>('0' + v % 10);
            v /= 10;
        }
        out.append(b, 9);
    }
    // At least four digits, zero-padded; wider years print as-is.
    static void append_year(std::string& out, std::int64_t y) {
        std::uint64_t mag = y >= 0 ? static_cast<std::uint64_t>(y)
                                   : static_cast<std::uint64_t>(-(y + 1)) + 1u;
        if (y < 0) {
            out.push_back('-');
        }
        if (mag < 1000) {
            out.push_back('0');
        }
        if (mag < 100) {
            out.push_back('0');
        }
        if (mag < 10) {
            out.push_back('0');
        }
        char buf[20];
        std::size_t n = 0;
        do {
            buf[n++] = static_cast<char>('0' + mag % 10);
            mag /= 10;
        } while (mag != 0);
        while (n != 0) {
            out.push_back(buf[--n]);
        }
    }

    std::int64_t ns_;
};

// Type trait: true exactly for rtti::date_time. The convert dispatch uses it
// to route date_time to the scalar converter even if a user also registers the
// type with RTTI_DEFINE_TYPE.
template <typename T>
struct is_date_time : std::bool_constant<std::is_same<T, date_time>::value> {};

// Map an integer Unix epoch to nanoseconds by decimal digit count (spec
// §14.1): 10 digits = seconds, 13 = ms, 16 = us, 19/20 = ns. A leading sign is
// not counted. Zero maps to epoch zero (seconds). Unknown counts report
// invalid_number; a scaled product out of the int64 ns range reports overflow.
inline error_code epoch_ns_from_digits(std::int64_t value,
                                       std::int64_t& out) noexcept {
    if (value == 0) {
        out = 0;
        return error_code::ok;
    }
    const std::uint64_t mag = value < 0
                                  ? static_cast<std::uint64_t>(-(value + 1)) + 1u
                                  : static_cast<std::uint64_t>(value);
    std::size_t digits = 1;
    std::uint64_t m = mag;
    while (m >= 10) {
        m /= 10;
        ++digits;
    }
    // (negative lower bound, positive upper bound; both inclusive). The ns
    // lane spans the whole int64 range (INT64_MIN is a valid 19-digit ns
    // value), so its bounds are the int64 endpoints themselves; the scaled
    // lanes 10/13/16 digits stay within the symmetric -bound..+bound window
    // where value*factor cannot leave int64.
    std::int64_t factor = 1;
    std::int64_t lo = 0;
    std::int64_t hi = 0;
    switch (digits) {
        case 10:
            factor = 1000000000LL;
            lo = -9223372036LL;
            hi = 9223372036LL;
            break;
        case 13:
            factor = 1000000LL;
            lo = -9223372036854LL;
            hi = 9223372036854LL;
            break;
        case 16:
            factor = 1000LL;
            lo = -9223372036854775LL;
            hi = 9223372036854775LL;
            break;
        default:
            if (digits == 19 || digits == 20) {
                factor = 1;
                lo = (std::numeric_limits<std::int64_t>::min)();
                hi = (std::numeric_limits<std::int64_t>::max)();
            } else {
                return error_code::invalid_number;
            }
    }
    if (value < lo || value > hi) {
        return error_code::overflow;
    }
    out = value * factor;
    return error_code::ok;
}

} // namespace rtti