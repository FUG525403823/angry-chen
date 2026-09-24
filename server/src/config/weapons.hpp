#pragma once
// S08 §5.1/§5.2：武器表、散布/后坐与抖动常量（逐条抄 v1 config/weapons.ts，数值不得再调）。
#include <cstdint>
#include <limits>

namespace ac::config {

inline constexpr int32_t kWeaponSlotCount = 3;

struct WeaponDef {
  double damage;
  int32_t pellets;
  double rpm;
  bool isAuto;
  int32_t mag;
  double reloadMs;
  double spreadDeg;
  double falloffStartM;
  double falloffPerM;
  double headshotMultiplier;
};

// slot 0 pistol / 1 rifle / 2 shotgun（v1 WEAPON_SLOT_ORDER）。
inline constexpr WeaponDef kWeapons[kWeaponSlotCount] = {
    {25.0, 1, 300.0, false, 12, 1400.0, 0.8, 30.0, 0.0, 2.0},   // pistol
    {20.0, 1, 600.0, true, 30, 2000.0, 0.6, 40.0, 0.0, 2.0},    // rifle
    {12.0, 8, 70.0, false, 6, 2600.0, 4.0, 12.0, 0.0, 2.0},     // shotgun
};

inline constexpr int32_t kReserveAmmoInitial = 120;
inline constexpr double kSpreadGrowthPerShotDeg = 0.1;
inline constexpr double kSpreadMaxDeg = 0.25;
inline constexpr uint32_t kSpreadDecayDelayMs = 350u;
inline constexpr double kSpreadDecayPerSecondDeg = 6.0;
inline constexpr double kRecoilPitchPerShotDeg = 0.35;
inline constexpr double kRecoilYawJitterDeg = 0.2;

// §5.3 射线上限（场地对角线 ≈113m ⇒ 等价于无射程上限但仍有限）。
inline constexpr double kShotMaxDistanceM = 160.0;

// §5.1：射速间隔 = 60000 / (rpm * mult)；公式现算，不写近似常数（霰弹枪 60000/70）。
// 非法输入（非有限 / <= 0）返回 +∞，与 v1 的 Number.POSITIVE_INFINITY 同语义。
inline double rpmToIntervalMs(double rpm, double fireRateMultiplier = 1.0) noexcept {
  const double effective = rpm * fireRateMultiplier;
  if (!(effective > 0.0) || effective > 1.0e308) return std::numeric_limits<double>::infinity();
  return 60000.0 / effective;
}

// §5.2 抖动派生：整数哈希（imul 语义）+ 归一化，逐位对齐 v1 unitJitter/signedJitter。
inline uint32_t imul32(uint32_t a, uint32_t b) noexcept {
  const uint64_t product = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
  return static_cast<uint32_t>(product & 0xffffffffu);
}

inline double unitJitter(uint32_t seq, uint32_t salt) noexcept {
  uint32_t x = imul32(seq, 0x9e3779b1u) + imul32(salt, 0x85ebca77u);
  x = imul32(x ^ (x >> 15), 0x2545f491u);
  x = imul32(x ^ (x >> 13), 0x9e3779b1u);
  x = x ^ (x >> 16);
  return static_cast<double>(x) / 4294967296.0;
}

inline double signedJitter(uint32_t seq, uint32_t salt) noexcept {
  return unitJitter(seq, salt) * 2.0 - 1.0;
}

inline constexpr uint32_t kJitterYawSalt = 0x9e3u;
inline constexpr uint32_t kJitterPitchSalt = 0x51fu;
inline constexpr uint32_t kPelletYawStride = 13u;
inline constexpr uint32_t kPelletPitchStride = 29u;
inline constexpr double kDegToRad = 0.017453292519943295;

}  // namespace ac::config
