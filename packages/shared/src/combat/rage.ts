import { RAGE } from '../config/combat.ts';
import { MS_PER_SECOND } from '../sim/localStep.ts';

export interface RageState {
  value: number;
  endsAtMs: number;
  lastCombatAtMs: number;
}

export function createRageState(): RageState {
  return { value: 0, endsAtMs: 0, lastCombatAtMs: 0 };
}

export function resetRageState(state: RageState): void {
  state.value = 0;
  state.endsAtMs = 0;
  state.lastCombatAtMs = 0;
}

export function isRageActive(state: RageState, nowMs: number): boolean {
  return state.endsAtMs > nowMs;
}

export function rageRatio(state: RageState): number {
  return state.value / RAGE.max;
}

export function rageSecondsLeft(state: RageState, nowMs: number): number {
  return state.endsAtMs <= nowMs ? 0 : (state.endsAtMs - nowMs) / MS_PER_SECOND;
}

export function noteCombat(state: RageState, nowMs: number): void {
  state.lastCombatAtMs = nowMs;
}

export function addKillRage(
  state: RageState,
  isElite: boolean,
  isHeadshot: boolean,
  nowMs: number,
): number {
  const base = isElite ? RAGE.perEliteKill : RAGE.perKill;
  const amount = isHeadshot ? base * RAGE.headshotKillMultiplier : base;
  const next = state.value + amount;
  state.value = next > RAGE.max ? RAGE.max : next;
  state.lastCombatAtMs = nowMs;
  return amount;
}

export function activateRage(state: RageState, nowMs: number): boolean {
  if (state.value < RAGE.max) return false;
  if (isRageActive(state, nowMs)) return false;
  state.value = 0;
  state.endsAtMs = nowMs + RAGE.durationMs;
  state.lastCombatAtMs = nowMs;
  return true;
}

export function updateRage(state: RageState, nowMs: number, dtMs: number): boolean {
  const wasActive = state.endsAtMs !== 0 && state.endsAtMs > nowMs;
  if (state.endsAtMs !== 0 && !wasActive) state.endsAtMs = 0;
  if (wasActive) return false;
  if (state.value <= 0) return false;
  if (nowMs - state.lastCombatAtMs < RAGE.idleDecayDelayMs) return false;
  const decayed = state.value - (RAGE.decayPerSecond * dtMs) / MS_PER_SECOND;
  state.value = decayed > 0 ? decayed : 0;
  return true;
}
