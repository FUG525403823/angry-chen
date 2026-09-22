import {
  LIMITS,
  countActive,
  createSnapshot,
  createSnapshotBaseline,
  createWorld,
  encodeSnapshot,
  getEntity,
  snapshotWorld,
  spawnEntity,
  stepWorld,
} from '@ac/shared';
import { describe, expect, it } from 'vitest';

import { createSnapshotView } from './state.ts';

function advanceTo(world: ReturnType<typeof createWorld>, timeMs: number): void {
  for (let guard = 0; guard < 200 && world.timeMs < timeMs; guard += 1) stepWorld(world, [], 50);
}

function spawnSheep(world: ReturnType<typeof createWorld>, x = 0): number {
  const spawned = spawnEntity(world, 'sheep', x, 0, 0);
  expect(spawned.ok).toBe(true);
  return spawned.ok ? spawned.id : 0;
}

function encodeFull(
  world: ReturnType<typeof createWorld>,
  out: Uint8Array,
  baseline: ReturnType<typeof createSnapshotBaseline>,
): number {
  const snapshot = createSnapshot();
  snapshotWorld(world, snapshot);
  return encodeSnapshot(snapshot, baseline, 0, out, true);
}

function sampleX(view: ReturnType<typeof createSnapshotView>, id: number): number {
  let value = -1;
  view.forEachVisible((entity) => {
    if (entity.id === id) value = entity.pos.x;
  });
  return value;
}

describe('SnapshotView', () => {
  it('在 100ms 延迟窗口内按 serverTimeMs 线性插值远端实体', () => {
    const world = createWorld(3);
    const sheepId = spawnSheep(world);
    advanceTo(world, 1000);
    const sheep = getEntity(world, sheepId);
    expect(sheep).toBeDefined();
    if (sheep === undefined) return;

    const outA = new Uint8Array(LIMITS.maxFrameBytes);
    const outB = new Uint8Array(LIMITS.maxFrameBytes);
    sheep.pos.x = 0;
    const sizeA = encodeFull(world, outA, createSnapshotBaseline());
    stepWorld(world, [], 50);
    sheep.pos.x = 10;
    const sizeB = encodeFull(world, outB, createSnapshotBaseline());

    let clockMs = 0;
    const view = createSnapshotView({ now: () => clockMs });
    expect(view.applyFrame(outA.subarray(0, sizeA), sizeA)).toBe(true);
    expect(view.applyFrame(outB.subarray(0, sizeB), sizeB)).toBe(true);
    const serverNowAtB = view.getServerTimeMs();

    const samples: number[] = [];
    for (const clock of [0, 25, 50, 75, 100]) {
      clockMs = clock;
      samples.push(sampleX(view, sheepId));
    }
    expect(clockMs).toBe(100);
    expect(view.getServerTimeMs()).toBeGreaterThan(serverNowAtB);

    expect(samples[0]).toBeCloseTo(0, 2);
    for (let i = 1; i < samples.length; i += 1) {
      expect(samples[i] ?? -1).toBeGreaterThanOrEqual(samples[i - 1] ?? -1);
    }
    expect(samples[2] ?? -1).toBeCloseTo(0, 1);
    expect(samples[3] ?? 0).toBeCloseTo(5, 1);
    expect(samples[4] ?? 0).toBeCloseTo(10, 1);
  });

  it('旧 tick 的快照被丢弃，不产生倒退', () => {
    const world = createWorld(4);
    spawnSheep(world, 2);
    advanceTo(world, 500);
    const outA = new Uint8Array(LIMITS.maxFrameBytes);
    const outB = new Uint8Array(LIMITS.maxFrameBytes);
    const sizeA = encodeFull(world, outA, createSnapshotBaseline());
    stepWorld(world, [], 50);
    stepWorld(world, [], 50);
    const sizeB = encodeFull(world, outB, createSnapshotBaseline());

    const view = createSnapshotView({ now: () => 0 });
    expect(view.applyFrame(outA.subarray(0, sizeA), sizeA)).toBe(true);
    expect(view.applyFrame(outB.subarray(0, sizeB), sizeB)).toBe(true);
    const appliedTick = view.getAppliedTick();
    expect(view.applyFrame(outA.subarray(0, sizeA), sizeA)).toBe(false);
    expect(view.getAppliedTick()).toBe(appliedTick);
  });

  it('统计窗口给出快照速率与入站带宽，RTT 由 Pong 采样写入', () => {
    const world = createWorld(5);
    advanceTo(world, 200);
    let clockMs = 0;
    const view = createSnapshotView({ now: () => clockMs });
    const out = new Uint8Array(LIMITS.maxFrameBytes);
    for (let i = 0; i < 12; i += 1) {
      stepWorld(world, [], 50);
      clockMs += 90;
      const size = encodeFull(world, out, createSnapshotBaseline());
      view.applyFrame(out.subarray(0, size), size);
    }
    const stats = view.getStats();
    expect(stats.snapshotsPerSec).toBeGreaterThan(9);
    expect(stats.inboundBytesPerSec).toBeGreaterThan(0);
    view.recordRtt(37.456);
    expect(view.getStats().rttMs).toBe(37.46);
  });

  it('本地玩家按 pid 暴露，未设置时返回 undefined', () => {
    const world = createWorld(6);
    expect(countActive(world, 'player')).toBeGreaterThan(0);
    advanceTo(world, 100);
    const out = new Uint8Array(LIMITS.maxFrameBytes);
    const size = encodeFull(world, out, createSnapshotBaseline());
    const view = createSnapshotView({ now: () => 0 });
    expect(view.getLocalPlayer()).toBeUndefined();
    view.applyFrame(out.subarray(0, size), size);
    expect(view.getLocalPlayer()).toBeUndefined();
    view.setLocalPlayerId(1);
    const local = view.getLocalPlayer();
    expect(local?.id).toBe(1);
    expect(local?.kind).toBe(0);
  });

  it('多帧区间内采样单调不减：插值终点必须落在相邻两帧之间', () => {
    const world = createWorld(7);
    const sheepId = spawnSheep(world, 0);
    advanceTo(world, 400);
    const sheep = getEntity(world, sheepId);
    expect(sheep).toBeDefined();
    if (sheep === undefined) return;

    const frames: Uint8Array[] = [];
    const sizes: number[] = [];
    for (let i = 0; i < 6; i += 1) {
      sheep.pos.x = 1 + i;
      const out = new Uint8Array(LIMITS.maxFrameBytes);
      sizes.push(encodeFull(world, out, createSnapshotBaseline()));
      frames.push(out);
      stepWorld(world, [], 50);
    }

    let clockMs = 0;
    const view = createSnapshotView({ now: () => clockMs });
    for (let i = 0; i < frames.length; i += 1) {
      const frame = frames[i];
      const size = sizes[i] ?? 0;
      if (frame === undefined) continue;
      expect(view.applyFrame(frame.subarray(0, size), size)).toBe(true);
    }

    const samples: number[] = [];
    for (let clock = 0; clock <= 260; clock += 10) {
      clockMs = clock;
      samples.push(sampleX(view, sheepId));
    }
    let backtracks = 0;
    let maxJump = 0;
    for (let i = 1; i < samples.length; i += 1) {
      const previous = samples[i - 1] ?? 0;
      const current = samples[i] ?? 0;
      if (current < previous - 0.01) backtracks += 1;
      maxJump = Math.max(maxJump, Math.abs(current - previous));
    }
    expect(backtracks).toBe(0);
    expect(maxJump).toBeLessThan(0.25);
    expect(samples[samples.length - 1] ?? 0).toBeGreaterThan(samples[0] ?? 0);
  });

  it('O05：插值延迟 = 2 × 到达间隔中位数，钳制在 [100, 250]ms', () => {
    const world = createWorld(41);
    spawnSheep(world, 0);
    advanceTo(world, 100);
    const frame = new Uint8Array(LIMITS.maxFrameBytes);
    let clockMs = 0;
    const view = createSnapshotView({ now: () => clockMs });
    const feed = (count: number, stepMs: number): void => {
      for (let i = 0; i < count; i += 1) {
        stepWorld(world, [], 50);
        const size = encodeFull(world, frame, createSnapshotBaseline());
        clockMs += stepMs;
        expect(view.applyFrame(frame.subarray(0, size), size)).toBe(true);
      }
    };

    feed(60, 33);
    expect(view.getInterpolationDelayMs()).toBe(100);
    feed(60, 66);
    expect(view.getInterpolationDelayMs()).toBe(132);
    feed(60, 200);
    expect(view.getInterpolationDelayMs()).toBe(250);
  });

  it('O05：窗口未填满时用服务器档位兜底（未知档位用默认 200）', () => {
    const view = createSnapshotView({ now: () => 0 });
    expect(view.getInterpolationDelayMs()).toBe(100);
    view.setSnapshotRateX10(100);
    expect(view.getInterpolationDelayMs()).toBe(200);
    view.setSnapshotRateX10(150);
    expect(view.getInterpolationDelayMs()).toBeCloseTo(133.33, 1);
    view.setSnapshotRateX10(0);
    expect(view.getInterpolationDelayMs()).toBeCloseTo(133.33, 1);
  });
});
