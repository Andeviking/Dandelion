if(NOT UNIX)
  message(FATAL_ERROR
    "Developer-tool tests currently require POSIX. "
    "Set DANDELION_BUILD_TOOLING_TESTS=OFF to build the other tests.")
endif()

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

# Preserve paths in a C++ string literal, including unusual checkout paths.
foreach(name PROJECT_SOURCE_DIR Python3_EXECUTABLE)
  string(REPLACE "\\" "\\\\" escaped "${${name}}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  set(DANDELION_ESCAPED_${name} "${escaped}")
endforeach()
configure_file(tooling_config.h.in tooling_config.h @ONLY)

add_executable(dandelion_tooling_tests cpp_tools_test.cc)
target_include_directories(dandelion_tooling_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(dandelion_tooling_tests PRIVATE ${DANDELION_GTEST_MAIN_TARGET})
dandelion_configure_target(dandelion_tooling_tests SOURCES cpp_tools_test.cc)

include(GoogleTest)
gtest_discover_tests(dandelion_tooling_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS tooling
)
