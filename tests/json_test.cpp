// JSON adapter tests for the RTTI reflect-core library.
// Covers parser correctness, error paths, serializer round-trips
// (compact + pretty), and the reflect bindings (nested structs, all
// container families, registered enums). English comments only.
//
// Feature-strip note: this file deliberately includes <rtti/json.hpp>. The
// JSON adapter is opt-in through the RTTI_ENABLE_JSON gate (default 1 in
// <rtti/rtti_fwd.hpp>); the reflect core itself never includes
// <rtti/json.hpp>, so defining RTTI_ENABLE_JSON=0 in a translation unit
// neither changes the core nor forces this adapter to exist there.

#include "test_harness.hpp"
#include <rtti/json.hpp>
#include <rtti/convert.hpp>
#include <rtti/reflection.hpp>
#include <rtti/error.hpp>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <unordered_map>
#include <cstdint>
#include <limits>

namespace rtti_test_lib {

struct Inner {
    int a = 0;
    std::string b;

    RTTI_DEFINE_TYPE_INTRUSIVE(Inner, a, b)
};

// Reflected struct holding a non-owning string_view member (T-D #1). A view is
// a text scalar, not a fixed char-code sequence: serialize must emit a JSON
// string ({"s":"hello"}), and the parsed node must be a string.
struct JsonStringViewDoc {
    std::string_view s;
    RTTI_DEFINE_TYPE_INTRUSIVE(JsonStringViewDoc, s)
};

enum class JsonColor { red = 1, green = 2, blue = 3 };

struct JsonInfHolder {
    double d = 0.0;
    RTTI_DEFINE_TYPE_INTRUSIVE(JsonInfHolder, d)
};

struct Player {
    int id = 0;
    double score = 0.0;
    bool active = false;
    std::string name;
    JsonColor color = JsonColor::red;
    std::vector<int> tags;
    std::map<std::string, double> stats;
    std::set<std::string> skills;
    std::queue<int> q;
    std::stack<int> stk;
    std::priority_queue<int> pq;
    Inner inner;

    RTTI_DEFINE_TYPE_INTRUSIVE(Player, id, score, active, name, color, tags,
                               stats, skills, q, stk, pq, inner)
};

// Deep structural equality over two value_io trees. Numeric nodes compare by
// value, so an integer 42 and a real 42.0 count as equal (serialize may
// demote integral doubles to integer nodes on re-parse).
bool same_tree(const rtti::value_io* a, const rtti::value_io* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    const auto ka = a->type();
    const auto kb = b->type();
    if (ka == rtti::value_io::kind::integer || ka == rtti::value_io::kind::real) {
        if (kb != rtti::value_io::kind::integer && kb != rtti::value_io::kind::real) {
            return false;
        }
        double x = 0.0;
        double y = 0.0;
        return a->as_real(x) && b->as_real(y) && x == y;
    }
    if (ka != kb) {
        return false;
    }
    switch (ka) {
        case rtti::value_io::kind::null:
            return true;
        case rtti::value_io::kind::boolean: {
            bool x = false;
            bool y = false;
            return a->as_bool(x) && b->as_bool(y) && x == y;
        }
        case rtti::value_io::kind::string: {
            rtti::string_view x;
            rtti::string_view y;
            return a->as_string(x) && b->as_string(y) && x == y;
        }
        case rtti::value_io::kind::array: {
            if (a->size() != b->size()) {
                return false;
            }
            for (std::size_t i = 0; i < a->size(); ++i) {
                if (!same_tree(a->at(i), b->at(i))) {
                    return false;
                }
            }
            return true;
        }
        case rtti::value_io::kind::object: {
            if (a->size() != b->size()) {
                return false;
            }
            for (std::size_t i = 0; i < a->size(); ++i) {
                rtti::string_view key;
                rtti::value_io* va = nullptr;
                if (!a->get_key_at(i, key, va)) {
                    return false;
                }
                rtti::value_io* vb = nullptr;
                if (!b->get_key(key, vb) || !same_tree(va, vb)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

// SAX streaming test support: a recorder that captures each callback into a
// flat vector so tests can assert the exact event order and payloads.
enum class SaxKind {
    object_start,
    object_end,
    array_start,
    array_end,
    key,
    string_value,
    integer_value,
    real_value,
    bool_value,
    null_value
};

struct SaxEvent {
    SaxKind kind = SaxKind::object_start;
    int64_t i = 0;
    double d = 0.0;
    bool b = false;
    std::string s;
};

// Build a parse_events set that appends every event to `out`.
inline rtti::json::parse_events make_recorder(std::vector<SaxEvent>& out) {
    rtti::json::parse_events ev;
    ev.object_start = [&out]() {
        out.push_back({SaxKind::object_start, 0, 0.0, false, {}});
        return true;
    };
    ev.object_end = [&out]() {
        out.push_back({SaxKind::object_end, 0, 0.0, false, {}});
        return true;
    };
    ev.array_start = [&out]() {
        out.push_back({SaxKind::array_start, 0, 0.0, false, {}});
        return true;
    };
    ev.array_end = [&out]() {
        out.push_back({SaxKind::array_end, 0, 0.0, false, {}});
        return true;
    };
    ev.key = [&out](rtti::string_view v) {
        out.push_back({});
        out.back().kind = SaxKind::key;
        out.back().s.assign(v.data(), v.size());
        return true;
    };
    ev.string_value = [&out](rtti::string_view v) {
        out.push_back({SaxKind::string_value, 0, 0.0, false, {}});
        out.back().s.assign(v.data(), v.size());
        return true;
    };
    ev.integer_value = [&out](int64_t v) {
        out.push_back({});
        out.back().kind = SaxKind::integer_value;
        out.back().i = v;
        return true;
    };
    ev.real_value = [&out](double v) {
        out.push_back({});
        out.back().kind = SaxKind::real_value;
        out.back().d = v;
        return true;
    };
    ev.bool_value = [&out](bool v) {
        out.push_back({});
        out.back().kind = SaxKind::bool_value;
        out.back().b = v;
        return true;
    };
    ev.null_value = [&out]() {
        out.push_back({SaxKind::null_value, 0, 0.0, false, {}});
        return true;
    };
    return ev;
}

} // namespace rtti_test_lib

RTTI_DEFINE_ENUM(rtti_test_lib::JsonColor, red, green, blue)

namespace rtti_test_lib {

RTTI_TEST_CASE(parse_value_io_interface) {
    auto r = rtti::json::parse(
        "{\"a\": 1, \"b\": [true, false, null, 1.5, \"x\"], \"c\": {\"k\": -2}}");
    RTTI_CHECK(r.ok());
    const rtti::value_io* root = &r.value;
    RTTI_CHECK_EQ(root->type(), rtti::value_io::kind::object);
    RTTI_CHECK_EQ(root->size(), std::size_t(3));

    rtti::value_io* a = nullptr;
    RTTI_CHECK(root->get_key("a", a));
    RTTI_CHECK(a != nullptr);
    int64_t ai = 0;
    RTTI_CHECK(a->as_integer(ai));
    RTTI_CHECK_EQ(ai, int64_t(1));

    rtti::value_io* b = nullptr;
    RTTI_CHECK(root->get_key("b", b));
    RTTI_CHECK_EQ(b->type(), rtti::value_io::kind::array);
    RTTI_CHECK_EQ(b->size(), std::size_t(5));
    bool bv = false;
    RTTI_CHECK(b->at(0)->as_bool(bv));
    RTTI_CHECK_EQ(bv, true);
    RTTI_CHECK(b->at(1)->as_bool(bv));
    RTTI_CHECK_EQ(bv, false);
    RTTI_CHECK_EQ(b->at(2)->type(), rtti::value_io::kind::null);
    double rd = 0.0;
    RTTI_CHECK(b->at(3)->as_real(rd));
    RTTI_CHECK_EQ(rd, 1.5);
    rtti::string_view sv;
    RTTI_CHECK(b->at(4)->as_string(sv));
    RTTI_CHECK_EQ(sv, rtti::string_view("x"));

    rtti::value_io* c = nullptr;
    RTTI_CHECK(root->get_key("c", c));
    RTTI_CHECK_EQ(c->type(), rtti::value_io::kind::object);
    rtti::string_view k0;
    rtti::value_io* v0 = nullptr;
    RTTI_CHECK(c->get_key_at(0, k0, v0));
    RTTI_CHECK_EQ(k0, rtti::string_view("k"));
    RTTI_CHECK(v0 != nullptr);
    int64_t ci = 0;
    RTTI_CHECK(v0->as_integer(ci));
    RTTI_CHECK_EQ(ci, int64_t(-2));
}

RTTI_TEST_CASE(parse_string_escapes) {
    auto r = rtti::json::parse("\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"");
    RTTI_CHECK(r.ok());
    rtti::string_view sv;
    RTTI_CHECK(r.value.as_string(sv));
    RTTI_CHECK_EQ(sv, rtti::string_view("a\"b\\c/d\b\f\n\r\t"));

    auto u = rtti::json::parse("\"\\u00e9\\u4e2d\"");
    RTTI_CHECK(u.ok());
    RTTI_CHECK(u.value.as_string(sv));
    // U+00E9 + U+4E2D encoded as UTF-8 bytes.
    RTTI_CHECK_EQ(sv, rtti::string_view("\xc3\xa9\xe4\xb8\xad"));

    // U+1F600 emoji via a surrogate pair.
    auto s = rtti::json::parse("\"\\ud83d\\ude00\"");
    RTTI_CHECK(s.ok());
    RTTI_CHECK(s.value.as_string(sv));
    RTTI_CHECK_EQ(sv, rtti::string_view("\xf0\x9f\x98\x80"));

    auto empty = rtti::json::parse("\"\"");
    RTTI_CHECK(empty.ok());
    RTTI_CHECK(empty.value.as_string(sv));
    RTTI_CHECK_EQ(sv.size(), std::size_t(0));

    auto raw = rtti::json::parse("\"plain\"");
    RTTI_CHECK(raw.ok());
    RTTI_CHECK(raw.value.as_string(sv));
    RTTI_CHECK_EQ(sv, rtti::string_view("plain"));
}

RTTI_TEST_CASE(parse_int_vs_real) {
    auto i = rtti::json::parse("42");
    RTTI_CHECK(i.ok());
    RTTI_CHECK_EQ(i.value.type(), rtti::value_io::kind::integer);
    int64_t iv = 0;
    RTTI_CHECK(i.value.as_integer(iv));
    RTTI_CHECK_EQ(iv, int64_t(42));

    auto n = rtti::json::parse("-17");
    RTTI_CHECK(n.ok());
    RTTI_CHECK_EQ(n.value.type(), rtti::value_io::kind::integer);

    auto real = rtti::json::parse("4.2");
    RTTI_CHECK(real.ok());
    RTTI_CHECK_EQ(real.value.type(), rtti::value_io::kind::real);

    auto r2 = rtti::json::parse("1e3");
    RTTI_CHECK(r2.ok());
    RTTI_CHECK_EQ(r2.value.type(), rtti::value_io::kind::real);
    double dv = 0.0;
    RTTI_CHECK(r2.value.as_real(dv));
    RTTI_CHECK_EQ(dv, 1000.0);

    auto r3 = rtti::json::parse("1.5e-3");
    RTTI_CHECK(r3.ok());
    double d3 = 0.0;
    RTTI_CHECK(r3.value.as_real(d3));
    RTTI_CHECK_EQ(d3, 0.0015);

    // int64 boundary: fits in an integer node.
    auto big = rtti::json::parse("9223372036854775807");
    RTTI_CHECK(big.ok());
    RTTI_CHECK_EQ(big.value.type(), rtti::value_io::kind::integer);
    RTTI_CHECK(big.value.as_integer(iv));
    RTTI_CHECK_EQ(iv, int64_t(9223372036854775807LL));

    // One past int64 max demotes to a real node.
    auto huge = rtti::json::parse("9223372036854775808");
    RTTI_CHECK(huge.ok());
    RTTI_CHECK_EQ(huge.value.type(), rtti::value_io::kind::real);

    // Integer nodes also feed double conversions (JSON convenience).
    double via = 0.0;
    RTTI_CHECK(i.value.as_real(via));
    RTTI_CHECK_EQ(via, 42.0);
}

RTTI_TEST_CASE(parse_true_false_null_whitespace) {
    auto t = rtti::json::parse("  true  ");
    RTTI_CHECK(t.ok());
    RTTI_CHECK_EQ(t.value.type(), rtti::value_io::kind::boolean);
    bool b = false;
    RTTI_CHECK(t.value.as_bool(b));
    RTTI_CHECK_EQ(b, true);

    auto f = rtti::json::parse("\n\tfalse\r");
    RTTI_CHECK(f.ok());
    RTTI_CHECK_EQ(f.value.type(), rtti::value_io::kind::boolean);

    auto n = rtti::json::parse("null");
    RTTI_CHECK(n.ok());
    RTTI_CHECK_EQ(n.value.type(), rtti::value_io::kind::null);
    RTTI_CHECK_EQ(n.value.size(), std::size_t(0));
}

RTTI_TEST_CASE(parse_arrays_objects_empty) {
    auto empty_obj = rtti::json::parse("{}");
    RTTI_CHECK(empty_obj.ok());
    RTTI_CHECK_EQ(empty_obj.value.type(), rtti::value_io::kind::object);
    RTTI_CHECK_EQ(empty_obj.value.size(), std::size_t(0));

    auto empty_arr = rtti::json::parse("[]");
    RTTI_CHECK(empty_arr.ok());
    RTTI_CHECK_EQ(empty_arr.value.type(), rtti::value_io::kind::array);
    RTTI_CHECK_EQ(empty_arr.value.size(), std::size_t(0));

    auto nested = rtti::json::parse("[[1,2],[3,4]]");
    RTTI_CHECK(nested.ok());
    RTTI_CHECK_EQ(nested.value.at(0)->at(1)->type(), rtti::value_io::kind::integer);
}

RTTI_TEST_CASE(parse_error_codes) {
    auto bad_token = rtti::json::parse("tru");
    RTTI_CHECK(!bad_token.ok());
    RTTI_CHECK_EQ(bad_token.err.code, rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(bad_token.err.offset, std::size_t(0));

    auto bad_key = rtti::json::parse("{a:1}");
    RTTI_CHECK(!bad_key.ok());
    RTTI_CHECK_EQ(bad_key.err.code, rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(bad_key.err.offset, std::size_t(1));

    auto trailing = rtti::json::parse("[1,]");
    RTTI_CHECK(!trailing.ok());
    RTTI_CHECK_EQ(trailing.err.code, rtti::error_code::unexpected_token);

    auto trailing_root = rtti::json::parse("truex");
    RTTI_CHECK(!trailing_root.ok());
    RTTI_CHECK_EQ(trailing_root.err.code, rtti::error_code::unexpected_token);
    RTTI_CHECK_EQ(trailing_root.err.offset, std::size_t(4));

    auto unterminated = rtti::json::parse("\"abc");
    RTTI_CHECK(!unterminated.ok());
    RTTI_CHECK_EQ(unterminated.err.code, rtti::error_code::unterminated_string);
    RTTI_CHECK_EQ(unterminated.err.offset, std::size_t(0));

    auto bad_lit = rtti::json::parse("[truese]");
    RTTI_CHECK(!bad_lit.ok());
    RTTI_CHECK_EQ(bad_lit.err.code, rtti::error_code::unexpected_token);

    auto bad_escape = rtti::json::parse("\"\\x\"");
    RTTI_CHECK(!bad_escape.ok());
    RTTI_CHECK_EQ(bad_escape.err.code, rtti::error_code::unexpected_token);

    auto bad_surrogate = rtti::json::parse("\"\\ud800\"");
    RTTI_CHECK(!bad_surrogate.ok());
    RTTI_CHECK_EQ(bad_surrogate.err.code, rtti::error_code::invalid_utf8);

    // G-4: a high surrogate must be followed by a low-surrogate escape, a
    // low surrogate must not appear alone. Both lone-surrogate shapes and an
    // unpaired high surrogate ahead of ordinary text are rejected as
    // invalid_utf8 (json.cpp surrogate-pair validation).
    auto bad_low = rtti::json::parse("\"\\udfff\"");
    RTTI_CHECK(!bad_low.ok());
    RTTI_CHECK_EQ(bad_low.err.code, rtti::error_code::invalid_utf8);

    auto bad_high_trail = rtti::json::parse("\"\\ud800x\"");
    RTTI_CHECK(!bad_high_trail.ok());
    RTTI_CHECK_EQ(bad_high_trail.err.code, rtti::error_code::invalid_utf8);

    auto bad_hex = rtti::json::parse("\"\\u12xz\"");
    RTTI_CHECK(!bad_hex.ok());
    RTTI_CHECK_EQ(bad_hex.err.code, rtti::error_code::invalid_utf8);
}

RTTI_TEST_CASE(parse_error_numbers) {
    auto lead_zero = rtti::json::parse("01");
    RTTI_CHECK(!lead_zero.ok());
    RTTI_CHECK_EQ(lead_zero.err.code, rtti::error_code::invalid_number);
    RTTI_CHECK_EQ(lead_zero.err.offset, std::size_t(0));

    auto dot = rtti::json::parse("1.");
    RTTI_CHECK(!dot.ok());
    RTTI_CHECK_EQ(dot.err.code, rtti::error_code::invalid_number);

    auto bare_dot = rtti::json::parse(".5");
    RTTI_CHECK(!bare_dot.ok());
    RTTI_CHECK_EQ(bare_dot.err.code, rtti::error_code::unexpected_token);

    auto exp = rtti::json::parse("1e");
    RTTI_CHECK(!exp.ok());
    RTTI_CHECK_EQ(exp.err.code, rtti::error_code::invalid_number);

    auto plus = rtti::json::parse("+1");
    RTTI_CHECK(!plus.ok());
    RTTI_CHECK_EQ(plus.err.code, rtti::error_code::unexpected_token);

    auto empty_all = rtti::json::parse("");
    RTTI_CHECK(!empty_all.ok());
    RTTI_CHECK_EQ(empty_all.err.code, rtti::error_code::invalid_root);

    auto empty_ws = rtti::json::parse(" \n\t ");
    RTTI_CHECK(!empty_ws.ok());
    RTTI_CHECK_EQ(empty_ws.err.code, rtti::error_code::invalid_root);
}

RTTI_TEST_CASE(parse_depth_limit) {
    std::string deep;
    for (int k = 0; k < 600; ++k) {
        deep.push_back('[');
    }
    deep.push_back('0');
    for (int k = 0; k < 600; ++k) {
        deep.push_back(']');
    }
    auto r = rtti::json::parse(deep);
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ(r.err.code, rtti::error_code::too_deep);

    std::string ok;
    for (int k = 0; k < 512; ++k) {
        ok.push_back('[');
    }
    ok.push_back('0');
    for (int k = 0; k < 512; ++k) {
        ok.push_back(']');
    }
    auto s = rtti::json::parse(ok);
    RTTI_CHECK(s.ok());
}

RTTI_TEST_CASE(serialize_roundtrip_compact_pretty) {
    const rtti::json::write_config compact{0, false};
    const rtti::json::write_config pretty{2, false};

    auto r = rtti::json::parse("{\"a\":[1,2],\"b\":{\"x\":\"y\"},\"c\":null}");
    RTTI_CHECK(r.ok());

    const std::string sc = rtti::json::serialize(r.value, compact);
    RTTI_CHECK_EQ(sc, std::string("{\"a\":[1,2],\"b\":{\"x\":\"y\"},\"c\":null}"));

    const std::string sp = rtti::json::serialize(r.value, pretty);
    RTTI_CHECK(sp.find('\n') != std::string::npos);

    auto back = rtti::json::parse(sp);
    RTTI_CHECK(back.ok());
    RTTI_CHECK(same_tree(&r.value, &back.value));

    auto sc_p = rtti::json::parse(sc);
    RTTI_CHECK(sc_p.ok());
    RTTI_CHECK(same_tree(&r.value, &sc_p.value));

    // Scalar documents round-trip through serialize too.
    auto num = rtti::json::parse("42");
    RTTI_CHECK_EQ(rtti::json::serialize(num.value, compact), std::string("42"));
    auto str = rtti::json::parse("\"hi\"");
    RTTI_CHECK_EQ(rtti::json::serialize(str.value, compact), std::string("\"hi\""));
}

RTTI_TEST_CASE(reflect_roundtrip_all_containers) {
    rtti_test_lib::Player p;
    p.id = 7;
    p.score = 3.5;
    p.active = true;
    p.name = "neo\n\"quoted\"\ttab\\x";
    p.color = rtti_test_lib::JsonColor::green;
    p.tags = {1, 2, 3};
    p.stats = {{"hp", 100.0}, {"mp", 50.5}};
    p.skills = {"bow", "sword"};
    p.q.push(1);
    p.q.push(2);
    p.q.push(3);
    p.stk.push(10);
    p.stk.push(20);
    p.stk.push(30);
    p.pq.push(5);
    p.pq.push(1);
    p.pq.push(9);
    p.inner.a = 4;
    p.inner.b = "sub";

    const std::string pretty = rtti::json::serialize(p);
    RTTI_CHECK(pretty.find('\n') != std::string::npos);

    auto back = rtti::json::parse_as<rtti_test_lib::Player>(pretty);
    RTTI_CHECK(back.ok());
    RTTI_CHECK_EQ(back.value.id, 7);
    RTTI_CHECK_EQ(back.value.score, 3.5);
    RTTI_CHECK_EQ(back.value.active, true);
    RTTI_CHECK_EQ(back.value.name, std::string("neo\n\"quoted\"\ttab\\x"));
    RTTI_CHECK(back.value.color == rtti_test_lib::JsonColor::green);
    RTTI_CHECK_EQ(back.value.tags.size(), std::size_t(3));
    RTTI_CHECK_EQ(back.value.tags[0], 1);
    RTTI_CHECK_EQ(back.value.tags[2], 3);
    RTTI_CHECK_EQ(back.value.stats.size(), std::size_t(2));
    RTTI_CHECK_EQ(back.value.stats.at("hp"), 100.0);
    RTTI_CHECK_EQ(back.value.stats.at("mp"), 50.5);
    RTTI_CHECK_EQ(back.value.skills.size(), std::size_t(2));
    RTTI_CHECK(back.value.skills.count("bow") == std::size_t(1));
    RTTI_CHECK_EQ(back.value.inner.a, 4);
    RTTI_CHECK_EQ(back.value.inner.b, std::string("sub"));

    // queue is FIFO after rebuild.
    RTTI_CHECK_EQ(back.value.q.size(), std::size_t(3));
    RTTI_CHECK_EQ(back.value.q.front(), 1);
    back.value.q.pop();
    RTTI_CHECK_EQ(back.value.q.front(), 2);
    back.value.q.pop();
    RTTI_CHECK_EQ(back.value.q.front(), 3);

    // stack keeps LIFO (top first).
    RTTI_CHECK_EQ(back.value.stk.size(), std::size_t(3));
    RTTI_CHECK_EQ(back.value.stk.top(), 30);
    back.value.stk.pop();
    RTTI_CHECK_EQ(back.value.stk.top(), 20);
    back.value.stk.pop();
    RTTI_CHECK_EQ(back.value.stk.top(), 10);

    // priority_queue re-heapifies to the same pop order.
    RTTI_CHECK_EQ(back.value.pq.size(), std::size_t(3));
    RTTI_CHECK_EQ(back.value.pq.top(), 9);
    back.value.pq.pop();
    RTTI_CHECK_EQ(back.value.pq.top(), 5);
    back.value.pq.pop();
    RTTI_CHECK_EQ(back.value.pq.top(), 1);

    // The serialized tree must match a direct to_value model tree.
    rtti::json::document model;
    RTTI_CHECK_EQ(rtti::to_value(&model, p), rtti::error_code::ok);
    auto reparsed = rtti::json::parse(pretty);
    RTTI_CHECK(reparsed.ok());
    RTTI_CHECK(same_tree(&model, &reparsed.value));

    // Compact form round-trips identically.
    const std::string scomp = rtti::json::serialize(p, rtti::json::write_config{0, false});
    RTTI_CHECK(scomp.find('\n') == std::string::npos);
    auto back2 = rtti::json::parse_as<rtti_test_lib::Player>(scomp);
    RTTI_CHECK(back2.ok());
    RTTI_CHECK_EQ(back2.value.id, 7);
}

RTTI_TEST_CASE(enum_via_registration) {
    RTTI_CHECK_EQ(rtti::json::serialize(rtti_test_lib::JsonColor::blue,
                                        rtti::json::write_config{0, false}),
                  std::string("\"blue\""));

    auto g = rtti::json::parse_as<rtti_test_lib::JsonColor>("\"green\"");
    RTTI_CHECK(g.ok());
    RTTI_CHECK(g.value == rtti_test_lib::JsonColor::green);

    auto bad = rtti::json::parse_as<rtti_test_lib::JsonColor>("7");
    RTTI_CHECK(!bad.ok());
    RTTI_CHECK_EQ(bad.err.code, rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(parse_as_scalars) {
    auto i = rtti::json::parse_as<int>("42");
    RTTI_CHECK(i.ok());
    RTTI_CHECK_EQ(i.value, 42);

    auto d = rtti::json::parse_as<double>("3.14");
    RTTI_CHECK(d.ok());
    RTTI_CHECK_EQ(d.value, 3.14);

    // Integer node feeds a double conversion.
    auto d2 = rtti::json::parse_as<double>("5");
    RTTI_CHECK(d2.ok());
    RTTI_CHECK_EQ(d2.value, 5.0);

    auto b = rtti::json::parse_as<bool>("true");
    RTTI_CHECK(b.ok());
    RTTI_CHECK_EQ(b.value, true);

    auto s = rtti::json::parse_as<std::string>("\"yo\"");
    RTTI_CHECK(s.ok());
    RTTI_CHECK_EQ(s.value, std::string("yo"));

    auto v = rtti::json::parse_as<std::vector<int>>("[1,2,3]");
    RTTI_CHECK(v.ok());
    RTTI_CHECK_EQ(v.value.size(), std::size_t(3));
    RTTI_CHECK_EQ(v.value[2], 3);

    auto m = rtti::json::parse_as<std::map<std::string, int>>("{\"a\":1,\"b\":2}");
    RTTI_CHECK(m.ok());
    RTTI_CHECK_EQ(m.value.size(), std::size_t(2));
    RTTI_CHECK_EQ(m.value.at("b"), 2);

    auto bad = rtti::json::parse_as<int>("\"no\"");
    RTTI_CHECK(!bad.ok());
    RTTI_CHECK_EQ(bad.err.code, rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(missing_key_is_strict_error) {
    auto r = rtti::json::parse_as<rtti_test_lib::Player>("{\"id\":1}");
    RTTI_CHECK(!r.ok());
    RTTI_CHECK_EQ(r.err.code, rtti::error_code::missing_key);
}

RTTI_TEST_CASE(sink_build_and_serialize) {
    // document as a write-side sink.
    std::vector<int> v{1, 2, 3};
    rtti::json::document doc;
    RTTI_CHECK_EQ(rtti::to_value(&doc, v), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::json::serialize(doc, rtti::json::write_config{0, false}),
                  std::string("[1,2,3]"));

    std::map<std::string, int> m{{"a", 1}, {"b", 2}};
    rtti::json::document md;
    RTTI_CHECK_EQ(rtti::to_value(&md, m), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::json::serialize(md, rtti::json::write_config{0, false}),
                  std::string("{\"a\":1,\"b\":2}"));

    // An empty document serializes to null.
    rtti::json::document empty;
    RTTI_CHECK_EQ(rtti::json::serialize(empty, rtti::json::write_config{0, false}),
                  std::string("null"));
}

RTTI_TEST_CASE(sort_keys_deterministic) {
    std::unordered_map<std::string, int> u{{"z", 1}, {"a", 2}, {"m", 3}};
    rtti::json::document d;
    RTTI_CHECK_EQ(rtti::to_value(&d, u), rtti::error_code::ok);
    const std::string sorted = rtti::json::serialize(d, rtti::json::write_config{2, true});
    RTTI_CHECK(sorted.find("\"a\"") < sorted.find("\"m\""));
    RTTI_CHECK(sorted.find("\"m\"") < sorted.find("\"z\""));
    const std::string natural = rtti::json::serialize(d, rtti::json::write_config{0, false});
    RTTI_CHECK(natural.find("\"a\":2") != std::string::npos);
    RTTI_CHECK(natural.find("\"z\":1") != std::string::npos);
}

RTTI_TEST_CASE(sax_stream_event_sequence) {
    std::vector<SaxEvent> got;
    auto r = rtti::json::parse_stream("{\"a\":1,\"b\":[true,null],\"c\":\"x\"}",
                                      make_recorder(got));
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(got.size(), std::size_t(11));
    RTTI_CHECK(got[0].kind == SaxKind::object_start);
    RTTI_CHECK(got[1].kind == SaxKind::key && got[1].s == "a");
    RTTI_CHECK(got[2].kind == SaxKind::integer_value && got[2].i == 1);
    RTTI_CHECK(got[3].kind == SaxKind::key && got[3].s == "b");
    RTTI_CHECK(got[4].kind == SaxKind::array_start);
    RTTI_CHECK(got[5].kind == SaxKind::bool_value && got[5].b == true);
    RTTI_CHECK(got[6].kind == SaxKind::null_value);
    RTTI_CHECK(got[7].kind == SaxKind::array_end);
    RTTI_CHECK(got[8].kind == SaxKind::key && got[8].s == "c");
    RTTI_CHECK(got[9].kind == SaxKind::string_value && got[9].s == "x");
    RTTI_CHECK(got[10].kind == SaxKind::object_end);
}

RTTI_TEST_CASE(sax_stream_cancel_early) {
    std::vector<SaxEvent> got;
    rtti::json::parse_events ev = make_recorder(got);
    int keys_seen = 0;
    ev.key = [&](rtti::string_view k) -> bool {
        ++keys_seen;
        got.push_back({});
        got.back().kind = SaxKind::key;
        got.back().s.assign(k.data(), k.size());
        return keys_seen < 2; // cancel at the second key
    };
    auto r = rtti::json::parse_stream("{\"a\":1,\"b\":2,\"c\":3}", ev);
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(keys_seen, 2);
    RTTI_CHECK_EQ(got.size(), std::size_t(4));
    RTTI_CHECK(got[0].kind == SaxKind::object_start);
    RTTI_CHECK(got[1].kind == SaxKind::key && got[1].s == "a");
    RTTI_CHECK(got[2].kind == SaxKind::integer_value);
    RTTI_CHECK(got[3].kind == SaxKind::key && got[3].s == "b");
}

RTTI_TEST_CASE(sax_stream_syntax_error) {
    auto bad_obj = rtti::json::parse_stream("{bad}", rtti::json::parse_events{});
    RTTI_CHECK(!bad_obj.ok());
    RTTI_CHECK_EQ(bad_obj.err.code, rtti::error_code::unexpected_token);

    auto bad_arr = rtti::json::parse_stream("[1,]", rtti::json::parse_events{});
    RTTI_CHECK(!bad_arr.ok());

    auto empty = rtti::json::parse_stream("", rtti::json::parse_events{});
    RTTI_CHECK(!empty.ok());
    RTTI_CHECK_EQ(empty.err.code, rtti::error_code::invalid_root);

    // A default (all-empty) event set still parses the whole stream (ok).
    auto silent = rtti::json::parse_stream("[1,\"a\",true,null]", rtti::json::parse_events{});
    RTTI_CHECK(silent.ok());

    // Top-level scalar streaming works too.
    auto scalar = rtti::json::parse_stream("42", rtti::json::parse_events{});
    RTTI_CHECK(scalar.ok());
}

RTTI_TEST_CASE(serialize_into_buffer) {
    auto r = rtti::json::parse("{\"a\":1,\"b\":[true,null],\"c\":\"x\"}");
    RTTI_CHECK(r.ok());

    const rtti::json::write_config compact{0, false};
    const std::string expected =
        rtti::json::serialize(r.value, compact);

    std::string buf = "junk-prefix";
    rtti::json::serialize(r.value, buf, compact);
    RTTI_CHECK_EQ(buf, expected);

    // The buffer path matches the default (pretty) config overload too.
    std::string buf2 = "prev";
    rtti::json::serialize(r.value, buf2);
    RTTI_CHECK_EQ(buf2, rtti::json::serialize(r.value));

    // Overwrite semantics: serializing twice to the same buffer replaces it.
    std::string buf3 = "something";
    rtti::json::serialize(r.value, buf3, compact);
    rtti::json::serialize(r.value, buf3, compact);
    RTTI_CHECK_EQ(buf3, expected);

    // An empty document serializes into the buffer as "null".
    rtti::json::document empty;
    std::string eb = "x";
    rtti::json::serialize(empty, eb, compact);
    RTTI_CHECK_EQ(eb, std::string("null"));
}

// C-4 note (no runtime injection): the arena's alloc can fail only when the
// underlying malloc returns nullptr, which is impractical to trigger from a
// unit test. The fix is a correctness review: every node-creation path now
// guards the new_node() result. document::make_array/make_object/make_scalar
// return nullptr, and each parser creation site reports error_code::overflow
// (the closest existing code; error.hpp has no dedicated out-of-memory code)
// instead of dereferencing null. json_large_object_key_lookup below keeps
// exercising those creation APIs through their steady-state path.
RTTI_TEST_CASE(json_large_object_key_lookup) {
    rtti::json::document d;
    d.make_object();
    for (int i = 0; i < 200; ++i) {
        std::string k = "key" + std::to_string(i);
        rtti::value_io* v = d.make_node();
        v->make_scalar(rtti::field_value::of(static_cast<int64_t>(i)));
        d.set_key(k, v);
    }
    rtti::value_io* found = nullptr;
    RTTI_CHECK(d.get_key("key199", found));
    if (found) {
        int64_t val = 0;
        found->as_integer(val);
        RTTI_CHECK(val == 199);
    }
    // Insertion order preserved via get_key_at (hash must not reorder).
    rtti::string_view k0;
    rtti::value_io* v0 = nullptr;
    RTTI_CHECK(d.get_key_at(0, k0, v0));
    if (v0) {
        int64_t val = 0;
        v0->as_integer(val);
        RTTI_CHECK(val == 0);
    }
}

RTTI_TEST_CASE(json_shared_stream_dot_zero) {
    // JSON serialization shares one output stream; a trailing integral real
    // after a fractional one must still keep its ".0".
    const std::string s = rtti::json::serialize(std::vector<double>{1.5, 2.0});
    RTTI_CHECK(s.find("1.5") != std::string::npos);
    RTTI_CHECK(s.find("2.0") != std::string::npos);
    const std::string s2 = rtti::json::serialize(std::vector<double>{2.0, 1.5});
    RTTI_CHECK(s2.find("2.0") != std::string::npos);
}

RTTI_TEST_CASE(string_view_member_serializes_as_string) {
    // T-D #1: a string_view member must serialize as a JSON string scalar,
    // never as a char-code array ([104,101,...]).
    rtti_test_lib::JsonStringViewDoc doc;
    doc.s = "hello";
    const rtti::json::write_config compact{0, false};
    const std::string j = rtti::json::serialize(doc, compact);
    RTTI_CHECK_EQ(j, std::string("{\"s\":\"hello\"}"));

    // The parsed node for the member is a string, not an array.
    auto parsed = rtti::json::parse(j);
    RTTI_CHECK(parsed.ok());
    rtti::value_io* n = nullptr;
    RTTI_CHECK(parsed.value.get_key("s", n));
    RTTI_CHECK(n != nullptr);
    RTTI_CHECK(n->type() == rtti::value_io::kind::string);
    rtti::string_view got;
    RTTI_CHECK(n->as_string(got));
    RTTI_CHECK_EQ(got, rtti::string_view("hello"));
}

// T-D #5: a non-finite double must not serialize to "null" (which cannot be
// read back — the parser has no null->double channel). Serialization must
// fail (empty output) instead of silently emitting an unreadable "null".
RTTI_TEST_CASE(json_serialize_nonfinite_rejected) {
    const rtti::json::write_config compact{0, false};

    JsonInfHolder inf;
    inf.d = std::numeric_limits<double>::infinity();
    RTTI_CHECK_EQ(rtti::json::serialize(inf, compact), std::string());

    JsonInfHolder ninf;
    ninf.d = -std::numeric_limits<double>::infinity();
    RTTI_CHECK_EQ(rtti::json::serialize(ninf, compact), std::string());

    JsonInfHolder nan;
    nan.d = std::numeric_limits<double>::quiet_NaN();
    RTTI_CHECK_EQ(rtti::json::serialize(nan, compact), std::string());

    // The buffer overload must also fail (leave the buffer empty), never
    // fall back to "null" that parse_as<double> cannot read back.
    rtti::json::document nd;
    RTTI_CHECK_EQ(rtti::to_value(&nd, nan), rtti::error_code::ok);
    std::string buf = "stale-prefix";
    rtti::json::serialize(nd, buf, compact);
    RTTI_CHECK_EQ(buf, std::string());
}

// P1a: a moved document must keep building correctly through value_io (the
// per-node owner back-pointer is re-pointed on move). Build a tree, move the
// document, extend the tree, then serialize; also move-assign a second time.
RTTI_TEST_CASE(json_document_move_then_build) {
    auto r = rtti::json::parse("{\"a\":1}");
    RTTI_CHECK(r.ok());

    rtti::json::document moved = std::move(r.value);
    RTTI_CHECK_EQ(rtti::json::serialize(moved, rtti::json::write_config{0, false}),
                  std::string("{\"a\":1}"));

    // Continue building after the move: the moved-to arena/pool must serve
    // make_node/set_key/make_scalar.
    rtti::value_io* b = moved.make_node();
    RTTI_CHECK(b != nullptr);
    b->make_scalar(rtti::field_value::of(rtti::string_view("hi")));
    moved.set_key("b", b);
    RTTI_CHECK_EQ(rtti::json::serialize(moved, rtti::json::write_config{0, false}),
                  std::string("{\"a\":1,\"b\":\"hi\"}"));

    // Nested array built after the move (make_node on a child node).
    rtti::value_io* root = &moved;
    RTTI_CHECK(root->type() == rtti::value_io::kind::object);
    rtti::value_io* arr = root->make_node();
    arr->make_array();
    rtti::value_io* el = arr->make_node();
    el->make_scalar(rtti::field_value::of(int64_t(7)));
    arr->push_back(el);
    moved.set_key("arr", arr);
    RTTI_CHECK_EQ(rtti::json::serialize(moved, rtti::json::write_config{0, false}),
                  std::string("{\"a\":1,\"b\":\"hi\",\"arr\":[7]}"));

    // Move-assignment to another document, then read back through the tree.
    rtti::json::document dst;
    dst = std::move(moved);
    rtti::value_io* found = nullptr;
    RTTI_CHECK(dst.get_key("arr", found));
    RTTI_CHECK(found != nullptr);
    RTTI_CHECK_EQ(rtti::json::serialize(dst, rtti::json::write_config{0, false}),
                  std::string("{\"a\":1,\"b\":\"hi\",\"arr\":[7]}"));

    // get_key after moves still matches (linear-scan first-wins).
    rtti::value_io* got = nullptr;
    RTTI_CHECK(dst.get_key("b", got));
    RTTI_CHECK(got != nullptr);
    rtti::string_view sv;
    RTTI_CHECK(got->as_string(sv));
    RTTI_CHECK_EQ(sv, rtti::string_view("hi"));
}

} // namespace rtti_test_lib
