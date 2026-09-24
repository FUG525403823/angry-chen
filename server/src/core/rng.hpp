#pragma once
// S02 §5.2：mulberry32 与三流派生，逐位复刻 v1 packages/shared/src/rng.ts。
//
// 约束（ADR-010 §5）：模拟只允许读 ai 与 spawn；fx 仅由表现层请求消费，读取 fx 不得改变模拟状态。
// 全部运算走 uint32 回绕语义，禁止任何浮点参与状态推进。

#include <cmath>
#include <cstdint>

namespace ac {

enum class RngStream : uint32_t { kAi = 1, kSpawn = 2, kFx = 3 };

inline constexpr uint32_t kRngMulberryStep = 0x6D2B79F5u;
inline constexpr uint32_t kRngSeedMultiplier = 0x9E3779B1u;  // 2654435761
inline constexpr double kRngUnitDoubleDivisor = 4294967296.0;  // 2^32

struct Rng {
  uint32_t a;

  // v1: a = (a + 0x6D2B79F5) >>> 0; t = a; t = imul(t ^ (t >>> 15), t | 1);
  //     t ^= t + imul(t ^ (t >>> 7), t | 61); return (t ^ (t >>> 14)) >>> 0
  inline uint32_t nextU32() noexcept {
    a = a + kRngMulberryStep;
    uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
  }

  inline double nextDouble() noexcept { return nextU32() / kRngUnitDoubleDivisor; }
};

// 派生：a0 = (seed * 0x9E3779B1 + streamId) mod 2^32，streamId = 1/2/3。
inline Rng createRng(uint32_t seed, RngStream stream) noexcept {
  return Rng{(seed * kRngSeedMultiplier + static_cast<uint32_t>(stream))};
}

inline double rngRange(Rng& rng, double lo, double hi) noexcept {
  return lo + (hi - lo) * rng.nextDouble();
}

inline int32_t rngInt(Rng& rng, int32_t loInclusive, int32_t hiInclusive) noexcept {
  if (hiInclusive < loInclusive) return loInclusive;
  const double span = static_cast<double>(hiInclusive) - static_cast<double>(loInclusive) + 1.0;
  return loInclusive + static_cast<int32_t>(std::floor(rng.nextDouble() * span));
}

}  // namespace ac
