#pragma once
// S08 §5.3/§9：三个求交函数（球 / AABB slab / 胶囊），零分配、结果写入调用方结构。
// 逐位对齐 v1 combat/raycast.ts（epsilon = 1e-12，逐轴顺序固定）。
#include <cstdint>

namespace ac::combat {

inline constexpr double kRayEpsilon = 1e-12;

struct RayHit {
  bool hit = false;
  double t = 0.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double nx = 0.0;
  double ny = 0.0;
  double nz = 0.0;
};

void clearRayHit(RayHit& out) noexcept;

// 方向向量必须是单位向量。maxDist 是允许的最大参数（含）。
RayHit rayVsSphere(double ox, double oy, double oz, double dx, double dy, double dz, double cx, double cy,
                   double cz, double radius, double maxDist, RayHit& out) noexcept;
RayHit rayVsAabb(double ox, double oy, double oz, double dx, double dy, double dz, double minX, double minY,
                 double minZ, double maxX, double maxY, double maxZ, double maxDist, RayHit& out) noexcept;
RayHit rayVsCapsule(double ox, double oy, double oz, double dx, double dy, double dz, double ax, double ay,
                    double az, double bx, double by, double bz, double radius, double maxDist,
                    RayHit& out) noexcept;

}  // namespace ac::combat
