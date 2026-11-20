option(DANDELION_ENABLE_COVERAGE "Instrument project sources for LLVM coverage" OFF)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
  if(DANDELION_BUILD_FUZZERS)
    message(FATAL_ERROR "DANDELION_BUILD_FUZZERS requires Clang with libFuzzer.")
  endif()
  if(DANDELION_ENABLE_COVERAGE)
    message(FATAL_ERROR "DANDELION_ENABLE_COVERAGE requires Clang with the profile runtime.")
  endif()
endif()

# Apply private development policy only to explicitly selected project targets.
function(dandelion_configure_target target)
  cmake_parse_arguments(PARSE_ARGV 1 policy "FUZZ" "" "SOURCES")
  if(NOT policy_SOURCES OR policy_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "List owned SOURCES for dandelion_configure_target().")
  endif()
  target_compile_features(${target} PUBLIC cxx_std_23)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    set(source_options -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
    if(DANDELION_WARNINGS_AS_ERRORS)
      list(APPEND source_options -Werror)
    endif()
    if(DANDELION_BUILD_FUZZERS AND policy_FUZZ)
      list(APPEND source_options -fsanitize=fuzzer-no-link)
    endif()
    if(DANDELION_ENABLE_COVERAGE)
      list(APPEND source_options -fprofile-instr-generate -fcoverage-mapping)
      get_target_property(target_type ${target} TYPE)
      if(target_type STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE -fprofile-instr-generate)
      else()
        target_link_options(${target} INTERFACE -fprofile-instr-generate)
      endif()
    endif()
    # CMake 4.5 copies target compile options into synthesized std module targets.
    # Keep warnings, fuzz instrumentation, and source coverage on owned files.
    set_property(SOURCE ${policy_SOURCES} TARGET_DIRECTORY ${target}
      APPEND PROPERTY COMPILE_OPTIONS ${source_options}
    )
    if(DANDELION_ENABLE_SANITIZERS)
      target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined -fno-sanitize-recover=undefined
        -fno-omit-frame-pointer
      )
      target_link_options(${target} PRIVATE
        -fsanitize=address,undefined -fno-sanitize-recover=undefined
      )
    endif()
  elseif(DANDELION_ENABLE_SANITIZERS)
    message(FATAL_ERROR "ASan + UBSan requires a supported GCC or Clang toolchain.")
  endif()
endfunction()
