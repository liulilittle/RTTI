// Native C-style static-array support (spec §12, Task G): is_c_array /
// is_md_array traits, carray_to/carray_from converters with strict known-extent
// validation (no truncation, no overrun), char[N] string-field handling, and
// three/four-format round-trips for int[5], int[2][3], int[2][2][2] plus
// nested std::array. Bounds/dimension mismatches return kind_mismatch and
// never crash (spec §0).

#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include "box.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace rtti_test_lib {

// --- Reflected holders (native C array members) ------------------------------

struct StaticArray1D { int a[5]; };
struct StaticArray2D { int m[2][3]; };
struct StaticArray3D { int c[2][2][2]; };
struct StaticArrayCharField { char name[32]; };
struct StaticArrayNested { std::array<std::array<int, 3>, 2> g; };
struct StaticArrayMixed { int vec[3]; double mat[2][2]; };

// Element-wise comparison for native C arrays (arrays are not ==-comparable).
template <typename T, std::size_t N>
static bool c_array_eq(const T (&a)[N], const T (&b)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if constexpr (std::is_array<T>::value) {
            if (!c_array_eq(a[i], b[i])) {
                return false;
            }
        } else {
            if (a[i] != b[i]) {
                return false;
            }
        }
    }
    return true;
}

} // namespace rtti_test_lib

RTTI_DEFINE_TYPE(rtti_test_lib::StaticArray1D, a)
RTTI_DEFINE_TYPE(rtti_test_lib::StaticArray2D, m)
RTTI_DEFINE_TYPE(rtti_test_lib::StaticArray3D, c)
RTTI_DEFINE_TYPE(rtti_test_lib::StaticArrayCharField, name)
RTTI_DEFINE_TYPE(rtti_test_lib::StaticArrayNested, g)
RTTI_DEFINE_TYPE(rtti_test_lib::StaticArrayMixed, vec, mat)

namespace rtti_test_lib {
using rtti::error_code;

// --- Compile-time shape traits ----------------------------------------------

RTTI_TEST_CASE(StaticArrayTraits){
    static_assert(rtti::is_c_array<int[5]>::value, "int[5] is a C array");
    static_assert(rtti::is_c_array<int[2][3]>::value, "int[2][3] is a C array");
    static_assert(rtti::is_c_array<int[2][2][2]>::value, "");
    static_assert(!rtti::is_c_array<char[32]>::value, "char[N] is a string field");
    static_assert(!rtti::is_c_array<std::array<int, 3>>::value, "std::array is not native");
    static_assert(!rtti::is_c_array<std::vector<int>>::value, "");
    static_assert(rtti::is_md_array<int[2][3]>::value, "");
    static_assert(rtti::is_md_array<int[2][2][2]>::value, "");
    static_assert(!rtti::is_md_array<int[5]>::value, "");
    RTTI_CHECK(true);
}

// Compile-time dimension metadata: rank and per-dimension extents.
RTTI_TEST_CASE(StaticArrayDimensionTraits){
    constexpr size_t e0 = rtti::carray_extent<int[2][3], 0>();
    constexpr size_t e1 = rtti::carray_extent<int[2][3], 1>();
    constexpr size_t e2 = rtti::carray_extent<int[2][2][2], 2>();
    RTTI_CHECK_EQ(rtti::carray_rank<int[2][3]>(), size_t(2));
    RTTI_CHECK_EQ(e0, size_t(2));
    RTTI_CHECK_EQ(e1, size_t(3));
    RTTI_CHECK_EQ(rtti::carray_rank<int[5]>(), size_t(1));
    RTTI_CHECK_EQ(e2, size_t(2));
}

// --- Core value_io tree round-trips -----------------------------------------

RTTI_TEST_CASE(StaticArrayCore1D){
    int in[5] = {1, 2, 3, 4, 5};
    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, in), error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(dst->size(), size_t(5));
    int out[5] = {};
    RTTI_CHECK_EQ(rtti::from_value(dst, out), error_code::ok);
    RTTI_CHECK(c_array_eq(in, out));
    delete dst;
}

RTTI_TEST_CASE(StaticArrayCore2DNested){
    int in[2][3] = {{1, 2, 3}, {4, 5, 6}};
    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, in), error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(dst->size(), size_t(2));
    RTTI_CHECK(dst->at(0)->type() == rtti::value_io::kind::array);
    int out[2][3] = {};
    RTTI_CHECK_EQ(rtti::from_value(dst, out), error_code::ok);
    RTTI_CHECK(c_array_eq(in, out));
    delete dst;
}

RTTI_TEST_CASE(StaticArrayCore2DFlatRead){
    box* src = new box;
    src->make_array();
    for (int i = 1; i <= 6; ++i) {
        src->push_back(make_int(i));
    }
    int out[2][3] = {};
    RTTI_CHECK_EQ(rtti::from_value(src, out), error_code::ok);
    RTTI_CHECK_EQ(out[0][0], 1);
    RTTI_CHECK_EQ(out[0][2], 3);
    RTTI_CHECK_EQ(out[1][2], 6);
    delete src;
}

RTTI_TEST_CASE(StaticArrayCore3D){
    int in[2][2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, in), error_code::ok);
    RTTI_CHECK_EQ(dst->size(), size_t(2));
    int out[2][2][2] = {};
    RTTI_CHECK_EQ(rtti::from_value(dst, out), error_code::ok);
    RTTI_CHECK(c_array_eq(in, out));
    delete dst;
}

// --- Bounds / dimension-mismatch safety probes (never crash) -----------------

RTTI_TEST_CASE(StaticArrayBoundsRejected){
    // Too many elements: strict known-extent check, no truncation, no write.
    {
        int out[5] = {9, 9, 9, 9, 9};
        box* src = new box;
        src->make_array();
        for (int i = 0; i < 6; ++i) {
            src->push_back(make_int(i));
        }
        RTTI_CHECK_EQ(rtti::from_value(src, out), error_code::kind_mismatch);
        RTTI_CHECK_EQ(out[0], 9);  // untouched: rejected before any write
        delete src;
    }
    // Too few elements.
    {
        int out[5] = {};
        box* src = new box;
        src->make_array();
        src->push_back(make_int(1));
        RTTI_CHECK_EQ(rtti::from_value(src, out), error_code::kind_mismatch);
        delete src;
    }
    // Non-array source.
    {
        int out[5] = {};
        box* src = new box;
        src->make_scalar(rtti::field_value::of(int64_t(7)));
        RTTI_CHECK_EQ(rtti::from_value(src, out), error_code::kind_mismatch);
        delete src;
    }
    // Null source / sink.
    {
        int out[5] = {};
        RTTI_CHECK_EQ(rtti::from_value<int[5]>(nullptr, out), error_code::no_such_field);
        int in[5] = {};
        RTTI_CHECK_EQ(rtti::to_value<int[5]>(nullptr, in), error_code::no_such_field);
    }
    // 2D flat list with the wrong total size.
    {
        int out[2][3] = {};
        box* src = new box;
        src->make_array();
        for (int i = 0; i < 5; ++i) {
            src->push_back(make_int(i));
        }
        RTTI_CHECK_EQ(rtti::from_value(src, out), error_code::kind_mismatch);
        delete src;
    }
    // 2D nested with an inner row of the wrong width.
    {
        int out[2][3] = {};
        box* src = new box;
        src->make_array();
        box* r0 = new box;
        r0->make_array();
        r0->push_back(make_int(1));
        r0->push_back(make_int(2));
        box* r1 = new box;
        r1->make_array();
        r1->push_back(make_int(3));
        src->push_back(r0);
        src->push_back(r1);
        RTTI_CHECK_EQ(rtti::from_value(src, out), error_code::kind_mismatch);
        delete src;
    }
}

} // namespace rtti_test_lib

// Format adapter round-trips (JSON/XML/INI/TOML). Gated so a stripped build
// (all RTTI_ENABLE_* = 0) skips this section without tripping the format
// headers' #error guards.
#if (!defined(RTTI_ENABLE_JSON) || RTTI_ENABLE_JSON) && \
    (!defined(RTTI_ENABLE_XML) || RTTI_ENABLE_XML) && \
    (!defined(RTTI_ENABLE_INI) || RTTI_ENABLE_INI) && \
    (!defined(RTTI_ENABLE_TOML) || RTTI_ENABLE_TOML)

#include <rtti/ini.hpp>
#include <rtti/json.hpp>
#include <rtti/toml.hpp>
#include <rtti/xml.hpp>

namespace rtti_test_lib {
using rtti::error_code;

// --- Format adapter round-trips ---------------------------------------------

RTTI_TEST_CASE(StaticArray1DFormatRoundtrips){
    StaticArray1D src;
    for (int i = 0; i < 5; ++i) {
        src.a[i] = i * 10;
    }
    auto jb = rtti::json::parse_as<StaticArray1D>(rtti::json::serialize(src));
    RTTI_CHECK(jb.ok());
    if (jb.ok()) {
        RTTI_CHECK(c_array_eq(src.a, jb.value.a));
    }
    auto xb = rtti::xml::parse_as<StaticArray1D>(rtti::xml::serialize(src));
    RTTI_CHECK(xb.ok());
    if (xb.ok()) {
        RTTI_CHECK(c_array_eq(src.a, xb.value.a));
    }
    auto ib = rtti::ini::parse_as<StaticArray1D>(rtti::ini::serialize(src));
    RTTI_CHECK(ib.ok());
    if (ib.ok()) {
        RTTI_CHECK(c_array_eq(src.a, ib.value.a));
    }
    auto tb = rtti::toml::parse_as<StaticArray1D>(rtti::toml::serialize(src));
    RTTI_CHECK(tb.ok());
    if (tb.ok()) {
        RTTI_CHECK(c_array_eq(src.a, tb.value.a));
    }
}

RTTI_TEST_CASE(StaticArray2DFormatRoundtrips){
    StaticArray2D src;
    src.m[0][0] = 1; src.m[0][1] = 2; src.m[0][2] = 3;
    src.m[1][0] = 4; src.m[1][1] = 5; src.m[1][2] = 6;
    auto jb = rtti::json::parse_as<StaticArray2D>(rtti::json::serialize(src));
    RTTI_CHECK(jb.ok());
    if (jb.ok()) {
        RTTI_CHECK(c_array_eq(src.m, jb.value.m));
    }
    auto tb = rtti::toml::parse_as<StaticArray2D>(rtti::toml::serialize(src));
    RTTI_CHECK(tb.ok());
    if (tb.ok()) {
        RTTI_CHECK(c_array_eq(src.m, tb.value.m));
    }
}

// JSON/TOML naturally express nested arrays (verified above). The XML/INI
// adapters serialize an array as flat repeated elements/keys and cannot
// re-read a nested ([[..],[..]]) value -- a pre-existing adapter limitation
// (a nested std::array fails identically). These probes pin the concrete
// current behavior: safe kind_mismatch, never a crash (spec §0).
RTTI_TEST_CASE(StaticArray2DNestedAdaptersSafe){
    StaticArray2D src;
    src.m[0][0] = 1; src.m[0][1] = 2; src.m[0][2] = 3;
    src.m[1][0] = 4; src.m[1][1] = 5; src.m[1][2] = 6;
    auto xb = rtti::xml::parse_as<StaticArray2D>(rtti::xml::serialize(src));
    RTTI_CHECK_EQ(xb.err.code, error_code::kind_mismatch);
    auto ib = rtti::ini::parse_as<StaticArray2D>(rtti::ini::serialize(src));
    RTTI_CHECK_EQ(ib.err.code, error_code::kind_mismatch);
    auto xb3 = rtti::xml::parse_as<StaticArray3D>(rtti::xml::serialize(StaticArray3D{}));
    RTTI_CHECK_EQ(xb3.err.code, error_code::kind_mismatch);
}

RTTI_TEST_CASE(StaticArray2DFlatJSON){
    // int[2][3] read from a flat 6-element JSON array (spec §12.3 minimal form).
    auto b = rtti::json::parse_as<StaticArray2D>("{\"m\":[1,2,3,4,5,6]}");
    RTTI_CHECK(b.ok());
    if (b.ok()) {
        RTTI_CHECK_EQ(b.value.m[0][0], 1);
        RTTI_CHECK_EQ(b.value.m[1][2], 6);
    }
}

RTTI_TEST_CASE(StaticArray2DWrongShapeRejected){
    auto a = rtti::json::parse_as<StaticArray2D>("{\"m\":[1,2,3,4,5]}");
    RTTI_CHECK_EQ(a.err.code, error_code::kind_mismatch);
    auto b = rtti::json::parse_as<StaticArray2D>("{\"m\":[[1,2],[3,4,5,6]]}");
    RTTI_CHECK_EQ(b.err.code, error_code::kind_mismatch);
}

RTTI_TEST_CASE(StaticArray3DFormatRoundtrips){
    StaticArray3D src;
    src.c[0][0][0] = 1; src.c[0][1][1] = 8;
    src.c[1][0][1] = 6; src.c[1][1][0] = 7;
    auto jb = rtti::json::parse_as<StaticArray3D>(rtti::json::serialize(src));
    RTTI_CHECK(jb.ok());
    if (jb.ok()) {
        RTTI_CHECK(c_array_eq(src.c, jb.value.c));
    }
    auto tb = rtti::toml::parse_as<StaticArray3D>(rtti::toml::serialize(src));
    RTTI_CHECK(tb.ok());
    if (tb.ok()) {
        RTTI_CHECK(c_array_eq(src.c, tb.value.c));
    }
}

// --- nested std::array multidimensional --------------------------------------

RTTI_TEST_CASE(StaticArrayNestedStdArray){
    StaticArrayNested src;
    src.g[0] = {1, 2, 3};
    src.g[1] = {4, 5, 6};
    auto jb = rtti::json::parse_as<StaticArrayNested>(rtti::json::serialize(src));
    RTTI_CHECK(jb.ok());
    if (jb.ok()) {
        RTTI_CHECK_EQ(jb.value.g[1][2], 6);
    }
    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, src), error_code::ok);
    StaticArrayNested out;
    RTTI_CHECK_EQ(rtti::from_value(dst, out), error_code::ok);
    RTTI_CHECK_EQ(out.g[0][0], 1);
    RTTI_CHECK_EQ(out.g[1][2], 6);
    delete dst;
}

// --- char[N] is a string field, not a numeric array ---------------------------

RTTI_TEST_CASE(StaticArrayCharFieldIsString){
    StaticArrayCharField src;
    std::memset(src.name, 0, sizeof(src.name));
    std::memcpy(src.name, "hello", 6);
    const std::string j = rtti::json::serialize(src);
    RTTI_CHECK(j.find("\"hello\"") != std::string::npos);
    RTTI_CHECK(j.find("[104,101") == std::string::npos);
    auto back = rtti::json::parse_as<StaticArrayCharField>(j);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK_EQ(std::string(back.value.name), std::string("hello"));
    }
    auto xb = rtti::xml::parse_as<StaticArrayCharField>(rtti::xml::serialize(src));
    RTTI_CHECK(xb.ok());
    if (xb.ok()) {
        RTTI_CHECK_EQ(std::string(xb.value.name), std::string("hello"));
    }
    auto ib = rtti::ini::parse_as<StaticArrayCharField>(rtti::ini::serialize(src));
    RTTI_CHECK(ib.ok());
    if (ib.ok()) {
        RTTI_CHECK_EQ(std::string(ib.value.name), std::string("hello"));
    }
}

RTTI_TEST_CASE(StaticArrayCharFieldOverflowRejected){
    auto back = rtti::json::parse_as<StaticArrayCharField>(
        "{\"name\":\"this string is far too long for a thirty-two byte buffer x\"}");
    RTTI_CHECK(!back.ok());
    RTTI_CHECK_EQ(back.err.code, error_code::kind_mismatch);
}

// --- Mixed scalar + native array fields --------------------------------------

RTTI_TEST_CASE(StaticArrayMixedFormatRoundtrips){
    StaticArrayMixed src;
    src.vec[0] = 1; src.vec[1] = 2; src.vec[2] = 3;
    src.mat[0][0] = 1.5; src.mat[0][1] = 2.5;
    src.mat[1][0] = 3.5; src.mat[1][1] = 4.5;
    auto jb = rtti::json::parse_as<StaticArrayMixed>(rtti::json::serialize(src));
    RTTI_CHECK(jb.ok());
    if (jb.ok()) {
        RTTI_CHECK_EQ(jb.value.vec[2], 3);
        RTTI_CHECK_EQ(jb.value.mat[1][1], 4.5);
    }
    auto tb = rtti::toml::parse_as<StaticArrayMixed>(rtti::toml::serialize(src));
    RTTI_CHECK(tb.ok());
    if (tb.ok()) {
        RTTI_CHECK_EQ(tb.value.vec[0], 1);
        RTTI_CHECK_EQ(tb.value.mat[0][1], 2.5);
    }
}

// --- named-conversion path (to_value_named / from_value_named) --------------

RTTI_TEST_CASE(StaticArrayNamedPath){
    StaticArray1D src;
    for (int i = 0; i < 5; ++i) {
        src.a[i] = i + 1;
    }
    rtti::json::write_config cfg;
    cfg.key_case = rtti::json::key_case::snake;
    const std::string j = rtti::json::serialize(src, cfg);
    auto back = rtti::json::parse_as<StaticArray1D>(j, cfg);
    RTTI_CHECK(back.ok());
    if (back.ok()) {
        RTTI_CHECK(c_array_eq(src.a, back.value.a));
    }
}

} // namespace rtti_test_lib

#endif