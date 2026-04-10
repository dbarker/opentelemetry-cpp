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
#include "opentelemetry/exporters/otlp/otlp_log_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/logs/logger.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/recordable.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h"  // IWYU pragma: keep
#include "google/protobuf/arena.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h"  // IWYU pragma: keep
// clang-format on

namespace
{

// A no-op SpanExporter used solely to back an SDK TracerProvider that puts spans
// into the thread-local context — allowing log benchmarks to emit in-span-context.
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

// A no-op LogRecordExporter whose MakeRecordable creates an OtlpLogRecordable.
// This exercises the full OTLP log recordable population path via the public API.
class NullLogExporter final : public opentelemetry::sdk::logs::LogRecordExporter
{
public:
  std::unique_ptr<opentelemetry::sdk::logs::Recordable> MakeRecordable() noexcept override
  {
    return std::make_unique<opentelemetry::exporter::otlp::OtlpLogRecordable>();
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

// Build a batch of N fully-populated OtlpLogRecordable records for PopulateRequest benchmarks.
std::vector<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> MakeLogBatch(std::size_t n)
{
  constexpr uint8_t kTraceIdBytes[opentelemetry::trace::TraceId::kSize] = {1, 2, 3, 4, 5, 6, 7, 8,
                                                                           1, 2, 3, 4, 5, 6, 7, 8};
  constexpr uint8_t kSpanIdBytes[opentelemetry::trace::SpanId::kSize]   = {1, 2, 3, 4, 5, 6, 7, 8};
  const opentelemetry::trace::TraceId trace_id{kTraceIdBytes};
  const opentelemetry::trace::SpanId span_id{kSpanIdBytes};
  const opentelemetry::trace::TraceFlags flags{opentelemetry::trace::TraceFlags::kIsSampled};
  const auto now = opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now());

  std::vector<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> batch;
  batch.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
  {
    auto rec = std::make_unique<opentelemetry::exporter::otlp::OtlpLogRecordable>();
    rec->SetTimestamp(now);
    rec->SetObservedTimestamp(now);
    rec->SetSeverity(opentelemetry::logs::Severity::kInfo);
    rec->SetBody(opentelemetry::common::AttributeValue{
        opentelemetry::nostd::string_view{"bench log message"}});
    rec->SetTraceId(trace_id);
    rec->SetSpanId(span_id);
    rec->SetTraceFlags(flags);
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
constexpr int kBodyBytesShort    = 128;   // typical structured log message
constexpr int kBodyBytesMedium   = 1024;  // verbose message
constexpr int kBodyBytesLarge    = 4096;  // stack trace / large payload
constexpr int kAttributeCountMin = 1;     // single attribute
constexpr int kAttributeCountMax = 20;    // high-cardinality record
constexpr int kBatchSmall        = 1;     // single record export
constexpr int kBatchLarge        = 100;   // high-throughput export

// Build a logger once and reuse across all benchmark calls.
opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> GetLogger()
{
  static auto logger = []() {
    auto exporter = std::make_unique<NullLogExporter>();
    auto processor =
        std::make_unique<opentelemetry::sdk::logs::SimpleLogRecordProcessor>(std::move(exporter));
    auto provider =
        std::make_shared<opentelemetry::sdk::logs::LoggerProvider>(std::move(processor));
    return provider->GetLogger("bench-logger", "benchmark_scope", "1.0.0",
                               "https://opentelemetry.io/schemas/1.24.0",
                               {{"scope.source", "benchmark"}});
  }();
  return logger;
}

// Build a tracer once and reuse across all benchmark calls.  The underlying
// SDK TracerProvider stores active spans in thread-local context, which the
// logger reads during EmitLogRecord to propagate TraceId/SpanId/TraceFlags.
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

// Baseline: emit a minimal log record with severity and a short message body.
static void BM_Log_EmitMinimal(benchmark::State &state)
{
  auto logger = GetLogger();
  for (auto _ : state)
  {
    logger->EmitLogRecord(opentelemetry::logs::Severity::kInfo,
                          opentelemetry::nostd::string_view{"bench message"});
  }
}
BENCHMARK(BM_Log_EmitMinimal)->Unit(benchmark::kNanosecond);

// Same as BM_Log_EmitMinimal but with an active span on the current thread.
// The SDK reads the thread-local context, extracts TraceId/SpanId/TraceFlags,
// and writes them into the recordable on every call.
// Delta vs BM_Log_EmitMinimal isolates the span-context propagation overhead.
static void BM_Log_EmitInSpanContext(benchmark::State &state)
{
  auto logger = GetLogger();
  auto tracer = GetTracer();
  auto span   = tracer->StartSpan("benchmark-span");
  opentelemetry::trace::Scope scope{span};
  for (auto _ : state)
  {
    logger->EmitLogRecord(opentelemetry::logs::Severity::kInfo,
                          opentelemetry::nostd::string_view{"bench message"});
  }
  span->End();
}
BENCHMARK(BM_Log_EmitInSpanContext)->Unit(benchmark::kNanosecond);

// Measure the cost of emitting a log record with a body of varying byte length.
static void BM_Log_EmitWithBody(benchmark::State &state)
{
  auto logger = GetLogger();
  const std::string body(static_cast<std::size_t>(state.range(0)), 'x');
  for (auto _ : state)
  {
    logger->EmitLogRecord(opentelemetry::logs::Severity::kInfo,
                          opentelemetry::nostd::string_view{body});
  }
}
BENCHMARK(BM_Log_EmitWithBody)
    ->ArgName("body_bytes")
    ->Arg(kBodyBytesShort)
    ->Arg(kBodyBytesMedium)
    ->Arg(kBodyBytesLarge)
    ->Unit(benchmark::kNanosecond);

// Measure the cost of setting N integer attributes on a log record before emitting.
static void BM_Log_EmitWithAttributes(benchmark::State &state)
{
  auto logger      = GetLogger();
  const int n_attr = static_cast<int>(state.range(0));
  const auto &keys = AttrKeys();
  for (auto _ : state)
  {
    auto record = logger->CreateLogRecord();
    record->SetSeverity(opentelemetry::logs::Severity::kInfo);
    for (int i = 0; i < n_attr; ++i)
    {
      record->SetAttribute(opentelemetry::nostd::string_view{keys[i]},
                           opentelemetry::common::AttributeValue{static_cast<int64_t>(i)});
    }
    logger->EmitLogRecord(std::move(record));
  }
  state.SetItemsProcessed(state.iterations() * n_attr);
}
BENCHMARK(BM_Log_EmitWithAttributes)
    ->ArgName("attribute_count")
    ->Arg(kAttributeCountMin)
    ->Arg(kAttributeCountMax)
    ->Unit(benchmark::kNanosecond);

// Benchmark the arena allocation + OtlpRecordableUtils::PopulateRequest for a
// batch of N log records — mirrors the hot path in OtlpGrpcLogRecordExporter::Export.
// Note: PopulateRequest takes ownership (releases) each recordable, so the
// batch must be rebuilt every iteration; timing is paused during the rebuild.
static void BM_PopulateRequest(benchmark::State &state)
{
  const std::size_t n      = static_cast<std::size_t>(state.range(0));
  const auto arena_options = ProductionArenaOptions();
  for (auto _ : state)
  {
    state.PauseTiming();
    auto batch = MakeLogBatch(n);
    state.ResumeTiming();

    google::protobuf::Arena arena{arena_options};
    auto *request = google::protobuf::Arena::Create<
        opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest>(&arena);
    opentelemetry::exporter::otlp::OtlpRecordableUtils::PopulateRequest(
        opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>>{
            batch.data(), batch.size()},
        request);
    benchmark::DoNotOptimize(request);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_PopulateRequest)
    ->ArgName("record_count")
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
