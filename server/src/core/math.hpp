#pragma once
// S02 §5.1：Vec3/Aabb 与纯函数；角度一律走共享查表（trig_table.hpp），只允许 + - * / sqrt。
// 字段名与语义对应 v1 packages/shared/src/math.ts。rightFromYaw 是镜像基，符号禁止翻转
// （它同时决定射线、羊群转向与快照语义）。

#include <cmath>
#include <cstdint>

#include "trig_table.hpp"

namespace ac {

struct Vec3 {
  double x;
  double y;
  double z;
};

struct Aabb {
  Vec3 min;
  Vec3 max;
};

inline constexpr double kPi = 3.14159265358979323846264338327950288;

// kTwoPi 由生成的 trig_table.hpp 提供（角度单位换算要用它）；这里把接缝钉成编译期断言，
// 生成脚本改了常量拼写会立刻编译失败，而不是静默改变 wrapAngle 的语义。
static_assert(kTwoPi == 2.0 * kPi, "trig_table.hpp 的 kTwoPi 必须是 2*kPi");
static_assert(kUnitsPerTurn == 65536u, "trig_table.hpp 的 kUnitsPerTurn 必须是 65536");

// §5.5 时间与容量常量（S05/S06/S10/S12 逐字复用）。
inline constexpr int32_t kTickMs = 50;
inline constexpr int32_t kTicksPerSecond = 20;
inline constexpr int32_t kRewindLimitMs = 200;
inline constexpr int32_t kPoseHistorySlots = 20;
inline constexpr int32_t kMaxEntities = 1024;

inline Vec3 addScaled(const Vec3& base, const Vec3& dir, double scale) noexcept {
  return Vec3{base.x + dir.x * scale, base.y + dir.y * scale, base.z + dir.z * scale};
}

inline double lengthSq(const Vec3& a) noexcept { return a.x * a.x + a.y * a.y + a.z * a.z; }

inline double length(const Vec3& a) noexcept { return std::sqrt(lengthSq(a)); }

inline Vec3 normalize(const Vec3& a) noexcept {
  const double len = length(a);
  if (len == 0.0) return Vec3{0.0, 0.0, 0.0};  // 零向量不产生 NaN
  return Vec3{a.x / len, a.y / len, a.z / len};
}

inline double clamp(double v, double lo, double hi) noexcept {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

inline double clamp01(double v) noexcept { return clamp(v, 0.0, 1.0); }

// 归一到 (-PI, PI]；非有限输入返回 0。
// 只用 + - * / 与 floor（ADR-010 §2 的运算子集，不用 std::fmod 这类库函数）。
// 与 v1 wrapAngle 的差别只有「恰好 -PI 折到 +PI」（§5.1 冻结左开右闭，v1 是 [-PI, PI]）；
// 在 |rad| <= 64 上与 v1 的 JS `%` 逐位一致（12.8M 采样零差异），并保留 v1 在负的 2*PI 整数倍上返回 -0 的符号位。
inline double wrapAngle(double rad) noexcept {
  if (!std::isfinite(rad)) return 0.0;
  const double turns = std::floor(rad / kTwoPi + 0.5);
  double a = rad - kTwoPi * turns;
  if (a > kPi) {
    a -= kTwoPi;
  } else if (a <= -kPi) {
    a += kTwoPi;
  }
  if (a == 0.0 && rad < 0.0) a = -0.0;
  return a;
}

namespace detail {

// 弧度 -> ADR-009 角度单位（与 quantizeAngle 同源，避免两处实现漂移）。
inline uint16_t angleUnitsFromRadians(double radians) noexcept {
  const double wrapped = wrapAngle(radians);
  const int64_t units = static_cast<int64_t>(std::floor(wrapped / kTwoPi * 65536.0 + 0.5));
  int64_t a = units % 65536;
  if (a < 0) a += 65536;
  return static_cast<uint16_t>(a);
}

}  // namespace detail

inline Vec3 forwardFromYaw(double yaw) noexcept {
  const uint16_t units = detail::angleUnitsFromRadians(yaw);
  return Vec3{sinUnits(units), 0.0, cosUnits(units)};
}

inline Vec3 rightFromYaw(double yaw) noexcept {
  const uint16_t units = detail::angleUnitsFromRadians(yaw);
  return Vec3{cosUnits(units), 0.0, -sinUnits(units)};
}

inline Vec3 yawPitchToDirection(double yaw, double pitch) noexcept {
  const uint16_t yawUnits = detail::angleUnitsFromRadians(yaw);
  const uint16_t pitchUnits = detail::angleUnitsFromRadians(pitch);
  const double cosPitch = cosUnits(pitchUnits);
  return Vec3{cosPitch * sinUnits(yawUnits), sinUnits(pitchUnits), cosPitch * cosUnits(yawUnits)};
}

inline bool aabbOverlaps(const Aabb& a, const Aabb& b) noexcept {
  return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y &&
         a.min.z <= b.max.z && b.min.z <= a.max.z;
}

inline bool aabbContainsPoint(const Aabb& a, const Vec3& p) noexcept {
  return p.x >= a.min.x && p.x <= a.max.x && p.y >= a.min.y && p.y <= a.max.y && p.z >= a.min.z &&
         p.z <= a.max.z;
}

}  // namespace ac
