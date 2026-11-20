set(util_test_sources
  integer_test.cc rational_test.cc bitvector_test.cc floating_point_test.cc
  failure_test.cc gmp_failure_test.cc
)
add_executable(dandelion_util_tests ${util_test_sources})
target_link_libraries(dandelion_util_tests
  PRIVATE dandelion_util ${DANDELION_GTEST_MAIN_TARGET}
)
dandelion_configure_target(dandelion_util_tests SOURCES ${util_test_sources})

add_executable(dandelion_util_import_probe import_probe.cc)
target_link_libraries(dandelion_util_import_probe PRIVATE dandelion_util)
dandelion_configure_target(dandelion_util_import_probe SOURCES import_probe.cc)
# Exercise the sanitizer runtime flags propagated by the static library.
if(DANDELION_ENABLE_SANITIZERS)
  target_compile_options(dandelion_util_import_probe PRIVATE
    -fno-sanitize=address,undefined
  )
  set_property(TARGET dandelion_util_import_probe PROPERTY LINK_OPTIONS "")
endif()

add_executable(dandelion_util_gmp_oom_probe gmp_oom_probe.cc)
target_link_libraries(dandelion_util_gmp_oom_probe PRIVATE GMP::GMP)
dandelion_configure_target(dandelion_util_gmp_oom_probe SOURCES gmp_oom_probe.cc)
# ASan's virtual address reservation conflicts with the probe's RLIMIT_AS.
if(DANDELION_ENABLE_SANITIZERS)
  target_compile_options(dandelion_util_gmp_oom_probe PRIVATE
    -fno-sanitize=address,undefined
  )
  set_property(TARGET dandelion_util_gmp_oom_probe PROPERTY LINK_OPTIONS
    -fno-sanitize=address,undefined
  )
endif()

add_dependencies(dandelion_util_tests
  dandelion_util_gmp_oom_probe dandelion_util_import_probe
)
target_compile_definitions(dandelion_util_tests PRIVATE
  DANDELION_UTIL_GMP_OOM_PROBE_PATH="$<TARGET_FILE:dandelion_util_gmp_oom_probe>"
  DANDELION_UTIL_IMPORT_PROBE_PATH="$<TARGET_FILE:dandelion_util_import_probe>"
)

include(GoogleTest)
gtest_discover_tests(dandelion_util_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS util
)
