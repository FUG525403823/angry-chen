// S06 §5.1：13 个阶段的固定顺序。阶段只允许在末尾追加，禁止变长 dt。
#include "sim/step.hpp"

namespace ac::sim {
namespace {

// 阶段 7：对全部活动实体 integrateState + aliveMs 累加。
// §5.1 末尾那句"对正在救援的玩家 clampHorizontalSpeed(1.5)"需要 S08 的救援状态机，
// 本份无该状态（见 README 的计划偏差清单），故调用点等 S08 接入；clampHorizontalSpeed 本身已实现并有用例。
void integrateEntities(World& world, uint32_t dtMs) noexcept {
  const double dtSeconds = static_cast<double>(dtMs) / 1000.0;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active) continue;
    MoveState state = moveStateOf(entity);
    integrateState(state, dtSeconds);
    storeMoveState(entity, state);
    entity.aliveMs += dtMs;
  }
}

// 阶段 8（与阶段 9 每趟分离后的复跑）：单实体先谷仓推离、后边界夹取。
void collideEntities(World& world) noexcept {
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active) continue;
    MoveState state = moveStateOf(entity);
    collideStatic(state, ac::config::kArenaConfig, radiusOf(entity));
    storeMoveState(entity, state);
  }
}

}  // namespace

void applyCommands(World& world, const Command* commands, uint32_t commandCount) noexcept {
  uint32_t cursor = 0u;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active || entity.kind != EntityKind::kPlayer) continue;
    const Command* command =
        (commands != nullptr && cursor < commandCount) ? &commands[cursor] : nullptr;
    ++cursor;
    MoveState state = moveStateOf(entity);
    applyCommandToState(state, command, 1.0);
    storeMoveState(entity, state);
  }
}

uint32_t collectPlayerIds(const World& world, EntityId* out) noexcept {
  uint32_t count = 0u;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active || entity.kind != EntityKind::kPlayer || entity.idle) continue;
    out[count] = entity.id;
    ++count;
  }
  return count;
}

// 阶段 4/5：空实现，等 S09。
void updateAiIntents(World&, uint32_t, const EntityId*, uint32_t, const SpatialGrid&) noexcept {}

void applyAiIntents(World&) noexcept {}

// 阶段 6：空实现，等 S08。
void applyKnockback(World&) noexcept {}

// 阶段 10/11：空实现，等 S08 / S09（返回值 = 落地条数，本份恒 0）。
void resolveCombat(World&, const Command*, uint32_t, uint32_t, const CombatContext*) noexcept {}

int resolveSheepAttacks(World&, const EntityId*, uint32_t) noexcept { return 0; }

int resolveEliteFire(World&) noexcept { return 0; }

int advanceProjectiles(World&, uint32_t) noexcept { return 0; }

int updateKing(World&, Entity&, uint32_t) noexcept { return 0; }

bool stepWorld(World& world, const Command* commands, uint32_t commandCount, uint32_t dtMs) noexcept {
  if (dtMs != ac::config::kStepDtMs) return false;  // §5.1：禁止变长 dt，拒绝时世界不变

  // 阶段 0：头部
  world.tick += 1u;
  world.timeMs += dtMs;
  world.eventCount = 0u;
  world.eventCursor = 0u;

  // 阶段 1：命令应用（按玩家 EntityId 升序）
  applyCommands(world, commands, commandCount);

  // 阶段 2：玩家列表（升序、跳过 idle）
  EntityId playerIds[kMaxEntities];
  const uint32_t playerCount = collectPlayerIds(world, playerIds);

  // 阶段 3：网格重建
  buildSpatialGrid(world);

  // 阶段 4/5：AI 意图
  updateAiIntents(world, dtMs, playerIds, playerCount, world.grid);
  applyAiIntents(world);

  // 阶段 6：击退
  applyKnockback(world);

  // 阶段 7：积分
  integrateEntities(world, dtMs);

  // 阶段 8：静态碰撞
  collideEntities(world);

  // 阶段 9：网格重建 + 分离（每趟分离后重跑静态碰撞）
  for (uint32_t pass = 0u; pass < ac::config::kSeparatePasses; ++pass) {
    buildSpatialGrid(world);
    separateEntities(world);
    collideEntities(world);
  }

  // 阶段 10/11：战斗、羊群攻击、投射物、羊王
  // updateKing(World&, Entity&, uint32_t) 需要 S09 创建的羊王实体，本份没有该实体，
  // 因此只冻结签名、不设调用点（见 README §7.5），其余四个阶段按 S09/S08 的冻结签名调用。
  resolveCombat(world, commands, commandCount, dtMs, nullptr);
  resolveSheepAttacks(world, playerIds, playerCount);
  resolveEliteFire(world);
  advanceProjectiles(world, dtMs);

  // 阶段 12：统计汇总
  updateWorldStats(world);

  // 阶段 13（S05 §5.3 的末尾追加）：姿态环记录
  recordPoseHistory(world.poseHistory, world);
  return true;
}

}  // namespace ac::sim
