import type { WeaponDef } from './weapons.ts';

export const SHEEP_KIND = Object.freeze({
  grunt: 'grunt',
  ram: 'ram',
  elite: 'elite',
  king: 'king',
} as const);

export type SheepKind = (typeof SHEEP_KIND)[keyof typeof SHEEP_KIND];

export interface SheepDef {
  readonly hp: number;
  readonly speed: number;
  readonly damage: number;
  readonly price: number;
  readonly radiusM: number;
  readonly heightM: number;
}

export const SHEEP_ORDER: readonly SheepKind[] = Object.freeze(['grunt', 'ram', 'elite', 'king']);

export const SHEEP: Readonly<Record<SheepKind, SheepDef>> = Object.freeze({
  grunt: { hp: 60, speed: 2.6, damage: 8, price: 1, radiusM: 0.5, heightM: 0.9 },
  ram: { hp: 140, speed: 3.2, damage: 22, price: 3, radiusM: 0.55, heightM: 1.0 },
  elite: { hp: 260, speed: 2.4, damage: 14, price: 6, radiusM: 0.6, heightM: 1.1 },
  king: { hp: 2400, speed: 2.0, damage: 30, price: 20, radiusM: 1.6, heightM: 2.4 },
});

export interface SheepHitProfile {
  /** 躯干盒（羊局部坐标，米）：x ±halfWidthM、z ±halfDepthM、y ∈ [0, topM]。 */
  readonly halfWidthM: number;
  readonly halfDepthM: number;
  readonly topM: number;
  /** 头盒（局部坐标）：x ±headHalfWidthM、y ∈ [headMinYM, headMaxYM]、z ∈ [headMinZM, headMaxZM]。 */
  readonly headHalfWidthM: number;
  readonly headMinYM: number;
  readonly headMaxYM: number;
  readonly headMinZM: number;
  readonly headMaxZM: number;
  /** 躯干盒内部的高度阈值：≥ headMinM 判头、≥ torsoMinM 判躯干、否则四肢。 */
  readonly headMinM: number;
  readonly torsoMinM: number;
}

/**
 * 命中体口径 = 客户端渲染盒体本身（躯干盒 + 前伸头盒），在羊的局部坐标系（+Z = 正前方）里求交。
 * 躯干盒 x/ z 取羊毛团的包围（0.62 + 0.21×1.14 ≈ 0.86 半宽、0.42 + 0.24 ≈ 0.66 半深，见 render/sheepModel.ts
 * 的 WOOL_SPREAD_*）；头盒是躯干前方那个独立的盒子（0.44×0.44×0.5 @ (0, 0.90, 0.66)），整体乘
 * SHEEP_FORM_SCALE(1/1.06/1.12/1.6) 并向上取整到 cm。
 * 旧口径是竖直胶囊（半径 0.55、轴 y∈[0.55,0.60]）：头顶高度上的有效半宽只剩 0.18–0.46 m，且胶囊完全盖不住
 * 前伸 0.66 m 的头盒 —— 真人试玩"受击体积太小、描头打不到"的根因（二轮只把胶囊口径换成按羊种的胶囊，仍不够）。
 * 注意：SHEEP.radiusM/heightM 仍是移动与碰撞口径（分离、攻击距离），不要拿来做命中判定。
 * 客户端 src/test/sheepHit.test.ts 守这条不漂移。
 */
export const SHEEP_HIT: Readonly<Record<SheepKind, SheepHitProfile>> = Object.freeze({
  grunt: {
    halfWidthM: 0.86,
    halfDepthM: 0.66,
    topM: 1.15,
    headHalfWidthM: 0.22,
    headMinYM: 0.68,
    headMaxYM: 1.12,
    headMinZM: 0.41,
    headMaxZM: 0.91,
    headMinM: 0.78,
    torsoMinM: 0.34,
  },
  ram: {
    halfWidthM: 0.92,
    halfDepthM: 0.7,
    topM: 1.22,
    headHalfWidthM: 0.24,
    headMinYM: 0.72,
    headMaxYM: 1.19,
    headMinZM: 0.43,
    headMaxZM: 0.97,
    headMinM: 0.83,
    torsoMinM: 0.36,
  },
  elite: {
    halfWidthM: 0.97,
    halfDepthM: 0.74,
    topM: 1.29,
    headHalfWidthM: 0.25,
    headMinYM: 0.76,
    headMaxYM: 1.26,
    headMinZM: 0.45,
    headMaxZM: 1.02,
    headMinM: 0.87,
    torsoMinM: 0.38,
  },
  king: {
    halfWidthM: 1.38,
    halfDepthM: 1.06,
    topM: 2.05,
    headHalfWidthM: 0.36,
    headMinYM: 1.08,
    headMaxYM: 2.04,
    headMinZM: 0.65,
    headMaxZM: 1.46,
    headMinM: 1.25,
    torsoMinM: 0.54,
  },
});

export const SHEEP_AI = Object.freeze({
  sightM: 35,
  attackRangeM: 1.4,
  attackCooldownMs: 1200,
  chargeWindupMs: 1000,
  chargeSpeedMps: 9,
  eliteBoltRangeM: 25,
  eliteBoltCooldownMs: 2500,
  eliteKeepMinM: 15,
  eliteKeepMaxM: 25,
  boltSpeedMps: 14,
  kingSummonCount: 4,
  kingSummonIntervalMs: 8000,
  kingPhase3SpeedMultiplier: 1.4,
  kingPhase3CooldownMultiplier: 0.7,
  staggerMs: 250,
  chargeStaggerMs: 1000,
  deadFadeMs: 1500,
  knockbackVelocityMps: 7,
  neighborRadiusM: 3,
  maxNeighbors: 12,
  aggroSlots: 8,
  aggroDecayPerTick: 0.02,
  aggroPerHit: 20,
  targetSwitchRatio: 1.5,
  grazeRadiusM: 6,
});

export const SHEEP_STATE = Object.freeze({
  graze: 0,
  alert: 1,
  chase: 2,
  windup: 3,
  charge: 4,
  attack: 5,
  ranged: 6,
  stagger: 7,
  dead: 8,
  kingPhase1: 9,
  kingPhase2: 10,
  kingPhase3: 11,
  kingSummoning: 12,
} as const);

export type SheepStateCode = (typeof SHEEP_STATE)[keyof typeof SHEEP_STATE];

export const SHEEP_STATE_NAMES: readonly string[] = Object.freeze([
  'graze',
  'alert',
  'chase',
  'windup',
  'charge',
  'attack',
  'ranged',
  'stagger',
  'dead',
  'kingPhase1',
  'kingPhase2',
  'kingPhase3',
  'kingSummoning',
]);

export const SHEEP_STATE_TRANSITIONS: Readonly<Record<SheepStateCode, readonly SheepStateCode[]>> =
  Object.freeze({
    0: [1, 2, 7, 8],
    1: [2, 0, 6, 7, 8],
    2: [3, 5, 6, 7, 8, 9],
    3: [4, 2, 7, 8],
    4: [2, 7, 8],
    5: [2, 7, 8],
    6: [2, 1, 7, 8],
    7: [2, 8, 9],
    8: [],
    9: [4, 10, 7, 8],
    10: [4, 12, 11, 7, 8],
    11: [4, 12, 7, 8],
    12: [2, 5, 7, 8],
  });

export function sheepDefFor(kind: SheepKind): SheepDef {
  return SHEEP[kind];
}

export const SHEEP_ATTACK_PROFILE: Readonly<Record<SheepKind, WeaponDef>> = Object.freeze({
  grunt: Object.freeze({
    damage: SHEEP.grunt.damage,
    pellets: 1,
    rpm: 0,
    auto: false,
    mag: 1,
    reloadMs: 0,
    spreadDeg: 0,
    falloffStartM: 1e9,
    falloffPerM: 0,
    headshotMultiplier: 1,
  }),
  ram: Object.freeze({
    damage: SHEEP.ram.damage,
    pellets: 1,
    rpm: 0,
    auto: false,
    mag: 1,
    reloadMs: 0,
    spreadDeg: 0,
    falloffStartM: 1e9,
    falloffPerM: 0,
    headshotMultiplier: 1,
  }),
  elite: Object.freeze({
    damage: SHEEP.elite.damage,
    pellets: 1,
    rpm: 0,
    auto: false,
    mag: 1,
    reloadMs: 0,
    spreadDeg: 0,
    falloffStartM: 1e9,
    falloffPerM: 0,
    headshotMultiplier: 1,
  }),
  king: Object.freeze({
    damage: SHEEP.king.damage,
    pellets: 1,
    rpm: 0,
    auto: false,
    mag: 1,
    reloadMs: 0,
    spreadDeg: 0,
    falloffStartM: 1e9,
    falloffPerM: 0,
    headshotMultiplier: 1,
  }),
});
