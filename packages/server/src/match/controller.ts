import {
  ARMOR_MAX,
  HIT_FLAG,
  LIMITS,
  MATCH_PHASE,
  RESERVE_AMMO_INITIAL,
  WAVE_MAX,
  WEAPONS,
  accumulateAlive,
  accumulateDowned,
  canTransition,
  createDirectorState,
  createPlayerMatchStats,
  createSimEvent,
  despawnEntity,
  noteDown,
  noteHit,
  noteKill,
  noteRevive,
  noteShot,
  resetDownedState,
  resetRageState,
  reviveDownedForWaveClear,
  type MatchPhase,
  type PlayerMatchStats,
} from '@ac/shared';

import { LOG_EVENTS, emit, type LogSink } from '../log.ts';
import type { Metrics } from '../metrics.ts';
import {
  buildMatchDiagnostics,
  createMatchCounters,
  resetMatchCounters,
  writeMatchReport,
  type MatchCounters,
} from '../report.ts';
import type { Room } from '../room.ts';
import type { Session } from '../session.ts';
import type { MatchResultRecord, MatchStore } from './store.ts';

export const LOADING_MS = 1500;
export const INTERMISSION_MS = 20000;
export const INTERMISSION_SKIP_MIN_MS = 5000;
export const GRACE_PERIOD_MS = 30000;
export const ALL_DISCONNECTED_RECLAIM_MS = 60000;
export const RECONNECT_MIN_HP_RATIO = 0.5;

export interface MatchDeps {
  readonly metrics: Metrics;
  readonly store: MatchStore;
  readonly dataDir?: string;
  now(): number;
  log: LogSink;
}

export interface MatchPlayerRecord {
  readonly pid: number;
  name: string;
  readonly stats: PlayerMatchStats;
  leftMidMatch: boolean;
}

export interface MatchRuntime {
  hostId: number;
  winnerTeam: 0 | 1;
  startedAtMs: number;
  endedAtMs: number;
  loadingMs: number;
  records: Map<number, MatchPlayerRecord>;
  readonly counters: MatchCounters;
  readonly shotMagBefore: Int32Array;
  readonly shotSlotBefore: Int32Array;
}

export function createMatchRuntime(): MatchRuntime {
  return {
    hostId: 0,
    winnerTeam: 0,
    startedAtMs: 0,
    endedAtMs: 0,
    loadingMs: 0,
    records: new Map<number, MatchPlayerRecord>(),
    counters: createMatchCounters(),
    shotMagBefore: new Int32Array(LIMITS.maxPlayersPerRoom),
    shotSlotBefore: new Int32Array(LIMITS.maxPlayersPerRoom),
  };
}

export function joinMatchRecord(room: Room, pid: number, name: string): MatchPlayerRecord {
  const existing = room.match.records.get(pid);
  if (existing !== undefined) {
    existing.name = name;
    return existing;
  }
  const record: MatchPlayerRecord = {
    pid,
    name,
    stats: createPlayerMatchStats(),
    leftMidMatch: false,
  };
  room.match.records.set(pid, record);
  return record;
}

export function applyMatchTransition(room: Room, deps: MatchDeps, next: MatchPhase): boolean {
  if (room.phase === next) return true;
  const result = canTransition(room.phase as MatchPhase, next);
  if (!result.ok) {
    emit(deps.log, 'warn', LOG_EVENTS.matchTransitionDenied, {
      room: room.code,
      tick: room.world.tick,
      detail: { current: room.phase, requested: next },
    });
    return false;
  }
  room.phase = next;
  return true;
}

export function allPlayersReady(room: Room): boolean {
  let ready = 0;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session === undefined || session.pid <= 0) continue;
    if (!session.ready) return false;
    ready += 1;
  }
  return ready >= 1;
}

export type StartOutcome = 'ok' | 'not-host' | 'not-ready' | 'wrong-phase';

export function tryStartMatch(room: Room, deps: MatchDeps, session: Session): StartOutcome {
  if (session.pid !== room.match.hostId) return 'not-host';
  if (room.phase === MATCH_PHASE.ended) resetMatchForRestart(room, deps);
  if (room.phase !== MATCH_PHASE.lobby) return 'wrong-phase';
  if (!allPlayersReady(room)) return 'not-ready';
  if (!applyMatchTransition(room, deps, MATCH_PHASE.loading)) return 'wrong-phase';
  room.match.loadingMs = LOADING_MS;
  resetMatchCounters(room.match.counters);
  return 'ok';
}

function resetMatchForRestart(room: Room, deps: MatchDeps): void {
  if (!applyMatchTransition(room, deps, MATCH_PHASE.lobby)) return;
  room.match.records.clear();
  room.match.winnerTeam = 0;
  room.match.startedAtMs = 0;
  room.match.endedAtMs = 0;
  room.match.loadingMs = 0;
  room.wave = 0;
  room.intermissionMs = 0;
  room.director = createDirectorState();
  const ids = room.world.activeIds;
  for (let i = ids.length - 1; i >= 0; i -= 1) {
    const id = ids[i];
    if (id === undefined) continue;
    const entity = room.world.entities[id - 1];
    if (entity === undefined) continue;
    if (entity.kind === 'sheep' || entity.kind === 'projectile') despawnEntity(room.world, id);
  }
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session !== undefined && session.pid > 0) joinMatchRecord(room, session.pid, session.name);
  }
  refillPlayers(room);
}

export function updateMatch(
  room: Room,
  deps: MatchDeps,
  nowMs: number,
  elapsedMs: number,
): boolean {
  if (room.phase === MATCH_PHASE.loading) {
    room.match.loadingMs -= elapsedMs;
    if (room.match.loadingMs <= 0) {
      room.match.loadingMs = 0;
      if (!applyMatchTransition(room, deps, MATCH_PHASE.playing)) return false;
      room.wave = 1;
      room.match.startedAtMs = nowMs;
      refillPlayers(room);
      emit(deps.log, 'info', LOG_EVENTS.matchStart, {
        room: room.code,
        tick: room.world.tick,
        detail: { wave: room.wave, players: room.sessions.length },
      });
      return true;
    }
    return false;
  }
  if (room.phase === MATCH_PHASE.intermission) {
    room.intermissionMs -= elapsedMs;
    const elapsed = INTERMISSION_MS - room.intermissionMs;
    if (elapsed >= INTERMISSION_SKIP_MIN_MS && allPlayersReady(room)) room.intermissionMs = 0;
    if (room.intermissionMs <= 0) {
      room.intermissionMs = 0;
      if (!applyMatchTransition(room, deps, MATCH_PHASE.playing)) return false;
      room.wave += 1;
      refillPlayers(room);
      return true;
    }
  }
  return false;
}

export function handleWaveCleared(room: Room, deps: MatchDeps, nowMs: number): boolean {
  if (room.phase !== MATCH_PHASE.playing) return false;
  emit(deps.log, 'info', LOG_EVENTS.waveClear, {
    room: room.code,
    tick: room.world.tick,
    detail: { wave: room.wave },
  });
  reviveDownedForWaveClear(room.world);
  if (room.wave >= WAVE_MAX) {
    endMatch(room, deps, nowMs, 0);
    return true;
  }
  if (!applyMatchTransition(room, deps, MATCH_PHASE.intermission)) return false;
  room.intermissionMs = INTERMISSION_MS;
  return true;
}

export function checkMatchEnd(room: Room, deps: MatchDeps, nowMs: number): boolean {
  if (room.phase !== MATCH_PHASE.playing) return false;
  let active = 0;
  let allDowned = true;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session === undefined || session.pid <= 0) continue;
    if (session.disconnectedAtMs !== null) continue;
    active += 1;
    const entity = room.world.entities[session.pid - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    if (!entity.combat.downed.downed) allDowned = false;
  }
  if (active === 0) {
    if (room.sessions.length === 0) {
      endMatch(room, deps, nowMs, 1);
      return true;
    }
    return false;
  }
  if (allDowned) {
    endMatch(room, deps, nowMs, 1);
    return true;
  }
  return false;
}

export function endMatch(room: Room, deps: MatchDeps, nowMs: number, winnerTeam: 0 | 1): void {
  if (room.phase === MATCH_PHASE.ended) return;
  if (!applyMatchTransition(room, deps, MATCH_PHASE.ended)) return;
  room.match.winnerTeam = winnerTeam;
  room.match.endedAtMs = nowMs;
  const durationMs = nowMs > room.match.startedAtMs ? nowMs - room.match.startedAtMs : 0;
  const events = room.world.events;
  let found = false;
  for (let i = events.length - 1; i >= 0; i -= 1) {
    const event = events[i];
    if (event !== undefined && event.type === 'matchEnded') {
      event.subjectId = winnerTeam;
      event.value = room.wave;
      event.flags = durationMs;
      found = true;
      break;
    }
  }
  if (!found) {
    const event = createSimEvent();
    event.type = 'matchEnded';
    event.tick = room.world.tick;
    event.flags = durationMs;
    event.subjectId = winnerTeam;
    event.value = room.wave;
    events.push(event);
  }
  deps.metrics.matchesFinished += 1;
  const record = buildMatchResult(room);
  emit(deps.log, 'info', LOG_EVENTS.matchEnd, {
    room: room.code,
    tick: room.world.tick,
    detail: { durationMs, wave: room.wave, winnerTeam },
  });
  void deps.store.appendMatchResult(record).catch((error: unknown) => {
    emit(deps.log, 'error', LOG_EVENTS.storeError, {
      room: room.code,
      tick: room.world.tick,
      detail: { error: String(error) },
    });
  });
  if (deps.dataDir !== undefined) {
    const diagnostics = buildMatchDiagnostics({
      matchId: record.matchId,
      startedAtMs: room.match.startedAtMs,
      endedAtMs: room.match.endedAtMs,
      counters: room.match.counters,
      metrics: deps.metrics,
    });
    void writeMatchReport(deps.dataDir, diagnostics).catch((error: unknown) => {
      emit(deps.log, 'error', LOG_EVENTS.reportWriteFailed, {
        room: room.code,
        tick: room.world.tick,
        detail: { error: String(error) },
      });
    });
  }
}

export function buildMatchResult(room: Room): MatchResultRecord {
  const runtime = room.match;
  const durationMs =
    runtime.endedAtMs > runtime.startedAtMs ? runtime.endedAtMs - runtime.startedAtMs : 0;
  const players: MatchResultRecord['players'] = [];
  for (const record of runtime.records.values()) {
    players.push({
      name: record.name,
      kills: record.stats.kills,
      headshots: record.stats.headshots,
      shotsFired: record.stats.shotsFired,
      hits: record.stats.hits,
      revives: record.stats.revives,
      downs: record.stats.downs,
      aliveMs: record.stats.aliveMs,
      leftMidMatch: record.leftMidMatch,
    });
  }
  return {
    matchId: room.code + '-' + String(runtime.startedAtMs),
    startedAtMs: runtime.startedAtMs,
    durationMs,
    waveReached: room.wave,
    winnerTeam: runtime.winnerTeam,
    playerCount: players.length,
    players,
  };
}

export function refillPlayers(room: Room): void {
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session === undefined || session.pid <= 0) continue;
    const entity = room.world.entities[session.pid - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    entity.hp = entity.maxHp;
    entity.armor = ARMOR_MAX;
    entity.weapon.magInSlot[0] = WEAPONS.pistol.mag;
    entity.weapon.magInSlot[1] = WEAPONS.rifle.mag;
    entity.weapon.magInSlot[2] = WEAPONS.shotgun.mag;
    entity.weapon.reserveAmmo = RESERVE_AMMO_INITIAL;
    entity.weapon.reloadEndsAtMs = 0;
    resetDownedState(entity.combat.downed);
    resetRageState(entity.combat.rage);
  }
}

function playerRecordFor(room: Room, entityId: number): MatchPlayerRecord | undefined {
  if (entityId <= 0) return undefined;
  const entity = room.world.entities[entityId - 1];
  if (entity === undefined || entity.kind !== 'player') return undefined;
  return room.match.records.get(entityId);
}

export function captureShotBaseline(room: Room): void {
  const sessions = room.sessions;
  const runtime = room.match;
  for (let i = 0; i < LIMITS.maxPlayersPerRoom; i += 1) {
    runtime.shotMagBefore[i] = -1;
    runtime.shotSlotBefore[i] = -1;
  }
  for (let i = 0; i < sessions.length && i < LIMITS.maxPlayersPerRoom; i += 1) {
    const session = sessions[i];
    if (session === undefined || session.pid <= 0) continue;
    const entity = room.world.entities[session.pid - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    runtime.shotMagBefore[i] = entity.weapon.magInSlot[entity.weapon.activeSlot];
    runtime.shotSlotBefore[i] = entity.weapon.activeSlot;
  }
}

export function applyShotDeltas(room: Room): void {
  const sessions = room.sessions;
  const runtime = room.match;
  for (let i = 0; i < sessions.length && i < LIMITS.maxPlayersPerRoom; i += 1) {
    const session = sessions[i];
    if (session === undefined || session.pid <= 0) continue;
    const before = runtime.shotMagBefore[i] ?? -1;
    const slotBefore = runtime.shotSlotBefore[i] ?? -1;
    if (before < 0) continue;
    const entity = room.world.entities[session.pid - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    if (slotBefore !== entity.weapon.activeSlot) continue;
    const after = entity.weapon.magInSlot[entity.weapon.activeSlot];
    if (after >= before) continue;
    const record = room.match.records.get(session.pid);
    if (record === undefined) continue;
    for (let fired = before - after; fired > 0; fired -= 1) noteShot(record.stats);
  }
}

export function accumulateMatchEvents(room: Room): void {
  const events = room.world.events;
  for (let i = 0; i < events.length; i += 1) {
    const event = events[i];
    if (event === undefined) continue;
    switch (event.type) {
      case 'playerHit': {
        const record = playerRecordFor(room, event.subjectId);
        if (record !== undefined) noteHit(record.stats);
        break;
      }
      case 'sheepKilled': {
        const record = playerRecordFor(room, event.subjectId);
        if (record !== undefined) noteKill(record.stats, (event.flags & HIT_FLAG.headshot) !== 0);
        break;
      }
      case 'playerDowned': {
        const victim = event.targetId > 0 ? event.targetId : event.subjectId;
        const record = room.match.records.get(victim);
        if (record !== undefined) noteDown(record.stats);
        break;
      }
      case 'reviveDone': {
        const record = playerRecordFor(room, event.subjectId);
        if (record !== undefined) noteRevive(record.stats);
        break;
      }
      default:
        break;
    }
  }
}

export function accumulateMatchTime(room: Room, dtMs: number): void {
  if (room.phase !== MATCH_PHASE.playing && room.phase !== MATCH_PHASE.intermission) return;
  const sessions = room.sessions;
  for (let i = 0; i < sessions.length; i += 1) {
    const session = sessions[i];
    if (session === undefined || session.pid <= 0) continue;
    const record = room.match.records.get(session.pid);
    if (record === undefined) continue;
    const entity = room.world.entities[session.pid - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    if (entity.combat.downed.downed) accumulateDowned(record.stats, dtMs);
    else accumulateAlive(record.stats, dtMs);
  }
}
