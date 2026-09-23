import { MS_PER_SECOND } from '../sim/localStep.ts';

export type WeaponSlot = 'pistol' | 'rifle' | 'shotgun';

export interface WeaponDef {
  readonly damage: number;
  readonly pellets: number;
  readonly rpm: number;
  readonly auto: boolean;
  readonly mag: number;
  readonly reloadMs: number;
  readonly spreadDeg: number;
  readonly falloffStartM: number;
  readonly falloffPerM: number;
  readonly headshotMultiplier: number;
}

export const WEAPON_SLOT_ORDER: readonly WeaponSlot[] = Object.freeze([
  'pistol',
  'rifle',
  'shotgun',
]);

export const WEAPON_SLOT_COUNT = WEAPON_SLOT_ORDER.length;

export const WEAPONS: Readonly<Record<WeaponSlot, WeaponDef>> = Object.freeze({
  pistol: {
    damage: 25,
    pellets: 1,
    rpm: 300,
    auto: false,
    mag: 12,
    reloadMs: 1400,
    spreadDeg: 0.8,
    falloffStartM: 30,
    // 0 = 不随距离衰减：三轮试玩追加反馈「射击有效距离还是太近」，三把武器整表关闭距离衰减。
    // 想重新打开只改这个数（公式与下限 FALLOFF_MIN_MULTIPLIER 都还在，见 docs/plans/P06-战斗系统与武器.md §5.4）。
    falloffPerM: 0,
    headshotMultiplier: 2.0,
  },
  rifle: {
    damage: 20,
    pellets: 1,
    rpm: 600,
    auto: true,
    mag: 30,
    reloadMs: 2000,
    // 三轮追加反馈「远处打不到」：步枪是远距离主力，基础散布从 1.4° 收到 0.6°
    // （1.4° 在 45m 处就是 ±1.1m，已经比羊身还宽）。
    spreadDeg: 0.6,
    falloffStartM: 40,
    falloffPerM: 0, // 无距离衰减
    headshotMultiplier: 2.0,
  },
  shotgun: {
    damage: 12,
    pellets: 8,
    rpm: 70,
    auto: false,
    mag: 6,
    reloadMs: 2600,
    spreadDeg: 4.0,
    falloffStartM: 12,
    falloffPerM: 0, // 无距离衰减
    headshotMultiplier: 2.0,
  },
});

export const RESERVE_AMMO_INITIAL = 120;
// 连射散布累积：三轮追加反馈「射击有效距离还是太近 / 打远处的羊怎么还打不到」。
// 原值 +0.6°/发、上限 3.0° 在 45m 处是 ±2.4m 的圆锥，按住不放时命中率只剩 ~20%（见 combat/longRange.test.ts），
// 玩家感知就是"射程不够"。现在上限 0.25°，45m 处最多 ±0.2m，连射不再把弹着点推出目标。
export const SPREAD_GROWTH_PER_SHOT_DEG = 0.1;
export const SPREAD_MAX_DEG = 0.25;
export const SPREAD_DECAY_DELAY_MS = 350;
export const SPREAD_DECAY_PER_SECOND_DEG = 6.0;
export const RECOIL_PITCH_PER_SHOT_DEG = 0.35;
export const RECOIL_YAW_JITTER_DEG = 0.2;

export function weaponDefForSlot(slot: 0 | 1 | 2): WeaponDef {
  const name = WEAPON_SLOT_ORDER[slot] ?? 'pistol';
  return WEAPONS[name];
}

export function rpmToIntervalMs(rpm: number, fireRateMultiplier = 1): number {
  const effective = rpm * fireRateMultiplier;
  if (!Number.isFinite(effective) || effective <= 0) return Number.POSITIVE_INFINITY;
  return (MS_PER_SECOND * 60) / effective;
}

export function unitJitter(seq: number, salt: number): number {
  let x = (Math.imul(seq | 0, 0x9e3779b1) + Math.imul(salt | 0, 0x85ebca77)) >>> 0;
  x = Math.imul(x ^ (x >>> 15), 0x2545f491) >>> 0;
  x = Math.imul(x ^ (x >>> 13), 0x9e3779b1) >>> 0;
  x = (x ^ (x >>> 16)) >>> 0;
  return x / 4294967296;
}

export function signedJitter(seq: number, salt: number): number {
  return unitJitter(seq, salt) * 2 - 1;
}
