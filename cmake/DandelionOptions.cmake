# Apply private development policy only to explicitly selected project targets.
function(dandelion_configure_target target)
  cmake_parse_arguments(PARSE_ARGV 1 policy "" "" "SOURCES")
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
    # CMake 4.5 copies target compile options into synthesized std module targets.
    # Source-local warnings keep project policy, especially -Werror, out of them.
    set_property(SOURCE ${policy_SOURCES} TARGET_DIRECTORY ${target}
      APPEND PROPERTY COMPILE_OPTIONS
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    )
    if(DANDELION_WARNINGS_AS_ERRORS)
      set_property(SOURCE ${policy_SOURCES} TARGET_DIRECTORY ${target}
        APPEND PROPERTY COMPILE_OPTIONS -Werror
      )
    endif()
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
