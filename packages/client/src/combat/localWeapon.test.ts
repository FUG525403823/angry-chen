import { describe, expect, it } from 'vitest';

import { WEAPONS, rpmToIntervalMs } from '@ac/shared';

import { createLocalWeapon } from './localWeapon.ts';

/**
 * onFire 是客户端唯一的开火裁决：枪口火焰、音效、后坐、曳光都必须走这个门。
 * 真人试玩缺陷「没子弹点左键还有弹道」的根因就是曳光没走这个门（main.ts 开火路径已修）。
 */
describe('本地武器开火裁决（真人试玩：空弹匣仍有弹道）', () => {
  it('空弹匣恒不开火', () => {
    const weapon = createLocalWeapon();
    weapon.syncMag(0, 0, 0);
    expect(weapon.onFire(0)).toBe(false);
    expect(weapon.onFire(1000)).toBe(false);
    expect(weapon.onFire(60000)).toBe(false);
  });

  it('有弹时按射速节流逐发开火，打空后停止', () => {
    const weapon = createLocalWeapon();
    weapon.syncMag(0, 2, 0);
    const interval = rpmToIntervalMs(WEAPONS.pistol.rpm);
    expect(weapon.onFire(10000)).toBe(true);
    expect(weapon.onFire(10000 + interval - 1)).toBe(false);
    expect(weapon.onFire(10000 + interval)).toBe(true);
    expect(weapon.onFire(10000 + interval * 2)).toBe(false);
    expect(weapon.spreadDeg).toBeGreaterThan(0);
  });

  it('切到没有弹的槽位后立刻不开火', () => {
    const weapon = createLocalWeapon();
    weapon.syncMag(0, 5, 0);
    weapon.syncMag(1, 0, 0);
    weapon.setSlot(1, 0);
    expect(weapon.onFire(0)).toBe(false);
  });
});
