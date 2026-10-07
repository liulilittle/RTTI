// Arbitrary-base integer <-> string conversion tests (spec §15):
//   rtti::detail::to_base / from_base (bases 2..64), the signed convenience
//   wrappers, overflow detection (never wrapping), malformed-input probes
//   (spec §0), the configurable digit table (RFC 4648 base64-style extras and
//   custom tables), the case policy (strtol-like for base <= 36, sensitive for
//   base > 36, explicit case-insensitive config), and the converter bridge
//   entry points that serialize integers as a base-N string through an
//   explicit output API while the plain to_value keeps numeric output
//   (spec §15.4).
// Structs are defined at global scope; test functions live in rtti_test_lib.
// All names are prefixed BaseConvert* so they stay unique across TUs.

#include "test_harness.hpp"
#include <rtti/base_convert.hpp>
#include <rtti/convert.hpp>
#include <rtti/date_time.hpp>

#include <cstdint>
#include <limits>
#include <random>
#include <string>

#include "box.hpp"

namespace rtti_test_lib {

namespace bc = rtti::detail;

static const unsigned kBaseConvertRoundTripBases[] = {
    2, 8, 10, 16, 32, 36, 62, 64};

// URL-safe extra symbols: '-' at index 62 and '_' at index 63.
static const char kBaseConvertUrlSafe[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";
// Uppercase-only 36-symbol table: exercises the case-sensitive custom path.
static const char kBaseConvertUpper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
// Table shorter than the base it claims to serve.
static const char kBaseConvertShort[] = "0123456789abcdef";

RTTI_TEST_CASE(BaseConvertKnownValues) {
    RTTI_CHECK_EQ(bc::to_base(0u, 2), std::string("0"));
    RTTI_CHECK_EQ(bc::to_base(0u, 10), std::string("0"));
    RTTI_CHECK_EQ(bc::to_base(0u, 64), std::string("0"));
    RTTI_CHECK_EQ(bc::to_base(255u, 16), std::string("ff"));
    RTTI_CHECK_EQ(bc::to_base(255u, 8), std::string("377"));
    RTTI_CHECK_EQ(bc::to_base(255u, 2), std::string("11111111"));
    RTTI_CHECK_EQ(bc::to_base(255u, 36), std::string("73"));
    RTTI_CHECK_EQ(bc::to_base(255u, 64), std::string("3/"));
    RTTI_CHECK_EQ(bc::to_base(255u, 63), std::string("43"));
    RTTI_CHECK_EQ(bc::to_base(10u, 10), std::string("10"));
    RTTI_CHECK_EQ(bc::to_base(42u, 62), std::string("G"));
    RTTI_CHECK_EQ(bc::to_base(4095u, 36), std::string("35r"));
    RTTI_CHECK_EQ(bc::to_base(46655u, 36), std::string("zzz"));
    // 'A' (index 36) vs 'a' (index 10) are distinct base-62 digits.
    RTTI_CHECK_EQ(bc::to_base(3843u, 62), std::string("ZZ"));
}

RTTI_TEST_CASE(BaseConvertFixedRoundTrip) {
    const std::uint64_t values[] = {
        0, 1, 2, 10, 35, 36, 61, 62, 63, 64, 255, 1024, 0xDEADBEEFull,
        0x8000000000000000ull, 1234567890123456789ull,
        (std::numeric_limits<std::uint64_t>::max)()};
    for (unsigned base : kBaseConvertRoundTripBases) {
        for (std::uint64_t v : values) {
            const std::string s = bc::to_base(v, base);
            std::uint64_t back = 0;
            RTTI_CHECK_EQ(bc::from_base(s, base, back), rtti::error_code::ok);
            RTTI_CHECK_EQ(back, v);
        }
    }
}

RTTI_TEST_CASE(BaseConvertAllBasesRoundTrip) {
    const std::uint64_t values[] = {
        0, 1, 7, 61, 62, 63, 255, 65535,
        (std::numeric_limits<std::uint64_t>::max)()};
    for (unsigned base = 2; base <= 64; ++base) {
        for (std::uint64_t v : values) {
            const std::string s = bc::to_base(v, base);
            std::uint64_t back = 0;
            RTTI_CHECK_EQ(bc::from_base(s, base, back), rtti::error_code::ok);
            RTTI_CHECK_EQ(back, v);
        }
    }
}

RTTI_TEST_CASE(BaseConvertBoundaryValues) {
    const std::uint64_t max = (std::numeric_limits<std::uint64_t>::max)();
    for (unsigned base = 2; base <= 64; ++base) {
        const std::string s = bc::to_base(max, base);
        RTTI_CHECK(!s.empty());
        std::uint64_t back = 0;
        RTTI_CHECK_EQ(bc::from_base(s, base, back), rtti::error_code::ok);
        RTTI_CHECK_EQ(back, max);
    }
    // Zero stays a single digit.
    RTTI_CHECK_EQ(bc::to_base(0u, 2), std::string("0"));
    RTTI_CHECK_EQ(bc::to_base(0u, 64), std::string("0"));
    // UINT64_MAX needs all 64 bits in base 2.
    RTTI_CHECK_EQ(bc::to_base(max, 2), std::string(64, '1'));
    std::uint64_t back = 0;
    RTTI_CHECK_EQ(bc::from_base(std::string(64, '1'), 2, back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(back, max);
}

RTTI_TEST_CASE(BaseConvertSignedKnown) {
    RTTI_CHECK_EQ(bc::to_base_signed(0, 10), std::string("0"));
    RTTI_CHECK_EQ(bc::to_base_signed(-1, 10), std::string("-1"));
    RTTI_CHECK_EQ(bc::to_base_signed(-255, 16), std::string("-ff"));
    RTTI_CHECK_EQ(bc::to_base_signed(255, 16), std::string("ff"));
    RTTI_CHECK_EQ(bc::to_base_signed((std::numeric_limits<std::int64_t>::min)(), 10),
                  std::string("-9223372036854775808"));
    RTTI_CHECK_EQ(bc::to_base_signed((std::numeric_limits<std::int64_t>::max)(), 10),
                  std::string("9223372036854775807"));
    // INT64_MIN magnitude = 2^63 = "1" followed by 63 zeros in base 2.
    RTTI_CHECK_EQ(bc::to_base_signed((std::numeric_limits<std::int64_t>::min)(), 2),
                  std::string("-1") + std::string(63, '0'));

    std::int64_t back = 0;
    RTTI_CHECK_EQ(bc::from_base_signed("-9223372036854775808", 10, back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(back, (std::numeric_limits<std::int64_t>::min)());
    RTTI_CHECK_EQ(bc::from_base_signed("9223372036854775807", 10, back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(back, (std::numeric_limits<std::int64_t>::max)());
    RTTI_CHECK_EQ(bc::from_base_signed("-1", 2, back), rtti::error_code::ok);
    RTTI_CHECK_EQ(back, std::int64_t(-1));
}

RTTI_TEST_CASE(BaseConvertSignedRoundTrip) {
    const std::int64_t values[] = {
        0, 1, -1, 42, -42, 123456789, -123456789,
        9223372036854775807ll, (-9223372036854775807ll - 1)};
    for (unsigned base : kBaseConvertRoundTripBases) {
        for (std::int64_t v : values) {
            const std::string s = bc::to_base_signed(v, base);
            std::int64_t back = 0;
            RTTI_CHECK_EQ(bc::from_base_signed(s, base, back),
                          rtti::error_code::ok);
            RTTI_CHECK_EQ(back, v);
        }
    }
}

RTTI_TEST_CASE(BaseConvertOverflow) {
    std::uint64_t out = 0;
    // 2^64 exactly: one past UINT64_MAX in decimal.
    RTTI_CHECK_EQ(bc::from_base("18446744073709551616", 10, out),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(bc::from_base("18446744073709551615", 10, out),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(out, (std::numeric_limits<std::uint64_t>::max)());
    // 64 one-bits fit exactly; 65 overflow.
    RTTI_CHECK_EQ(bc::from_base(std::string(64, '1'), 2, out),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(bc::from_base(std::string(65, '1'), 2, out),
                  rtti::error_code::overflow);
    // 16 'f' == 2^64-1; a 17th overflows.
    RTTI_CHECK_EQ(bc::from_base(std::string(16, 'f'), 16, out),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(bc::from_base(std::string(17, 'f'), 16, out),
                  rtti::error_code::overflow);
    // Any extra digit past the widest base-64 representation overflows.
    const std::string widest =
        bc::to_base((std::numeric_limits<std::uint64_t>::max)(), 64);
    RTTI_CHECK_EQ(bc::from_base(widest + '/', 64, out),
                  rtti::error_code::overflow);

    // Signed lanes: INT64_MAX + 1 and -(2^63) - 1 both overflow.
    std::int64_t so = 0;
    RTTI_CHECK_EQ(bc::from_base_signed("9223372036854775808", 10, so),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(bc::from_base_signed("-9223372036854775809", 10, so),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(bc::from_base_signed("-9223372036854775808", 10, so),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(so, (std::numeric_limits<std::int64_t>::min)());
}

RTTI_TEST_CASE(BaseConvertInvalidInput) {
    std::uint64_t out = 0;
    // Logged-invalid base numbers are rejected, never a crash.
    RTTI_CHECK_EQ(bc::from_base("10", 0, out), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base("10", 1, out), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base("10", 65, out), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base("10", 70, out), rtti::error_code::invalid_number);
    // Empty string.
    RTTI_CHECK_EQ(bc::from_base("", 10, out), rtti::error_code::invalid_number);
    // Characters outside the digit set of the base.
    RTTI_CHECK_EQ(bc::from_base("1g", 16, out), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base("1a", 10, out), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base("102", 2, out), rtti::error_code::invalid_number);
    // '/' is the 64th default symbol, so it is out of range for base 63.
    RTTI_CHECK_EQ(bc::from_base("3/", 63, out), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base("!", 64, out), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base(" ", 10, out), rtti::error_code::invalid_number);
    // 'z' is digit 35 of the default table; base 35 stops at 'y'.
    RTTI_CHECK_EQ(bc::from_base("z", 35, out), rtti::error_code::invalid_number);
    // Output side: invalid bases yield an empty string (no error_code channel).
    RTTI_CHECK_EQ(bc::to_base(255u, 0), std::string());
    RTTI_CHECK_EQ(bc::to_base(255u, 1), std::string());
    RTTI_CHECK_EQ(bc::to_base(255u, 70), std::string());
    RTTI_CHECK_EQ(bc::to_base(255u, 65), std::string());

    // Signed: bare/doubled/misplaced sign and sign-less magnitudes.
    std::int64_t so = 0;
    RTTI_CHECK_EQ(bc::from_base_signed("-", 10, so),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base_signed("+", 10, so),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base_signed("--1", 10, so),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base_signed("1-", 10, so),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base_signed("", 10, so),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base_signed("10", 0, so),
                  rtti::error_code::invalid_number);
}

RTTI_TEST_CASE(BaseConvertCustomDigits) {
    // URL-safe base64: '-' at index 62, '_' at index 63.
    RTTI_CHECK_EQ(bc::to_base(255u, 64, kBaseConvertUrlSafe),
                  std::string("3_"));
    RTTI_CHECK_EQ(bc::to_base(62u, 64, kBaseConvertUrlSafe), std::string("-"));
    RTTI_CHECK_EQ(bc::to_base(63u, 64, kBaseConvertUrlSafe), std::string("_"));
    RTTI_CHECK_EQ(bc::to_base(4095u, 64, kBaseConvertUrlSafe),
                  std::string("__"));
    // Round-trip through the same custom table.
    std::uint64_t back = 0;
    RTTI_CHECK_EQ(bc::from_base("3_", 64, back, kBaseConvertUrlSafe),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(back, std::uint64_t(255));
    // A table shorter than the base is rejected on both sides.
    RTTI_CHECK_EQ(bc::to_base(255u, 32, kBaseConvertShort), std::string());
    RTTI_CHECK_EQ(bc::from_base("ff", 32, back, kBaseConvertShort),
                  rtti::error_code::invalid_number);
}

RTTI_TEST_CASE(BaseConvertCasePolicy) {
    std::uint64_t out = 0;
    // base..36 parses letters case-insensitively (strtol-compatible).
    RTTI_CHECK_EQ(bc::from_base("ff", 16, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::uint64_t(255));
    RTTI_CHECK_EQ(bc::from_base("FF", 16, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::uint64_t(255));
    RTTI_CHECK_EQ(bc::from_base("DeAdBeEf", 16, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::uint64_t(0xDEADBEEF));
    // base > 36: 'a' and 'A' are distinct digits (case-sensitive by default).
    RTTI_CHECK_EQ(bc::from_base("a", 62, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::uint64_t(10));
    RTTI_CHECK_EQ(bc::from_base("A", 62, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::uint64_t(36));
    // Custom tables are case-sensitive by default; the explicit flag enables
    // case-insensitive fallback.
    RTTI_CHECK_EQ(bc::from_base("FF", 36, out, kBaseConvertUpper),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::uint64_t(555));
    RTTI_CHECK_EQ(bc::from_base("ff", 36, out, kBaseConvertUpper),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(bc::from_base("ff", 36, out, kBaseConvertUpper, true),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::uint64_t(555));
}

RTTI_TEST_CASE(BaseConvertFuzzRoundTrip) {
    std::mt19937_64 rng(std::uint64_t(0xC0FFEE));
    const std::uint64_t bases[] = {2, 3, 7, 8, 10, 16, 32, 36, 62, 63, 64};
    constexpr std::size_t nbases = sizeof(bases) / sizeof(bases[0]);
    for (int i = 0; i < 5000; ++i) {
        const std::uint64_t v = rng();
        const unsigned base = static_cast<unsigned>(bases[i % nbases]);
        const std::string s = bc::to_base(v, base);
        std::uint64_t back = 0;
        RTTI_CHECK_EQ(bc::from_base(s, base, back), rtti::error_code::ok);
        RTTI_CHECK_EQ(back, v);
    }
    for (int i = 0; i < 5000; ++i) {
        const std::int64_t v = static_cast<std::int64_t>(rng());
        const unsigned base = static_cast<unsigned>(bases[i % nbases]);
        const std::string s = bc::to_base_signed(v, base);
        std::int64_t back = 0;
        RTTI_CHECK_EQ(bc::from_base_signed(s, base, back),
                      rtti::error_code::ok);
        RTTI_CHECK_EQ(back, v);
    }
}

RTTI_TEST_CASE(BaseConvertMalformedProbe) {
    std::uint64_t out = 0;
    const char* bad_unsigned[] = {"",  " ",   "  ",  "0x10", "1_000",
                                  "1,000", "++1", "-1", "abc",  "!@#$"};
    for (const char* s : bad_unsigned) {
        RTTI_CHECK_EQ(bc::from_base(s, 10, out), rtti::error_code::invalid_number);
    }
    // Long all-valid digit runs overflow cleanly (no wrap, no crash).
    RTTI_CHECK_EQ(bc::from_base(std::string(100, '9'), 10, out),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(bc::from_base(std::string(100, '1'), 2, out),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(bc::from_base(std::string(100, 'f'), 16, out),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(bc::from_base(std::string(100, '/'), 64, out),
                  rtti::error_code::overflow);

    std::int64_t so = 0;
    const char* bad_signed[] = {"-", "+", "--1", "1-", "- 1", "  1", "-0x1", "1.5"};
    for (const char* s : bad_signed) {
        RTTI_CHECK_EQ(bc::from_base_signed(s, 10, so),
                      rtti::error_code::invalid_number);
    }
    RTTI_CHECK_EQ(bc::from_base_signed(std::string(100, '1'), 10, so),
                  rtti::error_code::overflow);
    // A NUL byte inside the string_view is not a digit, never a crash.
    const std::string with_nul("12\0 3", 5);
    RTTI_CHECK_EQ(bc::from_base(with_nul, 10, out),
                  rtti::error_code::invalid_number);
}

RTTI_TEST_CASE(BaseConvertConverterBridge) {
    // to_value_base writes a base-N string; from_value_base reads it back.
    rtti_test_lib::box* b = new rtti_test_lib::box;
    RTTI_CHECK_EQ(rtti::to_value_base(b, std::uint64_t(255), 16),
                  rtti::error_code::ok);
    rtti::string_view s;
    RTTI_CHECK(b->as_string(s));
    RTTI_CHECK_EQ(std::string(s), std::string("ff"));
    std::uint64_t back = 0;
    RTTI_CHECK_EQ(rtti::from_value_base(b, 16, back), rtti::error_code::ok);
    RTTI_CHECK_EQ(back, std::uint64_t(255));
    delete b;

    // Signed / negative, and a base-64 lane.
    b = new rtti_test_lib::box;
    RTTI_CHECK_EQ(rtti::to_value_base_signed(b, std::int64_t(-123), 10),
                  rtti::error_code::ok);
    RTTI_CHECK(b->as_string(s));
    RTTI_CHECK_EQ(std::string(s), std::string("-123"));
    std::int64_t sb = 0;
    RTTI_CHECK_EQ(rtti::from_value_base_signed(b, 10, sb), rtti::error_code::ok);
    RTTI_CHECK_EQ(sb, std::int64_t(-123));
    delete b;

    // Custom digits through the bridge.
    b = new rtti_test_lib::box;
    RTTI_CHECK_EQ(rtti::to_value_base(b, std::uint64_t(63), 64, kBaseConvertUrlSafe),
                  rtti::error_code::ok);
    RTTI_CHECK(b->as_string(s));
    RTTI_CHECK_EQ(std::string(s), std::string("_"));
    delete b;

    // date_time hookup (spec §15.4): an int64 epoch travels as base-N text.
    const auto dt = rtti::date_time::from_unix_ns(std::int64_t(1791290096123456789ll));
    RTTI_CHECK(dt.ok());
    b = new rtti_test_lib::box;
    RTTI_CHECK_EQ(rtti::to_value_base_signed(b, dt.value.epoch_ns(), 62),
                  rtti::error_code::ok);
    std::int64_t ns = 0;
    RTTI_CHECK_EQ(rtti::from_value_base_signed(b, 62, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, dt.value.epoch_ns());
    delete b;

    // Plain integer to_value keeps a numeric scalar (dispatch unchanged).
    b = new rtti_test_lib::box;
    RTTI_CHECK_EQ(rtti::to_value(b, std::int64_t(255)), rtti::error_code::ok);
    std::int64_t numeric = 0;
    RTTI_CHECK(b->as_integer(numeric));
    RTTI_CHECK_EQ(numeric, std::int64_t(255));
    delete b;

    // Non-string source -> kind_mismatch.
    b = new rtti_test_lib::box;
    b->make_scalar(rtti::field_value::of(std::int64_t(5)));
    std::uint64_t u = 0;
    RTTI_CHECK_EQ(rtti::from_value_base(b, 10, u), rtti::error_code::kind_mismatch);
    delete b;

    // Invalid base through the bridge -> invalid_number, node untouched.
    b = new rtti_test_lib::box;
    RTTI_CHECK_EQ(rtti::to_value_base(b, std::uint64_t(255), 1),
                  rtti::error_code::invalid_number);
    delete b;
}

} // namespace rtti_test_lib