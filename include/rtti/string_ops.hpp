#pragma once
// rtti::str - full-type string conversion plus a string-operation toolkit
// (spec 7.6 / Task 8a).
//
// Two layers:
//
//   1. Conversion shells. rtti::str::to_string / rtti::str::from_string are
//      using-aliases of the existing unified rtti::to_string<T> /
//      rtti::from_string<T> layer (to_string.hpp), so the whole supported type
//      surface - scalars, registered enums, date_time, containers, optional,
//      variant, tuple, pair, array, vector<bool>, native arrays, reflected
//      structs - converts through the same implementation with zero drift.
//      Float/double text comes from the shared format_real base already used
//      everywhere. rtti::str::to_base / from_base (and signed twins) bridge the
//      existing arbitrary-base helpers (bases 2..64) unchanged.
//
//   2. String operations (non-template, implemented in src/string_ops.cpp):
//      split / join, trim / ltrim / rtrim, to_lower / to_upper,
//      starts_with / ends_with, replace / replace_all, repeat,
//      pad_left / pad_right, reverse, quote / unquote, escape / unescape.
//      Pure string transforms are noexcept; parsing (unquote / unescape /
//      from_base / from_string) reports errors through error_code and never
//      crashes (spec 0 fail-safe).
//
// Naming: the alias imports live in namespace rtti::str, not rtti, so nothing
// is redefined or ambiguated against the existing rtti::to_string entry point;
// "rtti::str::to_string" *is* rtti::to_string (same entity, brought in by a
// using-declaration).
//
// English comments only; C++17; no third-party dependencies; header + one cpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rtti/error.hpp>
#include <rtti/to_string.hpp>

namespace rtti {
namespace str {

// ---------------------------------------------------------------------------
// Conversion: same entities as the existing unified layer (using-declarations).
// ---------------------------------------------------------------------------

using rtti::from_string;
using rtti::to_string;

// Arbitrary-base integer bridge (bases 2..64), forwarding to the existing
// to_string_base / from_string_base family. Invalid bases / illegal digits /
// overflow report empty-string or error_code exactly as the existing API.
std::string to_base(std::uint64_t value, unsigned base,
                    const char* digits = nullptr);
std::optional<std::uint64_t> from_base(string_view s, unsigned base);
error_code from_base(string_view s, unsigned base, std::uint64_t& out) noexcept;

std::string to_base_signed(std::int64_t value, unsigned base);
std::optional<std::int64_t> from_base_signed(string_view s, unsigned base);
error_code from_base_signed(string_view s, unsigned base,
                            std::int64_t& out) noexcept;

// ---------------------------------------------------------------------------
// String operations (non-template; definitions in src/string_ops.cpp).
// ---------------------------------------------------------------------------

// Split into tokens on every occurrence of sep. Empty input yields an empty
// vector; leading/trailing separators yield empty edge tokens, so
// split("a,b,,c", ',') == {"a","b","","c"}. An empty string separator treats
// the whole input as one token. noexcept; allocates.
std::vector<std::string> split(string_view s, char sep) noexcept;
std::vector<std::string> split(string_view s, string_view sep) noexcept;

// Join parts with sep between them; an empty part list joins to "".
std::string join(const std::vector<std::string>& parts, string_view sep) noexcept;

// Trim leading/trailing occurrences of chars (default: space, \t, \r, \n)
// from the ends. An empty chars set leaves the input unchanged.
std::string trim(string_view s, string_view chars = " \t\r\n") noexcept;
std::string ltrim(string_view s, string_view chars = " \t\r\n") noexcept;
std::string rtrim(string_view s, string_view chars = " \t\r\n") noexcept;

// ASCII case folding, byte-wise; non-ASCII bytes pass through untouched.
std::string to_lower(string_view s) noexcept;
std::string to_upper(string_view s) noexcept;

// Prefix/suffix tests. Both return true for an empty prefix/suffix.
bool starts_with(string_view s, string_view prefix) noexcept;
bool ends_with(string_view s, string_view suffix) noexcept;

// replace: the first occurrence of from; replace_all: every non-overlapping
// occurrence. An empty `from` returns the input unchanged (never an infinite
// loop). An empty `to` removes the matched text.
std::string replace(string_view s, string_view from, string_view to) noexcept;
std::string replace_all(string_view s, string_view from, string_view to) noexcept;

// Repeat s count times; count == 0 yields "" (an oversized count that cannot
// be represented fails-safe to an empty string).
std::string repeat(string_view s, std::size_t count) noexcept;

// Pad with fill up to width; input at or above width is returned unchanged.
std::string pad_left(string_view s, std::size_t width, char fill = ' ') noexcept;
std::string pad_right(string_view s, std::size_t width, char fill = ' ') noexcept;

// Reverse byte order.
std::string reverse(string_view s) noexcept;

// quote wraps s in double quotes and escapes the interior JSON/C-style, so
// unquote(quote(s)) == s always round-trips. unquote of a string that does not
// begin with '"' is kind_mismatch; one that does not end with '"' is
// unterminated_string; interior escape errors propagate from unescape. The
// string overload returns "" on any parse failure.
std::string quote(string_view s) noexcept;
std::string unquote(string_view s) noexcept;
error_code unquote(string_view s, std::string& out) noexcept;

// JSON/C-style escape / unescape. escape maps " \ \b \f \n \r \t and every
// control byte below 0x20 (\u00XX; also decodes surrogate pairs). A trailing
// lone backslash is unterminated_string, an unknown escape is
// unexpected_token, and a malformed \uXXXX is invalid_utf8. The string
// overload of unescape returns "" on failure.
std::string escape(string_view s) noexcept;
std::string unescape(string_view s) noexcept;
error_code unescape(string_view s, std::string& out) noexcept;

} // namespace str
} // namespace rtti