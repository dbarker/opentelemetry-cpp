// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

SpanDataEvent::SpanDataEvent(std::string name,
                             opentelemetry::common::SystemTimestamp timestamp,
                             const opentelemetry::common::KeyValueIterable &attributes)
    : name_(std::move(name)), timestamp_(timestamp)
{
  attributes.ForEachKeyValue(
      [this](opentelemetry::nostd::string_view key,
             const opentelemetry::common::AttributeValue &value) noexcept {
        attributes_[std::string(key)] =
            opentelemetry::nostd::visit(opentelemetry::sdk::common::AttributeConverter(), value);
        return true;
      });
}

const std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue> &
SpanDataEvent::GetAttributes() const noexcept
{
  return attributes_;
}

SpanDataLink::SpanDataLink(opentelemetry::trace::SpanContext span_context,
                           const opentelemetry::common::KeyValueIterable &attributes)
    : span_context_(std::move(span_context))
{
  attributes.ForEachKeyValue(
      [this](opentelemetry::nostd::string_view key,
             const opentelemetry::common::AttributeValue &value) noexcept {
        attributes_[std::string(key)] =
            opentelemetry::nostd::visit(opentelemetry::sdk::common::AttributeConverter(), value);
        return true;
      });
}

const std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue> &
SpanDataLink::GetAttributes() const noexcept
{
  return attributes_;
}

const opentelemetry::sdk::resource::Resource &SpanData::GetResource() const noexcept
{
  if (resource_ == nullptr)
  {
    // this shouldn't happen as TraceProvider provides default resources
    static opentelemetry::sdk::resource::Resource resource =
        opentelemetry::sdk::resource::Resource::GetEmpty();
    return resource;
  }
  return *resource_;
}

const opentelemetry::sdk::trace::InstrumentationScope &SpanData::GetInstrumentationScope()
    const noexcept
{
  if (instrumentation_scope_ == nullptr)
  {
    // this shouldn't happen as Tracer ensures there is valid default instrumentation scope.
    static std::unique_ptr<opentelemetry::sdk::instrumentationscope::InstrumentationScope>
        instrumentation_scope =
            opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create(
                "unknown_service");
    return *instrumentation_scope;
  }
  return *instrumentation_scope_;
}

const std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue> &
SpanData::GetAttributes() const noexcept
{
  return attributes_;
}

void SpanData::SetIdentity(const opentelemetry::trace::SpanContext &span_context,
                           opentelemetry::trace::SpanId parent_span_id) noexcept
{
  span_context_   = span_context;
  parent_span_id_ = parent_span_id;
}

void SpanData::SetAttribute(nostd::string_view key,
                            const opentelemetry::common::AttributeValue &value) noexcept
{
  attributes_[std::string(key)] =
      opentelemetry::nostd::visit(opentelemetry::sdk::common::AttributeConverter(), value);
}

void SpanData::SetAttributes(
    const opentelemetry::common::KeyValueIterable &attributes) noexcept
{
  attributes_.reserve(attributes_.size() + attributes.size());
  attributes.ForEachKeyValue(
      [this](opentelemetry::nostd::string_view key,
             const opentelemetry::common::AttributeValue &value) noexcept {
        attributes_[std::string(key)] = opentelemetry::nostd::visit(
            opentelemetry::sdk::common::AttributeConverter(), value);
        return true;
      });
}

void SpanData::AddEvent(nostd::string_view name,
                        opentelemetry::common::SystemTimestamp timestamp,
                        const opentelemetry::common::KeyValueIterable &attributes) noexcept
{
  events_.emplace_back(std::string(name), timestamp, attributes);
}

void SpanData::AddLink(const opentelemetry::trace::SpanContext &span_context,
                       const opentelemetry::common::KeyValueIterable &attributes) noexcept
{
  links_.emplace_back(span_context, attributes);
}

void SpanData::SetStatus(opentelemetry::trace::StatusCode code,
                         nostd::string_view description) noexcept
{
  status_code_ = code;
  status_desc_ = std::string(description);
}

void SpanData::SetName(nostd::string_view name) noexcept
{
  name_ = std::string(name.data(), name.length());
}

void SpanData::SetTraceFlags(opentelemetry::trace::TraceFlags flags) noexcept
{
  flags_ = flags;
}

void SpanData::SetSpanKind(opentelemetry::trace::SpanKind span_kind) noexcept
{
  span_kind_ = span_kind;
}

void SpanData::SetResource(const opentelemetry::sdk::resource::Resource &resource) noexcept
{
  resource_ = &resource;
}

void SpanData::SetStartTime(opentelemetry::common::SystemTimestamp start_time) noexcept
{
  start_time_ = start_time;
}

void SpanData::SetDuration(std::chrono::nanoseconds duration) noexcept
{
  duration_ = duration;
}

void SpanData::SetInstrumentationScope(const InstrumentationScope &instrumentation_scope) noexcept
{
  instrumentation_scope_ = &instrumentation_scope;
}

}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
