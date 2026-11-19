cmake_minimum_required(VERSION 4.5)
project(Dandelion LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build directory, such as build/debug.")
endif()

option(DANDELION_BUILD_TESTS "Build development and toolchain tests"
       ${PROJECT_IS_TOP_LEVEL})
option(DANDELION_BUILD_TOOLING_TESTS "Build developer-tool regression tests"
       ${PROJECT_IS_TOP_LEVEL})
option(DANDELION_BUILD_BENCHMARKS "Build component benchmarks" OFF)
option(DANDELION_WARNINGS_AS_ERRORS "Treat project target warnings as errors" OFF)
set(DANDELION_DEPENDENCY_MODE "EXISTING" CACHE STRING
    "Dependency acquisition: EXISTING or FETCH")
set_property(CACHE DANDELION_DEPENDENCY_MODE PROPERTY STRINGS EXISTING FETCH)
if(NOT DANDELION_DEPENDENCY_MODE STREQUAL "EXISTING"
   AND NOT DANDELION_DEPENDENCY_MODE STREQUAL "FETCH")
  message(FATAL_ERROR "DANDELION_DEPENDENCY_MODE must be EXISTING or FETCH.")
endif()
set(DANDELION_SUBPROJECTS_DIR "${PROJECT_SOURCE_DIR}/subprojects" CACHE PATH
    "Directory for project-managed dependency sources")
set(_dandelion_sanitizers_default OFF)
if(PROJECT_IS_TOP_LEVEL AND CMAKE_BUILD_TYPE STREQUAL "Debug"
   AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(_dandelion_sanitizers_default ON)
endif()
option(DANDELION_ENABLE_SANITIZERS "Instrument project targets with ASan and UBSan"
       ${_dandelion_sanitizers_default})
unset(_dandelion_sanitizers_default)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compilation commands")

include(cmake/DandelionOptions.cmake)
include(cmake/DandelionGMP.cmake)
dandelion_resolve_gmp()
add_subdirectory(src/node)
add_subdirectory(src/frontend)

if(DANDELION_BUILD_BENCHMARKS)
  include(cmake/DandelionBenchmark.cmake)
  dandelion_resolve_benchmark()
endif()
if(DANDELION_BUILD_TESTS OR DANDELION_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks/node)
endif()

if(DANDELION_BUILD_TESTS)
  enable_testing()
  include(cmake/DandelionGoogleTest.cmake)
  dandelion_resolve_googletest()
  add_subdirectory(tests/benchmark)
  add_subdirectory(tests/frontend)
  add_subdirectory(tests/node)
  add_subdirectory(tests/toolchain)
  if(DANDELION_BUILD_TOOLING_TESTS)
    add_subdirectory(tests/tooling)
  endif()
endif()
