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
  # The digit kernels at their unroll limits: none (general kernel only) and all eight sizes.
  add_executable(uinteger_t_property_unroll0 test/property.cc)
  target_link_libraries(uinteger_t_property_unroll0 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_unroll0 PRIVATE UINTEGER_T_RADIX_UNROLL=0)
  add_test(NAME uinteger_t_property_unroll0 COMMAND uinteger_t_property_unroll0)
  add_executable(uinteger_t_property_unroll8 test/property.cc)
  target_link_libraries(uinteger_t_property_unroll8 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_unroll8 PRIVATE UINTEGER_T_RADIX_UNROLL=8)
  add_test(NAME uinteger_t_property_unroll8 COMMAND uinteger_t_property_unroll8)
  add_executable(uinteger_t_property_portable test/property.cc)
  target_link_libraries(uinteger_t_property_portable PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_portable PRIVATE UINTEGER_T_NO_INT128)
  add_test(NAME uinteger_t_property_portable COMMAND uinteger_t_property_portable)

  # Allocation counts: a result allocates once, and in-place operations with room allocate nothing.
  add_executable(uinteger_t_alloc test/alloc.cc)
  target_link_libraries(uinteger_t_alloc PRIVATE uinteger_t)
  add_test(NAME uinteger_t_alloc COMMAND uinteger_t_alloc)
  add_executable(uinteger_t_alloc32 test/alloc.cc)
  target_link_libraries(uinteger_t_alloc32 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_alloc32 PRIVATE DIGIT_T=std::uint32_t HALF_DIGIT_T=std::uint16_t)
  add_test(NAME uinteger_t_alloc32 COMMAND uinteger_t_alloc32)

  # The same properties with a tiny recursive-division threshold, so the divide-and-conquer
  # recursion runs many levels deep on small operands.
  add_executable(uinteger_t_property_dc test/property.cc)
  target_link_libraries(uinteger_t_property_dc PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_dc PRIVATE DIV_DC_THRESHOLD=4)
  add_test(NAME uinteger_t_property_dc COMMAND uinteger_t_property_dc)
  add_executable(uinteger_t_property_dc32 test/property.cc)
  target_link_libraries(uinteger_t_property_dc32 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_dc32 PRIVATE DIGIT_T=std::uint32_t HALF_DIGIT_T=std::uint16_t DIV_DC_THRESHOLD=4)
  add_test(NAME uinteger_t_property_dc32 COMMAND uinteger_t_property_dc32)

  # Concurrent first use of the base conversion tables, which are built lazily and published
  # with atomics (the TSan job in CI runs it), at both limb widths.
  find_package(Threads REQUIRED)
  add_executable(uinteger_t_radix_threads test/radix_threads.cc)
  target_link_libraries(uinteger_t_radix_threads PRIVATE uinteger_t Threads::Threads)
  add_test(NAME uinteger_t_radix_threads COMMAND uinteger_t_radix_threads)
  add_executable(uinteger_t_radix_threads32 test/radix_threads.cc)
  target_link_libraries(uinteger_t_radix_threads32 PRIVATE uinteger_t Threads::Threads)
  target_compile_definitions(uinteger_t_radix_threads32 PRIVATE DIGIT_T=std::uint32_t HALF_DIGIT_T=std::uint16_t)
  add_test(NAME uinteger_t_radix_threads32 COMMAND uinteger_t_radix_threads32)

  # The recursive-division kernel against schoolbook division on raw limbs, with guard limbs
  # around an exactly sized scratch buffer (opens up `private`, so it is test-only).
  add_executable(uinteger_t_dc_kernel test/dc_kernel.cc)
  target_link_libraries(uinteger_t_dc_kernel PRIVATE uinteger_t)
  add_test(NAME uinteger_t_dc_kernel COMMAND uinteger_t_dc_kernel)
  add_executable(uinteger_t_dc_kernel_small test/dc_kernel.cc)
  target_link_libraries(uinteger_t_dc_kernel_small PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_dc_kernel_small PRIVATE DIV_DC_THRESHOLD=4)
  add_test(NAME uinteger_t_dc_kernel_small COMMAND uinteger_t_dc_kernel_small)
  add_executable(uinteger_t_dc_kernel32 test/dc_kernel.cc)
  target_link_libraries(uinteger_t_dc_kernel32 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_dc_kernel32 PRIVATE DIGIT_T=std::uint32_t HALF_DIGIT_T=std::uint16_t DIV_DC_THRESHOLD=4)
  add_test(NAME uinteger_t_dc_kernel32 COMMAND uinteger_t_dc_kernel32)
  add_executable(uinteger_t_dc_kernel_portable test/dc_kernel.cc)
  target_link_libraries(uinteger_t_dc_kernel_portable PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_dc_kernel_portable PRIVATE UINTEGER_T_NO_INT128 DIV_DC_THRESHOLD=4)
  add_test(NAME uinteger_t_dc_kernel_portable COMMAND uinteger_t_dc_kernel_portable)

  # The same property test with tiny multiplication cutoffs, so Karatsuba and Toom-3 (and the
  # squaring kernels) run at every size the test visits, at 64-bit and 32-bit limbs.
  set(UINTEGER_T_TINY_CUTOFFS UINTEGER_T_MUL_KARATSUBA_THRESHOLD=2 UINTEGER_T_MUL_TOOM3_THRESHOLD=3
      UINTEGER_T_SQR_KARATSUBA_THRESHOLD=2 UINTEGER_T_SQR_TOOM3_THRESHOLD=3)
  add_executable(uinteger_t_property_tiny test/property.cc)
  target_link_libraries(uinteger_t_property_tiny PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_tiny PRIVATE ${UINTEGER_T_TINY_CUTOFFS})
  add_test(NAME uinteger_t_property_tiny COMMAND uinteger_t_property_tiny)
  add_executable(uinteger_t_property_tiny32 test/property.cc)
  target_link_libraries(uinteger_t_property_tiny32 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_tiny32 PRIVATE DIGIT_T=std::uint32_t HALF_DIGIT_T=std::uint16_t ${UINTEGER_T_TINY_CUTOFFS})
  add_test(NAME uinteger_t_property_tiny32 COMMAND uinteger_t_property_tiny32)
  add_executable(uinteger_t_property_mid test/property.cc)
  target_link_libraries(uinteger_t_property_mid PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_mid PRIVATE UINTEGER_T_MUL_KARATSUBA_THRESHOLD=5 UINTEGER_T_MUL_TOOM3_THRESHOLD=9
      UINTEGER_T_SQR_KARATSUBA_THRESHOLD=4 UINTEGER_T_SQR_TOOM3_THRESHOLD=11)
  add_test(NAME uinteger_t_property_mid COMMAND uinteger_t_property_mid)

  # Recursive division multiplies through k_mul, so run it over the Toom-3 and squaring paths too:
  # tiny multiplication cutoffs with a tiny division threshold, at both limb widths.
  add_executable(uinteger_t_property_tiny_dc test/property.cc)
  target_link_libraries(uinteger_t_property_tiny_dc PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_tiny_dc PRIVATE DIV_DC_THRESHOLD=4 ${UINTEGER_T_TINY_CUTOFFS})
  add_test(NAME uinteger_t_property_tiny_dc COMMAND uinteger_t_property_tiny_dc)
  add_executable(uinteger_t_property_tiny_dc32 test/property.cc)
  target_link_libraries(uinteger_t_property_tiny_dc32 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property_tiny_dc32 PRIVATE DIGIT_T=std::uint32_t HALF_DIGIT_T=std::uint16_t DIV_DC_THRESHOLD=4 ${UINTEGER_T_TINY_CUTOFFS})
  add_test(NAME uinteger_t_property_tiny_dc32 COMMAND uinteger_t_property_tiny_dc32)
  add_executable(uinteger_t_dc_kernel_tiny test/dc_kernel.cc)
  target_link_libraries(uinteger_t_dc_kernel_tiny PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_dc_kernel_tiny PRIVATE DIV_DC_THRESHOLD=4 ${UINTEGER_T_TINY_CUTOFFS})
  add_test(NAME uinteger_t_dc_kernel_tiny COMMAND uinteger_t_dc_kernel_tiny)
  add_executable(uinteger_t_dc_kernel_tiny32 test/dc_kernel.cc)
  target_link_libraries(uinteger_t_dc_kernel_tiny32 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_dc_kernel_tiny32 PRIVATE DIGIT_T=std::uint32_t HALF_DIGIT_T=std::uint16_t DIV_DC_THRESHOLD=4 ${UINTEGER_T_TINY_CUTOFFS})
  add_test(NAME uinteger_t_dc_kernel_tiny32 COMMAND uinteger_t_dc_kernel_tiny32)

  # 16-bit limbs: the properties, the recursive-division kernel, the allocation counts, and the
  # reciprocal checked over the whole divisor space (a stride over it by default).
  add_executable(uinteger_t_property16 test/property.cc)
  target_link_libraries(uinteger_t_property16 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_property16 PRIVATE DIGIT_T=std::uint16_t HALF_DIGIT_T=std::uint8_t)
  add_test(NAME uinteger_t_property16 COMMAND uinteger_t_property16)
  add_executable(uinteger_t_dc_kernel16 test/dc_kernel.cc)
  target_link_libraries(uinteger_t_dc_kernel16 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_dc_kernel16 PRIVATE DIGIT_T=std::uint16_t HALF_DIGIT_T=std::uint8_t DIV_DC_THRESHOLD=4)
  add_test(NAME uinteger_t_dc_kernel16 COMMAND uinteger_t_dc_kernel16)
  add_executable(uinteger_t_alloc16 test/alloc.cc)
  target_link_libraries(uinteger_t_alloc16 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_alloc16 PRIVATE DIGIT_T=std::uint16_t HALF_DIGIT_T=std::uint8_t)
  add_test(NAME uinteger_t_alloc16 COMMAND uinteger_t_alloc16)
  add_executable(uinteger_t_recip16 test/recip16.cc)
  target_link_libraries(uinteger_t_recip16 PRIVATE uinteger_t)
  target_compile_definitions(uinteger_t_recip16 PRIVATE DIGIT_T=std::uint16_t HALF_DIGIT_T=std::uint8_t)
  add_test(NAME uinteger_t_recip16 COMMAND uinteger_t_recip16)

  # A runnable tour of the library (not a test): build & run ./build/uinteger_t_demo
  add_executable(uinteger_t_demo examples/demo.cc)
  target_link_libraries(uinteger_t_demo PRIVATE uinteger_t)
endif()
