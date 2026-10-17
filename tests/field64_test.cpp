// Field-registration macro-ladder capacity tests: the RTTI_NARG-driven
// registration macros must support up to 64 items (fields, enum enumerators,
// constructor parameters, template arguments, attribute entries, field
// attribute entries) and single-attribute argument lists. The ladders were
// extended from 16 to 64 in include/rtti/reflection.hpp (see the RTTI_NARG
// comment there: more than 64 arguments is a hard compile error -- the 65th
// token is taken as the count).
//
// Coverage:
//   * 64 external fields: count, name order, desc table, per-field get/set,
//     type-level and instance-level traversal, forward/reverse iterators;
//   * 64 intrusive fields (one of them private);
//   * RTTI_DEFINE_TYPE_WITH_DEFAULT over 64 fields (has_default trait);
//   * RTTI_DEFINE_TYPE_DERIVED merging a 64-field base with 3 own fields
//     (base-first order, 67 total);
//   * 64 enumerators (RTTI_DEFINE_ENUM);
//   * a 64-parameter constructor registry (RTTI_DEFINE_CTOR, runtime factory
//     construction through make_object);
//   * 64 template arguments (RTTI_TEMPLATE_INFO) and a 17-argument list with a
//     paren-wrapped element in a non-first position;
//   * a 64-entry type-attribute table including one attribute with 64
//     arguments, and a 64-entry field-attribute table;
//   * JSON/INI/XML/TOML/protobuf/FlatBuffers round-trips over the 64-field
//     type (each feature-gated) and missing-key default preservation for the
//     WITH_DEFAULT type;
//   * concurrent type_of/fields/for_each_field materialization of a 64-field
//     type (the concurrency_test.cpp pattern);
//   * allocation balance over repeated 64-field reflection calls
//     (the leak_counter.hpp pattern).
//
// The 65-field and empty-list boundary probes live outside the suite (see
// .opencode/exec-64-log.md); the empty-list behaviour itself stays covered by
// the existing zero-field tests. English comments only.

#include "test_harness.hpp"
#include "leak_counter.hpp"

#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#if RTTI_ENABLE_JSON
#include <rtti/json.hpp>
#endif
#if RTTI_ENABLE_INI
#include <rtti/ini.hpp>
#endif
#if RTTI_ENABLE_XML
#include <rtti/xml.hpp>
#endif
#if RTTI_ENABLE_TOML
#include <rtti/toml.hpp>
#endif
#if RTTI_ENABLE_PROTO
#include <rtti/proto.hpp>
#endif
#if RTTI_ENABLE_FLAT
#include <rtti/flat.hpp>
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// 64 distinct empty argument types for the template-argument registrations.
// They live at global scope because the RTTI_TEMPLATE_INFO registration below
// spells them as bare identifiers.
struct F64T01 {}; struct F64T02 {}; struct F64T03 {}; struct F64T04 {};
struct F64T05 {}; struct F64T06 {}; struct F64T07 {}; struct F64T08 {};
struct F64T09 {}; struct F64T10 {}; struct F64T11 {}; struct F64T12 {};
struct F64T13 {}; struct F64T14 {}; struct F64T15 {}; struct F64T16 {};
struct F64T17 {}; struct F64T18 {}; struct F64T19 {}; struct F64T20 {};
struct F64T21 {}; struct F64T22 {}; struct F64T23 {}; struct F64T24 {};
struct F64T25 {}; struct F64T26 {}; struct F64T27 {}; struct F64T28 {};
struct F64T29 {}; struct F64T30 {}; struct F64T31 {}; struct F64T32 {};
struct F64T33 {}; struct F64T34 {}; struct F64T35 {}; struct F64T36 {};
struct F64T37 {}; struct F64T38 {}; struct F64T39 {}; struct F64T40 {};
struct F64T41 {}; struct F64T42 {}; struct F64T43 {}; struct F64T44 {};
struct F64T45 {}; struct F64T46 {}; struct F64T47 {}; struct F64T48 {};
struct F64T49 {}; struct F64T50 {}; struct F64T51 {}; struct F64T52 {};
struct F64T53 {}; struct F64T54 {}; struct F64T55 {}; struct F64T56 {};
struct F64T57 {}; struct F64T58 {}; struct F64T59 {}; struct F64T60 {};
struct F64T61 {}; struct F64T62 {}; struct F64T63 {}; struct F64T64 {};

namespace rtti_test_lib {

// 64 external fields with distinguishable default values.
struct Field64Big {
    int f01 = 1;  int f02 = 2;  int f03 = 3;  int f04 = 4;
    int f05 = 5;  int f06 = 6;  int f07 = 7;  int f08 = 8;
    int f09 = 9;  int f10 = 10; int f11 = 11; int f12 = 12;
    int f13 = 13; int f14 = 14; int f15 = 15; int f16 = 16;
    int f17 = 17; int f18 = 18; int f19 = 19; int f20 = 20;
    int f21 = 21; int f22 = 22; int f23 = 23; int f24 = 24;
    int f25 = 25; int f26 = 26; int f27 = 27; int f28 = 28;
    int f29 = 29; int f30 = 30; int f31 = 31; int f32 = 32;
    int f33 = 33; int f34 = 34; int f35 = 35; int f36 = 36;
    int f37 = 37; int f38 = 38; int f39 = 39; int f40 = 40;
    int f41 = 41; int f42 = 42; int f43 = 43; int f44 = 44;
    int f45 = 45; int f46 = 46; int f47 = 47; int f48 = 48;
    int f49 = 49; int f50 = 50; int f51 = 51; int f52 = 52;
    int f53 = 53; int f54 = 54; int f55 = 55; int f56 = 56;
    int f57 = 57; int f58 = 58; int f59 = 59; int f60 = 60;
    int f61 = 61; int f62 = 62; int f63 = 63; int f64 = 64;
};

// 64 fields registered with the default-preserving variant.
struct Field64WithDefault {
    int d01 = 101; int d02 = 102; int d03 = 103; int d04 = 104;
    int d05 = 105; int d06 = 106; int d07 = 107; int d08 = 108;
    int d09 = 109; int d10 = 110; int d11 = 111; int d12 = 112;
    int d13 = 113; int d14 = 114; int d15 = 115; int d16 = 116;
    int d17 = 117; int d18 = 118; int d19 = 119; int d20 = 120;
    int d21 = 121; int d22 = 122; int d23 = 123; int d24 = 124;
    int d25 = 125; int d26 = 126; int d27 = 127; int d28 = 128;
    int d29 = 129; int d30 = 130; int d31 = 131; int d32 = 132;
    int d33 = 133; int d34 = 134; int d35 = 135; int d36 = 136;
    int d37 = 137; int d38 = 138; int d39 = 139; int d40 = 140;
    int d41 = 141; int d42 = 142; int d43 = 143; int d44 = 144;
    int d45 = 145; int d46 = 146; int d47 = 147; int d48 = 148;
    int d49 = 149; int d50 = 150; int d51 = 151; int d52 = 152;
    int d53 = 153; int d54 = 154; int d55 = 155; int d56 = 156;
    int d57 = 157; int d58 = 158; int d59 = 159; int d60 = 160;
    int d61 = 161; int d62 = 162; int d63 = 163; int d64 = 164;
};

// 64 intrusive fields; the last one is private to cover access levels.
struct Field64Intrusive {
    int g01 = 1;  int g02 = 2;  int g03 = 3;  int g04 = 4;
    int g05 = 5;  int g06 = 6;  int g07 = 7;  int g08 = 8;
    int g09 = 9;  int g10 = 10; int g11 = 11; int g12 = 12;
    int g13 = 13; int g14 = 14; int g15 = 15; int g16 = 16;
    int g17 = 17; int g18 = 18; int g19 = 19; int g20 = 20;
    int g21 = 21; int g22 = 22; int g23 = 23; int g24 = 24;
    int g25 = 25; int g26 = 26; int g27 = 27; int g28 = 28;
    int g29 = 29; int g30 = 30; int g31 = 31; int g32 = 32;
    int g33 = 33; int g34 = 34; int g35 = 35; int g36 = 36;
    int g37 = 37; int g38 = 38; int g39 = 39; int g40 = 40;
    int g41 = 41; int g42 = 42; int g43 = 43; int g44 = 44;
    int g45 = 45; int g46 = 46; int g47 = 47; int g48 = 48;
    int g49 = 49; int g50 = 50; int g51 = 51; int g52 = 52;
    int g53 = 53; int g54 = 54; int g55 = 55; int g56 = 56;
    int g57 = 57; int g58 = 58; int g59 = 59; int g60 = 60;
    int g61 = 61; int g62 = 62; int g63 = 63;
private:
    int g64 = 64;
public:
    RTTI_DEFINE_TYPE_INTRUSIVE(Field64Intrusive,
        g01, g02, g03, g04, g05, g06, g07, g08, g09, g10, g11, g12, g13,
        g14, g15, g16, g17, g18, g19, g20, g21, g22, g23, g24, g25, g26,
        g27, g28, g29, g30, g31, g32, g33, g34, g35, g36, g37, g38, g39,
        g40, g41, g42, g43, g44, g45, g46, g47, g48, g49, g50, g51, g52,
        g53, g54, g55, g56, g57, g58, g59, g60, g61, g62, g63, g64)
};

// 64-field base plus 3 own fields: the merged table has 67 entries.
struct Field64Derived : Field64Big {
    int extra1 = 0;
    int extra2 = 0;
    int extra3 = 0;
};

// A second 64-field type for the concurrent-materialization stress (distinct
// name keeps the descriptor registry entry separate from Field64Big).
struct Field64Concur {
    int c01 = 0; int c02 = 0; int c03 = 0; int c04 = 0;
    int c05 = 0; int c06 = 0; int c07 = 0; int c08 = 0;
    int c09 = 0; int c10 = 0; int c11 = 0; int c12 = 0;
    int c13 = 0; int c14 = 0; int c15 = 0; int c16 = 0;
    int c17 = 0; int c18 = 0; int c19 = 0; int c20 = 0;
    int c21 = 0; int c22 = 0; int c23 = 0; int c24 = 0;
    int c25 = 0; int c26 = 0; int c27 = 0; int c28 = 0;
    int c29 = 0; int c30 = 0; int c31 = 0; int c32 = 0;
    int c33 = 0; int c34 = 0; int c35 = 0; int c36 = 0;
    int c37 = 0; int c38 = 0; int c39 = 0; int c40 = 0;
    int c41 = 0; int c42 = 0; int c43 = 0; int c44 = 0;
    int c45 = 0; int c46 = 0; int c47 = 0; int c48 = 0;
    int c49 = 0; int c50 = 0; int c51 = 0; int c52 = 0;
    int c53 = 0; int c54 = 0; int c55 = 0; int c56 = 0;
    int c57 = 0; int c58 = 0; int c59 = 0; int c60 = 0;
    int c61 = 0; int c62 = 0; int c63 = 0; int c64 = 0;
};

// 64 enumerators.
enum class Field64Enum : int {
    e01 = 0,  e02 = 1,  e03 = 2,  e04 = 3,  e05 = 4,  e06 = 5,  e07 = 6,
    e08 = 7,  e09 = 8,  e10 = 9,  e11 = 10, e12 = 11, e13 = 12, e14 = 13,
    e15 = 14, e16 = 15, e17 = 16, e18 = 17, e19 = 18, e20 = 19, e21 = 20,
    e22 = 21, e23 = 22, e24 = 23, e25 = 24, e26 = 25, e27 = 26, e28 = 27,
    e29 = 28, e30 = 29, e31 = 30, e32 = 31, e33 = 32, e34 = 33, e35 = 34,
    e36 = 35, e37 = 36, e38 = 37, e39 = 38, e40 = 39, e41 = 40, e42 = 41,
    e43 = 42, e44 = 43, e45 = 44, e46 = 45, e47 = 46, e48 = 47, e49 = 48,
    e50 = 49, e51 = 50, e52 = 51, e53 = 52, e54 = 53, e55 = 54, e56 = 55,
    e57 = 56, e58 = 57, e59 = 58, e60 = 59, e61 = 60, e62 = 61, e63 = 62,
    e64 = 63,
};

// 64-parameter constructor target: a variadic constructor folds every injected
// parameter into one sum, so the factory result is easy to verify.
struct Field64Ctor {
    int sum = 0;
    Field64Ctor() = default;
    template <typename... Ts>
    explicit Field64Ctor(Ts... vs)
        : sum(static_cast<int>((0 + ... + static_cast<int>(vs)))) {}
};

// Template declarations used by the RTTI_TEMPLATE_INFO registrations.
template <typename...>
struct Field64Wide;

template <typename...>
struct Field64Mix;

// Type with a 64-entry attribute table (one entry carries 64 arguments).
struct Field64AttrHost {
    int v = 0;
};

} // namespace rtti_test_lib

// Non-intrusive registrations must sit at global scope (explicit
// specialization rules). Field list: 64 names, one per line group.
RTTI_DEFINE_TYPE(rtti_test_lib::Field64Big,
    f01, f02, f03, f04, f05, f06, f07, f08, f09, f10, f11, f12, f13, f14, f15, f16,
    f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32,
    f33, f34, f35, f36, f37, f38, f39, f40, f41, f42, f43, f44, f45, f46, f47, f48,
    f49, f50, f51, f52, f53, f54, f55, f56, f57, f58, f59, f60, f61, f62, f63, f64)

RTTI_DEFINE_TYPE_WITH_DEFAULT(rtti_test_lib::Field64WithDefault,
    d01, d02, d03, d04, d05, d06, d07, d08, d09, d10, d11, d12, d13, d14, d15, d16,
    d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31, d32,
    d33, d34, d35, d36, d37, d38, d39, d40, d41, d42, d43, d44, d45, d46, d47, d48,
    d49, d50, d51, d52, d53, d54, d55, d56, d57, d58, d59, d60, d61, d62, d63, d64)

RTTI_DEFINE_TYPE_DERIVED(rtti_test_lib::Field64Derived,
                         rtti_test_lib::Field64Big, extra1, extra2, extra3)

RTTI_DEFINE_TYPE(rtti_test_lib::Field64Concur,
    c01, c02, c03, c04, c05, c06, c07, c08, c09, c10, c11, c12, c13, c14, c15, c16,
    c17, c18, c19, c20, c21, c22, c23, c24, c25, c26, c27, c28, c29, c30, c31, c32,
    c33, c34, c35, c36, c37, c38, c39, c40, c41, c42, c43, c44, c45, c46, c47, c48,
    c49, c50, c51, c52, c53, c54, c55, c56, c57, c58, c59, c60, c61, c62, c63, c64)

RTTI_DEFINE_ENUM(rtti_test_lib::Field64Enum,
    e01, e02, e03, e04, e05, e06, e07, e08, e09, e10, e11, e12, e13, e14, e15, e16,
    e17, e18, e19, e20, e21, e22, e23, e24, e25, e26, e27, e28, e29, e30, e31, e32,
    e33, e34, e35, e36, e37, e38, e39, e40, e41, e42, e43, e44, e45, e46, e47, e48,
    e49, e50, e51, e52, e53, e54, e55, e56, e57, e58, e59, e60, e61, e62, e63, e64)

RTTI_DEFINE_CTOR(rtti_test_lib::Field64Ctor,
    ("p01", int, false), ("p02", int, false), ("p03", int, false), ("p04", int, false),
    ("p05", int, false), ("p06", int, false), ("p07", int, false), ("p08", int, false),
    ("p09", int, false), ("p10", int, false), ("p11", int, false), ("p12", int, false),
    ("p13", int, false), ("p14", int, false), ("p15", int, false), ("p16", int, false),
    ("p17", int, false), ("p18", int, false), ("p19", int, false), ("p20", int, false),
    ("p21", int, false), ("p22", int, false), ("p23", int, false), ("p24", int, false),
    ("p25", int, false), ("p26", int, false), ("p27", int, false), ("p28", int, false),
    ("p29", int, false), ("p30", int, false), ("p31", int, false), ("p32", int, false),
    ("p33", int, false), ("p34", int, false), ("p35", int, false), ("p36", int, false),
    ("p37", int, false), ("p38", int, false), ("p39", int, false), ("p40", int, false),
    ("p41", int, false), ("p42", int, false), ("p43", int, false), ("p44", int, false),
    ("p45", int, false), ("p46", int, false), ("p47", int, false), ("p48", int, false),
    ("p49", int, false), ("p50", int, false), ("p51", int, false), ("p52", int, false),
    ("p53", int, false), ("p54", int, false), ("p55", int, false), ("p56", int, false),
    ("p57", int, false), ("p58", int, false), ("p59", int, false), ("p60", int, false),
    ("p61", int, false), ("p62", int, false), ("p63", int, false), ("p64", int, false))

RTTI_TEMPLATE_INFO(rtti_test_lib::Field64Wide,
    F64T01, F64T02, F64T03, F64T04, F64T05, F64T06, F64T07, F64T08,
    F64T09, F64T10, F64T11, F64T12, F64T13, F64T14, F64T15, F64T16,
    F64T17, F64T18, F64T19, F64T20, F64T21, F64T22, F64T23, F64T24,
    F64T25, F64T26, F64T27, F64T28, F64T29, F64T30, F64T31, F64T32,
    F64T33, F64T34, F64T35, F64T36, F64T37, F64T38, F64T39, F64T40,
    F64T41, F64T42, F64T43, F64T44, F64T45, F64T46, F64T47, F64T48,
    F64T49, F64T50, F64T51, F64T52, F64T53, F64T54, F64T55, F64T56,
    F64T57, F64T58, F64T59, F64T60, F64T61, F64T62, F64T63, F64T64)

// 17 arguments with a paren-wrapped comma-bearing element in position 1.
RTTI_TEMPLATE_INFO(rtti_test_lib::Field64Mix,
    F64T01, (std::pair<int, int>), F64T03, F64T04, F64T05, F64T06, F64T07,
    F64T08, F64T09, F64T10, F64T11, F64T12, F64T13, F64T14, F64T15, F64T16,
    F64T17)

RTTI_DEFINE_TYPE(rtti_test_lib::Field64AttrHost, v)

RTTI_DEFINE_TYPE_ATTRS(rtti_test_lib::Field64AttrHost,
    RTTI_ATTR("k01", 1), RTTI_ATTR("k02", 2), RTTI_ATTR("k03", 3),
    RTTI_ATTR("k04", 4), RTTI_ATTR("k05", 5), RTTI_ATTR("k06", 6),
    RTTI_ATTR("k07", 7), RTTI_ATTR("k08", 8), RTTI_ATTR("k09", 9),
    RTTI_ATTR("k10", 10), RTTI_ATTR("k11", 11), RTTI_ATTR("k12", 12),
    RTTI_ATTR("k13", 13), RTTI_ATTR("k14", 14), RTTI_ATTR("k15", 15),
    RTTI_ATTR("k16", 16), RTTI_ATTR("k17", 17), RTTI_ATTR("k18", 18),
    RTTI_ATTR("k19", 19), RTTI_ATTR("k20", 20), RTTI_ATTR("k21", 21),
    RTTI_ATTR("k22", 22), RTTI_ATTR("k23", 23), RTTI_ATTR("k24", 24),
    RTTI_ATTR("k25", 25), RTTI_ATTR("k26", 26), RTTI_ATTR("k27", 27),
    RTTI_ATTR("k28", 28), RTTI_ATTR("k29", 29), RTTI_ATTR("k30", 30),
    RTTI_ATTR("k31", 31), RTTI_ATTR("k32", 32), RTTI_ATTR("k33", 33),
    RTTI_ATTR("k34", 34), RTTI_ATTR("k35", 35), RTTI_ATTR("k36", 36),
    RTTI_ATTR("k37", 37), RTTI_ATTR("k38", 38), RTTI_ATTR("k39", 39),
    RTTI_ATTR("k40", 40), RTTI_ATTR("k41", 41), RTTI_ATTR("k42", 42),
    RTTI_ATTR("k43", 43), RTTI_ATTR("k44", 44), RTTI_ATTR("k45", 45),
    RTTI_ATTR("k46", 46), RTTI_ATTR("k47", 47), RTTI_ATTR("k48", 48),
    RTTI_ATTR("k49", 49), RTTI_ATTR("k50", 50), RTTI_ATTR("k51", 51),
    RTTI_ATTR("k52", 52), RTTI_ATTR("k53", 53), RTTI_ATTR("k54", 54),
    RTTI_ATTR("k55", 55), RTTI_ATTR("k56", 56), RTTI_ATTR("k57", 57),
    RTTI_ATTR("k58", 58), RTTI_ATTR("k59", 59), RTTI_ATTR("k60", 60),
    RTTI_ATTR("k61", 61), RTTI_ATTR("k62", 62), RTTI_ATTR("k63", 63),
    RTTI_ATTR("wide", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
              17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
              33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
              49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64))

RTTI_DEFINE_FIELD_ATTRS(rtti_test_lib::Field64Big,
    RTTI_FIELD_ATTR_ENTRY(f01, RTTI_ATTR("col", 1)),
    RTTI_FIELD_ATTR_ENTRY(f02, RTTI_ATTR("col", 2)),
    RTTI_FIELD_ATTR_ENTRY(f03, RTTI_ATTR("col", 3)),
    RTTI_FIELD_ATTR_ENTRY(f04, RTTI_ATTR("col", 4)),
    RTTI_FIELD_ATTR_ENTRY(f05, RTTI_ATTR("col", 5)),
    RTTI_FIELD_ATTR_ENTRY(f06, RTTI_ATTR("col", 6)),
    RTTI_FIELD_ATTR_ENTRY(f07, RTTI_ATTR("col", 7)),
    RTTI_FIELD_ATTR_ENTRY(f08, RTTI_ATTR("col", 8)),
    RTTI_FIELD_ATTR_ENTRY(f09, RTTI_ATTR("col", 9)),
    RTTI_FIELD_ATTR_ENTRY(f10, RTTI_ATTR("col", 10)),
    RTTI_FIELD_ATTR_ENTRY(f11, RTTI_ATTR("col", 11)),
    RTTI_FIELD_ATTR_ENTRY(f12, RTTI_ATTR("col", 12)),
    RTTI_FIELD_ATTR_ENTRY(f13, RTTI_ATTR("col", 13)),
    RTTI_FIELD_ATTR_ENTRY(f14, RTTI_ATTR("col", 14)),
    RTTI_FIELD_ATTR_ENTRY(f15, RTTI_ATTR("col", 15)),
    RTTI_FIELD_ATTR_ENTRY(f16, RTTI_ATTR("col", 16)),
    RTTI_FIELD_ATTR_ENTRY(f17, RTTI_ATTR("col", 17)),
    RTTI_FIELD_ATTR_ENTRY(f18, RTTI_ATTR("col", 18)),
    RTTI_FIELD_ATTR_ENTRY(f19, RTTI_ATTR("col", 19)),
    RTTI_FIELD_ATTR_ENTRY(f20, RTTI_ATTR("col", 20)),
    RTTI_FIELD_ATTR_ENTRY(f21, RTTI_ATTR("col", 21)),
    RTTI_FIELD_ATTR_ENTRY(f22, RTTI_ATTR("col", 22)),
    RTTI_FIELD_ATTR_ENTRY(f23, RTTI_ATTR("col", 23)),
    RTTI_FIELD_ATTR_ENTRY(f24, RTTI_ATTR("col", 24)),
    RTTI_FIELD_ATTR_ENTRY(f25, RTTI_ATTR("col", 25)),
    RTTI_FIELD_ATTR_ENTRY(f26, RTTI_ATTR("col", 26)),
    RTTI_FIELD_ATTR_ENTRY(f27, RTTI_ATTR("col", 27)),
    RTTI_FIELD_ATTR_ENTRY(f28, RTTI_ATTR("col", 28)),
    RTTI_FIELD_ATTR_ENTRY(f29, RTTI_ATTR("col", 29)),
    RTTI_FIELD_ATTR_ENTRY(f30, RTTI_ATTR("col", 30)),
    RTTI_FIELD_ATTR_ENTRY(f31, RTTI_ATTR("col", 31)),
    RTTI_FIELD_ATTR_ENTRY(f32, RTTI_ATTR("col", 32)),
    RTTI_FIELD_ATTR_ENTRY(f33, RTTI_ATTR("col", 33)),
    RTTI_FIELD_ATTR_ENTRY(f34, RTTI_ATTR("col", 34)),
    RTTI_FIELD_ATTR_ENTRY(f35, RTTI_ATTR("col", 35)),
    RTTI_FIELD_ATTR_ENTRY(f36, RTTI_ATTR("col", 36)),
    RTTI_FIELD_ATTR_ENTRY(f37, RTTI_ATTR("col", 37)),
    RTTI_FIELD_ATTR_ENTRY(f38, RTTI_ATTR("col", 38)),
    RTTI_FIELD_ATTR_ENTRY(f39, RTTI_ATTR("col", 39)),
    RTTI_FIELD_ATTR_ENTRY(f40, RTTI_ATTR("col", 40)),
    RTTI_FIELD_ATTR_ENTRY(f41, RTTI_ATTR("col", 41)),
    RTTI_FIELD_ATTR_ENTRY(f42, RTTI_ATTR("col", 42)),
    RTTI_FIELD_ATTR_ENTRY(f43, RTTI_ATTR("col", 43)),
    RTTI_FIELD_ATTR_ENTRY(f44, RTTI_ATTR("col", 44)),
    RTTI_FIELD_ATTR_ENTRY(f45, RTTI_ATTR("col", 45)),
    RTTI_FIELD_ATTR_ENTRY(f46, RTTI_ATTR("col", 46)),
    RTTI_FIELD_ATTR_ENTRY(f47, RTTI_ATTR("col", 47)),
    RTTI_FIELD_ATTR_ENTRY(f48, RTTI_ATTR("col", 48)),
    RTTI_FIELD_ATTR_ENTRY(f49, RTTI_ATTR("col", 49)),
    RTTI_FIELD_ATTR_ENTRY(f50, RTTI_ATTR("col", 50)),
    RTTI_FIELD_ATTR_ENTRY(f51, RTTI_ATTR("col", 51)),
    RTTI_FIELD_ATTR_ENTRY(f52, RTTI_ATTR("col", 52)),
    RTTI_FIELD_ATTR_ENTRY(f53, RTTI_ATTR("col", 53)),
    RTTI_FIELD_ATTR_ENTRY(f54, RTTI_ATTR("col", 54)),
    RTTI_FIELD_ATTR_ENTRY(f55, RTTI_ATTR("col", 55)),
    RTTI_FIELD_ATTR_ENTRY(f56, RTTI_ATTR("col", 56)),
    RTTI_FIELD_ATTR_ENTRY(f57, RTTI_ATTR("col", 57)),
    RTTI_FIELD_ATTR_ENTRY(f58, RTTI_ATTR("col", 58)),
    RTTI_FIELD_ATTR_ENTRY(f59, RTTI_ATTR("col", 59)),
    RTTI_FIELD_ATTR_ENTRY(f60, RTTI_ATTR("col", 60)),
    RTTI_FIELD_ATTR_ENTRY(f61, RTTI_ATTR("col", 61)),
    RTTI_FIELD_ATTR_ENTRY(f62, RTTI_ATTR("col", 62)),
    RTTI_FIELD_ATTR_ENTRY(f63, RTTI_ATTR("col", 63)),
    RTTI_FIELD_ATTR_ENTRY(f64, RTTI_ATTR("col", 64)))

#if RTTI_ENABLE_PROTO
// protobuf schema for the 64-field type: one proto3 varint field per member
// (field numbers 1..64). Declared before the test cases so the static inline
// descriptor array initializes before the harness runs them.
namespace rtti {
namespace proto {

template <>
struct schema<rtti_test_lib::Field64Big> {
    static constexpr std::size_t count = 64;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f01>(
            make_field_desc("f01", 1, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f02>(
            make_field_desc("f02", 2, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f03>(
            make_field_desc("f03", 3, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f04>(
            make_field_desc("f04", 4, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f05>(
            make_field_desc("f05", 5, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f06>(
            make_field_desc("f06", 6, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f07>(
            make_field_desc("f07", 7, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f08>(
            make_field_desc("f08", 8, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f09>(
            make_field_desc("f09", 9, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f10>(
            make_field_desc("f10", 10, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f11>(
            make_field_desc("f11", 11, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f12>(
            make_field_desc("f12", 12, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f13>(
            make_field_desc("f13", 13, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f14>(
            make_field_desc("f14", 14, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f15>(
            make_field_desc("f15", 15, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f16>(
            make_field_desc("f16", 16, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f17>(
            make_field_desc("f17", 17, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f18>(
            make_field_desc("f18", 18, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f19>(
            make_field_desc("f19", 19, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f20>(
            make_field_desc("f20", 20, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f21>(
            make_field_desc("f21", 21, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f22>(
            make_field_desc("f22", 22, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f23>(
            make_field_desc("f23", 23, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f24>(
            make_field_desc("f24", 24, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f25>(
            make_field_desc("f25", 25, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f26>(
            make_field_desc("f26", 26, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f27>(
            make_field_desc("f27", 27, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f28>(
            make_field_desc("f28", 28, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f29>(
            make_field_desc("f29", 29, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f30>(
            make_field_desc("f30", 30, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f31>(
            make_field_desc("f31", 31, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f32>(
            make_field_desc("f32", 32, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f33>(
            make_field_desc("f33", 33, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f34>(
            make_field_desc("f34", 34, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f35>(
            make_field_desc("f35", 35, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f36>(
            make_field_desc("f36", 36, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f37>(
            make_field_desc("f37", 37, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f38>(
            make_field_desc("f38", 38, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f39>(
            make_field_desc("f39", 39, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f40>(
            make_field_desc("f40", 40, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f41>(
            make_field_desc("f41", 41, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f42>(
            make_field_desc("f42", 42, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f43>(
            make_field_desc("f43", 43, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f44>(
            make_field_desc("f44", 44, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f45>(
            make_field_desc("f45", 45, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f46>(
            make_field_desc("f46", 46, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f47>(
            make_field_desc("f47", 47, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f48>(
            make_field_desc("f48", 48, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f49>(
            make_field_desc("f49", 49, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f50>(
            make_field_desc("f50", 50, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f51>(
            make_field_desc("f51", 51, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f52>(
            make_field_desc("f52", 52, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f53>(
            make_field_desc("f53", 53, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f54>(
            make_field_desc("f54", 54, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f55>(
            make_field_desc("f55", 55, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f56>(
            make_field_desc("f56", 56, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f57>(
            make_field_desc("f57", 57, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f58>(
            make_field_desc("f58", 58, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f59>(
            make_field_desc("f59", 59, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f60>(
            make_field_desc("f60", 60, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f61>(
            make_field_desc("f61", 61, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f62>(
            make_field_desc("f62", 62, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f63>(
            make_field_desc("f63", 63, wire_type::varint)),
        make_field<rtti_test_lib::Field64Big, &rtti_test_lib::Field64Big::f64>(
            make_field_desc("f64", 64, wire_type::varint)),
    };
};

} // namespace proto
} // namespace rtti
#endif

#if RTTI_ENABLE_FLAT
// FlatBuffers schema for the 64-field type: one i32 vtable slot per member
// (slots 0..63).
namespace rtti {
namespace flat {

template <>
struct schema<rtti_test_lib::Field64Big> {
    static constexpr std::size_t count = 64;
    static inline const field_desc fields[count] = {
        make_field_desc("f01", 0, fkind::i32),
        make_field_desc("f02", 1, fkind::i32),
        make_field_desc("f03", 2, fkind::i32),
        make_field_desc("f04", 3, fkind::i32),
        make_field_desc("f05", 4, fkind::i32),
        make_field_desc("f06", 5, fkind::i32),
        make_field_desc("f07", 6, fkind::i32),
        make_field_desc("f08", 7, fkind::i32),
        make_field_desc("f09", 8, fkind::i32),
        make_field_desc("f10", 9, fkind::i32),
        make_field_desc("f11", 10, fkind::i32),
        make_field_desc("f12", 11, fkind::i32),
        make_field_desc("f13", 12, fkind::i32),
        make_field_desc("f14", 13, fkind::i32),
        make_field_desc("f15", 14, fkind::i32),
        make_field_desc("f16", 15, fkind::i32),
        make_field_desc("f17", 16, fkind::i32),
        make_field_desc("f18", 17, fkind::i32),
        make_field_desc("f19", 18, fkind::i32),
        make_field_desc("f20", 19, fkind::i32),
        make_field_desc("f21", 20, fkind::i32),
        make_field_desc("f22", 21, fkind::i32),
        make_field_desc("f23", 22, fkind::i32),
        make_field_desc("f24", 23, fkind::i32),
        make_field_desc("f25", 24, fkind::i32),
        make_field_desc("f26", 25, fkind::i32),
        make_field_desc("f27", 26, fkind::i32),
        make_field_desc("f28", 27, fkind::i32),
        make_field_desc("f29", 28, fkind::i32),
        make_field_desc("f30", 29, fkind::i32),
        make_field_desc("f31", 30, fkind::i32),
        make_field_desc("f32", 31, fkind::i32),
        make_field_desc("f33", 32, fkind::i32),
        make_field_desc("f34", 33, fkind::i32),
        make_field_desc("f35", 34, fkind::i32),
        make_field_desc("f36", 35, fkind::i32),
        make_field_desc("f37", 36, fkind::i32),
        make_field_desc("f38", 37, fkind::i32),
        make_field_desc("f39", 38, fkind::i32),
        make_field_desc("f40", 39, fkind::i32),
        make_field_desc("f41", 40, fkind::i32),
        make_field_desc("f42", 41, fkind::i32),
        make_field_desc("f43", 42, fkind::i32),
        make_field_desc("f44", 43, fkind::i32),
        make_field_desc("f45", 44, fkind::i32),
        make_field_desc("f46", 45, fkind::i32),
        make_field_desc("f47", 46, fkind::i32),
        make_field_desc("f48", 47, fkind::i32),
        make_field_desc("f49", 48, fkind::i32),
        make_field_desc("f50", 49, fkind::i32),
        make_field_desc("f51", 50, fkind::i32),
        make_field_desc("f52", 51, fkind::i32),
        make_field_desc("f53", 52, fkind::i32),
        make_field_desc("f54", 53, fkind::i32),
        make_field_desc("f55", 54, fkind::i32),
        make_field_desc("f56", 55, fkind::i32),
        make_field_desc("f57", 56, fkind::i32),
        make_field_desc("f58", 57, fkind::i32),
        make_field_desc("f59", 58, fkind::i32),
        make_field_desc("f60", 59, fkind::i32),
        make_field_desc("f61", 60, fkind::i32),
        make_field_desc("f62", 61, fkind::i32),
        make_field_desc("f63", 62, fkind::i32),
        make_field_desc("f64", 63, fkind::i32),
    };
};

} // namespace flat
} // namespace rtti
#endif

namespace rtti_test_lib {

// Helpers shared by the cases below.
inline std::string field64_name(std::size_t index) {
    // "fNN" for the test's 0-based field index (NN = index + 1, 1..64); written
    // by hand so no format-truncation analyzer can doubt the buffer size.
    const unsigned n = static_cast<unsigned>(index) + 1u;
    char buf[4];
    buf[0] = 'f';
    buf[1] = static_cast<char>('0' + (n / 10u) % 10u);
    buf[2] = static_cast<char>('0' + n % 10u);
    buf[3] = '\0';
    return std::string(buf);
}

RTTI_TEST_CASE(Field64CompileTimeCapacity) {
    // Every ladder that dispatches on RTTI_NARG must reach 64.
    static_assert(rtti::field_count<Field64Big>() == 64, "64 external fields");
    static_assert(rtti::field_count<Field64WithDefault>() == 64,
                  "64 default fields");
    static_assert(rtti::field_count<Field64Derived>() == 67,
                  "64 base fields + 3 own fields");
    static_assert(rtti::field_count<Field64Intrusive>() == 64,
                  "64 intrusive fields");
    static_assert(rtti::has_default<Field64WithDefault>::value,
                  "WITH_DEFAULT marks has_default");
    static_assert(!rtti::has_default<Field64Big>::value,
                  "plain macro leaves has_default false");
    static_assert(rtti::ctor_registry<Field64Ctor>::count == 64,
                  "64 constructor parameters");
    static_assert(
        std::tuple_size<typename rtti::ctor_registry<Field64Ctor>::
                            param_types>::value == 64,
        "64 constructor parameter types");
    static_assert(rtti::enum_table<Field64Enum>::count == 64,
                  "64 enumerators");
    using Wide = Field64Wide<F64T01, F64T02, F64T03, F64T04, F64T05, F64T06,
                             F64T07, F64T08, F64T09, F64T10, F64T11, F64T12,
                             F64T13, F64T14, F64T15, F64T16, F64T17, F64T18,
                             F64T19, F64T20, F64T21, F64T22, F64T23, F64T24,
                             F64T25, F64T26, F64T27, F64T28, F64T29, F64T30,
                             F64T31, F64T32, F64T33, F64T34, F64T35, F64T36,
                             F64T37, F64T38, F64T39, F64T40, F64T41, F64T42,
                             F64T43, F64T44, F64T45, F64T46, F64T47, F64T48,
                             F64T49, F64T50, F64T51, F64T52, F64T53, F64T54,
                             F64T55, F64T56, F64T57, F64T58, F64T59, F64T60,
                             F64T61, F64T62, F64T63, F64T64>;
    static_assert(rtti::template_arg_count<Wide>() == 64, "64 template args");
    RTTI_CHECK(true);
}

RTTI_TEST_CASE(Field64RegistrationNamesAndDesc) {
    RTTI_CHECK_EQ(rtti::field_count<Field64Big>(), std::size_t(64));
    const rtti::type_desc* td = rtti::type_of<Field64Big>();
    RTTI_CHECK(td != nullptr);
    RTTI_CHECK(td->kind == rtti::type_kind::reflected);
    RTTI_CHECK(td->as_reflected != nullptr);
    RTTI_CHECK_EQ(td->as_reflected->field_count, std::size_t(64));
    const auto* fs = rtti::fields<Field64Big>();
    RTTI_CHECK(fs != nullptr);
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK_EQ(std::string(fs[i]->name), field64_name(i));
    }
    RTTI_CHECK_EQ(std::string(fs[0]->name), std::string("f01"));
    RTTI_CHECK_EQ(std::string(fs[63]->name), std::string("f64"));
}

RTTI_TEST_CASE(Field64PerFieldGetSet) {
    Field64Big o;
    const auto* fs = rtti::fields<Field64Big>();
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK(fs[i]->set(
            &o, rtti::field_value::of(
                    static_cast<std::int64_t>(i + 1) * 10)));
    }
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK_EQ(fs[i]->get(&o).as_integer(),
                      static_cast<std::int64_t>(i + 1) * 10);
    }
    RTTI_CHECK_EQ(o.f01, 10);
    RTTI_CHECK_EQ(o.f32, 320);
    RTTI_CHECK_EQ(o.f64, 640);
}

RTTI_TEST_CASE(Field64ForEachFieldOrderAndValues) {
    std::string names;
    std::size_t n = 0;
    rtti::for_each_field<Field64Big>([&](const char* name, rtti::type_id) {
        ++n;
        names += name;
        names += ';';
    });
    RTTI_CHECK_EQ(n, std::size_t(64));
    std::string want;
    for (std::size_t i = 0; i < 64; ++i) {
        want += field64_name(i);
        want += ';';
    }
    RTTI_CHECK_EQ(names, want);

    Field64Big o;
    std::int64_t sum = 0;
    std::size_t m = 0;
    rtti::for_each_field(
        o, [&](const char*, rtti::type_id, const rtti::field_value& v) {
            ++m;
            sum += v.as_integer();
        });
    RTTI_CHECK_EQ(m, std::size_t(64));
    RTTI_CHECK_EQ(sum, std::int64_t(2080)); // defaults 1..64
}

RTTI_TEST_CASE(Field64FieldIteratorForwardAndReverse) {
    std::size_t fwd = 0;
    const char* first = nullptr;
    const char* last = nullptr;
    for (auto it = rtti::field_begin<Field64Big>();
         it != rtti::field_end<Field64Big>(); ++it) {
        if (fwd == 0) {
            first = (*it)->name;
        }
        last = (*it)->name;
        ++fwd;
    }
    RTTI_CHECK_EQ(fwd, std::size_t(64));
    RTTI_CHECK_EQ(std::string(first), std::string("f01"));
    RTTI_CHECK_EQ(std::string(last), std::string("f64"));

    std::size_t rev = 0;
    const char* rfirst = nullptr;
    const char* rlast = nullptr;
    for (auto it = rtti::field_rbegin<Field64Big>();
         it != rtti::field_rend<Field64Big>(); ++it) {
        if (rev == 0) {
            rfirst = (*it)->name;
        }
        rlast = (*it)->name;
        ++rev;
    }
    RTTI_CHECK_EQ(rev, std::size_t(64));
    RTTI_CHECK_EQ(std::string(rfirst), std::string("f64"));
    RTTI_CHECK_EQ(std::string(rlast), std::string("f01"));
}

RTTI_TEST_CASE(Field64IntrusivePrivateLastField) {
    RTTI_CHECK_EQ(rtti::field_count<Field64Intrusive>(), std::size_t(64));
    Field64Intrusive v;
    const auto* fs = rtti::fields<Field64Intrusive>();
    RTTI_CHECK_EQ(std::string(fs[0]->name), std::string("g01"));
    RTTI_CHECK_EQ(std::string(fs[63]->name), std::string("g64"));
    RTTI_CHECK_EQ(fs[63]->get(&v).as_integer(), std::int64_t(64));
    RTTI_CHECK(fs[63]->set(&v, rtti::field_value::of(std::int64_t(999))));
    RTTI_CHECK_EQ(fs[63]->get(&v).as_integer(), std::int64_t(999));
    RTTI_CHECK_EQ(fs[0]->get(&v).as_integer(), std::int64_t(1));
    // Intrusive registrations cannot access-classify members: every field is
    // reported as the conservative private_ boundary.
    RTTI_CHECK_EQ(rtti::fields_of<Field64Intrusive>::desc[0].access,
                  rtti::member_access::private_);
    RTTI_CHECK_EQ(rtti::fields_of<Field64Intrusive>::desc[63].access,
                  rtti::member_access::private_);
}

RTTI_TEST_CASE(Field64WithDefaultKeepsDefaults) {
    RTTI_CHECK_EQ(rtti::field_count<Field64WithDefault>(), std::size_t(64));
    Field64WithDefault d;
    RTTI_CHECK_EQ(d.d01, 101);
    RTTI_CHECK_EQ(d.d64, 164);
    const auto* fs = rtti::fields<Field64WithDefault>();
    RTTI_CHECK(fs[0]->set(&d, rtti::field_value::of(std::int64_t(7))));
    RTTI_CHECK_EQ(d.d01, 7);
    RTTI_CHECK_EQ(fs[63]->get(&d).as_integer(), std::int64_t(164));
}

RTTI_TEST_CASE(Field64DerivedMergedTable) {
    RTTI_CHECK_EQ(rtti::field_count<Field64Derived>(), std::size_t(67));
    const auto* fs = rtti::fields<Field64Derived>();
    RTTI_CHECK_EQ(std::string(fs[0]->name), std::string("f01"));
    RTTI_CHECK_EQ(std::string(fs[63]->name), std::string("f64"));
    RTTI_CHECK_EQ(std::string(fs[64]->name), std::string("extra1"));
    RTTI_CHECK_EQ(std::string(fs[65]->name), std::string("extra2"));
    RTTI_CHECK_EQ(std::string(fs[66]->name), std::string("extra3"));

    Field64Derived d;
    RTTI_CHECK(fs[32]->set(&d, rtti::field_value::of(std::int64_t(333))));
    RTTI_CHECK(fs[65]->set(&d, rtti::field_value::of(std::int64_t(555))));
    RTTI_CHECK_EQ(d.f33, 333);
    RTTI_CHECK_EQ(d.extra2, 555);
    RTTI_CHECK_EQ(d.f01, 1); // untouched base field intact
    const rtti::type_desc* td = rtti::type_of<Field64Derived>();
    RTTI_CHECK(td != nullptr);
    RTTI_CHECK(td->as_reflected != nullptr);
    RTTI_CHECK_EQ(td->as_reflected->field_count, std::size_t(67));
}

RTTI_TEST_CASE(Field64Enum64) {
    RTTI_CHECK_EQ(rtti::enum_count<Field64Enum>(), std::size_t(64));
    RTTI_CHECK_EQ(std::string(rtti::enum_name_at<Field64Enum>(0)),
                  std::string("e01"));
    RTTI_CHECK_EQ(std::string(rtti::enum_name_at<Field64Enum>(31)),
                  std::string("e32"));
    RTTI_CHECK_EQ(std::string(rtti::enum_name_at<Field64Enum>(63)),
                  std::string("e64"));
    RTTI_CHECK_EQ(rtti::enum_to_string(Field64Enum::e33), std::string("e33"));
    RTTI_CHECK(rtti::enum_from_string<Field64Enum>("e64") ==
               Field64Enum::e64);
    RTTI_CHECK_EQ(static_cast<int>(rtti::enum_from_string<Field64Enum>("e17")),
                  16);
}

RTTI_TEST_CASE(Field64Ctor64Parameters) {
    RTTI_CHECK_EQ(rtti::ctor_registry<Field64Ctor>::count, std::size_t(64));
    RTTI_CHECK_EQ(std::string(rtti::ctor_registry<Field64Ctor>::params[0].name),
                  std::string("p01"));
    RTTI_CHECK_EQ(
        std::string(rtti::ctor_registry<Field64Ctor>::params[63].name),
        std::string("p64"));
    RTTI_CHECK(!rtti::ctor_registry<Field64Ctor>::params[63].optional);

    std::array<rtti::ctor_arg, 64> args{};
    for (std::size_t i = 0; i < 64; ++i) {
        args[i] = rtti::ctor_arg::scalar(
            rtti::field_value::of(static_cast<std::int64_t>(i + 1)));
    }
    auto r = rtti::make_object<Field64Ctor>(args.data(), args.size());
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value->sum, 2080);
}

RTTI_TEST_CASE(Field64TemplateArgs64) {
    using Wide = Field64Wide<F64T01, F64T02, F64T03, F64T04, F64T05, F64T06,
                             F64T07, F64T08, F64T09, F64T10, F64T11, F64T12,
                             F64T13, F64T14, F64T15, F64T16, F64T17, F64T18,
                             F64T19, F64T20, F64T21, F64T22, F64T23, F64T24,
                             F64T25, F64T26, F64T27, F64T28, F64T29, F64T30,
                             F64T31, F64T32, F64T33, F64T34, F64T35, F64T36,
                             F64T37, F64T38, F64T39, F64T40, F64T41, F64T42,
                             F64T43, F64T44, F64T45, F64T46, F64T47, F64T48,
                             F64T49, F64T50, F64T51, F64T52, F64T53, F64T54,
                             F64T55, F64T56, F64T57, F64T58, F64T59, F64T60,
                             F64T61, F64T62, F64T63, F64T64>;
    RTTI_CHECK_EQ(rtti::template_arg_count<Wide>(), std::size_t(64));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<Wide, 0>, F64T01>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<Wide, 63>, F64T64>::value));
    RTTI_CHECK(rtti::template_arg_desc_of<Wide>(0) == rtti::type_of<F64T01>());
    RTTI_CHECK(rtti::template_arg_desc_of<Wide>(63) == rtti::type_of<F64T64>());
    std::size_t n = 0;
    rtti::for_each_template_arg<Wide>(
        [&](std::size_t, const rtti::type_desc* d) {
            if (d != nullptr) {
                ++n;
            }
        });
    RTTI_CHECK_EQ(n, std::size_t(64));
}

RTTI_TEST_CASE(Field64TemplateArgsParenWrappedElement) {
    using Mix = Field64Mix<F64T01, std::pair<int, int>, F64T03, F64T04,
                           F64T05, F64T06, F64T07, F64T08, F64T09, F64T10,
                           F64T11, F64T12, F64T13, F64T14, F64T15, F64T16,
                           F64T17>;
    RTTI_CHECK_EQ(rtti::template_arg_count<Mix>(), std::size_t(17));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<Mix, 0>, F64T01>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<Mix, 1>,
                             std::pair<int, int>>::value));
    RTTI_CHECK((std::is_same<rtti::template_arg_at<Mix, 16>, F64T17>::value));
    RTTI_CHECK((rtti::template_arg_desc_of<Mix>(1) ==
                rtti::type_of<std::pair<int, int>>()));
}

RTTI_TEST_CASE(Field64TypeAttrs64Entries) {
    RTTI_CHECK_EQ(rtti::attr_count<Field64AttrHost>(), std::size_t(64));
    const rtti::attr* k01 = rtti::attr_at<Field64AttrHost>(0);
    RTTI_CHECK(k01 != nullptr);
    RTTI_CHECK_EQ(std::string(k01->name), std::string("k01"));
    RTTI_CHECK_EQ(k01->args.size(), std::size_t(1));
    RTTI_CHECK_EQ(k01->args[0].as_integer(), std::uint64_t(1));
    const rtti::attr* k63 = rtti::attr_at<Field64AttrHost>(62);
    RTTI_CHECK(k63 != nullptr);
    RTTI_CHECK_EQ(std::string(k63->name), std::string("k63"));
    RTTI_CHECK_EQ(k63->args[0].as_integer(), std::uint64_t(63));
    RTTI_CHECK(rtti::attr_at<Field64AttrHost>(64) == nullptr);

    const rtti::attr* wide = rtti::find_attr<Field64AttrHost>("wide");
    RTTI_CHECK(wide != nullptr);
    RTTI_CHECK_EQ(wide->args.size(), std::size_t(64));
    RTTI_CHECK_EQ(wide->args[0].as_integer(), std::uint64_t(1));
    RTTI_CHECK_EQ(wide->args[63].as_integer(), std::uint64_t(64));
    RTTI_CHECK(rtti::has_attr<Field64AttrHost>("k63"));
    RTTI_CHECK(!rtti::has_attr<Field64AttrHost>("k64"));
}

RTTI_TEST_CASE(Field64FieldAttrs64Entries) {
    RTTI_CHECK_EQ(rtti::field_attr_table<Field64Big>::count, std::size_t(64));
    RTTI_CHECK_EQ(
        std::string(rtti::field_attr_table<Field64Big>::array[0].field),
        std::string("f01"));
    RTTI_CHECK_EQ(
        std::string(rtti::field_attr_table<Field64Big>::array[63].field),
        std::string("f64"));
    rtti::field_attr_list l = rtti::field_attr<Field64Big>("f07");
    RTTI_CHECK_EQ(l.count, std::size_t(1));
    RTTI_CHECK(l.items != nullptr);
    RTTI_CHECK_EQ(std::string(l.items[0].name), std::string("col"));
    RTTI_CHECK_EQ(l.items[0].args[0].as_integer(), std::uint64_t(7));
    RTTI_CHECK_EQ(rtti::field_attr_count<Field64Big>("f64"), std::size_t(1));
    RTTI_CHECK(rtti::field_has_attr<Field64Big>(std::size_t(0), "col"));
    RTTI_CHECK(rtti::field_attr<Field64Big>("f01").count == std::size_t(1));
    RTTI_CHECK(rtti::field_attr<Field64Big>("nope").count == std::size_t(0));
}

#if RTTI_ENABLE_JSON
RTTI_TEST_CASE(Field64JsonRoundTrip) {
    Field64Big o;
    const auto* fs = rtti::fields<Field64Big>();
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK(fs[i]->set(
            &o, rtti::field_value::of(
                    static_cast<std::int64_t>(i + 1) * 3)));
    }
    const std::string text = rtti::json::serialize(o);
    RTTI_CHECK(!text.empty());
    auto back = rtti::json::parse_as<Field64Big>(text);
    RTTI_CHECK(back.ok());
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK_EQ(fs[i]->get(&back.value).as_integer(),
                      static_cast<std::int64_t>(i + 1) * 3);
    }
}

RTTI_TEST_CASE(Field64JsonMissingKeysKeepDefaults) {
    // Only d01 is present; every other field must keep its default because
    // the type is registered through RTTI_DEFINE_TYPE_WITH_DEFAULT.
    auto back = rtti::json::parse_as<Field64WithDefault>(
        std::string("{\"d01\": 7}"));
    RTTI_CHECK(back.ok());
    RTTI_CHECK_EQ(back.value.d01, 7);
    RTTI_CHECK_EQ(back.value.d02, 102);
    RTTI_CHECK_EQ(back.value.d32, 132);
    RTTI_CHECK_EQ(back.value.d64, 164);
}
#endif

#if RTTI_ENABLE_INI
RTTI_TEST_CASE(Field64IniRoundTrip) {
    Field64Big o;
    const auto* fs = rtti::fields<Field64Big>();
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK(fs[i]->set(
            &o, rtti::field_value::of(
                    static_cast<std::int64_t>(i + 1) * 2)));
    }
    const std::string text = rtti::ini::serialize(o);
    RTTI_CHECK(!text.empty());
    auto back = rtti::ini::parse_as<Field64Big>(rtti::string_view(text));
    RTTI_CHECK(back.ok());
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK_EQ(fs[i]->get(&back.value).as_integer(),
                      static_cast<std::int64_t>(i + 1) * 2);
    }
}
#endif

#if RTTI_ENABLE_XML
RTTI_TEST_CASE(Field64XmlRoundTrip) {
    Field64Big o;
    const auto* fs = rtti::fields<Field64Big>();
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK(fs[i]->set(
            &o, rtti::field_value::of(
                    static_cast<std::int64_t>(i + 1) * 5)));
    }
    const std::string text = rtti::xml::serialize(o);
    RTTI_CHECK(!text.empty());
    auto back = rtti::xml::parse_as<Field64Big>(text);
    RTTI_CHECK(back.ok());
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK_EQ(fs[i]->get(&back.value).as_integer(),
                      static_cast<std::int64_t>(i + 1) * 5);
    }
}
#endif

#if RTTI_ENABLE_TOML
RTTI_TEST_CASE(Field64TomlRoundTrip) {
    Field64Big o;
    const auto* fs = rtti::fields<Field64Big>();
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK(fs[i]->set(
            &o, rtti::field_value::of(
                    static_cast<std::int64_t>(i + 1) * 7)));
    }
    const std::string text = rtti::toml::serialize(o);
    RTTI_CHECK(!text.empty());
    auto back = rtti::toml::parse_as<Field64Big>(text);
    RTTI_CHECK(back.ok());
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK_EQ(fs[i]->get(&back.value).as_integer(),
                      static_cast<std::int64_t>(i + 1) * 7);
    }
}
#endif

#if RTTI_ENABLE_PROTO
RTTI_TEST_CASE(Field64ProtoRoundTrip) {
    Field64Big o;
    const auto* fs = rtti::fields<Field64Big>();
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK(fs[i]->set(
            &o, rtti::field_value::of(
                    static_cast<std::int64_t>(i + 1) * 11)));
    }
    const std::string wire = rtti::proto::serialize(o);
    RTTI_CHECK(!wire.empty());
    auto back = rtti::proto::parse_as<Field64Big>(rtti::string_view(wire));
    RTTI_CHECK(back.ok());
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK_EQ(fs[i]->get(&back.value).as_integer(),
                      static_cast<std::int64_t>(i + 1) * 11);
    }
}
#endif

#if RTTI_ENABLE_FLAT
RTTI_TEST_CASE(Field64FlatRoundTrip) {
    Field64Big o;
    const auto* fs = rtti::fields<Field64Big>();
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK(fs[i]->set(
            &o, rtti::field_value::of(
                    static_cast<std::int64_t>(i + 1) * 13)));
    }
    std::string wire;
    RTTI_CHECK_EQ(rtti::flat::serialize(o, wire), rtti::error_code::ok);
    RTTI_CHECK(!wire.empty());
    auto back = rtti::flat::parse_as<Field64Big>(rtti::string_view(wire));
    RTTI_CHECK(back.ok());
    for (std::size_t i = 0; i < 64; ++i) {
        RTTI_CHECK_EQ(fs[i]->get(&back.value).as_integer(),
                      static_cast<std::int64_t>(i + 1) * 13);
    }
}
#endif

RTTI_TEST_CASE(Field64ConcurrentMaterialization) {
    constexpr int kThreads = 6;
    constexpr int kRounds = 32;

    std::vector<std::vector<const rtti::type_desc*>> seen(
        static_cast<std::size_t>(kThreads),
        std::vector<const rtti::type_desc*>(1, nullptr));
    std::atomic<int> null_desc{0};
    std::atomic<int> bad_count{0};
    std::atomic<int> bad_iteration{0};
    std::atomic<int> arrived{0};
    std::atomic<bool> go{false};

    auto worker = [&](int tid) {
        arrived.fetch_add(1, std::memory_order_acq_rel);
        while (arrived.load(std::memory_order_acquire) < kThreads) {
        }
        while (!go.load(std::memory_order_acquire)) {
        }
        for (int round = 0; round < kRounds; ++round) {
            const rtti::type_desc* d = rtti::type_of<Field64Concur>();
            if (d == nullptr || d->as_reflected == nullptr ||
                d->as_reflected->field_count != 64) {
                bad_count.fetch_add(1, std::memory_order_relaxed);
            }
            if (d == nullptr) {
                null_desc.fetch_add(1, std::memory_order_relaxed);
            }
            if (round == 0) {
                seen[static_cast<std::size_t>(tid)][0] = d;
            }
            std::size_t n = 0;
            rtti::for_each_field<Field64Concur>(
                [&](const char*, rtti::type_id) { ++n; });
            if (n != 64) {
                bad_iteration.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kThreads));
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(worker, t);
    }
    go.store(true, std::memory_order_release);
    for (std::thread& th : threads) {
        th.join();
    }

    RTTI_CHECK_EQ(null_desc.load(), 0);
    RTTI_CHECK_EQ(bad_count.load(), 0);
    RTTI_CHECK_EQ(bad_iteration.load(), 0);
    const rtti::type_desc* expected = rtti::type_of<Field64Concur>();
    RTTI_CHECK(expected != nullptr);
    for (int t = 0; t < kThreads; ++t) {
        RTTI_CHECK_EQ(seen[static_cast<std::size_t>(t)][0], expected);
    }
}

RTTI_TEST_CASE(Field64NoAllocationLeakOnRepeatAccess) {
    // Warm-up: materialize the descriptor (and any lazily built table) before
    // taking the balance snapshot; the registry caches them forever.
    RTTI_CHECK(rtti::type_of<Field64Big>() != nullptr);
    {
        Field64Big warm;
        std::size_t n = 0;
        rtti::for_each_field<Field64Big>(
            [&](const char*, rtti::type_id) { ++n; });
        RTTI_CHECK_EQ(n, std::size_t(64));
        (void)warm;
    }

    const long long before =
        rtti_test_lib::g_rttileak_live().load(std::memory_order_relaxed);
    constexpr int kIters = 1000;
    std::int64_t sink = 0;
    std::size_t count_ok = 0;
    for (int iter = 0; iter < kIters; ++iter) {
        Field64Big o;
        const auto* fs = rtti::fields<Field64Big>();
        const std::size_t idx = static_cast<std::size_t>(iter % 64);
        (void)fs[idx]->set(&o, rtti::field_value::of(
                                   static_cast<std::int64_t>(iter)));
        sink += fs[idx]->get(&o).as_integer();
        std::size_t n = 0;
        rtti::for_each_field<Field64Big>(
            [&](const char*, rtti::type_id) { ++n; });
        if (n == 64) {
            ++count_ok;
        }
    }
    RTTI_CHECK_EQ(count_ok, std::size_t(kIters));
    (void)sink;
    // Any per-call leak would grow the live balance by kIters allocations.
    RTTI_CHECK_EQ(
        rtti_test_lib::g_rttileak_live().load(std::memory_order_relaxed),
        before);
}

} // namespace rtti_test_lib
