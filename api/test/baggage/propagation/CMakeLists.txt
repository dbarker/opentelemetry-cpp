# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

foreach(testname baggage_propagator_test)
  add_executable(${testname} "${testname}.cc")
  target_link_libraries(${testname} ${GTEST_BOTH_LIBRARIES}
                        ${CMAKE_THREAD_LIBS_INIT} opentelemetry_api)
  gtest_add_tests(
    TARGET ${testname}
    TEST_PREFIX baggage.
    TEST_LIST ${testname})
endforeach()

if(WITH_BENCHMARK)
  add_executable(baggage_propagator_benchmark baggage_propagator_benchmark.cc)
  target_link_libraries(baggage_propagator_benchmark benchmark::benchmark
                        ${CMAKE_THREAD_LIBS_INIT} opentelemetry_api)
endif()
