#include "replication/snapshot_rate.hpp"

namespace ac::replication {

uint16_t snapshotRateX10(const SnapshotRateState& state) noexcept {
  return kSnapshotRateLevelsX10[static_cast<std::size_t>(state.level)];
}

bool updateSnapshotRate(SnapshotRateState& state, uint32_t nowMs, const RateTriggers& triggers,
                        ac::metrics::CounterRegistry* counters,
                        ac::metrics::GaugeRegistry* gauges) noexcept {
  // 干净期：任一触发条件出现就重置；升档要求「连续 3000 ms 无触发」。
  if (triggers.hasAny()) {
    state.cleanSinceMs = nowMs;
  }

  const bool isFirstEval = !state.isEvaluated;
  if (!isFirstEval && nowMs - state.lastEvalMs < kSnapshotRateEvalPeriodMs) {
    ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSnapshotRateX10,
                            static_cast<double>(snapshotRateX10(state)));
    return false;
  }
  state.isEvaluated = true;
  state.lastEvalMs = nowMs;  // 首个评估点就是 nowMs（档位从此刻起生效）

  bool isChanged = false;
  const std::size_t level = static_cast<std::size_t>(state.level);
  if (triggers.hasAny()) {
    if (level + 1u < kSnapshotRateLevelCount) {
      state.level = static_cast<RateLevel>(level + 1u);
      ++state.downshifts;
      ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kSnapshotRateDownshifts);
      isChanged = true;
    }
    state.cleanSinceMs = nowMs;
  } else if (nowMs - state.cleanSinceMs >= kSnapshotRateCleanPeriodMs && level > 0u) {
    state.level = static_cast<RateLevel>(level - 1u);
    ++state.upshifts;
    isChanged = true;
  }

  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSnapshotRateX10,
                          static_cast<double>(snapshotRateX10(state)));
  return isChanged;
}

}  // namespace ac::replication
