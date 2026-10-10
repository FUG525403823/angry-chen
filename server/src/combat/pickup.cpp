// S16 §5：补给箱补弹。只依赖实体位置 + interactHeld，无 RNG，天然确定。
#include "combat/pickup.hpp"

#include "config/pickup.hpp"
#include "config/upgrades.hpp"
#include "config/weapons.hpp"
#include "sim/entity_table.hpp"
#include "sim/world.hpp"

namespace ac::sim {
namespace {

bool isAmmoCrate(const Entity& entity) noexcept {
  return entity.active && entity.kind == EntityKind::kPickup;
}

}  // namespace

void resolveAmmoCrates(World& world, uint32_t dtMs) noexcept {
  // 快路径：世界无 pickup 实体（fixture 世界恒如此）→ 零开销、零 RNG。
  bool hasPickup = false;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (entity.active && entity.kind == EntityKind::kPickup) {
      hasPickup = true;
      break;
    }
  }
  if (!hasPickup) return;

  const double dt = static_cast<double>(dtMs);
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* player = entityById(world, world.activeIds[i]);
    if (player == nullptr || !player->active || player->kind != EntityKind::kPlayer) continue;
    if (!player->interactHeld) {
      player->upgrade.ammoRefillMs = 0.0;
      continue;
    }

    // 范围内是否有补给箱（中心距 <= kAmmoCrateRangeM）。
    bool inRange = false;
    for (std::size_t j = 0u; j < world.activeCount; ++j) {
      const Entity* crate = entityById(world, world.activeIds[j]);
      if (crate == nullptr || !isAmmoCrate(*crate)) continue;
      const double dx = crate->pos.x - player->pos.x;
      const double dz = crate->pos.z - player->pos.z;
      if (dx * dx + dz * dz <= ac::config::kAmmoCrateRangeM * ac::config::kAmmoCrateRangeM) {
        inRange = true;
        break;
      }
    }
    if (!inRange) {
      player->upgrade.ammoRefillMs = 0.0;
      continue;
    }

    // 弹药已满（三弹匣满且备弹到上限）→ 不蓄力。
    const int32_t reserveCap =
        ac::config::kReserveAmmoInitial + ac::config::upgradeReserveBonus(player->upgrade.reserveLevel);
    bool needsRefill = player->weapon.reserveAmmo < reserveCap;
    if (!needsRefill) {
      for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) {
        if (player->weapon.magInSlot[slot] < ac::config::kWeapons[slot].mag) {
          needsRefill = true;
          break;
        }
      }
    }
    if (!needsRefill) {
      player->upgrade.ammoRefillMs = 0.0;
      continue;
    }

    player->upgrade.ammoRefillMs += dt;
    if (player->upgrade.ammoRefillMs < ac::config::kAmmoCrateRefillMs) continue;
    player->upgrade.ammoRefillMs = 0.0;

    // 蓄满：补满三弹匣 + 备弹到上限，取消进行中的换弹。
    for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) {
      player->weapon.magInSlot[slot] = ac::config::kWeapons[slot].mag;
    }
    player->weapon.reserveAmmo = reserveCap;
    player->weapon.reloadEndsAtMs = 0.0;
  }
}

}  // namespace ac::sim
