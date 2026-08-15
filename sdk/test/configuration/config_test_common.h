// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Shared test helpers for the SDK configuration tests.
//
// Noop: Noop{Span,LogRecord,PushMetric}Exporter, NoopMetricReader,
//       NoopConsole{Span,LogRecord,PushMetric}ExporterBuilder,
//       Noop{Span,LogRecord,PushMetric}ExporterBuilder (extension),
//       NoopPullMetricExporterBuilder, NoopPeriodicMetricReaderBuilder
//
// Recording: Recording{Span,LogRecord,PushMetric}Exporter,
//            Recording{Span,LogRecord,PushMetric}ExporterBuilder (extension),
//            RecordingConsole{Span,LogRecord,PushMetric}ExporterBuilder,
//            RecordingOtlp{Http,Grpc,File}{Span,LogRecord,PushMetric}ExporterBuilder,
//            RecordingLogRecordProcessorBuilder,
//            SyncMetricReader, SyncPullMetricReader, SyncPeriodicMetricReaderBuilder,
//            CapturedPeriodicReaderArgs, CapturingPeriodicMetricReaderBuilder,
//            RecordingPrometheusPullMetricExporterBuilder
//
// Mock processors: MockBatchSpanProcessorBuilder, MockBatchLogRecordProcessorBuilder
//
// Noop provider builders (return minimal SDK providers):
//       Noop{Tracer,Logger,Meter}ProviderBuilder
//
// Propagator: MapCarrier

#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/logs/noop.h"
#include "opentelemetry/metrics/noop.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/configuration/batch_log_record_processor_builder.h"
#include "opentelemetry/sdk/configuration/batch_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/batch_span_processor_builder.h"
#include "opentelemetry/sdk/configuration/batch_span_processor_configuration.h"
#include "opentelemetry/sdk/configuration/console_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/console_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/console_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_log_record_processor_builder.h"
#include "opentelemetry/sdk/configuration/extension_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/extension_pull_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_pull_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/logger_provider_builder.h"
#include "opentelemetry/sdk/configuration/logger_provider_builder_context.h"
#include "opentelemetry/sdk/configuration/meter_provider_builder.h"
#include "opentelemetry/sdk/configuration/meter_provider_builder_context.h"
#include "opentelemetry/sdk/configuration/otlp_file_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_file_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_file_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_file_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_file_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_file_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_grpc_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_grpc_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_grpc_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_grpc_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_grpc_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_grpc_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_http_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_http_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_http_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_http_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_http_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_http_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/periodic_metric_reader_builder.h"
#include "opentelemetry/sdk/configuration/periodic_metric_reader_configuration.h"
#include "opentelemetry/sdk/configuration/prometheus_pull_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/prometheus_pull_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/simple_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/simple_span_processor_configuration.h"
#include "opentelemetry/sdk/configuration/tracer_provider_builder.h"
#include "opentelemetry/sdk/configuration/tracer_provider_builder_context.h"
#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/read_write_log_record.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"
#include "opentelemetry/sdk/metrics/push_metric_exporter.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/noop.h"

namespace config_test
{

// ---------------------------------------------------------------------------
// Export buffer type aliases

using SpanBuffer      = std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanData>>;
using LogRecordBuffer = std::vector<std::unique_ptr<opentelemetry::sdk::logs::ReadWriteLogRecord>>;
using MetricBuffer    = std::vector<opentelemetry::sdk::metrics::MetricData>;

// ---------------------------------------------------------------------------
// No-op exporters

class NoopSpanExporter : public opentelemetry::sdk::trace::SpanExporter
{
public:
  std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override
  {
    return std::make_unique<opentelemetry::sdk::trace::SpanData>();
  }
  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
          &) noexcept override
  {
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }
  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }
};

class NoopLogRecordExporter : public opentelemetry::sdk::logs::LogRecordExporter
{
public:
  std::unique_ptr<opentelemetry::sdk::logs::Recordable> MakeRecordable() noexcept override
  {
    return std::make_unique<opentelemetry::sdk::logs::ReadWriteLogRecord>();
  }
  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>>
          &) noexcept override
  {
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }
  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }
};

class NoopPushMetricExporter : public opentelemetry::sdk::metrics::PushMetricExporter
{
public:
  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::sdk::metrics::ResourceMetrics &) noexcept override
  {
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }
  opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      opentelemetry::sdk::metrics::InstrumentType) const noexcept override
  {
    return opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;
  }
  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }
};

// ---------------------------------------------------------------------------
// No-op extension builders

class NoopConsoleSpanExporterBuilder
    : public opentelemetry::sdk::configuration::ConsoleSpanExporterBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> Build(
      const opentelemetry::sdk::configuration::ConsoleSpanExporterConfiguration *) const override
  {
    return std::make_unique<NoopSpanExporter>();
  }
};

class NoopConsoleLogRecordExporterBuilder
    : public opentelemetry::sdk::configuration::ConsoleLogRecordExporterBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Build(
      const opentelemetry::sdk::configuration::ConsoleLogRecordExporterConfiguration *)
      const override
  {
    return std::make_unique<NoopLogRecordExporter>();
  }
};

class NoopConsolePushMetricExporterBuilder
    : public opentelemetry::sdk::configuration::ConsolePushMetricExporterBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> Build(
      const opentelemetry::sdk::configuration::ConsolePushMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<NoopPushMetricExporter>();
  }
};

class NoopSpanExporterBuilder
    : public opentelemetry::sdk::configuration::ExtensionSpanExporterBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> Build(
      const opentelemetry::sdk::configuration::ExtensionSpanExporterConfiguration *) const override
  {
    return std::make_unique<NoopSpanExporter>();
  }
};

class NoopLogRecordExporterBuilder
    : public opentelemetry::sdk::configuration::ExtensionLogRecordExporterBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Build(
      const opentelemetry::sdk::configuration::ExtensionLogRecordExporterConfiguration *)
      const override
  {
    return std::make_unique<NoopLogRecordExporter>();
  }
};

class NoopPushMetricExporterBuilder
    : public opentelemetry::sdk::configuration::ExtensionPushMetricExporterBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> Build(
      const opentelemetry::sdk::configuration::ExtensionPushMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<NoopPushMetricExporter>();
  }
};

// ---------------------------------------------------------------------------
// No-op metric reader

class NoopMetricReader : public opentelemetry::sdk::metrics::MetricReader
{
public:
  opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      opentelemetry::sdk::metrics::InstrumentType) const noexcept override
  {
    return opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;
  }

private:
  bool OnForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool OnShutDown(std::chrono::microseconds) noexcept override { return true; }
};

class NoopPullMetricExporterBuilder
    : public opentelemetry::sdk::configuration::ExtensionPullMetricExporterBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::metrics::MetricReader> Build(
      const opentelemetry::sdk::configuration::ExtensionPullMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<NoopMetricReader>();
  }
};

class NoopPeriodicMetricReaderBuilder
    : public opentelemetry::sdk::configuration::PeriodicMetricReaderBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::metrics::MetricReader> Build(
      const opentelemetry::sdk::configuration::PeriodicMetricReaderConfiguration *,
      std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> &&exporter) const override
  {
    auto unused = std::move(exporter);
    return std::make_unique<NoopMetricReader>();
  }
};

// ---------------------------------------------------------------------------
// Recording exporters: capture exported data into shared buffers for
// inspection in integration tests.

class RecordingSpanExporter : public opentelemetry::sdk::trace::SpanExporter
{
public:
  explicit RecordingSpanExporter(std::shared_ptr<SpanBuffer> buffer) : buffer_(std::move(buffer)) {}

  std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override
  {
    return std::make_unique<opentelemetry::sdk::trace::SpanData>();
  }

  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
          &spans) noexcept override
  {
    for (auto &span : spans)
    {
      buffer_->emplace_back(static_cast<opentelemetry::sdk::trace::SpanData *>(span.release()));
    }
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }

  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }

private:
  std::shared_ptr<SpanBuffer> buffer_;
};

class RecordingLogRecordExporter : public opentelemetry::sdk::logs::LogRecordExporter
{
public:
  explicit RecordingLogRecordExporter(std::shared_ptr<LogRecordBuffer> buffer)
      : buffer_(std::move(buffer))
  {}

  std::unique_ptr<opentelemetry::sdk::logs::Recordable> MakeRecordable() noexcept override
  {
    return std::make_unique<opentelemetry::sdk::logs::ReadWriteLogRecord>();
  }

  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>>
          &records) noexcept override
  {
    for (auto &rec : records)
    {
      buffer_->emplace_back(
          static_cast<opentelemetry::sdk::logs::ReadWriteLogRecord *>(rec.release()));
    }
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }

  bool RecordableEnforcesLogRecordLimits() const noexcept override { return true; }
  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }

private:
  std::shared_ptr<LogRecordBuffer> buffer_;
};

class RecordingPushMetricExporter : public opentelemetry::sdk::metrics::PushMetricExporter
{
public:
  explicit RecordingPushMetricExporter(std::shared_ptr<MetricBuffer> buffer)
      : buffer_(std::move(buffer))
  {}

  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::sdk::metrics::ResourceMetrics &resource_metrics) noexcept override
  {
    for (const auto &scope : resource_metrics.scope_metric_data_)
    {
      for (const auto &metric : scope.metric_data_)
      {
        buffer_->emplace_back(metric);
      }
    }
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }

  opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      opentelemetry::sdk::metrics::InstrumentType) const noexcept override
  {
    return opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;
  }

  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }

private:
  std::shared_ptr<MetricBuffer> buffer_;
};

// ---------------------------------------------------------------------------
// Recording extension builders

class RecordingSpanExporterBuilder
    : public opentelemetry::sdk::configuration::ExtensionSpanExporterBuilder
{
public:
  explicit RecordingSpanExporterBuilder(std::shared_ptr<SpanBuffer> buffer)
      : buffer_(std::move(buffer))
  {}
  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> Build(
      const opentelemetry::sdk::configuration::ExtensionSpanExporterConfiguration *) const override
  {
    return std::make_unique<RecordingSpanExporter>(buffer_);
  }

private:
  std::shared_ptr<SpanBuffer> buffer_;
};

class RecordingLogRecordExporterBuilder
    : public opentelemetry::sdk::configuration::ExtensionLogRecordExporterBuilder
{
public:
  explicit RecordingLogRecordExporterBuilder(std::shared_ptr<LogRecordBuffer> buffer)
      : buffer_(std::move(buffer))
  {}
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Build(
      const opentelemetry::sdk::configuration::ExtensionLogRecordExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingLogRecordExporter>(buffer_);
  }

private:
  std::shared_ptr<LogRecordBuffer> buffer_;
};

class RecordingPushMetricExporterBuilder
    : public opentelemetry::sdk::configuration::ExtensionPushMetricExporterBuilder
{
public:
  explicit RecordingPushMetricExporterBuilder(std::shared_ptr<MetricBuffer> buffer)
      : buffer_(std::move(buffer))
  {}
  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> Build(
      const opentelemetry::sdk::configuration::ExtensionPushMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingPushMetricExporter>(buffer_);
  }

private:
  std::shared_ptr<MetricBuffer> buffer_;
};

// ---------------------------------------------------------------------------
// Recording console component builders: route console-configured exporters to the
// recording buffers so dispatch can be verified without stdout capture.

class RecordingConsoleSpanExporterBuilder
    : public opentelemetry::sdk::configuration::ConsoleSpanExporterBuilder
{
public:
  explicit RecordingConsoleSpanExporterBuilder(std::shared_ptr<SpanBuffer> buffer)
      : buffer_(std::move(buffer))
  {}
  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> Build(
      const opentelemetry::sdk::configuration::ConsoleSpanExporterConfiguration *) const override
  {
    return std::make_unique<RecordingSpanExporter>(buffer_);
  }

private:
  std::shared_ptr<SpanBuffer> buffer_;
};

class RecordingConsoleLogRecordExporterBuilder
    : public opentelemetry::sdk::configuration::ConsoleLogRecordExporterBuilder
{
public:
  explicit RecordingConsoleLogRecordExporterBuilder(std::shared_ptr<LogRecordBuffer> buffer)
      : buffer_(std::move(buffer))
  {}
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Build(
      const opentelemetry::sdk::configuration::ConsoleLogRecordExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingLogRecordExporter>(buffer_);
  }

private:
  std::shared_ptr<LogRecordBuffer> buffer_;
};

class RecordingConsolePushMetricExporterBuilder
    : public opentelemetry::sdk::configuration::ConsolePushMetricExporterBuilder
{
public:
  explicit RecordingConsolePushMetricExporterBuilder(std::shared_ptr<MetricBuffer> buffer)
      : buffer_(std::move(buffer))
  {}
  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> Build(
      const opentelemetry::sdk::configuration::ConsolePushMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingPushMetricExporter>(buffer_);
  }

private:
  std::shared_ptr<MetricBuffer> buffer_;
};

// ---------------------------------------------------------------------------
// Recording OTLP component builders: route OTLP/Prometheus-configured exporters to
// the recording buffers so dispatch can be verified without real endpoints.

class RecordingOtlpHttpSpanExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpHttpSpanExporterBuilder
{
public:
  explicit RecordingOtlpHttpSpanExporterBuilder(std::shared_ptr<SpanBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> Build(
      const opentelemetry::sdk::configuration::OtlpHttpSpanExporterConfiguration *) const override
  {
    return std::make_unique<RecordingSpanExporter>(buffer_);
  }

private:
  std::shared_ptr<SpanBuffer> buffer_;
};

class RecordingOtlpGrpcSpanExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpGrpcSpanExporterBuilder
{
public:
  explicit RecordingOtlpGrpcSpanExporterBuilder(std::shared_ptr<SpanBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> Build(
      const opentelemetry::sdk::configuration::OtlpGrpcSpanExporterConfiguration *) const override
  {
    return std::make_unique<RecordingSpanExporter>(buffer_);
  }

private:
  std::shared_ptr<SpanBuffer> buffer_;
};

class RecordingOtlpFileSpanExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpFileSpanExporterBuilder
{
public:
  explicit RecordingOtlpFileSpanExporterBuilder(std::shared_ptr<SpanBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> Build(
      const opentelemetry::sdk::configuration::OtlpFileSpanExporterConfiguration *) const override
  {
    return std::make_unique<RecordingSpanExporter>(buffer_);
  }

private:
  std::shared_ptr<SpanBuffer> buffer_;
};

class RecordingOtlpHttpLogRecordExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpHttpLogRecordExporterBuilder
{
public:
  explicit RecordingOtlpHttpLogRecordExporterBuilder(std::shared_ptr<LogRecordBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Build(
      const opentelemetry::sdk::configuration::OtlpHttpLogRecordExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingLogRecordExporter>(buffer_);
  }

private:
  std::shared_ptr<LogRecordBuffer> buffer_;
};

class RecordingOtlpGrpcLogRecordExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpGrpcLogRecordExporterBuilder
{
public:
  explicit RecordingOtlpGrpcLogRecordExporterBuilder(std::shared_ptr<LogRecordBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Build(
      const opentelemetry::sdk::configuration::OtlpGrpcLogRecordExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingLogRecordExporter>(buffer_);
  }

private:
  std::shared_ptr<LogRecordBuffer> buffer_;
};

class RecordingOtlpFileLogRecordExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpFileLogRecordExporterBuilder
{
public:
  explicit RecordingOtlpFileLogRecordExporterBuilder(std::shared_ptr<LogRecordBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Build(
      const opentelemetry::sdk::configuration::OtlpFileLogRecordExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingLogRecordExporter>(buffer_);
  }

private:
  std::shared_ptr<LogRecordBuffer> buffer_;
};

class RecordingLogRecordProcessorBuilder
    : public opentelemetry::sdk::configuration::ExtensionLogRecordProcessorBuilder
{
public:
  mutable bool called{false};
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor> Build(
      const opentelemetry::sdk::configuration::ExtensionLogRecordProcessorConfiguration *)
      const override
  {
    called = true;
    return std::make_unique<opentelemetry::sdk::logs::SimpleLogRecordProcessor>(
        std::make_unique<NoopLogRecordExporter>());
  }
};

class RecordingOtlpHttpPushMetricExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpHttpPushMetricExporterBuilder
{
public:
  explicit RecordingOtlpHttpPushMetricExporterBuilder(std::shared_ptr<MetricBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> Build(
      const opentelemetry::sdk::configuration::OtlpHttpPushMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingPushMetricExporter>(buffer_);
  }

private:
  std::shared_ptr<MetricBuffer> buffer_;
};

class RecordingOtlpGrpcPushMetricExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpGrpcPushMetricExporterBuilder
{
public:
  explicit RecordingOtlpGrpcPushMetricExporterBuilder(std::shared_ptr<MetricBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> Build(
      const opentelemetry::sdk::configuration::OtlpGrpcPushMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingPushMetricExporter>(buffer_);
  }

private:
  std::shared_ptr<MetricBuffer> buffer_;
};

class RecordingOtlpFilePushMetricExporterBuilder
    : public opentelemetry::sdk::configuration::OtlpFilePushMetricExporterBuilder
{
public:
  explicit RecordingOtlpFilePushMetricExporterBuilder(std::shared_ptr<MetricBuffer> b)
      : buffer_(std::move(b))
  {}
  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> Build(
      const opentelemetry::sdk::configuration::OtlpFilePushMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<RecordingPushMetricExporter>(buffer_);
  }

private:
  std::shared_ptr<MetricBuffer> buffer_;
};

class SyncPullMetricReader : public opentelemetry::sdk::metrics::MetricReader
{
public:
  explicit SyncPullMetricReader(std::shared_ptr<MetricBuffer> buffer) : buffer_(std::move(buffer))
  {}
  opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      opentelemetry::sdk::metrics::InstrumentType) const noexcept override
  {
    return opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;
  }

private:
  bool OnForceFlush(std::chrono::microseconds) noexcept override
  {
    return Collect([this](opentelemetry::sdk::metrics::ResourceMetrics &data) {
      for (const auto &scope : data.scope_metric_data_)
        for (const auto &metric : scope.metric_data_)
          buffer_->emplace_back(metric);
      return true;
    });
  }
  bool OnShutDown(std::chrono::microseconds) noexcept override { return true; }

  std::shared_ptr<MetricBuffer> buffer_;
};

class RecordingPrometheusPullMetricExporterBuilder
    : public opentelemetry::sdk::configuration::PrometheusPullMetricExporterBuilder
{
public:
  explicit RecordingPrometheusPullMetricExporterBuilder(std::shared_ptr<MetricBuffer> buffer)
      : buffer_(std::move(buffer))
  {}
  std::unique_ptr<opentelemetry::sdk::metrics::MetricReader> Build(
      const opentelemetry::sdk::configuration::PrometheusPullMetricExporterConfiguration *)
      const override
  {
    return std::make_unique<SyncPullMetricReader>(buffer_);
  }

private:
  std::shared_ptr<MetricBuffer> buffer_;
};

// ---------------------------------------------------------------------------
// Synchronous metric reader: collects and exports on ForceFlush in
// the calling thread.
class SyncMetricReader : public opentelemetry::sdk::metrics::MetricReader
{
public:
  explicit SyncMetricReader(
      std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> exporter)
      : exporter_(std::move(exporter))
  {}

  opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      opentelemetry::sdk::metrics::InstrumentType instrument_type) const noexcept override
  {
    return exporter_->GetAggregationTemporality(instrument_type);
  }

private:
  bool CollectAndExport() noexcept
  {
    const bool success = Collect([this](opentelemetry::sdk::metrics::ResourceMetrics &metric_data) {
      return (exporter_->Export(metric_data) == opentelemetry::sdk::common::ExportResult::kSuccess);
    });
    return success;
  }

  bool OnForceFlush(std::chrono::microseconds timeout) noexcept override
  {
    const bool collect_result = CollectAndExport();
    const bool flush_result   = exporter_->ForceFlush(timeout);
    return collect_result && flush_result;
  }

  bool OnShutDown(std::chrono::microseconds timeout) noexcept override
  {
    return exporter_->Shutdown(timeout);
  }

  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> exporter_;
};

class SyncPeriodicMetricReaderBuilder
    : public opentelemetry::sdk::configuration::PeriodicMetricReaderBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::metrics::MetricReader> Build(
      const opentelemetry::sdk::configuration::PeriodicMetricReaderConfiguration *,
      std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> &&exporter) const override
  {
    return std::make_unique<SyncMetricReader>(std::move(exporter));
  }
};

// ---------------------------------------------------------------------------
// Capturing periodic metric reader builder. Records the configuration
// arguments passed to Build()

struct CapturedPeriodicReaderArgs
{
  std::size_t interval{0};
  std::size_t timeout{0};
  std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> exporter;
  // TODO: add cardinality limits and producers when we support them in the builder
  bool called{false};
};

class CapturingPeriodicMetricReaderBuilder
    : public opentelemetry::sdk::configuration::PeriodicMetricReaderBuilder
{
public:
  explicit CapturingPeriodicMetricReaderBuilder(
      std::shared_ptr<CapturedPeriodicReaderArgs> captured)
      : captured_(std::move(captured))
  {}

  std::unique_ptr<opentelemetry::sdk::metrics::MetricReader> Build(
      const opentelemetry::sdk::configuration::PeriodicMetricReaderConfiguration *model,
      std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter> &&exporter) const override
  {
    captured_->called   = true;
    captured_->interval = model->interval;
    captured_->timeout  = model->timeout;
    captured_->exporter = std::move(exporter);
    return std::make_unique<NoopMetricReader>();
  }

private:
  std::shared_ptr<CapturedPeriodicReaderArgs> captured_;
};

// ---------------------------------------------------------------------------
// Mock batch processor builders: satisfy the batch builder interface but
// create simple (synchronous) processors to avoid background-thread races in tests.

class MockBatchSpanProcessorBuilder
    : public opentelemetry::sdk::configuration::BatchSpanProcessorBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor> Build(
      const opentelemetry::sdk::configuration::BatchSpanProcessorConfiguration *,
      std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> &&exporter) const override
  {
    return std::make_unique<opentelemetry::sdk::trace::SimpleSpanProcessor>(std::move(exporter));
  }
};

class MockBatchLogRecordProcessorBuilder
    : public opentelemetry::sdk::configuration::BatchLogRecordProcessorBuilder
{
public:
  std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor> Build(
      const opentelemetry::sdk::configuration::BatchLogRecordProcessorConfiguration *,
      std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> &&exporter) const override
  {
    return std::make_unique<opentelemetry::sdk::logs::SimpleLogRecordProcessor>(
        std::move(exporter));
  }
};

// ---------------------------------------------------------------------------
// Noop custom provider builders

class NoopTracerProviderBuilder : public opentelemetry::sdk::configuration::TracerProviderBuilder
{
public:
  std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> Build(
      const opentelemetry::sdk::configuration::TracerProviderBuilderContext &,
      const opentelemetry::sdk::configuration::TracerProviderConfiguration *) const override
  {
    called    = true;
    provider_ = opentelemetry::sdk::trace::TracerProviderFactory::Create(
        std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>>{});
    return provider_;
  }
  mutable bool called{false};
  mutable std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> provider_;
};

class NoopLoggerProviderBuilder : public opentelemetry::sdk::configuration::LoggerProviderBuilder
{
public:
  std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> Build(
      const opentelemetry::sdk::configuration::LoggerProviderBuilderContext &,
      const opentelemetry::sdk::configuration::LoggerProviderConfiguration *) const override
  {
    called    = true;
    provider_ = opentelemetry::sdk::logs::LoggerProviderFactory::Create(
        std::vector<std::unique_ptr<opentelemetry::sdk::logs::LogRecordProcessor>>{});
    return provider_;
  }
  mutable bool called{false};
  mutable std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> provider_;
};

class NoopMeterProviderBuilder : public opentelemetry::sdk::configuration::MeterProviderBuilder
{
public:
  std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> Build(
      const opentelemetry::sdk::configuration::MeterProviderBuilderContext &,
      const opentelemetry::sdk::configuration::MeterProviderConfiguration *) const override
  {
    called    = true;
    provider_ = opentelemetry::sdk::metrics::MeterProviderFactory::Create();
    return provider_;
  }
  mutable bool called{false};
  mutable std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> provider_;
};

// ---------------------------------------------------------------------------
// TextMapCarrier for propagator tests.

class MapCarrier : public opentelemetry::context::propagation::TextMapCarrier
{
public:
  opentelemetry::nostd::string_view Get(
      opentelemetry::nostd::string_view key) const noexcept override
  {
    auto it = map_.find(std::string(key));
    return it != map_.end() ? opentelemetry::nostd::string_view(it->second) : "";
  }
  void Set(opentelemetry::nostd::string_view key,
           opentelemetry::nostd::string_view value) noexcept override
  {
    map_[std::string(key)] = std::string(value);
  }

  const std::map<std::string, std::string> &map() const { return map_; }

private:
  std::map<std::string, std::string> map_;
};

}  // namespace config_test
