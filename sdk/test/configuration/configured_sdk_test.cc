// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

#include "config_test_common.h"

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/noop_propagator.h"
#include "opentelemetry/logs/noop.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/metrics/noop.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/configuration/batch_log_record_processor_builder.h"
#include "opentelemetry/sdk/configuration/batch_span_processor_builder.h"
#include "opentelemetry/sdk/configuration/configuration.h"
#include "opentelemetry/sdk/configuration/configured_sdk.h"
#include "opentelemetry/sdk/configuration/console_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/console_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/console_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/logger_provider_builder.h"
#include "opentelemetry/sdk/configuration/logger_provider_configuration.h"
#include "opentelemetry/sdk/configuration/meter_provider_builder.h"
#include "opentelemetry/sdk/configuration/meter_provider_configuration.h"
#include "opentelemetry/sdk/configuration/metric_reader_configuration.h"
#include "opentelemetry/sdk/configuration/periodic_metric_reader_builder.h"
#include "opentelemetry/sdk/configuration/periodic_metric_reader_configuration.h"
#include "opentelemetry/sdk/configuration/propagator_configuration.h"
#include "opentelemetry/sdk/configuration/push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/configuration/registry_factory.h"
#include "opentelemetry/sdk/configuration/resource_configuration.h"
#include "opentelemetry/sdk/configuration/severity_number.h"
#include "opentelemetry/sdk/configuration/simple_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/simple_span_processor_configuration.h"
#include "opentelemetry/sdk/configuration/span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/span_processor_configuration.h"
#include "opentelemetry/sdk/configuration/tracer_provider_builder.h"
#include "opentelemetry/sdk/configuration/tracer_provider_configuration.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/provider.h"

namespace trace        = opentelemetry::trace;
namespace logs         = opentelemetry::logs;
namespace metrics      = opentelemetry::metrics;
namespace propagation  = opentelemetry::context::propagation;
namespace config_sdk   = opentelemetry::sdk::configuration;
namespace internal_log = opentelemetry::sdk::common::internal_log;

namespace
{

//------------------------------------------------------------------------------
// ConfiguredSdkTest fixture

class ConfiguredSdkTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    MakeRegistry();
    SetNoopProviders();
  }

  void TearDown() override
  {
    if (sdk_)
    {
      sdk_->UnInstall();
    }
    SetNoopProviders();
  }

  void SetNullProviders()
  {
    propagation::GlobalTextMapPropagator::SetGlobalPropagator({});
    trace::Provider::SetTracerProvider({});
    logs::Provider::SetLoggerProvider({});
    metrics::Provider::SetMeterProvider({});

    // Installing null providers falls back to the no-op providers.
    ASSERT_NE(propagation::GlobalTextMapPropagator::GetGlobalPropagator(), nullptr);
    ASSERT_NE(trace::Provider::GetTracerProvider(), nullptr);
    ASSERT_NE(logs::Provider::GetLoggerProvider(), nullptr);
    ASSERT_NE(metrics::Provider::GetMeterProvider(), nullptr);
  }

  void SetNoopProviders()
  {
    propagation::GlobalTextMapPropagator::SetGlobalPropagator(
        {std::make_shared<propagation::NoOpPropagator>()});
    trace::Provider::SetTracerProvider({std::make_shared<trace::NoopTracerProvider>()});
    logs::Provider::SetLoggerProvider({std::make_shared<logs::NoopLoggerProvider>()});
    metrics::Provider::SetMeterProvider({std::make_shared<metrics::NoopMeterProvider>()});

    ASSERT_NE(propagation::GlobalTextMapPropagator::GetGlobalPropagator(), nullptr);
    ASSERT_NE(trace::Provider::GetTracerProvider(), nullptr);
    ASSERT_NE(logs::Provider::GetLoggerProvider(), nullptr);
    ASSERT_NE(metrics::Provider::GetMeterProvider(), nullptr);
  }

  void CreateSdk(const std::unique_ptr<config_sdk::Configuration> &model)
  {
    ASSERT_TRUE(sdk_ == nullptr);
    ASSERT_NO_THROW(sdk_ = config_sdk::ConfiguredSdk::Create(registry_, model));
    ASSERT_FALSE(sdk_ == nullptr);
  }

  void CheckProviders()
  {
    ASSERT_FALSE(sdk_->tracer_provider == nullptr);
    ASSERT_FALSE(sdk_->logger_provider == nullptr);
    ASSERT_FALSE(sdk_->meter_provider == nullptr);
  }

  void MakeRegistry()
  {
    registry_ = config_sdk::RegistryFactory::Create();
    registry_->SetConsoleSpanBuilder(
        std::make_unique<config_test::NoopConsoleSpanExporterBuilder>());
    registry_->SetConsoleLogRecordBuilder(
        std::make_unique<config_test::NoopConsoleLogRecordExporterBuilder>());
    registry_->SetConsolePushMetricExporterBuilder(
        std::make_unique<config_test::NoopConsolePushMetricExporterBuilder>());
    registry_->SetPeriodicMetricReaderBuilder(
        std::make_unique<config_test::NoopPeriodicMetricReaderBuilder>());
    registry_->SetBatchSpanProcessorBuilder(
        std::make_unique<config_test::MockBatchSpanProcessorBuilder>());
    registry_->SetBatchLogRecordProcessorBuilder(
        std::make_unique<config_test::MockBatchLogRecordProcessorBuilder>());
  }

  static std::unique_ptr<config_sdk::TracerProviderConfiguration> MakeTracerProviderConfig()
  {
    auto exporter       = std::make_unique<config_sdk::ConsoleSpanExporterConfiguration>();
    auto processor      = std::make_unique<config_sdk::SimpleSpanProcessorConfiguration>();
    processor->exporter = std::move(exporter);
    auto config         = std::make_unique<config_sdk::TracerProviderConfiguration>();
    config->processors.emplace_back(std::move(processor));
    return config;
  }

  static std::unique_ptr<config_sdk::LoggerProviderConfiguration> MakeLoggerProviderConfig()
  {
    auto exporter       = std::make_unique<config_sdk::ConsoleLogRecordExporterConfiguration>();
    auto processor      = std::make_unique<config_sdk::SimpleLogRecordProcessorConfiguration>();
    processor->exporter = std::move(exporter);
    auto config         = std::make_unique<config_sdk::LoggerProviderConfiguration>();
    config->processors.emplace_back(std::move(processor));
    return config;
  }

  static std::unique_ptr<config_sdk::MeterProviderConfiguration> MakeMeterProviderConfig()
  {
    auto exporter    = std::make_unique<config_sdk::ConsolePushMetricExporterConfiguration>();
    auto reader      = std::make_unique<config_sdk::PeriodicMetricReaderConfiguration>();
    reader->exporter = std::move(exporter);
    reader->interval = 3'600'000;
    reader->timeout  = 60'000;
    auto config      = std::make_unique<config_sdk::MeterProviderConfiguration>();
    config->readers.emplace_back(std::move(reader));
    return config;
  }

  std::shared_ptr<config_sdk::Registry> registry_;
  std::unique_ptr<config_sdk::ConfiguredSdk> sdk_;
};

}  // namespace

//------------------------------------------------------------------------------
// ConfiguredSdk Tests. These are intended to cover just the API and implementation of the
// ConfiguredSdk class.
//                      For integration testing to determine if the SDK is configured correctly, see
//                      the programmatic_configuration_test.cc file

TEST_F(ConfiguredSdkTest, ConfiguredSdkDefaultLogLevel)
{
  // default log level is info
  auto model      = std::make_unique<config_sdk::Configuration>();
  model->resource = std::make_unique<config_sdk::ResourceConfiguration>();
  CreateSdk(model);
  sdk_->Install();
  EXPECT_EQ(sdk_->log_level, internal_log::LogLevel::Info);
  EXPECT_EQ(internal_log::GlobalLogHandler::GetLogLevel(), internal_log::LogLevel::Info);
}

TEST_F(ConfiguredSdkTest, ConfiguredSdkSetLogLevel)
{
  auto model       = std::make_unique<config_sdk::Configuration>();
  model->resource  = std::make_unique<config_sdk::ResourceConfiguration>();
  model->log_level = config_sdk::SeverityNumber::debug;
  CreateSdk(model);
  sdk_->Install();
  EXPECT_EQ(sdk_->log_level, internal_log::LogLevel::Debug);
  EXPECT_EQ(internal_log::GlobalLogHandler::GetLogLevel(), internal_log::LogLevel::Debug);
}

TEST_F(ConfiguredSdkTest, DefaultProviderBuilders)
{
  auto model                        = std::make_unique<config_sdk::Configuration>();
  model->resource                   = std::make_unique<config_sdk::ResourceConfiguration>();
  model->tracer_provider            = MakeTracerProviderConfig();
  model->logger_provider            = MakeLoggerProviderConfig();
  model->meter_provider             = MakeMeterProviderConfig();
  model->propagator                 = std::make_unique<config_sdk::PropagatorConfiguration>();
  model->propagator->composite_list = "tracecontext,baggage,b3,b3multi,jaeger";

  CreateSdk(model);
  CheckProviders();

  sdk_->Install();
  auto sdk_tracer_provider = trace::Provider::GetTracerProvider();
  auto sdk_logger_provider = logs::Provider::GetLoggerProvider();
  auto sdk_meter_provider  = metrics::Provider::GetMeterProvider();
  auto sdk_propagator      = propagation::GlobalTextMapPropagator::GetGlobalPropagator();

  EXPECT_NE(sdk_tracer_provider, nullptr);
  EXPECT_NE(sdk_logger_provider, nullptr);
  EXPECT_NE(sdk_meter_provider, nullptr);
  EXPECT_NE(sdk_propagator, nullptr);

  sdk_->UnInstall();
  // UnInstall() releases the SDK providers, and the globals fall back to no-op
  // providers instead of becoming null.
  EXPECT_NE(trace::Provider::GetTracerProvider(), nullptr);
  EXPECT_NE(logs::Provider::GetLoggerProvider(), nullptr);
  EXPECT_NE(metrics::Provider::GetMeterProvider(), nullptr);
  EXPECT_NE(propagation::GlobalTextMapPropagator::GetGlobalPropagator(), nullptr);

  EXPECT_NE(trace::Provider::GetTracerProvider(), sdk_tracer_provider);
  EXPECT_NE(logs::Provider::GetLoggerProvider(), sdk_logger_provider);
  EXPECT_NE(metrics::Provider::GetMeterProvider(), sdk_meter_provider);
  EXPECT_NE(propagation::GlobalTextMapPropagator::GetGlobalPropagator(), sdk_propagator);
}

// ---------------------------------------------------------------------------
// Custom provider builder tests: verify the registry seam routes to the
// caller-supplied builder and installs its API-only provider globally.

TEST_F(ConfiguredSdkTest, CustomTracerProviderBuilder)
{
  auto builder      = std::make_unique<config_test::NoopTracerProviderBuilder>();
  auto *builder_ptr = builder.get();
  registry_->SetTracerProviderBuilder(std::move(builder));

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->resource        = std::make_unique<config_sdk::ResourceConfiguration>();
  model->tracer_provider = MakeTracerProviderConfig();

  std::unique_ptr<config_sdk::ConfiguredSdk> sdk;
  ASSERT_NO_THROW(sdk = config_sdk::ConfiguredSdk::Create(registry_, model));
  ASSERT_NE(sdk, nullptr);
  sdk->Install();
  sdk_ = std::move(sdk);

  EXPECT_TRUE(builder_ptr->called);
  ASSERT_NE(sdk_->tracer_provider, nullptr);
  EXPECT_EQ(sdk_->tracer_provider, builder_ptr->provider_);
  EXPECT_EQ(trace::Provider::GetTracerProvider().get(), builder_ptr->provider_.get());
}

TEST_F(ConfiguredSdkTest, CustomLoggerProviderBuilder)
{
  auto builder      = std::make_unique<config_test::NoopLoggerProviderBuilder>();
  auto *builder_ptr = builder.get();
  registry_->SetLoggerProviderBuilder(std::move(builder));

  auto model             = std::make_unique<config_sdk::Configuration>();
  model->resource        = std::make_unique<config_sdk::ResourceConfiguration>();
  model->logger_provider = MakeLoggerProviderConfig();

  std::unique_ptr<config_sdk::ConfiguredSdk> sdk;
  ASSERT_NO_THROW(sdk = config_sdk::ConfiguredSdk::Create(registry_, model));
  ASSERT_NE(sdk, nullptr);
  sdk->Install();
  sdk_ = std::move(sdk);

  EXPECT_TRUE(builder_ptr->called);
  ASSERT_NE(sdk_->logger_provider, nullptr);
  EXPECT_EQ(sdk_->logger_provider, builder_ptr->provider_);
  EXPECT_EQ(logs::Provider::GetLoggerProvider().get(), builder_ptr->provider_.get());
}

TEST_F(ConfiguredSdkTest, CustomMeterProviderBuilder)
{
  auto builder      = std::make_unique<config_test::NoopMeterProviderBuilder>();
  auto *builder_ptr = builder.get();
  registry_->SetMeterProviderBuilder(std::move(builder));

  auto model            = std::make_unique<config_sdk::Configuration>();
  model->resource       = std::make_unique<config_sdk::ResourceConfiguration>();
  model->meter_provider = MakeMeterProviderConfig();

  std::unique_ptr<config_sdk::ConfiguredSdk> sdk;
  ASSERT_NO_THROW(sdk = config_sdk::ConfiguredSdk::Create(registry_, model));
  ASSERT_NE(sdk, nullptr);
  sdk->Install();
  sdk_ = std::move(sdk);

  EXPECT_TRUE(builder_ptr->called);
  ASSERT_NE(sdk_->meter_provider, nullptr);
  EXPECT_EQ(sdk_->meter_provider, builder_ptr->provider_);
  EXPECT_EQ(metrics::Provider::GetMeterProvider().get(), builder_ptr->provider_.get());
}

// ---------------------------------------------------------------------------
// ConfiguredSdk::Create returns nullptr when a required provider builder slot is unregistered.

TEST_F(ConfiguredSdkTest, UnregisteredTracerProviderBuilder)
{
  registry_->SetTracerProviderBuilder(nullptr);
  auto model             = std::make_unique<config_sdk::Configuration>();
  model->resource        = std::make_unique<config_sdk::ResourceConfiguration>();
  model->tracer_provider = std::make_unique<config_sdk::TracerProviderConfiguration>();
  EXPECT_EQ(config_sdk::ConfiguredSdk::Create(registry_, model), nullptr);
}

TEST_F(ConfiguredSdkTest, UnregisteredLoggerProviderBuilder)
{
  registry_->SetLoggerProviderBuilder(nullptr);
  auto model             = std::make_unique<config_sdk::Configuration>();
  model->resource        = std::make_unique<config_sdk::ResourceConfiguration>();
  model->logger_provider = std::make_unique<config_sdk::LoggerProviderConfiguration>();
  EXPECT_EQ(config_sdk::ConfiguredSdk::Create(registry_, model), nullptr);
}

TEST_F(ConfiguredSdkTest, UnregisteredMeterProviderBuilder)
{
  registry_->SetMeterProviderBuilder(nullptr);
  auto model            = std::make_unique<config_sdk::Configuration>();
  model->resource       = std::make_unique<config_sdk::ResourceConfiguration>();
  model->meter_provider = std::make_unique<config_sdk::MeterProviderConfiguration>();
  EXPECT_EQ(config_sdk::ConfiguredSdk::Create(registry_, model), nullptr);
}
