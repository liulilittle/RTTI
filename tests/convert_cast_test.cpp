// Numeric-cast bridge tests (rtti::convert::as / cast / to).
// Covers the value-semantics strong-conversion API added to convert.hpp:
// optional<T> as(Src&&), result<T> cast(Src&&), result<T> to(Src&&).
// The bridge must coexist with the existing pointer rtti::as<T>(U*) /
// rtti::cast<T>(U*) without ambiguity.

#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include <rtti/to_string.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "box.hpp"

// Registered enum for the label/name bridge (mirrors ConvertColor in
// convert_test.cpp but in its own TU).
enum class ConvertCastColor { Red, Green, Blue };

RTTI_DEFINE_ENUM(ConvertCastColor, Red, Green, Blue)

// Unregistered enum: the bridge must report kind_mismatch, never fabricate a
// table or reinterpret the integer lane as a label.
enum class ConvertCastPlain { Alpha, Beta };

// Plain class hierarchy for the pointer-overload coexistence probe.
struct ConvertCastBase {};
struct ConvertCastDerived : ConvertCastBase {};

namespace rtti_test_lib {

// --- string -> numeric (loose) ---------------------------------------------
RTTI_TEST_CASE(convert_cast_string_to_int) {
    RTTI_CHECK_EQ(rtti::convert::as<int>("42").value(), 42);
    RTTI_CHECK(!rtti::convert::as<int>("abc").has_value());
    RTTI_CHECK_EQ(rtti::convert::as<int>(" 42 ").value(), 42);
    // A fractional text token reaches an integer target via truncation.
    RTTI_CHECK_EQ(rtti::convert::as<int>("3.9").value(), 3);
    // Empty / malformed text stays nullopt.
    RTTI_CHECK(!rtti::convert::as<int>("").has_value());
}

RTTI_TEST_CASE(convert_cast_string_to_real) {
    const double v = rtti::convert::as<double>("3.14").value();
    RTTI_CHECK(v > 3.13 && v < 3.15);
    RTTI_CHECK(!rtti::convert::as<double>("abc").has_value());
    // Deterministic non-finite tokens round trip like the library serializer.
    RTTI_CHECK(rtti::convert::as<double>("inf").value() ==
               (std::numeric_limits<double>::infinity)());
}

// --- strict cast: distinguishable error codes ------------------------------
RTTI_TEST_CASE(convert_cast_strict_error_codes) {
    const auto ok = rtti::convert::cast<int>("42");
    RTTI_CHECK(ok.ok());
    RTTI_CHECK_EQ(ok.value, 42);

    // Malformed numeric text: the bridge reports the library's documented
    // invalid_number code (the ill-formed-numeric-literal lane), not a
    // silent zero.
    const auto bad = rtti::convert::cast<int>("abc");
    RTTI_CHECK(!bad.ok());
    RTTI_CHECK(bad.err.code == rtti::error_code::invalid_number);

    // Out-of-range across a narrow integer lane: overflow, never a wrap.
    const auto ov = rtti::convert::cast<std::int8_t>(std::int64_t(300));
    RTTI_CHECK(!ov.ok());
    RTTI_CHECK(ov.err.code == rtti::error_code::overflow);
    const auto ov2 = rtti::convert::cast<std::int8_t>("300");
    RTTI_CHECK(ov2.err.code == rtti::error_code::overflow);

    // Null source: no_such_field.
    const auto nul = rtti::convert::cast<int>(nullptr);
    RTTI_CHECK(nul.err.code == rtti::error_code::no_such_field);
}

// --- int <-> bool ----------------------------------------------------------
RTTI_TEST_CASE(convert_cast_int_bool) {
    RTTI_CHECK(rtti::convert::as<bool>("true").value());
    RTTI_CHECK(!rtti::convert::as<bool>("false").value());
    RTTI_CHECK(rtti::convert::as<bool>(" 1 ").value());
    RTTI_CHECK(!rtti::convert::as<bool>("0").value());
    RTTI_CHECK(rtti::convert::as<bool>(1).value());
    RTTI_CHECK(!rtti::convert::as<bool>(0).value());
    RTTI_CHECK_EQ(rtti::convert::as<int>(true).value(), 1);
    RTTI_CHECK_EQ(rtti::convert::as<int>(false).value(), 0);
    RTTI_CHECK(!rtti::convert::as<bool>("maybe").has_value());
}

// --- real -> int: truncate toward zero, range-checked ----------------------
RTTI_TEST_CASE(convert_cast_real_to_int) {
    RTTI_CHECK_EQ(rtti::convert::cast<int>(3.9).value, 3);
    RTTI_CHECK_EQ(rtti::convert::cast<int>(-3.9).value, -3);
    const auto ov = rtti::convert::cast<std::int8_t>(1.0e10);
    RTTI_CHECK(ov.err.code == rtti::error_code::overflow);
    const auto nan = rtti::convert::cast<std::int8_t>(
        (std::numeric_limits<double>::quiet_NaN)());
    RTTI_CHECK(nan.err.code == rtti::error_code::overflow);
}

// --- field_value source ----------------------------------------------------
RTTI_TEST_CASE(convert_cast_field_value) {
    RTTI_CHECK_EQ(rtti::convert::as<std::string_view>(
                      rtti::field_value::of(std::string_view("x")))
                      .value(),
                  std::string_view("x"));
    RTTI_CHECK_EQ(rtti::convert::as<int>(rtti::field_value::of(std::int64_t(7)))
                      .value(),
                  7);
    // bool -> int through the field_value lane is the specified int<->bool
    // rule, not a kind mismatch.
    RTTI_CHECK_EQ(
        rtti::convert::cast<int>(rtti::field_value::of(true)).value, 1);
    // Boolean has no real lane (matches the value_io converter contract).
    const auto br = rtti::convert::cast<double>(rtti::field_value::of(true));
    RTTI_CHECK(br.err.code == rtti::error_code::kind_mismatch);
    // Boolean has no text lane either.
    const auto bs = rtti::convert::cast<std::string>(rtti::field_value::of(true));
    RTTI_CHECK(bs.err.code == rtti::error_code::kind_mismatch);
    // An object/none field_value carries no scalar lane.
    const auto obj = rtti::convert::cast<int>(rtti::field_value::of_object());
    RTTI_CHECK(obj.err.code == rtti::error_code::kind_mismatch);
    // String field_value to string target is direct.
    RTTI_CHECK_EQ(rtti::convert::as<std::string>(
                      rtti::field_value::of(std::string_view("hello")))
                      .value(),
                  std::string("hello"));
}

// --- value_io* source ------------------------------------------------------
RTTI_TEST_CASE(convert_cast_value_io) {
    box* int_node = make_int(7);
    RTTI_CHECK_EQ(rtti::convert::cast<int>(int_node).value, 7);
    // int node -> string target renders decimal text (spec int->string lane).
    box* int42 = make_int(42);
    RTTI_CHECK_EQ(rtti::convert::cast<std::string>(int42).value,
                  std::string("42"));
    delete int42;
    // real node -> int target truncates.
    box* real_node = new box;
    real_node->make_scalar(rtti::field_value::of(3.9));
    RTTI_CHECK_EQ(rtti::convert::cast<int>(real_node).value, 3);
    delete real_node;
    // string node -> int parses; malformed text is invalid_number.
    box* str_node = make_str("55");
    RTTI_CHECK_EQ(rtti::convert::cast<int>(str_node).value, 55);
    delete str_node;
    box* bad_node = make_str("abc");
    const auto bad = rtti::convert::cast<int>(bad_node);
    RTTI_CHECK(bad.err.code == rtti::error_code::invalid_number);
    delete bad_node;
    // Null value_io source -> no_such_field.
    const auto null = rtti::convert::cast<int>(static_cast<box*>(nullptr));
    RTTI_CHECK(null.err.code == rtti::error_code::no_such_field);
    // A null-kind node is a null source too.
    box* null_node = new box;
    const auto nn = rtti::convert::cast<int>(null_node);
    RTTI_CHECK(nn.err.code == rtti::error_code::no_such_field);
    delete null_node;
    delete int_node;
}

// --- enum target -----------------------------------------------------------
RTTI_TEST_CASE(convert_cast_enum) {
    RTTI_CHECK_EQ(
        rtti::convert::as<ConvertCastColor>("Green").value(),
        ConvertCastColor::Green);
    RTTI_CHECK_EQ(
        rtti::convert::as<ConvertCastColor>(std::int64_t(2)).value(),
        ConvertCastColor::Blue);
    // Unknown label text is a parse error, not a silent cast to 0.
    const auto bad = rtti::convert::cast<ConvertCastColor>("Nope");
    RTTI_CHECK(bad.err.code == rtti::error_code::invalid_number);
    // Out-of-range integer lane for the enum's underlying type: overflow.
    // (An in-range integer that matches no label is an unnamed enumerator;
    //  only a value outside the int underlying lane is rejected.)
    const auto ov = rtti::convert::cast<ConvertCastColor>(std::int64_t(3000000000));
    RTTI_CHECK(ov.err.code == rtti::error_code::overflow);
    // Unregistered enums are unsupported targets: kind_mismatch.
    const auto plain = rtti::convert::cast<ConvertCastPlain>(std::int64_t(1));
    RTTI_CHECK(plain.err.code == rtti::error_code::kind_mismatch);
}

// --- std::vector<scalar> target --------------------------------------------
RTTI_TEST_CASE(convert_cast_vector) {
    // A single scalar source wraps into a one-element vector.
    const auto wrapped = rtti::convert::as<std::vector<int>>(42);
    RTTI_CHECK(wrapped.has_value());
    RTTI_CHECK_EQ(wrapped->size(), std::size_t(1));
    RTTI_CHECK_EQ((*wrapped)[0], 42);

    // An array source converts element by element.
    box* arr = make_arr();
    arr->push_back(make_int(1));
    arr->push_back(make_int(2));
    arr->push_back(make_int(3));
    const auto v = rtti::convert::as<std::vector<int>>(arr);
    RTTI_CHECK(v.has_value());
    RTTI_CHECK_EQ(v->size(), std::size_t(3));
    RTTI_CHECK_EQ((*v)[0], 1);
    RTTI_CHECK_EQ((*v)[2], 3);
    delete arr;

    // Element failure propagates its error code.
    box* bad_arr = make_arr();
    bad_arr->push_back(make_int(4));
    bad_arr->push_back(make_str("abc"));
    const auto eb = rtti::convert::cast<std::vector<int>>(bad_arr);
    RTTI_CHECK(!eb.ok());
    RTTI_CHECK(eb.err.code == rtti::error_code::invalid_number);
    delete bad_arr;

    // String elements ride the same vector lane.
    box* str_arr = make_arr();
    str_arr->push_back(make_str("a"));
    str_arr->push_back(make_str("b"));
    const auto sv = rtti::convert::as<std::vector<std::string>>(str_arr);
    RTTI_CHECK(sv.has_value());
    RTTI_CHECK_EQ(sv->size(), std::size_t(2));
    RTTI_CHECK_EQ((*sv)[0], std::string("a"));
    delete str_arr;
}

// --- int -> string formatting ----------------------------------------------
RTTI_TEST_CASE(convert_cast_int_to_string) {
    RTTI_CHECK_EQ(rtti::convert::as<std::string>(42).value(),
                  std::string("42"));
    RTTI_CHECK_EQ(rtti::convert::as<std::string>(-7).value(),
                  std::string("-7"));
    // uint64 above INT64_MAX formats exactly (no double rounding).
    const std::uint64_t big = (std::numeric_limits<std::uint64_t>::max)();
    RTTI_CHECK_EQ(rtti::convert::as<std::string>(big).value(),
                  std::string("18446744073709551615"));
    RTTI_CHECK_EQ(rtti::convert::as<std::string>("hello").value(),
                  std::string("hello"));
}

// --- to() is the cast() alias ----------------------------------------------
RTTI_TEST_CASE(convert_cast_to_alias) {
    const auto a = rtti::convert::to<int>("42");
    RTTI_CHECK(a.ok());
    RTTI_CHECK_EQ(a.value, 42);
    const auto b = rtti::convert::to<int>("abc");
    RTTI_CHECK(b.err.code == rtti::error_code::invalid_number);
}

// --- pointer overload coexistence ------------------------------------------
RTTI_TEST_CASE(convert_cast_pointer_overload_coexist) {
    ConvertCastDerived d;
    ConvertCastBase* bp = rtti::as<ConvertCastBase>(&d);
    RTTI_CHECK(bp != nullptr);
    const rtti::result<ConvertCastBase*> rc = rtti::cast<ConvertCastBase>(&d);
    RTTI_CHECK(rc.ok());
    RTTI_CHECK(rc.value != nullptr);
    // The value bridge is a distinct overload space: it neither shadows the
    // pointer path nor is shadowed by it.
    RTTI_CHECK_EQ(rtti::convert::as<int>("7").value(), 7);
}

} // namespace rtti_test_lib
