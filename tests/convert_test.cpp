// Convert tests. Task 6 of the RTTI reflect-core plan.
// Covers value_io abstraction, scalar/string converters, and the four
// container families (vector/list/map/set) with allocator transparency.

#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include <rtti/memory.hpp>
#include <rtti/to_string.hpp>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "box.hpp"

// Deep self-referential reflected type for the write-path depth guard (R1-3).
struct ConvertRecursive {
    std::vector<ConvertRecursive> children;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConvertRecursive, children)
};

// Registered enum used to force a child conversion failure in composite
// shapes (R1-4): a value outside the label table makes the enum converter
// report kind_mismatch, which composites must propagate instead of swallowing.
enum class ConvertColor { Red, Green, Blue };

RTTI_DEFINE_ENUM(ConvertColor, Red, Green, Blue)

// Numeric sets for the validated erase_key path (R1-5).
struct ConvertNumSet {
    std::set<int> vals;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConvertNumSet, vals)
};

struct ConvertU64Set {
    std::set<std::uint64_t> vals;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConvertU64Set, vals)
};

// Reflected struct holding an out-of-range uint64 member (R1-6).
struct ConvertU64Holder {
    std::uint64_t big = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConvertU64Holder, big)
};

namespace rtti_test_lib {

RTTI_TEST_CASE(scalar_roundtrip) {
    box* src = make_int(7);
    int64_t out = 0;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, int64_t(7));
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK(dst->as_integer(out));
    RTTI_CHECK_EQ(out, int64_t(7));
    delete dst;
    delete src;
}

RTTI_TEST_CASE(string_roundtrip) {
    box* src = make_str("hello");
    std::string out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out, std::string("hello"));

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::string);
    delete dst;
    delete src;
}

RTTI_TEST_CASE(vector_roundtrip) {
    box* src = make_arr();
    src->push_back(make_int(1));
    src->push_back(make_int(2));
    src->push_back(make_int(3));
    std::vector<int> out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.size(), size_t(3));
    RTTI_CHECK_EQ(out[0], 1);
    RTTI_CHECK_EQ(out[2], 3);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(dst->size(), size_t(3));
    delete dst;
    delete src;
}

RTTI_TEST_CASE(list_roundtrip) {
    box* src = make_arr();
    src->push_back(make_str("a"));
    src->push_back(make_str("b"));
    std::list<std::string> out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.size(), size_t(2));
    RTTI_CHECK_EQ(out.front(), std::string("a"));

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(dst->size(), size_t(2));
    delete dst;
    delete src;
}

RTTI_TEST_CASE(map_roundtrip) {
    box* src = make_obj();
    src->set_key("k1", make_int(10));
    src->set_key("k2", make_int(20));
    std::map<std::string, int> out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.size(), size_t(2));
    RTTI_CHECK_EQ(out["k1"], 10);
    RTTI_CHECK_EQ(out["k2"], 20);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::object);
    rtti::value_io* sub = nullptr;
    RTTI_CHECK(dst->get_key("k2", sub));
    RTTI_CHECK(sub->type() == rtti::value_io::kind::integer);
    delete dst;
    delete src;
}

RTTI_TEST_CASE(set_roundtrip) {
    box* src = make_arr();
    src->push_back(make_int(4));
    src->push_back(make_int(9));
    std::set<int> out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.size(), size_t(2));
    RTTI_CHECK(out.count(4) == 1);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(dst->size(), size_t(2));
    delete dst;
    delete src;
}

// Custom allocator for allocator-transparency tests.
template <typename T>
struct tiny_alloc {
    using value_type = T;
    tiny_alloc() = default;
    template <typename U>
    tiny_alloc(const tiny_alloc<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
    template <typename U>
    bool operator==(const tiny_alloc<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const tiny_alloc<U>&) const noexcept { return false; }
};

RTTI_TEST_CASE(custom_allocator_vector) {
    box* src = make_arr();
    src->push_back(make_int(5));
    std::vector<int, tiny_alloc<int>> out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.size(), size_t(1));
    RTTI_CHECK_EQ(out[0], 5);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(dst->size(), size_t(1));
    delete dst;
    delete src;
}

RTTI_TEST_CASE(unordered_map_roundtrip) {
    box* src = make_obj();
    src->set_key("a", make_int(1));
    std::unordered_map<std::string, int> out;
    RTTI_CHECK_EQ(rtti::from_value(src, out), rtti::error_code::ok);
    RTTI_CHECK_EQ(out.size(), size_t(1));
    RTTI_CHECK_EQ(out["a"], 1);
    delete src;
}

RTTI_TEST_CASE(kind_mismatch_clean_error) {
    // Under §23 (type-safe conversion tolerance) an integer node now reads as
    // the numeric text "5" into a string target, so this probe uses a pair
    // with no §23.1 conversion rule (bool has no real lane) to pin the clean
    // kind_mismatch behavior.
    box* src = new box;
    src->make_scalar(rtti::field_value::of(true));
    double out = 0.0;
    auto e = rtti::from_value(src, out);
    RTTI_CHECK(e != rtti::error_code::ok);
    RTTI_CHECK(e == rtti::error_code::kind_mismatch);
    delete src;
}

// --- Write-path depth guard (R1-3) -------------------------------------------
// to_value/serialize<T>/to_string recurse per data level; an unbounded
// recursive data chain overflowed the stack. The converters now carry a depth
// budget aligned with the parse-side cap and return error_code::too_deep.
RTTI_TEST_CASE(convert_recursive_to_value_too_deep) {
    ConvertRecursive root;
    ConvertRecursive* cur = &root;
    for (int i = 0; i < 600; ++i) {
        cur->children.emplace_back();
        cur = &cur->children.back();
    }

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, root), rtti::error_code::too_deep);
    // The failed child stays owned by its parent: every node created by
    // make_node is attached before the error propagates, so nothing is
    // orphaned (and the box dtor can reclaim the partial tree).
    rtti::value_io* chain = nullptr;
    RTTI_CHECK(dst->get_key("children", chain));
    delete dst;

    box* dst2 = new box;
    const rtti::detail::naming_policy np;
    RTTI_CHECK_EQ(rtti::to_value_named(dst2, root, np),
                  rtti::error_code::too_deep);
    RTTI_CHECK(dst2->get_key("children", chain));
    delete dst2;

    // to_string builds through the same write path: returns empty, no crash.
    RTTI_CHECK(rtti::to_string(root).empty());
}

// --- Composite child errors are propagated (R1-4) ----------------------------
// variant_to / tuple_to_seq / fixed_sequence_to used to discard the child
// to_value result, silently degrading a failed element to an empty node.
RTTI_TEST_CASE(convert_composite_child_error_propagated) {
    const ConvertColor bad = static_cast<ConvertColor>(9);  // outside the table

    box* v = new box;
    std::variant<int, ConvertColor> var = bad;
    RTTI_CHECK_EQ(rtti::to_value(v, var), rtti::error_code::kind_mismatch);
    delete v;

    box* t = new box;
    const std::tuple<int, ConvertColor> tup(1, bad);
    RTTI_CHECK_EQ(rtti::to_value(t, tup), rtti::error_code::kind_mismatch);
    delete t;

    box* a = new box;
    const std::array<ConvertColor, 2> arr = {ConvertColor::Red, bad};
    RTTI_CHECK_EQ(rtti::to_value(a, arr), rtti::error_code::kind_mismatch);
    delete a;

    // Success paths are unchanged: a well-formed composite still converts.
    box* ok = new box;
    const std::variant<int, ConvertColor> good = ConvertColor::Green;
    RTTI_CHECK_EQ(rtti::to_value(ok, good), rtti::error_code::ok);
    int64_t idx = 0;
    rtti::value_io* index_node = nullptr;
    RTTI_CHECK(ok->get_key("index", index_node));
    RTTI_CHECK(index_node->as_integer(idx));
    RTTI_CHECK_EQ(idx, int64_t(1));
    delete ok;
}

// --- erase_key numeric-set validation (R1-5) ----------------------------------
// The numeric-slot parse used a bare strtoll without an endptr: "foo" parsed as
// 0 and "34x" as 34, erasing the wrong element. The key must now be consumed
// entirely and stay in range, otherwise no_such_field.
RTTI_TEST_CASE(convert_erase_key_numeric_validation) {
    ConvertNumSet* obj = new ConvertNumSet;
    obj->vals = {0, 12, 34};
    rtti::object_ref<ConvertNumSet> h = rtti::object_ref<ConvertNumSet>::adopt(obj);

    RTTI_CHECK_EQ(h.erase_key("vals", rtti::string_view("12")),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(obj->vals.count(12), size_t(0));

    RTTI_CHECK_EQ(h.erase_key("vals", rtti::string_view("foo")),
                  rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(obj->vals.count(0), size_t(1));

    RTTI_CHECK_EQ(h.erase_key("vals", rtti::string_view("34x")),
                  rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(obj->vals.count(34), size_t(1));

    RTTI_CHECK_EQ(h.erase_key("vals", rtti::string_view("34")),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(obj->vals.count(34), size_t(0));

    RTTI_CHECK_EQ(h.erase_key("vals", rtti::string_view("0")),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(obj->vals.count(0), size_t(0));

    // A uint64 set key above INT64_MAX must erase by its exact decimal text.
    ConvertU64Set* u = new ConvertU64Set;
    u->vals = {(std::numeric_limits<std::uint64_t>::max)()};
    rtti::object_ref<ConvertU64Set> hu = rtti::object_ref<ConvertU64Set>::adopt(u);
    RTTI_CHECK_EQ(hu.erase_key("vals", rtti::string_view("18446744073709551615")),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(u->vals.size(), size_t(0));
}

// --- uint64 > INT64_MAX must not silently truncate (R1-6) --------------------
RTTI_TEST_CASE(convert_u64_overflow_not_truncated) {
    const std::uint64_t max = (std::numeric_limits<std::uint64_t>::max)();

    box* s = new box;
    RTTI_CHECK_EQ(rtti::to_value(s, max), rtti::error_code::overflow);
    delete s;

    box* v = new box;
    RTTI_CHECK_EQ(rtti::to_value(v, std::vector<std::uint64_t>{max}),
                  rtti::error_code::overflow);
    delete v;

    box* t = new box;
    RTTI_CHECK_EQ(rtti::to_value(t, std::tuple<std::uint64_t>{max}),
                  rtti::error_code::overflow);
    delete t;

    box* a = new box;
    RTTI_CHECK_EQ(rtti::to_value(a, std::array<std::uint64_t, 1>{{max}}),
                  rtti::error_code::overflow);
    delete a;

    box* h = new box;
    ConvertU64Holder holder;
    holder.big = max;
    RTTI_CHECK_EQ(rtti::to_value(h, holder), rtti::error_code::overflow);
    delete h;

    // map_to / sequence_to / reflect_to attach the failing child to the
    // parent before propagating the error; otherwise the make_node'd node
    // is orphaned and leaks (value_io has no free, adapters reclaim only
    // tree-attached nodes).
    box* m = new box;
    const std::map<std::string, std::uint64_t> mm{{"big", max}};
    RTTI_CHECK_EQ(rtti::to_value(m, mm), rtti::error_code::overflow);
    rtti::value_io* m_node = nullptr;
    RTTI_CHECK(m->get_key("big", m_node));
    delete m;

    // Note: `s` above tests the scalar-overflow lane; a distinct sequence lane
    // is checked here.
    box* sq = new box;
    const std::vector<std::uint64_t> sv{max};
    RTTI_CHECK_EQ(rtti::to_value(sq, sv), rtti::error_code::overflow);
    RTTI_CHECK(sq->size() == 1);
    RTTI_CHECK(sq->at(0) != nullptr);
    delete sq;

    box* h2 = new box;
    RTTI_CHECK_EQ(rtti::to_value(h2, holder), rtti::error_code::overflow);
    rtti::value_io* big_node = nullptr;
    RTTI_CHECK(h2->get_key("big", big_node));
    delete h2;

    // In-range unsigned scalar and container paths keep working.
    box* ok = new box;
    RTTI_CHECK_EQ(rtti::to_value(ok, std::uint64_t(12345)), rtti::error_code::ok);
    delete ok;
    box* okv = new box;
    RTTI_CHECK_EQ(rtti::to_value(okv, std::vector<std::uint64_t>{1, 2, 3}),
                  rtti::error_code::ok);
    delete okv;

    // The scalar to_string lane (std::to_string) still renders the full value.
    RTTI_CHECK_EQ(rtti::to_string(max), std::string("18446744073709551615"));
    // Container to_string reports failure (empty) instead of a wrapped "-1".
    RTTI_CHECK(rtti::to_string(std::vector<std::uint64_t>{max}).empty());
    // Reflected serialization likewise fails closed.
    RTTI_CHECK(rtti::to_string(holder).empty());
}

} // namespace rtti_test_lib
