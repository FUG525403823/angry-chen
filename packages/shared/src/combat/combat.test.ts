import { describe, expect, it } from 'vitest';

import { HIT_PART, RAGE, REVIVE, partForHeight, type HitPart } from '../config/combat.ts';
import { SPREAD_MAX_DEG, WEAPONS, rpmToIntervalMs, signedJitter } from '../config/weapons.ts';
import {
  createDownedState,
  REVIVE_OUTCOME,
  reviveRatio,
  reviveStep,
  reviveTo,
  type ReviveOutcome,
} from './downed.ts';
import { damageFor } from './damage.ts';
import { activateRage, addKillRage, createRageState, isRageActive, updateRage } from './rage.ts';
import {
  activeMag,
  createWeaponState,
  switchSlot,
  tryFire,
  tryStartReload,
  updateWeapon,
} from './weapon.ts';

const DISTANCES = [0, 5, 10, 20, 30, 40, 50, 70, 90, 120];
const PARTS: HitPart[] = [HIT_PART.head, HIT_PART.torso, HIT_PART.limb];

function referenceDamage(
  def: (typeof WEAPONS)['pistol'],
  part: number,
  distanceM: number,
  isRage: boolean,
): number {
  const multiplier =
    part === HIT_PART.head ? def.headshotMultiplier : part === HIT_PART.torso ? 1.0 : 0.75;
  const falloff = Math.max(0.2, 1 - def.falloffPerM * Math.max(0, distanceM - def.falloffStartM));
  return def.damage * multiplier * falloff * (isRage ? RAGE.damageMultiplier : 1);
}

describe('伤害矩阵', () => {
  it('3 武器 × 10 距离 × 3 部位与公式一致', () => {
    let assertions = 0;
    for (const name of ['pistol', 'rifle', 'shotgun'] as const) {
      const def = WEAPONS[name];
      for (const distance of DISTANCES) {
        for (const part of PARTS) {
          for (const rage of [false, true]) {
            const result = damageFor(def, part, distance, rage, 0);
            expect(result.hpDamage).toBeCloseTo(referenceDamage(def, part, distance, rage), 10);
            expect(result.armorDamage).toBe(0);
            assertions += 1;
          }
        }
      }
    }
    expect(assertions).toBe(180);
  });

  it('手算抽样值与武器表一致', () => {
    const cases: [keyof typeof WEAPONS, HitPart, number, boolean, number][] = [
      ['pistol', HIT_PART.torso, 10, false, 25],
      ['pistol', HIT_PART.head, 10, false, 50],
      ['pistol', HIT_PART.limb, 10, false, 18.75],
      ['pistol', HIT_PART.torso, 40, false, 5],
      ['pistol', HIT_PART.head, 40, false, 10],
      ['rifle', HIT_PART.torso, 40, false, 20],
      ['rifle', HIT_PART.torso, 45, false, 14],
      ['rifle', HIT_PART.head, 100, false, 8],
      ['rifle', HIT_PART.torso, 20, true, 26],
      ['shotgun', HIT_PART.torso, 0, false, 12],
      ['shotgun', HIT_PART.torso, 12, false, 12],
      ['shotgun', HIT_PART.torso, 100, false, 2.4],
      ['shotgun', HIT_PART.head, 0, false, 24],
    ];
    for (const [name, part, distance, rage, expected] of cases) {
      const result = damageFor(WEAPONS[name], part, distance, rage, 0);
      expect(result.hpDamage).toBeCloseTo(expected, 10);
      expect(result.isHeadshot).toBe(part === HIT_PART.head);
    }
  });

  it('护甲先扣 60% 且吸收量等于护甲损耗', () => {
    const withArmor = damageFor(WEAPONS.pistol, HIT_PART.torso, 10, false, 50);
    expect(withArmor.armorDamage).toBeCloseTo(15, 10);
    expect(withArmor.hpDamage).toBeCloseTo(10, 10);
    expect(withArmor.hpDamage + withArmor.armorDamage).toBeCloseTo(25, 10);
  });

  it('部位按身高等分判定', () => {
    expect(partForHeight(0, 1.7, 1.5)).toBe(HIT_PART.head);
    expect(partForHeight(0, 1.7, 1.1)).toBe(HIT_PART.torso);
    expect(partForHeight(0, 1.7, 0.5)).toBe(HIT_PART.limb);
    expect(partForHeight(2, 0.9, 2.8)).toBe(HIT_PART.head);
  });
});

describe('射速节流', () => {
  it('600 RPM 步枪 1 秒内 9-10 发', () => {
    const state = createWeaponState();
    switchSlot(state, 1, 0);
    let shots = 0;
    for (let tick = 0; tick < 20; tick += 1) {
      const nowMs = tick * 50;
      updateWeapon(state, nowMs, 50, 1);
      if (tryFire(state, nowMs, 1)) shots += 1;
    }
    expect(shots).toBeLessThanOrEqual(10);
    expect(shots).toBeGreaterThanOrEqual(9);
  });

  it('一 tick 内超发被丢弃', () => {
    const state = createWeaponState();
    switchSlot(state, 2, 0);
    expect(tryFire(state, 0, 1)).toBe(true);
    expect(tryFire(state, 0, 1)).toBe(false);
    expect(tryFire(state, 10, 1)).toBe(false);
    expect(activeMag(state)).toBe(WEAPONS.shotgun.mag - 1);
  });

  it('狂暴射速倍率缩短间隔', () => {
    const base = rpmToIntervalMs(WEAPONS.rifle.rpm, 1);
    const rage = rpmToIntervalMs(WEAPONS.rifle.rpm, RAGE.fireRateMultiplier);
    expect(rage).toBeCloseTo(base / RAGE.fireRateMultiplier, 10);
    const state = createWeaponState();
    switchSlot(state, 1, 0);
    expect(tryFire(state, 0, RAGE.fireRateMultiplier)).toBe(true);
    expect(tryFire(state, 79, RAGE.fireRateMultiplier)).toBe(false);
    expect(tryFire(state, 80, RAGE.fireRateMultiplier)).toBe(true);
  });
});

describe('弹匣与换弹', () => {
  it('换弹中被切枪打断且不补给弹药', () => {
    const state = createWeaponState();
    switchSlot(state, 1, 0);
    tryFire(state, 0, 1);
    tryFire(state, 200, 1);
    expect(activeMag(state)).toBe(28);
    expect(tryStartReload(state, 200)).toBe(true);
    expect(switchSlot(state, 0, 300)).toBe(true);
    expect(state.reloadEndsAtMs).toBe(0);
    expect(state.magInSlot[1]).toBe(28);
    expect(state.reserveAmmo).toBe(120);
  });

  it('换弹完成后从备弹补满', () => {
    const state = createWeaponState();
    switchSlot(state, 1, 0);
    tryFire(state, 0, 1);
    tryStartReload(state, 0);
    expect(updateWeapon(state, WEAPONS.rifle.reloadMs - 50, 50, 1)).toBe(false);
    expect(updateWeapon(state, WEAPONS.rifle.reloadMs, 50, 1)).toBe(true);
    expect(activeMag(state)).toBe(WEAPONS.rifle.mag);
    expect(state.reserveAmmo).toBe(119);
  });

  it('空仓与无备弹时不能换弹', () => {
    const state = createWeaponState();
    state.reserveAmmo = 0;
    expect(tryStartReload(state, 0)).toBe(false);
    for (let i = 0; i < WEAPONS.pistol.mag; i += 1) tryFire(state, i * 200, 1);
    expect(activeMag(state)).toBe(0);
    expect(tryFire(state, 10000, 1)).toBe(false);
  });

  it('连发累积散布到上限并在停火 0.35s 后衰减', () => {
    const state = createWeaponState();
    switchSlot(state, 1, 0);
    for (let i = 0; i < 8; i += 1) tryFire(state, i * 100, 1);
    expect(state.spreadDeg).toBeCloseTo(SPREAD_MAX_DEG, 10);
    updateWeapon(state, 1000, 50, 1);
    expect(state.spreadDeg).toBeCloseTo(SPREAD_MAX_DEG, 10);
    updateWeapon(state, 1700, 600, 1);
    expect(state.spreadDeg).toBeCloseTo(0, 10);
  });

  it('散布抖动可复现且落在 [-1,1)', () => {
    expect(signedJitter(7, 3)).toBe(signedJitter(7, 3));
    for (let seq = 0; seq < 50; seq += 1) {
      const value = signedJitter(seq, 0x9e3);
      expect(value).toBeGreaterThanOrEqual(-1);
      expect(value).toBeLessThan(1);
    }
  });
});

describe('怒气与狂暴', () => {
  it('击杀累积、爆头翻倍、上限 100', () => {
    const state = createRageState();
    for (let i = 0; i < 10; i += 1) addKillRage(state, false, false, i * 1000);
    expect(state.value).toBe(80);
    addKillRage(state, false, true, 10000);
    expect(state.value).toBe(96);
    addKillRage(state, true, false, 11000);
    expect(state.value).toBe(100);
    addKillRage(state, true, true, 12000);
    expect(state.value).toBe(100);
  });

  it('满值激活 8 秒后到期清零', () => {
    const state = createRageState();
    state.value = 100;
    expect(activateRage(state, 0)).toBe(true);
    expect(isRageActive(state, 0)).toBe(true);
    expect(state.value).toBe(0);
    expect(isRageActive(state, RAGE.durationMs - 1)).toBe(true);
    updateRage(state, RAGE.durationMs, 50);
    expect(isRageActive(state, RAGE.durationMs)).toBe(false);
    expect(state.endsAtMs).toBe(0);
    expect(activateRage(state, RAGE.durationMs)).toBe(false);
  });

  it('无击杀无受击 10 秒后每秒 -5，受击重置计时', () => {
    const state = createRageState();
    state.value = 50;
    updateRage(state, 9000, 1000);
    expect(state.value).toBe(50);
    updateRage(state, 11000, 1000);
    expect(state.value).toBe(45);
    state.value = 50;
    state.lastCombatAtMs = 20000;
    updateRage(state, 25000, 1000);
    expect(state.value).toBe(50);
  });

  it('狂暴期间伤害倍率生效', () => {
    const normal = damageFor(WEAPONS.rifle, HIT_PART.torso, 20, false, 0);
    const raging = damageFor(WEAPONS.rifle, HIT_PART.torso, 20, true, 0);
    expect(raging.hpDamage).toBeCloseTo(normal.hpDamage * RAGE.damageMultiplier, 10);
  });
});

describe('倒地与救援', () => {
  it('3 秒完成救援并按 50% 生命复起', () => {
    const state = createDownedState();
    state.downed = true;
    const health = { hp: 0, maxHp: 100 };
    let outcome: ReviveOutcome = REVIVE_OUTCOME.idle;
    for (let elapsed = 50; elapsed <= REVIVE.durationMs; elapsed += 50) {
      outcome = reviveStep(state, elapsed, 50, 2, true);
    }
    expect(outcome).toBe(REVIVE_OUTCOME.done);
    expect(reviveRatio(state)).toBe(1);
    reviveTo(health, state, REVIVE.revivedHpRatio);
    expect(health.hp).toBe(50);
    expect(state.downed).toBe(false);
  });

  it('松手中断后进度保留 5 秒再归零', () => {
    const state = createDownedState();
    state.downed = true;
    for (let elapsed = 50; elapsed <= 1500; elapsed += 50) reviveStep(state, elapsed, 50, 2, true);
    const held = state.reviveProgressMs;
    expect(held).toBeCloseTo(1500, 6);
    expect(reviveStep(state, 1500, 50, 2, false)).toBe(REVIVE_OUTCOME.interrupted);
    expect(state.reviveProgressMs).toBe(held);
    expect(reviveStep(state, 2000, 50, 0, false)).toBe(REVIVE_OUTCOME.idle);
    expect(state.reviveProgressMs).toBe(held);
    expect(reviveStep(state, 6500, 50, 0, false)).toBe(REVIVE_OUTCOME.interrupted);
    expect(state.reviveProgressMs).toBe(0);
  });

  it('超距即中断（换取救援者后重新计时）', () => {
    const state = createDownedState();
    state.downed = true;
    reviveStep(state, 50, 50, 2, true);
    expect(state.reviverId).toBe(2);
    expect(reviveStep(state, 100, 50, 3, false)).toBe(REVIVE_OUTCOME.interrupted);
    expect(state.reviverId).toBe(0);
    expect(state.resetAtMs).toBe(100 + REVIVE.resetDelayMs);
  });
});
