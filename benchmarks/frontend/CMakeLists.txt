add_library(dandelion_frontend_workload STATIC)
target_sources(dandelion_frontend_workload
  PUBLIC FILE_SET CXX_MODULES FILES workload.cppm
  PRIVATE workload.cc
)
target_link_libraries(dandelion_frontend_workload PUBLIC dandelion_frontend)
dandelion_configure_target(dandelion_frontend_workload SOURCES
  workload.cppm workload.cc
)
if(DANDELION_BUILD_BENCHMARKS)
  add_executable(dandelion_frontend_benchmarks frontend_bench.cc)
  target_link_libraries(dandelion_frontend_benchmarks PRIVATE
    dandelion_frontend_workload benchmark::benchmark
  )
  dandelion_configure_target(dandelion_frontend_benchmarks SOURCES
    frontend_bench.cc
  )
endif()
