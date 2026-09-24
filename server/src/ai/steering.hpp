#pragma once
// S09 §5.2/§5.7：转向原语（零分配；只用 + - * / 与 sqrt，禁用超越函数）。
#include "config/player.hpp"

namespace ac::ai {

// v1 steering.ts 的零长度阈值（三处共用同一字面量）。
inline constexpr double kSteeringZeroLengthM = 1e-9;

// ADR-010 §2：绝对值走位运算，不引浮点库调用；AI 各 TU 共用这一份。
inline constexpr double absoluteValue(double value) noexcept { return value < 0.0 ? -value : value; }

struct SteeringOut {
  double x = 0.0;
  double z = 0.0;
};

inline SteeringOut createSteeringOut() noexcept { return SteeringOut{0.0, 0.0}; }

// v1 steering.ts：长度 <= 1e-9 输出零向量，否则把方向缩放到 speed（speed 可为负 = 后退）。
SteeringOut& normalize(SteeringOut& out, double speed) noexcept;

SteeringOut& seek(SteeringOut& out, double selfX, double selfZ, double targetX, double targetZ,
                  double speed) noexcept;

SteeringOut& arrive(SteeringOut& out, double selfX, double selfZ, double targetX, double targetZ,
                    double speed, double slowRadiusM) noexcept;

// 分离力：count 个 (xs[i], zs[i]) 邻居，半径内按 (radiusM - distance) / radiusM / distanceSq 加权。
SteeringOut& separation(SteeringOut& out, double selfX, double selfZ, const double* xs,
                        const double* zs, int32_t count, double radiusM, double strength) noexcept;

// 边界与谷仓回避：在 dirX/dirZ 上叠加推离分量，最后缩放到 |dir|（保持原速度大小）。
SteeringOut& obstacleAvoid(SteeringOut& out, double selfX, double selfZ, double dirX, double dirZ,
                           const ac::config::ArenaConfig& arena, double radiusM) noexcept;

}  // namespace ac::ai
