import {
  ARENA,
  LIMITS,
  MATCH_PHASE,
  SERVER_TICK_MS,
  SNAPSHOT_RATE_X10,
  countActive,
  createCommand,
  createSnapshot,
  createWorld,
  despawnEntity,
  encodeEventFrame,
  encodeMatchState,
  encodeSnapshot,
  snapshotWorld,
  spawnEntity,
  stepWorld,
  type Command,
  type MatchState,
  type Snapshot,
  type World,
} from '@ac/shared';

import { recordTickJitter, type Metrics } from './metrics.ts';
import type { Session } from './session.ts';

export const MATCH_STATE_INTERVAL_MS = 1000;
export const INTERMISSION_MS = 5000;

export interface Room {
  readonly code: string;
  readonly world: World;
  readonly sessions: Session[];
  readonly commands: Command[];
  readonly idleCommand: Command;
  readonly snapshot: Snapshot;
  readonly matchState: MatchState;
  readonly broadcastBuffer: Uint8Array;
  phase: number;
  wave: number;
  intermissionMs: number;
  lastUpdateMs: number;
  lastTickAtMs: number;
  accumulatorMs: number;
  matchStateTimerMs: number;
  emptySinceMs: number | null;
  snapshotRateX10: number;
  jitterArmed: boolean;
  readonly capacity: number;
}

export interface RoomDeps {
  readonly metrics: Metrics;
  monotonicNow(): number;
}

export function createRoom(
  code: string,
  seed: number,
  nowMs: number,
  capacity: number = LIMITS.maxPlayersPerRoom,
): Room {
  const world = createWorldForRoom(seed);
  return {
    code,
    world,
    sessions: [],
    commands: [],
    idleCommand: createCommand(),
    snapshot: createSnapshot(),
    matchState: { phase: MATCH_PHASE.lobby, wave: 0, intermissionMs: 0, players: [] },
    broadcastBuffer: new Uint8Array(LIMITS.maxFrameBytes),
    phase: MATCH_PHASE.lobby,
    wave: 0,
    intermissionMs: 0,
    lastUpdateMs: nowMs,
    lastTickAtMs: nowMs,
    accumulatorMs: 0,
    matchStateTimerMs: 0,
    emptySinceMs: nowMs,
    snapshotRateX10: SNAPSHOT_RATE_X10,
    jitterArmed: false,
    capacity: Math.min(capacity, LIMITS.maxPlayersPerRoom),
  };
}

function createWorldForRoom(seed: number): World {
  const world = createWorld(seed);
  const initialPlayers: number[] = [];
  for (const entity of world.entities) {
    if (entity.active && entity.kind === 'player') initialPlayers.push(entity.id);
  }
  initialPlayers.sort((a, b) => b - a);
  for (const id of initialPlayers) despawnEntity(world, id);
  return world;
}

export function roomJoin(room: Room, session: Session, nowMs: number): 'ok' | 'full' {
  if (room.sessions.length >= room.capacity) return 'full';
  const spawnIndex = room.sessions.length % ARENA.playerSpawnPoints.length;
  const spawn = ARENA.playerSpawnPoints[spawnIndex];
  if (spawn === undefined) return 'full';
  const result = spawnEntity(room.world, 'player', spawn.x, 0, spawn.z);
  if (!result.ok) return 'full';
  session.pid = result.id;
  session.roomCode = room.code;
  session.ready = false;
  session.weapon = 0;
  session.kills = 0;
  session.joinedAtMs = nowMs;
  room.sessions.push(session);
  room.emptySinceMs = null;
  room.jitterArmed = false;
  broadcastMatchState(room);
  return 'ok';
}

export function roomLeave(room: Room, session: Session, nowMs: number): boolean {
  const index = room.sessions.indexOf(session);
  if (index < 0) return false;
  room.sessions.splice(index, 1);
  if (session.pid > 0) despawnEntity(room.world, session.pid);
  session.pid = 0;
  session.roomCode = null;
  session.ready = false;
  if (room.sessions.length === 0) {
    room.emptySinceMs = nowMs;
    room.accumulatorMs = 0;
    room.jitterArmed = false;
  }
  broadcastMatchState(room);
  return true;
}

function buildCommands(room: Room): void {
  const commands = room.commands;
  commands.length = 0;
  const world = room.world;
  const ids = world.activeIds;
  for (let i = 0; i < ids.length; i += 1) {
    const id = ids[i];
    if (id === undefined) continue;
    const entity = world.entities[id - 1];
    if (entity === undefined || !entity.active || entity.kind !== 'player') continue;
    let command: Command | undefined;
    for (let s = 0; s < room.sessions.length; s += 1) {
      const session = room.sessions[s];
      if (session !== undefined && session.pid === id) {
        command = session.command;
        break;
      }
    }
    if (command === undefined) {
      const idle = room.idleCommand;
      idle.moveX = 0;
      idle.moveY = 0;
      idle.buttons = 0;
      idle.seq = 0;
      idle.tick = 0;
      idle.switchTo = 0;
      idle.yaw = entity.yaw;
      idle.pitch = 0;
      command = idle;
    }
    commands.push(command);
  }
}

export function updateRoom(deps: RoomDeps, room: Room, nowMs: number): void {
  let elapsed = nowMs - room.lastUpdateMs;
  if (elapsed < 0) elapsed = 0;
  room.lastUpdateMs = nowMs;

  if (room.sessions.length === 0) {
    room.accumulatorMs = 0;
    room.jitterArmed = false;
    room.matchStateTimerMs = 0;
    return;
  }

  room.accumulatorMs += elapsed;
  let steps = 0;
  while (room.accumulatorMs >= SERVER_TICK_MS) {
    if (steps >= LIMITS.tickCatchUpLimit) {
      const skipped = Math.floor(room.accumulatorMs / SERVER_TICK_MS);
      room.accumulatorMs -= skipped * SERVER_TICK_MS;
      deps.metrics.tickSkips += skipped;
      break;
    }
    room.accumulatorMs -= SERVER_TICK_MS;
    runTick(deps, room, steps === 0);
    steps += 1;
  }

  if (room.phase === MATCH_PHASE.intermission) {
    room.intermissionMs -= elapsed;
    if (room.intermissionMs <= 0) {
      room.intermissionMs = 0;
      room.phase = MATCH_PHASE.playing;
      room.wave = 1;
      broadcastMatchState(room);
    }
  }

  room.matchStateTimerMs += elapsed;
  while (room.matchStateTimerMs >= MATCH_STATE_INTERVAL_MS) {
    room.matchStateTimerMs -= MATCH_STATE_INTERVAL_MS;
    broadcastMatchState(room);
  }
}

function runTick(deps: RoomDeps, room: Room, firstInBurst: boolean): void {
  const monotonic = deps.monotonicNow();
  if (room.jitterArmed && firstInBurst)
    recordTickJitter(deps.metrics, monotonic - room.lastTickAtMs - SERVER_TICK_MS);
  room.lastTickAtMs = monotonic;
  room.jitterArmed = true;
  deps.metrics.ticks += 1;

  buildCommands(room);
  stepWorld(room.world, room.commands, SERVER_TICK_MS);
  snapshotWorld(room.world, room.snapshot);

  const active = countActive(room.world);
  if (active > deps.metrics.maxEntitiesObserved) deps.metrics.maxEntitiesObserved = active;

  const periodicFull = room.world.tick % LIMITS.fullSnapshotIntervalTicks === 0;
  const sessions = room.sessions;
  for (let i = 0; i < sessions.length; i += 1) {
    const session = sessions[i];
    if (session !== undefined) sendSnapshot(deps, room, session, periodicFull);
  }

  const events = room.world.events;
  if (events.length > 0) {
    const size = encodeEventFrame(room.world.tick, events, room.broadcastBuffer);
    const frame = room.broadcastBuffer.subarray(0, size);
    for (let i = 0; i < sessions.length; i += 1) {
      const session = sessions[i];
      if (session !== undefined) session.connection.send(frame);
    }
    deps.metrics.eventsSent += events.length;
  }
}

function sendSnapshot(deps: RoomDeps, room: Room, session: Session, periodicFull: boolean): void {
  const forceFull = session.baseline.tick === 0 || periodicFull;
  const size = encodeSnapshot(
    room.snapshot,
    session.baseline,
    session.lastAckedCmdSeq,
    session.outbound,
    forceFull,
  );
  session.connection.send(session.outbound.subarray(0, size));
  deps.metrics.snapshotsSent += 1;
  deps.metrics.snapshotBytes += size;
  deps.metrics.snapshotRecords += room.snapshot.entities.length;
  deps.metrics.framesOut += 1;
  deps.metrics.bytesOut += size;
}

function hpRatioOf(room: Room, pid: number): number {
  const entity = pid > 0 ? room.world.entities[pid - 1] : undefined;
  if (entity === undefined || !entity.active) return 0;
  if (entity.maxHp <= 0) return 0;
  return entity.hp / entity.maxHp;
}

export function broadcastMatchState(room: Room): void {
  const state = room.matchState;
  state.phase = room.phase;
  state.wave = room.wave;
  state.intermissionMs =
    room.intermissionMs > 0 ? Math.min(65535, Math.round(room.intermissionMs)) : 0;
  const sessions = room.sessions;
  for (let i = 0; i < sessions.length; i += 1) {
    const session = sessions[i];
    if (session === undefined) continue;
    let entry = state.players[i];
    if (entry === undefined) {
      entry = { pid: 0, name: '', ready: false, weapon: 0, hpRatio: 1, kills: 0 };
      state.players[i] = entry;
    }
    entry.pid = session.pid;
    entry.name = session.name;
    entry.ready = session.ready;
    entry.weapon = session.weapon;
    entry.hpRatio = hpRatioOf(room, session.pid);
    entry.kills = session.kills;
  }
  state.players.length = sessions.length;
  if (sessions.length === 0) return;
  const size = encodeMatchState(state, room.broadcastBuffer);
  const frame = room.broadcastBuffer.subarray(0, size);
  for (let i = 0; i < sessions.length; i += 1) {
    const session = sessions[i];
    if (session !== undefined) session.connection.send(frame);
  }
}
