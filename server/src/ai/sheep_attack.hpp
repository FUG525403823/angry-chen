#pragma once
// S09 §5.3/§5.6：撕咬/冲锋命中结算、击退、问号弹生成与推进。
#include <cstdint>

#include "config/sheep.hpp"

namespace ac::sim {
struct World;
struct Entity;
}  // namespace ac::sim

namespace ac::ai {

// v1 knockbackPlayer：击退时长取 max(已有, distance / 7 * 1000)，方向 = 受击者 - 攻击者的水平单位向量。
void knockbackPlayer(ac::sim::Entity& target, double fromX, double fromZ, double distanceM) noexcept;

// v1 damagePlayer：护甲先扣、清零下限、倒地/击杀事件、最后按 knockbackM 施加击退。返回本次总伤害。
double damagePlayer(ac::sim::World& world, ac::sim::Entity& attacker, ac::config::SheepKind kind,
                    ac::sim::Entity& target, double distanceM, double knockbackM) noexcept;

// 失败（距离过近 / 实体池耗尽）返回 0；成功返回弹丸 id，初速为水平单位向量 × 14 m/s。
uint16_t spawnQuestionBolt(ac::sim::World& world, ac::sim::Entity& shooter, ac::sim::Entity& target) noexcept;

}  // namespace ac::ai
