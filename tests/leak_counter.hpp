// Shared leak-accounting helpers for the test suite.
//
// The global ::operator new/delete overrides are defined in exactly one TU
// (string_convert_test.cpp) because a second definition would violate the
// one-definition rule. Anything that only READS the counters can live here:
// a snapshot object turns "this section must not leak" into an assertion the
// same way the original string_convert test did, but from any test TU.
// English comments only.

#pragma once

namespace rtti_test_lib {

inline long long& g_rttileak_allocs() {
    static long long n = 0;
    return n;
}

inline long long& g_rttileak_live() {
    static long long n = 0;
    return n;
}

// RAII live-balance snapshot. Construct before a section that must not leak,
// then require live() == 0 afterwards (callers compare against the snapshot
// captured here). Reusable by any test that includes this header.
class leak_snapshot {
public:
    leak_snapshot() noexcept : base_(g_rttileak_live()) {}
    long long live() const noexcept { return g_rttileak_live() - base_; }
    long long allocated() const noexcept { return g_rttileak_allocs(); }

private:
    long long base_;
};

} // namespace rtti_test_lib