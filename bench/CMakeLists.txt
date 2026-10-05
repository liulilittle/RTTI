# Reflect-core microbenchmark executable. Not part of the test suite;
# run manually to measure ops/s of the field/reflection hot paths.

add_executable(rtti_bench bench_reflect.cpp)
target_link_libraries(rtti_bench PRIVATE rtti)
target_include_directories(rtti_bench PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_features(rtti_bench PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(rtti_bench PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(rtti_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()