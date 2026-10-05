#pragma once
// Minimal self-contained test harness. No third-party framework.
// English comments only.

#include <cstdio>
#include <cstdlib>
#include <string>

namespace rtti_test {

inline int& failures() {
    static int f = 0;
    return f;
}

inline int& assertions() {
    static int a = 0;
    return a;
}

inline void summary() {
    std::printf("\n%d assertions, %d failures\n", assertions(), failures());
    std::fflush(stdout);
    std::exit(failures() == 0 ? 0 : 1);
}

} // namespace rtti_test

#define RTTI_TEST_CASE(name)                                   \
    static void name();                                        \
    struct name##_register {                                   \
        name##_register() {                                    \
            std::fprintf(stderr, "[TEST] %s\n", #name);        \
            name();                                            \
        }                                                      \
    };                                                         \
    static name##_register name##_instance;                    \
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

#define RTTI_TEST_MAIN() int main() { rtti_test::summary(); return 0; }
