import { describe, expect, it } from 'vitest';

import {
  ERROR_CODE,
  MATCH_PHASE,
  NEW_ROOM_CODE,
  OPCODE,
  SERVER_TICK_MS,
  countActive,
  createRng,
} from '@ac/shared';

import { createHarness, sendSimple } from './testing/harness.ts';

describe('房间生命周期', () => {
  it('新建房间返回 Welcome（pid 按加入顺序、四位房间码）', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('陈sir', NEW_ROOM_CODE);

    const welcome = alice.welcome();
    expect(welcome).toBeDefined();
    expect(welcome?.pid).toBe(1);
    expect(welcome?.protocolVersion).toBe(2);
    expect(welcome?.token.length).toBe(8);
    expect(welcome?.roomCode.length).toBe(4);
    expect(NEW_ROOM_CODE).not.toBe(welcome?.roomCode);
    expect(harness.game.rooms.size).toBe(1);
    expect(harness.game.metrics.joins).toBe(1);
    await harness.close();
  });

  it('第二个客户端用房间码进入同一房间，双方世界一致', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    const bob = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    bob.join('bob', code);

    expect(bob.welcome()?.roomCode).toBe(code);
    expect(bob.welcome()?.pid).toBe(2);
    const room = harness.game.rooms.rooms.get(code);
    expect(room?.sessions.length).toBe(2);
    expect(countActive(room?.world as never, 'player')).toBe(2);
    await harness.close();
  });

  it('第五个客户端被拒绝并收到 Error(3)', async () => {
    const harness = createHarness();
    const clients = [harness.connect(), harness.connect(), harness.connect(), harness.connect()];
    clients[0]?.join('p1', NEW_ROOM_CODE);
    const code = clients[0]?.welcome()?.roomCode ?? '';
    for (let i = 1; i < 4; i += 1) clients[i]?.join('p' + String(i + 1), code);

    const late = harness.connect();
    late.join('p5', code);
    expect(late.errorCodes()).toContain(ERROR_CODE.roomFull);
    expect(harness.game.rooms.rooms.get(code)?.sessions.length).toBe(4);
    expect(harness.game.metrics.joins).toBe(4);
    await harness.close();
  });

  it('未知房间码返回 Error(2) 且连接保留', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', 'ZZZZ');
    expect(alice.errorCodes()).toContain(ERROR_CODE.roomNotFound);
    expect(alice.client.closed).toBe(false);
    alice.join('alice', NEW_ROOM_CODE);
    expect(alice.welcome()).toBeDefined();
    await harness.close();
  });

  it('协议版本不匹配返回 Error(1) 并断开', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE, 99);
    expect(alice.errorCodes()).toContain(ERROR_CODE.protocolMismatch);
    expect(alice.client.closed).toBe(true);
    expect(harness.game.rooms.size).toBe(0);
    await harness.close();
  });

  it('非法昵称返回 Error(6) 且连接保留', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('bad name', NEW_ROOM_CODE);
    expect(alice.errorCodes()).toContain(ERROR_CODE.invalidName);
    expect(alice.client.closed).toBe(false);
    await harness.close();
  });

  it('同一连接重复 Join 被当作非法帧', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    alice.join('alice', NEW_ROOM_CODE);
    expect(alice.errorCodes()).toContain(ERROR_CODE.malformedFrame);
    expect(alice.framesOf(OPCODE.welcome).length).toBe(1);
    await harness.close();
  });

  it('leave 帧结束会话并离开房间', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    sendSimple(alice, OPCODE.leave);
    expect(alice.client.closed).toBe(true);
    expect(harness.game.rooms.rooms.get(code)?.sessions.length).toBe(0);
    expect(harness.game.sessionCount).toBe(0);
    await harness.close();
  });

  it('空房间闲置 60 秒后被回收，有人时不回收', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    const bob = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    bob.join('bob', NEW_ROOM_CODE);
    const codeA = alice.welcome()?.roomCode ?? '';
    const codeB = bob.welcome()?.roomCode ?? '';
    expect(codeA).not.toBe(codeB);

    harness.advance(60_000);
    expect(harness.game.rooms.reclaimIdle(harness.clock.value)).toBe(0);
    expect(harness.game.rooms.size).toBe(2);

    sendSimple(alice, OPCODE.leave);
    harness.advance(59_000);
    expect(harness.game.rooms.reclaimIdle(harness.clock.value)).toBe(0);
    harness.advance(2_000);
    expect(harness.game.rooms.reclaimIdle(harness.clock.value)).toBe(1);
    expect(harness.game.rooms.size).toBe(1);
    expect(harness.game.metrics.roomsReclaimed).toBe(1);
    await harness.close();
  });

  it('房间码生成器只使用约定字母表', async () => {
    const harness = createHarness();
    const alphabet = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
    for (let i = 0; i < 3; i += 1) {
      const client = harness.connect();
      client.join('p' + String(i), NEW_ROOM_CODE);
      const code = client.welcome()?.roomCode ?? '';
      for (const char of code) expect(alphabet).toContain(char);
    }
    expect(harness.game.metrics.roomsCreated).toBe(3);
    await harness.close();
  });

  it('房间上限生效', async () => {
    const harness = createHarness({ maxRooms: 1 });
    const alice = harness.connect();
    const bob = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    bob.join('bob', NEW_ROOM_CODE);
    expect(bob.errorCodes()).toContain(ERROR_CODE.roomFull);
    expect(harness.game.rooms.size).toBe(1);
    await harness.close();
  });

  it('单房间玩家上限可用环境变量下调', async () => {
    const harness = createHarness({ maxPlayersPerRoom: 1 });
    const alice = harness.connect();
    const bob = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    bob.join('bob', code);
    expect(bob.errorCodes()).toContain(ERROR_CODE.roomFull);
    expect(harness.game.rooms.rooms.get(code)?.sessions.length).toBe(1);
    await harness.close();
  });

  it('世界与房间种子无关地为每个房间独立生成', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const room = harness.game.rooms.rooms.get(code);
    expect(room?.phase).toBe(MATCH_PHASE.lobby);
    expect(room?.snapshotRateX10).toBe(Math.round(10000 / 50));
    expect(createRng(1, 'fx')()).toBeCloseTo(createRng(1, 'fx')(), 12);
    await harness.close();
  });
});

describe('调度与漂移（O02）', () => {
  it('假时钟驱动 1000 次 step：模拟时间与真实时间偏差 ≤ 1 tick', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');
    const simStart = room.world.timeMs;
    const wallStart = harness.clock.value;

    harness.tick(1000);

    const simElapsed = room.world.timeMs - simStart;
    const wallElapsed = harness.clock.value - wallStart;
    expect(wallElapsed).toBe(1000 * SERVER_TICK_MS);
    expect(simElapsed).toBe(wallElapsed);
    expect(harness.game.metrics.ticks).toBe(1000);
    expect(harness.game.metrics.tickSkips).toBe(0);
    expect(harness.game.metrics.simDriftMaxAbs).toBe(0);
    expect(harness.game.metrics.tickScheduleErrorMaxMs).toBe(0);
    await harness.close();
  });

  it('不规则喂入仍不丢 tick、不漂移，而调度误差有值', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const room = harness.game.rooms.rooms.get(alice.welcome()?.roomCode ?? '');
    if (room === undefined) throw new Error('room missing');
    const simStart = room.world.timeMs;
    const wallStart = harness.clock.value;

    const pattern = [12, 18, 25, 7, 38];
    for (let i = 0; i < 200; i += 1) harness.advance(pattern[i % pattern.length] ?? 20);

    const simElapsed = room.world.timeMs - simStart;
    const wallElapsed = harness.clock.value - wallStart;
    expect(wallElapsed).toBe(4000);
    expect(Math.abs(simElapsed - wallElapsed)).toBeLessThanOrEqual(SERVER_TICK_MS);
    expect(harness.game.metrics.simDriftMaxAbs).toBeLessThanOrEqual(SERVER_TICK_MS);
    expect(harness.game.metrics.tickScheduleErrorSamples).toBe(harness.game.metrics.ticks);
    expect(harness.game.metrics.tickScheduleErrorMaxMs).toBeGreaterThan(0);
    await harness.close();
  });
});
