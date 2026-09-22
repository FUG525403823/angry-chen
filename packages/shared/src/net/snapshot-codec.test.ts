import { describe, expect, it } from 'vitest';

import { MAX_ENTITIES } from '../config/index.ts';
import { createSnapshot, type Snapshot } from '../snapshot.ts';
import { createWorld, despawnEntity, getEntity, spawnEntity, type World } from '../world.ts';
import { snapshotWorld } from '../snapshot.ts';
import { stepWorld } from '../sim.ts';
import type { SnapshotMirror } from './codec.ts';
import {
  LIMITS,
  OPCODE,
  SNAPSHOT_HEADER_BYTES,
  SNAPSHOT_RECORD_BYTES,
  baselinePresentIdsSorted,
  createSnapshotBaseline,
  mirrorIdsSorted,
  createSnapshotMirror,
  dequantizeAngle,
  dequantizePosition,
  decodeSnapshot,
  encodeSnapshot,
  quantizeSnapshotEntity,
  readSnapshotRecord,
} from '../index.ts';

const buffer = new Uint8Array(LIMITS.maxFrameBytes);
const record = new Uint8Array(SNAPSHOT_RECORD_BYTES);

/** O05：mirror.ids 现在是「Uint16Array + idCount」的紧凑列表，测试统一用这个取数。 */
function mirrorIdList(mirror: SnapshotMirror): number[] {
  return Array.from(mirror.ids.subarray(0, mirror.idCount));
}

function expectMirrorMatches(mirror: SnapshotMirror, snapshot: Snapshot): void {
  const ids = snapshot.entities.map((entity) => entity.id);
  expect(mirrorIdList(mirror)).toEqual(ids);
  for (const entity of snapshot.entities) {
    quantizeSnapshotEntity(entity, record, 0);
    const base = entity.id * SNAPSHOT_RECORD_BYTES;
    for (let i = 0; i < SNAPSHOT_RECORD_BYTES; i += 1) {
      expect(mirror.bytes[base + i]).toBe(record[i]);
    }
  }
}

function push(
  world: World,
  snapshot: Snapshot,
  baseline: ReturnType<typeof createSnapshotBaseline>,
  mirror: SnapshotMirror,
  forceFull = false,
): number {
  snapshotWorld(world, snapshot);
  const size = encodeSnapshot(snapshot, baseline, 0, buffer, forceFull);
  const decoded = decodeSnapshot(buffer.subarray(0, size), mirror);
  if (!decoded.ok) throw new Error('decode failed');
  return size;
}

describe('快照差分编码', () => {
  it('首帧下发全部实体，客户端镜像与服务器投影逐字节一致', () => {
    const world = createWorld(1001);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();

    stepWorld(world, [], 50);
    snapshotWorld(world, snapshot);
    const size = encodeSnapshot(snapshot, baseline, 0, buffer);
    expect(decodeSnapshot(buffer.subarray(0, size), mirror).ok).toBe(true);
    expectMirrorMatches(mirror, snapshot);
    expect(mirror.baselineTick).toBe(0);
    expect(mirror.recordCount).toBe(4);
  });

  it('晚到的旧快照（tick 更小）被丢弃，镜像保持最新状态', () => {
    const world = createWorld(21);
    const mirror = createSnapshotMirror();
    const snapshot = createSnapshot();
    const staleSnapshot = createSnapshot();
    const staleOut = new Uint8Array(LIMITS.maxFrameBytes);
    const staleBaseline = createSnapshotBaseline();

    stepWorld(world, [], 50);
    snapshotWorld(world, staleSnapshot);
    const staleSize = encodeSnapshot(staleSnapshot, staleBaseline, 0, staleOut, true);

    stepWorld(world, [], 50);
    snapshotWorld(world, snapshot);
    const baseline = createSnapshotBaseline();
    const size = encodeSnapshot(snapshot, baseline, 0, buffer, true);
    expect(decodeSnapshot(buffer.subarray(0, size), mirror).ok).toBe(true);
    const tickAfter = mirror.tick;
    const idsAfter = mirrorIdList(mirror);

    const result = decodeSnapshot(staleOut.subarray(0, staleSize), mirror);
    expect(result.ok).toBe(true);
    expect(mirror.tick).toBe(tickAfter);
    expect(mirrorIdList(mirror)).toEqual(idsAfter);
  });

  it('未变化的实体不重复下发，差分帧显著更小', () => {
    const world = createWorld(1002);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    stepWorld(world, [], 50);
    const first = push(world, snapshot, baseline, mirror);

    const idle = push(world, snapshot, baseline, mirror);
    expect(idle).toBeLessThan(first);
    expect(mirror.recordCount).toBe(0);
    expect(mirror.removedCount).toBe(0);
    expect(mirror.baselineTick).toBe(baseline.tick);
    expectMirrorMatches(mirror, snapshot);
  });

  it('50 tick 属性测试：每帧镜像都等于服务器投影', () => {
    const world = createWorld(1003);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    for (let i = 0; i < 12; i += 1) spawnEntity(world, 'sheep', (i % 4) * 3 - 5, 0, 10 + i);

    let totalBytes = 0;
    for (let tick = 0; tick < 50; tick += 1) {
      for (let id = 1; id <= 6; id += 1) {
        const entity = getEntity(world, id);
        if (entity !== undefined && entity.active) entity.yaw = tick * 0.1 + id;
      }
      if (tick === 20) {
        const sheep = world.activeIds[world.activeIds.length - 1];
        if (sheep !== undefined) despawnEntity(world, sheep);
      }
      if (tick === 30) spawnEntity(world, 'sheep', 2, 0, -3);
      stepWorld(world, [], 50);
      totalBytes += push(world, snapshot, baseline, mirror);
    }
    expectMirrorMatches(mirror, snapshot);
    expect(totalBytes).toBeGreaterThan(0);
  });

  it('实体消失时下发 removedId，镜像同步删除', () => {
    const world = createWorld(1004);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    const spawned = spawnEntity(world, 'sheep', 1, 0, 1);
    if (!spawned.ok) throw new Error('spawn failed');
    stepWorld(world, [], 50);
    push(world, snapshot, baseline, mirror);
    expect(mirrorIdList(mirror)).toContain(spawned.id);

    despawnEntity(world, spawned.id);
    snapshotWorld(world, snapshot);
    const size = encodeSnapshot(snapshot, baseline, 0, buffer);
    expect(decodeSnapshot(buffer.subarray(0, size), mirror).ok).toBe(true);
    expect(mirror.removedCount).toBe(1);
    expect(mirrorIdList(mirror)).not.toContain(spawned.id);
    expectMirrorMatches(mirror, snapshot);
  });

  it('强制全量（baselineTick = 0）会清空客户端残留实体', () => {
    const world = createWorld(1005);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    stepWorld(world, [], 50);
    push(world, snapshot, baseline, mirror);

    mirror.present[900] = 1;
    mirror.ids[mirror.idCount] = 900;
    mirror.idCount += 1;
    push(world, snapshot, baseline, mirror, true);
    expect(mirror.baselineTick).toBe(0);
    expect(mirrorIdList(mirror)).not.toContain(900);
    expectMirrorMatches(mirror, snapshot);
  });

  it('超过 255 条记录时拆到后续帧，镜像最终收敛', () => {
    const world = createWorld(1006);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    for (let i = 0; i < 300; i += 1) {
      spawnEntity(world, 'sheep', (i % 20) - 10, 0, 30 + i * 0.05);
    }
    stepWorld(world, [], 50);
    snapshotWorld(world, snapshot);
    expect(snapshot.entities.length).toBe(LIMITS.snapshotMaxEntities);

    const firstSize = encodeSnapshot(snapshot, baseline, 0, buffer);
    const first = decodeSnapshot(buffer.subarray(0, firstSize), mirror);
    if (!first.ok) throw new Error('decode failed');
    expect(first.value.recordCount).toBe(LIMITS.maxSnapshotRecordsPerFrame);
    expect(first.value.idCount).toBe(LIMITS.maxSnapshotRecordsPerFrame);

    const secondSize = encodeSnapshot(snapshot, baseline, 0, buffer);
    const second = decodeSnapshot(buffer.subarray(0, secondSize), mirror);
    if (!second.ok) throw new Error('decode failed');
    expect(second.value.recordCount).toBe(1);
    expectMirrorMatches(mirror, snapshot);
  });

  it('量化误差不超过半厘米，位置与角度可还原', () => {
    const world = createWorld(1007);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    const player = getEntity(world, 1);
    if (player === undefined) throw new Error('missing player');
    player.pos.x = 12.345;
    player.yaw = 1.234;
    stepWorld(world, [], 50);
    snapshotWorld(world, snapshot);
    const size = encodeSnapshot(snapshot, baseline, 0, buffer);
    if (!decodeSnapshot(buffer.subarray(0, size), mirror).ok) throw new Error('decode failed');

    const first = readSnapshotRecord(mirror.bytes, SNAPSHOT_RECORD_BYTES, mirror.record);
    expect(dequantizePosition(first.xCm)).toBeCloseTo(12.345, 2);
    expect(dequantizeAngle(first.yawUnits)).toBeCloseTo(1.234, 3);
    expect(first.kind).toBe('player');
  });

  it('单帧删除数超过上限时延后到下一帧', () => {
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    for (let i = 1; i <= 260; i += 1) {
      snapshot.entities.push({
        id: i,
        kind: 'sheep',
        flags: 0,
        x: 0,
        y: 0,
        z: 0,
        yaw: 0,
        pitch: 0,
        hpRatio: 1,
        state: 0,
      });
    }
    snapshot.tick = 1;
    let size = encodeSnapshot(snapshot, baseline, 0, buffer);
    if (!decodeSnapshot(buffer.subarray(0, size), mirror).ok) throw new Error('decode failed');
    size = encodeSnapshot(snapshot, baseline, 0, buffer);
    if (!decodeSnapshot(buffer.subarray(0, size), mirror).ok) throw new Error('decode failed');
    expect(mirror.idCount).toBe(260);

    snapshot.tick = 2;
    snapshot.entities.length = 0;
    size = encodeSnapshot(snapshot, baseline, 0, buffer);
    const first = decodeSnapshot(buffer.subarray(0, size), mirror);
    if (!first.ok) throw new Error('decode failed');
    expect(first.value.removedCount).toBe(LIMITS.maxRemovedPerFrame);
    expect(mirror.idCount).toBe(5);

    size = encodeSnapshot(snapshot, baseline, 0, buffer);
    const second = decodeSnapshot(buffer.subarray(0, size), mirror);
    if (!second.ok) throw new Error('decode failed');
    expect(second.value.removedCount).toBe(5);
    expect(mirror.idCount).toBe(0);
  });

  it('恶意快照帧不会抛异常', () => {
    const mirror = createSnapshotMirror();
    expect(decodeSnapshot(new Uint8Array([OPCODE.snapshot, 1, 2, 3]), mirror)).toEqual({
      ok: false,
      reason: 'truncated',
    });

    const badOpcode = new Uint8Array([
      OPCODE.welcome,
      1,
      2,
      3,
      4,
      5,
      6,
      7,
      8,
      9,
      10,
      11,
      12,
      13,
      14,
      15,
    ]);
    expect(decodeSnapshot(badOpcode, mirror)).toEqual({ ok: false, reason: 'bad-opcode' });

    const frame = new Uint8Array(LIMITS.maxFrameBytes);
    frame[0] = OPCODE.snapshot;
    frame[15] = 1;
    frame[16] = 255;
    frame[17] = 255;
    expect(decodeSnapshot(frame.subarray(0, 32), mirror)).toEqual({
      ok: false,
      reason: 'bad-value',
    });

    const world = createWorld(1008);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    snapshotWorld(world, snapshot);
    const size = encodeSnapshot(snapshot, baseline, 0, frame);
    const extra = new Uint8Array(size + 1);
    extra.set(frame.subarray(0, size));
    expect(decodeSnapshot(extra, mirror)).toEqual({ ok: false, reason: 'bad-length' });
  });

  it('实体 id 超过容量时编码端跳过', () => {
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    snapshot.entities.push({
      id: MAX_ENTITIES + 5,
      kind: 'sheep',
      flags: 0,
      x: 0,
      y: 0,
      z: 0,
      yaw: 0,
      pitch: 0,
      hpRatio: 1,
      state: 0,
    });
    const size = encodeSnapshot(snapshot, baseline, 0, buffer);
    expect(size).toBe(17);
  });

  it('O05：无增删的稳态帧只发差分（0 记录 / 0 删除），紧凑列表不变', () => {
    const world = createWorld(1007);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    stepWorld(world, [], 50);
    push(world, snapshot, baseline, mirror);
    const firstCount = baseline.presentCount;
    expect(firstCount).toBeGreaterThan(0);
    expect(baselinePresentIdsSorted(baseline)).toBe(true);

    const size = push(world, snapshot, baseline, mirror);
    expect(size).toBe(SNAPSHOT_HEADER_BYTES + 1);
    expect(buffer[SNAPSHOT_HEADER_BYTES - 1]).toBe(0);
    expect(buffer[SNAPSHOT_HEADER_BYTES]).toBe(0);
    expect(baseline.presentCount).toBe(firstCount);
    expect(baselinePresentIdsSorted(baseline)).toBe(true);
    expect(mirrorIdsSorted(mirror)).toBe(true);
    expect(mirror.idCount).toBe(firstCount);
    expectMirrorMatches(mirror, snapshot);
  });

  it('O05：删除列表升序且只含实际删除的 id', () => {
    const world = createWorld(1008);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    stepWorld(world, [], 50);
    for (let i = 0; i < 12; i += 1) {
      spawnEntity(world, 'sheep', i - 6, 0, 20 + i * 0.1);
    }
    push(world, snapshot, baseline, mirror);

    // 删掉中间三个 id（不是最大三个），删除列表必须是升序
    const victims = world.activeIds.filter((id) => id > 4).slice(1, 4);
    expect(victims.length).toBe(3);
    for (const id of victims) despawnEntity(world, id);
    push(world, snapshot, baseline, mirror);

    const count = buffer[SNAPSHOT_HEADER_BYTES - 1] ?? 0;
    const removedOffset = SNAPSHOT_HEADER_BYTES + count * SNAPSHOT_RECORD_BYTES;
    const removedCount = buffer[removedOffset] ?? 0;
    const removed: number[] = [];
    for (let i = 0; i < removedCount; i += 1) {
      const lo = buffer[removedOffset + 1 + i * 2] ?? 0;
      const hi = buffer[removedOffset + 2 + i * 2] ?? 0;
      removed.push(lo | (hi << 8));
    }
    expect(removed).toEqual([...victims].sort((a, b) => a - b));
    expect(mirrorIdList(mirror)).toEqual(snapshot.entities.map((entity) => entity.id));
    expect(baselinePresentIdsSorted(baseline)).toBe(true);
    expect(mirrorIdsSorted(mirror)).toBe(true);
  });

  it('O05：spawn/despawn 交替 200 次后两侧紧凑列表仍严格升序、无重复', () => {
    const world = createWorld(1009);
    const snapshot = createSnapshot();
    const baseline = createSnapshotBaseline();
    const mirror = createSnapshotMirror();
    stepWorld(world, [], 50);
    push(world, snapshot, baseline, mirror);

    for (let i = 0; i < 200; i += 1) {
      const spawned = spawnEntity(world, 'sheep', (i % 10) - 5, 0, 22 + (i % 5) * 0.2);
      expect(spawned.ok).toBe(true);
      push(world, snapshot, baseline, mirror);
      expect(baselinePresentIdsSorted(baseline)).toBe(true);
      expect(mirrorIdsSorted(mirror)).toBe(true);
      expect(mirror.idCount).toBe(baseline.presentCount);

      if (spawned.ok) despawnEntity(world, spawned.id);
      push(world, snapshot, baseline, mirror);
      expect(baselinePresentIdsSorted(baseline)).toBe(true);
      expect(mirrorIdsSorted(mirror)).toBe(true);
      expect(mirror.idCount).toBe(baseline.presentCount);
    }
    expectMirrorMatches(mirror, snapshot);
  });
});
