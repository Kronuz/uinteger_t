cmake_minimum_required(VERSION 3.14)
project(uinteger_t LANGUAGES CXX)

add_library(uinteger_t INTERFACE)
add_library(uinteger_t::uinteger_t ALIAS uinteger_t)
target_include_directories(uinteger_t INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(uinteger_t INTERFACE cxx_std_17)

# Only build the smoke test when this repo is the top-level project, so
# consumers (e.g. via FetchContent) don't build our tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(uinteger_t_test test/test.cc)
  target_link_libraries(uinteger_t_test PRIVATE uinteger_t)
  add_test(NAME uinteger_t COMMAND uinteger_t_test)

  # Kernel properties against a reference multiply: default 64-bit limbs, 32-bit
  # limbs, and 64-bit limbs on the portable (non-__int128) primitives.
  add_executable(uinteger_t_property test/property.cc)
  target_link_libraries(uinteger_t_property PRIVATE uinteger_t)
  add_test(NAME uinteger_t_property COMMAND uinteger_t_property)
  add_executable(uinteger_t_property32 test/property.cc)
  target_link_libraries(uinteger_t_property32 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property32 PRIVATE DIGIT_T=std::uint32_t HALF_DIGIT_T=std::uint16_t)
  add_test(NAME uinteger_t_property32 COMMAND uinteger_t_property32)
  add_executable(uinteger_t_property_portable test/property.cc)
  target_link_libraries(uinteger_t_property_portable PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_portable PRIVATE UINTEGER_T_NO_INT128)
  add_test(NAME uinteger_t_property_portable COMMAND uinteger_t_property_portable)

  # A runnable tour of the library (not a test): build & run ./build/uinteger_t_demo
  add_executable(uinteger_t_demo examples/demo.cc)
  target_link_libraries(uinteger_t_demo PRIVATE uinteger_t)
endif()
