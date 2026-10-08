// TOML format adapter tests: parsing, scalar classification, error paths,
// serialization round-trips, SAX streaming and reflect-bound structure
// conversion through serialize<T>/parse_as<T>. Structs are defined at global
// scope; test functions live in rtti_test_lib. All names are prefixed Toml* so
// they stay unique across translation units.

#include "test_harness.hpp"
#include <rtti/toml.hpp>

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

struct TomlPoint {
    int x = 0;
    int y = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(TomlPoint, x, y)
};

struct TomlServer {
    std::string host;
    int port = 0;
    bool secure = false;
    TomlPoint origin;
    RTTI_DEFINE_TYPE_INTRUSIVE(TomlServer, host, port, secure, origin)
};

struct TomlPerson {
    std::string name;
    int age = 0;
    double score = 0.0;
    bool active = false;
    std::vector<int> nums;
    std::vector<TomlPoint> points;
    std::map<std::string, int> scores;
    std::string note;
    RTTI_DEFINE_TYPE_INTRUSIVE(TomlPerson, name, age, score, active, nums,
                               points, scores, note)
};

struct TomlCamel {
    int fieldName = 7;
    RTTI_DEFINE_TYPE_INTRUSIVE(TomlCamel, fieldName)
};

struct TomlSnake {
    int first_name = 9;
    RTTI_DEFINE_TYPE_INTRUSIVE(TomlSnake, first_name)
};

// Registered enum: TOML has no native enum syntax, so a registered enum
// member travels as its label text through the reflect bridge (same path as
// XML/INI through the shared converter).
enum class TomlColor { Red, Green, Blue };
RTTI_DEFINE_ENUM(TomlColor, Red, Green, Blue)

struct TomlColorBox {
    TomlColor color = TomlColor::Red;
    RTTI_DEFINE_TYPE_INTRUSIVE(TomlColorBox, color)
};

namespace rtti_test_lib {

inline bool str_of(rtti::value_io* n, std::string& out) {
    rtti::string_view v;
    if (n != nullptr && n->as_string(v)) {
        out.assign(v.data(), v.size());
        return true;
    }
    return false;
}

inline bool int_of(rtti::value_io* n, std::int64_t& out) {
    return n != nullptr && n->as_integer(out);
}

inline bool real_of(rtti::value_io* n, double& out) {
    return n != nullptr && n->as_real(out);
}

RTTI_TEST_CASE(toml_parse_key_value_scalars) {
    const char* src =
        "title = \"Hello\"\n"
        "count = 42\n"
        "ratio = 3.5\n"
        "enabled = true\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* title = nullptr;
    RTTI_CHECK(d.value.get_key("title", title));
    std::string s;
    RTTI_CHECK(str_of(title, s));
    RTTI_CHECK_EQ(s, std::string("Hello"));
    rtti::value_io* count = nullptr;
    RTTI_CHECK(d.value.get_key("count", count));
    RTTI_CHECK(count != nullptr);
    RTTI_CHECK(count->type() == rtti::value_io::kind::integer);
    std::int64_t i = 0;
    RTTI_CHECK(int_of(count, i));
    RTTI_CHECK_EQ(i, std::int64_t(42));
    rtti::value_io* ratio = nullptr;
    RTTI_CHECK(d.value.get_key("ratio", ratio));
    RTTI_CHECK(ratio->type() == rtti::value_io::kind::real);
    double r = 0.0;
    RTTI_CHECK(real_of(ratio, r));
    RTTI_CHECK_EQ(r, 3.5);
    rtti::value_io* enabled = nullptr;
    RTTI_CHECK(d.value.get_key("enabled", enabled));
    RTTI_CHECK(enabled->type() == rtti::value_io::kind::boolean);
    bool b = false;
    RTTI_CHECK(enabled != nullptr && enabled->as_bool(b));
    RTTI_CHECK(b);
}

RTTI_TEST_CASE(toml_parse_dotted_key) {
    const char* src =
        "a.b.c = 1\n"
        "a.b.d = \"x\"\n"
        "a.z = 2\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* a = nullptr;
    RTTI_CHECK(d.value.get_key("a", a));
    rtti::value_io* b = nullptr;
    RTTI_CHECK(a->get_key("b", b));
    rtti::value_io* c = nullptr;
    RTTI_CHECK(b->get_key("c", c));
    std::int64_t i = 0;
    RTTI_CHECK(int_of(c, i));
    RTTI_CHECK_EQ(i, std::int64_t(1));
    rtti::value_io* z = nullptr;
    RTTI_CHECK(a->get_key("z", z));
    RTTI_CHECK(int_of(z, i));
    RTTI_CHECK_EQ(i, std::int64_t(2));
}

RTTI_TEST_CASE(toml_parse_quoted_keys) {
    const char* src =
        "\"quoted key\" = 1\n"
        "'literal key' = 2\n"
        "\"a\".\"b.c\" = 3\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* qk = nullptr;
    RTTI_CHECK(d.value.get_key("quoted key", qk));
    std::int64_t i = 0;
    RTTI_CHECK(int_of(qk, i));
    RTTI_CHECK_EQ(i, std::int64_t(1));
    rtti::value_io* lk = nullptr;
    RTTI_CHECK(d.value.get_key("literal key", lk));
    RTTI_CHECK(int_of(lk, i));
    RTTI_CHECK_EQ(i, std::int64_t(2));
    rtti::value_io* a = nullptr;
    RTTI_CHECK(d.value.get_key("a", a));
    rtti::value_io* bc = nullptr;
    RTTI_CHECK(a->get_key("b.c", bc));
    RTTI_CHECK(int_of(bc, i));
    RTTI_CHECK_EQ(i, std::int64_t(3));
}

RTTI_TEST_CASE(toml_parse_basic_table) {
    const char* src =
        "[server]\n"
        "host = \"example.com\"\n"
        "port = 8080\n"
        "[server.origin]\n"
        "x = 1\n"
        "[client]\n"
        "timeout = 30\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* server = nullptr;
    RTTI_CHECK(d.value.get_key("server", server));
    RTTI_CHECK(server->type() == rtti::value_io::kind::object);
    rtti::value_io* host = nullptr;
    RTTI_CHECK(server->get_key("host", host));
    std::string s;
    RTTI_CHECK(str_of(host, s));
    RTTI_CHECK_EQ(s, std::string("example.com"));
    rtti::value_io* port = nullptr;
    RTTI_CHECK(server->get_key("port", port));
    std::int64_t i = 0;
    RTTI_CHECK(int_of(port, i));
    RTTI_CHECK_EQ(i, std::int64_t(8080));
    rtti::value_io* origin = nullptr;
    RTTI_CHECK(server->get_key("origin", origin));
    rtti::value_io* x = nullptr;
    RTTI_CHECK(origin->get_key("x", x));
    RTTI_CHECK(int_of(x, i));
    RTTI_CHECK_EQ(i, std::int64_t(1));
    rtti::value_io* client = nullptr;
    RTTI_CHECK(d.value.get_key("client", client));
    rtti::value_io* to = nullptr;
    RTTI_CHECK(client->get_key("timeout", to));
    RTTI_CHECK(int_of(to, i));
    RTTI_CHECK_EQ(i, std::int64_t(30));
}

RTTI_TEST_CASE(toml_parse_array_table) {
    const char* src =
        "[[products]]\n"
        "name = \"A\"\n"
        "price = 1.0\n"
        "[[products]]\n"
        "name = \"B\"\n"
        "price = 2.5\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* products = nullptr;
    RTTI_CHECK(d.value.get_key("products", products));
    RTTI_CHECK(products->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(products->size(), std::size_t(2));
    rtti::value_io* p0 = products->at(0);
    rtti::value_io* p1 = products->at(1);
    RTTI_CHECK(p0 != nullptr && p1 != nullptr);
    rtti::value_io* n0 = nullptr;
    RTTI_CHECK(p0->get_key("name", n0));
    std::string s;
    RTTI_CHECK(str_of(n0, s));
    RTTI_CHECK_EQ(s, std::string("A"));
    rtti::value_io* n1 = nullptr;
    RTTI_CHECK(p1->get_key("name", n1));
    RTTI_CHECK(str_of(n1, s));
    RTTI_CHECK_EQ(s, std::string("B"));
}

RTTI_TEST_CASE(toml_parse_array_table_nested) {
    const char* src =
        "[[fruit]]\n"
        "name = \"apple\"\n"
        "[[fruit.variety]]\n"
        "name = \"red\"\n"
        "[[fruit.variety]]\n"
        "name = \"green\"\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* fruit = nullptr;
    RTTI_CHECK(d.value.get_key("fruit", fruit));
    RTTI_CHECK(fruit->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(fruit->size(), std::size_t(1));
    rtti::value_io* f0 = fruit->at(0);
    RTTI_CHECK(f0 != nullptr);
    rtti::value_io* variety = nullptr;
    RTTI_CHECK(f0->get_key("variety", variety));
    RTTI_CHECK(variety->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(variety->size(), std::size_t(2));
    std::string s;
    rtti::value_io* v1 = variety->at(1);
    RTTI_CHECK(v1 != nullptr);
    rtti::value_io* v1n = nullptr;
    RTTI_CHECK(v1->get_key("name", v1n));
    RTTI_CHECK(str_of(v1n, s));
    RTTI_CHECK_EQ(s, std::string("green"));
}

RTTI_TEST_CASE(toml_parse_strings) {
    const char* src =
        "basic = \"hello\"\n"
        "multi = \"\"\"first\nsecond\"\"\"\n"
        "lit = 'C:\\path\\to\\file'\n"
        "mlit = '''line1\nline2'''\n"
        "uni = \"\\u0041\"\n"
        "esc = \"a\\tb\\\"c\\\"\\\\d\"\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    std::string s;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d.value.get_key("basic", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("hello"));
    RTTI_CHECK(d.value.get_key("multi", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("first\nsecond"));
    RTTI_CHECK(d.value.get_key("lit", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("C:\\path\\to\\file"));
    RTTI_CHECK(d.value.get_key("mlit", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("line1\nline2"));
    RTTI_CHECK(d.value.get_key("uni", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("A"));
    RTTI_CHECK(d.value.get_key("esc", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("a\tb\"c\"\\d"));
}

RTTI_TEST_CASE(toml_parse_integers) {
    const char* src =
        "dec = 99\n"
        "neg = -17\n"
        "plus = +42\n"
        "hex = 0x1F\n"
        "oct = 0o17\n"
        "bin = 0b1010\n"
        "und = 1_000\n"
        "big = 9223372036854775807\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    std::int64_t i = 0;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d.value.get_key("dec", n) && int_of(n, i));
    RTTI_CHECK_EQ(i, std::int64_t(99));
    RTTI_CHECK(d.value.get_key("neg", n) && int_of(n, i));
    RTTI_CHECK_EQ(i, std::int64_t(-17));
    RTTI_CHECK(d.value.get_key("plus", n) && int_of(n, i));
    RTTI_CHECK_EQ(i, std::int64_t(42));
    RTTI_CHECK(d.value.get_key("hex", n) && int_of(n, i));
    RTTI_CHECK_EQ(i, std::int64_t(31));
    RTTI_CHECK(d.value.get_key("oct", n) && int_of(n, i));
    RTTI_CHECK_EQ(i, std::int64_t(15));
    RTTI_CHECK(d.value.get_key("bin", n) && int_of(n, i));
    RTTI_CHECK_EQ(i, std::int64_t(10));
    RTTI_CHECK(d.value.get_key("und", n) && int_of(n, i));
    RTTI_CHECK_EQ(i, std::int64_t(1000));
    RTTI_CHECK(d.value.get_key("big", n) && int_of(n, i));
    RTTI_CHECK_EQ(i, std::int64_t(9223372036854775807LL));
}

RTTI_TEST_CASE(toml_parse_floats) {
    const char* src =
        "pi = 3.14\n"
        "neg = -0.01\n"
        "sci = 5e+22\n"
        "sci2 = 1e6\n"
        "pinf = +inf\n"
        "ninf = -inf\n"
        "infv = inf\n"
        "nanv = nan\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    double r = 0.0;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d.value.get_key("pi", n) && real_of(n, r));
    RTTI_CHECK_EQ(r, 3.14);
    RTTI_CHECK(d.value.get_key("neg", n) && real_of(n, r));
    RTTI_CHECK_EQ(r, -0.01);
    RTTI_CHECK(d.value.get_key("sci", n) && real_of(n, r));
    RTTI_CHECK_EQ(r, 5e+22);
    RTTI_CHECK(d.value.get_key("sci2", n) && real_of(n, r));
    RTTI_CHECK_EQ(r, 1e6);
    RTTI_CHECK(d.value.get_key("pinf", n) && real_of(n, r));
    RTTI_CHECK(std::isinf(r));
    RTTI_CHECK(r > 0.0);
    RTTI_CHECK(d.value.get_key("ninf", n) && real_of(n, r));
    RTTI_CHECK(std::isinf(r));
    RTTI_CHECK(r < 0.0);
    RTTI_CHECK(d.value.get_key("infv", n) && real_of(n, r));
    RTTI_CHECK(std::isinf(r));
    RTTI_CHECK(d.value.get_key("nanv", n) && real_of(n, r));
    RTTI_CHECK(std::isnan(r));
}

RTTI_TEST_CASE(toml_parse_booleans) {
    const char* src = "yes = true\nno = false\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    bool b = false;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d.value.get_key("yes", n) && n->as_bool(b));
    RTTI_CHECK(b);
    RTTI_CHECK(d.value.get_key("no", n) && n->as_bool(b));
    RTTI_CHECK(!b);
}

RTTI_TEST_CASE(toml_parse_arrays) {
    const char* src =
        "mixed = [1, \"two\", true, 3.5]\n"
        "nested = [[1, 2], [3, 4]]\n"
        "ofobj = [{ a = 1 }, { a = 2 }]\n"
        "empty = []\n"
        "ml = [\n"
        "  1,\n"
        "  2,\n"
        "]\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    rtti::value_io* mixed = nullptr;
    RTTI_CHECK(d.value.get_key("mixed", mixed));
    RTTI_CHECK(mixed->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(mixed->size(), std::size_t(4));
    std::int64_t i = 0;
    RTTI_CHECK(mixed->at(0) != nullptr && mixed->at(0)->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(1));
    std::string s;
    RTTI_CHECK(str_of(mixed->at(1), s));
    RTTI_CHECK_EQ(s, std::string("two"));
    bool b = false;
    RTTI_CHECK(mixed->at(2) != nullptr && mixed->at(2)->as_bool(b));
    RTTI_CHECK(b);
    double r = 0.0;
    RTTI_CHECK(real_of(mixed->at(3), r));
    RTTI_CHECK_EQ(r, 3.5);

    rtti::value_io* nested = nullptr;
    RTTI_CHECK(d.value.get_key("nested", nested));
    RTTI_CHECK(nested->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(nested->size(), std::size_t(2));
    RTTI_CHECK(nested->at(1)->at(1) != nullptr &&
               nested->at(1)->at(1)->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(4));

    rtti::value_io* ofobj = nullptr;
    RTTI_CHECK(d.value.get_key("ofobj", ofobj));
    RTTI_CHECK_EQ(ofobj->size(), std::size_t(2));
    rtti::value_io* o0 = ofobj->at(0);
    rtti::value_io* a0 = nullptr;
    RTTI_CHECK(o0->get_key("a", a0) && a0->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(1));

    rtti::value_io* empty = nullptr;
    RTTI_CHECK(d.value.get_key("empty", empty));
    RTTI_CHECK(empty->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(empty->size(), std::size_t(0));

    rtti::value_io* ml = nullptr;
    RTTI_CHECK(d.value.get_key("ml", ml));
    RTTI_CHECK_EQ(ml->size(), std::size_t(2));
    RTTI_CHECK(ml->at(1) != nullptr && ml->at(1)->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(2));
}

RTTI_TEST_CASE(toml_parse_comments) {
    const char* src =
        "# leading comment\n"
        "key = 1  # trailing comment\n"
        "# another comment\n"
        "[sec] # section comment\n"
        "x = 2\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    std::int64_t i = 0;
    rtti::value_io* key = nullptr;
    RTTI_CHECK(d.value.get_key("key", key) && key->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(1));
    rtti::value_io* sec = nullptr;
    RTTI_CHECK(d.value.get_key("sec", sec));
    rtti::value_io* x = nullptr;
    RTTI_CHECK(sec->get_key("x", x) && x->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(2));
}

RTTI_TEST_CASE(toml_duplicate_key_later_wins) {
    const char* src = "a = 1\na = 2\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    std::int64_t i = 0;
    rtti::value_io* a = nullptr;
    RTTI_CHECK(d.value.get_key("a", a));
    RTTI_CHECK(a != nullptr && a->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(2));
    RTTI_CHECK_EQ(d.value.size(), std::size_t(1));
}

RTTI_TEST_CASE(toml_datetime_roundtrip) {
    const char* src =
        "date = 1979-05-27\n"
        "offset = 1979-05-27T07:32:00Z\n"
        "localdt = 1979-05-27T07:32:00\n"
        "local = 07:32:00\n"
        "frac = 1979-05-27T07:32:00.999\n"
        "zone = 1979-05-27T07:32:00-07:00\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    std::string s;
    rtti::value_io* n = nullptr;
    RTTI_CHECK(d.value.get_key("date", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("1979-05-27"));
    RTTI_CHECK(d.value.get_key("offset", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("1979-05-27T07:32:00Z"));
    RTTI_CHECK(d.value.get_key("zone", n) && str_of(n, s));
    RTTI_CHECK_EQ(s, std::string("1979-05-27T07:32:00-07:00"));

    const std::string s1 = rtti::toml::serialize(d.value);
    RTTI_CHECK(s1.find("1979-05-27T07:32:00Z") != std::string::npos);
    RTTI_CHECK(s1.find("\"1979-05-27T07:32:00Z\"") == std::string::npos);
    RTTI_CHECK(s1.find("1979-05-27T07:32:00-07:00") != std::string::npos);
    auto d2 = rtti::toml::parse(s1);
    RTTI_CHECK(d2.ok());
    RTTI_CHECK_EQ(rtti::toml::serialize(d2.value), s1);
}

RTTI_TEST_CASE(toml_serialize_document_roundtrip) {
    const char* src =
        "title = \"Hello\"\n"
        "count = 42\n"
        "nums = [1, 2, 3]\n"
        "[server]\n"
        "host = \"x\"\n"
        "port = 8080\n"
        "[[items]]\n"
        "id = 1\n"
        "[[items]]\n"
        "id = 2\n";
    auto d = rtti::toml::parse(src);
    RTTI_CHECK(d.ok());
    const std::string s1 = rtti::toml::serialize(d.value);
    auto d2 = rtti::toml::parse(s1);
    RTTI_CHECK(d2.ok());
    RTTI_CHECK_EQ(rtti::toml::serialize(d2.value), s1);
    std::int64_t i = 0;
    rtti::value_io* count = nullptr;
    RTTI_CHECK(d2.value.get_key("count", count));
    RTTI_CHECK(count != nullptr && count->as_integer(i));
    RTTI_CHECK_EQ(i, std::int64_t(42));
    rtti::value_io* items = nullptr;
    RTTI_CHECK(d2.value.get_key("items", items));
    RTTI_CHECK_EQ(items->size(), std::size_t(2));
}

RTTI_TEST_CASE(toml_reflect_struct_roundtrip) {
    TomlPerson p;
    p.name = "Alice";
    p.age = 30;
    p.score = 98.5;
    p.active = true;
    p.nums = {1, 2, 3};
    p.points = {{1, 2}, {3, 4}};
    p.scores = {{"a", 1}, {"b", 2}};
    p.note = "hi\nquote";
    const std::string s = rtti::toml::serialize(p);
    RTTI_CHECK(s.find("name = \"Alice\"") != std::string::npos);
    RTTI_CHECK(s.find("[[points]]") != std::string::npos);
    RTTI_CHECK(s.find("age = 30") != std::string::npos);
    auto back = rtti::toml::parse_as<TomlPerson>(s);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK_EQ(back.value.name, std::string("Alice"));
        RTTI_CHECK_EQ(back.value.age, 30);
        RTTI_CHECK_EQ(back.value.score, 98.5);
        RTTI_CHECK(back.value.active);
        RTTI_CHECK_EQ(back.value.nums.size(), std::size_t(3));
        RTTI_CHECK_EQ(back.value.nums[1], 2);
        RTTI_CHECK_EQ(back.value.points.size(), std::size_t(2));
        RTTI_CHECK_EQ(back.value.points[1].x, 3);
        RTTI_CHECK_EQ(back.value.points[1].y, 4);
        RTTI_CHECK_EQ(back.value.scores["a"], 1);
        RTTI_CHECK_EQ(back.value.scores["b"], 2);
        RTTI_CHECK_EQ(back.value.note, std::string("hi\nquote"));
    }
}

RTTI_TEST_CASE(toml_reflect_nested_table) {
    TomlServer srv;
    srv.host = "h";
    srv.port = 80;
    srv.secure = true;
    srv.origin.x = 5;
    srv.origin.y = 6;
    const std::string s = rtti::toml::serialize(srv);
    RTTI_CHECK(s.find("[origin]") != std::string::npos);
    auto back = rtti::toml::parse_as<TomlServer>(s);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK_EQ(back.value.host, std::string("h"));
        RTTI_CHECK_EQ(back.value.port, 80);
        RTTI_CHECK(back.value.secure);
        RTTI_CHECK_EQ(back.value.origin.x, 5);
        RTTI_CHECK_EQ(back.value.origin.y, 6);
    }
}

// A registered enum member round-trips as its label text through the TOML
// reflect bridge (TOML has no native enum syntax; the shared converter maps
// the enum to/from a string scalar).
RTTI_TEST_CASE(toml_enum_roundtrip) {
    TomlColorBox box;
    box.color = TomlColor::Blue;
    const std::string s = rtti::toml::serialize(box);
    RTTI_CHECK(s.find("color = \"Blue\"") != std::string::npos);
    auto back = rtti::toml::parse_as<TomlColorBox>(s);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK(back.value.color == TomlColor::Blue);
    }
    // An unknown label is rejected (conversion error), never crashes.
    auto bad = rtti::toml::parse_as<TomlColorBox>("color = \"Nope\"\n");
    RTTI_CHECK(!bad.ok());
}

RTTI_TEST_CASE(toml_named_snake_camel) {
    rtti::toml::write_config c;
    c.key_case = rtti::toml::key_case::snake;
    const std::string s = rtti::toml::serialize(TomlCamel{11}, c);
    RTTI_CHECK(s.find("field_name = 11") != std::string::npos);
    auto back = rtti::toml::parse_as<TomlCamel>(s, c);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK_EQ(back.value.fieldName, 11);
    }
    rtti::toml::write_config cc;
    cc.key_case = rtti::toml::key_case::camel;
    const std::string cs = rtti::toml::serialize(TomlSnake{4}, cc);
    RTTI_CHECK(cs.find("firstName = 4") != std::string::npos);
    auto back2 = rtti::toml::parse_as<TomlSnake>(cs, cc);
    RTTI_CHECK(back2.ok());
    if (back2.ok()) {
        RTTI_CHECK_EQ(back2.value.first_name, 4);
    }
}

RTTI_TEST_CASE(toml_sax_stream_events) {
    std::vector<std::string> ev;
    rtti::toml::parse_events pe;
    pe.table_start = [&](rtti::string_view n) {
        ev.push_back("S:" + std::string(n));
        return true;
    };
    pe.table_end = [&](rtti::string_view n) {
        ev.push_back("E:" + std::string(n));
        return true;
    };
    pe.array_table_start = [&](rtti::string_view n) {
        ev.push_back("A:" + std::string(n));
        return true;
    };
    pe.array_table_end = [&](rtti::string_view n) {
        ev.push_back("B:" + std::string(n));
        return true;
    };
    pe.key_value = [&](rtti::string_view k, rtti::string_view v) {
        ev.push_back("K:" + std::string(k) + "=" + std::string(v));
        return true;
    };
    pe.integer_value = [&](std::int64_t v) {
        ev.push_back("I:" + std::to_string(v));
        return true;
    };
    const char* src =
        "g = 1\n"
        "[a]\n"
        "x = 2\n"
        "[[b]]\n"
        "y = 3\n"
        "[[b]]\n"
        "y = 4\n";
    auto r = rtti::toml::parse_stream(src, pe);
    RTTI_CHECK(r.ok());
    // Typed scalar events and array events fire while a value is read; the
    // key_value event for that key follows (documented contract).
    const std::vector<std::string> want = {
        "S:",   "I:1", "K:g=1", "E:",   "S:a",   "I:2",
        "K:x=2", "E:a", "A:b",  "I:3",   "K:y=3", "B:b",
        "A:b",  "I:4", "K:y=4", "B:b",
    };
    RTTI_CHECK(ev == want);
}

RTTI_TEST_CASE(toml_sax_stream_array_and_cancel) {
    int kv = 0;
    rtti::toml::parse_events pe;
    pe.table_start = [&](rtti::string_view) { return true; };
    pe.key_value = [&](rtti::string_view, rtti::string_view) {
        ++kv;
        return kv < 2;
    };
    pe.string_value = [&](rtti::string_view) { return true; };
    pe.integer_value = [&](std::int64_t) { return true; };
    pe.array_start = [&]() { return true; };
    pe.array_end = [&]() { return true; };
    pe.table_end = [&](rtti::string_view) { return true; };
    // Cancel after the second key: parse still reports ok and no further
    // key_value events fire.
    auto r = rtti::toml::parse_stream("a = 1\nb = 2\nc = 3\n", pe);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(kv, 2);

    // Array values report array_start/array_end plus typed element events.
    std::vector<std::string> ev;
    rtti::toml::parse_events pe2;
    pe2.table_start = [&](rtti::string_view) { return true; };
    pe2.table_end = [&](rtti::string_view) { return true; };
    pe2.key_value = [&](rtti::string_view k, rtti::string_view v) {
        ev.push_back("K:" + std::string(k) + "=" + std::string(v));
        return true;
    };
    pe2.array_start = [&]() {
        ev.push_back("AS");
        return true;
    };
    pe2.array_end = [&]() {
        ev.push_back("AE");
        return true;
    };
    pe2.integer_value = [&](std::int64_t v) {
        ev.push_back("I:" + std::to_string(v));
        return true;
    };
    auto r2 = rtti::toml::parse_stream("nums = [1, 2]\n", pe2);
    RTTI_CHECK(r2.ok());
    const std::vector<std::string> want2 = {"AS", "I:1", "I:2", "AE",
                                            "K:nums=[1, 2]"};
    RTTI_CHECK(ev == want2);
}

RTTI_TEST_CASE(toml_sax_stream_syntax_error) {
    auto empty = rtti::toml::parse_stream("", rtti::toml::parse_events{});
    RTTI_CHECK(!empty.ok());
    RTTI_CHECK_EQ(empty.err.code, rtti::error_code::invalid_root);

    auto bad = rtti::toml::parse_stream("[a\n", rtti::toml::parse_events{});
    RTTI_CHECK(!bad.ok());

    auto trunc = rtti::toml::parse_stream("k = \"abc", rtti::toml::parse_events{});
    RTTI_CHECK(!trunc.ok());
    RTTI_CHECK_EQ(trunc.err.code, rtti::error_code::unterminated_string);

    auto silent =
        rtti::toml::parse_stream("a = 1\n[b]\nc = 2\n", rtti::toml::parse_events{});
    RTTI_CHECK(silent.ok());
}

RTTI_TEST_CASE(toml_serialize_buffer) {
    auto d = rtti::toml::parse("a = 1\n[b]\nc = 2\n");
    RTTI_CHECK(d.ok());
    std::string buf = "this old content is longer than the serialized output";
    rtti::toml::serialize(d.value, buf);
    RTTI_CHECK_EQ(buf, rtti::toml::serialize(d.value));
    const std::string first = buf;
    buf.clear();
    buf.shrink_to_fit();
    rtti::toml::serialize(d.value, buf);
    RTTI_CHECK_EQ(buf, first);
}

RTTI_TEST_CASE(toml_malformed_no_crash) {
    auto r1 = rtti::toml::parse("");
    RTTI_CHECK(!r1.ok());
    RTTI_CHECK_EQ(r1.err.code, rtti::error_code::invalid_root);

    auto r2 = rtti::toml::parse("   \n\t\n");
    RTTI_CHECK(!r2.ok());

    auto r3 = rtti::toml::parse("key");
    RTTI_CHECK(!r3.ok());

    auto r4 = rtti::toml::parse("key =");
    RTTI_CHECK(!r4.ok());

    auto r5 = rtti::toml::parse("key = \"unterminated");
    RTTI_CHECK(!r5.ok());
    RTTI_CHECK_EQ(r5.err.code, rtti::error_code::unterminated_string);

    auto r6 = rtti::toml::parse("key = [1, 2");
    RTTI_CHECK(!r6.ok());

    auto r7 = rtti::toml::parse("key = 1 ]");
    RTTI_CHECK(!r7.ok());

    auto r8 = rtti::toml::parse("[a");
    RTTI_CHECK(!r8.ok());

    auto r9 = rtti::toml::parse("[[a]");
    RTTI_CHECK(!r9.ok());

    auto r10 = rtti::toml::parse("key = 0x");
    RTTI_CHECK(!r10.ok());
    RTTI_CHECK_EQ(r10.err.code, rtti::error_code::invalid_number);

    auto r11 = rtti::toml::parse("key = 1.2.3");
    RTTI_CHECK(!r11.ok());
    RTTI_CHECK_EQ(r11.err.code, rtti::error_code::invalid_number);

    auto r12 = rtti::toml::parse("key = nope");
    RTTI_CHECK(!r12.ok());

    auto r13 = rtti::toml::parse("key = 'unterminated");
    RTTI_CHECK(!r13.ok());

    auto r14 = rtti::toml::parse("key = +");
    RTTI_CHECK(!r14.ok());

    auto r15 = rtti::toml::parse("[[ ]]");
    RTTI_CHECK(!r15.ok());

    auto r16 = rtti::toml::parse("a..b = 1");
    RTTI_CHECK(!r16.ok());
}

RTTI_TEST_CASE(toml_surrogate_escape_validation) {
    // G-4 pin: basic-string \uXXXX escapes must decode a valid surrogate
    // pair to the astral code point and reject lone low surrogates, lone
    // high surrogates, and an unpaired high surrogate ahead of ordinary
    // text with invalid_utf8 (toml.cpp decode_escape).
    {
        const auto d = rtti::toml::parse("pair = \"\\ud83d\\ude00\"\n");
        RTTI_CHECK(d.ok());
        std::string s;
        rtti::value_io* n = nullptr;
        RTTI_CHECK(d.value.get_key("pair", n) && str_of(n, s));
        RTTI_CHECK_EQ(s, std::string("\xf0\x9f\x98\x80"));
    }
    const char* bad[] = {
        "hi = \"\\ud800\"\n",  // lone high surrogate
        "hi = \"\\ud800x\"\n", // high surrogate not followed by a low escape
        "lo = \"\\udfff\"\n",  // lone low surrogate
    };
    for (const char* src : bad) {
        const auto d = rtti::toml::parse(src);
        RTTI_CHECK(!d.ok());
        if (!d.ok()) {
            RTTI_CHECK_EQ(d.err.code, rtti::error_code::invalid_utf8);
        }
    }
}

RTTI_TEST_CASE(toml_deep_nesting_too_deep) {
    std::string deep = "a = ";
    for (int i = 0; i < 600; ++i) {
        deep.push_back('[');
    }
    deep.push_back('1');
    for (int i = 0; i < 600; ++i) {
        deep.push_back(']');
    }
    auto d = rtti::toml::parse(deep);
    RTTI_CHECK(!d.ok());
    RTTI_CHECK_EQ(d.err.code, rtti::error_code::too_deep);
}

// Exact depth-limit boundary: the default max_depth is 512, so a 512-deep
// array nests parses while the 600-deep case above reports too_deep.
RTTI_TEST_CASE(toml_deep_nesting_boundary) {
    std::string ok = "a = ";
    for (int i = 0; i < 512; ++i) {
        ok.push_back('[');
    }
    ok.push_back('1');
    for (int i = 0; i < 512; ++i) {
        ok.push_back(']');
    }
    auto d = rtti::toml::parse(ok);
    RTTI_CHECK(d.ok());
}

// --- Deep table/key path guard (R1-2) -----------------------------------------
// A dotted table header or key path with an unbounded component count used to
// create branches that emit_object() then recursed into (stack overflow). The
// path reader now caps the component count at the parser max_depth and reports
// error_code::too_deep instead of building the tree.
RTTI_TEST_CASE(toml_deep_table_path_too_deep) {
    std::string name;
    for (int i = 0; i < 600; ++i) {
        if (i != 0) {
            name.push_back('.');
        }
        name += "a";
    }
    const std::string table = "[" + name + "]\nk = 1\n";
    auto rt = rtti::toml::parse(table);
    RTTI_CHECK(!rt.ok());
    RTTI_CHECK_EQ(rt.err.code, rtti::error_code::too_deep);

    const std::string keypath = name + " = 1\n";
    auto rk = rtti::toml::parse(keypath);
    RTTI_CHECK(!rk.ok());
    RTTI_CHECK_EQ(rk.err.code, rtti::error_code::too_deep);

    const std::string arrtable = "[[" + name + "]]\nk = 1\n";
    auto ra = rtti::toml::parse(arrtable);
    RTTI_CHECK(!ra.ok());
    RTTI_CHECK_EQ(ra.err.code, rtti::error_code::too_deep);

    // A reasonable depth still parses and serializes without error.
    std::string ok_name;
    for (int i = 0; i < 200; ++i) {
        if (i != 0) {
            ok_name.push_back('.');
        }
        ok_name += "a";
    }
    const std::string ok_src = "[" + ok_name + "]\nk = 2\n";
    auto ok = rtti::toml::parse(ok_src);
    RTTI_CHECK(ok.ok());
    const std::string out = rtti::toml::serialize(ok.value);
    RTTI_CHECK(out.find("k = 2") != std::string::npos);
}

// A dotted key inside an inline table that exceeds the path depth cap must
// also report too_deep: parse_inline_table used to overwrite read_path's
// too_deep with unexpected_token (toml.hpp:1504-1507), violating the header
// contract that deep nesting yields error_code::too_deep (toml.hpp:27-29).
RTTI_TEST_CASE(toml_deep_inline_table_path_too_deep) {
    std::string key;
    for (int i = 0; i < 600; ++i) {
        if (i != 0) {
            key.push_back('.');
        }
        key += "a";
    }
    const std::string src = "x = {" + key + " = 1 }\n";
    auto r = rtti::toml::parse(src);
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ(r.err.code, rtti::error_code::too_deep);
}

} // namespace rtti_test_lib
