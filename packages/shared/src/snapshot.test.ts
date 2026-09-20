import { describe, expect, it } from 'vitest';

import { CONFIG, SNAPSHOT_FLAG } from './config/index.ts';
import { stepWorld } from './sim.ts';
import { createSnapshot, snapshotWorld } from './snapshot.ts';
import { createWorld, despawnEntity, getEntity, spawnEntity } from './world.ts';

describe('snapshot 投影', () => {
  it('复用外部缓冲：返回同一对象、实体按 id 升序、快照对象被复用', () => {
    const world = createWorld(31);
    const out = createSnapshot();

    const snapshot = snapshotWorld(world, out);
    expect(snapshot).toBe(out);
    expect(snapshot.entities.map((entity) => entity.id)).toEqual([1, 2, 3, 4]);
    expect(snapshot.tick).toBe(0);
    expect(snapshot.serverTimeMs).toBe(0);
    expect(snapshot.truncated).toBe(false);

    const firstEntity = snapshot.entities[0];
    stepWorld(world, [], 50);
    snapshotWorld(world, out);
    expect(snapshot.tick).toBe(1);
    expect(snapshot.serverTimeMs).toBe(50);
    expect(snapshot.entities[0]).toBe(firstEntity);
    expect(snapshot.entities.length).toBe(4);
  });

  it('hpRatio 与 flags 反映血量与倒地位', () => {
    const world = createWorld(32);
    const player = getEntity(world, 1);
    if (player === undefined) throw new Error('missing player');

    const out = createSnapshot();
    player.hp = 50;
    snapshotWorld(world, out);
    expect(out.entities[0]?.hpRatio).toBeCloseTo(0.5, 12);
    expect(out.entities[0]?.flags).toBe(0);

    player.hp = 0;
    snapshotWorld(world, out);
    expect(out.entities[0]?.hpRatio).toBe(0);
    expect(out.entities[0]?.flags).toBe(SNAPSHOT_FLAG.downed);
  });

  it('超过容量时截断为最近的 N 个并置 truncated', () => {
    const world = createWorld(33);
    const player = getEntity(world, 1);
    if (player === undefined) throw new Error('missing player');
    player.pos.x = 0;
    player.pos.z = 0;

    for (let i = 0; i < 300; i += 1) {
      const result = spawnEntity(world, 'sheep', (i % 20) - 10 + 0.13 * (i % 7), 0, 30 + i * 0.1);
      expect(result.ok).toBe(true);
    }
    expect(world.liveCount).toBe(304);

    const out = createSnapshot();
    snapshotWorld(world, out);
    expect(out.truncated).toBe(true);
    expect(out.entities.length).toBe(CONFIG.net.snapshotMaxEntities);

    const ids = out.entities.map((entity) => entity.id);
    expect(ids).toEqual([...ids].sort((a, b) => a - b));
    expect(new Set(ids).size).toBe(ids.length);
  });
});

describe('snapshot 截断与空世界', () => {
  it('没有玩家时也能截断（锚点退化为场地中心）', () => {
    const world = createWorld(34);
    for (const id of [1, 2, 3, 4]) expect(despawnEntity(world, id)).toBe(true);
    for (let i = 0; i < 260; i += 1) {
      const result = spawnEntity(world, 'sheep', 30 + (i % 5), 0, 30 + (i % 7));
      expect(result.ok).toBe(true);
    }

    const out = createSnapshot();
    const snapshot = snapshotWorld(world, out);
    expect(snapshot.truncated).toBe(true);
    expect(snapshot.entities.length).toBe(CONFIG.net.snapshotMaxEntities);
    expect(snapshot.entities.every((entity) => entity.kind === 'sheep')).toBe(true);
  });

  it('空世界返回 0 个实体', () => {
    const world = createWorld(35);
    for (const id of [1, 2, 3, 4]) expect(despawnEntity(world, id)).toBe(true);
    const out = createSnapshot();
    const snapshot = snapshotWorld(world, out);
    expect(snapshot.truncated).toBe(false);
    expect(snapshot.entities.length).toBe(0);
  });
});

describe('快照缓冲所有权', () => {
  it('不同 out 互不干扰，同一个 out 复用实体对象与数组', () => {
    const world = createWorld(36);
    const a = createSnapshot();
    const b = createSnapshot();
    const arrayA = a.entities;

    snapshotWorld(world, a);
    const entityA = a.entities[0];
    expect(entityA?.id).toBe(1);

    stepWorld(world, [], 50);
    snapshotWorld(world, b);
    expect(b.tick).toBe(1);
    expect(a.tick).toBe(0);
    expect(a.entities).toBe(arrayA);
    expect(a.entities[0]).toBe(entityA);
    expect(b.entities[0]).not.toBe(entityA);

    snapshotWorld(world, a);
    expect(a.tick).toBe(1);
    expect(b.tick).toBe(1);
    expect(a.entities[0]).toBe(entityA);
  });
});
