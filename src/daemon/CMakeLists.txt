add_library(infsched_daemon_lib
  supervisor.cpp
  ops_server.cpp
)

target_include_directories(infsched_daemon_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(infsched_daemon_lib PUBLIC
  infsched_core
  infsched_worker_lib
)

add_executable(infsched-daemon
  daemon_main.cpp
)

target_link_libraries(infsched-daemon PRIVATE
  infsched_daemon_lib
  infsched_core
  infsched_worker_lib
)

add_executable(infsched-ctl
  ctl_main.cpp
)
