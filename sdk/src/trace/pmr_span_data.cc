// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/version.h"

#if defined(__cpp_lib_memory_resource)

#  include <algorithm>
#  include <chrono>
#  include <cstddef>
#  include <memory>
#  include <memory_resource>
#  include <string>
#  include <utility>
#  include <variant>
#  include <vector>

#  include "opentelemetry/common/attribute_value.h"
#  include "opentelemetry/common/key_value_iterable.h"
#  include "opentelemetry/common/timestamp.h"
#  include "opentelemetry/nostd/string_view.h"
#  include "opentelemetry/nostd/variant.h"
#  include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#  include "opentelemetry/sdk/resource/resource.h"
#  include "opentelemetry/sdk/trace/pmr_span_data.h"
#  include "opentelemetry/trace/span_context.h"
#  include "opentelemetry/trace/span_id.h"
#  include "opentelemetry/trace/span_metadata.h"
#  include "opentelemetry/trace/trace_flags.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

namespace
{

// ---------------------------------------------------------------------------
// PmrAttributeConverter — visits a non-owning AttributeValue and produces an
// owning PmrAttributeValue with all dynamic storage allocated from alloc.
// ---------------------------------------------------------------------------
struct PmrAttributeConverter
{
  std::pmr::polymorphic_allocator<std::byte> alloc;

  PmrAttributeValue operator()(bool v) noexcept { return v; }
  PmrAttributeValue operator()(int32_t v) noexcept { return v; }
  PmrAttributeValue operator()(uint32_t v) noexcept { return v; }
  PmrAttributeValue operator()(int64_t v) noexcept { return v; }
  PmrAttributeValue operator()(uint64_t v) noexcept { return v; }
  PmrAttributeValue operator()(double v) noexcept { return v; }

  PmrAttributeValue operator()(opentelemetry::nostd::string_view v) noexcept
  {
    return std::pmr::string(v.data(), v.size(), alloc);
  }

  PmrAttributeValue operator()(const char *v) noexcept
  {
    return std::pmr::string(v, alloc);
  }

  PmrAttributeValue operator()(opentelemetry::nostd::span<const bool> v) noexcept
  {
    return std::pmr::vector<bool>(v.begin(), v.end(), alloc);
  }

  PmrAttributeValue operator()(opentelemetry::nostd::span<const int32_t> v) noexcept
  {
    return std::pmr::vector<int32_t>(v.begin(), v.end(), alloc);
  }

  PmrAttributeValue operator()(opentelemetry::nostd::span<const uint32_t> v) noexcept
  {
    return std::pmr::vector<uint32_t>(v.begin(), v.end(), alloc);
  }

  PmrAttributeValue operator()(opentelemetry::nostd::span<const int64_t> v) noexcept
  {
    return std::pmr::vector<int64_t>(v.begin(), v.end(), alloc);
  }

  PmrAttributeValue operator()(opentelemetry::nostd::span<const uint64_t> v) noexcept
  {
    return std::pmr::vector<uint64_t>(v.begin(), v.end(), alloc);
  }

  PmrAttributeValue operator()(opentelemetry::nostd::span<const double> v) noexcept
  {
    return std::pmr::vector<double>(v.begin(), v.end(), alloc);
  }

  PmrAttributeValue operator()(
      opentelemetry::nostd::span<const opentelemetry::nostd::string_view> v) noexcept
  {
    std::pmr::vector<std::pmr::string> result(alloc);
    result.reserve(v.size());
    for (const auto &sv : v)
    {
      // The vector's polymorphic_allocator::construct injects *its* allocator
      // automatically (trailing-allocator convention), so we must NOT pass
      // alloc explicitly — that would produce a 4-argument call with no match.
      result.emplace_back(sv.data(), sv.size());
    }
    return result;
  }

  PmrAttributeValue operator()(opentelemetry::nostd::span<const uint8_t> v) noexcept
  {
    return std::pmr::vector<uint8_t>(v.begin(), v.end(), alloc);
  }
};

// Fills a PmrAttrVec from a KeyValueIterable.  alloc is used for both the
// key strings and the attribute values.
void FillAttrVec(PmrAttrVec &out,
                 const opentelemetry::common::KeyValueIterable &attrs,
                 std::pmr::polymorphic_allocator<std::byte> alloc) noexcept
{
  PmrAttributeConverter conv{alloc};
  attrs.ForEachKeyValue(
      [&](opentelemetry::nostd::string_view key,
          const opentelemetry::common::AttributeValue &value) noexcept {
        out.emplace_back(std::pmr::string(key.data(), key.size(), alloc),
                         opentelemetry::nostd::visit(conv, value));
        return true;
      });
}

}  // namespace

// ---------------------------------------------------------------------------
// PmrSpanDataEvent
// ---------------------------------------------------------------------------

PmrSpanDataEvent::PmrSpanDataEvent(opentelemetry::nostd::string_view name,
                                   opentelemetry::common::SystemTimestamp timestamp,
                                   const opentelemetry::common::KeyValueIterable &attributes,
                                   std::pmr::polymorphic_allocator<std::byte> alloc)
    : name_(name.data(), name.size(), alloc), timestamp_(timestamp), attributes_(alloc)
{
  FillAttrVec(attributes_, attributes, alloc);
}

// ---------------------------------------------------------------------------
// PmrSpanDataLink
// ---------------------------------------------------------------------------

PmrSpanDataLink::PmrSpanDataLink(opentelemetry::trace::SpanContext span_context,
                                 const opentelemetry::common::KeyValueIterable &attributes,
                                 std::pmr::polymorphic_allocator<std::byte> alloc)
    : context_(std::move(span_context)), attributes_(alloc)
{
  FillAttrVec(attributes_, attributes, alloc);
}

// ---------------------------------------------------------------------------
// PmrSpanData
// ---------------------------------------------------------------------------

PmrSpanData::PmrSpanData()
    : mono_(buf_, sizeof(buf_))
    , alloc_(&mono_)
    , context_(false, false)
    , name_(alloc_)
    , desc_(alloc_)
    , attributes_(alloc_)
    , events_(alloc_)
    , links_(alloc_)
{}

PmrAttributeValue PmrSpanData::convertAttr(
    const opentelemetry::common::AttributeValue &v) noexcept
{
  return opentelemetry::nostd::visit(PmrAttributeConverter{alloc_}, v);  // NOLINT
}

void PmrSpanData::SetIdentity(const opentelemetry::trace::SpanContext &span_context,
                              opentelemetry::trace::SpanId parent_span_id) noexcept
{
  context_        = span_context;
  parent_span_id_ = parent_span_id;
}

void PmrSpanData::SetAttribute(opentelemetry::nostd::string_view key,
                               const opentelemetry::common::AttributeValue &value) noexcept
{
  // insert_or_assign: constructs a pmr::string key from the arena.
  // On key collision the new value replaces the old; the temporary key string's
  // memory stays in the arena (monotonic dealloc is a no-op).
  attributes_.insert_or_assign(std::pmr::string(key.data(), key.size(), alloc_),
                               convertAttr(value));
}

void PmrSpanData::SetAttributes(
    const opentelemetry::common::KeyValueIterable &attributes) noexcept
{
  // Pre-size the bucket array to avoid rehash during bulk insert.
  attributes_.reserve(attributes_.size() + attributes.size());
  attributes.ForEachKeyValue(
      [this](opentelemetry::nostd::string_view key,
             const opentelemetry::common::AttributeValue &value) noexcept {
        attributes_.insert_or_assign(std::pmr::string(key.data(), key.size(), alloc_),
                                     convertAttr(value));
        return true;
      });
}

void PmrSpanData::AddEvent(opentelemetry::nostd::string_view name,
                           opentelemetry::common::SystemTimestamp timestamp,
                           const opentelemetry::common::KeyValueIterable &attributes) noexcept
{
  events_.emplace_back(name, timestamp, attributes, alloc_);
}

void PmrSpanData::AddLink(const opentelemetry::trace::SpanContext &span_context,
                          const opentelemetry::common::KeyValueIterable &attributes) noexcept
{
  links_.emplace_back(span_context, attributes, alloc_);
}

void PmrSpanData::SetStatus(opentelemetry::trace::StatusCode code,
                            opentelemetry::nostd::string_view description) noexcept
{
  status_code_ = code;
  desc_.assign(description.data(), description.size());
}

void PmrSpanData::SetName(opentelemetry::nostd::string_view name) noexcept
{
  name_.assign(name.data(), name.size());
}

void PmrSpanData::SetTraceFlags(opentelemetry::trace::TraceFlags flags) noexcept
{
  flags_ = flags;
}

void PmrSpanData::SetSpanKind(opentelemetry::trace::SpanKind span_kind) noexcept
{
  kind_ = span_kind;
}

void PmrSpanData::SetResource(
    const opentelemetry::sdk::resource::Resource &resource) noexcept
{
  resource_ = &resource;
}

void PmrSpanData::SetStartTime(opentelemetry::common::SystemTimestamp start_time) noexcept
{
  start_time_ = start_time;
}

void PmrSpanData::SetDuration(std::chrono::nanoseconds duration) noexcept
{
  duration_ = duration;
}

void PmrSpanData::SetInstrumentationScope(
    const opentelemetry::sdk::instrumentationscope::InstrumentationScope
        &instrumentation_scope) noexcept
{
  scope_ = &instrumentation_scope;
}

const opentelemetry::sdk::resource::Resource &PmrSpanData::GetResource() const noexcept
{
  if (OPENTELEMETRY_UNLIKELY_CONDITION(resource_ == nullptr))
  {
    static opentelemetry::sdk::resource::Resource empty =
        opentelemetry::sdk::resource::Resource::GetEmpty();
    return empty;
  }
  return *resource_;
}

const opentelemetry::sdk::instrumentationscope::InstrumentationScope &
PmrSpanData::GetInstrumentationScope() const noexcept
{
  if (OPENTELEMETRY_UNLIKELY_CONDITION(scope_ == nullptr))
  {
    static std::unique_ptr<opentelemetry::sdk::instrumentationscope::InstrumentationScope>
        fallback =
            opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("unknown");
    return *fallback;
  }
  return *scope_;
}

}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE

#endif  // __cpp_lib_memory_resource
