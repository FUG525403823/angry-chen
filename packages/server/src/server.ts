import { ERROR_CODE, encodeError } from '@ac/shared';

import { createMetrics, type Metrics } from './metrics.ts';
import { updateRoom, type RoomDeps } from './room.ts';
import { createRoomRegistry, type RoomRegistry } from './rooms.ts';
import { attachSession, createSession, type Session, type SessionDeps } from './session.ts';
import type { Transport } from './transport/types.ts';

export const ROOM_LOOP_INTERVAL_MS = 5;
export const ROOM_RECLAIM_INTERVAL_MS = 5000;

export interface GameServerOptions {
  readonly maxRooms: number;
  readonly maxPlayersPerRoom?: number;
  readonly seed?: number;
  readonly now?: () => number;
  readonly monotonicNow?: () => number;
  readonly log?: (message: string) => void;
}

export interface GameServer {
  readonly metrics: Metrics;
  readonly rooms: RoomRegistry;
  readonly transport: Transport;
  readonly sessionCount: number;
  step(nowMs: number): void;
  listen(): Promise<void>;
  close(): Promise<void>;
}

export function createGameServer(transport: Transport, options: GameServerOptions): GameServer {
  const metrics = createMetrics();
  const sessions = new Set<Session>();
  const now = options.now ?? ((): number => Date.now());
  const monotonicNow = options.monotonicNow ?? ((): number => performance.now());
  const log = options.log ?? ((): void => undefined);
  const rooms = createRoomRegistry(metrics, {
    maxRooms: options.maxRooms,
    ...(options.maxPlayersPerRoom === undefined
      ? {}
      : { maxPlayersPerRoom: options.maxPlayersPerRoom }),
    ...(options.seed === undefined ? {} : { seed: options.seed }),
  });
  const roomDeps: RoomDeps = { metrics, monotonicNow };
  const deps: SessionDeps = {
    rooms,
    metrics,
    now,
    monotonicNow,
    log,
    onSessionEnd: (session: Session): void => {
      sessions.delete(session);
    },
  };
  let loop: ReturnType<typeof setInterval> | undefined;
  let reclaim: ReturnType<typeof setInterval> | undefined;

  function step(nowMs: number): void {
    for (const room of rooms.rooms.values()) updateRoom(roomDeps, room, nowMs);
  }

  return {
    metrics,
    rooms,
    transport,
    get sessionCount(): number {
      return sessions.size;
    },
    step,
    async listen(): Promise<void> {
      await transport.listen((connection) => {
        const session = createSession(connection, now());
        sessions.add(session);
        attachSession(deps, session);
      });
      loop = setInterval(() => {
        step(now());
      }, ROOM_LOOP_INTERVAL_MS);
      reclaim = setInterval(() => {
        rooms.reclaimIdle(now());
      }, ROOM_RECLAIM_INTERVAL_MS);
      loop.unref();
      reclaim.unref();
    },
    async close(): Promise<void> {
      if (loop !== undefined) clearInterval(loop);
      if (reclaim !== undefined) clearInterval(reclaim);
      loop = undefined;
      reclaim = undefined;
      const frame = new Uint8Array(64);
      const size = encodeError(ERROR_CODE.serverShutdown, 'server shutting down', frame);
      transport.broadcast(frame.subarray(0, size));
      metrics.errorsSent += 1;
      await transport.close();
      for (const session of sessions) session.closed = true;
      sessions.clear();
    },
  };
}
