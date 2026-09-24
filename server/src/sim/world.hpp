#pragma once
// S05 §5.1：纯数据的 World —— 实体表、姿态环、空间网格、场地与事件缓冲。
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/math.hpp"
#include "core/rng.hpp"
#include "sim/arena.hpp"
#include "sim/entity_table.hpp"
#include "sim/pose_history.hpp"
#include "sim/spatial_grid.hpp"

namespace ac::sim {

inline constexpr std::size_t kMaxEvents = 256u;

// §5.1：本 tick 的事件缓冲条目。这里只固定 S03 §5.4 冻结的条目头（eventId u32 + type u8，type ∈ 1..10）；
// 各类型载荷（subjectId / targetId / value / flags / hitX.. 等，字段名以 S03 字段表为准）由 S06 起
// 按其契约**追加**在末尾 —— 只能追加，不得改本份冻结的容量与每 tick 清零语义。
struct Event {
  uint32_t eventId = 0u;
  uint8_t type = 0u;
};

struct WorldStats {
  uint32_t aliveSheep = 0u;
  uint32_t eventsDropped = 0u;  // 只累加，不随 tick 清零
};

struct RngStreams {
  ac::Rng ai{0u};
  ac::Rng spawn{0u};
  ac::Rng fx{0u};
};

// §5.1 World 字段表：类型、顺序与容量不得改动（实体子结构与波次/阶段字段只能追加在末尾）。
struct World {
  uint32_t seed = 0u;
  uint32_t tick = 0u;
  Entity entities[kMaxEntities];
  uint16_t activeIds[kMaxEntities];
  uint16_t activeCount = 0u;
  uint16_t freeIds[kMaxEntities];
  uint16_t freeCount = 0u;
  uint16_t highWater = 0u;
  RngStreams rng{};
  WorldStats stats{};
  PoseHistory poseHistory{};
  SpatialGrid grid{};
  Event events[kMaxEvents];
  uint16_t eventCount = 0u;
};

// §9 稳定接口：World 一次性定长预分配（此后热路径不再分配）。
std::unique_ptr<World> createWorld(uint32_t seed);
void resetWorld(World& world) noexcept;  // 回到 createWorld 之后的状态（沿用 world.seed）

// 一个 tick 的推进入口：清事件缓冲 → tick + 1 → 重建网格 → 记姿态 → 刷统计。
// （S06 起在 tick 递增之后推进实体状态；本份只冻结数据布局与派生结构。）
void stepWorld(World& world) noexcept;
void updateWorldStats(World& world) noexcept;

inline EntityTable tableOf(World& world) noexcept {
  return EntityTable{world.entities, world.activeIds,  world.freeIds,
                     &world.activeCount, &world.freeCount, &world.highWater};
}

inline EntityTableConst tableConstOf(const World& world) noexcept {
  return EntityTableConst{world.entities, world.activeIds, world.activeCount, world.highWater};
}

inline const Entity* entityById(const World& world, uint16_t id) noexcept {
  return tableConstOf(world).find(id);
}

inline Entity* entityById(World& world, uint16_t id) noexcept {
  // 与只读版本共用同一份守卫：world 本身非 const，这里只是把 const 去回来
  const Entity* entity = entityById(static_cast<const World&>(world), id);
  return entity == nullptr ? nullptr : const_cast<Entity*>(entity);
}

inline bool isActiveEntityId(const World& world, uint16_t id) noexcept {
  return tableConstOf(world).isActiveId(id);
}

inline std::size_t activeIndexOfId(const World& world, uint16_t id) noexcept {
  return tableConstOf(world).activeIndexOf(id);
}

inline SpawnResult spawnEntity(World& world, const SpawnParams& params) noexcept {
  return tableOf(world).allocate(params);
}

inline SpawnResult spawnEntity(World& world, EntityKind kind, ac::Vec3 pos) noexcept {
  SpawnParams params{};
  params.kind = kind;
  params.pos = pos;
  return spawnEntity(world, params);
}

inline bool despawnEntity(World& world, uint16_t id) noexcept {
  return tableOf(world).release(id);
}

// §5.1：缓冲满（256）则丢新事件并累加 eventsDropped（线上单帧另有 u8 上限 255）。
inline bool pushEvent(World& world, const Event& event) noexcept {
  if (world.eventCount >= kMaxEvents) {
    ++world.stats.eventsDropped;
    return false;
  }
  world.events[world.eventCount] = event;
  ++world.eventCount;
  return true;
}

}  // namespace ac::sim
