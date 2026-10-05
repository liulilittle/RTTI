// Runtime container-mutation tests. Extension of the RTTI reflect-core plan:
// add / erase_at / erase_key / clear / size / for_each over container fields,
// across vector/list/map/set and the queue/stack/priority_queue adapters,
// including failure paths (unknown field, wrong kind, out-of-range index).

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include "box.hpp"
#include <list>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

struct Mutation {
    std::vector<int> v;
    std::list<std::string> l;
    std::map<std::string, int> m;
    std::set<int> s;
    std::queue<int> q;
    std::stack<std::string> st;
    std::priority_queue<int> pq;
    int plain = 42;
    RTTI_DEFINE_TYPE_INTRUSIVE(Mutation, v, l, m, s, q, st, pq, plain);
};

namespace rtti_test_lib {

RTTI_TEST_CASE(add_to_vector_field) {
    Mutation o;
    RTTI_CHECK(o.v.empty());
    auto* e = make_int(7);
    RTTI_CHECK_EQ(rtti::add_field(o, "v", e), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.v.size(), size_t(1));
    RTTI_CHECK_EQ(o.v[0], 7);
    RTTI_CHECK_EQ(rtti::add_field(o, "v", e), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.v.size(), size_t(2));
    delete e;
}

RTTI_TEST_CASE(add_to_map_field_key_value) {
    Mutation o;
    auto* pair = new box;
    pair->make_object();
    pair->set_key("k", make_int(9));
    RTTI_CHECK_EQ(rtti::add_field(o, "m", pair), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.m.size(), size_t(1));
    RTTI_CHECK_EQ(o.m["k"], 9);
    delete pair;
}

RTTI_TEST_CASE(add_to_set_field) {
    Mutation o;
    auto* e = make_int(3);
    RTTI_CHECK_EQ(rtti::add_field(o, "s", e), rtti::error_code::ok);
    RTTI_CHECK(o.s.count(3) == 1);
    delete e;
}

RTTI_TEST_CASE(add_field_scalar_overload) {
    Mutation o;
    RTTI_CHECK_EQ(rtti::add_field(o, "v", rtti::field_value::of(int64_t(11))), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.v.size(), size_t(1));
    RTTI_CHECK_EQ(o.v[0], 11);
    RTTI_CHECK_EQ(rtti::add_field(o, "v", rtti::field_value::of(rtti::string_view("x"))), rtti::error_code::kind_mismatch);

    RTTI_CHECK_EQ(rtti::add_field(o, "l", rtti::field_value::of(rtti::string_view("s1"))), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.l.size(), size_t(1));
    RTTI_CHECK_EQ(o.l.front(), std::string("s1"));

    RTTI_CHECK_EQ(rtti::add_field(o, "q", rtti::field_value::of(int64_t(6))), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.q.front(), 6);

    RTTI_CHECK_EQ(rtti::add_field(o, "st", rtti::field_value::of(rtti::string_view("t"))), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.st.top(), std::string("t"));

    RTTI_CHECK_EQ(rtti::add_field(o, "pq", rtti::field_value::of(int64_t(8))), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.pq.top(), 8);

    RTTI_CHECK_EQ(rtti::add_field(o, "plain", rtti::field_value::of(int64_t(1))), rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::add_field(o, "m", rtti::field_value::of(int64_t(1))), rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(add_to_queue_and_stack_and_pq) {
    Mutation o;
    auto* e = make_int(5);
    RTTI_CHECK_EQ(rtti::add_field(o, "q", e), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::add_field(o, "q", e), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.q.size(), size_t(2));
    RTTI_CHECK_EQ(o.q.front(), 5);
    delete e;

    auto* s1 = make_str("a");
    auto* s2 = make_str("b");
    RTTI_CHECK_EQ(rtti::add_field(o, "st", s1), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::add_field(o, "st", s2), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.st.size(), size_t(2));
    RTTI_CHECK_EQ(o.st.top(), std::string("b"));
    delete s1;
    delete s2;

    auto* p1 = make_int(1);
    auto* p2 = make_int(9);
    RTTI_CHECK_EQ(rtti::add_field(o, "pq", p2), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::add_field(o, "pq", p1), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.pq.top(), 9);
    delete p1;
    delete p2;
}

RTTI_TEST_CASE(add_to_non_container_reports_kind_mismatch) {
    Mutation o;
    auto* e = make_int(1);
    RTTI_CHECK_EQ(rtti::add_field(o, "plain", e), rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::add_field(o, "nope", e), rtti::error_code::no_such_field);
    delete e;
}

RTTI_TEST_CASE(erase_at_vector_bounds) {
    Mutation o;
    o.v = {10, 20, 30};
    RTTI_CHECK_EQ(rtti::erase_field_at(o, "v", size_t(1)), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.v.size(), size_t(2));
    RTTI_CHECK_EQ(o.v[0], 10);
    RTTI_CHECK_EQ(o.v[1], 30);
    RTTI_CHECK_EQ(rtti::erase_field_at(o, "v", size_t(9)), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(o.v.size(), size_t(2));
}

RTTI_TEST_CASE(erase_at_list) {
    Mutation o;
    o.l = {"a", "b", "c"};
    RTTI_CHECK_EQ(rtti::erase_field_at(o, "l", size_t(0)), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.l.size(), size_t(2));
    RTTI_CHECK_EQ(o.l.front(), std::string("b"));
}

RTTI_TEST_CASE(erase_at_on_adapter_kind_mismatch) {
    Mutation o;
    RTTI_CHECK_EQ(rtti::erase_field_at(o, "q", size_t(0)), rtti::error_code::kind_mismatch);
    RTTI_CHECK_EQ(rtti::erase_field_at(o, "st", size_t(0)), rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(erase_key_map_and_set) {
    Mutation o;
    o.m["a"] = 1;
    o.m["b"] = 2;
    RTTI_CHECK_EQ(rtti::erase_field_key(o, "m", rtti::string_view("a")), rtti::error_code::ok);
    RTTI_CHECK_EQ(o.m.size(), size_t(1));
    RTTI_CHECK(o.m.count("b") == 1);
    RTTI_CHECK_EQ(rtti::erase_field_key(o, "m", rtti::string_view("zz")), rtti::error_code::no_such_field);

    o.s = {5, 6};
    RTTI_CHECK_EQ(rtti::erase_field_key(o, "s", rtti::string_view("5")), rtti::error_code::ok);
    RTTI_CHECK(o.s.count(6) == 1);
}

RTTI_TEST_CASE(clear_field_all_families) {
    Mutation o;
    o.v = {1};
    o.m["x"] = 1;
    o.s = {2};
    o.q.push(3);
    o.st.push("w");
    o.pq.push(4);
    RTTI_CHECK_EQ(rtti::clear_field(o, "v"), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::clear_field(o, "m"), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::clear_field(o, "s"), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::clear_field(o, "q"), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::clear_field(o, "st"), rtti::error_code::ok);
    RTTI_CHECK_EQ(rtti::clear_field(o, "pq"), rtti::error_code::ok);
    RTTI_CHECK(o.v.empty());
    RTTI_CHECK(o.m.empty());
    RTTI_CHECK(o.s.empty());
    RTTI_CHECK(o.q.empty());
    RTTI_CHECK(o.st.empty());
    RTTI_CHECK(o.pq.empty());
    RTTI_CHECK_EQ(rtti::clear_field(o, "plain"), rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(field_size_all_families) {
    Mutation o;
    o.v = {1, 2};
    o.m["a"] = 1;
    o.s = {7};
    o.q.push(1);
    RTTI_CHECK_EQ(rtti::field_size(o, "v"), size_t(2));
    RTTI_CHECK_EQ(rtti::field_size(o, "m"), size_t(1));
    RTTI_CHECK_EQ(rtti::field_size(o, "s"), size_t(1));
    RTTI_CHECK_EQ(rtti::field_size(o, "q"), size_t(1));

    rtti::error_code ec = rtti::error_code::ok;
    RTTI_CHECK_EQ(rtti::field_size(o, "plain", &ec), size_t(0));
    RTTI_CHECK(ec == rtti::error_code::kind_mismatch);
    ec = rtti::error_code::ok;
    RTTI_CHECK_EQ(rtti::field_size(o, "nope", &ec), size_t(0));
    RTTI_CHECK(ec == rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(for_each_sums_vector) {
    Mutation o;
    o.v = {1, 2, 3, 4};
    long long sum = 0;
    box* scratch = new box;
    const auto ec = rtti::for_each_element(
        o, "v", scratch,
        [&sum](std::size_t, rtti::string_view, const rtti::value_io* elem) -> bool {
            int64_t v = 0;
            elem->as_integer(v);
            sum += v;
            return true;
        });
    RTTI_CHECK_EQ(ec, rtti::error_code::ok);
    RTTI_CHECK_EQ(sum, long long(10));
    delete scratch;
}

RTTI_TEST_CASE(for_each_break_early) {
    Mutation o;
    o.v = {1, 2, 3, 4, 5};
    long long count = 0;
    box* scratch = new box;
    const auto ec = rtti::for_each_element(
        o, "v", scratch,
        [&count](std::size_t, rtti::string_view, const rtti::value_io*) -> bool {
            ++count;
            return count < 3;
        });
    RTTI_CHECK_EQ(ec, rtti::error_code::ok);
    RTTI_CHECK_EQ(count, long long(3));
    delete scratch;
}

RTTI_TEST_CASE(for_each_map_keys) {
    Mutation o;
    o.m["x"] = 1;
    o.m["y"] = 2;
    std::string keys;
    box* scratch = new box;
    const auto ec = rtti::for_each_element(
        o, "m", scratch,
        [&keys](std::size_t, rtti::string_view key, const rtti::value_io*) -> bool {
            keys += std::string(key.data(), key.size());
            return true;
        });
    RTTI_CHECK_EQ(ec, rtti::error_code::ok);
    RTTI_CHECK_EQ(keys.size(), size_t(2));
    delete scratch;
}

RTTI_TEST_CASE(for_each_unknown_and_non_container) {
    Mutation o;
    box* scratch = new box;
    const auto ec = rtti::for_each_element(
        o, "nope", scratch,
        [](std::size_t, rtti::string_view, const rtti::value_io*) -> bool { return true; });
    RTTI_CHECK_EQ(ec, rtti::error_code::no_such_field);

    const auto ec2 = rtti::for_each_element(
        o, "plain", scratch,
        [](std::size_t, rtti::string_view, const rtti::value_io*) -> bool { return true; });
    RTTI_CHECK_EQ(ec2, rtti::error_code::kind_mismatch);
    delete scratch;
}

RTTI_TEST_CASE(for_each_adapter_queue_drains_copy) {
    Mutation o;
    o.q.push(1);
    o.q.push(2);
    long long sum = 0;
    box* scratch = new box;
    const auto ec = rtti::for_each_element(
        o, "q", scratch,
        [&sum](std::size_t, rtti::string_view, const rtti::value_io* elem) -> bool {
            int64_t v = 0;
            elem->as_integer(v);
            sum += v;
            return true;
        });
    RTTI_CHECK_EQ(ec, rtti::error_code::ok);
    RTTI_CHECK_EQ(sum, long long(3));
    RTTI_CHECK_EQ(o.q.size(), size_t(2));
    delete scratch;
}

} // namespace rtti_test_lib