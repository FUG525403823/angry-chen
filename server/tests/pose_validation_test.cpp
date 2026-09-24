// S11 §5/§6–§7：姿态处置的冻结数值、硬纠正边界（0.36225 / 0.543375 m）、派生预算可解释性与位置合法优先级。
#include "tiny_test.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

#include "config/player.hpp"
#include "core/math.hpp"
#include "sim/arena.hpp"
#include "metrics/counters.hpp"
#include "security/pose_validation.hpp"
#include "security/rate_limit.hpp"
#include "security/rewind.hpp"

namespace sec = ac::security;
namespace metrics = ac::metrics;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double suspectCounter(const metrics::CounterRegistry& counters) {
  return static_cast<double>(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect));
}

sec::PoseSample sampleAt(double x, double z, double prevX = 0.0, double prevZ = 0.0) {
  sec::PoseSample sample{};
  sample.prevX = prevX;
  sample.prevZ = prevZ;
  sample.x = x;
  sample.z = z;
  sample.y = ac::sim::arena::kBarn.max.y;  // 高于仓顶：避免采样点落在谷仓投影内被判位置非法
  return sample;
}

}  // namespace

AC_TEST(motion_authority_frozen_values) {
  AC_CHECK_NEAR(sec::kAdvanceTolerance, 1.15, 1e-12);
  AC_CHECK_NEAR(sec::kPoseHardCorrectFactor, 1.5, 1e-12);
  AC_CHECK_NEAR(sec::kHorizontalLimitM, 0.36225, 1e-12);
  AC_CHECK_NEAR(sec::kVerticalLimitMps, 5.52, 1e-12);
  AC_CHECK_NEAR(sec::kHardCorrectLimitM, 0.543375, 1e-12);
  AC_CHECK_NEAR(sec::horizontalLimitM(50u), 0.36225, 1e-12);
  AC_CHECK_NEAR(sec::verticalLimitMps(), 5.52, 1e-12);
  AC_CHECK_NEAR(sec::hardCorrectLimitM(50u, 0.0), 0.543375, 1e-12);
  AC_CHECK_EQ(sec::kPoseDtMs, 50u);
  AC_CHECK_EQ(sec::kRewindLimitMs, 200u);
  AC_CHECK_EQ(sec::kCommandRateLimit, 30u);
  AC_CHECK_EQ(sec::kMaxMessagesPerSecond, 60u);
  AC_CHECK_EQ(sec::kRateStrikesBeforeDisconnect, 3u);
}

AC_TEST(motion_authority_horizontal_boundary_both_sides) {
  // 冻结值本身在 pose_validation_frozen_values 里以 1e-12 容差钉住；这里取 ±1e-9 相对带避免 1-ulp 翻面。
  const double limit = sec::kHorizontalLimitM;
  const double hard = sec::kHardCorrectLimitM;
  AC_CHECK(sec::evaluatePose(sampleAt(limit * (1.0 - 1e-9), 0.0)).verdict == sec::PoseVerdict::kAccept);
  AC_CHECK(sec::evaluatePose(sampleAt(limit * (1.0 + 1e-9), 0.0)).verdict == sec::PoseVerdict::kSuspect);
  AC_CHECK(sec::evaluatePose(sampleAt(hard * (1.0 - 1e-9), 0.0)).verdict == sec::PoseVerdict::kSuspect);
  AC_CHECK(sec::evaluatePose(sampleAt(hard * (1.0 + 1e-9), 0.0)).verdict == sec::PoseVerdict::kCorrected);
}

AC_TEST(motion_authority_accept_within_limit) {
  metrics::CounterRegistry counters{};
  const sec::PoseOutcome out = sec::evaluatePose(sampleAt(0.3, 0.2), &counters);
  AC_CHECK(out.verdict == sec::PoseVerdict::kAccept);
  AC_CHECK(!out.isRolledBack);
  AC_CHECK_NEAR(out.distanceM, sec::horizontalDistanceM(0.0, 0.0, 0.3, 0.2), 1e-15);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect), 0u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 0u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kSpeedViolations), 0u);
}

AC_TEST(motion_authority_suspect_counts_only) {
  metrics::CounterRegistry counters{};
  const sec::PoseOutcome out = sec::evaluatePose(sampleAt(0.5, 0.0), &counters);
  AC_CHECK(out.verdict == sec::PoseVerdict::kSuspect);
  AC_CHECK(out.isSpeedViolation);
  AC_CHECK(!out.isVerticalViolation && !out.isPositionIllegal);
  AC_CHECK(!out.isRolledBack);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect), 1u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 0u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kSpeedViolations), 1u);
}

AC_TEST(motion_authority_corrected_rolls_back_to_previous_position) {
  metrics::CounterRegistry counters{};
  const sec::PoseOutcome out = sec::evaluatePose(sampleAt(3.0, 4.0, 1.0, 1.0), &counters);
  AC_CHECK(out.verdict == sec::PoseVerdict::kCorrected);
  AC_CHECK(out.isRolledBack);
  AC_CHECK_NEAR(out.correctedX, 1.0, 1e-12);
  AC_CHECK_NEAR(out.correctedZ, 1.0, 1e-12);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 1u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseRejected), 0u);
}

AC_TEST(motion_authority_derived_budget_explains_movement) {
  metrics::CounterRegistry counters{};
  sec::PoseSample sample = sampleAt(0.4, 0.0);
  sample.derivedBudgetM = 0.1;  // 击退 / 分离推挤
  const sec::PoseOutcome out = sec::evaluatePose(sample, &counters);
  AC_CHECK(out.verdict == sec::PoseVerdict::kAccept);
  AC_CHECK(!out.isSpeedViolation);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect), 0u);
  AC_CHECK(sec::explainableByDerived(0.4, sec::kHorizontalLimitM, 0.1));
  AC_CHECK(!sec::explainableByDerived(0.5, sec::kHorizontalLimitM, 0.1));
}

AC_TEST(motion_authority_hard_limit_scales_with_derived_budget) {
  AC_CHECK_NEAR(sec::hardCorrectLimitM(50u, 1.0), (0.36225 + 1.0) * 1.5, 1e-12);
  metrics::CounterRegistry counters{};
  sec::PoseSample explained = sampleAt(1.3, 0.0);  // ≤ limit + 1.0（= 1.36225）→ 完全可解释
  explained.derivedBudgetM = 1.0;
  AC_CHECK(sec::evaluatePose(explained, &counters).verdict == sec::PoseVerdict::kAccept);
  sec::PoseSample suspect = sampleAt(1.5, 0.0);  // 超出可解释范围但未过 (limit+1)×1.5
  suspect.derivedBudgetM = 1.0;
  AC_CHECK(sec::evaluatePose(suspect, &counters).verdict == sec::PoseVerdict::kSuspect);
  sec::PoseSample fast = sampleAt(2.1, 0.0);
  fast.derivedBudgetM = 1.0;
  AC_CHECK(sec::evaluatePose(fast, &counters).verdict == sec::PoseVerdict::kCorrected);
}

AC_TEST(motion_authority_legality_helpers) {
  AC_CHECK(sec::isLegalPose(0.0, 0.0, 20.0));
  AC_CHECK(!sec::isLegalPose(0.0, 0.0, 0.0));  // 谷仓投影内
  AC_CHECK(sec::isLegalPose(0.0, 5.0, 0.0));   // 高于仓顶 → 合法
  AC_CHECK(!sec::isLegalPose(39.7, 0.0, 20.0));  // 越出栅栏内沿（40 - 0.25 - 0.4）
  AC_CHECK(!sec::isLegalPose(kNaN, 0.0, 20.0));
  const double barnEdge = ac::sim::arena::kBarn.min.x - ac::config::kPlayerRadiusM;
  AC_CHECK(sec::isLegalPose(barnEdge, 0.0, 0.0));  // 正好被推到边界 → 仓外
}

AC_TEST(motion_authority_position_rejected_and_counted) {
  metrics::CounterRegistry counters{};
  const sec::PoseOutcome out = sec::evaluatePose(sampleAt(40.0, 40.0), &counters);  // 越出栅栏内沿
  AC_CHECK(out.verdict == sec::PoseVerdict::kRejected);
  AC_CHECK(out.isPositionIllegal);
  AC_CHECK(out.isRolledBack);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseRejected), 1u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 1u);
  AC_CHECK_NEAR(out.correctedX, 0.0, 1e-12);
}

AC_TEST(motion_authority_nan_position_rejected) {
  metrics::CounterRegistry counters{};
  const sec::PoseOutcome out = sec::evaluatePose(sampleAt(kNaN, 0.0), &counters);
  AC_CHECK(out.verdict == sec::PoseVerdict::kRejected);
  AC_CHECK(out.isPositionIllegal);
  AC_CHECK(out.isRolledBack);
}

AC_TEST(motion_authority_position_priority_over_speed) {
  metrics::CounterRegistry counters{};
  const sec::PoseOutcome out = sec::evaluatePose(sampleAt(1.0e6, 1.0e6), &counters);
  AC_CHECK(out.verdict == sec::PoseVerdict::kRejected);  // 位置非法优先于超硬上限
  AC_CHECK(out.isPositionIllegal);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseRejected), 1u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect), 0u);
}

AC_TEST(motion_authority_vertical_violation_corrected) {
  metrics::CounterRegistry counters{};
  sec::PoseSample sample = sampleAt(0.1, 0.0);
  sample.velocityY = 5.53;
  const sec::PoseOutcome out = sec::evaluatePose(sample, &counters);
  AC_CHECK(out.verdict == sec::PoseVerdict::kCorrected);
  AC_CHECK(out.isVerticalViolation);
  AC_CHECK(!out.isSpeedViolation);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kSpeedViolations), 1u);
  sec::PoseSample atLimit = sampleAt(0.1, 0.0);
  atLimit.velocityY = 5.52;
  AC_CHECK(!sec::evaluatePose(atLimit, nullptr).isVerticalViolation);  // 恰好等于上限不算违规
}

AC_TEST(motion_authority_suspect_ge_hard_correct_in_one_run) {
  metrics::CounterRegistry counters{};
  for (uint32_t i = 0u; i < 4u; ++i) (void)sec::evaluatePose(sampleAt(0.45, 0.0), &counters);
  for (uint32_t i = 0u; i < 2u; ++i) (void)sec::evaluatePose(sampleAt(0.9, 0.0), &counters);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect), 4u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kHardCorrect), 2u);
  AC_CHECK(metrics::counterValue(counters, metrics::CounterId::kPoseSuspect) >=
           metrics::counterValue(counters, metrics::CounterId::kHardCorrect));
  AC_CHECK(suspectCounter(counters) >= 4.0);
}
