import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import {
  ERROR_CODE,
  LIMITS,
  MATCH_PHASE,
  NEW_ROOM_CODE,
  OPCODE,
  decodeChatMessage,
  encodeChat,
  encodeReady,
  getEntity,
  markDowned,
} from '@ac/shared';

import { createHarness, sendSimple, type TestClient } from '../testing/harness.ts';
import { createJsonMatchStore } from './store.ts';

const scratch = new Uint8Array(LIMITS.maxFrameBytes);

function ready(client: TestClient, weapon: number): void {
  const size = encodeReady({ ready: true, weapon }, scratch);
  client.send(scratch.subarray(0, size));
}

async function tempDir(prefix: string): Promise<string> {
  return mkdtemp(join(tmpdir(), prefix));
}

describe('对局生命周期', () => {
  it('2 客户端：开始 → wave1 清空 → 波间跳过 → wave2 → 一人离开 → 结算落盘', async () => {
    const dir = await tempDir('ac-life-');
    const store = createJsonMatchStore({ dir });
    await store.load();
    const harness = createHarness({ store });

    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const bob = harness.connect();
    bob.join('bob', code);

    ready(alice, 1);
    ready(bob, 2);
    sendSimple(alice, OPCODE.startMatch);
    expect(alice.matchState()?.phase).toBe(MATCH_PHASE.loading);
    harness.advance(1500);
    expect(alice.matchState()?.phase).toBe(MATCH_PHASE.playing);
    expect(alice.matchState()?.wave).toBe(1);

    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');

    room.director.wave = room.wave;
    room.director.planned = 0;
    room.director.spawned = 0;
    room.director.finished = false;
    harness.tick(1);
    expect(room.phase).toBe(MATCH_PHASE.intermission);
    expect(room.intermissionMs).toBeGreaterThan(0);

    harness.advance(5000);
    expect(room.phase).toBe(MATCH_PHASE.playing);
    expect(room.wave).toBe(2);

    sendSimple(bob, OPCODE.leave);
    expect(room.sessions.length).toBe(1);
    harness.tick(1);
    expect(room.phase).toBe(MATCH_PHASE.playing);

    const aliceEntity = getEntity(room.world, 1);
    if (aliceEntity === undefined) throw new Error('alice entity missing');
    aliceEntity.hp = 0;
    markDowned(aliceEntity.combat.downed, room.world.timeMs);
    harness.tick(1);
    expect(room.phase).toBe(MATCH_PHASE.ended);

    await store.flush();
    const recent = await store.getRecentMatches(5);
    expect(recent.length).toBe(1);
    expect(recent[0]?.waveReached).toBe(2);
    expect(recent[0]?.winnerTeam).toBe(1);
    expect(recent[0]?.playerCount).toBe(2);
    expect(recent[0]?.players.find((player) => player.name === 'bob')?.leftMidMatch).toBe(true);
    expect(recent[0]?.players.find((player) => player.name === 'alice')?.leftMidMatch).toBe(false);

    await harness.close();
    await rm(dir, { recursive: true, force: true });
  });

  it('宽限期 10 秒内重连：复用 pid 与实体，生命恢复到至少 50%', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const pid = alice.welcome()?.pid ?? 0;
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');
    const entity = getEntity(room.world, pid);
    if (entity === undefined) throw new Error('entity missing');
    entity.hp = 20;

    alice.disconnect();
    expect(room.sessions.length).toBe(1);
    expect(room.sessions[0]?.disconnectedAtMs).not.toBeNull();
    expect(getEntity(room.world, pid)?.idle).toBe(true);

    harness.advance(10000);
    expect(room.sessions.length).toBe(1);
    expect(getEntity(room.world, pid)?.active).toBe(true);

    const reconnected = harness.connect();
    reconnected.join('alice', code);
    expect(reconnected.welcome()?.pid).toBe(pid);
    expect(room.sessions.length).toBe(1);
    expect(room.sessions[0]?.disconnectedAtMs).toBeNull();
    const restored = getEntity(room.world, pid);
    expect(restored?.idle).toBe(false);
    expect(restored?.hp).toBeGreaterThanOrEqual(50);
    await harness.close();
  });

  it('宽限期 31 秒超时：回收实体并释放名额', async () => {
    const harness = createHarness();
    const carol = harness.connect();
    carol.join('carol', NEW_ROOM_CODE);
    const code = carol.welcome()?.roomCode ?? '';
    const pid = carol.welcome()?.pid ?? 0;
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');

    carol.disconnect();
    harness.advance(31000);
    expect(room.sessions.length).toBe(0);
    expect(getEntity(room.world, pid)?.active).toBe(false);

    const dave = harness.connect();
    dave.join('dave', code);
    expect(dave.welcome()).toBeDefined();
    expect(room.sessions.length).toBe(1);
    await harness.close();
  });

  it('房主转移给最小存活 id，非房主 StartMatch 返回错误码 9', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const bob = harness.connect();
    bob.join('bob', code);
    const carol = harness.connect();
    carol.join('carol', code);
    const room = harness.game.rooms.rooms.get(code);
    if (room === undefined) throw new Error('room missing');
    expect(room.match.hostId).toBe(1);

    sendSimple(carol, OPCODE.startMatch);
    expect(carol.errorCodes()).toContain(ERROR_CODE.notHost);
    expect(room.phase).toBe(MATCH_PHASE.lobby);

    sendSimple(alice, OPCODE.leave);
    expect(room.match.hostId).toBe(2);

    ready(bob, 1);
    ready(carol, 2);
    sendSimple(bob, OPCODE.startMatch);
    expect(room.phase).toBe(MATCH_PHASE.loading);
    await harness.close();
  });

  it('chat 校验后广播给同房间所有在线连接', async () => {
    const harness = createHarness();
    const alice = harness.connect();
    alice.join('alice', NEW_ROOM_CODE);
    const code = alice.welcome()?.roomCode ?? '';
    const bob = harness.connect();
    bob.join('bob', code);

    const size = encodeChat('你好', scratch);
    alice.send(scratch.subarray(0, size));
    const frames = bob.framesOf(0x87);
    expect(frames.length).toBe(1);
    const decoded = decodeChatMessage(frames[0] ?? new Uint8Array());
    expect(decoded.ok).toBe(true);
    if (decoded.ok) {
      expect(decoded.value.pid).toBe(1);
      expect(decoded.value.text).toBe('你好');
    }
    await harness.close();
  });
});
