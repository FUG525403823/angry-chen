#pragma once
// S09 §5.4：波次上限、预算公式、人数缩放、生成节流与出生点规则（逐条抄 v1 config/waves.ts）。
// 取整恒为 floor(x + 0.5)（v1 Math.round 的口径，见 docs/evidence/fixtures/README.md §6.1）。
#include <cmath>
#include <cstdint>

#include "config/sheep.hpp"

namespace ac::config {

inline constexpr int32_t kWaveMax = 10;
inline constexpr double kWaveIntermissionMs = 20000.0;
inline constexpr double kWaveIntermissionMinMs = 5000.0;
inline constexpr double kBudgetScalePerExtraPlayer = 0.35;
inline constexpr double kSpeedScalePerExtraPlayer = 0.02;
inline constexpr int32_t kMaxSpawnsPerTick = 8;
inline constexpr int32_t kMaxActiveSpawnPoints = 3;
inline constexpr double kMinSpawnDistanceM = 15.0;

// 单参数取整（v1 Math.round）：先 +0.5 再向下取整。
inline int32_t roundHalfUp(double value) noexcept {
  return static_cast<int32_t>(std::floor(value + 0.5));
}

// §5.4 基础预算：round(6 + 3.2 * w + 0.18 * w * w)（w < 0 取 0；运算顺序与 v1 逐字一致）。
inline int32_t waveBaseBudget(int32_t wave) noexcept {
  const double w = wave < 0 ? 0.0 : static_cast<double>(wave);
  return roundHalfUp(6.0 + 3.2 * w + 0.18 * w * w);
}

// §5.4 人数缩放：先算 base，再乘系数后整体取整；players < 1 取 1。
inline int32_t waveBudget(int32_t wave, int32_t playerCount) noexcept {
  const int32_t players = playerCount < 1 ? 1 : playerCount;
  const double scale = 1.0 + kBudgetScalePerExtraPlayer * static_cast<double>(players - 1);
  return roundHalfUp(static_cast<double>(waveBaseBudget(wave)) * scale);
}

inline double sheepSpeedMultiplier(int32_t playerCount) noexcept {
  const int32_t players = playerCount < 1 ? 1 : playerCount;
  return 1.0 + kSpeedScalePerExtraPlayer * static_cast<double>(players - 1);
}

inline constexpr double sheepPrice(SheepKind kind) noexcept { return kSheep[sheepIndexOf(kind)].price; }

// v1 firstWaveFor：ram 第三波、elite/king 第五波、其余第一波。
inline constexpr int32_t firstWaveFor(SheepKind kind) noexcept {
  return kind == SheepKind::kRam ? 3 : (kind == SheepKind::kElite ? 5 : (kind == SheepKind::kKing ? 5 : 1));
}

inline constexpr bool isBossWave(int32_t wave) noexcept { return wave > 0 && wave % 5 == 0; }

inline constexpr bool kindAllowedAt(SheepKind kind, int32_t wave) noexcept {
  return kind == SheepKind::kKing ? isBossWave(wave) : wave >= firstWaveFor(kind);
}

inline constexpr int32_t sheepKindIndex(SheepKind kind) noexcept { return static_cast<int32_t>(kind); }

static_assert(kWaveMax == 10 && kWaveIntermissionMs == 20000.0 && kWaveIntermissionMinMs == 5000.0,
              "§5.4：波次上限与波间时长冻结");
static_assert(kMaxSpawnsPerTick == 8 && kMaxActiveSpawnPoints == 3 && kMinSpawnDistanceM == 15.0,
              "§5.4：生成节流与出生点距离冻结");

}  // namespace ac::config
