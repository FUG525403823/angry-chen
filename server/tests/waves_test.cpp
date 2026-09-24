// S09 §6：波次预算与导演的断言（--filter=waves）。
#include "tiny_test.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "config/player.hpp"
#include "config/sheep.hpp"
#include "config/waves.hpp"
#include "core/rng.hpp"
#include "sim/arena.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"
#include "waves/director.hpp"

namespace {

namespace sim = ac::sim;
namespace waves = ac::waves;

using ac::config::SheepKind;

constexpr uint32_t kSeed = 0x5EA9u;

uint16_t spawnKind(sim::World& world, sim::EntityKind kind, double x, double z) {
  const sim::SpawnResult spawned = sim::spawnEntity(world, kind, ac::Vec3{x, 0.0, z});
  AC_CHECK(spawned.isOk);
  return spawned.id;
}

std::size_t countEvents(const sim::World& world, uint8_t type) {
  std::size_t count = 0u;
  for (std::size_t i = 0u; i < world.eventCount; ++i) {
    if (world.events[i].type == type) ++count;
  }
  return count;
}

std::size_t activeSheep(const sim::World& world) {
  std::size_t count = 0u;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const sim::Entity* entity = sim::entityById(world, world.activeIds[i]);
    if (entity != nullptr && entity->kind == sim::EntityKind::kSheep) ++count;
  }
  return count;
}

// 清空全部羊并把统计口径归零（导演的清波条件读 stats.aliveSheep）。
void killAllSheep(sim::World& world) {
  for (std::size_t i = world.activeCount; i-- > 0u;) {
    const sim::Entity* entity = sim::entityById(world, world.activeIds[i]);
    if (entity == nullptr || entity->kind != sim::EntityKind::kSheep) continue;
    sim::despawnEntity(world, entity->id);
  }
  world.stats.aliveSheep = 0u;
}

// ---------- §5.4 预算表 ----------

AC_TEST(waves_base_budget_samples) {
  AC_CHECK_EQ(ac::config::waveBaseBudget(1), 9);
  AC_CHECK_EQ(ac::config::waveBaseBudget(2), 13);
  AC_CHECK_EQ(ac::config::waveBaseBudget(3), 17);
  AC_CHECK_EQ(ac::config::waveBaseBudget(5), 27);
  AC_CHECK_EQ(ac::config::waveBaseBudget(10), 56);
  AC_CHECK_EQ(ac::config::roundHalfUp(0.5), 1);
  AC_CHECK_EQ(ac::config::roundHalfUp(-0.5), 0);
  AC_CHECK_EQ(ac::config::roundHalfUp(1.49), 1);
  AC_CHECK_EQ(ac::config::roundHalfUp(1.5), 2);
  AC_CHECK_EQ(ac::config::waveBaseBudget(0), 6);
}

AC_TEST(waves_player_scaling_and_speed_multiplier) {
  AC_CHECK_EQ(ac::config::waveBudget(5, 1), 27);
  AC_CHECK_EQ(ac::config::waveBudget(5, 4), 55);   // round(27 * 2.05)
  AC_CHECK_EQ(ac::config::waveBudget(1, 4), 18);   // round(9 * 2.05)
  AC_CHECK_EQ(ac::config::waveBudget(1, 0), 9);    // 人数下限 1
  AC_CHECK_NEAR(ac::config::sheepSpeedMultiplier(1), 1.0, 1e-12);
  AC_CHECK_NEAR(ac::config::sheepSpeedMultiplier(4), 1.06, 1e-12);
  AC_CHECK_NEAR(ac::config::sheepSpeedMultiplier(0), 1.0, 1e-12);
}

AC_TEST(waves_boss_wave_and_kind_unlock) {
  AC_CHECK(ac::config::isBossWave(5));
  AC_CHECK(ac::config::isBossWave(10));
  AC_CHECK(!ac::config::isBossWave(4));
  AC_CHECK(!ac::config::isBossWave(0));
  AC_CHECK(!ac::config::isBossWave(11));
  AC_CHECK_EQ(ac::config::firstWaveFor(SheepKind::kGrunt), 1);
  AC_CHECK_EQ(ac::config::firstWaveFor(SheepKind::kRam), 3);
  AC_CHECK_EQ(ac::config::firstWaveFor(SheepKind::kElite), 5);
  AC_CHECK_EQ(ac::config::firstWaveFor(SheepKind::kKing), 5);
  AC_CHECK(ac::config::kindAllowedAt(SheepKind::kGrunt, 1));
  AC_CHECK(!ac::config::kindAllowedAt(SheepKind::kRam, 2));
  AC_CHECK(ac::config::kindAllowedAt(SheepKind::kRam, 3));
  AC_CHECK(!ac::config::kindAllowedAt(SheepKind::kElite, 4));
  AC_CHECK(ac::config::kindAllowedAt(SheepKind::kElite, 5));
  AC_CHECK(!ac::config::kindAllowedAt(SheepKind::kKing, 4));
  AC_CHECK(ac::config::kindAllowedAt(SheepKind::kKing, 5));
}

AC_TEST(waves_constants_are_frozen) {
  AC_CHECK_EQ(ac::config::kWaveMax, 10);
  AC_CHECK_EQ(ac::config::kWaveIntermissionMs, 20000.0);
  AC_CHECK_EQ(ac::config::kWaveIntermissionMinMs, 5000.0);
  AC_CHECK_EQ(ac::config::kBudgetScalePerExtraPlayer, 0.35);
  AC_CHECK_EQ(ac::config::kSpeedScalePerExtraPlayer, 0.02);
  AC_CHECK_EQ(ac::config::kMaxSpawnsPerTick, 8);
  AC_CHECK_EQ(ac::config::kMaxActiveSpawnPoints, 3);
  AC_CHECK_EQ(ac::config::kMinSpawnDistanceM, 15.0);
  AC_CHECK_EQ(ac::config::sheepPrice(SheepKind::kGrunt), 1.0);
  AC_CHECK_EQ(ac::config::sheepPrice(SheepKind::kRam), 3.0);
  AC_CHECK_EQ(ac::config::sheepPrice(SheepKind::kElite), 6.0);
  AC_CHECK_EQ(ac::config::sheepPrice(SheepKind::kKing), 20.0);
  AC_CHECK_EQ(waves::kDirectorKindCount, 4);
  AC_CHECK_EQ(ac::sim::arena::kSpawnPoints.size(), 12u);
}

// ---------- §5.4 组队 ----------

AC_TEST(waves_plan_wave_five_one_player) {
  waves::DirectorState state = waves::createDirectorState();
  AC_CHECK_EQ(waves::planWave(state, 5, 1), 3);
  AC_CHECK_EQ(state.wave, 5);
  AC_CHECK_EQ(state.planned, 3);
  AC_CHECK_EQ(state.spawned, 0);
  AC_CHECK_EQ(state.cursor, 0);
  AC_CHECK(waves::planCountFor(state, SheepKind::kKing) == 1);
  AC_CHECK(waves::planCountFor(state, SheepKind::kElite) == 1);
  AC_CHECK(waves::planCountFor(state, SheepKind::kRam) == 0);
  AC_CHECK(waves::planCountFor(state, SheepKind::kGrunt) == 1);
  AC_CHECK_EQ(waves::plannedBudgetSum(state), 27);
  AC_CHECK_EQ(waves::plannedBudgetSum(state), ac::config::waveBudget(5, 1));
  AC_CHECK(state.isWaveStartPending);
}

AC_TEST(waves_plan_ten_one_player) {
  waves::DirectorState state = waves::createDirectorState();
  AC_CHECK_EQ(waves::planWave(state, 10, 1), 11);
  AC_CHECK(waves::planCountFor(state, SheepKind::kKing) == 1);
  AC_CHECK(waves::planCountFor(state, SheepKind::kElite) == 4);
  AC_CHECK(waves::planCountFor(state, SheepKind::kRam) == 3);
  AC_CHECK(waves::planCountFor(state, SheepKind::kGrunt) == 3);
  AC_CHECK_EQ(waves::plannedBudgetSum(state), 56);
}

AC_TEST(waves_plan_never_exceeds_budget) {
  for (int32_t wave = 1; wave <= ac::config::kWaveMax; ++wave) {
    for (int32_t players = 1; players <= 4; ++players) {
      waves::DirectorState state = waves::createDirectorState();
      const int32_t planned = waves::planWave(state, wave, players);
      int32_t sum = 0;
      for (int32_t kind = 0; kind < waves::kDirectorKindCount; ++kind) sum += state.plan[kind];
      AC_CHECK_EQ(planned, sum);
      AC_CHECK(planned >= 1);
      AC_CHECK(waves::plannedBudgetSum(state) <= ac::config::waveBudget(wave, players));
      AC_CHECK(state.isWaveStartPending);
      // 低波次不得出现未解锁羊形
      AC_CHECK(waves::planCountFor(state, SheepKind::kRam) == 0 || wave >= 3);
      AC_CHECK(waves::planCountFor(state, SheepKind::kElite) == 0 || wave >= 5);
      AC_CHECK(waves::planCountFor(state, SheepKind::kKing) == 0 || ac::config::isBossWave(wave));
    }
  }
}

// ---------- §5.4 生成节流与出生点 ----------

AC_TEST(waves_spawn_points_keep_distance) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  sim::EntityId ids[12];
  uint32_t idCount = 0u;
  for (int i = 0; i < 4; ++i) {
    const ac::Vec3& point = ac::sim::arena::kSpawnPoints[static_cast<std::size_t>(i)];
    ids[idCount] = spawnKind(*world, sim::EntityKind::kPlayer, point.x, point.z);
    ++idCount;
  }
  ac::Rng rng = ac::createRng(7u, ac::RngStream::kSpawn);
  int32_t out[ac::config::kMaxActiveSpawnPoints];
  const int32_t count = waves::selectSpawnPointIndices(*world, ids, idCount, rng, out,
                                                      ac::config::kMaxActiveSpawnPoints);
  AC_CHECK_EQ(count, ac::config::kMaxActiveSpawnPoints);
  for (int32_t i = 0; i < count; ++i) {
    AC_CHECK(out[i] >= 4);  // 前 4 个点上有玩家，必被跳过
    const ac::Vec3& point = ac::sim::arena::kSpawnPoints[static_cast<std::size_t>(out[i])];
    AC_CHECK(waves::nearestPlayerDistanceM(*world, point.x, point.z, ids, idCount) >
             ac::config::kMinSpawnDistanceM);
  }
  for (int32_t i = 1; i < count; ++i) AC_CHECK(out[i] != out[i - 1]);

  // 12 个点全被玩家占住 → 无可选点
  std::unique_ptr<sim::World> crowded = sim::createWorld(kSeed);
  sim::EntityId all[12];
  for (int i = 0; i < 12; ++i) {
    const ac::Vec3& point = ac::sim::arena::kSpawnPoints[static_cast<std::size_t>(i)];
    all[i] = spawnKind(*crowded, sim::EntityKind::kPlayer, point.x, point.z);
  }
  ac::Rng rng2 = ac::createRng(7u, ac::RngStream::kSpawn);
  AC_CHECK_EQ(waves::selectSpawnPointIndices(*crowded, all, 12u, rng2, out,
                                             ac::config::kMaxActiveSpawnPoints),
              0);
}

AC_TEST(waves_director_throttles_spawns_per_tick) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t player = spawnKind(*world, sim::EntityKind::kPlayer, 0.0, 0.0);
  sim::EntityId ids[1] = {player};
  waves::DirectorState state = waves::createDirectorState();
  AC_CHECK_EQ(waves::planWave(state, 10, 1), 11);

  int32_t total = 0;
  for (int tick = 0; tick < 20; ++tick) {
    const waves::DirectorTick result =
        waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
    AC_CHECK(result.spawned <= ac::config::kMaxSpawnsPerTick);
    total += result.spawned;
  }
  AC_CHECK_EQ(total, 11);
  AC_CHECK_EQ(state.spawned, 11);
  AC_CHECK_EQ(activeSheep(*world), 11u);
  AC_CHECK_EQ(world->stats.aliveSheep, 11u);
  // 全部出生点都在 12 个环点之一 ±1.5m 内
  for (std::size_t i = 0u; i < world->activeCount; ++i) {
    const sim::Entity* sheep = sim::entityById(*world, world->activeIds[i]);
    if (sheep == nullptr || sheep->kind != sim::EntityKind::kSheep) continue;
    bool matched = false;
    for (const ac::Vec3& point : ac::sim::arena::kSpawnPoints) {
      if (std::fabs(sheep->pos.x - point.x) <= 1.5 && std::fabs(sheep->pos.z - point.z) <= 1.5) {
        matched = true;
      }
    }
    AC_CHECK(matched);
  }
}

AC_TEST(waves_director_emits_wave_start_then_clears) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t player = spawnKind(*world, sim::EntityKind::kPlayer, 0.0, 0.0);
  sim::EntityId ids[1] = {player};
  waves::DirectorState state = waves::createDirectorState();
  AC_CHECK_EQ(waves::planWave(state, 1, 1), 9);  // w=1 只有咩咩兵

  world->eventCount = 0u;  // 直连导演（不经 stepWorld）时自行清事件缓冲
  const waves::DirectorTick first =
      waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
  AC_CHECK(first.isWaveStart);
  AC_CHECK_EQ(first.spawned, 8);
  AC_CHECK_EQ(countEvents(*world, sim::kEventWaveStart), 1u);
  AC_CHECK_EQ(world->events[0].value, 1.0);

  world->eventCount = 0u;
  const waves::DirectorTick second =
      waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
  AC_CHECK(!second.isWaveStart);  // waveStart 只发一次
  AC_CHECK_EQ(second.spawned, 1);
  AC_CHECK_EQ(countEvents(*world, sim::kEventWaveStart), 0u);
  AC_CHECK_EQ(state.spawned, state.planned);
  AC_CHECK(!second.isWaveClear);

  killAllSheep(*world);
  world->eventCount = 0u;
  const waves::DirectorTick third =
      waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
  AC_CHECK(third.isWaveClear);
  AC_CHECK_EQ(countEvents(*world, sim::kEventWaveClear), 1u);
  AC_CHECK_EQ(world->events[0].value, 1.0);
  AC_CHECK_EQ(state.wave, 2);
  AC_CHECK_EQ(state.planned, 13);
  AC_CHECK_EQ(state.spawned, 0);
  AC_CHECK(state.isWaveStartPending);
  AC_CHECK(!state.isFinished);
}

AC_TEST(waves_director_finishes_after_wave_ten) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t player = spawnKind(*world, sim::EntityKind::kPlayer, 0.0, 0.0);
  sim::EntityId ids[1] = {player};
  waves::DirectorState state = waves::createDirectorState();
  waves::planWave(state, ac::config::kWaveMax, 1);
  state.spawned = state.planned;
  world->stats.aliveSheep = 0u;
  world->eventCount = 0u;

  const waves::DirectorTick result =
      waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
  AC_CHECK(result.isWaveClear);
  AC_CHECK(result.isMatchEnd);
  AC_CHECK_EQ(result.spawned, 0);
  AC_CHECK(state.isFinished);
  AC_CHECK_EQ(countEvents(*world, sim::kEventMatchEnded), 1u);
  AC_CHECK_EQ(world->events[0].value, 10.0);

  world->eventCount = 0u;
  const waves::DirectorTick after =
      waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
  AC_CHECK(!after.isWaveClear && !after.isMatchEnd && !after.isWaveStart);
  AC_CHECK_EQ(after.spawned, 0);
  AC_CHECK_EQ(world->eventCount, 0u);
}

AC_TEST(waves_director_uses_spawn_stream_only) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t player = spawnKind(*world, sim::EntityKind::kPlayer, 0.0, 0.0);
  sim::EntityId ids[1] = {player};
  const uint32_t fxBefore = world->rng.fx.a;
  const uint32_t aiBefore = world->rng.ai.a;
  const uint32_t spawnBefore = world->rng.spawn.a;

  waves::DirectorState state = waves::createDirectorState();
  waves::planWave(state, 5, 1);
  const waves::DirectorTick result =
      waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
  AC_CHECK(result.spawned > 0);
  AC_CHECK(world->rng.spawn.a != spawnBefore);
  AC_CHECK_EQ(world->rng.fx.a, fxBefore);
  AC_CHECK_EQ(world->rng.ai.a, aiBefore);
}

AC_TEST(waves_director_follows_kind_order) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t player = spawnKind(*world, sim::EntityKind::kPlayer, 0.0, 0.0);
  sim::EntityId ids[1] = {player};
  waves::DirectorState state = waves::createDirectorState();
  AC_CHECK_EQ(waves::planWave(state, 10, 1), 11);
  for (int tick = 0; tick < 4; ++tick) waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);

  const uint8_t expected[11] = {0u, 0u, 0u, 1u, 1u, 1u, 2u, 2u, 2u, 2u, 3u};
  uint16_t seen[11];
  std::size_t seenCount = 0u;
  for (std::size_t i = 0u; i < world->activeCount && seenCount < 11u; ++i) {
    const sim::Entity* sheep = sim::entityById(*world, world->activeIds[i]);
    if (sheep == nullptr || sheep->kind != sim::EntityKind::kSheep) continue;
    seen[seenCount] = sheep->id;
    ++seenCount;
  }
  AC_CHECK_EQ(seenCount, 11u);
  for (std::size_t i = 0u; i < seenCount; ++i) {
    const sim::Entity* sheep = sim::entityById(*world, seen[i]);
    AC_CHECK(sheep != nullptr);
    if (sheep == nullptr) return;
    AC_CHECK_EQ(sheep->sheepKind, expected[i]);
    AC_CHECK_EQ(sheep->state, ac::config::sheepStateCode(ac::config::SheepState::kGraze));
    AC_CHECK_EQ(sheep->team, 1u);
    AC_CHECK_NEAR(sheep->maxHp, ac::config::kSheep[expected[i]].hp, 1e-12);
  }
}

AC_TEST(waves_director_pool_exhaustion_drops_spawn) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  for (std::size_t i = 0u; i < sim::kMaxEntities; ++i) {
    const sim::SpawnResult spawned =
        sim::spawnEntity(*world, sim::EntityKind::kPickup, ac::Vec3{20.0, 0.0, 20.0});
    if (!spawned.isOk) break;
  }
  AC_CHECK_EQ(world->activeCount, sim::kMaxEntities);
  const uint16_t player = 1u;  // 池里已有实体，拿不到玩家 id 也不影响本用例
  sim::EntityId ids[1] = {player};
  world->stats.aliveSheep = 0u;

  waves::DirectorState state = waves::createDirectorState();
  const int32_t planned = waves::planWave(state, 10, 1);
  const waves::DirectorTick result =
      waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
  AC_CHECK_EQ(result.spawned, 0);       // 池满：丢弃本次生成，不重试
  AC_CHECK_EQ(state.spawned, 0);
  AC_CHECK_EQ(world->stats.aliveSheep, 0u);
  AC_CHECK(planned > 0);
  AC_CHECK_EQ(state.isWaveStartPending, false);  // waveStart 仍按 v1 先发
  AC_CHECK_EQ(countEvents(*world, sim::kEventWaveStart), 1u);
}

AC_TEST(waves_director_reaches_last_wave) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const uint16_t player = spawnKind(*world, sim::EntityKind::kPlayer, 0.0, 0.0);
  sim::EntityId ids[1] = {player};
  waves::DirectorState state = waves::createDirectorState();
  waves::planWave(state, 1, 1);

  int32_t cleared = 0;
  int32_t ended = 0;
  for (int tick = 0; tick < 400 && !state.isFinished; ++tick) {
    const waves::DirectorTick result =
        waves::updateDirector(*world, state, 1, world->rng.spawn, ids, 1u);
    if (result.isWaveClear) ++cleared;
    if (result.isMatchEnd) ++ended;
    killAllSheep(*world);  // 假想玩家秒清波
  }
  AC_CHECK(state.isFinished);
  AC_CHECK_EQ(state.wave, ac::config::kWaveMax);
  AC_CHECK_EQ(cleared, ac::config::kWaveMax);
  AC_CHECK_EQ(ended, 1);
}

AC_TEST(waves_two_runs_same_seed_are_bit_identical) {
  std::unique_ptr<sim::World> worldA = sim::createWorld(kSeed);
  std::unique_ptr<sim::World> worldB = sim::createWorld(kSeed);
  waves::DirectorState stateA = waves::createDirectorState();
  waves::DirectorState stateB = waves::createDirectorState();
  sim::EntityId idsA[1];
  sim::EntityId idsB[1];
  idsA[0] = spawnKind(*worldA, sim::EntityKind::kPlayer, 0.0, 0.0);
  idsB[0] = spawnKind(*worldB, sim::EntityKind::kPlayer, 0.0, 0.0);
  waves::planWave(stateA, 3, 1);
  waves::planWave(stateB, 3, 1);

  for (int tick = 0; tick < 200; ++tick) {
    const waves::DirectorTick a = waves::updateDirector(*worldA, stateA, 1, worldA->rng.spawn, idsA, 1u);
    const waves::DirectorTick b = waves::updateDirector(*worldB, stateB, 1, worldB->rng.spawn, idsB, 1u);
    AC_CHECK_EQ(a.spawned, b.spawned);
    AC_CHECK_EQ(a.isWaveStart, b.isWaveStart);
    AC_CHECK_EQ(a.isWaveClear, b.isWaveClear);
    AC_CHECK_EQ(a.isMatchEnd, b.isMatchEnd);
    if (tick % 40 == 39) killAllSheep(*worldA);
    if (tick % 40 == 39) killAllSheep(*worldB);
  }
  AC_CHECK_EQ(stateA.wave, stateB.wave);
  AC_CHECK_EQ(stateA.planned, stateB.planned);
  AC_CHECK_EQ(stateA.spawned, stateB.spawned);
  AC_CHECK_EQ(stateA.cursor, stateB.cursor);
  AC_CHECK_EQ(stateA.isFinished, stateB.isFinished);
  AC_CHECK_EQ(stateA.totalSpawns, stateB.totalSpawns);
  AC_CHECK_EQ(worldA->rng.spawn.a, worldB->rng.spawn.a);
  AC_CHECK_EQ(worldA->activeCount, worldB->activeCount);
  AC_CHECK_EQ(worldA->eventCount, worldB->eventCount);
  for (std::size_t i = 0u; i < worldA->activeCount; ++i) {
    const uint16_t id = worldA->activeIds[i];
    AC_CHECK_EQ(id, worldB->activeIds[i]);
    const sim::Entity* a = sim::entityById(*worldA, id);
    const sim::Entity* b = sim::entityById(*worldB, id);
    AC_CHECK(a != nullptr && b != nullptr);
    if (a == nullptr || b == nullptr) return;
    AC_CHECK_EQ(std::bit_cast<uint64_t>(a->pos.x), std::bit_cast<uint64_t>(b->pos.x));
    AC_CHECK_EQ(std::bit_cast<uint64_t>(a->pos.z), std::bit_cast<uint64_t>(b->pos.z));
    AC_CHECK_EQ(a->sheepKind, b->sheepKind);
    AC_CHECK_EQ(a->state, b->state);
  }
}

}  // namespace
