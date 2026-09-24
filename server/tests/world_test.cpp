// S05 §5–§7：World 实体表、姿态环、空间网格与场地的行为测试（四组：world/entity/pose/grid）。
#include "tiny_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "config/player.hpp"
#include "core/math.hpp"
#include "core/rng.hpp"
#include "world_test_support.hpp"
#include "sim/arena.hpp"
#include "sim/entity_table.hpp"
#include "sim/step.hpp"
#include "sim/pose_history.hpp"
#include "sim/spatial_grid.hpp"
#include "sim/world.hpp"

namespace sim = ac::sim;

namespace {

constexpr uint32_t kSeed = 0xA5A5u;

ac::Vec3 vec(double x, double y, double z) { return ac::Vec3{x, y, z}; }

using ac::test::stepEmpty;

void setPosition(sim::World& world, sim::EntityId id, const ac::Vec3& pos) {
  sim::Entity* entity = sim::entityById(world, id);
  AC_CHECK(entity != nullptr);
  if (entity != nullptr) entity->pos = pos;
}

std::vector<uint16_t> collectNeighbors(const sim::SpatialGrid& grid, double x, double z,
                                      double radius) {
  std::vector<uint16_t> visited;
  sim::forEachNeighbor(grid, x, z, radius, [&visited](uint16_t id) { visited.push_back(id); });
  return visited;
}

bool containsId(const std::vector<uint16_t>& values, uint16_t id) {
  return std::find(values.begin(), values.end(), id) != values.end();
}

std::size_t gridItemCount(const sim::SpatialGrid& grid) {
  return static_cast<std::size_t>(grid.cellStart[sim::kSpatialCellCount]);
}

}  // namespace

// ---------- World（--filter=world）----------

AC_TEST(world_create_initializes_defaults) {
  const std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  AC_CHECK_EQ(world->seed, kSeed);
  AC_CHECK_EQ(world->tick, 0u);
  AC_CHECK_EQ(world->activeCount, 0u);
  AC_CHECK_EQ(world->freeCount, 0u);
  AC_CHECK_EQ(world->highWater, 0u);
  AC_CHECK_EQ(world->eventCount, 0u);
  AC_CHECK_EQ(world->stats.aliveSheep, 0u);
  AC_CHECK_EQ(world->stats.eventsDropped, 0u);

  // 三流由 seed 派生（确定性内核契约），互不相同
  AC_CHECK_EQ(world->rng.ai.a, ac::createRng(kSeed, ac::RngStream::kAi).a);
  AC_CHECK_EQ(world->rng.spawn.a, ac::createRng(kSeed, ac::RngStream::kSpawn).a);
  AC_CHECK_EQ(world->rng.fx.a, ac::createRng(kSeed, ac::RngStream::kFx).a);
  AC_CHECK(world->rng.ai.a != world->rng.spawn.a);
  AC_CHECK(world->rng.spawn.a != world->rng.fx.a);

  std::size_t activeSlots = 0u;
  for (std::size_t i = 0u; i < sim::kMaxEntities; ++i) {
    if (world->entities[i].active) ++activeSlots;
  }
  AC_CHECK_EQ(activeSlots, 0u);
  AC_CHECK_EQ(world->entities[0].id, 0u);
  AC_CHECK(world->entities[0].kind == sim::EntityKind::kPlayer);
  AC_CHECK_EQ(world->entities[sim::kMaxEntities - 1u].pos.z, 0.0);
  // S08 在 Entity 末尾追加武器/倒地/怒气/交互/羊形（96 -> 232），S09 再追加击退与羊群 AI
  // 状态（仇恨槽 8 + 邻居槽 12 + 计时/冲锋方向）→ 232 -> 960 字节（README §12 的字节账；两轴评审后
  // 删掉了与 S08 `Entity::sheepKind` 重复的 `ai.sheepKind` 镜像，故由 968 回落到 960）。
  AC_CHECK_EQ(sizeof(sim::Entity), 960u);
  AC_CHECK_EQ(sizeof(sim::Entity::id), 2u);
  AC_CHECK_EQ(sizeof(sim::Entity::aliveMs), 4u);

  std::size_t occupiedCells = 0;
  for (int32_t cell = 0; cell <= sim::kSpatialCellCount; ++cell) {
    if (world->grid.cellStart[cell] != 0) ++occupiedCells;
  }
  AC_CHECK_EQ(occupiedCells, 0u);
  AC_CHECK_EQ(world->poseHistory.writeCount, 0u);
  AC_CHECK_EQ(world->poseHistory.newestTick, 0u);

  // 场地与点位：§5.5 的坐标逐位硬编码比对（DoD §7）
  AC_CHECK_EQ(sim::arena::kArenaSizeMeters, 80.0);
  AC_CHECK_EQ(sim::arena::kArenaHalfSizeMeters, 40.0);
  AC_CHECK_EQ(sim::arena::kFenceHeightMeters, 3.0);
  AC_CHECK_EQ(sim::arena::kFenceThicknessMeters, 0.5);
  AC_CHECK_EQ(sim::arena::kBarn.min.x, -4.0);
  AC_CHECK_EQ(sim::arena::kBarn.min.y, 0.0);
  AC_CHECK_EQ(sim::arena::kBarn.min.z, -4.0);
  AC_CHECK_EQ(sim::arena::kBarn.max.x, 4.0);
  AC_CHECK_EQ(sim::arena::kBarn.max.y, 5.0);
  AC_CHECK_EQ(sim::arena::kBarn.max.z, 4.0);
  AC_CHECK_EQ(sim::arena::kSpawnRingRadiusMeters, 38.0);

  const double kPlayerSpawnXZ[4][2] = {{-4.5, 7.0}, {-1.5, 7.0}, {1.5, 7.0}, {4.5, 7.0}};
  AC_CHECK_EQ(sim::arena::kPlayerSpawns.size(), 4u);
  for (std::size_t i = 0u; i < sim::arena::kPlayerSpawns.size(); ++i) {
    AC_CHECK_EQ(sim::arena::kPlayerSpawns[i].x, kPlayerSpawnXZ[i][0]);
    AC_CHECK_EQ(sim::arena::kPlayerSpawns[i].y, 0.0);
    AC_CHECK_EQ(sim::arena::kPlayerSpawns[i].z, kPlayerSpawnXZ[i][1]);
  }

  const double kSpawnPointXZ[12][2] = {{0.0, 38.0},        {19.0, 32.909},   {32.909, 19.0},
                                       {38.0, 0.0},        {32.909, -19.0},  {19.0, -32.909},
                                       {0.0, -38.0},       {-19.0, -32.909}, {-32.909, -19.0},
                                       {-38.0, 0.0},       {-32.909, 19.0},  {-19.0, 32.909}};
  AC_CHECK_EQ(sim::arena::kSpawnPoints.size(), 12u);
  for (std::size_t i = 0u; i < sim::arena::kSpawnPoints.size(); ++i) {
    AC_CHECK_EQ(sim::arena::kSpawnPoints[i].x, kSpawnPointXZ[i][0]);
    AC_CHECK_EQ(sim::arena::kSpawnPoints[i].y, 0.0);
    AC_CHECK_EQ(sim::arena::kSpawnPoints[i].z, kSpawnPointXZ[i][1]);
    const ac::Vec3& point = sim::arena::kSpawnPoints[i];
    AC_CHECK(std::abs(std::sqrt(point.x * point.x + point.z * point.z) - 38.0) < 0.01);
  }

  const double kKindSize[4][2] = {{0.4, 1.7}, {0.5, 0.9}, {0.08, 0.16}, {0.35, 0.7}};
  AC_CHECK_EQ(sim::arena::kKindDimensions.size(), sim::kEntityKindCount);
  for (std::size_t i = 0u; i < sim::arena::kKindDimensions.size(); ++i) {
    AC_CHECK_EQ(sim::arena::kKindDimensions[i].radius, kKindSize[i][0]);
    AC_CHECK_EQ(sim::arena::kKindDimensions[i].height, kKindSize[i][1]);
  }
  AC_CHECK_EQ(sim::kMaxEntities, 1024u);
  AC_CHECK_EQ(sim::kSpatialCellCount, 400);
  AC_CHECK_EQ(sim::kMaxEvents, 256u);

  // 容量取证（README §6 表格引用本行）
  std::printf("worldBytes=%zu entityBytes=%zu poseBytes=%zu gridBytes=%zu eventBytes=%zu\n",
              sizeof(sim::World), sizeof(sim::Entity), sizeof(sim::PoseHistory),
              sizeof(sim::SpatialGrid), sizeof(sim::Event));
  AC_CHECK_EQ(sizeof(sim::Event), 48u);  // S08 §5.6 的载荷扩容后仍是 48 字节
  // S05 的 128 KiB 只是「一次性预分配」的卫生上限；S08 扩到 265352 字节、S09 再随 Entity 的
  // 羊群 AI 状态扩到 1010824 字节（1024 实体 × 960 B + 256 事件 × 48 B + 姿态环 7688），
  // 上限抬到 1 MiB 并保留同量级约束（README §12 已声明）。
  AC_CHECK(sizeof(sim::World) < 1024u * 1024u);
}

AC_TEST(world_reset_returns_to_start) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  const auto player = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(1.0, 0.0, 1.0));
  const auto sheep = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(2.0, 0.0, 2.0));
  AC_CHECK(player.isOk && sheep.isOk);
  AC_CHECK(sim::despawnEntity(*world, player.id));
  sim::Event event{};
  event.type = 3u;
  AC_CHECK(sim::pushEvent(*world, event));
  stepEmpty(*world);
  stepEmpty(*world);
  (void)world->rng.ai.nextU32();  // 把 ai 流推离起点
  AC_CHECK(world->tick != 0u);

  sim::resetWorld(*world);
  AC_CHECK_EQ(world->seed, kSeed);
  AC_CHECK_EQ(world->tick, 0u);
  AC_CHECK_EQ(world->activeCount, 0u);
  AC_CHECK_EQ(world->freeCount, 0u);
  AC_CHECK_EQ(world->highWater, 0u);
  AC_CHECK_EQ(world->eventCount, 0u);
  AC_CHECK_EQ(world->events[0].type, 0u);
  AC_CHECK_EQ(world->stats.aliveSheep, 0u);
  AC_CHECK_EQ(world->stats.eventsDropped, 0u);
  AC_CHECK_EQ(world->rng.ai.a, ac::createRng(kSeed, ac::RngStream::kAi).a);
  AC_CHECK_EQ(world->rng.spawn.a, ac::createRng(kSeed, ac::RngStream::kSpawn).a);
  AC_CHECK_EQ(world->rng.fx.a, ac::createRng(kSeed, ac::RngStream::kFx).a);
  AC_CHECK_EQ(world->poseHistory.writeCount, 0u);
  AC_CHECK_EQ(world->poseHistory.newestTick, 0u);
  AC_CHECK_EQ(gridItemCount(world->grid), 0u);
  AC_CHECK(sim::entityById(*world, 1u) == nullptr);
  AC_CHECK(sim::entityById(*world, 2u) == nullptr);
}

AC_TEST(world_tick_advances_counters) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  const auto player = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(1.0, 0.0, -2.0));
  const auto sheep = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(3.0, 0.0, 4.0));
  AC_CHECK(player.isOk && sheep.isOk);

  stepEmpty(*world);
  AC_CHECK_EQ(world->tick, 1u);
  AC_CHECK_EQ(world->poseHistory.newestTick, 1u);
  AC_CHECK_EQ(world->poseHistory.writeCount, 1u);
  AC_CHECK_EQ(world->poseHistory.slots[1u][0].id, player.id);
  AC_CHECK_EQ(world->poseHistory.slots[1u][0].x, 1.0);
  AC_CHECK_EQ(world->poseHistory.slots[1u][1].id, 0u);  // 只记玩家
  AC_CHECK_EQ(gridItemCount(world->grid), 2u);         // 玩家 + 羊

  for (int i = 0; i < 20; ++i) stepEmpty(*world);
  AC_CHECK_EQ(world->tick, 21u);
  AC_CHECK_EQ(world->poseHistory.newestTick, 21u);
  AC_CHECK_EQ(world->poseHistory.writeCount, sim::kPoseHistorySlots);  // 20 槽写满后只滚动
  AC_CHECK_EQ(static_cast<std::size_t>(world->tick % sim::kPoseHistorySlots), 1u);
  AC_CHECK_EQ(world->poseHistory.slots[1u][0].id, player.id);  // 槽 21 % 20 = 1 被重写
}

AC_TEST(world_tick_clears_event_buffer) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  sim::Event event{};
  event.eventId = 7u;
  event.type = 4u;
  AC_CHECK(sim::pushEvent(*world, event));
  AC_CHECK_EQ(world->eventCount, 1u);
  AC_CHECK_EQ(world->events[0].type, 4u);
  AC_CHECK_EQ(world->events[0].eventId, 7u);

  stepEmpty(*world);
  AC_CHECK_EQ(world->eventCount, 0u);
  AC_CHECK_EQ(world->stats.eventsDropped, 0u);
  AC_CHECK(sim::pushEvent(*world, event));
  AC_CHECK_EQ(world->eventCount, 1u);
}

AC_TEST(world_event_buffer_overflow_is_counted) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  sim::Event event{};
  event.type = 5u;
  for (std::size_t i = 0u; i < sim::kMaxEvents; ++i) {
    event.eventId = static_cast<uint32_t>(i) + 1u;
    AC_CHECK(sim::pushEvent(*world, event));
  }
  AC_CHECK_EQ(world->eventCount, sim::kMaxEvents);
  AC_CHECK_EQ(world->stats.eventsDropped, 0u);
  AC_CHECK(!sim::pushEvent(*world, event));
  AC_CHECK(!sim::pushEvent(*world, event));
  AC_CHECK_EQ(world->eventCount, sim::kMaxEvents);
  AC_CHECK_EQ(world->stats.eventsDropped, 2u);
  AC_CHECK_EQ(world->events[0].eventId, 1u);  // 先到先留
  AC_CHECK_EQ(world->events[sim::kMaxEvents - 1u].eventId, static_cast<uint32_t>(sim::kMaxEvents));

  stepEmpty(*world);
  AC_CHECK_EQ(world->eventCount, 0u);
  AC_CHECK_EQ(world->stats.eventsDropped, 2u);  // 累计值不随 tick 清零
}

AC_TEST(world_stats_count_alive_sheep) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  const auto player = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(0.0, 0.0, 0.0));
  const auto sheepA = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(1.0, 0.0, 1.0));
  const auto sheepB = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(2.0, 0.0, 2.0));
  // S09 起投射物有生命周期：谷仓 AABB 内的弹丸会在阶段 11 被回收，所以放在场地空地上。
  const auto bullet = sim::spawnEntity(*world, sim::EntityKind::kProjectile, vec(20.0, 0.0, 20.0));
  AC_CHECK(player.isOk && sheepA.isOk && sheepB.isOk && bullet.isOk);

  stepEmpty(*world);
  AC_CHECK_EQ(world->stats.aliveSheep, 2u);
  AC_CHECK(sim::despawnEntity(*world, sheepA.id));
  stepEmpty(*world);
  AC_CHECK_EQ(world->stats.aliveSheep, 1u);
  AC_CHECK(sim::despawnEntity(*world, sheepB.id));
  stepEmpty(*world);
  AC_CHECK_EQ(world->stats.aliveSheep, 0u);
  AC_CHECK(sim::despawnEntity(*world, bullet.id));
  stepEmpty(*world);
  AC_CHECK_EQ(world->stats.aliveSheep, 0u);
}

// ---------- 实体表（--filter=entity）----------

AC_TEST(entity_new_ids_stay_sorted) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  for (int i = 0; i < 5; ++i) {
    const sim::SpawnResult spawned =
        sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(static_cast<double>(i), 0.0, 0.0));
    AC_CHECK(spawned.isOk);
    AC_CHECK_EQ(spawned.id, static_cast<uint16_t>(i + 1));
    AC_CHECK(spawned.reason == sim::SpawnFailure::kNone);
  }
  AC_CHECK_EQ(world->activeCount, 5u);
  AC_CHECK_EQ(world->highWater, 5u);
  AC_CHECK_EQ(world->freeCount, 0u);
  for (std::size_t i = 0u; i < world->activeCount; ++i) {
    AC_CHECK_EQ(world->activeIds[i], static_cast<uint16_t>(i + 1u));
  }

  const sim::Entity* entity = sim::entityById(*world, 3u);
  AC_CHECK(entity != nullptr);
  if (entity == nullptr) return;
  AC_CHECK_EQ(entity->id, 3u);
  AC_CHECK(&world->entities[2] == entity);  // 下标 = EntityId - 1，永不搬移
  AC_CHECK(entity->kind == sim::EntityKind::kSheep);
  AC_CHECK_EQ(entity->pos.x, 2.0);
  AC_CHECK_EQ(sim::activeIndexOfId(*world, 3u), 2u);
  AC_CHECK(sim::isActiveEntityId(*world, 5u));
  AC_CHECK_EQ(world->entities[4].id, 5u);

  // 释放中间项后升序仍然成立，且复用的较小 id 会被插回正确位置
  AC_CHECK(sim::despawnEntity(*world, 3u));
  AC_CHECK_EQ(sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(0.0, 0.0, 0.0)).id, 3u);
  for (std::size_t i = 0u; i < world->activeCount; ++i) {
    AC_CHECK_EQ(world->activeIds[i], static_cast<uint16_t>(i + 1u));
  }
}

AC_TEST(entity_release_pushes_free_stack) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  for (int i = 0; i < 5; ++i) {
    AC_CHECK(sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 0.0)).isOk);
  }
  AC_CHECK(sim::despawnEntity(*world, 3u));
  AC_CHECK_EQ(world->activeCount, 4u);
  AC_CHECK_EQ(world->freeCount, 1u);
  AC_CHECK_EQ(world->freeIds[0], 3u);
  AC_CHECK_EQ(world->highWater, 5u);           // 释放不回收 highWater
  AC_CHECK(!world->entities[2].active);
  AC_CHECK_EQ(world->entities[2].id, 3u);      // 释放时不清字段（分配时整体重置）
  AC_CHECK(sim::entityById(*world, 3u) == nullptr);
  const uint16_t expected[4] = {1u, 2u, 4u, 5u};
  for (std::size_t i = 0u; i < 4u; ++i) AC_CHECK_EQ(world->activeIds[i], expected[i]);

  AC_CHECK(sim::despawnEntity(*world, 1u));
  AC_CHECK_EQ(world->freeCount, 2u);
  AC_CHECK_EQ(world->freeIds[1], 1u);          // 后释放的压在上层
  AC_CHECK_EQ(world->activeCount, 3u);
  AC_CHECK_EQ(world->activeIds[0], 2u);
}

AC_TEST(entity_reuse_is_lifo_and_resets_fields) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  sim::SpawnParams params{};
  params.kind = sim::EntityKind::kSheep;
  params.pos = vec(9.0, 8.0, 7.0);
  params.yaw = 1.25;
  params.pitch = -0.5;
  params.team = 1u;
  params.ownerId = 42u;
  params.hp = 30;
  params.armor = 2;
  const sim::SpawnResult first = sim::spawnEntity(*world, params);
  AC_CHECK(first.isOk);
  AC_CHECK_EQ(first.id, 1u);

  sim::Entity* occupied = sim::entityById(*world, first.id);
  AC_CHECK(occupied != nullptr);
  if (occupied == nullptr) return;
  AC_CHECK_EQ(occupied->hp, 30);               // params.hp 覆盖当前生命
  AC_CHECK_EQ(occupied->maxHp, 60);            // maxHp 恒为 kind 基础生命（v1 world.ts:231）
  AC_CHECK_EQ(occupied->armor, 2);
  occupied->vel = vec(1.0, 2.0, 3.0);
  occupied->state = 9u;
  occupied->aliveMs = 12345u;
  occupied->idle = true;

  const sim::SpawnResult third = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(3.0, 0.0, 3.0));
  AC_CHECK(third.isOk);
  AC_CHECK_EQ(third.id, 2u);
  sim::Entity* thirdEntity = sim::entityById(*world, third.id);
  AC_CHECK(thirdEntity != nullptr);
  if (thirdEntity == nullptr) return;
  thirdEntity->vel = vec(4.0, 5.0, 6.0);
  thirdEntity->aliveMs = 999u;
  thirdEntity->idle = true;

  AC_CHECK(sim::despawnEntity(*world, first.id));   // 空闲栈：[1]
  AC_CHECK(sim::despawnEntity(*world, third.id));   // 空闲栈：[1, 2]（2 在栈顶）
  AC_CHECK_EQ(world->freeCount, 2u);

  const sim::SpawnResult reused = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(1.0, 0.0, 1.0));
  AC_CHECK(reused.isOk);
  AC_CHECK_EQ(reused.id, 2u);                  // LIFO：最近释放的先复用
  AC_CHECK_EQ(world->highWater, 2u);           // 不新增 id
  AC_CHECK_EQ(world->freeCount, 1u);
  AC_CHECK_EQ(world->freeIds[0], 1u);

  // 复用槽 2：字段必须整体回到初值（之前写过 vel / aliveMs / idle）
  const sim::Entity* fresh = sim::entityById(*world, reused.id);
  AC_CHECK(fresh != nullptr);
  if (fresh == nullptr) return;
  AC_CHECK(fresh->active);
  AC_CHECK_EQ(fresh->id, reused.id);
  AC_CHECK_EQ(fresh->id, 2u);
  AC_CHECK(fresh->kind == sim::EntityKind::kPlayer);
  AC_CHECK_EQ(fresh->pos.x, 1.0);
  AC_CHECK_EQ(fresh->pos.y, 0.0);
  AC_CHECK_EQ(fresh->vel.x, 0.0);              // 复用必须整体重置
  AC_CHECK_EQ(fresh->vel.y, 0.0);
  AC_CHECK_EQ(fresh->vel.z, 0.0);
  AC_CHECK_EQ(fresh->yaw, 0.0);
  AC_CHECK_EQ(fresh->pitch, 0.0);
  // 3 参便捷重载自 S08 起与 v1 spawnEntity 对齐：hp/armor 取 kind 基础属性
  AC_CHECK_EQ(fresh->hp, 100);
  AC_CHECK_EQ(fresh->maxHp, 100);
  AC_CHECK_EQ(fresh->armor, 50);
  AC_CHECK_EQ(fresh->state, 0u);
  AC_CHECK_EQ(fresh->team, 0u);
  AC_CHECK_EQ(fresh->ownerId, 0u);
  AC_CHECK_EQ(fresh->aliveMs, 0u);
  AC_CHECK(!fresh->idle);
  AC_CHECK(&world->entities[reused.id - 1u] == fresh);  // 槽位不搬移

  // 再分配一个：拿走栈里剩下的槽 1，同样整体重置
  const sim::SpawnResult last = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 0.0));
  AC_CHECK(last.isOk);
  AC_CHECK_EQ(last.id, 1u);
  AC_CHECK_EQ(world->freeCount, 0u);
  AC_CHECK_EQ(world->highWater, 2u);
  const sim::Entity* recycled = sim::entityById(*world, 1u);
  AC_CHECK(recycled != nullptr);
  if (recycled == nullptr) return;
  AC_CHECK_EQ(recycled->vel.x, 0.0);
  AC_CHECK_EQ(recycled->vel.z, 0.0);
  AC_CHECK_EQ(recycled->state, 0u);
  AC_CHECK_EQ(recycled->aliveMs, 0u);
  AC_CHECK(!recycled->idle);
  AC_CHECK(recycled->kind == sim::EntityKind::kSheep);
}

AC_TEST(entity_release_unknown_id_is_ignored) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  AC_CHECK(!sim::despawnEntity(*world, 0u));       // 哨兵
  AC_CHECK(!sim::despawnEntity(*world, 7u));       // 从未分配
  AC_CHECK(!sim::despawnEntity(*world, 65535u));   // 越界
  AC_CHECK_EQ(world->freeCount, 0u);
  AC_CHECK_EQ(world->activeCount, 0u);

  const auto spawned = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 0.0));
  AC_CHECK(spawned.isOk);
  AC_CHECK(sim::despawnEntity(*world, spawned.id));
  AC_CHECK_EQ(world->activeCount, 0u);
  AC_CHECK_EQ(world->freeCount, 1u);
  AC_CHECK_EQ(world->freeIds[0], spawned.id);

  // 同一 id 重复释放：第二次失败且不再入栈
  AC_CHECK(!sim::despawnEntity(*world, spawned.id));
  AC_CHECK_EQ(world->freeCount, 1u);
  AC_CHECK(!sim::despawnEntity(*world, spawned.id));
  AC_CHECK_EQ(world->freeCount, 1u);
  AC_CHECK_EQ(world->activeCount, 0u);

  const auto again = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 0.0));
  AC_CHECK(again.isOk);
  AC_CHECK_EQ(again.id, spawned.id);
  AC_CHECK_EQ(world->freeCount, 0u);
  AC_CHECK_EQ(world->highWater, spawned.id);
}

AC_TEST(entity_pool_exhaustion_keeps_state) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  std::size_t spawnedCount = 0u;
  for (std::size_t i = 0u; i < sim::kMaxEntities; ++i) {
    if (sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 0.0)).isOk) ++spawnedCount;
  }
  AC_CHECK_EQ(spawnedCount, sim::kMaxEntities);
  AC_CHECK_EQ(world->highWater, static_cast<uint16_t>(sim::kMaxEntities));
  AC_CHECK_EQ(world->activeCount, static_cast<uint16_t>(sim::kMaxEntities));
  AC_CHECK_EQ(world->freeCount, 0u);
  AC_CHECK_EQ(world->activeIds[0], 1u);
  AC_CHECK_EQ(world->activeIds[sim::kMaxEntities - 1u], static_cast<uint16_t>(sim::kMaxEntities));

  const uint16_t highWaterBefore = world->highWater;
  const uint16_t lastIdBefore = world->activeIds[sim::kMaxEntities - 1u];
  const sim::SpawnResult overflow =
      sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(1.0, 1.0, 1.0));
  AC_CHECK(!overflow.isOk);
  AC_CHECK(overflow.reason == sim::SpawnFailure::kEntityPoolExhausted);
  AC_CHECK_EQ(overflow.id, 0u);
  AC_CHECK_EQ(world->highWater, highWaterBefore);       // 状态保持不变
  AC_CHECK_EQ(world->activeCount, static_cast<uint16_t>(sim::kMaxEntities));
  AC_CHECK_EQ(world->freeCount, 0u);
  AC_CHECK_EQ(world->activeIds[sim::kMaxEntities - 1u], lastIdBefore);
  AC_CHECK(world->entities[0].kind == sim::EntityKind::kSheep);

  // 释放一个名额后又能分配，且复用刚释放的 id
  AC_CHECK(sim::despawnEntity(*world, 500u));
  const sim::SpawnResult reused = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(0.0, 0.0, 0.0));
  AC_CHECK(reused.isOk);
  AC_CHECK_EQ(reused.id, 500u);
  AC_CHECK_EQ(world->highWater, highWaterBefore);
  AC_CHECK_EQ(world->activeCount, static_cast<uint16_t>(sim::kMaxEntities));
}

AC_TEST(entity_churn_preserves_invariants) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  ac::Rng rng = ac::createRng(7u, ac::RngStream::kSpawn);
  std::size_t spawns = 0u;
  std::size_t despawns = 0u;
  const std::size_t kOps = 100000u;

  // DoD：spawn / despawn 各 10 万次，activeCount + freeCount == highWater 恒成立
  for (std::size_t i = 0u; i < kOps; ++i) {
    if (sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 0.0)).isOk) ++spawns;
    if (world->activeCount > 64u) {
      const std::size_t pick = rng.nextU32() % world->activeCount;
      if (sim::despawnEntity(*world, world->activeIds[pick])) ++despawns;
    }
    if ((i % 10000u) == 0u) {
      AC_CHECK_EQ(static_cast<uint32_t>(world->activeCount) + world->freeCount,
                  static_cast<uint32_t>(world->highWater));
    }
  }
  AC_CHECK(spawns >= kOps);
  AC_CHECK(despawns >= kOps - 64u);

  // 收尾：全部释放，空闲栈应恰好装满 highWater 且无重复（释放失败即不变量被破坏，立刻停下）
  while (world->activeCount > 0u) {
    if (!sim::despawnEntity(*world, world->activeIds[0])) {
      AC_FAIL("activeIds[0] 指向的实体无法释放：升序表已被破坏");
      break;
    }
  }
  AC_CHECK_EQ(world->activeCount, 0u);
  AC_CHECK_EQ(world->freeCount, world->highWater);
  bool seen[sim::kMaxEntities] = {};
  for (std::size_t i = 0u; i < world->freeCount; ++i) {
    const uint16_t id = world->freeIds[i];
    AC_CHECK(id >= 1u && id <= world->highWater);
    AC_CHECK(!seen[id - 1u]);
    seen[id - 1u] = true;
  }
  std::size_t seenCount = 0u;
  for (std::size_t i = 0u; i < world->highWater; ++i) {
    if (seen[i]) ++seenCount;
  }
  AC_CHECK_EQ(seenCount, static_cast<std::size_t>(world->highWater));
}

AC_TEST(entity_lookup_by_id_and_index) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  const auto first = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(0.0, 0.0, 0.0));
  const auto second = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 0.0));
  const auto third = sim::spawnEntity(*world, sim::EntityKind::kPickup, vec(0.0, 0.0, 0.0));
  AC_CHECK(first.isOk && second.isOk && third.isOk);
  AC_CHECK(sim::despawnEntity(*world, second.id));

  AC_CHECK(sim::entityById(*world, 0u) == nullptr);
  AC_CHECK(sim::entityById(*world, second.id) == nullptr);   // 已释放
  AC_CHECK(sim::entityById(*world, 4u) == nullptr);          // 从未分配
  AC_CHECK(sim::entityById(*world, first.id) != nullptr);
  AC_CHECK(sim::entityById(*world, third.id) != nullptr);

  AC_CHECK_EQ(sim::activeIndexOfId(*world, first.id), 0u);
  AC_CHECK_EQ(sim::activeIndexOfId(*world, third.id), 1u);
  AC_CHECK_EQ(sim::activeIndexOfId(*world, second.id), sim::kMaxEntities);
  AC_CHECK(!sim::isActiveEntityId(*world, second.id));
  AC_CHECK(sim::isActiveEntityId(*world, third.id));

  sim::Entity* mutableEntity = sim::entityById(*world, third.id);
  AC_CHECK(mutableEntity != nullptr);
  if (mutableEntity == nullptr) return;
  AC_CHECK(mutableEntity->kind == sim::EntityKind::kPickup);
  AC_CHECK(mutableEntity->active);
  mutableEntity->hp = 9;
  AC_CHECK_EQ(sim::entityById(*world, third.id)->hp, 9);
  AC_CHECK_EQ(world->entities[third.id - 1u].hp, 9);
}

// ---------- 姿态环（--filter=pose）----------

AC_TEST(pose_history_records_players_only) {
  AC_CHECK_EQ(sim::kPoseHistorySlots, 20u);
  AC_CHECK_EQ(sim::kPoseHistoryPlayers, 8u);
  AC_CHECK_EQ(sim::kRewindLimitMs, 200u);
  AC_CHECK_EQ(sizeof(sim::PoseRecord), 48u);
  AC_CHECK_EQ(sizeof(sim::PoseSlot), 384u);
  AC_CHECK_EQ(sizeof(sim::PoseHistory::slots), 7680u);

  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  const auto playerA = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(1.0, 0.0, 1.0));
  const auto playerB = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(2.0, 0.0, 2.0));
  AC_CHECK(playerA.isOk && playerB.isOk);
  AC_CHECK(sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(3.0, 0.0, 3.0)).isOk);
  AC_CHECK(sim::spawnEntity(*world, sim::EntityKind::kProjectile, vec(4.0, 0.0, 4.0)).isOk);

  stepEmpty(*world);
  const sim::PoseSlot& slot = world->poseHistory.slots[1u];
  AC_CHECK_EQ(slot[0].id, playerA.id);
  AC_CHECK_EQ(slot[1].id, playerB.id);
  for (std::size_t i = 2u; i < sim::kPoseHistoryPlayers; ++i) AC_CHECK_EQ(slot[i].id, 0u);

  // 每槽上限 8 个玩家：只记 id 最小的 8 个（activeIds 升序）
  for (int i = 0; i < 8; ++i) {
    AC_CHECK(sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(0.0, 0.0, 0.0)).isOk);
  }
  AC_CHECK_EQ(world->stats.aliveSheep, 1u);
  stepEmpty(*world);
  AC_CHECK_EQ(world->stats.aliveSheep, 1u);
  // 只记 id 最小的 8 个玩家：playerA(1)、playerB(2) 与随后 8 个里的前 6 个（5..10）
  const sim::PoseSlot& full = world->poseHistory.slots[2u];
  const uint16_t expectedPlayers[sim::kPoseHistoryPlayers] = {1u, 2u, 5u, 6u, 7u, 8u, 9u, 10u};
  for (std::size_t i = 0u; i < sim::kPoseHistoryPlayers; ++i) {
    AC_CHECK_EQ(full[i].id, expectedPlayers[i]);
  }
  AC_CHECK_EQ(world->poseHistory.writeCount, 2u);
  AC_CHECK_EQ(world->poseHistory.newestTick, 2u);
}

AC_TEST(pose_sample_zero_is_exact) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  // S06 起 tick 内含静态碰撞：坐标要落在谷仓外扩 AABB（x、z 各 4.4）之外，否则会被推走
  const auto player = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(1.5, 0.0, -20.25));
  AC_CHECK(player.isOk);
  sim::Entity* entity = sim::entityById(*world, player.id);
  AC_CHECK(entity != nullptr);
  if (entity == nullptr) return;
  entity->yaw = 0.75;
  entity->pitch = -0.125;
  stepEmpty(*world);

  sim::SampledPose sampled{};
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, 0u, player.id, sampled));
  AC_CHECK(sampled.isOk);
  AC_CHECK(sampled.found);
  AC_CHECK(!sampled.clamped);
  AC_CHECK_EQ(sampled.x, 1.5);       // ms = 0：逐位等于最新槽位原值
  AC_CHECK_EQ(sampled.y, 0.0);
  AC_CHECK_EQ(sampled.z, -20.25);
  AC_CHECK_EQ(sampled.yaw, 0.75);
  AC_CHECK_EQ(sampled.pitch, -0.125);
  AC_CHECK(sampled.x == world->entities[player.id - 1u].pos.x);
  AC_CHECK(sampled.yaw == world->entities[player.id - 1u].yaw);

  // 再推一 tick：ms = 0 仍然取最新槽位（同样避开谷仓与栅栏）
  entity->pos.x = 30.0;
  stepEmpty(*world);
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, 0u, player.id, sampled));
  AC_CHECK_EQ(sampled.x, 30.0);
}

AC_TEST(pose_sample_interpolates_between_slots) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  // S06 起 tick 内含静态碰撞：z 落在谷仓外扩 AABB 之外，x 的插值样本才不会被推走
  const auto player = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(0.0, 0.0, 20.0));
  AC_CHECK(player.isOk);
  sim::Entity* entity = sim::entityById(*world, player.id);
  AC_CHECK(entity != nullptr);
  if (entity == nullptr) return;

  entity->pos.x = 0.0;
  stepEmpty(*world);  // tick 1：x = 0
  entity->pos.x = 4.0;
  stepEmpty(*world);  // tick 2：x = 4
  entity->pos.x = 8.0;
  stepEmpty(*world);  // tick 3：x = 8 (newest)

  sim::SampledPose sampled{};
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, 0u, player.id, sampled));
  AC_CHECK_EQ(sampled.x, 8.0);
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, 50u, player.id, sampled));  // 目标 = tick 2
  AC_CHECK(sampled.isOk);
  AC_CHECK_EQ(sampled.x, 4.0);                                              // alpha = 1：不插值
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, 75u, player.id, sampled));  // 目标 = tick 1.5
  AC_CHECK(sampled.isOk);
  AC_CHECK_EQ(sampled.x, 2.0);                                              // 0 与 4 的中点
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, 100u, player.id, sampled));  // 目标 = tick 1
  AC_CHECK_EQ(sampled.x, 0.0);
  // 历史不足（只有 3 槽）时夹到最老槽，插值退化为端点值
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, 150u, player.id, sampled));
  AC_CHECK(sampled.isOk);
  AC_CHECK_EQ(sampled.x, 0.0);
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, sim::kRewindLimitMs, player.id, sampled));
  AC_CHECK(sampled.isOk);
  AC_CHECK_EQ(sampled.x, 0.0);
  AC_CHECK(!sampled.clamped);
}

AC_TEST(pose_sample_beyond_limit_is_refused) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  const auto player = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(9.0, 0.0, 9.0));
  AC_CHECK(player.isOk);
  for (int i = 0; i < 5; ++i) stepEmpty(*world);

  sim::SampledPose sampled{};
  AC_CHECK(!sim::samplePoseAgo(world->poseHistory, sim::kRewindLimitMs + 1u, player.id, sampled));
  AC_CHECK(!sampled.isOk);          // 越限不回滚
  AC_CHECK(sampled.clamped);        // 仅诊断
  AC_CHECK(!sampled.found);         // 未使用回滚姿态
  AC_CHECK_EQ(sampled.x, 0.0);      // out 未被写入
  AC_CHECK_EQ(sampled.yaw, 0.0);

  AC_CHECK(!sim::samplePoseAgo(world->poseHistory, 1000u, player.id, sampled));
  AC_CHECK(sampled.clamped);
  AC_CHECK(!sampled.isOk);

  // 恰好等于上限仍然允许
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, sim::kRewindLimitMs, player.id, sampled));
  AC_CHECK(sampled.isOk);
  AC_CHECK(!sampled.clamped);
  AC_CHECK_EQ(sampled.x, 9.0);
}

AC_TEST(pose_sample_unknown_id_reports_missing) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  sim::SampledPose sampled{};
  AC_CHECK(!sim::samplePoseAgo(world->poseHistory, 0u, 1u, sampled));  // 空历史
  AC_CHECK(!sampled.found);
  AC_CHECK(!sampled.isOk);

  const auto player = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(1.0, 0.0, 1.0));
  const auto sheep = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(2.0, 0.0, 2.0));
  AC_CHECK(player.isOk && sheep.isOk);
  stepEmpty(*world);
  stepEmpty(*world);

  AC_CHECK(!sim::samplePoseAgo(world->poseHistory, 50u, sheep.id, sampled));  // 羊从不记录
  AC_CHECK(!sampled.found);
  AC_CHECK(!sampled.isOk);
  AC_CHECK_EQ(sampled.x, 0.0);
  AC_CHECK(!sim::samplePoseAgo(world->poseHistory, 50u, 0u, sampled));        // 哨兵 id
  AC_CHECK(!sampled.found);
  AC_CHECK(!sim::samplePoseAgo(world->poseHistory, 50u, 1024u, sampled));     // 从未分配
  AC_CHECK(sim::samplePoseAgo(world->poseHistory, 50u, player.id, sampled));  // 玩家可以
  AC_CHECK(sampled.found);
}

// ---------- 空间网格（--filter=grid）----------

AC_TEST(grid_build_counts_and_sorts_cells) {
  AC_CHECK_EQ(sim::kSpatialCellMeters, 4.0);
  AC_CHECK_EQ(sim::kSpatialCellsPerAxis, 20);
  AC_CHECK_EQ(sim::kSpatialCellCount, 400);
  AC_CHECK_EQ(sim::kSpatialCellCount + 1, 401);  // cellStart 401 项

  AC_CHECK_EQ(sim::spatialCellIndex(0.0, 0.0), 10 * 20 + 10);
  AC_CHECK_EQ(sim::spatialCellIndex(-40.0, -40.0), 0);
  AC_CHECK_EQ(sim::spatialCellIndex(-39.9, -39.9), 0);
  AC_CHECK_EQ(sim::spatialCellIndex(39.9, 39.9), 399);
  AC_CHECK_EQ(sim::spatialCellIndex(40.0, 40.0), 399);   // 上边界夹取
  AC_CHECK_EQ(sim::spatialCellIndex(-40.1, -40.1), 0);   // 下边界夹取

  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  const auto first = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(0.0, 0.0, 0.0));
  const auto second = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(1.0, 0.0, 1.0));
  const auto third = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(-1.0, 0.0, -1.0));
  const auto fourth = sim::spawnEntity(*world, sim::EntityKind::kPickup, vec(20.0, 0.0, -20.0));
  const auto fifth = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(39.0, 0.0, 39.0));
  // S09 起弹丸在谷仓内会被回收（§5.6），放场地空地上只验证「不入网格 + 留在活动表」。
  const auto bullet = sim::spawnEntity(*world, sim::EntityKind::kProjectile, vec(20.0, 0.0, 20.0));
  AC_CHECK(first.isOk && second.isOk && third.isOk && fourth.isOk && fifth.isOk && bullet.isOk);
  stepEmpty(*world);

  AC_CHECK_EQ(gridItemCount(world->grid), 5u);          // projectile 不入网格
  AC_CHECK_EQ(world->activeCount, 6u);

  std::vector<uint16_t> items(world->grid.cellItems, world->grid.cellItems + gridItemCount(world->grid));
  std::sort(items.begin(), items.end());
  const uint16_t expected[5] = {1u, 2u, 3u, 4u, 5u};
  AC_CHECK_EQ(items.size(), 5u);
  for (std::size_t i = 0u; i < items.size(); ++i) AC_CHECK_EQ(items[i], expected[i]);

  // 前缀和自洽：cellStart 单调不减且末项 = 条目总数
  for (int32_t cell = 1; cell <= sim::kSpatialCellCount; ++cell) {
    AC_CHECK(world->grid.cellStart[cell] >= world->grid.cellStart[cell - 1]);
  }
}

AC_TEST(grid_clamps_out_of_bounds_coordinates) {
  AC_CHECK_EQ(sim::spatialCellOf(1000.0), 19);
  AC_CHECK_EQ(sim::spatialCellOf(-1000.0), 0);
  AC_CHECK_EQ(sim::spatialCellOf(41.0), 19);
  AC_CHECK_EQ(sim::spatialCellOf(-41.0), 0);
  AC_CHECK_EQ(sim::spatialCellOf(0.0), 10);

  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  const auto farEast = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(1000.0, 0.0, 0.0));
  const auto farWest = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(-1000.0, 0.0, 0.0));
  const auto farNorth = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 1000.0));
  const auto farSouth = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, -1000.0));
  const auto middle = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(0.0, 0.0, 0.0));
  AC_CHECK(farEast.isOk && farWest.isOk && farNorth.isOk && farSouth.isOk && middle.isOk);
  stepEmpty(*world);

  // S06 起 tick 内的静态碰撞会先把越界坐标夹回场地（README §7.2），所以这里把坐标重设成越界值、
  // 单独重建网格，专门考 buildSpatialGrid 自己的夹取（这一趟不经过权威 tick）。
  setPosition(*world, farEast.id, vec(1000.0, 0.0, 0.0));
  setPosition(*world, farWest.id, vec(-1000.0, 0.0, 0.0));
  setPosition(*world, farNorth.id, vec(0.0, 0.0, 1000.0));
  setPosition(*world, farSouth.id, vec(0.0, 0.0, -1000.0));
  setPosition(*world, middle.id, vec(0.0, 0.0, 0.0));
  sim::buildSpatialGrid(*world);

  AC_CHECK_EQ(gridItemCount(world->grid), 5u);
  // 越界坐标各自夹到边界列/行：东 → (cx 19, cz 10)、西 → (0, 10)、北 → (10, 19)、南 → (10, 0)、中心 → (10, 10)
  const int32_t cellEast = 10 * 20 + 19;
  const int32_t cellWest = 10 * 20 + 0;
  const int32_t cellNorth = 19 * 20 + 10;
  const int32_t cellSouth = 0 * 20 + 10;
  const int32_t cellMiddle = 10 * 20 + 10;
  AC_CHECK_EQ(sim::spatialCellIndex(1000.0, 0.0), cellEast);
  AC_CHECK_EQ(sim::spatialCellIndex(-1000.0, 0.0), cellWest);
  AC_CHECK_EQ(sim::spatialCellIndex(0.0, 1000.0), cellNorth);
  AC_CHECK_EQ(sim::spatialCellIndex(0.0, -1000.0), cellSouth);
  AC_CHECK_EQ(sim::spatialCellIndex(0.0, 0.0), cellMiddle);
  AC_CHECK_EQ(world->grid.cellStart[cellEast + 1] - world->grid.cellStart[cellEast], 1);
  AC_CHECK_EQ(world->grid.cellStart[cellWest + 1] - world->grid.cellStart[cellWest], 1);
  AC_CHECK_EQ(world->grid.cellStart[cellNorth + 1] - world->grid.cellStart[cellNorth], 1);
  AC_CHECK_EQ(world->grid.cellStart[cellSouth + 1] - world->grid.cellStart[cellSouth], 1);
  AC_CHECK_EQ(world->grid.cellStart[cellMiddle + 1] - world->grid.cellStart[cellMiddle], 1);
  AC_CHECK_EQ(world->grid.cellItems[world->grid.cellStart[cellEast]], farEast.id);
  AC_CHECK_EQ(world->grid.cellItems[world->grid.cellStart[cellWest]], farWest.id);

  // 查询点在场地外（夹取只会多访问、不会漏配对）
  const std::vector<uint16_t> visited = collectNeighbors(world->grid, 100.0, 0.0, 1.0);
  AC_CHECK(containsId(visited, farEast.id));
  const std::vector<uint16_t> all = collectNeighbors(world->grid, 0.0, 0.0, 200.0);
  AC_CHECK_EQ(all.size(), 5u);
  AC_CHECK(containsId(all, farWest.id) && containsId(all, farNorth.id) &&
           containsId(all, farSouth.id) && containsId(all, middle.id));
}

AC_TEST(grid_cell_items_are_ascending) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  ac::Rng rng = ac::createRng(11u, ac::RngStream::kSpawn);
  for (int i = 0; i < 64; ++i) {
    const double x = static_cast<double>(static_cast<int32_t>(rng.nextU32() % 79u)) - 39.0;
    const double z = static_cast<double>(static_cast<int32_t>(rng.nextU32() % 79u)) - 39.0;
    AC_CHECK(sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(x, 0.0, z)).isOk);
  }
  for (uint16_t id = 2u; id <= 64u; id += 3u) AC_CHECK(sim::despawnEntity(*world, id));  // 制造 id 空洞
  AC_CHECK(sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(-30.0, 0.0, 30.0)).isOk);
  stepEmpty(*world);

  // 每格内 EntityId 升序，且每个条目确实落在它所属的格
  std::vector<uint16_t> sequence;
  for (int32_t cz = 0; cz < sim::kSpatialCellsPerAxis; ++cz) {
    for (int32_t cx = 0; cx < sim::kSpatialCellsPerAxis; ++cx) {
      const int32_t cell = cz * sim::kSpatialCellsPerAxis + cx;
      for (int32_t item = world->grid.cellStart[cell] + 1; item < world->grid.cellStart[cell + 1];
           ++item) {
        AC_CHECK(world->grid.cellItems[item - 1] < world->grid.cellItems[item]);
      }
      for (int32_t item = world->grid.cellStart[cell]; item < world->grid.cellStart[cell + 1];
           ++item) {
        const uint16_t id = world->grid.cellItems[item];
        const sim::Entity* entity = sim::entityById(*world, id);
        AC_CHECK(entity != nullptr);
        if (entity == nullptr) continue;
        AC_CHECK_EQ(sim::spatialCellIndex(entity->pos.x, entity->pos.z), cell);
        sequence.push_back(id);
      }
    }
  }
  AC_CHECK_EQ(sequence.size(), gridItemCount(world->grid));

  // 遍历顺序 = (cz, cx) 升序 + 格内升序：全覆盖查询应与手工扫描逐项一致
  const std::vector<uint16_t> viaNeighbor = collectNeighbors(world->grid, 0.0, 0.0, 200.0);
  AC_CHECK_EQ(viaNeighbor.size(), sequence.size());
  AC_CHECK(std::equal(viaNeighbor.begin(), viaNeighbor.end(), sequence.begin()));
  // 不作全局 EntityId 单调断言：只要求格索引升序这一级（这里验证全局序列不是升序的样本存在）
  bool strictlyAscending = true;
  for (std::size_t i = 1u; i < sequence.size(); ++i) {
    if (sequence[i - 1u] >= sequence[i]) strictlyAscending = false;
  }
  AC_CHECK(!strictlyAscending || sequence.size() < 2u);
}

AC_TEST(grid_ignores_projectiles) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  // S06 起 tick 内含静态碰撞：玩家落在谷仓外，末尾按它的坐标查邻域
  const auto player = sim::spawnEntity(*world, sim::EntityKind::kPlayer, vec(20.0, 0.0, 20.0));
  const auto sheep = sim::spawnEntity(*world, sim::EntityKind::kSheep, vec(1.0, 0.0, 1.0));
  // S09 起弹丸有生命周期：谷仓内的弹丸会在阶段 11 被回收，故放在场地空地上（§5.6）。
  const auto bulletA = sim::spawnEntity(*world, sim::EntityKind::kProjectile, vec(6.0, 0.0, 6.0));
  const auto bulletB = sim::spawnEntity(*world, sim::EntityKind::kProjectile, vec(7.0, 0.0, 7.0));
  const auto pickup = sim::spawnEntity(*world, sim::EntityKind::kPickup, vec(3.0, 0.0, 3.0));
  AC_CHECK(player.isOk && sheep.isOk && bulletA.isOk && bulletB.isOk && pickup.isOk);
  stepEmpty(*world);

  AC_CHECK_EQ(gridItemCount(world->grid), 3u);  // 玩家 + 羊 + 掉落物
  const std::vector<uint16_t> all = collectNeighbors(world->grid, 0.0, 0.0, 200.0);
  AC_CHECK_EQ(all.size(), 3u);
  AC_CHECK(containsId(all, player.id) && containsId(all, sheep.id) && containsId(all, pickup.id));
  AC_CHECK(!containsId(all, bulletA.id) && !containsId(all, bulletB.id));

  // 释放非投射物后网格随之收缩；投射物释放不影响
  AC_CHECK(sim::despawnEntity(*world, sheep.id));
  AC_CHECK(sim::despawnEntity(*world, bulletA.id));
  stepEmpty(*world);
  AC_CHECK_EQ(gridItemCount(world->grid), 2u);
  const std::vector<uint16_t> after = collectNeighbors(world->grid, 20.0, 20.0, 1.0);
  AC_CHECK(!containsId(after, bulletA.id));
  AC_CHECK(containsId(after, player.id));
}
