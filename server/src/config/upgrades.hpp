#pragma once
// 波次升级表（S16）：每波清波发 1 点，波间购买，立即生效。
// 数值是新增玩法常量（v1 无此系统，不参与 configHash——真值来源是冻结的 v1 实现，
// 见 docs/evidence/fixtures/README.md §1）；跨语言一致性由客户端 UpgradeTable.cs 镜像 +
// 双侧常量断言测试保证（与 WeaponTable 镜像 kWeapons 同一模式）。
#include <cstdint>

namespace ac::config {

inline constexpr int32_t kUpgradeCount = 4;
inline constexpr int32_t kUpgradeMaxLevel = 5;
inline constexpr int32_t kPointsPerWaveClear = 1;

// id：0 伤害 / 1 移速 / 2 换弹 / 3 备弹（与 MatchState/UpgradeSelect 的线上编号一致）。
inline constexpr int32_t kUpgradeDamage = 0;
inline constexpr int32_t kUpgradeSpeed = 1;
inline constexpr int32_t kUpgradeReload = 2;
inline constexpr int32_t kUpgradeReserve = 3;

// 每级增量（乘数公式见下）。
inline constexpr double kDamagePerLevel = 0.15;
inline constexpr double kSpeedPerLevel = 0.08;
inline constexpr double kReloadTimePerLevel = 0.12;
inline constexpr int32_t kReservePerLevel = 40;

// 换弹时间乘数下限：等级 5 时 1.0 − 0.6 = 0.4（不为 0）。
inline constexpr double kReloadTimeMinMultiplier = 0.4;

inline constexpr double upgradeDamageMultiplier(int32_t level) noexcept {
  const int32_t lv = level < 0 ? 0 : (level > kUpgradeMaxLevel ? kUpgradeMaxLevel : level);
  return 1.0 + kDamagePerLevel * static_cast<double>(lv);
}

inline constexpr double upgradeSpeedMultiplier(int32_t level) noexcept {
  const int32_t lv = level < 0 ? 0 : (level > kUpgradeMaxLevel ? kUpgradeMaxLevel : level);
  return 1.0 + kSpeedPerLevel * static_cast<double>(lv);
}

// 换弹时间乘数：1.0 − 0.12×lv，下限 0.4（reloadEndsAtMs = nowMs + def.reloadMs × mult）。
inline constexpr double upgradeReloadTimeMultiplier(int32_t level) noexcept {
  const int32_t lv = level < 0 ? 0 : (level > kUpgradeMaxLevel ? kUpgradeMaxLevel : level);
  const double mult = 1.0 - kReloadTimePerLevel * static_cast<double>(lv);
  return mult < kReloadTimeMinMultiplier ? kReloadTimeMinMultiplier : mult;
}

inline constexpr int32_t upgradeReserveBonus(int32_t level) noexcept {
  const int32_t lv = level < 0 ? 0 : (level > kUpgradeMaxLevel ? kUpgradeMaxLevel : level);
  return kReservePerLevel * lv;
}

}  // namespace ac::config
