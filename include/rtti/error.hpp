#pragma once
// Error infrastructure for RTTI.
// Provides error_code, error_info with lazy line/column, and result<T>.
// English comments only; C++17; no third-party dependencies.

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace rtti {

// Error codes shared by the reflect core and every format adapter
// (JSON/XML/INI/TOML/proto/flat).
enum class error_code : uint8_t {
    ok = 0,
    unexpected_token,
    unterminated_string,
    invalid_number,
    invalid_utf8,
    unterminated_comment,
    mismatched_tag,
    duplicate_attribute,
    bad_entity,
    unterminated_cdata,
    dtd_missing,
    too_deep,
    invalid_root,
    ini_unterminated_section,
    ini_bad_line,
    ini_missing_value,
    no_such_field,
    kind_mismatch,
    missing_key,
    proto_truncated,
    proto_bad_field,
    proto_type_mismatch,
    overflow,
    no_such_method
};

// Human-readable English message for a code. Never returns nullptr.
const char* to_string(error_code code) noexcept;

// Error details. line()/column() are computed lazily from the source buffer,
// so callers pay only when they actually need them.
struct error_info {
    error_code code = error_code::ok;
    const char* src = nullptr;   // entire source buffer (may be null)
    size_t src_len = 0;
    size_t offset = 0;           // byte offset of the error

    // 1-based line number of offset within src.
    size_t line() const noexcept;
    // 1-based column number of offset within src.
    size_t column() const noexcept;
    // Short English message equivalent to to_string(code).
    const char* message() const noexcept;
};

inline bool operator==(const error_info& a, const error_info& b) noexcept {
    return a.code == b.code && a.offset == b.offset;
}
inline bool operator!=(const error_info& a, const error_info& b) noexcept {
    return !(a == b);
}

// result<T> carries either a value or an error. T must be default-constructible.
template <typename T>
struct result {
    T value{};
    error_info err;

    bool ok() const noexcept { return err.code == error_code::ok; }
    explicit operator bool() const noexcept { return ok(); }
    T& operator*() noexcept { return value; }
    const T& operator*() const noexcept { return value; }
};

// result<void> specialization: no value payload.
template <>
struct result<void> {
    error_info err;

    bool ok() const noexcept { return err.code == error_code::ok; }
    explicit operator bool() const noexcept { return ok(); }
};

template <typename T>
result<T> make_ok(T v) {
    result<T> r;
    r.value = std::move(v);
    return r;
}

template <typename T>
result<T> make_error(error_code code) {
    result<T> r;
    r.err.code = code;
    return r;
}

} // namespace rtti