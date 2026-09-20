import { SHEEP_AI, SHEEP_STATE, type SheepStateCode } from '../config/sheep.ts';
import { rngRange } from '../rng.ts';
import { spawnEntity, type Entity, type World } from '../world.ts';
import { applySheepKind, setSheepState } from './sheepBrain.ts';

export const KING_PHASE_1_MIN_RATIO = 0.66;
export const KING_PHASE_2_MIN_RATIO = 0.33;
export const KING_PHASE_2_HEALTH_RATIO = KING_PHASE_1_MIN_RATIO;
export const KING_PHASE_3_HEALTH_RATIO = KING_PHASE_2_MIN_RATIO;

export function kingPhaseFor(hpRatio: number): number {
  if (hpRatio > KING_PHASE_1_MIN_RATIO) return 1;
  if (hpRatio > KING_PHASE_2_MIN_RATIO) return 2;
  return 3;
}

export function kingStateFor(phase: number): SheepStateCode {
  return phase === 1
    ? SHEEP_STATE.kingPhase1
    : phase === 2
      ? SHEEP_STATE.kingPhase2
      : SHEEP_STATE.kingPhase3;
}

export function kingSpeedMultiplier(phase: number): number {
  return phase >= 3 ? SHEEP_AI.kingPhase3SpeedMultiplier : 1;
}

export function kingAttackCooldownMs(phase: number): number {
  return phase >= 3
    ? SHEEP_AI.attackCooldownMs * SHEEP_AI.kingPhase3CooldownMultiplier
    : SHEEP_AI.attackCooldownMs;
}

export function summonGrunts(world: World, king: Entity): number {
  let spawned = 0;
  for (let i = 0; i < SHEEP_AI.kingSummonCount; i += 1) {
    const angle = (Math.PI * 2 * i) / SHEEP_AI.kingSummonCount;
    const radius = 2.6;
    const result = spawnEntity(
      world,
      'sheep',
      king.pos.x + Math.cos(angle) * radius + rngRange(world.rng.spawn, -0.3, 0.3),
      king.pos.y,
      king.pos.z + Math.sin(angle) * radius + rngRange(world.rng.spawn, -0.3, 0.3),
    );
    if (!result.ok) continue;
    const grunt = world.entities[result.id - 1];
    if (grunt === undefined) continue;
    applySheepKind(grunt, 'grunt');
    spawned += 1;
  }
  king.ai.summoned += spawned;
  return spawned;
}

export function updateKing(world: World, king: Entity, dtMs: number): number {
  const ratio = king.maxHp > 0 ? king.hp / king.maxHp : 0;
  const phase = kingPhaseFor(ratio);
  let spawned = 0;
  if (phase !== king.ai.phase) {
    king.ai.phase = phase;
    setSheepState(king, kingStateFor(phase));
    king.ai.summonMs = phase >= 2 ? SHEEP_AI.kingSummonIntervalMs : 0;
  }
  if (phase >= 2) {
    king.ai.summonMs -= dtMs;
    if (king.ai.summonMs <= 0) {
      king.ai.summonMs = SHEEP_AI.kingSummonIntervalMs;
      spawned = summonGrunts(world, king);
    }
  }
  return spawned;
}
