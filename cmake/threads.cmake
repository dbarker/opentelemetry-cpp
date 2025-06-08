# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

find_package(Threads REQUIRED)

otel_add_thirdparty_package(
  PACKAGE_NAME "Threads")

# Threads does not have a version, so we set it to an empty string instead of "unknown".
set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY OTEL_Threads_VERSION "")
