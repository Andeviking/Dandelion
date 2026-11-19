add_library(dandelion_node_workload STATIC)
target_sources(dandelion_node_workload
  PUBLIC FILE_SET CXX_MODULES FILES workload.cppm
  PRIVATE workload.cc
)
target_link_libraries(dandelion_node_workload PUBLIC dandelion_node)
dandelion_configure_target(dandelion_node_workload SOURCES
  workload.cppm workload.cc
)
if(DANDELION_BUILD_BENCHMARKS)
  add_executable(dandelion_node_benchmarks node_bench.cc)
  target_link_libraries(dandelion_node_benchmarks PRIVATE
    dandelion_node_workload benchmark::benchmark
  )
  dandelion_configure_target(dandelion_node_benchmarks SOURCES node_bench.cc)
endif()
