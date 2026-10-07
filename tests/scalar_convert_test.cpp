// Type-safe conversion tolerance tests (spec §23).
// The converter main template's scalar `from` converts between safely
// convertible scalar kinds instead of requiring an identical kind: the
// integer family range-checks (overflow instead of silent wrap), int <-> bool
// follows 0/non-zero, int <-> float truncates with range checking, numeric
// <-> string parses/renders, and plain char acts as an ASCII byte (0..255).
// Every failure returns an error_code and leaves the target unchanged; no
// path may crash (spec §0).
//
// Test functions live in rtti_test_lib; all names are prefixed ScalarConv* so
// they stay unique across TUs.

#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include <rtti/to_string.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "box.hpp"

// The JSON scalar no-wrap probe below needs the JSON interface; gate the
// include so a stripped build (RTTI_ENABLE_JSON = 0) skips it without
// tripping json.hpp's #error guard.
#if (!defined(RTTI_ENABLE_JSON) || RTTI_ENABLE_JSON)
#include <rtti/json.hpp>
#endif

namespace rtti_test_lib {

inline box* make_real(double v) {
    box* b = new box;
    b->make_scalar(rtti::field_value::of(v));
    return b;
}
inline box* make_bool(bool v) {
    box* b = new box;
    b->make_scalar(rtti::field_value::of(v));
    return b;
}

// Read a source node into `out` and return the conversion error code.
template <typename T>
rtti::error_code scalarconv_read(const rtti::value_io* src, T& out) {
    return rtti::from_value(src, out);
}

// Read `in` back out of a fresh box and return the scalar kind it became.
template <typename T>
rtti::value_io::kind scalarconv_write(T in) {
    box* dst = new box;
    (void)rtti::to_value(dst, in);
    const rtti::value_io::kind k = dst->type();
    delete dst;
    return k;
}

RTTI_TEST_CASE(ScalarConvIntToIntegralFamily) {
    box* s = make_int(7);
    char c = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, c), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(c), 7);
    unsigned char uc = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, uc), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(uc), 7);
    short sh = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, sh), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(sh), 7);
    unsigned short us = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, us), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(us), 7);
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, i), rtti::error_code::ok);
    RTTI_CHECK_EQ(i, 7);
    unsigned int ui = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, ui), rtti::error_code::ok);
    RTTI_CHECK_EQ(ui, 7u);
    long l = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, l), rtti::error_code::ok);
    RTTI_CHECK_EQ(l, 7l);
    unsigned long ul = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, ul), rtti::error_code::ok);
    RTTI_CHECK_EQ(ul, 7ul);
    long long ll = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, ll), rtti::error_code::ok);
    RTTI_CHECK_EQ(ll, 7ll);
    unsigned long long ull = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, ull), rtti::error_code::ok);
    RTTI_CHECK_EQ(ull, 7ull);
    delete s;
}

RTTI_TEST_CASE(ScalarConvIntToBoolFloatString) {
    box* s = make_int(7);
    bool b = false;
    RTTI_CHECK_EQ(rtti::from_value(s, b), rtti::error_code::ok);
    RTTI_CHECK(b);
    float f = 0.0f;
    RTTI_CHECK_EQ(rtti::from_value(s, f), rtti::error_code::ok);
    RTTI_CHECK_EQ(f, 7.0f);
    double d = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(s, d), rtti::error_code::ok);
    RTTI_CHECK_EQ(d, 7.0);
    std::string str;
    RTTI_CHECK_EQ(rtti::from_value(s, str), rtti::error_code::ok);
    RTTI_CHECK_EQ(str, std::string("7"));
    delete s;

    box* zero = make_int(0);
    bool z = true;
    RTTI_CHECK_EQ(rtti::from_value(zero, z), rtti::error_code::ok);
    RTTI_CHECK(!z);
    std::string zs = "x";
    RTTI_CHECK_EQ(rtti::from_value(zero, zs), rtti::error_code::ok);
    RTTI_CHECK_EQ(zs, std::string("0"));
    delete zero;

    box* neg = make_int(-42);
    bool nb = true;
    RTTI_CHECK_EQ(rtti::from_value(neg, nb), rtti::error_code::ok);
    RTTI_CHECK(nb);
    std::string ns;
    RTTI_CHECK_EQ(rtti::from_value(neg, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::string("-42"));
    delete neg;
}

RTTI_TEST_CASE(ScalarConvIntToCharByteLane) {
    box* hi = make_int(255);
    char c = 0;
    RTTI_CHECK_EQ(rtti::from_value(hi, c), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<unsigned char>(c), 255u);
    RTTI_CHECK_EQ(scalarconv_write(c), rtti::value_io::kind::integer);
    box* back = new box;
    RTTI_CHECK_EQ(rtti::to_value(back, c), rtti::error_code::ok);
    std::int64_t v = 0;
    RTTI_CHECK(back->as_integer(v));
    RTTI_CHECK_EQ(v, std::int64_t(255));
    delete back;
    delete hi;

    box* over = make_int(256);
    char c2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(over, c2), rtti::error_code::overflow);
    delete over;

    box* under = make_int(-1);
    char c3 = 0;
    RTTI_CHECK_EQ(rtti::from_value(under, c3), rtti::error_code::overflow);
    delete under;

    box* ascii = make_int(65);
    char a = 0;
    RTTI_CHECK_EQ(rtti::from_value(ascii, a), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(a), 65);
    RTTI_CHECK_EQ(a, 'A');
    delete ascii;
}

RTTI_TEST_CASE(ScalarConvCharToIntRoundTrip) {
    // char value -> ASCII integer and back (spec §23.1).
    const char src = 'A';
    box* b = new box;
    RTTI_CHECK_EQ(rtti::to_value(b, src), rtti::error_code::ok);
    std::int64_t i = 0;
    RTTI_CHECK(b->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(65));
    char out = 0;
    RTTI_CHECK_EQ(rtti::from_value(b, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, src);
    delete b;
}

RTTI_TEST_CASE(ScalarConvNarrowOverflow) {
    box* big = make_int(2147483647ll);
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_value(big, i), rtti::error_code::ok);
    RTTI_CHECK_EQ(i, 2147483647);
    delete big;

    box* one_past = make_int(2147483648ll);
    int i2 = 99;
    RTTI_CHECK_EQ(rtti::from_value(one_past, i2), rtti::error_code::overflow);
    RTTI_CHECK_EQ(i2, 99); // untouched on failure
    delete one_past;

    box* min_ok = make_int(-2147483648ll);
    int i3 = 0;
    RTTI_CHECK_EQ(rtti::from_value(min_ok, i3), rtti::error_code::ok);
    RTTI_CHECK_EQ(i3, -2147483648);
    delete min_ok;

    box* below = make_int(-2147483649ll);
    int i4 = 0;
    RTTI_CHECK_EQ(rtti::from_value(below, i4), rtti::error_code::overflow);
    delete below;

    box* short_ok = make_int(32767);
    short sh = 0;
    RTTI_CHECK_EQ(rtti::from_value(short_ok, sh), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(sh), 32767);
    delete short_ok;
    box* short_over = make_int(32768);
    short sh2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(short_over, sh2), rtti::error_code::overflow);
    delete short_over;

    box* uchar_ok = make_int(200);
    unsigned char u = 0;
    RTTI_CHECK_EQ(rtti::from_value(uchar_ok, u), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(u), 200);
    delete uchar_ok;
    box* schar_over = make_int(200);
    signed char sc = 0;
    RTTI_CHECK_EQ(rtti::from_value(schar_over, sc), rtti::error_code::overflow);
    delete schar_over;
    box* schar_min = make_int(-128);
    signed char sc2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(schar_min, sc2), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(sc2), -128);
    delete schar_min;
}

RTTI_TEST_CASE(ScalarConvUnsignedOverflow) {
    box* ok = make_int(3000000000ll);
    unsigned int ui = 0;
    RTTI_CHECK_EQ(rtti::from_value(ok, ui), rtti::error_code::ok);
    RTTI_CHECK_EQ(ui, 3000000000u);
    delete ok;

    box* neg = make_int(-5);
    unsigned int ui2 = 7;
    RTTI_CHECK_EQ(rtti::from_value(neg, ui2), rtti::error_code::overflow);
    RTTI_CHECK_EQ(ui2, 7u);
    delete neg;

    box* over32 = make_int(4294967296ll);
    unsigned int ui3 = 0;
    RTTI_CHECK_EQ(rtti::from_value(over32, ui3), rtti::error_code::overflow);
    delete over32;

    box* u64ok = make_int(9223372036854775807ll);
    unsigned long long ull = 0;
    RTTI_CHECK_EQ(rtti::from_value(u64ok, ull), rtti::error_code::ok);
    RTTI_CHECK_EQ(ull, 9223372036854775807ull);
    delete u64ok;
}

RTTI_TEST_CASE(ScalarConvRealToIntTruncate) {
    box* r = make_real(3.7);
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_value(r, i), rtti::error_code::ok);
    RTTI_CHECK_EQ(i, 3);
    delete r;

    box* rn = make_real(-3.7);
    int i2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(rn, i2), rtti::error_code::ok);
    RTTI_CHECK_EQ(i2, -3);
    delete rn;

    box* whole = make_real(2.0);
    int i3 = 0;
    RTTI_CHECK_EQ(rtti::from_value(whole, i3), rtti::error_code::ok);
    RTTI_CHECK_EQ(i3, 2);
    delete whole;

    box* huge = make_real(1e300);
    int i4 = 0;
    RTTI_CHECK_EQ(rtti::from_value(huge, i4), rtti::error_code::overflow);
    delete huge;

    box* inf = make_real(std::numeric_limits<double>::infinity());
    int i5 = 0;
    RTTI_CHECK_EQ(rtti::from_value(inf, i5), rtti::error_code::overflow);
    delete inf;

    box* ninf = make_real(-std::numeric_limits<double>::infinity());
    int i6 = 0;
    RTTI_CHECK_EQ(rtti::from_value(ninf, i6), rtti::error_code::overflow);
    delete ninf;

    box* nan = make_real(std::numeric_limits<double>::quiet_NaN());
    int i7 = 0;
    RTTI_CHECK_EQ(rtti::from_value(nan, i7), rtti::error_code::overflow);
    delete nan;

    box* boundary = make_real(2147483647.0);
    int i8 = 0;
    RTTI_CHECK_EQ(rtti::from_value(boundary, i8), rtti::error_code::ok);
    RTTI_CHECK_EQ(i8, 2147483647);
    delete boundary;

    box* past = make_real(2147483648.0);
    int i9 = 0;
    RTTI_CHECK_EQ(rtti::from_value(past, i9), rtti::error_code::overflow);
    delete past;

    box* rmin = make_real(-2147483648.0);
    int i10 = 0;
    RTTI_CHECK_EQ(rtti::from_value(rmin, i10), rtti::error_code::ok);
    RTTI_CHECK_EQ(i10, -2147483648);
    delete rmin;
}

RTTI_TEST_CASE(ScalarConvRealToCharTruncate) {
    box* r = make_real(255.9);
    char c = 0;
    RTTI_CHECK_EQ(rtti::from_value(r, c), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<unsigned char>(c), 255u);
    delete r;

    box* over = make_real(256.0);
    char c2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(over, c2), rtti::error_code::overflow);
    delete over;

    box* frac_neg = make_real(-0.5);
    char c3 = 0;
    // Truncation toward zero: -0.5 -> 0, which is in the char lane.
    RTTI_CHECK_EQ(rtti::from_value(frac_neg, c3), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(c3), 0);
    delete frac_neg;

    box* neg = make_real(-1.0);
    char c4 = 0;
    RTTI_CHECK_EQ(rtti::from_value(neg, c4), rtti::error_code::overflow);
    delete neg;
}

RTTI_TEST_CASE(ScalarConvIntToFloat) {
    box* s = make_int(42);
    double d = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(s, d), rtti::error_code::ok);
    RTTI_CHECK_EQ(d, 42.0);
    float f = 0.0f;
    RTTI_CHECK_EQ(rtti::from_value(s, f), rtti::error_code::ok);
    RTTI_CHECK_EQ(f, 42.0f);
    delete s;

    // Precision loss for values past 2^53 is accepted per IEEE (spec §23.1).
    box* wide = make_int(9007199254740993ll);
    double d2 = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(wide, d2), rtti::error_code::ok);
    RTTI_CHECK_EQ(d2, 9007199254740992.0);
    delete wide;
}

RTTI_TEST_CASE(ScalarConvFloatRoundTrip) {
    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, 3.5), rtti::error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::real);
    double d = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(dst, d), rtti::error_code::ok);
    RTTI_CHECK_EQ(d, 3.5);
    delete dst;

    box* intdst = new box;
    RTTI_CHECK_EQ(rtti::to_value(intdst, 42), rtti::error_code::ok);
    RTTI_CHECK(intdst->type() == rtti::value_io::kind::integer);
    double d2 = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(intdst, d2), rtti::error_code::ok);
    RTTI_CHECK_EQ(d2, 42.0);
    delete intdst;
}

RTTI_TEST_CASE(ScalarConvBoolToInt) {
    box* t = make_bool(true);
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_value(t, i), rtti::error_code::ok);
    RTTI_CHECK_EQ(i, 1);
    unsigned long long ull = 0;
    RTTI_CHECK_EQ(rtti::from_value(t, ull), rtti::error_code::ok);
    RTTI_CHECK_EQ(ull, 1ull);
    delete t;

    box* f = make_bool(false);
    int i2 = 5;
    RTTI_CHECK_EQ(rtti::from_value(f, i2), rtti::error_code::ok);
    RTTI_CHECK_EQ(i2, 0);
    delete f;

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, true), rtti::error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::boolean);
    bool b = false;
    RTTI_CHECK_EQ(rtti::from_value(dst, b), rtti::error_code::ok);
    RTTI_CHECK(b);
    delete dst;
}

RTTI_TEST_CASE(ScalarConvStringToNumeric) {
    box* s = make_str("42");
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_value(s, i), rtti::error_code::ok);
    RTTI_CHECK_EQ(i, 42);
    delete s;

    box* sn = make_str("-17");
    int i2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(sn, i2), rtti::error_code::ok);
    RTTI_CHECK_EQ(i2, -17);
    delete sn;

    box* sfrac = make_str("12.9");
    int i3 = 0;
    RTTI_CHECK_EQ(rtti::from_value(sfrac, i3), rtti::error_code::ok);
    RTTI_CHECK_EQ(i3, 12);
    delete sfrac;

    box* sexp = make_str("1e2");
    int i4 = 0;
    RTTI_CHECK_EQ(rtti::from_value(sexp, i4), rtti::error_code::ok);
    RTTI_CHECK_EQ(i4, 100);
    delete sexp;

    box* d = make_str("3.14");
    double dv = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(d, dv), rtti::error_code::ok);
    RTTI_CHECK(std::fabs(dv - 3.14) < 1e-9);
    delete d;

    box* dint = make_str("42");
    double dv2 = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(dint, dv2), rtti::error_code::ok);
    RTTI_CHECK_EQ(dv2, 42.0);
    delete dint;

    box* bad = make_str("abc");
    int i5 = 77;
    RTTI_CHECK_EQ(rtti::from_value(bad, i5), rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(i5, 77);
    delete bad;

    box* empty = make_str("");
    int i6 = 0;
    RTTI_CHECK_EQ(rtti::from_value(empty, i6), rtti::error_code::kind_mismatch);
    delete empty;

    box* trail = make_str("12abc");
    int i7 = 0;
    RTTI_CHECK_EQ(rtti::from_value(trail, i7), rtti::error_code::kind_mismatch);
    delete trail;

    box* realbad = make_str("abc");
    double d3 = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(realbad, d3), rtti::error_code::kind_mismatch);
    delete realbad;

    box* inf = make_str("inf");
    double d4 = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(inf, d4), rtti::error_code::ok);
    RTTI_CHECK(std::isinf(d4) && d4 > 0);
    delete inf;

    box* ninf = make_str("-inf");
    double d5 = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(ninf, d5), rtti::error_code::ok);
    RTTI_CHECK(std::isinf(d5) && d5 < 0);
    delete ninf;

    box* nan = make_str("nan");
    double d6 = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(nan, d6), rtti::error_code::ok);
    RTTI_CHECK(std::isnan(d6));
    delete nan;
}

RTTI_TEST_CASE(ScalarConvStringOverflow) {
    box* i32over = make_str("2147483648");
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_value(i32over, i), rtti::error_code::overflow);
    delete i32over;

    box* i32min = make_str("-2147483649");
    int i2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(i32min, i2), rtti::error_code::overflow);
    delete i32min;

    box* i32minok = make_str("-2147483648");
    int i3 = 0;
    RTTI_CHECK_EQ(rtti::from_value(i32minok, i3), rtti::error_code::ok);
    RTTI_CHECK_EQ(i3, -2147483648);
    delete i32minok;

    box* i64max = make_str("9223372036854775807");
    long long ll = 0;
    RTTI_CHECK_EQ(rtti::from_value(i64max, ll), rtti::error_code::ok);
    RTTI_CHECK_EQ(ll, 9223372036854775807ll);
    delete i64max;

    box* i64over = make_str("9223372036854775808");
    long long ll2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(i64over, ll2), rtti::error_code::overflow);
    delete i64over;

    box* i64min = make_str("-9223372036854775808");
    long long ll3 = 0;
    RTTI_CHECK_EQ(rtti::from_value(i64min, ll3), rtti::error_code::ok);
    RTTI_CHECK_EQ(ll3, (-9223372036854775807ll - 1));
    delete i64min;

    box* u64max = make_str("18446744073709551615");
    unsigned long long u = 0;
    RTTI_CHECK_EQ(rtti::from_value(u64max, u), rtti::error_code::ok);
    RTTI_CHECK_EQ(u, (std::numeric_limits<unsigned long long>::max)());
    delete u64max;

    box* u64over = make_str("18446744073709551616");
    unsigned long long u2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(u64over, u2), rtti::error_code::overflow);
    delete u64over;

    box* u64toint = make_str("18446744073709551615");
    int i4 = 0;
    RTTI_CHECK_EQ(rtti::from_value(u64toint, i4), rtti::error_code::overflow);
    delete u64toint;

    box* schar = make_str("128");
    signed char sc = 0;
    RTTI_CHECK_EQ(rtti::from_value(schar, sc), rtti::error_code::overflow);
    delete schar;
    box* scharok = make_str("127");
    signed char sc2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(scharok, sc2), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(sc2), 127);
    delete scharok;
    box* uchar = make_str("255");
    unsigned char uc = 0;
    RTTI_CHECK_EQ(rtti::from_value(uchar, uc), rtti::error_code::ok);
    RTTI_CHECK_EQ(static_cast<int>(uc), 255);
    delete uchar;
    box* ucharover = make_str("256");
    unsigned char uc2 = 0;
    RTTI_CHECK_EQ(rtti::from_value(ucharover, uc2), rtti::error_code::overflow);
    delete ucharover;

    box* huge = make_str("99999999999999999999999");
    int i5 = 0;
    RTTI_CHECK_EQ(rtti::from_value(huge, i5), rtti::error_code::overflow);
    delete huge;

    box* rexpo = make_str("1e999");
    double d = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(rexpo, d), rtti::error_code::overflow);
    delete rexpo;
}

RTTI_TEST_CASE(ScalarConvNumericToString) {
    box* s = make_int(-12345);
    std::string out;
    RTTI_CHECK_EQ(rtti::from_value(s, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::string("-12345"));
    delete s;

    box* r = make_real(3.5);
    std::string out2;
    RTTI_CHECK_EQ(rtti::from_value(r, out2), rtti::error_code::ok);
    RTTI_CHECK_EQ(out2, std::string("3.5"));
    delete r;

    box* rwhole = make_real(2.0);
    std::string out3;
    RTTI_CHECK_EQ(rtti::from_value(rwhole, out3), rtti::error_code::ok);
    RTTI_CHECK_EQ(out3, std::string("2.0"));
    delete rwhole;

    // An unrelated kind (bool) still fails for a string target.
    box* b = make_bool(true);
    std::string out4 = "keep";
    RTTI_CHECK_EQ(rtti::from_value(b, out4), rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(out4, std::string("keep"));
    delete b;
}

RTTI_TEST_CASE(ScalarConvUnrelatedKindsRejected) {
    box* r = make_real(0.5);
    bool b = true;
    RTTI_CHECK_EQ(rtti::from_value(r, b), rtti::error_code::kind_mismatch);
    delete r;

    box* bl = make_bool(true);
    double d = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(bl, d), rtti::error_code::kind_mismatch);
    delete bl;

    box* b2 = make_bool(true);
    std::string s;
    RTTI_CHECK_EQ(rtti::from_value(b2, s), rtti::error_code::kind_mismatch);
    delete b2;

    box* str = make_str("true");
    bool b3 = false;
    RTTI_CHECK_EQ(rtti::from_value(str, b3), rtti::error_code::kind_mismatch);
    delete str;

    box* obj = make_obj();
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_value(obj, i), rtti::error_code::kind_mismatch);
    delete obj;
}

RTTI_TEST_CASE(ScalarConvTextVectorNoWrap) {
    // K-section review: narrow int targets must report overflow, never wrap.
    std::vector<int> v;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[2147483648]"), v),
                  rtti::error_code::overflow);
    std::vector<int> v2;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[9223372036854775807]"), v2),
                  rtti::error_code::overflow);

    std::vector<long long> v3;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[9223372036854775807]"), v3),
                  rtti::error_code::ok);
    RTTI_CHECK(v3.size() == 1 && v3[0] == 9223372036854775807ll);

    std::vector<int> v4;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[12.5]"), v4),
                  rtti::error_code::ok);
    RTTI_CHECK(v4.size() == 1 && v4[0] == 12);

    std::vector<int> v5;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[\"12\"]"), v5),
                  rtti::error_code::ok);
    RTTI_CHECK(v5.size() == 1 && v5[0] == 12);

    std::vector<int> v6;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[true]"), v6),
                  rtti::error_code::ok);
    RTTI_CHECK(v6.size() == 1 && v6[0] == 1);

    std::vector<int> v7;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[\"abc\"]"), v7),
                  rtti::error_code::kind_mismatch);
}

#if (!defined(RTTI_ENABLE_JSON) || RTTI_ENABLE_JSON)
RTTI_TEST_CASE(ScalarConvJsonScalarNoWrap) {
    auto over = rtti::json::parse_as<int>("2147483648");
    RTTI_CHECK(!over.ok());
    RTTI_CHECK_EQ(over.err.code, rtti::error_code::overflow);

    auto i64over = rtti::json::parse_as<int>("9223372036854775807");
    RTTI_CHECK(!i64over.ok());
    RTTI_CHECK_EQ(i64over.err.code, rtti::error_code::overflow);

    auto ok = rtti::json::parse_as<int>("42");
    RTTI_CHECK(ok.ok());
    RTTI_CHECK_EQ(ok.value, 42);
}
#endif

RTTI_TEST_CASE(ScalarConvNullSourceSafe) {
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_value<int>(nullptr, i),
                  rtti::error_code::no_such_field);
    std::string s;
    RTTI_CHECK_EQ(rtti::from_value<std::string>(nullptr, s),
                  rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(ScalarConvScalarRoundTrip) {
    const std::int64_t samples[] = {0, 1, -1, 42, -42, 255, 32767, -32768,
                                    2147483647, -2147483648};
    for (std::int64_t sample : samples) {
        box* b = new box;
        RTTI_CHECK_EQ(rtti::to_value(b, sample), rtti::error_code::ok);
        std::int64_t back = 0;
        RTTI_CHECK_EQ(rtti::from_value(b, back), rtti::error_code::ok);
        RTTI_CHECK_EQ(back, sample);
        delete b;
    }

    // Double round trip through a real scalar.
    box* d = new box;
    RTTI_CHECK_EQ(rtti::to_value(d, 2.5), rtti::error_code::ok);
    double dback = 0.0;
    RTTI_CHECK_EQ(rtti::from_value(d, dback), rtti::error_code::ok);
    RTTI_CHECK_EQ(dback, 2.5);
    delete d;

    // Bool round trip through a boolean scalar.
    box* t = new box;
    RTTI_CHECK_EQ(rtti::to_value(t, true), rtti::error_code::ok);
    bool tback = false;
    RTTI_CHECK_EQ(rtti::from_value(t, tback), rtti::error_code::ok);
    RTTI_CHECK(tback);
    delete t;
}

} // namespace rtti_test_lib
