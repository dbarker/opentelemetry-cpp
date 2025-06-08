# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

# ZLIB must be found as an installed package for now.
# Fetching ZLIB and building in-tree is not supported.
# Protobuf, gRPC, prometheus-cpp, civetweb, CURL, and other dependencies require ZLIB and import its target.
# When ZLIB::ZLIB is an alias of the shared library then inconsistent linking may occur.

find_package(ZLIB REQUIRED)

otel_add_thirdparty_package(
  PACKAGE_NAME "ZLIB"
  )
