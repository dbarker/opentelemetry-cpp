# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0


# Protobuf 3.22+ depends on abseil-cpp and must be found using the cmake
# find_package CONFIG search mode. The following attempts to find Protobuf
# using the CONFIG mode first, and if not found, falls back to the MODULE
# mode. See https://gitlab.kitware.com/cmake/cmake/-/issues/24321 for more
# details.

# if(DEFINED gRPC_PROVIDER AND NOT gRPC_PROVIDER STREQUAL "package" AND NOT TARGET "protobuf::libprotobuf")
  add_thirdparty_package(
    PACKAGE_NAME Protobuf
    FETCH_NAME protobuf
    SEARCH_MODES "CONFIG" "MODULE"
    GIT_REPOSITORY "https://github.com/protocolbuffers/protobuf.git"
    GIT_TAG "${Protobuf}"
    VERSION_REGEX "\"cpp\"[ \t]*:[ \t]*\"([^\"]+)\""
    VERSION_FILE "\${protobuf_SOURCE_DIR}/version.json"
    CMAKE_ARGS
      CMAKE_POSITION_INDEPENDENT_CODE=ON
      protobuf_BUILD_TESTS=OFF
      protobuf_BUILD_EXAMPLES=OFF 
  )
# endif()

if(WIN32)
  # Always use x64 protoc.exe
  if(NOT EXISTS "${Protobuf_PROTOC_EXECUTABLE}")
     set(Protobuf_PROTOC_EXECUTABLE
         ${PROJECT_SOURCE_DIR}/tools/vcpkg/packages/protobuf_x64-windows/tools/protobuf/protoc.exe
      )
   endif()
endif()

# Latest Protobuf imported targets and without legacy module support
if(TARGET protobuf::protoc)
  if(CMAKE_CROSSCOMPILING AND Protobuf_PROTOC_EXECUTABLE)
    set(PROTOBUF_PROTOC_EXECUTABLE ${Protobuf_PROTOC_EXECUTABLE})
  else()
    project_build_tools_get_imported_location(PROTOBUF_PROTOC_EXECUTABLE
                                              protobuf::protoc)
    # If protobuf::protoc is not a imported target, then we use the target
    # directly for fallback
    if(NOT PROTOBUF_PROTOC_EXECUTABLE)
      set(PROTOBUF_PROTOC_EXECUTABLE protobuf::protoc)
    endif()
  endif()
elseif(Protobuf_PROTOC_EXECUTABLE)
  # Some versions of FindProtobuf.cmake uses mixed case instead of uppercase
  set(PROTOBUF_PROTOC_EXECUTABLE ${Protobuf_PROTOC_EXECUTABLE})
endif()

if( NOT TARGET protobuf::libprotobuf)
  message(FATAL_ERROR "A required protobuf target (protobuf::libprotobuf) was not found")
endif() 

message(STATUS "PROTOBUF_PROTOC_EXECUTABLE=${PROTOBUF_PROTOC_EXECUTABLE}")