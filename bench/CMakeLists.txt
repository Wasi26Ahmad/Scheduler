add_executable(infsched-bench
  benchmark_main.cpp
)

target_include_directories(infsched-bench PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(infsched-bench PRIVATE
  infsched_core
  infsched_worker_lib
)
