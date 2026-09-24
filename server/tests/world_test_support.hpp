#pragma once
// S06 起 stepWorld 要命令缓冲与 dtMs，而 S05 的数据层用例只关心"推进一个 tick"。
// 共享一份"空命令推进"，避免 alloc_test.cpp 与 world_test.cpp 各写一遍。
#include "config/player.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"

namespace ac::test {

inline bool stepEmpty(ac::sim::World& world) {
  return ac::sim::stepWorld(world, nullptr, 0u, ac::config::kStepDtMs);
}

// S09 起 stepWorld 的阶段 4/5 会驱动羊的 AI：只想测分离/静态碰撞的用例把羊按在硬直态，
// 让 updateSheepIntent 走「timerMs > 0」的早退分支（速度恒 0，也不消费任何 RNG 流）。
inline void freezeSheepAi(ac::sim::World& world) {
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    ac::sim::Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active || entity.kind != ac::sim::EntityKind::kSheep) continue;
    entity.state = ac::config::sheepStateCode(ac::config::SheepState::kStagger);
    entity.ai.timerMs = ac::config::kSheepAi.chargeStaggerMs;
  }
}

}  // namespace ac::test
