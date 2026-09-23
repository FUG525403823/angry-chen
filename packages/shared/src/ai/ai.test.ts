import { describe, expect, it } from 'vitest';

import { CONFIG, MAX_ENTITIES } from '../config/index.ts';
import { buildSpatialGrid, createSpatialGrid } from '../sim/spatialGrid.ts';
import {
  SHEEP_AI,
  SHEEP_ORDER,
  SHEEP_STATE,
  SHEEP_STATE_TRANSITIONS,
  type SheepKind,
  type SheepStateCode,
} from '../config/sheep.ts';
import {
  MAX_SPAWNS_PER_TICK,
  MIN_SPAWN_DISTANCE_M,
  WAVE_MAX,
  isBossWave,
  kindAllowedAt,
  waveBaseBudget,
  waveBudget,
} from '../config/waves.ts';
import { createCommand } from '../command.ts';
import { stepWorld } from '../sim.ts';
import { createWorld, getEntity, spawnEntity, type Entity, type World } from '../world.ts';
import {
  createDirectorState,
  nearestPlayerDistanceM,
  planWave,
  selectSpawnPointIndices,
  updateDirector,
} from './director.ts';
import {
  addNeighbor,
  createFlockNeighbors,
  finalizeNeighbors,
  flockForce,
  gatherNeighbors,
} from './flocking.ts';
import { SHEEP_ALERT_MS, applySheepKind } from './sheepBrain.ts';
import { arrive, createSteeringOut, separation } from './steering.ts';

const bareConfig = {
  ...CONFIG,
  arena: { ...CONFIG.arena, playerSpawnPoints: [] },
};

function bareWorld(seed: number): World {
  return createWorld(seed, bareConfig);
}

function addPlayer(world: World, x: number, z: number): Entity {
  const result = spawnEntity(world, 'player', x, 0, z);
  if (!result.ok) throw new Error('player spawn failed');
  const entity = getEntity(world, result.id);
  if (entity === undefined) throw new Error('player missing');
  return entity;
}

function addSheep(world: World, kind: SheepKind, x: number, z: number): Entity {
  const result = spawnEntity(world, 'sheep', x, 0, z);
  if (!result.ok) throw new Error('sheep spawn failed');
  const entity = getEntity(world, result.id);
  if (entity === undefined) throw new Error('sheep missing');
  applySheepKind(entity, kind);
  return entity;
}

function countSheep(world: World): number {
  let count = 0;
  for (const id of world.activeIds) {
    const entity = getEntity(world, id);
    if (entity !== undefined && entity.kind === 'sheep') count += 1;
  }
  return count;
}

describe('steering', () => {
  it('separation 在两点重合时输出非零向量', () => {
    const out = createSteeringOut();
    separation(out, 0, 0, Float64Array.from([0]), Float64Array.from([0]), 1, 3, 1);
    expect(out.x !== 0 || out.z !== 0).toBe(true);
  });

  it('arrive 在目标距离内速度收敛到 0', () => {
    const out = createSteeringOut();
    arrive(out, 0, 0, 0, 0, 4, 2);
    expect(out.x).toBe(0);
    expect(out.z).toBe(0);
  });
});

describe('flocking', () => {
  it('邻居贴脸时分离力把两只羊推开', () => {
    const flock = createFlockNeighbors(SHEEP_AI.maxNeighbors);
    addNeighbor(flock, 0.2, 0, 0, 0);
    finalizeNeighbors(flock);
    const out = createSteeringOut();
    flockForce(out, 0, 0, flock, 1);
    expect(out.x).toBeLessThan(0);
  });

  it('邻居上限固定为 12，超出容量的邻居被忽略', () => {
    const flock = createFlockNeighbors(SHEEP_AI.maxNeighbors);
    for (let i = 0; i < 40; i += 1) addNeighbor(flock, i * 0.1, 0, 0, 0);
    finalizeNeighbors(flock);
    expect(flock.count).toBe(SHEEP_AI.maxNeighbors);
  });
});

describe('flocking 网格查询与有界插入（O04）', () => {
  const CENTER_Z = 20;

  function scatter(world: World, count: number): void {
    for (let i = 0; i < count; i += 1) {
      spawnEntity(world, 'sheep', 0.31 * i - 2.4, 0, CENTER_Z + 0.21 * i - 2.0);
    }
  }

  it('选中的是距离最近的 12 个，而不是最先遇到的 12 个', () => {
    const world = bareWorld(31);
    const self = addSheep(world, 'grunt', 0, CENTER_Z);
    for (let i = 1; i <= 20; i += 1) spawnEntity(world, 'sheep', i * 0.1, 0, CENTER_Z);
    const grid = createSpatialGrid(SHEEP_AI.neighborRadiusM, MAX_ENTITIES);
    buildSpatialGrid(grid, world);
    const flock = createFlockNeighbors(SHEEP_AI.maxNeighbors);
    expect(gatherNeighbors(flock, world, self, grid)).toBe(SHEEP_AI.maxNeighbors);
    expect(flock.distSq[0]).toBeCloseTo(0.01, 10);
    expect(flock.distSq[SHEEP_AI.maxNeighbors - 1]).toBeCloseTo(1.44, 10);
  });

  it('打乱 activeIds 顺序后邻居集合与 flock 力逐位一致', () => {
    const world = bareWorld(32);
    const self = addSheep(world, 'grunt', 0.05, CENTER_Z + 0.05);
    scatter(world, 24);
    const grid = createSpatialGrid(SHEEP_AI.neighborRadiusM, MAX_ENTITIES);
    buildSpatialGrid(grid, world);
    const before = createFlockNeighbors(SHEEP_AI.maxNeighbors);
    gatherNeighbors(before, world, self, grid);
    const forceBefore = createSteeringOut();
    flockForce(forceBefore, self.pos.x, self.pos.z, before, 1);
    expect(before.count).toBeGreaterThan(3);

    world.activeIds.reverse();
    buildSpatialGrid(grid, world);
    const after = createFlockNeighbors(SHEEP_AI.maxNeighbors);
    gatherNeighbors(after, world, self, grid);
    const forceAfter = createSteeringOut();
    flockForce(forceAfter, self.pos.x, self.pos.z, after, 1);

    expect(after.count).toBe(before.count);
    expect(after.centroidX).toBe(before.centroidX);
    expect(after.centroidZ).toBe(before.centroidZ);
    expect(after.averageDirX).toBe(before.averageDirX);
    expect(after.averageDirZ).toBe(before.averageDirZ);
    expect([...after.distSq]).toEqual([...before.distSq]);
    expect(forceAfter.x).toBe(forceBefore.x);
    expect(forceAfter.z).toBe(forceBefore.z);
  });
});

describe('sheep state machine', () => {
  it('迁移矩阵覆盖 13 个状态且终态无出边', () => {
    const states = Object.keys(SHEEP_STATE).length;
    expect(states).toBe(13);
    for (let from = 0; from < states; from += 1) {
      const allowed = SHEEP_STATE_TRANSITIONS[from as SheepStateCode] ?? [];
      for (const to of allowed) {
        expect(to).toBeGreaterThanOrEqual(0);
        expect(to).toBeLessThan(states);
      }
    }
    expect(SHEEP_STATE_TRANSITIONS[SHEEP_STATE.dead].length).toBe(0);
  });

  it('每个非终态都有进入路径与离开路径', () => {
    const states = Object.keys(SHEEP_STATE).length;
    const incoming = new Set<number>();
    for (let from = 0; from < states; from += 1) {
      for (const to of SHEEP_STATE_TRANSITIONS[from as SheepStateCode] ?? []) incoming.add(to);
    }
    for (let state = 0; state < states; state += 1) {
      if (state === SHEEP_STATE.dead) continue;
      expect(incoming.has(state)).toBe(true);
      expect((SHEEP_STATE_TRANSITIONS[state as SheepStateCode] ?? []).length).toBeGreaterThan(0);
    }
  });
});

describe('wave director', () => {
  it('预算公式与人数缩放与需求一致', () => {
    expect(waveBaseBudget(1)).toBe(9);
    expect(waveBaseBudget(5)).toBe(27);
    expect(waveBaseBudget(10)).toBe(56);
    expect(waveBudget(5, 1)).toBe(27);
    expect(waveBudget(5, 4)).toBe(Math.round(27 * 2.05));
  });

  it('组队价格总和不超过预算且遵守首次出现规则', () => {
    for (let wave = 1; wave <= WAVE_MAX; wave += 1) {
      for (let players = 1; players <= 4; players += 1) {
        const state = createDirectorState();
        planWave(state, wave, players);
        let price = 0;
        for (let index = 0; index < SHEEP_ORDER.length; index += 1) {
          const kind = SHEEP_ORDER[index];
          if (kind === undefined) continue;
          const count = state.plan[index] ?? 0;
          expect(kindAllowedAt(kind, wave) || count === 0).toBe(true);
          price += count * (kind === 'grunt' ? 1 : kind === 'ram' ? 3 : kind === 'elite' ? 6 : 20);
        }
        expect(price).toBeLessThanOrEqual(waveBudget(wave, players));
        expect(state.planned).toBeGreaterThan(0);
        expect(state.plan[SHEEP_ORDER.indexOf('king')] ?? 0).toBe(isBossWave(wave) ? 1 : 0);
      }
    }
  });

  it('前两波只有咩咩兵，第三波起有冲撞羊，第五波起有问界羊', () => {
    for (const wave of [1, 2]) {
      const state = createDirectorState();
      planWave(state, wave, 1);
      expect(state.plan[1] ?? 0).toBe(0);
      expect(state.plan[2] ?? 0).toBe(0);
      expect(state.plan[0] ?? 0).toBeGreaterThan(0);
    }
    const third = createDirectorState();
    planWave(third, 3, 1);
    expect(third.plan[1] ?? 0).toBeGreaterThan(0);
    const fifth = createDirectorState();
    planWave(fifth, 5, 1);
    expect(fifth.plan[2] ?? 0).toBeGreaterThan(0);
  });

  it('单帧生成不超过 8 只，生成点距最近玩家大于 15m', () => {
    const world = bareWorld(31);
    const player = addPlayer(world, 0, 0);
    const playerIds = [player.id];
    const state = createDirectorState();
    planWave(state, 10, 4);
    const tick = updateDirector(world, state, 4, world.rng.spawn, playerIds);
    expect(MAX_SPAWNS_PER_TICK).toBe(8);
    expect(tick.spawned).toBeLessThanOrEqual(MAX_SPAWNS_PER_TICK);
    expect(tick.spawned).toBeGreaterThan(0);
    expect(countSheep(world)).toBe(tick.spawned);

    const indices = new Int32Array(3);
    const points = world.config.arena.enemySpawnPoints;
    const count = selectSpawnPointIndices(world, playerIds, world.rng.spawn, indices, 3);
    expect(count).toBeGreaterThan(0);
    for (let i = 0; i < count; i += 1) {
      const point = points[indices[i] ?? 0];
      if (point === undefined) continue;
      expect(nearestPlayerDistanceM(world, point.x, point.z, playerIds)).toBeGreaterThan(
        MIN_SPAWN_DISTANCE_M,
      );
    }
  });
});

describe('sheep ai determinism', () => {
  it('determinism: 同种子 1000 tick 羊群轨迹逐位一致', () => {
    const run = (): number[] => {
      const world = bareWorld(1234);
      addPlayer(world, 0, 0);
      for (let i = 0; i < 6; i += 1) {
        addSheep(world, i % 2 === 0 ? 'grunt' : 'ram', 6 + i, -4 + i);
      }
      const command = createCommand();
      for (let tick = 0; tick < 1000; tick += 1) stepWorld(world, [command], 50, null);
      const samples: number[] = [];
      for (const id of world.activeIds) {
        const entity = getEntity(world, id);
        if (entity === undefined || entity.kind !== 'sheep') continue;
        samples.push(entity.pos.x, entity.pos.z, entity.vel.x, entity.vel.z, entity.state);
      }
      return samples;
    };
    const first = run();
    const second = run();
    expect(first.length).toBeGreaterThan(20);
    expect(first).toEqual(second);
  });
});

describe('king phases', () => {
  it('生命降到 66% 进入阶段 2 并在 8 秒后恰好召唤 4 只咩咩兵', () => {
    const world = bareWorld(99);
    addPlayer(world, 0, 0);
    const king = addSheep(world, 'king', 10, 0);
    const before = countSheep(world);
    expect(before).toBe(1);
    king.hp = king.maxHp * 0.6;
    for (let tick = 0; tick < 200; tick += 1) stepWorld(world, [], 50, null);
    expect(king.ai.phase).toBe(2);
    expect(countSheep(world) - before).toBe(SHEEP_AI.kingSummonCount);
    king.hp = king.maxHp * 0.2;
    for (let tick = 0; tick < 20; tick += 1) stepWorld(world, [], 50, null);
    expect(king.ai.phase).toBe(3);
  });
});

describe('羊警戒→追击（真人试玩：羊不会动）', () => {
  it('警戒计时只武装一次，到期后转追击并真的靠近玩家', () => {
    const world = bareWorld(17);
    const player = addPlayer(world, 0, 20);
    const sheep = addSheep(world, 'grunt', 0, 25);
    const distanceTo = (): number =>
      Math.hypot(sheep.pos.x - player.pos.x, sheep.pos.z - player.pos.z);
    const startDistance = distanceTo();
    expect(startDistance).toBeCloseTo(5, 6);

    stepWorld(world, [], 50, null);
    expect(sheep.state).toBe(SHEEP_STATE.alert);
    const armed = sheep.ai.timerMs;
    expect(armed).toBe(SHEEP_ALERT_MS);

    stepWorld(world, [], 50, null);
    // 旧实现：setSheepState(alert) 同态转换返回 true ⇒ 每 tick 重新武装计时，
    // 警戒永不结束、else 分支（→chase）是死代码，羊定在原地。
    expect(sheep.state).toBe(SHEEP_STATE.alert);
    expect(sheep.ai.timerMs).toBeLessThan(armed);
    expect(distanceTo()).toBeCloseTo(startDistance, 6);

    for (let tick = 0; tick < 12; tick += 1) stepWorld(world, [], 50, null);
    expect(sheep.state).toBe(SHEEP_STATE.chase);
    expect(startDistance - distanceTo()).toBeGreaterThan(0.5);
  });
});
