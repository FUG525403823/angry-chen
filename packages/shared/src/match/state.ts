import { MATCH_PHASE } from './phase.ts';

export interface MatchStatePlayer {
  pid: number;
  name: string;
  ready: boolean;
  weapon: number;
  hpRatio: number;
  kills: number;
  mag: number;
  reserve: number;
  reloadLeft10Ms: number;
  rage: number;
  rageLeft100Ms: number;
  downed: boolean;
  reviveRatio255: number;
}

export interface MatchState {
  phase: number;
  wave: number;
  intermissionMs: number;
  hostId: number;
  players: MatchStatePlayer[];
}

export function createMatchStatePlayer(): MatchStatePlayer {
  return {
    pid: 0,
    name: '',
    ready: false,
    weapon: 0,
    hpRatio: 1,
    kills: 0,
    mag: 0,
    reserve: 0,
    reloadLeft10Ms: 0,
    rage: 0,
    rageLeft100Ms: 0,
    downed: false,
    reviveRatio255: 0,
  };
}

export function createMatchState(): MatchState {
  return { phase: MATCH_PHASE.lobby, wave: 0, intermissionMs: 0, hostId: 0, players: [] };
}
