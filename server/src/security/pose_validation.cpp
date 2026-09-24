#include "security/pose_validation.hpp"

#include <cmath>

namespace ac::security {

double horizontalLimitM(uint32_t dtMs) noexcept {
  return ac::config::kSprintSpeedMps * (static_cast<double>(dtMs) / 1000.0) * kAdvanceTolerance;
}

double verticalLimitMps() noexcept { return ac::config::kJumpSpeedMps * kAdvanceTolerance; }

double hardCorrectLimitM(uint32_t dtMs, double derivedBudgetM) noexcept {
  return (horizontalLimitM(dtMs) + derivedBudgetM) * kPoseHardCorrectFactor;
}

bool explainableByDerived(double distanceM, double limitM, double derivedBudgetM) noexcept {
  return distanceM <= limitM + derivedBudgetM;
}

double horizontalDistanceM(double prevX, double prevZ, double x, double z) noexcept {
  const double dx = x - prevX;
  const double dz = z - prevZ;
  return std::sqrt(dx * dx + dz * dz);
}

bool isLegalPose(double x, double y, double z, double radius) noexcept {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
  const double limit =
      ac::sim::arena::kArenaHalfSizeMeters - ac::config::kFenceHalfThicknessM - radius;
  constexpr double kEpsilon = 1e-6;
  if (std::abs(x) > limit + kEpsilon || std::abs(z) > limit + kEpsilon) return false;
  const ac::Aabb& barn = ac::sim::arena::kBarn;
  if (y >= barn.max.y) return true;
  // 边界语义必须与 S06 的静态碰撞一致：正好被推到 minX/maxX 的落点算「仓外」。
  const bool insideX = x > barn.min.x - radius && x < barn.max.x + radius;
  const bool insideZ = z > barn.min.z - radius && z < barn.max.z + radius;
  return !(insideX && insideZ);
}

const char* poseVerdictName(PoseVerdict verdict) noexcept {
  switch (verdict) {
    case PoseVerdict::kAccept: return "accept";
    case PoseVerdict::kSuspect: return "suspect";
    case PoseVerdict::kCorrected: return "corrected";
    case PoseVerdict::kRejected: return "rejected";
  }
  return "unknown";
}

PoseOutcome evaluatePose(const PoseSample& sample, ac::metrics::CounterRegistry* counters) noexcept {
  PoseOutcome out{};
  out.limitM = horizontalLimitM(sample.dtMs);
  out.hardLimitM = hardCorrectLimitM(sample.dtMs, sample.derivedBudgetM);
  out.correctedX = sample.prevX;
  out.correctedZ = sample.prevZ;
  out.distanceM = horizontalDistanceM(sample.prevX, sample.prevZ, sample.x, sample.z);
  out.isSpeedViolation = out.distanceM > out.limitM + sample.derivedBudgetM;
  out.isVerticalViolation = std::abs(sample.velocityY) > verticalLimitMps();
  out.isPositionIllegal = !isLegalPose(sample.x, sample.y, sample.z, sample.radius);

  // v1 applyPoseValidation：可解释 + 不垂直违规 + 位置合法 → 直接通过（不改姿态）。
  if (explainableByDerived(out.distanceM, out.limitM, sample.derivedBudgetM) &&
      !out.isVerticalViolation && !out.isPositionIllegal) {
    out.verdict = PoseVerdict::kAccept;
    return out;
  }
  // v1 口径：任何「速度或垂直」违规都计 ac_speed_violations_total，包括最终只判 Suspect 的那些。
  if (out.isSpeedViolation || out.isVerticalViolation) {
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kSpeedViolations);
  }
  const bool isCorrected =
      out.isPositionIllegal || out.isVerticalViolation || out.distanceM > out.hardLimitM;
  if (!isCorrected) {
    out.verdict = PoseVerdict::kSuspect;
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kPoseSuspect);
    return out;
  }
  out.isRolledBack = true;
  if (out.isPositionIllegal) {
    out.verdict = PoseVerdict::kRejected;
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kPoseRejected);
  } else {
    out.verdict = PoseVerdict::kCorrected;
  }
  ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kHardCorrect);
  return out;
}

}  // namespace ac::security
