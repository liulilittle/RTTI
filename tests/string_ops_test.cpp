// rtti::str string toolkit tests (spec Task 8a).
// Covers the full string-operation surface (split/join/trim/case/replace/
// repeat/pad/reverse/quote/escape) plus the generic conversion shells that
// forward to the existing rtti::to_string / rtti::from_string unified layer
// (scalars, enums, date_time, containers, optional, reflected structs) and the
// arbitrary-base bridge. Boundary cases: empty input, leading/trailing
// separators, zero repeat, over-width pad, and out-of-range numeric text, all
// fail-safe (spec 0, no crash).
//
// All type names are prefixed StringOps* so they stay unique across TUs.

#include "test_harness.hpp"
#include <rtti/string_ops.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace rtti_test_lib {

enum class StringOpsColor { Red = 1, Green = 2, Blue = 4 };

struct StringOpsPoint {
    int x = 0;
    int y = 0;
};

} // namespace rtti_test_lib

RTTI_DEFINE_ENUM(rtti_test_lib::StringOpsColor, Red, Green, Blue)
RTTI_DEFINE_TYPE(rtti_test_lib::StringOpsPoint, x, y)

namespace rtti_test_lib {

RTTI_TEST_CASE(StringOpsSplit) {
    RTTI_CHECK(rtti::str::split("a,b,,c", ',') ==
               std::vector<std::string>({"a", "b", "", "c"}));
    RTTI_CHECK(rtti::str::split("a,b,c", ',') ==
               std::vector<std::string>({"a", "b", "c"}));
    RTTI_CHECK(rtti::str::split("", ',') == std::vector<std::string>());
    RTTI_CHECK(rtti::str::split("a", ',') ==
               std::vector<std::string>({"a"}));
    // Leading / trailing separators keep the empty tokens at both edges.
    RTTI_CHECK(rtti::str::split(",a,", ',') ==
               std::vector<std::string>({"", "a", ""}));
    RTTI_CHECK(rtti::str::split(",", ',') ==
               std::vector<std::string>({"", ""}));
    // Multi-byte separator.
    RTTI_CHECK(rtti::str::split("a--b--c", "--") ==
               std::vector<std::string>({"a", "b", "c"}));
    RTTI_CHECK(rtti::str::split("a,b,c", ",") ==
               std::vector<std::string>({"a", "b", "c"}));
    // Empty separator string: the whole input is a single token.
    RTTI_CHECK(rtti::str::split("abc", "") ==
               std::vector<std::string>({"abc"}));
    // A separator equal to the whole input yields two empties (round trips).
    RTTI_CHECK(rtti::str::split("ab", "ab") ==
               std::vector<std::string>({"", ""}));
}

RTTI_TEST_CASE(StringOpsJoin) {
    RTTI_CHECK_EQ(rtti::str::join({"a", "b", "c"}, "-"), std::string("a-b-c"));
    RTTI_CHECK_EQ(rtti::str::join({}, "-"), std::string());
    RTTI_CHECK_EQ(rtti::str::join({"x"}, "-"), std::string("x"));
    RTTI_CHECK_EQ(rtti::str::join(std::vector<std::string>{}, "-"),
                  std::string());
    RTTI_CHECK_EQ(rtti::str::join(std::vector<std::string>{"a", "b"}, ","),
                  std::string("a,b"));
    RTTI_CHECK_EQ(rtti::str::join({"", ""}, ","), std::string(","));
    // split/join round trip with a trailing separator.
    const auto toks = rtti::str::split("a,b,,c", ',');
    RTTI_CHECK_EQ(rtti::str::join(toks, ","), std::string("a,b,,c"));
    RTTI_CHECK_EQ(rtti::str::join({std::string("ab"), std::string("c")}, "-"),
                  std::string("ab-c"));
}

RTTI_TEST_CASE(StringOpsTrim) {
    RTTI_CHECK_EQ(rtti::str::trim("  hi  "), std::string("hi"));
    RTTI_CHECK_EQ(rtti::str::ltrim("  hi  "), std::string("hi  "));
    RTTI_CHECK_EQ(rtti::str::rtrim("  hi  "), std::string("  hi"));
    RTTI_CHECK_EQ(rtti::str::trim("\t\r\n hi \n\t"), std::string("hi"));
    RTTI_CHECK_EQ(rtti::str::trim(""), std::string());
    RTTI_CHECK_EQ(rtti::str::trim("   "), std::string());
    RTTI_CHECK_EQ(rtti::str::ltrim(""), std::string());
    RTTI_CHECK_EQ(rtti::str::rtrim(""), std::string());
    RTTI_CHECK_EQ(rtti::str::trim("...hi...", "."), std::string("hi"));
    RTTI_CHECK_EQ(rtti::str::ltrim("...hi...", "."), std::string("hi..."));
    RTTI_CHECK_EQ(rtti::str::rtrim("...hi...", "."), std::string("...hi"));
    RTTI_CHECK_EQ(rtti::str::trim("x", "x"), std::string());
}

RTTI_TEST_CASE(StringOpsCase) {
    RTTI_CHECK_EQ(rtti::str::to_lower("AbC1"), std::string("abc1"));
    RTTI_CHECK_EQ(rtti::str::to_upper("AbC1"), std::string("ABC1"));
    RTTI_CHECK_EQ(rtti::str::to_lower(""), std::string());
    RTTI_CHECK_EQ(rtti::str::to_upper(""), std::string());
    RTTI_CHECK_EQ(rtti::str::to_lower("!@#"), std::string("!@#"));
    // Non-ASCII bytes pass through untouched (ASCII-only folding).
    RTTI_CHECK_EQ(rtti::str::to_lower("\xC3\xA9"), std::string("\xC3\xA9"));
    RTTI_CHECK_EQ(rtti::str::to_upper("xYz"), std::string("XYZ"));
}

RTTI_TEST_CASE(StringOpsStartsEnds) {
    RTTI_CHECK(rtti::str::starts_with("hello", "he"));
    RTTI_CHECK(!rtti::str::starts_with("hello", "heo"));
    RTTI_CHECK(!rtti::str::starts_with("he", "hello"));
    RTTI_CHECK(rtti::str::starts_with("", ""));
    RTTI_CHECK(rtti::str::starts_with("hello", ""));
    RTTI_CHECK(!rtti::str::starts_with("", "x"));
    RTTI_CHECK(rtti::str::ends_with("hello", "llo"));
    RTTI_CHECK(!rtti::str::ends_with("hello", "xlo"));
    RTTI_CHECK(!rtti::str::ends_with("llo", "hello"));
    RTTI_CHECK(rtti::str::ends_with("", ""));
    RTTI_CHECK(rtti::str::ends_with("hello", ""));
    RTTI_CHECK(!rtti::str::ends_with("", "x"));
}

RTTI_TEST_CASE(StringOpsReplace) {
    // First occurrence only.
    RTTI_CHECK_EQ(rtti::str::replace("aaa", "a", "b"), std::string("baa"));
    RTTI_CHECK_EQ(rtti::str::replace("abc", "x", "y"), std::string("abc"));
    RTTI_CHECK_EQ(rtti::str::replace("abab", "ab", "x"), std::string("xab"));
    RTTI_CHECK_EQ(rtti::str::replace("abc", "b", "BB"), std::string("aBBc"));
    // Empty needle: unchanged.
    RTTI_CHECK_EQ(rtti::str::replace("abc", "", "z"), std::string("abc"));
    // Replace with empty: removal of the first occurrence.
    RTTI_CHECK_EQ(rtti::str::replace("aXa", "X", ""), std::string("aa"));

    RTTI_CHECK_EQ(rtti::str::replace_all("aaa", "a", "b"),
                  std::string("bbb"));
    RTTI_CHECK_EQ(rtti::str::replace_all("ababab", "ab", "x"),
                  std::string("xxx"));
    RTTI_CHECK_EQ(rtti::str::replace_all("abc", "x", "y"),
                  std::string("abc"));
    RTTI_CHECK_EQ(rtti::str::replace_all("abc", "", "z"),
                  std::string("abc"));
    RTTI_CHECK_EQ(rtti::str::replace_all("aXbXc", "X", ""),
                  std::string("abc"));
    RTTI_CHECK_EQ(rtti::str::replace_all("aaaaaaaa", "aa", "a"),
                  std::string("aaaa"));
    RTTI_CHECK_EQ(rtti::str::replace_all("hello", "l", "L"),
                  std::string("heLLo"));
}

RTTI_TEST_CASE(StringOpsRepeat) {
    RTTI_CHECK_EQ(rtti::str::repeat("ab", 3), std::string("ababab"));
    RTTI_CHECK_EQ(rtti::str::repeat("ab", 0), std::string());
    RTTI_CHECK_EQ(rtti::str::repeat("", 5), std::string());
    RTTI_CHECK_EQ(rtti::str::repeat("x", 1), std::string("x"));
    RTTI_CHECK_EQ(rtti::str::repeat("xy", 2), std::string("xyxy"));
}

RTTI_TEST_CASE(StringOpsPad) {
    RTTI_CHECK_EQ(rtti::str::pad_left("ab", 5, '0'), std::string("000ab"));
    RTTI_CHECK_EQ(rtti::str::pad_right("ab", 5, '0'), std::string("ab000"));
    RTTI_CHECK_EQ(rtti::str::pad_left("abcd", 2, '0'), std::string("abcd"));
    RTTI_CHECK_EQ(rtti::str::pad_right("abcd", 4, '0'), std::string("abcd"));
    RTTI_CHECK_EQ(rtti::str::pad_left("ab", 3), std::string(" ab"));
    RTTI_CHECK_EQ(rtti::str::pad_right("ab", 3), std::string("ab "));
    RTTI_CHECK_EQ(rtti::str::pad_left("", 2, 'x'), std::string("xx"));
    RTTI_CHECK_EQ(rtti::str::pad_right("", 0, 'x'), std::string());
}

RTTI_TEST_CASE(StringOpsReverse) {
    RTTI_CHECK_EQ(rtti::str::reverse("abc"), std::string("cba"));
    RTTI_CHECK_EQ(rtti::str::reverse(""), std::string());
    RTTI_CHECK_EQ(rtti::str::reverse("a"), std::string("a"));
    RTTI_CHECK_EQ(rtti::str::reverse("ab"), std::string("ba"));
    RTTI_CHECK_EQ(rtti::str::reverse("a b c"), std::string("c b a"));
}

RTTI_TEST_CASE(StringOpsQuote) {
    RTTI_CHECK_EQ(rtti::str::quote("hi"), std::string("\"hi\""));
    RTTI_CHECK_EQ(rtti::str::quote(""), std::string("\"\""));
    RTTI_CHECK_EQ(rtti::str::quote("a\"b"), std::string("\"a\\\"b\""));
    RTTI_CHECK_EQ(rtti::str::quote(std::string("x\ny")),
                  std::string("\"x\\ny\""));
    // quote -> unquote round trip.
    const std::vector<std::string> samples = {
        "", "plain", "with quote \" inside", "ba\\ckslash", "line1\nline2",
        "\t tab \r cr \b bs \f ff"};
    for (const auto& s : samples) {
        std::string out;
        RTTI_CHECK_EQ(rtti::str::unquote(rtti::str::quote(s), out),
                      rtti::error_code::ok);
        RTTI_CHECK_EQ(out, s);
        RTTI_CHECK_EQ(rtti::str::unquote(rtti::str::quote(s)), s);
    }
    // unquote of a non-quoted string is an error (no crash).
    std::string out;
    RTTI_CHECK_EQ(rtti::str::unquote("plain", out),
                  rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::str::unquote("\"unterminated", out),
                  rtti::error_code::unterminated_string);
    RTTI_CHECK_EQ(rtti::str::unquote("\"\"", out), rtti::error_code::ok);
    RTTI_CHECK(out.empty());
    // Control-byte escape emitted as \u00XX decodes back.
    std::string ctrl("a\x01\x1Fz", 5);
    const std::string q = rtti::str::quote(ctrl);
    std::string dec;
    RTTI_CHECK_EQ(rtti::str::unquote(q, dec), rtti::error_code::ok);
    RTTI_CHECK_EQ(dec, ctrl);
}

RTTI_TEST_CASE(StringOpsEscape) {
    RTTI_CHECK_EQ(rtti::str::escape("a\"b"), std::string("a\\\"b"));
    RTTI_CHECK_EQ(rtti::str::escape("a\\b"), std::string("a\\\\b"));
    RTTI_CHECK_EQ(rtti::str::escape("x\ny"), std::string("x\\ny"));
    RTTI_CHECK_EQ(rtti::str::escape("\t"), std::string("\\t"));
    RTTI_CHECK_EQ(rtti::str::escape("\r"), std::string("\\r"));
    RTTI_CHECK_EQ(rtti::str::escape("\b"), std::string("\\b"));
    RTTI_CHECK_EQ(rtti::str::escape("\f"), std::string("\\f"));
    RTTI_CHECK_EQ(rtti::str::escape(""), std::string());
    // escape -> unescape round trip on every interesting byte.
    std::string raw;
    for (int i = 0; i < 256; ++i) {
        raw.push_back(static_cast<char>(i));
    }
    std::string dec;
    RTTI_CHECK_EQ(rtti::str::unescape(rtti::str::escape(raw), dec),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(dec, raw);
    // error mapping (fail-safe, no crash).
    std::string out;
    RTTI_CHECK_EQ(rtti::str::unescape("abc\\", out),
                  rtti::error_code::unterminated_string);
    RTTI_CHECK_EQ(rtti::str::unescape("\\q", out),
                  rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(rtti::str::unescape("\\u12xz", out),
                  rtti::error_code::invalid_utf8);
    RTTI_CHECK_EQ(rtti::str::unescape("\\u123", out),
                  rtti::error_code::invalid_utf8);
    RTTI_CHECK_EQ(rtti::str::unescape("", out), rtti::error_code::ok);
    RTTI_CHECK(out.empty());
    RTTI_CHECK_EQ(rtti::str::unescape("\\n"), std::string("\n"));
}

// Generic conversion shells forward to the existing rtti layer.
RTTI_TEST_CASE(StringOpsGenericToFrom) {
    RTTI_CHECK_EQ(rtti::str::to_string(42), std::string("42"));
    RTTI_CHECK_EQ(rtti::str::to_string(-7), std::string("-7"));
    RTTI_CHECK_EQ(rtti::str::to_string(3.5), std::string("3.5"));
    RTTI_CHECK_EQ(rtti::str::to_string(true), std::string("true"));
    RTTI_CHECK_EQ(rtti::str::to_string(false), std::string("false"));
    RTTI_CHECK_EQ(rtti::str::to_string(std::string("hi")), std::string("hi"));
    RTTI_CHECK_EQ(rtti::str::to_string(StringOpsColor::Green),
                  std::string("Green"));
    RTTI_CHECK_EQ(rtti::str::to_string(StringOpsPoint{3, 4}),
                  std::string("{\"x\":3,\"y\":4}"));
    RTTI_CHECK_EQ(rtti::str::to_string(std::vector<int>{1, 2, 3}),
                  std::string("[1,2,3]"));
    RTTI_CHECK_EQ(rtti::str::to_string(std::optional<int>(9)),
                  std::string("9"));
    RTTI_CHECK_EQ(rtti::str::to_string(std::optional<int>()),
                  std::string("null"));

    // Reverse via from_string.
    const auto iv = rtti::str::from_string<int>(std::string("-42"));
    RTTI_CHECK(iv.has_value());
    RTTI_CHECK_EQ(*iv, -42);
    double d = 0.0;
    RTTI_CHECK_EQ(rtti::str::from_string(std::string("3.25"), d),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(d, 3.25);
    const auto db = rtti::str::from_string<double>(std::string("3.25"));
    RTTI_CHECK(db.has_value());
    RTTI_CHECK_EQ(*db, 3.25);
    const auto col = rtti::str::from_string<StringOpsColor>(
        std::string("Blue"));
    RTTI_CHECK(col.has_value());
    RTTI_CHECK_EQ(*col, StringOpsColor::Blue);
    const auto p = rtti::str::from_string<StringOpsPoint>(
        std::string("{\"x\":1,\"y\":2}"));
    RTTI_CHECK(p.has_value());
    RTTI_CHECK_EQ(p->x, 1);
    RTTI_CHECK_EQ(p->y, 2);
    const auto vec = rtti::str::from_string<std::vector<int>>(
        std::string("[4,5,6]"));
    RTTI_CHECK(vec.has_value());
    RTTI_CHECK(*vec == std::vector<int>({4, 5, 6}));

    // date_time round trip through the shell.
    const auto dt = rtti::date_time::from_unix_seconds(
        std::int64_t(1700000000));
    RTTI_CHECK(dt.ok());
    const std::string iso = rtti::str::to_string(dt.value);
    RTTI_CHECK(iso.find('T') != std::string::npos);
    const auto dtb = rtti::str::from_string<rtti::date_time>(iso);
    RTTI_CHECK(dtb.has_value());
    RTTI_CHECK(*dtb == dt.value);
}

// fail-safe numeric boundaries through the existing conversion layer.
RTTI_TEST_CASE(StringOpsNumericBoundaries) {
    const auto big = rtti::str::from_string<int>(
        std::string("99999999999999999999999"));
    RTTI_CHECK(!big.has_value());
    int i = 0;
    RTTI_CHECK_EQ(rtti::str::from_string(std::string("99999999999999999999999"), i),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(rtti::str::from_string(std::string(""), i),
                  rtti::error_code::invalid_number);
    unsigned u = 0;
    RTTI_CHECK_EQ(rtti::str::from_string(std::string("-1"), u),
                  rtti::error_code::invalid_number);
    double d = 0.0;
    RTTI_CHECK_EQ(rtti::str::from_string(std::string("inf"), d),
                  rtti::error_code::ok);
    RTTI_CHECK(std::isinf(d));
    RTTI_CHECK_EQ(rtti::str::from_string(std::string("nan"), d),
                  rtti::error_code::ok);
    RTTI_CHECK(std::isnan(d));
}

RTTI_TEST_CASE(StringOpsBase) {
    RTTI_CHECK_EQ(rtti::str::to_base(255u, 16), std::string("ff"));
    RTTI_CHECK_EQ(rtti::str::to_base(255u, 2), std::string("11111111"));
    RTTI_CHECK_EQ(rtti::str::to_base(0u, 10), std::string("0"));
    RTTI_CHECK_EQ(rtti::str::to_base(std::numeric_limits<std::uint64_t>::max(), 16),
                  std::string("ffffffffffffffff"));
    const auto b = rtti::str::from_base(std::string("ff"), 16);
    RTTI_CHECK(b.has_value());
    RTTI_CHECK_EQ(*b, std::uint64_t(255));
    std::uint64_t raw = 0;
    RTTI_CHECK_EQ(rtti::str::from_base(std::string("ff"), 16, raw),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(raw, std::uint64_t(255));
    RTTI_CHECK_EQ(rtti::str::to_base_signed(-255, 10), std::string("-255"));
    const auto sgn = rtti::str::from_base_signed(std::string("-255"), 10);
    RTTI_CHECK(sgn.has_value());
    RTTI_CHECK_EQ(*sgn, std::int64_t(-255));
    // Overflow / invalid bases are fail-safe errors.
    std::uint64_t o = 0;
    RTTI_CHECK_EQ(rtti::str::from_base(std::string("18446744073709551616"), 10, o),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(rtti::str::from_base(std::string("1g"), 16, o),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::str::to_base(255u, 0), std::string());
    RTTI_CHECK_EQ(rtti::str::to_base(255u, 65), std::string());
}

} // namespace rtti_test_lib