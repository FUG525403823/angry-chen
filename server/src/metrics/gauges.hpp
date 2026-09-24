#pragma once
// S12 §4-8：复制与调度侧的量值型指标（量值 = 会上下波动，不是只增计数）。
// 本份只提供「名字表 + 定长取值区」：零分配、无字符串拼接；S13 的 metrics.cpp 在其上做
// Prometheus 渲染与 /metrics 出口（S12 只负责把值写进来）。
#include <cstddef>
#include <cstdint>

namespace ac::metrics {

enum class GaugeId : uint8_t {
  kSnapshotRateX10 = 0,     // ac_snapshot_rate_x10
  kSnapshotBytesAvg,        // ac_snapshot_bytes_avg
  kSnapshotBytesMax,        // ac_snapshot_bytes_max
  kSendQueueBytes,          // ac_send_queue_bytes
  kTickScheduleErrorMsP95,  // ac_tick_schedule_error_ms_p95
  kSimDriftMs,              // ac_sim_drift_ms
  kCount,
};

inline constexpr std::size_t kGaugeCount = static_cast<std::size_t>(GaugeId::kCount);

struct GaugeRegistry {
  double values[kGaugeCount] = {};
};

void resetGauges(GaugeRegistry& registry) noexcept;
void setGauge(GaugeRegistry& registry, GaugeId id, double value) noexcept;
double gaugeValue(const GaugeRegistry& registry, GaugeId id) noexcept;
const char* gaugeName(GaugeId id) noexcept;
bool isGaugeRegistered(const char* name) noexcept;
std::size_t gaugeCount() noexcept;

// 接线样板（registry 可为 nullptr = 只计算不发布）
inline void setGaugeIf(GaugeRegistry* registry, GaugeId id, double value) noexcept {
  if (registry != nullptr) setGauge(*registry, id, value);
}

}  // namespace ac::metrics
