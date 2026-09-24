#pragma once
// S12 §5（§3 交付物 5）：tick 绝对时刻自校正、单房间 8 ms 工作量预算、抖动与漂移采样。
//
// 冻结口径：
//   tickScheduleError = 单调时钟 − (首 tick 时刻 + tickIndex × 50)，P95 ≤ 8 ms；
//   simDrift = (world.timeMs − simStartMs) − (now − wallStartMs)，|drift| ≤ 50 ms；
//   单房间工作量预算 8 ms：超出则让出事件循环，累积量**不扣除**（让出 ≠ 丢 tick）。
#include <cstddef>
#include <cstdint>

#include "core/math.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"

namespace ac::core {

inline constexpr uint32_t kRoomTickBudgetMs = 8u;
inline constexpr double kTickScheduleErrorP95BudgetMs = 8.0;
inline constexpr double kScheduleHeadTailGapBudgetMs = 2.0;  // §5 替代判据（定时器粒度 > 8 ms 时）
inline constexpr int64_t kSimDriftBudgetMs = 50;
inline constexpr std::size_t kScheduleSampleCount = 64u;

struct TickScheduler {
  uint64_t startWallMs = 0u;
  uint32_t tickIndex = 0u;         // 已执行的 tick 数
  uint32_t accumulatedTicks = 0u;  // 累积应执行量：让出不扣除（§5）
  uint32_t tickSkips = 0u;         // 真正的丢 tick（追帧上限等），与让出分开计
  uint32_t budgetExceeded = 0u;    // 让出次数
  uint32_t lastWorkMs = 0u;
  double jitterMs[kScheduleSampleCount] = {};
  std::size_t jitterCount = 0u;
  std::size_t jitterNext = 0u;
};

void startScheduler(TickScheduler& scheduler, uint64_t nowMs) noexcept;
uint32_t pendingTicks(const TickScheduler& scheduler, uint64_t nowMs) noexcept;
double scheduleErrorMs(const TickScheduler& scheduler, uint64_t nowMs) noexcept;
bool noteTickRun(TickScheduler& scheduler, uint64_t nowMs, uint32_t elapsedMs,
                 ac::metrics::CounterRegistry* counters = nullptr,
                 ac::metrics::GaugeRegistry* gauges = nullptr) noexcept;
void noteTickSkip(TickScheduler& scheduler, uint32_t skipped = 1u,
                  ac::metrics::CounterRegistry* counters = nullptr) noexcept;
double tickScheduleErrorP95Ms(const TickScheduler& scheduler) noexcept;
double scheduleHeadTailGapMs(const TickScheduler& scheduler) noexcept;
bool meetsScheduleBudget(const TickScheduler& scheduler) noexcept;
int64_t simDriftMs(uint32_t worldTimeMs, uint32_t simStartMs, uint64_t nowMs,
                   uint64_t wallStartMs) noexcept;
void publishScheduleGauges(const TickScheduler& scheduler,
                           ac::metrics::GaugeRegistry* gauges) noexcept;

}  // namespace ac::core
