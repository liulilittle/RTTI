// protobuf 2/3 wire-format adapter tests for the RTTI reflect-core library.
// Covers the varint/fixed32/fixed64/length-delimited wire codecs (wire types
// 0/1/2/5), proto2 optional/required/default handling, proto3 scalar default
// omission, repeated packed/unpacked, nested messages, malformed-wire error
// paths (truncated varint / overlong length / unknown field skip / wrong wire
// type), SAX streaming events, deep-nesting DoS guards and the value_io tree
// bridge (C++ <-> wire <-> JSON documents). English comments only.

#include "test_harness.hpp"
#include <rtti/proto.hpp>
#include <rtti/convert.hpp>
#include <rtti/error.hpp>

#if defined(RTTI_ENABLE_JSON) && RTTI_ENABLE_JSON
#include <rtti/json.hpp>
#endif

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace rtti_test_lib {

enum class ProtoColor : int { red = 0, green = 1, blue = 2 };

// proto3 scalar field set (all four wire kinds represented).
struct ProtoScalar3 {
    bool flag = false;
    std::int32_t i32 = 0;
    std::int64_t i64 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    double d = 0.0;
    float f = 0.0f;
    std::string s;
    ProtoColor color = ProtoColor::red;

    RTTI_DEFINE_TYPE_INTRUSIVE(ProtoScalar3, flag, i32, i64, u32, u64, d, f, s,
                               color)
};

// proto3 packed repeated scalars.
struct ProtoNums {
    std::vector<std::int32_t> nums;
    std::vector<std::int64_t> big;

    RTTI_DEFINE_TYPE_INTRUSIVE(ProtoNums, nums, big)
};

// nested message + repeated message member.
struct ProtoNested {
    std::int32_t id = 0;
    ProtoScalar3 inner;
    std::vector<ProtoScalar3> items;
    std::string name;

    RTTI_DEFINE_TYPE_INTRUSIVE(ProtoNested, id, inner, items, name)
};

// self-referential message for the deep-nesting DoS guard.
struct ProtoNode {
    std::int32_t id = 0;
    std::vector<ProtoNode> children;
};

// proto2 required + optional-with-explicit-default field set.
struct ProtoReq {
    std::int32_t id = 0;
    std::string name;
    std::int32_t age = 0;
    bool active = false;
    double weight = 0.0;

    RTTI_DEFINE_TYPE_INTRUSIVE(ProtoReq, id, name, age, active, weight)
};

// protobuf map (repeated entry messages: key=1 / value=2).
struct ProtoMap {
    std::map<std::string, std::int32_t> attrs;

    RTTI_DEFINE_TYPE_INTRUSIVE(ProtoMap, attrs)
};

// sint32/sint64 (zig-zag) varint members plus a plain int64 control.
struct ProtoZigzag {
    std::int64_t z = 0;     // sint64, field 2
    std::int32_t s = 0;     // sint32, field 3
    std::int64_t plain = 0; // int64, field 4 (plain varint control)

    RTTI_DEFINE_TYPE_INTRUSIVE(ProtoZigzag, z, s, plain)
};

bool operator==(const ProtoScalar3& a, const ProtoScalar3& b) {
    return a.flag == b.flag && a.i32 == b.i32 && a.i64 == b.i64 &&
           a.u32 == b.u32 && a.u64 == b.u64 && a.d == b.d && a.f == b.f &&
           a.s == b.s && a.color == b.color;
}
bool operator==(const ProtoNums& a, const ProtoNums& b) {
    return a.nums == b.nums && a.big == b.big;
}
bool operator==(const ProtoNested& a, const ProtoNested& b) {
    return a.id == b.id && a.inner == b.inner && a.items == b.items &&
           a.name == b.name;
}
bool operator==(const ProtoReq& a, const ProtoReq& b) {
    return a.id == b.id && a.name == b.name && a.age == b.age &&
           a.active == b.active && a.weight == b.weight;
}
bool operator==(const ProtoZigzag& a, const ProtoZigzag& b) {
    return a.z == b.z && a.s == b.s && a.plain == b.plain;
}

inline rtti::string_view to_sv(const std::string& s) {
    return rtti::string_view(s.data(), s.size());
}

} // namespace rtti_test_lib

RTTI_DEFINE_ENUM(rtti_test_lib::ProtoColor, red, green, blue)
RTTI_DEFINE_TYPE(rtti_test_lib::ProtoNode, id, children)

namespace rtti {
namespace proto {

template <>
struct schema<rtti_test_lib::ProtoScalar3> {
    static constexpr std::size_t count = 9;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::flag>(
            make_field_desc("flag", 1, wire_type::varint)),
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::i32>(
            make_field_desc("i32", 2, wire_type::varint)),
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::i64>(
            make_field_desc("i64", 3, wire_type::varint)),
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::u32>(
            make_field_desc("u32", 4, wire_type::varint)),
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::u64>(
            make_field_desc("u64", 5, wire_type::varint)),
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::d>(
            make_field_desc("d", 6, wire_type::fixed64)),
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::f>(
            make_field_desc("f", 7, wire_type::fixed32)),
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::s>(
            make_field_desc("s", 8, wire_type::length)),
        make_field<rtti_test_lib::ProtoScalar3,
                   &rtti_test_lib::ProtoScalar3::color>(
            make_field_desc("color", 9, wire_type::varint)),
    };
};

template <>
struct schema<rtti_test_lib::ProtoNums> {
    static constexpr std::size_t count = 2;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<rtti_test_lib::ProtoNums, &rtti_test_lib::ProtoNums::nums>(
            make_field_desc("nums", 1, wire_type::varint, field_mode::repeated,
                            true)),
        make_field<rtti_test_lib::ProtoNums, &rtti_test_lib::ProtoNums::big>(
            make_field_desc("big", 2, wire_type::varint, field_mode::repeated,
                            true)),
    };
};

template <>
struct schema<rtti_test_lib::ProtoNested> {
    static constexpr std::size_t count = 4;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<rtti_test_lib::ProtoNested,
                   &rtti_test_lib::ProtoNested::id>(
            make_field_desc("id", 1, wire_type::varint)),
        make_field<rtti_test_lib::ProtoNested,
                   &rtti_test_lib::ProtoNested::inner>(
            make_field_desc("inner", 2, wire_type::length)),
        make_field<rtti_test_lib::ProtoNested,
                   &rtti_test_lib::ProtoNested::items>(
            make_field_desc("items", 3, wire_type::length,
                            field_mode::repeated)),
        make_field<rtti_test_lib::ProtoNested,
                   &rtti_test_lib::ProtoNested::name>(
            make_field_desc("name", 4, wire_type::length)),
    };
};

template <>
struct schema<rtti_test_lib::ProtoNode> {
    static constexpr std::size_t count = 2;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<rtti_test_lib::ProtoNode, &rtti_test_lib::ProtoNode::id>(
            make_field_desc("id", 1, wire_type::varint)),
        make_field<rtti_test_lib::ProtoNode,
                   &rtti_test_lib::ProtoNode::children>(
            make_field_desc("children", 2, wire_type::length,
                            field_mode::repeated)),
    };
};

template <>
struct schema<rtti_test_lib::ProtoReq> {
    static constexpr std::size_t count = 5;
    static constexpr syntax syn = syntax::proto2;
    static inline const field_base* const array[count] = {
        make_field<rtti_test_lib::ProtoReq, &rtti_test_lib::ProtoReq::id>(
            make_field_desc("id", 1, wire_type::varint,
                            field_mode::required)),
        make_field<rtti_test_lib::ProtoReq, &rtti_test_lib::ProtoReq::name>(
            with_default_str(make_field_desc("name", 2, wire_type::length),
                             "anon", 4)),
        make_field<rtti_test_lib::ProtoReq, &rtti_test_lib::ProtoReq::age>(
            with_default_int(make_field_desc("age", 3, wire_type::varint), 42)),
        make_field<rtti_test_lib::ProtoReq,
                   &rtti_test_lib::ProtoReq::active>(
            with_default_int(make_field_desc("active", 4, wire_type::varint),
                             1)),
        make_field<rtti_test_lib::ProtoReq,
                   &rtti_test_lib::ProtoReq::weight>(
            with_default_real(make_field_desc("weight", 5, wire_type::fixed64),
                              1.5)),
    };
};

template <>
struct schema<rtti_test_lib::ProtoMap> {
    static constexpr std::size_t count = 1;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<rtti_test_lib::ProtoMap, &rtti_test_lib::ProtoMap::attrs>(
            make_field_desc("attrs", 1, wire_type::length,
                            field_mode::repeated)),
    };
};

template <>
struct schema<rtti_test_lib::ProtoZigzag> {
    static constexpr std::size_t count = 3;
    static constexpr syntax syn = syntax::proto3;
    static inline const field_base* const array[count] = {
        make_field<rtti_test_lib::ProtoZigzag,
                   &rtti_test_lib::ProtoZigzag::z>(
            with_zigzag(make_field_desc("z", 2, wire_type::varint))),
        make_field<rtti_test_lib::ProtoZigzag,
                   &rtti_test_lib::ProtoZigzag::s>(
            with_zigzag(make_field_desc("s", 3, wire_type::varint))),
        make_field<rtti_test_lib::ProtoZigzag,
                   &rtti_test_lib::ProtoZigzag::plain>(
            make_field_desc("plain", 4, wire_type::varint)),
    };
};

} // namespace proto
} // namespace rtti

namespace rtti_test_lib {

RTTI_TEST_CASE(ProtoVarintBoundaries) {
    std::string out;
    RTTI_CHECK(rtti::proto::encode_varint(0, out));
    RTTI_CHECK_EQ(out.size(), size_t(1));
    RTTI_CHECK_EQ((int)(unsigned char)out[0], 0);

    out.clear();
    RTTI_CHECK(rtti::proto::encode_varint(1, out));
    RTTI_CHECK_EQ((int)(unsigned char)out[0], 1);

    out.clear();
    RTTI_CHECK(rtti::proto::encode_varint(127, out));
    RTTI_CHECK_EQ(out.size(), size_t(1));
    RTTI_CHECK_EQ((int)(unsigned char)out[0], 127);

    out.clear();
    RTTI_CHECK(rtti::proto::encode_varint(128, out));
    RTTI_CHECK_EQ(out.size(), size_t(2));
    RTTI_CHECK_EQ((int)(unsigned char)out[0], 0x80);
    RTTI_CHECK_EQ((int)(unsigned char)out[1], 0x01);

    out.clear();
    RTTI_CHECK(rtti::proto::encode_varint(16384, out));
    RTTI_CHECK_EQ(out.size(), size_t(3));
    RTTI_CHECK_EQ((int)(unsigned char)out[0], 0x80);
    RTTI_CHECK_EQ((int)(unsigned char)out[1], 0x80);
    RTTI_CHECK_EQ((int)(unsigned char)out[2], 0x01);

    out.clear();
    RTTI_CHECK(rtti::proto::encode_varint(0xFFFFFFFFFFFFFFFFull, out));
    RTTI_CHECK_EQ(out.size(), size_t(10));
    for (std::size_t i = 0; i < 9; ++i) {
        RTTI_CHECK_EQ((int)(unsigned char)out[i], 0xFF);
    }
    RTTI_CHECK_EQ((int)(unsigned char)out[9], 0x01);

    std::uint64_t v = 0;
    std::size_t used = 0;
    RTTI_CHECK(rtti::proto::decode_varint(rtti::string_view("\x00", 1), v, used));
    RTTI_CHECK_EQ(v, uint64_t(0));
    RTTI_CHECK_EQ(used, size_t(1));

    RTTI_CHECK(rtti::proto::decode_varint(rtti::string_view("\x7F", 1), v, used));
    RTTI_CHECK_EQ(v, uint64_t(127));

    RTTI_CHECK(rtti::proto::decode_varint(rtti::string_view("\x80\x01", 2), v, used));
    RTTI_CHECK_EQ(v, uint64_t(128));

    RTTI_CHECK(rtti::proto::decode_varint(rtti::string_view("\x80\x80\x01", 3), v, used));
    RTTI_CHECK_EQ(v, uint64_t(16384));

    RTTI_CHECK(rtti::proto::decode_varint(
        rtti::string_view("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01", 10), v,
        used));
    RTTI_CHECK_EQ(v, 0xFFFFFFFFFFFFFFFFull);
    RTTI_CHECK_EQ(used, size_t(10));

    // Truncated: continuation bit set at EOF.
    RTTI_CHECK(!rtti::proto::decode_varint(rtti::string_view("\x80", 1), v, used));
    // 10 bytes with the last byte still continuing -> truncated.
    RTTI_CHECK(!rtti::proto::decode_varint(
        rtti::string_view("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 10), v,
        used));
    // 10th byte carrying bits beyond the 64-bit range -> invalid.
    RTTI_CHECK(!rtti::proto::decode_varint(
        rtti::string_view("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F", 10), v,
        used));
}

RTTI_TEST_CASE(ProtoFixedCodec) {
    ProtoScalar3 a;
    a.d = 1.5;
    const std::string w1 = rtti::proto::serialize(a);
    RTTI_CHECK_EQ(w1.size(), size_t(9));
    RTTI_CHECK_EQ((int)(unsigned char)w1[0], 0x31); // field 6, fixed64
    const auto b1 = rtti::proto::parse_as<ProtoScalar3>(to_sv(w1));
    RTTI_CHECK(b1.ok());
    RTTI_CHECK_EQ((*b1).d, 1.5);

    ProtoScalar3 f;
    f.f = 2.25f;
    const std::string w2 = rtti::proto::serialize(f);
    RTTI_CHECK_EQ(w2.size(), size_t(5));
    RTTI_CHECK_EQ((int)(unsigned char)w2[0], 0x3D); // field 7, fixed32
    const auto b2 = rtti::proto::parse_as<ProtoScalar3>(to_sv(w2));
    RTTI_CHECK(b2.ok());
    RTTI_CHECK_EQ((*b2).f, 2.25f);
}

RTTI_TEST_CASE(ProtoLengthPrefix) {
    ProtoScalar3 a;
    a.s = "";
    const std::string w1 = rtti::proto::serialize(a);
    // proto3: an empty string is a default value and is omitted on the wire.
    RTTI_CHECK_EQ(w1.size(), size_t(0));
    // An explicit empty length-delimited field decodes to "".
    const auto b0 = rtti::proto::parse_as<ProtoScalar3>(
        rtti::string_view("\x42\x00", 2));
    RTTI_CHECK(b0.ok());
    RTTI_CHECK_EQ((*b0).s, std::string(""));

    a.s = "hello";
    const std::string w2 = rtti::proto::serialize(a);
    RTTI_CHECK_EQ(w2.size(), size_t(7));
    RTTI_CHECK_EQ((int)(unsigned char)w2[0], 0x42);
    RTTI_CHECK_EQ((int)(unsigned char)w2[1], 5);
    const auto b2 = rtti::proto::parse_as<ProtoScalar3>(to_sv(w2));
    RTTI_CHECK(b2.ok());
    RTTI_CHECK_EQ((*b2).s, std::string("hello"));
}

RTTI_TEST_CASE(ProtoScalarRoundtrip) {
    ProtoScalar3 a;
    a.flag = true;
    a.i32 = -7;
    a.i64 = -(std::int64_t(1) << 40);
    a.u32 = 4000000000u;
    a.u64 = std::uint64_t(1) << 63;
    a.d = 3.14;
    a.f = -0.5f;
    a.s = "proto";
    a.color = ProtoColor::blue;
    const auto r = rtti::proto::parse_as<ProtoScalar3>(to_sv(rtti::proto::serialize(a)));
    RTTI_CHECK(r.ok());
    RTTI_CHECK(*r == a);
}

RTTI_TEST_CASE(ProtoMapField) {
    ProtoMap m;
    m.attrs["a"] = 1;
    m.attrs["b"] = 300;
    const auto r = rtti::proto::parse_as<ProtoMap>(
        to_sv(rtti::proto::serialize(m)));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ((*r).attrs.size(), size_t(2));
    RTTI_CHECK_EQ((*r).attrs.at("a"), 1);
    RTTI_CHECK_EQ((*r).attrs.at("b"), 300);
}

RTTI_TEST_CASE(Proto3DefaultOmission) {
    const ProtoScalar3 z;
    const std::string w = rtti::proto::serialize(z);
    RTTI_CHECK_EQ(w.size(), size_t(0));
    const auto r = rtti::proto::parse_as<ProtoScalar3>(rtti::string_view("", 0));
    RTTI_CHECK(r.ok());
    RTTI_CHECK(*r == z);

    ProtoScalar3 c;
    c.color = ProtoColor::blue;
    const std::string wc = rtti::proto::serialize(c);
    RTTI_CHECK_EQ(wc.size(), size_t(2));
    RTTI_CHECK_EQ((int)(unsigned char)wc[0], 0x48); // field 9, varint
    RTTI_CHECK_EQ((int)(unsigned char)wc[1], 2);
}

RTTI_TEST_CASE(ProtoPackedRepeated) {
    ProtoNums n;
    n.nums = {1, 150, 300};
    const std::string w = rtti::proto::serialize(n);
    // packed field 1: 0x0A 0x05 0x01 0x96 0x01 0xAC 0x02
    RTTI_CHECK_EQ(w.size(), size_t(7));
    static const unsigned char exp[] = {0x0A, 0x05, 0x01, 0x96,
                                        0x01, 0xAC, 0x02};
    RTTI_CHECK_EQ(std::memcmp(w.data(), exp, 7), 0);
    const auto r = rtti::proto::parse_as<ProtoNums>(to_sv(w));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ((*r).nums.size(), size_t(3));
    RTTI_CHECK_EQ((*r).nums[0], 1);
    RTTI_CHECK_EQ((*r).nums[1], 150);
    RTTI_CHECK_EQ((*r).nums[2], 300);

    // An unpacked (per-element) stream still fills the same repeated field.
    static const unsigned char un[] = {0x08, 0x01, 0x08, 0x96,
                                       0x01, 0x08, 0xAC, 0x02};
    const auto u = rtti::proto::parse_as<ProtoNums>(
        rtti::string_view(reinterpret_cast<const char*>(un), sizeof(un)));
    RTTI_CHECK(u.ok());
    RTTI_CHECK_EQ((*u).nums[0], 1);
    RTTI_CHECK_EQ((*u).nums[1], 150);
    RTTI_CHECK_EQ((*u).nums[2], 300);

    // Negative int32 values pack as sign-extended 64-bit varints.
    n.nums = {-1, -2};
    const std::string wn = rtti::proto::serialize(n);
    RTTI_CHECK_EQ(wn.size(), size_t(22)); // tag(1)+len + 2*10 payload
    const auto rn = rtti::proto::parse_as<ProtoNums>(to_sv(wn));
    RTTI_CHECK(rn.ok());
    RTTI_CHECK_EQ((*rn).nums[0], -1);
    RTTI_CHECK_EQ((*rn).nums[1], -2);
}

RTTI_TEST_CASE(ProtoNestedMessage) {
    ProtoNested m;
    m.id = 5;
    m.inner.i32 = 7;
    m.inner.s = "in";
    m.inner.color = ProtoColor::green;
    m.items.push_back(m.inner);
    m.items.emplace_back(); // default message element (empty payload)
    m.name = "top";
    const auto r = rtti::proto::parse_as<ProtoNested>(to_sv(rtti::proto::serialize(m)));
    RTTI_CHECK(r.ok());
    RTTI_CHECK(*r == m);
}

RTTI_TEST_CASE(Proto2RequiredMissing) {
    // Wire with only the required id field (0x08 0x07).
    const auto r = rtti::proto::parse_as<ProtoReq>(
        rtti::string_view("\x08\x07", 2));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ((*r).id, 7);
    RTTI_CHECK_EQ((*r).name, std::string("anon"));
    RTTI_CHECK_EQ((*r).age, 42);
    RTTI_CHECK((*r).active);
    RTTI_CHECK_EQ((*r).weight, 1.5);

    // Required field absent -> error (missing_key).
    const auto e = rtti::proto::parse_as<ProtoReq>(rtti::string_view("", 0));
    RTTI_CHECK(!e.ok());
    RTTI_CHECK_EQ(e.err.code, rtti::error_code::missing_key);
}

RTTI_TEST_CASE(Proto2Defaults) {
    // Every optional member at its explicit default -> omitted on the wire.
    ProtoReq p;
    p.id = 1;
    p.name = "anon";
    p.age = 42;
    p.active = true;
    p.weight = 1.5;
    const std::string w = rtti::proto::serialize(p);
    RTTI_CHECK_EQ(w.size(), size_t(2));
    RTTI_CHECK_EQ((int)(unsigned char)w[0], 0x08);
    RTTI_CHECK_EQ((int)(unsigned char)w[1], 0x01);
    const auto r = rtti::proto::parse_as<ProtoReq>(to_sv(w));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ((*r).name, std::string("anon"));
    RTTI_CHECK_EQ((*r).age, 42);
    RTTI_CHECK((*r).active);
    RTTI_CHECK_EQ((*r).weight, 1.5);

    // A non-default optional is written and round-trips.
    p.age = 100;
    const std::string w2 = rtti::proto::serialize(p);
    RTTI_CHECK(w2.size() > 2);
    const auto r2 = rtti::proto::parse_as<ProtoReq>(to_sv(w2));
    RTTI_CHECK(r2.ok());
    RTTI_CHECK_EQ((*r2).age, 100);
}

RTTI_TEST_CASE(ProtoUnknownFieldSkip) {
    // i32(2)=7, unknown varint(99), unknown length(100)="xyz",
    // unknown fixed64(101), s(8)="hi".
    static const unsigned char w[] = {
        0x10, 0x07,                                 // field 2 varint 7
        0x98, 0x06, 0x01,                           // field 99 varint 1
        0xA2, 0x06, 0x03, 'x', 'y', 'z',            // field 100 length 3
        0xA9, 0x06, 1, 2, 3, 4, 5, 6, 7, 8,         // field 101 fixed64
        0x42, 0x02, 'h', 'i'                        // field 8 length "hi"
    };
    const auto r = rtti::proto::parse_as<ProtoScalar3>(
        rtti::string_view(reinterpret_cast<const char*>(w), sizeof(w)));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ((*r).i32, 7);
    RTTI_CHECK_EQ((*r).s, std::string("hi"));
}

RTTI_TEST_CASE(ProtoMalformedWire) {
    // Truncated varint value (continuation bit set at EOF).
    static const unsigned char tv[] = {0x08, 0x80};
    const auto r1 = rtti::proto::parse_as<ProtoScalar3>(
        rtti::string_view(reinterpret_cast<const char*>(tv), sizeof(tv)));
    RTTI_CHECK(!r1.ok());
    RTTI_CHECK_EQ(r1.err.code, rtti::error_code::proto_truncated);

    // Length prefix larger than the remaining buffer.
    static const unsigned char ol[] = {0x42, 0x7F, 'a'};
    const auto r2 = rtti::proto::parse_as<ProtoScalar3>(
        rtti::string_view(reinterpret_cast<const char*>(ol), sizeof(ol)));
    RTTI_CHECK(!r2.ok());
    RTTI_CHECK_EQ(r2.err.code, rtti::error_code::proto_truncated);

    // Wire type mismatch: field 2 is varint in the schema, stream is fixed64.
    static const unsigned char wt[] = {0x11, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto r3 = rtti::proto::parse_as<ProtoScalar3>(
        rtti::string_view(reinterpret_cast<const char*>(wt), sizeof(wt)));
    RTTI_CHECK(!r3.ok());
    RTTI_CHECK_EQ(r3.err.code, rtti::error_code::proto_type_mismatch);

    // Field number 0 is illegal.
    static const unsigned char zf[] = {0x00};
    const auto r4 = rtti::proto::parse_as<ProtoScalar3>(
        rtti::string_view(reinterpret_cast<const char*>(zf), sizeof(zf)));
    RTTI_CHECK(!r4.ok());
    RTTI_CHECK_EQ(r4.err.code, rtti::error_code::proto_bad_field);

    // Group wire types 3/4 are rejected (not supported).
    static const unsigned char gr[] = {0x0B};
    const auto r5 = rtti::proto::parse_as<ProtoScalar3>(
        rtti::string_view(reinterpret_cast<const char*>(gr), sizeof(gr)));
    RTTI_CHECK(!r5.ok());
    RTTI_CHECK_EQ(r5.err.code, rtti::error_code::proto_bad_field);
}

RTTI_TEST_CASE(ProtoSaxEvents) {
    static const unsigned char w[] = {0x08, 0x2A, 0x42, 0x03, 'a', 'b', 'c'};
    std::string log;
    rtti::proto::parse_events ev;
    ev.message_start = [&] { log += "S"; return true; };
    ev.message_end = [&] { log += "E"; return true; };
    ev.field = [&](std::uint32_t, rtti::proto::wire_type) {
        log += "F";
        return true;
    };
    ev.varint_value = [&](std::uint64_t) {
        log += "I";
        return true;
    };
    ev.bytes_value = [&](rtti::string_view) {
        log += "B";
        return true;
    };
    const auto r = rtti::proto::parse_stream(
        rtti::string_view(reinterpret_cast<const char*>(w), sizeof(w)), ev);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(log, std::string("SFIFBE"));

    // Early cancellation yields ok (not an error), like the other adapters.
    log.clear();
    ev.field = [&](std::uint32_t, rtti::proto::wire_type) { return false; };
    const auto c = rtti::proto::parse_stream(
        rtti::string_view(reinterpret_cast<const char*>(w), sizeof(w)), ev);
    RTTI_CHECK(c.ok());
    RTTI_CHECK_EQ(log, std::string("S"));

    // Malformed wire surfaces an error through parse_stream too.
    static const unsigned char bad[] = {0x08, 0x80};
    ev.field = [&](std::uint32_t, rtti::proto::wire_type) { return true; };
    const auto e = rtti::proto::parse_stream(
        rtti::string_view(reinterpret_cast<const char*>(bad), sizeof(bad)), ev);
    RTTI_CHECK(!e.ok());
    RTTI_CHECK_EQ(e.err.code, rtti::error_code::proto_truncated);
}

RTTI_TEST_CASE(ProtoTreeBridge) {
    ProtoScalar3 a;
    a.i32 = -9;
    a.s = "tree";
    a.color = ProtoColor::blue;
    const std::string w1 = rtti::proto::serialize(a);

    // C++ -> proto::document tree -> wire matches the typed serializer.
    rtti::proto::document doc;
    RTTI_CHECK_EQ(rtti::to_value(&doc, a), rtti::error_code::ok);
    std::string w2;
    RTTI_CHECK_EQ(rtti::proto::tree_to_wire<ProtoScalar3>(&doc, w2),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(w1, w2);

    // wire -> proto::document tree -> from_value recovers the object
    // (absent optional scalars are filled with their defaults).
    const auto parsed = rtti::proto::parse_as<ProtoScalar3>(to_sv(w1));
    RTTI_CHECK(parsed.ok());
    rtti::proto::document doc2;
    RTTI_CHECK_EQ(rtti::proto::wire_to_tree<ProtoScalar3>(to_sv(w1), doc2),
                  rtti::error_code::ok);
    ProtoScalar3 b;
    RTTI_CHECK_EQ(rtti::from_value(&doc2, b), rtti::error_code::ok);
    RTTI_CHECK(b == *parsed);

#if defined(RTTI_ENABLE_JSON) && RTTI_ENABLE_JSON
    // A JSON document tree (field names as keys) encodes to wire directly.
    const auto jdoc = rtti::json::parse(
        "{\"flag\":true,\"i32\":7,\"i64\":0,\"u32\":0,\"u64\":0,\"d\":0.0,"
        "\"f\":0.0,\"s\":\"json\",\"color\":\"green\"}");
    RTTI_CHECK(jdoc.ok());
    std::string w3;
    RTTI_CHECK_EQ(rtti::proto::tree_to_wire<ProtoScalar3>(&jdoc.value, w3),
                  rtti::error_code::ok);
    const auto jb = rtti::proto::parse_as<ProtoScalar3>(to_sv(w3));
    RTTI_CHECK(jb.ok());
    RTTI_CHECK_EQ((*jb).flag, true);
    RTTI_CHECK_EQ((*jb).i32, 7);
    RTTI_CHECK_EQ((*jb).s, std::string("json"));
    RTTI_CHECK_EQ((*jb).color, ProtoColor::green);
#endif

    // Schema-less parse maps keys to field numbers.
    const auto g = rtti::proto::parse(to_sv(w1));
    RTTI_CHECK(g.ok());
    RTTI_CHECK_EQ(g.value.type(), rtti::value_io::kind::object);
}

RTTI_TEST_CASE(ProtoDeepNesting) {
    // Encode a 600-level chain: hits the depth limit cleanly (too_deep).
    ProtoNode root;
    ProtoNode* cur = &root;
    for (int i = 0; i < 600; ++i) {
        cur->children.emplace_back();
        cur = &cur->children.back();
    }
    std::string w;
    RTTI_CHECK_EQ(rtti::proto::serialize(root, w), rtti::error_code::too_deep);

    // Decode a manually built 600-level chain: also too_deep, never crashes.
    std::string deep = "\x08\x01";
    for (int i = 0; i < 600; ++i) {
        std::string wrap;
        wrap.push_back(static_cast<char>(0x12)); // field 2 (children), length
        RTTI_CHECK(rtti::proto::encode_varint(deep.size(), wrap));
        wrap += deep;
        deep = std::move(wrap);
    }
    const auto r = rtti::proto::parse_as<ProtoNode>(to_sv(deep));
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ(r.err.code, rtti::error_code::too_deep);
}

RTTI_TEST_CASE(ProtoZigzagMatchesOfficialFixture) {
    // Official protoc 36.2 wire for Scalars{z = -456} from
    // tests/fixtures/proto/basic.Scalars.wire (basic.textbin sets `z: -456`):
    // field 2 sint64 varint -> tag 0x10, payload varint(zigzag(-456) = 911)
    // = 0x8F 0x07. protoc --decode re-reads these bytes back to z = -456.
    static const unsigned char official[] = {0x10, 0x8F, 0x07};
    ProtoZigzag p;
    p.z = -456;
    const std::string w = rtti::proto::serialize(p);
    RTTI_CHECK_EQ(w.size(), size_t(3));
    RTTI_CHECK_EQ(std::memcmp(w.data(), official, sizeof(official)), 0);

    const auto r = rtti::proto::parse_as<ProtoZigzag>(
        rtti::string_view(reinterpret_cast<const char*>(official),
                          sizeof(official)));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ((*r).z, std::int64_t(-456));
}

RTTI_TEST_CASE(ProtoZigzagRoundtrip) {
    // sint64 z (field 2) / sint32 s (field 3) zig-zag to positive varints:
    //   -1 -> 1, 1 -> 2, -2 -> 3, 300 -> 600 (0xD8 0x04), -300 -> 599
    //   (0xD7 0x04). The plain int64 control stays a sign-extended varint
    //   (10 bytes for -1). Expected bytes follow protoc --encode output.
    ProtoZigzag a;
    a.z = -1;
    a.s = -1;
    a.plain = -1;
    const std::string w = rtti::proto::serialize(a);
    static const unsigned char exp[] = {
        0x10, 0x01,                                        // z = -1 -> 1
        0x18, 0x01,                                        // s = -1 -> 1
        0x20, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01
    };                                                     // plain -1
    RTTI_CHECK_EQ(w.size(), size_t(sizeof(exp)));
    RTTI_CHECK_EQ(std::memcmp(w.data(), exp, sizeof(exp)), 0);
    const auto r = rtti::proto::parse_as<ProtoZigzag>(to_sv(w));
    RTTI_CHECK(r.ok());
    RTTI_CHECK(*r == a);

    // Larger magnitudes across both signs.
    ProtoZigzag b;
    b.z = 300;
    b.s = -300;
    const std::string w2 = rtti::proto::serialize(b);
    // protoc --encode verified: zigzag(300)=600 -> 0xD8 0x04; zigzag(-300)=599
    // -> 0xD7 0x04.
    static const unsigned char exp2[] = {
        0x10, 0xD8, 0x04, // z = 300  -> 600
        0x18, 0xD7, 0x04, // s = -300 -> 599
    };
    RTTI_CHECK_EQ(w2.size(), size_t(sizeof(exp2)));
    RTTI_CHECK_EQ(std::memcmp(w2.data(), exp2, sizeof(exp2)), 0);
    const auto r2 = rtti::proto::parse_as<ProtoZigzag>(to_sv(w2));
    RTTI_CHECK(r2.ok());
    RTTI_CHECK_EQ((*r2).z, std::int64_t(300));
    RTTI_CHECK_EQ((*r2).s, std::int32_t(-300));
    RTTI_CHECK_EQ((*r2).plain, std::int64_t(0)); // absent -> default 0

    // Round trip is byte-identical.
    RTTI_CHECK_EQ(rtti::proto::serialize(*r2), w2);

    // Full int64 range: INT64_MIN zig-zags to the 10-byte all-ones varint and
    // decodes back exactly.
    const std::int64_t kZigMin = static_cast<std::int64_t>(0x8000000000000000uLL);
    ProtoZigzag c;
    c.z = kZigMin;
    const auto rc = rtti::proto::parse_as<ProtoZigzag>(
        to_sv(rtti::proto::serialize(c)));
    RTTI_CHECK(rc.ok());
    RTTI_CHECK_EQ((*rc).z, kZigMin);
    RTTI_CHECK_EQ(rtti::proto::serialize(*rc), rtti::proto::serialize(c));
}

RTTI_TEST_CASE(ProtoZigzagTreeBridge) {
    // The value_io tree path honours zig-zag exactly like the typed path.
    rtti::proto::document doc;
    rtti::value_io* n = doc.make_node();
    n->make_scalar(rtti::field_value::of(std::int64_t(-456)));
    doc.set_key("z", n);
    std::string w;
    RTTI_CHECK_EQ(rtti::proto::tree_to_wire<ProtoZigzag>(&doc, w),
                  rtti::error_code::ok);
    static const unsigned char official[] = {0x10, 0x8F, 0x07};
    RTTI_CHECK_EQ(w.size(), size_t(sizeof(official)));
    RTTI_CHECK_EQ(std::memcmp(w.data(), official, sizeof(official)), 0);

    // Decoding the official bytes yields the same value back in the tree.
    rtti::proto::document out;
    RTTI_CHECK_EQ(rtti::proto::wire_to_tree<ProtoZigzag>(
                      rtti::string_view(
                          reinterpret_cast<const char*>(official),
                          sizeof(official)),
                      out),
                  rtti::error_code::ok);
    rtti::value_io* vz = nullptr;
    RTTI_CHECK(out.get_key("z", vz));
    std::int64_t z = 0;
    RTTI_CHECK(vz != nullptr && vz->as_integer(z));
    RTTI_CHECK_EQ(z, std::int64_t(-456));

    // Re-encoding the decoded tree reproduces the official bytes exactly.
    std::string w2;
    RTTI_CHECK_EQ(rtti::proto::tree_to_wire<ProtoZigzag>(&out, w2),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(w2, w);
}

} // namespace rtti_test_lib
