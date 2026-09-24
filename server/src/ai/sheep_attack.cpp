#include "ai/sheep_attack.hpp"

#include <cmath>
#include <limits>

#include "ai/sheep_brain.hpp"
#include "ai/steering.hpp"
#include "combat/damage.hpp"
#include "combat/downed.hpp"
#include "config/combat.hpp"
#include "config/player.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"

namespace ac::ai {

void knockbackPlayer(ac::sim::Entity& target, double fromX, double fromZ, double distanceM) noexcept {
  const double dx = target.pos.x - fromX;
  const double dz = target.pos.z - fromZ;
  const double length = std::sqrt(dx * dx + dz * dz);
  const double speed = ac::config::kSheepAi.knockbackVelocityMps;
  const double durationMs = (distanceM / speed) * 1000.0;
  target.knock.knockMs = target.knock.knockMs > durationMs ? target.knock.knockMs : durationMs;
  target.knock.knockVx = length > kSteeringZeroLengthM ? (dx / length) * speed : 0.0;
  target.knock.knockVz = length > kSteeringZeroLengthM ? (dz / length) * speed : speed;
}

double damagePlayer(ac::sim::World& world, ac::sim::Entity& attacker, ac::config::SheepKind kind,
                    ac::sim::Entity& target, double distanceM, double knockbackM) noexcept {
  ac::combat::DamageResult damage{};
  ac::combat::computeDamage(ac::config::kSheepAttackProfile[ac::config::sheepIndexOf(kind)],
                            ac::config::HitPart::kTorso, distanceM, false, target.armor, damage);
  const double armorDamage = damage.armorDamage;
  const double hpDamage = damage.hpDamage;
  const double armorLeft = target.armor - armorDamage;
  target.armor = armorLeft > 0.0 ? armorLeft : 0.0;
  const double hpLeft = target.hp - hpDamage;
  target.hp = hpLeft > 0.0 ? hpLeft : 0.0;

  uint8_t flags = 0u;
  if (target.hp <= 0.0 && !target.downed.downed) {
    flags |= ac::config::kHitFlagDowned | ac::config::kHitFlagKilled;
    ac::combat::markDowned(target.downed, static_cast<double>(world.timeMs));
    ac::sim::pushEvent(world, ac::sim::kEventPlayerDowned, 0u, attacker.id, target.id, target.pos.x,
                       target.pos.y, target.pos.z, 0.0);
  }
  ac::sim::pushEvent(world, ac::sim::kEventPlayerHit, flags, attacker.id, target.id, target.pos.x,
                     target.pos.y + ac::config::kVictimChestHeightM, target.pos.z,
                     damage.hpDamage + damage.armorDamage);
  if (knockbackM > 0.0 && target.hp > 0.0) {
    knockbackPlayer(target, attacker.pos.x, attacker.pos.z, knockbackM);
  }
  return damage.hpDamage + damage.armorDamage;
}

uint16_t spawnQuestionBolt(ac::sim::World& world, ac::sim::Entity& shooter, ac::sim::Entity& target) noexcept {
  const double dx = target.pos.x - shooter.pos.x;
  const double dz = target.pos.z - shooter.pos.z;
  const double length = std::sqrt(dx * dx + dz * dz);
  if (length <= 1e-6) return 0u;

  ac::sim::SpawnParams params{};
  params.kind = ac::sim::EntityKind::kProjectile;
  params.pos = ac::Vec3{shooter.pos.x, shooter.pos.y + ac::config::kQuestionBoltSpawnHeightM, shooter.pos.z};
  params.team = ac::config::kDefaultTeamByKind[static_cast<std::size_t>(ac::sim::EntityKind::kProjectile)];
  params.ownerId = shooter.id;
  params.hp = static_cast<double>(ac::config::kKindBaseStats[static_cast<std::size_t>(ac::sim::EntityKind::kProjectile)].hp);
  params.armor = ac::config::kKindBaseStats[static_cast<std::size_t>(ac::sim::EntityKind::kProjectile)].armor;
  const ac::sim::SpawnResult result = ac::sim::spawnEntity(world, params);
  if (!result.isOk) return 0u;
  ac::sim::Entity* questionBolt = ac::sim::entityById(world, result.id);
  if (questionBolt == nullptr) return 0u;
  questionBolt->vel.x = (dx / length) * ac::config::kSheepAi.boltSpeedMps;
  questionBolt->vel.z = (dz / length) * ac::config::kSheepAi.boltSpeedMps;
  questionBolt->hp = 1.0;
  questionBolt->maxHp = 1.0;
  return questionBolt->id;
}

}  // namespace ac::ai

namespace ac::sim {
namespace {

}  // namespace

int32_t resolveSheepAttacks(World& world, const EntityId* playerIds, uint32_t playerCount) noexcept {
  int32_t events = 0;
  const double sheepRadius = ac::config::kSheepRadiusM;
  const double playerRadius = ac::config::kPlayerRadiusM;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* attackerFound = entityById(world, world.activeIds[i]);
    if (attackerFound == nullptr || !attackerFound->active || attackerFound->kind != EntityKind::kSheep) continue;
    Entity& attacker = *attackerFound;
    const ac::config::SheepKind kind = ac::ai::sheepKindOf(attacker);

    if (attacker.state == ac::config::sheepStateCode(ac::config::SheepState::kCharge)) {
      for (uint32_t p = 0u; p < playerCount; ++p) {
        Entity* player = entityById(world, playerIds[p]);
        if (player == nullptr || !player->active || player->hp <= 0.0) continue;
        const double dx = player->pos.x - attacker.pos.x;
        const double dz = player->pos.z - attacker.pos.z;
        const double reach = sheepRadius + playerRadius + 0.15;
        if (dx * dx + dz * dz > reach * reach) continue;
        ac::ai::damagePlayer(world, attacker, ac::config::SheepKind::kRam, *player,
                             std::sqrt(dx * dx + dz * dz), ac::config::kChargeKnockbackM);
        events += 2;
        attacker.state = ac::config::sheepStateCode(ac::config::SheepState::kStagger);
        attacker.ai.timerMs = ac::config::kSheepAi.chargeStaggerMs;
        attacker.ai.chargeDirX = 0.0;
        attacker.ai.chargeDirZ = 0.0;
        break;
      }
      continue;
    }
    if (attacker.state != ac::config::sheepStateCode(ac::config::SheepState::kAttack)) continue;
    if (attacker.ai.cooldownMs > 0.0) continue;
    for (uint32_t p = 0u; p < playerCount; ++p) {
      Entity* player = entityById(world, playerIds[p]);
      if (player == nullptr || !player->active || player->hp <= 0.0) continue;
      const double dx = player->pos.x - attacker.pos.x;
      const double dz = player->pos.z - attacker.pos.z;
      const double reach = ac::config::kSheepAi.attackRangeM + sheepRadius + playerRadius;
      if (dx * dx + dz * dz > reach * reach) continue;
      const ac::config::SheepKind biteKind =
          kind == ac::config::SheepKind::kKing ? ac::config::SheepKind::kKing : ac::config::SheepKind::kGrunt;
      ac::ai::damagePlayer(world, attacker, biteKind, *player, std::sqrt(dx * dx + dz * dz),
                           ac::config::kBiteKnockbackM);
      events += 2;
      attacker.ai.cooldownMs = ac::config::kSheepAi.attackCooldownMs;
      break;
    }
  }
  return events;
}

int32_t resolveEliteFire(World& world, const EntityId* playerIds, uint32_t playerCount) noexcept {
  int32_t questionBoltCount = 0;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* eliteFound = entityById(world, world.activeIds[i]);
    if (eliteFound == nullptr || !eliteFound->active || eliteFound->kind != EntityKind::kSheep) continue;
    Entity& elite = *eliteFound;
    if (ac::ai::sheepKindOf(elite) != ac::config::SheepKind::kElite) continue;
    if (elite.state != ac::config::sheepStateCode(ac::config::SheepState::kRanged)) continue;
    if (elite.ai.cooldownMs > 0.0) continue;

    Entity* target = nullptr;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (uint32_t p = 0u; p < playerCount; ++p) {
      Entity* player = entityById(world, playerIds[p]);
      if (player == nullptr || !player->active || player->hp <= 0.0) continue;
      const double dx = player->pos.x - elite.pos.x;
      const double dz = player->pos.z - elite.pos.z;
      const double distance = std::sqrt(dx * dx + dz * dz);
      if (distance > ac::config::kSheepAi.eliteBoltRangeM || distance >= bestDistance) continue;
      bestDistance = distance;
      target = player;
    }
    if (target == nullptr) continue;
    if (ac::ai::spawnQuestionBolt(world, elite, *target) == 0u) continue;
    elite.ai.cooldownMs = ac::config::kSheepAi.eliteBoltCooldownMs;
    elite.yaw = ac::radiansFromUnits(
        ac::angleUnitsFromVector(target->pos.x - elite.pos.x, target->pos.z - elite.pos.z));
    ++questionBoltCount;
  }
  return questionBoltCount;
}

int32_t advanceProjectiles(World& world, uint32_t dtMs, const EntityId* playerIds, uint32_t playerCount) noexcept {
  int32_t events = 0;
  const ac::config::ArenaConfig& arena = ac::config::kArenaConfig;
  for (std::size_t i = world.activeCount; i-- > 0u;) {
    Entity* questionBoltFound = entityById(world, world.activeIds[i]);
    if (questionBoltFound == nullptr || !questionBoltFound->active ||
        questionBoltFound->kind != EntityKind::kProjectile) {
      continue;
    }
    Entity& questionBolt = *questionBoltFound;
    questionBolt.aliveMs += dtMs;

    bool destroy = static_cast<double>(questionBolt.aliveMs) >= ac::config::kQuestionBoltLifeMs ||
                   ac::ai::absoluteValue(questionBolt.pos.x) > ac::config::kFieldEdgeLimitM ||
                   ac::ai::absoluteValue(questionBolt.pos.z) > ac::config::kFieldEdgeLimitM ||
                   (questionBolt.pos.x > arena.barnMin.x && questionBolt.pos.x < arena.barnMax.x &&
                    questionBolt.pos.z > arena.barnMin.z && questionBolt.pos.z < arena.barnMax.z);

    Entity* victim = nullptr;
    if (!destroy) {
      for (uint32_t p = 0u; p < playerCount; ++p) {
        Entity* player = entityById(world, playerIds[p]);
        if (player == nullptr || !player->active || player->hp <= 0.0) continue;
        const double dx = player->pos.x - questionBolt.pos.x;
        const double dz = player->pos.z - questionBolt.pos.z;
        const double reach = ac::config::kQuestionBoltRadiusM + ac::config::kPlayerRadiusM;
        if (dx * dx + dz * dz > reach * reach) continue;
        victim = player;
        break;
      }
    }
    if (victim != nullptr) {
      Entity* owner = entityById(world, questionBolt.ownerId);
      ac::ai::damagePlayer(world, owner != nullptr ? *owner : questionBolt, ac::config::SheepKind::kElite,
                           *victim, 0.0, 0.0);
      events += 2;
      destroy = true;
    }
    if (destroy) despawnEntity(world, questionBolt.id);
  }
  return events;
}

}  // namespace ac::sim
