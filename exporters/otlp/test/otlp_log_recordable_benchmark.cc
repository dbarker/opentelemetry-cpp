// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/exporters/otlp/otlp_log_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/logs/logger.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/processor.h"
#include "opentelemetry/sdk/logs/read_write_log_record.h"
#include "opentelemetry/sdk/logs/recordable.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h"  // IWYU pragma: keep
#include "google/protobuf/arena.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h"  // IWYU pragma: keep
// clang-format on

namespace otlp      = opentelemetry::exporter::otlp;
namespace logs_api  = opentelemetry::logs;
namespace logs_sdk  = opentelemetry::sdk::logs;
namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;

namespace
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
constexpr uint32_t kMaxLogAttrs  = 128;  // OTel SDK default log-record attribute limit
constexpr int      kBatchSmall   = 1;    // single-record export
constexpr int      kBatchDefault = 100;  // realistic throughput batch
constexpr int      kBatchMax     = 512;  // batch processor default

// Backing arrays shared by RecordMaxLog for zero-allocation attribute cycles.
static constexpr bool                          kMaxBoolArr[]   = {true, false, true, false};
static constexpr int64_t                       kMaxInt64Arr[]  = {1, 2, 3, 4};
static constexpr double                        kMaxDoubleArr[] = {1.1, 2.2, 3.3, 4.4};
static const opentelemetry::nostd::string_view kMaxStrArr[]    = {"a", "b", "c", "d"};

// Long-string variants — all exceed 22 chars (safe past SSO on libstdc++, libc++, MSVC).
// Used to isolate heap-allocation cost for string-typed attributes.
static const opentelemetry::nostd::string_view kMaxLongStrValue{"long-string-value-no-sso-12345"};
static const opentelemetry::nostd::string_view kMaxLongStrArr[] = {
    "long-array-val-0-no-sso-12345", "long-array-val-1-no-sso-12345",
    "long-array-val-2-no-sso-12345", "long-array-val-3-no-sso-12345"};

// ---------------------------------------------------------------------------
// TestLogProcessor — buffers OnEmit recordables, provides Export/Clear.
// Templated on the concrete recordable so MakeRecordable returns the right type.
// ---------------------------------------------------------------------------
template <typename RecordableT>
class TestLogProcessor final : public logs_sdk::LogRecordProcessor
{
public:
  using Buffer   = std::vector<std::unique_ptr<logs_sdk::Recordable>>;
  using ExportFn = std::function<void(Buffer &)>;

  explicit TestLogProcessor(ExportFn callback = {}) noexcept
      : export_callback_(std::move(callback))
  {}

  std::unique_ptr<logs_sdk::Recordable> MakeRecordable() noexcept override
  {
    return std::unique_ptr<logs_sdk::Recordable>(new RecordableT());
  }

  void OnEmit(std::unique_ptr<logs_sdk::Recordable> &&record) noexcept override
  {
    buffer_.emplace_back(std::move(record));
  }

  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }

  // Run export callback (PopulateRequest + arena destruct), then drain the buffer.
  void Export() noexcept
  {
    if (export_callback_)
    {
      export_callback_(buffer_);
    }
    buffer_.clear();
  }

  // Drop buffered recordables without exporting (used during PauseTiming).
  void Clear() noexcept { buffer_.clear(); }

  std::size_t Size() const noexcept { return buffer_.size(); }

private:
  Buffer   buffer_;
  ExportFn export_callback_;
};

// ---------------------------------------------------------------------------
// Provider factory
// ---------------------------------------------------------------------------
template <typename RecordableT>
struct LogProviderHandle
{
  std::shared_ptr<logs_sdk::LoggerProvider>              provider;
  TestLogProcessor<RecordableT>                         *processor = nullptr;
  opentelemetry::nostd::shared_ptr<logs_api::Logger>     logger;
};

const opentelemetry::sdk::resource::Resource &NominalResource()
{
  static const auto r = opentelemetry::sdk::resource::Resource::Create(
      {{"service.name", opentelemetry::nostd::string_view{"bench-service"}},
       {"service.version", opentelemetry::nostd::string_view{"1.2.3"}},
       {"host.name", opentelemetry::nostd::string_view{"prod-host-01"}},
       {"deployment.environment", opentelemetry::nostd::string_view{"production"}}});
  return r;
}

template <typename RecordableT, typename ExportFn>
LogProviderHandle<RecordableT> MakeLogProvider(ExportFn callback)
{
  auto  processor     = std::make_unique<TestLogProcessor<RecordableT>>(std::move(callback));
  auto *processor_raw = processor.get();
  auto  provider =
      std::make_shared<logs_sdk::LoggerProvider>(std::move(processor), NominalResource());
  auto logger = provider->GetLogger("bench-logger", "benchmark_scope", "1.0.0",
                                    "https://opentelemetry.io/schemas/1.24.0");
  return {std::move(provider), processor_raw, std::move(logger)};
}

// ---------------------------------------------------------------------------
// Null span exporter — for wiring a TracerProvider that puts spans into the
// thread-local context so log benchmarks can measure in-span-context recording.
// ---------------------------------------------------------------------------
class NullSpanExporter final : public trace_sdk::SpanExporter
{
public:
  std::unique_ptr<trace_sdk::Recordable> MakeRecordable() noexcept override
  {
    return std::make_unique<otlp::OtlpRecordable>();
  }

  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<std::unique_ptr<trace_sdk::Recordable>> &) noexcept override
  {
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }

  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }
};

opentelemetry::nostd::shared_ptr<trace_api::Tracer> MakeNullTracer()
{
  auto exporter   = std::make_unique<NullSpanExporter>();
  auto processor  = std::make_unique<trace_sdk::SimpleSpanProcessor>(std::move(exporter));
  auto provider   = std::make_shared<trace_sdk::TracerProvider>(std::move(processor));
  return provider->GetTracer("bench-tracer");
}

// ---------------------------------------------------------------------------
// Log context modes — controls whether an active span is present in the
// thread-local context during recording (the common production case).
// ---------------------------------------------------------------------------
enum class LogContextMode
{
  kNone,
  kWithSpan,
};

struct LogContextSetup
{
  opentelemetry::nostd::shared_ptr<trace_api::Span>            span;
  opentelemetry::nostd::unique_ptr<opentelemetry::context::Token> token;
};

LogContextSetup SetupLogContext(LogContextMode mode)
{
  LogContextSetup setup;
  if (mode == LogContextMode::kWithSpan)
  {
    static auto tracer = MakeNullTracer();
    setup.span         = tracer->StartSpan("benchmark-span");
    auto ctx           = opentelemetry::context::RuntimeContext::GetCurrent();
    ctx                = trace_api::SetSpan(ctx, setup.span);
    setup.token        = opentelemetry::context::RuntimeContext::Attach(ctx);
  }
  return setup;
}

// ---------------------------------------------------------------------------
// Attribute key pool — pre-built to exclude string-formatting cost from timing.
// ---------------------------------------------------------------------------
const std::vector<std::string> &MaxAttrKeys()
{
  static const std::vector<std::string> keys = []() {
    std::vector<std::string> v;
    v.reserve(kMaxLogAttrs);
    for (uint32_t i = 0; i < kMaxLogAttrs; ++i)
    {
      v.push_back("attr." + std::to_string(i));
    }
    return v;
  }();
  return keys;
}

// ---------------------------------------------------------------------------
// Arena factory — matches the settings used by production OTLP exporters.
// ---------------------------------------------------------------------------
std::unique_ptr<google::protobuf::Arena> CreateArena()
{
  google::protobuf::ArenaOptions opts;
  opts.initial_block_size = 1024;
  opts.max_block_size     = 65536;
  return std::make_unique<google::protobuf::Arena>(opts);
}

// ---------------------------------------------------------------------------
// Export callbacks — build the export request arena + PopulateRequest then
// let everything destruct inside the timed region, mirroring what the real
// OTLP exporter does on the export thread.
// ---------------------------------------------------------------------------
using LogBuffer = std::vector<std::unique_ptr<logs_sdk::Recordable>>;

void OtlpLogExportCallback(LogBuffer &buffer) noexcept
{
  auto  arena   = CreateArena();
  auto *request = google::protobuf::Arena::Create<
      opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest>(arena.get());
  otlp::OtlpRecordableUtils::PopulateRequest(
      opentelemetry::nostd::span<std::unique_ptr<logs_sdk::Recordable>>{buffer.data(),
                                                                        buffer.size()},
      request);
  benchmark::DoNotOptimize(request);
}

void LogDataExportCallback(LogBuffer &buffer) noexcept
{
  auto  arena   = CreateArena();
  auto *request = google::protobuf::Arena::Create<
      opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest>(arena.get());
  otlp::OtlpRecordableUtils::PopulateRequestLogData(
      opentelemetry::nostd::span<std::unique_ptr<logs_sdk::Recordable>>{buffer.data(),
                                                                        buffer.size()},
      request);
  benchmark::DoNotOptimize(request);
}

// ---------------------------------------------------------------------------
// Log shapes — drive recording through the public Logger API only.
// ---------------------------------------------------------------------------

// Nominal: 4 mixed-type attributes + short string body.
// Mirrors a typical structured application log.
inline void RecordNominalLog(logs_api::Logger &logger)
{
  static const opentelemetry::nostd::string_view kBody{"bench log message"};
  static const opentelemetry::nostd::string_view kSvc{"bench-service"};
  static const opentelemetry::nostd::string_view kUrl{"https://example.com/api/v1"};

  auto record = logger.CreateLogRecord();
  record->SetBody(opentelemetry::common::AttributeValue{kBody});
  record->SetSeverity(logs_api::Severity::kInfo);
  record->SetAttribute("service.name",
                       opentelemetry::common::AttributeValue{kSvc});
  record->SetAttribute("http.status_code",
                       opentelemetry::common::AttributeValue{static_cast<int64_t>(200)});
  record->SetAttribute("error", opentelemetry::common::AttributeValue{false});
  record->SetAttribute("http.url", opentelemetry::common::AttributeValue{kUrl});
  logger.EmitLogRecord(std::move(record));
}

// Max: kMaxLogAttrs attributes with the same 8-way type cycle as the span benchmark.
inline void RecordMaxLog(logs_api::Logger &logger)
{
  static const opentelemetry::nostd::string_view kBody{"bench log message"};
  const auto                                    &keys = MaxAttrKeys();

  auto record = logger.CreateLogRecord();
  record->SetBody(opentelemetry::common::AttributeValue{kBody});
  record->SetSeverity(logs_api::Severity::kWarn);

  for (uint32_t i = 0; i < kMaxLogAttrs; ++i)
  {
    switch (i & 0x7)
    {
      case 0:
        record->SetAttribute(keys[i],
                             opentelemetry::common::AttributeValue{
                                 opentelemetry::nostd::string_view{"value"}});
        break;
      case 1:
        record->SetAttribute(keys[i],
                             opentelemetry::common::AttributeValue{static_cast<int64_t>(i)});
        break;
      case 2:
        record->SetAttribute(keys[i],
                             opentelemetry::common::AttributeValue{(i & 1) != 0});
        break;
      case 3:
        record->SetAttribute(keys[i],
                             opentelemetry::common::AttributeValue{static_cast<double>(i) * 0.5});
        break;
      case 4:
        record->SetAttribute(
            keys[i], opentelemetry::common::AttributeValue{
                         opentelemetry::nostd::span<const bool>{kMaxBoolArr}});
        break;
      case 5:
        record->SetAttribute(
            keys[i], opentelemetry::common::AttributeValue{
                         opentelemetry::nostd::span<const int64_t>{kMaxInt64Arr}});
        break;
      case 6:
        record->SetAttribute(
            keys[i], opentelemetry::common::AttributeValue{
                         opentelemetry::nostd::span<const double>{kMaxDoubleArr}});
        break;
      default:
        record->SetAttribute(
            keys[i],
            opentelemetry::common::AttributeValue{
                opentelemetry::nostd::span<const opentelemetry::nostd::string_view>{kMaxStrArr}});
        break;
    }
  }

  logger.EmitLogRecord(std::move(record));
}

// Long-string Max: same 8-way type cycle but case 0 / case 7 use strings > SSO threshold.
// Isolates heap-allocation overhead for string-typed attributes.
inline void RecordMaxLogLongStr(logs_api::Logger &logger)
{
  static const opentelemetry::nostd::string_view kBody{"bench log message"};
  const auto                                    &keys = MaxAttrKeys();

  auto record = logger.CreateLogRecord();
  record->SetBody(opentelemetry::common::AttributeValue{kBody});
  record->SetSeverity(logs_api::Severity::kWarn);

  for (uint32_t i = 0; i < kMaxLogAttrs; ++i)
  {
    switch (i & 0x7)
    {
      case 0:
        record->SetAttribute(keys[i], opentelemetry::common::AttributeValue{kMaxLongStrValue});
        break;
      case 1:
        record->SetAttribute(keys[i],
                             opentelemetry::common::AttributeValue{static_cast<int64_t>(i)});
        break;
      case 2:
        record->SetAttribute(keys[i],
                             opentelemetry::common::AttributeValue{(i & 1) != 0});
        break;
      case 3:
        record->SetAttribute(keys[i],
                             opentelemetry::common::AttributeValue{static_cast<double>(i) * 0.5});
        break;
      case 4:
        record->SetAttribute(
            keys[i], opentelemetry::common::AttributeValue{
                         opentelemetry::nostd::span<const bool>{kMaxBoolArr}});
        break;
      case 5:
        record->SetAttribute(
            keys[i], opentelemetry::common::AttributeValue{
                         opentelemetry::nostd::span<const int64_t>{kMaxInt64Arr}});
        break;
      case 6:
        record->SetAttribute(
            keys[i], opentelemetry::common::AttributeValue{
                         opentelemetry::nostd::span<const double>{kMaxDoubleArr}});
        break;
      default:
        record->SetAttribute(
            keys[i],
            opentelemetry::common::AttributeValue{
                opentelemetry::nostd::span<const opentelemetry::nostd::string_view>{
                    kMaxLongStrArr}});
        break;
    }
  }

  logger.EmitLogRecord(std::move(record));
}

// ---------------------------------------------------------------------------
// BM_LogRecord_Impl — measure CreateLogRecord + Set* + EmitLogRecord.
// Recordable destruction is excluded via PauseTiming / Clear.
// ---------------------------------------------------------------------------
template <typename RecordableT, typename ShapeFn>
void BM_LogRecord_Impl(benchmark::State &state, ShapeFn shape, LogContextMode mode,
                       int64_t attrs_per_record = 0)
{
  auto handle    = MakeLogProvider<RecordableT>(typename TestLogProcessor<RecordableT>::ExportFn{});
  auto ctx_setup = SetupLogContext(mode);

  for (auto _ : state)
  {
    shape(*handle.logger);
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }

  if (attrs_per_record > 0)
  {
    // records/sec: throughput at this log record shape.
    // Time column is the per-record cost directly.
    state.counters["records/sec"] = benchmark::Counter(
        static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
  }
}

// ---------------------------------------------------------------------------
// BM_LogExport_Impl — measure Export (PopulateRequest + arena + destruction).
// Recording is done during PauseTiming.
// ---------------------------------------------------------------------------
template <typename RecordableT, typename ShapeFn, typename ExportFn>
void BM_LogExport_Impl(benchmark::State &state, ShapeFn shape, ExportFn export_cb)
{
  const std::size_t n      = static_cast<std::size_t>(state.range(0));
  auto              handle = MakeLogProvider<RecordableT>(std::move(export_cb));

  for (auto _ : state)
  {
    state.PauseTiming();
    for (std::size_t i = 0; i < n; ++i)
    {
      shape(*handle.logger);
    }
    state.ResumeTiming();

    handle.processor->Export();  // measured: callback + arena + destruction
  }

  state.counters["time/record"] = benchmark::Counter(
      static_cast<double>(state.iterations() * state.range(0)),
      benchmark::Counter::kIsRate | benchmark::Counter::kInvert,
      benchmark::Counter::kIs1000);
}

// ===========================================================================
// Record benchmarks
// ===========================================================================

// --- Baseline: emit the smallest possible log record ----------------------

static void BM_Record_Empty_OtlpLogRecordable(benchmark::State &state)
{
  auto handle = MakeLogProvider<otlp::OtlpLogRecordable>(
      typename TestLogProcessor<otlp::OtlpLogRecordable>::ExportFn{});
  for (auto _ : state)
  {
    auto record = handle.logger->CreateLogRecord();
    record->SetSeverity(logs_api::Severity::kInfo);
    handle.logger->EmitLogRecord(std::move(record));
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }
}
BENCHMARK(BM_Record_Empty_OtlpLogRecordable)->Unit(benchmark::kNanosecond);

static void BM_Record_Empty_ReadWriteLogRecord(benchmark::State &state)
{
  auto handle = MakeLogProvider<logs_sdk::ReadWriteLogRecord>(
      typename TestLogProcessor<logs_sdk::ReadWriteLogRecord>::ExportFn{});
  for (auto _ : state)
  {
    auto record = handle.logger->CreateLogRecord();
    record->SetSeverity(logs_api::Severity::kInfo);
    handle.logger->EmitLogRecord(std::move(record));
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }
}
BENCHMARK(BM_Record_Empty_ReadWriteLogRecord)->Unit(benchmark::kNanosecond);

// --- Nominal × Recordable × ContextMode matrix ----------------------------

#define DEFINE_RECORD_NOMINAL_BENCH(Name, RecordableT, Mode)                   \
  static void BM_Record_Nominal_##Name(benchmark::State &state)                \
  {                                                                             \
    BM_LogRecord_Impl<RecordableT>(state, RecordNominalLog, Mode);             \
  }                                                                             \
  BENCHMARK(BM_Record_Nominal_##Name)->Unit(benchmark::kNanosecond)

DEFINE_RECORD_NOMINAL_BENCH(OtlpLogRecordable_NoContext,
                            otlp::OtlpLogRecordable,
                            LogContextMode::kNone);
DEFINE_RECORD_NOMINAL_BENCH(OtlpLogRecordable_WithContext,
                            otlp::OtlpLogRecordable,
                            LogContextMode::kWithSpan);
DEFINE_RECORD_NOMINAL_BENCH(ReadWriteLogRecord_NoContext,
                            logs_sdk::ReadWriteLogRecord,
                            LogContextMode::kNone);
DEFINE_RECORD_NOMINAL_BENCH(ReadWriteLogRecord_WithContext,
                            logs_sdk::ReadWriteLogRecord,
                            LogContextMode::kWithSpan);

#undef DEFINE_RECORD_NOMINAL_BENCH

// --- Max shape × Recordable -----------------------------------------------

static void BM_Record_Max_OtlpLogRecordable(benchmark::State &state)
{
  BM_LogRecord_Impl<otlp::OtlpLogRecordable>(state, RecordMaxLog, LogContextMode::kNone,
                                             kMaxLogAttrs);
}
BENCHMARK(BM_Record_Max_OtlpLogRecordable)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_ReadWriteLogRecord(benchmark::State &state)
{
  BM_LogRecord_Impl<logs_sdk::ReadWriteLogRecord>(state, RecordMaxLog, LogContextMode::kNone,
                                                  kMaxLogAttrs);
}
BENCHMARK(BM_Record_Max_ReadWriteLogRecord)->Unit(benchmark::kMicrosecond);

// --- Max long-string shape × Recordable (SSO vs heap) ---------------------

static void BM_Record_Max_OtlpLogRecordable_LongStr(benchmark::State &state)
{
  BM_LogRecord_Impl<otlp::OtlpLogRecordable>(state, RecordMaxLogLongStr, LogContextMode::kNone,
                                             kMaxLogAttrs);
}
BENCHMARK(BM_Record_Max_OtlpLogRecordable_LongStr)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_ReadWriteLogRecord_LongStr(benchmark::State &state)
{
  BM_LogRecord_Impl<logs_sdk::ReadWriteLogRecord>(state, RecordMaxLogLongStr,
                                                  LogContextMode::kNone, kMaxLogAttrs);
}
BENCHMARK(BM_Record_Max_ReadWriteLogRecord_LongStr)->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// Attribute-count sweep — measures total cost of one log record with N attrs.
// Time column = cost per record directly; use attrs:N to look up a known shape.
// Uses the same 8-way mixed-type cycle as RecordMaxLog.
// ---------------------------------------------------------------------------
template <typename RecordableT>
void BM_Record_AttrCount_Impl(benchmark::State &state)
{
  const uint32_t n    = static_cast<uint32_t>(state.range(0));
  const auto    &keys = MaxAttrKeys();
  auto           handle =
      MakeLogProvider<RecordableT>(typename TestLogProcessor<RecordableT>::ExportFn{});

  for (auto _ : state)
  {
    auto record = handle.logger->CreateLogRecord();
    record->SetSeverity(logs_api::Severity::kInfo);
    for (uint32_t i = 0; i < n; ++i)
    {
      switch (i & 0x7)
      {
        case 0:
          record->SetAttribute(
              keys[i], opentelemetry::common::AttributeValue{
                           opentelemetry::nostd::string_view{"value"}});
          break;
        case 1:
          record->SetAttribute(
              keys[i],
              opentelemetry::common::AttributeValue{static_cast<int64_t>(i)});
          break;
        case 2:
          record->SetAttribute(keys[i],
                               opentelemetry::common::AttributeValue{(i & 1) != 0});
          break;
        case 3:
          record->SetAttribute(
              keys[i],
              opentelemetry::common::AttributeValue{static_cast<double>(i) * 0.5});
          break;
        case 4:
          record->SetAttribute(
              keys[i], opentelemetry::common::AttributeValue{
                           opentelemetry::nostd::span<const bool>{kMaxBoolArr}});
          break;
        case 5:
          record->SetAttribute(
              keys[i], opentelemetry::common::AttributeValue{
                           opentelemetry::nostd::span<const int64_t>{kMaxInt64Arr}});
          break;
        case 6:
          record->SetAttribute(
              keys[i], opentelemetry::common::AttributeValue{
                           opentelemetry::nostd::span<const double>{kMaxDoubleArr}});
          break;
        default:
          record->SetAttribute(
              keys[i],
              opentelemetry::common::AttributeValue{
                  opentelemetry::nostd::span<const opentelemetry::nostd::string_view>{
                      kMaxStrArr}});
          break;
      }
    }
    handle.logger->EmitLogRecord(std::move(record));
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }

  // records/sec: throughput at this log record shape.  Time column is cost-per-record.
  state.counters["records/sec"] = benchmark::Counter(
      static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
}

// ---------------------------------------------------------------------------
// Marginal attribute cost sweep — CreateLogRecord and EmitLogRecord are outside
// the timed region so Time measures *only* the SetAttribute loop.
// Sweep: 1, 4, 8, 16, 24, 32, 48, 64, 96, 128
#define ATTR_COUNT_ARGS \
  ->Arg(1)->Arg(4)->Arg(8)->Arg(16)->Arg(24)->Arg(32)->Arg(48)->Arg(64)->Arg(96)->Arg(128)

static void BM_Record_AttrCount_OtlpLogRecordable(benchmark::State &state)
{
  BM_Record_AttrCount_Impl<otlp::OtlpLogRecordable>(state);
}
BENCHMARK(BM_Record_AttrCount_OtlpLogRecordable)
    ->ArgName("attrs")
    ATTR_COUNT_ARGS->Unit(benchmark::kMicrosecond);

static void BM_Record_AttrCount_ReadWriteLogRecord(benchmark::State &state)
{
  BM_Record_AttrCount_Impl<logs_sdk::ReadWriteLogRecord>(state);
}
BENCHMARK(BM_Record_AttrCount_ReadWriteLogRecord)
    ->ArgName("attrs")
    ATTR_COUNT_ARGS->Unit(benchmark::kMicrosecond);

#undef ATTR_COUNT_ARGS

// ===========================================================================
// Export benchmarks
// ===========================================================================

static void BM_Export_Nominal_OtlpLogRecordable(benchmark::State &state)
{
  BM_LogExport_Impl<otlp::OtlpLogRecordable>(state, RecordNominalLog, OtlpLogExportCallback);
}
BENCHMARK(BM_Export_Nominal_OtlpLogRecordable)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchDefault)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

static void BM_Export_Nominal_ReadWriteLogRecord(benchmark::State &state)
{
  BM_LogExport_Impl<logs_sdk::ReadWriteLogRecord>(state, RecordNominalLog, LogDataExportCallback);
}
BENCHMARK(BM_Export_Nominal_ReadWriteLogRecord)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchDefault)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

static void BM_Export_Max_OtlpLogRecordable(benchmark::State &state)
{
  BM_LogExport_Impl<otlp::OtlpLogRecordable>(state, RecordMaxLog, OtlpLogExportCallback);
}
BENCHMARK(BM_Export_Max_OtlpLogRecordable)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchDefault)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

static void BM_Export_Max_ReadWriteLogRecord(benchmark::State &state)
{
  BM_LogExport_Impl<logs_sdk::ReadWriteLogRecord>(state, RecordMaxLog, LogDataExportCallback);
}
BENCHMARK(BM_Export_Max_ReadWriteLogRecord)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchDefault)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

// --- Max long-string export (SSO vs heap) ---------------------------------

static void BM_Export_Max_OtlpLogRecordable_LongStr(benchmark::State &state)
{
  BM_LogExport_Impl<otlp::OtlpLogRecordable>(state, RecordMaxLogLongStr, OtlpLogExportCallback);
}
BENCHMARK(BM_Export_Max_OtlpLogRecordable_LongStr)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchDefault)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

static void BM_Export_Max_ReadWriteLogRecord_LongStr(benchmark::State &state)
{
  BM_LogExport_Impl<logs_sdk::ReadWriteLogRecord>(state, RecordMaxLogLongStr,
                                                  LogDataExportCallback);
}
BENCHMARK(BM_Export_Max_ReadWriteLogRecord_LongStr)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchDefault)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

}  // namespace

int main(int argc, char **argv)
{
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}

