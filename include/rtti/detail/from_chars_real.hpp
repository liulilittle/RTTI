#pragma once
// Portable floating-point text->real parsing front-end (requirement D).
//
// C++17 <charconv> provides floating-point std::from_chars overloads on the
// MSVC STL and libstdc++ (GCC >= 11), but libc++ (the Android NDK runtime)
// only shipped the floating-point FROM overload in LLVM 19 -- NDK r26 (LLVM
// 17) still lacks it, so a direct std::from_chars(first, last, double&) call
// fails to compile on ARM/ARM64 NDK targets. This header routes every
// floating-point text->real conversion through a small dispatcher:
//   * has_fp_from_chars<T> probes, at compile time, whether the standard
//     library implements the floating-point overload (SFINAE on the call
//     expression; the integral-only overloads are constrained to is_integral
//     and the deleted bool overload cannot bind a floating-point lvalue, so
//     the probe is false on libc++ < 19 and true elsewhere).
//   * from_chars_real<T>(first, last, out) then either delegates to
//     std::from_chars (MSVC/libstdc++/modern libc++) or falls back to a
//     strtod-based parser over a NUL-terminated copy of the span.
//
// Fallback contract (mirrors the std::from_chars semantics the call sites
// rely on):
//   * invalid_argument / ptr == first when the span is empty or its first
//     byte is not a decimal-number start (digit, sign or '.'). Leading
//     whitespace is therefore rejected (strtod would otherwise skip it).
//   * result_out_of_range when errno == ERANGE after strtod (magnitude not
//     representable); the callers only test ec for this branch.
//   * ok with out set on a successful parse; ptr points one past the last
//     consumed byte, and the callers enforce full consumption (ptr == end)
//     themselves, exactly as they do for std::from_chars. errno is not
//     modified on the success path.
//
// Locale note: std::strtod honours LC_NUMERIC while from_chars is
// locale-agnostic. Under a comma-decimal locale "1.5" would parse as "1" and
// the caller's ptr == end check would reject the token -- a safe parse
// failure, never a wrong value. Android/bionic defaults to the "C" locale,
// so '.' is the separator in practice. The deliberately minimal fix does not
// special-case locales further.
//
// inf/-inf/nan tokens and hex floats are not handled here: every call site
// pre-screens the deterministic non-finite tokens and feeds validated decimal
// number spans only, so the fallback never sees them.
//
// English comments only; C++17; no third-party dependencies.

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <string>
#include <type_traits>

namespace rtti {
namespace detail {

// Compile-time probe: does std::from_chars accept a floating-point T?
template <typename T, typename = void>
struct has_fp_from_chars : std::false_type {};
template <typename T>
struct has_fp_from_chars<T, std::void_t<decltype(std::from_chars(
                                std::declval<const char*>(),
                                std::declval<const char*>(),
                                std::declval<T&>()))>> : std::true_type {};

// Byte that may legally start a decimal number token. Mirrors the
// std::from_chars floating-point pattern's first-character set and keeps the
// strtod fallback from accepting leading whitespace.
inline bool fp_token_start(char c) noexcept {
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
}

// strtod fallback for standard libraries without the floating-point
// from_chars overload (libc++ < LLVM 19, i.e. the Android NDK r26 runtime).
template <typename T>
inline std::from_chars_result from_chars_real_impl(const char* first,
                                                   const char* last, T& out,
                                                   std::false_type) {
    std::from_chars_result r;
    if (first == last || !fp_token_start(*first)) {
        r.ec = std::errc::invalid_argument;
        r.ptr = first;
        return r;
    }
    // strtod needs a NUL-terminated buffer; the input is a span.
    const std::string tmp(first, last);
    char* endp = nullptr;
    errno = 0;
    const long double v = std::strtod(tmp.c_str(), &endp);
    const auto consumed =
        static_cast<std::size_t>(endp - tmp.c_str());
    if (endp == tmp.c_str()) {
        r.ec = std::errc::invalid_argument;
        r.ptr = first;
    } else if (errno == ERANGE) {
        // Magnitude out of representable range (overflow to +/-inf or
        // underflow to 0). Callers check only ec for this branch.
        r.ec = std::errc::result_out_of_range;
        r.ptr = last;
    } else {
        r.ec = std::errc();
        out = static_cast<T>(v);
        r.ptr = first + static_cast<std::ptrdiff_t>(consumed);
    }
    return r;
}

// Standard-library path: the floating-point overload exists, delegate.
template <typename T>
inline std::from_chars_result from_chars_real_impl(const char* first,
                                                   const char* last, T& out,
                                                   std::true_type) {
    return std::from_chars(first, last, out);
}

// Public front-end used by the real-number conversion call sites.
template <typename T>
inline std::from_chars_result from_chars_real(const char* first,
                                              const char* last, T& out) {
    return from_chars_real_impl(first, last, out,
                                has_fp_from_chars<T>{});
}

} // namespace detail
} // namespace rtti