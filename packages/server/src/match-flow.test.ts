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
  it('非房主 startMatch 被拒，房主经 1.5s loading 进入 playing', async () => {
    const harness = createHarness();
    const host = harness.connect();
    const guest = harness.connect();
    host.join('host', NEW_ROOM_CODE);
    const code = host.welcome()?.roomCode ?? '';
    guest.join('guest', code);

    sendSimple(guest, OPCODE.startMatch);
    expect(guest.errorCodes()).toContain(ERROR_CODE.notHost);
    expect(host.matchState()?.phase).toBe(MATCH_PHASE.lobby);

    let size = encodeReady({ ready: true, weapon: 1 }, scratch);
    host.send(scratch.subarray(0, size));
    size = encodeReady({ ready: true, weapon: 2 }, scratch);
    guest.send(scratch.subarray(0, size));

    sendSimple(host, OPCODE.startMatch);
    expect(host.matchState()?.phase).toBe(MATCH_PHASE.loading);
    expect(host.matchState()?.intermissionMs).toBe(1500);

    harness.advance(1500);
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

  it('大厅选的武器在开局后仍是所选槽位（不被实体旧槽位回写覆盖）', async () => {
    const harness = createHarness();
    const host = harness.connect();
    host.join('host', NEW_ROOM_CODE);
    const code = host.welcome()?.roomCode ?? '';
    const pid = host.welcome()?.pid ?? 0;
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');

    const size = encodeReady({ ready: true, weapon: 1 }, scratch);
    host.send(scratch.subarray(0, size));
    expect(host.matchState()?.players[0]?.weapon).toBe(1);

    sendSimple(host, OPCODE.startMatch);
    harness.advance(1600);
    expect(host.matchState()?.phase).toBe(MATCH_PHASE.playing);
    // 旧实现：handleReady 后 broadcastMatchState 用实体旧槽位（0）回写 session.weapon，
    // 抢在应用循环之前，于是大厅选了步枪、开局拿到手枪。
    expect(host.matchState()?.players[0]?.weapon).toBe(1);
    expect(getEntity(room.world, pid)?.weapon.activeSlot).toBe(1);
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

  it('chat 净化计数、interact 计入指标；respawn 已移出白名单被丢弃', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);

    let size = encodeChat('hello' + String.fromCharCode(7), scratch);
    alice.send(scratch.subarray(0, size));
    size = encodeInteract(1, scratch);
    alice.send(scratch.subarray(0, size));

    const malformedBefore = harness.game.metrics.malformedFrames;
    sendSimple(alice, OPCODE.respawn);

    expect(harness.game.metrics.chatMessages).toBe(1);
    expect(harness.game.metrics.interactRequests).toBe(1);
    // O10：respawn 从 CLIENT_OPCODE_LIST 移除后按非法帧丢弃（malformedFrame），废弃字段恒为 0。
    expect(harness.game.metrics.respawnRequests).toBe(0);
    expect(harness.game.metrics.malformedFrames).toBe(malformedBefore + 1);
    expect(alice.errorCodes()).toContain(ERROR_CODE.malformedFrame);
    await harness.close();
  });

  it('leftMidMatch：局中离场为 true、波间离场为 false', async () => {
    const harness = createHarness();

    const host = harness.connect();
    const guest = harness.connect();
    host.join('host', NEW_ROOM_CODE);
    const code = host.welcome()?.roomCode ?? '';
    guest.join('guest', code);
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');

    let size = encodeReady({ ready: true, weapon: 1 }, scratch);
    host.send(scratch.subarray(0, size));
    size = encodeReady({ ready: true, weapon: 2 }, scratch);
    guest.send(scratch.subarray(0, size));
    sendSimple(host, OPCODE.startMatch);
    harness.advance(1600);
    expect(room.phase).toBe(MATCH_PHASE.playing);
    expect(room.match.records.get(2)?.leftMidMatch).toBe(false);

    sendSimple(guest, OPCODE.leave);
    expect(room.match.records.get(2)?.leftMidMatch).toBe(true);

    const host2 = harness.connect();
    const guest2 = harness.connect();
    host2.join('host2', NEW_ROOM_CODE);
    const code2 = host2.welcome()?.roomCode ?? '';
    guest2.join('guest2', code2);
    const room2 = harness.game.rooms.rooms.get(code2);
    if (room2 === undefined) throw new Error('room 2 missing');

    size = encodeReady({ ready: true, weapon: 1 }, scratch);
    host2.send(scratch.subarray(0, size));
    size = encodeReady({ ready: true, weapon: 2 }, scratch);
    guest2.send(scratch.subarray(0, size));
    sendSimple(host2, OPCODE.startMatch);
    harness.advance(1600);
    expect(room2.phase).toBe(MATCH_PHASE.playing);

    // §5 冻结定义把 intermission 也算「局中」，因此「非局中」用例取 loading（赛前）。
    room2.phase = MATCH_PHASE.loading;
    sendSimple(guest2, OPCODE.leave);
    expect(room2.match.records.get(2)?.leftMidMatch).toBe(false);

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
