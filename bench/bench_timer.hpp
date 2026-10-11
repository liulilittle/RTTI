#pragma once
// Portable RAII steady_clock timer for microbenchmarks.
// No third-party dependencies; C++17.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(__linux__)
#include <unistd.h>
#endif

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

// Current CPU core frequency in GHz, or 0 when unavailable. Windows: the
// TSC is calibrated over a QPC-paced busy window (rdtsc delta divided by
// elapsed wall seconds), so the QPC counter only paces the window and never
// feeds the result; Linux: reported frequency from /proc/cpuinfo. Thermal
// state is not directly queryable here, so cross-date comparisons should
// treat this as an approximate ceiling, not a live measurement.
inline double cpu_frequency_ghz() {
#if defined(_WIN32) && defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    LARGE_INTEGER freq;
    LARGE_INTEGER qpc0;
    LARGE_INTEGER qpc1;
    if (!::QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
        return 0.0;
    }
    ::QueryPerformanceCounter(&qpc0);
    std::chrono::steady_clock::time_point s = std::chrono::steady_clock::now();
    // Busy-wait roughly 10 ms to amortize the counter read overhead; the TSC
    // is sampled immediately before and after this window so the cycle count
    // covers exactly the wall time measured below.
    long long ticks = 0;
    const long long target = static_cast<long long>(freq.QuadPart) / 100;
    const unsigned long long rdtsc0 = __rdtsc();
    while (ticks < target) {
        ::QueryPerformanceCounter(&qpc1);
        ticks = qpc1.QuadPart - qpc0.QuadPart;
    }
    const unsigned long long rdtsc1 = __rdtsc();
    std::chrono::steady_clock::time_point e = std::chrono::steady_clock::now();
    const double wall_s =
        std::chrono::duration<double>(e - s).count();
    if (wall_s <= 0.0) {
        return 0.0;
    }
    const double cycles = static_cast<double>(rdtsc1 - rdtsc0);
    return cycles / wall_s / 1e9;
#elif defined(__linux__)
    // /proc/cpuinfo "cpu MHz" reflects the current per-core frequency on most
    // x86 kernels; read the first occurrence as an approximate sample.
    std::FILE* fp = std::fopen("/proc/cpuinfo", "r");
    if (fp != nullptr) {
        char line[256];
        while (std::fgets(line, sizeof(line), fp) != nullptr) {
            std::string s(line);
            if (s.compare(0, 7, "cpu MHz") == 0) {
                std::size_t pos = s.find(':');
                if (pos != std::string::npos) {
                    std::string v = s.substr(pos + 1);
                    while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) {
                        v.pop_back();
                    }
                    const double mhz = std::atof(v.c_str());
                    std::fclose(fp);
                    return mhz > 0.0 ? mhz / 1000.0 : 0.0;
                }
                break;
            }
        }
        std::fclose(fp);
    }
    return 0.0;
#else
    return 0.0;
#endif
}

// Approximate system CPU utilisation (0..1) at call time, or -1 when the
// platform has no cheap way to sample it. Windows uses GetSystemTimes kernel +
// idle deltas over a short busy/sleep window; Linux uses the 1-minute load
// average renormalized by the core count (a coarse but portable proxy).
inline double system_load() {
#if defined(_WIN32)
    FILETIME idle0, kern0, user0;
    if (!::GetSystemTimes(&idle0, &kern0, &user0)) {
        return -1.0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    FILETIME idle1, kern1, user1;
    if (!::GetSystemTimes(&idle1, &kern1, &user1)) {
        return -1.0;
    }
    auto to_u64 = [](const FILETIME& ft) {
        ULARGE_INTEGER v;
        v.LowPart = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return v.QuadPart;
    };
    const unsigned long long busyt0 = to_u64(kern0) + to_u64(user0) - to_u64(idle0);
    const unsigned long long busyt1 = to_u64(kern1) + to_u64(user1) - to_u64(idle1);
    const unsigned long long tott0 = to_u64(kern0) + to_u64(user0);
    const unsigned long long tott1 = to_u64(kern1) + to_u64(user1);
    const unsigned long long db = busyt1 > busyt0 ? busyt1 - busyt0 : 0;
    const unsigned long long dt = tott1 > tott0 ? tott1 - tott0 : 0;
    if (dt == 0) {
        return -1.0;
    }
    double load = static_cast<double>(db) / static_cast<double>(dt);
    if (load < 0.0) {
        load = 0.0;
    }
    if (load > 1.0) {
        load = 1.0;
    }
    return load;
#elif defined(__linux__)
    std::FILE* fp = std::fopen("/proc/loadavg", "r");
    if (fp != nullptr) {
        double one_min = 0.0;
        if (std::fscanf(fp, "%lf", &one_min) == 1) {
            std::fclose(fp);
            if (one_min < 0.0) {
                one_min = 0.0;
            }
            const unsigned n = logical_processors();
            const double load = one_min / static_cast<double>(n == 0 ? 1 : n);
            return load > 1.0 ? 1.0 : load;
        }
        std::fclose(fp);
    }
    return -1.0;
#else
    return -1.0;
#endif
}

// Prints the environment header for one benchmark run.
inline void print_environment(const char* benchmark, int samples) {
    std::printf("benchmark   : %s\n", benchmark);
    std::printf("compiler    : %s %s\n", compiler_name(), compiler_version().c_str());
    std::printf("config      : %s\n", build_config());
    std::printf("cpu         : %s\n", cpu_model().c_str());
    std::printf("logical cpus: %u\n", logical_processors());
    const double ghz = cpu_frequency_ghz();
    if (ghz > 0.0) {
        std::printf("cpu freq    : %.3f GHz (rdtsc calibrated; ~ceiling, not live)\n", ghz);
    } else {
        std::printf("cpu freq    : n/a\n");
    }
    const double load = system_load();
    if (load >= 0.0) {
        std::printf("sys load    : %.2f (of 1.0 busy; sampled)\n", load);
    } else {
        std::printf("sys load    : n/a\n");
    }
    // Omit the method line when the caller has no timed samples to report
    // (e.g. a parse benchmark that reports iters, not samples).
    if (samples > 0) {
        std::printf("method      : steady_clock wall time; %d timed samples, median ns/op\n",
                    samples);
    }
}

} // namespace rtti_bench
