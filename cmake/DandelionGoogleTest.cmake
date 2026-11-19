include_guard(GLOBAL)

set(DANDELION_GTEST_SOURCE_DIR "" CACHE PATH
    "Existing standalone GoogleTest source directory")

set(_DANDELION_GTEST_VERSION "1.18.0")
set(_DANDELION_GTEST_COMMIT "063de7e9578f82b369302001269680b4b1553359")
set(_DANDELION_GTEST_SHA256
    "623fe05d283020f37a579a8985ba338091ac7229da62b5591ff40acdf05b95e2")

function(dandelion_check_googletest_source source_dir)
  if(NOT EXISTS "${source_dir}/CMakeLists.txt"
     OR NOT EXISTS "${source_dir}/googletest/include/gtest/gtest.h")
    message(FATAL_ERROR
      "Not a standalone GoogleTest source directory: ${source_dir}. "
      "Set DANDELION_GTEST_SOURCE_DIR to the GoogleTest source root.")
  endif()
endfunction()

function(dandelion_check_managed_googletest source_dir)
  if(IS_SYMLINK "${source_dir}" OR NOT IS_DIRECTORY "${source_dir}")
    message(FATAL_ERROR "Invalid managed GoogleTest directory: ${source_dir}")
  endif()
  set(marker "${source_dir}/.dandelion-googletest-commit")
  if(NOT EXISTS "${marker}")
    message(FATAL_ERROR
      "The managed GoogleTest directory has no provenance marker: ${source_dir}. "
      "Inspect it or provide existing sources with DANDELION_GTEST_SOURCE_DIR.")
  endif()
  file(READ "${marker}" marker_value)
  string(STRIP "${marker_value}" marker_value)
  if(NOT marker_value STREQUAL _DANDELION_GTEST_COMMIT)
    message(FATAL_ERROR "Unexpected GoogleTest provenance in ${source_dir}.")
  endif()
  dandelion_check_googletest_source("${source_dir}")
endfunction()

function(dandelion_download_googletest source_dir)
  file(MAKE_DIRECTORY "${DANDELION_SUBPROJECTS_DIR}")
  file(LOCK "${DANDELION_SUBPROJECTS_DIR}/.googletest-${_DANDELION_GTEST_VERSION}.lock"
       GUARD FUNCTION TIMEOUT 180 RESULT_VARIABLE lock_result)
  if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "Could not lock GoogleTest acquisition: ${lock_result}")
  endif()

  # Another configure may have completed the acquisition while we waited.
  if(EXISTS "${source_dir}" OR IS_SYMLINK "${source_dir}")
    dandelion_check_managed_googletest("${source_dir}")
    return()
  endif()

  string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef stage_id)
  set(stage "${DANDELION_SUBPROJECTS_DIR}/.googletest-${_DANDELION_GTEST_VERSION}-${stage_id}.tmp")
  if(EXISTS "${stage}" OR IS_SYMLINK "${stage}")
    message(FATAL_ERROR "GoogleTest staging directory already exists: ${stage}")
  endif()
  file(MAKE_DIRECTORY "${stage}")
  set(archive "${stage}/googletest.tar.gz")
  set(url "https://codeload.github.com/google/googletest/tar.gz/${_DANDELION_GTEST_COMMIT}")
  message(STATUS "Downloading GoogleTest ${_DANDELION_GTEST_VERSION} into ${DANDELION_SUBPROJECTS_DIR}")
  file(DOWNLOAD "${url}" "${archive}" TLS_VERIFY ON TIMEOUT 180 STATUS download_status)
  list(GET download_status 0 download_code)
  if(NOT download_code EQUAL 0)
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "GoogleTest download failed: ${download_status}")
  endif()
  file(SHA256 "${archive}" actual_sha256)
  if(NOT actual_sha256 STREQUAL _DANDELION_GTEST_SHA256)
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "GoogleTest archive checksum mismatch.")
  endif()

  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${stage}/extracted")
  set(extracted "${stage}/extracted/googletest-${_DANDELION_GTEST_COMMIT}")
  dandelion_check_googletest_source("${extracted}")
  file(WRITE "${extracted}/.dandelion-googletest-commit" "${_DANDELION_GTEST_COMMIT}\n")
  file(RENAME "${extracted}" "${source_dir}" RESULT rename_result)
  if(NOT rename_result STREQUAL "0")
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "Could not publish GoogleTest sources: ${rename_result}")
  endif()
  file(REMOVE_RECURSE "${stage}")
endfunction()

function(dandelion_add_googletest_source source_dir)
  dandelion_check_googletest_source("${source_dir}")
  # These normal variables affect this subdirectory without forcing parent cache
  # choices. GoogleTest uses option(), which honors them with CMP0077 enabled.
  set(BUILD_GMOCK OFF)
  set(INSTALL_GTEST OFF)
  set(gtest_build_tests OFF)
  set(gmock_build_tests OFF)
  add_subdirectory("${source_dir}" "${PROJECT_BINARY_DIR}/subprojects/googletest-build"
                   EXCLUDE_FROM_ALL SYSTEM)
endfunction()

function(dandelion_resolve_googletest)
  if(TARGET GTest::gtest_main OR TARGET GTest::gtest)
    message(STATUS "Using GoogleTest target provided by the parent project")
  elseif(DANDELION_GTEST_SOURCE_DIR)
    message(STATUS "Using user-provided GoogleTest source: ${DANDELION_GTEST_SOURCE_DIR}")
    dandelion_add_googletest_source("${DANDELION_GTEST_SOURCE_DIR}")
  else()
    find_package(GTest QUIET)
    if(TARGET GTest::gtest_main OR TARGET GTest::gtest)
      message(STATUS "Using an installed GoogleTest package")
    else()
      set(managed_source "${DANDELION_SUBPROJECTS_DIR}/googletest-${_DANDELION_GTEST_VERSION}")
      if(EXISTS "${managed_source}" OR IS_SYMLINK "${managed_source}")
        dandelion_check_managed_googletest("${managed_source}")
        message(STATUS "Using project-managed GoogleTest source: ${managed_source}")
        dandelion_add_googletest_source("${managed_source}")
      elseif(EXISTS "/usr/src/googletest/CMakeLists.txt"
             AND EXISTS "/usr/src/googletest/googletest/include/gtest/gtest.h")
        message(STATUS "Using existing GoogleTest source: /usr/src/googletest")
        dandelion_add_googletest_source("/usr/src/googletest")
      elseif(DANDELION_DEPENDENCY_MODE STREQUAL "FETCH")
        dandelion_download_googletest("${managed_source}")
        dandelion_add_googletest_source("${managed_source}")
      else()
        message(FATAL_ERROR
          "GoogleTest was not found. Install a compatible GoogleTest package, "
          "set GTest_ROOT or DANDELION_GTEST_SOURCE_DIR, or reconfigure with "
          "-DDANDELION_DEPENDENCY_MODE=FETCH to download a project-local copy. "
          "To build without tests, set -DDANDELION_BUILD_TESTS=OFF.")
      endif()
    endif()
  endif()

  if(TARGET GTest::gtest)
    set(gtest_target GTest::gtest)
  elseif(TARGET GTest::gtest_main)
    set(gtest_target GTest::gtest_main)
  else()
    message(FATAL_ERROR "GoogleTest did not provide a usable CMake target.")
  endif()
  add_library(dandelion_test_main STATIC "${PROJECT_SOURCE_DIR}/tests/test_main.cc")
  target_link_libraries(dandelion_test_main PUBLIC ${gtest_target})
  dandelion_configure_target(dandelion_test_main
    SOURCES "${PROJECT_SOURCE_DIR}/tests/test_main.cc")
  set(DANDELION_GTEST_MAIN_TARGET dandelion_test_main PARENT_SCOPE)
endfunction()
