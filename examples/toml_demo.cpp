// toml_demo.cpp : end-to-end usage example of the TOML format adapter.
//
// WHAT THIS FILE DOES
//   Demonstrates every public TOML adapter surface:
//     (1) DOM building  via rtti::value_io (make_object / make_node /
//         make_scalar / make_array / set_key / push_back); arrays render as
//         inline arrays, object children render as [tables].
//     (2) Parsing       rtti::toml::parse -> result<rtti::toml::document>.
//     (3) Serializing   serialize(doc, cfg) and serialize(doc, out, cfg)
//         (TOML buffer overload clears then writes, like JSON/INI).
//     (4) Streaming SAX rtti::toml::parse_stream(src, events).
//     (5) Reflect bridge rtti::toml::serialize<T>(obj) / parse_as<T>(src).
//     (6) Configuration  rtti::toml::write_config, incl. key_case naming
//         (identity / snake / camel) via the named-conversion path.
//     (7) Bad-text safety: unterminated strings / empty paths -> error_code.
//     (8) A realistic "load TOML config, tweak, write back" scenario.
//
// HOW TO COMPILE (MSVC; run from vcvars64.bat x64 prompt at repo root)
//   cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd
//      /I include examples\toml_demo.cpp
//      /link /LIBPATH:"%VCToolsInstallDir%\lib\x64"
//      /LIBPATH:"<SDK>\Lib\10.0.<ver>\ucrt\x64"
//      /LIBPATH:"<SDK>\Lib\10.0.<ver>\um\x64" build\Debug\rtti.lib
//
// EXPECTED OUTPUT
//   Eight labelled sections showing the TOML text each stage produced, plus
//   the error reports of the deliberate failure branches. Builds warning-free
//   (/W4, zero C4xxx) and exits cleanly.
//
// DEPENDENCIES / GATE
//   Requires #include <rtti/toml.hpp>; RTTI_ENABLE_TOML defaults to 1 in
//   <rtti/rtti_fwd.hpp> so the gate is open unless explicitly disabled.
//   No third-party dependency. English comments only; C++17.

#include <cstdint>                     // std::int64_t : integer field type.
#include <iostream>                    // std::cout : print demo sections.
#include <string>                      // std::string : payloads / buffers.
#include <vector>                      // std::vector : TOML inline array.
#include <rtti/rtti_fwd.hpp>           // rtti namespace + gate macro defaults.
#include <rtti/toml.hpp>               // rtti::toml: document / parse /
                                       //   serialize / parse_stream / config.

namespace rtti_demo_toml {
// A reflected enum: serializes as its label text, parses back from that text.
enum class Color { Red, Green, Blue };

// Nested reflected struct; maps to a [table] (a dotted/plain header).
struct Origin {
    std::int64_t x = 0;
    std::int64_t y = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(Origin, x, y)
};

// Main reflected struct. TOML mapping rules (toml.hpp header comment):
//   key -> scalar, dotted key / section path -> nested tables,
//   repeated [[array-table]] header -> array of tables, array value -> inline
//   array. Keys are used verbatim unless a naming policy is configured.
// `server_name` is snake_case on purpose so section (6) can demonstrate the
// key_case naming policy (identity stays, camel -> serverName, snake stays).
struct Config {
    std::string server_name = "localhost";
    std::int64_t port = 0;
    bool tls = false;
    Color color = Color::Red;
    std::vector<std::int64_t> ports;   // TOML inline array [1, 2, 3].
    Origin origin;                     // becomes [origin] table.
    RTTI_DEFINE_TYPE_INTRUSIVE(Config, server_name, port, tls, color, ports,
                               origin)
};
} // namespace rtti_demo_toml

// Enum registration at GLOBAL scope (the macro opens its own rtti namespace).
RTTI_DEFINE_ENUM(rtti_demo_toml::Color, Red, Green, Blue)

namespace {

// Print one error_info on one line: numeric code, English message, and the
// lazily-computed line/column. Used by all deliberate failure branches.
void print_error(const char* label, const rtti::error_info& err) {
    std::cout << "    [" << label << "] code=" << static_cast<int>(err.code)
              << " (" << err.message() << ") at line " << err.line()
              << ", col " << err.column() << "\n";
}

} // namespace

int main() {
    using namespace rtti;              // value_io / field_value / string_view.
    using namespace rtti::toml;        // document, parse, serialize, config.

    // ------------------------------------------------------------------ (1)
    // DOM BUILDING then SERIALIZE. The TOML document root is always the global
    // table. A scalar child becomes "key = value", a fresh array becomes an
    // inline array, and an object child becomes a [table].
    std::cout << "=== (1) DOM build then serialize ===\n";
    {
        document doc;                              // root = global table.
        value_io* h = doc.make_node();
        h->make_scalar(field_value::of(rtti::string_view("node-7")));
        doc.set_key("server_name", h);             // server_name = "node-7".
        value_io* p = doc.make_node();
        p->make_scalar(field_value::of(std::int64_t(8080)));
        doc.set_key("port", p);                    // port = 8080.
        // Inline array of integers.
        value_io* arr = doc.make_node();
        arr->make_array();                         // node becomes an array.
        for (std::int64_t i = 1; i <= 3; ++i) {
            value_io* el = arr->make_node();       // independent element node.
            el->make_scalar(field_value::of(i));
            arr->push_back(el);                    // appends to the array.
        }
        doc.set_key("ports", arr);                 // ports = [1, 2, 3].
        // Nested object renders as a [table].
        value_io* o = doc.make_node();
        o->make_object();
        value_io* ox = o->make_node();
        ox->make_scalar(field_value::of(std::int64_t(1)));
        o->set_key("x", ox);
        value_io* oy = o->make_node();
        oy->make_scalar(field_value::of(std::int64_t(2)));
        o->set_key("y", oy);
        doc.set_key("origin", o);                  // [origin] x=1 y=2.
        std::cout << serialize(doc, write_config{});
    }

    // ---- (2) PARSE ---------------------------------------------------------
    // parse(src) reads TOML into a document; result carries the document or an
    // error_info. Tables, dotted keys, inline arrays and quoted strings are
    // all handled by the parser.
    std::cout << "=== (2) parse a document string ===\n";
    {
        const char* src =
            "server_name = \"node-7\"\n"
            "port = 8080\n"
            "[origin]\n"
            "x = 1\n"
            "y = 2\n";
        result<document> r = parse(rtti::string_view(src));
        if (!r.ok()) {                             // syntax errors.
            print_error("parse", r.err);
            return 1;
        }
        std::int64_t port = 0;
        value_io* pv = nullptr;
        if (r.value.get_key("port", pv) && pv != nullptr) {
            pv->as_integer(port);                  // false on kind mismatch.
        }
        // Nested table is a child object; read it back via get_key.
        value_io* sec = nullptr;
        std::int64_t xv = 0;
        if (r.value.get_key("origin", sec) && sec != nullptr) {
            value_io* xvptr = nullptr;
            if (sec->get_key("x", xvptr) && xvptr != nullptr) {
                xvptr->as_integer(xv);
            }
        }
        std::cout << "    parsed port=" << port << " origin.x=" << xv << "\n";
    }

    // ---- (3) SERIALIZE: both entry points ----------------------------------
    std::cout << "=== (3) serialize: string and buffer entry points ===\n";
    {
        document d;
        value_io* k = d.make_node();
        k->make_scalar(field_value::of(std::int64_t(42)));
        d.set_key("answer", k);
        // (a) String-returning overload.
        const std::string s = serialize(d, write_config{});
        // (b) Buffer overload: clears the caller string first, so repeated
        // calls overwrite; reserved capacity is reused across calls.
        std::string buf;
        buf.reserve(128);
        serialize(d, buf, write_config{});
        std::cout << "    string overload: " << s;
        std::cout << "    buffer  overload: " << buf;
    }

    // ---- (4) STREAMING / SAX ------------------------------------------------
    std::cout << "=== (4) parse_stream SAX events ===\n";
    {
        const char* src =
            "server_name = \"node-7\"\n"
            "ports = [1, 2, 3]\n"      // array_start/array_end + int events.
            "[origin]\n"
            "x = 1\n";
        parse_events ev;              // empty slots are simply not invoked.
        ev.table_start = [](string_view n) {
            std::cout << "    table_start \""
                      << std::string(n.data(), n.size()) << "\"\n";
            return true;                          // false cancels the parse.
        };
        ev.table_end = [](string_view n) {
            std::cout << "    table_end   \""
                      << std::string(n.data(), n.size()) << "\"\n";
            return true;
        };
        ev.array_start = [] { std::cout << "    [array\n"; return true; };
        ev.array_end = [] { std::cout << "    ]array\n";   return true; };
        ev.integer_value = [](std::int64_t v) {
            std::cout << "      int " << v << "\n";
            return true;
        };
        ev.key_value = [](string_view k, string_view v) {
            std::cout << "    key_value   " << std::string(k.data(), k.size())
                      << " = " << std::string(v.data(), v.size()) << "\n";
            return true;
        };
        result<void> sr = parse_stream(rtti::string_view(src), ev);
        std::cout << "    streaming ok=" << (sr.ok() ? "yes" : "no") << "\n";
    }

    // ---- (5) REFLECT BRIDGE round-trip --------------------------------------
    std::cout << "=== (5) serialize<T>/parse_as<T> reflection bridge ===\n";
    {
        rtti_demo_toml::Config c;
        c.server_name = "node-a";
        c.port = 8080;
        c.tls = true;
        c.color = rtti_demo_toml::Color::Blue;
        c.ports = {7, 8, 9};                      // inline array.
        c.origin.x = 11;
        c.origin.y = 22;

        // Struct -> TOML text (runs to_value under the hood).
        const std::string text = serialize(c, write_config{});
        std::cout << "    serialized:\n" << text;

        // TOML text -> struct (runs from_value under the hood).
        result<rtti_demo_toml::Config> back =
            parse_as<rtti_demo_toml::Config>(rtti::string_view(text));
        if (!back.ok()) {
            print_error("parse_as round-trip", back.err);
            return 1;
        }
        std::cout << "    parsed back server_name=" << back.value.server_name
                  << " port=" << back.value.port
                  << " tls=" << (back.value.tls ? "true" : "false")
                  << " ports[0]=" << back.value.ports.at(0)
                  << " origin.y=" << back.value.origin.y << "\n";

        // DELIBERATE ERROR BRANCH: the int field cannot take a quoted string,
        // so from_value reports kind mismatch; parse_as surfaces it in the
        // result instead of throwing or crashing.
        const char* bad =
            "server_name = \"n\"\nport = \"oops\"\n";  // string into int field.
        result<rtti_demo_toml::Config> rbad =
            parse_as<rtti_demo_toml::Config>(rtti::string_view(bad));
        if (!rbad.ok()) {
            print_error("expected reflect error", rbad.err);
        }
    }

    // ---- (6) CONFIGURATION ------------------------------------------------
    std::cout << "=== (6) write_config: key_case naming policy ===\n";
    {
        rtti_demo_toml::Config c;
        c.server_name = "node-a";
        c.port = 8080;

        // Default: field names are used verbatim (identity).
        std::cout << "    identity:\n" << serialize(c, write_config{});
        // snake: already-snake names stay unchanged (server_name intact).
        write_config snake;
        snake.key_case = rtti::detail::key_case::snake;
        std::cout << "    snake:\n" << serialize(c, snake);
        // camel: server_name is rewritten to serverName on the way out.
        write_config camel;
        camel.key_case = rtti::detail::key_case::camel;
        std::cout << "    camel:\n" << serialize(c, camel);
    }

    // ---- (7) MALFORMED INPUT SAFETY ----------------------------------------
    // Bad TOML surfaces an error_code; it never throws or crashes (spec
    // section 0: bottom layer responsibility).
    std::cout << "=== (7) malformed input never crashes ===\n";
    {
        const char* bad1 = "title = \"unterminated";  // unterminated string.
        result<document> r1 = parse(rtti::string_view(bad1));
        if (!r1.ok()) { print_error("unterminated string", r1.err); }

        const char* bad2 = "[origin";              // unterminated table header.
        result<document> r2 = parse(rtti::string_view(bad2));
        if (!r2.ok()) { print_error("unterminated table", r2.err); }

        const char* bad3 = "k = [";                // unterminated inline array.
        result<document> r3 = parse(rtti::string_view(bad3));
        if (!r3.ok()) { print_error("unterminated array", r3.err); }
    }

    // ---- (8) REAL-WORLD SCENARIO: ingest, tweak, emit ----------------------
    std::cout << "=== (8) read TOML config, tweak value, write back ===\n";
    {
        // Simulate the contents of an on-disk TOML settings file.
        const std::string file =
            "server_name = \"relay\"\nport = 8080\ntls = true\ncolor = \"Green\"\n"
            "ports = [1, 2]\n"
            "[origin]\nx = 1\ny = 2\n";
        result<rtti_demo_toml::Config> cfg =
            parse_as<rtti_demo_toml::Config>(rtti::string_view(file));
        if (!cfg.ok()) {
            print_error("config load", cfg.err);
            return 1;
        }
        // Tweak one setting and append a port, then persist.
        cfg.value.port += 1;
        cfg.value.ports.push_back(3);
        const std::string saved = serialize(cfg.value, write_config{});
        std::cout << "    saved config:\n" << saved;
    }
    return 0;
}