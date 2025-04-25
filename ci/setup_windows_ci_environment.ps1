# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

$ErrorActionPreference = "Stop"
trap { $host.SetShouldExit(1) }

git submodule update -f "tools/vcpkg"
Push-Location -Path "tools/vcpkg"
$VCPKG_DIR = (Get-Item -Path ".\").FullName

./bootstrap-vcpkg.bat
./vcpkg integrate install

if ($env:CXX_STANDARD) {
    [int]$CXX_STANDARD = [int]$env:CXX_STANDARD
    Write-Host "CXX_STANDARD environment variable set: using $CXX_STANDARD"
} else {
    [int]$CXX_STANDARD = 14
    Write-Host "CXX_STANDARD not set: defaulting to $CXX_STANDARD"
}

[string]$BUILD_TYPE = $null

if ($env:VCPKG_BUILD_TYPE -eq "debug") {
    $BUILD_TYPE = "Debug"
    Write-Host "BUILD_TYPE environment variable set: using $BUILD_TYPE"
} else {
    Write-Host "BUILD_TYPE not set: defaulting to multi-config"
}

# Assemble common CMake options
$CMAKE_OPTIONS = @(
    "-DCMAKE_CXX_STANDARD=$CXX_STANDARD",
    "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
    "-DCMAKE_CXX_EXTENSIONS=OFF"
)

# Add build-type flags if single-config is requested
if ($BUILD_TYPE) {
    $CMAKE_OPTIONS += "-DVCPKG_BUILD_TYPE=$env:VCPKG_BUILD_TYPE"
    $CMAKE_OPTIONS += "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
}

# Display final CMake arguments
Write-Host "CMake arguments to pass:"
foreach ($opt in $CMAKE_OPTIONS) {
    Write-Host "  $opt"
}

# Google Benchmark
./vcpkg "--vcpkg-root=$VCPKG_DIR" install benchmark:x64-windows --x-cmake-args="$CMAKE_OPTIONS"

# Google Test
./vcpkg "--vcpkg-root=$VCPKG_DIR" install gtest:x64-windows --x-cmake-args="$CMAKE_OPTIONS"

# nlohmann-json
./vcpkg "--vcpkg-root=$VCPKG_DIR" install nlohmann-json:x64-windows --x-cmake-args="$CMAKE_OPTIONS"

# grpc with custom CMake arguments
./vcpkg "--vcpkg-root=$VCPKG_DIR" install grpc:x64-windows `
  --x-cmake-args="$CMAKE_OPTIONS `
    -DgRPC_INSTALL=ON `
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
./vcpkg "--vcpkg-root=$VCPKG_DIR" install curl:x64-windows --x-cmake-args="$CMAKE_OPTIONS"

# prometheus-cpp
./vcpkg "--vcpkg-root=$VCPKG_DIR" install prometheus-cpp:x64-windows --x-cmake-args="$CMAKE_OPTIONS"

Pop-Location
