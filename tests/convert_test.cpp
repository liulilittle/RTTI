// Convert tests. Task 6 of the RTTI reflect-core plan.
// Covers value_io abstraction, scalar/string converters, and the four
// container families (vector/list/map/set) with allocator transparency.

#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include <rtti/memory.hpp>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "box.hpp"

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
    box* src = make_int(5);
    std::string out;
    auto e = rtti::from_value(src, out);
    RTTI_CHECK(e != rtti::error_code::ok);
    RTTI_CHECK(e == rtti::error_code::kind_mismatch);
    delete src;
}

} // namespace rtti_test_lib
