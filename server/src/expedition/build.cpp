#include "expedition/build.hpp"
#include "sim/entity_table.hpp"
#include "sim/world.hpp"

namespace ac::expedition {
void enableBuild(BuildState& state, bool enabled) noexcept { state.enabled = enabled; }
void resetBuild(BuildState& state) noexcept { state = BuildState{}; }
bool selectCard(BuildState& state, uint8_t cardId) noexcept {
  if (!state.enabled || cardId < 1u || cardId > 6u) return false;
  const auto bit = static_cast<uint8_t>(1u << (cardId - 1u));
  if ((state.selectedMask & bit) != 0u) return false;
  state.selectedMask |= bit;
  return true;
}
namespace {
bool has(const BuildState& s, uint8_t id) noexcept {
  return s.enabled && (s.selectedMask & (1u << (id - 1u))) != 0u;
}
const TargetTrigger* trigger(const ac::sim::Entity& target, uint16_t sourceId) noexcept {
  for (const auto& slot : target.build.targets) if (slot.sourceId == sourceId) return &slot;
  return nullptr;
}
TargetTrigger* trigger(ac::sim::Entity& target, uint16_t sourceId) noexcept {
  for (auto& slot : target.build.targets) if (slot.sourceId == sourceId) return &slot;
  for (auto& slot : target.build.targets) if (slot.sourceId == 0u) { slot.sourceId = sourceId; return &slot; }
  return nullptr;
}
}
double calculateHitModifier(const ac::sim::Entity& shooter, const ac::sim::Entity& target,
                            ac::config::HitPart part, double distanceM, uint32_t nowMs) noexcept {
  if (!shooter.active || shooter.kind != ac::sim::EntityKind::kPlayer || shooter.hp <= 0.0 ||
      shooter.downed.downed || !target.active || !(distanceM >= 0.0) ||
      distanceM > ac::config::kShotMaxDistanceM || static_cast<uint8_t>(part) > 2u) return 1.0;
  double multiplier = 1.0;
  if (has(shooter.build, 1)) {
    const auto* slot = trigger(target, shooter.id);
    if (slot != nullptr && nowMs < slot->markUntilMs) multiplier *= 1.5;
    else if (part != ac::config::HitPart::kHead) multiplier *= 0.85;
  }
  if (has(shooter.build, 2) && part != ac::config::HitPart::kHead) multiplier *= 0.85;
  if (has(shooter.build, 3)) {
    if (shooter.vel.x != 0.0 || shooter.vel.y != 0.0 || shooter.vel.z != 0.0) multiplier *= 0.8;
    else if (shooter.build.stationaryMs >= 1000u && part == ac::config::HitPart::kHead) multiplier *= 1.75;
  }
  if (has(shooter.build, 4) && distanceM > 6.0) multiplier *= 0.8;
  if (has(shooter.build, 5) && distanceM > 6.0) multiplier *= 0.8;
  if (has(shooter.build, 6) && distanceM > 6.0) multiplier *= 0.8;
  return multiplier;
}
void onHit(ac::sim::Entity& shooter, ac::sim::Entity& target, ac::config::HitPart part,
           double distanceM, uint32_t nowMs) noexcept {
  if (!shooter.build.enabled || !shooter.active || shooter.kind != ac::sim::EntityKind::kPlayer ||
      shooter.hp <= 0.0 || shooter.downed.downed || !target.active || !(distanceM >= 0.0) ||
      distanceM > ac::config::kShotMaxDistanceM || static_cast<uint8_t>(part) > 2u || shooter.id == 0u) return;
  if (has(shooter.build, 2) && part == ac::config::HitPart::kHead && nowMs >= shooter.build.refundReadyAtMs &&
      shooter.weapon.activeSlot < ac::config::kWeaponSlotCount) {
    auto& mag = shooter.weapon.magInSlot[shooter.weapon.activeSlot];
    if (mag < ac::combat::activeWeaponDef(shooter.weapon).mag) ++mag;
    shooter.build.refundReadyAtMs = nowMs + 500u; ++shooter.build.triggerCount;
  }
  if (has(shooter.build, 4) && distanceM >= 0.0 && distanceM <= 6.0 &&
      target.kind == ac::sim::EntityKind::kSheep) {
    auto* slot = trigger(target, shooter.id);
    const auto stagger = static_cast<int32_t>(ac::config::SheepState::kStagger);
    if (slot != nullptr && nowMs >= slot->staggerReadyAtMs &&
        (target.state == stagger || ac::config::canSheepTransition(target.state, stagger))) {
      target.state = static_cast<uint8_t>(stagger); target.ai.timerMs = 500.0;
      target.vel = {}; target.ai.chargeDirX = 0.0; target.ai.chargeDirZ = 0.0;
      slot->staggerReadyAtMs = nowMs + 2000u; ++shooter.build.triggerCount;
    }
  }
  if (has(shooter.build, 1) && part == ac::config::HitPart::kHead) {
    auto* slot = trigger(target, shooter.id);
    if (slot != nullptr) { slot->markUntilMs = nowMs + 3000u; ++shooter.build.triggerCount; }
  }
}
void onKill(ac::sim::Entity& shooter, const ac::sim::Entity& target, double distanceM,
            uint32_t nowMs) noexcept {
  if (!shooter.build.enabled || !shooter.active || shooter.kind != ac::sim::EntityKind::kPlayer ||
      target.hp > 0.0 || target.kind != ac::sim::EntityKind::kSheep ||
      distanceM < 0.0 || !(distanceM <= 6.0) || shooter.hp <= 0.0 || shooter.downed.downed) return;
  if (has(shooter.build, 6) && nowMs >= shooter.build.tempoReadyAtMs &&
      shooter.weapon.activeSlot < ac::config::kWeaponSlotCount) {
    shooter.weapon.magInSlot[shooter.weapon.activeSlot] = ac::combat::activeWeaponDef(shooter.weapon).mag;
    ac::combat::cancelReload(shooter.weapon);
    shooter.build.tempoReadyAtMs = nowMs + 2000u; ++shooter.build.triggerCount;
  }
  if (has(shooter.build, 5) && nowMs >= shooter.build.healReadyAtMs) {
    shooter.hp += 8.0; if (shooter.hp > shooter.maxHp) shooter.hp = shooter.maxHp;
    shooter.build.healReadyAtMs = nowMs + 1000u; ++shooter.build.triggerCount;
  }
}
void updateBuild(ac::sim::Entity& entity, uint32_t nowMs, uint32_t dtMs) noexcept {
  (void)nowMs;
  if (!entity.build.enabled) return;
  if (entity.vel.x != 0.0 || entity.vel.y != 0.0 || entity.vel.z != 0.0) entity.build.stationaryMs = 0;
  else if (entity.build.stationaryMs < 1000u) {
    const uint32_t remaining = 1000u - entity.build.stationaryMs;
    entity.build.stationaryMs += dtMs < remaining ? dtMs : remaining;
  }
}
bool pulse(ac::sim::World& world, ac::sim::Entity& user, uint32_t nowMs) noexcept {
  if (!user.build.enabled || !user.active || user.kind != ac::sim::EntityKind::kPlayer ||
      user.hp <= 0.0 || user.downed.downed || user.idle || nowMs < user.build.pulseReadyAtMs) return false;
  user.build.pulseReadyAtMs = nowMs + 20000u;
  ++user.build.triggerCount;
  for (std::size_t i = 0; i < world.activeCount; ++i) {
    auto* target = ac::sim::entityById(world, world.activeIds[i]);
    if (target == nullptr || !target->active || target->kind != ac::sim::EntityKind::kPlayer ||
        target->hp <= 0.0 || target->downed.downed || target->idle) continue;
    const double dx = target->pos.x - user.pos.x, dy = target->pos.y - user.pos.y, dz = target->pos.z - user.pos.z;
    if (dx * dx + dy * dy + dz * dz > 64.0) continue;
    target->hp += 25.0; if (target->hp > target->maxHp) target->hp = target->maxHp;
  }
  return true;
}
}  // namespace ac::expedition
