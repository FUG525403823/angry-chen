export const HIT_PART = Object.freeze({ head: 0, torso: 1, limb: 2 } as const);

export type HitPart = (typeof HIT_PART)[keyof typeof HIT_PART];

export const HIT_PART_NAMES: readonly ('head' | 'torso' | 'limb')[] = Object.freeze([
  'head',
  'torso',
  'limb',
]);

export const BODY_PART_MULTIPLIER = Object.freeze({
  head: 2.0,
  torso: 1.0,
  limb: 0.75,
} as const);

export const HEAD_MIN_HEIGHT_RATIO = 0.85;
export const TORSO_MIN_HEIGHT_RATIO = 0.4;

export const ARMOR_ABSORB_RATIO = 0.6;
export const ARMOR_MAX = 50;
export const HEALTH_MAX = 100;
export const FALLOFF_MIN_MULTIPLIER = 0.5;
export const FRIENDLY_FIRE = false;

export const RAGE = Object.freeze({
  max: 100,
  perKill: 8,
  perEliteKill: 20,
  headshotKillMultiplier: 2,
  idleDecayDelayMs: 10000,
  decayPerSecond: 5,
  durationMs: 8000,
  damageMultiplier: 1.3,
  fireRateMultiplier: 1.25,
  moveSpeedMultiplier: 1.15,
});

export const REVIVE = Object.freeze({
  rangeM: 2.0,
  durationMs: 3000,
  resetDelayMs: 5000,
  reviverMaxSpeed: 1.5,
  revivedHpRatio: 0.5,
  waveReviveHpRatio: 0.5,
  progressEventStepRatio: 0.05,
});

export const SHEEP_ELITE_STATE = 2;

export function partForHeight(baseY: number, height: number, hitY: number): HitPart {
  const ratio = height <= 0 ? 0 : (hitY - baseY) / height;
  if (ratio >= HEAD_MIN_HEIGHT_RATIO) return HIT_PART.head;
  if (ratio >= TORSO_MIN_HEIGHT_RATIO) return HIT_PART.torso;
  return HIT_PART.limb;
}

/**
 * 显式高度阈值版本（单位：米）：命中体与渲染体分离的目标（羊，见 SHEEP_HIT）用固定阈值，
 * 而不是按身高比例切分——旧的比例口径（0.85/0.4 × 0.9m）把"描头"判成躯干。
 * partForHeight 保持原样（逐位不变），玩家口径不受影响。
 */
export function partForThresholds(
  baseY: number,
  headMinM: number,
  torsoMinM: number,
  hitY: number,
): HitPart {
  const local = hitY - baseY;
  if (local >= headMinM) return HIT_PART.head;
  if (local >= torsoMinM) return HIT_PART.torso;
  return HIT_PART.limb;
}

export function partMultiplier(part: HitPart, headshotMultiplier: number): number {
  if (part === HIT_PART.head) return headshotMultiplier;
  if (part === HIT_PART.torso) return BODY_PART_MULTIPLIER.torso;
  return BODY_PART_MULTIPLIER.limb;
}
