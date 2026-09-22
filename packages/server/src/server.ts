import { ERROR_CODE, encodeError } from '@ac/shared';

import { SILENT_LOGGER, type LogSink } from './log.ts';
import { createNullMatchStore, type MatchStore } from './match/store.ts';
import { createMetrics, type Metrics } from './metrics.ts';
import { updateRoom, type RoomDeps } from './room.ts';
import { createRoomRegistry, type RoomRegistry } from './rooms.ts';
import { attachSession, createSession, type Session, type SessionDeps } from './session.ts';
import type { Transport } from './transport/types.ts';

/** 房间循环的喂入网格（ms）：与旧 `setInterval(5ms)` 的节奏一致，但按绝对时刻自校正。 */
export const ROOM_LOOP_INTERVAL_MS = 5;
export const ROOM_RECLAIM_INTERVAL_MS = 5000;
/** 轮询间隔（ms）：实际粒度由 OS 决定（Windows ≈15.5ms），自校正保证误差不累积。 */
export const ROOM_LOOP_POLL_MS = 1;
/** 单次 `pump` 允许的追赶上限（ms）：落后超过它就直接对齐当前时刻，避免长停顿后的 while 追赶风暴。 */
export const ROOM_LOOP_MAX_CATCH_UP_MS = 100;

export interface GameServerOptions {
  readonly maxRooms: number;
  readonly maxPlayersPerRoom?: number;
  readonly seed?: number;
  readonly now?: () => number;
  readonly monotonicNow?: () => number;
  readonly log?: LogSink;
  readonly store?: MatchStore;
  readonly dataDir?: string;
  /** O09：`reports/` 保留份数（透传给对局结束路径，默认 `DEFAULT_REPORT_RETENTION`）。 */
  readonly reportRetention?: number;
}

export interface GameServer {
  readonly metrics: Metrics;
  readonly rooms: RoomRegistry;
  readonly transport: Transport;
  readonly store: MatchStore;
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
  const log = options.log ?? SILENT_LOGGER;
  const store = options.store ?? createNullMatchStore();
  const rooms = createRoomRegistry(metrics, {
    maxRooms: options.maxRooms,
    ...(options.maxPlayersPerRoom === undefined
      ? {}
      : { maxPlayersPerRoom: options.maxPlayersPerRoom }),
    ...(options.seed === undefined ? {} : { seed: options.seed }),
    log,
  });
  const roomDeps: RoomDeps = {
    metrics,
    store,
    now,
    log,
    monotonicNow,
    ...(options.dataDir === undefined ? {} : { dataDir: options.dataDir }),
    ...(options.reportRetention === undefined ? {} : { reportRetention: options.reportRetention }),
  };
  const deps: SessionDeps = {
    rooms,
    metrics,
    store,
    now,
    monotonicNow,
    log,
    onSessionEnd: (session: Session): void => {
      sessions.delete(session);
    },
  };
  let loop: ReturnType<typeof setTimeout> | undefined;
  let reclaim: ReturnType<typeof setInterval> | undefined;
  /** 下一个应当喂给房间的绝对时刻（自校正网格，步长 = ROOM_LOOP_INTERVAL_MS）。 */
  let nextDeadlineMs = 0;

  function step(nowMs: number): void {
    for (const room of rooms.rooms.values()) updateRoom(roomDeps, room, nowMs);
  }

  /**
   * 绝对时刻自校正调度。模拟正确性来自房间累积器（`updateRoom`），调度只决定「何时把时间片喂给房间」：
   * - `nextDeadlineMs` 按固定网格前进，单次唤醒的误差不向后续累积；
   * - 只在 `now >= nextDeadlineMs` 时 `step(now)`，其余轮询只做一次比较（不空转 CPU）；
   * - 落后超过 `ROOM_LOOP_MAX_CATCH_UP_MS` 时放弃追赶、直接对齐，避免长停顿后补跑风暴。
   */
  function pump(): void {
    const monotonic = monotonicNow();
    if (monotonic - nextDeadlineMs > ROOM_LOOP_MAX_CATCH_UP_MS) nextDeadlineMs = monotonic;
    if (monotonic >= nextDeadlineMs) {
      step(now());
      nextDeadlineMs += ROOM_LOOP_INTERVAL_MS;
      if (nextDeadlineMs <= monotonic) nextDeadlineMs = monotonic + ROOM_LOOP_INTERVAL_MS;
    }
    loop = setTimeout(pump, ROOM_LOOP_POLL_MS);
    loop.unref();
  }

  return {
    metrics,
    rooms,
    transport,
    store,
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
      nextDeadlineMs = monotonicNow() + ROOM_LOOP_INTERVAL_MS;
      loop = setTimeout(pump, ROOM_LOOP_INTERVAL_MS);
      reclaim = setInterval(() => {
        rooms.reclaimIdle(now());
      }, ROOM_RECLAIM_INTERVAL_MS);
      loop.unref();
      reclaim.unref();
    },
    async close(): Promise<void> {
      if (loop !== undefined) clearTimeout(loop);
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
