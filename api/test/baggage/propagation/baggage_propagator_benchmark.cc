// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>
#include <string>

#include "opentelemetry/baggage/baggage.h"
#include "opentelemetry/baggage/baggage_context.h"
#include "opentelemetry/baggage/propagation/baggage_propagator.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/nostd/string_view.h"

using namespace opentelemetry;
using namespace opentelemetry::baggage::propagation;

namespace
{

constexpr size_t kNominalEntries = 5;

class BaggageCarrier : public context::propagation::TextMapCarrier
{
public:
  nostd::string_view Get(nostd::string_view key) const noexcept override
  {
    if (key == baggage::kBaggageHeader)
      return header_;
    return "";
  }
  void Set(nostd::string_view /* key */, nostd::string_view /* value */) noexcept override {}

  nostd::string_view header_;
};

std::string MakeBaggageHeader(size_t num_entries)
{
  std::string header;
  for (size_t i = 0; i < num_entries; i++)
  {
    std::string key   = "benchkey" + std::to_string(i);
    std::string value = "benchval" + std::to_string(i);
    header.append(key).append("=").append(value);
    if (i != num_entries - 1)
    {
      header += ",";
    }
  }
  return header;
}

static BaggagePropagator propagator;

void BM_BaggagePropagatorExtract(benchmark::State &state)
{
  std::string header = MakeBaggageHeader(kNominalEntries);
  BaggageCarrier carrier;
  carrier.header_ = header;
  while (state.KeepRunning())
  {
    context::Context ctx = context::Context{};
    propagator.Extract(carrier, ctx);
  }
}
BENCHMARK(BM_BaggagePropagatorExtract);

void BM_BaggagePropagatorInject(benchmark::State &state)
{
  std::string header = MakeBaggageHeader(kNominalEntries);
  BaggageCarrier extract_carrier;
  extract_carrier.header_ = header;
  context::Context ctx1   = context::Context{};
  context::Context ctx2   = propagator.Extract(extract_carrier, ctx1);

  while (state.KeepRunning())
  {
    BaggageCarrier inject_carrier;
    propagator.Inject(inject_carrier, ctx2);
  }
}
BENCHMARK(BM_BaggagePropagatorInject);

void BM_BaggagePropagatorExtractInject(benchmark::State &state)
{
  std::string header = MakeBaggageHeader(kNominalEntries);
  BaggageCarrier extract_carrier;
  extract_carrier.header_ = header;
  while (state.KeepRunning())
  {
    context::Context ctx1 = context::Context{};
    context::Context ctx2 = propagator.Extract(extract_carrier, ctx1);
    BaggageCarrier inject_carrier;
    propagator.Inject(inject_carrier, ctx2);
  }
}
BENCHMARK(BM_BaggagePropagatorExtractInject);

void BM_BaggagePropagatorExtract180Entries(benchmark::State &state)
{
  std::string header = MakeBaggageHeader(baggage::Baggage::kMaxKeyValuePairs);
  BaggageCarrier carrier;
  carrier.header_ = header;
  while (state.KeepRunning())
  {
    context::Context ctx = context::Context{};
    propagator.Extract(carrier, ctx);
  }
}
BENCHMARK(BM_BaggagePropagatorExtract180Entries);

void BM_BaggagePropagatorInject180Entries(benchmark::State &state)
{
  std::string header = MakeBaggageHeader(baggage::Baggage::kMaxKeyValuePairs);
  BaggageCarrier extract_carrier;
  extract_carrier.header_ = header;
  context::Context ctx1   = context::Context{};
  context::Context ctx2   = propagator.Extract(extract_carrier, ctx1);

  while (state.KeepRunning())
  {
    BaggageCarrier inject_carrier;
    propagator.Inject(inject_carrier, ctx2);
  }
}
BENCHMARK(BM_BaggagePropagatorInject180Entries);

}  // namespace

BENCHMARK_MAIN();
