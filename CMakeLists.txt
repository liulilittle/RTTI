cmake_minimum_required(VERSION 3.15)
project(RTTI VERSION 0.1.0 LANGUAGES CXX)

# Single-source version: project() version feeds the generated public header
# include/rtti/version.hpp (industrialization plan Task E1).
configure_file(cmake/version.hpp.in ${CMAKE_CURRENT_SOURCE_DIR}/include/rtti/version.hpp @ONLY)

# --- Optional format adapter gates (default ON) -------------------------------
option(RTTI_ENABLE_JSON "Enable JSON format adapter" ON)
option(RTTI_ENABLE_XML  "Enable XML format adapter"  ON)
option(RTTI_ENABLE_INI  "Enable INI format adapter"  ON)
option(RTTI_ENABLE_TOML "Enable TOML format adapter" ON)
option(RTTI_ENABLE_PROTO "Enable protobuf format adapter" ON)
option(RTTI_ENABLE_FLAT  "Enable flatbuffers format adapter" ON)

# Interprocedural optimization (whole-program /GL + /LTCG on MSVC). Enabled for
# Release only; CMake then links with /LTCG and disables incremental linking,
# avoiding LNK4075. Default ON so the strongest optimization form is exercised.
option(RTTI_ENABLE_LTO "Enable link-time optimization in Release builds" ON)

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
if(RTTI_ENABLE_XML)
  target_sources(rtti PRIVATE src/xml.cpp)
endif()
if(RTTI_ENABLE_PROTO)
  target_sources(rtti PRIVATE src/proto_schema.cpp)
endif()
# BUILD_INTERFACE keeps the source-tree include dir for in-tree consumers while
# INSTALL_INTERFACE makes the exported rtti::rtti target resolve headers from
# the installed package (find_package must not leak F:/json_xml paths).
target_include_directories(rtti PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(rtti PUBLIC cxx_std_17)

target_compile_definitions(rtti
    PUBLIC
    RTTI_ENABLE_JSON=$<BOOL:${RTTI_ENABLE_JSON}>
    RTTI_ENABLE_XML=$<BOOL:${RTTI_ENABLE_XML}>
    RTTI_ENABLE_INI=$<BOOL:${RTTI_ENABLE_INI}>
    RTTI_ENABLE_TOML=$<BOOL:${RTTI_ENABLE_TOML}>
    RTTI_ENABLE_PROTO=$<BOOL:${RTTI_ENABLE_PROTO}>
    RTTI_ENABLE_FLAT=$<BOOL:${RTTI_ENABLE_FLAT}>)

if(RTTI_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT RTTI_IPO_OK OUTPUT RTTI_IPO_MSG)
  if(RTTI_IPO_OK)
    set_property(TARGET rtti PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set_property(TARGET rtti PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
  else()
    message(STATUS "RTTI: IPO not supported (${RTTI_IPO_MSG}); building without LTO")
  endif()
endif()

if(MSVC)
  # /Zc:preprocessor enables the conformant preprocessor, required for the
  # macro-family argument counting (RTTI_NARG) to expand correctly.
  target_compile_options(rtti PUBLIC /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(rtti PUBLIC -Wall -Wextra -Wpedantic)
endif()

# --- Install rules and find_package package (industrialization plan Task E3) --
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS rtti EXPORT rttiTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/rtti DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT rttiTargets NAMESPACE rtti:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rtti)

configure_package_config_file(
    cmake/rtti-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/rtti-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rtti)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/rtti-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/rtti-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/rtti-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rtti)

# --- Tests ---------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- Benchmark -----------------------------------------------------------------
add_subdirectory(bench)

# Enable IPO on every executable created above so /LTCG links without the
# incremental-link conflict (LNK4075) across the whole Release/LTCG build.
if(RTTI_ENABLE_LTO AND RTTI_IPO_OK)
  foreach(_t rtti_tests rtti_proto_extension_tests rtti_flat_schema_tests rtti_bench)
    if(TARGET ${_t})
      set_property(TARGET ${_t} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      set_property(TARGET ${_t} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
  endforeach()
endif()
