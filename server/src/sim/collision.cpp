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
  // 方向按 v1 的写法先取倒数再乘（dx / distance 与 dx * (1 / distance) 可能差 1 ULP，
  // 而对拍是逐位比较，必须同式）。
  const double distance = std::sqrt(distSq);
  const double inverse = 1.0 / distance;
  const double nx = dx * inverse;
  const double nz = dz * inverse;
  const double half = (minDistance - distance) / 2.0;
  a.pos.x -= nx * half;
  a.pos.z -= nz * half;
  b.pos.x += nx * half;
  b.pos.z += nz * half;
}

// 邻域偏移 = v1 SEPARATION_OFFSETS（东 / 北 / 东北 / 西北）：每对无序格只从西/南侧访问一次。
constexpr int32_t kNeighborOffsetX[4] = {1, 0, 1, -1};
constexpr int32_t kNeighborOffsetZ[4] = {0, 1, 1, 1};

}  // namespace

double radiusOf(const Entity& entity) noexcept {
  return ac::config::kKindRadiusM[static_cast<std::size_t>(entity.kind)];
}

void collideStatic(MoveState& state, const ac::config::ArenaConfig& arena, double radius) noexcept {
  pushOutOfBarn(state, arena, radius);  // §5.5：先谷仓推离
  clampToFence(state, arena, radius);   // 后边界夹取
}

// 遍历顺序逐字对齐 v1 resolveEntitySeparation：按格升序 → 格内实体升序 → 先同格 j > i，
// 再该实体的四个邻格。顺序会改变浮点累加次序（每次推挤都是 pos ±= ... 的读改写），
// 所以「同一批对、不同顺序」在逐位对拍下不等价。
void separateEntities(World& world) noexcept {
  const SpatialGrid& grid = world.grid;
  const int32_t cols = kSpatialCellsPerAxis;
  const int32_t rows = kSpatialCellsPerAxis;
  const int32_t cells = cols * rows;
  for (int32_t cell = 0; cell < cells; ++cell) {
    const int32_t start = grid.cellStart[cell];
    const int32_t end = grid.cellStart[cell + 1];
    if (start >= end) continue;
    const int32_t cx = cell % cols;
    const int32_t cz = (cell - cx) / cols;
    for (int32_t i = start; i < end; ++i) {
      Entity& a = world.entities[grid.cellItems[i] - 1u];
      for (int32_t j = i + 1; j < end; ++j) {
        separatePair(a, world.entities[grid.cellItems[j] - 1u]);
      }
      for (int32_t o = 0; o < 4; ++o) {
        const int32_t nx = cx + kNeighborOffsetX[o];
        const int32_t nz = cz + kNeighborOffsetZ[o];
        if (nx < 0 || nz < 0 || nx >= cols || nz >= rows) continue;
        const int32_t neighbor = nz * cols + nx;
        for (int32_t j = grid.cellStart[neighbor]; j < grid.cellStart[neighbor + 1]; ++j) {
          separatePair(a, world.entities[grid.cellItems[j] - 1u]);
        }
      }
    }
  }
}

}  // namespace ac::sim
