// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <chrono>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(ENABLE_METRICS_EXEMPLAR_PREVIEW) && !defined(NO_GETENV)
#  include <cstdlib>
#  include "opentelemetry/sdk/common/global_log_handler.h"
#  include "opentelemetry/sdk/configuration/exemplar_filter.h"
#  include "opentelemetry/test_common/sdk/common/scoped_test_log_handler.h"

#  if defined(_MSC_VER)
#    include "opentelemetry/sdk/common/env_variables.h"
#  endif
#else
#  include <cstddef>
#endif

#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/nostd/function_ref.h"
#include "opentelemetry/nostd/utility.h"
#include "opentelemetry/nostd/variant.h"

#include "opentelemetry/sdk/configuration/aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/cardinality_limits_configuration.h"
#include "opentelemetry/sdk/configuration/configuration.h"
#include "opentelemetry/sdk/configuration/configured_sdk.h"
#include "opentelemetry/sdk/configuration/console_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/console_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/default_aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/drop_aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/explicit_bucket_histogram_aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/extension_pull_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_pull_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/extension_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/extension_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/include_exclude_configuration.h"
#include "opentelemetry/sdk/configuration/instrument_type.h"
#include "opentelemetry/sdk/configuration/last_value_aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/meter_configurator_builder.h"
#include "opentelemetry/sdk/configuration/meter_configurator_configuration.h"
#include "opentelemetry/sdk/configuration/meter_provider_builder.h"
#include "opentelemetry/sdk/configuration/meter_provider_builder_context.h"
#include "opentelemetry/sdk/configuration/meter_provider_configuration.h"
#include "opentelemetry/sdk/configuration/metric_producer_configuration.h"
#include "opentelemetry/sdk/configuration/metric_reader_configuration.h"
#include "opentelemetry/sdk/configuration/metrics_builder_utils.h"
#include "opentelemetry/sdk/configuration/metrics_builders.h"
#include "opentelemetry/sdk/configuration/open_census_metric_producer_configuration.h"
#include "opentelemetry/sdk/configuration/periodic_metric_reader_builder.h"
#include "opentelemetry/sdk/configuration/periodic_metric_reader_configuration.h"
#include "opentelemetry/sdk/configuration/pull_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/pull_metric_reader_configuration.h"
#include "opentelemetry/sdk/configuration/push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/configuration/string_array_configuration.h"
#include "opentelemetry/sdk/configuration/sum_aggregation_configuration.h"
#include "opentelemetry/sdk/configuration/unsupported_exception.h"
#include "opentelemetry/sdk/configuration/view_configuration.h"
#include "opentelemetry/sdk/configuration/view_selector_configuration.h"
#include "opentelemetry/sdk/configuration/view_stream_configuration.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/instrumentationscope/scope_configurator.h"
#include "opentelemetry/sdk/metrics/aggregation/aggregation.h"
#include "opentelemetry/sdk/metrics/aggregation/aggregation_config.h"
#include "opentelemetry/sdk/metrics/aggregation/default_aggregation.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/meter_config.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"
#include "opentelemetry/sdk/metrics/push_metric_exporter.h"
#include "opentelemetry/sdk/metrics/view/attributes_processor.h"
#include "opentelemetry/sdk/metrics/view/view.h"
#include "opentelemetry/sdk/metrics/view/view_registry.h"
#include "opentelemetry/sdk/resource/resource.h"

#include "config_test_common.h"

namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace scope_sdk   = opentelemetry::sdk::instrumentationscope;
namespace config_sdk  = opentelemetry::sdk::configuration;

namespace
{

static std::unique_ptr<config_sdk::ViewConfiguration> MakeViewWithAggregation(
    std::unique_ptr<config_sdk::AggregationConfiguration> aggregation)
{
  auto model                       = std::make_unique<config_sdk::ViewConfiguration>();
  model->selector                  = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  model->selector->instrument_type = config_sdk::InstrumentType::none;
  model->stream                    = std::make_unique<config_sdk::ViewStreamConfiguration>();
  model->stream->aggregation       = std::move(aggregation);
  return model;
}

std::unique_ptr<config_sdk::ViewConfiguration> MakeCardinalityOnlyViewConfig(
    config_sdk::InstrumentType instrument_type,
    std::size_t cardinality_limit)
{
  auto model                       = std::make_unique<config_sdk::ViewConfiguration>();
  model->selector                  = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  model->selector->instrument_type = instrument_type;

  model->stream = std::make_unique<config_sdk::ViewStreamConfiguration>();
  model->stream->aggregation_cardinality_limit = cardinality_limit;

  return model;
}

class AddViewTest : public ::testing::Test
{
protected:
  void CheckInstrumentType(config_sdk::InstrumentType config_type,
                           opentelemetry::sdk::metrics::InstrumentType sdk_type,
                           opentelemetry::sdk::metrics::AggregationType expected_aggregation =
                               opentelemetry::sdk::metrics::AggregationType::kDefault)
  {
    namespace metrics_sdk = opentelemetry::sdk::metrics;

    auto model = MakeCardinalityOnlyViewConfig(config_type, 7);

    metrics_sdk::ViewRegistry view_registry;
    config_sdk::MetricsBuilderUtils::AddView(&view_registry, model);

    metrics_sdk::InstrumentDescriptor descriptor{"test.instrument", "test description", "units",
                                                 sdk_type, metrics_sdk::InstrumentValueType::kLong};
    auto scope = scope_sdk::InstrumentationScope::Create("");

    int matched = 0;
    view_registry.FindViews(descriptor, *scope, [&](const metrics_sdk::View &view) {
      matched++;
      auto *config = view.GetAggregationConfig();
      EXPECT_NE(config, nullptr);
      if (config)
      {
        EXPECT_EQ(config->GetType(), expected_aggregation);
        EXPECT_EQ(config->cardinality_limit_, 7u);
      }
      return true;
    });
    EXPECT_EQ(matched, 1);
  }

  void CheckAggregationType(std::unique_ptr<config_sdk::AggregationConfiguration> aggregation,
                            opentelemetry::sdk::metrics::AggregationType expected_type)
  {
    namespace metrics_sdk = opentelemetry::sdk::metrics;

    auto model = MakeViewWithAggregation(std::move(aggregation));
    metrics_sdk::ViewRegistry view_registry;
    config_sdk::MetricsBuilderUtils::AddView(&view_registry, model);

    auto scope = scope_sdk::InstrumentationScope::Create("");
    metrics_sdk::InstrumentDescriptor descriptor{"m", "", "", metrics_sdk::InstrumentType::kCounter,
                                                 metrics_sdk::InstrumentValueType::kLong};
    int matched = 0;
    view_registry.FindViews(descriptor, *scope, [&](const metrics_sdk::View &view) {
      EXPECT_EQ(view.GetAggregationType(), expected_type);
      matched++;
      return true;
    });
    EXPECT_EQ(matched, 1);
  }
};

}  // namespace

TEST(MetricsBuilders, InvalidBuilderContextMissingRegistry)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());
  auto resource = opentelemetry::sdk::resource::Resource::Create({});
  config_sdk::MeterProviderBuilderContext context{nullptr, &resource};
  auto model = std::make_unique<config_sdk::MeterProviderConfiguration>();
  EXPECT_THROW(registry->GetMeterProviderBuilder()->Build(context, model.get()),
               config_sdk::UnsupportedException);
}

TEST(MetricsBuilders, InvalidBuilderContextMissingResource)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());
  config_sdk::MeterProviderBuilderContext context{registry.get(), nullptr};
  auto model = std::make_unique<config_sdk::MeterProviderConfiguration>();
  EXPECT_THROW(registry->GetMeterProviderBuilder()->Build(context, model.get()),
               config_sdk::UnsupportedException);
}

#if defined(ENABLE_METRICS_EXEMPLAR_PREVIEW) && !defined(NO_GETENV)

namespace
{
constexpr char kMetricsExemplarFilterEnv[] = "OTEL_METRICS_EXEMPLAR_FILTER";

#  if defined(_MSC_VER)
using opentelemetry::sdk::common::setenv;
using opentelemetry::sdk::common::unsetenv;
#  endif

class SdkBuilderExemplarFilterEnvironmentTest : public ::testing::Test
{
protected:
  void SetUp() override { unsetenv(kMetricsExemplarFilterEnv); }

  void TearDown() override { unsetenv(kMetricsExemplarFilterEnv); }
};
}  // namespace

TEST_F(SdkBuilderExemplarFilterEnvironmentTest, DeclarativeExemplarFilterDoesNotReadEnvironment)
{
  opentelemetry::test_common::ScopedTestLogHandler log_handler{
      opentelemetry::sdk::common::internal_log::LogLevel::Warning};
  setenv(kMetricsExemplarFilterEnv, "invalid", 1);

  auto model             = std::make_unique<config_sdk::MeterProviderConfiguration>();
  model->exemplar_filter = config_sdk::ExemplarFilter::always_on;

  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());
  auto resource = opentelemetry::sdk::resource::Resource::Create({});
  config_sdk::MeterProviderBuilderContext context{registry.get(), &resource};
  auto provider = registry->GetMeterProviderBuilder()->Build(context, model.get());
  ASSERT_NE(provider, nullptr);
  EXPECT_TRUE(log_handler.Drain().empty());
}
#endif

TEST_F(AddViewTest, DefaultAggregation)
{
  CheckAggregationType(std::make_unique<config_sdk::DefaultAggregationConfiguration>(),
                       metrics_sdk::AggregationType::kDefault);
}

TEST_F(AddViewTest, SumAggregation)
{
  CheckAggregationType(std::make_unique<config_sdk::SumAggregationConfiguration>(),
                       metrics_sdk::AggregationType::kSum);
}

TEST_F(AddViewTest, LastValueAggregation)
{
  CheckAggregationType(std::make_unique<config_sdk::LastValueAggregationConfiguration>(),
                       metrics_sdk::AggregationType::kLastValue);
}

TEST_F(AddViewTest, DropAggregation)
{
  CheckAggregationType(std::make_unique<config_sdk::DropAggregationConfiguration>(),
                       metrics_sdk::AggregationType::kDrop);
}

TEST_F(AddViewTest, CounterMatches)
{
  CheckInstrumentType(config_sdk::InstrumentType::counter,
                      opentelemetry::sdk::metrics::InstrumentType::kCounter);
}

TEST_F(AddViewTest, UpDownCounterMatches)
{
  CheckInstrumentType(config_sdk::InstrumentType::up_down_counter,
                      opentelemetry::sdk::metrics::InstrumentType::kUpDownCounter);
}

TEST_F(AddViewTest, ObservableCounterMatches)
{
  CheckInstrumentType(config_sdk::InstrumentType::observable_counter,
                      opentelemetry::sdk::metrics::InstrumentType::kObservableCounter);
}

TEST_F(AddViewTest, ObservableGaugeMatches)
{
  CheckInstrumentType(config_sdk::InstrumentType::observable_gauge,
                      opentelemetry::sdk::metrics::InstrumentType::kObservableGauge);
}

TEST_F(AddViewTest, ObservableUpDownCounterMatches)
{
  CheckInstrumentType(config_sdk::InstrumentType::observable_up_down_counter,
                      opentelemetry::sdk::metrics::InstrumentType::kObservableUpDownCounter);
}

TEST_F(AddViewTest, HistogramMatches)
{
  CheckInstrumentType(config_sdk::InstrumentType::histogram,
                      opentelemetry::sdk::metrics::InstrumentType::kHistogram,
                      opentelemetry::sdk::metrics::AggregationType::kHistogram);
}

#if OPENTELEMETRY_ABI_VERSION_NO < 2
// No CheckInstrumentType test for gauge: gauge is unsupported in ABI v1 and throws instead.
TEST_F(AddViewTest, GaugeThrowsWithABIv1)
{
  auto model = MakeCardinalityOnlyViewConfig(config_sdk::InstrumentType::gauge, 42);
  opentelemetry::sdk::metrics::ViewRegistry view_registry;
  EXPECT_THROW(config_sdk::MetricsBuilderUtils::AddView(&view_registry, model),
               config_sdk::UnsupportedException);
}
#else
TEST_F(AddViewTest, GaugeMatches)
{
  CheckInstrumentType(config_sdk::InstrumentType::gauge,
                      opentelemetry::sdk::metrics::InstrumentType::kGauge);
}
#endif

TEST_F(AddViewTest, EmptySelectorMatchesAllSupportedInstrumentTypes)
{
  namespace metrics_sdk = opentelemetry::sdk::metrics;

  auto model = MakeCardinalityOnlyViewConfig(config_sdk::InstrumentType::none, 42);

  metrics_sdk::ViewRegistry view_registry;
  config_sdk::MetricsBuilderUtils::AddView(&view_registry, model);

  auto instrumentation_scope = scope_sdk::InstrumentationScope::Create("");
  std::vector<metrics_sdk::InstrumentType> supported_instrument_types{
      metrics_sdk::InstrumentType::kCounter,
      metrics_sdk::InstrumentType::kHistogram,
      metrics_sdk::InstrumentType::kUpDownCounter,
      metrics_sdk::InstrumentType::kObservableCounter,
      metrics_sdk::InstrumentType::kObservableGauge,
      metrics_sdk::InstrumentType::kObservableUpDownCounter};
#if OPENTELEMETRY_ABI_VERSION_NO >= 2
  supported_instrument_types.push_back(metrics_sdk::InstrumentType::kGauge);
#endif

  for (auto instrument_type : supported_instrument_types)
  {
    metrics_sdk::InstrumentDescriptor instrument_descriptor{
        "test.instrument", "test description", "units", instrument_type,
        metrics_sdk::InstrumentValueType::kLong};
    int matched = 0;
    view_registry.FindViews(instrument_descriptor, *instrumentation_scope,
                            [&](const metrics_sdk::View &view) {
                              auto *config = view.GetAggregationConfig();
                              EXPECT_NE(config, nullptr);
                              if (config != nullptr)
                              {
                                EXPECT_EQ(config->cardinality_limit_, 42u);
                                matched++;
                              }
                              return true;
                            });
    EXPECT_EQ(matched, 1);
  }
}

TEST_F(AddViewTest, HistogramDefaultBoundariesPreserved)
{
  namespace metrics_sdk = opentelemetry::sdk::metrics;

  // Verify that AddView populates default bucket boundaries on a cardinality-only
  // histogram view, rather than leaving boundaries_ empty (which would produce a
  // single-bucket histogram instead of the spec's 15-bucket default).
  auto model = MakeCardinalityOnlyViewConfig(config_sdk::InstrumentType::histogram, 42);

  metrics_sdk::ViewRegistry view_registry;
  config_sdk::MetricsBuilderUtils::AddView(&view_registry, model);

  metrics_sdk::InstrumentDescriptor instrument_descriptor{
      "test.instrument", "test description", "units", metrics_sdk::InstrumentType::kHistogram,
      metrics_sdk::InstrumentValueType::kLong};
  auto instrumentation_scope = scope_sdk::InstrumentationScope::Create("");

  view_registry.FindViews(
      instrument_descriptor, *instrumentation_scope, [&](const metrics_sdk::View &view) {
        auto *aggregation_config = view.GetAggregationConfig();
        EXPECT_NE(aggregation_config, nullptr);
        if (!aggregation_config)
          return true;
        auto aggregation = metrics_sdk::DefaultAggregation::CreateAggregation(
            metrics_sdk::AggregationType::kHistogram, instrument_descriptor, aggregation_config);
        EXPECT_NE(aggregation, nullptr);
        if (!aggregation)
          return true;
        auto histogram_data =
            opentelemetry::nostd::get<metrics_sdk::HistogramPointData>(aggregation->ToPoint());
        EXPECT_EQ(histogram_data.boundaries_.size(), 15u);
        EXPECT_EQ(histogram_data.counts_.size(), 16u);
        return true;
      });
}

TEST_F(AddViewTest, CardinalityLimitPreservesExplicitAggregation)
{
  namespace metrics_sdk = opentelemetry::sdk::metrics;

  auto model = MakeCardinalityOnlyViewConfig(config_sdk::InstrumentType::histogram, 42);
  auto aggregation =
      std::make_unique<config_sdk::ExplicitBucketHistogramAggregationConfiguration>();
  aggregation->boundaries    = {1.0, 2.0};
  model->stream->aggregation = std::move(aggregation);

  metrics_sdk::ViewRegistry view_registry;
  config_sdk::MetricsBuilderUtils::AddView(&view_registry, model);

  metrics_sdk::InstrumentDescriptor instrument_descriptor{
      "test.instrument", "test description", "units", metrics_sdk::InstrumentType::kHistogram,
      metrics_sdk::InstrumentValueType::kLong};
  auto instrumentation_scope = scope_sdk::InstrumentationScope::Create("");

  int matched = 0;
  view_registry.FindViews(
      instrument_descriptor, *instrumentation_scope, [&](const metrics_sdk::View &view) {
        ++matched;
        auto *aggregation_config = view.GetAggregationConfig();
        EXPECT_NE(aggregation_config, nullptr);
        if (aggregation_config)
        {
          EXPECT_EQ(aggregation_config->GetType(), metrics_sdk::AggregationType::kHistogram);
          EXPECT_EQ(aggregation_config->cardinality_limit_, 42u);
          auto *histogram_config =
              static_cast<const metrics_sdk::HistogramAggregationConfig *>(aggregation_config);
          EXPECT_EQ(histogram_config->boundaries_, (std::vector<double>{1.0, 2.0}));
        }
        return true;
      });

  EXPECT_EQ(matched, 1);
}

TEST_F(AddViewTest, AttributeKeysFilterApplied)
{
  namespace metrics_sdk = opentelemetry::sdk::metrics;

  auto model                       = std::make_unique<config_sdk::ViewConfiguration>();
  model->selector                  = std::make_unique<config_sdk::ViewSelectorConfiguration>();
  model->selector->instrument_type = config_sdk::InstrumentType::counter;
  model->stream                    = std::make_unique<config_sdk::ViewStreamConfiguration>();
  model->stream->attribute_keys    = std::make_unique<config_sdk::IncludeExcludeConfiguration>();
  model->stream->attribute_keys->included =
      std::make_unique<config_sdk::StringArrayConfiguration>();
  model->stream->attribute_keys->included->string_array = {"allowed"};

  metrics_sdk::ViewRegistry view_registry;
  config_sdk::MetricsBuilderUtils::AddView(&view_registry, model);

  auto scope = scope_sdk::InstrumentationScope::Create("");
  metrics_sdk::InstrumentDescriptor descriptor{"m", "", "", metrics_sdk::InstrumentType::kCounter,
                                               metrics_sdk::InstrumentValueType::kLong};

  std::map<std::string, int> attributes = {{"allowed", 1}, {"filtered_out", 2}};
  opentelemetry::common::KeyValueIterableView<std::map<std::string, int>> attr_view{attributes};

  view_registry.FindViews(descriptor, *scope, [&](const metrics_sdk::View &view) {
    auto processor = view.GetAttributesProcessor();
    EXPECT_NE(processor, nullptr);
    if (!processor)
      return true;
    auto result = processor->process(attr_view);
    EXPECT_EQ(result.size(), 1u);
    EXPECT_NE(result.find("allowed"), result.end());
    EXPECT_EQ(result.find("filtered_out"), result.end());
    return true;
  });
}

TEST(MetricsBuilders, EmptyRegistryHasNoMetricsBuilders)
{
  auto registry = std::make_shared<config_sdk::Registry>();

  EXPECT_EQ(registry->GetPeriodicMetricReaderBuilder(), nullptr);
  EXPECT_EQ(registry->GetMeterConfiguratorBuilder(), nullptr);
  EXPECT_EQ(registry->GetMeterProviderBuilder(), nullptr);
}

TEST(MetricsBuilders, RegisterDefaultMetricsBuildersFillesAllMetricsSlots)
{
  auto registry = std::make_shared<config_sdk::Registry>();

  config_sdk::RegisterDefaultMetricsBuilders(registry.get());

  EXPECT_NE(registry->GetPeriodicMetricReaderBuilder(), nullptr);
  EXPECT_NE(registry->GetMeterConfiguratorBuilder(), nullptr);
  EXPECT_NE(registry->GetMeterProviderBuilder(), nullptr);
}

TEST(MetricsBuilders, DefaultPeriodicMetricReaderBuildCreatesReader)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());

  auto model      = std::make_unique<config_sdk::PeriodicMetricReaderConfiguration>();
  model->interval = 2000;
  model->timeout  = 500;

  const auto *builder = registry->GetPeriodicMetricReaderBuilder();
  ASSERT_NE(builder, nullptr);
  auto reader =
      builder->Build(model.get(), std::make_unique<config_test::NoopPushMetricExporter>());
  ASSERT_NE(reader, nullptr);
  EXPECT_TRUE(reader->Shutdown(std::chrono::seconds(5)));
}

TEST(MetricsBuilderUtils, CreatePullMetricReaderWithExtensionExporter)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  registry->SetExtensionPullMetricExporterBuilder(
      "test_pull", std::make_unique<config_test::NoopPullMetricExporterBuilder>());

  auto exporter_config  = std::make_unique<config_sdk::ExtensionPullMetricExporterConfiguration>();
  exporter_config->name = "test_pull";
  auto model            = std::make_unique<config_sdk::PullMetricReaderConfiguration>();
  model->exporter       = std::move(exporter_config);
  std::unique_ptr<config_sdk::MetricReaderConfiguration> reader_config = std::move(model);

  auto reader = config_sdk::MetricsBuilderUtils::CreateMetricReader(registry.get(), reader_config);
  ASSERT_NE(reader, nullptr);
  EXPECT_TRUE(reader->Shutdown(std::chrono::seconds(5)));
}

TEST(MetricsBuilderUtils, CreatePullMetricReaderWithProducer)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  registry->SetExtensionPullMetricExporterBuilder(
      "test_pull", std::make_unique<config_test::NoopPullMetricExporterBuilder>());

  auto exporter_config  = std::make_unique<config_sdk::ExtensionPullMetricExporterConfiguration>();
  exporter_config->name = "test_pull";
  auto model            = std::make_unique<config_sdk::PullMetricReaderConfiguration>();
  model->exporter       = std::move(exporter_config);
  model->producers.push_back(std::make_unique<config_sdk::OpenCensusMetricProducerConfiguration>());
  std::unique_ptr<config_sdk::MetricReaderConfiguration> reader_config = std::move(model);

  auto reader = config_sdk::MetricsBuilderUtils::CreateMetricReader(registry.get(), reader_config);
  ASSERT_NE(reader, nullptr);
  EXPECT_TRUE(reader->Shutdown(std::chrono::seconds(5)));
}

TEST(MetricsBuilderUtils, CreatePullMetricReaderWithCardinalityLimits)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  registry->SetExtensionPullMetricExporterBuilder(
      "test_pull", std::make_unique<config_test::NoopPullMetricExporterBuilder>());

  auto exporter_config  = std::make_unique<config_sdk::ExtensionPullMetricExporterConfiguration>();
  exporter_config->name = "test_pull";
  auto model            = std::make_unique<config_sdk::PullMetricReaderConfiguration>();
  model->exporter       = std::move(exporter_config);
  model->cardinality_limits = std::make_unique<config_sdk::CardinalityLimitsConfiguration>();
  model->cardinality_limits->counter                                   = 42;
  std::unique_ptr<config_sdk::MetricReaderConfiguration> reader_config = std::move(model);

  auto reader = config_sdk::MetricsBuilderUtils::CreateMetricReader(registry.get(), reader_config);
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->GetCardinalityLimit(opentelemetry::sdk::metrics::InstrumentType::kCounter),
            42u);
  EXPECT_TRUE(reader->Shutdown(std::chrono::seconds(5)));
}

TEST(MetricsBuilderUtils, CreatePeriodicMetricReader)
{
  auto exporter  = std::make_unique<config_sdk::ExtensionPushMetricExporterConfiguration>();
  exporter->name = "noop";

  auto periodic                = std::make_unique<config_sdk::PeriodicMetricReaderConfiguration>();
  periodic->exporter           = std::move(exporter);
  periodic->interval           = 12345;
  periodic->timeout            = 678;
  periodic->cardinality_limits = std::make_unique<config_sdk::CardinalityLimitsConfiguration>();
  periodic->cardinality_limits->default_limit              = 100;
  periodic->cardinality_limits->counter                    = 200;
  periodic->cardinality_limits->gauge                      = 300;
  periodic->cardinality_limits->histogram                  = 400;
  periodic->cardinality_limits->observable_counter         = 500;
  periodic->cardinality_limits->observable_gauge           = 600;
  periodic->cardinality_limits->observable_up_down_counter = 700;
  periodic->cardinality_limits->up_down_counter            = 800;
  const auto *model                                        = periodic.get();

  auto captured = std::make_shared<config_test::CapturedPeriodicReaderArgs>();

  auto registry = std::make_shared<config_sdk::Registry>();
  registry->SetExtensionPushMetricExporterBuilder(
      "noop", std::make_unique<config_test::NoopPushMetricExporterBuilder>());
  registry->SetPeriodicMetricReaderBuilder(
      std::make_unique<config_test::CapturingPeriodicMetricReaderBuilder>(captured));

  std::unique_ptr<config_sdk::MetricReaderConfiguration> reader_config = std::move(periodic);
  auto reader = config_sdk::MetricsBuilderUtils::CreateMetricReader(registry.get(), reader_config);
  ASSERT_NE(reader, nullptr);

  EXPECT_TRUE(captured->called);
  EXPECT_EQ(captured->interval, model->interval);
  EXPECT_EQ(captured->timeout, model->timeout);
  EXPECT_TRUE(captured->exporter != nullptr);
  EXPECT_EQ(reader->GetCardinalityLimit(opentelemetry::sdk::metrics::InstrumentType::kCounter),
            200u);
  EXPECT_EQ(reader->GetCardinalityLimit(opentelemetry::sdk::metrics::InstrumentType::kGauge), 300u);
  EXPECT_EQ(reader->GetCardinalityLimit(opentelemetry::sdk::metrics::InstrumentType::kHistogram),
            400u);
  EXPECT_EQ(
      reader->GetCardinalityLimit(opentelemetry::sdk::metrics::InstrumentType::kObservableCounter),
      500u);
  EXPECT_EQ(
      reader->GetCardinalityLimit(opentelemetry::sdk::metrics::InstrumentType::kObservableGauge),
      600u);
  EXPECT_EQ(reader->GetCardinalityLimit(
                opentelemetry::sdk::metrics::InstrumentType::kObservableUpDownCounter),
            700u);
  EXPECT_EQ(
      reader->GetCardinalityLimit(opentelemetry::sdk::metrics::InstrumentType::kUpDownCounter),
      800u);
}

TEST(MetricsBuilderUtils, CreateAttributesProcessor)
{
  std::map<std::string, int> attributes = {{"included", 1}, {"excluded", 2}, {"unlisted", 3}};
  opentelemetry::common::KeyValueIterableView<std::map<std::string, int>> iterable(attributes);

  // When both lists are configured, exclusion takes precedence over inclusion.
  {
    auto model                    = std::make_unique<config_sdk::IncludeExcludeConfiguration>();
    model->included               = std::make_unique<config_sdk::StringArrayConfiguration>();
    model->included->string_array = {"included", "excluded"};
    model->excluded               = std::make_unique<config_sdk::StringArrayConfiguration>();
    model->excluded->string_array = {"excluded"};

    auto processor = config_sdk::MetricsBuilderUtils::CreateAttributesProcessor(model);
    ASSERT_NE(processor, nullptr);
    auto filtered = processor->process(iterable);

    EXPECT_EQ(filtered.size(), 1u);
    EXPECT_NE(filtered.find("included"), filtered.end());
  }

  // Wildcard patterns are evaluated per key, with exclusion taking precedence.
  {
    std::map<std::string, int> wildcard_attributes = {
        {"foo.bar", 1}, {"foo.baz", 2}, {"question.x", 3}, {"question.xy", 4}, {"other", 5}};
    opentelemetry::common::KeyValueIterableView<std::map<std::string, int>> wildcard_iterable(
        wildcard_attributes);

    auto model                    = std::make_unique<config_sdk::IncludeExcludeConfiguration>();
    model->included               = std::make_unique<config_sdk::StringArrayConfiguration>();
    model->included->string_array = {"foo.*", "question.?"};
    model->excluded               = std::make_unique<config_sdk::StringArrayConfiguration>();
    model->excluded->string_array = {"foo.bar"};

    auto processor = config_sdk::MetricsBuilderUtils::CreateAttributesProcessor(model);
    ASSERT_NE(processor, nullptr);
    auto filtered = processor->process(wildcard_iterable);

    EXPECT_EQ(filtered.size(), 2u);
    EXPECT_NE(filtered.find("foo.baz"), filtered.end());
    EXPECT_NE(filtered.find("question.x"), filtered.end());
  }

  // An exclude-only configuration retains every key that is not excluded.
  {
    auto model                    = std::make_unique<config_sdk::IncludeExcludeConfiguration>();
    model->excluded               = std::make_unique<config_sdk::StringArrayConfiguration>();
    model->excluded->string_array = {"excluded"};

    auto processor = config_sdk::MetricsBuilderUtils::CreateAttributesProcessor(model);
    ASSERT_NE(processor, nullptr);
    auto filtered = processor->process(iterable);

    EXPECT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered.find("excluded"), filtered.end());
  }

  // A null include/exclude block leaves attributes unchanged.
  {
    auto model     = std::make_unique<config_sdk::IncludeExcludeConfiguration>();
    auto processor = config_sdk::MetricsBuilderUtils::CreateAttributesProcessor(model);
    ASSERT_NE(processor, nullptr);
    auto filtered = processor->process(iterable);

    EXPECT_EQ(filtered.size(), attributes.size());
  }

  // An empty include list leaves attributes unchanged.
  {
    auto model      = std::make_unique<config_sdk::IncludeExcludeConfiguration>();
    model->included = std::make_unique<config_sdk::StringArrayConfiguration>();
    model->excluded = std::make_unique<config_sdk::StringArrayConfiguration>();

    auto processor = config_sdk::MetricsBuilderUtils::CreateAttributesProcessor(model);
    ASSERT_NE(processor, nullptr);
    auto filtered = processor->process(iterable);

    EXPECT_EQ(filtered.size(), attributes.size());
  }
}

TEST(MetricsBuilderUtils, UnregisteredMeterConfiguratorBuilder)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());
  registry->SetMeterConfiguratorBuilder(nullptr);
  auto model = std::make_unique<config_sdk::MeterConfiguratorConfiguration>();
  EXPECT_THROW(config_sdk::MetricsBuilderUtils::CreateMeterConfigurator(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(MetricsBuilderUtils, UnregisteredExtensionPushMetricExporterBuilder)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());
  auto ext         = std::make_unique<config_sdk::ExtensionPushMetricExporterConfiguration>();
  ext->name        = "unregistered";
  auto reader      = std::make_unique<config_sdk::PeriodicMetricReaderConfiguration>();
  reader->exporter = std::move(ext);
  std::unique_ptr<config_sdk::MetricReaderConfiguration> model = std::move(reader);
  EXPECT_THROW(config_sdk::MetricsBuilderUtils::CreateMetricReader(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(MetricsBuilderUtils, UnregisteredExtensionPullMetricExporterBuilder)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());
  auto ext         = std::make_unique<config_sdk::ExtensionPullMetricExporterConfiguration>();
  ext->name        = "unregistered";
  auto reader      = std::make_unique<config_sdk::PullMetricReaderConfiguration>();
  reader->exporter = std::move(ext);
  std::unique_ptr<config_sdk::MetricReaderConfiguration> model = std::move(reader);
  EXPECT_THROW(config_sdk::MetricsBuilderUtils::CreateMetricReader(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(MetricsBuilderUtils, UnregisteredPeriodicMetricReaderBuilder)
{
  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());
  registry->SetConsolePushMetricExporterBuilder(
      std::make_unique<config_test::NoopConsolePushMetricExporterBuilder>());
  registry->SetPeriodicMetricReaderBuilder(nullptr);

  auto exporter    = std::make_unique<config_sdk::ConsolePushMetricExporterConfiguration>();
  auto reader      = std::make_unique<config_sdk::PeriodicMetricReaderConfiguration>();
  reader->exporter = std::move(exporter);
  std::unique_ptr<config_sdk::MetricReaderConfiguration> model = std::move(reader);

  EXPECT_THROW(config_sdk::MetricsBuilderUtils::CreateMetricReader(registry.get(), model),
               config_sdk::UnsupportedException);
}

TEST(MetricsBuilderWorkflow, ConfigureInstallAndFlush)
{
  auto metric_buffer = std::make_shared<config_test::MetricBuffer>();

  auto registry = std::make_shared<config_sdk::Registry>();
  config_sdk::RegisterDefaultMetricsBuilders(registry.get());
  registry->SetExtensionPushMetricExporterBuilder(
      "recording",
      std::make_unique<config_test::RecordingPushMetricExporterBuilder>(metric_buffer));
  registry->SetPeriodicMetricReaderBuilder(
      std::make_unique<config_test::SyncPeriodicMetricReaderBuilder>());

  auto exporter    = std::make_unique<config_sdk::ExtensionPushMetricExporterConfiguration>();
  exporter->name   = "recording";
  auto reader      = std::make_unique<config_sdk::PeriodicMetricReaderConfiguration>();
  reader->exporter = std::move(exporter);
  auto mp_config   = std::make_unique<config_sdk::MeterProviderConfiguration>();
  mp_config->readers.emplace_back(std::move(reader));
  auto model            = std::make_unique<config_sdk::Configuration>();
  model->meter_provider = std::move(mp_config);

  auto sdk = config_sdk::ConfiguredSdk::Create(registry, model);
  ASSERT_NE(sdk, nullptr);
  EXPECT_EQ(sdk->tracer_provider, nullptr);
  EXPECT_EQ(sdk->logger_provider, nullptr);
  ASSERT_NE(sdk->meter_provider, nullptr);
  sdk->Install();

  auto meter   = opentelemetry::metrics::Provider::GetMeterProvider()->GetMeter("test");
  auto counter = meter->CreateUInt64Counter("test.counter");
  counter->Add(1, {});

  ASSERT_TRUE(sdk->meter_provider->ForceFlush(std::chrono::milliseconds(5000)));
  EXPECT_FALSE(metric_buffer->empty());

  sdk->UnInstall();
}
