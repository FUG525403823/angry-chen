#pragma once
// S09 §5.2/§5.7：仇恨槽衰减/命中加成、谷仓遮挡判定与目标选择（含切换比）。
#include <cstdint>

#include "ai/sheep_state.hpp"

namespace ac::sim {
struct World;
struct Entity;
}  // namespace ac::sim

namespace ac::ai {

// playerIds 中的下标（只在前 SHEEP_AI.aggroSlots 个里找）；找不到返回 -1。
int32_t aggroIndexOf(const uint16_t* playerIds, uint32_t playerCount, uint16_t pid) noexcept;

// 每 tick 衰减 2%：value < 0.01 归零（只作用于前 min(playerCount, aggroSlots) 个槽）。
void decayAggro(TargetState& state, int32_t playerCount) noexcept;

// 命中加成 += SHEEP_AI.aggroPerHit（20，v1 无上限夹取）；只作用于该玩家占的槽。
void noteHit(TargetState& state, const uint16_t* playerIds, uint32_t playerCount, uint16_t pid) noexcept;

// 谷仓 AABB 遮挡：从 (fromX, fromY, fromZ) 望向 target 胸口（+0.9m）的射线上有谷仓就不可见。
// 非玩家目标恒可见（v1 targeting.ts 的 kind !== 'player' 分支）。
bool isVisible(double fromX, double fromY, double fromZ, const ac::sim::Entity& target) noexcept;

// 多人统一按 aggro + 1/(1+distance) 打分；候选严格超过当前完整分数 × 切换比才替换。
// 多人失效重选、无目标距离归零；零/单玩家保留旧目标续期与缓存距离数值路径（ADR-019）。
// 仅新目标需要可见。
uint16_t selectTarget(TargetState& state, const ac::sim::World& world, const ac::sim::Entity& self,
                      const uint16_t* playerIds, uint32_t playerCount) noexcept;

}  // namespace ac::ai
