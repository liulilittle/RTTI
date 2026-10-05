// Reflection-core smoke test. Task 1 of the RTTI reflect-core plan.
// Verifies the namespace and the test harness wiring.

#include "test_harness.hpp"
#include <rtti/rtti_fwd.hpp>

RTTI_TEST_CASE(namespace_version_tag) {
    rtti::version_tag tag;
    RTTI_CHECK(tag.major >= 0);
    RTTI_CHECK_EQ(tag.minor, 1);
}

namespace rtti_test_lib {

inline bool gate_valid(int v) { return v == 0 || v == 1; }

} // namespace rtti_test_lib

RTTI_TEST_CASE(feature_gates_defined) {
    RTTI_CHECK(rtti_test_lib::gate_valid(RTTI_ENABLE_JSON));
    RTTI_CHECK(rtti_test_lib::gate_valid(RTTI_ENABLE_XML));
    RTTI_CHECK(rtti_test_lib::gate_valid(RTTI_ENABLE_INI));
}
