#include "combat/raycast.hpp"

#include <cmath>

namespace ac::combat {
namespace {

double sphereT(double ox, double oy, double oz, double dx, double dy, double dz, double cx, double cy, double cz,
               double radius, double maxDist) noexcept {
  const double mx = ox - cx;
  const double my = oy - cy;
  const double mz = oz - cz;
  const double b = mx * dx + my * dy + mz * dz;
  const double c = mx * mx + my * my + mz * mz - radius * radius;
  if (c > 0.0 && b > 0.0) return -1.0;
  const double disc = b * b - c;
  if (disc < 0.0) return -1.0;
  double t = -b - std::sqrt(disc);
  if (t < 0.0) t = 0.0;
  return t <= maxDist ? t : -1.0;
}

void writeHit(RayHit& out, double t, double ox, double oy, double oz, double dx, double dy, double dz, double nx,
              double ny, double nz) noexcept {
  out.hit = true;
  out.t = t;
  out.x = ox + dx * t;
  out.y = oy + dy * t;
  out.z = oz + dz * t;
  out.nx = nx;
  out.ny = ny;
  out.nz = nz;
}

// v1 用模块级 slabTMin/slabTMax/slabAxis/slabSign 暂存；这里改成显式状态，逐轴运算顺序不变。
struct SlabState {
  double tMin = 0.0;
  double tMax = 0.0;
  int32_t axis = 0;
  double sign = 1.0;
};

bool updateSlab(SlabState& state, int32_t axis, double origin, double dir, double low, double high) noexcept {
  if (std::fabs(dir) < kRayEpsilon) return origin >= low && origin <= high;
  const double inverse = 1.0 / dir;
  double near = (low - origin) * inverse;
  double far = (high - origin) * inverse;
  double nearSign = -1.0;
  if (near > far) {
    const double swap = near;
    near = far;
    far = swap;
    nearSign = 1.0;
  }
  if (near > state.tMin) {
    state.tMin = near;
    state.axis = axis;
    state.sign = nearSign;
  }
  if (far < state.tMax) state.tMax = far;
  return state.tMin <= state.tMax;
}

}  // namespace

void clearRayHit(RayHit& out) noexcept {
  out.hit = false;
  out.t = 0.0;
  out.x = 0.0;
  out.y = 0.0;
  out.z = 0.0;
  out.nx = 0.0;
  out.ny = 0.0;
  out.nz = 0.0;
}

RayHit rayVsSphere(double ox, double oy, double oz, double dx, double dy, double dz, double cx, double cy,
                   double cz, double radius, double maxDist, RayHit& out) noexcept {
  clearRayHit(out);
  const double t = sphereT(ox, oy, oz, dx, dy, dz, cx, cy, cz, radius, maxDist);
  if (t < 0.0) return out;
  const double inverse = radius > kRayEpsilon ? 1.0 / radius : 0.0;
  writeHit(out, t, ox, oy, oz, dx, dy, dz, (ox + dx * t - cx) * inverse, (oy + dy * t - cy) * inverse,
           (oz + dz * t - cz) * inverse);
  return out;
}

RayHit rayVsAabb(double ox, double oy, double oz, double dx, double dy, double dz, double minX, double minY,
                 double minZ, double maxX, double maxY, double maxZ, double maxDist, RayHit& out) noexcept {
  clearRayHit(out);
  SlabState state;
  state.tMin = 0.0;
  state.tMax = maxDist;
  state.axis = 0;
  state.sign = 1.0;
  if (!updateSlab(state, 0, ox, dx, minX, maxX) || !updateSlab(state, 1, oy, dy, minY, maxY) ||
      !updateSlab(state, 2, oz, dz, minZ, maxZ)) {
    return out;
  }
  const double nx = state.axis == 0 ? state.sign : 0.0;
  const double ny = state.axis == 1 ? state.sign : 0.0;
  const double nz = state.axis == 2 ? state.sign : 0.0;
  writeHit(out, state.tMin, ox, oy, oz, dx, dy, dz, nx, ny, nz);
  return out;
}

RayHit rayVsCapsule(double ox, double oy, double oz, double dx, double dy, double dz, double ax, double ay,
                    double az, double bx, double by, double bz, double radius, double maxDist,
                    RayHit& out) noexcept {
  clearRayHit(out);
  const double abx = bx - ax;
  const double aby = by - ay;
  const double abz = bz - az;
  const double ab2 = abx * abx + aby * aby + abz * abz;
  double bestT = 1.0e308 * 1.0e308;  // +inf
  double bestK = 0.0;

  if (ab2 > kRayEpsilon) {
    const double aox = ox - ax;
    const double aoy = oy - ay;
    const double aoz = oz - az;
    const double abd = dx * abx + dy * aby + dz * abz;
    const double aod = aox * abx + aoy * aby + aoz * abz;
    const double a = ab2 * (dx * dx + dy * dy + dz * dz) - abd * abd;
    const double b = 2.0 * (ab2 * (dx * aox + dy * aoy + dz * aoz) - abd * aod);
    const double c = ab2 * (aox * aox + aoy * aoy + aoz * aoz) - aod * aod - radius * radius * ab2;
    if (std::fabs(a) > kRayEpsilon) {
      const double disc = b * b - 4.0 * a * c;
      if (disc >= 0.0) {
        const double root = std::sqrt(disc);
        const double t0 = (-b - root) / (2.0 * a);
        const double t1 = (-b + root) / (2.0 * a);
        for (int32_t i = 0; i < 2; ++i) {
          const double t = i == 0 ? t0 : t1;
          if (t < 0.0 || t > maxDist || t >= bestT) continue;
          const double k = (aod + t * abd) / ab2;
          if (k < 0.0 || k > 1.0) continue;
          bestT = t;
          bestK = k;
        }
      }
    }
  }

  const double capA = sphereT(ox, oy, oz, dx, dy, dz, ax, ay, az, radius, maxDist);
  if (capA >= 0.0 && capA < bestT) {
    bestT = capA;
    bestK = 0.0;
  }
  const double capB = sphereT(ox, oy, oz, dx, dy, dz, bx, by, bz, radius, maxDist);
  if (capB >= 0.0 && capB < bestT) {
    bestT = capB;
    bestK = 1.0;
  }

  if (!(bestT < 1.0e308 * 1.0e308)) return out;  // !Number.isFinite
  const double cx = ax + abx * bestK;
  const double cy = ay + aby * bestK;
  const double cz = az + abz * bestK;
  const double inverse = radius > kRayEpsilon ? 1.0 / radius : 0.0;
  writeHit(out, bestT, ox, oy, oz, dx, dy, dz, (ox + dx * bestT - cx) * inverse,
           (oy + dy * bestT - cy) * inverse, (oz + dz * bestT - cz) * inverse);
  return out;
}

}  // namespace ac::combat
