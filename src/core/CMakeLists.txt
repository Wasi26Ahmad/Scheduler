add_library(infsched_core
  topology.cpp
  resource_manager.cpp
  cost_model.cpp
  dynamic_batcher.cpp
  scheduler.cpp
  shm.cpp
  metrics.cpp
)

target_include_directories(infsched_core PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(infsched_core PUBLIC
  pthread
)
