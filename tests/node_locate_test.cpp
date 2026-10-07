// Node-level conversion tests: locate / node_as / to_node (spec 4.3).
// Unified path navigation + node-level bidirectional conversion over every
// value_io-backed DOM format (JSON / XML / INI / TOML / proto). English
// comments only.
//
// The format adapters are feature-gated; this file compiles to nothing when
// any of the five gates is off (the adapter headers #error in stripped
// builds, mirroring date_time_test.cpp / tests/CMakeLists.txt).

#include "test_harness.hpp"
#include <rtti/node_io.hpp>
#include <string>
#include <vector>

#if defined(RTTI_ENABLE_JSON) && RTTI_ENABLE_JSON &&    \
    defined(RTTI_ENABLE_XML) && RTTI_ENABLE_XML &&      \
    defined(RTTI_ENABLE_INI) && RTTI_ENABLE_INI &&      \
    defined(RTTI_ENABLE_TOML) && RTTI_ENABLE_TOML &&    \
    defined(RTTI_ENABLE_PROTO) && RTTI_ENABLE_PROTO

#include <rtti/json.hpp>
#include <rtti/xml.hpp>
#include <rtti/ini.hpp>
#include <rtti/toml.hpp>
#include <rtti/proto.hpp>

namespace rtti_test_lib {

struct NodeLocateAddress {
    std::string city;
    int zip = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(NodeLocateAddress, city, zip)
};

struct NodeLocateIniCfg {
    int newkey = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(NodeLocateIniCfg, newkey)
};

struct NodeLocateTomlCfg {
    int x = 0;
    int y = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(NodeLocateTomlCfg, x, y)
};

namespace {

// Root JSON document used by the positive / node_as / to_node cases.
rtti::json::document sample_json() {
    auto d = rtti::json::parse(
        "{\"user\":{\"name\":\"ada\",\"age\":42,"
        "\"tags\":[1,2,3],"
        "\"addresses\":[{\"city\":\"Milan\",\"zip\":20121},"
        "{\"city\":\"Rome\",\"zip\":100}]}}");
    // parse() cannot fail on this literal; a failed document is a null tree
    // and the cases below would fail loudly instead of crash.
    (void)d.ok();
    return std::move(d.value);
}

} // namespace

RTTI_TEST_CASE(NodeLocateJsonPositive) {
    rtti::json::document doc = sample_json();

    const auto u = rtti::locate(doc, "user");
    RTTI_CHECK(u.ok());
    RTTI_CHECK(*u != nullptr);

    const auto city = rtti::locate(doc, "user.addresses[1].city");
    RTTI_CHECK(city.ok());
    RTTI_CHECK(*city != nullptr);
    const auto city_v = rtti::node_as<std::string>(*city);
    RTTI_CHECK(city_v.ok());
    RTTI_CHECK_EQ(city_v.value, std::string("Rome"));

    const auto name = rtti::locate(doc, "user.name");
    RTTI_CHECK(name.ok());
    RTTI_CHECK_EQ(rtti::node_as<std::string>(*name).value, std::string("ada"));

    // Nested object via index, then converted as a reflected struct.
    const auto addr = rtti::node_as<NodeLocateAddress>(
        *rtti::locate(doc, "user.addresses[0]"));
    RTTI_CHECK(addr.ok());
    RTTI_CHECK_EQ(addr.value.city, std::string("Milan"));
    RTTI_CHECK_EQ(addr.value.zip, 20121);

    // Chain an index after a key and index twice ("a[0][1]" shape).
    auto d2 = rtti::json::parse("{\"m\":[[1,2],[3,4]]}");
    RTTI_CHECK(d2.ok());
    RTTI_CHECK_EQ(rtti::node_as<int>(*rtti::locate(d2.value, "m[1][0]")).value, 3);

    // Indexing into an array whose first element is a scalar and the second a
    // nested array: m[1] is [1,2], so m[1][0] == 1.
    auto d3 = rtti::json::parse("{\"m\":[5,[1,2]]}");
    RTTI_CHECK(d3.ok());
    RTTI_CHECK_EQ(rtti::node_as<int>(*rtti::locate(d3.value, "m[1][0]")).value, 1);
}

RTTI_TEST_CASE(NodeLocateConst) {
    rtti::json::document doc = sample_json();
    const rtti::value_io& cref = doc;

    const auto r = rtti::locate(cref, "user.addresses[1].zip");
    RTTI_CHECK(r.ok());
    const auto z = rtti::node_as<int>(*r);
    RTTI_CHECK(z.ok());
    RTTI_CHECK_EQ(z.value, 100);
}

RTTI_TEST_CASE(NodeLocateJsonFailure) {
    auto d = rtti::json::parse("{\"a\":[{\"x\":1},{\"x\":2}]}");
    RTTI_CHECK(d.ok());

    // Missing key at the root and after an index.
    const auto missing = rtti::locate(d.value, "nope");
    RTTI_CHECK(!missing.ok());
    RTTI_CHECK_EQ(missing.err.code, rtti::error_code::no_such_field);

    const auto nested = rtti::locate(d.value, "a[1].z");
    RTTI_CHECK(!nested.ok());
    RTTI_CHECK_EQ(nested.err.code, rtti::error_code::no_such_field);

    // Out-of-range index.
    const auto oob = rtti::locate(d.value, "a[9]");
    RTTI_CHECK(!oob.ok());
    RTTI_CHECK_EQ(oob.err.code, rtti::error_code::no_such_field);

    // Empty path selects nothing (documented ruling, spec 4.3).
    const auto empty = rtti::locate(d.value, "");
    RTTI_CHECK(!empty.ok());
    RTTI_CHECK_EQ(empty.err.code, rtti::error_code::no_such_field);

    // Malformed paths are rejected as syntax/kind errors, never crash.
    const auto bad_step = rtti::locate(d.value, "a.[0]"); // index step after '.'
    RTTI_CHECK(!bad_step.ok());
    RTTI_CHECK_EQ(bad_step.err.code, rtti::error_code::kind_mismatch);

    RTTI_CHECK(!rtti::locate(d.value, ".a").ok());        // leading dot
    RTTI_CHECK(!rtti::locate(d.value, "a.").ok());        // trailing dot
    RTTI_CHECK(!rtti::locate(d.value, "a..x").ok());      // double dot
    RTTI_CHECK(!rtti::locate(d.value, "a[x]").ok());      // non-digit index
    RTTI_CHECK(!rtti::locate(d.value, "a[0").ok());       // unclosed bracket
    RTTI_CHECK(!rtti::locate(d.value, "a[0]x").ok());     // junk after index

    // Kind mismatches: key on an array, index on an object, index on a scalar.
    const auto key_on_array = rtti::locate(d.value, "a.x");
    RTTI_CHECK(!key_on_array.ok());
    RTTI_CHECK_EQ(key_on_array.err.code, rtti::error_code::kind_mismatch);

    const auto idx_on_object = rtti::locate(d.value, "a[0][1]");
    RTTI_CHECK(!idx_on_object.ok());
    RTTI_CHECK_EQ(idx_on_object.err.code, rtti::error_code::kind_mismatch);

    // A genuine scalar-indexing negative: [0] applied to the scalar 5.
    auto d2 = rtti::json::parse("{\"m\":[5,[1,2]]}");
    RTTI_CHECK(d2.ok());
    const auto idx_on_scalar = rtti::locate(d2.value, "m[0][0]");
    RTTI_CHECK(!idx_on_scalar.ok());
    RTTI_CHECK_EQ(idx_on_scalar.err.code, rtti::error_code::kind_mismatch);

    // Index overflow beyond size_t is treated as an out-of-range index.
    const std::string big = "a[" + std::string(30, '9') + "]";
    const auto overflow = rtti::locate(d.value, big);
    RTTI_CHECK(!overflow.ok());
    RTTI_CHECK_EQ(overflow.err.code, rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(NodeLocateNodeAs) {
    auto d = rtti::json::parse(
        "{\"i\":42,\"s\":\"hi\",\"u\":true,\"tags\":[1,2,3],"
        "\"addr\":{\"city\":\"Seoul\",\"zip\":1}}");
    RTTI_CHECK(d.ok());

    const auto ni = rtti::node_as<int>(*rtti::locate(d.value, "i"));
    RTTI_CHECK(ni.ok());
    RTTI_CHECK_EQ(ni.value, 42);

    const auto ns = rtti::node_as<std::string>(*rtti::locate(d.value, "s"));
    RTTI_CHECK(ns.ok());
    RTTI_CHECK_EQ(ns.value, std::string("hi"));

    const auto nb = rtti::node_as<bool>(*rtti::locate(d.value, "u"));
    RTTI_CHECK(nb.ok());
    RTTI_CHECK_EQ(nb.value, true);

    const auto nt = rtti::node_as<std::vector<int>>(*rtti::locate(d.value, "tags"));
    RTTI_CHECK(nt.ok());
    RTTI_CHECK_EQ(nt.value.size(), std::size_t(3));
    RTTI_CHECK_EQ(nt.value[0], 1);
    RTTI_CHECK_EQ(nt.value[1], 2);
    RTTI_CHECK_EQ(nt.value[2], 3);

    const auto na =
        rtti::node_as<NodeLocateAddress>(*rtti::locate(d.value, "addr"));
    RTTI_CHECK(na.ok());
    RTTI_CHECK_EQ(na.value.city, std::string("Seoul"));
    RTTI_CHECK_EQ(na.value.zip, 1);

    // A null node converts to nothing (fails cleanly, never crashes).
    const auto nz = rtti::node_as<int>(nullptr);
    RTTI_CHECK(!nz.ok());
    RTTI_CHECK_EQ(nz.err.code, rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(NodeLocateToNode) {
    rtti::json::document doc = sample_json();

    // Replace a scalar subtree; verify by locate + node_as and by the
    // serialized document.
    {
        const auto loc = rtti::locate(doc, "user.age");
        RTTI_CHECK(loc.ok());
        RTTI_CHECK_EQ(rtti::to_node(**loc, 99), rtti::error_code::ok);
    }
    const auto age = rtti::node_as<int>(*rtti::locate(doc, "user.age"));
    RTTI_CHECK(age.ok());
    RTTI_CHECK_EQ(age.value, 99);
    rtti::json::write_config compact;
    compact.indent = 0;
    const std::string out1 = rtti::json::serialize(doc, compact);
    RTTI_CHECK(out1.find("\"age\":99") != std::string::npos);

    // Replace a nested object subtree; round-trip through a reflected struct.
    {
        const auto loc = rtti::locate(doc, "user.addresses[1]");
        RTTI_CHECK(loc.ok());
        RTTI_CHECK_EQ(rtti::to_node(**loc, NodeLocateAddress{"Paris", 75000}),
                      rtti::error_code::ok);
    }
    const auto addr = rtti::node_as<NodeLocateAddress>(
        *rtti::locate(doc, "user.addresses[1]"));
    RTTI_CHECK(addr.ok());
    RTTI_CHECK_EQ(addr.value.city, std::string("Paris"));
    RTTI_CHECK_EQ(addr.value.zip, 75000);

    // The untouched sibling still locates and converts unchanged, and the
    // whole document re-serializes consistently.
    const auto city = rtti::node_as<std::string>(
        *rtti::locate(doc, "user.addresses[0].city"));
    RTTI_CHECK_EQ(city.value, std::string("Milan"));
    const std::string out2 = rtti::json::serialize(doc, compact);
    RTTI_CHECK(out2.find("\"city\":\"Paris\"") != std::string::npos);
    RTTI_CHECK(out2.find("\"city\":\"Milan\"") != std::string::npos);
}

RTTI_TEST_CASE(NodeLocateToNodeIniToml) {
    // INI: replacing an object-valued section subtree must drop stale keys
    // instead of merging them (spec 4.3 replace semantics).
    {
        auto d = rtti::ini::parse("[sect]\nz=1\nkeep=2\n");
        RTTI_CHECK(d.ok());
        const auto loc = rtti::locate(d.value, "sect");
        RTTI_CHECK(loc.ok());
        RTTI_CHECK_EQ(rtti::to_node(**loc, NodeLocateIniCfg{7}), rtti::error_code::ok);
        const std::string out = rtti::ini::serialize(d.value);
        RTTI_CHECK(out.find("newkey = 7") != std::string::npos);
        RTTI_CHECK(out.find("keep") == std::string::npos);
        RTTI_CHECK(out.find("z = 1") == std::string::npos);
        const auto back = rtti::node_as<NodeLocateIniCfg>(
            *rtti::locate(d.value, "sect"));
        RTTI_CHECK(back.ok());
        RTTI_CHECK_EQ(back.value.newkey, 7);
    }
    // INI: replacing the whole root object must not keep old top-level keys.
    {
        auto d = rtti::ini::parse("z=1\nkeep=2\n");
        RTTI_CHECK(d.ok());
        RTTI_CHECK_EQ(rtti::to_node(d.value, NodeLocateIniCfg{9}), rtti::error_code::ok);
        const std::string out = rtti::ini::serialize(d.value);
        RTTI_CHECK(out.find("newkey = 9") != std::string::npos);
        RTTI_CHECK(out.find("keep") == std::string::npos);
    }
    // TOML: replacing an array subtree must clear previous elements.
    {
        auto d = rtti::toml::parse("list=[1,2,3]\n");
        RTTI_CHECK(d.ok());
        const auto loc = rtti::locate(d.value, "list");
        RTTI_CHECK(loc.ok());
        RTTI_CHECK_EQ(rtti::to_node(**loc, std::vector<int>{7, 8}),
                      rtti::error_code::ok);
        const std::string out = rtti::toml::serialize(d.value);
        RTTI_CHECK(out.find("7") != std::string::npos);
        RTTI_CHECK(out.find("8") != std::string::npos);
        RTTI_CHECK(out.find("3") == std::string::npos);
        const auto back = rtti::node_as<std::vector<int>>(
            *rtti::locate(d.value, "list"));
        RTTI_CHECK(back.ok());
        RTTI_CHECK_EQ(back.value.size(), std::size_t(2));
        RTTI_CHECK_EQ(back.value[0], 7);
        RTTI_CHECK_EQ(back.value[1], 8);
    }
    // TOML: replacing a table object subtree must not keep stale keys.
    {
        auto d = rtti::toml::parse("[tbl]\nx=1\ny=2\n");
        RTTI_CHECK(d.ok());
        const auto loc = rtti::locate(d.value, "tbl");
        RTTI_CHECK(loc.ok());
        RTTI_CHECK_EQ(rtti::to_node(**loc, NodeLocateTomlCfg{5, 6}),
                      rtti::error_code::ok);
        const std::string out = rtti::toml::serialize(d.value);
        RTTI_CHECK(out.find("x = 5") != std::string::npos);
        RTTI_CHECK(out.find("y = 6") != std::string::npos);
        RTTI_CHECK(out.find("y = 2") == std::string::npos);
        const auto back = rtti::node_as<NodeLocateTomlCfg>(
            *rtti::locate(d.value, "tbl"));
        RTTI_CHECK(back.ok());
        RTTI_CHECK_EQ(back.value.x, 5);
        RTTI_CHECK_EQ(back.value.y, 6);
    }
}

RTTI_TEST_CASE(NodeLocateRootIndex) {
    auto d = rtti::json::parse("[10,20,{\"k\":7}]");
    RTTI_CHECK(d.ok());

    const auto one = rtti::node_as<int>(*rtti::locate(d.value, "[1]"));
    RTTI_CHECK_EQ(one.value, 20);

    const auto deep = rtti::node_as<int>(*rtti::locate(d.value, "[2].k"));
    RTTI_CHECK_EQ(deep.value, 7);

    const auto oob = rtti::locate(d.value, "[9]");
    RTTI_CHECK(!oob.ok());
    RTTI_CHECK_EQ(oob.err.code, rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(NodeLocateAllFormatsSmoke) {
    // --- JSON --------------------------------------------------------------
    {
        const auto d =
            rtti::json::parse("{\"server\":{\"host\":\"example.com\",\"port\":8080}}");
        RTTI_CHECK(d.ok());
        RTTI_CHECK_EQ(
            rtti::node_as<std::string>(*rtti::locate(d.value, "server.host")).value,
            std::string("example.com"));
        RTTI_CHECK_EQ(rtti::node_as<int>(*rtti::locate(d.value, "server.port")).value,
                      8080);
        RTTI_CHECK(!rtti::locate(d.value, "server.bogus").ok());
    }
    // --- XML: an element carrying text is reached via "[0].value" -----------
    {
        const auto d = rtti::xml::parse(
            "<root><user><name>ada</name><age>42</age></user></root>");
        RTTI_CHECK(d.ok());
        RTTI_CHECK_EQ(
            rtti::node_as<std::string>(*rtti::locate(d.value, "user.name[0].value")).value,
            std::string("ada"));
        const auto a = rtti::node_as<int>(*rtti::locate(d.value, "user.age[0].value"));
        RTTI_CHECK(a.ok());
        RTTI_CHECK_EQ(a.value, 42);
        RTTI_CHECK(!rtti::locate(d.value, "user.nope").ok());
    }
    // --- INI: sections become nested object keys ----------------------------
    {
        const auto d = rtti::ini::parse("[server]\nhost=example.com\nport=8080\n");
        RTTI_CHECK(d.ok());
        RTTI_CHECK_EQ(
            rtti::node_as<std::string>(*rtti::locate(d.value, "server.host")).value,
            std::string("example.com"));
        const auto p = rtti::node_as<int>(*rtti::locate(d.value, "server.port"));
        RTTI_CHECK(p.ok());
        RTTI_CHECK_EQ(p.value, 8080);
        RTTI_CHECK(!rtti::locate(d.value, "server.bogus").ok());
    }
    // --- TOML: scalars, arrays, tables --------------------------------------
    {
        const auto d = rtti::toml::parse(
            "title=\"Hello\"\ncount=42\nlist=[1,2,3]\n[section]\nz=\"bye\"\n");
        RTTI_CHECK(d.ok());
        RTTI_CHECK_EQ(
            rtti::node_as<std::string>(*rtti::locate(d.value, "title")).value,
            std::string("Hello"));
        RTTI_CHECK_EQ(rtti::node_as<int>(*rtti::locate(d.value, "count")).value, 42);
        RTTI_CHECK_EQ(rtti::node_as<int>(*rtti::locate(d.value, "list[2]")).value, 3);
        RTTI_CHECK_EQ(
            rtti::node_as<std::string>(*rtti::locate(d.value, "section.z")).value,
            std::string("bye"));
        RTTI_CHECK(!rtti::locate(d.value, "missing").ok());
    }
    // --- proto: schema-less parse, object keys are decimal field numbers ----
    {
        // field 1 = varint 42, field 2 length 2 + "hi" (6 bytes total).
        const std::string wire("\x08\x2A\x12\x02hi", 6);
        const auto d = rtti::proto::parse(rtti::string_view(wire));
        RTTI_CHECK(d.ok());
        RTTI_CHECK_EQ(rtti::node_as<int>(*rtti::locate(d.value, "1")).value, 42);
        RTTI_CHECK_EQ(
            rtti::node_as<std::string>(*rtti::locate(d.value, "2")).value,
            std::string("hi"));
        RTTI_CHECK(!rtti::locate(d.value, "3").ok());
    }
}

} // namespace rtti_test_lib

#endif // all five format gates enabled