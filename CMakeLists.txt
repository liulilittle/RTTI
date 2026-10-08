cmake_minimum_required(VERSION 3.15)
project(RTTI VERSION 0.1.0 LANGUAGES CXX)

# --- Optional format adapter gates (default ON) -------------------------------
option(RTTI_ENABLE_JSON "Enable JSON format adapter" ON)
option(RTTI_ENABLE_XML  "Enable XML format adapter"  ON)
option(RTTI_ENABLE_INI  "Enable INI format adapter"  ON)
option(RTTI_ENABLE_TOML "Enable TOML format adapter" ON)
option(RTTI_ENABLE_PROTO "Enable protobuf format adapter" ON)
option(RTTI_ENABLE_FLAT  "Enable flatbuffers format adapter" ON)

# --- Reflect-core static library ----------------------------------------------
add_library(rtti STATIC
    src/base_convert.cpp
    src/common.cpp
    src/date_time.cpp
    src/reflect.cpp
    src/error.cpp
    src/memory.cpp
    src/string_ops.cpp
    src/text_node.cpp
    src/naming.cpp
    src/scan.cpp
    src/number_format.cpp
    src/format_reflected.cpp
    src/flat.cpp
    src/flat_schema.cpp)
# Format adapter sources compile only when their feature gate is on (default
# ON): their headers hard-#error when disabled, so a stripped build must not
# compile them.
if(RTTI_ENABLE_INI)
  target_sources(rtti PRIVATE src/ini.cpp)
endif()
if(RTTI_ENABLE_JSON)
  target_sources(rtti PRIVATE src/json.cpp)
endif()
if(RTTI_ENABLE_TOML)
  target_sources(rtti PRIVATE src/toml.cpp)
endif()
target_include_directories(rtti PUBLIC include)
target_compile_features(rtti PUBLIC cxx_std_17)

target_compile_definitions(rtti
    PUBLIC
    RTTI_ENABLE_JSON=$<BOOL:${RTTI_ENABLE_JSON}>
    RTTI_ENABLE_XML=$<BOOL:${RTTI_ENABLE_XML}>
    RTTI_ENABLE_INI=$<BOOL:${RTTI_ENABLE_INI}>
    RTTI_ENABLE_TOML=$<BOOL:${RTTI_ENABLE_TOML}>
    RTTI_ENABLE_PROTO=$<BOOL:${RTTI_ENABLE_PROTO}>
    RTTI_ENABLE_FLAT=$<BOOL:${RTTI_ENABLE_FLAT}>)

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
