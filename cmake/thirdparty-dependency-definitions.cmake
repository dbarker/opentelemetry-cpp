# Copyright The OpenTelemetry Authors
# SPDX-License-Identifier: Apache-2.0

#-----------------------------------------------------------------------
# Third party dependencies supported by opentelemetry-cpp
#    Dependencies will be found in this order when find_package(opentelemetry-cpp ...)  is called.
#-----------------------------------------------------------------------
set(THIRD_PARTY_DEPENDENCIES_SUPPORTED
     absl
     Threads
     ZLIB
     CURL
     nlohmann_json
     Protobuf
     gRPC
     prometheus-cpp
     OpenTracing
)

#-----------------------------------------------------------------------
# Flags to determine if CONFIG search mode should be used in find_dependency(...)
#-----------------------------------------------------------------------

# absl
set(FIND_DEPENDENCY_absl_USE_CONFIG TRUE)
# Threads
set(FIND_DEPENDENCY_Threads_USE_CONFIG FALSE)
# ZLIB
set(FIND_DEPENDENCY_ZLIB_USE_CONFIG FALSE)
# CURL
set(FIND_DEPENDENCY_CURL_USE_CONFIG FALSE)
# nlohmann_json
set(FIND_DEPENDENCY_nlohmann_json_USE_CONFIG TRUE)
# Protobuf
set(FIND_DEPENDENCY_Protobuf_USE_CONFIG TRUE)
# gRPC
set(FIND_DEPENDENCY_gRPC_USE_CONFIG TRUE)
# prometheus-cpp
set(FIND_DEPENDENCY_prometheus-cpp_USE_CONFIG TRUE)
# OpenTracing
set(FIND_DEPENDENCY_OpenTracing_USE_CONFIG TRUE)

#-----------------------------------------------------------------------
# THIRD_PARTY to COMPONENT dependencies
#   These are the components that may require the third party dependency
#-----------------------------------------------------------------------

# Components that may require absl
set(THIRD_PARTY_absl_DEPENDENT_COMPONENTS
    api
    sdk
    exporters_otlp_common
    exporters_otlp_grpc
)

# Components that require Threads
set(THIRD_PARTY_Threads_DEPENDENT_COMPONENTS
    sdk
)

# Components that may require ZLIB
set(THIRD_PARTY_ZLIB_DEPENDENT_COMPONENTS
    ext_http_curl
)

# Components that may require CURL
set(THIRD_PARTY_CURL_DEPENDENT_COMPONENTS
    ext_http_curl
)

# Components that require nlohmann_json
set(THIRD_PARTY_nlohmann_json_DEPENDENT_COMPONENTS
    exporters_zipkin
    exporters_elasticsearch
    exporters_etw
)

# Components that require Protobuf
set(THIRD_PARTY_Protobuf_DEPENDENT_COMPONENTS
    exporters_otlp_common
    exporters_otlp_file
    exporters_otlp_http
    exporters_otlp_grpc
)

# Components that require gRPC
set(THIRD_PARTY_gRPC_DEPENDENT_COMPONENTS
    exporters_otlp_grpc
)

# Components that require prometheus-cpp
set(THIRD_PARTY_prometheus-cpp_DEPENDENT_COMPONENTS
    exporters_prometheus
)

# Components that require OpenTracing
set(THIRD_PARTY_OpenTracing_DEPENDENT_COMPONENTS
    shims_opentracing
)
