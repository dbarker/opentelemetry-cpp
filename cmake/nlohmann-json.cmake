# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

add_thirdparty_package(
  PACKAGE_NAME nlohmann_json
  SEARCH_MODES "CONFIG"
  GIT_REPOSITORY "https://github.com/nlohmann/json.git"
  GIT_TAG ${nlohmann-json_GIT_TAG}
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/nlohmann-json"
  REQUIRED_TARGETS "nlohmann_json::nlohmann_json"
  VERSION_REGEX "project\\([^\\)]*VERSION[ \t]*([0-9]+(\\.[0-9]+)*(\\.[0-9]+)*)"
  VERSION_FILE "\${nlohmann_json_SOURCE_DIR}/CMakeLists.txt"
  CMAKE_ARGS
    JSON_BuildTests=OFF
    JSON_Install=${OPENTELEMETRY_INSTALL}
)