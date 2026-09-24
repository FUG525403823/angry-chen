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

// §5.1：本 tick 的事件缓冲条目。头（eventId u32 + type u8，type ∈ 1..10）由 S05 冻结；
// S08 按 S06 的约定在末尾追加各类型载荷，字段语义与 v1 world.ts 的 SimEvent 逐字对应
// （x/y/z 是米制、value 是原始数值 —— 量化到 u16/i16 厘米是编码层的事，见 S03 §5.4）。
// eventId 仍留 0：v1 的 sim 事件没有这个字段，幂等键由广播层（S12）分配。
// 字段顺序按「8 字节对齐优先」排布：eventId/tick + 4 个 double + 3 个 u16/u8 组 = 48 字节（无填充浪费）。
struct Event {
  uint32_t eventId = 0u;
  uint32_t tick = 0u;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double value = 0.0;
  uint16_t subjectId = 0u;
  uint16_t targetId = 0u;
  uint8_t type = 0u;
  uint8_t flags = 0u;
  uint8_t kind = 0u;  // 仅 sheepKilled 用（0 grunt / 1 ram / 2 elite / 3 king）
};
static_assert(sizeof(Event) == 48u, "S08 §5.6：事件载荷扩容后条目仍是 48 字节（无隐式填充）");

// S03 §5.4 的事件类型编号（sim 层只认整数，避免 sim -> net 的反向依赖；
// combat_test.cpp 用 static_assert 把这份编号与 net/codec.hpp 的枚举钉在一起）。
inline constexpr uint8_t kEventPlayerHit = 1u;
inline constexpr uint8_t kEventSheepKilled = 2u;
inline constexpr uint8_t kEventWaveStart = 3u;
inline constexpr uint8_t kEventWaveClear = 4u;
inline constexpr uint8_t kEventPlayerDowned = 5u;
inline constexpr uint8_t kEventReviveProgress = 6u;
inline constexpr uint8_t kEventReviveDone = 7u;
inline constexpr uint8_t kEventRageActivated = 8u;
inline constexpr uint8_t kEventMatchEnded = 9u;
inline constexpr uint8_t kEventPhaseChange = 10u;

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
  // S06 §5.1 阶段 0 追加（S05 §5.1 的字段顺序与容量不动，新字段只追加在末尾）：
  // timeMs 是 tick 的毫秒镜像（S12 的模拟漂移口径读它）；eventCursor 是事件遍历游标，阶段 0 清零。
  uint32_t timeMs = 0u;
  uint16_t eventCursor = 0u;
};

// §9 稳定接口：World 一次性定长预分配（此后热路径不再分配）。
std::unique_ptr<World> createWorld(uint32_t seed);
void resetWorld(World& world) noexcept;  // 回到 createWorld 之后的状态（沿用 world.seed）

// 阶段 12 的统计汇总入口。tick 入口是 step.hpp 的 stepWorld —— S06 起签名带命令缓冲与 dtMs。
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

// v1 world.ts 的 spawnEntity(kind, x, y, z, ...) 口径：hp/armor 取该 kind 的基础属性。
// S08 起这个便捷重载与 v1 对齐（此前留 0，会让 S08 的救援/怒气用例拿到 0 血实体）；
// S09 起连 team 也对齐 v1 的 defaultTeamByKind（羊 = 1）—— 否则 director/羊王召唤出的羊与玩家同队，
// 会被 kFriendlyFire=false 的命中结算全部挡掉（见 README §12）。
inline SpawnResult spawnEntity(World& world, EntityKind kind, ac::Vec3 pos) noexcept {
  SpawnParams params{};
  params.kind = kind;
  params.pos = pos;
  params.team = ac::config::kDefaultTeamByKind[static_cast<std::size_t>(kind)];
  const ac::config::BaseStats& stats = ac::config::kKindBaseStats[static_cast<std::size_t>(kind)];
  params.hp = static_cast<double>(stats.hp);
  params.armor = static_cast<double>(stats.armor);
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

// S08 §5.6：按 v1 sim/events.ts 的 pushEvent 口径入队（type 用上表的整数编号）。
inline bool pushEvent(World& world, uint8_t type_, uint8_t flags, uint16_t subjectId, uint16_t targetId,
                      double x, double y, double z, double value, uint8_t kind = 0u) noexcept {
  Event event;
  event.type = type_;
  event.tick = world.tick;
  event.flags = flags;
  event.subjectId = subjectId;
  event.targetId = targetId;
  event.x = x;
  event.y = y;
  event.z = z;
  event.value = value;
  event.kind = kind;
  return pushEvent(world, event);
}

}  // namespace ac::sim
