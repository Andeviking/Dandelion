set(node_test_sources
  numeric_test.cc sort_test.cc declaration_test.cc datatype_test.cc
  operator_test.cc boundary_test.cc value_test.cc identity_test.cc
  lifetime_test.cc table_test.cc traversal_test.cc failure_test.cc
  frontend_boundary_test.cc category_test.cc gmp_failure_test.cc
)
add_executable(dandelion_node_tests ${node_test_sources})
target_link_libraries(dandelion_node_tests PRIVATE
  dandelion_node ${DANDELION_GTEST_MAIN_TARGET}
)
dandelion_configure_target(dandelion_node_tests SOURCES ${node_test_sources})
include(GoogleTest)
gtest_discover_tests(dandelion_node_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS node
)

add_executable(dandelion_gmp_oom_probe gmp_oom_probe.cc)
target_link_libraries(dandelion_gmp_oom_probe PRIVATE GMP::GMP)
dandelion_configure_target(dandelion_gmp_oom_probe SOURCES gmp_oom_probe.cc)
# ASan's virtual-address reservation would invalidate this isolated GMP probe.
# The library and GoogleTest executable retain their sanitizer policy.
if(DANDELION_ENABLE_SANITIZERS)
  target_compile_options(dandelion_gmp_oom_probe PRIVATE
    -fno-sanitize=address,undefined
  )
  target_link_options(dandelion_gmp_oom_probe PRIVATE
    -fno-sanitize=address,undefined
  )
endif()
add_dependencies(dandelion_node_tests dandelion_gmp_oom_probe)
target_compile_definitions(dandelion_node_tests PRIVATE
  DANDELION_GMP_OOM_PROBE_PATH="$<TARGET_FILE:dandelion_gmp_oom_probe>"
)
