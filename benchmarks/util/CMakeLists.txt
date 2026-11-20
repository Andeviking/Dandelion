set(util_benchmark_sources util_bench.cc creation.cc computation.cc)
add_executable(dandelion_util_benchmarks ${util_benchmark_sources})
target_link_libraries(dandelion_util_benchmarks PRIVATE
  dandelion_util benchmark::benchmark
)
dandelion_configure_target(dandelion_util_benchmarks
  SOURCES ${util_benchmark_sources}
)

if(DANDELION_BUILD_TESTS AND UNIX)
  add_executable(dandelion_util_benchmark_tests util_bench_test.cc)
  target_link_libraries(dandelion_util_benchmark_tests PRIVATE
    dandelion_util ${DANDELION_GTEST_MAIN_TARGET} benchmark::benchmark
  )
  target_compile_definitions(dandelion_util_benchmark_tests PRIVATE
    "DANDELION_UTIL_BENCHMARK_PATH=\"$<TARGET_FILE:dandelion_util_benchmarks>\""
  )
  add_dependencies(dandelion_util_benchmark_tests dandelion_util_benchmarks)
  dandelion_configure_target(dandelion_util_benchmark_tests
    SOURCES util_bench_test.cc
  )
  include(GoogleTest)
  gtest_discover_tests(dandelion_util_benchmark_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS benchmark-infrastructure TIMEOUT 60
  )
endif()
