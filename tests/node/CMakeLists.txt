set(node_test_sources
  sort_test.cc declaration_test.cc datatype_test.cc
  operator_test.cc boundary_test.cc value_test.cc identity_test.cc
  lifetime_test.cc table_test.cc traversal_test.cc failure_test.cc
  frontend_boundary_test.cc category_test.cc phase_one_test.cc
  phase_two_test.cc module_test.cc numeric_payload_test.cc numeric_failure_test.cc
)
add_executable(dandelion_node_tests ${node_test_sources})
target_link_libraries(dandelion_node_tests PRIVATE
  dandelion_node dandelion_util ${DANDELION_GTEST_MAIN_TARGET}
)
dandelion_configure_target(dandelion_node_tests SOURCES ${node_test_sources})
include(GoogleTest)
gtest_discover_tests(dandelion_node_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS node
)
