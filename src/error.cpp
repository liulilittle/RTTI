// Error infrastructure implementation. English comments only.

#include <rtti/error.hpp>

namespace rtti {

const char* to_string(error_code code) noexcept {
    switch (code) {
        case error_code::ok: return "ok";
        case error_code::unexpected_token: return "unexpected token";
        case error_code::unterminated_string: return "unterminated string";
        case error_code::invalid_number: return "invalid number";
        case error_code::invalid_utf8: return "invalid utf8";
        case error_code::unterminated_comment: return "unterminated comment";
        case error_code::mismatched_tag: return "mismatched tag";
        case error_code::duplicate_attribute: return "duplicate attribute";
        case error_code::bad_entity: return "bad entity";
        case error_code::unterminated_cdata: return "unterminated cdata";
        case error_code::dtd_missing: return "dtd missing";
        case error_code::too_deep: return "too deep";
        case error_code::invalid_root: return "invalid root";
        case error_code::ini_unterminated_section: return "unterminated section";
        case error_code::ini_bad_line: return "bad ini line";
        case error_code::ini_missing_value: return "missing ini value";
        case error_code::no_such_field: return "no such field";
        case error_code::kind_mismatch: return "kind mismatch";
        case error_code::missing_key: return "missing key";
    }
    return "unknown error";
}

size_t error_info::line() const noexcept {
    if (!src) return 1;
    const size_t end = offset < src_len ? offset : src_len;
    size_t line = 1;
    for (size_t i = 0; i < end; ++i) {
        if (src[i] == '\n') ++line;
    }
    return line;
}

size_t error_info::column() const noexcept {
    if (!src) return 1;
    const size_t end = offset < src_len ? offset : src_len;
    size_t i = end;
    while (i > 0 && src[i - 1] != '\n') --i;
    return end - i + 1;
}

const char* error_info::message() const noexcept { return to_string(code); }

} // namespace rtti