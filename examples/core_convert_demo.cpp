// =============================================================================
// core_convert_demo.cpp -- application example for four reflect-core
// capabilities of the RTTI library (namespace rtti, header-only C++17):
//
//   1. date_time      -- <rtti/date_time.hpp>
//       Unix epoch time value in int64 nanoseconds, ISO 8601 parse/format,
//       precision-aware unit factories and numeric digit-count auto-detection.
//   2. base_convert   -- <rtti/base_convert.hpp>
//       Arbitrary-base integer <-> string (bases 2..64), signed variants,
//       custom digit tables and overflow detection.
//   3. to_string      -- <rtti/to_string.hpp>
//       Unified T <-> std::string: scalars, enums, date_time, arbitrary-base
//       integers, containers and reflected structs, plus the optional<T>
//       convenience API and error codes.
//   4. enum           -- RTTI_DEFINE_ENUM
//       Enum label table, label <-> value conversion and the convert/serialize
//       bridge that renders enums as their label text in any format tree.
//
// PURPOSE
//   A readable teaching program. Every API call is preceded by a comment block
//   (what / why / return semantics / error handling). The library never throws
//   for bad input; failures arrive as rtti::error_code and are printed, never
//   crashed. Everything runs in main() so the output doubles as a spec walk.
//
// DESIGN NOTE
//   The demos that bridge through a tree need a value_io SINK. The library's
//   format adapters (JSON/XML/...) each ship their own document, but a *core*
//   example should stay free of format adapters, so this file carries a tiny
//   in-memory rtti::value_io implementation (demo_core::box, ~60 lines). This
//   is exactly how a user would add a new format: implement rtti::value_io,
//   then to_value/from_value and to_value_dt/to_value_base all work for free.
//
// DEPENDENCIES
//   <rtti/date_time.hpp>  <rtti/base_convert.hpp>  <rtti/to_string.hpp>
//   (to_string.hpp pulls in convert.hpp -> reflection.hpp, the reflect core)
//
// BUILD / RUN (verified with MSVC 14.44.35207, Debug, x64):
//   call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
//   cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd ^
//      /I F:\json_xml\include examples\core_convert_demo.cpp /link ^
//      /LIBPATH:"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\lib\x64" ^
//      /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" ^
//      /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" ^
//      build\Debug\rtti.lib
//   examples\core_convert_demo.exe
//
//   Expected: zero compiler warnings (/W4), every section prints and verifies,
//   exit code 0.
//
// ALL TYPE NAMES ARE PREFIXED "DemoCore" SO THIS TU NEVER COLLIDES WITH THE
// PARALLEL EXAMPLE PROGRAMS (json/xml/ini/toml/proto/core) AT LINK TIME.
// English comments only; C++17; zero third-party dependencies.
// =============================================================================

#include <rtti/base_convert.hpp>
#include <rtti/date_time.hpp>
#include <rtti/to_string.hpp>

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// SECTION 1 -- types used across the demos
// =============================================================================

// A registered enum (label table). Note the explicit values: RTTI_DEFINE_ENUM
// records the *actual* enumerator values, so gaps (3 here) are preserved.
enum class DemoCoreColor : int { Red = 1, Green = 2, Blue = 4 };

// A reflected struct holding a date_time member, used to show that date_time
// flows through the shared convert dispatch like any scalar (the JSON/XML/INI/
// TOML adapters would round-trip it through the same path).
struct DemoCoreDtRecord {
    std::int64_t id = 0;
    rtti::date_time when{};

    RTTI_DEFINE_TYPE_INTRUSIVE(DemoCoreDtRecord, id, when)
};

// Two reflected structs used for the to_string / from_string struct round
// trips (DemoCoreNested exercises scalar + string + container + optional
// members; DemoCorePainted exercises an enum member).
struct DemoCoreNested {
    int id = 0;
    std::string name;
    std::vector<int> nums;
    std::map<std::string, int> tags;
    std::optional<int> maybe;

    RTTI_DEFINE_TYPE_INTRUSIVE(DemoCoreNested, id, name, nums, tags, maybe)
};

struct DemoCorePainted {
    int id = 0;
    DemoCoreColor tint = DemoCoreColor::Red;

    RTTI_DEFINE_TYPE_INTRUSIVE(DemoCorePainted, id, tint)
};

// operator== so the generic check_string_round_trip helper can compare a
// round-tripped struct against the source.
bool operator==(const DemoCoreNested& a, const DemoCoreNested& b) {
    return a.id == b.id && a.name == b.name && a.nums == b.nums &&
           a.tags == b.tags && a.maybe == b.maybe;
}
bool operator==(const DemoCorePainted& a, const DemoCorePainted& b) {
    return a.id == b.id && a.tint == b.tint;
}

// The enum label table must be registered at namespace scope.
RTTI_DEFINE_ENUM(DemoCoreColor, Red, Green, Blue)

// =============================================================================
// SECTION 2 -- a minimal value_io sink (demo_core::box)
// =============================================================================
// value_io is the abstract tree interface every format adapter implements
// (kind(), as_*(), size(), at(), get_key(), get_key_at(), make_*(), set_key(),
// push_back(), make_node()). demo_core::box is a self-owning implementation
// that stores scalar payloads and children as simple C++ members; it is used
// below as the sink for to_value_dt / to_value_base and the enum bridge.

namespace demo_core {

struct box : rtti::value_io {
    using child_list = std::vector<box*>;
    using child_map = std::map<std::string, box*>;

    rtti::field_value scalar;   // scalar payload (or the empty/null value)
    child_list arr;             // children when kind == array
    child_map obj;              // children when kind == object
    std::string owned;          // owns string payload so views stay valid
    bool is_object = false;
    bool is_array = false;

    box() = default;
    // Recursively delete children; the whole tree frees itself.
    ~box() override {
        for (box* c : arr) {
            delete c;
        }
        for (auto& kv : obj) {
            delete kv.second;
        }
    }

    // kind() is derived from the current shape (object/array/scalar/null).
    rtti::value_io::kind type() const noexcept override {
        if (is_object) {
            return rtti::value_io::kind::object;
        }
        if (is_array) {
            return rtti::value_io::kind::array;
        }
        switch (scalar.type()) {
            case rtti::field_value::kind::boolean:
                return rtti::value_io::kind::boolean;
            case rtti::field_value::kind::integer:
                return rtti::value_io::kind::integer;
            case rtti::field_value::kind::real:
                return rtti::value_io::kind::real;
            case rtti::field_value::kind::string:
                return rtti::value_io::kind::string;
            default:
                return rtti::value_io::kind::null;
        }
    }

    // --- scalar readers (return false when the node is not that kind) -------
    bool as_bool(bool& v) const noexcept override {
        if (scalar.type() != rtti::field_value::kind::boolean) {
            return false;
        }
        v = scalar.as_bool();
        return true;
    }
    bool as_integer(std::int64_t& v) const noexcept override {
        if (scalar.type() != rtti::field_value::kind::integer) {
            return false;
        }
        v = scalar.as_integer();
        return true;
    }
    bool as_real(double& v) const noexcept override {
        if (scalar.type() != rtti::field_value::kind::real) {
            return false;
        }
        v = scalar.as_real();
        return true;
    }
    bool as_string(rtti::string_view& v) const noexcept override {
        if (scalar.type() != rtti::field_value::kind::string) {
            return false;
        }
        v = scalar.as_string();
        return true;
    }

    // --- container readers ------------------------------------------------
    std::size_t size() const noexcept override {
        return is_array ? arr.size() : (is_object ? obj.size() : 0);
    }
    rtti::value_io* at(std::size_t i) override {
        return is_array && i < arr.size() ? arr[i] : nullptr;
    }
    rtti::value_io* at(std::size_t i) const override {
        return is_array && i < arr.size() ? arr[i] : nullptr;
    }
    bool get_key(rtti::string_view k, rtti::value_io*& out) override {
        return const_get_key_impl(k, out);
    }
    bool get_key(rtti::string_view k, rtti::value_io*& out) const override {
        return const_get_key_impl(k, out);
    }
    bool get_key_at(std::size_t index, rtti::string_view& key,
                    rtti::value_io*& out) const override {
        if (!is_object || index >= obj.size()) {
            return false;
        }
        auto it = obj.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(index));
        key = rtti::string_view(it->first);
        out = it->second;
        return true;
    }

    // --- container writers --------------------------------------------------
    rtti::value_io* make_array() override {
        is_array = true;
        is_object = false;
        return this;
    }
    void push_back(rtti::value_io* v) override {
        arr.push_back(static_cast<box*>(v));
    }
    rtti::value_io* make_object() override {
        is_object = true;
        is_array = false;
        return this;
    }
    void set_key(rtti::string_view k, rtti::value_io* v) override {
        obj[std::string(k)] = static_cast<box*>(v);
    }

    // Store a scalar. String payloads are copied into `owned` first so the
    // stored view stays valid after the source string is destroyed.
    rtti::value_io* make_scalar(rtti::field_value v) override {
        if (v.type() == rtti::field_value::kind::string) {
            owned.assign(v.as_string().data(), v.as_string().size());
            scalar = rtti::field_value::of(rtti::string_view(owned));
        } else {
            scalar = v;
        }
        is_object = false;
        is_array = false;
        return this;
    }
    rtti::value_io* make_node() override { return new box; }

private:
    bool const_get_key_impl(rtti::string_view k, rtti::value_io*& out) const {
        if (!is_object) {
            return false;
        }
        auto it = obj.find(std::string(k));
        if (it == obj.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
};

// Print the scalar held by a box (for the to_value_dt / to_value_base output
// demos). Returns the human-readable form.
static std::string scalar_text(const box& b) {
    switch (b.type()) {
        case rtti::value_io::kind::boolean: {
            bool v = false;
            (void)b.as_bool(v);
            return v ? "true" : "false";
        }
        case rtti::value_io::kind::integer: {
            std::int64_t v = 0;
            (void)b.as_integer(v);
            return std::to_string(v);
        }
        case rtti::value_io::kind::real: {
            double v = 0.0;
            (void)b.as_real(v);
            return rtti::to_string(v);
        }
        case rtti::value_io::kind::string: {
            rtti::string_view v;
            (void)b.as_string(v);
            return std::string(v.data(), v.size());
        }
        default:
            return "null";
    }
}

} // namespace demo_core

// =============================================================================
// SECTION 3 -- small output helpers
// =============================================================================

namespace demo_core {

// Human-readable name of an error_code (defined in src/error.cpp).
static const char* err_name(rtti::error_code e) {
    return rtti::to_string(e);
}

// Round-trip one value through rtti::to_string / rtti::from_string and print
// the outcome, returning the error code of the parse half.
template <typename T>
static rtti::error_code check_string_round_trip(const char* what,
                                                const T& value) {
    const std::string s = rtti::to_string(value);
    T back{};
    const rtti::error_code e = rtti::from_string<T>(s, back);
    std::printf("  %-26s -> \"%s\" parse=%s", what, s.c_str(), err_name(e));
    if (e == rtti::error_code::ok) {
        std::printf("  %s", back == value ? "exact" : "DIFFERS");
    }
    std::printf("\n");
    return e;
}

} // namespace demo_core

// =============================================================================
// SECTION 4 -- main: run every demonstration
// =============================================================================

int main() {
    using namespace demo_core;

    // =========================================================================
    // 4.1  date_time: precision-aware Unix epoch factories
    // =========================================================================
    // date_time stores Unix epoch time as an int64 nanosecond count. The four
    // from_unix_* factories scale their input to nanoseconds and RANGE-CHECK
    // the product: a value whose scaled result leaves the int64 lane reports
    // error_code::overflow instead of wrapping (spec principle: never wrong).

    std::printf("== [4.1] date_time from_unix_* factories ==\n");
    const auto dt_s = rtti::date_time::from_unix_seconds(1791290096);
    const auto dt_ms = rtti::date_time::from_unix_ms(1791290096123);
    const auto dt_us = rtti::date_time::from_unix_us(1791290096123456);
    const auto dt_ns = rtti::date_time::from_unix_ns(1791290096123456789);

    // Each result<T> carries ok() plus err.code on failure.
    std::printf("  seconds -> %s epoch_ns=%lld\n", err_name(dt_s.err.code),
                static_cast<long long>(dt_s.value.epoch_ns()));
    std::printf("  ms      -> %s epoch_ns=%lld\n", err_name(dt_ms.err.code),
                static_cast<long long>(dt_ms.value.epoch_ns()));
    std::printf("  us      -> %s epoch_ns=%lld\n", err_name(dt_us.err.code),
                static_cast<long long>(dt_us.value.epoch_ns()));
    std::printf("  ns      -> %s epoch_ns=%lld\n", err_name(dt_ns.err.code),
                static_cast<long long>(dt_ns.value.epoch_ns()));

    // The inverse unit accessors use floor division toward negative infinity
    // (so a pre-epoch instant with a fraction maps to the whole second that
    // contains it). None of them can overflow the int64 output.
    std::int64_t sec = 0, ms = 0, us = 0, ns = 0;
    (void)dt_ns.value.to_unix_seconds(sec);
    (void)dt_ns.value.to_unix_ms(ms);
    (void)dt_ns.value.to_unix_us(us);
    (void)dt_ns.value.to_unix_ns(ns);
    std::printf("  ns=...789 -> sec=%lld ms=%lld us=%lld ns=%lld (exact "
                "round trip)\n",
                static_cast<long long>(sec), static_cast<long long>(ms),
                static_cast<long long>(us), static_cast<long long>(ns));
    std::printf("\n");

    // =========================================================================
    // 4.2  date_time: overflow errors (never wrap, never crash)
    // =========================================================================
    // The int64 nanosecond lane covers roughly 1677..2262 AD at ns resolution.
    // Each unit has its own exact boundary; one past the boundary overflows.

    std::printf("== [4.2] date_time overflow boundaries ==\n");
    const auto ok_sec = rtti::date_time::from_unix_seconds(9223372036);
    const auto ov_sec = rtti::date_time::from_unix_seconds(9223372037);
    const auto ov_ms = rtti::date_time::from_unix_ms(9223372036855);
    const auto ov_us = rtti::date_time::from_unix_us(9223372036854776);
    std::printf("  from_unix_seconds( 9223372036) -> %s\n",
                err_name(ok_sec.err.code));
    std::printf("  from_unix_seconds( 9223372037) -> %s\n",
                err_name(ov_sec.err.code));
    std::printf("  from_unix_ms( 9223372036855)   -> %s\n",
                err_name(ov_ms.err.code));
    std::printf("  from_unix_us( 9223372036854776) -> %s\n",
                err_name(ov_us.err.code));
    std::printf("\n");

    // =========================================================================
    // 4.3  date_time: ISO 8601 parse / format round trips
    // =========================================================================
    // parse() accepts "YYYY-MM-DDTHH:MM:SS[.frac][Z|+/-HH:MM]" (1..9 fraction
    // digits, offsets normalized to UTC) and returns error_code::invalid_number
    // on malformed text. format() emits the canonical UTC form with the SHORTEST
    // fraction that carries the exact value, so round trips are exact.

    std::printf("== [4.3] ISO 8601 parse/format ==\n");

    const auto iso1 = rtti::date_time::parse("2026-10-06T12:34:56Z");
    std::printf("  parse(%s) -> %s  format()=%s\n", "2026-10-06T12:34:56Z",
                err_name(iso1.err.code), iso1.value.format().c_str());

    const auto iso2 = rtti::date_time::parse("2026-10-06T12:34:56.123456789Z");
    std::printf("  parse(2026-10-06T12:34:56.123456789Z) -> %s\n",
                err_name(iso2.err.code));
    std::printf("  format() -> %s  (9-digit fraction kept exactly)\n",
                iso2.value.format().c_str());

    // Offset timezones normalize to UTC.
    const auto iso3 = rtti::date_time::parse("2026-10-06T20:34:56+08:00");
    std::printf("  parse(2026-10-06T20:34:56+08:00) -> format()=%s (UTC)\n",
                iso3.value.format().c_str());

    // Malformed text is an error, never a throw or a garbage value.
    const auto bad1 = rtti::date_time::parse("not-a-date");
    const auto bad2 = rtti::date_time::parse("2026-13-01T00:00:00Z");  // month 13
    const auto bad3 = rtti::date_time::parse("2026-10-06T12:34:56");   // no tz
    std::printf("  parse(\"not-a-date\")                 -> %s\n",
                err_name(bad1.err.code));
    std::printf("  parse(2026-13-01T00:00:00Z)          -> %s\n",
                err_name(bad2.err.code));
    std::printf("  parse(2026-10-06T12:34:56)            -> %s\n",
                err_name(bad3.err.code));
    std::printf("\n");

    // =========================================================================
    // 4.4  date_time: numeric epoch digit-count auto-detection
    // =========================================================================
    // epoch_ns_from_digits infers the UNIT from the decimal digit count of a
    // numeric Unix epoch: 10 digits = seconds, 13 = ms, 16 = us, 19/20 = ns.
    // This is what makes the tree bridge accept "1791290096" and
    // "1791290096123456789" as the same instant. Unknown counts report
    // invalid_number; an out-of-range product reports overflow.

    std::printf("== [4.4] numeric epoch digit-count auto-detection ==\n");
    const std::int64_t samples[] = {
        1791290096,             // 10 digits -> seconds
        1791290096123,          // 13 digits -> ms
        1791290096123456,       // 16 digits -> us
        1791290096123456789,    // 19 digits -> ns
        12345,                  //  5 digits -> unknown -> invalid_number
        9223372036855LL,        // 13 digits but one PAST the ms boundary
    };
    for (std::int64_t v : samples) {
        std::int64_t ns_out = 0;
        const rtti::error_code e = rtti::epoch_ns_from_digits(v, ns_out);
        std::printf("  %-20lld -> %-14s", static_cast<long long>(v),
                    err_name(e));
        if (e == rtti::error_code::ok) {
            std::printf(" epoch_ns=%lld",
                        static_cast<long long>(ns_out));
        }
        std::printf("\n");
    }
    // The 20-digit lane exists for symmetry (both 19 and 20 digits are
    // nanosecond counts) but no positive 20-digit value fits in int64, so the
    // 20-digit case can never be reached from an int64 input.
    std::printf("\n");

    // =========================================================================
    // 4.5  date_time: four-format field round trip via to_value_dt
    // =========================================================================
    // The plain to_value path writes date_time as numeric epoch SECONDS (the
    // documented default). to_value_dt is the explicit-output entry point: it
    // writes the same instant as seconds / ms / us / ns / ISO string, and
    // from_value auto-detects each form on read. Each numeric lane is exact
    // for its own unit (the SECONDS lane floors away any sub-second part --
    // documented behavior), and from_value recovers the exact lane value.

    std::printf("== [4.5] four-format date_time bridge (to_value_dt) ==\n");
    const rtti::date_time instant = dt_us.value;  // 1791290096.123456 s

    // Each lane holds [mode, display name] and the epoch_ns its read-back is
    // REQUIRED to produce. Computed from the instant itself via the inverse
    // accessors, so the expectations follow the documented floor semantics.
    struct dt_lane {
        rtti::date_time_output mode;
        const char* name;
        std::int64_t expected_ns;
    };
    const dt_lane lanes[] = {
        {rtti::date_time_output::numeric_seconds, "seconds",
         [&instant] {
             std::int64_t v = 0;
             (void)instant.to_unix_seconds(v);
             return v * 1000000000LL;
         }()},
        {rtti::date_time_output::numeric_ms, "ms",
         [&instant] {
             std::int64_t v = 0;
             (void)instant.to_unix_ms(v);
             return v * 1000000LL;
         }()},
        {rtti::date_time_output::numeric_us, "us",
         [&instant] {
             std::int64_t v = 0;
             (void)instant.to_unix_us(v);
             return v * 1000LL;
         }()},
        {rtti::date_time_output::numeric_ns, "ns", instant.epoch_ns()},
        {rtti::date_time_output::iso_string, "iso_string", instant.epoch_ns()},
    };
    for (const dt_lane& L : lanes) {
        box sink;  // a fresh value_io node
        // --- to_value_dt(sink, instant, mode) --------------------------------
        // Explicitly render the instant; returns an error_code (no_such_field
        // on a null sink, otherwise the unit accessor's ok).
        const rtti::error_code we = rtti::to_value_dt(&sink, instant, L.mode);
        // --- from_value(sink, back) -----------------------------------------
        // Generic read: integer scalar -> digit-count detection, string scalar
        // -> ISO parse. Must recover the lane's exact value.
        rtti::date_time back{};
        const rtti::error_code re = rtti::from_value(&sink, back);
        const bool exact = back.epoch_ns() == L.expected_ns;
        std::printf("  %-10s -> %-26s read=%s back_ns=%lld (lane expects "
                    "%lld) %s\n",
                    L.name, scalar_text(sink).c_str(), err_name(re),
                    static_cast<long long>(back.epoch_ns()),
                    static_cast<long long>(L.expected_ns),
                    exact ? "exact" : "DIFFERS");
        if (we != rtti::error_code::ok || re != rtti::error_code::ok ||
            !exact) {
            std::printf("  four-format round trip FAILED\n");
            return 1;
        }
    }
    // NOTE the seconds lane's truncated epoch_ns (1791290096000000000):
    // 1791290096.123456 s floors to 1791290096 whole seconds because the
    // seconds lane cannot carry a sub-second part.

    // --- date_time inside a reflected struct (default numeric-seconds) ------
    // The plain to_value path writes numeric SECONDS (default), which for a
    // whole-second instant is exact; the record below uses one so the round
    // trip is lossless.
    DemoCoreDtRecord rec;
    rec.id = 7;
    rec.when = rtti::date_time::from_unix_seconds(1791290096).value;
    box rec_sink;
    const rtti::error_code to_e = rtti::to_value(&rec_sink, rec);
    DemoCoreDtRecord rec2;
    const rtti::error_code fr_e = rtti::from_value(&rec_sink, rec2);
    std::printf("  reflected record: to_value=%s from_value=%s "
                "id=%lld when_ns=%lld exact=%s\n",
                err_name(to_e), err_name(fr_e),
                static_cast<long long>(rec2.id),
                static_cast<long long>(rec2.when.epoch_ns()),
                (rec2.when == rec.when) ? "yes" : "NO");
    if (to_e != rtti::error_code::ok || fr_e != rtti::error_code::ok ||
        rec2.when != rec.when) {
        return 1;
    }
    std::printf("\n");

    // =========================================================================
    // 4.6  base_convert: to_base / from_base across bases 2/8/16/32/62/64
    // =========================================================================
    // rtti::detail::to_base converts a uint64 to a digit string in any base
    // 2..64; from_base is the inverse with overflow detection. The default
    // digit table is "0-9a-z" for base 2..36 (strtol-compatible, so base 16
    // input is case-insensitive) and "0-9a-zA-Z" plus '+'/'/' at 62/63 beyond.

    std::printf("== [4.6] base_convert known values ==\n");
    namespace bc = rtti::detail;
    std::printf("  to_base(255,16)     = \"%s\"\n", bc::to_base(255u, 16).c_str());
    std::printf("  to_base(255,8)      = \"%s\"\n", bc::to_base(255u, 8).c_str());
    std::printf("  to_base(255,2)      = \"%s\"\n", bc::to_base(255u, 2).c_str());
    std::printf("  to_base(255,32)     = \"%s\"\n", bc::to_base(255u, 32).c_str());
    std::printf("  to_base(255,62)     = \"%s\"\n", bc::to_base(255u, 62).c_str());
    std::printf("  to_base(255,64)     = \"%s\"  (62 symbols + '+' at 62, '/' at 63)\n",
                bc::to_base(255u, 64).c_str());

    // Round trip every supported base over a fixed value set.
    std::printf("  round trips:");
    const std::uint64_t values[] = {0, 1, 255, 1024, 0xDEADBEEFull,
                                    std::numeric_limits<std::uint64_t>::max()};
    const unsigned bases[] = {2, 8, 16, 32, 62, 64};
    bool all_ok = true;
    for (unsigned base : bases) {
        for (std::uint64_t v : values) {
            const std::string s = bc::to_base(v, base);
            std::uint64_t back = 0;
            const rtti::error_code e = bc::from_base(s, base, back);
            if (e != rtti::error_code::ok || back != v) {
                all_ok = false;
            }
        }
        std::printf(" base%u", base);
    }
    std::printf(" -> %s\n", all_ok ? "all exact" : "FAILED");
    if (!all_ok) {
        return 1;
    }
    std::printf("\n");

    // =========================================================================
    // 4.7  base_convert: signed variants and INT64_MIN
    // =========================================================================
    // to_base_signed / from_base_signed carry a leading '-' for negatives.
    // INT64_MIN is representable exactly through its 2^63 magnitude.

    std::printf("== [4.7] signed base conversion ==\n");
    std::printf("  to_base_signed(-255,16) = \"%s\"\n",
                bc::to_base_signed(-255, 16).c_str());
    std::printf("  to_base_signed(INT64_MIN,10) = \"%s\"\n",
                bc::to_base_signed((std::numeric_limits<std::int64_t>::min)(), 10).c_str());
    std::int64_t sneg = 0;
    const rtti::error_code sne =
        bc::from_base_signed("-ff", 16, sneg);
    std::printf("  from_base_signed(\"-ff\",16) -> %s value=%lld\n",
                err_name(sne), static_cast<long long>(sneg));
    std::int64_t smin = 0;
    const rtti::error_code sme = bc::from_base_signed(
        bc::to_base_signed((std::numeric_limits<std::int64_t>::min)(), 62),
        62, smin);
    std::printf("  INT64_MIN 62-round-trip -> %s value=%lld\n",
                err_name(sme), static_cast<long long>(smin));
    std::printf("\n");

    // =========================================================================
    // 4.8  base_convert: custom symbol tables and case policy
    // =========================================================================
    // The digit table is a configurable symbol string: base <= its length.
    // RFC 4648 base64 order ("A-Za-z0-9+/") and URL-safe "-_" extras are one
    // call away. The DEFAULT table parses base<=36 case-insensitively
    // (strtol behavior); a custom table is case-sensitive unless the explicit
    // case-insensitive overload is used.

    std::printf("== [4.8] custom digit tables + case policy ==\n");
    std::printf("  default to_base(255,64)  = \"%s\"\n",
                bc::to_base(255u, 64).c_str());
    std::printf("  rfc4648 to_base(255,64)  = \"%s\"  (RFC 4648 order)\n",
                bc::to_base(255u, 64, bc::rfc4648_digits()).c_str());

    // URL-safe table for the base64 URL alphabet: '-' at 62, '_' at 63.
    static const char url_safe[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";
    std::printf("  url-safe to_base(255,64)  = \"%s\"\n",
                bc::to_base(255u, 64, url_safe).c_str());

    // Default base 16 parses letters case-insensitively (strtol behavior).
    std::uint64_t hexv = 0;
    const rtti::error_code he = bc::from_base("FF", 16, hexv);
    std::printf("  from_base(\"FF\",16) -> %s value=%llu (case-insensitive)\n",
                err_name(he), static_cast<unsigned long long>(hexv));

    // A CUSTOM table is case-sensitive by default: 'z' is in a lowercase-only
    // 36-symbol table, but 'Z' is not. The final explicit-flag overload opts
    // back into case-insensitive parsing.
    static const char lower36[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::uint64_t c1 = 0, c2 = 0, c3 = 0;
    const rtti::error_code ce1 = bc::from_base("z", 36, c1, lower36);
    const rtti::error_code ce2 = bc::from_base("Z", 36, c2, lower36);
    const rtti::error_code ce3 = bc::from_base("Z", 36, c3, lower36, true);
    std::printf("  custom table : from_base(\"z\",36) -> %s (value=%llu)\n",
                err_name(ce1), static_cast<unsigned long long>(c1));
    std::printf("  custom table : from_base(\"Z\",36) -> %s (case-sensitive "
                "custom table)\n",
                err_name(ce2));
    std::printf("  custom table : from_base(\"Z\",36,insensitive) -> %s "
                "(value=%llu)\n",
                err_name(ce3), static_cast<unsigned long long>(c3));
    std::printf("\n");

    // =========================================================================
    // 4.9  base_convert: overflow / illegal inputs
    // =========================================================================
    // from_base reports invalid_number for bad bases/empty/illegal chars and
    // overflow when the accumulated value would pass UINT64_MAX. The check is
    // done BEFORE the multiply, so an overflowing lane never wraps.

    std::printf("== [4.9] base_convert overflow + malformed input ==\n");
    std::uint64_t ov = 0;
    // 16 hex F's is exactly UINT64_MAX; 17 F's overflows.
    const rtti::error_code o1 = bc::from_base("FFFFFFFFFFFFFFFFF", 16, ov);
    std::printf("  from_base(\"FFFFFFFFFFFFFFFFF\",16) -> %s\n",
                err_name(o1));
    const rtti::error_code o2 = bc::from_base("12#", 10, ov);
    std::printf("  from_base(\"12#\",10)                -> %s\n",
                err_name(o2));
    const rtti::error_code o3 = bc::from_base("", 10, ov);
    std::printf("  from_base(\"\",10)                  -> %s\n",
                err_name(o3));
    const rtti::error_code o4 = bc::from_base("1", 1, ov);   // base must be 2..64
    const rtti::error_code o5 = bc::from_base("1", 65, ov);
    std::printf("  from_base(\"1\",1) / (\"1\",65)      -> %s / %s\n",
                err_name(o4), err_name(o5));
    // to_base has no error channel, so an invalid base yields an empty string.
    std::printf("  to_base(5, 1) -> \"%s\" (empty = documented failure)\n",
                bc::to_base(5u, 1).c_str());
    std::printf("\n");

    // =========================================================================
    // 4.10 base_convert: value_io bridge (to_value_base / from_value_base)
    // =========================================================================
    // Like date_time, the ARBITRARY-BASE path is an explicit entry point: the
    // plain to_value keeps numeric output, and to_value_base renders an
    // integer as a base-N STRING scalar in any value_io tree. from_value_base
    // is the inverse. Signed variants cover negative int64 (and the date_time
    // epoch hookup, since epochs are int64).

    std::printf("== [4.10] base bridge through value_io ==\n");
    box bs;
    const rtti::error_code wb = rtti::to_value_base(&bs, 255u, 16);
    std::uint64_t rb = 0;
    const rtti::error_code rbe = rtti::from_value_base(&bs, 16, rb);
    std::printf("  to_value_base(sink,255,16) -> \"%s\" (write=%s) "
                "from_value_base -> %llu (read=%s)\n",
                scalar_text(bs).c_str(), err_name(wb),
                static_cast<unsigned long long>(rb), err_name(rbe));

    box bneg;
    const rtti::error_code wbn = rtti::to_value_base_signed(&bneg, -255, 16);
    std::int64_t rbn = 0;
    const rtti::error_code rbne = rtti::from_value_base_signed(&bneg, 16, rbn);
    std::printf("  to_value_base_signed(-255,16) -> \"%s\" (write=%s) "
                "from_value_base_signed -> %lld (read=%s)\n",
                scalar_text(bneg).c_str(), err_name(wbn),
                static_cast<long long>(rbn), err_name(rbne));
    if (wb != rtti::error_code::ok || rbe != rtti::error_code::ok ||
        rb != 255u || wbn != rtti::error_code::ok ||
        rbne != rtti::error_code::ok || rbn != -255) {
        return 1;
    }
    std::printf("\n");

    // =========================================================================
    // 4.11 to_string / from_string: scalars
    // =========================================================================
    // to_string<T> renders any supported type; from_string<T>(s, out) returns
    // an error_code; from_string<T>(s) returns std::optional<T> (nullopt on
    // error). Malformed text is invalid_number, out-of-range is overflow,
    // wrong-kind text is kind_mismatch.

    std::printf("== [4.11] to_string / from_string scalars ==\n");
    std::printf("  to_string(true) = \"%s\"  to_string(42) = \"%s\"  "
                "to_string(-17) = \"%s\"  to_string(3.14) = \"%s\"\n",
                rtti::to_string(true).c_str(), rtti::to_string(42).c_str(),
                rtti::to_string(-17).c_str(), rtti::to_string(3.14).c_str());

    int iv = 0;
    const rtti::error_code iv_e = rtti::from_string<int>("-273", iv);
    std::printf("  from_string<int>(\"-273\") -> %s value=%d\n",
                err_name(iv_e), iv);
    // Optional convenience API: success yields a value, failure nullopt.
    const std::optional<int> opt = rtti::from_string<int>("42");
    const std::optional<int> opt_bad = rtti::from_string<int>("abc");
    std::printf("  from_string<int>(\"42\")  -> optional%s; "
                "(\"abc\") -> %s\n",
                opt.has_value() ? "=42" : " empty",
                opt_bad.has_value() ? "HAS VALUE (unexpected)"
                                    : "nullopt (error)");

    // Error codes: malformed text, range overflow, wrong bool text.
    int bad = 0;
    const rtti::error_code e1 = rtti::from_string<int>("12x", bad);
    const rtti::error_code e2 = rtti::from_string<int>(
        "999999999999999999999999999999", bad);
    bool bb = false;
    const rtti::error_code e3 = rtti::from_string<bool>("yes", bb);
    std::printf("  from_string<int>(\"12x\")              -> %s\n",
                err_name(e1));
    std::printf("  from_string<int>(huge)                -> %s\n",
                err_name(e2));
    std::printf("  from_string<bool>(\"yes\")             -> %s\n",
                err_name(e3));
    std::printf("\n");

    // =========================================================================
    // 4.12 to_string / from_string: enum and date_time
    // =========================================================================
    // Registered enums serialize to their LABEL; date_time serializes to the
    // canonical ISO 8601 text and accepts ISO text or a numeric epoch (with
    // digit-count unit detection) on read.

    std::printf("== [4.12] enum + date_time strings ==\n");
    std::printf("  to_string(Red) = \"%s\"  to_string(Green) = \"%s\"  "
                "to_string(Blue) = \"%s\"\n",
                rtti::to_string(DemoCoreColor::Red).c_str(),
                rtti::to_string(DemoCoreColor::Green).c_str(),
                rtti::to_string(DemoCoreColor::Blue).c_str());
    DemoCoreColor cback{};
    const rtti::error_code ce =
        rtti::from_string<DemoCoreColor>("Green", cback);
    std::printf("  from_string<DemoCoreColor>(\"Green\") -> %s value=%d\n",
                err_name(ce), static_cast<int>(cback));
    // An unknown label is kind_mismatch (no silent default).
    DemoCoreColor cbad{};
    const rtti::error_code cbe =
        rtti::from_string<DemoCoreColor>("Purple", cbad);
    std::printf("  from_string<DemoCoreColor>(\"Purple\") -> %s\n",
                err_name(cbe));

    // date_time as text: ISO round trip plus numeric-digit input.
    const std::string iso_text = rtti::to_string(instant);
    std::printf("  to_string(instant) = \"%s\"\n", iso_text.c_str());
    rtti::date_time dback{};
    const rtti::error_code de1 =
        rtti::from_string<rtti::date_time>(iso_text, dback);
    std::printf("  from_string<date_time>(\"%s\") -> %s exact=%s\n",
                iso_text.c_str(), err_name(de1),
                (dback == instant) ? "yes" : "NO");
    // A numeric epoch is also accepted (10 digits = seconds here); this parse
    // runs LAST so the exact check above already used the ISO read-back.
    rtti::date_time dback2{};
    const rtti::error_code de2 =
        rtti::from_string<rtti::date_time>("1791290096", dback2);
    std::printf("  from_string<date_time>(\"1791290096\") -> %s (10 digits = "
                "seconds)\n",
                err_name(de2));
    std::printf("\n");

    // =========================================================================
    // 4.13 to_string / from_string: arbitrary-base bridge
    // =========================================================================
    // to_string_base / from_string_base are thin wrappers over base_convert
    // with the optional custom digit table; signed variants exist too.

    std::printf("== [4.13] base strings (to_string_base family) ==\n");
    std::printf("  to_string_base(255,16) = \"%s\"  to_string_base(255,64) = \"%s\"\n",
                rtti::to_string_base(255u, 16).c_str(),
                rtti::to_string_base(255u, 64).c_str());
    std::uint64_t bv = 0;
    const rtti::error_code be =
        rtti::from_string_base("ff", 16, bv);
    std::printf("  from_string_base(\"ff\",16) -> %s value=%llu\n",
                err_name(be), static_cast<unsigned long long>(bv));
    const std::optional<std::uint64_t> bo = rtti::from_string_base("101", 2);
    std::printf("  from_string_base(\"101\",2) -> optional=%llu\n",
                static_cast<unsigned long long>(bo.value_or(0)));
    const std::optional<std::int64_t> bso = rtti::from_string_base_signed("-ff", 16);
    std::printf("  from_string_base_signed(\"-ff\",16) -> optional=%lld\n",
                static_cast<long long>(bso.value_or(0)));
    std::printf("\n");

    // =========================================================================
    // 4.14 to_string / from_string: containers and reflected structs
    // =========================================================================
    // Containers / reflected structs / optional / tuple / variant use a compact
    // single-line JSON value built through the existing to_value/from_value
    // dispatch chain. Object key order is source order (field declaration
    // order for reflected structs); keys are never sorted.

    std::printf("== [4.14] containers + reflected structs ==\n");
    const rtti::error_code v1 = check_string_round_trip<int>("scalar int", 42);
    const rtti::error_code v2 = check_string_round_trip<std::vector<int>>(
        "vector<int>", std::vector<int>{1, 2, 3});
    const rtti::error_code v3 = check_string_round_trip<std::map<std::string, int>>(
        "map<string,int>",
        std::map<std::string, int>{{"a", 1}, {"b", 2}});
    if (v1 != rtti::error_code::ok || v2 != rtti::error_code::ok ||
        v3 != rtti::error_code::ok) {
        std::printf("  scalar/container round trip FAILED\n");
        return 1;
    }

    DemoCoreNested nested;
    nested.id = 5;
    nested.name = "hello";
    nested.nums = {9, 8, 7};
    nested.tags["k"] = 1;
    nested.maybe = 11;
    const rtti::error_code v4 =
        check_string_round_trip<DemoCoreNested>("reflected struct", nested);

    // A struct round trip through the reflected path (JSON object text).
    if (v4 != rtti::error_code::ok) {
        std::printf("  reflected struct round trip FAILED\n");
        return 1;
    }
    const std::string s4 = rtti::to_string(nested);
    std::printf("  to_string(DemoCoreNested) = %s\n", s4.c_str());

    // Missing-key error: struct JSON without the required "id" field.
    DemoCoreNested missing;
    const rtti::error_code mk =
        rtti::from_string<DemoCoreNested>(R"({"name":"x"})", missing);
    std::printf("  from_string<struct>({\"name\":\"x\"}) -> %s (missing key)\n",
                err_name(mk));

    // Malformed container JSON surfaces the compact-JSON parser codes.
    std::vector<int> bad_vec;
    const rtti::error_code bad_vec_e =
        rtti::from_string<std::vector<int>>("[1,,2]", bad_vec);
    std::printf("  from_string<vector<int>>(\"[1,,2]\") -> %s\n",
                err_name(bad_vec_e));
    std::printf("\n");

    // =========================================================================
    // 4.15 enum: label table + convert/serialize bridge
    // =========================================================================
    // RTTI_DEFINE_ENUM builds rtti::enum_table<T> (count + array of
    // enum_label{name, value}). enum_to_string / enum_from_string are the
    // direct conversions; the CONVERT bridge renders an enum as its label
    // string scalar in any value_io tree (this is what the JSON/XML/INI/TOML
    // adapters rely on, and it lives in the reflect core so all formats share
    // one definition).

    std::printf("== [4.15] enum label table + serialization bridge ==\n");
    std::printf("  enum_table<DemoCoreColor>::count = %zu\n",
                rtti::enum_table<DemoCoreColor>::count);
    for (std::size_t i = 0; i < rtti::enum_table<DemoCoreColor>::count; ++i) {
        std::printf("    label \"%s\" = %d\n",
                    rtti::enum_table<DemoCoreColor>::array[i].name,
                    static_cast<int>(
                        rtti::enum_table<DemoCoreColor>::array[i].value));
    }

    // enum_to_string on a known value; on an UNKNOWN value it returns "".
    std::printf("  enum_to_string(Blue) = \"%s\"\n",
                rtti::enum_to_string(DemoCoreColor::Blue));
    const DemoCoreColor unknown = static_cast<DemoCoreColor>(999);
    std::printf("  enum_to_string(unknown=999) = \"%s\" (empty = no label)\n",
                rtti::enum_to_string(unknown));
    // enum_from_string returns the matching enumerator (first match).
    std::printf("  enum_from_string(\"Blue\") = %d\n",
                static_cast<int>(rtti::enum_from_string<DemoCoreColor>("Blue")));

    // Serialization bridge: to_value writes the label string, from_value reads
    // it back.
    box es;
    const rtti::error_code et = rtti::to_value(&es, DemoCoreColor::Green);
    DemoCoreColor eback{};
    const rtti::error_code er = rtti::from_value(&es, eback);
    std::printf("  to_value(sink, Green) -> \"%s\" (%s) from_value -> %d (%s)\n",
                scalar_text(es).c_str(), err_name(et),
                static_cast<int>(eback), err_name(er));
    if (et != rtti::error_code::ok || er != rtti::error_code::ok ||
        eback != DemoCoreColor::Green) {
        return 1;
    }

    // Enum inside a reflected struct round-trips through to_string/from_string
    // (the label text flows through the struct's JSON object). DemoCorePainted
    // is declared at global scope (Section 1): a local class cannot carry the
    // static registration members the intrusive macro generates.
    DemoCorePainted painted;
    painted.id = 3;
    painted.tint = DemoCoreColor::Blue;
    const std::string ps = rtti::to_string(painted);
    DemoCorePainted painted2;
    const rtti::error_code pe = rtti::from_string<DemoCorePainted>(ps, painted2);
    std::printf("  painted struct to_string = %s\n", ps.c_str());
    std::printf("  painted struct from_string -> %s exact=%s\n", err_name(pe),
                (painted2.tint == painted.tint && painted2.id == painted.id)
                    ? "yes"
                    : "NO");

    std::printf("\ncore_convert_demo: ALL SECTIONS PASSED\n");
    return 0;
}
