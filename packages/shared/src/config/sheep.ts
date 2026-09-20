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
