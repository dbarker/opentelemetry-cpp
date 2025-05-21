# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

add_thirdparty_package(
  PACKAGE_NAME prometheus-cpp
  SEARCH_MODES "CONFIG"
  GIT_REPOSITORY "https://github.com/jupp0r/prometheus-cpp.git"
  GIT_TAG "${prometheus-cpp}"
  SUBMODULE_DIR "${PROJECT_SOURCE_DIR}/third_party/prometheus-cpp"
  REQUIRED_TARGETS "core;push"
  VERSION_REGEX "project\\([^\\)]*VERSION[ \t]*([0-9]+(\\.[0-9]+)*(\\.[0-9]+)*)"
  VERSION_FILE "\${prometheus-cpp_SOURCE_DIR}/CMakeLists.txt"
  CMAKE_ARGS "ENABLE_TESTING=OFF"
)

if(TARGET core AND NOT TARGET prometheus-cpp::core)
  add_library(prometheus-cpp::core ALIAS core)
endif()

if(TARGET push AND NOT TARGET prometheus-cpp::push)
  add_library(prometheus-cpp::push ALIAS push)
endif()

if(NOT TARGET prometheus-cpp::core OR NOT TARGET prometheus-cpp::push)
  message(FATAL_ERROR "A required prometheus-cpp target (core or push) was not found")
endif()