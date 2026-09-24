#include "combat/weapon.hpp"

namespace ac::combat {

void createWeaponState(WeaponState& state) noexcept {
  resetWeaponState(state);
}

void resetWeaponState(WeaponState& state) noexcept {
  state.activeSlot = 0u;
  for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) {
    state.magInSlot[slot] = ac::config::kWeapons[slot].mag;
  }
  state.reserveAmmo = ac::config::kReserveAmmoInitial;
  state.reloadEndsAtMs = 0.0;
  state.nextFireAllowedAtMs = 0.0;
  state.spreadDeg = 0.0;
}

const ac::config::WeaponDef& activeWeaponDef(const WeaponState& state) noexcept {
  const int32_t slot = state.activeSlot < ac::config::kWeaponSlotCount ? static_cast<int32_t>(state.activeSlot) : 0;
  return ac::config::kWeapons[slot];
}

int32_t activeMag(const WeaponState& state) noexcept {
  const uint8_t slot = state.activeSlot < ac::config::kWeaponSlotCount ? state.activeSlot : 0u;
  return state.magInSlot[slot];
}

bool isReloading(const WeaponState& state) noexcept {
  return state.reloadEndsAtMs != 0.0;
}

double reloadRemainingMs(const WeaponState& state, double nowMs) noexcept {
  if (state.reloadEndsAtMs == 0.0) return 0.0;
  const double remaining = state.reloadEndsAtMs - nowMs;
  return remaining > 0.0 ? remaining : 0.0;
}

bool updateWeapon(WeaponState& state, double nowMs, double dtMs, double fireRateMultiplier) noexcept {
  bool reloadFinished = false;
  if (state.reloadEndsAtMs != 0.0 && nowMs >= state.reloadEndsAtMs) {
    const ac::config::WeaponDef& def = activeWeaponDef(state);
    const int32_t slot = static_cast<int32_t>(state.activeSlot);
    const int32_t need = def.mag - state.magInSlot[slot];
    const int32_t take = need < state.reserveAmmo ? need : state.reserveAmmo;
    state.magInSlot[slot] += take;
    state.reserveAmmo -= take;
    state.reloadEndsAtMs = 0.0;
    reloadFinished = true;
  }

  if (state.spreadDeg > 0.0) {
    const double interval = ac::config::rpmToIntervalMs(activeWeaponDef(state).rpm, fireRateMultiplier);
    const double lastShotAtMs = state.nextFireAllowedAtMs - interval;
    if (nowMs - lastShotAtMs >= static_cast<double>(ac::config::kSpreadDecayDelayMs)) {
      const double decayed = state.spreadDeg - (ac::config::kSpreadDecayPerSecondDeg * dtMs) / 1000.0;
      state.spreadDeg = decayed > 0.0 ? decayed : 0.0;
    }
  }
  return reloadFinished;
}

bool tryFire(WeaponState& state, double nowMs, double fireRateMultiplier) noexcept {
  if (state.reloadEndsAtMs != 0.0) return false;
  if (nowMs < state.nextFireAllowedAtMs) return false;
  const int32_t slot = static_cast<int32_t>(state.activeSlot);
  if (state.magInSlot[slot] <= 0) return false;
  state.magInSlot[slot] -= 1;
  state.nextFireAllowedAtMs = nowMs + ac::config::rpmToIntervalMs(activeWeaponDef(state).rpm, fireRateMultiplier);
  const double grown = state.spreadDeg + ac::config::kSpreadGrowthPerShotDeg;
  state.spreadDeg = grown < ac::config::kSpreadMaxDeg ? grown : ac::config::kSpreadMaxDeg;
  return true;
}

bool tryStartReload(WeaponState& state, double nowMs) noexcept {
  if (state.reloadEndsAtMs != 0.0) return false;
  const ac::config::WeaponDef& def = activeWeaponDef(state);
  if (state.magInSlot[state.activeSlot] >= def.mag) return false;
  if (state.reserveAmmo <= 0) return false;
  state.reloadEndsAtMs = nowMs + def.reloadMs;
  return true;
}

void cancelReload(WeaponState& state) noexcept {
  state.reloadEndsAtMs = 0.0;
}

bool switchSlot(WeaponState& state, uint8_t slot, double nowMs) noexcept {
  if (slot >= ac::config::kWeaponSlotCount) return false;
  if (slot == state.activeSlot) return false;
  state.activeSlot = slot;
  state.reloadEndsAtMs = 0.0;
  if (state.nextFireAllowedAtMs < nowMs) state.nextFireAllowedAtMs = nowMs;
  return true;
}

}  // namespace ac::combat
