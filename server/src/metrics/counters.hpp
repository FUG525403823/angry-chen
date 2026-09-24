#pragma once
// S11 §4-7/§6-4：校验与姿态处置的定长计数注册表。
// 本份只提供「名字表 + 定长取值区」：零分配、无字符串拼接；S13 的 metrics.cpp 在其上做 Prometheus 渲染。
#include <cstddef>
#include <cstdint>

namespace ac::metrics {

enum class CounterId : uint8_t {
  kMalformedFrames = 0,  // ac_malformed_frames_total
  kOversizedFrames,      // ac_oversized_frames_total
  kRateLimitedFrames,    // ac_rate_limited_frames_total
  kDroppedFrames,        // ac_dropped_frames_total
  kPoseSuspect,          // ac_pose_suspect_total
  kPoseRejected,         // ac_pose_rejected_total
  kHardCorrect,          // ac_hard_correct_total
  kRewindClamped,        // ac_rewind_clamped_total
  kSpeedViolations,      // ac_speed_violations_total
  // S12 §4-8 追加（名字见 S13 §5 的「调度」「复制与背压」两行）
  kSnapshotRateDownshifts,  // ac_snapshot_rate_downshifts_total
  kSlowClientDrops,         // ac_slow_client_drops_total
  kRoomBudgetExceeded,      // ac_room_budget_exceeded_total
  kTickSkips,               // ac_tick_skips_total（S12 §8 风险判据；S13 §5 名单暂缺，见 README §13.1）
  // S13 §5 追加（「复制与背压」「会话与房间」「流量与存储」三行；名字即契约）
  kSnapshotsSent,      // ac_snapshots_sent_total
  kSnapshotBytesTotal, // ac_snapshot_bytes_total
  kGraceStarts,        // ac_grace_starts_total
  kGraceReconnects,    // ac_grace_reconnects_total
  kGraceTimeouts,      // ac_grace_timeouts_total
  kBytesOut,           // ac_bytes_out_total
  kBytesIn,            // ac_bytes_in_total
  kFramesOut,          // ac_frames_out_total
  kFramesIn,           // ac_frames_in_total
  kEventsSent,         // ac_events_sent_total
  kEventsDropped,      // ac_events_dropped_total
  kCorruptLines,       // ac_corrupt_lines_total
  kHttpRateLimited,    // ac_http_rate_limited_total
  kHttpCacheHits,      // ac_http_cache_hits_total
  kCount,
};

inline constexpr std::size_t kCounterCount = static_cast<std::size_t>(CounterId::kCount);

// 名字即契约（S13 §5 的「校验」一行）：改名必须同提交改 S13 的清单与断言。
struct CounterRegistry {
  uint64_t values[kCounterCount] = {};
};

void resetCounters(CounterRegistry& registry) noexcept;
void addCounter(CounterRegistry& registry, CounterId id, uint64_t delta = 1u) noexcept;
uint64_t counterValue(const CounterRegistry& registry, CounterId id) noexcept;
const char* counterName(CounterId id) noexcept;
bool isCounterRegistered(const char* name) noexcept;
std::size_t counterCount() noexcept;

// 计数接线样板（registry 可为 nullptr = 只判定不计数）；4 个校验模块共用，避免各写一遍判空。
inline void bumpCounter(CounterRegistry* registry, CounterId id, uint64_t delta = 1u) noexcept {
  if (registry != nullptr) addCounter(*registry, id, delta);
}

}  // namespace ac::metrics
