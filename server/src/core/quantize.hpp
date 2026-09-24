#pragma once
// S02 §5.3：ADR-009 冻结的量化/反量化（只做实现不做设计）。
//
// 舍入一律 floor(x + 0.5)：负数半值语义与标准库的 round 系列不同（floor(-0.5 + 0.5) = 0，round(-0.5) = -1）。
// 非有限输入（NaN/Inf）按 wrapAngle 的同一条规则归 0，避免整数转换的未定义行为。

#include <cmath>
#include <cstdint>

#include "math.hpp"

namespace ac {

inline int16_t quantizePosition(double meters) noexcept {
  if (!std::isfinite(meters)) return 0;
  const double cm = std::floor(meters * 100.0 + 0.5);
  return static_cast<int16_t>(clamp(cm, -32768.0, 32767.0));
}

inline double dequantizePosition(int16_t cm) noexcept { return cm / 100.0; }

inline uint16_t quantizeAngle(double radians) noexcept { return detail::angleUnitsFromRadians(radians); }

inline double dequantizeAngle(uint16_t units) noexcept { return wrapAngle(radiansFromUnits(units)); }

inline int8_t quantizeAxis(double axis) noexcept {
  if (!std::isfinite(axis)) return 0;
  const double steps = std::floor(axis * 127.0 + 0.5);
  return static_cast<int8_t>(clamp(steps, -127.0, 127.0));
}

inline double dequantizeAxis(int8_t v) noexcept { return v / 127.0; }

inline uint8_t quantizeRatio(double ratio) noexcept {
  if (!std::isfinite(ratio)) return 0;
  const double steps = std::floor(ratio * 255.0 + 0.5);
  return static_cast<uint8_t>(clamp(steps, 0.0, 255.0));
}

inline double dequantizeRatio(uint8_t v) noexcept { return v / 255.0; }

}  // namespace ac
