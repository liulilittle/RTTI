#pragma once
// Unified T <-> string conversion layer (spec §16).
//
// Every supported type converts directly to/from a std::string through one
// pair of entry points:
//     rtti::to_string<T>(v)                      any type -> string
//     rtti::from_string<T>(s, T& out)            string  -> type (error_code)
//     rtti::from_string<T>(s)                    string  -> optional<T>
// plus the arbitrary-base integer helpers (spec §15/§16):
//     rtti::to_string_base / from_string_base    uint64 in any base 2..64
//     rtti::to_string_base_signed / from_string_base_signed
//
// Serialization forms:
//   * Scalars (bool / integers / floating point / strings / registered enums /
//     date_time) use a minimal text form and never build a tree:
//       bool       -> "true"/"false"
//       integral   -> decimal digits            (std::to_string)
//       float/dbl  -> shortest round-tripping text via std::to_chars, a ".0"
//                     suffix when the shortest form has no dot/exponent, and
//                     the deterministic tokens "inf"/"-inf"/"nan"
//       string     -> raw bytes, no quoting
//       enum       -> the registered label
//       date_time  -> canonical ISO 8601 UTC (exact sub-second round-trip)
//   * Containers / reflected structs / optional / variant / tuple / pair /
//     std::array / vector<bool> / native C arrays use a compact single-line
//     JSON value built through the existing rtti::to_value / rtti::from_value
//     dispatch chain (spec §16.2 "reuse value_io"). Object key order is the
//     source order (field declaration order for reflected structs); keys are
//     never sorted (spec §16.2 determinism).
//
// Architecture decision (documented per spec §16.2): to_string.hpp carries its
// OWN minimal in-header value_io tree (rtti::detail::text_node plus a
// compact-JSON serializer/parser) instead of reusing json::document. json.hpp
// is a format adapter gated by RTTI_ENABLE_JSON; depending on it would tie the
// string layer to an optional feature and break standalone stripping. There is
// no production zero-format-dependency tree in the reflect core (reflection.hpp
// forward-declares value_io only; memory.hpp provides just an arena;
// tests/box.hpp is a test double), so a small self-owning tree is the smallest
// surface that lets to_value/from_value build and read containers/new types.
// Container/reflected round trips therefore inherit the full existing dispatch
// (optional, variant, tuple, pair, array, vector<bool>, native/multi-dim C
// arrays, queue/stack/priority_queue, ADL hooks) for free.
//
// Error mapping (from_string): the JSON parse path reports the parser's codes
// (invalid_root / unexpected_token / unterminated_string / invalid_number /
// invalid_utf8 / too_deep); conversion kind mismatches report kind_mismatch /
// missing_key and numeric range violations report overflow (spec §0). Scalar
// fast paths report invalid_number for malformed text and overflow for values
// outside the target range. to_string returns an empty string when the
// conversion itself fails (e.g. an enum whose value has no label).
//
// Strictness contrast: from_string is a STRICT literal parser, while the
// rtti::convert::as / cast value bridge (convert.hpp) is a LOOSE conversion:
// from_string<int>("3.9") reports invalid_number, but convert::as<int>("3.9")
// truncates the real lane and returns 3. The split is intentional (parse vs
// cast); see convert.hpp for the forward-referencing note.
//
// Zero format dependency: this header includes only the reflect core
// (<rtti/convert.hpp> pulls in base_convert, date_time, error, naming and
// reflection). English comments only; C++17; no third-party dependencies.

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <rtti/convert.hpp>

#include <rtti/detail/from_chars_real.hpp>

namespace rtti {

namespace detail {

// --- Minimal in-header JSON tree -------------------------------------------
// text_node is the value_io sink/source used by the container/reflected path.
// Scalars hold a std::string payload by value; array/object children are owned
// through unique_ptr, so the whole tree frees itself recursively. Object keys
// are copied by value and stored in insertion order (deterministic, unsorted).
//
// Only the class declaration and the serializer/parser entry points live here;
// every non-template member, the compact single-line serializer
// (append_text_string / serialize_text) and the compact JSON parser
// (text_parser / parse_text) are defined in src/text_node.cpp so the tree
// compiles once instead of per-TU (spec §16.2).

struct text_node : value_io {
    value_io::kind k = value_io::kind::null;
    bool b = false;
    std::int64_t i = 0;
    double d = 0.0;
    std::string s;
    std::vector<std::unique_ptr<text_node>> arr;
    std::vector<std::pair<std::string, std::unique_ptr<text_node>>> obj;

    ~text_node() override;

    value_io::kind type() const noexcept override;
    bool as_bool(bool& out) const noexcept override;
    bool as_integer(std::int64_t& out) const noexcept override;
    bool as_real(double& out) const noexcept override;
    bool as_string(string_view& out) const noexcept override;
    std::size_t size() const noexcept override;
    value_io* at(std::size_t index) override;
    value_io* at(std::size_t index) const override;
    bool get_key(string_view key, value_io*& out) override;
    bool get_key(string_view key, value_io*& out) const override;
    bool get_key_at(std::size_t index, string_view& key,
                    value_io*& out) const override;
    value_io* make_array() override;
    void push_back(value_io* v) override;
    value_io* make_object() override;
    void set_key(string_view key, value_io* v) override;
    value_io* make_scalar(field_value v) override;
    value_io* make_node() override;

private:
    // make_node()-produced children not yet attached to arr/obj. Owning them
    // here closes the failure-path leak: every raw child pointer handed out by
    // make_node() is the unique_ptr below until a successful attach.
    std::unique_ptr<text_node> adopt_child(text_node* c);
    bool get_key_impl(string_view key, value_io*& out) const;
    std::vector<std::unique_ptr<text_node>> pending_;
};

// Compact single-line serializer and compact JSON parser. Declared here,
// defined in src/text_node.cpp. Deterministic: arrays/objects emit in source
// order, no sort_keys. Non-finite reals emit the bare tokens "inf"/"-inf"/"nan"
// so container floats round-trip exactly (the JSON adapters map them to null;
// here exact round-trip is more useful and the string layer owns its parser).
void append_text_string(const char* data, std::size_t len, std::string& out);
void serialize_text(const text_node& n, std::string& out);
result<std::unique_ptr<text_node>> parse_text(string_view src);

// --- Scalar parsing helpers ---------------------------------------------------

// Shortest round-tripping text for float/double (narrow to_chars handles the
// exact float range; format_real double is used for double). A ".0" suffix is
// appended when the shortest form has no dot or exponent.
template <typename F>
inline void append_shortest_real(F value, std::string& out) {
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
    // Inspect only the token just written; an earlier token's '.' in a shared
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

// Integral with full-input validation and range checking: the whole string
// must parse, malformed text is invalid_number and an out-of-range value is
// overflow (spec §0). Narrow types round-trip their cast to detect range loss.
template <typename T>
inline error_code parse_integral(string_view s, T& out) {
    if (s.empty()) {
        return error_code::invalid_number;
    }
    if constexpr (std::is_signed<T>::value) {
        std::int64_t v = 0;
        const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec == std::errc::result_out_of_range) {
            return error_code::overflow;
        }
        if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
            return error_code::invalid_number;
        }
        if (static_cast<std::int64_t>(static_cast<T>(v)) != v) {
            return error_code::overflow;
        }
        out = static_cast<T>(v);
        return error_code::ok;
    } else {
        std::uint64_t v = 0;
        const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec == std::errc::result_out_of_range) {
            return error_code::overflow;
        }
        if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
            return error_code::invalid_number;
        }
        if (static_cast<std::uint64_t>(static_cast<T>(v)) != v) {
            return error_code::overflow;
        }
        out = static_cast<T>(v);
        return error_code::ok;
    }
}

// Floating point with direct from_chars into T (exact float round-trip) and
// explicit non-finite tokens matching the serializer output.
template <typename T>
inline error_code parse_real(string_view s, T& out) {
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
        return error_code::invalid_number;
    }
    return error_code::ok;
}

// date_time: canonical ISO 8601 or a numeric Unix epoch (10/13/16/19/20
// decimal digits infer seconds/ms/us/ns, spec §14.1).
template <typename T>
inline error_code parse_date_time(string_view s, T& out) {
    const result<date_time> r = date_time::parse(s);
    if (r.ok()) {
        out = r.value;
        return error_code::ok;
    }
    std::size_t i = 0;
    if (i < s.size() && s[i] == '-') {
        ++i;
    }
    const std::size_t first = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
    }
    if (i != s.size() || i == first) {
        return r.err.code;
    }
    std::int64_t v = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec == std::errc::result_out_of_range) {
        return error_code::overflow;
    }
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
        return error_code::invalid_number;
    }
    std::int64_t ns = 0;
    const error_code e = epoch_ns_from_digits(v, ns);
    if (e != error_code::ok) {
        return e;
    }
    out = date_time(ns);
    return error_code::ok;
}

// True for a char-based std::basic_string (any traits/allocator); false for
// native arrays, string_view and non-char strings (wchar_t...). The void_t
// guard keeps typename T::value_type out of the condition for types that lack
// it (an array, for instance, would otherwise be a hard error inside the
// if constexpr condition of to_string/from_string).
template <typename T, typename = void>
struct is_char_basic_string : std::false_type {};
template <typename T>
struct is_char_basic_string<T, std::void_t<typename T::value_type>>
    : std::bool_constant<rtti::is_char_string<T>::value &&
                         std::is_same<typename T::value_type, char>::value> {};

} // namespace detail

// --- Public entry points ------------------------------------------------------

// Any supported type -> minimal text. Scalars direct; containers / reflected
// structs / new types through the compact JSON tree. Returns an empty string
// when the conversion itself fails (e.g. an enum value outside its label
// table).
template <typename T>
std::string to_string(const T& v) {
    if constexpr (std::is_same<T, bool>::value) {
        return v ? std::string("true") : std::string("false");
    } else if constexpr (std::is_integral<T>::value) {
        return std::to_string(v);
    } else if constexpr (std::is_floating_point<T>::value) {
        std::string out;
        rtti::detail::append_shortest_real(v, out);
        return out;
    } else if constexpr (std::is_same<T, std::string>::value ||
                         std::is_same<T, string_view>::value) {
        return std::string(v.data(), v.size());
    } else if constexpr (rtti::detail::is_char_basic_string<T>::value) {
        return std::string(v.data(), v.size());
    } else if constexpr (std::is_array<T>::value &&
                         std::is_same<std::remove_extent_t<T>, char>::value) {
        std::size_t len = 0;
        const std::size_t total = sizeof(v) / sizeof(v[0]);
        while (len < total && v[len] != '\0') {
            ++len;
        }
        return std::string(v, len);
    } else if constexpr (is_registered_enum<T>::value) {
        const char* name = enum_to_string(v);
        return (name != nullptr && name[0] != '\0') ? std::string(name)
                                                    : std::string();
    } else if constexpr (is_date_time<T>::value) {
        return v.format();
    } else if constexpr (std::is_enum<T>::value) {
        // An enum without a label table is not a supported type.
        return std::string();
    } else {
        // Container / reflected / new-type path: build a compact JSON value
        // through the existing to_value dispatch, then serialize one line.
        std::unique_ptr<rtti::detail::text_node> root(
            new rtti::detail::text_node);
        const error_code e = rtti::to_value(root.get(), v);
        if (e != error_code::ok) {
            return std::string();
        }
        std::string out;
        out.reserve(64);
        rtti::detail::serialize_text(*root, out);
        return out;
    }
}

// Federated string -> T. Scalars parse directly; containers / reflected
// structs / new types parse the compact JSON then run from_value.
template <typename T>
error_code from_string(string_view s, T& out) {
    if constexpr (std::is_same<T, bool>::value) {
        if (s == "true") {
            out = true;
            return error_code::ok;
        }
        if (s == "false") {
            out = false;
            return error_code::ok;
        }
        return error_code::kind_mismatch;
    } else if constexpr (std::is_integral<T>::value) {
        return rtti::detail::parse_integral(s, out);
    } else if constexpr (std::is_floating_point<T>::value) {
        return rtti::detail::parse_real(s, out);
    } else if constexpr (std::is_same<T, std::string>::value) {
        out.assign(s.data(), s.size());
        return error_code::ok;
    } else if constexpr (std::is_same<T, string_view>::value) {
        out = s;
        return error_code::ok;
    } else if constexpr (rtti::detail::is_char_basic_string<T>::value) {
        out.assign(s.data(), s.size());
        return error_code::ok;
    } else if constexpr (std::is_array<T>::value &&
                         std::is_same<std::remove_extent_t<T>, char>::value) {
        const std::size_t total = sizeof(out) / sizeof(out[0]);
        if (s.size() >= total) {
            return error_code::kind_mismatch; // over-long input, never overrun
        }
        std::memset(out, 0, sizeof(out));
        if (s.size() != 0) {
            std::memcpy(out, s.data(), s.size());
        }
        return error_code::ok;
    } else if constexpr (is_registered_enum<T>::value) {
        for (std::size_t i = 0; i < enum_table<T>::count; ++i) {
            if (string_view(enum_table<T>::array[i].name) == s) {
                out = enum_table<T>::array[i].value;
                return error_code::ok;
            }
        }
        return error_code::kind_mismatch;
    } else if constexpr (is_date_time<T>::value) {
        return rtti::detail::parse_date_time(s, out);
    } else if constexpr (std::is_enum<T>::value) {
        // An enum without a label table is not a supported type.
        return error_code::kind_mismatch;
    } else {
        // Container / reflected / new-type path.
        const result<std::unique_ptr<rtti::detail::text_node>> parsed =
            rtti::detail::parse_text(s);
        if (!parsed.ok()) {
            return parsed.err.code;
        }
        return rtti::from_value(parsed.value.get(), out);
    }
}

// Convenience: returns nullopt on any parse/conversion error.
template <typename T>
std::optional<T> from_string(string_view s) {
    T out{};
    if (from_string<T>(s, out) != error_code::ok) {
        return std::nullopt;
    }
    return out;
}

// --- Arbitrary-base integer bridge (spec §15.4 / §16.1) ----------------------
// Thin wrappers over rtti::detail::to_base / from_base (bases 2..64) with the
// optional custom digit table; signed variants carry a leading '-'.

inline std::string to_string_base(std::uint64_t value, unsigned base,
                                  const char* digits = nullptr) {
    return rtti::detail::to_base(value, base, digits);
}

inline error_code from_string_base(string_view s, unsigned base,
                                   std::uint64_t& out) {
    return rtti::detail::from_base(s, base, out);
}

inline std::optional<std::uint64_t> from_string_base(string_view s,
                                                     unsigned base) {
    std::uint64_t v = 0;
    if (rtti::detail::from_base(s, base, v) != error_code::ok) {
        return std::nullopt;
    }
    return v;
}

inline std::string to_string_base_signed(std::int64_t value, unsigned base) {
    return rtti::detail::to_base_signed(value, base);
}

inline error_code from_string_base_signed(string_view s, unsigned base,
                                          std::int64_t& out) {
    return rtti::detail::from_base_signed(s, base, out);
}

inline std::optional<std::int64_t> from_string_base_signed(string_view s,
                                                           unsigned base) {
    std::int64_t v = 0;
    if (rtti::detail::from_base_signed(s, base, v) != error_code::ok) {
        return std::nullopt;
    }
    return v;
}

} // namespace rtti
