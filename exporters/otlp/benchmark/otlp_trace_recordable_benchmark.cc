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
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h"  // IWYU pragma: keep
#include "google/protobuf/arena.h"
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h"  // IWYU pragma: keep
// clang-format on

namespace
{

// A no-op SpanExporter whose MakeRecordable creates an OtlpRecordable.
// This exercises the full OTLP recordable population path via the public API.
class NullSpanExporter final : public opentelemetry::sdk::trace::SpanExporter
{
public:
  std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override
  {
    return std::make_unique<opentelemetry::exporter::otlp::OtlpRecordable>();
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

// Pre-built attribute keys to avoid measuring string formatting inside benchmarks.
const std::vector<std::string> &AttrKeys()
{
  static const std::vector<std::string> keys = []() {
    std::vector<std::string> v;
    v.reserve(32);
    for (int i = 0; i < 32; ++i)
    {
      v.push_back("attr." + std::to_string(i));
    }
    return v;
  }();
  return keys;
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
  static auto s = opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create(
      "benchmark_scope", "1.0.0", "https://opentelemetry.io/schemas/1.24.0",
      {{"scope.source", "benchmark"}});
  return *s;
}

// Build a batch of N fully-populated OtlpRecordable spans for PopulateRequest benchmarks.
std::vector<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> MakeSpanBatch(std::size_t n)
{
  constexpr uint8_t kTraceIdBytes[opentelemetry::trace::TraceId::kSize] = {1, 2, 3, 4, 5, 6, 7, 8,
                                                                           1, 2, 3, 4, 5, 6, 7, 8};
  constexpr uint8_t kSpanIdBytes[opentelemetry::trace::SpanId::kSize]   = {1, 2, 3, 4, 5, 6, 7, 8};
  constexpr uint8_t kParentIdBytes[opentelemetry::trace::SpanId::kSize] = {8, 7, 6, 5, 4, 3, 2, 1};
  const opentelemetry::trace::TraceId trace_id{kTraceIdBytes};
  const opentelemetry::trace::SpanId span_id{kSpanIdBytes};
  const opentelemetry::trace::SpanId parent_id{kParentIdBytes};
  const opentelemetry::trace::SpanContext ctx{
      trace_id, span_id,
      opentelemetry::trace::TraceFlags{opentelemetry::trace::TraceFlags::kIsSampled}, true};
  const auto now = opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now());

  std::vector<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> batch;
  batch.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
  {
    auto rec = std::make_unique<opentelemetry::exporter::otlp::OtlpRecordable>();
    rec->SetIdentity(ctx, parent_id);
    rec->SetName("bench-span");
    rec->SetSpanKind(opentelemetry::trace::SpanKind::kServer);
    rec->SetStartTime(now);
    rec->SetDuration(std::chrono::milliseconds(5));
    rec->SetResource(TestResource());
    rec->SetInstrumentationScope(TestScope());
    batch.push_back(std::move(rec));
  }
  return batch;
}

google::protobuf::ArenaOptions ProductionArenaOptions()
{
  google::protobuf::ArenaOptions opts;
  opts.initial_block_size = 1024;
  opts.max_block_size     = 65536;
  return opts;
}

// Benchmark argument values.
constexpr int kAttributeCountMin    = 1;     // single attribute
constexpr int kAttributeCountMax    = 20;    // high-cardinality span
constexpr int kAttributeBytesShort  = 8;     // fits in a CPU cache line word
constexpr int kAttributeBytesMedium = 256;   // typical URL / identifier
constexpr int kAttributeBytesLarge  = 1024;  // large string value
constexpr int kBatchSmall           = 1;     // single span export
constexpr int kBatchLarge           = 100;   // high-throughput export

// Build a tracer once and reuse across all benchmark calls.
opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> GetTracer()
{
  static auto tracer = []() {
    auto exporter = std::make_unique<NullSpanExporter>();
    auto processor =
        std::make_unique<opentelemetry::sdk::trace::SimpleSpanProcessor>(std::move(exporter));
    auto provider =
        std::make_shared<opentelemetry::sdk::trace::TracerProvider>(std::move(processor));
    return provider->GetTracer("benchmark_scope", "1.0.0",
                               "https://opentelemetry.io/schemas/1.24.0");
  }();
  return tracer;
}

}  // namespace

// Baseline: start a span and end it immediately with no attributes.
static void BM_Span_StartEnd(benchmark::State &state)
{
  auto tracer = GetTracer();
  for (auto _ : state)
  {
    auto span = tracer->StartSpan("op");
    span->End();
  }
}
BENCHMARK(BM_Span_StartEnd)->Unit(benchmark::kNanosecond);

// Measure the cost of setting N integer attributes on a span before ending it.
static void BM_Span_StartEnd_WithAttributes(benchmark::State &state)
{
  auto tracer      = GetTracer();
  const int n_attr = static_cast<int>(state.range(0));
  const auto &keys = AttrKeys();
  for (auto _ : state)
  {
    auto span = tracer->StartSpan("op");
    for (int i = 0; i < n_attr; ++i)
    {
      span->SetAttribute(opentelemetry::nostd::string_view{keys[i]},
                         opentelemetry::common::AttributeValue{static_cast<int64_t>(i)});
    }
    span->End();
  }
  state.SetItemsProcessed(state.iterations() * n_attr);
}
BENCHMARK(BM_Span_StartEnd_WithAttributes)
    ->ArgName("attribute_count")
    ->Arg(kAttributeCountMin)
    ->Arg(kAttributeCountMax)
    ->Unit(benchmark::kNanosecond);

// Measure the cost of setting a single string attribute of varying byte length.
static void BM_Span_StartEnd_StringAttribute(benchmark::State &state)
{
  auto tracer = GetTracer();
  const std::string value(static_cast<std::size_t>(state.range(0)), 'x');
  for (auto _ : state)
  {
    auto span = tracer->StartSpan("op");
    span->SetAttribute("value", opentelemetry::nostd::string_view{value});
    span->End();
  }
}
BENCHMARK(BM_Span_StartEnd_StringAttribute)
    ->ArgName("attribute_bytes")
    ->Arg(kAttributeBytesShort)
    ->Arg(kAttributeBytesMedium)
    ->Arg(kAttributeBytesLarge)
    ->Unit(benchmark::kNanosecond);

// Benchmark the arena allocation + OtlpRecordableUtils::PopulateRequest for a
// batch of N spans — mirrors the hot path in OtlpGrpcExporter::Export.
// Note: PopulateRequest takes ownership (releases) each recordable, so the
// batch must be rebuilt every iteration; timing is paused during the rebuild.
static void BM_PopulateRequest(benchmark::State &state)
{
  const std::size_t n      = static_cast<std::size_t>(state.range(0));
  const auto arena_options = ProductionArenaOptions();
  for (auto _ : state)
  {
    state.PauseTiming();
    auto batch = MakeSpanBatch(n);
    state.ResumeTiming();

    google::protobuf::Arena arena{arena_options};
    auto *request = google::protobuf::Arena::Create<
        opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest>(&arena);
    opentelemetry::exporter::otlp::OtlpRecordableUtils::PopulateRequest(
        opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>{
            batch.data(), batch.size()},
        request);
    benchmark::DoNotOptimize(request);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_PopulateRequest)
    ->ArgName("span_count")
    ->Arg(kBatchSmall)
    ->Arg(kBatchLarge)
    ->Unit(benchmark::kMicrosecond);

int main(int argc, char **argv)
{
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
