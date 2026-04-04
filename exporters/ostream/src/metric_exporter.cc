// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/ostream/common_utils.h"
#include "opentelemetry/exporters/ostream/metric_exporter.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/metrics/data/circular_buffer.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

namespace
{
std::string timeToString(opentelemetry::common::SystemTimestamp time_stamp)
{
  std::time_t epoch_time = std::chrono::system_clock::to_time_t(time_stamp);

  struct tm *tm_ptr = nullptr;
#if defined(_MSC_VER)
  struct tm buf_tm;
  if (!gmtime_s(&buf_tm, &epoch_time))
  {
    tm_ptr = &buf_tm;
  }
#else
  tm_ptr = std::gmtime(&epoch_time);
#endif

  char buf[100];
  char *date_str = nullptr;
  if (tm_ptr == nullptr)
  {
    OTEL_INTERNAL_LOG_ERROR("[OStream Metric] gmtime failed for " << epoch_time);
  }
  else if (std::strftime(buf, sizeof(buf), "%c", tm_ptr) > 0)
  {
    date_str = buf;
  }
  else
  {
    OTEL_INTERNAL_LOG_ERROR("[OStream Metric] strftime failed for " << epoch_time);
  }

  return std::string{date_str};
}
}  // namespace

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace metrics
{

template <typename Container>
static inline void printVec(std::ostream &os, Container &vec)
{
  using T = typename std::decay<decltype(*vec.begin())>::type;
  os << '[';
  if (vec.size() > 1)
  {
    std::copy(vec.begin(), vec.end(), std::ostream_iterator<T>(os, ", "));
  }
  os << ']';
}

struct ValueTypePrinter
{
  std::ostream &sout;
  void operator()(int64_t v) const noexcept { sout << v; }
  void operator()(double v) const noexcept { sout << v; }
};

struct PointDataPrinter
{
  std::ostream &sout;

  bool operator()(const sdk::metrics::SumPointData &d) const noexcept
  {
    sout << "\n  type\t\t: SumPointData";
    sout << "\n  value\t\t: ";
    nostd::visit(ValueTypePrinter{sout}, d.value_);
    return true;
  }

  bool operator()(const sdk::metrics::HistogramPointData &d) const noexcept
  {
    sout << "\n  type     : HistogramPointData";
    sout << "\n  count     : " << d.count_;
    sout << "\n  sum     : ";
    nostd::visit(ValueTypePrinter{sout}, d.sum_);
    if (d.record_min_max_)
    {
      sout << "\n  min     : ";
      nostd::visit(ValueTypePrinter{sout}, d.min_);
      sout << "\n  max     : ";
      nostd::visit(ValueTypePrinter{sout}, d.max_);
    }
    sout << "\n  buckets     : ";
    printVec(sout, d.boundaries_);
    sout << "\n  counts     : ";
    printVec(sout, d.counts_);
    return true;
  }

  bool operator()(const sdk::metrics::LastValuePointData &d) const noexcept
  {
    sout << "\n  type     : LastValuePointData";
    sout << "\n  timestamp     : " << std::to_string(d.sample_ts_.time_since_epoch().count())
         << std::boolalpha << "\n  valid     : " << d.is_lastvalue_valid_;
    sout << "\n  value     : ";
    nostd::visit(ValueTypePrinter{sout}, d.value_);
    return true;
  }

  bool operator()(const sdk::metrics::Base2ExponentialHistogramPointData &d) const noexcept
  {
    if (!d.positive_buckets_ && !d.negative_buckets_)
    {
      return false;
    }
    sout << "\n  type: Base2ExponentialHistogramPointData";
    sout << "\n  count: " << d.count_;
    sout << "\n  sum: " << d.sum_;
    sout << "\n  zero_count: " << d.zero_count_;
    if (d.record_min_max_)
    {
      sout << "\n  min: " << d.min_;
      sout << "\n  max: " << d.max_;
    }
    sout << "\n  scale: " << d.scale_;
    sout << "\n  positive buckets:";
    if (!d.positive_buckets_->Empty())
    {
      for (auto i = d.positive_buckets_->StartIndex(); i <= d.positive_buckets_->EndIndex(); ++i)
      {
        sout << "\n\t" << i << ": " << d.positive_buckets_->Get(i);
      }
    }
    sout << "\n  negative buckets:";
    if (!d.negative_buckets_->Empty())
    {
      for (auto i = d.negative_buckets_->StartIndex(); i <= d.negative_buckets_->EndIndex(); ++i)
      {
        sout << "\n\t" << i << ": " << d.negative_buckets_->Get(i);
      }
    }
    return true;
  }

  bool operator()(const sdk::metrics::DropPointData &) const noexcept { return false; }
};

OStreamMetricExporter::OStreamMetricExporter(
    std::ostream &sout,
    sdk::metrics::AggregationTemporality aggregation_temporality) noexcept
    : sout_(sout), aggregation_temporality_(aggregation_temporality)
{}

sdk::metrics::AggregationTemporality OStreamMetricExporter::GetAggregationTemporality(
    sdk::metrics::InstrumentType /* instrument_type */) const noexcept
{
  return aggregation_temporality_;
}

sdk::common::ExportResult OStreamMetricExporter::Export(
    const sdk::metrics::ResourceMetrics &data) noexcept
{
  if (isShutdown())
  {
    OTEL_INTERNAL_LOG_ERROR("[OStream Metric] Exporting "
                            << data.scope_metric_data_.size()
                            << " records(s) failed, exporter is shutdown");
    return sdk::common::ExportResult::kFailure;
  }

  for (auto &record : data.scope_metric_data_)
  {
    printInstrumentationInfoMetricData(record, data);
  }
  return sdk::common::ExportResult::kSuccess;
}

void OStreamMetricExporter::printAttributes(
    const std::map<std::string, sdk::common::OwnedAttributeValue> &map,
    const std::string &prefix) noexcept
{
  for (const auto &kv : map)
  {
    sout_ << prefix << kv.first << ": ";
    opentelemetry::exporter::ostream_common::print_value(kv.second, sout_);
  }
}

void OStreamMetricExporter::printResources(
    const opentelemetry::sdk::resource::Resource &resources) noexcept
{
  auto attributes = resources.GetAttributes();
  if (attributes.size())
  {
    // Convert unordered_map to map for printing so that iteration
    // order is guaranteed.
    std::map<std::string, sdk::common::OwnedAttributeValue> attr_map;
    for (auto &kv : attributes)
      attr_map[kv.first] = std::move(kv.second);
    printAttributes(attr_map, "\n\t");
  }
}

void OStreamMetricExporter::printInstrumentationInfoMetricData(
    const sdk::metrics::ScopeMetrics &info_metric,
    const sdk::metrics::ResourceMetrics &data) noexcept
{
  // sout_ is shared
  const std::lock_guard<std::mutex> serialize(serialize_lock_);
  sout_ << "{";
  sout_ << "\n  scope name\t: " << info_metric.scope_->GetName()
        << "\n  schema url\t: " << info_metric.scope_->GetSchemaURL()
        << "\n  version\t: " << info_metric.scope_->GetVersion();
  for (const auto &record : info_metric.metric_data_)
  {
    sout_ << "\n  start time\t: " << timeToString(record.start_ts)
          << "\n  end time\t: " << timeToString(record.end_ts)
          << "\n  instrument name\t: " << record.instrument_descriptor.name_
          << "\n  description\t: " << record.instrument_descriptor.description_
          << "\n  unit\t\t: " << record.instrument_descriptor.unit_;

    for (const auto &pd : record.point_data_attr_)
    {
      printPointDataAttributes(pd);
    }

    sout_ << "\n  resources\t:";
    printResources(*data.resource_);
  }
  sout_ << "\n}\n";
}

void OStreamMetricExporter::printPointDataAttributes(
    const opentelemetry::sdk::metrics::PointDataAttributes &point_data_attributes) noexcept
{
  if (!nostd::visit(PointDataPrinter{sout_}, point_data_attributes.point_data))
  {
    return;
  }

  sout_ << "\n  attributes\t\t: ";
  for (const auto &kv : point_data_attributes.attributes)
  {
    sout_ << "\n\t" << kv.first << ": ";
    opentelemetry::exporter::ostream_common::print_value(kv.second, sout_);
  }
}

bool OStreamMetricExporter::ForceFlush(std::chrono::microseconds /* timeout */) noexcept
{
  const std::lock_guard<std::mutex> serialize(serialize_lock_);
  sout_.flush();
  return true;
}

bool OStreamMetricExporter::Shutdown(std::chrono::microseconds /* timeout */) noexcept
{
  is_shutdown_ = true;
  return true;
}

bool OStreamMetricExporter::isShutdown() const noexcept
{
  return is_shutdown_;
}

}  // namespace metrics
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
