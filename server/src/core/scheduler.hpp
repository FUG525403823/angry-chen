#pragma once
// S12 §5（§3 交付物 5）：tick 绝对时刻自校正、单房间 8 ms 工作量预算、抖动与漂移采样。
//
// 冻结口径：
//   tickScheduleError = 单调时钟 − (首 tick 时刻 + 已计 tick 数 × 50)，P95 ≤ 8 ms；
//   simDrift = (world.timeMs − simStartMs) − (now − wallStartMs)，|drift| ≤ 50 ms；
//   单房间工作量预算 8 ms：超出则让出事件循环，已计 tick 数**不扣除**（让出 ≠ 丢 tick）。
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

// 模拟时钟的两个锚点：漂移就是「模拟走了多久」减「墙上时钟走了多久」。
struct SimClock {
  uint32_t simStartMs = 0u;
  uint64_t wallStartMs = 0u;
};

struct TickScheduler {
  SimClock simClock{};
  uint32_t tickIndex = 0u;       // 已执行的 tick 数
  uint32_t tickSkips = 0u;       // 真正的丢 tick（追帧上限等），与让出分开计
  uint32_t budgetExceeded = 0u;  // 让出次数
  uint32_t lastWorkMs = 0u;
  double jitterMs[kScheduleSampleCount] = {};
  std::size_t jitterCount = 0u;
  std::size_t jitterNext = 0u;
  // S13 §5「调度」行：tick 间隔误差与工作量也各自留一份样本环（同样的 64 格、同样的分位口径）。
  std::uint64_t lastRunWallMs = 0u;  // 上一次 tick 的墙上时刻（u64 时钟，避免 ~49.7 天后截断）；间隔误差只在第二次 tick 起有定义
  double intervalErrorMs[kScheduleSampleCount] = {};
  std::size_t intervalCount = 0u;
  std::size_t intervalNext = 0u;
  double workMs[kScheduleSampleCount] = {};
  std::size_t workCount = 0u;
  std::size_t workNext = 0u;
};

// 已计 tick 数 = 执行过的 + 真正丢掉的（让出不计入丢 tick）。派生值，不另存一份状态。
inline uint32_t accountedTicks(const TickScheduler& scheduler) noexcept {
  return scheduler.tickIndex + scheduler.tickSkips;
}

void startScheduler(TickScheduler& scheduler, uint64_t nowMs, uint32_t simStartMs = 0u) noexcept;
uint32_t pendingTicks(const TickScheduler& scheduler, uint64_t nowMs) noexcept;
double scheduleErrorMs(const TickScheduler& scheduler, uint64_t nowMs) noexcept;
bool noteTickRun(TickScheduler& scheduler, uint64_t nowMs, uint32_t elapsedMs,
                 ac::metrics::CounterRegistry* counters = nullptr,
                 ac::metrics::GaugeRegistry* gauges = nullptr) noexcept;
void noteTickSkip(TickScheduler& scheduler, uint32_t skipped = 1u,
                  ac::metrics::CounterRegistry* counters = nullptr) noexcept;
// 房间每 tick 报一次世界时间：算漂移并把 ac_sim_drift_ms 写进量值表（返回值同时给调用方判定）。
int64_t noteSimClock(TickScheduler& scheduler, uint32_t worldTimeMs, uint64_t nowMs,
                     ac::metrics::GaugeRegistry* gauges = nullptr) noexcept;
int64_t simDriftMs(const SimClock& clock, uint32_t worldTimeMs, uint64_t nowMs) noexcept;
double tickScheduleErrorP95Ms(const TickScheduler& scheduler) noexcept;
// S13 §5 的新分位：间隔误差与抖动取幅值（符号由 scheduleErrorMs 暴露），工作量取原值。
double tickIntervalErrorP95Ms(const TickScheduler& scheduler) noexcept;
double tickJitterP50Ms(const TickScheduler& scheduler) noexcept;
double tickJitterP95Ms(const TickScheduler& scheduler) noexcept;
double tickWorkP95Ms(const TickScheduler& scheduler) noexcept;
double tickWorkP99Ms(const TickScheduler& scheduler) noexcept;
double scheduleHeadTailGapMs(const TickScheduler& scheduler) noexcept;
bool meetsScheduleBudget(const TickScheduler& scheduler) noexcept;
void publishScheduleGauges(const TickScheduler& scheduler,
                           ac::metrics::GaugeRegistry* gauges) noexcept;

}  // namespace ac::core
