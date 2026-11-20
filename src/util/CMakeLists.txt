set(util_interfaces
  numeric.cppm error.cppm integer.cppm rational.cppm bitvector.cppm
  floating_point.cppm
)
set(util_private_interfaces internal.cppm)
set(util_sources integer.cc rational.cc bitvector.cc floating_point.cc)
add_library(dandelion_util STATIC)
target_sources(dandelion_util
  PUBLIC FILE_SET CXX_MODULES FILES ${util_interfaces}
  PRIVATE FILE_SET util_private_modules TYPE CXX_MODULES
          FILES ${util_private_interfaces}
  PRIVATE ${util_sources}
)
target_link_libraries(dandelion_util PRIVATE GMP::GMP)
if(DANDELION_BUILD_TESTS)
  target_compile_definitions(dandelion_util PRIVATE DANDELION_UTIL_TESTING=1)
endif()
if(DANDELION_ENABLE_SANITIZERS)
  target_link_options(dandelion_util INTERFACE
    -fsanitize=address,undefined -fno-sanitize-recover=undefined
  )
endif()
if(DANDELION_BUILD_FUZZERS)
  # Supply sanitizer coverage callbacks to ordinary executable consumers.
  target_link_options(dandelion_util INTERFACE -fsanitize=fuzzer-no-link)
endif()
dandelion_configure_target(dandelion_util
  FUZZ
  SOURCES ${util_interfaces} ${util_private_interfaces} ${util_sources}
)
