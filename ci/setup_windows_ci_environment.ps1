# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

$ErrorActionPreference = "Stop"
trap { $host.SetShouldExit(1) }

git submodule update -f "tools/vcpkg"
Push-Location -Path "tools/vcpkg"
$VCPKG_DIR = (Get-Item -Path ".\").FullName

$TripletsDir = Join-Path $VCPKG_DIR "..\triplets"
$TripletOptions = "--overlay-triplets=$TripletsDir --triplet=x64-windows-debug-only"

./bootstrap-vcpkg.bat
./vcpkg integrate install

# Google Benchmark
./vcpkg "--vcpkg-root=$VCPKG_DIR" install benchmark:x64-windows $TripletOptions

# Google Test
./vcpkg "--vcpkg-root=$VCPKG_DIR" install gtest:x64-windows $TripletOptions

# nlohmann-json
./vcpkg "--vcpkg-root=$VCPKG_DIR" install nlohmann-json:x64-windows $TripletOptions

# grpc with custom CMake arguments
./vcpkg "--vcpkg-root=$VCPKG_DIR" install grpc:x64-windows $TripletOptions `
  --x-cmake-args=" -DgRPC_INSTALL=ON `
    -DgRPC_BUILD_TESTS=OFF `
    -DgRPC_BUILD_GRPC_CPP_PLUGIN=ON `
    -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF `
    -DgRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF `
    -DgRPC_BUILD_GRPC_PHP_PLUGIN=OFF `
    -DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF `
    -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF `
    -DgRPC_BUILD_GRPC_RUBY_PLUGIN=OFF `
    -DgRPC_BUILD_GRPCPP_OTEL_PLUGIN=OFF"

# curl
./vcpkg "--vcpkg-root=$VCPKG_DIR" install curl:x64-windows $TripletOptions

# prometheus-cpp
./vcpkg "--vcpkg-root=$VCPKG_DIR" install prometheus-cpp:x64-windows $TripletOptions

Pop-Location
