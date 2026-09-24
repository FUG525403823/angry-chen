#pragma once
// S11 §5（§3 交付物 3）：姿态可解释性判定与硬纠正，语义逐字继承 v1 anticheat.ts 与 O01。
// 处置优先级（§5）：位置非法 → Rejected；超硬上限（或垂直违规）→ Corrected；派生位移可解释 → Suspect；否则 Accept。
#include <cstdint>

#include "config/player.hpp"
#include "core/math.hpp"
#include "metrics/counters.hpp"
#include "sim/arena.hpp"
#include "sim/entity_table.hpp"

namespace ac::security {

// §5 冻结数值：全部由常量表派生，字面量只出现在 static_assert 里做锚点。
inline constexpr double kAdvanceTolerance = 1.15;
inline constexpr double kPoseHardCorrectFactor = 1.5;
inline constexpr uint32_t kPoseDtMs = static_cast<uint32_t>(ac::config::kStepDtMs);
inline constexpr double kHorizontalLimitM =
    ac::config::kSprintSpeedMps * (static_cast<double>(kPoseDtMs) / 1000.0) * kAdvanceTolerance;
inline constexpr double kVerticalLimitMps = ac::config::kJumpSpeedMps * kAdvanceTolerance;
inline constexpr double kHardCorrectLimitM = kHorizontalLimitM * kPoseHardCorrectFactor;

constexpr bool nearPlanValue(double actual, double planned, double tol) noexcept {
  const double diff = actual > planned ? actual - planned : planned - actual;
  return diff <= tol;
}
static_assert(nearPlanValue(kHorizontalLimitM, 0.36225, 1e-12), "§5：单 tick 水平位移上限 = 0.36225 m");
static_assert(nearPlanValue(kVerticalLimitMps, 5.52, 1e-12), "§5：垂直速度上限 = 5.52 m/s");
static_assert(nearPlanValue(kHardCorrectLimitM, 0.543375, 1e-12), "§5：硬纠正阈值 = 0.543375 m");

double horizontalLimitM(uint32_t dtMs) noexcept;
double verticalLimitMps() noexcept;
double hardCorrectLimitM(uint32_t dtMs, double derivedBudgetM) noexcept;
bool explainableByDerived(double distanceM, double limitM, double derivedBudgetM) noexcept;
double horizontalDistanceM(double prevX, double prevZ, double x, double z) noexcept;

// v1 anticheat.ts isLegalPosition：非有限值一律非法；越出栅栏内沿非法；谷仓投影内（且低于仓顶）非法。
bool isLegalPose(double x, double y, double z,
                 double radius = ac::config::kPlayerRadiusM) noexcept;

enum class PoseVerdict : uint8_t {
  kAccept = 0,
  kSuspect,    // 只计数、不改姿态
  kCorrected,  // 硬纠正：回退到 tick 前位置 + 速度清零
  kRejected,   // 位置非法：同样回退（v1 语义：计 poseRejected 且计 hardCorrect）
};

const char* poseVerdictName(PoseVerdict verdict) noexcept;

// 派生预算只由权威模拟产生（击退 / 实体分离 / 静态碰撞外推），客户端不上行该字段。
struct PoseSample {
  double prevX = 0.0;  // tick 前位置（回退目标）
  double prevZ = 0.0;
  double x = 0.0;      // 本 tick 后位置
  double y = 0.0;
  double z = 0.0;
  double velocityY = 0.0;
  double derivedBudgetM = 0.0;
  uint32_t dtMs = kPoseDtMs;
  double radius = ac::config::kPlayerRadiusM;
  uint16_t entityId = ac::sim::kNoEntityId;
};

struct PoseOutcome {
  PoseVerdict verdict = PoseVerdict::kAccept;
  double distanceM = 0.0;
  double limitM = kHorizontalLimitM;
  double hardLimitM = kHardCorrectLimitM;
  bool isSpeedViolation = false;    // 位移超出 limit + 派生预算
  bool isVerticalViolation = false; // |velocityY| > 5.52 m/s
  bool isPositionIllegal = false;   // v1 isLegalPosition 判定
  bool isRolledBack = false;        // true：调用方把位置置为 correctedX/correctedZ 且速度清零
  double correctedX = 0.0;
  double correctedZ = 0.0;
};

PoseOutcome evaluatePose(const PoseSample& sample,
                         ac::metrics::CounterRegistry* counters = nullptr) noexcept;

}  // namespace ac::security
