# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

file(READ "${CMAKE_CURRENT_LIST_DIR}/../api/include/opentelemetry/version.h"
     OPENTELEMETRY_CPP_HEADER_VERSION_H)

if(NOT DEFINED OPENTELEMETRY_CPP_HEADER_VERSION_H)
      message(FATAL_ERROR "OPENTELEMETRY_CPP_HEADER_VERSION_H not found")
endif()

if(OPENTELEMETRY_CPP_HEADER_VERSION_H MATCHES
   "OPENTELEMETRY_VERSION[ \t\r\n]+\"?([^\"]+)\"?")
  set(OPENTELEMETRY_VERSION ${CMAKE_MATCH_1})
else()
  message(
    FATAL_ERROR
      "OPENTELEMETRY_VERSION not found on ${CMAKE_CURRENT_LIST_DIR}/../api/include/opentelemetry/version.h"
 )
endif()

if(OPENTELEMETRY_CPP_HEADER_VERSION_H MATCHES "OPENTELEMETRY_ABI_VERSION_NO[ \t\r\n]+\"?([0-9]+)\"?")
   math(EXPR OPENTELEMETRY_ABI_VERSION_DEFAULT ${CMAKE_MATCH_1})
endif()

if(NOT OPENTELEMETRY_VERSION)
  message(FATAL_ERROR "Failed to extract OpenTelemetry C++ version from version.h")
endif()

if(NOT OPENTELEMETRY_ABI_VERSION_DEFAULT)
  message(FATAL_ERROR "Failed to extract OpenTelemetry C++ ABI version from version.h")
endif()

message(STATUS "OpenTelemetry C++ version: ${OPENTELEMETRY_VERSION}")

# Allow overriding the third-party version tags file with -DOTELCPP_THIRDPARTY_FILE=<file>
if(NOT OTELCPP_THIRDPARTY_FILE)
  set(OTELCPP_THIRDPARTY_FILE "${CMAKE_CURRENT_LIST_DIR}/../third_party_release")
else()
  string(PREPEND OTELCPP_THIRDPARTY_FILE "${CMAKE_CURRENT_LIST_DIR}/../")
endif()

if(NOT EXISTS "${OTELCPP_THIRDPARTY_FILE}")
  message(FATAL_ERROR "Third-party version tags file not found: ${OTELCPP_THIRDPARTY_FILE}")
endif()

message(STATUS "Reading third-party version tags from ${OTELCPP_THIRDPARTY_FILE}")

file(STRINGS "${OTELCPP_THIRDPARTY_FILE}" OPENTELEMETRY_CPP_THIRDPARTY_FILE_CONTENT)

# Parse the third-party tags file
foreach(_raw_line IN LISTS OPENTELEMETRY_CPP_THIRDPARTY_FILE_CONTENT)
    # Strip leading/trailing whitespace
    string(STRIP "${_raw_line}" _line)

    # Skip empty lines and comments
    if(_line STREQUAL "")
      continue()
    endif()
    if(_line MATCHES "^#")
      continue()
    endif()

    # Match "package_name=git_tag"
    if(_line MATCHES "^([^=]+)=(.+)$")
      set(_third_party_name   "${CMAKE_MATCH_1}")
      set(_git_tag  "${CMAKE_MATCH_2}")
      set(_third_party_tag_var "${_third_party_name}_GIT_TAG")
      set("${_third_party_tag_var}" "${_git_tag}")
      message(DEBUG "Set third-party tag: ${_third_party_tag_var}=${_git_tag}")
    else()
      message(FATAL_ERROR "Could not parse third-party tag. Invalid line in ${THIRD_PARTY_TAGS_FILE}. Line:\n  ${_raw_line}")
    endif()
endforeach()
