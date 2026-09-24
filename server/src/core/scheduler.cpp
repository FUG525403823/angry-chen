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

// 取样环的公共口径：取 count 个样本（可选取幅值）、插入排序、最近秩取分位。
struct RingSamples {
  double sorted[kScheduleSampleCount] = {};
  std::size_t count = 0u;
};

RingSamples sortRing(const double* samples, std::size_t count, bool useMagnitude) noexcept {
  RingSamples ring;
  ring.count = count;
  for (std::size_t i = 0u; i < count; ++i) {
    const double value = samples[i];
    ring.sorted[i] = useMagnitude && value < 0.0 ? -value : value;
  }
  insertionSort(ring.sorted, count);
  return ring;
}

double pickPercentile(const RingSamples& ring, double fraction) noexcept {
  if (ring.count == 0u) return 0.0;
  return ring.sorted[percentileIndex(ring.count, fraction)];
}

double percentileOf(const double* samples, std::size_t count, double fraction,
                    bool useMagnitude) noexcept {
  return pickPercentile(sortRing(samples, count, useMagnitude), fraction);
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
  scheduler.lastRunWallMs = 0u;
  scheduler.intervalCount = 0u;
  scheduler.intervalNext = 0u;
  scheduler.workCount = 0u;
  scheduler.workNext = 0u;
  for (std::size_t i = 0u; i < kScheduleSampleCount; ++i) {
    scheduler.jitterMs[i] = 0.0;
    scheduler.intervalErrorMs[i] = 0.0;
    scheduler.workMs[i] = 0.0;
  }
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
  // S13 §5：间隔误差（相邻两次 tick 的墙上间隔 − 50 ms）与工作量各留一份样本。
  if (scheduler.tickIndex > 0u && nowMs > scheduler.lastRunWallMs) {
    scheduler.intervalErrorMs[scheduler.intervalNext] =
        static_cast<double>(nowMs - scheduler.lastRunWallMs) - static_cast<double>(kTickMs);
    scheduler.intervalNext = (scheduler.intervalNext + 1u) % kScheduleSampleCount;
    if (scheduler.intervalCount < kScheduleSampleCount) ++scheduler.intervalCount;
  }
  scheduler.lastRunWallMs = nowMs;
  scheduler.workMs[scheduler.workNext] = static_cast<double>(elapsedMs);
  scheduler.workNext = (scheduler.workNext + 1u) % kScheduleSampleCount;
  if (scheduler.workCount < kScheduleSampleCount) ++scheduler.workCount;
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
  // §5 约束幅度；符号由 scheduleErrorMs 暴露。
  return percentileOf(scheduler.jitterMs, scheduler.jitterCount, 0.95, true);
}

double tickIntervalErrorP95Ms(const TickScheduler& scheduler) noexcept {
  return percentileOf(scheduler.intervalErrorMs, scheduler.intervalCount, 0.95, true);
}

double tickJitterP50Ms(const TickScheduler& scheduler) noexcept {
  return percentileOf(scheduler.jitterMs, scheduler.jitterCount, 0.50, true);
}

// 与 tickScheduleErrorP95Ms 同源：两者取的都是误差环的 P95（§14.1 登记），保留两个名字是因为
// 报告与 /metrics 各冻结了一个字段名。
double tickJitterP95Ms(const TickScheduler& scheduler) noexcept {
  return percentileOf(scheduler.jitterMs, scheduler.jitterCount, 0.95, true);
}

double tickWorkP95Ms(const TickScheduler& scheduler) noexcept {
  return percentileOf(scheduler.workMs, scheduler.workCount, 0.95, false);
}

double tickWorkP99Ms(const TickScheduler& scheduler) noexcept {
  return percentileOf(scheduler.workMs, scheduler.workCount, 0.99, false);
}

double scheduleHeadTailGapMs(const TickScheduler& scheduler) noexcept {
  const RingSamples ring = sortRing(scheduler.jitterMs, scheduler.jitterCount, true);
  if (ring.count == 0u) return 0.0;
  return ring.sorted[ring.count - 1u] - ring.sorted[0];
}

bool meetsScheduleBudget(const TickScheduler& scheduler) noexcept {
  return tickScheduleErrorP95Ms(scheduler) <= kTickScheduleErrorP95BudgetMs &&
         scheduleHeadTailGapMs(scheduler) <= kScheduleHeadTailGapBudgetMs;
}

void publishScheduleGauges(const TickScheduler& scheduler,
                           ac::metrics::GaugeRegistry* gauges) noexcept {
  // 每 tick 的发布路径：三个采样环各排一次（此前 6 次访问器调用会把 jitter 环排 3 遍、work 环排 2 遍）。
  const RingSamples jitter = sortRing(scheduler.jitterMs, scheduler.jitterCount, true);
  const RingSamples interval = sortRing(scheduler.intervalErrorMs, scheduler.intervalCount, true);
  const RingSamples work = sortRing(scheduler.workMs, scheduler.workCount, false);
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kTickScheduleErrorMsP95,
                          pickPercentile(jitter, 0.95));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kTickIntervalErrorMsP95,
                          pickPercentile(interval, 0.95));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kTickJitterMsP50, pickPercentile(jitter, 0.50));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kTickJitterMsP95, pickPercentile(jitter, 0.95));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kTickWorkMsP95, pickPercentile(work, 0.95));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kTickWorkMsP99, pickPercentile(work, 0.99));
}

}  // namespace ac::core
