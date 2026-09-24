#include "replication/snapshot_rate.hpp"

namespace ac::replication {

RateLevel snapshotRateLevel(const SnapshotRateState& state) noexcept { return state.level; }

uint16_t snapshotRateX10(const SnapshotRateState& state) noexcept {
  const std::size_t index = static_cast<std::size_t>(state.level);
  return index < kSnapshotRateLevelCount ? kSnapshotRateLevelsX10[index] : kSnapshotRateHighX10;
}

bool updateSnapshotRate(SnapshotRateState& state, uint32_t nowMs, const RateTriggers& triggers,
                        ac::metrics::CounterRegistry* counters) noexcept {
  if (state.isEvaluated && nowMs - state.lastEvalMs < kSnapshotRateEvalPeriodMs) return false;
  const bool isFirstEval = !state.isEvaluated;
  state.isEvaluated = true;
  state.lastEvalMs = nowMs;

  if (triggers.hasAny()) {
    state.cleanSinceMs = nowMs;  // 有触发就重开干净期
    if (state.level == RateLevel::kLow) return false;
    state.level = static_cast<RateLevel>(static_cast<uint8_t>(state.level) + 1u);
    ++state.downshifts;
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kSnapshotRateDownshifts);
    return true;
  }

  if (isFirstEval) {
    state.cleanSinceMs = nowMs;  // 首个评估周期只建立干净期起点，不升档
    return false;
  }
  if (state.level == RateLevel::kHigh) return false;
  if (nowMs - state.cleanSinceMs < kSnapshotRateCleanPeriodMs) return false;
  state.level = static_cast<RateLevel>(static_cast<uint8_t>(state.level) - 1u);
  state.cleanSinceMs = nowMs;  // 升档后重新计时，避免一次干净期连升两档
  ++state.upshifts;
  return true;
}

}  // namespace ac::replication
