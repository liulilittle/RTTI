// Unified T <-> string conversion tests (spec §16).
// Exercises rtti::to_string<T> / rtti::from_string<T> over the full supported
// type surface: scalars (bool/ints/floats/doubles/strings), registered enums,
// containers (custom allocator/less included), optional/variant/tuple/pair/
// array/vector<bool>, native and multi-dim C arrays, reflected structs,
// date_time, and arbitrary-base integers (to_string_base family).
// Malformed/trailing/wrong-type/overflow inputs must return an error and never
// crash (spec §0); round trips must be exact, including a 600-round fuzz of a
// random reflected struct.
//
// Structs are declared in rtti_test_lib and registered at global scope; test
// functions live in rtti_test_lib. All names are prefixed StringConv* so they
// stay unique across TUs.

#include "test_harness.hpp"
#include <rtti/date_time.hpp>
#include <rtti/to_string.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <list>
#include <map>
#include <new>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// R2-1 leak accounting: to_string's text_node tree allocates through the
// global new/delete. A failed to_value (convert.hpp reflect_to returns before
// attaching the just-built child) must free the detached node with the tree;
// the original make_node returned a raw `new` with no reclaim so every failure
// leaked one node. The counter below turns that leak into an assertion: a
// tight loop over a deterministic failing conversion must return the live
// allocation balance to its snapshot.
// ---------------------------------------------------------------------------

namespace rtti_test_lib {

inline long long& g_rttileak_allocs() {
    static long long n = 0;
    return n;
}
inline long long& g_rttileak_live() {
    static long long n = 0;
    return n;
}

} // namespace rtti_test_lib

void* operator new(std::size_t sz) {
    ++rtti_test_lib::g_rttileak_allocs();
    ++rtti_test_lib::g_rttileak_live();
    void* p = std::malloc(sz == 0 ? 1 : sz);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new[](std::size_t sz) {
    ++rtti_test_lib::g_rttileak_allocs();
    ++rtti_test_lib::g_rttileak_live();
    void* p = std::malloc(sz == 0 ? 1 : sz);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void operator delete(void* p) noexcept {
    if (p != nullptr) {
        --rtti_test_lib::g_rttileak_live();
        std::free(p);
    }
}
void operator delete[](void* p) noexcept {
    if (p != nullptr) {
        --rtti_test_lib::g_rttileak_live();
        std::free(p);
    }
}
void operator delete(void* p, std::size_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    ::operator delete[](p);
}

namespace rtti_test_lib {

enum class StringConvColor { Red = 1, Green = 2, Blue = 4 };

// R2-1: a registered enum whose label table does not cover an out-of-range
// value (serializing that value fails inside a reflected struct's field walk).
enum class StringConvLeakyEnum { Alpha = 1, Beta = 2 };

// Reflected holder whose "bad" field fails to serialize when its enum value
// has no label: reflect_to attaches "good", then fails on "bad" and returns
// without attaching the just-built "bad" child (convert.hpp reflect_to). The
// detached text_node child must be freed with the enclosing tree.

struct StringConvPoint {
    int x = 0;
    int y = 0;
};

struct StringConvNested {
    int id = 0;
    std::string name;
    std::vector<int> nums;
    std::map<std::string, int> tags;
};

struct StringConvFuzzStruct {
    int a = 0;
    double b = 0.0;
    bool c = false;
    std::string d;
    std::vector<int> e;
    std::optional<int> f;
    StringConvPoint g;
};

struct StringConvLeakyHolder {
    int good = 0;
    StringConvLeakyEnum bad = StringConvLeakyEnum::Alpha;
};

template <typename T>
struct StringConvAlloc {
    using value_type = T;
    StringConvAlloc() noexcept = default;
    template <typename U>
    StringConvAlloc(const StringConvAlloc<U>&) noexcept {}
    T* allocate(std::size_t n) {
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
    template <typename U>
    struct rebind {
        using other = StringConvAlloc<U>;
    };
    bool operator==(const StringConvAlloc&) const noexcept { return true; }
    bool operator!=(const StringConvAlloc&) const noexcept { return false; }
};

struct StringConvLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    }
};

} // namespace rtti_test_lib

RTTI_DEFINE_ENUM(rtti_test_lib::StringConvColor, Red, Green, Blue)
RTTI_DEFINE_TYPE(rtti_test_lib::StringConvPoint, x, y)
RTTI_DEFINE_TYPE(rtti_test_lib::StringConvNested, id, name, nums, tags)
RTTI_DEFINE_TYPE(rtti_test_lib::StringConvFuzzStruct, a, b, c, d, e, f, g)
RTTI_DEFINE_ENUM(rtti_test_lib::StringConvLeakyEnum, Alpha, Beta)
RTTI_DEFINE_TYPE(rtti_test_lib::StringConvLeakyHolder, good, bad)

namespace rtti_test_lib {

// Exact round trip for a scalar value, including the optional convenience API.
template <typename T>
void stringconv_check_scalar_rt(const T& value) {
    const std::string s = rtti::to_string(value);
    T back{};
    RTTI_CHECK_EQ(rtti::from_string(s, back), rtti::error_code::ok);
    RTTI_CHECK(back == value);
    const std::optional<T> opt = rtti::from_string<T>(s);
    RTTI_CHECK(opt.has_value());
    RTTI_CHECK(*opt == value);
}

RTTI_TEST_CASE(StringConvScalarRoundTrip) {
    stringconv_check_scalar_rt<int>(0);
    stringconv_check_scalar_rt<int>(42);
    stringconv_check_scalar_rt<int>(-17);
    stringconv_check_scalar_rt<int>((std::numeric_limits<int>::max)());
    stringconv_check_scalar_rt<int>((std::numeric_limits<int>::min)());
    stringconv_check_scalar_rt<long long>((std::numeric_limits<long long>::max)());
    stringconv_check_scalar_rt<long long>((std::numeric_limits<long long>::min)());
    stringconv_check_scalar_rt<unsigned int>((std::numeric_limits<unsigned int>::max)());
    stringconv_check_scalar_rt<unsigned long long>(
        (std::numeric_limits<unsigned long long>::max)());
    stringconv_check_scalar_rt<signed char>(signed char(-5));
    stringconv_check_scalar_rt<unsigned char>(unsigned char(200));
    stringconv_check_scalar_rt<short>(short(-300));
    stringconv_check_scalar_rt<bool>(true);
    stringconv_check_scalar_rt<bool>(false);
    stringconv_check_scalar_rt<double>(3.14);
    stringconv_check_scalar_rt<double>(0.0);
    stringconv_check_scalar_rt<double>(-2.5e-100);
    stringconv_check_scalar_rt<double>(1.0e300);
    stringconv_check_scalar_rt<double>(0.1);
    stringconv_check_scalar_rt<float>(3.14f);
    stringconv_check_scalar_rt<float>(-0.0f);
    stringconv_check_scalar_rt<float>(1.5e10f);
    stringconv_check_scalar_rt<std::string>(std::string());
    stringconv_check_scalar_rt<std::string>(std::string("hello"));
    const std::string with_nul("a\0b\n\t\"q\"", 9);
    stringconv_check_scalar_rt<std::string>(with_nul);
    stringconv_check_scalar_rt<std::string>(std::string("line1\nline2"));

    const std::string sview_src("view-data");
    const std::string_view sview(sview_src);
    std::string_view sv_back;
    RTTI_CHECK_EQ(rtti::from_string(sview_src, sv_back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(sv_back, sview);

    char buf[16] = {0};
    RTTI_CHECK_EQ(rtti::from_string(std::string("hello"), buf),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(std::strcmp(buf, "hello"), 0);
    char buf_out[16] = {0};
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(buf), buf_out),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(std::strcmp(buf, buf_out), 0);
}

RTTI_TEST_CASE(StringConvEnumRoundTrip) {
    stringconv_check_scalar_rt<StringConvColor>(StringConvColor::Green);
    stringconv_check_scalar_rt<StringConvColor>(StringConvColor::Blue);
    stringconv_check_scalar_rt<StringConvColor>(StringConvColor::Red);
    RTTI_CHECK_EQ(rtti::to_string<StringConvColor>(StringConvColor::Green),
                  std::string("Green"));
    RTTI_CHECK_EQ(rtti::to_string<StringConvColor>(StringConvColor::Red),
                  std::string("Red"));

    StringConvColor c = StringConvColor::Red;
    RTTI_CHECK_EQ(rtti::from_string(std::string("nope"), c),
                  rtti::error_code::kind_mismatch);
    RTTI_CHECK(
        !rtti::from_string<StringConvColor>(std::string("nope")).has_value());

    // A value with no label serializes to an empty string.
    StringConvColor unknown = static_cast<StringConvColor>(8);
    RTTI_CHECK_EQ(rtti::to_string(unknown), std::string());
}

RTTI_TEST_CASE(StringConvFloatText) {
    // Shortest round-tripping text with an explicit ".0" for whole values.
    RTTI_CHECK_EQ(rtti::to_string(3.14f), std::string("3.14"));
    RTTI_CHECK_EQ(rtti::to_string(3.0), std::string("3.0"));
    RTTI_CHECK_EQ(rtti::to_string(-0.5), std::string("-0.5"));
    // Non-finite tokens, exact round trip in both directions.
    const double pos_inf = std::numeric_limits<double>::infinity();
    const double neg_inf = -std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    RTTI_CHECK_EQ(rtti::to_string(pos_inf), std::string("inf"));
    RTTI_CHECK_EQ(rtti::to_string(neg_inf), std::string("-inf"));
    RTTI_CHECK_EQ(rtti::to_string(nan), std::string("nan"));
    double back = 0.0;
    RTTI_CHECK_EQ(rtti::from_string(std::string("inf"), back),
                  rtti::error_code::ok);
    RTTI_CHECK(std::isinf(back) && back > 0);
    RTTI_CHECK_EQ(rtti::from_string(std::string("-inf"), back),
                  rtti::error_code::ok);
    RTTI_CHECK(std::isinf(back) && back < 0);
    RTTI_CHECK_EQ(rtti::from_string(std::string("nan"), back),
                  rtti::error_code::ok);
    RTTI_CHECK(std::isnan(back));
    float fback = 0.0f;
    RTTI_CHECK_EQ(rtti::from_string(std::string("inf"), fback),
                  rtti::error_code::ok);
    RTTI_CHECK(std::isinf(fback));
}

RTTI_TEST_CASE(StringConvContainerRoundTrip) {
    std::vector<int> vi{1, 2, 3, -4};
    RTTI_CHECK_EQ(rtti::from_string<std::vector<int>>(rtti::to_string(vi)).value_or(std::vector<int>{}), vi);
    std::list<std::string> ls{"a", "b"};
    RTTI_CHECK_EQ(rtti::from_string<std::list<std::string>>(rtti::to_string(ls)).value_or(std::list<std::string>{}), ls);
    std::deque<double> dd{1.5, -2.25, 3.0};
    RTTI_CHECK_EQ(rtti::from_string<std::deque<double>>(rtti::to_string(dd)).value_or(std::deque<double>{}), dd);
    std::set<int> si{9, 1, 5};
    RTTI_CHECK_EQ(rtti::from_string<std::set<int>>(rtti::to_string(si)).value_or(std::set<int>{}), si);
    std::set<std::string> ss{"x", "y"};
    RTTI_CHECK_EQ(rtti::from_string<std::set<std::string>>(rtti::to_string(ss)).value_or(std::set<std::string>{}), ss);
    std::map<std::string, int> mi{{"b", 2}, {"a", 1}};
    RTTI_CHECK_EQ((rtti::from_string<std::map<std::string, int>>(rtti::to_string(mi)).value_or(std::map<std::string, int>{})), mi);
    std::unordered_map<std::string, double> um{{"pi", 3.14}, {"e", 2.71}};
    const std::string ums = rtti::to_string(um);
    std::unordered_map<std::string, double> umb;
    RTTI_CHECK_EQ(rtti::from_string(ums, umb), rtti::error_code::ok);
    RTTI_CHECK_EQ(umb.size(), std::size_t(2));
    std::unordered_set<int> us{1, 2, 3};
    RTTI_CHECK_EQ(rtti::from_string<std::unordered_set<int>>(rtti::to_string(us)).value_or(std::unordered_set<int>{}), us);

    // Custom allocator / comparator flow through untouched.
    std::vector<int, StringConvAlloc<int>> custom_vec{7, 8, 9};
    std::vector<int, StringConvAlloc<int>> custom_vec_back;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(custom_vec), custom_vec_back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(custom_vec_back.size(), std::size_t(3));
    RTTI_CHECK_EQ(custom_vec_back[2], 9);

    std::map<std::string, int, StringConvLess> less_map;
    less_map["Alpha"] = 1;
    less_map["beta"] = 2;
    std::map<std::string, int, StringConvLess> less_map_back;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(less_map), less_map_back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(less_map_back.size(), std::size_t(2));
}

RTTI_TEST_CASE(StringConvNewTypeRoundTrip) {
    std::optional<int> engaged = 123;
    RTTI_CHECK_EQ(rtti::from_string<std::optional<int>>(rtti::to_string(engaged)).value_or(std::optional<int>{}), engaged);
    std::optional<int> empty;
    RTTI_CHECK_EQ(rtti::to_string(empty), std::string("null"));
    const std::optional<std::optional<int>> opt_empty =
        rtti::from_string<std::optional<int>>(rtti::to_string(empty));
    RTTI_CHECK(opt_empty.has_value());
    RTTI_CHECK(!opt_empty.value().has_value());

    std::variant<int, std::string, double, StringConvPoint> v(int(7));
    const std::string vs = rtti::to_string(v);
    std::variant<int, std::string, double, StringConvPoint> vout;
    RTTI_CHECK_EQ(rtti::from_string(vs, vout), rtti::error_code::ok);
    RTTI_CHECK(std::holds_alternative<int>(vout));
    RTTI_CHECK_EQ(std::get<int>(vout), 7);
    v = std::string("text");
    std::variant<int, std::string, double, StringConvPoint> vstr;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(v), vstr), rtti::error_code::ok);
    RTTI_CHECK(std::holds_alternative<std::string>(vstr));
    RTTI_CHECK_EQ(std::get<std::string>(vstr), std::string("text"));
    v = StringConvPoint{3, 4};
    std::variant<int, std::string, double, StringConvPoint> vpoint;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(v), vpoint), rtti::error_code::ok);
    RTTI_CHECK(std::holds_alternative<StringConvPoint>(vpoint));
    RTTI_CHECK_EQ(std::get<StringConvPoint>(vpoint).x, 3);
    RTTI_CHECK_EQ(std::get<StringConvPoint>(vpoint).y, 4);

    std::tuple<int, std::string, double> t(1, "two", 3.0);
    std::tuple<int, std::string, double> tb;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(t), tb), rtti::error_code::ok);
    RTTI_CHECK(tb == t);

    std::pair<int, std::string> p(5, "five");
    std::pair<int, std::string> pb;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(p), pb), rtti::error_code::ok);
    RTTI_CHECK(pb == p);

    std::array<int, 3> arr{{1, 2, 3}};
    std::array<int, 3> arrb;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(arr), arrb), rtti::error_code::ok);
    RTTI_CHECK(arrb == arr);

    std::array<std::array<int, 2>, 3> arr2d{{
        {{1, 2}}, {{3, 4}}, {{5, 6}}}};
    std::array<std::array<int, 2>, 3> arr2db;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(arr2d), arr2db),
                  rtti::error_code::ok);
    RTTI_CHECK(arr2db == arr2d);

    std::vector<bool> vb_vec{true, false, true, true};
    std::vector<bool> vb_back;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(vb_vec), vb_back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(vb_back, vb_vec);

    std::vector<std::optional<int>> vo{std::optional<int>(1),
                                       std::optional<int>(), std::optional<int>(3)};
    std::vector<std::optional<int>> vo_back;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(vo), vo_back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(vo_back, vo);
}

RTTI_TEST_CASE(StringConvNativeArrayRoundTrip) {
    int a1[3] = {1, 2, 3};
    int a1b[3] = {0, 0, 0};
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(a1), a1b),
                  rtti::error_code::ok);
    RTTI_CHECK(a1b[0] == 1 && a1b[1] == 2 && a1b[2] == 3);

    int a2[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int a2b[2][3] = {};
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(a2), a2b),
                  rtti::error_code::ok);
    RTTI_CHECK(a2b[1][2] == 6);

    int a3[2][2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
    int a3b[2][2][2] = {};
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(a3), a3b),
                  rtti::error_code::ok);
    RTTI_CHECK(a3b[1][1][1] == 8);

    double ad[2][2] = {{1.5, -2.5}, {3.0, 4e10}};
    double adb[2][2] = {};
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(ad), adb),
                  rtti::error_code::ok);
    RTTI_CHECK(adb[0][0] == 1.5 && adb[1][1] == 4e10);

    RTTI_CHECK_EQ(rtti::to_string(a1), std::string("[1,2,3]"));
}

RTTI_TEST_CASE(StringConvDateTimeRoundTrip) {
    const auto dt = rtti::date_time::from_unix_seconds(std::int64_t(1700000000));
    RTTI_CHECK(dt.ok());
    const std::string iso = rtti::to_string(dt.value);
    RTTI_CHECK(iso.find('T') != std::string::npos);
    RTTI_CHECK(iso.size() >= 20);
    rtti::date_time back{};
    RTTI_CHECK_EQ(rtti::from_string(iso, back), rtti::error_code::ok);
    RTTI_CHECK(back == dt.value);

    // Sub-second precision survives the ISO round trip exactly.
    const auto frac = rtti::date_time::from_unix_ns(std::int64_t(1791290096123456789ll));
    RTTI_CHECK(frac.ok());
    rtti::date_time fback{};
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(frac.value), fback),
                  rtti::error_code::ok);
    RTTI_CHECK(fback == frac.value);

    // Pre-epoch instant.
    const auto pre = rtti::date_time::from_unix_seconds(std::int64_t(-86400));
    RTTI_CHECK(pre.ok());
    rtti::date_time pback{};
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(pre.value), pback),
                  rtti::error_code::ok);
    RTTI_CHECK(pback == pre.value);

    // Numeric epoch input (10 digits = seconds) is accepted too.
    const auto ndt = rtti::date_time::from_unix_seconds(std::int64_t(1000000000));
    RTTI_CHECK(ndt.ok());
    rtti::date_time ndt2;
    RTTI_CHECK_EQ(rtti::from_string(std::string("1000000000"), ndt2),
                  rtti::error_code::ok);
    RTTI_CHECK(ndt2 == ndt.value);

    // Malformed ISO and numeric overflow report errors, never a crash.
    rtti::date_time bad{};
    RTTI_CHECK_EQ(rtti::from_string(std::string("not-a-date"), bad),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string(""), bad),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string("2023-13-99T99:99:99Z"), bad),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string("99999999999999999999999"), bad),
                  rtti::error_code::overflow);
}

RTTI_TEST_CASE(StringConvBaseRoundTrip) {
    const unsigned bases[] = {2, 8, 16, 32, 36, 62, 64};
    const std::uint64_t values[] = {0, 1, 255, 1024, 0xDEADBEEFull,
                                    (std::numeric_limits<std::uint64_t>::max)()};
    for (unsigned base : bases) {
        for (std::uint64_t v : values) {
            const std::string s = rtti::to_string_base(v, base);
            std::uint64_t back = 0;
            RTTI_CHECK_EQ(rtti::from_string_base(s, base, back),
                          rtti::error_code::ok);
            RTTI_CHECK_EQ(back, v);
            const std::optional<std::uint64_t> opt =
                rtti::from_string_base(s, base);
            RTTI_CHECK(opt.has_value());
            RTTI_CHECK_EQ(*opt, v);
        }
    }
    RTTI_CHECK_EQ(rtti::to_string_base(255u, 16), std::string("ff"));
    RTTI_CHECK_EQ(rtti::to_string_base(255u, 2), std::string("11111111"));
    RTTI_CHECK_EQ(rtti::to_string_base_signed(-255, 10), std::string("-255"));
    std::int64_t sback = 0;
    RTTI_CHECK_EQ(rtti::from_string_base_signed(std::string("-255"), 10, sback),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(sback, std::int64_t(-255));

    // Overflow: 2^64 in decimal.
    std::uint64_t u = 0;
    RTTI_CHECK_EQ(rtti::from_string_base(std::string("18446744073709551616"), 10, u),
                  rtti::error_code::overflow);
    // Invalid bases / malformed digit strings.
    RTTI_CHECK_EQ(rtti::to_string_base(255u, 0), std::string());
    RTTI_CHECK_EQ(rtti::to_string_base(255u, 65), std::string());
    RTTI_CHECK_EQ(rtti::from_string_base(std::string("1g"), 16, u),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string_base(std::string(""), 10, u),
                  rtti::error_code::invalid_number);
    RTTI_CHECK(!rtti::from_string_base(std::string("zz"), 10).has_value());
}

RTTI_TEST_CASE(StringConvReflectRoundTrip) {
    StringConvNested in;
    in.id = 42;
    in.name = "node";
    in.nums = {1, 2, 3};
    in.tags = {{"k", 9}, {"j", 8}};

    const std::string s = rtti::to_string(in);
    RTTI_CHECK_EQ(s,
                  std::string("{\"id\":42,\"name\":\"node\",\"nums\":[1,2,3],"
                              "\"tags\":{\"j\":8,\"k\":9}}"));
    // Deterministic: two serializations are byte-identical.
    RTTI_CHECK_EQ(rtti::to_string(in), s);

    StringConvNested out;
    RTTI_CHECK_EQ(rtti::from_string(s, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.id, 42);
    RTTI_CHECK_EQ(out.name, std::string("node"));
    RTTI_CHECK(out.nums == std::vector<int>({1, 2, 3}));
    RTTI_CHECK_EQ(out.tags.size(), std::size_t(2));
    RTTI_CHECK_EQ(out.tags["k"], 9);

    // Key order = declaration order (id before name before nums before tags).
    const std::size_t p_id = s.find("\"id\":");
    const std::size_t p_name = s.find("\"name\":");
    const std::size_t p_nums = s.find("\"nums\":");
    const std::size_t p_tags = s.find("\"tags\":");
    RTTI_CHECK(p_id != std::string::npos && p_name != std::string::npos &&
               p_nums != std::string::npos && p_tags != std::string::npos);
    RTTI_CHECK(p_id < p_name && p_name < p_nums && p_nums < p_tags);

    // Nested reflected fields and escapes.
    StringConvNested esc;
    esc.id = -1;
    esc.name = "a\"b\nc\td\\e";
    esc.nums = {0};
    esc.tags = {};
    const std::string es = rtti::to_string(esc);
    StringConvNested esc_back;
    RTTI_CHECK_EQ(rtti::from_string(es, esc_back), rtti::error_code::ok);
    RTTI_CHECK_EQ(esc_back.name, esc.name);

    // Strict keys: a missing field is an error (not a silent default).
    StringConvNested strict;
    RTTI_CHECK_EQ(rtti::from_string(std::string("{\"id\":1}"), strict),
                  rtti::error_code::missing_key);
    RTTI_CHECK_EQ(rtti::from_string(std::string("{}"), strict),
                  rtti::error_code::missing_key);
}

RTTI_TEST_CASE(StringConvScalarMalformed) {
    int i = 0;
    RTTI_CHECK_EQ(rtti::from_string(std::string("abc"), i),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string("12.5"), i),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string(""), i),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string(" "), i),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string("12 "), i),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string(" 12"), i),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string("99999999999999999999999"), i),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(rtti::from_string(std::string("-2147483649"), i),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(rtti::from_string(std::string("2147483648"), i),
                  rtti::error_code::overflow);

    unsigned u = 0;
    RTTI_CHECK_EQ(rtti::from_string(std::string("-5"), u),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string("4294967296"), u),
                  rtti::error_code::overflow);

    long long ll = 0;
    RTTI_CHECK_EQ(rtti::from_string(std::string("9223372036854775808"), ll),
                  rtti::error_code::overflow);
    signed char sc = 0;
    RTTI_CHECK_EQ(rtti::from_string(std::string("128"), sc),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(rtti::from_string(std::string("127"), sc),
                  rtti::error_code::ok);
    unsigned char uc = 0;
    RTTI_CHECK_EQ(rtti::from_string(std::string("255"), uc),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::from_string(std::string("256"), uc),
                  rtti::error_code::overflow);

    bool b = false;
    RTTI_CHECK_EQ(rtti::from_string(std::string("yes"), b),
                  rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::from_string(std::string(""), b),
                  rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::from_string(std::string("1"), b),
                  rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::from_string(std::string("true"), b),
                  rtti::error_code::ok);
    RTTI_CHECK(b);

    double d = 0.0;
    RTTI_CHECK_EQ(rtti::from_string(std::string("3.14abc"), d),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string("1e999"), d),
                  rtti::error_code::overflow);
    RTTI_CHECK_EQ(rtti::from_string(std::string("1.2.3"), d),
                  rtti::error_code::invalid_number);

    char buf[3] = {0, 0, 0};
    RTTI_CHECK_EQ(rtti::from_string(std::string("ab"), buf),
                  rtti::error_code::ok);
    RTTI_CHECK(buf[0] == 'a' && buf[1] == 'b' && buf[2] == '\0');
    RTTI_CHECK_EQ(rtti::from_string(std::string("abc"), buf),
                  rtti::error_code::kind_mismatch);

    // Optional convenience returns nullopt on failure.
    RTTI_CHECK(!rtti::from_string<int>(std::string("zz")).has_value());
    RTTI_CHECK(!rtti::from_string<std::vector<int>>(std::string("[")).has_value());
}

RTTI_TEST_CASE(StringConvTreeMalformed) {
    std::vector<int> v;
    RTTI_CHECK_EQ(rtti::from_string(std::string(""), v),
                  rtti::error_code::invalid_root);
    RTTI_CHECK_EQ(rtti::from_string(std::string(" "), v),
                  rtti::error_code::invalid_root);
    RTTI_CHECK_EQ(rtti::from_string(std::string("{"), v),
                  rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(rtti::from_string(std::string("[1,"), v),
                  rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(rtti::from_string(std::string("[1,2"), v),
                  rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(rtti::from_string(std::string("[]x"), v),
                  rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(rtti::from_string(std::string("\"abc"), v),
                  rtti::error_code::unterminated_string);
    RTTI_CHECK_EQ(rtti::from_string(std::string("01"), v),
                  rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(rtti::from_string(std::string("tru"), v),
                  rtti::error_code::unexpected_token);
    // Right structure, wrong element kind. Under §23 (type-safe conversion
    // tolerance) a boolean or numeric-string element now converts to int
    // (true -> 1, "1" -> 1) instead of failing; only kinds with no scalar
    // conversion rule still mismatch.
    RTTI_CHECK_EQ(rtti::from_string(std::string("{}"), v),
                  rtti::error_code::kind_mismatch);
    std::vector<int> bool_ok;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[true]"), bool_ok),
                  rtti::error_code::ok);
    RTTI_CHECK(bool_ok.size() == 1 && bool_ok[0] == 1);
    std::vector<int> str_ok;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[\"1\"]"), str_ok),
                  rtti::error_code::ok);
    RTTI_CHECK(str_ok.size() == 1 && str_ok[0] == 1);
    RTTI_CHECK_EQ(rtti::from_string(std::string("[[1,2]]"), v),
                  rtti::error_code::kind_mismatch);
    // An int64-overflow literal demotes to a real node; an int target now
    // reports overflow for the out-of-range magnitude (never a wrap/crash,
    // spec §23).
    RTTI_CHECK_EQ(rtti::from_string(std::string("[99999999999999999999999]"), v),
                  rtti::error_code::overflow);

    // Deeply nested input hits the depth cap instead of overflowing the stack.
    std::string deep;
    for (int i = 0; i < 600; ++i) {
        deep.push_back('[');
    }
    for (int i = 0; i < 600; ++i) {
        deep.push_back(']');
    }
    RTTI_CHECK_EQ(rtti::from_string(deep, v), rtti::error_code::too_deep);

    // String escapes decode back to the original bytes inside containers.
    std::vector<std::string> esc_vec;
    RTTI_CHECK_EQ(rtti::from_string(std::string("[\"a\\\"b\\nc\"]"), esc_vec),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(esc_vec.size(), std::size_t(1));
    RTTI_CHECK_EQ(esc_vec[0], std::string("a\"b\nc"));

    RTTI_CHECK_EQ(rtti::from_string(std::string("\\u00ff"), v),
                  rtti::error_code::unexpected_token);
}

RTTI_TEST_CASE(StringConvFuzzRoundTrip) {
    std::mt19937 rng(std::uint64_t(0x5EEDBEEF));
    std::uniform_int_distribution<int> d_int(-1000, 1000);
    std::uniform_real_distribution<double> d_real(-1.0e6, 1.0e6);
    std::uniform_int_distribution<int> d_small(0, 7);
    static const char kAlphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        " \n\t\"\\{}[],:";
    std::uniform_int_distribution<std::size_t> d_char(0,
                                                      sizeof(kAlphabet) - 2);

    for (int round = 0; round < 600; ++round) {
        StringConvFuzzStruct in;
        in.a = d_int(rng);
        in.b = d_real(rng);
        in.c = (rng() & 1u) != 0;
        const std::size_t n = static_cast<std::size_t>(d_small(rng));
        in.d.clear();
        for (std::size_t k = 0; k < n; ++k) {
            in.d.push_back(kAlphabet[d_char(rng)]);
        }
        in.e.clear();
        const std::size_t ne = static_cast<std::size_t>(d_small(rng));
        for (std::size_t k = 0; k < ne; ++k) {
            in.e.push_back(d_int(rng));
        }
        if ((rng() & 1u) != 0) {
            in.f = d_int(rng);
        } else {
            in.f.reset();
        }
        in.g.x = d_int(rng);
        in.g.y = d_int(rng);

        const std::string s = rtti::to_string(in);
        StringConvFuzzStruct out;
        const rtti::error_code e = rtti::from_string(s, out);
        RTTI_CHECK_EQ(e, rtti::error_code::ok);
        if (e != rtti::error_code::ok) {
            continue;
        }
        RTTI_CHECK(out.a == in.a);
        RTTI_CHECK(out.b == in.b);
        RTTI_CHECK(out.c == in.c);
        RTTI_CHECK(out.d == in.d);
        RTTI_CHECK(out.e == in.e);
        RTTI_CHECK(out.f == in.f);
        RTTI_CHECK(out.g.x == in.g.x);
        RTTI_CHECK(out.g.y == in.g.y);
    }
}

RTTI_TEST_CASE(StringConvFuzzContainers) {
    std::mt19937 rng(std::uint64_t(0xFACE));
    std::uniform_int_distribution<int> d_int(-5000, 5000);
    std::uniform_int_distribution<int> d_small(0, 9);
    for (int round = 0; round < 400; ++round) {
        std::vector<int> v;
        const std::size_t n = static_cast<std::size_t>(d_small(rng));
        for (std::size_t k = 0; k < n; ++k) {
            v.push_back(d_int(rng));
        }
        RTTI_CHECK_EQ(rtti::from_string<std::vector<int>>(rtti::to_string(v)),
                      v);

        std::map<std::string, int> m;
        const std::size_t nm = static_cast<std::size_t>(d_small(rng));
        for (std::size_t k = 0; k < nm; ++k) {
            m["k" + std::to_string(d_int(rng) % 100)] = d_int(rng);
        }
        RTTI_CHECK_EQ((rtti::from_string<std::map<std::string, int>>(
                          rtti::to_string(m))),
                      m);
    }
}

RTTI_TEST_CASE(StringConvNonFiniteContainerRoundTrip) {
    std::vector<double> v{std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity()};
    std::vector<double> back;
    RTTI_CHECK_EQ(rtti::from_string(rtti::to_string(v), back),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(back.size(), std::size_t(3));
    RTTI_CHECK(std::isnan(back[0]));
    RTTI_CHECK(std::isinf(back[1]) && back[1] > 0);
    RTTI_CHECK(std::isinf(back[2]) && back[2] < 0);
}

RTTI_TEST_CASE(StringConvUnregisteredEnum) {
    // An enum without a label table is not a supported type: empty string on
    // the way out, kind_mismatch on the way in.
    enum class StringConvUnregEnum { A = 1 };
    RTTI_CHECK_EQ(rtti::to_string(StringConvUnregEnum::A), std::string());
    StringConvUnregEnum out = StringConvUnregEnum::A;
    RTTI_CHECK_EQ(rtti::from_string(std::string("A"), out),
                  rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(StringConvStringScalarMinimalText) {
    // std::string is a scalar: minimal text (raw bytes, no JSON quoting).
    const std::string special("quote\" newline\n tab\t slash\\ nul\0end", 27);
    RTTI_CHECK_EQ(rtti::to_string(special), special);
    RTTI_CHECK_EQ(rtti::from_string<std::string>(rtti::to_string(special)),
                  special);
    RTTI_CHECK_EQ(rtti::to_string(std::string("hello")), std::string("hello"));
}

RTTI_TEST_CASE(StringConvDeterministicFormat) {
    RTTI_CHECK_EQ(rtti::to_string(std::vector<int>{1, 2, 3}),
                  std::string("[1,2,3]"));
    RTTI_CHECK_EQ(rtti::to_string(true), std::string("true"));
    RTTI_CHECK_EQ(rtti::to_string(42), std::string("42"));
    RTTI_CHECK_EQ(rtti::to_string(-42), std::string("-42"));
    RTTI_CHECK_EQ(rtti::to_string(std::optional<int>()), std::string("null"));
    std::map<std::string, int> m;
    m["a"] = 1;
    m["b"] = 2;
    RTTI_CHECK_EQ(rtti::to_string(m), std::string("{\"a\":1,\"b\":2}"));
    RTTI_CHECK_EQ(rtti::to_string(std::vector<std::string>{"x", "y", "z"}),
                  std::string("[\"x\",\"y\",\"z\"]"));
    // Escaped JSON string inside a container (not minimal text).
    RTTI_CHECK_EQ(rtti::to_string(std::vector<std::string>{"a\"b"}),
                  std::string("[\"a\\\"b\"]"));
}

RTTI_TEST_CASE(StringConvSharedStreamDotZero) {
    // A real whose shortest form is integral keeps its ".0" even when an
    // earlier element already put a '.' into the shared output stream
    // (previously the whole-buffer search missed the trailing ".0").
    RTTI_CHECK_EQ(rtti::to_string(std::vector<double>{1.5, 2.0}),
                  std::string("[1.5,2.0]"));
    RTTI_CHECK_EQ(rtti::to_string(std::vector<double>{2.0, 1.5}),
                  std::string("[2.0,1.5]"));
    RTTI_CHECK_EQ(rtti::to_string(std::vector<double>{1.0, 2.0, 3.0}),
                  std::string("[1.0,2.0,3.0]"));
    RTTI_CHECK_EQ(rtti::to_string(std::vector<double>{-3.0, 0.0}),
                  std::string("[-3.0,0.0]"));
    // Exponent-form shortest values stay untouched.
    const std::string big = rtti::to_string(std::vector<double>{1.5, 1.0e300});
    RTTI_CHECK(big.find("1.5") != std::string::npos);
    RTTI_CHECK(big.find("e+") != std::string::npos);
}

// ---------------------------------------------------------------------------
// R2-1: a failed serialization must not leak detached text_node children.
// The to_value writer (convert.hpp reflect_to / sequence_to / map_to / ...)
// builds each child through value_io::make_node() and attaches it with
// set_key/push_back only after the child conversion succeeded. On failure the
// function returns the error without attaching, so the child node must still
// be reclaimed with the enclosing tree. Without the ownership fix every failed
// conversion abandons one heap text_node (the global counter below grows by
// one per iteration); with it, the live balance returns to the snapshot.
// ---------------------------------------------------------------------------
RTTI_TEST_CASE(StringConvNoLeakOnFailedSerialize) {
    using rtti_test_lib::StringConvLeakyHolder;
    using rtti_test_lib::StringConvLeakyEnum;
    StringConvLeakyHolder h;
    h.bad = static_cast<StringConvLeakyEnum>(7); // value has no label

    // The failure path must actually fail (empty output).
    RTTI_CHECK_EQ(rtti::to_string(h), std::string());

    // Warm-up: any lazily-built reflection tables (derived_pool etc.) are
    // cached forever, so let them finish allocating before the snapshot so the
    // per-iteration delta measures only per-call allocations.
    for (int i = 0; i < 3; ++i) {
        RTTI_CHECK_EQ(rtti::to_string(h), std::string());
    }

    const long long snap = rtti_test_lib::g_rttileak_live();
    constexpr int kIters = 2000;
    for (int i = 0; i < kIters; ++i) {
        RTTI_CHECK_EQ(rtti::to_string(h), std::string());
    }
    // A leak leaves kIters live text_node allocations behind.
    RTTI_CHECK_EQ(rtti_test_lib::g_rttileak_live(), snap);
}

} // namespace rtti_test_lib