// S06 §5.6：预测侧子集步进。不读 RNG、不写事件池、不碰其他实体与统计量。
#include "sim/local_step.hpp"

namespace ac::sim {

LocalStepResult localStep(World& world, const Command* commands, uint32_t commandCount, uint32_t dtMs,
                          EntityId localPlayerId) noexcept {
  LocalStepResult result{false, world.tick, world.timeMs};
  if (dtMs != ac::config::kStepDtMs || commands == nullptr || commandCount == 0u) return result;
  Entity* entity = entityById(world, localPlayerId);
  if (entity == nullptr || entity->kind != EntityKind::kPlayer) return result;

  MoveState state = moveStateOf(*entity);
  applyCommandToState(state, &commands[0], 1.0);
  integrateState(state, static_cast<double>(dtMs) / 1000.0);
  collideStatic(state, ac::config::kArenaConfig, radiusOf(*entity));
  storeMoveState(*entity, state);

  world.tick += 1u;
  world.timeMs += dtMs;
  result.isOk = true;
  result.tick = world.tick;
  result.timeMs = world.timeMs;
  return result;
}

}  // namespace ac::sim
