// Formatting tests (spec §7): date_time::format(pattern), format_number
// (integer/real)), the to_string<T>(v, spec) overload, node_to_string and
// reflection-driven format_reflected<T>.
// All names are prefixed Format* so they stay unique across translation units.
// The suite pins exact output text, so any change to the formatting rules is
// caught here.

#include "test_harness.hpp"
#include <rtti/date_time.hpp>
#include <rtti/number_format.hpp>
#include <rtti/to_string.hpp>

#include <cstdint>
#include <limits>
#include <string>

// Reflected structs for format_reflected / node tests (global scope).
struct FormatPoint {
    std::int64_t x = 0;
    std::int64_t y = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(FormatPoint, x, y)
};

struct FormatStats {
    std::int64_t id = 0;
    double score = 0.0;
    RTTI_DEFINE_TYPE_INTRUSIVE(FormatStats, id, score)
};

struct FormatNested {
    FormatPoint point;
    std::string label;
    RTTI_DEFINE_TYPE_INTRUSIVE(FormatNested, point, label)
};

namespace rtti_test_lib {

RTTI_TEST_CASE(FormatDatePatterns) {
    const auto r = rtti::date_time::from_unix_ns(std::int64_t(1791290096123456789));
    RTTI_CHECK(r.ok());
    const rtti::date_time t = r.value;

    // Full pattern and the composite directives.
    RTTI_CHECK_EQ(t.format("%Y-%m-%d %H:%M:%S.%f"),
                  std::string("2026-10-06 12:34:56.123456789"));
    RTTI_CHECK_EQ(t.format("%F %T"), std::string("2026-10-06 12:34:56"));
    RTTI_CHECK_EQ(t.format("%F"), std::string("2026-10-06"));
    RTTI_CHECK_EQ(t.format("%T"), std::string("12:34:56"));
    RTTI_CHECK_EQ(t.format("%R"), std::string("12:34"));

    // Individual directives.
    RTTI_CHECK_EQ(t.format("%Y"), std::string("2026"));
    RTTI_CHECK_EQ(t.format("%y"), std::string("26"));
    RTTI_CHECK_EQ(t.format("%m"), std::string("10"));
    RTTI_CHECK_EQ(t.format("%d"), std::string("06"));
    RTTI_CHECK_EQ(t.format("%H"), std::string("12"));
    RTTI_CHECK_EQ(t.format("%M"), std::string("34"));
    RTTI_CHECK_EQ(t.format("%S"), std::string("56"));
    RTTI_CHECK_EQ(t.format("%f"), std::string("123456789"));

    // Reordered components and timezone constants.
    RTTI_CHECK_EQ(t.format("%d/%m/%Y"), std::string("06/10/2026"));
    RTTI_CHECK_EQ(t.format("%H:%M %z %Z"), std::string("12:34 +0000 UTC"));
    RTTI_CHECK_EQ(t.format("%%Y"), std::string("%Y"));
    RTTI_CHECK_EQ(t.format("year %Y ok"), std::string("year 2026 ok"));
    RTTI_CHECK_EQ(t.format("%Y%m%d%H%M%S"), std::string("20261006123456"));

    // Whole-second instant: %f is exactly nine zero-padded digits.
    const auto w = rtti::date_time::from_unix_seconds(1791290096);
    RTTI_CHECK(w.ok());
    RTTI_CHECK_EQ(w.value.format("%f"), std::string("000000000"));
    RTTI_CHECK_EQ(w.value.format("%Y-%m-%dT%H:%M:%S"),
                  std::string("2026-10-06T12:34:56"));

    // The canonical no-pattern form is unchanged.
    RTTI_CHECK_EQ(t.format(), std::string("2026-10-06T12:34:56.123456789Z"));
    RTTI_CHECK_EQ(t.to_string(), std::string("2026-10-06T12:34:56.123456789Z"));
}

RTTI_TEST_CASE(FormatDateInvalidPatterns) {
    const auto r = rtti::date_time::from_unix_seconds(1791290096);
    RTTI_CHECK(r.ok());
    const rtti::date_time t = r.value;

    // Unknown directive -> empty string (fail-safe, never a crash).
    RTTI_CHECK_EQ(t.format("%Q"), std::string());
    RTTI_CHECK_EQ(t.format("%x"), std::string());
    RTTI_CHECK_EQ(t.format("%b"), std::string());
    // Trailing lone '%' and truncated patterns.
    RTTI_CHECK_EQ(t.format("%"), std::string());
    RTTI_CHECK_EQ(t.format("ab%"), std::string());
    RTTI_CHECK_EQ(t.format("%Y%"), std::string());
    // The error-code overload reports unexpected_token.
    std::string out;
    RTTI_CHECK_EQ(t.format("%Q", out), rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(t.format("x% f", out), rtti::error_code::unexpected_token);
    // A fully valid pattern reports ok and fills out.
    std::string ok_out;
    RTTI_CHECK_EQ(t.format("%Y-%m-%d", ok_out), rtti::error_code::ok);
    RTTI_CHECK_EQ(ok_out, std::string("2026-10-06"));
}

RTTI_TEST_CASE(FormatNumberInteger) {
    // Grouping + base examples from spec §7.2.
    RTTI_CHECK_EQ(rtti::format_number(1234567, "#,d"), std::string("1,234,567"));
    RTTI_CHECK_EQ(rtti::format_number(1234567, ","), std::string("1,234,567"));
    RTTI_CHECK_EQ(rtti::format_number(1234567), std::string("1234567"));
    RTTI_CHECK_EQ(rtti::format_number(-1234567, "#,d"),
                  std::string("-1,234,567"));

    // Signs.
    RTTI_CHECK_EQ(rtti::format_number(-42), std::string("-42"));
    RTTI_CHECK_EQ(rtti::format_number(42, "+"), std::string("+42"));
    RTTI_CHECK_EQ(rtti::format_number(42, " "), std::string(" 42"));
    RTTI_CHECK_EQ(rtti::format_number(-42, "+"), std::string("-42"));

    // Zero padding and width (the '0' flag pads after the sign).
    RTTI_CHECK_EQ(rtti::format_number(42, "08d"), std::string("00000042"));
    RTTI_CHECK_EQ(rtti::format_number(-42, "08d"), std::string("-0000042"));
    RTTI_CHECK_EQ(rtti::format_number(42, ".5d"), std::string("00042"));

    // Bases and alternate form.
    RTTI_CHECK_EQ(rtti::format_number(255, "x"), std::string("ff"));
    RTTI_CHECK_EQ(rtti::format_number(255, "X"), std::string("FF"));
    RTTI_CHECK_EQ(rtti::format_number(255, "#x"), std::string("0xff"));
    RTTI_CHECK_EQ(rtti::format_number(255, "#X"), std::string("0XFF")); // alt form uppercases the prefix too
    RTTI_CHECK_EQ(rtti::format_number(63, "o"), std::string("77"));
    RTTI_CHECK_EQ(rtti::format_number(63, "#o"), std::string("0o77"));
    RTTI_CHECK_EQ(rtti::format_number(255, "b"), std::string("11111111"));
    RTTI_CHECK_EQ(rtti::format_number(255, "#b"), std::string("0b11111111"));
    RTTI_CHECK_EQ(rtti::format_number(42, "08x"), std::string("0000002a"));
    RTTI_CHECK_EQ(rtti::format_number(-1, "x"), std::string("-1"));
    RTTI_CHECK_EQ(rtti::format_number(-1, "#x"), std::string("-0x1"));

    // Alignment and fill.
    RTTI_CHECK_EQ(rtti::format_number(42, "<6d"), std::string("42    "));
    RTTI_CHECK_EQ(rtti::format_number(42, ">6d"), std::string("    42"));
    RTTI_CHECK_EQ(rtti::format_number(42, "^6d"), std::string("  42  "));
    RTTI_CHECK_EQ(rtti::format_number(42, "0<6d"), std::string("420000"));
    RTTI_CHECK_EQ(rtti::format_number(-42, "0=6d"), std::string("-00042"));
    RTTI_CHECK_EQ(rtti::format_number(42, "8d"), std::string("      42"));

    // Wide lanes stay exact (no double round trip).
    RTTI_CHECK_EQ(rtti::format_number((std::numeric_limits<std::uint64_t>::max)()),
                  std::string("18446744073709551615"));
    RTTI_CHECK_EQ(rtti::format_number((std::uint64_t(0xDEADBEEF)), "X"),
                  std::string("DEADBEEF"));
    RTTI_CHECK_EQ(rtti::format_number((std::numeric_limits<std::int64_t>::min)()),
                  std::string("-9223372036854775808"));
    RTTI_CHECK_EQ(rtti::format_number((std::numeric_limits<std::int64_t>::min)(), "x"),
                  std::string("-8000000000000000"));
}

RTTI_TEST_CASE(FormatNumberReal) {
    // Fixed precision + grouping.
    RTTI_CHECK_EQ(rtti::format_number(1234567.0, ".2f"),
                  std::string("1234567.00"));
    RTTI_CHECK_EQ(rtti::format_number(1234567.0, ",.2f"),
                  std::string("1,234,567.00"));
    RTTI_CHECK_EQ(rtti::format_number(-1234567.8, ",.2f"),
                  std::string("-1,234,567.80"));
    RTTI_CHECK_EQ(rtti::format_number(3.14159, ".2f"), std::string("3.14"));
    RTTI_CHECK_EQ(rtti::format_number(3.14159, ".3f"), std::string("3.142"));
    RTTI_CHECK_EQ(rtti::format_number(0.0, ".1f"), std::string("0.0"));

    // Shortest round-tripping text is the default.
    RTTI_CHECK_EQ(rtti::format_number(3.14159), std::string("3.14159"));
    RTTI_CHECK_EQ(rtti::format_number(3.0), std::string("3"));
    RTTI_CHECK_EQ(rtti::format_number(1.5, "g"), std::string("1.5"));

    // Sign flags.
    RTTI_CHECK_EQ(rtti::format_number(-3.14159, ".2f"), std::string("-3.14"));
    RTTI_CHECK_EQ(rtti::format_number(3.14159, "+.2f"), std::string("+3.14"));

    // Scientific notation with normalized exponents.
    RTTI_CHECK_EQ(rtti::format_number(1234.5, ".2e"), std::string("1.23e+03"));
    RTTI_CHECK_EQ(rtti::format_number(1234.5, ".2E"), std::string("1.23E+03"));
    RTTI_CHECK_EQ(rtti::format_number(0.0001, ".2e"), std::string("1.00e-04"));

    // Width / align / zero-pad for reals.
    RTTI_CHECK_EQ(rtti::format_number(1234.5678, "^10.2f"),
                  std::string(" 1234.57  "));
    RTTI_CHECK_EQ(rtti::format_number(1234.5678, "08.2f"),
                  std::string("01234.57"));
    RTTI_CHECK_EQ(rtti::format_number(-1234.5, "08.1f"),
                  std::string("-01234.5"));

    // Non-finite tokens (deterministic, sign-aware).
    const double finf = (std::numeric_limits<double>::infinity)();
    const double ninf = -(std::numeric_limits<double>::infinity)();
    const double fnan = (std::numeric_limits<double>::quiet_NaN)();
    RTTI_CHECK_EQ(rtti::format_number(finf), std::string("inf"));
    RTTI_CHECK_EQ(rtti::format_number(ninf), std::string("-inf"));
    RTTI_CHECK_EQ(rtti::format_number(fnan), std::string("nan"));
    RTTI_CHECK_EQ(rtti::format_number(fnan, "+"), std::string("+nan"));
}

RTTI_TEST_CASE(FormatNumberInvalidSpec) {
    // An illegal spec reports unexpected_token and returns "" (spec §0).
    rtti::error_info e;
    RTTI_CHECK_EQ(rtti::format_number(42, "r", &e), std::string());
    RTTI_CHECK_EQ(e.code, rtti::error_code::unexpected_token);
    e = rtti::error_info{};
    RTTI_CHECK_EQ(rtti::format_number(42, ".d", &e), std::string());
    RTTI_CHECK_EQ(e.code, rtti::error_code::unexpected_token);
    e = rtti::error_info{};
    RTTI_CHECK_EQ(rtti::format_number(1.5, "z", &e), std::string());
    RTTI_CHECK_EQ(e.code, rtti::error_code::unexpected_token);
    e = rtti::error_info{};
    RTTI_CHECK_EQ(rtti::format_number(1.5, ".f", &e), std::string());
    RTTI_CHECK_EQ(e.code, rtti::error_code::unexpected_token);
    e = rtti::error_info{};
    // Trailing bytes after the type.
    RTTI_CHECK_EQ(rtti::format_number(42, "d ", &e), std::string());
    RTTI_CHECK_EQ(e.code, rtti::error_code::unexpected_token);
}

RTTI_TEST_CASE(FormatToStringOverload) {
    // The thin to_string(v, spec) overload.
    RTTI_CHECK_EQ(rtti::to_string(1234, "#,d"), std::string("1,234"));
    RTTI_CHECK_EQ(rtti::to_string(3.14159, ".2f"), std::string("3.14"));
    RTTI_CHECK_EQ(rtti::to_string(255, "#x"), std::string("0xff"));
    const auto r = rtti::date_time::from_unix_seconds(1791290096);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(rtti::to_string(r.value, "%Y"), std::string("2026"));
    RTTI_CHECK_EQ(rtti::to_string(r.value, "%F"), std::string("2026-10-06"));

    // The existing one-argument to_string is untouched.
    RTTI_CHECK_EQ(rtti::to_string(42), std::string("42"));
    RTTI_CHECK_EQ(rtti::to_string(3.5), std::string("3.5"));
    RTTI_CHECK_EQ(rtti::to_string(r.value), r.value.format());
}

RTTI_TEST_CASE(FormatNodeToString) {
    // Build a mixed tree through the shared value_io writer.
    rtti::detail::text_node n;
    n.make_object();
    rtti::value_io* c = n.make_node();
    c->make_scalar(rtti::field_value::of(std::int64_t(7)));
    n.set_key("n", c);
    rtti::value_io* x = n.make_node();
    x->make_scalar(rtti::field_value::of(3.5));
    n.set_key("x", x);
    rtti::value_io* s = n.make_node();
    s->make_scalar(
        rtti::field_value::of(rtti::string_view("hi")));
    n.set_key("s", s);
    rtti::value_io* arr = n.make_node();
    arr->make_array();
    rtti::value_io* a1 = arr->make_node();
    a1->make_scalar(rtti::field_value::of(std::int64_t(1)));
    arr->push_back(a1);
    rtti::value_io* a2 = arr->make_node();
    a2->make_scalar(rtti::field_value::of(std::int64_t(2)));
    arr->push_back(a2);
    n.set_key("arr", arr);
    rtti::value_io* b = n.make_node();
    b->make_scalar(rtti::field_value::of(true));
    n.set_key("b", b);

    RTTI_CHECK_EQ(rtti::node_to_string(&n),
                  std::string("{\"n\":7,\"x\":3.5,\"s\":\"hi\",\"arr\":[1,2],\"b\":true}"));
    // A real-only spec applies to the real field and falls back for integers.
    RTTI_CHECK_EQ(rtti::node_to_string(&n, ".2f"),
                  std::string("{\"n\":7,\"x\":3.50,\"s\":\"hi\",\"arr\":[1,2],\"b\":true}"));
    // An integer-only spec applies to the integers, reals fall back.
    RTTI_CHECK_EQ(rtti::node_to_string(&n, ",d"),
                  std::string("{\"n\":7,\"x\":3.5,\"s\":\"hi\",\"arr\":[1,2],\"b\":true}"));
    // A scalar node formats directly.
    RTTI_CHECK_EQ(rtti::node_to_string(c), std::string("7"));
    RTTI_CHECK_EQ(rtti::node_to_string(x, ".1f"), std::string("3.5"));

    // Fail-safe null argument.
    rtti::error_info e;
    RTTI_CHECK_EQ(rtti::node_to_string(nullptr, "", &e), std::string());
    RTTI_CHECK_EQ(e.code, rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(FormatReflectedToString) {
    FormatPoint pt;
    pt.x = 1;
    pt.y = 2;
    RTTI_CHECK_EQ(rtti::format_reflected(pt),
                  std::string("{\"x\":1,\"y\":2}"));

    FormatStats st;
    st.id = 7;
    st.score = 3.5;
    RTTI_CHECK_EQ(rtti::format_reflected(st),
                  std::string("{\"id\":7,\"score\":3.5}"));
    // The spec is applied to every numeric field (fixed precision here).
    RTTI_CHECK_EQ(rtti::format_reflected(st, ".2f"),
                  std::string("{\"id\":7,\"score\":3.50}"));

    // Nested reflected members serialize recursively.
    FormatNested nest;
    nest.point.x = 10;
    nest.point.y = 20;
    nest.label = "hit";
    RTTI_CHECK_EQ(rtti::format_reflected(nest),
                  std::string("{\"point\":{\"x\":10,\"y\":20},\"label\":\"hit\"}"));

    // Grouping reaches the integer fields.
    FormatPoint big;
    big.x = 1234567;
    big.y = 2;
    RTTI_CHECK_EQ(rtti::format_reflected(big, "#,d"),
                  std::string("{\"x\":1,234,567,\"y\":2}"));
}

} // namespace rtti_test_lib