// FlatBuffers wire-format adapter tests for the RTTI reflect-core library.
// Covers root uoffset / vtable walk (slot presence + .fbs defaults), scalars
// aligned 1/2/4/8, strings (length + UTF-8 + trailing NUL), vectors of
// scalars/structs/tables, inline structs, unions (type+value slot pair) and
// file_identifier verification, plus the typed serialize/parse_as bridge and
// malformed-wire error paths. The golden buffers are the committed fixtures
// produced by the official flatc 25.12.19 (tests/fixtures/flat/monster.bin)
// and a locally generated union fixture (featured verbatim below); parsing
// them asserts byte-level interop with the official toolchain. English comments
// only.
//
// Layout note: the schema<T> specializations must appear before the
// RTTI_TEST_CASEs. The harness runs every test during static initialization;
// C++ initializes the static inline descriptor arrays in declaration order, so
// the schemas have to be declared first for their arrays to be ready when the
// tests run (the same ordering proto_test.cpp relies on).

#include "test_harness.hpp"
#include <rtti/flat.hpp>
#include <rtti/convert.hpp>
#include <rtti/error.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace rtti_test_lib {

// ---- types mirroring tests/fixtures/flat/basic.fbs ------------------------
// namespace rtti.fixt: enum Color:byte {Red=0,Green=1}; table Weapon
// {name:string; damage:short;} table Monster {pos:Vec3; mana:short=150;
// hp:short=200; name:string; friendly:bool=false; inventory:[byte];
// weapons:[Weapon]; path:[Vec3];} struct Vec3 {x,y,z: float;} root Monster.

enum class FlatColor : int { red = 0, green = 1 };

struct FlatVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(FlatVec3, x, y, z)
};

struct FlatWeapon {
    std::string name;
    std::int16_t damage = 0;

    RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(FlatWeapon, name, damage)
};

struct FlatMonster {
    FlatVec3 pos;
    std::int16_t mana = 0;
    std::int16_t hp = 0;
    std::string name;
    bool friendly = false;
    std::vector<std::int8_t> inventory;
    std::vector<FlatWeapon> weapons;
    std::vector<FlatVec3> path;

    RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(FlatMonster, pos, mana, hp, name,
                                            friendly, inventory, weapons, path)
};

// ---- union + file_identifier probe types -----------------------------------

enum class FlatLeafKind : int { none = 0, leaf = 1 };

struct FlatLeaf {
    std::int32_t tag = 0;
    std::string label;

    RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(FlatLeaf, tag, label)
};

struct FlatBox {
    std::int32_t payload_type = 0; // FlatLeafKind as int (0 none, 1 leaf)
    std::optional<FlatLeaf> payload;
    std::string extra;

    RTTI_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(FlatBox, payload_type, payload,
                                            extra)
};

// Bare tag type for the DoS-guard nesting test; only schema<FlatChain> is
// consulted (the wire only carries an optional child table), so no reflection
// registration is required.
struct FlatChain {};

// ---- typed round-trip helpers ----------------------------------------------

bool operator==(const FlatVec3& a, const FlatVec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool operator==(const FlatWeapon& a, const FlatWeapon& b) {
    return a.name == b.name && a.damage == b.damage;
}
bool operator==(const FlatMonster& a, const FlatMonster& b) {
    return a.pos == b.pos && a.mana == b.mana && a.hp == b.hp &&
           a.name == b.name && a.friendly == b.friendly &&
           a.inventory == b.inventory && a.weapons == b.weapons &&
           a.path == b.path;
}
bool operator==(const FlatLeaf& a, const FlatLeaf& b) {
    return a.tag == b.tag && a.label == b.label;
}

inline std::string get_str(const rtti::value_io* node, const char* key,
                           bool& found) {
    found = false;
    rtti::value_io* child = nullptr;
    if (node == nullptr || !node->get_key(rtti::string_view(key), child) ||
        child == nullptr) {
        return std::string();
    }
    rtti::string_view sv;
    if (child->type() == rtti::value_io::kind::string &&
        child->as_string(sv)) {
        found = true;
        return std::string(sv.data(), sv.size());
    }
    return std::string();
}

inline double get_real(const rtti::value_io* node, const char* key,
                       bool& found) {
    found = false;
    rtti::value_io* child = nullptr;
    if (node == nullptr || !node->get_key(rtti::string_view(key), child) ||
        child == nullptr) {
        return 0.0;
    }
    double d = 0.0;
    if (child->as_real(d)) {
        found = true;
    }
    return d;
}

inline std::int64_t get_int(const rtti::value_io* node, const char* key,
                            bool& found) {
    found = false;
    rtti::value_io* child = nullptr;
    if (node == nullptr || !node->get_key(rtti::string_view(key), child) ||
        child == nullptr) {
        return 0;
    }
    std::int64_t i = 0;
    if (child->as_integer(i)) {
        found = true;
    }
    return i;
}

} // namespace rtti_test_lib

RTTI_DEFINE_ENUM(rtti_test_lib::FlatColor, red, green)
RTTI_DEFINE_ENUM(rtti_test_lib::FlatLeafKind, none, leaf)

// Schema specializations (see layout note above: declared before the tests so
// their static inline descriptor arrays initialize before the tests run).
namespace rtti {
namespace flat {

template <>
struct schema<rtti_test_lib::FlatVec3> {
    static constexpr std::size_t count = 3;
    static inline const field_desc fields[count] = {
        make_field_desc("x", 0, fkind::f32),
        make_field_desc("y", 1, fkind::f32),
        make_field_desc("z", 2, fkind::f32),
    };
};

template <>
struct schema<rtti_test_lib::FlatWeapon> {
    static constexpr std::size_t count = 2;
    static inline const field_desc fields[count] = {
        make_field_desc("name", 0, fkind::string),
        make_field_desc("damage", 1, fkind::i16),
    };
};

template <>
struct schema<rtti_test_lib::FlatMonster> {
    static constexpr std::size_t count = 8;
    static inline const field_desc fields[count] = {
        make_field_desc("pos", 0, fkind::stct, fkind::none,
                        schema_of<rtti_test_lib::FlatVec3>()),
        with_default_int(make_field_desc("mana", 1, fkind::i16), 150),
        with_default_int(make_field_desc("hp", 2, fkind::i16), 200),
        make_field_desc("name", 3, fkind::string),
        with_default_int(make_field_desc("friendly", 4, fkind::bool_), 0),
        make_field_desc("inventory", 5, fkind::vector, fkind::i8),
        make_field_desc("weapons", 6, fkind::vector, fkind::table,
                        schema_of<rtti_test_lib::FlatWeapon>()),
        make_field_desc("path", 7, fkind::vector, fkind::stct,
                        schema_of<rtti_test_lib::FlatVec3>()),
    };
};

template <>
struct schema<rtti_test_lib::FlatLeaf> {
    static constexpr std::size_t count = 2;
    static inline const field_desc fields[count] = {
        make_field_desc("tag", 0, fkind::i32),
        make_field_desc("label", 1, fkind::string),
    };
};

namespace {
static inline const union_variant_entry kFlatBoxVariants[] = {
    variant_of<rtti_test_lib::FlatLeaf>(rtti_test_lib::FlatLeafKind::leaf),
};
} // namespace

template <>
struct schema<rtti_test_lib::FlatBox> {
    static constexpr std::size_t count = 3;
    static inline const field_desc fields[count] = {
        make_field_desc("payload_type", 0, fkind::union_type),
        make_union_value("payload", 1, 0, kFlatBoxVariants, 1),
        make_field_desc("extra", 2, fkind::string),
    };
    static inline const char* identifier = "MONS";
};

// Recursive table: slot0 is a plain i32, slot1 is a child table.
template <>
struct schema<rtti_test_lib::FlatChain> {
    static constexpr std::size_t count = 2;
    static inline const field_desc fields[count] = {
        make_field_desc("dummy", 0, fkind::i32),
        make_field_desc("child", 1, fkind::table, fkind::none,
                        schema_of<rtti_test_lib::FlatChain>()),
    };
};

} // namespace flat
} // namespace rtti

namespace {
// Golden bytes captured from the official flatc 25.12.19:
//   tests/fixtures/flat/monster.bin  (schema basic.fbs, JSON monster.json)
const std::uint8_t kOfficialMonster[] = {
    0x18, 0x00, 0x00, 0x00, 0x14, 0x00, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x14, 0x00, 0x18, 0x00, 0x1C, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40,
    0x00, 0x00, 0x40, 0x40, 0x74, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40,
    0x02, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0xE8, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x0A, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x53, 0x77, 0x6F, 0x72, 0x64, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x0C, 0x00, 0x08, 0x00, 0x06, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x05, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x41, 0x78, 0x65, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
    0x03, 0x00, 0x00, 0x00, 0x4F, 0x72, 0x63, 0x00};

// Official flatc --binary of a schema with `union + file_identifier "MONS"`
// (probe.fixt: Box {payload: LeafOrNone; extra:string}).
const std::uint8_t kOfficialUnion[] = {
    0x14, 0x00, 0x00, 0x00, 0x4D, 0x4F, 0x4E, 0x53, 0x00, 0x00, 0x0A, 0x00,
    0x10, 0x00, 0x07, 0x00, 0x08, 0x00, 0x0C, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x1C, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x62, 0x6F, 0x78, 0x65, 0x64, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x0C, 0x00, 0x04, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x68, 0x69, 0x00, 0x00};

inline rtti::string_view to_sv(const std::uint8_t* p, std::size_t n) {
    return rtti::string_view(reinterpret_cast<const char*>(p), n);
}
} // namespace

namespace rtti_test_lib {

RTTI_TEST_CASE(FlatOfficialMonsterBytesParse) {
    const rtti::string_view bytes =
        to_sv(kOfficialMonster, sizeof(kOfficialMonster));
    const auto r = rtti::flat::parse<FlatMonster>(bytes);
    RTTI_CHECK(r.ok());
    const rtti::value_io& root = r.value;

    bool found = false;
    rtti::value_io* pos = nullptr;
    RTTI_CHECK(root.get_key("pos", pos));
    RTTI_CHECK(pos != nullptr);
    RTTI_CHECK_EQ(get_real(pos, "x", found), 1.0);
    RTTI_CHECK_EQ(get_real(pos, "y", found), 2.0);
    RTTI_CHECK_EQ(get_real(pos, "z", found), 3.0);
    // mana/hp/friendly are omitted by flatc (at their .fbs defaults).
    RTTI_CHECK_EQ(get_int(&root, "mana", found), 150);
    RTTI_CHECK_EQ(get_int(&root, "hp", found), 200);
    RTTI_CHECK_EQ(get_str(&root, "name", found), std::string("Orc"));
    {
        rtti::value_io* n = nullptr;
        RTTI_CHECK(root.get_key("friendly", n));
        RTTI_CHECK(n != nullptr);
        bool b = false;
        RTTI_CHECK(n->as_bool(b));
        RTTI_CHECK(!b);
    }
    {
        rtti::value_io* inv = nullptr;
        RTTI_CHECK(root.get_key("inventory", inv));
        RTTI_CHECK(inv != nullptr);
        RTTI_CHECK_EQ(inv->size(), size_t(4));
        for (std::size_t i = 0; i < 4; ++i) {
            std::int64_t v = 0;
            RTTI_CHECK(inv->at(i)->as_integer(v));
            RTTI_CHECK_EQ(v, std::int64_t(i));
        }
    }
    {
        rtti::value_io* w = nullptr;
        RTTI_CHECK(root.get_key("weapons", w));
        RTTI_CHECK(w != nullptr);
        RTTI_CHECK_EQ(w->size(), size_t(2));
        RTTI_CHECK_EQ(get_str(w->at(0), "name", found), std::string("Axe"));
        RTTI_CHECK_EQ(get_int(w->at(0), "damage", found), 5);
        RTTI_CHECK_EQ(get_str(w->at(1), "name", found), std::string("Sword"));
        RTTI_CHECK_EQ(get_int(w->at(1), "damage", found), 10);
    }
    {
        rtti::value_io* p = nullptr;
        RTTI_CHECK(root.get_key("path", p));
        RTTI_CHECK(p != nullptr);
        RTTI_CHECK_EQ(p->size(), size_t(2));
        RTTI_CHECK_EQ(get_real(p->at(0), "x", found), 0.0);
        RTTI_CHECK_EQ(get_real(p->at(0), "y", found), 0.0);
        RTTI_CHECK_EQ(get_real(p->at(0), "z", found), 0.0);
        RTTI_CHECK_EQ(get_real(p->at(1), "x", found), 1.0);
        RTTI_CHECK_EQ(get_real(p->at(1), "y", found), 2.0);
        RTTI_CHECK_EQ(get_real(p->at(1), "z", found), 3.0);
    }
}

RTTI_TEST_CASE(FlatOfficialUnionBytesParse) {
    // The union buffer carries a "MONS" file identifier at bytes 4..7.
    const rtti::string_view bytes =
        to_sv(kOfficialUnion, sizeof(kOfficialUnion));
    RTTI_CHECK(rtti::flat::verify_file_identifier(bytes, "MONS"));
    RTTI_CHECK(!rtti::flat::verify_file_identifier(bytes, "XXXX"));

    const auto r = rtti::flat::parse<FlatBox>(bytes);
    RTTI_CHECK(r.ok());
    bool found = false;
    RTTI_CHECK_EQ(get_int(&r.value, "payload_type", found), 1); // Leaf
    {
        rtti::value_io* p = nullptr;
        RTTI_CHECK(r.value.get_key("payload", p));
        RTTI_CHECK(p != nullptr);
        RTTI_CHECK_EQ(p->type(), rtti::value_io::kind::object);
        RTTI_CHECK_EQ(get_int(p, "tag", found), 7);
        RTTI_CHECK_EQ(get_str(p, "label", found), std::string("hi"));
    }
    RTTI_CHECK_EQ(get_str(&r.value, "extra", found), std::string("boxed"));

    // The same bytes corrupt their identifier -> invalid_root.
    std::uint8_t bad[sizeof(kOfficialUnion)];
    std::memcpy(bad, kOfficialUnion, sizeof(kOfficialUnion));
    bad[4] = 'X';
    const auto e = rtti::flat::parse<FlatBox>(to_sv(bad, sizeof(bad)));
    RTTI_CHECK(!e.ok());
    RTTI_CHECK_EQ(e.err.code, rtti::error_code::invalid_root);
}

RTTI_TEST_CASE(FlatSerializeRoundtripsOfficialBytes) {
    // Rebuild the same monster the fixture encodes and prove the wire is
    // spec-equivalent: serialize -> parse reads back identical values.
    FlatMonster m;
    m.pos = {1.0f, 2.0f, 3.0f};
    m.mana = 150;
    m.hp = 200;
    m.name = "Orc";
    m.friendly = false;
    m.inventory = {0, 1, 2, 3};
    m.weapons = {{"Axe", 5}, {"Sword", 10}};
    m.path = {{0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}};

    std::string wire;
    RTTI_CHECK_EQ(rtti::flat::serialize(m, wire), rtti::error_code::ok);
    RTTI_CHECK(wire.size() >= 4);

    const auto back = rtti::flat::parse_as<FlatMonster>(
        rtti::string_view(wire.data(), wire.size()));
    RTTI_CHECK(back.ok());
    RTTI_CHECK(back.value == m);

    // The parsed tree reports the same values the official binary did.
    const auto tree = rtti::flat::parse<FlatMonster>(
        rtti::string_view(wire.data(), wire.size()));
    RTTI_CHECK(tree.ok());
    bool found = false;
    {
        rtti::value_io* pos = nullptr;
        RTTI_CHECK(tree.value.get_key("pos", pos));
        RTTI_CHECK_EQ(get_real(pos, "x", found), 1.0);
        RTTI_CHECK_EQ(get_real(pos, "z", found), 3.0);
    }
    RTTI_CHECK_EQ(get_int(&tree.value, "mana", found), 150);
    RTTI_CHECK_EQ(get_str(&tree.value, "name", found), std::string("Orc"));
    {
        rtti::value_io* w = nullptr;
        RTTI_CHECK(tree.value.get_key("weapons", w));
        RTTI_CHECK(w != nullptr);
        RTTI_CHECK_EQ(w->size(), size_t(2));
        RTTI_CHECK_EQ(get_str(w->at(1), "name", found), std::string("Sword"));
        RTTI_CHECK_EQ(get_int(w->at(1), "damage", found), 10);
    }
}

RTTI_TEST_CASE(FlatUnionRoundtrip) {
    FlatBox b;
    b.payload_type = static_cast<std::int32_t>(FlatLeafKind::leaf);
    b.payload = FlatLeaf{7, "hi"};
    b.extra = "boxed";

    std::string wire = rtti::flat::serialize(b);
    RTTI_CHECK(wire.size() >= 8); // header + file identifier
    // Our encoder emits the "MONS" identifier for this schema.
    RTTI_CHECK(rtti::flat::verify_file_identifier(
        rtti::string_view(wire.data(), wire.size()), "MONS"));

    const auto back = rtti::flat::parse_as<FlatBox>(
        rtti::string_view(wire.data(), wire.size()));
    RTTI_CHECK(back.ok());
    RTTI_CHECK(back.value.payload.has_value());
    RTTI_CHECK(back.value.payload_type == 1);
    RTTI_CHECK(back.value.payload->tag == 7);
    RTTI_CHECK(back.value.payload->label == std::string("hi"));
    RTTI_CHECK(back.value.extra == std::string("boxed"));

    // None variant: no union value, no type byte on the wire.
    FlatBox none;
    none.payload_type = 0;
    none.extra = "standalone";
    std::string wire2 = rtti::flat::serialize(none);
    const auto back2 = rtti::flat::parse_as<FlatBox>(
        rtti::string_view(wire2.data(), wire2.size()));
    RTTI_CHECK(back2.ok());
    RTTI_CHECK(!back2.value.payload.has_value());
    RTTI_CHECK_EQ(back2.value.extra, std::string("standalone"));

    // Parsing the official (flatc-built) union buffer into the typed value.
    const auto official = rtti::flat::parse_as<FlatBox>(
        to_sv(kOfficialUnion, sizeof(kOfficialUnion)));
    RTTI_CHECK(official.ok());
    RTTI_CHECK(official.value.payload.has_value());
    RTTI_CHECK_EQ(official.value.payload->tag, 7);
    RTTI_CHECK_EQ(official.value.payload->label, std::string("hi"));
}

RTTI_TEST_CASE(FlatDefaultsFromAbsentSlots) {
    // Hand-built minimal root: uoffset(8)=8, vtable{vtable_size=4,
    // table_size=4}, empty table {soffset 4}. Everything is at its default.
    static const std::uint8_t minimal[] = {
        0x08, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00};
    const auto r = rtti::flat::parse<FlatMonster>(
        to_sv(minimal, sizeof(minimal)));
    RTTI_CHECK(r.ok());
    bool found = false;
    RTTI_CHECK_EQ(get_int(&r.value, "mana", found), 150); // .fbs default
    RTTI_CHECK_EQ(get_int(&r.value, "hp", found), 200);
    RTTI_CHECK_EQ(get_str(&r.value, "name", found), std::string(""));
    RTTI_CHECK_EQ(get_int(&r.value, "friendly", found), std::int64_t(0));
    {
        rtti::value_io* inv = nullptr;
        RTTI_CHECK(r.value.get_key("inventory", inv));
        RTTI_CHECK(inv != nullptr);
        RTTI_CHECK_EQ(inv->type(), rtti::value_io::kind::array);
        RTTI_CHECK_EQ(inv->size(), size_t(0));
    }
    {
        rtti::value_io* pos = nullptr;
        RTTI_CHECK(r.value.get_key("pos", pos));
        RTTI_CHECK(pos != nullptr);
        RTTI_CHECK_EQ(pos->type(), rtti::value_io::kind::object);
    }
    const auto back = rtti::flat::parse_as<FlatMonster>(
        to_sv(minimal, sizeof(minimal)));
    RTTI_CHECK(back.ok());
    RTTI_CHECK_EQ(back.value.mana, 150);
    RTTI_CHECK_EQ(back.value.hp, 200);
    RTTI_CHECK(back.value.name.empty());
    RTTI_CHECK(!back.value.friendly);
    RTTI_CHECK(back.value.inventory.empty());
}

RTTI_TEST_CASE(FlatMalformedWire) {
    // Too short to hold the root offset.
    static const std::uint8_t tiny[] = {0x01, 0x02, 0x03};
    const auto r0 = rtti::flat::parse<FlatMonster>(to_sv(tiny, 3));
    RTTI_CHECK(!r0.ok());
    RTTI_CHECK_EQ(r0.err.code, rtti::error_code::invalid_root);

    // Root offset zero.
    static const std::uint8_t root0[] = {0x00, 0x00, 0x00, 0x00};
    const auto r1 = rtti::flat::parse<FlatMonster>(to_sv(root0, 4));
    RTTI_CHECK(!r1.ok());
    RTTI_CHECK_EQ(r1.err.code, rtti::error_code::proto_bad_field);

    // Root offset points past the buffer.
    static const std::uint8_t rootbig[] = {0xFF, 0xFF, 0xFF, 0x7F};
    const auto r2 = rtti::flat::parse<FlatMonster>(to_sv(rootbig, 4));
    RTTI_CHECK(!r2.ok());
    RTTI_CHECK_EQ(r2.err.code, rtti::error_code::proto_truncated);

    // Table whose soffset is zero (vtable missing).
    static const std::uint8_t tab0[] = {
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto r3 = rtti::flat::parse<FlatMonster>(to_sv(tab0, 8));
    RTTI_CHECK(!r3.ok());
    RTTI_CHECK_EQ(r3.err.code, rtti::error_code::proto_bad_field);

    // Table whose soffset runs the vtable off the start of the buffer.
    static const std::uint8_t tabneg[] = {
        0x0C, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00};
    const auto r4 = rtti::flat::parse<FlatMonster>(to_sv(tabneg, 16));
    RTTI_CHECK(!r4.ok());
    RTTI_CHECK_EQ(r4.err.code, rtti::error_code::proto_bad_field);

    // A name (slot3) string whose uoffset over-runs the buffer end.
    // vtable at 4 (size 12 -> slots 0..3, table_size 8), table at 16
    // (soffset 12), slot3 field at 20.
    static const std::uint8_t overrun[] = {
        0x10, 0x00, 0x00, 0x00,                    // root = 16
        0x0C, 0x00, 0x08, 0x00,                    // vtable size 12, table 8
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        // slots 0..2 absent
        0x04, 0x00,                                // slot 3 -> +4
        0x0C, 0x00, 0x00, 0x00,                    // table soffset 12
        0xFF, 0xFF, 0xFF, 0x7F                     // uoffset way past EOF
    };
    const auto r5 = rtti::flat::parse<FlatMonster>(to_sv(overrun, sizeof(overrun)));
    RTTI_CHECK(!r5.ok());
    RTTI_CHECK_EQ(r5.err.code, rtti::error_code::proto_truncated);

    // A name (slot3) string missing its trailing NUL terminator.
    static const std::uint8_t nonul[] = {
        0x10, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x08, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x0C, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x68, 0x69, 0x01};
    const auto r6 = rtti::flat::parse<FlatMonster>(to_sv(nonul, sizeof(nonul)));
    RTTI_CHECK(!r6.ok());
    RTTI_CHECK_EQ(r6.err.code, rtti::error_code::proto_bad_field);

    // inventory (slot5) vector whose length prefix over-runs the buffer.
    static const std::uint8_t vinc[] = {
        0x14, 0x00, 0x00, 0x00,                    // root = 20
        0x10, 0x00, 0x08, 0x00,                    // vtable size 16, table 8
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        // slots 0..2
        0x00, 0x00, 0x00, 0x00,                    // slots 3..4
        0x04, 0x00,                                // slot 5 -> +4
        0x10, 0x00, 0x00, 0x00,                    // table soffset 16
        0x04, 0x00, 0x00, 0x00,                    // slot5 uoffset 4 -> vector
        0xFF, 0xFF, 0xFF, 0x7F                     // count far beyond EOF
    };
    const auto r7 = rtti::flat::parse<FlatMonster>(to_sv(vinc, sizeof(vinc)));
    RTTI_CHECK(!r7.ok());
    RTTI_CHECK_EQ(r7.err.code, rtti::error_code::proto_truncated);

    // Invalid UTF-8 in a string -> invalid_utf8 (len 2, 0xC0 0x80 overlong).
    static const std::uint8_t badutf8[] = {
        0x10, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x08, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x0C, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0xC0, 0x80, 0x00};
    const auto r9 = rtti::flat::parse<FlatMonster>(to_sv(badutf8, sizeof(badutf8)));
    RTTI_CHECK(!r9.ok());
    RTTI_CHECK_EQ(r9.err.code, rtti::error_code::invalid_utf8);
}

RTTI_TEST_CASE(FlatUnknownSlotForwardCompat) {
    // A vtable carrying slots the schema does not know must be skipped safely.
    // Schema knows slots 0..7; build a buffer that also emits slot 8 (a
    // string). vtable at 4 (size 4+2*9 = 22), table at 26 (soffset 22).
    // Slot 3 (name) -> +4 at table; slot 8 (unknown) -> +8 at table.
    static const std::uint8_t fwd[] = {
        0x1C, 0x00, 0x00, 0x00,                    // root = 28
        0x16, 0x00, 0x0C, 0x00,                    // vtable size 22, table 12
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        // slots 0..2
        0x04, 0x00,                                // slot 3 (name) -> +4
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // slots 4..7
        0x08, 0x00,                                // slot 8 (unknown) -> +8
        0x00, 0x00,                                // pad to the 4-aligned table
        0x18, 0x00, 0x00, 0x00,                    // table soffset = 24
        0x08, 0x00, 0x00, 0x00,                    // slot3 uoffset 8
        0x0E, 0x00, 0x00, 0x00,                    // slot8 uoffset 14
        0x03, 0x00, 0x00, 0x00, 0x4F, 0x72, 0x63, 0x00, // "Orc"
        0x03, 0x00, 0x00, 0x00, 0x78, 0x79, 0x7A, 0x00  // "xyz"
    };
    const auto r = rtti::flat::parse<FlatMonster>(to_sv(fwd, sizeof(fwd)));
    RTTI_CHECK(r.ok());
    bool found = false;
    RTTI_CHECK_EQ(get_str(&r.value, "name", found), std::string("Orc"));
    RTTI_CHECK_EQ(get_int(&r.value, "mana", found), 150);
}

RTTI_TEST_CASE(FlatDeepNestingGuard) {
    // Build a 12-level table chain where every level is a table wrapping the
    // next one through a child-table slot; decode must trip too_deep under a
    // tight max_depth and succeed under a generous one, never crash.
    auto build_chain = [](std::size_t levels) {
            const auto put_le = [](std::string& s, std::size_t at,
                                   std::uint32_t v) {
                for (int i = 0; i < 4; ++i) {
                    s[at + i] = static_cast<char>((v >> (8 * i)) & 0xFFu);
                }
            };
            std::vector<std::size_t> table_pos;
            std::vector<std::size_t> placeholder;
            std::string out(4, '\0'); // root offset header
            // Each level carries one present field: slot1 (child table).
            // vtable: {size 8, table 8, slot0=0, slot1=4}; table: soffset +
            // slot1 uoffset placeholder.
            for (std::size_t i = 0; i < levels; ++i) {
                const std::size_t vtable = out.size();
                out.append(6, '\0'); // header (patched below) - slots at +4/+6
                out.append(2, '\0');
                out[vtable] = static_cast<char>(8); // vtable_size
                out[vtable + 1] = 0;
                out[vtable + 2] = static_cast<char>(8); // table_size
                out[vtable + 3] = 0;
                out[vtable + 4] = static_cast<char>(0); // slot0 absent
                out[vtable + 5] = 0;
                out[vtable + 6] = static_cast<char>(4); // slot1 -> +4
                out[vtable + 7] = 0;
                const std::size_t pad4 = (4 - (out.size() % 4)) % 4;
                out.append(pad4, '\0');
                const std::size_t table = out.size();
                out.append(4, '\0'); // soffset
                table_pos.push_back(table);
                placeholder.push_back(out.size());
                out.append(4, '\0'); // slot1 child-table uoffset
                put_le(out, table, static_cast<std::uint32_t>(table - vtable));
            }
            // Innermost empty table (4-aligned).
            const std::size_t pad4 = (4 - (out.size() % 4)) % 4;
            out.append(pad4, '\0');
            const std::size_t inner_vtable = out.size();
            out.append(4, '\0');
            out[inner_vtable] = static_cast<char>(4);
            out[inner_vtable + 2] = static_cast<char>(4);
            const std::size_t inner_table = out.size();
            out.append(4, '\0');
            out[inner_table] = static_cast<char>(4);
            // Patch each level's child placeholder to the child table position.
            for (std::size_t i = 0; i < levels; ++i) {
                const std::size_t target =
                    (i + 1 < levels) ? table_pos[i + 1] : inner_table;
                put_le(out, placeholder[i],
                       static_cast<std::uint32_t>(target - placeholder[i]));
            }
            put_le(out, 0, static_cast<std::uint32_t>(table_pos[0]));
            return out;
        };

    const std::string deep = build_chain(12);
    rtti::flat::parse_config tight;
    tight.max_depth = 4;
    const auto e = rtti::flat::parse<FlatChain>(
        rtti::string_view(deep.data(), deep.size()), tight);
    RTTI_CHECK(!e.ok());
    RTTI_CHECK_EQ(e.err.code, rtti::error_code::too_deep);

    rtti::flat::parse_config loose;
    loose.max_depth = 64;
    const auto ok = rtti::flat::parse<FlatChain>(
        rtti::string_view(deep.data(), deep.size()), loose);
    RTTI_CHECK(ok.ok());
}

RTTI_TEST_CASE(FlatFileIdentifierHelpers) {
    RTTI_CHECK(rtti::flat::verify_file_identifier(
        to_sv(kOfficialUnion, sizeof(kOfficialUnion)), "MONS"));
    RTTI_CHECK(!rtti::flat::verify_file_identifier(
        to_sv(kOfficialMonster, sizeof(kOfficialMonster)), "MONS"));
    // A null identifier never fails.
    RTTI_CHECK(rtti::flat::verify_file_identifier(rtti::string_view("", 0),
                                                  nullptr));
    RTTI_CHECK(rtti::flat::verify_file_identifier(rtti::string_view("abc", 3),
                                                  nullptr));
}

} // namespace rtti_test_lib