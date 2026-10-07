// xml_demo.cpp : end-to-end usage example of the XML format adapter.
//
// WHAT THIS FILE DOES
//   Demonstrates every public XML adapter surface:
//     (1) DOM building  via rtti::value_io (make_object / make_node /
//         make_scalar / set_key) followed by serialization.
//     (2) Parsing       rtti::xml::parse -> result<rtti::xml::document>.
//     (3) Serializing   serialize(doc, cfg) and serialize(doc, out, cfg)
//         (XML buffer entry point APPENDS; call out.clear() first).
//     (4) Streaming SAX rtti::xml::parse_stream(src, events).
//     (5) Reflect bridge rtti::xml::serialize<T>(obj) / parse_as<T>(src),
//         including attribute binding and element-text binding.
//     (6) Configuration  rtti::xml::write_config{with_declaration, indent}.
//     (7) Bad-text safety: mismatched tags / bad entities -> error_code only.
//     (8) A realistic "load XML config, tweak, write back" scenario.
//
// HOW TO COMPILE (MSVC; run from vcvars64.bat x64 prompt at repo root)
//   cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd
//      /I include examples\xml_demo.cpp
//      /link /LIBPATH:"%VCToolsInstallDir%\lib\x64"
//      /LIBPATH:"<SDK>\Lib\10.0.<ver>\ucrt\x64"
//      /LIBPATH:"<SDK>\Lib\10.0.<ver>\um\x64" build\Debug\rtti.lib
//
// EXPECTED OUTPUT
//   Eight labelled sections showing the XML text each stage produced, plus
//   the error reports of the deliberate failure branches. Builds warning-free
//   (/W4, zero C4xxx) and exits cleanly.
//
// DEPENDENCIES / GATE
//   Requires #include <rtti/xml.hpp>; RTTI_ENABLE_XML defaults to 1 in
//   <rtti/rtti_fwd.hpp> so the gate is open unless explicitly disabled.
//   No third-party dependency. English comments only; C++17.

#include <cstdint>                     // std::int64_t : integer field type.
#include <iostream>                    // std::cout : print demo sections.
#include <string>                      // std::string : payloads / buffers.
#include <rtti/rtti_fwd.hpp>           // rtti namespace + gate macro defaults.
#include <rtti/xml.hpp>                // rtti::xml: document / parse /
                                       //   serialize / parse_stream / config.

namespace rtti_demo_xml {
// A reflected enum: registered with RTTI_DEFINE_ENUM so the reflect core can
// serialize Color as its label text and parse it back from that label.
enum class Color { Red, Green, Blue };

// Nested reflected struct; maps to a child element parsed as an object. The
// XmlOrigin prefix keeps this type unique across the four example TUs.
struct XmlOrigin {
    std::int64_t x = 0;
    std::int64_t y = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(XmlOrigin, x, y)
};

// Main reflected struct. XML mapping rules: scalar fields bind to child
// element text, attributes win over same-named child elements, nested structs
// bind to child elements, and a field named "value"/"_text" binds to the
// element's own text (shown in section 5 via OriginPoint).
struct XmlHost {
    std::int64_t id = 0;
    std::string name = "host";
    double score = 0.0;
    bool active = false;
    Color color = Color::Red;
    XmlOrigin origin;
    RTTI_DEFINE_TYPE_INTRUSIVE(XmlHost, id, name, score, active, color, origin)
};

// Element-text binding: the field literally named "value" maps to the root
// element's own text content (see the header mapping rules in xml.hpp).
struct XmlRawValue {
    std::string value;
    RTTI_DEFINE_TYPE_INTRUSIVE(XmlRawValue, value)
};
} // namespace rtti_demo_xml

// Enum registration must appear at GLOBAL scope (RTTI_DEFINE_ENUM opens its
// own `namespace rtti { ... }`, which must not nest inside rtti_demo_xml).
RTTI_DEFINE_ENUM(rtti_demo_xml::Color, Red, Green, Blue)

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
    using namespace rtti::xml;         // document, parse, serialize, config.

    // ------------------------------------------------------------------ (1)
    // DOM BUILDING then SERIALIZE. Build the tree by hand through the same
    // value_io write side the reflect core uses, then write it as XML.
    std::cout << "=== (1) DOM build then serialize ===\n";
    {
        document doc;                              // owns arena + node pool.
        doc.make_object();                         // root becomes element "root".
        value_io* id = doc.make_node();            // child element node.
        id->make_scalar(field_value::of(std::int64_t(7)));
        doc.set_key("id", id);                     // <id>7</id>.
        value_io* nm = doc.make_node();
        nm->make_scalar(field_value::of(rtti::string_view("node-7")));
        doc.set_key("name", nm);                   // <name>node-7</name>.
        // Nested element: a fresh node turned into an object then populated.
        value_io* o = doc.make_node();
        o->make_object();
        value_io* ox = o->make_node();
        ox->make_scalar(field_value::of(std::int64_t(1)));
        o->set_key("x", ox);
        value_io* oy = o->make_node();
        oy->make_scalar(field_value::of(std::int64_t(2)));
        o->set_key("y", oy);
        doc.set_key("origin", o);                  // <origin><x>1</x><y>2</y></origin>.
        std::cout << serialize(doc, write_config{}) << "\n";
    }

    // ---- (2) PARSE ---------------------------------------------------------
    // parse(src) reads XML into a document; result carries the document or an
    // error_info. Attributes, child text and entities are all decoded here.
    std::cout << "=== (2) parse a document string ===\n";
    {
        const char* src =
            "<root><id>7</id><name>Zoe</name><active>true</active></root>";
        result<document> r = parse(rtti::string_view(src));
        if (!r.ok()) {                             // syntax / structure errors.
            print_error("parse", r.err);
            return 1;
        }
        // Read scalar members back through the value_io source interface.
        std::int64_t idv = 0;
        value_io* idn = nullptr;
        if (r.value.get_key("id", idn) && idn != nullptr) {
            idn->as_integer(idv);                  // false on kind mismatch.
        }
        // Enumerate every child key/value pair with get_key_at.
        for (std::size_t i = 0; i < r.value.size(); ++i) {
            string_view k;
            value_io* v = nullptr;
            if (!r.value.get_key_at(i, k, v)) {
                break;                             // out of range / not object.
            }
            std::cout << "    child " << std::string(k.data(), k.size())
                      << " kind=" << static_cast<int>(v->type()) << "\n";
        }
        std::cout << "    parsed id=" << idv << "\n";
    }

    // ---- (3) SERIALIZE: both entry points ----------------------------------
    std::cout << "=== (3) serialize: string and buffer entry points ===\n";
    {
        document d;
        d.make_object();
        value_io* k = d.make_node();
        k->make_scalar(field_value::of(std::int64_t(42)));
        d.set_key("answer", k);
        // (a) String-returning overload.
        const std::string s = serialize(d, write_config{});
        // (b) Buffer overload: APPENDS into the caller's string (unlike the
        // JSON/INI/TOML overloads which clear first), so clear() before use.
        std::string buf;
        buf.reserve(128);
        buf.clear();
        serialize(d, buf, write_config{});
        std::cout << "    string overload:\n" << s << "\n";
        std::cout << "    buffer  overload:\n" << buf << "\n";
    }

    // ---- (4) STREAMING / SAX ------------------------------------------------
    std::cout << "=== (4) parse_stream SAX events ===\n";
    {
        const char* src =
            "<root id=\"7\">text<!-- note --><![CDATA[<raw>]]></root>";
        parse_events ev;              // any empty slot is simply not invoked.
        ev.element_start = [](string_view n) {
            std::cout << "    element_start " << std::string(n.data(), n.size())
                      << "\n";
            return true;                          // false cancels the parse.
        };
        ev.element_end = [](string_view n) {
            std::cout << "    element_end   " << std::string(n.data(), n.size())
                      << "\n";
            return true;
        };
        ev.attribute = [](string_view n, string_view v) {
            std::cout << "    attribute     " << std::string(n.data(), n.size())
                      << "=" << std::string(v.data(), v.size()) << "\n";
            return true;
        };
        ev.text = [](string_view t) {
            std::cout << "    text          " << std::string(t.data(), t.size())
                      << "\n";
            return true;
        };
        ev.comment = [](string_view c) {
            std::cout << "    comment       " << std::string(c.data(), c.size())
                      << "\n";
            return true;
        };
        ev.cdata = [](string_view c) {
            std::cout << "    cdata         " << std::string(c.data(), c.size())
                      << "\n";
            return true;
        };
        result<void> sr = parse_stream(rtti::string_view(src), ev);
        std::cout << "    streaming ok=" << (sr.ok() ? "yes" : "no") << "\n";
    }

    // ---- (5) REFLECT BRIDGE round-trip --------------------------------------
    std::cout << "=== (5) serialize<T>/parse_as<T> reflection bridge ===\n";
    {
        rtti_demo_xml::XmlHost h;
        h.id = 7;
        h.name = "Alice <&> \"Q\"";                // entities escaped on write.
        h.score = 3.25;
        h.active = true;
        h.color = rtti_demo_xml::Color::Blue;
        h.origin.x = 5;
        h.origin.y = 6;

        // Struct -> XML text (runs to_value under the hood).
        const std::string text = serialize(h, write_config{});
        std::cout << "    serialized:\n" << text << "\n";

        // XML text -> struct (runs from_value under the hood).
        result<rtti_demo_xml::XmlHost> back =
            parse_as<rtti_demo_xml::XmlHost>(rtti::string_view(text));
        if (!back.ok()) {
            print_error("parse_as round-trip", back.err);
            return 1;
        }
        std::cout << "    parsed back id=" << back.value.id
                  << " name=" << back.value.name
                  << " color=" << static_cast<int>(back.value.color)
                  << " origin.x=" << back.value.origin.x << "\n";

        // Element-text binding: <root>text</root> fills the field named value.
        result<rtti_demo_xml::XmlRawValue> rv =
            parse_as<rtti_demo_xml::XmlRawValue>(rtti::string_view("<root>hi</root>"));
        if (!rv.ok()) {
            print_error("value binding", rv.err);
            return 1;
        }
        std::cout << "    _value binding read=\"" << rv.value.value << "\"\n";

        // DELIBERATE ERROR BRANCH: a scalar root ("42") is not an object, so
        // the document cannot map to a struct; parse_as reports it (here
        // error_code 12, invalid root) instead of throwing or crashing.
        result<rtti_demo_xml::XmlHost> bad =
            parse_as<rtti_demo_xml::XmlHost>(rtti::string_view("42"));
        if (!bad.ok()) {
            print_error("expected reflect error", bad.err);
        }
    }

    // ---- (6) CONFIGURATION ------------------------------------------------
    std::cout << "=== (6) write_config: declaration and indentation ===\n";
    {
        document doc;
        doc.make_object();
        value_io* a = doc.make_node();
        a->make_scalar(field_value::of(std::int64_t(1)));
        doc.set_key("a", a);

        std::cout << "    default (declaration + indent 4):\n"
                  << serialize(doc, write_config{}) << "\n";
        write_config nodecl;
        nodecl.with_declaration = false;           // drop the <?xml ...?> line.
        std::cout << "    no declaration:\n" << serialize(doc, nodecl) << "\n";
        write_config compact;
        compact.with_declaration = false;
        compact.indent = 0;                        // single-line output.
        std::cout << "    compact: " << serialize(doc, compact) << "\n";
        write_config tabs;
        tabs.with_declaration = false;
        tabs.indent = 1;                           // one tab per level.
        tabs.indent_char = '\t';
        std::cout << "    tab indent:\n" << serialize(doc, tabs) << "\n";
    }

    // ---- (7) MALFORMED INPUT SAFETY ----------------------------------------
    // Bad XML surfaces an error_code plus a byte offset; it never throws or
    // crashes (spec section 0: bottom layer responsibility).
    std::cout << "=== (7) malformed input never crashes ===\n";
    {
        const char* bad1 = "<root><a></b></root>"; // mismatched close tag.
        result<document> r1 = parse(rtti::string_view(bad1));
        if (!r1.ok()) { print_error("mismatched tag", r1.err); }

        const char* bad2 = "<root>&bogus;</root>"; // undefined entity.
        result<document> r2 = parse(rtti::string_view(bad2));
        if (!r2.ok()) { print_error("bad entity", r2.err); }

        const char* bad3 = "<root>";               // unterminated element.
        result<document> r3 = parse(rtti::string_view(bad3));
        if (!r3.ok()) { print_error("unterminated", r3.err); }
    }

    // ---- (8) REAL-WORLD SCENARIO: ingest, tweak, emit ----------------------
    std::cout << "=== (8) read XML config, tweak value, write back ===\n";
    {
        // Simulate the contents of an on-disk XML settings file.
        const std::string file =
            "<host><id>3</id><name>relay</name><score>1.5</score>"
            "<active>true</active><color>Green</color>"
            "<origin><x>1</x><y>2</y></origin></host>";
        result<rtti_demo_xml::XmlHost> cfg =
            parse_as<rtti_demo_xml::XmlHost>(rtti::string_view(file));
        if (!cfg.ok()) {
            print_error("config load", cfg.err);
            return 1;
        }
        // Tweak a couple of settings in memory...
        cfg.value.name = "relay-v2";
        cfg.value.score += 0.5;
        // ...then persist the document back with declaration and indent.
        write_config out_cfg;
        out_cfg.indent = 2;
        const std::string saved = serialize(cfg.value, out_cfg);
        std::cout << "    saved config:\n" << saved << "\n";
    }
    return 0;
}
