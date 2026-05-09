// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <unordered_map>

#include "opentelemetry/exporters/otlp/otlp_log_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_populate_attribute_utils.h"
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/readable_log_record.h"
#include "opentelemetry/sdk/logs/recordable.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/sdk/trace/pmr_span_data.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h"  // IWYU pragma: keep
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"           // IWYU pragma: keep
#include "opentelemetry/proto/trace/v1/trace.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h"  // IWYU pragma: keep
// clang-format on

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{
struct InstrumentationScopePointerHasher
{
  std::size_t operator()(const opentelemetry::sdk::instrumentationscope::InstrumentationScope
                             *instrumentation) const noexcept
  {
    if (instrumentation == nullptr)
    {
      return 0;
    }

    return instrumentation->HashCode();
  }
};

struct InstrumentationScopePointerEqual
{
  bool operator()(
      const opentelemetry::sdk::instrumentationscope::InstrumentationScope *left,
      const opentelemetry::sdk::instrumentationscope::InstrumentationScope *right) const noexcept
  {
    if (left == right)
    {
      return true;
    }

    if (left == nullptr || right == nullptr)
    {
      return false;
    }

    return *left == *right;
  }
};
}  // namespace

void OtlpRecordableUtils::PopulateRequest(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> &spans,
    proto::collector::trace::v1::ExportTraceServiceRequest *request) noexcept
{
  if (nullptr == request)
  {
    return;
  }

  using ScopeSpansMap =
      std::unordered_map<const opentelemetry::sdk::instrumentationscope::InstrumentationScope *,
                         proto::trace::v1::ScopeSpans *, InstrumentationScopePointerHasher,
                         InstrumentationScopePointerEqual>;
  struct ResourceSpansEntry
  {
    proto::trace::v1::ResourceSpans *resource_spans = nullptr;
    ScopeSpansMap scope_spans;
  };
  std::unordered_map<const opentelemetry::sdk::resource::Resource *, ResourceSpansEntry>
      resource_spans_index;

  for (const auto &recordable : spans)
  {
    const auto *otlp_recordable = static_cast<const OtlpRecordable *>(recordable.get());
    const auto *resource        = otlp_recordable->GetResource();
    const auto *instrumentation = otlp_recordable->GetInstrumentationScope();

    // Find or create the ResourceSpans entry for this recordable's resource
    auto &resource_entry = resource_spans_index[resource];
    if (resource_entry.resource_spans == nullptr)
    {
      resource_entry.resource_spans = request->add_resource_spans();
      if (resource != nullptr)
      {
        // Populate the resource attributes and schema url
        OtlpPopulateAttributeUtils::PopulateAttribute(
            resource_entry.resource_spans->mutable_resource(), *resource);
        resource_entry.resource_spans->set_schema_url(resource->GetSchemaURL());
      }
    }

    // Find or create the ScopeSpans entry for this recordable's instrumentation scope
    auto &scope_spans = resource_entry.scope_spans[instrumentation];
    if (scope_spans == nullptr)
    {
      scope_spans = resource_entry.resource_spans->add_scope_spans();
      // Reserve capacity for the common case where all spans share the same resource+scope.
      // Over-reserving for multi-scope batches is harmless (arena memory).
      scope_spans->mutable_spans()->Reserve(static_cast<int>(spans.size()));
      if (instrumentation != nullptr)
      {
        // Populate the instrumentation scope attributes and schema url
        proto::common::v1::InstrumentationScope *instrumentation_scope_proto =
            scope_spans->mutable_scope();
        instrumentation_scope_proto->set_name(instrumentation->GetName());
        instrumentation_scope_proto->set_version(instrumentation->GetVersion());
        OtlpPopulateAttributeUtils::PopulateAttribute(instrumentation_scope_proto,
                                                      *instrumentation);

        scope_spans->set_schema_url(instrumentation->GetSchemaURL());
      }
    }

    // The recordable span must be copied here: the request message is arena-allocated
    // while OtlpRecordable::span() lives on a per-recordable arena.  Protobuf does not
    // allow zero-copy adoption between different arenas — UnsafeArenaAddAllocated would
    // corrupt memory, and AddAllocated deep-copies anyway then frees the source (strictly
    // worse than CopyFrom).  The only true zero-copy path is pre-allocating span_ on the
    // same arena as the request, which requires a deeper API change.
    scope_spans->add_spans()->CopyFrom(otlp_recordable->span());
  }
}

void OtlpRecordableUtils::PopulateRequestSpanData(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
        &spans,
    proto::collector::trace::v1::ExportTraceServiceRequest *request) noexcept
{
  if (nullptr == request)
  {
    return;
  }

  using ScopeSpansMap =
      std::unordered_map<const opentelemetry::sdk::instrumentationscope::InstrumentationScope *,
                         proto::trace::v1::ScopeSpans *, InstrumentationScopePointerHasher,
                         InstrumentationScopePointerEqual>;
  struct ResourceSpansEntry
  {
    proto::trace::v1::ResourceSpans *resource_spans = nullptr;
    ScopeSpansMap scope_spans;
  };
  std::unordered_map<const opentelemetry::sdk::resource::Resource *, ResourceSpansEntry>
      resource_spans_index;

  for (const auto &span : spans)
  {
    const auto *span_data =
        static_cast<const opentelemetry::sdk::trace::SpanData *>(span.get());
    const auto *resource        = &span_data->GetResource();
    const auto *instrumentation = &span_data->GetInstrumentationScope();

    auto &resource_entry = resource_spans_index[resource];
    if (resource_entry.resource_spans == nullptr)
    {
      resource_entry.resource_spans = request->add_resource_spans();
      if (resource != nullptr)
      {
        OtlpPopulateAttributeUtils::PopulateAttribute(
            resource_entry.resource_spans->mutable_resource(), *resource);
        resource_entry.resource_spans->set_schema_url(resource->GetSchemaURL());
      }
    }

    auto &scope_spans = resource_entry.scope_spans[instrumentation];
    if (scope_spans == nullptr)
    {
      scope_spans = resource_entry.resource_spans->add_scope_spans();
      // Reserve capacity for the common case where all spans share the same resource+scope.
      scope_spans->mutable_spans()->Reserve(static_cast<int>(spans.size()));
      if (instrumentation != nullptr)
      {
        proto::common::v1::InstrumentationScope *scope_proto = scope_spans->mutable_scope();
        scope_proto->set_name(instrumentation->GetName());
        scope_proto->set_version(instrumentation->GetVersion());
        OtlpPopulateAttributeUtils::PopulateAttribute(scope_proto, *instrumentation);
        scope_spans->set_schema_url(instrumentation->GetSchemaURL());
      }
    }

    // Translate SpanData fields into a new proto::Span
    auto *proto_span = scope_spans->add_spans();

    // Identity
    proto_span->set_trace_id(
        reinterpret_cast<const char *>(span_data->GetTraceId().Id().data()),
        opentelemetry::trace::TraceId::kSize);
    proto_span->set_span_id(
        reinterpret_cast<const char *>(span_data->GetSpanId().Id().data()),
        opentelemetry::trace::SpanId::kSize);
    if (span_data->GetParentSpanId().IsValid())
    {
      proto_span->set_parent_span_id(
          reinterpret_cast<const char *>(span_data->GetParentSpanId().Id().data()),
          opentelemetry::trace::SpanId::kSize);
    }
    proto_span->set_trace_state(
        span_data->GetSpanContext().trace_state()->ToHeader());

    // Name, kind, timing
    proto_span->set_name(span_data->GetName().data(), span_data->GetName().size());
    proto_span->set_kind(
        static_cast<proto::trace::v1::Span_SpanKind>(span_data->GetSpanKind()));
    const uint64_t start_ns =
        static_cast<uint64_t>(span_data->GetStartTime().time_since_epoch().count());
    proto_span->set_start_time_unix_nano(start_ns);
    proto_span->set_end_time_unix_nano(
        start_ns + static_cast<uint64_t>(span_data->GetDuration().count()));

    // Flags
    proto_span->set_flags(span_data->GetFlags().flags() &
                          opentelemetry::proto::trace::v1::SPAN_FLAGS_TRACE_FLAGS_MASK);

    // Status — kUnset is the proto3 default; skip the sub-message allocation entirely.
    if (span_data->GetStatus() != opentelemetry::trace::StatusCode::kUnset)
    {
      proto_span->mutable_status()->set_code(
          proto::trace::v1::Status_StatusCode(span_data->GetStatus()));
      if (span_data->GetStatus() == opentelemetry::trace::StatusCode::kError)
      {
        const auto desc = span_data->GetDescription();
        proto_span->mutable_status()->set_message(desc.data(), desc.size());
      }
    }

    // Span-level attributes
    proto_span->mutable_attributes()->Reserve(
        static_cast<int>(span_data->GetAttributeCount()));
    span_data->ForEachAttribute(
        [&proto_span](const std::string &key,
                      const opentelemetry::sdk::common::OwnedAttributeValue &value) {
          OtlpPopulateAttributeUtils::PopulateAttribute(proto_span->add_attributes(), key, value,
                                                        false);
        });

    // Events
    proto_span->mutable_events()->Reserve(static_cast<int>(span_data->GetEvents().size()));
    for (const auto &event : span_data->GetEvents())
    {
      auto *proto_event = proto_span->add_events();
      proto_event->set_name(event.GetName().data(), event.GetName().size());
      proto_event->set_time_unix_nano(
          static_cast<uint64_t>(event.GetTimestamp().time_since_epoch().count()));
      proto_event->mutable_attributes()->Reserve(
          static_cast<int>(event.GetAttributeCount()));
      for (const auto &attr : event.GetAttributes())
      {
        OtlpPopulateAttributeUtils::PopulateAttribute(proto_event->add_attributes(), attr.first,
                                                      attr.second, false);
      }
    }

    // Links
    proto_span->mutable_links()->Reserve(static_cast<int>(span_data->GetLinks().size()));
    for (const auto &link : span_data->GetLinks())
    {
      auto *proto_link = proto_span->add_links();
      proto_link->set_trace_id(
          reinterpret_cast<const char *>(link.GetSpanContext().trace_id().Id().data()),
          opentelemetry::trace::TraceId::kSize);
      proto_link->set_span_id(
          reinterpret_cast<const char *>(link.GetSpanContext().span_id().Id().data()),
          opentelemetry::trace::SpanId::kSize);
      proto_link->set_trace_state(link.GetSpanContext().trace_state()->ToHeader());
      proto_link->mutable_attributes()->Reserve(
          static_cast<int>(link.GetAttributeCount()));
      for (const auto &attr : link.GetAttributes())
      {
        OtlpPopulateAttributeUtils::PopulateAttribute(proto_link->add_attributes(), attr.first,
                                                      attr.second, false);
      }
    }
  }
}

void OtlpRecordableUtils::PopulateRequest(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> &logs,
    proto::collector::logs::v1::ExportLogsServiceRequest *request) noexcept
{
  if (nullptr == request)
  {
    return;
  }

  using ScopeLogsMap =
      std::unordered_map<const opentelemetry::sdk::instrumentationscope::InstrumentationScope *,
                         proto::logs::v1::ScopeLogs *, InstrumentationScopePointerHasher,
                         InstrumentationScopePointerEqual>;
  struct ResourceLogsEntry
  {
    proto::logs::v1::ResourceLogs *resource_logs = nullptr;
    ScopeLogsMap scope_logs;
  };
  std::unordered_map<const opentelemetry::sdk::resource::Resource *, ResourceLogsEntry>
      resource_logs_index;

  for (const auto &recordable : logs)
  {
    const auto *otlp_recordable = static_cast<const OtlpLogRecordable *>(recordable.get());
    const auto *instrumentation = &otlp_recordable->GetInstrumentationScope();
    const auto *resource        = &otlp_recordable->GetResource();

    // Find or create the ResourceLogs entry for this recordable's resource
    auto &resource_entry = resource_logs_index[resource];
    if (resource_entry.resource_logs == nullptr)
    {
      resource_entry.resource_logs = request->add_resource_logs();
      if (resource != nullptr)
      {
        // Populate the resource attributes and schema url
        OtlpPopulateAttributeUtils::PopulateAttribute(
            resource_entry.resource_logs->mutable_resource(), *resource);
        resource_entry.resource_logs->set_schema_url(resource->GetSchemaURL());
      }
    }

    // Find or create the ScopeLogs entry for this recordable's instrumentation scope
    auto &scope_logs = resource_entry.scope_logs[instrumentation];
    if (scope_logs == nullptr)
    {
      scope_logs = resource_entry.resource_logs->add_scope_logs();
      if (instrumentation != nullptr)
      {
        auto proto_scope = scope_logs->mutable_scope();
        proto_scope->set_name(instrumentation->GetName());
        proto_scope->set_version(instrumentation->GetVersion());
        OtlpPopulateAttributeUtils::PopulateAttribute(proto_scope, *instrumentation);
        scope_logs->set_schema_url(instrumentation->GetSchemaURL());
      }
    }

    // The recordable log can only be copied here since the request message is Arena allocated.
    scope_logs->add_log_records()->CopyFrom(otlp_recordable->log_record());
  }
}

void OtlpRecordableUtils::PopulateRequestLogData(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> &logs,
    proto::collector::logs::v1::ExportLogsServiceRequest *request) noexcept
{
  if (nullptr == request)
  {
    return;
  }

  using ScopeLogsMap =
      std::unordered_map<const opentelemetry::sdk::instrumentationscope::InstrumentationScope *,
                         proto::logs::v1::ScopeLogs *, InstrumentationScopePointerHasher,
                         InstrumentationScopePointerEqual>;
  struct ResourceLogsEntry
  {
    proto::logs::v1::ResourceLogs *resource_logs = nullptr;
    ScopeLogsMap scope_logs;
  };
  std::unordered_map<const opentelemetry::sdk::resource::Resource *, ResourceLogsEntry>
      resource_logs_index;

  for (const auto &recordable : logs)
  {
    const auto *log_data =
        static_cast<const opentelemetry::sdk::logs::ReadableLogRecord *>(recordable.get());
    const auto *instrumentation = &log_data->GetInstrumentationScope();
    const auto *resource        = &log_data->GetResource();

    auto &resource_entry = resource_logs_index[resource];
    if (resource_entry.resource_logs == nullptr)
    {
      resource_entry.resource_logs = request->add_resource_logs();
      if (resource != nullptr)
      {
        OtlpPopulateAttributeUtils::PopulateAttribute(
            resource_entry.resource_logs->mutable_resource(), *resource);
        resource_entry.resource_logs->set_schema_url(resource->GetSchemaURL());
      }
    }

    auto &scope_logs = resource_entry.scope_logs[instrumentation];
    if (scope_logs == nullptr)
    {
      scope_logs = resource_entry.resource_logs->add_scope_logs();
      scope_logs->mutable_log_records()->Reserve(static_cast<int>(logs.size()));
      if (instrumentation != nullptr)
      {
        auto *proto_scope = scope_logs->mutable_scope();
        proto_scope->set_name(instrumentation->GetName());
        proto_scope->set_version(instrumentation->GetVersion());
        OtlpPopulateAttributeUtils::PopulateAttribute(proto_scope, *instrumentation);
        scope_logs->set_schema_url(instrumentation->GetSchemaURL());
      }
    }

    auto *proto_log = scope_logs->add_log_records();

    // Timestamps
    proto_log->set_time_unix_nano(
        static_cast<uint64_t>(log_data->GetTimestamp().time_since_epoch().count()));
    proto_log->set_observed_time_unix_nano(
        static_cast<uint64_t>(log_data->GetObservedTimestamp().time_since_epoch().count()));

    // Severity — SDK enum values are numerically identical to proto SeverityNumber (1–24).
    const auto sev = log_data->GetSeverity();
    proto_log->set_severity_number(static_cast<proto::logs::v1::SeverityNumber>(sev));
    const auto sev_text = log_data->GetSeverityText();
    proto_log->set_severity_text(sev_text.data(), sev_text.size());

    // Body
    OtlpPopulateAttributeUtils::PopulateAnyValue(proto_log->mutable_body(),
                                                 log_data->GetBody(), true);

    // Event name
    const auto event_name = log_data->GetEventName();
    if (!event_name.empty())
    {
      proto_log->set_event_name(event_name.data(), event_name.size());
    }

    // Trace context
    const auto &trace_id = log_data->GetTraceId();
    if (trace_id.IsValid())
    {
      proto_log->set_trace_id(reinterpret_cast<const char *>(trace_id.Id().data()),
                              opentelemetry::trace::TraceId::kSize);
    }
    const auto &span_id = log_data->GetSpanId();
    if (span_id.IsValid())
    {
      proto_log->set_span_id(reinterpret_cast<const char *>(span_id.Id().data()),
                             opentelemetry::trace::SpanId::kSize);
    }
    proto_log->set_flags(log_data->GetTraceFlags().flags());

    // Attributes
    const auto &attrs = log_data->GetAttributes();
    proto_log->mutable_attributes()->Reserve(static_cast<int>(attrs.size()));
    for (const auto &attr : attrs)
    {
      OtlpPopulateAttributeUtils::PopulateAttribute(proto_log->add_attributes(), attr.first,
                                                    attr.second, true);
    }
  }
}

//==============================================================================
// PmrSpanData export utilities
//==============================================================================

// PmrSpanData export path — serialises PmrSpanData directly to proto without
// going through OwnedAttributeValue.  PmrAttributeValue holds pmr::string and
// pmr::vector<T> variants; this local visitor writes them to AnyValue directly.
struct PmrAttributeValueProtoVisitor
{
  opentelemetry::proto::common::v1::AnyValue *proto_value;

  void operator()(bool v) const noexcept { proto_value->set_bool_value(v); }
  void operator()(int32_t v) const noexcept { proto_value->set_int_value(v); }
  void operator()(uint32_t v) const noexcept { proto_value->set_int_value(v); }
  void operator()(int64_t v) const noexcept { proto_value->set_int_value(v); }
  void operator()(double v) const noexcept { proto_value->set_double_value(v); }
  void operator()(uint64_t v) const noexcept
  {
    if (v <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      proto_value->set_int_value(static_cast<int64_t>(v));
    else
      proto_value->set_string_value(std::to_string(v));
  }
  void operator()(const std::pmr::string &v) const noexcept
  {
    proto_value->set_string_value(v.data(), v.size());
  }
  void operator()(const std::pmr::vector<bool> &v) const noexcept
  {
    auto *arr = proto_value->mutable_array_value();
    arr->mutable_values()->Reserve(static_cast<int>(v.size()));
    for (const auto val : v)
      arr->add_values()->set_bool_value(val);
  }
  void operator()(const std::pmr::vector<int32_t> &v) const noexcept
  {
    auto *arr = proto_value->mutable_array_value();
    arr->mutable_values()->Reserve(static_cast<int>(v.size()));
    for (const auto &val : v)
      arr->add_values()->set_int_value(val);
  }
  void operator()(const std::pmr::vector<uint32_t> &v) const noexcept
  {
    auto *arr = proto_value->mutable_array_value();
    arr->mutable_values()->Reserve(static_cast<int>(v.size()));
    for (const auto &val : v)
      arr->add_values()->set_int_value(static_cast<int64_t>(val));
  }
  void operator()(const std::pmr::vector<int64_t> &v) const noexcept
  {
    auto *arr = proto_value->mutable_array_value();
    arr->mutable_values()->Reserve(static_cast<int>(v.size()));
    for (const auto &val : v)
      arr->add_values()->set_int_value(val);
  }
  void operator()(const std::pmr::vector<double> &v) const noexcept
  {
    auto *arr = proto_value->mutable_array_value();
    arr->mutable_values()->Reserve(static_cast<int>(v.size()));
    for (const auto &val : v)
      arr->add_values()->set_double_value(val);
  }
  void operator()(const std::pmr::vector<std::pmr::string> &v) const noexcept
  {
    auto *arr = proto_value->mutable_array_value();
    arr->mutable_values()->Reserve(static_cast<int>(v.size()));
    for (const auto &val : v)
      arr->add_values()->set_string_value(val.data(), val.size());
  }
  void operator()(const std::pmr::vector<uint64_t> &v) const noexcept
  {
    auto *arr = proto_value->mutable_array_value();
    arr->mutable_values()->Reserve(static_cast<int>(v.size()));
    for (const auto &val : v)
      arr->add_values()->set_int_value(static_cast<int64_t>(val));
  }
  void operator()(const std::pmr::vector<uint8_t> &v) const noexcept
  {
    auto *arr = proto_value->mutable_array_value();
    arr->mutable_values()->Reserve(static_cast<int>(v.size()));
    for (const auto val : v)
      arr->add_values()->set_int_value(val);
  }
};

inline void PopulatePmrAttribute(
    opentelemetry::proto::common::v1::KeyValue *kv,
    const std::pmr::string &key,
    const opentelemetry::sdk::trace::PmrAttributeValue &value) noexcept
{
  kv->set_key(key.data(), key.size());
  std::visit(PmrAttributeValueProtoVisitor{kv->mutable_value()}, value);
}

inline void PopulatePmrAttrVec(
    google::protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> *dest,
    const opentelemetry::sdk::trace::PmrAttrVec &attrs) noexcept
{
  dest->Reserve(static_cast<int>(attrs.size()));
  for (const auto &kv : attrs)
    PopulatePmrAttribute(dest->Add(), kv.first, kv.second);
}

void OtlpRecordableUtils::PopulateRequestPmrSpanData(
      const nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> &spans,
      proto::collector::trace::v1::ExportTraceServiceRequest *request) noexcept
 {

    if (nullptr == request)
    {
      return;
    }

    using ScopeSpansMap =
        std::unordered_map<const opentelemetry::sdk::instrumentationscope::InstrumentationScope *,
                          proto::trace::v1::ScopeSpans *, InstrumentationScopePointerHasher, InstrumentationScopePointerEqual>;
    struct ResourceSpansEntry
    {
      proto::trace::v1::ResourceSpans *resource_spans = nullptr;
      ScopeSpansMap scope_spans;
    };
    std::unordered_map<const opentelemetry::sdk::resource::Resource *, ResourceSpansEntry> resource_index;

    for (const auto &recordable : spans)
    {
      const auto *span_data       = static_cast<const sdk::trace::PmrSpanData *>(recordable.get());
      const auto *resource        = &span_data->GetResource();
      const auto *instrumentation = &span_data->GetInstrumentationScope();

      auto &entry = resource_index[resource];
      if (entry.resource_spans == nullptr)
      {
        entry.resource_spans = request->add_resource_spans();
        otlp::OtlpPopulateAttributeUtils::PopulateAttribute(
            entry.resource_spans->mutable_resource(), *resource);
        entry.resource_spans->set_schema_url(resource->GetSchemaURL());
      }

      auto &scope_spans = entry.scope_spans[instrumentation];
      if (scope_spans == nullptr)
      {
        scope_spans = entry.resource_spans->add_scope_spans();
        scope_spans->mutable_spans()->Reserve(static_cast<int>(spans.size()));
        if (instrumentation != nullptr)
        {
          auto *scope_proto = scope_spans->mutable_scope();
          scope_proto->set_name(instrumentation->GetName());
          scope_proto->set_version(instrumentation->GetVersion());
          otlp::OtlpPopulateAttributeUtils::PopulateAttribute(scope_proto, *instrumentation);
          scope_spans->set_schema_url(instrumentation->GetSchemaURL());
        }
      }

      auto *proto_span = scope_spans->add_spans();

      // Identity
      proto_span->set_trace_id(
          reinterpret_cast<const char *>(span_data->GetSpanContext().trace_id().Id().data()),
          trace::TraceId::kSize);
      proto_span->set_span_id(
          reinterpret_cast<const char *>(span_data->GetSpanContext().span_id().Id().data()),
          trace::SpanId::kSize);
      if (span_data->GetParentSpanId().IsValid())
        proto_span->set_parent_span_id(
            reinterpret_cast<const char *>(span_data->GetParentSpanId().Id().data()),
            trace::SpanId::kSize);
      proto_span->set_trace_state(span_data->GetSpanContext().trace_state()->ToHeader());

      // Name, kind, timing
      proto_span->set_name(span_data->GetName().data(), span_data->GetName().size());
      proto_span->set_kind(static_cast<proto::trace::v1::Span_SpanKind>(span_data->GetSpanKind()));
      const uint64_t start_ns =
          static_cast<uint64_t>(span_data->GetStartTime().time_since_epoch().count());
      proto_span->set_start_time_unix_nano(start_ns);
      proto_span->set_end_time_unix_nano(start_ns +
                                         static_cast<uint64_t>(span_data->GetDuration().count()));
      proto_span->set_flags(span_data->GetFlags().flags() &
                            proto::trace::v1::SPAN_FLAGS_TRACE_FLAGS_MASK);

      // Status
      if (span_data->GetStatus() != trace::StatusCode::kUnset)
      {
        proto_span->mutable_status()->set_code(
            proto::trace::v1::Status_StatusCode(span_data->GetStatus()));
        if (span_data->GetStatus() == trace::StatusCode::kError)
        {
          const auto desc = span_data->GetDescription();
          proto_span->mutable_status()->set_message(desc.data(), desc.size());
        }
      }

      // Span-level attributes (pmr::unordered_map)
      const auto &attrs = span_data->GetAttributes();
      proto_span->mutable_attributes()->Reserve(static_cast<int>(attrs.size()));
      for (const auto &kv : attrs)
        PopulatePmrAttribute(proto_span->add_attributes(), kv.first, kv.second);

      // Events
      const auto &events = span_data->GetEvents();
      proto_span->mutable_events()->Reserve(static_cast<int>(events.size()));
      for (const auto &event : events)
      {
        auto *proto_event = proto_span->add_events();
        proto_event->set_name(event.GetName().data(), event.GetName().size());
        proto_event->set_time_unix_nano(
            static_cast<uint64_t>(event.GetTimestamp().time_since_epoch().count()));
        PopulatePmrAttrVec(proto_event->mutable_attributes(), event.GetAttributes());
      }

      // Links
      const auto &links = span_data->GetLinks();
      proto_span->mutable_links()->Reserve(static_cast<int>(links.size()));
      for (const auto &link : links)
      {
        auto *proto_link = proto_span->add_links();
        proto_link->set_trace_id(
            reinterpret_cast<const char *>(link.GetSpanContext().trace_id().Id().data()),
            trace::TraceId::kSize);
        proto_link->set_span_id(
            reinterpret_cast<const char *>(link.GetSpanContext().span_id().Id().data()),
            trace::SpanId::kSize);
        proto_link->set_trace_state(link.GetSpanContext().trace_state()->ToHeader());
        PopulatePmrAttrVec(proto_link->mutable_attributes(), link.GetAttributes());
      }
    }
 }

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
