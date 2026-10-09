# Reflect-core microbenchmark executable. Not part of the test suite;
# run manually to measure ops/s of the field/reflection hot paths.

add_executable(rtti_bench bench_reflect.cpp)
target_link_libraries(rtti_bench PRIVATE rtti)
target_include_directories(rtti_bench PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_features(rtti_bench PRIVATE cxx_std_17)

# Microbenchmark for the traversal APIs (spec B1/B2/B3). Environment-tagged
# output (see bench_timer.hpp) so reported ns/op stays tied to its toolchain.
add_executable(rtti_bench_new bench_new_apis.cpp)
target_link_libraries(rtti_bench_new PRIVATE rtti)
target_include_directories(rtti_bench_new PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_features(rtti_bench_new PRIVATE cxx_std_17)

foreach(_t rtti_bench rtti_bench_new)
  if(MSVC)
    target_compile_options(${_t} PRIVATE /W4 /permissive- /Zc:preprocessor)
  else()
    target_compile_options(${_t} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

# The rtti library is compiled with /GL in Release+LTO builds; give this
# executable the same whole-program treatment so /LTCG links without the
# incremental-link conflict (LNK4075), mirroring the root CMakeLists policy.
if(RTTI_ENABLE_LTO AND RTTI_IPO_OK)
  foreach(_t rtti_bench rtti_bench_new)
    if(TARGET ${_t})
      set_property(TARGET ${_t} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      set_property(TARGET ${_t} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
  endforeach()
endif()