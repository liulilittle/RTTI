// Naming-policy configuration tests (key_case / rename_map / skip_empty /
// indentation). Task D1+D2: naming applies only through the format-layer
// serialize(const T&, cfg) / parse_as<T>(src, cfg) entry points, which route
// through the to_value_named / from_value_named convert overloads; the core
// to_value / from_value signatures stay untouched.

#include "test_harness.hpp"

#include <rtti/reflection.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rtti_test_lib {

struct NamingSnake { int fieldName = 5; };
struct NamingCamel { int first_name = 5; };
struct NamingSkip { int a = 1; std::string empty = ""; std::vector<int> vec; };
struct NamingZero { int zero = 0; double ratio = 0.0; int one = 1; };
struct NamingParse { int fieldName = 0; std::string other = ""; };
struct NamingRm { int a = 0; };
struct NamingInner { int innerField = 9; };
struct NamingOuter {
    int outerField = 1;
    NamingInner direct;
    std::vector<NamingInner> vec;
};
struct NamingOpt { std::optional<int> opt; int present = 5; };
struct NamingBool { bool active = false; bool ok = true; };

} // namespace rtti_test_lib

// Every test in this file runs through the format serialize(cfg)/parse_as
// entry points, and the RTTI_DEFINE_TYPE registrations below are only consumed
// by those tests (they also pull in format-only converter instantiations).
// Gated so a stripped build (all RTTI_ENABLE_* = 0) skips this section without
// tripping the format headers' #error guards.
#if (!defined(RTTI_ENABLE_JSON) || RTTI_ENABLE_JSON) && \
    (!defined(RTTI_ENABLE_XML) || RTTI_ENABLE_XML) && \
    (!defined(RTTI_ENABLE_INI) || RTTI_ENABLE_INI)

#include <rtti/ini.hpp>
#include <rtti/json.hpp>
#include <rtti/xml.hpp>

RTTI_DEFINE_TYPE(rtti_test_lib::NamingSnake, fieldName)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingCamel, first_name)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingSkip, a, empty, vec)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingZero, zero, ratio, one)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingParse, fieldName, other)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingRm, a)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingInner, innerField)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingOuter, outerField, direct, vec)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingOpt, opt, present)
RTTI_DEFINE_TYPE(rtti_test_lib::NamingBool, active, ok)

namespace rtti_test_lib {

RTTI_TEST_CASE(naming_snake_json) {
    rtti::json::write_config cfg;
    cfg.key_case = rtti::json::key_case::snake;
    const std::string j = rtti::json::serialize(NamingSnake{5}, cfg);
    RTTI_CHECK(j.find("field_name") != std::string::npos);
}

RTTI_TEST_CASE(naming_camel_json) {
    rtti::json::write_config cfg;
    cfg.key_case = rtti::json::key_case::camel;
    const std::string j = rtti::json::serialize(NamingCamel{5}, cfg);
    RTTI_CHECK(j.find("firstName") != std::string::npos);
}

// rename_map wins over key_case; skip_empty drops empty string and empty vec.
RTTI_TEST_CASE(naming_rename_map_and_skip) {
    std::map<std::string, std::string> rm;
    rm["a"] = "alpha";
    rtti::json::write_config cfg;
    cfg.rename_map = &rm;
    cfg.skip_empty = true;
    const std::string j = rtti::json::serialize(NamingSkip{}, cfg);
    RTTI_CHECK(j.find("alpha") != std::string::npos);
    RTTI_CHECK(j.find("empty") == std::string::npos);
    RTTI_CHECK(j.find("vec") == std::string::npos);
}

// skip_empty also drops numeric-zero fields (0 int and 0.0 double).
RTTI_TEST_CASE(naming_skip_zero) {
    rtti::json::write_config cfg;
    cfg.skip_empty = true;
    const std::string j = rtti::json::serialize(NamingZero{}, cfg);
    RTTI_CHECK(j.find("zero") == std::string::npos);
    RTTI_CHECK(j.find("ratio") == std::string::npos);
    RTTI_CHECK(j.find("one") != std::string::npos);
}

// skip_empty drops null-valued fields (empty std::optional) and false bools.
RTTI_TEST_CASE(naming_skip_null) {
    rtti::json::write_config cfg;
    cfg.skip_empty = true;
    const std::string j = rtti::json::serialize(NamingOpt{}, cfg);
    RTTI_CHECK(j.find("opt") == std::string::npos);
    RTTI_CHECK(j.find("present") != std::string::npos);
}

RTTI_TEST_CASE(naming_skip_false) {
    rtti::json::write_config cfg;
    cfg.skip_empty = true;
    const std::string j = rtti::json::serialize(NamingBool{}, cfg);
    RTTI_CHECK(j.find("active") == std::string::npos);
    RTTI_CHECK(j.find("ok") != std::string::npos);
}

// Naming propagates recursively: a direct struct field and every element of a
// container field are renamed under the same policy, not just the top level.
RTTI_TEST_CASE(naming_nested_snake) {
    rtti::json::write_config cfg;
    cfg.key_case = rtti::json::key_case::snake;
    NamingOuter o;
    o.direct.innerField = 4;
    o.vec.push_back(NamingInner{7});
    const std::string j = rtti::json::serialize(o, cfg);
    RTTI_CHECK(j.find("outer_field") != std::string::npos);
    RTTI_CHECK(j.find("innerField") == std::string::npos);
    std::size_t inner_count = 0;
    for (std::size_t p = 0; (p = j.find("inner_field", p)) != std::string::npos;
         ++p) {
        ++inner_count;
    }
    RTTI_CHECK_EQ(inner_count, std::size_t(2));
    const auto back = rtti::json::parse_as<NamingOuter>(j, cfg);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK_EQ(back.value.direct.innerField, 4);
        RTTI_CHECK_EQ(back.value.vec.size(), std::size_t(1));
        if (back.value.vec.size() == 1) {
            RTTI_CHECK_EQ(back.value.vec[0].innerField, 7);
        }
    }
}

RTTI_TEST_CASE(naming_snake_xml_ini) {
    rtti::xml::write_config cx;
    cx.key_case = rtti::xml::key_case::snake;
    RTTI_CHECK(rtti::xml::serialize(NamingSnake{5}, cx).find("field_name") !=
               std::string::npos);
    rtti::ini::write_config ci;
    ci.key_case = rtti::ini::key_case::snake;
    RTTI_CHECK(rtti::ini::serialize(NamingSnake{5}, ci).find("field_name") !=
               std::string::npos);
}

// parse_as reverses the naming: output key -> field name (snake back to camel).
RTTI_TEST_CASE(naming_parse_reverse) {
    rtti::json::write_config cfg;
    cfg.key_case = rtti::json::key_case::snake;
    const std::string j = rtti::json::serialize(NamingParse{42, "hi"}, cfg);
    RTTI_CHECK(j.find("field_name") != std::string::npos);
    const auto back = rtti::json::parse_as<NamingParse>(j, cfg);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK_EQ(back.value.fieldName, 42);
        RTTI_CHECK_EQ(back.value.other, std::string("hi"));
    }
}

// parse_as reverse-renames through rename_map.
RTTI_TEST_CASE(naming_rename_parse) {
    std::map<std::string, std::string> rm;
    rm["a"] = "alpha";
    rtti::json::write_config cfg;
    cfg.rename_map = &rm;
    const std::string j = rtti::json::serialize(NamingRm{7}, cfg);
    RTTI_CHECK(j.find("alpha") != std::string::npos);
    const auto back = rtti::json::parse_as<NamingRm>(j, cfg);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK_EQ(back.value.a, 7);
    }
}

// Custom indentation character reaches the element-nesting indentation.
RTTI_TEST_CASE(naming_indent_char) {
    rtti::xml::write_config cx;
    cx.indent = 1;
    cx.indent_char = '\t';
    const std::string x = rtti::xml::serialize(NamingSnake{5}, cx);
    RTTI_CHECK(x.find("\n\t<fieldName>") != std::string::npos);
    rtti::json::write_config cj;
    cj.indent = 1;
    cj.indent_char = '\t';
    const std::string j = rtti::json::serialize(NamingSnake{5}, cj);
    RTTI_CHECK(j.find("\n\t\"fieldName\"") != std::string::npos);
}

// skip_empty reaches the XML and INI adapters too (existing coverage was JSON
// only): empty string / empty container / false bool / empty optional gone,
// non-empty fields kept.
RTTI_TEST_CASE(naming_skip_empty_xml_ini) {
    rtti::xml::write_config cx;
    cx.skip_empty = true;
    const std::string x = rtti::xml::serialize(NamingSkip{}, cx);
    RTTI_CHECK(x.find("empty") == std::string::npos);
    RTTI_CHECK(x.find("vec") == std::string::npos);
    RTTI_CHECK(x.find(">1<") != std::string::npos);  // a = 1 survives
    const std::string xo = rtti::xml::serialize(NamingOpt{}, cx);
    RTTI_CHECK(xo.find("opt") == std::string::npos);  // empty optional dropped
    RTTI_CHECK(xo.find("present") != std::string::npos);
    const std::string xb = rtti::xml::serialize(NamingBool{}, cx);
    RTTI_CHECK(xb.find("active") == std::string::npos);  // false bool dropped
    RTTI_CHECK(xb.find("ok") != std::string::npos);

    rtti::ini::write_config ci;
    ci.skip_empty = true;
    const std::string i = rtti::ini::serialize(NamingSkip{}, ci);
    RTTI_CHECK(i.find("empty") == std::string::npos);
    RTTI_CHECK(i.find("vec") == std::string::npos);
    RTTI_CHECK(i.find("a = 1") != std::string::npos);
    const std::string io = rtti::ini::serialize(NamingOpt{}, ci);
    RTTI_CHECK(io.find("opt") == std::string::npos);
    RTTI_CHECK(io.find("present") != std::string::npos);
    const std::string ib = rtti::ini::serialize(NamingBool{}, ci);
    RTTI_CHECK(ib.find("active") == std::string::npos);
    RTTI_CHECK(ib.find("ok") != std::string::npos);
}

// rename_map reaches XML/INI with parse-side reverse mapping (existing coverage
// was JSON only).
RTTI_TEST_CASE(naming_rename_map_xml_ini) {
    std::map<std::string, std::string> rm;
    rm["a"] = "alpha";
    rtti::xml::write_config cx;
    cx.rename_map = &rm;
    const std::string x = rtti::xml::serialize(NamingRm{7}, cx);
    RTTI_CHECK(x.find("alpha") != std::string::npos);
    const auto bx = rtti::xml::parse_as<NamingRm>(x, cx);
    RTTI_CHECK(bx.ok());
    if (bx.ok()) {
        RTTI_CHECK_EQ(bx.value.a, 7);
    }

    rtti::ini::write_config ci;
    ci.rename_map = &rm;
    const std::string i = rtti::ini::serialize(NamingRm{7}, ci);
    RTTI_CHECK(i.find("alpha") != std::string::npos);
    const auto bi = rtti::ini::parse_as<NamingRm>(i, ci);
    RTTI_CHECK(bi.ok());
    if (bi.ok()) {
        RTTI_CHECK_EQ(bi.value.a, 7);
    }
}

// Nested skip_empty: a nonzero-outer nested struct whose inner int is zero is
// dropped recursively; a struct field itself is never "empty", only its empty
// children are.
RTTI_TEST_CASE(naming_skip_empty_nested_json_xml_ini) {
    NamingOuter o;
    o.direct.innerField = 0;  // zero int -> dropped below the nested level
    o.vec.push_back(NamingInner{0});
    o.vec.push_back(NamingInner{0});

    rtti::json::write_config cj;
    cj.skip_empty = true;
    const std::string j = rtti::json::serialize(o, cj);
    RTTI_CHECK(j.find("outerField") != std::string::npos);  // struct kept
    RTTI_CHECK(j.find("innerField") == std::string::npos);  // all zeros dropped
    RTTI_CHECK(j.find("\"vec\"") != std::string::npos);     // vector not empty

    rtti::xml::write_config cx;
    cx.skip_empty = true;
    const std::string x = rtti::xml::serialize(o, cx);
    RTTI_CHECK(x.find("innerField") == std::string::npos);
    RTTI_CHECK(x.find("vec") != std::string::npos);      // vector not empty

    rtti::ini::write_config ci;
    ci.skip_empty = true;
    const std::string i = rtti::ini::serialize(o, ci);
    RTTI_CHECK(i.find("innerField") == std::string::npos);
}

} // namespace rtti_test_lib

// TOML shares the same naming policy surface; gate it on its own feature flag
// so a build with JSON/XML/INI on but TOML off still compiles cleanly.
#if (!defined(RTTI_ENABLE_TOML) || RTTI_ENABLE_TOML)

#include <rtti/toml.hpp>

namespace rtti_test_lib {

RTTI_TEST_CASE(naming_skip_empty_toml) {
    rtti::toml::write_config ct;
    ct.skip_empty = true;
    const std::string t = rtti::toml::serialize(NamingSkip{}, ct);
    RTTI_CHECK(t.find("empty") == std::string::npos);
    RTTI_CHECK(t.find("vec") == std::string::npos);
    RTTI_CHECK(t.find("a = 1") != std::string::npos);
    const std::string to = rtti::toml::serialize(NamingOpt{}, ct);
    RTTI_CHECK(to.find("opt") == std::string::npos);
    RTTI_CHECK(to.find("present") != std::string::npos);
    const std::string tb = rtti::toml::serialize(NamingBool{}, ct);
    RTTI_CHECK(tb.find("active") == std::string::npos);
    RTTI_CHECK(tb.find("ok") != std::string::npos);
}

RTTI_TEST_CASE(naming_rename_map_toml) {
    std::map<std::string, std::string> rm;
    rm["a"] = "alpha";
    rtti::toml::write_config ct;
    ct.rename_map = &rm;
    const std::string t = rtti::toml::serialize(NamingRm{7}, ct);
    RTTI_CHECK(t.find("alpha") != std::string::npos);
    const auto b = rtti::toml::parse_as<NamingRm>(t, ct);
    RTTI_CHECK(b.ok());
    if (b.ok()) {
        RTTI_CHECK_EQ(b.value.a, 7);
    }
}

RTTI_TEST_CASE(naming_skip_empty_nested_toml) {
    NamingOuter o;
    o.direct.innerField = 0;
    o.vec.push_back(NamingInner{0});
    rtti::toml::write_config ct;
    ct.skip_empty = true;
    const std::string t = rtti::toml::serialize(o, ct);
    RTTI_CHECK(t.find("innerField") == std::string::npos);  // zero dropped
    RTTI_CHECK(t.find("direct") != std::string::npos);
}

} // namespace rtti_test_lib

#endif

#endif