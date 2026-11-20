# This module and executable validate the toolchain; they are not solver code.
add_library(dandelion_toolchain_module STATIC)
target_sources(dandelion_toolchain_module
  PUBLIC FILE_SET CXX_MODULES FILES module_probe.cppm
  PRIVATE module_probe.cc
)
dandelion_configure_target(dandelion_toolchain_module
  SOURCES module_probe.cppm module_probe.cc
)

add_executable(dandelion_toolchain_smoke module_smoke.cc gmp_smoke.cc)
target_link_libraries(dandelion_toolchain_smoke PRIVATE
  dandelion_toolchain_module ${DANDELION_GTEST_MAIN_TARGET} GMP::GMP
)
dandelion_configure_target(dandelion_toolchain_smoke
  SOURCES module_smoke.cc gmp_smoke.cc
)

include(GoogleTest)
gtest_discover_tests(dandelion_toolchain_smoke
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS toolchain
)
