#pragma once
// S06 §5.6：供客户端预测复用的单玩家子集步进（命令应用 → 积分 → 静态碰撞）。
#include <cstdint>

#include "sim/collision.hpp"
#include "sim/movement.hpp"
#include "sim/world.hpp"

namespace ac::sim {

// §5.6 的字段名照抄计划；bool 名按工程约定 §6 用 is 前缀（与 SpawnResult 一致，见 README 偏差清单）。
struct LocalStepResult {
  bool isOk = false;
  uint32_t tick = 0u;
  uint32_t timeMs = 0u;
};

// 只推进 localPlayerId 一个 tick；commands[0] 是唯一被消费的命令。
// dtMs != 50、命令缓冲为空或 localPlayerId 不是活动玩家 → {false, 当前 tick, 当前 timeMs} 且世界不变。
LocalStepResult localStep(World& world, const Command* commands, uint32_t commandCount, uint32_t dtMs,
                          EntityId localPlayerId) noexcept;

}  // namespace ac::sim
