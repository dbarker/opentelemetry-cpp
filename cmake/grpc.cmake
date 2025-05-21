# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

# Including the CMakeFindDependencyMacro resolves an error from
# gRPCConfig.cmake on some grpc versions. See
# https://github.com/grpc/grpc/pull/33361 for more details.
include(CMakeFindDependencyMacro)

add_thirdparty_package(
  PACKAGE_NAME gRPC
  SEARCH_MODES "CONFIG"
  GIT_REPOSITORY "https://github.com/grpc/grpc.git"
  GIT_TAG "${gRPC}"
  REQUIRED_TARGETS "gRPC::grpc++"
  VERSION_REGEX "set\\s*\\(\\s*PACKAGE_VERSION[ \t]+([0-9]+(\\.[0-9]+(\\.[0-9])*)([ \t]|\\))"
  CMAKE_ARGS
    CMAKE_POSITION_INDEPENDENT_CODE=ON 
    gRPC_BUILD_TESTS=OFF
    gRPC_BUILD_GRPC_CPP_PLUGIN=ON
    gRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF
    gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF
    gRPC_BUILD_GRPC_PHP_PLUGIN=OFF
    gRPC_BUILD_GRPC_NODE_PLUGIN=OFF
    gRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF
    gRPC_BUILD_GRPC_RUBY_PLUGIN=OFF
    gRPC_BUILD_GRPCPP_OTEL_PLUGIN=OFF
)