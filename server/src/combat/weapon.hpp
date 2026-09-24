#pragma once
// S08 §5.1/§5.2：WeaponState 与武器状态机（换弹计时、射速节流、散布累积与衰减、切枪）。
// 逐位对齐 v1 combat/weapon.ts：所有 ms 与度都用 double，算术顺序不得重排。
#include <cstdint>

#include "config/weapons.hpp"

namespace ac::combat {

struct WeaponState {
  int32_t magInSlot[ac::config::kWeaponSlotCount] = {0, 0, 0};
  int32_t reserveAmmo = 0;
  uint8_t activeSlot = 0u;
  double reloadEndsAtMs = 0.0;
  double nextFireAllowedAtMs = 0.0;
  double spreadDeg = 0.0;
};

void createWeaponState(WeaponState& state) noexcept;
void resetWeaponState(WeaponState& state) noexcept;
const ac::config::WeaponDef& activeWeaponDef(const WeaponState& state) noexcept;
int32_t activeMag(const WeaponState& state) noexcept;
bool isReloading(const WeaponState& state) noexcept;
double reloadRemainingMs(const WeaponState& state, double nowMs) noexcept;
bool updateWeapon(WeaponState& state, double nowMs, double dtMs, double fireRateMultiplier = 1.0) noexcept;
bool tryFire(WeaponState& state, double nowMs, double fireRateMultiplier = 1.0) noexcept;
bool tryStartReload(WeaponState& state, double nowMs) noexcept;
void cancelReload(WeaponState& state) noexcept;
bool switchSlot(WeaponState& state, uint8_t slot, double nowMs) noexcept;

}  // namespace ac::combat
