#include "ai/flocking.hpp"

#include <cmath>
#include <limits>

#include "sim/spatial_grid.hpp"
#include "sim/world.hpp"

namespace ac::ai {
namespace {

// 格遍历的上下文：替代 v1 的模块级变量，避免每次调用新建闭包。
struct GatherContext {
  FlockNeighbors* neighbors = nullptr;
  const ac::sim::World* world = nullptr;
  uint16_t selfId = 0u;
  double x = 0.0;
  double z = 0.0;
  double radiusSq = 0.0;
};

void visitNeighbor(const GatherContext& context, uint16_t id) noexcept {
  const ac::sim::Entity* other = ac::sim::entityById(*context.world, id);
  if (other == nullptr || !other->active || other->kind != ac::sim::EntityKind::kSheep ||
      other->id == context.selfId) {
    return;
  }
  const double dx = other->pos.x - context.x;
  const double dz = other->pos.z - context.z;
  const double distanceSq = dx * dx + dz * dz;
  if (distanceSq > context.radiusSq) return;
  insertNeighborByDistance(*context.neighbors, distanceSq, other->id, other->pos.x, other->pos.z,
                           other->vel.x, other->vel.z);
}

}  // namespace

void addNeighbor(FlockNeighbors& neighbors, double x, double z, double dirX, double dirZ) noexcept {
  if (neighbors.count >= neighbors.capacity) return;
  const int32_t index = neighbors.count;
  neighbors.xs[index] = x;
  neighbors.zs[index] = z;
  neighbors.dirXs[index] = dirX;
  neighbors.dirZs[index] = dirZ;
  neighbors.distSq[index] = std::numeric_limits<double>::infinity();
  neighbors.ids[index] = 0u;
  neighbors.count += 1;
}

void insertNeighborByDistance(FlockNeighbors& neighbors, double distanceSq, uint16_t id, double x,
                              double z, double dirX, double dirZ) noexcept {
  const int32_t capacity = neighbors.capacity;
  int32_t count = neighbors.count;
  if (count >= capacity) {
    const int32_t worst = count - 1;
    const double worstDist = neighbors.distSq[worst];
    const uint16_t worstId = neighbors.ids[worst];
    if (distanceSq > worstDist || (distanceSq == worstDist && id >= worstId)) return;
  } else {
    count += 1;
  }

  int32_t at = 0;
  while (at < count - 1) {
    const double slotDist = neighbors.distSq[at];
    const uint16_t slotId = neighbors.ids[at];
    if (slotDist > distanceSq || (slotDist == distanceSq && slotId > id)) break;
    at += 1;
  }
  for (int32_t i = count - 1; i > at; --i) {
    neighbors.distSq[i] = neighbors.distSq[i - 1];
    neighbors.ids[i] = neighbors.ids[i - 1];
    neighbors.xs[i] = neighbors.xs[i - 1];
    neighbors.zs[i] = neighbors.zs[i - 1];
    neighbors.dirXs[i] = neighbors.dirXs[i - 1];
    neighbors.dirZs[i] = neighbors.dirZs[i - 1];
  }
  neighbors.distSq[at] = distanceSq;
  neighbors.ids[at] = id;
  neighbors.xs[at] = x;
  neighbors.zs[at] = z;
  neighbors.dirXs[at] = dirX;
  neighbors.dirZs[at] = dirZ;
  neighbors.count = count;
}

void finalizeNeighbors(FlockNeighbors& neighbors) noexcept {
  const int32_t count = neighbors.count;
  if (count <= 0) return;
  double centroidX = 0.0;
  double centroidZ = 0.0;
  double averageDirX = 0.0;
  double averageDirZ = 0.0;
  for (int32_t i = 0; i < count; ++i) {
    centroidX += neighbors.xs[i];
    centroidZ += neighbors.zs[i];
    averageDirX += neighbors.dirXs[i];
    averageDirZ += neighbors.dirZs[i];
  }
  neighbors.centroidX = centroidX / count;
  neighbors.centroidZ = centroidZ / count;
  neighbors.averageDirX = averageDirX / count;
  neighbors.averageDirZ = averageDirZ / count;
}

int32_t gatherNeighbors(FlockNeighbors& neighbors, const ac::sim::World& world,
                        const ac::sim::Entity& self, const ac::sim::SpatialGrid& grid) noexcept {
  resetFlockNeighbors(neighbors);
  const double radius = ac::config::kSheepAi.neighborRadiusM;
  GatherContext context{};
  context.neighbors = &neighbors;
  context.world = &world;
  context.selfId = self.id;
  context.x = self.pos.x;
  context.z = self.pos.z;
  context.radiusSq = radius * radius;
  ac::sim::forEachNeighbor(grid, context.x, context.z, radius,
                           [&context](uint16_t id) { visitNeighbor(context, id); });
  finalizeNeighbors(neighbors);
  return neighbors.count;
}

SteeringOut& flockForce(SteeringOut& out, double selfX, double selfZ, const FlockNeighbors& neighbors,
                        double speed) noexcept {
  if (neighbors.count <= 0) {
    out.x = 0.0;
    out.z = 0.0;
    return out;
  }
  double separatorX = 0.0;
  double separatorZ = 0.0;
  const double radiusSq = ac::config::kSheepAi.neighborRadiusM * ac::config::kSheepAi.neighborRadiusM;
  for (int32_t i = 0; i < neighbors.count; ++i) {
    const double dx = selfX - neighbors.xs[i];
    const double dz = selfZ - neighbors.zs[i];
    const double distanceSq = dx * dx + dz * dz;
    if (distanceSq >= radiusSq) continue;
    if (distanceSq < kSteeringZeroLengthM) {
      separatorX += i % 2 == 0 ? 1.0 : -1.0;
      separatorZ += i % 2 == 0 ? -1.0 : 1.0;
      continue;
    }
    const double weight = 1.0 / distanceSq;
    separatorX += dx * weight;
    separatorZ += dz * weight;
  }
  const double separationLength = std::sqrt(separatorX * separatorX + separatorZ * separatorZ);
  if (separationLength > kSteeringZeroLengthM) {
    separatorX /= separationLength;
    separatorZ /= separationLength;
  }

  out.x = separatorX * ac::config::kFlockWeightSeparation +
          (neighbors.averageDirX - 0.0) * ac::config::kFlockWeightAlignment +
          (neighbors.centroidX - selfX) * ac::config::kFlockWeightCohesion * ac::config::kFlockCohesionScale;
  out.z = separatorZ * ac::config::kFlockWeightSeparation +
          (neighbors.averageDirZ - 0.0) * ac::config::kFlockWeightAlignment +
          (neighbors.centroidZ - selfZ) * ac::config::kFlockWeightCohesion * ac::config::kFlockCohesionScale;
  return normalize(out, speed);
}

}  // namespace ac::ai
