// Memory-arena tests. Task 3 of the RTTI reflect-core plan.
// Covers string_view alias and the MemoryArena allocator.

#include "test_harness.hpp"
#include <rtti/memory.hpp>
#include <string>

RTTI_TEST_CASE(string_view_alias_exists) {
    rtti::string_view sv = "hello";
    RTTI_CHECK_EQ(sv.size(), size_t(5));
    RTTI_CHECK_EQ(std::string(sv), std::string("hello"));
}

RTTI_TEST_CASE(arena_alloc_and_dup) {
    rtti::memory_arena a;
    int* p = a.alloc_obj<int>();
    *p = 7;
    RTTI_CHECK_EQ(*p, 7);

    const char* s = a.dup("hello", 5);
    RTTI_CHECK_EQ(std::string(s), std::string("hello"));
}

RTTI_TEST_CASE(arena_alignment_ok) {
    rtti::memory_arena a;
    double* d = a.alloc_obj<double>();
    void* raw = a.alloc(16, 16);
    RTTI_CHECK((reinterpret_cast<uintptr_t>(d) % alignof(double)) == 0);
    RTTI_CHECK((reinterpret_cast<uintptr_t>(raw) % 16) == 0);
}

RTTI_TEST_CASE(arena_grows_on_demand) {
    rtti::memory_arena a;
    for (int i = 0; i < 100000; ++i) {
        const char* c = a.dup("x", 1);
        (void)c;
    }
    // Multiple 64KB blocks must have been allocated.
    RTTI_CHECK(a.total_bytes() > 64 * 1024);
}

RTTI_TEST_CASE(arena_reset_frees_and_is_reusable) {
    rtti::memory_arena a;
    const char* c1 = a.dup("abc", 3);
    (void)c1;
    RTTI_CHECK(a.total_bytes() > 0);
    a.reset();
    RTTI_CHECK_EQ(a.total_bytes(), size_t(0));
    int* q = a.alloc_obj<int>();
    RTTI_CHECK(q != nullptr);
}

RTTI_TEST_CASE(arena_move_semantics) {
    rtti::memory_arena a;
    a.dup("keep", 4);
    rtti::memory_arena b = std::move(a);
    const char* s = b.dup("after", 5);
    RTTI_CHECK_EQ(std::string(s), std::string("after"));
}
