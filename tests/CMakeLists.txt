add_executable(test_topology test_topology.cpp)
target_link_libraries(test_topology PRIVATE infsched_core)
add_test(NAME test_topology COMMAND test_topology)

add_executable(test_resource_manager test_resource_manager.cpp)
target_link_libraries(test_resource_manager PRIVATE infsched_core)
add_test(NAME test_resource_manager COMMAND test_resource_manager)

add_executable(test_cost_model test_cost_model.cpp)
target_link_libraries(test_cost_model PRIVATE infsched_core)
add_test(NAME test_cost_model COMMAND test_cost_model)

add_executable(test_shm test_shm.cpp)
target_link_libraries(test_shm PRIVATE infsched_core)
add_test(NAME test_shm COMMAND test_shm)

add_executable(test_scheduler test_scheduler.cpp)
target_link_libraries(test_scheduler PRIVATE infsched_core infsched_worker_lib)
add_test(NAME test_scheduler COMMAND test_scheduler)

add_executable(test_chaos test_chaos.cpp)
target_link_libraries(test_chaos PRIVATE infsched_daemon_lib infsched_core)
add_test(NAME test_chaos COMMAND test_chaos)
