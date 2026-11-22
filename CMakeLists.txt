cmake_minimum_required(VERSION 3.20)
project(infsched VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(INFSCHED_BUILD_TESTS "Build test suite" ON)
option(INFSCHED_BUILD_BENCH "Build benchmarks" ON)
option(INFSCHED_BUILD_SIM   "Build simulator" ON)
set(INFSCHED_SANITIZE "" CACHE STRING "address;undefined or thread")

if(INFSCHED_SANITIZE)
  add_compile_options(-fsanitize=${INFSCHED_SANITIZE} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${INFSCHED_SANITIZE})
endif()

# Compiler warnings
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

add_subdirectory(src/core)
add_subdirectory(src/worker)
add_subdirectory(src/daemon)

if(INFSCHED_BUILD_SIM)
  add_subdirectory(src/sim)
endif()

if(INFSCHED_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(INFSCHED_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
