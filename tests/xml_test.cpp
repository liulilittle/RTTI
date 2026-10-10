// XML format adapter tests.
// Covers parsing (elements/attributes/text/mixed content/CDATA/comments/PIs/
// self-closing/nesting/entities/namespaces), error paths, serialization
// round-trips (compact and pretty), and reflection binding
// (attribute / child-element / _text / repeated-child collection / nested /
// enum). Structs are defined at global scope; test functions live in
// namespace rtti_test_lib. English comments only.

#include <rtti/xml.hpp>
#include "test_harness.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

// --- Reflected types at global scope ---------------------------------------

enum class XmlColor { Red, Green, Blue };

RTTI_DEFINE_ENUM(XmlColor, Red, Green, Blue)

struct Point {
    int x = 0;
    int y = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(Point, x, y)
};

struct Stats {
    int id = 0;
    double score = 0.0;
    std::string name;
    std::vector<int> scores;
    std::vector<Point> points;
    std::map<std::string, int> table;
    std::set<std::string> tags;
    Point center;
    XmlColor color = XmlColor::Red;
    bool active = false;
    RTTI_DEFINE_TYPE_INTRUSIVE(Stats, id, score, name, scores, points, table,
                               tags, center, color, active)
};

struct HandInfo {
    int id = 0;
    std::string name;
    double score = 0.0;
    bool active = false;
    RTTI_DEFINE_TYPE_INTRUSIVE(HandInfo, id, name, score, active)
};

struct PlainText {
    std::string value;
    RTTI_DEFINE_TYPE_INTRUSIVE(PlainText, value)
};

struct TextNode {
    std::string _text;
    RTTI_DEFINE_TYPE_INTRUSIVE(TextNode, _text)
};

// Streaming SAX event recorder (uniquely named to avoid ODR clashes with
// other test translation units). Records one XmlSaxEvent per callback and can
// be configured to cancel (return false) on a specific event.
struct XmlSaxEvent {
    std::string op;  // start/attr/text/end/comment/cdata/pi
    std::string a;   // primary payload (name, text, content)
    std::string b;   // secondary payload (attribute value only)
};

struct XmlSaxRecorder {
    std::vector<XmlSaxEvent> events;
    bool cancel_on = false;  // when set, cancel on the first matching event
    std::string cancel_op;
    std::string cancel_a;
    bool canceled = false;

    bool start(rtti::string_view n) { return record("start", n, ""); }
    bool end(rtti::string_view n) { return record("end", n, ""); }
    bool attr(rtti::string_view n, rtti::string_view v) {
        return record("attr", n, v);
    }
    bool text(rtti::string_view t) { return record("text", t, ""); }
    bool comment(rtti::string_view c) { return record("comment", c, ""); }
    bool cdata(rtti::string_view c) { return record("cdata", c, ""); }
    bool pi(rtti::string_view p) { return record("pi", p, ""); }

    std::string last() const {
        return events.empty() ? std::string("<none>")
                              : (events.back().op + ":" + events.back().a +
                                 (events.back().op == "attr" ? "=" + events.back().b
                                                             : std::string()));
    }

private:
    bool record(const char* op, rtti::string_view a, rtti::string_view b) {
        events.push_back(XmlSaxEvent{op, std::string(a), std::string(b)});
        if (cancel_on && op == cancel_op && a == cancel_a) {
            canceled = true;
            return false;
        }
        return true;
    }
};

namespace rtti_test_lib {

namespace {

void check_stats_eq(const Stats& a, const Stats& b) {
    RTTI_CHECK_EQ(a.id, b.id);
    RTTI_CHECK(a.score == b.score);
    RTTI_CHECK_EQ(a.name, b.name);
    RTTI_CHECK_EQ(a.scores.size(), b.scores.size());
    for (std::size_t i = 0; i < a.scores.size(); ++i) {
        RTTI_CHECK_EQ(a.scores[i], b.scores[i]);
    }
    RTTI_CHECK_EQ(a.points.size(), b.points.size());
    for (std::size_t i = 0; i < a.points.size(); ++i) {
        RTTI_CHECK_EQ(a.points[i].x, b.points[i].x);
        RTTI_CHECK_EQ(a.points[i].y, b.points[i].y);
    }
    RTTI_CHECK_EQ(a.table.size(), b.table.size());
    for (const auto& kv : a.table) {
        auto it = b.table.find(kv.first);
        RTTI_CHECK(it != b.table.end());
        if (it != b.table.end()) {
            RTTI_CHECK_EQ(kv.second, it->second);
        }
    }
    RTTI_CHECK_EQ(a.tags.size(), b.tags.size());
    for (const auto& t : a.tags) {
        RTTI_CHECK(b.tags.find(t) != b.tags.end());
    }
    RTTI_CHECK_EQ(a.center.x, b.center.x);
    RTTI_CHECK_EQ(a.center.y, b.center.y);
    RTTI_CHECK(a.color == b.color);
    RTTI_CHECK_EQ(a.active, b.active);
}

std::string string_of(rtti::value_io* v) {
    rtti::string_view sv;
    if (v == nullptr || !v->as_string(sv)) {
        return std::string("<no-string>");
    }
    return std::string(sv);
}

} // namespace

// --- Parsing: structure, attributes, children, text, entities --------------

// C-9 note (no runtime injection): like json's C-4, the arena's alloc can fail
// only when the underlying malloc returns nullptr, which is impractical to
// trigger from a unit test. The fix is a correctness review: make_view() (the
// get_key view-construction path) and create_node()/ensure_root() (write-side
// and parser node creation) now all guard their allocation result. get_key
// returns false, the write-side value_io builders (make_array/make_object/
// make_scalar) return nullptr, the void push_back/set_key no-op, and the
// parser paths report error_code::overflow. Every case behaves identically on
// the normal (allocation-succeeds) path; the tests below exercise those paths
// continuously.
RTTI_TEST_CASE(xml_parse_document) {
    const char* src =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<person id=\"42\" active=\"true\" xmlns=\"urn:base\" xmlns:p=\"urn:p\">\n"
        "  <!-- a comment -->\n"
        "  <name>Alice &amp; Bob &lt;dev&gt;</name>\n"
        "  <age>30</age>\n"
        "  <meta><![CDATA[raw <stuff> & more]]></meta>\n"
        "  <?target data-val?>\n"
        "  <empty/>\n"
        "  <scores>1</scores>\n"
        "  <scores>2</scores>\n"
        "  <scores>3</scores>\n"
        "  <addr><city>NYC</city><zip>10001</zip></addr>\n"
        "  <h>caf&#233; &#x1F600; &#65;</h>\n"
        "</person>\n";

    const auto rd = rtti::xml::parse(src);
    RTTI_CHECK(rd.ok());
    if (!rd.ok()) {
        return;
    }
    const rtti::xml::document& d = rd.value;

    RTTI_CHECK(d.type() == rtti::value_io::kind::object);

    // Attribute binding (id).
    rtti::value_io* idv = nullptr;
    RTTI_CHECK(d.get_key("id", idv));
    RTTI_CHECK(idv != nullptr);
    std::int64_t iv = 0;
    RTTI_CHECK(idv->as_integer(iv));
    RTTI_CHECK_EQ(iv, std::int64_t(42));
    RTTI_CHECK_EQ(string_of(idv), std::string("42"));

    // Attribute binding (active, boolean).
    rtti::value_io* actv = nullptr;
    RTTI_CHECK(d.get_key("active", actv));
    bool ab = false;
    RTTI_CHECK(actv->as_bool(ab));
    RTTI_CHECK(ab);

    // Child element text (name), with entity decoding.
    rtti::value_io* namev = nullptr;
    RTTI_CHECK(d.get_key("name", namev));
    RTTI_CHECK_EQ(string_of(namev), std::string("Alice & Bob <dev>"));

    // Scalar child (age).
    rtti::value_io* agev = nullptr;
    RTTI_CHECK(d.get_key("age", agev));
    std::int64_t age = 0;
    RTTI_CHECK(agev->as_integer(age));
    RTTI_CHECK_EQ(age, std::int64_t(30));

    // CDATA content.
    rtti::value_io* metav = nullptr;
    RTTI_CHECK(d.get_key("meta", metav));
    RTTI_CHECK_EQ(string_of(metav), std::string("raw <stuff> & more"));

    // Repeated same-name children become an array.
    rtti::value_io* sc = nullptr;
    RTTI_CHECK(d.get_key("scores", sc));
    RTTI_CHECK(sc != nullptr);
    RTTI_CHECK(sc->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(sc->size(), std::size_t(3));
    std::int64_t s0 = 0;
    RTTI_CHECK(sc->at(0)->as_integer(s0));
    RTTI_CHECK_EQ(s0, std::int64_t(1));
    std::int64_t s2 = 0;
    RTTI_CHECK(sc->at(2)->as_integer(s2));
    RTTI_CHECK_EQ(s2, std::int64_t(3));

    // Nested object element.
    rtti::value_io* addr = nullptr;
    RTTI_CHECK(d.get_key("addr", addr));
    RTTI_CHECK(addr != nullptr);
    RTTI_CHECK(addr->type() == rtti::value_io::kind::object);
    rtti::value_io* city = nullptr;
    RTTI_CHECK(addr->get_key("city", city));
    RTTI_CHECK_EQ(string_of(city), std::string("NYC"));
    rtti::value_io* zip = nullptr;
    RTTI_CHECK(addr->get_key("zip", zip));
    std::int64_t z = 0;
    RTTI_CHECK(zip->as_integer(z));
    RTTI_CHECK_EQ(z, std::int64_t(10001));

    // Empty (self-closing) element is present.
    rtti::value_io* emptyv = nullptr;
    RTTI_CHECK(d.get_key("empty", emptyv));

    // Numeric character references.
    rtti::value_io* hv = nullptr;
    RTTI_CHECK(d.get_key("h", hv));
    RTTI_CHECK_EQ(string_of(hv), std::string("caf") +
                                     std::string("\xC3\xA9") +
                                     std::string(" ") + std::string("\xF0\x9F\x98\x80") +
                                     std::string(" ") + std::string("A"));

    // Namespace attributes preserved + in-scope lookup.
    RTTI_CHECK_EQ(d.ns_uri(nullptr, ""), std::string("urn:base"));
    RTTI_CHECK_EQ(d.ns_uri(nullptr, "p"), std::string("urn:p"));

    // Serialization keeps comments and PIs in place.
    const std::string out = rtti::xml::serialize(d);
    RTTI_CHECK(out.find("<!-- a comment -->") != std::string::npos);
    RTTI_CHECK(out.find("<?target data-val?>") != std::string::npos);
    RTTI_CHECK(out.find("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") !=
               std::string::npos);
    // Round-trip the serialized form and re-verify the core values.
    const auto rd2 = rtti::xml::parse(out);
    RTTI_CHECK(rd2.ok());
    if (rd2.ok()) {
        rtti::value_io* id2 = nullptr;
        RTTI_CHECK(rd2.value.get_key("id", id2));
        std::int64_t iv2 = 0;
        RTTI_CHECK(id2->as_integer(iv2));
        RTTI_CHECK_EQ(iv2, std::int64_t(42));
        rtti::value_io* sc2 = nullptr;
        RTTI_CHECK(rd2.value.get_key("scores", sc2));
        RTTI_CHECK_EQ(sc2->size(), std::size_t(3));
    }
}

// --- Attribute priority over same-named child element ----------------------

RTTI_TEST_CASE(xml_attr_priority) {
    const auto rd = rtti::xml::parse("<person id=\"1\"><id>99</id></person>");
    RTTI_CHECK(rd.ok());
    if (!rd.ok()) {
        return;
    }
    rtti::value_io* idv = nullptr;
    RTTI_CHECK(rd.value.get_key("id", idv));
    std::int64_t iv = 0;
    RTTI_CHECK(idv->as_integer(iv));
    RTTI_CHECK_EQ(iv, std::int64_t(1));
}

// --- Mixed content: text interleaved with child elements -------------------

RTTI_TEST_CASE(xml_mixed_content) {
    const auto rd = rtti::xml::parse("<p>Hello <b>world</b>!</p>");
    RTTI_CHECK(rd.ok());
    if (!rd.ok()) {
        return;
    }
    // Element text content concatenates text children only (DOM semantics).
    rtti::value_io* tv = nullptr;
    RTTI_CHECK(rd.value.get_key("value", tv));
    RTTI_CHECK_EQ(string_of(tv), std::string("Hello !"));
    // The child element is still addressable.
    rtti::value_io* bv = nullptr;
    RTTI_CHECK(rd.value.get_key("b", bv));
    RTTI_CHECK_EQ(string_of(bv), std::string("world"));
    // Serialize/parse keeps the mixed structure and text.
    const std::string out = rtti::xml::serialize(rd.value);
    const auto rd2 = rtti::xml::parse(out);
    RTTI_CHECK(rd2.ok());
    if (rd2.ok()) {
        rtti::value_io* tv2 = nullptr;
        RTTI_CHECK(rd2.value.get_key("value", tv2));
        RTTI_CHECK_EQ(string_of(tv2), std::string("Hello !"));
    }
}

// --- Error paths ------------------------------------------------------------

RTTI_TEST_CASE(xml_parse_errors) {
    {
        const auto r = rtti::xml::parse("<root><a></b></root>");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::mismatched_tag);
    }
    {
        const auto r = rtti::xml::parse("<a x=\"1\" x=\"2\"/>");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::duplicate_attribute);
    }
    {
        const auto r = rtti::xml::parse("<root><a>");
        RTTI_CHECK(!r.ok());
    }
    {
        const auto r = rtti::xml::parse("");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::invalid_root);
    }
    {
        const auto r = rtti::xml::parse("   \n\t  ");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::invalid_root);
    }
    {
        const auto r = rtti::xml::parse("<root>&nope;</root>");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::bad_entity);
    }
    {
        const auto r = rtti::xml::parse("<root><a></root>");
        RTTI_CHECK(!r.ok());
    }
}

// Numeric character references whose value exceeds the XML code-point range
// must be rejected even when the accumulated value would wrap a 32-bit
// unsigned (e.g. `&#x100000041;` wraps to 0x41 'A'); both hex and decimal
// forms report bad_entity instead of emitting a character (C-1).
RTTI_TEST_CASE(xml_entity_overflow_range) {
    const char* bad[] = {
        "<root>&#x100000041;</root>",
        "<root>&#4294967361;</root>",
    };
    for (const char* src : bad) {
        const auto r = rtti::xml::parse(src);
        RTTI_CHECK(!r.ok());
        if (!r.ok()) {
            RTTI_CHECK(r.err.code == rtti::error_code::bad_entity);
        }
    }
}

// The UTF-16 surrogate range (U+D800..U+DFFF) holds no XML code points, so
// numeric character references naming a surrogate must be rejected with
// bad_entity (xml.cpp valid_xml_char), for both hex and decimal forms.
RTTI_TEST_CASE(xml_entity_surrogate_range) {
    const char* bad[] = {
        "<root>&#xD800;</root>",
        "<root>&#xDBFF;</root>", // last high surrogate
        "<root>&#xDC00;</root>", // first low surrogate
        "<root>&#xDFFF;</root>",
        "<root>&#55296;</root>", // decimal U+D800
        "<root>&#57343;</root>", // decimal U+DFFF
    };
    for (const char* src : bad) {
        const auto r = rtti::xml::parse(src);
        RTTI_CHECK(!r.ok());
        if (!r.ok()) {
            RTTI_CHECK(r.err.code == rtti::error_code::bad_entity);
        }
    }
    // A valid BMP reference still decodes, so the rejection is range-specific.
    const auto good = rtti::xml::parse("<root>&#x41;</root>");
    RTTI_CHECK(good.ok());
}

// --- Nesting depth limit ----------------------------------------------------

RTTI_TEST_CASE(xml_parse_too_deep) {
    std::string deep;
    for (int i = 0; i < 600; ++i) {
        deep += "<a>";
    }
    for (int i = 0; i < 600; ++i) {
        deep += "</a>";
    }
    const auto r = rtti::xml::parse(deep);
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::too_deep);
}

// --- DTD internal entities + Billion-Laughs guard --------------------------

RTTI_TEST_CASE(xml_dtd_entities) {
    const char* src =
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE root [\n"
        "  <!ENTITY author \"Jane Doe\">\n"
        "  <!ENTITY copyright \"2026 (c) &author;\">\n"
        "]>\n"
        "<root><who>&author;</who><cpy>&copyright;</cpy></root>";
    const auto rd = rtti::xml::parse(src);
    RTTI_CHECK(rd.ok());
    if (!rd.ok()) {
        return;
    }
    rtti::value_io* who = nullptr;
    RTTI_CHECK(rd.value.get_key("who", who));
    RTTI_CHECK_EQ(string_of(who), std::string("Jane Doe"));
    rtti::value_io* cpy = nullptr;
    RTTI_CHECK(rd.value.get_key("cpy", cpy));
    RTTI_CHECK_EQ(string_of(cpy), std::string("2026 (c) Jane Doe"));
}

RTTI_TEST_CASE(xml_dtd_billion_laughs) {
    const char* src =
        "<!DOCTYPE root [\n"
        "  <!ENTITY a \"x\">\n"
        "  <!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\">\n"
        "  <!ENTITY c \"&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;\">\n"
        "  <!ENTITY d \"&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;\">\n"
        "  <!ENTITY e \"&d;&d;&d;&d;&d;&d;&d;&d;&d;&d;\">\n"
        "]>\n"
        "<root>&e;</root>";
    const auto r = rtti::xml::parse(src);
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::too_deep);
}

// --- Serialization round-trip ----------------------------------------------

RTTI_TEST_CASE(xml_serialize_roundtrip) {
    const char* src =
        "<root><id>7</id><score>3.5</score><name>O&#39;Brien</name>"
        "<scores>10</scores><scores>20</scores><scores>30</scores>"
        "<points><x>1</x><y>2</y></points><points><x>3</x><y>4</y></points>"
        "<table><a>1</a><b>2</b></table>"
        "<tags>x</tags><tags>y</tags>"
        "<center><x>5</x><y>6</y></center>"
        "<color>Green</color><active>true</active></root>";

    const auto rd = rtti::xml::parse(src);
    RTTI_CHECK(rd.ok());

    // Pretty serialization then re-parse keeps the structure.
    const std::string pretty = rtti::xml::serialize(rd.value);
    RTTI_CHECK(pretty.find('\n') != std::string::npos);
    const auto rp = rtti::xml::parse(pretty);
    RTTI_CHECK(rp.ok());
    if (rp.ok()) {
        rtti::value_io* idv = nullptr;
        RTTI_CHECK(rp.value.get_key("id", idv));
        std::int64_t iv = 0;
        RTTI_CHECK(idv->as_integer(iv));
        RTTI_CHECK_EQ(iv, std::int64_t(7));
        rtti::value_io* sc = nullptr;
        RTTI_CHECK(rp.value.get_key("scores", sc));
        RTTI_CHECK_EQ(sc->size(), std::size_t(3));
        rtti::value_io* nm = nullptr;
        RTTI_CHECK(rp.value.get_key("name", nm));
        RTTI_CHECK_EQ(string_of(nm), std::string("O'Brien"));
    }

    // Compact serialization contains no newlines.
    const rtti::xml::write_config compact_cfg{true, 0};
    const std::string compact = rtti::xml::serialize(rd.value, compact_cfg);
    RTTI_CHECK(compact.find('\n') == std::string::npos);
    const auto rc = rtti::xml::parse(compact);
    RTTI_CHECK(rc.ok());
}

// --- Reflection binding: attribute, child text, vector, nested, enum -------

RTTI_TEST_CASE(xml_reflect_hand_written) {
    const char* src =
        "<person id=\"7\" name=\"Zoe\"><score>3.5</score><active>true</active></person>";
    const auto r = rtti::xml::parse_as<HandInfo>(src);
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    RTTI_CHECK_EQ(r.value.id, 7);
    RTTI_CHECK_EQ(r.value.name, std::string("Zoe"));
    RTTI_CHECK(r.value.score == 3.5);
    RTTI_CHECK(r.value.active);
}

RTTI_TEST_CASE(xml_reflect_roundtrip) {
    Stats s;
    s.id = 7;
    s.score = 3.25;
    s.name = "Alice <&> \"Q\"";
    s.scores = {10, 20, 30};
    s.points = {{1, 2}, {3, 4}};
    s.table = {{"a", 1}, {"b", 2}};
    s.tags = {"x", "y"};
    s.center = {5, 6};
    s.color = XmlColor::Blue;
    s.active = true;

    const std::string out = rtti::xml::serialize(s);
    const auto r = rtti::xml::parse_as<Stats>(out);
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    check_stats_eq(r.value, s);

    // Compact output also round-trips.
    const rtti::xml::write_config c{true, 0};
    const std::string out2 = rtti::xml::serialize(s, c);
    const auto r2 = rtti::xml::parse_as<Stats>(out2);
    RTTI_CHECK(r2.ok());
    if (r2.ok()) {
        check_stats_eq(r2.value, s);
    }

    // No declaration also round-trips.
    const rtti::xml::write_config nd{false, 4};
    const std::string out3 = rtti::xml::serialize(s, nd);
    RTTI_CHECK(out3.find("<?xml") == std::string::npos);
    const auto r3 = rtti::xml::parse_as<Stats>(out3);
    RTTI_CHECK(r3.ok());
    if (r3.ok()) {
        check_stats_eq(r3.value, s);
    }
}

RTTI_TEST_CASE(xml_reflect_text_binding) {
    // The field named "value" binds to the element's own text.
    const auto r1 = rtti::xml::parse_as<PlainText>("<root>hello</root>");
    RTTI_CHECK(r1.ok());
    if (r1.ok()) {
        RTTI_CHECK_EQ(r1.value.value, std::string("hello"));
    }
    // Serialization writes it back as element text, so the round-trip holds.
    const std::string out = rtti::xml::serialize(PlainText{"hi"});
    RTTI_CHECK(out.find("<root>hi</root>") != std::string::npos);
    const auto r2 = rtti::xml::parse_as<PlainText>(out);
    RTTI_CHECK(r2.ok());
    if (r2.ok()) {
        RTTI_CHECK_EQ(r2.value.value, std::string("hi"));
    }

    // Same for the "_text" field.
    const auto r3 = rtti::xml::parse_as<TextNode>("<root>world</root>");
    RTTI_CHECK(r3.ok());
    if (r3.ok()) {
        RTTI_CHECK_EQ(r3.value._text, std::string("world"));
    }
    const std::string out2 = rtti::xml::serialize(TextNode{"bye"});
    RTTI_CHECK(out2.find("<root>bye</root>") != std::string::npos);
    const auto r4 = rtti::xml::parse_as<TextNode>(out2);
    RTTI_CHECK(r4.ok());
    if (r4.ok()) {
        RTTI_CHECK_EQ(r4.value._text, std::string("bye"));
    }
}

// --- Enum round-trip --------------------------------------------------------

RTTI_TEST_CASE(xml_enum_roundtrip) {
    const std::string out = rtti::xml::serialize(XmlColor::Green);
    RTTI_CHECK(out.find("Green") != std::string::npos);
    const auto r = rtti::xml::parse_as<XmlColor>(out);
    RTTI_CHECK(r.ok());
    if (r.ok()) {
        RTTI_CHECK(r.value == XmlColor::Green);
    }
    const auto r2 = rtti::xml::parse_as<XmlColor>("<root>Blue</root>");
    RTTI_CHECK(r2.ok());
    if (r2.ok()) {
        RTTI_CHECK(r2.value == XmlColor::Blue);
    }
}

// --- Streaming SAX: event order --------------------------------------------

RTTI_TEST_CASE(xml_sax_events) {
    XmlSaxRecorder rec;
    rtti::xml::parse_events ev;
    ev.element_start = [&](rtti::string_view n) { return rec.start(n); };
    ev.element_end = [&](rtti::string_view n) { return rec.end(n); };
    ev.attribute = [&](rtti::string_view n, rtti::string_view v) {
        return rec.attr(n, v);
    };
    ev.text = [&](rtti::string_view t) { return rec.text(t); };
    ev.comment = [&](rtti::string_view c) { return rec.comment(c); };
    ev.cdata = [&](rtti::string_view c) { return rec.cdata(c); };
    ev.pi = [&](rtti::string_view p) { return rec.pi(p); };

    const char* src =
        "<root a=\"1\"><item>x</item><item>y</item><!--c--><![CDATA[d]]></root>";
    const auto r = rtti::xml::parse_stream(src, ev);
    RTTI_CHECK(r.ok());

    const std::vector<std::string> want = {
        "start:root", "attr:a=1",   "start:item", "text:x",  "end:item",
        "start:item", "text:y",     "end:item",   "comment:c",
        "cdata:d",    "end:root"};
    RTTI_CHECK_EQ(rec.events.size(), want.size());
    for (std::size_t i = 0; i < want.size() && i < rec.events.size(); ++i) {
        std::string got = rec.events[i].op + ":" + rec.events[i].a;
        if (rec.events[i].op == "attr") {
            got += "=" + rec.events[i].b;
        }
        RTTI_CHECK_EQ(got, want[i]);
    }
}

// --- Streaming SAX: mixed content, PI and self-closing ----------------------

RTTI_TEST_CASE(xml_sax_mixed_and_misc) {
    XmlSaxRecorder rec;
    rtti::xml::parse_events ev;
    ev.element_start = [&](rtti::string_view n) { return rec.start(n); };
    ev.element_end = [&](rtti::string_view n) { return rec.end(n); };
    ev.text = [&](rtti::string_view t) { return rec.text(t); };
    ev.pi = [&](rtti::string_view p) { return rec.pi(p); };

    // "Hello " and "!" are separated by the <b> element, so they arrive as two
    // text events; the PI is delivered as "target content"; the self-closing
    // <c/> yields both start and end events.
    const char* src = "<p>Hello <b>world</b>!<?t data?><c/></p>";
    const auto r = rtti::xml::parse_stream(src, ev);
    RTTI_CHECK(r.ok());

    const std::vector<std::string> want = {
        "start:p", "text:Hello ", "start:b", "text:world",
        "end:b",   "text:!",     "pi:t data", "start:c", "end:c", "end:p"};
    RTTI_CHECK_EQ(rec.events.size(), want.size());
    for (std::size_t i = 0; i < want.size() && i < rec.events.size(); ++i) {
        std::string got = rec.events[i].op + ":" + rec.events[i].a;
        RTTI_CHECK_EQ(got, want[i]);
    }
}

// --- Streaming SAX: early cancel --------------------------------------------

RTTI_TEST_CASE(xml_sax_cancel) {
    XmlSaxRecorder rec;
    rec.cancel_on = true;
    rec.cancel_op = "end";
    rec.cancel_a = "a";
    rtti::xml::parse_events ev;
    ev.element_start = [&](rtti::string_view n) { return rec.start(n); };
    ev.element_end = [&](rtti::string_view n) { return rec.end(n); };
    ev.text = [&](rtti::string_view t) { return rec.text(t); };

    const auto r = rtti::xml::parse_stream("<root><a>1</a><b>2</b></root>", ev);
    RTTI_CHECK(r.ok());
    RTTI_CHECK(rec.canceled);
    RTTI_CHECK_EQ(rec.events.size(), std::size_t(4));
    RTTI_CHECK_EQ(rec.last(), std::string("end:a"));
}

// --- Streaming SAX: syntax error --------------------------------------------

RTTI_TEST_CASE(xml_sax_error) {
    XmlSaxRecorder rec;
    rtti::xml::parse_events ev;
    ev.element_start = [&](rtti::string_view n) { return rec.start(n); };
    ev.element_end = [&](rtti::string_view n) { return rec.end(n); };

    const auto r = rtti::xml::parse_stream("<root><a></b></root>", ev);
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::mismatched_tag);
}

// --- Serialization into a preallocated buffer -------------------------------

RTTI_TEST_CASE(xml_serialize_buffer) {
    const char* src =
        "<root><id>7</id><name>O&#39;Brien</name>"
        "<scores>1</scores><scores>2</scores></root>";
    const auto rd = rtti::xml::parse(src);
    RTTI_CHECK(rd.ok());
    if (!rd.ok()) {
        return;
    }

    // Default config: buffer result matches the string result exactly.
    const std::string ref = rtti::xml::serialize(rd.value);
    std::string buf;
    rtti::xml::serialize(rd.value, buf);
    RTTI_CHECK_EQ(buf, ref);

    // Compact config matches too.
    const rtti::xml::write_config c{true, 0};
    const std::string refc = rtti::xml::serialize(rd.value, c);
    std::string bufc;
    rtti::xml::serialize(rd.value, bufc, c);
    RTTI_CHECK_EQ(bufc, refc);

    // The overload overwrites (never appends): a dirty prefix is dropped so
    // the buffer always holds exactly this document's text, matching the
    // json/ini/toml serialize(doc, out) contract (C-3).
    std::string out = "PREFIX:";
    rtti::xml::serialize(rd.value, out);
    RTTI_CHECK_EQ(out, ref);
    RTTI_CHECK_EQ(out.size(), ref.size());

    // Repeated calls to the same buffer keep producing exactly the reference.
    rtti::xml::serialize(rd.value, out);
    RTTI_CHECK_EQ(out, ref);
}

// --- Scalar-root serialization ------------------------------------------------

RTTI_TEST_CASE(xml_scalar_root_serialize_real) {
    rtti::xml::document d;
    d.make_scalar(rtti::field_value::of(3.14));
    const std::string o = rtti::xml::serialize(d);
    RTTI_CHECK(o.find("3.14") != std::string::npos);
}

RTTI_TEST_CASE(xml_scalar_root_serialize_other_kinds) {
    rtti::xml::document di;
    di.make_scalar(rtti::field_value::of(static_cast<int64_t>(42)));
    RTTI_CHECK(rtti::xml::serialize(di).find("42") != std::string::npos);

    rtti::xml::document db;
    db.make_scalar(rtti::field_value::of(true));
    RTTI_CHECK(rtti::xml::serialize(db).find("true") != std::string::npos);

    rtti::xml::document ds;
    ds.make_scalar(rtti::field_value::of(rtti::string_view("a<b&c")));
    const std::string so = rtti::xml::serialize(ds);
    RTTI_CHECK(so.find("a&lt;b&amp;c") != std::string::npos);
}

// Non-finite reals read back as inf/-inf/nan through the XML adapter (the XML
// text keeps the deterministic tokens; no crash, no corrupt output).
RTTI_TEST_CASE(xml_nonfinite_readback) {
    const std::string xi = rtti::xml::serialize(std::numeric_limits<double>::infinity());
    RTTI_CHECK(xi.find("inf") != std::string::npos);
    const auto pi = rtti::xml::parse_as<double>(xi);
    RTTI_CHECK(pi.ok() && std::isinf(pi.value) && pi.value > 0);

    const auto pn = rtti::xml::parse_as<double>(
        rtti::xml::serialize(-std::numeric_limits<double>::infinity()));
    RTTI_CHECK(pn.ok() && std::isinf(pn.value) && pn.value < 0);

    const auto pa = rtti::xml::parse_as<double>(
        rtti::xml::serialize(std::numeric_limits<double>::quiet_NaN()));
    RTTI_CHECK(pa.ok() && std::isnan(pa.value));
}

// Exact depth-limit boundary: the default max_depth is 512, so a 512-deep
// element chain parses while the existing 600-deep case reports too_deep.
RTTI_TEST_CASE(xml_parse_depth_boundary) {
    std::string ok;
    for (int i = 0; i < 512; ++i) {
        ok += "<a>";
    }
    for (int i = 0; i < 512; ++i) {
        ok += "</a>";
    }
    const auto r = rtti::xml::parse(ok);
    RTTI_CHECK(r.ok());
}

// T-D #7: an element/attribute name whose first byte is >= 0x80 (a UTF-8
// name) is legal XML and must parse instead of failing on the first byte.
RTTI_TEST_CASE(xml_utf8_name_first_byte) {
    // "élan" encoded as UTF-8: C3 A9 6C 61 6E (high first byte).
    const char* elem = "<\xC3\xA9lan>x</\xC3\xA9lan>";
    const auto r1 = rtti::xml::parse(elem);
    RTTI_CHECK(r1.ok());

    const char* attr = "<root \xC3\xA9=\"1\"/>";
    const auto r2 = rtti::xml::parse(attr);
    RTTI_CHECK(r2.ok());
}

// T-D #8: a '[' or '>' inside a SYSTEM/PUBLIC quoted literal must not
// terminate the DOCTYPE scan early.
RTTI_TEST_CASE(xml_doctype_system_public_quoted) {
    const char* sys = "<!DOCTYPE root SYSTEM \"http://a>b\">\n<root/>";
    const auto r1 = rtti::xml::parse(sys);
    RTTI_CHECK(r1.ok());

    const char* pub = "<!DOCTYPE root PUBLIC \"-//a[b//EN\" \"z[q\">\n<root/>";
    const auto r2 = rtti::xml::parse(pub);
    RTTI_CHECK(r2.ok());
}

// H2: the per-element attribute duplicate check keeps a small stack array and
// promotes to a hash set past a threshold. Verify a wide element with distinct
// attributes still parses (no false duplicates), a duplicate anywhere in the
// wide set still reports duplicate_attribute (both DOM and SAX), and the
// parser reports the first duplicate rather than only the highest-index one.
RTTI_TEST_CASE(xml_wide_attribute_dup_check) {
    // 16 distinct attributes on one element parse fine (exercises the set
    // path beyond the stack threshold).
    {
        const auto r = rtti::xml::parse(
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" l=\"12\" m=\"13\" n=\"14\" "
            "o=\"15\" p=\"16\"/>");
        RTTI_CHECK(r.ok());
        if (!r.ok()) {
            return;
        }
        rtti::value_io* v = nullptr;
        RTTI_CHECK(r.value.get_key("p", v));
        if (v != nullptr) {
            RTTI_CHECK_EQ(string_of(v), std::string("16"));
        }
    }
    // Duplicate that sits past the stack threshold must be caught by the
    // hash-set path.
    {
        const auto r = rtti::xml::parse(
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" a=\"dup\"/>");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::duplicate_attribute);
    }
    // Duplicate at attribute index 0 reports duplicate_attribute.
    {
        const auto r = rtti::xml::parse("<root a=\"1\" a=\"2\" b=\"3\" c=\"4\"/>");
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::duplicate_attribute);
    }
    // SAX: wide distinct attributes parse; wide duplicate still errors and the
    // duplicate scan fires the same error code without an event explosion.
    {
        rtti::xml::parse_events ev;
        int attrs = 0;
        ev.attribute = [&](rtti::string_view, rtti::string_view) {
            ++attrs;
            return true;
        };
        const auto r = rtti::xml::parse_stream(
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" l=\"12\"/>", ev);
        RTTI_CHECK(r.ok());
        if (!r.ok()) {
            return;
        }
        RTTI_CHECK_EQ(attrs, 12);
        const auto r2 = rtti::xml::parse_stream(
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" a=\"dup\"/>", ev);
        RTTI_CHECK(!r2.ok());
        RTTI_CHECK(r2.err.code == rtti::error_code::duplicate_attribute);
    }
}

// H2 boundary: the stack threshold is 20 attributes. Exactly 20 distinct
// attributes on one element stay on the stack (full, no promotion), 24 distinct
// attributes ride the promoted hash-set path, and a duplicate reports
// duplicate_attribute with the historical error offset in both regimes (stack
// at the boundary, set past it). DOM and SAX behave identically.
RTTI_TEST_CASE(xml_attr_dup_stack_threshold_boundary) {
    // Exactly 20 distinct attributes: stack path at its full capacity.
    {
        const auto r = rtti::xml::parse(
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" l=\"12\" m=\"13\" n=\"14\" "
            "o=\"15\" p=\"16\" q=\"17\" r=\"18\" s=\"19\" t=\"20\"/>");
        RTTI_CHECK(r.ok());
        if (!r.ok()) {
            return;
        }
        rtti::value_io* v = nullptr;
        RTTI_CHECK(r.value.get_key("t", v));
        if (v != nullptr) {
            RTTI_CHECK_EQ(string_of(v), std::string("20"));
        }
    }
    // 24 distinct attributes: past the threshold, set path still parses.
    {
        const auto r = rtti::xml::parse(
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" l=\"12\" m=\"13\" n=\"14\" "
            "o=\"15\" p=\"16\" q=\"17\" r=\"18\" s=\"19\" t=\"20\" "
            "u=\"21\" v=\"22\" w=\"23\" x=\"24\"/>");
        RTTI_CHECK(r.ok());
    }
    // Duplicate at the boundary: 20th attribute repeats the 1st; the stack path
    // must report duplicate_attribute with the offset at the repeated name.
    {
        const std::string src =
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" l=\"12\" m=\"13\" n=\"14\" "
            "o=\"15\" p=\"16\" q=\"17\" r=\"18\" s=\"19\" a=\"dup\"/>";
        const auto r = rtti::xml::parse(src);
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::duplicate_attribute);
        RTTI_CHECK_EQ(r.err.offset, src.find(" a=\"dup\"") + 2);
    }
    // Duplicate past the threshold: set path reports the same code and offset.
    {
        const std::string src =
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" l=\"12\" m=\"13\" n=\"14\" "
            "o=\"15\" p=\"16\" q=\"17\" r=\"18\" s=\"19\" t=\"20\" "
            "u=\"21\" v=\"22\" w=\"23\" a=\"dup\"/>";
        const auto r = rtti::xml::parse(src);
        RTTI_CHECK(!r.ok());
        RTTI_CHECK(r.err.code == rtti::error_code::duplicate_attribute);
        RTTI_CHECK_EQ(r.err.offset, src.find(" a=\"dup\"") + 2);
    }
    // SAX: 20 distinct attributes stream fine; a boundary duplicate errors with
    // the same code as the DOM path.
    {
        rtti::xml::parse_events ev;
        int attrs = 0;
        ev.attribute = [&](rtti::string_view, rtti::string_view) {
            ++attrs;
            return true;
        };
        const auto r = rtti::xml::parse_stream(
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" l=\"12\" m=\"13\" n=\"14\" "
            "o=\"15\" p=\"16\" q=\"17\" r=\"18\" s=\"19\" t=\"20\"/>", ev);
        RTTI_CHECK(r.ok());
        if (!r.ok()) {
            return;
        }
        RTTI_CHECK_EQ(attrs, 20);
        const auto r2 = rtti::xml::parse_stream(
            "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\" f=\"6\" g=\"7\" "
            "h=\"8\" i=\"9\" j=\"10\" k=\"11\" l=\"12\" m=\"13\" n=\"14\" "
            "o=\"15\" p=\"16\" q=\"17\" r=\"18\" s=\"19\" a=\"dup\"/>", ev);
        RTTI_CHECK(!r2.ok());
        RTTI_CHECK(r2.err.code == rtti::error_code::duplicate_attribute);
    }
}

// H3: read_name classifies name bytes through a static lookup table instead of
// per-byte std::isalpha/std::isalnum. The table must behave identically:
// letters/digits/'.'/'-'/'_'/':' are name characters (never as a leading
// digit/'.'/'-'), high bytes are accepted, and any other ASCII byte terminates
// the name (a stray byte then errors).
RTTI_TEST_CASE(xml_name_table_classification) {
    // Valid names spanning every allowed ASCII name character.
    {
        const auto r = rtti::xml::parse("<_a1.b-c:d z=\"1\" x__y=\"2\"/>");
        RTTI_CHECK(r.ok());
    }
    // High-byte (UTF-8) element and attribute names are accepted.
    {
        const auto r = rtti::xml::parse("<na\xC3\xAFve c\xC3\xA9"
                                        "f\xC3\xA9=\"v\"/>");
        RTTI_CHECK(r.ok());
    }
    // A name may not START with a digit, '.', or '-'.
    {
        RTTI_CHECK(!rtti::xml::parse("<1abc/>").ok());
        RTTI_CHECK(!rtti::xml::parse("<.abc/>").ok());
        RTTI_CHECK(!rtti::xml::parse("<-abc/>").ok());
    }
    // Digit-looking attribute names are rejected at the name-start check.
    {
        RTTI_CHECK(!rtti::xml::parse("<a 1=\"v\"/>").ok());
    }
    // An ASCII byte outside the name set terminates the name; the leftover
    // byte (here a space of all things) then fails the element grammar.
    {
        RTTI_CHECK(!rtti::xml::parse("<abc x/>").ok());
    }
}

} // namespace rtti_test_lib
