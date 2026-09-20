import { SHEEP, SHEEP_ORDER, type SheepKind } from './sheep.ts';

export const WAVE_MAX = 10;
export const WAVE_INTERMISSION_MS = 20000;
export const WAVE_INTERMISSION_MIN_MS = 5000;
export const BUDGET_SCALE_PER_EXTRA_PLAYER = 0.35;
export const SPEED_SCALE_PER_EXTRA_PLAYER = 0.02;
export const MAX_SPAWNS_PER_TICK = 8;
export const MAX_ACTIVE_SPAWN_POINTS = 3;
export const MIN_SPAWN_DISTANCE_M = 15;

export function waveBaseBudget(wave: number): number {
  const w = wave < 0 ? 0 : wave;
  return Math.round(6 + 3.2 * w + 0.18 * w * w);
}

export function waveBudget(wave: number, playerCount: number): number {
  const players = playerCount < 1 ? 1 : playerCount;
  const scale = 1 + BUDGET_SCALE_PER_EXTRA_PLAYER * (players - 1);
  return Math.round(waveBaseBudget(wave) * scale);
}

export function sheepSpeedMultiplier(playerCount: number): number {
  const players = playerCount < 1 ? 1 : playerCount;
  return 1 + SPEED_SCALE_PER_EXTRA_PLAYER * (players - 1);
}

export function sheepPrice(kind: SheepKind): number {
  return SHEEP[kind].price;
}

export function firstWaveFor(kind: SheepKind): number {
  if (kind === 'ram') return 3;
  if (kind === 'elite') return 5;
  if (kind === 'king') return 5;
  return 1;
}

export function isBossWave(wave: number): boolean {
  return wave > 0 && wave % 5 === 0;
}

export function kindAllowedAt(kind: SheepKind, wave: number): boolean {
  if (kind === 'king') return isBossWave(wave);
  return wave >= firstWaveFor(kind);
}

export function sheepKindIndex(kind: SheepKind): number {
  const index = SHEEP_ORDER.indexOf(kind);
  return index < 0 ? 0 : index;
}
