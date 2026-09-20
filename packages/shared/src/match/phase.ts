export const MATCH_PHASE = Object.freeze({
  lobby: 0,
  loading: 1,
  playing: 2,
  intermission: 3,
  ended: 4,
} as const);

export type MatchPhase = (typeof MATCH_PHASE)[keyof typeof MATCH_PHASE];

export const MATCH_PHASE_NAMES: readonly string[] = Object.freeze([
  'lobby',
  'loading',
  'playing',
  'intermission',
  'ended',
]);

export const MATCH_PHASE_TRANSITIONS: Readonly<Record<MatchPhase, readonly MatchPhase[]>> =
  Object.freeze({
    [MATCH_PHASE.lobby]: Object.freeze([MATCH_PHASE.loading]),
    [MATCH_PHASE.loading]: Object.freeze([MATCH_PHASE.playing]),
    [MATCH_PHASE.playing]: Object.freeze([MATCH_PHASE.intermission, MATCH_PHASE.ended]),
    [MATCH_PHASE.intermission]: Object.freeze([MATCH_PHASE.playing, MATCH_PHASE.ended]),
    [MATCH_PHASE.ended]: Object.freeze([MATCH_PHASE.lobby]),
  });
