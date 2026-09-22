import { describe, expect, it } from 'vitest';

import {
  CONFIG,
  MAX_ENTITIES,
  buildSpatialGrid,
  cellIndexOf,
  createSpatialGrid,
  createWorld,
  forEachNeighbor,
  spawnEntity,
  type World,
} from '../index.ts';
import { SHEEP_AI } from '../config/sheep.ts';

const bareConfig = {
  ...CONFIG,
  arena: { ...CONFIG.arena, playerSpawnPoints: [], enemySpawnPoints: [] },
};

function bareWorld(seed: number): World {
  return createWorld(seed, bareConfig);
}

function newGrid() {
  return createSpatialGrid(SHEEP_AI.neighborRadiusM, MAX_ENTITIES);
}

function neighborsOf(grid: ReturnType<typeof newGrid>, x: number, z: number, radius: number) {
  const visited: number[] = [];
  forEachNeighbor(grid, x, z, radius, (index) => visited.push(index));
  return visited;
}

describe('spatialGrid 建表与查询', () => {
  it('空世界：itemCount 为 0，查询不访问任何条目', () => {
    const grid = buildSpatialGrid(newGrid(), bareWorld(1));
    expect(grid.itemCount).toBe(0);
    expect(neighborsOf(grid, 0, 0, SHEEP_AI.neighborRadiusM)).toEqual([]);
  });

  it('只收 player/sheep/pickup，projectile 不入网', () => {
    const world = bareWorld(2);
    const a = spawnEntity(world, 'player', 0, 0, 0);
    const b = spawnEntity(world, 'sheep', 1, 0, 0);
    spawnEntity(world, 'projectile', 0.5, 0, 0);
    const grid = buildSpatialGrid(newGrid(), world);
    expect(grid.itemCount).toBe(2);
    expect(grid.cellItems[0]).toBe(a.ok ? a.id - 1 : -1);
    expect(grid.cellItems[1]).toBe(b.ok ? b.id - 1 : -1);
  });

  it('同格落在同一区间，跨越格边界的实体分到相邻格', () => {
    const world = bareWorld(3);
    const near = spawnEntity(world, 'player', 0, 0, 0);
    const near2 = spawnEntity(world, 'sheep', 0.5, 0, 0);
    const across = spawnEntity(world, 'sheep', 3.5, 0, 0);
    const grid = buildSpatialGrid(newGrid(), world);
    const cellA = cellIndexOf(grid, 0, 0);
    const cellB = cellIndexOf(grid, 3.5, 0);
    expect(cellB).toBe(cellA + 1);
    expect(grid.cellStart[cellA + 1]! - grid.cellStart[cellA]!).toBe(2);
    expect(grid.cellStart[cellB + 1]! - grid.cellStart[cellB]!).toBe(1);
    const ids = [near, near2, across].map((r) => (r.ok ? r.id : 0));
    expect([...grid.cellItems.slice(0, 3)]).toEqual([ids[0]! - 1, ids[1]! - 1, ids[2]! - 1]);
  });

  it('邻格查询覆盖半径内跨格的实体', () => {
    const world = bareWorld(4);
    const a = spawnEntity(world, 'player', 0, 0, 0);
    const b = spawnEntity(world, 'sheep', 3.5, 0, 0);
    const grid = buildSpatialGrid(newGrid(), world);
    const visited = neighborsOf(grid, 2.9, 0, SHEEP_AI.neighborRadiusM);
    expect(visited).toContain(a.ok ? a.id - 1 : -1);
    expect(visited).toContain(b.ok ? b.id - 1 : -1);
  });

  it('超出竞技场范围的坐标夹到边界格，查询仍能命中', () => {
    const world = bareWorld(5);
    const far = spawnEntity(world, 'sheep', 1, 0, 1);
    expect(far.ok).toBe(true);
    if (far.ok) {
      const entity = world.entities[far.id - 1];
      if (entity !== undefined) {
        entity.pos.x = 200;
        entity.pos.z = 200;
      }
    }
    const grid = buildSpatialGrid(newGrid(), world);
    const lastCell = grid.cols * grid.rows - 1;
    expect(cellIndexOf(grid, 200, 200)).toBe(lastCell);
    expect(cellIndexOf(grid, 200, -200)).toBe(grid.cols - 1);
    expect(neighborsOf(grid, 200, 200, SHEEP_AI.neighborRadiusM)).toContain(
      far.ok ? far.id - 1 : -1,
    );
  });

  it('坐标非有限值时不计入网格', () => {
    const world = bareWorld(6);
    spawnEntity(world, 'sheep', 1, 0, 1);
    const broken = spawnEntity(world, 'sheep', 2, 0, 2);
    if (broken.ok) {
      const entity = world.entities[broken.id - 1];
      if (entity !== undefined) entity.pos.x = Number.NaN;
    }
    const grid = buildSpatialGrid(newGrid(), world);
    expect(grid.itemCount).toBe(1);
  });

  it('重建复用同一批数组（零分配），且格内按 id 升序稳定', () => {
    const world = bareWorld(7);
    for (let i = 0; i < 4; i += 1) spawnEntity(world, 'sheep', 0.2 * i, 0, 0);
    const grid = newGrid();
    buildSpatialGrid(grid, world);
    const cellStart = grid.cellStart;
    const cellItems = grid.cellItems;
    const first = grid.itemCount;
    expect(first).toBe(4);
    const sorted = [...cellItems.slice(0, 4)];
    expect(sorted).toEqual([...sorted].sort((a, b) => a - b));

    buildSpatialGrid(grid, world);
    expect(grid.cellStart).toBe(cellStart);
    expect(grid.cellItems).toBe(cellItems);
    expect(grid.itemCount).toBe(first);
  });
});
