#pragma once
// 弹药补给箱（S16）：地图固定位置的补弹点。实体 kind = pickup(3)，比赛开始时生成。
// 与 upgrades 同为新增玩法常量（不进 configHash），客户端 PickupTable.cs 镜像同一份数值。
#include <array>
#include <cstdint>

#include "core/math.hpp"

namespace ac::config {

inline constexpr int32_t kAmmoCrateCount = 4;
// 4 个箱位：谷仓（±4）之外、出生点（z=7）附近可达、与羊群出生环（r=38）保持距离。
inline constexpr std::array<ac::Vec3, kAmmoCrateCount> kAmmoCratePositions{{
    {10.0, 0.0, 10.0},
    {-10.0, 0.0, 10.0},
    {10.0, 0.0, -10.0},
    {-10.0, 0.0, -10.0},
}};

// 交互半径（米，玩家中心到箱子中心的距离）与蓄力时长（毫秒）。
inline constexpr double kAmmoCrateRangeM = 1.8;
inline constexpr double kAmmoCrateRefillMs = 1500.0;

}  // namespace ac::config
