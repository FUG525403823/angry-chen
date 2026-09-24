// S09 §6：羊群 AI 与波次导演的断言（--filter=ai）。
// 口径来源：v1 packages/shared/src/ai/*.test.ts 的采样点 + docs/plans-v2/server/S09 §5/§6。
#include "tiny_test.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "ai/flocking.hpp"
#include "ai/king_phases.hpp"
#include "ai/sheep_attack.hpp"
#include "ai/sheep_brain.hpp"
#include "ai/steering.hpp"
#include "ai/targeting.hpp"
#include "allocation_probe.hpp"
#include "config/player.hpp"
#include "config/sheep.hpp"
#include "config/waves.hpp"
#include "core/math.hpp"
#include "core/rng.hpp"
#include "sim/spatial_grid.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"
#include "world_test_support.hpp"

namespace {

namespace sim = ac::sim;
namespace ai = ac::ai;

constexpr uint32_t kSeed = 0x5EA9u;

using ac::config::SheepKind;
using ac::config::SheepState;

uint16_t spawnAt(sim::World& world, sim::EntityKind kind, double x, double z) {
  const sim::SpawnResult spawned = sim::spawnEntity(world, kind, ac::Vec3{x, 0.0, z});
  AC_CHECK(spawned.isOk);
  return spawned.id;
}

sim::Entity& entityAt(sim::World& world, uint16_t id) { return world.entities[id - 1u]; }

// 阶段 3/4/5（网格 → 意图 → 落地），不跑积分与战斗：只看 AI 状态与速度的用例用它。
void aiTick(sim::World& world) {
  sim::EntityId ids[sim::kMaxEntities];
  const uint32_t count = sim::collectPlayerIds(world, ids);
  sim::buildSpatialGrid(world);
  sim::updateAiIntents(world, ac::config::kStepDtMs, ids, count, world.grid);
  sim::applyAiIntents(world);
}

// 羊 + 玩家（羊形落地会覆盖 hp/armor/sheepKind）。
void placeSheepAndPlayer(sim::World& world, SheepKind kind, uint16_t sheep, uint16_t player) {
  ai::applySheepKind(entityAt(world, sheep), kind);
  AC_CHECK(entityAt(world, player).kind == sim::EntityKind::kPlayer);
}

uint64_t bitsOf(double value) { return std::bit_cast<uint64_t>(value); }

uint16_t findProjectileOf(const sim::World& world, uint16_t ownerId) {
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const sim::Entity* entity = sim::entityById(world, world.activeIds[i]);
    if (entity == nullptr || !entity->active || entity->kind != sim::EntityKind::kProjectile) continue;
    if (entity->ownerId == ownerId) return entity->id;
  }
  return 0u;
}

std::size_t countEvents(const sim::World& world, uint8_t type) {
  std::size_t count = 0u;
  for (std::size_t i = 0u; i < world.eventCount; ++i) {
    if (world.events[i].type == type) ++count;
  }
  return count;
}

// ---------- §5.1/§5.2 常量表 ----------

AC_TEST(ai_sheep_kinds_equal_v1_table) {
  AC_CHECK_EQ(ac::config::kSheep.size(), 4u);
  const double hp[4] = {60.0, 140.0, 260.0, 2400.0};
  const double speed[4] = {2.6, 3.2, 2.4, 2.0};
  const double damage[4] = {8.0, 22.0, 14.0, 30.0};
  const double price[4] = {1.0, 3.0, 6.0, 20.0};
  const double radius[4] = {0.5, 0.55, 0.6, 1.6};
  const double height[4] = {0.9, 1.0, 1.1, 2.4};
  for (std::size_t i = 0u; i < 4u; ++i) {
    AC_CHECK_EQ(ac::config::kSheep[i].hp, hp[i]);
    AC_CHECK_EQ(ac::config::kSheep[i].speed, speed[i]);
    AC_CHECK_EQ(ac::config::kSheep[i].damage, damage[i]);
    AC_CHECK_EQ(ac::config::kSheep[i].price, price[i]);
    AC_CHECK_EQ(ac::config::kSheep[i].radiusM, radius[i]);
    AC_CHECK_EQ(ac::config::kSheep[i].heightM, height[i]);
    AC_CHECK_EQ(static_cast<uint8_t>(ac::config::kSheepOrder[i]), static_cast<uint8_t>(i));
    AC_CHECK_EQ(ac::config::kSheepAttackProfile[i].damage, damage[i]);
  }
  AC_CHECK_EQ(ac::config::sheepKindIndex(SheepKind::kKing), 3);
  AC_CHECK_EQ(ac::config::sheepPrice(SheepKind::kElite), 6.0);
  AC_CHECK_EQ(ac::config::sheepKindIndex(SheepKind::kGrunt), 0);
}

AC_TEST(ai_params_equal_v1_values) {
  const ac::config::SheepAiParams& p = ac::config::kSheepAi;
  AC_CHECK_EQ(p.sightM, 35.0);
  AC_CHECK_EQ(p.attackRangeM, 1.4);
  AC_CHECK_EQ(p.attackCooldownMs, 1200.0);
  AC_CHECK_EQ(p.chargeWindupMs, 1000.0);
  AC_CHECK_EQ(p.chargeSpeedMps, 9.0);
  AC_CHECK_EQ(p.eliteBoltRangeM, 25.0);
  AC_CHECK_EQ(p.eliteBoltCooldownMs, 2500.0);
  AC_CHECK_EQ(p.eliteKeepMinM, 15.0);
  AC_CHECK_EQ(p.eliteKeepMaxM, 25.0);
  AC_CHECK_EQ(p.boltSpeedMps, 14.0);
  AC_CHECK_EQ(p.kingSummonCount, 4);
  AC_CHECK_EQ(p.kingSummonIntervalMs, 8000.0);
  AC_CHECK_EQ(p.kingPhase3SpeedMultiplier, 1.4);
  AC_CHECK_EQ(p.kingPhase3CooldownMultiplier, 0.7);
  AC_CHECK_NEAR(p.attackCooldownMs * p.kingPhase3CooldownMultiplier, 840.0, 1e-9);
  AC_CHECK_EQ(p.staggerMs, 250.0);
  AC_CHECK_EQ(p.chargeStaggerMs, 1000.0);
  AC_CHECK_EQ(p.deadFadeMs, 1500.0);
  AC_CHECK_EQ(p.knockbackVelocityMps, 7.0);
  AC_CHECK_EQ(p.neighborRadiusM, 3.0);
  AC_CHECK_EQ(p.maxNeighbors, 12);
  AC_CHECK_EQ(p.aggroSlots, 8);
  AC_CHECK_EQ(p.aggroDecayPerTick, 0.02);
  AC_CHECK_EQ(p.aggroPerHit, 20.0);
  AC_CHECK_EQ(p.targetSwitchRatio, 1.5);
  AC_CHECK_EQ(p.grazeRadiusM, 6.0);

  AC_CHECK_EQ(ac::config::kSheepAlertMs, 300.0);
  AC_CHECK_EQ(ac::config::kRamChargeTriggerM, 12.0);
  AC_CHECK_EQ(ac::config::kRamChargeMaxMs, 1600.0);
  AC_CHECK_EQ(ac::config::kGrazeRepickMs, 2500.0);
  AC_CHECK_EQ(ac::config::kEliteStrafeMs, 1200.0);
  AC_CHECK_EQ(ac::config::kEliteStrafeSpeedRatio, 0.5);
  AC_CHECK_EQ(ac::config::kGrazeSpeedRatio, 0.4);
  AC_CHECK_EQ(ac::config::kGrazeObstacleRadiusRatio, 0.5);
  AC_CHECK_EQ(ac::config::kArriveSlowRadiusM, 2.0);
  AC_CHECK_EQ(ac::config::kFlockWeightSeparation, 1.6);
  AC_CHECK_EQ(ac::config::kFlockWeightAlignment, 0.4);
  AC_CHECK_EQ(ac::config::kFlockWeightCohesion, 0.5);
  AC_CHECK_EQ(ac::config::kFlockCohesionScale, 0.1);
  AC_CHECK_EQ(ac::config::kFlockBlendRatio, 0.5);
  AC_CHECK_EQ(ac::config::kBiteKnockbackM, 1.5);
  AC_CHECK_EQ(ac::config::kChargeKnockbackM, 3.0);
  AC_CHECK_EQ(ac::config::kQuestionBoltLifeMs, 3000.0);
  AC_CHECK_EQ(ac::config::kQuestionBoltRadiusM, 0.22);
  AC_CHECK_EQ(ac::config::kQuestionBoltSpawnHeightM, 0.6);
  AC_CHECK_EQ(ac::config::kTargetEyeHeightM, 0.6);
  AC_CHECK_EQ(ac::config::kVictimChestHeightM, 0.9);
  AC_CHECK_EQ(ac::config::kKingSummonRadiusM, 2.6);
  AC_CHECK_EQ(ac::config::kKingSummonJitterM, 0.3);
  AC_CHECK_EQ(ac::config::kKingPhase1MinRatio, 0.66);
  AC_CHECK_EQ(ac::config::kKingPhase2MinRatio, 0.33);
  AC_CHECK_EQ(ac::config::kFieldEdgeLimitM, 39.5);
}

AC_TEST(ai_transition_table_is_v1_exact) {
  const uint8_t expected[13][7] = {
      {4u, 1u, 2u, 7u, 8u, 0u, 0u},    {5u, 2u, 0u, 6u, 7u, 8u, 0u},
      {6u, 3u, 5u, 6u, 7u, 8u, 9u},    {4u, 4u, 2u, 7u, 8u, 0u, 0u},
      {3u, 2u, 7u, 8u, 0u, 0u, 0u},    {3u, 2u, 7u, 8u, 0u, 0u, 0u},
      {4u, 2u, 1u, 7u, 8u, 0u, 0u},    {3u, 2u, 8u, 9u, 0u, 0u, 0u},
      {0u, 0u, 0u, 0u, 0u, 0u, 0u},    {4u, 4u, 10u, 7u, 8u, 0u, 0u},
      {5u, 4u, 12u, 11u, 7u, 8u, 0u},  {4u, 4u, 12u, 7u, 8u, 0u, 0u},
      {4u, 2u, 5u, 7u, 8u, 0u, 0u},
  };
  for (int from = 0; from < ac::config::kSheepStateCount; ++from) {
    AC_CHECK_EQ(ac::config::kSheepTransitions[from].count, expected[from][0]);
    for (int slot = 0; slot < ac::config::kSheepMaxTransitions; ++slot) {
      AC_CHECK_EQ(ac::config::kSheepTransitions[from].to[slot], expected[from][slot + 1]);
    }
    for (int to = 0; to < ac::config::kSheepStateCount; ++to) {
      bool allowed = from == to;
      for (uint8_t slot = 0u; slot < ac::config::kSheepTransitions[from].count; ++slot) {
        if (ac::config::kSheepTransitions[from].to[slot] == static_cast<uint8_t>(to)) allowed = true;
      }
      AC_CHECK_EQ(ac::config::canSheepTransition(from, to), allowed);
    }
  }
  AC_CHECK(!ac::config::canSheepTransition(-1, 0));
  AC_CHECK(!ac::config::canSheepTransition(0, 13));
  AC_CHECK(ac::config::canSheepTransition(8, 8));
  AC_CHECK(!ac::config::canSheepTransition(8, 0));
}

AC_TEST(ai_set_state_rejects_illegal_transition) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t id = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 20.0);
  sim::Entity& entity = entityAt(*world, id);
  AC_CHECK_EQ(entity.state, ac::config::sheepStateCode(SheepState::kGraze));

  AC_CHECK(!ai::setSheepState(entity, static_cast<int32_t>(SheepState::kCharge)));
  AC_CHECK_EQ(entity.state, ac::config::sheepStateCode(SheepState::kGraze));
  AC_CHECK(ai::setSheepState(entity, static_cast<int32_t>(SheepState::kAlert)));
  AC_CHECK_EQ(entity.state, ac::config::sheepStateCode(SheepState::kAlert));
  AC_CHECK(ai::setSheepState(entity, static_cast<int32_t>(SheepState::kAlert)));  // 同态恒真
  AC_CHECK(ai::setSheepState(entity, static_cast<int32_t>(SheepState::kChase)));
  AC_CHECK(!ai::setSheepState(entity, static_cast<int32_t>(SheepState::kKingPhase2)));

  entity.state = ac::config::sheepStateCode(SheepState::kDead);
  AC_CHECK(ai::setSheepState(entity, static_cast<int32_t>(SheepState::kDead)));
  AC_CHECK(!ai::setSheepState(entity, static_cast<int32_t>(SheepState::kChase)));
  AC_CHECK_EQ(entity.state, ac::config::sheepStateCode(SheepState::kDead));
}

AC_TEST(ai_apply_kind_sets_hp_and_armor) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t id = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 20.0);
  sim::Entity& entity = entityAt(*world, id);
  const SheepKind kinds[4] = {SheepKind::kGrunt, SheepKind::kRam, SheepKind::kElite, SheepKind::kKing};
  const double hp[4] = {60.0, 140.0, 260.0, 2400.0};
  for (int i = 0; i < 4; ++i) {
    ai::applySheepKind(entity, kinds[i]);
    AC_CHECK_EQ(entity.hp, hp[i]);
    AC_CHECK_EQ(entity.maxHp, hp[i]);
    AC_CHECK_EQ(entity.armor, 0.0);
    AC_CHECK_EQ(entity.sheepKind, static_cast<uint8_t>(i));
    AC_CHECK(ai::sheepKindOf(entity) == kinds[i]);
    AC_CHECK_EQ(entity.state, ac::config::sheepStateCode(SheepState::kGraze));
  }
}

// ---------- §5.7 转向原语 ----------

AC_TEST(ai_steering_seek_and_zero_length) {
  ai::SteeringOut out = ai::createSteeringOut();
  ai::seek(out, 0.0, 0.0, 3.0, 4.0, 5.0);
  AC_CHECK_EQ(out.x, 3.0);
  AC_CHECK_EQ(out.z, 4.0);
  ai::seek(out, 0.0, 0.0, 3.0, 4.0, 10.0);
  AC_CHECK_EQ(out.x, 6.0);
  AC_CHECK_EQ(out.z, 8.0);
  ai::seek(out, 1.0, 1.0, 1.0, 1.0, 4.0);
  AC_CHECK_EQ(out.x, 0.0);
  AC_CHECK_EQ(out.z, 0.0);
  out.x = 0.0;
  out.z = 0.0;
  ai::normalize(out, 7.0);
  AC_CHECK_EQ(out.x, 0.0);
  AC_CHECK_EQ(out.z, 0.0);
}

AC_TEST(ai_steering_arrive_slows_inside_radius) {
  ai::SteeringOut out = ai::createSteeringOut();
  ai::arrive(out, 0.0, 0.0, 1.0, 0.0, 4.0, 2.0);  // 距离 1 < 2 → 4 * (1/2)
  AC_CHECK_EQ(out.x, 2.0);
  AC_CHECK_EQ(out.z, 0.0);
  ai::arrive(out, 0.0, 0.0, 2.0, 0.0, 4.0, 2.0);  // 距离 == 半径 → 全速（< 而非 <=）
  AC_CHECK_EQ(out.x, 4.0);
  ai::arrive(out, 0.0, 0.0, 5.0, 0.0, 4.0, 2.0);
  AC_CHECK_EQ(out.x, 4.0);
  ai::arrive(out, 2.0, 2.0, 2.0, 2.0, 4.0, 2.0);
  AC_CHECK_EQ(out.x, 0.0);
  AC_CHECK_EQ(out.z, 0.0);
}

AC_TEST(ai_steering_separation_weights) {
  const double xs[2] = {1.0, 3.0};
  const double zs[2] = {0.0, 0.0};
  ai::SteeringOut out = ai::createSteeringOut();
  ai::separation(out, 0.0, 0.0, xs, zs, 2, 2.0, 1.0);  // 只有第 0 个在半径内
  AC_CHECK_EQ(out.x, -1.0);
  AC_CHECK_EQ(out.z, 0.0);
  ai::separation(out, 0.0, 0.0, xs, zs, 0, 2.0, 1.0);
  AC_CHECK_EQ(out.x, 0.0);
  ai::separation(out, 0.0, 0.0, xs, zs, 2, 0.0, 1.0);
  AC_CHECK_EQ(out.x, 0.0);
  // 零距离按 i%2 破平局：单个重合邻居 → normalize(1, -1, speed 1)
  const double zeroXs[1] = {0.0};
  const double zeroZs[1] = {0.0};
  ai::separation(out, 0.0, 0.0, zeroXs, zeroZs, 1, 2.0, 1.0);
  AC_CHECK_NEAR(out.x, std::sqrt(0.5), 1e-12);
  AC_CHECK_NEAR(out.z, -std::sqrt(0.5), 1e-12);
}

AC_TEST(ai_steering_obstacle_avoid_fence_and_barn) {
  const ac::config::ArenaConfig& arena = ac::config::kArenaConfig;
  ai::SteeringOut out = ai::createSteeringOut();
  ai::obstacleAvoid(out, 0.0, 0.0, 1.0, 0.0, arena, 0.5);
  AC_CHECK_EQ(out.x, 1.0);
  AC_CHECK_EQ(out.z, 0.0);
  // 围栏外：x = 45 远超 limit = 40 - 0.25 - 0.5 = 39.25 → 推回为负方向
  ai::obstacleAvoid(out, 45.0, 0.0, 1.0, 0.0, arena, 0.5);
  AC_CHECK_EQ(out.x, -1.0);
  AC_CHECK_EQ(out.z, 0.0);
  // 谷仓：x = 5.4 在扩展 AABB（barn.maxX + radius + 1 = 5.5）内 → 沿 -x 的行进被翻转
  ai::obstacleAvoid(out, 5.4, 0.0, -1.0, 0.0, arena, 0.5);
  AC_CHECK_EQ(out.x, 1.0);
  AC_CHECK_EQ(out.z, 0.0);
}

// ---------- §5.7 邻居收集与 flock ----------

AC_TEST(ai_flocking_gathers_twelve_nearest) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t self = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 0.0);
  uint16_t expectedIds[20];
  for (int i = 0; i < 20; ++i) {
    expectedIds[i] = spawnAt(*world, sim::EntityKind::kSheep, 0.1 * static_cast<double>(i + 1), 0.0);
  }
  sim::buildSpatialGrid(*world);
  ai::FlockNeighbors neighbors;
  const int32_t count = ai::gatherNeighbors(neighbors, *world, entityAt(*world, self), world->grid);
  AC_CHECK_EQ(count, 12);
  AC_CHECK_EQ(neighbors.count, 12);
  AC_CHECK_EQ(neighbors.capacity, 12);
  AC_CHECK_NEAR(neighbors.distSq[0], 0.01, 1e-12);
  AC_CHECK_NEAR(neighbors.distSq[11], 1.44, 1e-12);
  AC_CHECK_EQ(neighbors.ids[0], expectedIds[0]);
  AC_CHECK_EQ(neighbors.ids[11], expectedIds[11]);
  for (int i = 1; i < 12; ++i) {
    AC_CHECK(neighbors.distSq[i] >= neighbors.distSq[i - 1]);
  }
  AC_CHECK_NEAR(neighbors.centroidX, 0.65, 1e-12);  // 0.1..1.2 的均值
  AC_CHECK_NEAR(neighbors.centroidZ, 0.0, 1e-12);
}

AC_TEST(ai_flocking_force_weights) {
  ai::FlockNeighbors neighbors;
  ai::addNeighbor(neighbors, 1.0, 0.0, 0.0, 0.0);
  ai::finalizeNeighbors(neighbors);
  ai::SteeringOut out = ai::createSteeringOut();
  // 分离 (0-1)/1 = -1 → ×1.6；凝聚 (1-0)*0.5*0.1 = +0.05 → -1.55 → normalize 到 2
  ai::flockForce(out, 0.0, 0.0, neighbors, 2.0);
  AC_CHECK_NEAR(out.x, -2.0, 1e-12);
  AC_CHECK_NEAR(out.z, 0.0, 1e-12);

  ai::FlockNeighbors pair;
  ai::addNeighbor(pair, 1.0, 0.0, 1.0, 0.0);
  ai::addNeighbor(pair, 2.0, 0.0, 1.0, 0.0);
  ai::finalizeNeighbors(pair);
  // 分离 -1 + (0-2)/4 = -1.5 → 归一 (-1,0)；对齐 1×0.4；凝聚 ((1+2)/2)*0.05 = 0.075
  ai::flockForce(out, 0.0, 0.0, pair, 1.0);
  AC_CHECK_NEAR(out.x, -1.0, 1e-12);
  AC_CHECK_NEAR(out.z, 0.0, 1e-12);

  ai::FlockNeighbors empty;
  ai::flockForce(out, 0.0, 0.0, empty, 3.0);
  AC_CHECK_EQ(out.x, 0.0);
  AC_CHECK_EQ(out.z, 0.0);
}

AC_TEST(ai_neighbor_gather_skips_flock_and_far) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t self = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 0.0);
  const uint16_t near = spawnAt(*world, sim::EntityKind::kSheep, 2.0, 0.0);
  spawnAt(*world, sim::EntityKind::kSheep, 3.5, 0.0);   // 超出 3m 邻居半径
  spawnAt(*world, sim::EntityKind::kPlayer, 1.0, 0.0);  // 非羊
  spawnAt(*world, sim::EntityKind::kProjectile, 0.5, 0.0);
  spawnAt(*world, sim::EntityKind::kPickup, 1.5, 0.0);
  sim::buildSpatialGrid(*world);
  ai::FlockNeighbors neighbors;
  AC_CHECK_EQ(ai::gatherNeighbors(neighbors, *world, entityAt(*world, self), world->grid), 1);
  AC_CHECK_EQ(neighbors.ids[0], near);
  AC_CHECK_NEAR(neighbors.distSq[0], 4.0, 1e-12);
}

// ---------- §5.7 仇恨与目标选择 ----------

AC_TEST(ai_aggro_decay_and_hit_bonus) {
  ai::TargetState state;
  ai::resetTargetState(state);
  const uint16_t players[2] = {7u, 9u};
  state.aggro[0] = 1.0;
  state.aggro[1] = 0.009;
  ai::decayAggro(state, 2);
  AC_CHECK_EQ(state.aggro[0], 0.98);
  AC_CHECK_EQ(state.aggro[1], 0.0);  // 衰减后 < 0.01 → 归零
  ai::noteHit(state, players, 2, 9u);
  AC_CHECK_EQ(state.aggro[1], 20.0);
  ai::noteHit(state, players, 2, 5u);  // 不在玩家列表 → 丢弃
  AC_CHECK_EQ(state.aggro[0], 0.98);
  for (int i = 0; i < 12; ++i) ai::noteHit(state, players, 2, 7u);
  AC_CHECK(state.aggro[0] > 240.0);  // v1 无上限 clamp
  AC_CHECK_EQ(ai::aggroIndexOf(players, 2, 9u), 1);
  AC_CHECK_EQ(ai::aggroIndexOf(players, 2, 5u), -1);
}

AC_TEST(ai_target_selects_highest_score_past_ratio) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t sheep = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 20.0);
  const uint16_t nearPlayer = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 25.0);
  const uint16_t farPlayer = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 30.0);
  sim::EntityId ids[2] = {nearPlayer, farPlayer};
  ai::TargetState state;
  ai::resetTargetState(state);

  AC_CHECK_EQ(ai::selectTarget(state, *world, entityAt(*world, sheep), ids, 2u), nearPlayer);
  AC_CHECK_NEAR(state.targetDistanceM, 5.0, 1e-12);

  state.aggro[1] = 1.0;  // 远处玩家仇恨更高 → 切换
  AC_CHECK_EQ(ai::selectTarget(state, *world, entityAt(*world, sheep), ids, 2u), farPlayer);
  AC_CHECK_NEAR(state.targetDistanceM, 10.0, 1e-12);

  state.aggro[0] = 1.2;  // 1.2 + 1/6 = 1.3667 < 1.0 * 1.5 → 不切换
  AC_CHECK_EQ(ai::selectTarget(state, *world, entityAt(*world, sheep), ids, 2u), farPlayer);
  state.aggro[0] = 2.0;  // 2.1667 > 1.5 → 切换回近处玩家
  AC_CHECK_EQ(ai::selectTarget(state, *world, entityAt(*world, sheep), ids, 2u), nearPlayer);
  AC_CHECK_EQ(state.targetId, nearPlayer);
}

AC_TEST(ai_target_skips_player_behind_barn) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t sheep = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 10.0);
  const uint16_t behind = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, -10.0);
  const uint16_t front = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 6.0);
  AC_CHECK(!ai::isVisible(0.0, 0.6, 10.0, entityAt(*world, behind)));
  AC_CHECK(ai::isVisible(0.0, 0.6, 10.0, entityAt(*world, front)));
  AC_CHECK(ai::isVisible(0.0, 0.6, 10.0, entityAt(*world, sheep)));  // 非玩家不遮挡
  sim::EntityId ids[1] = {behind};
  ai::TargetState state;
  ai::resetTargetState(state);
  AC_CHECK_EQ(ai::selectTarget(state, *world, entityAt(*world, sheep), ids, 1u), 0u);
  AC_CHECK_EQ(state.targetDistanceM, 0.0);
}

// ---------- §5.3 行为与状态机 ----------

AC_TEST(ai_alert_arms_timer_once_then_chases) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t sheep = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 25.0);
  spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 20.0);

  AC_CHECK(ac::test::stepEmpty(*world));
  AC_CHECK_EQ(entityAt(*world, sheep).state, ac::config::sheepStateCode(SheepState::kAlert));
  AC_CHECK_EQ(entityAt(*world, sheep).ai.timerMs, 300.0);
  AC_CHECK_EQ(entityAt(*world, sheep).pos.z, 25.0);  // 警戒帧速度 0

  AC_CHECK(ac::test::stepEmpty(*world));
  AC_CHECK_EQ(entityAt(*world, sheep).ai.timerMs, 250.0);  // 同态转移不得重复武装
  AC_CHECK_EQ(entityAt(*world, sheep).pos.z, 25.0);

  for (int i = 0; i < 12; ++i) AC_CHECK(ac::test::stepEmpty(*world));
  AC_CHECK_EQ(entityAt(*world, sheep).state, ac::config::sheepStateCode(SheepState::kChase));
  AC_CHECK(25.0 - entityAt(*world, sheep).pos.z > 0.5);  // 追击后距离缩短
}

AC_TEST(ai_graze_reselects_on_ai_stream) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t sheep = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 25.0);
  const uint32_t aiBefore = world->rng.ai.a;
  const uint32_t fxBefore = world->rng.fx.a;

  aiTick(*world);
  sim::Entity& entity = entityAt(*world, sheep);
  AC_CHECK_EQ(entity.state, ac::config::sheepStateCode(SheepState::kGraze));
  AC_CHECK_EQ(entity.ai.timerMs, ac::config::kGrazeRepickMs);
  AC_CHECK(world->rng.ai.a != aiBefore);         // 吃草点重选消费 ai 流
  AC_CHECK_EQ(world->rng.fx.a, fxBefore);        // fx 流禁止被模拟读取
  AC_CHECK(std::fabs(entity.ai.grazeX) <= 6.0);
  AC_CHECK(std::fabs(entity.ai.grazeZ - 25.0) <= 6.0);
  const double speed = std::sqrt(entity.vel.x * entity.vel.x + entity.vel.z * entity.vel.z);
  AC_CHECK(speed > 0.0);
  AC_CHECK(speed <= 2.6 * ac::config::kGrazeSpeedRatio + 1e-12);

  const double grazeX = entity.ai.grazeX;
  const uint32_t aiAfter = world->rng.ai.a;
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, sheep).ai.timerMs, 2450.0);
  AC_CHECK_EQ(entityAt(*world, sheep).ai.grazeX, grazeX);
  AC_CHECK_EQ(world->rng.ai.a, aiAfter);  // 计时未到 → 不重抽
}

AC_TEST(ai_ram_windup_then_charge) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t ram = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 30.0);
  const uint16_t player = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 19.0);
  placeSheepAndPlayer(*world, SheepKind::kRam, ram, player);
  entityAt(*world, ram).state = ac::config::sheepStateCode(SheepState::kChase);  // 跳过 graze→alert→chase 的起步

  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, ram).state, ac::config::sheepStateCode(SheepState::kWindup));
  AC_CHECK_EQ(entityAt(*world, ram).ai.timerMs, 1000.0);
  AC_CHECK_EQ(entityAt(*world, ram).ai.chargeDirX, 0.0);
  AC_CHECK_EQ(entityAt(*world, ram).ai.chargeDirZ, -1.0);
  AC_CHECK_EQ(entityAt(*world, ram).vel.z, 0.0);

  for (int i = 1; i < 21; ++i) aiTick(*world);  // 蓄力 1000ms 后转冲锋
  AC_CHECK_EQ(entityAt(*world, ram).state, ac::config::sheepStateCode(SheepState::kCharge));
  AC_CHECK_EQ(entityAt(*world, ram).ai.timerMs, ac::config::kRamChargeMaxMs);
  AC_CHECK_EQ(entityAt(*world, ram).ai.chargeDirZ, -1.0);

  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, ram).ai.timerMs, 1550.0);
  AC_CHECK_EQ(entityAt(*world, ram).vel.z, -9.0);
  AC_CHECK_EQ(entityAt(*world, ram).vel.x, 0.0);

  const double zBefore = entityAt(*world, ram).pos.z;
  AC_CHECK(ac::sim::stepWorld(*world, nullptr, 0u, ac::config::kStepDtMs));
  AC_CHECK_NEAR(zBefore - entityAt(*world, ram).pos.z, 0.45, 1e-12);  // 9 m/s × 50ms
}

AC_TEST(ai_ram_staggers_on_timeout_or_bounds) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t ram = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 30.0);
  const uint16_t player = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 19.0);
  placeSheepAndPlayer(*world, SheepKind::kRam, ram, player);
  sim::Entity& first = entityAt(*world, ram);
  first.state = ac::config::sheepStateCode(SheepState::kCharge);
  first.ai.timerMs = 50.0;
  first.ai.chargeDirX = 0.0;
  first.ai.chargeDirZ = -1.0;
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, ram).state, ac::config::sheepStateCode(SheepState::kStagger));
  AC_CHECK_EQ(entityAt(*world, ram).ai.timerMs, ac::config::kSheepAi.chargeStaggerMs);
  AC_CHECK_EQ(entityAt(*world, ram).ai.chargeDirX, 0.0);
  AC_CHECK_EQ(entityAt(*world, ram).ai.chargeDirZ, 0.0);
  AC_CHECK_EQ(entityAt(*world, ram).vel.z, 0.0);
  AC_CHECK_EQ(entityAt(*world, ram).vel.x, 0.0);

  // 越界（|x| > 39.5）：即便计时未到也进硬直
  const uint16_t outRam = spawnAt(*world, sim::EntityKind::kSheep, 39.6, 0.0);
  const uint16_t farPlayer = spawnAt(*world, sim::EntityKind::kPlayer, 30.0, 0.0);
  placeSheepAndPlayer(*world, SheepKind::kRam, outRam, farPlayer);
  sim::Entity& second = entityAt(*world, outRam);
  second.state = ac::config::sheepStateCode(SheepState::kCharge);
  second.ai.timerMs = ac::config::kRamChargeMaxMs;
  second.ai.chargeDirX = -1.0;
  second.ai.chargeDirZ = 0.0;
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, outRam).state, ac::config::sheepStateCode(SheepState::kStagger));
  AC_CHECK_EQ(entityAt(*world, outRam).ai.chargeDirZ, 0.0);

  // 冲进谷仓 AABB：同样进硬直（现目标保留，不看遮挡）
  const uint16_t barnRam = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 1.0);
  const uint16_t barnPlayer = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 6.0);
  placeSheepAndPlayer(*world, SheepKind::kRam, barnRam, barnPlayer);
  sim::Entity& third = entityAt(*world, barnRam);
  third.state = ac::config::sheepStateCode(SheepState::kCharge);
  third.ai.timerMs = ac::config::kRamChargeMaxMs;
  third.ai.chargeDirX = 0.0;
  third.ai.chargeDirZ = 1.0;
  third.ai.target.targetId = barnPlayer;
  third.ai.target.targetDistanceM = 5.0;
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, barnRam).state, ac::config::sheepStateCode(SheepState::kStagger));
}

AC_TEST(ai_elite_keeps_distance_and_strafes) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t elite = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 30.0);
  const uint16_t player = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 20.0);
  placeSheepAndPlayer(*world, SheepKind::kElite, elite, player);
  entityAt(*world, elite).state = ac::config::sheepStateCode(SheepState::kChase);

  aiTick(*world);  // 距离 10 < 15 → 后退
  AC_CHECK_EQ(entityAt(*world, elite).state, ac::config::sheepStateCode(SheepState::kRanged));
  AC_CHECK_EQ(entityAt(*world, elite).vel.z, 2.4);
  AC_CHECK_EQ(entityAt(*world, elite).vel.x, 0.0);

  // 15–25m：侧移，1200ms 翻号
  entityAt(*world, player).pos.z = 50.0;  // 距离 20
  entityAt(*world, elite).state = ac::config::sheepStateCode(SheepState::kRanged);
  entityAt(*world, elite).ai.timerMs = 0.0;
  entityAt(*world, elite).ai.strafeSign = 1.0;
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, elite).vel.x, -1.2);
  AC_CHECK_EQ(entityAt(*world, elite).vel.z, 0.0);
  AC_CHECK_EQ(entityAt(*world, elite).ai.timerMs, ac::config::kEliteStrafeMs);
  AC_CHECK_EQ(entityAt(*world, elite).ai.strafeSign, -1.0);
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, elite).vel.x, 1.2);
  AC_CHECK_EQ(entityAt(*world, elite).ai.strafeSign, -1.0);

  // > 25m：前进
  entityAt(*world, player).pos.z = 56.0;  // 距离 26
  entityAt(*world, elite).state = ac::config::sheepStateCode(SheepState::kChase);
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, elite).vel.z, 2.4);
  AC_CHECK_EQ(entityAt(*world, elite).state, ac::config::sheepStateCode(SheepState::kChase));
}

AC_TEST(ai_grunt_enters_attack_within_reach) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t sheep = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 30.0);
  const uint16_t player = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 28.8);
  placeSheepAndPlayer(*world, SheepKind::kGrunt, sheep, player);
  entityAt(*world, sheep).state = ac::config::sheepStateCode(SheepState::kChase);

  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, sheep).state, ac::config::sheepStateCode(SheepState::kAttack));
  AC_CHECK_EQ(entityAt(*world, sheep).vel.x, 0.0);
  AC_CHECK_EQ(entityAt(*world, sheep).vel.z, 0.0);
  const ac::Vec3 facing = ac::yawPitchToDirection(entityAt(*world, sheep).yaw, 0.0);
  AC_CHECK_NEAR(facing.x, 0.0, 5e-3);
  AC_CHECK_NEAR(facing.z, -1.0, 5e-3);  // 朝向 -z 的目标

  // 距离 > 1.4 × 1.4 时回 chase
  entityAt(*world, player).pos.z = 25.0;  // 距离 5
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, sheep).state, ac::config::sheepStateCode(SheepState::kChase));
  AC_CHECK_NEAR(entityAt(*world, sheep).vel.z, -2.6, 1e-12);
}

// ---------- §5.6 命中结算 ----------

AC_TEST(ai_bite_deals_damage_cooldown_knockback) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t sheep = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 30.0);
  const uint16_t player = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 28.8);
  placeSheepAndPlayer(*world, SheepKind::kGrunt, sheep, player);
  sim::Entity& attacker = entityAt(*world, sheep);
  attacker.state = ac::config::sheepStateCode(SheepState::kAttack);
  attacker.ai.cooldownMs = 0.0;
  sim::EntityId ids[1] = {player};

  AC_CHECK_EQ(sim::resolveSheepAttacks(*world, ids, 1u), 2);
  const sim::Entity& victim = entityAt(*world, player);
  AC_CHECK_NEAR(victim.armor, 50.0 - 4.8, 1e-12);  // 8 × 0.6 由护甲吸收
  AC_CHECK_NEAR(victim.hp, 100.0 - 3.2, 1e-12);
  AC_CHECK_EQ(entityAt(*world, sheep).ai.cooldownMs, ac::config::kSheepAi.attackCooldownMs);
  AC_CHECK_NEAR(victim.knock.knockMs, ac::config::kBiteKnockbackM / 7.0 * 1000.0, 1e-9);
  AC_CHECK_EQ(victim.knock.knockVx, 0.0);
  AC_CHECK_EQ(victim.knock.knockVz, -7.0);
  AC_CHECK_EQ(countEvents(*world, sim::kEventPlayerHit), 1u);
  const sim::Event& hit = world->events[0];
  AC_CHECK_NEAR(hit.value, 8.0, 1e-12);
  AC_CHECK_EQ(hit.subjectId, sheep);
  AC_CHECK_EQ(hit.targetId, player);

  // 冷却未清零时不再咬
  AC_CHECK_EQ(sim::resolveSheepAttacks(*world, ids, 1u), 0);
  AC_CHECK_NEAR(entityAt(*world, player).hp, 96.8, 1e-12);
}

AC_TEST(ai_charge_hit_deals_22_and_staggers) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t ram = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 30.0);
  const uint16_t player = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 29.0);
  placeSheepAndPlayer(*world, SheepKind::kRam, ram, player);
  sim::Entity& attacker = entityAt(*world, ram);
  attacker.state = ac::config::sheepStateCode(SheepState::kCharge);
  attacker.ai.chargeDirX = 0.0;
  attacker.ai.chargeDirZ = -1.0;
  sim::EntityId ids[1] = {player};

  AC_CHECK_EQ(sim::resolveSheepAttacks(*world, ids, 1u), 2);
  const sim::Entity& victim = entityAt(*world, player);
  AC_CHECK_NEAR(victim.armor, 50.0 - 22.0 * 0.6, 1e-12);
  AC_CHECK_NEAR(victim.hp, 100.0 - 22.0 * 0.4, 1e-12);
  AC_CHECK_NEAR(victim.knock.knockMs, ac::config::kChargeKnockbackM / 7.0 * 1000.0, 1e-9);
  AC_CHECK_EQ(victim.knock.knockVz, -7.0);
  AC_CHECK_EQ(entityAt(*world, ram).state, ac::config::sheepStateCode(SheepState::kStagger));
  AC_CHECK_EQ(entityAt(*world, ram).ai.timerMs, ac::config::kSheepAi.chargeStaggerMs);
  AC_CHECK_EQ(entityAt(*world, ram).ai.chargeDirZ, 0.0);
}

AC_TEST(ai_question_bolt_spawns_hits_and_expires) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t elite = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 30.0);
  const uint16_t player = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 10.0);
  placeSheepAndPlayer(*world, SheepKind::kElite, elite, player);
  entityAt(*world, elite).state = ac::config::sheepStateCode(SheepState::kRanged);
  entityAt(*world, elite).ai.cooldownMs = 0.0;
  sim::EntityId ids[1] = {player};

  AC_CHECK_EQ(sim::resolveEliteFire(*world, ids, 1u), 1);
  const uint16_t questionBoltId = findProjectileOf(*world, elite);
  AC_CHECK(questionBoltId != 0u);
  if (questionBoltId == 0u) return;
  const sim::Entity& questionBolt = entityAt(*world, questionBoltId);
  AC_CHECK_EQ(questionBolt.vel.z, -14.0);
  AC_CHECK_EQ(questionBolt.vel.x, 0.0);
  AC_CHECK_EQ(questionBolt.pos.y, ac::config::kQuestionBoltSpawnHeightM);
  AC_CHECK_EQ(questionBolt.pos.z, 30.0);
  AC_CHECK_EQ(questionBolt.team, 0u);
  AC_CHECK_EQ(entityAt(*world, elite).ai.cooldownMs, ac::config::kSheepAi.eliteBoltCooldownMs);
  const ac::Vec3 eliteFacing = ac::yawPitchToDirection(entityAt(*world, elite).yaw, 0.0);
  AC_CHECK_NEAR(eliteFacing.z, -1.0, 5e-3);

  // 每 tick 位移 14 m/s × 50ms = 0.7（静态碰撞不作用于投射物）
  AC_CHECK(ac::test::stepEmpty(*world));
  const sim::Entity* moved = sim::entityById(*world, questionBoltId);
  AC_CHECK(moved != nullptr);
  if (moved == nullptr) return;
  AC_CHECK_NEAR(moved->pos.z, 30.0 - 0.7, 1e-12);

  // 命中半径 0.62 / 0.63
  const uint16_t nearQuestionBolt = spawnAt(*world, sim::EntityKind::kProjectile, 0.0, 30.0);
  entityAt(*world, player).pos.x = 0.62;
  entityAt(*world, player).pos.z = 30.0;
  AC_CHECK_EQ(sim::advanceProjectiles(*world, 50u, ids, 1u), 2);
  AC_CHECK(sim::entityById(*world, nearQuestionBolt) == nullptr);
  AC_CHECK_NEAR(entityAt(*world, player).armor, 50.0 - 8.4, 1e-12);
  AC_CHECK_NEAR(entityAt(*world, player).hp, 100.0 - 5.6, 1e-12);

  const uint16_t missQuestionBolt = spawnAt(*world, sim::EntityKind::kProjectile, 0.0, 30.0);
  entityAt(*world, player).pos.x = 0.63;
  entityAt(*world, player).pos.z = 30.0;
  AC_CHECK_EQ(sim::advanceProjectiles(*world, 50u, ids, 1u), 0);
  AC_CHECK(sim::entityById(*world, missQuestionBolt) != nullptr);

  // 寿命 3000ms
  sim::Entity* lived = sim::entityById(*world, missQuestionBolt);
  AC_CHECK(lived != nullptr);
  if (lived == nullptr) return;
  lived->aliveMs = 2950u;
  AC_CHECK_EQ(sim::advanceProjectiles(*world, 50u, ids, 1u), 0);
  AC_CHECK(sim::entityById(*world, missQuestionBolt) == nullptr);

  // 谷仓内与越界即销毁
  const uint16_t inBarn = spawnAt(*world, sim::EntityKind::kProjectile, 0.0, 0.0);
  sim::advanceProjectiles(*world, 50u, ids, 1u);
  AC_CHECK(sim::entityById(*world, inBarn) == nullptr);
  const uint16_t outField = spawnAt(*world, sim::EntityKind::kProjectile, 39.6, 0.0);
  sim::advanceProjectiles(*world, 50u, ids, 1u);
  AC_CHECK(sim::entityById(*world, outField) == nullptr);
  const uint16_t alive = spawnAt(*world, sim::EntityKind::kProjectile, 20.0, 20.0);
  sim::advanceProjectiles(*world, 50u, ids, 1u);
  AC_CHECK(sim::entityById(*world, alive) != nullptr);
}

// ---------- §5.5 羊王 ----------

AC_TEST(ai_king_phase_switch_and_summon) {
  AC_CHECK_EQ(ac::ai::kingPhaseFor(1.0), 1);
  AC_CHECK_EQ(ac::ai::kingPhaseFor(0.70), 1);
  AC_CHECK_EQ(ac::ai::kingPhaseFor(0.66), 2);
  AC_CHECK_EQ(ac::ai::kingPhaseFor(0.5), 2);
  AC_CHECK_EQ(ac::ai::kingPhaseFor(0.34), 2);
  AC_CHECK_EQ(ac::ai::kingPhaseFor(0.33), 3);
  AC_CHECK_EQ(ac::ai::kingPhaseFor(0.0), 3);
  AC_CHECK_EQ(ac::ai::kingStateFor(1), ac::config::sheepStateCode(SheepState::kKingPhase1));
  AC_CHECK_EQ(ac::ai::kingStateFor(2), ac::config::sheepStateCode(SheepState::kKingPhase2));
  AC_CHECK_EQ(ac::ai::kingStateFor(3), ac::config::sheepStateCode(SheepState::kKingPhase3));

  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t king = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 20.0);
  ai::applySheepKind(entityAt(*world, king), SheepKind::kKing);
  // graze 没有指向 kingPhase* 的出边，阶段切换必须经 setSheepState 才生效（v1 同口径）
  entityAt(*world, king).state = ac::config::sheepStateCode(SheepState::kKingPhase1);
  entityAt(*world, king).hp = 2400.0 * 0.5;
  const uint32_t spawnBefore = world->rng.spawn.a;

  AC_CHECK_EQ(sim::updateKing(*world, entityAt(*world, king), ac::config::kStepDtMs), 0);
  AC_CHECK_EQ(entityAt(*world, king).ai.phase, 2);
  AC_CHECK_EQ(entityAt(*world, king).state, ac::config::sheepStateCode(SheepState::kKingPhase2));
  AC_CHECK_EQ(entityAt(*world, king).ai.summonMs, 7950.0);
  AC_CHECK_EQ(world->activeCount, 1u);

  const uint32_t before = world->activeCount;
  entityAt(*world, king).ai.summonMs = 0.0;
  AC_CHECK_EQ(sim::updateKing(*world, entityAt(*world, king), ac::config::kStepDtMs), 4);
  AC_CHECK_EQ(world->activeCount, before + 4u);
  AC_CHECK_EQ(entityAt(*world, king).ai.summonMs, ac::config::kSheepAi.kingSummonIntervalMs);
  AC_CHECK_EQ(entityAt(*world, king).ai.summoned, 4);
  AC_CHECK(world->rng.spawn.a != spawnBefore);
  const double angles[4] = {0.0, ac::kPi / 2.0, ac::kPi, 3.0 * ac::kPi / 2.0};
  int seen = 0;
  for (std::size_t i = 0u; i < world->activeCount; ++i) {
    const sim::Entity* grunt = sim::entityById(*world, world->activeIds[i]);
    if (grunt == nullptr || grunt->id == king) continue;
    AC_CHECK_EQ(grunt->kind, sim::EntityKind::kSheep);
    AC_CHECK_EQ(grunt->sheepKind, static_cast<uint8_t>(SheepKind::kGrunt));
    AC_CHECK_EQ(grunt->hp, 60.0);
    AC_CHECK_EQ(grunt->maxHp, 60.0);
    AC_CHECK_EQ(grunt->team, 1u);
    AC_CHECK_EQ(grunt->state, ac::config::sheepStateCode(SheepState::kGraze));
    const double dx = grunt->pos.x - 0.0;
    const double dz = grunt->pos.z - 20.0;
    const double radius = std::sqrt(dx * dx + dz * dz);
    AC_CHECK(radius > 2.0 && radius < 3.2);
    const double angle = std::atan2(dx, dz);
    bool matched = false;
    for (double expected : angles) {
      double delta = angle - expected;
      while (delta > ac::kPi) delta -= ac::kTwoPi;
      while (delta < -ac::kPi) delta += ac::kTwoPi;
      if (std::fabs(delta) < 0.2) matched = true;
    }
    AC_CHECK(matched);
    ++seen;
  }
  AC_CHECK_EQ(seen, 4);

  // phase 3：速度 ×1.4
  entityAt(*world, king).hp = 2400.0 * 0.2;
  AC_CHECK_EQ(sim::updateKing(*world, entityAt(*world, king), ac::config::kStepDtMs), 0);
  AC_CHECK_EQ(entityAt(*world, king).ai.phase, 3);
  AC_CHECK_EQ(entityAt(*world, king).state, ac::config::sheepStateCode(SheepState::kKingPhase3));
  AC_CHECK_NEAR(ac::ai::kingSpeedMultiplier(3), 1.4, 1e-12);
  AC_CHECK_EQ(ac::ai::kingSpeedMultiplier(1), 1.0);
  AC_CHECK_EQ(ac::ai::kingAttackCooldownMs(1), 1200.0);
  AC_CHECK_NEAR(ac::ai::kingAttackCooldownMs(3), 840.0, 1e-9);

  // phase 3 的 speed ×1.4 走意图内联（v1 sheepBrain 同款）：2.0 × 1.4 = 2.8 m/s
  const uint16_t king3 = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 30.0);
  const uint16_t nearPlayer = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 20.0);
  ai::applySheepKind(entityAt(*world, king3), SheepKind::kKing);
  entityAt(*world, king3).ai.phase = 3;
  entityAt(*world, king3).state = ac::config::sheepStateCode(SheepState::kKingPhase3);
  AC_CHECK_EQ(ai::sheepKindOf(entityAt(*world, king3)), SheepKind::kKing);
  AC_CHECK(nearPlayer != 0u);
  aiTick(*world);
  AC_CHECK_NEAR(entityAt(*world, king3).vel.z, -2.8, 1e-12);
  AC_CHECK_NEAR(entityAt(*world, king3).vel.x, 0.0, 1e-12);
}

// v1 的两趟结构里死亡羊在阶段 4 被 continue 掉（不进意图表），所以阶段 5 的回收分支不可达；
// 这里按 v1 逐字验证：只累加 timerMs、不改速度、不回收（S08 击杀即回收，dead 态实际到不了）。
AC_TEST(ai_dead_sheep_timer_accumulates) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t sheep = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 20.0);
  sim::Entity& entity = entityAt(*world, sheep);
  entity.state = ac::config::sheepStateCode(SheepState::kDead);
  entity.vel.x = 3.0;
  entity.vel.z = 3.0;
  entity.ai.timerMs = 0.0;

  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, sheep).ai.timerMs, 50.0);
  AC_CHECK_EQ(entityAt(*world, sheep).vel.x, 3.0);
  AC_CHECK_EQ(entityAt(*world, sheep).vel.z, 3.0);
  AC_CHECK_EQ(entityAt(*world, sheep).state, ac::config::sheepStateCode(SheepState::kDead));

  entityAt(*world, sheep).ai.timerMs = 1450.0;
  aiTick(*world);
  AC_CHECK_EQ(entityAt(*world, sheep).ai.timerMs, 1500.0);
  AC_CHECK(sim::entityById(*world, sheep) != nullptr);
}

AC_TEST(ai_spawn_teams_make_sheep_hostile) {
  AC_CHECK_EQ(ac::config::kDefaultTeamByKind[0], 0u);
  AC_CHECK_EQ(ac::config::kDefaultTeamByKind[1], 1u);
  AC_CHECK_EQ(ac::config::kDefaultTeamByKind[2], 0u);
  AC_CHECK_EQ(ac::config::kDefaultTeamByKind[3], 0u);

  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t sheep = spawnAt(*world, sim::EntityKind::kSheep, 0.0, 15.0);
  const uint16_t player = spawnAt(*world, sim::EntityKind::kPlayer, 0.0, 35.0);
  AC_CHECK_EQ(entityAt(*world, sheep).team, 1u);
  AC_CHECK_EQ(entityAt(*world, player).team, 0u);

  // 敌对关系可结算：同队会被 kFriendlyFire=false 挡掉，这里必须掉血
  sim::Command command{};
  command.seq = 1u;
  command.yaw = ac::kPi;  // yaw=0 指向 +Z，朝 -Z 射击用 pi
  command.pitch = -std::atan2(ac::config::kEyeHeightM - 0.56, 20.0);
  command.buttons = ac::config::kButtonFire;
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, ac::config::kStepDtMs));
  const sim::Entity* target = sim::entityById(*world, sheep);
  AC_CHECK(target != nullptr);
  if (target != nullptr) AC_CHECK(target->hp < 60.0);
  AC_CHECK_EQ(countEvents(*world, sim::kEventPlayerHit), 1u);
}

// ---------- §5.7 确定性 ----------

AC_TEST(ai_two_runs_same_seed_are_bit_identical) {
  std::unique_ptr<sim::World> worldA = sim::createWorld(kSeed);
  std::unique_ptr<sim::World> worldB = sim::createWorld(kSeed);
  for (sim::World* world : {worldA.get(), worldB.get()}) {
    for (int p = 0; p < 4; ++p) {
      spawnAt(*world, sim::EntityKind::kPlayer, -3.0 + 2.0 * static_cast<double>(p), 20.0);
    }
    for (int i = 0; i < 60; ++i) {
      const uint16_t id = spawnAt(*world, sim::EntityKind::kSheep,
                                  -9.0 + 2.0 * static_cast<double>(i % 10),
                                  2.0 * static_cast<double>(i / 10));
      ai::applySheepKind(entityAt(*world, id), static_cast<SheepKind>(i % 4));
    }
  }
  const uint32_t fxBefore = worldA->rng.fx.a;

  for (uint32_t tick = 0u; tick < 600u; ++tick) {
    sim::Command commands[4]{};
    for (int i = 0; i < 4; ++i) {
      commands[i].seq = static_cast<uint16_t>(tick);
      commands[i].moveX = static_cast<double>((tick + static_cast<uint32_t>(i)) % 3u) - 1.0;
      commands[i].moveY = static_cast<double>((tick / 3u + static_cast<uint32_t>(i)) % 3u) - 1.0;
    }
    AC_CHECK(ac::sim::stepWorld(*worldA, commands, 4u, ac::config::kStepDtMs));
    AC_CHECK(ac::sim::stepWorld(*worldB, commands, 4u, ac::config::kStepDtMs));
  }

  AC_CHECK_EQ(worldA->rng.ai.a, worldB->rng.ai.a);
  AC_CHECK_EQ(worldA->rng.spawn.a, worldB->rng.spawn.a);
  AC_CHECK_EQ(worldA->rng.fx.a, worldB->rng.fx.a);
  AC_CHECK_EQ(worldA->rng.fx.a, fxBefore);  // 模拟从不读 fx 流
  AC_CHECK_EQ(worldA->activeCount, worldB->activeCount);
  AC_CHECK_EQ(worldA->eventCount, worldB->eventCount);
  AC_CHECK_EQ(worldA->stats.aliveSheep, worldB->stats.aliveSheep);
  AC_CHECK_EQ(worldA->tick, worldB->tick);
  AC_CHECK(worldA->activeCount > 40u);

  for (std::size_t i = 0u; i < worldA->activeCount; ++i) {
    const uint16_t id = worldA->activeIds[i];
    AC_CHECK_EQ(id, worldB->activeIds[i]);
    const sim::Entity* a = sim::entityById(*worldA, id);
    const sim::Entity* b = sim::entityById(*worldB, id);
    AC_CHECK(a != nullptr && b != nullptr);
    if (a == nullptr || b == nullptr) return;
    AC_CHECK_EQ(bitsOf(a->pos.x), bitsOf(b->pos.x));
    AC_CHECK_EQ(bitsOf(a->pos.y), bitsOf(b->pos.y));
    AC_CHECK_EQ(bitsOf(a->pos.z), bitsOf(b->pos.z));
    AC_CHECK_EQ(bitsOf(a->vel.x), bitsOf(b->vel.x));
    AC_CHECK_EQ(bitsOf(a->vel.y), bitsOf(b->vel.y));
    AC_CHECK_EQ(bitsOf(a->vel.z), bitsOf(b->vel.z));
    AC_CHECK_EQ(bitsOf(a->yaw), bitsOf(b->yaw));
    AC_CHECK_EQ(bitsOf(a->hp), bitsOf(b->hp));
    AC_CHECK_EQ(bitsOf(a->armor), bitsOf(b->armor));
    AC_CHECK_EQ(a->state, b->state);
    AC_CHECK_EQ(a->sheepKind, b->sheepKind);
    AC_CHECK_EQ(a->aliveMs, b->aliveMs);
    AC_CHECK_EQ(a->idle, b->idle);
    AC_CHECK_EQ(bitsOf(a->ai.timerMs), bitsOf(b->ai.timerMs));
    AC_CHECK_EQ(bitsOf(a->ai.cooldownMs), bitsOf(b->ai.cooldownMs));
    AC_CHECK_EQ(bitsOf(a->ai.chargeDirX), bitsOf(b->ai.chargeDirX));
    AC_CHECK_EQ(bitsOf(a->ai.chargeDirZ), bitsOf(b->ai.chargeDirZ));
    AC_CHECK_EQ(bitsOf(a->ai.grazeX), bitsOf(b->ai.grazeX));
    AC_CHECK_EQ(bitsOf(a->ai.strafeSign), bitsOf(b->ai.strafeSign));
    AC_CHECK_EQ(a->ai.phase, b->ai.phase);
    AC_CHECK_EQ(a->ai.target.targetId, b->ai.target.targetId);
    AC_CHECK_EQ(bitsOf(a->ai.target.targetDistanceM), bitsOf(b->ai.target.targetDistanceM));
    AC_CHECK_EQ(a->ai.flock.count, b->ai.flock.count);
    AC_CHECK_EQ(bitsOf(a->knock.knockMs), bitsOf(b->knock.knockMs));
  }
}

AC_TEST(ai_swarm_tick_loop_is_heap_clean) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  for (int p = 0; p < 4; ++p) {
    spawnAt(*world, sim::EntityKind::kPlayer, -3.0 + 2.0 * static_cast<double>(p), 20.0);
  }
  for (int i = 0; i < 60; ++i) {
    const uint16_t id = spawnAt(*world, sim::EntityKind::kSheep,
                                -9.0 + 2.0 * static_cast<double>(i % 10),
                                2.0 * static_cast<double>(i / 10));
    ai::applySheepKind(entityAt(*world, id), static_cast<SheepKind>(i % 4));
  }

  for (int warmup = 0; warmup < 10; ++warmup) {
    AC_CHECK(ac::sim::stepWorld(*world, nullptr, 0u, ac::config::kStepDtMs));
  }
  ac::test::AllocationScope scope;
  for (int tick = 0; tick < 600; ++tick) {
    AC_CHECK(ac::sim::stepWorld(*world, nullptr, 0u, ac::config::kStepDtMs));
  }
  const std::size_t allocations = scope.since();
  std::printf("aiSwarmTicks=600 sheep=60 allocations=%zu\n", allocations);
  AC_CHECK_EQ(allocations, 0u);
}

}  // namespace
