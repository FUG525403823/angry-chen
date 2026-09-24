#include "ai/steering.hpp"

#include <cmath>

namespace ac::ai {

SteeringOut& normalize(SteeringOut& out, double speed) noexcept {
  const double length = std::sqrt(out.x * out.x + out.z * out.z);
  if (length <= kSteeringZeroLengthM) {
    out.x = 0.0;
    out.z = 0.0;
    return out;
  }
  const double scale = speed / length;
  out.x *= scale;
  out.z *= scale;
  return out;
}

SteeringOut& seek(SteeringOut& out, double selfX, double selfZ, double targetX, double targetZ,
                  double speed) noexcept {
  out.x = targetX - selfX;
  out.z = targetZ - selfZ;
  return normalize(out, speed);
}

SteeringOut& arrive(SteeringOut& out, double selfX, double selfZ, double targetX, double targetZ,
                    double speed, double slowRadiusM) noexcept {
  const double dx = targetX - selfX;
  const double dz = targetZ - selfZ;
  const double distance = std::sqrt(dx * dx + dz * dz);
  if (distance <= kSteeringZeroLengthM) {
    out.x = 0.0;
    out.z = 0.0;
    return out;
  }
  const double desired = distance < slowRadiusM ? speed * (distance / slowRadiusM) : speed;
  out.x = dx;
  out.z = dz;
  return normalize(out, desired);
}

SteeringOut& separation(SteeringOut& out, double selfX, double selfZ, const double* xs,
                        const double* zs, int32_t count, double radiusM, double strength) noexcept {
  out.x = 0.0;
  out.z = 0.0;
  if (count <= 0 || radiusM <= 0.0) return out;
  const double radiusSq = radiusM * radiusM;
  for (int32_t i = 0; i < count; ++i) {
    const double dx = selfX - xs[i];
    const double dz = selfZ - zs[i];
    const double distanceSq = dx * dx + dz * dz;
    if (distanceSq >= radiusSq) continue;
    if (distanceSq < kSteeringZeroLengthM) {
      out.x += i % 2 == 0 ? 1.0 : -1.0;
      out.z += i % 2 == 0 ? -1.0 : 1.0;
      continue;
    }
    const double weight = (radiusM - std::sqrt(distanceSq)) / radiusM / distanceSq;
    out.x += dx * weight;
    out.z += dz * weight;
  }
  return normalize(out, strength);
}

SteeringOut& obstacleAvoid(SteeringOut& out, double selfX, double selfZ, double dirX, double dirZ,
                           const ac::config::ArenaConfig& arena, double radiusM) noexcept {
  const double limit = arena.halfSizeMeters - arena.fenceHalfThicknessMeters - radiusM;
  double steerX = 0.0;
  double steerZ = 0.0;
  if (selfX > limit) steerX -= (selfX - limit) * 2.0;
  if (selfX < -limit) steerX += (-limit - selfX) * 2.0;
  if (selfZ > limit) steerZ -= (selfZ - limit) * 2.0;
  if (selfZ < -limit) steerZ += (-limit - selfZ) * 2.0;

  const double marginM = radiusM + 1.0;
  const double minX = arena.barnMin.x - marginM;
  const double maxX = arena.barnMax.x + marginM;
  const double minZ = arena.barnMin.z - marginM;
  const double maxZ = arena.barnMax.z + marginM;
  if (selfX > minX && selfX < maxX && selfZ > minZ && selfZ < maxZ) {
    const double pushLeft = selfX - minX;
    const double pushRight = maxX - selfX;
    const double pushBack = selfZ - minZ;
    const double pushForward = maxZ - selfZ;
    double minPush = pushLeft;
    if (pushRight < minPush) minPush = pushRight;
    if (pushBack < minPush) minPush = pushBack;
    if (pushForward < minPush) minPush = pushForward;
    if (minPush == pushLeft) {
      steerX -= (marginM - pushLeft) * 2.0;
    } else if (minPush == pushRight) {
      steerX += (marginM - pushRight) * 2.0;
    } else if (minPush == pushBack) {
      steerZ -= (marginM - pushBack) * 2.0;
    } else {
      steerZ += (marginM - pushForward) * 2.0;
    }
  }

  out.x = dirX + steerX;
  out.z = dirZ + steerZ;
  return normalize(out, std::sqrt(dirX * dirX + dirZ * dirZ));
}

}  // namespace ac::ai
