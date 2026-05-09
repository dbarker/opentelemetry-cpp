// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "opentelemetry/baggage/baggage.h"
#include "opentelemetry/baggage/baggage_context.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/exporters/otlp/otlp_populate_attribute_utils.h"
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/span_data.h"
#if defined(__cpp_lib_memory_resource)
#  include "opentelemetry/sdk/trace/pmr_span_data.h"
#endif
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h"  // IWYU pragma: keep
#include "google/protobuf/arena.h"
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h"  // IWYU pragma: keep
// clang-format on

namespace otlp       = opentelemetry::exporter::otlp;
namespace trace_sdk  = opentelemetry::sdk::trace;
namespace trace_api  = opentelemetry::trace;
namespace common_api = opentelemetry::common;
#if defined(__cpp_lib_memory_resource)
using PmrSpanData = opentelemetry::sdk::trace::PmrSpanData;
#endif

namespace
{

// ---------------------------------------------------------------------------
// Constants — span shape limits and benchmark batch sizes.
// ---------------------------------------------------------------------------
constexpr uint32_t kMaxAttrs           = 128;  // OTel SDK default span-attr limit
constexpr uint32_t kMaxEvents          = 128;  // OTel SDK default event limit
constexpr uint32_t kMaxLinks           = 128;  // OTel SDK default link limit
constexpr uint32_t kEventAttrsPerEvent = 2;    // attrs added to each Max-shape event
constexpr int kBatchSmall              = 1;    // single-span export
constexpr int kBatchLarge              = 100;  // realistic high-throughput export
constexpr int kBatchMax                = 512;  // stress test
constexpr int kNominalBaggageEntries   = 3;
constexpr int kMaxBaggageEntries       = 64;

// ---------------------------------------------------------------------------
// TestSpanProcessor — captures every recordable produced via a real Tracer
// pipeline so benchmarks can drive recording through the public trace API
// while still inspecting / exporting the resulting batch.
//
// Templated on the recordable concrete type so MakeRecordable returns the
// requested implementation; storage uses the base `unique_ptr<Recordable>`
// type to match what the production BatchSpanProcessor passes to exporters.
//
// Export() invokes the user-supplied callback (which performs serialisation
// and any teardown that should be timed); both buffer fill and Clear() are
// available so benchmarks can include or exclude destruction as desired.
// ---------------------------------------------------------------------------
template <typename RecordableT>
class TestSpanProcessor final : public trace_sdk::SpanProcessor
{
public:
  using Buffer   = std::vector<std::unique_ptr<trace_sdk::Recordable>>;
  using ExportFn = std::function<void(Buffer &)>;

  explicit TestSpanProcessor(ExportFn callback = {}) noexcept
      : export_callback_(std::move(callback))
  {}

  std::unique_ptr<trace_sdk::Recordable> MakeRecordable() noexcept override
  {
    return std::make_unique<RecordableT>();
  }

  void OnStart(trace_sdk::Recordable & /*span*/,
               const trace_api::SpanContext & /*parent_context*/) noexcept override
  {}

  void OnEnd(std::unique_ptr<trace_sdk::Recordable> &&span) noexcept override
  {
    buffer_.emplace_back(std::move(span));
  }

  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool Shutdown(std::chrono::microseconds) noexcept override { return true; }

  // Hand the buffered recordables to the export callback, then drain the
  // buffer.  Recordable destruction (OtlpRecordable arena free / SpanData
  // string free) happens here, inside the timed region, matching the real
  // batch processor path.
  void Export() noexcept
  {
    if (export_callback_)
    {
      export_callback_(buffer_);
    }
    buffer_.clear();
  }

  // Drop buffered recordables without calling the export callback.  Used by
  // record benchmarks during PauseTiming() to bound memory and keep
  // destruction cost out of the recording measurement.
  void Clear() noexcept { buffer_.clear(); }

  std::size_t Size() const noexcept { return buffer_.size(); }

private:
  Buffer buffer_;
  ExportFn export_callback_;
};

// ---------------------------------------------------------------------------
// Export callbacks — one per recordable type.  Both build a fresh protobuf
// arena, run PopulateRequest, then let the arena (and any owned recordables)
// destruct inside the timed region — matching what a real exporter does on
// the export thread.
// ---------------------------------------------------------------------------

std::unique_ptr<google::protobuf::Arena> CreateArena()
{
  google::protobuf::ArenaOptions opts;
  opts.initial_block_size = 1024;
  opts.max_block_size     = 65536;
  return std::make_unique<google::protobuf::Arena>(opts);
}

// OtlpRecordable export path: matches what OtlpHttpExporter / OtlpGrpcExporter
// do on the export thread.  PopulateRequest downcasts each entry to
// OtlpRecordable internally.
void OtlpExportCallback(std::vector<std::unique_ptr<trace_sdk::Recordable>> &buffer) noexcept
{
  auto arena    = CreateArena();
  auto *request = google::protobuf::Arena::Create<
      opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest>(arena.get());
  otlp::OtlpRecordableUtils::PopulateRequest(
      opentelemetry::nostd::span<std::unique_ptr<trace_sdk::Recordable>>{buffer.data(),
                                                                         buffer.size()},
      request);
  benchmark::DoNotOptimize(request);
  // buffer is cleared by Export() after this callback returns.
}

// SpanData export path: PopulateRequestSpanData downcasts to SpanData.  The
// SDK never wires this in production (OtlpExporter::MakeRecordable returns
// OtlpRecordable) — the function exists for benchmarking the hypothetical
// "what if exporters consumed SpanData directly".
void SpanDataExportCallback(std::vector<std::unique_ptr<trace_sdk::Recordable>> &buffer) noexcept
{
  auto arena    = CreateArena();
  auto *request = google::protobuf::Arena::Create<
      opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest>(arena.get());
  otlp::OtlpRecordableUtils::PopulateRequestSpanData(
      opentelemetry::nostd::span<std::unique_ptr<trace_sdk::Recordable>>{buffer.data(),
                                                                         buffer.size()},
      request);
  benchmark::DoNotOptimize(request);
  // buffer is cleared by Export() after this callback returns.
}

void PmrSpanDataExportCallback(std::vector<std::unique_ptr<trace_sdk::Recordable>> &buffer) noexcept
{
  auto arena    = CreateArena();
  auto *request = google::protobuf::Arena::Create<
      opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest>(arena.get());
  otlp::OtlpRecordableUtils::PopulateRequestPmrSpanData(
      opentelemetry::nostd::span<std::unique_ptr<trace_sdk::Recordable>>{buffer.data(),
                                                                         buffer.size()},
      request);
  benchmark::DoNotOptimize(request);
}

const opentelemetry::sdk::resource::Resource &NominalResource();

// ---------------------------------------------------------------------------
// Provider factory — wires a TestSpanProcessor<RecordableT> into a real
// TracerProvider so spans can be recorded through the public trace API.
// Returns the provider (owning) and a non-owning processor pointer so the
// benchmark can call Export() / Clear().
// ---------------------------------------------------------------------------
template <typename RecordableT>
struct ProviderHandle
{
  std::shared_ptr<trace_sdk::TracerProvider> provider;
  TestSpanProcessor<RecordableT> *processor = nullptr;
  opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer;
};

template <typename RecordableT, typename ExportFn>
ProviderHandle<RecordableT> MakeProvider(ExportFn callback)
{
  auto processor =
      std::make_unique<TestSpanProcessor<RecordableT>>(std::move(callback));
  auto *processor_raw = processor.get();
  auto provider =
      std::make_shared<trace_sdk::TracerProvider>(std::move(processor), NominalResource());
  auto tracer   = provider->GetTracer("benchmark_scope", "1.0.0",
                                      "https://opentelemetry.io/schemas/1.24.0");
  return {std::move(provider), processor_raw, std::move(tracer)};
}

// ---------------------------------------------------------------------------
// Static fixture data — built once, reused across all iterations.
// ---------------------------------------------------------------------------

// Distinct attribute keys for Max-shape spans.  Using kMaxAttrs unique keys
// avoids the SDK's last-writer-wins dedup overhead masking the real attribute
// population cost.
const std::vector<std::string> &MaxAttrKeys()
{
  static const std::vector<std::string> keys = []() {
    std::vector<std::string> v;
    v.reserve(kMaxAttrs);
    for (uint32_t i = 0; i < kMaxAttrs; ++i)
    {
      v.push_back("attr." + std::to_string(i));
    }
    return v;
  }();
  return keys;
}

// 8-attribute resource representing a typical production deployment.
const opentelemetry::sdk::resource::Resource &NominalResource()
{
  static const auto r = opentelemetry::sdk::resource::Resource::Create(
      {{"service.name", opentelemetry::nostd::string_view{"my-service"}},
       {"service.version", opentelemetry::nostd::string_view{"1.2.3"}},
       {"host.name", opentelemetry::nostd::string_view{"prod-host-01"}},
       {"host.arch", opentelemetry::nostd::string_view{"amd64"}},
       {"process.pid", static_cast<int64_t>(12345)},
       {"deployment.environment", opentelemetry::nostd::string_view{"production"}},
       {"telemetry.sdk.name", opentelemetry::nostd::string_view{"opentelemetry"}},
       {"telemetry.sdk.version", opentelemetry::nostd::string_view{"1.9.0"}}});
  return r;
}

// Cached event-attrs map for nominal events (2 entries) and max events (4 entries).
const std::initializer_list<std::pair<const std::string, common_api::AttributeValue>> &NominalEventAttrs()
{
  static const std::initializer_list<std::pair<const std::string, common_api::AttributeValue>> m = {
      {"event.kind", opentelemetry::nostd::string_view{"processed"}},
      {"event.duration_ms", static_cast<int64_t>(2)}};
  return m;
}

// kMaxEvents event-attrs vectors, each with kEventAttrsPerEvent unique keys.
// Pre-built so construction cost doesn't pollute timing.
const std::vector<std::map<std::string, common_api::AttributeValue>> &MaxEventAttrsList()
{
  static const std::vector<std::map<std::string, common_api::AttributeValue>> list = []() {
    std::vector<std::map<std::string, common_api::AttributeValue>> v;
    v.reserve(kMaxEvents);
    for (uint32_t i = 0; i < kMaxEvents; ++i)
    {
      std::map<std::string, common_api::AttributeValue> m;
      for (uint32_t j = 0; j < kEventAttrsPerEvent; ++j)
      {
        m["evt." + std::to_string(i) + ".k" + std::to_string(j)] =
            static_cast<int64_t>(i * kEventAttrsPerEvent + j);
      }
      v.push_back(std::move(m));
    }
    return v;
  }();
  return list;
}

// kMaxLinks links pre-built; reused across all Max-shape StartSpan calls.
// Each link gets a distinct SpanContext and kEventAttrsPerEvent unique attrs
// to stress the link-attribute serialisation path.
using LinkPair = std::pair<trace_api::SpanContext, std::map<std::string, common_api::AttributeValue>>;
const std::vector<LinkPair> &MaxLinks()
{
  static const std::vector<LinkPair> links = []() {
    std::vector<LinkPair> v;
    v.reserve(kMaxLinks);
    for (uint32_t i = 0; i < kMaxLinks; ++i)
    {
      // Give each link a unique SpanId so the contexts are distinct.
      uint8_t span_bytes[trace_api::SpanId::kSize] = {};
      span_bytes[0]                                = static_cast<uint8_t>(i & 0xFF);
      span_bytes[1]                                = static_cast<uint8_t>((i >> 8) & 0xFF);
      constexpr uint8_t kTrace[trace_api::TraceId::kSize] = {1, 2, 3, 4, 5, 6, 7, 8,
                                                              1, 2, 3, 4, 5, 6, 7, 8};
      trace_api::SpanContext ctx{trace_api::TraceId{kTrace}, trace_api::SpanId{span_bytes},
                                 trace_api::TraceFlags{trace_api::TraceFlags::kIsSampled}, true};

      // kEventAttrsPerEvent unique attr keys per link.
      std::map<std::string, common_api::AttributeValue> attrs;
      for (uint32_t j = 0; j < kEventAttrsPerEvent; ++j)
      {
        attrs["link." + std::to_string(i) + ".k" + std::to_string(j)] =
            static_cast<int64_t>(i * kEventAttrsPerEvent + j);
      }
      v.emplace_back(ctx, std::move(attrs));
    }
    return v;
  }();
  return links;
}

// Build a baggage with N entries (for context-with-baggage benchmarks).
opentelemetry::nostd::shared_ptr<opentelemetry::baggage::Baggage> MakeBaggage(int entries)
{
  auto bag = opentelemetry::baggage::Baggage::GetDefault();
  for (int i = 0; i < entries; ++i)
  {
    auto key = "bk" + std::to_string(i);
    auto val = "bv" + std::to_string(i);
    bag      = bag->Set(key, val);
  }
  return bag;
}

// ---------------------------------------------------------------------------
// Span shaping — drives the public trace API only (StartSpan / SetAttribute /
// AddEvent / SetStatus / End).  No direct recordable manipulation.
// ---------------------------------------------------------------------------

// Nominal: 6 mixed-type span attrs, 1 event w/ 2 attrs, status Ok.
// Mirrors a typical production HTTP/DB span.
inline void RecordNominalSpan(trace_api::Tracer &tracer,
                              const trace_api::StartSpanOptions &opts)
{
  static const opentelemetry::nostd::string_view kMethod{"GET"};
  static const opentelemetry::nostd::string_view kUrl{"https://example.com/api/v1/users"};
  static const opentelemetry::nostd::string_view kDbSystem{"postgresql"};

  auto span = tracer.StartSpan(
      "GET /api/v1/users", {{"http.method", kMethod}, {"http.url", kUrl}, {"db.system", kDbSystem}},
      opts);

      
  span->SetAttribute("http.status_code", static_cast<int64_t>(200));
  span->SetAttribute("error", false);
  span->SetAttribute("net.peer.port", static_cast<int64_t>(5432));
  span->AddEvent("processed", NominalEventAttrs());
  span->SetStatus(trace_api::StatusCode::kOk, "");
  span->End();
}

// Max: kMaxAttrs span attrs, kMaxEvents events with kEventAttrsPerEvent attrs each,
// kMaxLinks links attached at StartSpan.  Stresses the recordable's attribute,
// event, and link storage paths.
//
// Attribute type cycle (8-way) covers all 4 spec scalar types and the 4
// spec array types.  Backing storage for array spans is static so span<const T>
// construction is zero-cost inside the timed region.

// File-scope static backing arrays shared by RecordMaxSpan and MaxStartAttrs().
static constexpr bool                            kMaxBoolArr[]   = {true, false, true, false};
static constexpr int64_t                         kMaxInt64Arr[]  = {1, 2, 3, 4};
static constexpr double                          kMaxDoubleArr[] = {1.1, 2.2, 3.3, 4.4};
static const opentelemetry::nostd::string_view   kMaxStrArr[]    = {"a", "b", "c", "d"};

// Pre-built flat vector of (key, value) pairs for StartSpan-time attribute passing.
// Same 8-way type cycle as RecordMaxSpan's post-construction SetAttribute loop.
// Static so construction cost is paid once, not per benchmark iteration.
using StartAttrPair =
    std::pair<opentelemetry::nostd::string_view, common_api::AttributeValue>;
const std::vector<StartAttrPair> &MaxStartAttrs()
{
  static const std::vector<StartAttrPair> attrs = []() {
    const auto &keys = MaxAttrKeys();
    std::vector<StartAttrPair> v;
    v.reserve(kMaxAttrs);
    for (uint32_t i = 0; i < kMaxAttrs; ++i)
    {
      switch (i & 0x7)
      {
        case 0:
          v.emplace_back(keys[i], opentelemetry::nostd::string_view{"value"});
          break;
        case 1:
          v.emplace_back(keys[i], static_cast<int64_t>(i));
          break;
        case 2:
          v.emplace_back(keys[i], (i & 1) != 0);
          break;
        case 3:
          v.emplace_back(keys[i], static_cast<double>(i) * 0.5);
          break;
        case 4:
          v.emplace_back(keys[i], opentelemetry::nostd::span<const bool>{kMaxBoolArr});
          break;
        case 5:
          v.emplace_back(keys[i], opentelemetry::nostd::span<const int64_t>{kMaxInt64Arr});
          break;
        case 6:
          v.emplace_back(keys[i], opentelemetry::nostd::span<const double>{kMaxDoubleArr});
          break;
        default:
          v.emplace_back(
              keys[i],
              opentelemetry::nostd::span<const opentelemetry::nostd::string_view>{kMaxStrArr});
          break;
      }
    }
    return v;
  }();
  return attrs;
}

inline void RecordMaxSpan(trace_api::Tracer &tracer,
                          const trace_api::StartSpanOptions &opts)
{
  const auto &keys = MaxAttrKeys();
  auto span = tracer.StartSpan(
      "max-span",
      opentelemetry::nostd::span<const std::pair<opentelemetry::nostd::string_view,
                                                 common_api::AttributeValue>>{},
      MaxLinks(), opts);

      

  for (uint32_t i = 0; i < kMaxAttrs; ++i)
  {
    // 8-way cycle: 4 scalar types then 4 array types.
    switch (i & 0x7)
    {
      case 0:
        span->SetAttribute(keys[i], opentelemetry::nostd::string_view{"value"});
        break;
      case 1:
        span->SetAttribute(keys[i], static_cast<int64_t>(i));
        break;
      case 2:
        span->SetAttribute(keys[i], (i & 1) != 0);
        break;
      case 3:
        span->SetAttribute(keys[i], static_cast<double>(i) * 0.5);
        break;
      case 4:
        span->SetAttribute(keys[i],
                           opentelemetry::nostd::span<const bool>{kMaxBoolArr});
        break;
      case 5:
        span->SetAttribute(keys[i],
                           opentelemetry::nostd::span<const int64_t>{kMaxInt64Arr});
        break;
      case 6:
        span->SetAttribute(keys[i],
                           opentelemetry::nostd::span<const double>{kMaxDoubleArr});
        break;
      default:
        span->SetAttribute(
            keys[i],
            opentelemetry::nostd::span<const opentelemetry::nostd::string_view>{kMaxStrArr});
        break;
    }
  }

  const auto &event_attrs_list = MaxEventAttrsList();
  for (uint32_t i = 0; i < kMaxEvents; ++i)
  {
    span->AddEvent("evt", event_attrs_list[i]);
  }

  span->SetStatus(trace_api::StatusCode::kOk, "");
  span->End();
}

// Variant: all attrs passed at StartSpan time (no post-construction SetAttribute).
// Bypasses the per-call mutex lock in Span::SetAttribute (128 lock/unlock cycles saved).
// Events still added post-construction since AddEvent has no StartSpan equivalent.
inline void RecordMaxSpanStartAttrs(trace_api::Tracer &tracer,
                                    const trace_api::StartSpanOptions &opts)
{
  const auto &attrs = MaxStartAttrs();
  auto span = tracer.StartSpan(
      "max-span",
      opentelemetry::nostd::span<const StartAttrPair>{attrs.data(), attrs.size()},
      MaxLinks(), opts);

      

  const auto &event_attrs_list = MaxEventAttrsList();
  for (uint32_t i = 0; i < kMaxEvents; ++i)
  {
    span->AddEvent("evt", event_attrs_list[i]);
  }

  span->SetStatus(trace_api::StatusCode::kOk, "");
  span->End();
}

// ---------------------------------------------------------------------------
// Type-isolated Max shapes — same kMaxAttrs count, same events and links as
// RecordMaxSpan, but attribute value type is held constant.  Isolates the
// per-type storage/serialisation cost from the overall mixed benchmark.
//
// Primitives: int64/bool/double cycling — no heap allocation in either impl.
// Strings:    string_view — both impls allocate once per attr (std::string /
//             proto string field).
// Arrays:     span<int64>/span<bool>/span<double>/span<string_view> cycling —
//             SpanData allocates a std::vector<T> per attr; OtlpRecordable
//             appends elements into an existing RepeatedField (no per-attr alloc).
// ---------------------------------------------------------------------------

inline void RecordMaxSpanPrimitives(trace_api::Tracer &tracer,
                                    const trace_api::StartSpanOptions &opts)
{
  const auto &keys = MaxAttrKeys();
  auto span        = tracer.StartSpan(
      "max-span-primitives",
      opentelemetry::nostd::span<const std::pair<opentelemetry::nostd::string_view,
                                                 common_api::AttributeValue>>{},
      MaxLinks(), opts);


  for (uint32_t i = 0; i < kMaxAttrs; ++i)
  {
    switch (i % 3)
    {
      case 0:
        span->SetAttribute(keys[i], static_cast<int64_t>(i));
        break;
      case 1:
        span->SetAttribute(keys[i], (i & 1) != 0);
        break;
      default:
        span->SetAttribute(keys[i], static_cast<double>(i) * 0.5);
        break;
    }
  }

  const auto &event_attrs_list = MaxEventAttrsList();
  for (uint32_t i = 0; i < kMaxEvents; ++i)
  {
    span->AddEvent("evt", event_attrs_list[i]);
  }
  span->SetStatus(trace_api::StatusCode::kOk, "");
  span->End();
}

inline void RecordMaxSpanStrings(trace_api::Tracer &tracer,
                                 const trace_api::StartSpanOptions &opts)
{
  // 5 chars — within SSO threshold on all major stdlib implementations.
  // Tests the proto string-field write path without heap allocation overhead.
  static const opentelemetry::nostd::string_view kVal{"value"};
  const auto &keys = MaxAttrKeys();
  auto span        = tracer.StartSpan(
      "max-span-strings",
      opentelemetry::nostd::span<const std::pair<opentelemetry::nostd::string_view,
                                                 common_api::AttributeValue>>{},
      MaxLinks(), opts);

      

  for (uint32_t i = 0; i < kMaxAttrs; ++i)
  {
    span->SetAttribute(keys[i], kVal);
  }

  const auto &event_attrs_list = MaxEventAttrsList();
  for (uint32_t i = 0; i < kMaxEvents; ++i)
  {
    span->AddEvent("evt", event_attrs_list[i]);
  }
  span->SetStatus(trace_api::StatusCode::kOk, "");
  span->End();
}

inline void RecordMaxSpanStringsLong(trace_api::Tracer &tracer,
                                     const trace_api::StartSpanOptions &opts)
{
  // 64 chars — well beyond the SSO threshold of all major stdlib implementations
  // (libstdc++ 15, libc++ 22).  Forces a heap allocation per attr in SpanData's
  // AttributeConverter and a proto string copy in OtlpRecordable.
  // Representative of URL / message / stack-trace fragment values.
  static const opentelemetry::nostd::string_view kVal{
      "https://api.example.com/v1/users/profile?format=json&include=meta"};
  // 64 chars — verify with: echo -n "https://..." | wc -c
  const auto &keys = MaxAttrKeys();
  auto span        = tracer.StartSpan(
      "max-span-strings-long",
      opentelemetry::nostd::span<const std::pair<opentelemetry::nostd::string_view,
                                                 common_api::AttributeValue>>{},
      MaxLinks(), opts);

      

  for (uint32_t i = 0; i < kMaxAttrs; ++i)
  {
    span->SetAttribute(keys[i], kVal);
  }

  const auto &event_attrs_list = MaxEventAttrsList();
  for (uint32_t i = 0; i < kMaxEvents; ++i)
  {
    span->AddEvent("evt", event_attrs_list[i]);
  }
  span->SetStatus(trace_api::StatusCode::kOk, "");
  span->End();
}

inline void RecordMaxSpanArrays(trace_api::Tracer &tracer,
                                const trace_api::StartSpanOptions &opts)
{
  const auto &keys = MaxAttrKeys();
  auto span        = tracer.StartSpan(
      "max-span-arrays",
      opentelemetry::nostd::span<const std::pair<opentelemetry::nostd::string_view,
                                                 common_api::AttributeValue>>{},
      MaxLinks(), opts);
  

  for (uint32_t i = 0; i < kMaxAttrs; ++i)
  {
    switch (i & 0x3)
    {
      case 0:
        span->SetAttribute(keys[i], opentelemetry::nostd::span<const bool>{kMaxBoolArr});
        break;
      case 1:
        span->SetAttribute(keys[i], opentelemetry::nostd::span<const int64_t>{kMaxInt64Arr});
        break;
      case 2:
        span->SetAttribute(keys[i], opentelemetry::nostd::span<const double>{kMaxDoubleArr});
        break;
      default:
        span->SetAttribute(
            keys[i],
            opentelemetry::nostd::span<const opentelemetry::nostd::string_view>{kMaxStrArr});
        break;
    }
  }

  const auto &event_attrs_list = MaxEventAttrsList();
  for (uint32_t i = 0; i < kMaxEvents; ++i)
  {
    span->AddEvent("evt", event_attrs_list[i]);
  }
  span->SetStatus(trace_api::StatusCode::kOk, "");
  span->End();
}

// ---------------------------------------------------------------------------
// Parent-mode setup helpers.  Each returns a struct that, while alive,
// configures the runtime context (and / or fills opts.parent) appropriately.
// Constructed once per benchmark and reused across iterations.
// ---------------------------------------------------------------------------
enum class ParentMode
{
  kNone,
  kExplicit,
  kImplicit,
  kImplicitWithNominalBaggage,
  kImplicitWithMaxBaggage,
};

struct ParentSetup
{
  trace_api::StartSpanOptions opts;
  // Hold a parent span shared_ptr so its SpanContext stays valid for explicit mode.
  opentelemetry::nostd::shared_ptr<trace_api::Span> parent_span;
  // Token for runtime context attach (implicit modes).
  opentelemetry::nostd::unique_ptr<opentelemetry::context::Token> token;
};

ParentSetup ConfigureParent(trace_api::Tracer &tracer, ParentMode mode)
{
  ParentSetup setup;
  switch (mode)
  {
    case ParentMode::kNone:
      // Empty opts; no implicit context attached above the benchmark.
      break;
    case ParentMode::kExplicit:
    {
      setup.parent_span = tracer.StartSpan("parent");
      setup.opts.parent = setup.parent_span->GetContext();
      break;
    }
    case ParentMode::kImplicit:
    {
      setup.parent_span = tracer.StartSpan("parent");
      auto ctx          = opentelemetry::context::RuntimeContext::GetCurrent();
      ctx               = trace_api::SetSpan(ctx, setup.parent_span);
      setup.token       = opentelemetry::context::RuntimeContext::Attach(ctx);
      break;
    }
    case ParentMode::kImplicitWithNominalBaggage:
    case ParentMode::kImplicitWithMaxBaggage:
    {
      setup.parent_span = tracer.StartSpan("parent");
      auto ctx          = opentelemetry::context::RuntimeContext::GetCurrent();
      ctx               = trace_api::SetSpan(ctx, setup.parent_span);
      const int n =
          mode == ParentMode::kImplicitWithMaxBaggage ? kMaxBaggageEntries : kNominalBaggageEntries;
      ctx         = opentelemetry::baggage::SetBaggage(ctx, MakeBaggage(n));
      setup.token = opentelemetry::context::RuntimeContext::Attach(ctx);
      break;
    }
  }
  return setup;
}

// ---------------------------------------------------------------------------
// Record benchmarks — measure StartSpan + attrs/events/status + End,
// recorded through the public trace API.  Recordable destruction is
// excluded by clearing the processor buffer during PauseTiming().
// ---------------------------------------------------------------------------

template <typename RecordableT, typename ShapeFn>
void BM_Record_Impl(benchmark::State &state, ShapeFn shape, ParentMode mode,
                    int64_t attrs_per_span = 0)
{
  // Each benchmark builds its own provider so processors don't share buffers.
  auto handle = MakeProvider<RecordableT>(typename TestSpanProcessor<RecordableT>::ExportFn{});
  auto setup  = ConfigureParent(*handle.tracer, mode);

  for (auto _ : state)
  {
    shape(*handle.tracer, setup.opts);
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }

  if (attrs_per_span > 0)
  {
    // time/attr: time per individual span-attribute write (span attrs only).
    state.counters["time/attr"] = benchmark::Counter(
        static_cast<double>(state.iterations() * attrs_per_span),
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert,
        benchmark::Counter::kIs1000);
  }
}

// ---------------------------------------------------------------------------
// Baseline — empty StartSpan/End with no attrs and no parent.  Provides a
// floor for interpreting all Record benchmarks.
// ---------------------------------------------------------------------------
static void BM_Record_Empty_OtlpRecordable(benchmark::State &state)
{
  auto handle =
      MakeProvider<otlp::OtlpRecordable>(typename TestSpanProcessor<otlp::OtlpRecordable>::ExportFn{});
  for (auto _ : state)
  {
    auto span = handle.tracer->StartSpan("op");
    
    span->End();
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }
}
BENCHMARK(BM_Record_Empty_OtlpRecordable)->Unit(benchmark::kNanosecond);

static void BM_Record_Empty_SpanData(benchmark::State &state)
{
  auto handle =
      MakeProvider<trace_sdk::SpanData>(typename TestSpanProcessor<trace_sdk::SpanData>::ExportFn{});
  for (auto _ : state)
  {
    auto span = handle.tracer->StartSpan("op");
    
    span->End();
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }
}
BENCHMARK(BM_Record_Empty_SpanData)->Unit(benchmark::kNanosecond);

#if defined(__cpp_lib_memory_resource)
static void BM_Record_Empty_PmrSpanData(benchmark::State &state)
{
  auto handle =
      MakeProvider<PmrSpanData>(typename TestSpanProcessor<PmrSpanData>::ExportFn{});
  for (auto _ : state)
  {
    auto span = handle.tracer->StartSpan("op");
    
    span->End();
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }
}
BENCHMARK(BM_Record_Empty_PmrSpanData)->Unit(benchmark::kNanosecond);
#endif

// --- Nominal × Recordable × ParentMode matrix --------------------------------

#define DEFINE_RECORD_NOMINAL_BENCH(Name, RecordableT, Mode)                                  \
  static void BM_Record_Nominal_##Name(benchmark::State &state)                               \
  {                                                                                           \
    BM_Record_Impl<RecordableT>(state, RecordNominalSpan, Mode);                              \
  }                                                                                           \
  BENCHMARK(BM_Record_Nominal_##Name)->Unit(benchmark::kNanosecond)

DEFINE_RECORD_NOMINAL_BENCH(OtlpRecordable_NoParent, otlp::OtlpRecordable, ParentMode::kNone);
DEFINE_RECORD_NOMINAL_BENCH(SpanData_NoParent, trace_sdk::SpanData, ParentMode::kNone);

DEFINE_RECORD_NOMINAL_BENCH(OtlpRecordable_ExplicitParent,
                            otlp::OtlpRecordable,
                            ParentMode::kExplicit);
DEFINE_RECORD_NOMINAL_BENCH(SpanData_ExplicitParent, trace_sdk::SpanData, ParentMode::kExplicit);

DEFINE_RECORD_NOMINAL_BENCH(OtlpRecordable_ImplicitParent,
                            otlp::OtlpRecordable,
                            ParentMode::kImplicit);
DEFINE_RECORD_NOMINAL_BENCH(SpanData_ImplicitParent, trace_sdk::SpanData, ParentMode::kImplicit);

DEFINE_RECORD_NOMINAL_BENCH(OtlpRecordable_ImplicitParent_NominalBaggage,
                            otlp::OtlpRecordable,
                            ParentMode::kImplicitWithNominalBaggage);
DEFINE_RECORD_NOMINAL_BENCH(SpanData_ImplicitParent_NominalBaggage,
                            trace_sdk::SpanData,
                            ParentMode::kImplicitWithNominalBaggage);

DEFINE_RECORD_NOMINAL_BENCH(OtlpRecordable_ImplicitParent_MaxBaggage,
                            otlp::OtlpRecordable,
                            ParentMode::kImplicitWithMaxBaggage);
#if defined(__cpp_lib_memory_resource)
DEFINE_RECORD_NOMINAL_BENCH(PmrSpanData_NoParent, PmrSpanData, ParentMode::kNone);
DEFINE_RECORD_NOMINAL_BENCH(PmrSpanData_ExplicitParent, PmrSpanData, ParentMode::kExplicit);
DEFINE_RECORD_NOMINAL_BENCH(PmrSpanData_ImplicitParent, PmrSpanData, ParentMode::kImplicit);
DEFINE_RECORD_NOMINAL_BENCH(PmrSpanData_ImplicitParent_NominalBaggage,
                            PmrSpanData,
                            ParentMode::kImplicitWithNominalBaggage);
DEFINE_RECORD_NOMINAL_BENCH(PmrSpanData_ImplicitParent_MaxBaggage,
                            PmrSpanData,
                            ParentMode::kImplicitWithMaxBaggage);
#endif

DEFINE_RECORD_NOMINAL_BENCH(SpanData_ImplicitParent_MaxBaggage,
                            trace_sdk::SpanData,
                            ParentMode::kImplicitWithMaxBaggage);

#undef DEFINE_RECORD_NOMINAL_BENCH

// --- Max × Recordable (no-parent only — parent mode is orthogonal at this
//     scale and adds noise rather than insight) -------------------------------

static void BM_Record_Max_OtlpRecordable(benchmark::State &state)
{
  BM_Record_Impl<otlp::OtlpRecordable>(state, RecordMaxSpan, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_OtlpRecordable)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_SpanData(benchmark::State &state)
{
  BM_Record_Impl<trace_sdk::SpanData>(state, RecordMaxSpan, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_SpanData)->Unit(benchmark::kMicrosecond);

// StartSpan-attrs variants: same attributes but passed at construction time,
// bypassing the per-call mutex in Span::SetAttribute.
static void BM_Record_Max_OtlpRecordable_StartAttrs(benchmark::State &state)
{
  BM_Record_Impl<otlp::OtlpRecordable>(state, RecordMaxSpanStartAttrs, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_OtlpRecordable_StartAttrs)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_SpanData_StartAttrs(benchmark::State &state)
{
  BM_Record_Impl<trace_sdk::SpanData>(state, RecordMaxSpanStartAttrs, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_SpanData_StartAttrs)->Unit(benchmark::kMicrosecond);

// --- Type-isolated Max shapes × 2 implementations -------------------------
// Primitives: int64/bool/double — zero heap allocs in both impls.
// Strings:    string_view — 1 alloc/attr in both impls (std::string / proto string).
// Arrays:     span<T> — 1 std::vector alloc/attr in SpanData; 0 in OtlpRecordable.

static void BM_Record_Max_OtlpRecordable_Primitives(benchmark::State &state)
{
  BM_Record_Impl<otlp::OtlpRecordable>(state, RecordMaxSpanPrimitives, ParentMode::kNone,
                                       kMaxAttrs);
}
BENCHMARK(BM_Record_Max_OtlpRecordable_Primitives)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_SpanData_Primitives(benchmark::State &state)
{
  BM_Record_Impl<trace_sdk::SpanData>(state, RecordMaxSpanPrimitives, ParentMode::kNone,
                                      kMaxAttrs);
}
BENCHMARK(BM_Record_Max_SpanData_Primitives)->Unit(benchmark::kMicrosecond);

#if defined(__cpp_lib_memory_resource)
static void BM_Record_Max_PmrSpanData_Primitives(benchmark::State &state)
{
  BM_Record_Impl<PmrSpanData>(state, RecordMaxSpanPrimitives, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_PmrSpanData_Primitives)->Unit(benchmark::kMicrosecond);
#endif

static void BM_Record_Max_OtlpRecordable_Strings(benchmark::State &state)
{
  BM_Record_Impl<otlp::OtlpRecordable>(state, RecordMaxSpanStrings, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_OtlpRecordable_Strings)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_SpanData_Strings(benchmark::State &state)
{
  BM_Record_Impl<trace_sdk::SpanData>(state, RecordMaxSpanStrings, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_SpanData_Strings)->Unit(benchmark::kMicrosecond);

#if defined(__cpp_lib_memory_resource)
static void BM_Record_Max_PmrSpanData_Strings(benchmark::State &state)
{
  BM_Record_Impl<PmrSpanData>(state, RecordMaxSpanStrings, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_PmrSpanData_Strings)->Unit(benchmark::kMicrosecond);
#endif

static void BM_Record_Max_OtlpRecordable_StringsLong(benchmark::State &state)
{
  BM_Record_Impl<otlp::OtlpRecordable>(state, RecordMaxSpanStringsLong, ParentMode::kNone,
                                       kMaxAttrs);
}
BENCHMARK(BM_Record_Max_OtlpRecordable_StringsLong)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_SpanData_StringsLong(benchmark::State &state)
{
  BM_Record_Impl<trace_sdk::SpanData>(state, RecordMaxSpanStringsLong, ParentMode::kNone,
                                      kMaxAttrs);
}
BENCHMARK(BM_Record_Max_SpanData_StringsLong)->Unit(benchmark::kMicrosecond);

#if defined(__cpp_lib_memory_resource)
static void BM_Record_Max_PmrSpanData_StringsLong(benchmark::State &state)
{
  BM_Record_Impl<PmrSpanData>(state, RecordMaxSpanStringsLong, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_PmrSpanData_StringsLong)->Unit(benchmark::kMicrosecond);
#endif

static void BM_Record_Max_OtlpRecordable_Arrays(benchmark::State &state)
{
  BM_Record_Impl<otlp::OtlpRecordable>(state, RecordMaxSpanArrays, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_OtlpRecordable_Arrays)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_SpanData_Arrays(benchmark::State &state)
{
  BM_Record_Impl<trace_sdk::SpanData>(state, RecordMaxSpanArrays, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_SpanData_Arrays)->Unit(benchmark::kMicrosecond);

#if defined(__cpp_lib_memory_resource)
static void BM_Record_Max_PmrSpanData_Arrays(benchmark::State &state)
{
  BM_Record_Impl<PmrSpanData>(state, RecordMaxSpanArrays, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_PmrSpanData_Arrays)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_PmrSpanData(benchmark::State &state)
{
  BM_Record_Impl<PmrSpanData>(state, RecordMaxSpan, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_PmrSpanData)->Unit(benchmark::kMicrosecond);

static void BM_Record_Max_PmrSpanData_StartAttrs(benchmark::State &state)
{
  BM_Record_Impl<PmrSpanData>(state, RecordMaxSpanStartAttrs, ParentMode::kNone, kMaxAttrs);
}
BENCHMARK(BM_Record_Max_PmrSpanData_StartAttrs)->Unit(benchmark::kMicrosecond);
#endif  // __cpp_lib_memory_resource

// ---------------------------------------------------------------------------
// Attribute-count sweep — measures total cost of one span with N attributes.
// Time column = cost per span directly; use attrs:N in the benchmark name to
// look up the cost for a known span shape.
// No events, no links — isolates the attribute storage cost.
// Uses the same 8-way mixed-type cycle as RecordMaxSpan.
// ---------------------------------------------------------------------------
template <typename RecordableT>
void BM_Record_AttrCount_Impl(benchmark::State &state)
{
  const uint32_t n    = static_cast<uint32_t>(state.range(0));
  const auto    &keys = MaxAttrKeys();
  auto           handle =
      MakeProvider<RecordableT>(typename TestSpanProcessor<RecordableT>::ExportFn{});

  trace_api::StartSpanOptions opts;

  for (auto _ : state)
  {
    auto span = handle.tracer->StartSpan("sweep", opts);
    
    for (uint32_t i = 0; i < n; ++i)
    {
      switch (i & 0x7)
      {
        case 0:
          span->SetAttribute(keys[i], opentelemetry::nostd::string_view{"value"});
          break;
        case 1:
          span->SetAttribute(keys[i], static_cast<int64_t>(i));
          break;
        case 2:
          span->SetAttribute(keys[i], (i & 1) != 0);
          break;
        case 3:
          span->SetAttribute(keys[i], static_cast<double>(i) * 0.5);
          break;
        case 4:
          span->SetAttribute(keys[i],
                             opentelemetry::nostd::span<const bool>{kMaxBoolArr});
          break;
        case 5:
          span->SetAttribute(keys[i],
                             opentelemetry::nostd::span<const int64_t>{kMaxInt64Arr});
          break;
        case 6:
          span->SetAttribute(keys[i],
                             opentelemetry::nostd::span<const double>{kMaxDoubleArr});
          break;
        default:
          span->SetAttribute(
              keys[i],
              opentelemetry::nostd::span<const opentelemetry::nostd::string_view>{
                  kMaxStrArr});
          break;
      }
    }
    span->End();
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }

  // spans/sec: throughput at this span shape.  Time column is cost-per-span.
  state.counters["spans/sec"] = benchmark::Counter(
      static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
}

// ---------------------------------------------------------------------------
// Attribute-count sweep — StartSpan + SetAttribute loop + End + destruction
// are all inside the timed region.  Time column = total per-span cost at
// that attribute count.  No events, no links.
// Sweep: 1, 4, 8, 16, 24, 32, 48, 64, 96, 128
#define ATTR_COUNT_ARGS \
  ->Arg(1)->Arg(4)->Arg(8)->Arg(16)->Arg(24)->Arg(32)->Arg(48)->Arg(64)->Arg(96)->Arg(128)

static void BM_Record_AttrCount_OtlpRecordable(benchmark::State &state)
{
  BM_Record_AttrCount_Impl<otlp::OtlpRecordable>(state);
}
BENCHMARK(BM_Record_AttrCount_OtlpRecordable)
    ->ArgName("attrs")
    ATTR_COUNT_ARGS->Unit(benchmark::kMicrosecond);

static void BM_Record_AttrCount_SpanData(benchmark::State &state)
{
  BM_Record_AttrCount_Impl<trace_sdk::SpanData>(state);
}
BENCHMARK(BM_Record_AttrCount_SpanData)
    ->ArgName("attrs")
    ATTR_COUNT_ARGS->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// Attribute-count sweep — construction-time path.
// Attributes are passed to StartSpan (no mutex, no per-call lock).
// For SpanData this exercises SetAttributes() with reserve() — no rehash.
// For OtlpRecordable this still calls SetAttribute() one at a time (no bulk
// reserve), so a rehash spike remains possible at 32 attrs.
// No events, no links — same isolation goal as the post-construction sweep.
// ---------------------------------------------------------------------------
template <typename RecordableT>
void BM_Record_AttrCount_StartAttrs_Impl(benchmark::State &state)
{
  const uint32_t n     = static_cast<uint32_t>(state.range(0));
  const auto    &attrs = MaxStartAttrs();
  auto           handle =
      MakeProvider<RecordableT>(typename TestSpanProcessor<RecordableT>::ExportFn{});

  trace_api::StartSpanOptions opts;

  for (auto _ : state)
  {
    auto span = handle.tracer->StartSpan(
        "sweep",
        opentelemetry::nostd::span<const StartAttrPair>{attrs.data(), n},
        opts);
    span->End();
    state.PauseTiming();
    handle.processor->Clear();
    state.ResumeTiming();
  }

  // spans/sec: throughput at this span shape.  Time column is cost-per-span.
  state.counters["spans/sec"] = benchmark::Counter(
      static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
}

static void BM_Record_AttrCount_StartAttrs_OtlpRecordable(benchmark::State &state)
{
  BM_Record_AttrCount_StartAttrs_Impl<otlp::OtlpRecordable>(state);
}
BENCHMARK(BM_Record_AttrCount_StartAttrs_OtlpRecordable)
    ->ArgName("attrs")
    ATTR_COUNT_ARGS->Unit(benchmark::kMicrosecond);

static void BM_Record_AttrCount_StartAttrs_SpanData(benchmark::State &state)
{
  BM_Record_AttrCount_StartAttrs_Impl<trace_sdk::SpanData>(state);
}
BENCHMARK(BM_Record_AttrCount_StartAttrs_SpanData)
    ->ArgName("attrs")
    ATTR_COUNT_ARGS->Unit(benchmark::kMicrosecond);

#if defined(__cpp_lib_memory_resource)
static void BM_Record_AttrCount_PmrSpanData(benchmark::State &state)
{
  BM_Record_AttrCount_Impl<PmrSpanData>(state);
}
BENCHMARK(BM_Record_AttrCount_PmrSpanData)
    ->ArgName("attrs")
    ATTR_COUNT_ARGS->Unit(benchmark::kMicrosecond);

static void BM_Record_AttrCount_StartAttrs_PmrSpanData(benchmark::State &state)
{
  BM_Record_AttrCount_StartAttrs_Impl<PmrSpanData>(state);
}
BENCHMARK(BM_Record_AttrCount_StartAttrs_PmrSpanData)
    ->ArgName("attrs")
    ATTR_COUNT_ARGS->Unit(benchmark::kMicrosecond);
#endif

#undef ATTR_COUNT_ARGS

// ---------------------------------------------------------------------------
// Export benchmarks — measure TestSpanProcessor::Export() given a
// pre-recorded batch.  Includes serialisation (PopulateRequest), arena
// allocation, and recordable destruction; excludes the recording phase.
// ---------------------------------------------------------------------------

template <typename RecordableT, typename ShapeFn, typename ExportFn>
void BM_Export_Impl(benchmark::State &state, ShapeFn shape, ExportFn export_cb)
{
  const std::size_t n = static_cast<std::size_t>(state.range(0));
  auto handle         = MakeProvider<RecordableT>(std::move(export_cb));
  trace_api::StartSpanOptions opts;  // no parent — keep export benchmark focused

  for (auto _ : state)
  {
    state.PauseTiming();
    for (std::size_t i = 0; i < n; ++i)
    {
      shape(*handle.tracer, opts);
    }
    state.ResumeTiming();

    handle.processor->Export();  // measured: callback + arena + destruction
  }
  // time/span: per-span export latency — allows direct comparison across batch sizes.
  state.counters["time/span"] = benchmark::Counter(
      static_cast<double>(state.iterations() * state.range(0)),
      benchmark::Counter::kIsRate | benchmark::Counter::kInvert,
      benchmark::Counter::kIs1000);
}

#if defined(__cpp_lib_memory_resource)
static void BM_Export_Nominal_PmrSpanData(benchmark::State &state)
{
  BM_Export_Impl<PmrSpanData>(state, RecordNominalSpan, PmrSpanDataExportCallback);
}
BENCHMARK(BM_Export_Nominal_PmrSpanData)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchLarge)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

static void BM_Export_Max_PmrSpanData(benchmark::State &state)
{
  BM_Export_Impl<PmrSpanData>(state, RecordMaxSpan, PmrSpanDataExportCallback);
}
BENCHMARK(BM_Export_Max_PmrSpanData)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchLarge)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);
#endif

static void BM_Export_Nominal_OtlpRecordable(benchmark::State &state)
{
  BM_Export_Impl<otlp::OtlpRecordable>(state, RecordNominalSpan, OtlpExportCallback);
}
BENCHMARK(BM_Export_Nominal_OtlpRecordable)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchLarge)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

static void BM_Export_Nominal_SpanData(benchmark::State &state)
{
  BM_Export_Impl<trace_sdk::SpanData>(state, RecordNominalSpan, SpanDataExportCallback);
}
BENCHMARK(BM_Export_Nominal_SpanData)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchLarge)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

static void BM_Export_Max_OtlpRecordable(benchmark::State &state)
{
  BM_Export_Impl<otlp::OtlpRecordable>(state, RecordMaxSpan, OtlpExportCallback);
}
BENCHMARK(BM_Export_Max_OtlpRecordable)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchLarge)
    ->Arg(kBatchMax)
    ->Unit(benchmark::kMicrosecond);

static void BM_Export_Max_SpanData(benchmark::State &state)
{
  BM_Export_Impl<trace_sdk::SpanData>(state, RecordMaxSpan, SpanDataExportCallback);
}
BENCHMARK(BM_Export_Max_SpanData)
    ->ArgName("batch")
    ->Arg(kBatchSmall)
    ->Arg(kBatchLarge)
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
