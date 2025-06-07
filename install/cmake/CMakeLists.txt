# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.14)
project(opentelemetry-cpp-external-dependencies LANGUAGES CXX)

# Added in CMake 3.16. ExternalProject_Add() with GIT_SUBMODULES "" initializes
# no submodules.
if(POLICY CMP0097)
  cmake_policy(SET CMP0097 NEW)
endif()

# Added in CMake 3.19 ExternalProject step targets fully adopt their steps. This
# is required for parallel builds.
if(POLICY CMP0114)
  cmake_policy(SET CMP0114 NEW)
endif()

set(OTELCPP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")

# This file uses CMake's ExternalProject module to fetch, build, and install all
# opentelemetry-cpp dependencies

include(ExternalProject)

include(${OTELCPP_SOURCE_DIR}/cmake/version.cmake)

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

set(CMAKE_OPTIONS
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}"
    "-DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}"
    "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
    "-DCMAKE_CXX_EXTENSIONS=OFF"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")

# ------------------------------------------------------------------------
# ---- zlib ----
ExternalProject_Add(
  zlib
  STEP_TARGETS install
  GIT_REPOSITORY "https://github.com/madler/zlib.git"
  GIT_TAG ${zlib_GIT_TAG}
  GIT_SHALLOW ON
  PREFIX ${CMAKE_BINARY_DIR}/external/zlib
  INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  CMAKE_ARGS "${CMAKE_OPTIONS}" "-DZLIB_BUILD_EXAMPLES=OFF")

# ------------------------------------------------------------------------
# ---- curl ----
ExternalProject_Add(
  curl
  DEPENDS zlib
  STEP_TARGETS build install
  GIT_REPOSITORY "https://github.com/curl/curl.git"
  GIT_TAG ${curl_GIT_TAG}
  GIT_SHALLOW ON
  PREFIX ${CMAKE_BINARY_DIR}/external/curl
  INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  CMAKE_ARGS "${CMAKE_OPTIONS}"
             "-DCURL_USE_LIBPSL=OFF"
             "-DBUILD_CURL_EXE=OFF"
             "-DBUILD_LIBCURL_DOCS=OFF"
             "-DBUILD_MISC_DOCS=OFF"
             "-DENABLE_CURL_MANUAL=OFF"
             "-DBUILD_SHARED_LIBS=ON")

add_dependencies(curl-build zlib-install)
# ------------------------------------------------------------------------
# ---- abseil-cpp ----
ExternalProject_Add(
  abseil
  STEP_TARGETS install
  GIT_REPOSITORY "https://github.com/abseil/abseil-cpp.git"
  GIT_TAG ${abseil_GIT_TAG}
  GIT_SHALLOW ON
  PREFIX ${CMAKE_BINARY_DIR}/external/abseil
  INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  CMAKE_ARGS "${CMAKE_OPTIONS}" "-DABSL_BUILD_TESTING=OFF"
             "-DABSL_ENABLE_INSTALL=ON")

# ------------------------------------------------------------------------
# ---- protobuf ----
ExternalProject_Add(
  protobuf
  STEP_TARGETS build install
  DEPENDS zlib abseil
  GIT_REPOSITORY "https://github.com/protocolbuffers/protobuf.git"
  GIT_TAG ${protobuf_GIT_TAG}
  GIT_SHALLOW ON
  GIT_SUBMODULES ""
  PREFIX ${CMAKE_BINARY_DIR}/external/protobuf
  INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  CMAKE_ARGS "${CMAKE_OPTIONS}"
             "-Dprotobuf_WITH_ZLIB=ON"
             "-Dprotobuf_ZLIB_PROVIDER=package"
             "-Dprotobuf_ABSL_PROVIDER=package"
             "-Dprotobuf_INSTALL=ON"
             "-Dprotobuf_BUILD_TESTS=OFF"
             "-Dprotobuf_BUILD_EXAMPLES=OFF")

add_dependencies(protobuf-build zlib-install abseil-install)

# ------------------------------------------------------------------------
# ---- grpc ----
ExternalProject_Add(
  grpc
  DEPENDS zlib abseil protobuf
  STEP_TARGETS build
  GIT_REPOSITORY "https://github.com/grpc/grpc.git"
  GIT_TAG ${grpc_GIT_TAG}
  GIT_SHALLOW ON
  GIT_SUBMODULES "third_party/re2" "third_party/cares/cares"
                 "third_party/boringssl-with-bazel"
  PREFIX ${CMAKE_BINARY_DIR}/external/grpc
  INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  CMAKE_ARGS "${CMAKE_OPTIONS}"
             "-DgRPC_INSTALL=ON"
             "-DgRPC_BUILD_TESTS=OFF"
             "-DgRPC_BUILD_GRPC_CPP_PLUGIN=ON"
             "-DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF"
             "-DgRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF"
             "-DgRPC_BUILD_GRPC_PHP_PLUGIN=OFF"
             "-DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF"
             "-DgRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF"
             "-DgRPC_BUILD_GRPC_RUBY_PLUGIN=OFF"
             "-DgRPC_BUILD_GRPCPP_OTEL_PLUGIN=OFF"
             "-DRE2_BUILD_TESTING=OFF"
             "-DgRPC_ZLIB_PROVIDER=package"
             "-DgRPC_PROTOBUF_PROVIDER=package"
             "-DgRPC_ABSL_PROVIDER=package")
add_dependencies(grpc-build zlib-install abseil-install protobuf-install)

# ------------------------------------------------------------------------
# ---- benchmark ----
set(benchmark_ARGS
    PREFIX
    ${CMAKE_BINARY_DIR}/external/benchmark
    INSTALL_DIR
    ${CMAKE_INSTALL_PREFIX}
    CMAKE_ARGS
    "${CMAKE_OPTIONS}"
    "-DBENCHMARK_ENABLE_TESTING=OFF"
    "-DBENCHMARK_ENABLE_INSTALL=ON"
    "-DBUILD_SHARED_LIBS=ON")

if(EXISTS "${OTELCPP_SOURCE_DIR}/third_party/benchmark/.git")
  ExternalProject_Add(
    benchmark
    SOURCE_DIR "${OTELCPP_SOURCE_DIR}/third_party/benchmark"
    ${benchmark_ARGS})
else()
  ExternalProject_Add(
    benchmark
    GIT_REPOSITORY "https://github.com/google/benchmark.git"
    GIT_TAG ${benchmark_GIT_TAG}
    GIT_SHALLOW ON
    ${benchmark_ARGS})
endif()

# -----------------------------------------------------------------------
# ---- googletest ----
set(googletest_ARGS
    PREFIX
    ${CMAKE_BINARY_DIR}/external/googletest
    INSTALL_DIR
    ${CMAKE_INSTALL_PREFIX}
    CMAKE_ARGS
    "${CMAKE_OPTIONS}"
    "-DINSTALL_GTEST=ON"
    "-DBUILD_GMOCK=ON"
    "-DBUILD_SHARED_LIBS=ON")

if(EXISTS "${OTELCPP_SOURCE_DIR}/third_party/googletest/.git")
  ExternalProject_Add(
    googletest
    SOURCE_DIR "${OTELCPP_SOURCE_DIR}/third_party/googletest"
    ${googletest_ARGS})
else()
  ExternalProject_Add(
    googletest
    GIT_REPOSITORY "https://github.com/google/googletest.git"
    GIT_TAG ${googletest_GIT_TAG}
    GIT_SHALLOW ON
    ${googletest_ARGS})
endif()

# ----------------------------------------------------------------------
# ---- Microsoft.GSL ----
set(ms-gsl_ARGS
    PREFIX
    ${CMAKE_BINARY_DIR}/external/ms-gsl
    INSTALL_DIR
    ${CMAKE_INSTALL_PREFIX}
    CMAKE_ARGS
    "${CMAKE_OPTIONS}"
    "-DGSL_TEST=OFF")

if(EXISTS "${OTELCPP_SOURCE_DIR}/third_party/ms-gsl/.git")
  ExternalProject_Add(
    ms-gsl
    SOURCE_DIR "${OTELCPP_SOURCE_DIR}/third_party/ms-gsl"
    ${ms-gsl_ARGS})
else()
  ExternalProject_Add(
    ms-gsl
    GIT_REPOSITORY "https://github.com/microsoft/GSL.git"
    GIT_TAG ${ms-gsl_GIT_TAG}
    GIT_SHALLOW ON
    ${ms-gsl_ARGS})
endif()

# ----------------------------------------------------------------------
# ---- nlohmann-json ----
set(nhlohmann-json_ARGS
    PREFIX
    ${CMAKE_BINARY_DIR}/external/nlohmann-json
    INSTALL_DIR
    ${CMAKE_INSTALL_PREFIX}
    CMAKE_ARGS
    "${CMAKE_OPTIONS}"
    "-DJSON_BuildTests=OFF"
    "-DJSON_Install=ON"
    "-DJSON_MultipleHeaders=OFF")

if(EXISTS "${OTELCPP_SOURCE_DIR}/third_party/nlohmann-json/.git")
  ExternalProject_Add(
    nlohmann-json
    SOURCE_DIR "${OTELCPP_SOURCE_DIR}/third_party/nlohmann-json"
    ${nhlohmann-json_ARGS})
else()
  ExternalProject_Add(
    nlohmann-json
    GIT_REPOSITORY "https://github.com/nlohmann/json.git"
    GIT_TAG ${nhlohmann-json_GIT_TAG}
    GIT_SHALLOW ON
    ${nhlohmann-json_ARGS})
endif()

# ----------------------------------------------------------------------
# ---- opentelemetry-proto ----
set(opentelemetry-proto_ARGS PREFIX
                             ${CMAKE_BINARY_DIR}/external/opentelemetry-proto)

if(EXISTS "${OTELCPP_SOURCE_DIR}/third_party/opentelemetry-proto/.git")
  ExternalProject_Add(
    opentelemetry-proto
    SOURCE_DIR "${OTELCPP_SOURCE_DIR}/third_party/opentelemetry-proto"
    ${opentelemetry-proto_ARGS}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND "")
else()
  ExternalProject_Add(
    opentelemetry-proto
    GIT_REPOSITORY "https://github.com/open-telemetry/opentelemetry-proto.git"
    GIT_TAG ${opentelemetry-proto_GIT_TAG}
    GIT_SHALLOW ON
    ${opentelemetry-proto_ARGS}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND "")
endif()

# ----------------------------------------------------------------------
# ---- opentracing-cpp ----

set(opentracing-cpp_ARGS
    PREFIX
    ${CMAKE_BINARY_DIR}/external/opentracing-cpp
    INSTALL_DIR
    ${CMAKE_INSTALL_PREFIX}
    CMAKE_ARGS
    "${CMAKE_OPTIONS}"
    "-DBUILD_TESTING=OFF")

if(EXISTS "${OTELCPP_SOURCE_DIR}/third_party/opentracing-cpp/.git")
  ExternalProject_Add(
    opentracing-cpp
    SOURCE_DIR "${OTELCPP_SOURCE_DIR}/third_party/opentracing-cpp"
    ${opentracing-cpp_ARGS})
else()
  ExternalProject_Add(
    opentracing-cpp
    GIT_REPOSITORY "https://github.com/opentracing/opentracing-cpp.git"
    GIT_TAG ${opentracing-cpp_GIT_TAG}
    GIT_SHALLOW ON
    ${opentracing-cpp_ARGS})
endif()

# ----------------------------------------------------------------------
# ---- prometheus-cpp ----
set(prometheus-cpp_ARGS
    PREFIX
    ${CMAKE_BINARY_DIR}/external/prometheus-cpp
    INSTALL_DIR
    ${CMAKE_INSTALL_PREFIX}
    STEP_TARGETS
    build
    DEPENDS
    zlib
    curl
    CMAKE_ARGS
    "${CMAKE_OPTIONS}"
    "-DENABLE_TESTING=OFF"
    "-DENABLE_PUSH=ON"
    "-DENABLE_PULL=ON")

if(EXISTS "${OTELCPP_SOURCE_DIR}/third_party/prometheus-cpp/.git")
  ExternalProject_Add(
    prometheus-cpp
    SOURCE_DIR "${OTELCPP_SOURCE_DIR}/third_party/prometheus-cpp"
    ${prometheus-cpp_ARGS})
else()
  ExternalProject_Add(
    prometheus-cpp
    GIT_REPOSITORY "https://github.com/jupp0r/prometheus-cpp.git"
    GIT_TAG ${prometheus-cpp_GIT_TAG}
    GIT_SHALLOW ON
    GIT_SUBMODULES "3rdparty/civetweb" ${prometheus-cpp_ARGS})
endif()

add_dependencies(prometheus-cpp-build zlib-install curl-install)
