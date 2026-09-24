#pragma once
// S09 §5.2/§5.3/§5.7：羊形落地、状态转移校验、仇恨记账与逐 tick 意图计算。
// 两趟结构：阶段 4 只读世界算意图（updateAiIntents，见 sim/step.hpp），阶段 5 落速度（applyAiIntents）。
#include <cstdint>

#include "ai/sheep_state.hpp"
#include "config/sheep.hpp"

namespace ac::sim {
struct World;
struct Entity;
}  // namespace ac::sim

namespace ac::ai {

struct SheepIntent {
  double x = 0.0;
  double z = 0.0;
  double speed = 0.0;
  double yaw = 0.0;
};

// §5.1：落羊形（kind 码 + hp/maxHp/armor；v1 applySheepKind 只动这三项）。
void applySheepKind(ac::sim::Entity& entity, ac::config::SheepKind kind) noexcept;

ac::config::SheepKind sheepKindOf(const ac::sim::Entity& entity) noexcept;

bool canSheepTransition(int32_t from, int32_t to) noexcept;

// §5.3：同态恒真；非法转移返回 false 且不改状态。
bool setSheepState(ac::sim::Entity& entity, int32_t next) noexcept;

void noteSheepHit(ac::sim::Entity& entity, const uint16_t* playerIds, uint32_t playerCount,
                  uint16_t pid) noexcept;

// v1 updateSheepIntent：吃草/警戒/追击/蓄力/冲锋/攻击/游走/羊王加速全在这一处；
// 会消费 world.rng.ai（吃草重选点），因此 world 需要非 const 引用。
SheepIntent updateSheepIntent(ac::sim::World& world, ac::sim::Entity& entity, const uint16_t* playerIds,
                             uint32_t playerCount, double speedMultiplier, double dtMs) noexcept;

}  // namespace ac::ai
