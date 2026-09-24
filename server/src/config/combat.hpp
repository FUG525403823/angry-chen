#pragma once
// S08 §5.4/§5.5/§5.7：部位口径、护甲/生命、怒气、救援常量与四种羊形命中盒表。
// 数值逐条抄 v1 config/combat.ts 与 config/sheep.ts 的 SHEEP_HIT（羊王取 BO SS/king 档案）。
#include <cstdint>

namespace ac::config {

enum class HitPart : uint8_t { kHead = 0u, kTorso = 1u, kLimb = 2u };

inline constexpr double kBodyPartMultiplierTorso = 1.0;
inline constexpr double kBodyPartMultiplierLimb = 0.75;
inline constexpr double kHeadMinHeightRatio = 0.85;
inline constexpr double kTorsoMinHeightRatio = 0.4;
// 护甲吸收比 / 生命与护甲上限已在 config/player.hpp（S06 §5.4）冻结：kArmorAbsorbRatio / kMaxHp / kMaxArmor。
inline constexpr double kFalloffMinMultiplier = 0.5;

// §5.6 命中事件 flags（与 v1 net/protocol.ts 的 HIT_FLAG 同值，也与 S03 §5.4 的载荷一致）。
inline constexpr uint8_t kHitFlagHeadshot = 1u;
inline constexpr uint8_t kHitFlagDowned = 2u;
inline constexpr uint8_t kHitFlagKilled = 4u;

// §5.2：开火时的俯仰夹取（v1 combat/resolve.ts 的 PITCH_LIMIT_RAD = 1.5533，**不是**输入域的 pi/2）。
inline constexpr double kAimPitchLimitRad = 1.5533;
inline constexpr bool kFriendlyFire = false;

inline constexpr double kRageMax = 100.0;
inline constexpr double kRagePerKill = 8.0;
inline constexpr double kRagePerEliteKill = 20.0;
inline constexpr double kRageHeadshotKillMultiplier = 2.0;
inline constexpr double kRageIdleDecayDelayMs = 10000.0;
inline constexpr double kRageDecayPerSecond = 5.0;
inline constexpr double kRageDurationMs = 8000.0;
inline constexpr double kRageDamageMultiplier = 1.3;
inline constexpr double kRageFireRateMultiplier = 1.25;
inline constexpr double kRageMoveSpeedMultiplier = 1.15;

inline constexpr double kReviveRangeM = 2.0;
inline constexpr double kReviveDurationMs = 3000.0;
inline constexpr double kReviveResetDelayMs = 5000.0;
inline constexpr double kRevivedHpRatio = 0.5;
inline constexpr double kWaveReviveHpRatio = 0.5;
inline constexpr double kProgressEventStepRatio = 0.05;

// §5.7：精英判定与 v1 SHEEP_ELITE_STATE 逐字一致（= 羊状态码 2）。
inline constexpr uint8_t kSheepEliteState = 2u;

// §5.4 羊形命中盒（渲染盒体口径，与移动/分离用的 radiusM/heightM 分离）。
struct SheepHitProfile {
  double halfWidthM;
  double halfDepthM;
  double topM;
  double headHalfWidthM;
  double headMinYM;
  double headMaxYM;
  double headMinZM;
  double headMaxZM;
  double headMinM;
  double torsoMinM;
};

enum class SheepKind : uint8_t { kGrunt = 0u, kRam = 1u, kElite = 2u, kKing = 3u };

inline constexpr SheepHitProfile kSheepHit[4] = {
    {0.86, 0.66, 1.15, 0.22, 0.68, 1.12, 0.41, 0.91, 0.78, 0.34},  // grunt
    {0.92, 0.70, 1.22, 0.24, 0.72, 1.19, 0.43, 0.97, 0.83, 0.36},  // ram
    {0.97, 0.74, 1.29, 0.25, 0.76, 1.26, 0.45, 1.02, 0.87, 0.38},  // elite
    {1.38, 1.06, 2.05, 0.36, 1.08, 2.04, 0.65, 1.46, 1.25, 0.54},  // king
};

// v1 partForHeight（玩家口径，逐位不变）；height <= 0 时 ratio 记 0。
inline HitPart partForHeight(double baseY, double height, double hitY) noexcept {
  const double ratio = height <= 0.0 ? 0.0 : (hitY - baseY) / height;
  if (ratio >= kHeadMinHeightRatio) return HitPart::kHead;
  if (ratio >= kTorsoMinHeightRatio) return HitPart::kTorso;
  return HitPart::kLimb;
}

// v1 partForThresholds（羊口径：显式米制阈值）。
inline HitPart partForThresholds(double baseY, double headMinM, double torsoMinM, double hitY) noexcept {
  const double local = hitY - baseY;
  if (local >= headMinM) return HitPart::kHead;
  if (local >= torsoMinM) return HitPart::kTorso;
  return HitPart::kLimb;
}

inline double partMultiplier(HitPart part, double headshotMultiplier) noexcept {
  if (part == HitPart::kHead) return headshotMultiplier;
  if (part == HitPart::kTorso) return kBodyPartMultiplierTorso;
  return kBodyPartMultiplierLimb;
}

}  // namespace ac::config
