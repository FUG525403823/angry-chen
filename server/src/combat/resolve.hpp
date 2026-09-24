#pragma once
// S08 §5.6/§9：权威命中结算（命令 → 开火 → 射线 → 伤害 → 事件，含 200ms 回滚）。
#include <cstdint>

#include "combat/raycast.hpp"
#include "config/combat.hpp"
#include "sim/entity_table.hpp"
#include "sim/pose_history.hpp"
#include "sim/world.hpp"

namespace ac::sim {

struct CombatCounters {
  uint32_t shotsFired = 0u;
  uint32_t hits = 0u;
  double damage = 0.0;
};

// §5.6/§9：回滚上下文。history 为空、counters 为空时各自功能整体关闭；rewindMsFor 返回该射手的
// 回滚毫秒数（v1 的同名回调），nullptr 时按 0 处理。
struct CombatContext {
  const PoseHistory* history = nullptr;
  CombatCounters* counters = nullptr;
  double (*rewindMsFor)(void* user, EntityId shooterId) = nullptr;
  void* rewindUser = nullptr;
};

struct ShotTrace {
  bool hit = false;
  EntityId targetId = kNoEntityId;
  EntityKind targetKind = EntityKind::kSheep;
  ac::config::HitPart part = ac::config::HitPart::kTorso;
  double distanceM = 0.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

ShotTrace& createShotTrace(ShotTrace& out) noexcept;

// §5.3：先对谷仓 AABB 求交取 bestT，再按 EntityId 升序逐个尝试（跳过射手自身），最近命中者胜。
ShotTrace& traceRay(const World& world, const PoseHistory* history, EntityId shooterId, double originX,
                    double originY, double originZ, double dx, double dy, double dz, double maxDistanceM,
                    double rewindMs, ShotTrace& out) noexcept;

// §5.7：波间复苏（所有倒地玩家按 waveReviveHpRatio 复活），返回复苏人数。
uint32_t reviveDownedForWaveClear(World& world) noexcept;
// §5.7：范围内是否有倒地队友（S09 的羊形 AI 与阶段 1 的救援限速都读它）。
bool hasDownedTeammateInRange(const World& world, const Entity& entity, double rangeM) noexcept;

}  // namespace ac::sim
