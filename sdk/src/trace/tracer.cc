// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>
#include <chrono>
#include <new>
#include <utility>

#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/common/macros.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/instrumentationscope/scope_configurator.h"
#include "opentelemetry/sdk/trace/id_generator.h"
#include "opentelemetry/sdk/trace/sampler.h"
#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/sdk/trace/tracer_config.h"
#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_context_kv_iterable.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/trace/trace_state.h"
#include "opentelemetry/version.h"

#include "src/trace/span.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

// Fallback noop span for error cases (thread-local, eagerly initialized per thread)
static thread_local const nostd::shared_ptr<opentelemetry::trace::Span> kFallbackNoopSpan{
    std::make_shared<opentelemetry::trace::NoopSpan>(nullptr)};

// Returns the fallback noop span for error cases
static nostd::shared_ptr<opentelemetry::trace::Span> GetNoopSpan(
    const std::shared_ptr<opentelemetry::trace::Tracer> &tracer,
    nostd::unique_ptr<opentelemetry::trace::SpanContext> &&span_context) noexcept
{
  // create no-op span with valid span-context.
  auto noop_span = nostd::shared_ptr<opentelemetry::trace::Span>{
      new (std::nothrow) opentelemetry::trace::NoopSpan(tracer, std::move(span_context))};
  if (!noop_span)
  {
    return kFallbackNoopSpan;
  }
  return noop_span;
}

Tracer::Tracer(std::shared_ptr<TracerContext> context,
               std::unique_ptr<InstrumentationScope> instrumentation_scope) noexcept
    : instrumentation_scope_{std::move(instrumentation_scope)},
      context_{std::move(context)},
      tracer_config_(context_->GetTracerConfigurator().ComputeConfig(*instrumentation_scope_))
{
#if OPENTELEMETRY_ABI_VERSION_NO >= 2
  UpdateEnabled(tracer_config_.IsEnabled());
#endif
}

nostd::shared_ptr<opentelemetry::trace::Span> Tracer::StartSpan(
    nostd::string_view name,
    const opentelemetry::common::KeyValueIterable &attributes,
    const opentelemetry::trace::SpanContextKeyValueIterable &links,
    const opentelemetry::trace::StartSpanOptions &options) noexcept
{
  if (!tracer_config_.IsEnabled())
  {
    auto ctx = nostd::unique_ptr<opentelemetry::trace::SpanContext>{
        new (std::nothrow) opentelemetry::trace::SpanContext(false, false)};
    if (!ctx)
    {
      return kFallbackNoopSpan;
    }
    return GetNoopSpan(this->shared_from_this(), std::move(ctx));
  }

  // Resolve parent span context from options or fall back to the current runtime context.
  const auto parent_context = [&options]() noexcept -> opentelemetry::trace::SpanContext {
    if (const auto *sc = nostd::get_if<opentelemetry::trace::SpanContext>(&options.parent))
    {
      if (sc->IsValid())
      {
        return *sc;
      }
    }
    else if (const auto *ctx = nostd::get_if<context::Context>(&options.parent))
    {
      auto pc = opentelemetry::trace::GetSpanContext(*ctx);
      if (pc.IsValid())
      {
        return pc;
      }
      if (opentelemetry::trace::IsRootSpan(*ctx))
      {
        return opentelemetry::trace::SpanContext(false, false);
      }
    }
    return opentelemetry::trace::GetSpanContext(
        opentelemetry::context::RuntimeContext::GetCurrent());
  }();

  IdGenerator &generator                     = GetIdGenerator();
  const opentelemetry::trace::SpanId span_id = generator.GenerateSpanId();
  const opentelemetry::trace::TraceId trace_id =
      parent_context.IsValid() ? parent_context.trace_id() : generator.GenerateTraceId();
  auto sampling_result = context_->GetSampler().ShouldSample(parent_context, trace_id, name,
                                                             options.kind, attributes, links);

  const opentelemetry::trace::TraceFlags trace_flags =
      [&]() noexcept -> opentelemetry::trace::TraceFlags {
    uint8_t f = 0;
    if (parent_context.IsValid())
    {
      f = parent_context.trace_flags().flags();
    }
    else if (generator.IsRandom())
    {
      f = opentelemetry::trace::TraceFlags::kIsRandom;
    }

    if (sampling_result.IsSampled())
    {
      f |= opentelemetry::trace::TraceFlags::kIsSampled;
    }
    else
    {
      f &= ~opentelemetry::trace::TraceFlags::kIsSampled;
    }

#if 1
    /* https://github.com/open-telemetry/opentelemetry-specification as of v1.29.0 */
    /* Support W3C Trace Context version 1. */
    f &= opentelemetry::trace::TraceFlags::kAllW3CTraceContext1Flags;
#endif

#if 0
    /* Waiting for https://github.com/open-telemetry/opentelemetry-specification/issues/3411 */
    /* Support W3C Trace Context version 2. */
    f &= opentelemetry::trace::TraceFlags::kAllW3CTraceContext2Flags;
#endif

    return opentelemetry::trace::TraceFlags(f);
  }();

  auto trace_state = sampling_result.trace_state;
  if (!trace_state)
  {
    trace_state = parent_context.IsValid() ? parent_context.trace_state()
                                           : opentelemetry::trace::TraceState::GetDefault();
  }

  auto span_context = nostd::unique_ptr<opentelemetry::trace::SpanContext>{
      new (std::nothrow) opentelemetry::trace::SpanContext(trace_id, span_id, trace_flags, false,
                                                           std::move(trace_state))};
  if (!span_context)
  {
    return kFallbackNoopSpan;
  }

  if (!sampling_result.IsRecording())
  {
    return GetNoopSpan(this->shared_from_this(), std::move(span_context));
  }

  auto span = nostd::shared_ptr<opentelemetry::trace::Span>{
      new (std::nothrow) Span(this->shared_from_this(), name, attributes, links, options,
                              parent_context, std::move(span_context))};

  // span_context is potentially moved-from at this point; do not reuse.
  if (!span)
  {
    return kFallbackNoopSpan;
  }

  if (sampling_result.attributes)
  {
    for (auto &kv : *sampling_result.attributes)
    {
      span->SetAttribute(kv.first, kv.second);
    }
  }

  return span;
}

void Tracer::ForceFlushWithMicroseconds(uint64_t timeout) noexcept
{
  if (context_)
  {
    context_->ForceFlush(
        std::chrono::microseconds{static_cast<std::chrono::microseconds::rep>(timeout)});
  }
}

void Tracer::CloseWithMicroseconds(uint64_t timeout) noexcept
{
  // Trace context is shared by many tracers.So we just call ForceFlush to flush all pending spans
  // and do not shutdown it.
  if (context_)
  {
    context_->ForceFlush(
        std::chrono::microseconds{static_cast<std::chrono::microseconds::rep>(timeout)});
  }
}
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
