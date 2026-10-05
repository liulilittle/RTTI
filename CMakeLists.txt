cmake_minimum_required(VERSION 3.15)
project(RTTI VERSION 0.1.0 LANGUAGES CXX)

# --- Optional format adapter gates (default ON) -------------------------------
option(RTTI_ENABLE_JSON "Enable JSON format adapter" ON)
option(RTTI_ENABLE_XML  "Enable XML format adapter"  ON)
option(RTTI_ENABLE_INI  "Enable INI format adapter"  ON)

# --- Reflect-core static library ----------------------------------------------
add_library(rtti STATIC src/common.cpp src/reflect.cpp src/error.cpp src/memory.cpp)
target_include_directories(rtti PUBLIC include)
target_compile_features(rtti PUBLIC cxx_std_17)

target_compile_definitions(rtti
    PUBLIC
    RTTI_ENABLE_JSON=$<BOOL:${RTTI_ENABLE_JSON}>
    RTTI_ENABLE_XML=$<BOOL:${RTTI_ENABLE_XML}>
    RTTI_ENABLE_INI=$<BOOL:${RTTI_ENABLE_INI}>)

if(MSVC)
  # /Zc:preprocessor enables the conformant preprocessor, required for the
  # macro-family argument counting (RTTI_NARG) to expand correctly.
  target_compile_options(rtti PUBLIC /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(rtti PUBLIC -Wall -Wextra -Wpedantic)
endif()

# --- Tests ---------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- Benchmark -----------------------------------------------------------------
add_subdirectory(bench)
