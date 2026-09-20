import { describe, expect, it } from 'vitest';
import {
  REWIND_LIMIT_MS,
  SERVER_TICK_MS,
  createWorld,
  getEntity,
  spawnEntity,
  stepWorld,
} from '../index.ts';
import {
  createPoseHistory,
  createSampledPose,
  recordPoseHistory,
  samplePoseAgo,
} from './history.ts';

function buildHistory() {
  const world = createWorld(21);
  const spawned = spawnEntity(world, 'player', 5, 0, 0);
  expect(spawned.ok).toBe(true);
  if (!spawned.ok) throw new Error('spawn failed');
  const entity = getEntity(world, spawned.id);
  if (entity === undefined) throw new Error('missing entity');
  const history = createPoseHistory();
  for (let i = 0; i < 6; i += 1) {
    stepWorld(world, [], SERVER_TICK_MS);
    entity.pos.z = 1 + i * 0.5;
    entity.yaw = i * 0.25;
    recordPoseHistory(history, world);
  }
  return { history, pid: spawned.id };
}

describe('姿态历史环', () => {
  it('按回滚时长采样命中槽位', () => {
    const { history, pid } = buildHistory();
    const out = createSampledPose();
    samplePoseAgo(history, 100, pid, out);
    expect(out.found).toBe(true);
    expect(out.clamped).toBe(false);
    expect(out.z).toBeCloseTo(2.5, 6);
  });

  it('相邻槽之间线性插值', () => {
    const { history, pid } = buildHistory();
    const out = createSampledPose();
    samplePoseAgo(history, 75, pid, out);
    expect(out.z).toBeCloseTo(2.75, 6);
    expect(out.yaw).toBeCloseTo(0.875, 6);
  });

  it('超过上限被夹到 200ms 并标记 clamped', () => {
    const { history, pid } = buildHistory();
    const out = createSampledPose();
    samplePoseAgo(history, 500, pid, out);
    expect(out.clamped).toBe(true);
    expect(out.z).toBeCloseTo(3.5 - (REWIND_LIMIT_MS / SERVER_TICK_MS) * 0.5, 6);
  });

  it('0ms 取最新槽；未知 pid 返回 not found', () => {
    const { history, pid } = buildHistory();
    const out = createSampledPose();
    samplePoseAgo(history, 0, pid, out);
    expect(out.z).toBeCloseTo(3.5, 6);
    samplePoseAgo(history, 0, 999, out);
    expect(out.found).toBe(false);
  });

  it('历史不足时回退到最旧槽', () => {
    const world = createWorld(22);
    const spawned = spawnEntity(world, 'player', 0, 0, 0);
    if (!spawned.ok) throw new Error('spawn failed');
    const entity = getEntity(world, spawned.id);
    if (entity === undefined) throw new Error('missing entity');
    const history = createPoseHistory();
    stepWorld(world, [], SERVER_TICK_MS);
    entity.pos.z = 4;
    recordPoseHistory(history, world);
    stepWorld(world, [], SERVER_TICK_MS);
    entity.pos.z = 9;
    recordPoseHistory(history, world);
    const out = createSampledPose();
    samplePoseAgo(history, 200, spawned.id, out);
    expect(out.found).toBe(true);
    expect(out.z).toBe(4);
  });
});
