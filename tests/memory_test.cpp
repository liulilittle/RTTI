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

// Cross-block allocation must keep the requested alignment promise. The first
// block (64 KiB) is filled so the cursor sits at an offset that is 7 (mod 16):
// the next 16-aligned request computes pad == 9 and must cross into a fresh
// block, where the pad has to be re-derived from the new block's start.
RTTI_TEST_CASE(arena_cross_block_keeps_requested_alignment) {
    // 16-byte-aligned request crossing the 64 KiB boundary: the pad derived
    // from the previous block's cursor must be recomputed for the fresh block.
    rtti::memory_arena a;
    RTTI_CHECK(a.alloc(65520, 16) != nullptr);  // cursor 65520, 16 bytes left
    RTTI_CHECK(a.alloc(8, 16) != nullptr);       // cursor 65528 (8 mod 16), 8 left
    RTTI_CHECK(a.total_bytes() < 2 * 64 * 1024); // still a single block
    void* o = a.alloc(16, 16);
    RTTI_CHECK(o != nullptr);
    RTTI_CHECK(a.total_bytes() >= 2 * 64 * 1024); // a fresh block was taken
    RTTI_CHECK((reinterpret_cast<uintptr_t>(o) % 16) == 0);

    // The same crossing for a plain double (8-byte alignment here).
    rtti::memory_arena b;
    RTTI_CHECK(b.alloc(65520, 16) != nullptr);  // cursor 65520
    RTTI_CHECK(b.alloc(9, 16) != nullptr);       // cursor 65529 (1 mod 8), 7 left
    double* d = static_cast<double*>(b.alloc(sizeof(double), alignof(double)));
    RTTI_CHECK(d != nullptr);
    RTTI_CHECK((reinterpret_cast<uintptr_t>(d) % alignof(double)) == 0);
}
