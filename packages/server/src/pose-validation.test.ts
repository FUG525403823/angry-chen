import {
  ARENA,
  BUTTON,
  CONFIG,
  LIMITS,
  SERVER_TICK_MS,
  getEntity,
  knockbackPlayer,
  spawnEntity,
  type Entity,
} from '@ac/shared';
import { describe, expect, it } from 'vitest';

import { createNullMatchStore } from './match/store.ts';
import { createMetrics } from './metrics.ts';
import {
  applyPoseValidation,
  createRoom,
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
    bufferedAmount: 0,
    send: (): void => undefined,
    close: (): void => undefined,
    onMessage: (): void => undefined,
    onClose: (): void => undefined,
  };
}

interface Fixture {
  room: Room;
  deps: RoomDeps;
  session: Session;
  player: Entity;
  tick(count: number): void;
}

function fixture(): Fixture {
  const now = 1_000_000;
  const room = createRoom('PV01', 11, now, LIMITS.maxPlayersPerRoom);
  let clock = now;
  const deps: RoomDeps = {
    metrics: createMetrics(),
    store: createNullMatchStore(),
    now: (): number => clock,
    log: (): void => undefined,
    monotonicNow: (): number => clock,
  };
  const session = createSession(stubConnection(1), now);
  expect(roomJoin(room, session, now)).toBe('ok');
  const player = getEntity(room.world, session.pid);
  if (player === undefined) throw new Error('player entity missing');
  return {
    room,
    deps,
    session,
    player,
    tick(count: number): void {
      for (let i = 0; i < count; i += 1) {
        clock += SERVER_TICK_MS;
        updateRoom(deps, room, clock);
      }
    },
  };
}

function sprint(f: Fixture): void {
  f.session.command.seq = 1;
  f.session.command.moveX = 1;
  f.session.command.moveY = 0;
  f.session.command.buttons = BUTTON.sprint;
  f.session.command.yaw = 0;
}

describe('权威姿态校验端到端', () => {
  it('16 只羊围住疾跑中的玩家 200 tick：零硬纠正且玩家确实在移动', () => {
    const f = fixture();
    for (let i = 0; i < 16; i += 1) {
      const angle = (i / 16) * Math.PI * 2;
      const spawned = spawnEntity(
        f.room.world,
        'sheep',
        f.player.pos.x + Math.cos(angle) * 0.5,
        0,
        f.player.pos.z + Math.sin(angle) * 0.5,
      );
      expect(spawned.ok).toBe(true);
    }
    sprint(f);

    let path = 0;
    let lastX = f.player.pos.x;
    let lastZ = f.player.pos.z;
    for (let i = 0; i < 200; i += 1) {
      f.tick(1);
      path += Math.hypot(f.player.pos.x - lastX, f.player.pos.z - lastZ);
      lastX = f.player.pos.x;
      lastZ = f.player.pos.z;
    }

    expect(f.deps.metrics.hardCorrectTotal).toBe(0);
    expect(f.deps.metrics.poseRejected).toBe(0);
    // 位移被回退吞掉时玩家会原地不动；围困场景下必须仍在正常移动
    expect(path).toBeGreaterThan(10);
  });

  it('人为瞬移 5m：计入硬纠正并回退到 tick 前位置', () => {
    const f = fixture();
    f.tick(1);
    const before = f.player.pos.x;
    expect(f.deps.metrics.hardCorrectTotal).toBe(0);

    f.player.pos.x = before + 5;
    applyPoseValidation(f.deps, f.room, 1);

    expect(f.deps.metrics.hardCorrectTotal).toBe(1);
    expect(f.deps.metrics.speedViolations).toBe(1);
    expect(f.deps.metrics.poseSuspects).toBe(0);
    expect(f.deps.metrics.poseRejected).toBe(0);
    expect(f.player.pos.x).toBe(before);
    expect(f.player.vel.x).toBe(0);
  });

  // O01 并入修复的回归用例：修复前这里每个 tick 都被判「位置非法」并回退（探针 200 tick → 200 次）。
  it('贴住谷仓外墙 200 tick：零回退且不会挤进谷仓', () => {
    const f = fixture();
    const wallX = ARENA.barn.minX - CONFIG.entity.radiusByKind.player;
    f.player.pos.x = wallX - 0.2;
    f.player.pos.y = 0;
    f.player.pos.z = 0;
    f.session.command.seq = 1;
    f.session.command.moveX = 1;
    f.session.command.moveY = 0;
    f.session.command.buttons = BUTTON.sprint;
    f.session.command.yaw = Math.PI / 2; // forward = (+1, 0)：朝谷仓西墙推进

    f.tick(200);

    expect(f.deps.metrics.hardCorrectTotal).toBe(0);
    expect(f.deps.metrics.poseRejected).toBe(0);
    expect(f.player.pos.x).toBeCloseTo(wallX, 9);
  });

  it('冲撞击退位移被保留：3m 击退跑 20 tick 后位移 ≥ 2.5m', () => {
    const f = fixture();
    f.player.pos.x = 0;
    f.player.pos.y = 0;
    f.player.pos.z = 20;
    knockbackPlayer(f.player, 0, 17, 3);

    f.tick(20);

    const moved = Math.hypot(f.player.pos.x, f.player.pos.z - 20);
    expect(moved).toBeGreaterThanOrEqual(2.5);
    expect(f.deps.metrics.hardCorrectTotal).toBe(0);
    expect(f.deps.metrics.poseSuspects).toBe(0);
  });
});
