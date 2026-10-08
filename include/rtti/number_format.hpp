#pragma once
// Public string-formatting surface (spec §7): numeric, date, node-level and
// reflection-driven text formatting for RTTI.
//
// What lives here and in the static library:
//   * include/rtti/number_format.hpp (this header):
//       - the format_number<T> template shells (they only dispatch);
//       - the to_string<T>(v, format_spec) overload (thin wrapper, does not
//         replace or modify the existing to_string<T>(v) from to_string.hpp);
//       - the format_reflected<T>(obj, spec) and node_to_string(value_io*, spec)
//         declarations plus their generic implementations' lookup hooks.
//   * src/number_format.cpp:
//       - the non-template format_number cores that implement the format-spec
//         grammar (grouping / width / precision / base / alignment).
//   * src/format_reflected.cpp:
//       - node_to_string (tree walk over any value_io node) and the reflected
//         field-table formatter used by format_reflected<T>.
//
// Format-spec grammar (a Python-format-style subset, spec §7.2):
//     format_spec := [[fill]align] [sign] ['#'] ['0'] [width] [',']
//                    ['.' precision] [type]
//   fill       any single byte followed by an align byte.
//   align      '<' left, '>' right (default), '^' center, '=' pad after the
//              sign.
//   sign       '+' always print the sign, '-' sign only for negatives
//              (default), ' ' a space instead of a sign for non-negatives.
//   '#'        alternate form: '0b'/'0o'/'0x'/'0X' prefix for non-decimal
//              integer bases (no effect for decimal or reals).
//   '0'        zero-pad to `width` (equivalent to fill '0' with align '=').
//   width      minimum total width (decimal digits).
//   ','        thousands grouping; applies to the decimal integer part and to
//              fixed-notation reals (ignored for other bases / scientific).
//   precision  '.': integer = minimum digit count (zero padded); real = digits
//              after the point for f/e (default 6).
//   type       integer 'd' (decimal, default) 'b' 'o' 'x' 'X'; real 'f'/'F'
//              (fixed), 'e'/'E' (scientific), 'g'/'G' or empty (shortest
//              round-tripping text).
// Illegal specs report error_code::unexpected_token through the optional
// error_info out-parameter and the formatting function returns an empty
// string (spec §0 fail-safe, never a crash).
//
// date_time gains a strftime-style format(pattern) in <rtti/date_time.hpp>;
// to_string(v, spec) routes date_time through it.
//
// English comments only; C++17; no third-party dependencies.

#include <cstdint>
#include <type_traits>

#include <rtti/date_time.hpp>
#include <rtti/error.hpp>
#include <rtti/memory.hpp>
#include <rtti/reflection.hpp>
#include <rtti/to_string.hpp>

namespace rtti {

// --- Non-template cores (defined in src/number_format.cpp) -----------------
namespace detail {

// Format an unsigned integer magnitude with `spec` ("#,d", "08x", ...). On a
// malformed spec `err` (nullable) receives error_code::unexpected_token and an
// empty string is returned.
std::string format_number_unsigned(std::uint64_t value, string_view spec,
                                   error_info* err);

// Signed variant: a negative value gains a '-' before the digits (or before
// the zero-padding when zero-padding / align '=').
std::string format_number_signed(std::int64_t value, string_view spec,
                                 error_info* err);

// Real variant: shortest text by default, or fixed/scientific with precision
// when the spec requests it.
std::string format_number_real(double value, string_view spec,
                               error_info* err);

// Shared spec parser (also used by the reflected/node formatters). Returns ok
// and fills the fields when `spec` is a valid format spec for the family.
struct number_parts;
number_parts parse_number_spec(string_view spec, bool is_floating,
                               error_info* err);

// Reflection-driven formatter (defined in src/format_reflected.cpp): walks
// `fields` (the RTTI_DEFINE_TYPE field-accessor table) on `obj`, applies
// `spec` to every number and serializes the whole object to one compact line.
std::string reflected_fields_to_string(const field_access_base* const* fields,
                                       std::size_t count, const void* obj,
                                       string_view spec, error_info* err);

} // namespace detail

// Public number formatting (spec §7.2). The template overloads are thin shells
// over the non-template cores above, so the whole formatting engine lives in
// the static library.
template <typename T,
          std::enable_if_t<std::is_integral<T>::value &&
                               !std::is_same<T, bool>::value,
                           int> = 0>
std::string format_number(T value, string_view spec = string_view(),
                          error_info* err = nullptr) {
    if constexpr (std::is_signed<T>::value) {
        return detail::format_number_signed(static_cast<std::int64_t>(value),
                                            spec, err);
    } else {
        return detail::format_number_unsigned(
            static_cast<std::uint64_t>(value), spec, err);
    }
}

template <typename T,
          std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
std::string format_number(T value, string_view spec = string_view(),
                          error_info* err = nullptr) {
    return detail::format_number_real(static_cast<double>(value), spec, err);
}

// to_string<T>(v, format_spec): a thin overload that applies a format spec to
// the existing scalar conversion surface. It deliberately takes the spec as a
// required argument (no default), so the existing to_string<T>(v) overload is
// never shadowed or made ambiguous. Integers/floats go through format_number;
// date_time goes through its strftime-style format(pattern).
template <typename T,
          std::enable_if_t<(std::is_integral<T>::value &&
                            !std::is_same<T, bool>::value) ||
                               std::is_floating_point<T>::value ||
                               is_date_time<T>::value,
                           int> = 0>
std::string to_string(const T& v, string_view format_spec) {
    if constexpr (is_date_time<T>::value) {
        return v.format(format_spec);
    } else {
        return format_number(v, format_spec);
    }
}

// --- Node-level and reflected formatting (src/format_reflected.cpp) ---------

// Serialize any value_io node to a single compact line (spec §7.3): objects as
// {"k":v,...}, arrays as [a,b,...], booleans/strings/null in their compact
// forms and numbers through the format spec (empty spec = shortest texts).
// A null node argument reports no_such_field. Allocation failure may throw
// std::bad_alloc; no other failure throws.
std::string node_to_string(const value_io* node,
                           string_view spec = string_view(),
                           error_info* err = nullptr);

// Reflection-driven formatting (spec §7.4): walk the reflected field table of
// T (registered with RTTI_DEFINE_TYPE*) and render the object as a compact
// object line. The optional spec is applied to every numeric field value.
// A zero-field table renders as "{}"; a failed field read returns an empty
// string (allocation failure may throw std::bad_alloc; otherwise it never
// throws).
template <typename T>
std::string format_reflected(const T& obj, string_view spec = string_view(),
                             error_info* err = nullptr) {
    using U = typename std::decay<T>::type;
    return detail::reflected_fields_to_string(
        rtti::fields<U>(), rtti::field_count<U>(),
        static_cast<const void*>(&obj), spec, err);
}

} // namespace rtti