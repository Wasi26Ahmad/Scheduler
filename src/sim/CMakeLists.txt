add_library(infsched_sim_lib
  simulator.cpp
)

target_include_directories(infsched_sim_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(infsched_sim_lib PUBLIC
  infsched_core
)

add_executable(infsched-sim
  sim_main.cpp
)

target_link_libraries(infsched-sim PRIVATE
  infsched_sim_lib
  infsched_core
)
