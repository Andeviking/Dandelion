include_guard(GLOBAL)

set(GMP_ROOT "" CACHE PATH "Prefix of an existing GMP installation")
set(GMP_INCLUDE_DIR "" CACHE PATH "Directory containing the selected gmp.h")
set(GMP_LIBRARY "" CACHE FILEPATH "Selected GMP library")
set(DANDELION_GMP_SOURCE_DIR "" CACHE PATH "Clean standalone GMP source directory")
set(DANDELION_GMP_ARCHIVE "" CACHE FILEPATH "Official GMP 6.3.0 source archive")
set(DANDELION_GMP_BUILD_JOBS "1" CACHE STRING "Parallel GNU Make jobs for a GMP source build")

set(_DANDELION_GMP_VERSION "6.3.0")
set(_DANDELION_GMP_SHA256
    "a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898")

function(dandelion_gmp_header_version header result)
  set(version_parts "")
  foreach(suffix IN ITEMS "" "_MINOR" "_PATCHLEVEL")
    set(name "__GNU_MP_VERSION${suffix}")
    if(NOT EXISTS "${header}")
      set(${result} "" PARENT_SCOPE)
      return()
    endif()
    file(STRINGS "${header}" line
         REGEX "^[ \t]*#[ \t]*define[ \t]+${name}[ \t]+[0-9]+")
    if(NOT line)
      set(${result} "" PARENT_SCOPE)
      return()
    endif()
    string(REGEX MATCH "[0-9]+" part "${line}")
    list(APPEND version_parts "${part}")
  endforeach()
  list(JOIN version_parts "." version)
  set(${result} "${version}" PARENT_SCOPE)
endfunction()

function(dandelion_check_gmp_source source_dir)
  if(NOT EXISTS "${source_dir}/configure" OR NOT EXISTS "${source_dir}/gmp-h.in")
    message(FATAL_ERROR "Not a standalone GMP source directory: ${source_dir}")
  endif()
  if(EXISTS "${source_dir}/config.status")
    message(FATAL_ERROR
      "GMP source directory is already configured in place: ${source_dir}. "
      "Provide a clean source tree or DANDELION_GMP_ARCHIVE; it will not be modified.")
  endif()
  dandelion_gmp_header_version("${source_dir}/gmp-h.in" source_version)
  if(NOT source_version OR source_version VERSION_LESS _DANDELION_GMP_VERSION)
    message(FATAL_ERROR
      "GMP source must be version ${_DANDELION_GMP_VERSION} or newer: ${source_dir}")
  endif()
endfunction()

function(dandelion_check_managed_gmp source_dir)
  if(IS_SYMLINK "${source_dir}" OR NOT IS_DIRECTORY "${source_dir}")
    message(FATAL_ERROR "Invalid managed GMP directory: ${source_dir}")
  endif()
  set(marker "${source_dir}/.dandelion-gmp-sha256")
  if(NOT EXISTS "${marker}")
    message(FATAL_ERROR
      "The managed GMP directory has no provenance marker: ${source_dir}. "
      "Inspect it or use DANDELION_GMP_SOURCE_DIR for user-provided sources.")
  endif()
  file(READ "${marker}" marker_value)
  string(STRIP "${marker_value}" marker_value)
  if(NOT marker_value STREQUAL _DANDELION_GMP_SHA256)
    message(FATAL_ERROR "Unexpected GMP provenance in ${source_dir}.")
  endif()
  dandelion_check_gmp_source("${source_dir}")
endfunction()

function(dandelion_prepare_managed_gmp source_dir)
  if(DANDELION_GMP_ARCHIVE)
    get_filename_component(provided_archive "${DANDELION_GMP_ARCHIVE}" ABSOLUTE
                           BASE_DIR "${PROJECT_SOURCE_DIR}")
    if(NOT EXISTS "${provided_archive}" OR IS_DIRECTORY "${provided_archive}")
      message(FATAL_ERROR "GMP archive does not exist: ${provided_archive}")
    endif()
    file(SHA256 "${provided_archive}" provided_sha256)
    if(NOT provided_sha256 STREQUAL _DANDELION_GMP_SHA256)
      message(FATAL_ERROR "GMP ${_DANDELION_GMP_VERSION} archive checksum mismatch.")
    endif()
  endif()
  file(MAKE_DIRECTORY "${DANDELION_SUBPROJECTS_DIR}")
  file(LOCK "${DANDELION_SUBPROJECTS_DIR}/.gmp-${_DANDELION_GMP_VERSION}.lock"
       GUARD FUNCTION TIMEOUT 180 RESULT_VARIABLE lock_result)
  if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "Could not lock GMP acquisition: ${lock_result}")
  endif()

  if(EXISTS "${source_dir}" OR IS_SYMLINK "${source_dir}")
    dandelion_check_managed_gmp("${source_dir}")
    return()
  endif()

  string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef stage_id)
  set(stage "${DANDELION_SUBPROJECTS_DIR}/.gmp-${_DANDELION_GMP_VERSION}-${stage_id}.tmp")
  if(EXISTS "${stage}" OR IS_SYMLINK "${stage}")
    message(FATAL_ERROR "GMP staging directory already exists: ${stage}")
  endif()
  file(MAKE_DIRECTORY "${stage}")
  if(DANDELION_GMP_ARCHIVE)
    set(archive "${provided_archive}")
    message(STATUS "Preparing GMP from user-provided archive: ${archive}")
  elseif(DANDELION_DEPENDENCY_MODE STREQUAL "FETCH")
    set(archive "${stage}/gmp-${_DANDELION_GMP_VERSION}.tar.xz")
    set(url "https://ftp.gnu.org/gnu/gmp/gmp-${_DANDELION_GMP_VERSION}.tar.xz")
    message(STATUS "Downloading GMP ${_DANDELION_GMP_VERSION} into ${DANDELION_SUBPROJECTS_DIR}")
    file(DOWNLOAD "${url}" "${archive}" TLS_VERIFY ON TIMEOUT 180 STATUS download_status)
    list(GET download_status 0 download_code)
    if(NOT download_code EQUAL 0)
      file(REMOVE_RECURSE "${stage}")
      message(FATAL_ERROR "GMP download failed: ${download_status}")
    endif()
  else()
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "GMP acquisition requires an archive or FETCH mode.")
  endif()

  file(SHA256 "${archive}" actual_sha256)
  if(NOT actual_sha256 STREQUAL _DANDELION_GMP_SHA256)
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "GMP ${_DANDELION_GMP_VERSION} archive checksum mismatch.")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${stage}/extracted")
  set(extracted "${stage}/extracted/gmp-${_DANDELION_GMP_VERSION}")
  dandelion_check_gmp_source("${extracted}")
  file(WRITE "${extracted}/.dandelion-gmp-sha256" "${_DANDELION_GMP_SHA256}\n")
  file(RENAME "${extracted}" "${source_dir}" RESULT rename_result)
  if(NOT rename_result STREQUAL "0")
    file(REMOVE_RECURSE "${stage}")
    message(FATAL_ERROR "Could not publish GMP sources: ${rename_result}")
  endif()
  file(REMOVE_RECURSE "${stage}")
endfunction()

function(dandelion_check_gmp_link target result)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LIBRARIES "${target}")
  set(CMAKE_REQUIRED_QUIET TRUE)
  unset(_DANDELION_GMP_LINKS CACHE)
  check_cxx_source_compiles([=[
    #include <gmp.h>
    #if __GNU_MP_VERSION < 6 || (__GNU_MP_VERSION == 6 && __GNU_MP_VERSION_MINOR < 3)
    #error GMP 6.3.0 or newer is required
    #endif
    int main() {
      mpz_t value;
      mpz_init_set_ui(value, 10);
      // This public function was introduced in GMP 6.3.0.
      mpz_prevprime(value, value);
      mpz_clear(value);
      return 0;
    }
  ]=] _DANDELION_GMP_LINKS)
  if(NOT _DANDELION_GMP_LINKS OR CMAKE_CROSSCOMPILING)
    set(${result} "${_DANDELION_GMP_LINKS}" PARENT_SCOPE)
    return()
  endif()

  include(CheckCXXSourceRuns)
  unset(_DANDELION_GMP_RUNS CACHE)
  check_cxx_source_runs([=[
    #include <cstdio>
    #include <gmp.h>
    int main() {
      int major = 0, minor = 0, patch = 0;
      if (std::sscanf(gmp_version, "%d.%d.%d", &major, &minor, &patch) != 3)
        return 1;
      return major > 6 || (major == 6 && minor >= 3) ? 0 : 1;
    }
  ]=] _DANDELION_GMP_RUNS)
  set(${result} "${_DANDELION_GMP_RUNS}" PARENT_SCOPE)
endfunction()

function(dandelion_use_gmp_files include_dir library is_explicit result)
  dandelion_gmp_header_version("${include_dir}/gmp.h" version)
  if(NOT EXISTS "${library}" OR NOT version OR version VERSION_LESS _DANDELION_GMP_VERSION)
    if(is_explicit)
      message(FATAL_ERROR
        "The selected GMP headers and library must exist and be version "
        "${_DANDELION_GMP_VERSION} or newer (header version: '${version}').")
    endif()
    set(${result} FALSE PARENT_SCOPE)
    return()
  endif()
  add_library(dandelion_gmp_candidate UNKNOWN IMPORTED GLOBAL)
  set_target_properties(dandelion_gmp_candidate PROPERTIES
    IMPORTED_LOCATION "${library}"
    INTERFACE_INCLUDE_DIRECTORIES "${include_dir}")
  dandelion_check_gmp_link(dandelion_gmp_candidate usable)
  if(NOT usable)
    if(is_explicit)
      message(FATAL_ERROR
        "The selected GMP headers and library failed the compile, link, or "
        "runtime version check. Verify their ABI and runtime library path.")
    endif()
    set(${result} FALSE PARENT_SCOPE)
    return()
  endif()
  add_library(GMP::GMP ALIAS dandelion_gmp_candidate)
  message(STATUS "Using GMP ${version}: ${library}")
  set(${result} TRUE PARENT_SCOPE)
endfunction()

function(dandelion_build_gmp_source source_dir)
  dandelion_check_gmp_source("${source_dir}")
  if(CMAKE_CROSSCOMPILING OR CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR
      "GMP source builds currently require native, single-configuration builds. "
      "Provide a compatible prebuilt GMP for this toolchain.")
  endif()
  if(NOT DANDELION_GMP_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "DANDELION_GMP_BUILD_JOBS must be a positive integer.")
  endif()
  find_program(_DANDELION_GNU_MAKE NAMES gmake make REQUIRED)
  execute_process(COMMAND "${_DANDELION_GNU_MAKE}" --version
                  OUTPUT_VARIABLE make_version RESULT_VARIABLE make_result)
  if(NOT make_result EQUAL 0 OR NOT make_version MATCHES "^GNU Make")
    message(FATAL_ERROR "Building GMP from source requires GNU Make.")
  endif()

  enable_language(C)
  include(ExternalProject)
  set(prefix "${PROJECT_BINARY_DIR}/subprojects/gmp-install")
  set(c_compiler "${CMAKE_C_COMPILER}")
  if(CMAKE_C_COMPILER_ARG1)
    string(APPEND c_compiler " ${CMAKE_C_COMPILER_ARG1}")
  endif()
  dandelion_gmp_header_version("${source_dir}/gmp-h.in" source_version)
  if(source_version VERSION_LESS "6.4.0"
     AND CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    # GMP 6.3.0 has configure probes that are invalid in C23. GCC 15 and
    # later default to C23, so select the earlier C dialect for this dependency.
    string(APPEND c_compiler " -std=gnu17")
  endif()
  set(environment "CC=${c_compiler}")
  if(CMAKE_C_FLAGS)
    list(APPEND environment "CFLAGS=${CMAKE_C_FLAGS}")
  endif()
  file(MAKE_DIRECTORY "${prefix}/include")
  ExternalProject_Add(dandelion_gmp_external
    PREFIX "${PROJECT_BINARY_DIR}/subprojects/gmp-external"
    SOURCE_DIR "${source_dir}"
    BINARY_DIR "${PROJECT_BINARY_DIR}/subprojects/gmp-build"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env ${environment}
      "${source_dir}/configure" "--prefix=${prefix}"
      --disable-shared --enable-static --with-pic --disable-cxx
    BUILD_COMMAND "${_DANDELION_GNU_MAKE}" "-j${DANDELION_GMP_BUILD_JOBS}"
    INSTALL_COMMAND "${_DANDELION_GNU_MAKE}" install
    INSTALL_BYPRODUCTS "${prefix}/include/gmp.h" "${prefix}/lib/libgmp.a"
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_INSTALL TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
  )
  add_library(GMP::GMP STATIC IMPORTED GLOBAL)
  set_target_properties(GMP::GMP PROPERTIES
    IMPORTED_LOCATION "${prefix}/lib/libgmp.a"
    INTERFACE_INCLUDE_DIRECTORIES "${prefix}/include")
  add_dependencies(GMP::GMP dandelion_gmp_external)
  message(STATUS "Building GMP from ${source_dir} into ${prefix}")
endfunction()

function(dandelion_resolve_gmp)
  if(DANDELION_GMP_SOURCE_DIR AND DANDELION_GMP_ARCHIVE)
    message(FATAL_ERROR "Specify only one of DANDELION_GMP_SOURCE_DIR and DANDELION_GMP_ARCHIVE.")
  endif()
  if(TARGET GMP::GMP)
    get_target_property(imported GMP::GMP IMPORTED)
    if(imported)
      dandelion_check_gmp_link(GMP::GMP usable)
      if(NOT usable)
        message(FATAL_ERROR "The parent-provided GMP::GMP target is incompatible or older than 6.3.0.")
      endif()
    elseif(NOT DEFINED GMP_VERSION OR GMP_VERSION VERSION_LESS _DANDELION_GMP_VERSION)
      message(FATAL_ERROR
        "A parent-provided source target GMP::GMP must set GMP_VERSION to at least "
        "${_DANDELION_GMP_VERSION}; its headers are checked when compiled.")
    endif()
    message(STATUS "Using GMP::GMP target provided by the parent project")
    return()
  endif()

  if(DANDELION_GMP_SOURCE_DIR)
    get_filename_component(source_dir "${DANDELION_GMP_SOURCE_DIR}" ABSOLUTE
                           BASE_DIR "${PROJECT_SOURCE_DIR}")
    dandelion_build_gmp_source("${source_dir}")
    return()
  endif()
  set(managed_source "${DANDELION_SUBPROJECTS_DIR}/gmp-${_DANDELION_GMP_VERSION}")
  if(DANDELION_GMP_ARCHIVE)
    dandelion_prepare_managed_gmp("${managed_source}")
    dandelion_build_gmp_source("${managed_source}")
    return()
  endif()

  set(root "${GMP_ROOT}")
  if(NOT root AND DEFINED ENV{GMP_ROOT})
    set(root "$ENV{GMP_ROOT}")
  endif()
  if(GMP_INCLUDE_DIR OR GMP_LIBRARY)
    if(NOT GMP_INCLUDE_DIR OR NOT GMP_LIBRARY)
      message(FATAL_ERROR "Set both GMP_INCLUDE_DIR and GMP_LIBRARY, or neither.")
    endif()
    dandelion_use_gmp_files("${GMP_INCLUDE_DIR}" "${GMP_LIBRARY}" TRUE found)
    return()
  elseif(root)
    unset(_DANDELION_GMP_ROOT_INCLUDE CACHE)
    unset(_DANDELION_GMP_ROOT_LIBRARY CACHE)
    find_path(_DANDELION_GMP_ROOT_INCLUDE NAMES gmp.h PATHS "${root}"
              PATH_SUFFIXES include "include/${CMAKE_LIBRARY_ARCHITECTURE}" NO_DEFAULT_PATH)
    find_library(_DANDELION_GMP_ROOT_LIBRARY NAMES gmp PATHS "${root}"
                 PATH_SUFFIXES lib lib64 "lib/${CMAKE_LIBRARY_ARCHITECTURE}" NO_DEFAULT_PATH)
    dandelion_use_gmp_files("${_DANDELION_GMP_ROOT_INCLUDE}"
                           "${_DANDELION_GMP_ROOT_LIBRARY}" TRUE found)
    return()
  endif()

  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(_DANDELION_GMP_PKG QUIET IMPORTED_TARGET GLOBAL "gmp>=6.3.0")
    if(TARGET PkgConfig::_DANDELION_GMP_PKG)
      dandelion_check_gmp_link(PkgConfig::_DANDELION_GMP_PKG usable)
      if(usable)
        add_library(GMP::GMP ALIAS PkgConfig::_DANDELION_GMP_PKG)
        message(STATUS "Using GMP ${_DANDELION_GMP_PKG_VERSION} from pkg-config")
        return()
      endif()
    endif()
  endif()

  unset(_DANDELION_GMP_SYSTEM_INCLUDE CACHE)
  unset(_DANDELION_GMP_SYSTEM_LIBRARY CACHE)
  find_path(_DANDELION_GMP_SYSTEM_INCLUDE NAMES gmp.h)
  find_library(_DANDELION_GMP_SYSTEM_LIBRARY NAMES gmp)
  if(_DANDELION_GMP_SYSTEM_INCLUDE AND _DANDELION_GMP_SYSTEM_LIBRARY)
    dandelion_use_gmp_files("${_DANDELION_GMP_SYSTEM_INCLUDE}"
                           "${_DANDELION_GMP_SYSTEM_LIBRARY}" FALSE found)
    if(found)
      return()
    endif()
  endif()

  if(EXISTS "${managed_source}" OR IS_SYMLINK "${managed_source}")
    dandelion_check_managed_gmp("${managed_source}")
    message(STATUS "Using project-managed GMP source: ${managed_source}")
  elseif(DANDELION_DEPENDENCY_MODE STREQUAL "FETCH")
    dandelion_prepare_managed_gmp("${managed_source}")
  else()
    message(FATAL_ERROR
      "GMP ${_DANDELION_GMP_VERSION} or newer was not found. Install a compatible "
      "GMP, set GMP_ROOT or GMP_INCLUDE_DIR and GMP_LIBRARY, provide clean sources "
      "with DANDELION_GMP_SOURCE_DIR or a local 6.3.0 archive with "
      "DANDELION_GMP_ARCHIVE, or reconfigure with -DDANDELION_DEPENDENCY_MODE=FETCH.")
  endif()
  dandelion_build_gmp_source("${managed_source}")
endfunction()
