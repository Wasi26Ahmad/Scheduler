add_library(infsched_worker_lib
  mock_backend.cpp
  onnx_backend.cpp
  gguf_backend.cpp
)

target_include_directories(infsched_worker_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(infsched_worker_lib PUBLIC
  infsched_core
)

add_executable(infsched-worker
  worker_main.cpp
)

target_link_libraries(infsched-worker PRIVATE
  infsched_worker_lib
  infsched_core
)
