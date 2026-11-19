if(NOT UNIX)
  message(STATUS "Benchmark integration fixture tests require POSIX and are unavailable")
  return()
endif()

foreach(name PROJECT_SOURCE_DIR CMAKE_COMMAND CMAKE_CXX_COMPILER)
  string(REPLACE "\\" "\\\\" escaped "${${name}}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  set(DANDELION_ESCAPED_${name} "${escaped}")
endforeach()
configure_file(benchmark_test_config.h.in benchmark_test_config.h @ONLY)

add_executable(dandelion_benchmark_integration_tests cmake_dependency_test.cc)
target_include_directories(dandelion_benchmark_integration_tests PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(dandelion_benchmark_integration_tests PRIVATE
  ${DANDELION_GTEST_MAIN_TARGET}
)
if(DANDELION_BUILD_BENCHMARKS)
  set(frontend_benchmark_path "$<TARGET_FILE:dandelion_frontend_benchmarks>")
  target_compile_definitions(dandelion_benchmark_integration_tests PRIVATE
    "DANDELION_BENCHMARK_EXECUTABLE=\"$<TARGET_FILE:dandelion_node_benchmarks>\""
    "DANDELION_FRONTEND_BENCHMARK_EXECUTABLE=\"${frontend_benchmark_path}\""
  )
  add_dependencies(dandelion_benchmark_integration_tests
    dandelion_node_benchmarks dandelion_frontend_benchmarks
  )
endif()
dandelion_configure_target(dandelion_benchmark_integration_tests
  SOURCES cmake_dependency_test.cc
)

include(GoogleTest)
gtest_discover_tests(dandelion_benchmark_integration_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS benchmark-infrastructure
)

add_executable(dandelion_node_workload_tests node_workload_test.cc)
target_link_libraries(dandelion_node_workload_tests PRIVATE
  dandelion_node_workload ${DANDELION_GTEST_MAIN_TARGET}
)
dandelion_configure_target(dandelion_node_workload_tests
  SOURCES node_workload_test.cc
)
gtest_discover_tests(dandelion_node_workload_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS benchmark-infrastructure
)

add_executable(dandelion_frontend_workload_tests frontend_workload_test.cc)
target_include_directories(dandelion_frontend_workload_tests PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(dandelion_frontend_workload_tests PRIVATE
  dandelion_frontend_workload ${DANDELION_GTEST_MAIN_TARGET}
)
if(DANDELION_BUILD_BENCHMARKS)
  target_compile_definitions(dandelion_frontend_workload_tests PRIVATE
    "DANDELION_FRONTEND_BENCHMARK_EXECUTABLE=\"${frontend_benchmark_path}\""
  )
  add_dependencies(dandelion_frontend_workload_tests
    dandelion_frontend_benchmarks
  )
endif()
dandelion_configure_target(dandelion_frontend_workload_tests
  SOURCES frontend_workload_test.cc
)
gtest_discover_tests(dandelion_frontend_workload_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS benchmark-infrastructure TIMEOUT 120
)
