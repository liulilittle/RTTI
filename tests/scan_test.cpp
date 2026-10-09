// Shared SIMD/scalar structural-character scanning tests.
// Covers find_first_of / find_first_not_of / skip_whitespace and the
// JSON helper against a scalar reference implementation, including
// unaligned ranges (0..15 byte offsets) that exercise the SSE2 path,
// empty ranges, NUL handling, and multi-byte masks.
//
// NOTE: this file is self-registering. RTTI_TEST_CASE instantiates a
// static registration object that runs during static initialization, so
// no registry line is required in test_main.cpp. Do not compare pointers
// into the same string literal written twice: without /GF the compiler
// emits two distinct buffers, so bind each literal to a variable once.

#include "test_harness.hpp"
#include <rtti/detail/scan.hpp>

#include <cstring>
#include <string>

namespace rtti_test_lib {

namespace {

using rtti::detail::char_mask;
using rtti::detail::find_first_of;
using rtti::detail::find_first_not_of;
using rtti::detail::skip_whitespace;
using rtti::detail::is_structural_or_string;
using rtti::detail::json_structural_mask;
using rtti::detail::json_string_mask;
using rtti::detail::xml_special_mask;

// Scalar reference implementations (documented semantics of scan.hpp):
// bytes >= 128 are never in the mask; the NUL byte is a normal byte whose
// membership depends solely on the mask bit.
inline const char* ref_first_of(const char* b, const char* e, char_mask m) noexcept {
    for (const char* p = b; p != e; ++p) {
        if (m.test(static_cast<unsigned char>(*p))) return p;
    }
    return e;
}

inline const char* ref_first_not_of(const char* b, const char* e, char_mask m) noexcept {
    for (const char* p = b; p != e; ++p) {
        if (!m.test(static_cast<unsigned char>(*p))) return p;
    }
    return e;
}

inline const char* ref_skip_ws(const char* b, const char* e) noexcept {
    while (b != e && (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n')) ++b;
    return b;
}

} // namespace

RTTI_TEST_CASE(scan_find_first_of_basic) {
    const char* s = "abc,def";
    const std::size_t n = std::strlen(s);
    const char* lead = ",abc";
    const char* trail = "abc,";
    char_mask comma;
    comma.set(',');
    // Target present in the middle.
    RTTI_CHECK_EQ(find_first_of(s, s + n, comma), s + 3);
    // Target absent -> end.
    char_mask colon;
    colon.set(':');
    RTTI_CHECK_EQ(find_first_of(s, s + n, colon), s + n);
    // Target is the very first character (same literal variable, one buffer).
    RTTI_CHECK_EQ(find_first_of(lead, lead + 4, comma), lead);
    // Target is the very last character.
    RTTI_CHECK_EQ(find_first_of(trail, trail + 4, comma), trail + 3);
    // Empty mask -> nothing matches, returns end.
    RTTI_CHECK_EQ(find_first_of(s, s + n, char_mask{}), s + n);
}

RTTI_TEST_CASE(scan_find_first_of_multi_char_mask) {
    char_mask m = char_mask::from_chars(":,");
    const char* c1 = "x:y";
    RTTI_CHECK_EQ(find_first_of(c1, c1 + 3, m), c1 + 1);
    const char* c2 = "x,y";
    RTTI_CHECK_EQ(find_first_of(c2, c2 + 3, m), c2 + 1);
    const char* c3 = "x{}y";
    char_mask braces = char_mask::from_chars("{}");
    RTTI_CHECK_EQ(find_first_of(c3, c3 + 4, braces), c3 + 1);
    // High-ascii bytes (>127) must never match, even when mask covers 127.
    char_mask all;
    for (int i = 0; i < 128; ++i) all.set(i);
    const unsigned char hi[] = {0x80, 'a'};
    RTTI_CHECK_EQ(find_first_of(reinterpret_cast<const char*>(hi),
                                reinterpret_cast<const char*>(hi) + 2, all),
                  reinterpret_cast<const char*>(hi) + 1);
}

RTTI_TEST_CASE(scan_json_structural_mask) {
    const char* s = "{\"a\": [1,2]}";
    const std::size_t n = std::strlen(s);
    const char_mask json = json_structural_mask();
    // Every structural/string character is located at its exact offset.
    RTTI_CHECK_EQ(find_first_of(s, s + n, json), s + 0);     // '{'
    RTTI_CHECK_EQ(find_first_of(s + 1, s + n, json), s + 1); // '"'
    RTTI_CHECK_EQ(find_first_of(s + 2, s + n, json), s + 3); // '"'
    RTTI_CHECK_EQ(find_first_of(s + 4, s + n, json), s + 4); // ':'
    RTTI_CHECK_EQ(find_first_of(s + 5, s + n, json), s + 6); // '['
    RTTI_CHECK_EQ(find_first_of(s + 7, s + n, json), s + 8); // ','
    RTTI_CHECK_EQ(find_first_of(s + 9, s + n, json), s + 10); // ']'
    RTTI_CHECK_EQ(find_first_of(s + 11, s + n, json), s + 11); // '}'
    // Predicate helper agrees with the mask on every byte of the string.
    for (std::size_t i = 0; i < n; ++i) {
        RTTI_CHECK_EQ(is_structural_or_string(s + i, s + n),
                      json.test(static_cast<unsigned char>(s[i])));
    }
    // Non-structural chars in a plain string are not found.
    const char* plain = "alpha beta";
    RTTI_CHECK_EQ(find_first_of(plain, plain + std::strlen(plain), json),
                  plain + std::strlen(plain));
}

RTTI_TEST_CASE(scan_json_string_mask) {
    const char_mask m = json_string_mask();
    RTTI_CHECK(m.test('"'));
    RTTI_CHECK(m.test('\\'));
    const char s[] = "abc\"def\\ghi";
    const char* hit = find_first_of(s, s + 11, m);
    RTTI_CHECK(hit != nullptr && *hit == '"');
}

RTTI_TEST_CASE(scan_find_first_not_of) {
    char_mask a;
    a.set('a');
    const char* s1 = "aaaab";
    RTTI_CHECK_EQ(find_first_not_of(s1, s1 + 5, a), s1 + 4);
    // Everything matches -> end.
    const char* s2 = "aaaaa";
    RTTI_CHECK_EQ(find_first_not_of(s2, s2 + 5, a), s2 + 5);
    // First byte already outside the mask.
    const char* s3 = "baaaa";
    RTTI_CHECK_EQ(find_first_not_of(s3, s3 + 5, a), s3 + 0);
    // Empty mask -> the first byte is always "not of".
    RTTI_CHECK_EQ(find_first_not_of(s3, s3 + 5, char_mask{}), s3 + 0);
}

RTTI_TEST_CASE(scan_skip_whitespace) {
    const char* s1 = "  \t\r\nx";
    RTTI_CHECK_EQ(skip_whitespace(s1, s1 + 6), s1 + 5);
    const char* s2 = "x";
    RTTI_CHECK_EQ(skip_whitespace(s2, s2 + 1), s2 + 0);
    const char* s3 = "\n\n\n";
    RTTI_CHECK_EQ(skip_whitespace(s3, s3 + 3), s3 + 3);
    const char* s4 = " x";
    RTTI_CHECK_EQ(skip_whitespace(s4, s4 + 2), s4 + 1);
    // Mixed CRLF line endings.
    const char* s5 = " \r\n \t x";
    RTTI_CHECK_EQ(skip_whitespace(s5, s5 + 7), s5 + 6);
}

RTTI_TEST_CASE(scan_empty_range) {
    const char* p = "abc";
    char_mask m;
    m.set('a');
    RTTI_CHECK_EQ(find_first_of(p, p, m), p);
    RTTI_CHECK_EQ(find_first_not_of(p, p, m), p);
    RTTI_CHECK_EQ(skip_whitespace(p, p), p);
    RTTI_CHECK_EQ(skip_whitespace(p + 3, p + 3), p + 3);
}

RTTI_TEST_CASE(scan_unaligned_consistency) {
    // Exercise both the SSE2 path and the scalar reference across every
    // 0..15 byte alignment, for several text shapes.
    static const char* const texts[] = {
        "  {\"key\": [1, 2], \"s\": true }\n\t",      // structurals + whitespace, > 16 bytes
        "hello\nworld\tend\r\n",                       // no JSON structurals, only ws bytes
        ",\"abc\"[def]{gh}",                          // structurals packed at front
        "a b c",                                       // short, < 16 bytes (scalar tail only)
    };
    const char_mask json = json_structural_mask();
    std::string buf(80, 'z');
    for (int o = 0; o < 16; ++o) {
        for (const char* t : texts) {
            const std::size_t n = std::strlen(t);
            buf.replace(static_cast<std::size_t>(o), n, t, n);
            const char* b = buf.data() + o;
            const char* e = b + n;
            RTTI_CHECK_EQ(find_first_of(b, e, json), ref_first_of(b, e, json));
            RTTI_CHECK_EQ(find_first_not_of(b, e, json), ref_first_not_of(b, e, json));
            RTTI_CHECK_EQ(skip_whitespace(b, e), ref_skip_ws(b, e));
        }
    }
}

RTTI_TEST_CASE(scan_long_buffer_tail_and_boundary) {
    // Targets placed at block boundaries and inside the scalar tail of a
    // 64-byte region (relative positions 15, 16, 17, 31, 32, 47, 60). A
    // fresh buffer is built for every alignment so commas never leak
    // between iterations. The backing buffer is 80 bytes so every write
    // stays in bounds for all 0..15 offsets.
    for (int o = 0; o < 16; ++o) {
        std::string s(80, 'x');
        s[o + 15] = ',';
        s[o + 16] = ',';
        s[o + 17] = ',';
        s[o + 31] = ',';
        s[o + 32] = ',';
        s[o + 47] = ',';
        s[o + 60] = ',';
        char_mask comma;
        comma.set(',');
        RTTI_CHECK_EQ(find_first_of(s.data() + o, s.data() + o + 64, comma),
                      s.data() + o + 15);
    }
    // Absent target over a long buffer still returns end (scalar tail runs).
    std::string long_buf(1000, 'x');
    char_mask colon;
    colon.set(':');
    RTTI_CHECK_EQ(find_first_of(long_buf.data(), long_buf.data() + 1000, colon),
                  long_buf.data() + 1000);
}

RTTI_TEST_CASE(scan_nul_not_structural) {
    // A NUL byte inside the range is a plain byte: it is never a JSON
    // structural character and never a string quote.
    const char s[] = "ab\0,cd";
    const std::size_t n = sizeof(s) - 1;
    char_mask comma;
    comma.set(',');
    RTTI_CHECK_EQ(find_first_of(s, s + n, comma), s + 3);
    RTTI_CHECK(!is_structural_or_string(s + 2, s + n));
    // find_first_not_of stops at the NUL when everything else is in the mask.
    char_mask almost_all = char_mask::from_chars("ab,cd");
    RTTI_CHECK_EQ(find_first_not_of(s, s + n, almost_all), s + 2);
    // A mask with bit 0 set must not match the terminating NUL that lies
    // outside the scanned range [begin, end).
    const char* t = "abc,def";
    char_mask nul_bit;
    nul_bit.set(0);
    RTTI_CHECK_EQ(find_first_of(t, t + 7, nul_bit), t + 7);
}

RTTI_TEST_CASE(scan_xml_special_mask) {
    const char_mask xml = xml_special_mask();
    RTTI_CHECK(xml.test('<'));
    RTTI_CHECK(xml.test('>'));
    RTTI_CHECK(xml.test('&'));
    RTTI_CHECK(xml.test('"'));
    RTTI_CHECK(xml.test('\''));
    RTTI_CHECK(!xml.test('a'));
    // Successive scan over the whole snippet finds exactly the special bytes:
    // 0 '<'  8 '"'  10 '"'  11 '>'  13 '<'  16 '>'  17 '&'
    const char* s = "<a href=\"x\">y</a>&amp;";
    const std::size_t n = std::strlen(s);
    const std::size_t want[] = {0, 8, 10, 11, 13, 16, 17};
    std::size_t k = 0;
    const char* p = s;
    for (;;) {
        p = find_first_of(p, s + n, xml);
        if (p == s + n) break;
        RTTI_CHECK(xml.test(static_cast<unsigned char>(*p)));
        RTTI_CHECK_EQ(static_cast<std::size_t>(p - s), want[k]);
        ++k;
        ++p;
    }
    RTTI_CHECK_EQ(k, std::size_t(7));
    // Nothing special after the last '&'.
    RTTI_CHECK_EQ(find_first_of(&s[18], s + n, xml), s + n);
}

} // namespace rtti_test_lib