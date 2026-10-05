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
    scan_test.cpp
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

add_executable(rtti_tests ${RTTI_TEST_SOURCES})
target_link_libraries(rtti_tests PRIVATE rtti)
target_compile_features(rtti_tests PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(rtti_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(rtti_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME rtti_tests COMMAND rtti_tests)
