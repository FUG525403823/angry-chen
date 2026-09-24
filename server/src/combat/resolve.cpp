// S08 §5.6：权威命中结算。逐行对齐 v1 combat/resolve.ts（阶段顺序、事件顺序、数值口径都不动）。
#include "combat/resolve.hpp"

#include <cmath>

#include "combat/damage.hpp"
#include "combat/downed.hpp"
#include "combat/rage.hpp"
#include "combat/weapon.hpp"
#include "config/player.hpp"
#include "config/weapons.hpp"
#include "core/math.hpp"
#include "sim/arena.hpp"
#include "sim/step.hpp"

namespace ac::sim {
namespace {

bool isCombatTarget(const Entity& entity) noexcept {
  return entity.active && (entity.kind == EntityKind::kPlayer || entity.kind == EntityKind::kSheep);
}

const ac::config::SheepHitProfile& sheepHitProfile(const Entity& target) noexcept {
  const uint8_t kind = target.sheepKind < 4u ? target.sheepKind : 0u;
  return ac::config::kSheepHit[kind];
}

double fireRateMultiplierFor(const Entity& entity, double nowMs) noexcept {
  return ac::combat::isRageActive(entity.rage, nowMs) ? ac::config::kRageFireRateMultiplier : 1.0;
}

void applyHit(World& world, Entity& shooter, const ShotTrace& trace, const ac::config::WeaponDef& def, bool rage,
              CombatCounters* counters) noexcept;

void fireWeapon(World& world, Entity& shooter, const Command& command, bool rage, const PoseHistory* history,
                double rewindMs, CombatCounters* counters, ShotTrace& out) noexcept {
  const ac::config::WeaponDef& def = ac::combat::activeWeaponDef(shooter.weapon);
  if (!ac::combat::tryFire(shooter.weapon, static_cast<double>(world.timeMs), rage ? ac::config::kRageFireRateMultiplier : 1.0)) {
    return;
  }
  if (counters != nullptr) counters->shotsFired += 1u;

  const double eyeY = shooter.pos.y + ac::config::kEyeHeightM;
  const double spreadDeg = def.spreadDeg + shooter.weapon.spreadDeg;
  for (int32_t pellet = 0; pellet < def.pellets; ++pellet) {
    const double yawJitter = spreadDeg *
                             ac::config::signedJitter(command.seq + static_cast<uint32_t>(pellet) * ac::config::kPelletYawStride,
                                                      ac::config::kJitterYawSalt) *
                             ac::config::kDegToRad;
    const double pitchJitter = spreadDeg *
                               ac::config::signedJitter(command.seq + static_cast<uint32_t>(pellet) * ac::config::kPelletPitchStride,
                                                        ac::config::kJitterPitchSalt) *
                               ac::config::kDegToRad;
    double pitch = command.pitch + pitchJitter;
    if (pitch > ac::config::kAimPitchLimitRad) pitch = ac::config::kAimPitchLimitRad;
    else if (pitch < -ac::config::kAimPitchLimitRad) pitch = -ac::config::kAimPitchLimitRad;
    const ac::Vec3 direction = ac::yawPitchToDirection(ac::wrapAngle(command.yaw + yawJitter), pitch);
    traceRay(world, history, shooter.id, shooter.pos.x, eyeY, shooter.pos.z, direction.x, direction.y, direction.z,
             ac::config::kShotMaxDistanceM, rewindMs, out);
    if (!out.hit) continue;
    applyHit(world, shooter, out, def, rage, counters);
  }
}

void applyHit(World& world, Entity& shooter, const ShotTrace& trace, const ac::config::WeaponDef& def, bool rage,
              CombatCounters* counters) noexcept {
  Entity* found = entityById(world, trace.targetId);
  if (found == nullptr || !found->active) return;
  Entity& target = *found;
  if (target.id == shooter.id) return;
  if (!ac::config::kFriendlyFire && target.team == shooter.team) return;

  const bool wasDowned = target.kind == EntityKind::kPlayer && target.downed.downed;
  ac::combat::DamageResult damage{};
  ac::combat::computeDamage(def, trace.part, trace.distanceM, rage, target.armor, damage);
  const double total = damage.hpDamage + damage.armorDamage;
  if (counters != nullptr) {
    counters->hits += 1u;
    counters->damage += total;
  }

  const double armorLeft = target.armor - damage.armorDamage;
  target.armor = armorLeft > 0.0 ? armorLeft : 0.0;
  const double hpLeft = target.hp - damage.hpDamage;
  target.hp = hpLeft > 0.0 ? hpLeft : 0.0;
  if (target.kind == EntityKind::kPlayer) ac::combat::noteCombat(target.rage, static_cast<double>(world.timeMs));

  uint8_t flags = 0u;
  if (damage.isHeadshot) flags |= ac::config::kHitFlagHeadshot;
  if (wasDowned) flags |= ac::config::kHitFlagDowned;

  bool killed = false;
  if (target.hp <= 0.0) {
    if (target.kind == EntityKind::kSheep) {
      killed = true;
      flags |= ac::config::kHitFlagKilled;
    } else if (!wasDowned) {
      flags |= ac::config::kHitFlagKilled;
      ac::combat::markDowned(target.downed, static_cast<double>(world.timeMs));
      pushEvent(world, kEventPlayerDowned, 0u, target.id, 0u, target.pos.x, target.pos.y, target.pos.z, 0.0);
    }
  }

  pushEvent(world, kEventPlayerHit, flags, shooter.id, target.id, trace.x, trace.y, trace.z, total);

  if (!killed) return;
  const bool isElite = target.state == ac::config::kSheepEliteState;
  ac::combat::addKillRage(shooter.rage, isElite, damage.isHeadshot, static_cast<double>(world.timeMs));
  pushEvent(world, kEventSheepKilled, flags, shooter.id, target.id, target.pos.x, target.pos.y, target.pos.z, total,
            target.sheepKind);
  despawnEntity(world, target.id);
}

void updateRevives(World& world, uint32_t dtMs, double nowMs) noexcept {
  const double dt = static_cast<double>(dtMs);
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* victimFound = entityById(world, world.activeIds[i]);
    if (victimFound == nullptr || !victimFound->active || victimFound->kind != EntityKind::kPlayer) continue;
    Entity& victim = *victimFound;
    if (!victim.downed.downed) continue;

    EntityId reviverId = kNoEntityId;
    for (std::size_t j = 0u; j < world.activeCount; ++j) {
      Entity* otherFound = entityById(world, world.activeIds[j]);
      if (otherFound == nullptr || !otherFound->active || otherFound->kind != EntityKind::kPlayer) continue;
      const Entity& other = *otherFound;
      if (other.id == victim.id) continue;
      if (other.downed.downed || other.team != victim.team) continue;
      if (!other.interactHeld) continue;
      const double speed = std::sqrt(other.vel.x * other.vel.x + other.vel.z * other.vel.z);
      if (speed > ac::config::kRescueSpeedClampMps) continue;
      const double dx = other.pos.x - victim.pos.x;
      const double dz = other.pos.z - victim.pos.z;
      if (dx * dx + dz * dz > ac::config::kReviveRangeM * ac::config::kReviveRangeM) continue;
      reviverId = other.id;
      break;
    }

    const ac::combat::ReviveOutcome outcome =
        ac::combat::reviveStep(victim.downed, nowMs, dt, reviverId, reviverId != kNoEntityId);
    const double ratio = ac::combat::reviveRatio(victim.downed);
    if (outcome == ac::combat::ReviveOutcome::kProgress || outcome == ac::combat::ReviveOutcome::kDone) {
      if (outcome == ac::combat::ReviveOutcome::kDone ||
          ratio - victim.downed.lastEventRatio >= ac::config::kProgressEventStepRatio) {
        victim.downed.lastEventRatio = ratio;
        pushEvent(world, kEventReviveProgress, 0u, reviverId, victim.id, victim.pos.x, victim.pos.y, victim.pos.z,
                  std::floor(ratio * 1000.0 + 0.5));
      }
    } else if (outcome == ac::combat::ReviveOutcome::kInterrupted) {
      pushEvent(world, kEventReviveProgress, 0u, kNoEntityId, victim.id, victim.pos.x, victim.pos.y, victim.pos.z,
                std::floor(ratio * 1000.0 + 0.5));
    }

    if (outcome != ac::combat::ReviveOutcome::kDone) continue;
    victim.hp = ac::combat::reviveTo(victim.downed, victim.maxHp, ac::config::kRevivedHpRatio);
    pushEvent(world, kEventReviveDone, 0u, reviverId, victim.id, victim.pos.x, victim.pos.y, victim.pos.z, victim.hp);
  }
}

}  // namespace

ShotTrace& createShotTrace(ShotTrace& out) noexcept {
  out.hit = false;
  out.targetId = kNoEntityId;
  out.targetKind = EntityKind::kSheep;
  out.part = ac::config::HitPart::kTorso;
  out.distanceM = 0.0;
  out.x = 0.0;
  out.y = 0.0;
  out.z = 0.0;
  return out;
}

ShotTrace& traceRay(const World& world, const PoseHistory* history, EntityId shooterId, double originX,
                    double originY, double originZ, double dx, double dy, double dz, double maxDistanceM,
                    double rewindMs, ShotTrace& out) noexcept {
  out.hit = false;
  out.targetId = kNoEntityId;
  out.distanceM = 0.0;
  out.x = 0.0;
  out.y = 0.0;
  out.z = 0.0;

  const ac::config::ArenaConfig& arena = ac::config::kArenaConfig;
  ac::combat::RayHit rayScratch;
  ac::combat::RayHit targetScratch;
  ac::combat::RayHit headScratch;
  ac::combat::rayVsAabb(originX, originY, originZ, dx, dy, dz, arena.barnMin.x, arena.barnMin.y, arena.barnMin.z,
                        arena.barnMax.x, arena.barnMax.y, arena.barnMax.z, maxDistanceM, rayScratch);
  double bestT = rayScratch.hit ? rayScratch.t : maxDistanceM;

  const double horizontalSq = dx * dx + dz * dz;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const Entity* targetFound = entityById(world, world.activeIds[i]);
    if (targetFound == nullptr || !targetFound->active) continue;
    const Entity& target = *targetFound;
    if (target.id == shooterId || !isCombatTarget(target)) continue;

    double tx = target.pos.x;
    double ty = target.pos.y;
    double tz = target.pos.z;
    if (history != nullptr && target.kind == EntityKind::kPlayer) {
      SampledPose pose{};
      ac::sim::samplePoseAgo(*history, static_cast<uint32_t>(rewindMs), target.id, pose);
      if (pose.found) {
        tx = pose.x;
        ty = pose.y;
        tz = pose.z;
      }
    }

    // 命中体口径：羊用 SHEEP_HIT（躯干盒 + 前伸头盒，在羊局部坐标系里求交），玩家用 ENTITY 的竖直胶囊。
    if (target.kind == EntityKind::kSheep) {
      const ac::config::SheepHitProfile& profile = sheepHitProfile(target);
      // 世界 -> 羊局部：绕 Y 轴反向旋转（局部 +Z = 羊的正前方）。v1 用 Math.sin/cos；
      // 导出侧已把 v1 的 Math.sin/cos 换成共享整数表，所以 C++ 侧同样只能走 S02 的整数表。
      const ac::Vec3 forward = ac::forwardFromYaw(target.yaw);
      const double sinYaw = forward.x;
      const double cosYaw = forward.z;
      const double relX = originX - tx;
      const double relZ = originZ - tz;
      const double lox = cosYaw * relX - sinYaw * relZ;
      const double loz = sinYaw * relX + cosYaw * relZ;
      const double ldx = cosYaw * dx - sinYaw * dz;
      const double ldz = sinYaw * dx + cosYaw * dz;
      const double relY = originY - ty;
      // 廉价早退（不改变命中结果）：盒体包围球之外必然打不中。v1 用 Math.hypot（实现自定）；
      // 这里用 sqrt(w*w + d*d) —— 与 Math.hypot 最多差 1 ULP，只影响这个早退的边界候选（README §9 已声明）。
      const double reach = std::sqrt(profile.halfWidthM * profile.halfWidthM + profile.halfDepthM * profile.halfDepthM);
      if (horizontalSq > ac::combat::kRayEpsilon) {
        double along = ((tx - originX) * dx + (tz - originZ) * dz) / horizontalSq;
        if (along < 0.0) along = 0.0;
        else if (along > maxDistanceM) along = maxDistanceM;
        const double gapX = originX + dx * along - tx;
        const double gapZ = originZ + dz * along - tz;
        if (gapX * gapX + gapZ * gapZ > reach * reach) continue;
      }
      ac::combat::rayVsAabb(lox, relY, loz, ldx, dy, ldz, -profile.halfWidthM, 0.0, -profile.halfDepthM,
                            profile.halfWidthM, profile.topM, profile.halfDepthM, maxDistanceM, targetScratch);
      double hitT = targetScratch.hit ? targetScratch.t : 1.0e308 * 1.0e308;
      ac::config::HitPart part = ac::config::HitPart::kLimb;
      if (targetScratch.hit) {
        part = ac::config::partForThresholds(ty, profile.headMinM, profile.torsoMinM, originY + dy * hitT);
      }
      ac::combat::rayVsAabb(lox, relY, loz, ldx, dy, ldz, -profile.headHalfWidthM, profile.headMinYM,
                            profile.headMinZM, profile.headHalfWidthM, profile.headMaxYM, profile.headMaxZM,
                            maxDistanceM, headScratch);
      if (headScratch.hit && headScratch.t < hitT) {
        hitT = headScratch.t;
        part = ac::config::HitPart::kHead;
      }
      if (hitT >= bestT) continue;
      bestT = hitT;
      out.hit = true;
      out.targetId = target.id;
      out.targetKind = target.kind;
      out.part = part;
      out.distanceM = hitT;
      out.x = originX + dx * hitT;
      out.y = originY + dy * hitT;
      out.z = originZ + dz * hitT;
      continue;
    }

    // —— 玩家：ENTITY 口径的竖直胶囊（配合历史姿态回滚）——
    const ac::sim::arena::EntityDimensions dimensions =
        ac::sim::arena::kKindDimensions[static_cast<std::size_t>(target.kind)];
    const double radius = dimensions.radius;
    const double height = dimensions.height;
    if (horizontalSq > ac::combat::kRayEpsilon) {
      double along = ((tx - originX) * dx + (tz - originZ) * dz) / horizontalSq;
      if (along < 0.0) along = 0.0;
      else if (along > maxDistanceM) along = maxDistanceM;
      const double gapX = originX + dx * along - tx;
      const double gapZ = originZ + dz * along - tz;
      if (gapX * gapX + gapZ * gapZ > radius * radius) continue;
    }
    const double axisY = dy >= 0.0 ? ty + radius : ty + height - radius;
    const double minAlong = (tx - originX) * dx + (axisY - originY) * dy + (tz - originZ) * dz;
    if (minAlong - radius > bestT) continue;
    ac::combat::rayVsCapsule(originX, originY, originZ, dx, dy, dz, tx, ty + radius, tz, tx, ty + height - radius,
                             tz, radius, maxDistanceM, targetScratch);
    if (!targetScratch.hit || targetScratch.t >= bestT) continue;
    bestT = targetScratch.t;
    out.hit = true;
    out.targetId = target.id;
    out.targetKind = target.kind;
    out.part = ac::config::partForHeight(ty, height, targetScratch.y);
    out.distanceM = targetScratch.t;
    out.x = targetScratch.x;
    out.y = targetScratch.y;
    out.z = targetScratch.z;
  }
  return out;
}

uint32_t reviveDownedForWaveClear(World& world) noexcept {
  uint32_t revived = 0u;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* found = entityById(world, world.activeIds[i]);
    if (found == nullptr || !found->active || found->kind != EntityKind::kPlayer) continue;
    Entity& entity = *found;
    if (!entity.downed.downed) continue;
    entity.hp = ac::combat::reviveTo(entity.downed, entity.maxHp, ac::config::kWaveReviveHpRatio);
    revived += 1u;
    pushEvent(world, kEventReviveDone, 0u, kNoEntityId, entity.id, entity.pos.x, entity.pos.y, entity.pos.z, entity.hp);
  }
  return revived;
}

bool hasDownedTeammateInRange(const World& world, const Entity& entity, double rangeM) noexcept {
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const Entity* other = entityById(world, world.activeIds[i]);
    if (other == nullptr || !other->active || other->kind != EntityKind::kPlayer) continue;
    if (other->id == entity.id || other->team != entity.team) continue;
    if (!other->downed.downed) continue;
    const double dx = other->pos.x - entity.pos.x;
    const double dz = other->pos.z - entity.pos.z;
    if (dx * dx + dz * dz <= rangeM * rangeM) return true;
  }
  return false;
}

// §5.6/§9：四段顺序固定 —— 1) 全玩家 updateWeapon；2) 命令游标（switchTo → reload → rage → fire）；
// 3) 全玩家 updateRage；4) updateRevives。commands 不足的玩家与 v1 一致：跳过（interactHeld 保持上一 tick）。
void resolveCombat(World& world, const Command* commands, uint32_t commandCount, uint32_t dtMs,
                   const CombatContext* context) noexcept {
  const PoseHistory* history = context == nullptr ? nullptr : context->history;
  CombatCounters* counters = context == nullptr ? nullptr : context->counters;
  double (*rewindMsFor)(void*, EntityId) = context == nullptr ? nullptr : context->rewindMsFor;
  void* rewindUser = context == nullptr ? nullptr : context->rewindUser;
  const double nowMs = static_cast<double>(world.timeMs);

  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* found = entityById(world, world.activeIds[i]);
    if (found == nullptr || !found->active || found->kind != EntityKind::kPlayer) continue;
    ac::combat::updateWeapon(found->weapon, nowMs, static_cast<double>(dtMs), fireRateMultiplierFor(*found, nowMs));
  }

  uint32_t slot = 0u;
  ShotTrace traceScratch;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* found = entityById(world, world.activeIds[i]);
    if (found == nullptr || !found->active || found->kind != EntityKind::kPlayer) continue;
    Entity& entity = *found;
    if (slot >= commandCount) break;
    const Command& raw = commands[slot];
    slot += 1u;
    entity.interactHeld = (raw.buttons & ac::config::kButtonInteract) != 0u;
    if (entity.downed.downed) continue;

    const bool rage = ac::combat::isRageActive(entity.rage, nowMs);
    if ((raw.buttons & ac::config::kButtonSwitchWeapon) != 0u) {
      ac::combat::switchSlot(entity.weapon, raw.switchTo, nowMs);
    }
    if ((raw.buttons & ac::config::kButtonReload) != 0u) ac::combat::tryStartReload(entity.weapon, nowMs);
    if ((raw.buttons & ac::config::kButtonRage) != 0u && ac::combat::activateRage(entity.rage, nowMs)) {
      pushEvent(world, kEventRageActivated, 0u, entity.id, entity.id, entity.pos.x, entity.pos.y, entity.pos.z,
                ac::config::kRageDurationMs);
    }
    if ((raw.buttons & ac::config::kButtonFire) == 0u) continue;
    const double rewindMs = rewindMsFor == nullptr ? 0.0 : rewindMsFor(rewindUser, entity.id);
    fireWeapon(world, entity, raw, rage, history, rewindMs, counters, traceScratch);
  }

  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* found = entityById(world, world.activeIds[i]);
    if (found == nullptr || !found->active || found->kind != EntityKind::kPlayer) continue;
    ac::combat::updateRage(found->rage, nowMs, static_cast<double>(dtMs));
  }

  updateRevives(world, dtMs, nowMs);
}

}  // namespace ac::sim
