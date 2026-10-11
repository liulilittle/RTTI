// Single translation unit that provides main() for the test executable.
// Every RTTI_TEST_CASE registers its name and function pointer during static
// initialization; main() parses an optional --filter substring and runs the
// matching cases in registration order, then prints the summary.

#include "test_harness.hpp"

int main(int argc, char** argv) {
    return rtti_test::run_all(argc, argv);
}