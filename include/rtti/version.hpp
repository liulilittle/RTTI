#pragma once
#define RTTI_VERSION_MAJOR 0
#define RTTI_VERSION_MINOR 1
#define RTTI_VERSION_PATCH 0
#define RTTI_VERSION_STRING "0.1.0"
namespace rtti { inline const char* version() noexcept { return RTTI_VERSION_STRING; } }
