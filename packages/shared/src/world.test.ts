import { describe, expect, it } from 'vitest';

import { CONFIG } from './config/index.ts';
import {
  countActive,
  createWorld,
  despawnEntity,
  getEntity,
  spawnEntity,
  type Entity,
  type SpawnResult,
  type World,
} from './world.ts';

function requireEntity(world: World, id: number): Entity {
  const entity = getEntity(world, id);
  if (entity === undefined) throw new Error('entity missing: ' + String(id));
  return entity;
}

function spawnIds(world: World, kind: 'sheep', count: number): number[] {
  const ids: number[] = [];
  for (let i = 0; i < count; i += 1) {
    const result = spawnEntity(world, kind);
    ids.push(result.ok ? result.id : -1);
  }
  return ids;
}

describe('world 实体池', () => {
  it('createWorld 在 4 个出生点生成玩家实体', () => {
    const world = createWorld(1);
    expect(world.liveCount).toBe(4);
    expect(countActive(world, 'player')).toBe(4);
    expect(world.entities.length).toBe(CONFIG.entity.maxEntities);

    const first = requireEntity(world, 1);
    expect(first.kind).toBe('player');
    expect(first.pos).toEqual({ x: -4.5, y: 0, z: 7 });
    expect(first.hp).toBe(CONFIG.player.maxHp);
    expect(first.maxHp).toBe(CONFIG.player.maxHp);
    expect(first.armor).toBe(CONFIG.player.maxArmor);
    expect(first.team).toBe(0);
    expect(first.aliveMs).toBe(0);

    expect(requireEntity(world, 4).pos).toEqual({ x: 4.5, y: 0, z: 7 });
    expect(getEntity(world, 0)).toBeUndefined();
    expect(getEntity(world, CONFIG.entity.maxEntities + 1)).toBeUndefined();
  });

  it('实体池耗尽时返回失败而不抛异常', () => {
    const world = createWorld(2);
    let spawned = 0;
    let lastFailure: SpawnResult | undefined;
    for (let i = 0; i < CONFIG.entity.maxEntities + 10; i += 1) {
      const result = spawnEntity(world, 'projectile');
      if (result.ok) spawned += 1;
      else lastFailure = result;
    }

    expect(spawned).toBe(CONFIG.entity.maxEntities - 4);
    expect(lastFailure).toEqual({ ok: false, reason: 'entity-pool-exhausted' });
    expect(world.liveCount).toBe(CONFIG.entity.maxEntities);
    expect(world.freeStack.length).toBe(0);

    expect(world.entities.length).toBe(CONFIG.entity.maxEntities);
    expect(requireEntity(world, 1).active).toBe(true);
    expect(requireEntity(world, 4).kind).toBe('player');
    expect(countActive(world)).toBe(CONFIG.entity.maxEntities);
  });

  it('id 分配与复用顺序确定（空闲表后进先出）', () => {
    const scenario = () => {
      const world = createWorld(3);
      const first = spawnIds(world, 'sheep', 3);
      despawnEntity(world, first[1] ?? -1);
      despawnEntity(world, first[2] ?? -1);
      despawnEntity(world, first[0] ?? -1);
      const liveAfterDespawn = world.liveCount;
      const reused = spawnIds(world, 'sheep', 3);
      return { first, reused, liveAfterDespawn };
    };

    const a = scenario();
    const b = scenario();
    expect(a.first).toEqual([5, 6, 7]);
    expect(a.reused).toEqual([5, 7, 6]);
    expect(a.liveAfterDespawn).toBe(4);
    expect(a).toEqual(b);
  });

  it('despawnEntity 对无效 id 返回 false 且不改变世界', () => {
    const world = createWorld(4);
    expect(despawnEntity(world, 0)).toBe(false);
    expect(despawnEntity(world, 9999)).toBe(false);
    expect(despawnEntity(world, 1)).toBe(true);
    expect(despawnEntity(world, 1)).toBe(false);
    expect(world.liveCount).toBe(3);
    expect(world.freeStack.length).toBe(CONFIG.entity.maxEntities - 3);
  });
});

describe('world 活动列表不变式', () => {
  it('activeIds 始终按 id 升序，且与 liveCount 一致', () => {
    const world = createWorld(41);
    expect(world.activeIds).toEqual([1, 2, 3, 4]);

    const a = spawnEntity(world, 'sheep');
    const b = spawnEntity(world, 'sheep');
    if (!a.ok || !b.ok) throw new Error('spawn failed');
    expect(world.activeIds).toEqual([1, 2, 3, 4, 5, 6]);

    despawnEntity(world, 3);
    const reused = spawnEntity(world, 'sheep');
    if (!reused.ok) throw new Error('spawn failed');
    expect(reused.id).toBe(3);
    expect(world.activeIds).toEqual([1, 2, 3, 4, 5, 6]);
    expect(world.liveCount).toBe(6);

    for (let i = 1; i < world.activeIds.length; i += 1) {
      expect(world.activeIds[i]).toBeGreaterThan(world.activeIds[i - 1] ?? 0);
    }
    expect(countActive(world, 'sheep')).toBe(3);
    expect(countActive(world, 'player')).toBe(3);
    expect(getEntity(world, 1.5)).toBeUndefined();
    expect(getEntity(world, -1)).toBeUndefined();
  });
});

describe('阵营默认值', () => {
  it('玩家默认 team 0，羊默认 team 1，显式传入时以传入值为准', () => {
    const world = createWorld(51);
    expect(getEntity(world, 1)?.team).toBe(0);

    const sheep = spawnEntity(world, 'sheep');
    if (!sheep.ok) throw new Error('spawn failed');
    expect(getEntity(world, sheep.id)?.team).toBe(1);

    const pickup = spawnEntity(world, 'pickup');
    if (!pickup.ok) throw new Error('spawn failed');
    expect(getEntity(world, pickup.id)?.team).toBe(0);

    const explicit = spawnEntity(world, 'sheep', 0, 0, 0, 0);
    if (!explicit.ok) throw new Error('spawn failed');
    expect(getEntity(world, explicit.id)?.team).toBe(0);
  });
});
