// DateTime serialization tests (spec §14): rtti::date_time value type,
// ISO 8601 parse/format round trips, digit-count precision auto-detection,
// overflow/malformed inputs returning error_code (never a crash), and the
// converter bridge so a reflected struct holding a date_time field round-trips
// through the JSON/XML/INI/TOML adapters (numeric seconds default plus ISO
// string form).
// Structs are defined at global scope; test functions live in rtti_test_lib.
// All names are prefixed DateTime* so they stay unique across translation units.

#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include <rtti/date_time.hpp>
#include <rtti/ini.hpp>
#include <rtti/json.hpp>
#include <rtti/toml.hpp>
#include <rtti/xml.hpp>

#include <cstdint>
#include <limits>
#include <string>

struct DateTimeEvent {
    int id = 0;
    rtti::date_time when{};
    std::string note;
    RTTI_DEFINE_TYPE_INTRUSIVE(DateTimeEvent, id, when, note)
};

struct DateTimeSubsecond {
    int id = 0;
    rtti::date_time when{};
    std::string note;
    RTTI_DEFINE_TYPE_INTRUSIVE(DateTimeSubsecond, id, when, note)
};

namespace rtti_test_lib {

constexpr std::int64_t kISoEpochNs = std::int64_t(1791290096) * 1000000000LL;

RTTI_TEST_CASE(DateTimeValuePrecisionRoundTrip) {
    const auto s = rtti::date_time::from_unix_seconds(1791290096);
    RTTI_CHECK(s.ok());
    std::int64_t v = 0;
    RTTI_CHECK_EQ(s.value.to_unix_seconds(v), rtti::error_code::ok);
    RTTI_CHECK_EQ(v, std::int64_t(1791290096));
    RTTI_CHECK_EQ(s.value.to_unix_ms(v), rtti::error_code::ok);
    RTTI_CHECK_EQ(v, std::int64_t(1791290096000));
    RTTI_CHECK_EQ(s.value.to_unix_us(v), rtti::error_code::ok);
    RTTI_CHECK_EQ(v, std::int64_t(1791290096000000));
    RTTI_CHECK_EQ(s.value.to_unix_ns(v), rtti::error_code::ok);
    RTTI_CHECK_EQ(v, kISoEpochNs);

    const auto m = rtti::date_time::from_unix_ms(1791290096123);
    RTTI_CHECK(m.ok());
    std::int64_t mv = 0;
    RTTI_CHECK_EQ(m.value.to_unix_ms(mv), rtti::error_code::ok);
    RTTI_CHECK_EQ(mv, std::int64_t(1791290096123));
    RTTI_CHECK_EQ(m.value.epoch_ns(), std::int64_t(1791290096123000000));

    const auto u = rtti::date_time::from_unix_us(1791290096123456);
    RTTI_CHECK(u.ok());
    std::int64_t uv = 0;
    RTTI_CHECK_EQ(u.value.to_unix_us(uv), rtti::error_code::ok);
    RTTI_CHECK_EQ(uv, std::int64_t(1791290096123456));

    const auto n = rtti::date_time::from_unix_ns(1791290096123456789);
    RTTI_CHECK(n.ok());
    std::int64_t nv = 0;
    RTTI_CHECK_EQ(n.value.to_unix_ns(nv), rtti::error_code::ok);
    RTTI_CHECK_EQ(nv, std::int64_t(1791290096123456789));
    std::int64_t ns2 = 0;
    RTTI_CHECK_EQ(n.value.to_unix_seconds(ns2), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns2, std::int64_t(1791290096));

    // Boundary of each scaled unit: just inside is ok, just outside overflows.
    const auto okb = rtti::date_time::from_unix_seconds(9223372036);
    RTTI_CHECK(okb.ok());
    RTTI_CHECK_EQ(okb.value.epoch_ns(), std::int64_t(9223372036000000000));
    const auto ov1 = rtti::date_time::from_unix_seconds(9223372037);
    RTTI_CHECK(!ov1.ok());
    RTTI_CHECK_EQ(ov1.err.code, rtti::error_code::overflow);
    const auto ov2 = rtti::date_time::from_unix_ms(9223372036855);
    RTTI_CHECK(!ov2.ok());
    RTTI_CHECK_EQ(ov2.err.code, rtti::error_code::overflow);
    const auto ov3 = rtti::date_time::from_unix_us(9223372036854776);
    RTTI_CHECK(!ov3.ok());
    RTTI_CHECK_EQ(ov3.err.code, rtti::error_code::overflow);
    RTTI_CHECK(rtti::date_time::from_unix_us(9223372036854775).ok());
    RTTI_CHECK(rtti::date_time::from_unix_ns(std::numeric_limits<std::int64_t>::max()).ok());
    const auto ov4 = rtti::date_time::from_unix_seconds(-9223372037);
    RTTI_CHECK(!ov4.ok());
    RTTI_CHECK_EQ(ov4.err.code, rtti::error_code::overflow);
    RTTI_CHECK(rtti::date_time::from_unix_seconds(-9223372036).ok());
}

RTTI_TEST_CASE(DateTimeIsoParseFormatRoundTrip) {
    const auto t = rtti::date_time::parse("2026-10-06T12:34:56Z");
    RTTI_CHECK(t.ok());
    RTTI_CHECK_EQ(t.value.epoch_ns(), kISoEpochNs);
    RTTI_CHECK_EQ(t.value.format(), std::string("2026-10-06T12:34:56Z"));
    RTTI_CHECK_EQ(t.value.to_string(), std::string("2026-10-06T12:34:56Z"));
    RTTI_CHECK_EQ(rtti::date_time::from_unix_seconds(1791290096).value.format(),
                  std::string("2026-10-06T12:34:56Z"));

    // Millisecond fraction.
    const auto ms = rtti::date_time::parse("2026-10-06T12:34:56.123Z");
    RTTI_CHECK(ms.ok());
    RTTI_CHECK_EQ(ms.value.epoch_ns(), std::int64_t(1791290096123000000));
    RTTI_CHECK_EQ(ms.value.format(), std::string("2026-10-06T12:34:56.123Z"));

    // Microsecond fraction.
    const auto us = rtti::date_time::parse("2026-10-06T12:34:56.123456Z");
    RTTI_CHECK(us.ok());
    RTTI_CHECK_EQ(us.value.epoch_ns(), std::int64_t(1791290096123456000));
    RTTI_CHECK_EQ(us.value.format(), std::string("2026-10-06T12:34:56.123456Z"));

    // Nanosecond fraction (full 9 digits).
    const auto ns = rtti::date_time::parse("2026-10-06T12:34:56.123456789Z");
    RTTI_CHECK(ns.ok());
    RTTI_CHECK_EQ(ns.value.epoch_ns(), std::int64_t(1791290096123456789));
    RTTI_CHECK_EQ(ns.value.format(), std::string("2026-10-06T12:34:56.123456789Z"));
    RTTI_CHECK_EQ(rtti::date_time::from_unix_ns(1791290096123456789).value.format(),
                  std::string("2026-10-06T12:34:56.123456789Z"));

    // Offset timezones normalize to UTC.
    const auto e1 = rtti::date_time::parse("2026-10-06T20:34:56+08:00");
    RTTI_CHECK(e1.ok());
    RTTI_CHECK_EQ(e1.value.epoch_ns(), kISoEpochNs);
    RTTI_CHECK_EQ(e1.value.format(), std::string("2026-10-06T12:34:56Z"));
    const auto e2 = rtti::date_time::parse("2026-10-06T04:34:56-08:00");
    RTTI_CHECK(e2.ok());
    RTTI_CHECK_EQ(e2.value.epoch_ns(), kISoEpochNs);
    // Zero offset normalizes to Z too.
    const auto e3 = rtti::date_time::parse("2026-10-06T12:34:56+00:00");
    RTTI_CHECK(e3.ok());
    RTTI_CHECK_EQ(e3.value.epoch_ns(), kISoEpochNs);
    // Fraction combined with an offset.
    const auto e4 = rtti::date_time::parse("2026-10-06T20:34:56.5+08:00");
    RTTI_CHECK(e4.ok());
    RTTI_CHECK_EQ(e4.value.epoch_ns(), std::int64_t(1791290096500000000));

    // Epoch zero and pre-epoch negatives format/parse exactly (floor division).
    const auto z = rtti::date_time::parse("1970-01-01T00:00:00Z");
    RTTI_CHECK(z.ok());
    RTTI_CHECK_EQ(z.value.epoch_ns(), std::int64_t(0));
    RTTI_CHECK_EQ(rtti::date_time().format(), std::string("1970-01-01T00:00:00Z"));
    const auto neg = rtti::date_time::parse("1969-12-31T23:59:59Z");
    RTTI_CHECK(neg.ok());
    RTTI_CHECK_EQ(neg.value.epoch_ns(), std::int64_t(-1000000000));
    RTTI_CHECK_EQ(neg.value.format(), std::string("1969-12-31T23:59:59Z"));
    const auto negf = rtti::date_time::parse("1969-12-31T23:59:58.5Z");
    RTTI_CHECK(negf.ok());
    RTTI_CHECK_EQ(negf.value.epoch_ns(), std::int64_t(-1500000000));
    RTTI_CHECK_EQ(negf.value.format(), std::string("1969-12-31T23:59:58.5Z"));

    // Leap year is honored.
    const auto leap = rtti::date_time::parse("2024-02-29T00:00:00Z");
    RTTI_CHECK(leap.ok());
    RTTI_CHECK_EQ(leap.value.format(), std::string("2024-02-29T00:00:00Z"));
}

RTTI_TEST_CASE(DateTimeDigitDetection) {
    std::int64_t ns = 0;
    // 10 digits -> seconds.
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(1789263451, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::int64_t(1789263451000000000));
    // 13 digits -> milliseconds.
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(1789263451012, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::int64_t(1789263451012000000));
    // 16 digits -> microseconds.
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(1789263451012345, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::int64_t(1789263451012345000));
    // 19 digits -> nanoseconds.
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(1789263451012345678, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::int64_t(1789263451012345678));
    // Negative epoch with a leading sign: digits are counted without the sign.
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(-1789263451, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::int64_t(-1789263451000000000));
    // Zero maps to epoch zero (seconds).
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(0, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::int64_t(0));
    // Unrecognized digit counts -> invalid_number.
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(123456789, ns), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(123456789012, ns), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(1, ns), rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(-1, ns), rtti::error_code::invalid_number);
    // Scaled value out of the int64 nanosecond range -> overflow.
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(9223372037, ns), rtti::error_code::overflow);
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(9999999999999, ns), rtti::error_code::overflow);
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(9223372036855, ns), rtti::error_code::overflow);
    // In-range scaled values stay ok.
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(9223372036, ns), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(-9223372036, ns), rtti::error_code::ok);
    // The full int64 nanosecond lane is valid: both INT64_MIN and INT64_MAX
    // are 19-digit ns values and must parse exactly (the ns branch negative
    // bound previously rejected INT64_MIN by one).
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(std::numeric_limits<std::int64_t>::min(), ns),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::numeric_limits<std::int64_t>::min());
    RTTI_CHECK_EQ(rtti::epoch_ns_from_digits(std::numeric_limits<std::int64_t>::max(), ns),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(ns, std::numeric_limits<std::int64_t>::max());
}

RTTI_TEST_CASE(DateTimeMalformedNoCrash) {
    const char* bad[] = {
        "",
        "garbage",
        "2026-10-06",
        "2026-13-01T00:00:00Z",
        "2026-00-10T00:00:00Z",
        "2026-04-31T00:00:00Z",
        "2023-02-29T00:00:00Z",
        "2026-10-06T24:00:00Z",
        "2026-10-06T12:60:00Z",
        "2026-10-06T12:34:60Z",
        "2026-10-06T12:34:56",          // missing timezone designator
        "2026-10-06T12:34:56Zextra",     // trailing junk
        "2026-10-06T12:34:56.1234567890Z", // 10 fractional digits
        "2026-10-06T12:34:56.",          // dangling fraction
        "2026-10-06T12:34:56+0800",      // offset without colon
        "2026-10-06T12:34:56+08:0X",     // non-digit offset minute
        "2026-10-06T12:34:56+25:00",     // hour offset out of range
        "2026-10-06T12:34:56-24:00",     // negative offset out of range
        "9999-12-31T23:59:59Z",          // years beyond the int64 ns range
    };
    for (const char* s : bad) {
        const auto r = rtti::date_time::parse(s);
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code != rtti::error_code::ok);
    }
    // Year 9999 overflow is reported as overflow specifically.
    const auto over = rtti::date_time::parse("9999-12-31T23:59:59Z");
    RTTI_CHECK_EQ(over.err.code, rtti::error_code::overflow);
}

RTTI_TEST_CASE(DateTimeBoundaryEpoch) {
    const auto maxd = rtti::date_time(std::numeric_limits<std::int64_t>::max());
    const auto maxs = rtti::date_time::parse(maxd.format());
    RTTI_CHECK(maxs.ok());
    RTTI_CHECK_EQ(maxs.value.epoch_ns(), std::numeric_limits<std::int64_t>::max());
    std::int64_t sv = 0;
    RTTI_CHECK_EQ(maxd.to_unix_seconds(sv), rtti::error_code::ok);
    RTTI_CHECK_EQ(sv, std::int64_t(9223372036));

    const auto mind = rtti::date_time(std::numeric_limits<std::int64_t>::min());
    const auto mins = rtti::date_time::parse(mind.format());
    RTTI_CHECK(mins.ok());
    RTTI_CHECK_EQ(mins.value.epoch_ns(), std::numeric_limits<std::int64_t>::min());
    std::int64_t msv = 0;
    RTTI_CHECK_EQ(mind.to_unix_seconds(msv), rtti::error_code::ok);
    // floor() of the most negative nanosecond count is one second below the
    // truncated -9223372036.
    RTTI_CHECK_EQ(msv, std::int64_t(-9223372037));
}

RTTI_TEST_CASE(DateTimeComparisonArithmetic) {
    const auto a = rtti::date_time::from_unix_seconds(5);
    const auto b = rtti::date_time::from_unix_seconds(5);
    const auto c = rtti::date_time::from_unix_seconds(8);
    RTTI_CHECK(a.ok() && b.ok() && c.ok());
    RTTI_CHECK(a.value == b.value);
    RTTI_CHECK(a.value != c.value);
    RTTI_CHECK(a.value < c.value);
    RTTI_CHECK(c.value > a.value);
    RTTI_CHECK(a.value <= b.value);
    RTTI_CHECK(a.value >= b.value);
    RTTI_CHECK_EQ(a.value.epoch_ns(), std::int64_t(5000000000));

    const auto p = a.value + std::int64_t(1000000000);
    RTTI_CHECK_EQ(p.epoch_ns(), std::int64_t(6000000000));
    const auto m = c.value - std::int64_t(1000000000);
    RTTI_CHECK_EQ(m.epoch_ns(), std::int64_t(7000000000));
    const auto d = c.value - a.value;
    RTTI_CHECK_EQ(d, std::int64_t(3000000000));
}

RTTI_TEST_CASE(DateTimeConverterScalarNodes) {
    rtti::json::document doc;
    rtti::value_io* sec = doc.make_node();
    sec->make_scalar(rtti::field_value::of(std::int64_t(1791290096)));
    rtti::date_time dt;
    RTTI_CHECK_EQ(rtti::from_value(sec, dt), rtti::error_code::ok);
    RTTI_CHECK_EQ(dt.epoch_ns(), kISoEpochNs);

    rtti::value_io* msn = doc.make_node();
    msn->make_scalar(rtti::field_value::of(std::int64_t(1791290096123)));
    RTTI_CHECK_EQ(rtti::from_value(msn, dt), rtti::error_code::ok);
    RTTI_CHECK_EQ(dt.epoch_ns(), std::int64_t(1791290096123000000));

    rtti::value_io* usn = doc.make_node();
    usn->make_scalar(rtti::field_value::of(std::int64_t(1791290096123456)));
    RTTI_CHECK_EQ(rtti::from_value(usn, dt), rtti::error_code::ok);
    RTTI_CHECK_EQ(dt.epoch_ns(), std::int64_t(1791290096123456000));

    rtti::value_io* nsn = doc.make_node();
    nsn->make_scalar(rtti::field_value::of(std::int64_t(1791290096123456789)));
    RTTI_CHECK_EQ(rtti::from_value(nsn, dt), rtti::error_code::ok);
    RTTI_CHECK_EQ(dt.epoch_ns(), std::int64_t(1791290096123456789));

    // ISO text scalar reads as a string.
    rtti::value_io* str = doc.make_node();
    str->make_scalar(rtti::field_value::of(rtti::string_view("2026-10-06T12:34:56Z")));
    RTTI_CHECK_EQ(rtti::from_value(str, dt), rtti::error_code::ok);
    RTTI_CHECK_EQ(dt.epoch_ns(), kISoEpochNs);

    // Malformed text -> parse error, no crash.
    rtti::value_io* bad = doc.make_node();
    bad->make_scalar(rtti::field_value::of(rtti::string_view("not-a-time")));
    RTTI_CHECK_EQ(rtti::from_value(bad, dt), rtti::error_code::invalid_number);

    // Unknown digit count -> invalid_number.
    rtti::value_io* shortn = doc.make_node();
    shortn->make_scalar(rtti::field_value::of(std::int64_t(123456789)));
    RTTI_CHECK_EQ(rtti::from_value(shortn, dt), rtti::error_code::invalid_number);

    // Wrong scalar kinds -> kind_mismatch.
    rtti::value_io* bl = doc.make_node();
    bl->make_scalar(rtti::field_value::of(true));
    RTTI_CHECK_EQ(rtti::from_value(bl, dt), rtti::error_code::kind_mismatch);
    rtti::value_io* rl = doc.make_node();
    rl->make_scalar(rtti::field_value::of(3.5));
    RTTI_CHECK_EQ(rtti::from_value(rl, dt), rtti::error_code::kind_mismatch);

    // Write side defaults to numeric epoch seconds.
    rtti::value_io* out = doc.make_node();
    const auto w = rtti::date_time::from_unix_seconds(1791290096);
    RTTI_CHECK_EQ(rtti::to_value(out, w.value), rtti::error_code::ok);
    std::int64_t ov = 0;
    RTTI_CHECK(out->as_integer(ov));
    RTTI_CHECK_EQ(ov, std::int64_t(1791290096));
}

RTTI_TEST_CASE(DateTimeExplicitOutputModes) {
    const auto dt = rtti::date_time::from_unix_ns(1791290096123456789);
    RTTI_CHECK(dt.ok());
    rtti::json::document doc;

    rtti::value_io* s = doc.make_node();
    RTTI_CHECK_EQ(rtti::to_value_dt(s, dt.value, rtti::date_time_output::numeric_seconds),
                  rtti::error_code::ok);
    std::int64_t sv = 0;
    RTTI_CHECK(s->as_integer(sv));
    RTTI_CHECK_EQ(sv, std::int64_t(1791290096));

    rtti::value_io* ms = doc.make_node();
    RTTI_CHECK_EQ(rtti::to_value_dt(ms, dt.value, rtti::date_time_output::numeric_ms),
                  rtti::error_code::ok);
    std::int64_t msv = 0;
    RTTI_CHECK(ms->as_integer(msv));
    RTTI_CHECK_EQ(msv, std::int64_t(1791290096123));

    rtti::value_io* us = doc.make_node();
    RTTI_CHECK_EQ(rtti::to_value_dt(us, dt.value, rtti::date_time_output::numeric_us),
                  rtti::error_code::ok);
    std::int64_t usv = 0;
    RTTI_CHECK(us->as_integer(usv));
    RTTI_CHECK_EQ(usv, std::int64_t(1791290096123456));

    rtti::value_io* ns = doc.make_node();
    RTTI_CHECK_EQ(rtti::to_value_dt(ns, dt.value, rtti::date_time_output::numeric_ns),
                  rtti::error_code::ok);
    std::int64_t nsv = 0;
    RTTI_CHECK(ns->as_integer(nsv));
    RTTI_CHECK_EQ(nsv, std::int64_t(1791290096123456789));

    // ISO text output preserves sub-second precision exactly.
    rtti::value_io* txt = doc.make_node();
    RTTI_CHECK_EQ(rtti::to_value_dt(txt, dt.value, rtti::date_time_output::iso_string),
                  rtti::error_code::ok);
    rtti::string_view tv;
    RTTI_CHECK(txt->as_string(tv));
    RTTI_CHECK_EQ(std::string(tv), std::string("2026-10-06T12:34:56.123456789Z"));
    // And it reads back to the identical value through the converter.
    rtti::date_time back;
    RTTI_CHECK_EQ(rtti::from_value(txt, back), rtti::error_code::ok);
    RTTI_CHECK_EQ(back.epoch_ns(), dt.value.epoch_ns());
}

static bool is_same_dt(const rtti::date_time& a, const rtti::date_time& b) noexcept {
    return a.epoch_ns() == b.epoch_ns();
}

RTTI_TEST_CASE(DateTimeFourFormatNumericRoundTrip) {
    const auto when = rtti::date_time::from_unix_seconds(1791290096).value;
    DateTimeEvent ev;
    ev.id = 7;
    ev.when = when;
    ev.note = "hit";

    // Reflected date_time members serialize through the sub-second-preserving
    // ISO-string lane (R1-7), so the ISO token appears in the output; the
    // numeric-seconds default is still covered by DateTimeConverterScalarNodes
    // and DateTimeExplicitOutputModes. Round-trip equality proves losslessness.
    const std::string jt = rtti::json::serialize(ev);
    RTTI_CHECK(jt.find("2026-10-06T12:34:56Z") != std::string::npos);
    const auto jr = rtti::json::parse_as<DateTimeEvent>(jt);
    RTTI_CHECK(jr.ok());
    RTTI_CHECK_EQ(jr.value.id, 7);
    RTTI_CHECK_EQ(jr.value.note, std::string("hit"));
    RTTI_CHECK(is_same_dt(jr.value.when, when));

    const std::string xt = rtti::xml::serialize(ev);
    RTTI_CHECK(xt.find("2026-10-06T12:34:56Z") != std::string::npos);
    const auto xr = rtti::xml::parse_as<DateTimeEvent>(xt);
    RTTI_CHECK(xr.ok());
    RTTI_CHECK_EQ(xr.value.id, 7);
    RTTI_CHECK(is_same_dt(xr.value.when, when));

    const std::string it = rtti::ini::serialize(ev);
    RTTI_CHECK(it.find("2026-10-06T12:34:56Z") != std::string::npos);
    const auto ir = rtti::ini::parse_as<DateTimeEvent>(rtti::string_view(it));
    RTTI_CHECK(ir.ok());
    RTTI_CHECK_EQ(ir.value.id, 7);
    RTTI_CHECK(is_same_dt(ir.value.when, when));

    const std::string tt = rtti::toml::serialize(ev);
    RTTI_CHECK(tt.find("2026-10-06T12:34:56Z") != std::string::npos);
    const auto tr = rtti::toml::parse_as<DateTimeEvent>(rtti::string_view(tt));
    RTTI_CHECK(tr.ok());
    RTTI_CHECK_EQ(tr.value.id, 7);
    RTTI_CHECK(is_same_dt(tr.value.when, when));
}

RTTI_TEST_CASE(DateTimeFourFormatStringFormParse) {
    // The ISO string morphology of the date_time field is accepted by every
    // adapter's parser and resolves to the same instant.
    const std::string jt = "{\"id\":7,\"when\":\"2026-10-06T12:34:56Z\",\"note\":\"hit\"}";
    const auto jr = rtti::json::parse_as<DateTimeEvent>(jt);
    RTTI_CHECK(jr.ok());
    RTTI_CHECK(is_same_dt(jr.value.when, rtti::date_time::from_unix_seconds(1791290096).value));

    const std::string xt =
        "<r><id>7</id><when>2026-10-06T12:34:56Z</when><note>hit</note></r>";
    const auto xr = rtti::xml::parse_as<DateTimeEvent>(xt);
    RTTI_CHECK(xr.ok());
    RTTI_CHECK(is_same_dt(xr.value.when, rtti::date_time::from_unix_seconds(1791290096).value));

    const std::string it = "id = 7\nwhen = 2026-10-06T12:34:56Z\nnote = hit\n";
    const auto ir = rtti::ini::parse_as<DateTimeEvent>(rtti::string_view(it));
    RTTI_CHECK(ir.ok());
    RTTI_CHECK(is_same_dt(ir.value.when, rtti::date_time::from_unix_seconds(1791290096).value));

    // Bare TOML datetime token (kept as an unquoted string scalar).
    const std::string tt = "id = 7\nwhen = 2026-10-06T12:34:56Z\nnote = \"hit\"\n";
    const auto tr = rtti::toml::parse_as<DateTimeEvent>(rtti::string_view(tt));
    RTTI_CHECK(tr.ok());
    RTTI_CHECK(is_same_dt(tr.value.when, rtti::date_time::from_unix_seconds(1791290096).value));

    // Quoted TOML string form also works.
    const std::string t2 = "id = 7\nwhen = \"2026-10-06T12:34:56Z\"\nnote = \"hit\"\n";
    const auto t2r = rtti::toml::parse_as<DateTimeEvent>(rtti::string_view(t2));
    RTTI_CHECK(t2r.ok());
    RTTI_CHECK(is_same_dt(t2r.value.when, rtti::date_time::from_unix_seconds(1791290096).value));
}

RTTI_TEST_CASE(DateTimeFractionStringFormRoundTrip) {
    // Sub-second precision survives when the field travels as ISO text.
    const std::string jt =
        "{\"id\":7,\"when\":\"2026-10-06T12:34:56.123456789Z\",\"note\":\"hit\"}";
    const auto jr = rtti::json::parse_as<DateTimeEvent>(jt);
    RTTI_CHECK(jr.ok());
    RTTI_CHECK_EQ(jr.value.when.epoch_ns(), std::int64_t(1791290096123456789));

    const std::string xt =
        "<r><id>7</id><when>2026-10-06T12:34:56.123456789Z</when><note>hit</note></r>";
    const auto xr = rtti::xml::parse_as<DateTimeEvent>(xt);
    RTTI_CHECK(xr.ok());
    RTTI_CHECK_EQ(xr.value.when.epoch_ns(), std::int64_t(1791290096123456789));

    const std::string it =
        "id = 7\nwhen = 2026-10-06T12:34:56.123456789Z\nnote = hit\n";
    const auto ir = rtti::ini::parse_as<DateTimeEvent>(rtti::string_view(it));
    RTTI_CHECK(ir.ok());
    RTTI_CHECK_EQ(ir.value.when.epoch_ns(), std::int64_t(1791290096123456789));

    const std::string tt =
        "id = 7\nwhen = 2026-10-06T12:34:56.123456789Z\nnote = \"hit\"\n";
    const auto tr = rtti::toml::parse_as<DateTimeEvent>(rtti::string_view(tt));
    RTTI_CHECK(tr.ok());
    RTTI_CHECK_EQ(tr.value.when.epoch_ns(), std::int64_t(1791290096123456789));
}

RTTI_TEST_CASE(DateTimeConverterBadFieldKinds) {
    // A bool / real / short-integer "when" is rejected through the reflected
    // struct walk with an error (never a crash).
    const std::string jb = "{\"id\":7,\"when\":true,\"note\":\"hit\"}";
    const auto br = rtti::json::parse_as<DateTimeEvent>(jb);
    RTTI_CHECK(!br.ok());

    const std::string ji = "{\"id\":7,\"when\":123456789,\"note\":\"hit\"}";
    const auto ir = rtti::json::parse_as<DateTimeEvent>(ji);
    RTTI_CHECK(!ir.ok());
}

RTTI_TEST_CASE(DateTimeReflectedSubsecondRoundTrip) {
    // Sub-second precision must survive a reflected-struct round trip through
    // every adapter. The reflected member lane emits the ISO string (which
    // preserves the fraction exactly); the scalar default stays numeric
    // seconds (covered by DateTimeFourFormatNumericRoundTrip).
    const auto dt = rtti::date_time::from_unix_ns(1791290096123456789).value;
    DateTimeSubsecond ev;
    ev.id = 7;
    ev.when = dt;
    ev.note = "hit";

    const std::string jt = rtti::json::serialize(ev);
    const auto jr = rtti::json::parse_as<DateTimeSubsecond>(jt);
    RTTI_CHECK(jr.ok());
    RTTI_CHECK_EQ(jr.value.when.epoch_ns(), dt.epoch_ns());

    const std::string xt = rtti::xml::serialize(ev);
    const auto xr = rtti::xml::parse_as<DateTimeSubsecond>(xt);
    RTTI_CHECK(xr.ok());
    RTTI_CHECK_EQ(xr.value.when.epoch_ns(), dt.epoch_ns());

    const std::string it = rtti::ini::serialize(ev);
    const auto ir = rtti::ini::parse_as<DateTimeSubsecond>(rtti::string_view(it));
    RTTI_CHECK(ir.ok());
    RTTI_CHECK_EQ(ir.value.when.epoch_ns(), dt.epoch_ns());

    const std::string tt = rtti::toml::serialize(ev);
    const auto tr = rtti::toml::parse_as<DateTimeSubsecond>(rtti::string_view(tt));
    RTTI_CHECK(tr.ok());
    RTTI_CHECK_EQ(tr.value.when.epoch_ns(), dt.epoch_ns());

    // The member lane writes the fractional ISO text (not numeric seconds).
    rtti::json::document doc;
    rtti::value_io* n = doc.make_node();
    RTTI_CHECK_EQ(rtti::to_value(n, ev), rtti::error_code::ok);
    rtti::value_io* when_node = nullptr;
    RTTI_CHECK(n->get_key("when", when_node));
    if (when_node != nullptr) {
        rtti::string_view tv;
        RTTI_CHECK(when_node->as_string(tv));
        RTTI_CHECK(std::string(tv).find(".123456789Z") != std::string::npos);
    }
}

} // namespace rtti_test_lib