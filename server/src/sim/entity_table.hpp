#pragma once
// S05 §5.2：实体存储、空闲栈（LIFO）、活动 ID 升序表与按 id 取的访问器。
// World 直接持有这些平行数组（§5.1 冻结字段名与顺序），EntityTable 只是它们的"表视图"。
#include <cstddef>
#include <cstdint>

#include "ai/sheep_state.hpp"
#include "combat/downed.hpp"
#include "config/player.hpp"
#include "combat/knockback.hpp"
#include "combat/rage.hpp"
#include "combat/weapon.hpp"
#include "core/math.hpp"

namespace ac::sim {

using EntityId = uint16_t;  // §5.2 的 "id u16"；S08/S09/S11 的计划用这个名字引用它
inline constexpr EntityId kNoEntityId = 0u;  // "无实体"哨兵
inline constexpr std::size_t kMaxEntities = static_cast<std::size_t>(ac::kMaxEntities);
static_assert(kMaxEntities == 1024u, "§5.5：实体容量必须与 EntityId 上限、网格 cellItems 容量恒等");

enum class EntityKind : uint8_t {
  kPlayer = 0u,
  kSheep = 1u,
  kProjectile = 2u,
  kPickup = 3u,
};

inline constexpr std::size_t kEntityKindCount = 4u;
static_assert(static_cast<uint8_t>(EntityKind::kPlayer) == 0u &&
                  static_cast<uint8_t>(EntityKind::kSheep) == 1u &&
                  static_cast<uint8_t>(EntityKind::kProjectile) == 2u &&
                  static_cast<uint8_t>(EntityKind::kPickup) == 3u,
              "§5.2：kind 的线上编号 0/1/2/3 不得改动");

enum class SpawnFailure : uint8_t {
  kNone = 0u,
  kEntityPoolExhausted = 1u,
};

struct SpawnResult {
  bool isOk = false;
  uint16_t id = kNoEntityId;
  SpawnFailure reason = SpawnFailure::kNone;
};

// §5.2 冻结字段（类型与顺序不得改）。武器/倒地/怒气/羊群 AI 状态由所属模块追加，
// 追加只能放在本表字段之后。
struct Entity {
  uint16_t id = kNoEntityId;
  bool active = false;
  EntityKind kind = EntityKind::kPlayer;
  ac::Vec3 pos{0.0, 0.0, 0.0};
  ac::Vec3 vel{0.0, 0.0, 0.0};
  double yaw = 0.0;
  double pitch = 0.0;
  // S08 起 hp/maxHp/armor 是 double：v1 的权威值就是 number，伤害公式产出分数（例：手枪四肢 18.75、
  // 护甲 50 时躯干 armorDamage 15 / hpDamage 10），取整会直接破坏与 v1 的逐位一致（见 README §9）。
  double hp = 0.0;
  double maxHp = 0.0;
  double armor = 0.0;
  uint8_t state = 0u;
  uint8_t team = 0u;
  uint16_t ownerId = kNoEntityId;
  uint32_t aliveMs = 0u;
  bool idle = false;
  // —— S08 追加（S05 §5.2 的原文允许："武器/倒地/怒气/羊群 AI 状态由所属模块追加"）——
  ac::combat::WeaponState weapon{};
  ac::combat::RageState rage{};
  ac::combat::DownedState downed{};
  bool interactHeld = false;  // §5.6：本 tick 的交互键（救援判定读它）
  uint8_t sheepKind = 0u;     // 羊形（0 grunt / 1 ram / 2 elite / 3 king）：S09 填，§5.4 命中盒读它
  // —— S09 追加 ——
  ac::combat::KnockbackState knock{};  // 击退（S08 只带了武器/怒气/倒地；阶段 6 读它）
  ac::ai::SheepAiState ai{};           // 每只羊的 AI 状态（仇恨槽 / 计时 / 冲锋方向 / 邻居槽）
};

struct SpawnParams {
  EntityKind kind = EntityKind::kPlayer;
  ac::Vec3 pos{0.0, 0.0, 0.0};
  double yaw = 0.0;
  double pitch = 0.0;
  uint8_t team = 0u;
  uint16_t ownerId = kNoEntityId;
  double hp = 0.0;
  double armor = 0.0;
};

// 分配与复用时必须整体重置（§5.2/§8）：释放时不清，分配时清。
inline void resetEntity(Entity& entity) noexcept {
  entity.id = kNoEntityId;
  entity.active = false;
  entity.kind = EntityKind::kPlayer;
  entity.pos = ac::Vec3{0.0, 0.0, 0.0};
  entity.vel = ac::Vec3{0.0, 0.0, 0.0};
  entity.yaw = 0.0;
  entity.pitch = 0.0;
  entity.hp = 0;
  entity.maxHp = 0;
  entity.armor = 0;
  entity.state = 0u;
  entity.team = 0u;
  entity.ownerId = kNoEntityId;
  entity.aliveMs = 0u;
  entity.idle = false;
  ac::combat::resetWeaponState(entity.weapon);
  ac::combat::resetRageState(entity.rage);
  ac::combat::resetDownedState(entity.downed);
  entity.interactHeld = false;
  entity.sheepKind = 0u;
  ac::combat::resetKnockbackState(entity.knock);
  ac::ai::resetSheepAiState(entity.ai);
}

// 只读视图：查询语义与 EntityTable 完全一致（模拟遍历与只读消费方用它）。
struct EntityTableConst {
  const Entity* entities = nullptr;
  const uint16_t* activeIds = nullptr;
  uint16_t activeCount = 0u;
  uint16_t highWater = 0u;

  const Entity* byIndex(uint16_t id) const noexcept {
    if (id == kNoEntityId || id > highWater) return nullptr;
    return &entities[id - 1u];  // §5.1：下标 = EntityId - 1
  }
  const Entity* find(uint16_t id) const noexcept {
    const Entity* entity = byIndex(id);
    if (entity == nullptr || !entity->active) return nullptr;
    return entity;
  }
  // 第一个 activeIds[i] >= id 的下标（lower_bound）：插入位置与查找共用。
  std::size_t lowerBoundActive(uint16_t id) const noexcept {
    std::size_t lo = 0u;
    std::size_t hi = activeCount;
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2u;
      if (activeIds[mid] < id) {
        lo = mid + 1u;
      } else {
        hi = mid;
      }
    }
    return lo;
  }
  // 活动 ID 表内的下标（二分）；不在表内返回 kMaxEntities。
  std::size_t activeIndexOf(uint16_t id) const noexcept {
    const std::size_t lo = lowerBoundActive(id);
    if (lo < activeCount && activeIds[lo] == id) return lo;
    return kMaxEntities;
  }
  bool isActiveId(uint16_t id) const noexcept { return activeIndexOf(id) != kMaxEntities; }
};

// 可变视图：指向 World 的平行数组（§5.1 的字段名与顺序不变）。
struct EntityTable {
  Entity* entities = nullptr;
  uint16_t* activeIds = nullptr;
  uint16_t* freeIds = nullptr;
  uint16_t* activeCount = nullptr;
  uint16_t* freeCount = nullptr;
  uint16_t* highWater = nullptr;

  EntityTableConst read() const noexcept {
    return EntityTableConst{entities, activeIds, *activeCount, *highWater};
  }

  // §5.2：空闲栈 LIFO 复用优先；池与栈都空则失败且不改任何状态。
  SpawnResult allocate(const SpawnParams& params) noexcept {
    uint16_t id = kNoEntityId;
    if (*freeCount > 0u) {
      --(*freeCount);
      id = freeIds[*freeCount];
    } else if (*highWater < static_cast<uint16_t>(kMaxEntities)) {
      id = static_cast<uint16_t>(*highWater + 1u);
      *highWater = id;
    } else {
      return SpawnResult{false, kNoEntityId, SpawnFailure::kEntityPoolExhausted};
    }

    Entity& entity = entities[id - 1u];
    resetEntity(entity);  // 复用必须整体重置，历史数据不得残留
    entity.id = id;
    entity.active = true;
    entity.kind = params.kind;
    entity.pos = params.pos;
    entity.yaw = params.yaw;
    entity.pitch = params.pitch;
    entity.team = params.team;
    entity.ownerId = params.ownerId;
    entity.hp = params.hp;
    // v1 world.ts:231 的 entity.maxHp = stats.hp：maxHp 恒为该 kind 的基础生命，
    // params.hp 只覆盖当前生命（S05 为快照/用例加的显式入口）。
    entity.maxHp = static_cast<double>(ac::config::kKindBaseStats[static_cast<std::size_t>(params.kind)].hp);
    entity.armor = params.armor;

    // 复用的 id 可能小于表内既有 id：按 lower_bound 插入，保持 §5.1 的严格升序。
    const std::size_t insertAt = read().lowerBoundActive(id);
    for (std::size_t i = *activeCount; i > insertAt; --i) activeIds[i] = activeIds[i - 1u];
    activeIds[insertAt] = id;
    ++(*activeCount);
    return SpawnResult{true, id, SpawnFailure::kNone};
  }

  // §5.2：active = false、压入空闲栈、从升序表移除；重复释放同一 id 返回 false。
  bool release(uint16_t id) noexcept {
    if (id == kNoEntityId || id > *highWater) return false;
    Entity& entity = entities[id - 1u];
    if (!entity.active) return false;  // 重复释放：不改任何状态
    const std::size_t position = read().activeIndexOf(id);
    if (position == kMaxEntities) return false;  // 升序表被破坏（不该发生）
    entity.active = false;                       // 只清 active：字段留给下一次分配时整体重置
    for (std::size_t i = position; i + 1u < *activeCount; ++i) activeIds[i] = activeIds[i + 1u];
    --(*activeCount);
    freeIds[(*freeCount)++] = id;
    return true;
  }
};

}  // namespace ac::sim
