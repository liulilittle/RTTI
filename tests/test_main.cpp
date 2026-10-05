// Single translation unit that provides main() for the test executable.
// Every RTTI_TEST_CASE registers a static instance that runs during static
// initialization; main() only prints the summary and sets the exit code.

#include "test_harness.hpp"
#include <cstdio>

int main() {
    // Unbuffered stdout so a crash mid-test still shows the last [TEST] line.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    rtti_test::summary();
    return 0;
}