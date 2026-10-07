// ini_demo.cpp : end-to-end usage example of the INI format adapter.
//
// WHAT THIS FILE DOES
//   Demonstrates every public INI adapter surface:
//     (1) DOM building  via rtti::value_io (make_object / make_node /
//         make_scalar / set_key); INI renders keys in the global area and
//         nested objects as [sections].
//     (2) Parsing       rtti::ini::parse -> result<rtti::ini::document>.
//     (3) Serializing   serialize(doc, cfg) and serialize(doc, out, cfg)
//         (INI buffer overload clears then writes, like JSON/TOML).
//     (4) Streaming SAX rtti::ini::parse_stream(src, events) with the
//         section_start / section_end / key_value callbacks.
//     (5) Reflect bridge rtti::ini::serialize<T>(obj) / parse_as<T>(src).
//     (6) Configuration  rtti::ini::write_config{with_comments}.
//     (7) Bad-text safety: bad lines / missing values -> error_code only.
//     (8) A realistic "load INI config, tweak, write back" scenario.
//
// HOW TO COMPILE (MSVC; run from vcvars64.bat x64 prompt at repo root)
//   cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd
//      /I include examples\ini_demo.cpp
//      /link /LIBPATH:"%VCToolsInstallDir%\lib\x64"
//      /LIBPATH:"<SDK>\Lib\10.0.<ver>\ucrt\x64"
//      /LIBPATH:"<SDK>\Lib\10.0.<ver>\um\x64" build\Debug\rtti.lib
//
// EXPECTED OUTPUT
//   Eight labelled sections showing the INI text each stage produced, plus
//   the error reports of the deliberate failure branches. Builds warning-free
//   (/W4, zero C4xxx) and exits cleanly.
//
// DEPENDENCIES / GATE
//   Requires #include <rtti/ini.hpp>; RTTI_ENABLE_INI defaults to 1 in
//   <rtti/rtti_fwd.hpp> so the gate is open unless explicitly disabled.
//   No third-party dependency. English comments only; C++17.

#include <cstdint>                     // std::int64_t : integer field type.
#include <iostream>                    // std::cout : print demo sections.
#include <string>                      // std::string : payloads / buffers.
#include <vector>                      // std::vector : repeated-key array.
#include <rtti/rtti_fwd.hpp>           // rtti namespace + gate macro defaults.
#include <rtti/ini.hpp>                // rtti::ini: document / parse /
                                       //   serialize / parse_stream / config.

namespace rtti_demo_ini {
// A reflected enum: serializes as its label text, parses back from that text.
enum class Color { Red, Green, Blue };

// Nested reflected struct; maps to a [section] (a dotted or plain header).
// The IniOrigin prefix keeps this type unique across the four example TUs.
struct IniOrigin {
    std::int64_t x = 0;
    std::int64_t y = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(IniOrigin, x, y)
};

// Main reflected struct. INI mapping rules (ini.hpp header comment):
//   key -> scalar (global or per-section), repeated key -> array slot,
//   nested struct / map field -> section, dotted section name -> nesting.
struct IniConfig {
    std::string host = "localhost";
    std::int64_t port = 0;
    bool tls = false;
    Color color = Color::Red;
    std::vector<std::int64_t> ports;   // repeated key -> INI array slot.
    IniOrigin origin;                  // becomes [origin] x/y.
    RTTI_DEFINE_TYPE_INTRUSIVE(IniConfig, host, port, tls, color, ports, origin)
};
} // namespace rtti_demo_ini

// Enum registration at GLOBAL scope (the macro opens its own rtti namespace).
RTTI_DEFINE_ENUM(rtti_demo_ini::Color, Red, Green, Blue)

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
    using namespace rtti::ini;         // document, parse, serialize, config.

    // ------------------------------------------------------------------ (1)
    // DOM BUILDING then SERIALIZE. The INI document root is always the global
    // area; a scalar child becomes "key = value" and an object child becomes
    // a [section] with its keys inside.
    std::cout << "=== (1) DOM build then serialize ===\n";
    {
        document doc;                              // root object pre-created.
        value_io* h = doc.make_node();
        h->make_scalar(field_value::of(rtti::string_view("node-7")));
        doc.set_key("host", h);                    // host = node-7 (global).
        value_io* p = doc.make_node();
        p->make_scalar(field_value::of(std::int64_t(8080)));
        doc.set_key("port", p);                    // port = 8080 (global).
        // Nested object: rendered as a [section] with keys inside.
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
    // parse(src) reads INI into a document; result carries the document or an
    // error_info. Sections, quoted values and inline '=' / ':' separators are
    // all handled by the parser.
    std::cout << "=== (2) parse a document string ===\n";
    {
        const char* src =
            "host = \"node-7\"\n"
            "port = 8080\n"
            "[origin]\n"
            "x = 1\n"
            "y = 2\n";
        result<document> r = parse(rtti::string_view(src));
        if (!r.ok()) {                             // malformed-line errors.
            print_error("parse", r.err);
            return 1;
        }
        std::int64_t port = 0;
        value_io* pv = nullptr;
        if (r.value.get_key("port", pv) && pv != nullptr) {
            pv->as_integer(port);                  // false on kind mismatch.
        }
        // Nested section is a child object; read it back via get_key.
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
            "tls = true\n"              // global-area key.
            "[origin]\n"
            "x = 1\n"
            "y = 2\n";
        parse_events ev;              // empty slots are simply not invoked.
        ev.section_start = [](string_view n) {
            std::cout << "    section_start \""
                      << std::string(n.data(), n.size()) << "\"\n";
            return true;                          // false cancels the parse.
        };
        ev.section_end = [](string_view n) {
            std::cout << "    section_end   \""
                      << std::string(n.data(), n.size()) << "\"\n";
            return true;
        };
        ev.key_value = [](string_view k, string_view v) {
            std::cout << "    key_value     "
                      << std::string(k.data(), k.size()) << " = "
                      << std::string(v.data(), v.size()) << "\n";
            return true;
        };
        result<void> sr = parse_stream(rtti::string_view(src), ev);
        std::cout << "    streaming ok=" << (sr.ok() ? "yes" : "no") << "\n";
    }

    // ---- (5) REFLECT BRIDGE round-trip --------------------------------------
    std::cout << "=== (5) serialize<T>/parse_as<T> reflection bridge ===\n";
    {
        rtti_demo_ini::IniConfig c;
        c.host = "node-a";
        c.port = 8080;
        c.tls = true;
        c.color = rtti_demo_ini::Color::Blue;
        c.ports = {7, 8, 9};           // 3+ elements round-trip as repeated keys.
        c.origin.x = 11;
        c.origin.y = 22;

        // Struct -> INI text (runs to_value under the hood).
        const std::string text = serialize(c, write_config{});
        std::cout << "    serialized:\n" << text;

        // INI text -> struct (runs from_value under the hood).
        result<rtti_demo_ini::IniConfig> back =
            parse_as<rtti_demo_ini::IniConfig>(rtti::string_view(text));
        if (!back.ok()) {
            print_error("parse_as round-trip", back.err);
            return 1;
        }
        std::cout << "    parsed back host=" << back.value.host
                  << " port=" << back.value.port
                  << " tls=" << (back.value.tls ? "true" : "false")
                  << " ports[0]=" << back.value.ports.at(0)
                  << " origin.y=" << back.value.origin.y << "\n";

        // DELIBERATE ERROR BRANCH: the int field cannot take a non-numeric
        // value, so from_value reports a conversion error; parse_as surfaces
        // it in the result instead of throwing or crashing.
        const char* bad =
            "host = node-a\nport = nope\n";        // port text is not numeric.
        result<rtti_demo_ini::IniConfig> rbad =
            parse_as<rtti_demo_ini::IniConfig>(rtti::string_view(bad));
        if (!rbad.ok()) {
            print_error("expected reflect error", rbad.err);
        }
    }

    // ---- (6) CONFIGURATION ------------------------------------------------
    std::cout << "=== (6) write_config: with_comments ===\n";
    {
        // Comments captured at parse time are re-emitted only when the config
        // asks for them (with_comments), letting you preserve a user file.
        const char* src =
            "; server endpoint\n"
            "host = node-a\n"
            "port = 8080\n";
        result<document> rd = parse(rtti::string_view(src));
        if (!rd.ok()) {
            print_error("config parse", rd.err);
            return 1;
        }
        std::cout << "    without comments:\n" << serialize(rd.value, write_config{});
        write_config withc;
        withc.with_comments = true;                // re-emit ';' lines.
        std::cout << "    with comments:\n" << serialize(rd.value, withc);
    }

    // ---- (7) MALFORMED INPUT SAFETY ----------------------------------------
    // Bad INI surfaces an error_code plus a byte offset; it never throws or
    // crashes (spec section 0: bottom layer responsibility).
    std::cout << "=== (7) malformed input never crashes ===\n";
    {
        const char* bad1 = "k = \n";               // key with no value.
        result<document> r1 = parse(rtti::string_view(bad1));
        if (!r1.ok()) { print_error("missing value", r1.err); }

        const char* bad2 = "[abc\n";               // unterminated section.
        result<document> r2 = parse(rtti::string_view(bad2));
        if (!r2.ok()) { print_error("unterminated section", r2.err); }

        const char* bad3 = "justtext\n";           // no '=' or ':' separator.
        result<document> r3 = parse(rtti::string_view(bad3));
        if (!r3.ok()) { print_error("bad line", r3.err); }
    }

    // ---- (8) REAL-WORLD SCENARIO: ingest, tweak, emit ----------------------
    std::cout << "=== (8) read INI config, tweak value, write back ===\n";
    {
        // Simulate the contents of an on-disk INI settings file.
        const std::string file =
            "host = relay\nport = 8080\ntls = true\ncolor = Green\n"
            "ports = 1\nports = 2\n"
            "[origin]\nx = 1\ny = 2\n";
        result<rtti_demo_ini::IniConfig> cfg =
            parse_as<rtti_demo_ini::IniConfig>(rtti::string_view(file));
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
