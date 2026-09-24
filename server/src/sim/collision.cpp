// S06 §5.5：静态碰撞与实体分离。
#include "sim/collision.hpp"

#include <cmath>

namespace ac::sim {
namespace {

// §5.5：谷仓推离。pos.y >= 5 直接跳过；按半径外扩的 AABB 内部才处理，贴最近的面。
// 计划只写了"贴哪一面"，没写清速度；§6 第 5 条断言 400 tick 后 vel.z == 0 要求该轴速度清零，
// 与边界夹取（明写"清零该轴速度"）保持同一语义。
void pushOutOfBarn(MoveState& state, const ac::config::ArenaConfig& arena, double radius) noexcept {
  if (state.pos.y >= arena.barnMax.y) return;
  const double minX = arena.barnMin.x - radius;
  const double maxX = arena.barnMax.x + radius;
  const double minZ = arena.barnMin.z - radius;
  const double maxZ = arena.barnMax.z + radius;
  if (state.pos.x <= minX || state.pos.x >= maxX || state.pos.z <= minZ || state.pos.z >= maxZ) {
    return;  // 未进入外扩后的 AABB
  }
  const double pushLeft = state.pos.x - minX;
  const double pushRight = maxX - state.pos.x;
  const double pushBack = state.pos.z - minZ;
  const double pushForward = maxZ - state.pos.z;
  if (std::min(pushLeft, pushRight) <= std::min(pushBack, pushForward)) {
    state.pos.x = pushLeft < pushRight ? minX : maxX;
    state.vel.x = 0.0;
  } else {
    state.pos.z = pushBack < pushForward ? minZ : maxZ;
    state.vel.z = 0.0;
  }
}

// §5.5：边界夹取，limit = halfSize - thickness / 2 - radius；夹取时清零该轴速度。
void clampToFence(MoveState& state, const ac::config::ArenaConfig& arena, double radius) noexcept {
  const double limit = arena.halfSizeMeters - arena.fenceHalfThicknessMeters - radius;
  if (state.pos.x < -limit) {
    state.pos.x = -limit;
    state.vel.x = 0.0;
  } else if (state.pos.x > limit) {
    state.pos.x = limit;
    state.vel.x = 0.0;
  }
  if (state.pos.z < -limit) {
    state.pos.z = -limit;
    state.vel.z = 0.0;
  } else if (state.pos.z > limit) {
    state.pos.z = limit;
    state.vel.z = 0.0;
  }
}

// §5.5：重叠则双方各推一半；distance < 1e-6 时按 (+1, 0) 推。
void separatePair(Entity& a, Entity& b) noexcept {
  const double dx = b.pos.x - a.pos.x;
  const double dz = b.pos.z - a.pos.z;
  const double distSq = dx * dx + dz * dz;
  if (distSq >= ac::config::kSeparatePrefilterSqM) return;  // 预筛：(2 × 0.5)^2 = 1.0
  const double minDistance = radiusOf(a) + radiusOf(b);
  if (distSq >= minDistance * minDistance) return;
  const double zeroSq = ac::config::kSeparateZeroDistanceM * ac::config::kSeparateZeroDistanceM;
  if (distSq < zeroSq) {
    const double push = minDistance / 2.0;
    a.pos.x -= push;
    b.pos.x += push;
    return;
  }
  const double distance = std::sqrt(distSq);
  const double push = (minDistance - distance) / 2.0;
  const double nx = dx / distance;
  const double nz = dz / distance;
  a.pos.x -= nx * push;
  a.pos.z -= nz * push;
  b.pos.x += nx * push;
  b.pos.z += nz * push;
}

// cellA / cellB 的笛卡尔积；同格时跳过 j <= i，保证每个无序对只处理一次。
void separateCellPairs(World& world, int32_t cellA, int32_t cellB) noexcept {
  const SpatialGrid& grid = world.grid;
  for (int32_t i = grid.cellStart[cellA]; i < grid.cellStart[cellA + 1]; ++i) {
    Entity& a = world.entities[grid.cellItems[i] - 1u];
    const int32_t start = cellA == cellB ? i + 1 : grid.cellStart[cellB];
    for (int32_t j = start; j < grid.cellStart[cellB + 1]; ++j) {
      separatePair(a, world.entities[grid.cellItems[j] - 1u]);
    }
  }
}

}  // namespace

double radiusOf(const Entity& entity) noexcept {
  return ac::config::kKindRadiusM[static_cast<std::size_t>(entity.kind)];
}

void collideStatic(MoveState& state, const ac::config::ArenaConfig& arena, double radius) noexcept {
  pushOutOfBarn(state, arena, radius);  // §5.5：先谷仓推离
  clampToFence(state, arena, radius);   // 后边界夹取
}

void separateEntities(World& world) noexcept {
  for (int32_t cz = 0; cz < kSpatialCellsPerAxis; ++cz) {
    for (int32_t cx = 0; cx < kSpatialCellsPerAxis; ++cx) {
      const int32_t cell = cz * kSpatialCellsPerAxis + cx;
      separateCellPairs(world, cell, cell);  // 同格
      if (cx + 1 < kSpatialCellsPerAxis) {
        separateCellPairs(world, cell, cell + 1);  // 东
      }
      if (cz > 0) {
        const int32_t south = cell - kSpatialCellsPerAxis;
        separateCellPairs(world, cell, south);  // 南
        if (cx + 1 < kSpatialCellsPerAxis) {
          separateCellPairs(world, cell, south + 1);  // 东南
        }
        if (cx > 0) {
          separateCellPairs(world, cell, south - 1);  // 西南
        }
      }
    }
  }
}

}  // namespace ac::sim
