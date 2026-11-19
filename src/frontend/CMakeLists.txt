set(frontend_interfaces frontend.cppm)
set(frontend_private_interfaces frontend_internal.cppm)
set(frontend_sources
  syntax.cc grammar.cc session.cc transaction.cc sorts.cc datatypes.cc
  operators.cc terms.cc elaboration.cc
  driver.cc model.cc printing.cc
)
add_library(dandelion_frontend STATIC)
target_sources(dandelion_frontend
  PUBLIC FILE_SET CXX_MODULES FILES ${frontend_interfaces}
  PRIVATE FILE_SET frontend_private_modules TYPE CXX_MODULES
    FILES ${frontend_private_interfaces}
  PRIVATE ${frontend_sources}
)
target_link_libraries(dandelion_frontend PUBLIC dandelion_node)
if(DANDELION_BUILD_TESTS)
  target_compile_definitions(dandelion_frontend PUBLIC DANDELION_FRONTEND_TESTING=1)
endif()
if(DANDELION_ENABLE_SANITIZERS)
  target_link_options(dandelion_frontend INTERFACE
    -fsanitize=address,undefined -fno-sanitize-recover=undefined
  )
endif()
dandelion_configure_target(dandelion_frontend
  SOURCES ${frontend_interfaces} ${frontend_private_interfaces} ${frontend_sources}
)
