#pragma once
// Arbitrary-base integer <-> string conversion (spec §15).
// rtti::detail::to_base / from_base convert a uint64_t to/from a digit string
// in any base 2..64; to_base_signed / from_base_signed add a leading '-' sign
// for int64_t. The digit table is configurable (the default follows §15.2;
// base64-style tables such as RFC 4648 or URL-safe order are one custom-table
// call away). Overflow / illegal-character / invalid-base inputs return an
// error_code (spec §0) and never crash; to_base has no error_code channel so
// it returns an empty string on invalid bases instead.
//
// Digit table (default, spec §15.2):
//   base 2..36  : "0-9a-z"            (strtol-compatible lowercase set)
//   base 37..62 : "0-9a-zA-Z"         (62 symbols)
//   base 63..64 : the 62 symbols + '+' at index 62 and '/' at index 63
// The §15.2 "RFC 4648 base64 style" note governs the *extra* symbols (the
// base64 '+','/') rather than the leading order, so a single contiguous table
// keeps the explicitly enumerated per-range charsets of §15.2 while leaving
// base64's "A-Za-z0-9+/" ordering available through the digits extension
// point (kRfc4648Digits below).
//
// Case policy:
//   * base <= 36 parses letters case-insensitively (strtol behavior) when the
//     default table is used, so "FF" is 255 as in strtol base 16;
//   * base > 36 is case-sensitive (spec §15.2);
//   * an explicit case-insensitive flag resolves letters whose exact case is
//     absent from a custom table.
//
// Zero format dependency: this header only needs <rtti/error.hpp> from the
// reflect core. The value_io converter bridge that serializes an integer as a
// base-N string lives in <rtti/convert.hpp> (to_value_base / from_value_base),
// which is where value_io is defined.
// English comments only; C++17; no third-party dependencies.

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include <rtti/error.hpp>

namespace rtti {
namespace detail {

// Default digit table for bases 2..64 (spec §15.2). Index i is the symbol for
// digit value i, so the first `base` entries form the alphabet of that base.
const char* default_digits() noexcept;

// RFC 4648 base64 order ("A-Za-z0-9+/") as a ready custom table. This is the
// ordering the §15.2 note names; supply it explicitly to opt into it for any
// base up to 64.
const char* rfc4648_digits() noexcept;

// Number of symbols in a digits table; 0 when the pointer is null.
std::size_t digit_table_len(const char* digits) noexcept;

// True when the default table parses letters case-insensitively for `base`:
// strtol accepts both cases for bases 2..36, so the lowercase default table
// matches letters case-insensitively there. Custom tables default to strict
// case-sensitive parsing (callers opt into insensitivity explicitly).
bool default_case_insensitive(unsigned base) noexcept;

// Digit value of byte c within the first `base` symbols of `digits`, or -1
// when c is not one of them. When `insensitive` is set, a byte that has no
// exact match is retried in the opposite ASCII letter case (so 'A' resolves
// against a lowercase-only table, and vice versa).
int base_digit(char c, const char* digits, std::size_t base,
               bool insensitive) noexcept;

// Integer -> string in any base 2..64. `digits` is an optional symbol table
// with at least `base` symbols; it defaults to default_digits(). Invalid bases
// (0/1/>64) and undersized tables return an empty string -- to_base has no
// error_code channel, so an empty result is the documented failure signal.
std::string to_base(std::uint64_t value, unsigned base,
                    const char* digits = nullptr);

// Signed integer -> string with a leading '-' for negatives. Invalid bases
// propagate to_base's empty-string failure signal.
std::string to_base_signed(std::int64_t value, unsigned base);

// String -> integer in any base 2..64 with overflow detection. `digits`
// defaults to default_digits(); `case_insensitive` enables the swap-case
// fallback in base_digit. Invalid bases / empty input / illegal characters
// report invalid_number; a value past UINT64_MAX reports overflow. The
// accumulator check (acc > (UINT64_MAX - d) / base) is done before the
// multiply so an overflowing lane is detected instead of wrapped (spec §0).
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, const char* digits,
                     bool case_insensitive);

// Canonical 3-argument form: default table, strtol-style case policy.
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out);

// Default table with an explicit case-insensitivity flag.
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, bool case_insensitive);

// Custom table; case-sensitive unless the 5-argument form enables the flag.
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, const char* digits);

// String -> signed integer. Accepts an optional '+'/'-' prefix (strtol-like);
// a bare or doubled sign is invalid. The magnitude is parsed through
// from_base; the signed lane rejects magnitudes above INT64_MAX (positive) or
// above 2^63 (negative) with overflow. INT64_MIN is representable exactly
// through its 2^63 magnitude.
error_code from_base_signed(std::string_view s, unsigned base,
                            std::int64_t& out);

} // namespace detail
} // namespace rtti