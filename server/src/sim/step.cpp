// S06 §5.1：13 个阶段的固定顺序。阶段只允许在末尾追加，禁止变长 dt。
#include "sim/step.hpp"

#include "combat/rage.hpp"
#include "combat/resolve.hpp"
#include "config/combat.hpp"

namespace ac::sim {
namespace {

// 阶段 7：对全部活动实体 integrateState + aliveMs 累加。
// §5.1 末尾那句"对正在救援的玩家 clampHorizontalSpeed(1.5)"在 S09 补齐：救援状态机（S08）与
// hasDownedTeammateInRange（combat/resolve）都已就位，调用点见 applyCommands（v1 sim.ts:109-115 同序）。
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
// §5.6/§5.7：投射物不参与静态碰撞（v1 sim.ts 的 resolveStaticCollisions 明确跳过 projectile）；
// 问号弹的越界/进场回收由阶段 11 的 advanceProjectiles 负责，否则谷仓推离会把弹丸挤到
// AABB 边界上、让 v1 的"进谷仓即销毁"判定永远不成立（S09 §5.6）。
void collideEntities(World& world) noexcept {
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active || entity.kind == EntityKind::kProjectile) continue;
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
    // S08 §5.7：狂暴期移速 ×1.15（v1 sim.ts 在同一个调用点传 RAGE.moveSpeedMultiplier）。
    const double speedMultiplier =
        ac::combat::isRageActive(entity.rage, static_cast<double>(world.timeMs))
            ? ac::config::kRageMoveSpeedMultiplier
            : 1.0;
    MoveState state = moveStateOf(entity);
    applyCommandToState(state, command, speedMultiplier);
    // S08 §5.7：倒地玩家不移动（v1 sim.ts 在同处把速度清零并 continue）。
    if (entity.downed.downed) {
      state.vel.x = 0.0;
      state.vel.y = 0.0;
      state.vel.z = 0.0;
    } else if (command != nullptr && (command->buttons & ac::config::kButtonInteract) != 0u &&
               hasDownedTeammateInRange(world, entity, ac::config::kReviveRangeM)) {
      // S09 补上 S08 §9.2-1 的欠账（v1 sim.ts:109-115）：按住交互且救援距离内有倒地队友 → 限速 1.5 m/s。
      clampHorizontalSpeed(state, ac::config::kReviveSpeedClampMps);
    }
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

// 阶段 4/5：S09 已落地，定义在 ai/sheep_brain.cpp。
// 阶段 6：S09 已落地，定义在 combat/knockback.cpp（S06 的占位签名补上 dtMs）。
// 阶段 10：S08 已落地，定义在 combat/resolve.cpp（签名不变）。
// 阶段 11：S09 已落地，定义在 ai/sheep_attack.cpp（resolveSheepAttacks / resolveEliteFire /
// advanceProjectiles，S06 的占位签名补上 playerIds/playerCount）与 ai/king_phases.cpp（updateKing / updateKings）。

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
  applyKnockback(world, dtMs);

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

  // 阶段 10/11：战斗、羊群攻击、投射物、羊王（updateKings 遍历本 tick 存活的羊王，逐个走冻结的
  // updateKing(World&, Entity&, uint32_t)；召唤出的咩咩兵按 activeIds 升序插入，遍历按 v1 逐字重读当前下标）。
  resolveCombat(world, commands, commandCount, dtMs, nullptr);
  resolveSheepAttacks(world, playerIds, playerCount);
  resolveEliteFire(world, playerIds, playerCount);
  advanceProjectiles(world, dtMs, playerIds, playerCount);
  updateKings(world, dtMs);

  // 阶段 12：统计汇总
  updateWorldStats(world);

  // 阶段 13（S05 §5.3 的末尾追加）：姿态环记录
  recordPoseHistory(world.poseHistory, world);
  return true;
}

}  // namespace ac::sim
