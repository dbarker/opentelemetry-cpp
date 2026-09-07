// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "opentelemetry/exporters/otlp/otlp_file_client_options.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

class OtlpFileBuilderUtils
{
public:
  /**
   * Convert a declarative configuration output_stream value to backend options.
   * Accepted values: "stdout", "file://<path>". Empty or unknown values map to stdout.
   */
  static OtlpFileClientBackendOptions ConvertOutputStream(const std::string &output_stream);
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
