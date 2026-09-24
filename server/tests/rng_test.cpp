// S02 §5.2：mulberry32 三流位级锚点。期望值来自 v1 packages/shared/src/rng.ts 的实测输出（§7 DoD）。
#include "tiny_test.hpp"

#include "core/rng.hpp"

#include <cstdint>
#include <cstring>

namespace {

using ac::Rng;
using ac::RngStream;

// 位级比较：double 的位模式，任何容差都可能掩盖漂移。
uint64_t bitsOf(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

}  // namespace

AC_TEST(rng_derived_seeds) {
  AC_CHECK_EQ(ac::createRng(1234u, RngStream::kAi).a, 0xA7689733u);
  AC_CHECK_EQ(ac::createRng(1234u, RngStream::kSpawn).a, 0xA7689734u);
  AC_CHECK_EQ(ac::createRng(1234u, RngStream::kFx).a, 0xA7689735u);
  AC_CHECK_EQ(static_cast<uint32_t>(RngStream::kAi), 1u);
  AC_CHECK_EQ(static_cast<uint32_t>(RngStream::kSpawn), 2u);
  AC_CHECK_EQ(static_cast<uint32_t>(RngStream::kFx), 3u);
  // 派生在 2^32 上回绕
  AC_CHECK_EQ(ac::createRng(0xFFFFFFFFu, RngStream::kFx).a, (0xFFFFFFFFu * 0x9E3779B1u + 3u));
}

AC_TEST(rng_ai_stream_bits) {
  Rng rng = ac::createRng(1234u, RngStream::kAi);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FE136AC1DA00000ULL);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FE621AEFEC00000ULL);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FEA98B75AC00000ULL);
}

AC_TEST(rng_spawn_stream_bits) {
  Rng rng = ac::createRng(1234u, RngStream::kSpawn);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FE034F2D0E00000ULL);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FE9A52769400000ULL);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FC03B8F8F000000ULL);
}

AC_TEST(rng_fx_stream_bits) {
  Rng rng = ac::createRng(1234u, RngStream::kFx);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FE6611F0D400000ULL);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FE348F073800000ULL);
  AC_CHECK_EQ(bitsOf(rng.nextDouble()), 0x3FC777A9BA000000ULL);
}

AC_TEST(rng_int_and_range_anchors) {
  Rng rng = ac::createRng(1234u, RngStream::kAi);
  const int32_t expected[6] = {53, 69, 83, 62, 24, 82};
  for (int i = 0; i < 6; ++i) {
    AC_CHECK_EQ(ac::rngInt(rng, 0, 99), expected[i]);
  }

  Rng ranged = ac::createRng(99u, RngStream::kSpawn);
  Rng probe = ac::createRng(99u, RngStream::kSpawn);
  for (int i = 0; i < 4; ++i) {
    const double expectedValue = -2.0 + 7.0 * probe.nextDouble();  // lo + (hi - lo) * d
    AC_CHECK_NEAR(ac::rngRange(ranged, -2.0, 5.0), expectedValue, 0.0);
  }

  Rng inverted = ac::createRng(5u, RngStream::kFx);
  AC_CHECK_EQ(ac::rngInt(inverted, 10, 9), 10);  // 上界小于下界 -> 返回下界且不消耗随机数
  AC_CHECK_EQ(bitsOf(inverted.nextDouble()), bitsOf(ac::createRng(5u, RngStream::kFx).nextDouble()));
}

AC_TEST(rng_fx_does_not_disturb_sim_streams) {
  Rng fx = ac::createRng(1234u, RngStream::kFx);
  for (int i = 0; i < 6; ++i) {
    (void)fx.nextDouble();
  }
  Rng ai = ac::createRng(1234u, RngStream::kAi);
  Rng spawn = ac::createRng(1234u, RngStream::kSpawn);
  AC_CHECK_EQ(bitsOf(ai.nextDouble()), 0x3FE136AC1DA00000ULL);
  AC_CHECK_EQ(bitsOf(spawn.nextDouble()), 0x3FE034F2D0E00000ULL);

  // nextDouble 就是 nextU32 / 2^32，同一状态两条路径逐位一致
  Rng byDouble = ac::createRng(7u, RngStream::kAi);
  Rng byU32 = ac::createRng(7u, RngStream::kAi);
  AC_CHECK_NEAR(byDouble.nextDouble(), byU32.nextU32() / 4294967296.0, 0.0);
}
