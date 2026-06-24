cmake_minimum_required(VERSION 3.14)
project(uinteger_t LANGUAGES CXX)

add_library(uinteger_t INTERFACE)
target_include_directories(uinteger_t INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(uinteger_t INTERFACE cxx_std_17)

# Only build the smoke test when this repo is the top-level project, so
# consumers (e.g. via FetchContent) don't build our tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(uinteger_t_test test/test.cc)
  target_link_libraries(uinteger_t_test PRIVATE uinteger_t)
  add_test(NAME uinteger_t COMMAND uinteger_t_test)
endif()
