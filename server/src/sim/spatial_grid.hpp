#pragma once
// S05 §5.4：4m 均匀网格（20 × 20 = 400 格）、两趟计数排序重建、按半径遍历邻居。
#include <cstddef>
#include <cstdint>

#include "core/math.hpp"
#include "sim/arena.hpp"
#include "sim/entity_table.hpp"

namespace ac::sim {

inline constexpr double kSpatialCellMeters = 4.0;
inline constexpr int32_t kSpatialCellsPerAxis = 20;  // ceil(80 / 4)
inline constexpr int32_t kSpatialCellCount = kSpatialCellsPerAxis * kSpatialCellsPerAxis;

struct SpatialGrid {
  int32_t cellStart[kSpatialCellCount + 1]{};  // 前缀和，§5.4 的 401 项
  uint16_t cellItems[kMaxEntities]{};          // 存 EntityId，容量恒等于实体容量
};
static_assert(sizeof(SpatialGrid::cellItems) == kMaxEntities * sizeof(uint16_t),
              "§5.5：cellItems 容量必须与 kMaxEntities 恒等");

// §5.4：cx = clamp(floor((x + 40) / 4), 0, 19)，越界夹取到边界格（只会多访问、不会漏配对）。
inline int32_t spatialCellOf(double coordinate) noexcept {
  const double shifted = (coordinate + arena::kArenaHalfSizeMeters) / kSpatialCellMeters;
  if (!(shifted > 0.0)) return 0;  // 负数与 NaN 一律落到边界格
  const int32_t cell = static_cast<int32_t>(shifted);  // 正数截断即 floor
  return cell > kSpatialCellsPerAxis - 1 ? kSpatialCellsPerAxis - 1 : cell;
}

// §5.4：cell = cz * 20 + cx。
inline int32_t spatialCellIndex(double x, double z) noexcept {
  return spatialCellOf(z) * kSpatialCellsPerAxis + spatialCellOf(x);
}

struct World;  // 前置声明：buildSpatialGrid 的实现放在 world.cpp

// §5.4：两趟计数排序重建，按 activeIds 升序写入 → 每格内 EntityId 升序；projectile 不参与。
void buildSpatialGrid(World& world) noexcept;

// §5.4 遍历顺序：cz 外层升序 → cx 内层升序 → 格内按写入下标升序（即 EntityId 升序）。
template <typename Visit>
inline void forEachNeighbor(const SpatialGrid& grid, double x, double z, double radiusM,
                            Visit&& visit) noexcept {
  const int32_t cxMin = spatialCellOf(x - radiusM);
  const int32_t cxMax = spatialCellOf(x + radiusM);
  const int32_t czMin = spatialCellOf(z - radiusM);
  const int32_t czMax = spatialCellOf(z + radiusM);
  for (int32_t cz = czMin; cz <= czMax; ++cz) {
    for (int32_t cx = cxMin; cx <= cxMax; ++cx) {
      const int32_t cell = cz * kSpatialCellsPerAxis + cx;
      for (int32_t item = grid.cellStart[cell]; item < grid.cellStart[cell + 1]; ++item) {
        visit(grid.cellItems[item]);
      }
    }
  }
}

}  // namespace ac::sim
