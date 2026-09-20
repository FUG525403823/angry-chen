import { MATCH_PHASE_TRANSITIONS, type MatchPhase } from './phase.ts';

export interface IllegalTransition {
  readonly ok: false;
  readonly reason: 'illegal-transition';
}

export type TransitionResult = { readonly ok: true } | IllegalTransition;

export function canTransition(from: MatchPhase, to: MatchPhase): TransitionResult {
  const allowed = MATCH_PHASE_TRANSITIONS[from];
  if (allowed === undefined || allowed.indexOf(to) < 0) {
    return { ok: false, reason: 'illegal-transition' };
  }
  return { ok: true };
}
