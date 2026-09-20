import { LIMITS, MATCH_PHASE, NEW_ROOM_CODE, createRng, type Rng } from '@ac/shared';

import { LOG_EVENTS, SILENT_LOGGER, emit, type LogSink } from './log.ts';
import { GRACE_PERIOD_MS } from './match/controller.ts';
import type { Metrics } from './metrics.ts';
import {
  createRoom,
  roomDisconnect,
  roomIsIdle,
  roomJoin,
  roomLeave,
  roomReconnect,
  type Room,
} from './room.ts';
import { pickRoomCode } from './security.ts';
import type { Session } from './session.ts';

export type JoinFailure = 'room-not-found' | 'room-full' | 'match-in-progress';

export type JoinOutcome =
  { readonly ok: true; readonly room: Room } | { readonly ok: false; readonly reason: JoinFailure };

export interface RoomRegistryOptions {
  readonly maxRooms: number;
  readonly maxPlayersPerRoom?: number;
  readonly seed?: number;
  readonly log?: LogSink;
}

export interface RoomRegistry {
  readonly rooms: Map<string, Room>;
  create(nowMs: number): Room | null;
  join(code: string, session: Session, nowMs: number): JoinOutcome;
  leave(session: Session, nowMs: number): void;
  disconnect(session: Session, nowMs: number): void;
  roomOf(session: Session): Room | undefined;
  reclaimIdle(nowMs: number): number;
  readonly size: number;
}

function findGracedSession(room: Room, name: string): Session | undefined {
  if (name.length === 0) return undefined;
  for (let i = 0; i < room.sessions.length; i += 1) {
    const session = room.sessions[i];
    if (session !== undefined && session.disconnectedAtMs !== null && session.name === name) {
      return session;
    }
  }
  return undefined;
}

export function createRoomRegistry(metrics: Metrics, options: RoomRegistryOptions): RoomRegistry {
  const rooms = new Map<string, Room>();
  const rng: Rng = createRng(options.seed ?? Date.now() % 2147483647, 'spawn');
  const log = options.log ?? SILENT_LOGGER;
  let roomSeed = 1;

  function generateCode(): string | null {
    for (let attempt = 0; attempt < LIMITS.roomCodeAttempts; attempt += 1) {
      const code = pickRoomCode(rng);
      if (code !== NEW_ROOM_CODE && !rooms.has(code)) return code;
    }
    return null;
  }

  function create(nowMs: number): Room | null {
    if (rooms.size >= options.maxRooms) return null;
    const code = generateCode();
    if (code === null) return null;
    const room = createRoom(
      code,
      roomSeed,
      nowMs,
      options.maxPlayersPerRoom ?? LIMITS.maxPlayersPerRoom,
    );
    roomSeed += 1;
    rooms.set(code, room);
    metrics.roomsCreated += 1;
    emit(log, 'info', LOG_EVENTS.roomCreate, {
      room: room.code,
      detail: { capacity: room.capacity, maxRooms: options.maxRooms },
    });
    return room;
  }

  return {
    rooms,
    create,
    join(code: string, session: Session, nowMs: number): JoinOutcome {
      let room: Room | undefined;
      if (code === NEW_ROOM_CODE) {
        const created = create(nowMs);
        if (created === null) return { ok: false, reason: 'room-full' };
        room = created;
      } else {
        room = rooms.get(code);
        if (room === undefined) return { ok: false, reason: 'room-not-found' };
        const graced = findGracedSession(room, session.name);
        if (graced !== undefined) {
          const reconnected = roomReconnect(room, graced, session);
          if (reconnected === 'ok') {
            room.match.counters.graceReconnects += 1;
            metrics.graceReconnects += 1;
            emit(log, 'info', LOG_EVENTS.graceReconnect, {
              room: room.code,
              tick: room.world.tick,
              pid: session.pid,
              detail: { name: session.name },
            });
            return { ok: true, room };
          }
        }
        if (room.phase === MATCH_PHASE.playing) return { ok: false, reason: 'match-in-progress' };
      }
      const result = roomJoin(room, session, nowMs);
      if (result === 'full') {
        if (room.sessions.length === 0) rooms.delete(room.code);
        return { ok: false, reason: 'room-full' };
      }
      return { ok: true, room };
    },
    leave(session: Session, nowMs: number): void {
      if (session.roomCode === null) return;
      const room = rooms.get(session.roomCode);
      if (room === undefined) {
        session.roomCode = null;
        return;
      }
      roomLeave(room, session, nowMs);
    },
    disconnect(session: Session, nowMs: number): void {
      if (session.roomCode === null) return;
      const room = rooms.get(session.roomCode);
      if (room === undefined) {
        session.roomCode = null;
        return;
      }
      if (session.disconnectedAtMs !== null) return;
      roomDisconnect(room, session, nowMs);
      room.match.counters.graceStarts += 1;
      metrics.graceStarts += 1;
      emit(log, 'info', LOG_EVENTS.graceStart, {
        room: room.code,
        tick: room.world.tick,
        pid: session.pid,
        detail: { graceMs: GRACE_PERIOD_MS },
      });
    },
    roomOf(session: Session): Room | undefined {
      if (session.roomCode === null) return undefined;
      return rooms.get(session.roomCode);
    },
    reclaimIdle(nowMs: number): number {
      let reclaimed = 0;
      for (const [code, room] of rooms) {
        if (!roomIsIdle(room)) continue;
        const since = room.emptySinceMs ?? nowMs;
        if (nowMs - since < LIMITS.emptyRoomReclaimMs) continue;
        for (let i = 0; i < room.sessions.length; i += 1) {
          const session = room.sessions[i];
          if (session === undefined) continue;
          session.pid = 0;
          session.roomCode = null;
          session.disconnectedAtMs = null;
        }
        room.sessions.length = 0;
        rooms.delete(code);
        reclaimed += 1;
      }
      metrics.roomsReclaimed += reclaimed;
      if (reclaimed > 0) {
        emit(log, 'info', LOG_EVENTS.roomReclaim, { detail: { reclaimed } });
      }
      return reclaimed;
    },
    get size(): number {
      return rooms.size;
    },
  };
}
