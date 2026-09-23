import { type WeaponState, createWeaponState, switchSlot, tryFire, updateWeapon } from '@ac/shared';

import type { AmmoView } from '../prediction/ammoLedger.ts';

export interface LocalWeapon {
  readonly spreadDeg: number;
  setSlot(slot: 0 | 1 | 2, nowMs: number): void;
  syncMag(slot: 0 | 1 | 2, mag: number, reserve: number): void;
  /** O07：按账本裁决写入显示值（`view` 由 `createAmmoLedger().reconcile` 给出）。 */
  reconcileAmmo(slot: 0 | 1 | 2, serverMag: number, serverReserve: number, view: AmmoView): void;
  onFire(nowMs: number): boolean;
  update(nowMs: number, dtMs: number): void;
  reset(): void;
}

const authorityOnlyView: AmmoView = {
  mag: 0,
  gateMag: 0,
  reserve: 0,
  pending: 0,
  rejected: 0,
  overridden: false,
};

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
      // O07：无账本时的退化路径 —— 直接采用权威值（等价于 pending = 0 的裁决结果）。
      authorityOnlyView.mag = mag;
      authorityOnlyView.gateMag = mag;
      authorityOnlyView.reserve = reserve;
      authorityOnlyView.pending = 0;
      authorityOnlyView.rejected = 0;
      authorityOnlyView.overridden = false;
      this.reconcileAmmo(slot, mag, reserve, authorityOnlyView);
    },
    reconcileAmmo(slot: 0 | 1 | 2, serverMag: number, serverReserve: number, view: AmmoView): void {
      void serverMag;
      void serverReserve;
      state.magInSlot[slot] = view.gateMag;
      state.reserveAmmo = view.reserve;
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
