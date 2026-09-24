#pragma once
// S06 §5.5：静态碰撞（谷仓推离 → 栅栏夹取）与单趟半邻域实体分离。
#include "sim/movement.hpp"
#include "sim/world.hpp"

namespace ac::sim {

// §5.4 半径表：kind → 半径（投影物/掉落物也在表里）。分离、静态碰撞与预测三处共用同一入口。
double radiusOf(const Entity& entity) noexcept;

// 单实体：先把圆推出谷仓 AABB（贴最近的面并清零该轴速度），再按场地边界夹取。
void collideStatic(MoveState& state, const ac::config::ArenaConfig& arena, double radius) noexcept;

// 单趟分离：只跟"东/南/东南/西南"四个半邻域 + 同格配对，每个无序对恰好处理一次。
void separateEntities(World& world) noexcept;

}  // namespace ac::sim
