#pragma once
// S09 §5.5：羊王三阶段阈值、阶段状态与召唤（spawn 流）。
#include <cstdint>

namespace ac::sim {
struct World;
struct Entity;
}  // namespace ac::sim

namespace ac::ai {

int32_t kingPhaseFor(double hpRatio) noexcept;
int32_t kingStateFor(int32_t phase) noexcept;
double kingSpeedMultiplier(int32_t phase) noexcept;
double kingAttackCooldownMs(int32_t phase) noexcept;

// 环身 2.6m 均匀 4 只咩咩兵，每只出生点加 ±0.3m 的 spawn 流抖动（x 抖动先于 z 抖动抽取）。
int32_t summonGrunts(ac::sim::World& world, ac::sim::Entity& king) noexcept;

// 返回本次召唤出的咩咩兵数。
int32_t updateKing(ac::sim::World& world, ac::sim::Entity& king, uint32_t dtMs) noexcept;

}  // namespace ac::ai
