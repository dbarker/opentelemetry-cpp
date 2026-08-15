// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "opentelemetry/baggage/baggage.h"
#include "opentelemetry/baggage/baggage_context.h"
#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/noop_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/logs/logger.h"
#include "opentelemetry/logs/logger_provider.h"
#include "opentelemetry/logs/noop.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/noop.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/trace/tracer_provider.h"

#include "opentelemetry/sdk/configuration/aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/always_off_sampler_configuration.h"
#include "opentelemetry/sdk/configuration/attribute_limits_configuration.h"
#include "opentelemetry/sdk/configuration/base2_exponential_bucket_histogram_aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/batch_log_record_processor_builder.h"
#include "opentelemetry/sdk/configuration/batch_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/batch_span_processor_builder.h"
#include "opentelemetry/sdk/configuration/batch_span_processor_configuration.h"
#include "opentelemetry/sdk/configuration/configuration.h"
#include "opentelemetry/sdk/configuration/configured_sdk.h"
#include "opentelemetry/sdk/configuration/console_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/console_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/console_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/explicit_bucket_histogram_aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/extension_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/instrument_type.h"
#include "opentelemetry/sdk/configuration/log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/log_record_limits_configuration.h"
#include "opentelemetry/sdk/configuration/log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/logger_config_configuration.h"
#include "opentelemetry/sdk/configuration/logger_configurator_configuration.h"
#include "opentelemetry/sdk/configuration/logger_matcher_and_config_configuration.h"
#include "opentelemetry/sdk/configuration/logger_provider_configuration.h"
#include "opentelemetry/sdk/configuration/meter_config_configuration.h"
#include "opentelemetry/sdk/configuration/meter_configurator_configuration.h"
#include "opentelemetry/sdk/configuration/meter_matcher_and_config_configuration.h"
#include "opentelemetry/sdk/configuration/meter_provider_configuration.h"
#include "opentelemetry/sdk/configuration/metric_reader_configuration.h"
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
#include "opentelemetry/sdk/configuration/parent_based_sampler_configuration.h"
#include "opentelemetry/sdk/configuration/periodic_metric_reader_builder.h"
#include "opentelemetry/sdk/configuration/periodic_metric_reader_configuration.h"
#include "opentelemetry/sdk/configuration/prometheus_pull_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/prometheus_pull_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/propagator_configuration.h"
#include "opentelemetry/sdk/configuration/pull_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/pull_metric_reader_configuration.h"
#include "opentelemetry/sdk/configuration/push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/configuration/registry_factory.h"
#include "opentelemetry/sdk/configuration/resource_configuration.h"
#include "opentelemetry/sdk/configuration/sampler_configuration.h"
#include "opentelemetry/sdk/configuration/severity_number.h"
#include "opentelemetry/sdk/configuration/simple_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/simple_span_processor_configuration.h"
#include "opentelemetry/sdk/configuration/span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/span_limits_configuration.h"
#include "opentelemetry/sdk/configuration/span_processor_configuration.h"
#include "opentelemetry/sdk/configuration/text_map_propagator_builder.h"
#include "opentelemetry/sdk/configuration/tracer_config_configuration.h"
#include "opentelemetry/sdk/configuration/tracer_configurator_configuration.h"
#include "opentelemetry/sdk/configuration/tracer_matcher_and_config_configuration.h"
#include "opentelemetry/sdk/configuration/tracer_provider_configuration.h"
#include "opentelemetry/sdk/configuration/view_configuration.h"
#include "opentelemetry/sdk/configuration/view_selector_configuration.h"
#include "opentelemetry/sdk/configuration/view_stream_configuration.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/read_write_log_record.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"

#include "config_test_common.h"

namespace common      = opentelemetry::common;
namespace nostd       = opentelemetry::nostd;
namespace metrics     = opentelemetry::metrics;
namespace trace       = opentelemetry::trace;
namespace logs        = opentelemetry::logs;
namespace baggage     = opentelemetry::baggage;
namespace propagation = opentelemetry::context::propagation;
namespace context     = opentelemetry::context;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace config_sdk  = opentelemetry::sdk::configuration;

namespace
{

constexpr std::chrono::milliseconds kProcessTimeout{1000};

namespace
{
static std::unique_ptr<config_sdk::Configuration> MakeSpanConfig(
    std::unique_ptr<config_sdk::SpanExporterConfiguration> exporter)
{
  auto processor      = std::make_unique<config_sdk::SimpleSpanProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  auto tracer_config  = std::make_unique<config_sdk::TracerProviderConfiguration>();
  tracer_config->processors.emplace_back(std::move(processor));
  auto model             = std::make_unique<config_sdk::Configuration>();
  model->resource        = std::make_unique<config_sdk::ResourceConfiguration>();
  model->tracer_provider = std::move(tracer_config);
  return model;
}

static std::unique_ptr<config_sdk::Configuration> MakeLogConfig(
    std::unique_ptr<config_sdk::LogRecordExporterConfiguration> exporter)
{
  auto processor      = std::make_unique<config_sdk::SimpleLogRecordProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  auto logger_config  = std::make_unique<config_sdk::LoggerProviderConfiguration>();
  logger_config->processors.emplace_back(std::move(processor));
  auto model             = std::make_unique<config_sdk::Configuration>();
  model->resource        = std::make_unique<config_sdk::ResourceConfiguration>();
  model->logger_provider = std::move(logger_config);
  return model;
}

static std::unique_ptr<config_sdk::Configuration> MakePushMetricConfig(
    std::unique_ptr<config_sdk::PushMetricExporterConfiguration> exporter)
{
  auto reader       = std::make_unique<config_sdk::PeriodicMetricReaderConfiguration>();
  reader->exporter  = std::move(exporter);
  auto meter_config = std::make_unique<config_sdk::MeterProviderConfiguration>();
  meter_config->readers.emplace_back(std::move(reader));
  auto model            = std::make_unique<config_sdk::Configuration>();
  model->resource       = std::make_unique<config_sdk::ResourceConfiguration>();
  model->meter_provider = std::move(meter_config);
  return model;
}

static std::unique_ptr<config_sdk::Configuration> MakePullMetricConfig(
    std::unique_ptr<config_sdk::PullMetricExporterConfiguration> exporter)
{
  auto reader       = std::make_unique<config_sdk::PullMetricReaderConfiguration>();
  reader->exporter  = std::move(exporter);
  auto meter_config = std::make_unique<config_sdk::MeterProviderConfiguration>();
  meter_config->readers.emplace_back(std::move(reader));
  auto model            = std::make_unique<config_sdk::Configuration>();
  model->resource       = std::make_unique<config_sdk::ResourceConfiguration>();
  model->meter_provider = std::move(meter_config);
  return model;
}

static std::unique_ptr<config_sdk::Configuration> MakePropagatorConfig(const std::string &name)
{
  auto model                        = std::make_unique<config_sdk::Configuration>();
  model->resource                   = std::make_unique<config_sdk::ResourceConfiguration>();
  model->propagator                 = std::make_unique<config_sdk::PropagatorConfiguration>();
  model->propagator->composite_list = name;
  return model;
}
}  // namespace

//---------------------------------------------------------------------------
// ProgrammaticConfigTest fixture: This supports integration testing of the configured SDK.
// It registers the recording exporters and maintains buffers for inspection of exported signal
// data.

class ProgrammaticConfigTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    MakeRegistry();

    propagation::GlobalTextMapPropagator::SetGlobalPropagator(
        {std::make_shared<propagation::NoOpPropagator>()});
    trace::Provider::SetTracerProvider({std::make_shared<trace::NoopTracerProvider>()});
    logs::Provider::SetLoggerProvider({std::make_shared<logs::NoopLoggerProvider>()});
    metrics::Provider::SetMeterProvider({std::make_shared<metrics::NoopMeterProvider>()});
  }

  void TearDown() override
  {
    if (sdk_)
    {
      sdk_->UnInstall();
    }
  }

  void CreateAndInstallSdk(const std::unique_ptr<config_sdk::Configuration> &model)
  {
    ASSERT_TRUE(sdk_ == nullptr);
    ASSERT_NO_THROW(sdk_ = config_sdk::ConfiguredSdk::Create(registry_, model));
    ASSERT_FALSE(sdk_ == nullptr);
    sdk_->Install();
  }

  void MakeRegistry()
  {
    registry_ = config_sdk::RegistryFactory::Create();
    registry_->SetExtensionSpanExporterBuilder(
        "recording", std::make_unique<config_test::RecordingSpanExporterBuilder>(span_buffer_));
    registry_->SetExtensionLogRecordExporterBuilder(
        "recording", std::make_unique<config_test::RecordingLogRecordExporterBuilder>(log_buffer_));
    registry_->SetExtensionPushMetricExporterBuilder(
        "recording",
        std::make_unique<config_test::RecordingPushMetricExporterBuilder>(metric_buffer_));
    registry_->SetPeriodicMetricReaderBuilder(
        std::make_unique<config_test::SyncPeriodicMetricReaderBuilder>());
    registry_->SetBatchSpanProcessorBuilder(
        std::make_unique<config_test::MockBatchSpanProcessorBuilder>());
    registry_->SetBatchLogRecordProcessorBuilder(
        std::make_unique<config_test::MockBatchLogRecordProcessorBuilder>());
  }

  static std::unique_ptr<config_sdk::TracerProviderConfiguration> MakeTracerProviderConfig()
  {
    auto exporter       = std::make_unique<config_sdk::ExtensionSpanExporterConfiguration>();
    exporter->name      = "recording";
    auto processor      = std::make_unique<config_sdk::SimpleSpanProcessorConfiguration>();
    processor->exporter = std::move(exporter);
    auto config         = std::make_unique<config_sdk::TracerProviderConfiguration>();
    config->processors.emplace_back(std::move(processor));
    return config;
  }

  static std::unique_ptr<config_sdk::LoggerProviderConfiguration> MakeLoggerProviderConfig()
  {
    auto exporter       = std::make_unique<config_sdk::ExtensionLogRecordExporterConfiguration>();
    exporter->name      = "recording";
    auto processor      = std::make_unique<config_sdk::SimpleLogRecordProcessorConfiguration>();
    processor->exporter = std::move(exporter);
    auto config         = std::make_unique<config_sdk::LoggerProviderConfiguration>();
    config->processors.emplace_back(std::move(processor));
    return config;
  }

  static std::unique_ptr<config_sdk::MeterProviderConfiguration> MakeMeterProviderConfig()
  {
    auto exporter    = std::make_unique<config_sdk::ExtensionPushMetricExporterConfiguration>();
    exporter->name   = "recording";
    auto reader      = std::make_unique<config_sdk::PeriodicMetricReaderConfiguration>();
    reader->exporter = std::move(exporter);
    auto config      = std::make_unique<config_sdk::MeterProviderConfiguration>();
    config->readers.emplace_back(std::move(reader));
    return config;
  }

  std::shared_ptr<config_test::SpanBuffer> span_buffer_{
      std::make_shared<config_test::SpanBuffer>()};
  std::shared_ptr<config_test::LogRecordBuffer> log_buffer_{
      std::make_shared<config_test::LogRecordBuffer>()};
  std::shared_ptr<config_test::MetricBuffer> metric_buffer_{
      std::make_shared<config_test::MetricBuffer>()};

  std::shared_ptr<config_sdk::Registry> registry_;
  std::unique_ptr<config_sdk::ConfiguredSdk> sdk_;
};

}  // namespace

//---------------------------------------------------------------------------
// Resource configuration tests

TEST_F(ProgrammaticConfigTest, ResourceAttributesFromList)
{
  auto resource_config             = std::make_unique<config_sdk::ResourceConfiguration>();
  resource_config->attributes_list = "service.name=test-service,service.version=1.0";
  auto model                       = std::make_unique<config_sdk::Configuration>();
  model->resource                  = std::move(resource_config);

  CreateAndInstallSdk(model);

  const auto &attributes = sdk_->resource.GetAttributes();
  ASSERT_NE(attributes.find("service.name"), attributes.end());
  EXPECT_EQ(nostd::get<std::string>(attributes.at("service.name")), "test-service");
  ASSERT_NE(attributes.find("service.version"), attributes.end());
  EXPECT_EQ(nostd::get<std::string>(attributes.at("service.version")), "1.0");
}

//--------------------------------------------------------------------------
// Disabled SDK configuration tests

TEST_F(ProgrammaticConfigTest, DisabledConfigProducesNullProviders)
{
  auto model             = std::make_unique<config_sdk::Configuration>();
  model->disabled        = true;
  model->tracer_provider = MakeTracerProviderConfig();
  model->logger_provider = MakeLoggerProviderConfig();
  model->meter_provider  = MakeMeterProviderConfig();

  CreateAndInstallSdk(model);

  EXPECT_EQ(sdk_->tracer_provider, nullptr);
  EXPECT_EQ(sdk_->logger_provider, nullptr);
  EXPECT_EQ(sdk_->meter_provider, nullptr);
  EXPECT_EQ(sdk_->propagator, nullptr);
}

TEST_F(ProgrammaticConfigTest, EnabledConfigProducesProviders)
{
  auto model                        = std::make_unique<config_sdk::Configuration>();
  model->disabled                   = false;
  model->tracer_provider            = MakeTracerProviderConfig();
  model->logger_provider            = MakeLoggerProviderConfig();
  model->meter_provider             = MakeMeterProviderConfig();
  model->propagator                 = std::make_unique<config_sdk::PropagatorConfiguration>();
  model->propagator->composite_list = "tracecontext";

  CreateAndInstallSdk(model);

  EXPECT_NE(sdk_->tracer_provider, nullptr);
  EXPECT_NE(sdk_->logger_provider, nullptr);
  EXPECT_NE(sdk_->meter_provider, nullptr);
  EXPECT_NE(sdk_->propagator, nullptr);
}

//---------------------------------------------------------------------------
// LoggerProvider tests

TEST_F(ProgrammaticConfigTest, LoggerProviderWithDefaults)
{
  auto logger_provider_config = MakeLoggerProviderConfig();

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->logger_provider = std::move(logger_provider_config);

  CreateAndInstallSdk(model);

  ASSERT_NE(sdk_->logger_provider, nullptr);

  auto logger = logs::Provider::GetLoggerProvider()->GetLogger("test");
  logger->EmitLogRecord(
      logs::Severity::kInfo, nostd::string_view("test-message"),
      common::MakeAttributes({{"key1", "value1"}, {"key2", "value2"}, {"key3", "value3"}}));

  ASSERT_TRUE(sdk_->logger_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_EQ(log_buffer_->size(), 1);
}

TEST_F(ProgrammaticConfigTest, LoggerProviderWithLogRecordLimits)
{
  config_sdk::LogRecordLimitsConfiguration limits;
  limits.attribute_count_limit        = 2;
  limits.attribute_value_length_limit = 5;

  auto logger_provider_config = MakeLoggerProviderConfig();
  logger_provider_config->limits =
      std::make_unique<config_sdk::LogRecordLimitsConfiguration>(limits);

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->logger_provider = std::move(logger_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->logger_provider, nullptr);

  auto logger = logs::Provider::GetLoggerProvider()->GetLogger("test");
  logger->EmitLogRecord(
      logs::Severity::kInfo, nostd::string_view("test-message"),
      common::MakeAttributes({{"key1", "value1"}, {"key2", "value2"}, {"key3", "value3"}}));

  ASSERT_TRUE(sdk_->logger_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_EQ(log_buffer_->size(), 1);
  auto *record = log_buffer_->front().get();
  EXPECT_EQ(nostd::get<std::string>(record->GetBody()), "test-message");
  const auto &attributes = record->GetAttributes();
  EXPECT_EQ(attributes.size(), limits.attribute_count_limit);
  for (const auto &attr : attributes)
  {
    EXPECT_EQ(nostd::get<std::string>(attr.second).size(), limits.attribute_value_length_limit);
  }
}

TEST_F(ProgrammaticConfigTest, AttributeLimitsAppliedToAllProviders)
{
  config_sdk::AttributeLimitsConfiguration attribute_limits;
  attribute_limits.attribute_count_limit        = 2;
  attribute_limits.attribute_value_length_limit = 5;

  auto model = std::make_unique<config_sdk::Configuration>();
  model->attribute_limits =
      std::make_unique<config_sdk::AttributeLimitsConfiguration>(attribute_limits);
  model->tracer_provider = MakeTracerProviderConfig();
  model->logger_provider = MakeLoggerProviderConfig();
  // no signal-specific limits: the fallback to top-level attribute_limits must apply to both

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->tracer_provider, nullptr);
  ASSERT_NE(sdk_->logger_provider, nullptr);

  trace::Provider::GetTracerProvider()
      ->GetTracer("test")
      ->StartSpan("s", {{"k1", "value1"}, {"k2", "value2"}, {"k3", "value3"}})
      ->End();
  logs::Provider::GetLoggerProvider()->GetLogger("test")->EmitLogRecord(
      logs::Severity::kInfo, nostd::string_view("body"),
      common::MakeAttributes({{"k1", "value1"}, {"k2", "value2"}, {"k3", "value3"}}));

  ASSERT_TRUE(sdk_->tracer_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->tracer_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  ASSERT_EQ(span_buffer_->size(), 1);
  const auto &span_attrs = (*span_buffer_)[0]->GetAttributes();
  EXPECT_EQ(span_attrs.size(), attribute_limits.attribute_count_limit);
  for (const auto &attr : span_attrs)
  {
    EXPECT_EQ(nostd::get<std::string>(attr.second).size(),
              attribute_limits.attribute_value_length_limit);
  }

  ASSERT_EQ(log_buffer_->size(), 1);
  const auto &log_attrs = log_buffer_->front()->GetAttributes();
  EXPECT_EQ(log_attrs.size(), attribute_limits.attribute_count_limit);
  for (const auto &attr : log_attrs)
  {
    EXPECT_EQ(nostd::get<std::string>(attr.second).size(),
              attribute_limits.attribute_value_length_limit);
  }
}

TEST_F(ProgrammaticConfigTest, SignalLimitsOverrideAttributeLimits)
{
  // top-level: attribute_count_limit = 1 (very restrictive)
  config_sdk::AttributeLimitsConfiguration attribute_limits;
  attribute_limits.attribute_count_limit        = 1;
  attribute_limits.attribute_value_length_limit = 100;

  // signal-specific: attribute_count_limit = 2 — must win over top-level
  config_sdk::SpanLimitsConfiguration span_limits;
  span_limits.attribute_count_limit        = 2;
  span_limits.attribute_value_length_limit = 100;

  config_sdk::LogRecordLimitsConfiguration log_limits;
  log_limits.attribute_count_limit        = 2;
  log_limits.attribute_value_length_limit = 100;

  auto model = std::make_unique<config_sdk::Configuration>();
  model->attribute_limits =
      std::make_unique<config_sdk::AttributeLimitsConfiguration>(attribute_limits);
  model->tracer_provider = MakeTracerProviderConfig();
  model->tracer_provider->limits =
      std::make_unique<config_sdk::SpanLimitsConfiguration>(span_limits);
  model->logger_provider = MakeLoggerProviderConfig();
  model->logger_provider->limits =
      std::make_unique<config_sdk::LogRecordLimitsConfiguration>(log_limits);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->tracer_provider, nullptr);
  ASSERT_NE(sdk_->logger_provider, nullptr);

  trace::Provider::GetTracerProvider()
      ->GetTracer("test")
      ->StartSpan("s", {{"k1", "v"}, {"k2", "v"}, {"k3", "v"}})
      ->End();
  logs::Provider::GetLoggerProvider()->GetLogger("test")->EmitLogRecord(
      logs::Severity::kInfo, nostd::string_view("body"),
      common::MakeAttributes({{"k1", "v"}, {"k2", "v"}, {"k3", "v"}}));

  ASSERT_TRUE(sdk_->tracer_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->tracer_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  ASSERT_EQ(span_buffer_->size(), 1);
  EXPECT_EQ((*span_buffer_)[0]->GetAttributes().size(), span_limits.attribute_count_limit);

  ASSERT_EQ(log_buffer_->size(), 1);
  EXPECT_EQ(log_buffer_->front()->GetAttributes().size(), log_limits.attribute_count_limit);
}

TEST_F(ProgrammaticConfigTest, LoggerProviderWithLoggerConfigurator)
{
  auto error_logger_matcher                    = config_sdk::LoggerMatcherAndConfigConfiguration();
  error_logger_matcher.name                    = "error_logger";
  error_logger_matcher.config.enabled          = true;
  error_logger_matcher.config.minimum_severity = config_sdk::SeverityNumber::error;

  auto logger_configurator = std::make_unique<config_sdk::LoggerConfiguratorConfiguration>();
  logger_configurator->default_config.enabled          = true;
  logger_configurator->default_config.minimum_severity = config_sdk::SeverityNumber::info;
  logger_configurator->loggers.emplace_back(std::move(error_logger_matcher));

  auto logger_provider_config                 = MakeLoggerProviderConfig();
  logger_provider_config->logger_configurator = std::move(logger_configurator);

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->logger_provider = std::move(logger_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->logger_provider, nullptr);

  // The default logger should be enabled and have a minimum severity of info
  auto default_logger = logs::Provider::GetLoggerProvider()->GetLogger("default_logger");
  default_logger->EmitLogRecord(
      logs::Severity::kInfo, nostd::string_view("test-message"),
      common::MakeAttributes({{"key1", "value1"}, {"key2", "value2"}, {"key3", "value3"}}));
  EXPECT_TRUE(default_logger->Enabled(logs::Severity::kInfo));

  // The error_logger should be enabled and have a minimum severity of error
  auto error_logger = logs::Provider::GetLoggerProvider()->GetLogger("error_logger");
  error_logger->EmitLogRecord(logs::Severity::kError, nostd::string_view("test-message"));

  EXPECT_FALSE(error_logger->Enabled(logs::Severity::kInfo));
  EXPECT_TRUE(error_logger->Enabled(logs::Severity::kError));

  ASSERT_TRUE(sdk_->logger_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_EQ(log_buffer_->size(), 2);
}

TEST_F(ProgrammaticConfigTest, LoggerProviderWithBatchProcessorDefaults)
{
  auto exporter       = std::make_unique<config_sdk::ExtensionLogRecordExporterConfiguration>();
  exporter->name      = "recording";
  auto processor      = std::make_unique<config_sdk::BatchLogRecordProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  auto logger_provider_config = std::make_unique<config_sdk::LoggerProviderConfiguration>();
  logger_provider_config->processors.emplace_back(std::move(processor));

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->logger_provider = std::move(logger_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->logger_provider, nullptr);

  logs::Provider::GetLoggerProvider()->GetLogger("test")->Info("test-message");
  ASSERT_TRUE(sdk_->logger_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_GE(log_buffer_->size(), 1);
}

TEST_F(ProgrammaticConfigTest, LoggerProviderWithBatchProcessorConfigured)
{
  auto exporter       = std::make_unique<config_sdk::ExtensionLogRecordExporterConfiguration>();
  exporter->name      = "recording";
  auto processor      = std::make_unique<config_sdk::BatchLogRecordProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  processor->schedule_delay        = 1000;
  processor->max_queue_size        = 100;
  processor->max_export_batch_size = 50;
  processor->export_timeout        = 1000;
  auto logger_provider_config      = std::make_unique<config_sdk::LoggerProviderConfiguration>();
  logger_provider_config->processors.emplace_back(std::move(processor));

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->logger_provider = std::move(logger_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->logger_provider, nullptr);

  logs::Provider::GetLoggerProvider()->GetLogger("test")->Info("test-message");
  ASSERT_TRUE(sdk_->logger_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->logger_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_GE(log_buffer_->size(), 1);
}

//--------------------------------------------------------------------------
// MeterProvider tests

TEST_F(ProgrammaticConfigTest, MeterProviderWithDefaults)
{
  auto model            = std::make_unique<config_sdk::Configuration>();
  model->meter_provider = MakeMeterProviderConfig();

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->meter_provider, nullptr);

  metrics::Provider::GetMeterProvider()
      ->GetMeter("test")
      ->CreateUInt64Counter("test-counter")
      ->Add(1);

  ASSERT_TRUE(sdk_->meter_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->meter_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_EQ(metric_buffer_->size(), 1);
}

TEST_F(ProgrammaticConfigTest, MeterProviderWithMeterConfigurator)
{
  auto disabled_meter_config           = config_sdk::MeterMatcherAndConfigConfiguration();
  disabled_meter_config.name           = "disabled-meter";
  disabled_meter_config.config.enabled = false;

  auto meter_configurator = std::make_unique<config_sdk::MeterConfiguratorConfiguration>();
  meter_configurator->default_config.enabled = true;
  meter_configurator->meters.push_back(disabled_meter_config);

  auto model                                = std::make_unique<config_sdk::Configuration>();
  model->meter_provider                     = MakeMeterProviderConfig();
  model->meter_provider->meter_configurator = std::move(meter_configurator);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->meter_provider, nullptr);

  auto default_meter = metrics::Provider::GetMeterProvider()->GetMeter("default-meter");
  default_meter->CreateUInt64Counter("test-counter")->Add(1);

  auto disabled_meter = metrics::Provider::GetMeterProvider()->GetMeter(disabled_meter_config.name);
  disabled_meter->CreateUInt64Counter("disabled-test-counter")->Add(1);

  ASSERT_TRUE(sdk_->meter_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->meter_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_EQ(metric_buffer_->size(), 1);
  for (const auto &metric : *metric_buffer_)
  {
    EXPECT_NE(metric.instrument_descriptor.name_, "disabled-test-counter");
  }
}

TEST_F(ProgrammaticConfigTest, MeterProviderWithDefaultViewSelector)
{
  // Create a view with a default selector (no instrument name or type, no meter name).
  // This view must match all instruments from all meters.

  auto view                 = std::make_unique<config_sdk::ViewConfiguration>();
  view->selector            = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  view->stream              = std::make_unique<config_sdk::ViewStreamConfiguration>();
  view->stream->description = "selected-by-default-view";

  auto meter_provider_config = MakeMeterProviderConfig();
  meter_provider_config->views.emplace_back(std::move(view));

  auto model            = std::make_unique<config_sdk::Configuration>();
  model->meter_provider = std::move(meter_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->meter_provider, nullptr);

  auto first_meter = metrics::Provider::GetMeterProvider()->GetMeter(
      "first-meter", "1.0", "https://opentelemetry.io/schemas/1.0.0");
  first_meter->CreateUInt64Counter("first-counter", "original", "requests")->Add(1);

  auto second_meter = metrics::Provider::GetMeterProvider()->GetMeter(
      "second-meter", "2.0", "https://opentelemetry.io/schemas/1.1.0");
  second_meter->CreateInt64UpDownCounter("second-up-down-counter", "original", "connections")
      ->Add(1);
  auto active_context = context::Context{};
  second_meter->CreateDoubleHistogram("third-histogram", "original", "milliseconds")
      ->Record(1.0, active_context);

  ASSERT_TRUE(sdk_->meter_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->meter_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  ASSERT_EQ(metric_buffer_->size(), 3);
  for (const auto &metric : *metric_buffer_)
  {
    EXPECT_EQ(metric.instrument_descriptor.description_, "selected-by-default-view");
  }
}

TEST_F(ProgrammaticConfigTest, MeterProviderWithDefaultInstrumentNameViewSelector)
{
  // Create a selector with a specific instrument type, but default (empty) instrument name.
  // This must match all instruments of the specified type.
  auto selector             = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  selector->instrument_type = config_sdk::InstrumentType::histogram;

  auto stream         = std::make_unique<config_sdk::ViewStreamConfiguration>();
  stream->description = "selected-histogram";

  auto view      = std::make_unique<config_sdk::ViewConfiguration>();
  view->selector = std::move(selector);
  view->stream   = std::move(stream);

  auto meter_provider_config = MakeMeterProviderConfig();
  meter_provider_config->views.emplace_back(std::move(view));

  auto model            = std::make_unique<config_sdk::Configuration>();
  model->meter_provider = std::move(meter_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->meter_provider, nullptr);

  auto active_context = context::Context{};
  auto meter          = metrics::Provider::GetMeterProvider()->GetMeter("test");
  meter->CreateDoubleHistogram("first-histogram", "original")->Record(1.0, active_context);
  meter->CreateDoubleHistogram("second-histogram", "original")->Record(1.0, active_context);
  meter->CreateUInt64Counter("counter", "original")->Add(1);

  ASSERT_TRUE(sdk_->meter_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->meter_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  ASSERT_EQ(metric_buffer_->size(), 3);
  std::size_t matched = 0;
  for (const auto &metric : *metric_buffer_)
  {
    if (metric.instrument_descriptor.description_ == "selected-histogram")
    {
      EXPECT_EQ(metric.instrument_descriptor.type_, metrics_sdk::InstrumentType::kHistogram);
      ++matched;
    }
  }
  EXPECT_EQ(matched, 2);
}

TEST_F(ProgrammaticConfigTest, MeterProviderWithDefaultInstrumentTypeViewSelector)
{
  // Create a selector with a specific instrument name, but default (none) instrument type.
  // This must match all instruments of the specified name, regardless of type.
  // Different meters may each have an instrument of the same name and type.
  auto selector             = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  selector->instrument_name = "selected-instrument";

  auto stream         = std::make_unique<config_sdk::ViewStreamConfiguration>();
  stream->description = "selected-instrument";

  auto view      = std::make_unique<config_sdk::ViewConfiguration>();
  view->selector = std::move(selector);
  view->stream   = std::move(stream);

  auto meter_provider_config = MakeMeterProviderConfig();
  meter_provider_config->views.emplace_back(std::move(view));

  auto model            = std::make_unique<config_sdk::Configuration>();
  model->meter_provider = std::move(meter_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->meter_provider, nullptr);

  auto active_context  = context::Context{};
  auto histogram_meter = metrics::Provider::GetMeterProvider()->GetMeter("histogram-meter");
  histogram_meter->CreateDoubleHistogram("selected-instrument", "original")
      ->Record(1.0, active_context);

  auto counter_meter = metrics::Provider::GetMeterProvider()->GetMeter("counter-meter");
  counter_meter->CreateUInt64Counter("selected-instrument", "original")->Add(1);
  counter_meter->CreateUInt64Counter("unmatched-counter", "original")->Add(1);

  ASSERT_TRUE(sdk_->meter_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->meter_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  ASSERT_EQ(metric_buffer_->size(), 3);
  std::size_t matched = 0;
  for (const auto &metric : *metric_buffer_)
  {
    if (metric.instrument_descriptor.description_ == "selected-instrument")
    {
      ++matched;
    }
  }
  EXPECT_EQ(matched, 2);
}

TEST_F(ProgrammaticConfigTest, MeterProviderWithMeterScopeViewSelector)
{
  auto selector              = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  selector->instrument_name  = "test-counter";
  selector->instrument_type  = config_sdk::InstrumentType::counter;
  selector->meter_name       = "selected-meter";
  selector->meter_version    = "1.0";
  selector->meter_schema_url = "https://opentelemetry.io/schemas/1.0.0";

  auto stream         = std::make_unique<config_sdk::ViewStreamConfiguration>();
  stream->description = "selected-by-meter";

  auto view      = std::make_unique<config_sdk::ViewConfiguration>();
  view->selector = std::move(selector);
  view->stream   = std::move(stream);

  auto meter_provider_config = MakeMeterProviderConfig();
  meter_provider_config->views.emplace_back(std::move(view));

  auto model            = std::make_unique<config_sdk::Configuration>();
  model->meter_provider = std::move(meter_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->meter_provider, nullptr);

  auto meter_provider = metrics::Provider::GetMeterProvider();
  meter_provider->GetMeter("selected-meter", "1.0", "https://opentelemetry.io/schemas/1.0.0")
      ->CreateUInt64Counter("test-counter", "original")
      ->Add(1);
  meter_provider->GetMeter("other-meter", "1.0", "https://opentelemetry.io/schemas/1.0.0")
      ->CreateUInt64Counter("test-counter", "original")
      ->Add(1);
  meter_provider->GetMeter("selected-meter", "2.0", "https://opentelemetry.io/schemas/1.0.0")
      ->CreateUInt64Counter("test-counter", "original")
      ->Add(1);
  meter_provider->GetMeter("selected-meter", "1.0", "https://opentelemetry.io/schemas/1.1.0")
      ->CreateUInt64Counter("test-counter", "original")
      ->Add(1);

  ASSERT_TRUE(sdk_->meter_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->meter_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  ASSERT_EQ(metric_buffer_->size(), 4);
  std::size_t matched = 0;
  for (const auto &metric : *metric_buffer_)
  {
    if (metric.instrument_descriptor.description_ == "selected-by-meter")
    {
      ++matched;
    }
  }
  EXPECT_EQ(matched, 1);
}

TEST_F(ProgrammaticConfigTest, MeterProviderWithHistogramAggregationViews)
{
  // View 1: Base2 exponential aggregation
  const std::size_t max_scale   = 10;
  const std::size_t max_buckets = 135;
  auto base2_histogram_aggregation =
      std::make_unique<config_sdk::Base2ExponentialBucketHistogramAggregationConfiguration>();
  base2_histogram_aggregation->max_scale = max_scale;
  base2_histogram_aggregation->max_size  = max_buckets;

  auto base2_histogram_stream         = std::make_unique<config_sdk::ViewStreamConfiguration>();
  base2_histogram_stream->aggregation = std::move(base2_histogram_aggregation);

  auto base2_histogram_selector = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  base2_histogram_selector->instrument_type = config_sdk::InstrumentType::histogram;
  base2_histogram_selector->instrument_name = "exponential-histogram";

  auto base2_histogram_view      = std::make_unique<config_sdk::ViewConfiguration>();
  base2_histogram_view->selector = std::move(base2_histogram_selector);
  base2_histogram_view->stream   = std::move(base2_histogram_stream);

  // View 2: Explicit bucket histogram aggregation.
  auto explicit_histogram_aggregation =
      std::make_unique<config_sdk::ExplicitBucketHistogramAggregationConfiguration>();
  explicit_histogram_aggregation->boundaries     = {0.0, 10.0, 50.0, 100.0};
  explicit_histogram_aggregation->record_min_max = true;

  auto explicit_histogram_stream         = std::make_unique<config_sdk::ViewStreamConfiguration>();
  explicit_histogram_stream->aggregation = std::move(explicit_histogram_aggregation);

  auto explicit_selector             = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  explicit_selector->instrument_type = config_sdk::InstrumentType::histogram;
  explicit_selector->instrument_name = "explicit-histogram";

  auto explicit_histogram_view      = std::make_unique<config_sdk::ViewConfiguration>();
  explicit_histogram_view->selector = std::move(explicit_selector);
  explicit_histogram_view->stream   = std::move(explicit_histogram_stream);

  // View 3: no aggregation set
  auto default_stream         = std::make_unique<config_sdk::ViewStreamConfiguration>();
  default_stream->aggregation = nullptr;  // intentionally null

  auto default_selector             = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  default_selector->instrument_type = config_sdk::InstrumentType::counter;
  default_selector->instrument_name = "default-counter";

  auto default_view      = std::make_unique<config_sdk::ViewConfiguration>();
  default_view->selector = std::move(default_selector);
  default_view->stream   = std::move(default_stream);

  auto meter_provider_config = MakeMeterProviderConfig();
  meter_provider_config->views.emplace_back(std::move(base2_histogram_view));
  meter_provider_config->views.emplace_back(std::move(explicit_histogram_view));
  meter_provider_config->views.emplace_back(std::move(default_view));

  auto model            = std::make_unique<config_sdk::Configuration>();
  model->meter_provider = std::move(meter_provider_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->meter_provider, nullptr);

  auto context = context::Context{};
  auto meter   = metrics::Provider::GetMeterProvider()->GetMeter("test");
  meter->CreateDoubleHistogram("exponential-histogram")->Record(42.0, context);
  meter->CreateDoubleHistogram("explicit-histogram")->Record(42.0, context);
  meter->CreateUInt64Counter("default-counter")->Add(1, context);
  ASSERT_TRUE(sdk_->meter_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->meter_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  // check that instances of the three data points were collected and are of the right type.
  EXPECT_EQ(metric_buffer_->size(), 3);
  bool found_base2_histogram    = false;
  bool found_explicit_histogram = false;
  bool found_default_counter    = false;

  for (const auto &metric : *metric_buffer_)
  {
    auto &point_data = metric.point_data_attr_.front().point_data;
    if (nostd::holds_alternative<metrics_sdk::Base2ExponentialHistogramPointData>(point_data))
    {
      found_base2_histogram = true;
      auto &base2_point_data =
          nostd::get<metrics_sdk::Base2ExponentialHistogramPointData>(point_data);
      EXPECT_EQ(base2_point_data.max_buckets_, max_buckets);
      EXPECT_LE(base2_point_data.scale_, max_scale);
    }
    else if (nostd::holds_alternative<metrics_sdk::HistogramPointData>(point_data))
    {
      found_explicit_histogram = true;
    }
    else if (nostd::holds_alternative<metrics_sdk::SumPointData>(point_data))
    {
      found_default_counter = true;
    }
  }

  EXPECT_TRUE(found_base2_histogram);
  EXPECT_TRUE(found_explicit_histogram);
  EXPECT_TRUE(found_default_counter);
}

//---------------------------------------------------------------------------
// TracerProvider tests

TEST_F(ProgrammaticConfigTest, TracerProviderWithDefaults)
{
  auto model             = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider = MakeTracerProviderConfig();

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->tracer_provider, nullptr);

  auto default_tracer = trace::Provider::GetTracerProvider()->GetTracer("default-tracer");
  default_tracer->StartSpan("test-span")->End();

  ASSERT_TRUE(sdk_->tracer_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->tracer_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_EQ(span_buffer_->size(), 1);
}

TEST_F(ProgrammaticConfigTest, TracerProviderWithTracerConfigurator)
{
  auto disabled_tracer_matcher           = config_sdk::TracerMatcherAndConfigConfiguration();
  disabled_tracer_matcher.name           = "disabled-tracer";
  disabled_tracer_matcher.config.enabled = false;

  auto tracer_configurator = std::make_unique<config_sdk::TracerConfiguratorConfiguration>();
  tracer_configurator->default_config.enabled = true;
  tracer_configurator->tracers.push_back(disabled_tracer_matcher);

  auto model                                  = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider                      = MakeTracerProviderConfig();
  model->tracer_provider->tracer_configurator = std::move(tracer_configurator);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->tracer_provider, nullptr);

  auto default_tracer = trace::Provider::GetTracerProvider()->GetTracer("default-tracer");
  default_tracer->StartSpan("test-span")->End();

  auto disabled_tracer = trace::Provider::GetTracerProvider()->GetTracer("disabled-tracer");
  disabled_tracer->StartSpan("disabled-test-span")->End();

  ASSERT_TRUE(sdk_->tracer_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));

  ASSERT_EQ(span_buffer_->size(), 1);
  EXPECT_NE(span_buffer_->at(0)->GetName(), "disabled-test-span");
}

TEST_F(ProgrammaticConfigTest, TracerProviderWithSampler)
{
  auto sampler                    = std::make_unique<config_sdk::AlwaysOffSamplerConfiguration>();
  auto model                      = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider          = MakeTracerProviderConfig();
  model->tracer_provider->sampler = std::move(sampler);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->tracer_provider, nullptr);

  trace::Provider::GetTracerProvider()->GetTracer("test")->StartSpan("test-span")->End();
  ASSERT_TRUE(sdk_->tracer_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->tracer_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_EQ(span_buffer_->size(), 0);
}

TEST_F(ProgrammaticConfigTest, TracerProviderWithParentBasedSamplerNullRoot)
{
  auto sampler                    = std::make_unique<config_sdk::ParentBasedSamplerConfiguration>();
  sampler->root                   = nullptr;  // explicitly null
  auto model                      = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider          = MakeTracerProviderConfig();
  model->tracer_provider->sampler = std::move(sampler);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->tracer_provider, nullptr);

  trace::Provider::GetTracerProvider()->GetTracer("test")->StartSpan("test-span")->End();
  ASSERT_TRUE(sdk_->tracer_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->tracer_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_EQ(span_buffer_->size(), 1);
}

TEST_F(ProgrammaticConfigTest, TracerProviderWithBatchProcessor)
{
  auto exporter               = std::make_unique<config_sdk::ExtensionSpanExporterConfiguration>();
  exporter->name              = "recording";
  auto processor              = std::make_unique<config_sdk::BatchSpanProcessorConfiguration>();
  processor->exporter         = std::move(exporter);
  auto tracer_provider_config = std::make_unique<config_sdk::TracerProviderConfiguration>();
  tracer_provider_config->processors.emplace_back(std::move(processor));

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider = std::move(tracer_provider_config);

  CreateAndInstallSdk(model);

  trace::Provider::GetTracerProvider()->GetTracer("test")->StartSpan("test-span")->End();
  ASSERT_TRUE(sdk_->tracer_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->tracer_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_GE(span_buffer_->size(), 1);
}

TEST_F(ProgrammaticConfigTest, TracerProviderWithBatchProcessorConfigured)
{
  auto exporter             = std::make_unique<config_sdk::ExtensionSpanExporterConfiguration>();
  exporter->name            = "recording";
  auto processor            = std::make_unique<config_sdk::BatchSpanProcessorConfiguration>();
  processor->schedule_delay = 60000;
  processor->max_queue_size = 100;
  processor->max_export_batch_size = 50;
  processor->export_timeout        = 1000;
  processor->exporter              = std::move(exporter);
  auto tracer_provider_config      = std::make_unique<config_sdk::TracerProviderConfiguration>();
  tracer_provider_config->processors.emplace_back(std::move(processor));

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider = std::move(tracer_provider_config);

  CreateAndInstallSdk(model);

  trace::Provider::GetTracerProvider()->GetTracer("test")->StartSpan("test-span")->End();
  ASSERT_TRUE(sdk_->tracer_provider->ForceFlush(std::chrono::milliseconds(kProcessTimeout)));
  ASSERT_TRUE(sdk_->tracer_provider->Shutdown(std::chrono::milliseconds(kProcessTimeout)));

  EXPECT_GE(span_buffer_->size(), 1);
}

//---------------------------------------------------------------------------
// Propagator tests

namespace
{
void CheckPropagators()
{
  auto tracer = trace::Provider::GetTracerProvider()->GetTracer("test");

  auto span = tracer->StartSpan("test-span");
  auto ctx0 = context::RuntimeContext::GetCurrent();
  auto ctx1 = trace::SetSpan(ctx0, span);

  // Add baggage so the baggage propagator has something to inject
  auto baggage = baggage::Baggage::GetDefault()->Set("key", "value");
  auto ctx     = baggage::SetBaggage(ctx1, baggage);

  config_test::MapCarrier carrier;
  propagation::GlobalTextMapPropagator::GetGlobalPropagator()->Inject(carrier, ctx);

  ASSERT_NE(carrier.map().find("traceparent"), carrier.map().end());  // tracecontext
  // traceparent format: "00-<32 hex trace_id>-<16 hex span_id>-<2 hex flags>" = 55 chars
  const std::string &traceparent = carrier.map().at("traceparent");
  EXPECT_EQ(traceparent.size(), 55U);
  EXPECT_EQ(traceparent.substr(0, 3), "00-");
  EXPECT_NE(carrier.map().find("baggage"), carrier.map().end());        // baggage
  EXPECT_NE(carrier.map().find("b3"), carrier.map().end());             // b3 single
  EXPECT_NE(carrier.map().find("X-B3-TraceId"), carrier.map().end());   // b3multi
  EXPECT_NE(carrier.map().find("uber-trace-id"), carrier.map().end());  // jaeger
}
}  // namespace

TEST_F(ProgrammaticConfigTest, PropagatorsCompositeList)
{
  auto propagator_config            = std::make_unique<config_sdk::PropagatorConfiguration>();
  propagator_config->composite_list = "tracecontext,baggage,b3,b3multi,jaeger";

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider = MakeTracerProviderConfig();
  model->propagator      = std::move(propagator_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->propagator, nullptr);

  CheckPropagators();
}

TEST_F(ProgrammaticConfigTest, PropagatorsComposite)
{
  auto propagator_config = std::make_unique<config_sdk::PropagatorConfiguration>();
  propagator_config->composite.emplace_back("tracecontext");
  propagator_config->composite.emplace_back("baggage");
  propagator_config->composite.emplace_back("b3");
  propagator_config->composite.emplace_back("b3multi");
  propagator_config->composite.emplace_back("jaeger");

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider = MakeTracerProviderConfig();
  model->propagator      = std::move(propagator_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->propagator, nullptr);

  CheckPropagators();
}

TEST_F(ProgrammaticConfigTest, PropagatorsDuplicateNames)
{
  // Duplicate names in composite + composite_list must each be registered only once.
  auto propagator_config = std::make_unique<config_sdk::PropagatorConfiguration>();
  propagator_config->composite.emplace_back("tracecontext");
  propagator_config->composite.emplace_back("baggage");
  propagator_config->composite.emplace_back("b3");
  propagator_config->composite.emplace_back("b3multi");
  propagator_config->composite.emplace_back("jaeger");
  // tracecontext and baggage duplicated via composite_list — must be skipped
  propagator_config->composite_list = "tracecontext,baggage";

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->tracer_provider = MakeTracerProviderConfig();
  model->propagator      = std::move(propagator_config);

  CreateAndInstallSdk(model);
  ASSERT_NE(sdk_->propagator, nullptr);

  CheckPropagators();
}

// ---------------------------------------------------------------------------
// BuilderDispatchTest: each test registers only the one builder under test.
// Any wrong dispatch throws UnsupportedException, failing the test immediately.

namespace
{

class BuilderDispatchTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    trace::Provider::SetTracerProvider({std::make_shared<trace::NoopTracerProvider>()});
    logs::Provider::SetLoggerProvider({std::make_shared<logs::NoopLoggerProvider>()});
    metrics::Provider::SetMeterProvider({std::make_shared<metrics::NoopMeterProvider>()});
  }

  void TearDown() override
  {
    if (sdk_)
      sdk_->UnInstall();
  }

  void BuildAndInstall(std::shared_ptr<config_sdk::Registry> registry,
                       std::unique_ptr<config_sdk::Configuration> model)
  {
    ASSERT_NO_THROW(sdk_ = config_sdk::ConfiguredSdk::Create(std::move(registry), model));
    ASSERT_NE(sdk_, nullptr);
    sdk_->Install();
  }

  // Registers all exporter builders with private buffers so only the cleared slot causes failure.
  static void RegisterAllExporterBuilders(config_sdk::Registry *registry)
  {
    registry->SetConsoleSpanBuilder(
        std::make_unique<config_test::RecordingConsoleSpanExporterBuilder>(
            std::make_shared<config_test::SpanBuffer>()));
    registry->SetOtlpHttpSpanBuilder(
        std::make_unique<config_test::RecordingOtlpHttpSpanExporterBuilder>(
            std::make_shared<config_test::SpanBuffer>()));
    registry->SetOtlpGrpcSpanBuilder(
        std::make_unique<config_test::RecordingOtlpGrpcSpanExporterBuilder>(
            std::make_shared<config_test::SpanBuffer>()));
    registry->SetOtlpFileSpanBuilder(
        std::make_unique<config_test::RecordingOtlpFileSpanExporterBuilder>(
            std::make_shared<config_test::SpanBuffer>()));
    registry->SetConsoleLogRecordBuilder(
        std::make_unique<config_test::RecordingConsoleLogRecordExporterBuilder>(
            std::make_shared<config_test::LogRecordBuffer>()));
    registry->SetOtlpHttpLogRecordBuilder(
        std::make_unique<config_test::RecordingOtlpHttpLogRecordExporterBuilder>(
            std::make_shared<config_test::LogRecordBuffer>()));
    registry->SetOtlpGrpcLogRecordBuilder(
        std::make_unique<config_test::RecordingOtlpGrpcLogRecordExporterBuilder>(
            std::make_shared<config_test::LogRecordBuffer>()));
    registry->SetOtlpFileLogRecordBuilder(
        std::make_unique<config_test::RecordingOtlpFileLogRecordExporterBuilder>(
            std::make_shared<config_test::LogRecordBuffer>()));
    registry->SetConsolePushMetricExporterBuilder(
        std::make_unique<config_test::RecordingConsolePushMetricExporterBuilder>(
            std::make_shared<config_test::MetricBuffer>()));
    registry->SetOtlpHttpPushMetricExporterBuilder(
        std::make_unique<config_test::RecordingOtlpHttpPushMetricExporterBuilder>(
            std::make_shared<config_test::MetricBuffer>()));
    registry->SetOtlpGrpcPushMetricExporterBuilder(
        std::make_unique<config_test::RecordingOtlpGrpcPushMetricExporterBuilder>(
            std::make_shared<config_test::MetricBuffer>()));
    registry->SetOtlpFilePushMetricExporterBuilder(
        std::make_unique<config_test::RecordingOtlpFilePushMetricExporterBuilder>(
            std::make_shared<config_test::MetricBuffer>()));
    registry->SetPrometheusPullMetricExporterBuilder(
        std::make_unique<config_test::RecordingPrometheusPullMetricExporterBuilder>(
            std::make_shared<config_test::MetricBuffer>()));
  }

  std::unique_ptr<config_sdk::ConfiguredSdk> sdk_;
};

}  // namespace

// --- Span exporter slots ---

TEST_F(BuilderDispatchTest, ConsoleSpanExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::SpanBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetConsoleSpanBuilder(
      std::make_unique<config_test::RecordingConsoleSpanExporterBuilder>(buffer));
  BuildAndInstall(std::move(registry),
                  MakeSpanConfig(std::make_unique<config_sdk::ConsoleSpanExporterConfiguration>()));

  trace::Provider::GetTracerProvider()->GetTracer("t")->StartSpan("console-span")->End();
  sdk_->tracer_provider->ForceFlush(kProcessTimeout);

  ASSERT_EQ(buffer->size(), 1u);
  EXPECT_EQ((*buffer)[0]->GetName(), "console-span");
}

TEST_F(BuilderDispatchTest, OtlpHttpSpanExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::SpanBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpHttpSpanBuilder(
      std::make_unique<config_test::RecordingOtlpHttpSpanExporterBuilder>(buffer));
  BuildAndInstall(
      std::move(registry),
      MakeSpanConfig(std::make_unique<config_sdk::OtlpHttpSpanExporterConfiguration>()));

  trace::Provider::GetTracerProvider()->GetTracer("t")->StartSpan("otlp-http-span")->End();
  sdk_->tracer_provider->ForceFlush(kProcessTimeout);

  ASSERT_EQ(buffer->size(), 1u);
  EXPECT_EQ((*buffer)[0]->GetName(), "otlp-http-span");
}

TEST_F(BuilderDispatchTest, OtlpGrpcSpanExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::SpanBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpGrpcSpanBuilder(
      std::make_unique<config_test::RecordingOtlpGrpcSpanExporterBuilder>(buffer));
  BuildAndInstall(
      std::move(registry),
      MakeSpanConfig(std::make_unique<config_sdk::OtlpGrpcSpanExporterConfiguration>()));

  trace::Provider::GetTracerProvider()->GetTracer("t")->StartSpan("otlp-grpc-span")->End();
  sdk_->tracer_provider->ForceFlush(kProcessTimeout);

  ASSERT_EQ(buffer->size(), 1u);
  EXPECT_EQ((*buffer)[0]->GetName(), "otlp-grpc-span");
}

TEST_F(BuilderDispatchTest, OtlpFileSpanExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::SpanBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpFileSpanBuilder(
      std::make_unique<config_test::RecordingOtlpFileSpanExporterBuilder>(buffer));
  BuildAndInstall(
      std::move(registry),
      MakeSpanConfig(std::make_unique<config_sdk::OtlpFileSpanExporterConfiguration>()));

  trace::Provider::GetTracerProvider()->GetTracer("t")->StartSpan("otlp-file-span")->End();
  sdk_->tracer_provider->ForceFlush(kProcessTimeout);

  ASSERT_EQ(buffer->size(), 1u);
  EXPECT_EQ((*buffer)[0]->GetName(), "otlp-file-span");
}

// --- Log record exporter slots ---

TEST_F(BuilderDispatchTest, ConsoleLogRecordExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::LogRecordBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetConsoleLogRecordBuilder(
      std::make_unique<config_test::RecordingConsoleLogRecordExporterBuilder>(buffer));
  BuildAndInstall(
      std::move(registry),
      MakeLogConfig(std::make_unique<config_sdk::ConsoleLogRecordExporterConfiguration>()));

  logs::Provider::GetLoggerProvider()->GetLogger("t")->Info("log");
  sdk_->logger_provider->ForceFlush(kProcessTimeout);

  ASSERT_EQ(buffer->size(), 1u);
}

TEST_F(BuilderDispatchTest, OtlpHttpLogRecordExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::LogRecordBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpHttpLogRecordBuilder(
      std::make_unique<config_test::RecordingOtlpHttpLogRecordExporterBuilder>(buffer));
  BuildAndInstall(
      std::move(registry),
      MakeLogConfig(std::make_unique<config_sdk::OtlpHttpLogRecordExporterConfiguration>()));

  logs::Provider::GetLoggerProvider()->GetLogger("t")->Info("log");
  sdk_->logger_provider->ForceFlush(kProcessTimeout);

  ASSERT_EQ(buffer->size(), 1u);
}

TEST_F(BuilderDispatchTest, OtlpGrpcLogRecordExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::LogRecordBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpGrpcLogRecordBuilder(
      std::make_unique<config_test::RecordingOtlpGrpcLogRecordExporterBuilder>(buffer));
  BuildAndInstall(
      std::move(registry),
      MakeLogConfig(std::make_unique<config_sdk::OtlpGrpcLogRecordExporterConfiguration>()));

  logs::Provider::GetLoggerProvider()->GetLogger("t")->Info("log");
  sdk_->logger_provider->ForceFlush(kProcessTimeout);

  ASSERT_EQ(buffer->size(), 1u);
}

TEST_F(BuilderDispatchTest, OtlpFileLogRecordExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::LogRecordBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpFileLogRecordBuilder(
      std::make_unique<config_test::RecordingOtlpFileLogRecordExporterBuilder>(buffer));
  BuildAndInstall(
      std::move(registry),
      MakeLogConfig(std::make_unique<config_sdk::OtlpFileLogRecordExporterConfiguration>()));

  logs::Provider::GetLoggerProvider()->GetLogger("t")->Info("log");
  sdk_->logger_provider->ForceFlush(kProcessTimeout);

  ASSERT_EQ(buffer->size(), 1u);
}

// --- Push metric exporter slots ---

TEST_F(BuilderDispatchTest, ConsolePushMetricExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::MetricBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetConsolePushMetricExporterBuilder(
      std::make_unique<config_test::RecordingConsolePushMetricExporterBuilder>(buffer));
  registry->SetPeriodicMetricReaderBuilder(
      std::make_unique<config_test::SyncPeriodicMetricReaderBuilder>());
  BuildAndInstall(
      std::move(registry),
      MakePushMetricConfig(std::make_unique<config_sdk::ConsolePushMetricExporterConfiguration>()));

  metrics::Provider::GetMeterProvider()->GetMeter("t")->CreateUInt64Counter("m")->Add(1);
  sdk_->meter_provider->ForceFlush(kProcessTimeout);

  ASSERT_FALSE(buffer->empty());
}

TEST_F(BuilderDispatchTest, OtlpHttpPushMetricExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::MetricBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpHttpPushMetricExporterBuilder(
      std::make_unique<config_test::RecordingOtlpHttpPushMetricExporterBuilder>(buffer));
  registry->SetPeriodicMetricReaderBuilder(
      std::make_unique<config_test::SyncPeriodicMetricReaderBuilder>());
  BuildAndInstall(std::move(registry),
                  MakePushMetricConfig(
                      std::make_unique<config_sdk::OtlpHttpPushMetricExporterConfiguration>()));

  metrics::Provider::GetMeterProvider()->GetMeter("t")->CreateUInt64Counter("m")->Add(1);
  sdk_->meter_provider->ForceFlush(kProcessTimeout);

  ASSERT_FALSE(buffer->empty());
}

TEST_F(BuilderDispatchTest, OtlpGrpcPushMetricExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::MetricBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpGrpcPushMetricExporterBuilder(
      std::make_unique<config_test::RecordingOtlpGrpcPushMetricExporterBuilder>(buffer));
  registry->SetPeriodicMetricReaderBuilder(
      std::make_unique<config_test::SyncPeriodicMetricReaderBuilder>());
  BuildAndInstall(std::move(registry),
                  MakePushMetricConfig(
                      std::make_unique<config_sdk::OtlpGrpcPushMetricExporterConfiguration>()));

  metrics::Provider::GetMeterProvider()->GetMeter("t")->CreateUInt64Counter("m")->Add(1);
  sdk_->meter_provider->ForceFlush(kProcessTimeout);

  ASSERT_FALSE(buffer->empty());
}

TEST_F(BuilderDispatchTest, OtlpFilePushMetricExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::MetricBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetOtlpFilePushMetricExporterBuilder(
      std::make_unique<config_test::RecordingOtlpFilePushMetricExporterBuilder>(buffer));
  registry->SetPeriodicMetricReaderBuilder(
      std::make_unique<config_test::SyncPeriodicMetricReaderBuilder>());
  BuildAndInstall(std::move(registry),
                  MakePushMetricConfig(
                      std::make_unique<config_sdk::OtlpFilePushMetricExporterConfiguration>()));

  metrics::Provider::GetMeterProvider()->GetMeter("t")->CreateUInt64Counter("m")->Add(1);
  sdk_->meter_provider->ForceFlush(kProcessTimeout);

  ASSERT_FALSE(buffer->empty());
}

// --- Prometheus pull metric slot ---

TEST_F(BuilderDispatchTest, PrometheusPullMetricExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::MetricBuffer>();
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetPrometheusPullMetricExporterBuilder(
      std::make_unique<config_test::RecordingPrometheusPullMetricExporterBuilder>(buffer));
  BuildAndInstall(std::move(registry),
                  MakePullMetricConfig(
                      std::make_unique<config_sdk::PrometheusPullMetricExporterConfiguration>()));

  metrics::Provider::GetMeterProvider()->GetMeter("t")->CreateUInt64Counter("m")->Add(1);
  sdk_->meter_provider->ForceFlush(kProcessTimeout);

  ASSERT_FALSE(buffer->empty());
}
// ---------------------------------------------------------------------------
// Unregistered builder slot tests: all other exporter slots are populated so
// the failure is specific to the one cleared slot.

TEST_F(BuilderDispatchTest, UnregisteredConsoleSpanBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetConsoleSpanBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakeSpanConfig(std::make_unique<config_sdk::ConsoleSpanExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpHttpSpanBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpHttpSpanBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakeSpanConfig(std::make_unique<config_sdk::OtlpHttpSpanExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpGrpcSpanBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpGrpcSpanBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakeSpanConfig(std::make_unique<config_sdk::OtlpGrpcSpanExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpFileSpanBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpFileSpanBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakeSpanConfig(std::make_unique<config_sdk::OtlpFileSpanExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredConsoleLogRecordBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetConsoleLogRecordBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakeLogConfig(std::make_unique<config_sdk::ConsoleLogRecordExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpHttpLogRecordBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpHttpLogRecordBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakeLogConfig(std::make_unique<config_sdk::OtlpHttpLogRecordExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpGrpcLogRecordBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpGrpcLogRecordBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakeLogConfig(std::make_unique<config_sdk::OtlpGrpcLogRecordExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpFileLogRecordBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpFileLogRecordBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakeLogConfig(std::make_unique<config_sdk::OtlpFileLogRecordExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredConsolePushMetricBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetConsolePushMetricExporterBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakePushMetricConfig(std::make_unique<config_sdk::ConsolePushMetricExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpHttpPushMetricBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpHttpPushMetricExporterBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakePushMetricConfig(
          std::make_unique<config_sdk::OtlpHttpPushMetricExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpGrpcPushMetricBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpGrpcPushMetricExporterBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakePushMetricConfig(
          std::make_unique<config_sdk::OtlpGrpcPushMetricExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredOtlpFilePushMetricBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetOtlpFilePushMetricExporterBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakePushMetricConfig(
          std::make_unique<config_sdk::OtlpFilePushMetricExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredPrometheusPullMetricBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  RegisterAllExporterBuilders(registry.get());
  registry->SetPrometheusPullMetricExporterBuilder(nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(
      std::move(registry),
      MakePullMetricConfig(
          std::make_unique<config_sdk::PrometheusPullMetricExporterConfiguration>()));
  EXPECT_EQ(sdk, nullptr);
}

// ---------------------------------------------------------------------------
// Unregistered propagator slot tests.

TEST_F(BuilderDispatchTest, UnregisteredTraceContextPropagatorBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetTextMapPropagatorBuilder("tracecontext", nullptr);
  auto sdk =
      config_sdk::ConfiguredSdk::Create(std::move(registry), MakePropagatorConfig("tracecontext"));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredBaggagePropagatorBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetTextMapPropagatorBuilder("baggage", nullptr);
  auto sdk =
      config_sdk::ConfiguredSdk::Create(std::move(registry), MakePropagatorConfig("baggage"));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredB3PropagatorBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetTextMapPropagatorBuilder("b3", nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(std::move(registry), MakePropagatorConfig("b3"));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredB3MultiPropagatorBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetTextMapPropagatorBuilder("b3multi", nullptr);
  auto sdk =
      config_sdk::ConfiguredSdk::Create(std::move(registry), MakePropagatorConfig("b3multi"));
  EXPECT_EQ(sdk, nullptr);
}

TEST_F(BuilderDispatchTest, UnregisteredJaegerPropagatorBuilder)
{
  auto registry = config_sdk::RegistryFactory::Create();
  registry->SetTextMapPropagatorBuilder("jaeger", nullptr);
  auto sdk = config_sdk::ConfiguredSdk::Create(std::move(registry), MakePropagatorConfig("jaeger"));
  EXPECT_EQ(sdk, nullptr);
}
