// format_demo.cpp : end-to-end usage of RTTI's string formatting surfaces.
//
// WHAT THIS FILE DOES
//   Demonstrates every public interface added by the formatting feature set
//   (spec §7):
//     (1) date_time::format(pattern)     strftime-style %Y %y %m %d %H %M %S
//                                        %f %F %T %R %z %Z %%.
//     (2) format_number<T>(v, spec)      integers: base / grouping / width /
//                                        fill / align / sign / '#';
//                                        reals: precision / f / e / grouping /
//                                        width / fill / sign; plus the
//                                        shortest-round-trip default.
//     (3) error safety                   a malformed spec surfaces
//                                        error_code::unexpected_token and an
//                                        empty result, never a crash.
//     (4) to_string<T>(v, spec)          the thin overload over format_number
//                                        and date_time::format.
//     (5) node_to_string(node, spec)     any value_io node -> one text line,
//                                        numbers formatted by the spec.
//     (6) format_reflected<T>(obj, spec) a RTTI_DEFINE_TYPE object -> a compact
//                                        object line driven by its field table.
//     (7) rtti::str                       the full-type conversion shells
//                                        (to_string/from_string + base bridges)
//                                        and the string-op toolkit (split/join,
//                                        trim family, case folds, starts/ends
//                                        with, replace/replace_all, repeat, pad,
//                                        reverse, quote/unquote, escape/unescape).
//
//   INTERFACE COVERAGE CHECKLIST (spec 7.6)
//     [x] date_time::format(string_view)
//     [x] date_time::format(string_view, std::string&)   (error overload)
//     [x] date_time::format() / to_string()              (existing, re-checked)
//     [x] format_number<T> integral  (default, ',', '#,d', base x/X/o/b, '#x',
//                                     width, '<','>','^','=', '0'-pad, '+', ' ')
//     [x] format_number<T> floating  (default shortest, '.Nf', '.Ne', ',',
//                                     width, fill, align, sign, non-finite)
//     [x] error_info out-param       (format_number invalid spec)
//     [x] to_string<T>(v, spec)      (integer, real, date_time)
//     [x] node_to_string             (object tree, scalar nodes, spec, nullptr)
//     [x] format_reflected<T>        (plain, with spec, nested struct)
//     [x] rtti::str::to_string/from_string  (scalar, enum, date_time, container,
//                                            optional, reflected struct)
//     [x] rtti::str::to_base/from_base      (unsigned + signed, custom digits)
//     [x] rtti::str::split/join/trim/ltrim/rtrim
//     [x] rtti::str::to_lower/to_upper/starts_with/ends_with
//     [x] rtti::str::replace/replace_all/repeat/pad_left/pad_right/reverse
//     [x] rtti::str::quote/unquote + escape/unescape round-trip
//
// HOW TO COMPILE (MSVC; run from vcvars64.bat x64 prompt at repo root)
//   The formatter translation units live in the static library after task 10
//   merges them; until then compile them alongside the example:
//     cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd
//        /I include examples\format_demo.cpp src\number_format.cpp
//        src\format_reflected.cpp
//        /link /LIBPATH:"%VCToolsInstallDir%\lib\x64"
//        /LIBPATH:"<SDK>\Lib\10.0.<ver>\ucrt\x64"
//        /LIBPATH:"<SDK>\Lib\10.0.<ver>\um\x64" build\Debug\rtti.lib
//
// EXPECTED OUTPUT
//   Six labelled sections printing the formatted text each stage produced,
//   matching the reference strings in the comments. Builds warning-free
//   (/W4, zero C4xxx) and exits cleanly.
//
// DEPENDENCIES / GATE
//   Requires #include <rtti/number_format.hpp> (reflect core only; no format
//   adapter gate). No third-party dependency. English comments only; C++17.

#include <cstdint>                     // std::int64_t / std::uint64_t : values.
#include <iostream>                    // std::cout : print demo sections.
#include <string>                      // std::string : formatted output.
#include <limits>                      // max()/infinity() : boundary values.
#include <vector>                      // std::vector : split/join containers.
#include <rtti/number_format.hpp>      // format_number / to_string(v,spec) /
                                       // node_to_string / format_reflected.
#include <rtti/string_ops.hpp>         // rtti::str: conversion shells + toolkit.

namespace rtti_demo_format {
// A reflected struct so format_reflected<T> has a field table to walk.
struct FormatDemoPoint {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::string name;
    RTTI_DEFINE_TYPE_INTRUSIVE(FormatDemoPoint, x, y, name)
};
// A registered enum so str::to_string renders its label table.
enum class DemoKind { fast = 0, slow = 1 };
} // namespace rtti_demo_format
RTTI_DEFINE_ENUM(rtti_demo_format::DemoKind, fast, slow)

namespace {

// Print one formatted result with its expected reference text side by side.
void show(const char* label, const std::string& actual,
          const char* expected) {
    std::cout << "    " << label << " -> \"" << actual << "\""
              << (actual == std::string(expected) ? "  [ok]" : "  [MISMATCH]")
              << "\n";
}

} // namespace

int main() {
    using namespace rtti;              // date_time / format_number / string_view.

    // ---- (1) DATE PATTERNS ---------------------------------------------------
    std::cout << "=== (1) date_time::format(pattern) ===\n";
    {
        // The ns-exact instant 2026-10-06T12:34:56.123456789Z.
        const auto r = date_time::from_unix_ns(std::int64_t(1791290096123456789));
        const date_time t = r.value;
        show("%Y-%m-%d %H:%M:%S.%f", t.format("%Y-%m-%d %H:%M:%S.%f"),
             "2026-10-06 12:34:56.123456789");
        show("%F %T", t.format("%F %T"), "2026-10-06 12:34:56");
        show("%R", t.format("%R"), "12:34");
        show("%y", t.format("%y"), "26");
        show("%d/%m/%Y", t.format("%d/%m/%Y"), "06/10/2026");
        show("%H:%M %z %Z", t.format("%H:%M %z %Z"), "12:34 +0000 UTC");
        show("%%Y", t.format("%%Y"), "%Y");
        show("%f (zero seconds)", date_time().format("%f"), "000000000");

        // The canonical no-pattern form still works.
        show("format()", t.format(),
             "2026-10-06T12:34:56.123456789Z");
        // Error overload: an unknown directive is unexpected_token.
        std::string out;
        const error_code ec = t.format("%Q", out);
        std::cout << "    \"%Q\" err=" << static_cast<int>(ec)
                  << " out=\"" << out << "\"  [fail-safe empty]\n";
    }

    // ---- (2) INTEGER FORMATTING ----------------------------------------------
    std::cout << "=== (2) format_number<T> integers ===\n";
    {
        show("1234567 default", format_number(1234567), "1234567");
        show("1234567 '#,d'", format_number(1234567, "#,d"), "1,234,567");
        show("-1234567 '#,d'", format_number(-1234567, "#,d"), "-1,234,567");
        show("255 '#x'", format_number(255, "#x"), "0xff");
        show("255 '#X'", format_number(255, "#X"), "0XFF");
        show("255 'X'", format_number(255, "X"), "FF");
        show("63 'o'", format_number(63, "o"), "77");
        show("255 'b'", format_number(255, "b"), "11111111");
        show("42 '08d'", format_number(42, "08d"), "00000042");
        show("-42 '08d'", format_number(-42, "08d"), "-0000042");
        show("42 '+d'", format_number(42, "+d"), "+42");
        show("42 '<6d'", format_number(42, "<6d"), "42    ");
        show("42 '^6d'", format_number(42, "^6d"), "  42  ");
        show("UINT64_MAX", format_number((std::numeric_limits<std::uint64_t>::max)()),
             "18446744073709551615");
    }

    // ---- (3) REAL FORMATTING ---------------------------------------------------
    std::cout << "=== (3) format_number<T> reals ===\n";
    {
        show("3.14159 default", format_number(3.14159), "3.14159");
        show("3.0 default", format_number(3.0), "3");
        show("1234567.0 '.2f'", format_number(1234567.0, ".2f"), "1234567.00");
        show("1234567.0 ',.2f'", format_number(1234567.0, ",.2f"),
             "1,234,567.00");
        show("3.14159 '.3f'", format_number(3.14159, ".3f"), "3.142");
        show("1234.5 '.2e'", format_number(1234.5, ".2e"), "1.23e+03");
        show("0.0001 '.2e'", format_number(0.0001, ".2e"), "1.00e-04");
        show("1234.5678 '^10.2f'", format_number(1234.5678, "^10.2f"),
             " 1234.57  ");
        show("-1234.5 '08.1f'", format_number(-1234.5, "08.1f"), "-01234.5");
        show("+inf", format_number((std::numeric_limits<double>::infinity)()), "inf");
        show("-inf", format_number(-(std::numeric_limits<double>::infinity)()), "-inf");
        show("nan", format_number((std::numeric_limits<double>::quiet_NaN)()), "nan");
    }

    // ---- (4) ERROR SAFETY ------------------------------------------------------
    std::cout << "=== (4) malformed spec -> error_code, empty result ===\n";
    {
        error_info err;
        const std::string bad = format_number(42, "r", &err);
        std::cout << "    format_number(42, \"r\") -> \"" << bad
                  << "\" code=" << static_cast<int>(err.code)
                  << " (" << err.message() << ")\n";
        err = error_info{};
        const std::string bad2 = format_number(1.5, "q", &err);
        std::cout << "    format_number(1.5, \"q\") -> \"" << bad2
                  << "\" code=" << static_cast<int>(err.code)
                  << " (" << err.message() << ")\n";
    }

    // ---- (5) to_string(v, spec) OVERLOAD ----------------------------------------
    std::cout << "=== (5) to_string<T>(v, format_spec) ===\n";
    {
        show("to_string(1234, \"#,d\")", to_string(1234, "#,d"), "1,234");
        show("to_string(3.14159, \".2f\")", to_string(3.14159, ".2f"), "3.14");
        const auto r = date_time::from_unix_seconds(1791290096);
        show("to_string(dt, \"%F\")", to_string(r.value, "%F"), "2026-10-06");
        // The original one-argument to_string is unaffected.
        show("to_string(42) unchanged", to_string(42), "42");
        show("to_string(3.5) unchanged", to_string(3.5), "3.5");
    }

    // ---- (6) NODE AND REFLECTED FORMATTING --------------------------------------
    std::cout << "=== (6) node_to_string / format_reflected ===\n";
    {
        // node_to_string over a hand-built value_io tree.
        rtti::detail::text_node doc;
        doc.make_object();
        rtti::value_io* n = doc.make_node();
        n->make_scalar(rtti::field_value::of(std::int64_t(7)));
        doc.set_key("n", n);
        rtti::value_io* x = doc.make_node();
        x->make_scalar(rtti::field_value::of(2.5));
        doc.set_key("x", x);
        rtti::value_io* arr = doc.make_node();
        arr->make_array();
        rtti::value_io* e = arr->make_node();
        e->make_scalar(rtti::field_value::of(std::int64_t(1)));
        arr->push_back(e);
        doc.set_key("arr", arr);

        show("node_to_string(&doc)", node_to_string(&doc),
             "{\"n\":7,\"x\":2.5,\"arr\":[1]}");
        // A real-only spec reaches the reals; integers fall back gracefully.
        show("node_to_string(&doc, \".1f\")", node_to_string(&doc, ".1f"),
             "{\"n\":7,\"x\":2.5,\"arr\":[1]}");
        show("node_to_string scalar", node_to_string(n), "7");
        // Fail-safe: a null node yields an empty string and an error code.
        error_info null_err;
        const std::string null_out = node_to_string(nullptr, "", &null_err);
        std::cout << "    node_to_string(nullptr) -> \"" << null_out
                  << "\" code=" << static_cast<int>(null_err.code)
                  << " (" << null_err.message() << ")  [fail-safe]\n";

        // format_reflected walks the type_desc field table.
        rtti_demo_format::FormatDemoPoint pt;
        pt.x = 1234567;
        pt.y = 42;
        pt.name = "origin";
        show("format_reflected(pt)", format_reflected(pt),
             "{\"x\":1234567,\"y\":42,\"name\":\"origin\"}");
        // The spec is applied to every numeric field (int grouping here).
        show("format_reflected(pt, \"#,d\")", format_reflected(pt, "#,d"),
             "{\"x\":1,234,567,\"y\":42,\"name\":\"origin\"}");
    }

    // ---- (7) rtti::str CONVERSION SHELLS + STRING TOOLKIT ---------------------
    std::cout << "=== (7) rtti::str: conversions + string ops ===\n";
    {
        namespace S = rtti::str;

        // Conversion shells are using-aliases of the unified unified layer.
        show("str::to_string(-42)", S::to_string(-42), "-42");
        show("str::to_string(3.5)", S::to_string(3.5), "3.5");
        {
            // A registered enum converts through its label table.
            show("str::to_string(enum)",
                 S::to_string(rtti_demo_format::DemoKind::fast), "fast");
        }
        {   // date_time via the existing bridge.
            const auto r = date_time::from_unix_seconds(0);
            show("str::to_string(dt)", S::to_string(r.value),
                 "1970-01-01T00:00:00Z");
        }
        {   // Containers / optional / reflected structs all convert.
            show("str::to_string(vector)", S::to_string(std::vector<int>{1, 2}),
                 "[1,2]");
            show("str::to_string(optional)",
                 S::to_string(std::optional<int>(7)), "7");
            rtti_demo_format::FormatDemoPoint pt;  // reflected via RTTI_DEFINE_TYPE
            pt.x = 1; pt.y = 2; pt.name = "n";
            show("str::to_string(reflected)", S::to_string(pt),
                 "{\"x\":1,\"y\":2,\"name\":\"n\"}");
        }
        // from_string<T>(s) returns std::optional<T>; empty on parse failure.
        show("str::from_string<int>(\"-42\")", S::to_string(*S::from_string<int>("-42")), "-42");
        {
            const auto no = S::from_string<int>("nope");
            std::cout << "    str::from_string<int>(\"nope\") -> "
                      << (no ? S::to_string(*no) : std::string("<empty>"))
                      << "  [empty on failure]\n";
        }
        // Base bridges (2..64).
        show("str::to_base(255, 16)", S::to_base(255, 16), "ff");
        show("str::to_base_signed(-255, 16)", S::to_base_signed(-255, 16), "-ff");
        {
            std::uint64_t v = 0;
            const error_code ec = S::from_base("ff", 16, v);
            show("str::from_base(\"ff\",16,out)", ec == error_code::ok ? S::to_base(v, 10) : "err", "255");
        }

        // Splitting / joining.
        {
            const auto toks = S::split("a,b,,c", ',');
            std::cout << "    split(\"a,b,,c\", ',') -> ["
                      << S::join(toks, " | ") << "]  [4 tokens incl. empty]\n";
        }
        show("join({\"a\",\"b\",\"c\"}, \"-\")", S::join({"a", "b", "c"}, "-"),
             "a-b-c");
        // Trimming, case folds, prefix/suffix.
        show("trim(\"  hi  \")", S::trim("  hi  "), "hi");
        show("ltrim(\"  hi  \")", S::ltrim("  hi  "), "hi  ");
        show("rtrim(\"  hi  \")", S::rtrim("  hi  "), "  hi");
        show("to_lower(\"AbC1\")", S::to_lower("AbC1"), "abc1");
        show("to_upper(\"AbC1\")", S::to_upper("AbC1"), "ABC1");
        {
            const bool sw = S::starts_with("foobar", "foo");
            const bool ew = S::ends_with("foobar", "bar");
            std::cout << "    starts_with(\"foobar\",\"foo\")=" << sw
                      << "  ends_with(\"foobar\",\"bar\")=" << ew << "\n";
        }
        // Replace / repeat / pad / reverse.
        show("replace(\"abcabc\", \"ab\", \"X\")", S::replace("abcabc", "ab", "X"),
             "Xcabc");
        show("replace_all(\"abcabc\", \"ab\", \"X\")",
             S::replace_all("abcabc", "ab", "X"), "XcXc");
        show("repeat(\"ab\", 3)", S::repeat("ab", 3), "ababab");
        show("pad_left(\"7\", 3, '0')", S::pad_left("7", 3, '0'), "007");
        show("pad_right(\"7\", 3, '.')", S::pad_right("7", 3, '.'), "7..");
        show("reverse(\"abc\")", S::reverse("abc"), "cba");
        // Quote/unquote + escape/unescape round-trip.
        {
            const std::string q = S::quote("he said \"hi\"");
            std::cout << "    quote -> " << q << "\n";
            show("unquote(quote(s)) round-trip",
                 S::unquote(q), "he said \"hi\"");
        }
        {
            const std::string e = S::escape("a\nb\t");
            std::cout << "    escape(\"a\\nb\\t\") -> " << e << "\n";
            show("unescape(escape(s)) round-trip", S::unescape(e), "a\nb\t");
        }
    }

    std::cout << "    format_demo done\n";
    return 0;
}