#include "core/scheduler.hpp"

namespace ac::core {
namespace {

void pushJitter(TickScheduler& scheduler, double value) noexcept {
  scheduler.jitterMs[scheduler.jitterNext] = value;
  scheduler.jitterNext = (scheduler.jitterNext + 1u) % kScheduleSampleCount;
  if (scheduler.jitterCount < kScheduleSampleCount) ++scheduler.jitterCount;
}

// 升序插入排序（≤64 条）；不分配、不改动采样环本身。
void sortCopy(const TickScheduler& scheduler, double* out) noexcept {
  for (std::size_t i = 0u; i < scheduler.jitterCount; ++i) out[i] = scheduler.jitterMs[i];
  for (std::size_t i = 1u; i < scheduler.jitterCount; ++i) {
    const double value = out[i];
    std::size_t j = i;
    while (j > 0u && out[j - 1u] > value) {
      out[j] = out[j - 1u];
      --j;
    }
    out[j] = value;
  }
}

double percentileOf(const double* sorted, std::size_t count, uint32_t percent) noexcept {
  if (count == 0u) return 0.0;
  const std::size_t rank = (count * percent + 99u) / 100u;  // ceil(p% × n)，取值 1..n
  const std::size_t index = rank == 0u ? 0u : rank - 1u;
  return sorted[index < count ? index : count - 1u];
}

}  // namespace

void startScheduler(TickScheduler& scheduler, uint64_t nowMs) noexcept {
  scheduler = TickScheduler{};
  scheduler.startWallMs = nowMs;
}

uint32_t pendingTicks(const TickScheduler& scheduler, uint64_t nowMs) noexcept {
  if (nowMs < scheduler.startWallMs) return 0u;
  const uint64_t elapsedMs = nowMs - scheduler.startWallMs;
  const uint64_t dueTicks = elapsedMs / ac::kTickMs + 1u;
  if (dueTicks <= scheduler.tickIndex) return 0u;
  return static_cast<uint32_t>(dueTicks - scheduler.tickIndex);
}

double scheduleErrorMs(const TickScheduler& scheduler, uint64_t nowMs) noexcept {
  const int64_t scheduled =
      static_cast<int64_t>(scheduler.startWallMs) +
      static_cast<int64_t>(scheduler.tickIndex) * static_cast<int64_t>(ac::kTickMs);
  return static_cast<double>(static_cast<int64_t>(nowMs) - scheduled);
}

bool noteTickRun(TickScheduler& scheduler, uint64_t nowMs, uint32_t elapsedMs,
                 ac::metrics::CounterRegistry* counters,
                 ac::metrics::GaugeRegistry* gauges) noexcept {
  const double error = scheduleErrorMs(scheduler, nowMs);
  pushJitter(scheduler, error < 0.0 ? -error : error);
  ++scheduler.tickIndex;
  ++scheduler.accumulatedTicks;
  scheduler.lastWorkMs = elapsedMs;
  const bool isOverBudget = elapsedMs > kRoomTickBudgetMs;
  if (isOverBudget) {
    ++scheduler.budgetExceeded;  // 让出事件循环：tickIndex/累积量照常前进（让出 ≠ 丢 tick）
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kRoomBudgetExceeded);
  }
  publishScheduleGauges(scheduler, gauges);
  return isOverBudget;
}

void noteTickSkip(TickScheduler& scheduler, uint32_t skipped,
                  ac::metrics::CounterRegistry* counters) noexcept {
  scheduler.tickSkips += skipped;
  scheduler.accumulatedTicks += skipped;
  ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kTickSkips, skipped);
}

double tickScheduleErrorP95Ms(const TickScheduler& scheduler) noexcept {
  double sorted[kScheduleSampleCount] = {};
  sortCopy(scheduler, sorted);
  return percentileOf(sorted, scheduler.jitterCount, 95u);
}

double scheduleHeadTailGapMs(const TickScheduler& scheduler) noexcept {
  // §5 替代判据：前 1/3 与后 1/3 的 P95 差（定时器粒度大于 8 ms 时的等价判据）。
  const std::size_t count = scheduler.jitterCount;
  if (count < 6u) return 0.0;
  const std::size_t third = count / 3u;
  double head[kScheduleSampleCount] = {};
  double tail[kScheduleSampleCount] = {};
  for (std::size_t i = 0u; i < third; ++i) head[i] = scheduler.jitterMs[i];
  for (std::size_t i = 0u; i < third; ++i) tail[i] = scheduler.jitterMs[count - third + i];
  for (std::size_t i = 1u; i < third; ++i) {
    const double value = head[i];
    std::size_t j = i;
    while (j > 0u && head[j - 1u] > value) {
      head[j] = head[j - 1u];
      --j;
    }
    head[j] = value;
  }
  for (std::size_t i = 1u; i < third; ++i) {
    const double value = tail[i];
    std::size_t j = i;
    while (j > 0u && tail[j - 1u] > value) {
      tail[j] = tail[j - 1u];
      --j;
    }
    tail[j] = value;
  }
  const double headP95 = percentileOf(head, third, 95u);
  const double tailP95 = percentileOf(tail, third, 95u);
  return headP95 > tailP95 ? headP95 - tailP95 : tailP95 - headP95;
}

bool meetsScheduleBudget(const TickScheduler& scheduler) noexcept {
  return tickScheduleErrorP95Ms(scheduler) <= kTickScheduleErrorP95BudgetMs;
}

int64_t simDriftMs(uint32_t worldTimeMs, uint32_t simStartMs, uint64_t nowMs,
                   uint64_t wallStartMs) noexcept {
  const int64_t simElapsed = static_cast<int64_t>(worldTimeMs) - static_cast<int64_t>(simStartMs);
  const int64_t wallElapsed = static_cast<int64_t>(nowMs) - static_cast<int64_t>(wallStartMs);
  return simElapsed - wallElapsed;
}

void publishScheduleGauges(const TickScheduler& scheduler,
                           ac::metrics::GaugeRegistry* gauges) noexcept {
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kTickScheduleErrorMsP95,
                          tickScheduleErrorP95Ms(scheduler));
}

}  // namespace ac::core
