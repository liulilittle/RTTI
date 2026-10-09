#pragma once
// Portable RAII steady_clock timer for microbenchmarks.
// No third-party dependencies; C++17.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <thread>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif

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

// ---------------------------------------------------------------------------
// Environment fingerprint. Every benchmark report must be read against the
// toolchain / machine that produced it; helper functions below print a one-line
// context tag so numbers are never presented without their environment.
// ---------------------------------------------------------------------------

// Human-readable compiler family, e.g. "MSVC" / "GCC" / "Clang".
inline const char* compiler_name() noexcept {
#if defined(_MSC_VER)
    return "MSVC";
#elif defined(__clang__)
    return "Clang";
#elif defined(__GNUC__)
    return "GCC";
#else
    return "unknown";
#endif
}

// e.g. "19.44.35229" for MSVC (cl reports the same triple).
inline std::string compiler_version() {
#if defined(_MSC_VER)
    const int major = _MSC_VER / 100;
    const int minor = _MSC_VER % 100;
    const int build = _MSC_FULL_VER % 100000;
    return std::to_string(major) + "." + std::to_string(minor) + "." +
           std::to_string(build);
#elif defined(__clang__)
    return std::string(__clang_version__);
#elif defined(__GNUC__)
    return std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
           std::to_string(__GNUC_PATCHLEVEL__);
#else
    return "unknown";
#endif
}

// Build configuration as seen through the standard sanity macros.
inline const char* build_config() noexcept {
#if defined(_DEBUG)
    return "Debug (_DEBUG defined)";
#elif defined(NDEBUG)
    return "Release (NDEBUG defined)";
#else
    return "unspecified";
#endif
}

// CPU brand string via a preprocessor-selected path; returns "unknown" when no
// portable source of truth is available.
inline std::string cpu_model() {
#if defined(_WIN32) && defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 0x80000000);
    const int max_ext = info[0];
    if (max_ext >= 0x80000004) {
        char brand[49] = {0};
        for (int leaf = 0x80000002; leaf <= 0x80000004; ++leaf) {
            __cpuid(info, leaf);
            std::memcpy(brand + static_cast<std::size_t>(leaf - 0x80000002) * 16,
                        info, 16);
        }
        // Trim trailing spaces left by the CPUID brand string.
        std::string s(brand);
        while (!s.empty() && s.back() == ' ') {
            s.pop_back();
        }
        if (!s.empty()) {
            return s;
        }
    }
#elif defined(__linux__)
    // /proc/cpuinfo "model name" is the canonical source on Linux.
    std::FILE* fp = std::fopen("/proc/cpuinfo", "r");
    if (fp != nullptr) {
        char line[512];
        while (std::fgets(line, sizeof(line), fp) != nullptr) {
            std::string s(line);
            if (s.compare(0, 11, "model name\t") == 0) {
                std::fclose(fp);
                std::size_t pos = s.find(':');
                if (pos != std::string::npos) {
                    std::string name = s.substr(pos + 1);
                    while (!name.empty() && name.front() == ' ') {
                        name.erase(name.begin());
                    }
                    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
                        name.pop_back();
                    }
                    return name;
                }
                break;
            }
        }
        std::fclose(fp);
    }
#endif
    return "unknown";
}

// Logical processor count (hardware threads).
inline unsigned logical_processors() noexcept {
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : n;
}

// Prints the environment header for one benchmark run.
inline void print_environment(const char* benchmark, int samples) {
    std::printf("benchmark   : %s\n", benchmark);
    std::printf("compiler    : %s %s\n", compiler_name(), compiler_version().c_str());
    std::printf("config      : %s\n", build_config());
    std::printf("cpu         : %s\n", cpu_model().c_str());
    std::printf("logical cpus: %u\n", logical_processors());
    std::printf("method      : steady_clock wall time; %d timed samples, median ns/op\n", samples);
}

} // namespace rtti_bench
