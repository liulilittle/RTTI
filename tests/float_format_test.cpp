// Float-format unification tests (Task C2).
// detail::format_real is the single shortest round-tripping real formatter
// shared by the JSON/XML/INI serializers. This file pins its exact output
// (3.14 must serialize as "3.14", never the long-tail
// "3.1400000000000001") and proves all three formats use it.

#include "test_harness.hpp"

#include <rtti/convert.hpp>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace rtti_test_lib {

// Shortest round-trip form: the decimal parses back to the exact double and
// no long-tail digits leak out. Whole-valued reals carry a ".0" suffix so the
// text reads back as a floating-point token.
RTTI_TEST_CASE(format_real_shortest_roundtrip) {
    std::string s;
    rtti::detail::format_real(3.14, s);
    RTTI_CHECK_EQ(s, std::string("3.14"));
    RTTI_CHECK(std::strtod(s.c_str(), nullptr) == 3.14);

    s.clear();
    rtti::detail::format_real(1.0, s);
    RTTI_CHECK_EQ(s, std::string("1.0"));
    RTTI_CHECK(std::strtod(s.c_str(), nullptr) == 1.0);

    s.clear();
    rtti::detail::format_real(0.5, s);
    RTTI_CHECK_EQ(s, std::string("0.5"));
    RTTI_CHECK(std::strtod(s.c_str(), nullptr) == 0.5);
}

// Boundary doubles: the shortest form still round-trips exactly and preserves
// signed zero.
RTTI_TEST_CASE(format_real_edge_values) {
    std::string s;
    rtti::detail::format_real((std::numeric_limits<double>::max)(), s);
    RTTI_CHECK(std::strtod(s.c_str(), nullptr) == (std::numeric_limits<double>::max)());
    RTTI_CHECK(s.find('e') != std::string::npos || s.find('E') != std::string::npos);

    s.clear();
    rtti::detail::format_real((std::numeric_limits<double>::min)(), s);
    RTTI_CHECK(std::strtod(s.c_str(), nullptr) == (std::numeric_limits<double>::min)());
    RTTI_CHECK(s.find('e') != std::string::npos || s.find('E') != std::string::npos);

    s.clear();
    rtti::detail::format_real(0.0, s);
    RTTI_CHECK_EQ(s, std::string("0.0"));

    s.clear();
    rtti::detail::format_real(-0.0, s);
    RTTI_CHECK_EQ(s, std::string("-0.0"));
    RTTI_CHECK(std::signbit(std::strtod(s.c_str(), nullptr)));

    s.clear();
    rtti::detail::format_real(1e300, s);
    RTTI_CHECK(std::strtod(s.c_str(), nullptr) == 1e300);
    RTTI_CHECK(s.find('e') != std::string::npos || s.find('E') != std::string::npos);

    s.clear();
    rtti::detail::format_real(1e-300, s);
    RTTI_CHECK(std::strtod(s.c_str(), nullptr) == 1e-300);
    RTTI_CHECK(s.find('e') != std::string::npos || s.find('E') != std::string::npos);
}

// Non-finite input must never crash or produce an empty token (spec §0).
RTTI_TEST_CASE(format_real_nonfinite_tokens) {
    std::string s;
    rtti::detail::format_real(std::numeric_limits<double>::infinity(), s);
    RTTI_CHECK(!s.empty());

    s.clear();
    rtti::detail::format_real(-std::numeric_limits<double>::infinity(), s);
    RTTI_CHECK(!s.empty());

    s.clear();
    rtti::detail::format_real(std::numeric_limits<double>::quiet_NaN(), s);
    RTTI_CHECK(!s.empty());
}

// Subnormals (denorm_min and an extended range), lowest() and the sign of a
// negative near-zero value round-trip exactly through the shortcut decimal.
RTTI_TEST_CASE(format_real_subnormal_and_lowest) {
    std::string s;
    const double denorm = (std::numeric_limits<double>::denorm_min)();
    rtti::detail::format_real(denorm, s);
    RTTI_CHECK_EQ(std::strtod(s.c_str(), nullptr), denorm);
    RTTI_CHECK(!s.empty());

    s.clear();
    rtti::detail::format_real(-denorm, s);
    RTTI_CHECK_EQ(std::strtod(s.c_str(), nullptr), -denorm);

    s.clear();
    const double lowest = (std::numeric_limits<double>::lowest)();
    rtti::detail::format_real(lowest, s);
    RTTI_CHECK_EQ(std::strtod(s.c_str(), nullptr), lowest);
    RTTI_CHECK_EQ(s.front(), '-');

    s.clear();
    // A value equivalent to zero on comparison but carrying a sign: -0.0 keeps
    // its sign round-trip (the bare -0.0 token is covered above; the parse-side
    // equivalence is what callers rely on).
    RTTI_CHECK(std::strtod("-0.0", nullptr) == 0.0);
    RTTI_CHECK(std::signbit(std::strtod("-0.0", nullptr)));
}

} // namespace rtti_test_lib

// Three-format integration: every serializer must emit the short form and no
// long-tail "00000001" digits. The format headers are gated so a stripped
// build (all RTTI_ENABLE_* = 0) skips this section without tripping the
// header #error guards.
#if (!defined(RTTI_ENABLE_JSON) || RTTI_ENABLE_JSON) && \
    (!defined(RTTI_ENABLE_XML) || RTTI_ENABLE_XML) && \
    (!defined(RTTI_ENABLE_INI) || RTTI_ENABLE_INI)

#include <rtti/ini.hpp>
#include <rtti/json.hpp>
#include <rtti/xml.hpp>

namespace rtti_test_lib {

RTTI_TEST_CASE(float_short_format_three_formats) {
    const std::string j = rtti::json::serialize(3.14);
    RTTI_CHECK(j.find("3.14") != std::string::npos);
    RTTI_CHECK(j.find("00000001") == std::string::npos);

    const std::string x = rtti::xml::serialize(3.14);
    RTTI_CHECK(x.find("3.14") != std::string::npos);
    RTTI_CHECK(x.find("00000001") == std::string::npos);

    const std::string i = rtti::ini::serialize(3.14);
    RTTI_CHECK(i.find("3.14") != std::string::npos);
    RTTI_CHECK(i.find("00000001") == std::string::npos);
}

RTTI_TEST_CASE(float_roundtrip_json) {
    auto back = rtti::json::parse_as<double>(rtti::json::serialize(3.14));
    RTTI_CHECK(back.ok());
    RTTI_CHECK_EQ(back.value, 3.14);
}

} // namespace rtti_test_lib

#endif
