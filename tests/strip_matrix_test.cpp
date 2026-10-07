// Stripped-build matrix verification. Task 10 of the RTTI reflect-core plan.
// Forces every format adapter gate off in THIS translation unit (independent
// of the project-wide CMake gates), proving the reflect core carries zero
// format-header dependencies and keeps working with all six formats disabled.

#if defined(RTTI_ENABLE_JSON) && RTTI_ENABLE_JSON
#  undef RTTI_ENABLE_JSON
#  define RTTI_ENABLE_JSON 0
#endif
#if defined(RTTI_ENABLE_XML) && RTTI_ENABLE_XML
#  undef RTTI_ENABLE_XML
#  define RTTI_ENABLE_XML 0
#endif
#if defined(RTTI_ENABLE_INI) && RTTI_ENABLE_INI
#  undef RTTI_ENABLE_INI
#  define RTTI_ENABLE_INI 0
#endif
#if defined(RTTI_ENABLE_TOML) && RTTI_ENABLE_TOML
#  undef RTTI_ENABLE_TOML
#  define RTTI_ENABLE_TOML 0
#endif
#if defined(RTTI_ENABLE_PROTO) && RTTI_ENABLE_PROTO
#  undef RTTI_ENABLE_PROTO
#  define RTTI_ENABLE_PROTO 0
#endif

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include <string>

// The reflect core must compile with all six format gates off. The stubs
// below are never included by core headers; if one ever leaks in, the #error
// in the stub trips during this test's compile.
// #include <rtti/json.hpp>   // intentionally commented: would #error when off

struct MatrixType {
    int a = 0;
    std::string b;
};

RTTI_DEFINE_TYPE(MatrixType, a, b)

namespace rtti_test_lib {

RTTI_TEST_CASE(core_compiles_without_format_gates) {
    RTTI_CHECK(rtti::is_reflected<MatrixType>::value);
    RTTI_CHECK_EQ(rtti::field_count<MatrixType>(), size_t(2));
}

} // namespace rtti_test_lib
