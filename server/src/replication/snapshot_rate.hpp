#pragma once
// S12 §5（§3 交付物 4）：自适应快照档位状态机（200 / 150 / 100 = 20 / 15 / 10 Hz）。
//
// 降档触发（任一）：最慢会话积压 > 32 KiB、tickSkips 增长、房间工作量超预算计数增长；
// 升档：连续 3000 ms 无触发条件；评估周期 ≥ 1000 ms；事件帧不受档位影响（仍每 tick 广播）。
#include <cstddef>
#include <cstdint>

#include "metrics/counters.hpp"

namespace ac::replication {

inline constexpr uint16_t kSnapshotRateHighX10 = 200u;
inline constexpr uint16_t kSnapshotRateMidX10 = 150u;
inline constexpr uint16_t kSnapshotRateLowX10 = 100u;
inline constexpr uint16_t kSnapshotRateMinX10 = 100u;  // ADR-009 允许区间下界
inline constexpr uint16_t kSnapshotRateMaxX10 = 200u;  // 20 Hz tick 下的实际上界（300 档本步不启用）
inline constexpr uint32_t kSnapshotRateEvalPeriodMs = 1000u;
inline constexpr uint32_t kSnapshotRateCleanPeriodMs = 3000u;
inline constexpr std::size_t kSnapshotRateLevelCount = 3u;
inline constexpr uint16_t kSnapshotRateLevelsX10[kSnapshotRateLevelCount] = {
    kSnapshotRateHighX10, kSnapshotRateMidX10, kSnapshotRateLowX10};

enum class RateLevel : uint8_t { kHigh = 0u, kMid = 1u, kLow = 2u };

// 相位（§5，纯函数、可白盒断言）：200 = 每 tick；150 = 每 3 tick 发 2 次（tick % 3 != 2）；
// 100 = 每 2 tick 发 1 次。相位唯一确定，不依赖调用历史。
constexpr bool shouldSendSnapshot(uint16_t rateX10, uint32_t tick) noexcept {
  switch (rateX10) {
    case kSnapshotRateHighX10: return true;
    case kSnapshotRateMidX10: return (tick % 3u) != 2u;
    case kSnapshotRateLowX10: return (tick % 2u) == 0u;
    default: return true;
  }
}

struct RateTriggers {
  bool isBacklogOverHalf = false;
  bool didTickSkipsGrow = false;
  bool didBudgetExceedGrow = false;

  bool hasAny() const noexcept { return isBacklogOverHalf || didTickSkipsGrow || didBudgetExceedGrow; }
};

struct SnapshotRateState {
  RateLevel level = RateLevel::kHigh;
  uint32_t lastEvalMs = 0u;
  uint32_t cleanSinceMs = 0u;
  uint32_t downshifts = 0u;
  uint32_t upshifts = 0u;
  bool isEvaluated = false;
};

RateLevel snapshotRateLevel(const SnapshotRateState& state) noexcept;
uint16_t snapshotRateX10(const SnapshotRateState& state) noexcept;

// 返回 true = 本周期档位发生变更（降档或升档）。
bool updateSnapshotRate(SnapshotRateState& state, uint32_t nowMs, const RateTriggers& triggers,
                        ac::metrics::CounterRegistry* counters = nullptr) noexcept;

}  // namespace ac::replication
