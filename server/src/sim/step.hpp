#pragma once
// S06 §5.1：权威步进入口与 13 个阶段。本份实现 0/1/2/7/8/9/12，其余为空实现但签名一次冻结
// （S08 消费 resolveCombat，S09 消费 updateAiIntents / applyAiIntents）。
#include <cstdint>

#include "sim/collision.hpp"
#include "sim/movement.hpp"
#include "sim/world.hpp"

namespace ac::sim {

struct CombatContext;  // S08 定义

// §5.1/§9：固定 50ms 的权威步进入口。dtMs != 50 时返回 false 且世界不变（校验发生在阶段 0 之前）。
bool stepWorld(World& world, const Command* commands, uint32_t commandCount, uint32_t dtMs) noexcept;

// 阶段 1：按玩家 EntityId 升序应用命令 —— commands[k] 属于第 k 个升序玩家实体；
// 命令不足的玩家走 §5.2 的归零分支（游标语义与 S08 §5 的命令游标一致）。
void applyCommands(World& world, const Command* commands, uint32_t commandCount) noexcept;

// 阶段 2：升序收集非 idle 玩家的 EntityId，返回条数；out 容量至少 kMaxEntities。
uint32_t collectPlayerIds(const World& world, EntityId* out) noexcept;

// 阶段 3/9 复用 S05 的 buildSpatialGrid。

// 阶段 4/5：S09 落地的空实现（签名按 S09 §9 冻结）。
void updateAiIntents(World& world, uint32_t dtMs, const EntityId* playerIds, uint32_t playerCount,
                     const SpatialGrid& grid) noexcept;
void applyAiIntents(World& world) noexcept;

// 阶段 6：S09 落地（S06 的占位签名补上 dtMs —— 击退时长要按 tick 递减）。
void applyKnockback(World& world, uint32_t dtMs) noexcept;

// 阶段 10/11：S08 / S09 落地（返回值 = 本阶段落地的事件条数 / 弹丸数 / 召唤数）。
// 签名按下游计划的移交物冻结：resolveCombat 见 S08 §9；resolveSheepAttacks / resolveEliteFire /
// advanceProjectiles / updateKing 见 S09 §9（S06 的占位签名只钉住前缀，参数表由 S09 补齐）。
void resolveCombat(World& world, const Command* commands, uint32_t commandCount, uint32_t dtMs,
                   const CombatContext* context) noexcept;
int resolveSheepAttacks(World& world, const EntityId* playerIds, uint32_t playerCount) noexcept;
int resolveEliteFire(World& world, const EntityId* playerIds, uint32_t playerCount) noexcept;
int advanceProjectiles(World& world, uint32_t dtMs, const EntityId* playerIds,
                       uint32_t playerCount) noexcept;
int updateKing(World& world, Entity& king, uint32_t dtMs) noexcept;
// 阶段 11 的循环入口：遍历存活羊王逐个调 updateKing，返回本 tick 召唤总数。
int updateKings(World& world, uint32_t dtMs) noexcept;

}  // namespace ac::sim
