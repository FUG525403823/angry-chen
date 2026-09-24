#pragma once
// S09 §5.7：有界邻居收集（容量恒为 SHEEP_AI.maxNeighbors；按 (distSq, id) 升序插入，与遍历顺序无关）
// 与 flock 三权重组（分离 1.6 / 对齐 0.4 / 聚集 0.5）。零分配：格遍历用引用捕获的 lambda。
#include <cstdint>

#include "ai/sheep_state.hpp"
#include "ai/steering.hpp"

namespace ac::sim {
struct World;
struct Entity;
struct SpatialGrid;
}  // namespace ac::sim

namespace ac::ai {

// 纯追加：容量满即丢弃（不参与距离排序）。
void addNeighbor(FlockNeighbors& neighbors, double x, double z, double dirX, double dirZ) noexcept;

// 有界按距离插入：容量满时挤掉最远的一个；距离相同按 id 升序破平局。
void insertNeighborByDistance(FlockNeighbors& neighbors, double distanceSq, uint16_t id, double x,
                              double z, double dirX, double dirZ) noexcept;

void finalizeNeighbors(FlockNeighbors& neighbors) noexcept;

// 只访问覆盖 radius 的整数格，取距离最近的 maxNeighbors 个；返回收集到的邻居数。
int32_t gatherNeighbors(FlockNeighbors& neighbors, const ac::sim::World& world,
                        const ac::sim::Entity& self, const ac::sim::SpatialGrid& grid) noexcept;

// 分离力（半径内 1/distSq 加权并归一）+ 对齐 + 聚集，最后缩放到 speed。
SteeringOut& flockForce(SteeringOut& out, double selfX, double selfZ, const FlockNeighbors& neighbors,
                        double speed) noexcept;

}  // namespace ac::ai
