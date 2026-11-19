include_guard(GLOBAL)

set(DANDELION_BENCHMARK_SOURCE_DIR "" CACHE PATH
    "Existing standalone Google Benchmark source directory")

set(_DANDELION_BENCHMARK_VERSION "1.10.0")
set(_DANDELION_BENCHMARK_COMMIT "a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c")
set(_DANDELION_BENCHMARK_SHA256
    "1e34f52437882bfe010f955580e77a19b4fb0a702ef678609b509a834296075c")

function(dandelion_check_benchmark_source source_dir)
  if(NOT EXISTS "${source_dir}/CMakeLists.txt"
     OR NOT EXISTS "${source_dir}/include/benchmark/benchmark.h")
    message(FATAL_ERROR
      "Not a standalone Google Benchmark source directory: ${source_dir}. "
      "Set DANDELION_BENCHMARK_SOURCE_DIR to the Google Benchmark source root.")
  endif()
endfunction()

function(dandelion_check_managed_benchmark source_dir)
  if(IS_SYMLINK "${source_dir}" OR NOT IS_DIRECTORY "${source_dir}")
    message(FATAL_ERROR "Invalid managed Google Benchmark directory: ${source_dir}")
  endif()
  set(marker "${source_dir}/.dandelion-benchmark-commit")
  if(NOT EXISTS "${marker}")
    message(FATAL_ERROR
      "The managed Google Benchmark directory has no provenance marker: ${source_dir}. "
      "Inspect it or provide existing sources with DANDELION_BENCHMARK_SOURCE_DIR.")
  endif()
  file(READ "${marker}" marker_value)
  string(STRIP "${marker_value}" marker_value)
  if(NOT marker_value STREQUAL _DANDELION_BENCHMARK_COMMIT)
    message(FATAL_ERROR "Unexpected Google Benchmark provenance in ${source_dir}.")
  endif()
  dandelion_check_benchmark_source("${source_dir}")
endfunction()

function(dandelion_download_benchmark source_dir)
  file(MAKE_DIRECTORY "${DANDELION_SUBPROJECTS_DIR}")
  file(LOCK "${DANDELION_SUBPROJECTS_DIR}/.benchmark-${_DANDELION_BENCHMARK_VERSION}.lock"
       GUARD FUNCTION TIMEOUT 180 RESULT_VARIABLE lock_result)
  if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "Could not lock Google Benchmark acquisition: ${lock_result}")
  endif()

  # Another configure may have completed the acquisition while we waited.
  if(EXISTS "${source_dir}" OR IS_SYMLINK "${source_dir}")
    dandelion_check_managed_benchmark("${source_dir}")
    return()
  endif()

  string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef stage_id)
  set(stage "${DANDELION_SUBPROJECTS_DIR}/.benchmark-${_DANDELION_BENCHMARK_VERSION}-${stage_id}.tmp")
  if(EXISTS "${stage}" OR IS_SYMLINK "${stage}")
    message(FATAL_ERROR "Google Benchmark staging directory already exists: ${stage}")
  endif()
  file(MAKE_DIRECTORY "${stage}")
  set(archive "${stage}/benchmark.tar.gz")
  set(url "https://codeload.github.com/google/benchmark/tar.gz/${_DANDELION_BENCHMARK_COMMIT}")
  message(STATUS "Downloading Google Benchmark ${_DANDELION_BENCHMARK_VERSION} into ${DANDELION_SUBPROJECTS_DIR}")
  file(DOWNLOAD "${url}" "${archive}" TLS_VERIFY ON TIMEOUT 180 STATUS download_status)
  list(GET download_status 0 download_code)
  if(NOT download_code EQUAL 0)
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "Google Benchmark download failed: ${download_status}")
  endif()
  file(SHA256 "${archive}" actual_sha256)
  if(NOT actual_sha256 STREQUAL _DANDELION_BENCHMARK_SHA256)
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "Google Benchmark archive checksum mismatch.")
  endif()

  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${stage}/extracted")
  set(extracted "${stage}/extracted/benchmark-${_DANDELION_BENCHMARK_COMMIT}")
  dandelion_check_benchmark_source("${extracted}")
  file(WRITE "${extracted}/.dandelion-benchmark-commit" "${_DANDELION_BENCHMARK_COMMIT}\n")
  file(RENAME "${extracted}" "${source_dir}" RESULT rename_result)
  if(NOT rename_result STREQUAL "0")
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "Could not publish Google Benchmark sources: ${rename_result}")
  endif()
  file(REMOVE_RECURSE "${stage}")
endfunction()

function(dandelion_add_benchmark_source source_dir)
  dandelion_check_benchmark_source("${source_dir}")
  # These normal variables affect the embedded project without forcing the cache.
  set(BENCHMARK_ENABLE_TESTING OFF)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
  set(BENCHMARK_ENABLE_INSTALL OFF)
  set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF)
  set(BENCHMARK_ENABLE_WERROR OFF)
  add_subdirectory("${source_dir}" "${PROJECT_BINARY_DIR}/subprojects/benchmark-build"
                   EXCLUDE_FROM_ALL SYSTEM)
  if(NOT TARGET benchmark::benchmark)
    message(FATAL_ERROR "Google Benchmark source did not provide benchmark::benchmark.")
  endif()
endfunction()

function(dandelion_resolve_benchmark)
  if(TARGET benchmark::benchmark)
    message(STATUS "Using Google Benchmark target provided by the parent project")
  elseif(DANDELION_BENCHMARK_SOURCE_DIR)
    message(STATUS "Using user-provided Google Benchmark source: ${DANDELION_BENCHMARK_SOURCE_DIR}")
    dandelion_add_benchmark_source("${DANDELION_BENCHMARK_SOURCE_DIR}")
  else()
    find_package(benchmark 1.10.0 CONFIG QUIET)
    if(TARGET benchmark::benchmark)
      message(STATUS "Using an installed Google Benchmark package")
    else()
      set(managed_source "${DANDELION_SUBPROJECTS_DIR}/benchmark-${_DANDELION_BENCHMARK_VERSION}")
      if(EXISTS "${managed_source}" OR IS_SYMLINK "${managed_source}")
        dandelion_check_managed_benchmark("${managed_source}")
        message(STATUS "Using project-managed Google Benchmark source: ${managed_source}")
        dandelion_add_benchmark_source("${managed_source}")
      elseif(DANDELION_DEPENDENCY_MODE STREQUAL "FETCH")
        dandelion_download_benchmark("${managed_source}")
        dandelion_add_benchmark_source("${managed_source}")
      else()
        message(FATAL_ERROR
          "Google Benchmark was not found. Provide a compatible installed package "
          "or set DANDELION_BENCHMARK_SOURCE_DIR, or reconfigure with "
          "-DDANDELION_DEPENDENCY_MODE=FETCH to download a project-local copy. "
          "To build without benchmarks, set -DDANDELION_BUILD_BENCHMARKS=OFF.")
      endif()
    endif()
  endif()
endfunction()
