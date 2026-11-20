set(node_interfaces node.cppm)
set(node_private_interfaces node_internal.cppm)
set(node_sources
  node.cc sort_store.cc decl_store.cc datatype_builder.cc
  kind_rules.cc unique_table.cc traversal.cc
)
if(DANDELION_BUILD_TESTS)
  list(APPEND node_sources ../../tests/node/node_test_peer.cc)
endif()
add_library(dandelion_node STATIC)
target_sources(dandelion_node
  PUBLIC FILE_SET CXX_MODULES FILES ${node_interfaces}
  PRIVATE FILE_SET node_private_modules TYPE CXX_MODULES
          FILES ${node_private_interfaces}
  PRIVATE ${node_sources}
)
target_link_libraries(dandelion_node PUBLIC dandelion_util)
if(DANDELION_BUILD_TESTS)
  target_compile_definitions(dandelion_node PRIVATE DANDELION_NODE_TESTING=1)
endif()
if(DANDELION_ENABLE_SANITIZERS)
  target_link_options(dandelion_node INTERFACE
    -fsanitize=address,undefined -fno-sanitize-recover=undefined
  )
endif()
dandelion_configure_target(dandelion_node
  FUZZ
  SOURCES ${node_interfaces} ${node_private_interfaces} ${node_sources}
)
