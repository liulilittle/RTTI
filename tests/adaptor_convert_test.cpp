// Container-adaptor conversion tests. Extension of the RTTI reflect-core
// plan: queue/stack/priority_queue participate in from_value/to_value with
// order-semantic rebuild (FIFO, LIFO, heap order).

#include "test_harness.hpp"
#include <rtti/convert.hpp>
#include "box.hpp"
#include <queue>
#include <stack>
#include <string>
#include <vector>

namespace rtti_test_lib {

RTTI_TEST_CASE(queue_roundtrip_fifo) {
    std::queue<int> src;
    src.push(1);
    src.push(2);
    src.push(3);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, src), rtti::error_code::ok);
    RTTI_CHECK(dst->type() == rtti::value_io::kind::array);
    RTTI_CHECK_EQ(dst->size(), size_t(3));

    std::queue<int> back;
    RTTI_CHECK_EQ(rtti::from_value(dst, back), rtti::error_code::ok);
    RTTI_CHECK_EQ(back.size(), size_t(3));
    RTTI_CHECK_EQ(back.front(), 1);
    back.pop();
    RTTI_CHECK_EQ(back.front(), 2);
    back.pop();
    RTTI_CHECK_EQ(back.front(), 3);
    delete dst;
}

RTTI_TEST_CASE(queue_to_emits_front_first) {
    std::queue<std::string> q;
    q.push("a");
    q.push("b");
    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, q), rtti::error_code::ok);
    rtti::string_view first;
    RTTI_CHECK(dst->at(size_t(0))->as_string(first));
    RTTI_CHECK_EQ(std::string(first.data(), first.size()), std::string("a"));
    delete dst;
}

RTTI_TEST_CASE(stack_roundtrip_lifo) {
    std::stack<int> src;
    src.push(1);
    src.push(2);
    src.push(3);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, src), rtti::error_code::ok);
    RTTI_CHECK_EQ(dst->size(), size_t(3));

    std::stack<int> back;
    RTTI_CHECK_EQ(rtti::from_value(dst, back), rtti::error_code::ok);
    RTTI_CHECK_EQ(back.size(), size_t(3));
    RTTI_CHECK_EQ(back.top(), 3);
    back.pop();
    RTTI_CHECK_EQ(back.top(), 2);
    back.pop();
    RTTI_CHECK_EQ(back.top(), 1);
    delete dst;
}

RTTI_TEST_CASE(stack_to_emits_top_first) {
    std::stack<int> s;
    s.push(10);
    s.push(20);
    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, s), rtti::error_code::ok);
    int64_t v = 0;
    RTTI_CHECK(dst->at(size_t(0))->as_integer(v));
    RTTI_CHECK_EQ(v, int64_t(20));
    delete dst;
}

RTTI_TEST_CASE(priority_queue_roundtrip) {
    std::priority_queue<int> src;
    src.push(5);
    src.push(1);
    src.push(9);

    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, src), rtti::error_code::ok);
    RTTI_CHECK_EQ(dst->size(), size_t(3));

    std::priority_queue<int> back;
    RTTI_CHECK_EQ(rtti::from_value(dst, back), rtti::error_code::ok);
    RTTI_CHECK_EQ(back.size(), size_t(3));
    RTTI_CHECK_EQ(back.top(), 9);
    back.pop();
    RTTI_CHECK_EQ(back.top(), 5);
    back.pop();
    RTTI_CHECK_EQ(back.top(), 1);
    delete dst;
}

RTTI_TEST_CASE(priority_queue_emits_max_first) {
    std::priority_queue<int> pq;
    pq.push(4);
    pq.push(7);
    pq.push(2);
    box* dst = new box;
    RTTI_CHECK_EQ(rtti::to_value(dst, pq), rtti::error_code::ok);
    int64_t v = 0;
    RTTI_CHECK(dst->at(size_t(0))->as_integer(v));
    RTTI_CHECK_EQ(v, int64_t(7));
    delete dst;
}

} // namespace rtti_test_lib
