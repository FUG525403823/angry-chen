// S12 §4-7/§6-2：tick 档位相位、档位状态机、8 ms 工作量预算（让出 ≠ 丢 tick）、
// 抖动 P95 与仿真漂移口径。
#include "tiny_test.hpp"

#include <cstdint>

#include "core/scheduler.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"
#include "replication/snapshot_rate.hpp"

namespace core = ac::core;
namespace metrics = ac::metrics;
namespace bp = ac::replication;

namespace {

// 固定序列的评估循环：每 1000 ms 评估一次（§5 评估周期 ≥ 1000 ms）。
struct RateWalk {
  bp::SnapshotRateState state;
  uint16_t levels[8] = {};
  int count = 0;

  RateWalk() { levels[count++] = bp::snapshotRateX10(state); }

  bool step(uint32_t nowMs, bool isTriggered) {
    bp::RateTriggers triggers{};
    triggers.isBacklogOverHalf = isTriggered;
    const bool didChange = bp::updateSnapshotRate(state, nowMs, triggers, nullptr);
    if (didChange && count < 8) levels[count++] = bp::snapshotRateX10(state);
    return didChange;
  }
};

}  // namespace

AC_TEST(schedule_phase_high_sends_every_tick) {
  int sent = 0;
  for (uint32_t tick = 0u; tick < 60u; ++tick) {
    if (bp::shouldSendSnapshot(200u, tick)) ++sent;
  }
  AC_CHECK_EQ(sent, 60);
}

AC_TEST(schedule_phase_mid_sends_two_of_three) {
  int sent = 0;
  for (uint32_t tick = 0u; tick < 60u; ++tick) {
    if (bp::shouldSendSnapshot(150u, tick)) ++sent;
  }
  AC_CHECK_EQ(sent, 40);
  AC_CHECK(bp::shouldSendSnapshot(150u, 0u));
  AC_CHECK(bp::shouldSendSnapshot(150u, 1u));
  AC_CHECK(!bp::shouldSendSnapshot(150u, 2u));
  AC_CHECK(bp::shouldSendSnapshot(150u, 3u));
}

AC_TEST(schedule_phase_low_sends_every_other_tick) {
  int sent = 0;
  for (uint32_t tick = 0u; tick < 60u; ++tick) {
    if (bp::shouldSendSnapshot(100u, tick)) ++sent;
  }
  AC_CHECK_EQ(sent, 30);
  AC_CHECK(bp::shouldSendSnapshot(100u, 0u));
  AC_CHECK(!bp::shouldSendSnapshot(100u, 1u));
}

AC_TEST(schedule_rate_machine_walks_all_levels) {
  RateWalk walk;
  walk.step(1000u, true);   // 降档 150
  walk.step(2000u, true);   // 降档 100
  walk.step(3000u, false);
  walk.step(4000u, false);
  walk.step(5000u, false);  // 干净 3000 ms 升档 150
  walk.step(6000u, false);
  walk.step(7000u, false);
  walk.step(8000u, false);  // 再干净 3000 ms 升档 200
  AC_CHECK_EQ(walk.count, 5);
  AC_CHECK_EQ(walk.levels[0], 200u);
  AC_CHECK_EQ(walk.levels[1], 150u);
  AC_CHECK_EQ(walk.levels[2], 100u);
  AC_CHECK_EQ(walk.levels[3], 150u);
  AC_CHECK_EQ(walk.levels[4], 200u);
  AC_CHECK_EQ(walk.state.downshifts, 2u);
  AC_CHECK_EQ(walk.state.upshifts, 2u);
  AC_CHECK_EQ(bp::snapshotRateX10(walk.state), bp::kSnapshotRateHighX10);
}

AC_TEST(schedule_eval_period_is_one_second) {
  bp::SnapshotRateState state;
  bp::RateTriggers triggered{};
  triggered.hasTickSkipsGrown = true;
  AC_CHECK(bp::updateSnapshotRate(state, 0u, triggered, nullptr));
  AC_CHECK_EQ(bp::snapshotRateX10(state), bp::kSnapshotRateMidX10);
  triggered.hasTickSkipsGrown = false;
  AC_CHECK(!bp::updateSnapshotRate(state, 500u, triggered, nullptr));
  AC_CHECK_EQ(bp::snapshotRateX10(state), bp::kSnapshotRateMidX10);
  triggered.hasTickSkipsGrown = true;
  AC_CHECK(bp::updateSnapshotRate(state, 1000u, triggered, nullptr));
  AC_CHECK_EQ(bp::snapshotRateX10(state), bp::kSnapshotRateLowX10);
  AC_CHECK_EQ(state.lastEvalMs, 1000u);
}

AC_TEST(schedule_clean_period_is_three_seconds) {
  bp::SnapshotRateState state;
  bp::RateTriggers triggered{};
  triggered.hasTickSkipsGrown = true;
  AC_CHECK(bp::updateSnapshotRate(state, 0u, triggered, nullptr));
  triggered.hasTickSkipsGrown = false;
  AC_CHECK(!bp::updateSnapshotRate(state, 1000u, triggered, nullptr));
  AC_CHECK(!bp::updateSnapshotRate(state, 2000u, triggered, nullptr));
  AC_CHECK(bp::updateSnapshotRate(state, 3000u, triggered, nullptr));
  AC_CHECK_EQ(bp::snapshotRateX10(state), bp::kSnapshotRateHighX10);
}

AC_TEST(schedule_downshift_signals_are_independent) {
  bp::SnapshotRateState backlog;
  bp::RateTriggers backlogTrigger{};
  backlogTrigger.isBacklogOverHalf = true;
  AC_CHECK(bp::updateSnapshotRate(backlog, 0u, backlogTrigger, nullptr));
  AC_CHECK_EQ(bp::snapshotRateX10(backlog), bp::kSnapshotRateMidX10);

  bp::SnapshotRateState skips;
  bp::RateTriggers skipTrigger{};
  skipTrigger.hasTickSkipsGrown = true;
  AC_CHECK(bp::updateSnapshotRate(skips, 0u, skipTrigger, nullptr));
  AC_CHECK_EQ(bp::snapshotRateX10(skips), bp::kSnapshotRateMidX10);

  bp::SnapshotRateState budget;
  bp::RateTriggers budgetTrigger{};
  budgetTrigger.hasBudgetExceededGrown = true;
  AC_CHECK(bp::updateSnapshotRate(budget, 0u, budgetTrigger, nullptr));
  AC_CHECK_EQ(bp::snapshotRateX10(budget), bp::kSnapshotRateMidX10);

  metrics::CounterRegistry counters{};
  bp::SnapshotRateState counted;
  bp::RateTriggers none{};
  AC_CHECK(bp::updateSnapshotRate(counted, 0u, budgetTrigger, &counters));
  AC_CHECK(bp::updateSnapshotRate(counted, 8000u, budgetTrigger, &counters));
  AC_CHECK(!bp::updateSnapshotRate(counted, 16000u, budgetTrigger, &counters));  // 已到最低档
  AC_CHECK_EQ(counted.downshifts, 2u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kSnapshotRateDownshifts), 2u);
  AC_CHECK(!bp::updateSnapshotRate(counted, 17000u, none, &counters));
}

AC_TEST(schedule_budget_yield_does_not_drop_ticks) {
  AC_CHECK_EQ(core::kRoomTickBudgetMs, 8u);
  core::TickScheduler scheduler;
  core::startScheduler(scheduler, 1000u);
  AC_CHECK_EQ(core::pendingTicks(scheduler, 1000u), 1u);
  AC_CHECK_EQ(core::pendingTicks(scheduler, 1099u), 2u);
  AC_CHECK_EQ(core::pendingTicks(scheduler, 1100u), 3u);

  metrics::CounterRegistry counters{};
  metrics::GaugeRegistry gauges{};
  const bool didYield = core::noteTickRun(scheduler, 1000u, 9u, &counters, &gauges);
  AC_CHECK(didYield);
  AC_CHECK_EQ(scheduler.budgetExceeded, 1u);
  AC_CHECK_EQ(scheduler.tickIndex, 1u);
  AC_CHECK_EQ(core::accountedTicks(scheduler), 1u);
  AC_CHECK_EQ(scheduler.tickSkips, 0u);  // 让出不记丢 tick
  AC_CHECK_EQ(core::pendingTicks(scheduler, 1100u), 2u);  // 累积量照旧
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kRoomBudgetExceeded), 1u);
  AC_CHECK(!core::noteTickRun(scheduler, 1050u, 7u, &counters, &gauges));
  AC_CHECK_EQ(core::accountedTicks(scheduler), 2u);
}

AC_TEST(schedule_tick_skip_is_counted_separately) {
  core::TickScheduler scheduler;
  core::startScheduler(scheduler, 0u);
  metrics::CounterRegistry counters{};
  core::noteTickSkip(scheduler, 3u, &counters);
  AC_CHECK_EQ(scheduler.tickSkips, 3u);
  AC_CHECK_EQ(core::accountedTicks(scheduler), 3u);
  AC_CHECK_EQ(scheduler.tickIndex, 0u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kTickSkips), 3u);
  AC_CHECK_NEAR(core::tickScheduleErrorP95Ms(scheduler), 0.0, 1e-9);  // 无采样即无误差，丢 tick 另有计数
}

AC_TEST(schedule_error_p95_reports_worst_case) {
  core::TickScheduler scheduler;
  core::startScheduler(scheduler, 0u);
  for (uint32_t i = 0u; i < 20u; ++i) {
    core::noteTickRun(scheduler, static_cast<uint64_t>(i) * 50u, 1u);
  }
  AC_CHECK_NEAR(core::tickScheduleErrorP95Ms(scheduler), 0.0, 1e-9);
  AC_CHECK(core::meetsScheduleBudget(scheduler));
  for (uint32_t i = 20u; i < 40u; ++i) {
    core::noteTickRun(scheduler, static_cast<uint64_t>(i) * 50u + 30u, 1u);
  }
  AC_CHECK_NEAR(core::tickScheduleErrorP95Ms(scheduler), 30.0, 1e-9);
  AC_CHECK(!core::meetsScheduleBudget(scheduler));
  AC_CHECK_NEAR(core::scheduleErrorMs(scheduler, 2000u), 0.0, 1e-9);  // 40 tick × 50 ms = 2000 ms
  AC_CHECK_NEAR(core::scheduleErrorMs(scheduler, 1900u), -100.0, 1e-9);
}

AC_TEST(schedule_late_ticks_gap_grows) {
  core::TickScheduler scheduler;
  core::startScheduler(scheduler, 0u);
  for (uint32_t i = 0u; i < 18u; ++i) {
    core::noteTickRun(scheduler, static_cast<uint64_t>(i) * 50u + 1u, 1u);  // 前 1/3：误差 1 ms
  }
  for (uint32_t i = 18u; i < 36u; ++i) {
    core::noteTickRun(scheduler, static_cast<uint64_t>(i) * 50u + 6u, 1u);  // 后 1/3：误差 6 ms
  }
  AC_CHECK_NEAR(core::scheduleHeadTailGapMs(scheduler), 5.0, 1e-9);  // 前 1/3 vs 后 1/3
  AC_CHECK_NEAR(core::tickScheduleErrorP95Ms(scheduler), 6.0, 1e-9);
}

AC_TEST(schedule_sim_drift_formula_holds) {
  AC_CHECK_EQ(core::kSimDriftBudgetMs, 50);
  AC_CHECK_EQ(core::simDriftMs(core::SimClock{0u, 0u}, 1000u, 1000u), 0);
  AC_CHECK_EQ(core::simDriftMs(core::SimClock{0u, 0u}, 1030u, 1000u), 30);
  AC_CHECK_EQ(core::simDriftMs(core::SimClock{100u, 100u}, 980u, 1000u), -20);
  AC_CHECK_EQ(core::simDriftMs(core::SimClock{1000u, 0u}, 2000u, 500u), 500);  // 仿真跑得比墙钟快
  AC_CHECK_EQ(core::simDriftMs(core::SimClock{0u, 0u}, 1000u, 2000u), -1000);

  // 房间每 tick 走 noteSimClock：漂移既回给调用方，也写进 ac_sim_drift_ms。
  core::TickScheduler scheduler;
  metrics::GaugeRegistry gauges{};
  core::startScheduler(scheduler, 0u);
  AC_CHECK_EQ(core::noteSimClock(scheduler, 30u, 50u, &gauges), -20);
  AC_CHECK_NEAR(metrics::gaugeValue(gauges, metrics::GaugeId::kSimDriftMs), -20.0, 1e-12);
  AC_CHECK_EQ(core::accountedTicks(scheduler), 0u);
}

AC_TEST(schedule_frozen_values_are_named) {
  AC_CHECK_EQ(static_cast<uint32_t>(ac::kTickMs), 50u);
  AC_CHECK_EQ(core::kRoomTickBudgetMs, 8u);
  AC_CHECK_NEAR(core::kTickScheduleErrorP95BudgetMs, 8.0, 1e-12);
  AC_CHECK_NEAR(core::kScheduleHeadTailGapBudgetMs, 2.0, 1e-12);
  AC_CHECK_EQ(core::kScheduleSampleCount, 64u);
  AC_CHECK_EQ(bp::kSnapshotRateHighX10, 200u);
  AC_CHECK_EQ(bp::kSnapshotRateMidX10, 150u);
  AC_CHECK_EQ(bp::kSnapshotRateLowX10, 100u);
  AC_CHECK_EQ(bp::kSnapshotRateMinX10, 100u);
  AC_CHECK_EQ(bp::kSnapshotRateMaxX10, 200u);
  AC_CHECK_EQ(bp::kSnapshotRateEvalPeriodMs, 1000u);
  AC_CHECK_EQ(bp::kSnapshotRateCleanPeriodMs, 3000u);
  AC_CHECK_EQ(bp::kSnapshotRateLevelCount, 3u);
}

// §4-8「接线」：每个量值都得有生产者在写；这里逐条把生产者跑一遍并读回量值表。
AC_TEST(schedule_gauges_are_written_by_producers) {
  metrics::GaugeRegistry gauges{};
  core::TickScheduler scheduler;
  core::startScheduler(scheduler, 0u);
  core::noteTickRun(scheduler, 50u, 3u, nullptr, &gauges);
  AC_CHECK_NEAR(metrics::gaugeValue(gauges, metrics::GaugeId::kTickScheduleErrorMsP95),
                core::tickScheduleErrorP95Ms(scheduler), 1e-12);

  bp::SnapshotRateState rate;
  bp::RateTriggers triggers{};
  bp::updateSnapshotRate(rate, 0u, triggers, nullptr, &gauges);
  AC_CHECK_NEAR(metrics::gaugeValue(gauges, metrics::GaugeId::kSnapshotRateX10), 200.0, 1e-12);
  triggers.isBacklogOverHalf = true;
  bp::updateSnapshotRate(rate, 1000u, triggers, nullptr, &gauges);
  AC_CHECK_NEAR(metrics::gaugeValue(gauges, metrics::GaugeId::kSnapshotRateX10), 150.0, 1e-12);
}
