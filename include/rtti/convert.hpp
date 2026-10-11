#pragma once
// Format-agnostic conversion for RTTI.
// value_io is the abstract source/sink that the format adapters
// (JSON/XML/INI/TOML/proto/flat/text_node) implement. from_value/to_value
// route scalars, strings, and the four container families (vector/list/map/
// set + unordered twins) with transparent allocators/comparators; reflective
// structs walk their field tables and custom converters plug in via ADL.
// English comments only; C++17; no third-party dependencies.

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <rtti/base_convert.hpp>
#include <rtti/date_time.hpp>
#include <rtti/error.hpp>
#include <rtti/naming.hpp>
#include <rtti/reflection.hpp>

#include <rtti/detail/from_chars_real.hpp>

namespace rtti {

// value_io (the abstract source/sink tree interface) is defined in
// <rtti/reflection.hpp> so the reflect-core accessors can call into it.
// convert.hpp implements the converters that consume it.

// --- Decomposition traits ---------------------------------------------------
// The container shape traits (is_sequence / is_associative / is_map / is_set /
// is_queue / is_stack / is_priority_queue), the key_traits helper and
// is_reflected are core type features and live in <rtti/reflection.hpp>;
// convert.hpp consumes them through its #include.

// --- Scalar converter -------------------------------------------------------

namespace detail {

// True for plain char. Spec §23.1 treats char as an ASCII byte (0..255) when
// converting between char and integers (distinct from signed char / unsigned
// char, which keep their own numeric_limits lanes). Serialization emits the
// byte value so an int -> char -> int round trip is exact for the full byte
// lane.
template <typename T>
struct is_plain_char : std::bool_constant<std::is_same<T, char>::value> {};

// Range-checked int64 -> integral T (spec §23.1 integer family). Overflow
// returns error_code::overflow and the target is never modified on failure.
template <typename T>
inline error_code scalar_int64_to_integral(std::int64_t v, T& out) {
    if constexpr (is_plain_char<T>::value) {
        if (v < 0 ||
            static_cast<std::uint64_t>(v) >
                static_cast<std::uint64_t>(
                    (std::numeric_limits<unsigned char>::max)())) {
            return error_code::overflow;
        }
        out = static_cast<T>(static_cast<unsigned char>(v));
        return error_code::ok;
    } else if constexpr (std::is_unsigned<T>::value) {
        if (v < 0 ||
            static_cast<std::uint64_t>(v) >
                static_cast<std::uint64_t>((std::numeric_limits<T>::max)())) {
            return error_code::overflow;
        }
        out = static_cast<T>(v);
        return error_code::ok;
    } else {
        if (v < static_cast<std::int64_t>((std::numeric_limits<T>::min)()) ||
            v > static_cast<std::int64_t>((std::numeric_limits<T>::max)())) {
            return error_code::overflow;
        }
        out = static_cast<T>(v);
        return error_code::ok;
    }
}

// Range-checked uint64 -> integral T. Used by the exact integer-text lane so
// UINT64_MAX round trips without double precision loss.
template <typename T>
inline error_code scalar_uint64_to_integral(std::uint64_t v, T& out) {
    if constexpr (is_plain_char<T>::value) {
        if (v > static_cast<std::uint64_t>(255u)) {
            return error_code::overflow;
        }
        out = static_cast<T>(static_cast<unsigned char>(v));
        return error_code::ok;
    } else if constexpr (std::is_unsigned<T>::value) {
        if (v > static_cast<std::uint64_t>((std::numeric_limits<T>::max)())) {
            return error_code::overflow;
        }
        out = static_cast<T>(v);
        return error_code::ok;
    } else {
        if (v > static_cast<std::uint64_t>((std::numeric_limits<T>::max)())) {
            return error_code::overflow;
        }
        out = static_cast<T>(v);
        return error_code::ok;
    }
}

// Real -> integral T (spec §23.1): truncate toward zero, then range check.
// NaN/infinities and out-of-range values are overflow; never UB.
template <typename T>
inline error_code scalar_real_to_integral(double v, T& out) {
    if (!std::isfinite(v)) {
        return error_code::overflow;
    }
    const double t = std::trunc(v);
    if constexpr (is_plain_char<T>::value) {
        if (t < 0.0 || t > 255.0) {
            return error_code::overflow;
        }
        out = static_cast<T>(static_cast<unsigned char>(t));
        return error_code::ok;
    } else if constexpr (std::is_unsigned<T>::value) {
        if (t < 0.0) {
            return error_code::overflow;
        }
        if constexpr (sizeof(T) == 8) {
            // static_cast<double>(uint64 max) rounds up to 2^64; any double
            // at or above 2^64 is out of range.
            if (t >= 18446744073709551616.0) {
                return error_code::overflow;
            }
        } else if (t > static_cast<double>((std::numeric_limits<T>::max)())) {
            return error_code::overflow;
        }
        out = static_cast<T>(t);
        return error_code::ok;
    } else {
        if constexpr (sizeof(T) == 8) {
            // static_cast<double>(int64 max) rounds up to 2^63: 2^63 itself
            // is out of range while int64 min (-2^63) is exactly representable.
            if (t < -9223372036854775808.0 || t >= 9223372036854775808.0) {
                return error_code::overflow;
            }
        } else if (t < static_cast<double>((std::numeric_limits<T>::min)()) ||
                   t > static_cast<double>((std::numeric_limits<T>::max)())) {
            return error_code::overflow;
        }
        out = static_cast<T>(t);
        return error_code::ok;
    }
}

// Text -> integral T (spec §23.1 "string -> number"). The whole text must be
// a number: non-numeric text is kind_mismatch, an out-of-range value is
// overflow. Integer text is read exactly through the uint64 then int64 lanes
// (so UINT64_MAX does not lose precision in a double); fractional/exponent
// text goes through the truncated real path.
template <typename T>
inline error_code scalar_text_to_integral(string_view s, T& out) {
    if (s.empty()) {
        return error_code::kind_mismatch;
    }
    std::uint64_t uv = 0;
    const auto ures = std::from_chars(s.data(), s.data() + s.size(), uv);
    if (ures.ec == std::errc() && ures.ptr == s.data() + s.size()) {
        return scalar_uint64_to_integral<T>(uv, out);
    }
    std::int64_t iv = 0;
    const auto ires = std::from_chars(s.data(), s.data() + s.size(), iv);
    if (ires.ec == std::errc() && ires.ptr == s.data() + s.size()) {
        return scalar_int64_to_integral<T>(iv, out);
    }
    double dv = 0.0;
    const auto rres = detail::from_chars_real(s.data(), s.data() + s.size(), dv);
    if (rres.ec == std::errc::result_out_of_range) {
        return error_code::overflow;
    }
    if (rres.ec != std::errc() || rres.ptr != s.data() + s.size()) {
        return error_code::kind_mismatch;
    }
    return scalar_real_to_integral<T>(dv, out);
}

// Text -> floating T (spec §23.1). The deterministic inf/-inf/nan tokens the
// library itself serializes round trip; other non-numeric text is
// kind_mismatch, an out-of-range magnitude is overflow.
template <typename T>
inline error_code scalar_text_to_real(string_view s, T& out) {
    if (s == "inf" || s == "infinity") {
        out = std::numeric_limits<T>::infinity();
        return error_code::ok;
    }
    if (s == "-inf" || s == "-infinity") {
        out = -std::numeric_limits<T>::infinity();
        return error_code::ok;
    }
    if (s == "nan") {
        out = std::numeric_limits<T>::quiet_NaN();
        return error_code::ok;
    }
    const auto res = detail::from_chars_real(s.data(), s.data() + s.size(), out);
    if (res.ec == std::errc::result_out_of_range) {
        return error_code::overflow;
    }
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
        return error_code::kind_mismatch;
    }
    return error_code::ok;
}

// Shortest round-tripping real text for the scalar string converter. Mirrors
// detail::format_real (defined later in this header) so a real source renders
// the same way everywhere.
inline void scalar_format_real(double value, std::string& out) {
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            out += "nan";
        } else {
            out += std::signbit(value) ? "-inf" : "inf";
        }
        return;
    }
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), value);
    const char* const first = buf;
    const char* const last = res.ptr;
    out.append(first, static_cast<std::size_t>(last - first));
    // Inspect only the token just written: an earlier scalar's '.' in a shared
    // stream must not suppress this token's ".0".
    bool has_point = false;
    for (const char* p = first; p != last; ++p) {
        if (*p == '.' || *p == 'e' || *p == 'E') {
            has_point = true;
            break;
        }
    }
    if (!has_point) {
        out += ".0";
    }
}

// Integral target (spec §23.1): try the source lanes in order and apply the
// matching conversion rule. Range violations never wrap the target.
template <typename T>
inline error_code scalar_integral_from(const value_io* src, T& out) {
    std::int64_t iv = 0;
    if (src->as_integer(iv)) {
        return scalar_int64_to_integral<T>(iv, out);
    }
    double dv = 0.0;
    if (src->as_real(dv)) {
        return scalar_real_to_integral<T>(dv, out);
    }
    string_view sv;
    if (src->as_string(sv)) {
        return scalar_text_to_integral<T>(sv, out);
    }
    bool bv = false;
    if (src->as_bool(bv)) {
        out = static_cast<T>(bv ? 1 : 0);
        return error_code::ok;
    }
    return error_code::kind_mismatch;
}

// Floating target (spec §23.1): real lane directly, integer lane with IEEE
// precision loss accepted, text lane parsed. NaN/infinity only reach an
// integer target through a real/text lane and are reported as overflow there.
template <typename T>
inline error_code scalar_real_from(const value_io* src, T& out) {
    double dv = 0.0;
    if (src->as_real(dv)) {
        out = static_cast<T>(dv);
        return error_code::ok;
    }
    std::int64_t iv = 0;
    if (src->as_integer(iv)) {
        out = static_cast<T>(iv);
        return error_code::ok;
    }
    string_view sv;
    if (src->as_string(sv)) {
        return scalar_text_to_real<T>(sv, out);
    }
    return error_code::kind_mismatch;
}

} // namespace detail

template <typename T, typename = void>
struct converter {
    static error_code from(const value_io* src, T& out) {
        if constexpr (std::is_same<T, bool>::value) {
            bool v = false;
            if (src->as_bool(v)) {
                out = v;
                return error_code::ok;
            }
            std::int64_t iv = 0;
            if (src->as_integer(iv)) {
                // int <-> bool (spec §23.1): 0 -> false, any nonzero -> true.
                out = (iv != 0);
                return error_code::ok;
            }
            return error_code::kind_mismatch;
        } else if constexpr (std::is_integral<T>::value) {
            return detail::scalar_integral_from<T>(src, out);
        } else if constexpr (std::is_floating_point<T>::value) {
            return detail::scalar_real_from<T>(src, out);
        } else {
            return error_code::kind_mismatch;
        }
    }

    static error_code to(value_io* dst, const T& in) {
        // Unsigned values above INT64_MAX have no int64 scalar lane, so a
        // silent static_cast would wrap and drop data (spec §0 forbids silent
        // loss). Report overflow instead; the exact uint64 lane (numeric text
        // via to_string_base / JSON number reader) is out of scope for the
        // value_io scalar bridge today. The sizeof gate keeps bool and narrow
        // unsigned lanes out of the comparison (C4804 / dead check).
        if constexpr (std::is_unsigned<T>::value &&
                      sizeof(T) >= sizeof(std::uint64_t)) {
            if (in > static_cast<std::uint64_t>(
                         (std::numeric_limits<std::int64_t>::max)())) {
                return error_code::overflow;
            }
        }
        dst->make_scalar(scalar_of(in));
        return error_code::ok;
    }

private:
    static field_value scalar_of(const T& v) {
        if constexpr (std::is_same<T, bool>::value) {
            return field_value::of(v);
        } else if constexpr (std::is_integral<T>::value) {
            if constexpr (detail::is_plain_char<T>::value) {
                // char serializes as its ASCII byte value (spec §23.1) so the
                // full 0..255 lane round trips exactly.
                return field_value::of(
                    static_cast<int64_t>(static_cast<unsigned char>(v)));
            } else {
                return field_value::of(static_cast<int64_t>(v));
            }
        } else if constexpr (std::is_floating_point<T>::value) {
            return field_value::of(static_cast<double>(v));
        } else {
            return field_value::of_object();
        }
    }
};

template <typename Char, typename Traits, typename Alloc>
struct converter<std::basic_string<Char, Traits, Alloc>> {
    static error_code from(const value_io* src,
                           std::basic_string<Char, Traits, Alloc>& out) {
        string_view v;
        if (src->as_string(v)) {
            out.assign(v.data(), v.size());
            return error_code::ok;
        }
        std::int64_t iv = 0;
        if (src->as_integer(iv)) {
            // int -> string (spec §23.1): a numeric source reads as its
            // decimal text.
            const std::string s = std::to_string(iv);
            out.assign(s.data(), s.size());
            return error_code::ok;
        }
        double dv = 0.0;
        if (src->as_real(dv)) {
            // float -> string (spec §23.1): shortest round-tripping text.
            std::string s;
            rtti::detail::scalar_format_real(dv, s);
            out.assign(s.data(), s.size());
            return error_code::ok;
        }
        return error_code::kind_mismatch;
    }
    static error_code to(value_io* dst,
                         const std::basic_string<Char, Traits, Alloc>& in) {
        dst->make_scalar(
            field_value::of(rtti::string_view(in.data(), in.size())));
        return error_code::ok;
    }
};

// Non-owning char text (std::basic_string_view), routed onto the string lane
// like basic_string (is_fixed_sequence intentionally excludes it). The read
// side accepts the string lane directly and refuses the integer/real -> text
// lanes of basic_string (spec §23.1): a view cannot own a temporary
// conversion buffer, so the view always points into the source node's storage.
template <typename Char, typename Traits>
struct converter<std::basic_string_view<Char, Traits>> {
    static error_code from(const value_io* src,
                           std::basic_string_view<Char, Traits>& out) {
        rtti::string_view v;
        if (!src->as_string(v)) {
            return error_code::kind_mismatch;
        }
        out = std::basic_string_view<Char, Traits>(v.data(), v.size());
        return error_code::ok;
    }
    static error_code to(value_io* dst,
                         const std::basic_string_view<Char, Traits>& in) {
        dst->make_scalar(
            field_value::of(rtti::string_view(in.data(), in.size())));
        return error_code::ok;
    }
};

// Registered enums (RTTI_DEFINE_ENUM) convert through their label text.
// Specialized here (not in any format adapter header) so every format shares
// one definition and multiple adapters can be included in the same TU.
template <typename T, typename = void>
struct is_registered_enum : std::false_type {};
template <typename T>
struct is_registered_enum<T, std::void_t<decltype(enum_table<T>::count)>>
    : std::true_type {};

template <typename T>
struct converter<T, std::enable_if_t<std::is_enum<T>::value &&
                                     is_registered_enum<T>::value>> {
    static error_code from(const value_io* src, T& out) {
        string_view s;
        if (!src->as_string(s)) {
            return error_code::kind_mismatch;
        }
        for (std::size_t i = 0; i < enum_table<T>::count; ++i) {
            if (string_view(enum_table<T>::array[i].name) == s) {
                out = enum_table<T>::array[i].value;
                return error_code::ok;
            }
        }
        return error_code::kind_mismatch;
    }
    static error_code to(value_io* dst, const T& in) {
        const char* name = enum_to_string(in);
        if (name == nullptr || name[0] == '\0') {
            return error_code::kind_mismatch;
        }
        dst->make_scalar(field_value::of(string_view(name)));
        return error_code::ok;
    }
};

// date_time (spec §14): a scalar timestamp that converts to/from a numeric
// Unix epoch or ISO 8601 text. Read: a numeric scalar's unit is inferred from
// the decimal digit count (10 = seconds, 13 = ms, 16 = us, 19/20 = ns) and an
// ISO text scalar is parsed; anything else is an error. Write: numeric epoch
// seconds (spec §14.2 default). Non-default output (ms/us/ns or ISO text) is
// selected through to_value_dt, the documented config surface.
enum class date_time_output {
    numeric_seconds,
    numeric_ms,
    numeric_us,
    numeric_ns,
    iso_string
};

template <typename T>
struct converter<T, std::enable_if_t<is_date_time<T>::value>> {
    static error_code from(const value_io* src, T& out) {
        if (src == nullptr) {
            return error_code::no_such_field;
        }
        std::int64_t num = 0;
        if (src->as_integer(num)) {
            std::int64_t ns = 0;
            const error_code e = rtti::epoch_ns_from_digits(num, ns);
            if (e != error_code::ok) {
                return e;
            }
            out = T(ns);
            return error_code::ok;
        }
        string_view text;
        if (src->as_string(text)) {
            const result<T> r = T::parse(text);
            if (r.err.code != error_code::ok) {
                return r.err.code;
            }
            out = r.value;
            return error_code::ok;
        }
        return error_code::kind_mismatch;
    }
    static error_code to(value_io* dst, const T& in) {
        std::int64_t sec = 0;
        const error_code e = in.to_unix_seconds(sec);
        if (e != error_code::ok) {
            return e;
        }
        dst->make_scalar(field_value::of(sec));
        return error_code::ok;
    }
};

// Explicit-output entry point for date_time: numeric epoch seconds (default),
// ms, us, ns, or the ISO 8601 text form (which preserves sub-second precision
// exactly). The plain to_value/from_value paths default to numeric seconds on
// write and auto-detect the unit on read; this is how a caller opts into a
// different output representation.
inline error_code to_value_dt(value_io* dst, const date_time& in,
                              date_time_output mode) {
    if (dst == nullptr) {
        return error_code::no_such_field;
    }
    std::int64_t v = 0;
    switch (mode) {
        case date_time_output::numeric_ms: {
            const error_code e = in.to_unix_ms(v);
            if (e != error_code::ok) {
                return e;
            }
            dst->make_scalar(field_value::of(v));
            return error_code::ok;
        }
        case date_time_output::numeric_us: {
            const error_code e = in.to_unix_us(v);
            if (e != error_code::ok) {
                return e;
            }
            dst->make_scalar(field_value::of(v));
            return error_code::ok;
        }
        case date_time_output::numeric_ns: {
            const error_code e = in.to_unix_ns(v);
            if (e != error_code::ok) {
                return e;
            }
            dst->make_scalar(field_value::of(v));
            return error_code::ok;
        }
        case date_time_output::iso_string: {
            const std::string s = in.format();
            dst->make_scalar(
                field_value::of(string_view(s.data(), s.size())));
            return error_code::ok;
        }
        default: {
            const error_code e = in.to_unix_seconds(v);
            if (e != error_code::ok) {
                return e;
            }
            dst->make_scalar(field_value::of(v));
            return error_code::ok;
        }
    }
}

// Reflected-member write hookup. A date_time member serializes through the
// ISO-string output so sub-second precision is preserved by reflected-struct
// writes (the scalar default stays numeric epoch seconds, spec §14.2; the
// explicit-modes entry point to_value_dt remains the opt-in surface). All
// other member types route through the plain converter chain unchanged.
template <typename M>
inline error_code member_to_value(value_io* dst, const M& v) {
    if constexpr (is_date_time<M>::value) {
        return rtti::to_value_dt(dst, v, rtti::date_time_output::iso_string);
    } else {
        return rtti::to_value(dst, v);
    }
}

template <typename M>
inline error_code member_to_value_named(value_io* dst, const M& v,
                                        const detail::naming_policy& np) {
    if constexpr (is_date_time<M>::value) {
        (void)np;
        return rtti::to_value_dt(dst, v, rtti::date_time_output::iso_string);
    } else {
        return rtti::to_value_named(dst, v, np);
    }
}

// Arbitrary-base integer bridge (spec §15.4): to_value_base /
// from_value_base serialize an integer as a base-N string through an explicit
// output entry point, mirroring the date_time design where the plain to_value
// defaults to numeric output and text is opted into via to_value_dt. The plain
// from_value/to_value paths keep emitting numeric scalars for integers (the
// four-format assertions depend on it); `base` (2..64) is the documented
// config surface, with an optional custom digits table. date_time epochs are
// int64, so the signed entry points cover that hookup (numeric timestamp <-> 
// arbitrary-base string).
inline error_code to_value_base(value_io* dst, std::uint64_t value,
                                unsigned base, const char* digits = nullptr) {
    if (dst == nullptr) {
        return error_code::no_such_field;
    }
    const std::string s = rtti::detail::to_base(value, base, digits);
    if (s.empty()) {
        return error_code::invalid_number;
    }
    dst->make_scalar(field_value::of(string_view(s.data(), s.size())));
    return error_code::ok;
}

inline error_code from_value_base(const value_io* src, unsigned base,
                                  std::uint64_t& out) {
    if (src == nullptr) {
        return error_code::no_such_field;
    }
    string_view s;
    if (!src->as_string(s)) {
        return error_code::kind_mismatch;
    }
    return rtti::detail::from_base(s, base, out);
}

inline error_code to_value_base_signed(value_io* dst, std::int64_t value,
                                       unsigned base) {
    if (dst == nullptr) {
        return error_code::no_such_field;
    }
    const std::string s = rtti::detail::to_base_signed(value, base);
    if (s.empty()) {
        return error_code::invalid_number;
    }
    dst->make_scalar(field_value::of(string_view(s.data(), s.size())));
    return error_code::ok;
}

inline error_code from_value_base_signed(const value_io* src, unsigned base,
                                         std::int64_t& out) {
    if (src == nullptr) {
        return error_code::no_such_field;
    }
    string_view s;
    if (!src->as_string(s)) {
        return error_code::kind_mismatch;
    }
    return rtti::detail::from_base_signed(s, base, out);
}

// Fixed-size C char array (char[N]): a string target, not a numeric sequence
// (spec §12.2 excludes char element arrays from is_c_array). Serializes as a
// text scalar (up to the first NUL); deserializes from a text scalar sized
// into the buffer -- over-long input is kind_mismatch, never an overrun.
template <std::size_t N>
struct converter<char[N], void> {
    static error_code from(const value_io* src, char (&out)[N]) {
        string_view v;
        if (!src->as_string(v)) {
            return error_code::kind_mismatch;
        }
        if (v.size() >= N) {
            return error_code::kind_mismatch;
        }
        std::memset(out, 0, N);
        for (std::size_t i = 0; i < v.size(); ++i) {
            out[i] = v[i];
        }
        return error_code::ok;
    }
    static error_code to(value_io* dst, const char (&in)[N]) {
        std::size_t len = 0;
        while (len < N && in[len] != '\0') {
            ++len;
        }
        dst->make_scalar(field_value::of(string_view(in, len)));
        return error_code::ok;
    }
};

// --- Sequence converters ----------------------------------------------------

// std::vector<bool> is a bit-packed specialization whose iterators yield proxy
// objects on some standard libraries (libstdc++/libc++ const iterators too).
// Copy each proxy element into a real bool before conversion so the sequence
// path stays portable; on MSVC the const proxy already converts to bool here.
template <typename T, typename = void>
struct is_vector_bool : std::false_type {};
template <typename Alloc>
struct is_vector_bool<std::vector<bool, Alloc>> : std::true_type {};

// Sequence family (vector/list/deque): array <-> elements.
template <typename Seq>
error_code sequence_from(const value_io* src, Seq& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Seq::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.push_back(std::move(v));
    }
    return error_code::ok;
}

template <typename Seq>
error_code sequence_to(value_io* dst, const Seq& in) {
    dst->make_array();
    if constexpr (is_vector_bool<Seq>::value) {
        for (bool item : in) {
            value_io* child = dst->make_node();
            const error_code e = to_value(child, item);
            dst->push_back(child);
            if (e != error_code::ok) {
                return e;
            }
        }
    } else {
        for (const auto& item : in) {
            value_io* child = dst->make_node();
            const error_code e = to_value(child, item);
            dst->push_back(child);
            if (e != error_code::ok) {
                return e;
            }
        }
    }
    return error_code::ok;
}

// Map family: object <-> key/value pairs (key type must be string-convertible).
template <typename Map>
error_code map_from(const value_io* src, Map& out) {
    if (src->type() != value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        string_view key;
        value_io* child = nullptr;
        if (!src->get_key_at(i, key, child) || child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Map::mapped_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        typename Map::key_type k{};
        key_traits<typename Map::key_type>::assign(k, key);
        out.emplace(std::move(k), std::move(v));
    }
    return error_code::ok;
}

template <typename Map>
error_code map_to(value_io* dst, const Map& in) {
    dst->make_object();
    for (const auto& kv : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, kv.second);
        dst->set_key(key_traits<typename Map::key_type>::view(kv.first), child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

// Set family: array <-> elements (no value_type pair distinction).
template <typename Set>
error_code set_from(const value_io* src, Set& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Set::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.insert(std::move(v));
    }
    return error_code::ok;
}

template <typename Set>
error_code set_to(value_io* dst, const Set& in) {
    dst->make_array();
    for (const auto& item : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, item);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

// --- Container-adaptor converters ------------------------------------------
// queue/stack/priority_queue expose no iterators; reading them requires a
// copy, then draining front/top one at a time. The array emitted follows the
// adapter's order semantics; rebuild restores that order.

// queue: FIFO. Array order = front..back; rebuild pushes in the same order.
template <typename Q>
error_code queue_from(const value_io* src, Q& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    Q empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Q::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

template <typename Q>
error_code queue_to(value_io* dst, const Q& in) {
    dst->make_array();
    Q copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, copy.front());
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
        copy.pop();
    }
    return error_code::ok;
}

// stack: LIFO. Array order = top..bottom; rebuild pushes in reverse order so
// the top stays on top.
template <typename S>
error_code stack_from(const value_io* src, S& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    S empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = n; i-- > 0;) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename S::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

template <typename S>
error_code stack_to(value_io* dst, const S& in) {
    dst->make_array();
    S copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, copy.top());
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
        copy.pop();
    }
    return error_code::ok;
}

// priority_queue: heap order. Array order = pop order (largest first for the
// default less<int>); rebuild pushes in reverse order so the priority queue
// re-heapifies to the same top.
template <typename PQ>
error_code priority_queue_from(const value_io* src, PQ& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    PQ empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = n; i-- > 0;) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename PQ::value_type v{};
        const error_code e = from_value(child, v);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

template <typename PQ>
error_code priority_queue_to(value_io* dst, const PQ& in) {
    dst->make_array();
    PQ copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, copy.top());
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
        copy.pop();
    }
    return error_code::ok;
}

// --- New type-family converters (optional/variant/tuple/fixed sequence) ------
// Optional/variant/tuple/pair/array are composite shapes that dispatch through
// the same from_value/to_value core. std::pair is a 2-element tuple
// (std::get works, std::tuple_size is specialized), so it is covered entirely
// by the tuple path and has no converter of its own.

// optional: empty -> null, engaged -> converted value.
template <typename T>
error_code optional_from(const value_io* src, std::optional<T>& out) {
    if (src == nullptr) {
        return error_code::no_such_field;
    }
    if (src->type() == value_io::kind::null) {
        out.reset();
        return error_code::ok;
    }
    T v{};
    const error_code e = from_value(src, v);
    if (e != error_code::ok) {
        return e;
    }
    out = std::move(v);
    return error_code::ok;
}

template <typename T>
error_code optional_to(value_io* dst, const std::optional<T>& in) {
    if (!in.has_value()) {
        dst->make_scalar(field_value{});
        return error_code::ok;
    }
    return to_value(dst, *in);
}

// variant: object {"index":N,"value":V}. to_ emits the active alternative's
// value; from_ reads the numeric index and dispatches to that alternative
// only. Every alternative read is a generic from_value<Alt> call so all
// alternatives may be instantiated without any non-generic operation leaking
// into the wrong branch (Review Focus RF5).
template <typename V>
error_code variant_to(value_io* dst, const V& in) {
    if (in.valueless_by_exception()) {
        return error_code::kind_mismatch;
    }
    dst->make_object();
    error_code e = error_code::ok;
    std::visit([&](const auto& val) {
        value_io* index_node = dst->make_node();
        index_node->make_scalar(
            field_value::of(static_cast<int64_t>(in.index())));
        dst->set_key("index", index_node);
        value_io* value_node = dst->make_node();
        // Propagate a child conversion failure instead of silently leaving an
        // empty "value" node behind.
        e = to_value(value_node, val);
        dst->set_key("value", value_node);
    }, in);
    return e;
}

template <typename V, std::size_t I>
error_code variant_from_at(const value_io* src, V& out) {
    using Alt = std::variant_alternative_t<I, V>;
    Alt a{};
    const error_code e = from_value(src, a);
    if (e != error_code::ok) {
        return e;
    }
    out = std::move(a);
    return error_code::ok;
}

template <typename V, std::size_t... I>
error_code variant_from_indexed(const value_io* src, V& out, std::size_t idx,
                                std::index_sequence<I...>) {
    error_code e = error_code::kind_mismatch;
    (void)std::initializer_list<int>{
        (idx == I ? (e = variant_from_at<V, I>(src, out)) : e, 0)...};
    return e;
}

template <typename V>
error_code variant_from(const value_io* src, V& out) {
    if (src->type() != value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    value_io* index_node = nullptr;
    if (!src->get_key("index", index_node)) {
        return error_code::kind_mismatch;
    }
    int64_t idx = 0;
    if (!index_node->as_integer(idx)) {
        return error_code::kind_mismatch;
    }
    if (idx < 0 || static_cast<std::size_t>(idx) >= std::variant_size<V>::value) {
        return error_code::kind_mismatch;
    }
    value_io* value_node = nullptr;
    if (!src->get_key("value", value_node)) {
        return error_code::kind_mismatch;
    }
    return variant_from_indexed(value_node, out, static_cast<std::size_t>(idx),
                                std::make_index_sequence<std::variant_size<V>::value>{});
}

// tuple/pair: make_index_sequence expansion (never std::apply(index_sequence)
// -- MSVC cannot reliably fold its tuple_size, Review Focus RF3).
template <typename Tuple, std::size_t... I>
error_code tuple_to_seq(value_io* dst, const Tuple& t, std::index_sequence<I...>) {
    dst->make_array();
    error_code e = error_code::ok;
    (void)std::initializer_list<int>{
        ([&] {
            value_io* child = dst->make_node();
            const error_code c = to_value(child, std::get<I>(t));
            dst->push_back(child);
            if (c != error_code::ok && e == error_code::ok) {
                e = c;  // propagate the first child failure, keep converting
            }
            return 0;
        }())...};
    return e;
}

template <typename Tuple>
error_code tuple_to(value_io* dst, const Tuple& in) {
    return tuple_to_seq(dst, in,
                        std::make_index_sequence<std::tuple_size<Tuple>::value>{});
}

template <typename Tuple, std::size_t... I>
error_code tuple_from_seq(const value_io* src, Tuple& out, std::index_sequence<I...>) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    error_code e = error_code::ok;
    (void)std::initializer_list<int>{
        ([&] {
            value_io* child = src->at(I);
            if (child == nullptr) {
                e = error_code::kind_mismatch;
                return 0;
            }
            const error_code c = from_value(child, std::get<I>(out));
            if (c != error_code::ok) {
                e = c;
            }
            return 0;
        }())...};
    return e;
}

template <typename Tuple>
error_code tuple_from(const value_io* src, Tuple& out) {
    return tuple_from_seq(src, out,
                          std::make_index_sequence<std::tuple_size<Tuple>::value>{});
}

// Fixed-size sequence (std::array): array <-> elements, bounded by size().
template <typename A>
error_code fixed_sequence_to(value_io* dst, const A& in) {
    dst->make_array();
    for (const auto& item : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, item);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename A>
error_code fixed_sequence_from(const value_io* src, A& out) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    // Match the strict carray_from contract (spec 12.2): an element count
    // that differs from the fixed compile-time extent is kind_mismatch, never
    // a silent truncation or a leftover stale tail.
    if (src->size() != out.size()) {
        return error_code::kind_mismatch;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        const error_code e = from_value(child, out[i]);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

// --- C-style array converters (spec §12.2) ---------------------------------
// Native T[N] / T[N][M] / T[A][B][C] convert through compile-time extents with
// strict known-size validation on read: an element count that does not match
// the exact extent product is kind_mismatch, never a truncation or overrun.
// Serialization produces nested arrays (one level per dimension; natural for
// JSON/TOML). Deserialization accepts either the nested form or a flat
// row-major element list (spec §12.3 minimal form).

// Product of every compile-time extent of a C array type (rank 0 => 1).
template <typename T>
struct carray_total : std::integral_constant<std::size_t, 1> {};
template <typename T, std::size_t N>
struct carray_total<T[N]>
    : std::integral_constant<std::size_t, N * carray_total<T>::value> {};

// Number of dimensions of a C array type.
template <typename T>
constexpr std::size_t carray_rank() noexcept {
    return std::rank<T>::value;
}

// Compile-time extent of dimension I (0-based) via std::extent.
template <typename T, std::size_t I>
constexpr std::size_t carray_extent() noexcept {
    return std::extent<T, I>::value;
}

// Serialize a C array as an array of elements; multidimensional arrays recurse
// one dimension per level, emitting nested arrays.
template <typename T, std::size_t N>
error_code carray_to(value_io* dst, const T (&in)[N]) {
    dst->make_array();
    for (std::size_t i = 0; i < N; ++i) {
        value_io* child = dst->make_node();
        const error_code e = to_value(child, in[i]);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

// Row-major flat fill: treat src as one flat element sequence spanning the
// full product of extents, filling the array linearly.
template <typename T, std::size_t N>
error_code carray_from_flat(const value_io* src, std::size_t base, T (&out)[N]) {
    if constexpr (std::is_array<T>::value) {
        constexpr std::size_t inner = carray_total<T>::value;
        for (std::size_t i = 0; i < N; ++i) {
            const error_code e = carray_from_flat(src, base + i * inner, out[i]);
            if (e != error_code::ok) {
                return e;
            }
        }
        return error_code::ok;
    } else {
        for (std::size_t i = 0; i < N; ++i) {
            value_io* child = src->at(base + i);
            if (child == nullptr) {
                return error_code::kind_mismatch;
            }
            const error_code e = from_value(child, out[i]);
            if (e != error_code::ok) {
                return e;
            }
        }
        return error_code::ok;
    }
}

// Read a C array from an array source. The element count must match the exact
// compile-time extent(s): a multidimensional target accepts either the nested
// form (one child array per dimension) or a flat row-major list. Any mismatch
// returns kind_mismatch; writes never go out of bounds.
template <typename T, std::size_t N>
error_code carray_from(const value_io* src, T (&out)[N]) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    constexpr std::size_t total = carray_total<T[N]>::value;
    const std::size_t sz = src->size();
    if constexpr (std::is_array<T>::value) {
        // Nested interpretation attempted first (only meaningful when the
        // element type is itself an array; also disambiguates M==1 where the
        // flat and nested counts coincide).
        if (sz == N) {
            error_code e = error_code::ok;
            for (std::size_t i = 0; i < N; ++i) {
                value_io* child = src->at(i);
                if (child == nullptr) {
                    return error_code::kind_mismatch;
                }
                e = carray_from(child, out[i]);
                if (e != error_code::ok) {
                    break;
                }
            }
            if (e == error_code::ok) {
                return error_code::ok;
            }
            if (sz == total) {
                return carray_from_flat(src, 0, out);
            }
            return e;
        }
    }
    if (sz == total) {
        return carray_from_flat(src, 0, out);
    }
    return error_code::kind_mismatch;
}

// --- Reflected-struct walk (field-table path) ------------------------------
// A type registered with RTTI_DEFINE_TYPE is read/written field by field.
// Missing-key behavior depends on has_default<T>: the plain macro returns
// missing_key; the _WITH_DEFAULT variant keeps the member's current value.

template <typename T>
error_code reflect_from(const value_io* src, T& out) {
    if (src->type() != value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    const auto* const* f = fields<T>();
    for (std::size_t i = 0; i < field_count<T>(); ++i) {
        const field_access_base* fa = f[i];
        value_io* sub = nullptr;
        if (!src->get_key(fa->name, sub)) {
            if (has_default<T>::value) {
                continue; // keep existing member value
            }
            return error_code::missing_key;
        }
        // Nested/container/scalar members all recurse via the accessor.
        const error_code e = fa->set_value(static_cast<void*>(&out), sub);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename T>
error_code reflect_to(value_io* dst, const T& in) {
    dst->make_object();
    const auto* const* f = fields<T>();
    for (std::size_t i = 0; i < field_count<T>(); ++i) {
        const field_access_base* fa = f[i];
        value_io* child = dst->make_node();
        const error_code e = fa->get_value(static_cast<const void*>(&in), child);
        dst->set_key(fa->name, child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

// --- Entry points -----------------------------------------------------------

// Depth budget shared by the read/write converter chains. The format parsers
// cap tree depth at 512 (e.g. JSON/XML/TOML max_depth), but a write path over
// a self-referential data structure recursed without any bound and overflowed
// the stack. Every from_value/to_value entry carries a thread-local depth
// guard; exceeding it reports error_code::too_deep instead of crashing.
namespace detail {

inline constexpr std::size_t kConversionMaxDepth = 512;

inline int& conversion_depth() noexcept {
    static thread_local int depth = 0;
    return depth;
}

struct conversion_depth_guard {
    conversion_depth_guard() noexcept : depth_(++conversion_depth()) {}
    ~conversion_depth_guard() noexcept { --conversion_depth(); }
    bool over() const noexcept {
        return static_cast<unsigned>(depth_) > kConversionMaxDepth;
    }

private:
    int depth_;
};

} // namespace detail

// Detection: does the type's associated namespace provide an ADL
// from_value/to_value hook with the 3-argument signature (plan contract)?
// The rtti two-argument templates never match the 3-argument detection, so
// ordinary lookup cannot pick them; only a user hook survives overload
// resolution. Users write, in the type's namespace:
//   void from_value(const value_io*, T&, error_code&);
//   void to_value(value_io*, const T&, error_code&);

template <typename T, typename = void>
struct has_adl_from : std::false_type {};
template <typename T>
struct has_adl_from<T, std::void_t<decltype(from_value(std::declval<const value_io*>(),
                                                        std::declval<T&>(),
                                                        std::declval<error_code&>()))>>
    : std::true_type {};

template <typename T, typename = void>
struct has_adl_to : std::false_type {};
template <typename T>
struct has_adl_to<T, std::void_t<decltype(to_value(std::declval<value_io*>(),
                                                    std::declval<const T&>(),
                                                    std::declval<error_code&>()))>>
    : std::true_type {};

// Read `src` into object `out`. Returns an error_code.
template <typename T>
error_code from_value(const value_io* src, T& out) {
    if (src == nullptr) {
        return error_code::no_such_field;
    }
    detail::conversion_depth_guard depth_guard;
    if (depth_guard.over()) {
        return error_code::too_deep;
    }
    // Dispatch order: optional -> variant -> tuple (pair is a 2-tuple) ->
    // c_array -> fixed_sequence -> map -> set -> priority_queue -> queue ->
    // stack -> sequence -> reflected -> adl -> converter. is_fixed_sequence
    // never matches map/set (is_associative excludes them, reflection.hpp), so
    // ordering it before the associative branches is safe without leaking
    // key-based containers into the index-addressable fixed-sequence path.
    // Native C arrays (is_c_array) have no value_type, so they reach neither
    // is_sequence nor is_fixed_sequence and are routed before fixed_sequence
    // (spec §12.4).
    if constexpr (is_optional<T>::value) {
        return optional_from(src, out);
    } else if constexpr (is_variant<T>::value) {
        return variant_from(src, out);
    } else if constexpr (is_tuple<T>::value) {
        return tuple_from(src, out);
    } else if constexpr (is_c_array<T>::value) {
        // Before fixed_sequence: C arrays have no value_type/push_back, so
        // they can never reach the fixed-sequence branch; ordering here keeps
        // the fixed-size families grouped (spec §12.4).
        return carray_from(src, out);
    } else if constexpr (is_fixed_sequence<T>::value) {
        return fixed_sequence_from(src, out);
    } else if constexpr (is_map<T>::value) {
        return map_from(src, out);
    } else if constexpr (is_set<T>::value) {
        return set_from(src, out);
    } else if constexpr (is_priority_queue<T>::value) {
        // Before is_stack: a priority_queue also has push/pop/top.
        return priority_queue_from(src, out);
    } else if constexpr (is_queue<T>::value) {
        return queue_from(src, out);
    } else if constexpr (is_stack<T>::value) {
        return stack_from(src, out);
    } else if constexpr (is_sequence<T>::value) {
        return sequence_from(src, out);
    } else if constexpr (is_date_time<T>::value) {
        // Before is_reflected: a user-registered date_time still converts as a
        // scalar timestamp, not as a field-walked object (spec §14.2).
        return converter<T>::from(src, out);
    } else if constexpr (is_reflected<T>::value) {
        return reflect_from(src, out);
    } else if constexpr (has_adl_from<T>::value) {
        error_code e = error_code::ok;
        from_value(src, out, e); // found via ADL in the type's namespace
        return e;
    } else {
        return converter<T>::from(src, out);
    }
}

// Write `in` into sink `dst`.
template <typename T>
error_code to_value(value_io* dst, const T& in) {
    if (dst == nullptr) {
        return error_code::no_such_field;
    }
    detail::conversion_depth_guard depth_guard;
    if (depth_guard.over()) {
        return error_code::too_deep;
    }
    // Dispatch order mirrors from_value.
    if constexpr (is_optional<T>::value) {
        return optional_to(dst, in);
    } else if constexpr (is_variant<T>::value) {
        return variant_to(dst, in);
    } else if constexpr (is_tuple<T>::value) {
        return tuple_to(dst, in);
    } else if constexpr (is_c_array<T>::value) {
        return carray_to(dst, in);
    } else if constexpr (is_fixed_sequence<T>::value) {
        return fixed_sequence_to(dst, in);
    } else if constexpr (is_map<T>::value) {
        return map_to(dst, in);
    } else if constexpr (is_set<T>::value) {
        return set_to(dst, in);
    } else if constexpr (is_priority_queue<T>::value) {
        // Before is_stack: a priority_queue also has push/pop/top.
        return priority_queue_to(dst, in);
    } else if constexpr (is_queue<T>::value) {
        return queue_to(dst, in);
    } else if constexpr (is_stack<T>::value) {
        return stack_to(dst, in);
    } else if constexpr (is_sequence<T>::value) {
        return sequence_to(dst, in);
    } else if constexpr (is_date_time<T>::value) {
        return converter<T>::to(dst, in);
    } else if constexpr (is_reflected<T>::value) {
        return reflect_to(dst, in);
    } else if constexpr (has_adl_to<T>::value) {
        error_code e = error_code::ok;
        to_value(dst, in, e); // found via ADL in the type's namespace
        return e;
    } else {
        return converter<T>::to(dst, in);
    }
}

// --- Named conversion (key naming policy) ----------------------------------
// to_value_named / from_value_named mirror the plain to_value / from_value
// dispatch chain while applying the naming policy at the reflected-field
// boundary: keys are computed through detail::apply_naming and skip_empty
// drops empty fields on the serialize side. Container element recursion goes
// through the named variants so a nested reflected struct inside any container
// is renamed too; associative-container keys are user data and stay untouched.

template <typename T>
error_code to_value_named(value_io* dst, const T& in,
                          const detail::naming_policy& np);

template <typename T>
error_code from_value_named(const value_io* src, T& out,
                            const detail::naming_policy& np);

namespace detail {

// True when a reflected field's value is "empty" under skip_empty semantics:
// a null-equivalent value (empty optional / null pointer), an empty container
// (size()==0), an empty string, a numeric zero, or a false bool. Containers
// are probed through the type-erased field accessor (size() is const-safe and
// never mutates its object); scalars go through get() -> field_value so the
// check is format-independent.
inline bool field_is_empty(const field_access_base* fa, const void* obj) {
    if (fa->is_null_value(obj)) {
        return true;
    }
    if (fa->is_container()) {
        return fa->size(const_cast<void*>(obj)) == 0;
    }
    const field_value v = fa->get(obj);
    switch (v.type()) {
        case field_value::kind::string:
            return v.as_string().empty();
        case field_value::kind::integer:
            return v.as_integer() == 0;
        case field_value::kind::real:
            return v.as_real() == 0.0;
        case field_value::kind::boolean:
            return !v.as_bool();
        default:
            return false;
    }
}

} // namespace detail

template <typename T>
error_code reflect_to_named(value_io* dst, const T& in,
                            const detail::naming_policy& np) {
    dst->make_object();
    const auto* const* f = fields<T>();
    for (std::size_t i = 0; i < field_count<T>(); ++i) {
        const field_access_base* fa = f[i];
        if (np.skip_empty &&
            rtti::detail::field_is_empty(fa, static_cast<const void*>(&in))) {
            continue;
        }
        value_io* child = dst->make_node();
        const error_code e =
            fa->get_value_named(static_cast<const void*>(&in), child, np);
        dst->set_key(rtti::detail::apply_naming(fa->name, np), child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename T>
error_code reflect_from_named(const value_io* src, T& out,
                              const detail::naming_policy& np) {
    if (src->type() != value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    const auto* const* f = fields<T>();
    for (std::size_t i = 0; i < field_count<T>(); ++i) {
        const field_access_base* fa = f[i];
        value_io* sub = nullptr;
        const std::string key = rtti::detail::apply_naming(fa->name, np);
        if (!src->get_key(key, sub)) {
            if (has_default<T>::value) {
                continue; // keep existing member value
            }
            return error_code::missing_key;
        }
        const error_code e = fa->set_value_named(static_cast<void*>(&out), sub, np);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename T>
error_code optional_to_named(value_io* dst, const std::optional<T>& in,
                             const detail::naming_policy& np) {
    if (!in.has_value()) {
        dst->make_scalar(field_value{});
        return error_code::ok;
    }
    return to_value_named(dst, *in, np);
}

template <typename T>
error_code optional_from_named(const value_io* src, std::optional<T>& out,
                               const detail::naming_policy& np) {
    if (src == nullptr) {
        return error_code::no_such_field;
    }
    if (src->type() == value_io::kind::null) {
        out.reset();
        return error_code::ok;
    }
    T v{};
    const error_code e = from_value_named(src, v, np);
    if (e != error_code::ok) {
        return e;
    }
    out = std::move(v);
    return error_code::ok;
}

template <typename V>
error_code variant_to_named(value_io* dst, const V& in,
                            const detail::naming_policy& np) {
    if (in.valueless_by_exception()) {
        return error_code::kind_mismatch;
    }
    dst->make_object();
    error_code e = error_code::ok;
    std::visit([&](const auto& val) {
        value_io* index_node = dst->make_node();
        index_node->make_scalar(
            field_value::of(static_cast<int64_t>(in.index())));
        dst->set_key("index", index_node);
        value_io* value_node = dst->make_node();
        e = to_value_named(value_node, val, np);
        dst->set_key("value", value_node);
    }, in);
    return e;
}

template <typename V, std::size_t I>
error_code variant_from_at_named(const value_io* src, V& out,
                                 const detail::naming_policy& np) {
    using Alt = std::variant_alternative_t<I, V>;
    Alt a{};
    const error_code e = from_value_named(src, a, np);
    if (e != error_code::ok) {
        return e;
    }
    out = std::move(a);
    return error_code::ok;
}

template <typename V, std::size_t... I>
error_code variant_from_indexed_named(const value_io* src, V& out,
                                      std::size_t idx,
                                      std::index_sequence<I...>,
                                      const detail::naming_policy& np) {
    error_code e = error_code::kind_mismatch;
    (void)std::initializer_list<int>{
        (idx == I ? (e = variant_from_at_named<V, I>(src, out, np)) : e, 0)...};
    return e;
}

template <typename V>
error_code variant_from_named(const value_io* src, V& out,
                              const detail::naming_policy& np) {
    if (src->type() != value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    value_io* index_node = nullptr;
    if (!src->get_key("index", index_node)) {
        return error_code::kind_mismatch;
    }
    int64_t idx = 0;
    if (!index_node->as_integer(idx)) {
        return error_code::kind_mismatch;
    }
    if (idx < 0 || static_cast<std::size_t>(idx) >= std::variant_size<V>::value) {
        return error_code::kind_mismatch;
    }
    value_io* value_node = nullptr;
    if (!src->get_key("value", value_node)) {
        return error_code::kind_mismatch;
    }
    return variant_from_indexed_named(
        value_node, out, static_cast<std::size_t>(idx),
        std::make_index_sequence<std::variant_size<V>::value>{}, np);
}

template <typename Tuple, std::size_t... I>
error_code tuple_to_seq_named(value_io* dst, const Tuple& t,
                              std::index_sequence<I...>,
                              const detail::naming_policy& np) {
    dst->make_array();
    error_code e = error_code::ok;
    (void)std::initializer_list<int>{
        ([&] {
            value_io* child = dst->make_node();
            const error_code c = to_value_named(child, std::get<I>(t), np);
            dst->push_back(child);
            if (c != error_code::ok && e == error_code::ok) {
                e = c;
            }
            return 0;
        }())...};
    return e;
}

template <typename Tuple>
error_code tuple_to_named(value_io* dst, const Tuple& in,
                          const detail::naming_policy& np) {
    return tuple_to_seq_named(dst, in,
                              std::make_index_sequence<std::tuple_size<Tuple>::value>{},
                              np);
}

template <typename Tuple, std::size_t... I>
error_code tuple_from_seq_named(const value_io* src, Tuple& out,
                                std::index_sequence<I...>,
                                const detail::naming_policy& np) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    error_code e = error_code::ok;
    (void)std::initializer_list<int>{
        ([&] {
            value_io* child = src->at(I);
            if (child == nullptr) {
                e = error_code::kind_mismatch;
                return 0;
            }
            const error_code c = from_value_named(child, std::get<I>(out), np);
            if (c != error_code::ok) {
                e = c;
            }
            return 0;
        }())...};
    return e;
}

template <typename Tuple>
error_code tuple_from_named(const value_io* src, Tuple& out,
                            const detail::naming_policy& np) {
    return tuple_from_seq_named(src, out,
                                std::make_index_sequence<std::tuple_size<Tuple>::value>{},
                                np);
}

template <typename A>
error_code fixed_sequence_to_named(value_io* dst, const A& in,
                                   const detail::naming_policy& np) {
    dst->make_array();
    for (const auto& item : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value_named(child, item, np);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename A>
error_code fixed_sequence_from_named(const value_io* src, A& out,
                                     const detail::naming_policy& np) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    // Match fixed_sequence_from (spec 12.2): an element count that differs
    // from the fixed compile-time extent is kind_mismatch, never a silent
    // truncation to the prefix or a stale tail.
    if (src->size() != out.size()) {
        return error_code::kind_mismatch;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        const error_code e = from_value_named(child, out[i], np);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

// C-style array named variants: mirror carray_to/carray_from with element
// recursion through the named converter chain (spec §12.2 + naming policy).
template <typename T, std::size_t N>
error_code carray_to_named(value_io* dst, const T (&in)[N],
                           const detail::naming_policy& np) {
    dst->make_array();
    for (std::size_t i = 0; i < N; ++i) {
        value_io* child = dst->make_node();
        const error_code e = to_value_named(child, in[i], np);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename T, std::size_t N>
error_code carray_from_flat_named(const value_io* src, std::size_t base,
                                  T (&out)[N],
                                  const detail::naming_policy& np) {
    if constexpr (std::is_array<T>::value) {
        constexpr std::size_t inner = carray_total<T>::value;
        for (std::size_t i = 0; i < N; ++i) {
            const error_code e =
                carray_from_flat_named(src, base + i * inner, out[i], np);
            if (e != error_code::ok) {
                return e;
            }
        }
        return error_code::ok;
    } else {
        for (std::size_t i = 0; i < N; ++i) {
            value_io* child = src->at(base + i);
            if (child == nullptr) {
                return error_code::kind_mismatch;
            }
            const error_code e = from_value_named(child, out[i], np);
            if (e != error_code::ok) {
                return e;
            }
        }
        return error_code::ok;
    }
}

template <typename T, std::size_t N>
error_code carray_from_named(const value_io* src, T (&out)[N],
                             const detail::naming_policy& np) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    constexpr std::size_t total = carray_total<T[N]>::value;
    const std::size_t sz = src->size();
    if constexpr (std::is_array<T>::value) {
        if (sz == N) {
            error_code e = error_code::ok;
            for (std::size_t i = 0; i < N; ++i) {
                value_io* child = src->at(i);
                if (child == nullptr) {
                    return error_code::kind_mismatch;
                }
                e = carray_from_named(child, out[i], np);
                if (e != error_code::ok) {
                    break;
                }
            }
            if (e == error_code::ok) {
                return error_code::ok;
            }
            if (sz == total) {
                return carray_from_flat_named(src, 0, out, np);
            }
            return e;
        }
    }
    if (sz == total) {
        return carray_from_flat_named(src, 0, out, np);
    }
    return error_code::kind_mismatch;
}

template <typename Seq>
error_code sequence_to_named(value_io* dst, const Seq& in,
                             const detail::naming_policy& np) {
    dst->make_array();
    if constexpr (is_vector_bool<Seq>::value) {
        for (bool item : in) {
            value_io* child = dst->make_node();
            const error_code e = to_value_named(child, item, np);
            dst->push_back(child);
            if (e != error_code::ok) {
                return e;
            }
        }
    } else {
        for (const auto& item : in) {
            value_io* child = dst->make_node();
            const error_code e = to_value_named(child, item, np);
            dst->push_back(child);
            if (e != error_code::ok) {
                return e;
            }
        }
    }
    return error_code::ok;
}

template <typename Seq>
error_code sequence_from_named(const value_io* src, Seq& out,
                               const detail::naming_policy& np) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Seq::value_type v{};
        const error_code e = from_value_named(child, v, np);
        if (e != error_code::ok) {
            return e;
        }
        out.push_back(std::move(v));
    }
    return error_code::ok;
}

template <typename Map>
error_code map_to_named(value_io* dst, const Map& in,
                        const detail::naming_policy& np) {
    dst->make_object();
    for (const auto& kv : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value_named(child, kv.second, np);
        dst->set_key(key_traits<typename Map::key_type>::view(kv.first), child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename Map>
error_code map_from_named(const value_io* src, Map& out,
                          const detail::naming_policy& np) {
    if (src->type() != value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        string_view key;
        value_io* child = nullptr;
        if (!src->get_key_at(i, key, child) || child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Map::mapped_type v{};
        const error_code e = from_value_named(child, v, np);
        if (e != error_code::ok) {
            return e;
        }
        typename Map::key_type k{};
        key_traits<typename Map::key_type>::assign(k, key);
        out.emplace(std::move(k), std::move(v));
    }
    return error_code::ok;
}

template <typename Set>
error_code set_to_named(value_io* dst, const Set& in,
                        const detail::naming_policy& np) {
    dst->make_array();
    for (const auto& item : in) {
        value_io* child = dst->make_node();
        const error_code e = to_value_named(child, item, np);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
    }
    return error_code::ok;
}

template <typename Set>
error_code set_from_named(const value_io* src, Set& out,
                          const detail::naming_policy& np) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    out.clear();
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Set::value_type v{};
        const error_code e = from_value_named(child, v, np);
        if (e != error_code::ok) {
            return e;
        }
        out.insert(std::move(v));
    }
    return error_code::ok;
}

template <typename Q>
error_code queue_to_named(value_io* dst, const Q& in,
                          const detail::naming_policy& np) {
    dst->make_array();
    Q copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value_named(child, copy.front(), np);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
        copy.pop();
    }
    return error_code::ok;
}

template <typename Q>
error_code queue_from_named(const value_io* src, Q& out,
                            const detail::naming_policy& np) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    Q empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = 0; i < n; ++i) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename Q::value_type v{};
        const error_code e = from_value_named(child, v, np);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

template <typename S>
error_code stack_to_named(value_io* dst, const S& in,
                          const detail::naming_policy& np) {
    dst->make_array();
    S copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value_named(child, copy.top(), np);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
        copy.pop();
    }
    return error_code::ok;
}

template <typename S>
error_code stack_from_named(const value_io* src, S& out,
                            const detail::naming_policy& np) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    S empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = n; i-- > 0;) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename S::value_type v{};
        const error_code e = from_value_named(child, v, np);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

template <typename PQ>
error_code priority_queue_to_named(value_io* dst, const PQ& in,
                                   const detail::naming_policy& np) {
    dst->make_array();
    PQ copy = in;
    while (!copy.empty()) {
        value_io* child = dst->make_node();
        const error_code e = to_value_named(child, copy.top(), np);
        dst->push_back(child);
        if (e != error_code::ok) {
            return e;
        }
        copy.pop();
    }
    return error_code::ok;
}

template <typename PQ>
error_code priority_queue_from_named(const value_io* src, PQ& out,
                                     const detail::naming_policy& np) {
    if (src->type() != value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    PQ empty;
    out.swap(empty);
    const std::size_t n = src->size();
    for (std::size_t i = n; i-- > 0;) {
        value_io* child = src->at(i);
        if (child == nullptr) {
            return error_code::kind_mismatch;
        }
        typename PQ::value_type v{};
        const error_code e = from_value_named(child, v, np);
        if (e != error_code::ok) {
            return e;
        }
        out.push(std::move(v));
    }
    return error_code::ok;
}

// Entry points: identical dispatch order to the plain from_value / to_value,
// with naming applied at the reflected-struct boundary and container
// recursion routed through the named variants.
template <typename T>
error_code from_value_named(const value_io* src, T& out,
                            const detail::naming_policy& np) {
    if (src == nullptr) {
        return error_code::no_such_field;
    }
    detail::conversion_depth_guard depth_guard;
    if (depth_guard.over()) {
        return error_code::too_deep;
    }
    if constexpr (is_optional<T>::value) {
        return optional_from_named(src, out, np);
    } else if constexpr (is_variant<T>::value) {
        return variant_from_named(src, out, np);
    } else if constexpr (is_tuple<T>::value) {
        return tuple_from_named(src, out, np);
    } else if constexpr (is_c_array<T>::value) {
        return carray_from_named(src, out, np);
    } else if constexpr (is_fixed_sequence<T>::value) {
        return fixed_sequence_from_named(src, out, np);
    } else if constexpr (is_map<T>::value) {
        return map_from_named(src, out, np);
    } else if constexpr (is_set<T>::value) {
        return set_from_named(src, out, np);
    } else if constexpr (is_priority_queue<T>::value) {
        return priority_queue_from_named(src, out, np);
    } else if constexpr (is_queue<T>::value) {
        return queue_from_named(src, out, np);
    } else if constexpr (is_stack<T>::value) {
        return stack_from_named(src, out, np);
    } else if constexpr (is_sequence<T>::value) {
        return sequence_from_named(src, out, np);
    } else if constexpr (is_date_time<T>::value) {
        return converter<T>::from(src, out);
    } else if constexpr (is_reflected<T>::value) {
        return reflect_from_named(src, out, np);
    } else if constexpr (has_adl_from<T>::value) {
        error_code e = error_code::ok;
        from_value(src, out, e); // user hook owns its own key naming
        return e;
    } else {
        return converter<T>::from(src, out);
    }
}

template <typename T>
error_code to_value_named(value_io* dst, const T& in,
                          const detail::naming_policy& np) {
    if (dst == nullptr) {
        return error_code::no_such_field;
    }
    detail::conversion_depth_guard depth_guard;
    if (depth_guard.over()) {
        return error_code::too_deep;
    }
    if constexpr (is_optional<T>::value) {
        return optional_to_named(dst, in, np);
    } else if constexpr (is_variant<T>::value) {
        return variant_to_named(dst, in, np);
    } else if constexpr (is_tuple<T>::value) {
        return tuple_to_named(dst, in, np);
    } else if constexpr (is_c_array<T>::value) {
        return carray_to_named(dst, in, np);
    } else if constexpr (is_fixed_sequence<T>::value) {
        return fixed_sequence_to_named(dst, in, np);
    } else if constexpr (is_map<T>::value) {
        return map_to_named(dst, in, np);
    } else if constexpr (is_set<T>::value) {
        return set_to_named(dst, in, np);
    } else if constexpr (is_priority_queue<T>::value) {
        return priority_queue_to_named(dst, in, np);
    } else if constexpr (is_queue<T>::value) {
        return queue_to_named(dst, in, np);
    } else if constexpr (is_stack<T>::value) {
        return stack_to_named(dst, in, np);
    } else if constexpr (is_sequence<T>::value) {
        return sequence_to_named(dst, in, np);
    } else if constexpr (is_date_time<T>::value) {
        return converter<T>::to(dst, in);
    } else if constexpr (is_reflected<T>::value) {
        return reflect_to_named(dst, in, np);
    } else if constexpr (has_adl_to<T>::value) {
        error_code e = error_code::ok;
        to_value(dst, in, e); // user hook owns its own key naming
        return e;
    } else {
        return converter<T>::to(dst, in);
    }
}

// --- Shared real formatting -------------------------------------------------
// The single place that turns a double into its serialized text for every
// format adapter. std::to_chars emits the shortest decimal that round-trips
// back to the exact value, so 3.14 stays "3.14" instead of the long-tail
// "3.1400000000000001". A ".0" suffix is appended when the shortest form has
// no decimal point or exponent (e.g. "1") so parsers read the token back as a
// floating-point value. Non-finite doubles map to the deterministic tokens
// "inf"/"-inf"/"nan" (matching the previous snprintf %.17g output) rather
// than relying on the implementation-defined to_chars handling for those
// values, so callers never receive an empty string (spec §0). JSON maps
// non-finite to null before calling; XML/INI keep the inf/nan text.
namespace detail {

inline void format_real(double value, std::string& out) {
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            out += "nan";
        } else {
            out += std::signbit(value) ? "-inf" : "inf";
        }
        return;
    }
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), value);
    const char* const first = buf;
    const char* const last = res.ptr;
    out.append(first, static_cast<std::size_t>(last - first));
    // Decide from the token just written, not the accumulated whole stream: an
    // earlier element's '.' (e.g. "[1.5,") must not suppress this token's
    // ".0" (C2-①).
    bool has_point = false;
    for (const char* p = first; p != last; ++p) {
        if (*p == '.' || *p == 'e' || *p == 'E') {
            has_point = true;
            break;
        }
    }
    if (!has_point) {
        out += ".0";
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// Value-semantics strong-cast bridge (rtti::convert::as / cast / to).
//
// A numeric "value bridge" over the scalar lanes already implemented above:
//   rtti::convert::as<T>(src)   -> optional<T>, nullopt on any failure
//   rtti::convert::cast<T>(src) -> result<T>, distinguishable error codes
//   rtti::convert::to<T>(src)   -> alias of cast (single unified entry)
//
// Sources: native scalars (bool / integers / floating point), string_view /
// std::string, field_value, and value_io-derived pointers (including const).
// Targets: scalar int family, floating point, std::string / string_view,
// registered enums, and single-layer std::vector<scalar>.
//
// Error mapping: kind_mismatch for incompatible lanes, overflow for numeric
// range violations (truncation toward zero, never wrap), invalid_number for
// malformed numeric/label text (the library's documented ill-formed-literal
// code), and no_such_field for null / null-kind sources.
//
// The bridge lives in its own namespace so the existing pointer
// rtti::as<T>(U*) / rtti::cast<T>(U*) overloads are never shadowed; the
// entry-point SFINAE additionally excludes non-value_io pointers so an
// unrelated pointer source is a hard compile error instead of a silent lane.
namespace convert {

namespace detail {

// True for the scalar target families the bridge converts to.
template <typename T>
struct is_scalar_target : std::bool_constant<
    std::is_same<T, bool>::value || std::is_integral<T>::value ||
    std::is_floating_point<T>::value || std::is_same<T, std::string>::value ||
    std::is_same<T, string_view>::value || is_registered_enum<T>::value> {};

// True for a single-layer std::vector whose elements are scalar bridge
// targets (the only container the bridge accepts). Defined as a partial
// specialization so the trait never accesses `value_type` for a non-vector
// target (a plain `T::value_type` in an &&-guarded condition would still be a
// hard substitution error inside an if constexpr instantiation).
template <typename T, typename = void>
struct is_vector_of_scalar : std::false_type {};
template <typename U, typename A>
struct is_vector_of_scalar<std::vector<U, A>,
                           std::enable_if_t<is_scalar_target<U>::value>>
    : std::true_type {};

// --- Strict parse vs loose cast: intentional layering ----------------------
// The library's rtti::from_string<T> (to_string.hpp) is a STRICT parser:
// the whole input must be a well-formed literal of the target type, so
// from_string<int>("3.9") reports invalid_number ("3.9" is not an integer
// literal). This bridge is deliberately LOOSE: rtti::convert::as / cast
// convert the value, so as<int>("3.9") truncates the real lane and returns 3.
// The two entry points intentionally disagree on the same input: from_string
// parses, the bridge casts (see to_string.hpp for the mirrored note).

// Entry gate: the target is not a pointer and the source is either a
// non-pointer value, a character pointer (string literal / C string text), or
// a value_io-derived pointer. This keeps the bridge out of the existing
// rtti::as<T>(U*) / rtti::cast<T>(U*) pointer overload space while still
// accepting value_io* / const value_io* sources and C-string literals.
template <typename T, typename Src>
struct bridge_ok : std::bool_constant<
    !std::is_pointer<T>::value &&
    (!std::is_pointer<std::decay_t<Src>>::value ||
     std::is_convertible<std::decay_t<Src>, const char*>::value ||
     std::is_convertible<std::decay_t<Src>, const value_io*>::value)> {};

// Trim ASCII whitespace (spec: " 42 " parses as 42) without allocating.
inline string_view trim_view(string_view s) noexcept {
    const std::size_t n = s.size();
    std::size_t b = 0;
    while (b < n && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' ||
                     s[b] == '\r')) {
        ++b;
    }
    std::size_t e = n;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' ||
                     s[e - 1] == '\n' || s[e - 1] == '\r')) {
        --e;
    }
    return s.substr(b, e - b);
}

// Normalized source lane shared by every target dispatch.
enum class lane_kind : uint8_t {
    none,
    boolean,
    integer,
    uinteger,  // uint64 above INT64_MAX keeps its exact unsigned lane
    real,
    text,
    array
};

struct src_lane {
    lane_kind kind = lane_kind::none;
    bool b = false;
    std::int64_t i = 0;
    std::uint64_t u = 0;
    double d = 0.0;
    string_view s;
    const value_io* node = nullptr;  // array source (element iteration)
};

// Normalize any supported source into a src_lane. `e` receives the failure
// code for sources that carry no scalar lane (null / kind-mismatch).
template <typename Src>
inline src_lane extract_lane(Src&& src, error_code& e) noexcept {
    using S = std::decay_t<Src>;
    src_lane l;
    if constexpr (std::is_pointer<S>::value) {
        if constexpr (std::is_convertible<S, const char*>::value) {
            const char* cp = src;
            if (cp == nullptr) {
                e = error_code::no_such_field;
                return l;
            }
            l.kind = lane_kind::text;
            l.s = string_view(cp);
            return l;
        } else if constexpr (std::is_convertible<S, const value_io*>::value) {
            const value_io* node = src;
            if (node == nullptr) {
                e = error_code::no_such_field;
                return l;
            }
            switch (node->type()) {
                case value_io::kind::null:
                    e = error_code::no_such_field;
                    return l;
                case value_io::kind::boolean: {
                    bool v = false;
                    if (node->as_bool(v)) {
                        l.kind = lane_kind::boolean;
                        l.b = v;
                        return l;
                    }
                    break;
                }
                case value_io::kind::integer: {
                    std::int64_t v = 0;
                    if (node->as_integer(v)) {
                        l.kind = lane_kind::integer;
                        l.i = v;
                        return l;
                    }
                    break;
                }
                case value_io::kind::real: {
                    double v = 0.0;
                    if (node->as_real(v)) {
                        l.kind = lane_kind::real;
                        l.d = v;
                        return l;
                    }
                    break;
                }
                case value_io::kind::string: {
                    string_view v;
                    if (node->as_string(v)) {
                        l.kind = lane_kind::text;
                        l.s = v;
                        return l;
                    }
                    break;
                }
                case value_io::kind::array:
                    l.kind = lane_kind::array;
                    l.node = node;
                    return l;
                default:
                    break;
            }
            e = error_code::kind_mismatch;
            return l;
        } else {
            // Unreachable through the public entry points (bridge_ok excludes
            // unrelated object pointers); keep the internals total.
            e = error_code::kind_mismatch;
            return l;
        }
    } else if constexpr (std::is_same<S, std::nullptr_t>::value) {
        (void)src;
        e = error_code::no_such_field;
        return l;
    } else if constexpr (std::is_same<S, field_value>::value) {
        const field_value& f = src;
        switch (f.type()) {
            case field_value::kind::boolean:
                l.kind = lane_kind::boolean;
                l.b = f.as_bool();
                return l;
            case field_value::kind::integer:
                l.kind = lane_kind::integer;
                l.i = f.as_integer();
                return l;
            case field_value::kind::real:
                l.kind = lane_kind::real;
                l.d = f.as_real();
                return l;
            case field_value::kind::string:
                l.kind = lane_kind::text;
                l.s = f.as_string();
                return l;
            default:
                break;
        }
        e = error_code::kind_mismatch;
        return l;
    } else if constexpr (std::is_same<S, bool>::value) {
        l.kind = lane_kind::boolean;
        l.b = src;
        return l;
    } else if constexpr (std::is_integral<S>::value) {
        if constexpr (std::is_signed<S>::value) {
            l.kind = lane_kind::integer;
            l.i = static_cast<std::int64_t>(src);
        } else {
            const std::uint64_t u = static_cast<std::uint64_t>(src);
            if (u <= static_cast<std::uint64_t>(
                         (std::numeric_limits<std::int64_t>::max)())) {
                l.kind = lane_kind::integer;
                l.i = static_cast<std::int64_t>(u);
            } else {
                l.kind = lane_kind::uinteger;
                l.u = u;
            }
        }
        return l;
    } else if constexpr (std::is_floating_point<S>::value) {
        l.kind = lane_kind::real;
        l.d = static_cast<double>(src);
        return l;
    } else if constexpr (std::is_same<S, string_view>::value) {
        l.kind = lane_kind::text;
        l.s = src;
        return l;
    } else if constexpr (std::is_same<S, std::string>::value) {
        l.kind = lane_kind::text;
        l.s = string_view(src.data(), src.size());
        return l;
    } else {
        e = error_code::kind_mismatch;
        return l;
    }
}

// Text -> integral T through the existing scalar lane, with malformed text
// remapped from the converter's kind_mismatch to the bridge's invalid_number
// (the documented ill-formed-numeric-literal code); an out-of-range value is
// overflow. Integer text reads exactly through the uint64 then int64 lanes
// (so UINT64_MAX loses no precision in a double); fractional/exponent text
// goes through the truncated real path.
template <typename T>
inline error_code text_to_integral(string_view s, T& out) noexcept {
    if (s.empty()) {
        return error_code::invalid_number;
    }
    const error_code e = rtti::detail::scalar_text_to_integral<T>(s, out);
    return e == error_code::kind_mismatch ? error_code::invalid_number : e;
}

// Text -> floating T through the existing scalar lane, with malformed text
// remapped from the converter's kind_mismatch to the bridge's invalid_number.
template <typename T>
inline error_code text_to_real(string_view s, T& out) noexcept {
    if (s.empty()) {
        return error_code::invalid_number;
    }
    const error_code e = rtti::detail::scalar_text_to_real<T>(s, out);
    return e == error_code::kind_mismatch ? error_code::invalid_number : e;
}

// Integral target: every scalar lane applies its own conversion rule.
template <typename T>
inline result<T> lane_to_integral(const src_lane& l) noexcept {
    switch (l.kind) {
        case lane_kind::boolean: {
            T out{};
            const error_code e =
                rtti::detail::scalar_int64_to_integral<T>(l.b ? 1 : 0, out);
            return e == error_code::ok ? make_ok<T>(out) : make_error<T>(e);
        }
        case lane_kind::integer: {
            T out{};
            const error_code e =
                rtti::detail::scalar_int64_to_integral<T>(l.i, out);
            return e == error_code::ok ? make_ok<T>(out) : make_error<T>(e);
        }
        case lane_kind::uinteger: {
            T out{};
            const error_code e =
                rtti::detail::scalar_uint64_to_integral<T>(l.u, out);
            return e == error_code::ok ? make_ok<T>(out) : make_error<T>(e);
        }
        case lane_kind::real: {
            T out{};
            const error_code e =
                rtti::detail::scalar_real_to_integral<T>(l.d, out);
            return e == error_code::ok ? make_ok<T>(out) : make_error<T>(e);
        }
        case lane_kind::text: {
            const string_view t = trim_view(l.s);
            T out{};
            const error_code e = text_to_integral<T>(t, out);
            return e == error_code::ok ? make_ok<T>(out) : make_error<T>(e);
        }
        default:
            return make_error<T>(error_code::kind_mismatch);
    }
}

// Floating target: integer/real lanes cast directly (IEEE precision loss
// accepted, mirroring the scalar real-from lane); text is parsed.
template <typename T>
inline result<T> lane_to_real(const src_lane& l) noexcept {
    switch (l.kind) {
        case lane_kind::boolean:
            return make_error<T>(error_code::kind_mismatch);
        case lane_kind::integer:
            return make_ok<T>(static_cast<T>(l.i));
        case lane_kind::uinteger:
            return make_ok<T>(static_cast<T>(l.u));
        case lane_kind::real:
            return make_ok<T>(static_cast<T>(l.d));
        case lane_kind::text: {
            const string_view t = trim_view(l.s);
            T out{};
            const error_code e = text_to_real<T>(t, out);
            return e == error_code::ok ? make_ok<T>(out) : make_error<T>(e);
        }
        default:
            return make_error<T>(error_code::kind_mismatch);
    }
}

// Bool target: integer lane via the int<->bool rule (0 false, nonzero true);
// text accepts the "true"/"false"/"1"/"0" tokens; real has no bool lane.
inline result<bool> lane_to_bool(const src_lane& l) noexcept {
    switch (l.kind) {
        case lane_kind::boolean:
            return make_ok<bool>(l.b);
        case lane_kind::integer:
            return make_ok<bool>(l.i != 0);
        case lane_kind::uinteger:
            return make_ok<bool>(l.u != 0);
        case lane_kind::real:
            return make_error<bool>(error_code::kind_mismatch);
        case lane_kind::text: {
            const string_view t = trim_view(l.s);
            if (t == "true" || t == "1") {
                return make_ok<bool>(true);
            }
            if (t == "false" || t == "0") {
                return make_ok<bool>(false);
            }
            return make_error<bool>(error_code::invalid_number);
        }
        default:
            return make_error<bool>(error_code::kind_mismatch);
    }
}

// String target: text copies directly; integer/real lanes format through the
// existing numeric text lanes. The noexcept contract is kept by turning an
// allocation failure into a clean error instead of a thrown exception. There
// is no dedicated out-of-memory code in error.hpp; overflow is the closest
// general resource-limit code for allocation exhaustion (see json.cpp arena
// exhaustion -> overflow, C-4). Malformed text is never reported here: the
// text lanes return invalid_number at parse time, so a thrown exception on
// these paths is an allocation failure, not a format error.
inline result<std::string> lane_to_string(const src_lane& l) noexcept {
    switch (l.kind) {
        case lane_kind::boolean:
            return make_error<std::string>(error_code::kind_mismatch);
        case lane_kind::integer:
            try {
                return make_ok<std::string>(std::to_string(l.i));
            } catch (const std::bad_alloc&) {
                return make_error<std::string>(error_code::overflow);
            }
        case lane_kind::uinteger:
            try {
                return make_ok<std::string>(std::to_string(l.u));
            } catch (const std::bad_alloc&) {
                return make_error<std::string>(error_code::overflow);
            }
        case lane_kind::real: {
            std::string s;
            try {
                rtti::detail::scalar_format_real(l.d, s);
                return make_ok<std::string>(std::move(s));
            } catch (const std::bad_alloc&) {
                return make_error<std::string>(error_code::overflow);
            }
        }
        case lane_kind::text:
            try {
                return make_ok<std::string>(std::string(l.s.data(), l.s.size()));
            } catch (const std::bad_alloc&) {
                return make_error<std::string>(error_code::overflow);
            }
        default:
            return make_error<std::string>(error_code::kind_mismatch);
    }
}

// string_view target: only a text source is accepted (the view always points
// into the source's storage, so the caller owns its lifetime), matching the
// converter<string_view> lane.
inline result<string_view> lane_to_string_view(const src_lane& l) noexcept {
    switch (l.kind) {
        case lane_kind::text:
            return make_ok<string_view>(l.s);
        default:
            return make_error<string_view>(error_code::kind_mismatch);
    }
}

// Registered-enum target: label text via the enum table, or an integer lane
// range-checked against the enum's underlying type. Unregistered enums never
// reach this lane (the target dispatch reports kind_mismatch).
template <typename T>
inline result<T> lane_to_enum(const src_lane& l) noexcept {
    using UT = std::underlying_type_t<T>;
    switch (l.kind) {
        case lane_kind::text: {
            const string_view t = trim_view(l.s);
            for (std::size_t i = 0; i < enum_table<T>::count; ++i) {
                if (string_view(enum_table<T>::array[i].name) == t) {
                    return make_ok<T>(enum_table<T>::array[i].value);
                }
            }
            return make_error<T>(error_code::invalid_number);
        }
        case lane_kind::integer: {
            const std::int64_t iv = l.i;
            if constexpr (rtti::detail::is_plain_char<UT>::value) {
                // Plain char keeps its own representable-range gate: the
                // scalar lane treats char as an ASCII byte (0..255), which is
                // not the enum's underlying-type range; the gate uses
                // numeric_limits<UT> so an implementation where plain char is
                // unsigned still allows the full 0..255 underlying range.
                if (iv < static_cast<std::int64_t>(
                             (std::numeric_limits<UT>::min)()) ||
                    iv > static_cast<std::int64_t>(
                             (std::numeric_limits<UT>::max)())) {
                    return make_error<T>(error_code::overflow);
                }
                return make_ok<T>(static_cast<T>(static_cast<UT>(iv)));
            }
            UT tmp{};
            const error_code e =
                rtti::detail::scalar_int64_to_integral<UT>(iv, tmp);
            if (e != error_code::ok) {
                return make_error<T>(e);
            }
            return make_ok<T>(static_cast<T>(tmp));
        }
        case lane_kind::uinteger: {
            const std::uint64_t uv = l.u;
            if constexpr (rtti::detail::is_plain_char<UT>::value) {
                if (uv > static_cast<std::uint64_t>(
                             (std::numeric_limits<UT>::max)())) {
                    return make_error<T>(error_code::overflow);
                }
                return make_ok<T>(static_cast<T>(static_cast<UT>(uv)));
            }
            UT tmp{};
            const error_code e =
                rtti::detail::scalar_uint64_to_integral<UT>(uv, tmp);
            if (e != error_code::ok) {
                return make_error<T>(e);
            }
            return make_ok<T>(static_cast<T>(tmp));
        }
        default:
            return make_error<T>(error_code::kind_mismatch);
    }
}

// Dispatch a normalized lane onto the target scalar family.
template <typename T>
inline result<T> lane_to(const src_lane& l) noexcept {
    if constexpr (std::is_same<T, bool>::value) {
        return lane_to_bool(l);
    } else if constexpr (std::is_integral<T>::value) {
        return lane_to_integral<T>(l);
    } else if constexpr (std::is_floating_point<T>::value) {
        return lane_to_real<T>(l);
    } else if constexpr (std::is_same<T, std::string>::value) {
        return lane_to_string(l);
    } else if constexpr (std::is_same<T, string_view>::value) {
        return lane_to_string_view(l);
    } else if constexpr (is_registered_enum<T>::value) {
        return lane_to_enum<T>(l);
    } else {
        return make_error<T>(error_code::kind_mismatch);
    }
}

// std::vector<scalar> target: an array source converts element by element
// (propagating the first element failure), any non-array scalar source wraps
// into a single-element vector. The noexcept contract is kept by turning a
// vector allocation failure (reserve / push_back growth) into a clean error
// instead of a thrown exception; overflow is the resource-limit code for
// allocation exhaustion (see json.cpp arena exhaustion -> overflow, C-4).
template <typename T>
inline result<T> lane_to_vector(const src_lane& l) noexcept {
    using U = typename T::value_type;
    T out;
    try {
        if (l.kind == lane_kind::array) {
            const value_io* node = l.node;
            const std::size_t n = node->size();
            out.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                value_io* child = node->at(i);
                if (child == nullptr) {
                    return make_error<T>(error_code::kind_mismatch);
                }
                error_code e = error_code::ok;
                const src_lane cl = extract_lane(child, e);
                if (e != error_code::ok) {
                    return make_error<T>(e);
                }
                const result<U> r = lane_to<U>(cl);
                if (!r.ok()) {
                    return make_error<T>(r.err.code);
                }
                out.push_back(std::move(r.value));
            }
            return make_ok<T>(std::move(out));
        }
        if (l.kind == lane_kind::none) {
            return make_error<T>(error_code::kind_mismatch);
        }
        const result<U> r = lane_to<U>(l);
        if (!r.ok()) {
            return make_error<T>(r.err.code);
        }
        out.push_back(std::move(r.value));
        return make_ok<T>(std::move(out));
    } catch (const std::bad_alloc&) {
        return make_error<T>(error_code::overflow);
    }
}

// Shared strict-conversion body used by cast/to (and, through them, as).
template <typename T, typename Src>
inline result<T> cast_impl(Src&& src) noexcept {
    if constexpr (is_scalar_target<T>::value) {
        error_code e = error_code::ok;
        const src_lane l = extract_lane(src, e);
        if (e != error_code::ok) {
            return make_error<T>(e);
        }
        return lane_to<T>(l);
    } else if constexpr (is_vector_of_scalar<T>::value) {
        error_code e = error_code::ok;
        const src_lane l = extract_lane(src, e);
        if (e != error_code::ok) {
            return make_error<T>(e);
        }
        return lane_to_vector<T>(l);
    } else {
        return make_error<T>(error_code::kind_mismatch);
    }
}

} // namespace detail

// Loose conversion: nullopt on any failure, never throws.
template <typename T, typename Src,
          std::enable_if_t<detail::bridge_ok<T, Src>::value, int> = 0>
std::optional<T> as(Src&& src) noexcept {
    result<T> r = detail::cast_impl<T>(src);
    if (!r.ok()) {
        return std::nullopt;
    }
    return std::optional<T>(std::move(r.value));
}

// Strict conversion: result<T> with a distinguishable error_code.
template <typename T, typename Src,
          std::enable_if_t<detail::bridge_ok<T, Src>::value, int> = 0>
result<T> cast(Src&& src) noexcept {
    return detail::cast_impl<T>(std::forward<Src>(src));
}

// Unified entry alias: identical to cast.
template <typename T, typename Src,
          std::enable_if_t<detail::bridge_ok<T, Src>::value, int> = 0>
result<T> to(Src&& src) noexcept {
    return detail::cast_impl<T>(std::forward<Src>(src));
}

} // namespace convert

} // namespace rtti