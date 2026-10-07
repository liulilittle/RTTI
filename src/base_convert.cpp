// Arbitrary-base integer <-> string conversion (spec §15) implementation.
// The non-template definitions declared by <rtti/base_convert.hpp>: the digit
// table accessors, the to_base / from_base families (bases 2..64), and the
// signed convenience wrappers. Behavior is identical to the former inline
// header definitions.
// English comments only; C++17; no third-party dependencies.

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include <rtti/base_convert.hpp>

namespace rtti {
namespace detail {

const char* default_digits() noexcept {
    static const char kDigits[65] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+/";
    return kDigits;
}

const char* rfc4648_digits() noexcept {
    static const char kRfc[65] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    return kRfc;
}

std::size_t digit_table_len(const char* digits) noexcept {
    if (digits == nullptr) {
        return 0;
    }
    return std::strlen(digits);
}

bool default_case_insensitive(unsigned base) noexcept {
    return base <= 36;
}

int base_digit(char c, const char* digits, std::size_t base,
               bool insensitive) noexcept {
    for (std::size_t i = 0; i < base; ++i) {
        if (digits[i] == c) {
            return static_cast<int>(i);
        }
    }
    if (insensitive) {
        const char alt =
            (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A')
            : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a')
                                     : c;
        if (alt != c) {
            for (std::size_t i = 0; i < base; ++i) {
                if (digits[i] == alt) {
                    return static_cast<int>(i);
                }
            }
        }
    }
    return -1;
}

std::string to_base(std::uint64_t value, unsigned base, const char* digits) {
    if (base < 2 || base > 64) {
        return std::string();
    }
    if (digits == nullptr) {
        digits = default_digits();
    }
    if (digit_table_len(digits) < base) {
        return std::string();
    }
    if (value == 0) {
        return std::string(1, digits[0]);
    }
    // 2^64-1 needs at most 64 digits (base 2); one more byte is slack.
    char buf[65];
    std::size_t n = 0;
    std::uint64_t v = value;
    while (v != 0) {
        buf[n++] = digits[v % base];
        v /= base;
    }
    std::string out;
    out.reserve(n);
    while (n != 0) {
        out.push_back(buf[--n]);
    }
    return out;
}

std::string to_base_signed(std::int64_t value, unsigned base) {
    if (value >= 0) {
        return to_base(static_cast<std::uint64_t>(value), base);
    }
    // Magnitude of INT64_MIN (2^63) fits uint64; -(value+1)+1 stays in int64
    // while yielding the two's-complement magnitude (same idiom as date_time).
    const std::uint64_t mag =
        static_cast<std::uint64_t>(-(value + 1)) + 1u;
    std::string out = to_base(mag, base);
    if (out.empty()) {
        return out;
    }
    out.insert(out.begin(), '-');
    return out;
}

error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, const char* digits,
                     bool case_insensitive) {
    out = 0;
    if (base < 2 || base > 64) {
        return error_code::invalid_number;
    }
    if (digits == nullptr) {
        digits = default_digits();
    }
    if (digit_table_len(digits) < base) {
        return error_code::invalid_number;
    }
    if (s.empty()) {
        return error_code::invalid_number;
    }
    const std::uint64_t max = (std::numeric_limits<std::uint64_t>::max)();
    std::uint64_t acc = 0;
    for (char c : s) {
        const int d = base_digit(c, digits, base, case_insensitive);
        if (d < 0) {
            return error_code::invalid_number;
        }
        const std::uint64_t dv = static_cast<std::uint64_t>(d);
        if (acc > (max - dv) / base) {
            return error_code::overflow;
        }
        acc = acc * base + dv;
    }
    out = acc;
    return error_code::ok;
}

error_code from_base(std::string_view s, unsigned base, std::uint64_t& out) {
    return from_base(s, base, out, nullptr, default_case_insensitive(base));
}

error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, bool case_insensitive) {
    return from_base(s, base, out, nullptr, case_insensitive);
}

error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, const char* digits) {
    return from_base(s, base, out, digits, false);
}

error_code from_base_signed(std::string_view s, unsigned base,
                            std::int64_t& out) {
    out = 0;
    if (base < 2 || base > 64) {
        return error_code::invalid_number;
    }
    if (s.empty()) {
        return error_code::invalid_number;
    }
    bool neg = false;
    std::string_view mag = s;
    if (mag.front() == '-') {
        neg = true;
        mag = mag.substr(1);
        if (mag.empty()) {
            return error_code::invalid_number;
        }
    } else if (mag.front() == '+') {
        mag = mag.substr(1);
        if (mag.empty()) {
            return error_code::invalid_number;
        }
    }
    std::uint64_t m = 0;
    const error_code e = from_base(mag, base, m);
    if (e != error_code::ok) {
        return e;
    }
    if (neg) {
        const std::uint64_t kMinMag = std::uint64_t(1) << 63;
        if (m > kMinMag) {
            return error_code::overflow;
        }
        if (m == kMinMag) {
            out = (std::numeric_limits<std::int64_t>::min)();
            return error_code::ok;
        }
        out = -static_cast<std::int64_t>(m);
        return error_code::ok;
    }
    if (m > static_cast<std::uint64_t>(
               (std::numeric_limits<std::int64_t>::max)())) {
        return error_code::overflow;
    }
    out = static_cast<std::int64_t>(m);
    return error_code::ok;
}

} // namespace detail
} // namespace rtti