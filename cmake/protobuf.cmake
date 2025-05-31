# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

if(DEFINED gRPC_PROVIDER AND NOT gRPC_PROVIDER STREQUAL "package" AND TARGET libprotobuf)
  # gRPC was fetched and built protobuf as a submodule
  add_thirdparty_package(
    PACKAGE_NAME Protobuf
    VERSION_REGEX "\"cpp\"[ \t]*:[ \t]*\"([0-9]+\\.[0-9]+(\\.[0-9]+)?)\""
    VERSION_FILE "\${grpc_SOURCE_DIR}/third_party/protobuf/version.json"
  )
  message(STATUS "Using Protobuf from gRPC submodule. version file is ${grpc_SOURCE_DIR}/third_party/protobuf/version.json")
  set(Protobuf_PROVIDER "grpc_submodule")
else()
  # Protobuf 3.22+ depends on abseil-cpp and must be found using the cmake
  # find_package CONFIG search mode. The following attempts to find Protobuf
  # using the CONFIG mode first, and if not found, falls back to the MODULE
  # mode. See https://gitlab.kitware.com/cmake/cmake/-/issues/24321 for more
  # details.

  add_thirdparty_package(
    PACKAGE_NAME Protobuf
    FETCH_NAME protobuf
    SEARCH_MODES "CONFIG" "MODULE"
    GIT_REPOSITORY "https://github.com/protocolbuffers/protobuf.git"
    GIT_TAG ${Protobuf_GIT_TAG}
    VERSION_REGEX "\"cpp\"[ \t]*:[ \t]*\"([0-9]+\\.[0-9]+(\\.[0-9]+)?)\""
    VERSION_FILE "\${protobuf_SOURCE_DIR}/version.json"
    CMAKE_ARGS
      CMAKE_POSITION_INDEPENDENT_CODE=ON
      protobuf_BUILD_TESTS=OFF
      protobuf_BUILD_EXAMPLES=OFF
  )
endif()

if(NOT TARGET protobuf::libprotobuf)
  message(FATAL_ERROR "A required protobuf target (protobuf::libprotobuf) was not found")
endif()

if(CMAKE_CROSSCOMPILING)
  find_program(PROTOBUF_PROTOC_EXECUTABLE protoc)
else()
  if(NOT TARGET protobuf::protoc)
    message(FATAL_ERROR "A required protobuf target (protobuf::protoc) was not found")
  endif()
  set(PROTOBUF_PROTOC_EXECUTABLE protobuf::protoc)
endif()

message(STATUS "PROTOBUF_PROTOC_EXECUTABLE=${PROTOBUF_PROTOC_EXECUTABLE}")