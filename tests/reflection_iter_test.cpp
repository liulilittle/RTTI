// Reflection iterator/traversal tests (spec section 10).
// Covers the field-array iterator (forward + reverse), the container-element
// iterator (three access forms: element_value / element_to / index + key),
// out-of-bounds and empty-container safety, and custom allocator/less
// containers. All definitions live in rtti_test_lib for cross-TU uniqueness.

#include "test_harness.hpp"
#include "box.hpp"

#include <rtti/reflect.hpp>

#include <cstddef>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace rtti_test_lib {

struct IterPoint {
    int x = 0;
    int y = 0;
};

struct IterRecord {
    int id = 0;
    std::string label;
    std::vector<int> nums;
    std::map<std::string, int> scores;
    std::set<std::string> tags;
    std::vector<double> empty;
    IterPoint inner;
    std::vector<IterPoint> pts;
};

template <typename T>
struct IterAlloc {
    using value_type = T;
    IterAlloc() noexcept = default;
    template <typename U> IterAlloc(const IterAlloc<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
    template <typename U> struct rebind { using other = IterAlloc<U>; };
    bool operator==(const IterAlloc&) const noexcept { return true; }
    bool operator!=(const IterAlloc&) const noexcept { return false; }
};

struct IterLess {
    bool operator()(const std::string& a, const std::string& b) const noexcept {
        return a > b; // reverse lexicographic order
    }
};

struct IterCustom {
    std::vector<int, IterAlloc<int>> av;
    std::map<std::string, int, IterLess, IterAlloc<std::pair<const std::string, int>>> am;
    std::set<std::string, IterLess, IterAlloc<std::string>> as;
};

struct IterEmpty {
    RTTI_DEFINE_TYPE_INTRUSIVE(IterEmpty);
};

} // namespace rtti_test_lib

RTTI_DEFINE_TYPE(rtti_test_lib::IterPoint, x, y)
RTTI_DEFINE_TYPE(rtti_test_lib::IterRecord, id, label, nums, scores, tags, empty, inner, pts)
RTTI_DEFINE_TYPE(rtti_test_lib::IterCustom, av, am, as)

namespace rtti_test_lib {

RTTI_TEST_CASE(field_iterator_forward_names) {
    std::vector<std::string> names;
    for (auto it = rtti::field_begin<IterRecord>(); it != rtti::field_end<IterRecord>(); ++it) {
        names.push_back((*it)->name);
    }
    RTTI_CHECK_EQ(names.size(), size_t(8));
    RTTI_CHECK_EQ(names[0], std::string("id"));
    RTTI_CHECK_EQ(names[1], std::string("label"));
    RTTI_CHECK_EQ(names[2], std::string("nums"));
    RTTI_CHECK_EQ(names[6], std::string("inner"));
    RTTI_CHECK_EQ(names[7], std::string("pts"));
}

RTTI_TEST_CASE(field_iterator_reverse_matches_forward_reversed) {
    std::vector<std::string> fwd;
    for (auto it = rtti::field_begin<IterRecord>(); it != rtti::field_end<IterRecord>(); ++it) {
        fwd.push_back((*it)->name);
    }
    std::vector<std::string> rev;
    for (auto it = rtti::field_rbegin<IterRecord>(); it != rtti::field_rend<IterRecord>(); ++it) {
        rev.push_back((*it)->name);
    }
    RTTI_CHECK_EQ(rev.size(), fwd.size());
    for (std::size_t i = 0; i < fwd.size(); ++i) {
        RTTI_CHECK_EQ(rev[i], std::string(fwd[fwd.size() - 1 - i]));
    }
}

RTTI_TEST_CASE(field_iterator_bare_aliases) {
    std::size_t n = 0;
    for (auto it = rtti::begin<IterRecord>(); it != rtti::end<IterRecord>(); ++it) {
        ++n;
    }
    RTTI_CHECK_EQ(n, size_t(8));
    n = 0;
    for (auto it = rtti::rbegin<IterRecord>(); it != rtti::rend<IterRecord>(); ++it) {
        ++n;
    }
    RTTI_CHECK_EQ(n, size_t(8));
}

RTTI_TEST_CASE(field_iterator_decrement_and_deref) {
    auto b = rtti::begin<IterRecord>();
    auto e = rtti::end<IterRecord>();
    RTTI_CHECK(b == rtti::begin<IterRecord>());
    RTTI_CHECK(b != e);
    RTTI_CHECK_EQ(std::string((*b)->name), std::string("id"));
    RTTI_CHECK_EQ(std::string(b->name), std::string("id"));
    --e;
    RTTI_CHECK_EQ(std::string((*e)->name), std::string("pts"));
    auto rb = rtti::rbegin<IterRecord>();
    RTTI_CHECK_EQ(std::string((*rb)->name), std::string("pts"));
    ++rb;
    RTTI_CHECK_EQ(std::string((*rb)->name), std::string("inner"));
}

RTTI_TEST_CASE(element_forward_values_and_indices) {
    IterRecord r;
    r.nums = {1, 2, 3};
    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_begin(r, "nums", it), rtti::error_code::ok);
    std::vector<int64_t> values;
    std::vector<std::size_t> indices;
    while (!it.at_end()) {
        values.push_back(it.element_value().as_integer());
        indices.push_back(it.index());
        ++it;
    }
    RTTI_CHECK_EQ(values.size(), size_t(3));
    RTTI_CHECK_EQ(values[0], int64_t(1));
    RTTI_CHECK_EQ(values[1], int64_t(2));
    RTTI_CHECK_EQ(values[2], int64_t(3));
    RTTI_CHECK_EQ(indices[0], size_t(0));
    RTTI_CHECK_EQ(indices[1], size_t(1));
    RTTI_CHECK_EQ(indices[2], size_t(2));
}

RTTI_TEST_CASE(element_reverse_values) {
    IterRecord r;
    r.nums = {1, 2, 3};
    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_rbegin(r, "nums", it), rtti::error_code::ok);
    std::vector<int64_t> values;
    for (; !it.at_end(); ++it) {
        values.push_back(it.element_value().as_integer());
    }
    RTTI_CHECK_EQ(values.size(), size_t(3));
    RTTI_CHECK_EQ(values[0], int64_t(3));
    RTTI_CHECK_EQ(values[1], int64_t(2));
    RTTI_CHECK_EQ(values[2], int64_t(1));
}

RTTI_TEST_CASE(element_reverse_indices) {
    IterRecord r;
    r.nums = {1, 2, 3};
    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_rbegin(r, "nums", it), rtti::error_code::ok);
    std::vector<std::size_t> indices;
    for (; !it.at_end(); ++it) {
        indices.push_back(it.index());
    }
    RTTI_CHECK_EQ(indices.size(), size_t(3));
    RTTI_CHECK_EQ(indices[0], size_t(2));
    RTTI_CHECK_EQ(indices[1], size_t(1));
    RTTI_CHECK_EQ(indices[2], size_t(0));
}

RTTI_TEST_CASE(element_map_key_and_values) {
    IterRecord r;
    r.scores = {{"a", 10}, {"b", 20}, {"c", 30}};
    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_begin(r, "scores", it), rtti::error_code::ok);
    std::vector<std::string> keys;
    std::vector<int64_t> vals;
    for (; !it.at_end(); ++it) {
        keys.push_back(std::string(it.key()));
        vals.push_back(it.element_value().as_integer());
    }
    RTTI_CHECK_EQ(keys.size(), size_t(3));
    RTTI_CHECK_EQ(keys[0], std::string("a"));
    RTTI_CHECK_EQ(keys[1], std::string("b"));
    RTTI_CHECK_EQ(keys[2], std::string("c"));
    RTTI_CHECK_EQ(vals[0], int64_t(10));
    RTTI_CHECK_EQ(vals[1], int64_t(20));
    RTTI_CHECK_EQ(vals[2], int64_t(30));
}

RTTI_TEST_CASE(element_set_key_equals_value) {
    IterRecord r;
    r.tags = {"x", "y"};
    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_begin(r, "tags", it), rtti::error_code::ok);
    RTTI_CHECK_EQ(std::string(it.key()), std::string("x"));
    RTTI_CHECK_EQ(std::string(it.element_value().as_string()), std::string("x"));
    ++it;
    RTTI_CHECK_EQ(std::string(it.key()), std::string("y"));
    RTTI_CHECK_EQ(std::string(it.element_value().as_string()), std::string("y"));
}

RTTI_TEST_CASE(element_deref_past_end_safe) {
    IterRecord r;
    r.nums = {1, 2, 3};
    rtti::element_iterator<IterRecord> end_;
    RTTI_CHECK_EQ(rtti::element_end(r, "nums", end_), rtti::error_code::ok);
    RTTI_CHECK(end_.at_end());
    RTTI_CHECK(end_.element_value().is_none());
    box sc;
    RTTI_CHECK(end_.element_to(&sc) != rtti::error_code::ok);
    RTTI_CHECK_EQ(std::string(end_.key()), std::string());
}

RTTI_TEST_CASE(element_empty_container_begin_equals_end) {
    IterRecord r;
    rtti::element_iterator<IterRecord> b;
    rtti::element_iterator<IterRecord> e;
    RTTI_CHECK_EQ(rtti::element_begin(r, "empty", b), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::element_end(r, "empty", e), rtti::error_code::ok);
    RTTI_CHECK(b == e);
    RTTI_CHECK(b.at_end());
    RTTI_CHECK(b.element_value().is_none());
    box sc;
    RTTI_CHECK(b.element_to(&sc) != rtti::error_code::ok);

    rtti::element_iterator<IterRecord> rb;
    rtti::element_iterator<IterRecord> re;
    RTTI_CHECK_EQ(rtti::element_rbegin(r, "empty", rb), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::element_rend(r, "empty", re), rtti::error_code::ok);
    RTTI_CHECK(rb == re);
    RTTI_CHECK(rb.element_value().is_none());
}

RTTI_TEST_CASE(element_missing_field_returns_no_such_field) {
    IterRecord r;
    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_begin(r, "nope", it), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(rtti::element_end(r, "nope", it), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(rtti::element_rbegin(r, "nope", it), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(rtti::element_rend(r, "nope", it), rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(element_non_container_returns_kind_mismatch) {
    IterRecord r;
    r.label = "hi";
    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_begin(r, "label", it), rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::element_end(r, "label", it), rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::element_rbegin(r, "label", it), rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::element_rend(r, "label", it), rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(element_to_writes_scalar_into_value_io) {
    IterRecord r;
    r.nums = {1, 2, 3};
    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_begin(r, "nums", it), rtti::error_code::ok);
    box b;
    RTTI_CHECK_EQ(it.element_to(&b), rtti::error_code::ok);
    int64_t v = 0;
    RTTI_CHECK(b.as_integer(v));
    RTTI_CHECK_EQ(v, int64_t(1));
    ++it;
    RTTI_CHECK_EQ(it.element_to(&b), rtti::error_code::ok);
    RTTI_CHECK(b.as_integer(v));
    RTTI_CHECK_EQ(v, int64_t(2));
}

RTTI_TEST_CASE(custom_allocator_and_less_element_iteration) {
    IterCustom o;
    o.av.push_back(10);
    o.av.push_back(20);
    o.am.emplace("a", 1);
    o.am.emplace("b", 2);
    o.as.insert("z");
    o.as.insert("a");

    rtti::element_iterator<IterCustom> it;
    RTTI_CHECK_EQ(rtti::element_begin(o, "av", it), rtti::error_code::ok);
    std::vector<int64_t> vals;
    for (; !it.at_end(); ++it) {
        vals.push_back(it.element_value().as_integer());
    }
    RTTI_CHECK_EQ(vals.size(), size_t(2));
    RTTI_CHECK_EQ(vals[0], int64_t(10));
    RTTI_CHECK_EQ(vals[1], int64_t(20));

    RTTI_CHECK_EQ(rtti::element_begin(o, "am", it), rtti::error_code::ok);
    RTTI_CHECK_EQ(std::string(it.key()), std::string("b"));
    ++it;
    RTTI_CHECK_EQ(std::string(it.key()), std::string("a"));

    RTTI_CHECK_EQ(rtti::element_begin(o, "as", it), rtti::error_code::ok);
    RTTI_CHECK_EQ(std::string(it.element_value().as_string()), std::string("z"));
    ++it;
    RTTI_CHECK_EQ(std::string(it.element_value().as_string()), std::string("a"));
}

RTTI_TEST_CASE(nested_struct_element_value_io_roundtrip) {
    IterRecord r;
    r.pts.push_back(IterPoint{1, 2});
    r.pts.push_back(IterPoint{3, 4});

    rtti::element_iterator<IterRecord> it;
    RTTI_CHECK_EQ(rtti::element_begin(r, "pts", it), rtti::error_code::ok);
    box b;
    RTTI_CHECK_EQ(it.element_to(&b), rtti::error_code::ok);
    rtti::value_io* xv = nullptr;
    int64_t x = 0;
    RTTI_CHECK(b.get_key("x", xv));
    RTTI_CHECK(xv != nullptr && xv->as_integer(x));
    RTTI_CHECK_EQ(x, int64_t(1));
    ++it;
    RTTI_CHECK_EQ(it.element_to(&b), rtti::error_code::ok);
    RTTI_CHECK(b.get_key("x", xv));
    RTTI_CHECK(xv != nullptr && xv->as_integer(x));
    RTTI_CHECK_EQ(x, int64_t(3));

    rtti::element_iterator<IterRecord> it2;
    RTTI_CHECK_EQ(rtti::element_begin(r, "pts", it2), rtti::error_code::ok);
    RTTI_CHECK(it2.element_value().type() == rtti::field_value::kind::object);
}

RTTI_TEST_CASE(element_type_query) {
    RTTI_CHECK_EQ(rtti::element_type<IterRecord>("nums")->kind, rtti::type_kind::integer);
    RTTI_CHECK_EQ(rtti::element_type<IterRecord>("scores")->kind, rtti::type_kind::integer);
    RTTI_CHECK_EQ(rtti::element_type<IterRecord>("tags")->kind, rtti::type_kind::string);
    RTTI_CHECK_EQ(rtti::element_type<IterRecord>("pts")->kind, rtti::type_kind::reflected);
    RTTI_CHECK(rtti::element_type<IterRecord>("label") == nullptr);
    RTTI_CHECK(rtti::element_type<IterRecord>("nope") == nullptr);
}

// Zero-field types: forward and reverse scans are empty immediatel and every
// boundary dereference yields nullptr -- no before-begin/one-past pointer is
// ever formed (spec section 0.2), so nothing crashes.
RTTI_TEST_CASE(field_iterator_zero_field_type_safe) {
    IterEmpty e;
    (void)e;
    RTTI_CHECK(rtti::begin<IterEmpty>() == rtti::end<IterEmpty>());
    RTTI_CHECK(rtti::rbegin<IterEmpty>() == rtti::rend<IterEmpty>());
    auto b = rtti::begin<IterEmpty>();
    auto en = rtti::end<IterEmpty>();
    auto rb = rtti::rbegin<IterEmpty>();
    auto re = rtti::rend<IterEmpty>();
    RTTI_CHECK((*b) == nullptr);
    RTTI_CHECK((*en) == nullptr);
    RTTI_CHECK((*rb) == nullptr);
    RTTI_CHECK((*re) == nullptr);
    std::size_t n = 0;
    for (auto it = rtti::begin<IterEmpty>(); it != rtti::end<IterEmpty>(); ++it) {
        ++n;
    }
    RTTI_CHECK_EQ(n, size_t(0));
    n = 0;
    for (auto it = rtti::rbegin<IterEmpty>(); it != rtti::rend<IterEmpty>(); ++it) {
        ++n;
    }
    RTTI_CHECK_EQ(n, size_t(0));
}

} // namespace rtti_test_lib
