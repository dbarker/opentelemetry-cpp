// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/configuration/registry_factory.h"

namespace config_sdk = opentelemetry::sdk::configuration;

TEST(RegistryFactory, FillsAllDefaultSlots)
{
  auto registry = config_sdk::RegistryFactory::Create();

  // trace
  EXPECT_NE(registry->GetAlwaysOnSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetAlwaysOffSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetTraceIdRatioBasedSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetProbabilitySamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetParentBasedSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetJaegerRemoteSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetBatchSpanProcessorBuilder(), nullptr);
  EXPECT_NE(registry->GetSimpleSpanProcessorBuilder(), nullptr);
  EXPECT_NE(registry->GetTracerConfiguratorBuilder(), nullptr);
  EXPECT_NE(registry->GetComposableAlwaysOnSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetComposableAlwaysOffSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetComposableProbabilitySamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetComposableParentThresholdSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetComposableRuleBasedSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetCompositeSamplerBuilder(), nullptr);
  EXPECT_NE(registry->GetTracerProviderBuilder(), nullptr);

  // logs
  EXPECT_NE(registry->GetBatchLogRecordProcessorBuilder(), nullptr);
  EXPECT_NE(registry->GetSimpleLogRecordProcessorBuilder(), nullptr);
  EXPECT_NE(registry->GetLoggerConfiguratorBuilder(), nullptr);
  EXPECT_NE(registry->GetLoggerProviderBuilder(), nullptr);

  // metrics
  EXPECT_NE(registry->GetPeriodicMetricReaderBuilder(), nullptr);
  EXPECT_NE(registry->GetMeterConfiguratorBuilder(), nullptr);
  EXPECT_NE(registry->GetMeterProviderBuilder(), nullptr);

  // propagators
  EXPECT_NE(registry->GetTextMapPropagatorBuilder("tracecontext"), nullptr);
  EXPECT_NE(registry->GetTextMapPropagatorBuilder("baggage"), nullptr);
  EXPECT_NE(registry->GetTextMapPropagatorBuilder("b3"), nullptr);
  EXPECT_NE(registry->GetTextMapPropagatorBuilder("b3multi"), nullptr);
  EXPECT_NE(registry->GetTextMapPropagatorBuilder("jaeger"), nullptr);
}
