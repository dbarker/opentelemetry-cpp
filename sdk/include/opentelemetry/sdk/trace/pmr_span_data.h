// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/version.h"

// PmrSpanData requires C++17 <memory_resource>.
#if defined(__cpp_lib_memory_resource)

#  include <atomic>
#  include <chrono>
#  include <cstddef>
#  include <memory_resource>
#  include <new>
#  include <string>
#  include <utility>
#  include <variant>
#  include <vector>

#  include "opentelemetry/common/attribute_value.h"
#  include "opentelemetry/common/key_value_iterable.h"
#  include "opentelemetry/common/key_value_iterable_view.h"
#  include "opentelemetry/common/timestamp.h"
#  include "opentelemetry/nostd/string_view.h"
#  include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#  include "opentelemetry/sdk/resource/resource.h"
#  include "opentelemetry/sdk/trace/recordable.h"
#  include "opentelemetry/trace/span_context.h"
#  include "opentelemetry/trace/span_id.h"
#  include "opentelemetry/trace/span_metadata.h"
#  include "opentelemetry/trace/trace_flags.h"
#  include "opentelemetry/trace/trace_id.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

// ---------------------------------------------------------------------------
// PmrAttributeValue — mirrors OwnedAttributeValue alternatives, but every
// owning type is a pmr type so all dynamic allocation comes from the span's
// monotonic arena.  std::variant itself is not allocator-aware; the pmr types
// carry the resource pointer internally.
// ---------------------------------------------------------------------------
using PmrAttributeValue = std::variant<bool,
                                       int32_t,
                                       uint32_t,
                                       int64_t,
                                       double,
                                       std::pmr::string,
                                       std::pmr::vector<bool>,
                                       std::pmr::vector<int32_t>,
                                       std::pmr::vector<uint32_t>,
                                       std::pmr::vector<int64_t>,
                                       std::pmr::vector<double>,
                                       std::pmr::vector<std::pmr::string>,
                                       uint64_t,
                                       std::pmr::vector<uint64_t>,
                                       std::pmr::vector<uint8_t>>;

// Flat key-value pair — used for event/link attributes where cardinality is
// small and a linear scan for dedup is cheaper than unordered_map bucket setup.
using PmrAttrEntry = std::pair<std::pmr::string, PmrAttributeValue>;
using PmrAttrVec   = std::pmr::vector<PmrAttrEntry>;

// ---------------------------------------------------------------------------
// PmrSpanDataEvent — fully owning, all dynamic storage in the parent span's
// arena.  Flat vector avoids the cold unordered_map allocation that
// SpanDataEvent pays for each event.
// ---------------------------------------------------------------------------
class PmrSpanDataEvent
{
public:
  PmrSpanDataEvent(opentelemetry::nostd::string_view name,
                   opentelemetry::common::SystemTimestamp timestamp,
                   const opentelemetry::common::KeyValueIterable &attributes,
                   std::pmr::polymorphic_allocator<std::byte> alloc);

  opentelemetry::nostd::string_view GetName() const noexcept
  {
    return {name_.data(), name_.size()};
  }
  opentelemetry::common::SystemTimestamp GetTimestamp() const noexcept { return timestamp_; }
  const PmrAttrVec &GetAttributes() const noexcept { return attributes_; }

private:
  std::pmr::string                       name_;
  opentelemetry::common::SystemTimestamp timestamp_;
  PmrAttrVec                             attributes_;
};

// ---------------------------------------------------------------------------
// PmrSpanDataLink — same arena as parent span.
// ---------------------------------------------------------------------------
class PmrSpanDataLink
{
public:
  PmrSpanDataLink(opentelemetry::trace::SpanContext span_context,
                  const opentelemetry::common::KeyValueIterable &attributes,
                  std::pmr::polymorphic_allocator<std::byte> alloc);

  const opentelemetry::trace::SpanContext &GetSpanContext() const noexcept { return context_; }
  const PmrAttrVec &GetAttributes() const noexcept { return attributes_; }

private:
  opentelemetry::trace::SpanContext context_;
  PmrAttrVec                        attributes_;
};

// ---------------------------------------------------------------------------
// PmrSpanData — owning span recordable backed by a per-span monotonic arena.
//
// Allocation strategy:
//   buf_[kInlineBytes] is used as the initial block of a monotonic_buffer_resource.
//   All pmr containers (attributes_, events_, links_, pmr::string fields) draw
//   from this resource via alloc_.  When buf_ is exhausted the resource
//   transparently falls back to the heap using exponentially growing blocks.
//
//   On destruction the monotonic_buffer_resource releases all heap blocks in a
//   single upstream_resource->deallocate call, making destruction O(1) with
//   respect to the number of attributes stored.
//
// kInlineBytes sizing:
//   unordered_map node ≈ 80 B (key + value + hash + next ptr), bucket ptr ≈ 8 B
//   pmr::string short key (≤ 15 B) stays in SSO, no arena heap needed
//   → 8 attrs ≈ 768 B, 16 attrs ≈ 1.5 KB, 32 attrs ≈ 3 KB
//   4 KB covers the common case (p90 span ≤ ~30 attrs) with no heap spill.
//   Spans with more attributes spill to heap transparently.
//
// Non-movable: std::pmr::monotonic_buffer_resource is pinned; pmr containers
// store a pointer to it.  Moving the PmrSpanData would leave dangling pointers.
// The SDK creates recordables behind unique_ptr and never moves them.
// ---------------------------------------------------------------------------
class PmrSpanData final : public Recordable
{
public:
  static constexpr std::size_t kInlineBytes = 4096;

  PmrSpanData();

  PmrSpanData(const PmrSpanData &)            = delete;
  PmrSpanData &operator=(const PmrSpanData &) = delete;
  PmrSpanData(PmrSpanData &&)                 = delete;
  PmrSpanData &operator=(PmrSpanData &&)      = delete;

  ~PmrSpanData() override = default;

  // --- Recordable interface -------------------------------------------------

  void SetIdentity(const opentelemetry::trace::SpanContext &span_context,
                   opentelemetry::trace::SpanId parent_span_id) noexcept override;

  // Last-write-wins on duplicate keys (same semantics as SpanData).
  // insert_or_assign constructs a pmr::string key for lookup; on collision
  // the abandoned key string's memory stays in the arena (no-op dealloc).
  void SetAttribute(opentelemetry::nostd::string_view key,
                    const opentelemetry::common::AttributeValue &value) noexcept override;

  // Calls attributes_.reserve() before the loop to pre-size the bucket array
  // and avoid rehash during bulk insert (e.g. StartSpan initial attrs).
  void SetAttributes(
      const opentelemetry::common::KeyValueIterable &attributes) noexcept override;

  void AddEvent(opentelemetry::nostd::string_view name,
                opentelemetry::common::SystemTimestamp timestamp,
                const opentelemetry::common::KeyValueIterable &attributes) noexcept override;

  void AddLink(const opentelemetry::trace::SpanContext &span_context,
               const opentelemetry::common::KeyValueIterable &attributes) noexcept override;

  void SetStatus(opentelemetry::trace::StatusCode code,
                 opentelemetry::nostd::string_view description) noexcept override;

  void SetName(opentelemetry::nostd::string_view name) noexcept override;
  void SetTraceFlags(opentelemetry::trace::TraceFlags flags) noexcept override;
  void SetSpanKind(opentelemetry::trace::SpanKind span_kind) noexcept override;
  void SetResource(const opentelemetry::sdk::resource::Resource &resource) noexcept override;
  void SetStartTime(opentelemetry::common::SystemTimestamp start_time) noexcept override;
  void SetDuration(std::chrono::nanoseconds duration) noexcept override;
  void SetInstrumentationScope(
      const opentelemetry::sdk::instrumentationscope::InstrumentationScope
          &instrumentation_scope) noexcept override;

  // --- Reader interface -----------------------------------------------------

  opentelemetry::nostd::string_view GetName() const noexcept
  {
    return {name_.data(), name_.size()};
  }
  const opentelemetry::trace::SpanContext &GetSpanContext() const noexcept { return context_; }
  opentelemetry::trace::SpanId GetParentSpanId() const noexcept { return parent_span_id_; }
  opentelemetry::trace::SpanKind GetSpanKind() const noexcept { return kind_; }
  opentelemetry::trace::TraceFlags GetFlags() const noexcept { return flags_; }
  opentelemetry::common::SystemTimestamp GetStartTime() const noexcept { return start_time_; }
  std::chrono::nanoseconds GetDuration() const noexcept { return duration_; }
  opentelemetry::trace::StatusCode GetStatus() const noexcept { return status_code_; }
  opentelemetry::nostd::string_view GetDescription() const noexcept
  {
    return {desc_.data(), desc_.size()};
  }

  const opentelemetry::sdk::resource::Resource &GetResource() const noexcept;
  const opentelemetry::sdk::instrumentationscope::InstrumentationScope &
      GetInstrumentationScope() const noexcept;

  // Attribute map — O(1) amortized lookup/insert via pmr::unordered_map.
  const std::pmr::unordered_map<std::pmr::string, PmrAttributeValue> &GetAttributes()
      const noexcept
  {
    return attributes_;
  }

  // Events and links — contiguous arena-backed vectors.
  const std::pmr::vector<PmrSpanDataEvent> &GetEvents() const noexcept { return events_; }
  const std::pmr::vector<PmrSpanDataLink> &GetLinks() const noexcept { return links_; }

  // Expose allocator so PmrSpanDataEvent/Link can share the same resource.
  std::pmr::polymorphic_allocator<std::byte> GetAllocator() const noexcept { return alloc_; }

private:
  // Converts a non-owning AttributeValue into an owning PmrAttributeValue,
  // allocating any dynamic storage (strings, vectors) from alloc_.
  PmrAttributeValue convertAttr(const opentelemetry::common::AttributeValue &v) noexcept;

  // ---------------------------------------------------------------------------
  // Storage layout — order matters: arena members must be initialised before
  // any pmr containers that reference them.
  // ---------------------------------------------------------------------------

  // 1. Inline buffer — initial block for the monotonic resource.
  alignas(std::max_align_t) std::byte buf_[kInlineBytes];

  // 2. Monotonic resource — backed by buf_, spills to heap when exhausted.
  std::pmr::monotonic_buffer_resource mono_;

  // 3. Allocator — thin wrapper around &mono_, passed to all pmr containers.
  std::pmr::polymorphic_allocator<std::byte> alloc_;

  // 4. Fixed-size identity/metadata — no arena allocation needed.
  opentelemetry::trace::SpanContext        context_;
  opentelemetry::trace::SpanId            parent_span_id_;
  opentelemetry::trace::SpanKind          kind_{opentelemetry::trace::SpanKind::kInternal};
  opentelemetry::trace::TraceFlags        flags_;
  opentelemetry::trace::StatusCode        status_code_{opentelemetry::trace::StatusCode::kUnset};
  opentelemetry::common::SystemTimestamp  start_time_;
  std::chrono::nanoseconds                duration_{0};

  // 5. Arena-allocated strings.
  std::pmr::string name_;
  std::pmr::string desc_;

  // 6. Non-owning pointers (SDK lifetime guarantee: resource/scope outlive span).
  const opentelemetry::sdk::resource::Resource                              *resource_{nullptr};
  const opentelemetry::sdk::instrumentationscope::InstrumentationScope *scope_{nullptr};

  // 7. Arena-allocated collections.
  //    unordered_map for O(1) amortized dedup on the attribute-write path.
  //    pmr::vector for events/links: no per-event cold map construction.
  std::pmr::unordered_map<std::pmr::string, PmrAttributeValue> attributes_;
  std::pmr::vector<PmrSpanDataEvent>                           events_;
  std::pmr::vector<PmrSpanDataLink>                            links_;
};

}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE

#endif  // __cpp_lib_memory_resource
