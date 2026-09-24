#include "ai/sheep_brain.hpp"

#include <cmath>

#include "ai/flocking.hpp"
#include "ai/steering.hpp"
#include "ai/targeting.hpp"
#include "config/player.hpp"
#include "config/waves.hpp"
#include "core/rng.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"

namespace ac::ai {
namespace {

// v1 的补丁口径：方向角走 S02 的整数表（angleUnitsFromVector → radiansFromUnits），不做 wrapAngle。
double yawFromDelta(double dx, double dz) noexcept {
  return ac::radiansFromUnits(ac::angleUnitsFromVector(dx, dz));
}

// v1 blendFlock：把 flock 三权重组按 0.5 叠加进当前转向向量。
void blendFlock(const ac::sim::Entity& entity, SteeringOut& into) noexcept {
  if (entity.ai.flock.count <= 0) return;
  SteeringOut flockScratch = createSteeringOut();
  flockForce(flockScratch, entity.pos.x, entity.pos.z, entity.ai.flock,
             std::sqrt(into.x * into.x + into.z * into.z));
  into.x += flockScratch.x * ac::config::kFlockBlendRatio;
  into.z += flockScratch.z * ac::config::kFlockBlendRatio;
}

}  // namespace

void applySheepKind(ac::sim::Entity& entity, ac::config::SheepKind kind) noexcept {
  const ac::config::SheepDef& def = ac::config::sheepDefFor(kind);
  entity.sheepKind = static_cast<uint8_t>(kind);
  entity.hp = def.hp;
  entity.maxHp = def.hp;
  entity.armor = 0.0;
}

ac::config::SheepKind sheepKindOf(const ac::sim::Entity& entity) noexcept {
  const uint8_t code = entity.sheepKind;
  return code == 1u ? ac::config::SheepKind::kRam
                    : (code == 2u ? ac::config::SheepKind::kElite
                                  : (code == 3u ? ac::config::SheepKind::kKing
                                                : ac::config::SheepKind::kGrunt));
}

bool canSheepTransition(int32_t from, int32_t to) noexcept { return ac::config::canSheepTransition(from, to); }

bool setSheepState(ac::sim::Entity& entity, int32_t next) noexcept {
  if (!canSheepTransition(static_cast<int32_t>(entity.state), next)) return false;
  entity.state = static_cast<uint8_t>(next);
  return true;
}

void noteSheepHit(ac::sim::Entity& entity, const uint16_t* playerIds, uint32_t playerCount,
                  uint16_t pid) noexcept {
  noteHit(entity.ai.target, playerIds, playerCount, pid);
}

SheepIntent updateSheepIntent(ac::sim::World& world, ac::sim::Entity& entity, const uint16_t* playerIds,
                              uint32_t playerCount, double speedMultiplier, double dtMs) noexcept {
  SheepAiState& ai = entity.ai;
  const ac::config::SheepKind kind = sheepKindOf(entity);
  const ac::config::SheepDef& def = ac::config::sheepDefFor(kind);
  const ac::config::SheepAiParams& params = ac::config::kSheepAi;
  const double kingBoost =
      (kind == ac::config::SheepKind::kKing && ai.phase >= 3) ? params.kingPhase3SpeedMultiplier : 1.0;
  const double baseSpeed = def.speed * speedMultiplier * kingBoost;

  SheepIntent out{};
  out.x = 0.0;
  out.z = 0.0;
  out.speed = baseSpeed;
  out.yaw = entity.yaw;

  if (ai.timerMs > 0.0) {
    const double left = ai.timerMs - dtMs;
    ai.timerMs = left > 0.0 ? left : 0.0;
  }
  if (ai.cooldownMs > 0.0) {
    const double left = ai.cooldownMs - dtMs;
    ai.cooldownMs = left > 0.0 ? left : 0.0;
  }

  if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kDead)) {
    out.speed = 0.0;
    return out;
  }

  if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kStagger)) {
    if (ai.timerMs <= 0.0) setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kChase));
    out.speed = 0.0;
    return out;
  }

  decayAggro(ai.target, static_cast<int32_t>(playerCount));
  const uint16_t targetId = selectTarget(ai.target, world, entity, playerIds, playerCount);
  const ac::sim::Entity* target = targetId != 0u ? ac::sim::entityById(world, targetId) : nullptr;
  const bool hasTarget =
      target != nullptr && target->active && target->kind == ac::sim::EntityKind::kPlayer && target->hp > 0.0;
  const double distance = hasTarget ? ai.target.targetDistanceM : 0.0;

  if (!hasTarget) {
    if (entity.state != ac::config::sheepStateCode(ac::config::SheepState::kGraze)) {
      setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kGraze));
    }
    if (ai.timerMs <= 0.0) {
      ai.grazeX = entity.pos.x + ac::rngRange(world.rng.ai, -params.grazeRadiusM, params.grazeRadiusM);
      ai.grazeZ = entity.pos.z + ac::rngRange(world.rng.ai, -params.grazeRadiusM, params.grazeRadiusM);
      ai.timerMs = ac::config::kGrazeRepickMs;
    }
    SteeringOut steering = createSteeringOut();
    arrive(steering, entity.pos.x, entity.pos.z, ai.grazeX, ai.grazeZ,
           baseSpeed * ac::config::kGrazeSpeedRatio, ac::config::kArriveSlowRadiusM);
    blendFlock(entity, steering);
    SteeringOut avoid = createSteeringOut();
    obstacleAvoid(avoid, entity.pos.x, entity.pos.z, steering.x, steering.z, ac::config::kArenaConfig,
                  def.radiusM * ac::config::kGrazeObstacleRadiusRatio);
    out.x = avoid.x;
    out.z = avoid.z;
    return out;
  }

  const double targetX = target->pos.x;
  const double targetZ = target->pos.z;

  if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kGraze) ||
      entity.state == ac::config::sheepStateCode(ac::config::SheepState::kAlert)) {
    if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kGraze)) {
      // 只在"从吃草进入警戒"这一帧武装计时；同态转换返回 true，用它判分支会让羊永远停在警戒。
      setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kAlert));
      ai.timerMs = ac::config::kSheepAlertMs;
    } else if (ai.timerMs <= 0.0) {
      setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kChase));
    }
    out.speed = 0.0;
    return out;
  }

  if (kind == ac::config::SheepKind::kElite) {
    SteeringOut steering = createSteeringOut();
    if (distance < params.eliteKeepMinM) {
      seek(steering, entity.pos.x, entity.pos.z, targetX, targetZ, -baseSpeed);
    } else if (distance > params.eliteKeepMaxM) {
      seek(steering, entity.pos.x, entity.pos.z, targetX, targetZ, baseSpeed);
    } else {
      const double dx = targetX - entity.pos.x;
      const double dz = targetZ - entity.pos.z;
      const double length = std::sqrt(dx * dx + dz * dz);
      const double scale = (baseSpeed * ac::config::kEliteStrafeSpeedRatio) / (length > 0.0 ? length : 1.0);
      steering.x = -dz * scale * ai.strafeSign;
      steering.z = dx * scale * ai.strafeSign;
      if (ai.timerMs <= 0.0) {
        ai.timerMs = ac::config::kEliteStrafeMs;
        ai.strafeSign = -ai.strafeSign;
      }
    }
    if (distance <= params.eliteBoltRangeM) {
      setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kRanged));
    } else if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kRanged)) {
      setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kChase));
    }
    blendFlock(entity, steering);
    SteeringOut avoid = createSteeringOut();
    obstacleAvoid(avoid, entity.pos.x, entity.pos.z, steering.x, steering.z, ac::config::kArenaConfig,
                  def.radiusM);
    out.x = avoid.x;
    out.z = avoid.z;
    return out;
  }

  if (kind == ac::config::SheepKind::kRam) {
    if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kWindup)) {
      if (ai.timerMs <= 0.0) {
        if (setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kCharge))) {
          ai.timerMs = ac::config::kRamChargeMaxMs;
        }
      }
      out.speed = 0.0;
      return out;
    }
    if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kCharge)) {
      const bool chargeTimedOut = ai.timerMs <= 0.0;
      const ac::config::ArenaConfig& arena = ac::config::kArenaConfig;
      const bool inBarn = entity.pos.x > arena.barnMin.x && entity.pos.x < arena.barnMax.x &&
                          entity.pos.z > arena.barnMin.z && entity.pos.z < arena.barnMax.z;
      const bool outOfBounds =
          absoluteValue(entity.pos.x) > ac::config::kFieldEdgeLimitM || absoluteValue(entity.pos.z) > ac::config::kFieldEdgeLimitM;
      if (chargeTimedOut || inBarn || outOfBounds) {
        ai.chargeDirX = 0.0;
        ai.chargeDirZ = 0.0;
        if (setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kStagger))) {
          ai.timerMs = params.chargeStaggerMs;
        }
        out.speed = 0.0;
        return out;
      }
      out.x = ai.chargeDirX * params.chargeSpeedMps;
      out.z = ai.chargeDirZ * params.chargeSpeedMps;
      out.speed = params.chargeSpeedMps;
      out.yaw = yawFromDelta(ai.chargeDirX, ai.chargeDirZ);
      return out;
    }
    if (distance <= ac::config::kRamChargeTriggerM &&
        setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kWindup))) {
      ai.timerMs = params.chargeWindupMs;
      const double dx = targetX - entity.pos.x;
      const double dz = targetZ - entity.pos.z;
      const double length = std::sqrt(dx * dx + dz * dz);
      ai.chargeDirX = length > kSteeringZeroLengthM ? dx / length : 0.0;
      ai.chargeDirZ = length > kSteeringZeroLengthM ? dz / length : 1.0;
      out.speed = 0.0;
      out.yaw = yawFromDelta(ai.chargeDirX, ai.chargeDirZ);
      return out;
    }
  }

  if (distance <= params.attackRangeM &&
      (kind == ac::config::SheepKind::kGrunt || kind == ac::config::SheepKind::kKing)) {
    setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kAttack));
    out.speed = 0.0;
    out.yaw = yawFromDelta(targetX - entity.pos.x, targetZ - entity.pos.z);
    return out;
  }
  if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kAttack) &&
      distance > params.attackRangeM * 1.4) {
    setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kChase));
  }
  if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kRanged)) {
    setSheepState(entity, static_cast<int32_t>(ac::config::SheepState::kChase));
  }

  SteeringOut steering = createSteeringOut();
  seek(steering, entity.pos.x, entity.pos.z, targetX, targetZ, baseSpeed);
  out.yaw = yawFromDelta(targetX - entity.pos.x, targetZ - entity.pos.z);
  blendFlock(entity, steering);
  SteeringOut avoid = createSteeringOut();
  obstacleAvoid(avoid, entity.pos.x, entity.pos.z, steering.x, steering.z, ac::config::kArenaConfig,
                def.radiusM);
  out.x = avoid.x;
  out.z = avoid.z;
  return out;
}

}  // namespace ac::ai

namespace ac::sim {
namespace {

// v1 sim.ts 的意图暂存（定长、模块级、零分配）：阶段 4 写入，阶段 5 消费。
ac::ai::SheepIntent g_intents[kMaxEntities];
EntityId g_intentIds[kMaxEntities];
uint32_t g_intentCount = 0u;

}  // namespace

void updateAiIntents(World& world, uint32_t dtMs, const EntityId* playerIds, uint32_t playerCount,
                     const SpatialGrid& grid) noexcept {
  const double speedMultiplier = ac::config::sheepSpeedMultiplier(static_cast<int32_t>(playerCount));
  const double dt = static_cast<double>(dtMs);
  g_intentCount = 0u;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* found = entityById(world, world.activeIds[i]);
    if (found == nullptr || !found->active || found->kind != EntityKind::kSheep) continue;
    Entity& entity = *found;
    if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kDead)) {
      entity.ai.timerMs += dt;
      continue;
    }
    ac::ai::gatherNeighbors(entity.ai.flock, world, entity, grid);
    g_intents[g_intentCount] =
        ac::ai::updateSheepIntent(world, entity, playerIds, playerCount, speedMultiplier, dt);
    g_intentIds[g_intentCount] = entity.id;
    ++g_intentCount;
  }
}

void applyAiIntents(World& world) noexcept {
  for (uint32_t i = 0u; i < g_intentCount; ++i) {
    Entity* found = entityById(world, g_intentIds[i]);
    if (found == nullptr || !found->active || found->kind != EntityKind::kSheep) continue;
    Entity& entity = *found;
    if (entity.state == ac::config::sheepStateCode(ac::config::SheepState::kDead)) {
      entity.vel.x = 0.0;
      entity.vel.y = 0.0;
      entity.vel.z = 0.0;
      if (entity.ai.timerMs >= ac::config::kSheepAi.deadFadeMs) despawnEntity(world, entity.id);
      continue;
    }
    const ac::ai::SheepIntent& intent = g_intents[i];
    entity.vel.x = intent.x;
    entity.vel.z = intent.z;
    entity.vel.y = 0.0;
    entity.yaw = intent.yaw;
  }
}

}  // namespace ac::sim
