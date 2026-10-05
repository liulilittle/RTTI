#pragma once
// RTTI forward declarations.
// This header carries no implementation and no format adapter includes,
// so it is safe to include from any translation unit.

namespace rtti {

// Library version information.
struct version_tag {
    int major = 0;
    int minor = 1;
    int patch = 0;
};

// Compile-time feature gates for the optional format adapters.
// The reflect core itself compiles with all three macros undefined.
#if !defined(RTTI_ENABLE_JSON)
#define RTTI_ENABLE_JSON 1
#endif
#if !defined(RTTI_ENABLE_XML)
#define RTTI_ENABLE_XML 1
#endif
#if !defined(RTTI_ENABLE_INI)
#define RTTI_ENABLE_INI 1
#endif

} // namespace rtti
