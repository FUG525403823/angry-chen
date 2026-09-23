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
  readonly radiusM: number;
  readonly topM: number;
  readonly headMinM: number;
  readonly torsoMinM: number;
}

/**
 * 命中体口径（竖直胶囊 + 高度阈值），与客户端渲染模型 render/sheepModel.ts 对齐：
 * 躯干 1.1×0.72×0.72 @ y=0.62、头盒 0.44×0.44×0.5 @ (0, 0.90, 0.66)，整体乘 SHEEP_FORM_SCALE(1/1.06/1.12/1.6)。
 * radiusM = 体宽一半（向上取整到 cm，浮点比较必须 ≥ 渲染体半宽）、topM = 头顶（留少量余量）、
 * headMinM = 头盒下沿附近、torsoMinM = 腿/躯干分界。
 * 注意：SHEEP.radiusM/heightM 仍是移动与碰撞口径（分离、攻击距离），不要拿来做命中判定；
 * 旧实现两种口径共用 0.5/0.9，于是"描羊头打不到、描羊王只有一半体积算命中"（真人试玩反馈）。
 * 客户端 render/sheepHit.test.ts 守这条不漂移。
 */
export const SHEEP_HIT: Readonly<Record<SheepKind, SheepHitProfile>> = Object.freeze({
  grunt: { radiusM: 0.55, topM: 1.15, headMinM: 0.78, torsoMinM: 0.34 },
  ram: { radiusM: 0.59, topM: 1.22, headMinM: 0.83, torsoMinM: 0.36 },
  elite: { radiusM: 0.62, topM: 1.29, headMinM: 0.87, torsoMinM: 0.38 },
  king: { radiusM: 0.89, topM: 1.84, headMinM: 1.25, torsoMinM: 0.54 },
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
