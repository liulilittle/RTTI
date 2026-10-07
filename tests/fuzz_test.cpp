// R3 robustness-fuzz acceptance tests (spec section 7).
// Deterministic robustness probes for every format adapter:
//   * deterministic random byte streams (fixed-seed mt19937_64) that must parse
//     without crashing or throwing (a result with any ok/error is accepted),
//   * a 600-round random reflected-struct round trip through each adapter,
//   * null-byte injection into otherwise-valid documents (no crash).
// The random bytes derive from a fixed seed, so the exact corpus is
// reproducible across runs. Structs are Fuzz*-prefixed and defined at global
// scope; test functions live in rtti_test_lib. Format sections are gated so a
// stripped build (every RTTI_ENABLE_* off) still compiles without tripping the
// adapters' #error guards.

#include "test_harness.hpp"
#include <rtti/reflection.hpp>

// convert.hpp (core, no feature gate) provides the definitions behind the
// typed_field_access virtuals (member_to_value / from_value / _named). It must
// be included unconditionally: in a stripped build no format adapter is
// compiled, so without it the field-table virtuals would emit unresolved
// external references.
#include <rtti/convert.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace rtti_test_lib {

struct FuzzPoint {
    int x = 0;
    int y = 0;
};

struct FuzzData {
    int a = 0;
    double b = 0.0;
    bool c = false;
    std::string d;
    std::vector<int> e;
    FuzzPoint g;
};

} // namespace rtti_test_lib

// FuzzData registers with default-keep semantics: XML/INI serialize empty
// containers by omitting the element and a strict parse would then report the
// field missing (missing_key). Default-keep makes the round trip lossless for
// every format (an omitted empty container parses back as the empty default),
// which is exactly how the adapters are composed in practice.
RTTI_DEFINE_TYPE(rtti_test_lib::FuzzPoint, x, y)
RTTI_DEFINE_TYPE_WITH_DEFAULT(rtti_test_lib::FuzzData, a, b, c, d, e, g)

namespace rtti_test_lib {

RTTI_TEST_CASE(FuzzCoreShapeSanity) {
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_lib::FuzzData>(), size_t(6));
    RTTI_CHECK_EQ(rtti::field_count<rtti_test_lib::FuzzPoint>(), size_t(2));
}

} // namespace rtti_test_lib

// Random-byte probe: assert the parse never throws for the produced corpus.
#define RTTI_FUZZ_NO_THROW(PARSE_CALL)                     \
    do {                                                   \
        bool threw = false;                                \
        try {                                              \
            auto tmp_result = (PARSE_CALL);                \
            (void)tmp_result;                              \
        } catch (...) {                                    \
            threw = true;                                  \
        }                                                  \
        RTTI_CHECK(!threw);                                \
    } while (0)

#if (!defined(RTTI_ENABLE_JSON) || RTTI_ENABLE_JSON)

#include <rtti/json.hpp>

namespace rtti_test_lib {

RTTI_TEST_CASE(FuzzJsonRandomBytesNoThrow) {
    std::mt19937_64 rng(std::uint64_t(0x4A534F4E5A000008));
    for (int g = 0; g < 200; ++g) {
        std::string bytes;
        bytes.reserve(64);
        for (int i = 0; i < 64; ++i) {
            bytes.push_back(static_cast<char>(rng() & 0xFFu));
        }
        RTTI_FUZZ_NO_THROW(rtti::json::parse(bytes));
    }
}

RTTI_TEST_CASE(FuzzJsonStructRoundTrip) {
    std::mt19937_64 rng(std::uint64_t(0x6A534F4E7A000000));
    std::uniform_int_distribution<int> d_int(-1000, 1000);
    std::uniform_real_distribution<double> d_real(-1.0e6, 1.0e6);
    std::uniform_int_distribution<int> d_small(0, 7);
    static const char kAlphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        " \n\t\"\\{}[],:";
    std::uniform_int_distribution<std::size_t> d_char(0, sizeof(kAlphabet) - 2);
    for (int round = 0; round < 600; ++round) {
        rtti_test_lib::FuzzData in;
        in.a = d_int(rng);
        in.b = d_real(rng);
        in.c = (rng() & 1u) != 0;
        const std::size_t n = static_cast<std::size_t>(d_small(rng));
        in.d.clear();
        for (std::size_t k = 0; k < n; ++k) {
            in.d.push_back(kAlphabet[d_char(rng)]);
        }
        in.e.clear();
        const std::size_t ne = static_cast<std::size_t>(d_small(rng));
        for (std::size_t k = 0; k < ne; ++k) {
            in.e.push_back(d_int(rng));
        }
        in.g.x = d_int(rng);
        in.g.y = d_int(rng);

        const std::string s = rtti::json::serialize(in);
        const auto out = rtti::json::parse_as<rtti_test_lib::FuzzData>(s);
        RTTI_CHECK(out.ok());
        if (!out.ok()) {
            continue;
        }
        RTTI_CHECK(out.value.a == in.a);
        RTTI_CHECK(out.value.b == in.b);
        RTTI_CHECK(out.value.c == in.c);
        RTTI_CHECK(out.value.d == in.d);
        RTTI_CHECK(out.value.e == in.e);
        RTTI_CHECK(out.value.g.x == in.g.x);
        RTTI_CHECK(out.value.g.y == in.g.y);
    }
}

RTTI_TEST_CASE(FuzzJsonNullByteInjection) {
    // Null bytes after a valid JSON value must not crash the reader.
    RTTI_FUZZ_NO_THROW(rtti::json::parse(std::string("{\"a\":1}\0\0tail", 13)));
}

} // namespace rtti_test_lib

#endif

#if (!defined(RTTI_ENABLE_XML) || RTTI_ENABLE_XML)

#include <rtti/xml.hpp>

namespace rtti_test_lib {

RTTI_TEST_CASE(FuzzXmlRandomBytesNoThrow) {
    std::mt19937_64 rng(std::uint64_t(0x584D4C5A00000));
    for (int g = 0; g < 200; ++g) {
        std::string bytes;
        bytes.reserve(64);
        for (int i = 0; i < 64; ++i) {
            bytes.push_back(static_cast<char>(rng() & 0xFFu));
        }
        RTTI_FUZZ_NO_THROW(rtti::xml::parse(bytes));
    }
}

RTTI_TEST_CASE(FuzzXmlStructRoundTrip) {
    std::mt19937_64 rng(std::uint64_t(0x78584D4C7A000000));
    std::uniform_int_distribution<int> d_int(-1000, 1000);
    std::uniform_real_distribution<double> d_real(-1.0e6, 1.0e6);
    std::uniform_int_distribution<int> d_small(0, 7);
    static const char kAlphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
        "\n\t\"'<>&{}[],:";
    std::uniform_int_distribution<std::size_t> d_char(0, sizeof(kAlphabet) - 2);
    for (int round = 0; round < 600; ++round) {
        rtti_test_lib::FuzzData in;
        in.a = d_int(rng);
        in.b = d_real(rng);
        in.c = (rng() & 1u) != 0;
        const std::size_t n = static_cast<std::size_t>(d_small(rng));
        in.d.clear();
        for (std::size_t k = 0; k < n; ++k) {
            in.d.push_back(kAlphabet[d_char(rng)]);
        }
        in.e.clear();
        const std::size_t ne = static_cast<std::size_t>(d_small(rng));
        for (std::size_t k = 0; k < ne; ++k) {
            in.e.push_back(d_int(rng));
        }
        in.g.x = d_int(rng);
        in.g.y = d_int(rng);

        const std::string s = rtti::xml::serialize(in);
        const auto out = rtti::xml::parse_as<rtti_test_lib::FuzzData>(s);
        RTTI_CHECK(out.ok());
        if (!out.ok()) {
            continue;
        }
        RTTI_CHECK(out.value.a == in.a);
        RTTI_CHECK(out.value.b == in.b);
        RTTI_CHECK(out.value.c == in.c);
        RTTI_CHECK(out.value.d == in.d);
        RTTI_CHECK(out.value.e == in.e);
        RTTI_CHECK(out.value.g.x == in.g.x);
        RTTI_CHECK(out.value.g.y == in.g.y);
    }
}

RTTI_TEST_CASE(FuzzXmlNullByteInjection) {
    // A well-formed doc followed by NUL bytes must not crash the reader.
    RTTI_FUZZ_NO_THROW(rtti::xml::parse(
        std::string("<root><a>1</a></root>\0\0", 23)));
}

} // namespace rtti_test_lib

#endif

#if (!defined(RTTI_ENABLE_INI) || RTTI_ENABLE_INI)

#include <rtti/ini.hpp>

namespace rtti_test_lib {

RTTI_TEST_CASE(FuzzIniRandomBytesNoThrow) {
    std::mt19937_64 rng(std::uint64_t(0x494E495A0000000));
    for (int g = 0; g < 200; ++g) {
        std::string bytes;
        bytes.reserve(64);
        for (int i = 0; i < 64; ++i) {
            bytes.push_back(static_cast<char>(rng() & 0xFFu));
        }
        RTTI_FUZZ_NO_THROW(rtti::ini::parse(rtti::string_view(bytes)));
    }
}

RTTI_TEST_CASE(FuzzIniStructRoundTrip) {
    std::mt19937_64 rng(std::uint64_t(0x694E495A7A000000));
    std::uniform_int_distribution<int> d_int(-1000, 1000);
    std::uniform_real_distribution<double> d_real(-1.0e6, 1.0e6);
    std::uniform_int_distribution<int> d_small(0, 7);
    // INI is line-based: a value containing a newline (or tab-only content)
    // cannot round-trip through a key/value line, so the string alphabet here
    // stays single-line text. Structured value coverage (arrays/nested) comes
    // from the JSON/XML/TOML round trips and the dedicated INI tests.
    static const char kAlphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
        "._-+(){}&!";
    std::uniform_int_distribution<std::size_t> d_char(0, sizeof(kAlphabet) - 2);
    for (int round = 0; round < 600; ++round) {
        rtti_test_lib::FuzzData in;
        in.a = d_int(rng);
        in.b = d_real(rng);
        in.c = (rng() & 1u) != 0;
        const std::size_t n = static_cast<std::size_t>(d_small(rng));
        in.d.clear();
        for (std::size_t k = 0; k < n; ++k) {
            in.d.push_back(kAlphabet[d_char(rng)]);
        }
        in.e.clear();
        std::size_t ne = static_cast<std::size_t>(d_small(rng));
        if (ne == 1) {
            ne = 2;  // INI writes a single-element array as a bare scalar
        }
        for (std::size_t k = 0; k < ne; ++k) {
            in.e.push_back(d_int(rng));
        }
        in.g.x = d_int(rng);
        in.g.y = d_int(rng);

        const std::string s = rtti::ini::serialize(in);
        const auto out = rtti::ini::parse_as<rtti_test_lib::FuzzData>(
            rtti::string_view(s));
        RTTI_CHECK(out.ok());
        if (!out.ok()) {
            continue;
        }
        RTTI_CHECK(out.value.a == in.a);
        RTTI_CHECK(out.value.b == in.b);
        RTTI_CHECK(out.value.c == in.c);
        RTTI_CHECK(out.value.d == in.d);
        RTTI_CHECK(out.value.e == in.e);
        RTTI_CHECK(out.value.g.x == in.g.x);
        RTTI_CHECK(out.value.g.y == in.g.y);
    }
}

RTTI_TEST_CASE(FuzzIniNullByteInjection) {
    // A valid key/value line followed by NUL bytes must not crash.
    RTTI_FUZZ_NO_THROW(
        rtti::ini::parse(rtti::string_view(std::string("a = 1\n\0\0", 8))));
}

} // namespace rtti_test_lib

#endif

#if (!defined(RTTI_ENABLE_TOML) || RTTI_ENABLE_TOML)

#include <rtti/toml.hpp>

namespace rtti_test_lib {

RTTI_TEST_CASE(FuzzTomlRandomBytesNoThrow) {
    std::mt19937_64 rng(std::uint64_t(0x544F4D4CA000000));
    for (int g = 0; g < 200; ++g) {
        std::string bytes;
        bytes.reserve(64);
        for (int i = 0; i < 64; ++i) {
            bytes.push_back(static_cast<char>(rng() & 0xFFu));
        }
        RTTI_FUZZ_NO_THROW(rtti::toml::parse(bytes));
    }
}

RTTI_TEST_CASE(FuzzTomlStructRoundTrip) {
    std::mt19937_64 rng(std::uint64_t(0x744F4D4C7A000000));
    std::uniform_int_distribution<int> d_int(-1000, 1000);
    std::uniform_real_distribution<double> d_real(-1.0e6, 1.0e6);
    std::uniform_int_distribution<int> d_small(0, 7);
    static const char kAlphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
        "\n\t\"'[]=#.";
    std::uniform_int_distribution<std::size_t> d_char(0, sizeof(kAlphabet) - 2);
    for (int round = 0; round < 600; ++round) {
        rtti_test_lib::FuzzData in;
        in.a = d_int(rng);
        in.b = d_real(rng);
        in.c = (rng() & 1u) != 0;
        const std::size_t n = static_cast<std::size_t>(d_small(rng));
        in.d.clear();
        for (std::size_t k = 0; k < n; ++k) {
            in.d.push_back(kAlphabet[d_char(rng)]);
        }
        in.e.clear();
        const std::size_t ne = static_cast<std::size_t>(d_small(rng));
        for (std::size_t k = 0; k < ne; ++k) {
            in.e.push_back(d_int(rng));
        }
        in.g.x = d_int(rng);
        in.g.y = d_int(rng);

        const std::string s = rtti::toml::serialize(in);
        const auto out = rtti::toml::parse_as<rtti_test_lib::FuzzData>(s);
        RTTI_CHECK(out.ok());
        if (!out.ok()) {
            continue;
        }
        RTTI_CHECK(out.value.a == in.a);
        RTTI_CHECK(out.value.b == in.b);
        RTTI_CHECK(out.value.c == in.c);
        RTTI_CHECK(out.value.d == in.d);
        RTTI_CHECK(out.value.e == in.e);
        RTTI_CHECK(out.value.g.x == in.g.x);
        RTTI_CHECK(out.value.g.y == in.g.y);
    }
}

RTTI_TEST_CASE(FuzzTomlNullByteInjection) {
    // A valid key/value line followed by NUL bytes must not crash.
    RTTI_FUZZ_NO_THROW(
        rtti::toml::parse(std::string("a = 1\n\0\0", 8)));
}

} // namespace rtti_test_lib

#endif