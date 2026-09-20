import {
  LIMITS,
  SERVER_TICK_MS,
  createCommand,
  getEntity,
  type Command,
  type Entity,
} from '@ac/shared';
import { describe, expect, it } from 'vitest';
import { createNullMatchStore } from './match/store.ts';
import { createMetrics } from './metrics.ts';
import {
  applyPoseValidation,
  createRoom,
  resolveShot,
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
  shooter: Session;
  target: Session;
  shooterEntity: Entity;
  targetEntity: Entity;
  tick(): void;
}

function fixture(): Fixture {
  const now = 1_000_000;
  const room = createRoom('RW01', 5, now, LIMITS.maxPlayersPerRoom);
  const deps: RoomDeps = {
    metrics: createMetrics(),
    store: createNullMatchStore(),
    now: (): number => clock,
    log: (): void => undefined,
    monotonicNow: (): number => clock,
  };
  let clock = now;
  const shooter = createSession(stubConnection(1), now);
  const target = createSession(stubConnection(2), now);
  expect(roomJoin(room, shooter, now)).toBe('ok');
  expect(roomJoin(room, target, now)).toBe('ok');
  const shooterEntity = getEntity(room.world, shooter.pid);
  const targetEntity = getEntity(room.world, target.pid);
  if (shooterEntity === undefined || targetEntity === undefined) throw new Error('no entities');
  return {
    room,
    deps,
    shooter,
    target,
    shooterEntity,
    targetEntity,
    tick(): void {
      clock += SERVER_TICK_MS;
      updateRoom(deps, room, clock);
    },
  };
}

function aim(entity: Entity, x: number, z: number, yaw: number): void {
  entity.pos.x = x;
  entity.pos.y = 0;
  entity.pos.z = z;
  entity.yaw = yaw;
  entity.pitch = 0;
  entity.vel.x = 0;
  entity.vel.y = 0;
  entity.vel.z = 0;
}

function moveCommand(
  seq: number,
  yaw: number,
  moveX: number,
  moveY: number,
  sprint: boolean,
): Command {
  const command = createCommand();
  command.seq = seq & 0xffff;
  command.yaw = yaw;
  command.moveX = moveX;
  command.moveY = moveY;
  command.buttons = sprint ? 2 : 0;
  return command;
}

describe('回滚补偿命中判定', () => {
  it('当下姿态命中，横向让开后仅靠回滚才能命中', () => {
    const f = fixture();
    aim(f.shooterEntity, 0, 20, 0);
    aim(f.targetEntity, 0, 30, 0);

    for (let i = 0; i < 6; i += 1) f.tick();

    const current = resolveShot(f.room, f.deps, f.shooter.pid, 0);
    expect(current.hit).toBe(true);
    expect(current.targetId).toBe(f.target.pid);
    expect(current.clamped).toBe(false);

    f.targetEntity.pos.x = 3;
    f.tick();
    expect(resolveShot(f.room, f.deps, f.shooter.pid, 0).hit).toBe(false);

    const rewound = resolveShot(f.room, f.deps, f.shooter.pid, 100);
    expect(rewound.hit).toBe(true);
    expect(rewound.targetId).toBe(f.target.pid);
    expect(rewound.rewindMs).toBe(100);
    expect(f.deps.metrics.rewindClampedCount).toBe(0);
  });

  it('超过 200ms 的请求被夹取并计数', () => {
    const f = fixture();
    aim(f.shooterEntity, 0, 20, 0);
    aim(f.targetEntity, 0, 30, 0);
    for (let i = 0; i < 6; i += 1) f.tick();

    resolveShot(f.room, f.deps, f.shooter.pid, 400);
    expect(f.deps.metrics.rewindClampedCount).toBe(1);
    const atLimit = resolveShot(f.room, f.deps, f.shooter.pid, 200);
    expect(atLimit.clamped).toBe(false);
    expect(f.deps.metrics.rewindClampedCount).toBe(1);
  });

  it('谷仓挡住时不允许隔墙命中', () => {
    const f = fixture();
    aim(f.shooterEntity, 0, -20, 0);
    aim(f.targetEntity, 0, 20, 0);
    for (let i = 0; i < 4; i += 1) f.tick();

    expect(resolveShot(f.room, f.deps, f.shooter.pid, 0).hit).toBe(false);
  });
});

describe('150ms 延迟 + 3% 丢包下的 600 tick', () => {
  it('硬纠正不超过 2 次且没有速度违规', () => {
    const f = fixture();
    aim(f.shooterEntity, 10, 20, 0);
    aim(f.targetEntity, -10, 20, Math.PI);

    const delayed: { arriveAt: number; session: Session; command: Command }[] = [];
    for (let i = 0; i < 600; i += 1) {
      if (i % 33 !== 0) {
        delayed.push({
          arriveAt: i + 3,
          session: f.shooter,
          command: moveCommand(i, i * 0.03, 1, 0, i % 120 < 60),
        });
        delayed.push({
          arriveAt: i + 3,
          session: f.target,
          command: moveCommand(i, Math.PI + i * 0.02, 0, 1, false),
        });
      }
      for (let j = delayed.length - 1; j >= 0; j -= 1) {
        const entry = delayed[j];
        if (entry === undefined || entry.arriveAt > i) continue;
        const into = entry.session.command;
        const from = entry.command;
        into.seq = from.seq;
        into.tick = from.tick;
        into.moveX = from.moveX;
        into.moveY = from.moveY;
        into.yaw = from.yaw;
        into.pitch = from.pitch;
        into.buttons = from.buttons;
        into.switchTo = from.switchTo;
        delayed.splice(j, 1);
      }
      f.tick();
    }

    expect(f.deps.metrics.hardCorrectTotal).toBe(0);
    expect(f.deps.metrics.speedViolations).toBe(0);
    expect(f.deps.metrics.ticks).toBeGreaterThanOrEqual(600);
    const border = LIMITS.maxPlayersPerRoom > 0 ? f.room.world.config.arena.halfSize : 0;
    expect(Math.abs(f.shooterEntity.pos.x)).toBeLessThanOrEqual(border);
    expect(Math.abs(f.shooterEntity.pos.z)).toBeLessThanOrEqual(border);
  });
});

describe('姿态校验纠正', () => {
  it('3 倍速位移被权威值覆盖并计数', () => {
    const f = fixture();
    aim(f.shooterEntity, 0, 20, 0);
    f.tick();
    const legalX = f.shooterEntity.pos.x;
    f.room.preTick[0] = legalX - 1;
    applyPoseValidation(f.deps, f.room, 1);
    expect(f.deps.metrics.speedViolations).toBe(1);
    expect(f.deps.metrics.hardCorrectTotal).toBe(1);
    expect(f.shooterEntity.pos.x).toBe(legalX - 1);
    expect(f.shooterEntity.vel.x).toBe(0);
  });

  it('合法位移不被纠正', () => {
    const f = fixture();
    aim(f.shooterEntity, 0, 20, 0);
    f.tick();
    const before = f.shooterEntity.pos.x;
    f.room.preTick[0] = before - 0.1;
    applyPoseValidation(f.deps, f.room, 1);
    expect(f.deps.metrics.speedViolations).toBe(0);
    expect(f.deps.metrics.hardCorrectTotal).toBe(0);
    expect(f.shooterEntity.pos.x).toBe(before);
  });
});
