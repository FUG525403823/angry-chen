// S02 §5.1 数学锚点、朝向基约定与 wrapAngle 边界；§5.4 查表精度上界。
// 测试里允许用 std::sin/cos/atan2/asin 当预言机（禁用范围只覆盖 server/src/core）。
#include "tiny_test.hpp"

#include "core/math.hpp"

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace {

// 向量相等（容差 2^-30 量级），用于查表驱动的朝向向量。
bool nearVec(const ac::Vec3& a, const ac::Vec3& b, double eps) {
  return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

ac::Vec3 cross(const ac::Vec3& a, const ac::Vec3& b) {
  return ac::Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// angleUnitsFromRatio 相对 asin 预言机的单位误差（环形距离）。
int ratioDiffUnits(double ratio) {
  const double oracle = std::asin(ratio) / (2.0 * ac::kPi) * 65536.0;
  const int expected = static_cast<int>(std::floor(oracle + 0.5)) & 0xFFFF;
  int diff = static_cast<int>(ac::angleUnitsFromRatio(ratio)) - expected;
  if (diff < 0) diff = -diff;
  return diff > 32768 ? 65536 - diff : diff;
}

}  // namespace

AC_TEST(math_vec3_and_aabb_layout) {
  AC_CHECK_EQ(sizeof(ac::Vec3), std::size_t{24});
  AC_CHECK_EQ(alignof(ac::Vec3), std::size_t{8});
  AC_CHECK_EQ(sizeof(ac::Aabb), std::size_t{48});
  AC_CHECK_EQ(alignof(ac::Aabb), std::size_t{8});
  AC_CHECK(std::is_standard_layout_v<ac::Vec3>);
  AC_CHECK(std::is_trivially_copyable_v<ac::Vec3>);
  AC_CHECK(std::is_standard_layout_v<ac::Aabb>);
}

AC_TEST(math_vector_ops_and_normalize) {
  const ac::Vec3 base{1.0, 2.0, 3.0};
  const ac::Vec3 dir{0.0, 0.0, 1.0};
  const ac::Vec3 moved = ac::addScaled(base, dir, 2.5);
  AC_CHECK_NEAR(moved.x, 1.0, 0.0);
  AC_CHECK_NEAR(moved.y, 2.0, 0.0);
  AC_CHECK_NEAR(moved.z, 5.5, 0.0);

  const ac::Vec3 v{3.0, 4.0, 12.0};
  AC_CHECK_NEAR(ac::lengthSq(v), 169.0, 0.0);
  AC_CHECK_NEAR(ac::length(v), 13.0, 0.0);

  const ac::Vec3 unit = ac::normalize(v);
  AC_CHECK_NEAR(ac::length(unit), 1.0, 1e-15);
  AC_CHECK_NEAR(unit.x, 3.0 / 13.0, 0.0);

  // 零向量归一返回 (0,0,0)，不产生 NaN
  const ac::Vec3 zero = ac::normalize(ac::Vec3{0.0, 0.0, 0.0});
  AC_CHECK_EQ(zero.x, 0.0);
  AC_CHECK_EQ(zero.y, 0.0);
  AC_CHECK_EQ(zero.z, 0.0);
  AC_CHECK(std::isfinite(zero.x) && std::isfinite(zero.y) && std::isfinite(zero.z));

  // 负零向量同样返回零向量，不产生 NaN
  const ac::Vec3 negZero = ac::normalize(ac::Vec3{-0.0, -0.0, -0.0});
  AC_CHECK_EQ(ac::length(negZero), 0.0);

  const ac::Vec3 unitY = ac::normalize(ac::Vec3{0.0, 1.0, 0.0});
  AC_CHECK_EQ(unitY.y, 1.0);
  AC_CHECK_EQ(unitY.x, 0.0);
}

AC_TEST(math_clamp_helpers) {
  AC_CHECK_EQ(ac::clamp(5.0, 0.0, 1.0), 1.0);
  AC_CHECK_EQ(ac::clamp(-5.0, 0.0, 1.0), 0.0);
  AC_CHECK_EQ(ac::clamp(0.25, 0.0, 1.0), 0.25);
  AC_CHECK_EQ(ac::clamp01(2.0), 1.0);
  AC_CHECK_EQ(ac::clamp01(-2.0), 0.0);
  AC_CHECK_EQ(ac::clamp01(0.5), 0.5);
}

AC_TEST(math_wrap_angle_range) {
  AC_CHECK_EQ(ac::wrapAngle(0.0), 0.0);
  AC_CHECK_NEAR(ac::wrapAngle(ac::kPi), ac::kPi, 0.0);
  AC_CHECK_NEAR(ac::wrapAngle(-ac::kPi), ac::kPi, 0.0);  // §5.1 冻结 (-PI, PI]
  AC_CHECK_NEAR(ac::wrapAngle(2.0 * ac::kPi), 0.0, 0.0);
  AC_CHECK_NEAR(ac::wrapAngle(-2.0 * ac::kPi), 0.0, 0.0);
  AC_CHECK_NEAR(ac::wrapAngle(3.0 * ac::kPi), ac::kPi, 1e-12);
  AC_CHECK_NEAR(ac::wrapAngle(-3.0 * ac::kPi), ac::kPi, 1e-12);
  AC_CHECK_NEAR(ac::wrapAngle(ac::kPi / 2.0), ac::kPi / 2.0, 0.0);
  AC_CHECK_NEAR(ac::wrapAngle(-ac::kPi / 2.0), -ac::kPi / 2.0, 0.0);
  // 与 v1 的 JS `%` 逐位一致的符号位：负的 2*PI 整数倍返回 -0，正的返回 +0
  AC_CHECK(std::signbit(ac::wrapAngle(-2.0 * ac::kPi)));
  AC_CHECK(!std::signbit(ac::wrapAngle(2.0 * ac::kPi)));
  AC_CHECK(std::signbit(ac::wrapAngle(-4.0 * ac::kPi)));
  AC_CHECK_EQ(ac::wrapAngle(std::nan("")), 0.0);
  AC_CHECK_EQ(ac::wrapAngle(HUGE_VAL), 0.0);
  AC_CHECK_EQ(ac::wrapAngle(-HUGE_VAL), 0.0);

  for (int i = -40; i <= 40; ++i) {
    const double wrapped = ac::wrapAngle(static_cast<double>(i) * 0.37);
    AC_CHECK(wrapped > -ac::kPi - 1e-15 && wrapped <= ac::kPi + 1e-15);
  }
}

AC_TEST(math_forward_right_basis) {
  const double eps = 2e-9;
  AC_CHECK(nearVec(ac::forwardFromYaw(0.0), ac::Vec3{0.0, 0.0, 1.0}, 0.0));
  AC_CHECK(nearVec(ac::rightFromYaw(0.0), ac::Vec3{1.0, 0.0, 0.0}, 0.0));
  AC_CHECK(nearVec(ac::forwardFromYaw(ac::kPi / 2.0), ac::Vec3{1.0, 0.0, 0.0}, eps));
  AC_CHECK(nearVec(ac::rightFromYaw(ac::kPi / 2.0), ac::Vec3{0.0, 0.0, -1.0}, eps));

  // 镜像基约定：forward x right = +Y（翻转 rightFromYaw 符号会变成 -Y）
  const double yaws[5] = {0.0, ac::kPi / 2.0, ac::kPi, -ac::kPi / 2.0, 0.7};
  for (const double yaw : yaws) {
    const ac::Vec3 forward = ac::forwardFromYaw(yaw);
    const ac::Vec3 right = ac::rightFromYaw(yaw);
    AC_CHECK_EQ(forward.y, 0.0);
    AC_CHECK_EQ(right.y, 0.0);
    AC_CHECK(nearVec(cross(forward, right), ac::Vec3{0.0, 1.0, 0.0}, eps));
    AC_CHECK(nearVec(ac::normalize(forward), forward, eps));
  }
}

AC_TEST(math_yaw_pitch_direction) {
  const double eps = 2e-9;
  AC_CHECK(nearVec(ac::yawPitchToDirection(0.0, 0.0), ac::Vec3{0.0, 0.0, 1.0}, 0.0));
  AC_CHECK(nearVec(ac::yawPitchToDirection(0.0, ac::kPi / 2.0), ac::Vec3{0.0, 1.0, 0.0}, eps));
  AC_CHECK(nearVec(ac::yawPitchToDirection(ac::kPi / 2.0, 0.0), ac::Vec3{1.0, 0.0, 0.0}, eps));
  AC_CHECK_NEAR(ac::length(ac::yawPitchToDirection(0.7, -0.3)), 1.0, 4e-9);
}

AC_TEST(math_aabb_overlaps_and_contains) {
  const ac::Aabb a{ac::Vec3{0.0, 0.0, 0.0}, ac::Vec3{1.0, 1.0, 1.0}};
  const ac::Aabb touching{ac::Vec3{1.0, 1.0, 1.0}, ac::Vec3{2.0, 2.0, 2.0}};
  const ac::Aabb apart{ac::Vec3{1.001, 0.0, 0.0}, ac::Vec3{2.0, 1.0, 1.0}};
  AC_CHECK(ac::aabbOverlaps(a, a));
  AC_CHECK(ac::aabbOverlaps(a, touching));  // 闭区间：共面算重叠
  AC_CHECK(!ac::aabbOverlaps(a, apart));
  AC_CHECK(ac::aabbContainsPoint(a, ac::Vec3{0.0, 1.0, 0.5}));
  AC_CHECK(ac::aabbContainsPoint(a, ac::Vec3{0.5, 0.5, 0.5}));
  AC_CHECK(!ac::aabbContainsPoint(a, ac::Vec3{0.5, 1.0001, 0.5}));
}

AC_TEST(math_table_precision_bound) {
  const double bound = 1.0 / 1073741824.0;  // 2^-30：表幅值量化步长
  int checked = 0;
  for (uint32_t units = 0; units < 65536u; units += 997u) {
    const uint16_t u = static_cast<uint16_t>(units);
    const double radians = ac::radiansFromUnits(u);
    AC_CHECK(std::fabs(ac::sinUnits(u) - std::sin(radians)) <= bound);
    AC_CHECK(std::fabs(ac::cosUnits(u) - std::cos(radians)) <= bound);
    checked += 1;
  }
  AC_CHECK(checked >= 65);
}

// ---------- §5.4 共享查表入口 ----------

AC_TEST(trig_sin_cos_units) {
  AC_CHECK_EQ(ac::kSinQ30[0], 0);
  AC_CHECK_EQ(ac::kSinQ30[8192], 759250125);
  AC_CHECK_EQ(ac::kSinQ30[16384], 1073741824);
  AC_CHECK_EQ(ac::kSinQ30[32768], 0);
  AC_CHECK_EQ(ac::kSinQ30[49152], -1073741824);
  AC_CHECK_EQ(ac::sinUnits(0), 0.0);
  AC_CHECK_EQ(ac::cosUnits(0), 1.0);
  AC_CHECK_NEAR(ac::sinUnits(16384), 1.0, 0.0);
  AC_CHECK_NEAR(ac::cosUnits(16384), 0.0, 0.0);
  AC_CHECK_NEAR(ac::radiansFromUnits(16384), ac::kPi / 2.0, 1e-12);

  for (uint32_t units = 0; units < 65536u; units += 4099u) {
    const uint16_t u = static_cast<uint16_t>(units);
    const uint16_t shifted = static_cast<uint16_t>((units + 16384u) & 0xFFFFu);
    AC_CHECK_EQ(ac::cosUnits(u), ac::sinUnits(shifted));
  }
}

AC_TEST(trig_angle_units_from_vector) {
  AC_CHECK_EQ(ac::kAtanUnits[0], 0);
  AC_CHECK_EQ(ac::kAtanUnits[512], 4836);
  AC_CHECK_EQ(ac::kAtanUnits[1024], 8192);
  AC_CHECK_EQ(ac::angleUnitsFromVector(0.0, 1.0), 0u);
  AC_CHECK_EQ(ac::angleUnitsFromVector(1.0, 0.0), 16384u);
  AC_CHECK_EQ(ac::angleUnitsFromVector(0.0, -1.0), 32768u);
  AC_CHECK_EQ(ac::angleUnitsFromVector(-1.0, 0.0), 49152u);
  AC_CHECK_EQ(ac::angleUnitsFromVector(1.0, 1.0), 8192u);
  AC_CHECK_EQ(ac::angleUnitsFromVector(0.0, 0.0), 0u);
  AC_CHECK_EQ(ac::angleUnitsFromVector(std::nan(""), 1.0), 0u);

  // 与 atan2 预言机比对：atan 表最近点，误差上界 6 单位（§5.4）
  const double samples[6][2] = {{0.3, 0.7}, {-0.9, 0.2}, {1.0, 1.0}, {-0.4, -0.6}, {0.05, 0.99}, {-1.0, 0.0}};
  int worst = 0;
  for (const auto& sample : samples) {
    const double oracle = std::atan2(sample[0], sample[1]) / (2.0 * ac::kPi) * 65536.0;
    const int expected = static_cast<int>(std::floor(oracle + 0.5)) & 0xFFFF;
    int actual = static_cast<int>(ac::angleUnitsFromVector(sample[0], sample[1]));
    int diff = actual - expected;
    if (diff < 0) diff = -diff;
    if (diff > 32768) diff = 65536 - diff;
    AC_CHECK(diff <= 6);
    if (diff > worst) worst = diff;
  }
  AC_CHECK(worst <= 6);
}

AC_TEST(trig_angle_units_from_ratio) {
  AC_CHECK_EQ(ac::kAsinUnits[0], 0);
  AC_CHECK_EQ(ac::kAsinUnits[512], 5461);
  AC_CHECK_EQ(ac::kAsinUnits[1024], 16384);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(0.0), 0u);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(0.5), 5461u);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(1.0), 16384u);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(-0.5), 60075u);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(-1.0), 49152u);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(2.0), 16384u);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(-2.0), 49152u);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(std::nan("")), 0u);
  AC_CHECK_EQ(ac::angleUnitsFromRatio(0.25), static_cast<uint16_t>(ac::kAsinUnits[256]));

  // 实测误差带（最近点 + 1025 点表；asin 在 |r| -> 1 处导数发散，表点之间误差随之放大）：
  //   |r| <= 0.5   <= 6 单位（§5.4 说的「同量级」只在这个区间成立）
  //   |r| <= 0.9   <= 12 ；|r| <= 0.99 <= 36 ；|r| <= 0.999 <= 104 ；|r| < 1 最坏 326（r = 0.999512）
  // 要把 |r| -> 1 收紧，必须先按 §8 改契约（2049 点 + 两侧同一插值规则），所以这里把实测值钉成文档。
  const double bandLow[5] = {0.0, 0.5, 0.9, 0.99, 0.999};
  const double bandHigh[5] = {0.5, 0.9, 0.99, 0.999, 1.0};
  const int bandBound[5] = {6, 12, 36, 104, 326};
  for (int band = 0; band < 5; ++band) {
    for (double ratio = bandLow[band]; ratio < bandHigh[band]; ratio += 0.0002) {
      AC_CHECK(ratioDiffUnits(ratio) <= bandBound[band]);
    }
  }
  AC_CHECK_EQ(ratioDiffUnits(0.9995), 131);
  AC_CHECK_EQ(ratioDiffUnits(0.999512), 326);
  AC_CHECK_EQ(ratioDiffUnits(0.9999), 148);
  AC_CHECK_EQ(ratioDiffUnits(1.0), 0);

  // 网格点（i/1024）与 asin 预言机逐点一致（点之间是最近点近似，越接近 +-1 误差越大，不在上界内）
  for (int i = 1; i <= 900; i += 97) {
    const double ratio = static_cast<double>(i) / 1024.0;
    const double oracle = std::asin(ratio) / (2.0 * ac::kPi) * 65536.0;
    const int expected = static_cast<int>(std::floor(oracle + 0.5));
    const int actual = static_cast<int>(ac::angleUnitsFromRatio(ratio));
    AC_CHECK(actual - expected <= 1 && expected - actual <= 1);
  }
}
