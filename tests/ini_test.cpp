// INI format adapter tests: parsing, error paths, serialization round-trips,
// and reflect-bound structure conversion through serialize<T>/parse_as<T>.
// Structs are defined at global scope; test functions live in rtti_test_lib.

#include "test_harness.hpp"
#include <rtti/ini.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

struct IniPoint {
    double x = 0.0;
    double y = 0.0;
    RTTI_DEFINE_TYPE_INTRUSIVE(IniPoint, x, y)
};

enum class IniColor { Red, Green, Blue };

RTTI_DEFINE_ENUM(IniColor, Red, Green, Blue)

struct IniChild {
    std::string name;
    int id = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(IniChild, name, id)
};

struct IniPerson {
    int id = 0;
    double score = 0.0;
    std::string name;
    bool active = false;
    std::vector<int> nums;
    std::vector<IniChild> friends;
    std::map<std::string, int> scores;
    std::set<std::string> tags;
    IniPoint point;
    IniColor color = IniColor::Red;
    RTTI_DEFINE_TYPE_INTRUSIVE(IniPerson, id, score, name, active, nums, friends,
                               scores, tags, point, color)
};

// Wraps a real field for non-finite round-trips. The INI adapter binds scalar
// roots as key/value lines, so a bare real cannot be round-tripped on its own;
// a reflected member (like every format test struct) round-trips losslessly.
struct IniRealBox {
    double d = 0.0;
    RTTI_DEFINE_TYPE_INTRUSIVE(IniRealBox, d)
};

struct IniStrict {
    int a = 0;
    int b = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(IniStrict, a, b)
};

struct IniDefault {
    int a = 1;
    int b = 2;
    RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(IniDefault, a, b)
};

struct IniColorWrap {
    IniColor color = IniColor::Red;
    RTTI_DEFINE_TYPE_INTRUSIVE(IniColorWrap, color)
};

namespace rtti_test_lib {

inline bool scalar_str(rtti::value_io* n, std::string& out) {
    rtti::string_view v;
    if (n != nullptr && n->as_string(v)) {
        out.assign(v.data(), v.size());
        return true;
    }
    return false;
}

inline bool scalar_int(rtti::value_io* n, std::int64_t& out) {
    return n != nullptr && n->as_integer(out);
}

RTTI_TEST_CASE(ini_parse_global_keys_and_sections) {
    const char* src =
        "title = Hello\n"
        "width = 640\n"
        "\n"
        "[server]\n"
        "host = example.com\n"
        "port = 8080\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* title = nullptr;
    RTTI_CHECK(d.value.get_key("title", title));
    std::string s;
    RTTI_CHECK(scalar_str(title, s));
    RTTI_CHECK_EQ(s, std::string("Hello"));
    rtti::value_io* width = nullptr;
    RTTI_CHECK(d.value.get_key("width", width));
    std::int64_t w = 0;
    RTTI_CHECK(scalar_int(width, w));
    RTTI_CHECK_EQ(w, std::int64_t(640));
    rtti::value_io* server = nullptr;
    RTTI_CHECK(d.value.get_key("server", server));
    RTTI_CHECK(server != nullptr);
    RTTI_CHECK(server->type() == rtti::value_io::kind::object);
    rtti::value_io* host = nullptr;
    RTTI_CHECK(server->get_key("host", host));
    RTTI_CHECK(scalar_str(host, s));
    RTTI_CHECK_EQ(s, std::string("example.com"));
    rtti::value_io* port = nullptr;
    RTTI_CHECK(server->get_key("port", port));
    RTTI_CHECK(scalar_int(port, w));
    RTTI_CHECK_EQ(w, std::int64_t(8080));
}

RTTI_TEST_CASE(ini_parse_nested_sections) {
    const char* src =
        "[a]\n"
        "x = 1\n"
        "[a.b]\n"
        "y = 2\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* a = nullptr;
    RTTI_CHECK(d.value.get_key("a", a));
    std::int64_t x = 0;
    rtti::value_io* xv = nullptr;
    RTTI_CHECK(a->get_key("x", xv));
    RTTI_CHECK(scalar_int(xv, x));
    RTTI_CHECK_EQ(x, std::int64_t(1));
    rtti::value_io* b = nullptr;
    RTTI_CHECK(a->get_key("b", b));
    RTTI_CHECK(b != nullptr);
    RTTI_CHECK(b->type() == rtti::value_io::kind::object);
    rtti::value_io* yv = nullptr;
    RTTI_CHECK(b->get_key("y", yv));
    RTTI_CHECK(scalar_int(yv, x));
    RTTI_CHECK_EQ(x, std::int64_t(2));
}

RTTI_TEST_CASE(ini_parse_comments) {
    const char* src =
        "; leading comment\n"
        "# hash comment\n"
        "key = value\n"
        "[s]\n"
        "; section comment\n"
        "k = v\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* key = nullptr;
    RTTI_CHECK(d.value.get_key("key", key));
    std::string s;
    RTTI_CHECK(scalar_str(key, s));
    RTTI_CHECK_EQ(s, std::string("value"));
    rtti::value_io* sec = nullptr;
    RTTI_CHECK(d.value.get_key("s", sec));
    rtti::value_io* k = nullptr;
    RTTI_CHECK(sec->get_key("k", k));
    RTTI_CHECK(scalar_str(k, s));
    RTTI_CHECK_EQ(s, std::string("v"));
}

RTTI_TEST_CASE(ini_parse_quoted_values_and_escapes) {
    const char* src =
        "a = \"hello world\"\n"
        "b = 'single'\n"
        "c = \"say \\\"hi\\\" and \\\\\"\n"
        "d = plain\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    std::string s;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d.value.get_key("a", n));
    RTTI_CHECK(scalar_str(n, s));
    RTTI_CHECK_EQ(s, std::string("hello world"));
    RTTI_CHECK(d.value.get_key("b", n));
    RTTI_CHECK(scalar_str(n, s));
    RTTI_CHECK_EQ(s, std::string("single"));
    RTTI_CHECK(d.value.get_key("c", n));
    RTTI_CHECK(scalar_str(n, s));
    RTTI_CHECK_EQ(s, std::string("say \"hi\" and \\"));
    RTTI_CHECK(d.value.get_key("d", n));
    RTTI_CHECK(scalar_str(n, s));
    RTTI_CHECK_EQ(s, std::string("plain"));
}

RTTI_TEST_CASE(ini_parse_separators_eq_and_colon) {
    const char* src =
        "a = 1\n"
        "b: 2\n"
        "c : 3\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    std::int64_t v = 0;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d.value.get_key("a", n));
    RTTI_CHECK(scalar_int(n, v));
    RTTI_CHECK_EQ(v, std::int64_t(1));
    RTTI_CHECK(d.value.get_key("b", n));
    RTTI_CHECK(scalar_int(n, v));
    RTTI_CHECK_EQ(v, std::int64_t(2));
    RTTI_CHECK(d.value.get_key("c", n));
    RTTI_CHECK(scalar_int(n, v));
    RTTI_CHECK_EQ(v, std::int64_t(3));
}

RTTI_TEST_CASE(ini_parse_duplicate_key_later_wins) {
    const char* src =
        "x = 1\n"
        "x = 2\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* x = nullptr;
    RTTI_CHECK(d.value.get_key("x", x));
    // The slot is an array preserving every occurrence...
    RTTI_CHECK(x->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(x->size(), std::size_t(2));
    // ...while scalar reads see the LAST value (later-wins).
    std::int64_t v = 0;
    RTTI_CHECK(scalar_int(x, v));
    RTTI_CHECK_EQ(v, std::int64_t(2));
}

RTTI_TEST_CASE(ini_parse_duplicate_sections_array) {
    const char* src =
        "[s]\n"
        "a = 1\n"
        "[s]\n"
        "b = 2\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* s = nullptr;
    RTTI_CHECK(d.value.get_key("s", s));
    RTTI_CHECK(s->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(s->size(), std::size_t(2));
    std::int64_t v = 0;
    rtti::value_io* e0 = s->at(std::size_t(0));
    RTTI_CHECK(e0 != nullptr);
    rtti::value_io* av = nullptr;
    RTTI_CHECK(e0->get_key("a", av));
    RTTI_CHECK(scalar_int(av, v));
    RTTI_CHECK_EQ(v, std::int64_t(1));
    rtti::value_io* e1 = s->at(std::size_t(1));
    RTTI_CHECK(e1 != nullptr);
    rtti::value_io* bv = nullptr;
    RTTI_CHECK(e1->get_key("b", bv));
    RTTI_CHECK(scalar_int(bv, v));
    RTTI_CHECK_EQ(v, std::int64_t(2));
}

RTTI_TEST_CASE(ini_parse_blank_lines_and_crlf) {
    const char* src =
        "\n"
        "   \r\n"
        "\t\r\n"
        "   spaced   =   val   \r\n"
        "[sec]\r\n"
        "k = v\r\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    std::string s;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d.value.get_key("spaced", n));
    RTTI_CHECK(scalar_str(n, s));
    RTTI_CHECK_EQ(s, std::string("val"));
    rtti::value_io* sec = nullptr;
    RTTI_CHECK(d.value.get_key("sec", sec));
    RTTI_CHECK(sec->get_key("k", n));
    RTTI_CHECK(scalar_str(n, s));
    RTTI_CHECK_EQ(s, std::string("v"));
}

RTTI_TEST_CASE(ini_parse_error_unterminated_section) {
    auto d = rtti::ini::parse("[abc\n");
    RTTI_CHECK(!d.ok());
    RTTI_CHECK(d.err.code == rtti::error_code::ini_unterminated_section);
}

RTTI_TEST_CASE(ini_parse_error_bad_line) {
    auto d = rtti::ini::parse("justtext\n");
    RTTI_CHECK(!d.ok());
    RTTI_CHECK(d.err.code == rtti::error_code::ini_bad_line);
}

RTTI_TEST_CASE(ini_parse_error_missing_value) {
    auto d1 = rtti::ini::parse("key =\n");
    RTTI_CHECK(!d1.ok());
    RTTI_CHECK(d1.err.code == rtti::error_code::ini_missing_value);
    auto d2 = rtti::ini::parse("key =   \n");
    RTTI_CHECK(!d2.ok());
    RTTI_CHECK(d2.err.code == rtti::error_code::ini_missing_value);
}

RTTI_TEST_CASE(ini_parse_error_empty_input) {
    auto d = rtti::ini::parse("");
    RTTI_CHECK(!d.ok());
    RTTI_CHECK(d.err.code == rtti::error_code::ini_bad_line);
}

RTTI_TEST_CASE(ini_serialize_roundtrip_idempotent) {
    const char* src =
        "title = Hello\n"
        "width = 640\n"
        "[server]\n"
        "host = example.com\n"
        "port = 8080\n"
        "[a.b]\n"
        "flag = true\n"
        "pi = 3.5\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    const std::string text = rtti::ini::serialize(d.value);
    auto d2 = rtti::ini::parse(rtti::string_view(text));
    RTTI_CHECK(d2.ok());
    const std::string text2 = rtti::ini::serialize(d2.value);
    RTTI_CHECK_EQ(text2, text);
}

RTTI_TEST_CASE(ini_serialize_comment_switch) {
    const char* src =
        "; hello\n"
        "a = 1\n"
        "[s]\n"
        "# inside\n"
        "k = v\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    const std::string with_c = rtti::ini::serialize(d.value, {true});
    RTTI_CHECK(with_c.find("; hello") != std::string::npos);
    RTTI_CHECK(with_c.find("# inside") != std::string::npos);
    const std::string without_c = rtti::ini::serialize(d.value, {false});
    RTTI_CHECK(without_c.find("; hello") == std::string::npos);
    RTTI_CHECK(without_c.find("# inside") == std::string::npos);
}

RTTI_TEST_CASE(ini_serialize_quoted_values_roundtrip) {
    const char* src =
        "a = \"has space\"\n"
        "b = \"starts with quote\\\" inside\"\n"
        "c = 42\n";
    auto d = rtti::ini::parse(src);
    RTTI_CHECK(d.ok());
    const std::string text = rtti::ini::serialize(d.value);
    auto d2 = rtti::ini::parse(rtti::string_view(text));
    RTTI_CHECK(d2.ok());
    std::string s;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d2.value.get_key("a", n));
    RTTI_CHECK(scalar_str(n, s));
    RTTI_CHECK_EQ(s, std::string("has space"));
    RTTI_CHECK(d2.value.get_key("b", n));
    RTTI_CHECK(scalar_str(n, s));
    RTTI_CHECK_EQ(s, std::string("starts with quote\" inside"));
    std::int64_t v = 0;
    RTTI_CHECK(d2.value.get_key("c", n));
    RTTI_CHECK(scalar_int(n, v));
    RTTI_CHECK_EQ(v, std::int64_t(42));
}

RTTI_TEST_CASE(ini_reflect_roundtrip_struct) {
    IniPerson p;
    p.id = 7;
    p.score = 12.5;
    p.name = "Alice";
    p.active = true;
    p.nums = {1, 2, 3};
    IniChild c1;
    c1.name = "Bob";
    c1.id = 10;
    IniChild c2;
    c2.name = "Carol";
    c2.id = 20;
    p.friends = {c1, c2};
    p.scores["z"] = 26;
    p.scores["a"] = 1;
    p.tags = {"x", "y", "z"};
    p.point.x = 1.5;
    p.point.y = -2.25;
    p.color = IniColor::Blue;

    const std::string text = rtti::ini::serialize(p);
    RTTI_CHECK(!text.empty());
    auto r = rtti::ini::parse_as<IniPerson>(rtti::string_view(text));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.id, 7);
    RTTI_CHECK_EQ(r.value.score, 12.5);
    RTTI_CHECK_EQ(r.value.name, std::string("Alice"));
    RTTI_CHECK(r.value.active);
    RTTI_CHECK_EQ(r.value.nums.size(), std::size_t(3));
    RTTI_CHECK_EQ(r.value.nums[0], 1);
    RTTI_CHECK_EQ(r.value.nums[2], 3);
    RTTI_CHECK_EQ(r.value.friends.size(), std::size_t(2));
    RTTI_CHECK_EQ(r.value.friends[0].name, std::string("Bob"));
    RTTI_CHECK_EQ(r.value.friends[0].id, 10);
    RTTI_CHECK_EQ(r.value.friends[1].name, std::string("Carol"));
    RTTI_CHECK_EQ(r.value.friends[1].id, 20);
    RTTI_CHECK_EQ(r.value.scores.size(), std::size_t(2));
    RTTI_CHECK_EQ(r.value.scores["a"], 1);
    RTTI_CHECK_EQ(r.value.scores["z"], 26);
    RTTI_CHECK_EQ(r.value.tags.size(), std::size_t(3));
    RTTI_CHECK(r.value.tags.find("x") != r.value.tags.end());
    RTTI_CHECK(r.value.tags.find("y") != r.value.tags.end());
    RTTI_CHECK(r.value.tags.find("z") != r.value.tags.end());
    RTTI_CHECK_EQ(r.value.point.x, 1.5);
    RTTI_CHECK_EQ(r.value.point.y, -2.25);
    RTTI_CHECK(r.value.color == IniColor::Blue);
}

RTTI_TEST_CASE(ini_reflect_repeated_sections_for_vector) {
    IniPerson p;
    p.id = 1;
    p.nums = {1, 2};
    p.tags = {"x", "y"};
    IniChild c1;
    c1.name = "Bob";
    c1.id = 10;
    IniChild c2;
    c2.name = "Carol";
    c2.id = 20;
    p.friends = {c1, c2};

    const std::string text = rtti::ini::serialize(p);
    std::size_t count = 0;
    std::size_t at = 0;
    while ((at = text.find("[friends]", at)) != std::string::npos) {
        ++count;
        ++at;
    }
    RTTI_CHECK_EQ(count, std::size_t(2));

    auto r = rtti::ini::parse_as<IniPerson>(rtti::string_view(text));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.friends.size(), std::size_t(2));
}

RTTI_TEST_CASE(ini_reflect_missing_key_strict) {
    auto r = rtti::ini::parse_as<IniStrict>(rtti::string_view("a = 5\n"));
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::missing_key);
}

RTTI_TEST_CASE(ini_reflect_with_default_keeps_existing) {
    // parse_as constructs a fresh object: the missing member keeps its default.
    auto r = rtti::ini::parse_as<IniDefault>(rtti::string_view("a = 10\n"));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.a, 10);
    RTTI_CHECK_EQ(r.value.b, 2);
    // Direct from_value on a preset object preserves the current value 77.
    IniDefault obj;
    obj.b = 77;
    auto d = rtti::ini::parse(rtti::string_view("a = 10\n"));
    RTTI_CHECK(d.ok());
    RTTI_CHECK_EQ(rtti::from_value(&d.value, obj), rtti::error_code::ok);
    RTTI_CHECK_EQ(obj.a, 10);
    RTTI_CHECK_EQ(obj.b, 77);
}

RTTI_TEST_CASE(ini_reflect_enum_roundtrip) {
    IniColorWrap w;
    w.color = IniColor::Green;
    const std::string text = rtti::ini::serialize(w);
    RTTI_CHECK(text.find("color = Green") != std::string::npos);
    auto r = rtti::ini::parse_as<IniColorWrap>(rtti::string_view(text));
    RTTI_CHECK(r.ok());
    RTTI_CHECK(r.value.color == IniColor::Green);
}

RTTI_TEST_CASE(ini_reflect_map_set_nested_roundtrip) {
    IniPerson p;
    p.id = 2;
    p.nums = {5, 6};
    IniChild fc;
    fc.name = "Zed";
    fc.id = 3;
    IniChild fd;
    fd.name = "Ann";
    fd.id = 4;
    p.friends = {fc, fd};
    p.scores["k"] = 9;
    p.tags = {"t", "u"};
    p.point.x = 0.25;
    p.point.y = 0.5;
    const std::string text = rtti::ini::serialize(p);
    auto r = rtti::ini::parse_as<IniPerson>(rtti::string_view(text));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value.scores.size(), std::size_t(1));
    RTTI_CHECK_EQ(r.value.scores["k"], 9);
    RTTI_CHECK_EQ(r.value.tags.size(), std::size_t(2));
    RTTI_CHECK_EQ(r.value.point.x, 0.25);
    RTTI_CHECK_EQ(r.value.point.y, 0.5);
    RTTI_CHECK_EQ(r.value.nums.size(), std::size_t(2));
    RTTI_CHECK_EQ(r.value.friends.size(), std::size_t(2));
}

// --- SAX (parse_stream) tests -------------------------------------------------
//
// Global-section SAX semantics (documented contract):
//   * "S:" is section_start(name), "E:" is section_end(name), "K:" is
//     key_value(name, value).
//   * Section-less keys are grouped under an implicit global section reported
//     as section_start("") before the first global key.
//   * Every started section (including the implicit global one) fires a
//     matching section_end when a later section header appears or at EOF.
//   * A dotted header like "[a.b]" is reported as a single "a.b" section.
//   * Comments never fire events.

inline std::vector<std::string> ini_sax_run(const char* src) {
    std::vector<std::string> ev;
    rtti::ini::parse_events pe;
    pe.section_start = [&](rtti::string_view n) {
        ev.push_back("S:" + std::string(n));
        return true;
    };
    pe.section_end = [&](rtti::string_view n) {
        ev.push_back("E:" + std::string(n));
        return true;
    };
    pe.key_value = [&](rtti::string_view k, rtti::string_view v) {
        ev.push_back("K:" + std::string(k) + "=" + std::string(v));
        return true;
    };
    (void)rtti::ini::parse_stream(rtti::string_view(src), pe);
    return ev;
}

RTTI_TEST_CASE(ini_sax_stream_event_order) {
    // Each section header closes the previous one before firing section_start.
    const std::vector<std::string> got =
        ini_sax_run("[a]\nx=1\n[global]\ny=2\n");
    const std::vector<std::string> want = {"S:a", "K:x=1", "E:a",
                                           "S:global", "K:y=2", "E:global"};
    RTTI_CHECK(got == want);
}

RTTI_TEST_CASE(ini_sax_stream_global_parenthesis_keys) {
    // Global keys before the first section are reported under the implicit
    // global section section_start("").
    const std::vector<std::string> got = ini_sax_run("x=1\n[a]\ny=2\n");
    const std::vector<std::string> want = {"S:", "K:x=1", "E:", "S:a",
                                           "K:y=2", "E:a"};
    RTTI_CHECK(got == want);
}

RTTI_TEST_CASE(ini_sax_stream_quoted_value_decoded) {
    std::string k, v;
    rtti::ini::parse_events pe;
    pe.key_value = [&](rtti::string_view a, rtti::string_view b) {
        k.assign(a.data(), a.size());
        v.assign(b.data(), b.size());
        return true;
    };
    auto r = rtti::ini::parse_stream(rtti::string_view("k = \"has space\"\n"), pe);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(k, std::string("k"));
    RTTI_CHECK_EQ(v, std::string("has space"));
}

RTTI_TEST_CASE(ini_sax_stream_comments_no_events) {
    const std::vector<std::string> got =
        ini_sax_run("; comment\n# another\n[sec]\n; inside\nk = v\n");
    const std::vector<std::string> want = {"S:sec", "K:k=v", "E:sec"};
    RTTI_CHECK(got == want);
}

RTTI_TEST_CASE(ini_sax_stream_cancel_early_ok) {
    // A callback returning false cancels the parse; parse_stream still
    // reports ok and no further events fire.
    int kv_count = 0;
    rtti::ini::parse_events pe;
    pe.section_start = [&](rtti::string_view) { return true; };
    pe.key_value = [&](rtti::string_view, rtti::string_view) {
        ++kv_count;
        return kv_count < 2;
    };
    pe.section_end = [&](rtti::string_view) {
        ++kv_count;  // must NOT fire after cancellation
        return true;
    };
    auto r = rtti::ini::parse_stream(rtti::string_view("a=1\nb=2\nc=3\n"), pe);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(kv_count, 2);
}

RTTI_TEST_CASE(ini_sax_stream_error_unterminated_section) {
    auto r = rtti::ini::parse_stream(rtti::string_view("[abc\n"),
                                     rtti::ini::parse_events{});
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::ini_unterminated_section);
}

RTTI_TEST_CASE(ini_sax_stream_error_bad_line_and_missing_value) {
    auto r = rtti::ini::parse_stream(rtti::string_view("justtext\n"),
                                     rtti::ini::parse_events{});
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::ini_bad_line);
    auto r2 = rtti::ini::parse_stream(rtti::string_view("k =\n"),
                                      rtti::ini::parse_events{});
    RTTI_CHECK(!r2.ok());
    RTTI_CHECK(r2.err.code == rtti::error_code::ini_missing_value);
}

// --- serialize into a preallocated buffer -------------------------------------
RTTI_TEST_CASE(ini_serialize_into_buffer) {
    auto d = rtti::ini::parse("a = 1\n[b]\nc = 2\n");
    RTTI_CHECK(d.ok());
    std::string buf = "this old content is longer than the serialized output";
    rtti::ini::serialize(d.value, buf);
    RTTI_CHECK_EQ(buf, rtti::ini::serialize(d.value));
    // Repeated serialize() on the same buffer overwrites: same content again.
    const std::string first = buf;
    buf.clear();
    buf.shrink_to_fit();
    rtti::ini::serialize(d.value, buf);
    RTTI_CHECK_EQ(buf, first);
}

RTTI_TEST_CASE(ini_serialize_into_buffer_comments_cfg) {
    auto d = rtti::ini::parse("; hello\na = 1\n[s]\n# inside\nk = v\n");
    RTTI_CHECK(d.ok());
    std::string buf;
    rtti::ini::serialize(d.value, buf, rtti::ini::write_config{true});
    RTTI_CHECK_EQ(buf, rtti::ini::serialize(d.value, rtti::ini::write_config{true}));
    RTTI_CHECK(buf.find("; hello") != std::string::npos);
    RTTI_CHECK(buf.find("# inside") != std::string::npos);
}

// --- Scalar-root serialization ------------------------------------------------

RTTI_TEST_CASE(ini_scalar_root_serialize_real) {
    rtti::ini::document d;
    d.make_scalar(rtti::field_value::of(3.14));
    const std::string o = rtti::ini::serialize(d);
    RTTI_CHECK(o.find("3.14") != std::string::npos);
}

RTTI_TEST_CASE(ini_scalar_root_serialize_other_kinds) {
    rtti::ini::document di;
    di.make_scalar(rtti::field_value::of(static_cast<int64_t>(42)));
    RTTI_CHECK(rtti::ini::serialize(di).find("42") != std::string::npos);

    rtti::ini::document db;
    db.make_scalar(rtti::field_value::of(true));
    RTTI_CHECK(rtti::ini::serialize(db).find("true") != std::string::npos);

    rtti::ini::document ds;
    ds.make_scalar(rtti::field_value::of(rtti::string_view("hello world")));
    RTTI_CHECK(rtti::ini::serialize(ds).find("hello world") != std::string::npos);
}

// --- Deep dotted-section-name guard (R1-1) ------------------------------------
// A dotted section header with an unbounded component count used to build an
// arbitrarily deep tree that serialize() then recursed into (stack overflow).
// Parse now caps the section-name component depth; over the limit it returns
// error_code::too_deep instead of building a tree, so the parse -> serialize
// workflow errors out cleanly and never crashes.
RTTI_TEST_CASE(ini_deep_dotted_section_too_deep) {
    std::string name;
    for (int i = 0; i < 600; ++i) {
        if (i != 0) {
            name.push_back('.');
        }
        name += "a";
    }
    const std::string src = "[" + name + "]\nk = 1\n";
    auto r = rtti::ini::parse(rtti::string_view(src));
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ(r.err.code, rtti::error_code::too_deep);

    // A reasonable depth still parses and serializes without error.
    std::string ok_name;
    for (int i = 0; i < 200; ++i) {
        if (i != 0) {
            ok_name.push_back('.');
        }
        ok_name += "a";
    }
    const std::string ok_src = "[" + ok_name + "]\nk = 2\n";
    auto ok = rtti::ini::parse(rtti::string_view(ok_src));
    RTTI_CHECK(ok.ok());
    const std::string out = rtti::ini::serialize(ok.value);
    RTTI_CHECK(out.find("k = 2") != std::string::npos);
}

// Non-finite reals read back as inf/-inf/nan through the INI adapter (the INI
// text keeps the deterministic tokens; no crash, no corrupt output).
RTTI_TEST_CASE(ini_nonfinite_readback) {
    IniRealBox p;
    p.d = std::numeric_limits<double>::infinity();
    const std::string x = rtti::ini::serialize(p);
    RTTI_CHECK(x.find("inf") != std::string::npos);
    const auto pi = rtti::ini::parse_as<IniRealBox>(rtti::string_view(x));
    RTTI_CHECK(pi.ok() && std::isinf(pi.value.d) && pi.value.d > 0);

    IniRealBox n;
    n.d = -std::numeric_limits<double>::infinity();
    const auto pn = rtti::ini::parse_as<IniRealBox>(
        rtti::string_view(rtti::ini::serialize(n)));
    RTTI_CHECK(pn.ok() && std::isinf(pn.value.d) && pn.value.d < 0);

    IniRealBox a;
    a.d = std::numeric_limits<double>::quiet_NaN();
    const auto pa = rtti::ini::parse_as<IniRealBox>(
        rtti::string_view(rtti::ini::serialize(a)));
    RTTI_CHECK(pa.ok() && std::isnan(pa.value.d));
}

} // namespace rtti_test_lib