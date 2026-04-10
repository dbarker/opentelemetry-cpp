// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/otlp/otlp_metric_utils.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h"  // IWYU pragma: keep
#include "google/protobuf/arena.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h"  // IWYU pragma: keep
// clang-format on

namespace
{

using opentelemetry::common::SystemTimestamp;
namespace metrics_sdk = opentelemetry::sdk::metrics;

static SystemTimestamp NowTs()
{
  return SystemTimestamp(std::chrono::system_clock::now());
}

const opentelemetry::sdk::resource::Resource &TestResource()
{
  static const auto r = opentelemetry::sdk::resource::Resource::Create(
      {{"service.name", opentelemetry::nostd::string_view{"bench-service"}},
       {"service.version", opentelemetry::nostd::string_view{"1.0.0"}}});
  return r;
}

const opentelemetry::sdk::instrumentationscope::InstrumentationScope &TestScope()
{
  static auto scope = opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create(
      "benchmark_scope", "1.0.0", "https://opentelemetry.io/schemas/1.24.0",
      {{"scope.source", "benchmark"}});
  return *scope;
}

static metrics_sdk::PointDataAttributes MakeSumPointData(std::size_t i, bool is_monotonic)
{
  metrics_sdk::SumPointData point;
  point.value_        = static_cast<double>(i) + 1.0;
  point.is_monotonic_ = is_monotonic;
  metrics_sdk::PointDataAttributes data;
  data.attributes = opentelemetry::sdk::common::OrderedAttributeMap{
      {{"point.attribute1", "string_value"}, {"point.attribute2", 123}}};
  data.point_data = point;
  return data;
}

static metrics_sdk::PointDataAttributes MakeLastValuePointData(std::size_t i)
{
  metrics_sdk::LastValuePointData point;
  point.value_              = static_cast<double>(i) + 1.0;
  point.is_lastvalue_valid_ = true;
  point.sample_ts_          = NowTs();
  metrics_sdk::PointDataAttributes data;
  data.attributes = opentelemetry::sdk::common::OrderedAttributeMap{
      {{"point.attribute1", "string_value"}, {"point.attribute2", 123}}};
  data.point_data = std::move(point);
  return data;
}

static metrics_sdk::PointDataAttributes MakeHistogramPointData(std::size_t i)
{
  // SDK default boundaries (15 boundaries, 16 buckets) as set by HistogramAggregation.
  static const std::vector<double> kBoundaries = {0.0,    5.0,    10.0,   25.0,   50.0,
                                                  75.0,   100.0,  250.0,  500.0,  750.0,
                                                  1000.0, 2500.0, 5000.0, 7500.0, 10000.0};
  static const std::vector<uint64_t> kCounts   = {0, 1, 3, 5, 8, 6, 4, 3, 2, 2, 1, 1, 1, 0, 0, 0};
  metrics_sdk::HistogramPointData point;
  point.boundaries_ = kBoundaries;
  point.counts_     = kCounts;
  point.sum_        = static_cast<double>(i) * 10.0;
  point.count_      = i + 37;
  point.min_        = 1.0;
  point.max_        = static_cast<double>(i) * 10.0 + 100.0;
  metrics_sdk::PointDataAttributes data;
  data.attributes = opentelemetry::sdk::common::OrderedAttributeMap{
      {{"point.attribute1", "string_value"}, {"point.attribute2", 123}}};
  data.point_data = std::move(point);
  return data;
}

static metrics_sdk::PointDataAttributes MakeExponentialHistogramPointData(std::size_t i)
{
  static const std::size_t kMaxBuckets = 160;
  metrics_sdk::Base2ExponentialHistogramPointData point;
  point.sum_            = static_cast<double>(i) * 10.0;
  point.count_          = i + 8;
  point.min_            = 1.0;
  point.max_            = static_cast<double>(i) * 10.0 + 100.0;
  point.scale_          = 0;
  point.zero_count_     = 0;
  point.record_min_max_ = true;
  point.max_buckets_    = kMaxBuckets;
  point.positive_buckets_ =
      std::make_unique<opentelemetry::sdk::metrics::AdaptingCircularBufferCounter>(kMaxBuckets);
  point.negative_buckets_ =
      std::make_unique<opentelemetry::sdk::metrics::AdaptingCircularBufferCounter>(kMaxBuckets);
  // Populate half the positive bucket range — realistic for a well-distributed workload.
  for (int32_t b = 0; b < 80; ++b)
  {
    point.positive_buckets_->Increment(b, static_cast<uint64_t>(b + 1));
  }
  metrics_sdk::PointDataAttributes data;
  data.attributes = opentelemetry::sdk::common::OrderedAttributeMap{
      {{"point.attribute1", "string_value"}, {"point.attribute2", 123}}};
  data.point_data = std::move(point);
  return data;
}

// Build ResourceMetrics for a given instrument type and point-data factory.
template <typename PointDataFactory>
static metrics_sdk::ResourceMetrics MakeResourceMetrics(std::size_t n_points,
                                                        const char *name,
                                                        const char *unit,
                                                        metrics_sdk::InstrumentType type,
                                                        metrics_sdk::InstrumentValueType vtype,
                                                        PointDataFactory &&make_data)
{
  metrics_sdk::MetricData data;
  data.start_ts                = NowTs();
  data.end_ts                  = NowTs();
  data.aggregation_temporality = metrics_sdk::AggregationTemporality::kCumulative;
  data.instrument_descriptor   = {name, "", unit, type, vtype};
  data.point_data_attr_.reserve(n_points);
  for (std::size_t i = 0; i < n_points; ++i)
  {
    data.point_data_attr_.push_back(make_data(i));
  }

  metrics_sdk::ScopeMetrics scope;
  scope.scope_       = &TestScope();
  scope.metric_data_ = {std::move(data)};

  metrics_sdk::ResourceMetrics rm;
  rm.resource_          = &TestResource();
  rm.scope_metric_data_ = {std::move(scope)};
  return rm;
}

google::protobuf::ArenaOptions CreateArenaOptions()
{
  google::protobuf::ArenaOptions opts;
  opts.initial_block_size = 1024;
  opts.max_block_size     = 65536;
  return opts;
}

static void RunPopulateRequestBenchmark(benchmark::State &state,
                                        const metrics_sdk::ResourceMetrics &data)
{
  const auto arena_options = CreateArenaOptions();
  for (auto _ : state)
  {
    google::protobuf::Arena arena{arena_options};
    auto *request = google::protobuf::Arena::Create<
        opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest>(&arena);
    opentelemetry::exporter::otlp::OtlpMetricUtils::PopulateRequest(data, request);
    benchmark::DoNotOptimize(request);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

}  // namespace

// ---------------------------------------------------------------------------
// Counter  (sync monotonic sum)
// ---------------------------------------------------------------------------
static void BM_PopulateRequest_Counter(benchmark::State &state)
{
  const auto data = MakeResourceMetrics(
      static_cast<std::size_t>(state.range(0)), "requests", "1",
      metrics_sdk::InstrumentType::kCounter, metrics_sdk::InstrumentValueType::kDouble,
      [](std::size_t i) { return MakeSumPointData(i, /*is_monotonic=*/true); });
  RunPopulateRequestBenchmark(state, data);
}
BENCHMARK(BM_PopulateRequest_Counter)
    ->ArgName("point_count")
    ->Arg(1)
    ->Arg(100)
    ->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// Histogram  (explicit bucket histogram)
// ---------------------------------------------------------------------------
static void BM_PopulateRequest_Histogram(benchmark::State &state)
{
  const auto data = MakeResourceMetrics(static_cast<std::size_t>(state.range(0)), "latency", "ms",
                                        metrics_sdk::InstrumentType::kHistogram,
                                        metrics_sdk::InstrumentValueType::kDouble,
                                        [](std::size_t i) { return MakeHistogramPointData(i); });
  RunPopulateRequestBenchmark(state, data);
}
BENCHMARK(BM_PopulateRequest_Histogram)
    ->ArgName("point_count")
    ->Arg(1)
    ->Arg(100)
    ->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// ExponentialHistogram  (base-2 exponential bucket histogram)
// ---------------------------------------------------------------------------
static void BM_PopulateRequest_ExponentialHistogram(benchmark::State &state)
{
  const auto data = MakeResourceMetrics(
      static_cast<std::size_t>(state.range(0)), "latency_exp", "ms",
      metrics_sdk::InstrumentType::kHistogram, metrics_sdk::InstrumentValueType::kDouble,
      [](std::size_t i) { return MakeExponentialHistogramPointData(i); });
  RunPopulateRequestBenchmark(state, data);
}
BENCHMARK(BM_PopulateRequest_ExponentialHistogram)
    ->ArgName("point_count")
    ->Arg(1)
    ->Arg(100)
    ->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// Gauge  (sync last-value)
// ---------------------------------------------------------------------------
static void BM_PopulateRequest_Gauge(benchmark::State &state)
{
  const auto data = MakeResourceMetrics(static_cast<std::size_t>(state.range(0)), "cpu_usage", "1",
                                        metrics_sdk::InstrumentType::kGauge,
                                        metrics_sdk::InstrumentValueType::kDouble,
                                        [](std::size_t i) { return MakeLastValuePointData(i); });
  RunPopulateRequestBenchmark(state, data);
}
BENCHMARK(BM_PopulateRequest_Gauge)
    ->ArgName("point_count")
    ->Arg(1)
    ->Arg(100)
    ->Unit(benchmark::kMicrosecond);

int main(int argc, char **argv)
{
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
