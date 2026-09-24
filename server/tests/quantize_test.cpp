// S02 §5.3：量化锚点、half-up 舍入、回绕与越界夹取；§5.4：三张表的 CRC32C 门禁。
#include "tiny_test.hpp"

#include "core/hash.hpp"
#include "core/quantize.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// 以 int32 小端字节流复算 CRC32C（与共享 JSON 的字节口径一致，不依赖本机端序）。
uint32_t crcOfInt32Array(const int32_t* values, std::size_t count) {
  std::vector<unsigned char> bytes(count * 4);
  for (std::size_t i = 0; i < count; ++i) {
    const uint32_t v = static_cast<uint32_t>(values[i]);
    bytes[i * 4 + 0] = static_cast<unsigned char>(v & 0xFFu);
    bytes[i * 4 + 1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    bytes[i * 4 + 2] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    bytes[i * 4 + 3] = static_cast<unsigned char>((v >> 24) & 0xFFu);
  }
  return ac::crc32c(bytes.data(), bytes.size());
}

}  // namespace

AC_TEST(quantize_position_anchors) {
  AC_CHECK_EQ(ac::quantizePosition(1.2345), 123);
  AC_CHECK_EQ(ac::quantizePosition(-0.005), 0);
  AC_CHECK_EQ(ac::quantizePosition(500.0), 32767);
  AC_CHECK_EQ(ac::quantizePosition(-500.0), -32768);
  AC_CHECK_EQ(ac::dequantizePosition(static_cast<int16_t>(123)), 1.23);
}

AC_TEST(quantize_position_rounding_is_half_up) {
  // floor(x + 0.5)：-0.005 * 100 = -0.5 -> 0（std::round 会给 -1）
  AC_CHECK_EQ(ac::quantizePosition(-0.005), 0);
  AC_CHECK_EQ(ac::quantizePosition(0.005), 1);
  AC_CHECK_EQ(ac::quantizePosition(-0.004), 0);
  AC_CHECK_EQ(ac::quantizePosition(-0.006), -1);
  AC_CHECK_EQ(ac::quantizePosition(0.0149), 1);
  AC_CHECK_EQ(ac::quantizePosition(-1.0), -100);
}

AC_TEST(quantize_position_clamp_and_zero) {
  AC_CHECK_EQ(ac::quantizePosition(0.0), 0);
  AC_CHECK_EQ(ac::quantizePosition(-0.0), 0);
  AC_CHECK_EQ(ac::quantizePosition(327.674), 32767);   // 恰好上界
  AC_CHECK_EQ(ac::quantizePosition(327.675), 32767);   // 越界夹取
  AC_CHECK_EQ(ac::quantizePosition(-327.685), -32768); // 越界夹取
  AC_CHECK_EQ(ac::quantizePosition(1e9), 32767);
  AC_CHECK_EQ(ac::quantizePosition(-1e9), -32768);
}

AC_TEST(quantize_axis_anchors) {
  AC_CHECK_EQ(ac::quantizeAxis(0.5), 64);
  AC_CHECK_EQ(ac::quantizeAxis(-0.004), -1);
  AC_CHECK_EQ(ac::quantizeAxis(0.0), 0);
  AC_CHECK_EQ(ac::dequantizeAxis(static_cast<int8_t>(127)), 1.0);
  AC_CHECK_EQ(ac::dequantizeAxis(static_cast<int8_t>(-127)), -1.0);
}

AC_TEST(quantize_axis_rounding_and_clamp) {
  AC_CHECK_EQ(ac::quantizeAxis(1.0), 127);
  AC_CHECK_EQ(ac::quantizeAxis(-1.0), -127);
  AC_CHECK_EQ(ac::quantizeAxis(1.5), 127);
  AC_CHECK_EQ(ac::quantizeAxis(-1.5), -127);
  AC_CHECK_EQ(ac::quantizeAxis(-0.0), 0);
  AC_CHECK_EQ(ac::quantizeAxis(0.0039), 0);   // 0.495 -> 0
  AC_CHECK_EQ(ac::quantizeAxis(0.00394), 1);  // 0.5004 -> 1
}

AC_TEST(quantize_ratio_anchors) {
  AC_CHECK_EQ(ac::quantizeRatio(0.5), 128);
  AC_CHECK_EQ(ac::quantizeRatio(0.004), 1);
  AC_CHECK_EQ(ac::quantizeRatio(0.0), 0);
  AC_CHECK_EQ(ac::dequantizeRatio(static_cast<uint8_t>(255)), 1.0);
}

AC_TEST(quantize_ratio_rounding_and_clamp) {
  AC_CHECK_EQ(ac::quantizeRatio(1.0), 255);
  AC_CHECK_EQ(ac::quantizeRatio(1.5), 255);
  AC_CHECK_EQ(ac::quantizeRatio(-0.5), 0);
  AC_CHECK_EQ(ac::quantizeRatio(1.0 / 255.0), 1);
  AC_CHECK_EQ(ac::quantizeRatio(0.001), 0);
  AC_CHECK_EQ(ac::quantizeRatio(0.0019), 0);   // 0.4845 -> 0（半点 1/510 = 0.0019608）
  AC_CHECK_EQ(ac::quantizeRatio(0.002), 1);    // 0.51 -> 1
}

AC_TEST(quantize_angle_anchors) {
  AC_CHECK_EQ(ac::quantizeAngle(1.0), 10430);
  AC_CHECK_EQ(ac::quantizeAngle(-3.5), 29030);
  AC_CHECK_EQ(ac::quantizeAngle(ac::kPi), 32768);
  AC_CHECK_EQ(ac::quantizeAngle(0.0), 0);
  AC_CHECK_NEAR(ac::dequantizeAngle(32768u), ac::kPi, 1e-12);
}

AC_TEST(quantize_angle_wrap_and_units) {
  AC_CHECK_EQ(ac::quantizeAngle(ac::kPi / 2.0), 16384);
  AC_CHECK_EQ(ac::quantizeAngle(-ac::kPi / 2.0), 49152);
  AC_CHECK_EQ(ac::quantizeAngle(ac::kPi * 2.0), 0);
  AC_CHECK_EQ(ac::quantizeAngle(ac::kPi * 3.0), 32768);
  AC_CHECK_EQ(ac::quantizeAngle(-ac::kPi), 32768);  // wrapAngle 把 -PI 折到 +PI
  AC_CHECK_EQ(ac::quantizeAngle(ac::kPi * 5.5), static_cast<uint16_t>(ac::quantizeAngle(ac::kPi * 1.5)));
  for (int i = -20; i <= 20; ++i) {
    const uint16_t units = ac::quantizeAngle(static_cast<double>(i) * 0.9);
    AC_CHECK_NEAR(ac::wrapAngle(ac::dequantizeAngle(units)), ac::dequantizeAngle(units), 0.0);
  }
}

AC_TEST(quantize_non_finite_inputs) {
  const double nan = std::nan("");
  AC_CHECK_EQ(ac::quantizePosition(nan), 0);
  AC_CHECK_EQ(ac::quantizePosition(HUGE_VAL), 0);
  AC_CHECK_EQ(ac::quantizePosition(-HUGE_VAL), 0);
  AC_CHECK_EQ(ac::quantizeAxis(nan), 0);
  AC_CHECK_EQ(ac::quantizeAxis(HUGE_VAL), 0);
  AC_CHECK_EQ(ac::quantizeRatio(nan), 0);
  AC_CHECK_EQ(ac::quantizeRatio(-HUGE_VAL), 0);
  AC_CHECK_EQ(ac::quantizeAngle(nan), 0);
  AC_CHECK_EQ(ac::quantizeAngle(HUGE_VAL), 0);
}

AC_TEST(trig_table_crcs) {
  const uint32_t sinCrc = crcOfInt32Array(ac::kSinQ30, 65536u);
  const uint32_t atanCrc = crcOfInt32Array(ac::kAtanUnits, 1025u);
  const uint32_t asinCrc = crcOfInt32Array(ac::kAsinUnits, 1025u);
  std::printf("sin crc=0x%08X atan crc=0x%08X asin crc=0x%08X\n", sinCrc, atanCrc, asinCrc);
  std::fflush(stdout);

  AC_CHECK_EQ(sinCrc, 0x8BD9F737u);
  AC_CHECK_EQ(atanCrc, 0x197C3A8Du);
  AC_CHECK_EQ(asinCrc, 0xAD2BD35Eu);
  AC_CHECK_EQ(sinCrc, ac::kSinQ30Crc32c);
  AC_CHECK_EQ(atanCrc, ac::kAtanUnitsCrc32c);
  AC_CHECK_EQ(asinCrc, ac::kAsinUnitsCrc32c);
  // 分段入口与单次入口等价
  const std::size_t half = 65536u / 2u;
  AC_CHECK_EQ(ac::crc32cExtend(ac::crc32c(ac::kSinQ30, half * 4u), ac::kSinQ30 + half, half * 4u),
              sinCrc);
  AC_CHECK_EQ(ac::crc32cExtend(0u, ac::kSinQ30, sizeof(ac::kSinQ30)), sinCrc);
}
