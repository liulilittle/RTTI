// rtti::date_time implementation. The non-template member definitions declared
// by <rtti/date_time.hpp>: constructors, the from_unix_* factories, the
// to_unix_* floor-division accessors, the canonical ISO 8601 format() and the
// strftime-style pattern overload, parse() with its exact overflow lanes, and
// the private civil-clock decompose() / digit-helper helpers. Behavior is
// identical to the former inline header definitions.
// English comments only; C++17; no third-party dependencies.

#include <cstddef>
#include <cstdint>
#include <string>

#include <rtti/date_time.hpp>

namespace rtti {

date_time::date_time() noexcept : ns_(0) {}

date_time::date_time(std::int64_t epoch_ns) noexcept : ns_(epoch_ns) {}

result<date_time> date_time::from_unix_seconds(std::int64_t seconds) {
    if (seconds > 9223372036LL || seconds < -9223372036LL) {
        return make_error<date_time>(error_code::overflow);
    }
    return make_ok<date_time>(date_time(seconds * 1000000000LL));
}

result<date_time> date_time::from_unix_ms(std::int64_t ms) {
    if (ms > 9223372036854LL || ms < -9223372036854LL) {
        return make_error<date_time>(error_code::overflow);
    }
    return make_ok<date_time>(date_time(ms * 1000000LL));
}

result<date_time> date_time::from_unix_us(std::int64_t us) {
    if (us > 9223372036854775LL || us < -9223372036854775LL) {
        return make_error<date_time>(error_code::overflow);
    }
    return make_ok<date_time>(date_time(us * 1000LL));
}

result<date_time> date_time::from_unix_ns(std::int64_t ns) {
    return make_ok<date_time>(date_time(ns));
}

error_code date_time::to_unix_seconds(std::int64_t& out) const noexcept {
    out = floor_div(ns_, 1000000000LL);
    return error_code::ok;
}

error_code date_time::to_unix_ms(std::int64_t& out) const noexcept {
    out = floor_div(ns_, 1000000LL);
    return error_code::ok;
}

error_code date_time::to_unix_us(std::int64_t& out) const noexcept {
    out = floor_div(ns_, 1000LL);
    return error_code::ok;
}

error_code date_time::to_unix_ns(std::int64_t& out) const noexcept {
    out = ns_;
    return error_code::ok;
}

std::int64_t date_time::epoch_ns() const noexcept { return ns_; }

std::string date_time::format() const {
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

std::string date_time::to_string() const { return format(); }

error_code date_time::format(string_view pattern, std::string& out) const {
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

std::string date_time::format(string_view pattern) const {
    std::string out;
    if (format(pattern, out) != error_code::ok) {
        return std::string();
    }
    return out;
}

result<date_time> date_time::parse(string_view iso) {
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
    // Compose the nanosecond lane without overflowing the intermediate
    // seconds term. For secs == -9223372037 (the one value whose bare
    // product -9223372037*1e9 crosses INT64_MIN), fold a nanosecond into the
    // multiplier: (secs+1)*1e9 is exactly -9223372036000000000, and the
    // guarded frac_ns (>= 145224192) keeps (1e9 - frac_ns) within 854775808,
    // so the difference stays inside [INT64_MIN, INT64_MIN] boundaries.
    std::int64_t ns = 0;
    if (secs == -9223372037LL) {
        ns = (secs + 1) * 1000000000LL - (1000000000LL - frac_ns);
    } else {
        ns = secs * 1000000000LL + frac_ns;
    }
    return make_ok<date_time>(date_time(ns));
}

date_time::date_fields date_time::decompose() const noexcept {
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

std::int64_t date_time::floor_div(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t q = a / b;
    const std::int64_t r = a % b;
    return (r != 0 && r < 0) ? q - 1 : q;
}

bool date_time::is_leap(std::int64_t y) noexcept {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int date_time::days_in_month(std::int64_t y, int m) noexcept {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap(y)) {
        return 29;
    }
    return kDays[m - 1];
}

std::int64_t date_time::days_from_civil(std::int64_t y, int m, int d) noexcept {
    y -= (m <= 2 ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy =
        (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int date_time::read_two(string_view s, std::size_t pos) noexcept {
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

void date_time::append_two(std::string& out, std::int64_t v) {
    out.push_back(static_cast<char>('0' + v / 10));
    out.push_back(static_cast<char>('0' + v % 10));
}

void date_time::append_nanos9(std::string& out, std::int64_t v) {
    char b[9];
    for (int k = 8; k >= 0; --k) {
        b[k] = static_cast<char>('0' + v % 10);
        v /= 10;
    }
    out.append(b, 9);
}

void date_time::append_year(std::string& out, std::int64_t y) {
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

} // namespace rtti