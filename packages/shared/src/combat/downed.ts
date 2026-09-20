import { REVIVE } from '../config/combat.ts';
import type { EntityId } from '../world.ts';

export interface DownedState {
  downed: boolean;
  reviveProgressMs: number;
  reviverId: EntityId;
  resetAtMs: number;
  lastEventRatio: number;
}

export const REVIVE_OUTCOME = Object.freeze({
  idle: 0,
  progress: 1,
  done: 2,
  interrupted: 3,
} as const);

export type ReviveOutcome = (typeof REVIVE_OUTCOME)[keyof typeof REVIVE_OUTCOME];

export function createDownedState(): DownedState {
  return { downed: false, reviveProgressMs: 0, reviverId: 0, resetAtMs: 0, lastEventRatio: 0 };
}

export function resetDownedState(state: DownedState): void {
  state.downed = false;
  state.reviveProgressMs = 0;
  state.reviverId = 0;
  state.resetAtMs = 0;
  state.lastEventRatio = 0;
}

export function markDowned(state: DownedState, nowMs: number): void {
  state.downed = true;
  state.reviveProgressMs = 0;
  state.reviverId = 0;
  state.resetAtMs = 0;
  state.lastEventRatio = 0;
  void nowMs;
}

export function reviveRatio(state: DownedState): number {
  const ratio = state.reviveProgressMs / REVIVE.durationMs;
  return ratio > 1 ? 1 : ratio;
}

export function canBeRevived(state: DownedState): boolean {
  return state.downed;
}

export function reviveStep(
  state: DownedState,
  nowMs: number,
  dtMs: number,
  reviverId: EntityId,
  reviverAllowed: boolean,
): ReviveOutcome {
  if (!state.downed) return REVIVE_OUTCOME.idle;

  if (reviverAllowed && reviverId !== 0) {
    state.reviverId = reviverId;
    state.resetAtMs = 0;
    state.reviveProgressMs += dtMs;
    if (state.reviveProgressMs >= REVIVE.durationMs) {
      state.reviveProgressMs = REVIVE.durationMs;
      return REVIVE_OUTCOME.done;
    }
    return REVIVE_OUTCOME.progress;
  }

  if (state.reviverId !== 0) {
    state.reviverId = 0;
    state.resetAtMs = nowMs + REVIVE.resetDelayMs;
    return REVIVE_OUTCOME.interrupted;
  }

  if (state.resetAtMs !== 0 && nowMs >= state.resetAtMs) {
    state.reviveProgressMs = 0;
    state.lastEventRatio = 0;
    state.resetAtMs = 0;
    return REVIVE_OUTCOME.interrupted;
  }
  return REVIVE_OUTCOME.idle;
}

export function reviveTo(
  health: { hp: number; maxHp: number },
  state: DownedState,
  ratio: number,
): void {
  state.downed = false;
  state.reviveProgressMs = 0;
  state.reviverId = 0;
  state.resetAtMs = 0;
  state.lastEventRatio = 0;
  health.hp = health.maxHp * ratio;
}
