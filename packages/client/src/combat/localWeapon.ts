import { type WeaponState, createWeaponState, switchSlot, tryFire, updateWeapon } from '@ac/shared';

export interface LocalWeapon {
  readonly spreadDeg: number;
  setSlot(slot: 0 | 1 | 2, nowMs: number): void;
  syncMag(slot: 0 | 1 | 2, mag: number, reserve: number): void;
  onFire(nowMs: number): boolean;
  update(nowMs: number, dtMs: number): void;
  reset(): void;
}

export function createLocalWeapon(): LocalWeapon {
  const state: WeaponState = createWeaponState();
  return {
    get spreadDeg(): number {
      return state.spreadDeg;
    },
    setSlot(slot: 0 | 1 | 2, nowMs: number): void {
      switchSlot(state, slot, nowMs);
    },
    syncMag(slot: 0 | 1 | 2, mag: number, reserve: number): void {
      state.magInSlot[slot] = mag;
      state.reserveAmmo = reserve;
    },
    onFire(nowMs: number): boolean {
      return tryFire(state, nowMs);
    },
    update(nowMs: number, dtMs: number): void {
      updateWeapon(state, nowMs, dtMs);
    },
    reset(): void {
      state.activeSlot = 0;
      state.spreadDeg = 0;
      state.nextFireAllowedAtMs = 0;
      state.reloadEndsAtMs = 0;
    },
  };
}
