// FlatBuffers .fbs text-schema parser tests for the RTTI reflect-core library.
// Covers namespace/table/struct/enum/union/field/default/attribute parsing into
// a queryable descriptor, the compile step that produces a codec-ready
// schema_info, wire integration (monster.bin produced by the official flatc
// 25.12.19), semantic cross-checks against the official flatc --cpp generated
// header (tests/fixtures/flat/basic_generated.h) and a full negative battery of
// malformed inputs (unterminated blocks/comments/strings, unknown types, bad
// defaults, illegal struct/union forms) -- every failure is an error_code, never
// a crash (spec section 0). English comments only.
//
// This test TU is compiled standalone with cl (it provides its own main via
// RTTI_TEST_MAIN), independent of the main CMake test executable, because Task
// 6 may not touch the CMake files. It links against the flat wire layer
// (src/flat.cpp) plus src/flat_schema.cpp.
//
// The golden bytes below match tests/fixtures/flat/monster.bin and the union
// buffer from flat_test.cpp (both produced by flatc 25.12.19); the schema text
// is read from the committed fixtures. The disk assertions are deliberate:
// they prove the committed official fixture is really the parse target, so the
// test must be run from the repository root (the verification command does).
// The embedded copies only back the runtime if pick_path() cannot locate the
// working directory.

#include "test_harness.hpp"
#include <rtti/flat_schema.hpp>
#include <rtti/flat.hpp>
#include <rtti/error.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

// --- embedded official fixtures (fallback + byte pins) ---------------------

// tests/fixtures/flat/basic.fbs (verbatim).
const char kBasicFbs[] =
    "namespace rtti.fixt;\n"
    "enum Color:byte { Red=0, Green=1 }\n"
    "table Weapon { name:string; damage:short; }\n"
    "table Monster { pos:Vec3; mana:short=150; hp:short=200; name:string; "
    "friendly:bool=false; inventory:[byte]; weapons:[Weapon]; path:[Vec3]; }\n"
    "struct Vec3 { x:float; y:float; z:float; }\n"
    "root_type Monster;\n";

// Official flatc 25.12.19 --binary of basic.fbs + monster.json.
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

/// Try a list of candidate paths (the test runs from a few working dirs);
/// returns the first one that exists.
std::string pick_path(const std::vector<std::string>& candidates) {
    for (const std::string& p : candidates) {
        std::ifstream in(p, std::ios::binary);
        if (in.good()) {
            return p;
        }
    }
    return std::string();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

rtti::string_view to_sv(const std::uint8_t* p, std::size_t n) {
    return rtti::string_view(reinterpret_cast<const char*>(p), n);
}

// value_io helpers mirrored from flat_test.cpp.
std::string get_str(const rtti::value_io* node, const char* key, bool& found) {
    found = false;
    rtti::value_io* child = nullptr;
    if (node == nullptr || !node->get_key(rtti::string_view(key), child) ||
        child == nullptr) {
        return std::string();
    }
    rtti::string_view sv;
    if (child->type() == rtti::value_io::kind::string && child->as_string(sv)) {
        found = true;
        return std::string(sv.data(), sv.size());
    }
    return std::string();
}

std::int64_t get_int(const rtti::value_io* node, const char* key, bool& found) {
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

double get_real(const rtti::value_io* node, const char* key, bool& found) {
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

} // namespace
namespace rtti_test_lib {

RTTI_TEST_CASE(FbsParseOfficialBasicSchema) {
    // Read the committed fixture .fbs so the parse target is the official file
    // (fallback to the embedded copy if the path cannot be located).
    const std::string path = pick_path({
        "tests/fixtures/flat/basic.fbs",
        "./tests/fixtures/flat/basic.fbs",
        "../tests/fixtures/flat/basic.fbs",
    });
    const bool disk_loaded = !path.empty();
    RTTI_CHECK(disk_loaded); // fixtures are committed; a miss is a real failure
    std::string text = disk_loaded ? read_file(path) : std::string(kBasicFbs);
    RTTI_CHECK(!text.empty());

    const auto r = rtti::flat::parse_schema(
        rtti::string_view(text.data(), text.size()));
    RTTI_CHECK(r.ok());
    const rtti::flat::fbs_schema_desc& d = r.value;
    RTTI_CHECK_EQ(d.ns, std::string("rtti.fixt"));
    RTTI_CHECK_EQ(d.root_type, std::string("rtti.fixt.Monster"));
    RTTI_CHECK_EQ(d.file_identifier, std::string(""));
    RTTI_CHECK(d.includes.empty());
    RTTI_CHECK_EQ(d.enums.size(), size_t(1));
    RTTI_CHECK(d.unions.empty());
    RTTI_CHECK_EQ(d.types.size(), size_t(3)); // Weapon, Monster (tables), Vec3

    // --- enum Color:byte {Red=0, Green=1} ---
    const rtti::flat::fbs_enum_desc* color = rtti::flat::find_enum(d, "Color");
    RTTI_CHECK(color != nullptr);
    RTTI_CHECK_EQ(color->full_name, std::string("rtti.fixt.Color"));
    RTTI_CHECK_EQ(static_cast<int>(color->underlying),
                  static_cast<int>(rtti::flat::fkind::i8));
    RTTI_CHECK_EQ(color->values.size(), size_t(2));
    RTTI_CHECK_EQ(color->values[0].name, std::string("Red"));
    RTTI_CHECK_EQ(color->values[0].value, int64_t(0));
    RTTI_CHECK_EQ(color->values[1].name, std::string("Green"));
    RTTI_CHECK_EQ(color->values[1].value, int64_t(1));

    // --- struct Vec3 ---
    const rtti::flat::fbs_agg_desc* v3 = rtti::flat::find_type(d, "Vec3");
    RTTI_CHECK(v3 != nullptr);
    RTTI_CHECK(v3->stct);
    RTTI_CHECK_EQ(v3->fields.size(), size_t(3));
    RTTI_CHECK_EQ(v3->fields[0].type, rtti::flat::fbs_type::scalar);
    RTTI_CHECK_EQ(v3->fields[0].kind, rtti::flat::fkind::f32);
    RTTI_CHECK_EQ(v3->fields[2].kind, rtti::flat::fkind::f32);

    // --- table Weapon ---
    const rtti::flat::fbs_agg_desc* weapon = rtti::flat::find_type(d, "Weapon");
    RTTI_CHECK(weapon != nullptr);
    RTTI_CHECK(!weapon->stct);
    RTTI_CHECK_EQ(weapon->full_name, std::string("rtti.fixt.Weapon"));
    RTTI_CHECK_EQ(weapon->fields.size(), size_t(2));
    RTTI_CHECK_EQ(weapon->fields[0].name, std::string("name"));
    RTTI_CHECK_EQ(weapon->fields[0].type, rtti::flat::fbs_type::string);
    RTTI_CHECK_EQ(weapon->fields[1].name, std::string("damage"));
    RTTI_CHECK_EQ(weapon->fields[1].type, rtti::flat::fbs_type::scalar);
    RTTI_CHECK_EQ(weapon->fields[1].kind, rtti::flat::fkind::i16);

    // --- table Monster ---
    const rtti::flat::fbs_agg_desc* monster = rtti::flat::find_type(d, "Monster");
    RTTI_CHECK(monster != nullptr);
    RTTI_CHECK_EQ(monster->fields.size(), size_t(8));

    const rtti::flat::fbs_field_desc& pos = monster->fields[0];
    RTTI_CHECK_EQ(pos.name, std::string("pos"));
    RTTI_CHECK_EQ(pos.type, rtti::flat::fbs_type::stct);
    RTTI_CHECK_EQ(pos.ref, std::string("Vec3"));
    RTTI_CHECK(pos.type_index != kNoIndex);

    const rtti::flat::fbs_field_desc& mana = monster->fields[1];
    RTTI_CHECK_EQ(mana.name, std::string("mana"));
    RTTI_CHECK_EQ(mana.type, rtti::flat::fbs_type::scalar);
    RTTI_CHECK_EQ(mana.kind, rtti::flat::fkind::i16);
    RTTI_CHECK(mana.has_default);
    RTTI_CHECK_EQ(mana.dflt_int, int64_t(150));

    const rtti::flat::fbs_field_desc& hp = monster->fields[2];
    RTTI_CHECK_EQ(hp.name, std::string("hp"));
    RTTI_CHECK(hp.has_default);
    RTTI_CHECK_EQ(hp.dflt_int, int64_t(200));

    const rtti::flat::fbs_field_desc& name = monster->fields[3];
    RTTI_CHECK_EQ(name.name, std::string("name"));
    RTTI_CHECK_EQ(name.type, rtti::flat::fbs_type::string);

    const rtti::flat::fbs_field_desc& friendly = monster->fields[4];
    RTTI_CHECK_EQ(friendly.name, std::string("friendly"));
    RTTI_CHECK_EQ(friendly.kind, rtti::flat::fkind::bool_);
    RTTI_CHECK(friendly.has_default);
    RTTI_CHECK_EQ(friendly.dflt_int, int64_t(0));

    const rtti::flat::fbs_field_desc& inventory = monster->fields[5];
    RTTI_CHECK_EQ(inventory.name, std::string("inventory"));
    RTTI_CHECK_EQ(inventory.type, rtti::flat::fbs_type::vector);
    RTTI_CHECK_EQ(inventory.elem, rtti::flat::fkind::i8);
    RTTI_CHECK_EQ(inventory.ref, std::string(""));

    const rtti::flat::fbs_field_desc& weapons = monster->fields[6];
    RTTI_CHECK_EQ(weapons.name, std::string("weapons"));
    RTTI_CHECK_EQ(weapons.type, rtti::flat::fbs_type::vector);
    RTTI_CHECK_EQ(weapons.elem, rtti::flat::fkind::table);
    RTTI_CHECK_EQ(weapons.ref, std::string("Weapon"));
    RTTI_CHECK(weapons.type_index != kNoIndex);

const rtti::flat::fbs_field_desc& path_f = monster->fields[7];
    RTTI_CHECK_EQ(path_f.name, std::string("path"));
    RTTI_CHECK_EQ(path_f.type, rtti::flat::fbs_type::vector);
    RTTI_CHECK_EQ(path_f.elem, rtti::flat::fkind::stct);
    RTTI_CHECK_EQ(path_f.ref, std::string("Vec3"));
    RTTI_CHECK(path_f.type_index != kNoIndex);

    // root_type resolved to the Monster table.
    RTTI_CHECK(rtti::flat::find_type(d, d.root_type) == monster);
}

RTTI_TEST_CASE(FbsParseFullGrammar) {
    // Exercises attributes, unions, file_identifier, include, comments (line
    // and block), enum defaults by name, hex/negative/inf defaults and the
    // required attribute -- every element has to survive parse into the IR.
    const char schema[] =
        "// line comment\n"
        "/** block comment\n spanning */\n"
        "include \"common.fbs\";\n"
        "namespace rtti.fixt;\n"
        "enum Color:byte { Red=0, Green=1, Blue=2 }\n"
        "union LeafOrNone { Leaf }\n"
        "attribute \"foo\";\n"
        "table Leaf { tag:int; label:string; }\n"
        "table Monster {\n"
        "  name:string (required);\n"
        "  id:ulong = 0xFFFFFFFFFFFFFFFF;\n"
        "  hp:short = -1;\n"
        "  ratio:double = inf;\n"
        "  ratio2:float = -inf;\n"
        "  color:Color = Blue;\n"
        "  items:[Leaf] (deprecated);\n"
        "}\n"
        "root_type Monster;\n"
        "file_identifier \"MONS\";\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    const rtti::flat::fbs_schema_desc& d = r.value;
    RTTI_CHECK_EQ(d.ns, std::string("rtti.fixt"));
    RTTI_CHECK_EQ(d.includes.size(), size_t(1));
    RTTI_CHECK_EQ(d.includes[0], std::string("common.fbs"));
    RTTI_CHECK_EQ(d.file_identifier, std::string("MONS"));
    RTTI_CHECK_EQ(d.root_type, std::string("rtti.fixt.Monster"));

    const rtti::flat::fbs_enum_desc* color = rtti::flat::find_enum(d, "Color");
    RTTI_CHECK(color != nullptr);
    RTTI_CHECK_EQ(color->values.size(), size_t(3));
    RTTI_CHECK_EQ(color->values[2].name, std::string("Blue"));
    RTTI_CHECK_EQ(color->values[2].value, int64_t(2));

    const rtti::flat::fbs_union_desc* uni = rtti::flat::find_union(d, "LeafOrNone");
    RTTI_CHECK(uni != nullptr);
    RTTI_CHECK_EQ(uni->full_name, std::string("rtti.fixt.LeafOrNone"));
    RTTI_CHECK_EQ(uni->variants.size(), size_t(1));
    RTTI_CHECK_EQ(uni->variants[0].name, std::string("Leaf"));
    RTTI_CHECK_EQ(uni->variants[0].value, int64_t(1)); // NONE=0 implicit
    RTTI_CHECK_EQ(uni->variants[0].type_ref, std::string("Leaf"));
    RTTI_CHECK(uni->variants[0].type_index != kNoIndex);

    const rtti::flat::fbs_agg_desc* monster =
        rtti::flat::find_type(d, "Monster");
    RTTI_CHECK(monster != nullptr);
    RTTI_CHECK_EQ(monster->fields.size(), size_t(7));

    const rtti::flat::fbs_field_desc& name = monster->fields[0];
    RTTI_CHECK_EQ(name.name, std::string("name"));
    RTTI_CHECK_EQ(name.type, rtti::flat::fbs_type::string);
    RTTI_CHECK(name.required);

    const rtti::flat::fbs_field_desc& id = monster->fields[1];
    RTTI_CHECK_EQ(id.kind, rtti::flat::fkind::u64);
    RTTI_CHECK(id.has_default);
    // u64 defaults above INT64_MAX clamp to INT64_MAX on the descriptor.
    RTTI_CHECK_EQ(id.dflt_int, (std::numeric_limits<std::int64_t>::max)());

    const rtti::flat::fbs_field_desc& hp = monster->fields[2];
    RTTI_CHECK_EQ(hp.kind, rtti::flat::fkind::i16);
    RTTI_CHECK(hp.has_default);
    RTTI_CHECK_EQ(hp.dflt_int, int64_t(-1));

    const rtti::flat::fbs_field_desc& ratio = monster->fields[3];
    RTTI_CHECK_EQ(ratio.kind, rtti::flat::fkind::f64);
    RTTI_CHECK(ratio.has_default);
    RTTI_CHECK(ratio.dflt_real == std::numeric_limits<double>::infinity());

    const rtti::flat::fbs_field_desc& ratio2 = monster->fields[4];
    RTTI_CHECK_EQ(ratio2.kind, rtti::flat::fkind::f32);
    RTTI_CHECK(ratio2.has_default);
    RTTI_CHECK(ratio2.dflt_real == -std::numeric_limits<double>::infinity());

    const rtti::flat::fbs_field_desc& color_f = monster->fields[5];
    RTTI_CHECK_EQ(color_f.type, rtti::flat::fbs_type::enumref);
    RTTI_CHECK_EQ(color_f.kind, rtti::flat::fkind::i8); // byte underlying
    RTTI_CHECK_EQ(color_f.enum_index, size_t(0));
    RTTI_CHECK(color_f.has_default);
    RTTI_CHECK_EQ(color_f.dflt_int, int64_t(2)); // Blue resolved by name

    const rtti::flat::fbs_field_desc& items = monster->fields[6];
    RTTI_CHECK_EQ(items.type, rtti::flat::fbs_type::vector);
    RTTI_CHECK_EQ(items.elem, rtti::flat::fkind::table);
    RTTI_CHECK_EQ(items.ref, std::string("Leaf"));
    RTTI_CHECK(items.deprecated);

    // The union member resolves to the Leaf table descriptor slot.
    const rtti::flat::fbs_agg_desc* leaf = rtti::flat::find_type(d, "Leaf");
    RTTI_CHECK(leaf != nullptr);
    RTTI_CHECK_EQ(d.types[uni->variants[0].type_index].name, leaf->name);
}

RTTI_TEST_CASE(FbsEnumImplicitValuesAndHex) {
    const char schema[] =
        "enum X:ulong { A, B=0xFF, C }\n"
        "table T { e:X; }\n"
        "root_type T;\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    const rtti::flat::fbs_enum_desc* x = rtti::flat::find_enum(r.value, "X");
    RTTI_CHECK(x != nullptr);
    RTTI_CHECK_EQ(static_cast<int>(x->underlying),
                  static_cast<int>(rtti::flat::fkind::u64));
    RTTI_CHECK_EQ(x->values.size(), size_t(3));
    RTTI_CHECK_EQ(x->values[0].value, int64_t(0));   // implicit 0
    RTTI_CHECK_EQ(x->values[1].value, int64_t(255)); // hex 0xFF
    RTTI_CHECK_EQ(x->values[2].value, int64_t(256)); // prev + 1
}

RTTI_TEST_CASE(FbsUnionExplicitValues) {
    const char schema[] =
        "table Leaf { tag:int; }\n"
        "table Stem { n:int; }\n"
        "union U { Leaf=3, Stem=7 }\n"
        "table Box { p:U; }\n"
        "root_type Box;\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    const rtti::flat::fbs_union_desc* u = rtti::flat::find_union(r.value, "U");
    RTTI_CHECK(u != nullptr);
    RTTI_CHECK_EQ(u->variants.size(), size_t(2));
    RTTI_CHECK_EQ(u->variants[0].name, std::string("Leaf"));
    RTTI_CHECK_EQ(u->variants[0].value, int64_t(3));
    RTTI_CHECK_EQ(u->variants[1].name, std::string("Stem"));
    RTTI_CHECK_EQ(u->variants[1].value, int64_t(7));
    RTTI_CHECK(u->variants[1].type_index != kNoIndex);
}

RTTI_TEST_CASE(FbsIdAttributeRequiredOnAllFields) {
    // flatc: either all fields or no fields must carry `id`; when present the
    // ids must be consecutive from 0. Explicit union ids cover type+value.
    const char schema[] =
        "table Leaf { tag:int; }\n"
        "union U { Leaf }\n"
        "table Box { a:int (id:0); payload:U (id:2); extra:string (id:3); }\n"
        "root_type Box;\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    const rtti::flat::fbs_agg_desc* box =
        rtti::flat::find_type(r.value, "Box");
    RTTI_CHECK(box != nullptr);
    RTTI_CHECK_EQ(box->fields.size(), size_t(3));
    RTTI_CHECK(box->fields[0].has_id);
    RTTI_CHECK_EQ(box->fields[0].id, uint16_t(0));
    RTTI_CHECK(box->fields[1].has_id);
    RTTI_CHECK_EQ(box->fields[1].id, uint16_t(2)); // union value id
RTTI_CHECK_EQ(box->fields[1].type, rtti::flat::fbs_type::unionref);
    RTTI_CHECK(box->fields[2].has_id);
    RTTI_CHECK_EQ(box->fields[2].id, uint16_t(3));
}

RTTI_TEST_CASE(FbsUnionRefIdAtTotalRejected) {
    // D-1 regression: a unionref whose explicit id equals the table's slot
    // total (here one union member -> total 2, id 2) slipped past the
    // `f.id > total` guard; the union-value branch then indexed
    // `seen[f.id]` == `seen[2]` past the end of a size==2 vector (heap OOB,
    // malformed .fbs could crash the parser). It must be a clean
    // proto_bad_field, never undefined behaviour.
    const char schema[] =
        "namespace t; table A { x:int; } union U { A; } "
        "table T { u:U (id: 2); }\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(!r.ok());
    if (!r.ok()) {
        RTTI_CHECK_EQ(r.err.code, rtti::error_code::proto_bad_field);
    }
}

RTTI_TEST_CASE(FbsRealExponentDefaultParsed) {
    // Minor regression: a real default with an exponent like "1.5e3" used to
    // stop the number token at "1.5" and leave "e3" as stray tokens (the file
    // then failed to parse, or worse mis-scanned). The fraction branch must
    // swallow the exponent so strtod sees the full literal.
    const char schema[] = "table T { x:double = 1.5e3; }\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    const rtti::flat::fbs_agg_desc* t = rtti::flat::find_type(r.value, "T");
    RTTI_CHECK(t != nullptr);
    RTTI_CHECK_EQ(t->fields.size(), size_t(1));
    RTTI_CHECK(t->fields[0].has_default);
    RTTI_CHECK(t->fields[0].dflt_real == 1500.0);
}

// --- compile + wire integration -------------------------------------------------

RTTI_TEST_CASE(FbsCompiledMonsterMatchesOfficialGeneratedHeader) {
    // Semantic cross-check against the committed flatc --cpp header: the
    // parsed+compiled schema must agree with the official accessors on field
    // order, vtable slots, scalar kinds and .fbs defaults.
    const std::string path = pick_path({
        "tests/fixtures/flat/basic_generated.h",
        "./tests/fixtures/flat/basic_generated.h",
        "../tests/fixtures/flat/basic_generated.h",
    });
    RTTI_CHECK(!path.empty());
    if (path.empty()) {
        return;
    }
    const std::string hdr = read_file(path);
    RTTI_CHECK(!hdr.empty());
    // Official generated constants (flatc 25.12.19): vtable offsets and
    // accessor defaults for Monster and Color enum values.
    RTTI_CHECK(hdr.find("VT_POS = 4") != std::string::npos);
    RTTI_CHECK(hdr.find("VT_MANA = 6") != std::string::npos);
    RTTI_CHECK(hdr.find("VT_HP = 8") != std::string::npos);
    RTTI_CHECK(hdr.find("VT_NAME = 10") != std::string::npos);
    RTTI_CHECK(hdr.find("VT_FRIENDLY = 12") != std::string::npos);
    RTTI_CHECK(hdr.find("VT_INVENTORY = 14") != std::string::npos);
    RTTI_CHECK(hdr.find("VT_WEAPONS = 16") != std::string::npos);
    RTTI_CHECK(hdr.find("VT_PATH = 18") != std::string::npos);
    RTTI_CHECK(hdr.find("Color_Red = 0") != std::string::npos);
    RTTI_CHECK(hdr.find("Color_Green = 1") != std::string::npos);
    RTTI_CHECK(hdr.find("GetField<int16_t>(VT_MANA, 150)") != std::string::npos);
    RTTI_CHECK(hdr.find("GetField<int16_t>(VT_HP, 200)") != std::string::npos);
    RTTI_CHECK(hdr.find("GetField<int32_t>(VT_TAG") == std::string::npos); // not a Monster field

    // Now compile the parsed schema and verify the same facts structurally.
    const std::string spath = pick_path({
        "tests/fixtures/flat/basic.fbs",
        "./tests/fixtures/flat/basic.fbs",
        "../tests/fixtures/flat/basic.fbs",
    });
    RTTI_CHECK(!spath.empty());
    const std::string text = spath.empty() ? std::string(kBasicFbs)
                                           : read_file(spath);
    const auto pr = rtti::flat::parse_schema(
        rtti::string_view(text.data(), text.size()));
    RTTI_CHECK(pr.ok());
    const auto cr = rtti::flat::compile_schema(pr.value);
    RTTI_CHECK(cr.ok());
    if (!cr.ok()) {
        return;
    }
    const rtti::flat::compiled_schema& cs = cr.value;
    const rtti::flat::schema_info* si = cs.info();
    RTTI_CHECK(si != nullptr);
    RTTI_CHECK_EQ(cs.root_name(), rtti::string_view("rtti.fixt.Monster"));
    RTTI_CHECK_EQ(cs.size(), size_t(3));
    RTTI_CHECK(si->identifier == nullptr || si->identifier[0] == '\0');
    RTTI_CHECK_EQ(si->count, size_t(8));
    // Slots 0..7 in declaration order.
    for (std::size_t i = 0; i < 8; ++i) {
        RTTI_CHECK_EQ(static_cast<int>(si->fields[i].slot), int(i));
    }
    RTTI_CHECK_EQ(static_cast<int>(si->fields[0].kind),
                  static_cast<int>(rtti::flat::fkind::stct));
    RTTI_CHECK(si->fields[0].sub != nullptr);
    RTTI_CHECK_EQ(si->fields[0].sub->count, size_t(3)); // Vec3
    RTTI_CHECK(si->fields[1].has_default);
    RTTI_CHECK_EQ(si->fields[1].dflt_int, int64_t(150));
    RTTI_CHECK(si->fields[2].has_default);
    RTTI_CHECK_EQ(si->fields[2].dflt_int, int64_t(200));
    RTTI_CHECK_EQ(static_cast<int>(si->fields[3].kind),
                  static_cast<int>(rtti::flat::fkind::string));
    RTTI_CHECK(si->fields[4].has_default);
    RTTI_CHECK_EQ(si->fields[4].dflt_int, int64_t(0)); // friendly=false
    RTTI_CHECK_EQ(static_cast<int>(si->fields[5].kind),
                  static_cast<int>(rtti::flat::fkind::vector));
    RTTI_CHECK_EQ(static_cast<int>(si->fields[5].elem),
                  static_cast<int>(rtti::flat::fkind::i8));
    RTTI_CHECK_EQ(static_cast<int>(si->fields[6].elem),
                  static_cast<int>(rtti::flat::fkind::table));
    RTTI_CHECK(si->fields[6].sub != nullptr);
    RTTI_CHECK_EQ(si->fields[6].sub->count, size_t(2)); // Weapon
    RTTI_CHECK_EQ(static_cast<int>(si->fields[7].elem),
                  static_cast<int>(rtti::flat::fkind::stct));
    RTTI_CHECK(si->fields[7].sub == si->fields[0].sub); // same Vec3 schema
}

RTTI_TEST_CASE(FbsCompiledSchemaDrivesOfficialMonsterWire) {
    // The whole point: a parsed .fbs drives the flat wire layer over the
    // official flatc-produced monster.bin bytes and reads the same values the
    // official generated accessors expose.
    const auto pr = rtti::flat::compile_schema(
        rtti::string_view(kBasicFbs, sizeof(kBasicFbs) - 1));
    RTTI_CHECK(pr.ok());
    if (!pr.ok()) {
        return;
    }
    const rtti::flat::schema_info* si = pr.value.info();
    rtti::flat::document doc;
    const rtti::error_code e = rtti::flat::wire_to_tree(
        to_sv(kOfficialMonster, sizeof(kOfficialMonster)), si, doc,
        rtti::flat::parse_config{});
    RTTI_CHECK_EQ(e, rtti::error_code::ok);
    bool found = false;
    {
        rtti::value_io* pos = nullptr;
        RTTI_CHECK(doc.get_key("pos", pos));
        RTTI_CHECK_EQ(get_real(pos, "x", found), 1.0);
        RTTI_CHECK_EQ(get_real(pos, "y", found), 2.0);
        RTTI_CHECK_EQ(get_real(pos, "z", found), 3.0);
    }
    RTTI_CHECK_EQ(get_int(&doc, "mana", found), 150);
    RTTI_CHECK_EQ(get_int(&doc, "hp", found), 200);
    RTTI_CHECK_EQ(get_str(&doc, "name", found), std::string("Orc"));
    {
        rtti::value_io* inv = nullptr;
        RTTI_CHECK(doc.get_key("inventory", inv));
        RTTI_CHECK_EQ(inv->size(), size_t(4));
        for (std::size_t i = 0; i < 4; ++i) {
            std::int64_t v = 0;
            RTTI_CHECK(inv->at(i)->as_integer(v));
            RTTI_CHECK_EQ(v, std::int64_t(i));
        }
    }
    {
        rtti::value_io* w = nullptr;
        RTTI_CHECK(doc.get_key("weapons", w));
        RTTI_CHECK_EQ(w->size(), size_t(2));
        RTTI_CHECK_EQ(get_str(w->at(0), "name", found), std::string("Axe"));
        RTTI_CHECK_EQ(get_int(w->at(0), "damage", found), 5);
        RTTI_CHECK_EQ(get_str(w->at(1), "name", found), std::string("Sword"));
        RTTI_CHECK_EQ(get_int(w->at(1), "damage", found), 10);
    }
    {
        rtti::value_io* p = nullptr;
        RTTI_CHECK(doc.get_key("path", p));
        RTTI_CHECK_EQ(p->size(), size_t(2));
        RTTI_CHECK_EQ(get_real(p->at(0), "x", found), 0.0);
        RTTI_CHECK_EQ(get_real(p->at(1), "x", found), 1.0);
        RTTI_CHECK_EQ(get_real(p->at(1), "z", found), 3.0);
    }
}

RTTI_TEST_CASE(FbsSchemaWireRoundtrip) {
    // Encode the officially parsed tree back through the compiled schema and
    // prove the values survive a second decode (spec-equivalent wire).
    const auto pr = rtti::flat::compile_schema(
        rtti::string_view(kBasicFbs, sizeof(kBasicFbs) - 1));
    RTTI_CHECK(pr.ok());
    if (!pr.ok()) {
        return;
    }
    const rtti::flat::schema_info* si = pr.value.info();
    rtti::flat::document doc;
    RTTI_CHECK_EQ(rtti::flat::wire_to_tree(
                      to_sv(kOfficialMonster, sizeof(kOfficialMonster)), si,
                      doc, rtti::flat::parse_config{}),
                  rtti::error_code::ok);
    std::string out;
    RTTI_CHECK_EQ(rtti::flat::tree_to_wire(&doc, si, out,
                                           rtti::flat::parse_config{}),
                  rtti::error_code::ok);
    RTTI_CHECK(out.size() >= 4);
    rtti::flat::document doc2;
    RTTI_CHECK_EQ(rtti::flat::wire_to_tree(
                      rtti::string_view(out.data(), out.size()), si, doc2,
                      rtti::flat::parse_config{}),
                  rtti::error_code::ok);
    bool found = false;
    RTTI_CHECK_EQ(get_str(&doc2, "name", found), std::string("Orc"));
    RTTI_CHECK_EQ(get_int(&doc2, "mana", found), 150);
    {
        rtti::value_io* w = nullptr;
        RTTI_CHECK(doc2.get_key("weapons", w));
        RTTI_CHECK_EQ(w->size(), size_t(2));
        RTTI_CHECK_EQ(get_int(w->at(1), "damage", found), 10);
    }
}

RTTI_TEST_CASE(FbsCompiledUnionSchemaParsesOfficialBytes) {
    const char schema[] =
        "namespace probe.fixt;\n"
        "table Leaf { tag:int; label:string; }\n"
        "union LeafOrNone { Leaf }\n"
        "table Box { payload:LeafOrNone; extra:string; }\n"
        "root_type Box;\n"
        "file_identifier \"MONS\";\n";
    const auto cr = rtti::flat::compile_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(cr.ok());
    if (!cr.ok()) {
        return;
    }
    const rtti::flat::compiled_schema& cs = cr.value;
    const rtti::flat::schema_info* si = cs.info();
    RTTI_CHECK(si != nullptr);
    RTTI_CHECK(si->identifier != nullptr);
    RTTI_CHECK(std::memcmp(si->identifier, "MONS", 4) == 0);
    // Union occupies two consecutive slots: payload_type (0) + payload (1).
    RTTI_CHECK_EQ(si->count, size_t(3));
    RTTI_CHECK_EQ(static_cast<int>(si->fields[0].kind),
                  static_cast<int>(rtti::flat::fkind::union_type));
    RTTI_CHECK_EQ(static_cast<int>(si->fields[1].kind),
                  static_cast<int>(rtti::flat::fkind::union_value));
    RTTI_CHECK_EQ(si->fields[1].union_type_slot, uint16_t(0));
    RTTI_CHECK_EQ(si->fields[1].variant_count, size_t(1));
    RTTI_CHECK_EQ(si->fields[1].variants[0].value, uint32_t(1));
    RTTI_CHECK(si->fields[1].variants[0].schema != nullptr);

    const rtti::string_view bytes =
        to_sv(kOfficialUnion, sizeof(kOfficialUnion));
    RTTI_CHECK(rtti::flat::verify_file_identifier(bytes, "MONS"));
    rtti::flat::document doc;
    RTTI_CHECK_EQ(rtti::flat::wire_to_tree(bytes, si, doc,
                                           rtti::flat::parse_config{}),
                  rtti::error_code::ok);
    bool found = false;
    RTTI_CHECK_EQ(get_int(&doc, "payload_type", found), 1); // Leaf
    {
        rtti::value_io* p = nullptr;
        RTTI_CHECK(doc.get_key("payload", p));
        RTTI_CHECK(p != nullptr);
        RTTI_CHECK_EQ(p->type(), rtti::value_io::kind::object);
        RTTI_CHECK_EQ(get_int(p, "tag", found), 7);
        RTTI_CHECK_EQ(get_str(p, "label", found), std::string("hi"));
    }
    RTTI_CHECK_EQ(get_str(&doc, "extra", found), std::string("boxed"));
}

RTTI_TEST_CASE(FbsCompileDefaultsAndFindType) {
    const auto pr = rtti::flat::compile_schema(
        rtti::string_view(kBasicFbs, sizeof(kBasicFbs) - 1));
    RTTI_CHECK(pr.ok());
    if (!pr.ok()) {
        return;
    }
    const rtti::flat::compiled_schema& cs = pr.value;
    const rtti::flat::schema_info* monster = cs.find_type("rtti.fixt.Monster");
    RTTI_CHECK(monster == cs.info()); // root is found by full and simple name
    const rtti::flat::schema_info* v3 = cs.find_type("Vec3");
    RTTI_CHECK(v3 != nullptr);
    RTTI_CHECK_EQ(v3->count, size_t(3));
    RTTI_CHECK(cs.find_type("Missing") == nullptr);
    RTTI_CHECK_EQ(rtti::flat::verify_file_identifier(
                      to_sv(kOfficialMonster, sizeof(kOfficialMonster)),
                      "MONS"),
                  false); // monster.bin has no file identifier
}

// --- error paths (every malformed input -> error_code, never a crash) ----------

RTTI_TEST_CASE(FbsParseErrorPaths) {
    struct Case {
        const char* text;
        rtti::error_code want;
    };
    const Case cases[] = {
        {"table T { x:int; ", rtti::error_code::unexpected_token},         // EOF inside block
        {"table ", rtti::error_code::unexpected_token},                    // missing table name
        {"table T { x:Missing; }", rtti::error_code::unexpected_token},    // unknown type ref
        {"table T { x:int }", rtti::error_code::unexpected_token},         // missing ';'
        {"table T { x:nope = 1; }", rtti::error_code::unexpected_token},   // unknown type w/ default
        {"table T { s:short = 1.5; }", rtti::error_code::proto_type_mismatch},
        {"table T { b:byte = 999; }", rtti::error_code::overflow},
        {"table T { s:string = 3; }", rtti::error_code::proto_bad_field},  // default on non-scalar
        {"/* unterminated", rtti::error_code::unterminated_comment},
        {"table T { s:\"abc; }", rtti::error_code::unterminated_string},
        {"file_identifier \"ABC\";", rtti::error_code::unexpected_token},  // must be 4 bytes
        {"root_type Nope;", rtti::error_code::no_such_field},              // unknown root
        {"struct S { v:[int]; }", rtti::error_code::proto_bad_field},      // struct with vector
        {"struct S { x:int = 3; }", rtti::error_code::proto_bad_field},    // struct default
        {"struct S { x:int (deprecated); }", rtti::error_code::proto_bad_field},
        {"table T { a:int; a:int; }", rtti::error_code::unexpected_token}, // duplicate field name
        {"x ?!;", rtti::error_code::unexpected_token},                     // stray tokens
        {"table T { x:int = 0xFFFFFFFFFFFFFFFFFFFFFF; }",
         rtti::error_code::invalid_number},                                // integer literal overflow
        {"table T { a:int (id: 0); b:int; }",
         rtti::error_code::proto_bad_field},                               // partial ids
        {"table T { a:int (id: 0); b:int (id: 9); }",
         rtti::error_code::proto_bad_field},                               // ids not consecutive
        {"table T { a:int (id: 0); b:int (id: 0); }",
         rtti::error_code::proto_bad_field},                               // duplicate id
        {"union U { S } struct S { x:int; } table T { u:U; }",
         rtti::error_code::unexpected_token},                              // union member struct
        {"table T { a:int (required); }",
         rtti::error_code::proto_bad_field},                               // required scalar
        {"enum E { A } table T { d:E = Z; }",
         rtti::error_code::unexpected_token},                              // unknown enum value
        {"enum E:bool { A }",
         rtti::error_code::unexpected_token},                              // bool enum underlying
    };
    for (const Case& c : cases) {
        const auto r = rtti::flat::parse_schema(
            rtti::string_view(c.text, std::strlen(c.text)));
        if (c.want == rtti::error_code::ok) {
            continue;
        }
        RTTI_CHECK(!r.ok());
        RTTI_CHECK_EQ(r.err.code, c.want);
    }
    // Empty input parses to an empty schema (no error), like flatc accepting
    // a header-only file.
    const auto empty = rtti::flat::parse_schema(rtti::string_view("", 0));
    RTTI_CHECK(empty.ok());
    RTTI_CHECK(empty.value.types.empty());
}

RTTI_TEST_CASE(FbsForwardRefAndNestedStructAccepted) {
    const char schema[] =
        "struct Inner { x:int; }\n"
        "struct Outer { i:Inner; }\n"
        "table A { b:B; }\n"
        "table B { n:int; }\n"
        "root_type A;\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    const rtti::flat::fbs_agg_desc* a = rtti::flat::find_type(r.value, "A");
    RTTI_CHECK(a != nullptr);
    RTTI_CHECK_EQ(a->fields[0].type, rtti::flat::fbs_type::table);
    RTTI_CHECK(a->fields[0].type_index != kNoIndex);
    const rtti::flat::fbs_agg_desc* outer =
        rtti::flat::find_type(r.value, "Outer");
    RTTI_CHECK(outer != nullptr);
    RTTI_CHECK_EQ(outer->fields[0].type, rtti::flat::fbs_type::stct);
    RTTI_CHECK(outer->fields[0].type_index != kNoIndex);
    const auto cr = rtti::flat::compile_schema(r.value);
    RTTI_CHECK(cr.ok());
}

RTTI_TEST_CASE(FbsEnumDefaultsResolve) {
    // Explicit `= NAME` resolves through the enum table; without a default the
    // wire layer falls back to the universal 0 (flatc: GetField(VT_C, 0)).
    const char schema[] =
        "enum Color:byte { Red=3, Green=4 }\n"
        "table T { a:Color = Green; b:Color; }\n"
        "root_type T;\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(r.ok());
    if (!r.ok()) {
        return;
    }
    const rtti::flat::fbs_agg_desc* t = rtti::flat::find_type(r.value, "T");
    RTTI_CHECK(t != nullptr);
    RTTI_CHECK(t->fields[0].has_default);
    RTTI_CHECK_EQ(t->fields[0].dflt_int, int64_t(4));
    RTTI_CHECK(!t->fields[1].has_default);
    const auto cr = rtti::flat::compile_schema(r.value);
    RTTI_CHECK(cr.ok());
    if (!cr.ok()) {
        return;
    }
    RTTI_CHECK(cr.value.info()->fields[0].has_default);
    RTTI_CHECK_EQ(cr.value.info()->fields[0].dflt_int, int64_t(4));
}

RTTI_TEST_CASE(FbsCompileErrors) {
    // No root available -> invalid_root; unknown configured root -> no_such_field.
    const auto a = rtti::flat::compile_schema(rtti::string_view("", 0));
    RTTI_CHECK(!a.ok());
    RTTI_CHECK_EQ(a.err.code, rtti::error_code::invalid_root);

    const char schema[] = "table T { x:int; }\n";
    const auto pr = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(pr.ok());
    if (!pr.ok()) {
        return;
    }
    rtti::flat::compile_config cfg;
    cfg.root_name = "DoesNotExist";
    const auto b = rtti::flat::compile_schema(pr.value, cfg);
    RTTI_CHECK(!b.ok());
    RTTI_CHECK_EQ(b.err.code, rtti::error_code::no_such_field);

    // A table-without-root that names a valid root via config compiles.
    rtti::flat::compile_config cfg2;
    cfg2.root_name = "rtti.fixt.Monster";
    const auto c = rtti::flat::compile_schema(
        rtti::string_view(kBasicFbs, sizeof(kBasicFbs) - 1), cfg2);
    RTTI_CHECK(c.ok());
    RTTI_CHECK_EQ(c.value.root_name(), rtti::string_view("rtti.fixt.Monster"));
}

RTTI_TEST_CASE(FbsErrorInfoLineColumn) {
    // error_info exposes offset/line/column for a clear diagnostic.
    const char text[] = "table T {\n  x:int = 5;\n  y:Missing;\n}\n";
    const auto r = rtti::flat::parse_schema(
        rtti::string_view(text, sizeof(text) - 1));
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ(r.err.code, rtti::error_code::unexpected_token);
    RTTI_CHECK(r.err.offset > 0);
    RTTI_CHECK_EQ(r.err.line(), size_t(3));
    RTTI_CHECK_EQ(r.err.column(), size_t(5));
    RTTI_CHECK(r.err.message() != nullptr);
}

RTTI_TEST_CASE(FbsTwoUnionsStableMemory) {
    // Regression for review C-1: two union fields in one table force several
    // vector reallocations of the compiled name/variant storage. Every
    // field_desc.name (const char*) and union variants pointer must remain
    // valid after all of the allocation has settled.
    const char schema[] =
        "table Leaf { tag:int; label:string; }\n"
        "table Stem { n:int; }\n"
        "union U1 { Leaf, Stem }\n"
        "union U2 { Stem, Leaf }\n"
        "table Two { u1:U1; u2:U2; }\n"
        "root_type Two;\n";
    const auto cr = rtti::flat::compile_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(cr.ok());
    if (!cr.ok()) {
        return;
    }
    const rtti::flat::schema_info* si = cr.value.info();
    RTTI_CHECK_EQ(si->count, size_t(4)); // u1_type,u1,u2_type,u2
    RTTI_CHECK(std::string(si->fields[0].name) == std::string("u1_type"));
    RTTI_CHECK(std::string(si->fields[1].name) == std::string("u1"));
    RTTI_CHECK(std::string(si->fields[2].name) == std::string("u2_type"));
    RTTI_CHECK(std::string(si->fields[3].name) == std::string("u2"));
    // Variant tables must still point at the definitive schemas (Leaf has two
    // fields, Stem one), i.e. the variants array did not dangle on regrow.
    RTTI_CHECK_EQ(si->fields[1].variant_count, size_t(2));
    RTTI_CHECK(si->fields[1].variants != nullptr);
    RTTI_CHECK(si->fields[1].variants[0].schema != nullptr);
    RTTI_CHECK_EQ(si->fields[1].variants[0].schema->count, size_t(2)); // Leaf
    RTTI_CHECK(si->fields[1].variants[1].schema != nullptr);
    RTTI_CHECK_EQ(si->fields[1].variants[1].schema->count, size_t(1)); // Stem
    RTTI_CHECK_EQ(si->fields[3].variant_count, size_t(2));
    RTTI_CHECK(si->fields[3].variants[0].schema != nullptr);
    RTTI_CHECK_EQ(si->fields[3].variants[0].schema->count, size_t(1)); // Stem
    RTTI_CHECK(si->fields[3].variants[1].schema != nullptr);
    RTTI_CHECK_EQ(si->fields[3].variants[1].schema->count, size_t(2)); // Leaf
}

RTTI_TEST_CASE(FbsTwoUnionsWireRoundtrip) {
    // C-1 continuation: encode a buffer with both union fields populated through
    // the compiled (potentially dangling) descriptors and prove the data
    // survives the wire round trip -- silent loss would fail the decode checks.
    const char schema[] =
        "table Leaf { tag:int; }\n"
        "table Stem { n:int; }\n"
        "union U1 { Leaf, Stem }\n"
        "union U2 { Stem, Leaf }\n"
        "table Two { u1:U1; u2:U2; }\n"
        "root_type Two;\n";
    const auto cr = rtti::flat::compile_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(cr.ok());
    if (!cr.ok()) {
        return;
    }
    const rtti::flat::schema_info* si = cr.value.info();
    rtti::flat::document doc;
    // u1 = Leaf { tag:5 }; u2 = Stem { n:9 } (U1: NONE=0,Leaf=1,Stem=2;
    // U2: NONE=0,Stem=1,Leaf=2).
    {
        rtti::value_io* t1 = doc.make_node();
        t1->make_scalar(rtti::field_value::of(std::int64_t(1)));
        doc.set_key("u1_type", t1);
        rtti::value_io* leaf = doc.make_node();
        leaf->make_object();
        rtti::value_io* tt = leaf->make_node();
        tt->make_scalar(rtti::field_value::of(std::int64_t(5)));
        leaf->set_key("tag", tt);
        doc.set_key("u1", leaf);
    }
    {
        rtti::value_io* t2 = doc.make_node();
        t2->make_scalar(rtti::field_value::of(std::int64_t(1)));
        doc.set_key("u2_type", t2);
        rtti::value_io* stem = doc.make_node();
        stem->make_object();
        rtti::value_io* nn = stem->make_node();
        nn->make_scalar(rtti::field_value::of(std::int64_t(9)));
        stem->set_key("n", nn);
        doc.set_key("u2", stem);
    }
    std::string wire;
    RTTI_CHECK_EQ(rtti::flat::tree_to_wire(&doc, si, wire,
                                           rtti::flat::parse_config{}),
                  rtti::error_code::ok);
    rtti::flat::document back;
    RTTI_CHECK_EQ(rtti::flat::wire_to_tree(
                      rtti::string_view(wire.data(), wire.size()), si, back,
                      rtti::flat::parse_config{}),
                  rtti::error_code::ok);
    bool found = false;
    RTTI_CHECK_EQ(get_int(&back, "u1_type", found), 1);
    {
        rtti::value_io* p = nullptr;
        RTTI_CHECK(back.get_key("u1", p));
        p = nullptr;
        RTTI_CHECK(back.get_key("u1", p));
        RTTI_CHECK(p != nullptr);
        RTTI_CHECK_EQ(get_int(p, "tag", found), 5);
    }
    RTTI_CHECK_EQ(get_int(&back, "u2_type", found), 1);
    {
        rtti::value_io* p = nullptr;
        RTTI_CHECK(back.get_key("u2", p));
        RTTI_CHECK(p != nullptr);
        RTTI_CHECK_EQ(get_int(p, "n", found), 9);
    }
}

RTTI_TEST_CASE(FbsVectorOfEnum) {
    // flatc accepts vectors of enum types (`[Color]`; review I-1): the element
    // kind is the enum's underlying scalar, not a table reference.
    const char schema[] =
        "enum Color:byte { Red=0, Green=1, Blue=2 }\n"
        "table P { colors:[Color]; }\n"
        "root_type P;\n";
    const auto pr = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(pr.ok());
    if (!pr.ok()) {
        return;
    }
    const rtti::flat::fbs_agg_desc* p = rtti::flat::find_type(pr.value, "P");
    RTTI_CHECK(p != nullptr);
    RTTI_CHECK_EQ(p->fields[0].type, rtti::flat::fbs_type::vector);
    RTTI_CHECK_EQ(static_cast<int>(p->fields[0].elem),
                  static_cast<int>(rtti::flat::fkind::i8));
    RTTI_CHECK(p->fields[0].enum_index != kNoIndex); // the enum resolved
    const auto cr = rtti::flat::compile_schema(pr.value);
    RTTI_CHECK(cr.ok());
    if (!cr.ok()) {
        return;
    }
    const rtti::flat::schema_info* si = cr.value.info();
    RTTI_CHECK_EQ(static_cast<int>(si->fields[0].kind),
                  static_cast<int>(rtti::flat::fkind::vector));
    RTTI_CHECK_EQ(static_cast<int>(si->fields[0].elem),
                  static_cast<int>(rtti::flat::fkind::i8));
    // Round trip [0,1,2] through the wire layer.
    rtti::flat::document doc;
    rtti::value_io* colors = doc.make_node();
    colors->make_array();
    for (std::int64_t i = 0; i < 3; ++i) {
        rtti::value_io* el = colors->make_node();
        el->make_scalar(rtti::field_value::of(i));
        colors->push_back(el);
    }
    doc.set_key("colors", colors);
    std::string wire;
    RTTI_CHECK_EQ(rtti::flat::tree_to_wire(&doc, si, wire,
                                           rtti::flat::parse_config{}),
                  rtti::error_code::ok);
    rtti::flat::document back;
    RTTI_CHECK_EQ(rtti::flat::wire_to_tree(
                      rtti::string_view(wire.data(), wire.size()), si, back,
                      rtti::flat::parse_config{}),
                  rtti::error_code::ok);
    rtti::value_io* c = nullptr;
    RTTI_CHECK(back.get_key("colors", c));
    RTTI_CHECK(c != nullptr);
    RTTI_CHECK_EQ(c->size(), size_t(3));
    for (std::size_t i = 0; i < 3; ++i) {
        std::int64_t v = 0;
        RTTI_CHECK(c->at(i)->as_integer(v));
        RTTI_CHECK_EQ(v, std::int64_t(i));
    }
}

RTTI_TEST_CASE(FbsQualifiedIdentifiers) {
    // flatc resolves dotted field/vector/root_type references across
    // namespaces (`foo.T1`, `bar.T2`; review I-2).
    const char schema[] =
        "namespace foo;\n"
        "table T1 { n:int; }\n"
        "namespace bar;\n"
        "table T2 { t:foo.T1; v:[foo.T1]; }\n"
        "root_type bar.T2;\n";
    const auto pr = rtti::flat::parse_schema(
        rtti::string_view(schema, sizeof(schema) - 1));
    RTTI_CHECK(pr.ok());
    if (!pr.ok()) {
        return;
    }
    RTTI_CHECK_EQ(pr.value.root_type, std::string("bar.T2"));
    const rtti::flat::fbs_agg_desc* t2 = rtti::flat::find_type(pr.value, "bar.T2");
    RTTI_CHECK(t2 != nullptr);
    RTTI_CHECK_EQ(t2->fields[0].type, rtti::flat::fbs_type::table);
    RTTI_CHECK_EQ(t2->fields[0].ref, std::string("foo.T1"));
    RTTI_CHECK(t2->fields[0].type_index != kNoIndex);
    RTTI_CHECK_EQ(t2->fields[1].type, rtti::flat::fbs_type::vector);
    RTTI_CHECK_EQ(t2->fields[1].elem, rtti::flat::fkind::table);
    RTTI_CHECK_EQ(t2->fields[1].ref, std::string("foo.T1"));
    RTTI_CHECK(t2->fields[1].type_index != kNoIndex);
    RTTI_CHECK_EQ(t2->fields[1].type_index, t2->fields[0].type_index);
    const auto cr = rtti::flat::compile_schema(pr.value);
    RTTI_CHECK(cr.ok());
    if (!cr.ok()) {
        return;
    }
    RTTI_CHECK_EQ(cr.value.root_name(), rtti::string_view("bar.T2"));
    const rtti::flat::schema_info* si = cr.value.info();
    RTTI_CHECK_EQ(static_cast<int>(si->fields[0].kind),
                  static_cast<int>(rtti::flat::fkind::table));
    RTTI_CHECK(si->fields[0].sub != nullptr);
    RTTI_CHECK_EQ(si->fields[0].sub->count, size_t(1));
    RTTI_CHECK_EQ(static_cast<int>(si->fields[1].elem),
                  static_cast<int>(rtti::flat::fkind::table));
    RTTI_CHECK(si->fields[1].sub == si->fields[0].sub);
    // A dotted root_type can also select the compiled root.
    rtti::flat::compile_config cfg;
    cfg.root_name = "foo.T1";
    const auto c2 = rtti::flat::compile_schema(pr.value, cfg);
    RTTI_CHECK(c2.ok());
    RTTI_CHECK_EQ(c2.value.root_name(), rtti::string_view("foo.T1"));
}

} // namespace rtti_test_lib

RTTI_TEST_MAIN()
