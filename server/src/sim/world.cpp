// S05 §5.1/§5.3/§5.4：World 的创建、重置与 tick 入口；
// recordPoseHistory 与 buildSpatialGrid 需要 World 的完整定义，实现放在这里（头文件只前置声明）。
#include "sim/world.hpp"

#include <cstring>

namespace ac::sim {

std::unique_ptr<World> createWorld(uint32_t seed) {
  std::unique_ptr<World> world = std::make_unique<World>();
  world->seed = seed;
  resetWorld(*world);
  return world;
}

void resetWorld(World& world) noexcept {
  const uint32_t seed = world.seed;
  std::memset(static_cast<void*>(&world), 0, sizeof(World));
  world.seed = seed;
  world.rng.ai = ac::createRng(seed, ac::RngStream::kAi);
  world.rng.spawn = ac::createRng(seed, ac::RngStream::kSpawn);
  world.rng.fx = ac::createRng(seed, ac::RngStream::kFx);
  buildSpatialGrid(world);  // 空世界也要有合法的 cellStart[0..400]
}

void stepWorld(World& world) noexcept {
  world.eventCount = 0u;  // §5.1：每 tick 开头清零（只有 [0, eventCount) 有效）
  world.tick += 1u;
  buildSpatialGrid(world);
  recordPoseHistory(world.poseHistory, world);
  updateWorldStats(world);
}

void updateWorldStats(World& world) noexcept {
  uint32_t aliveSheep = 0u;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (entity.active && entity.kind == EntityKind::kSheep) ++aliveSheep;
  }
  world.stats.aliveSheep = aliveSheep;
}

void buildSpatialGrid(World& world) noexcept {
  SpatialGrid& grid = world.grid;
  for (int32_t cell = 0; cell <= kSpatialCellCount; ++cell) grid.cellStart[cell] = 0;

  // 第一趟：计数写进 cellStart[cell + 1]（省掉额外的计数数组），projectile 不参与。
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active || entity.kind == EntityKind::kProjectile) continue;
    ++grid.cellStart[spatialCellIndex(entity.pos.x, entity.pos.z) + 1];
  }
  for (int32_t cell = 0; cell < kSpatialCellCount; ++cell) {
    grid.cellStart[cell + 1] += grid.cellStart[cell];
  }

  // 第二趟：按 activeIds 升序写入 → 每格内 EntityId 升序。
  int32_t cursor[kSpatialCellCount];
  for (int32_t cell = 0; cell < kSpatialCellCount; ++cell) cursor[cell] = grid.cellStart[cell];
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active || entity.kind == EntityKind::kProjectile) continue;
    const int32_t cell = spatialCellIndex(entity.pos.x, entity.pos.z);
    grid.cellItems[cursor[cell]] = entity.id;
    ++cursor[cell];
  }
}

void recordPoseHistory(PoseHistory& history, const World& world) noexcept {
  const std::size_t slot = static_cast<std::size_t>(world.tick) % kPoseHistorySlots;
  PoseSlot& target = history.slots[slot];
  clearPoseSlot(target);  // 本槽重写，避免上一圈的残留
  std::size_t count = 0u;
  for (std::size_t i = 0u; i < world.activeCount && count < kPoseHistoryPlayers; ++i) {
    const Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active || entity.kind != EntityKind::kPlayer) continue;
    PoseRecord& record = target[count];
    record.x = entity.pos.x;
    record.y = entity.pos.y;
    record.z = entity.pos.z;
    record.yaw = entity.yaw;
    record.pitch = entity.pitch;
    record.id = entity.id;
    ++count;
  }
  history.newestTick = world.tick;
  if (history.writeCount < kPoseHistorySlots) ++history.writeCount;
}

}  // namespace ac::sim
