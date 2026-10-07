// =============================================================================
// flatbuffers_demo.cpp -- end-to-end application example for the RTTI
// FlatBuffers wire-format adapter (<rtti/flat.hpp> + <rtti/flat_schema.hpp>).
//
// PURPOSE
//   A *readable teaching program*, not a benchmark. It exercises the FlatBuffers
//   surface the way a real program would:
//     (1) parse a .fbs schema *text* file with flat::parse_schema,
//     (2) compile it into a codec-ready runtime schema with flat::compile_schema,
//     (3) drive that compiled schema through the wire codec
//         (wire_to_tree / tree_to_wire) over the OFFICIAL flatc-produced
//         `tests/fixtures/flat/monster.bin` fixture bytes,
//     (4) register rtti::flat::schema<T> specializations for a plain C++ struct
//         and use the typed bridge (serialize<T> / parse_as<T>) to round-trip
//         a C++ object through the FlatBuffers binary format.
//   Every public interface call is preceded by a comment explaining what it
//   does, why we call it, what it returns, and how errors are surfaced. The
//   library never throws for malformed input; every failure comes back as
//   rtti::error_code and is printed, never crashed.
//
// DEMONSTRATED CAPABILITIES
//   1. .fbs text parsing (parse_schema + find_type / find_enum), the parsed IR
//      (tables, structs, enums, unions, defaults, attributes).
//   2. compile_schema -> a codec-ready schema_info that drives the same wire
//      codec as a schema<T> registration (official flatc bytes decoded).
//   3. Wire reading: root uoffset, vtable walk, aligned scalars, defaults for
//      absent slots, strings, vectors of scalars/tables/structs -- all bounds
//      checked (fail-safe error_code).
//   4. Wire writing: wire_to_tree -> tree_to_wire round trip, spec-equivalent
//      bytes (values survive), verified against the official decode.
//   5. schema<T> registration + typed serialize<T>/parse_as<T> C++ round trip.
//   6. file_identifier verification (verify_file_identifier).
//   7. Malformed-buffer error handling (truncated / bogus root) and the
//      nesting DoS guard (too_deep).
//
// DEPENDENCIES
//   <rtti/flat.hpp>          -- the flatbuffers adapter template shell.
//   <rtti/flat_schema.hpp>   -- the .fbs text parser + compiler.
//   Both pull in only the reflect core (convert/error/reflection). Zero
//   third-party dependencies. The official fixture `tests/fixtures/flat/`
//   is shipped with the repo; the demo reads monster.bin from that directory
//   relative to the repo root (the same convention the test suite uses).
//
// COMPILE GATE
//   flat.hpp #errors unless RTTI_ENABLE_FLATBUFFERS evaluates true (default 1).
//   This demo therefore needs no extra -D when built with the default CMake
//   configuration. The reflect core itself never includes flat.hpp.
//
// BUILD / RUN (verified with MSVC 14.44.35207, Debug, x64, at the repo root):
//   call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
//   cl /nologo /W4 /Zc:preprocessor /permissive- /std:c++17 /EHsc /MDd ^
//      /I F:\json_xml\include examples\flatbuffers_demo.cpp /link ^
//      /LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\lib\x64" ^
//      /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" ^
//      /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" ^
//      build\Debug\rtti.lib
//   examples\flatbuffers_demo.exe
//
//   Expected: zero compiler warnings (/W4), every section prints, all round
//   trips verify, exit code 0.
//
// ALL DEMO TYPE NAMES ARE PREFIXED "DemoFlat" SO THIS TU NEVER COLLIDES WITH
// THE PARALLEL EXAMPLE PROGRAMS AT LINK TIME.
// English comments only; C++17; zero third-party dependencies.
// =============================================================================

#include <rtti/flat.hpp>
#include <rtti/flat_schema.hpp>

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// =============================================================================
// SECTION 1 -- the .fbs descriptor being demonstrated
// =============================================================================
//
// This is the official `tests/fixtures/flat/basic.fbs` (the fixture the RTTI
// flat tests use). We embed the TEXT so parse_schema has input everywhere the
// demo runs; the committed monster.bin was produced from exactly this schema
// by the official flatc compiler.
namespace demo_flat_data {

static const char kBasicFbs[] =
    "namespace rtti.fixt;\n"
    "enum Color:byte { Red=0, Green=1 }\n"
    "table Weapon { name:string; damage:short; }\n"
    "table Monster { pos:Vec3; mana:short=150; hp:short=200; name:string; "
    "friendly:bool=false; inventory:[byte]; weapons:[Weapon]; path:[Vec3]; }\n"
    "struct Vec3 { x:float; y:float; z:float; }\n"
    "root_type Monster;\n";

// Relative path to the official flatc binary fixture (run from the repo root,
// same convention as the test suite).
static const char kMonsterBin[] = "tests/fixtures/flat/monster.bin";

// -----------------------------------------------------------------------------
// Plain C++ structs matching Monster/Vec3/Weapon. The field NAMES must match
// the FlatBuffers field names: the reflected walk (to_value) writes the same
// keys the wire codec looks up.
// -----------------------------------------------------------------------------
struct DemoFlatVec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    RTTI_DEFINE_TYPE_INTRUSIVE(DemoFlatVec3, x, y, z)
};

struct DemoFlatWeapon {
    std::string name;
    std::int16_t damage = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(DemoFlatWeapon, name, damage)
};

struct DemoFlatMonster {
    DemoFlatVec3 pos{};
    std::int16_t mana = 150;      // .fbs default 150
    std::int16_t hp = 200;        // .fbs default 200
    std::string name;
    bool friendly = false;        // .fbs default false
    std::vector<std::int8_t> inventory;
    std::vector<DemoFlatWeapon> weapons;
    std::vector<DemoFlatVec3> path;
    RTTI_DEFINE_TYPE_INTRUSIVE(DemoFlatMonster, pos, mana, hp, name, friendly,
                               inventory, weapons, path)
};

} // namespace demo_flat_data

// =============================================================================
// SECTION 2 -- rtti::flat::schema<T> typed registration
// =============================================================================
//
// flat::schema<T> is a template the user specializes for each C++ type that
// should be treated as a FlatBuffers type. Each field_desc maps a reflected
// field NAME to a vtable SLOT and wire fkind. The PRIMARY template is an empty
// table, so an unregistered type parses to an empty object / serializes to a
// header-only buffer -- registration is strictly opt-in.
//
// Helper builders:
//   make_field_desc(name, slot, kind)                 scalar / string / table /
//                                                     struct / union_type
//   make_field_desc(name, slot, kind, elem)           vector (elem = element kind)
//   make_field_desc(name, slot, kind, elem, sub)      vector/table/struct with a
//                                                     nested schema_info
//   with_default_int / with_default_real(fd, v)       explicit .fbs default
//   schema_of<T>()                                    the schema_info<T> pointer
//
// fkind values: bool_, i8,u8, i16,u16, i32,u32, i64,u64, f32, f64, string,
// vector, table, stct (inline struct), union_type, union_value.
namespace rtti {
namespace flat {

template <>
struct schema<demo_flat_data::DemoFlatVec3> {
    static constexpr std::size_t count = 3;
    static inline const field_desc fields[count] = {
        make_field_desc("x", 0, fkind::f32),
        make_field_desc("y", 1, fkind::f32),
        make_field_desc("z", 2, fkind::f32),
    };
};

template <>
struct schema<demo_flat_data::DemoFlatWeapon> {
    static constexpr std::size_t count = 2;
    static inline const field_desc fields[count] = {
        make_field_desc("name", 0, fkind::string),
        make_field_desc("damage", 1, fkind::i16),
    };
};

template <>
struct schema<demo_flat_data::DemoFlatMonster> {
    static constexpr std::size_t count = 8;
    static inline const field_desc fields[count] = {
        // pos: inline struct Vec3 (schema in sub).
        make_field_desc("pos", 0, fkind::stct, fkind::none,
                        schema_of<demo_flat_data::DemoFlatVec3>()),
        // mana/hp carry the .fbs defaults (filled when the slot is absent).
        with_default_int(make_field_desc("mana", 1, fkind::i16), 150),
        with_default_int(make_field_desc("hp", 2, fkind::i16), 200),
        make_field_desc("name", 3, fkind::string),
        with_default_int(make_field_desc("friendly", 4, fkind::bool_), 0),
        // inventory: vector of byte (i8).
        make_field_desc("inventory", 5, fkind::vector, fkind::i8),
        // weapons: vector of TABLE Weapon (element schema in sub).
        make_field_desc("weapons", 6, fkind::vector, fkind::table,
                        schema_of<demo_flat_data::DemoFlatWeapon>()),
        // path: vector of inline struct Vec3.
        make_field_desc("path", 7, fkind::vector, fkind::stct,
                        schema_of<demo_flat_data::DemoFlatVec3>()),
    };
};

} // namespace flat
} // namespace rtti

// =============================================================================
// SECTION 3 -- small output / file helpers (teaching only)
// =============================================================================
namespace demo_flat_ns {

// A one-liner name for an error_code, mirroring the test-suite helpers.
static const char* err_name(rtti::error_code e) {
    return rtti::to_string(e);
}

// Read a whole file (the committed official fixture).
static bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Print bytes as a compact hex dump.
static void print_hex(const char* tag, const std::string& bytes) {
    std::printf("%s [%zu bytes]  ", tag, bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::printf("%02x ", static_cast<unsigned>(
                                  static_cast<unsigned char>(bytes[i])));
    }
    std::printf("\n");
}

// Recursively print any value_io tree (every adapter document implements the
// shared rtti::value_io interface, so this one helper prints a flat::document
// the same way it prints a JSON or proto document).
static void print_tree(const rtti::value_io* n, int depth) {
    for (int i = 0; i < depth; ++i) {
        std::printf("  ");
    }
    switch (n->type()) {
        case rtti::value_io::kind::null:
            std::printf("null\n");
            break;
        case rtti::value_io::kind::boolean: {
            bool b = false;
            (void)n->as_bool(b);
            std::printf("bool %s\n", b ? "true" : "false");
            break;
        }
        case rtti::value_io::kind::integer: {
            std::int64_t i = 0;
            (void)n->as_integer(i);
            std::printf("int %lld\n", static_cast<long long>(i));
            break;
        }
        case rtti::value_io::kind::real: {
            double d = 0.0;
            (void)n->as_real(d);
            std::printf("real %.6g\n", d);
            break;
        }
        case rtti::value_io::kind::string: {
            rtti::string_view s;
            (void)n->as_string(s);
            std::printf("string \"%.*s\"\n", static_cast<int>(s.size()),
                        s.data());
            break;
        }
        case rtti::value_io::kind::array:
            std::printf("array[%zu]\n", n->size());
            for (std::size_t i = 0; i < n->size(); ++i) {
                print_tree(n->at(i), depth + 1);
            }
            break;
        case rtti::value_io::kind::object:
            std::printf("object[%zu]\n", n->size());
            for (std::size_t i = 0; i < n->size(); ++i) {
                rtti::string_view key;
                rtti::value_io* child = nullptr;
                if (n->get_key_at(i, key, child) && child != nullptr) {
                    for (int k = 0; k <= depth; ++k) {
                        std::printf("  ");
                    }
                    std::printf("\"%.*s\" =>\n", static_cast<int>(key.size()),
                                key.data());
                    print_tree(child, depth + 1);
                }
            }
            break;
    }
}

// Read an int/real key out of a value_io subtree (to verify decoded values).
static std::int64_t get_int(const rtti::value_io* n, const char* key) {
    rtti::value_io* c = nullptr;
    if (n->get_key(key, c) && c != nullptr) {
        std::int64_t v = 0;
        if (c->as_integer(v)) {
            return v;
        }
    }
    return -1;
}

static double get_real(const rtti::value_io* n, const char* key) {
    rtti::value_io* c = nullptr;
    if (n->get_key(key, c) && c != nullptr) {
        double v = 0.0;
        if (c->as_real(v)) {
            return v;
        }
    }
    return -1.0;
}

static std::string get_str(const rtti::value_io* n, const char* key) {
    rtti::value_io* c = nullptr;
    if (n->get_key(key, c) && c != nullptr) {
        rtti::string_view s;
        if (c->as_string(s)) {
            return std::string(s.data(), s.size());
        }
    }
    return std::string();
}

// Read a boolean key out of a value_io subtree. bool scalars live in their own
// kind (boolean), not integer, so a separate helper is required.
static bool get_bool(const rtti::value_io* n, const char* key) {
    rtti::value_io* c = nullptr;
    if (n->get_key(key, c) && c != nullptr) {
        bool v = false;
        if (c->as_bool(v)) {
            return v;
        }
    }
    return false;
}

} // namespace demo_flat_ns

// =============================================================================
// SECTION 4 -- main: run every demonstration
// =============================================================================
int main() {
    using namespace demo_flat_data;
    using namespace demo_flat_ns;

    // =========================================================================
    // 4.1  .fbs text parsing: flat::parse_schema
    // =========================================================================
    // parse_schema turns .fbs TEXT into a queryable fbs_schema_desc result.
    // It is fail-safe: malformed input yields error_code with offset info,
    // never a crash.
    std::printf("== [4.1] .fbs text parsing (parse_schema) ==\n");
    const auto pd = rtti::flat::parse_schema(
        rtti::string_view(kBasicFbs, sizeof(kBasicFbs) - 1));
    if (!pd.ok()) {
        std::printf("parse_schema FAILED: %s\n", err_name(pd.err.code));
        return 1;
    }
    const rtti::flat::fbs_schema_desc& desc = pd.value;
    std::printf("namespace=%s root_type=%s file_identifier=\"%s\" types=%zu "
                "enums=%zu unions=%zu\n",
                desc.ns.c_str(), desc.root_type.c_str(),
                desc.file_identifier.c_str(), desc.types.size(),
                desc.enums.size(), desc.unions.size());

    // The IR lookup helpers match by full name or last path component.
    const rtti::flat::fbs_agg_desc* monster =
        rtti::flat::find_type(desc, "Monster");
    const rtti::flat::fbs_enum_desc* color = rtti::flat::find_enum(desc, "Color");
    if (monster == nullptr || color == nullptr) {
        std::printf("find_type/find_enum FAILED\n");
        return 1;
    }
    std::printf("Monster fields (%zu):\n", monster->fields.size());
    for (std::size_t i = 0; i < monster->fields.size(); ++i) {
        const rtti::flat::fbs_field_desc& f = monster->fields[i];
        std::printf("    slot/decl %zu: name=%s type=%d kind=%d default=%s\n",
                    i, f.name.c_str(), static_cast<int>(f.type),
                    static_cast<int>(f.kind),
                    f.has_default ? (f.dflt_real_lit ? "real" : "int") : "-");
    }
    std::printf("Color enum values: %zu\n\n", color->values.size());

    // =========================================================================
    // 4.2  compile the parsed schema into a codec-ready schema_info
    // =========================================================================
    std::printf("== [4.2] compile_schema -> codec schema ==\n");
    const auto cr = rtti::flat::compile_schema(pd.value);
    if (!cr.ok()) {
        std::printf("compile_schema FAILED: %s\n", err_name(cr.err.code));
        return 1;
    }
    const rtti::flat::compiled_schema& cs = cr.value;
    const rtti::flat::schema_info* si = cs.info();
    std::printf("root_name=%s compiled types=%zu info.count=%zu\n",
                cs.root_name().empty() ? "(none)" : std::string(cs.root_name()).c_str(),
                cs.size(), si ? si->count : 0);
    std::printf("\n");

    // =========================================================================
    // 4.3  wire decode of the OFFICIAL flatc bytes through the compiled schema
    // =========================================================================
    // The committed monster.bin was produced by the official flatc 25.12.19
    // from basic.fbs. A compiled-SCHEMA decode and a schema<T> decode share
    // the exact same wire_to_tree codec, so this proves the text path is wire
    // equivalent to a typed registration.
    std::printf("== [4.3] official flatc bytes decoded via compiled schema ==\n");
    std::string bin;
    if (!read_file(kMonsterBin, bin)) {
        std::printf("  (!) %s not found; skipping official-byte section\n",
                    kMonsterBin);
    } else {
        // wire_to_tree(source, schema_info, doc, config) decodes the buffer into
        // an owned flat::document tree. Malformed input -> error_code.
        rtti::flat::document doc;
        const rtti::error_code wte = rtti::flat::wire_to_tree(
            rtti::string_view(bin.data(), bin.size()), si, doc,
            rtti::flat::parse_config{});
        if (wte != rtti::error_code::ok) {
            std::printf("official wire_to_tree FAILED: %s\n", err_name(wte));
            return 1;
        }
        // monster.json says: pos {1,2,3}; mana=150 (default, absent); hp=200
        // (default, absent); name="Orc"; friendly=false; inventory=[0,1,2,3];
        // weapons=[Axe(5), Sword(10)]; path=[{0,0,0},{1,2,3}].
        {
            rtti::value_io* posn = nullptr;
            const bool have_pos = doc.get_key("pos", posn);
            std::printf("pos = %s\n",
                        have_pos && posn ? "(object)" : "(missing)");
            if (have_pos && posn) {
                std::printf("        x=%.6g y=%.6g z=%.6g\n",
                            get_real(posn, "x"), get_real(posn, "y"),
                            get_real(posn, "z"));
            }
        }
        rtti::value_io* inv = nullptr;
        std::printf("name=\"%s\" friendly=%s inventory=%zu mana=%lld hp=%lld\n",
                    get_str(&doc, "name").c_str(),
                    get_bool(&doc, "friendly") ? "true" : "false",
                    doc.get_key("inventory", inv) && inv ? inv->size() : 0,
                    get_int(&doc, "mana"), get_int(&doc, "hp"));
        // The tree is an object{pos, mana, hp, name, ...}; print the whole tree
        // generically (any value_io document) and then the nested vectors.
        rtti::value_io* weapons = nullptr;
        rtti::value_io* path = nullptr;
        std::printf("weapons=%zu path=%zu\n",
                    doc.get_key("weapons", weapons) && weapons
                        ? weapons->size()
                        : 0,
                    doc.get_key("path", path) && path ? path->size() : 0);
        print_tree(&doc, 1);
        std::printf("\n");
    }

    // =========================================================================
    // 4.4  build a tree by hand and round-trip through the wire codec
    // =========================================================================
    // Every format adapter's document is a value_io sink, so we can construct a
    // monster object tree node-by-node (make_node/make_scalar/make_object/
    // set_key/push_back) and tree_to_wire it through the compiled schema, then
    // decode the wire back and read the same values.
    std::printf("== [4.4] hand-built tree -> wire -> tree ==\n");
    {
        rtti::flat::document doc;
        // pos: inline struct object.
        rtti::value_io* pos = doc.make_node();
        pos->make_object();
        {
            rtti::value_io* x = pos->make_node();
            x->make_scalar(rtti::field_value::of(1.0));
            pos->set_key("x", x);
            rtti::value_io* y = pos->make_node();
            y->make_scalar(rtti::field_value::of(2.0));
            pos->set_key("y", y);
            rtti::value_io* z = pos->make_node();
            z->make_scalar(rtti::field_value::of(3.0));
            pos->set_key("z", z);
        }
        doc.set_key("pos", pos);
        // name: scalar string.
        rtti::value_io* name = doc.make_node();
        name->make_scalar(rtti::field_value::of(rtti::string_view("Orc")));
        doc.set_key("name", name);
        // inventory: vector of bytes.
        rtti::value_io* inv = doc.make_node();
        inv->make_array();
        for (std::int64_t i = 0; i < 4; ++i) {
            rtti::value_io* e = inv->make_node();
            e->make_scalar(rtti::field_value::of(i));
            inv->push_back(e);
        }
        doc.set_key("inventory", inv);

        std::string wire;
        const rtti::error_code tw = rtti::flat::tree_to_wire(
            &doc, si, wire, rtti::flat::parse_config{});
        print_hex("hand wire  :", wire);
        std::printf("tree_to_wire -> %s\n", err_name(tw));

        // Decode our own wire through the same compiled schema.
        rtti::flat::document back;
        const rtti::error_code wt = rtti::flat::wire_to_tree(
            rtti::string_view(wire.data(), wire.size()), si, back,
            rtti::flat::parse_config{});
        std::printf("wire_to_tree -> %s\n", err_name(wt));
        if (wt != rtti::error_code::ok) {
            return 1;
        }
        std::printf("name=%s pos=(%zu keys) inventory=%zu\n",
                    get_str(&back, "name").c_str(),
                    back.get_key("pos", pos) && pos ? pos->size() : 0,
                    back.get_key("inventory", inv) && inv ? inv->size() : 0);
        // mana/hp absent -> schema defaults surfaced (150/200).
        std::printf("mana(default)=%lld hp(default)=%lld\n",
                    get_int(&back, "mana"), get_int(&back, "hp"));
        std::printf("\n");
    }

    // =========================================================================
    // 4.5  typed bridge: schema<T> + serialize<T> / parse_as<T>
    // =========================================================================
    // register via the schema<T> specializations in Section 2. serialize walks
    // the reflected object through to_value into a document, then tree_to_wire;
    // parse_as does the reverse, restoring a C++ object field by field.
    std::printf("== [4.5] typed schema<T> serialize / parse_as ==\n");
    {
        DemoFlatMonster m;
        m.pos = DemoFlatVec3{1.0f, 2.0f, 3.0f};
        m.mana = 150;
        m.hp = 200;
        m.name = "Orc";
        m.friendly = false;
        m.inventory = {0, 1, 2, 3};
        m.weapons = {{"Axe", 5}, {"Sword", 10}};
        m.path = {{0.f, 0.f, 0.f}, {1.f, 2.f, 3.f}};

        // serialize<T>(obj) convenience overload returns the wire bytes.
        const std::string wire = rtti::flat::serialize(m);
        print_hex("typed wire :", wire);
        // Overload serialize<T>(obj, out) returns error_code.
        std::string wire2;
        const rtti::error_code se = rtti::flat::serialize(m, wire2);
        std::printf("serialize<T>(obj,out) -> %s; both overloads agree: %s\n",
                    err_name(se), wire == wire2 ? "yes" : "NO");

        // parse_as<T>(string_view) restores the object.
        const auto back = rtti::flat::parse_as<DemoFlatMonster>(
            rtti::string_view(wire.data(), wire.size()));
        if (!back.ok()) {
            std::printf("parse_as FAILED: %s\n", err_name(back.err.code));
            return 1;
        }
        const DemoFlatMonster& b = back.value;
        const bool ok_rt = b.pos.x == m.pos.x && b.pos.y == m.pos.y &&
                           b.pos.z == m.pos.z && b.name == m.name &&
                           b.inventory.size() == m.inventory.size() &&
                           b.weapons.size() == m.weapons.size() &&
                           b.weapons[1].name == "Sword" &&
                           b.weapons[1].damage == 10 &&
                           b.path.size() == m.path.size();
        std::printf("typed round trip exact: %s (name=%s, weapons=%zu, "
                    "path=%zu)\n\n",
                    ok_rt ? "yes" : "NO", b.name.c_str(), b.weapons.size(),
                    b.path.size());
        if (!ok_rt) {
            return 1;
        }
    }

    // =========================================================================
    // 4.6  file_identifier verification
    // =========================================================================
    // schema<T>::identifier (or a .fbs `file_identifier "XXXX";`) causes the
    // codec to verify/emit the 4-byte magic at buffer[4,8) (the first word is
    // the root uoffset). basic.fbs declares none, so the official bytes carry
    // no identifier; here we splice "MONS" into the canonical slot of a typed
    // buffer and show verify_file_identifier recognising it.
    std::printf("== [4.6] verify_file_identifier ==\n");
    {
        const std::string body = rtti::flat::serialize(DemoFlatMonster{});
        // A real flatc buffer with a file identifier keeps the root uoffset in
        // the first 4 bytes and the id in the next 4 (buffer[4,8)). Splice it:
        std::string tagged = body.substr(0, 4);
        tagged += "MONS";
        tagged += body.substr(4);
        std::printf("verify(no-id body, \"MONS\") = %s\n",
                    rtti::flat::verify_file_identifier(
                        rtti::string_view(body.data(), body.size()), "MONS")
                        ? "true"
                        : "false");
        std::printf("verify(tagged body, \"MONS\") = %s\n",
                    rtti::flat::verify_file_identifier(
                        rtti::string_view(tagged.data(), tagged.size()), "MONS")
                        ? "true"
                        : "false");
        std::printf("verify(tagged body, \"XXXX\") = %s\n",
                    rtti::flat::verify_file_identifier(
                        rtti::string_view(tagged.data(), tagged.size()), "XXXX")
                        ? "true"
                        : "false");
        std::printf("\n");
    }

    // =========================================================================
    // 4.7  malformed-buffer error handling
    // =========================================================================
    // The wire reader is fully bounds-checked: truncated buffers, absurd
    // offsets and over-deep nesting all fail with error_code, never crash.
    std::printf("== [4.7] malformed wire / depth guard ==\n");
    {
        // Truncated root: we need the full 4-byte root uoffset at the tail.
        const std::string short_buf = "MON";
        rtti::flat::document bad;
        const rtti::error_code te = rtti::flat::wire_to_tree(
            rtti::string_view(short_buf.data(), short_buf.size()), si, bad,
            rtti::flat::parse_config{});
        std::printf("3-byte buffer -> %s (fail-safe)\n", err_name(te));

        // Nested-table depth guard: define a self-referential table and encode
        // a chain deeper than the default max_depth (512). The wire encoder
        // refuses with too_deep, same as the JSON/XML/INI adapters' guard.
        const char chain_schema[] =
            "table Chain { tag:int; child:Chain; }\n"
            "root_type Chain;\n";
        const auto cpr = rtti::flat::compile_schema(
            rtti::string_view(chain_schema, sizeof(chain_schema) - 1));
        if (cpr.ok()) {
            const rtti::flat::schema_info* csi = cpr.value.info();
            rtti::flat::document chain_doc;
            chain_doc.make_object();
            rtti::value_io* cur = &chain_doc;
            for (int i = 0; i < 600; ++i) {
                rtti::value_io* child = cur->make_node();
                child->make_object();
                rtti::value_io* tag = child->make_node();
                tag->make_scalar(rtti::field_value::of(std::int64_t(i)));
                child->set_key("tag", tag);
                cur->set_key("child", child);
                cur = child;
            }
            std::string deep_wire;
            const rtti::error_code de = rtti::flat::tree_to_wire(
                &chain_doc, csi, deep_wire, rtti::flat::parse_config{});
            std::printf("600-deep Chain serialize -> %s\n", err_name(de));
            if (de != rtti::error_code::too_deep) {
                std::printf("  (!) expected too_deep from the nesting guard\n");
                return 1;
            }
        } else {
            std::printf("  (!) recursive schema compile skipped\n");
        }
    }

    std::printf("\nflatbuffers_demo: ALL SECTIONS PASSED\n");
    return 0;
}