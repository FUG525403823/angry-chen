#pragma once
// S05 §5.5：80m × 80m 牧场与实体尺寸（继承 v1 冻结数值，逐位一致）。
#include <array>
#include <cstddef>

#include "core/math.hpp"

namespace ac::sim::arena {

inline constexpr double kArenaSizeMeters = 80.0;
inline constexpr double kArenaHalfSizeMeters = 40.0;
inline constexpr double kFenceHeightMeters = 3.0;
inline constexpr double kFenceThicknessMeters = 0.5;

// §5.5：谷仓 AABB，min(-4, 0, -4) / max(4, 5, 4)。
inline constexpr ac::Aabb kBarn{{-4.0, 0.0, -4.0}, {4.0, 5.0, 4.0}};

// §5.5：4 个玩家出生点（y 一律 0，顺序 = 数组下标）。
inline constexpr std::array<ac::Vec3, 4> kPlayerSpawns{{
    {-4.5, 0.0, 7.0},
    {-1.5, 0.0, 7.0},
    {1.5, 0.0, 7.0},
    {4.5, 0.0, 7.0},
}};

// §5.5：12 个生成点，半径 38m 环，从 +z 起每 30° 一个。
inline constexpr double kSpawnRingRadiusMeters = 38.0;
inline constexpr std::array<ac::Vec3, 12> kSpawnPoints{{
    {0.0, 0.0, 38.0},
    {19.0, 0.0, 32.909},
    {32.909, 0.0, 19.0},
    {38.0, 0.0, 0.0},
    {32.909, 0.0, -19.0},
    {19.0, 0.0, -32.909},
    {0.0, 0.0, -38.0},
    {-19.0, 0.0, -32.909},
    {-32.909, 0.0, -19.0},
    // v1 arena.ts 的字面量是 { x: -38, z: -0 }：z 是**负零**。数值上与 +0 相等，
    // 但 S07 §5.4 的逐位比较与 configHash（%.17g 文本）会区分两者，故这里照抄 -0.0。
    {-38.0, 0.0, -0.0},
    {-32.909, 0.0, 19.0},
    {-19.0, 0.0, 32.909},
}};

// §5.5：碰撞半径 / 高度（米），下标 = EntityKind（0 player / 1 sheep / 2 projectile / 3 pickup）。
struct EntityDimensions {
  double radius;
  double height;
};

inline constexpr std::array<EntityDimensions, 4> kKindDimensions{{
    {0.4, 1.7},
    {0.5, 0.9},
    {0.08, 0.16},
    {0.35, 0.7},
}};

}  // namespace ac::sim::arena
