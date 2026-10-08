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
    date_time() noexcept;
    // Construct from an exact epoch nanosecond count.
    explicit date_time(std::int64_t epoch_ns) noexcept;

    // Coarser Unix epoch factories. The scaled nanosecond product is
    // range-checked; a value out of the int64 range reports overflow.
    static result<date_time> from_unix_seconds(std::int64_t seconds);
    static result<date_time> from_unix_ms(std::int64_t ms);
    static result<date_time> from_unix_us(std::int64_t us);
    static result<date_time> from_unix_ns(std::int64_t ns);

    // Unit accessors. Sub-second counts use floor division toward negative
    // infinity, so a pre-epoch instant with a fractional part maps to the
    // whole second that actually contains it. None of these can overflow the
    // int64 output (the divisor is at least 1000); they return error_code::ok.
    error_code to_unix_seconds(std::int64_t& out) const noexcept;
    error_code to_unix_ms(std::int64_t& out) const noexcept;
    error_code to_unix_us(std::int64_t& out) const noexcept;
    error_code to_unix_ns(std::int64_t& out) const noexcept;

    // Raw epoch nanoseconds.
    std::int64_t epoch_ns() const noexcept;

    // Canonical ISO 8601 UTC text, exact round-trip: whole seconds format as
    // "YYYY-MM-DDTHH:MM:SSZ"; a nonzero sub-second part appends the shortest
    // ".fff"/".ffffff"/".fffffffff" fraction that carries the exact value.
    std::string format() const;
    // Alias of format().
    std::string to_string() const;

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
    error_code format(string_view pattern, std::string& out) const;
    // Convenience overload: the pattern output, or an empty string when the
    // pattern contains an unsupported directive (the error code is available
    // through the out-parameter overload above).
    std::string format(string_view pattern) const;

    // Parse the ISO 8601 subset the formatter emits, plus offset timezones:
    //   YYYY-MM-DDTHH:MM:SS[.frac][Z | +/-HH:MM]
    // Fractional digits may be 1..9 and are kept verbatim as nanoseconds.
    // Reports overflow when the resulting instant leaves the int64 ns range,
    // invalid_number on any malformed text. Never throws for malformed input.
    static result<date_time> parse(string_view iso);

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

    date_fields decompose() const noexcept;

    // Floor division (divisors are always positive here).
    static std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept;

    static bool is_leap(std::int64_t y) noexcept;
    static int days_in_month(std::int64_t y, int m) noexcept;
    static std::int64_t days_from_civil(std::int64_t y, int m, int d) noexcept;

    // Two ASCII digits at pos; -1 when either byte is not a digit.
    static int read_two(string_view s, std::size_t pos) noexcept;

    static void append_two(std::string& out, std::int64_t v);
    // The %f directive: exactly nine zero-padded nanosecond digits.
    static void append_nanos9(std::string& out, std::int64_t v);
    // At least four digits, zero-padded; wider years print as-is.
    static void append_year(std::string& out, std::int64_t y);

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