# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

add_thirdparty_package(
  PACKAGE_NAME prometheus-cpp
  SEARCH_MODES "CONFIG"
  GIT_REPOSITORY "https://github.com/jupp0r/prometheus-cpp.git"
  GIT_TAG "${prometheus-cpp}"
  SUBMODULE_DIR "${PROJECT_SOURCE_DIR}/third_party/prometheus-cpp"
  REQUIRED_TARGETS "prometheus-cpp::core;prometheus-cpp::push;prometheus-cpp::util;prometheus-cpp::pull"
  VERSION_REGEX "project\\([^\\)]*VERSION[ \t]*([0-9]+(\\.[0-9]+)*(\\.[0-9]+)*)"
  CMAKE_ARGS "ENABLE_TESTING=OFF"
)