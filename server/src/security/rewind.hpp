#pragma once
// S11 §5（§3 交付物 4）：回退时长与回退姿态取样。
// 取样复用 S05 的 samplePoseAgo（20 tick 姿态环）；越限与环内缺失都「按当下姿态判定」，
// clamped 只作诊断计数（ac_rewind_clamped_total），不改变处置结果。
#include <cstdint>

#include "metrics/counters.hpp"
#include "sim/pose_history.hpp"

namespace ac::security {

inline constexpr uint32_t kRewindLimitMs = static_cast<uint32_t>(ac::kRewindLimitMs);  // 200

static_assert(kRewindLimitMs == 200u, "§5：回退上限 = 200 ms");

// §5：rewind_ms(rttMs) = min(rttMs / 2, 200)（整数除法得到取样用的「多少 ms 之前」，夹到上限）。
uint32_t rewindMs(uint32_t rttMs) noexcept;

// §5：越限判定**不取整** —— 精确比较 rttMs / 2 > 200，即 rttMs > 400（401 也算越限）。
constexpr bool isRewindOverLimit(uint32_t rttMs) noexcept {
  return rttMs > 2u * kRewindLimitMs;
}

struct RewindOutcome {
  bool isOk = false;      // false = 不得回滚：调用方按当下姿态判定
  bool isClamped = false; // 诊断：越限或环内缺失（只计数，不改变处置）
  bool isFound = false;   // 姿态环里找到了该实体
  ac::sim::SampledPose pose{};
};

// 越限（rttMs > 400）→ isOk = false + isClamped + 计数，不取样、不回滚。
RewindOutcome sampleRewindPose(const ac::sim::PoseHistory& history, uint32_t rttMs, uint16_t entityId,
                               ac::metrics::CounterRegistry* counters = nullptr) noexcept;

}  // namespace ac::security
