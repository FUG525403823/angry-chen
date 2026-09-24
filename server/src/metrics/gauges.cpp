#include "metrics/gauges.hpp"

#include <cstring>

namespace ac::metrics {
namespace {

// 名字即契约（S13 §5 的「调度」「复制与背压」两行）：改名必须同提交改 S13 的清单与断言。
constexpr const char* kGaugeNames[kGaugeCount] = {
    "ac_snapshot_rate_x10",
    "ac_snapshot_bytes_avg",
    "ac_snapshot_bytes_max",
    "ac_send_queue_bytes",
    "ac_tick_schedule_error_ms_p95",
    "ac_sim_drift_ms",
    "ac_snapshot_records_avg",
    "ac_tick_interval_error_ms_p95",
    "ac_tick_jitter_ms_p50",
    "ac_tick_jitter_ms_p95",
    "ac_tick_work_ms_p95",
    "ac_tick_work_ms_p99",
};

static_assert(sizeof(kGaugeNames) / sizeof(kGaugeNames[0]) == kGaugeCount,
              "名字表条数必须与 GaugeId 一致");

}  // namespace

void resetGauges(GaugeRegistry& registry) noexcept { registry = GaugeRegistry{}; }

void setGauge(GaugeRegistry& registry, GaugeId id, double value) noexcept {
  registry.values[static_cast<std::size_t>(id)] = value;
}

double gaugeValue(const GaugeRegistry& registry, GaugeId id) noexcept {
  return registry.values[static_cast<std::size_t>(id)];
}

const char* gaugeName(GaugeId id) noexcept { return kGaugeNames[static_cast<std::size_t>(id)]; }

bool isGaugeRegistered(const char* name) noexcept {
  if (name == nullptr) return false;
  for (const char* candidate : kGaugeNames) {
    if (std::strcmp(candidate, name) == 0) return true;
  }
  return false;
}

std::size_t gaugeCount() noexcept { return kGaugeCount; }

}  // namespace ac::metrics
