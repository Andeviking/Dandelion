set(frontend_test_sources
  syntax_test.cc command_test.cc profile_test.cc operator_test.cc
  binding_test.cc sort_datatype_test.cc
  driver_test.cc model_test.cc probe_test.cc overhaul_test.cc
)
add_executable(dandelion_frontend_probe parse_probe.cc)
target_link_libraries(dandelion_frontend_probe PRIVATE dandelion_frontend)
dandelion_configure_target(dandelion_frontend_probe SOURCES parse_probe.cc)

add_executable(dandelion_frontend_tests ${frontend_test_sources})
add_dependencies(dandelion_frontend_tests dandelion_frontend_probe)
target_compile_definitions(dandelion_frontend_tests PRIVATE
  DANDELION_FRONTEND_PROBE_PATH="$<TARGET_FILE:dandelion_frontend_probe>"
)
# Intercept scalar C++ allocations for the ModelOwner construction failure test.
target_link_options(dandelion_frontend_tests PRIVATE "LINKER:--wrap=_Znwm")
target_link_libraries(dandelion_frontend_tests PRIVATE
  dandelion_frontend ${DANDELION_GTEST_MAIN_TARGET}
)
dandelion_configure_target(dandelion_frontend_tests
  SOURCES ${frontend_test_sources})
include(GoogleTest)
gtest_discover_tests(dandelion_frontend_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS frontend
)

