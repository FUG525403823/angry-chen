import { describe, expect, it } from 'vitest';

import {
  ERROR_CODE,
  LIMITS,
  MATCH_PHASE,
  NEW_ROOM_CODE,
  OPCODE,
  createCommand,
  encodeChat,
  encodeCommand,
  encodeInteract,
  encodeReady,
  getEntity,
} from '@ac/shared';

import { createHarness, sendPing, sendSimple } from './testing/harness.ts';

const scratch = new Uint8Array(LIMITS.maxFrameBytes);

describe('对局流程', () => {
  it('非房主 startMatch 被拒，房主触发 intermission 并在倒计时后进入 playing', async () => {
    const harness = createHarness();
    const host = harness.connect();
    const guest = harness.connect();
    host.join('host', NEW_ROOM_CODE);
    const code = host.welcome()?.roomCode ?? '';
    guest.join('guest', code);

    sendSimple(guest, OPCODE.startMatch);
    expect(guest.errorCodes()).toContain(ERROR_CODE.notHost);
    expect(host.matchState()?.phase).toBe(MATCH_PHASE.lobby);

    sendSimple(host, OPCODE.startMatch);
    expect(host.matchState()?.phase).toBe(MATCH_PHASE.intermission);
    expect(host.matchState()?.intermissionMs).toBe(5000);

    harness.advance(5000);
    expect(host.matchState()?.phase).toBe(MATCH_PHASE.playing);
    expect(host.matchState()?.wave).toBe(1);

    const late = harness.connect();
    late.join('late', code);
    expect(late.errorCodes()).toContain(ERROR_CODE.matchInProgress);
    await harness.close();
  });

  it('ready 与武器选择写入 MatchState 广播', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const size = encodeReady({ ready: true, weapon: 1 }, scratch);
    alice.send(scratch.subarray(0, size));

    const state = alice.matchState();
    expect(state?.players.length).toBe(1);
    expect(state?.players[0]?.ready).toBe(true);
    expect(state?.players[0]?.weapon).toBe(1);
    expect(state?.players[0]?.name).toBe('alice');
    expect(state?.players[0]?.hpRatio).toBeCloseTo(1, 2);
    await harness.close();
  });

  it('ping 回显 clientTimeMs 并返回快照速率', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    sendPing(alice, 4242, 0);
    const pong = alice.pong();
    expect(pong?.clientTimeMs).toBe(4242);
    expect(pong?.snapshotRateX10).toBe(200);
    expect(harness.game.metrics.framesIn).toBe(2);
    await harness.close();
  });

  it('chat 净化计数、interact 与 respawn 计入指标', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);

    let size = encodeChat('hello' + String.fromCharCode(7), scratch);
    alice.send(scratch.subarray(0, size));
    size = encodeInteract(1, scratch);
    alice.send(scratch.subarray(0, size));
    sendSimple(alice, OPCODE.respawn);

    expect(harness.game.metrics.chatMessages).toBe(1);
    expect(harness.game.metrics.interactRequests).toBe(1);
    expect(harness.game.metrics.respawnRequests).toBe(1);
    await harness.close();
  });

  it('输入命令驱动权威模拟，无命令时玩家静止', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const room = harness.game.rooms.rooms.get(code);
    expect(room).toBeDefined();
    if (room === undefined) return;

    const player = getEntity(room.world, 1);
    expect(player).toBeDefined();
    if (player === undefined) return;
    const startX = player.pos.x;
    const startZ = player.pos.z;

    harness.tick(10);
    expect(player.pos.x).toBeCloseTo(startX, 5);
    expect(player.pos.z).toBeCloseTo(startZ, 5);

    const command = createCommand();
    command.seq = 1;
    command.moveX = 1;
    const size = encodeCommand(command, scratch);
    alice.send(scratch.subarray(0, size));
    harness.tick(20);
    expect(player.pos.z).toBeGreaterThan(startZ);
    await harness.close();
  });

  it('追帧上限：一次 1000ms 推进最多执行 5 tick，其余计入 tickSkips', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const room = harness.game.rooms.rooms.get(code);
    expect(room).toBeDefined();
    if (room === undefined) return;
    const before = room.world.tick;

    harness.advance(1000);
    expect(room.world.tick - before).toBe(LIMITS.tickCatchUpLimit);
    expect(harness.game.metrics.tickSkips).toBe(15);
    expect(harness.game.metrics.ticks).toBe(LIMITS.tickCatchUpLimit);
    await harness.close();
  });

  it('空房间不推进模拟（无人在线不烧 CPU）', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const room = harness.game.rooms.rooms.get(code);
    expect(room).toBeDefined();
    if (room === undefined) return;
    sendSimple(alice, OPCODE.leave);
    harness.advance(500);
    expect(room.world.tick).toBe(0);
    expect(harness.game.metrics.ticks).toBe(0);
    await harness.close();
  });
});
