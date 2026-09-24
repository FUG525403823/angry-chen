#include "core/scheduler.hpp"

namespace ac::core {
namespace {

void insertionSort(double* values, std::size_t count) noexcept {
  for (std::size_t i = 1u; i < count; ++i) {
    const double key = values[i];
    std::size_t j = i;
    while (j > 0u && values[j - 1u] > key) {
      values[j] = values[j - 1u];
      --j;
    }
    values[j] = key;
  }
}

// 最近秩（nearest-rank）取分位：样本量 ≤ 64，误差在 §5 预算的量级之下。
std::size_t percentileIndex(std::size_t count, double fraction) noexcept {
  const double position = static_cast<double>(count - 1u) * fraction;
  std::size_t index = static_cast<std::size_t>(position + 0.5);
  if (index >= count) index = count - 1u;
  return index;
}

}  // namespace

void startScheduler(TickScheduler& scheduler, uint64_t nowMs, uint32_t simStartMs) noexcept {
  scheduler.simClock = SimClock{simStartMs, nowMs};
  scheduler.tickIndex = 0u;
  scheduler.tickSkips = 0u;
  scheduler.budgetExceeded = 0u;
  scheduler.lastWorkMs = 0u;
  scheduler.jitterCount = 0u;
  scheduler.jitterNext = 0u;
  for (std::size_t i = 0u; i < kScheduleSampleCount; ++i) scheduler.jitterMs[i] = 0.0;
}

uint32_t pendingTicks(const TickScheduler& scheduler, uint64_t nowMs) noexcept {
  const uint64_t elapsedMs =
      nowMs > scheduler.simClock.wallStartMs ? nowMs - scheduler.simClock.wallStartMs : 0u;
  // tick 0 在 startScheduler 的时刻就到点：应执行量 = 已流逝的整 tick 数 + 1。
  const uint64_t dueTicks = elapsedMs / static_cast<uint64_t>(kTickMs) + 1u;
  return dueTicks > scheduler.tickIndex ? static_cast<uint32_t>(dueTicks - scheduler.tickIndex) : 0u;
}

double scheduleErrorMs(const TickScheduler& scheduler, uint64_t nowMs) noexcept {
  const double expectedMs = static_cast<double>(accountedTicks(scheduler)) * static_cast<double>(kTickMs);
  const double actualMs = static_cast<double>(
      nowMs > scheduler.simClock.wallStartMs ? nowMs - scheduler.simClock.wallStartMs : 0u);
  return actualMs - expectedMs;
}

bool noteTickRun(TickScheduler& scheduler, uint64_t nowMs, uint32_t elapsedMs,
                 ac::metrics::CounterRegistry* counters, ac::metrics::GaugeRegistry* gauges) noexcept {
  scheduler.lastWorkMs = elapsedMs;
  // 误差按「本 tick 的应到时刻」取样：第 i 个 tick 应在 wallStart + i × 50 ms 执行。
  scheduler.jitterMs[scheduler.jitterNext] = scheduleErrorMs(scheduler, nowMs);
  ++scheduler.tickIndex;
  scheduler.jitterNext = (scheduler.jitterNext + 1u) % kScheduleSampleCount;
  if (scheduler.jitterCount < kScheduleSampleCount) ++scheduler.jitterCount;

  const bool isOverBudget = elapsedMs > kRoomTickBudgetMs;
  if (isOverBudget) {
    ++scheduler.budgetExceeded;  // 让出事件循环：累积量不扣除（让出 ≠ 丢 tick）
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kRoomBudgetExceeded);
  }
  publishScheduleGauges(scheduler, gauges);
  return isOverBudget;
}

void noteTickSkip(TickScheduler& scheduler, uint32_t skipped,
                  ac::metrics::CounterRegistry* counters) noexcept {
  scheduler.tickSkips += skipped;
  ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kTickSkips, skipped);
}

int64_t noteSimClock(TickScheduler& scheduler, uint32_t worldTimeMs, uint64_t nowMs,
                     ac::metrics::GaugeRegistry* gauges) noexcept {
  const int64_t driftMs = simDriftMs(scheduler.simClock, worldTimeMs, nowMs);
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSimDriftMs, static_cast<double>(driftMs));
  return driftMs;
}

int64_t simDriftMs(const SimClock& clock, uint32_t worldTimeMs, uint64_t nowMs) noexcept {
  const int64_t simElapsedMs =
      static_cast<int64_t>(worldTimeMs) - static_cast<int64_t>(clock.simStartMs);
  const int64_t wallElapsedMs =
      nowMs > clock.wallStartMs ? static_cast<int64_t>(nowMs - clock.wallStartMs) : 0;
  return simElapsedMs - wallElapsedMs;
}

double tickScheduleErrorP95Ms(const TickScheduler& scheduler) noexcept {
  if (scheduler.jitterCount == 0u) return 0.0;
  double magnitudes[kScheduleSampleCount] = {};
  for (std::size_t i = 0u; i < scheduler.jitterCount; ++i) {
    const double value = scheduler.jitterMs[i];
    magnitudes[i] = value < 0.0 ? -value : value;  // §5 约束幅度；符号由 scheduleErrorMs 暴露
  }
  insertionSort(magnitudes, scheduler.jitterCount);
  return magnitudes[percentileIndex(scheduler.jitterCount, 0.95)];
}

double scheduleHeadTailGapMs(const TickScheduler& scheduler) noexcept {
  if (scheduler.jitterCount == 0u) return 0.0;
  double sorted[kScheduleSampleCount] = {};
  for (std::size_t i = 0u; i < scheduler.jitterCount; ++i) sorted[i] = scheduler.jitterMs[i];
  insertionSort(sorted, scheduler.jitterCount);
  return sorted[scheduler.jitterCount - 1u] - sorted[0];
}

bool meetsScheduleBudget(const TickScheduler& scheduler) noexcept {
  return tickScheduleErrorP95Ms(scheduler) <= kTickScheduleErrorP95BudgetMs &&
         scheduleHeadTailGapMs(scheduler) <= kScheduleHeadTailGapBudgetMs;
}

void publishScheduleGauges(const TickScheduler& scheduler,
                           ac::metrics::GaugeRegistry* gauges) noexcept {
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kTickScheduleErrorMsP95,
                          tickScheduleErrorP95Ms(scheduler));
}

}  // namespace ac::core
