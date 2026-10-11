// Shared leak-accounting helpers for the test suite.
//
// The global ::operator new/delete overrides are defined in exactly one TU
// (string_convert_test.cpp) because a second definition would violate the
// one-definition rule. Anything that only READS the counters can live here:
// a snapshot object turns "this section must not leak" into an assertion the
// same way the original string_convert test did, but from any test TU.
// The counters are relaxed atomics: the concurrency cases allocate from
// worker threads while readers may sample the balance, and the assertions
// only need atomicity, not ordering. English comments only.

#pragma once

#include <atomic>

namespace rtti_test_lib {

inline std::atomic<long long>& g_rttileak_allocs() {
    static std::atomic<long long> n{0};
    return n;
}

inline std::atomic<long long>& g_rttileak_live() {
    static std::atomic<long long> n{0};
    return n;
}

// RAII live-balance snapshot. Construct before a section that must not leak,
// then require live() == 0 afterwards (callers compare against the snapshot
// captured here). Reusable by any test that includes this header.
class leak_snapshot {
public:
    leak_snapshot() noexcept : base_(g_rttileak_live().load(std::memory_order_relaxed)) {}
    long long live() const noexcept {
        return g_rttileak_live().load(std::memory_order_relaxed) - base_;
    }
    long long allocated() const noexcept {
        // Absolute cumulative allocation count since program start (not a
        // delta against this snapshot); for leak checks compare live()
        // against the snapshot instead.
        return g_rttileak_allocs().load(std::memory_order_relaxed);
    }

private:
    long long base_;
};

} // namespace rtti_test_lib
