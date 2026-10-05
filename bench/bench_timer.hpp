#pragma once
// Portable RAII steady_clock timer for microbenchmarks.
// No third-party dependencies; C++17.

#include <chrono>

namespace rtti_bench {

class timer {
public:
    timer() : start_(std::chrono::steady_clock::now()) {}

    // Elapsed seconds since construction (or the last reset()).
    double elapsed_sec() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

    // Elapsed nanoseconds since construction (or the last reset()).
    long long elapsed_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - start_)
            .count();
    }

    void reset() { start_ = std::chrono::steady_clock::now(); }

private:
    std::chrono::steady_clock::time_point start_;
};

} // namespace rtti_bench
