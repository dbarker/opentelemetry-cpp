// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <iostream>
#include <string>

#include "opentelemetry/exporters/otlp/otlp_file_builder_utils.h"
#include "opentelemetry/exporters/otlp/otlp_file_client_options.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

OtlpFileClientBackendOptions OtlpFileBuilderUtils::ConvertOutputStream(
    const std::string &output_stream)
{
  static constexpr char kFileScheme[]         = "file://";
  static constexpr std::size_t kFileSchemeLen = sizeof(kFileScheme) - 1;

  OTEL_INTERNAL_LOG_DEBUG("[Otlp File Exporter] output_stream: " << output_stream);

  if (output_stream.compare(0, kFileSchemeLen, kFileScheme) == 0)
  {
    std::string file_pattern = output_stream.substr(kFileSchemeLen);

    if (file_pattern.empty())
    {
      OTEL_INTERNAL_LOG_WARN("[Otlp File Exporter] output_stream '"
                             << output_stream << "' has no file path, using stdout");
      return std::ref(std::cout);
    }

    OtlpFileClientFileSystemOptions fs_options;
    fs_options.file_pattern = file_pattern;
    return fs_options;
  }

  if (!output_stream.empty() && output_stream != "stdout")
  {
    OTEL_INTERNAL_LOG_WARN("[Otlp File Exporter] unknown output_stream '" << output_stream
                                                                          << "', using stdout");
  }

  return std::ref(std::cout);
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
