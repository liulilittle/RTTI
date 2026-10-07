set(RTTI_TEST_SOURCES
    test_main.cpp
    reflection_smoke.cpp
    error_test.cpp
    memory_test.cpp
    reflection_table_test.cpp
    macro_test.cpp
    convert_test.cpp
    recursion_test.cpp
    runtime_api_test.cpp
    review_focus_test.cpp
    strip_matrix_test.cpp
    adaptor_convert_test.cpp
    runtime_mutation_test.cpp
    type_desc_test.cpp
    reflection_iter_test.cpp
    scan_test.cpp
    type_support_test.cpp
    custom_alloc_test.cpp
    float_format_test.cpp
    naming_config_test.cpp
    static_array_test.cpp
    base_convert_test.cpp
    string_convert_test.cpp
    scalar_convert_test.cpp
    attr_test.cpp
    instance_test.cpp
    type_judge_test.cpp
    template_reflect_test.cpp
    method_reflect_test.cpp
    ioc_test.cpp
    fuzz_test.cpp
)

# Format adapter tests compile only when their feature gate is on (default ON).
if(RTTI_ENABLE_JSON)
  list(APPEND RTTI_TEST_SOURCES json_test.cpp)
endif()
if(RTTI_ENABLE_XML)
  list(APPEND RTTI_TEST_SOURCES xml_test.cpp)
endif()
if(RTTI_ENABLE_INI)
  list(APPEND RTTI_TEST_SOURCES ini_test.cpp)
endif()
if(RTTI_ENABLE_TOML)
  list(APPEND RTTI_TEST_SOURCES toml_test.cpp)
endif()
# date_time_test exercises the converter bridge across all four format
# adapters, so it is compiled only when every format gate is on (a stripped
# build would otherwise hit the format headers' #error).
if(RTTI_ENABLE_JSON AND RTTI_ENABLE_XML AND RTTI_ENABLE_INI AND RTTI_ENABLE_TOML)
  list(APPEND RTTI_TEST_SOURCES date_time_test.cpp)
endif()
# node_locate_test covers the unified locate/node_as/to_node node-level API
# across all five format adapters (including proto schema-less docs); its own
# #if guard matches this condition exactly.
if(RTTI_ENABLE_JSON AND RTTI_ENABLE_XML AND RTTI_ENABLE_INI AND RTTI_ENABLE_TOML AND RTTI_ENABLE_PROTO)
  list(APPEND RTTI_TEST_SOURCES node_locate_test.cpp)
endif()
if(RTTI_ENABLE_PROTO)
  list(APPEND RTTI_TEST_SOURCES proto_test.cpp)
  list(APPEND RTTI_TEST_SOURCES proto_schema_test.cpp)
endif()

add_executable(rtti_tests ${RTTI_TEST_SOURCES})
target_link_libraries(rtti_tests PRIVATE rtti)
target_compile_features(rtti_tests PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(rtti_tests PRIVATE /W4 /permissive-)
  # The deep-nesting DoS tests decode ~600-level chains; the typed and
  # runtime tree paths recurse one C++ frame per message level, so the
  # default 1 MiB test-process stack overflows in Debug. Reserve 16 MiB.
  target_link_options(rtti_tests PRIVATE /STACK:16777216)
else()
  target_compile_options(rtti_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME rtti_tests COMMAND rtti_tests)
