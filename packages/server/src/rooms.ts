import { LIMITS, MATCH_PHASE, NEW_ROOM_CODE, createRng, type Rng } from '@ac/shared';

import type { Metrics } from './metrics.ts';
import { createRoom, roomJoin, roomLeave, type Room } from './room.ts';
import type { Session } from './session.ts';

export type JoinFailure = 'room-not-found' | 'room-full' | 'match-in-progress';

export type JoinOutcome =
  { readonly ok: true; readonly room: Room } | { readonly ok: false; readonly reason: JoinFailure };

export interface RoomRegistryOptions {
  readonly maxRooms: number;
  readonly maxPlayersPerRoom?: number;
  readonly seed?: number;
}

export interface RoomRegistry {
  readonly rooms: Map<string, Room>;
  create(nowMs: number): Room | null;
  join(code: string, session: Session, nowMs: number): JoinOutcome;
  leave(session: Session, nowMs: number): void;
  roomOf(session: Session): Room | undefined;
  reclaimIdle(nowMs: number): number;
  readonly size: number;
}

export function createRoomRegistry(metrics: Metrics, options: RoomRegistryOptions): RoomRegistry {
  const rooms = new Map<string, Room>();
  const rng: Rng = createRng(options.seed ?? Date.now() % 2147483647, 'spawn');
  let roomSeed = 1;

  function generateCode(): string | null {
    for (let attempt = 0; attempt < LIMITS.roomCodeAttempts; attempt += 1) {
      let code = '';
      for (let i = 0; i < LIMITS.roomCodeLength; i += 1) {
        const index = Math.floor(rng() * LIMITS.roomCodeAlphabet.length);
        code += LIMITS.roomCodeAlphabet[index] ?? 'A';
      }
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
    roomOf(session: Session): Room | undefined {
      if (session.roomCode === null) return undefined;
      return rooms.get(session.roomCode);
    },
    reclaimIdle(nowMs: number): number {
      let reclaimed = 0;
      for (const [code, room] of rooms) {
        if (room.sessions.length > 0) continue;
        const since = room.emptySinceMs ?? nowMs;
        if (nowMs - since < LIMITS.emptyRoomReclaimMs) continue;
        rooms.delete(code);
        reclaimed += 1;
      }
      metrics.roomsReclaimed += reclaimed;
      return reclaimed;
    },
    get size(): number {
      return rooms.size;
    },
  };
}
