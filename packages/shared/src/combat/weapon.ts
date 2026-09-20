import { MS_PER_SECOND } from '../sim/localStep.ts';
import {
  RESERVE_AMMO_INITIAL,
  SPREAD_DECAY_DELAY_MS,
  SPREAD_DECAY_PER_SECOND_DEG,
  SPREAD_GROWTH_PER_SHOT_DEG,
  SPREAD_MAX_DEG,
  WEAPON_SLOT_ORDER,
  WEAPONS,
  rpmToIntervalMs,
  weaponDefForSlot,
  type WeaponDef,
  type WeaponSlot,
} from '../config/weapons.ts';

export interface WeaponState {
  slots: readonly [WeaponSlot, WeaponSlot, WeaponSlot];
  activeSlot: 0 | 1 | 2;
  magInSlot: [number, number, number];
  reserveAmmo: number;
  reloadEndsAtMs: number;
  nextFireAllowedAtMs: number;
  spreadDeg: number;
}

export function createWeaponState(): WeaponState {
  return {
    slots: Object.freeze([
      WEAPON_SLOT_ORDER[0] ?? 'pistol',
      WEAPON_SLOT_ORDER[1] ?? 'rifle',
      WEAPON_SLOT_ORDER[2] ?? 'shotgun',
    ]) as readonly [WeaponSlot, WeaponSlot, WeaponSlot],
    activeSlot: 0,
    magInSlot: [WEAPONS.pistol.mag, WEAPONS.rifle.mag, WEAPONS.shotgun.mag],
    reserveAmmo: RESERVE_AMMO_INITIAL,
    reloadEndsAtMs: 0,
    nextFireAllowedAtMs: 0,
    spreadDeg: 0,
  };
}

export function resetWeaponState(state: WeaponState): void {
  state.activeSlot = 0;
  state.magInSlot[0] = WEAPONS.pistol.mag;
  state.magInSlot[1] = WEAPONS.rifle.mag;
  state.magInSlot[2] = WEAPONS.shotgun.mag;
  state.reserveAmmo = RESERVE_AMMO_INITIAL;
  state.reloadEndsAtMs = 0;
  state.nextFireAllowedAtMs = 0;
  state.spreadDeg = 0;
}

export function activeWeaponDef(state: WeaponState): WeaponDef {
  return weaponDefForSlot(state.activeSlot);
}

export function activeMag(state: WeaponState): number {
  return state.magInSlot[state.activeSlot];
}

export function isReloading(state: WeaponState): boolean {
  return state.reloadEndsAtMs !== 0;
}

export function reloadRemainingMs(state: WeaponState, nowMs: number): number {
  if (state.reloadEndsAtMs === 0) return 0;
  return Math.max(0, state.reloadEndsAtMs - nowMs);
}

export function updateWeapon(
  state: WeaponState,
  nowMs: number,
  dtMs: number,
  fireRateMultiplier = 1,
): boolean {
  let reloadFinished = false;
  if (state.reloadEndsAtMs !== 0 && nowMs >= state.reloadEndsAtMs) {
    const def = activeWeaponDef(state);
    const slot = state.activeSlot;
    const need = def.mag - state.magInSlot[slot];
    const take = Math.min(need, state.reserveAmmo);
    state.magInSlot[slot] += take;
    state.reserveAmmo -= take;
    state.reloadEndsAtMs = 0;
    reloadFinished = true;
  }

  if (state.spreadDeg > 0) {
    const interval = rpmToIntervalMs(activeWeaponDef(state).rpm, fireRateMultiplier);
    const lastShotAtMs = state.nextFireAllowedAtMs - interval;
    if (nowMs - lastShotAtMs >= SPREAD_DECAY_DELAY_MS) {
      const decayed = state.spreadDeg - (SPREAD_DECAY_PER_SECOND_DEG * dtMs) / MS_PER_SECOND;
      state.spreadDeg = decayed > 0 ? decayed : 0;
    }
  }
  return reloadFinished;
}

export function tryFire(state: WeaponState, nowMs: number, fireRateMultiplier = 1): boolean {
  if (state.reloadEndsAtMs !== 0) return false;
  if (nowMs < state.nextFireAllowedAtMs) return false;
  const slot = state.activeSlot;
  if (state.magInSlot[slot] <= 0) return false;
  state.magInSlot[slot] -= 1;
  state.nextFireAllowedAtMs =
    nowMs + rpmToIntervalMs(activeWeaponDef(state).rpm, fireRateMultiplier);
  const grown = state.spreadDeg + SPREAD_GROWTH_PER_SHOT_DEG;
  state.spreadDeg = grown < SPREAD_MAX_DEG ? grown : SPREAD_MAX_DEG;
  return true;
}

export function tryStartReload(state: WeaponState, nowMs: number): boolean {
  if (state.reloadEndsAtMs !== 0) return false;
  const def = activeWeaponDef(state);
  if (state.magInSlot[state.activeSlot] >= def.mag) return false;
  if (state.reserveAmmo <= 0) return false;
  state.reloadEndsAtMs = nowMs + def.reloadMs;
  return true;
}

export function cancelReload(state: WeaponState): void {
  state.reloadEndsAtMs = 0;
}

export function switchSlot(state: WeaponState, slot: 0 | 1 | 2, nowMs: number): boolean {
  if (slot === state.activeSlot) return false;
  state.activeSlot = slot;
  state.reloadEndsAtMs = 0;
  if (state.nextFireAllowedAtMs < nowMs) state.nextFireAllowedAtMs = nowMs;
  return true;
}
