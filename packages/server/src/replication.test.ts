import { describe, expect, it } from 'vitest';

import {
  ERROR_CODE,
  LIMITS,
  NEW_ROOM_CODE,
  OPCODE,
  SNAPSHOT_RECORD_BYTES,
  createCommand,
  despawnEntity,
  encodeCommand,
  getEntity,
  quantizeSnapshotEntity,
  spawnEntity,
  type SnapshotMirror,
} from '@ac/shared';

import { createHarness, sendPing } from './testing/harness.ts';
import type { TestClient } from './testing/harness.ts';
import type { Room } from './room.ts';

const record = new Uint8Array(SNAPSHOT_RECORD_BYTES);

function expectMirrorMatchesServer(mirror: SnapshotMirror, room: Room): void {
  const ids = room.snapshot.entities.map((entity) => entity.id);
  expect(mirror.ids).toEqual(ids);
  for (const entity of room.snapshot.entities) {
    quantizeSnapshotEntity(entity, record, 0);
    const base = entity.id * SNAPSHOT_RECORD_BYTES;
    for (let i = 0; i < SNAPSHOT_RECORD_BYTES; i += 1) {
      if (mirror.bytes[base + i] !== record[i]) {
        throw new Error('mirror mismatch id=' + String(entity.id) + ' byte=' + String(i));
      }
    }
  }
}

function joinRoom(
  harness: ReturnType<typeof createHarness>,
  name: string,
): { client: TestClient; room: Room } {
  const client = harness.connect();
  client.join(name, NEW_ROOM_CODE);
  const code = client.welcome()?.roomCode ?? '';
  const room = harness.game.rooms.rooms.get(code);
  if (room === undefined) throw new Error('room missing');
  return { client, room };
}

describe('快照差分复制', () => {
  it('两个客户端在同一 tick 上重建出同一个世界', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    const b = harness.connect();
    b.join('bob', a.room.code);

    harness.tick(30);
    expect(a.client.applySnapshots()).toBe(30);
    expect(b.applySnapshots()).toBe(30);

    expect(a.client.snapshotTicks()).toEqual(b.snapshotTicks());
    expect(a.client.mirror.ids).toEqual(b.mirror.ids);
    expect(a.client.mirror.bytes).toEqual(b.mirror.bytes);
    expectMirrorMatchesServer(a.client.mirror, a.room);
    expectMirrorMatchesServer(b.mirror, a.room);
    await harness.close();
  });

  it('逐帧差分：60 帧内每帧镜像都等于服务器投影，含新增与删除', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    const zeroBytes = a.client.mirror.bytes.length;

    for (let tick = 0; tick < 60; tick += 1) {
      if (tick === 5) spawnEntity(a.room.world, 'sheep', 0, 0, 12);
      if (tick === 9) spawnEntity(a.room.world, 'sheep', 3, 0, 14);
      if (tick === 20) {
        const sheep = a.room.world.entities.find(
          (entity) => entity.active && entity.kind === 'sheep',
        );
        if (sheep !== undefined) despawnEntity(a.room.world, sheep.id);
      }
      const player = getEntity(a.room.world, 1);
      if (player !== undefined) player.yaw += 0.25;
      harness.tick(1);
      expect(a.client.applySnapshots()).toBe(1);
      expectMirrorMatchesServer(a.client.mirror, a.room);
    }
    expect(a.client.mirror.bytes.length).toBe(zeroBytes);
    expect(a.client.mirror.ids.length).toBeGreaterThanOrEqual(1);
    await harness.close();
  });

  it('新会话的首帧是全量，无需历史即可重建', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    spawnEntity(a.room.world, 'sheep', 1, 0, 20);
    harness.tick(40);
    a.client.applySnapshots();

    const late = harness.connect();
    late.join('late', a.room.code);
    harness.tick(1);
    expect(late.applySnapshots()).toBe(1);
    expect(late.mirror.baselineTick).toBe(0);
    expectMirrorMatchesServer(late.mirror, a.room);

    harness.tick(40);
    late.applySnapshots();
    expectMirrorMatchesServer(late.mirror, a.room);
    await harness.close();
  });

  it('实体数上限：超过 256 时快照被截断且每帧记录数不超过上限', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    for (let i = 0; i < 300; i += 1)
      spawnEntity(a.room.world, 'sheep', (i % 30) - 15, 0, 20 + (i % 15));
    harness.tick(1);
    expect(a.room.snapshot.truncated).toBe(true);
    expect(a.room.snapshot.entities.length).toBe(LIMITS.snapshotMaxEntities);

    a.client.applySnapshots();
    expect(a.client.mirror.ids.length).toBeLessThanOrEqual(LIMITS.snapshotMaxEntities);
    harness.tick(1);
    a.client.applySnapshots();
    expectMirrorMatchesServer(a.client.mirror, a.room);
    expect(harness.game.metrics.snapshotsSent).toBe(2);
    await harness.close();
  });
});

describe('滥用防护', () => {
  it('未知 opcode：Error(5)、记入 malformedFrames、连接存活', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    a.client.send(new Uint8Array([0x7f, 1, 2, 3]));

    expect(harness.game.metrics.malformedFrames).toBe(1);
    expect(a.client.errorCodes()).toContain(ERROR_CODE.malformedFrame);
    expect(a.client.client.closed).toBe(false);

    sendPing(a.client, 7, 0);
    expect(a.client.pong()?.clientTimeMs).toBe(7);
    await harness.close();
  });

  it('超过 8KB 的帧被传输层拒绝并断开', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    const oversized = new Uint8Array(LIMITS.maxFrameBytes + 1);
    oversized[0] = OPCODE.ping;
    a.client.send(oversized);

    expect(harness.transport.oversizedFrames).toBe(1);
    expect(a.client.client.closed).toBe(true);
    expect(harness.game.sessionCount).toBe(0);
    await harness.close();
  });

  it('每会话保留最近 3 条命令，按 seq 丢弃更旧的命令', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    const buffer = new Uint8Array(LIMITS.maxFrameBytes);
    const command = createCommand();
    const room = [...harness.game.rooms.rooms.values()][0];
    const session = room?.sessions[0];
    const send = (seq: number, moveX: number): void => {
      command.seq = seq;
      command.moveX = moveX;
      a.client.send(buffer.subarray(0, encodeCommand(command, buffer)));
    };

    for (const seq of [5, 2, 4]) send(seq, seq / 10);
    expect(session?.queue.map((entry) => entry.seq)).toEqual([2, 4, 5]);
    expect(session?.command.seq).toBe(5);
    expect(session?.command.moveX).toBeCloseTo(0.5, 2);

    for (const seq of [6, 7, 8, 9]) send(seq, seq / 10);
    expect(session?.queueCount).toBe(3);
    expect(session?.queue.map((entry) => entry.seq)).toEqual([7, 8, 9]);
    expect(session?.command.seq).toBe(9);

    send(1, 0.1);
    expect(session?.queue.map((entry) => entry.seq)).toEqual([7, 8, 9]);
    expect(session?.command.seq).toBe(9);
    await harness.close();
  });

  it('限流是 1 秒滑窗：窗口内第 61 帧被丢弃，窗口滑过后恢复', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    for (let i = 0; i < 59; i += 1) sendPing(a.client, i, 0);
    expect(harness.game.metrics.rateLimitedFrames).toBe(0);
    sendPing(a.client, 99, 0);
    expect(harness.game.metrics.rateLimitedFrames).toBe(1);
    expect(a.client.client.closed).toBe(false);
    harness.advance(1000);
    sendPing(a.client, 100, 0);
    expect(harness.game.metrics.rateLimitedFrames).toBe(1);
    expect(a.client.client.closed).toBe(false);
    await harness.close();
  });

  it('限流：每秒 60 帧上限，累计 3 次超限后 Error(4) 并断开', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');

    // Join 帧同样占用限流窗口，因此本秒内前 60 帧为 Join + clientTimeMs 0..58
    for (let i = 0; i < 70; i += 1) sendPing(a.client, i, 0);
    expect(harness.game.metrics.rateLimitedFrames).toBeGreaterThanOrEqual(3);
    expect(a.client.errorCodes()).toContain(ERROR_CODE.rateLimited);
    expect(a.client.client.closed).toBe(true);
    expect(a.client.pong()?.clientTimeMs).toBe(LIMITS.maxMessagesPerSecond - 2);
    await harness.close();
  });

  it('错序 seq 不回退已确认序号，快照里带最新 ack', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    const buffer = new Uint8Array(LIMITS.maxFrameBytes);
    const command = createCommand();
    for (const seq of [5, 3, 1]) {
      command.seq = seq;
      const size = encodeCommand(command, buffer);
      a.client.send(buffer.subarray(0, size));
    }
    harness.tick(1);
    a.client.applySnapshots();
    expect(a.client.mirror.lastAckedSeq).toBe(5);
    await harness.close();
  });

  it('限流窗口每秒重置，正常流量不受影响', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    for (let round = 0; round < 5; round += 1) {
      for (let i = 0; i < 20; i += 1) sendPing(a.client, i, 0);
      harness.advance(1000);
    }
    expect(harness.game.metrics.rateLimitedFrames).toBe(0);
    expect(harness.game.metrics.malformedFrames).toBe(0);
    expect(harness.game.metrics.framesIn).toBe(101);
    await harness.close();
  });

  it('空帧与截断帧被拒绝但不崩溃', async () => {
    const harness = createHarness();
    const a = joinRoom(harness, 'alice');
    a.client.send(new Uint8Array(0));
    a.client.send(new Uint8Array([OPCODE.inputCmd, 1, 2]));
    expect(harness.game.metrics.malformedFrames).toBeGreaterThanOrEqual(1);
    const before = harness.game.sessionCount;
    expect(before).toBe(1);
    await harness.close();
  });
});
