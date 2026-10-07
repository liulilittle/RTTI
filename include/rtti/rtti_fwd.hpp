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

// Attribute system forward declarations (defined in reflection.hpp).
class attr_arg;
struct attr;
struct field_attr_entry;
struct field_attr_list;
template <typename T, typename> struct attr_table;
template <typename T, typename> struct field_attr_table;

// Explicit template-argument registry (spec 20). Defined in reflection.hpp;
// RTTI_TEMPLATE_INFO specializations supply a type list and a count.
template <typename T, typename> struct template_info;

// Compile-time feature gates for the optional format adapters.
// The reflect core itself compiles with all five macros undefined.
#if !defined(RTTI_ENABLE_JSON)
#define RTTI_ENABLE_JSON 1
#endif
#if !defined(RTTI_ENABLE_XML)
#define RTTI_ENABLE_XML 1
#endif
#if !defined(RTTI_ENABLE_INI)
#define RTTI_ENABLE_INI 1
#endif
#if !defined(RTTI_ENABLE_TOML)
#define RTTI_ENABLE_TOML 1
#endif
#if !defined(RTTI_ENABLE_PROTO)
#define RTTI_ENABLE_PROTO 1
#endif

} // namespace rtti
