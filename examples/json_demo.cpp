// json_demo.cpp : end-to-end usage example of the JSON format adapter.
//
// WHAT THIS FILE DOES
//   Demonstrates every public JSON adapter surface:
//     (1) DOM building  via rtti::value_io (make_object / make_node /
//         make_scalar / set_key) followed by serialization.
//     (2) Parsing       rtti::json::parse -> result<rtti::json::document>.
//     (3) Serializing   rtti::json::serialize(doc, cfg) and the buffer entry
//         point serialize(doc, out, cfg).
//     (4) Streaming SAX rtti::json::parse_stream(src, events).
//     (5) Reflect bridge rtti::json::serialize<T>(obj) / parse_as<T>(src).
//     (6) Configuration  rtti::json::write_config{indent, sort_keys, ...}.
//     (7) Bad-text safety: errors come back as error_code, never a crash.
//     (8) A realistic "read config, tweak, write back" scenario.
//
// HOW TO COMPILE (MSVC; run from vcvars64.bat x64 prompt at repo root)
//   cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd
//      /I include examples\json_demo.cpp
//      /link /LIBPATH:"%VCToolsInstallDir%\lib\x64"
//      /LIBPATH:"<SDK>\Lib\10.0.<ver>\ucrt\x64"
//      /LIBPATH:"<SDK>\Lib\10.0.<ver>\um\x64" build\Debug\rtti.lib
//
// EXPECTED OUTPUT
//   Eight labelled sections printing the JSON each stage produced plus the
//   error report of the malformed-input/DEMONSTRATION-of-error branches.
//   Builds warning-free (/W4, zero C4xxx) and exits cleanly.
//
// DEPENDENCIES / GATE
//   Requires #include <rtti/json.hpp>; RTTI_ENABLE_JSON defaults to 1 in
//   <rtti/rtti_fwd.hpp> so the gate is open unless explicitly disabled
//   (-DRTTI_ENABLE_JSON=0 makes this header a hard compile error).
//   No third-party dependency. English comments only; C++17.

#include <cstddef>                     // std::size_t : index loops over keys.
#include <cstdint>                     // std::int64_t : integer fields/events.
#include <iostream>                    // std::cout : print demo sections.
#include <string>                      // std::string : payloads and buffers.
#include <vector>                      // std::vector : reflected array field.
#include <rtti/rtti_fwd.hpp>           // rtti namespace + gate macro defaults.
#include <rtti/json.hpp>               // rtti::json: document / parse /
                                       //   serialize / parse_stream / config.

namespace rtti_demo_json {
// A reflected enum: registered with RTTI_DEFINE_ENUM so the reflect core can
// serialize Color as its label text ("Green") and parse it back from such text.
enum class Color { Red, Green, Blue };

// Nested reflected struct; round-trips as a nested JSON object. The intrusive
// macro goes INSIDE the class body (its nested rtti_registration struct is
// legal there) and registers each public member by name. The JsonOrigin name
// prefix keeps this type unique across the four example translation units.
struct JsonOrigin {
    std::int64_t x = 0;
    std::int64_t y = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(JsonOrigin, x, y)
};

// Main reflected struct: a JSON object whose keys are the field names below.
// Field set exercises string / integer / bool / vector / enum / nested struct
// so every generic converter participates in the round trip.
struct JsonSettings {
    std::string name = "default";
    std::int64_t level = 0;
    bool active = false;
    Color primary = Color::Red;
    std::vector<std::int64_t> tags;    // JSON array of int64.
    JsonOrigin origin;                 // nested JSON object.
    RTTI_DEFINE_TYPE_INTRUSIVE(JsonSettings, name, level, active, primary,
                               tags, origin)
};
} // namespace rtti_demo_json

// Enum registration must appear at GLOBAL scope: RTTI_DEFINE_ENUM opens
// `namespace rtti { ... }`, and calling it inside rtti_demo_json would create
// a nested rtti_demo_json::rtti specialization (wrong target namespace).
RTTI_DEFINE_ENUM(rtti_demo_json::Color, Red, Green, Blue)

namespace {

// Print one error_info on one line: numeric code, English message, and the
// lazily-computed line/column of the offending byte. Used by the malformed
// input section and by every deliberate error demo.
void print_error(const char* label, const rtti::error_info& err) {
    std::cout << "    [" << label << "] code=" << static_cast<int>(err.code)
              << " (" << err.message() << ") at line " << err.line()
              << ", col " << err.column() << "\n";
}

// Short name of a value_io::kind for readable kind reporting in section (2).
const char* kind_name(rtti::value_io::kind k) {
    switch (k) {
        case rtti::value_io::kind::null:    return "null";
        case rtti::value_io::kind::boolean: return "boolean";
        case rtti::value_io::kind::integer: return "integer";
        case rtti::value_io::kind::real:    return "real";
        case rtti::value_io::kind::string:  return "string";
        case rtti::value_io::kind::array:   return "array";
        case rtti::value_io::kind::object:  return "object";
    }
    return "?";
}

} // namespace

int main() {
    using namespace rtti;              // value_io / field_value / string_view.
    using namespace rtti::json;        // document, parse, serialize, write_config.

    // ------------------------------------------------------------------ (1)
    // DOM BUILDING then SERIALIZE. Construct a tree by hand through the same
    // value_io write side the reflect core uses, then write it as text.
    std::cout << "=== (1) DOM build then serialize ===\n";
    {
        document doc;                              // owns arena + node pool.
        doc.make_object();                         // root becomes a JSON object.
        value_io* n = doc.make_node();             // allocated child node.
        n->make_scalar(field_value::of(rtti::string_view("node-7")));
        doc.set_key("name", n);                    // {"name":"node-7"}.
        value_io* lv = doc.make_node();
        lv->make_scalar(field_value::of(std::int64_t(3)));
        doc.set_key("level", lv);                  // {"level":3}.
        // Nested object under key "probe": a fresh node turned into an object
        // then populated like the document itself.
        value_io* probe = doc.make_node();
        probe->make_object();
        value_io* px = probe->make_node();
        px->make_scalar(field_value::of(std::int64_t(1)));
        probe->set_key("x", px);
        value_io* flu = probe->make_node();
        flu->make_scalar(field_value::of(2.5));
        probe->set_key("f", flu);
        doc.set_key("probe", probe);               // {"probe":{"x":1,"f":2.5}}.
        std::cout << doc.write(write_config{}) << "\n";
    }

    // ---- (2) PARSE ---------------------------------------------------------
    // parse(src) builds a document. result<document> carries either the value
    // or an error_info; ok() distinguishes the two outcomes.
    std::cout << "=== (2) parse a document string ===\n";
    {
        const char* src = "{\"a\":1,\"b\":[true,null],\"c\":\"hi\"}";
        result<document> r = parse(rtti::string_view(src));
        if (!r.ok()) {                             // syntax/trivial errors.
            print_error("parse", r.err);
            return 1;
        }
        // Read individual members back through the value_io source interface.
        std::int64_t a = 0;                        // filled by as_integer().
        value_io* av = nullptr;
        if (r.value.get_key("a", av) && av != nullptr) {
            av->as_integer(a);                     // false on kind mismatch.
        }
        // Enumerate every key/value pair in document order with get_key_at.
        for (std::size_t i = 0; i < r.value.size(); ++i) {
            string_view k;
            value_io* v = nullptr;
            if (!r.value.get_key_at(i, k, v)) {
                break;                             // out of range or not object.
            }
            std::cout << "    key=" << std::string(k.data(), k.size())
                      << " kind=" << kind_name(v->type()) << "\n";
        }
        std::cout << "    parsed a=" << a << "\n";
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
        // (b) Buffer overload: appends into a caller-owned std::string but
        // clears it first so repeated calls overwrite instead of appending.
        std::string buf;
        buf.reserve(128);                          // reuse capacity across calls.
        serialize(d, buf, write_config{});
        std::cout << "    string overload: " << s << "\n";
        std::cout << "    buffer  overload: " << buf << "\n";
    }

    // ---- (4) STREAMING / SAX ------------------------------------------------
    std::cout << "=== (4) parse_stream SAX events ===\n";
    {
        const char* src = "{\"list\":[1,2,3],\"flag\":true}";
        parse_events ev;              // any empty slot is simply not invoked.
        ev.object_start = [] { std::cout << "    {start\n"; return true; };
        ev.object_end   = [] { std::cout << "    }end\n";   return true; };
        ev.array_start  = [] { std::cout << "    [start\n"; return true; };
        ev.array_end    = [] { std::cout << "    ]end\n";   return true; };
        ev.key = [](string_view k) {
            std::cout << "      key " << std::string(k.data(), k.size()) << "\n";
            return true;                          // false would cancel the parse.
        };
        ev.integer_value = [](std::int64_t v) {
            std::cout << "      int " << v << "\n";
            return true;
        };
        ev.bool_value = [](bool b) {
            std::cout << "      bool " << (b ? "true" : "false") << "\n";
            return true;
        };
        result<void> sr = parse_stream(rtti::string_view(src), ev);
        // ok: full successful parse, or early cancel from a callback; error:
        // syntax problem. Here it must be ok.
        std::cout << "    streaming ok=" << (sr.ok() ? "yes" : "no") << "\n";
    }

    // ---- (5) REFLECT BRIDGE round-trip --------------------------------------
    std::cout << "=== (5) serialize<T>/parse_as<T> reflection bridge ===\n";
    {
        rtti_demo_json::JsonSettings obj;
        obj.name = "node-a";
        obj.level = 4;
        obj.active = true;
        obj.primary = rtti_demo_json::Color::Blue;
        obj.origin.x = 11;
        obj.origin.y = 22;
        obj.tags = {7, 8, 9};                      // becomes a JSON array.

        // Struct -> JSON text (runs to_value under the hood).
        const std::string text = serialize(obj, write_config{});
        std::cout << "    serialized:\n" << text << "\n";

        // JSON text -> struct (runs from_value under the hood).
        result<rtti_demo_json::JsonSettings> back =
            parse_as<rtti_demo_json::JsonSettings>(rtti::string_view(text));
        if (!back.ok()) {
            print_error("parse_as round-trip", back.err);
            return 1;
        }
        std::cout << "    parsed back level=" << back.value.level
                  << " primary=" << static_cast<int>(back.value.primary)
                  << " tags[0]=" << back.value.tags.at(0) << "\n";

        // DELIBERATE ERROR BRANCH: this object cannot be populated from a
        // raw string root and only a few top-level keys exist, so from_value
        // reports an error (here: a missing required field, error_code 18).
        // parse_as surfaces it in the result's error_info instead of throwing
        // or crashing, which is exactly the failure path a caller must handle.
        result<rtti_demo_json::JsonSettings> bad =
            parse_as<rtti_demo_json::JsonSettings>(
                rtti::string_view("{\"level\":\"oops\"}"));
        if (!bad.ok()) {
            print_error("expected reflect error", bad.err);
        }
    }

    // ---- (6) CONFIGURATION ------------------------------------------------
    std::cout << "=== (6) write_config: sort_keys and indent ===\n";
    {
        document doc;
        doc.make_object();
        value_io* z = doc.make_node();
        z->make_scalar(field_value::of(std::int64_t(1)));
        doc.set_key("zebra", z);
        value_io* a = doc.make_node();
        a->make_scalar(field_value::of(std::int64_t(2)));
        doc.set_key("apple", a);

        std::cout << "    default:\n" << serialize(doc, write_config{}) << "\n";
        write_config sorted;
        sorted.sort_keys = true;                   // deterministic key ordering.
        std::cout << "    sort_keys:\n" << serialize(doc, sorted) << "\n";
        write_config compact;
        compact.indent = 0;                        // single-line output.
        std::cout << "    indent=0: " << serialize(doc, compact) << "\n";
        write_config tabs;
        tabs.indent = 1;                           // tab-char indentation.
        tabs.indent_char = '\t';
        std::cout << "    indent char '\\t':\n" << serialize(doc, tabs) << "\n";
    }

    // ---- (7) MALFORMED INPUT SAFETY ----------------------------------------
    // The contract (spec section 0: bottom layer responsibility) is that a
    // bad payload surfaces an error_code plus a byte offset and never crashes.
    std::cout << "=== (7) malformed input never crashes ===\n";
    {
        const char* bad1 = "{\"a\":1,}";           // trailing comma.
        result<document> r1 = parse(rtti::string_view(bad1));
        if (!r1.ok()) { print_error("trailing comma", r1.err); }

        const char* bad2 = "[1,2";                 // unterminated array.
        result<document> r2 = parse(rtti::string_view(bad2));
        if (!r2.ok()) { print_error("unterminated array", r2.err); }

        const char* bad3 = "\"unclosed";           // unterminated string.
        result<document> r3 = parse(rtti::string_view(bad3));
        if (!r3.ok()) { print_error("unterminated string", r3.err); }
    }

    // ---- (8) REAL-WORLD SCENARIO: ingest, tweak, emit ----------------------
    std::cout << "=== (8) read config, tweak value, write back ===\n";
    {
        // Simulate the contents of an on-disk settings JSON file.
        const std::string file =
            "{\"name\":\"relay\",\"level\":2,\"active\":true,"
            "\"primary\":\"Green\",\"tags\":[1,2],\"origin\":{\"x\":1,\"y\":2}}";
        result<rtti_demo_json::JsonSettings> cfg =
            parse_as<rtti_demo_json::JsonSettings>(rtti::string_view(file));
        if (!cfg.ok()) {
            print_error("config load", cfg.err);
            return 1;
        }
        // Tweak one setting in memory...
        cfg.value.level += 1;                      // bump the retry level.
        cfg.value.tags.push_back(3);               // append a tag.
        // ...then persist the document back with pretty formatting.
        write_config cfg_out;
        cfg_out.indent = 2;
        const std::string saved = serialize(cfg.value, cfg_out);
        std::cout << "    saved config:\n" << saved << "\n";
    }
    return 0;
}
