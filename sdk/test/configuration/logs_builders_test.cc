// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/logs/logger.h"
#include "opentelemetry/logs/logger_provider.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/logs/severity.h"

#include "opentelemetry/sdk/configuration/batch_log_record_processor_builder.h"
#include "opentelemetry/sdk/configuration/batch_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/configuration.h"
#include "opentelemetry/sdk/configuration/configured_sdk.h"
#include "opentelemetry/sdk/configuration/console_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_log_record_processor_builder.h"
#include "opentelemetry/sdk/configuration/extension_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/logger_config_configuration.h"
#include "opentelemetry/sdk/configuration/logger_configurator_builder.h"
#include "opentelemetry/sdk/configuration/logger_configurator_configuration.h"
#include "opentelemetry/sdk/configuration/logger_matcher_and_config_configuration.h"
#include "opentelemetry/sdk/configuration/logger_provider_builder.h"
#include "opentelemetry/sdk/configuration/logger_provider_builder_context.h"
#include "opentelemetry/sdk/configuration/logger_provider_configuration.h"
#include "opentelemetry/sdk/configuration/logs_builder_utils.h"
#include "opentelemetry/sdk/configuration/logs_builders.h"
#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/configuration/severity_number.h"
#include "opentelemetry/sdk/configuration/simple_log_record_processor_builder.h"
#include "opentelemetry/sdk/configuration/simple_log_record_processor_configuration.h"
#include "opentelemetry/sdk/configuration/unsupported_exception.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/instrumentationscope/scope_configurator.h"
#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/sdk/logs/logger_config.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/processor.h"
#include "opentelemetry/sdk/resource/resource.h"

#include "config_test_common.h"

namespace logs       = opentelemetry::logs;
namespace logs_sdk   = opentelemetry::sdk::logs;
namespace scope_sdk  = opentelemetry::sdk::instrumentationscope;
namespace config_sdk = opentelemetry::sdk::configuration;

TEST(LogsBuilders, InvalidBuilderContextMissingRegistry)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());
  auto resource = opentelemetry::sdk::resource::Resource::Create({});
  config_sdk::LoggerProviderBuilderContext context{nullptr, &resource};
  auto model = std::make_unique<config_sdk::LoggerProviderConfiguration>();
  EXPECT_THROW(registry->GetLoggerProviderBuilder()->Build(context, model.get()),
               config_sdk::UnsupportedException);
}

TEST(LogsBuilders, InvalidBuilderContextMissingResource)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());
  config_sdk::LoggerProviderBuilderContext context{registry.get(), nullptr};
  auto model = std::make_unique<config_sdk::LoggerProviderConfiguration>();
  EXPECT_THROW(registry->GetLoggerProviderBuilder()->Build(context, model.get()),
               config_sdk::UnsupportedException);
}

TEST(LogsBuilders, EmptyRegistryHasNoLogsBuilders)
{
  auto registry = std::make_shared<config_sdk::Registry>();

  EXPECT_EQ(registry->GetBatchLogRecordProcessorBuilder(), nullptr);
  EXPECT_EQ(registry->GetSimpleLogRecordProcessorBuilder(), nullptr);
  EXPECT_EQ(registry->GetLoggerConfiguratorBuilder(), nullptr);
  EXPECT_EQ(registry->GetLoggerProviderBuilder(), nullptr);
}

TEST(LogsBuilders, RegisterDefaultLogsBuildersFillesAllLogsSlots)
{
  auto registry = std::make_shared<config_sdk::Registry>();

  config_sdk::RegisterDefaultLogsBuilders(registry.get());

  EXPECT_NE(registry->GetBatchLogRecordProcessorBuilder(), nullptr);
  EXPECT_NE(registry->GetSimpleLogRecordProcessorBuilder(), nullptr);
  EXPECT_NE(registry->GetLoggerConfiguratorBuilder(), nullptr);
  EXPECT_NE(registry->GetLoggerProviderBuilder(), nullptr);
}

TEST(LogsBuilderUtils, CreateLoggerConfigurator)
{
  config_sdk::LoggerConfigConfiguration default_config;
  default_config.enabled          = true;
  default_config.minimum_severity = config_sdk::SeverityNumber::warn;
  default_config.trace_based      = false;

  config_sdk::LoggerMatcherAndConfigConfiguration matcher1;
  matcher1.name                    = "enabled_minsev_error_not_trace_based";
  matcher1.config.enabled          = true;
  matcher1.config.minimum_severity = config_sdk::SeverityNumber::error3;
  matcher1.config.trace_based      = false;

  config_sdk::LoggerMatcherAndConfigConfiguration matcher2;
  matcher2.name                    = "disabled_minsev_info_trace_based";
  matcher2.config.enabled          = false;
  matcher2.config.minimum_severity = config_sdk::SeverityNumber::debug;
  matcher2.config.trace_based      = true;

  auto model            = std::make_unique<config_sdk::LoggerConfiguratorConfiguration>();
  model->default_config = default_config;
  model->loggers.push_back(matcher1);
  model->loggers.push_back(matcher2);

  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());

  auto logger_configurator = registry->GetLoggerConfiguratorBuilder()->Build(model.get());
  ASSERT_NE(logger_configurator, nullptr);

  auto default_scope = scope_sdk::InstrumentationScope::Create("default_scope");
  logs_sdk::LoggerConfig sdk_logger_config_default =
      logger_configurator->ComputeConfig(*default_scope);

  auto scope_1 = scope_sdk::InstrumentationScope::Create(matcher1.name);
  logs_sdk::LoggerConfig sdk_logger_config_1 = logger_configurator->ComputeConfig(*scope_1);

  auto scope_2 = scope_sdk::InstrumentationScope::Create(matcher2.name);
  logs_sdk::LoggerConfig sdk_logger_config_2 = logger_configurator->ComputeConfig(*scope_2);

  EXPECT_TRUE(sdk_logger_config_default.IsEnabled());
  EXPECT_EQ(sdk_logger_config_default.GetMinimumSeverity(), logs::Severity::kWarn);
  EXPECT_FALSE(sdk_logger_config_default.IsTraceBased());

  EXPECT_TRUE(sdk_logger_config_1.IsEnabled());
  EXPECT_EQ(sdk_logger_config_1.GetMinimumSeverity(), logs::Severity::kError3);
  EXPECT_FALSE(sdk_logger_config_1.IsTraceBased());

  EXPECT_FALSE(sdk_logger_config_2.IsEnabled());
  EXPECT_EQ(sdk_logger_config_2.GetMinimumSeverity(), logs::Severity::kDebug);
  EXPECT_TRUE(sdk_logger_config_2.IsTraceBased());
}

TEST(LogsBuilderUtils, ToLogSeverityCoversAllVariants)
{
  // Sweep all SeverityNumber values through CreateLoggerConfigurator to cover ToLogSeverity.
  const struct
  {
    config_sdk::SeverityNumber input;
    logs::Severity expected;
  } kCases[] = {
      {config_sdk::SeverityNumber::trace, logs::Severity::kTrace},
      {config_sdk::SeverityNumber::trace2, logs::Severity::kTrace2},
      {config_sdk::SeverityNumber::trace3, logs::Severity::kTrace3},
      {config_sdk::SeverityNumber::trace4, logs::Severity::kTrace4},
      {config_sdk::SeverityNumber::debug, logs::Severity::kDebug},
      {config_sdk::SeverityNumber::debug2, logs::Severity::kDebug2},
      {config_sdk::SeverityNumber::debug3, logs::Severity::kDebug3},
      {config_sdk::SeverityNumber::debug4, logs::Severity::kDebug4},
      {config_sdk::SeverityNumber::info, logs::Severity::kInfo},
      {config_sdk::SeverityNumber::info2, logs::Severity::kInfo2},
      {config_sdk::SeverityNumber::info3, logs::Severity::kInfo3},
      {config_sdk::SeverityNumber::info4, logs::Severity::kInfo4},
      {config_sdk::SeverityNumber::warn, logs::Severity::kWarn},
      {config_sdk::SeverityNumber::warn2, logs::Severity::kWarn2},
      {config_sdk::SeverityNumber::warn3, logs::Severity::kWarn3},
      {config_sdk::SeverityNumber::warn4, logs::Severity::kWarn4},
      {config_sdk::SeverityNumber::error, logs::Severity::kError},
      {config_sdk::SeverityNumber::error2, logs::Severity::kError2},
      {config_sdk::SeverityNumber::error3, logs::Severity::kError3},
      {config_sdk::SeverityNumber::error4, logs::Severity::kError4},
      {config_sdk::SeverityNumber::fatal, logs::Severity::kFatal},
      {config_sdk::SeverityNumber::fatal2, logs::Severity::kFatal2},
      {config_sdk::SeverityNumber::fatal3, logs::Severity::kFatal3},
      {config_sdk::SeverityNumber::fatal4, logs::Severity::kFatal4},
  };

  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());

  for (const auto &tc : kCases)
  {
    auto model = std::make_unique<config_sdk::LoggerConfiguratorConfiguration>();
    model->default_config.minimum_severity = tc.input;
    auto configurator = registry->GetLoggerConfiguratorBuilder()->Build(model.get());
    ASSERT_NE(configurator, nullptr);
    auto scope  = scope_sdk::InstrumentationScope::Create("s");
    auto config = configurator->ComputeConfig(*scope);
    EXPECT_EQ(config.GetMinimumSeverity(), tc.expected);
  }
}

TEST(LogsBuilderUtils, CreateBatchLogRecordProcessor)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());
  registry->SetConsoleLogRecordBuilder(
      std::make_unique<config_test::NoopConsoleLogRecordExporterBuilder>());

  auto exporter       = std::make_unique<config_sdk::ConsoleLogRecordExporterConfiguration>();
  auto processor      = std::make_unique<config_sdk::BatchLogRecordProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  std::unique_ptr<config_sdk::LogRecordProcessorConfiguration> model = std::move(processor);

  auto result = config_sdk::LogsBuilderUtils::CreateLogRecordProcessor(registry.get(), model);
  EXPECT_NE(result, nullptr);
}

TEST(LogsBuilderUtils, CreateSimpleLogRecordProcessor)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());
  registry->SetConsoleLogRecordBuilder(
      std::make_unique<config_test::NoopConsoleLogRecordExporterBuilder>());

  auto exporter       = std::make_unique<config_sdk::ConsoleLogRecordExporterConfiguration>();
  auto processor      = std::make_unique<config_sdk::SimpleLogRecordProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  std::unique_ptr<config_sdk::LogRecordProcessorConfiguration> model = std::move(processor);

  auto result = config_sdk::LogsBuilderUtils::CreateLogRecordProcessor(registry.get(), model);
  EXPECT_NE(result, nullptr);
}

TEST(LogsBuilderUtils, UnregisteredLoggerConfigurator)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  auto model    = std::make_unique<config_sdk::LoggerConfiguratorConfiguration>();
  EXPECT_THROW(config_sdk::LogsBuilderUtils::CreateLoggerConfigurator(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(LogsBuilderUtils, UnregisteredExtensionLogRecordExporter)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  auto ext      = std::make_unique<config_sdk::ExtensionLogRecordExporterConfiguration>();
  ext->name     = "unregistered";
  std::unique_ptr<config_sdk::LogRecordExporterConfiguration> model = std::move(ext);
  EXPECT_THROW(config_sdk::LogsBuilderUtils::CreateLogRecordExporter(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(LogsBuilderUtils, RegisteredExtensionLogRecordExporterBuilder)
{
  auto buffer   = std::make_shared<config_test::LogRecordBuffer>();
  auto registry = std::make_shared<config_sdk::Registry>();
  auto builder  = std::make_unique<config_test::RecordingLogRecordExporterBuilder>(buffer);
  registry->SetExtensionLogRecordExporterBuilder("my_exporter", std::move(builder));

  auto ext  = std::make_unique<config_sdk::ExtensionLogRecordExporterConfiguration>();
  ext->name = "my_exporter";
  std::unique_ptr<config_sdk::LogRecordExporterConfiguration> model = std::move(ext);
  auto exporter = config_sdk::LogsBuilderUtils::CreateLogRecordExporter(registry.get(), model);

  EXPECT_NE(exporter, nullptr);
}

TEST(LogsBuilderUtils, UnregisteredExtensionLogRecordProcessorBuilder)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  auto ext      = std::make_unique<config_sdk::ExtensionLogRecordProcessorConfiguration>();
  ext->name     = "unregistered";
  std::unique_ptr<config_sdk::LogRecordProcessorConfiguration> model = std::move(ext);
  EXPECT_THROW(config_sdk::LogsBuilderUtils::CreateLogRecordProcessor(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(LogsBuilderUtils, RegisteredExtensionLogRecordProcessorBuilder)
{
  auto registry     = std::make_shared<config_sdk::Registry>();
  auto builder      = std::make_unique<config_test::RecordingLogRecordProcessorBuilder>();
  auto *builder_ptr = builder.get();
  registry->SetExtensionLogRecordProcessorBuilder("my_processor", std::move(builder));

  auto ext  = std::make_unique<config_sdk::ExtensionLogRecordProcessorConfiguration>();
  ext->name = "my_processor";
  std::unique_ptr<config_sdk::LogRecordProcessorConfiguration> model = std::move(ext);
  auto processor = config_sdk::LogsBuilderUtils::CreateLogRecordProcessor(registry.get(), model);

  EXPECT_NE(processor, nullptr);
  EXPECT_TRUE(builder_ptr->called);
}

TEST(LogsBuilderUtils, UnregisteredBatchLogRecordProcessorBuilder)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());
  registry->SetConsoleLogRecordBuilder(
      std::make_unique<config_test::NoopConsoleLogRecordExporterBuilder>());
  registry->SetBatchLogRecordProcessorBuilder(nullptr);

  auto exporter       = std::make_unique<config_sdk::ConsoleLogRecordExporterConfiguration>();
  auto processor      = std::make_unique<config_sdk::BatchLogRecordProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  std::unique_ptr<config_sdk::LogRecordProcessorConfiguration> model = std::move(processor);

  EXPECT_THROW(config_sdk::LogsBuilderUtils::CreateLogRecordProcessor(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(LogsBuilderUtils, UnregisteredSimpleLogRecordProcessorBuilder)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());
  registry->SetConsoleLogRecordBuilder(
      std::make_unique<config_test::NoopConsoleLogRecordExporterBuilder>());
  registry->SetSimpleLogRecordProcessorBuilder(nullptr);

  auto exporter       = std::make_unique<config_sdk::ConsoleLogRecordExporterConfiguration>();
  auto processor      = std::make_unique<config_sdk::SimpleLogRecordProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  std::unique_ptr<config_sdk::LogRecordProcessorConfiguration> model = std::move(processor);

  EXPECT_THROW(config_sdk::LogsBuilderUtils::CreateLogRecordProcessor(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(LogsBuilderWorkflow, ConfigureInstallAndFlush)
{
  auto log_buffer = std::make_shared<config_test::LogRecordBuffer>();

  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultLogsBuilders(registry.get());
  registry->SetExtensionLogRecordExporterBuilder(
      "recording", std::make_unique<config_test::RecordingLogRecordExporterBuilder>(log_buffer));

  auto exporter       = std::make_unique<config_sdk::ExtensionLogRecordExporterConfiguration>();
  exporter->name      = "recording";
  auto processor      = std::make_unique<config_sdk::SimpleLogRecordProcessorConfiguration>();
  processor->exporter = std::move(exporter);
  auto lp_config      = std::make_unique<config_sdk::LoggerProviderConfiguration>();
  lp_config->processors.emplace_back(std::move(processor));
  auto model             = std::make_unique<config_sdk::Configuration>();
  model->logger_provider = std::move(lp_config);

  auto sdk = config_sdk::ConfiguredSdk::Create(registry, model);
  ASSERT_NE(sdk, nullptr);
  EXPECT_EQ(sdk->tracer_provider, nullptr);
  ASSERT_NE(sdk->logger_provider, nullptr);
  EXPECT_EQ(sdk->meter_provider, nullptr);
  sdk->Install();

  opentelemetry::logs::Provider::GetLoggerProvider()->GetLogger("test")->EmitLogRecord(
      opentelemetry::logs::Severity::kInfo, "hello");

  ASSERT_TRUE(sdk->logger_provider->ForceFlush(std::chrono::milliseconds(5000)));
  EXPECT_EQ(log_buffer->size(), 1u);

  sdk->UnInstall();
}
