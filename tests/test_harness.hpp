#pragma once
// Minimal self-contained test harness. No third-party framework.
// English comments only.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#if !defined(_MSC_VER)
#include <strings.h>
#endif

namespace rtti_test {

// Case-insensitive ASCII compare shared by the custom-allocator map tests
// (MSVC spells it _stricmp, POSIX strcasecmp).
inline int ci_stricmp(const char* a, const char* b) noexcept {
#if defined(_MSC_VER)
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

inline int& failures() {
    static int f = 0;
    return f;
}

inline int& assertions() {
    static int a = 0;
    return a;
}

// Prints the summary and returns the process exit code (0 = pass). Callers
// return this value from main(); it does not call std::exit, so main's tail
// is not unreachable under /GL whole-program optimization (avoids C4702).
inline int summary() noexcept {
    std::printf("\n%d assertions, %d failures\n", assertions(), failures());
    std::fflush(stdout);
    return failures() == 0 ? 0 : 1;
}

// Registry of test cases built at static initialization. Each RTTI_TEST_CASE
// appends one entry (name + function pointer); execution happens in run_all
// inside main, so a --filter substring can select a subset. The intrusive
// linked list avoids heap allocation in the registration path - the global
// operator new override in string_convert_test.cpp counts every allocation
// and tests must observe a clean baseline.
struct test_case {
    const char* name;
    void (*fn)();
    test_case* next;
};

inline test_case*& test_cases_head() {
    static test_case* head = nullptr;
    return head;
}

// Points at the link the next registration fills (the head's own slot while
// the list is empty, then the last entry's next).
inline test_case**& test_cases_tail() {
    static test_case** tail = &test_cases_head();
    return tail;
}

// Runs registered test cases in registration order. --filter=SUB or
// --filter SUB keeps only cases whose name contains SUB (case-sensitive
// substring). Returns the process exit code via summary().
inline int run_all(int argc, char** argv) {
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--filter=", 9) == 0) {
            filter = argv[i] + 9;
        } else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        }
    }
    // Unbuffered stdout so a crash mid-test still shows the last [TEST] line.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int run = 0;
    int skipped = 0;
    for (const test_case* c = test_cases_head(); c != nullptr; c = c->next) {
        if (filter != nullptr && std::strstr(c->name, filter) == nullptr) {
            ++skipped;
            continue;
        }
        std::fprintf(stderr, "[TEST] %s\n", c->name);
        std::fflush(stderr);
        c->fn();
        ++run;
    }
    if (filter != nullptr) {
        std::fprintf(stderr, "[FILTER] '%s': %d run, %d skipped\n",
                     filter, run, skipped);
    }
    return summary();
}

} // namespace rtti_test

#define RTTI_TEST_CASE(name)                                            \
    static void name();                                                 \
    struct name##_register {                                            \
        name##_register() {                                             \
            static rtti_test::test_case entry{#name, &name, nullptr};   \
            *rtti_test::test_cases_tail() = &entry;                     \
            rtti_test::test_cases_tail() = &entry.next;                 \
        }                                                               \
    };                                                                  \
    static name##_register name##_instance;                             \
    static void name()

#define RTTI_CHECK(expr)                                                     \
    do {                                                                     \
        ++rtti_test::assertions();                                           \
        if (!(expr)) {                                                       \
            ++rtti_test::failures();                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        }                                                                    \
    } while (0)

#define RTTI_CHECK_EQ(a, b)                                                   \
    do {                                                                      \
        ++rtti_test::assertions();                                            \
        auto va = (a);                                                        \
        auto vb = (b);                                                        \
        if (!(va == vb)) {                                                    \
            ++rtti_test::failures();                                          \
            std::fprintf(stderr, "FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); \
        }                                                                     \
    } while (0)

#define RTTI_CHECK_THROWS(expr)                                              \
    do {                                                                     \
        ++rtti_test::assertions();                                           \
        bool threw = false;                                                  \
        try {                                                                \
            (void)(expr);                                                    \
        } catch (...) {                                                      \
            threw = true;                                                    \
        }                                                                    \
        if (!threw) {                                                        \
            ++rtti_test::failures();                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s (expected throw)\n",        \
                         __FILE__, __LINE__, #expr);                          \
        }                                                                    \
    } while (0)

#define RTTI_TEST_MAIN() int main(int argc, char** argv) { return rtti_test::run_all(argc, argv); }
