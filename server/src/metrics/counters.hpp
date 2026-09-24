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
