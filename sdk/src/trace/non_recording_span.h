// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

/**
 * A lightweight non-recording span used when a span is not sampled.
 *
 * Replaces opentelemetry::trace::NoopSpan for the unsampled case produced by
 * the SDK tracer.  Unlike NoopSpan, the SpanContext is stored by value inside
 * the span object, saving one heap allocation per unsampled span start.
 */
class NonRecordingSpan final : public opentelemetry::trace::Span
{
public:
  explicit NonRecordingSpan(opentelemetry::trace::SpanContext context) noexcept
      : context_(std::move(context))
  {}

  // Not copyable or movable — lifetime managed via nostd::shared_ptr.
  NonRecordingSpan(const NonRecordingSpan &)            = delete;
  NonRecordingSpan(NonRecordingSpan &&)                 = delete;
  NonRecordingSpan &operator=(const NonRecordingSpan &) = delete;
  NonRecordingSpan &operator=(NonRecordingSpan &&)      = delete;

  ~NonRecordingSpan() override = default;

  // opentelemetry::trace::Span — all no-ops except identity accessors.
  void SetAttribute(opentelemetry::nostd::string_view /*key*/,
                    const opentelemetry::common::AttributeValue & /*value*/) noexcept override
  {}

  void AddEvent(opentelemetry::nostd::string_view /*name*/) noexcept override {}

  void AddEvent(opentelemetry::nostd::string_view /*name*/,
                opentelemetry::common::SystemTimestamp /*timestamp*/) noexcept override
  {}

  void AddEvent(opentelemetry::nostd::string_view /*name*/,
                const opentelemetry::common::KeyValueIterable & /*attributes*/) noexcept override
  {}

  void AddEvent(
      opentelemetry::nostd::string_view /*name*/,
      opentelemetry::common::SystemTimestamp /*timestamp*/,
      const opentelemetry::common::KeyValueIterable & /*attributes*/) noexcept override
  {}

#if OPENTELEMETRY_ABI_VERSION_NO >= 2
  void AddLink(const opentelemetry::trace::SpanContext & /*target*/,
               const opentelemetry::common::KeyValueIterable & /*attrs*/) noexcept override
  {}

  void AddLinks(
      const opentelemetry::trace::SpanContextKeyValueIterable & /*links*/) noexcept override
  {}
#endif

  void SetStatus(opentelemetry::trace::StatusCode /*code*/,
                 opentelemetry::nostd::string_view /*description*/) noexcept override
  {}

  void UpdateName(opentelemetry::nostd::string_view /*name*/) noexcept override {}

  void End(const opentelemetry::trace::EndSpanOptions & /*options*/ = {}) noexcept override {}

  bool IsRecording() const noexcept override { return false; }

  opentelemetry::trace::SpanContext GetContext() const noexcept override { return context_; }

private:
  opentelemetry::trace::SpanContext context_;
};

}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
