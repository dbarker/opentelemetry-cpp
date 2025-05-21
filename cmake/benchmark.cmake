# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

add_thirdparty_package(
  PACKAGE_NAME benchmark
  SEARCH_MODES "CONFIG"
  GIT_REPOSITORY "https://github.com/google/benchmark.git"
  GIT_TAG ${benchmark} 
  SUBMODULE_DIR "${PROJECT_SOURCE_DIR}/third_party/benchmark"
  REQUIRED_TARGETS "benchmark::benchmark"
  VERSION_REGEX "project\\([^\\)]*VERSION[ \t]*([0-9]+(\\.[0-9]+)*(\\.[0-9]+)*)"
  CMAKE_ARGS 
    BENCHMARK_ENABLE_TESTING=OFF
)
