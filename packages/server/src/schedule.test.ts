import { LIMITS, SERVER_TICK_MS } from '@ac/shared';
import { describe, expect, it } from 'vitest';

import { createNullMatchStore } from './match/store.ts';
import { createMetrics, type Metrics } from './metrics.ts';
import {
  createRoom,
  removeMember,
  roomJoin,
  updateRoom,
  type Room,
  type RoomDeps,
} from './room.ts';
import { createSession, type Session } from './session.ts';
import type { Connection } from './transport/types.ts';

function stubConnection(id: number): Connection {
  return {
    id,
    remote: 'test',
    closed: false,
    send: (): void => undefined,
    close: (): void => undefined,
    onMessage: (): void => undefined,
    onClose: (): void => undefined,
  };
}

interface Fixture {
  room: Room;
  deps: RoomDeps;
  metrics: Metrics;
  session: Session;
  /** 推进真实时间 ms 后调用一次 updateRoom（间隔即喂入间隔）。 */
  feed(ms: number): void;
  wallMs(): number;
}

/**
 * monotonicMode = 'clock'：假时钟与真实时间同源（判定调度误差/漂移用）；
 * monotonicMode = 'step'：每次读取 monotonicNow() 各前进 stepMs（用来触发工作量预算）。
 */
function fixture(monotonicMode: 'clock' | 'step', stepMs = 0): Fixture {
  const now = 2_000_000;
  const metrics = createMetrics();
  const room = createRoom('SC01', 5, now, LIMITS.maxPlayersPerRoom);
  let clock = now;
  let mono = 0;
  const deps: RoomDeps = {
    metrics,
    store: createNullMatchStore(),
    now: (): number => clock,
    log: (): void => undefined,
    monotonicNow: (): number => {
      if (monotonicMode === 'step') {
        mono += stepMs;
        return mono;
      }
      return clock;
    },
  };
  const session = createSession(stubConnection(1), now);
  expect(roomJoin(room, session, now)).toBe('ok');
  return {
    room,
    deps,
    metrics,
    session,
    feed(ms: number): void {
      clock += ms;
      updateRoom(deps, room, clock);
    },
    wallMs(): number {
      return clock;
    },
  };
}

describe('房间调度与工作量预算', () => {
  it('单调时钟一 tick 内前进超过 roomTickBudgetMs：只跑一个 tick 后让出，累积量不丢', () => {
    const f = fixture('step', 5);
    expect(LIMITS.roomTickBudgetMs).toBe(8);

    f.feed(SERVER_TICK_MS * 4); // 累积 200ms：本可连跑 4 个 tick
    expect(f.room.world.tick).toBe(1);
    expect(f.metrics.roomBudgetExceeded).toBe(1);
    expect(f.room.accumulatorMs).toBeCloseTo(SERVER_TICK_MS * 3, 6);

    // 让出 ≠ 丢 tick：剩余累积量留给后续调用，最终照样跑满且没有跳 tick
    f.feed(0);
    f.feed(0);
    f.feed(0);
    expect(f.room.world.tick).toBe(4);
    expect(f.room.accumulatorMs).toBe(0);
    expect(f.metrics.tickSkips).toBe(0);
  });

  it('不规则喂入：模拟时间不漂移，调度误差有值（真问题可见）', () => {
    const f = fixture('clock');
    const wallStart = f.wallMs();
    const simStart = f.room.world.timeMs;
    // 5 次喂入共 100ms（平均 20ms），但每一段都不落在 50ms 网格上 ⇒ 真实调度抖动
    const pattern = [12, 18, 25, 7, 38];
    for (let i = 0; i < 200; i += 1) f.feed(pattern[i % pattern.length] ?? 20);

    const wallElapsed = f.wallMs() - wallStart;
    const simElapsed = f.room.world.timeMs - simStart;
    expect(wallElapsed).toBe(4000);
    expect(Math.abs(simElapsed - wallElapsed)).toBeLessThanOrEqual(SERVER_TICK_MS);
    expect(f.metrics.simDriftMaxAbs).toBeLessThanOrEqual(SERVER_TICK_MS);
    expect(f.metrics.tickScheduleErrorSamples).toBe(f.room.world.tick);
    expect(f.metrics.tickScheduleErrorMaxMs).toBeGreaterThan(0);
    expect(f.metrics.tickSkips).toBe(0);
  });

  it('规则喂入（50ms 网格）：调度误差与漂移都为 0', () => {
    const f = fixture('clock');
    const wallStart = f.wallMs();
    const simStart = f.room.world.timeMs;
    for (let i = 0; i < 100; i += 1) f.feed(SERVER_TICK_MS);

    expect(f.room.world.timeMs - simStart).toBe(f.wallMs() - wallStart);
    expect(f.metrics.tickScheduleErrorMaxMs).toBe(0);
    expect(f.metrics.simDriftMaxAbs).toBe(0);
  });

  it('首 tick 建立基准：firstTickAtMs 固定、tickIndex 逐 tick 自增', () => {
    const f = fixture('clock');
    f.feed(SERVER_TICK_MS);
    const first = f.room.firstTickAtMs;
    const index = f.room.tickIndex;
    expect(first).toBe(2_000_000 + SERVER_TICK_MS);
    expect(index).toBe(1);

    f.feed(SERVER_TICK_MS);
    f.feed(SERVER_TICK_MS);
    expect(f.room.firstTickAtMs).toBe(first);
    expect(f.room.tickIndex).toBe(index + 2);
  });

  it('房间清空后调度基准重置（重新计时，不把空档算成漂移）', () => {
    const f = fixture('clock');
    f.feed(SERVER_TICK_MS);
    expect(f.room.firstTickAtMs).toBeGreaterThan(0);

    expect(removeMember(f.room, f.session, f.wallMs(), true)).toBe(true);
    expect(f.room.firstTickAtMs).toBe(-1);
    expect(f.room.tickIndex).toBe(0);

    const driftSamples = f.metrics.simDriftSamples;
    f.feed(60_000); // 空房：不步进、不采样
    expect(f.metrics.simDriftSamples).toBe(driftSamples);
    expect(f.room.world.tick).toBe(1);
  });

  it('空房间与断开连接的房间都不产生调度/漂移样本', () => {
    const now = 3_000_000;
    const metrics = createMetrics();
    const room = createRoom('SC02', 6, now, LIMITS.maxPlayersPerRoom);
    let clock = now;
    const deps: RoomDeps = {
      metrics,
      store: createNullMatchStore(),
      now: (): number => clock,
      log: (): void => undefined,
      monotonicNow: (): number => clock,
    };
    clock += 60_000;
    updateRoom(deps, room, clock);
    expect(metrics.simDriftSamples).toBe(0);
    expect(metrics.tickScheduleErrorSamples).toBe(0);

    const session = createSession(stubConnection(2), clock);
    expect(roomJoin(room, session, clock)).toBe('ok');
    session.disconnectedAtMs = clock; // 全部断开（宽限期）：同样不步进
    clock += 60_000;
    updateRoom(deps, room, clock);
    expect(metrics.simDriftSamples).toBe(0);
  });
});
